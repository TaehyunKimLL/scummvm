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
import glob
import os
import re
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


def _ksx1001_hangul():
    """_ksx1001() 의 완성형 한글 2350자만 (기호, 낱자, 한자는 뺀다)."""
    return [cp for cp in _ksx1001() if 0xAC00 <= cp <= 0xD7A3]


def _ksx1001_symbols():
    """_ksx1001_nohanja() 에서 한글 2350자를 뺀 것: 기호와 호환 낱자만."""
    return [cp for cp in _ksx1001_nohanja() if not (0xAC00 <= cp <= 0xD7A3)]


def _cp949_hangul():
    """cp949 이 담는 현대 한글 11172자 전부 (U+AC00-U+D7A3, 유니코드 한글
    음절 블록과 정확히 같다): "hangul" 의 다른 이름."""
    return list(range(0xAC00, 0xD7A4))


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
    "ksx1001-hangul": _ksx1001_hangul,
    "ksx1001-symbols": _ksx1001_symbols,
    "cp949": _cp949,
    "cp949-hangul": _cp949_hangul,
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


# --chars-from/--limit 을 붙여도 항상 남는 글자: ASCII 와 대체 상자(□).
# font_map.cpp 의 missing=u+25a1 이 그리는 바로 그 글자다.
ALWAYS_KEEP = set(range(0x20, 0x7F)) | {0x25A1}


def _parse_code_value(value):
    """"0x5e", "u+2192", 10진수 -> 코드 포인트. font_map.cpp 의
    parseCodeValue() 와 같은 규칙 (그 함수와 달리 실패하면 None)."""
    if not value:
        return None
    s, base = value, 10
    if s[:2].lower() in ("u+", "0x"):
        s, base = s[2:], 16
    if not s:
        return None
    try:
        n = int(s, base)
    except ValueError:
        return None
    return n if 0 <= n <= 0x10FFFF else None


def _strip_ini_comment(line):
    """`;` 뒤가 주석이지만 앞에 공백이 있어야 한다 (font_map.cpp 와 같은
    규칙); 줄 첫 글자가 `;` 나 `#` 이면 줄 전체가 주석."""
    stripped = line.strip()
    if stripped[:1] in (";", "#"):
        return ""
    out = []
    for i, c in enumerate(line):
        if c == ";" and i > 0 and line[i - 1] in " \t":
            break
        out.append(c)
    return "".join(out)


def chars_from_map(path):
    """.MAP (hires_text INI) 에서 missing= 값과 [glyphs] 오른쪽의 절대
    코드를 모은다. 범위의 +n 오프셋은 고정된 코드가 아니라서 뺀다."""
    cps = []
    section = ""
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for raw in f:
            line = _strip_ini_comment(raw).strip()
            if not line:
                continue
            if line[0] == "[" and line[-1] == "]":
                section = line[1:-1].split(":", 1)[0].strip().lower()
                continue
            if "=" not in line:
                continue
            key, _, value = line.partition("=")
            key, value = key.strip().lower(), value.strip()
            # 버전 2 맵: [font]/[font.N] missing=, [glyphs]/[glyphs.N].
            # 버전 1 의 [hires] missing= 과 keep 도 그대로 읽는다.
            if (section in ("hires", "font") or section.startswith("font.")) and key == "missing":
                cp = _parse_code_value(value)
                if cp is not None:
                    cps.append(cp)
            elif section == "glyphs" or section.startswith("glyphs."):
                # original/keep 은 게임 글꼴, +n 은 고정 코드가 아니고,
                # <face>:u+XXXX 는 그 face 의 글자라 이 글꼴과 상관없다.
                if value.lower() in ("keep", "original") or value.startswith("+") or ":" in value:
                    continue
                cp = _parse_code_value(value)
                if cp is not None:
                    cps.append(cp)
    return cps


def chars_from_str(path):
    """SCI-KO.STR/sci-ko.str (script<TAB>id[<TAB>room]<TAB>text, UTF-8,
    '#' 주석) 에서 쓰인 모든 글자. translation.cpp::loadFromStream 과 같은
    필드 나누기; room 은 숫자뿐이라 따로 가려낼 필요가 없다."""
    cps = []
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.rstrip("\r\n")
            if not line or line[0] == "#":
                continue
            parts = line.split("\t", 2)
            if len(parts) < 3:
                continue
            cps.extend(ord(c) for c in parts[2])
    return cps


