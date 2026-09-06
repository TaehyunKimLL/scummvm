#!/usr/bin/env python3
"""TTF 에서 SVFN 비트맵 폰트를 굽는다.

포맷은 docs/FONT_FORMAT.md 에 적어 두었다. 굽는 쪽만 FreeType 을 쓰므로
게임을 돌리는 빌드에는 FreeType 이 없어도 된다.

  python3 mkfont.py neodgm.ttf out.fnt --size 16 --bpp 1
  python3 mkfont.py NanumGothic.ttf out.fnt --size 24 --bpp 8 --variable
"""

import argparse
import struct
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    sys.exit("Pillow 가 필요하다: pip install Pillow")


MAGIC = b"SVFN"
VERSION = 1

FLAG_VARIABLE = 1 << 0
FLAG_JAMO = 1 << 1

# 코드 페이지별 (글리프 수, idx -> 바이트쌍) 규칙.
# docs/FONT_FORMAT.md 5절과 같은 식이다.
CODEPAGES = {
    949: dict(count=2350, enc="cp949",
              to_bytes=lambda i: (0xB0 + i // 94, 0xA1 + i % 94)),
    932: dict(count=6879, enc="cp932",
              to_bytes=lambda i: (
                  (0x81 + i // 188) if (0x81 + i // 188) < 0xA0
                  else (0xC1 + i // 188 - 0x1F),
                  (0x40 + i % 188) if (i % 188) < 0x3F else (0x41 + i % 188))),
    936: dict(count=6763, enc="gbk",
              to_bytes=lambda i: (0x81 + i // 191, 0x40 + i % 191)),
    950: dict(count=13053, enc="big5",
              to_bytes=lambda i: (0x81 + i // 191, 0x40 + i % 191)),
}


def glyph_chars(codepage, count):
    """글리프 순서대로 유니코드 문자를 내놓는다. 없는 자리는 None."""
    if codepage == 0:
        # 라틴: 글리프 번호가 곧 문자 코드다. 제어 문자 자리는 비운다.
        for i in range(count):
            yield i, (chr(i) if 0x20 <= i < 0x7F or 0xA0 <= i <= 0xFF else None)
        return

    if codepage == -1:
        # 전각 라틴: 자리는 ASCII 그대로 두되 글리프는 전각 것을 쓴다.
        # CJK 글꼴의 U+FF01~FF5E 는 한글과 같은 정사각 틀에 그려져 있어서,
        # 글자마다 8px 셀을 쓰는 v0-v2 에 그대로 들어맞는다.
        for i in range(count):
            if i == 0x20:
                yield i, "\u3000"          # 전각 공백
            elif 0x21 <= i <= 0x7E:
                yield i, chr(i + 0xFEE0)   # ASCII -> U+FF01..FF5E
            else:
                yield i, None
        return

    spec = CODEPAGES[codepage]
    for i in range(count):
        hi, lo = spec["to_bytes"](i)
        if not (0 <= hi <= 0xFF and 0 <= lo <= 0xFF):
            yield i, None
            continue
        try:
            ch = bytes((hi, lo)).decode(spec["enc"])
        except UnicodeDecodeError:
            yield i, None
            continue
        yield i, ch


def render(font, ch, cell_w, cell_h, ascent, bpp, center=False):
    """글자 하나를 셀에 그려 (픽셀들, 잉크왼쪽, 잉크폭) 로 돌려준다.

    center 를 켜면 잉크를 셀 가운데에 놓는다. 고정폭으로 구울 때 쓴다:
    비례폭 글꼴은 글자마다 잉크 폭이 다른데, 셀 왼쪽에 붙여 놓으면 좁은
    글자 뒤에 구멍이 생겨 글이 성기게 보인다.
    """
    img = Image.new("L", (cell_w, cell_h), 0)
    if ch is None:
        return img, 0, 0

    # 잉크가 셀 밖으로 나가면 잘린다. 먼저 어디에 놓이는지 물어보고,
    # 왼쪽이나 위로 새는 만큼 밀어 넣는다.
    try:
        bbox = font.getbbox(ch, anchor="ls")
    except (ValueError, OSError):
        bbox = None

    dx, dy = 0, ascent
    if bbox:
        left, top, right, bottom = bbox
        dx = -min(0, left)
        dy = ascent - min(0, top + ascent)
        # 오른쪽으로도 넘치면 왼쪽으로 당긴다.
        if right + dx > cell_w:
            dx -= (right + dx) - cell_w
            dx = max(dx, -left)

        if center:
            ink_w = right - left
            dx = (cell_w - ink_w) // 2 - left

    draw = ImageDraw.Draw(img)
    try:
        draw.text((dx, dy), ch, font=font, fill=255, anchor="ls")
    except (ValueError, OSError):
        draw.text((dx, 0), ch, font=font, fill=255)

    if bpp == 1:
        img = img.point(lambda v: 255 if v >= 128 else 0)

    ink = img.getbbox()
    if ink is None:
        return img, 0, 0
    return img, ink[0], ink[2] - ink[0]


def pack_glyph(img, cell_w, cell_h, bpp):
    px = img.load()
    out = bytearray()

    if bpp == 1:
        stride = (cell_w + 7) // 8
        for y in range(cell_h):
            row = bytearray(stride)
            for x in range(cell_w):
                if px[x, y] >= 128:
                    row[x >> 3] |= 0x80 >> (x & 7)
            out += row
    else:
        for y in range(cell_h):
            for x in range(cell_w):
                out.append(px[x, y])

    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description="TTF -> SVFN")
    ap.add_argument("input", help="원본 TTF/OTF")
    ap.add_argument("output", help="구워낼 .fnt")
    ap.add_argument("--size", type=int, required=True,
                    help="글꼴을 몇 px 로 렌더할지")
    ap.add_argument("--cell", type=int, default=0,
                    help="셀 높이. 생략하면 --size 와 같다. 손글씨처럼 잉크가 "
                         "명목 크기보다 작은 글꼴은 --size 를 키우고 --cell 을 "
                         "셀에 맞춰 잡는다")
    ap.add_argument("--width", type=int, default=0,
                    help="셀 폭. 생략하면 셀 높이와 같다")
    ap.add_argument("--bpp", type=int, choices=(1, 8), default=8)
    ap.add_argument("--codepage", type=int, choices=sorted(CODEPAGES), default=949)
    ap.add_argument("--center", action="store_true",
                    help="잉크를 셀 가운데에 놓는다. 비례폭 글꼴을 고정폭 셀에 "
                         "구울 때 쓴다 (v0-v2 처럼 셀이 고정된 엔진)")
    ap.add_argument("--fixed", action="store_true",
                    help="라틴을 고정폭으로 굽는다. 셀이 고정된 v0-v2 용")
    ap.add_argument("--fullwidth", action="store_true",
                    help="라틴을 전각 글리프로 굽는다. 글자마다 셀이 고정된 "
                         "v0-v2 용. --latin 과 함께 쓴다")
    ap.add_argument("--latin", action="store_true",
                    help="단일 바이트 폰트를 굽는다. 글리프 번호가 곧 문자 코드이고 "
                         "가변폭이 기본이다")
    ap.add_argument("--ink-advance", action="store_true",
                    help="가변폭 advance 를 폰트 metric 이 아니라 실제 잉크 폭에서 잡는다. "
                         "CJK 폰트는 모든 글자에 같은 advance 를 보고하므로 "
                         "한글을 가변폭으로 구우려면 이 옵션이 필요하다")
    ap.add_argument("--variable", action="store_true",
                    help="글자별 전진 폭 표를 넣는다")
    ap.add_argument("--ascent", type=int, default=0,
                    help="기준선 위치. 생략하면 셀 높이의 약 80%%")
    ap.add_argument("--shadow", type=int, default=0xFF,
                    help="기존 그림자 방식 0~3, 없으면 255")
    ap.add_argument("--count", type=int, default=0,
                    help="글리프 수. 생략하면 코드 페이지 기본값")
    args = ap.parse_args()

    cell_h = args.cell or args.size
    cell_w = args.width or cell_h

    if args.latin:
        codepage = -1 if args.fullwidth else 0
        count = args.count or 256
        # 셀이 고정된 엔진(v0-v2)에서는 전진 폭 표가 무시되므로 고정폭으로
        # 굽고 잉크를 셀 가운데 놓는다. 그 밖에는 글자마다 폭을 싣는다.
        variable = not (args.fullwidth or args.fixed)
    else:
        codepage = args.codepage
        count = args.count or CODEPAGES[codepage]["count"]
        variable = args.variable

    # 고정폭으로 구우면서 가운데 정렬을 끄면 글자가 왼쪽에 몰린다.
    center = args.center or not variable

    try:
        font = ImageFont.truetype(args.input, args.size)
    except OSError as e:
        sys.exit(f"폰트를 {args.size}px 로 열 수 없다: {e}\n"
                 f"비트맵 TTF 라면 내장된 크기만 쓸 수 있다.")

    # 기준선은 폰트가 알려주는 값을 쓴다. 셀 높이에 비례해 짐작하면
    # 글리프가 위아래로 밀려 잘린다.
    if args.ascent:
        ascent = args.ascent
    else:
        ascent, descent = font.getmetrics()
        if ascent + descent > cell_h:
            # 글꼴이 셀보다 크다. 잉크가 실제로 차지하는 자리를 재서
            # 그것을 셀에 맞춘다: 명목 크기로 계산하면 손글씨처럼 여백이
            # 큰 글꼴이 쓸데없이 눌린다.
            probe = font.getbbox("한글AQg", anchor="ls")
            ink_top, ink_bottom = probe[1], probe[3]
            ink_h = ink_bottom - ink_top
            if ink_h > 0 and ink_h <= cell_h:
                ascent = -ink_top + (cell_h - ink_h) // 2
            else:
                ascent = max(1, cell_h - descent)

    glyphs = bytearray()
    metrics = bytearray()
    missing = 0
    ink_max = 0

    for idx, ch in glyph_chars(codepage, count):
        img, ink_x, ink_w = render(font, ch, cell_w, cell_h, ascent,
                                         args.bpp, center=center)
        if ch is None or ink_w == 0:
            missing += 1
        ink_max = max(ink_max, ink_w)

        glyphs += pack_glyph(img, cell_w, cell_h, args.bpp)

        if variable:
            if args.ink_advance and ch and ink_w:
                # Advance from the ink, not the face's own metric.
                #
                # A CJK face reports one advance for every syllable because
                # they are designed on a square em - NanumGothic says 15.05 for
                # 가, 이 and 무 alike - yet their ink is 14, 12 and 15 wide. A
                # font baked from those advances is fixed width in all but
                # name, and a game that lays text out on that grid cannot close
                # the gaps.
                #
                # Ink plus one pixel of side bearing keeps neighbouring
                # syllables from touching while letting narrow ones take less
                # room. The renderer widens any glyph whose ink still reaches
                # past this, so a wrong guess here cannot cause an overlap.
                adv = ink_x + ink_w + 1
            else:
                adv = int(round(font.getlength(ch))) if ch else cell_w
            adv = max(0, min(255, adv))
            metrics += struct.pack("<BbBB", adv, max(-128, min(127, ink_x)),
                                   min(255, ink_w), 0)

    flags = FLAG_VARIABLE if variable else 0
    header_size = 32
    metrics_off = header_size if variable else 0
    data_off = header_size + len(metrics)

    header = struct.pack(
        "<4sHHBBHHBBBBHIII",
        MAGIC, VERSION, flags,
        args.bpp, args.shadow & 0xFF,
        max(codepage, 0), count,
        cell_w, cell_h, ascent, 0, 0,
        metrics_off, data_off, len(glyphs))

    assert len(header) == header_size, len(header)

    with open(args.output, "wb") as f:
        f.write(header)
        f.write(metrics)
        f.write(glyphs)

    total = header_size + len(metrics) + len(glyphs)
    print(f"{args.output}: {cell_w}x{cell_h} {args.bpp}bpp "
          f"{'가변폭' if variable else '고정폭'} "
          f"{'latin-fullwidth' if codepage == -1 else ('latin' if codepage == 0 else f'cp{codepage}')}")
    print(f"  글리프 {count}개 (빈 글리프 {missing}개), 최대 잉크 폭 {ink_max}px")
    print(f"  {total:,} 바이트")


if __name__ == "__main__":
    main()
