#!/usr/bin/env python3
"""Connects one emulator running the netplay ROM to the relay server.

    python3 tools/netplay/bridge.py --gdb-port 3333 --nef build/main.nef [--server 127.0.0.1:4545]

The game keeps its side of the conversation in `gNetPlay` (include/netplay.h). This
bridge reads and writes that struct through the emulator's GDB stub ~15 times a
second and exchanges it with the other player's bridge through server.py.

It also listens on a control port (default: GDB port + 1000) for JSON lines, used
to drive the game from scripts: {"press": ["A"], "frames": 6} holds buttons, and
{"status": true} returns the decoded mailbox.
"""
import argparse
import json
import pathlib
import queue
import socket
import socketserver
import struct
import subprocess
import sys
import threading
import time
import traceback

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from gdb_link import GdbLink  # noqa: E402

MAGIC = 0x594C504E
STATE_SIZE = 0xD48

# Offsets inside NetPlayState
OFF_MAGIC = 0x00
OFF_FRAME = 0x08
OFF_INJECT_KEYS = 0x0C
OFF_REMOTE_CONNECTED = 0x0E
OFF_LOCAL = 0x10
OFF_REMOTE = 0x2C
PLAYER_SIZE = 0x1C
OFF_OUT_REQUEST = 0x48
OFF_IN_RESPONSE = 0x49
OFF_IN_REQUEST = 0x4A
OFF_OUT_RESPONSE = 0x4B
OFF_OUT_MON = 0x50
OFF_IN_MON = 0x13C
MON_SIZE = 0xEC
OFF_OUT_PARTY = 0x228
OFF_IN_PARTY = 0x7B8
PARTY_SIZE = 0x590

REQUEST_BATTLE, REQUEST_TRADE, REQUEST_CANCEL = 1, 2, 3
RESPONSE_ACCEPT = 1

KEYS = {'A': 0x1, 'B': 0x2, 'SELECT': 0x4, 'START': 0x8, 'RIGHT': 0x10, 'LEFT': 0x20, 'UP': 0x40,
        'DOWN': 0x80, 'R': 0x100, 'L': 0x200, 'X': 0x400, 'Y': 0x800}


def decode_name(raw: bytes) -> str:
    out = ''
    for (c,) in struct.iter_unpack('<H', raw):
        if c == 0xFFFF:
            break
        if 0x12B <= c <= 0x144:
            out += chr(ord('A') + c - 0x12B)
        elif 0x145 <= c <= 0x15E:
            out += chr(ord('a') + c - 0x145)
        elif 0x121 <= c <= 0x12A:
            out += chr(ord('0') + c - 0x121)
        else:
            out += '?'
    return out


def decode_player(raw: bytes) -> dict:
    header, matrix, x, z, direction, gender, in_field, seq = struct.unpack_from('<HHhhBBBB', raw)
    return {'map': header, 'matrix': matrix, 'x': x, 'z': z, 'dir': direction, 'gender': gender,
            'inField': in_field, 'seq': seq, 'name': decode_name(raw[12:28])}


def find_symbol(nef: pathlib.Path, symbol: str) -> tuple[int, int]:
    nm = subprocess.run(['arm-none-eabi-nm', '-S', str(nef)], capture_output=True, text=True, check=True).stdout
    for line in nm.splitlines():
        parts = line.split()
        if len(parts) == 4 and parts[3] == symbol:
            return int(parts[0], 16), int(parts[1], 16)
    raise SystemExit(f'{symbol} not found in {nef}; is this the netplay build?')


