#!/usr/bin/env python3
"""Converts land data binaries (land_data.narc members) into editable map sources.

This is how res/field/maps was produced from the original ROM data. It is also the
way to import a map exported by an external editor (e.g. Pokémon DS Map Studio):

    tools/scripts/unpack_map_data.py \\
        --map-prop-model-naix build/res/field/props/models/prop_models.naix \\
        --json-dir res/field/maps/data --models-dir res/field/maps/models \\
        path/to/map_data_123.bin

Every converted file is packed again with tools/jsoncnv/map_data.py and compared
byte-for-byte with the input, so a successful run guarantees a lossless conversion.
"""
import argparse
import json
import pathlib
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / 'jsoncnv'))
import map_data  # noqa: E402

# Mnemonic legend characters, tried in order; anything else gets the first free
# character of FALLBACK_CHARS. '#' is avoided so the grids never look like collision.
PREFERRED_CHARS = {
    'TILE_BEHAVIOR_NONE': '.',
    'TILE_BEHAVIOR_TALL_GRASS': 'g',
    'TILE_BEHAVIOR_VERY_TALL_GRASS': 'G',
    'TILE_BEHAVIOR_CAVE_FLOOR': 'c',
    'TILE_BEHAVIOR_OLD_CHATEAU_FLOOR': 'o',
    'TILE_BEHAVIOR_MOUNTAIN_FLOOR': 'm',
    'TILE_BEHAVIOR_WATER_RIVER': 'r',
    'TILE_BEHAVIOR_WATERFALL': 'F',
    'TILE_BEHAVIOR_WATER_SEA': 'w',
    'TILE_BEHAVIOR_PUDDLE': 'p',
    'TILE_BEHAVIOR_SHALLOW_WATER': 'h',
    'TILE_BEHAVIOR_PUDDLE_NO_SPLASHING': 'P',
    'TILE_BEHAVIOR_ICE': 'i',
    'TILE_BEHAVIOR_SAND': 's',
    'TILE_BEHAVIOR_REFLECTIVE': 'R',
    'TILE_BEHAVIOR_JUMP_EAST': '>',
    'TILE_BEHAVIOR_JUMP_WEST': '<',
    'TILE_BEHAVIOR_JUMP_NORTH': '^',
    'TILE_BEHAVIOR_JUMP_SOUTH': 'v',
    'TILE_BEHAVIOR_DOOR': 'D',
    'TILE_BEHAVIOR_WARP_PANEL': 'X',
    'TILE_BEHAVIOR_TABLE': 't',
    'TILE_BEHAVIOR_PC': 'C',
    'TILE_BEHAVIOR_TOWN_MAP': 'M',
    'TILE_BEHAVIOR_TV': 'T',
    'TILE_BEHAVIOR_BERRY_PATCH': 'b',
    'TILE_BEHAVIOR_SNOW_SHALLOW': 'n',
    'TILE_BEHAVIOR_SNOW_DEEP': 'N',
    'TILE_BEHAVIOR_MUD': 'u',
    'TILE_BEHAVIOR_MUD_DEEP': 'U',
    'TILE_BEHAVIOR_BOOKSHELF_1': 'B',
    'TILE_BEHAVIOR_BOOKSHELF_2': 'B',
    'TILE_BEHAVIOR_TRASH_CAN': 'x',
    'TILE_BEHAVIOR_BIKE_PARKING': 'k',
    'TILE_BEHAVIOR_ESCALATOR': 'E',
    'TILE_BEHAVIOR_BRIDGE': '=',
    'TILE_BEHAVIOR_BRIDGE_START': '+',
}
FALLBACK_CHARS = 'abdefjlqyzAHIJKLOQSVWYZ0123456789!$%&*-/:;?@_|~()[]{},`\''


def assign_legend(behavior_values: set[int], names_by_value: dict[int, str]) -> dict[int, str]:
    legend, taken = {}, set()
    for value in sorted(behavior_values):
        name = names_by_value[value]
        char = PREFERRED_CHARS.get(name)
        if char is None or char in taken:
            char = next(c for c in FALLBACK_CHARS if c not in taken)
        legend[value] = char
        taken.add(char)
    return legend


