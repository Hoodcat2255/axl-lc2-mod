"""Render player sprite frames from type-2 (tiles) + type-3 (composition) + type-9 (palette) subfiles.

usage: spritesheet.py <resdir> <type2 file> <type3 file> <type9 file> out.png [first last]

type 2 section 0: u32 per frame, low 20 bits = offset of LZ data (from section start), high 12 = tile count.
LZ (FUN_80019748): 16-bit words; control word, 16 flags MSB first; 0 = literal word,
1 = ref word w: if w & 0xf800 == 0 → dist = w, len = next word, else len = w >> 11, dist = w & 0x7ff;
len == dist == 0 ends, dist == 0 emits len zero words, else copies len words from dst - dist.
The output is a 4bpp image min(n,16)*16 px wide, 16 px per row of 16 tiles.
type 3 section 0: (u16 parts, u16 offset/4) per frame; part = (attr, tile lo, x, y), tile = lo | (attr & 3) << 8,
attr 0x40 = h-flip, (attr & 0xc) >> 2 = palette offset.
"""
import struct
import sys

import numpy as np
from PIL import Image


def sections(b):
    first = struct.unpack_from('<I', b, 0)[0]
    return [struct.unpack_from('<I', b, 4 * i)[0] for i in range(first // 4)]


def lz(data, off):
    out, p = [], off
    while True:
        ctl = struct.unpack_from('<H', data, p)[0]; p += 2
        for bit in range(15, -1, -1):
            w = struct.unpack_from('<H', data, p)[0]; p += 2
            if not ctl >> bit & 1:
                out.append(w)
                continue
            if w & 0xf800 == 0:
                dist, ln = w, struct.unpack_from('<H', data, p)[0]; p += 2
            else:
                ln, dist = w >> 11, w & 0x7ff
            if ln == 0 and dist == 0:
                return out
            if dist == 0:
                out += [0] * ln
            else:
                for _ in range(ln):
                    out.append(out[-dist])


def tiles_image(t2, sec0, frame):
    ent = struct.unpack_from('<I', t2, sec0 + 4 * frame)[0]
    n, off = ent >> 20, ent & 0xfffff
    if n == 0:
        return None, 0
    words = lz(t2, sec0 + off)
    raw = np.array(words, '<u2').view(np.uint8)
    nib = np.empty(len(raw) * 2, np.uint8)
    nib[0::2] = raw & 15
    nib[1::2] = raw >> 4
    # layout of the upload buffer (FUN_80017b04): full rows of 16 tiles as 256-px lines, then the remaining
    # tiles packed as one (rem * 16)-px wide row; returned as a grid 16 tiles wide
    w = min(n, 16) * 16
    h = 16 * ((n + 15) // 16)
    img = np.zeros((h, w), np.uint8)
    full, rem = n // 16, n % 16
    p = 0
    for r in range(full):
        img[r * 16:(r + 1) * 16, :] = nib[p:p + 16 * 256].reshape(16, 256)[:, :w]
        p += 16 * 256
    if rem:
        rw = rem * 16
        blk = nib[p:p + 16 * rw]
        blk = np.pad(blk, (0, 16 * rw - len(blk)))
        img[full * 16:full * 16 + 16, :rw] = blk.reshape(16, rw)
    return img, n


def palette(t9, idx):
    cols = struct.unpack_from('<16H', t9, idx * 32)
    pal = []
    for c in cols:
        pal.append(((c & 31) << 3, (c >> 5 & 31) << 3, (c >> 10 & 31) << 3, 0 if c == 0 else 255))
    return pal


def render(t2, t3, t9, frame, size=96, layers=((0, 0), (5, 1))):
    """layers = (type-3 section, palette base): section 0 is the body, section 5 the Gaea armor layer
    (drawn by X5 FUN_800287d4 / FUN_80028110 with obj+0x155 = 5)."""
    s2 = sections(t2)[0]
    img, n = tiles_image(t2, s2, frame)
    canvas = Image.new('RGBA', (size, size), (40, 40, 48, 255))
    if img is None:
        return canvas
    for sec, pbase in layers:
        draw_layer(canvas, img, n, t3, t9, sections(t3)[sec], frame, pbase, size)
    return canvas


def draw_layer(canvas, img, n, t3, t9, s3, frame, pbase, size):
    cnt, off4 = struct.unpack_from('<HH', t3, s3 + 4 * frame)
    for i in range(cnt):
        attr, lo, x, y = struct.unpack_from('<BBbb', t3, s3 + off4 * 4 + 4 * i)
        t = lo | (attr & 3) << 8
        if t >= n:
            continue
        tile = img[(t // 16) * 16:(t // 16) * 16 + 16, (t % 16) * 16:(t % 16) * 16 + 16]
        pal = palette(t9, pbase + ((attr & 0xc) >> 2))
        rgba = np.array([pal[v] for v in tile.ravel()], np.uint8).reshape(16, 16, 4)
        tim = Image.fromarray(rgba, 'RGBA')
        if attr & 0x40:
            tim = tim.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
        canvas.alpha_composite(tim, (size // 2 + x, size * 3 // 4 + y))


def main():
    t2, t3, t9 = (open(p, 'rb').read() for p in sys.argv[1:4])
    out = sys.argv[4]
    first, last = (int(sys.argv[5]), int(sys.argv[6])) if len(sys.argv) > 6 else (0, 255)
    frames = range(first, last + 1)
    cols = 16
    sheet = Image.new('RGBA', (cols * 96, ((len(frames) + cols - 1) // cols) * 96), (0, 0, 0, 255))
    for k, f in enumerate(frames):
        try:
            sheet.paste(render(t2, t3, t9, f), ((k % cols) * 96, (k // cols) * 96))
        except (struct.error, IndexError):
            pass
    sheet.save(out)


if __name__ == '__main__':
    main()
