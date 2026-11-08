#!/bin/bash
# Summarises a run: guest thread states, GS activity, the end of the game's log, frames with content.
# Usage: report.sh <name>
source "$(dirname "$0")/env.sh"
NAME="$1"
LOG="$MARZ/logs/run_$NAME.log"
grep 'trace:ee. host' "$LOG" | tail -n 1 | cut -c1-1400 || true
grep -E 'trace:vu1|trace:ee. finish' "$LOG" | tail -n 2 | cut -c1-260 || true
grep -vE 'trace:|IOP printf|^$|FILEIO|TEXTURE:|SHADER:|^\[call\]|^\[ret ' "$LOG" | tail -n "${2:-25}" | cut -c1-260 || true
python3 - "$(cygpath -m "$MARZ/build/runs/$NAME")" <<'E'
import glob, os, sys
files = sorted(glob.glob(sys.argv[1] + '/*.png'))
sizes = [(os.path.getsize(f), os.path.basename(f)) for f in files]
print('frames', len(files), [s for s, _ in sizes[::max(1, len(sizes) // 20)]])
if sizes:
    print('largest', sorted(sizes)[-3:], 'last', sizes[-1])
E
