"""The 8 bosses' looks for DNA Change: their own poses from the boss data, cut into X-style frames.

Sources (US v1.1 ROCK_X6.DAT, mods/axl/docs/feasibility.md sections 10-11; checked against the rematch dumps of
emu/plans/common/plan_yam_warp.lua: boss object +0x3c = parts table 0, +0x40 tile base, +0x42 = CLUT 0x7948):
  texture   boss DAT subfile 0: strips of 64 x 160 halfwords (4bpp) loaded side by side at VRAM (x0, 16)
  parts     boss DAT subfile 1 (type 0x17): {u32 offset}[tables], table 0 = the boss: {u16 count, u16 offset/4}[],
            part = (attr, tile, x, y); attr & 3 = tile high bits, (attr >> 2) & 3 = palette, 0x40 / 0x80 = h / v flip
  tiles     the game's enemy drawing (FUN_800232D4, flag 0x40 path): t = tile + base, t += (t // 0xa0) * 0x60 + 0x610,
            tpage = t >> 8, u = (t & 15) * 16, v = t & 0xf0
  palettes  4 x 16 colors in a stage DAT (the same as the boss room's VRAM CLUT 0x7948-0x794b)
  animation the boss's own scripts (rematch RAM, obj+0x30): (duration, frame) loops for standing / attacking
Each pose is re-tiled on a 16 px grid per palette for X's drawing in both facings (retile; <= 32 tiles) with the bottom of all its poses on
X's feet. Data faces left like X's. Turtloid (about 150 px, up to 78 tiles) is scaled to 0.55 first.
"""
import json
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import datpak  # noqa: E402

