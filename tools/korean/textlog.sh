#!/bin/bash
# textlog.sh — log what text is drawn, with which font, while you play.
#
# Prints one line per run of text as it is drawn:
#
#   HRTEXT charset=2 font=2 cell=18x18 "안녕하세요"
#
# Use it to find scenes that exercise fonts nothing has tested yet: play until
# the log shows the charset you are missing, then quicksave there with
# Alt+<digit>.
#
#   textlog.sh <target> [slot] [seconds] [display]
#   BIN=/tmp/svm-log textlog.sh mi2-svfn-hr 1 120
#
# The log is left at /tmp/textlog_<target>.txt.
set -u
export PATH=$HOME/.local/sysroot/usr/bin:$PATH
export LD_LIBRARY_PATH=$HOME/.local/sysroot/usr/lib/x86_64-linux-gnu
export SDL_AUDIODRIVER=dummy

find_window() {
	local disp=$1
	DISPLAY=$disp xwininfo -root -children 2>/dev/null | \
		awk '/^     0x/ {print $1; exit}'
}

TARGET=${1:?usage: textlog.sh <target> [slot] [seconds] [display]}
SLOT=${2:-1}
SECS=${3:-120}
DISP=${4:-}
BIN=${BIN:-$HOME/src/scummvm/scummvm}
OUT=/tmp/textlog_$TARGET.txt

D=$(mktemp -d /tmp/textlog.XXXXXX)
cp "$HOME/.config/scummvm/scummvm.ini" "$D/t.ini"
python3 "$HOME/games/inifix.py" "$D/t.ini" "$TARGET" --log || {
	echo "inifix 실패 - 설정이 적용되지 않았다" >&2
	exit 1
}

slotarg=""
[ -f "$HOME/.local/share/scummvm/saves/$TARGET.s0$SLOT" ] && slotarg="--save-slot=$SLOT"

if [ -n "$DISP" ]; then

	Xvfb "$DISP" -screen 0 1600x1000x24 -nolisten tcp >/dev/null 2>&1 &

	XV=$!

else

	. "$(dirname "$0")/xvfb.sh"

	xvfb_start 1600 1000 || exit 1

	XV=$XVFB_PID

fi
trap 'kill $GAME $XV 2>/dev/null; rm -rf "$D"' EXIT
sleep 2

# shellcheck disable=SC2086
DISPLAY=$DISP timeout $((SECS + 15)) "$BIN" --config="$D/t.ini" -d1 \
	--gfx-mode=opengl $slotarg \
	--no-filtering --no-aspect-ratio --stretch-mode=pixel-perfect --scale-factor=1 \
	"$TARGET" > "$D/log.txt" 2>&1 &
GAME=$!

sleep 8
ID=$(find_window "$DISP")
[ -n "$ID" ] || { echo "no window"; exit 1; }
DISPLAY=$DISP xdotool windowfocus --sync "$ID" 2>/dev/null

echo "playing for ${SECS}s; text is being logged"
t=0
while [ "$t" -lt "$SECS" ]; do
	DISPLAY=$DISP xdotool mousemove --window "$ID" \
		$((30 + (t * 53) % 280)) $((40 + (t * 31) % 150)) 2>/dev/null
	sleep 1
	DISPLAY=$DISP xdotool click 1 2>/dev/null
	sleep 1
	t=$((t + 2))
done

kill $GAME 2>/dev/null
wait $GAME 2>/dev/null

# The log is UTF-8 but ScummVM's own output can carry stray bytes, so filter
# rather than reading the whole file as text.
grep -a HRTEXT "$D/log.txt" > "$OUT" 2>/dev/null

echo
echo "text drawn, by font:"
python3 - "$OUT" <<'PY'
import collections
import re
import sys

pat = re.compile(r'HRTEXT charset=(\d+) font=(-?\d+) cell=(\d+)x(\d+) "(.*)"')
seen = collections.OrderedDict()
for line in open(sys.argv[1], errors="replace"):
    m = pat.search(line)
    if not m:
        continue
    cs, font, w, h, text = m.groups()
    key = (int(cs), int(font), f"{w}x{h}")
    seen.setdefault(key, []).append(text)

if not seen:
    print("  nothing logged - is hires_text_log set for this target?")
    raise SystemExit

for (cs, font, cell), runs in sorted(seen.items()):
    # A sample long enough to recognise, and the count so rare paths show up.
    sample = max(runs, key=len)
    if len(sample) > 60:
        sample = sample[:57] + "..."
    print(f"  charset {cs:>2}  font {font:>2}  {cell:>7}  {len(runs):>4} runs  {sample}")
PY

echo
echo "full log: $OUT"
