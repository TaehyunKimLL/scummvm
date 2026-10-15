#!/usr/bin/env python3
"""trs_fix.py TABLE.tsv IN.trs -o OUT.trs [--utf8]: apply byte fixes to a SCVMTRS bundle's translations.

The table has one fix a line: the line index, the translation's bytes before and after (hex), a note, and
optionally the UTF-8 bytes (hex) that stand for the new bytes in a UTF-8 bundle (a character the CP949 form
cannot spell, such as the a-circumflex of "Papier-Mache"). The old bytes must be the whole translation or a
run inside it that occurs once; the strings after it move and their offsets in the index follow. Everything
else (encoding, file order, room ranges) stays.

--utf8 then converts every translation from CP949 to UTF-8 the way scumm_trs_from_patch.legacy_to_utf8()
does (Hangul and KS X 1001 symbols to UTF-8, escapes FF xx and every other byte kept), writes the UTF-8 BOM
at the body start, and lists each non-Hangul high byte left raw with its line number. The originals (the
lookup keys), the index order and the room table are not touched.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from scumm_trs_from_patch import BOM, esc_len, legacy_to_utf8, parse_trs, res_str_len  # noqa: E402


def read_table(rows):
    out = []
    for row in rows:
        row = row.rstrip('\r\n')
        if not row.strip() or row.startswith('#'):
            continue
        f = row.split('\t')
        utf8 = bytes.fromhex(f[4]) if len(f) > 4 and f[4].strip() else None
        out.append((int(f[0]), bytes.fromhex(f[1]), bytes.fromhex(f[2]), utf8))
    return out


def _index(d):
    recs, _rooms, _body = parse_trs(d, 'bundle')
    return {idx: (o, t) for idx, o, t in recs}


def lines(d):
    """{line index: (original, translation)} as bytes."""
    def cstr(o):
        return d[o:o + res_str_len(d, o)]
    return {i: (cstr(o), cstr(t)) for i, (o, t) in _index(d).items()}


def apply(d, fixes):
    d = bytes(d)
    for line, old, new in (f[:3] for f in fixes):
        idx = _index(d)
        if line not in idx:
            raise SystemExit('line %d: no such line' % line)
        t = idx[line][1]
        trans = d[t:t + res_str_len(d, t)]
        at = trans.find(old)
        if at < 0 or trans.find(old, at + 1) >= 0:
            raise SystemExit('line %d: %s is not once in its translation %s' % (line, old.hex(' '), trans.hex(' ')))
        pos, delta = t + at, len(new) - len(old)
        d = d[:pos] + new + d[pos + len(old):]
        if delta:
            n = struct.unpack_from('<H', d, 8)[0]
            buf = bytearray(d)
            for k in range(n):
                p = 10 + k * 10
                i, o1, o2 = struct.unpack_from('<HII', buf, p)
                struct.pack_into('<HII', buf, p, i, o1 + delta if o1 > pos else o1, o2 + delta if o2 > pos else o2)
            d = bytes(buf)
    parse_trs(d, 'fixed bundle')
    return d


# FE 01/02/03/08 are v1-v6 escapes without arguments (ScummTextDecoder::escapeLength): not a character.
KNOWN_FE = (1, 2, 3, 8)


def _put_literals(t, fixes):
    """A translation as [(bytes, is_utf8)]: the runs a fix names a UTF-8 form for are cut out as literals."""
    segs = [(t, False)]
    for _line, _old, new, utf8 in fixes:
        if utf8 is None:
            continue
        hits = [(k, seg.find(new)) for k, (seg, lit) in enumerate(segs) if not lit and seg.find(new) >= 0]
        if len(hits) != 1 or segs[hits[0][0]][0].find(new, hits[0][1] + 1) >= 0:
            raise SystemExit('%s is not once in the translation %s' % (new.hex(' '), t.hex(' ')))
        k, at = hits[0]
        seg = segs[k][0]
        segs[k:k + 1] = [(seg[:at], False), (utf8, True), (seg[at + len(new):], False)]
    return segs


def raw_high_bytes(b):
    """[(position, byte)] of the bytes at or above 0x80 that are neither UTF-8 nor part of an escape."""
    out = []
    i = 0
    while i < len(b):
        c = b[i]
        if c == 0xFF and i + 1 < len(b):
            i += esc_len(b, i)
            continue
        if c == 0xFE and i + 1 < len(b) and b[i + 1] in KNOWN_FE:
            i += 2
            continue
        if c < 0x80:
            i += 1
            continue
        n = 2 if c & 0xE0 == 0xC0 else 3 if c & 0xF0 == 0xE0 else 1
        if n > 1 and i + n <= len(b):
            try:
                b[i:i + n].decode('utf-8')
                i += n
                continue
            except UnicodeDecodeError:
                pass
        out.append((i, c))
        i += 1
    return out


def to_utf8(d, fixes=()):
    """(UTF-8 bundle, raw): d is a CP949 bundle with the table's fixes already applied; raw is
    [(line index, byte, position, the translation as text)] of every high byte left raw. A fix with a UTF-8
    form uses it for its new bytes."""
    recs, _rooms, body = parse_trs(d, 'bundle')
    if d[body:body + 3] == BOM:
        raise SystemExit('bundle is already UTF-8 (BOM at the body start)')
    by_line = {}
    for f in fixes:
        by_line.setdefault(f[0], []).append(f)
    strings = {}
    for idx, o, t in recs:
        strings.setdefault(o, (idx, False))
        strings.setdefault(t, (idx, True))
    pos = body
    new_off = {}
    new_body = bytearray(BOM)
    raw = []
    for off in sorted(strings):
        if off != pos:
            raise SystemExit('offset %d: the strings do not follow each other from the body start (expected %d)'
                             % (off, pos))
        idx, is_trans = strings[off]
        n = res_str_len(d, off)
        s = d[off:off + n]
        if is_trans:
            segs = _put_literals(s, by_line.get(idx, []))
            s = b''.join(seg if lit else legacy_to_utf8(seg) for seg, lit in segs)
            raw += [(idx, c, p, s.decode('utf-8', 'backslashreplace')) for p, c in raw_high_bytes(s)]
        new_off[off] = body + len(new_body)
        new_body += s + b'\0'
        pos = off + n + 1
    if pos != len(d):
        raise SystemExit('%d bytes follow the last string' % (len(d) - pos))
    out = bytearray(d[:body])
    for k, (idx, o, t) in enumerate(recs):
        struct.pack_into('<HII', out, 10 + k * 10, idx, new_off[o], new_off[t])
    out += new_body
    parse_trs(bytes(out), 'UTF-8 bundle')
    return bytes(out), raw


def _text(b, utf8):
    """A translation as text: escapes kept as \\xNN, a raw byte as itself in Latin-1 (what the game's glyph is)."""
    out = []
    i = 0
    while i < len(b):
        c = b[i]
        if c == 0xFF and i + 1 < len(b):
            n = esc_len(b, i)
            out.append(''.join('\\x%02x' % x for x in b[i:i + n]))
            i += n
        elif c < 0x80:
            out.append(chr(c))
            i += 1
        elif utf8:
            n = 2 if c & 0xE0 == 0xC0 else 3 if c & 0xF0 == 0xE0 else 1
            try:
                out.append(b[i:i + n].decode('utf-8') if n > 1 else '\\x%02x' % c)
                i += n
            except UnicodeDecodeError:
                out.append('\\x%02x' % c)
                i += 1
        else:
            try:
                ch = b[i:i + 2].decode('cp949')
            except UnicodeDecodeError:
                ch = None
            if ch and len(ch) == 1 and 0xAC00 <= ord(ch) <= 0xD7A3 or ch and len(ch) == 1 and (0xA1 <= c <= 0xAC and b[i + 1] >= 0xA1):
                out.append(ch)
                i += 2
            else:
                out.append('\\x%02x' % c)
                i += 1
    return ''.join(out)


def compare(old, new):
    """[(line, old text, new text)] of the translations whose text differs between a CP949 bundle and its UTF-8
    conversion; the originals and the index order must be identical or this raises SystemExit."""
    a, b = lines(old), lines(new)
    if sorted(a) != sorted(b):
        raise SystemExit('the bundles do not have the same lines')
    ra, _, _ = parse_trs(old, 'old')
    rb, _, _ = parse_trs(new, 'new')
    if [r[0] for r in ra] != [r[0] for r in rb]:
        raise SystemExit('the index order differs')
    diffs = []
    for i in sorted(a):
        if a[i][0] != b[i][0]:
            raise SystemExit('line %d: the original differs' % i)
        x, y = _text(a[i][1], False), _text(b[i][1], True)
        if x != y:
            diffs.append((i, x, y))
    return diffs


def main():
    if len(sys.argv) == 4 and sys.argv[1] == '--compare':
        with open(sys.argv[2], 'rb') as f:
            old = f.read()
        with open(sys.argv[3], 'rb') as f:
            new = f.read()
        diffs = compare(old, new)
        n = len(lines(old))
        print('%s -> %s: %d lines, originals and index order identical, %d translations differ' % (
            sys.argv[2], sys.argv[3], n, len(diffs)))
        for i, x, y in diffs:
            print('  line %d: %r -> %r' % (i, x, y))
        return
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('table')
    ap.add_argument('trs')
    ap.add_argument('-o', '--output', required=True)
    ap.add_argument('--utf8', action='store_true', help='write a UTF-8 bundle (BOM at the body start)')
    a = ap.parse_args()
    with open(a.table, encoding='utf-8') as f:
        fixes = read_table(f)
    with open(a.trs, 'rb') as f:
        d = f.read()
    out = apply(d, fixes)
    raw = []
    if a.utf8:
        out, raw = to_utf8(out, fixes)
    with open(a.output, 'wb') as f:
        f.write(out)
    print('%s: %d fixes, %d -> %d bytes%s' % (a.output, len(fixes), len(d), len(out), ', UTF-8' if a.utf8 else ''))
    for line, c, p, text in raw:
        print('  raw byte 0x%02x kept: line %d, byte %d: %s' % (c, line, p, text.replace('\n', ' ')[:60]))
    if raw:
        print('  %d raw high bytes: each draws as the game\'s own glyph' % len(raw))


if __name__ == '__main__':
    main()
