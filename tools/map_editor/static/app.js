import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
// apicula exports skinned meshes: a plain Object3D.clone() would keep using the original skeleton.
import { clone as cloneModel } from 'three/addons/utils/SkeletonUtils.js';

// A map is 32x32 tiles; world units span -256..256 on X and Z (1 tile = 16 units).
const TILE = 16;
const HALF = 256;
const N = 32;
const NONE = 'TILE_BEHAVIOR_NONE';
const EVENT_KINDS = ['warp_events', 'object_events', 'bg_events', 'coord_events'];
const EVENT_STYLE = {
    warp_events: { color: '#c084fc', label: 'W', name: 'Warp' },
    object_events: { color: '#fb923c', label: 'N', name: 'NPC / item' },
    bg_events: { color: '#2dd4bf', label: 'S', name: 'Sign / hidden item' },
    coord_events: { color: '#facc15', label: 'T', name: 'Trigger' },
};

const $ = (id) => document.getElementById(id);
const clone = (value) => JSON.parse(JSON.stringify(value));
const short = (name) => name.replace(/^TILE_BEHAVIOR_|^MAP_HEADER_/, '');

const S = {
    project: null,
    maps: [],
    stem: null,
    original: null,
    context: null,
    events: null,
    collision: null,  // [row][col] -> bool
    names: null,      // [row][col] -> tile behavior name
    props: null,
    plates: null,
    eventPos: {},     // "kind:index" -> {x, z} (map-local tiles)
    tool: 'select',
    view: 'top',
    layers: { terrain: true, props3d: true, grid: true, collision: true, behaviors: true, propBoxes: true, events: true, heights: false },
    brushBlocked: true,
    behavior: 'TILE_BEHAVIOR_TALL_GRASS',
    addModel: null,
    placing: false,
    selection: null,
    undo: [],
    redo: [],
    dirty: false,
    cam: { cx: 0, cz: 0, half: 276 },
    footprints: {},
};

// ---------------------------------------------------------------- 3D scene

const glCanvas = $('gl');
const overlay = $('overlay');
const ctx = overlay.getContext('2d');
const renderer = new THREE.WebGLRenderer({ canvas: glCanvas, antialias: true, preserveDrawingBuffer: true });
renderer.outputColorSpace = THREE.SRGBColorSpace;
const scene = new THREE.Scene();
scene.background = new THREE.Color('#0b0d11');
scene.add(new THREE.AmbientLight(0xffffff, 2.2));
const sun = new THREE.DirectionalLight(0xffffff, 1.2);
sun.position.set(-200, 400, 300);
scene.add(sun);

const terrainGroup = new THREE.Group();
const propGroup = new THREE.Group();
const markerGroup = new THREE.Group();
scene.add(terrainGroup, propGroup, markerGroup);

const topCam = new THREE.OrthographicCamera(-HALF, HALF, HALF, -HALF, 1, 6000);
const orbitCam = new THREE.PerspectiveCamera(32, 1, 1, 6000);
const controls = new OrbitControls(orbitCam, glCanvas);
controls.addEventListener('change', requestRender);

const loader = new GLTFLoader();
const modelCache = new Map();
let propObjects = [];
let renderQueued = false;
let warned3d = false;

function loadModel(url) {
    if (!modelCache.has(url)) {
        modelCache.set(url, new Promise((resolve) => {
            loader.load(url, (gltf) => resolve(gltf.scene), undefined, () => {
                if (!warned3d) {
                    warned3d = true;
                    toast('3D models unavailable (is apicula installed?) — showing 2D only', true);
                }
                resolve(null);
            });
        }));
    }
    return modelCache.get(url);
}

function textureSet(key) {
    return (S.context && S.context[key]) || 'none';
}

async function loadTerrain() {
    terrainGroup.clear();
    const stem = S.stem;
    setLoading(true);
    const model = await loadModel(`/models/terrain/${stem}/${textureSet('mapTextureSet')}/model.glb`);
    if (model && stem === S.stem) {
        terrainGroup.add(cloneModel(model));
    }
    setLoading(false);
    requestRender();
}

async function ensurePropModel(name) {
    const model = await loadModel(`/models/prop/${name}/${textureSet('propTextureSet')}/model.glb`);
    if (model && !S.footprints[name]) {
        const box = new THREE.Box3().setFromObject(model);
        S.footprints[name] = { minX: box.min.x, maxX: box.max.x, minZ: box.min.z, maxZ: box.max.z, maxY: box.max.y };
    }
    return model;
}

async function syncProps3D() {
    const props = S.props || [];
    const stem = S.stem;
    const models = await Promise.all(props.map((prop) => ensurePropModel(prop.model)));
    if (stem !== S.stem) {
        return;
    }
    props.forEach((prop, i) => {
        let entry = propObjects[i];
        if (!entry || entry.model !== prop.model) {
            if (entry) {
                propGroup.remove(entry.object);
            }
            const object = models[i] ? cloneModel(models[i]) : new THREE.Group();
            entry = propObjects[i] = { model: prop.model, object };
            propGroup.add(object);
        }
        const [x, y, z] = prop.position;
        const [sx, sy, sz] = prop.scale || [1, 1, 1];
        entry.object.position.set(x, y, z);
        entry.object.scale.set(sx, sy, sz);
    });
    for (const entry of propObjects.splice(props.length)) {
        propGroup.remove(entry.object);
    }
    requestRender();
    draw();
}

function syncMarkers3D() {
    markerGroup.clear();
    if (!S.events || S.view !== '3d' || !S.layers.events) {
        return;
    }
    for (const { kind, event, pos } of allEvents()) {
        const w = kind === 'coord_events' ? event.width : 1;
        const l = kind === 'coord_events' ? event.length : 1;
        const geometry = new THREE.BoxGeometry(w * TILE - 2, 10, l * TILE - 2);
        const material = new THREE.MeshBasicMaterial({ color: EVENT_STYLE[kind].color, transparent: true, opacity: 0.75 });
        const mesh = new THREE.Mesh(geometry, material);
        const cx = (pos.x + w / 2) * TILE - HALF;
        const cz = (pos.z + l / 2) * TILE - HALF;
        mesh.position.set(cx, heightAt(cx, cz) + 5, cz);
        markerGroup.add(mesh);
    }
}

function requestRender() {
    if (!renderQueued) {
        renderQueued = true;
        requestAnimationFrame(renderFrame);
    }
}

function renderFrame() {
    renderQueued = false;
    terrainGroup.visible = S.layers.terrain;
    propGroup.visible = S.layers.props3d;
    if (S.view === '3d') {
        controls.update();
        renderer.render(scene, orbitCam);
    } else {
        const { cx, cz, half } = S.cam;
        const aspect = glCanvas.clientWidth / Math.max(1, glCanvas.clientHeight);
        topCam.left = -half * aspect;
        topCam.right = half * aspect;
        topCam.top = half;
        topCam.bottom = -half;
        topCam.position.set(cx, 3000, cz);
        topCam.up.set(0, 0, -1);
        topCam.lookAt(cx, 0, cz);
        topCam.updateProjectionMatrix();
        renderer.render(scene, topCam);
    }
}

