"""Dialogue portrait: X's face (ROCK_X6.DAT palette + texture pairs, 164-201) -> Axl's face from the sheet.

A portrait texture is 128x64 texels, 8 bpp (8192 bytes) with a 256-color 15-bit palette (512 bytes); the game draws
it as pieces (the face, eye and mouth pieces over it). The piece layout is measured, not decoded: a test build
(AXL_FACE_TEST=u / v) gives every texel the colour of its own u (or v), the dialogue is recorded
(emu/plans/common/plan_axl_facemap.lua) and each portrait pixel on screen tells which texel it shows
(work/face/map.json: per distinct on-screen layout, the screen pixel -> texel lists).
usage: face.py measure <rec prefix u> <rec prefix v>  (writes work/face/map.json)"""
import json
import os
import struct
import sys
import glob

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, HERE)
import axl_config as C  # noqa: E402
DAT = os.path.join(C.X6_US_FS, 'ROCK_X6.DAT')
REC = os.path.join(ROOT, 'emu', 'out', 'rec')
WORK = os.path.join(HERE, '..', 'work', 'face')
TW, TH = 128, 64
X_FACE = (172, 173)          # armor 5 (normal: Axl); the Ultimate pair is found the same way


def coded(mode, pair):
    """palette: index i -> 15-bit (r = i & 31, g = i >> 5, b = 12 + pair), never 0 (no transparent texel);
    texture: every texel = its u (mode 'u') or its v (mode 'v'). pair = (DAT entry - 164) // 2"""
    pal = struct.pack('<256H', *[(i & 31) | (i >> 5) << 5 | (12 + pair) << 10 for i in range(256)])
    u, v = np.meshgrid(np.arange(TW), np.arange(TH))
    tex = (u if mode == 'u' else v).astype(np.uint8)
    return pal, tex.tobytes()


def piece_coded(pair):
    """palette: code c = y * 16 + x -> (r, g, b) = (2x, 2y, 31): every piece texel shows its place inside its 16x16
    piece (u 32-111, v 16-31), every other texel is code 0 (blue), so one frame shows where a piece is drawn"""
    pal = struct.pack('<256H', *[(2 * (c & 15)) | (2 * (c >> 4)) << 5 | 31 << 10 for c in range(256)])
    tex = np.zeros((TH, TW), np.uint8)
    for v in range(16, 32):
        for u in range(32, 112):
            tex[v, u] = (v - 16) * 16 + (u - 32) % 16
    return pal, tex.tobytes()


def load_shot(p):
    raw = open(p, 'rb').read()
    nl = raw.index(b'\n')
    w, h, _ = map(int, raw[:nl].split())
    return np.frombuffer(raw[nl + 1:nl + 1 + w * h * 2], '<u2').reshape(h, w).astype(np.int32)


def decode(a):
    """15-bit screen -> (texel value, pair) where the pixel has a code colour (b >= 12), else -1"""
    b = a >> 10 & 31
    code = b >= 12
    return np.where(code, (a & 31) | (a >> 5 & 7) << 5, -1), np.where(code, b - 12, -1)


def measure(pu, pv):
    def shots(prefix):   # <prefix>_<take>_<n>.shot, one frame per take
        return sorted(glob.glob(os.path.join(REC, prefix + '_*.shot')),
                      key=lambda p: [int(x) for x in os.path.basename(p)[len(prefix) + 1:-5].split('_')])
    us, vs = shots(pu), shots(pv)
    layouts = {}
    for fu, fv in zip(us, vs):
        (u, pu_), (v, pv_) = decode(load_shot(fu)), decode(load_shot(fv))
        m = (u >= 0) & (v >= 0) & (pu_ == pv_)
        # other blue screen pixels look coded too: keep each pair's pixels inside the box of the pixels that differ
        # between the two runs (only the coded textures change)
        diff = m & (u != v)
        keep = np.zeros_like(m)
        for p in set(pu_[diff].tolist()):
            ys, xs = np.nonzero(diff & (pu_ == p))
            box = np.zeros_like(m)
            box[ys.min():ys.max() + 1, xs.min():xs.max() + 1] = True
            keep |= m & box & (pu_ == p)
        m = keep
        if not m.any():
            continue
        ys, xs = np.nonzero(m)
        key = tuple(sorted(zip(ys.tolist(), xs.tolist(), u[m].tolist(), v[m].tolist(), pu_[m].tolist())))
        layouts.setdefault(key, os.path.basename(fu))
    os.makedirs(WORK, exist_ok=True)
    out = [{'shot': s, 'px': [list(p) for p in k]} for k, s in layouts.items()]
    json.dump(out, open(os.path.join(WORK, 'map.json'), 'w'))
    print(len(us), 'shots,', len(out), 'distinct layouts')
    for o in out:
        p = np.array(o['px'])
        print(o['shot'], 'pairs', sorted(set(p[:, 4].tolist())), 'pixels', len(p), 'screen x %d-%d y %d-%d' % (p[:, 1].min(), p[:, 1].max(), p[:, 0].min(), p[:, 0].max()),
              'texels u %d-%d v %d-%d' % (p[:, 2].min(), p[:, 2].max(), p[:, 3].min(), p[:, 3].max()))


