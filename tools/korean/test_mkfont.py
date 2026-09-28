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


class TwoBppTest(unittest.TestCase):
    """M2: --bpp 2. 0-255 커버리지를 4단계로, 네 픽셀을 한 바이트에 (왼쪽이 상위 비트 쌍)."""

    def test_quantize_is_round_to_nearest_level(self):
        self.assertEqual([mkfont.quantize2(v) for v in (0, 42, 43, 127, 128, 212, 213, 255)],
                         [0, 0, 1, 1, 2, 2, 3, 3])
        # 읽는 쪽은 단계 x 85: 어느 값도 43 넘게 틀어지지 않는다.
        self.assertLessEqual(max(abs(mkfont.quantize2(v) * 85 - v) for v in range(256)), 43)

    def test_pack_glyph_packs_four_pixels_a_byte_msb_first(self):
        from PIL import Image
        img = Image.new("L", (6, 2), 0)
        for x, v in enumerate((0, 85, 170, 255, 255, 170)):
            img.putpixel((x, 1), v)
        out = mkfont.pack_glyph(img, 6, 2, 2)
        # 6 px 는 12비트: 행마다 2바이트, 둘째 바이트의 아래 4비트는 채움.
        self.assertEqual(out, bytes([0x00, 0x00, 0x1B, 0xE0]))

    def test_cp949_is_every_code_it_decodes_and_ascii(self):
        cps = mkfont.parse_ranges("cp949")
        self.assertEqual(len(cps), len(set(cps)))
        self.assertEqual(cps[:95], list(range(0x20, 0x7F)))
        s = set(cps)
        self.assertEqual(len([c for c in s if 0xAC00 <= c <= 0xD7A3]), 11172)
        self.assertIn(0xB620, s)        # 똠: 2350 에 없는 음절도 cp949 에는 있다
        self.assertIn(0x4E00, s)
        self.assertLessEqual(set(mkfont.parse_ranges("ksx1001")), s)
        self.assertEqual(len(cps), 17143)


def _nanum_bold():
    for p in (os.environ.get("SCUMMVM_TEST_KO_BOLD_TTF", ""),
              os.path.expanduser("~/.local/sysroot/usr/share/fonts/truetype/nanum/NanumGothicBold.ttf"),
              "/usr/share/fonts/truetype/nanum/NanumGothicBold.ttf"):
        if p and os.path.exists(p):
            return p
    return ""


NANUM_BOLD = _nanum_bold()


def read_svfn(data):
    """SVFN 을 bitmap_font.cpp 처럼 읽는다: {코드 포인트: (폭 표, [행마다 커버리지 0-255])}.
    버전 2 (cmap) 만. 픽셀은 TextCompose::expandCoverage 순서로 펼친다."""
    import struct
    hdr = struct.unpack_from("<4sHHBBHHBBBBHIII", data, 0)
    magic, version, flags, bpp, _, _, count, cw, ch = hdr[:9]
    metrics_off, data_off = hdr[12:14]
    assert magic == b"SVFN" and version == 2 and bpp in (1, 2, 8)
    pitch = {1: (cw + 7) // 8, 2: (cw + 3) // 4, 8: cw}[bpp]
    cmap_off = struct.unpack_from("<I", data, 32)[0]

    def px(row, x):
        if bpp == 1:
            return 255 if row[x >> 3] & (0x80 >> (x & 7)) else 0
        if bpp == 2:
            return ((row[x >> 2] >> (6 - (x & 3) * 2)) & 3) * 85
        return row[x]

    out = {}
    for i in range(count):
        cp, idx = struct.unpack_from("<II", data, cmap_off + 8 * i)
        g = data_off + idx * pitch * ch
        rows = [[px(data[g + y * pitch:g + (y + 1) * pitch], x) for x in range(cw)] for y in range(ch)]
        m = data[metrics_off + 4 * idx:metrics_off + 4 * idx + 4] if metrics_off else b""
        out[cp] = (m, rows)
    return dict(bpp=bpp, cell=(cw, ch), pitch=pitch, glyphs=out)


@unittest.skipUnless(NANUM_BOLD, "NanumGothicBold.ttf not found (SCUMMVM_TEST_KO_BOLD_TTF)")
class TwoBppBakeTest(unittest.TestCase):
    """같은 TTF, 같은 크기를 8bpp 와 2bpp 로 구우면 같은 글자, 같은 폭, 픽셀마다
    커버리지 차 43 이하 (단계 양자화 오차)."""

    UNICODE = "ascii,AC00-AC40,B620,D7A3,4E00,3131,25A1"

    def bake(self, bpp):
        import subprocess
        import tempfile
        out = tempfile.NamedTemporaryFile(suffix=".fnt", delete=False).name
        try:
            subprocess.run([sys.executable, mkfont.__file__, NANUM_BOLD, out, "--size", "18",
                            "--bpp", str(bpp), "--unicode", self.UNICODE], check=True, capture_output=True)
            with open(out, "rb") as f:
                return read_svfn(f.read())
        finally:
            os.unlink(out)

    def test_2bpp_is_8bpp_within_one_half_level(self):
        f8, f2 = self.bake(8), self.bake(2)
        self.assertEqual(f2["bpp"], 2)
        self.assertEqual(f2["cell"], (18, 18))
        self.assertEqual(f2["pitch"], 5)
        self.assertEqual(sorted(f8["glyphs"]), sorted(f2["glyphs"]))
        worst = 0
        levels = set()
        for cp, (m8, rows8) in f8["glyphs"].items():
            m2, rows2 = f2["glyphs"][cp]
            self.assertEqual(m8[0], m2[0], hex(cp))     # 같은 advance
            for r8, r2 in zip(rows8, rows2):
                for a, b in zip(r8, r2):
                    worst = max(worst, abs(a - b))
                    levels.add(b)
        self.assertLessEqual(worst, 43)
        self.assertEqual(levels, {0, 85, 170, 255})
        print(f"\n  2bpp vs 8bpp, {len(f8['glyphs'])} glyphs at 18px: max per-pixel diff {worst}",
              file=sys.stderr)


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
