#!/usr/bin/env python3
"""Starts a two-player netplay session: relay server, two melonDS emulators, two bridges.

    python3 tools/netplay/play.py --melonds /path/to/melonDS.app [--names LUCAS DAWN]

Build the netplay ROM first (`make rom` on this branch). Each player gets a portable
copy of melonDS in --work-dir (muted, with its own GDB port and firmware name), and
its own copy of the ROM so their save files don't collide. Choosing NEW GAME skips
the intro: the player is named after the emulator's firmware name and starts in
Twinleaf Town with three Pokémon. Talk to the other player to battle or trade.
Ctrl+C stops everything.
"""
import argparse
import os
import pathlib
import shutil
import signal
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
HERE = pathlib.Path(__file__).resolve().parent

sys.path.insert(0, str(HERE))
from checkpoint import load_checkpoint  # noqa: E402


def melonds_config(name: str, gdb_port: int, birthday_month: int, speed: float, jit: bool) -> str:
    # melonDS (1.1) needs every parent table spelled out, or it fails to save its config.
    limit = 'false' if speed == 0 else 'true'
    return f'''LimitFPS = {limit}
TargetFPS = {60 * (speed or 1):.1f}

[JIT]
Enable = {'true' if jit else 'false'}

[Emu]
DirectBoot = true

[Instance0]

[Instance0.Audio]
Volume = 0

[Instance0.Firmware]
OverrideSettings = true
Username = "{name}"
Language = 1
BirthdayMonth = {birthday_month}
BirthdayDay = 1
FavouriteColour = 0

[Instance0.Gdb]
Enabled = true

[Instance0.Gdb.ARM9]
Port = {gdb_port}
BreakOnStartup = false

[Instance0.Gdb.ARM7]
Port = {gdb_port + 1}
BreakOnStartup = false
'''


def copy_app(melonds: pathlib.Path, app: pathlib.Path):
    exe = pathlib.Path('Contents/MacOS/melonDS')
    if app.exists():
        source, copy = (melonds / exe).stat(), (app / exe).stat()
        if (source.st_size, source.st_mtime) == (copy.st_size, copy.st_mtime):
            return
        shutil.rmtree(app)  # a different melonDS than last time
    shutil.copytree(melonds, app, symlinks=True)


def setup_player(args, index: int, name: str, gdb_port: int):
    player_dir = args.work_dir / f'player{index + 1}'
    player_dir.mkdir(parents=True, exist_ok=True)
    app = player_dir / 'melonDS.app'
    copy_app(args.melonds, app)
    (player_dir / 'portable').mkdir(exist_ok=True)
    # An odd birthday month makes the player Lucas, an even one Dawn (see NetPlay_QuickStartTrainer).
    config = melonds_config(name, gdb_port, 1 + index % 2, args.speed, args.jit)
    (player_dir / 'portable' / 'melonDS.toml').write_text(config)
    player_rom = player_dir / 'pokeplatinum.nds'
    shutil.copy2(args.rom, player_rom)
    return app / 'Contents' / 'MacOS' / 'melonDS', player_rom


def build_background_shim(work_dir: pathlib.Path) -> pathlib.Path:
    dylib = work_dir / 'background_shim.dylib'
    subprocess.run(['clang', '-arch', 'x86_64', '-arch', 'arm64', '-dynamiclib', '-framework', 'AppKit',
                    '-o', dylib, HERE / 'background_shim.m'], check=True)
    return dylib


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--melonds', type=pathlib.Path, required=True, help='path to melonDS.app (1.1+)')
    parser.add_argument('--rom', type=pathlib.Path, default=ROOT / 'build' / 'pokeplatinum.us.nds')
    parser.add_argument('--nef', type=pathlib.Path, default=ROOT / 'build' / 'main.nef')
    parser.add_argument('--work-dir', type=pathlib.Path, default=ROOT / 'build' / 'netplay')
    parser.add_argument('--names', nargs=2, default=['LUCAS', 'DAWN'])
    parser.add_argument('--server-port', type=int, default=4545)
    parser.add_argument('--gdb-ports', type=int, nargs=2, default=[3333, 3433])
    parser.add_argument('--speed', type=float, default=1,
                        help='emulation speed: 1 is normal, 4 is four times as fast, 0 is as fast as possible')
    parser.add_argument('--jit', action='store_true', help='use the JIT recompiler (patched melonDS only)')
    parser.add_argument('--load', metavar='CHECKPOINT',
                        help='start from a checkpoint saved with checkpoint.py (patched melonDS only)')
    parser.add_argument('--no-background-shim', action='store_true',
                        help="macOS: don't load background_shim.m, which keeps the games drawing while their "
                             "windows are covered and stops them from taking the keyboard when they start")
    parser.add_argument('--emulator-logs', action='store_true', help='keep melonDS output in playerN/melonds.log')
    args = parser.parse_args()

    procs = []

    def stop(*_):
        for proc in reversed(procs):
            proc.terminate()
        time.sleep(2)
        for proc in procs:
            if proc.poll() is None:
                proc.kill()  # melonDS ignores SIGTERM
        sys.exit(0)

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)

    melonds_env = dict(os.environ)
    if sys.platform == 'darwin' and not args.no_background_shim:
        args.work_dir.mkdir(parents=True, exist_ok=True)
        melonds_env['DYLD_INSERT_LIBRARIES'] = str(build_background_shim(args.work_dir))

    py = sys.executable
    procs.append(subprocess.Popen([py, HERE / 'server.py', '--port', str(args.server_port)]))
    for i, (name, port) in enumerate(zip(args.names, args.gdb_ports)):
        exe, rom = setup_player(args, i, name, port)
        log = open(args.work_dir / f'player{i + 1}' / 'melonds.log', 'w') if args.emulator_logs else subprocess.DEVNULL
        procs.append(subprocess.Popen([exe, rom], stdout=log, stderr=subprocess.STDOUT, env=melonds_env))
    time.sleep(3)
    for port in args.gdb_ports:
        procs.append(subprocess.Popen([py, HERE / 'bridge.py', '--gdb-port', str(port), '--nef', args.nef,
                                       '--server', f'127.0.0.1:{args.server_port}']))

    if args.load:
        load_checkpoint(args.work_dir, args.load, [port + 1000 for port in args.gdb_ports])
        print(f'Loaded checkpoint {args.load}.', flush=True)

    print(f'Netplay running: {args.names[0]} and {args.names[1]}. Press Ctrl+C to stop.', flush=True)
    while all(p.poll() is None for p in procs):
        time.sleep(1)
    stop()


if __name__ == '__main__':
    main()
