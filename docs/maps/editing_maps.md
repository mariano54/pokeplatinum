# Editing maps

This is a practical guide to changing the overworld: moving or adding buildings,
changing collision and tile behaviors, terrain heights, warps and NPCs. For how the
underlying systems work, see [maps.md](maps.md), [bdhc.md](bdhc.md) and
[file_format_specifications.md](file_format_specifications.md).

Everything below is plain text in the repository and is rebuilt into the ROM by
`make`. After editing, build with `make rom` (`make` also checks that the ROM
matches the original, which it no longer will).

## Where things live

| What | Where | Notes |
| --- | --- | --- |
| Map tiles, buildings, heights | `res/field/maps/data/map_<name>.json` | one file per 32x32 map; see below |
| Map 3D terrain model | `res/field/maps/models/map_<name>.nsbmd` | visual only; edit with a 3D map editor |
| Which map goes where | `res/field/matrices/map_matrix_NNN.json` | `map_matrix_000` is the overworld (30x30 maps) |
| Map metadata | `include/data/map_headers.h` | music, weather, area, bike/fly/escape rope, battle background... |
| Warps, NPCs, signs, triggers | `res/field/events/events_<header>.json` | one file per map header |
| Scripts | `res/field/scripts/scripts_<header>.s` | what NPCs/signs/triggers do |
| Wild encounters | `res/field/encounters/encounters_<header>.json` | |
| Area data | `res/field/area_data/area_data_NNN.json` | picks the prop model set, texture set and lighting |
| Props (buildings, trees, signs...) available in an area | `res/field/props/model_sets/prop_model_set_NNN.json` | |
| Prop 3D models | `res/field/props/models/*.nsbmd` | list: [prop_models.md](prop_models.md) |

Map names come from the map headers that use them:

- overworld maps are named after their location, with their position in the
  overworld matrix when a place spans several maps (`map_route_201_x03_y26`);
- interiors are named after their header (`map_twinleaf_town_rival_house_1f`);
- layouts shared by many places are named after what they are
  (`map_pokecenter_1f`, `map_house_1`, `map_overworld_filler_sea`);
- 34 maps that no map header uses keep their number (`map_119`).

To find the map for a place, search `res/field/matrices` for its header (e.g.
`MAP_HEADER_TWINLEAF_TOWN`), or just search `res/field/maps/data` by name.

## Coordinates

A map is 32x32 tiles. Three coordinate systems are involved:

- **Tile grids** in map JSON: row 0 is the north edge, column 0 the west edge.
- **World units** for props and BDHC: 1 tile = 16 units, the map spans -256 to 256
  on X (west to east) and Z (north to south), so (0, 0) is the center of the map.
  The center of tile (column `c`, row `r`) is at `x = 16c - 248`, `z = 16r - 248`.
  Y is the height (most towns are at Y = 16).
- **Event coordinates** in events JSON are tiles counted across the whole map
  matrix: `x = 32 * matrixColumn + c`, `z = 32 * matrixRow + r`. For example,
  Twinleaf Town is at column 3, row 27 of `map_matrix_000`, so its tile (10, 21) is
  event position (106, 885).

## Map JSON

```json
{
    "model": "map_twinleaf_town.nsbmd",
    "collision": [ "##########....", ... ],
    "tileBehaviorLegend": { ".": "TILE_BEHAVIOR_NONE", "D": "TILE_BEHAVIOR_DOOR", ... },
    "tileBehaviors": [ "..........D...", ... ],
    "props": [
        {"model": "prop_model_022_nsbmd", "position": [-80, 16, 72]}
    ],
    "bdhc": {
        "plates": [
            {"rect": [-256, -256, 256, 176], "normal": [0, 1, 0], "constant": -16}
        ]
    }
}
```

- `model`: the terrain model, in `res/field/maps/models`.
- `collision`: 32 rows of 32 characters. `#` blocks the tile, `.` lets you walk on it.
- `tileBehaviors` + `tileBehaviorLegend`: 32 rows of 32 characters, each one standing
  for a behavior from `include/constants/field/map_tile_behaviors.h` (tall grass,
  water, ledges, doors, ice...). The legend is per map; add any character you like.
- `props`: up to 32 placed 3D models (`model` is a name from
  [prop_models.md](prop_models.md); `position` in world units).
  - `scale` (default `[1, 1, 1]`) and `rotation` (default `[0, 0, 0]`) are optional.
    Note that the game currently ignores `rotation` when rendering.
  - `dummy` is unused by the game.
