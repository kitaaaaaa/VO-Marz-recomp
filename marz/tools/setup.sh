#!/bin/bash
# Builds the game from the user's own disc image and writes Marz.exe at the top of the checkout (setup.bat runs this).
# Usage: setup.sh <Virtual-On Marz (Japan) .iso>
# Steps already done are skipped on a second run; recompiling and building always run.
set -euo pipefail
TOP="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ISO="$(cygpath -u "$1")"
[ -f "$ISO" ] || { echo "no disc image at $1" >&2; exit 1; }
TC="$TOP/toolchain"
mkdir -p "$TC/downloads"

fetch() {  # url sha256 file folder-it-unpacks-to [folder to unpack into, default toolchain]
    [ -d "$TC/$4" ] && return
    echo "Downloading $(basename "$1")..."
    curl -L --fail -o "$TC/downloads/$3" "$1"
    echo "$2 *$TC/downloads/$3" | sha256sum -c --quiet
    unzip -q -o "$TC/downloads/$3" -d "$TC/${5:-}"
}
fetch https://github.com/adoptium/temurin25-binaries/releases/download/jdk-25.0.5%2B8/OpenJDK25U-jdk_x64_windows_hotspot_25.0.5_8.zip \
    636bdc13dc649ddc7d5921c5c7d23cca6e44fd19d662b45cc9c02135a6c96c42 jdk25.zip jdk-25.0.5+8
fetch https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.1.4_build/ghidra_12.1.4_PUBLIC_20260921.zip \
    ddac49f903da9d5bac833e5cc79395098b9c33cfd3279be5f31bd00387d2d4db ghidra.zip ghidra_12.1.4_PUBLIC
# Ghidra's PS2 (r5900) processor.
fetch https://github.com/chaoticgd/ghidra-emotionengine-reloaded/releases/download/v2.1.39/ghidra_12.1.4_PUBLIC_20261029_ghidra-emotionengine-reloaded.zip \
    088369c4d7de43cc95dbf254936dfed45eae5eddfcaf8b928189344bcc6a4dac ee-ext.zip \
    ghidra_12.1.4_PUBLIC/Ghidra/Extensions/ghidra-emotionengine-reloaded ghidra_12.1.4_PUBLIC/Ghidra/Extensions

export ISO_IMAGE="$ISO"
unset MARZ_GAME_DIR
source "$TOP/marz/tools/env.sh"
cd "$MARZ"

echo "Extracting the disc..."
python3 tools/extract_iso.py "$ISO" iso_jp > logs/setup_extract.log
[ -f iso_jp/SLPM_653.03 ] || { echo "SLPM_653.03 is not on this disc: the Japanese Virtual-On Marz (SLPM-65303) is needed" >&2; exit 1; }

# English game folder: a copy of the disc with the translation written in (text_insert.py, font_narrow.py).
if [ ! -f build/setup_en.done ]; then
    echo "Making the English game folder..."
    rm -rf build/setup_text
    python3 tools/cvm_extract.py iso_jp/SLPM_653.03 iso_jp/ROFSVO4.CVM build/setup_text --list > build/cvm_list.txt
    python3 tools/cvm_extract.py iso_jp/SLPM_653.03 iso_jp/ROFSVO4.CVM build/setup_text MSD_ DB_ > logs/setup_text.log
    python3 tools/text_dump.py build/setup_text iso_jp/SLPM_653.03 build/setup_jp.json >> logs/setup_text.log
    mkdir -p iso_en
    rsync -a --exclude mc0 --exclude mc1 iso_jp/ iso_en/
    python3 tools/text_insert.py build/setup_jp.json build/cvm_list.txt iso_en translation/en/*.json
    python3 tools/font_narrow.py iso_en/ROFSVO4.CVM build/cvm_list.txt
    touch build/setup_en.done
fi
# Memory card folders (the runtime's default: mc0 next to the ELF).
mkdir -p iso_jp/mc0 iso_en/mc0

if [ ! -f build/tools/ps2xRecomp/ps2_recomp.exe ] || [ ! -f build/runtime/build.ninja ]; then
    echo "Configuring the build (downloads the libraries it uses)..."
    bash tools/configure.sh > logs/setup_configure.log 2>&1 || { tail -n 30 logs/setup_configure.log; exit 1; }
fi
if [ ! -d build/ghidra/Marz.rep ]; then
    echo "Analysing the game program with Ghidra (a few minutes)..."
    bash tools/ghidra_import.sh
fi
echo "Exporting the function list..."
bash tools/ghidra_export.sh
echo "Lifting the VU1 microprograms..."
python3 tools/vu1lift_marz.py --elf iso_jp/SLPM_653.03 --targets config/vu1_targets.json > logs/setup_vu1.log
echo "Recompiling and building (this takes a while)..."
bash tools/recomp.sh

echo "Writing Marz.exe..."
mkdir -p bin
cp build/runtime/ps2xRuntime/ps2EntryRunner.exe bin/
# The DLLs it needs from the toolchain, so the game runs without it.
ldd bin/ps2EntryRunner.exe | awk '$3 ~ "^/clang64/" {print $3}' | sort -u | xargs -r cp -t bin/
cygpath -w "$ISO" > bin/iso_path.txt
clang++ -std=c++20 -O2 -municode -mwindows -static tools/marz_launcher.cpp -o "$TOP/Marz.exe"
echo "Setup finished."
