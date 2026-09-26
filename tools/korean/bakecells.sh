#!/bin/bash
# bakecells.sh — bake the font cells a game actually needs.
#
# The cell size is not a preference. The game lays text out on its own font's
# grid: Indy3 reports _2byteWidth = 8 for a Hangul syllable, so each character
# gets 8 game pixels and 8 * scale on screen. A wider replacement is overlapped
# by the next character, and a narrower one leaves gaps.
#
# So the needed cell is height * scale, once per distinct height in the game's
# own korean*.fnt set. ~/games/fontplan.py lists them; this bakes them.
#
#   bakecells.sh <game-folder> <scale> [face.ttf] [latin.ttf]
#   bakecells.sh ~/games/indy3kor 2
#
# Output goes into the game folder as hr<cell>_<index>.fnt plus a Latin
# companion, and makemaps.py will pick them up.
set -u

. "$(dirname "$0")/xvfb.sh"

ERRLOG=$(mktemp)
cleanup_file "$ERRLOG"
FAILED=0

MKFONT=$HOME/src/scummvm-korean-ttf/scripts/mkfont.py
FONTDIR=$HOME/.local/sysroot/usr/share/fonts/truetype/nanum

FOLDER=${1:?usage: bakecells.sh <game-folder> <scale> [face.ttf] [latin.ttf]}
FOLDER_ARG=$FOLDER
SCALE=${2:?scale (2 or 3)}
FACE=${3:-$FONTDIR/NanumGothic.ttf}

