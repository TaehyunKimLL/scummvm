#!/usr/bin/env python3
"""TTF 에서 SVFN 비트맵 폰트를 굽는다.

포맷은 docs/FONT_FORMAT.md 에 적어 두었다. 굽는 쪽만 FreeType 을 쓰므로
게임을 돌리는 빌드에는 FreeType 이 없어도 된다.

  python3 mkfont.py neodgm.ttf out.fnt --size 16 --bpp 1
  python3 mkfont.py NanumGothic.ttf out.fnt --size 24 --bpp 8 --variable
  # 4단계 커버리지 (2bpp): 8bpp 의 1/4 크기, cp949 가 담는 글자 전부
  python3 mkfont.py NanumGothic-Bold.ttf ko.fnt --size 18 --bpp 2 --unicode ascii,cp949
  # 코드 포인트 순서 (버전 2): 한글 11172자 전부, 다른 문자도
  python3 mkfont.py Galmuri7.ttf ko.fnt --size 8 --cell 9 --bpp 1 --unicode ascii,hangul,ksx1001
"""

import argparse
import struct
import sys
import unicodedata

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    sys.exit("Pillow 가 필요하다: pip install Pillow")


MAGIC = b"SVFN"
VERSION = 1

FLAG_VARIABLE = 1 << 0
FLAG_JAMO = 1 << 1
# 결합 부호(Mn/Me)는 펜을 max(0, -bearingX) 열에 두고 저장했다 (FONT_FORMAT.md 3절).
FLAG_MARKS_AT_ORIGIN = 1 << 2

# 버전 2: 코드 페이지 순서 대신 코드 포인트 표(cmap)를 싣는다. 머리말 32번
# 위치에 표의 오프셋이 들어가서 머리말이 36바이트다 (HiResFontBaker 와 같다).
# 읽는 쪽은 graphics/hires_text/bitmap_font.cpp (버전 1 과 2 를 다 읽는다).
VERSION_CMAP = 2
HEADER_V2 = 36
# glyphCount 는 16비트다.
MAX_GLYPHS = 0xFFFF

# 잉크가 없어도 싣는 글자: 레이아웃이 그 폭만큼 나아가야 한다.
BLANK_OK = {0x20, 0xA0, 0x3000}


def _ksx1001():
    """KS X 1001 (cp949 의 A1A1-FEFE) 이 담는 모든 글자: 기호, 낱자, 완성형
    2350자, 한자 4888자."""
    out = []
    for hi in range(0xA1, 0xFF):
        for lo in range(0xA1, 0xFF):
            try:
                ch = bytes((hi, lo)).decode("cp949")
            except UnicodeDecodeError:
                continue
            if len(ch) == 1:
                out.append(ord(ch))
    return out


def _ksx1001_nohanja():
    """_ksx1001() 에서 한자 (CJK 통합 한자 U+4E00-U+9FFF, 호환용 U+F900-U+FAFF)
    를 뺀 것: 기호, 낱자, 완성형 2350자만 남는다. 한자까지 구우면 한 파일이
    커지고, 한자는 다른 폭소스(예: 고밀도 CJK 글꼴)로 넘길 수 있어 DOS L
    프리셋에는 굳이 필요 없다."""
    return [cp for cp in _ksx1001()
            if not (0x4E00 <= cp <= 0x9FFF or 0xF900 <= cp <= 0xFAFF)]


def _cp949():
    """cp949 (통합형 한글 코드) 가 담는 모든 글자: 2바이트 코드 0x81-0xFE x
    0x41-0xFE 중 한 글자로 디코드되는 것 전부 (현대 한글 11172자, 한자,
    기호, 낱자) 와 ASCII. 코드 순서대로, 중복 없이."""
    out = list(range(0x20, 0x7F))
    for hi in range(0x81, 0xFF):
        for lo in range(0x41, 0xFF):
            try:
                ch = bytes((hi, lo)).decode("cp949")
            except UnicodeDecodeError:
                continue
            if len(ch) == 1:
                out.append(ord(ch))
    seen = set()
    return [c for c in out if not (c in seen or seen.add(c))]


