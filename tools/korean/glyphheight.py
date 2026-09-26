"""Measure the drawn glyph height in a band, per capture.

The cell size a font was baked at is not necessarily the size the game asked
for: the engine picks the charset, and a charset can be remapped (MI2's
charset 6 becomes font 0) so the grid it hands over belongs to a different
font. Measuring the ink is the only way to know what actually landed.
"""
from PIL import Image
import sys

path = sys.argv[1]
Y0, Y1 = int(sys.argv[2]), int(sys.argv[3])

img = Image.open(path).convert('RGB')
W, _ = img.size
px = img.load()


def ink(p):
    """Text is brighter than the interface strip behind it."""
    r, g, b = p
    return (r + g + b) > 250


rows = []
for y in range(Y0, Y1):
    n = sum(1 for x in range(W) if ink(px[x, y]))
    rows.append((y, n))

used = [(y, n) for y, n in rows if n]
print('%s  band y%d-%d' % (path.split('/')[-1], Y0, Y1))
if not used:
    print('  no ink')
    raise SystemExit

# Group rows into text lines, so a two-line band reports each line.
lines, cur = [], [used[0]]
for prev, item in zip(used, used[1:]):
    if item[0] - prev[0] <= 2:
        cur.append(item)
    else:
        lines.append(cur)
        cur = [item]
lines.append(cur)

for ln in lines:
    y0, y1 = ln[0][0], ln[-1][0]
    total = sum(n for _, n in ln)
    cols = [sum(1 for y in range(y0, y1 + 1) if ink(px[x, y])) for x in range(W)]
    runs, start = [], None
    for x, n in enumerate(cols):
        if n and start is None:
            start = x
        elif not n and start is not None:
            runs.append((start, x - 1))
            start = None
    if start is not None:
        runs.append((start, W - 1))
    widths = [b - a + 1 for a, b in runs]
    print('  line y%3d-%3d  height %2d  %4d px  %2d runs  widths %s'
          % (y0, y1, y1 - y0 + 1, total, len(runs),
             ' '.join(str(w) for w in widths[:14])))
