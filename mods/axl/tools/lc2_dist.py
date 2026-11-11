"""Package the Legacy Collection 2 Axl mod for testers (user request 2026-10-27: a test release, the testing aids out).

usage: lc2_dist.py [version] [--xdelta] [--no-skills]   ->  mods/axl/work/dist/axl_lc2_<version>[_xdelta]/ and its .zip

--xdelta (user request 2026-11-04: the archives hold Capcom's pictures - Nexus / GameBanana forbid game files, Google
Drive flagged the zip): instead of the five archives, an xdelta3 (VCDIFF) patch of each against the shipped one, the
SHA-1 of the shipped and the patched files, apply_linux.sh (xdelta3, else apply_xdelta.py: python3 only, Steam Deck)
and apply_windows.bat (xdelta3.exe the user gets; both find the game through Steam) from tools/dist_files/ - or Delta
Patcher by hand. No executable goes in the package. Patches without secondary compression (-S none: plain VCDIFF).
--no-skills: the DLL built with AXL_NO_SKILLS (no boss giga attacks: test5, 2026-11-04; the cooldowns are gone and the
skill HUD is off in every build since 0.0.7).

Builds first (lc2_build.py, lc2_hook.py --release: the DLL without the test harness / options file, lc2_selftest.py)
and stops if any of it fails. The package mirrors the game folder: dinput8.dll and axl_data.bin (its tables, read
from next to it: lc2/axl_data.h) at the top, the archives under
nativeDX10/X6/romPC/, with README.md (install / uninstall / controls) and CREDITS.md. Nothing of the testing aids goes
in: no axl_options.txt (all_weapons), no axl_test.txt.
"""
import datetime
import hashlib
import os
import shutil
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lc2_fmt as F  # noqa: E402
import lc2_install as I  # noqa: E402

ROOT = F.ROOT
PY = F.C.PYTHON if os.path.exists(F.C.PYTHON) else sys.executable   # (axl_config python)
DIST = os.path.join(F.BUILT, '..', 'dist')

README = """# Axl mod for Mega Man X6 (Mega Man X Legacy Collection 2) — test build {version}

Normal X becomes Axl (Mega Man X8): his gun, hover, air dash, the copy shot and DNA Change into the 8 bosses.
This is a test build: please report what looks or plays wrong.

## Install

1. Back up the five archives in `<game folder>/nativeDX10/X6/romPC/` that this package replaces
   (pl02.arc, pl02_eng.arc, kao.arc, stsel_eng.arc, cledit_eng_reg_wor.arc). Steam's "Verify integrity of game
   files" also restores them.
2. Copy everything in this folder into the game folder (`steamapps/common/Mega Man X Legacy Collection 2`), keeping
   the folder structure: `dinput8.dll` and `axl_data.bin` next to `RXC2.exe`, the archives into
   `nativeDX10/X6/romPC/`.
3. Windows: nothing else. Linux / Steam Deck (Proton): set the game's launch options to
   `WINEDLLOVERRIDES="dinput8=n,b" %command%`.

## Tested on

Linux (Steam / Proton) and Windows (test3, 2026-10-29; test5 on Windows 11, 2026-11-05).

## Windows: if it does not start

- **The game closes when you pick X6** (even unmodded): since the game's update of 2026-03 its movies are WebM (VP9).
  Install "VP9 Video Extensions" (and "Web Media Extensions") from the Microsoft Store, then restart Windows.
- **Windows 11 blocks `dinput8.dll`** (Smart App Control: the DLL is not code-signed): Windows Security > App &
  browser control > Smart App Control settings > Off. Recent Windows updates let you turn it back on later.
- Otherwise check that the unmodded game runs (Steam "Verify integrity of game files", no `dinput8.dll`).

## Uninstall

Delete `dinput8.dll` and `axl_data.bin` from the game folder and run Steam's "Verify integrity of game files".

## Controls (X6's defaults; play as normal X, not the armours)

- Shot: rapid fire, 8-way aim (up / down / diagonals with the arrows while shooting)
- Jump in the air: hover. Dash in the air: air dash
- R (next weapon) on the ground: dodge roll (invincible while tumbling)
- Special button with the plain gun: hold to charge, let go at full charge = copy shot
- DNA: a Maverick finished off by the copy shot gives its DNA - only then is its boss weapon (DNA Change) yours.
  Missed it? A beaten Maverick fights again (no dialogue) when its stage's panel is red on the stage select
- L (previous weapon): the DNA ring - left / right to turn, shot or L again to change into it, jump to select its weapon only
- With a boss weapon selected and its energy: special button = DNA Change into that boss
  - while changed: shot = the boss's attack, dash = its move, special = its second attack, R = change back
{skills_en}  - the weapon's number on the HUD = the seconds of change left (Energy Saver worn: twice as long; Weapon Drive: no drain)
- Giga button (plain gun, full gauge): Giga Attack = Giga Crush (its gauge: the pause menu's GIGA ATTACK row)
- Auto Charge option on: the copy shot charges by itself, the special button fires it. Ultimate Buster worn: the special
  button fires the copy shot at once. Overdrive running: the plain shots are X's charge shots

What changed: PATCHNOTES.md (Korean).

## 한국어 요약

- 노멀 엑스가 액셀(록맨 X8)이 됩니다. 테스트 빌드입니다.
- 설치: 위 아카이브 5개를 백업하고(스팀 "게임 파일 무결성 확인"으로도 복원됨), 이 폴더 내용을 게임 폴더에
  구조 그대로 복사합니다. 리눅스·스팀덱은 실행 옵션에 `WINEDLLOVERRIDES="dinput8=n,b" %command%`.
- 제거: `dinput8.dll`과 `axl_data.bin`을 지우고 스팀 "게임 파일 무결성 확인".
- 리눅스(Proton)와 윈도우(test3, test5는 윈도우 11)에서 실행을 확인했습니다.
- 윈도우에서 X6를 고르면 바로 꺼질 때(원본 게임도): 2026-03 업데이트부터 게임 영상이 WebM(VP9)입니다. Microsoft Store에서
  "VP9 Video Extensions"(와 "Web Media Extensions")를 설치하고 재부팅하세요.
- 윈도우 11이 `dinput8.dll`을 막을 때(스마트 앱 컨트롤, 서명 없는 DLL): Windows 보안 > 앱 및 브라우저 컨트롤 >
  스마트 앱 컨트롤 설정 > 끄기. 최근 업데이트된 윈도우는 나중에 다시 켤 수 있습니다.
- 그래도 튕기면 원본 게임(무결성 확인, dll 없음)이 실행되는지 확인해 주세요.
- 조작: 사격 연사·8방향 조준, 공중 점프 = 호버, 공중 대시, 땅에서 R = 구르기(구르는 동안 무적),
  기본 총에서 특수 버튼 차지 후 떼면 카피샷, 보스 무기를 고르고 특수 버튼 = DNA 변신
  (변신 중 사격 = 보스 공격, 대시 = 이동기, 특수 버튼 = 둘째 공격, R = 변신 해제,
  HUD 숫자 = 남은 변신 시간(초), 에너지 세이버 장착 시 두 배).
{skills_ko}
- DNA: 보스를 카피샷으로 마무리해야 그 보스의 무기(변신)를 얻습니다. 놓친 보스는 스테이지 선택 화면에서
  그 스테이지 패널이 빨간색일 때 다시 싸울 수 있습니다(대화 없음).
- L = DNA 링 메뉴(좌우로 돌리고 사격 또는 L로 변신, 점프는 무기 선택만). 기가 버튼 = 기가 어택(기가크러쉬).
- 오토차지 옵션: 카피샷 자동 차지, 특수 버튼으로 발사. 얼티밋 버스터: 특수 버튼에 바로 카피샷. 오버드라이브: 일반 사격이 엑스 차지샷.
- 바뀐 점은 PATCHNOTES.md.

Built from commit {commit}.
"""


