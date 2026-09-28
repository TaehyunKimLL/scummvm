#!/bin/bash
# Configure and build the DOS port out of tree, then stage dist/dos.
# Usage: backends/platform/dos/build-dos.sh [extra configure args]
set -e
source ~/opt/dos-dev/env.sh
src="$(cd "$(dirname "$0")/../../.." && pwd)"
out="$src/build-dos"
mkdir -p "$out" "$src/dist/dos"
cd "$out"
if [ ! -f config.mk ] || [ -n "$*" ]; then
	"$src/configure" --host=i586-pc-msdosdjgpp \
		--disable-all-engines --enable-engine=sci --disable-engine=sci32 \
		--disable-mt32emu --disable-fluidsynth --disable-timidity \
		--disable-zlib --disable-png --disable-jpeg --disable-gif \
		--disable-vorbis --disable-tremor --disable-flac --disable-mad \
		--disable-theoradec --disable-mpeg2 --disable-faad --disable-a52 \
		--disable-freetype2 --disable-fribidi --disable-lua \
		--disable-detection-full --enable-release "$@"
fi
make -j"$(nproc)"
cp scummvm.exe "$src/dist/dos/SCUMMVM.EXE"
cp "$CWSDPMI_EXE" "$src/dist/dos/CWSDPMI.EXE"
mkdir -p "$src/dist/dos/DATA" && cp "$src"/dists/engine-data/hires_text/dos/* "$src/dist/dos/DATA/"
ls -la "$src/dist/dos"
