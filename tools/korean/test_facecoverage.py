#!/usr/bin/env python3
"""Tests for facecoverage.py: a map's face chains against the characters a bundle draws."""
import os
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import facecoverage as fc  # noqa: E402
import test_trs_fix as tf  # noqa: E402


def svf(cps, cell=8, bpp=1, ascent=6):
    """A minimal SVFN v2 file holding the code points (every glyph blank)."""
    stride = ((cell * bpp + 7) // 8) * cell
    cmap = b''.join(struct.pack('<II', cp, i) for i, cp in enumerate(sorted(cps)))
    head = struct.pack('<4sHHBBHHBBBBHIII', b'SVFN', 2, 0, bpp, 255, 0, len(cps), cell, cell, ascent, 0, 0, 0,
                       36 + len(cmap), stride * len(cps))
    return head + struct.pack('<I', 36) + cmap + bytes(stride * len(cps))


ASCII = set(range(0x20, 0x7F))
KS = {ord(c) for c in (bytes([h, l]).decode('cp949') for h in range(0xB0, 0xC9) for l in range(0xA1, 0xFF))}

MAP = """[fonts]
dlg=A.SVF
dlgx=AX.SVF
[fonts:clut8]
dlg=B.SVF
[font]
missing=u+25a1
[font.0]
face=dlg,dlgx
[font.2]
face=dlg
[glyphs]
0x5e = u+2026
0x07 = original
[glyphs.2]
0x0f = u+2122
"""


class MapTest(unittest.TestCase):
    def test_chains_and_targets_per_preset(self):
        chains, targets = fc.plan(fc.parse_map(MAP), 'U')
        self.assertEqual(chains, {0: ['A.SVF', 'AX.SVF'], 2: ['A.SVF']})
        self.assertEqual(targets, {0: {0x2026, 0x25A1}, 2: {0x2026, 0x25A1, 0x2122}})
        chains, _ = fc.plan(fc.parse_map(MAP), 'L')
        self.assertEqual(chains[0], ['B.SVF', 'AX.SVF'])        # the 8-bit preset swaps only what it names


class CheckTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)

    def put(self, name, cps):
        with open(os.path.join(self.dir.name, name), 'wb') as f:
            f.write(svf(cps))

    def run_check(self, text, **kw):
        return fc.check(text, MAP, self.dir.name, **kw)

    def test_a_supplement_covers_what_the_base_lacks(self):
        self.put('A.SVF', ASCII | KS | {0x25A1})
        self.put('AX.SVF', {0xE2, 0xE9, 0x2026})
        self.put('B.SVF', ASCII | KS | {0x25A1, 0x2026, 0xE2, 0xE9})
        text = {ord('a'), 0xE2, 0xE9, 0xAC00}
        problems = self.run_check(text, presets=('U',), base_check=True)
        # font 0 chains A + AX: nothing missing; font 2 has A alone
        self.assertEqual([(p[0], p[1], p[2], p[3]) for p in problems], [('U', 2, 'text', [0xE2, 0xE9, 0x2026, 0x2122])])

    def test_font_ids_are_judged_alone(self):
        self.put('A.SVF', ASCII | KS | {0x25A1})
        self.put('AX.SVF', {0xE2, 0x2026, 0x2122})
        problems = self.run_check({0xE2, 0xE9}, presets=('U',))
        self.assertEqual([(p[1], p[3]) for p in problems], [(0, [0xE9]), (2, [0xE2, 0xE9, 0x2026, 0x2122])])

    def test_a_base_without_every_syllable_fails_the_base_check(self):
        self.put('A.SVF', ASCII | set(sorted(KS)[:-1]) | {0x25A1})
        self.put('AX.SVF', {0x2026, 0x2122})
        problems = self.run_check({ord('a')}, presets=('U',), base_check=True)
        kinds = {(p[1], p[2]) for p in problems}
        self.assertIn((0, 'base'), kinds)
        self.assertNotIn((0, 'text'), kinds)

    def test_a_face_that_is_not_there_is_reported(self):
        self.put('A.SVF', ASCII | KS | {0x25A1, 0x2026, 0x2122})
        problems = self.run_check({ord('a')}, presets=('U',))
        self.assertEqual([(p[1], p[2], p[4]) for p in problems], [(0, 'file', 'AX.SVF')])

    def test_the_bundle_text_is_what_is_checked(self):
        data = tf.trs_fix.to_utf8(tf.bundle([(b'x', '팻 시장'.encode('cp949') + b' \xe9')]))[0]
        path = os.path.join(self.dir.name, 'k.trs')
        with open(path, 'wb') as f:
            f.write(data)
        cps = fc.bundle_chars([path])
        self.assertIn(ord('팻'), cps)
        self.assertNotIn(0xFFFD, cps)     # a raw game byte is not a code point to find in a face


