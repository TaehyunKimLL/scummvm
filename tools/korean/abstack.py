"""Stack captures with labels for side-by-side reading.

Usage:
    abstack.py out.png [x0 y0 x1 y1] [zoom] -- label1 img1 label2 img2 ...

Two or three images in a chat are compared from memory; one image with the
panels stacked and labelled is not. The original (hi-res off) belongs in the
stack whenever the question is "is our layer drawing the right thing?" - two
builds that both have the feature only show which one regressed.
"""
from PIL import Image, ImageDraw
import sys

argv = sys.argv[1:]
if "--" not in argv:
    print(__doc__)
    raise SystemExit(1)

sep = argv.index("--")
head, rest = argv[:sep], argv[sep + 1:]

out_path = head[0]
crop = tuple(int(v) for v in head[1:5]) if len(head) >= 5 else None
zoom = int(head[5]) if len(head) >= 6 else 1

panels = []
for i in range(0, len(rest) - 1, 2):
    label, path = rest[i], rest[i + 1]
    img = Image.open(path).convert("RGB")
    if crop:
        img = img.crop(crop)
    if zoom > 1:
        img = img.resize((img.width * zoom, img.height * zoom), Image.NEAREST)
    panels.append((label, img))

COLOURS = [(70, 20, 20), (20, 40, 70), (20, 60, 25), (60, 50, 15)]
BAR = 24

W = max(p.width for _, p in panels)
H = sum(BAR + p.height for _, p in panels)

out = Image.new("RGB", (W, H), (0, 0, 0))
d = ImageDraw.Draw(out)

y = 0
for i, (label, img) in enumerate(panels):
    d.rectangle([0, y, W, y + BAR], fill=COLOURS[i % len(COLOURS)])
    d.text((6, 7), label, fill=(245, 245, 245)) if False else \
        d.text((6, y + 7), label, fill=(245, 245, 245))
    out.paste(img, (0, y + BAR))
    y += BAR + img.height

out.save(out_path)
print("%s  %dx%d  (%d panels)" % (out_path, out.width, out.height, len(panels)))
