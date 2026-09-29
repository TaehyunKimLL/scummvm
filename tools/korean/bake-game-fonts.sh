#!/bin/bash
# Bake a per-game ("sparse") subset Korean font pair: only the code points a
# game's own UTF-8 translation actually uses, further limited to what its
# preset's shared font (bake-dos-fonts.sh's KO2350.SVF / KOCP949.SVF) could
# draw in the first place. See tools/korean/README.md, "Only what a game
# uses: --chars-from, --limit".
#
#   tools/korean/bake-game-fonts.sh KQ1KO  /path/to/KQ1KO/game/dir   /out/dir
#   tools/korean/bake-game-fonts.sh LB1KO  /path/to/lb1utf           /out/dir
#
# Reads every TEXT.*/text.* (SCI TEXT resource patch: 2-byte header, NUL-
# separated UTF-8 strings - a fan translation's replacement for the game's
# own TEXT resources) and SCI-KO.STR/sci-ko.str (script-string manifest,
# engines/sci/engine/translation.h) found directly in GAMEDIR, plus any extra
# files given after OUTDIR (e.g. the game's hires_text.map, or the dists
# per-game .MAP that names the shared fonts, for its declared missing= box).
#
# Writes <NAME>L.SVF (KO2350 settings: neodgm, 16px, 1bpp, KS X 1001 without
# Hanja) and <NAME>U.SVF (KOCP949 settings: NanumGothic-Bold, 18px, 2bpp,
# cp949) to OUTDIR. NAME becomes the file's 8.3 stem - keep it to 5
# characters (KQ1KO -> KQ1KOL.SVF/KQ1KOU.SVF fits at 6).
#
# A syllable the game uses but KS X 1001 does not hold (outside the L
# preset's 2350) cannot go in the L subset either; mkfont.py's --limit
# reports these as dropped (they would show as the missing= box, □, in
# KO2350 too - this is not a regression from the shared font). --require
# still runs on both bakes: it fails if a *kept* code point (already inside
# the preset's character set) somehow has no glyph in the source TTF, which
# would be a real gap rather than an expected drop.
set -e
here="$(cd "$(dirname "$0")" && pwd)"

name="$1"
gamedir="$2"
out="$3"
shift 3 || true

if [ -z "$name" ] || [ -z "$gamedir" ] || [ -z "$out" ]; then
	echo "usage: bake-game-fonts.sh NAME GAMEDIR OUTDIR [EXTRA-CHARS-FROM-FILE ...]" >&2
	exit 1
fi
mkdir -p "$out"

# L preset source: neodgm, as bake-dos-fonts.sh's KO2350.SVF.
# NEODGM=/path/to/neodgm.ttf bakes another copy.
neodgm="${NEODGM:-$here/../../dists/engine-data/hires_text/fonts/neodgm/neodgm.ttf}"
# U preset source: NanumGothic-Bold, as bake-dos-fonts.sh's KOCP949.SVF.
# NANUMGOTHIC_BOLD=/path/to/NanumGothic-Bold.ttf bakes another copy.
nanum="${NANUMGOTHIC_BOLD:-$here/../../dists/engine-data/hires_text/fonts/nanumgothic/NanumGothic-Bold.ttf}"
for f in "$neodgm" "$nanum"; do
	if [ ! -f "$f" ]; then
		echo "bake-game-fonts: no font at $f" >&2
		exit 1
	fi
done

# nocaseglob so one glob finds both the KQ1KO-style TEXT.NNN and the
# lb1utf-style text.nnn; nullglob so a pattern that matches nothing
# disappears instead of being passed through literally. SCI-KO.STR has no
# glob metacharacter of its own (nocaseglob only affects an actual glob), so
# its case is tried explicitly instead.
shopt -s nullglob nocaseglob
srcs=("$gamedir"/TEXT.*)
shopt -u nullglob nocaseglob
for cand in "$gamedir/SCI-KO.STR" "$gamedir/sci-ko.str"; do
	if [ -f "$cand" ]; then
		srcs+=("$cand")
		break
	fi
done
srcs+=("$@")

if [ ${#srcs[@]} -eq 0 ]; then
	echo "bake-game-fonts: no TEXT.*/SCI-KO.STR found in $gamedir" >&2
	exit 1
fi

echo "== $name: sources =="
printf '  %s\n' "${srcs[@]}"

echo "== $name L (KO2350 settings: neodgm 16px 1bpp, KS X 1001 without Hanja) =="
python3 "$here/mkfont.py" "$neodgm" "$out/${name}L.SVF" --size 16 --cell 16 --bpp 1 \
	--chars-from "${srcs[@]}" --limit ascii,ksx1001-nohanja --require

echo "== $name U (KOCP949 settings: NanumGothic-Bold 18px 2bpp, cp949) =="
python3 "$here/mkfont.py" "$nanum" "$out/${name}U.SVF" --size 18 --bpp 2 \
	--chars-from "${srcs[@]}" --limit ascii,cp949 --require
