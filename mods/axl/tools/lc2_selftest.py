"""Checks for the Legacy Collection 2 conversion (lc2_fmt / lc2_build). Silent when all pass (-v: summary); exit 1 on failure.

1. codec round trip on original data: PS1 US/JP DAT 86 -> PAT/COL/TEX/COF equal the shipped pl02_eng / pl02 entries
2. ARC rebuild: parse + rebuild reproduces every entry
3. (when built) mods/axl/work/lc2/*.arc: Axl frames render the same as from the Axl PS1 DAT 86
4. mods/axl/work/lc2/axl_data.bin (the DLL's tables): magic, version, size and hash as lc2/axl_data.h, not older than
   the generated headers it is packed from
"""
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lc2_fmt as F  # noqa: E402

fails = []
checks = [0]


def check(ok, msg):
    checks[0] += 1
    if not ok:
        fails.append(msg)


def roundtrip(fn):
    region, pat_name = F.PL02[fn]
    if not os.path.exists(F.DAT[region]):   # (the Japanese PS1 data is optional: axl_config x6_jp_dir)
        fails.append('informational: no %s PS1 data (%s), %s codec round trip skipped' % (region, F.DAT[region], fn))
        return
    arc = F.Arc(F.orig_path(fn))
    d = F.dat_entry86(F.DAT[region])
    secs = [F.t3_section(d[3], k) for k in range(32)]
    check(F.pat(secs) == arc.get(pat_name, F.H_PAT), '%s: PAT differs from shipped' % region)
    check(F.col(d[9]) == arc.get(F.COL_NAME, F.H_COL), '%s: COL differs from shipped' % region)
    for k in range(19):
        if k in F.TEX_STUB:
            continue
        name = F.TEX_NAME % k
        tex, cof = F.tex_cof(d[2], k, arc.get(name, F.H_COF))
        check(cof == arc.get(name, F.H_COF), '%s: COF %d differs' % (region, k))
        check(tex == arc.get(name, F.H_TEX), '%s: TEX %d differs' % (region, k))


def arc_rebuild(fn):
    region = fn
    path = F.orig_path(fn)
    a = F.Arc(path)
    b = a.build()
    tmp = os.path.join('/tmp', 'lc2_selftest_' + fn)
    open(tmp, 'wb').write(b)
    r = F.Arc(tmp)
    check([(e['name'], e['hash'], e['flags']) for e in a.entries] == [(e['name'], e['hash'], e['flags']) for e in r.entries],
          '%s: rebuilt entry list differs' % region)
    for e in a.entries:
        check(r.get(e['name'], e['hash']) == a.get(e['name'], e['hash']), '%s: rebuilt %s differs' % (region, e['name']))
    check(b == open(path, 'rb').read(), '%s: rebuilt file not byte-identical' % region)
    os.remove(tmp)


