#!/usr/bin/env python3
"""Packs fielddata/maptable/mapname.bin: Game Freak's internal name for every map header.

The input JSON maps each MAP_HEADER_* constant to its internal name ("C01" for
Jubilife City, "T01R0101" for the first room of Twinleaf Town's first building,
...). Each name is stored as 16 NUL-padded bytes, in map header order. The game
itself never reads this table.
"""
import argparse
import json
import pathlib
import sys

from convert import from_map_header

NAME_SIZE = 16


def pack(names: dict[str, str]) -> bytes:
    records = {}
    for header, name in names.items():
        encoded = name.encode('ascii')
        if len(encoded) >= NAME_SIZE:
            raise ValueError(f'{header}: "{name}" is longer than {NAME_SIZE - 1} characters')
        records[from_map_header(header)] = encoded.ljust(NAME_SIZE, b'\0')
    if sorted(records) != list(range(len(records))):
        raise ValueError('every map header needs exactly one name')
    return b''.join(records[i] for i in range(len(records)))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('input', type=pathlib.Path)
    parser.add_argument('output', type=pathlib.Path)
    args = parser.parse_args()
    try:
        packed = pack(json.loads(args.input.read_text(encoding='utf-8')))
    except (ValueError, KeyError) as err:
        print(f'{args.input}: error: {err}', file=sys.stderr)
        sys.exit(1)
    args.output.write_bytes(packed)


if __name__ == '__main__':
    main()
