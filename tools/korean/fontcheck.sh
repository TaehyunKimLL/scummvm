#!/bin/bash
# fontcheck.sh — which font system is a target actually using?
#
# Two font systems can be live at once and it is not obvious from the screen.
# i3-multi took an hour to diagnose for exactly this reason: its map is the old
# TrueType format, so the legacy loader picked it up and drew the text, while
# the new reader found nothing and only the scale was applied. The result was a
# screen drawn by one system and laid out by the other.
#
# This reports, for each target, which loader claimed the map, how many fonts
# each got, and flags the mixed state.
#
#   fontcheck.sh [target ...]      (no arguments: every target with a hi-res key)
set -u
export PATH=$HOME/.local/sysroot/usr/bin:$PATH
export LD_LIBRARY_PATH=$HOME/.local/sysroot/usr/lib/x86_64-linux-gnu
export SDL_AUDIODRIVER=dummy

INI=$HOME/.config/scummvm/scummvm.ini
BIN=${BIN:-$HOME/src/scummvm/scummvm}
# No default: a fixed number made the free-display fallback below dead code,
# and two runs then shared one X server and captured each other's windows.
DISP=${DISP:-}
SECS=${SECS:-13}

if [ $# -gt 0 ]; then
	TARGETS="$*"
else
	TARGETS=$(python3 - "$INI" <<'PY'
import re
import sys

text = open(sys.argv[1], errors="replace").read()
out = []
for block in re.split(r'(?m)^\[', text):
    if not block.strip():
        continue
    name, _, body = block.partition(']')
    if any(k in body for k in ('korean_ttf_map', 'korean_hires_scale',
                               'korean_alpha_text', 'hires_text_map',
                               'hires_text_scale')):
        out.append(name.strip())
print(' '.join(out))
PY
)
fi

D=$(mktemp -d /tmp/fontcheck.XXXXXX)
trap 'rm -rf "$D"' EXIT
cp "$INI" "$D/t.ini"

ENGDATA=$HOME/src/scummvm/dists/engine-data
if [ -f "$ENGDATA/encoding.dat" ]; then
	python3 - "$D/t.ini" "$ENGDATA" <<'PY'
import sys
ini, path = sys.argv[1], sys.argv[2]
s = open(ini).read()
if '[scummvm]' in s:
    s = s.replace('[scummvm]', '[scummvm]' + chr(10) + 'extrapath=' + path, 1)
else:
    s = '[scummvm]' + chr(10) + 'extrapath=' + path + chr(10) * 2 + s
open(ini, 'w').write(s)
PY
else
	echo "warning: no encoding.dat under $ENGDATA" >&2
fi

if [ -n "$DISP" ]; then

	Xvfb "$DISP" -screen 0 1024x768x24 -nolisten tcp >/dev/null 2>&1 &

	XV=$!

else

	. "$(dirname "$0")/xvfb.sh"

	xvfb_start 1024 768 || exit 1

	XV=$XVFB_PID

fi
trap 'kill $XV 2>/dev/null; rm -rf "$D"' EXIT
sleep 2

printf '%-17s %-6s %-6s %-8s %-7s %-5s %s\n' \
	target scale alpha metrics legacy new notes
printf '%-17s %-6s %-6s %-8s %-7s %-5s %s\n' \
	----------------- ------ ------ -------- ------- ----- -----

for t in $TARGETS; do
	log="$D/$t.log"
	# Load a save when one exists: hi-res is configured while the game starts,
	# so a run that never gets that far reports nothing.
	save_arg=""
	for slot in 01 00; do
		if [ -f "$HOME/.local/share/scummvm/saves/$t.s$slot" ]; then
			save_arg="--save-slot=$((10#$slot))"
			break
		fi
	done

	DISPLAY=$DISP timeout "$SECS" "$BIN" --config="$D/t.ini" -d1 \
		--gfx-mode=opengl $save_arg "$t" > "$log" 2>&1

	# A run that produced nothing must not be printed as a data row: every
	# grep below then yields empty and the row reads "this target has hi-res
	# off", which is a definitive-looking answer to a question never asked.
	if [ ! -s "$log" ]; then
		printf '%-18s *** no output - the run failed (%s) ***\n' "$t" "$log"
		continue
	fi
	if ! grep -q "User picked target" "$log"; then
		printf '%-18s *** game never started - see %s ***\n' "$t" "$log"
		continue
	fi

	scale=$(grep -oE 'hi-res text enabled: scale [0-9]+' "$log" | grep -oE '[0-9]+$' | head -1)
	alpha=$(grep -oE 'alpha (on|off)' "$log" | head -1 | awk '{print $2}')
	metrics=$(grep -oE 'metrics (game|font)' "$log" | head -1 | awk '{print $2}')

	# The legacy Korean font loader announces itself and its count.
	legacy=$(grep -oE '[0-9]+ fonts are loaded' "$log" | grep -oE '^[0-9]+' | head -1)
	[ -n "$legacy" ] || legacy=0

	# The new reader logs one line per font it loaded.
	new=$(grep -c 'SCUMM: hi-res font' "$log")

	# The legacy loader (loadKorFont) is upstream code that always looks for
	# korean%02d.fnt, whether or not hi-res text is configured, and serves as
	# the fallback for characters the replacement set does not cover. Its
	# presence alone is not a problem.
	#
	# The problem is hi-res being switched on - so the surface is scaled and
	# the layout follows the new path - while the new reader loaded nothing.
	# Then the legacy fonts are drawn into a surface sized for fonts that are
	# not there.
	notes=""
	if grep -qi 'no replacement font loaded' "$log"; then
		if [ -n "$scale" ]; then
			notes="*** scaled $scale x but NO hi-res font: legacy fonts in a scaled surface ***"
		else
			notes="new reader found no fonts (hi-res off, so harmless)"
		fi
	fi
	if grep -qi 'names no \[bitmap\] fonts' "$log"; then
		notes="$notes; map is in the OLD TrueType format"
	fi
	if grep -qi 'encoding.dat is not found' "$log"; then
		notes="$notes; NO encoding.dat, CJK will not decode"
	fi
	if [ -z "$scale" ]; then
		scale="-"
		[ -z "$notes" ] && notes="hi-res not enabled"
	fi

	printf '%-17s %-6s %-6s %-8s %-7s %-5s %s\n' \
		"$t" "$scale" "${alpha:--}" "${metrics:--}" "$legacy" "$new" "$notes"
done

kill $XV 2>/dev/null

cat <<'NOTES'

Columns:
  legacy  fonts loaded by loadKorFont(), the upstream korean%02d.fnt path.
  This always runs for a Korean target and is the fallback for
  characters the replacement set lacks - a non-zero count is normal.
  new     fonts loaded by the hi-res reader ("SCUMM: hi-res font N: ...")

  What to worry about is a scale with new=0: the text surface is enlarged and the
  layout follows the hi-res path, but the glyphs come from the legacy fonts, so
  the picture is drawn by one system and laid out by the other. That looks like a
  font bug and is not one.

The usual cause is a map in the old format: the new reader wants

    [bitmap]
    multi=korean%02d.fnt

and ignores a map that only has [fonts] default=<something>.ttf.
NOTES
