#!/usr/bin/env python3
"""trscheck.py: check a SCUMM translation bundle (korean.trs, ko.trs) the way the engine reads it.

The engine (engines/scumm/trs_bundle.h, korean_text.h) holds Korean text as UTF-8; a bundle without the UTF-8 BOM
(EF BB BF at the start of its body) is CP949 and converted once at load. This tool applies the same rules in Python:

  census FILE       encoding, lines, CP949 pairs the engine cannot read ('?'), game charset bytes (0x80-0xFF that are
                    no pair), characters that korean.fnt lacks (anything but the 2350 KS X 1001 syllables)
  utf8len FILE      the longest translation as UTF-8 and how many exceed the engine's buffers: 512 bytes
                    (loadPtrToResource(), _charsetBuffer, drawString()) and 256 (the v2 sentence parts)
  equal CP949 UTF8  the CP949 bundle converted by the engine's rules against the UTF-8 stage of the same translation:
                    the lines that differ (--allow N tolerates N, as the stray 0xFA byte of the MI1 UTE stage)

usage: python3 -I tools/korean/trscheck.py census|utf8len|equal ... ; exit 1 on a finding (census: unreadable pairs,
utf8len: a translation of 512 bytes or more, equal: more differing lines than --allow).
"""

import argparse
import bisect
import struct
import sys

BOM = b"\xef\xbb\xbf"
KS_ROWS = range(0xB0, 0xC9)


class Bundle:
    def __init__(self, body, entries):
        self.body = body
        self.entries = entries  # (original offset, translation offset) in file order, relative to the body
        self.bom = body[:3] == BOM
        self._starts = sorted({o for e in self.entries for o in e})

    def starts(self):
        return self._starts

    def text(self, off):
        """The string at off: to its NUL outside an escape, never into the next entry (engine rule)."""
        starts = self._starts
        i = bisect.bisect_right(starts, off)
        limit = starts[i] if i < len(starts) else len(self.body)
        end = text_end(self.body, off, limit)
        if end == limit and end > off and self.body[end - 1] == 0:
            end -= 1
        return bytes(self.body[off:end])


def escape_length(b, p, end):
    if p >= end or b[p] not in (0xFF, 0xFE):
        return 0
    n = 2
    if p + 1 < end and b[p + 1] not in (1, 2, 3, 8):
        n = 4
    return min(n, end - p)


def text_end(b, p, end):
    # only 0xFF starts an escape here (0xFE is also a CP949 trail byte), as the engine's scummTextEnd()
    while p < end:
        e = escape_length(b, p, end) if b[p] == 0xFF else 0
        if e:
            p += e
        elif b[p]:
            p += 1
        else:
            break
    return p


def parse(data):
    if data[:8] != b"SCVMTRS ":
        raise ValueError("not a SCVMTRS bundle")
    n = struct.unpack_from("<H", data, 8)[0]
    p = 10
    raw = []
    for _ in range(n):
        _idx, o, t = struct.unpack_from("<HII", data, p)
        p += 10
        raw.append((o, t))
    rooms = data[p]
    p += 1
    for _ in range(rooms):
        p += 1
        scripts = struct.unpack_from("<H", data, p)[0]
        p += 2 + 8 * scripts
    return Bundle(data[p:], [(o - p, t - p) for o, t in raw])


def _ksx_char(lead, trail):
    if lead in KS_ROWS and 0xA1 <= trail <= 0xFE:
        try:
            return bytes([lead, trail]).decode("euc_kr")
        except UnicodeDecodeError:
            return None
    return None


def cp949_to_utf8(s):
    """(UTF-8 bytes, unreadable pairs) by the engine's rule: escapes copied, a lead 0xB0-0xD0 takes the next byte,
    any other byte >= 0x80 is a charset glyph U+0080..U+00FF."""
    out = bytearray()
    unmapped = 0
    p = 0
    while p < len(s) and s[p]:
        e = escape_length(s, p, len(s))
        if e:
            out += s[p:p + e]
            p += e
            continue
        c = s[p]
        if c < 0x80:
            out.append(c)
            p += 1
        elif c < 0xB0 or c > 0xD0 or p + 1 >= len(s) or not s[p + 1]:
            out += chr(c).encode("utf-8")
            p += 1
        else:
            ch = _ksx_char(c, s[p + 1])
            if ch is None:
                try:
                    d = bytes([c, s[p + 1]]).decode("cp949")
                    ch = d if len(d) == 1 and d != "�" and ord(d) >= 0x100 else None
                except UnicodeDecodeError:
                    ch = None
            if ch is None:
                out.append(ord("?"))
                unmapped += 1
            else:
                out += ch.encode("utf-8")
            p += 2
    return bytes(out), unmapped


