#!/bin/bash
# Bake the DOS L-preset font: neodgm 16 px, 1 bpp, KS X 1001 without Hanja.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
# The in-tree neodgm; NEODGM=/path/to/neodgm.ttf bakes another copy.
src="${NEODGM:-$here/../../dists/engine-data/hires_text/fonts/neodgm/neodgm.ttf}"
if [ ! -f "$src" ]; then
	echo "bake-dos-fonts: no font at $src" >&2
	exit 1
fi
out="$here/../../dists/engine-data/hires_text/dos"
mkdir -p "$out"
python3 "$here/mkfont.py" "$src" "$out/KO2350.SVF" --size 16 --cell 16 --bpp 1 --unicode ascii,ksx1001-nohanja
