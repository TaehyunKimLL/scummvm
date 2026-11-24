#!/bin/bash
# Build the launcher, dist/dos/PLAY.EXE: plain DJGPP, no SDL3 and no ScummVM code.
# Usage: backends/platform/dos/launcher/build-play.sh [OUT.EXE]
#   OUT.EXE  where to put it instead of dist/dos/PLAY.EXE (a test build)
set -e
source ~/opt/dos-dev/env.sh
here="$(cd "$(dirname "$0")" && pwd)"
src="$(cd "$here/../../../.." && pwd)"
out="${1:-$src/dist/dos/PLAY.EXE}"
mkdir -p "$(dirname "$out")"
# The driver leaves "play" (COFF) beside "play.exe"; only the latter ships.
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
"$DJGPP_PREFIX/bin/i586-pc-msdosdjgpp-g++" -std=gnu++11 -Os -Wall -Wextra \
	-fno-exceptions -fno-rtti -fno-threadsafe-statics \
	-ffunction-sections -fdata-sections -Wl,--gc-sections -s \
	-o "$tmp/play" "$here/play.cpp"
cp "$tmp/play.exe" "$out"
ls -la "$out"
