#!/usr/bin/env python3
"""trs_fix.py TABLE.tsv IN.trs -o OUT.trs: apply byte fixes to a SCVMTRS bundle's translations.

The table has one fix a line: the line index, the translation's bytes before and after (hex), a note.
The old bytes must be the whole translation or a run inside it that occurs once; the strings after it
move and their offsets in the index follow. Everything else (encoding, file order, room ranges) stays.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from scumm_trs_from_patch import parse_trs, res_str_len  # noqa: E402


def read_table(rows):
    out = []
    for row in rows:
        row = row.rstrip('\r\n')
        if not row.strip() or row.startswith('#'):
            continue
        f = row.split('\t')
        out.append((int(f[0]), bytes.fromhex(f[1]), bytes.fromhex(f[2])))
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
    for line, old, new in fixes:
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


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('table')
    ap.add_argument('trs')
    ap.add_argument('-o', '--output', required=True)
    a = ap.parse_args()
    with open(a.table, encoding='utf-8') as f:
        fixes = read_table(f)
    with open(a.trs, 'rb') as f:
        d = f.read()
    out = apply(d, fixes)
    with open(a.output, 'wb') as f:
        f.write(out)
    print('%s: %d fixes, %d -> %d bytes' % (a.output, len(fixes), len(d), len(out)))


if __name__ == '__main__':
    main()
