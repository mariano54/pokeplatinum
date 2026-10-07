#!/usr/bin/env python3
"""Decompile/recompile NitroSystem texture sets (NSBTX files and TEX0 blocks).

    nsbtx.py unpack  IN.nsbtx OUTDIR [--models GLOB ...]
    nsbtx.py pack    OUTDIR/<name>.json OUT.nsbtx [--depfile OUT.d]
    nsbtx.py verify  FILE.nsbtx ... [--models GLOB ...]

`unpack` writes one PNG per texture plus a JSON file with the remaining
metadata (names, formats, which palette each texture previews with, ...).
`pack` rebuilds the binary from those files; for unedited files the result is
byte-identical to the original.  `verify` does unpack+pack into a temporary
directory and compares.

PNG conventions
---------------
* palette4/palette16/palette256 textures: 8-bit indexed PNGs whose pixel
  values are the exact texel indices.  The PNG palette is the texture's paired
  palette (the whole palette region, so it may hold more colours than the
  format can address).  Index 0 is transparent in the PNG when the texture has
  "color0_transparent".
* a3i5/a5i3 textures: 8-bit indexed PNGs whose pixel values are the raw texel
  bytes (alpha bits included); the 256-entry PNG palette is generated from the
  paired palette with the matching alpha, so the PNG previews correctly.
* direct textures: RGBA PNGs (alpha is 0 or 255).
* tex4x4 (block-compressed) textures are kept as raw .4x4.bin files (no field
  texture set uses this format).

Palettes
--------
A TEX0 block does not record which palette belongs to which texture (models
pick that per material), so `unpack` pairs them using the material tables of
the models passed with --models, falling back to the "<texture>_pl" / same-name
convention.  Each palette is *owned* by exactly one file: the first indexed
(palette4/16/256) texture PNG paired with it, or else a standalone swatch
"<palette>.pal.png".  `pack` takes the palette colours from the owner's PNG
palette, so recolouring is done by editing that PNG's palette (or its swatch).
Other PNGs that share the palette only use it for preview; `pack` warns if
their palette disagrees with the owner's.
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

import pngio  # noqa: E402
from nitro_g3d import (  # noqa: E402
    align,
    bgr555_to_rgb,
    build_container,
    build_dict,
    dict_size,
    name_is_clean,
    name_to_str,
    read_container,
    read_dict,
    rgb_to_bgr555,
    str_to_name,
    u16,
    u32,
)

FORMAT_NAMES = {1: "a3i5", 2: "palette4", 3: "palette16", 4: "palette256", 5: "tex4x4", 6: "a5i3", 7: "direct"}
FORMAT_IDS = {v: k for k, v in FORMAT_NAMES.items()}
BITS_PER_TEXEL = {1: 8, 2: 2, 3: 4, 4: 8, 5: 2, 6: 8, 7: 16}
INDEXED = (2, 3, 4)
MAX_INDEX = {2: 4, 3: 16, 4: 256, 1: 32, 6: 8}

TEX0_HEADER_SIZE = 0x3C
# Bits of a texture's TEXIMAGE_PARAM that are stored in the TEX0 dictionary.
PARAM_KNOWN = 0xFFFF | (7 << 20) | (7 << 23) | (7 << 26) | (1 << 29)

PREVIEW_FILL = (255, 0, 255, 255)  # palette slots past the end of a palette region


class TexSetError(Exception):
    pass


# ---------------------------------------------------------------------------
# Binary <-> in-memory model


def parse_tex0(blk):
    """Parse a TEX0 block into a plain dict (see build_tex0 for the inverse)."""
    if blk[:4] != b"TEX0":
        raise TexSetError("not a TEX0 block")
    (size, vram_key, tex_size, tex_dict, tex_flag, tex_dummy, tex_ofs,
     vram_key4, c_size, c_dict, c_flag, c_dummy, c_ofs, c_idx_ofs,
     vram_keyp, p_size, p_flag, p_dict, p_ofs) = struct.unpack_from("<IIHHHHIIHHHHIIIHHII", blk, 4)

    header = {
        "size": size, "vram_key": vram_key, "tex_flag": tex_flag, "tex_dummy": tex_dummy,
        "vram_key4": vram_key4, "c_flag": c_flag, "c_dummy": c_dummy, "vram_keyp": vram_keyp,
        "p_flag": p_flag, "tex_dict": tex_dict, "c_dict": c_dict,
    }

    t_names, t_entries, _ = read_dict(blk, tex_dict)
    p_names, p_entries, _ = read_dict(blk, p_dict)

    textures = []
    for nm, ent in zip(t_names, t_entries):
        param, extra = struct.unpack("<II", ent)
        fmt = (param >> 26) & 7
        s, t = (param >> 20) & 7, (param >> 23) & 7
        width, height = 8 << s, 8 << t
        nbytes = width * height * BITS_PER_TEXEL.get(fmt, 0) // 8
        off = (param & 0xFFFF) << 3
        tex = {
            "name": nm, "format": fmt, "width": width, "height": height,
            "color0": (param >> 29) & 1, "extra": extra, "param_rest": param & ~PARAM_KNOWN & 0xFFFFFFFF,
            "offset": off,
        }
        if fmt == 5:
            tex["data"] = blk[c_ofs + off:c_ofs + off + nbytes]
            tex["idx_data"] = blk[c_idx_ofs + off // 2:c_idx_ofs + off // 2 + nbytes // 2]
        elif fmt == 0:
            tex["data"] = b""
        else:
            tex["data"] = blk[tex_ofs + off:tex_ofs + off + nbytes]
        textures.append(tex)

    # Palette regions run from each distinct offset to the next one.
    p_size_bytes = p_size << 3
    offs = sorted({(u16(e, 0) & 0xFFFF) << 3 for e in p_entries})
    palettes = []
    for nm, ent in zip(p_names, p_entries):
        off = u16(ent, 0) << 3
        flag = u16(ent, 2)
        nxt = [o for o in offs if o > off]
        end = nxt[0] if nxt else p_size_bytes
        palettes.append({"name": nm, "flag": flag, "offset": off,
                         "data": blk[p_ofs + off:p_ofs + end]})

    return {"header": header, "textures": textures, "palettes": palettes,
            "raw_sizes": (tex_size << 3, c_size << 3, p_size << 3, len(blk))}


def texture_extra(tex):
    """The extra dictionary word Nintendo's converter writes (original size)."""
    ow, oh = tex.get("orig_width", tex["width"]), tex.get("orig_height", tex["height"])
    fills = ow == tex["width"] and oh == tex["height"]
    return ow | (oh << 11) | (0x80000000 if fills else 0)


