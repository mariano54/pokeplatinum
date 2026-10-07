# Map editor

`tools/map_editor` is a small local web app for editing the maps in
`res/field/maps` visually. It shows the real terrain and buildings, and edits the
same JSON files described in [editing_maps.md](editing_maps.md).

```sh
python3 tools/map_editor/server.py --apicula /path/to/apicula --build-cmd 'make rom'
```

Then open <http://localhost:8000>. Options:

- `--port`: default 8000.
- `--apicula`: path to [apicula](https://github.com/scurest/apicula), used to turn
  the NSBMD terrain and prop models into glTF for the 3D view
  (`cargo install --git https://github.com/scurest/apicula`). It is also looked up on
  `PATH` and in `$APICULA`. Without it the editor still works, in 2D only.
  Converted models are cached in `build/map_editor`.
- `--build-cmd`: what the **Build ROM** button runs (default `make rom`).

The page loads three.js from a CDN, so the browser needs internet access.

## Views

- **Top** (`1`): top-down orthographic view of the terrain and props with the
  editable overlays. Scroll to zoom, right-drag or space-drag to pan, `F` to frame
  the map.
- **3D** (`2`): orbit preview of the map with event markers.

The layer checkboxes toggle the terrain, prop models, grid, collision (red
hatching), tile behaviors (colored, with a two-letter label), prop boxes, events
and BDHC plates. Hovering a tile shows its coordinates, collision, behavior,
height(s), prop and event.

## Tools

| Tool | Key | What it does |
| --- | --- | --- |
| Props | `V` | Select a prop (building, door, sign...) and drag it, snapping to whole tiles (hold `⇧` for 1-unit steps). Arrow keys nudge, `⌫` deletes. "Place on map" adds the model picked in the panel; ★ marks models in the area's prop model set, the only ones the game renders there. |
| Collision | `C` | Paint blocked or walkable tiles (`⌥` paints the opposite). |
| Tiles | `B` | Paint a tile behavior (tall grass, water, door, ledges...). `⌥`-click picks a tile's behavior; clicking an entry of the "Tiles in this map" list selects it. |
| Move area | `M` | Drag to select a block of tiles, then drag it (or use the arrow keys) to move its collision, tile behaviors, props and events together. This moves a whole building with its door, door tile and warp. Vacated tiles become walkable with no behavior. |
| Events | `E` | Drag warps (W), NPCs/items (N), signs (S) and triggers (T). Their new positions are written to the map header's events JSON. |
| Heights | `H` | Select a BDHC plate to change its rectangle or, for flat plates, its height; duplicate or delete plates. |

`⌘Z`/`⇧⌘Z` undo and redo, `⌘S` saves. Saving validates the map with the packer
used by the build (`tools/jsoncnv/map_data.py`), so a map that wouldn't build is
not written. Opening and saving a map without changes rewrites it byte for byte.

## Limits

- Ground details painted into the terrain model (paths, grass, a building's
  shadow) can't be edited here: that needs a 3D editor, see
  [Change the terrain model](editing_maps.md#change-the-terrain-model-3d).
- Sloped BDHC plates are shown but only flat plates' heights can be edited.
- Map headers, matrices, scripts and the other event fields are edited in their
  files directly.
- The editor binds to `127.0.0.1` and has no authentication: don't expose it.
