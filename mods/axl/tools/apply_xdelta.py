#!/usr/bin/env python3
"""Apply an xdelta3 patch (VCDIFF, RFC 3284, no secondary compression) with Python's standard library only.

For players without xdelta3 (Steam Deck: SteamOS is read-only, no package manager; user request 2026-11-04). Shipped
in the Axl mod's xdelta package next to apply_linux.sh, which uses it when xdelta3 is missing. The patches are made
with `xdelta3 -e -S none` (lc2_dist.py --xdelta): plain VCDIFF windows with xdelta's Adler-32 per window.

usage: apply_xdelta.py SOURCE PATCH OUTPUT
"""
import sys
import zlib

VCD_SOURCE, VCD_TARGET, VCD_ADLER32 = 0x01, 0x02, 0x04
NOOP, ADD, RUN, COPY = 0, 1, 2, 3


def code_table():
    """RFC 3284 section 5.6: the default instruction code table, (type1, size1, mode1, type2, size2, mode2) x 256"""
    t = [(RUN, 0, 0, NOOP, 0, 0)]
    t += [(ADD, s, 0, NOOP, 0, 0) for s in [0] + list(range(1, 18))]
    for m in range(9):
        t += [(COPY, s, m, NOOP, 0, 0) for s in [0] + list(range(4, 19))]
    for m in range(6):
        for a in range(1, 5):
            t += [(ADD, a, 0, COPY, c, m) for c in range(4, 7)]
    for m in range(6, 9):
        t += [(ADD, a, 0, COPY, 4, m) for a in range(1, 5)]
    t += [(COPY, 4, m, ADD, 1, 0) for m in range(9)]
    assert len(t) == 256
    return t


TABLE = code_table()


class Reader:
    def __init__(self, b, pos=0):
        self.b, self.p = b, pos

    def byte(self):
        v = self.b[self.p]
        self.p += 1
        return v

    def varint(self):
        v = 0
        while True:
            c = self.byte()
            v = (v << 7) | (c & 0x7F)
            if not c & 0x80:
                return v

    def take(self, n):
        v = self.b[self.p:self.p + n]
        if len(v) != n:
            raise ValueError('patch truncated')
        self.p += n
        return v


def adler32(data):
    return zlib.adler32(data) & 0xFFFFFFFF


def apply(source, patch):
    r = Reader(patch)
    if r.take(4) != b'\xd6\xc3\xc4\x00':
        raise ValueError('not a VCDIFF / xdelta3 patch')
    hdr = r.byte()
    if hdr & 0x01:
        raise ValueError('secondary compression: make the patch with xdelta3 -S none')
    if hdr & 0x02:
        raise ValueError('custom code table: not supported')
    if hdr & 0x04:
        r.take(r.varint())   # (xdelta's application header: the file names)
    out = bytearray()
    while r.p < len(patch):
        win = r.byte()
        seg = b''
        if win & (VCD_SOURCE | VCD_TARGET):
            size, pos = r.varint(), r.varint()
            seg = bytes(source[pos:pos + size]) if win & VCD_SOURCE else bytes(out[pos:pos + size])
            if len(seg) != size:
                raise ValueError('the source is shorter than the patch expects (another file?)')
        r.varint()                      # (delta encoding length)
        tlen = r.varint()
        if r.byte():
            raise ValueError('compressed sections: make the patch with xdelta3 -S none')
        dlen, ilen, alen = r.varint(), r.varint(), r.varint()
        want = int.from_bytes(r.take(4), 'big') if win & VCD_ADLER32 else None
        data, inst, addr = Reader(r.take(dlen)), Reader(r.take(ilen)), Reader(r.take(alen))
        t = bytearray()
        near, ni, same = [0] * 4, 0, [0] * (3 * 256)

        def copy_addr(mode, here):
            nonlocal ni
            if mode == 0:
                a = addr.varint()
            elif mode == 1:
                a = here - addr.varint()
            elif mode < 6:
                a = near[mode - 2] + addr.varint()
            else:
                a = same[(mode - 6) * 256 + addr.byte()]
            near[ni] = a
            ni = (ni + 1) % 4
            same[a % (3 * 256)] = a
            return a

        def do(typ, size, mode):
            if typ == NOOP:
                return
            if size == 0:
                size = inst.varint()
            if typ == ADD:
                t.extend(data.take(size))
            elif typ == RUN:
                t.extend(data.take(1) * size)
            else:
                here = len(seg) + len(t)
                a = copy_addr(mode, here)
                for i in range(size):   # (byte by byte: a copy may overlap what it is writing)
                    j = a + i
                    t.append(seg[j] if j < len(seg) else t[j - len(seg)])

        while inst.p < len(inst.b):
            t1, s1, m1, t2, s2, m2 = TABLE[inst.byte()]
            do(t1, s1, m1)
            do(t2, s2, m2)
        if len(t) != tlen:
            raise ValueError('window length mismatch: wrong source file?')
        if want is not None and adler32(bytes(t)) != want:
            raise ValueError('checksum mismatch: wrong source file?')
        out += t
    return bytes(out)


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    src, pat, dst = sys.argv[1:]
    with open(src, 'rb') as f:
        source = f.read()
    with open(pat, 'rb') as f:
        patch = f.read()
    try:
        out = apply(source, patch)
    except ValueError as e:
        sys.exit('%s: %s' % (pat, e))
    with open(dst, 'wb') as f:
        f.write(out)


if __name__ == '__main__':
    main()
