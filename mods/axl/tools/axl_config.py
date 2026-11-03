"""Where this machine keeps the game data the build reads (nothing of it is in the repository).

Each setting: the environment variable, else the key in config.local.json at the repository root (not committed),
else the default below. A relative path is taken from the repository root.

  AXL_LC2_DIR     lc2_dir     Mega Man X Legacy Collection 2's game folder (the one with RXC2.exe)
  AXL_X6_US_DIR   x6_us_dir   PS1 Mega Man X6 (USA) v1.1: a folder with fs/ (ROCK_X6.DAT, ROCK_X6.BIN and
                              SLUS_013.95 extracted from the image: mods/axl/tools/psxiso.py) and the image itself
                              (PS1 ISO build only)
  AXL_X6_JP_DIR   x6_jp_dir   optional, PS1 Mega Man X6 (Japan) the same way: lc2_build.py checks that Axl's data
                              leaves the Japanese-only text sections alone
  AXL_READY_SOUND ready_sound optional, the sound file for Axl's "Ready" (else X8's announcer READY)
  AXL_LC2_SAVE    lc2_save    the game's save file (lc2_test.py keeps a copy around each run)
  AXL_PYTHON      python      the Python lc2_dist.py runs the build steps with
"""
import glob
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
LOCAL = os.path.join(ROOT, 'config.local.json')
# Steam's save of the game (Linux, one Steam account), else none
_SAVES = glob.glob(os.path.expanduser('~/.local/share/Steam/userdata/*/743900/remote/RXC2/savedata.dat'))

# the game folder on this machine (Steam's default Linux library)
LC2_DIR_DEFAULT = '~/.local/share/Steam/steamapps/common/Mega Man X Legacy Collection 2'
DEFAULTS = {
    'lc2_dir': LC2_DIR_DEFAULT,
    'x6_us_dir': os.path.join(ROOT, 'games', 'x6_us'),
    'x6_jp_dir': os.path.join(ROOT, 'games', 'x6_jp'),
    'ready_sound': os.path.join(ROOT, 'mods', 'axl', 'sound', 'axl_ready.ogg'),
    'lc2_save': _SAVES[0] if len(_SAVES) == 1 else '',
    'python': os.path.join(ROOT, '.venv', 'bin', 'python'),
}
ENV = {'lc2_dir': 'AXL_LC2_DIR', 'x6_us_dir': 'AXL_X6_US_DIR', 'x6_jp_dir': 'AXL_X6_JP_DIR',
       'ready_sound': 'AXL_READY_SOUND', 'lc2_save': 'AXL_LC2_SAVE', 'python': 'AXL_PYTHON'}

_local = json.load(open(LOCAL, encoding='utf-8')) if os.path.exists(LOCAL) else {}


def get(key):
    v = os.environ.get(ENV[key]) or _local.get(key) or DEFAULTS[key]
    return os.path.normpath(os.path.join(ROOT, os.path.expanduser(v))) if v else ''   # (relative: to ROOT)


LC2_DIR = get('lc2_dir')
ROMPC = os.path.join(LC2_DIR, 'nativeDX10', 'X6', 'romPC') if LC2_DIR else ''
X8_SOUND = os.path.join(LC2_DIR, 'nativeDX10', 'X8', 'sound') if LC2_DIR else ''
X6_US = get('x6_us_dir')
X6_US_FS = os.path.join(X6_US, 'fs') if X6_US else ''
X6_JP_FS = os.path.join(get('x6_jp_dir'), 'fs') if get('x6_jp_dir') else ''
READY_SOUND = get('ready_sound')
LC2_SAVE = get('lc2_save')
PYTHON = get('python')


def require(*what):
    """exit with what is missing and where to set it; what: 'lc2' (the game folder), 'x6_us' (the PS1 files)"""
    missing = []
    if 'lc2' in what and not os.path.isdir(ROMPC):
        missing.append(('lc2_dir', 'Legacy Collection 2 game folder (nativeDX10/X6/romPC not found under %r)'
                        % LC2_DIR))
    if 'x6_us' in what and not all(os.path.isfile(os.path.join(X6_US_FS, f))
                                   for f in ('ROCK_X6.DAT', 'ROCK_X6.BIN', 'SLUS_013.95')):
        missing.append(('x6_us_dir', 'PS1 Mega Man X6 (USA) v1.1 files (ROCK_X6.DAT, ROCK_X6.BIN, SLUS_013.95 '
                        'not found in %r; extract them: python mods/axl/tools/psxiso.py extract IMAGE.bin DIR/fs)'
                        % X6_US_FS))
    if missing:
        sys.exit('missing game data:\n' + '\n'.join('  %s: set %s or "%s" in %s' % (msg, ENV[k], k, LOCAL)
                                                    for k, msg in missing))
