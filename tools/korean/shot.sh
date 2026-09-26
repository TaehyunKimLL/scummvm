#!/bin/bash
# shot.sh — one capture of a running target, driven far enough to show text.
#
# Wraps the fiddly parts that cost time repeatedly: extrapath for encoding.dat,
# capturing the window rather than the root, XTEST rather than XSendEvent, and
# skipping an intro that would otherwise fill the frame with a title card.
#
#   shot.sh <target> <out.png> [slot] [display] [skip-escapes] [settle-seconds]
set -u
export PATH=$HOME/.local/sysroot/usr/bin:$PATH
export LD_LIBRARY_PATH=$HOME/.local/sysroot/usr/lib/x86_64-linux-gnu
export SDL_AUDIODRIVER=dummy

# Find the game window without depending on the executable's name: SDL derives
# the window class from argv[0], so a binary copied to /tmp/svm-work has class
# "svm-work" and a search for "scummvm" finds nothing. Take the first mapped
# child of the root instead.
find_window() {
	local disp=$1
	DISPLAY=$disp xwininfo -root -children 2>/dev/null | \
		awk '/^     0x/ {print $1; exit}'
}

TARGET=${1:?usage: shot.sh <target> <out.png> [slot] [display] [escapes] [settle]}
OUT=${2:?output png}
SLOT=${3:-1}
DISP=${4:-}
ESCAPES=${5:-0}
SETTLE=${6:-8}
BIN=${BIN:-$HOME/src/scummvm/scummvm}

D=$(mktemp -d /tmp/shot.XXXXXX)
cp "$HOME/.config/scummvm/scummvm.ini" "$D/t.ini"

# NOHIRES=1 captures the game's ORIGINAL bitmap rendering, which is the control
# a "does our layer draw the right thing?" question needs. Comparing two builds
# that both have the feature only shows which one regressed.
#
# hires_text_scale=1 is NOT enough: the map in the game folder is still picked
# up automatically, the replacement fonts still load, and the glyphs are drawn
# from our SVFN set at 1x and then magnified by the backend. The log gives it
# away - "scale 1 ... fonts hr%02d.fnt" - and the capture shows big anti-
# aliased text that is ours, not the game's. Point the game at a copy of the
# folder with no map in it.
INIFIX_ARGS=""
if [ "${NOHIRES:-0}" = "1" ]; then
	INIFIX_ARGS="--no-hires"

	GAMEPATH=$(sed -n "/^\[$TARGET\]/,/^\[/p" "$HOME/.config/scummvm/scummvm.ini" \
		| sed -n 's/^path=//p' | head -1)
	if [ -n "$GAMEPATH" ] && [ -d "$GAMEPATH" ]; then
		PLAIN="$D/plain"
		mkdir -p "$PLAIN"
		# Symlink the data, but leave the map and the baked fonts behind.
		for f in "$GAMEPATH"/*; do
			b=$(basename "$f")
			case "$b" in
				hires_text.map|korean_ttf.map|km_*.map) continue ;;
				hr*.fnt|i4v*.fnt|uni24_*.fnt) continue ;;
			esac
			ln -sf "$f" "$PLAIN/$b"
		done
		python3 - "$D/t.ini" "$TARGET" "$PLAIN" <<'PY'
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
python3 "$(dirname "$0")/inifix.py" "$D/t.ini" "$TARGET" $INIFIX_ARGS || {
	echo "shot.sh: inifix failed - the run would use an unpatched ini" >&2
	exit 1
}

if [ -n "$DISP" ]; then

	Xvfb "$DISP" -screen 0 1600x1000x24 -nolisten tcp >/dev/null 2>&1 &

	XV=$!

else

	. "$(dirname "$0")/xvfb.sh"

	xvfb_start 1600 1000 || exit 1

	XV=$XVFB_PID

fi
trap 'kill $XV 2>/dev/null; rm -rf "$D"' EXIT
sleep 2

# The game has to outlive: settle, one second per Escape, the 5s pause after
# the click, and the capture itself. Getting this wrong kills the process
# before xwd runs and no file appears - which reads like a rendering failure.
TOTAL=$(( SETTLE + ESCAPES + 20 ))
DISPLAY=$DISP timeout "$TOTAL" "$BIN" --config="$D/t.ini" -d1 \
	--gfx-mode=opengl --save-slot="$SLOT" \
	--no-filtering --no-aspect-ratio --stretch-mode=pixel-perfect --scale-factor=1 \
	"$TARGET" > "$D/log.txt" 2>&1 &
GAME=$!

sleep "$SETTLE"
ID=$(find_window "$DISP")
if [ -n "$ID" ]; then
	DISPLAY=$DISP xdotool windowfocus --sync "$ID" 2>/dev/null
	i=0
	while [ "$i" -lt "$ESCAPES" ]; do
		DISPLAY=$DISP xdotool key --clearmodifiers Escape 2>/dev/null
		sleep 1
		i=$((i + 1))
	done
	# Hover and click so the sentence line and a reply are on screen.
	DISPLAY=$DISP xdotool mousemove --window "$ID" 160 100 click 1 2>/dev/null
	sleep 5
	DISPLAY=$DISP xwd -id "$ID" -silent > "$D/s.xwd" 2>/dev/null
	if [ -s "$D/s.xwd" ]; then
		python3 "$HOME/games/xwd2png.py" "$D/s.xwd" "$OUT"
	else
		echo "no window content captured - did the game exit early?" >&2
	fi
else
	echo "no scummvm window found on $DISP" >&2
fi

kill $GAME 2>/dev/null
wait $GAME 2>/dev/null
grep -iE "hi-res text enabled|hi-res font 0|not be blended" "$D/log.txt" | head -3
