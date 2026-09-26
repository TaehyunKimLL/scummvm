"""Compare Latin ink width between faces at a given cell.

v0-v2 lay text on a fixed 8-pixel cell, so at 2x every glyph gets 16 pixels
whatever it is. A Korean face's Latin is half-width plus padding - the ink
comes out around 12 and the line looks gappy and thin. A true full-width
design fills the cell.
"""
import os
import sys
from PIL import Image, ImageDraw, ImageFont

size = int(sys.argv[1]) if len(sys.argv) > 1 else 16
def find_font(*names):
    """First readable match among the usual font locations."""
    roots = [
        '/usr/share/fonts', '/usr/local/share/fonts',
        os.path.expanduser('~/.local/share/fonts'),
        os.path.expanduser('~/.fonts'),
        os.path.expanduser('~/.local/sysroot/usr/share/fonts'),
    ]
    for root in roots:
        for dirpath, _, files in os.walk(root):
            for want in names:
                if want in files:
                    return os.path.join(dirpath, want)
    return None


faces = sys.argv[2:]
if not faces:
    faces = [f for f in (find_font('NanumGothic.ttf'),
                         find_font('unifont_jp.otf', 'unifont.otf')) if f]
    if not faces:
        print('no fonts found - pass paths on the command line')
        raise SystemExit(1)

SAMPLE = 'Copyright1987MWi'

for path in faces:
    try:
        font = ImageFont.truetype(path, size)
    except Exception as e:
        print('%-28s  cannot load: %s' % (path.split('/')[-1], e))
        continue

    widths = []
    for ch in SAMPLE:
        img = Image.new('L', (size * 3, size * 3), 0)
        ImageDraw.Draw(img).text((size, size // 2), ch, fill=255, font=font)
        bbox = img.getbbox()
        widths.append(bbox[2] - bbox[0] if bbox else 0)

    adv = [font.getlength(ch) for ch in SAMPLE]
    print('%-28s size %d' % (path.split('/')[-1], size))
    print('   ink      %s' % ' '.join('%2d' % w for w in widths))
    print('   advance  %s' % ' '.join('%2d' % round(a) for a in adv))
    print('   ink %d..%d, advance %d..%d %s'
          % (min(widths), max(widths), round(min(adv)), round(max(adv)),
             '(fixed)' if len(set(round(a) for a in adv)) == 1 else '(variable)'))
