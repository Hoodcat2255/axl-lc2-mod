"""Build Mega Man X6 (USA) v1.1 with Axl replacing normal X's graphics (stage 1: looks).

usage: build_axl.py outdir [--lc2]
--lc2 (also when src/ is absent): only what the Legacy Collection 2 build reads - outdir/patch/ROCK_X6.DAT,
axl_scripts.json, axl_gen.h, flame_spots.json; no PS1 C code, EXE or disc image.

Source frames: work/sheet/axl_sheet.png cut by tools/axl_segment.py into work/sheet/groups.json
(group = label index of the single-pistol half, frames in reading order). Every X body frame of DAT 86
(type 2 section 0 = bank A, 228 frames; section 10 = bank B, 66 frames) is replaced by one Axl frame
(X_MAP_A / X_MAP_B below), mirrored (X's data faces left) and placed so that its feet line up with the
feet of the X frame it replaces (bottom row and the center of the lowest 10 rows of opaque pixels).
Frames not in the map keep X's own tiles and parts.

Container changes (DAT 86 = normal X and Ultimate; loader FUN_80013bf0: armor 4/5 -> 86):
  type 2 sections 0 / 10: Axl tiles (16x16 4bpp, <= 32 per frame, LZ like the original)
  type 3 sections 0 / 18: Axl parts; 5 / 19 (Ultimate armor layer): empty
  type 9: X's body palettes (BODY/FLASH lists of x6_sprites) -> Axl's 15 colors; deco palettes unchanged
  Axl frames over 32 tiles (intro sphere) keep X's frame.
Frames stay within X's 32-tile VRAM area and 0x1000-byte decompression buffer.
X overlay (BIN 0) untouched. Axl's code + data go to ROCK_X6.BIN entry 41 (an empty 4-byte entry), loaded to EXT_BASE
(free RAM 0x801f8304-0x801fe7ff, docs/memory_map_x6.md section 7) right after the X overlay by FUN_80013bf0 (its call at
0x80013cdc goes through a resident stub that loads both): 4-byte id, C code (src/), Axl's own animation table +
scripts (idle, walk timing, entry, viewer edits). The per-frame hook (axl_frame, EXE stub in dead FUN_80022f1c)
points obj+0x30 at that table while the armor is normal / Ultimate; the armored X (DAT 87-89) is untouched.
"""
import json
import os
import struct
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, HERE)
import axl_config as C  # noqa: E402
import datpak  # noqa: E402
import patch_dat  # noqa: E402
import spritesheet as ss  # noqa: E402
import sound  # noqa: E402
import boss_look  # noqa: E402
from x6_sprites import (BODY_PALETTES, DECO_PALETTES, FLASH_PALETTES, join_sections,  # noqa: E402
                        split_sections, tint, type2_section, type3_section)

SRC = C.X6_US_FS   # (axl_config: the PS1 files of this machine)
SRC_BIN = os.path.join(C.X6_US, 'Mega Man X6 (USA) (v1.1).bin')
SHEET = os.path.join(HERE, '..', 'work', 'sheet')
ENTRY = 86
BG = (27, 89, 153)
MAX_TILES = 32
X_FRAMES = 228   # X's bank-A frames; viewer-added frames use 228-239 (USER_SLOT_END), built-in ones 240-255
USER_SLOT_END = 240
# Axl's 15 body colors (sheet "Palettes" > Frame 1, x 2..34, y 4821..4874)
PAL_SRC = (2, 4821, 34, 4874)


def seq(group, idxs):
    return [(group, i) for i in idxs]


IDLE = seq(4, [0, 1, 2, 3, 4, 5])
BLINK = seq(4, [6, 7, 6])
INTRO_POSES = seq(0, [12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24])
IDLE_LOW = seq(5, [0, 1, 2])
SHOOT_FWD = seq(10, [10, 11, 12])
WALK_START = seq(12, [2, 3])   # the sheet's "Walk Start" label was split, so its 2 pictures landed in group 12
WALK = seq(14, [0, 1, 2, 3, 4, 5, 6, 7, 8, 9])
JUMP = seq(23, [0, 1, 2, 3])
FALL = seq(23, [4, 5, 6, 7])
LAND = seq(24, [0, 1, 2])
DASH_START = seq(15, [0, 1])
DASH = seq(16, [0, 1])
DASH_END = seq(17, [0, 1, 2, 3])
ROLL = seq(18, [0, 1, 2, 3, 4, 5, 6, 7, 8])
WALL = seq(28, [1, 2, 3, 4])
WALL_KICK = seq(29, [0, 1, 2])
WALL_SHOOT = seq(30, [4, 5])
CROUCH = seq(32, [0, 1])           # going down, down (X 209, 211; sheet "Crouch")
CROUCH_SHOOT = seq(33, [8, 9])     # forward (X 212 = anim 108, 210; sheet "Crouch Shoot")
LADDER_SHOOT = seq(62, [13])
HURT = seq(74, [0, 2])
HURT_E = seq(76, [1, 4, 5])
DAMAGED = seq(78, [0, 1, 2])
GIGA = seq(73, [12, 13, 14, 15, 16])
INTRO_BEAM = (0, 1)
# short beam, small sphere, lightning sphere, white silhouette, 4 forming spheres, colored sphere, breaking sphere
INTRO_SPHERES = seq(0, [2, 3, 4, 5, 6, 7, 8, 9, 10, 11])
EXIT = seq(81, [0, 2, 4, 6, 7, 8, 9, 10])  # 4 poses, whitening, silhouette, thin, beam
SHOOT_UP = seq(10, [2])
SHOOT_DIAG = seq(10, [4, 5, 6])


IDLE_FRAMES = (0, 115, 119, 97, 98, 99)           # 97-104 are not used by any X script (work/xanims.txt)
INTRO_POSE_FRAMES = (135, 136, 137, 138, 139, 140, 141, 142, 100, 101, 102, 103, 104)
# X frames for the entry beam / sphere part; 161 and 169 were unused by X's scripts
INTRO_SPHERE_FRAMES = (127, 128, 129, 130, 131, 132, 133, 134, 161, 169)
# X frames whose feet position is taken from X frame 0 (standing) instead of their own picture
ANCHOR_FROM = {f: 0 for f in IDLE_FRAMES + INTRO_POSE_FRAMES + INTRO_SPHERE_FRAMES[1:] + (116, 117, 118)}


# X's walk loop has 14 script entries (32 frames, flags 0-13 = phase shared by the plain / buster versions);
# Axl loops 10 pictures; entries are grouped 2,2,2,2,1,1,1,1,1,1. X walks 2.0 px/frame and Axl's planted foot moves
# about 4.8 px per picture (48 px per loop), so the loop is 24 frames (2.4 per picture) to keep the feet from
# sliding: pictures show 2,3,2,3,2,3,2,3,2,2 frames. (X's own loop was 32 frames: armored X now walks a faster cycle.)
WALK_GROUPS = (2, 2, 2, 2, 1, 1, 1, 1, 1, 1)
WALK_PIC_DUR = (2, 3, 2, 3, 2, 3, 2, 3, 2, 2)   # tried 3 each (30 f, slower): user preferred this
WALK_DUR = [d for n, p in zip(WALK_GROUPS, WALK_PIC_DUR) for d in ([1, p - 1] if n == 2 else [p])]
WALK_CYCLE = sum(WALK_PIC_DUR)


# pictures whose feet are pinned to a reference picture (same stance: breathing, blink, low-HP idle)
FEET_REF = {k: IDLE[0] for k in IDLE[1:] + BLINK}
FEET_REF.update({k: IDLE_LOW[0] for k in IDLE_LOW[1:]})


# walk pictures are placed by their pelvis like X's walk (user, comparing in the viewer: feet, torso and head
# placement all made Axl lurch forward between pictures); the pelvis plate is the gray-green (96,120,108)
PELVIS_KEYS = set(WALK_START + WALK)
PELVIS_RGB = (96, 120, 108)
PELVIS_REF = WALK_START[0]


# X's walk / jump / fall / land / dash frames come in pairs: odd = plain, even = buster out (same Axl picture);
# a nudge on the plain frame also applies to its twin unless the twin has its own
BUSTER_TWIN = {f + 1: f for f in [1, 3, 29, 31] + list(range(5, 28, 2))}


# 8-direction aim poses (src/axl_main.c): up, up-diagonal, forward, down-diagonal (+ down in the air);
# frames 247-254 and 242 in bank A, shown by Axl-only animations AIM_ANIM_BASE + i (one held entry each).
# Never frame 255: set_anim (FUN_800179a4) marks +0x48 = 0xff to make FUN_80017b04 stream the new frame, and that
# skips when +0x47 == +0x48, so a frame 255 kept the previous frame's tiles (air down-aim after forward, 2026-10-25)
AIM_GROUND = seq(10, [1, 5, 9, 13])
AIM_AIR = seq(27, [0, 5, 9, 15, 16])
AIM_GROUND_FRAMES = (247, 248, 249, 250)
AIM_AIR_FRAMES = (251, 252, 253, 254, 242)
AIM_ANIM_BASE = 193        # Axl's table grows past X's 193 anims: 193-196 ground, 197-201 air
ANCHOR_FROM.update({f: 0 for f in AIM_GROUND_FRAMES})    # feet on X's standing feet
ANCHOR_FROM.update({f: 41 for f in AIM_AIR_FRAMES})      # X's fall pose

# hover (jet flicker) and dodge roll (dive, tumble, get up): Axl-only animations, frames 240-246 + 97
# (97 was an idle frame; the viewer's idle edit no longer uses it)
HOVER = seq(26, [0, 1])
DODGE = seq(18, [1, 5, 6, 7, 8, 2])
HOVER_FRAMES = (240, 241)
ROLL_FRAMES = (243, 244, 245, 246, 97)   # 242 = air down-aim
HOVER_ANIM, ROLL_ANIM = 202, 203
# DNA Change pose (sheet "DNA Change", right of the segmenter's cut: boxes [y0, x0, y1, x1]) -> frames 98, 99, 105
# (unreferenced X frames; all others in 228-255 are taken)
DNA_BOXES = [[1212, 1281, 1272, 1318], [1207, 1370, 1272, 1411], [1217, 1453, 1269, 1483]]
DNA_GROUP = 200
# ladder and zipline (sheet "Ladder Climb" / "Ladder Top" / "Zipline Attach" / "Zipline Idle/Move", boxes
# [y0, x0, y1, x1] measured below the direction arrows): the segmenter's group 60 mixes them with label text
LADDER_GROUP = 201
# sheet "Ladder Climb": grab (side), 8 climbing pictures (a cycle); "Ladder Top": standing, crouched, getting off
LADDER_BOXES = [[2104, 5, 2154, 35], [2106, 40, 2154, 73], [2101, 75, 2159, 102], [2102, 111, 2160, 136],
                [2106, 139, 2157, 169], [2106, 172, 2154, 205], [2101, 209, 2159, 236], [2102, 242, 2160, 267],
                [2106, 270, 2157, 300], [2104, 303, 2159, 337], [2095, 342, 2139, 373], [2093, 382, 2131, 416]]