# Axl's portraits on the sheet: face 78x64 at (x, FACE_Y), its 5 pieces (eye half / closed, mouth 3) 16x16 in a row at
# (x - 1, PIECE_Y); on the face the eye pieces belong at (24, 28), the mouth pieces at (19, 44)
SHEET = os.path.join(HERE, '..', 'work', 'sheet', 'axl_sheet.png')
FACE_X, FACE_Y, PIECE_Y = (639, 722, 805), 4678, 4746
SHEET_BG = (27, 89, 153)   # the sheet's background colour
PIECE_AT = ((24, 28), (24, 28), (19, 44), (19, 44), (19, 44))
# X's pieces as measured: X piece k (texels u 32 + 16k, v 16) shows at portrait (18, 28) (eyes, k 0-1) or (16, 48)
# (mouth, k 2-4); shifting Axl's face 3 px left puts all of his eye / mouth changes inside those rects
SHIFT = -3
# LC2 (PC) draws the same texture pieces elsewhere, measured with piece_coded (2026-10-26, work/lc2/test/fp_*): eye
# pieces at portrait (24, 24), mouth pieces at (21, 44). Axl's changes fit those rects with his face 4 px up and 2 px
# right: his 78 columns end at the portrait's edge (no made-up hair columns: their seam showed, user report 2026-10-26),
# the 2 new left columns repeat his first (background / shoulder)
PC_PIECES = ((24, 24), (24, 24), (21, 44), (21, 44), (21, 44))
PC_SHIFT = (2, -4)
# which X portrait (texture entry, palette entry, measured map) gets which Axl face: armor 5 normal, 4 Ultimate (White)
PORTRAITS = ((173, 174, 'map5.json', 0), (167, 168, 'map4.json', 2))


def hair_slope(f, n):
    """dy with f[y, c + n] ~ f[y - dy, c] at the face's right edge (the rows a stroke drops over n columns)"""
    f = f.astype(int)
    best = None
    for dy in range(-2 * n, 2 * n + 1):
        a = f[max(0, dy):64 + min(0, dy), 78 - n:]
        b = f[max(0, -dy):64 - max(0, dy), 78 - 2 * n:78 - n]
        e = np.abs(a - b).mean()
        if best is None or e < best[0]:
            best = (e, dy)
    return best[1]


def axl_images(i, shift=None):
    """the 80x64 portrait in each state: base, then with piece k; RGB arrays. shift = (dx, dy) (PC; else (SHIFT, 0))"""
    sx, sy = shift or (SHIFT, 0)
    from PIL import Image
    im = np.array(Image.open(SHEET).convert('RGB'))
    fx = FACE_X[i]
    face = im[FACE_Y:FACE_Y + 64, fx:fx + 78]
    pieces = [im[PIECE_Y:PIECE_Y + 16, fx - 1 + 16 * k:fx - 1 + 16 * k + 16] for k in range(5)]

    def portrait(f):
        if sy < 0:   # up: the bottom rows repeat the last one (the shoulders)
            f = np.concatenate([f[-sy:], np.repeat(f[-1:], -sy, axis=0)])
        out = np.zeros((64, 80, 3), np.uint8)
        if sx > 0:
            out[:, sx:sx + 78] = f
            out[:, :sx] = f[:, :1]
            return out
        out[:, :78 + sx] = f[:, -sx:]
        # right edge (hair): continue the last columns along the hair strokes (measured: 5 columns right = 5 rows
        # down on the sheet) instead of mirroring them, which bent the hair tips (user report 2026-10-26)
        # the slope is measured per face (hair_slope): the strokes rise to the right (2026-10-27: the fixed +5 drew
        # them falling, the seam the user saw at the back of White Axl's head); rows past the top / bottom repeat the
        # nearest one
        n = 2 - sx
        dy = hair_slope(f, n)
        rows = np.clip(np.arange(64) - dy, 0, 63)
        out[:, 78 + sx:] = f[rows][:, 78 - n:]
        return out
    states = [portrait(face)]
    for k, (x, y) in enumerate(PIECE_AT):
        f = face.copy()
        keep = (pieces[k] != SHEET_BG).any(-1)   # the sheet's background around the piece: the face stays (chin)
        f[y:y + 16, x:x + 16][keep] = pieces[k][keep]
        states.append(portrait(f))
    return states


