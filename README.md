# Axl mod for Mega Man X6 (LC2)

(LC2: Mega Man X Legacy Collection 2. This is the product name in the DLL's version resource.)

A fan mod for the Steam release of **Mega Man X Legacy Collection 2**: in X6, normal X becomes Axl (Mega Man X8) -
his gun with 8-way aim, hover, air dash, dodge roll, the copy shot and DNA Change into the eight Mavericks.
It is a `dinput8.dll` hook (C, built with zig) with its data file `axl_data.bin`, plus rebuilt game archives
(`pl02*.arc`, `kao.arc`, `stsel_eng.arc`, `cledit_eng_reg_wor.arc`).

**No game data is in this repository.** Every picture, sound, table or archive the mod needs is made by the build
from your own copies of the games. The DLL itself is built from this repository alone (the game-derived tables are
in `axl_data.bin`, which it reads at start-up), so GitHub Actions builds it with no game files
(`.github/workflows/build-dll.yml`). Releases ship the DLL, `axl_data.bin` and xdelta patches against your own
archives (`lc2_dist.py --xdelta`).

What changed between test builds: `mods/axl/PATCHNOTES.md` (Korean). Credits: `mods/axl/CREDITS.md`.

## What you need

- **Mega Man X Legacy Collection 2** (Steam). The build reads X6's archives (`nativeDX10/X6/romPC`) and X8's sounds
  (`nativeDX10/X8/sound`: Axl's voice and sound effects). The archives must be the shipped ones (Steam:
  "Verify integrity of game files").
- **PS1 Mega Man X6 (USA) v1.1**, a MODE2/2352 `.bin` image of your own disc. Axl's PS1 build step (`build_axl.py
  --lc2`) converts the sprites on the PS1 data, which the PC archives carry. Expected files after extraction:

  | file | SHA-1 |
  |---|---|
  | `ROCK_X6.DAT` | `32c2c1e95efd59d8608e169ad41dea201a9c9241` |
  | `ROCK_X6.BIN` | `00a49b8bc5cfc897276f605c7b92f2d49d42e7fe` |
  | `SLUS_013.95` | `667ce8f792cfaa69abe6e999a87513f798bbcac4` |

  Optional: PS1 Mega Man X6 (Japan) the same way (`x6_jp_dir`): only a consistency check uses it.
- **The Axl sprite sheet**: "Ultimate Axl Sprite Sheet (2021)" by JuggaloManX and others
  (https://www.deviantart.com/juggalomanx/art/Ultimate-Axl-Sprite-Sheet-%282021%29-824018313; also on The Spriters
  Resource as "Custom / Edited - Mega Man X Customs - Axl - Axl (PS1-Style, 2021 Update)"). It is not in this
  repository (its makers' terms): download it yourself and save it as `mods/axl/work/sheet/axl_sheet.png`
  (2941 x 4939, RGB). The cut / placement data in `mods/axl/work/sheet/*.json` was made for the file with SHA-1
  `8206b44ca0f4935414f4233c325ec719db55f477`; another version of the sheet will cut wrong.
  Please keep the sheet's credits (`mods/axl/CREDITS.md`; the build also adds them to X6's staff roll).
- Optional: a sound file for Axl's "Ready" at the stage start (`ready_sound`). Without it the build uses X8's
  announcer "READY".
- Python 3.12 with `requirements.txt` (numpy, Pillow, ziglang - zig is the C compiler for the DLL), `ffmpeg`
  (sound conversion) and `xdelta3` (release patches). Linux was used; Windows is untested for the build.

## Setup

```sh
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt

# the PS1 files (any folder; games/ is ignored by git)
.venv/bin/python mods/axl/tools/psxiso.py extract "Mega Man X6 (USA) (v1.1).bin" games/x6_us/fs

# where your data is: copy and edit (or set the environment variables named in mods/axl/tools/axl_config.py)
cp config.example.json config.local.json
```

`config.local.json` (not committed; relative paths are from the repository root):

| key | meaning | default |
|---|---|---|
| `lc2_dir` | the game folder (the one with `RXC2.exe`) | `~/.local/share/Steam/steamapps/common/Mega Man X Legacy Collection 2` |
| `x6_us_dir` | folder with `fs/` (the extracted PS1 files) | `games/x6_us` |
| `x6_jp_dir` | the same for the Japanese PS1 game (optional) | `games/x6_jp` |
| `ready_sound` | Axl's "Ready" sound file (optional) | `mods/axl/sound/axl_ready.ogg` |
| `lc2_save` | the game's save (only `lc2_test.py`: kept safe around test runs) | Steam's userdata, when there is one |
| `python` | the Python `lc2_dist.py` builds with | `.venv/bin/python` |

## Build

```sh
P=.venv/bin/python
$P mods/axl/tools/build_axl.py mods/axl/work/build --lc2   # PS1 step: Axl's sprites, scripts, axl_gen.h
$P mods/axl/tools/lc2_build.py                              # -> mods/axl/work/lc2/*.arc (+ generated headers)
$P mods/axl/tools/lc2_hook.py                               # -> mods/axl/work/lc2/axl_data.bin, dinput8.dll (test harness)
$P mods/axl/tools/lc2_hook.py --release                     # -> mods/axl/work/lc2/release/dinput8.dll
$P mods/axl/tools/lc2_selftest.py                           # silent = passed
```

The DLL alone, without any game data (what the CI workflow runs):

```sh
python mods/axl/tools/lc2_dll.py --release [--no-skills] [--out PATH]   # Python standard library + ziglang
```

### The data file `axl_data.bin`

The DLL's game-derived tables (Axl's animation scripts, the bosses' palettes and pose sequences, picture indices of
the rebuilt archives, ...) are not compiled in. The build writes them as C headers into `mods/axl/work/lc2/gen/`
(`lc2_build.py` via `lc2_dna.py`, `lc2_hook.py`, and `build_axl.py`'s `axl_gen.h`), then `lc2_data.py pack` (run by
`lc2_hook.py`) compiles `mods/axl/lc2/data/axl_data_pack.c` with them for the build machine and writes
`mods/axl/work/lc2/axl_data.bin`: the structure `axl_data_t` of `mods/axl/lc2/axl_data.h` with a magic number, a
format version, its size and an FNV-1a hash. `lc2_install.py install` and the release's apply scripts put it next
to `dinput8.dll`; at start-up the DLL reads it from there and, if it is missing or its magic, version, size or hash
does not match, installs no hook at all (the game runs unmodded). `lc2_data.py check` and `lc2_selftest.py` check
the file.

`build_axl.py` without `--lc2` also builds the PS1 disc image, which needs the PS1 C sources (`mods/axl/src`, not
part of this repository). Run it again after editing the sheet data.

`--no-skills` (lc2_hook.py, lc2_dll.py, lc2_dist.py): the DLL without the boss skill cooldowns, skill HUD and boss
Giga attacks. The DLL's version (its VERSIONINFO resource, `mods/axl/lc2/axl_version.rc`) comes from
`mods/axl/VERSION`.

## Install / release

- Your own game: `$P mods/axl/tools/lc2_install.py install` (keeps the shipped archives in `mods/axl/work/lc2/backup/`,
  copies the build, the DLL and `axl_data.bin`; on Linux it also sets the Proton prefix's `dinput8` override).
  `restore` undoes it.
- Release package: `$P mods/axl/tools/lc2_dist.py VERSION --xdelta [--no-skills]` -> `mods/axl/work/dist/`:
  `dinput8.dll`, `axl_data.bin`, xdelta patches against the shipped archives, `SHA1SUMS.txt`, `apply_linux.sh` / `apply_windows.bat` /
  `apply_xdelta.py`, README (install for players), CREDITS, PATCHNOTES. No game file goes in it.
- In-game tests (`lc2_test.py`, `lc2_e2e.py`, `lc2_regress.py`, scenarios in `mods/axl/tools/e2e/`) start the game
  through Steam with the debug DLL and log what the scenario checks.

## Layout

- `mods/axl/lc2/` - the DLL (C); `axl_data.h` the data file's layout, `data/axl_data_pack.c` its packer. The
  generated headers (`axl_anims.h`, `axl_dna_looks.h`, `axl_hud_looks.h`, `axl_gen.h`, `axl_pal.h`) and
  `axl_data.bin` are written by the build from your game data under `mods/axl/work/lc2/` and are not committed.
- `.github/workflows/build-dll.yml` - builds `dinput8.dll` (release, and the `--no-skills` variant) on every push and
  tag, from this repository only, and keeps it as a workflow artifact.
- `mods/axl/tools/` - build, install, dist and test tools (Python); `axl_config.py` - where your data is.
- `mods/axl/work/sheet/`, `mods/axl/work/face/` - the mod's own edit data (sheet cuts, placements, animation edits,
  portrait layout); everything else under `work/` is build output.

## Windows: if the game does not start

- The game closes when you pick X6 (even unmodded): since the game's update of 2026-03 its movies are WebM (VP9).
  Install "VP9 Video Extensions" (and "Web Media Extensions") from the Microsoft Store, then restart Windows.
- Windows 11 blocks `dinput8.dll`: Smart App Control blocks unsigned code and has no per-app exception. Windows
  Security > App & browser control > Smart App Control settings > Off (recent Windows updates let you turn it back
  on). The DLL is not code-signed yet (see "Code signing policy").

## 한국어 요약

- 레거시 컬렉션 2(스팀)의 X6에서 노멀 엑스를 액셀로 바꾸는 팬 모드의 소스입니다. 게임 데이터는 들어 있지 않고,
  빌드가 사용자 자신의 게임(레거시 컬렉션 2, PS1 록맨 X6 북미 v1.1 이미지)과 따로 받은 스프라이트 시트에서 만듭니다.
- 준비: 위 "What you need"와 "Setup", 빌드: "Build". 배포본은 `lc2_dist.py 버전 --xdelta --no-skills`.
- 윈도우에서 X6를 고르면 바로 꺼지면 Microsoft Store에서 "VP9 Video Extensions"를 설치하세요(2026-03 업데이트부터 영상이 WebM).
  윈도우 11 스마트 앱 컨트롤이 서명 없는 `dinput8.dll`을 막으면: Windows 보안 > 앱 및 브라우저 컨트롤 > 스마트 앱 컨트롤 설정 > 끄기.
- DLL은 게임 데이터 없이 이 저장소만으로 빌드됩니다(GitHub Actions). 게임에서 뽑은 표는 빌드가 `axl_data.bin`으로
  만들고, DLL이 시작할 때 자기 옆의 이 파일을 읽습니다. 파일이 없거나 맞지 않으면 아무것도 바꾸지 않습니다.

## Code signing policy

- What is signed: only `dinput8.dll`, as built by the `build-dll` GitHub Actions workflow
  (`.github/workflows/build-dll.yml`) from a tagged commit of this repository. Nothing else is signed: not
  `axl_data.bin` (the game-derived tables: made by the build from the maintainer's copy of the game, shipped
  next to the DLL in the release package, read as data only), the patches or the scripts.
- The workflow builds from source only: the C sources in `mods/axl/lc2/` and `mods/axl/tools/lc2_dll.py`, compiled
  with zig (`ziglang` from PyPI, the version pinned in `requirements.txt`). No game data, prebuilt binaries or
  proprietary components go into the DLL. The file carries its product name and version (VERSIONINFO, from
  `mods/axl/VERSION`).
- Committers and reviewers: hoodcat (https://github.com/Hoodcat2255). Approvers: hoodcat - every signing request is
  approved by the maintainer by hand, for release tags only.
- Privacy: this program does not transfer any information to other networked systems. The DLL opens no network
  connection; it only reads `axl_data.bin` next to itself and patches the running game in memory.

## Disclaimer

This is an unofficial fan project, not affiliated with or endorsed by Capcom. Mega Man, Mega Man X, Axl and the
related names are trademarks of Capcom Co., Ltd. The repository contains no game data; you need your own legal copies
of the games. The Axl sprites belong to their makers (see `mods/axl/CREDITS.md`).

## License

The mod's code and tools: MIT (`LICENSE`). The license does not cover game data or the sprite sheet.
