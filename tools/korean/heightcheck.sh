#!/bin/bash
# heightcheck.sh - compare drawn text height between the original and hi-res.
#
#   heightcheck.sh <target> [band-y0 band-y1]
#
# The hi-res glyphs must land in the same line box the game laid out for its
# own font, so the measured heights should match within a pixel or two. A
# taller result means the cell exceeds the grid and neighbouring lines will
# collide; a much shorter one means the replacement is not being used.
#
# Expects /tmp/true_<target>.png (captured with NOHIRES=1) and
# /tmp/final_<target>.png next to each other.

set -u

T=${1:?target}
Y0=${2:-0}
Y1=${3:-60}

A=/tmp/true_$T.png
B=/tmp/final_$T.png

for f in "$A" "$B"; do
	[ -f "$f" ] || { echo "missing $f" >&2; exit 1; }
done

echo "=== $T  band y$Y0-$Y1"
echo "--- original (game's own fonts)"
python3 "$(dirname "$0")/subtitleink.py" "$A" "$Y0" "$Y1" | grep -E "line|no "
echo "--- patched (hi-res)"
python3 "$(dirname "$0")/subtitleink.py" "$B" "$Y0" "$Y1" | grep -E "line|no "
