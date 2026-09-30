#!/bin/bash
# Bake the DOS L-preset and U-preset Korean fonts.
#
# common2350-brief (2026-09-29): every SCI DOS package (KQ1, LB1, Conquests
# of Camelot, LB2) now uses these SHARED fonts from
# dists/engine-data/hires_text/dos instead of a per-game subset bake
# (bake-game-fonts.sh is still used for the per-game .MAP's own missing=
# check and for other, non-SCI packages). The coverage gate
# (common2350-report.md) read every character each shipped SCI translation
# can display - KQ1/LB1/Camelot's UTF-8 TEXT.*/SCI-KO.STR/RESOURCE.MSG and
# LB2's native CP949 RESOURCE.MSG - against ascii+ksx1001-nohanja (2350
# Hangul, no Hanja): only LB2 needs code points outside it, 7 KS X 1001
# extension syllables (outside the 2350, inside the 11172-syllable cp949
# range) in $EXTRA_REQUIRE below, confirmed present in neodgm, NanumGothic-Bold
# and Gowun Batang Bold (fontTools cmap check, common2350-report.md) and
# folded into all three fonts' --unicode list. No --require here: it checks
# every code point in --unicode, not just EXTRA_REQUIRE, and neodgm (a small
# pixel font) and even NanumGothic-Bold/Gowun Batang Bold lack glyphs for
# plenty of ksx1001-nohanja's symbol-table code points (Greek, Cyrillic, kana,
# roman numerals, ...) that this bake has always silently dropped - the same
# as the pre-existing KO2350.SVF/KOCP949/KOBATANG bakes below, which never
# used --require either. No game needs Hanja or anything else outside
# ascii+ksx1001-nohanja+EXTRA_REQUIRE.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
out="$here/../../dists/engine-data/hires_text/dos"
mkdir -p "$out"

# The 7 code points the coverage gate found outside KS X 1001's 2350 Hangul
# (all in LB2's RESOURCE.MSG; union across KQ1/LB1/Camelot/LB2 is these 7 -
# small enough (<=~100) to fold in rather than BLOCK, common2350-brief step 2).
EXTRA_REQUIRE="AC37,B2CF,B584,C125,C2BA,D14A,D59D"

# L preset: neodgm 16 px, 1 bpp, KS X 1001 without Hanja (+ EXTRA_REQUIRE).
# The in-tree neodgm; NEODGM=/path/to/neodgm.ttf bakes another copy.
src="${NEODGM:-$here/../../dists/engine-data/hires_text/fonts/neodgm/neodgm.ttf}"
if [ ! -f "$src" ]; then
	echo "bake-dos-fonts: no font at $src" >&2
	exit 1
fi
python3 "$here/mkfont.py" "$src" "$out/KO2350.SVF" --size 16 --cell 16 --bpp 1 \
	--unicode "ascii,ksx1001-nohanja,$EXTRA_REQUIRE"

# U preset, two faces (the U maps pick one per SCI font id):
# - UI (status/menu bar, parser line, system dialogs, buttons) and the
#   default: KOCP949.SVF, NanumGothic-Bold 16 px, 2 bpp, ASCII + cp949.
# - body (dialogue, narration, message boxes, titles): KOBATANG.SVF, Gowun
#   Batang Bold 17 px, 2 bpp, ASCII + the 11172 syllables + KS X 1001 symbols.
# Both in a 16x16 cell on one baseline (--clip-cell): ink a glyph has above
# or below the 16-row SCI line is cut rather than left for the game to miss
# when it erases the line (README.md). Neither file carries a font name, so
# NanumGothic's Reserved Font Names are not used.
#
# KO2350G.SVF / KO2350B.SVF: the new shared U-preset pair, same faces, sizes,
# cell, ascent, clip and bpp as KOCP949/KOBATANG, but ascii+ksx1001-nohanja
# (2350 Hangul + EXTRA_REQUIRE) instead of the full 11172-syllable cp949/
# cp949-hangul sets. ksx1001-nohanja already carries every KS X 1001 symbol/
# jamo KOBATANG had (_ksx1001_symbols() in mkfont.py), so this is also the
# union of what KOCP949/KOBATANG held outside Hangul, Hanja aside - Hanja is
# left out on purpose (as KO2350.SVF already does): it roughly doubles KS X
# 1001's glyph count for characters no SCI DOS translation here uses, and
# none of neodgm/NanumGothic-Bold/Gowun Batang Bold carry the handful of
# Hanja the coverage gate found garbled in LB2's own resource (see
# common2350-report.md) - those still draw missing= (U+25A1), same as before.
# Ruling: KOCP949.SVF/KOBATANG.SVF STAY in dists (non-DOS/UTF-8 consumers may
# still want full cp949 coverage); the DOS .MAPs just stop referencing them,
# in favour of KO2350G.SVF/KO2350B.SVF.
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

python3 "$here/mkfont.py" "$nanum" "$out/KO2350G.SVF" --size 16 --cell 16 --clip-cell --ascent 14 \
	--bpp 2 --unicode "ascii,ksx1001-nohanja,$EXTRA_REQUIRE"
python3 "$here/mkfont.py" "$gowun" "$out/KO2350B.SVF" --size 17 --cell 16 --clip-cell --ascent 15 \
	--bpp 2 --unicode "ascii,ksx1001-nohanja,$EXTRA_REQUIRE"
