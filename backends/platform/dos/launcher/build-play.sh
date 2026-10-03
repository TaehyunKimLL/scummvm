#!/bin/bash
# Build the launcher, dist/dos/PLAY.EXE: plain DJGPP, no SDL3 and no ScummVM code.
# Usage: backends/platform/dos/launcher/build-play.sh
set -e
source ~/opt/dos-dev/env.sh
here="$(cd "$(dirname "$0")" && pwd)"
src="$(cd "$here/../../../.." && pwd)"
mkdir -p "$src/dist/dos"
# The driver leaves "play" (COFF) beside "play.exe"; only the latter ships.
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
"$DJGPP_PREFIX/bin/i586-pc-msdosdjgpp-g++" -std=gnu++11 -Os -Wall -Wextra \
	-fno-exceptions -fno-rtti -fno-threadsafe-statics \
	-ffunction-sections -fdata-sections -Wl,--gc-sections -s \
	-o "$tmp/play" "$here/play.cpp"
cp "$tmp/play.exe" "$src/dist/dos/PLAY.EXE"
ls -la "$src/dist/dos/PLAY.EXE"
