#!/usr/bin/env python3
"""What font cells does each game actually need?

The game lays text out on its ORIGINAL font's grid: Indy3 reports
_2byteWidth = 8 for a Hangul syllable, so each character gets 8 game pixels and
8 * scale on screen. A replacement whose cell is wider than that is overlapped
by the next character - a 24px cell at scale 2 has 16px of room and loses 8.

So the needed cell is not a matter of taste: it is height * scale, per font,
per scale. This reads the games' own korean*.fnt headers and prints the set of
cells that would fit, marking which ones we already have.

    python3 fontplan.py                 # every game folder
    python3 fontplan.py indy3kor        # one
"""
import os
import struct
import sys

GAMES = ["mi1kor", "mi2kor", "indy3kor", "indy4kor", "loomkor", "loomtowns",
         "zakkor", "mmkor", "digkor"]


def original_fonts(folder):
    """(name, height) for the game's own fonts, which define the grid."""
    out = []
    for name in sorted(os.listdir(folder)):
        if not name.startswith("korean") or not name.endswith(".fnt"):
            continue
        try:
            with open(os.path.join(folder, name), "rb") as fh:
                head = fh.read(4)
        except OSError:
            continue
        # Old format header is [version=2, shadow, width, height] - see
        # loadKorFont() in charset.cpp, which reads them in that order. The
        # width is what _2byteWidth becomes and therefore what sets the
        # advance; most fonts are square but not all (MI2's korean00 is 11x12,
        # The Dig's korean.fnt is 10x9).
        if len(head) >= 4 and head[:4] != b"SVFN" and head[0] == 2:
            out.append((name, head[2], head[3]))
    return out


def have_cells(folder):
    """Cell sizes we already have baked, as {cell: [names]}."""
    have = {}
    for name in sorted(os.listdir(folder)):
        if not name.endswith(".fnt"):
            continue
        try:
            with open(os.path.join(folder, name), "rb") as fh:
                head = fh.read(32)
        except OSError:
            continue
        if head[:4] != b"SVFN" or len(head) < 32:
            continue
        h = struct.unpack("<4sHHBBHHBBBBHIII", head)
        cell, glyphs = h[7], h[6]
        # Only the CJK sets matter for the grid; Latin companions are sized
        # against them separately.
        if glyphs > 1000:
            have.setdefault(cell, []).append(name)
    return have


def main():
    wanted = sys.argv[1:]
    base = os.path.expanduser("~/games")
    folders = wanted or [g for g in GAMES if os.path.isdir(os.path.join(base, g))]

    needed = {}   # cell -> [(game, font, height, scale)]

    for key in folders:
        folder = os.path.join(base, key) if not os.path.isabs(key) else key
        if not os.path.isdir(folder):
            print(f"{key}: no such folder")
            continue

        originals = original_fonts(folder)
        if not originals:
            print(f"{key}: no korean*.fnt - skipped")
            continue

        have = have_cells(folder)
        print(f"=== {key}")
        print(f"  the game's own fonts: "
              f"{', '.join(f'{n}={w}x{h}' for n, w, h in originals)}")
        print(f"  cells already baked:  "
              f"{', '.join(str(c) for c in sorted(have)) or '(none)'}")

        # The advance comes from the width, so that is what the replacement
        # cell has to match.
        heights = sorted({w for _, w, _ in originals})
        for scale in (2, 3):
            cells = [h * scale for h in heights]
            missing = [c for c in cells if c not in have]
            status = "ok" if not missing else \
                     f"need {', '.join(str(c) for c in sorted(set(missing)))}"
            print(f"  scale {scale}: cells {cells}  -> {status}")
            for h, c in zip(heights, cells):
                if c not in have:
                    needed.setdefault(c, []).append((key, h, scale))
        print()

    if not needed:
        print("Nothing to bake.")
        return 0

    print("=" * 60)
    print("Cells to bake:")
    for cell in sorted(needed):
        users = needed[cell]
        who = ", ".join(f"{g}({h}px@{s}x)" for g, h, s in users)
        print(f"  {cell:>3}x{cell:<3}  for {who}")

    print()
    print("Bake with tools/korean/bakecells.sh, which drives the docs repo's")
    print("mkfont.py against a TrueType face at each size.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