def unpack_bdhc(blob: bytes) -> dict:
    if blob[:4] != b'BDHC':
        raise map_data.MapDataError('BDHC section does not start with the BDHC magic')
    n_points, n_normals, n_constants, n_plates, n_strips, n_access = struct.unpack_from('<6H', blob, 4)
    offset = 16
    points = [struct.unpack_from('<2i', blob, offset + 8 * i) for i in range(n_points)]
    offset += 8 * n_points
    normals = [struct.unpack_from('<3i', blob, offset + 12 * i) for i in range(n_normals)]
    offset += 12 * n_normals
    constants = [struct.unpack_from('<i', blob, offset + 4 * i)[0] for i in range(n_constants)]
    offset += 4 * n_constants
    plates = [struct.unpack_from('<4H', blob, offset + 8 * i) for i in range(n_plates)]

    # Value-equal table entries stored more than once get increasing "slots".
    def slots(table):
        seen, result = {}, []
        for value in table:
            result.append(seen.get(value, 0))
            seen[value] = seen.get(value, 0) + 1
        return result

    normal_slots, constant_slots = slots(normals), slots(constants)
    out = []
    for first, second, normal, constant in plates:
        plate = {
            'rect': [map_data.from_fx32(v) for v in (*points[first], *points[second])],
            'normal': [map_data.from_fx32(v) for v in normals[normal]],
            'constant': map_data.from_fx32(constants[constant]),
        }
        if normal_slots[normal]:
            plate['normalSlot'] = normal_slots[normal]
        if constant_slots[constant]:
            plate['constantSlot'] = constant_slots[constant]
        out.append(plate)
    return {'plates': out}


def unpack_terrain_attributes(blob: bytes, behavior_names: dict[int, str]) -> dict:
    """Terrain attributes section -> the "collision", "tileBehaviorLegend" and "tileBehaviors" keys."""
    if len(blob) != map_data.TERRAIN_ATTRIBUTES_SIZE:
        raise map_data.MapDataError(f'terrain attributes must be {map_data.TERRAIN_ATTRIBUTES_SIZE} bytes, not {len(blob)}')
    attributes = struct.unpack('<1024H', blob)
    for attribute in attributes:
        if attribute & ~(map_data.TILE_COLLISION_BIT | map_data.TILE_BEHAVIOR_MASK):
            raise map_data.MapDataError(f'unknown terrain attribute bits in {attribute:#06x}')
        if attribute & map_data.TILE_BEHAVIOR_MASK not in behavior_names:
            raise map_data.MapDataError(f'unknown tile behavior {attribute & map_data.TILE_BEHAVIOR_MASK:#04x}')

    legend = assign_legend({a & map_data.TILE_BEHAVIOR_MASK for a in attributes}, behavior_names)
    rows = range(map_data.MAP_TILES_COUNT_Z)
    cols = range(map_data.MAP_TILES_COUNT_X)
    return {
        'collision': [''.join('#' if attributes[z * 32 + x] & map_data.TILE_COLLISION_BIT else '.' for x in cols) for z in rows],
        'tileBehaviorLegend': {char: behavior_names[value] for value, char in legend.items()},
        'tileBehaviors': [''.join(legend[attributes[z * 32 + x] & map_data.TILE_BEHAVIOR_MASK] for x in cols) for z in rows],
    }


def unpack_map_data(blob: bytes, model_name: str, behavior_names: dict[int, str], prop_model_names: dict[int, str]):
    attrs_size, props_size, model_size, bdhc_size = struct.unpack_from('<4I', blob, 0)
    if attrs_size != map_data.TERRAIN_ATTRIBUTES_SIZE:
        raise map_data.MapDataError(f'unexpected terrain attributes size {attrs_size}')
    if 16 + attrs_size + props_size + model_size + bdhc_size != len(blob):
        raise map_data.MapDataError('section sizes do not add up to the file size')

    terrain = unpack_terrain_attributes(blob[16:16 + attrs_size], behavior_names)

    props = []
    props_offset = 16 + attrs_size
    count = props_size // map_data.MAP_PROP_SIZE
    for i in range(count):
        values = struct.unpack_from('<I3i3i3i2I', blob, props_offset + i * map_data.MAP_PROP_SIZE)
        prop = {
            'model': prop_model_names[values[0]],
            'position': [map_data.from_fx32(v) for v in values[1:4]],
        }
        if any(values[4:7]):
            prop['rotation'] = list(values[4:7])
        if values[7:10] != (1 << 12,) * 3:
            prop['scale'] = [map_data.from_fx32(v) for v in values[7:10]]
        if any(values[10:12]):
            prop['dummy'] = list(values[10:12])
        props.append(prop)

    data = {'model': model_name, **terrain, 'props': props}
    trailing = blob[props_offset + count * map_data.MAP_PROP_SIZE:props_offset + props_size]
    if trailing:
        data['propsTrailingBytes'] = trailing.hex()

    model_offset = props_offset + props_size
    model = blob[model_offset:model_offset + model_size]
    data['bdhc'] = unpack_bdhc(blob[model_offset + model_size:])
    return data, model