class Bridge:
    def __init__(self, args):
        self.args = args
        self.addr, size = find_symbol(args.nef, 'gNetPlay')
        if size != STATE_SIZE:
            raise SystemExit(f'gNetPlay is {size:#x} bytes, expected {STATE_SIZE:#x}: update the offsets in bridge.py')
        self.incoming = queue.Queue()  # messages from the server
        self.commands = queue.Queue()  # (command, reply queue) from the control port
        self.server = None
        self.last_local = None
        self.pending_in = 0  # request type the other player sent us
        self.pending_out = 0  # request type we sent
        self.keys_until = 0.0
        self.status = {}

    def log(self, *args):
        print(f'[bridge {self.args.gdb_port}]', *args, flush=True)

    # --- server connection ----------------------------------------------
    def connect_server(self):
        host, port = self.args.server.rsplit(':', 1)
        while True:
            try:
                self.server = socket.create_connection((host, int(port)))
                break
            except OSError:
                time.sleep(1)
        threading.Thread(target=self.read_server, daemon=True).start()

    def read_server(self):
        for line in self.server.makefile('r'):
            try:
                self.incoming.put(json.loads(line))
            except json.JSONDecodeError:
                pass
        self.log('server connection closed')

    def send(self, message: dict):
        self.server.sendall((json.dumps(message) + '\n').encode())

    # --- control port -----------------------------------------------------
    def serve_control(self):
        bridge = self

        class Handler(socketserver.StreamRequestHandler):
            def handle(self):
                for line in self.rfile:
                    reply = queue.Queue()
                    bridge.commands.put((json.loads(line), reply))
                    self.wfile.write((json.dumps(reply.get()) + '\n').encode())

        class Server(socketserver.ThreadingTCPServer):
            allow_reuse_address = True
            daemon_threads = True

        server = Server(('127.0.0.1', self.args.control_port), Handler)
        threading.Thread(target=server.serve_forever, daemon=True).start()

    # --- main loop ---------------------------------------------------------
    def run(self):
        self.connect_server()
        self.serve_control()
        link = GdbLink(self.args.gdb_port)
        self.log(f'connected to emulator; gNetPlay at {self.addr:#x}')
        hello_sent = False

        while True:
            link.halt()
            try:
                state = link.read(self.addr, 0x50)
                if struct.unpack_from('<I', state, OFF_MAGIC)[0] == MAGIC:
                    if not hello_sent:
                        self.send({'type': 'hello', 'name': f'port {self.args.gdb_port}'})
                        hello_sent = True
                    self.sync(link, state)
                self.handle_commands(link, state)
            except (EOFError, ConnectionError):
                raise
            except Exception:
                # A failed read or write shouldn't end the session; log it and try again next tick.
                traceback.print_exc()
            finally:
                link.resume()
            time.sleep(1 / 15)

    def sync(self, link: GdbLink, state: bytes):
        local_raw = state[OFF_LOCAL:OFF_LOCAL + PLAYER_SIZE]
        local = decode_player(local_raw)
        if local_raw != self.last_local and local['inField']:
            self.send({'type': 'state', 'player': local_raw.hex()})
            self.last_local = local_raw
        self.status = {'frame': struct.unpack_from('<I', state, OFF_FRAME)[0], 'local': local,
                       'remote': decode_player(state[OFF_REMOTE:OFF_REMOTE + PLAYER_SIZE]),
                       'remoteConnected': state[OFF_REMOTE_CONNECTED]}

        out_request = state[OFF_OUT_REQUEST]
        if out_request:
            message = {'type': 'request', 'request': out_request}
            if out_request == REQUEST_TRADE:
                message['mon'] = link.read(self.addr + OFF_OUT_MON, MON_SIZE).hex()
            elif out_request == REQUEST_BATTLE:
                message['party'] = link.read(self.addr + OFF_OUT_PARTY, PARTY_SIZE).hex()
            self.pending_out = out_request if out_request != REQUEST_CANCEL else 0
            link.write(self.addr + OFF_OUT_REQUEST, b'\0')
            self.send(message)
            self.log(f'{local["name"]} sent request {out_request}')

        out_response = state[OFF_OUT_RESPONSE]
        if out_response:
            message = {'type': 'response', 'response': out_response}
            if out_response == RESPONSE_ACCEPT:
                if self.pending_in == REQUEST_TRADE:
                    message['mon'] = link.read(self.addr + OFF_OUT_MON, MON_SIZE).hex()
                elif self.pending_in == REQUEST_BATTLE:
                    message['party'] = link.read(self.addr + OFF_OUT_PARTY, PARTY_SIZE).hex()
            link.write(self.addr + OFF_OUT_RESPONSE, b'\0')
            self.send(message)
            self.log(f'{local["name"]} answered {out_response} to request {self.pending_in}')
            self.pending_in = 0

        while not self.incoming.empty():
            message = self.incoming.get()
            kind = message.get('type')
            if kind == 'state':
                link.write(self.addr + OFF_REMOTE, bytes.fromhex(message['player']))
                link.write(self.addr + OFF_REMOTE_CONNECTED, b'\1')
            elif kind == 'peer_left':
                link.write(self.addr + OFF_REMOTE_CONNECTED, b'\0')
            elif kind == 'request':
                request = message['request']
                if 'mon' in message:
                    link.write(self.addr + OFF_IN_MON, bytes.fromhex(message['mon']))
                if 'party' in message:
                    link.write(self.addr + OFF_IN_PARTY, bytes.fromhex(message['party']))
                self.pending_in = request if request != REQUEST_CANCEL else 0
                link.write(self.addr + OFF_IN_REQUEST, bytes([request]))
            elif kind == 'response':
                if 'mon' in message:
                    link.write(self.addr + OFF_IN_MON, bytes.fromhex(message['mon']))
                if 'party' in message:
                    link.write(self.addr + OFF_IN_PARTY, bytes.fromhex(message['party']))
                link.write(self.addr + OFF_IN_RESPONSE, bytes([message['response']]))
                self.pending_out = 0

    def handle_commands(self, link: GdbLink, state: bytes):
        if self.keys_until and time.time() >= self.keys_until:
            link.write(self.addr + OFF_INJECT_KEYS, b'\0\0')
            self.keys_until = 0.0
        while not self.commands.empty():
            command, reply = self.commands.get()
            if 'press' in command:
                mask = 0
                for key in command['press']:
                    mask |= KEYS[key.upper()]
                link.write(self.addr + OFF_INJECT_KEYS, struct.pack('<H', mask))
                self.keys_until = time.time() + command.get('frames', 6) / 60
                reply.put({'ok': True})
            elif command.get('status'):
                reply.put(self.status or {'magic': struct.unpack_from('<I', state, OFF_MAGIC)[0] == MAGIC})
            else:
                reply.put({'error': 'unknown command'})


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--gdb-port', type=int, required=True)
    parser.add_argument('--nef', type=pathlib.Path, required=True, help='build/main.nef of the ROM being played')
    parser.add_argument('--server', default='127.0.0.1:4545')
    parser.add_argument('--control-port', type=int)
    args = parser.parse_args()
    if args.control_port is None:
        args.control_port = args.gdb_port + 1000
    Bridge(args).run()


if __name__ == '__main__':
    main()