function resize() {
    const rect = $('viewport').getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    renderer.setPixelRatio(dpr);
    renderer.setSize(rect.width, rect.height, false);
    overlay.width = Math.round(rect.width * dpr);
    overlay.height = Math.round(rect.height * dpr);
    orbitCam.aspect = rect.width / Math.max(1, rect.height);
    orbitCam.updateProjectionMatrix();
    requestRender();
    draw();
}

// ---------------------------------------------------------------- coordinates

function viewSize() {
    return { w: overlay.clientWidth, h: overlay.clientHeight };
}

function unitsPerPixel() {
    return (2 * S.cam.half) / viewSize().h;
}

function toWorld(px, py) {
    const { w, h } = viewSize();
    const k = unitsPerPixel();
    return { x: S.cam.cx + (px - w / 2) * k, z: S.cam.cz + (py - h / 2) * k };
}

function toScreen(x, z) {
    const { w, h } = viewSize();
    const k = unitsPerPixel();
    return { x: w / 2 + (x - S.cam.cx) / k, y: h / 2 + (z - S.cam.cz) / k };
}

function tileAt(x, z) {
    return { c: Math.floor((x + HALF) / TILE), r: Math.floor((z + HALF) / TILE) };
}

function inMap(c, r) {
    return c >= 0 && c < N && r >= 0 && r < N;
}

// Frames what is actually on the map: interiors often use only a corner of the 32x32 grid.
function contentBounds() {
    const box = new THREE.Box3();
    if (terrainGroup.children.length) {
        box.setFromObject(terrainGroup);
    }
    for (const prop of S.props || []) {
        const r = propRect(prop);
        box.expandByPoint(new THREE.Vector3(r.x1, 0, r.z1)).expandByPoint(new THREE.Vector3(r.x2, 0, r.z2));
    }
    for (const { pos } of allEvents()) {
        box.expandByPoint(new THREE.Vector3(pos.x * TILE - HALF, 0, pos.z * TILE - HALF));
        box.expandByPoint(new THREE.Vector3((pos.x + 1) * TILE - HALF, 0, (pos.z + 1) * TILE - HALF));
    }
    if (box.isEmpty()) {
        return { x1: -HALF, z1: -HALF, x2: HALF, z2: HALF };
    }
    const clamp = (v) => Math.max(-HALF, Math.min(HALF, v));
    return { x1: clamp(box.min.x), z1: clamp(box.min.z), x2: clamp(box.max.x), z2: clamp(box.max.z) };
}

function fitView() {
    const b = contentBounds();
    const { w, h } = viewSize();
    const aspect = w / Math.max(1, h);
    S.cam.cx = (b.x1 + b.x2) / 2;
    S.cam.cz = (b.z1 + b.z2) / 2;
    S.cam.half = Math.max((b.z2 - b.z1) / 2, (b.x2 - b.x1) / 2 / aspect, 48) * 1.06 + 6;
}

function heightsAt(x, z) {
    const out = [];
    for (const plate of S.plates || []) {
        const [x1, z1, x2, z2] = plate.rect;
        if (x >= x1 && x <= x2 && z >= z1 && z <= z2) {
            const [nx, ny, nz] = plate.normal;
            out.push(-(nx * x + nz * z + plate.constant) / ny);
        }
    }
    return out;
}

function heightAt(x, z) {
    const hs = heightsAt(x, z);
    return hs.length ? Math.max(...hs) : 0;
}

// ---------------------------------------------------------------- colors

const BEHAVIOR_COLORS = [
    [/VERY_TALL_GRASS/, '#15803d'], [/GRASS/, '#22c55e'], [/WATERFALL/, '#93c5fd'],
    [/SEA|RIVER|WATER/, '#2563eb'], [/PUDDLE/, '#60a5fa'], [/ICE/, '#67e8f9'], [/SAND/, '#eab308'],
    [/SNOW/, '#e2e8f0'], [/MUD/, '#a16207'], [/DOOR|ENTRANCE/, '#f97316'], [/WARP|ESCALATOR/, '#c084fc'],
    [/JUMP/, '#ef4444'], [/BLOCK/, '#be123c'], [/BRIDGE/, '#d97706'], [/BIKE/, '#94a3b8'],
    [/ROCK_CLIMB/, '#92400e'], [/CAVE|MOUNTAIN|FLOOR/, '#a8a29e'], [/SLIDE/, '#22d3ee'], [/BERRY/, '#e11d48'],
    [/BOOKSHELF|SHELF|TABLE|PC$|TV|TOWN_MAP|TRASH/, '#14b8a6'], [/REFLECTIVE/, '#bae6fd'],
];

function behaviorColor(name) {
    const n = short(name);
    for (const [re, color] of BEHAVIOR_COLORS) {
        if (re.test(n)) {
            return color;
        }
    }
    let h = 0;
    for (const ch of n) {
        h = (h * 31 + ch.charCodeAt(0)) >>> 0;
    }
    return `hsl(${h % 360} 70% 58%)`;
}

let hatch = null;
function hatchPattern() {
    if (!hatch) {
        const c = document.createElement('canvas');
        c.width = c.height = 8;
        const g = c.getContext('2d');
        g.fillStyle = 'rgba(239, 68, 68, 0.28)';
        g.fillRect(0, 0, 8, 8);
        g.strokeStyle = 'rgba(254, 202, 202, 0.55)';
        g.lineWidth = 1.2;
        g.beginPath();
        g.moveTo(0, 8);
        g.lineTo(8, 0);
        g.stroke();
        hatch = ctx.createPattern(c, 'repeat');
    }
    return hatch;
}

// ---------------------------------------------------------------- overlay drawing

function propRect(prop) {
    const f = S.footprints[prop.model];
    const [x, , z] = prop.position;
    const [sx, , sz] = prop.scale || [1, 1, 1];
    if (!f) {
        return { x1: x - 12, z1: z - 12, x2: x + 12, z2: z + 12 };
    }
    return { x1: x + f.minX * sx, z1: z + f.minZ * sz, x2: x + f.maxX * sx, z2: z + f.maxZ * sz };
}

function* allEvents() {
    if (!S.events) {
        return;
    }
    for (const kind of EVENT_KINDS) {
        for (const event of S.events[kind] || []) {
            const key = `${kind}:${event.index}`;
            yield { kind, event, key, pos: S.eventPos[key] || { x: event.x, z: event.z } };
        }
    }
}

function rectOnScreen(x1, z1, x2, z2) {
    const a = toScreen(x1, z1);
    const b = toScreen(x2, z2);
    return [a.x, a.y, b.x - a.x, b.y - a.y];
}