INSTALL_XDELTA = """## Install (patches: no game file is in this package)

The archives are patched from your own copy of the game (the Steam version of 2026-11). SHA1SUMS.txt lists the file
each patch expects and what it becomes; a different file is refused (Steam: "Verify integrity of game files" first).
Running the patcher again is safe: archives already patched are left alone. Each original is kept as `<name>.orig`.

- Linux: `sh apply_linux.sh` - it finds the game in your Steam libraries (native or Flatpak Steam, other library
  disks), or give the folder: `sh apply_linux.sh "<game folder>"` (`.../steamapps/common/Mega Man X Legacy Collection 2`).
  Uses `xdelta3` when installed, else `apply_xdelta.py` (python3 only). Then set the game's launch options to
  `WINEDLLOVERRIDES="dinput8=n,b" %command%`.
- Steam Deck: in Desktop Mode, open a terminal (Konsole) in this folder and run `sh apply_linux.sh`
  (nothing to install: SteamOS has python3). Then the same launch options (game > Properties > Launch Options).
- Windows: put `xdelta3.exe` (https://github.com/jmacd/xdelta-gpl/releases) next to `apply_windows.bat` and
  double-click it - it finds the game through Steam - or run `apply_windows.bat "<game folder>"`.
  Without xdelta3.exe: Delta Patcher (https://github.com/marco-calautti/DeltaPatcher/releases), for each file in
  `patches/`: "Original file" = `<game folder>\\nativeDX10\\X6\\romPC\\<name>.arc`, "XDelta patch" =
  `patches\\<name>.arc.xdelta`, Apply (keep the backup it offers); then copy `dinput8.dll` and `axl_data.bin` next to `RXC2.exe`.

"""

DIST_FILES = os.path.join(HERE, 'dist_files')   # apply_linux.sh, apply_windows.bat (copied as they are)


SKILLS_EN = """  - giga = its Giga Attack: needs the full giga gauge (the Giga Crush's, shared) and empties it
"""
SKILLS_KO = """- 변신 중 기가 버튼 = 보스 기가어택. 기가크러쉬와 같은 기가 게이지가 꽉 차야 나가고, 쓰면 비워집니다.
"""


