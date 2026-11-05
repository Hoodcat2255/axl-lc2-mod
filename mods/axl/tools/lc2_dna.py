"""DNA Change looks for Legacy Collection 2: the 8 bosses' own poses as extra textures / parts sections of pl02*.arc.

Per boss k (1-8, X's weapon numbers; tools/boss_look.py BOSSES and the viewer edits):
  TEX + COF  X6\\chr\\pl\\pl02\\pl02_0NN (NN = 18 + k), registered at the end of the CTEX list (texture index 18 + k)
  PAT        one more parts section (32 + k - 1): frame i = pose i, the boss's own parts (flips, palette layers, list
             order) with the tiles it uses numbered per frame; frame i's tiles are block i of the texture (COF y)
  palettes   the boss's 4 palettes go to the DLL (axl_dna_looks.h): uploaded at the change to the emulated CLUT slots
             16-19 (PS1 0x7810-0x7813, free in every stage) with the game's own upload FUN_023aaef0
No tile limit and no scaling on PC (PS1 cut poses into <= 32-tile slices and shrank Turtloid). Every pose of a boss
stands on X's feet line (the lowest opaque row of X's frame 0), as on PS1; part positions are signed bytes, a taller
pose is stored `lift` px lower and drawn with the player moved up by as much (axl_dna.c).
dna_looks(arcs...) adds them; header() writes mods/axl/work/lc2/gen/axl_dna_looks.h (sequences, indices) for the DLL's data file.
"""
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lc2_fmt as F  # noqa: E402

CTEX = 'X6\\chr\\pl\\pl02\\pl02'
PAL_SLOT = 400         # emulated CLUT slot of the look's palette 0: free in the stages (PS1 used CLUT 0x7810 = slot
                       # 16, which PC's HUD uses: the weapon icon took the boss's colours, 2026-10-26)
PAT_BASE = 32          # first appended parts section


def feet_line():
    """lowest opaque row of X's frame 0 relative to the origin (PS1 build_axl: x_frame_mask)"""
    import build_axl as A
    import spritesheet as ss
    d = F.dat_entry86(F.DAT['us'])
    xm, _, cy0 = A.x_frame_mask(d[2], ss.sections(d[2])[0], d[3], ss.sections(d[3])[0], 0)
    return int(np.nonzero(xm)[0].max()) + cy0


# the looks' sequences: standing, shooting, the attack motion, the dash-button move and the DNA-button second attack
# (move / alt: boss_look.BOSSES, user request 2026-10-27: the moves showed the standing loop), then the boss's own
# walk / jump (rising) / fall / land for what Axl does (user request 2026-10-27; flyers: forward / up / down)
SEQS = ('idle', 'fire', 'atk', 'move', 'alt', 'walk', 'jump', 'fall', 'land', 'cling', 'ceil', 'cjump', 'cshot', 'cdash',
        'leap', 'idle2', 'jump2', 'fall2', 'toss')
# toss: a thrown attack's motion played once by the boss's code (axl_dna.c dna_toss_t; Sheldon's shell, 2026-10-29)
# idle2 / jump2 / fall2: the looks while the second attack is on, when it changes the boss (Sheldon: the shell held as
# the plates - with the plates up no shell in his hands, user report 2026-10-28; empty: the plain ones)
ALTS = ('idle2', 'jump2', 'fall2')
MOTIONS = ('walk', 'jump', 'fall', 'land')
# Wolfang's walls and ceiling (axl_moves.c; user plan 2026-10-27): the wall cling, the hang, the jump up to it, the
# ceiling shots, the ceiling dash, the leap - each played once, the last picture held
EXTRAS = ('cling', 'ceil', 'cjump', 'cshot', 'cdash', 'leap')
SEQ_MAX = 96   # entries of a sequence (Yammark's standing look: 90, 2026-10-29)
HOLD = 255   # dna_seq.at of a motion: the entry its loop goes back to, HOLD = the last entry held


def boss_poses(k, feet_y, dat):
    """-> dict(name, poses [(parts [(attr, tile, x, y)], tiles [16x16 u8], lift)], seqs {idle, fire, atk: [(dur,
    pose)]}, atk_at, pals [4][16])"""
    import boss_look as L
    ed = L.edits()
    cfg = dict(L.config(k, ed))
    for q in SEQS:   # (PC-only sequences: q_pc in place of q - the PS1 build keeps q)
        if q + '_pc' in cfg:
            cfg[q] = cfg[q + '_pc']
    b = L.Boss(dat, cfg)
    seq = [f for q in SEQS for _, f in cfg.get(q, ())]
    used = list(dict.fromkeys(seq))
    # {picture: dx}: a picture drawn moved along x (in the boss's own facing). Not in the parts: their signed-byte x
    # cut Heatnix's firebird tail off (user report 2026-10-28) - the draw moves the player instead (push, axl_dna.c)
    shift = cfg.get('shift', {})
    parts = {f: [(a, lo, x, y) for a, lo, x, y in b.parts(f) if not a & 0x20 and -128 <= x <= 127 - 15] for f in used}
    imgs = {f: b.compose(f) for f in used}
    # (walk / jump / fall / land and the rest keep the boss's own offsets, e.g. Shark's jump pictures reach 13 px lower)
    # the feet (and the body: hurt box, height) from the standing / shooting / attack pictures only: the moves' and
    # second attacks' pictures (Heatnix's firebird, Shark's leap) are attacks - user report 2026-10-27, Heatnix grew
    base = {f for q in cfg.get('feet_from', ('idle', 'fire', 'atk')) for _, f in cfg.get(q, ()) if f < L.TABLE1}
    dy_all = feet_y - L.bottom([imgs[f] for f in used if f in base])
    poses, index = [], {}
    for f in used:
        dy = feet_y - L.bottom([imgs[f]]) if k in L.FEET_PER_POSE else dy_all
        nx, ny = ed['nudge'].get('%d:%d' % (k, f), (0, 0))
        ny += cfg.get('drop', {}).get(f, 0)   # (a pose lower than the others: Shark's prone fin)
        ys = [y + dy + ny for _, _, _, y in parts[f]]
        lift = max(0, -128 - min(ys))
        assert max(ys) + lift <= 127, (cfg['name'], f, 'too tall')
        ids, tiles, ps = {}, [], []
        for a, lo, x, y in parts[f]:
            t = lo | (a & 3) << 8
            if t not in ids:
                ids[t] = len(tiles)
                tiles.append(b.tile(a, lo, f).astype(np.uint8))
            i = ids[t]
            x, y = x + nx, y + dy + ny + lift
            assert -128 <= x <= 127 and -128 <= y <= 127, (cfg['name'], f, x, y)
            ps.append(((a & 0xfc) | i >> 8, i & 0xff, x, y))
        index[f] = len(poses)
        assert -128 <= shift.get(f, 0) <= 127, (cfg['name'], f)
        poses.append((ps, tiles, lift, shift.get(f, 0)))
    seqs = {q: [(d, index[f]) for d, f in cfg.get(q, ())] for q in SEQS}
    loops = {q: cfg.get(q + '_loop', 0 if q == 'walk' else HOLD) for q in MOTIONS}
    loops['move'] = cfg.get('move_loop', 0)
    loops['alt'] = cfg.get('alt_loop', HOLD)   # (the second attack plays once unless it loops)
    body = index[cfg.get('body', cfg['idle'][0][1])]   # the picture the hurt box / height are taken from
    return dict(name=cfg['name'], poses=poses, seqs=seqs, atk_at=cfg.get('atk_at', 0), loops=loops, body=body,
                pals=np.asarray(b.pals, np.uint16))