def quantize2(v):
    """0-255 커버리지를 2bpp 의 0-3 단계로. 읽는 쪽은 단계 x 85 로 되돌린다
    (TextCompose::expandCoverage), 그래서 오차는 픽셀당 43 이하다."""
    return (v * 3 + 127) // 255


# --unicode 에 이름으로 쓸 수 있는 묶음.
NAMED_RANGES = {
    "ascii": lambda: list(range(0x20, 0x7F)),
    "latin1": lambda: list(range(0xA0, 0x100)),
    "hangul": lambda: list(range(0xAC00, 0xD7A4)),          # 현대 한글 11172자
    "jamo": lambda: list(range(0x3131, 0x318F)),            # 호환 낱자
    "cjk-punct": lambda: list(range(0x3000, 0x3040)) + list(range(0xFF01, 0xFF5F)),
    "ksx1001": _ksx1001,
    "ksx1001-nohanja": _ksx1001_nohanja,
    "cp949": _cp949,
    "kana": lambda: list(range(0x3041, 0x3100)),
    "thai": lambda: list(range(0x0E01, 0x0E3B)) + list(range(0x0E3F, 0x0E5C)),
}


def unicode_keep(cp, ink_w, absent):
    """--unicode 로 구울 때 이 글자를 실을지.

    글꼴에 없는 글자 (absent: .notdef 와 똑같이 그려짐) 는 싣지 않는다. 빈
    글리프나 네모를 실으면 이 폰트가 그 글자를 가진 것처럼 보여 대체 글꼴로
    넘어가지 못한다. 잉크 없는 글자는 공백 (BLANK_OK) 만 싣는다: 레이아웃이
    그 폭만큼 나아가야 한다. 공백이라도 글꼴에 없으면 싣지 않는다.
    """
    if absent:
        return False
    return ink_w > 0 or cp in BLANK_OK


def is_mark(ch):
    return ch is not None and unicodedata.category(ch) in ("Mn", "Me")


def parse_ranges(spec):
    """'0E01-0E3A,0020,hangul' -> 코드 포인트 목록 (순서대로, 중복 없이).

    16진 코드 포인트나 범위, 또는 NAMED_RANGES 의 이름. 모르는 이름은
    ValueError.
    """
    out = []
    for part in spec.split(","):
        part = part.strip()
        if not part:
            continue
        if part.lower() in NAMED_RANGES:
            out.extend(NAMED_RANGES[part.lower()]())
        elif "-" in part:
            a, b = part.split("-", 1)
            out.extend(range(int(a, 16), int(b, 16) + 1))
        else:
            out.append(int(part, 16))
    seen = set()
    return [c for c in out if not (c in seen or seen.add(c))]

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


# 줄이 셀보다 큰 글꼴의 잉크를 재는 글자들. 라틴 세트는 한글이 없는
# 글꼴에서도 재도록 라틴 글자만 쓴다.
PROBE_CJK = "한글AQg"
PROBE_LATIN = "AQgjy"


def choose_ascent_from(ascent, descent, ink_top, ink_bottom, cell_h, latin):
    """셀 맨 위에서 기준선까지의 거리.

    글꼴의 줄 (ascent + descent) 이 셀에 들어가면 글꼴 값을 그대로 쓴다.
    넘치면 잉크 상자 (ink_top..ink_bottom, 기준선 기준, 위가 음수) 를 셀
    가운데 놓는다. 잉크마저 셀보다 크면: 한글 세트는 예전처럼 내림자
    자리를 남기고, 라틴 세트는 대문자 위를 셀 맨 위에 두어 넘치는 만큼은
    내림자에서 잘린다.

    라틴에 한글 규칙을 쓰면 기준선이 셀 위로 올라가, 셀 위로 새는 글리프를
    render() 가 하나씩 밀어 내린다: 'H' 'l' 은 내려가고 'r' ',' '.' 는
    그대로라 글자마다 다른 줄에 앉는다 (C6 G2, AppleGothic 16px: 기준선 10
    에서 'l' 은 13, 'r' 은 10). 대문자 위에 맞추면 밀려 내려가는 것은
    'Ä' 같은 위 부호 글자와 괄호처럼 대문자보다 높은 것뿐이다.
    """
    if ascent + descent <= cell_h:
        return ascent
    ink_h = ink_bottom - ink_top
    if 0 < ink_h <= cell_h:
        return -ink_top + (cell_h - ink_h) // 2
    if latin and ink_h > 0:
        return max(1, min(cell_h, -ink_top))
    return max(1, cell_h - descent)


