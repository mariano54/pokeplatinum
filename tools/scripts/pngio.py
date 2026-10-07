"""Minimal, dependency-free PNG reader/writer for the NSBTX/NSBMD tools.

Writes 8-bit indexed (colour type 3) and 8-bit RGBA (colour type 6) images.
Reads non-interlaced greyscale/RGB/indexed/grey+alpha/RGBA images at bit depths
1-8 (16-bit samples are reduced to 8 bits).  Output is deterministic (filter 0,
zlib level 9) so re-running an unpack produces identical files.
"""

import struct
import zlib

PNG_SIG = b"\x89PNG\r\n\x1a\n"


class Image:
    """A decoded PNG.

    mode == "P":    pixels are palette indices; palette is a list of (r, g, b, a).
    mode == "RGBA": pixels are (r, g, b, a) tuples; palette is None.
    pixels is a flat row-major list of length width * height.
    """

    def __init__(self, width, height, mode, pixels, palette=None):
        self.width = width
        self.height = height
        self.mode = mode
        self.pixels = pixels
        self.palette = palette


def _chunk(kind, data):
    out = struct.pack(">I", len(data)) + kind + data
    return out + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


def write_indexed(path, width, height, indices, palette):
    """indices: flat list of ints (< len(palette)); palette: list of (r,g,b) or (r,g,b,a)."""
    if not 1 <= len(palette) <= 256:
        raise ValueError(f"{path}: palette must have 1..256 entries, has {len(palette)}")
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw += bytes(indices[y * width:(y + 1) * width])
    plte = bytearray()
    alphas = []
    for c in palette:
        plte += bytes(c[:3])
        alphas.append(c[3] if len(c) > 3 else 255)
    while alphas and alphas[-1] == 255:
        alphas.pop()
    out = PNG_SIG
    out += _chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0))
    out += _chunk(b"PLTE", bytes(plte))
    if alphas:
        out += _chunk(b"tRNS", bytes(alphas))
    out += _chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    out += _chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(out)


def write_rgba(path, width, height, pixels):
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for p in pixels[y * width:(y + 1) * width]:
            raw += bytes(p)
    out = PNG_SIG
    out += _chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    out += _chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    out += _chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(out)


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def read(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != PNG_SIG:
        raise ValueError(f"{path}: not a PNG file")
    pos = 8
    idat = bytearray()
    plte = None
    trns = None
    ihdr = None
    while pos < len(data):
        (length,) = struct.unpack_from(">I", data, pos)
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", body)
        elif kind == b"PLTE":
            plte = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif kind == b"tRNS":
            trns = body
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    width, height, depth, ctype, _, _, interlace = ihdr
    if interlace:
        raise ValueError(f"{path}: interlaced PNGs are not supported")
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    bits_pp = depth * channels
    bpp = max(1, bits_pp // 8)
    stride = (width * bits_pp + 7) // 8
    raw = zlib.decompress(bytes(idat))
    rows = []
    prev = bytearray(stride)
    p = 0
    for _ in range(height):
        ftype = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        if ftype == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif ftype == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ftype == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif ftype == 4:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                upleft = prev[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + _paeth(left, prev[i], upleft)) & 0xFF
        elif ftype != 0:
            raise ValueError(f"{path}: bad PNG filter {ftype}")
        rows.append(line)
        prev = line

    def samples(line):
        if depth == 8:
            return list(line)
        if depth == 16:
            return [line[i] for i in range(0, len(line), 2)]
        out = []
        mask = (1 << depth) - 1
        for byte in line:
            for shift in range(8 - depth, -1, -depth):
                out.append((byte >> shift) & mask)
        return out

    if ctype == 3:
        pixels = []
        for line in rows:
            pixels += samples(line)[:width]
        palette = []
        for i, c in enumerate(plte or []):
            a = trns[i] if trns is not None and i < len(trns) else 255
            palette.append((c[0], c[1], c[2], a))
        return Image(width, height, "P", pixels, palette)

    scale = 255 // ((1 << min(depth, 8)) - 1)
    pixels = []
    for line in rows:
        s = [v * scale for v in samples(line)]
        for x in range(width):
            px = s[x * channels:(x + 1) * channels]
            if ctype == 0:
                pixels.append((px[0], px[0], px[0], 255))
            elif ctype == 4:
                pixels.append((px[0], px[0], px[0], px[1]))
            elif ctype == 2:
                pixels.append((px[0], px[1], px[2], 255))
            else:
                pixels.append(tuple(px))
    return Image(width, height, "RGBA", pixels)