def translations_utf8(b):
    """The translations of a bundle as UTF-8: a UTF-8 body as it is, a CP949 body converted by the engine rule."""
    res = []
    unmapped = 0
    for _o, t in b.entries:
        raw = b.text(t)
        if b.bom:
            res.append(raw)
        else:
            u, n = cp949_to_utf8(raw)
            unmapped += n
            res.append(u)
    return res, unmapped


def chars(u):
    """The characters of UTF-8 engine text, escapes (0xFF/0xFE + code + arguments) skipped."""
    out = []
    p = 0
    while p < len(u) and u[p]:
        e = escape_length(u, p, len(u))
        if e:
            p += e
            continue
        n = 1 if u[p] < 0x80 else 2 if u[p] < 0xE0 else 3 if u[p] < 0xF0 else 4
        try:
            out.append(u[p:p + n].decode("utf-8"))
        except UnicodeDecodeError:
            out.append("\ufffd")  # a byte that starts no sequence (the stray 0xFA of the MI1 UTE stage)
            n = 1
        p += n
    return out


def census(b):
    unreadable = 0
    charset = 0
    out_of_font = 0
    for _o, t in b.entries:
        raw = b.text(t)
        if b.bom:
            u = raw
        else:
            u, n = cp949_to_utf8(raw)
            unreadable += n
        for ch in chars(u):
            if ch == "\ufffd" and b.bom:
                unreadable += 1  # a byte of a UTF-8 bundle that starts no character
            elif 0x80 <= ord(ch) < 0x100:
                charset += 1
            elif ord(ch) >= 0x100 and _ksx_encode(ch) is None:
                out_of_font += 1
    return {"encoding": "utf8" if b.bom else "cp949", "lines": len(b.entries), "unreadable": unreadable,
            "charset_bytes": charset, "out_of_font": out_of_font}


_KS = None


def _ksx_encode(ch):
    global _KS
    if _KS is None:
        _KS = {chr(0): None}
        for l in KS_ROWS:
            for t in range(0xA1, 0xFF):
                _KS[bytes([l, t]).decode("euc_kr")] = (l, t)
    return _KS.get(ch)


def utf8len(b):
    ts, _ = translations_utf8(b)
    lens = [len(t) for t in ts]
    return {"longest": max(lens) if lens else 0, "over_511": sum(1 for n in lens if n >= 512),
            "over_255": sum(1 for n in lens if n >= 256), "lines": len(lens)}


def equal(cp, u8):
    """Indexes of the lines whose translation differs between the converted CP949 bundle and the UTF-8 stage."""
    a, _ = translations_utf8(cp)
    b, _ = translations_utf8(u8)
    if len(a) != len(b):
        raise ValueError("different line counts: %d and %d" % (len(a), len(b)))
    return [i for i, (x, y) in enumerate(zip(a, b)) if x != y]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("census").add_argument("file")
    sub.add_parser("utf8len").add_argument("file")
    e = sub.add_parser("equal")
    e.add_argument("cp949")
    e.add_argument("utf8")
    e.add_argument("--allow", type=int, default=0)
    a = ap.parse_args(argv)
    if a.cmd == "equal":
        diff = equal(parse(open(a.cp949, "rb").read()), parse(open(a.utf8, "rb").read()))
        print("%d lines differ (allowed %d)%s" % (len(diff), a.allow, (": first " + ",".join(map(str, diff[:5]))) if diff else ""))
        return 0 if len(diff) <= a.allow else 1
    b = parse(open(a.file, "rb").read())
    if a.cmd == "census":
        c = census(b)
        print("%s: %s, %d lines, %d unreadable pairs, %d charset bytes, %d characters korean.fnt lacks" % (
            a.file, c["encoding"], c["lines"], c["unreadable"], c["charset_bytes"], c["out_of_font"]))
        return 1 if c["unreadable"] else 0
    r = utf8len(b)
    print("%s: longest translation %d bytes as UTF-8, %d >= 512, %d >= 256" % (a.file, r["longest"], r["over_511"], r["over_255"]))
    return 1 if r["over_511"] else 0


if __name__ == "__main__":
    sys.exit(main())
