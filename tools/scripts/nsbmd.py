#!/usr/bin/env python3
"""Decompile/recompile NitroSystem models (NSBMD files).

    nsbmd.py unpack IN.nsbmd OUTDIR
    nsbmd.py pack   OUTDIR/<name>.json OUT.nsbmd [--depfile OUT.d]
    nsbmd.py verify FILE.nsbmd ...
    nsbmd.py info   FILE.nsbmd

`unpack` writes
  <name>.json          model metadata: header info (position scale, bounding
                       box; vertex/polygon counts are recomputed), node/material/shape names,
                       the render (SBC) commands as text and the sections that
                       are still opaque (node SRT data, materials, envelope
                       matrices) as hex;
  <model>.<shape>.dl   one text file per shape with its GPU display list, one
                       command per line (see below);
  textures/            the embedded TEX0 block, if any, unpacked by nsbtx.py.

Display list text format
------------------------
Every GX command of the display list on its own line, operands in natural
units.  Vertex commands always list the *absolute* position the vertex ends up
at (model units; multiply by the model's pos_scale for world units), whatever
encoding the command uses:

    BEGIN_VTXS quads            (triangles | quads | triangle_strip | quad_strip)
    TEXCOORD 12.5 3.0           (texels, multiples of 1/16)
    NORMAL 0.0 1.0 0.0          (multiples of 1/512, -1 .. 511/512)
    COLOR 31 31 31              (5-bit r g b)
    VTX_16 -2.0 0.09375 1.375   (any position in [-8, 8), multiples of 1/4096)
    VTX_10 / VTX_XY / VTX_XZ / VTX_YZ / VTX_DIFF x y z
    VTX x y z                   (let the packer choose the smallest encoding)
    MTX_RESTORE 1
    END_VTXS
    NOP                         (explicit no-op slots inside the packed stream)
    CMD 0xNN 0x... ...          (any other command, raw parameter words)

When a position can no longer be expressed with the command written in the
file (e.g. after moving a VTX_DIFF vertex too far), `pack` silently picks an
encoding that can.  Unedited files are re-encoded byte-for-byte.
"""

import argparse
import collections
import glob
import json
import os
import struct
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import nsbtx  # noqa: E402
from nitro_g3d import (  # noqa: E402
    build_container,
    build_dict,
    dict_size,
    name_is_clean,
    name_to_str,
    read_container,
    read_dict,
    str_to_name,
    u16,
    u32,
)

FX = 4096.0

# --------------------------------------------------------------------------
# GX display lists

GX_PARAMS = {
    0x00: 0, 0x10: 1, 0x11: 0, 0x12: 1, 0x13: 1, 0x14: 1, 0x15: 0, 0x16: 16, 0x17: 12,
    0x18: 16, 0x19: 12, 0x1A: 9, 0x1B: 3, 0x1C: 3, 0x20: 1, 0x21: 1, 0x22: 1, 0x23: 2,
    0x24: 1, 0x25: 1, 0x26: 1, 0x27: 1, 0x28: 1, 0x29: 1, 0x2A: 1, 0x2B: 1, 0x30: 1,
    0x31: 1, 0x32: 1, 0x33: 1, 0x34: 32, 0x40: 1, 0x41: 0, 0x50: 1, 0x60: 1, 0x70: 3,
    0x71: 2, 0x72: 1,
}
GX_NAMES = {
    0x00: "NOP", 0x14: "MTX_RESTORE", 0x1B: "MTX_SCALE", 0x20: "COLOR", 0x21: "NORMAL", 0x22: "TEXCOORD",
    0x23: "VTX_16", 0x24: "VTX_10", 0x25: "VTX_XY", 0x26: "VTX_XZ", 0x27: "VTX_YZ", 0x28: "VTX_DIFF",
    0x40: "BEGIN_VTXS", 0x41: "END_VTXS",
}
GX_OPS = {v: k for k, v in GX_NAMES.items()}
PRIMS = ["triangles", "quads", "triangle_strip", "quad_strip"]
VTX_OPS = (0x23, 0x24, 0x25, 0x26, 0x27, 0x28)


