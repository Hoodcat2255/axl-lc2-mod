"""Axl's victory motion on Legacy Collection 2 (X's anim 39, X's frames 183-186: a boss beaten, an armour part got;
user 2026-10-27 - 95 / 97, used here at first, are not it), editable in
the viewer's victory tab (tools/make_viewer.py): work/sheet/victory.json = {"cells": [[y0, x0, y1, x1], ...],
"seq": [[cell, duration], ...], "pos": {"cell": [dx, dy]}} (screen pixels facing right: right / down +; older files
have "lift": {"cell": dy}). Cells are sheet rectangles (default: the sheet's 9 "Victory"
pictures, lc2_build.VICTORY_BOXES: 0 / 1 guns out, 2 arms open, 3 crouch, 4 up, 5 jump, 6-8 the spin; the viewer can
pick any other part of the sheet), at most N_CELLS, built into bank B frames lc2_build.VICTORY_B + cell (47-65, X's
Z-Saber slashes there); the last entry is held. dy = the picture's height off its feet position (negative = up). X's event stays at X's time: anim 39 flags
0x80 at t = 23 (the entry running then is split there)."""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
PATH = os.path.join(HERE, '..', 'work', 'sheet', 'victory.json')
# up, crouch, jump, one turn, land, arms open, guns out (held): user 2026-10-27
DEFAULT = {'seq': [[4, 7], [3, 5], [5, 5], [6, 4], [7, 4], [8, 6], [3, 5], [2, 8], [1, 8], [0, 96]],
           'lift': {'5': -14, '6': -30, '7': -38, '8': -26}}
EVENTS = {39: (23, 128)}
N_CELLS = 19   # bank B 47-65


def load():
    """(seq [(cell, duration)], pos {cell: (dx, dy)} on screen facing right, cells [[y0, x0, y1, x1]])"""
    import lc2_build
    v = json.load(open(PATH)) if os.path.exists(PATH) else DEFAULT
    cells = [[int(c) for c in b] for b in v.get('cells', lc2_build.VICTORY_BOXES)]
    assert 0 < len(cells) <= N_CELLS and all(len(b) == 4 and b[0] < b[2] and b[1] < b[3] for b in cells), cells
    seq = [(int(c), max(1, min(255, int(d)))) for c, d in v.get('seq', DEFAULT['seq'])]
    assert seq and all(0 <= c < len(cells) for c, _ in seq), seq
    pos = {int(k): (0, int(d)) for k, d in v.get('lift', {}).items()}
    pos.update({int(k): (int(d[0]), int(d[1])) for k, d in v.get('pos', {}).items()})
    pos = {k: d for k, d in pos.items() if 0 <= k < len(cells) and d != (0, 0)}
    return seq, pos, cells


def script(first_frame, anim):
    """[(dur, flag, step, frame)] of anim 39 (linear; the last entry held, as lc2_hook's PC_SCRIPTS)"""
    seq = load()[0]
    at, flag = EVENTS[anim]
    out, t = [], 0
    for i, (c, d) in enumerate(seq):
        last = i == len(seq) - 1
        if t <= at < t + d and not last:        # split this entry at the event time
            if at > t:
                out.append((at - t, 0, 1, first_frame + c))
            out.append((t + d - at, flag, 1, first_frame + c))
        elif last and t <= at:                   # (the event falls into the held pose: one frame of it first)
            if at > t:
                out.append((at - t, 0, 1, first_frame + c))
            out.append((96, flag, 0, first_frame + c))
            return out
        else:
            out.append((d if not last else 96, 0, 1 if not last else 0, first_frame + c))
        t += d
    return out
