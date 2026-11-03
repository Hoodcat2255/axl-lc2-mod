"""Install the converted Axl archives into Mega Man X Legacy Collection 2, or restore the shipped ones.

usage: lc2_install.py install | restore | status | options [all_weapons ...]

install: on first use copies the shipped pl02.arc / pl02_eng.arc to mods/axl/work/lc2/backup/ (sha1 in SHA1SUMS),
         then copies mods/axl/work/lc2/*.arc over the game's files. Refuses when the game's file is not the shipped
         one and there is no backup yet (a backup must never hold a modified file).
         Also copies mods/axl/work/lc2/dinput8.dll (lc2_hook.py) next to RXC2.exe; the game ships no dinput8.dll,
         and sets "dinput8"="native,builtin" in the game's Proton prefix (compatdata/743900/pfx/user.reg,
         [Software\\Wine\\DllOverrides]) so a plain Steam launch loads it (no launch option needed).
options: writes axl_options.txt next to RXC2.exe with the given words (none = removes it); all_weapons = every
         boss weapon owned from the intro stage on (testing the DNA Change).
restore: copies the backups back, removes that dinput8.dll and the prefix override.
Steam's "Verify integrity of game files" also restores the shipped files.
"""
import hashlib
import os
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lc2_fmt as F  # noqa: E402

FILES = tuple(F.PL02) + (F.KAO, F.STSEL, 'cledit_eng_reg_wor.arc')   # (+ the staff roll: lc2_credits)
DLL = 'dinput8.dll'
OPTIONS = 'axl_options.txt'
GAME_DIR = os.path.normpath(os.path.join(F.ROMPC, '..', '..', '..'))   # .../Mega Man X Legacy Collection 2
USER_REG = os.path.normpath(os.path.join(GAME_DIR, '..', '..', 'compatdata', '743900', 'pfx', 'user.reg'))
OVERRIDES = '[Software\\\\Wine\\\\DllOverrides]'
OVERRIDE = '"dinput8"="native,builtin"'


def prefix_override(on):
    """add / remove the dinput8 override in the prefix (Wine rewrites user.reg while the game runs: not then)"""
    if subprocess.run(['pgrep', '-x', 'RXC2.exe'], capture_output=True).returncode == 0:
        sys.exit('the game is running: close it first (the prefix registry is in use)')
    if not os.path.exists(USER_REG):
        print('no Proton prefix yet (%s): start the game once from Steam, then install again' % USER_REG)
        return
    lines = open(USER_REG, encoding='utf-8').read().split('\n')
    has = OVERRIDE in lines
    if on == has:
        return
    if on:
        k = next((i for i, l in enumerate(lines) if l.startswith(OVERRIDES)), None)
        if k is None:
            lines += ['', OVERRIDES, OVERRIDE]
        else:
            k += 1
            while k < len(lines) and lines[k].startswith('#'):
                k += 1
            lines.insert(k, OVERRIDE)
    else:
        lines.remove(OVERRIDE)
    tmp = USER_REG + '.axltmp'
    open(tmp, 'w', encoding='utf-8').write('\n'.join(lines))
    os.replace(tmp, USER_REG)
    print('%s prefix override dinput8=native,builtin' % ('set' if on else 'removed'))


def sha1(path):
    return hashlib.sha1(open(path, 'rb').read()).hexdigest()


def kao_shipped(path):
    """every portrait k_pl00-06 is the PS1 US DAT pair 163 + 2n / 164 + 2n"""
    dat = open(F.DAT['us'], 'rb').read()

    def entry(k):
        s, z = struct.unpack_from('<II', dat, 8 * k)
        return dat[s * 2048:s * 2048 + z]
    arc = F.Arc(path)
    return all(arc.get(F.KAO_TEX % n, F.H_TEX)[20:20 + 8192] == entry(163 + 2 * n)[:8192] and
               arc.get(F.KAO_COL % n, F.H_COL)[12:12 + 512] == entry(164 + 2 * n)[:512] for n in range(7))


def is_shipped(path, fn):
    """the game's file is unmodified: PAT, COL and every TEX/COF are exactly what the original PS1 data converts to"""
    if fn == F.KAO:
        return kao_shipped(path)
    if fn == 'cledit_eng_reg_wor.arc':   # the staff roll: its last two screens still empty
        import lc2_credits
        return lc2_credits.is_shipped(path)
    if fn == F.STSEL:   # the name frame still X's single tile
        arc = F.Arc(path)
        return [struct.unpack('<BBbb', r) for r in F.pat_sections(arc.get('X6\\pat\\stage\\stsel_eng\\stsel_eng', F.H_PAT))[0][5]] == [(0, 6, -8, -8)]
    region, pat_name = F.PL02[fn]
    d = F.dat_entry86(F.DAT[region])
    arc = F.Arc(path)
    if arc.get(pat_name, F.H_PAT) != F.pat([F.t3_section(d[3], k) for k in range(32)]):
        return False
    if arc.get(F.COL_NAME, F.H_COL) != F.col(d[9]):
        return False
    for k in range(19):
        if k in F.TEX_STUB:
            continue
        name = F.TEX_NAME % k
        if (arc.get(name, F.H_TEX), arc.get(name, F.H_COF)) != F.tex_cof(d[2], k, arc.get(name, F.H_COF)):
            return False
    return True


