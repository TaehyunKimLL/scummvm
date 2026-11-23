#!/usr/bin/env python3
"""Tests of strcheck.py (run: python3 -I tools/korean/test_strcheck.py)."""

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import strcheck  # noqa: E402

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


if __name__ == "__main__":
    unittest.main()
