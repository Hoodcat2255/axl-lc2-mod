"""In-game regression of the Axl DLL on Legacy Collection 2 (runs the game once through Steam, ~3 minutes).

usage: lc2_regress.py [--keep]     prints one line per failed check; silent and exit 0 when everything passes

A new game (X6, the intro stage, normal X = Axl since the new-game armour is 0) is driven by the DLL's test harness
(mods/axl/lc2/axl_test.c) and the player work is dumped every frame (axl_pdump.bin). Checks, on the dump:
  rapid fire: holding the shot gives X a new shot press every <= 9 frames, aim pose anim 195 on the ground
  aim up: anim 193; hover: a new jump press in the air -> anim 202, vy 0, then the glide (gravity / 4)
  air dash: dash in the air -> anim 250 (lc2_hook.AIR_DASH_ANIM) with gravity 0; air shooting down: anim 201 (AIM_AIR + 4)
  hurt box: every frame >= 228 shown has a hurt box (P+0x7C != 0)
Frames are the game's own (its keyboard reads, axl_test.c), so a slow load no longer shifts the scenario; skipto
starts the play part at the same frame however long the opening took. Wall shooting is
checked separately (work/lc2/test/t24.txt: the cling holds while shooting).
"""
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lc2_fmt as F  # noqa: E402

OUT = os.path.join(F.BUILT, 'test')
R = 20 + 0x100 + 0x400
AIR_DASH = 250   # lc2_hook.AIR_DASH_ANIM: Axl's own air dash anim (anim 16 until 2026-10-27)


def script():
    # fast start (work/lc2/test/base_fast.txt): collection menu -> X6, Space skips the opening, talk runs the
    # stage dialogue, and once the player is free the frame count jumps to 7990 (skipto) however long it all took
    L = ['key 900 905 RETURN', 'key 1150 1155 DOWN', 'key 1200 1204 Z', 'key 1240 1244 Z']
    L += ['key %d %d SPACE' % (f, f + 4) for f in range(1600, 2401, 100)]
    L += ['talk 2300 6000', 'skipto 7990']
    L += ['key 8000 8100 X', 'key 8150 8250 X UP']                        # rapid fire, aim up
    L += ['key 8300 8303 Z', 'key 8315 8390 Z']                           # jump, hover
    L += ['key 8500 8503 Z', 'key 8515 8518 A']                           # jump, air dash
    L += ['key 8600 8603 Z', 'key 8606 8640 X DOWN']                      # air shot down
    L += ['pdump 7990 8800', 'quit 8800']
    return '\n'.join(L) + '\n'


def records(path):
    d = open(path, 'rb').read()
    for i in range(0, len(d) - R + 1, R):
        f, phase = struct.unpack_from('<I', d, i + 4)[0], struct.unpack_from('<I', d, i + 16)[0]
        if phase == 1:
            yield f, d[i + 20 + 0x100:i + R]


def check(path):
    fails = []
    recs = list(records(path))
    if not recs:
        return ['no player frames dumped']

    def frames(lo, hi):
        return [(f, P) for f, P in recs if lo <= f < hi]

    # rapid fire: the shot press reaches X (new buttons P+0xC4 bit 0x20) every 6 frames while held
    rises = [f for f, P in frames(8000, 8100) if struct.unpack_from('<H', P, 0xC4)[0] & 0x20]
    gaps = [b - a for a, b in zip(rises, rises[1:])]
    if len(rises) < 10 or max(gaps or [99]) > 9:
        fails.append('rapid fire: %d new bullets, gaps %s' % (len(rises), gaps[:12]))
    if not any(P[0x2B] == 195 for _, P in frames(8005, 8100)):
        fails.append('aim forward pose (anim 195) not shown')
    if not any(P[0x2B] == 193 for _, P in frames(8155, 8250)):
        fails.append('aim up pose (anim 193) not shown')
    hov = [(f, P) for f, P in frames(8315, 8400) if P[0x2B] == 202]
    if len(hov) < 30 or any(struct.unpack_from('<i', P, 0x40)[0] for _, P in hov):
        fails.append('hover: %d frames of anim 202' % len(hov))
    if not any(struct.unpack_from('<i', P, 0x48)[0] == 0x4200 // 4 for _, P in frames(8360, 8420)):
        fails.append('glide (gravity / 4) not seen')
    if not any(P[0x2B] == AIR_DASH and struct.unpack_from('<i', P, 0x48)[0] == 0 for _, P in frames(8515, 8560)):
        fails.append('air dash (anim %d, gravity 0) not seen' % AIR_DASH)
    if not any(P[0x2B] == 201 for _, P in frames(8606, 8650)):
        fails.append('air aim down (anim 201) not seen')
    for f, P in recs:
        if P[0x66] >= 228 and P[0x0C] == 1 and not struct.unpack_from('<I', P, 0x7C)[0]:
            fails.append('frame %d at %d has no hurt box' % (P[0x66], f))
            break
    return fails


def main():
    os.makedirs(OUT, exist_ok=True)
    s = os.path.join(OUT, 'regress.txt')
    open(s, 'w').write(script())
    subprocess.run([sys.executable, '-I', os.path.join(HERE, 'lc2_test.py'), s, '--timeout', '240', '--tag',
                    'regress'], check=True, stdout=subprocess.DEVNULL)
    fails = check(os.path.join(OUT, 'regress_pdump.bin'))
    for x in fails:
        print('FAIL', x)
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