def built(fn):
    """Axl arcs: every frame with parts in type-3 sections 0 and 5 renders like the PS1 Axl DAT 86"""
    import lc2_build as B
    region, pat_name = F.PL02[fn]
    path = os.path.join(F.BUILT, fn)
    if not os.path.exists(path):
        check(False, '%s: built arc missing (run lc2_build.py)' % region)
        return
    arc = F.Arc(path)
    orig = F.Arc(F.orig_path(fn))
    added = [(F.TEX_NAME % (18 + k), h) for k in range(1, 11) for h in (F.H_TEX, F.H_COF)]   # DNA looks, boss shots, skill HUD
    check([(e['name'], e['hash']) for e in arc.entries if (e['name'], e['hash']) not in added] ==
          [(e['name'], e['hash']) for e in orig.entries], '%s: built entry list differs from shipped' % region)
    check(all(any((e['name'], e['hash']) == a for e in arc.entries) for a in added), '%s: DNA textures missing' % region)
    d = F.dat_entry86(F.DAT['axl'])
    t2_axl, t3_axl, _, _ = B.plan(d, F.dat_entry86(F.DAT['us']))
    changed = {(F.TEX_NAME % k, h) for k in t2_axl for h in (F.H_TEX, F.H_COF)}
    changed |= {(pat_name, F.H_PAT), (F.COL_NAME, F.H_COL), (B.COM00, F.H_TEX), ('X6\\chr\\pl\\pl02\\pl02', F.H_CTEX)}
    changed.add(('X6\\chr\\com\\com00', F.H_CTEX))   # + the bosses' projectile texture (lc2_dna.fx_looks)
    import lc2_build
    changed |= {(n, lc2_build.H_WAV) for n in lc2_build.sound_entries(orig)}   # X8 / boss / change / charge sounds
    for n, i, layout, pc in B.KAO_FACES:      # dialogue portraits: the face build as lc2_build makes it
        import face
        pal, tex = face.build(i, layout, pc=pc)
        changed |= {(F.KAO_TEX % n, F.H_TEX), (F.KAO_COL % n, F.H_COL)}
        check(arc.get(F.KAO_TEX % n, F.H_TEX)[20:20 + len(tex)] == tex and
              arc.get(F.KAO_COL % n, F.H_COL)[12:12 + len(pal)] == pal, '%s: portrait k_pl%02d' % (region, n))
    # com00: only the rotated-shot / flame tiles 864-877, the life gauge emblem tiles (B.HUD_TILES) and the 1UP tiles
    # (B.ONEUP_TILE0.., at most 4 per picture) change
    w = 256
    ca = np.frombuffer(arc.get(B.COM00, F.H_TEX)[20:], np.uint8).reshape(-1, w)
    co = np.frombuffer(orig.get(B.COM00, F.H_TEX)[20:], np.uint8).reshape(-1, w)
    diff = {16 * (y // 16) + x // 16 for y, x in zip(*np.nonzero(ca != co))}
    hud = {16 * (y // 16) + x // 16 for x, y, _, _ in B.HUD_TILES}
    oneup = {t for t in diff if B.ONEUP_TILE0 <= t < B.ONEUP_TILE0 + 4 * len(B.ONEUP_CELLS)}
    shots = diff - hud - oneup
    check(shots and min(shots) >= 864 and max(shots) <= 877 and hud <= diff and oneup,
          '%s: com00 tiles changed %s' % (region, sorted(diff)[:8]))
    for e in orig.entries:
        if (e['name'], e['hash']) not in changed:
            check(arc.get(e['name'], e['hash']) == orig.get(e['name'], e['hash']),
                  '%s: untouched entry %s differs' % (region, e['name']))
    t9 = list(struct.unpack('<%dH' % (len(d[9]) // 2), d[9]))
    col = arc.get(F.COL_NAME, F.H_COL)
    pal = list(struct.unpack('<%dH' % ((len(col) - 12) // 2), col[12:]))
    secs = F.pat_sections(arc.get(pat_name, F.H_PAT))
    import spritesheet as ss
    n = 0
    # (type 3 section, palette base, type 2 section): body / Ultimate layer on bank A, bank B parts
    for sec, pbase, k in ((0, 0, 0), (5, 1, 0), (18, 0, 10)):
        tex = F.tex_image(arc.get(F.TEX_NAME % k, F.H_TEX))
        ys = F.cof_ys(arc.get(F.TEX_NAME % k, F.H_COF))
        s2 = ss.sections(d[2])[k]
        ps1 = F.t3_section(d[3], sec)
        check(sec in t3_axl, 'section %d expected to come from Axl' % sec)
        extra = len(lc2_build.LADDER_AIM_CELLS) if sec == 18 else 0   # (bank B grows by the ladder aims, 74-77)
        check(len(secs[sec]) == len(ps1) + extra, '%s: section %d frame count' % (region, sec))
        for f, parts in enumerate(ps1):
            if not parts or f >= len(ys):
                continue
            img, _ = ss.tiles_image(d[2], s2, f)
            if img is None:
                continue
            a = F.render(parts, img, t9, pbase=pbase)
            b = F.render(secs[sec][f], tex[ys[f]:ys[f] + img.shape[0]], pal, pbase=pbase)
            if not a[..., 3].any():
                continue
            n += 1
            if not np.array_equal(a, b):
                check(False, '%s: section %d frame %d renders differently' % (region, sec, f))
    check(n > 200, '%s: only %d frames compared' % (region, n))
    return n


def data():
    import lc2_data
    err = lc2_data.check()
    check(not err, 'axl_data.bin: ' + err)
    if not err:
        gen = [os.path.join(lc2_data.GEN, h) for h in lc2_data.HEADERS]
        newest = max((os.path.getmtime(g) for g in gen if os.path.exists(g)), default=0)
        check(os.path.getmtime(lc2_data.DATA) >= newest, 'axl_data.bin older than %s (run lc2_hook.py)' % lc2_data.GEN)


def main():
    verbose = '-v' in sys.argv
    stages = [a for a in sys.argv[1:] if a != '-v'] or ['codec', 'arc', 'built', 'data']
    counts = {}
    for region in F.PL02:
        if 'codec' in stages:
            roundtrip(region)
        if 'arc' in stages:
            arc_rebuild(region)
        if 'built' in stages:
            counts[region] = built(region)
    if 'data' in stages:
        data()
    info = [f for f in fails if 'informational' in f]
    real = [f for f in fails if 'informational' not in f]
    for f in info if verbose else []:
        print('note:', f)
    for f in real:
        print('FAIL:', f)
    if real or verbose:   # silent when everything passes (feedback gate convention)
        print('lc2_selftest %s: %d checks, %s, frames compared %s' % (','.join(stages), checks[0],
              '%d NOT OK' % len(real) if real else 'all ok', counts))
    sys.exit(1 if real else 0)


if __name__ == '__main__':
    main()
