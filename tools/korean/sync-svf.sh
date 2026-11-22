#!/bin/sh
# Copies the SVF C renderer from the svf-fonts-open submodule into graphics/svf and records the upstream commit.
#
#   tools/korean/sync-svf.sh            copy the files and rewrite graphics/svf/VERSION
#   tools/korean/sync-svf.sh --check    exit 1 if graphics/svf differs from the submodule's checkout
set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
src="$root/dists/svf-fonts-open"
dst="$root/graphics/svf"
files="c/include/svf.h c/src/svf_internal.h c/src/svf_draw.c c/src/svf_face.c c/src/svf_jamo.c c/src/svf_scale.c c/src/svf_stream.c LICENSE"

[ -f "$src/c/include/svf.h" ] || { echo "sync-svf: $src is empty: run git submodule update --init dists/svf-fonts-open" >&2; exit 1; }
if [ -n "$(git -C "$src" status --porcelain -- c/include c/src LICENSE)" ]; then
	echo "sync-svf: the library in $src has local changes; commit them upstream first" >&2
	exit 1
fi
commit=$(git -C "$src" rev-parse HEAD)
date=$(git -C "$src" log -1 --format=%cI HEAD)

version() {
	echo "svf-fonts-open C renderer (SVFN v3), vendored by tools/korean/sync-svf.sh"
	echo "url: $(git -C "$root" config -f .gitmodules submodule.dists/svf-fonts-open.url)"
	echo "commit: $commit"
	echo "date: $date"
	echo "build: configure adds -DSVF_JMAP=1 -DSVF_JSET_BIG=1 -DSVF_JMAP_RECOG=0 to DEFINES"
	echo "files (sha256, name):"
	for f in $files; do
		echo "$(sha256sum "$src/$f" | cut -d' ' -f1)  $(basename "$f")"
	done
}

case "${1:-}" in
"")
	mkdir -p "$dst"
	for f in $files; do
		cp "$src/$f" "$dst/$(basename "$f")"
	done
	version > "$dst/VERSION"
	echo "sync-svf: graphics/svf is svf-fonts-open $commit"
	;;
--check)
	bad=0
	for f in $files; do
		cmp -s "$src/$f" "$dst/$(basename "$f")" || { echo "sync-svf: graphics/svf/$(basename "$f") differs from $f" >&2; bad=1; }
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
