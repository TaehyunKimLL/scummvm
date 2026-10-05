#!/bin/bash
# Configure and build the DOS port out of tree, then stage dist/dos.
# A build directory configured with other flags or another SDL3 is
# configured again (see config_current below).
# Usage: backends/platform/dos/build-dos.sh [sci|scumm] [extra configure args]
#   sci (default): build-dos/,       engines/sci (not sci32)             -> dist/dos/SCI.EXE
#   scumm: build-dos-scumm/, engines/scumm (not scumm_7_8, he), FLAC + Tremor -> dist/dos/SCUMM.EXE
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
# The trimmed SDL3 build (sdl3-build.txt): SDL3_DOS_MIN overrides where it is.
export SDL3_DOS="${SDL3_DOS_MIN:-$HOME/opt/sdl3-dos-min}"
export PKG_CONFIG_LIBDIR="$SDL3_DOS/lib/pkgconfig"
# SDL3 must carry sdl3-irq-code.patch: without it its Sound Blaster handler's
# code is not locked, and the link would fail late on DOS_IRQCodeChecked.
sdl_lib="$SDL3_DOS/lib/libSDL3.a"
if [ ! -f "$sdl_lib" ]; then
	echo "build-dos.sh: no SDL3 for DOS at $sdl_lib (set SDL3_DOS_MIN)." >&2
	echo "  Build SDL3 as backends/platform/dos/sdl3-build.txt says." >&2
	exit 1
fi
if ! "$DJGPP_PREFIX/bin/i586-pc-msdosdjgpp-nm" "$sdl_lib" 2>/dev/null | grep -q ' [TDR] _DOS_IRQCodeChecked$'; then
	echo "build-dos.sh: $sdl_lib lacks the dos-irq-lock patch (no DOS_IRQCodeChecked)." >&2
	echo "  Rebuild SDL3 as backends/platform/dos/sdl3-build.txt says (with sdl3-irq-code.patch)." >&2
	exit 1
fi
# And sdl3-cpuid.patch: without it SDL_Init() runs CPUID on a 486 that has
# none and dies with an invalid opcode; the link would fail on DOS_CPUIDChecked.
if ! "$DJGPP_PREFIX/bin/i586-pc-msdosdjgpp-nm" "$sdl_lib" 2>/dev/null | grep -q ' [TDR] _DOS_CPUIDChecked$'; then
	echo "build-dos.sh: $sdl_lib lacks the CPUID fix (no DOS_CPUIDChecked)." >&2
	echo "  Rebuild SDL3 as backends/platform/dos/sdl3-build.txt says (with sdl3-cpuid.patch)." >&2
	exit 1
fi
# And sdl3-sb-shutdown.patch: without it, closing the Sound Blaster waits
# forever for an audio thread whose card sends no interrupts (a BLASTER D
# that is not the card's DMA channel), and the program never exits.
if ! "$DJGPP_PREFIX/bin/i586-pc-msdosdjgpp-nm" "$sdl_lib" 2>/dev/null | grep -q ' [TDR] _DOS_SBShutdownChecked$'; then
	echo "build-dos.sh: $sdl_lib lacks the Sound Blaster shutdown fix (no DOS_SBShutdownChecked)." >&2
	echo "  Rebuild SDL3 as backends/platform/dos/sdl3-build.txt says (with sdl3-sb-shutdown.patch)." >&2
	exit 1
fi
# And sdl3-sb-open-fail.patch: without it, a Sound Blaster that cannot be
# opened (an SB16 whose BLASTER has no H) kills the program with an exception
# instead of leaving it without digital sound.
if ! "$DJGPP_PREFIX/bin/i586-pc-msdosdjgpp-nm" "$sdl_lib" 2>/dev/null | grep -q ' [TDR] _DOS_SBOpenFailChecked$'; then
	echo "build-dos.sh: $sdl_lib lacks the Sound Blaster open-failure fix (no DOS_SBOpenFailChecked)." >&2
	echo "  Rebuild SDL3 as backends/platform/dos/sdl3-build.txt says (with sdl3-sb-open-fail.patch)." >&2
	exit 1
