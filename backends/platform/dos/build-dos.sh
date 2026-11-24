#!/bin/bash
# Configure and build one DOS executable out of tree.
# Usage: backends/platform/dos/build-dos.sh [sci|scumm|agi] BUILD_DIR [extra configure args]
#   sci:   engines/sci (not sci32)                                   -> BUILD_DIR/scummvm.exe (SCI.EXE)
#   scumm: engines/scumm (not scumm_7_8, he), FLAC + Tremor          -> BUILD_DIR/scummvm.exe (SCUMM.EXE)
#   agi:   engines/agi                                               -> BUILD_DIR/scummvm.exe (AGI.EXE)
# BUILD_DIR must be outside the source tree. A build directory configured with other flags or another SDL3
# is configured again (see config_current below). Environment: SDL3_DOS_MIN (the trimmed SDL3 prefix of
# sdl3-build.txt, default ~/opt/sdl3-dos-min-kbd), DOS_CODECS (SCUMM, default ~/opt/codecs-dos), DOS_MARCH
# (empty: DJGPP's default -march=pentium; i486 builds -march=i486 -mtune=pentium: not the shipped build).
set -e
edition=sci
case "$1" in
	sci|scumm|agi) edition="$1"; shift ;;
esac
source ~/opt/dos-dev/env.sh
src="$(cd "$(dirname "$0")/../../.." && pwd)"
if [ -z "$1" ]; then
	echo "build-dos.sh: usage: build-dos.sh [sci|scumm|agi] BUILD_DIR [configure args]" >&2
	exit 1
