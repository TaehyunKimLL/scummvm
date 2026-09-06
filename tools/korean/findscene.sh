#!/bin/bash
# findscene.sh - find which save slot of a target actually shows text.
#
#   findscene.sh <target> [seconds-per-slot]
#
# Most saves sit on a cutscene or an empty landscape and draw nothing, which
# reads as "the renderer is broken" when the game simply had nothing to say.
# Loom's slots 0-2 were like that; only slot 5 spoke. Ask the engine rather
# than guessing: hires_text_log prints one line per run of text drawn.

set -u

. "$(dirname "$0")/xvfb.sh"

TARGET=${1:?target}
SECS=${2:-25}
BIN=${BIN:-/tmp/svm-final}

export PATH=$HOME/.local/sysroot/usr/bin:$PATH
export LD_LIBRARY_PATH=$HOME/.local/sysroot/usr/lib/x86_64-linux-gnu
export SDL_AUDIODRIVER=dummy

SAVES=$HOME/.local/share/scummvm/saves

echo "=== $TARGET"
found=""
for f in "$SAVES/$TARGET".s[0-9][0-9]; do
	[ -e "$f" ] || continue
	slot=${f##*.s}
	slot=$((10#$slot))

	D=$(mktemp -d)
	cp "$HOME/.config/scummvm/scummvm.ini" "$D/t.ini"
	python3 "$(dirname "$0")/inifix.py" "$D/t.ini" "$TARGET" --log >/dev/null || {
		rm -rf "$D"; continue
	}

	xvfb_start 1280 800 >/dev/null || { rm -rf "$D"; continue; }

	DISPLAY=$DISP timeout $((SECS + 12)) "$BIN" --config="$D/t.ini" -d1 \
		--gfx-mode=opengl --save-slot="$slot" --no-filtering \
		--no-aspect-ratio --stretch-mode=pixel-perfect --scale-factor=1 \
		"$TARGET" > "$D/log.txt" 2>&1 &
	game=$!
	sleep 8

	wid=$(find_window "$DISP")
	if [ -n "$wid" ]; then
		# Nudge the game: some scenes only speak once something is clicked.
		t=0
		while [ "$t" -lt "$SECS" ]; do
			DISPLAY=$DISP xdotool mousemove --window "$wid" --sync \
				$(( 60 + (RANDOM % 500) )) $(( 130 + (RANDOM % 120) )) 2>/dev/null
			sleep 1
			DISPLAY=$DISP xdotool click 1 2>/dev/null
			sleep 2
			t=$((t + 3))
		done
	fi

	kill $game 2>/dev/null
	wait $game 2>/dev/null

	runs=$(grep -c "HRTEXT" "$D/log.txt" 2>/dev/null)
	runs=${runs:-0}
	size=$(stat -c%s "$f")
	if [ "$runs" -gt 0 ]; then
		sample=$(grep -oE '"[^"]*"' "$D/log.txt" | sort -u | head -3 | tr '\n' ' ')
		printf '  slot %-2d %8dB  %4d runs  %s\n' "$slot" "$size" "$runs" "$sample"
		found="$found $slot"
	else
		printf '  slot %-2d %8dB  no text\n' "$slot" "$size"
	fi

	xvfb_stop
	rm -rf "$D"
done

if [ -n "$found" ]; then
	echo "  -> use slot:$found"
else
	echo "  -> no save of this target reaches a talking scene"
fi
