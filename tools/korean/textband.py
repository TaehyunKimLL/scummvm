"""Find where the bright verb-name text actually sits, then profile that band.

Guessing the band by eye put it 10 pixels off and the profile came back empty,
which looks the same as "the text is not drawn".
"""
from PIL import Image
import sys

path = sys.argv[1] if len(sys.argv) > 1 else '/tmp/final_i4-multi-hr.png'
img = Image.open(path).convert('RGB')
W, H = img.size
px = img.load()


def bright_text(p):
    """Verb text is a light colour on the dark interface strip."""
    r, g, b = p
    return (r + g + b) > 330 and max(r, g, b) - min(r, g, b) < 120


rows = []
for y in range(H):
    n = sum(1 for x in range(W) if bright_text(px[x, y]))
    if n > 2:
        rows.append((y, n))

print(path.split('/')[-1])
if not rows:
    print('  no bright text rows found')
    raise SystemExit

# Group contiguous rows into bands.
bands, cur = [], [rows[0]]
for prev, item in zip(rows, rows[1:]):
    if item[0] - prev[0] <= 2:
        cur.append(item)
    else:
        bands.append(cur)
        cur = [item]
bands.append(cur)

for band in bands:
    y0, y1 = band[0][0], band[-1][0]
    total = sum(n for _, n in band)
    cols = [sum(1 for y in range(y0, y1 + 1) if bright_text(px[x, y]))
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
    xs = [a for a, _ in runs]
    print('  band y%3d-%3d  %5d px  %2d ink runs  x%s..%s'
          % (y0, y1, total, len(runs),
             min(xs) if xs else '-', max(b for _, b in runs) if runs else '-'))
