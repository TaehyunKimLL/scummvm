#!/usr/bin/env python3
# ScummVM - Graphic Adventure Engine
#
# ScummVM is the legal property of its developers, whose names
# are too numerous to list here. Please refer to the COPYRIGHT
# file distributed with this source distribution.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
"""Tests of k6bake.py: python3 -I tools/korean/test_k6bake.py (from anywhere; standard library only).

Scratch files go to $TMPDIR, or to k6bake-test/ in the working directory when it is not set (never the system
temp dir)."""

import atexit
import hashlib
import os
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import k6bake  # noqa: E402

MINI = os.path.join(HERE, "testdata", "mini.vocab.tsv")
GOLDEN = os.path.join(ROOT, "test", "korean_vocab_golden.h")
GOLDEN_ARGS = ["--game", "kq1", "--fingerprint", "0123456789abcdef0123456789abcdef",
               "--vocab-md5", "fedcba9876543210fedcba9876543210"]
FP = "0123456789abcdef0123456789abcdef"
# Scratch files go under the working directory, never the system temp dir (host convention).
# Set TMPDIR to put them elsewhere; the folder is removed again when it is empty.
TMPROOT = os.environ.get("TMPDIR") or os.path.join(os.getcwd(), "k6bake-test")
os.makedirs(TMPROOT, exist_ok=True)
atexit.register(lambda: os.path.isdir(TMPROOT) and not os.listdir(TMPROOT) and os.rmdir(TMPROOT))


def master(*lines):
    return "\n".join([k6bake.MASTER_TAG, "@magic SCIKOR", "@class 1 0x0800 a", "@class 2 0x0010 b"] + list(lines)) + "\n"