MAX_TILES = 32
C = 256
# weapon k: boss DAT, VRAM x of the texture, tile base (+0x40), palettes (DAT entry, offset), scale,
# standing loop and attacking loop as (duration, boss frame) from the boss's animation table; move / alt (LC2 only,
# 2026-10-27, decoded from the overlays' anim tables, base 0x800FA000): the dash-button move (looped) and the
# DNA-button second attack (played once, last picture held), axl_moves.c
TABLE1 = 1000   # boss frame numbers TABLE1 + n: picture n of parts table 1
COMBO = 900     # boss frame numbers COMBO + n: cfg['combos'][n] = (pictures drawn together, back first)
HOLD = 255      # *_loop: the last entry held (lc2_dna.HOLD)
SHELL_SPIN = tuple((3, TABLE1 + i) for i in range(16))   # Turtloid's anim 0x1E: the spiked shell turning
BOSSES = {
    1: dict(name='Yammark', dat=75, x0=576, base=0x1e0, pal=(3, 0x1380), scale=1,
            idle=((1, 94), (1, 95), (1, 96)),                       # anim 3 (facing out, wings beating): standing still
            # PC (lc2_dna): his own standing look (user request 2026-10-29) = anim 0's body (0-2, 30 frames each) under
            # the wings object's anim 14 (22 / 24 / 25, a frame each), drawn together: combined pictures COMBO + n
            combos={COMBO + 3 * b + w: (wing, body) for b, body in enumerate((0, 1, 2))
                    for w, wing in enumerate((22, 24, 25))},
            idle_pc=tuple((1, COMBO + 3 * b + w) for b in range(3) for _ in range(10) for w in range(3)),
            body=26,                                                # (the hurt box / height: the side view, as before)
                                                                    # (user 2026-10-27: still and moving told apart)
            fire=(),                                                # the boss has no shooting motion (options fire)
            atk=((10, 34), (10, 36), (6, 35), (6, 100), (6, 101), (1, 37)), atk_at=0,   # anim 5: option summon
            walk=((4, 3), (1, 26), (1, 28), (1, 28)), walk_loop=1,    # anim 2: forward flight
            jump=((1, 94), (1, 95), (1, 96)), jump_loop=0,            # anim 3: flying up
            fall=((1, 97), (1, 98), (1, 99)), fall_loop=0,            # anim 4: flying down
            # PC: the change of formation (axl_dna_bosses.c, user request 2026-11-10): arms up, then pointing ahead
            toss_pc=tuple((3, f) for f in (94, 95, 96, 97, 98, 99, 102, 103, 104, 105, 106, 107, 108, 109))),
    2: dict(name='Wolfang', dat=76, x0=512, base=0x140, pal=(5, 0x1300), scale=1,
            idle=((14, 30), (14, 31), (14, 32), (14, 31)),          # anim 0
            fire=(),
            # anim 0xD (ice chunk throw), the 46-frame hold while the chunk forms cut to 12; thrown at picture 21
            atk=((9, 18), (9, 19), (12, 20), (10, 21), (20, 20), (9, 19), (9, 18)), atk_at=30,
            move=((6, 17), (1, 25)), move_loop=HOLD,                # anim 0x10: ice-spike dash (sub-state 8), 25 held
            walk=((5, 0), (5, 1), (5, 2), (5, 3), (5, 4), (5, 5)),  # anim 2's run loop (sub-state 5)
            jump=((3, 13), (1, 6)), fall=((8, 0), (1, 6)), land=((1, 13),),   # sub-state 6: 4 crouch, 5 up, 6 apex, 5 down, 4
            # walls and ceiling (axl_moves.c wolf_*): 0xB wall cling, 0x14 the hang, 0x13 the jump up, 0xF the ceiling
            # shots (upside down), 0x11 the ceiling dash, 10 the leap
            cling=((6, 92), (1, 23)), ceil=((6, 15), (1, 91)), cjump=((6, 13), (1, 14)),
            cshot=((9, 26), (9, 27), (8, 29), (8, 29), (8, 29), (14, 29), (4, 28), (4, 29), (4, 28), (4, 29), (20, 28),
                   (9, 27), (9, 26)),
            cdash=((6, 97), (1, 96)), leap=((1, 25),),
            drop={26: 12, 27: 12, 28: 12, 29: 12},              # (the ceiling shot pictures' top on 91's: on the ceiling)
            alt=((9, 18), (9, 19), (12, 20), (10, 21), (9, 19))),   # anim 0xD throw (0xF's 26-29 hang upside down)
    3: dict(name='Heatnix', dat=77, x0=512, base=0x140, base1=0x440, pal=(7, 0x1300), scale=1,
            idle=((11, 2), (10, 1), (9, 0), (9, 3)),                # anim 0
            fire=(),
            atk=((14, 27), (3, 28), (1, 29), (4, 30), (11, 31), (1, 31)), atk_at=18,   # anim 6: straight shot
            # the firebird (sub 8, user request 2026-10-27): table 0x800FE858 anim 4 = parts table 1 pictures 15-17
            move=((10, TABLE1 + 15), (10, TABLE1 + 16), (10, TABLE1 + 17)),
            # (its head was 120 px ahead of him - user report 2026-10-28: moved 100 px back, the head now just ahead;
            # the tail's last 100 px past the parts' byte range cut off)
            shift={TABLE1 + 15: 100, TABLE1 + 16: 100, TABLE1 + 17: 100},
            alt=((10, 21), (14, 22), (3, 23), (1, 24), (2, 25), (15, 26)),   # anim 5: the thrown fireball (sub 4, t=26)
            walk=((11, 11), (8, 14), (1, 12), (9, 12), (9, 13)), walk_loop=1,   # anim 2: level flight
            jump=((3, 15), (3, 16)), jump_loop=0,                   # anim 3: flying up
            fall=((10, 19), (8, 20), (1, 17), (9, 17), (9, 18)), fall_loop=1),  # anim 4: flying down
    4: dict(name='Shark', dat=78, x0=512, base=0x140, pal=(11, 0x1300), scale=1,
            idle=((24, 0), (19, 2), (20, 4)),                       # anim 2, every other picture (RAM)
            fire=(),
            atk=((4, 10), (8, 11), (4, 12), (5, 13), (11, 13)), atk_at=21,   # anim 8: anchor (at the jump's top)
            # walking: the prone fin (sub 4, anim 0x26's loop; user request 2026-10-27); the dash: the junk swim (sub 7:
            # anim 0x20 head-first dive, then 0x21 the fin only out of the floor)
            walk=((3, 20), (3, 21), (3, 22), (3, 23), (3, 24), (3, 25), (3, 26)),
            move=((4, 101), (4, 102), (4, 103), (4, 104), (6, 28), (6, 29), (6, 30), (6, 29)), move_loop=4,
            drop={**{f: 28 for f in range(19, 27)},                # (the fin lies prone: y + 0x1C, BIN 36 FD0FC)
                  **{f: 42 for f in (28, 29, 30)}},               # (the swim: y + 0x20, FDA14; 10 more: its base in the floor)
            jump=((3, 6), (3, 7), (1, 8)),                          # anim 3 crouch (shortened), 4 rising
            fall=((6, 8), (6, 9)), fall_loop=0, land=((4, 7), (9, 6)),   # anim 6 falling, 7 landing
            # sub 6 leap and pound (axl_moves.c): anim 10 crouch / leap, 11 hover, 12 the pound landing
            alt=((4, 6), (6, 7), (14, 8), (2, 9), (4, 14), (4, 15), (3, 16), (13, 17), (6, 14), (5, 27), (90, 27))),
    5: dict(name='Scaravich', dat=79, x0=512, base=0x140, pal=(13, 0x1300), scale=1,
            idle=((11, 0), (17, 1), (14, 0), (14, 2), (16, 3), (11, 2)),  # anim 0
            fire=(),
            atk=((4, 0), (14, 13), (3, 14), (2, 15), (3, 16), (4, 17), (8, 18)), atk_at=23,   # anim 3: kick
            move=((5, 19), (5, 20), (5, 21), (5, 22), (5, 23), (5, 24)),   # anim 4: rolled-up charge (sub-state 7)
            alt=((4, 0), (4, 8), (3, 9), (2, 10), (3, 11), (4, 12), (4, 8)),   # anim 2: small balls
            walk=((2, 0), (6, 6), (8, 4), (6, 6), (2, 0), (6, 7), (8, 5), (6, 7)),   # anim 1: walking
            # no jump: anim 5 (the knock-up hop) without its flash pictures 26 / 27
            jump=((1, 25),), fall=((1, 25),)),
    6: dict(name='Turtloid', dat=80, x0=512, base=0x140, pal=(15, 0x1300), scale=0.55,
            idle=((22, 16), (24, 17), (26, 18), (20, 19), (22, 20), (24, 21)),  # anim 10 (full size)
            fire=((4, 32), (4, 33), (250, 34)),                     # anim 13: into the shell, held
            # anim 0x1C (into the shell), then 0x1E (the spiked shell turning), both parts table 1
            move=((2, TABLE1 + 17), (2, TABLE1 + 16), (1, TABLE1 + 0), (2, TABLE1 + 38), (3, TABLE1 + 37), (2, TABLE1 + 38))
                 + SHELL_SPIN, move_loop=6,
            jump=SHELL_SPIN, jump_loop=0, fall=SHELL_SPIN, fall_loop=0,   # airborne: in the shell (0x1E)
            alt=SHELL_SPIN, alt_loop=0),                          # the bounce: the spiked shell turning
    7: dict(name='Sheldon', dat=81, x0=512, base=0x140, base1=0x540, pal=(17, 0x1300), scale=1,   # (base1: the thrown
            # shell's parts table 1, BIN 39 FUN_800FCAEC +0x40 = 0x540)
            idle=((14, 134), (14, 146), (14, 147), (14, 148)),      # anim 6: open, the shell held (his resting pose)
            fire=(),
            # anim 0x17: plates summoned (at the change), they appear at t = 71
            atk=((6, 95), (6, 96), (6, 97), (6, 98), (6, 99), (6, 100), (6, 101), (6, 102), (6, 103), (6, 104),
                 (6, 105), (6, 106)), atk_at=71,
            # the teleport (axl_moves.c move 7: out 12 frames, the warp, in 12): anim 0xD out / 0xC in, shortened
            move=((2, 7), (2, 6), (2, 1), (1, 47), (1, 49), (1, 51), (1, 53), (1, 55), (1, 57),
                  (1, 57), (1, 55), (1, 53), (1, 51), (1, 49), (1, 47), (2, 1), (2, 6), (2, 7)), move_loop=HOLD,
            # the shell in his hands is the plates (user report 2026-10-28): without them he holds it in the air too
            # (154 / 155: the dash pictures with the shell), with them up (dna_alt) none - idle 149-151, 153 / 152
            jump=((1, 154),), fall=((1, 155),),
            idle2=((14, 149), (14, 150), (14, 151), (14, 150)), jump2=((1, 153),), fall2=((1, 152),),
            # the shell thrown (user request 2026-10-29: his shot; original pattern 4, anim 0x15: the shell goes at
            # t = 12, axl_dna_bosses.c S_TOSS_AT); while it is out he has none in his hands (idle2 / jump2 / fall2)
            # anim 0x15 (shell 1 at t = 12), then 0x16 (shell 2 at its t = 16): BIN 39 pattern 4 steps 2-4
            toss=((6, 10), (6, 11), (2, 12), (12, 13), (10, 15), (6, 16), (2, 17), (20, 18)),
            # (the shell-less pictures' body 2.4 px right of the shell-held ones': he moved as the plates came up - user
            # report 2026-10-28; body centres 1.1 / 3.5 px)
            shift={149: -2, 150: -2, 151: -2, 152: -2, 153: -2},
            feet_from=('idle',)),   # (the plates of the summon reach 17 px below his feet: from them he stood 17 px
                                    # up in the air - 2026-10-28)
    8: dict(name='Mijinion', dat=82, x0=512, base=0x140, pal=(19, 0x1380), scale=1,
            idle=((3, 3), (4, 2), (4, 1), (3, 0)),                  # anim 0
            fire=(),
            atk=((5, 8), (6, 9), (16, 10), (2, 11), (2, 12), (3, 13), (3, 14), (2, 15), (3, 16), (8, 17), (6, 39)),
            atk_at=31,                                              # anim 2: Ray Arrow
            move=((9, 18), (7, 19), (6, 20)), move_loop=1,          # anim 3: the drop (19-20 loop)
            fall=((9, 18), (7, 19), (6, 20)), fall_loop=1,          # (flying down: the drop's fall)
            land=((6, 18), (10, 21), (11, 22), (9, 23), (8, 24), (6, 31)),   # anim 4 shortened: the drop's landing
            alt=((12, 39), (6, 49), (6, 50), (20, 51), (8, 52), (8, 53), (13, 54), (8, 55), (30, 56))),   # anim 7: clones
}
SEQ_MAX = 24


