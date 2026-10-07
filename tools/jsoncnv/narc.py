"""Writes NARC archives with the same layout as the original game files (no file names)."""
import struct


def pack_narc(members: list[bytes]) -> bytes:
    offsets, start = [], 0
    for member in members:
        if len(member) % 4:
            raise ValueError('NARC members must be 4-byte aligned')
        offsets.append((start, start + len(member)))
        start += len(member)
    btaf = b'BTAF' + struct.pack('<IHH', 12 + 8 * len(members), len(members), 0) + b''.join(struct.pack('<II', *o) for o in offsets)
    btnf = b'BTNF' + struct.pack('<IIHH', 16, 4, 0, 1)
    gmif = b'GMIF' + struct.pack('<I', 8 + start) + b''.join(members)
    size = 16 + len(btaf) + len(btnf) + len(gmif)
    return b'NARC' + struct.pack('<HHIHH', 0xFFFE, 0x0100, size, 16, 3) + btaf + btnf + gmif


def unpack_narc(blob: bytes) -> list[bytes]:
    btaf_size, count = struct.unpack_from('<IH', blob, 0x14)
    entries = [struct.unpack_from('<2I', blob, 0x1C + 8 * i) for i in range(count)]
    offset = 0x10 + btaf_size
    offset += struct.unpack_from('<I', blob, offset + 4)[0]
    data = blob[offset + 8:]
    return [data[start:end] for start, end in entries]