fi
out="$(realpath -m "$1")"
shift
case "$out/" in
	"$src"/*) echo "build-dos.sh: build outside the source tree" >&2; exit 1 ;;
esac
# The faces staged into DATA come from the svf-fonts-open submodule: stop before the long build.
if [ ! -f "$src/dists/svf-fonts-open/svf/dos/NEODG16A.SVF" ]; then
	echo "build-dos.sh: the svf-fonts-open submodule (dists/svf-fonts-open) is missing." >&2
	echo "  Run 'git submodule update --init' in $src, then try again." >&2
	exit 1
fi
# The trimmed SDL3 build (sdl3-build.txt): SDL3_DOS_MIN overrides where it is.
export SDL3_DOS="${SDL3_DOS_MIN:-$HOME/opt/sdl3-dos-min-kbd}"
export PKG_CONFIG_LIBDIR="$SDL3_DOS/lib/pkgconfig"
sdl_lib="$SDL3_DOS/lib/libSDL3.a"
if [ ! -f "$sdl_lib" ]; then
	echo "build-dos.sh: no SDL3 for DOS at $sdl_lib (set SDL3_DOS_MIN)." >&2
	echo "  Build SDL3 as backends/platform/dos/sdl3-build.txt says." >&2
	exit 1
fi
# SDL 3.5.0 with the patch series of sdl3/ (the DOS port exists only there, sdl3-build.txt).
if ! strings "$sdl_lib" | grep -q "SDL-3\.5\.0-"; then
	echo "build-dos.sh: $sdl_lib is not SDL 3.5.0 (sdl3-build.txt)" >&2
	exit 1
fi
# Each patch leaves a mark the link checks (a missing one would fail late): refuse an SDL3 without any of them.
#   DOS_IRQCodeChecked     irq-code: the Sound Blaster handler's code is locked
#   DOS_CPUIDChecked       cpuid: SDL_Init() does not run CPUID on a 486 without it
#   DOS_SBShutdownChecked  sb-shutdown: closing a card that sends no interrupts does not hang the exit
#   DOS_SBOpenFailChecked  sb-open-fail: a card that cannot be opened leaves the program without sound
#   DOS_SBProbeChecked     sb-probe: the DMA channel is probed (BLASTER's D may be wrong)
#   DOS_SBStatsChecked     sb-stats: the mixer reads underruns and ring fill (DOS_SBGetStats)
#   DOS_SBRingChunks       sb-ring: five device buffers, not four
#   DOS_KbdHangulChecked   kbd-hangul: Han/Yeong and Hanja (bytes 0xF2/0xF1) reach the engine as LANG1/LANG2
syms="$("$DJGPP_PREFIX/bin/i586-pc-msdosdjgpp-nm" "$sdl_lib" 2>/dev/null)"
for m in DOS_IRQCodeChecked DOS_CPUIDChecked DOS_SBShutdownChecked DOS_SBOpenFailChecked DOS_SBProbeChecked \
         DOS_SBStatsChecked DOS_SBRingChunks DOS_KbdHangulChecked; do
	if ! printf '%s\n' "$syms" | grep -q " [TDR] _$m\$"; then
		echo "build-dos.sh: $sdl_lib lacks $m." >&2
		echo "  Rebuild SDL3 as backends/platform/dos/sdl3-build.txt says (git am of sdl3/*.patch)." >&2
		exit 1
	fi
done
# SCUMM.EXE plays compressed speech and CD tracks (the Ultimate Talkie
# editions: FLAC MONKEY.SOF, Vorbis MONKEY2.SOG, FLAC tracks) with the
# libraries build-deps.sh builds; SCI.EXE links none.
codecs="${DOS_CODECS:-$HOME/opt/codecs-dos}"
if [ "$edition" = scumm ]; then
	engine_args=(--enable-engine=scumm --disable-engine=scumm_7_8,he)
	exe=SCUMM.EXE
	for l in libFLAC.a libogg.a libvorbisidec.a; do
		if [ ! -f "$codecs/lib/$l" ]; then
			echo "build-dos.sh: no $codecs/lib/$l (set DOS_CODECS)." >&2
			echo "  Run backends/platform/dos/build-deps.sh codecs." >&2
			exit 1
		fi
	done
	codec_args=(--disable-vorbis --with-tremor-prefix="$codecs" --with-ogg-prefix="$codecs"
		--with-flac-prefix="$codecs" --disable-mad)
elif [ "$edition" = agi ]; then
	engine_args=(--enable-engine=agi)
	exe=AGI.EXE
	codec_args=(--disable-vorbis --disable-tremor --disable-flac --disable-mad)
else
	engine_args=(--enable-engine=sci --disable-engine=sci32)
	exe=SCI.EXE
	codec_args=(--disable-vorbis --disable-tremor --disable-flac --disable-mad)
fi
conf_args=(--host=i586-pc-msdosdjgpp
	--disable-all-engines "${engine_args[@]}"
	--disable-mt32emu --disable-fluidsynth --disable-timidity
	--disable-zlib --disable-png --disable-jpeg --disable-gif
	"${codec_args[@]}"
	--disable-theoradec --disable-mpeg2 --disable-faad --disable-a52
	--disable-freetype2 --disable-fribidi --disable-lua
	--disable-detection-full --disable-translation --enable-release)
# The debug socket's UART transport comes with ko/07-debugsocket: until configure knows the option it is not asked for.
if "$src/configure" --help | grep -q -- '--disable-debug-socket'; then
	conf_args+=(--disable-debug-socket)
fi
# DOS_MARCH=i486: a 486 build for machines without Pentium instructions (D-4: not the shipped one).
march_flags=
case "$DOS_MARCH" in
	"") ;;
	i486) march_flags="-march=i486 -mtune=pentium" ;;
	*) echo "build-dos.sh: DOS_MARCH is empty or i486" >&2; exit 1 ;;
esac
mkdir -p "$out"
cd "$out"
# audio/decoders/vorbis.h: VorbisStream refills 4096 samples everywhere else;
# here 1024, so that one refill stays short. Every object gets the define
# (vorbis.cpp and the Nancy subclass both lay VorbisStream out from it).
dos_cppflags="-DVORBIS_REFILL_SAMPLES=1024"
# Configure again when there is no config.mk, when extra arguments are
# given, or when the one there was made with other flags or another SDL3
# (its SAVED_CONFIGFLAGS must start with conf_args - extra arguments of an
# earlier run may follow - and SAVED_PKG_CONFIG_LIBDIR must be this SDL3's).
# Otherwise make's own configure.stamp rule would rerun configure with the
# stale flags and SDL3 of the old config.mk after a change to configure.
config_current() {
	[ -f config.mk ] || return 1
	local saved pkg cpp
	saved="$(sed -n 's/^SAVED_CONFIGFLAGS *:= *//p' config.mk)"
	pkg="$(sed -n 's/^SAVED_PKG_CONFIG_LIBDIR *:= *//p' config.mk)"
	cpp="$(sed -n 's/^SAVED_CPPFLAGS *:= *//p' config.mk)"
	case "$saved" in
		"${conf_args[*]}"|"${conf_args[*]} "*) ;;
		*) echo "build-dos.sh: $out/config.mk has other configure flags; configuring again." >&2; return 1 ;;
	esac
	if [ "$pkg" != "$PKG_CONFIG_LIBDIR" ]; then
		echo "build-dos.sh: $out/config.mk uses another SDL3 ($pkg); configuring again." >&2
		return 1
	fi
	case " $cpp " in
		*" $dos_cppflags "*) ;;
		*) echo "build-dos.sh: $out/config.mk lacks $dos_cppflags; configuring again." >&2; return 1 ;;
	esac
}
if [ -n "$*" ] || ! config_current; then
	CXXFLAGS="${CXXFLAGS:+$CXXFLAGS }$march_flags" CPPFLAGS="${CPPFLAGS:+$CPPFLAGS }$dos_cppflags" \
		"$src/configure" "${conf_args[@]}" "$@"
