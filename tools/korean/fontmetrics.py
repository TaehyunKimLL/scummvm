#!/usr/bin/env python3
"""Do a font's advances actually clear its glyphs?

If a glyph's ink reaches past its advance, the next character starts on top of
it. The SVFN metrics table carries both numbers, so this is answerable from the
file without rendering anything:

    metrics per glyph = advance, bearingX, ink width, reserved

A glyph overlaps the next when bearingX + inkWidth > advance.

Usage:
    python3 fontmetrics.py tools/korean/indy3kor/vj00.fnt [more.fnt ...]
"""
import struct
import sys
from collections import Counter

HEADER = "<4sHHBBHHBBBBHIII"
HEADER_SIZE = struct.calcsize(HEADER)


def report(path):
    data = open(path, "rb").read()
    if len(data) < HEADER_SIZE:
        print(f"{path}: too small")
        return

    (magic, ver, flags, bpp, shadow, codepage, count, cellW, cellH,
     ascent, _r0, _r1, metricsOff, dataOff, dataSize) = struct.unpack(
        HEADER, data[:HEADER_SIZE])

    if magic != b"SVFN":
        print(f"{path}: not an SVFN file")
        return

    proportional = bool(flags & 1)
    print(f"{path}")
    print(f"  cell {cellW}x{cellH}  bpp={bpp}  glyphs={count}  "
          f"codepage={codepage}  {'proportional' if proportional else 'fixed width'}")

    if not proportional or not metricsOff:
        print(f"  no metrics table - every glyph advances by the cell width ({cellW})")
        print()
        return

    overlap = []
    advances = Counter()
    widths = Counter()
    for i in range(count):
        off = metricsOff + i * 4
        if off + 4 > len(data):
            break
        advance, bearingX, inkW, _res = data[off:off + 4]
        advances[advance] += 1
        widths[inkW] += 1
        reach = bearingX + inkW
        if reach > advance:
            overlap.append((i, advance, bearingX, inkW, reach - advance))

    print(f"  advances seen: {sorted(advances)[:12]}{' ...' if len(advances) > 12 else ''}")
    print(f"  ink widths:    {sorted(widths)[:12]}{' ...' if len(widths) > 12 else ''}")

    if overlap:
        worst = sorted(overlap, key=lambda t: -t[4])[:6]
        print(f"  *** {len(overlap)} of {count} glyphs reach past their advance ***")
        print("      idx  advance  bearingX  inkW  overhang")
        for idx, adv, bx, iw, over in worst:
            print(f"      {idx:5d} {adv:8d} {bx:9d} {iw:5d} {over:9d}")
    else:
        print(f"  every glyph fits inside its advance")
    print()


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    for path in sys.argv[1:]:
        try:
            report(path)
        except OSError as exc:
            print(f"{path}: {exc}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
