#!/bin/bash
# Applies marz/config/symbols/*.txt to the Ghidra project and exports build/functions.csv plus the
# exporter's TOML (build/ghidra_export.toml), which marz/config/marz.toml takes its stubs from.
source "$(dirname "$0")/env.sh"
cat "$MARZ"/config/symbols/*.txt 2>/dev/null | grep -v '^#' | grep . > "$MARZ/build/symbols_all.txt" || true
# One script folder: cmd splits arguments on ';', so a path list can't reach analyzeHeadless.bat.
SCRIPTS="$MARZ/build/ghidra_scripts"
mkdir -p "$SCRIPTS"
cp "$BASE/tools/ghidra/ApplySymbols.java" "$RECOMP_SRC/ps2xRecomp/tools/ghidra/ExportPS2Functions.java" "$SCRIPTS/"
cmd //c "$(cygpath -w "$GHIDRA/support/analyzeHeadless.bat")" \
    "$(cygpath -w "$MARZ/build/ghidra")" Marz -process SLPM_653.03 -noanalysis \
    -scriptPath "$(cygpath -w "$SCRIPTS")" \
    -postScript ApplySymbols.java "$(cygpath -w "$MARZ/build/symbols_all.txt")" \
    -postScript ExportPS2Functions.java "$(cygpath -w "$MARZ/build/ghidra_export.toml")" "$(cygpath -w "$MARZ/build/functions.csv")" \
    < /dev/null > "$MARZ/logs/ghidra_export.log" 2>&1
grep -E 'ApplySymbols:|Exported [0-9]+ functions' "$MARZ/logs/ghidra_export.log"
