#!/bin/bash
# Build what SCUMM.EXE links for compressed audio.
# Usage: backends/platform/dos/build-deps.sh [codecs]
#   codecs: libFLAC 1.4.3, libogg 1.3.5 and Tremor (integer Vorbis) for DJGPP,
#           static, -O2 -march=i586 -mtune=pentium (i486 with DOS_MARCH=i486), no asm/SSE -> $DOS_CODECS
#           (default ~/opt/codecs-dos). build-dos.sh scumm links them.
# (jamo's host tools for the Ultimate Talkie pack script, mkute.py, are not carried.)
# Sources come from $DOS_DEPS_SRC (default ~/opt/src/flac-dos); a missing
# tarball is downloaded. libFLAC and libogg are checked by SHA-256; Tremor has
# no release, so its tarball must be xiph's tremor at the commit below
# (git get-tar-commit-id).
set -e
what="${1:-codecs}"
src="${DOS_DEPS_SRC:-$HOME/opt/src/flac-dos}"
codecs="${DOS_CODECS:-$HOME/opt/codecs-dos}"
here="$(cd "$(dirname "$0")" && pwd)"
work="$(mktemp -d "${TMPDIR:-/tmp}/dos-deps.XXXXXX")"
trap 'rc=$?; [ $rc = 0 ] && rm -rf "$work"; exit $rc' EXIT

FLAC_TAR=flac-1.4.3.tar.xz
FLAC_SHA=6c58e69cd22348f441b861092b825e591d0b822e106de6eb0ee4d05d27205b70
FLAC_URL=https://downloads.xiph.org/releases/flac/flac-1.4.3.tar.xz
OGG_TAR=libogg-1.3.5.tar.xz
OGG_SHA=c4d91be36fc8e54deae7575241e03f4211eb102afb3fc0775fbbc1b740016705
OGG_URL=https://downloads.xiph.org/releases/ogg/libogg-1.3.5.tar.xz
TREMOR_TAR=tremor.tar.gz
TREMOR_COMMIT=820fb3237ea81af44c9cc468c8b4e20128e3e5ad
TREMOR_URL=https://gitlab.xiph.org/xiph/tremor/-/archive/$TREMOR_COMMIT/tremor-$TREMOR_COMMIT.tar.gz
# DOS_MARCH=i486 (build-dos.sh): the codecs a 486 build links are built for the 486 as well.
case "$DOS_MARCH" in
	"") CFLAGS_DOS="-O2 -march=i586 -mtune=pentium" ;;
	i486) CFLAGS_DOS="-O2 -march=i486 -mtune=pentium" ;;
	*) echo "build-deps.sh: DOS_MARCH is empty or i486" >&2; exit 1 ;;
esac
TREMOR_OBJS="block codebook floor0 floor1 info mapping0 mdct registry res012 sharedbook synthesis vorbisfile window"

fetch() {	# tarball url
	if [ ! -f "$src/$1" ]; then
		mkdir -p "$src"
		curl -fL -o "$src/$1.part" "$2" && mv "$src/$1.part" "$src/$1"
	fi
}
check_sha() {	# tarball sha256
	echo "$2  $src/$1" | sha256sum -c --quiet - || { echo "build-deps.sh: $src/$1 has the wrong SHA-256; delete it and rerun" >&2; exit 1; }
}
unpack() {	# tarball dir
	mkdir -p "$work/$2"
	tar -xf "$src/$1" -C "$work/$2" --strip-components=1
}
fetch_tremor() {
	fetch "$TREMOR_TAR" "$TREMOR_URL"
	got="$(gzip -dc "$src/$TREMOR_TAR" | git get-tar-commit-id)" || got=none
	if [ "$got" != "$TREMOR_COMMIT" ]; then
		echo "build-deps.sh: $src/$TREMOR_TAR is tremor $got, not $TREMOR_COMMIT" >&2
		exit 1
	fi
}
run_logged() {	# log command...
	local log="$1"; shift
	if ! "$@" >>"$log" 2>&1; then
		tail -30 "$log" >&2
		echo "build-deps.sh: failed: $* (log $log, kept)" >&2
		exit 1
	fi
}

