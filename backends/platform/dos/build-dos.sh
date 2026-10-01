#!/bin/bash
# Configure and build the DOS port out of tree, then stage dist/dos.
# Usage: backends/platform/dos/build-dos.sh [sci|scumm] [extra configure args]
#   sci (default): build-dos/,       engines/sci (not sci32)             -> dist/dos/SCUMMVM.EXE
#   scumm:         build-dos-scumm/, engines/scumm (not scumm_7_8, he)   -> dist/dos/SCUMM.EXE
# A first argument that is neither "sci" nor "scumm" is not consumed, so old
# calls (build-dos.sh --foo) still build the sci edition with that as a
# configure argument.
set -e
edition=sci
case "$1" in
	sci|scumm) edition="$1"; shift ;;
esac
source ~/opt/dos-dev/env.sh
src="$(cd "$(dirname "$0")/../../.." && pwd)"
if [ "$edition" = scumm ]; then
	out="$src/build-dos-scumm"
	engine_args=(--enable-engine=scumm --disable-engine=scumm_7_8,he)
	exe=SCUMM.EXE
else
	out="$src/build-dos"
	engine_args=(--enable-engine=sci --disable-engine=sci32)
	exe=SCUMMVM.EXE
fi
mkdir -p "$out" "$src/dist/dos"
cd "$out"
if [ ! -f config.mk ] || [ -n "$*" ]; then
	"$src/configure" --host=i586-pc-msdosdjgpp \
		--disable-all-engines "${engine_args[@]}" \
		--disable-mt32emu --disable-fluidsynth --disable-timidity \
		--disable-zlib --disable-png --disable-jpeg --disable-gif \
		--disable-vorbis --disable-tremor --disable-flac --disable-mad \
		--disable-theoradec --disable-mpeg2 --disable-faad --disable-a52 \
		--disable-freetype2 --disable-fribidi --disable-lua \
		--disable-detection-full --enable-release "$@"
fi
make -j"$(nproc)"
cp scummvm.exe "$src/dist/dos/$exe"
cp "$CWSDPMI_EXE" "$src/dist/dos/CWSDPMI.EXE"
# A fresh DATA: a map or font removed from dists must not stay behind from an earlier build.
rm -rf "$src/dist/dos/DATA"
mkdir -p "$src/dist/dos/DATA" && cp "$src"/dists/engine-data/hires_text/dos/* "$src/dist/dos/DATA/"
cp "$src/dists/engine-data/encoding.dat" "$src/dist/dos/DATA/ENCODING.DAT"
ls -la "$src/dist/dos"