# The Latin companion defaults to the Hangul face, whose Latin is half-width
# with padding: at a 16px cell the ink comes out around 8px and the line looks
# thin and gappy. That is right for v3+, which lays Latin out proportionally.
#
# v0-v2 are different. CharsetRendererV2::getCharWidth() returns a hard-coded
# 8, so every character - Latin included - gets one 8px cell, and only a truly
# full-width design fills it. unifont_jp has one; Korean faces do not. Render
# it at twice the cell so its 16-unit advance lands on the 16px grid.
FULLWIDTH=/usr/share/fonts/opentype/unifont/unifont_jp.otf
# Detect it rather than asking: a v0-v2 game ships exactly one korean00.fnt
# with an 8x8 cell, because that is the only grid its renderer has.
V0V2=0
if [ -f "$FOLDER_ARG/korean00.fnt" ] && [ ! -f "$FOLDER_ARG/korean01.fnt" ]; then
	read -r kw kh < <(python3 -c "
import sys
d = open(sys.argv[1], 'rb').read(4)
print(d[2], d[3])
" "$FOLDER_ARG/korean00.fnt" 2>/dev/null)
	[ "${kw:-0}" = "8" ] && [ "${kh:-0}" = "8" ] && V0V2=1
fi

LATIN=${4:-}
if [ -z "$LATIN" ]; then
	if [ "$V0V2" = "1" ] && [ -f "$FULLWIDTH" ]; then
		LATIN=$FULLWIDTH
	else
		LATIN=$FACE
	fi
fi

FOLDER=$(cd "$FOLDER" && pwd)

[ -f "$MKFONT" ] || { echo "mkfont.py not found at $MKFONT"; exit 1; }
[ -f "$FACE" ]   || { echo "face not found: $FACE"; exit 1; }

# The game's own fonts define the grid. Read their heights straight from the
# headers: byte 0 is the format version (2), byte 2 is the height.
mapfile -t PLAN < <(python3 - "$FOLDER" "$SCALE" <<'PY'
import os
import sys

folder, scale = sys.argv[1], int(sys.argv[2])
seen = {}
for name in sorted(os.listdir(folder)):
    if not name.startswith("korean") or not name.endswith(".fnt"):
        continue
    with open(os.path.join(folder, name), "rb") as fh:
        head = fh.read(4)
    # head[3] is read below, so three bytes are not enough - a short file
    # raised IndexError, mapfile saw an empty PLAN, and the script reported
    # "no korean*.fnt in this folder" for a folder full of them.
    if len(head) < 4 or head[:4] == b"SVFN" or head[0] != 2:
        continue
    # korean<NN>.fnt is charset NN and the engine picks the font by charset,
    # so the index has to survive into the output name. The Dig instead names
    # them korean.fnt and korean_g.fnt, with no index at all - give those a
    # suffix of their own so one does not overwrite the other.
    idx = "".join(c for c in name if c.isdigit())
    if not idx:
        stem = os.path.splitext(name)[0]
        idx = stem[len("korean"):].lstrip("_") or "0"
    # Header is [version=2, shadow, width, height]; loadKorFont() reads them in
    # that order. The width becomes _2byteWidth and sets the advance, the
    # height sets the line box - and they are not always equal (MI2's korean00
    # is 11x12, The Dig's korean.fnt is 10x9), so a square replacement cell
    # would either clip the glyph or overrun the line.
    seen[idx] = (head[2] * scale, head[3] * scale)
for idx, (w, h) in sorted(seen.items()):
    print(f"{idx} {w} {h}")
PY
)

if [ ${#PLAN[@]} -eq 0 ]; then
	echo "no korean*.fnt in $FOLDER - nothing to bake against"
	exit 1
fi

# Charset 6 has no korean06.fnt in MI1 CD, MI2 or DOTT, and upstream works
# around that by remapping it to font 0 (charset.cpp, "HACK: Fix
# monkey1cd/monkey2/dott font error"). The remap decides the grid, so bake a
# charset-6 replacement at font 0's size rather than at charset 6's nominal
# one - it asks for a 14px font and is handed an 11x12 grid.
have6=""
for entry in "${PLAN[@]}"; do
	case "$entry" in 06\ *|6\ *) have6=1 ;; esac
done
if [ -z "$have6" ]; then
	for entry in "${PLAN[@]}"; do
		case "$entry" in
		00\ *|0\ *)
			read -r _ w0 h0 <<<"$entry"
			PLAN+=("06 $w0 $h0")
			echo "  (charset 6 has no font of its own; baking one at font 0's"
			echo "   ${w0}x${h0} grid, which is what the engine remaps it to)"
			;;
		esac
	done
fi

echo "baking for $(basename "$FOLDER") at scale $SCALE"
echo "  face:  $(basename "$FACE")"

cells=""
for entry in "${PLAN[@]}"; do
	read -r idx cellw cellh <<<"$entry"

	out="$FOLDER/hr${idx}.fnt"
	# Render at the cell size and let mkfont fit the ink inside it. 8bpp gives
	# the coverage the alpha path blends; 1bpp would look like the original.
	#
	# --ink-advance, not the face's own metrics. A CJK face reports a single
	# advance for every syllable because they are designed on a square em -
	# NanumGothic says 15.05 for 가, 이 and 무 alike - while their ink is 14,
	# 12 and 15 wide. Baking those advances gives a font that is variable in
	# name only and one pixel too narrow everywhere, which makes neighbouring
	# syllables touch. Measuring the ink recovers the real variation: 14/15/16
	# at a 16px cell, 30..35 at 36px.
	# Remove first and check the exit status: the output name is fixed, so a
	# font left by an earlier run satisfied `[ -f ]` and the script announced
	# the cell it INTENDED while the old, wrong-sized file stayed on disk.
	rm -f "$out"
	if python3 "$MKFONT" "$FACE" "$out" \
		--size "$cellh" --cell "$cellh" --width "$cellw" \
		--bpp 8 --codepage 949 \
		--variable --ink-advance > "$ERRLOG" 2>&1 && [ -s "$out" ]; then
		printf '  %-14s cell %2dx%-2d\n' "$(basename "$out")" "$cellw" "$cellh"
	else
		echo "  FAILED for index $idx (cell ${cellw}x${cellh})"
		sed 's/^/      /' "$ERRLOG" | tail -3
		FAILED=$((FAILED + 1))
	fi
done

# A Latin companion per charset, named by index so the map can use one pattern.
# A game can use a different cell per charset - MI2 has five - and a Latin face
# at the wrong cell sits on a different baseline from the Hangul beside it.
for entry in "${PLAN[@]}"; do
	read -r idx cellw cellh <<<"$entry"

	out="$FOLDER/hrlat${idx}.fnt"
	# Latin faces do report per-glyph advances, so use them directly. The cell
	# only bounds them; the advance comes from the metrics table.
	rm -f "$out"
	# A full-width face has to be rendered at twice the cell for its advance to
	# match the grid: unifont_jp at 16px gives 8px ink (half-width), at 32px it
	# gives 16px ink with a fixed 16 advance.
	latin_size=$cellh
	[ "$LATIN" = "$FULLWIDTH" ] && latin_size=$((cellh * 2))

	if python3 "$MKFONT" "$LATIN" "$out" \
		--size "$latin_size" --cell "$cellh" --width "$cellw" \
		--bpp 8 --latin --variable > "$ERRLOG" 2>&1 && [ -s "$out" ]; then
		printf '  %-14s cell %2dx%-2d (Latin)\n' \
			"$(basename "$out")" "$cellw" "$cellh"
	else
		# Silence here meant a Latin companion that never got baked was not
		# even mentioned.
		echo "  FAILED hrlat${idx}.fnt (cell ${cellw}x${cellh})"
		sed 's/^/      /' "$ERRLOG" | tail -3
		FAILED=$((FAILED + 1))
	fi
done

echo
if [ "$FAILED" -gt 0 ]; then
	echo "*** $FAILED font(s) failed to bake - the map would point at gaps ***"
	echo "    fix those before regenerating the map"
	exit 1
fi
echo "now regenerate the map:  python3 ~/games/makemaps.py $FOLDER"