function draw() {
    const dpr = window.devicePixelRatio || 1;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, overlay.width, overlay.height);
    if (S.view !== 'top' || !S.collision) {
        return;
    }
    const px = 1 / unitsPerPixel();
    const tilePx = TILE * px;

    if (!S.layers.terrain || terrainGroup.children.length === 0) {
        ctx.fillStyle = '#1a1f29';
        ctx.fillRect(...rectOnScreen(-HALF, -HALF, HALF, HALF));
    }

    for (let r = 0; r < N; r++) {
        for (let c = 0; c < N; c++) {
            const [sx, sy] = rectOnScreen(c * TILE - HALF, r * TILE - HALF, 0, 0);
            const name = S.names[r][c];
            if (S.layers.behaviors && name !== NONE) {
                ctx.globalAlpha = 0.5;
                ctx.fillStyle = behaviorColor(name);
                ctx.fillRect(sx, sy, tilePx, tilePx);
                ctx.globalAlpha = 1;
            }
            if (S.layers.collision && S.collision[r][c]) {
                ctx.fillStyle = hatchPattern();
                ctx.fillRect(sx, sy, tilePx, tilePx);
            }
        }
    }

    if (S.layers.behaviors && tilePx >= 14) {
        ctx.font = `${Math.min(11, tilePx * 0.42)}px ui-monospace, Menlo, monospace`;
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillStyle = 'rgba(255,255,255,0.85)';
        for (let r = 0; r < N; r++) {
            for (let c = 0; c < N; c++) {
                const name = S.names[r][c];
                if (name !== NONE) {
                    const p = toScreen(c * TILE - HALF + TILE / 2, r * TILE - HALF + TILE / 2);
                    ctx.fillText(short(name).slice(0, 2), p.x, p.y);
                }
            }
        }
    }

    if (S.layers.grid) {
        ctx.strokeStyle = 'rgba(255,255,255,0.12)';
        ctx.lineWidth = 1;
        ctx.beginPath();
        for (let i = 0; i <= N; i++) {
            const a = toScreen(i * TILE - HALF, -HALF);
            const b = toScreen(i * TILE - HALF, HALF);
            ctx.moveTo(Math.round(a.x) + 0.5, a.y);
            ctx.lineTo(Math.round(b.x) + 0.5, b.y);
            const c = toScreen(-HALF, i * TILE - HALF);
            const d = toScreen(HALF, i * TILE - HALF);
            ctx.moveTo(c.x, Math.round(c.y) + 0.5);
            ctx.lineTo(d.x, Math.round(d.y) + 0.5);
        }
        ctx.stroke();
    }

    if (S.layers.heights || S.tool === 'heights') {
        const heights = S.plates.map((p) => -p.constant / p.normal[1]);
        const lo = Math.min(...heights);
        const hi = Math.max(...heights);
        S.plates.forEach((plate, i) => {
            const flat = plate.normal[0] === 0 && plate.normal[2] === 0;
            const t = hi > lo ? (heights[i] - lo) / (hi - lo) : 0.5;
            const color = flat ? `hsl(${220 - 200 * t} 85% 60%)` : '#f472b6';
            const selected = S.selection && S.selection.type === 'plate' && S.selection.index === i;
            const [x, y, w, h] = rectOnScreen(...plate.rect);
            ctx.strokeStyle = color;
            ctx.lineWidth = selected ? 3 : 1.5;
            ctx.setLineDash(flat ? [] : [5, 3]);
            ctx.strokeRect(x + 1, y + 1, w - 2, h - 2);
            ctx.setLineDash([]);
            if (selected) {
                ctx.fillStyle = 'rgba(255,255,255,0.08)';
                ctx.fillRect(x, y, w, h);
            }
            if (w > 30 && h > 16) {
                ctx.font = 'bold 11px ui-monospace, Menlo, monospace';
                ctx.textAlign = 'center';
                ctx.textBaseline = 'middle';
                const label = flat ? `h ${+heights[i].toFixed(2)}` : 'slope';
                ctx.fillStyle = 'rgba(0,0,0,0.65)';
                const tw = ctx.measureText(label).width + 8;
                ctx.fillRect(x + w / 2 - tw / 2, y + h / 2 - 8, tw, 16);
                ctx.fillStyle = color;
                ctx.fillText(label, x + w / 2, y + h / 2);
            }
        });
    }

    if (S.layers.propBoxes) {
        const inSet = new Set((S.context && S.context.propSetModels) || []);
        S.props.forEach((prop, i) => {
            const rect = propRect(prop);
            const [x, y, w, h] = rectOnScreen(rect.x1, rect.z1, rect.x2, rect.z2);
            const selected = S.selection && S.selection.type === 'prop' && S.selection.index === i;
            const missing = S.context && S.context.propSetModels && !inSet.has(prop.model);
            ctx.strokeStyle = missing ? '#fb923c' : selected ? '#ffffff' : 'rgba(91,156,255,0.95)';
            ctx.lineWidth = selected ? 2.5 : 1.5;
            ctx.setLineDash(missing ? [6, 4] : []);
            ctx.strokeRect(x, y, w, h);
            ctx.setLineDash([]);
            if (selected) {
                ctx.fillStyle = 'rgba(91,156,255,0.18)';
                ctx.fillRect(x, y, w, h);
            }
            const p = toScreen(prop.position[0], prop.position[2]);
            ctx.fillStyle = selected ? '#ffffff' : '#5b9cff';
            ctx.beginPath();
            ctx.arc(p.x, p.y, 3, 0, Math.PI * 2);
            ctx.fill();
            if (w > 34) {
                const label = S.project.propInternal[prop.model] || prop.model;
                ctx.font = '10px ui-monospace, Menlo, monospace';
                ctx.textAlign = 'left';
                ctx.textBaseline = 'top';
                const tw = ctx.measureText(label).width + 6;
                ctx.fillStyle = 'rgba(11,13,17,0.75)';
                ctx.fillRect(x, y, tw, 14);
                ctx.fillStyle = selected ? '#fff' : '#9ec2ff';
                ctx.fillText(label, x + 3, y + 2);
            }
        });
    }

    if (S.layers.events) {
        for (const { kind, event, key, pos } of allEvents()) {
            const style = EVENT_STYLE[kind];
            const w = kind === 'coord_events' ? event.width : 1;
            const l = kind === 'coord_events' ? event.length : 1;
            const [x, y, sw, sh] = rectOnScreen(pos.x * TILE - HALF, pos.z * TILE - HALF, (pos.x + w) * TILE - HALF, (pos.z + l) * TILE - HALF);
            const selected = S.selection && S.selection.type === 'event' && S.selection.key === key;
            ctx.fillStyle = style.color + (kind === 'coord_events' ? '40' : 'd0');
            ctx.strokeStyle = selected ? '#ffffff' : style.color;
            ctx.lineWidth = selected ? 2.5 : 1.5;
            if (kind === 'object_events') {
                ctx.beginPath();
                ctx.arc(x + sw / 2, y + sh / 2, sw * 0.42, 0, Math.PI * 2);
                ctx.fill();
                ctx.stroke();
            } else {
                ctx.fillRect(x + 1, y + 1, sw - 2, sh - 2);
                ctx.strokeRect(x + 1, y + 1, sw - 2, sh - 2);
            }
            if (tilePx >= 10) {
                ctx.font = `bold ${Math.min(12, tilePx * 0.55)}px -apple-system, sans-serif`;
                ctx.textAlign = 'center';
                ctx.textBaseline = 'middle';
                ctx.fillStyle = kind === 'coord_events' ? style.color : '#111';
                ctx.fillText(style.label, x + tilePx / 2, y + tilePx / 2);
            }
        }
    }

    if (S.selection && S.selection.type === 'area') {
        const a = S.selection;
        const [dc, dr] = a.preview || [0, 0];
        const [x, y, w, h] = rectOnScreen((a.c1 + dc) * TILE - HALF, (a.r1 + dr) * TILE - HALF, (a.c2 + dc + 1) * TILE - HALF, (a.r2 + dr + 1) * TILE - HALF);
        ctx.strokeStyle = '#ffffff';
        ctx.lineWidth = 2;
        ctx.setLineDash([6, 4]);
        ctx.strokeRect(x, y, w, h);
        ctx.setLineDash([]);
        ctx.fillStyle = 'rgba(255,255,255,0.08)';
        ctx.fillRect(x, y, w, h);
    }

    ctx.strokeStyle = 'rgba(255,255,255,0.55)';
    ctx.lineWidth = 1.5;
    ctx.strokeRect(...rectOnScreen(-HALF, -HALF, HALF, HALF));
}

