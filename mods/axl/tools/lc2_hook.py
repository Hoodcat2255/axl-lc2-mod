"""Build the Legacy Collection 2 hook DLL (dinput8.dll proxy) that gives normal X Axl's animation scripts.

usage: lc2_hook.py [--release] [--no-skills]   ->  mods/axl/work/lc2/gen/axl_anims.h, axl_pal.h, axl_gen.h (generated),
       mods/axl/work/lc2/axl_data.bin (lc2_data.py pack), mods/axl/work/lc2/dinput8.dll (lc2_dll.py)

The scripts come from the PS1 build (mods/axl/work/build/axl_scripts.json, written by build_axl.build_ext): an anim
whose Axl script equals X's PS1 script keeps the game's own pointer (the player's table 0x03480798 holds exactly
X's 193 PS1 scripts); the others, and Axl's own anims 193+, point at Axl's scripts in the DLL's data file.
A PC entry is the PS1 entry behind a vtable pointer (8 bytes), see re/lc2/codemap.md.
"""
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lc2_fmt as F  # noqa: E402
import lc2_data  # noqa: E402
from lc2_dll import build_dll  # noqa: E402

SCRIPTS = os.path.join(F.ROOT, 'mods', 'axl', 'work', 'build', 'axl_scripts.json')
N_ANIMS = 193
# PC-only anims: 203 (the removed roll's) = aiming straight down standing, frame 243 (lc2_build.PC_EXTRA_FRAMES)
# 229 / 230 = aiming up / down on a wall, frames 244 / 245
# 39 = victory (X's 183-186 fist pump: a boss beaten, an armour part got): the sheet's 9 victory pictures in bank B
# frames 47-55 (lc2_build.VICTORY_B; the DLL switches anim 39 to bank B), order / times from tools/victory.py
# (work/sheet/victory.json, the viewer's victory tab). 245 = the Giga Crush pose (the PS1 build's 183-186 pictures,
# which took X's victory anim 39 there): crouch, the giga poses, arms out held (axl_x.c ANIM_GIGA)
import victory as _victory  # noqa: E402
PC_SCRIPTS = {203: ([(96, 0, 0, 243)], None), 229: ([(96, 0, 0, 244)], None), 230: ([(96, 0, 0, 245)], None),
              39: (_victory.script(47, 39), None),
              245: ([(2, 0, 1, 81), (3, 0, 1, 183), (16, 0, 1, 184), (2, 0, 1, 185), (96, 0, 0, 186)], None)}
# 231.. = the Giga Crush pictures (one held FX frame each: lc2_dna.GIGA_BASE + i; the objects draw from the FX parts,
# axl_x.c giga_look sets GIGA_ANIM + i)
GIGA_ANIM = 231
import lc2_dna as _D  # noqa: E402
for _i in range(_D.GIGA_SPIN + len(_D.GIGA_BURST)):
    PC_SCRIPTS[GIGA_ANIM + _i] = ([(96, 0, 0, _D.GIGA_BASE + _i)], None)
# 183-186 = the ladder aims up / up-diagonal / down-diagonal / down: bank B frames 74-77 (lc2_build.LADDER_AIM_B;
# X's Z-Saber slash anims, bank B in X's bank table: X's anim call switches the bank; axl_x.c ladder aims)
for _i in range(4):
    PC_SCRIPTS[183 + _i] = ([(96, 0, 0, 74 + _i)], None)
# 246.. = the 4 hover flames (one held FX frame each: lc2_dna.FLAME_BASE + i; axl_x.c hover_jets)
FLAME_ANIM = 246
for _i in range(_D.N_FLAMES):
    PC_SCRIPTS[FLAME_ANIM + _i] = ([(96, 0, 0, _D.FLAME_BASE + _i)], None)
# 252..255 = the rope aims of poses 2 / 3 (bank B 70-73): their PS1 numbers ROPE_AIM_ANIM + 4.. (229..232) are the wall
# aims and the Giga Crush pictures here (user report 2026-11-05: Axl vanished shooting up / down on a rope); axl_x.c
# ROPE_AIM
ROPE_AIM_HI = 252
for _i in range(4):
    PC_SCRIPTS[ROPE_AIM_HI + _i] = ([(96, 0, 0, 70 + _i)], None)
# (the copy shot's pictures: axl_proj.c fx_table, not here - anims are a byte and 0-255 are taken)
# 250 = Axl's air dash (axl_x.c hover_after; user report 2026-10-27): anim 16's dash frames without its standing start
# (1, 3: X set his air anim back every frame after an air shot, the dash restarted on them)
AIR_DASH_ANIM = 250
PC_SCRIPTS[AIR_DASH_ANIM] = ([(1, 0, 1, 71), (1, 0, 1, 75), (1, 0, -1, 73)], None)   # (axl_x.c also picks its frame)
# 251 = the dodge roll (user request 2026-10-28: R when not changed, as X7's Axl): the sheet's "Dodge Roll" row is in
# bank A already (build_axl ROLL on frames 219-227, cell n = frame 219 + n); the PS1 order DODGE (dive, tumble, get
# up) - 20 frames, axl_x.c ROLL_LEN
ROLL_ANIM = 251
ROLL_DEFAULT = [(3, 220), (3, 224), (3, 225), (3, 226), (3, 227), (5, 221)]   # (duration, frame)
# the viewer's edit of it (user request 2026-10-29: the roll editable on PC): work/sheet/axl_edits.json anims['251']
# = {steps: [{f, d}], loop} (loop None: the last held, else the entry it loops back to)
EDITABLE_PC = (ROLL_ANIM,)