fi
# And sdl3-sb-probe.patch: without it a Sound Blaster wired to another 8-bit
# DMA channel than BLASTER's D plays nothing, and nothing says why.
if ! "$DJGPP_PREFIX/bin/i586-pc-msdosdjgpp-nm" "$sdl_lib" 2>/dev/null | grep -q ' [TDR] _DOS_SBProbeChecked$'; then
	echo "build-dos.sh: $sdl_lib lacks the Sound Blaster DMA probe (no DOS_SBProbeChecked)." >&2
	echo "  Rebuild SDL3 as backends/platform/dos/sdl3-build.txt says (with sdl3-sb-probe.patch)." >&2
	exit 1
fi
# SCUMM.EXE plays compressed speech and CD tracks (the Ultimate Talkie
# editions: FLAC MONKEY.SOF, Vorbis MONKEY2.SOG, FLAC tracks) with the
# libraries build-deps.sh builds; SCI.EXE links none.
codecs="${DOS_CODECS:-$HOME/opt/codecs-dos}"
if [ "$edition" = scumm ]; then
	out="$src/build-dos-scumm"
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
else
	out="$src/build-dos"
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
	--disable-detection-full --disable-gui --disable-translation --enable-release)
mkdir -p "$out" "$src/dist/dos"
cd "$out"
# Configure again when there is no config.mk, when extra arguments are
# given, or when the one there was made with other flags or another SDL3
# (its SAVED_CONFIGFLAGS must start with conf_args - extra arguments of an
# earlier run may follow - and SAVED_PKG_CONFIG_LIBDIR must be this SDL3's).
# Otherwise make's own configure.stamp rule would rerun configure with the
# stale flags and SDL3 of the old config.mk after a change to configure.
config_current() {
	[ -f config.mk ] || return 1
	local saved pkg
	saved="$(sed -n 's/^SAVED_CONFIGFLAGS *:= *//p' config.mk)"
	pkg="$(sed -n 's/^SAVED_PKG_CONFIG_LIBDIR *:= *//p' config.mk)"
	case "$saved" in
		"${conf_args[*]}"|"${conf_args[*]} "*) ;;
		*) echo "build-dos.sh: $out/config.mk has other configure flags; configuring again." >&2; return 1 ;;
	esac
	if [ "$pkg" != "$PKG_CONFIG_LIBDIR" ]; then
		echo "build-dos.sh: $out/config.mk uses another SDL3 ($pkg); configuring again." >&2
		return 1
	fi
}
if [ -n "$*" ] || ! config_current; then
	"$src/configure" "${conf_args[@]}" "$@"
fi
if ! config_current || ! grep -q '^DISABLE_GUI = 1$' config.mk; then
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
make -j"$(nproc)"
# Interrupt handler code may reach nothing outside its locked range.
if ! python3 "$src/backends/platform/dos/irqcheck.py" scummvm.exe; then
	echo "build-dos.sh: scummvm.exe failed the interrupt code check (irqcheck.py); not staged." >&2
	exit 1
fi
cp scummvm.exe "$src/dist/dos/$exe"
if [ "$edition" = scumm ]; then
	# The codecs' BSD licences ask for their notices next to the program.
	cp "$codecs/share/licenses/FLAC.TXT" "$src/dist/dos/FLAC.TXT"
	cat "$codecs/share/licenses/OGG.TXT" "$codecs/share/licenses/TREMOR.TXT" > "$src/dist/dos/VORBIS.TXT"
fi
cp "$CWSDPMI_EXE" "$src/dist/dos/CWSDPMI.EXE"
# A fresh DATA: a map or font removed from dists must not stay behind from an earlier build.
rm -rf "$src/dist/dos/DATA"
mkdir -p "$src/dist/dos/DATA" && cp "$src"/dists/engine-data/hires_text/dos/* "$src/dist/dos/DATA/"
cp "$src/dists/engine-data/encoding.dat" "$src/dist/dos/DATA/ENCODING.DAT"
"$src/backends/platform/dos/launcher/build-play.sh" >/dev/null
ls -la "$src/dist/dos"