// ---------------------------------------------------------------- editing helpers

function snapshot() {
    return JSON.stringify({ collision: S.collision, names: S.names, props: S.props, plates: S.plates, eventPos: S.eventPos });
}

function restore(snap) {
    const s = JSON.parse(snap);
    Object.assign(S, { collision: s.collision, names: s.names, props: s.props, plates: s.plates, eventPos: s.eventPos });
    afterEdit({ props3d: true });
    renderPanels();
}

function pushUndo() {
    S.undo.push(snapshot());
    if (S.undo.length > 300) {
        S.undo.shift();
    }
    S.redo = [];
    setDirty(true);
}

function setDirty(dirty) {
    S.dirty = dirty;
    $('dirty').classList.toggle('on', dirty);
}

function afterEdit({ props3d = false } = {}) {
    if (props3d) {
        syncProps3D();
    }
    syncMarkers3D();
    requestRender();
    draw();
}

function pickProp(x, z) {
    let best = null;
    S.props.forEach((prop, i) => {
        const r = propRect(prop);
        if (x >= r.x1 && x <= r.x2 && z >= r.z1 && z <= r.z2) {
            const area = (r.x2 - r.x1) * (r.z2 - r.z1);
            if (!best || area < best.area) {
                best = { index: i, area };
            }
        }
    });
    return best && best.index;
}

function pickEvent(x, z) {
    const t = tileAt(x, z);
    let found = null;
    for (const item of allEvents()) {
        const w = item.kind === 'coord_events' ? item.event.width : 1;
        const l = item.kind === 'coord_events' ? item.event.length : 1;
        if (t.c >= item.pos.x && t.c < item.pos.x + w && t.r >= item.pos.z && t.r < item.pos.z + l) {
            if (!found || item.kind !== 'coord_events') {
                found = item;
            }
        }
    }
    return found;
}

function pickPlate(x, z) {
    let best = null;
    S.plates.forEach((plate, i) => {
        const [x1, z1, x2, z2] = plate.rect;
        if (x >= x1 && x <= x2 && z >= z1 && z <= z2) {
            const area = (x2 - x1) * (z2 - z1);
            if (!best || area < best.area) {
                best = { index: i, area };
            }
        }
    });
    return best && best.index;
}

function paintTile(c, r, invert) {
    if (!inMap(c, r)) {
        return;
    }
    if (S.tool === 'collision') {
        S.collision[r][c] = invert ? !S.brushBlocked : S.brushBlocked;
    } else {
        S.names[r][c] = S.behavior;
    }
}

function moveArea(area, dc, dr) {
    const oldC = clone(S.collision);
    const oldN = clone(S.names);
    for (let r = area.r1; r <= area.r2; r++) {
        for (let c = area.c1; c <= area.c2; c++) {
            S.collision[r][c] = false;
            S.names[r][c] = NONE;
        }
    }
    for (let r = area.r1; r <= area.r2; r++) {
        for (let c = area.c1; c <= area.c2; c++) {
            if (inMap(c + dc, r + dr)) {
                S.collision[r + dr][c + dc] = oldC[r][c];
                S.names[r + dr][c + dc] = oldN[r][c];
            }
        }
    }
    const x1 = area.c1 * TILE - HALF;
    const z1 = area.r1 * TILE - HALF;
    const x2 = (area.c2 + 1) * TILE - HALF;
    const z2 = (area.r2 + 1) * TILE - HALF;
    for (const prop of S.props) {
        const [x, , z] = prop.position;
        if (x >= x1 && x < x2 && z >= z1 && z < z2) {
            prop.position[0] += dc * TILE;
            prop.position[2] += dr * TILE;
        }
    }
    for (const { key, pos } of allEvents()) {
        if (pos.x >= area.c1 && pos.x <= area.c2 && pos.z >= area.r1 && pos.z <= area.r2) {
            S.eventPos[key] = { x: pos.x + dc, z: pos.z + dr };
        }
    }
    Object.assign(area, { c1: area.c1 + dc, c2: area.c2 + dc, r1: area.r1 + dr, r2: area.r2 + dr });
}

// ---------------------------------------------------------------- pointer interaction

let drag = null;
let spaceDown = false;

overlay.addEventListener('contextmenu', (e) => e.preventDefault());

