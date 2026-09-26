"""Isolate subtitle text by colour, then report its glyph height.

A brightness threshold over a scene also catches sky and highlights, so the
"line" it reports spans the whole crop and the height is meaningless. SCUMM
subtitles are drawn in one flat palette colour, so sampling the dominant
non-background colour in the band separates them cleanly.
"""
from PIL import Image
from collections import Counter
import sys

path = sys.argv[1]
Y0, Y1 = int(sys.argv[2]), int(sys.argv[3])

img = Image.open(path).convert('RGB')
W, _ = img.size
px = img.load()

# Which colours appear in this band, brightest first.
counts = Counter()
for y in range(Y0, Y1):
    for x in range(W):
        counts[px[x, y]] += 1

print('%s  band y%d-%d' % (path.split('/')[-1], Y0, Y1))
print('  dominant colours:')
for c, n in counts.most_common(6):
    print('    %-16s %6d px  sum=%d' % (str(c), n, sum(c)))

# Text is a bright, near-neutral or single-hue colour used for a few hundred
# pixels - not the thousands a background occupies.
cands = [(c, n) for c, n in counts.items()
         if sum(c) > 380 and 40 < n < W * (Y1 - Y0) * 0.25]
if not cands:
    print('  no text-like colour found')
    raise SystemExit

cands.sort(key=lambda t: -t[1])
text_colour, n = cands[0]
print('  -> text colour %s (%d px)' % (str(text_colour), n))


def is_text(p):
    return all(abs(a - b) <= 24 for a, b in zip(p, text_colour))


rows = [(y, sum(1 for x in range(W) if is_text(px[x, y])))
        for y in range(Y0, Y1)]
used = [(y, c) for y, c in rows if c]
if not used:
    print('  no rows')
    raise SystemExit

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
    cols = [sum(1 for y in range(y0, y1 + 1) if is_text(px[x, y]))
            for x in range(W)]
    runs, start = [], None
    for x, c in enumerate(cols):
        if c and start is None:
            start = x
        elif not c and start is not None:
            runs.append((start, x - 1))
            start = None
    if start is not None:
        runs.append((start, W - 1))
    widths = [b - a + 1 for a, b in runs]
    print('  line y%3d-%3d  height %2d  %2d runs  widths %s'
          % (y0, y1, y1 - y0 + 1, len(runs),
             ' '.join(str(w) for w in widths[:16])))
