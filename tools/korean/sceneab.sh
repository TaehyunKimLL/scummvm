#!/bin/bash
# sceneab.sh - capture the same scene with and without hi-res text.
#
#   sceneab.sh <target> <slot> [seconds] [outprefix]
#
# Both runs load the same save and click the same sequence (RANDOM seeded
# identically), so a frame at a given time is the same moment in the game.
# Find a slot that actually speaks with findscene.sh first.

set -u

. "$(dirname "$0")/xvfb.sh"

TARGET=${1:?target}
SLOT=${2:?slot}
SECS=${3:-30}
PREFIX=${4:-/tmp/sab_$TARGET}

BIN=${BIN:-/tmp/svm-final}
export PATH=$HOME/.local/sysroot/usr/bin:$PATH
export LD_LIBRARY_PATH=$HOME/.local/sysroot/usr/lib/x86_64-linux-gnu
export SDL_AUDIODRIVER=dummy

run_one() {
	local mode=$1        # on | off
	local out=$2
	local D
	D=$(mktemp -d)

	cp "$HOME/.config/scummvm/scummvm.ini" "$D/t.ini"

	local args="--log"
	if [ "$mode" = "off" ]; then
		args="--log --no-hires"
		# hires_text_scale=1 is not enough: the map in the game folder is
		# still found and our replacement fonts still load, so the capture
		# shows our glyphs at 1x rather than the game's own bitmaps.
		local gamepath
		gamepath=$(sed -n "/^\[$TARGET\]/,/^\[/p" "$HOME/.config/scummvm/scummvm.ini" \
			| sed -n 's/^path=//p' | head -1)
		if [ -n "$gamepath" ] && [ -d "$gamepath" ]; then
			mkdir -p "$D/plain"
			local f b
			for f in "$gamepath"/*; do
				b=$(basename "$f")
				case "$b" in
					hires_text.map|korean_ttf.map|km_*.map) continue ;;
					hr*.fnt|i4v*.fnt|uni24_*.fnt) continue ;;
				esac
				ln -sf "$f" "$D/plain/$b"
			done
			python3 - "$D/t.ini" "$TARGET" "$D/plain" <<'PY'
import sys
ini, target, newpath = sys.argv[1], sys.argv[2], sys.argv[3]
out, inside = [], False
for line in open(ini):
    if line.startswith('['):
        inside = line.strip() == '[' + target + ']'
    if inside and line.startswith('path='):
        line = 'path=' + newpath + '\n'
    out.append(line)
open(ini, 'w').writelines(out)
PY
		fi
	fi

	# shellcheck disable=SC2086
	python3 "$(dirname "$0")/inifix.py" "$D/t.ini" "$TARGET" $args >/dev/null || return 1

	xvfb_start 1280 800 >/dev/null || return 1

	DISPLAY=$DISP timeout $((SECS + 20)) "$BIN" --config="$D/t.ini" -d1 \
		--gfx-mode=opengl --save-slot="$SLOT" --no-filtering \
		--no-aspect-ratio --stretch-mode=pixel-perfect --scale-factor=1 \
		"$TARGET" > "$D/log.txt" 2>&1 &
	local game=$!
	sleep 8

	local wid
	wid=$(find_window "$DISP")
	[ -n "$wid" ] || { kill $game 2>/dev/null; xvfb_stop; return 1; }

	# Same walk in both runs.
	RANDOM=${SEED:-1337}

	local t=0
	while [ "$t" -lt "$SECS" ]; do
		DISPLAY=$DISP xdotool mousemove --window "$wid" --sync \
			$(( 60 + (RANDOM % 500) )) $(( 130 + (RANDOM % 120) )) 2>/dev/null
		sleep 1
		DISPLAY=$DISP xdotool click 1 2>/dev/null
		sleep 2
		t=$((t + 3))

		local raw=$D/f$t.xwd
		DISPLAY=$DISP xwd -id "$wid" -out "$raw" 2>/dev/null || continue
		python3 "$(dirname "$0")/xwd2png.py" "$raw" "${out}_t$t.png" >/dev/null 2>&1
	done

	kill $game 2>/dev/null
	wait $game 2>/dev/null
	cp "$D/log.txt" "${out}_log.txt"
	xvfb_stop
	rm -rf "$D"
}

echo "── control (hi-res off)"
run_one off "${PREFIX}_off"
echo "── patched (hi-res on)"
run_one on "${PREFIX}_on"

runs=$(grep -c HRTEXT "${PREFIX}_on_log.txt" 2>/dev/null)
echo
echo "text runs: patched=${runs:-0}  control=$(grep -c HRTEXT "${PREFIX}_off_log.txt" 2>/dev/null || echo 0)"
grep -oE '"[^"]{4,}"' "${PREFIX}_on_log.txt" 2>/dev/null | sort -u | head -5

echo
echo "pairs:"
for f in "${PREFIX}_on"_t*.png; do
	[ -e "$f" ] || continue
	tag=$(basename "$f" .png); tag=${tag##*_}
	other="${PREFIX}_off_$tag.png"
	[ -f "$other" ] || continue
	python3 "$(dirname "$0")/abstack.py" "${PREFIX}_AB_$tag.png" -- \
		"ORIGINAL (game's own fonts)" "$other" \
		"PATCHED (hi-res 2x)" "$f" >/dev/null 2>&1 \
		&& echo "  ${PREFIX}_AB_$tag.png"
done