overlay.addEventListener('pointerdown', (e) => {
    if (!S.collision) {
        return;
    }
    overlay.setPointerCapture(e.pointerId);
    const w = toWorld(e.offsetX, e.offsetY);
    if (e.button === 1 || e.button === 2 || spaceDown) {
        drag = { type: 'pan', sx: e.offsetX, sy: e.offsetY, cx: S.cam.cx, cz: S.cam.cz };
        return;
    }
    const t = tileAt(w.x, w.z);
    if (S.tool === 'collision' || S.tool === 'behavior') {
        if (S.tool === 'behavior' && e.altKey) {
            if (inMap(t.c, t.r)) {
                S.behavior = S.names[t.r][t.c];
                renderPanels();
            }
            return;
        }
        pushUndo();
        paintTile(t.c, t.r, e.altKey);
        drag = { type: 'paint', invert: e.altKey, last: t };
        draw();
    } else if (S.tool === 'select') {
        if (S.placing && S.addModel) {
            pushUndo();
            const snap = (v) => Math.round(v / 8) * 8;
            S.props.push({ model: S.addModel, position: [snap(w.x), heightAt(w.x, w.z), snap(w.z)] });
            S.selection = { type: 'prop', index: S.props.length - 1 };
            S.placing = false;
            afterEdit({ props3d: true });
            renderPanels();
            return;
        }
        const index = pickProp(w.x, w.z);
        S.selection = index === null ? null : { type: 'prop', index };
        if (index !== null) {
            drag = { type: 'prop', start: w, orig: [...S.props[index].position], index, pushed: false };
        }
        renderPanels();
        draw();
    } else if (S.tool === 'events') {
        const found = pickEvent(w.x, w.z);
        S.selection = found ? { type: 'event', key: found.key, kind: found.kind, index: found.event.index } : null;
        if (found) {
            drag = { type: 'event', start: t, orig: { ...found.pos }, key: found.key, pushed: false };
        }
        renderPanels();
        draw();
    } else if (S.tool === 'heights') {
        const index = pickPlate(w.x, w.z);
        S.selection = index === null ? null : { type: 'plate', index };
        renderPanels();
        draw();
    } else if (S.tool === 'area') {
        const a = S.selection && S.selection.type === 'area' ? S.selection : null;
        if (a && t.c >= a.c1 && t.c <= a.c2 && t.r >= a.r1 && t.r <= a.r2) {
            drag = { type: 'areaMove', start: t };
        } else if (inMap(t.c, t.r)) {
            S.selection = { type: 'area', c1: t.c, r1: t.r, c2: t.c, r2: t.r, anchor: t };
            drag = { type: 'areaSelect' };
            renderPanels();
        }
        draw();
    }
});

overlay.addEventListener('pointermove', (e) => {
    if (!S.collision) {
        return;
    }
    const w = toWorld(e.offsetX, e.offsetY);
    const t = tileAt(w.x, w.z);
    updateHover(w, t);
    if (!drag) {
        return;
    }
    if (drag.type === 'pan') {
        const k = unitsPerPixel();
        S.cam.cx = drag.cx - (e.offsetX - drag.sx) * k;
        S.cam.cz = drag.cz - (e.offsetY - drag.sy) * k;
        requestRender();
        draw();
    } else if (drag.type === 'paint') {
        // Interpolate so fast strokes don't skip tiles.
        const steps = Math.max(Math.abs(t.c - drag.last.c), Math.abs(t.r - drag.last.r), 1);
        for (let i = 1; i <= steps; i++) {
            paintTile(Math.round(drag.last.c + ((t.c - drag.last.c) * i) / steps), Math.round(drag.last.r + ((t.r - drag.last.r) * i) / steps), drag.invert);
        }
        drag.last = t;
        draw();
    } else if (drag.type === 'prop') {
        const step = e.shiftKey ? 1 : TILE;
        const dx = Math.round((w.x - drag.start.x) / step) * step;
        const dz = Math.round((w.z - drag.start.z) / step) * step;
        if ((dx || dz) && !drag.pushed) {
            pushUndo();
            drag.pushed = true;
        }
        const prop = S.props[drag.index];
        prop.position[0] = drag.orig[0] + dx;
        prop.position[2] = drag.orig[2] + dz;
        afterEdit({ props3d: true });
        renderSelection();
    } else if (drag.type === 'event') {
        const dc = t.c - drag.start.c;
        const dr = t.r - drag.start.r;
        if ((dc || dr) && !drag.pushed) {
            pushUndo();
            drag.pushed = true;
        }
        S.eventPos[drag.key] = { x: drag.orig.x + dc, z: drag.orig.z + dr };
        afterEdit();
        renderSelection();
    } else if (drag.type === 'areaSelect') {
        const a = S.selection;
        const tc = Math.max(0, Math.min(N - 1, t.c));
        const tr = Math.max(0, Math.min(N - 1, t.r));
        Object.assign(a, { c1: Math.min(a.anchor.c, tc), c2: Math.max(a.anchor.c, tc), r1: Math.min(a.anchor.r, tr), r2: Math.max(a.anchor.r, tr) });
        renderSelection();
        draw();
    } else if (drag.type === 'areaMove') {
        S.selection.preview = [t.c - drag.start.c, t.r - drag.start.r];
        draw();
    }
});

overlay.addEventListener('pointerup', () => {
    if (drag && drag.type === 'areaMove' && S.selection.preview) {
        const [dc, dr] = S.selection.preview;
        delete S.selection.preview;
        if (dc || dr) {
            pushUndo();
            moveArea(S.selection, dc, dr);
            afterEdit({ props3d: true });
        }
        renderPanels();
    }
    drag = null;
    draw();
});

overlay.addEventListener('pointerleave', () => {
    $('hover').textContent = '';
});

overlay.addEventListener('wheel', (e) => {
    e.preventDefault();
    const before = toWorld(e.offsetX, e.offsetY);
    S.cam.half = Math.max(48, Math.min(900, S.cam.half * Math.exp(e.deltaY * 0.0015)));
    const after = toWorld(e.offsetX, e.offsetY);
    S.cam.cx += before.x - after.x;
    S.cam.cz += before.z - after.z;
    requestRender();
    draw();
}, { passive: false });

function updateHover(w, t) {
    if (!inMap(t.c, t.r)) {
        $('hover').textContent = '';
        return;
    }
    const lines = [`tile (${t.c}, ${t.r})   world (${w.x.toFixed(0)}, ${w.z.toFixed(0)})`];
    lines.push(`${S.collision[t.r][t.c] ? 'blocked' : 'walkable'} · ${short(S.names[t.r][t.c])}`);
    const hs = heightsAt(w.x, w.z);
    if (hs.length) {
        lines.push(`height ${hs.map((h) => +h.toFixed(2)).join(' / ')}`);
    }
    const prop = pickProp(w.x, w.z);
    if (prop !== null) {
        const p = S.props[prop];
        lines.push(`prop #${prop} ${p.model} (${S.project.propInternal[p.model]})`);
    }
    const ev = pickEvent(w.x, w.z);
    if (ev) {
        const dest = ev.kind === 'warp_events' ? ` → ${short(ev.event.dest_header_id)} #${ev.event.dest_warp_id}` : '';
        lines.push(`${EVENT_STYLE[ev.kind].name} #${ev.event.index}${dest}`);
    }
    $('hover').textContent = lines.join('\n');
}

// ---------------------------------------------------------------- keyboard

window.addEventListener('keydown', (e) => {
    const typing = ['INPUT', 'SELECT', 'TEXTAREA'].includes(document.activeElement.tagName);
    const mod = e.metaKey || e.ctrlKey;
    if (mod && e.key.toLowerCase() === 's') {
        e.preventDefault();
        save();
        return;
    }
    if (typing) {
        return;
    }
    if (mod && e.key.toLowerCase() === 'z') {
        e.preventDefault();
        (e.shiftKey ? redo : undo)();
        return;
    }
    if (e.key === ' ') {
        spaceDown = true;
    }
    const tools = { v: 'select', c: 'collision', b: 'behavior', m: 'area', e: 'events', h: 'heights' };
    if (!mod && tools[e.key.toLowerCase()]) {
        setTool(tools[e.key.toLowerCase()]);
    }
    if (e.key.toLowerCase() === 'f' && !mod) {
        fitView();
        setView(S.view);
    }
    if (e.key === '1') {
        setView('top');
    }
    if (e.key === '2') {
        setView('3d');
    }
    if ((e.key === 'Delete' || e.key === 'Backspace') && S.selection && S.selection.type === 'prop') {
        deleteSelectedProp();
    }
    if (e.key === 'Escape') {
        S.selection = null;
        S.placing = false;
        renderPanels();
        draw();
    }
    const arrows = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] };
    if (arrows[e.key] && S.selection) {
        e.preventDefault();
        nudge(...arrows[e.key], e.shiftKey);
    }
});