class ModelError(Exception):
    pass


def sext(v, bits):
    v &= (1 << bits) - 1
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


def fmt_num(raw, scale):
    v = raw / scale
    return repr(v) if v != int(v) else f"{int(v)}.0"


def parse_num(s, scale, what):
    v = float(s) * scale
    r = round(v)
    if abs(v - r) > 1e-6:
        raise ModelError(f"{what}: {s} is not a multiple of 1/{int(scale)}")
    return int(r)


def apply_vertex(op, p, cur):
    """Position after vertex command (op, params) given the previous position."""
    if op == 0x23:
        return [sext(p[0], 16), sext(p[0] >> 16, 16), sext(p[1], 16)]
    if op == 0x24:
        return [sext(p[0], 10) << 6, sext(p[0] >> 10, 10) << 6, sext(p[0] >> 20, 10) << 6]
    if op == 0x25:
        return [sext(p[0], 16), sext(p[0] >> 16, 16), cur[2]]
    if op == 0x26:
        return [sext(p[0], 16), cur[1], sext(p[0] >> 16, 16)]
    if op == 0x27:
        return [cur[0], sext(p[0], 16), sext(p[0] >> 16, 16)]
    d = [sext(p[0], 10), sext(p[0] >> 10, 10), sext(p[0] >> 20, 10)]
    return [sext(cur[i] + d[i], 16) for i in range(3)]


def decode_dl(dl):
    """Display list bytes -> list of text lines (plus decode statistics)."""
    cmds = []
    pos = 0
    while pos < len(dl):
        if pos + 4 > len(dl):
            raise ModelError("truncated display list")
        ops = dl[pos:pos + 4]
        pos += 4
        for op in ops:
            if op not in GX_PARAMS:
                raise ModelError(f"unknown GX opcode {op:#x}")
            cnt = GX_PARAMS[op]
            cmds.append((op, list(struct.unpack_from(f"<{cnt}I", dl, pos))))
            pos += 4 * cnt
    # The final packet is padded with up to 3 NOPs; the packer re-creates them.
    pad = 0
    while pad < 3 and cmds and cmds[-1][0] == 0:
        cmds.pop()
        pad += 1

    lines = []
    cur = [0, 0, 0]
    for op, p in cmds:
        if op in VTX_OPS:
            new = apply_vertex(op, p, cur)
            stray = (op == 0x23 and p[1] >> 16) or (op in (0x24, 0x28) and p[0] >> 30)
            cur = new
            if not stray:
                lines.append(f"{GX_NAMES[op]} {fmt_num(new[0], FX)} {fmt_num(new[1], FX)} {fmt_num(new[2], FX)}")
                continue
        elif op == 0x40 and p[0] < 4:
            lines.append(f"BEGIN_VTXS {PRIMS[p[0]]}")
            continue
        elif op in (0x41, 0x00):
            lines.append(GX_NAMES[op])
            continue
        elif op == 0x22:
            lines.append(f"TEXCOORD {fmt_num(sext(p[0], 16), 16)} {fmt_num(sext(p[0] >> 16, 16), 16)}")
            continue
        elif op == 0x21 and not p[0] >> 30:
            lines.append("NORMAL " + " ".join(fmt_num(sext(p[0] >> s, 10), 512) for s in (0, 10, 20)))
            continue
        elif op == 0x20 and not p[0] >> 15:
            lines.append(f"COLOR {p[0] & 31} {(p[0] >> 5) & 31} {(p[0] >> 10) & 31}")
            continue
        elif op == 0x14:
            lines.append(f"MTX_RESTORE {p[0]}")
            continue
        # Anything else (or a command with stray high bits) stays raw.
        lines.append(f"CMD {op:#04x}" + "".join(f" {v:#010x}" for v in p))
    return lines


def _check_range(v, lo, hi, what):
    if not lo <= v <= hi:
        raise ModelError(f"{what}: value {v} out of range")
    return v


