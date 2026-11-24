#!/bin/bash
# Build the launcher, PLAY.EXE: plain DJGPP, no SDL3 and no ScummVM code.
# Usage: backends/platform/dos/launcher/build-play.sh OUT.EXE
#   OUT.EXE  where to put it; outside the source tree (build-dos.sh play BUILD_DIR gives BUILD_DIR/PLAY.EXE)
set -e
source ~/opt/dos-dev/env.sh
here="$(cd "$(dirname "$0")" && pwd)"
src="$(cd "$here/../../../.." && pwd)"
if [ -z "$1" ]; then
	echo "build-play.sh: usage: build-play.sh OUT.EXE" >&2
	exit 1
fi
out="$(realpath -m "$1")"
case "$out" in
	"$src"/*) echo "build-play.sh: build outside the source tree" >&2; exit 1 ;;
esac
mkdir -p "$(dirname "$out")"
# The driver leaves "play" (COFF) beside "play.exe"; only the latter ships.
tmp="$(mktemp -d "${TMPDIR:-$HOME/work/tmp}/build-play.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT
"$DJGPP_PREFIX/bin/i586-pc-msdosdjgpp-g++" -std=gnu++11 -Os -Wall -Wextra \
	-fno-exceptions -fno-rtti -fno-threadsafe-statics \
	-ffunction-sections -fdata-sections -Wl,--gc-sections -s \
	-o "$tmp/play" "$here/play.cpp"
cp "$tmp/play.exe" "$out"
ls -la "$out"
