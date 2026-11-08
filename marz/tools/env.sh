# Sourced by the other scripts in this folder. Run them from the MSYS2 CLANG64 shell:
#   J:\Marz-decomp\toolchain\msys64\usr\bin\env.exe MSYSTEM=CLANG64 CHERE_INVOKING=1 /usr/bin/bash -lc "marz/tools/<script>.sh"
set -euo pipefail
MARZ_TOP="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
export MARZ="$MARZ_TOP/marz"
export BASE="$MARZ_TOP/external/gitaroo-man-native"
export RECOMP_SRC="$BASE/external/PS2Recomp"
export TOOLCHAIN="$MARZ_TOP/toolchain"
export GHIDRA="$TOOLCHAIN/ghidra_12.1.4_PUBLIC"
JDK="$(ls -d "$TOOLCHAIN"/jdk-25* | head -1)"
export JAVA_HOME="$(cygpath -w "$JDK")"
# Ghidra keeps its settings/cache under these. Packaged apps get AppData\Local redirected, which makes
# Ghidra's OSGi layer reject its own cache folder, and C: has little room anyway.
GHIDRA_HOME="$TOOLCHAIN/ghidra_home"
mkdir -p "$GHIDRA_HOME/local" "$GHIDRA_HOME/roaming" "$GHIDRA_HOME/tmp"
export LOCALAPPDATA="$(cygpath -w "$GHIDRA_HOME/local")"
export APPDATA="$(cygpath -w "$GHIDRA_HOME/roaming")"
export TEMP="$(cygpath -w "$GHIDRA_HOME/tmp")"
export TMP="$TEMP"
export ISO_IMAGE="$MARZ_TOP/Dennou Senki Virtual-On Marz (Japan).iso"
# MARZ_GAME_DIR: the extracted disc folder to boot (default iso_jp; the English build uses a patched copy).
export ELF="${MARZ_GAME_DIR:-$MARZ/iso_jp}/SLPM_653.03"
mkdir -p "$MARZ/build" "$MARZ/logs"