def encode_dl(lines, where="display list"):
    """Text lines -> (display list bytes, stats dict)."""
    cmds = []
    cur = [0, 0, 0]
    stats = collections.Counter()
    for lineno, raw_line in enumerate(lines, 1):
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        tok = line.split()
        name, args = tok[0].upper(), tok[1:]
        ctx = f"{where}:{lineno}"
        if name == "CMD":
            op = int(args[0], 0)
            params = [int(a, 0) & 0xFFFFFFFF for a in args[1:]]
            if GX_PARAMS.get(op) != len(params):
                raise ModelError(f"{ctx}: opcode {op:#x} takes {GX_PARAMS.get(op)} parameters")
            cmds.append((op, params))
            if op in VTX_OPS:
                cur = apply_vertex(op, params, cur)
                stats["vertices"] += 1
            continue
        if name in ("VTX", "VTX_16", "VTX_10", "VTX_XY", "VTX_XZ", "VTX_YZ", "VTX_DIFF"):
            if len(args) != 3:
                raise ModelError(f"{ctx}: {name} needs x y z")
            new = [parse_num(a, FX, ctx) for a in args]
            for v in new:
                if not -32768 <= v <= 32767:
                    raise ModelError(f"{ctx}: coordinate {v / FX} outside [-8, 8)")
            op, params = _encode_vertex(name, new, cur)
            if name != "VTX" and GX_NAMES[op] != name:
                stats["re-encoded vertices"] += 1
            cmds.append((op, params))
            cur = new
            stats["vertices"] += 1
            continue
        if name == "BEGIN_VTXS":
            if args[0].lower() not in PRIMS:
                raise ModelError(f"{ctx}: unknown primitive {args[0]}")
            cmds.append((0x40, [PRIMS.index(args[0].lower())]))
            continue
        if name in ("END_VTXS", "NOP"):
            cmds.append((GX_OPS[name], []))
            continue
        if name == "TEXCOORD":
            s, t = (parse_num(a, 16, ctx) for a in args)
            _check_range(s, -32768, 32767, ctx)
            _check_range(t, -32768, 32767, ctx)
            cmds.append((0x22, [(s & 0xFFFF) | ((t & 0xFFFF) << 16)]))
            continue
        if name == "NORMAL":
            n = [_check_range(parse_num(a, 512, ctx), -512, 511, ctx) for a in args]
            cmds.append((0x21, [(n[0] & 0x3FF) | ((n[1] & 0x3FF) << 10) | ((n[2] & 0x3FF) << 20)]))
            continue
        if name == "COLOR":
            c = [_check_range(int(a), 0, 31, ctx) for a in args]
            cmds.append((0x20, [c[0] | (c[1] << 5) | (c[2] << 10)]))
            continue
        if name == "MTX_RESTORE":
            cmds.append((0x14, [int(args[0], 0)]))
            continue
        raise ModelError(f"{ctx}: unknown command {tok[0]}")

    out = bytearray()
    for i in range(0, len(cmds), 4):
        group = cmds[i:i + 4]
        ops = [c[0] for c in group] + [0] * (4 - len(group))
        out += bytes(ops)
        for _, params in group:
            out += struct.pack(f"<{len(params)}I", *params)
    return bytes(out), stats


