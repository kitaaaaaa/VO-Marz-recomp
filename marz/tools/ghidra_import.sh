#!/bin/bash
# One-time: creates build/ghidra/Marz from the game ELF and runs auto-analysis (slow).
source "$(dirname "$0")/env.sh"
mkdir -p "$MARZ/build/ghidra"
cmd //c "$(cygpath -w "$GHIDRA/support/analyzeHeadless.bat")" \
    "$(cygpath -w "$MARZ/build/ghidra")" Marz -import "$(cygpath -w "$ELF")" \
    -processor r5900:LE:32:default -cspec default -overwrite -max-cpu "$(nproc)" \
    < /dev/null > "$MARZ/logs/ghidra_import.log" 2>&1
tail -n 3 "$MARZ/logs/ghidra_import.log"