window.addEventListener('keyup', (e) => {
    if (e.key === ' ') {
        spaceDown = false;
    }
});

function nudge(dx, dz, fine) {
    const sel = S.selection;
    pushUndo();
    if (sel.type === 'prop') {
        const step = fine ? 1 : TILE;
        S.props[sel.index].position[0] += dx * step;
        S.props[sel.index].position[2] += dz * step;
        afterEdit({ props3d: true });
    } else if (sel.type === 'event') {
        const item = [...allEvents()].find((ev) => ev.key === sel.key);
        S.eventPos[sel.key] = { x: item.pos.x + dx, z: item.pos.z + dz };
        afterEdit();
    } else if (sel.type === 'area') {
        moveArea(sel, dx, dz);
        afterEdit({ props3d: true });
    } else {
        S.undo.pop();
        return;
    }
    renderSelection();
}

function undo() {
    if (S.undo.length) {
        S.redo.push(snapshot());
        restore(S.undo.pop());
        setDirty(true);
    }
}

function redo() {
    if (S.redo.length) {
        S.undo.push(snapshot());
        restore(S.redo.pop());
        setDirty(true);
    }
}

function deleteSelectedProp() {
    pushUndo();
    S.props.splice(S.selection.index, 1);
    S.selection = null;
    afterEdit({ props3d: true });
    renderPanels();
}

// ---------------------------------------------------------------- panels

function setTool(tool) {
    S.tool = tool;
    S.placing = false;
    if (!(tool === 'area' && S.selection && S.selection.type === 'area')) {
        S.selection = null;
    }
    document.querySelectorAll('#tools [data-tool]').forEach((b) => b.classList.toggle('active', b.dataset.tool === tool));
    overlay.style.cursor = { select: 'default', events: 'default', heights: 'help' }[tool] || 'crosshair';
    renderPanels();
    draw();
}

function setView(view) {
    S.view = view;
    document.querySelectorAll('[data-view]').forEach((b) => b.classList.toggle('active', b.dataset.view === view));
    overlay.style.display = view === 'top' ? 'block' : 'none';
    if (view === '3d') {
        const { cx, cz, half } = S.cam;
        const ground = heightAt(cx, cz);
        orbitCam.position.set(cx, ground + half * 1.55, cz + half * 1.9);
        controls.target.set(cx, ground, cz - half * 0.08);
    }
    syncMarkers3D();
    requestRender();
    draw();
}

function html(strings, ...values) {
    const esc = (v) => String(v).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
    return strings.reduce((out, s, i) => out + s + (i < values.length ? (values[i] && values[i].raw !== undefined ? values[i].raw : esc(values[i])) : ''), '');
}
const raw = (s) => ({ raw: s });

function renderPanels() {
    renderContext();
    renderToolOptions();
    renderSelection();
    renderLegend();
}

function renderContext() {
    if (!S.context) {
        $('context').innerHTML = '';
        return;
    }
    const c = S.context;
    const eventsCount = S.events ? EVENT_KINDS.reduce((n, k) => n + S.events[k].length, 0) : 0;
    $('context').innerHTML = html`<h3>Map</h3><div class="kv">
        <span>File</span><span>${S.stem}.json</span>
        <span>Header</span><span>${c.header ? short(c.header) : '— (unused)'}</span>
        <span>Matrix</span><span>${c.matrix ? `${c.matrix} (${c.cellX}, ${c.cellZ})` : '—'}</span>
        <span>Area</span><span>${c.areaData || '—'}</span>
        <span>Props</span><span>${S.props.length} / 32</span>
        <span>Events</span><span>${eventsCount}${S.events ? ` in ${S.events.file.split('/').pop()}` : ''}</span>
    </div>`;
}

function renderToolOptions() {
    const el = $('tool-options');
    if (S.tool === 'collision') {
        el.innerHTML = html`<h3>Collision brush</h3>
            <div class="seg"><button data-brush="1" class="${S.brushBlocked ? 'active' : ''}">Blocked</button>
            <button data-brush="0" class="${S.brushBlocked ? '' : 'active'}">Walkable</button></div>
            <p class="hint">Drag to paint. Hold ⌥ to paint the opposite.</p>`;
        el.querySelectorAll('[data-brush]').forEach((b) => b.addEventListener('click', () => {
            S.brushBlocked = b.dataset.brush === '1';
            renderToolOptions();
        }));
    } else if (S.tool === 'behavior') {
        const options = S.project.behaviors.filter((b) => !/UNUSED/.test(b) || b === S.behavior)
            .map((b) => html`<option value="${b}" ${raw(b === S.behavior ? 'selected' : '')}>${short(b)}</option>`).join('');
        el.innerHTML = html`<h3>Tile behavior brush</h3>
            <select id="behavior-select">${raw(options)}</select>
            <p class="hint">Drag to paint. ⌥-click a tile to pick its behavior. The visible grass/water comes from the terrain model.</p>`;
        $('behavior-select').addEventListener('change', (e) => {
            S.behavior = e.target.value;
            renderLegend();
        });
    } else if (S.tool === 'select') {
        const inSet = new Set((S.context && S.context.propSetModels) || []);
        const models = S.project.props.slice().sort((a, b) => inSet.has(b.name) - inSet.has(a.name));
        const options = models.map((p) => html`<option value="${p.name}" ${raw(p.name === S.addModel ? 'selected' : '')}>${inSet.has(p.name) ? '★ ' : ''}${p.internal} — ${p.name}</option>`).join('');
        el.innerHTML = html`<h3>Props</h3>
            <p class="hint">Click a building to select, drag to move (snaps to tiles; hold ⇧ for 1 unit). Arrow keys nudge, ⌫ deletes.</p>
            <label class="row">Add<select id="add-model">${raw(options)}</select></label>
            <div class="buttons"><button id="place" class="${S.placing ? 'active' : ''}">${S.placing ? 'Click the map…' : 'Place on map'}</button></div>
            <p class="hint">★ = in this area's prop model set (others won't render in game).</p>`;
        if (!S.addModel) {
            S.addModel = (models.find((p) => inSet.has(p.name) && !p.internal.startsWith('dmy')) || models[0]).name;
            $('add-model').value = S.addModel;
        }
        $('add-model').addEventListener('change', (e) => {
            S.addModel = e.target.value;
            S.addModelChosen = true;
        });
        $('place').addEventListener('click', () => {
            S.placing = !S.placing;
            renderToolOptions();
        });
    } else if (S.tool === 'area') {
        el.innerHTML = html`<h3>Move area</h3>
            <p class="hint">Drag to select a block of tiles, then drag the selection (or use the arrow keys) to move its collision, tile behaviors, props and events together — e.g. a whole building with its door and warp.</p>`;
    } else if (S.tool === 'events') {
        el.innerHTML = html`<h3>Events</h3>
            <p class="hint">Drag warps (W), NPCs/items (N), signs (S) and triggers (T). Positions are saved to the map header's events file.</p>`;
    } else if (S.tool === 'heights') {
        el.innerHTML = html`<h3>Heights (BDHC)</h3>
            <p class="hint">Click a plate to edit it. Flat plates have a single height; the rest of the BDHC data is rebuilt when building.</p>`;
    }
}

