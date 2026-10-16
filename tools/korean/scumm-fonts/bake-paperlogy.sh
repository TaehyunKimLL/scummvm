#!/bin/bash
# usage: bake-paperlogy.sh OUTDIR [PAPERLOGY_TTF]
# The Latin companion PAPLG16.SVF of the Paperlogy jamo set PAPLG18.SVF (dists/engine-data/hires_text/dos/PAPLG.TXT).
# PAPLG18.SVF itself is not baked here: the Hangul pieces come from the vector separation of Paperlogy
# (runs/seg-night/F, fontbuild2.py paper 18 --sets 24,16,16 --classes 12,12 --weights syllable-freq.tsv; its json, with
# the lead/tail classes, is paplg-classes.json) and the built face is stored in the engine data like the other jamo sets.
# The cell is 18x18 so that the chain's first face (PAPLG18, 18 rows) and this one have the same height; the
# 16 px size and ascent 14 keep descenders (gjpqy, brackets) inside the 18 rows once the engine puts the baseline
# on PAPLG18's (row 17); the space is 6 px, . , : ; ! ? ' and the ellipsis one px wider.
set -e
FONTS="${FONTS:-$HOME/scummvm-i18n/fonts}"
here="$(cd "$(dirname "$0")" && pwd)"
out="${1:?usage: bake-paperlogy.sh OUTDIR [PAPERLOGY_TTF]}"
ttf="${2:-$FONTS/paperlogy/Paperlogy-4Regular.ttf}"
mkdir -p "$out"
python3 "$here/../mkfont.py" "$ttf" "$out/PAPLG16.SVF" --size 16 --cell 18 --width 18 --bpp 2 --clip-cell --ascent 14 \
	--unicode ascii,2026,2122,3131-318e --only-listed --space-advance 6 --punct-extra 1
