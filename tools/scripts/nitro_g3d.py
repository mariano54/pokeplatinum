"""Shared helpers for NitroSystem G3D binary resources (NSBMD/NSBTX/...).

- Resource dictionaries (NNSG3dResDict): name -> fixed-size entry, plus the
  Patricia tree the runtime uses for lookups.  The tree is regenerated from the
  names with the same algorithm as tools/nitrobtx (which matches Nintendo's
  converter), so dictionaries can be rebuilt instead of copied.
- Container files: the 'BMD0'/'BTX0'/... header and its block offsets.
"""

import struct

NAME_LEN = 16


def u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def s32(b, o):
    return struct.unpack_from("<i", b, o)[0]


def align(n, a):
    return (n + a - 1) // a * a


def name_to_str(raw):
    """16 raw bytes -> printable name.  Bytes after the first NUL must be zero
    for the name to round-trip; callers check that with name_is_clean()."""
    return raw.split(b"\0", 1)[0].decode("latin1")


def name_is_clean(raw):
    return str_to_name(name_to_str(raw)) == raw


def str_to_name(s):
    b = s.encode("latin1")
    if len(b) > NAME_LEN:
        raise ValueError(f"resource name too long (max 16 bytes): {s!r}")
    return b.ljust(NAME_LEN, b"\0")


# ---------------------------------------------------------------------------
# Patricia tree (port of tools/nitrobtx/src/ns/resource_tree.c)

_ZERO = 0xFF  # sentinel entry index meaning "the all-zero name"


def _bit(name, i):
    return (name[i // 8] >> (i % 8)) & 1


def _diff_bit(names, e1, e2, start, stop):
    if e1 == _ZERO:
        if e2 == _ZERO:
            return -1
        e1, e2 = e2, e1
    for i in range(start, stop - 1, -1):
        b1 = _bit(names[e1], i)
        b2 = 0 if e2 == _ZERO else _bit(names[e2], i)
        if b1 != b2:
            return i
    return -1


class _Node:
    __slots__ = ("leaf", "bit", "entry", "left", "right", "index")

    def __init__(self, leaf, entry, bit=0, left=None, right=None):
        self.leaf = leaf
        self.entry = entry
        self.bit = bit
        self.left = left
        self.right = right
        self.index = 0


def make_tree(names):
    """names: list of 16-byte names.  Returns list of (refBit, left, right, entry)."""
    n = len(names)
    root = [_Node(True, _ZERO)]  # boxed so we can replace it

    for e in range(n):
        holder, attr = root, 0
        cur_bit = 127
        while True:
            node = holder[attr] if isinstance(holder, list) else getattr(holder, attr)
            if node.leaf:
                d = _diff_bit(names, e, node.entry, cur_bit, 0)
                if d < 0:
                    raise ValueError(f"duplicate resource name {names[e]!r}")
            else:
                d = _diff_bit(names, e, node.entry, cur_bit, node.bit + 1)
                if d < 0:
                    cur_bit = node.bit
                    holder, attr = node, ("right" if _bit(names[e], node.bit) else "left")
                    continue
            leaf = _Node(True, e)
            if _bit(names[e], d):
                new = _Node(False, e, d, node, leaf)
            else:
                new = _Node(False, e, d, leaf, node)
            if isinstance(holder, list):
                holder[attr] = new
            else:
                setattr(holder, attr, new)
            break

    out = [(127, 1 if n else 0, 0, 0)]
    if n == 0:
        return out
    order = []
    entry_to_index = {}
    stack = [root[0]]
    while stack:
        node = stack.pop()
        order.append(node)
        node.index = len(order)
        entry_to_index[node.entry] = node.index
        if not node.right.leaf:
            stack.append(node.right)
        if not node.left.leaf:
            stack.append(node.left)

    def ref(child):
        if child.leaf:
            return 0 if child.entry == _ZERO else entry_to_index[child.entry]
        return child.index

    for node in order:
        out.append((node.bit, ref(node.left), ref(node.right), node.entry))
    return out


# ---------------------------------------------------------------------------
# Resource dictionaries


def read_dict(b, off):
    """Parse a dictionary at absolute offset `off`.

    Returns (names, entries, size) where names are 16-byte raw names, entries
    are raw entry bytes and size is the dictionary's byte size.
    """
    count = b[off + 1]
    size = u16(b, off + 2)
    entry_off = off + u16(b, off + 6)
    entry_size = u16(b, entry_off)
    names_off = entry_off + u16(b, entry_off + 2)
    entries = [bytes(b[entry_off + 4 + i * entry_size:entry_off + 4 + (i + 1) * entry_size]) for i in range(count)]
    names = [bytes(b[names_off + i * NAME_LEN:names_off + (i + 1) * NAME_LEN]) for i in range(count)]
    return names, entries, size


def dict_size(count, entry_size):
    return 8 + 4 * (count + 1) + 4 + count * (entry_size + NAME_LEN)


def build_dict(names, entries, entry_size):
    count = len(names)
    assert len(entries) == count
    tree = make_tree(names)
    out = bytearray()
    out += struct.pack("<BBHHH", 0, count, dict_size(count, entry_size), 8, 8 + 4 * (count + 1))
    for node in tree:
        out += struct.pack("<BBBB", *node)
    out += struct.pack("<HH", entry_size, 4 + entry_size * count)
    for e in entries:
        assert len(e) == entry_size
        out += e
    for nm in names:
        out += nm
    return bytes(out)


# ---------------------------------------------------------------------------
# Containers


def read_container(b, magic):
    if b[:4] != magic:
        raise ValueError(f"expected {magic!r} file, got {bytes(b[:4])!r}")
    bom, version, filesize, header_size, nblocks = struct.unpack_from("<HHIHH", b, 4)
    if bom != 0xFEFF or header_size != 16:
        raise ValueError("unexpected container header")
    offsets = [u32(b, 16 + 4 * i) for i in range(nblocks)]
    blocks = []
    for i, o in enumerate(offsets):
        end = offsets[i + 1] if i + 1 < len(offsets) else filesize
        blocks.append(bytes(b[o:end]))
    return version, blocks, filesize


def build_container(magic, version, blocks):
    head = 16 + 4 * len(blocks)
    offsets = []
    pos = head
    for blk in blocks:
        offsets.append(pos)
        pos += len(blk)
    out = bytearray(magic + struct.pack("<HHIHH", 0xFEFF, version, pos, 16, len(blocks)))
    for o in offsets:
        out += struct.pack("<I", o)
    for blk in blocks:
        out += blk
    return bytes(out)


# ---------------------------------------------------------------------------
# Colours


def bgr555_to_rgb(c):
    r, g, b = c & 31, (c >> 5) & 31, (c >> 10) & 31
    return ((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2))


def rgb_to_bgr555(rgb):
    r, g, b = rgb[:3]
    return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)
