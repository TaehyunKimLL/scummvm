#!/bin/bash
# coverrun.sh — drive a target through enough situations to exercise its fonts.
#
# A target can load eight replacement fonts and draw with two. Loading proves
# nothing; the untested six may first appear in a scene nobody captured. MI2's
# verb line went the whole project drawn by the original bitmap font because
# nothing checked which fonts were actually used.
#
# This runs the same target several ways - from a save, from the start, and
# through the menu - and merges the coverage so one report covers the lot.
#
#   coverrun.sh <target> [display]
#   BIN=/tmp/svm-cover coverrun.sh mi2-svfn-hr
#
# Needs the FONTUSE probe in ScummHiResText::drawChar().
set -u

. "$(dirname "$0")/xvfb.sh"
export PATH=$HOME/.local/sysroot/usr/bin:$PATH
export LD_LIBRARY_PATH=$HOME/.local/sysroot/usr/lib/x86_64-linux-gnu
export SDL_AUDIODRIVER=dummy

find_window() {
	local disp=$1
	DISPLAY=$disp xwininfo -root -children 2>/dev/null | \
		awk '/^     0x/ {print $1; exit}'
}

TARGET=${1:?usage: coverrun.sh <target> [display]}
DISP=${2:-}
BIN=${BIN:-$HOME/src/scummvm/scummvm}
SAVES=$HOME/.local/share/scummvm/saves
OUT=/tmp/cover_$TARGET

rm -rf "$OUT"
mkdir -p "$OUT"

mkini() {
	local out=$1
	cp "$HOME/.config/scummvm/scummvm.ini" "$out"
	python3 - "$out" "$HOME/src/scummvm/dists/engine-data" "$TARGET" <<'PY'
import re
import sys
ini, engdata, target = sys.argv[1], sys.argv[2], sys.argv[3]
s = open(ini).read()
if '[scummvm]' in s:
    s = s.replace('[scummvm]', '[scummvm]' + chr(10) + 'extrapath=' + engdata, 1)
else:
    s = '[scummvm]' + chr(10) + 'extrapath=' + engdata + chr(10) * 2 + s
# The ScummVM menu draws with the engine's charsets too, and the original GUI
# would bypass that path entirely.
s = re.sub(r'\[' + re.escape(target) + r'\]',
           '[' + target + ']' + chr(10) + 'original_gui=false', s)
open(ini, 'w').write(s)
PY
}

# One pass: start the game a given way, poke at it, keep the log.
pass_run() {
	local name=$1
	local secs=$2
	shift 2

	local d
	d=$(mktemp -d)
	mkini "$d/t.ini"

	local xv
	if [ -n "$DISP" ]; then
		Xvfb "$DISP" -screen 0 1600x1000x24 -nolisten tcp >/dev/null 2>&1 &
		xv=$!
		sleep 2
	else
		# Without this the script ran with DISPLAY= empty: the game exited at
		# once, find_window returned nothing, the poke loop was skipped, and
		# the report said the target draws with almost no fonts - which is
		# exactly the finding this script exists to produce.
		xvfb_start 1600 1000 || return 1
		xv=$XVFB_PID
	fi

	# shellcheck disable=SC2086
	DISPLAY=$DISP timeout $((secs + 15)) "$BIN" --config="$d/t.ini" -d1 \
		--gfx-mode=opengl "$@" \
		--no-filtering --no-aspect-ratio --stretch-mode=pixel-perfect --scale-factor=1 \
		"$TARGET" > "$OUT/$name.log" 2>&1 &
	local game=$!

	sleep 8
	local id
	id=$(find_window "$DISP")
	if [ -n "$id" ]; then
		DISPLAY=$DISP xdotool windowfocus --sync "$id" 2>/dev/null

		local t=0
		while [ "$t" -lt "$secs" ]; do
			# Sweep the pointer: hovering objects draws the sentence line,
			# hovering verbs highlights them, and both use their own charsets.
			DISPLAY=$DISP xdotool mousemove --window "$id" \
				$((40 + (t * 37) % 260)) $((60 + (t * 23) % 130)) 2>/dev/null
			sleep 1
			if [ $((t % 4)) -eq 3 ]; then
				DISPLAY=$DISP xdotool click 1 2>/dev/null
			fi
			if [ $((t % 7)) -eq 6 ]; then
				# The menu and the save dialog use different charsets again.
				DISPLAY=$DISP xdotool key --window "$id" F5 2>/dev/null
				sleep 2
				DISPLAY=$DISP xdotool key --window "$id" Escape 2>/dev/null
			fi
			t=$((t + 1))
		done
	fi

	kill $game 2>/dev/null
	wait $game 2>/dev/null
	kill $xv 2>/dev/null
	rm -rf "$d"
	sleep 1
}

echo "=== $TARGET"

# One pass per save, because saves of different sizes are different scenes and
# a scene is what decides which charsets appear. Running only the largest was
# how four of MI2's nine fonts stayed untested.
n=0
seen_sizes=""
for save in $(ls -S "$SAVES/$TARGET".s?? 2>/dev/null); do
	size=$(stat -c%s "$save")
	# Same size, same scene - no new coverage to be had.
	case " $seen_sizes " in *" $size "*) continue ;; esac
	seen_sizes="$seen_sizes $size"

	slot=$(basename "$save" | sed 's/.*\.s//' | sed 's/^0*\([0-9]\)/\1/')
	n=$((n + 1))
	echo "  pass $n: save slot $slot ($size bytes)"
	pass_run "save$slot" 25 --save-slot="$slot"

	[ "$n" -ge 4 ] && break
done
[ "$n" -eq 0 ] && echo "  no saves for this target"

# From the start: the intro, titles and selection screens a save skips past.
echo "  pass $((n + 1)): from the start"
pass_run intro 40

echo
python3 "$HOME/games/fontcover.py" "$OUT"/*.log