function renderSelection() {
    const el = $('selection');
    const sel = S.selection;
    if (!sel) {
        el.innerHTML = '';
        return;
    }
    if (sel.type === 'prop') {
        const prop = S.props[sel.index];
        const inSet = !S.context.propSetModels || S.context.propSetModels.includes(prop.model);
        const options = S.project.props.map((p) => html`<option value="${p.name}" ${raw(p.name === prop.model ? 'selected' : '')}>${p.internal} — ${p.name}</option>`).join('');
        el.innerHTML = html`<h3>Prop #${sel.index}</h3>
            <label class="row">Model<select id="prop-model">${raw(options)}</select></label>
            <label class="row">Position<div class="xyz">
                <input type="number" step="1" data-axis="0" value="${prop.position[0]}">
                <input type="number" step="1" data-axis="1" value="${prop.position[1]}">
                <input type="number" step="1" data-axis="2" value="${prop.position[2]}"></div></label>
            <div class="kv"><span>Tile</span><span>${((prop.position[0] + HALF) / TILE).toFixed(2)}, ${((prop.position[2] + HALF) / TILE).toFixed(2)}</span></div>
            ${raw(inSet ? '' : '<p class="warn">Not in this area\'s prop model set: it will show as a placeholder in game.</p>')}
            <div class="buttons"><button id="prop-delete" class="danger">Delete</button></div>`;
        $('prop-model').addEventListener('change', (e) => {
            pushUndo();
            prop.model = e.target.value;
            afterEdit({ props3d: true });
            renderSelection();
        });
        el.querySelectorAll('[data-axis]').forEach((input) => input.addEventListener('change', () => {
            pushUndo();
            prop.position[+input.dataset.axis] = Math.round(parseFloat(input.value) * 4096) / 4096 || 0;
            afterEdit({ props3d: true });
        }));
        $('prop-delete').addEventListener('click', deleteSelectedProp);
    } else if (sel.type === 'event') {
        const item = [...allEvents()].find((ev) => ev.key === sel.key);
        const ev = item.event;
        const detail = {
            warp_events: () => [['Goes to', short(ev.dest_header_id)], ['Arrive at', `warp #${ev.dest_warp_id}`]],
            object_events: () => [['Id', ev.id], ['Sprite', ev.graphics_id.replace('OBJ_EVENT_GFX_', '')], ['Movement', ev.movement_type.replace('MOVEMENT_TYPE_', '')], ['Script', ev.script]],
            bg_events: () => [['Script', ev.script], ['Facing', ev.player_facing_dir.replace('BG_EVENT_DIR_', '')]],
            coord_events: () => [['Script', ev.script], ['Size', `${ev.width} × ${ev.length}`], ['When', `${ev.var} == ${ev.value}`]],
        }[sel.kind]();
        const ox = S.context.cellX * 32;
        const oz = S.context.cellZ * 32;
        el.innerHTML = html`<h3>${EVENT_STYLE[sel.kind].name} #${ev.index}</h3><div class="kv">
            ${raw(detail.map(([k, v]) => html`<span>${k}</span><span>${v}</span>`).join(''))}
            <span>Tile</span><span>${item.pos.x}, ${item.pos.z}</span>
            <span>Event x/z</span><span>${item.pos.x + ox}, ${item.pos.z + oz}</span></div>`;
    } else if (sel.type === 'plate') {
        const plate = S.plates[sel.index];
        const flat = plate.normal[0] === 0 && plate.normal[2] === 0;
        el.innerHTML = html`<h3>Plate #${sel.index}</h3>
            <label class="row">From<div class="xyz"><input type="number" data-r="0" value="${plate.rect[0]}"><input type="number" data-r="1" value="${plate.rect[1]}"><span></span></div></label>
            <label class="row">To<div class="xyz"><input type="number" data-r="2" value="${plate.rect[2]}"><input type="number" data-r="3" value="${plate.rect[3]}"><span></span></div></label>
            ${raw(flat ? html`<label class="row">Height<input type="number" id="plate-height" value="${-plate.constant / plate.normal[1]}"></label>`
            : html`<div class="kv"><span>Normal</span><span>${plate.normal.map((v) => +v.toFixed(4)).join(', ')}</span><span>Constant</span><span>${plate.constant}</span></div><p class="hint">Sloped plate: edit its normal/constant in the JSON.</p>`)}
            <div class="buttons"><button id="plate-dup">Duplicate</button><button id="plate-delete" class="danger">Delete</button></div>`;
        el.querySelectorAll('[data-r]').forEach((input) => input.addEventListener('change', () => {
            pushUndo();
            plate.rect[+input.dataset.r] = Math.round(parseFloat(input.value)) || 0;
            afterEdit();
        }));
        if (flat) {
            $('plate-height').addEventListener('change', (e) => {
                pushUndo();
                plate.constant = -Math.round(parseFloat(e.target.value) * 4096) / 4096 * plate.normal[1];
                delete plate.constantSlot;
                afterEdit();
            });
        }
        $('plate-dup').addEventListener('click', () => {
            pushUndo();
            const copy = clone(plate);
            delete copy.normalSlot;
            delete copy.constantSlot;
            S.plates.push(copy);
            S.selection = { type: 'plate', index: S.plates.length - 1 };
            afterEdit();
            renderSelection();
        });
        $('plate-delete').addEventListener('click', () => {
            pushUndo();
            S.plates.splice(sel.index, 1);
            S.selection = null;
            afterEdit();
            renderSelection();
        });
    } else if (sel.type === 'area') {
        el.innerHTML = html`<h3>Selected area</h3><div class="kv">
            <span>Tiles</span><span>(${sel.c1}, ${sel.r1}) → (${sel.c2}, ${sel.r2})</span>
            <span>Size</span><span>${sel.c2 - sel.c1 + 1} × ${sel.r2 - sel.r1 + 1}</span></div>
            <p class="hint">Drag inside the selection or use the arrow keys to move it.</p>`;
    }
}

