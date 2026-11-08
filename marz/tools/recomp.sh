#!/bin/bash
# Regenerates C++ from the ELF using build/marz.gen.toml, then rebuilds the runner. Output is synced by
# content so only changed translation units rebuild.
source "$(dirname "$0")/env.sh"
cd "$MARZ"
python3 tools/add_reloc_targets.py "$(cygpath -m "$ELF")" build/functions.csv
python3 tools/gen_toml.py build/ghidra_export.toml config/marz_overrides.toml build/marz.gen.toml "$MARZ"
rm -rf build/recomp.new
mkdir -p build/recomp.new build/recomp src
build/tools/ps2xRecomp/ps2_recomp build/marz.gen.toml > logs/recomp.log 2>&1 || { tail -n 20 logs/recomp.log; exit 1; }
tail -n 1 logs/recomp.log
# CMake only needs to run when generated files were added or removed: it takes minutes with this many.
FILES_CHANGED=1
if [ "$(ls build/recomp)" = "$(ls build/recomp.new)" ]; then FILES_CHANGED=0; fi
rsync -a --checksum --delete build/recomp.new/ build/recomp/
rm -rf build/recomp.new
if [ "$FILES_CHANGED" = 1 ]; then cmake "$MARZ/build/runtime" > logs/reconfigure.log 2>&1; fi
ninja -C build/runtime -k 0 -j"$(nproc)" ps2EntryRunner > logs/build_runtime.log 2>&1 || { grep -m30 -E 'error|FAILED' logs/build_runtime.log; exit 1; }
tail -n 1 logs/build_runtime.log
