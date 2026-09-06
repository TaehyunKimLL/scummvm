# xvfb.sh — pick a free X display and start Xvfb on it.
#
# Source this, then call `xvfb_start`. It sets DISP and XVFB_PID, and registers
# an exit trap that kills the server.
#
#   . "$(dirname "$0")/xvfb.sh"
#   xvfb_start 1600 1000
#   DISPLAY=$DISP some-command
#
# Why: every harness here hardcoded its display number (:870, :881, :2 ...) and
# two runs on the same number silently share one X server. Windows from one run
# then get captured by the other, or a run finds a window that belongs to a
# different game entirely - and the result looks like a rendering difference.
#
# The lock file X itself uses (/tmp/.X<n>-lock) is the reliable test: checking
# only whether a process is running loses the race between two starts.

# Find a display number nothing is using, starting from a random offset so two
# processes launched together do not both pick the same first candidate.
xvfb_free_display() {
	local n
	local start=$(( (RANDOM % 400) + 100 ))
	for n in $(seq "$start" $((start + 400))); do
		[ -e "/tmp/.X${n}-lock" ] && continue
		[ -e "/tmp/.X11-unix/X${n}" ] && continue
		echo ":$n"
		return 0
	done
	return 1
}

# Registering an EXIT trap REPLACES any previous one, so a script that traps a
# temp file and later traps a process ends up doing only the second - the temp
# files then pile up in /tmp for ever. Collect the work instead of trapping
# twice.
_CLEANUP_FILES=""
_CLEANUP_PIDS=""

_run_cleanup() {
	local f p
	for p in $_CLEANUP_PIDS; do
		kill "$p" 2>/dev/null
	done
	for f in $_CLEANUP_FILES; do
		rm -rf "$f"
	done
}

# cleanup_file <path> ...   remove these on exit
cleanup_file() {
	_CLEANUP_FILES="$_CLEANUP_FILES $*"
	trap _run_cleanup EXIT
}

# cleanup_pid <pid> ...     kill these on exit
cleanup_pid() {
	_CLEANUP_PIDS="$_CLEANUP_PIDS $*"
	trap _run_cleanup EXIT
}


# xvfb_start [width] [height] [depth]
xvfb_start() {
	local w=${1:-1600}
	local h=${2:-1000}
	local d=${3:-24}

	local tries=0
	while [ "$tries" -lt 5 ]; do
		DISP=$(xvfb_free_display) || return 1

		Xvfb "$DISP" -screen 0 "${w}x${h}x${d}" -nolisten tcp >/dev/null 2>&1 &
		XVFB_PID=$!

		# Wait for it to actually accept connections; a server that lost the
		# race dies within a second and xdotool would then fail confusingly.
		local i=0
		while [ "$i" -lt 20 ]; do
			if ! kill -0 "$XVFB_PID" 2>/dev/null; then
				break
			fi
			if DISPLAY=$DISP xdpyinfo >/dev/null 2>&1; then
				export DISP XVFB_PID
				cleanup_pid "$XVFB_PID"
				return 0
			fi
			sleep 0.2
			i=$((i + 1))
		done

		kill "$XVFB_PID" 2>/dev/null
		tries=$((tries + 1))
	done

	echo "could not start Xvfb" >&2
	return 1
}

xvfb_stop() {
	[ -n "${XVFB_PID:-}" ] && kill "$XVFB_PID" 2>/dev/null
	XVFB_PID=
}


# Find the game window without depending on the executable's name: SDL derives
# the window class from argv[0], so a binary copied to /tmp/svm-work has class
# "svm-work" and a search for "scummvm" finds nothing.
find_window() {
	local disp=${1:-$DISP}
	DISPLAY=$disp xwininfo -root -children 2>/dev/null | \
		awk '/^     0x/ {print $1; exit}'
}
