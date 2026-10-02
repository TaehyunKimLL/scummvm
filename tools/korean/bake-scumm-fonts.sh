#!/bin/bash
# usage: bake-scumm-fonts.sh PLAN.TSV GAMEDIR OUTDIR CHARS [EXTRA_CHARS_FROM] [EXTRA_LIMIT]
# (see SCUMM_FONTS.md)
# The ttf field of the plan may start with $FONTS (written literally); it is replaced by the
# FONTS environment variable, default ~/scummvm-i18n/fonts. Nothing else is expanded.
#
# EXTRA_CHARS_FROM, if given, is an extra --chars-from input alongside CHARS - typically the
# map itself (mkfont.py already reads a *.map's missing= and [glyphs] absolute u+XXXX targets,
# "*.map (hires_text INI 의 missing= 과 [glyphs] 절대 코드)" in its --chars-from help), so a
# remap target the map just gained is requested from the face without editing the game's own
# chars-from text. EXTRA_LIMIT, if given, is appended to the hardcoded
# --limit ascii,ksx1001-nohanja so that target survives the limit too (a remap target such as
# u+2026 is outside both named ranges). Both are omitted by default, which reproduces the
# previous four-argument invocation byte for byte.
#
# A plan line may carry a seventh field, a --limit of its own that replaces
# ascii,ksx1001-nohanja for that line only (EXTRA_LIMIT is still appended): for a face that
# lacks a symbol the bundle's non-text records decode to, which --require would otherwise
# refuse although no text ever draws it.
set -e
FONTS="${FONTS:-$HOME/scummvm-i18n/fonts}"
here="$(cd "$(dirname "$0")" && pwd)"

plan="$1"
gamedir="$2"
outdir="$3"
chars="$4"
extra_chars_from="$5"
extra_limit="$6"

if [ -z "$plan" ] || [ -z "$gamedir" ] || [ -z "$outdir" ] || [ -z "$chars" ]; then
	echo "usage: bake-scumm-fonts.sh PLAN.TSV GAMEDIR OUTDIR CHARS [EXTRA_CHARS_FROM] [EXTRA_LIMIT]" >&2
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
while IFS=$'\t' read -r name charset ttf size bpp ascent row_limit || [ -n "$name" ]; do
	n=$((n + 1))
	trimmed="${name#"${name%%[![:space:]]*}"}"
	if [ -z "$trimmed" ] || [ "${trimmed:0:1}" = "#" ]; then
		continue
	fi
	if [ -z "$name" ] || [ -z "$charset" ] || [ -z "$ttf" ] || [ -z "$size" ] || [ -z "$bpp" ]; then
		echo "bake-scumm-fonts: $plan line $n: expected 6 tab-separated fields (and an optional seventh, a limit)" >&2
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
	key="$ttf|$size|$bpp|$cell_w|$cell_h|${ascent:--}|$row_limit"
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

	chars_from_args=("$chars")
	if [ -n "$extra_chars_from" ]; then
		chars_from_args+=("$extra_chars_from")
	fi
	limit="ascii,ksx1001-nohanja"
	if [ -n "$row_limit" ]; then
		limit="$row_limit"
	fi
	if [ -n "$extra_limit" ]; then
		limit="$limit,$extra_limit"
	fi

	echo "== $name: charset $charset ($korfont: ${w}x${h} -> cell ${cell_w}x${cell_h}), $ttf ${size}px ${bpp}bpp =="
	python3 "$here/mkfont.py" "$ttf" "$out" --size "$size" --cell "$cell_h" --width "$cell_w" \
		--bpp "$bpp" --clip-cell "${ascent_args[@]}" --unicode ascii \
		--chars-from "${chars_from_args[@]}" --limit "$limit" "${require_args[@]}"

	baked[$key]="$out"
done < "$plan"
