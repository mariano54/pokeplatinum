"""Find NSBMD/NSBTX assets that may be stored as editable sources.

A binary asset `dir/name.nsbtx` (or `.nsbmd`) can instead live in the repo as
`dir/name/name.json` plus PNG / display-list files, as written by
`nsbtx.py unpack` / `nsbmd.py unpack`; the build packs it back.  Tools that
read the binaries directly call `binary_path()` to get a usable file either
way.
"""

import os
import pathlib
import sys
import threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

_LOCK = threading.Lock()  # tools like the map editor call in from several threads


def source_json(binary: pathlib.Path) -> pathlib.Path:
    return binary.parent / binary.stem / f'{binary.stem}.json'


def exists(binary: pathlib.Path) -> bool:
    return binary.exists() or source_json(binary).exists()


def binary_path(binary: pathlib.Path, cache_dir: pathlib.Path) -> pathlib.Path:
    """The binary itself, or a copy packed from its sources into cache_dir."""
    binary = pathlib.Path(binary)
    if binary.exists():
        return binary
    src = source_json(binary)
    if not src.exists():
        return binary
    out = pathlib.Path(cache_dir) / binary.name
    with _LOCK:
        newest = max(p.stat().st_mtime_ns for p in src.parent.rglob('*') if p.is_file())
        if not out.exists() or out.stat().st_mtime_ns < newest:
            out.parent.mkdir(parents=True, exist_ok=True)
            tmp = out.with_name(f'{out.name}.{os.getpid()}.tmp')
            if binary.suffix == '.nsbmd':
                import nsbmd
                nsbmd.pack_file(str(src), str(tmp))
            else:
                import nsbtx
                nsbtx.pack_file(str(src), str(tmp))
            os.replace(tmp, out)
    return out


def read_bytes(binary: pathlib.Path, cache_dir: pathlib.Path) -> bytes:
    return binary_path(binary, cache_dir).read_bytes()


def without_textures(model: pathlib.Path, cache_dir: pathlib.Path) -> pathlib.Path:
    """A copy of an NSBMD without its embedded TEX0 block (or the file itself if
    it has none).  Map props are drawn with the area's texture set bound in
    place of their own textures (src/overlay005/area_data.c), so viewers must
    not prefer the embedded copies."""
    import nitro_g3d

    data = model.read_bytes()
    version, blocks, _ = nitro_g3d.read_container(data, b'BMD0')
    if len(blocks) < 2:
        return model
    out = pathlib.Path(cache_dir) / model.name
    with _LOCK:
        if not out.exists() or out.stat().st_mtime_ns < model.stat().st_mtime_ns:
            out.parent.mkdir(parents=True, exist_ok=True)
            tmp = out.with_name(f'{out.name}.{os.getpid()}.tmp')
            tmp.write_bytes(nitro_g3d.build_container(b'BMD0', version, blocks[:1]))
            os.replace(tmp, out)
    return out
