#!/usr/bin/env python3
"""Packs the overworld's animated map textures (water, flowers, lamps...).

    texture_animations.py narc texture_animations.json fldtanime.narc --textures-dir DIR
    texture_animations.py legacy legacy_texture_animations.json OUTDIR

`narc` builds data/fldtanime.narc, read by src/overlay005/texture_resource_manager.c.
Member 0 lists, for each animated texture of the area's map texture set, its name
and up to 17 [frame, duration] steps (duration in frames, the list loops). Members
1..N hold the frames: one NSBTX per texture, `<name>.nsbtx` in DIR.

`legacy` builds data/fld_anime0.bin..: an older version of the same data from
Diamond and Pearl (a texture file path plus up to 9 steps), which Platinum only
mentions in a debug string.
"""
import argparse
import json
import pathlib
import struct
import sys

from narc import pack_narc

NAME_SIZE = 16
MAX_STEPS = 18  # including the terminator
LEGACY_PATH_SIZE = 32
LEGACY_MAX_STEPS = 10  # including the terminator


def padded(text: str, size: int) -> bytes:
    encoded = text.encode('ascii')
    if len(encoded) >= size:
        raise ValueError(f'"{text}" is longer than {size - 1} characters')
    return encoded.ljust(size, b'\0')


def pack_steps(frames: list, max_steps: int, fmt: str, end: int) -> bytes:
    if len(frames) >= max_steps:
        raise ValueError(f'at most {max_steps - 1} animation steps, got {len(frames)}')
    return b''.join(struct.pack(fmt, frame, duration) for frame, duration in frames) + struct.pack(fmt, end, end)


def pack_table(animations: list[dict]) -> bytes:
    records = []
    for animation in animations:
        steps = pack_steps(animation['frames'], MAX_STEPS, '<BB', 0xFF)
        records.append(padded(animation['texture'], NAME_SIZE) + steps.ljust(2 * MAX_STEPS, b'\xff'))
    return struct.pack('<I', len(animations)) + b''.join(records)


def pack_legacy(animation: dict) -> bytes:
    steps = pack_steps(animation['frames'], LEGACY_MAX_STEPS, '<HH', 0xFFFF)
    return padded(animation['path'], LEGACY_PATH_SIZE) + steps.ljust(4 * LEGACY_MAX_STEPS, b'\0')


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('mode', choices=['narc', 'legacy'])
    parser.add_argument('input', type=pathlib.Path)
    parser.add_argument('output', type=pathlib.Path)
    parser.add_argument('--textures-dir', type=pathlib.Path)
    args = parser.parse_args()
    try:
        animations = json.loads(args.input.read_text(encoding='utf-8'))
        if args.mode == 'narc':
            textures = [(args.textures_dir / f'{a["texture"]}.nsbtx').read_bytes() for a in animations]
            args.output.write_bytes(pack_narc([pack_table(animations)] + textures))
        else:
            for i, animation in enumerate(animations):
                (args.output / f'fld_anime{i}.bin').write_bytes(pack_legacy(animation))
    except (ValueError, KeyError, OSError) as err:
        print(f'{args.input}: error: {err}', file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()
