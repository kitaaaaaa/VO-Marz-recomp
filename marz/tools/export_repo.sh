#!/bin/bash
# Copies the hand-written project files into a checkout of the GitHub repo (argument 1), laid out as this
# tree is: marz/, and the parts of external/gitaroo-man-native it uses, with its PS2Recomp submodule's
# tracked files as they are in the working tree. An allow-list: nothing generated or taken from the game
# (ISO, ELF, IRX, recompiled code, Ghidra exports, generated VU1 code, logs, dumps) is copied.
set -euo pipefail
TOP="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEST="$1"
[ -d "$DEST/.git" ] || { echo "not a git checkout: $DEST" >&2; exit 1; }
copy() { mkdir -p "$DEST/$(dirname "$1")"; cp "$TOP/$1" "$DEST/$1"; }

for f in marz/src/*.cpp marz/src/*.inc marz/tools/*.sh marz/tools/*.py marz/tools/*.cpp marz/config/*.toml marz/config/*.json marz/config_us/*.txt \
         marz/translation/*.md marz/translation/en/*.json; do
    copy "${f#$TOP/}"
done
copy setup.bat
BASE=external/gitaroo-man-native
for f in LICENSE tools/vu1lift/vu1lift.py tools/vu1lift/vu1dis.py tools/vu1lift/census_compare.py \
         tools/ghidra/ApplySymbols.java tools/ghidra/DumpDecompilation.java tools/ghidra/export.sh tools/ghidra/import.sh; do
    copy "$BASE/$f"
done
RECOMP=$BASE/external/PS2Recomp
git -C "$TOP/$RECOMP" ls-files | while IFS= read -r f; do
    [ -f "$TOP/$RECOMP/$f" ] && copy "$RECOMP/$f"
done
echo "copied into $DEST"
