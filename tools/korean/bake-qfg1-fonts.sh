#!/bin/bash
# Bake the Quest for Glory I VGA (SCI1.1) Korean fonts, one face and size per
# font id (QFG1KO.MAP). Usage: bake-qfg1-fonts.sh TEXT.TXT SCI-KO.STR
#   TEXT.TXT   sci11_kr_extract.py --text-dump output (the message strings)
#   SCI-KO.STR the heap strings it wrote
# Only the characters those two files use (and ASCII) are baked, limited to
# KS X 1001 without Hanja.
#
# QFG1's fonts, game height (px) x 2 = the hi-res line, and the face chosen by
# height and decoration. The cell is the line less 2 px of leading:
#   999   8  plain, tiny           line 16  Q1G14  NanumGothic-Bold  14
#   0,3,4 9  plain (UI, dialogue)  line 18  Q1G16  NanumGothic-Bold  16
#   2107 10  calligraphic          line 20  Q1B18  GowunBatang-Bold  18
#   1    12  plain, larger         line 24  Q1G22  NanumGothic-Bold  22
#   300  12  calligraphic body     line 24  Q1B22  GowunBatang-Bold  22
#   123  15  ornate display        line 30  Q1T28  BlackHanSans 28 + Q1L28 Coustard-Black Latin
set -e
here="$(cd "$(dirname "$0")" && pwd)"
out="$here/../../dists/engine-data/hires_text/dos"
fonts="$here/../../dists/engine-data/hires_text/fonts"
text="$1"; str="$2"
[ -f "$text" ] && [ -f "$str" ] || { echo "usage: $0 TEXT.TXT SCI-KO.STR" >&2; exit 1; }
nanum="$fonts/nanumgothic/NanumGothic-Bold.ttf"
gowun="$fonts/gowunbatang/GowunBatang-Bold.ttf"
black="$fonts/blackhansans/BlackHanSans-Regular.ttf"
coust="$fonts/coustard/Coustard-Black.ttf"
bake() { # ttf out size cell ascent [unicode-limit]
	python3 "$here/mkfont.py" "$1" "$out/$2" --size "$3" --cell "$4" --clip-cell --ascent "$5" --bpp 2 \
		--chars-from "$text" "$str" --limit "${6:-ascii,ksx1001-nohanja}"
}
bake "$nanum" Q1G14.SVF 14 14 12
bake "$nanum" Q1G16.SVF 16 16 14
bake "$gowun" Q1B18.SVF 18 18 16
bake "$nanum" Q1G22.SVF 22 22 19
bake "$gowun" Q1B22.SVF 21 22 19
bake "$black" Q1T28.SVF 28 28 23
# Latin for the title face: the Hangul face has none worth keeping, Coustard has no Hangul
bake "$coust" Q1L28.SVF 24 28 23 ascii
