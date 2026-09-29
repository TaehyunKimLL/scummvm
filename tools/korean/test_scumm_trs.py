#!/usr/bin/env python3
"""python3 tools/korean/test_scumm_trs.py"""

import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import scumm_trs_from_patch as T  # noqa: E402
import scummscript  # noqa: E402

GROUPS = {
    (0, (2 << 16) | 22): {b'Open': '열기'.encode(), b'Look at': '관찰하기'.encode()},
    (38, 1 << 16): {b'lookout': '망루지기'.encode(), b'Hi!\xff\x03Bye.': '안녕!\xff\x03잘 가.'.encode()},
    (38, (3 << 16) | 202): {b'lookout': '망보는 이'.encode()},
}


class RoundTrip(unittest.TestCase):
    def test_every_key_comes_back_from_its_range(self):
        data = T.write_trs(GROUPS)
        self.assertEqual(data[:8], b'SCVMTRS ')
        look = T.trs_lookup(data)
        for (room, key), lines in GROUPS.items():
            for orig, trans in lines.items():
                self.assertEqual(look(orig, (room, key)), trans)
        self.assertIsNone(look(b'Close', (0, (2 << 16) | 22)))

    def test_read_trs_sees_the_bom_and_the_ranges(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, 'korean.trs')
            with open(p, 'wb') as f:
                f.write(T.write_trs(GROUPS))
            recs, utf8 = T.read_trs(p)
        self.assertTrue(utf8)
        self.assertIn(((38, (3 << 16) | 202), b'lookout', '망보는 이'.encode()), recs)


class BadInput(unittest.TestCase):
    def test_wrong_magic(self):
        with self.assertRaisesRegex(SystemExit, 'not a SCVMTRS'):
            T.trs_lookup(b'SCVMTRX ' + T.write_trs(GROUPS)[8:], 'x.trs')

    def test_truncated_header(self):
        data = T.write_trs(GROUPS)
        for cut in (9, 30, 60):
            with self.assertRaisesRegex(SystemExit, 'x.trs: truncated at offset'):
                T.trs_lookup(data[:cut], 'x.trs')

    def test_truncated_body(self):
        data = T.write_trs(GROUPS)
        with self.assertRaisesRegex(SystemExit, 'x.trs: (string at offset|string offset)'):
            T.trs_lookup(data[:-3], 'x.trs')

    def test_too_many_rooms(self):
        with self.assertRaisesRegex(SystemExit, 'rooms 0..255'):
            T.write_trs({(300, 1 << 16): {b'a': b'b'}})

    def test_missing_paths(self):
        with self.assertRaisesRegex(SystemExit, 'no such file'):
            T.read_trs('/nonexistent/korean.trs')
        with self.assertRaisesRegex(SystemExit, 'no such game folder'):
            scummscript.game_strings('/nonexistent/mi1vga')
        r = subprocess.run([sys.executable, os.path.join(HERE, 'scumm_trs_from_patch.py'),
                            '--english', '/nonexistent/mi1vga', '-o', '/dev/null'],
                           capture_output=True, text=True)
        self.assertEqual(r.returncode, 1)
        self.assertIn('--english /nonexistent/mi1vga: no such folder', r.stderr)
        self.assertNotIn('Traceback', r.stderr)


if __name__ == '__main__':
    unittest.main()