def _res_str_len(data, off, path="?"):
    """resStrLen() 규칙의 SCUMM 문자열 길이 (0xFF 코드 1/2/3/8 은 인수 없음, 그 밖은 2바이트)."""
    n = 0
    i = off
    while True:
        if i >= len(data):
            sys.exit(f"--chars-from: {path} 의 오프셋 {off} 문자열에 종료 NUL 이 없다")
        c = data[i]
        i += 1
        if c == 0:
            return n
        n += 1
        if c == 0xFF:
            if i >= len(data):
                sys.exit(f"--chars-from: {path} 의 오프셋 {off} 문자열의 이스케이프가 파일 끝을 넘는다")
            code = data[i]
            i += 1
            n += 1
            if code not in (1, 2, 3, 8):
                if i + 2 > len(data):
                    sys.exit(f"--chars-from: {path} 의 오프셋 {off} 문자열의 이스케이프가 파일 끝을 넘는다")
                i += 2
                n += 2


def _trs_string_chars(data, off, enc, path="?"):
    """오프셋 off 의 문자열을 제어 코드를 건너뛰며 enc 로 디코드한 코드 포인트들 (U+FFFD 제외)."""
    end = off + _res_str_len(data, off, path)
    cps = []
    run = bytearray()
    i = off

    def flush():
        if run:
            cps.extend(ord(c) for c in run.decode(enc, errors="replace") if c != "�")
            run.clear()

    while i < end:
        c = data[i]
        if c == 0xFF:
            flush()
            code = data[i + 1]
            i += 2 if code in (1, 2, 3, 8) else 4
            continue
        run.append(c)
        i += 1
    flush()
    return cps


def chars_from_trs(path):
    """.trs (SCVMTRS 묶음) 의 색인에 있는 원문과 번역문의 모든 코드 포인트 (본문이 BOM 이면 UTF-8, 아니면 CP949)."""
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 10 or data[:8] != b"SCVMTRS ":
        sys.exit(f"--chars-from: {path} 는 SCVMTRS 묶음이 아니다 (머리 8바이트가 다르다)")
    num = struct.unpack_from("<H", data, 8)[0]
    p = 10
    if p + num * 10 > len(data):
        sys.exit(f"--chars-from: {path} 의 줄 색인이 잘렸다")
    offsets = set()
    for _ in range(num):
        _idx, orig_off, trans_off = struct.unpack_from("<HII", data, p)
        offsets.add(orig_off)
        offsets.add(trans_off)
        p += 10
    if p >= len(data):
        sys.exit(f"--chars-from: {path} 의 방 표가 잘렸다")
    nroom = data[p]
    p += 1
    for _ in range(nroom):
        if p + 3 > len(data):
            sys.exit(f"--chars-from: {path} 의 방 표가 잘렸다")
        p += 1  # roomId
        nscript = struct.unpack_from("<H", data, p)[0]
        p += 2 + nscript * 8
    if p > len(data):
        sys.exit(f"--chars-from: {path} 의 방 표가 잘렸다")
    body_pos = p
    enc = "utf-8" if data[body_pos:body_pos + 3] == b"\xef\xbb\xbf" else "cp949"

    cps = []
    for off in sorted(offsets):
        if off >= len(data):
            continue
        cps.extend(_trs_string_chars(data, off, enc, path))
    return cps


def chars_from_text_patch(path):
    """TEXT.nnn/text.nnn (SCI 패치 포맷): 머리말은 2바이트 + 둘째 바이트가
    적은 만큼의 추가 바이트 (resource.h kResourceHeaderSize, resource.cpp
    processPatch() - 대개 추가 바이트는 0이라 TEXT 는 2바이트 머리말이다).
    그 뒤는 실제 TEXT 리소스와 같은 NUL 로 나뉜 UTF-8 문자열들."""
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 2:
        return []
    header_len = 2 + data[1]
    if header_len > len(data):
        header_len = 2
    cps = []
    for chunk in data[header_len:].split(b"\x00"):
        if not chunk:
            continue
        cps.extend(ord(c) for c in chunk.decode("utf-8", errors="replace"))
    return cps