def sha1(path):
    with open(path, 'rb') as f:
        return hashlib.sha1(f.read()).hexdigest()


def run(args):
    r = subprocess.run([PY, '-I'] + args, cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit('failed: %s\n%s%s' % (' '.join(args), r.stdout[-2000:], r.stderr[-2000:]))
    return r.stdout


def main(version=None, xdelta=False, no_skills=False):
    commit = subprocess.run(['git', 'rev-parse', '--short', 'HEAD'], cwd=ROOT, capture_output=True,
                            text=True).stdout.strip()
    if subprocess.run(['git', 'status', '--porcelain', '--untracked-files=no', '--', 'mods/axl/lc2', 'mods/axl/tools'], cwd=ROOT,
                      capture_output=True, text=True).stdout.strip():
        commit += '-dirty'   # (uncommitted changes in the mod's sources)
    version = version or datetime.date.today().strftime('%Y%m%d')
    run(['mods/axl/tools/lc2_build.py'])
    run(['mods/axl/tools/lc2_hook.py', '--release'] + (['--no-skills'] if no_skills else []))
    out = run(['mods/axl/tools/lc2_selftest.py'])
    if out.strip():
        sys.exit('selftest:\n' + out)
    name = 'axl_lc2_%s%s' % (version, '_xdelta' if xdelta else '')
    dest = os.path.normpath(os.path.join(DIST, name))
    if os.path.exists(dest):
        shutil.rmtree(dest)
    readme = README.format(version=version, commit=commit, skills_en='' if no_skills else SKILLS_EN,
                           skills_ko='' if no_skills else SKILLS_KO)
    if xdelta:
        pdir = os.path.join(dest, 'patches')
        os.makedirs(pdir)
        sums = []
        for fn in I.FILES:
            old, new = F.orig_path(fn), os.path.join(F.BUILT, fn)
            patch = os.path.join(pdir, fn + '.xdelta')
            subprocess.run(['xdelta3', '-e', '-9', '-S', 'none', '-f', '-s', old, new, patch], check=True)   # (plain
            # VCDIFF: Delta Patcher / any xdelta3 build / apply_xdelta.py read it)
            back = patch + '.check'   # (the patch must give the build back from the shipped file)
            subprocess.run(['xdelta3', '-d', '-f', '-s', old, patch, back], check=True)
            if sha1(back) != sha1(new):
                sys.exit('xdelta round trip failed: ' + fn)
            os.remove(back)
            sums.append('%s %s %s' % (sha1(old), sha1(new), fn))
        open(os.path.join(dest, 'SHA1SUMS.txt'), 'w', newline='\n').write('\n'.join(sums) + '\n')
        shutil.copy2(os.path.join(DIST_FILES, 'apply_linux.sh'), os.path.join(dest, 'apply_linux.sh'))
        bat = open(os.path.join(DIST_FILES, 'apply_windows.bat')).read()
        open(os.path.join(dest, 'apply_windows.bat'), 'w', newline='\r\n').write(bat)   # (CRLF for cmd.exe)
        shutil.copy2(os.path.join(HERE, 'apply_xdelta.py'), os.path.join(dest, 'apply_xdelta.py'))
        a, b = readme.index('## Install'), readme.index('## Tested on')
        readme = readme[:a] + INSTALL_XDELTA + readme[b:]
    else:
        rom = os.path.join(dest, 'nativeDX10', 'X6', 'romPC')
        os.makedirs(rom)
        for fn in I.FILES:
            shutil.copy2(os.path.join(F.BUILT, fn), os.path.join(rom, fn))
    shutil.copy2(os.path.join(F.BUILT, 'release', I.DLL), os.path.join(dest, I.DLL))
    shutil.copy2(os.path.join(F.BUILT, I.DATA), os.path.join(dest, I.DATA))   # (lc2_hook.py packed it with the DLL)
    shutil.copy2(os.path.join(ROOT, 'mods', 'axl', 'CREDITS.md'), os.path.join(dest, 'CREDITS.md'))
    shutil.copy2(os.path.join(ROOT, 'mods', 'axl', 'PATCHNOTES.md'), os.path.join(dest, 'PATCHNOTES.md'))
    open(os.path.join(dest, 'README.md'), 'w').write(readme)
    zpath = dest + '.zip'
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED) as z:
        for d, _, files in os.walk(dest):
            for f in sorted(files):
                full = os.path.join(d, f)
                z.write(full, os.path.join(name, os.path.relpath(full, dest)))
    print('wrote %s (%d bytes)' % (zpath, os.path.getsize(zpath)))
    for d, _, files in os.walk(dest):
        for f in sorted(files):
            print('  %s' % os.path.relpath(os.path.join(d, f), dest))


if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    main(*args[:1], xdelta='--xdelta' in sys.argv, no_skills='--no-skills' in sys.argv)
