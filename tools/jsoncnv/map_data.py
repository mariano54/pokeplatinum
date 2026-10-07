#!/usr/bin/env python3
"""Packs a map's land data (land_data.narc member) from its editable sources.

A map is described by a JSON file plus the NSBMD terrain model it references:

    {
        "model": "map_data_000.nsbmd",          # terrain model, relative to --models-dir
        "collision": [ 32 strings of 32 chars ], # '#' = blocked, '.' = walkable
        "tileBehaviorLegend": { ".": "TILE_BEHAVIOR_NONE", "g": "TILE_BEHAVIOR_TALL_GRASS", ... },
        "tileBehaviors": [ 32 strings of 32 chars ],
        "props": [ { "model": "<prop model NAIX name>", "position": [x, y, z], ... } ],
        "bdhc": { "plates": [ { "rect": [x1, z1, x2, z2], "normal": [x, y, z], "constant": d } ] }
    }

Rows of the tile grids go from north (row 0) to south, columns from west to east.
All coordinates are in world units (1 tile = 16 units); a map spans -256..256 on
the X and Z axes, with (0, 0) at the center of the map.

The BDHC point list, strips and access list are derived from the plates, so editing
the plates is enough to change the terrain heights. See docs/maps/editing_maps.md.

This module is also imported by tools/scripts/unpack_map_data.py, so it must not
depend on any build-generated constants.
"""
import argparse
import json
import pathlib
import re
import struct
import sys

MAP_TILES_COUNT_X = 32
MAP_TILES_COUNT_Z = 32
TERRAIN_ATTRIBUTES_SIZE = 2 * MAP_TILES_COUNT_X * MAP_TILES_COUNT_Z
MAP_PROP_SIZE = 48
MAX_MAP_PROPS = 32

TILE_COLLISION_BIT = 1 << 15
TILE_BEHAVIOR_MASK = 0xFF

COLLISION_CHARS = {'.': False, '#': True}

FX32_SHIFT = 12


class MapDataError(Exception):
    pass


def to_fx32(value) -> int:
    fx = round(value * (1 << FX32_SHIFT))
    if fx != value * (1 << FX32_SHIFT):
        raise MapDataError(f'{value} is not representable as an fx32 (multiples of 1/4096 only)')
    if not -(1 << 31) <= fx < (1 << 31):
        raise MapDataError(f'{value} is out of range for an fx32')
    return fx


def from_fx32(fx: int):
    value = fx / (1 << FX32_SHIFT)
    return int(value) if value.is_integer() else value


def parse_tile_behaviors(header_path: pathlib.Path) -> dict[str, int]:
    """Reads `enum TileBehavior` from include/constants/field/map_tile_behaviors.h."""
    text = header_path.read_text(encoding='utf-8')
    body = re.search(r'enum\s+TileBehavior\s*\{(?P<body>.*?)\};', text, re.S)
    if body is None:
        raise MapDataError(f'{header_path}: could not find enum TileBehavior')

    behaviors = {}
    value = 0
    for entry in body['body'].split(','):
        entry = re.sub(r'//.*', '', entry).strip()
        if not entry:
            continue
        name, _, explicit = (part.strip() for part in entry.partition('='))
        if explicit:
            value = int(explicit, 0)
        behaviors[name] = value
        value += 1
    behaviors.pop('TILE_BEHAVIOR_MAX', None)
    return behaviors


def parse_naix(naix_path: pathlib.Path) -> dict[str, int]:
    return {
        match['ident']: int(match['value'])
        for match in re.finditer(r'#define (?P<ident>\w+) (?P<value>\d+)', naix_path.read_text(encoding='utf-8'))
    }


def pack_terrain_attributes(data: dict, behaviors: dict[str, int]) -> bytes:
    collision = data['collision']
    tiles = data['tileBehaviors']
    legend = data['tileBehaviorLegend']

    for grid_name, grid in (('collision', collision), ('tileBehaviors', tiles)):
        if len(grid) != MAP_TILES_COUNT_Z or any(len(row) != MAP_TILES_COUNT_X for row in grid):
            raise MapDataError(f'"{grid_name}" must be {MAP_TILES_COUNT_Z} rows of {MAP_TILES_COUNT_X} characters')

    legend_values = {}
    for char, behavior in legend.items():
        if len(char) != 1:
            raise MapDataError(f'tileBehaviorLegend key "{char}" must be a single character')
        if behavior not in behaviors:
            raise MapDataError(f'unknown tile behavior "{behavior}" (see include/constants/field/map_tile_behaviors.h)')
        legend_values[char] = behaviors[behavior]

    attributes = []
    for z in range(MAP_TILES_COUNT_Z):
        for x in range(MAP_TILES_COUNT_X):
            collision_char = collision[z][x]
            behavior_char = tiles[z][x]
            if collision_char not in COLLISION_CHARS:
                raise MapDataError(f'collision row {z} column {x}: "{collision_char}" is not "." or "#"')
            if behavior_char not in legend_values:
                raise MapDataError(f'tileBehaviors row {z} column {x}: "{behavior_char}" is not in tileBehaviorLegend')
            attribute = legend_values[behavior_char]
            if COLLISION_CHARS[collision_char]:
                attribute |= TILE_COLLISION_BIT
            attributes.append(attribute)

    return struct.pack(f'<{len(attributes)}H', *attributes)


