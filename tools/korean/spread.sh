#!/bin/bash
# spread.sh — collect saves from several points in a game, not one.
#
# Eleven MI2 saves existed and all eleven were the same scene, so four of its
# nine fonts had never been drawn with. Coverage needs saves that differ, and
# the cheapest way to get them is to keep playing and keep saving.
#
# Alt+<digit> is a quicksave and only fires when canSaveGameStateCurrently(),
# so the saves land wherever the game is willing - no dialog to drive, and a
# save that appears is proof the game was interactive at that moment.
#
#   spread.sh <target> [slots] [minutes] [display]
#   BIN=/tmp/svm-work spread.sh mi2-svfn-hr 4 6
#
# Writes slots 1..N, spaced through the run, then reports their sizes: saves
# that differ in size are usually different scenes.
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

TARGET=${1:?usage: spread.sh <target> [slots] [minutes] [display]}
SLOTS=${2:-4}
MINUTES=${3:-6}
DISP=${4:-}
BIN=${BIN:-$HOME/src/scummvm/scummvm}
SAVES=$HOME/.local/share/scummvm/saves

# Mark the start so a save from an earlier run is not reported as this one.
STAMP=$(mktemp)
cleanup_file "$STAMP"

D=$(mktemp -d /tmp/spread.XXXXXX)
cp "$HOME/.config/scummvm/scummvm.ini" "$D/t.ini"
python3 - "$D/t.ini" "$HOME/src/scummvm/dists/engine-data" <<'PY'
import sys
ini, engdata = sys.argv[1], sys.argv[2]
s = open(ini).read()
if '[scummvm]' in s:
    s = s.replace('[scummvm]', '[scummvm]' + chr(10) + 'extrapath=' + engdata, 1)
else:
    s = '[scummvm]' + chr(10) + 'extrapath=' + engdata + chr(10) * 2 + s
open(ini, 'w').write(s)
PY

# Start from the furthest-along save there is, so the run explores new ground
# instead of replaying the intro again.
start=$(ls -S "$SAVES/$TARGET".s?? 2>/dev/null | head -1)
slotarg=""
if [ -n "$start" ]; then
	s=$(basename "$start" | sed 's/.*\.s//' | sed 's/^0*\([0-9]\)/\1/')
	slotarg="--save-slot=$s"
	echo "starting from slot $s ($(stat -c%s "$start") bytes)"
fi

TOTAL=$((MINUTES * 60))

if [ -n "$DISP" ]; then

	Xvfb "$DISP" -screen 0 1600x1000x24 -nolisten tcp >/dev/null 2>&1 &

	XV=$!

else


	xvfb_start 1600 1000 || exit 1

	XV=$XVFB_PID

fi
trap 'kill $GAME $XV 2>/dev/null; rm -rf "$D"' EXIT
sleep 2

# shellcheck disable=SC2086
DISPLAY=$DISP timeout $((TOTAL + 30)) "$BIN" --config="$D/t.ini" \
	--gfx-mode=opengl $slotarg \
	--no-filtering --no-aspect-ratio --stretch-mode=pixel-perfect --scale-factor=1 \
	"$TARGET" > "$D/log.txt" 2>&1 &
GAME=$!

sleep 8
ID=$(find_window "$DISP")
[ -n "$ID" ] || { echo "no window"; exit 1; }
DISPLAY=$DISP xdotool windowfocus --sync "$ID" 2>/dev/null

# Play by clicking around: walking to new places is what changes the scene, and
# a scene change is what brings a different charset into use.
slot=1
next_save=$((TOTAL / SLOTS))
t=0
while [ "$t" -lt "$TOTAL" ]; do
	# Spread clicks over the scene rather than one spot, and include the verb
	# row at the bottom so objects actually get used.
	x=$((30 + (t * 53) % 280))
	y=$((40 + (t * 31) % 150))
	DISPLAY=$DISP xdotool mousemove --window "$ID" "$x" "$y" 2>/dev/null
	sleep 1
	DISPLAY=$DISP xdotool click 1 2>/dev/null
	sleep 1

	if [ $((t % 9)) -eq 8 ]; then
		# A verb, so the click above does something other than walk.
		DISPLAY=$DISP xdotool mousemove --window "$ID" $((40 + (t % 5) * 60)) 170 2>/dev/null
		sleep 1
		DISPLAY=$DISP xdotool click 1 2>/dev/null
	fi

	t=$((t + 2))

	if [ "$t" -ge "$next_save" ] && [ "$slot" -le "$SLOTS" ]; then
		f=$(printf '%s/%s.s%02d' "$SAVES" "$TARGET" "$slot")
		DISPLAY=$DISP xdotool key --window "$ID" "alt+$slot" 2>/dev/null
		sleep 2
		# alt+digit only fires when canSaveGameStateCurrently(), so announcing
		# the keystroke as a save was wrong: a run spent entirely in cutscenes
		# printed four "saved" lines and then listed four OLD files.
		if [ -f "$f" ] && [ "$f" -nt "$STAMP" ]; then
			echo "  saved slot $slot at ${t}s"
		else
			echo "  slot $slot at ${t}s: NOT saved (not an interactive moment)"
		fi
		slot=$((slot + 1))
		next_save=$((TOTAL * slot / SLOTS))
	fi
done

kill $GAME 2>/dev/null
wait $GAME 2>/dev/null

echo
echo "saves for $TARGET (* = written by this run):"
for f in "$SAVES/$TARGET".s??; do
	[ -e "$f" ] || continue
	if [ "$f" -nt "$STAMP" ]; then mark="*"; else mark=" "; fi
	ls -l "$f" | awk -v m="$mark" '{print " " m, $5"B", $9}'
done 2>/dev/null | awk '{print "  ", $5"B", $9}'
echo
echo "Different sizes usually mean different scenes; identical sizes mean the"
echo "run never left the room it started in."
