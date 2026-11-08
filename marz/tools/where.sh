#!/bin/bash
# Runs the game and, after <delay> seconds, prints every host thread's stack (generated guest functions
# carry their address in their name, e.g. dvCiOpen_0x4d30a0) to logs/where_<name>.txt.
# Usage: where.sh <name> <delay secs> <run secs> [VAR=value ...]
source "$(dirname "$0")/env.sh"
NAME="$1"; DELAY="$2"; RUN="$3"; shift 3
bash "$MARZ/tools/run.sh" "$NAME" "$RUN" "$@" &
sleep "$DELAY"
PID="$(powershell -NoProfile -Command "(Get-Process ps2EntryRunner).Id" | tr -d '\r')"
lldb -b -p "$PID" -o "thread backtrace all -c 40" -o "process detach" > "$MARZ/logs/where_$NAME.txt" 2>&1 || true
wait
grep -nE '^\* thread|^  thread' "$MARZ/logs/where_$NAME.txt" | cut -c1-160
