#!/usr/bin/env python3
"""Replaces parts of an existing map with raw files exported by a map editor.

Map editors such as Pokémon DS Map Studio export a map's sections separately: the
terrain model (.nsbmd), the tile collision/behavior table (2048 bytes, often .per)
and the BDHC height data (.bdhc). This copies them into the map's sources, keeping
everything else (e.g. its props) as is:

    tools/scripts/import_map_parts.py res/field/maps/data/map_twinleaf_town.json \\
        --model twinleaf.nsbmd --terrain-attributes twinleaf.per --bdhc twinleaf.bdhc

The result is packed once to validate it. To import a whole land data binary
instead, use tools/scripts/unpack_map_data.py.
"""
import argparse
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / 'jsoncnv'))
import map_data  # noqa: E402
import unpack_map_data  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('map_json', type=pathlib.Path)
    parser.add_argument('--model', type=pathlib.Path, help='terrain model (.nsbmd)')
    parser.add_argument('--terrain-attributes', type=pathlib.Path, help='tile collision/behaviors (2048 bytes)')
    parser.add_argument('--bdhc', type=pathlib.Path, help='BDHC height data')
    parser.add_argument('--models-dir', type=pathlib.Path, default=ROOT / 'res/field/maps/models')
    parser.add_argument('--map-prop-model-naix', type=pathlib.Path,
                        default=ROOT / 'build/res/field/props/models/prop_models.naix')
    parser.add_argument('--tile-behaviors', type=pathlib.Path,
                        default=ROOT / 'include/constants/field/map_tile_behaviors.h')
    args = parser.parse_args()

    behaviors = map_data.parse_tile_behaviors(args.tile_behaviors)
    data = json.loads(args.map_json.read_text(encoding='utf-8'))
    model_path = args.models_dir / data['model']
    model = model_path.read_bytes()

    try:
        if args.terrain_attributes:
            names = {value: name for name, value in behaviors.items()}
            data.update(unpack_map_data.unpack_terrain_attributes(args.terrain_attributes.read_bytes(), names))
        if args.bdhc:
            data['bdhc'] = unpack_map_data.unpack_bdhc(args.bdhc.read_bytes())
        if args.model:
            model = args.model.read_bytes()
            if model[:4] != b'BMD0':
                raise map_data.MapDataError(f'{args.model} is not an NSBMD file')
        text = unpack_map_data.dump_json(data)
        map_data.pack_map_data(json.loads(text), model, behaviors, map_data.parse_naix(args.map_prop_model_naix))
    except (map_data.MapDataError, OSError) as err:
        print(f'error: {err}', file=sys.stderr)
        sys.exit(1)

    args.map_json.write_text(text, encoding='utf-8')
    model_path.write_bytes(model)
    print(f'updated {args.map_json}' + (f' and {model_path}' if args.model else ''))


if __name__ == '__main__':
    main()
