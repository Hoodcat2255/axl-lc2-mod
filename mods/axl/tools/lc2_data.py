"""axl_data.bin: the game-derived tables the DLL reads at start-up (lc2/axl_data.h), from the generated headers.

usage: lc2_data.py pack            mods/axl/work/lc2/gen/*.h -> mods/axl/work/lc2/axl_data.bin
       lc2_data.py check [FILE]    magic, version, size and hash against lc2/axl_data.h (default the built file)

The headers are written by the build from the user's game data (lc2_dna.py header / hud_header via lc2_build.py,
lc2_hook.py anims_header, build_axl.py's axl_gen.h copied by lc2_hook.py) into mods/axl/work/lc2/gen/. pack builds
lc2/data/axl_data_pack.c with them for this machine (zig cc) and runs it: the packer fills axl_data_t from the
headers' tables and writes it with its hash. axl_data_t has only char / short / int fields, the same layout here and
in the 32-bit Windows DLL (AXL_DATA_SIZE, asserted in both builds).
"""
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
AXL = os.path.normpath(os.path.join(HERE, '..'))
LC2 = os.path.join(AXL, 'lc2')
BUILT = os.path.join(AXL, 'work', 'lc2')
GEN = os.path.join(BUILT, 'gen')
DATA = os.path.join(BUILT, 'axl_data.bin')
HEADERS = ('axl_anims.h', 'axl_pal.h', 'axl_gen.h', 'axl_dna_looks.h', 'axl_hud_looks.h')


def layout():
    """(magic, version, size) of lc2/axl_data.h"""
    h = open(os.path.join(LC2, 'axl_data.h')).read()

    def num(name):
        return int(re.search(r'^#define %s (0x[0-9A-Fa-f]+|\d+)u?\b' % name, h, re.M).group(1), 0)
    return num('AXL_DATA_MAGIC'), num('AXL_DATA_VERSION'), num('AXL_DATA_SIZE')


def fnv1a(b):
    h = 2166136261
    for c in b:
        h = ((h ^ c) * 16777619) & 0xFFFFFFFF
    return h


def check(path=DATA):
    """'' when path is a data file this DLL source reads, else what is wrong"""
    if not os.path.exists(path):
        return 'missing %s (lc2_hook.py makes it)' % path
    b = open(path, 'rb').read()
    magic, ver, size = layout()
    if len(b) != size:
        return '%s: %d bytes, axl_data.h says %d' % (path, len(b), size)
    m, v, z, h = struct.unpack_from('<4I', b)
    if (m, v, z) != (magic, ver, size):
        return '%s: header %08x v%d %d, axl_data.h %08x v%d %d' % (path, m, v, z, magic, ver, size)
    if h != fnv1a(b[16:]):
        return '%s: hash mismatch' % path
    return ''


def pack(out=DATA):
    missing = [h for h in HEADERS if not os.path.exists(os.path.join(GEN, h))]
    if missing:
        sys.exit('missing generated headers in %s: %s (run lc2_build.py, then lc2_hook.py)' % (GEN, ', '.join(missing)))
    exe = os.path.join(GEN, 'axl_data_pack')
    subprocess.run([sys.executable, '-m', 'ziglang', 'cc', '-O1', '-Wall', '-Wno-unused-const-variable',
                    '-Werror=macro-redefined', '-Werror=excess-initializers', '-I', GEN, '-o', exe,
                    os.path.join(LC2, 'data', 'axl_data_pack.c')], check=True)
    tmp = out + '.tmp'
    subprocess.run([exe, tmp], check=True)
    for junk in (exe, exe + '.o'):
        if os.path.exists(junk):
            os.remove(junk)
    err = check(tmp)
    if err:
        os.remove(tmp)
        sys.exit('axl_data_pack: ' + err)
    os.replace(tmp, out)
    print('wrote %s (%d bytes)' % (out, os.path.getsize(out)))
    return out


if __name__ == '__main__':
    if sys.argv[1:2] == ['pack']:
        pack()
    elif sys.argv[1:2] == ['check']:
        e = check(*sys.argv[2:3])
        sys.exit(e or None)
    else:
        sys.exit(__doc__)
