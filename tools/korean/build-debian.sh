#!/bin/bash
# build-debian.sh — build the Korean hi-res text branch on a plain Debian box.
#
# Written for ChromeOS Crostini, which is Debian with no GPU passthrough
# surprises and no ScummVM build dependencies installed. It should work on any
# Debian or Ubuntu.
#
#   git clone -b hires-text https://github.com/TaehyunKimLL/scummvm.git
#   cd scummvm
#   tools/korean/build-debian.sh
#
# Options:
#   --deps-only     install packages and stop
#   --no-deps       skip the apt step (already installed, or no sudo)
#   --jobs N        parallel jobs (default: all cores)
#   --scumm-only    build just the SCUMM engine - much faster, enough to test
set -eu

DEPS=1
BUILD=1
JOBS=$(nproc 2>/dev/null || echo 2)
SCUMM_ONLY=0

while [ $# -gt 0 ]; do
	case "$1" in
	--deps-only) BUILD=0 ;;
	--no-deps)   DEPS=0 ;;
	--scumm-only) SCUMM_ONLY=1 ;;
	--jobs) shift; JOBS=$1 ;;
	-h|--help) sed -n '2,20p' "$0"; exit 0 ;;
	*) echo "unknown option: $1"; exit 1 ;;
	esac
	shift
done

# Every one of these is needed for a build that can actually show Korean text:
# freetype for the font baking tools, fluidsynth/mad/vorbis for the audio the
# games expect, and libsdl2 for everything else.
PACKAGES="
build-essential git pkg-config
libsdl2-dev libsdl2-net-dev
libfreetype6-dev libpng-dev libjpeg-dev libgif-dev
libmad0-dev libvorbis-dev libflac-dev libmpeg2-4-dev
libtheora-dev libfaad-dev libfluidsynth-dev
libcurl4-openssl-dev libspeechd-dev
zlib1g-dev
python3 python3-pil fonts-nanum
"

if [ "$DEPS" = 1 ]; then
	echo "== installing build dependencies"
	if ! command -v sudo >/dev/null; then
		echo "no sudo; install these yourself and rerun with --no-deps:"
		echo "$PACKAGES"
		exit 1
	fi
	sudo apt-get update
	# shellcheck disable=SC2086
	sudo apt-get install -y $PACKAGES
	echo
fi

[ "$BUILD" = 1 ] || exit 0

# The repository root, two levels up from tools/korean.
cd "$(dirname "$0")/../.."

if [ ! -f configure ]; then
	echo "cannot find the scummvm checkout - run this from inside it"
	exit 1
fi

echo "== configuring"
if [ "$SCUMM_ONLY" = 1 ]; then
	# The Korean work is all in the SCUMM engine, and this cuts the build from
	# about half an hour to a few minutes.
	./configure --disable-all-engines --enable-engine=scumm,scumm_7_8
else
	./configure
fi

echo
echo "== building with $JOBS jobs"
make -j"$JOBS"

echo
echo "== done"
ls -l scummvm
echo
echo "Next:"
echo "  1. put your Korean game folder somewhere, with its korean*.fnt files"
echo "  2. bake replacement fonts:  tools/korean/bakecells.sh <game-folder> 2"
echo "  3. generate the map:        python3 tools/korean/makemaps.py <game-folder>"
echo "  4. add the game in ScummVM, then set in ~/.config/scummvm/scummvm.ini:"
echo "         hires_text_map=<game-folder>/hires_text.map"
echo
echo "  encoding.dat must be findable or Korean silently turns into U+FFFD:"
echo "         [scummvm]"
echo "         extrapath=$(pwd)/dists/engine-data"
