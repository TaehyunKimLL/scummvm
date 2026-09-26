#!/usr/bin/env python3
"""Does scaling the advance down to game pixels make glyphs collide?

The engine lays text out in unscaled game pixels, but the replacement font's
metrics are in the scaled font's pixels, so advanceFor() divides by the scale.
That division is lossy, and the question is which way to round.

Rounding down loses up to (scale-1) pixels per character. Once the engine
multiplies the position back up, the next glyph starts that much too early -
and since ink can already fill the whole advance, characters overlap.

Rounding up costs at most (scale-1) pixels of extra spacing and cannot overlap.

Usage:
    python3 advancecheck.py tools/korean/indy3kor/vj00.fnt [scale]
"""
import struct
import sys
from collections import Counter

HEADER = "<4sHHBBHHBBBBHIII"
HEADER_SIZE = struct.calcsize(HEADER)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    path = sys.argv[1]
    scale = int(sys.argv[2]) if len(sys.argv) > 2 else 2

    data = open(path, "rb").read()
    (magic, _ver, flags, _bpp, _shadow, _cp, count, cellW, _cellH,
     _asc, _r0, _r1, metricsOff, _do, _ds) = struct.unpack(HEADER, data[:HEADER_SIZE])

    if magic != b"SVFN" or not (flags & 1) or not metricsOff:
        print(f"{path}: not a proportional SVFN font")
        return 1

    print(f"{path}: cell {cellW}px, scale {scale}")
    print()

    down_bad = []
    up_bad = []
    fixed_bad = []
    up_slack = Counter()

    for i in range(count):
        off = metricsOff + i * 4
        if off + 4 > len(data):
            break
        advance, bearingX, inkW, _res = data[off:off + 4]
        reach = bearingX + inkW          # rightmost ink column, in font pixels

        # What the engine will actually step, in font pixels, after the
        # division and the engine's own multiply back up.
        down = (advance // scale) * scale
        up = ((advance + scale - 1) // scale) * scale

        # The shipped fix also widens the advance to clear ink that was baked
        # past it, before rounding.
        fixed_adv = max(advance, reach)
        fixed = ((fixed_adv + scale - 1) // scale) * scale

        if reach > down:
            down_bad.append((i, advance, reach, reach - down))
        if reach > up:
            up_bad.append((i, advance, reach, reach - up))
        if reach > fixed:
            fixed_bad.append((i, advance, reach, reach - fixed))
        up_slack[up - advance] += 1

    print(f"  rounding DOWN: {len(down_bad)} of {count} glyphs overlap the next")
    if down_bad:
        worst = sorted(down_bad, key=lambda t: -t[3])[:5]
        print("      idx  advance  ink reach  overlap px")
        for idx, adv, reach, over in worst:
            print(f"      {idx:5d} {adv:8d} {reach:10d} {over:11d}")

    print(f"  rounding UP:   {len(up_bad)} of {count} glyphs overlap the next")
    if up_bad:
        worst = sorted(up_bad, key=lambda t: -t[3])[:5]
        print("      idx  advance  ink reach  overlap px")
        for idx, adv, reach, over in worst:
            print(f"      {idx:5d} {adv:8d} {reach:10d} {over:11d}")

    print(f"  shipped fix (widen to ink, then round up): "
          f"{len(fixed_bad)} of {count} overlap")
    if fixed_bad:
        worst = sorted(fixed_bad, key=lambda t: -t[3])[:5]
        print("      idx  advance  ink reach  overlap px")
        for idx, adv, reach, over in worst:
            print(f"      {idx:5d} {adv:8d} {reach:10d} {over:11d}")

    print()
    print(f"  extra spacing from rounding up: {dict(sorted(up_slack.items()))}")
    print(f"  (in font pixels; divide by {scale} for game pixels)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
