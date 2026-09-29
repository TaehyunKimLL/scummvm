#!/bin/bash
# Bake the DOS L-preset and U-preset Korean fonts.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
out="$here/../../dists/engine-data/hires_text/dos"
mkdir -p "$out"

# L preset: neodgm 16 px, 1 bpp, KS X 1001 without Hanja.
# The in-tree neodgm; NEODGM=/path/to/neodgm.ttf bakes another copy.
src="${NEODGM:-$here/../../dists/engine-data/hires_text/fonts/neodgm/neodgm.ttf}"
if [ ! -f "$src" ]; then
	echo "bake-dos-fonts: no font at $src" >&2
	exit 1
fi
python3 "$here/mkfont.py" "$src" "$out/KO2350.SVF" --size 16 --cell 16 --bpp 1 --unicode ascii,ksx1001-nohanja

# U preset: NanumGothic-Bold 18 px, 2 bpp, ASCII + cp949 (full modern Hangul).
# NANUMGOTHIC_BOLD=/path/to/NanumGothic-Bold.ttf bakes another copy.
nanum="${NANUMGOTHIC_BOLD:-$here/../../dists/engine-data/hires_text/fonts/nanumgothic/NanumGothic-Bold.ttf}"
if [ ! -f "$nanum" ]; then
	echo "bake-dos-fonts: no font at $nanum" >&2
	exit 1
fi
python3 "$here/mkfont.py" "$nanum" "$out/KOCP949.SVF" --size 18 --bpp 2 --unicode ascii,cp949
