#!/bin/bash
# fbcompare.sh — compare frame dumps of two builds across several targets.
#
# Frame-indexed, so the comparison does not depend on how fast the machine ran.
# Prints, per target, how many dumped buffers are byte-identical and how many
# differ, plus the main screen geometry so a scale change is visible.
set -u
BIN_A=${1:?usage: fbcompare.sh <binA> <binB> <frame> <target>...}
BIN_B=${2:?binary B}
FRAME=${3:?frame number}
shift 3

for t in "$@"; do
	# Include the pid: the path used to be a function of the target name only,
	# so a second comparison of the same target rm -rf'd the first one's dumps
	# mid-run and then compared a mixture of both builds.
	da=/tmp/fbcmp_a_${t}_$$
	db=/tmp/fbcmp_b_${t}_$$
	rm -rf "$da" "$db"
	mkdir -p "$da" "$db"

	# Run both builds at once - they take separate free displays (fbdump.sh
	# allocates them) and separate temporary GDB scripts, so there is nothing
	# left to serialise for. Their output is kept rather than discarded: a
	# harness failure used to surface as "the game may not have reached the
	# frame", which sent the reader to look in the wrong place.
	bash "$HOME/games/fbdump.sh" "$da" "$t" "$FRAME" "$BIN_A" > "$da/fbdump.log" 2>&1 &
	pid_a=$!
	bash "$HOME/games/fbdump.sh" "$db" "$t" "$FRAME" "$BIN_B" > "$db/fbdump.log" 2>&1 &
	pid_b=$!
	wait $pid_a; rc_a=$?
	wait $pid_b; rc_b=$?

	same=0
	diff=0
	details=""
	for f in "$da"/*.bin; do
		[ -e "$f" ] || continue
		b="$db/$(basename "$f")"
		if [ ! -f "$b" ]; then
			details="$details $(basename "$f"):missing"
			continue
		fi
		if cmp -s "$f" "$b"; then
			same=$((same + 1))
		else
			diff=$((diff + 1))
			n=$(cmp -l "$f" "$b" 2>/dev/null | wc -l)
			details="$details $(basename "$f"):${n}B"
		fi
	done

	geo_a=$(grep -h 'vs0 ' "$da/meta.txt" 2>/dev/null | head -1)
	sz_a=$(stat -c %s "$da/f${FRAME}_text.bin" 2>/dev/null || echo -)
	sz_b=$(stat -c %s "$db/f${FRAME}_text.bin" 2>/dev/null || echo -)

	# same=0 diff=0 means neither side produced a dump, not that they agreed.
	# Reading it as a pass is how a broken harness looks like a clean run.
	if [ "$((same + diff))" -eq 0 ]; then
		printf '%-14s *** NO DUMPS - nothing was compared ***\n' "$t"
		printf '                a: %s files, b: %s files\n' \
			"$(ls "$da"/*.bin 2>/dev/null | wc -l)" \
			"$(ls "$db"/*.bin 2>/dev/null | wc -l)"
		[ "$rc_a" -ne 0 ] && printf '                build A: fbdump exited %d\n' "$rc_a"
		[ "$rc_b" -ne 0 ] && printf '                build B: fbdump exited %d\n' "$rc_b"
		for d in "$da" "$db"; do
			[ -s "$d/fbdump.log" ] || continue
			sed 's/^/                  /' "$d/fbdump.log" | tail -4
		done
	else
		printf '%-14s same=%d diff=%d  text: %s -> %s\n' "$t" "$same" "$diff" "$sz_a" "$sz_b"
	fi
	[ -n "$details" ] && printf '               %s\n' "$details"
	[ -n "$geo_a" ] && printf '              %s\n' "$geo_a"
done