function renderLegend() {
    if (!S.names) {
        $('legend').innerHTML = '';
        return;
    }
    const counts = {};
    for (const row of S.names) {
        for (const name of row) {
            counts[name] = (counts[name] || 0) + 1;
        }
    }
    const blocked = S.collision.flat().filter(Boolean).length;
    const items = Object.entries(counts).sort((a, b) => b[1] - a[1]).map(([name, count]) => html`
        <div class="legend-item ${raw(S.tool === 'behavior' && name === S.behavior ? 'active' : '')}" data-name="${name}">
            <span class="swatch" style="background:${name === NONE ? 'transparent' : behaviorColor(name)}"></span>
            <span class="name">${short(name)}</span><span class="count">${count}</span></div>`).join('');
    $('legend').innerHTML = html`<h3>Tiles in this map</h3><div class="legend-list">
        <div class="legend-item"><span class="swatch" style="background:${raw('rgba(239,68,68,.45)')}"></span><span class="name">BLOCKED (collision)</span><span class="count">${blocked}</span></div>
        ${raw(items)}</div>`;
    $('legend').querySelectorAll('[data-name]').forEach((item) => item.addEventListener('click', () => {
        S.behavior = item.dataset.name;
        setTool('behavior');
    }));
}

// ---------------------------------------------------------------- loading & saving

function setLoading(on) {
    $('loading').classList.toggle('on', on);
}

let toastTimer = null;
function toast(message, error = false) {
    const el = $('toast');
    el.textContent = message;
    el.classList.toggle('error', error);
    el.classList.add('on');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => el.classList.remove('on'), error ? 5000 : 2500);
}

async function api(path, options) {
    const response = await fetch(path, options);
    const body = await response.json().catch(() => ({}));
    if (!response.ok) {
        throw new Error(body.error || `${response.status} ${response.statusText}`);
    }
    return body;
}

let pendingSwitch = null;
async function openMap(stem, { keepCamera = false } = {}) {
    if (S.dirty && stem !== S.stem && pendingSwitch !== stem) {
        pendingSwitch = stem;
        toast('Unsaved changes — save first, or pick the map again to discard them', true);
        return;
    }
    pendingSwitch = null;
    setLoading(true);
    const data = await api(`/api/map/${stem}`);
    S.stem = stem;
    S.original = data.map;
    S.context = data.context;
    S.events = data.events;
    const legend = data.map.tileBehaviorLegend;
    S.collision = data.map.collision.map((row) => [...row].map((ch) => ch === '#'));
    S.names = data.map.tileBehaviors.map((row) => [...row].map((ch) => legend[ch]));
    S.props = clone(data.map.props);
    S.plates = clone(data.map.bdhc.plates);
    S.eventPos = {};
    if (!S.addModelChosen) {
        S.addModel = null;  // pick a default from the new area's prop set
    }
    S.undo = [];
    S.redo = [];
    S.selection = null;
    setDirty(false);
    $('map-name').textContent = stem;
    $('map-search').value = '';
    if (location.hash.slice(1) !== stem) {
        history.replaceState(null, '', `#${stem}`);
    }
    propObjects.forEach((entry) => propGroup.remove(entry.object));
    propObjects = [];
    renderPanels();
    draw();
    await Promise.all([loadTerrain(), syncProps3D()]);
    if (!keepCamera) {
        fitView();
        if (S.view === '3d') {
            setView('3d');
        }
    }
    syncMarkers3D();
    setLoading(false);
    requestRender();
    draw();
}

async function save() {
    if (!S.stem) {
        return;
    }
    const eventMoves = [];
    for (const { kind, event, key } of allEvents()) {
        const pos = S.eventPos[key];
        if (pos && (pos.x !== event.x || pos.z !== event.z)) {
            eventMoves.push({ kind, index: event.index, x: pos.x, z: pos.z });
        }
    }
    try {
        const result = await api(`/api/map/${S.stem}`, {
            method: 'PUT',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                map: { collision: S.collision, tileBehaviorNames: S.names, props: S.props, bdhc: { plates: S.plates } },
                eventMoves,
            }),
        });
        setDirty(false);
        toast(`Saved ${result.saved}${eventMoves.length ? ` and ${eventMoves.length} event move(s)` : ''}`);
        await openMap(S.stem, { keepCamera: true });
    } catch (err) {
        toast(`Not saved: ${err.message}`, true);
    }
}

let buildTimer = null;
async function pollBuild() {
    const status = await api('/api/build');
    const el = $('build-status');
    el.className = status.state;
    const last = (status.tail || []).filter(Boolean).pop() || '';
    el.textContent = { idle: '', running: `Building… ${last}`, ok: 'ROM built ✓', failed: `Build failed: ${last}` }[status.state];
    el.title = (status.tail || []).join('\n');
    $('build').disabled = status.state === 'running';
    clearTimeout(buildTimer);
    if (status.state === 'running') {
        buildTimer = setTimeout(pollBuild, 1500);
    }
}

async function init() {
    window.addEventListener('resize', resize);
    resize();
    const project = await api('/api/project');
    S.project = { ...project, propInternal: Object.fromEntries(project.props.map((p) => [p.name, p.internal])) };
    S.maps = await api('/api/maps');
    $('map-list').innerHTML = S.maps.map((m) => html`<option value="${m.name}">${m.header ? short(m.header) : 'unused'}</option>`).join('');
    $('map-search').addEventListener('change', (e) => {
        const value = e.target.value.trim();
        const match = S.maps.find((m) => m.name === value) || S.maps.find((m) => m.name.includes(value.toLowerCase().replace(/\s+/g, '_')));
        if (match) {
            openMap(match.name);
        }
    });
    document.querySelectorAll('#tools [data-tool]').forEach((b) => b.addEventListener('click', () => setTool(b.dataset.tool)));
    document.querySelectorAll('[data-view]').forEach((b) => b.addEventListener('click', () => setView(b.dataset.view)));
    document.querySelectorAll('[data-layer]').forEach((input) => input.addEventListener('change', () => {
        S.layers[input.dataset.layer] = input.checked;
        syncMarkers3D();
        requestRender();
        draw();
    }));
    $('save').addEventListener('click', save);
    $('undo').addEventListener('click', undo);
    $('redo').addEventListener('click', redo);
    $('build').addEventListener('click', async () => {
        await api('/api/build', { method: 'POST' });
        pollBuild();
    });
    window.addEventListener('hashchange', () => openMap(location.hash.slice(1)));
    window.addEventListener('beforeunload', (e) => {
        if (S.dirty) {
            e.preventDefault();
        }
    });
    setTool('select');
    pollBuild();
    await openMap(location.hash.slice(1) || 'map_twinleaf_town');
    // Handy from the browser console (and for scripted screenshots).
    window.editor = {
        S, openMap, setTool, setView, requestRender, draw,
        pageXY(x, z) {
            const r = overlay.getBoundingClientRect();
            const p = toScreen(x, z);
            return { x: r.left + p.x, y: r.top + p.y };
        },
        tileXY(c, r) {
            return this.pageXY(c * TILE - HALF + TILE / 2, r * TILE - HALF + TILE / 2);
        },
    };
}

init().catch((err) => toast(err.message, true));