class K6BakeTest(unittest.TestCase):
    def test_master_rows_sorted_with_sequences_and_alternatives(self):
        magic, rows = k6bake.master_rows(open(MINI, encoding="utf-8").read())
        self.assertEqual(magic, "SCIKOR")
        self.assertEqual([w for w, _g, _c in rows], ["가방", "들여다봐", "들여다봐", "문", "문", "열쇠", "집다"])
        self.assertEqual(rows[1], ("들여다봐", 30, 0x8080))
        self.assertEqual(rows[2], ("들여다봐", 169, 0x0010))
        self.assertEqual(rows[3][1:], (83, 0x0800))
        self.assertEqual(rows[4][1:], (166, 0x0800))

    def test_version2_layout(self):
        data = k6bake.table_bytes("SCIKOR", [("가", 1, 0x0800), ("나다", 2, 0x8010)], 2)
        self.assertEqual(data.hex(), "5343494b4f520000" "02000000" "02000000"
                         "0000" "0100" "0100" "0008" "0100" "0200" "0200" "1080" "00ac" "98b0" "e4b2")

    def test_version3_layout(self):
        head = k6bake.header_line("kq1", FP.upper(), "sci")
        self.assertEqual(head, "#! format=1 game=kq1 fingerprint=%s engine=sci" % FP)
        data = k6bake.table_bytes("SCIKOR", [("가", 1, 0x0800)], 3, head)
        self.assertEqual(data[8:12], b"\x03\0\0\0")
        size = int.from_bytes(data[16:20], "little")
        self.assertEqual(size % 4, 0)
        self.assertEqual(data[20:20 + len(head)].decode("ascii"), head)
        self.assertEqual(data[20 + len(head)], 0)
        self.assertEqual(data[20 + size:], bytes.fromhex("00000000" "0100" "0100" "0008" "0000" "00ac"))

    def test_round_trip_between_versions(self):
        magic, rows = k6bake.master_rows(open(MINI, encoding="utf-8").read())
        v2 = k6bake.table_bytes(magic, rows, 2)
        v3 = k6bake.table_bytes(magic, rows, 3, k6bake.header_line("kq1", FP, "sci"))
        m2, ver2, h2, r2 = k6bake.read_table(v2)
        m3, ver3, h3, r3 = k6bake.read_table(v3)
        self.assertEqual((m2, ver2, h2, r2), ("SCIKOR", 2, None, rows))
        self.assertEqual((m3, ver3, r3), ("SCIKOR", 3, rows))
        self.assertTrue(h3.startswith("#! format=1 game=kq1 "))

    def test_committed_golden_is_current(self):
        with open(GOLDEN, encoding="utf-8") as f:
            self.assertEqual(f.read(), k6bake.golden(MINI, list(GOLDEN_ARGS)))

    def test_cli_build_convert_dump(self):
        with tempfile.TemporaryDirectory(dir=TMPROOT) as tmp:
            v3 = os.path.join(tmp, "t3.dat")
            v2 = os.path.join(tmp, "t2.dat")
            self.assertEqual(k6bake.main(["k6bake", "build", MINI, v3, "--game", "kq1", "--fingerprint", FP]), 0)
            self.assertEqual(k6bake.main(["k6bake", "convert", v3, v2, "--format", "2"]), 0)
            magic, rows = k6bake.master_rows(open(MINI, encoding="utf-8").read())
            with open(v2, "rb") as f:
                self.assertEqual(f.read(), k6bake.table_bytes(magic, rows, 2))
            self.assertEqual(k6bake.main(["k6bake", "build", MINI, v3]), 2)            # format 3 needs --game
            self.assertEqual(k6bake.main(["k6bake", "build", MINI, v2, "--format", "2", "--game", "kq1"]), 2)

    def test_k3map_source(self):
        import json
        with tempfile.TemporaryDirectory(dir=TMPROOT) as tmp:
            k3 = os.path.join(tmp, "k3map.json")
            k2 = os.path.join(tmp, "k2targets.json")
            json.dump({"map": {"문": [83, 166], "들여다봐": "30+169", "\U00020000": 1, "\ua960": 5}}, open(k3, "w"))
            json.dump({"groups": [{"id": 83, "classes": [0x800]}, {"id": 30, "classes": [0x80, 0x10]}]}, open(k2, "w"))
            magic, rows = k6bake.source_rows(k3, ["--targets", k2])
        # U+A960 (a Hangul Jamo Extended-A letter) is not CP949: skipped, as the harness's k6bake.py did; the plane-2
        # word too.
        self.assertEqual(magic, "SCIKOR")
        self.assertEqual(rows, [("들여다봐", 30, 0x8090), ("들여다봐", 169, 0), ("문", 83, 0x800), ("문", 166, 0)])

    def test_refusals(self):
        bad = [
            "not a master\n",
            master("가\t1", "가\t2"),        # a word on two lines
            master("가\t3"),                 # a group without @class
            master("가\tx"),
            master("\U00020000\t1"),         # outside the BMP
        ]
        for text in bad:
            with self.assertRaises(k6bake.BakeError):
                k6bake.master_rows(text)
        with self.assertRaises(k6bake.BakeError):
            k6bake.header_line("kq1", "0123", "sci")
        with self.assertRaises(k6bake.BakeError):
            k6bake.header_line("k q", FP, "sci")
        rows = [("가" * 15 + chr(0xAC00 + i), 1, 0) for i in range(4200)]      # 67,200 code units
        with self.assertRaises(k6bake.BakeError):
            k6bake.table_bytes("SCIKOR", rows, 2)
        self.assertEqual(k6bake.read_table(k6bake.table_bytes("SCIKOR", rows, 3, k6bake.header_line("x", FP, "sci")))[3], rows)
        unsorted = k6bake.table_bytes("SCIKOR", [("나", 1, 0), ("가", 1, 0)], 2)
        with self.assertRaises(k6bake.BakeError):
            k6bake.read_table(unsorted)

    def test_word_longer_than_the_reader_looks_up_is_refused(self):
        # common/korean_stem.h KoreanStems::kMaxLen: a longer stored word could never be found.
        ok = [("가" * 15 + "나", 1, 0)]
        self.assertEqual(k6bake.read_table(k6bake.table_bytes("SCIKOR", ok, 2))[3], ok)
        long_rows = [("가" * 16 + "나", 1, 0)]
        for version, head in ((2, None), (3, k6bake.header_line("kq1", FP, "sci", FP))):
            with self.assertRaises(k6bake.BakeError):
                k6bake.table_bytes("SCIKOR", long_rows, version, head)
        # a table that already holds one (written by another tool) is refused on reading
        unit = "가".encode("utf-16-le")
        raw = (b"SCIKOR\0\0" + struct.pack("<II", 2, 1) + struct.pack("<HHHH", 0, 17, 1, 0) + unit * 17)
        with self.assertRaises(k6bake.BakeError):
            k6bake.read_table(raw)
        with tempfile.TemporaryDirectory(dir=TMPROOT) as tmp:
            master_path = os.path.join(tmp, "long.vocab.tsv")
            with open(master_path, "w", encoding="utf-8") as f:
                f.write(master("가" * 17 + "\t1"))
            self.assertEqual(k6bake.main(["k6bake", "build", master_path, os.path.join(tmp, "o.dat"), "--format", "2"]), 2)
            self.assertFalse(os.path.exists(os.path.join(tmp, "o.dat")))


if __name__ == "__main__":
    unittest.main()
