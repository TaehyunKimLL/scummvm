#!/bin/bash
# Build the launcher, dist/dos/PLAY.EXE: plain DJGPP, no SDL3 and no ScummVM code.
# Usage: backends/platform/dos/launcher/build-play.sh
set -e
source ~/opt/dos-dev/env.sh
here="$(cd "$(dirname "$0")" && pwd)"
src="$(cd "$here/../../../.." && pwd)"
mkdir -p "$src/dist/dos"
"$DJGPP_PREFIX/bin/i586-pc-msdosdjgpp-g++" -std=gnu++11 -Os -Wall -Wextra \
	-fno-exceptions -fno-rtti -fno-threadsafe-statics \
	-ffunction-sections -fdata-sections -Wl,--gc-sections -s \
	-o "$src/dist/dos/PLAY.EXE" "$here/play.cpp"
ls -la "$src/dist/dos/PLAY.EXE"
