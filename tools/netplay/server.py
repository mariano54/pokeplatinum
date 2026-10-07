#!/usr/bin/env python3
"""Netplay relay server: forwards messages between the bridges of two players.

    python3 tools/netplay/server.py [--port 4545]

Every bridge connects here and sends JSON lines (its player's state, battle/trade
requests and responses); each message is forwarded to the other connected bridge.
The last known state of each player is replayed to bridges that connect later.
"""
import argparse
import json
import socketserver
import threading

clients = {}  # handler -> name
last_state = {}  # handler -> state message
lock = threading.Lock()


def log(*args):
    print('[server]', *args, flush=True)


class Handler(socketserver.StreamRequestHandler):
    def send(self, message: dict):
        try:
            self.wfile.write((json.dumps(message) + '\n').encode())
            self.wfile.flush()
        except OSError:
            pass

    def broadcast(self, message: dict):
        with lock:
            others = [h for h in clients if h is not self]
        for other in others:
            other.send(message)

    def handle(self):
        with lock:
            clients[self] = '?'
            replay = [m for h, m in last_state.items() if h is not self]
        for message in replay:
            self.send(message)

        for line in self.rfile:
            try:
                message = json.loads(line)
            except json.JSONDecodeError:
                continue
            kind = message.get('type')
            if kind == 'hello':
                with lock:
                    clients[self] = message.get('name', '?')
                log(f'{clients[self]} connected ({len(clients)} player(s))')
                continue
            if kind == 'state':
                with lock:
                    last_state[self] = message
            else:
                log(f'{clients[self]} -> {kind} {message.get("request", message.get("response", ""))}')
            self.broadcast(message)

        with lock:
            name = clients.pop(self, '?')
            last_state.pop(self, None)
        log(f'{name} disconnected')
        self.broadcast({'type': 'peer_left'})


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=4545)
    args = parser.parse_args()
    with Server((args.host, args.port), Handler) as server:
        log(f'listening on {args.host}:{args.port}')
        server.serve_forever()


if __name__ == '__main__':
    main()
