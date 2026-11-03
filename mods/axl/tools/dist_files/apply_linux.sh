#!/bin/sh
# Axl mod (Mega Man X Legacy Collection 2): patch the game's 5 archives, keep <name>.orig, copy dinput8.dll.
#
#   sh apply_linux.sh                 the game folder found in the Steam libraries (native, Flatpak, Steam Deck)
#   sh apply_linux.sh "<game folder>" that folder (steamapps/common/Mega Man X Legacy Collection 2)
#
# Patches with xdelta3 when it is installed, else with apply_xdelta.py (python3 only: Steam Deck).
# Each archive is checked against SHA1SUMS.txt before (the shipped file) and after (the patched one); an archive
# already patched is left alone, so running it again is safe.
set -e
here=$(cd "$(dirname "$0")" && pwd)
sub="steamapps/common/Mega Man X Legacy Collection 2"

find_game() {
  for root in "$HOME/.local/share/Steam" "$HOME/.steam/steam" "$HOME/.steam/root" \
              "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam" \
              "$HOME/.var/app/com.valvesoftware.Steam/data/Steam"; do
    [ -d "$root" ] || continue
    if [ -f "$root/$sub/RXC2.exe" ]; then echo "$root/$sub"; return 0; fi
    vdf="$root/steamapps/libraryfolders.vdf"
    [ -f "$vdf" ] || continue
    # lines like:   "path"   "/media/disk/SteamLibrary"
    grep '"path"' "$vdf" | sed 's/.*"path"[[:space:]]*"\(.*\)".*/\1/' | while IFS= read -r lib; do
      if [ -f "$lib/$sub/RXC2.exe" ]; then echo "$lib/$sub"; break; fi
    done | head -n 1 | grep . && return 0
  done
  return 1
}

if [ -n "$1" ]; then
  game=$1
else
  game=$(find_game) || { echo "game folder not found: sh apply_linux.sh \"<game folder>\""; exit 1; }
  echo "game folder: $game"
fi
[ -f "$game/RXC2.exe" ] || { echo "not the game folder (no RXC2.exe): $game"; exit 1; }
rom="$game/nativeDX10/X6/romPC"

if command -v xdelta3 >/dev/null 2>&1; then
  patch_one() { xdelta3 -d -f -s "$1" "$2" "$3"; }
elif command -v python3 >/dev/null 2>&1; then
  patch_one() { python3 "$here/apply_xdelta.py" "$1" "$2" "$3"; }
else
  echo "needs xdelta3 (sudo apt install xdelta3) or python3"; exit 1
fi
sha() { sha1sum "$1" | cut -c1-40; }

while read -r want_old want_new name; do
  [ -n "$name" ] || continue
  f="$rom/$name"
  [ -f "$f" ] || { echo "$name: missing in $rom"; exit 1; }
  cur=$(sha "$f")
  if [ "$cur" = "$want_new" ]; then echo "$name: already patched"; continue; fi
  [ "$cur" = "$want_old" ] || { echo "$name: not the expected file (Steam: Verify integrity of game files, then again)"; exit 1; }
  cp -p "$f" "$f.orig"
  patch_one "$f.orig" "$here/patches/$name.xdelta" "$f.new"
  [ "$(sha "$f.new")" = "$want_new" ] || { rm -f "$f.new"; echo "$name: patched file is wrong"; exit 1; }
  mv "$f.new" "$f"
  echo "$name: patched"
done < "$here/SHA1SUMS.txt"
cp "$here/dinput8.dll" "$game/dinput8.dll"
echo 'done. Steam launch options: WINEDLLOVERRIDES="dinput8=n,b" %command%'
