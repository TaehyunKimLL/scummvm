#!/bin/bash
# Usage: check-tremor-cache.sh <codec prefix>
# build-dos.sh's guard for the SCUMM edition's Vorbis setup cache (MI2 speech).
# Prints warnings to stderr and always exits 0: MI1 builds need no cache.
#   - $prefix/include/tremor/codec_internal.h absent: the cache is compiled out
#     and every MI2 line opens cold.
#   - ivorbisfile.h with a CHUNKSIZE above 4096 (what patches/tremor-chunksize.patch
#     sets; stock Tremor has 65535): every open reads 64 KB and the stream
#     buffers cost more.
codecs="$1"
inc="$codecs/include/tremor"
if [ ! -f "$inc/codec_internal.h" ]; then
	{
		echo "build-dos.sh: *****************************************************************"
		echo "build-dos.sh: WARNING: Vorbis setup cache OFF: every MI2 line will open cold (~150 ms)."
		echo "build-dos.sh:   $inc/codec_internal.h is missing from this codec prefix."
		echo "build-dos.sh:   Rerun build-deps.sh codecs with a private prefix (DOS_CODECS=...), see README."
		echo "build-dos.sh:   MI1 does not need the cache; the build continues."
		echo "build-dos.sh: *****************************************************************"
	} >&2
	exit 0
fi
size="$(sed -n 's/^#define CHUNKSIZE[[:space:]]\{1,\}\([0-9]\{1,\}\).*/\1/p' "$inc/ivorbisfile.h" 2>/dev/null | head -n 1)"
if [ -z "$size" ]; then
	echo "build-dos.sh: WARNING: no CHUNKSIZE found in $inc/ivorbisfile.h; cannot tell whether tremor-chunksize.patch is applied." >&2
elif [ "$size" -gt 4096 ]; then
	{
		echo "build-dos.sh: *****************************************************************"
		echo "build-dos.sh: WARNING: $inc/ivorbisfile.h has CHUNKSIZE $size; tremor-chunksize.patch sets 4096."
		echo "build-dos.sh:   Rerun build-deps.sh codecs with a private prefix (DOS_CODECS=...), see README."
		echo "build-dos.sh: *****************************************************************"
	} >&2
fi
exit 0
