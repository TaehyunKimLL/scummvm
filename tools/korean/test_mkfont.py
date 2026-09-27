#!/usr/bin/env python3
"""mkfont.py 의 기준선 규칙 시험.

  python3 tools/korean/test_mkfont.py
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkfont  # noqa: E402

APPLE_GOTHIC = "/System/Library/Fonts/Supplemental/AppleGothic.ttf"


class AscentTest(unittest.TestCase):
    def test_face_that_fits_keeps_its_own_ascent(self):
        self.assertEqual(mkfont.choose_ascent_from(12, 4, -12, 3, 16, latin=True), 12)
        self.assertEqual(mkfont.choose_ascent_from(12, 4, -12, 3, 16, latin=False), 12)

    def test_ink_that_fits_is_centred(self):
        # 줄은 셀보다 크지만 잉크는 들어간다: 가운데 놓는다.
        self.assertEqual(mkfont.choose_ascent_from(15, 6, -12, 2, 16, latin=True), 13)

    def test_latin_ink_taller_than_the_cell_keeps_the_capitals(self):
        # AppleGothic 16px: 줄 15+6, 'A' 위 -13, 'g' 아래 4 - 17줄이 16줄 셀에.
        # 대문자 위를 셀 맨 위에 두고 넘치는 한 줄은 내림자에서 잘린다.
        self.assertEqual(mkfont.choose_ascent_from(15, 6, -13, 4, 16, latin=True), 13)

    def test_cjk_ink_taller_than_the_cell_is_unchanged(self):
        # 한글 세트는 예전 규칙 그대로다 (글리프마다 셀 안으로 밀어 넣는다).
        self.assertEqual(mkfont.choose_ascent_from(15, 6, -13, 4, 16, latin=False), 10)


@unittest.skipUnless(os.path.exists(APPLE_GOTHIC), "AppleGothic.ttf not installed")
class AppleGothicLatinTest(unittest.TestCase):
    """C6 G2: ',' '.' 'r' 'l' sat on different baselines in a --latin bake."""

    def test_one_baseline_for_every_letter(self):
        from PIL import ImageFont
        font = ImageFont.truetype(APPLE_GOTHIC, 16)
        ascent = mkfont.choose_ascent(font, 16, latin=True)
        bottoms = {}
        for ch in "Hlr,.xo":
            img, _, _ = mkfont.render(font, ch, 16, 16, ascent, 8, center=True)
            # 잉크가 기준선 위에서 끝나는 줄: 기준선이 같으면 모두 같다.
            box = img.point(lambda v: 255 if v >= 128 else 0).getbbox()
            bottoms[ch] = box[3]
        self.assertEqual(len({bottoms[c] for c in "Hlrxo"}), 1, bottoms)
        # 쉼표와 마침표는 기준선에 닿거나 그 아래로 내려간다.
        self.assertGreaterEqual(bottoms[","], bottoms["H"], bottoms)
        self.assertGreaterEqual(bottoms["."], bottoms["H"] - 1, bottoms)


class RangeTest(unittest.TestCase):
    """C28: --unicode 목록. 16진 범위와 이름 붙인 묶음."""

    def test_hex_ranges_in_order_without_duplicates(self):
        self.assertEqual(mkfont.parse_ranges("0041-0043,0020,0042"), [0x41, 0x42, 0x43, 0x20])

    def test_named_sets(self):
        hangul = mkfont.parse_ranges("hangul")
        self.assertEqual(len(hangul), 11172)
        self.assertEqual((hangul[0], hangul[-1]), (0xAC00, 0xD7A3))
        self.assertEqual(mkfont.parse_ranges("ascii"), list(range(0x20, 0x7F)))
        # KS X 1001 전체: 완성형 2350자, 한자 4888자, 기호와 낱자.
        ks = set(mkfont.parse_ranges("ksx1001"))
        self.assertIn(0xAC00, ks)
        self.assertIn(0x4E00, ks)       # 一
        self.assertIn(0x3131, ks)       # ㄱ
        self.assertNotIn(0xB620, ks)    # 똠: 11172 에는 있지만 2350 에는 없다
        self.assertEqual(len([c for c in ks if 0xAC00 <= c <= 0xD7A3]), 2350)
        self.assertEqual(len(mkfont.parse_ranges("ascii,hangul,ascii")), 95 + 11172)

    def test_blank_code_points_are_kept_only_when_the_face_has_them(self):
        # 공백은 잉크가 없어도 싣는다. 하지만 글꼴에 없어 .notdef 로 그려졌다면
        # 공백 자리에 네모를 구워 넣지 않는다 (C28 리뷰).
        self.assertTrue(mkfont.unicode_keep(0x20, 0, False))
        self.assertFalse(mkfont.unicode_keep(0x3000, 7, True))
        self.assertFalse(mkfont.unicode_keep(0xA0, 0, True))
        self.assertTrue(mkfont.unicode_keep(0xAC00, 7, False))
        self.assertFalse(mkfont.unicode_keep(0xAC00, 7, True))
        self.assertFalse(mkfont.unicode_keep(0xAC00, 0, False))

    def test_unknown_name_is_an_error(self):
        with self.assertRaises(ValueError):
            mkfont.parse_ranges("klingon")


def _galmuri(name):
    d = os.environ.get("SCUMMVM_TEST_PIXEL_FONT_DIR")
    if not d and os.environ.get("SCUMMVM_TEST_I18N_DATA"):
        d = os.path.join(os.environ["SCUMMVM_TEST_I18N_DATA"], "..", "fonts", "pixel", "galmuri")
    return os.path.join(d, name) if d else ""


GALMURI7 = _galmuri("Galmuri7.ttf")


@unittest.skipUnless(GALMURI7 and os.path.exists(GALMURI7), "Galmuri7.ttf not found (SCUMMVM_TEST_PIXEL_FONT_DIR)")
class UnicodeBakeTest(unittest.TestCase):
    """C28: --unicode 로 구운 SVFN 버전 2 는 11172자 전부를 싣는다."""

    def bake(self, *extra):
        import struct
        import subprocess
        import tempfile
        out = tempfile.NamedTemporaryFile(suffix=".fnt", delete=False).name
        try:
            subprocess.run([sys.executable, mkfont.__file__, GALMURI7, out, "--size", "8", "--cell", "9",
                            "--bpp", "1"] + list(extra), check=True, capture_output=True)
            data = open(out, "rb").read()
        finally:
            os.unlink(out)
        hdr = struct.unpack_from("<4sHHBBHHBBBBHIII", data, 0)
        magic, version, flags, bpp, _, _, count, cw, ch, asc = hdr[:10]
        metrics_off, data_off, data_size = hdr[12:15]
        cmap_off = struct.unpack_from("<I", data, 32)[0]
        cmap = {}
        for i in range(count):
            cp, idx = struct.unpack_from("<II", data, cmap_off + 8 * i)
            cmap[cp] = idx
        stride = (cw + 7) // 8 * ch
        return dict(magic=magic, version=version, flags=flags, count=count, cell=(cw, ch),
                    cmap=cmap, data=data, data_off=data_off, data_size=data_size, stride=stride,
                    metrics_off=metrics_off)

    def ink(self, f, cp):
        i = f["cmap"][cp]
        g = f["data"][f["data_off"] + i * f["stride"]:f["data_off"] + (i + 1) * f["stride"]]
        return sum(bin(b).count("1") for b in g)

    def test_every_syllable_is_baked(self):
        f = self.bake("--unicode", "ascii,hangul")
        self.assertEqual(f["magic"], b"SVFN")
        self.assertEqual(f["version"], 2)
        self.assertEqual(f["cell"], (9, 9))
        self.assertEqual(f["data_size"], f["count"] * f["stride"])
        for cp in (0xAC00, 0xAC01, 0xBDC1, 0xB620, 0xD7A3, 0x41):
            self.assertIn(cp, f["cmap"])
            self.assertGreater(self.ink(f, cp), 0, hex(cp))
        self.assertEqual(len([c for c in f["cmap"] if 0xAC00 <= c <= 0xD7A3]), 11172)
        self.assertIn(0x20, f["cmap"])   # 공백은 잉크가 없어도 싣는다

    def test_code_points_the_face_lacks_are_left_out(self):
        # Galmuri7 에는 타이 문자가 없다: 빈 글리프를 실으면 대체 글꼴로
        # 넘어가지 못하므로 목록에서 뺀다.
        f = self.bake("--unicode", "0041,0E01")
        self.assertIn(0x41, f["cmap"])
        self.assertNotIn(0x0E01, f["cmap"])
        self.assertEqual(f["count"], 1)


if __name__ == "__main__":
    unittest.main()
