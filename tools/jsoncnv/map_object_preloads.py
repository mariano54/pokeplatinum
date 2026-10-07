#!/usr/bin/env python3
"""Packs a NARC of map object graphics preload lists (fielddata/mm_list/move_model_list.narc).

The input JSON is a list of lists of OBJ_EVENT_GFX_* names. A map header's
preloadedMapObjectsArchiveID picks one list, whose graphics are loaded with the
field map (see FetchMapObjectsToPreload in src/overlay005/fieldmap.c). Each list
is stored as u16 IDs ending with 0xFFFF and padded with zeros to 4 bytes.
"""
import argparse
import json
import pathlib
import struct
import sys

from convert import from_object_event_gfx

SENTINEL = 0xFFFF
MAX_MAP_OBJECTS_TO_PRELOAD = 24


def pack_list(names: list[str]) -> bytes:
    if len(names) >= MAX_MAP_OBJECTS_TO_PRELOAD:
        raise ValueError(f'a list holds at most {MAX_MAP_OBJECTS_TO_PRELOAD - 1} graphics, got {len(names)}')
    ids = [from_object_event_gfx(name) for name in names] + [SENTINEL]
    if len(ids) % 2:
        ids.append(0)
    return struct.pack(f'<{len(ids)}H', *ids)


def pack_narc(members: list[bytes]) -> bytes:
    offsets, start = [], 0
    for member in members:
        offsets.append((start, start + len(member)))
        start += len(member)
    btaf = b'BTAF' + struct.pack('<IHH', 12 + 8 * len(members), len(members), 0) + b''.join(struct.pack('<II', *o) for o in offsets)
    btnf = b'BTNF' + struct.pack('<IIHH', 16, 4, 0, 1)
    gmif = b'GMIF' + struct.pack('<I', 8 + start) + b''.join(members)
    size = 16 + len(btaf) + len(btnf) + len(gmif)
    return b'NARC' + struct.pack('<HHIHH', 0xFFFE, 0x0100, size, 16, 3) + btaf + btnf + gmif


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('input', type=pathlib.Path)
    parser.add_argument('output', type=pathlib.Path)
    args = parser.parse_args()
    try:
        lists = json.loads(args.input.read_text(encoding='utf-8'))
        narc = pack_narc([pack_list(names) for names in lists])
    except (ValueError, KeyError) as err:
        print(f'{args.input}: error: {err}', file=sys.stderr)
        sys.exit(1)
    args.output.write_bytes(narc)


if __name__ == '__main__':
    main()
