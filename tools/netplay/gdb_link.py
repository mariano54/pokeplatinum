"""Minimal GDB remote serial protocol client for melonDS's ARM9 stub: pause, read/write RAM, resume."""
import socket
import time

# melonDS's stub replies E02 to a 0x400-byte read: its packets hold ~1 KB of hex.
CHUNK = 0x100


class GdbLink:
    def __init__(self, port, host='127.0.0.1', timeout=5.0):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        self.buf = b''
        self.running = True
        self.sock.sendall(b'+')  # melonDS expects an initial ack
        self.halt()

    # --- packet layer ---------------------------------------------------
    def _send(self, payload: str):
        data = payload.encode('latin-1')
        checksum = sum(data) & 0xFF
        self.sock.sendall(b'$' + data + b'#' + f'{checksum:02x}'.encode())
        self._expect_ack()

    def _expect_ack(self):
        while True:
            c = self._read_byte()
            if c == b'+':
                return
            if c == b'-':
                raise IOError('gdb stub NAKed a packet')

    def _read_byte(self):
        if not self.buf:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise EOFError('gdb stub closed the connection')
            self.buf += chunk
        c, self.buf = self.buf[:1], self.buf[1:]
        return c

    def _recv_packet(self) -> str:
        while self._read_byte() != b'$':
            pass
        data = b''
        while True:
            c = self._read_byte()
            if c == b'#':
                break
            data += c
        self._read_byte(), self._read_byte()  # checksum
        self.sock.sendall(b'+')
        return data.decode('latin-1')

    def command(self, payload: str) -> str:
        self._send(payload)
        return self._recv_packet()

    # --- execution control ----------------------------------------------
    def halt(self):
        if not self.running:
            return
        self.sock.sendall(b'\x03')
        reply = self._recv_packet()  # stop reply, e.g. T05 / S05
        self.running = False
        return reply

    def resume(self):
        if self.running:
            return
        self._send('c')
        self.running = True

    # --- memory -----------------------------------------------------------
    def read(self, addr: int, size: int) -> bytes:
        out = b''
        while size:
            n = min(size, CHUNK)
            reply = self.command(f'm{addr:x},{n:x}')
            if reply.startswith('E'):
                raise IOError(f'read {addr:#x} failed: {reply}')
            out += bytes.fromhex(reply)
            addr += n
            size -= n
        return out

    def write(self, addr: int, data: bytes):
        for i in range(0, len(data), CHUNK):
            chunk = data[i:i + CHUNK]
            reply = self.command(f'M{addr + i:x},{len(chunk):x}:{chunk.hex()}')
            if reply != 'OK':
                raise IOError(f'write {addr + i:#x} failed: {reply}')

    def monitor(self, text: str) -> str:
        """Runs a `monitor` command and returns the final reply ('OK' on success).

        Stock melonDS only knows `reset`; the patched build (tools/build_melonds.sh in the
        NDS folder) adds `savestate PATH`, `loadstate PATH` and `fps N` (0 = unlimited).
        It replies once the command is done, and stays halted if it was halted."""
        reply = self.command('qRcmd,' + text.encode().hex())
        while reply.startswith('O') and reply != 'OK':  # console output packets
            reply = self._recv_packet()
        return reply

    def close(self):
        try:
            self.resume()
        finally:
            self.sock.close()


if __name__ == '__main__':
    import sys
    link = GdbLink(int(sys.argv[1]))
    t = time.time()
    header = link.read(0x027FFE00, 0x10)
    print('game title in RAM header:', header[:12], 'code', header[12:16])
    link.resume()
    time.sleep(0.5)
    link.halt()
    print('second read ok:', link.read(0x027FFE00, 4), f'{(time.time() - t) * 1000:.0f} ms')
    link.close()
