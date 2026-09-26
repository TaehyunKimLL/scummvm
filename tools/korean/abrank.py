"""Rank A/B frame pairs by how much they differ, and where.

A pair where the only difference is the text band is the one worth looking at;
a pair that differs everywhere caught the two runs at different moments and
proves nothing.
"""
from PIL import Image, ImageChops
import sys, glob, os

prefix = sys.argv[1]

rows = []
for on in sorted(glob.glob(prefix + '_on_t*.png')):
    tag = os.path.basename(on).rsplit('_', 1)[-1][:-4]
    off = prefix + '_off_' + tag + '.png'
    if not os.path.exists(off):
        continue

    a = Image.open(off).convert('RGB')
    b = Image.open(on).convert('RGB')
    if a.size != b.size:
        continue

    d = ImageChops.difference(a, b).convert('L')
    W, H = a.size
    px = d.load()

    total = 0
    band_rows = []
    for y in range(H):
        n = sum(1 for x in range(W) if px[x, y] > 16)
        total += n
        if n:
            band_rows.append((y, n))

    if not band_rows:
        rows.append((tag, 0, 0, '-', 'identical'))
        continue

    y0, y1 = band_rows[0][0], band_rows[-1][0]
    span = y1 - y0 + 1
    verdict = 'text band' if span < H * 0.4 else 'whole frame (different moment)'
    rows.append((tag, total, span, 'y%d-%d' % (y0, y1), verdict))

rows.sort(key=lambda r: -r[1])
print('%-6s %8s %6s %-12s %s' % ('frame', 'diff px', 'span', 'where', 'verdict'))
for tag, total, span, where, verdict in rows:
    print('%-6s %8d %6d %-12s %s' % (tag, total, span, where, verdict))
