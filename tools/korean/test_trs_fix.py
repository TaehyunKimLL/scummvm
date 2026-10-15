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
        self.assertEqual(rows, [(0, b'A\x92', b'A', None)])

    def test_a_fifth_column_is_the_utf8_form(self):
        rows = trs_fix.read_table(['0\t41 83\t41 61\tnote\t41 c3 a2'])
        self.assertEqual(rows, [(0, b'A\x83', b'Aa', b'A\xc3\xa2')])


class Utf8Test(unittest.TestCase):
    def setUp(self):
        self.data = trs_fix.apply(bundle(list(zip(EN, KO))), [(0, KO[0], '팻 시장의 보초'.encode('cp949')),
                                                              (1, KO[1], '가이브러시의 엄마'.encode('cp949'))])

    def test_translations_become_utf8_and_the_rest_stays(self):
        out, raw = trs_fix.to_utf8(self.data)
        self.assertEqual(raw, [])
        recs, rooms, body = stp.parse_trs(out, 'out')
        self.assertEqual(out[body:body + 3], stp.BOM)
        self.assertEqual(rooms, {1: {7: (0, 2)}})
        lines = trs_fix.lines(out)
        for i, (o, t) in trs_fix.lines(self.data).items():
            self.assertEqual(lines[i][0], o)                       # keys byte for byte
            self.assertEqual(lines[i][1], t.decode('cp949').encode('utf-8'))
        self.assertEqual([r[0] for r in recs], [r[0] for r in stp.parse_trs(self.data, 'in')[0]])   # index order

    def test_escapes_and_their_high_arguments_are_kept(self):
        d = bundle([(b'a\xffb\x80\x81z', b'\xbc\xbc\xff\x04\xc0\xce\x07\x07')])
        out, raw = trs_fix.to_utf8(d)
        self.assertEqual(trs_fix.lines(out)[0][1], '세'.encode() + b'\xff\x04\xc0\xce\x07\x07')
        self.assertEqual(raw, [])

    def test_a_raw_high_byte_is_reported_with_its_line(self):
        d = bundle([(b'x', b'\xbc\xbc'), (b'St.\xfaK', b'\xc6\xae\xfa\xc5\xb0 \xfe\x01')])
        out, raw = trs_fix.to_utf8(d)
        self.assertEqual([(r[0], r[1]) for r in raw], [(1, 0xFA)])
        self.assertEqual(trs_fix.lines(out)[1][1], '트'.encode() + b'\xfa' + '키'.encode() + b' \xfe\x01')

    def test_the_utf8_form_of_a_fix(self):
        d = bundle([(b'papier-mache', b'Papier-M\x83ch\x83')])
        fixes = [(0, b'\x83ch\x83', b'ache', '\u00e2ch\u00e9'.encode())]
        out, raw = trs_fix.to_utf8(trs_fix.apply(d, fixes), fixes)
        self.assertEqual(trs_fix.lines(out)[0][1], 'Papier-M\u00e2ch\u00e9'.encode())
        self.assertEqual(trs_fix.lines(trs_fix.apply(d, fixes))[0][1], b'Papier-Mache')   # the CP949 path

    def test_compare_lists_only_the_intended_differences(self):
        d = bundle([(b'a', '팻 시장'.encode('cp949')), (b'papier', b'Papier-M\x83ch\x83')])
        fixes = [(1, b'\x83ch\x83', b'ache', '\u00e2ch\u00e9'.encode())]
        fixed = trs_fix.apply(d, fixes)
        out, _ = trs_fix.to_utf8(fixed, fixes)
        self.assertEqual(trs_fix.compare(fixed, out), [(1, 'Papier-Mache', 'Papier-M\u00e2ch\u00e9')])
        self.assertEqual(trs_fix.compare(fixed, trs_fix.to_utf8(fixed)[0]), [])

    def test_a_bundle_that_is_utf8_already_is_refused(self):
        out, _ = trs_fix.to_utf8(self.data)
        with self.assertRaises(SystemExit):
            trs_fix.to_utf8(out)


if __name__ == '__main__':
    unittest.main()
