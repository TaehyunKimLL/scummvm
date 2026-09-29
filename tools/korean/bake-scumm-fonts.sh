#!/bin/bash
# Bake a SCUMM game's per-charset hi-res Korean fonts from a plan table.
#
#   tools/korean/bake-scumm-fonts.sh PLAN.TSV GAMEDIR OUTDIR CHARS
#
# PLAN.TSV: one line per output font, tab-separated -
#
#   <output 8.3 name>  <charset>  <ttf path>  <size>  <bpp>  <ascent or ->
#
# <charset> is the SCUMM charset number (engines/scumm/charset.cpp's
# loadKorFont()/loadCJKCells(): korean%02d.fnt, 0..19 - MI1/MI2 use 0..8).
# The cell this script bakes at is GAMEDIR/korean<charset>.fnt's own header
# width and height (bytes 2 and 3: skip byte 0, byte 1 is the shadow flag -
# engines/scumm/charset.cpp's loadKorFont()), doubled: the shipped hi-res
# fonts (hr00.fnt etc.) are baked at exactly 2x the low-res bitmap font's
# cell (verified against gamedata/mi2kor: korean00.fnt header 11x12, hr00.fnt
# cell 22x24). <ascent> is passed to mkfont.py's --ascent verbatim, or
# omitted for `-` (mkfont.py picks one from the face and --clip-cell).
#
# CHARS is a single file for --chars-from: a .trs bundle (mkfont.py's
# chars_from_trs()) or scummtext.py's output. Every line bakes with
# `--unicode ascii --chars-from CHARS --limit ascii,ksx1001-nohanja
# --require`: only ASCII plus what the game's own Korean text uses, limited
# to the 2350 KS X 1001 syllables (a syllable outside that set falls back to
# missing=(u+25a1) same as it would in the shared L-preset font - see
# tools/korean/SCUMM_FONTS.md), and --require so a syllable the collected
# text needs but the chosen TTF lacks fails the bake instead of shipping a
# silent gap.
#
# A (ttf, size, bpp, cell) already baked earlier in the same run is copied,
# not re-baked - MI1/MI2's L and U presets often reuse the very same face at
# the same size for several charsets (e.g. every UI charset at 16x16).
set -e
here="$(cd "$(dirname "$0")" && pwd)"

plan="$1"
gamedir="$2"
outdir="$3"
chars="$4"

if [ -z "$plan" ] || [ -z "$gamedir" ] || [ -z "$outdir" ] || [ -z "$chars" ]; then
	echo "usage: bake-scumm-fonts.sh PLAN.TSV GAMEDIR OUTDIR CHARS" >&2
	exit 1
fi
if [ ! -f "$plan" ]; then
	echo "bake-scumm-fonts: no plan at $plan" >&2
	exit 1
fi
if [ ! -f "$chars" ]; then
	echo "bake-scumm-fonts: no chars-from file at $chars" >&2
	exit 1
fi
mkdir -p "$outdir"

declare -A baked   # "ttf|size|bpp|cellw|cellh" -> already-baked output path

n=0
while IFS=$'\t' read -r name charset ttf size bpp ascent || [ -n "$name" ]; do
	n=$((n + 1))
	# blank lines and '#' comments (leading whitespace stripped) are skipped.
	trimmed="${name#"${name%%[![:space:]]*}"}"
	if [ -z "$trimmed" ] || [ "${trimmed:0:1}" = "#" ]; then
		continue
	fi
	if [ -z "$name" ] || [ -z "$charset" ] || [ -z "$ttf" ] || [ -z "$size" ] || [ -z "$bpp" ]; then
		echo "bake-scumm-fonts: $plan line $n: expected 6 tab-separated fields" >&2
		exit 1
	fi

	korfont=$(printf '%s/korean%02d.fnt' "$gamedir" "$charset")
	if [ ! -f "$korfont" ]; then
		echo "bake-scumm-fonts: $plan line $n: no $korfont (charset $charset)" >&2
		exit 1
	fi
	read -r w h < <(od -An -tu1 -j2 -N2 "$korfont")
	cell_w=$((w * 2))
	cell_h=$((h * 2))

	out="$outdir/$name"
	key="$ttf|$size|$bpp|$cell_w|$cell_h"
	if [ -n "${baked[$key]}" ]; then
		echo "== $name: same as ${baked[$key]} ($key) - copying =="
		cp "${baked[$key]}" "$out"
		continue
	fi

	ascent_args=()
	if [ -n "$ascent" ] && [ "$ascent" != "-" ]; then
		ascent_args=(--ascent "$ascent")
	fi

	echo "== $name: charset $charset ($korfont: ${w}x${h} -> cell ${cell_w}x${cell_h}), $ttf ${size}px ${bpp}bpp =="
	python3 "$here/mkfont.py" "$ttf" "$out" --size "$size" --cell "$cell_h" --width "$cell_w" \
		--bpp "$bpp" --clip-cell "${ascent_args[@]}" --unicode ascii \
		--chars-from "$chars" --limit ascii,ksx1001-nohanja --require

	baked[$key]="$out"
done < "$plan"
