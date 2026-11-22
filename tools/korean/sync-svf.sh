#!/bin/sh
# Copies the SVF C renderer from the svf-fonts-open submodule into graphics/svf and records the upstream commit,
# and the fallback face into dists/engine-data/hires_text.
#
#   tools/korean/sync-svf.sh            copy the files and rewrite graphics/svf/VERSION, and copy the fallback face
#                                       into dists/engine-data/hires_text
#   tools/korean/sync-svf.sh --check    exit 1 if graphics/svf differs from the submodule's checkout
set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
src="$root/dists/svf-fonts-open"
dst="$root/graphics/svf"
files="c/include/svf.h c/src/svf_internal.h c/src/svf_draw.c c/src/svf_face.c c/src/svf_jamo.c c/src/svf_scale.c c/src/svf_stream.c LICENSE"
# The engine's fallback face (DESIGN decision 3), copied into engine data with its notice.
faces="svf/dos/NEODG16A.SVF svf/dos/NEODGM.TXT"
facedst="$root/dists/engine-data/hires_text"

[ -f "$src/c/include/svf.h" ] || { echo "sync-svf: $src is empty: run git submodule update --init dists/svf-fonts-open" >&2; exit 1; }
git -C "$src" rev-parse HEAD >/dev/null 2>&1 || { echo "sync-svf: $src is not a git checkout of svf-fonts-open" >&2; exit 1; }
if [ -n "$(git -C "$src" status --porcelain -- c/include c/src LICENSE $faces)" ]; then
	echo "sync-svf: the library in $src has local changes; commit them upstream first" >&2
	exit 1
fi
commit=$(git -C "$src" rev-parse HEAD)
date=$(git -C "$src" log -1 --format=%cI HEAD)

if command -v sha256sum >/dev/null 2>&1; then
	sha256() { sha256sum "$1" | cut -d' ' -f1; }
else
	sha256() { shasum -a 256 "$1" | cut -d' ' -f1; }
fi

version() {
	echo "svf-fonts-open C renderer (SVFN v3), vendored by tools/korean/sync-svf.sh"
	echo "url: $(git -C "$root" config -f .gitmodules submodule.dists/svf-fonts-open.url)"
	echo "commit: $commit"
	echo "date: $date"
	echo "build: configure adds -DSVF_JMAP=1 -DSVF_JSET_BIG=1 -DSVF_JMAP_RECOG=0 to DEFINES"
	echo "files (sha256, name):"
	for f in $files; do
		echo "$(sha256 "$src/$f")  $(basename "$f")"
	done
}

case "${1:-}" in
"")
	mkdir -p "$dst"
	for f in $files; do
		cp "$src/$f" "$dst/$(basename "$f")"
	done
	mkdir -p "$facedst"
	for f in $faces; do
		cp "$src/$f" "$facedst/$(basename "$f")"
	done
	version > "$dst/VERSION"
	echo "sync-svf: graphics/svf is svf-fonts-open $commit"
	;;
--check)
	bad=0
	for f in $files; do
		cmp -s "$src/$f" "$dst/$(basename "$f")" || { echo "sync-svf: graphics/svf/$(basename "$f") differs from $f" >&2; bad=1; }
	done
	for f in $faces; do
		cmp -s "$src/$f" "$facedst/$(basename "$f")" || { echo "sync-svf: $facedst/$(basename "$f") differs from $f" >&2; bad=1; }
	done
	version | cmp -s - "$dst/VERSION" || { echo "sync-svf: graphics/svf/VERSION does not name $commit" >&2; bad=1; }
	[ "$bad" = 0 ] && echo "sync-svf: graphics/svf matches svf-fonts-open $commit"
	exit "$bad"
	;;
*)
	echo "usage: $0 [--check]" >&2
	exit 2
	;;
esac
