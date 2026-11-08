#!/bin/bash
# Runs the recompiled game for a while and keeps its log and dumped frames.
# Usage: run.sh <name> <seconds> [VAR=value ...] [--runner-option ...]   Log: logs/run_<name>.log  Frames: build/runs/<name>/
# Runner options: --skip-intro skips the movies; --scale=N sets the internal resolution (default 2; write it with "=" here).
source "$(dirname "$0")/env.sh"
NAME="$1"; SECS="$2"; shift 2
VARS=(); OPTS=()
for a in "$@"; do case "$a" in --*) OPTS+=("$a") ;; *) VARS+=("$a") ;; esac; done
OUT="$MARZ/build/runs/$NAME"
rm -rf "$OUT"; mkdir -p "$OUT"
cd "$OUT"
STATUS=0
env PS2X_CD_IMAGE="$(cygpath -m "$ISO_IMAGE")" PS2X_DUMP_FRAMES="$(cygpath -m "$OUT")" "${VARS[@]}" \
    timeout --kill-after=5 "$SECS" "$MARZ/build/runtime/ps2xRuntime/ps2EntryRunner.exe" "$(cygpath -m "$ELF")" "${OPTS[@]}" \
    > "$MARZ/logs/run_$NAME.log" 2>&1 || STATUS=$?
echo "exit=$STATUS log_lines=$(wc -l < "$MARZ/logs/run_$NAME.log") frames=$(ls "$OUT" | wc -l)"