def recorded_sums():
    sums = os.path.join(F.BACKUP, 'SHA1SUMS')
    if not os.path.exists(sums):
        return {}
    return {fn: h for h, fn in (line.split() for line in open(sums) if line.strip())}


def copy_atomic(src, dst):
    tmp = dst + '.tmp'
    shutil.copyfile(src, tmp)
    os.replace(tmp, dst)


def checked_backup(fn):
    b = os.path.join(F.BACKUP, fn)
    want = recorded_sums().get(fn)
    if not os.path.exists(b) or want is None or sha1(b) != want:
        sys.exit('backup of %s missing or does not match SHA1SUMS' % fn)
    return b


def backup():
    os.makedirs(F.BACKUP, exist_ok=True)
    sums = os.path.join(F.BACKUP, 'SHA1SUMS')
    for fn in FILES:
        dst = os.path.join(F.BACKUP, fn)
        if os.path.exists(dst):
            continue
        src = os.path.join(F.ROMPC, fn)
        if not is_shipped(src, fn):
            sys.exit('refusing: %s is not the shipped file and no backup exists (verify the game files in Steam first)'
                     % src)
        shutil.copy2(src, dst)
        if fn not in recorded_sums():
            with open(sums, 'a') as f:
                f.write('%s  %s\n' % (sha1(dst), fn))
        print('backed up %s' % fn)


def install():
    backup()
    for fn in FILES:
        checked_backup(fn)
        src = os.path.join(F.BUILT, fn)
        if not os.path.exists(src):
            sys.exit('missing %s: run lc2_build.py first' % src)
        copy_atomic(src, os.path.join(F.ROMPC, fn))
        print('installed %s (sha1 %s)' % (fn, sha1(src)[:12]))
    src = os.path.join(F.BUILT, DLL)
    if not os.path.exists(src):
        sys.exit('missing %s: run lc2_hook.py first' % src)
    copy_atomic(src, os.path.join(GAME_DIR, DLL))
    print('installed %s (sha1 %s)' % (DLL, sha1(src)[:12]))
    prefix_override(True)


def restore():
    for fn in FILES:
        src = checked_backup(fn)
        copy_atomic(src, os.path.join(F.ROMPC, fn))
        print('restored %s (sha1 %s)' % (fn, sha1(src)[:12]))
    dll = os.path.join(GAME_DIR, DLL)
    if os.path.exists(dll):
        built = os.path.join(F.BUILT, DLL)
        if not os.path.exists(built) or sha1(dll) != sha1(built):
            sys.exit('not removing %s: it is not the Axl hook build' % dll)
        os.remove(dll)
        print('removed %s' % DLL)
    prefix_override(False)
    options()


def options(*words):
    path = os.path.join(GAME_DIR, OPTIONS)
    if words:
        with open(path, 'w') as f:
            f.write('\n'.join(words) + '\n')
        print('%s: %s' % (OPTIONS, ' '.join(words)))
    elif os.path.exists(path):
        os.remove(path)
        print('removed %s' % OPTIONS)


def status():
    for fn in FILES:
        game = os.path.join(F.ROMPC, fn)
        g = sha1(game)
        b = os.path.join(F.BACKUP, fn)
        built = os.path.join(F.BUILT, fn)
        state = ('shipped' if os.path.exists(b) and g == sha1(b) else
                 'Axl build' if os.path.exists(built) and g == sha1(built) else
                 'shipped (no backup yet)' if is_shipped(game, fn) else 'other')
        print('%s: %s (sha1 %s)' % (fn, state, g[:12]))
    dll, built = os.path.join(GAME_DIR, DLL), os.path.join(F.BUILT, DLL)
    print('%s: %s' % (DLL, 'not installed' if not os.path.exists(dll) else
                      'Axl hook' if os.path.exists(built) and sha1(dll) == sha1(built) else 'other'))
    if os.path.exists(USER_REG):
        print('prefix override: %s' % ('set' if OVERRIDE in open(USER_REG, encoding='utf-8').read().split('\n')
                                       else 'not set'))
    opt = os.path.join(GAME_DIR, OPTIONS)
    print('%s: %s' % (OPTIONS, open(opt).read().split() if os.path.exists(opt) else 'none'))


if __name__ == '__main__':
    {'install': install, 'restore': restore, 'status': status, 'options': options}[
        sys.argv[1] if len(sys.argv) > 1 else 'status'](*sys.argv[2:])