class HotReportTest(unittest.TestCase):
    def test_list_coverage_is_counted_per_face_file(self):
        with tempfile.TemporaryDirectory() as d:
            for name, cps in (('A.SVF', ASCII | {0xAC00, 0xAC01, 0xAC02}), ('AX.SVF', {0x2026})):
                with open(os.path.join(d, name), 'wb') as f:
                    f.write(svf(cps))
            os.mkdir(os.path.join(d, 'hot'))
            with open(os.path.join(d, 'hot', 'A.HOT'), 'w', encoding='utf-8') as f:
                f.write('# comment 가\nab\n\ufeff가 \u2026 \ud55c\n')
            text = {0xAC00, 0xAC01, ord('a')}
            rows = fc.hot_report(text, MAP, d, os.path.join(d, 'hot'), ('U',))
            # list: a b 가 U+2026 한 = 5; the face holds a b 가 = 3; the text has 가 U+AC01 a: AC01 and 'a' are held,
            # 'a' is listed, so one more (U+AC01)
            self.assertEqual(rows, [('U', 'A.SVF', 5, 3, 1)])
            self.assertEqual(fc.read_hot(os.path.join(d, 'hot', 'A.HOT')), {ord('a'), ord('b'), 0xAC00, 0x2026, 0xD55C})
            self.assertEqual(fc.hot_report(text, MAP, d, os.path.join(d, 'none'), ('U',)), [])


NEODGM = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'dists', 'svf-fonts', 'ttf', 'neodgm',
                      'neodgm.ttf')
DEJAVU = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'


@unittest.skipUnless(os.path.exists(NEODGM) and os.path.exists(DEJAVU), 'fonts not there')
class SupplementBakeTest(unittest.TestCase):
    def test_a_supplement_holds_what_the_base_lacks_from_the_fallback_face(self):
        import subprocess
        with tempfile.TemporaryDirectory() as d:
            with open(os.path.join(d, 'A.SVF'), 'wb') as f:
                f.write(svf(ASCII | KS | {0x25A1}, cell=16, bpp=1, ascent=13))
            with open(os.path.join(d, 'T.MAP'), 'w') as f:
                f.write('[fonts]\nb=A.SVF\nbx=AX.SVF\n[font]\nmissing=u+25a1\n[font.0]\nface=b,bx\n[glyphs]\n0x0f = u+2122\n')
            with open(os.path.join(d, 'p.tsv'), 'w') as f:
                f.write('AX.SVF\tA.SVF\t%s|%s\t16\n' % (NEODGM, DEJAVU))
            # the text draws a-circumflex (neodgm has it) and the map remaps 0x0f to the trademark sign (neodgm has
            # not: DejaVu draws it)
            trs = os.path.join(d, 'k.trs')
            script = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'bake-supplements.py')
            # a bundle with a-circumflex in it: a fix's UTF-8 form spells it
            fixes = [(0, '가'.encode('cp949'), b'a', '\u00e2'.encode())]
            base = tf.trs_fix.apply(tf.bundle([(b'x', '가'.encode('cp949'))]), fixes)
            with open(trs, 'wb') as f:
                f.write(tf.trs_fix.to_utf8(base, fixes)[0])
            subprocess.run([sys.executable, script, os.path.join(d, 'p.tsv'), '--trs', trs, '--map',
                            os.path.join(d, 'T.MAP'), '--faces', d], check=True, capture_output=True, text=True)
            self.assertEqual(sorted(fc.face_chars(os.path.join(d, 'AX.SVF'))), [0xE2, 0x2122])
            with open(os.path.join(d, 'T.MAP')) as f:
                self.assertEqual(fc.check(fc.bundle_chars([trs]), f.read(), d, presets=('U',), base_check=True), [])


MAPS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'dists', 'engine-data', 'hires_text', 'dos')
SHIPPED = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'dists', 'svf-fonts', 'svf', 'dos')


@unittest.skipUnless(os.path.exists(os.path.join(SHIPPED, 'NEODG16B.SVF')), 'shipped faces not there')
class JamoSetTest(unittest.TestCase):
    def test_a_jamo_set_composes_every_syllable_and_a_precomposed_base_has_all_2350(self):
        self.assertEqual(len([c for c in fc.face_chars(os.path.join(SHIPPED, 'NEODG16B.SVF')) if 0xAC00 <= c <= 0xD7A3]), 11172)
        base = fc.base_set()
        self.assertFalse(base - fc.face_chars(os.path.join(SHIPPED, 'NEODG16B.SVF')))
        self.assertFalse(base - fc.face_chars(os.path.join(SHIPPED, 'NGOTB16B.SVF')))

    def test_every_shipped_map_of_the_two_games_has_a_base_and_covers_its_bundle(self):
        # the bundles live outside the repo (gamedata); the shipped maps' own claim is checked whenever they are there
        for game, trs in (('M1KO.MAP', 'mi1ute-kor'), ('M2KO.MAP', 'mi2kor')):
            path = os.path.join(os.path.expanduser('~/work/scummvm/gamedata'), trs, 'korean.trs')
            if not os.path.exists(path):
                continue
            with open(os.path.join(MAPS, game), encoding='utf-8') as f:
                self.assertEqual(fc.check(fc.bundle_chars([path]), f.read(), SHIPPED, base_check=True), [], game)


if __name__ == '__main__':
    unittest.main()
