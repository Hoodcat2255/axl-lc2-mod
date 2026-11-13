"""Render Mega Man X6 (Legacy Collection 2, PC) stage background layers to PNG.

Formats (all inside nativeDX10/X6/romPC/stNN.arc, zlib entries of ARC v7):
  X6\\stage\\map\\stNN   (0x11320e86) 'OMP\\0' u32 1, u32 n, u16[n]   = PS1 stage DAT type 0 as is: screens of
                       16x16 cells (256 px square), u16 per cell: bits 0-13 cel index (0 = empty),
                       0x4000 semi-transparent, 0x8000 front priority list
  X6\\stage\\cel\\stNN   (0x6bc1bac7) 'OCL\\0' u32 1, u32 n, u8[4][n]   = PS1 type 1 as is: b0 collision,
                       b1 CLUT (CLUT row 0x1E6 + b1>>4 [+16 when b3&0x40], x = (b1&15)*16),
                       b2 tile in page (u = (b2&15)*16, v = b2&0xF0), b3 page (&0x3F; 0xFF = no graphic)
  X6\\stage\\dds\\stNN   TEX 2048x2048 8bpp, only 2048x512 used: 16 pages of 256x256 (page p at x=(p%8)*256,
  ..\\stNN_chr256       y=(p//8)*256). Pages 0-7 hold 4-bit texels (0-15), pages 8-15 8-bit texels.
                       stNN = PS1 VRAM type 0x10000, stNN_chr256 = PS1 type 0x16 (second VRAM set).
  X6\\stage\\col\\colNN_0x / _0z  'COL\\0' u32 4, u32 ncolors(8192), PS1 16-bit colours = VRAM rows 480-511,
                       x 0-255 (512 CLUTs of 16). Colour index = (6 + (b1>>4) [+16]) * 256 + (b1&15)*16 + texel.
                       _0x / _0z: X / Zero variants (guess from the names, as kaiwa st_NNx / st_NNz).
  Screen layout: not in the arc. u8[3][h][w] screen numbers per layer (0 = empty), from the PS1 overlay
                       (SLUS table 0x800762E0 [stage*2+sub] -> pointer, dims 0x80076398 [stage*2+sub] -> w, h;
                       stage BIN = SLUS u16 table 0x8006DB78, loaded at 0x800E9860). The same bytes are in the
                       PC executable (st03: RXC2_unwrapped.exe file offset 0x2D99CB8).

usage:
  render_stage.py st03.arc [--tex dds|chr256] [--col 0x|0z] [--out DIR]   # layers 0..2 + screen sheet
  render_stage.py st03.arc --sheet-only
"""
import argparse
import os
import struct
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'mods', 'axl', 'tools'))
import lc2_fmt as F  # noqa: E402
import axl_config as C  # noqa: E402

H_MAP, H_CEL, H_TEX, H_COL = 0x11320e86, 0x6bc1bac7, 0x241f5deb, 0x619cf7e7
SLUS_BASE = 0x80010000 - 0x800
LAYOUT_PTRS, LAYOUT_DIMS, STAGE_BIN = 0x800762E0, 0x80076398, 0x8006DB78
OVL_BASE = 0x800E9860          # SLUS word 0x80010000 (stage overlay load address)
EXE = os.path.join(ROOT, 're', 'lc2', 'bin', 'RXC2_unwrapped.exe')