def entry(dat, k):
    sec, size = struct.unpack_from('<II', dat, 8 * k)
    return dat[sec * 2048:sec * 2048 + size]


class Boss:
    def __init__(self, dat, cfg):
        e = entry(dat, cfg['dat'])
        subs = datpak.parse(e)
        t, o, z = subs[0]
        d = np.frombuffer(e[o:o + z], '<u2')
        self.vram = np.zeros((512, 1024), np.uint16)
        n = 64 * 160
        for i in range(len(d) // n + (len(d) % n != 0)):
            s = d[i * n:(i + 1) * n]
            rows = len(s) // 64
            x = cfg['x0'] + 64 * i
            if x >= 1024:
                break
            self.vram[16:16 + rows, x:x + 64] = s[:rows * 64].reshape(rows, 64)
        t, o, z = subs[1]
        p = e[o:o + z]
        self.pt = p[struct.unpack_from('<I', p, 0)[0]:]   # first offset = table 0
        self.pt1 = p[struct.unpack_from('<I', p, 4)[0]:]  # table 1 (Turtloid's shell: anims 0x1C-0x20), frame TABLE1 + n
        self.base = cfg['base']
        self.combos = cfg.get('combos', {})
        self.base1 = cfg.get('base1', cfg['base'])   # parts table 1's tile base (Heatnix 0x440: +0x40 += 0x300, BIN 35 FB708)
        pe, po = cfg['pal']
        self.pals = np.frombuffer(entry(dat, pe)[po:po + 128], '<u2').reshape(4, 16)

    def parts(self, f):
        if COMBO <= f < TABLE1:   # (two pictures as one: the first behind)
            return [q for g in self.combos[f] for q in self.parts(g)]
        pt = self.pt1 if f >= TABLE1 else self.pt
        f %= TABLE1
        cnt, off4 = struct.unpack_from('<HH', pt, 4 * f)
        return [struct.unpack_from('<BBbb', pt, off4 * 4 + 4 * i) for i in range(cnt)]

    def tile(self, attr, lo, f=0):
        t = ((lo | (attr & 3) << 8) + (self.base1 if f >= TABLE1 else self.base)) & 0xffff
        t = (t + (t // 0xa0) * 0x60 + 0x610) & 0xffff
        tp = t >> 8
        bx, by = (tp & 0xf) * 64 + (t & 0xf) * 4, (tp >> 4 & 1) * 256 + (t & 0xf0)
        w = self.vram[by:by + 16, bx:bx + 4]
        px = np.zeros((16, 16), np.int16)
        for k in range(4):
            for n in range(4):
                px[:, k * 4 + n] = (w[:, k] >> (4 * n)) & 15
        return px

    def compose(self, f):
        """Index image: 0 transparent, 16 * palette + color; later parts on top (list order, as the game draws:
        Yammark's staff and wings behind the body, rematch screens; feasibility.md §13)."""
        img = np.zeros((2 * C, 2 * C), np.int16)
        for a, lo, x, y in self.parts(f):
            if a & 0x20:
                continue
            px = self.tile(a, lo, f)
            if a & 0x40:
                px = px[:, ::-1]
            if a & 0x80:
                px = px[::-1]
            m = px != 0
            img[C + y:C + y + 16, C + x:C + x + 16][m] = px[m] + 16 * ((a >> 2) & 3)
        return img


def scaled(img, s):
    """Nearest-neighbour scale about the object origin (C, C)."""
    if s == 1:
        return img
    yy, xx = np.mgrid[0:2 * C, 0:2 * C]
    sy = np.clip(np.round((yy - C) / s).astype(int) + C, 0, 2 * C - 1)
    sx = np.clip(np.round((xx - C) / s).astype(int) + C, 0, 2 * C - 1)
    return img[sy, sx]


def mirrored_view(tiles, parts):
    """The picture as X's drawing shows it facing right (mirrored, in data orientation): column j of a part shows
    texel j - 1 of its 16-tile VRAM row (u wraps inside the row), so the whole picture moves 1 px and each part's
    first column comes from the VRAM tile before it (seen: Wolfang's leg seam, emu/out/axlfight_fx2_255)."""
    vr = np.zeros((32, 256), np.int16)
    for i, t in enumerate(tiles):
        vr[(i // 16) * 16:(i // 16) * 16 + 16, (i % 16) * 16:(i % 16) * 16 + 16] = t
    img = np.zeros((2 * C, 2 * C), np.int16)
    for layer, t, x, y in parts:
        us = [((t % 16) * 16 + j - 1) % 256 for j in range(16)]
        px = vr[(t // 16) * 16:(t // 16) * 16 + 16][:, us]
        m = px != 0
        img[C + y:C + y + 16, C + x:C + x + 16][m] = px[m] + 16 * layer
    return img


def retile(img, max_tiles=MAX_TILES, prefix=()):
    """Cut on a 16 px grid per palette into (tiles, parts [(palette, tile, x, y)]) laid out for both facings: the
    tiles of each horizontal run are consecutive in VRAM (the mirrored drawing takes each part's first column from
    the tile before it), an empty spacer follows a run whose last column is drawn (plus a part on it, which then
    shows that column), runs do not cross the VRAM row, and unused tiles are empty (frames are padded to 32 tiles).
    Of the 256 grid offsets, the one with the fewest wrong pixels in the mirrored view, then the fewest tiles.
    `prefix`: tiles already in VRAM before these (a projectile art frame holds several pictures); returns the new
    tiles only, parts number the tiles from the start of prefix."""
    ys, xs = np.nonzero(img)
    ref = np.roll(img, 1, axis=1)
    empty = np.zeros((16, 16), np.uint8)
    best = None
    for oy in range(16):
        for ox in range(16):
            y0, x0 = ys.min() - oy, xs.min() - ox
            runs = []
            for layer in sorted({int(v) for v in np.unique(img[img > 0] // 16)}):
                lay = np.where((img > 0) & (img // 16 == layer), img % 16, 0)
                for ty in range(y0, ys.max() + 1, 16):
                    run = []
                    for tx in range(x0, xs.max() + 17, 16):
                        t = lay[ty:ty + 16, tx:tx + 16]
                        if t.shape == (16, 16) and t.any():
                            run.append((t.astype(np.uint8), tx - C, ty - C))
                        elif run:
                            runs.append((layer, run))
                            run = []
            tiles, parts = list(prefix), []
            for layer, run in runs:
                need = len(run) + (1 if run[-1][0][:, 15].any() else 0)
                if len(tiles) // 16 != (len(tiles) + need - 1) // 16:
                    tiles += [empty] * (16 - len(tiles) % 16)   # keep the run in one VRAM row
                for t, x, y in run:
                    parts.append((layer, len(tiles), x, y))
                    tiles.append(t)
                if run[-1][0][:, 15].any():
                    parts.append((layer, len(tiles), run[-1][1] + 16, run[-1][2]))
                    tiles.append(empty)
            if len(tiles) > max_tiles:
                continue
            err = int((mirrored_view(tiles, parts) != ref).sum())
            key = (err, len(tiles))
            if best is None or key < best[0]:
                best = (key, tiles, parts)
    assert best is not None, 'no layout within %d tiles' % max_tiles
    assert best[0][0] == 0, 'mirrored view off by %d px' % best[0][0]
    return best[1][len(prefix):], best[2]


EDITS = os.path.join(HERE, '..', 'work', 'sheet', 'boss_edits.json')


def edits():
    """viewer edits of the DNA change pictures (tools/make_viewer.py boss tab): work/sheet/boss_edits.json =
    {looks: {k: {idle: [[dur, boss frame]], fire: [...], scale}}, nudge: {"k:frame": [dx, dy]} (data coordinates)}"""
    if not os.path.exists(EDITS):
        return {'looks': {}, 'nudge': {}}
    e = json.load(open(EDITS))
    return {'looks': e.get('looks', {}), 'nudge': e.get('nudge', {})}


EDIT_SEQS = ('idle', 'fire', 'atk', 'alt', 'move')   # the viewer's editable sequences (2026-10-29: + attack, second
                                                     # attack, dash - picked from the boss's own anims or by hand)


def config(k, ed=None):
    """BOSSES[k] with the viewer edits applied (EDIT_SEQS loops, scale)"""
    ed = edits() if ed is None else ed
    cfg = dict(BOSSES[k])
    for key, v in ed['looks'].get(str(k), {}).items():
        cfg[key] = tuple(tuple(e) for e in v) if key in EDIT_SEQS else v
    return cfg


def bottom(imgs):
    """lowest opaque row over the pictures, relative to the origin"""
    return max(int(np.nonzero(im)[0].max()) for im in imgs) - C


def grid_cells(img, oy, ox):
    """16 px cells per palette of an index image on the grid offset (oy, ox) from the picture's top-left:
    [(palette, tile pixels, x, y)] in data coordinates"""
    ys, xs = np.nonzero(img)
    y0, x0 = ys.min() - oy, xs.min() - ox
    out = []
    for layer in sorted({int(v) for v in np.unique(img[img > 0] // 16)}):
        lay = np.where((img > 0) & (img // 16 == layer), img % 16, 0)
        for ty in range(y0, ys.max() + 1, 16):
            for tx in range(x0, xs.max() + 1, 16):
                t = lay[ty:ty + 16, tx:tx + 16]
                if t.any():
                    out.append((layer, t.astype(np.uint8), tx - C, ty - C))
    return out


def tile_count(img):
    """fewest 16 px cells (per palette) over the 256 grid offsets = tiles of a plain cut"""
    ys, xs = np.nonzero(img)
    if not len(ys):
        return 0
    box = img[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
    best = None
    layers = [box // 16 == v for v in np.unique(box[box > 0] // 16)]
    layers = [(box > 0) & m for m in layers]
    for oy in range(16):
        for ox in range(16):
            n = 0
            for m in layers:
                pad = np.zeros(((m.shape[0] + oy + 15) // 16 * 16, (m.shape[1] + ox + 15) // 16 * 16), bool)
                pad[oy:oy + m.shape[0], ox:ox + m.shape[1]] = m
                n += int(pad.reshape(pad.shape[0] // 16, 16, pad.shape[1] // 16, 16).any(axis=(1, 3)).sum())
            best = n if best is None else min(best, n)
    return best


def plain_cut(img):
    """(tiles, parts) of the plain cut with the fewest tiles, padded to MAX_TILES with empty tiles"""
    best = None
    for oy in range(16):
        for ox in range(16):
            cells = grid_cells(img, oy, ox)
            if best is None or len(cells) < len(best):
                best = cells
    tiles = [t for _, t, _, _ in best]
    parts = [(layer, i, x, y) for i, (layer, _, x, y) in enumerate(best)]
    return tiles, parts


# Poses drawn at full size (no scale) in slices of <= MAX_TILES tiles: the C code re-uploads X's 32-tile VRAM area
# inside the frame's draw chain before each slice (axl_dna.c full_draw). {weapon: poses}. Their scaled frame is left
# empty (RAM). Turtloid: idle anim 10 (16-21), shell spin 32-34.
FULL = {6: (16, 17, 18, 19, 20, 21, 32, 33, 34)}
FULL_MAP = []   # [(look frame in the section, first slice frame, slices, lift)], set by frames()


def slices(img):
    """plain cut of a full-size picture split in order into chunks of MAX_TILES tiles: [(tiles, parts)] with the
    parts' tile numbers local to their chunk (chunks keep the list order, so the overlap order stays)"""
    tiles, ps = plain_cut(img)
    out = []
    for c in range(0, len(tiles), MAX_TILES):
        ch = tiles[c:c + MAX_TILES]
        ch = ch + [np.zeros((16, 16), np.uint8)] * (MAX_TILES - len(ch))
        out.append((ch, [(layer, ti - c, x, y) for layer, ti, x, y in ps if c <= ti < c + MAX_TILES]))
    return out


def frames(dat, feet_y):
    """All bosses' frames for one tile / parts section: (tiles per frame, parts per frame, palettes [9][64],
    sequences {k: (idle [(dur, frame)], fire [(dur, frame)])} with frame = index in the section)."""
    all_tiles, all_parts, pals, seqs = [], [], np.zeros((9, 64), np.uint16), {}
    ed = edits()
    FULL_MAP.clear()
    for k in sorted(BOSSES):
        cfg = config(k, ed)
        b = Boss(dat, cfg)
        pals[k] = b.pals.reshape(-1)
        used = sorted({f for _, f in cfg['idle'] + cfg['fire']})
        imgs = {f: scaled(b.compose(f), cfg['scale']) for f in used}
        dy = feet_y - bottom(imgs.values())
        index = {}
        for f in used:
            if f in FULL.get(k, ()):
                index[f] = len(all_tiles)   # drawn from its full-size slices: no scaled picture
                all_tiles.append([])
                all_parts.append([])
                continue
            try:
                tiles, ps = retile(imgs[f])
            except AssertionError:
                # the run layout of retile needs more tiles; the game mirrors exactly (§13), so a plain cut is enough
                tiles, ps = plain_cut(imgs[f])
                assert len(tiles) <= MAX_TILES, '%s 그림 %d: %d타일 (한도 %d) - 뷰어에서 그림이나 크기를 바꾸세요' % (
                    cfg['name'], f, len(tiles), MAX_TILES)
            tiles = tiles + [np.zeros((16, 16), np.uint8)] * (MAX_TILES - len(tiles))   # no stale tiles in VRAM
            nx, ny = ed['nudge'].get('%d:%d' % (k, f), (0, 0))
            parts = []
            for layer, ti, x, y in ps:
                x, y = x + nx, y + dy + ny
                assert -128 <= x <= 127 and -128 <= y <= 127, (cfg['name'], f, x, y)
                parts.append((ti >> 8 | layer << 2, ti & 0xff, x, y))
            index[f] = len(all_tiles)
            all_tiles.append(tiles)
            all_parts.append(parts)
        # full-size poses: slices appended after the boss's frames; their own feet line (they are taller)
        full = [f for f in FULL.get(k, ()) if f in used]
        if full:
            big = {f: b.compose(f) for f in full}
            for f in full:
                fdy = feet_y - bottom([big[f]])   # each on the feet line (the shell spin reaches lower than idle)
                first = len(all_tiles)
                nx, ny = ed['nudge'].get('%d:%d' % (k, f), (0, 0))
                sl = slices(big[f])
                # parts are signed bytes: a picture taller than that is stored `lift` px lower and drawn with the
                # object moved up by as much (axl_dna.c full_draw)
                top = min(y for _, ps in sl for _, _, _, y in ps) + fdy + ny
                lift = max(0, -128 - top)
                for tiles, ps in sl:
                    parts = []
                    for layer, ti, x, y in ps:
                        x, y = x + nx, y + fdy + ny + lift
                        assert -128 <= x <= 127 and -128 <= y <= 127, (cfg['name'], f, x, y)
                        parts.append((ti >> 8 | layer << 2, ti & 0xff, x, y))
                    all_tiles.append(tiles)
                    all_parts.append(parts)
                FULL_MAP.append((index[f], first, len(all_tiles) - first, lift))
        assert len(cfg['idle']) <= SEQ_MAX and len(cfg['fire']) <= SEQ_MAX
        seqs[k] = tuple([(d, index[f]) for d, f in cfg[s]] for s in ('idle', 'fire'))
    assert len(all_tiles) <= 256
    return all_tiles, all_parts, pals, seqs


# ---- the looks as the bosses' own tiles (no re-cut) ----------------------------------------------------------
# Each boss's poses are drawn with its own parts (flip bits, palettes, list order) and its own 16x16 tiles: the tiles
# the poses use are pooled per boss (each tile once, in order of first use) and stored in chunks of POOL_CHUNK tiles
# (one LZ stream each, LOOK_T2 frames). A pose is cut, in list order, into slices of parts that use <= MAX_TILES
# distinct tiles; for each slice axl_dna.c decompresses the chunks it needs, gathers its tiles into X's 32-tile VRAM
# area inside the draw chain and draws the slice's parts (LOOK_T3 frames, tile = slot). The slot map of a slice
# (LOOK_MAP frames): byte 0 = tile count, then per slot (chunk - boss's first chunk) << 4 | tile in the chunk.
POOL_CHUNK = 16
SLICE_MAX = 4
FEET_PER_POSE = (6,)   # Turtloid: each pose on the feet line (the shell spin reaches lower than standing)


def pool_frames(dat, feet_y):
    """-> dict(chunks [[tile]], parts [[(attr, tile, x, y)]] per slice, maps [bytes] per slice, pals [9][64],
    seqs {k: (idle, fire, atk)} as (duration, pose), poses [(first slice, slices, lift)], chunk0 [9],
    atk_at [9] = frame of the attack motion at which the shot comes out)"""
    ed = edits()
    out = dict(chunks=[], parts=[], maps=[], pals=np.zeros((9, 64), np.uint16), seqs={}, poses=[], chunk0=[0] * 9,
               atk_at=[0] * 9)
    for k in sorted(BOSSES):
        cfg = config(k, ed)
        if cfg.get('scale', 1) != 1:
            print('%s: scale %.2f ignored (own tiles are drawn at full size)' % (cfg['name'], cfg['scale']))
        b = Boss(dat, cfg)
        out['pals'][k] = b.pals.reshape(-1)
        seq = [f for _, f in cfg['idle'] + cfg['fire'] + cfg.get('atk', ())]
        used = list(dict.fromkeys(seq))
        parts = {f: [p for p in b.parts(f) if not p[0] & 0x20] for f in used}
        pool, pix = {}, []
        for f in used:
            for a, lo, x, y in parts[f]:
                t = lo | (a & 3) << 8
                if t not in pool:
                    pool[t] = len(pix)
                    pix.append(b.tile(a, lo).astype(np.uint8))
        nchunks = (len(pix) + POOL_CHUNK - 1) // POOL_CHUNK
        assert nchunks <= 16, (cfg['name'], 'pool of %d tiles' % len(pix))
        out['chunk0'][k] = len(out['chunks'])
        for c in range(0, len(pix), POOL_CHUNK):
            out['chunks'].append(pix[c:c + POOL_CHUNK])
        imgs = {f: b.compose(f) for f in used}
        dy_all = feet_y - bottom(imgs.values())
        index = {}
        for f in used:
            dy = feet_y - bottom([imgs[f]]) if k in FEET_PER_POSE else dy_all
            nx, ny = ed['nudge'].get('%d:%d' % (k, f), (0, 0))
            ys = [y + dy + ny for _, _, _, y in parts[f]]
            lift = max(0, -128 - min(ys))
            assert max(ys) + lift <= 127, (cfg['name'], f, 'too tall')
            # slices in list order, <= MAX_TILES distinct tiles each
            groups, cur = [], []
            for p in parts[f]:
                t = p[1] | (p[0] & 3) << 8
                if t not in {q[1] | (q[0] & 3) << 8 for q in cur} and \
                        len({q[1] | (q[0] & 3) << 8 for q in cur}) == MAX_TILES:
                    groups.append(cur)
                    cur = []
                cur.append(p)
            groups.append(cur)
            assert len(groups) <= SLICE_MAX, (cfg['name'], f, len(groups))
            first = len(out['parts'])
            for g in groups:
                ids = sorted({p[1] | (p[0] & 3) << 8 for p in g}, key=lambda t: pool[t])   # grouped by chunk
                slot = {t: i for i, t in enumerate(ids)}
                ps = []
                for a, lo, x, y in g:
                    x, y = x + nx, y + dy + ny + lift
                    assert -128 <= x <= 127 and -128 <= y <= 127, (cfg['name'], f, x, y)
                    ps.append((a & 0xfc, slot[lo | (a & 3) << 8], x, y))
                out['parts'].append(ps)
                out['maps'].append(bytes([len(ids)] + [(pool[t] // POOL_CHUNK) << 4 | pool[t] % POOL_CHUNK for t in ids]))
            index[f] = len(out['poses'])
            out['poses'].append((first, len(groups), lift))
        assert all(len(cfg.get(q, ())) <= SEQ_MAX for q in ('idle', 'fire', 'atk'))
        out['seqs'][k] = tuple([(d, index[f]) for d, f in cfg.get(q, ())] for q in ('idle', 'fire', 'atk'))
        out['atk_at'][k] = cfg.get('atk_at', 0)
    assert len(out['chunks']) <= 256 and len(out['parts']) <= 256 and len(out['poses']) <= 256
    return out


def bytes_section(frames):
    """type 3 layout with raw bytes: per frame (count of 4-byte words, offset in words), then the bodies"""
    n = len(frames)
    head, body = b'', bytearray()
    for fr in frames:
        fr = fr + b'\0' * (-len(fr) % 4)
        head += struct.pack('<HH', len(fr) // 4, (4 * n + len(body)) // 4)
        body += fr
    return head + bytes(body)


# ---- the bosses' projectiles --------------------------------------------------------------------------------
# weapon k: [(name, parts table, scale, loop [(duration, boss frame)][, radius])]. All of one boss's projectile pictures share
# one art frame (<= 32 tiles) streamed to VRAM (320, 32) like X's weapon art (tile base 0x520) at the change.
FX = {
    1: [('OPT', 0, 1, ((2, 73), (2, 74), (2, 75))),            # dragonfly option (boss anim 11)
        ('SHOT', 0, 1, ((4, 84), (3, 86), (3, 88))),           # option shot: enemy shot anim 0x11 = 84-88 2 f each;
        # every other picture over the same 10 frames (all 5 do not fit the 32 FX tiles)
        ('BOMB', 0, 1, ((1, 93),)),                            # bomb: anim 0x12 (BIN 33 anim table 0x80100978,
        # base 0x800FA000, read 2026-10-27; 80 / 81 and 84 / 85 were guesses, user report: the shot looked wrong)
        ('BOOM', 0, 1, ((3, 47), (3, 48)))],                     # blast (the last spark pictures: 32 tiles)
    2: [('ICE', 0, 1, ((3, 78), (3, 80), (3, 82), (3, 84))),   # ice chunk
        ('SHARD', 0, 1, ((4, 86), (4, 87)))],
    3: [('FIRE', 0, 1, ((4, 55), (4, 57))),                    # straight shot: the crescent (boss anim 0xB, 55-58; every
        # other picture since the fireball came: the four took all 32 FX tiles; 60 is empty)
        ('FIREBALL', 0, 0.6, ((8, 51), (8, 53))),                # the thrown fireball (anim 10, 51-54; user report 2026-10-28:
        # it flew as the crescent; 0.6x: the 48 px ball four times did not fit the 32 FX tiles)
        ],   # (its fire bits draw as the small fireball: the frames are a byte, 0-255 all taken - the bosses keep 47)
    4: [('ANCHOR', 0, 1, ((2, 81), (2, 84), (2, 87), (2, 90), (2, 93), (2, 96)))],
    5: [('BALL', 1, 0.4, ((5, 21), (5, 22), (5, 23)), 47)],              # rock ball (table 1, about 96 px: 0.4x; r 47 drops
                                                               # stray parts of another texture)
    6: [('RAIN', 0, 1, ((2, 81), (2, 85), (2, 89), (2, 93)))],  # water shot
    7: [('PLATE_V', 0, 1, ((8, 107),)),   # shell plates: the blue picture of the boss's plate anims 0x19 / 0x18
        ('PLATE_H', 0, 1, ((8, 111),)),   # (the orange ones too do not fit the 32 FX tiles; 47 was a teleport picture)
        ('PLATE_HIT', 0, 1, ((2, 117), (2, 139))),                     # 0x1a: a plate that blocked, until it fires
        ('COUNTER', 0, 1, ((3, 117), (3, 119))),   # (every other picture since the shell came: the FX frames end at 256)
        # the thrown shell (BIN 39 pattern 4: parts table 1 pictures 16-23 tumbling, 2 frames each; in the boss's palette
        # 2 = its own CLUT block 0x1380's 0): at full size (user request 2026-10-29: not smaller than the boss's) one
        # of them fits the 32 FX tiles - 17, three-quarter; the tumble by its flips (axl_dna_bosses.c)
        ('SHELL', 1, 1, ((4, 17),))],
    8: [('RAY_H', 0, 1, ((3, 60), (3, 62))),                   # ray arrow (60-63 / 64-67: every other picture, for the
        ('RAY_V', 0, 1, ((3, 64), (3, 66))),                   # clone and the beam within the 32 FX tiles)
        ('CLONE', 0, 1, ((4, 32), (4, 34))),                     # the small clone (pictures 32-38; user report 2026-10-28:
        # the clones flew as ray arrows)
        ('BEAM', 0, 1, ((3, 85), (3, 87)))],                     # the laser rain's beam (85-91, the yellow falling beam)
}
FX_TILES = 32
FX_PAL = {'SHELL': 2}   # a projectile drawn in another of the boss's 4 palettes than its parts say
FX_HOLD = ('BOOM',)   # played once (the others loop)


def table(b, dat, cfg, t):
    e = entry(dat, cfg['dat'])
    p = e[datpak.parse(e)[1][1]:]
    return p[struct.unpack_from('<I', p, 4 * t)[0]:]


def fx_frames(dat):
    """(art tiles per weapon [9], parts per projectile frame, anims [(name, k, [(dur, frame)])])."""
    arts, parts, anims = [[] for _ in range(9)], [], []
    for k, specs in sorted(FX.items()):
        cfg = BOSSES[k]
        b = Boss(dat, cfg)
        tiles = arts[k]
        for name, t, s, loop, *r in specs:
            b.pt = table(b, dat, cfg, t)
            idx = {}
            for _, f in loop:
                if f in idx:
                    continue
                img = b.compose(TABLE1 + f if t == 1 else f)   # (table 1: its own tile base, base1)
                if name in FX_PAL:
                    img = np.where(img > 0, img % 16 + 16 * FX_PAL[name], 0)
                if r:   # keep the picture within r px of its center
                    ys, xs = np.nonzero(img)
                    yy, xx = np.mgrid[0:2 * C, 0:2 * C]
                    cy, cx = np.median(ys), np.median(xs)
                    img = np.where((yy - cy) ** 2 + (xx - cx) ** 2 <= r[0] ** 2, img, 0)
                tl, ps = retile(scaled(img, s), FX_TILES, tiles)
                tiles += tl
                out = []
                for layer, n, x, y in ps:
                    assert -128 <= x <= 127 and -128 <= y <= 127, (name, f, x, y)
                    out.append((n >> 8 | layer << 2, n & 0xff, x, y))
                idx[f] = len(parts)
                parts.append(out)
            anims.append((name, k, [(d, idx[f]) for d, f in loop]))
        assert len(tiles) <= FX_TILES, '%s projectiles need %d tiles' % (cfg['name'], len(tiles))
        tiles += [np.zeros((16, 16), np.uint8)] * (FX_TILES - len(tiles))   # no stale tiles in VRAM
    assert len(parts) <= 256
    return arts, parts, anims
