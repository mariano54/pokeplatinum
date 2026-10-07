#!/usr/bin/env python3
"""Packs build_model_matshp.dat: the material/shape pairs each map prop model is drawn with.

The input JSON maps every prop model (by its NAIX name, e.g. "honey_tree_nsbmd") to
a list of [materialID, shapeID] pairs; models drawn as a whole have an empty list.
See docs/maps/file_format_specifications.md and MapPropManager_RenderUsing1Mat1Shp.
"""
import argparse
import json
import pathlib
import struct
import sys

NO_IDS = 0xFFFF


def pack(data: dict, model_order: list[str]) -> bytes:
    missing = [m for m in model_order if m not in data]
    unknown = [m for m in data if m not in model_order]
    if missing or unknown:
        raise ValueError(f'models missing from the JSON: {missing[:5]}; unknown models: {unknown[:5]}')

    locators, ids = [], []
    for model in model_order:
        pairs = data[model]
        locators.append(struct.pack('<HH', len(pairs), len(ids) if pairs else NO_IDS))
        ids += [struct.pack('<HH', material, shape) for material, shape in pairs]
    return struct.pack('<HH', len(locators), len(ids)) + b''.join(locators) + b''.join(ids)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('input', type=pathlib.Path)
    parser.add_argument('output', type=pathlib.Path)
    parser.add_argument('--model-order', type=pathlib.Path, required=True, help='map_prop_models.order')
    args = parser.parse_args()

    order = [f.rsplit('.', 1)[0] + '_nsbmd' for f in args.model_order.read_text().split()]
    try:
        packed = pack(json.loads(args.input.read_text(encoding='utf-8')), order)
    except (ValueError, TypeError) as err:
        print(f'{args.input}: error: {err}', file=sys.stderr)
        sys.exit(1)
    args.output.write_bytes(packed)


if __name__ == '__main__':
    main()
