#!/bin/bash
# Bake the DOS L-preset font: neodgm 16 px, 1 bpp, KS X 1001 without Hanja.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
src="${NEODGM:-$HOME/work/scummvm/fonts/neodgm.ttf}"
if [ ! -f "$src" ]; then
	src="$here/../../dists/engine-data/hires_text/fonts/neodgm/neodgm.ttf"
fi
out="$here/../../dists/engine-data/hires_text/dos"
mkdir -p "$out"
python3 "$here/mkfont.py" "$src" "$out/KO2350.SVF" --size 16 --cell 16 --bpp 1 --unicode ascii,ksx1001-nohanja
