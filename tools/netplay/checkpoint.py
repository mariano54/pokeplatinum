#!/usr/bin/env python3
"""Saves or loads both players' emulator states at once (needs the patched melonDS).

    python3 tools/netplay/checkpoint.py save next-to-each-other
    python3 tools/netplay/checkpoint.py load next-to-each-other
    python3 tools/netplay/checkpoint.py list

Checkpoints live in build/netplay/states/NAME/playerN.mln. Loading one puts both games
back exactly where they were, which makes testing a battle or a trade a matter of
seconds: no booting, walking or menus. A savestate holds the game code that was running,
so a checkpoint only loads with the ROM it was saved with: save it again after a rebuild.
"""
import argparse
import hashlib
import json
import pathlib
import socket
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
CONTROL_PORTS = [4333, 4433]  # bridge control ports: GDB port + 1000


def command(port: int, message: dict, timeout: float = 30) -> dict:
    with socket.create_connection(('127.0.0.1', port), timeout=timeout) as sock:
        sock.sendall((json.dumps(message) + '\n').encode())
        return json.loads(sock.makefile('r').readline())


def wait_for_bridges(ports: list[int], timeout: float = 60):
    deadline = time.time() + timeout
    for port in ports:
        while True:
            try:
                if 'frame' in command(port, {'status': True}, timeout=5):
                    break
            except OSError:
                pass
            if time.time() > deadline:
                raise SystemExit(f'bridge on port {port} is not ready')
            time.sleep(0.5)


def rom_hash(work_dir: pathlib.Path) -> str:
    return hashlib.sha1((work_dir / 'player1' / 'pokeplatinum.nds').read_bytes()).hexdigest()


def run(verb: str, work_dir: pathlib.Path, name: str, ports: list[int], force: bool = False):
    folder = work_dir / 'states' / name
    if verb == 'save':
        folder.mkdir(parents=True, exist_ok=True)
        (folder / 'rom.sha1').write_text(rom_hash(work_dir) + '\n')
    elif not folder.is_dir():
        raise SystemExit(f'no checkpoint {name!r} in {folder.parent}')
    elif not force and (folder / 'rom.sha1').exists() and (folder / 'rom.sha1').read_text().strip() != rom_hash(work_dir):
        raise SystemExit(f'checkpoint {name!r} was saved with a different ROM build: save it again '
                         '(or pass --force to load it anyway)')
    wait_for_bridges(ports)
    for index, port in enumerate(ports):
        path = folder / f'player{index + 1}.mln'
        # Both states were saved together, so each game's copy of the other player is right.
        reply = command(port, {f'{verb}state': str(path), 'keepRemote': False})
        if not reply.get('ok'):
            raise SystemExit(f'player {index + 1}: {verb} failed: {reply.get("error")}')


def load_checkpoint(work_dir: pathlib.Path, name: str, ports: list[int], force: bool = False):
    run('load', work_dir, name, ports, force)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('verb', choices=['save', 'load', 'list'])
    parser.add_argument('name', nargs='?')
    parser.add_argument('--work-dir', type=pathlib.Path, default=ROOT / 'build' / 'netplay')
    parser.add_argument('--ports', type=int, nargs='+', default=CONTROL_PORTS)
    parser.add_argument('--force', action='store_true', help='load even if the ROM has changed since')
    args = parser.parse_args()
    if args.verb == 'list':
        for folder in sorted((args.work_dir / 'states').glob('*')):
            print(folder.name)
        return
    if not args.name:
        parser.error('a checkpoint name is needed')
    started = time.time()
    run(args.verb, args.work_dir, args.name, args.ports, args.force)
    print(f'{"Saved" if args.verb == "save" else "Loaded"} {args.name} in {time.time() - started:.2f}s')


if __name__ == '__main__':
    main()