def build(i, map_name, pc=False):
    """(palette 512 B, texture 8192 B) of Axl's face i laid out as X's portrait measured in map_name (PS1); pc: the
    pieces where LC2 draws them (PC_PIECES; the base layout is the PS1 one)"""
    layouts = json.load(open(os.path.join(WORK, map_name)))
    states = axl_images(i, PC_SHIFT if pc else None)
    # layout -> state: the base has no piece texel (v 16-31, u 32-111); else the piece number
    def state_of(px):
        ks = {(u - 32) // 16 for y, x, u, v, _ in px if 16 <= v < 32 and 32 <= u < 112}
        assert len(ks) <= 1, ks
        return 0 if not ks else 1 + ks.pop()
    if pc:   # screen pixel lists as the PS1 maps (portrait (x - 200, y - 40))
        pair = layouts[0]['px'][0][4]
        layouts = [o for o in layouts if state_of(o['px']) == 0] + [
            {'px': [(py + v + 40, px + u + 200, 32 + 16 * k + u, 16 + v, pair) for v in range(16) for u in range(16)]}
            for k, (px, py) in enumerate(PC_PIECES)]
    tex_rgb = {}
    for o in sorted(layouts, key=lambda o: state_of(o['px']) != 0):   # base first, pieces override their texels
        st = state_of(o['px'])
        img = states[st]
        for y, x, u, v, _ in o['px']:
            if st and not (16 <= v < 32 and 32 <= u < 112):
                continue
            c = tuple(int(c) >> 3 for c in img[y - 40, x - 200])
            old = tex_rgb.get((u, v))
            assert st or old is None or old == c, ('base texel shown twice with two colours', u, v)
            tex_rgb[(u, v)] = c
    colors = sorted(set(tex_rgb.values()))
    assert len(colors) <= 255, len(colors)
    idx = {c: n + 1 for n, c in enumerate(colors)}
    pal = [0] + [r | g << 5 | b << 10 or 0x8000 for r, g, b in colors]   # black: STP bit (0 is transparent)
    pal += [0] * (256 - len(pal))
    tex = np.zeros((TH, TW), np.uint8)
    for (u, v), c in tex_rgb.items():
        tex[v, u] = idx[c]
    return struct.pack('<256H', *pal), tex.tobytes()


def entries():
    """DAT entries for the build: {entry: bytes}"""
    out = {}
    for t, p, m, i in PORTRAITS:
        out[p], out[t] = build(i, m)
    return out


if __name__ == '__main__':
    if sys.argv[1] == 'measure':
        measure(sys.argv[2], sys.argv[3])
    elif sys.argv[1] == 'preview':
        # what the measured layouts show with the built textures (work/face/preview.png)
        from PIL import Image
        rows = []
        for t, p, m, i in PORTRAITS:
            pal, tex = build(i, m)
            pal = np.frombuffer(pal, '<u2'); tex = np.frombuffer(tex, np.uint8).reshape(TH, TW)
            rgb = np.stack([(pal & 31) << 3, (pal >> 5 & 31) << 3, (pal >> 10 & 31) << 3], -1).astype(np.uint8)
            row = []
            for o in sorted(json.load(open(os.path.join(WORK, m))), key=lambda o: o['shot']):
                img = np.zeros((64, 80, 3), np.uint8)
                for y, x, u, v, _ in o['px']:
                    img[y - 40, x - 200] = rgb[tex[v, u]]
                row.append(img)
            rows.append(np.hstack(row))
        Image.fromarray(np.vstack(rows)).resize((480 * 4, 128 * 4), Image.NEAREST).save(os.path.join(WORK, 'preview.png'))
        print('wrote', os.path.join(WORK, 'preview.png'))
