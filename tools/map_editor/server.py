#!/usr/bin/env python3
"""Local web editor for the maps in res/field/maps (see docs/maps/map_editor.md).

    python3 tools/map_editor/server.py [--port 8000] [--apicula PATH] [--build-cmd 'make rom']

then open http://localhost:8000. Edits are written back to the map and events JSON
files, validated with the same packer the build uses.

3D previews of the terrain and props need apicula (https://github.com/scurest/apicula),
found on PATH, through $APICULA or --apicula. Without it, the editor works in 2D.
"""
import argparse
import http.server
import json
import os
import pathlib
import re
import shlex
import shutil
import subprocess
import sys
import threading
import urllib.parse

ROOT = pathlib.Path(__file__).resolve().parents[2]
PROJECT = CONVERTER = BUILDER = None  # set up in main()
STATIC = pathlib.Path(__file__).resolve().parent / 'static'
CACHE = ROOT / 'build' / 'map_editor'

sys.path.insert(0, str(ROOT / 'tools' / 'jsoncnv'))
sys.path.insert(0, str(ROOT / 'tools' / 'scripts'))
import g3d_sources  # noqa: E402  (assets kept as editable PNG/JSON sources are packed on demand)
sys.path.insert(0, str(ROOT / 'tools' / 'scripts'))
import map_data  # noqa: E402
import make_prop_model_catalog  # noqa: E402
import unpack_map_data  # noqa: E402

MAPS_DIR = ROOT / 'res/field/maps/data'
MODELS_DIR = ROOT / 'res/field/maps/models'
EVENT_KINDS = ('warp_events', 'object_events', 'bg_events', 'coord_events')


class Project:
    """Read-only indexes over the repository, built once at startup."""

    def __init__(self):
        self.behaviors = map_data.parse_tile_behaviors(ROOT / 'include/constants/field/map_tile_behaviors.h')

        order = (ROOT / 'res/field/props/models/map_prop_models.order').read_text().split()
        self.prop_files = {f.rsplit('.', 1)[0] + '_nsbmd': f for f in order}
        self.prop_models = {name: i for i, name in enumerate(self.prop_files)}
        self.prop_internal = {
            name: make_prop_model_catalog.nsbmd_model_name(
                g3d_sources.read_bytes(ROOT / 'res/field/props/models' / f, CACHE / 'packed'))
            for name, f in self.prop_files.items()
        }

        self.headers = {}
        source = (ROOT / 'include/data/map_headers.h').read_text()
        for match in re.finditer(r'\[(MAP_HEADER_\w+)\] = \{(.*?)\n    \}', source, re.S):
            fields = dict(re.findall(r'\.(\w+) = (\w+)', match.group(2)))
            self.headers[match.group(1)] = fields

        self.uses = {}  # map constant -> [(matrix, x, y, cell header or None)]
        for path in sorted((ROOT / 'res/field/matrices').glob('map_matrix_*.json')):
            matrix = json.loads(path.read_text())
            for y, row in enumerate(matrix['maps']):
                for x, name in enumerate(row):
                    if name != 'MAP_NONE':
                        header = matrix['headers'][y][x] if matrix['headers'] else None
                        self.uses.setdefault(name, []).append((path.stem, x, y, header))

    def map_path(self, stem):
        path = MAPS_DIR / f'{stem}.json'
        if not re.fullmatch(r'map_\w+', stem) or not path.exists():
            raise KeyError(stem)
        return path

    def context(self, stem):
        """Header, area and position of a map, picked from its first use."""
        const = stem.upper()
        for matrix, x, y, header in self.uses.get(const, []):
            if header in (None, 'MAP_HEADER_EVERYWHERE', 'MAP_HEADER_NOTHING'):
                users = [h for h, f in self.headers.items() if f.get('mapMatrixID') == matrix]
                users.sort(key=lambda h: bool(re.match(r'MAP_HEADER_(UNKNOWN|UNUSED|EVERYWHERE|NOTHING)', h)))
                header = users[0] if users else None
            if header:
                return {'header': header, 'matrix': matrix, 'cellX': x, 'cellZ': y}
        return {'header': None, 'matrix': None, 'cellX': 0, 'cellZ': 0}

    def area(self, header):
        if not header:
            return {}
        area_id = self.headers[header]['areaDataArchiveID']
        area = json.loads((ROOT / 'res/field/area_data' / f'{area_id}.json').read_text())
        return {
            'areaData': area_id,
            'mapTextureSet': area['mapTextureSet'],
            'mapPropSet': area['mapPropSet'],
            'propTextureSet': area['mapPropSet'].replace('prop_model_set', 'prop_texture_set'),
            'propSetModels': json.loads((ROOT / 'res/field/props/model_sets' / f'{area["mapPropSet"]}.json').read_text())['mapPropModels'],
        }

    def events_path(self, header):
        events = self.headers.get(header, {}).get('eventsArchiveID')
        path = ROOT / 'res/field/events' / f'{events}.json'
        return path if events and path.exists() else None