def dump_json(data: dict) -> str:
    """JSON with one grid row / prop / plate per line, so diffs stay readable."""
    def inline(value):
        return json.dumps(value, separators=(', ', ': '))

    lines = ['{', f'    "model": {inline(data["model"])},']
    lines.append('    "collision": [')
    lines += [f'        {inline(row)},' for row in data['collision']]
    lines[-1] = lines[-1].rstrip(',')
    lines.append('    ],')
    lines.append('    "tileBehaviorLegend": {')
    lines += [f'        {inline(char)}: {inline(name)},' for char, name in data['tileBehaviorLegend'].items()]
    lines[-1] = lines[-1].rstrip(',')
    lines.append('    },')
    lines.append('    "tileBehaviors": [')
    lines += [f'        {inline(row)},' for row in data['tileBehaviors']]
    lines[-1] = lines[-1].rstrip(',')
    lines.append('    ],')
    if data['props']:
        lines.append('    "props": [')
        lines += [f'        {inline(prop)},' for prop in data['props']]
        lines[-1] = lines[-1].rstrip(',')
        lines.append('    ],')
    else:
        lines.append('    "props": [],')
    if 'propsTrailingBytes' in data:
        lines.append(f'    "propsTrailingBytes": {inline(data["propsTrailingBytes"])},')
    lines.append('    "bdhc": {')
    lines.append('        "plates": [')
    lines += [f'            {inline(plate)},' for plate in data['bdhc']['plates']]
    lines[-1] = lines[-1].rstrip(',')
    lines.append('        ]')
    lines.append('    }')
    lines.append('}')
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('inputs', nargs='+', type=pathlib.Path, help='land data binaries')
    parser.add_argument('--json-dir', type=pathlib.Path, required=True)
    parser.add_argument('--models-dir', type=pathlib.Path, required=True)
    parser.add_argument('--map-prop-model-naix', type=pathlib.Path, required=True)
    parser.add_argument('--tile-behaviors', type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parents[2] / 'include/constants/field/map_tile_behaviors.h')
    args = parser.parse_args()

    behaviors = map_data.parse_tile_behaviors(args.tile_behaviors)
    prop_models = map_data.parse_naix(args.map_prop_model_naix)
    behavior_names = {value: name for name, value in behaviors.items()}
    prop_model_names = {value: name for name, value in prop_models.items()}

    args.json_dir.mkdir(parents=True, exist_ok=True)
    args.models_dir.mkdir(parents=True, exist_ok=True)
    failures = 0
    for path in args.inputs:
        blob = path.read_bytes()
        try:
            data, model = unpack_map_data(blob, f'{path.stem}.nsbmd', behavior_names, prop_model_names)
            text = dump_json(data)
            repacked = map_data.pack_map_data(json.loads(text), model, behaviors, prop_models)
        except map_data.MapDataError as err:
            print(f'{path}: error: {err}', file=sys.stderr)
            failures += 1
            continue
        if repacked != blob:
            print(f'{path}: error: repacking does not reproduce the input', file=sys.stderr)
            failures += 1
            continue
        (args.json_dir / f'{path.stem}.json').write_text(text, encoding='utf-8')
        (args.models_dir / data['model']).write_bytes(model)

    print(f'converted {len(args.inputs) - failures}/{len(args.inputs)} maps')
    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
