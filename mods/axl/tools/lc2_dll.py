"""Build the Legacy Collection 2 hook DLL (dinput8.dll) from the sources alone: no game data needed.

usage: lc2_dll.py [--release] [--no-skills] [--out PATH]
       default out: mods/axl/work/lc2/dinput8.dll (--release: mods/axl/work/lc2/release/dinput8.dll)

The game-derived tables are not compiled in: the DLL reads them at start-up from axl_data.bin next to it
(lc2/axl_data.h; lc2_data.py makes the file from the user's game data). Python standard library and zig (the ziglang
package) only, so a CI machine without the game builds the same DLL (.github/workflows/build-dll.yml).
--release: AXL_RELEASE (no test harness, no options file, no log: the test-only functions dropped by --gc-sections); --no-skills: AXL_NO_SKILLS (no cooldowns, no skill HUD, no
boss giga attacks - the test5 release, user decision 2026-11-04). The version resource (lc2/axl_version.rc) takes
its numbers from mods/axl/VERSION.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
AXL = os.path.normpath(os.path.join(HERE, '..'))
LC2 = os.path.join(AXL, 'lc2')
BUILT = os.path.join(AXL, 'work', 'lc2')
ZIG = [sys.executable, '-m', 'ziglang']


def version():
    """mods/axl/VERSION: MAJOR.MINOR.PATCH"""
    v = open(os.path.join(AXL, 'VERSION')).read().strip()
    if not re.fullmatch(r'\d+\.\d+\.\d+', v):
        sys.exit('mods/axl/VERSION: %r is not MAJOR.MINOR.PATCH' % v)
    return v


def build_dll(release=False, no_skills=False, out=None):
    out = out or (os.path.join(BUILT, 'release', 'dinput8.dll') if release else os.path.join(BUILT, 'dinput8.dll'))
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    v = version()
    res = os.path.splitext(out)[0] + '_version.obj'
    subprocess.run(ZIG + ['rc', '/:auto-includes', 'gnu', '/:target', 'x86', '/d', 'AXL_VER_NUM=%s,0' % v.replace('.', ','),
                          '/d', 'AXL_VER_STR="%s"' % v, '/fo', res, '--', os.path.join(LC2, 'axl_version.rc')], check=True)
    srcs = sorted(os.path.join(LC2, f) for f in os.listdir(LC2) if f.endswith('.c'))
    cmd = ZIG + ['cc', '-target', 'x86-windows-gnu', '-shared', '-O2', '-s',
                 '-Wall', '-Wno-unused-function', '-Wno-unused-const-variable', '-Wno-unused-variable'] + \
        (['-DAXL_RELEASE', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections'] if release else []) + (['-DAXL_NO_SKILLS'] if no_skills else []) + ['-o', out] + srcs + \
        [res, os.path.join(LC2, 'dinput8.def'), '-lkernel32', '-luser32']
    try:
        subprocess.run(cmd, check=True)
    finally:
        os.remove(res)
    # (zig names the import library after the first source)
    first = os.path.join(os.path.dirname(os.path.abspath(out)), os.path.splitext(os.path.basename(srcs[0]))[0])
    for p in (os.path.splitext(out)[0] + '.pdb', os.path.splitext(out)[0] + '.lib', first + '.lib', first + '.pdb'):
        if os.path.exists(p):
            os.remove(p)
    print('wrote %s (%d bytes, version %s)' % (out, os.path.getsize(out), v))
    return out


if __name__ == '__main__':
    a = sys.argv[1:]
    build_dll('--release' in a, '--no-skills' in a, a[a.index('--out') + 1] if '--out' in a else None)
