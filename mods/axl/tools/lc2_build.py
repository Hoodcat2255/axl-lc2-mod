"""Axl PS1 build -> Mega Man X Legacy Collection 2 player archives (normal X = pl02).

usage: lc2_build.py [axl ROCK_X6.DAT] [out dir]
defaults: mods/axl/work/build/patch/ROCK_X6.DAT -> mods/axl/work/lc2/{pl02.arc, pl02_eng.arc}

Per archive (pl02.arc = Japanese data, pl02_eng.arc = US data), starting from the shipped archive:
  type 2 section k (TEX + COF pl02_kkk): Axl's when the Axl build changed it, except the BLANK_T2 sections that the
      PS1 build only emptied to save RAM (the PC has no such budget: X's original art stays)
  type 3 (PAT): per section, Axl's when changed from the US original, else the archive's own (JP text frames stay)
  type 9 (COL): per 16-colour CLUT, Axl's when changed from the US original, else the archive's own
Sections that exist only in the PS1 build (DNA Change looks: type 2 19-20, type 3 32-35) have no PC slot and are
reported as skipped. X's resident shot art (PS1 DAT 86 subfile 0x10206, VRAM) is com00_000 on PC with the same tile
layout (tile t at (16 * (t % 16), 16 * (t // 16))): the tiles the Axl build changed there (rotated shots from 864)
are copied in, for the aimed shots' frames 212-231 of parts section 1.
"""
import os
import subprocess
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lc2_fmt as F  # noqa: E402

PC_T2, PC_T3 = 19, 32   # sections the PC files have


def blank_t2():
    from build_axl import BLANK_T2
    return set(BLANK_T2)


def plan(axl, us):
    """(type 2 sections to take from Axl, type 3 sections to take from Axl, CLUTs to take from Axl, skipped)"""
    blank = blank_t2()
    t2 = [k for k in range(PC_T2) if k not in blank and F.t2_section(axl[2], k) != F.t2_section(us[2], k)]
    t3, appended = [], []
    for k in range(PC_T3):
        a, u = F.t3_section(axl[3], k), F.t3_section(us[3], k)
        if a == u:
            continue
        t3.append(k)   # section 1 only gains frames (212-231: the aimed shots, rotated; tiles in com00, see shots())
    n_clut = len(axl[9]) // 32
    t9 = [p for p in range(n_clut) if axl[9][32 * p:32 * p + 32] != us[9][32 * p:32 * p + 32]]
    import spritesheet as ss
    skipped = ['type 2 section %d' % k for k in range(PC_T2, len(ss.sections(axl[2])))]
    skipped += ['type 3 section %d' % k for k in range(PC_T3, len(ss.sections(axl[3])))] + appended
    return t2, t3, t9, skipped


COM00 = 'X6\\chr\\com\\com00_000'