def _edited(anim, default):
    import json
    path = os.path.join(F.ROOT, 'mods', 'axl', 'work', 'sheet', 'axl_edits.json')
    ed = json.load(open(path)).get('anims', {}).get(str(anim)) if os.path.exists(path) else None
    steps = [(s['d'], s['f']) for s in ed['steps']] if ed and ed.get('steps') else default
    loop = ed.get('loop') if ed else None
    n = len(steps)
    assert all(0 < d < 256 and 0 <= f < 256 for d, f in steps), anim
    return ([(d, 0, 1 if i < n - 1 else (0 if loop is None else loop - i), f) for i, (d, f) in enumerate(steps)],
            None)


PC_SCRIPTS[ROLL_ANIM] = _edited(ROLL_ANIM, ROLL_DEFAULT)


def anims_header():
    import build_axl as A
    rb = open(os.path.join(F.C.X6_US_FS, 'ROCK_X6.BIN'), 'rb').read()
    s, z = struct.unpack_from('<II', rb, 0)
    ovl = rb[s * 2048:s * 2048 + z]
    xt = A.x_table(ovl)
    scripts = {int(a): ([tuple(e) for e in ents], lp) for a, (ents, lp) in json.load(open(SCRIPTS)).items()}
    scripts.update(PC_SCRIPTS)
    n_table = max(scripts) + 1
    ents, start, seen = [], [], {}
    for a in range(n_table):
        e, lp = scripts[a]
        if a < N_ANIMS and (e, lp) == A.read_script(ovl, xt[a]):
            start.append(-1)
            continue
        key = (tuple(e), lp)
        if key not in seen:
            seen[key] = len(ents)
            ents += A.linear(list(e), lp)
        start.append(seen[key])
    lines = ['/* generated by mods/axl/tools/lc2_hook.py from work/build/axl_scripts.json; do not edit */',
             '/* PC script entry = C++ object: vtable 0x031DE36C, then the PS1 bytes (dur, flag, step, frame);',
             ' * step counts entries (8 bytes) as on PS1 (4 bytes) */',
             '/* (axl_ent, AXL_ENT_VT: lc2/axl_data.h) */',
             '#define AXL_N_TABLE %d' % n_table,
             '#define AXL_N_ENTS %d' % len(ents),
             '#define AXL_ROLL_LEN %d   /* the roll anim\'s frames (axl_x.c: the roll lasts as long) */'
             % sum(d for d, *_ in PC_SCRIPTS[ROLL_ANIM][0]),
             'static const axl_ent axl_ents[AXL_N_ENTS] = {']
    lines += ['    {AXL_ENT_VT, %d, %d, %d, %d},' % (d, fl, st, f) for d, fl, st, f in ents]
    lines += ['};', "/* first entry of anim a in axl_ents, -1 = keep the game's own script (X's table 0x03480798) */",
              'static const short axl_start[AXL_N_TABLE] = {']
    lines += ['    ' + ', '.join('%d' % v for v in start[i:i + 16]) + ',' for i in range(0, n_table, 16)]
    lines += ['};']
    # Axl's body palette 0 as the built pl02 COL holds it: White Axl (axl_x.c white_axl) only overwrites a palette
    # blob that holds it (user report 2026-10-27: the gate stage's art broke)
    col = F.Arc(os.path.join(F.BUILT, 'pl02_eng.arc')).get(F.COL_NAME, F.H_COL)
    pal0 = struct.unpack_from('<16H', col, 12)
    os.makedirs(F.GEN, exist_ok=True)
    open(os.path.join(F.GEN, 'axl_pal.h'), 'w').write(
        '/* generated by mods/axl/tools/lc2_hook.py from the built pl02_eng.arc; do not edit */\n'
        'static const unsigned short axl_body_pal[16] = {%s};\n' % ', '.join('0x%04x' % v for v in pal0))
    lines += ['']
    open(os.path.join(F.GEN, 'axl_anims.h'), 'w').write('\n'.join(lines))
    changed = sum(v >= 0 for v in start)
    print('anims from Axl: %d of %d, %d script entries' % (changed, n_table, len(ents)))


def gen_header():
    """the PS1 build's constants (work/build/axl_gen.h: tips, anims) next to the other generated headers"""
    gen = os.path.join(F.ROOT, 'mods', 'axl', 'work', 'build', 'axl_gen.h')
    open(os.path.join(F.GEN, 'axl_gen.h'), 'w').write('/* copied from work/build/axl_gen.h by lc2_hook.py */\n' +
                                                      open(gen).read())


if __name__ == '__main__':
    F.C.require('x6_us')
    anims_header()
    gen_header()
    lc2_data.pack()
    build_dll('--release' in sys.argv, '--no-skills' in sys.argv)