def load_events(project, ctx):
    """Events of the map's header that lie on this map, in map-local tile coordinates."""
    path = project.events_path(ctx['header'])
    if path is None:
        return None
    events = json.loads(path.read_text(encoding='utf-8'))
    ox, oz = 32 * ctx['cellX'], 32 * ctx['cellZ']
    out = {'file': path.relative_to(ROOT).as_posix()}
    for kind in EVENT_KINDS:
        out[kind] = []
        for index, event in enumerate(events.get(kind, [])):
            if ox <= event['x'] < ox + 32 and oz <= event['z'] < oz + 32:
                out[kind].append({**event, 'index': index, 'x': event['x'] - ox, 'z': event['z'] - oz})
    return out


def save_events(project, ctx, moves):
    path = project.events_path(ctx['header'])
    text = path.read_text(encoding='utf-8')
    events = json.loads(text)
    ox, oz = 32 * ctx['cellX'], 32 * ctx['cellZ']
    for move in moves:
        event = events[move['kind']][move['index']]
        event['x'] = int(move['x']) + ox
        event['z'] = int(move['z']) + oz
    trailing = text[len(text.rstrip()):] or '\n'
    path.write_text(json.dumps(events, indent=4, ensure_ascii=False) + trailing, encoding='utf-8')


def compose_map(original, edited):
    """Turn the editor's map (tile behavior names per tile) back into the map JSON layout."""
    names = edited['tileBehaviorNames']
    legend = {char: name for char, name in original['tileBehaviorLegend'].items()}
    used = {name for row in names for name in row}
    legend = {char: name for char, name in legend.items() if name in used}
    by_name = {name: char for char, name in legend.items()}
    for name in sorted(used - set(by_name)):
        char = unpack_map_data.PREFERRED_CHARS.get(name)
        if char is None or char in legend or char == '#':
            char = next(c for c in unpack_map_data.FALLBACK_CHARS if c not in legend)
        legend[char] = name
        by_name[name] = char

    data = {
        'model': original['model'],
        'collision': [''.join('#' if blocked else '.' for blocked in row) for row in edited['collision']],
        'tileBehaviorLegend': legend,
        'tileBehaviors': [''.join(by_name[name] for name in row) for row in names],
        'props': edited['props'],
        'bdhc': edited['bdhc'],
    }
    if 'propsTrailingBytes' in original:
        data['propsTrailingBytes'] = original['propsTrailingBytes']
    return data


class Converter:
    """Converts NSBMD models to glTF with apicula, caching the results in build/map_editor."""

    def __init__(self, apicula):
        self.apicula = apicula
        self.locks = {}
        self.lock = threading.Lock()

    def model_dir(self, kind, name, texture_set):
        if kind == 'terrain':
            model = MODELS_DIR / json.loads(PROJECT.map_path(name).read_text(encoding='utf-8'))['model']
            textures = ROOT / 'res/field/maps/texture_sets' / f'{texture_set}.nsbtx'
        else:
            model = ROOT / 'res/field/props/models' / PROJECT.prop_files[name]
            textures = ROOT / 'res/field/props/texture_sets' / f'{texture_set}.nsbtx'
        out = CACHE / kind / f'{name}__{texture_set}'
        with self.lock:
            lock = self.locks.setdefault(out, threading.Lock())
        with lock:
            model = g3d_sources.binary_path(model, CACHE / 'packed')
            textures = g3d_sources.binary_path(textures, CACHE / 'packed')
            if kind != 'terrain' and textures.exists():
                # The game binds the area texture set over the model's own TEX0.
                model = g3d_sources.without_textures(model, CACHE / 'untextured')
            stamp = f'{model.stat().st_mtime_ns}:{textures.stat().st_mtime_ns if textures.exists() else 0}'
            stale = not out.exists() or (out / '.stamp').read_text() != stamp
            if stale:
                shutil.rmtree(out, ignore_errors=True)
                out.parent.mkdir(parents=True, exist_ok=True)
                inputs = [str(model)] + ([str(textures)] if textures.exists() else [])
                subprocess.run([self.apicula, 'convert', *inputs, '-f', 'glb', '-o', str(out)],
                               check=True, capture_output=True)
                (out / '.stamp').write_text(stamp)
        return out