def _classify_chars_from(path):
    base = os.path.basename(path)
    stem, ext = os.path.splitext(base)
    if base.lower() == "sci-ko.str" or ext.lower() == ".str":
        return "str"
    if ext.lower() == ".map":
        return "map"
    if ext.lower() == ".trs":
        return "trs"
    if stem.lower() == "text" and ext[1:].isdigit():
        return "text"
    return "plain"


def _drop_layout_controls(cps):
    """개행이나 탭 같은 ASCII 제어 문자는 화면에 그려지는 글자가 아니라
    텍스트 레이아웃의 구분자다 (SCI TEXT 리소스는 줄바꿈을 실제 0x0A 로
    담는다): 글꼴에 실을 대상에서 뺀다. 0x20 (스페이스) 부터는 그대로."""
    return [cp for cp in cps if cp >= 0x20 and cp != 0x7F]


def collect_chars_from_files(paths):
    """--chars-from: 주어진 파일들에 쓰인 코드 포인트를 모두 모은다.

    글롭 패턴(예: TEXT.*)은 쉘이 펼치지 않았다면 여기서 펼친다. 반환값은
    (코드 포인트 목록, [(경로, 그 파일의 서로 다른 코드 포인트 수), ...]).
    """
    expanded = []
    for p in paths:
        matches = sorted(glob.glob(p))
        expanded.extend(matches if matches else [p])

    cps = []
    counts = []
    for path in expanded:
        if not os.path.isfile(path):
            sys.exit(f"--chars-from: {path} 를 찾을 수 없다")
        kind = _classify_chars_from(path)
        if kind == "str":
            found = chars_from_str(path)
        elif kind == "text":
            found = chars_from_text_patch(path)
        elif kind == "map":
            found = chars_from_map(path)
        elif kind == "trs":
            found = chars_from_trs(path)
        else:
            with open(path, "r", encoding="utf-8", errors="replace") as f:
                found = [ord(c) for c in f.read()]
        found = _drop_layout_controls(found)
        counts.append((path, len(set(found))))
        cps.extend(found)
    return cps, counts


def parse_ranges(spec):
    """'0E01-0E3A,0020,hangul,file:sci-ko.str' -> 코드 포인트 목록 (순서대로,
    중복 없이).

    16진 코드 포인트나 범위, NAMED_RANGES 의 이름, 또는 `file:<경로>`
    (collect_chars_from_files() 와 같은 규칙으로 그 파일이 쓴 글자를 담는다).
    모르는 이름은 ValueError.
    """
    out = []
    for part in spec.split(","):
        part = part.strip()
        if not part:
            continue
        if part.lower().startswith("file:"):
            found, _ = collect_chars_from_files([part[5:]])
            out.extend(found)
        elif part.lower() in NAMED_RANGES:
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