LADDER_GRAB = (LADDER_GROUP, 0)
LADDER = seq(LADDER_GROUP, [1, 2, 3, 4, 5, 6, 7, 8])
LADDER_TOP = seq(LADDER_GROUP, [9, 10, 11])
LADDER_MID = (143, 144, 145, 146, 147, 148, 149, 151, 152)   # X's ladder frames placed by their middle (mid_anchor)
ZIP_GROUP = 202
ZIP_BOXES = [[2197, 7, 2255, 50]]
ZIP_ATTACH = (ZIP_GROUP, 0)
# rope (X's FUN_80039984, up held: a vertical rope (collision 0x2e) = state 27, a rope to hang from (0x25-0x2d) =
# state 28 with one of 3 poses by the slope and the facing; X only shoots forward). Per pose: sheet
# "Zipline Idle/Move" pictures (the arrow over each group) and a "Zipline Shoot" row of 10 (gun 45 up, 20 up,
# forward, 20 down, 45 down, two each)
ZIP_COLS = [(8, 52), (54, 97), (99, 149), (150, 199), (201, 250), (256, 303), (309, 354), (363, 407), (414, 457),
            (468, 510)]
ZIP_POSES = (    # (idle row y0, y1, idle x ranges, shoot row y0, y1)
    (2198, 2262, ((432, 463), (467, 500), (505, 538)), 2282, 2346),                # 0 vertical rope (up)
    (2194, 2262, ((129, 160), (164, 197), (202, 235)), 2361, 2429),                # 1 slope pose 1 (up-right)
    (2193, 2262, ((324, 355), (363, 392), (398, 427)), 2532, 2601),                # 2 slope pose 2 (down-right)
    (2193, 2262, ((245, 276), (283, 314)), 2443, 2512))                            # 3 level rope (right)
ZIP_IDLE, ZIP_SHOOT = [], []
for _y0, _y1, _xs, _s0, _s1 in ZIP_POSES:
    ZIP_IDLE.append([(ZIP_GROUP, len(ZIP_BOXES) + i) for i in range(len(_xs))])
    ZIP_BOXES += [[_y0, a, _y1, b] for a, b in _xs]
    ZIP_SHOOT.append([(ZIP_GROUP, len(ZIP_BOXES) + i) for i in range(len(ZIP_COLS))])
    ZIP_BOXES += [[_s0, a, _s1, b] for a, b in ZIP_COLS]
ZIP_AIM_COL = (0, 4, 8)         # up-diagonal, forward, down-diagonal
ZIP_SLIDE = (ZIP_GROUP, len(ZIP_BOXES))   # sliding down the vertical rope ("down")
ZIP_BOXES.append([2198, 540, 2262, 571])
# X's bank B frames per pose: plain hang, plain moving, buster out, buster out moving
ZIP_X = ((2, (3, 4, 5, 6, 11, 12, 13, 14), 7, (8, 9, 10, 15, 16, 17, 18)),   # 3-6 climb, 11-14 slide down
         (19, (20, 21, 22, 23), 24, (25, 26, 27)),
         (28, (29, 30, 31, 32), 33, (34, 35, 36)),
         (39, (40, 41, 42), 43, (44, 45, 46)))
ZIP_AIM_FRAMES = 66             # added bank B frames 66 + 2 * pose (+0 up-diagonal, +1 down-diagonal)
ZIP_FRAMES_B = ZIP_AIM_FRAMES + 2 * len(ZIP_X)
BLANK_B = range(47, 66)
# X's head-only frames 163-182 (anims 41-61; no X6 code sets those anims, the X anim table is only X's: X5's ride
# armor leftovers, estimate): empty for RAM (5 KB, the hand-cut ladder pictures 2026-10-26)
BLANK_A = range(163, 183)
# hanging pictures line up at the hand on the rope (top of the picture), not at the feet: X frame -> its pose's
# plain hang frame (no sparks above it)
ZIP_REF = {}
for _p, (_h, _mv, _b, _bmv) in enumerate(ZIP_X):
    ZIP_REF.update({f: _h for f in (_h, _b) + _mv + _bmv})
    ZIP_REF.update({ZIP_AIM_FRAMES + 2 * _p: _h, ZIP_AIM_FRAMES + 2 * _p + 1: _h})
DNA = seq(DNA_GROUP, [0, 1, 2])
DNA_FRAMES = (98, 99, 105)
DNA_ANIM = 204
FX_ANIM_BASE = 205   # the bosses' projectile anims (boss_look.FX order)
DNA_DUR = (8, 8, 14)
HOVER_DUR = 3
ROLL_DUR = (3, 3, 3, 3, 5)        # roll removed: anim kept, frames empty
ANCHOR_FROM.update({f: 41 for f in HOVER_FRAMES})
ANCHOR_FROM.update({f: 0 for f in ROLL_FRAMES})
ANCHOR_FROM.update({f: 0 for f in DNA_FRAMES})


