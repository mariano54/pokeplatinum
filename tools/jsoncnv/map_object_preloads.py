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
from narc import pack_narc

SENTINEL = 0xFFFF
MAX_MAP_OBJECTS_TO_PRELOAD = 24


def pack_list(names: list[str]) -> bytes:
    if len(names) >= MAX_MAP_OBJECTS_TO_PRELOAD:
        raise ValueError(f'a list holds at most {MAX_MAP_OBJECTS_TO_PRELOAD - 1} graphics, got {len(names)}')
    ids = [from_object_event_gfx(name) for name in names] + [SENTINEL]
    if len(ids) % 2:
        ids.append(0)
    return struct.pack(f'<{len(ids)}H', *ids)


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