build_codecs() (
	source ~/opt/dos-dev/env.sh
	fetch "$FLAC_TAR" "$FLAC_URL"; check_sha "$FLAC_TAR" "$FLAC_SHA"
	fetch "$OGG_TAR" "$OGG_URL"; check_sha "$OGG_TAR" "$OGG_SHA"
	fetch_tremor
	rm -rf "$codecs"
	mkdir -p "$codecs/share/licenses"
	# libogg (Tremor's bitstream layer)
	unpack "$OGG_TAR" ogg
	( cd "$work/ogg" &&
	  run_logged "$work/ogg.log" ./configure --host=i586-pc-msdosdjgpp --prefix="$codecs" \
		--disable-shared --enable-static CFLAGS="$CFLAGS_DOS" &&
	  run_logged "$work/ogg.log" make -j"$(nproc)" &&
	  run_logged "$work/ogg.log" make install )
	cp "$work/ogg/COPYING" "$codecs/share/licenses/OGG.TXT"
	# libFLAC: the FLAC spike's configuration (no asm, no SSE/AVX, no Ogg FLAC, no C++, no programs)
	unpack "$FLAC_TAR" flac
	( cd "$work/flac" &&
	  run_logged "$work/flac.log" ./configure --host=i586-pc-msdosdjgpp --prefix="$codecs" \
		--enable-static --disable-shared --disable-ogg --disable-asm-optimizations --disable-sse \
		--disable-avx --disable-cpplibs --disable-programs --disable-examples --disable-doxygen-docs \
		--disable-xmms-plugin --disable-stack-smash-protection --disable-thorough-tests \
		--disable-oggtest --disable-rpath --disable-version-from-git --disable-multithreading \
		CFLAGS="$CFLAGS_DOS" &&
	  run_logged "$work/flac.log" make -j"$(nproc)" &&
	  run_logged "$work/flac.log" make install )
	cp "$work/flac/COPYING.Xiph" "$codecs/share/licenses/FLAC.TXT"
	# Tremor: its autotools do not know DJGPP; the library is these sources
	unpack "$TREMOR_TAR" tremor
	# Limit backward page reads on short Ogg clips; exact seeks are unchanged.
	patch -d "$work/tremor" -p1 --forward < "$here/patches/tremor-chunksize.patch"
	for f in $TREMOR_OBJS; do
		run_logged "$work/tremor.log" i586-pc-msdosdjgpp-gcc $CFLAGS_DOS \
			-DBYTE_ORDER=1234 -DLITTLE_ENDIAN=1234 -DBIG_ENDIAN=4321 \
			-I"$codecs/include" -c "$work/tremor/$f.c" -o "$work/tremor/$f.o"
	done
	run_logged "$work/tremor.log" i586-pc-msdosdjgpp-ar rcs "$codecs/lib/libvorbisidec.a" \
		$(for f in $TREMOR_OBJS; do echo "$work/tremor/$f.o"; done)
	mkdir -p "$codecs/include/tremor"
	cp "$work/tremor/ivorbiscodec.h" "$work/tremor/ivorbisfile.h" "$work/tremor/config_types.h" \
		"$codecs/include/tremor/"
	# The opt-in setup cache uses these private layouts from the same Tremor
	# source as libvorbisidec.a; codec_internal.h includes codebook.h.
	cp "$work/tremor/codec_internal.h" "$work/tremor/codebook.h" "$codecs/include/tremor/"
	cp "$work/tremor/COPYING" "$codecs/share/licenses/TREMOR.TXT"
	ls -l "$codecs/lib"/*.a
)

case "$what" in
	codecs) build_codecs ;;
	*) echo "usage: build-deps.sh [codecs]" >&2; exit 1 ;;
esac
