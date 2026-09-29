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

# U preset, two faces (the U maps pick one per SCI font id):
# - UI (status/menu bar, parser line, system dialogs, buttons) and the
#   default: KOCP949.SVF, NanumGothic-Bold 16 px, 2 bpp, ASCII + cp949.
# - body (dialogue, narration, message boxes, titles): KOBATANG.SVF, Gowun
#   Batang Bold 17 px, 2 bpp, ASCII + the 11172 syllables + KS X 1001 symbols.
# Both in a 16x16 cell on one baseline (--clip-cell): ink a glyph has above
# or below the 16-row SCI line is cut rather than left for the game to miss
# when it erases the line (README.md). Neither file carries a font name, so
# NanumGothic's Reserved Font Names are not used.
# NANUMGOTHIC_BOLD / GOWUNBATANG_BOLD=/path/to/font.ttf bake other copies.
nanum="${NANUMGOTHIC_BOLD:-$here/../../dists/engine-data/hires_text/fonts/nanumgothic/NanumGothic-Bold.ttf}"
gowun="${GOWUNBATANG_BOLD:-$here/../../dists/engine-data/hires_text/fonts/gowunbatang/GowunBatang-Bold.ttf}"
for f in "$nanum" "$gowun"; do
	if [ ! -f "$f" ]; then
		echo "bake-dos-fonts: no font at $f" >&2
		exit 1
	fi
done
python3 "$here/mkfont.py" "$nanum" "$out/KOCP949.SVF" --size 16 --cell 16 --clip-cell --ascent 14 \
	--bpp 2 --unicode ascii,cp949
python3 "$here/mkfont.py" "$gowun" "$out/KOBATANG.SVF" --size 17 --cell 16 --clip-cell --ascent 15 \
	--bpp 2 --unicode ascii,cp949-hangul,ksx1001-symbols