def render(font, ch, cell_w, cell_h, ascent, bpp, center=False, mark_origin=False, nudge=True):
    """글자 하나를 셀에 그려 (픽셀들, 잉크왼쪽, 잉크폭) 로 돌려준다.

    nudge 를 끄면 (--clip-cell) 위로 새는 글자를 밀어 내리지 않는다: 모든
    글자가 같은 기준선에 서고, 셀 밖의 잉크는 잘린다.

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
        dy = ascent - min(0, top + ascent) if nudge else ascent
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


def ink_box(font, ch, bpp):
    """글자 하나의 잉크 상자 (왼쪽, 위, 오른쪽, 아래), 펜과 기준선 기준 (위가
    음수, 오른쪽/아래는 끝 다음 칸). 실릴 값으로 재므로 (bpp 로 양자화) 파일에
    들어가는 잉크만 센다. 잉크가 없으면 None. 두 번째 값은 그린 그림의
    바이트 (.notdef 와 비교해 글꼴에 없는 글자를 가린다)."""
    size = max(8, int(getattr(font, "size", 16)))
    ox, oy = size * 2, size * 3
    img = Image.new("L", (size * 5, size * 5), 0)
    try:
        ImageDraw.Draw(img).text((ox, oy), ch, font=font, fill=255, anchor="ls")
    except (ValueError, OSError):
        return None, b""
    if bpp == 1:
        img = img.point(lambda v: 255 if v >= 128 else 0)
    elif bpp == 2:
        img = img.point(lambda v: quantize2(v) * 85)
    box = img.getbbox()
    if box is None:
        return None, img.tobytes()
    return (box[0] - ox, box[1] - oy, box[2] - ox, box[3] - oy), img.tobytes()


def fit_counts(boxes, cell_w, cell_h, ascent):
    """ascent 하나로 셀에 놓았을 때 (위로 넘치는 글자, 아래로 넘치는 글자,
    셀보다 넓은 글자) 목록 - 각각 코드 포인트."""
    over_top, over_bottom, too_wide = [], [], []
    for cp, (left, top, right, bottom) in boxes.items():
        if top + ascent < 0:
            over_top.append(cp)
        if bottom + ascent > cell_h:
            over_bottom.append(cp)
        if right - left > cell_w:
            too_wide.append(cp)
    return over_top, over_bottom, too_wide


def fit_ascent(boxes, cell_h, preferred):
    """모든 글자의 잉크가 [0, cell_h) 에 드는 ascent 하나: 그런 값들 가운데
    preferred 에 가장 가까운 것. 없으면 넘치는 글자가 가장 적은 ascent (같으면
    preferred 에 가까운 쪽) 와 False."""
    if not boxes:
        return preferred, True
    lo = max(-top for (_, top, _, _) in boxes.values())
    hi = min(cell_h - bottom for (_, _, _, bottom) in boxes.values())
    if lo <= hi:
        return max(lo, min(hi, preferred)), True
    best = None
    for a in range(min(lo, hi) - 1, max(lo, hi) + 2):
        t, b, _ = fit_counts(boxes, 0, cell_h, a)
        n = len(set(t) | set(b))
        key = (n, abs(a - preferred))
        if best is None or key < best[0]:
            best = (key, a)
    return best[1], False


def measure_boxes(path, size, chars, bpp):
    """size px 로 연 글꼴에서 chars (코드 포인트) 의 잉크 상자들. 글꼴에 없는
    글자 (.notdef 와 같게 그려지는 것) 와 잉크 없는 글자는 뺀다."""
    font = ImageFont.truetype(path, size)
    _, notdef = ink_box(font, "\U0010FFFF", bpp)
    boxes = {}
    for cp in chars:
        box, raw = ink_box(font, chr(cp), bpp)
        if box is None or raw == notdef:
            continue
        boxes[cp] = box
    return font, boxes


def fit_cell(path, max_size, chars, cell_w, cell_h, bpp, latin=False, min_size=8, log=print,
             preferred_ascent=0):
    """--fit-cell: max_size 부터 한 px 씩 줄여, 모든 글자의 잉크가 셀
    (cell_w x cell_h) 에 드는 가장 큰 크기와 그 크기의 ascent (하나, 모든
    글자 공통) 를 고른다. 글자마다 옮기거나 줄이지 않는다: 글꼴 전체가 한
    크기, 한 기준선이다. 크기마다 넘치는 글자 수를 찍는다. 맞는 ascent 가
    여럿이면 preferred_ascent (0 이면 choose_ascent() 의 값) 에 가장 가까운 것.
    (크기, ascent) 를 돌려준다. 어느 크기도 맞지 않으면 SystemExit."""
    for size in range(max_size, min_size - 1, -1):
        font, boxes = measure_boxes(path, size, chars, bpp)
        preferred = preferred_ascent or choose_ascent(font, cell_h, latin=latin)
        ascent, ok = fit_ascent(boxes, cell_h, preferred)
        top, bottom, wide = fit_counts(boxes, cell_w, cell_h, ascent)
        ink_top = min((b[1] for b in boxes.values()), default=0)
        ink_bottom = max((b[3] for b in boxes.values()), default=0)
        over = sorted(set(top) | set(bottom) | set(wide))
        log(f"--fit-cell: {size}px: 잉크 {ink_bottom - ink_top}줄 (기준선 위 {-ink_top}, "
            f"아래 {ink_bottom}), ascent {ascent}: 셀 {cell_w}x{cell_h} 을 넘는 글자 "
            f"{len(over)}개 (위 {len(top)}, 아래 {len(bottom)}, 폭 {len(wide)})")
        if over and len(over) <= 40:
            log("  " + ", ".join(f"U+{cp:04X}({chr(cp)})" for cp in over))
        if not over:
            log(f"--fit-cell: {size}px, ascent {ascent} 로 굽는다 ({len(boxes)}자 모두 셀 안)")
            return size, ascent
    sys.exit(f"--fit-cell: {max_size}px 부터 {min_size}px 까지 어느 크기도 셀에 맞지 않는다")


def clipped_ink(font, ch, bpp, ascent, cell_h):
    """--clip-cell: 셀 (위 0, 아래 cell_h, 기준선 ascent) 위아래로 잘려 나가는
    잉크 픽셀 수 (가득 찬 픽셀, 옅은 가장자리) - 실릴 값으로 센다."""
    size = max(8, int(getattr(font, "size", 16)))
    ox, oy = size * 2, size * 3
    img = Image.new("L", (size * 5, size * 5), 0)
    try:
        ImageDraw.Draw(img).text((ox, oy), ch, font=font, fill=255, anchor="ls")
    except (ValueError, OSError):
        return 0, 0
    if bpp == 1:
        img = img.point(lambda v: 255 if v >= 128 else 0)
    elif bpp == 2:
        img = img.point(lambda v: quantize2(v) * 85)
    full = fringe = 0
    top, bottom = oy - ascent, oy - ascent + cell_h
    px = img.load()
    for y in list(range(0, top)) + list(range(bottom, img.height)):
        for x in range(img.width):
            v = px[x, y]
            if v == 255:
                full += 1
            elif v:
                fringe += 1
    return full, fringe


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
                    help="기준선 위치, 셀 맨 위에서 몇 행째인지. 생략하면 "
                         "choose_ascent_from() 이 고른다: 글꼴의 ascent + descent 가 "
                         "셀에 들어가면 글꼴의 ascent, 넘치면 잉크 상자를 셀 가운데에 "
                         "놓는 값 (라틴 세트가 셀보다 크면 대문자 위를 셀 맨 위에)")
    ap.add_argument("--fit-cell", action="store_true",
                    help="굽는 글자 전부의 잉크가 셀 (--cell x --width) 안에 들도록 "
                         "글꼴 크기를 --size 부터 1px 씩 줄여 고른다. 글꼴 전체가 한 "
                         "크기, 기준선 (ascent) 하나다: 글자마다 옮기거나 줄이지 "
                         "않는다. 크기마다 넘치는 글자 수를 찍고, 고른 크기와 ascent "
                         "를 알린다. --ascent 를 함께 주면 맞는 ascent 가 여럿일 때 "
                         "그 값에 가장 가까운 것을 고른다 (게임 글꼴의 기준선 행에 "
                         "맞출 때)")
    ap.add_argument("--clip-cell", action="store_true",
                    help="모든 글자를 한 기준선 (--ascent, 생략하면 셀 밖으로 나가는 "
                         "글자가 가장 적은 값) 에 그대로 두고, 셀 위아래로 나가는 잉크는 "
                         "자른다 (글자를 밀어 넣지 않는다). 잘린 글자 수와 픽셀 수 "
                         "(가득 찬 것 / 옅은 가장자리) 를 알린다")
    ap.add_argument("--shadow", type=int, default=0xFF,
                    help="기존 그림자 방식 0~3, 없으면 255")
    ap.add_argument("--count", type=int, default=0,
                    help="글리프 수. 생략하면 코드 페이지 기본값")
    ap.add_argument("--unicode", default="",
                    help="코드 포인트 순서로 굽는다 (버전 2, cmap). 16진 범위나 이름: "
                         + ", ".join(sorted(NAMED_RANGES)) +
                         ". 항목에 file:<경로> 를 쓰면 --chars-from 과 같은 규칙으로 "
                         "그 파일이 쓴 글자를 담는다. 예: ascii,hangul,ksx1001 (한글 "
                         "11172자 + 한자), ascii,ksx1001-hangul (2350 음절만). 가변폭이고, "
                         "결합 부호는 flags 비트 2 규칙으로 저장한다. 글꼴에 없는 글자 "
                         "(잉크가 없는 것, 공백 제외)는 빼서 대체 글꼴로 넘어가게 한다")
    ap.add_argument("--chars-from", nargs="+", default=None, metavar="FILE",
                    help="이 파일들이 쓴 코드 포인트를 모아 --unicode 와 합친다 "
                         "(합집합). 글롭 패턴(TEXT.*)도 받는다. 파일 종류는 이름으로 "
                         "가린다: TEXT.nnn/text.nnn (SCI 패치 - 2바이트 머리말 뒤 "
                         "NUL 로 나뉜 UTF-8 문자열), *.str/sci-ko.str (script<TAB>id"
                         "[<TAB>room]<TAB>text, UTF-8, # 주석), *.map (hires_text "
                         "INI 의 missing= 과 [glyphs] 절대 코드), *.trs (SCUMM SCVMTRS "
                         "묶음 - 본문이 UTF-8 BOM 으로 시작하면 UTF-8, 아니면 CP949 로 "
                         "원문+번역문 모든 문자열을 읽고, 0xFF 로 시작하는 SCUMM 제어 "
                         "코드는 건너뛴다), 그 밖은 그냥 UTF-8 텍스트. ASCII 와 U+25A1 "
                         "은 --chars-from 유무와 무관하게 항상 실린다")
    ap.add_argument("--limit", default="",
                    help="결과를 이 코드 포인트 집합과 교집합한다 (--unicode 와 같은 "
                         "문법, file: 항목도 된다). --chars-from 으로 모은 글자 중 "
                         "이 집합 밖의 것은 빠지고(대체 폰트가 없으면 missing= 로 □ "
                         "그려진다) 요약에 찍힌다. ASCII 와 U+25A1 은 --limit 과 "
                         "무관하게 남는다. 예: --chars-from TEXT.* SCI-KO.STR "
                         "--limit ascii,ksx1001-nohanja (게임이 쓴 글자 중 KS X 1001 "
                         "에 있는 것만)")
    ap.add_argument("--fail-on-drop", action="store_true",
                    help="--limit 이 --chars-from 의 글자를 하나라도 뺐으면 "
                         "0 이 아닌 종료 코드로 실패한다")
    ap.add_argument("--require", action="store_true",
                    help="모은 코드 포인트(--limit 을 거친 뒤) 중 원본 TTF 에 없는 "
                         "것이 있으면(그려 보면 .notdef 상자) 실패한다: 목록을 찍고 "
                         "0 이 아닌 종료 코드로 끝나며, 파일을 쓰지 않는다")
    args = ap.parse_args()

    cell_h = args.cell or args.size
    cell_w = args.width or cell_h

    chars_from_cps = []
    chars_from_counts = []
    if args.chars_from:
        chars_from_cps, chars_from_counts = collect_chars_from_files(args.chars_from)

    unicode_cps = None
    if args.unicode or args.chars_from:
        try:
            unicode_cps = parse_ranges(args.unicode) if args.unicode else []
        except ValueError as e:
            sys.exit(f"--unicode 를 읽을 수 없다: {e}")
        seen = set(unicode_cps)
        for cp in chars_from_cps:
            if cp not in seen:
                seen.add(cp)
                unicode_cps.append(cp)
        for cp in ALWAYS_KEEP:
            if cp not in seen:
                seen.add(cp)
                unicode_cps.append(cp)
        if not unicode_cps:
            sys.exit("--unicode/--chars-from 이 빈 목록이다")

    dropped = []
    if args.limit:
        if unicode_cps is None:
            sys.exit("--limit 은 --unicode 나 --chars-from 과 함께 써야 한다")
        try:
            limit_cps = set(parse_ranges(args.limit))
        except ValueError as e:
            sys.exit(f"--limit 을 읽을 수 없다: {e}")
        keep = limit_cps | ALWAYS_KEEP
        dropped = sorted(cp for cp in set(chars_from_cps) if cp not in keep)
        unicode_cps = [cp for cp in unicode_cps if cp in keep]
        if not unicode_cps:
            sys.exit("--limit 이 모든 글자를 뺐다")

    if args.chars_from:
        print(f"--chars-from: {sum(c for _, c in chars_from_counts)} 개 (서로 다른 "
              f"코드 포인트, 파일별 합계 - 겹치면 더 많게 보임), 합쳐서 "
              f"{len(set(chars_from_cps))}개")
        for path, c in chars_from_counts:
            print(f"  {path}: {c}")
        if args.limit:
            if dropped:
                names = ", ".join(f"U+{cp:04X}({chr(cp)})" for cp in dropped)
                print(f"  --limit 밖이라 뺀 게임 글자 {len(dropped)}개 (missing= 이 "
                      f"있으면 □ 로 보인다): {names}")
                if args.fail_on_drop:
                    sys.exit(f"--fail-on-drop: --limit 이 게임이 쓴 글자 {len(dropped)}"
                              f"개를 뺐다")
            else:
                print("  --limit 이 뺀 게임 글자 없음")

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

    size = args.size
    fit_ascent_value = 0
    if args.fit_cell:
        if unicode_cps:
            fit_chars = list(unicode_cps)
        else:
            fit_chars = [ord(ch) for _, ch in glyph_chars(codepage, count) if ch is not None]
        size, fit_ascent_value = fit_cell(args.input, args.size, fit_chars, cell_w, cell_h,
                                          args.bpp, latin=args.latin, preferred_ascent=args.ascent)

    try:
        font = ImageFont.truetype(args.input, size)
    except OSError as e:
        sys.exit(f"폰트를 {size}px 로 열 수 없다: {e}\n"
                 f"비트맵 TTF 라면 내장된 크기만 쓸 수 있다.")

    # 기준선은 폰트가 알려주는 값을 쓴다. 셀 높이에 비례해 짐작하면
    # 글리프가 위아래로 밀려 잘린다.
    # 글꼴이 셀보다 크면 잉크가 실제로 차지하는 자리를 재서 그것을 셀에
    # 맞춘다 (choose_ascent_from()): 명목 크기로 계산하면 손글씨처럼 여백이
    # 큰 글꼴이 쓸데없이 눌린다.
    if args.clip_cell and not args.fit_cell:
        clip_chars = list(unicode_cps) if unicode_cps else \
            [ord(ch) for _, ch in glyph_chars(codepage, count) if ch is not None]
        _, clip_boxes = measure_boxes(args.input, size, clip_chars, args.bpp)
        if args.ascent:
            ascent = args.ascent
        else:
            ascent, _ = fit_ascent(clip_boxes, cell_h, choose_ascent(font, cell_h, latin=args.latin))
        top, bottom, wide = fit_counts(clip_boxes, cell_w, cell_h, ascent)
        cut = sorted(set(top) | set(bottom))
        full = fringe = 0
        for cp in cut:
            f, g = clipped_ink(font, chr(cp), args.bpp, ascent, cell_h)
            full += f
            fringe += g
        print(f"--clip-cell: {size}px, ascent {ascent}: {len(clip_boxes)}자 중 {len(cut)}자가 셀 "
              f"위아래로 잘린다 (위 {len(top)}, 아래 {len(bottom)}), 잉크 {full + fringe}픽셀 "
              f"(가득 {full}, 가장자리 {fringe}); 셀보다 넓은 글자 {len(wide)}")
        if cut and len(cut) <= 40:
            print("  " + "".join(chr(cp) for cp in cut))
    elif args.fit_cell:
        ascent = fit_ascent_value
    elif args.ascent:
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
    requested_absent = []   # --require: 요청했지만 글꼴에 없는 (.notdef) 코드 포인트
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
                                         mark_origin=bool(unicode_cps),
                                         nudge=not args.clip_cell)
        absent = notdef is not None and ch is not None and img.tobytes() == notdef
        if ch is None or ink_w == 0 or absent:
            missing += 1
        if unicode_cps and absent:
            requested_absent.append(ord(ch))
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

    if args.require and requested_absent:
        names = ", ".join(f"U+{cp:04X}" for cp in sorted(set(requested_absent)))
        sys.exit(f"--require: 원본 글꼴에 없는 코드 포인트 {len(set(requested_absent))}"
                  f"개, 파일을 쓰지 않는다: {names}")

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
    print(f"  크기 {size}px, ascent {ascent}")
    print(f"  {total:,} 바이트")


if __name__ == "__main__":
    main()
