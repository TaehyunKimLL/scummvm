#!/bin/bash
# noisefloor.sh - measure run-to-run variation before judging an A/B result.
#
#   noisefloor.sh <binA> <binB> <frame> <repeats> <target...>
#
# Two builds that render identically still differ from run to run: The Dig
# plays a SMUSH cutscene at start-up and the frame counter does not pin the
# video's own timeline, so the same binary compared against itself has come
# back anywhere between 0 and 3.3 kB. A single byte count is therefore not a
# verdict - run both comparisons several times and compare their spreads.
#
#   self-spread  overlaps the A/B spread  -> no evidence of a regression
#   A/B spread   sits above it            -> go and look at the pixels
#
# Usage:
#   noisefloor.sh /tmp/svm-alldbg  /tmp/svm-alldbg 600 3 en-dig   # noise floor
#   noisefloor.sh /tmp/svm-basedbg /tmp/svm-alldbg 600 3 en-dig   # A/B

set -u

BIN_A=${1:?binary A}
BIN_B=${2:?binary B}
FRAME=${3:-600}
REPEATS=${4:-3}
shift 4 2>/dev/null || {
	echo "usage: noisefloor.sh <binA> <binB> <frame> <repeats> <target...>" >&2
	exit 1
}

if [ "$#" -eq 0 ]; then
	echo "usage: noisefloor.sh <binA> <binB> <frame> <repeats> <target...>" >&2
	exit 1
fi
TARGETS=("$@")

for b in "$BIN_A" "$BIN_B"; do
	[ -x "$b" ] || { echo "not executable: $b" >&2; exit 1; }
done

echo "A: $BIN_A"
echo "B: $BIN_B"
[ "$BIN_A" = "$BIN_B" ] && echo "   (same binary - this is the noise floor)"
echo "frame $FRAME, $REPEATS repeats, targets: ${TARGETS[*]}"
echo

LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT

for i in $(seq 1 "$REPEATS"); do
	echo "--- run $i/$REPEATS"
	# Tag each run so the summary can separate them.
	echo "RUNMARK $i" >> "$LOG"
	bash "$HOME/games/fbcompare.sh" "$BIN_A" "$BIN_B" "$FRAME" "${TARGETS[@]}" \
		2>&1 | grep -vE "amdgpu|xkbcomp|keysym" | tee -a "$LOG"
	echo
done

echo "======== summary ========"
for t in "${TARGETS[@]}"; do
	printf '%-12s ' "$t"
	awk -v tgt="$t" '
		/^RUNMARK/            { run = $2; sum[run] = 0; seen[run] = 0; next }
		$1 == tgt             { active = 1; seen[run] = 1; next }
		/^[a-zA-Z]/           { active = 0 }
		active && /NO DUMPS/  { bad[run] = 1 }
		active {
			# Byte counts appear as "f600_vs0.bin:2515B".
			s = $0
			while (match(s, /[0-9]+B/)) {
				sum[run] += substr(s, RSTART, RLENGTH - 1)
				s = substr(s, RSTART + RLENGTH)
			}
		}
		END {
			lo = -1; hi = 0; n = 0
			for (r = 1; r <= run; r++) {
				if (!seen[r]) continue
				if (bad[r]) { printf "NODUMP "; continue }
				printf "%dB ", sum[r]
				n++
				if (lo < 0 || sum[r] < lo) lo = sum[r]
				if (sum[r] > hi) hi = sum[r]
			}
			if (n) printf " -> spread %d..%dB", lo, hi
			printf "\n"
		}' "$LOG"
done
echo
echo "Compare the spreads, not single numbers."