def texture(poses):
    """(TEX bytes, ys, parts per pose): the poses' tiles once each (shared between poses), 16 per row, every frame
    block at row 0 (Turtloid's per-pose blocks made a 2048 px texture; the FX one that size crashed, 2026-10-26)"""
    ids, tiles, out = {}, [], []
    for ps, ts, *_ in poses:
        remap = []
        for t in ts:
            key = t.tobytes()
            if key not in ids:
                ids[key] = len(tiles)
                tiles.append(t)
            remap.append(ids[key])
        fr = []
        for a, lo, x, y in ps:
            i = remap[lo | (a & 3) << 8]
            fr.append(((a & 0xfc) | i >> 8, i & 0xff, x, y))
        out.append(fr)
    assert len(tiles) <= 1024, len(tiles)
    y = 16 * ((len(tiles) + 15) // 16)
    h = 16
    while h < y:
        h *= 2
    tex = np.full((h, 256), F.TEX_PAD, np.uint8)
    for i, t in enumerate(tiles):
        tex[16 * (i // 16):16 * (i // 16) + 16, 16 * (i % 16):16 * (i % 16) + 16] = t
    head = struct.pack('<4sIIII', b'TEX\0', 0x200080a1, 1 | 256 << 6 | h << 19, 0x00011201, 0x14)
    return head + tex.tobytes(), [0] * len(poses), out


def all_bosses():
    dat = open(F.DAT['us'], 'rb').read()
    feet = feet_line()
    return [boss_poses(k, feet, dat) for k in range(1, 9)]


def dna_looks(arc, pat_name, bosses):
    """add the 8 looks to one pl02 arc; returns [(texture index, PAT section, palette slot)]"""
    cof0 = arc.get(F.TEX_NAME % 0, F.H_COF)
    secs = F.pat_sections(arc.get(pat_name, F.H_PAT))
    assert len(secs) == PAT_BASE, len(secs)
    out = []
    for k, bo in enumerate(bosses, 1):
        tex, ys, frames = texture(bo['poses'])
        ti = F.add_texture(arc, CTEX, F.TEX_NAME % (18 + k), tex, cof0, len(ys), ys)
        gi = F.register_texture(arc, FX_CTEX, F.TEX_NAME % (18 + k))   # (also in the weapon objects' list: afterimages)
        secs.append([[struct.pack('<BBbb', *q) for q in ps] for ps in frames])
        out.append((ti, PAT_BASE + k - 1, PAL_SLOT, gi))
    arc.put(pat_name, F.H_PAT, F.pat(secs))
    return out


FX_TEX = 18 + 9         # pl02_027: the bosses' projectile tiles
SHOT_BASE = 47          # first bullet frame in the projectile parts section (after the bosses' 47)
ICON_BASE = 47 + 32 * 5  # then the 9 DNA menu badges (0 Axl, k boss k)
GIGA_BASE = ICON_BASE + 9   # then the Giga Crush pictures (giga_pictures)
N_FLAMES = 4

# Giga Crush (user request 2026-10-27: all enemies on screen hit, the guns spinning in both hands): the sheet's
# "Giga Crush" effects, 3 rows (angles 45 / 20 / 0 degrees) x 9 cells [y0, x0, y1, x1]: 0 gun, 1 orb, 2-6 the burst
# (big .. tiny), 7 comet, 8 blue ring
GIGA_CELLS = [[[2698, 549, 2716, 565], [2699, 575, 2712, 588], [2688, 603, 2715, 630], [2687, 643, 2716, 672],
               [2700, 681, 2717, 698], [2704, 711, 2717, 724], [2707, 737, 2716, 746], [2693, 757, 2718, 782],
               [2695, 802, 2712, 819]],
              [[2735, 546, 2751, 565], [2735, 575, 2748, 588], [2728, 603, 2751, 635], [2723, 643, 2754, 674],
               [2734, 681, 2749, 700], [2735, 711, 2748, 725], [2737, 737, 2743, 748], [2729, 757, 2746, 788],
               [2728, 802, 2747, 819]],
              [[2767, 546, 2779, 565], [2767, 575, 2781, 589], [2762, 603, 2786, 636], [2758, 643, 2790, 674],
               [2765, 681, 2781, 701], [2766, 711, 2780, 726], [2771, 737, 2775, 748], [2768, 757, 2790, 790],
               [2763, 802, 2783, 820]]]
GIGA_SPIN = 8           # the gun turned 0, 45 .. 315 degrees (counter-clockwise on screen)
GIGA_BURST = (1, 2, 3, 4, 5, 6)   # the burst at each enemy: orb, then the flash shrinking (row 2, flat)
FLAME_BASE = GIGA_BASE + GIGA_SPIN + len(GIGA_BURST)   # then the 4 hover flames (flame_pictures)
COPY_BASE = FLAME_BASE + N_FLAMES   # then the copy shot's pictures (copy_pictures)
# the copy shot (user request 2026-10-27: the sheet's other bullet, growing and shrinking, small bullets turning
# round it in an X, its path drawn): the ring at sheet (528, 3958)-(548, 3978) (its 4 colours after the flames' in
# their palette, flame_pal 6..) redrawn at COPY_RADII, then a small bullet, then the trail's segments (COPY_DIRS
# directions x COPY_FADE steps). The sheet has only the ring: the others are drawn in its colours.
COPY_BOX = (528, 3958, 548, 3978)
COPY_RADII = (8, 9, 10, 11, 12)   # (the sheet's ring: 10)
COPY_DIRS = 4                     # trail segment: 0 level, 1 "\", 2 upright, 3 "/" (data, as drawn facing left)
COPY_FADE = 2   # (3 until 2026-10-27: 4 frames for Wolfang's icicle / wall spike)
COPY_ORB = COPY_BASE + len(COPY_RADII)
COPY_TRAIL = COPY_ORB + 1
N_COPY = len(COPY_RADII) + 1 + COPY_DIRS * COPY_FADE
# Wolfang's floor ice spike (user request 2026-10-27: the dash's spikes; BIN 34 type 0x36 sub 0x10, anim 0x1F): some
# of its pictures (the burst out of the floor, then the spike) after the copy shot's, drawn in the boss's palettes
# (Wolfang's are up while changed: dna_pal_slot)
SPIKE_BASE = COPY_BASE + N_COPY
SPIKE_PICS = (34, 37, 40, 43)   # (the FX frames end at 256: 4 of anim 0x1F's pictures - the burst big, smaller, the spike)
SPIKE_SEQ = ((3, 0), (5, 1), (4, 2), (1, 3))   # (duration, SPIKE_PICS index): anim 0x1F's timing, the spike held
# Wolfang's icicle (BIN 34 sub 0x12) and wall spike (0x11), user decision 2026-10-27: the floor spike's pictures 37 / 43
# turned (no FX frames left for the boss's own): the icicle upside down, the wall spike pointing left (data facing)
TURNED_PICS = (37, 43)
ICICLE_BASE = SPIKE_BASE + len(SPIKE_PICS)
WALL_SPIKE_BASE = ICICLE_BASE + len(TURNED_PICS)


def flame_spots():
    """{body frame: [[x, y, degrees], [x, y, degrees]]} of the PS1 build (work/build/flame_spots.json)"""
    import json
    sp = json.load(open(os.path.join(F.ROOT, 'mods', 'axl', 'work', 'build', 'flame_spots.json')))['spots']
    return {int(f): v for f, v in sp.items()}


def flame_pictures():
    """the hover jet (PS1 build_axl.hover_flames): the sheet's 4 flames (build_axl.FLAME_BOXES), data facing left
    (nozzle top-left, trail down-right), each with its nozzle texel: ([(index image, nx, ny)], palette [16])"""
    import build_axl as BA
    from PIL import Image
    sheet = np.array(Image.open(os.path.join(F.ROOT, 'mods', 'axl', 'work', 'sheet', 'axl_sheet.png')).convert('RGB')).astype(int)
    pal = np.array(BA.shot_pal()[1:])
    out = []
    for x0, y0, x1, y1 in BA.FLAME_BOXES:
        rgb = sheet[y0:y1, x0:x1]
        mask = ~(rgb == np.array(BA.BG)).all(2)
        img = ((((rgb[:, :, None, :] - pal[None, None]) ** 2).sum(3).argmin(2) + 1) * mask)[:, ::-1].astype(np.uint8)
        ys, xs = np.nonzero(img)
        k = np.lexsort((xs, ys))[0]
        out.append((img, int(xs[k]), int(ys[k])))
    p16 = [0] + [(r >> 3) | (g >> 3) << 5 | (b >> 3) << 10 for r, g, b in BA.shot_pal()[1:]]
    return out, p16 + [0] * (16 - len(p16))


def copy_pictures(pal16):
    """the copy shot's pictures (index images, centred) and pal16 with the ring's colours added"""
    from PIL import Image
    sheet = np.array(Image.open(os.path.join(F.ROOT, 'mods', 'axl', 'work', 'sheet', 'axl_sheet.png')).convert('RGB')).astype(int)
    x0, y0, x1, y1 = COPY_BOX
    rgb = sheet[y0:y1, x0:x1]
    mask = np.abs(rgb - np.array((27, 89, 153))).sum(2) >= 12
    cols = sorted({tuple(int(v) for v in c) for c in rgb[mask]}, key=sum)   # dark .. light
    first = max(i for i, v in enumerate(pal16) if v) + 1
    assert first + len(cols) <= 16 and len(cols) == 4, (first, cols)
    pal = list(pal16)
    for i, c in enumerate(cols):
        pal[first + i] = (c[0] >> 3) | (c[1] >> 3) << 5 | (c[2] >> 3) << 10 or 0x8000
    B, D, M, W = range(first, first + 4)   # blue, dark, mid, white (the sheet's ring: W band, M / D edges, B dot)

    def disc(n, f):
        img = np.zeros((n, n), np.uint8)
        c = (n - 1) / 2
        for y in range(n):
            for x in range(n):
                img[y, x] = f(np.hypot(x - c, y - c))
        return img

    def ring(r):
        def f(d):
            band = abs(d - (r - 1.6))
            if band < 0.7:
                return W
            if band < 1.3:
                return M
            if band < 1.8:
                return D
            return W if d < 1.0 else B if d < 1.7 else M if d < 2.3 else D if d < 2.8 else 0   # the centre dot
        return disc(2 * r, f)
    pics = [ring(r) for r in COPY_RADII]
    pics.append(disc(6, lambda d: W if d < 1.0 else M if d < 1.9 else D if d < 2.6 else 0))   # the small bullet
    for o in range(COPY_DIRS):
        for k in range(COPY_FADE):
            core, edge, half = ((W, M, 6), (D, B, 4))[k]
            img = np.zeros((15, 15), np.uint8)
            dx, dy = ((1, 0), (1, 1), (0, 1), (1, -1))[o]
            for t in np.linspace(-half, half, 4 * half + 1):
                x, y = int(round(7 + t * dx)), int(round(7 + t * dy))
                for ox, oy in ((0, 1), (0, -1), (1, 0), (-1, 0)) if k < 1 else ():
                    if not img[y + oy, x + ox]:
                        img[y + oy, x + ox] = edge
                img[y, x] = core
            pics.append(img)
    assert len(pics) == N_COPY
    return pics, pal


def spike_pictures():
    """Wolfang's spike: (tiles [16x16], frames [[(attr, tile index, x, y)]]) for SPIKE_PICS"""
    import boss_look as L
    import build_axl as BA
    b = L.Boss(open(os.path.join(BA.SRC, 'ROCK_X6.DAT'), 'rb').read(), L.BOSSES[2])
    ids, tiles, frames = {}, [], []
    for f in SPIKE_PICS:
        fr = []
        for a, lo, x, y in b.parts(f):
            if a & 0x20:
                continue
            t = b.tile(a, lo, f).astype(np.uint8)
            key = t.tobytes()
            if key not in ids:
                ids[key] = len(tiles)
                tiles.append(t)
            fr.append((a, ids[key], x, y))
        frames.append(fr)
    return tiles, frames


def spike_turned():
    """[(tiles, parts)] of TURNED_PICS upside down (the icicles), then turned to point left (the wall spikes)"""
    import boss_look as L
    import build_axl as BA
    b = L.Boss(open(os.path.join(BA.SRC, 'ROCK_X6.DAT'), 'rb').read(), L.BOSSES[2])
    out = []
    for quarter in (False, True):
        for f in TURNED_PICS:
            img = b.compose(f)
            ys, xs = np.nonzero(img)
            box = img[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
            h, w = box.shape
            oy, ox = ys.min() - L.C, xs.min() - L.C          # the picture's corner from the origin
            if quarter:                                      # (x, y) -> (y, -x): pointing up -> pointing left
                t = np.rot90(box, 1)
                ox, oy = oy, -(ox + w - 1)
            else:                                            # (x, y) -> (x, -y): upside down
                t = box[::-1]
                oy = -(oy + h - 1)
            th, tw = (t.shape[0] + 15) // 16, (t.shape[1] + 15) // 16
            tiles, fr = [], []
            for ty in range(th):
                for tx in range(tw):
                    cell = np.zeros((16, 16), np.int16)
                    part = t[16 * ty:16 * ty + 16, 16 * tx:16 * tx + 16]
                    cell[:part.shape[0], :part.shape[1]] = part
                    if not cell.any():
                        continue
                    pal = int(cell.max()) // 16
                    tiles.append((cell % 16).astype(np.uint8) * (cell > 0))
                    fr.append((pal << 2, len(tiles) - 1, int(ox) + 16 * tx, int(oy) + 16 * ty))
            out.append((tiles, fr))
    return out


def giga_pictures():
    """(pictures [RGBA arrays, data facing left], palette [16] 15-bit): GIGA_SPIN gun angles, then the burst"""
    from PIL import Image
    sheet = Image.open(os.path.join(F.ROOT, 'mods', 'axl', 'work', 'sheet', 'axl_sheet.png')).convert('RGB')
    bg = (27, 89, 153)

    def cell(r, c):
        y0, x0, y1, x1 = GIGA_CELLS[r][c]
        im = sheet.crop((x0, y0, x1, y1)).convert('RGBA')
        a = np.array(im)
        a[(a[:, :, :3] == bg).all(2), 3] = 0
        return Image.fromarray(a)
    g0, g45 = cell(2, 0), cell(0, 0)
    T = Image.Transpose
    guns = [g0, g45, g0.transpose(T.ROTATE_90), g45.transpose(T.FLIP_LEFT_RIGHT), g0.transpose(T.FLIP_LEFT_RIGHT),
            g45.transpose(T.ROTATE_180), g0.transpose(T.ROTATE_270), g45.transpose(T.FLIP_TOP_BOTTOM)]
    pics = guns + [cell(2, c) for c in GIGA_BURST]
    pics = [np.array(p.transpose(T.FLIP_LEFT_RIGHT)) for p in pics]   # (data faces left)
    cols = sorted({tuple(int(v) for v in px[:3]) for p in pics for px in p.reshape(-1, 4) if px[3]})
    if len(cols) > 15:   # the 15 most used, the rest to the nearest of them
        from collections import Counter
        cnt = Counter(tuple(int(v) for v in px[:3]) for p in pics for px in p.reshape(-1, 4) if px[3])
        cols = [c for c, _ in cnt.most_common(15)]
    pal = [0] + [(r >> 3) | (g >> 3) << 5 | (b >> 3) << 10 or 0x8000 for r, g, b in cols]
    pal += [0] * (16 - len(pal))
    carr = np.array(cols)
    out = []
    for p in pics:
        idx = np.zeros(p.shape[:2], np.uint8)
        m = p[:, :, 3] > 0
        d = ((p[:, :, None, :3].astype(int) - carr[None, None]) ** 2).sum(3)
        idx[m] = d.argmin(2)[m] + 1
        out.append(idx)
    return out, pal
FX_CTEX = 'X6\\chr\\com\\com00'   # weapon objects draw from the common list (index 0 = com00), parts from pl02's PAT


def fx_data():
    """the bosses' projectile pictures as on PS1 (boss_look.fx_frames): (arts [9] tiles per boss, parts per frame,
    owner boss of each frame); the frames are numbered as the FX_* anims of Axl's table use them"""
    import boss_look as L
    arts, parts, anims = L.fx_frames(open(F.DAT['us'], 'rb').read())
    owner = {}
    for _, k, seq in anims:
        for _, f in seq:
            owner[f] = k
    assert sorted(owner) == list(range(len(parts)))
    return arts, parts, owner


SHOT_DIRS = 32          # Axl's bullet in 32 directions (11.25 degrees; 0 = right, counter-clockwise on screen)
SHOT_FLY = (5, 6, 7, 8, 9)   # X's shot anim 0x15 frames (PAT section 1; 4 = launch), one 16 x 16 com00 tile each


def shot_tiles(arc, pat_name):
    """the flying shot pictures: 16 x 16 index images (PAT section 1 frames SHOT_FLY over com00's tiles)"""
    tex = F.tex_image(arc.get('X6\\chr\\com\\com00_000', F.H_TEX))
    secs = F.pat_sections(arc.get(pat_name, F.H_PAT))
    out = []
    for f in SHOT_FLY:
        (rec,) = secs[1][f]
        a, lo, x, y = struct.unpack('<BBbb', rec)
        assert (a & 0xc3, x, y) == (0, -8, -8), (f, a, x, y)
        t = lo | (a & 3) << 8
        out.append(np.where(tex[16 * (t // 16):16 * (t // 16) + 16, 16 * (t % 16):16 * (t % 16) + 16] == F.TEX_PAD, 0,
                            tex[16 * (t // 16):16 * (t // 16) + 16, 16 * (t % 16):16 * (t % 16) + 16]))
    return out


def rotated(tile, deg, n=32):
    """nearest-neighbour rotation (counter-clockwise on screen) of a square picture centred on the origin (16 x 16:
    the bullet) -> n x n picture centred on the origin"""
    a = np.radians(deg)
    s = tile.shape[0]
    yy, xx = np.mgrid[0:n, 0:n]
    u, v = xx + 0.5 - n // 2, yy + 0.5 - n // 2
    sx = np.floor(u * np.cos(a) - v * np.sin(a) + s // 2).astype(int)
    sy = np.floor(u * np.sin(a) + v * np.cos(a) + s // 2).astype(int)
    ok = (sx >= 0) & (sx < s) & (sy >= 0) & (sy < s)
    out = np.zeros((n, n), np.uint8)
    out[ok] = tile[sy[ok], sx[ok]]
    return out


def cut(img):
    """square picture (origin at its centre; 32 x 32: the bullet) -> (tiles, parts [(x, y)]) on the 16 px grid offset
    with the fewest tiles"""
    n = img.shape[0]
    best = None
    for oy in range(16):
        for ox in range(16):
            tl, ps = [], []
            for ty in range(-oy, n, 16):
                for tx in range(-ox, n, 16):
                    t = np.zeros((16, 16), np.uint8)
                    y0, x0 = max(ty, 0), max(tx, 0)
                    y1, x1 = min(ty + 16, n), min(tx + 16, n)
                    t[y0 - ty:y1 - ty, x0 - tx:x1 - tx] = img[y0:y1, x0:x1]
                    if t.any():
                        tl.append(t)
                        ps.append((tx - n // 2, ty - n // 2))
            if best is None or len(tl) < len(best[0]):
                best = (tl, ps)
    return best


def shot_frames(arc, pat_name):
    """[(tiles, parts)] per direction (frame i of direction d = SHOT_FLY[i] turned d * 11.25 degrees), all of one
    direction's tiles in one block of 32 (2 rows), parts numbered in the block"""
    src = shot_tiles(arc, pat_name)
    dirs = []
    for d in range(SHOT_DIRS):
        tiles, frames = [], []
        for t in src:
            tl, ps = cut(rotated(t, d * 360 / SHOT_DIRS))
            frames.append([(len(tiles) + i, x, y) for i, (x, y) in enumerate(ps)])
            tiles += tl
        assert len(tiles) <= 32, (d, len(tiles))
        dirs.append((tiles, frames))
    return dirs


MENU_ICON_TEX = ('X6\\chr\\wepsel\\x_wep2_eng_000', 'X6\\col\\wepsel\\x_wep2_eng')   # the weapon menu's
MENU_ICON_PAL = 33      # its icons' green palette (as the game's weapon menu shows them)
MENU_PAL_SLOT = 410     # our badges: 5 palette slots each from 410 (4 by depth, far = dark .. near = bright, then selected)
MENU_LEVELS = (0.35, 0.55, 0.78, 1.0)
BADGE = 24


MENU_ICON_PAL = 33   # the icons' own (green) palette: its shading is kept, the hue becomes the boss's (boss_hue)


def boss_hue(bo):
    """(hue, saturation) of the boss's main colour: the 30-degree hue band its pictures use most (grey and very dark
    colours left out), weighted mean (user request 2026-10-27: each badge in its boss's colours)"""
    import colorsys
    import math
    cnt = {}
    for parts, tiles, *_ in bo['poses']:
        for attr, lo, x, y in parts:
            t = lo | (attr & 3) << 8
            if t >= len(tiles):
                continue
            pal = bo['pals'][(attr & 0xc) >> 2]
            for v, n in zip(*np.unique(tiles[t], return_counts=True)):
                if v:
                    cnt[int(pal[v])] = cnt.get(int(pal[v]), 0) + int(n)
    bands = {}
    for c, n in cnt.items():
        h, s, v = colorsys.rgb_to_hsv((c & 31) / 31, (c >> 5 & 31) / 31, (c >> 10 & 31) / 31)
        if s >= 0.3 and v >= 0.25:
            bands.setdefault(int(h * 12) % 12, []).append((n, h, s))
    top = max(bands.values(), key=lambda b: sum(n for n, _, _ in b))
    w = sum(n for n, _, _ in top)
    hx = sum(n * math.cos(2 * math.pi * h) for n, h, _ in top) / w
    hy = sum(n * math.sin(2 * math.pi * h) for n, h, _ in top) / w
    return (math.atan2(hy, hx) / (2 * math.pi)) % 1, sum(n * s for n, _, s in top) / w
AXL_EMBLEM = (912, 168)  # a pixel inside the "A" of the sheet's X8 gauge emblem (work/sheet/axl_sheet.png)
A_COLOURS = {(0x1d, 0x1d, 0x2c), (0x3a, 0x4b, 0x6b), (0x93, 0xa3, 0xc1), (0xf0, 0xf0, 0xf0)}   # its outline .. white


def axl_emblem():
    """Axl's badge glyph: only the letter A of the sheet's gauge emblem (user request 2026-10-27: no frame), the
    pixels of the letter's colours joined to its inside, centred in 16 x 16 -> (index image 0 / 4.., RGB colours)"""
    from PIL import Image
    im = np.array(Image.open(os.path.join(HERE, '..', 'work', 'sheet', 'axl_sheet.png')).convert('RGB')).astype(int)
    x0, y0 = AXL_EMBLEM
    seen, todo = set(), [(y0, x0)]
    while todo:
        y, x = todo.pop()
        if (y, x) in seen or tuple(im[y, x]) not in A_COLOURS:
            continue
        seen.add((y, x))
        todo += [(y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)]
    ys, xs = [y for y, _ in seen], [x for _, x in seen]
    h, w = max(ys) - min(ys) + 1, max(xs) - min(xs) + 1
    assert h <= 16 and w <= 16, (h, w)
    oy, ox = (16 - h) // 2 - min(ys), (16 - w) // 2 - min(xs)
    cols = sorted({tuple(int(c) >> 3 for c in im[y, x]) for y, x in seen})
    idx = {c: 4 + i for i, c in enumerate(cols)}
    img = np.zeros((16, 16), np.uint8)
    for y, x in seen:
        img[y + oy, x + ox] = idx[tuple(int(c) >> 3 for c in im[y, x])]
    return img, cols


def menu_icons(arc, bosses=None):
    """the DNA menu badges: 0 = Axl (the sheet's 'A'), k = X's weapon icon k (weapon menu texture, column x 144,
    row 7 + k) in boss k's palette, on a round badge -> (index images [9] of 24 x 24, palettes [9] of
    (MENU_LEVELS by depth + selected) x 16) (15-bit colours); bosses (all_bosses) give the hues (palettes only)"""
    names = {e['name'] for e in arc.entries}
    tn, cn = [n if n in names else n.replace('_eng', '') for n in MENU_ICON_TEX]   # (the JP archive: x_wep2)
    tex = F.tex_image(arc.get(tn, F.H_TEX))
    allpal = np.frombuffer(arc.get(cn, F.H_COL)[12:], '<u2')
    icons = [np.where(tex[96 + 16 * (k + 1):112 + 16 * (k + 1), 144:160] == F.TEX_PAD, 0,
                      tex[96 + 16 * (k + 1):112 + 16 * (k + 1), 144:160]) for k in range(1, 9)]
    used = sorted({int(v) for ic in icons for v in np.unique(ic) if v})
    assert len(used) <= 12, used
    remap = {v: 4 + i for i, v in enumerate(used)}
    glyph, axl_cols = axl_emblem()
    out = []
    c0 = (BADGE - 1) / 2
    for i in range(9):
        img = np.zeros((BADGE, BADGE), np.uint8)
        for y in range(BADGE):
            for x in range(BADGE):
                r = ((x - c0) ** 2 + (y - c0) ** 2) ** 0.5
                if r <= BADGE / 2:
                    img[y, x] = 1 if r > BADGE / 2 - 1 else 2 if r > BADGE / 2 - 2 else 3
        ic = glyph if i == 0 else np.vectorize(lambda v: remap.get(int(v), 0))(icons[i - 1])
        for y in range(16):
            for x in range(16):
                if ic[y, x]:
                    img[4 + y, 4 + x] = ic[y, x]
        out.append(img)

    def c15(r, g, b):
        return (r >> 3) | (g >> 3) << 5 | (b >> 3) << 10 | 0x8000

    def bright(v, t):
        r, g, b = v & 31, v >> 5 & 31, v >> 10 & 31
        return (int(r + (31 - r) * t)) | (int(g + (31 - g) * t)) << 5 | (int(b + (31 - b) * t)) << 10 | 0x8000

    def dim(v, t):
        if not v & 0x7fff:
            return v
        r, g, b = v & 31, v >> 5 & 31, v >> 10 & 31
        return int(r * t) | int(g * t) << 5 | int(b * t) << 10 | 0x8000
    import colorsys
    green = allpal[16 * MENU_ICON_PAL:16 * MENU_ICON_PAL + 16]
    hsv = {v: colorsys.rgb_to_hsv((int(green[v]) & 31) / 31, (int(green[v]) >> 5 & 31) / 31,
                                  (int(green[v]) >> 10 & 31) / 31) for v in used}
    mean_s = sum(s for _, s, _ in hsv.values()) / len(hsv)
    pals = []
    for i in range(9):
        if i == 0:
            body = [r | g << 5 | b << 10 | 0x8000 for r, g, b in axl_cols]
        elif bosses is None:
            body = [int(green[v]) | 0x8000 for v in used]
        else:
            hb, sb = boss_hue(bosses[i - 1])
            body = []
            for v in used:
                _, s, val = hsv[v]
                r, g, b = colorsys.hsv_to_rgb(hb, min(1.0, s * sb / mean_s), val)
                body.append(int(r * 31 + 0.5) | int(g * 31 + 0.5) << 5 | int(b * 31 + 0.5) << 10 | 0x8000)
        normal = [0, c15(40, 56, 112), c15(176, 200, 240), c15(16, 24, 56)] + body
        sel = [0, c15(96, 72, 0), c15(255, 232, 72), c15(32, 48, 104)] + [bright(v, 0.45) for v in body]
        normal += [0] * (16 - len(normal))
        sel += [0] * (16 - len(sel))
        pals.append([[dim(v, t) for v in normal] for t in MENU_LEVELS] + [sel])
    return out, pals


def fx_looks(arc, pat_name, fx):
    """one texture (block k - 1 = boss k's art, 32 tiles in 2 rows; then Axl's bullet in SHOT_DIRS directions, one
    run of tiles from row 256), registered in the common list the weapon objects draw from, and one parts section (all projectile
    frames, each drawn from its block; then the bullet frames: SHOT_BASE + len(SHOT_FLY) * d + i); returns (texture
    index, PAT section)"""
    arts, parts, owner = fx
    dirs = shot_frames(arc, pat_name)
    shot_tiles_all = [t for tiles, _ in dirs for t in tiles]   # one run after the bosses' blocks (rows 256-)
    h = 16
    giga, giga_pal = giga_pictures()
    flames, flame_pal = flame_pictures()
    copy_pics, flame_pal = copy_pictures(flame_pal)
    spike = spike_pictures()
    giga_tiles = sum(((g.shape[0] + 15) // 16) * ((g.shape[1] + 15) // 16) for g in giga) + 4 * len(flames) + 4 * N_COPY + len(spike[0])
    while h < 32 * 8 + 16 * ((len(shot_tiles_all) + 4 * 9 + giga_tiles + 15) // 16):
        h *= 2
    assert h <= 1024, h      # (a 2048 px texture crashed the game when drawn, 2026-10-26)
    tex = np.full((h, 256), F.TEX_PAD, np.uint8)
    for k in range(1, 9):
        for i, t in enumerate(arts[k]):
            y, x = 32 * (k - 1) + 16 * (i // 16), 16 * (i % 16)
            tex[y:y + 16, x:x + 16] = t
    for i, t in enumerate(shot_tiles_all):
        y, x = 32 * 8 + 16 * (i // 16), 16 * (i % 16)
        tex[y:y + 16, x:x + 16] = t
    head = struct.pack('<4sIIII', b'TEX\0', 0x200080a1, 1 | 256 << 6 | tex.shape[0] << 19, 0x00011201, 0x14)
    assert len(parts) == SHOT_BASE, len(parts)
    ys = [32 * (owner[f] - 1) for f in range(len(parts))]
    parts = list(parts)
    first = 0
    for tiles, frames in dirs:
        for fr in frames:
            parts.append([((first + n) >> 8, (first + n) & 0xff, x, y) for n, x, y in fr])
            ys.append(32 * 8)
        first += len(tiles)
    # the DNA menu badges after the bullet tiles: 4 tiles each (2 x 2 grid over the 24 px badge)
    assert len(parts) == ICON_BASE, len(parts)
    badges, _ = menu_icons(arc)
    t0 = len(shot_tiles_all)
    for i, b in enumerate(badges):
        big = np.zeros((32, 32), np.uint8)
        big[4:28, 4:28] = b
        fr = []
        for j in range(4):
            n = t0 + 4 * i + j
            y, x = 32 * 8 + 16 * (n // 16), 16 * (n % 16)
            tex[y:y + 16, x:x + 16] = big[16 * (j // 2):16 * (j // 2) + 16, 16 * (j % 2):16 * (j % 2) + 16]
            fr.append((n >> 8, n & 0xff, -16 + 16 * (j % 2), -16 + 16 * (j // 2)))
        parts.append(fr)
        ys.append(32 * 8)
    # the Giga Crush pictures after the badges, each centred on its object
    assert len(parts) == GIGA_BASE, len(parts)
    n = t0 + 4 * len(badges)
    for g in giga:
        gh, gw = (g.shape[0] + 15) // 16 * 16, (g.shape[1] + 15) // 16 * 16
        big = np.zeros((gh, gw), np.uint8)
        big[(gh - g.shape[0]) // 2:(gh - g.shape[0]) // 2 + g.shape[0], (gw - g.shape[1]) // 2:(gw - g.shape[1]) // 2 + g.shape[1]] = g
        fr = []
        for ty in range(gh // 16):
            for tx in range(gw // 16):
                y, x = 32 * 8 + 16 * (n // 16), 16 * (n % 16)
                tex[y:y + 16, x:x + 16] = big[16 * ty:16 * ty + 16, 16 * tx:16 * tx + 16]
                fr.append((n >> 8, n & 0xff, -gw // 2 + 16 * tx, -gh // 2 + 16 * ty))
                n += 1
        parts.append(fr)
        ys.append(32 * 8)
    fx_looks.giga_pal = giga_pal
    # then the hover flames, each with its nozzle texel on the object's origin (up to 2 x 2 tiles)
    assert len(parts) == FLAME_BASE, len(parts)
    for img, nx, ny in flames:
        big = np.zeros((32, 32), np.uint8)
        big[:img.shape[0], :img.shape[1]] = img
        fr = []
        for j in range(4):
            tile = big[16 * (j // 2):16 * (j // 2) + 16, 16 * (j % 2):16 * (j % 2) + 16]
            if not tile.any():
                continue
            y, x = 32 * 8 + 16 * (n // 16), 16 * (n % 16)
            tex[y:y + 16, x:x + 16] = tile
            fr.append((n >> 8, n & 0xff, 16 * (j % 2) - nx, 16 * (j // 2) - ny))
            n += 1
        parts.append(fr)
        ys.append(32 * 8)
    # then the copy shot's pictures, each centred on its object
    assert len(parts) == COPY_BASE, len(parts)
    for g in copy_pics:
        gh, gw = (g.shape[0] + 15) // 16 * 16, (g.shape[1] + 15) // 16 * 16
        big = np.zeros((gh, gw), np.uint8)
        oy, ox = (gh - g.shape[0]) // 2, (gw - g.shape[1]) // 2
        big[oy:oy + g.shape[0], ox:ox + g.shape[1]] = g
        fr = []
        for ty in range(gh // 16):
            for tx in range(gw // 16):
                tile = big[16 * ty:16 * ty + 16, 16 * tx:16 * tx + 16]
                if not tile.any():
                    continue
                y, x = 32 * 8 + 16 * (n // 16), 16 * (n % 16)
                tex[y:y + 16, x:x + 16] = tile
                fr.append((n >> 8, n & 0xff, -ox - g.shape[1] // 2 + 16 * tx, -oy - g.shape[0] // 2 + 16 * ty))
                n += 1
        parts.append(fr)
        ys.append(32 * 8)
    # then Wolfang's spike pictures: the boss's own parts (palette bits kept) on their tiles
    assert len(parts) == SPIKE_BASE, len(parts)
    s_tiles, s_frames = spike
    t0 = n
    for i, t in enumerate(s_tiles):
        y, x = 32 * 8 + 16 * (n // 16), 16 * (n % 16)
        tex[y:y + 16, x:x + 16] = t
        n += 1
    for fr in s_frames:
        parts.append([((a & 0xfc) | (t0 + i) >> 8, (t0 + i) & 0xff, x, y) for a, i, x, y in fr])
        ys.append(32 * 8)
    # the turned spikes (spike_turned): their own tiles
    assert len(parts) == ICICLE_BASE, len(parts)
    for tiles, fr in spike_turned():
        t0 = n
        for t in tiles:
            y, x = 32 * 8 + 16 * (n // 16), 16 * (n % 16)
            tex[y:y + 16, x:x + 16] = t
            n += 1
        parts.append([(a | (t0 + i) >> 8, (t0 + i) & 0xff, x, y) for a, i, x, y in fr])
        ys.append(32 * 8)
    assert len(parts) <= 256, len(parts)
    fx_looks.flame_pal = flame_pal
    ti = F.add_texture(arc, FX_CTEX, F.TEX_NAME % FX_TEX, head + tex.tobytes(), arc.get(F.TEX_NAME % 0, F.H_COF),
                       len(ys), ys)
    secs = F.pat_sections(arc.get(pat_name, F.H_PAT))
    secs.append([[struct.pack('<BBbb', *q) for q in ps] for ps in parts])
    arc.put(pat_name, F.H_PAT, F.pat(secs))
    return ti, len(secs) - 1


# The skill HUD (docs/skills_design.md, user decisions 2026-11-03; axl_hud.c): while changed, 4 buttons at the bottom
# left (shot, special, dash, giga), each an icon on a dark backing in a square border that fills clockwise as the
# cooldown runs. Its own texture (com00's list, after the projectile one) and one parts section per boss: frame
# HUD_FRAMES * slot + step = that slot's icon in the border filled step / HUD_STEPS (the border's parts on palette
# layer 0, the icon's on layer 1: the DLL picks the brightness by the palette slot, HUD_LEVELS pairs).
HUD_TEX = 18 + 10       # pl02_028
HUD_ICON = 16           # the icon tile: backing + a picture of at most 14 px
HUD_RING = 2            # the border around it
HUD_CELL = HUD_ICON + 2 * HUD_RING
HUD_STEPS = 16
# per boss (shot, special, dash, giga): ('fx', NAME[, degrees]) his projectile picture (FX anim NAME's first, turned),
# ('balls', NAME) three small ones, ('pose', SEQ) his own first picture of that sequence, ('glyph', NAME) a drawn sign
# (dash: two chevrons, giga: a burst, slam: an arrow onto the floor) in his colour
HUD_ICONS = {1: (('fx', 'SHOT'), ('fx', 'OPT'), ('glyph', 'dash'), ('glyph', 'giga')),
             2: (('fx', 'ICE'), ('fx', 'SHARD'), ('glyph', 'dash'), ('glyph', 'giga')),
             3: (('fx', 'FIRE'), ('fx', 'FIREBALL'), ('glyph', 'dash'), ('glyph', 'giga')),
             4: (('fx', 'ANCHOR'), ('glyph', 'slam'), ('glyph', 'dash'), ('glyph', 'giga')),
             5: (('fx', 'BALL'), ('balls', 'BALL'), ('glyph', 'dash'), ('glyph', 'giga')),
             6: (('fx', 'RAIN'), ('pose', 'alt'), ('glyph', 'dash'), ('glyph', 'giga')),
             7: (('fx', 'SHELL'), ('fx', 'PLATE_V'), ('glyph', 'dash'), ('glyph', 'giga')),
             8: (('fx', 'RAY_H', 45), ('fx', 'CLONE'), ('glyph', 'dash'), ('glyph', 'giga'))}
HUD_LEVELS = ('cool', 'ready', 'flash')   # palette pairs: cooling down (icon dimmed), ready, the ready flash
# The giga attacks' own pictures (docs/skills_design.md §2.2; axl_dna_bosses.c giga_*): the FX texture is full (32 tiles
# a boss, 256 frames), so they go into the HUD texture too, as frames HUD_FRAMES * 4 + i of the boss's HUD section,
# drawn in the boss's own 4 palettes (the parts' palette layer as in his art; the projectile's slot = dna_pal_slot).
# Per boss: (NAME, parts table, [boss pictures], crop radius or 0) -> GIGA_NAME (its first frame) and GIGA_NAME_N
GIGA_PICS = {5: (('BALL', 1, (21, 22, 23), 47),),          # Scaravich's rock ball at full size (size 4: about 96 px)
             8: (('GATHER', 0, (94, 95, 96, 97, 98, 99), 0),   # Mijinion's energy gathering (8 orbs closing in)
                 ('ORB', 0, (102, 103), 0))}                    # his small energy orb (the 8-way shot)
HUD_BACK = (44, 54, 92)                   # the icon's backing


def hud_glyph(name, n=14):
    """a sign's mask (n x n), drawn 4x larger and averaged down"""
    import math
    from PIL import Image, ImageDraw
    s = 4
    im = Image.new('L', (n * s, n * s), 0)
    d = ImageDraw.Draw(im)
    if name == 'dash':
        for x0 in (0, 6):
            d.polygon([(x0 * s, 1 * s), ((x0 + 3.5) * s, 1 * s), ((x0 + 8) * s, 7 * s), ((x0 + 3.5) * s, 13 * s),
                       (x0 * s, 13 * s), ((x0 + 4.5) * s, 7 * s)], fill=255)
    elif name == 'giga':
        pts = []
        for i in range(16):
            a = math.pi * 2 * i / 16 - math.pi / 2
            r = (7 if i % 4 == 0 else 4.8 if i % 2 == 0 else 3.0) * s
            pts.append((7 * s + r * math.cos(a), 7 * s + r * math.sin(a)))
        d.polygon(pts, fill=255)
    elif name == 'slam':
        d.rectangle([5 * s, 0, 9 * s - 1, 6 * s], fill=255)
        d.polygon([(1.5 * s, 6 * s), (12.5 * s, 6 * s), (7 * s, 11 * s)], fill=255)
        d.rectangle([0, 12 * s, 14 * s - 1, 14 * s - 1], fill=255)
    else:
        raise ValueError(name)
    return np.array(im.resize((n, n), Image.BOX)) >= 128


def hud_shade(mask, hue, sat):
    """a sign's mask -> RGBA in the boss's hue: light top / left edges, dark bottom / right edges"""
    import colorsys

    def c(s, v):
        r, g, b = colorsys.hsv_to_rgb(hue, min(1.0, s), v)
        return (int(r * 255), int(g * 255), int(b * 255), 255)
    light, mid, dark = c(sat * 0.35, 1.0), c(sat, 0.92), c(sat * 1.1, 0.55)
    h, w = mask.shape
    out = np.zeros((h, w, 4), np.uint8)
    for y in range(h):
        for x in range(w):
            if mask[y, x]:
                up = y == 0 or not mask[y - 1, x] or x == 0 or not mask[y, x - 1]
                dn = y == h - 1 or not mask[y + 1, x] or x == w - 1 or not mask[y, x + 1]
                out[y, x] = light if up else dark if dn else mid
    return out


def hud_fit(rgba, n=14):
    """the picture cut to its pixels, made at most n px (smaller only) and centred in n x n"""
    from PIL import Image
    ys, xs = np.nonzero(rgba[..., 3])
    im = Image.fromarray(rgba[ys.min():ys.max() + 1, xs.min():xs.max() + 1])
    sc = min(1.0, n / max(im.size))
    if sc < 1:
        im = im.resize((max(1, round(im.size[0] * sc)), max(1, round(im.size[1] * sc))), Image.LANCZOS)
    a = np.array(im)
    a[..., 3] = np.where(a[..., 3] >= 100, 255, 0)
    out = np.zeros((n, n, 4), np.uint8)
    oy, ox = (n - a.shape[0]) // 2, (n - a.shape[1]) // 2
    out[oy:oy + a.shape[0], ox:ox + a.shape[1]] = a
    return out


def hud_render(tile_of, pals, frame, size=256):
    """RGBA of a parts frame (attr & 3 tile high bits, (attr >> 2) & 3 palette, 0x40 / 0x80 flips) about the centre"""
    img = np.zeros((size, size, 4), np.uint8)
    c = size // 2
    for a, lo, x, y in frame:
        t = tile_of(lo | (a & 3) << 8)
        if a & 0x40:
            t = t[:, ::-1]
        if a & 0x80:
            t = t[::-1]
        pal = pals[(a >> 2) & 3]
        for yy in range(16):
            for xx in range(16):
                v = int(t[yy, xx])
                if v:
                    col = int(pal[v])
                    img[c + y + yy, c + x + xx] = ((col & 31) * 255 // 31, (col >> 5 & 31) * 255 // 31,
                                                   (col >> 10 & 31) * 255 // 31, 255)
    return img


def hud_rings():
    """HUD_STEPS + 1 border pictures (HUD_CELL square; 1 / 2 the empty track outer / inner, 3 / 4 the filled part):
    the fill runs clockwise from the top middle; the outer corners left out"""
    import math
    n, c0 = HUD_CELL, (HUD_CELL - 1) / 2
    out = []
    for g in range(HUD_STEPS + 1):
        img = np.zeros((n, n), np.uint8)
        for y in range(n):
            for x in range(n):
                d = min(x, y, n - 1 - x, n - 1 - y)
                if d >= HUD_RING or (min(x, n - 1 - x) == 0 and min(y, n - 1 - y) == 0):
                    continue
                a = math.atan2(x - c0, c0 - y) % (2 * math.pi)
                full = g == HUD_STEPS or a < 2 * math.pi * g / HUD_STEPS
                img[y, x] = (3 if full else 1) + (d == 1)
        out.append(img)
    return out


def hud_pictures(fx_anims, bosses):
    """-> (rings, icons {k: [4 index images HUD_ICON square]}, pals [9][HUD_LEVELS][2 layers][16] 15-bit):
    fx_anims = boss_look.fx_frames (arts, parts, anims). Per boss one icon palette: 0 none, 1 the backing, 2-4 the
    signs' light / mid / dark, 5-15 the pictures' colours (quantised together)"""
    import colorsys
    from PIL import Image
    arts, parts, anims = fx_anims
    first = {name: (k, seq[0][1]) for name, k, seq in anims}
    pals = [[[[0] * 16, [0] * 16] for _ in HUD_LEVELS]]
    icons = {}

    def c15(r, g, b):
        return (r >> 3) | (g >> 3) << 5 | (b >> 3) << 10 | 0x8000

    def hsv15(h, s, v):
        r, g, b = colorsys.hsv_to_rgb(h, min(1.0, s), v)
        return c15(int(r * 255), int(g * 255), int(b * 255))

    def scale(v, t, white=0.0):
        if not v & 0x7fff:
            return v
        rgb = [v & 31, v >> 5 & 31, v >> 10 & 31]
        rgb = [min(31, int(c * t + (31 - c * t) * white + 0.5)) for c in rgb]
        return rgb[0] | rgb[1] << 5 | rgb[2] << 10 | 0x8000
    for k in range(1, 9):
        bo = bosses[k - 1]
        hue, sat = boss_hue(bo)
        pics, signs = [], []
        for spec in HUD_ICONS[k]:
            if spec[0] == 'glyph':
                signs.append(len(pics))
                pics.append(hud_shade(hud_glyph(spec[1]), hue, sat))
                continue
            if spec[0] == 'pose':
                ps, tiles, _, _ = bo['poses'][bo['seqs'][spec[1]][0][1]]
                pics.append(hud_fit(hud_render(lambda t: tiles[t], bo['pals'], ps)))
                continue
            kk, f = first[spec[1]]
            im = hud_render(lambda t: arts[kk][t], bo['pals'], parts[f])
            if len(spec) > 2:
                im = np.array(Image.fromarray(im).rotate(spec[2], Image.NEAREST))
            if spec[0] == 'balls':
                b = hud_fit(im, 8)
                im = np.zeros((14, 14, 4), np.uint8)
                m = b[..., 3] > 0
                for oy, ox in ((0, 3), (6, 0), (6, 6)):
                    im[oy:oy + 8, ox:ox + 8][m] = b[m]
                pics.append(im)
            else:
                pics.append(hud_fit(im))
        # the signs' 3 colours are fixed; the pictures' colours quantised into the other 11
        sign_cols = {}
        for i in signs:
            for px in pics[i].reshape(-1, 4):
                if px[3]:
                    sign_cols.setdefault(tuple(int(c) for c in px[:3]), len(sign_cols))
        assert len(sign_cols) <= 3, sign_cols
        rest = [i for i in range(4) if i not in signs]
        strip = np.concatenate([pics[i][..., :3] for i in rest], axis=1)
        mask = np.concatenate([pics[i][..., 3] > 0 for i in rest], axis=1)
        q = Image.fromarray(strip[mask].reshape(1, -1, 3)).quantize(11, method=Image.Quantize.MEDIANCUT)
        qpal = q.getpalette()[:33]
        body = [c15(*sorted(sign_cols, key=sign_cols.get)[j]) if j < len(sign_cols) else 0 for j in range(3)]
        body += [c15(qpal[3 * j], qpal[3 * j + 1], qpal[3 * j + 2]) for j in range(11)]
        qcols = np.array(qpal, int).reshape(11, 3)
        out = []
        for i, pic in enumerate(pics):
            img = np.ones((HUD_ICON, HUD_ICON), np.uint8)
            m = pic[..., 3] > 0
            if i in signs:
                vals = np.array([2 + sign_cols[tuple(int(c) for c in px[:3])] if px[3] else 0
                                 for px in pic.reshape(-1, 4)], np.uint8).reshape(14, 14)
            else:
                d = ((pic[..., :3].astype(int)[:, :, None, :] - qcols[None, None]) ** 2).sum(-1)
                vals = (5 + d.argmin(-1)).astype(np.uint8)   # (the nearest of the quantised colours)
            img[1:15, 1:15][m] = vals[m]
            out.append(img)
        icons[k] = out
        icon_pal = [0, c15(*HUD_BACK)] + body
        ring_cool = [0, c15(40, 48, 72), c15(22, 26, 44), hsv15(hue, sat * 0.8, 0.78), hsv15(hue, sat * 0.5, 0.92)]
        ring_ready = [0, c15(40, 48, 72), c15(22, 26, 44), hsv15(hue, sat * 0.55, 1.0), hsv15(hue, sat * 0.2, 1.0)]
        ring_flash = [0, c15(40, 48, 72), c15(22, 26, 44), c15(255, 255, 255), c15(255, 255, 255)]
        lv = []
        for name, ring in zip(HUD_LEVELS, (ring_cool, ring_ready, ring_flash)):
            t, w = {'cool': (0.42, 0.0), 'ready': (1.0, 0.0), 'flash': (1.0, 0.6)}[name]
            ip = [icon_pal[0], icon_pal[1]] + [scale(v, t, w) for v in icon_pal[2:]]
            lv.append([ring + [0] * (16 - len(ring)), ip + [0] * (16 - len(ip))])
        pals.append(lv)
    return hud_rings(), icons, pals, giga_pictures_hud()


def giga_pictures_hud():
    """GIGA_PICS -> {k: [(name, [index image (16 * palette layer + colour) per picture])]}"""
    import boss_look as L
    dat = open(F.DAT['us'], 'rb').read()
    out = {}
    for k, specs in GIGA_PICS.items():
        b = L.Boss(dat, L.BOSSES[k])
        out[k] = []
        for name, t, pics, r in specs:
            b.pt = L.table(b, dat, L.BOSSES[k], t)
            imgs = []
            for f in pics:
                img = b.compose(L.TABLE1 + f if t == 1 else f)
                if r:   # (the picture's own pixels only: stray parts of another texture dropped, as boss_look.FX)
                    ys, xs = np.nonzero(img)
                    yy, xx = np.mgrid[0:2 * L.C, 0:2 * L.C]
                    cy, cx = np.median(ys), np.median(xs)
                    img = np.where((yy - cy) ** 2 + (xx - cx) ** 2 <= r * r, img, 0)
                imgs.append(img)
            out[k].append((name, imgs))
    return out


def hud_looks(arc, pat_name, hud):
    """the HUD texture (rings' tiles, then the icons') in com00's list and one parts section per boss (HUD_ICONS
    order): returns (texture index, first section)"""
    import boss_look as L
    rings, icons, _, giga = hud
    tiles, ring_parts = [], []
    for img in rings:
        big = np.zeros((32, 32), np.uint8)
        o = (32 - HUD_CELL) // 2
        big[o:o + HUD_CELL, o:o + HUD_CELL] = img
        fr = []
        for j in range(4):
            t = big[16 * (j // 2):16 * (j // 2) + 16, 16 * (j % 2):16 * (j % 2) + 16]
            if t.any():
                fr.append((len(tiles), -16 + 16 * (j % 2), -16 + 16 * (j // 2)))
                tiles.append(t)
        ring_parts.append(fr)
    icon_tile = {}
    for k in range(1, 9):
        for s, img in enumerate(icons[k]):
            icon_tile[k, s] = len(tiles)
            tiles.append(img)
    giga_parts = {}   # (k, name) -> [parts per picture]: a plain 16 px grid per palette layer (drawn unmirrored only:
    # the DLL keeps their facing 0, so no mirrored-view layout as boss_look.retile's)
    for k, specs in sorted(giga.items()):
        for name, imgs in specs:
            fl = []
            for img in imgs:
                ys, xs = np.nonzero(img)
                fr = []
                for layer in sorted({int(v) for v in np.unique(img[img > 0] // 16)}):
                    lay = np.where((img > 0) & (img // 16 == layer), img % 16, 0).astype(np.uint8)
                    for ty in range(ys.min(), ys.max() + 1, 16):
                        for tx in range(xs.min(), xs.max() + 1, 16):
                            t = np.zeros((16, 16), np.uint8)
                            src = lay[ty:ty + 16, tx:tx + 16]
                            t[:src.shape[0], :src.shape[1]] = src
                            if t.any():
                                fr.append((len(tiles) >> 8 | layer << 2, len(tiles) & 0xff, tx - L.C, ty - L.C))
                                tiles.append(t)
                fl.append(fr)
            giga_parts[k, name] = fl
    assert len(tiles) <= 1024, len(tiles)
    h = 16
    while h < 16 * ((len(tiles) + 15) // 16):
        h *= 2
    tex = np.full((h, 256), F.TEX_PAD, np.uint8)
    for i, t in enumerate(tiles):
        tex[16 * (i // 16):16 * (i // 16) + 16, 16 * (i % 16):16 * (i % 16) + 16] = t
    head = struct.pack('<4sIIII', b'TEX\0', 0x200080a1, 1 | 256 << 6 | h << 19, 0x00011201, 0x14)
    secs = F.pat_sections(arc.get(pat_name, F.H_PAT))
    first = len(secs)
    n_frames = 4 * (HUD_STEPS + 1)
    for k in range(1, 9):
        frames = []
        for s in range(4):
            n = icon_tile[k, s]
            for g in range(HUD_STEPS + 1):
                fr = [(t >> 8, t & 0xff, x, y) for t, x, y in ring_parts[g]]
                fr.append((1 << 2 | n >> 8, n & 0xff, -HUD_ICON // 2, -HUD_ICON // 2))
                frames.append([struct.pack('<BBbb', *q) for q in fr])
        for name, _ in giga.get(k, ()):
            frames += [[struct.pack('<BBbb', *q) for q in fr] for fr in giga_parts[k, name]]
        n_frames = max(n_frames, len(frames))
        secs.append(frames)
    arc.put(pat_name, F.H_PAT, F.pat(secs))
    ti = F.add_texture(arc, FX_CTEX, F.TEX_NAME % HUD_TEX, head + tex.tobytes(), arc.get(F.TEX_NAME % 0, F.H_COF),
                       n_frames, [0] * n_frames)
    return ti, first


def hud_header(slot, hud):
    """mods/axl/work/lc2/gen/axl_hud_looks.h: the HUD's texture, sections, frames and palettes for axl_hud.c"""
    _, _, pals, giga = hud
    L = ['/* generated by mods/axl/tools/lc2_dna.py hud_header: the skill HUD pictures (pl02*.arc); do not edit */',
         '#define HUD_TEX_INDEX %d   /* in the weapon objects\' texture list (com00\'s) */' % slot[0],
         '#define HUD_PAT %d         /* boss k: parts section HUD_PAT + k - 1 */' % slot[1],
         '#define HUD_STEPS %d       /* frame HUD_FRAMES * slot + step: the border filled step / HUD_STEPS */' % HUD_STEPS,
         '#define HUD_FRAMES %d' % (HUD_STEPS + 1),
         '#define HUD_CELL %d        /* px: the icon %d and its border %d each side */' % (HUD_CELL, HUD_ICON, HUD_RING),
         '#define HUD_LEVELS %d      /* palette pairs (border, icon): %s */' % (len(HUD_LEVELS), ', '.join(HUD_LEVELS))]
    L += ['#define HUD_%s %d' % (n.upper(), i) for i, n in enumerate(HUD_LEVELS)]
    L += ['/* the giga attacks\' pictures (GIGA_PICS): frames of the boss\'s HUD section, in his own palettes */']
    for k, specs in sorted(giga.items()):
        f = 4 * (HUD_STEPS + 1)
        for name, imgs in specs:
            L += ['#define GIGA_%s %d   /* boss %d */' % (name, f, k), '#define GIGA_%s_N %d' % (name, len(imgs))]
            f += len(imgs)
    L += ['static const unsigned short hud_pals[9][%d][16] = {' % (2 * len(HUD_LEVELS))]
    for k in range(9):
        L.append('    {%s},' % ', '.join('{%s}' % ', '.join('0x%04x' % v for v in p) for lv in pals[k] for p in lv))
    L += ['};', '']
    os.makedirs(F.GEN, exist_ok=True)
    open(os.path.join(F.GEN, 'axl_hud_looks.h'), 'w').write('\n'.join(L))


# Axl's muzzle flash turned to the aim (user request 2026-11-04): X's flash (pool 6 id 2, anim 39 of the shot table:
# PAT section 1 frames FLASH_SRC on com00's tiles, origin = the gun tip) is drawn forward only. Its frames turned in
# FLASH_DIRS directions (the aims are 45-degree steps) go into their own texture and parts section: frame
# FLASH_NF * d + i = FLASH_SRC[i] turned d * 360 / FLASH_DIRS degrees (as SHOT_DIRS: counter-clockwise on screen,
# data facing left); axl_x.c flash_turn.
FLASH_TEX = 18 + 11     # pl02_029
FLASH_DIRS = 8
FLASH_SRC = (0, 1, 2, 3)
FLASH_BOX = 48          # the source pictures on a square centred on the origin (frame 3 reaches x -21)
FLASH_OUT = 64          # turned: the radius of frame 3's far corner is about 26 px


def flash_pictures(arc, pat_name):
    """FLASH_SRC as FLASH_BOX square index images centred on the origin (com00's tiles, parts' flips applied)"""
    tex = F.tex_image(arc.get('X6\\chr\\com\\com00_000', F.H_TEX))
    secs = F.pat_sections(arc.get(pat_name, F.H_PAT))
    out = []
    for f in FLASH_SRC:
        img = np.zeros((FLASH_BOX, FLASH_BOX), np.uint8)
        for rec in secs[1][f]:
            a, lo, x, y = struct.unpack('<BBbb', rec)
            assert a & 0x3c == 0, (f, a)   # (palette layer 0, as the object's slot)
            t = lo | (a & 3) << 8
            tl = tex[16 * (t // 16):16 * (t // 16) + 16, 16 * (t % 16):16 * (t % 16) + 16]
            tl = np.where(tl == F.TEX_PAD, 0, tl)
            if a & 0x40:
                tl = tl[:, ::-1]
            if a & 0x80:
                tl = tl[::-1]
            dst = img[FLASH_BOX // 2 + y:FLASH_BOX // 2 + y + 16, FLASH_BOX // 2 + x:FLASH_BOX // 2 + x + 16]
            dst[tl > 0] = tl[tl > 0]
        out.append(img)
    return out


def flash_looks(arc, pat_name):
    """the turned flash's texture (com00's list, the object's) and parts section: returns (texture index, section)"""
    tiles, frames = [], []
    for d in range(FLASH_DIRS):
        for img in flash_pictures(arc, pat_name):
            tl, ps = cut(rotated(img, d * 360 / FLASH_DIRS, FLASH_OUT))
            frames.append([((len(tiles) + i) >> 8, (len(tiles) + i) & 0xff, x, y) for i, (x, y) in enumerate(ps)])
            tiles += tl
    assert len(tiles) <= 1024, len(tiles)
    h = 16
    while h < 16 * ((len(tiles) + 15) // 16):
        h *= 2
    assert h <= 1024, h      # (a 2048 px texture crashed the game when drawn, 2026-10-26)
    tex = np.full((h, 256), F.TEX_PAD, np.uint8)
    for i, t in enumerate(tiles):
        tex[16 * (i // 16):16 * (i // 16) + 16, 16 * (i % 16):16 * (i % 16) + 16] = t
    head = struct.pack('<4sIIII', b'TEX\0', 0x200080a1, 1 | 256 << 6 | h << 19, 0x00011201, 0x14)
    secs = F.pat_sections(arc.get(pat_name, F.H_PAT))
    secs.append([[struct.pack('<BBbb', *q) for q in fr] for fr in frames])
    arc.put(pat_name, F.H_PAT, F.pat(secs))
    ti = F.add_texture(arc, FX_CTEX, F.TEX_NAME % FLASH_TEX, head + tex.tobytes(), arc.get(F.TEX_NAME % 0, F.H_COF),
                       len(frames), [0] * len(frames))
    flash_looks.size = (len(tiles), h)
    return ti, len(secs) - 1


FEET = 22   # X's feet below its origin (feet_line)


# The bosses' own hurt boxes {dx, w, h} (user decision 2026-11-02: the change's height and the changed hurt box follow
# the original fight, not the idle picture, which was 1.5 - 2.4 x taller): PS1 boss code's +0x54 box
# (re/decomp/decomp_us_bin0NN_v2.txt, data in bin 033 - 040); Yammark, Wolfang and Shark pick theirs from the picture,
# their usual one is taken (docs/boss_patterns.md). Turtloid standing is hurt only behind his front shell (96 tall);
# his whole width is kept.
ORIG_BOX = {1: (-8, 24, 48), 2: (-16, 32, 42), 3: (-20, 40, 44), 4: (-16, 32, 56),
            5: (-16, 32, 32), 6: (-48, 96, 96), 7: (-16, 32, 32), 8: (-10, 20, 32)}


# The room a change needs above the feet (user decision 2026-11-02, after the box above let Turtloid's picture sink
# into ceilings): his idle picture's height less ROOM_OVERLAP px it may overlap the terrain, at least his hurt box
ROOM_OVERLAP = 16


def picture_height(bo):
    """the boss's idle picture from its top down to the feet (px)"""
    ps, _, lift, _ = bo['poses'][bo['body']]
    return FEET - max(-128, min(y - lift for _, _, _, y in ps))


def room_of(k, bo):
    return max(ORIG_BOX[k][2], picture_height(bo) - ROOM_OVERLAP)


def box_of(k):
    """hurt box {dx, dy, w, h} of boss k changed: his own box standing on Axl's feet (signed / unsigned bytes)"""
    dx, w, h = ORIG_BOX[k]
    return dx, FEET - h, w, h


def header(bosses, slots, fx_slot, menu_pals, flash_slot):
    L = ['/* generated by mods/axl/tools/lc2_dna.py: the DNA Change looks (pl02*.arc); do not edit */',
         '/* (dna_seq, dna_look_t: lc2/axl_data.h) */',
         '#define DNA_SEQ_MAX %d' % SEQ_MAX,
         '#define DNA_HOLD %d   /* dna_seq.at of move / walk / jump / fall / land: the last entry held, else the loop start */' % HOLD,
         'static const dna_look_t dna_looks[9] = {', '    {0},']
    for bo, (ti, ps, pal, _) in zip(bosses, slots):
        def seq(q, at=0):
            s = bo['seqs'][q]
            assert len(s) <= SEQ_MAX, (bo['name'], q, len(s))
            return '{%d, %d, {%s}, {%s}}' % (len(s), at, ', '.join(str(min(d, 255)) for d, _ in s) or '0',
                                             ', '.join(str(p) for _, p in s) or '0')
        assert len(bo['poses']) <= 64
        lift = ', '.join(str(l) for _, _, l, _ in bo['poses'])
        push = ', '.join(str(d) for _, _, _, d in bo['poses'])
        L.append('    {%d, %d, %d, %s, %s, %s, %s, %s, %s, %s, %s, {%s}, {%s}, %s},   /* %d %s: %d poses */' % (
            ti, ps, pal, seq('idle'), seq('fire'), seq('atk', bo['atk_at']), seq('move', bo['loops']['move']), seq('alt', bo['loops']['alt']),
            ', '.join(seq(q, bo['loops'][q]) for q in MOTIONS), ', '.join(seq(q, HOLD) for q in EXTRAS),
            ', '.join(seq(q, 0 if q == 'idle2' else HOLD) for q in ALTS), lift, push, seq('toss', HOLD),
            bosses.index(bo) + 1, bo['name'],
            len(bo['poses'])))
    # boss-size collision (2026-10-26): the hurt box {dx, dy, w, h} of the idle pose (X's: -10, -17, 19 x 38, the feet
    # at +22) and the height above the feet the change needs
    L += ['};', '/* boss k: his own hurt box (as in the original fight, on the feet at +%d as X) and its height above the feet */' % FEET,
          'static const signed char dna_box[9][4] = {{0}, ' + ', '.join('{%d, %d, %d, %d}' % box_of(k) for k in range(1, 9)) + '};',
          'static const unsigned char dna_height[9] = {0, ' + ', '.join(str(FEET - box_of(k)[1]) for k in range(1, 9)) + '};',
          '/* boss k: the room his change needs above the feet (his picture less %d px of overlap, at least dna_height) */'
          % ROOM_OVERLAP,
          'static const unsigned char dna_room[9] = {0, ' + ', '.join(str(min(255, room_of(k, bo))) for k, bo in
                                                                       enumerate(bosses, 1)) + '};',
          "/* boss k's texture in the weapon objects' list (com00's): projectiles drawn as the boss (afterimages) */",
          'static const unsigned char dna_ghost_tex[9] = {0, ' + ', '.join(str(s[3]) for s in slots) + '};']
    L += ['/* the bosses\' palettes (4 x 16 colours each), uploaded to CLUT slots DNA_PAL_SLOT.. at the change */',
          '#define DNA_PAL_SLOT %d' % PAL_SLOT,
          "/* the bosses' projectile pictures: texture index, parts section (frames = Axl's FX_* anims) */",
          '#define FX_TEX_INDEX %d' % fx_slot[0], '#define FX_PAT %d' % fx_slot[1],
          "/* Axl's bullet turned in SHOT_DIRS directions: frame SHOT_BASE + SHOT_NFLY * d + i = X's shot frame 5 + i */",
          '#define SHOT_DIRS %d' % SHOT_DIRS, '#define SHOT_BASE %d' % SHOT_BASE, '#define SHOT_NFLY %d' % len(SHOT_FLY),
          "/* Axl's muzzle flash turned in FLASH_DIRS directions (flash_looks): frame FLASH_NF * d + i = X's flash frame i */",
          '#define FLASH_TEX_INDEX %d' % flash_slot[0], '#define FLASH_PAT %d' % flash_slot[1],
          '#define FLASH_DIRS %d' % FLASH_DIRS, '#define FLASH_NF %d' % len(FLASH_SRC),
          "/* the DNA menu badges (frame ICON_BASE + i: 0 Axl, k boss k); badge i's palettes at slots MENU_PAL_SLOT +",
          " * MENU_PALS * i ..: MENU_LEVELS by depth (far dark .. near bright), then selected */",
          '#define ICON_BASE %d' % ICON_BASE, '#define MENU_PAL_SLOT %d' % MENU_PAL_SLOT,
          "/* Giga Crush pictures (lc2_dna.giga_pictures): GIGA_BASE + a = the gun at a * 45 degrees, then the burst */",
          '#define GIGA_BASE %d' % GIGA_BASE, '#define GIGA_SPIN %d' % GIGA_SPIN, '#define GIGA_BURST %d' % len(GIGA_BURST),
          'static const unsigned short giga_pal[16] = {%s};' % ', '.join('0x%04x' % v for v in fx_looks.giga_pal),
          "/* the hover flames (lc2_dna.flame_pictures): FLAME_BASE + i, nozzle on the origin; per body frame the 2 nozzles",
          " * (one per boot, data coordinates facing left; PS1 work/build/flame_spots.json) */",
          '#define FLAME_BASE %d' % FLAME_BASE, '#define N_FLAMES %d' % N_FLAMES,
          "/* the copy shot (lc2_dna.copy_pictures; colours in flame_pal): the ring at COPY_RADII sizes (COPY_N_RING),",
          " * the small bullet, the trail segments (COPY_TRAIL + COPY_FADE * direction + fade) */",
          '#define COPY_BASE %d' % COPY_BASE, '#define COPY_N_RING %d' % len(COPY_RADII),
          '#define COPY_ORB %d' % COPY_ORB, '#define COPY_TRAIL %d' % COPY_TRAIL, '#define COPY_FADE %d' % COPY_FADE,
          '#define N_COPY %d' % N_COPY,
          "/* Wolfang's floor spike (lc2_dna.spike_pictures): frames SPIKE_BASE + i, anim 0x1F as (duration, i), last held */",
          '#define SPIKE_BASE %d' % SPIKE_BASE, '#define SPIKE_SEQ_N %d' % len(SPIKE_SEQ),
          '#define ICICLE_BASE %d   /* + 0 the burst, + 1 the spike, upside down */' % ICICLE_BASE,
          '#define WALL_SPIKE_BASE %d   /* + 0 the burst, + 1 the spike, pointing left (mirrored facing right) */' % WALL_SPIKE_BASE,
          'static const unsigned char spike_seq[%d][2] = {%s};' % (len(SPIKE_SEQ), ', '.join('{%d, %d}' % e for e in SPIKE_SEQ)),
          'static const unsigned short flame_pal[16] = {%s};' % ', '.join('0x%04x' % v for v in fx_looks.flame_pal),
          'static const unsigned char flame_frames[%d] = {%s};' % (len(flame_spots()), ', '.join(str(f) for f in sorted(flame_spots()))),
          'static const signed char flame_spot[%d][2][2] = {%s};' % (len(flame_spots()), ', '.join(
              '{{%d, %d}, {%d, %d}}' % tuple(c for sp in flame_spots()[f] for c in sp[:2]) for f in sorted(flame_spots()))),
          '#define MENU_LEVELS %d' % len(MENU_LEVELS), '#define MENU_PALS %d' % (len(MENU_LEVELS) + 1),
          'static const unsigned short menu_pals[%d][16] = {%s};' % (9 * (len(MENU_LEVELS) + 1), ', '.join(
              '{%s}' % ', '.join('0x%04x' % v for v in p) for ps in menu_pals for p in ps)),
          'static const unsigned short dna_pals[9][64] = {', '    {0},']
    for bo in bosses:
        L.append('    {' + ', '.join('0x%04x' % v for v in bo['pals'].reshape(-1)) + '},')
    L += ['};', '']
    os.makedirs(F.GEN, exist_ok=True)
    open(os.path.join(F.GEN, 'axl_dna_looks.h'), 'w').write('\n'.join(L))


if __name__ == '__main__':
    bs = all_bosses()
    for k, b in enumerate(bs, 1):
        print(k, b['name'], 'poses', len(b['poses']), 'tiles max', max(len(t) for _, t, _ in b['poses']),
              'lift max', max(p[2] for p in b['poses']), {q: len(v) for q, v in b['seqs'].items()})
