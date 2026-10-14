#!/usr/bin/env python3
"""Tests for trs_fix.py: byte fixes to a SCVMTRS bundle's translations."""
import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scumm_trs_from_patch as stp  # noqa: E402
import trs_fix  # noqa: E402


def bundle(pairs):
    """A CP949 (no BOM) bundle, one room 1 script 7 range over every line, strings in file order."""
    n = len(pairs)
    header = 8 + 2 + n * 10 + 1 + 3 + 8
    body = bytearray()
    index = bytearray()
    for i, (o, t) in enumerate(pairs):
        o1 = header + len(body)
        body += o + b'\0'
        o2 = header + len(body)
        body += t + b'\0'
        index += struct.pack('<HII', i, o1, o2)
    return bytes(b'SCVMTRS ' + struct.pack('<H', n) + index + bytes([1, 1]) + struct.pack('<H', 1)
                 + struct.pack('<IHH', 7, 0, n - 1) + body)


EN = [b'governor phatt\x92s guard', b'guybrush\x92s mom', b'hammer']
KO = ['팻 시장'.encode('cp949') + b'\x92' + '의 보초'.encode('cp949'),
      '가이브러시'.encode('cp949') + b'\x92' + '의 엄마'.encode('cp949'), '망치'.encode('cp949')]


class TrsFixTest(unittest.TestCase):
    def setUp(self):
        self.data = bundle(list(zip(EN, KO)))

    def test_a_shorter_translation_moves_the_later_strings(self):
        new = trs_fix.apply(self.data, [(0, KO[0], '팻 시장의 보초'.encode('cp949'))])
        lines = trs_fix.lines(new)
        self.assertEqual(lines[0], (EN[0], '팻 시장의 보초'.encode('cp949')))
        self.assertEqual(lines[1], (EN[1], KO[1]))
        self.assertEqual(lines[2], (EN[2], KO[2]))
        self.assertEqual(len(new), len(self.data) - 1)
        stp.parse_trs(new, 'fixed')   # offsets valid, ranges intact

    def test_the_old_bytes_must_match(self):
        with self.assertRaises(SystemExit):
            trs_fix.apply(self.data, [(1, b'nope', b'x')])

    def test_a_longer_translation_and_the_room_table_survive(self):
        new = trs_fix.apply(self.data, [(2, KO[2], '쇠망치'.encode('cp949'))])
        self.assertEqual(trs_fix.lines(new)[2][1], '쇠망치'.encode('cp949'))
        _recs, rooms, _ = stp.parse_trs(new, 'fixed')
        self.assertEqual(rooms, {1: {7: (0, 2)}})

    def test_the_table_reads_hex_and_skips_comments(self):
        rows = trs_fix.read_table(['# line\told\tnew\tnote', '0\t41 92\t41\tdrop the apostrophe', ''])
        self.assertEqual(rows, [(0, b'A\x92', b'A')])


if __name__ == '__main__':
    unittest.main()