def pack_props(data: dict, prop_models: dict[str, int]) -> bytes:
    props = data.get('props', [])
    if len(props) > MAX_MAP_PROPS:
        raise MapDataError(f'{len(props)} props found, but a map can only hold {MAX_MAP_PROPS}')

    packed = []
    for i, prop in enumerate(props):
        model = prop['model']
        if model not in prop_models:
            raise MapDataError(f'prop {i}: unknown prop model "{model}" (see docs/maps/prop_models.md)')
        rotation = prop.get('rotation', [0, 0, 0])
        scale = prop.get('scale', [1, 1, 1])
        dummy = prop.get('dummy', [0, 0])
        packed.append(struct.pack(
            '<I3i3i3i2I',
            prop_models[model],
            *(to_fx32(v) for v in prop['position']),
            *(int(v) for v in rotation),
            *(to_fx32(v) for v in scale),
            *dummy,
        ))

    # Only map_data_506 has this: a stray CRLF left behind by the original tool.
    # The game divides the section size by sizeof(MapPropFile), so it is ignored.
    packed.append(bytes.fromhex(data.get('propsTrailingBytes', '')))
    return b''.join(packed)


def _first_use_table(keys):
    table, indexes = {}, []
    for key in keys:
        if key not in table:
            table[key] = len(table)
        indexes.append(table[key])
    return list(table), indexes


def pack_bdhc(bdhc: dict) -> bytes:
    plates = bdhc['plates']

    rects = []
    for i, plate in enumerate(plates):
        x1, z1, x2, z2 = (to_fx32(v) for v in plate['rect'])
        if x1 > x2 or z1 > z2:
            raise MapDataError(f'bdhc plate {i}: rect must be [minX, minZ, maxX, maxZ]')
        rects.append(((x1, z1), (x2, z2)))

    points, point_indexes = _first_use_table(pt for rect in rects for pt in rect)

    # The original tool deduplicated normals/constants before rounding them to fx32,
    # so a few vanilla maps store the same fx32 value twice. A plate selects such a
    # second copy with "normalSlot"/"constantSlot"; new plates never need these.
    normal_keys = [(tuple(to_fx32(v) for v in plate['normal']), plate.get('normalSlot', 0)) for plate in plates]
    constant_keys = [(to_fx32(plate['constant']), plate.get('constantSlot', 0)) for plate in plates]
    normals, normal_indexes = _first_use_table(normal_keys)
    constants, constant_indexes = _first_use_table(constant_keys)

    # A strip exists for every distinct Z coordinate of the plate corners except the
    # smallest one, and lists (in plate order) the plates whose Z range contains it.
    scanlines = sorted({pt[1] for pt in points})[1:]
    strips, access_list = [], []
    in_any_strip = set()
    for scanline in scanlines:
        members = [i for i, (a, b) in enumerate(rects) if a[1] <= scanline <= b[1]]
        in_any_strip.update(members)
        strips.append([scanline, members])
    # Zero-height plates lying on the smallest Z are not crossed by any scanline;
    # the original tool filed them under the first strip below them.
    for i, (a, b) in enumerate(rects):
        if i not in in_any_strip and strips:
            strip = next((s for s in strips if s[0] >= a[1]), strips[-1])
            strip[1] = sorted(strip[1] + [i])
    packed_strips = []
    for scanline, members in strips:
        packed_strips.append(struct.pack('<iHH', scanline, len(members), len(access_list)))
        access_list += members

    return b''.join([
        b'BDHC',
        struct.pack('<6H', len(points), len(normals), len(constants), len(plates), len(strips), len(access_list)),
        b''.join(struct.pack('<2i', *pt) for pt in points),
        b''.join(struct.pack('<3i', *normal) for normal, _ in normals),
        b''.join(struct.pack('<i', constant) for constant, _ in constants),
        b''.join(
            struct.pack('<4H', point_indexes[2 * i], point_indexes[2 * i + 1], normal_indexes[i], constant_indexes[i])
            for i in range(len(plates))
        ),
        b''.join(packed_strips),
        struct.pack(f'<{len(access_list)}H', *access_list),
    ])


def pack_map_data(data: dict, model: bytes, behaviors: dict[str, int], prop_models: dict[str, int]) -> bytes:
    terrain_attributes = pack_terrain_attributes(data, behaviors)
    props = pack_props(data, prop_models)
    bdhc = pack_bdhc(data['bdhc'])
    return b''.join([
        struct.pack('<4I', len(terrain_attributes), len(props), len(model), len(bdhc)),
        terrain_attributes,
        props,
        model,
        bdhc,
    ])


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('input', type=pathlib.Path, help='map JSON file')
    parser.add_argument('output', type=pathlib.Path, help='land data binary to write')
    parser.add_argument('--models-dir', type=pathlib.Path, required=True, help='directory holding terrain NSBMD files')
    parser.add_argument('--map-prop-model-naix', type=pathlib.Path, required=True)
    parser.add_argument('--tile-behaviors', type=pathlib.Path, required=True, help='map_tile_behaviors.h')
    parser.add_argument('--depfile', type=pathlib.Path, help='write a Makefile-style depfile for the terrain model')
    args = parser.parse_args()

    try:
        with open(args.input, 'r', encoding='utf-8') as input_file:
            data = json.load(input_file)
        model_path = args.models_dir / data['model']
        packed = pack_map_data(
            data,
            model_path.read_bytes(),
            parse_tile_behaviors(args.tile_behaviors),
            parse_naix(args.map_prop_model_naix),
        )
    except (MapDataError, KeyError, ValueError, OSError) as err:
        detail = f'missing key {err}' if isinstance(err, KeyError) else str(err)
        print(f'{args.input}: error: {detail}', file=sys.stderr)
        sys.exit(1)

    args.output.write_bytes(packed)
    if args.depfile:
        def escape(path):
            return str(path.resolve()).replace(' ', '\\ ')
        args.depfile.write_text(f'{escape(args.output)}: {escape(model_path)} {escape(args.tile_behaviors)}\n')


if __name__ == '__main__':
    main()
