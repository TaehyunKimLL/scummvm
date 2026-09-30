#!/bin/bash
# usage: bake-scumm-fonts.sh PLAN.TSV GAMEDIR OUTDIR CHARS  (see SCUMM_FONTS.md)
# The ttf field of the plan may start with $FONTS (written literally); it is replaced by the
# FONTS environment variable, default ~/scummvm-i18n/fonts. Nothing else is expanded.
set -e
FONTS="${FONTS:-$HOME/scummvm-i18n/fonts}"
here="$(cd "$(dirname "$0")" && pwd)"

plan="$1"
gamedir="$2"
outdir="$3"
chars="$4"

if [ -z "$plan" ] || [ -z "$gamedir" ] || [ -z "$outdir" ] || [ -z "$chars" ]; then
	echo "usage: bake-scumm-fonts.sh PLAN.TSV GAMEDIR OUTDIR CHARS" >&2
	exit 1
fi
if [ ! -f "$plan" ]; then
	echo "bake-scumm-fonts: no plan at $plan" >&2
	exit 1
fi
if [ ! -f "$chars" ]; then
	echo "bake-scumm-fonts: no chars-from file at $chars" >&2
	exit 1
fi
mkdir -p "$outdir"

declare -A baked

n=0
while IFS=$'\t' read -r name charset ttf size bpp ascent || [ -n "$name" ]; do
	n=$((n + 1))
	trimmed="${name#"${name%%[![:space:]]*}"}"
	if [ -z "$trimmed" ] || [ "${trimmed:0:1}" = "#" ]; then
		continue
	fi
	if [ -z "$name" ] || [ -z "$charset" ] || [ -z "$ttf" ] || [ -z "$size" ] || [ -z "$bpp" ]; then
		echo "bake-scumm-fonts: $plan line $n: expected 6 tab-separated fields" >&2
		exit 1
	fi

	ttf="${ttf//\$FONTS/$FONTS}"

	korfont=$(printf '%s/korean%02d.fnt' "$gamedir" "$charset")
	if [ ! -f "$korfont" ]; then
		echo "bake-scumm-fonts: $plan line $n: no $korfont (charset $charset)" >&2
		exit 1
	fi
	read -r w h < <(od -An -tu1 -j2 -N2 "$korfont")
	cell_w=$((w * 2))
	cell_h=$((h * 2))

	out="$outdir/$name"
	key="$ttf|$size|$bpp|$cell_w|$cell_h|${ascent:--}"
	if [ -n "${baked[$key]}" ]; then
		echo "== $name: same as ${baked[$key]} ($key) - copying =="
		cp "${baked[$key]}" "$out"
		continue
	fi

	ascent_args=()
	if [ -n "$ascent" ] && [ "$ascent" != "-" ]; then
		ascent_args=(--ascent "$ascent")
	fi

	require_args=()
	case "$chars" in
	*.trs | *.TRS) require_args=(--require) ;;
	esac

	echo "== $name: charset $charset ($korfont: ${w}x${h} -> cell ${cell_w}x${cell_h}), $ttf ${size}px ${bpp}bpp =="
	python3 "$here/mkfont.py" "$ttf" "$out" --size "$size" --cell "$cell_h" --width "$cell_w" \
		--bpp "$bpp" --clip-cell "${ascent_args[@]}" --unicode ascii \
		--chars-from "$chars" --limit ascii,ksx1001-nohanja "${require_args[@]}"

	baked[$key]="$out"
done < "$plan"