def shots(arc, axl_dat):
    """com00_000: the shot VRAM tiles that differ between the Axl build and the US original"""
    import datpak
    from build_axl import SHOT_VRAM, shot_tile

    def vram(path):
        import struct
        dat = open(path, 'rb').read()
        s, z = struct.unpack_from('<II', dat, 8 * 86)
        cont = dat[s * 2048:s * 2048 + z]
        return [cont[o:o + n] for t, o, n in datpak.parse(cont) if t == SHOT_VRAM][0]
    a, u = vram(axl_dat), vram(F.DAT['us'])
    tex = bytearray(arc.get(COM00, F.H_TEX))
    w = (struct.unpack_from('<I', tex, 8)[0] >> 6) & 0x1fff
    img = np.frombuffer(bytes(tex[20:]), np.uint8).reshape(-1, w).copy()
    changed = [t for t in range(len(a) // 128) if (shot_tile(a, t) != shot_tile(u, t)).any()]
    for t in changed:
        y, x = 16 * (t // 16), 16 * (t % 16)
        assert y + 16 <= img.shape[0], t
        img[y:y + 16, x:x + 16] = shot_tile(a, t)
    hud_emblem(img)
    band = os.environ.get('AXL_COM00_TEST')   # test: rows a-b of com00 cleared (where is the HUD?)
    if band in ('row', 'col'):   # every texel = 16 + its tile row * 3 (or column * 8): read through the CLUT
        yy, xx = np.mgrid[0:img.shape[0], 0:img.shape[1]]
        img[:] = 16 + (yy // 16) * 3 if band == 'row' else 16 + (xx // 16) * int(os.environ.get('AXL_COL_MUL', '8'))
    elif band:
        a, b = map(int, band.split('-'))
        img[a:b] = F.TEX_PAD
    arc.put(COM00, F.H_TEX, bytes(tex[:20]) + img.tobytes())
    return changed


# The life gauge's emblem (user request 2026-10-27: the sheet's "A" gauge emblem instead of X's): four com00 tiles,
# CLUT slot 17, found by filling com00 with coded tile rows / columns (work/lc2/test/hud_row, hud_col5) and matching
# the tiles against the game-pixel HUD (hud1): (texture x, y) -> its game-screen top-left. The emblem (x 11-42,
# y 80-111): X frame 80-93, lives box 94-103 (digit window 95-101, the digits are drawn over it), spike 104-109.
# The sheet's emblem lines up with game = sheet - (887, 79) (its digit window on X's, its spike on X's).
HUD_TILES = ((32, 144, 11, 80), (240, 64, 27, 80), (208, 144, 11, 96), (224, 144, 27, 96))
HUD_SHEET_OFF = (887, 79)
HUD_CLUT = 17   # the HUD's CLUT slot (cdump 2026-10-27), read from the PS1 data (build_axl.stage_clut)
SHEET_BG = (27, 89, 153)


def hud_emblem(img):
    from PIL import Image
    sheet = np.array(Image.open(os.path.join(HERE, '..', 'work', 'sheet', 'axl_sheet.png')).convert('RGB')).astype(int)
    from build_axl import stage_clut
    pal = np.array([[(c & 31) << 3, (c >> 5 & 31) << 3, (c >> 10 & 31) << 3] for c in stage_clut(HUD_CLUT)])
    ox, oy = HUD_SHEET_OFF
    for tx, ty, gx, gy in HUD_TILES:
        out = np.zeros((16, 16), np.uint8)
        for v in range(16):
            for u in range(16):
                c = sheet[gy + v + oy, gx + u + ox]
                if np.abs(c - SHEET_BG).sum() < 12:
                    continue
                if c[0] > 150 and c[1] < 90:      # the sheet's red dots: no red in this CLUT -> frame grey
                    c = np.array([144, 144, 144])
                out[v, u] = 1 + int(np.argmin(((pal[1:] - c) ** 2).sum(1)))
        img[ty:ty + 16, tx:tx + 16] = out


# The 1UP (user request 2026-10-27: the sheet's Axl 1UP instead of X's helmet): X draws it with type 3 section 3
# frames 96-98 (the helmet turning) and 89-95 (the ball; 90-95 inside a ring of tiles 513-515 mirrored four ways),
# 2-4 com00 tiles each, 18-23 px across. The sheet's 10 pictures (19-25 px) go in 1:1 (not shrunk: user 2026-10-27),
# one per frame, centred on X's picture, onto empty com00 tiles from ONEUP_TILE0. The game draws items with CLUT slot 11
# of the stage palette (stage COL *_0x, the same 16 colours in all 46 stage archives; X's helmet matches a game capture
# with it, user 2026-11-08), which the energy capsules share and which has no grey: Axl's pictures are quantized to
# their own palette (oneup_own_pal: the sheet's 1UP colours, at most 15), which the DLL uploads to a free CLUT slot
# and gives the 1UP object (axl_items.c; the palette reaches it through axl_data.bin oneup_pal, lc2_hook.py).
ONEUP_CELLS = ((982, 234, 1001, 251), (1006, 234, 1025, 251), (1030, 234, 1049, 251), (1054, 231, 1078, 253),
               (1078, 231, 1102, 253), (1102, 231, 1127, 253), (1132, 232, 1153, 252), (1158, 233, 1178, 251),
               (1183, 232, 1204, 252), (1209, 231, 1232, 253))   # sheet (x0, y0, x1, y1): head turning into the ball
ONEUP_FRAMES = (96, 97, 98, 89, 90, 91, 92, 93, 94, 95)
ONEUP_SEC = 3
ONEUP_TILE0 = 878   # com00 878-917 are empty (864-877: the rotated shots)
ONEUP_STAGE, ONEUP_CLUT = 'st00_eng.arc', 11   # the item palette: stage COL *_0x slot 11
# placement (user 2026-11-08: the item wobbled): each picture's drawn pixels (not its cell, whose margins differ) are
# centred on X's picture of that frame, then moved by the viewer's per-frame nudge. work/sheet/oneup.json (the viewer's
# 1UP tab "save") = {"cells": [[x0, y0, x1, y1] x 10, in ONEUP_FRAMES order], "nudge": {"<frame>": [dx, dy]}}
ONEUP_JSON = os.path.join(HERE, '..', 'work', 'sheet', 'oneup.json')


def oneup_edits():
    """-> (cells, nudge {frame: (dx, dy)}) from ONEUP_JSON, else the defaults"""
    if not os.path.exists(ONEUP_JSON):
        return [tuple(c) for c in ONEUP_CELLS], {}
    import json
    d = json.load(open(ONEUP_JSON, encoding='utf-8'))
    cells = [tuple(int(v) for v in c) for c in d.get('cells', ONEUP_CELLS)]
    assert len(cells) == len(ONEUP_FRAMES) and all(x1 > x0 and y1 > y0 for x0, y0, x1, y1 in cells), \
        'oneup.json: %d cells, need %d with x1 > x0, y1 > y0' % (len(cells), len(ONEUP_FRAMES))
    nudge = {int(k): (int(v[0]), int(v[1])) for k, v in d.get('nudge', {}).items()}
    return cells, nudge


def oneup_own_pal():
    """the 1UP's own palette as [16, 3] RGB (5-bit steps): 0 transparent, then the sheet 1UP cells' colours, most used
    first (at most 15; more: the 15 most used, the rest drawn with the nearest)"""
    from PIL import Image
    sheet = np.array(Image.open(os.path.join(HERE, '..', 'work', 'sheet', 'axl_sheet.png')).convert('RGB')).astype(int)
    count = {}
    for x0, y0, x1, y1 in oneup_edits()[0]:
        c = sheet[y0:y1, x0:x1].reshape(-1, 3)
        for v in map(tuple, c[np.abs(c - SHEET_BG).sum(1) >= 12] >> 3 << 3):
            count[v] = count.get(v, 0) + 1
    cols = sorted(count, key=lambda v: (-count[v], v))[:15]
    return np.array([(0, 0, 0)] + cols + [(0, 0, 0)] * (15 - len(cols)))


def oneup_words():
    """oneup_own_pal as 16 PS1 colour words (the DLL's oneup_pal)"""
    return [int(r) >> 3 | (int(g) >> 3) << 5 | (int(b) >> 3) << 10 for r, g, b in oneup_own_pal()]


def oneup_pal():
    """X's 1UP colours as the game draws them (stage palette slot ONEUP_CLUT) as [16, 3] RGB (the viewer)"""
    st = F.Arc(os.path.join(F.ROMPC, ONEUP_STAGE))
    col = [e['name'] for e in st.entries if e['hash'] == F.H_COL and e['name'].endswith('_0x')][0]
    words = struct.unpack_from('<16H', st.get(col, F.H_COL), 12 + 32 * ONEUP_CLUT)
    return np.array([[(c & 31) << 3, (c >> 5 & 31) << 3, (c >> 10 & 31) << 3] for c in words])


def oneup_box(parts, img):
    """(x0, y0, x1, y1) of a frame's drawn texels (parts = 16x16 com00 tiles, flips as attr 0x40 / 0x80)"""
    m = np.zeros((96, 96), bool)
    for r in parts:
        attr, lo, x, y = struct.unpack('<BBbb', r)
        t = lo | (attr & 3) << 8
        tile = ~np.isin(img[t // 16 * 16:t // 16 * 16 + 16, t % 16 * 16:t % 16 * 16 + 16], (0, F.TEX_PAD))
        tile = tile[:, ::-1] if attr & 0x40 else tile
        m[48 + y:64 + y, 48 + x:64 + x] |= tile[::-1] if attr & 0x80 else tile
    ys, xs = np.nonzero(m)
    return xs.min() - 48, ys.min() - 48, xs.max() - 47, ys.max() - 47


def put_oneup(arc, pat_name):
    """the sheet's 1UP pictures onto com00 tiles ONEUP_TILE0.. and PAT section 3 frames ONEUP_FRAMES; -> tiles used"""
    from PIL import Image
    sheet = np.array(Image.open(os.path.join(HERE, '..', 'work', 'sheet', 'axl_sheet.png')).convert('RGB')).astype(int)
    pal = oneup_own_pal()
    tex = bytearray(arc.get(COM00, F.H_TEX))
    img = np.frombuffer(bytes(tex[20:]), np.uint8).reshape(-1, 256).copy()
    secs = F.pat_sections(arc.get(pat_name, F.H_PAT))
    t = ONEUP_TILE0
    cells, nudge = oneup_edits()
    for (x0, y0, x1, y1), f in zip(cells, ONEUP_FRAMES):
        c = sheet[y0:y1, x0:x1]
        pic = 1 + np.argmin(((c[:, :, None] - pal[None, None, 1:]) ** 2).sum(3), 2)
        pic[np.abs(c - SHEET_BG).sum(2) < 12] = 0
        ys, xs = np.nonzero(pic)
        assert len(ys), 'oneup cell %s (frame %d) is empty' % ((x0, y0, x1, y1), f)
        pic = pic[ys.min():ys.max() + 1, xs.min():xs.max() + 1]   # the drawn pixels only
        h, w = pic.shape
        bx0, by0, bx1, by1 = oneup_box(secs[ONEUP_SEC][f], img)
        dx, dy = nudge.get(f, (0, 0))
        ox, oy = (bx0 + bx1 - w) // 2 + dx, (by0 + by1 - h) // 2 + dy   # picture top-left, frame coordinates
        parts = []
        for ty in range(0, h, 16):
            for tx in range(0, w, 16):
                tile = np.zeros((16, 16), np.uint8)
                part = pic[ty:ty + 16, tx:tx + 16]
                tile[:part.shape[0], :part.shape[1]] = part
                if not tile.any():
                    continue
                r = img[t // 16 * 16:t // 16 * 16 + 16, t % 16 * 16:t % 16 * 16 + 16]
                assert np.isin(r, (0, F.TEX_PAD)).all(), 'com00 tile %d not empty' % t
                r[:] = tile
                parts.append(struct.pack('<BBbb', t >> 8, t & 0xff, ox + tx, oy + ty))
                t += 1
        secs[ONEUP_SEC][f] = parts
    arc.put(COM00, F.H_TEX, bytes(tex[:20]) + img.tobytes())
    arc.put(pat_name, F.H_PAT, F.pat(secs))
    return list(range(ONEUP_TILE0, t))


def convert(arc, axl, t2, t3, t9, pat_name):
    for k in t2:
        name = F.TEX_NAME % k
        tex, cof = F.tex_cof(axl[2], k, arc.get(name, F.H_COF))
        arc.put(name, F.H_TEX, tex)
        arc.put(name, F.H_COF, cof)
    own = F.pat_sections(arc.get(pat_name, F.H_PAT))
    arc.put(pat_name, F.H_PAT, F.pat([F.t3_section(axl[3], k) if k in t3 else own[k] for k in range(PC_T3)]))
    col = bytearray(arc.get(F.COL_NAME, F.H_COL))
    assert len(col) == 12 + len(axl[9]), 'COL size changed (%d vs %d)' % (len(col), 12 + len(axl[9]))
    for p in t9:
        col[12 + 32 * p:12 + 32 * p + 32] = axl[9][32 * p:32 * p + 32]
    arc.put(F.COL_NAME, F.H_COL, bytes(col))



# dialogue portraits (PS1 tools/face.py, laid out as the measured X portraits; k_pl00 has k_pl05's texel layout):
# normal X's k_pl00 -> Axl, the Ultimate's k_pl02 -> White Axl. The game reads them from pl02*.arc (in-game dialogue,
# checked 2026-10-26); kao.arc holds the same entries and gets the same faces.
# pc: the eye / mouth pieces where LC2 draws them (k_pl00: face.PC_PIECES); k_pl02's pieces stay at the PS1 places
# (measured 2026-10-27 with AXL_FACE_TEST=p: the mouth piece at portrait (16, 48); the PC ones broke his eyes / mouth)
KAO_FACES = ((0, 0, 'map5.json', True), (2, 2, 'map4.json', False))


# Frames the PS1 build left empty for its RAM (build_axl.BLANK_A) that Axl's anims use: 169 = the last entry sphere
# (the beam-in anim showed nothing for 6 frames: user report 2026-10-26). The PC has no such limit: built here as the
# PS1 build would without the blank and added to bank A (texture section 0, parts section 0).
PC_EXTRA_FRAMES = (169, 243, 244, 245)
# 243: aiming straight down standing (user request 2026-10-27: 8 directions on the ground, no crouch): the sheet's
# ground aim group 10 cell 17 (the PS1 build has no RAM for it; frame 243 was the removed roll's), anim 203 on PC
# 244 / 245: aiming up / down on a wall (user request 2026-10-27): "Shoot + Wall Cling" (group 30: 5 directions x
# shot / recoil, 4-5 = the forward one Axl already has) cells 0 (steep up) and 8 (steep down), anims 229 / 230
PC_EXTRA_MAP = {243: (10, 17), 244: (30, 0), 245: (30, 8)}
# X's head-only frames 163-182 (anims 41-64) are the ride armor's rider (user report 2026-11-02: no head showed in the
# ride armor; the PS1 build had blanked them as leftovers): the sheet's "Ride Armor" heads (RIDE_HEADS, facing
# right; RIDE_PICK) stand where X's head stands in each frame - bottom and face side lined up with X's head (177: X has none)
RIDE_FRAMES = tuple(f for f in range(163, 183) if f not in (169, 177))   # (169 = the entry sphere, above; the
# ride anims never show it - user report 2026-11-02: a lone head flashed in the beam-in)
RIDE_HEADS = ([1012, 2937, 1032, 2953], [1033, 2937, 1054, 2953], [1056, 2937, 1076, 2953], [1078, 2937, 1097, 2953],
              [1099, 2939, 1120, 2953])   # side, turning (2), the back of the head, side looking down
# X's head turns in some frames (2026-11-02 comparison, docs/visuals/lc2_ride_head_compare.png): 173 / 174 look back,
# 175 / 176 toward the screen; the rest are side views (RIDE_HEADS[0])
RIDE_PICK = {173: 2, 174: 3, 175: 1, 176: 1}
PC_EXTRA_FRAMES += RIDE_FRAMES


def ride_cuts(t2, sec2, t3, sec3):
    """{'A<f>': hand cut} for RIDE_FRAMES: the head picture over X's head (x_frame_mask: game data, facing left)"""
    import build_axl as BA
    cuts = {}
    for f in RIDE_FRAMES:
        x0, y0, x1, y1 = RIDE_HEADS[RIDE_PICK.get(f, 0)]
        mask, mx, my = BA.x_frame_mask(t2, sec2, t3, sec3, f)
        ys, xs = np.nonzero(mask)
        xa, yb = mx + int(xs.min()), my + int(ys.max())
        cuts['A%d' % f] = {'rect': [x0, y0, x1, y1], 'sx': -xa + 1 - (x1 - x0), 'sy': yb + 1 - (y1 - y0)}
    return cuts


def extra_frames():
    """{frame: (tiles [16 x 16 index arrays], parts [(attr, lo, x, y)])} for PC_EXTRA_FRAMES"""
    import build_axl as BA
    dat = open(F.DAT['us'], 'rb').read()
    sec, size = struct.unpack_from('<II', dat, 8 * BA.ENTRY)
    cont = dat[sec * 2048:sec * 2048 + size]
    subs = [(t, cont[o:o + z]) for t, o, z in BA.datpak.parse(cont)]
    t2, t3 = [b for t, b in subs if t == 2][0], [b for t, b in subs if t == 3][0]
    blank, anchors, feet = BA.BLANK_A, dict(BA.ANCHOR_FROM), dict(BA.FEET_REF)
    BA.BLANK_A = [f for f in blank if f not in PC_EXTRA_FRAMES]
    # the wall aims stand where X's wall shot (frame 62) stands, their legs on the forward wall shot's picture (the
    # roll's anchor, X standing, put them off the wall and their muzzle flash off the gun: user report 2026-10-27)
    BA.ANCHOR_FROM.update({244: 62, 245: 62})
    BA.FEET_REF.update({PC_EXTRA_MAP[244]: BA.WALL_SHOOT[1], PC_EXTRA_MAP[245]: BA.WALL_SHOOT[1]})
    xmap = BA.build_map_a()
    xmap.update(PC_EXTRA_MAP)
    axl = BA.Axl()
    sec2, sec3 = BA.ss.sections(t2)[0], BA.ss.sections(t3)[0]
    axl.cuts = dict(ride_cuts(t2, sec2, t3, sec3), **axl.cuts)   # (a viewer cut of the same frame wins)
    try:
        tiles, parts, _ = BA.convert_bank(axl, t2, t3, 0, 0, xmap, max(PC_EXTRA_FRAMES) + 1)
    finally:
        BA.BLANK_A, BA.ANCHOR_FROM, BA.FEET_REF = blank, anchors, feet
    return {f: (tiles[f], parts[f]) for f in PC_EXTRA_FRAMES}


# Victory (user request 2026-10-27: the sheet's "Victory" row was never used): X's victory anim 39 (his 183-186
# fist pump, a boss beaten / an armour part got; the PS1 build had made it the Giga Crush pose) gets all 9 pictures,
# in bank B (texture 10, PAT section 18) frames 47-55, X's Z-Saber slashes there (anims 183-188, a full charge only:
# Axl never charges) - bank A has no free frame numbers left. The DLL turns anim 39 to bank B while Axl plays
# (axl_x.c, X's bank table 0x034806B8). Boxes [y0, x0, y1, x1] on the sheet (the row's own cuts; cell 0 without its
# "Victory" label). The jump / spin pictures are lifted off the floor (tools/victory.py).
VICTORY_BOXES = [[2915, 2, 2966, 41], [2912, 45, 2966, 97], [2922, 100, 2966, 161], [2917, 164, 2966, 202],
                 [2909, 205, 2962, 236], [2900, 239, 2953, 282], [2905, 289, 2940, 348], [2890, 355, 2941, 416],
                 [2907, 422, 2959, 483]]
VICTORY_GROUP = 250        # (200-202: build_axl DNA / ladder / zip groups)
VICTORY_B = 47           # bank B frames VICTORY_B .. + 8
VICTORY_LIFT = None     # (tools/victory.py: work/sheet/victory.json's lift, the viewer's victory tab)


def victory_frames():
    """{bank B frame: (tiles, parts)} of the 9 victory pictures"""
    import build_axl as BA
    dat = open(F.DAT['us'], 'rb').read()
    sec, size = struct.unpack_from('<II', dat, 8 * BA.ENTRY)
    cont = dat[sec * 2048:sec * 2048 + size]
    subs = [(t, cont[o:o + z]) for t, o, z in BA.datpak.parse(cont)]
    t2, t3 = [b for t, b in subs if t == 2][0], [b for t, b in subs if t == 3][0]
    import victory
    _, lift, cells = victory.load()
    frames = range(VICTORY_B, VICTORY_B + len(cells))
    blank = BA.BLANK_B
    BA.BLANK_B = [f for f in blank if f not in frames]
    axl = BA.Axl()
    axl.groups[VICTORY_GROUP] = cells
    xmap = BA.build_map_b()
    for i, f in enumerate(frames):
        xmap[f] = (VICTORY_GROUP, i)
        if i in lift:   # (the nudge moves the parts by -nx in data x = +nx on screen facing right; ny = down;
            axl.nudge['B%d' % f] = (lift[i][0], lift[i][1])   # checked on the built frames 2026-10-27)
    # every picture's feet on the same spot, X's frame 47's feet (convert_bank anchors each frame on the X frame it
    # replaces: X's Z-Saber poses 47-65 stand in different places, the victory shook - user report 2026-10-27)
    xmask = BA.x_frame_mask
    BA.x_frame_mask = lambda a, b, c, d, f: xmask(a, b, c, d, VICTORY_B if f in frames else f)
    try:
        tiles, parts, _ = BA.convert_bank(axl, t2, t3, 10, 18, xmap, BA.ZIP_FRAMES_B)
    finally:
        BA.BLANK_B, BA.x_frame_mask = blank, xmask
    return {f: (tiles[f], parts[f]) for f in frames}


# Ladder aims (user request 2026-10-27: on a ladder Axl only shot forward, one by one): the sheet's "Ladder Shoot" row
# (group 62: 9 angles x shot / recoil; 13 = forward, X's ladder shot frame 150) gives up (4), up-diagonal (8),
# down-diagonal (15) and down (17), built after the rope aims in bank B (frames 74-77, played by anims 183-186:
# X's Z-Saber slashes, bank B in X's bank table) with their feet on X's ladder shot (frame 150) as frame 150's
LADDER_AIM_B = 74
LADDER_AIM_CELLS = ((62, 4), (62, 8), (62, 15), (62, 17))
LADDER_FIX = 2   # frame 150 in the built pl02 stands 2 px (data x) from this convert's estimate (measured 2026-10-27)


def ladder_frames():
    """{bank B frame: (tiles, parts)} of the 4 ladder aims"""
    import build_axl as BA
    dat = open(F.DAT['us'], 'rb').read()
    sec, size = struct.unpack_from('<II', dat, 8 * BA.ENTRY)
    cont = dat[sec * 2048:sec * 2048 + size]
    subs = [(t, cont[o:o + z]) for t, o, z in BA.datpak.parse(cont)]
    t2, t3 = [b for t, b in subs if t == 2][0], [b for t, b in subs if t == 3][0]
    frames = range(LADDER_AIM_B, LADDER_AIM_B + len(LADDER_AIM_CELLS))
    xmap = BA.build_map_b()
    xmap.update(dict(zip(frames, LADDER_AIM_CELLS)))
    xmask = BA.x_frame_mask
    a2, a3 = BA.ss.sections(t2)[0], BA.ss.sections(t3)[0]
    BA.x_frame_mask = lambda a, b, c, d, f: xmask(a, a2, c, a3, 150) if f in frames else xmask(a, b, c, d, f)
    try:
        # two passes: the legs (lowest 10 rows) of each aim on the forward shot's legs (frame 150 as bank A builds it):
        # the pictures' own feet estimates differ by the gun hanging low
        axl = BA.Axl()
        a_tiles, a_parts, _ = BA.convert_bank(axl, t2, t3, 0, 0, BA.build_map_a(), 151)   # (as the bank A build)
        ref = legs_x(a_tiles[150], a_parts[150])
        tiles, parts, _ = BA.convert_bank(axl, t2, t3, 10, 18, xmap, frames[-1] + 1)
        for f in frames:
            axl.nudge['B%d' % f] = (round(ref - legs_x(tiles[f], parts[f])) + LADDER_FIX, 0)   # (parts move by -nx)
        axl.cache.clear()
        tiles, parts, _ = BA.convert_bank(axl, t2, t3, 10, 18, xmap, frames[-1] + 1)
    finally:
        BA.x_frame_mask = xmask
    return {f: (tiles[f], parts[f]) for f in frames}


def legs_x(tl, ps):
    """left edge of the lowest 10 opaque rows of a built frame (data coordinates)"""
    S = 256
    c = np.zeros((S, S), bool)
    for a, lo, x, y in ps:
        m = tl[lo | (a & 3) << 8] != 0
        c[S // 2 + y:S // 2 + y + 16, S // 2 + x:S // 2 + x + 16] |= m[:, ::-1] if a & 0x40 else m
    rows = np.where(c.any(1))[0]
    low = c[rows.max() - 9:rows.max() + 1]
    return float(np.where(low.any(0))[0].min())


def put_extra_frames(arc, pat_name, extra, tex_sec=0, pat_sec=0):
    name = F.TEX_NAME % tex_sec
    tex, cof = F.tex_image(arc.get(name, F.H_TEX)), bytearray(arc.get(name, F.H_COF))
    ys = list(F.cof_ys(bytes(cof)))
    used = np.nonzero((tex != F.TEX_PAD).any(1))[0]
    y = (int(used.max()) + 16) // 16 * 16 if len(used) else 0
    secs = F.pat_sections(arc.get(pat_name, F.H_PAT))
    blocks = []
    while len(secs[pat_sec]) <= max(extra):   # (frames past the section's end: the ladder aims in bank B)
        secs[pat_sec].append([])
    while len(ys) <= max(extra):
        ys.append(0)
    for f, (tl, ps) in sorted(extra.items()):
        blk = np.full((16 * ((len(tl) + 15) // 16), 256), F.TEX_PAD, np.uint8)
        for t, img in enumerate(tl):
            blk[16 * (t // 16):16 * (t // 16) + 16, 16 * (t % 16):16 * (t % 16) + 16] = img
        ys[f] = y
        y += blk.shape[0]
        blocks.append(blk)
        secs[pat_sec][f] = [struct.pack('<BBbb', *q) for q in ps]
    h = tex.shape[0]
    while h < y:
        h *= 2
    out = np.full((h, 256), F.TEX_PAD, np.uint8)
    out[:tex.shape[0]] = tex
    out[y - sum(b.shape[0] for b in blocks):y] = np.vstack(blocks)
    head = struct.pack('<4sIIII', b'TEX\0', 0x200080a1, 1 | 256 << 6 | h << 19, 0x00011201, 0x14)
    struct.pack_into('<256I', cof, 0x2d, *(ys + [0] * (256 - len(ys))))
    struct.pack_into('<I', cof, 0x29, max(len(ys), struct.unpack_from('<I', cof, 0x29)[0]))   # (the frame count)
    arc.put(name, F.H_TEX, head + out.tobytes())
    arc.put(name, F.H_COF, bytes(cof))
    arc.put(pat_name, F.H_PAT, F.pat(secs))


def put_faces(arc):
    import face
    test = os.environ.get('AXL_FACE_TEST')   # u / v: every texel shows its own u / v (layout measurement)
    for n, i, layout, pc in KAO_FACES:
        pal, tex = (face.coded(test, n // 2) if test in ('u', 'v') else face.piece_coded(n // 2) if test == 'p' else
                    face.build(i, layout, pc=pc))
        t = bytearray(arc.get(F.KAO_TEX % n, F.H_TEX))
        t[20:20 + len(tex)] = tex
        arc.put(F.KAO_TEX % n, F.H_TEX, bytes(t))
        c = bytearray(arc.get(F.KAO_COL % n, F.H_COL))
        c[12:12 + len(pal)] = pal
        arc.put(F.KAO_COL % n, F.H_COL, bytes(c))


# Axl's voice and sounds from X8 (Legacy Collection 2's own X8 files, play_a; the same RIFF MS-ADPCM as X6's) over
# X's, as PS1 tools/sound.py chose them: the X6 sound files are named by (program, tone), mapped to the PS1 samples
# by the PS1 VAB; the voice files (vo_pl00) matched to the PS1 voice samples by their envelopes (2026-10-26).
X8_SE = os.path.join(F.ROMPC, '..', '..', '..', 'nativeDX10', 'X8', 'sound', 'se', 'play_a', 'wav')
AXL_SOUNDS = {
    'se\\pl00\\wav\\se_pl00_PG00_ID01': '01_a_jump_',          # sample 1 jump
    'se\\pl00\\wav\\se_pl00_PG00_ID08': '03_a_shot',           # request 8: the shot
    'se\\pl00\\wav\\se_pl00_PG03_ID06': '03_a_shot',
    'se\\pl00\\wav\\se_pl00_PG00_ID05': '00_a_dash_',          # sample 3 dash
    'se\\pl00\\wav\\se_pl00_PG00_ID10': '41_a_copy_shot',      # sample 6
    # request 13, X's beam-in (the PS1 build's sample 9, Giga Crush's "ikuzo", here once: no teleport sound - user
    # report 2026-10-29): X8's appear sound (common bank); the "ikuzo" with it from request 32 (IKUZO_SOUND)
    'se\\pl00\\wav\\se_pl00_PG00_ID14': os.path.join('..', '..', 'common', 'wav', '30_appear_kari'),
    'se\\vo_pl00\\wav\\x_05_01': '10_a_ite',                   # voice 1 hit
    'se\\vo_pl00\\wav\\x_05_04': '11_a_u',                     # voice 2 hit
    'se\\vo_pl00\\wav\\x_02_04': '13_a_ya',                    # voices 3-7: jump / wall kick shouts
    'se\\vo_pl00\\wav\\x_02_07': '15_a_ha',
    'se\\vo_pl00\\wav\\x_02_05': '14_a_sore',
    'se\\vo_pl00\\wav\\x_02_01': '31_a_hehen2',
    'se\\vo_pl00\\wav\\x_02_02': '15_a_ha',
    'se\\vo_pl00\\wav\\x_01_01b': '14_a_sore',                 # voices 8-10
    'se\\vo_pl00\\wav\\x_01_03b': '28_a_atare',
    'se\\vo_pl00\\wav\\x_01_04b': '32_a_ohhto',
    'se\\vo_pl00\\wav\\x_04_05b': '19_a_yabai',                # voice 11
    'se\\vo_pl00\\wav\\x_06_01': '20_a_uwaaaa',                # voices 12-13 (death)
    'se\\vo_pl00\\wav\\PL_OUT': '20_a_uwaaaa',
}
H_WAV = 0x724df879

# The DNA attacks' sounds: the bosses' own SE (their banks st0N_b load only in their stages) copied over X's special
# weapon sounds in Axl's bank (program 3: Axl fires no special weapons). Key: the PC request id the DLL plays
# (FUN_023be620 kind 1); the wave it plays is looked up in the bank (sebank.sounds; request_wave). The boss file is
# the PS1 SE id looked up in the boss DAT's SE table (type 7: id -> program / tone = the PC file name).
SE_BANK = 'X6\\sound\\se\\pl00\\pl00'
BOSS_SOUNDS = {
    20: ('em31', 'st01_b_PG00_ID02'),   # Yammark option shot (PS1 0x32)
    21: ('em36', 'st02_b_PG00_ID07'),   # Wolfang ice chunk (0x18)
    22: ('em36', 'st02_b_PG00_ID06'),   # Wolfang chunk breaks (0x17)
    23: ('em34', 'st03_b_PG00_ID04'),   # Heatnix shot (0x0E)
    25: ('em32', 'st05_b_PG00_ID01'),   # Scaravich kick (0x35)
    26: ('em32', 'st05_b_PG00_ID02'),   # Scaravich ball lands (0x36)
}
# Yammark's formation voices (user requests 2026-11-10/11): "Formation" + the formation's name, one wave each, played
# when the change starts (axl_dna_bosses.c y_form_tick). His clips (em31 st01_b PG02, by ear / whisper): ID07
# "Formation!", ID08 "Guard!", ID09 "Wing!", ID10 "Final!". The parts decoded, joined and encoded again at 48 kHz
# (adpcm_wave, as READY: the waves' blocks joined as they were played low and slow in the game, user report
# 2026-11-10). A part ending in .wav is a file of sound/ (absent: "Formation!" alone; a tuple of parts: the first there). Slots of pl00 nothing plays
# (a long session's log: 19 was played, so not it), each checked by recording the game (e2e zz_yam_voice, 2026-11-11):
# 24 / 35 / 17 right (16 too, the giga's: recorded 2026-11-11, its whole wave matched at speed 1 where the log put it); 36 / 37 (program 4) silent, as a renamed spare record of the bank with a new wave entry; 34
# played 4 semitones low (a 2000 Hz tone came out at 1588 Hz, 0.794 = 2^(-4/12): the game's own setting for X's
# sound there, its bank records the same as 24's), so its wave is made 4 semitones higher and faster beforehand (the
# last field: the speed it is made at).
YAM_VOICES = (
    (34, ('st01_b_PG02_ID07', 'st01_b_PG02_ID08'), 2 ** (4 / 12)),   # circle: "Formation... Guard!"
    (35, ('st01_b_PG02_ID07', ('yam_delta.wav', 'st01_b_PG02_ID09')), 1),   # wedge: "Formation... Delta!" (the
                                                                     # user's file, user request 2026-11-11; without
                                                                     # it his "Wing!")
    (17, ('st01_b_PG02_ID07', 'yam_line.wav'), 1),                   # line abreast: "Formation... Line!" (the user's
                                                                     # file, local only)
    (24, ('st01_b_PG02_ID07', 'st01_b_PG02_ID10'), 1),               # column: "Formation... Final!"
    (16, ('st01_b_PG02_ID07', ('yam_spiral.wav', 'st01_b_PG02_ID10')), 1),   # the giga: "Formation... Spiral!" (the
                                                                     # user's file, user request 2026-11-11)
)
# Heatnix's voices (user request 2026-11-12), his clips (em34 st03_b PG02, by whisper): ID00 "Ryaaa!", ID02 "Magma
# Stream!", ID03 "Ha!", ID04 "God Bird!", ID05 "Teya!" (01 / 06 / 07 hurt). In pl00 program 2's last free requests
# (14 / 15, beside Yammark's 16 / 17): the God Bird (the charged dash) and the giga's start; the shot's and the
# special's in X's 9 / 12 (user request 2026-11-12). (rid, parts, speed, bank)
HEAT_VOICES = (
    (14, ('st03_b_PG02_ID04',), 1, 'em34'),   # the charged dash: "God Bird!"
    (15, (('heat_extreme.wav', 'st03_b_PG02_ID02'),), 1, 'em34'),   # the giga: "Magma Extreme!" (sound/, an AI voice
                                                                   # made from his clips, user request 2026-11-12;
                                                                   # without it his "Magma Stream!")
    (9, ('st03_b_PG02_ID03',), 1, 'em34'),    # the shot: "Ha!" (X's request 9 / 12: never played for Axl in the test
    (12, ('st03_b_PG02_ID05',), 1, 'em34'),   # logs; the game's own go to the shot, axl_hook.c se_hook) / the special: "Teya!"
)
CHANGE_SOUND = (27, ('concat', ['41_a_copy_shot', '33_a_hensin']))
# the copy shot's charge (user request 2026-10-27: X's charge sound): X's charge loop (its request 7, 1.35 s, the tail
# looped; X's code stops request 7 while X is not charging) copied to a free special-weapon slot, request 29
CHARGE_SOUND = (29, 'se\\pl00\\wav\\se_pl00_PG00_ID07_LOOP')
# the hover (user request 2026-10-27; it sounded late from request 11, whose record links / fades: X's charge loop):
# X8's burner in a plain free slot, request 30: its loop whole (user report 2026-10-29: the 0.6 s start alone was not
# heard), played while Axl hovers and stopped after (axl_x.c hover_sound)
HOVER_SOUND = (30, '09_a_burner_loop')
# the common READY / WARNING (user requests 2026-10-29; the game's own requests: X's "Ready" voice / a BLANK wave):
# free special-weapon slots of pl00, which the DLL plays in their place for normal X (axl_hook.c se_hook; X's
# beam-in, request 13, comes 47 frames after READY). (request, file - sound/ or paths, gain dB, both ends' silence cut,
# seconds kept or None)
# READY: Axl's "Ready" voice (the user's file, axl_config ready_sound), its lead silence kept (the timing as recorded);
# without that file X8's announcer READY (English, its left and right halves mixed).
# WARNING: X8's warning siren, its left and right halves mixed, its first 1.2 s (one wail of two: user request),
# repeated back to back by the DLL (SIREN_MS)
X8_WARNING = [os.path.join(X8_SE, '..', '..', 'common3', 'wav', '09_warning_3_%s.xsew' % s) for s in 'LR']
SIREN_SOUND = (33, X8_WARNING, 0, False, 1.2)
IKUZO_SOUND = (32, '27_a_ikuzo')   # a free special-weapon slot, played with the beam-in (axl_hook.c se_hook)
READY_SOUND = (28, F.C.READY_SOUND if os.path.isfile(F.C.READY_SOUND) else
               [os.path.join(X8_SE, '..', '..', 'common', 'wav', 'eng', '08_ready_e_%s.xsew' % s) for s in 'LR'],
               0, False, None)
SOUND_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'sound')


def request_wave(arc, rid):
    """the wave entry (full name) that pl00 request rid plays"""
    import sebank
    return sebank.sounds(arc, SE_BANK)[0][rid]


def sound_entries(arc):
    """every wave entry put_sounds writes"""
    out = {'X6\\sound\\' + t for t in AXL_SOUNDS}
    out |= {request_wave(arc, r) for r in list(BOSS_SOUNDS) + [r for r, *_ in YAM_VOICES + HEAT_VOICES] + [CHANGE_SOUND[0], CHARGE_SOUND[0], HOVER_SOUND[0],
                                                           READY_SOUND[0], IKUZO_SOUND[0],
                                                           SIREN_SOUND[0]]}
    return out


def riff_chunks(b):
    """{chunk id: body}, in file order (dicts keep it)"""
    out, o = {}, 12
    while o < len(b) - 8:
        cid, sz = b[o:o + 4], struct.unpack_from('<I', b, o + 4)[0]
        out.setdefault(cid, b[o + 8:o + 8 + sz])
        o += 8 + sz + (sz & 1)
    return out


def riff(chunks):
    body = b'WAVE' + b''.join(c + struct.pack('<I', len(v)) + v + b'\0' * (len(v) & 1) for c, v in chunks.items())
    return b'RIFF' + struct.pack('<I', len(body)) + body


def x8_sound(spec):
    if isinstance(spec, str):
        return open(os.path.join(X8_SE, spec + '.xsew'), 'rb').read()
    if spec[0] == 'concat':  # one after the other: MS-ADPCM blocks decode on their own (the first one's rate)
        parts = [riff_chunks(open(os.path.join(X8_SE, n + '.xsew'), 'rb').read()) for n in spec[1]]
        first = dict(parts[0])                     # (its other chunks too: the engine reads smpl / tIME / ver.)
        first[b'data'] = b''.join(p[b'data'] for p in parts)
        return riff(first)
    name, secs = spec        # the start of a loop: whole MS-ADPCM blocks (each decodes on its own) up to secs
    b = open(os.path.join(X8_SE, name + '.xsew'), 'rb').read()
    out, o = [], 12
    while o < len(b) - 8:
        cid, sz = b[o:o + 4], struct.unpack_from('<I', b, o + 4)[0]
        body = b[o + 8:o + 8 + sz]
        if cid == b'fmt ':
            rate, align, spb = struct.unpack_from('<I', body, 4)[0], struct.unpack_from('<H', body, 12)[0], \
                struct.unpack_from('<H', body, 18)[0]
        elif cid == b'data':
            body = body[:align * int(secs * rate / spb)]
        elif cid == b'smpl':                       # no loop (its points lay past the cut): played once
            body = body[:28] + struct.pack('<I', 0) + body[32:]
        elif cid == b'fact':
            continue
        out.append(cid + struct.pack('<I', len(body)) + body + b'\0' * (len(body) & 1))
        o += 8 + sz + (sz & 1)
    body = b'WAVE' + b''.join(out)
    return b'RIFF' + struct.pack('<I', len(body)) + body


MSADPCM_COEF = ((256, 0), (512, -256), (0, 0), (192, 64), (240, 0), (460, -208), (392, -232))
MSADPCM_ADAPT = (230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230)


def msadpcm_block(x):
    """one mono MS-ADPCM block of the samples x (int16s): of the 7 standard predictors the one that errs least"""
    best = None
    for ci, (c1, c2) in enumerate(MSADPCM_COEF):
        s2, s1 = int(x[0]), int(x[1])
        delta = min(32767, max(16, abs(int(x[2]) - ((s1 * c1 + s2 * c2) >> 8)) // 4))
        head = struct.pack('<Bhhh', ci, delta, s1, s2)
        nibs, err = [], 0
        for v in x[2:]:
            pred = (s1 * c1 + s2 * c2) >> 8
            n = max(-8, min(7, round((int(v) - pred) / delta)))
            new = max(-32768, min(32767, pred + n * delta))
            err += (int(v) - new) ** 2
            nibs.append(n & 15)
            delta = min(32767, max(16, (delta * MSADPCM_ADAPT[n & 15]) >> 8))
            s2, s1 = s1, new
        if best is None or err < best[0]:
            best = (err, head, nibs)
    nibs = best[2] + [0] * (len(best[2]) & 1)
    return best[1] + bytes(nibs[i] << 4 | nibs[i + 1] for i in range(0, len(nibs), 2))


def file_sound(name, gain, trim, secs, old):
    """a sound file (sound/; or a list of paths, mixed: X8's left / right halves) as the game's MS-ADPCM wave (mono,
    48 kHz, its 70-byte blocks of 128 samples, which ffmpeg won't write), keeping the old wave's other chunks"""
    names = [os.path.join(SOUND_DIR, name)] if isinstance(name, str) else name   # (an absolute path stays)
    mix = 'amerge=inputs=%d,pan=mono|c0=%s,' % (len(names), '+'.join('%g*c%d' % (1 / len(names), i)
                                                                     for i in range(len(names)))) if len(names) > 1 else ''
    pcm = subprocess.run(['ffmpeg', '-v', 'error'] + [a for n in names for a in ('-i', n)] + ['-filter_complex', mix +
                          ('silenceremove=start_periods=1:start_threshold=-45dB,areverse,'
                           'silenceremove=start_periods=1:start_threshold=-45dB,areverse,' if trim else '') +
                          ('atrim=0:%g,' % secs if secs else '') + 'volume=%ddB,alimiter=limit=0.8' % gain,
                          '-ac', '1', '-ar', '48000', '-f', 's16le', '-'], check=True, capture_output=True).stdout
    return adpcm_wave(np.frombuffer(pcm, '<i2'), old)


def adpcm_wave(x, old):
    """the samples x (int16, mono, 48 kHz) as the game's MS-ADPCM wave, the old wave's other chunks kept"""
    x = np.concatenate([x, np.zeros(-len(x) % 128, '<i2')])
    ch = dict(riff_chunks(old))
    fmt = bytearray(ch[b'fmt '])
    assert struct.unpack_from('<HHxxxxxxxxHHxxH', fmt) == (2, 1, 70, 4, 128), fmt.hex()
    struct.pack_into('<II', fmt, 4, 48000, 48000 * 70 // 128)
    ch[b'fmt '] = bytes(fmt)
    ch[b'data'] = b''.join(msadpcm_block(x[i:i + 128]) for i in range(0, len(x), 128))
    if b'smpl' in ch:                              # its sample period (ns)
        ch[b'smpl'] = ch[b'smpl'][:8] + struct.pack('<I', 10 ** 9 // 48000) + ch[b'smpl'][12:]
    return riff(ch)


def put_sounds(arc):
    names = {e['name'] for e in arc.entries}
    done = 0
    for tail, spec in AXL_SOUNDS.items():
        n = 'X6\\sound\\' + tail
        if n in names:
            arc.put(n, H_WAV, x8_sound(spec))
            done += 1
    arc.put(request_wave(arc, CHANGE_SOUND[0]), H_WAV, x8_sound(CHANGE_SOUND[1]))
    boss = {}
    for rid, (em, wav) in BOSS_SOUNDS.items():
        if em not in boss:
            boss[em] = F.Arc(os.path.join(F.ROMPC, em + '.arc'))
        src = [e['name'] for e in boss[em].entries if e['hash'] == H_WAV and e['name'].endswith('\\' + wav)]
        assert len(src) == 1, (em, wav, src)
        arc.put(request_wave(arc, rid), H_WAV, boss[em].get(src[0], H_WAV))
        done += 1
    for em in ('em31', 'em34'):
        if em not in boss:
            boss[em] = F.Arc(os.path.join(F.ROMPC, em + '.arc'))
    for rid, parts, speed, *bank in [v for v in YAM_VOICES] + [v for v in HEAT_VOICES]:
        em = bank[0] if bank else 'em31'
        pcm = []
        for w in parts:
            if isinstance(w, tuple):               # (the first of them there)
                w = next(a for a in w if not a.endswith('.wav') or os.path.isfile(os.path.join(SOUND_DIR, a)))
            if w.endswith('.wav'):
                path = os.path.join(SOUND_DIR, w)
                if not os.path.isfile(path):
                    continue
                src = open(path, 'rb').read()
            else:
                n = [e['name'] for e in boss[em].entries if e['hash'] == H_WAV and e['name'].endswith('\\' + w)]
                assert len(n) == 1, (w, n)
                src = boss[em].get(n[0], H_WAV)
            pcm.append(np.frombuffer(subprocess.run(['ffmpeg', '-v', 'error', '-i', '-', '-ac', '1', '-ar', '48000',
                                                     '-f', 's16le', '-'], input=src, check=True,
                                                    capture_output=True).stdout, '<i2'))
        x = np.concatenate(pcm)
        if speed != 1:                             # (played faster: resampled to fewer samples)
            x = np.interp(np.arange(int(len(x) / speed)) * speed, np.arange(len(x)), x).astype('<i2')
        n = request_wave(arc, rid)
        arc.put(n, H_WAV, adpcm_wave(x, arc.get(n, H_WAV)))
        done += 1
    arc.put(request_wave(arc, CHARGE_SOUND[0]), H_WAV, arc.get('X6\\sound\\' + CHARGE_SOUND[1], H_WAV))
    arc.put(request_wave(arc, HOVER_SOUND[0]), H_WAV, x8_sound(HOVER_SOUND[1]))
    arc.put(request_wave(arc, IKUZO_SOUND[0]), H_WAV, x8_sound(IKUZO_SOUND[1]))
    for rid, name, gain, trim, secs in (READY_SOUND, SIREN_SOUND):
        n = request_wave(arc, rid)
        arc.put(n, H_WAV, file_sound(name, gain, trim, secs, arc.get(n, H_WAV)))
    return done + 6


# Stage select (user request 2026-10-27): PLAYER SELECT says "AXL" instead of "X". The names are parts frames of
# section 0 (frame 5 = X: one tile, 6; frame 7 = ZERO: three tiles 7-9 at x -24 / -8 / 8) on texture
# stsel_eng_000 (keyboard / pad copies). AXL = FALCON's A and L around the small alphabet's X (rows 212-220: the
# name font's 9 rows, X's own X is 11 rows and looked too big), 1 px apart as ZERO's letters, drawn in the free
# tiles 224-226 (row 14).
STSEL_TEX = ('X6\\chr\\stage\\stsel_eng\\stsel_eng_000_keyboard', 'X6\\chr\\stage\\stsel_eng\\stsel_eng_000_pcpad')
STSEL_PAT = 'X6\\pat\\stage\\stsel_eng\\stsel_eng'
STSEL_GLYPHS = ((0, 170, 180), (208, 161, 172), (0, 181, 189))   # (top row of the 16 rows, x span): A, X, L
STSEL_TILE = 224
# the Ultimate armour's "ULTIMATE" (frame 10, tiles 24-27) -> "UNKNOWN" (user request 2026-10-29): the name font's U
# (ULTIMATE), N (FALCON), O (ZERO), W (SHADOW), the small alphabet's K (its glyph rows 196-204 = the name font's 4-12),
# 1 px apart, 71 px: five tiles from 227 (free), centred
STSEL_WORDS = (
    (5, (0, 6, -8, -8), STSEL_GLYPHS, STSEL_TILE, 3),
    (10, None, ((16, 129, 137), (0, 210, 219), (192, 177, 186), (0, 210, 219), (0, 146, 155), (16, 52, 64),
                (0, 210, 219)), 227, 5),
)


def build_stsel(out):
    arc = F.Arc(F.orig_path(F.STSEL))
    for name in STSEL_TEX:
        raw = bytearray(arc.get(name, F.H_TEX))
        w = (struct.unpack_from('<I', raw, 8)[0] >> 6) & 0x1fff
        img = np.frombuffer(bytes(raw[20:]), np.uint8).reshape(-1, w).copy()
        clean = np.where(img == F.TEX_PAD, 0, img)
        for _, _, glyphs, tile, n in STSEL_WORDS:
            word = np.zeros((16, 16 * n), np.uint8)
            width = sum(b - a for _, a, b in glyphs) + len(glyphs) - 1
            x = (16 * n - width) // 2
            for top, a, b in glyphs:
                word[:, x:x + b - a] = clean[top:top + 16, a:b]
                x += b - a + 1
            y0, x0 = 16 * (tile // 16), 16 * (tile % 16)
            assert (np.isin(img[y0:y0 + 16, x0:x0 + 16 * n], (0, F.TEX_PAD))).all(), name   # (free tiles)
            img[y0:y0 + 16, x0:x0 + 16 * n] = word
        arc.put(name, F.H_TEX, bytes(raw[:20]) + img.tobytes())
    secs = F.pat_sections(arc.get(STSEL_PAT, F.H_PAT))
    assert [struct.unpack('<BBbb', r) for r in secs[0][5]] == [(0, 6, -8, -8)], secs[0][5]
    assert [struct.unpack('<BBbb', r)[1] for r in secs[0][10]] == [24, 25, 26, 27], secs[0][10]
    for frame, _, _, tile, n in STSEL_WORDS:
        secs[0][frame] = [struct.pack('<BBbb', 0, tile + i, -8 * n + 16 * i, -8) for i in range(n)]
    arc.put(STSEL_PAT, F.H_PAT, F.pat(secs))
    path = os.path.join(out, F.STSEL)
    open(path, 'wb').write(arc.build())
    print('wrote %s (%d bytes): PLAYER SELECT "AXL", "UNKNOWN"' % (path, os.path.getsize(path)))


# the Japanese PLAYER SELECT (user request 2026-11-14): X's "エックス" (frame 5, tiles 6-9) -> "アクセル". The game has
# no font for it (each name is a drawn word); hand-drawn glyphs looked like another font (2026-11-14 play: "폰트 스타일이
# 너무 달라"), an earlier cut was uneven ("일정하지 않고 부자연스러워"; "アルティメット" is squeezed: 7 letters in 64 px), so
# the 4 are cut from full-size words of the same texture: ア = "ファルコン"'s ァ (enlarged: STSEL_JP_FILL), ク =
# "エックス"'s, セ = "プレイヤーセレクト"'s, ル = "ファルコン"'s. A cut's white outline (15) is redrawn round its fill (the
# neighbours' outlines touch), the word centred in 64 px, 1 px overlapping outlines like the game's words; its 4 tiles go
# to free ones (no 4 free in a row; tiles 6-9 are other frames' too).
STSEL_JP_TEX = ('X6\\chr\\stage\\stsel\\stsel_000_keyboard', 'X6\\chr\\stage\\stsel\\stsel_000_pcpad')
STSEL_JP_PAT = 'X6\\pat\\stage\\stsel\\stsel'
STSEL_JP_TILES = (175, 176, 191, 192)
STSEL_JP_CUT = {'ク': (6, 32, 44, 0), 'セ': (0, 53, 63, 0), 'ル': (10, 26, 37, 0)}   # first tile of the word's 16-px
# strip, columns x0-x1 in it, rows moved
# ア: the ァ cut was a row shorter and 2 px narrower than セ / ル ("ア 가 더 커야할 거 같아. 높이를 맞춰봐", 2026-11-14), so
# it is the ァ's pixels enlarged by hand to their 11 rows (rows 3-13): the bar 1 px longer, the stem 1 row and 1 px
# further down-left (# dark 1, o grey 13, + light 14; the outline drawn round it as the cuts')
STSEL_JP_FILL = {'ア': ["..#########..",
                       "..ooooooooo#.",
                       "..+++++++oo#.",
                       "......#.##o#.",
                       ".....+o#oo#..",
                       ".....+o#++...",
                       ".....oo#.....",
                       "....#oo#.....",
                       "...+oo+......",
                       "..+oo+.......",
                       ".#o+........."]}


def stsel_jp_glyph(img, c):
    if c in STSEL_JP_FILL:
        rows = STSEL_JP_FILL[c]
        g = np.zeros((16, len(rows[0])), np.uint8)
        for y, r in enumerate(rows):
            g[3 + y] = [{'.': 0, '#': 1, 'o': 13, '+': 14}[ch] for ch in r]
        fill = g > 0
    else:
        t, x0, x1, dy = STSEL_JP_CUT[c]
        g = img[16 * (t // 16):16 * (t // 16) + 16, 16 * (t % 16) + x0:16 * (t % 16) + x1 + 1]   # (a word's tiles: a row)
        fill = np.roll(np.pad((g > 0) & (g != 15), ((0, 0), (1, 1))), dy, 0)
        g = np.roll(np.pad(g, ((0, 0), (1, 1))), dy, 0)
    pad = np.pad(fill, 1)
    near = np.zeros_like(fill)
    for ry in (-1, 0, 1):
        for rx in (-1, 0, 1):
            near |= pad[1 + ry:17 + ry, 1 + rx:fill.shape[1] + 1 + rx]
    return np.where(fill, g, np.where(near, 15, 0)).astype(np.uint8)


def stsel_jp_word(img, text='アクセル'):
    glyphs = [stsel_jp_glyph(img, c) for c in text]
    word = np.zeros((16, 64), np.uint8)
    x = (64 - sum(g.shape[1] for g in glyphs) + len(glyphs) - 1) // 2
    for g in glyphs:   # (outlines overlap 1 px: a fill pixel wins over an outline)
        cur = word[:, x:x + g.shape[1]]
        word[:, x:x + g.shape[1]] = np.where((g > 0) & ((g != 15) | (cur == 0)), g, cur)
        x += g.shape[1] - 1
    return word


def build_stsel_jp(out):
    arc = F.Arc(F.orig_path(F.STSEL_JP))
    for name in STSEL_JP_TEX:
        raw = bytearray(arc.get(name, F.H_TEX))
        w = (struct.unpack_from('<I', raw, 8)[0] >> 6) & 0x1fff
        img = np.frombuffer(bytes(raw[20:]), np.uint8).reshape(-1, w).copy()
        word = stsel_jp_word(img)
        for i, tile in enumerate(STSEL_JP_TILES):
            y0, x0 = 16 * (tile // 16), 16 * (tile % 16)
            assert (np.isin(img[y0:y0 + 16, x0:x0 + 16], (0, F.TEX_PAD))).all(), name   # (free tiles)
            img[y0:y0 + 16, x0:x0 + 16] = word[:, 16 * i:16 * i + 16]
        arc.put(name, F.H_TEX, bytes(raw[:20]) + img.tobytes())
    secs = F.pat_sections(arc.get(STSEL_JP_PAT, F.H_PAT))
    assert [struct.unpack('<BBbb', r)[1] for r in secs[0][5]] == [6, 7, 8, 9], secs[0][5]
    secs[0][5] = [struct.pack('<BBbb', 0, t, -32 + 16 * i, -8) for i, t in enumerate(STSEL_JP_TILES)]
    arc.put(STSEL_JP_PAT, F.H_PAT, F.pat(secs))
    path = os.path.join(out, F.STSEL_JP)
    open(path, 'wb').write(arc.build())
    print('wrote %s (%d bytes): PLAYER SELECT "アクセル"' % (path, os.path.getsize(path)))


def build_kaiwa(out):
    total = 0
    for fn in F.KAIWA_ARCS:
        arc = F.Arc(F.orig_path(fn))
        names = F.kaiwa_x(arc)
        assert names, fn
        for name, lang in names:
            data, n = F.kaiwa_axl(arc.get(name, F.H_KAIWA), lang)
            assert n, (fn, name)
            arc.put(name, F.H_KAIWA, data)
            total += n
        open(os.path.join(out, fn), 'wb').write(arc.build())
    print('wrote %d stage archives: the dialogue names X -> Axl (%d)' % (len(F.KAIWA_ARCS), total))


def build_kao(out):
    arc = F.Arc(F.orig_path(F.KAO))
    put_faces(arc)
    path = os.path.join(out, F.KAO)
    open(path, 'wb').write(arc.build())
    print('wrote %s (%d bytes): portraits k_pl%s' % (path, os.path.getsize(path),
                                                   ', '.join('%02d' % n for n, _, _, _ in KAO_FACES)))


def main(axl_dat=F.DAT['axl'], out=F.BUILT):
    F.C.require('lc2', 'x6_us')
    if not os.path.exists(axl_dat):
        sys.exit('%s not found: run the PS1 build step first (build_axl.py mods/axl/work/build --lc2)' % axl_dat)
    axl, us = F.dat_entry86(axl_dat), F.dat_entry86(F.DAT['us'])
    t2, t3, t9, skipped = plan(axl, us)
    print('type 2 sections from Axl: %s' % t2)
    print('type 3 sections from Axl: %s' % t3)
    print('CLUTs from Axl: %d of %d' % (len(t9), len(axl[9]) // 32))
    print('skipped (no PC slot): %s' % ', '.join(skipped))
    # the JP archive keeps its own type 3 sections where JP and US differ (6-9: region text); Axl must not touch them
    # (the Japanese PS1 data is optional: without it this check is skipped)
    if os.path.exists(F.DAT['jp']):
        jp = F.dat_entry86(F.DAT['jp'])
        region_t3 = [k for k in range(PC_T3) if F.t3_section(jp[3], k) != F.t3_section(us[3], k)]
        assert not set(region_t3) & set(t3), \
            'Axl changed region-specific type 3 sections %s' % (set(region_t3) & set(t3))
    else:
        print('no Japanese PS1 data (%s): region check skipped' % F.DAT['jp'])
    os.makedirs(out, exist_ok=True)
    import lc2_dna
    bosses = lc2_dna.all_bosses()
    fx = lc2_dna.fx_data()
    import boss_look
    hud = lc2_dna.hud_pictures(boss_look.fx_frames(open(F.DAT['us'], 'rb').read()), bosses)
    extra = extra_frames()
    victory = victory_frames()
    victory.update(ladder_frames())
    for fn, (_, pat_name) in F.PL02.items():
        arc = F.Arc(F.orig_path(fn))
        convert(arc, axl, t2, t3, t9, pat_name)
        put_extra_frames(arc, pat_name, extra)
        put_extra_frames(arc, pat_name, victory, 10, 18)
        tiles = shots(arc, axl_dat)
        put_oneup(arc, pat_name)
        slots = lc2_dna.dna_looks(arc, pat_name, bosses)   # DNA Change: the bosses' looks (textures 19-26, ...)
        fx_slot = lc2_dna.fx_looks(arc, pat_name, fx)       # and their projectiles (texture 27, PAT section 40)
        hud_slot = lc2_dna.hud_looks(arc, pat_name, hud)    # the skill HUD (texture 28, PAT sections 41-48)
        flash_slot = lc2_dna.flash_looks(arc, pat_name)    # the muzzle flash turned (texture 29, PAT section 49)
        if os.environ.get('AXL_LC2_SPIKE'):   # feasibility: one more texture (copy of 000) and PAT section 32 (of 0)
            tex0, cof0 = arc.get(F.TEX_NAME % 0, F.H_TEX), arc.get(F.TEX_NAME % 0, F.H_COF)
            px = np.frombuffer(tex0[20:], np.uint8).copy()
            m = (px > 0) & (px < 16)
            px[m] = (px[m] % 15) + 1          # visibly different colours: proves the texture index is used
            tex0 = tex0[:20] + px.tobytes()
            n = F.add_texture(arc, 'X6\\chr\\pl\\pl02\\pl02', F.TEX_NAME % 19, tex0, cof0,
                              struct.unpack_from('<I', cof0, 0x29)[0], F.cof_ys(cof0))
            secs = F.pat_sections(arc.get(pat_name, F.H_PAT))
            arc.put(pat_name, F.H_PAT, F.pat(secs + [secs[0]]))
            print('spike: texture index %d, PAT sections %d' % (n, len(secs) + 1))
        put_faces(arc)
        n_sounds = put_sounds(arc)
        path = os.path.join(out, fn)
        open(path, 'wb').write(arc.build())
        print('wrote %s (%d bytes); com00 shot tiles from Axl: %d (%s-%s); DNA looks %s; X8 sounds %d' % (
            path, os.path.getsize(path), len(tiles), min(tiles), max(tiles), slots[0], n_sounds))
    _, menu_pals = lc2_dna.menu_icons(F.Arc(F.orig_path('pl02_eng.arc')), bosses)
    lc2_dna.header(bosses, slots, fx_slot, menu_pals, flash_slot)
    lc2_dna.hud_header(hud_slot, hud)
    build_kao(out)
    build_stsel(out)
    build_stsel_jp(out)
    build_kaiwa(out)
    import lc2_credits                     # the sprite sheet's credits at the end of the staff roll
    lc2_credits.build_credits(out)


if __name__ == '__main__':
    main(*sys.argv[1:])
