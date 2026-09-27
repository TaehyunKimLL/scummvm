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


if __name__ == "__main__":
    unittest.main()
