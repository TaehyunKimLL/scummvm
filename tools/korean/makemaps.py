#!/usr/bin/env python3
"""Write one hi-res font map per game, from what the font files actually are.

Maps were being hand-written and drifting: i3-multi still pointed at a map in
the older TrueType format, so the legacy loader drew the text while the new
reader supplied only the scale - two systems laying out one screen, which looks
like a font bug and is not one.

This reads every .fnt in a game folder, groups the ones that form a numbered
set, and writes a map naming them. Choices are recorded in the file itself so
the next reader does not have to guess.

    python3 makemaps.py                 # every known game folder
    python3 makemaps.py ~/games/mi2kor  # just one
"""
import glob
import os
import re
import struct
import sys

HEADER = "<4sHHBBHHBBBBHIII"
HEADER_SIZE = struct.calcsize(HEADER)

# Which set to prefer when a folder carries several. Earlier wins.
# The reasoning, from measuring the files:
#   svfn*  the set baked to match each of the game's own charsets - the safe
#          default, since every charset the game switches to has a font
#   vj/lm  proportional sets; better looking, but only for games whose scripts
#          tolerate different advances (see hires_text_metrics)
#   jm/aa  alternative typefaces at the same sizes
#   uni*   1bpp stencil sets, for comparison against the anti-aliased ones
# "hr" first: those are baked by ~/games/bakecells.sh at exactly
# height * scale for this game, so they sit on the layout grid. The rest were
# baked by hand at whatever size and only fit by luck.
PREFERENCE = ["hr", "svfn", "vj", "lm", "jm", "aa", "jangmi", "uni24_", "uni"]

GAMES = {
    "mi2kor": ("monkey2", "Monkey Island 2"),
    "indy3kor": ("indy3", "Indiana Jones 3"),
    "indy4kor": ("atlantis", "Indiana Jones 4"),
    "loomkor": ("loom", "Loom"),
    "loomtowns": ("loom", "Loom (FM-Towns)"),
    "zakkor": ("zak", "Zak McKracken"),
    "mi1kor": ("monkey", "Monkey Island 1"),
    "mmkor": ("maniac", "Maniac Mansion"),
    "digkor": ("dig", "The Dig"),
}


def describe(path):
    """Read an SVFN header; None when the file is not one."""
    try:
        raw = open(path, "rb").read(HEADER_SIZE)
    except OSError:
        return None
    if len(raw) < HEADER_SIZE:
        return None

    (magic, ver, flags, bpp, shadow, codepage, count, cellW, cellH,
     ascent, _r0, _r1, metricsOff, dataOff, dataSize) = struct.unpack(HEADER, raw)
    if magic != b"SVFN":
        return None

    return {
        "cell": (cellW, cellH),
        "bpp": bpp,
        "glyphs": count,
        "codepage": codepage,
        "proportional": bool(flags & 1),
    }


def scan(folder):
    """Group the SVFN fonts in a folder into numbered sets and Latin extras."""
    sets = {}       # prefix -> {index: (name, info)}
    latin = {}      # prefix -> (name, info)
    unindexed = {}  # stem -> (name, info), CJK fonts with no number

    for path in sorted(glob.glob(os.path.join(folder, "*.fnt"))):
        info = describe(path)
        if not info:
            continue
        name = os.path.basename(path)
        stem = name[:-4]

        m = re.match(r"^([a-zA-Z_]+?)(\d+)$", stem)
        if m and info["glyphs"] > 1000:
            prefix, idx = m.group(1), int(m.group(2))
            sets.setdefault(prefix, {})[idx] = (name, info)
        elif info["glyphs"] <= 512:
            # A 256-glyph font is the Latin companion to a CJK set.
            latin[stem] = (name, info)
        elif info["glyphs"] > 1000:
            # A CJK font whose name carries no index: The Dig ships korean.fnt
            # and korean_g.fnt, which bake to hr0.fnt and hrg.fnt. hrg matched
            # neither branch above and vanished from the map silently - the
            # exact "loads zero fonts" failure this script exists to prevent.
            unindexed[stem] = (name, info)

    return sets, latin, unindexed


def pattern_for(prefix, indices):
    """The %0Nd pattern that reproduces this set's file names."""
    width = 2 if max(indices) < 100 else 3
    return f"{prefix}%0{width}d.fnt"