- `bdhc.plates`: the walkable surfaces, see [Terrain heights](#terrain-heights).

Values in world units must be multiples of 1/4096.

A few vanilla files carry fields that only exist to rebuild the original data byte
for byte: `propsTrailingBytes`, and `normalSlot`/`constantSlot` on some plates. You
can ignore them, and you never need them in new data.

## Recipes

### Move a building

Move everything that makes up the building by the same amount:

1. its prop, and its door prop if it has one (`brown_wooden_door_nsbmd`, ...)
   in `props` (+16 units per tile);
2. its block of `#` in `collision`;
3. its door tile in `tileBehaviors` (`TILE_BEHAVIOR_DOOR`);
4. the warp in the map header's events JSON that enters it (`warp_events`, in
   event coordinates). The interior's exit warp points back at this warp's
   index, so you don't need to touch the interior.

The `demo/move-twinleaf-house` branch moves Twinleaf Town's southwest house two
tiles east this way.

Ground details painted into the terrain model (paths, the building's shadow)
don't move: that needs a 3D edit of the terrain model.

### Add or remove a building, tree, sign...

Add or remove an entry in `props`, and add or remove the matching collision (and
door tile/warp for an enterable building).

A prop only renders if its model is in the prop model set of the current area.
Area data is loaded when the field loads (e.g. after a warp or Fly), from the
map header the player is on, and is not reloaded while walking around the
overworld. Check the map header's `areaDataArchiveID`, then its area data's
`mapPropSet`, and add the model to that set (and to the sets of the areas the
player can walk in from) if it is missing. [prop_models.md](prop_models.md) lists
which models each map uses.

### Change collision or tile behaviors

Edit `collision` and `tileBehaviors`. For example, to turn tiles into tall grass,
add `"g": "TILE_BEHAVIOR_TALL_GRASS"` to the legend (if it is not there yet) and
write `g` on those tiles. The grass you see is part of the terrain model, so to
make the change visible you also need to edit the model. Wild encounters for tall
grass and water come from the map header's encounters JSON.

### Terrain heights

The height of the ground is given by `bdhc.plates`. Each plate is a rectangle
(`rect`: min X, min Z, max X, max Z in world units) and a plane: for a point
inside it, the height is `-(nx * x + nz * z + constant) / ny`. For flat ground,
use `"normal": [0, 1, 0]` and `"constant": -height`. The rest of the BDHC data
(points, strips, access list) is rebuilt from the plates when building.

Keep the whole map covered by plates, or map objects outside them behave
erratically. Plates can overlap at different heights (e.g. bridges): the one
closest to the object's current height wins.

### Change the terrain model (3D)

The `.nsbmd` terrain model and its textures (in the area's map texture set,
`res/field/maps/texture_sets`) are 3D assets. Edit them with a 3D map editor
such as Pokémon DS Map Studio, which can also generate matching collision and
BDHC data. To bring its exports into a map:

```sh
tools/scripts/import_map_parts.py res/field/maps/data/map_twinleaf_town.json \
    --model exported.nsbmd --terrain-attributes exported.per --bdhc exported.bdhc
```

Any of the three options can be left out. If you have a whole land data binary
instead, `tools/scripts/unpack_map_data.py` converts it to a map JSON and model.

### Rearrange the world

`res/field/matrices/map_matrix_NNN.json` lays maps out in a grid: `maps` holds the
map for each cell, `headers` the map header for each cell (for matrices that
span several places, like the overworld), and `altitudes` an optional per-cell
height. A map can appear in several cells.

### Warps, NPCs, signs and triggers

These live in `res/field/events/events_<header>.json`, in event coordinates:

- `warp_events`: `x`, `z`, the destination map header and the index of the warp
  to arrive at in that header's events.
- `object_events`: NPCs and items: sprite, movement, script, position.
- `bg_events`: signs and hidden items: script and position.
- `coord_events`: triggers that run a script when the player steps on a tile.

The scripts they run are in `res/field/scripts`.

## Limits and gotchas

- 32 props per map at most.
- Every map is exactly 32x32 tiles.
- Prop rotation is stored but not applied when rendering.
- Door tiles are usually also blocked in `collision`. Walking into the door
  triggers the warp.

## Tools

- `tools/jsoncnv/map_data.py`: packs a map JSON and its model into land data. The
  build runs it.
- `tools/scripts/unpack_map_data.py`: land data binaries to map JSON + model. It
  checks that packing the result gives back the input.
- `tools/scripts/import_map_parts.py`: replaces a map's model, collision/behaviors
  or BDHC with raw files.
- `tools/scripts/make_prop_model_catalog.py`: regenerates
  [prop_models.md](prop_models.md).
