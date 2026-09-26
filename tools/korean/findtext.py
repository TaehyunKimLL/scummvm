"""Find every row of text-coloured pixels in a capture, whole frame.

Used when two captures do not line up: the copyright line was at y18 in one
and further down in the other, and a band picked for one returned nothing for
the other - which looks like "the text is missing" rather than "the text
moved".
"""
from PIL import Image
from collections import Counter
import sys

path = sys.argv[1]
img = Image.open(path).convert('RGB')
W, H = img.size
px = img.load()

counts = Counter()
for y in range(H):
    for x in range(W):
        counts[px[x, y]] += 1

# Text colours: bright, and used for a modest number of pixels.
cands = [(c, n) for c, n in counts.items()
         if sum(c) > 380 and 60 < n < W * H * 0.06]
cands.sort(key=lambda t: -t[1])

print('%s  %dx%d' % (path.split('/')[-1], W, H))
if not cands:
    print('  no text-like colour')
    raise SystemExit

for colour, total in cands[:3]:
    def is_text(p, c=colour):
        return all(abs(a - b) <= 24 for a, b in zip(p, c))

    rows = [(y, sum(1 for x in range(W) if is_text(px[x, y]))) for y in range(H)]
    used = [(y, n) for y, n in rows if n > 1]
    if not used:
        continue

    lines, cur = [], [used[0]]
    for prev, item in zip(used, used[1:]):
        if item[0] - prev[0] <= 3:
            cur.append(item)
        else:
            lines.append(cur)
            cur = [item]
    lines.append(cur)

    print('  colour %-16s %5d px' % (str(colour), total))
    for ln in lines:
        y0, y1 = ln[0][0], ln[-1][0]
        cols = [sum(1 for y in range(y0, y1 + 1) if is_text(px[x, y]))
                for x in range(W)]
        runs, start = [], None
        for x, n in enumerate(cols):
            if n and start is None:
                start = x
            elif not n and start is not None:
                runs.append((start, x - 1))
                start = None
        if start is not None:
            runs.append((start, W - 1))
        if not runs:
            continue
        gaps = [runs[i + 1][0] - runs[i][1] - 1 for i in range(len(runs) - 1)]
        print('    y%3d-%3d  h%2d  %2d runs  x%d..%d  gaps %s'
              % (y0, y1, y1 - y0 + 1, len(runs), runs[0][0], runs[-1][1],
                 ' '.join(str(g) for g in gaps[:12])))
