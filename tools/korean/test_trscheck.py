#!/usr/bin/env python3
"""Tests of trscheck.py (run: python3 -I tools/korean/test_trscheck.py)."""

import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trscheck  # noqa: E402


def bundle(pairs, body_prefix=b""):
    """A SCVMTRS bundle of (original, translation) byte strings; no rooms. Strings are NUL-terminated."""
    body = bytearray(body_prefix)
    offs = []
    for o, t in pairs:
        offs.append((len(body), len(body) + len(o) + 1))
        body += o + b"\0" + t + b"\0"
    head = 8 + 2 + 10 * len(pairs) + 1
    out = bytearray(b"SCVMTRS ") + struct.pack("<H", len(pairs))
    for i, (o, t) in enumerate(offs):
        out += struct.pack("<HII", i, head + o, head + t)
    out += b"\0" + body
    return bytes(out)


CP = "한글".encode("cp949")


class TrsCheckTest(unittest.TestCase):
    def test_parse(self):
        b = trscheck.parse(bundle([(b"Hi", CP), (b"Go", b"x")]))
        self.assertEqual(len(b.entries), 2)
        self.assertEqual(b.text(b.entries[0][1]), CP)
        self.assertFalse(b.bom)

    def test_translation_runs_past_a_nul_argument(self):
        t = CP + b"\xff\x04\x00\x01" + CP
        b = trscheck.parse(bundle([(b"Hi", t), (b"Go", b"x")]))
        self.assertEqual(b.text(b.entries[0][1]), t)
        # the original ending in an escape with the NUL as its last argument stops at the next entry
        b = trscheck.parse(bundle([(b"sound \xff\x04\xc8", b"x"), (b"Go", b"y")]))
        self.assertEqual(b.text(b.entries[0][0]), b"sound \xff\x04\xc8")

    def test_cp949_to_utf8_like_the_engine(self):
        out, unmapped = trscheck.cp949_to_utf8(b"\xc7\xd1\xff\x04\xb0\xa1\xb1\xdb\xbc\xfe\xfe\x01a\xfa")
        self.assertEqual(out, "한".encode() + b"\xff\x04\xb0\xa1" + "글".encode() + "숭".encode() + b"\xfe\x01a\xc3\xba")
        self.assertEqual(unmapped, 0)
        # a pair no table or decoder knows becomes '?', counted
        out, unmapped = trscheck.cp949_to_utf8(b"\xb3\x14")
        self.assertEqual((out, unmapped), (b"?", 1))

    def test_census(self):
        c = trscheck.census(trscheck.parse(bundle([(b"Hi", CP), (b"Go", b"\xb3\x14")])))
        self.assertEqual(c["encoding"], "cp949")
        self.assertEqual(c["unreadable"], 1)
        self.assertEqual(c["lines"], 2)
        u = trscheck.census(trscheck.parse(bundle([(b"Hi", "한".encode())], b"\xef\xbb\xbf")))
        self.assertEqual(u["encoding"], "utf8")

    def test_utf8len(self):
        long_t = ("한" * 200).encode("cp949")
        r = trscheck.utf8len(trscheck.parse(bundle([(b"Hi", long_t), (b"Go", b"x")])))
        self.assertEqual(r["longest"], 600)
        self.assertEqual(r["over_511"], 1)
        self.assertEqual(r["over_255"], 1)

    def test_equal(self):
        a = bundle([(b"Hi", CP), (b"Go", b"x")])
        b = bundle([(b"Hi", "한글".encode()), (b"Go", b"x")], b"\xef\xbb\xbf")
        self.assertEqual(trscheck.equal(trscheck.parse(a), trscheck.parse(b)), [])
        c = bundle([(b"Hi", "한국".encode()), (b"Go", b"x")], b"\xef\xbb\xbf")
        self.assertEqual(trscheck.equal(trscheck.parse(a), trscheck.parse(c)), [0])


if __name__ == "__main__":
    unittest.main()