def _encode_vertex(name, new, cur):
    x, y, z = new
    fits10 = all(v % 64 == 0 and -512 <= v // 64 <= 511 for v in new)
    diff = [new[i] - cur[i] for i in range(3)]
    fitsdiff = all(-512 <= d <= 511 for d in diff)

    def enc(op):
        if op == 0x23:
            return [(x & 0xFFFF) | ((y & 0xFFFF) << 16), z & 0xFFFF]
        if op == 0x24:
            return [((x >> 6) & 0x3FF) | (((y >> 6) & 0x3FF) << 10) | (((z >> 6) & 0x3FF) << 20)]
        if op == 0x25:
            return [(x & 0xFFFF) | ((y & 0xFFFF) << 16)]
        if op == 0x26:
            return [(x & 0xFFFF) | ((z & 0xFFFF) << 16)]
        if op == 0x27:
            return [(y & 0xFFFF) | ((z & 0xFFFF) << 16)]
        return [(diff[0] & 0x3FF) | ((diff[1] & 0x3FF) << 10) | ((diff[2] & 0x3FF) << 20)]

    ok = {
        0x23: True,
        0x24: fits10,
        0x25: z == cur[2],
        0x26: y == cur[1],
        0x27: x == cur[0],
        0x28: fitsdiff,
    }
    if name != "VTX" and ok[GX_OPS[name]]:
        op = GX_OPS[name]
    else:
        # Smallest encoding that works (all one-word commands beat VTX_16).
        for op in (0x28, 0x24, 0x25, 0x26, 0x27, 0x23):
            if ok[op]:
                break
    return op, enc(op)


def dl_stats(lines):
    """Vertex/polygon counts and position bounds of a decoded display list."""
    verts = polys = tris = quads = 0
    prim = None
    run = 0
    lo = [None] * 3
    hi = [None] * 3

    def close():
        nonlocal polys, tris, quads
        if prim == "triangles":
            tris += run // 3
        elif prim == "quads":
            quads += run // 4
        elif prim == "triangle_strip" and run >= 3:
            tris += run - 2
        elif prim == "quad_strip" and run >= 4:
            quads += (run - 2) // 2

    for line in lines:
        tok = line.split()
        if not tok:
            continue
        if tok[0] == "BEGIN_VTXS":
            close()
            prim, run = tok[1], 0
        elif tok[0] == "END_VTXS":
            close()
            prim, run = None, 0
        elif tok[0].startswith("VTX"):
            verts += 1
            run += 1
            for i in range(3):
                v = round(float(tok[1 + i]) * FX)
                lo[i] = v if lo[i] is None else min(lo[i], v)
                hi[i] = v if hi[i] is None else max(hi[i], v)
    close()
    polys = tris + quads
    return {"vertices": verts, "polygons": polys, "triangles": tris, "quads": quads, "min": lo, "max": hi}


# --------------------------------------------------------------------------
# SBC (render command) bytecode

SBC_NAMES = {0x00: "NOP", 0x01: "RET", 0x02: "NODE", 0x03: "MTX", 0x04: "MAT", 0x05: "SHP", 0x06: "NODEDESC",
             0x07: "BB", 0x08: "BBY", 0x09: "NODEMIX", 0x0A: "CALLDL", 0x0B: "POSSCALE", 0x0C: "ENVMAP", 0x0D: "PRJMAP"}
SBC_OPS = {v: k for k, v in SBC_NAMES.items()}


def sbc_len(code, pos):
    op = code[pos]
    cmd, flg = op & 0x1F, op >> 5
    if cmd in (0x00, 0x01, 0x0B):
        return 1
    if cmd == 0x02:
        return 3
    if cmd in (0x03, 0x04, 0x05):
        return 2
    if cmd == 0x06:
        return 4 + (1 if flg & 1 else 0) + (1 if flg & 2 else 0)
    if cmd in (0x07, 0x08):
        return 2 + (1 if flg & 1 else 0) + (1 if flg & 2 else 0)
    if cmd == 0x09:
        return 3 + 3 * code[pos + 2]
    if cmd == 0x0A:
        return 9
    if cmd in (0x0C, 0x0D):
        return 3
    raise ModelError(f"unknown SBC opcode {op:#x}")


def decode_sbc(code):
    """SBC bytes -> text lines; stops after RET.  Returns (lines, length)."""
    lines = []
    pos = 0
    while pos < len(code):
        n = sbc_len(code, pos)
        op = code[pos]
        name = SBC_NAMES[op & 0x1F]
        if op >> 5:
            name += f".{op >> 5}"
        lines.append(" ".join([name] + [str(b) for b in code[pos + 1:pos + n]]))
        pos += n
        if op == 0x01:
            break
    return lines, pos


def encode_sbc(lines):
    out = bytearray()
    for line in lines:
        tok = line.split()
        name, _, flg = tok[0].partition(".")
        out.append(SBC_OPS[name] | (int(flg) << 5 if flg else 0))
        out += bytes(int(t) for t in tok[1:])
    return bytes(out)


# --------------------------------------------------------------------------
# Model parsing / building

INFO_FMT = "<BBBBBBBBiiHHHHhhhhhhii"
INFO_SIZE = struct.calcsize(INFO_FMT)  # 0x2C
MDL_HEADER = 20


def parse_model(mdl, mo):
    """Split one model (offset mo inside the MDL0 block) into its parts."""
    size, ofs_sbc, ofs_mat, ofs_shp, ofs_evp = struct.unpack_from("<IIIII", mdl, mo)
    info = struct.unpack_from(INFO_FMT, mdl, mo + MDL_HEADER)
    m = mdl[mo:mo + size]
    node_dict_off = MDL_HEADER + INFO_SIZE
    n_names, n_ents, n_dict_size = read_dict(m, node_dict_off)
    s_names, s_ents, s_dict_size = read_dict(m, ofs_shp)
    shapes = []
    for nm, ent in zip(s_names, s_ents):
        sh = ofs_shp + u32(ent, 0)
        tag, ssize, flag, ofs_dl, size_dl = struct.unpack_from("<HHIII", m, sh)
        shapes.append({"name": nm, "header_off": sh, "tag": tag, "size": ssize, "flag": flag,
                       "dl_off": sh + ofs_dl, "dl": m[sh + ofs_dl:sh + ofs_dl + size_dl]})
    sbc_lines, sbc_used = decode_sbc(m[ofs_sbc:ofs_mat])
    return {
        "m": m, "size": size, "ofs_sbc": ofs_sbc, "ofs_mat": ofs_mat, "ofs_shp": ofs_shp, "ofs_evp": ofs_evp,
        "info": info, "node_names": n_names, "shapes": shapes, "s_names": s_names, "s_ents": s_ents,
        "s_dict_size": s_dict_size, "sbc_lines": sbc_lines, "sbc_used": sbc_used,
    }


def material_info(m, ofs_mat):
    """Material names with their texture/palette names (for documentation and pairing)."""
    tex_off, pal_off = struct.unpack_from("<HH", m, ofs_mat)
    names, ents, _ = read_dict(m, ofs_mat + 4)
    mats = [{"name": name_to_str(n), "texture": None, "palette": None} for n in names]
    for key, off in (("texture", tex_off), ("palette", pal_off)):
        tn, te, _ = read_dict(m, ofs_mat + off)
        for nm, ent in zip(tn, te):
            o, cnt = u16(ent, 0), ent[2]
            for idx in m[ofs_mat + o:ofs_mat + o + cnt]:
                if idx < len(mats):
                    mats[idx][key] = name_to_str(nm)
    return mats


def material_pairings(data):
    """[(texture name, palette name)] for every material of every model in an NSBMD."""
    _, blocks, _ = read_container(data, b"BMD0")
    mdl = blocks[0]
    names, ents, _ = read_dict(mdl, 8)
    out = []
    for ent in ents:
        mo = u32(ent, 0)
        ofs_mat = u32(mdl, mo + 8)
        m = mdl[mo:mo + u32(mdl, mo)]
        out += [(x["texture"], x["palette"]) for x in material_info(m, ofs_mat)]
    return out


def info_to_json(info):
    (sbc_type, scaling_rule, tex_mtx_mode, num_node, num_mat, num_shp, first_unused, dummy,
     pos_scale, inv_pos_scale, nv, npoly, ntri, nquad, bx, by, bz, bw, bh, bd, box_scale, box_inv) = info
    return {
        "sbc_type": sbc_type, "scaling_rule": scaling_rule, "tex_mtx_mode": tex_mtx_mode,
        "first_unused_mtx_stack_id": first_unused, "info_pad": dummy,
        "pos_scale": pos_scale / FX, "inv_pos_scale": inv_pos_scale / FX,
        "bounding_box": {"min": [bx / FX, by / FX, bz / FX], "size": [bw / FX, bh / FX, bd / FX]},
        "box_pos_scale": box_scale / FX, "box_inv_pos_scale": box_inv / FX,
    }


def _fx(v, what):
    r = round(v * FX)
    if abs(v * FX - r) > 1e-6:
        raise ModelError(f"{what}: {v} is not a multiple of 1/4096")
    return r


def unpack_file(path, outdir):
    with open(path, "rb") as f:
        data = f.read()
    version, blocks, _ = read_container(data, b"BMD0")
    os.makedirs(outdir, exist_ok=True)
    stem = os.path.splitext(os.path.basename(path))[0]
    doc = {"container": "BMD0", "version": version, "models": []}

    mdl = blocks[0]
    if mdl[:4] != b"MDL0" or u32(mdl, 4) != len(mdl):
        raise ModelError("bad MDL0 block")
    m_names, m_ents, m_dict_size = read_dict(mdl, 8)
    pos = 8 + m_dict_size
    for nm, ent in zip(m_names, m_ents):
        mo = u32(ent, 0)
        if mo != pos:
            raise ModelError("unexpected gap before model")
        mp = parse_model(mdl, mo)
        pos = mo + mp["size"]
        m = mp["m"]
        mname = name_to_str(nm)
        for raw in [nm] + mp["node_names"] + mp["s_names"]:
            if not name_is_clean(raw):
                raise ModelError(f"name {raw!r} has junk after NUL")

        # Opaque sections between the node dictionary and the shapes.
        node_dict_off = MDL_HEADER + INFO_SIZE
        shp_hdr_end = max([s["header_off"] + 16 for s in mp["shapes"]], default=mp["ofs_shp"] + mp["s_dict_size"])
        first_dl = min([s["dl_off"] for s in mp["shapes"]], default=mp["ofs_evp"])
        if mp["sbc_used"] > mp["ofs_mat"] - mp["ofs_sbc"]:
            raise ModelError("SBC overruns materials")
        model_doc = {
            "name": mname,
            "info": info_to_json(mp["info"]),
            "nodes": [name_to_str(n) for n in mp["node_names"]],
            "node_section": m[node_dict_off:mp["ofs_sbc"]].hex(),
            "render_commands": mp["sbc_lines"],
            "render_commands_pad": m[mp["ofs_sbc"] + mp["sbc_used"]:mp["ofs_mat"]].hex(),
            "materials": material_info(m, mp["ofs_mat"]),
            "material_section": m[mp["ofs_mat"]:mp["ofs_shp"]].hex(),
            "shapes": [],
        }
        if shp_hdr_end != first_dl:
            model_doc["shape_section_pad"] = m[shp_hdr_end:first_dl].hex()
        expect = first_dl
        used = set()
        for s in mp["shapes"]:
            sname = name_to_str(s["name"])
            if s["dl_off"] != expect:
                raise ModelError(f"display list of {sname} is not contiguous")
            expect += len(s["dl"])
            lines = decode_dl(s["dl"])
            # Guarantee exactness: re-encode now and refuse to emit lossy text.
            again, _ = encode_dl(lines)
            if again != s["dl"]:
                raise ModelError(f"display list of {sname} does not round-trip")
            fname = nsbtx._safe_filename(f"{mname}.{sname}", used) + ".dl"
            with open(os.path.join(outdir, fname), "w") as f:
                f.write(f"# model {mname}, shape {sname}: positions in model units "
                        f"(x{model_doc['info']['pos_scale']:g} = world units)\n")
                f.write("\n".join(lines) + "\n")
            sd = {"name": sname, "file": fname, "flag": s["flag"]}
            if s["tag"] or s["size"] != 16:
                sd["tag"], sd["header_size"] = s["tag"], s["size"]
            model_doc["shapes"].append(sd)
        if expect != mp["ofs_evp"]:
            raise ModelError("data between display lists and envelope matrices")
        model_doc["envelope_matrices"] = m[mp["ofs_evp"]:mp["size"]].hex()
        doc["models"].append(model_doc)
    if pos != len(mdl):
        raise ModelError("trailing data in MDL0 block")

    if len(blocks) > 2:
        raise ModelError("more than one TEX0 block")
    if len(blocks) == 2:
        ts = nsbtx.parse_tex0(blocks[1])
        pairs = collections.defaultdict(collections.Counter)
        for tex, pal in material_pairings(data):
            if tex:
                pairs[tex][pal] += 1
        tdir = os.path.join(outdir, "textures")
        nsbtx.unpack_texset(ts, tdir, pairs, json_name="textures.json", extra_json={"container": "TEX0 (embedded)"})
        doc["textures"] = "textures/textures.json"

    json_path = os.path.join(outdir, stem + ".json")
    with open(json_path, "w") as f:
        json.dump(doc, f, indent=2)
        f.write("\n")
    return json_path


def build_model(md, base, deps, options):
    info = md["info"]
    shapes = md["shapes"]
    node_section = bytes.fromhex(md["node_section"])
    sbc = encode_sbc(md["render_commands"]) + bytes.fromhex(md.get("render_commands_pad", ""))
    mat_section = bytes.fromhex(md["material_section"])

    dls = []
    totals = collections.Counter()
    lo, hi = [None] * 3, [None] * 3
    for sd in shapes:
        p = os.path.join(base, sd["file"])
        deps.append(p)
        with open(p) as f:
            lines = f.read().splitlines()
        dl, st = encode_dl(lines, sd["file"])
        if st["re-encoded vertices"]:
            print(f"note: {sd['file']}: {st['re-encoded vertices']} vertices needed a different encoding",
                  file=sys.stderr)
        dls.append(dl)
        ds = dl_stats(decode_dl(dl))
        for k in ("vertices", "polygons", "triangles", "quads"):
            totals[k] += ds[k]
        for i in range(3):
            if ds["min"][i] is not None:
                lo[i] = ds["min"][i] if lo[i] is None else min(lo[i], ds["min"][i])
                hi[i] = ds["max"][i] if hi[i] is None else max(hi[i], ds["max"][i])

    ofs_sbc = MDL_HEADER + INFO_SIZE + len(node_section)
    ofs_mat = ofs_sbc + len(sbc)
    ofs_shp = ofs_mat + len(mat_section)
    s_names = [str_to_name(sd["name"]) for sd in shapes]
    hdr_base = dict_size(len(shapes), 4)
    s_ents = [struct.pack("<I", hdr_base + 16 * i) for i in range(len(shapes))]
    s_dict = build_dict(s_names, s_ents, 4)
    shp_section = bytearray(s_dict)
    pad = bytes.fromhex(md.get("shape_section_pad", ""))
    dl_start = ofs_shp + len(s_dict) + 16 * len(shapes) + len(pad)
    dl_pos = dl_start
    for i, (sd, dl) in enumerate(zip(shapes, dls)):
        hdr_off = ofs_shp + hdr_base + 16 * i
        shp_section += struct.pack("<HHIII", sd.get("tag", 0), sd.get("header_size", 16), sd["flag"],
                                   dl_pos - hdr_off, len(dl))
        dl_pos += len(dl)
    shp_section += pad
    ofs_evp = dl_pos
    evp = bytes.fromhex(md["envelope_matrices"])
    size = ofs_evp + len(evp)

    box = info["bounding_box"]
    if options.get("fit_box") or box == "auto":
        bs = info["box_pos_scale"] / info["pos_scale"]
        bmin = [lo[i] / FX / bs for i in range(3)]
        bsize = [(hi[i] - lo[i]) / FX / bs for i in range(3)]
        box = {"min": [round(v * FX) / FX for v in bmin], "size": [round(v * FX) / FX for v in bsize]}
    # Vertex/polygon counts are always derived from the display lists (this
    # reproduces every original model exactly).
    counts = [totals["vertices"], totals["polygons"], totals["triangles"], totals["quads"]]
    info_bytes = struct.pack(
        INFO_FMT, info["sbc_type"], info["scaling_rule"], info["tex_mtx_mode"], len(md["nodes"]),
        len(md["materials"]), len(shapes), info["first_unused_mtx_stack_id"], info.get("info_pad", 0),
        _fx(info["pos_scale"], "pos_scale"), _fx(info["inv_pos_scale"], "inv_pos_scale"), *counts,
        *[_fx(v, "bounding_box") for v in box["min"]], *[_fx(v, "bounding_box") for v in box["size"]],
        _fx(info["box_pos_scale"], "box_pos_scale"), _fx(info["box_inv_pos_scale"], "box_inv_pos_scale"),
    )
    out = struct.pack("<IIIII", size, ofs_sbc, ofs_mat, ofs_shp, ofs_evp) + info_bytes
    out += node_section + sbc + mat_section + shp_section + b"".join(dls) + evp
    assert len(out) == size
    return out, {"vertices_min": lo, "vertices_max": hi, **totals}


def pack_file(json_path, out_path, depfile=None, options=None):
    options = options or {}
    base = os.path.dirname(os.path.abspath(json_path))
    deps = [os.path.abspath(json_path)]
    with open(json_path) as f:
        doc = json.load(f)
    models = []
    for md in doc["models"]:
        blob, _ = build_model(md, base, deps, options)
        models.append((md["name"], blob))
    dsz = dict_size(len(models), 4)
    pos = 8 + dsz
    ents = []
    for _, blob in models:
        ents.append(struct.pack("<I", pos))
        pos += len(blob)
    mdl = bytearray(b"MDL0" + struct.pack("<I", pos))
    mdl += build_dict([str_to_name(n) for n, _ in models], ents, 4)
    for _, blob in models:
        mdl += blob
    blocks = [bytes(mdl)]
    if doc.get("textures"):
        tjson = os.path.join(base, doc["textures"])
        tdeps = []
        ts, _, warnings = nsbtx.load_texset(tjson, tdeps)
        deps += tdeps
        for w in warnings:
            print(f"warning: {w}", file=sys.stderr)
        blocks.append(nsbtx.build_tex0(ts))
    data = build_container(b"BMD0", doc.get("version", 2), blocks)
    with open(out_path, "wb") as f:
        f.write(data)
    if depfile:
        with open(depfile, "w") as f:
            f.write(out_path + ": " + " ".join(deps) + "\n")


# --------------------------------------------------------------------------
# CLI


def verify(paths):
    stats = collections.Counter()
    failures = []
    gx = collections.Counter()
    with tempfile.TemporaryDirectory(prefix="nsbmd_verify_") as tmp:
        for i, path in enumerate(paths):
            out = os.path.join(tmp, str(i))
            try:
                orig = open(path, "rb").read()
                j = unpack_file(path, out)
                for fn in os.listdir(out):
                    if fn.endswith(".dl"):
                        for line in open(os.path.join(out, fn)):
                            if not line.startswith("#"):
                                gx[line.split()[0] if line.strip() else ""] += 1
                pack_file(j, os.path.join(out, "repacked.nsbmd"))
                new = open(os.path.join(out, "repacked.nsbmd"), "rb").read()
                if new == orig:
                    stats["identical"] += 1
                else:
                    stats["different"] += 1
                    failures.append(path)
            except Exception as e:
                stats["error"] += 1
                failures.append(f"{path}: {type(e).__name__}: {e}")
    print(f"{len(paths)} files: " + ", ".join(f"{k}={v}" for k, v in sorted(stats.items())))
    print("display list commands: " + ", ".join(f"{k}={v}" for k, v in gx.most_common()))
    for f in failures[:50]:
        print("  FAIL", f)
    return not failures


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("unpack")
    p.add_argument("input")
    p.add_argument("outdir")
    p = sub.add_parser("pack")
    p.add_argument("json")
    p.add_argument("output")
    p.add_argument("--depfile")
    p.add_argument("--fit-box", action="store_true", help="recompute the bounding box from the vertices")
    p = sub.add_parser("verify")
    p.add_argument("inputs", nargs="+")
    args = ap.parse_args(argv)
    try:
        if args.cmd == "unpack":
            print(unpack_file(args.input, args.outdir))
        elif args.cmd == "pack":
            pack_file(args.json, args.output, args.depfile, {"fit_box": args.fit_box})
        elif args.cmd == "verify":
            return 0 if verify(args.inputs) else 1
    except (ModelError, nsbtx.TexSetError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
