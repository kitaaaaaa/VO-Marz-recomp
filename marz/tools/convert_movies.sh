#!/bin/bash
# Converts the game's 5 Sofdec movies (OP_MOVIE0001A/B, ENDINGA/B/C in ROFSVO4.CVM) to mp4 for the native movie player
# (marz_movie.cpp). Run from the MSYS2 CLANG64 shell; needs ffmpeg and python3.
# Usage: convert_movies.sh [game dir]   (default: $MARZ_GAME_DIR, else iso_jp)   Output: <game dir>/movies/*.mp4
# The mp4 files are made from the disc: they are game material, keep them out of the repository.
source "$(dirname "$0")/env.sh"
GAME_DIR="${1:-${MARZ_GAME_DIR:-$MARZ/iso_jp}}"
OUT="$GAME_DIR/movies"
TMP="$OUT/.sfd"
mkdir -p "$OUT" "$TMP"
python3 "$MARZ/tools/cvm_extract.py" "$GAME_DIR/SLPM_653.03" "$GAME_DIR/ROFSVO4.CVM" "$TMP" OP_MOVIE0001 ENDING
for sfd in "$TMP"/*.SFD; do
    name="$(basename "$sfd" .SFD)"
    # 640x448 MPEG-2 + ADX in, H.264 + AAC out; the 4:3 display aspect is kept.
    ffmpeg -hide_banner -loglevel error -y -i "$sfd" -c:v libx264 -crf 16 -preset slow -pix_fmt yuv420p \
        -c:a aac -b:a 256k -movflags +faststart "$OUT/$name.mp4"
    echo "$OUT/$name.mp4"
done
rm -rf "$TMP"
