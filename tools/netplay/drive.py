#!/usr/bin/env python3
"""Sends commands to a bridge's control port, to script the game. Times are in game frames.

    drive.py 4333 status
    drive.py 4333 press A [frames]        hold A for 6 (or FRAMES) frames
    drive.py 4333 press UP,B 30
    drive.py 4333 wait 60                 return once the game has run 60 more frames
    drive.py 4333 savestate PATH          save the emulator's state (patched melonDS, see README)
    drive.py 4333 loadstate PATH
    drive.py 4333 speed 4                 4x speed; 0 runs as fast as possible
"""
import json
import socket
import sys


def command(port: int, message: dict) -> dict:
    with socket.create_connection(('127.0.0.1', port)) as sock:
        sock.sendall((json.dumps(message) + '\n').encode())
        return json.loads(sock.makefile('r').readline())


def main():
    port, verb, rest = int(sys.argv[1]), sys.argv[2], sys.argv[3:]
    if verb == 'status':
        print(json.dumps(command(port, {'status': True}), indent=1))
    elif verb == 'press':
        frames = int(rest[1]) if len(rest) > 1 else 6
        print(command(port, {'press': rest[0].split(','), 'frames': frames, 'wait': True}))
    elif verb == 'wait':
        print(command(port, {'wait': int(rest[0])}))
    elif verb in ('savestate', 'loadstate'):
        print(command(port, {verb: rest[0]}))
    elif verb == 'speed':
        print(command(port, {'speed': float(rest[0])}))
    else:
        raise SystemExit(__doc__)


if __name__ == '__main__':
    main()