def choose_ascent(font, cell_h, latin=False):
    """choose_ascent_from() 을 글꼴에서 잰 값으로 부른다."""
    ascent, descent = font.getmetrics()
    probe = font.getbbox(PROBE_LATIN if latin else PROBE_CJK, anchor="ls")
    return choose_ascent_from(ascent, descent, probe[1], probe[3], cell_h, latin)


def render(font, ch, cell_w, cell_h, ascent, bpp, center=False, mark_origin=False):
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
        if mark_origin and is_mark(ch):
            # 결합 부호: 잉크가 펜 왼쪽에 있다 (타이 성조는 음의 bearing 으로
            # 앞 글자 위에 얹힌다). 펜을 max(0, -left) 열에 두면 잘리지 않고,
            # 읽는 쪽은 bearingX 에서 그 열을 되찾는다.
            dx = min(max(0, -left), cell_w)
        else:
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
    elif bpp == 2:
        # 실릴 값 그대로 (0, 85, 170, 255): 잉크 상자와 .notdef 비교가 파일에
        # 들어가는 픽셀을 본다. 43 미만의 옅은 가장자리는 여기서 사라진다.
        img = img.point(lambda v: quantize2(v) * 85)

    ink = img.getbbox()
    if ink is None:
        return img, 0, 0
    if mark_origin and is_mark(ch):
        # bearingX 는 펜에서 잉크 왼쪽까지: 음수 그대로.
        return img, ink[0] - dx, ink[2] - ink[0]
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
    elif bpp == 2:
        # 네 픽셀이 한 바이트, 왼쪽 픽셀이 상위 비트 쌍이다
        # (TextCompose::expandCoverage). 행은 바이트 단위로 채운다.
        stride = (cell_w + 3) // 4
        for y in range(cell_h):
            row = bytearray(stride)
            for x in range(cell_w):
                row[x >> 2] |= quantize2(px[x, y]) << (6 - (x & 3) * 2)
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
    ap.add_argument("--bpp", type=int, choices=(1, 2, 8), default=8,
                    help="픽셀당 비트: 1 (스텐실), 2 (커버리지 4단계, 8bpp 의 "
                         "1/4 크기), 8 (커버리지 256단계)")
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
    ap.add_argument("--unicode", default="",
                    help="코드 포인트 순서로 굽는다 (버전 2, cmap). 16진 범위나 이름: "
                         + ", ".join(sorted(NAMED_RANGES)) +
                         ". 예: ascii,hangul,ksx1001 (한글 11172자 + 한자). 가변폭이고, "
                         "결합 부호는 flags 비트 2 규칙으로 저장한다. 글꼴에 없는 글자 "
                         "(잉크가 없는 것, 공백 제외)는 빼서 대체 글꼴로 넘어가게 한다")
    args = ap.parse_args()

    cell_h = args.cell or args.size
    cell_w = args.width or cell_h

    unicode_cps = None
    if args.unicode:
        try:
            unicode_cps = parse_ranges(args.unicode)
        except ValueError as e:
            sys.exit(f"--unicode 를 읽을 수 없다: {e}")
        if not unicode_cps:
            sys.exit("--unicode 가 빈 목록이다")

    if unicode_cps:
        codepage = 0
        count = len(unicode_cps)
        variable = True
    elif args.latin:
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
    center = (args.center or not variable) and not unicode_cps

    try:
        font = ImageFont.truetype(args.input, args.size)
    except OSError as e:
        sys.exit(f"폰트를 {args.size}px 로 열 수 없다: {e}\n"
                 f"비트맵 TTF 라면 내장된 크기만 쓸 수 있다.")

    # 기준선은 폰트가 알려주는 값을 쓴다. 셀 높이에 비례해 짐작하면
    # 글리프가 위아래로 밀려 잘린다.
    # 글꼴이 셀보다 크면 잉크가 실제로 차지하는 자리를 재서 그것을 셀에
    # 맞춘다 (choose_ascent_from()): 명목 크기로 계산하면 손글씨처럼 여백이
    # 큰 글꼴이 쓸데없이 눌린다.
    if args.ascent:
        ascent = args.ascent
    else:
        ascent = choose_ascent(font, cell_h, latin=args.latin)

    glyphs = bytearray()
    metrics = bytearray()
    missing = 0
    ink_max = 0

    if unicode_cps:
        chars = [(i, chr(cp)) for i, cp in enumerate(unicode_cps)]
    else:
        chars = glyph_chars(codepage, count)

    kept = []   # --unicode: 실은 코드 포인트, 글리프 순서대로
    # 글꼴에 없는 글자는 .notdef (보통 네모) 로 그려진다. Pillow 는 cmap 을
    # 묻는 길이 없어서, 어느 글꼴의 cmap 에도 없는 U+10FFFF (비문자) 를 그린
    # 것과 똑같으면 없는 글자로 친다.
    notdef = None
    if unicode_cps:
        nd_img, _, nd_w = render(font, "\U0010FFFF", cell_w, cell_h, ascent, args.bpp)
        if nd_w:
            notdef = nd_img.tobytes()
    for idx, ch in chars:
        img, ink_x, ink_w = render(font, ch, cell_w, cell_h, ascent,
                                         args.bpp, center=center,
                                         mark_origin=bool(unicode_cps))
        absent = notdef is not None and ch is not None and img.tobytes() == notdef
        if ch is None or ink_w == 0 or absent:
            missing += 1
        if unicode_cps and not unicode_keep(ord(ch), ink_w, absent):
            continue
        if unicode_cps:
            kept.append(ord(ch))
        ink_max = max(ink_max, ink_w)

        glyphs += pack_glyph(img, cell_w, cell_h, args.bpp)

        if variable:
            if unicode_cps and is_mark(ch):
                # 결합 부호는 나아가지 않는다.
                adv = int(round(font.getlength(ch)))
            elif args.ink_advance and ch and ink_w:
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
    cmap = b""
    version = VERSION
    if unicode_cps:
        count = len(kept)
        if count == 0:
            sys.exit("--unicode 의 글자가 글꼴에 하나도 없다")
        if count > MAX_GLYPHS:
            sys.exit(f"글리프 {count}개: 한 파일에는 {MAX_GLYPHS}개까지만 실린다")
        # 버전 2: 머리말 뒤에 cmap, 그 뒤에 폭 표와 글리프 (HiResFontBaker 순서).
        flags |= FLAG_MARKS_AT_ORIGIN
        version = VERSION_CMAP
        header_size = HEADER_V2
        cmap = b"".join(struct.pack("<II", cp, i) for i, cp in enumerate(kept))
    metrics_off = (header_size + len(cmap)) if variable else 0
    data_off = header_size + len(cmap) + len(metrics)

    header = struct.pack(
        "<4sHHBBHHBBBBHIII",
        MAGIC, version, flags,
        args.bpp, args.shadow & 0xFF,
        max(codepage, 0), count,
        cell_w, cell_h, ascent, 0, 0,
        metrics_off, data_off, len(glyphs))

    if unicode_cps:
        header += struct.pack("<I", header_size)   # cmapOffset
    assert len(header) == header_size, len(header)

    with open(args.output, "wb") as f:
        f.write(header)
        f.write(cmap)
        f.write(metrics)
        f.write(glyphs)

    total = header_size + len(cmap) + len(metrics) + len(glyphs)
    print(f"{args.output}: {cell_w}x{cell_h} {args.bpp}bpp "
          f"{'가변폭' if variable else '고정폭'} "
          f"{'unicode (v2 cmap)' if unicode_cps else ('latin-fullwidth' if codepage == -1 else ('latin' if codepage == 0 else f'cp{codepage}'))}")
    if unicode_cps:
        print(f"  글리프 {count}개 (글꼴에 없어 뺀 글자 {len(unicode_cps) - count}개), 최대 잉크 폭 {ink_max}px")
    else:
        print(f"  글리프 {count}개 (빈 글리프 {missing}개), 최대 잉크 폭 {ink_max}px")
    print(f"  {total:,} 바이트")


if __name__ == "__main__":
    main()