def build_tex0(ts):
    """Lay out a TEX0 block canonically (dictionary order, identical data shared)."""
    textures, palettes, hdr = ts["textures"], ts["palettes"], ts.get("header", {})
    tex_dict_off = hdr.get("tex_dict", TEX0_HEADER_SIZE)
    p_dict_off = tex_dict_off + dict_size(len(textures), 8)
    tex_ofs = p_dict_off + dict_size(len(palettes), 4)

    tex_data, c_data, c_idx = bytearray(), bytearray(), bytearray()
    seen, seen4 = {}, {}
    t_entries = []
    for tex in textures:
        fmt = tex["format"]
        if fmt == 5:
            key = (tex["data"], tex["idx_data"])
            if key not in seen4:
                seen4[key] = len(c_data)
                c_data += tex["data"]
                c_idx += tex["idx_data"]
            off = seen4[key]
        else:
            if tex["data"] not in seen:
                seen[tex["data"]] = len(tex_data)
                tex_data += tex["data"]
            off = seen[tex["data"]]
        if off & 7 or off >> 3 > 0xFFFF:
            raise TexSetError(f"texture data offset {off:#x} not encodable")
        s = (tex["width"] // 8).bit_length() - 1
        t = (tex["height"] // 8).bit_length() - 1
        param = (off >> 3) | (s << 20) | (t << 23) | (fmt << 26) | (tex["color0"] << 29) | tex.get("param_rest", 0)
        t_entries.append(struct.pack("<II", param, tex["extra"]))

    pal_data = bytearray()
    seenp = {}
    p_entries = []
    for pal in palettes:
        data = pal["data"]
        if len(data) % 8:
            data = data + bytes(8 - len(data) % 8)
        if data not in seenp:
            seenp[data] = len(pal_data)
            pal_data += data
        p_entries.append(struct.pack("<HH", seenp[data] >> 3, pal["flag"]))

    c_ofs = tex_ofs + len(tex_data)
    c_idx_ofs = c_ofs + len(c_data)
    p_ofs = c_idx_ofs + len(c_idx)
    size = p_ofs + len(pal_data)

    t_dict = build_dict([t["name"] for t in textures], t_entries, 8)
    p_dict = build_dict([p["name"] for p in palettes], p_entries, 4)

    has_pltt4 = any(t["format"] == 2 for t in textures)
    p_flag = hdr.get("p_flag", 0x8000 if has_pltt4 else 0)
    out = bytearray(b"TEX0")
    out += struct.pack(
        "<IIHHHHIIHHHHIIIHHII",
        size, hdr.get("vram_key", 0), len(tex_data) >> 3, tex_dict_off, hdr.get("tex_flag", 0), hdr.get("tex_dummy", 0), tex_ofs,
        hdr.get("vram_key4", 0), len(c_data) >> 3, hdr.get("c_dict", tex_dict_off), hdr.get("c_flag", 0), hdr.get("c_dummy", 0), c_ofs, c_idx_ofs,
        hdr.get("vram_keyp", 0), len(pal_data) >> 3, p_flag, p_dict_off, p_ofs,
    )
    assert len(out) == TEX0_HEADER_SIZE
    out += bytes(tex_dict_off - len(out))
    out += t_dict + p_dict + tex_data + c_data + c_idx + pal_data
    assert len(out) == size
    return bytes(out)


# ---------------------------------------------------------------------------
# Texel codecs


def decode_texels(fmt, data, width, height):
    n = width * height
    if fmt in (1, 4, 6):
        return list(data[:n])
    if fmt == 3:
        out = []
        for b in data:
            out += (b & 15, b >> 4)
        return out[:n]
    if fmt == 2:
        out = []
        for b in data:
            out += (b & 3, (b >> 2) & 3, (b >> 4) & 3, b >> 6)
        return out[:n]
    if fmt == 7:
        return [u16(data, 2 * i) for i in range(n)]
    raise TexSetError(f"cannot decode texture format {fmt}")


def encode_texels(fmt, texels):
    if fmt in (1, 4, 6):
        return bytes(texels)
    if fmt == 3:
        return bytes(texels[i] | (texels[i + 1] << 4) for i in range(0, len(texels), 2))
    if fmt == 2:
        return bytes(texels[i] | (texels[i + 1] << 2) | (texels[i + 2] << 4) | (texels[i + 3] << 6)
                     for i in range(0, len(texels), 4))
    if fmt == 7:
        return b"".join(struct.pack("<H", t) for t in texels)
    raise TexSetError(f"cannot encode texture format {fmt}")


def palette_colors(data):
    return [u16(data, i) for i in range(0, len(data) - 1, 2)]


def alpha_preview_palette(fmt, colors):
    """256-entry preview palette for a3i5/a5i3: entry = (alpha << ibits) | index."""
    ibits = 5 if fmt == 1 else 3
    amax = (1 << (8 - ibits)) - 1
    out = []
    for v in range(256):
        i, a = v & ((1 << ibits) - 1), v >> ibits
        rgb = bgr555_to_rgb(colors[i]) if i < len(colors) else PREVIEW_FILL[:3]
        out.append(rgb + (round(a * 255 / amax),))
    return out


# ---------------------------------------------------------------------------
# Texture <-> palette pairing


def scan_model_pairings(patterns):
    """texture name -> Counter(palette name) from the materials of NSBMD models."""
    pairs = collections.defaultdict(collections.Counter)
    if not patterns:
        return pairs
    import nsbmd  # local import: nsbmd imports this module too

    for pat in patterns:
        for path in sorted(glob.glob(pat)):
            with open(path, "rb") as f:
                data = f.read()
            try:
                for tex, pal in nsbmd.material_pairings(data):
                    if tex:
                        pairs[tex][pal] += 1
            except Exception as e:  # pragma: no cover - diagnostics only
                print(f"warning: {path}: {e}", file=sys.stderr)
    return pairs


def pair_palettes(ts, model_pairs):
    pal_names = [name_to_str(p["name"]) for p in ts["palettes"]]
    pal_set = set(pal_names)
    result = []
    for tex in ts["textures"]:
        name = name_to_str(tex["name"])
        choice = None
        if tex["format"] != 7:
            for pal, _ in (model_pairs.get(name) or collections.Counter()).most_common():
                if pal in pal_set:
                    choice = pal
                    break
            if choice is None:
                # Naming conventions: "<tex>_pl", "<tex>", and animation frames
                # "<base>.N" sharing "<base>_pl" / "<base>".
                base = name.rsplit(".", 1)[0] if "." in name else name
                for cand in (name + "_pl", name, base + "_pl", base):
                    if cand in pal_set:
                        choice = cand
                        break
            if choice is None and len(pal_names) == 1:
                choice = pal_names[0]
        result.append(choice)
    return result


# ---------------------------------------------------------------------------
# unpack


def _safe_filename(name, used):
    base = "".join(c if (c.isalnum() or c in "._-") else "_" for c in name) or "_"
    cand, n = base, 1
    while cand.lower() in used:
        n += 1
        cand = f"{base}~{n}"
    used.add(cand.lower())
    return cand


def unpack_texset(ts, outdir, model_pairs=None, json_name=None, extra_json=None):
    """Write PNGs + JSON for a parsed TEX0 into outdir.  Returns the JSON path."""
    os.makedirs(outdir, exist_ok=True)
    pairing = pair_palettes(ts, model_pairs or {})
    pal_by_name = {name_to_str(p["name"]): p for p in ts["palettes"]}
    used = set()

    # Palette ownership: first indexed texture paired with it, else a swatch.
    owner = {}
    for tex, pal in zip(ts["textures"], pairing):
        if pal and tex["format"] in INDEXED and pal not in owner:
            owner[pal] = tex

    tex_json = []
    tex_files = {}
    for tex, pal in zip(ts["textures"], pairing):
        name = name_to_str(tex["name"])
        if not name_is_clean(tex["name"]):
            raise TexSetError(f"texture name {tex['name']!r} has junk after NUL")
        fmt = tex["format"]
        entry = {"name": name, "format": FORMAT_NAMES.get(fmt, fmt), "width": tex["width"], "height": tex["height"]}
        if fmt in (2, 3, 4, 1, 6):
            entry["color0_transparent"] = bool(tex["color0"])
        elif tex["color0"]:
            entry["color0_transparent"] = True
        ow, oh = tex["extra"] & 0x7FF, (tex["extra"] >> 11) & 0x7FF
        if (ow, oh) != (tex["width"], tex["height"]):
            entry["orig_width"], entry["orig_height"] = ow, oh
        probe = dict(tex, **({"orig_width": ow, "orig_height": oh}))
        if texture_extra(probe) != tex["extra"]:
            entry["extra_param"] = f"{tex['extra']:#010x}"
        if tex["param_rest"]:
            entry["param_rest"] = f"{tex['param_rest']:#010x}"
        if pal:
            entry["palette"] = pal

        fname = _safe_filename(name, used)
        colors = palette_colors(pal_by_name[pal]["data"]) if pal else []
        if fmt in INDEXED:
            texels = decode_texels(fmt, tex["data"], tex["width"], tex["height"])
            need = max(max(texels, default=0) + 1, len(colors), 1)
            plte = [bgr555_to_rgb(c) + (255,) for c in colors]
            if not colors:  # no palette known: grey ramp so the PNG is still viewable
                plte = [(v, v, v, 255) for v in (round(i * 255 / (MAX_INDEX[fmt] - 1)) for i in range(MAX_INDEX[fmt]))]
            while len(plte) < need:
                plte.append(PREVIEW_FILL)
            if tex["color0"]:
                plte[0] = plte[0][:3] + (0,)
            entry["file"] = fname + ".png"
            pngio.write_indexed(os.path.join(outdir, entry["file"]), tex["width"], tex["height"], texels, plte)
        elif fmt in (1, 6):
            texels = decode_texels(fmt, tex["data"], tex["width"], tex["height"])
            entry["file"] = fname + ".png"
            pngio.write_indexed(os.path.join(outdir, entry["file"]), tex["width"], tex["height"], texels,
                                alpha_preview_palette(fmt, colors))
        elif fmt == 7:
            texels = decode_texels(fmt, tex["data"], tex["width"], tex["height"])
            if any((t & 0x8000) == 0 and (t & 0x7FFF) for t in texels):
                entry["transparent_rgb"] = True  # keep colour of alpha=0 texels via raw file
                with open(os.path.join(outdir, fname + ".direct.bin"), "wb") as f:
                    f.write(tex["data"])
                entry["raw_file"] = fname + ".direct.bin"
            px = [bgr555_to_rgb(t) + (255 if t & 0x8000 else 0,) for t in texels]
            entry["file"] = fname + ".png"
            pngio.write_rgba(os.path.join(outdir, entry["file"]), tex["width"], tex["height"], px)
        elif fmt == 5:
            entry["raw_file"] = fname + ".4x4.bin"
            with open(os.path.join(outdir, entry["raw_file"]), "wb") as f:
                f.write(tex["data"] + tex["idx_data"])
        tex_files[id(tex)] = entry.get("file")
        tex_json.append(entry)

    pal_json = []
    for pal in ts["palettes"]:
        name = name_to_str(pal["name"])
        if not name_is_clean(pal["name"]):
            raise TexSetError(f"palette name {pal['name']!r} has junk after NUL")
        colors = palette_colors(pal["data"])
        entry = {"name": name, "colors": len(colors)}
        if pal["flag"] in (0, 1):
            if pal["flag"]:
                entry["four_color"] = True
        else:
            entry["flag"] = pal["flag"]
        hi = [i for i, c in enumerate(colors) if c & 0x8000]
        if hi:
            entry["bit15"] = hi
        if name in owner:
            entry["source"] = tex_files[id(owner[name])]
        else:
            fname = _safe_filename(name, used) + ".pal.png"
            w = min(16, max(1, len(colors)))
            h = max(1, (len(colors) + 15) // 16)
            idx = [i if i < len(colors) else 0 for i in range(w * h)]
            plte = [bgr555_to_rgb(c) for c in colors] or [(0, 0, 0)]
            pngio.write_indexed(os.path.join(outdir, fname), w, h, idx, plte)
            entry["source"] = fname
        pal_json.append(entry)

    doc = {}
    if extra_json:
        doc.update(extra_json)
    hdr = ts["header"]
    derived_pflag = 0x8000 if any(t["format"] == 2 for t in ts["textures"]) else 0
    odd = {k: v for k, v in hdr.items() if k not in ("size", "tex_dict", "c_dict", "p_flag") and v}
    if hdr["p_flag"] != derived_pflag:
        odd["p_flag"] = hdr["p_flag"]
    if hdr["tex_dict"] != TEX0_HEADER_SIZE:
        odd["tex_dict"] = hdr["tex_dict"]
    if hdr["c_dict"] != hdr["tex_dict"]:
        odd["c_dict"] = hdr["c_dict"]
    if odd:
        doc["tex0_header"] = odd
    doc["textures"] = tex_json
    doc["palettes"] = pal_json
    json_path = os.path.join(outdir, json_name or "textures.json")
    with open(json_path, "w") as f:
        json.dump(doc, f, indent=2)
        f.write("\n")
    return json_path


def unpack_file(path, outdir, model_pairs=None):
    with open(path, "rb") as f:
        data = f.read()
    version, blocks, filesize = read_container(data, b"BTX0")
    if len(blocks) != 1:
        raise TexSetError(f"{path}: expected exactly one TEX0 block, found {len(blocks)}")
    ts = parse_tex0(blocks[0])
    stem = os.path.splitext(os.path.basename(path))[0]
    extra = {"container": "BTX0"}
    if version != 1:
        extra["version"] = version
    return unpack_texset(ts, outdir, model_pairs, json_name=stem + ".json", extra_json=extra)


# ---------------------------------------------------------------------------
# pack


def _parse_int(v):
    return int(v, 0) if isinstance(v, str) else int(v)


def load_texset(json_path, deps=None):
    """Read a JSON + PNGs back into the in-memory model used by build_tex0."""
    base = os.path.dirname(os.path.abspath(json_path))
    with open(json_path) as f:
        doc = json.load(f)
    if deps is not None:
        deps.append(os.path.abspath(json_path))

    images = {}

    def image(fname):
        if fname not in images:
            p = os.path.join(base, fname)
            if deps is not None:
                deps.append(p)
            images[fname] = pngio.read(p)
        return images[fname]

    warnings = []
    pal_by_name = {}
    palettes = []
    for pe in doc["palettes"]:
        n = pe["colors"]
        img = image(pe["source"])
        if img.mode != "P":
            raise TexSetError(f"{pe['source']}: palette owner must be an indexed PNG")
        if len(img.palette) < n:
            raise TexSetError(f"{pe['source']}: PNG palette has {len(img.palette)} colours, palette "
                              f"{pe['name']} needs {n}")
        colors = [rgb_to_bgr555(c) for c in img.palette[:n]]
        for i in pe.get("bit15", []):
            colors[i] |= 0x8000
        flag = pe.get("flag", 1 if pe.get("four_color") else 0)
        pal = {"name": str_to_name(pe["name"]), "flag": flag,
               "data": b"".join(struct.pack("<H", c) for c in colors), "colors": colors,
               "source": pe["source"]}
        palettes.append(pal)
        pal_by_name[pe["name"]] = pal

    textures = []
    for te in doc["textures"]:
        fmt = FORMAT_IDS[te["format"]] if isinstance(te["format"], str) else te["format"]
        w, h = te["width"], te["height"]
        if w not in (8, 16, 32, 64, 128, 256, 512, 1024) or h not in (8, 16, 32, 64, 128, 256, 512, 1024):
            raise TexSetError(f"{te['name']}: width/height must be powers of two from 8 to 1024")
        tex = {"name": str_to_name(te["name"]), "format": fmt, "width": w, "height": h,
               "color0": 1 if te.get("color0_transparent") else 0,
               "param_rest": _parse_int(te.get("param_rest", 0))}
        if "orig_width" in te:
            tex["orig_width"], tex["orig_height"] = te["orig_width"], te["orig_height"]
        tex["extra"] = _parse_int(te["extra_param"]) if "extra_param" in te else texture_extra(tex)
        pal = pal_by_name.get(te.get("palette"))

        if fmt == 5 or (fmt == 7 and "raw_file" in te and not os.path.exists(os.path.join(base, te.get("file", "")))):
            p = os.path.join(base, te["raw_file"])
            if deps is not None:
                deps.append(p)
            raw = open(p, "rb").read()
            n = w * h * BITS_PER_TEXEL[fmt] // 8
            tex["data"], tex["idx_data"] = raw[:n], raw[n:]
            textures.append(tex)
            continue

        img = image(te["file"])
        if (img.width, img.height) != (w, h):
            raise TexSetError(f"{te['file']}: image is {img.width}x{img.height}, JSON says {w}x{h}")
        if fmt == 7:
            texels = [rgb_to_bgr555(p) | (0x8000 if p[3] >= 128 else 0) for p in img.pixels]
            if "raw_file" in te:  # keep RGB of fully transparent texels if the PNG still matches
                p = os.path.join(base, te["raw_file"])
                raw = decode_texels(7, open(p, "rb").read(), w, h)
                if [t if t & 0x8000 else 0 for t in raw] == [t if t & 0x8000 else 0 for t in texels]:
                    texels = raw
        elif img.mode == "P":
            texels = list(img.pixels)
            if fmt in INDEXED and pal is not None and pal["source"] != te["file"]:
                plte = [rgb_to_bgr555(c) for c in img.palette[:len(pal["colors"])]]
                if plte != [c & 0x7FFF for c in pal["colors"][:len(plte)]]:
                    warnings.append(f"{te['file']}: PNG palette differs from palette {te['palette']} "
                                    f"(owned by {pal['source']}); edit that file to recolour")
        else:
            texels = _quantize(img, fmt, pal, te)
        limit = 256 if fmt in (1, 6) else MAX_INDEX[fmt]
        bad = [t for t in texels if t >= limit]
        if bad:
            raise TexSetError(f"{te['file']}: pixel index {bad[0]} out of range for {te['format']}")
        tex["data"] = encode_texels(fmt, texels)
        textures.append(tex)

    hdr = {}
    for k, v in doc.get("tex0_header", {}).items():
        hdr[k] = _parse_int(v)
    return {"header": hdr, "textures": textures, "palettes": palettes}, doc, warnings


def _quantize(img, fmt, pal, te):
    """Map an RGBA PNG back to palette indices (exact colour matches only)."""
    if pal is None:
        raise TexSetError(f"{te['file']}: truecolour PNG for a paletted texture without a palette")
    lookup = {}
    for i, c in enumerate(pal["colors"][:MAX_INDEX[fmt]]):
        lookup.setdefault(bgr555_to_rgb(c & 0x7FFF), i)
    out = []
    for p in img.pixels:
        if p[3] < 128 and te.get("color0_transparent"):
            out.append(0)
            continue
        key = tuple(v | (v >> 5) for v in (p[0] & 0xF8, p[1] & 0xF8, p[2] & 0xF8))
        if key not in lookup:
            raise TexSetError(f"{te['file']}: colour {p[:3]} is not in palette {te['palette']}; "
                              "save the PNG as indexed or use palette colours only")
        out.append(lookup[key])
    return out


def pack_file(json_path, out_path, depfile=None):
    deps = []
    ts, doc, warnings = load_texset(json_path, deps)
    for w in warnings:
        print(f"warning: {w}", file=sys.stderr)
    blob = build_tex0(ts)
    data = build_container(b"BTX0", doc.get("version", 1), [blob])
    with open(out_path, "wb") as f:
        f.write(data)
    if depfile:
        with open(depfile, "w") as f:
            f.write(out_path.replace(" ", "\\ ") + ": " + " ".join(d.replace(" ", "\\ ") for d in deps) + "\n")


def edit_palette(json_path, pal_name, sets):
    """Print a palette (from its owner PNG) and optionally change entries.

    sets: ["5=#a03030", ...].  Only the owner PNG's PLTE chunk changes; pixel
    data is untouched.  Other PNGs previewing the same palette are updated too
    so they keep showing what the game will draw.  json_path may also be a
    plain indexed PNG (e.g. a sprite sheet packed by nitrobtx), whose own
    palette is then edited; pal_name is then the first INDEX=#RRGGBB."""
    if json_path.lower().endswith(".png"):
        if pal_name:
            sets = [pal_name] + list(sets)
        base, pal_name = os.path.dirname(os.path.abspath(json_path)), os.path.basename(json_path)
        img = pngio.read(json_path)
        pe = {"source": os.path.basename(json_path), "colors": len(img.palette)}
        targets = [pe["source"]]
    else:
        base = os.path.dirname(os.path.abspath(json_path))
        with open(json_path) as f:
            doc = json.load(f)
        pe = next((p for p in doc["palettes"] if p["name"] == pal_name), None)
        if pe is None:
            raise TexSetError(f"no palette named {pal_name}")
        targets = [pe["source"]] + [t["file"] for t in doc["textures"]
                                    if t.get("palette") == pal_name and t.get("file") and t["file"] != pe["source"]
                                    and t["format"] in ("palette4", "palette16", "palette256")]
    changes = {}
    for s in sets:
        idx, _, col = s.partition("=")
        col = col.lstrip("#")
        changes[int(idx)] = (int(col[0:2], 16), int(col[2:4], 16), int(col[4:6], 16))
    for k, fname in enumerate(targets):
        path = os.path.join(base, fname)
        img = pngio.read(path)
        if k == 0:
            for i, c in enumerate(img.palette[:pe["colors"]]):
                mark = f"  -> #{''.join(f'{v:02x}' for v in changes[i])}" if i in changes else ""
                print(f"{pal_name}[{i:3d}] #{c[0]:02x}{c[1]:02x}{c[2]:02x}{mark}")
        if not changes:
            break
        pal = list(img.palette)
        for i, rgb in changes.items():
            if i >= pe["colors"]:
                raise TexSetError(f"{pal_name} has only {pe['colors']} colours")
            # Snap to what the hardware can show (5 bits per channel).
            snapped = tuple((v >> 3 << 3) | (v >> 5) for v in rgb)
            pal[i] = snapped + (pal[i][3],)
        pngio.write_indexed(path, img.width, img.height, img.pixels, pal, img.bit_depth)
        if changes:
            print(f"updated {fname}")


# ---------------------------------------------------------------------------
# CLI


def verify(paths, model_pairs, quiet=False):
    stats = collections.Counter()
    failures = []
    formats = collections.Counter()
    with tempfile.TemporaryDirectory(prefix="nsbtx_verify_") as tmp:
        for i, path in enumerate(paths):
            out = os.path.join(tmp, str(i))
            try:
                orig = open(path, "rb").read()
                ts = parse_tex0(read_container(orig, b"BTX0")[1][0])
                for t in ts["textures"]:
                    formats[FORMAT_NAMES.get(t["format"], t["format"])] += 1
                j = unpack_file(path, out, model_pairs)
                pack_file(j, os.path.join(out, "repacked.nsbtx"))
                new = open(os.path.join(out, "repacked.nsbtx"), "rb").read()
                if new == orig:
                    stats["identical"] += 1
                else:
                    stats["different"] += 1
                    failures.append(path)
            except Exception as e:
                stats["error"] += 1
                failures.append(f"{path}: {e}")
    if not quiet:
        print(f"{len(paths)} files: " + ", ".join(f"{k}={v}" for k, v in sorted(stats.items())))
        print("texture formats: " + ", ".join(f"{k}={v}" for k, v in sorted(formats.items())))
        for f in failures:
            print("  FAIL", f)
    return not failures


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("unpack")
    p.add_argument("input")
    p.add_argument("outdir")
    p.add_argument("--models", action="append", default=[], help="glob of NSBMD files used to pair palettes")
    p = sub.add_parser("pack")
    p.add_argument("json")
    p.add_argument("output")
    p.add_argument("--depfile")
    p = sub.add_parser("verify")
    p.add_argument("inputs", nargs="+")
    p.add_argument("--models", action="append", default=[])
    p = sub.add_parser("palette", help="print a palette, or recolour entries with INDEX=#rrggbb")
    p.add_argument("json", help="unpacked texture set JSON, or a plain indexed PNG")
    p.add_argument("palette", nargs="?", default="")
    p.add_argument("set", nargs="*", metavar="INDEX=#RRGGBB")
    args = ap.parse_args(argv)

    try:
        if args.cmd == "unpack":
            print(unpack_file(args.input, args.outdir, scan_model_pairings(args.models)))
        elif args.cmd == "pack":
            pack_file(args.json, args.output, args.depfile)
        elif args.cmd == "verify":
            return 0 if verify(args.inputs, scan_model_pairings(args.models)) else 1
        elif args.cmd == "palette":
            edit_palette(args.json, args.palette, args.set)
    except TexSetError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