class Stage:
    def __init__(self, arc_name, tex='dds', col='0x'):
        self.name = os.path.basename(arc_name)[:-4].replace('_eng', '')
        a = F.Arc(F.orig_path(os.path.basename(arc_name)))
        ents = {(e['name'], e['hash']): e for e in a.entries}
        n = self.name

        def get(name, h):
            return a.get(name, h) if (name, h) in ents else None
        m = get('X6\\stage\\map\\' + n, H_MAP)
        cnt = struct.unpack_from('<I', m, 8)[0]
        self.screens = np.frombuffer(m, '<u2', cnt, 12).reshape(-1, 16, 16)
        c = get('X6\\stage\\cel\\' + n, H_CEL)
        cnt = struct.unpack_from('<I', c, 8)[0]
        self.cel = np.frombuffer(c, 'u1', 4 * cnt, 12).reshape(-1, 4).astype(int)
        tname = 'X6\\stage\\dds\\' + n + ('' if tex == 'dds' else '_chr256')
        t = get(tname, H_TEX)
        self.tex = np.frombuffer(t, 'u1', 2048 * 2048, 20).reshape(2048, 2048)
        # the col file sits in the non-_eng arc only
        cname = None
        for (en, h) in ents:
            if h == H_COL and en.startswith('X6\\stage\\col\\') and en.endswith(col[-1]) and '_' in en:
                cname = en if cname is None or en < cname else cname
        if cname is None:   # *_eng.arc: take the colours from the Japanese-region arc
            b = F.Arc(F.orig_path(n + '.arc'))
            cname = sorted(e['name'] for e in b.entries if e['hash'] == H_COL and e['name'].startswith(
                'X6\\stage\\col\\') and e['name'].endswith(col[-1]))[0]
            cb = b.get(cname, H_COL)
        else:
            cb = a.get(cname, H_COL)
        self.col_name = cname
        cnt = struct.unpack_from('<I', cb, 8)[0]
        self.col = np.frombuffer(cb, '<u2', cnt, 12)
        w = self.col.astype(int)
        self.rgba = np.stack([(w & 31) * 255 // 31, (w >> 5 & 31) * 255 // 31, (w >> 10 & 31) * 255 // 31,
                              np.where(w == 0, 0, 255)], 1).astype(np.uint8)

    # --- one 16x16 cell -> (rgba 16x16x4, colour indices 16x16) or None
    def clut_base(self, ci):
        b0, b1, b2, b3 = self.cel[ci]
        return (6 + (b1 >> 4) + (16 if b3 & 0x40 else 0)) * 256 + (b1 & 15) * 16

    def cell(self, ci):
        b0, b1, b2, b3 = self.cel[ci]
        if b3 == 0xFF:
            return None
        p = b3 & 0x3F
        x, y = (p % 8) * 256 + (b2 & 15) * 16, (p // 8) * 256 + (b2 & 0xF0)
        texel = self.tex[y:y + 16, x:x + 16].astype(int)
        idx = self.clut_base(ci) + texel
        return self.rgba[idx % len(self.rgba)], idx

    def screen(self, s):
        img = np.zeros((256, 256, 4), np.uint8)
        for ty in range(16):
            for tx in range(16):
                v = int(self.screens[s, ty, tx]) & 0x3FFF
                if v:
                    c = self.cell(v)
                    if c is not None:
                        img[ty * 16:ty * 16 + 16, tx * 16:tx * 16 + 16] = c[0]
        return img

    def sheet(self, cols=16):
        ns = len(self.screens)
        img = np.zeros(((ns + cols - 1) // cols * 264, cols * 264, 4), np.uint8)
        for s in range(ns):
            oy, ox = s // cols * 264, s % cols * 264
            img[oy:oy + 256, ox:ox + 256] = self.screen(s)
        return img

    def render_layer(self, layout):
        h, w = layout.shape
        img = np.zeros((h * 256, w * 256, 4), np.uint8)
        cache = {}
        for y in range(h):
            for x in range(w):
                s = int(layout[y, x])
                if s:
                    if s not in cache:
                        cache[s] = self.screen(s)
                    img[y * 256:y * 256 + 256, x * 256:x * 256 + 256] = cache[s]
        return img


def stage_id(name):
    """st03 -> (3, 0); st0ca -> (0xC, 0); st0cb -> (0xC, 1); st04a/b -> (4, 0/1)"""
    s = name[2:4]
    sub = {'a': 0, 'b': 1}.get(name[4:5], 0)
    return int(s, 16), sub


def ps1_layout(name, nscreens):
    """u8[3][h][w] from the PS1 executable + stage overlay; None when the lookup does not give a valid layout"""
    slus = open(os.path.join(C.X6_US_FS, 'SLUS_013.95'), 'rb').read()
    rom = open(os.path.join(C.X6_US_FS, 'ROCK_X6.BIN'), 'rb').read()
    st, sub = stage_id(name)
    k = st * 2 + sub
    ptr = struct.unpack_from('<I', slus, LAYOUT_PTRS - SLUS_BASE + 4 * k)[0]
    w, h = slus[LAYOUT_DIMS - SLUS_BASE + 2 * k], slus[LAYOUT_DIMS - SLUS_BASE + 2 * k + 1]
    binno = struct.unpack_from('<H', slus, STAGE_BIN - SLUS_BASE + 2 * k)[0]
    sec, size = struct.unpack_from('<II', rom, 8 * binno)
    ovl = rom[sec * 2048:sec * 2048 + size]
    o = ptr - OVL_BASE
    if not ptr or not w or o < 0 or o + 3 * w * h > len(ovl):
        return None, (st, sub, binno, hex(ptr), w, h)
    lay = np.frombuffer(ovl, 'u1', 3 * w * h, o).reshape(3, h, w)
    if lay.max() >= nscreens:
        return None, (st, sub, binno, hex(ptr), w, h)
    info = dict(stage=st, sub=sub, bin=binno, ptr=hex(ptr), w=w, h=h)
    if os.path.exists(EXE):
        info['pc_exe_offset'] = hex(open(EXE, 'rb').read().find(lay.tobytes()))
    return lay, info


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('arc')
    ap.add_argument('--tex', default='dds', choices=('dds', 'chr256'))
    ap.add_argument('--col', default='0x', choices=('0x', '0z'))
    ap.add_argument('--out', default=os.path.join(HERE, 'out'))
    ap.add_argument('--sheet-only', action='store_true')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    S = Stage(a.arc, a.tex, a.col)
    tag = '%s_%s_%s' % (S.name, a.tex, a.col)
    Image.fromarray(S.sheet()).save(os.path.join(a.out, tag + '_screens.png'))
    print('screens', len(S.screens), 'cels', len(S.cel), 'col', S.col_name)
    if a.sheet_only:
        return
    lay, info = ps1_layout(S.name, len(S.screens))
    print('layout', info)
    if lay is None:
        print('no layout: only the screen sheet was written')
        return
    for i in range(3):
        p = os.path.join(a.out, '%s_layer%d.png' % (tag, i))
        Image.fromarray(S.render_layer(lay[i])).save(p)
        print(p)


if __name__ == '__main__':
    main()