class Builder:
    def __init__(self, command):
        self.command = command
        self.process = None
        self.log = CACHE / 'build.log'

    def start(self):
        if self.process and self.process.poll() is None:
            return
        CACHE.mkdir(parents=True, exist_ok=True)
        log = open(self.log, 'w')
        self.process = subprocess.Popen(self.command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)

    def status(self):
        if self.process is None:
            return {'state': 'idle'}
        code = self.process.poll()
        tail = self.log.read_text(errors='replace').splitlines()[-12:] if self.log.exists() else []
        return {'state': 'running' if code is None else ('ok' if code == 0 else 'failed'), 'tail': tail}


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(STATIC), **kwargs)

    def log_message(self, fmt, *args):
        pass

    def send_json(self, value, status=200):
        body = json.dumps(value).encode()
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def read_json(self):
        return json.loads(self.rfile.read(int(self.headers['Content-Length'])))

    def do_GET(self):
        url = urllib.parse.urlparse(self.path)
        parts = url.path.strip('/').split('/')
        try:
            if url.path == '/api/project':
                return self.send_json({
                    'behaviors': sorted(PROJECT.behaviors, key=PROJECT.behaviors.get),
                    'props': [{'name': n, 'internal': PROJECT.prop_internal[n]} for n in PROJECT.prop_models],
                    'has3d': CONVERTER.apicula is not None,
                })
            if url.path == '/api/maps':
                maps = []
                for path in sorted(MAPS_DIR.glob('map_*.json')):
                    ctx = PROJECT.context(path.stem)
                    maps.append({'name': path.stem, 'header': ctx['header'], 'matrix': ctx['matrix']})
                return self.send_json(maps)
            if parts[:2] == ['api', 'map'] and len(parts) == 3:
                path = PROJECT.map_path(parts[2])
                ctx = PROJECT.context(parts[2])
                return self.send_json({
                    'map': json.loads(path.read_text(encoding='utf-8')),
                    'context': {**ctx, **PROJECT.area(ctx['header'])},
                    'events': load_events(PROJECT, ctx),
                })
            if url.path == '/api/build':
                return self.send_json(BUILDER.status())
            if parts[0] == 'models' and len(parts) == 5 and parts[1] in ('terrain', 'prop'):
                # /models/<kind>/<name>/<texture set>/<file>
                if CONVERTER.apicula is None:
                    return self.send_error(404, 'apicula not available')
                kind, name, texture_set, file = parts[1:]
                if kind == 'terrain':
                    PROJECT.map_path(name)
                elif name not in PROJECT.prop_files:
                    raise KeyError(name)
                if not re.fullmatch(r'\w+', texture_set) or not re.fullmatch(r'[\w.-]+', file):
                    raise KeyError(file)
                out = CONVERTER.model_dir(kind, name, texture_set)
                target = next(out.glob('*.glb')) if file == 'model.glb' else out / file
                if not target.exists():
                    raise KeyError(file)
                body = target.read_bytes()
                self.send_response(200)
                self.send_header('Content-Type', 'model/gltf-binary' if file.endswith('.glb') else 'image/png')
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return
        except (KeyError, StopIteration):
            return self.send_error(404)
        except subprocess.CalledProcessError as err:
            return self.send_error(500, err.stderr.decode(errors='replace')[:200])
        return super().do_GET()

    def do_PUT(self):
        parts = urllib.parse.urlparse(self.path).path.strip('/').split('/')
        if parts[:2] != ['api', 'map'] or len(parts) != 3:
            return self.send_error(404)
        try:
            path = PROJECT.map_path(parts[2])
        except KeyError:
            return self.send_error(404)
        body = self.read_json()
        original = json.loads(path.read_text(encoding='utf-8'))
        try:
            data = compose_map(original, body['map'])
            text = unpack_map_data.dump_json(data)
            model = (MODELS_DIR / data['model']).read_bytes()
            map_data.pack_map_data(json.loads(text), model, PROJECT.behaviors, PROJECT.prop_models)
        except (map_data.MapDataError, KeyError, ValueError) as err:
            return self.send_json({'error': str(err)}, 400)
        path.write_text(text, encoding='utf-8')
        if body.get('eventMoves'):
            save_events(PROJECT, PROJECT.context(parts[2]), body['eventMoves'])
        self.send_json({'saved': path.relative_to(ROOT).as_posix()})

    def do_POST(self):
        if self.path == '/api/build':
            BUILDER.start()
            return self.send_json(BUILDER.status())
        self.send_error(404)


def main():
    global PROJECT, CONVERTER, BUILDER
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--port', type=int, default=8000)
    parser.add_argument('--apicula', default=shutil.which('apicula') or None)
    parser.add_argument('--build-cmd', default='make rom', help='command run by the "Build ROM" button')
    args = parser.parse_args()
    apicula = args.apicula or os.environ.get('APICULA')

    PROJECT = Project()
    CONVERTER = Converter(apicula)
    BUILDER = Builder(shlex.split(args.build_cmd))
    server = http.server.ThreadingHTTPServer(('127.0.0.1', args.port), Handler)
    print(f'Map editor: http://localhost:{args.port}' + ('' if apicula else '  (2D only: apicula not found)'))
    server.serve_forever()


if __name__ == '__main__':
    main()