def original_heights(folder):
    """Heights of the game's own korean*.fnt, which define the layout grid.

    The engine reads these into _2byteWidth/_2byteHeight and the scripts lay
    text out on them, so they - not the replacement font - decide how much room
    each character gets. Byte 2 of the old format is the height.
    """
    heights = set()
    for name in sorted(os.listdir(folder)):
        if not name.startswith("korean") or not name.endswith(".fnt"):
            continue
        try:
            with open(os.path.join(folder, name), "rb") as fh:
                head = fh.read(4)
        except OSError:
            continue
        # The old format starts with a version byte of 2; SVFN files start
        # with the magic and are the replacements, not the originals.
        if len(head) >= 3 and head[:4] != b"SVFN" and head[0] == 2:
            heights.add(head[2])
    return heights


def write_map(folder, gameid, title):
    sets, latin, unindexed = scan(folder)
    if not sets and not unindexed:
        return None

    chosen = None
    for want in PREFERENCE:
        if want in sets:
            chosen = want
            break
    if chosen is None:
        chosen = sorted(sets)[0]

    members = sets[chosen]
    indices = sorted(members)

    # Whether the game selects a font by charset at all. v7 loadCJKFont() opens
    # a single "korean.fnt" - The Dig's own fonts are korean.fnt and
    # korean_g.fnt, with no index - so a %02d pattern there matches nothing and
    # the hi-res layer quietly loads zero fonts.
    indexed_names = any(
        re.match(r"^korean\d+\.fnt$", n)
        for n in os.listdir(folder)
    )
    first_info = members[indices[0]][1]
    pattern = pattern_for(chosen, indices)

    lines = []
    lines.append(f"# {title} - hi-res text map")
    lines.append("#")
    lines.append("# Generated by ~/games/makemaps.py from the font files themselves.")
    lines.append("# Edit freely; rerunning the script overwrites it.")
    lines.append("#")
    lines.append(f"# Fonts found in {os.path.basename(folder)}:")
    for prefix in sorted(sets):
        idx = sorted(sets[prefix])
        sample = sets[prefix][idx[0]][1]
        cells = sorted({f"{sets[prefix][i][1]['cell'][0]}x{sets[prefix][i][1]['cell'][1]}"
                        for i in idx})
        lines.append(f"#   {prefix + '*':<12} {len(idx):2d} files, cells {','.join(cells)}, "
                     f"{sample['bpp']}bpp, "
                     f"{'proportional' if sample['proportional'] else 'fixed width'}"
                     f"{'   <- selected' if prefix == chosen else ''}")
    # List these too. They used to be dropped without a word, so the comment
    # block looked like a full survey of the folder while omitting files
    # sitting right there.
    for stem in sorted(unindexed):
        name, info = unindexed[stem]
        lines.append(f"#   {name:<12}    (no index) cell "
                     f"{info['cell'][0]}x{info['cell'][1]}, {info['bpp']}bpp"
                     f"{'   <- used as single=' if not indexed_names and stem.endswith('0') else ''}")
    for stem, (name, info) in sorted(latin.items()):
        lines.append(f"#   {name:<12} {info['glyphs']} glyphs (Latin), "
                     f"{info['cell'][0]}x{info['cell'][1]}, {info['bpp']}bpp")
    lines.append("")

    # Comments go on their own line: the ini reader does not treat ';' as a
    # trailing comment, so "scale=3   ; because" parses as the literal string
    # "3   ; because" and the value is silently dropped.
    # The scale is not free: the game lays text out on the ORIGINAL font's
    # grid. Indy3's korean00.fnt is 8px high, so a Hangul syllable advances 8
    # game pixels and occupies 8*scale on screen. A replacement cell wider than
    # that is overlapped by the next character - a 24px cell at scale 2 gets 16
    # pixels of room and loses 8.
    #
    # So pick the scale that makes the replacement cell fit the game's own
    # grid, and say so when nothing fits exactly.
    orig = original_heights(folder)
    cell = first_info["cell"][0]

    # Derive the scale from the cell, not the other way round: a set baked at
    # height * 2 must run at 2x or it will not sit on the grid. Match the cell
    # against the game's own heights and take the multiplier that produced it.
    scale = None
    if orig:
        for want in (2, 3):
            if any(h * want == cell for h in orig):
                scale = want
                break
    if scale is None:
        # Nothing matches exactly, so fall back to the largest scale whose
        # grid can still hold the cell.
        if orig:
            base = min(orig)
            scale = 3 if base * 3 >= cell else 2
        else:
            scale = 3 if cell >= 30 else 2

    lines.append("[hires]")
    lines.append("enabled=true")
    if orig:
        base = min(orig)
        lines.append(f"# the game's own fonts are {sorted(orig)} px high; the "
                     f"smallest ({base}) sets the grid")
        room = base * scale
        if room < cell:
            lines.append(f"# WARNING: {cell}px cell needs {cell} but the grid "
                         f"gives {room} - characters will overlap by "
                         f"{cell - room}px")
            lines.append(f"# a {room}x{room} font would fit exactly")
        else:
            lines.append(f"# {scale}x gives each character {room}px for a "
                         f"{cell}px cell")
    else:
        lines.append(f"# cells are {cell}px, so {scale}x keeps them near "
                     f"their native size")
    lines.append(f"scale={scale}")
    lines.append("# blend glyph coverage into the background")
    lines.append("alpha=true")
    lines.append("")

    lines.append("[bitmap]")
    # A game whose own fonts carry no charset index does not select by charset
    # either: v7 loadCJKFont() opens the single "korean.fnt". Naming a pattern
    # there matches nothing and the hi-res layer silently loads no fonts.
    if indexed_names:
        lines.append(f"multi={pattern}")
    else:
        lines.append(f"single={members[indices[0]][0]}")
    lines.append(f"glyphs={first_info['glyphs']}")
    lines.append("")

    # A Latin companion for the single-byte characters. The double-byte sets
    # are indexed by a code page that has no Latin glyphs, so without this the
    # menu and the Latin half of mixed lines stay at the game's own 8px size
    # while the Hangul around them is replaced - the two halves of one line
    # end up at different qualities.
    #
    # Match the cell height to the chosen CJK set so both sit on one baseline.
    want_cell = first_info["cell"][1]
    best = None
    for stem, (name, info) in sorted(latin.items()):
        # Prefer an exact cell match, then the nearest, and among equals the
        # one whose name shares the CJK set's prefix (they were baked together).
        # An exact match matters more than being baked together: a Latin face
        # at a different cell sits on a different baseline.
        delta = abs(info["cell"][1] - want_cell)
        related = 0 if name.startswith(chosen.rstrip("0123456789")) else 1
        key = (delta, related, name)
        if best is None or key < best[0]:
            best = (key, name, info)

    # Prefer a per-charset set: the cell can differ per charset (MI2 has five)
    # and one Latin face cannot sit on all of those baselines.
    indexed = {}
    for stem, (name, info) in latin.items():
        digits = "".join(c for c in os.path.splitext(name)[0] if c.isdigit())
        if name.startswith("hrlat") and digits:
            indexed[int(digits)] = (name, info)

    if len(indexed) > 1 and set(indexed) == set(indices):
        sample = indexed[min(indexed)][1]
        lines.append("[latin]")
        lines.append(f"# one face per charset, matching each CJK cell")
        lines.append(f"bitmap=hrlat%02d.fnt")
        if sample["proportional"]:
            lines.append("# these have per-glyph metrics; set metrics=font to use them")
        lines.append("")
    elif best is not None:
        _, latin_name, latin_info = best
        lines.append("[latin]")
        lines.append(f"# {latin_info['cell'][0]}x{latin_info['cell'][1]} cell against "
                     f"the {first_info['cell'][0]}x{want_cell} CJK set")
        lines.append(f"bitmap={latin_name}")
        if latin_info["proportional"]:
            lines.append("# this face has per-glyph metrics; set metrics=font to use them")
        lines.append("")

    lines.append("[render]")
    if first_info["proportional"]:
        lines.append("# This set carries per-glyph metrics. metrics=font lets it space")
        lines.append("# itself, but the game picks line breaks and speech-bubble sizes")
        lines.append("# from its own widths, so the default stays with the game.")
        lines.append("# set to 'font' to use the font's own advances")
        lines.append("metrics=game")
    else:
        lines.append("# This set is fixed width, so metrics=font would change nothing.")
        lines.append("metrics=game")

    text = "\n".join(lines) + "\n"
    out = os.path.join(folder, "hires_text.map")
    with open(out, "w") as fh:
        fh.write(text)
    return out, chosen, pattern, len(members), scale


def main():
    folders = sys.argv[1:]
    if not folders:
        base = os.path.expanduser("~/games")
        folders = [os.path.join(base, name) for name in GAMES
                   if os.path.isdir(os.path.join(base, name))]

    for folder in folders:
        folder = os.path.abspath(os.path.expanduser(folder))
        key = os.path.basename(folder)
        gameid, title = GAMES.get(key, (key, key))
        result = write_map(folder, gameid, title)
        if result is None:
            print(f"{key}: no numbered SVFN set found - skipped")
            continue
        out, chosen, pattern, count, scale = result
        print(f"{key}: {chosen}* ({count} fonts, {pattern}), scale {scale} -> {out}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
