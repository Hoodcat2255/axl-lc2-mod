"""DAT entry container: sector0 = [count, total, (type,size)*count]; subfiles 2048-aligned.
parse() returns None when the entry is not a container."""
import struct

def parse(b):
    if len(b) < 8:
        return None
    n, total = struct.unpack_from('<II', b, 0)
    if total != len(b) or not 0 < n < 255 or 8 + n * 8 > 2048:
        return None
    subs, off = [], 2048
    for i in range(n):
        t, z = struct.unpack_from('<II', b, 8 + i * 8)
        subs.append((t, off, z))
        off += (z + 2047) // 2048 * 2048
    if off != len(b):
        return None
    return subs


def build(subs):
    """Inverse of parse(): subs = [(type, bytes)] -> container bytes."""
    head = struct.pack('<II', len(subs), 0) + b''.join(struct.pack('<II', t, len(d)) for t, d in subs)
    head = head.ljust(2048, b'\0')
    body = b''.join(d + b'\0' * (-len(d) % 2048) for _, d in subs)
    total = len(head) + len(body)
    return struct.pack('<II', len(subs), total) + head[8:] + body