fi
if ! config_current || ! grep -q '^BACKEND := dos$' config.mk; then
	echo "build-dos.sh: $out/config.mk is not the DOS configuration after configure; not building." >&2
	exit 1
fi
if [ "$edition" = scumm ] && ! { grep -q '^#define USE_FLAC$' config.h && grep -q '^#define USE_TREMOR$' config.h; }; then
	echo "build-dos.sh: configure did not take libFLAC and Tremor from $codecs; not building." >&2
	exit 1
fi
if [ "$edition" = sci ] && grep -qE '^#define USE_(FLAC|TREMOR|VORBIS|MAD)$' config.h; then
	echo "build-dos.sh: the sci edition must link no codec; not building." >&2
	exit 1
fi
if [ "$edition" = scumm ]; then
	# Recompile this object on every invocation: a previous build may have
	# used another prefix (or no private header) without changing its mtime.
	setup_flags=
	[ ! -f "$src/backends/platform/dos/check-tremor-cache.sh" ] || bash "$src/backends/platform/dos/check-tremor-cache.sh" "$codecs"
	if [ -f "$codecs/include/tremor/codec_internal.h" ]; then
		setup_flags="-DVORBIS_SETUP_INTERNALS -I$codecs/include/tremor"
	fi
	# Force this object only: -B would also rebuild config.mk and rerun
	# configure on every invocation. A prior prefix may have used other flags.
	rm -f audio/decoders/vorbis.o
	make audio/decoders/vorbis.o DOS_VORBIS_SETUP_CPPFLAGS="$setup_flags"
	make -j"$(nproc)" DOS_VORBIS_SETUP_CPPFLAGS="$setup_flags"
else
	make -j"$(nproc)"
fi
# Interrupt handler code may reach nothing outside its locked range.
if ! python3 "$src/backends/platform/dos/irqcheck.py" scummvm.exe; then
	echo "build-dos.sh: scummvm.exe failed the interrupt code check (irqcheck.py); not staged." >&2
	exit 1
fi
echo "build-dos.sh: $out/scummvm.exe ($edition) built and checked."
