#!/usr/bin/env python3
"""Sends commands to a bridge's control port, to script the game.

    drive.py 4333 status
    drive.py 4333 press A [frames]
    drive.py 4333 press UP,B 30
"""
import json
import socket
import sys


def command(port: int, message: dict) -> dict:
    with socket.create_connection(('127.0.0.1', port)) as sock:
        sock.sendall((json.dumps(message) + '\n').encode())
        return json.loads(sock.makefile('r').readline())


def main():
    port = int(sys.argv[1])
    if sys.argv[2] == 'status':
        print(json.dumps(command(port, {'status': True}), indent=1))
    elif sys.argv[2] == 'press':
        frames = int(sys.argv[4]) if len(sys.argv) > 4 else 6
        print(command(port, {'press': sys.argv[3].split(','), 'frames': frames}))


if __name__ == '__main__':
    main()
