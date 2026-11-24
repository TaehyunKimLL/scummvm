#!/usr/bin/env python3
"""Tests of strcheck.py (run: python3 -I tools/korean/test_strcheck.py)."""

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import strcheck  # noqa: E402

HDR_AGI = "#! format=1 game=lsl1 fingerprint=1d9311bbee31342e76c7570a29d7616d engine=agi"
HDR = "#! format=1 game=kq1sci fingerprint=87ef5027c321e18e7b81a47bb584c098 engine=sci"


class StrCheckTest(unittest.TestCase):
    def test_good_table(self):
        header, entries, problems = strcheck.check([HDR + "\n", "# c\n", "0\t5\tA\n", "0\t5\t14\tB\n", "12\t3\ta\\nb\tc\n"])
        self.assertEqual(header["game"], "kq1sci")
        self.assertEqual(entries, 3)
        self.assertEqual(problems, [])

    def test_bad_lines(self):
        _, _, problems = strcheck.check(["0\tx\tA\n", "0\t5\tA\n", "0\t5\tB\n", "70000\t1\tC\n"])
        texts = [t for _, t in problems]
        self.assertIn((1, "not script<TAB>id[<TAB>room]<TAB>text"), problems)
        self.assertIn((3, "duplicates line 2 (the later line wins)"), problems)
        self.assertIn((4, "not script<TAB>id[<TAB>room]<TAB>text"), problems)
        self.assertTrue(any(t.startswith("no fingerprint") for t in texts))

    def test_leading_zeros_like_the_engine(self):
        # ScriptStrings::readNum accepts digits with leading zeros (0..65534)
        _, entries, problems = strcheck.check([HDR + "\n", "05\t007\tA\n", "0\t5\t014\tB\n"])
        self.assertEqual(entries, 2)
        self.assertEqual(problems, [])

    def test_duplicates_by_value(self):
        # 05/007 and 5/7 are the same key for the engine
        _, _, problems = strcheck.check([HDR + "\n", "05\t007\tA\n", "5\t7\tB\n"])
        self.assertTrue(any("duplicates" in t for _, t in problems))

    def test_bad_headers(self):
        for line in ("#!format=1 game=a fingerprint=%s engine=sci" % ("0" * 32),
                     "#! format=2 game=a fingerprint=%s engine=sci" % ("0" * 32),
                     "#! format=1 game=a fingerprint=xyz engine=sci",
                     "#! format=1 fingerprint=%s engine=sci" % ("0" * 32)):
            with self.assertRaises(ValueError):
                strcheck.parse_header(line)

    def test_set_header(self):
        d = tempfile.mkdtemp(dir=os.environ.get("TMPDIR") or ".")
        try:
            p = os.path.join(d, "sci-ko.str")
            with open(p, "w", encoding="utf-8") as f:
                f.write("\ufeff#! format=1 game=old fingerprint=%s engine=sci\n0\t5\tA\n" % ("0" * 32))
            self.assertEqual(strcheck.main(["header", p, "--line", HDR]), 0)
            with open(p, encoding="utf-8") as f:
                self.assertEqual(f.read(), HDR + "\n0\t5\tA\n")
            self.assertEqual(strcheck.main(["check", p]), 0)
            self.assertEqual(strcheck.main(["header", p, "--line", "#! bad"]), 2)
        finally:
            for n in os.listdir(d):
                os.remove(os.path.join(d, n))
            os.rmdir(d)


    # AGI tables (agi-<code>.str, DESIGN.md section 4): keys m<logic>:<n>, o<index>, v<view>, TAB, text
    def test_agi_good_table(self):
        header, entries, problems = strcheck.check([HDR_AGI + "\n", "# c\n", "\n", "m0:1\tA\n", "m255:255\ta\\nb\\tc\\\\d\n",
                                                   "o3\tB\n", "o0\t\n", "v121\t\uc548\n", "m007:0012\tZ\n"])
        self.assertEqual(header["engine"], "agi")
        self.assertEqual(entries, 6)
        self.assertEqual(problems, [])

    def test_agi_engine_flag_without_a_header(self):
        header, entries, problems = strcheck.check(["m0:1\tA\n"], engine="agi")
        self.assertIsNone(header)
        self.assertEqual(entries, 1)
        self.assertEqual([t for _, t in problems if not t.startswith("no fingerprint")], [])

    def test_agi_bad_lines(self):
        lines = [HDR_AGI + "\n", "m0:0\tA\n", "m256:1\tA\n", "o256\tA\n", "q1\tA\n", "m1:1 no tab\n", "m1:2\tbad \udcff byte\n",
                 "v300\tA\n", "m1:256\tA\n", "m:1\tA\n", "m1:\tA\n", "o\tA\n", "0\t5\tA\n"]
        _, entries, problems = strcheck.check(lines)
        self.assertEqual(entries, 0)
        self.assertEqual([n for n, _ in problems], list(range(2, 14)))
        by_line = dict(problems)
        self.assertIn("message number", by_line[2])
        self.assertIn("logic number", by_line[3])
        self.assertIn("object index", by_line[4])
        self.assertIn("key prefix", by_line[5])
        self.assertIn("tab", by_line[6])
        self.assertIn("not UTF-8", by_line[7])
        self.assertIn("view number", by_line[8])

    def test_agi_duplicates_by_value(self):
        _, entries, problems = strcheck.check([HDR_AGI + "\n", "m1:2\tA\n", "m01:002\tB\n", "o5\tA\n", "o05\tB\n", "v5\tA\n"])
        self.assertEqual(entries, 5)
        self.assertEqual(problems, [(3, "duplicates line 2 (the later line wins)"), (5, "duplicates line 4 (the later line wins)")])

    def test_agi_files_and_header(self):
        d = tempfile.mkdtemp(dir=os.environ.get("TMPDIR") or ".")
        try:
            p = os.path.join(d, "agi-ko.str")
            with open(p, "wb") as f:
                f.write(b"m0:1\tA\nm0:2\t\xc0\xafx\n")  # the second text is not UTF-8
            self.assertEqual(strcheck.main(["check", p, "--engine", "agi"]), 1)
            with open(p, "wb") as f:
                f.write("m0:1\tA\n".encode("utf-8"))
            self.assertEqual(strcheck.main(["header", p, "--line", HDR_AGI]), 0)
            with open(p, encoding="utf-8") as f:
                self.assertEqual(f.read(), HDR_AGI + "\nm0:1\tA\n")
            self.assertEqual(strcheck.main(["check", p]), 0)  # the engine comes from the header
            with open(p, "wb") as f:
                f.write((HDR_AGI + "\nm0:0\tA\n").encode("utf-8"))
            self.assertEqual(strcheck.main(["check", p]), 1)
        finally:
            for n in os.listdir(d):
                os.remove(os.path.join(d, n))
            os.rmdir(d)

if __name__ == "__main__":
    unittest.main()
