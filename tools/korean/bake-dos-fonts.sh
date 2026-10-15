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
# as the pre-existing NEODG16.SVF and cp949 bakes, which never
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
python3 "$here/mkfont.py" "$src" "$out/NEODG16.SVF" --size 16 --cell 16 --bpp 1 \
	--unicode "ascii,ksx1001-nohanja,$EXTRA_REQUIRE"

# U preset, two shared faces (the U maps pick one per SCI font id), both in a
# 16x16 cell on one baseline (--clip-cell): ink a glyph has above or below the
# 16-row SCI line is cut rather than left for the game to miss when it erases
# the line (README.md). Neither file carries a font name, so NanumGothic's
# Reserved Font Names are not used.
# - NGOTB16.SVF: UI (status/menu bar, parser line, system dialogs, buttons) and
#   the default; NanumGothic-Bold 16 px, 2 bpp.
# - GBATB17.SVF: body (dialogue, narration, message boxes, titles); Gowun Batang
#   Bold 17 px, 2 bpp.
# Both are ascii+ksx1001-nohanja (2350 Hangul + EXTRA_REQUIRE, and every KS X
# 1001 symbol/jamo, _ksx1001_symbols() in mkfont.py). Hanja is left out on
# purpose (as NEODG16.SVF does): it roughly doubles KS X 1001's glyph count for
# characters no SCI DOS translation here uses, and none of neodgm/NanumGothic-
# Bold/Gowun Batang Bold carry the handful of Hanja the coverage gate found
# garbled in LB2's own resource (see common2350-report.md) - those draw
# missing= (U+25A1). The full cp949 / 11172-syllable bakes of the same faces
# (once KOCP949.SVF/KOBATANG.SVF, later NGOTB16A/GBATB17A) were named by no map
# and were removed (2026-10-15); `mkfont.py ... --unicode ascii,cp949` (Nanum)
# or `ascii,cp949-hangul,ksx1001-symbols` (Gowun) with the options below makes
# them again.
# NANUMGOTHIC_BOLD / GOWUNBATANG_BOLD=/path/to/font.ttf bake other copies.
nanum="${NANUMGOTHIC_BOLD:-$here/../../dists/engine-data/hires_text/fonts/nanumgothic/NanumGothic-Bold.ttf}"
gowun="${GOWUNBATANG_BOLD:-$here/../../dists/engine-data/hires_text/fonts/gowunbatang/GowunBatang-Bold.ttf}"
for f in "$nanum" "$gowun"; do
	if [ ! -f "$f" ]; then
		echo "bake-dos-fonts: no font at $f" >&2
		exit 1
	fi
done
python3 "$here/mkfont.py" "$nanum" "$out/NGOTB16.SVF" --size 16 --cell 16 --clip-cell --ascent 14 \
	--bpp 2 --unicode "ascii,ksx1001-nohanja,$EXTRA_REQUIRE"
python3 "$here/mkfont.py" "$gowun" "$out/GBATB17.SVF" --size 17 --cell 16 --clip-cell --ascent 15 \
	--bpp 2 --unicode "ascii,ksx1001-nohanja,$EXTRA_REQUIRE"