def spread(frames, target):
    """Assign target frames (list) to X frames in order, resampling to the same length."""
    return {f: target[min(len(target) - 1, k * len(target) // len(frames))] for k, f in enumerate(frames)}


def build_map_a():
    m = {}
    # idle: script rewritten to 0,115,119,97,98,99 (IDLE_FRAMES); blink 116-118
    m.update(dict(zip(IDLE_FRAMES, IDLE)))
    m.update({116: BLINK[0], 117: BLINK[1], 118: BLINK[2]})
    m.update(spread(range(120, 126), IDLE_LOW))
    for base in (1, 2):                                   # odd = plain, even = buster out (X shoots while moving)
        m[base] = WALK_START[0]; m[base + 2] = WALK_START[1]
        walk = [29 + base - 1, 31 + base - 1] + list(range(5 + base - 1, 28 + base - 1, 2))
        k = 0
        for g, n in enumerate(WALK_GROUPS):           # X walk entries -> Axl walk picture g (4 frames each)
            for _ in range(n):
                m[walk[k]] = WALK[g]; k += 1
        m.update(spread([33 + base - 1, 35 + base - 1, 37 + base - 1, 39 + base - 1], JUMP))
        m.update(spread([41 + base - 1, 43 + base - 1, 45 + base - 1, 47 + base - 1], FALL))
        m.update(spread([49 + base - 1, 51 + base - 1, 53 + base - 1], LAND))
        # anim 17 / 129 = dash stop (brake), not an air dash: X leans back and slides to a halt
        m.update(dict(zip([77 + base - 1, 79 + base - 1, 81 + base - 1, 83 + base - 1], DASH_END)))
    m.update({55: WALL[0], 61: WALL[1], 63: WALL[2], 65: WALL[3], 57: WALL_KICK[0], 59: WALL_KICK[1]})
    m.update({56: WALL_SHOOT[0], 62: WALL_SHOOT[1], 64: WALL_SHOOT[1], 58: WALL_KICK[0], 60: WALL_KICK[1],
              66: WALL_SHOOT[0]})
    m.update({67: DASH_START[0], 68: DASH_START[0], 69: DASH_START[1], 70: DASH_START[1]})
    m.update(spread(range(71, 77), DASH))
    m.update({93: DASH_START[0], 94: DASH_START[1]})
    m.update(spread(range(85, 93), SHOOT_FWD))
    m.update(spread([95, 96, 105], EXIT[:4]))
    m.update({106: HURT[0], 107: HURT[1], 108: HURT[0], 109: HURT[1]})
    m.update(spread(range(110, 115), DAMAGED))
    m[126] = INTRO_BEAM
    m.update(dict(zip(INTRO_SPHERE_FRAMES, INTRO_SPHERES)))
    m.update(dict(zip(INTRO_POSE_FRAMES, INTRO_POSES)))
    # X6's ladder (collision 0x20/0x21, FUN_8003a488): anim 30 grab (143), 31 / 32 climb up / down (8 frames, in
    # anim 31's order = the sheet's cycle), 92 / 93 shoot (150, 155), 28 / 29 on / off the top (162, 153, 154)
    m[143] = LADDER_GRAB
    # (RAM: every other climbing picture, each shown for two of X's frames)
    m.update(dict(zip([148, 151, 145, 147, 149, 152, 144, 146], [LADDER[k // 2 * 2] for k in range(8)])))
    m.update({150: LADDER_SHOOT[0], 155: LADDER_SHOOT[0], 162: LADDER_TOP[0], 153: LADDER_TOP[1], 154: LADDER_TOP[2]})
    # 161: entry sphere (INTRO_SPHERE_FRAMES)
    m.update({156: EXIT[4], 157: EXIT[5], 158: EXIT[6], 159: EXIT[6], 160: EXIT[7]})
    m.update(spread(range(183, 187), GIGA))
    m.update({187: HURT[0], 188: DAMAGED[0], 189: DAMAGED[1]})
    m.update({f: DAMAGED[2] for f in range(190, 195)})
    m.update(dict(zip([195, 197, 199, 201], LADDER[::2])))   # anims 23-26 (no X6 code sets them)
    m.update({196: LADDER_SHOOT[0], 198: LADDER_SHOOT[0], 200: LADDER_SHOOT[0], 202: LADDER_SHOOT[0],
              203: LADDER_TOP[0], 204: LADDER_TOP[1]})
    m.update(spread(range(205, 209), HURT_E))
    # X6's crouch (state 26): 209 going down, 211 down, 212 / 210 shooting; 213-218 (X's crouch slash) stay down
    m.update({209: CROUCH[0], 210: CROUCH_SHOOT[1], 211: CROUCH[1], 212: CROUCH_SHOOT[0]})
    m.update({f: CROUCH[1] for f in range(213, 219)})
    m.update(spread(range(219, 228), ROLL))
    m.update(dict(zip(AIM_GROUND_FRAMES, AIM_GROUND)))
    m.update(dict(zip(AIM_AIR_FRAMES, AIM_AIR)))
    m.update(dict(zip(HOVER_FRAMES, HOVER)))
    # dodge roll removed (src/axl_main.c, user's decision 2026-10-24): frames 243-246 stay empty, RAM for full-size looks
    m.update(dict(zip(DNA_FRAMES, DNA)))
    return m                                              # 163-182 (tiny heads): BLANK_A


def build_map_b():
    # X's rope (bank B): 0, 1, 37, 38 grab it; per pose (ZIP_X) the hang / moving frames get the sheet's idle / move
    # pictures, the buster frames the forward shot; added frames 66-73 are the diagonal aims (src/axl_main.c)
    m = {0: ZIP_ATTACH, 1: ZIP_ATTACH, 37: ZIP_ATTACH, 38: ZIP_ATTACH}
    for p, (hang, move, bus, bmove) in enumerate(ZIP_X):
        idle, shoot = ZIP_IDLE[p], ZIP_SHOOT[p]
        m[hang] = idle[0]
        m.update({f: idle[0] for f in move})
        m.update({f: shoot[ZIP_AIM_COL[1]] for f in (bus,) + bmove})
        m[ZIP_AIM_FRAMES + 2 * p] = shoot[ZIP_AIM_COL[0]]
        m[ZIP_AIM_FRAMES + 2 * p + 1] = shoot[ZIP_AIM_COL[2]]
    m.update({f: ZIP_SLIDE for f in (11, 12, 13, 14)})
    # 47-65: X's Z-Saber slashes (anims 183-188, on a full charge): Axl never charges, so they stay empty (BLANK_B)
    return m


# --- Axl frames ----------------------------------------------------------------------------------------
class Axl:
    def __init__(self):
        self.im = np.array(Image.open(os.path.join(SHEET, 'axl_sheet.png')).convert('RGB')).astype(int)
        self.groups = {int(k): v for k, v in json.load(open(os.path.join(SHEET, 'groups.json'))).items()}
        self.groups[DNA_GROUP] = DNA_BOXES
        self.groups[LADDER_GROUP] = LADDER_BOXES
        self.groups[ZIP_GROUP] = ZIP_BOXES
        x0, y0, x1, y1 = PAL_SRC
        r = self.im[y0:y1, x0:x1].reshape(-1, 3)
        cols = sorted({tuple(c) for c in r if tuple(c) != BG})
        assert len(cols) <= 15, len(cols)
        self.cols = np.array(cols)
        self.cache = {}
        # hand edits from the viewer (tools/make_viewer.py): work/sheet/axl_edits.json = {nudge, cuts}
        epath = os.path.join(SHEET, 'axl_edits.json')
        npath = os.path.join(SHEET, 'nudge.json')
        if os.path.exists(epath):
            ed = json.load(open(epath))
            self.nudge, self.cuts = ed.get('nudge', {}), ed.get('cuts', {})
            # anims: {anim id: {steps: [{f, d}], loop}} = Axl's own script for that animation;
            # slots: {frame >= 228: X frame it was added next to} = new bank-A frames (pictures come from cuts)
            self.anims, self.slots = ed.get('anims', {}), {int(k): v for k, v in ed.get('slots', {}).items()}
        else:
            self.nudge = json.load(open(npath)) if os.path.exists(npath) else {}
            self.cuts, self.anims, self.slots = {}, {}, {}

    def palette555(self):
        p = [0]
        for r, g, b in self.cols:
            v = (r >> 3) | (g >> 3) << 5 | (b >> 3) << 10
            p.append(v or 1 << 10)
        p += [0] * (16 - len(p))
        return np.array(p, '<u2')

    def white555(self):
        """White Axl (Ultimate armor slot): Axl's palette through work/sheet/white_map.json (sheet colors)."""
        wm = {tuple(a): tuple(b) for a, b in json.load(open(os.path.join(SHEET, 'white_map.json')))['map']}
        p = [0]
        for c in self.cols:
            r, g, b = wm[tuple(int(v) for v in c)]
            v = (r >> 3) | (g >> 3) << 5 | (b >> 3) << 10
            p.append(v or 1 << 10)
        p += [0] * (16 - len(p))
        return p

    def crop(self, rect):
        """Indexed image of a hand-cut sheet rectangle [x0, y0, x1, y1], mirrored to face left."""
        x0, y0, x1, y1 = rect
        rgb = self.im[y0:y1, x0:x1]
        mask = ~(rgb == BG).all(2)
        d = ((rgb[:, :, None, :] - self.cols[None, None, :, :]) ** 2).sum(3)
        return ((d.argmin(2) + 1) * mask)[:, ::-1].astype(np.uint8)

    def frame(self, key):
        """Indexed image (0 = transparent), mirrored to face left."""
        if key in self.cache:
            return self.cache[key]
        y0, x0, y1, x1 = self.groups[key[0]][key[1]]
        rgb = self.im[y0:y1, x0:x1]
        mask = ~(rgb == BG).all(2)
        d = ((rgb[:, :, None, :] - self.cols[None, None, :, :]) ** 2).sum(3)
        idx = (d.argmin(2) + 1) * mask
        idx = idx[:, ::-1]
        self.cache[key] = idx.astype(np.uint8)
        return self.cache[key]


def bottom(a):
    return int(np.where(a.any(1))[0].max()) + 1


def feet_offset(img, ref, rows=8, search=6):
    """dx so that img column c lines up with ref column c + dx: the shift with the fewest differing pixels
    in the lowest `rows` rows (the feet), both pictures bottom-aligned."""
    fa, fr = img[bottom(img) - rows:bottom(img)], ref[bottom(ref) - rows:bottom(ref)]
    W = max(fa.shape[1], fr.shape[1]) + 2 * search
    best = None
    for dx in range(-search, search + 1):
        A = np.zeros((rows, W), np.int16); R = np.zeros_like(A)
        R[:, search:search + fr.shape[1]] = fr
        A[:, search + dx:search + dx + fa.shape[1]] = fa
        n = int((A != R).sum())
        if best is None or n < best[0]:
            best = (n, dx)
    return best[1]


def anchor(mask):
    """(bottom row + 1, x center of the lowest 10 opaque rows) of a boolean image."""
    rows = np.where(mask.any(1))[0]
    bot = rows.max() + 1
    low = mask[max(0, bot - 10):bot]
    xs = np.where(low.any(0))[0]
    return bot, (xs.min() + xs.max() + 1) / 2


def mid_anchor(mask):
    """(bottom row + 1, x center of the whole picture): climbing pictures stay centred on the ladder (their feet
    swing from side to side)"""
    rows, cols = np.where(mask.any(1))[0], np.where(mask.any(0))[0]
    return rows.max() + 1, (cols.min() + cols.max() + 1) / 2


def top_anchor(mask, rows=6):
    """(top row, x center of the highest `rows` opaque rows): the hand on the rope of a hanging picture."""
    top = int(np.where(mask.any(1))[0].min())
    xs = np.where(mask[top:top + rows].any(0))[0]
    return top, (xs.min() + xs.max() + 1) / 2


def clamp_anchor(axl, key, rows=12):
    """vertical rope pictures: the clamp of the hang picture (ZIP_IDLE[0][0], top-anchored) matched pixel for
    pixel, so the rope runs through every clamp (the slide picture's brake wing and the up aim's gun reach the
    top rows and moved a top-row center)"""
    ref = axl.frame(ZIP_IDLE[0][0]) != 0
    rt, rc = top_anchor(ref)
    if key == ZIP_IDLE[0][0]:
        return rt, rc
    img = axl.frame(key) != 0
    blk = ref[rt:rt + rows]
    best = None
    for dy in range(-4, 5):
        for dx in range(-4, img.shape[1] - blk.shape[1] + 5):   # the gun widens the picture on one side
            y0, x0 = rt + dy, dx
            if y0 < 0:
                continue
            A = np.zeros_like(blk)
            src = img[y0:y0 + rows, max(0, x0):x0 + blk.shape[1]]
            A[:src.shape[0], max(0, -x0):max(0, -x0) + src.shape[1]] = src
            n = int((A != blk).sum())
            if best is None or n < best[0]:
                best = (n, dy, dx)
    return rt + best[1], rc + best[2]


def pelvis_x(axl, img):
    """x of the pelvis: centroid of the pelvis plate color within rows 24-44 below the head top."""
    m = img != 0
    top = int(np.where(m.any(1))[0].min())
    idx = [tuple(c) for c in axl.cols].index(PELVIS_RGB) + 1
    xs = np.where(img[top + 24:top + 44] == idx)[1]
    return float(xs.mean())


def pelvis_offset(axl):
    """pelvis - feet anchor of the first walk picture (standing), so idle -> walk does not jump."""
    if not hasattr(axl, 'pelvis_off'):
        ref = axl.frame(PELVIS_REF)
        axl.pelvis_off = pelvis_x(axl, ref) - anchor(ref != 0)[1]
    return axl.pelvis_off


def sphere_center(img):
    ys, xs = np.where(img != 0)
    return np.array([(ys.min() + ys.max() + 1) / 2, (xs.min() + xs.max() + 1) / 2])


def sphere_offset(axl):
    if not hasattr(axl, 'sphere_off'):
        last = axl.frame(INTRO_SPHERES[-1])
        axl.sphere_off = np.array(anchor(last != 0), float) - sphere_center(last)
    return axl.sphere_off


def axl_anchor(axl, key):
    """Feet anchor of an Axl picture. Pictures listed in FEET_REF are chained to their reference picture's
    anchor by matching the feet pixel for pixel, so a sequence keeps its feet still instead of following a
    per-picture estimate (which moved the body 1-2 px between pictures)."""
    img = axl.frame(key)
    if key in PELVIS_KEYS:
        # X keeps its pelvis still while walking: do the same (x), feet on the ground (y)
        return bottom(img), pelvis_x(axl, img) - pelvis_offset(axl)
    if key in INTRO_SPHERES[1:]:
        # spheres grow and shrink: keep their center still; the last one (Axl standing inside) sets the offset
        return sphere_center(img) + sphere_offset(axl)
    ref = FEET_REF.get(key)
    if ref is None:
        return anchor(img != 0)
    rimg = axl.frame(ref)
    rb, rc = axl_anchor(axl, ref)
    return rb + bottom(img) - bottom(rimg), rc - feet_offset(img, rimg)


def tiles_parts(img, ox, oy):
    """Cut an indexed image placed with its top-left at (ox, oy) (object coordinates) into 16x16 tiles."""
    h, w = img.shape
    tiles, parts = [], []
    for ty in range(0, h, 16):
        for tx in range(0, w, 16):
            t = np.zeros((16, 16), np.uint8)
            blk = img[ty:ty + 16, tx:tx + 16]
            t[:blk.shape[0], :blk.shape[1]] = blk
            if not t.any():
                continue
            n = len(tiles)
            tiles.append(t)
            parts.append((n >> 8 & 3, n & 0xff, ox + tx, oy + ty))
    return tiles, parts


# --- X frames ------------------------------------------------------------------------------------------
def x_frame_mask(t2, sec2, t3, sec3, f):
    """Opaque mask of an X frame in object coordinates: (mask, x_min, y_min)."""
    img, n = ss.tiles_image(t2, sec2, f)
    cnt, off4 = struct.unpack_from('<HH', t3, sec3 + 4 * f)
    if img is None or cnt == 0:
        return None
    S = 256
    canvas = np.zeros((S, S), bool)
    for i in range(cnt):
        attr, lo, x, y = struct.unpack_from('<BBbb', t3, sec3 + off4 * 4 + 4 * i)
        t = lo | (attr & 3) << 8
        if t >= n:
            continue
        tile = img[(t // 16) * 16:(t // 16) * 16 + 16, (t % 16) * 16:(t % 16) * 16 + 16] != 0
        if attr & 0x40:
            tile = tile[:, ::-1]
        canvas[S // 2 + y:S // 2 + y + 16, S // 2 + x:S // 2 + x + 16] |= tile
    return (canvas, -S // 2, -S // 2) if canvas.any() else None


def convert_bank(axl, t2, t3, s2, s3, xmap, nframes):
    sec2, sec3 = ss.sections(t2)[s2], ss.sections(t3)[s3]
    frame_tiles, frame_parts, stats = [], [], []
    tile_cache = {}
    for f in range(nframes):
        cut = None
        if s2 == 0:
            cut = axl.cuts.get('A%d' % f) or (axl.cuts.get('A%d' % BUSTER_TWIN[f]) if f in BUSTER_TWIN else None)
        if cut:
            # hand cut in the viewer: rect on the sheet, its top-left at (sx, sy) from the object origin on screen
            # (facing right); in game data (facing left) the mirrored picture's top-left is (-(sx + w), sy)
            img = axl.crop(cut['rect'])
            tl, ps = tiles_parts(img, -(cut['sx'] + img.shape[1]), cut['sy'])
            assert len(tl) <= MAX_TILES, 'A%d: hand cut needs %d tiles (max %d)' % (f, len(tl), MAX_TILES)
            assert all(-128 <= x <= 127 and -128 <= y <= 127 for _, _, x, y in ps), ('A%d off range' % f)
            frame_tiles.append(tl)
            frame_parts.append(ps)
            stats.append(len(tl))
            continue
        key = xmap.get(f)
        if (s2 == 0 and f >= X_FRAMES and key is None) or (s2 != 0 and f in BLANK_B) or (s2 == 0 and f in BLANK_A):
            assert f not in axl.slots, 'added frame %d has no cut' % f
            frame_tiles.append([])        # unused frame number
            frame_parts.append([])
            continue
        xm = x_frame_mask(t2, sec2, t3, sec3, ANCHOR_FROM.get(f, f) if s2 == 0 else ZIP_REF.get(f, f))
        if key is None or xm is None:
            # keep X's own frame
            img, n = ss.tiles_image(t2, sec2, f)
            tl = [] if img is None else [img[(t // 16) * 16:(t // 16) * 16 + 16, (t % 16) * 16:(t % 16) * 16 + 16]
                                         for t in range(n)]
            cnt, off4 = struct.unpack_from('<HH', t3, sec3 + 4 * f)
            ps = [struct.unpack_from('<BBbb', t3, sec3 + off4 * 4 + 4 * i) for i in range(cnt)]
            frame_tiles.append(tl)
            frame_parts.append(ps)
            continue
        canvas, cx0, cy0 = xm
        img = axl.frame(key)
        top = s2 != 0 and f in ZIP_REF
        mid = s2 == 0 and f in LADDER_MID
        xb, xc = top_anchor(canvas) if top else mid_anchor(canvas) if mid else anchor(canvas)
        if key not in tile_cache:
            tile_cache[key] = tiles_parts(img, 0, 0)
        if len(tile_cache[key][0]) > MAX_TILES:
            print('X frame %d: Axl %s needs %d tiles, keeping X' % (f, key, len(tile_cache[key][0])))
            xmap = dict(xmap); xmap.pop(f)
            img0, n = ss.tiles_image(t2, sec2, f)
            frame_tiles.append([img0[(t // 16) * 16:(t // 16) * 16 + 16, (t % 16) * 16:(t % 16) * 16 + 16]
                                for t in range(n)])
            cnt, off4 = struct.unpack_from('<HH', t3, sec3 + 4 * f)
            frame_parts.append([struct.unpack_from('<BBbb', t3, sec3 + off4 * 4 + 4 * i) for i in range(cnt)])
            continue
        ab, ac = (clamp_anchor(axl, key) if ZIP_REF.get(f) == ZIP_X[0][0] else top_anchor(img != 0)) if top \
            else mid_anchor(img != 0) if mid else axl_anchor(axl, key)
        # hand-tuned per X frame in work/viewer.html (tools/make_viewer.py): key 'A<frame>' (bank A) / 'B<frame>'
        bank = 'A' if s2 == 0 else 'B'
        nk = '%s%d' % (bank, f)
        if nk not in axl.nudge and s2 == 0 and f in BUSTER_TWIN:
            nk = 'A%d' % BUSTER_TWIN[f]   # walking while shooting uses the even twin of the plain frame
        nx, ny = axl.nudge.get(nk, (0, 0))
        ac, ab = ac + nx, ab - ny   # parts move by -nx, +ny (data coords), as drawn in the viewer
        ox = int(np.floor(cx0 + xc - ac + 0.5))  # not round(): banker's rounding turned 1 px into 2 at .5
        oy = int(cy0 + xb - ab)
        if key not in tile_cache:
            tile_cache[key] = tiles_parts(img, 0, 0)
        tl, ps0 = tile_cache[key]
        assert len(tl) <= MAX_TILES, (f, key, len(tl))
        ps = [(a, lo, x + ox, y + oy) for a, lo, x, y in ps0]
        assert all(-128 <= x <= 127 and -128 <= y <= 127 for _, _, x, y in ps), (f, key, ox, oy)
        frame_tiles.append(tl)
        frame_parts.append(ps)
        stats.append(len(tl))
    return frame_tiles, frame_parts, stats


def ram_size(cont):
    return sum((z + 3) & ~3 for t, o, z in datpak.parse(cont) if t < 0x10000)


LOOK = {}   # bosses' looks: section numbers, palettes, sequences (header for src/axl_dna.c)

# Aimed bullets (src/axl_main.c): X's normal shot flies as one 16x16 tile, frames 5-9 of type 3 section 1 (tiles 19-23,
# anim 0x15 of the EXE table 0x8007789c). Rotated copies (90 and 45 degrees) go into tiles of the resident shot art
# (VRAM subfile 0x10206: 11 bands of 64x80 halfwords, tile t = band t // 80, row t % 80 // 16, column t % 16) that are
# empty in the data, used by no frame and empty in every stage dump (31 dumps, 2026-10-24); new frames
# BULLET_ROT_BASE + 5 * d + i (d = 0 up, 1 up-diagonal, 2 down, 3 down-diagonal; down = up flipped vertically)
BULLET_TILES = (19, 20, 21, 22, 23)
BULLET_ROT_TILE = 864
SHOT_VRAM = 0x10206
# X's effect / weapon art sections of type 2 that Axl never decompresses (RAM): every LZ decode from DAT 86's type 2
# was logged (FUN_80018be0 breakpoint, emu/plans/common/plan_axl_fight.lua TRACE_T2) over the 8 DNA Changes, weapon
# switching, Giga, moves and entering stages 1 / 5 (2026-10-24, mods/axl/work/logs/t2trace): these were never read.
# They keep their frame table and tile counts, with all-zero (transparent) tiles: an untested reader gets blank art.
# Kept: 0 / 10 (Axl, White Axl), 2 / 7 / 9 (X's weapon art, streamed on weapon switch), 19 / 20 (boss looks / shots).
BLANK_T2 = (1, 3, 5, 6, 8, 11, 12, 13, 14, 16, 17, 18)


def shot_tile(blob, t):
    w = np.frombuffer(blob, '<u2').reshape(-1, 80, 64)[t // 80][t % 80 // 16 * 16:t % 80 // 16 * 16 + 16, t % 16 * 4:t % 16 * 4 + 4]
    px = np.zeros((16, 16), np.uint8)
    for k in range(4):
        for n in range(4):
            px[:, k * 4 + n] = (w[:, k] >> (4 * n)) & 15
    return px


def put_shot_tile(blob, t, px):
    a = np.frombuffer(blob, '<u2').reshape(-1, 80, 64).copy()
    w = np.zeros((16, 4), np.uint16)
    for k in range(4):
        for n in range(4):
            w[:, k] |= px[:, k * 4 + n].astype(np.uint16) << (4 * n)
    a[t // 80][t % 80 // 16 * 16:t % 80 // 16 * 16 + 16, t % 16 * 4:t % 16 * 4 + 4] = w
    return a.tobytes()


def rotate_tile(px, deg):
    """clockwise by deg about the tile centre; 90 is exact, 45 samples a 4x nearest upscale at block centres"""
    if deg == 90:
        return np.rot90(px, -1).copy()
    big = Image.fromarray(np.kron(px, np.ones((4, 4), np.uint8)), 'L').rotate(-deg, Image.NEAREST)
    return np.array(big)[2::4, 2::4].copy()


def bullet_rotations(sec1, vram):
    """(type 3 section 1 with the rotated frames appended, shot VRAM blob with their tiles, first new frame)"""
    n = struct.unpack_from('<HH', sec1, 0)[1]
    frames = []
    for f in range(n):
        c, o = struct.unpack_from('<HH', sec1, 4 * f)
        frames.append([struct.unpack_from('<BBbb', sec1, o * 4 + 4 * i) for i in range(c)])
    for i, t in enumerate(BULLET_TILES):
        assert frames[5 + i] == [(0, t, -8, -8)], frames[5 + i]
    for k in range(2 * len(BULLET_TILES)):
        assert not shot_tile(vram, BULLET_ROT_TILE + k).any()
    for j, deg in enumerate((90, 45)):
        for i, t in enumerate(BULLET_TILES):
            vram = put_shot_tile(vram, BULLET_ROT_TILE + 5 * j + i, rotate_tile(shot_tile(vram, t), deg))
    rot = lambda j, i, flip: [(((BULLET_ROT_TILE + 5 * j + i) >> 8) | flip, (BULLET_ROT_TILE + 5 * j + i) & 0xff, -8, -8)]
    for j, flip in ((0, 0), (1, 0), (0, 0x80), (1, 0x80)):   # up, up-diagonal, down, down-diagonal
        frames += [rot(j, i, flip) for i in range(len(BULLET_TILES))]
    assert len(frames) <= 256
    return type3_section(frames), vram, n


# hover jet (sheet "Hover", 4 flames right of "Hover + Shoot", facing right: nozzle top-right, trail down-left):
# tiles 874-877 of the shot VRAM (empty after the rotated bullets), shot palette CLUT 0x7802 (read from VRAM dumps:
# not in the container), each flame's nozzle on one common texel. Two per body frame (one per boot), drawn by
# axl_draw while hovering as turned textured quads (src/axl_main.c draw_flame), also with the air aim poses.
FLAME_BOXES = ((1055, 639, 1068, 652), (1077, 639, 1090, 652), (1104, 639, 1114, 652), (1121, 639, 1128, 652))
FLAME_TILE = 874
SHOT_VRAM_AT = (320, 176)   # where subfile 0x10206 is put (docs/memory_map_x6.md VRAM table, dest 6)
FLAME_PAGE = 15
FLAME_UV = (160, 240)   # tile 874's texel in page 15 (column 10, row 15); 875-877 follow
# viewer edits (tools/make_viewer.py, tab "호버 불꽃"): {body frame: [[x, y, degrees], [x, y, degrees]]} of the 2 nozzles
# (one per boot), data coordinates (facing left, y down; + degrees turn clockwise in data = counterclockwise on screen)
FLAME_EDITS = json.load(open(os.path.join(SHEET, 'flame.json'))) if os.path.exists(os.path.join(SHEET, 'flame.json')) else {}
FLAME_BODY = HOVER_FRAMES + AIM_AIR_FRAMES
SHOT_CLUT = 2   # X's shot palette, CLUT 0x7802 (seen in VRAM dumps; not in the container)


def stage_clut(n):
    """16-colour CLUT n (15-bit) of ROCK_X6.DAT entry 1's palette subfile (type 5), the CLUTs every stage loads:
    2 = X's shots (SHOT_CLUT), 17 = the HUD (lc2_build.HUD_CLUT)"""
    with open(os.path.join(SRC, 'ROCK_X6.DAT'), 'rb') as f:
        s, z = struct.unpack_from('<II', f.read(16), 8)
        f.seek(s * 2048)
        b = f.read(z)
    t, o, size = datpak.parse(b)[0]
    assert t == 5 and size >= 32 * (n + 1), (t, size)
    return struct.unpack_from('<16H', b, o + 32 * n)


def shot_pal():
    """X's shot palette as RGB: (0, 0, 0) and its 5 colours"""
    return tuple(((v & 31) << 3, (v >> 5 & 31) << 3, (v >> 10 & 31) << 3) for v in stage_clut(SHOT_CLUT)[:6])


def hover_flames(vram, axl, tl, ps):
    """(shot VRAM blob with the 4 flame tiles, quads [frame][flame][corner] = (x, y)): the flames are drawn as
    textured quads (src/axl_main.c draw_flame), so each can turn; corners in data coordinates (facing left) from
    the object origin, for the tile's (0, 0), (15, 0), (0, 15), (15, 15) texels"""
    pal = np.array(shot_pal()[1:])
    imgs = []
    for x0, y0, x1, y1 in FLAME_BOXES:
        rgb = axl.im[y0:y1, x0:x1]
        mask = ~(rgb == BG).all(2)
        idx = (((rgb[:, :, None, :] - pal[None, None]) ** 2).sum(3).argmin(2) + 1) * mask
        img = idx[:, ::-1].astype(np.uint8)              # game data faces left: nozzle top-left, trail down-right
        ys, xs = np.nonzero(img)
        k = np.lexsort((xs, ys))[0]
        imgs.append((img, int(xs[k]), int(ys[k])))
    noz = (max(nx for _, nx, _ in imgs), max(ny for _, _, ny in imgs))   # every flame's nozzle on this texel
    for i, (img, nx, ny) in enumerate(imgs):
        t = FLAME_TILE + i
        assert not shot_tile(vram, t).any()
        vx, vy = SHOT_VRAM_AT[0] + 64 * (t // 80), SHOT_VRAM_AT[1] + t % 80 // 16 * 16
        assert vx // 64 + (vy >= 256) * 16 == FLAME_PAGE
        assert ((t % 16) * 16, (vy % 256)) == (FLAME_UV[0] + 16 * i, FLAME_UV[1])
        tile = np.zeros((16, 16), np.uint8)
        oy, ox = noz[1] - ny, noz[0] - nx
        assert ox >= 0 and oy >= 0 and oy + img.shape[0] <= 16 and ox + img.shape[1] <= 16
        tile[oy:oy + img.shape[0], ox:ox + img.shape[1]] = img
        vram = put_shot_tile(vram, t, tile)
    quads, spots = [], []
    for f in FLAME_BODY:
        S = 256
        canvas = np.zeros((S, S), bool)
        for attr, lo, x, y in ps[f]:
            tile = tl[f][lo | (attr & 3) << 8] != 0
            if attr & 0x40:
                tile = tile[:, ::-1]
            if attr & 0x80:
                tile = tile[::-1]
            canvas[S // 2 + y:S // 2 + y + 16, S // 2 + x:S // 2 + x + 16] |= tile
        # two boots: the lowest pixel (front leg) and, as a start (the back boot hides behind the front leg in these
        # pictures), 5 px toward his back (data +x) and 2 px up; the viewer tab places and turns them
        ys, xs = np.nonzero(canvas)
        by = ys.max()
        bx = int(round(xs[ys == by].mean())) - S // 2
        two = FLAME_EDITS.get(str(f), [[bx, int(by) - S // 2 + 1, 0], [bx + 5, int(by) - S // 2 - 1, 0]])
        spots.append([[*p[:2], p[2] if len(p) > 2 else 0] for p in two])
        q = []
        for sx, sy, *deg in two:
            a = np.radians(deg[0] if deg else 0)
            c, sn = np.cos(a), np.sin(a)
            for cx, cy in ((0, 0), (15, 0), (0, 15), (15, 15)):
                dx, dy = cx - noz[0], cy - noz[1]
                q.append((int(round(sx + dx * c - dy * sn)), int(round(sy + dx * sn + dy * c))))
        assert all(-128 <= v <= 127 for p in q for v in p)
        quads.append(q)
    return vram, quads, noz, spots


def blank_section(sec):
    """same frames and tile counts, every tile transparent (one shared stream per tile count)"""
    if not sec:
        return sec
    n = (struct.unpack_from('<I', sec, 0)[0] & 0xfffff) // 4
    zero = np.zeros((16, 16), np.uint8)
    lists = {}
    return type2_section([lists.setdefault(c, [zero] * c)
                          for c in (struct.unpack_from('<I', sec, 4 * f)[0] >> 20 for f in range(n))])


def build_container(axl, dat):
    sec, size = struct.unpack_from('<II', dat, 8 * ENTRY)
    cont = dat[sec * 2048:sec * 2048 + size]
    subs = [(t, cont[o:o + z]) for t, o, z in datpak.parse(cont)]
    t2 = [b for t, b in subs if t == 2][0]
    t3 = [b for t, b in subs if t == 3][0]
    banks = []
    for s2, s3, armor, xmap in ((0, 0, 5, build_map_a()), (10, 18, 19, build_map_b())):
        n = (struct.unpack_from('<I', t2, ss.sections(t2)[s2])[0] & 0xfffff) // 4
        if s2 == 0:
            assert all(f < USER_SLOT_END for f in axl.slots), 'viewer frames must stay below %d' % USER_SLOT_END
            n = 256   # added frames: viewer 228-239, hover 240-241, aim poses 242 + 247-254 (one byte; 255 = stream mark)
        else:
            assert n == ZIP_AIM_FRAMES, n
            n = ZIP_FRAMES_B   # rope aims
        banks.append((s2, s3, armor, convert_bank(axl, t2, t3, s2, s3, xmap, n)))
    pal = axl.palette555()
    # DNA Change looks: the 8 bosses' poses as one more tile / parts section each (LOOK_T2 / LOOK_T3)
    xm, _, cy0 = x_frame_mask(t2, ss.sections(t2)[0], t3, ss.sections(t3)[0], 0)
    pool = boss_look.pool_frames(dat, int(np.nonzero(xm)[0].max()) + cy0)
    LOOK['pals'], LOOK['seqs'], LOOK['poses'], LOOK['chunk0'] = pool['pals'], pool['seqs'], pool['poses'], pool['chunk0']
    # and their projectiles: art frame k = boss k's projectile tiles (FX_T2), the pictures (FX_T3), anims (FX_ANIM_BASE+)
    fx_arts, fx_parts, LOOK['fx'] = boss_look.fx_frames(dat)
    t3_sec1, shot_vram, LOOK['bullet'] = bullet_rotations(split_sections(t3)[1], [b for t, b in subs if t == SHOT_VRAM][0])
    shot_vram, LOOK['flame_quad'], LOOK['flame_noz'], spots = hover_flames(shot_vram, axl, *banks[0][3][:2])
    # crouch shot start (src/axl_main.c tip_crouch): the gun end of frame 212 as built (viewer nudges move it too):
    # forward-most column (data faces left: smallest x), middle of the rows there
    # (and the wall shot's, tip_wall: frame 62, sheet "Shoot + Wall Cling")
    tl, ps = banks[0][3][:2]
    S = 256

    def gun_tip(f, d=0, bank=0):
        """d = 0: forward, -1 / 1: the up / down diagonal (the outermost pixels along it, their middle)"""
        tl, ps = banks[bank][3][:2]
        canvas = np.zeros((S, S), bool)
        for attr, lo, x, y in ps[f]:
            t = tl[f][lo | (attr & 3) << 8] != 0
            canvas[S // 2 + y:S // 2 + y + 16, S // 2 + x:S // 2 + x + 16] |= t[:, ::-1] if attr & 0x40 else t
        ys, xs = np.nonzero(canvas)
        if d == 0:
            front = ys[xs <= xs.min() + 3]
            return S // 2 - int(xs.min()), int(round((front.min() + front.max()) / 2)) - S // 2
        score = -xs + d * ys
        far = score >= score.max() - 2
        return S // 2 - int(round(xs[far].mean())), int(round(ys[far].mean())) - S // 2
    LOOK['tip_crouch'], LOOK['tip_wall'] = gun_tip(212), gun_tip(62)
    # rope: per pose (up-diagonal, forward, down-diagonal)
    LOOK['tip_rope'] = [(gun_tip(ZIP_AIM_FRAMES + 2 * p, -1, 1), gun_tip(x[2], 0, 1), gun_tip(ZIP_AIM_FRAMES + 2 * p + 1, 1, 1))
                        for p, x in enumerate(ZIP_X)]
    print('rope tips', LOOK['tip_rope'])
    # for the viewer tab (tools/make_viewer.py): the spots this build uses and the common nozzle texel
    json.dump({'spots': dict(zip(map(str, FLAME_BODY), spots)), 'noz': LOOK['flame_noz']},
              open(os.path.join(HERE, '..', 'work', 'build', 'flame_spots.json'), 'w'))
    # Axl's X8 sounds in place of X's samples (tools/sound.py, docs/sound.md)
    snd, seen = sound.rebuild_cached(subs), {}
    out = []
    for t, b in subs:
        n = seen[t] = seen.get(t, -1) + 1
        if (t, n) in snd:
            b = snd[(t, n)]
        elif t == SHOT_VRAM:
            b = shot_vram
        elif t == 2:
            s = split_sections(b)
            for s2, s3, armor, (tl, ps, st) in banks:
                # identical tile lists share one LZ stream (type2_section dedupes by object identity)
                uniq = {}
                tl = [uniq.setdefault(tuple(x.tobytes() for x in tt), tt) for tt in tl]
                s[s2] = type2_section(tl)
            for i in BLANK_T2:
                s[i] = blank_section(s[i])
            LOOK['t2'] = len(s)
            s.append(type2_section(pool['chunks']))
            LOOK['fx_t2'] = len(s)
            s.append(type2_section(fx_arts))
            b = join_sections(s)
        elif t == 3:
            s = split_sections(b)
            s[1] = t3_sec1
            for s2, s3, armor, (tl, ps, st) in banks:
                s[s3] = type3_section(ps)
                s[armor] = type3_section([[] for _ in range(len(ps))])
            LOOK['t3'] = len(s)
            s.append(type3_section(pool['parts']))
            LOOK['map'], LOOK['nmaps'] = len(s), len(pool['maps'])
            # seq frames: [count, spawn frame (atk only), (duration, pose) ...] for boss k, loop q (idle, fire, atk)
            seqf = []
            for k in range(9):
                for q, sq in enumerate(pool['seqs'].get(k, ([], [], []))):
                    seqf.append(bytes([len(sq), pool['atk_at'][k] if q == 2 else 0] + [v for e in sq for v in e]))
            s.append(boss_look.bytes_section(pool['maps'] + [pool['pals'][k].astype('<u2').tobytes() for k in range(9)]
                                             + [b''.join(bytes((a, n, l, 0)) for a, n, l in pool['poses'])] + seqf))
            # the pose cache (axl_dna.c pose_draw): a tag word, then the gathered tiles of the shown pose's slices
            LOOK['cache'] = len(s)
            s.append(bytes(4 + boss_look.SLICE_MAX * 4096))
            LOOK['fx_t3'] = len(s)
            s.append(type3_section(fx_parts))
            b = join_sections(s)
        elif t == 9:
            b = bytearray(b)
            for k in BODY_PALETTES:  # deco palettes stay X's: kept X frames (entry beam) use them
                b[32 * k:32 * k + 32] = pal.tobytes()
            # X's shot / charge flash palettes: Axl's own colors (he fires every few frames: a tint would flicker)
            for k in FLASH_PALETTES:
                b[32 * k:32 * k + 32] = pal.tobytes()
            # READY (CLUT 0x7801: palettes 60 -> 64 -> 68 while it fades in, then X's deco 57 / 69 once X is in):
            # greys of the same brightness (user: black / grey like Axl); kept X frames using 57 / 69 turn grey too
            for k in READY_PALETTES:
                c = np.frombuffer(bytes(b[32 * k:32 * k + 32]), '<u2').astype(np.int32)
                lum = ((c & 31) * 30 + (c >> 5 & 31) * 59 + (c >> 10 & 31) * 11) // 100
                g = np.clip(lum * 85 // 100, 1, 31)
                grey = np.where((c & 0x7fff) == 0, c, (c & 0x8000) | g | g << 5 | g << 10)
                b[32 * k:32 * k + 32] = grey.astype('<u2').tobytes()
            b = bytes(b)
        out.append((t, b))
    for s2, s3, armor, (tl, ps, st) in banks:
        print('bank s%d: %d Axl frames, tiles/frame max %d mean %.1f' % (s2, len(st), max(st), sum(st) / len(st)))
    old2 = len(t2)
    new2 = len([b for t, b in out if t == 2][0])
    print('type 2: %d -> %d bytes' % (old2, new2))
    new = datpak.build(out)
    # RAM budget: the RAM subfiles (type < 0x10000; VRAM / sound ones are uploaded, not kept) are stacked from
    # 0x80103000 before the stage's (docs/memory_map_x6.md). The stage-start maximum 0x801E5D14 was seen with armor 1:
    # DAT 87's RAM subfiles are the measured fit, so normal X's may grow up to that.
    ram = ram_size(new)
    sec87, size87 = struct.unpack_from('<II', dat, 8 * 87)
    limit = ram_size(dat[sec87 * 2048:sec87 * 2048 + size87])
    print('RAM subfiles %d bytes (X %d, armor-1 limit %d)' % (ram, ram_size(cont), limit))
    assert ram <= limit, 'RAM subfiles over budget (%d > %d)' % (ram, limit)
    return new


# --- X overlay (ROCK_X6.BIN entry 0, loaded at 0x801ea000 including its 4-byte id) ----------------------
OVL_BASE = 0x801ea000
EXT_ENTRY = 41             # ROCK_X6.BIN entry 41: 4 bytes, unused (no loader references it)
EXT_BASE, EXT_END = 0x801f8400, 0x801fe800   # entry 41 is loaded here; task stacks start at 0x801fe800
# Axl's .bss: free RAM between the boss / event overlay window's largest use (chunk 44 ends 0x80102218) and the
# player container (0x80103000), docs/research-x6-ram-free-space-20261024.md
BSS_BASE, BSS_END = 0x80102300, 0x80103000
TEXT2_ENTRY = 42           # ROCK_X6.BIN entry 42 (4 bytes, unused like 41): more code, loaded after .bss (axl_us.ld)
EXT_ID = b'AXL1'
TABLE_BASE = EXT_BASE + 4
X_LOAD_CALL = 0x80013cdc   # FUN_80013bf0: jal FUN_80016858(0, 0x801ea000) (X overlay)
BIN_LOAD = 0x80016858
LOAD_WAIT = 0x8001494c
ANIM_TABLE = 0x801f4ad4    # X's table of 193 script pointers (obj+0x30); left untouched
N_ANIMS = 193
IDLE_SCRIPT = 0x801f416c   # shared by 46 idle-type anims (0, 5, 9, 65-80, ...), 14 entries
ENTRY_ANIM = 2             # stage entry: beam, sphere, then the landing pose
ENTRY_ORIG_LEN = 18
WALK_ANIMS = ((8, 0), (120, 0), (38, 2))   # (anim, first walk-loop entry)
# Axl breathes over 6 pictures (IDLE_DUR frames each) in a loop.
# Blink (user: looks odd mid-breath, left out for now): the sheet's blink pictures are breathing pictures 4-6
# with the eyes closed (0:22 = 4:3, 4:6 = 4:4, 4:7 = 4:5, 2-5 pixels differ), so a blink would swap those in.
IDLE_DUR = 12   # X used 11/13
ENTRY_POSE_DUR = (6, 4, 4, 3, 3, 3, 3, 3, 3, 4, 4, 6, 33)
ENTRY_SPHERE_DUR = (3, 3, 3, 3, 2, 2, 2, 2, 3, 4)   # X's part was 23 frames
# X's buster twins (odd frames plain, even frames buster out); Axl uses the same pictures for both, so an edited
# animation is used for its twin too (Axl's own table only)
TWIN_ANIMS = {7: 119, 8: 120, 10: 122, 11: 123, 12: 124, 13: 125, 14: 126, 15: 127, 16: 128, 17: 129, 20: 132,
              21: 133, 22: 134, 23: 135, 24: 136, 25: 137, 26: 138}
SRC_DIR = os.path.join(HERE, '..', 'src')
C_SOURCES = ['axl_main.c', 'axl_proj.c', 'axl_dna.c', 'axl_dna_bosses.c']
WEAPON_DISPATCH = 0x80071220   # weapon pool handlers by id (FUN_8002012c), original entries -> axl_gen.h
# damage ids whose dispatch entry Axl's projectiles wrap (src/axl_proj.c refuses others: WRAP_IDS in axl_gen.h);
# the Zero overlay load restores them, so Zero never runs the wrapper of an X-only blob that is not loaded
WRAP_IDS = (1, 2, 3, 4, 5, 6, 8, 9, 0x10, 0x12, 0x35)
ZERO_LOAD_CALL = 0x80013cf0  # FUN_80013bf0: jal FUN_80016858(1, 0x801ea000) (Zero overlay)
HOOK_STUB = 0x80022f1c     # dead FUN_80022f1c (952 bytes, no references): resident stubs
GHOST_DRAW_CALL = 0x80022ba8   # the afterimage loop's jal FUN_800232d4 (objects 0x800972f8 + 0x60 * i)
READY_PALETTES = tuple(range(57, 70))   # type 9: READY's fade-in palettes 60-68 and X's deco 57 / 69 (CLUT 0x7801)
SABER_SITE = 0x800315e0    # FUN_800315cc: lb $s0, 1($a3) (attacker id) -> saber-only targets for Axl's shots
STUB_HEAD = 'e8ffbd27'     # addiu $sp, $sp, -0x18 (its first word), checked before overwriting
REMATCH_SLOT, REMATCH_FN = 0x8007acfc, 0x80053578   # boss-room trigger, state 0 (src/axl_main.c axl_rematch)
HOOKS = [  # (call site, original target, C symbol); the stub goes to C only for X (obj+2 == 0)
    (0x800350cc, 0x8003f048, 'axl_frame'),
    (0x80035120, 0x8003ce38, 'axl_post'),   # after the state dispatch and the buster code (FUN_8003ec78)
    (0x80022ac8, 0x800232d4, 'axl_draw'),   # sprite pass, draw X (before the weapon pool is drawn)
    (0x80035128, 0x8003c3c8, 'axl_box'),    # hurt box from the frame (+0x47): frames >= 228 have none in X's tables
]


def box_from(axl):
    """X frame whose hurt box each added frame 228-255 uses (header for src/axl_main.c)."""
    m = {f: x for f, x in axl.slots.items()}
    m.update({f: 41 for f in HOVER_FRAMES + AIM_AIR_FRAMES})
    m.update({f: 0 for f in ROLL_FRAMES + AIM_GROUND_FRAMES})
    return [m.get(f, 0) for f in range(X_FRAMES, 256)]
TEXT_BASE = 0x80010000 - 0x800


def read_script(b, ptr):
    """[(dur, flag, step, frame)] from ptr until the loop / hold entry, and the loop target index (None = hold)."""
    seen, out, p = {}, [], ptr
    while p not in seen and len(out) < 256:
        seen[p] = len(out)
        e = struct.unpack_from('<BBbB', b, p - OVL_BASE)
        out.append(e)
        if e[2] == 0:
            return out, None
        p += e[2] * 4
    return out, seen.get(p)


def linear(entries, loop):
    """Re-link a script as consecutive entries: step 1, last entry loops to `loop` (or holds when None)."""
    n = len(entries)
    return [(d, fl, 1 if i < n - 1 else (0 if loop is None else loop - i), f) for i, (d, fl, st, f) in enumerate(entries)]


def axl_scripts(ovl, axl):
    """Axl's own animation table: {anim: (entries, loop)} for every anim (X's scripts where nothing changed)."""
    table = x_table(ovl)
    scripts = {a: read_script(ovl, table[a]) for a in range(N_ANIMS)}
    users = lambda ptr: [a for a in range(N_ANIMS) if table[a] == ptr]
    # idle: 6 breathing pictures
    idle = [(IDLE_DUR, 0, 1, f) for f in IDLE_FRAMES]
    for a in users(IDLE_SCRIPT):
        scripts[a] = (idle, 0)
    # walk: Axl's 10 pictures at the stride-matched timing (frames and phase flags unchanged)
    for a, first in WALK_ANIMS:
        ents, lp = scripts[a]
        olds = [e[0] for e in ents[first:first + 14]]
        assert olds == [2, 2, 2, 3, 2, 2, 3, 2, 2, 2, 3, 2, 2, 3], (a, olds)
        assert len(WALK_DUR) == 14 and sum(WALK_DUR) == WALK_CYCLE
        ents = list(ents)
        for i, d in enumerate(WALK_DUR):
            e = ents[first + i]
            ents[first + i] = (d, e[1], e[2], e[3])
        scripts[a] = (ents, lp)
    # entry: Axl's 10 beam / sphere pictures in order (X flashes 130/131/130), then 13 landing poses
    ent, _ = scripts[ENTRY_ANIM]
    assert [e[3] for e in ent[:9]] == [127, 128, 129, 130, 131, 130, 132, 134, 133] and len(ent) == ENTRY_ORIG_LEN, ent
    spheres = [(d, 0, 1, f) for d, f in zip(ENTRY_SPHERE_DUR, INTRO_SPHERE_FRAMES)]
    poses = [(d, 1 if i == 5 else 0, 1, f) for i, (d, f) in enumerate(zip(ENTRY_POSE_DUR, INTRO_POSE_FRAMES))]
    scripts[ENTRY_ANIM] = (spheres + poses + [(96, 0, 0, INTRO_POSE_FRAMES[-1])], None)
    # aim poses: one held entry each
    for i, f in enumerate(AIM_GROUND_FRAMES + AIM_AIR_FRAMES):
        scripts[AIM_ANIM_BASE + i] = ([(96, 0, 0, f)], None)
    scripts[HOVER_ANIM] = ([(HOVER_DUR, 0, 1, f) for f in HOVER_FRAMES], 0)
    scripts[ROLL_ANIM] = ([(d, 0, 1, f) for d, f in zip(ROLL_DUR, ROLL_FRAMES)], None)
    scripts[DNA_ANIM] = ([(d, 0, 1, f) for d, f in zip(DNA_DUR, DNA_FRAMES)], None)
    # the bosses' projectiles (frames of LOOK_FX_T3)
    for j, (name, k, loop) in enumerate(LOOK['fx']):
        scripts[FX_ANIM_BASE + j] = ([(d, 0, 1, f) for d, f in loop], None if name in boss_look.FX_HOLD else 0)
    # rope aims (bank B frames; set by src/axl_main.c while X is already on bank B): one held entry each
    for i in range(2 * len(ZIP_X)):
        scripts[FX_ANIM_BASE + len(LOOK['fx']) + i] = ([(96, 0, 0, ZIP_AIM_FRAMES + i)], None)
    # viewer edits: replace the script of that anim, every anim that shared X's script with it, and its twin
    for anim in sorted(axl.anims, key=int):
        a, ed = int(anim), axl.anims[anim]
        if a not in scripts:   # (a PC-only anim edited in the viewer: lc2_hook.EDITABLE_PC, not the PS1 build's)
            continue
        old, _ = scripts[a]
        steps, loop = ed['steps'], ed.get('loop')
        assert steps and all(0 < s['d'] < 256 and 0 <= s['f'] < 256 for s in steps), anim
        new = [(s['d'], old[i][1] if i < len(old) else 0, 1, s['f']) for i, s in enumerate(steps)]
        # X's state code waits for events in the script (e.g. the dash brake, anim 17, returns to idle on the flag-4
        # entry after its 4 pictures): when the edit is shorter, keep the original's remaining entries that carry
        # a flag, showing the edit's last picture; a loop into them is kept, else the edit's loop is used
        tail = [(d, fl, 1, steps[-1]['f']) for d, fl, st, f in old[len(steps):] if fl]
        if tail:
            new += tail
            if loop is None or loop >= len(steps):
                loop = None if loop is None else loop
        group = set(users(table[a])) if a < N_ANIMS else {a}   # Axl-only anims (193 on) have no X script to share
        for tw in [TWIN_ANIMS.get(a)] + [k for k, v in TWIN_ANIMS.items() if v == a]:
            if tw is not None:
                group |= set(users(table[tw]))
        for k in group:
            scripts[k] = (new, loop)
    return scripts


def x_table(ovl):
    return [struct.unpack_from('<I', ovl, ANIM_TABLE + 4 * a - OVL_BASE)[0] for a in range(N_ANIMS)]


def pack_scripts(scripts, base, ovl):
    """Axl table (193 pointers) at `base`, then each script that differs from X's; anims whose script is X's
    point at X's own script. Returns bytes."""
    xt = x_table(ovl)
    keys, order = {}, []
    n_tab = max(scripts) + 1
    for a in range(n_tab):
        ents, lp = scripts[a]
        k = (tuple(ents), lp)
        x_ents, x_lp = read_script(ovl, xt[a]) if a < N_ANIMS else (None, None)
        if x_ents is not None and list(ents) == list(x_ents) and lp == x_lp:
            keys.setdefault(k, xt[a])
            continue
        if k not in keys:
            keys[k] = None
            order.append(k)
    p = base + 4 * n_tab
    for k in order:
        keys[k] = p
        p += 4 * len(k[0])
    out = b''.join(struct.pack('<I', keys[(tuple(scripts[a][0]), scripts[a][1])]) for a in range(n_tab))
    assert all(keys[k] for k in keys)
    for ents, lp in order:
        out += b''.join(struct.pack('<BBbB', d, fl, st, f) for d, fl, st, f in linear(list(ents), lp))
    return out


def asm(src, addr):
    import keystone
    ks = keystone.Ks(keystone.KS_ARCH_MIPS, keystone.KS_MODE_MIPS32 + keystone.KS_MODE_LITTLE_ENDIAN)
    return bytes(ks.asm(src, addr)[0])


def gen_header(workdir, axl):
    """workdir/axl_gen.h: the build's constants for the C code (PS1 src/ and the LC2 DLL, lc2_hook.py)"""
    with open(os.path.join(workdir, 'axl_gen.h'), 'w') as h:
        h.write('/* generated by build_axl.py */\n#ifndef AXL_GEN_H\n#define AXL_GEN_H\nstatic const unsigned char box_from[%d] = {%s};\n'
                % (256 - X_FRAMES, ', '.join(map(str, box_from(axl)))))
        exe = open(os.path.join(SRC, 'SLUS_013.95'), 'rb').read()
        o = WEAPON_DISPATCH - TEXT_BASE
        h.write('#define WEAPON_ENTRIES %s\n' % ', '.join('0x%08x' % v for v in struct.unpack_from('<64I', exe, o)))
        mask = sum(1 << i for i in WRAP_IDS)
        h.write('#define WRAP_IDS_LO 0x%08xu\n#define WRAP_IDS_HI 0x%08xu\n' % (mask & 0xffffffff, mask >> 32))
        h.write('static const unsigned short white_pal[16] = {%s};\n' % ', '.join('0x%04x' % v for v in axl.white555()))
        h.write('static const unsigned char body_pals[] = {%s};\n'
                % ', '.join(map(str, sorted(set(BODY_PALETTES) | set(FLASH_PALETTES)))))
        h.write('#define LOOK_T2 %d\n#define LOOK_T3 %d\n#define LOOK_MAP %d\n#define LOOK_SEQ %d\n#define LOOK_SLICE_MAX %d\n'
                % (LOOK['t2'], LOOK['t3'], LOOK['map'], boss_look.SEQ_MAX, boss_look.SLICE_MAX))
        # LOOK_MAP section frames: slice maps, then the 9 bosses' palettes (64 colors), then the pose table
        # {first slice, slices, lift, 0} (data in DAT 86, not in the code area)
        h.write('#define LOOK_PAL0 %d\n#define LOOK_POSES %d\n#define LOOK_SEQ0 %d\n#define LOOK_CACHE %d\n'
                % (LOOK['nmaps'], LOOK['nmaps'] + 9, LOOK['nmaps'] + 10, LOOK['cache']))
        h.write('static const unsigned char look_chunk0[9] = {%s};\n' % ', '.join(map(str, LOOK['chunk0'])))
        h.write('#define FX_T2 %d\n#define FX_T3 %d\n' % (LOOK['fx_t2'], LOOK['fx_t3']))
        h.write('#define BULLET_ROT_BASE %d\n' % LOOK['bullet'])
        h.write('#define FLAME_PAGE %d\n' % FLAME_PAGE)
        h.write('#define DNA_POSE_LEN %d\n' % LOOK['dna_pose'])
        h.write('static const signed char tip_crouch[2] = {%d, %d};\n' % LOOK['tip_crouch'])
        h.write('static const signed char tip_wall[2] = {%d, %d};\n' % LOOK['tip_wall'])
        h.write('static const signed char tip_rope[%d][3][2] = {%s};\n' % (len(ZIP_X), ', '.join(
            '{%s}' % ', '.join('{%d, %d}' % t for t in p) for p in LOOK['tip_rope'])))
        h.write('#define ROPE_AIM_ANIM %d\n' % (FX_ANIM_BASE + len(LOOK['fx'])))
        h.write('#define FLAME_U0 %d\n#define FLAME_V0 %d\nstatic const unsigned char flame_body[%d] = {%s};\n'
                % (*FLAME_UV, len(FLAME_BODY), ', '.join(map(str, FLAME_BODY))))
        h.write('static const signed char flame_quad[%d][8][2] = {%s};\n'
                % (len(FLAME_BODY), ', '.join('{%s}' % ', '.join('{%d, %d}' % p for p in q) for q in LOOK['flame_quad'])))
        for j, (name, k, loop) in enumerate(LOOK['fx']):
            h.write('#define FX_%s %d\n' % (name, FX_ANIM_BASE + j))
        h.write('#endif\n')


def build_c(workdir, table_addr, axl, code_base):
    """Compile and link src/*.c at code_base with axl_anim_table = table_addr; returns (blob, symbols)."""
    import subprocess
    import elfbin
    from x6_sprites import C_FLAGS, ZIG
    gen_header(workdir, axl)
    objs = []
    for src in C_SOURCES:
        obj = os.path.join(workdir, src + '.o')
        # -Os for room in the code area (the 8-boss regression showed no extra lag, 2026-10-25)
        flags = [f if f != '-O2' else '-Os' for f in C_FLAGS]
        subprocess.run(ZIG + ['cc'] + flags + ['-I', workdir, '-c', os.path.join(SRC_DIR, src), '-o', obj], check=True)
        objs.append(obj)
    ld = os.path.join(workdir, 'axl_us.ld')
    open(ld, 'w').write(open(os.path.join(SRC_DIR, 'axl_us.ld')).read()
                        .replace('__ORIGIN__', hex(code_base)).replace('__TABLE__', hex(table_addr))
                        .replace('__BSS__', hex(BSS_BASE)))
    elf = os.path.join(workdir, 'axl.elf')
    subprocess.run(ZIG + ['ld.lld', '-T', ld, '--no-check-sections', '-o', elf] + objs, check=True)
    blob, lo = elfbin.flat_binary(elf, ('.text', '.rodata', '.data'))   # .bss lives at BSS_BASE, zeroed at each load
    LOOK['text2'] = elfbin.flat_binary(elf, ('.text2',))[0]
    assert lo >= code_base
    blob = b'\0' * (lo - code_base) + blob
    syms = elfbin.symbols(elf)
    assert BSS_BASE <= syms['__bss_start'] <= syms['__bss_end'] <= BSS_END, 'bss: %#x-%#x' % (syms['__bss_start'], syms['__bss_end'])
    print('bss %#x-%#x (%d bytes, limit %#x)' % (syms['__bss_start'], syms['__bss_end'], syms['__bss_end'] - syms['__bss_start'], BSS_END))
    assert syms['__bss_end'] <= syms['__text2_start'] and syms['__text2_end'] <= BSS_END, \
        'text2: %#x-%#x' % (syms['__text2_start'], syms['__text2_end'])
    print('text2 %#x-%#x (%d bytes, limit %#x)' % (syms['__text2_start'], syms['__text2_end'],
                                                   syms['__text2_end'] - syms['__text2_start'], BSS_END))
    text = [s for s in elfbin._sections(open(elf, 'rb').read()) if s['name'] == '.text'][0]
    check_code(blob[:text['addr'] + text['size'] - code_base], code_base)   # code only, not the data after it
    check_code(LOOK['text2'], syms['__text2_start'])
    return blob, syms


def check_code(blob, base):
    """No MIPS II+ instruction, and no write to $gp/$k0/$k1 (the game keeps $gp; $k0/$k1 are the kernel's)."""
    import capstone
    from x6_sprites import NOT_MIPS1
    md = capstone.Cs(capstone.CS_ARCH_MIPS, capstone.CS_MODE_MIPS32 + capstone.CS_MODE_LITTLE_ENDIAN)
    md.skipdata = True
    for i in md.disasm(blob, base):
        if i.mnemonic in NOT_MIPS1:
            raise SystemExit(f'{i.mnemonic} (not MIPS I) at {i.address:08x}')
        dst = i.op_str.split(',')[0].strip()
        if dst in ('$gp', '$k0', '$k1') and i.mnemonic not in ('sw', 'sh', 'sb', 'beq', 'bne'):
            raise SystemExit(f'writes {dst} at {i.address:08x}: {i.mnemonic} {i.op_str}')


def build_ext(ovl, axl, workdir, lc2_only=False):
    """Entry 41 = id, Axl's animation table + scripts at TABLE_BASE (fixed: a savestate's player object keeps
    pointing at it when only the code changes), then the C code; returns (blob, symbols). lc2_only: the scripts
    (axl_scripts.json) and axl_gen.h only, no code (returns None)."""
    scripts = axl_scripts(ovl, axl)
    # the same scripts for the Legacy Collection 2 hook DLL (mods/axl/lc2/): {anim: [entries, loop]}
    json.dump({a: [[list(e) for e in ents], lp] for a, (ents, lp) in scripts.items()},
              open(os.path.join(workdir, 'axl_scripts.json'), 'w'))
    data = pack_scripts(scripts, TABLE_BASE, ovl)
    LOOK['dna_pose'] = sum(e[0] for e in scripts[DNA_ANIM][0])   # the change pose's length (viewer may edit it)
    assert LOOK['dna_pose'] < 256
    if lc2_only:
        gen_header(workdir, axl)
        return None
    code_base = (TABLE_BASE + len(data) + 15) & ~15
    blob, syms = build_c(workdir, TABLE_BASE, axl, code_base)
    out = EXT_ID + data
    out += b'\0' * (code_base - EXT_BASE - len(out)) + blob
    assert EXT_BASE + len(out) <= EXT_END, 'extension region full: %#x > %#x' % (EXT_BASE + len(out), EXT_END)
    print('Axl table at %#x, C code %#x bytes at %#x, end %#x (limit %#x)' % (
        TABLE_BASE, len(blob), code_base, EXT_BASE + len(out), EXT_END))
    return out, syms


def jal(target):
    return 0x0C000000 | ((target >> 2) & 0x3ffffff)


def patch_exe(exe, syms):
    """Resident stubs in dead FUN_80022f1c: 'if (obj[2] == 0) goto C; else goto original' for each hook."""
    b = bytearray(exe)
    o = HOOK_STUB - TEXT_BASE
    assert b[o:o + 4] == bytes.fromhex(STUB_HEAD), 'unexpected bytes at %08x' % HOOK_STUB
    addr = HOOK_STUB
    # X overlay load -> wait (the read is asynchronous; FUN_80013de8 waits after each load too) -> load entry 41;
    # FUN_80013bf0 itself waits for the second one
    so = X_LOAD_CALL - TEXT_BASE
    assert struct.unpack_from('<I', b, so)[0] == jal(BIN_LOAD), 'unexpected word at %08x' % X_LOAD_CALL
    # keystone fills every delay slot with a nop itself: arguments are set before each jal
    # then zero Axl's .bss (BSS_BASE, outside the loaded entry: the load no longer clears it)
    # (and entry 42, the code past .bss: wait for 41, load 42; FUN_80013bf0 waits for that one)
    bs, be, t2 = syms['__bss_start'], syms['__bss_end'], syms['__text2_start']
    code = asm('addiu $sp, $sp, -0x18; sw $ra, 0x10($sp); jal 0x%x; jal 0x%x; addiu $a0, $zero, %d; '
               'lui $a1, 0x%x; ori $a1, $a1, 0x%x; jal 0x%x; '
               'lui $t0, 0x%x; ori $t0, $t0, 0x%x; lui $t1, 0x%x; ori $t1, $t1, 0x%x; '
               'zl: sw $zero, 0($t0); addiu $t0, $t0, 4; bne $t0, $t1, zl; '
               'jal 0x%x; addiu $a0, $zero, %d; lui $a1, 0x%x; ori $a1, $a1, 0x%x; jal 0x%x; '
               'lw $ra, 0x10($sp); addiu $sp, $sp, 0x18; jr $ra'
               % (BIN_LOAD, LOAD_WAIT, EXT_ENTRY, EXT_BASE >> 16, EXT_BASE & 0xffff, BIN_LOAD,
                  bs >> 16, bs & 0xffff, be >> 16, be & 0xffff,
                  LOAD_WAIT, TEXT2_ENTRY, t2 >> 16, t2 & 0xffff, BIN_LOAD), addr)
    assert code[-4:] == b'\0' * 4, 'jr delay slot'
    b[addr - TEXT_BASE:addr - TEXT_BASE + len(code)] = code
    struct.pack_into('<I', b, so, jal(addr))
    addr += (len(code) + 0x1f) & ~0x1f
    # Zero overlay load -> load it, then put back the original dispatch entries of WRAP_IDS
    so = ZERO_LOAD_CALL - TEXT_BASE
    assert struct.unpack_from('<I', b, so)[0] == jal(BIN_LOAD), 'unexpected word at %08x' % ZERO_LOAD_CALL
    orig = struct.unpack_from('<64I', exe, WEAPON_DISPATCH - TEXT_BASE)
    src = 'addiu $sp, $sp, -0x18; sw $ra, 0x10($sp); jal 0x%x; lui $t1, 0x%x; ' % (BIN_LOAD, WEAPON_DISPATCH >> 16)
    for i in WRAP_IDS:
        src += 'lui $t0, 0x%x; ori $t0, $t0, 0x%x; sw $t0, 0x%x($t1); ' % (orig[i] >> 16, orig[i] & 0xffff,
                                                                       (WEAPON_DISPATCH & 0xffff) + 4 * i)
    src += 'lw $ra, 0x10($sp); addiu $sp, $sp, 0x18; jr $ra'
    code = asm(src, addr)
    b[addr - TEXT_BASE:addr - TEXT_BASE + len(code)] = code
    struct.pack_into('<I', b, so, jal(addr))
    addr += (len(code) + 0x1f) & ~0x1f
    # boss rematch: the trigger's state table entry -> stub: character (0x800ccf08) X -> axl_rematch, else original
    so = REMATCH_SLOT - TEXT_BASE
    assert struct.unpack_from('<I', b, so)[0] == REMATCH_FN, 'unexpected word at %08x' % REMATCH_SLOT
    code = asm('lui $t0, 0x800d; lbu $t0, -0x30f8($t0); nop; bnez $t0, z_; j 0x%x; z_: j 0x%x'
               % (syms['axl_rematch'], REMATCH_FN), addr)
    b[addr - TEXT_BASE:addr - TEXT_BASE + len(code)] = code
    struct.pack_into('<I', b, so, addr)
    addr += (len(code) + 0x1f) & ~0x1f
    for site, orig_fn, sym in HOOKS:
        code = asm('lbu $t0, 2($a0); nop; bnez $t0, zero_; j 0x%x; zero_: j 0x%x' % (syms[sym], orig_fn), addr)
        assert len(code) <= 0x20
        b[addr - TEXT_BASE:addr - TEXT_BASE + len(code)] = code
        so = site - TEXT_BASE
        assert struct.unpack_from('<I', b, so)[0] == jal(orig_fn), 'unexpected word at %08x' % site
        struct.pack_into('<I', b, so, jal(addr))
        addr += 0x20
    # saber-only objects (the gaea mod's way, US address): FUN_800315cc (damage of attacker a3 on target t0) reads the
    # attacker id with "lb $s0, 1($a3)" (0x800315e0; $ra is already saved, the jal's delay slot loads $v1). For Axl
    # (armor 4 / 5), his shots (id 0, guard break 9) use the Shadow saber's entry (0x32) when the target's own entry
    # for them does no damage (response -1 / 2 void, 7 erased, 8 deflected) and the saber's does: saber-only blocks
    # break, enemy damage stays the game's. Only $t1-$t4 and $s0 are written.
    so = SABER_SITE - TEXT_BASE
    assert struct.unpack_from('<I', b, so)[0] == 0x80F00001, 'unexpected word at %08x' % SABER_SITE
    nodmg = lambda reg, hit, miss: ''.join('addiu $t4, $zero, %d; beq %s, $t4, %s; ' % (v, reg, hit) for v in (-1, 2, 7)) + \
        'addiu $t4, $zero, 8; bne %s, $t4, %s; ' % (reg, miss)
    src = ('lb $s0, 1($a3); lui $t1, 0x800d; lbu $t1, -0x30d2($t1); nop; addiu $t1, $t1, -4; sltiu $t1, $t1, 2; '   # MIPS I load delay: nop
           'beqz $t1, sb_done; beqz $s0, sb_chk; addiu $t2, $zero, 9; bne $s0, $t2, sb_done; '
           'sb_chk: lw $t2, 0x58($t0); sll $t3, $s0, 1; addu $t3, $t2, $t3; lb $t3, 0($t3); '
           + nodmg('$t3', 'sb_saber', 'sb_done') +
           'sb_saber: lb $t3, 0x64($t2); ' + ''.join('addiu $t4, $zero, %d; beq $t3, $t4, sb_done; ' % v for v in (-1, 2, 7, 8)) +
           'addiu $s0, $zero, 0x32; sb_done: jr $ra')
    code = asm(src, addr)
    assert code[-4:] == b'\0' * 4, 'jr delay slot'
    b[addr - TEXT_BASE:addr - TEXT_BASE + len(code)] = code
    struct.pack_into('<I', b, so, jal(addr))
    addr += (len(code) + 0x1f) & ~0x1f
    assert addr <= HOOK_STUB + 952, 'stubs overflow dead FUN_80022f1c'
    # dash afterimages: their own draw loop calls axl_ghost (src/axl_main.c) instead of FUN_800232d4
    so = GHOST_DRAW_CALL - TEXT_BASE
    assert struct.unpack_from('<I', b, so)[0] == jal(0x800232d4), 'unexpected word at %08x' % GHOST_DRAW_CALL
    struct.pack_into('<I', b, so, jal(syms['axl_ghost']))
    return bytes(b)


def drop_unused_slots(axl, dat):
    """viewer-added frames (slots) that no animation shows get no picture (RAM); the edit file keeps them"""
    LOOK['fx'] = boss_look.fx_frames(dat)[2]
    bb = open(os.path.join(SRC, 'ROCK_X6.BIN'), 'rb').read()
    sec, size = struct.unpack_from('<II', bb, 0)
    used = {e[3] for a, (ents, lp) in axl_scripts(bb[sec * 2048:sec * 2048 + size], axl).items() for e in ents}
    for f in [f for f in axl.slots if f not in used]:
        del axl.slots[f]
        axl.cuts.pop('A%d' % f, None)
        print('slot %d: no animation shows it, not built' % f)


def main(outdir, lc2_only=False):
    C.require('x6_us')
    os.makedirs(os.path.join(outdir, 'patch'), exist_ok=True)
    axl = Axl()
    dat_path = os.path.join(SRC, 'ROCK_X6.DAT')
    dat = open(dat_path, 'rb').read()
    drop_unused_slots(axl, dat)
    cont = build_container(axl, dat)
    open(os.path.join(outdir, 'x86.bin'), 'wb').write(cont)
    import face
    entries = {ENTRY: cont}
    entries.update(face.entries())   # dialogue portraits: Axl / White Axl (tools/face.py, work/face/map*.json)
    if os.environ.get('AXL_FACE_TEST'):
        # test disc only: X's portraits get coordinate-coded texels (tools/face.py measure)
        for e in range(164, 202, 2):
            entries[e], entries[e + 1] = face.coded(os.environ['AXL_FACE_TEST'], (e - 164) // 2)
    open(os.path.join(outdir, 'patch', 'ROCK_X6.DAT'), 'wb').write(patch_dat.patch(dat_path, entries))
    bin_path = os.path.join(SRC, 'ROCK_X6.BIN')
    bb = open(bin_path, 'rb').read()
    sec, size = struct.unpack_from('<II', bb, 0)
    assert struct.unpack_from('<II', bb, 8 * EXT_ENTRY)[1] == 4, 'BIN entry %d is not empty' % EXT_ENTRY
    if lc2_only:
        build_ext(bb[sec * 2048:sec * 2048 + size], axl, outdir, lc2_only=True)
        print('entry %d: container %#x bytes; --lc2: no PS1 code, EXE or disc image' % (ENTRY, len(cont)))
        return
    ext, syms = build_ext(bb[sec * 2048:sec * 2048 + size], axl, outdir)
    open(os.path.join(outdir, 'patch', 'ROCK_X6.BIN'), 'wb').write(patch_dat.patch(bin_path, {EXT_ENTRY: ext, TEXT2_ENTRY: LOOK['text2']}))
    json.dump({k: v for k, v in syms.items() if not k.startswith('.')}, open(os.path.join(outdir, 'syms.json'), 'w'),
              indent=0, sort_keys=True)
    exe = open(os.path.join(SRC, 'SLUS_013.95'), 'rb').read()
    new_exe = patch_exe(exe, syms)
    open(os.path.join(outdir, 'patch', 'SLUS_013.95'), 'wb').write(new_exe)
    # changed EXE words (RAM address -> word) for test plans that inject a build into a savestate
    words = {'%x' % (TEXT_BASE + o): struct.unpack_from('<I', new_exe, o)[0]
             for o in range(0x800, len(exe), 4) if new_exe[o:o + 4] != exe[o:o + 4]}
    json.dump(words, open(os.path.join(outdir, 'exe_words.json'), 'w'))
    out_bin = os.path.join(outdir, 'axl_x6.bin')
    import build_iso
    placed = build_iso.build(SRC_BIN, os.path.join(outdir, 'patch'), out_bin)
    # the game's sector tables (built at boot): BIN 0x800e0b58, DAT 0x800e0398 = {file LBA + entry sector, size};
    # a test that loads an older savestate must rewrite them (emu/plans/common/axl_lib.lua)
    tables = {}
    for name, addr in (('ROCK_X6.BIN', 0x800e0b58), ('ROCK_X6.DAT', 0x800e0398)):
        lba = [v for k, v in placed.items() if k.upper().lstrip('/').startswith(name)][0]
        hdr = open(os.path.join(outdir, 'patch', name), 'rb').read(8 * 247)
        n = 59 if name.endswith('BIN') else 243   # entries the boot code copies (RAM after them is other data)
        tables['%x' % addr] = [[lba + s, z] for s, z in struct.iter_unpack('<II', hdr[:8 * n])]
    json.dump(tables, open(os.path.join(outdir, 'disc_tables.json'), 'w'))
    open(os.path.join(outdir, 'axl_x6.cue'), 'w').write(
        'FILE "axl_x6.bin" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n')
    print('entry %d: container %#x bytes (orig %#x)' % (ENTRY, len(cont), struct.unpack_from('<I', dat, 8 * ENTRY + 4)[0]))
    print('wrote', out_bin)


if __name__ == '__main__':
    main(sys.argv[1], '--lc2' in sys.argv[2:] or not os.path.isdir(SRC_DIR))
