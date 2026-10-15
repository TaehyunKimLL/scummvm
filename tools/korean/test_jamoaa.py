#!/usr/bin/env python3
"""jamoaa.py tests: anti-aliased (2 bpp) jamo sets from an outline font.

  python3 tools/korean/test_jamoaa.py        (also run by test_jamoset.py)

The outline tests build a small TrueType font with fontTools (a few dozen syllables drawn from fixed jamo shapes on
whole and half pixels, an initial with a hole), so they need fontTools and nothing else.
"""

import json
import math
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import jamoaa  # noqa: E402
import jamoset  # noqa: E402

try:
    import fontTools
    FONTTOOLS = True
    FT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(fontTools.__file__)))
except ImportError:
    FONTTOOLS = False
    FT_DIR = None


def gint(pixels, w):
    g = 0
    for x, y in pixels:
        g |= 1 << (y * (w + 1) + x)
    return g


def raster(rects, w, h):
    """Coverage list of axis-aligned rectangles (x0, y0, x1, y1) in pixel units, y down (exact area)."""
    out = [0.0] * (w * h)
    for x0, y0, x1, y1 in rects:
        for y in range(h):
            for x in range(w):
                ox = max(0.0, min(x + 1, x1) - max(x, x0))
                oy = max(0.0, min(y + 1, y1) - max(y, y0))
                out[y * w + x] = min(1.0, out[y * w + x] + ox * oy)
    return out


def rect_poly(x0, y0, x1, y1, reverse=False):
    """A rectangle as a polyline in pen-relative pixels, y up (the rasteriser's input), from y-down cell coordinates
    with the pen at (0, 0) and baseline row 0 (so y_up = -y_down)."""
    pts = [(x0, -y0), (x1, -y0), (x1, -y1), (x0, -y1)]
    return pts[::-1] if reverse else pts


class Quantise(unittest.TestCase):
    def test_same_as_mkfont_for_every_byte(self):
        import mkfont
        for v in range(256):
            self.assertEqual(jamoaa.quantize2(v), mkfont.quantize2(v), v)

    def test_coverage_to_level(self):
        self.assertEqual([jamoaa.level(c) for c in (0.0, 0.16, 0.17, 0.5, 0.83, 0.84, 1.0, 1.2)],
                         [0, 0, 1, 2, 2, 3, 3, 3])


class Rasteriser(unittest.TestCase):
    W = H = 8

    def cov(self, polys, ox=0, oy=0):
        acc = [0.0] * (self.W * self.H)
        for p in polys:
            for i, v in enumerate(jamoaa.rasterize(p, ox, oy, self.W, self.H)):
                acc[i] += v
        return jamoaa.coverage(acc)

    def assertCov(self, got, want):
        for i, (a, b) in enumerate(zip(got, want)):
            self.assertAlmostEqual(a, b, places=6, msg=f"pixel {i % self.W},{i // self.W}")

    def test_whole_pixel_square(self):
        self.assertCov(self.cov([rect_poly(1, 2, 3, 4)]), raster([(1, 2, 3, 4)], 8, 8))

    def test_half_pixel_edges(self):
        self.assertCov(self.cov([rect_poly(1.5, 2, 3, 4.5)]), raster([(1.5, 2, 3, 4.5)], 8, 8))

    def test_reversed_inner_contour_is_a_hole(self):
        got = self.cov([rect_poly(1, 1, 7, 7), rect_poly(2, 2, 6, 6, reverse=True)])
        want = raster([(1, 1, 7, 2), (1, 6, 7, 7), (1, 2, 2, 6), (6, 2, 7, 6)], 8, 8)
        self.assertCov(got, want)

    def test_either_winding_fills(self):
        self.assertCov(self.cov([rect_poly(1, 2, 3, 4, reverse=True)]), raster([(1, 2, 3, 4)], 8, 8))

    def test_overlap_of_same_direction_contours_is_one(self):
        got = self.cov([rect_poly(1, 1, 4, 4), rect_poly(2, 2, 5, 5)])
        self.assertAlmostEqual(got[2 * 8 + 2], 1.0)
        self.assertAlmostEqual(sum(got), 9 + 9 - 4)

    def test_origin_and_clipping(self):
        # pen at column 2, baseline row 6: a 2x2 box above the baseline lands on rows 4..5, columns 3..4
        got = self.cov([[(1, 0), (3, 0), (3, 2), (1, 2)]], ox=2, oy=6)
        self.assertCov(got, raster([(3, 4, 5, 6)], 8, 8))
        # reaching outside the cell on every side: what is inside is still exact
        got = self.cov([rect_poly(-3, -2, 11, 3.5)])
        self.assertCov(got, raster([(0, 0, 8, 3.5)], 8, 8))

    def test_slanted_edge_area(self):
        tri = [(0.5, -0.5), (7.5, -0.5), (0.5, -7.5)]
        got = self.cov([tri])
        self.assertAlmostEqual(sum(got), 7 * 7 / 2, places=5)

    def test_contours_add_linearly(self):
        a, b = rect_poly(1, 1, 3, 3), rect_poly(2, 2, 6, 6)
        ra, rb = jamoaa.rasterize(a, 0, 0, 8, 8), jamoaa.rasterize(b, 0, 0, 8, 8)
        both = jamoaa.coverage([x + y for x, y in zip(ra, rb)])
        self.assertCov(both, self.cov([a, b]))


class Grouping(unittest.TestCase):
    def test_hole_joins_its_outer_contour_others_stay_apart(self):
        w = h = 10
        outer = jamoaa.rasterize(rect_poly(1, 1, 6, 6), 0, 0, w, h)
        inner = jamoaa.rasterize(rect_poly(2, 2, 5, 5, reverse=True), 0, 0, w, h)
        bar = jamoaa.rasterize(rect_poly(7, 1, 8, 9), 0, 0, w, h)
        groups = jamoaa.group_contours([bar, inner, outer])
        self.assertEqual(sorted(sorted(g) for g in groups), [[0], [1, 2]])

    def test_overlapping_strokes_are_not_nested(self):
        w = h = 10
        a = jamoaa.rasterize(rect_poly(1, 1, 8, 3), 0, 0, w, h)
        b = jamoaa.rasterize(rect_poly(4, 1, 6, 9), 0, 0, w, h)
        self.assertEqual(sorted(sorted(g) for g in jamoaa.group_contours([a, b])), [[0], [1]])


class Assignment(unittest.TestCase):
    W, H = 12, 8

    def test_group_goes_to_the_piece_whose_region_it_inks(self):
        w, h = self.W, self.H
        cores = [gint([(x, y) for x in range(1, 5) for y in range(1, 7)], w),
                 gint([(x, y) for x in range(7, 9) for y in range(0, 8)], w), None]
        # the initial's contour lies a little outside its fitted region: still the initial's
        left = jamoaa.coverage(jamoaa.rasterize(rect_poly(0.5, 1, 5.5, 6), 0, 0, w, h))
        right = jamoaa.coverage(jamoaa.rasterize(rect_poly(7, 0, 9, 8), 0, 0, w, h))
        slots, info = jamoaa.assign([left, right], cores, w, h)
        self.assertEqual(info["straddled"], 0)
        self.assertEqual(slots[0], left)
        self.assertEqual(slots[1], right)
        self.assertEqual(slots[2], [0.0] * (w * h))

    def test_fitted_pixels_moved_between_pieces_do_not_split_a_contour(self):
        # the fit gave one pixel of the initial's stroke to the medial piece: the contour stays whole
        w, h = self.W, self.H
        init = [(x, y) for x in range(1, 5) for y in range(1, 7)]
        cores = [gint(init[1:], w), gint([init[0]] + [(x, y) for x in range(7, 9) for y in range(0, 8)], w), None]
        left = jamoaa.coverage(jamoaa.rasterize(rect_poly(1, 1, 5, 7), 0, 0, w, h))
        slots, info = jamoaa.assign([left], cores, w, h)
        self.assertEqual(slots[0], left)
        self.assertEqual(info["straddled"], 0)

    def test_merged_outline_is_split_by_the_regions(self):
        # one contour that draws both jamo (an L joining the initial's stem to the medial's bar)
        w, h = self.W, self.H
        cores = [gint([(x, y) for x in range(1, 3) for y in range(0, 8)], w),
                 gint([(x, y) for x in range(4, 10) for y in range(6, 8)], w), None]
        poly = [(1, 0), (3, 0), (3, -6), (10, -6), (10, -8), (1, -8)]
        cov = jamoaa.coverage(jamoaa.rasterize(poly, 0, 0, w, h))
        slots, info = jamoaa.assign([cov], cores, w, h)
        self.assertEqual(info["straddled"], 1)
        for y in range(h):
            for x in range(w):
                i = y * w + x
                if cov[i] == 0:
                    continue
                if x == 3:		# a pixel next to both regions: either may take it, one must
                    self.assertEqual(max(slots[0][i], slots[1][i]), cov[i], (x, y))
                elif x < 3:
                    self.assertEqual((slots[0][i], slots[1][i]), (cov[i], 0.0), (x, y))
                else:
                    self.assertEqual((slots[0][i], slots[1][i]), (0.0, cov[i]), (x, y))


class Consensus(unittest.TestCase):
    def test_per_pixel_median_ignores_one_odd_donor(self):
        a = bytes([0, 255, 128, 0])
        b = bytes([0, 255, 120, 0])
        odd = bytes([255, 0, 124, 0])
        self.assertEqual(jamoaa.median_consensus([a, b, odd]), [0, 255, 124, 0])

    def test_even_count_takes_the_mean_of_the_middle_two(self):
        self.assertEqual(jamoaa.median_consensus([bytes([10]), bytes([20]), bytes([30]), bytes([200])]), [25])


class Levels2bpp(unittest.TestCase):
    def test_pack_round_trip_and_layout(self):
        w, h = 5, 2
        lv = [0, 1, 2, 3, 1, 3, 3, 0, 0, 2]
        b = jamoaa.pack_levels(lv, w, h)
        self.assertEqual(len(b), 2 * 2)
        self.assertEqual(b[0], 0b00011011)		# leftmost pixel in the top bits
        self.assertEqual(b[1], 0b01000000)
        self.assertEqual(jamoaa.unpack_levels(b, w, h, 2), lv)

    def test_written_2bpp_face_parses_and_composes_by_the_maximum(self):
        w = h = 16
        t = jamoset.tables_for(jamoset.LAYOUTS["844"])
        used = jamoset.used_pieces(t)
        mapping = {p: n for n, p in enumerate(used)}
        t = jamoset.renumber(t, mapping)
        cho, jung = jamoset.pieces_of(t, 0xAC00)		# 가
        lv_a, lv_b = [0] * (w * h), [0] * (w * h)
        lv_a[5 * w + 4], lv_a[5 * w + 5] = 3, 1		# pixels 4 and 5 share a byte at 2 bpp
        lv_b[5 * w + 5] = 2
        glyphs = []
        for n in range(len(used)):
            lv = lv_a if n == cho else lv_b if n == jung else [0] * (w * h)
            glyphs.append((jamoset.PIECE_BASE + n, jamoaa.pack_levels(lv, w, h), w, 0, 0))
        out = jamoset.write_svfn(w, h, 13, glyphs, jamoset.pack(t), bpp=2)
        f = jamoset.parse_svfn(out)
        self.assertEqual(f.bpp, 2)
        self.assertEqual(len(f.glyphs[jamoset.PIECE_BASE]), 4 * 16)
        got = jamoaa.unpack_levels(jamoset.compose(f, jamoset.unpack(f.jset), 0xAC00), w, h, 2)
        self.assertEqual((got[5 * w + 4], got[5 * w + 5]), (3, 2))
        self.assertEqual(sum(got), 5)
        self.assertIn("composed 11172", jamoset.check(out, jamoset.LAYOUTS["844"], None))


class Ridge(unittest.TestCase):
    def test_faint_one_pixel_line_survives_ridge_but_not_the_threshold(self):
        w, h = 6, 4
        gray = [0.0] * (w * h)
        for y in range(h):
            gray[y * w + 2] = 0.4					# a vertical hairline at 40 % coverage
            gray[y * w + 3] = 0.1
        plain = jamoaa.binarize(gray, w, h, "threshold", 0.5)
        ridge = jamoaa.binarize(gray, w, h, "ridge", 0.5)
        self.assertEqual(plain, 0)
        self.assertEqual(ridge, gint([(2, y) for y in range(h)], w))

    def test_weak_ridge_is_dropped(self):
        w, h = 5, 1
        gray = [0.0, 0.1, 0.3, 0.1, 0.0]				# 0.3 + 0.1 < 0.48
        self.assertEqual(jamoaa.binarize(gray, w, h, "ridge", 0.5), 0)

    def test_default_binarisation_is_freetype_mono(self):
        ap = jamoset.make_parser()
        a = ap.parse_args(["--layout", "844", "--from-ttf", "x.ttf", "--size", "16", "-o", "o"])
        self.assertEqual(a.binarize, "mono")
        self.assertEqual(a.bpp, 1)


class Quality(unittest.TestCase):
    def test_level_error_exact_pixels_and_threshold_iou(self):
        w, h = 4, 1
        target = {0: bytes([3, 2, 0, 0])}
        comp = {0: bytes([3, 1, 1, 0])}
        q = jamoaa.aa_quality(target, comp, w, h)
        self.assertAlmostEqual(q["level_mae"], 2 / 4)
        self.assertAlmostEqual(q["level_mae_ink"], 2 / 3, places=3)
        self.assertAlmostEqual(q["exact_pixels_ink"], 1 / 3, places=3)
        self.assertAlmostEqual(q["iou_t2_mean"], 0.5)		# level >= 2: {0, 1} vs {0}
        self.assertEqual(q["exact"], 0)
        self.assertAlmostEqual(q["ink_ratio"], 5 / 5)

    def test_join_loss_counts_what_max_drops_where_jamo_share_a_pixel(self):
        w, h = 2, 1
        donors = {0: [bytes([128, 0]), bytes([128, 255])]}
        direct = {0: bytes([3, 3])}
        j = jamoaa.join_loss(donors, direct, w, h)
        self.assertAlmostEqual(j["level_mae_max"], 0.5)		# max: levels 2, 3
        self.assertAlmostEqual(j["level_mae_sum"], 0.0)		# sum: 255 -> 3
        self.assertAlmostEqual(j["ink_lost_max"], 1 / 6, places=3)


# ---- a small real font ------------------------------------------------------------------------------------------

UPEM = 1600		# at --size 16 a pixel is 100 units


def design_glyphs(shared_column=False):
    """{syllable code point: [contour, ...]} in cell pixels, y down (baseline row 13). Jamo keep a pixel apart."""
    def cho(L, fin):
        b = 6 if fin else 8					# bottom row edge
        if L == 0:						# ㄱ, one contour
            return [[(1, 1), (7, 1), (7, b), (6, b), (6, 2), (1, 2)]]
        if L == 2:						# ㄴ
            return [[(1, 1), (2, 1), (2, b - 1), (7, b - 1), (7, b), (1, b)]]
        return [[(1, 1), (7, 1), (7, b), (1, b)], [(2, 2), (2, b - 1), (6, b - 1), (6, 2)]]	# ㅇ: a hole
    def vowel(V, fin):
        b = 9 if fin else 10
        if V == 0:						# ㅏ: bar with a half-pixel right edge, stub to column 15
            return [[(10, 1), (11.5, 1), (11.5, b), (10, b)], [(11.5, 4), (15, 4), (15, 5), (11.5, 5)]]
        if V == 4 and shared_column:			# ㅓ with its bar in column 10 like the others: every user of an
            return [[(10, 1), (11, 1), (11, b), (10, b)], [(8, 4), (10, 4), (10, 5), (8, 5)]]	# initial set inks it
        if V == 4:						# ㅓ (its bar in another column: the vowels do not all ink one column,
            return [[(12, 1), (13, 1), (13, b), (12, b)], [(8, 4), (12, 4), (12, 5), (8, 5)]]	# as in real fonts)
        return [[(10.5, 1), (12, 1), (12, b), (10.5, b)]]	# ㅣ: half-pixel left edge
    def final(T):
        if T == 1:
            return [[(2, 11), (14, 11), (14, 15), (13, 15), (13, 12), (2, 12)]]
        return [[(2, 11), (3, 11), (3, 14), (14, 14), (14, 15), (2, 15)]]
    out = {}
    for L in (0, 2, 11):
        for V in (0, 4, 20):
            for T in (0, 1, 4):
                cs = cho(L, T) + vowel(V, T) + (final(T) if T else [])
                out[0xAC00 + (L * 21 + V) * 28 + T] = cs
    return out


def build_test_font(path, shared_column=False):
    from fontTools.fontBuilder import FontBuilder
    from fontTools.pens.ttGlyphPen import TTGlyphPen
    glyphs = design_glyphs(shared_column)
    names = [".notdef", "space", "A"] + ["uni%04X" % cp for cp in sorted(glyphs)]
    fb = FontBuilder(UPEM, isTTF=True)
    fb.setupGlyphOrder(names)
    fb.setupCharacterMap({0x20: "space", 0x41: "A", **{cp: "uni%04X" % cp for cp in glyphs}})

    def pen_of(contours):
        pen = TTGlyphPen(None)
        for c in contours:
            # y down (baseline row 13) -> font units y up; clockwise in y-down is counter-clockwise in y-up, so
            # reverse to keep TrueType's outer-clockwise rule (the hole contours are given the other way round)
            pts = [(round(x * 100), round((13 - y) * 100)) for x, y in c][::-1]
            pen.moveTo(pts[0])
            for p in pts[1:]:
                pen.lineTo(p)
            pen.closePath()
        return pen.glyph()

    g = {".notdef": pen_of([]), "space": pen_of([]), "A": pen_of([[(1, 4), (6, 4), (6, 13), (1, 13)]])}
    for cp, cs in glyphs.items():
        g["uni%04X" % cp] = pen_of(cs)
    fb.setupGlyf(g)
    def lsb(name):
        cs = glyphs.get(int(name[3:], 16)) if name.startswith("uni") else ([[(1, 4)]] if name == "A" else [])
        return min((round(x * 100) for c in cs for x, _ in c), default=0)
    fb.setupHorizontalMetrics({n: (UPEM if n.startswith("uni") else 800, lsb(n)) for n in names})
    fb.setupHorizontalHeader(ascent=1400, descent=-400)
    fb.setupNameTable({"familyName": "JamoAATest", "styleName": "Regular"})
    fb.setupOS2()
    fb.setupPost()
    fb.save(path)
    return glyphs


@unittest.skipUnless(FONTTOOLS, "needs fontTools")
class FromOutlineFont(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.ttf = os.path.join(cls.tmp.name, "t.ttf")
        cls.glyphs = build_test_font(cls.ttf)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_outline_face_reads_contours_through_a_pen(self):
        face = jamoaa.OutlineFace(self.ttf, 16)
        self.assertTrue(face.has(0xAC00))
        self.assertFalse(face.has(0xAC02))				# not drawn by the font
        self.assertEqual(len(face.contours(0xAC00 + 11 * 21 * 28)), 2 + 2)	# 아: ㅇ's two + ㅏ's two
        self.assertAlmostEqual(face.advance(0x41), 8.0)

    def test_frame_puts_the_design_on_the_cell(self):
        face = jamoaa.OutlineFace(self.ttf, 16)
        ox, oy, info = jamoaa.frame(face, sorted(self.glyphs), 16, 16, None)
        self.assertEqual((ox, oy), (0, 13))
        self.assertFalse(info["clipped"])

    def run_tool(self, *argv):
        return subprocess.run([sys.executable, "-I", os.path.join(HERE, "jamoset.py"), "--layout", "844",
                               "--from-ttf", self.ttf, "--size", "16", "--bpp", "2", "--pylib", FT_DIR, *argv],
                              capture_output=True, text=True)

    def test_2bpp_set_composes_every_syllable_like_the_direct_render(self):
        out, rep = os.path.join(self.tmp.name, "AA.SVF"), os.path.join(self.tmp.name, "aa.json")
        r = self.run_tool("-o", out, "--check", "--report", rep)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("check: composed 11172", r.stdout)
        f = jamoset.parse_svfn(open(out, "rb").read())
        self.assertEqual(f.bpp, 2)
        self.assertTrue(f.flags & jamoset.FLAG_JAMO)
        t = jamoset.unpack(f.jset)
        face = jamoaa.OutlineFace(self.ttf, 16)
        for cp in self.glyphs:
            want = jamoaa.direct_levels(face, cp, 0, 13, 16, 16)
            got = jamoaa.unpack_levels(jamoset.compose(f, t, cp), 16, 16, 2)
            self.assertEqual(got, want, "U+%04X" % cp)
        q = json.load(open(rep))
        self.assertEqual(q["aa_quality"]["level_mae"], 0.0)
        self.assertEqual(q["aa_quality"]["exact"], len(self.glyphs))
        self.assertEqual(q["contours"]["straddled"], 0)

    def test_hand_checked_levels(self):
        out = os.path.join(self.tmp.name, "AA2.SVF")
        self.assertEqual(self.run_tool("-o", out).returncode, 0)
        f = jamoset.parse_svfn(open(out, "rb").read())
        t = jamoset.unpack(f.jset)
        ga = jamoaa.unpack_levels(jamoset.compose(f, t, 0xAC00), 16, 16, 2)		# 가
        at = lambda g, x, y: g[y * 16 + x]
        self.assertEqual(at(ga, 10, 3), 3)		# ㅏ's bar
        self.assertEqual(at(ga, 11, 3), 2)		# its half-pixel edge (coverage 0.5 -> 128 -> level 2)
        self.assertEqual(at(ga, 11, 4), 3)		# bar half + stub half: one jamo, summed before quantising
        self.assertEqual(at(ga, 14, 4), 3)
        self.assertEqual(at(ga, 0, 0), 0)
        a = jamoaa.unpack_levels(jamoset.compose(f, t, 0xAC00 + 11 * 21 * 28), 16, 16, 2)	# 아
        self.assertEqual(at(a, 1, 4), 3)			# ㅇ's ring
        self.assertEqual(at(a, 3, 4), 0)			# its hole
        A = jamoaa.unpack_levels(f.glyphs[0x41], 16, 16, 2)
        self.assertEqual(at(A, 1, 5), 3)			# ASCII at 2 bpp from the same frame
        self.assertEqual(f.metrics[0x41][0], 8)

    def test_syllables_step_by_the_fonts_advance_in_a_wider_cell(self):
        out, info = jamoaa.build(jamoset.LAYOUTS["844"], self.ttf, 16, cell=(18, 16))
        self.assertEqual(jamoset.unpack(jamoset.parse_svfn(out).jset).advance, 16)
        out, info = jamoaa.build(jamoset.LAYOUTS["844"], self.ttf, 16)
        self.assertEqual(jamoset.unpack(jamoset.parse_svfn(out).jset).advance, 0)		# the cell width

    def test_1bpp_default_is_unchanged_by_the_options(self):
        ap = jamoset.make_parser()
        a = ap.parse_args(["--layout", "844", "--from-ttf", self.ttf, "--size", "16", "-o", "o"])
        self.assertEqual((a.bpp, a.binarize), (1, "mono"))

    def test_ridge_1bpp_from_the_outline(self):
        out = os.path.join(self.tmp.name, "R.SVF")
        r = subprocess.run([sys.executable, "-I", os.path.join(HERE, "jamoset.py"), "--layout", "844",
                            "--from-ttf", self.ttf, "--size", "16", "--binarize", "ridge", "--mask", "none",
                            "--pylib", FT_DIR, "-o", out, "--check"], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        f = jamoset.parse_svfn(open(out, "rb").read())
        self.assertEqual(f.bpp, 1)
        self.assertIn("IoU 1.000", r.stdout)

    def test_bpp2_refuses_options_it_cannot_honour(self):
        r = self.run_tool("-o", os.path.join(self.tmp.name, "x.svf"), "--embolden")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("--bpp 2", r.stderr)


# ---- noise cleanup -------------------------------------------------------------------------------------------------

class Cleanup(unittest.TestCase):
    def test_speck_the_targets_do_not_have_is_removed_one_they_need_is_kept(self):
        w = h = 6
        keys = {0: ("a", "b", None), 1: ("a", "c", None)}
        need = gint([(1, 1)], w)
        noise = gint([(4, 4)], w)
        stem = gint([(0, y) for y in range(6)], w)
        P = {"a": stem | need | noise, "b": 0, "c": 0}
        targets = {0: stem | need, 1: stem | need}
        removed = jamoaa.remove_specks(P, keys, targets, w, h, 1)
        self.assertEqual(removed, 1)
        self.assertEqual(P["a"], stem | need)

    def test_robust_consensus_drops_an_outlier_donor(self):
        good = [bytes([0, 255, 255, 0]), bytes([0, 250, 255, 0]), bytes([0, 255, 240, 0]), bytes([10, 255, 255, 0]),
                bytes([0, 255, 255, 5])]
        odd = bytes([255, 0, 0, 255])
        m, kept = jamoaa.robust_consensus(good + [odd, odd])
        self.assertEqual(kept, 5)
        self.assertEqual(m, jamoaa.median_consensus(good))

    def test_faint_levels_need_half_the_donors_and_small_faint_specks_go(self):
        w, h = 6, 1
        lv = [1, 0, 1, 0, 2, 3]
        sup = [0.3, 0.0, 0.6, 0.0, 0.9, 0.9]
        out, changed = jamoaa.aa_clean(lv, sup, w, h, 1)
        self.assertEqual(out, [0, 0, 0, 0, 2, 3])		# pixel 2 is supported but alone and faint: a speck
        self.assertEqual(changed, 2)
        out, _ = jamoaa.aa_clean([0, 1, 1, 0, 0, 0], [0, 0.6, 0.6, 0, 0, 0], w, h, 1)
        self.assertEqual(out, [0, 1, 1, 0, 0, 0])		# two pixels: not a speck at size 1

    def test_donor_masks_are_the_dilated_support(self):
        w = h = 5
        keys = {s: ("a", None, None) for s in range(4)}
        on = bytes([255 if i == 12 else 0 for i in range(25)])
        off = bytes(25)
        donors = {0: [on, None, None], 1: [on, None, None], 2: [off, None, None], 3: [off, None, None]}
        m = jamoaa.donor_masks(keys, donors, w, h, 128, 0.5, 1)
        self.assertEqual(m["a"], gint([(x, y) for x in range(1, 4) for y in range(1, 4)], w))
        m = jamoaa.donor_masks(keys, donors, w, h, 128, 0.75, 1)
        self.assertEqual(m["a"], 0)


@unittest.skipUnless(FONTTOOLS, "needs fontTools")
class PeersDecideWhatRegionsCannot(unittest.TestCase):
    """Every vowel inks column 10, so the free region fit gives that column to the initials' pieces (all their users
    ink it). The bars of ㅏ and ㅣ must still go to the medials: the syllables sharing the vowel have them, those
    with another vowel in the same set do not. (ㅓ's bar is column 10 exactly, as every vowel's: nothing can tell
    whose it is, and the regions decide.)"""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.ttf = os.path.join(cls.tmp.name, "s.ttf")
        cls.glyphs = build_test_font(cls.ttf, shared_column=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def error(self):
        out, info = jamoaa.build(jamoset.LAYOUTS["844"], self.ttf, 16, keep=True)
        ctx = info["ctx"]
        comp = jamoaa.composed_levels(out, ctx["direct"])
        return jamoaa.aa_quality(ctx["direct"], comp, 16, 16)["level_mae"], ctx, info

    def test_vowel_bars_go_to_the_medial(self):
        err, ctx, info = self.error()
        for ch in "가기":
            s = ord(ch) - 0xAC00
            self.assertEqual(sum(1 for v in ctx["donors"][s][0] if v), 6 + 7 - 1, ch)	# ㄱ alone in the initial
        self.assertTrue(info["contours"]["by_peers"] > 0)
        sure = jamoaa.PEER_SURE
        try:
            jamoaa.PEER_SURE = 99.0					# the regions alone
            err_regions, _, _ = self.error()
        finally:
            jamoaa.PEER_SURE = sure
        self.assertLess(err, err_regions)


class CleanAndAdaptiveFromOutline(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.ttf = os.path.join(cls.tmp.name, "t.ttf")
        cls.glyphs = build_test_font(cls.ttf)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_clean_pieces_ink_only_where_their_donors_do(self):
        out, info = jamoaa.build(jamoset.LAYOUTS["844"], self.ttf, 16, keep=True, clean=True)
        ctx = info["ctx"]
        masks = jamoaa.donor_masks(ctx["keys"], ctx["donors"], 16, 16, 43, 0.25, 1)
        for k, lv in ctx["pieces"].items():
            if k not in masks:
                continue
            ink = gint([(i % 16, i // 16) for i, v in enumerate(lv) if v], 16)
            self.assertEqual(ink & ~masks[k], 0, k)

    def test_adaptive_aa_writes_its_own_tables_and_checks(self):
        out = os.path.join(self.tmp.name, "AD.SVF")
        r = subprocess.run([sys.executable, "-I", os.path.join(HERE, "jamoset.py"), "--layout", "adaptive",
                            "--from-ttf", self.ttf, "--size", "16", "--bpp", "2", "--pylib", FT_DIR, "-o", out,
                            "--check"], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("adaptive layout's", r.stdout)
        self.assertIn("level error 0.0000", r.stdout)


def layout_face(layout_name, present):
    """A precomposed 1 bpp face of @p present syllables drawn by @p layout_name's rule from pieces that keep to separate
    regions (initial top left, medial right, final bottom), each with its own random pixels."""
    import random
    import test_jamoset as TJ
    layout = jamoset.LAYOUTS[layout_name]
    rnd = random.Random(5)
    n = {"844": 360, "1044-san": 400}[layout_name]
    med0, fin0 = {"844": (160, 248), "1044-san": (200, 288)}[layout_name]
    data = bytearray(n * 32)
    for i in range(n):
        if i < med0:
            if i % 20 == 0:
                continue
            xs, ys = range(0, 9), range(0, 10)
        elif i < fin0:
            if (i - med0) % 22 == 0:
                continue
            xs, ys = range(9, 16), range(0, 11)
        else:
            if (i - fin0) % 28 == 0:
                continue
            xs, ys = range(0, 16), range(11, 16)
        for y in ys:
            row = 0
            for x in xs:
                if rnd.random() < 0.45:
                    row |= 0x8000 >> x
            data[i * 32 + y * 2:i * 32 + y * 2 + 2] = row.to_bytes(2, "big")
    built = jamoset.build(layout, bytes(data))[0]
    f = jamoset.parse_svfn(built)
    t = jamoset.unpack(f.jset)
    face, _ = TJ.make_face({s: jamoset.compose(f, t, 0xAC00 + s) for s in present})
    return face


class AdaptiveFromBitmap(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.ks = jamoset.ks_x_1001_syllables()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def fit(self, face_bytes, *argv):
        src = os.path.join(self.tmp.name, "F.SVF")
        out = os.path.join(self.tmp.name, "O.SVF")
        rep = os.path.join(self.tmp.name, "r.json")
        open(src, "wb").write(face_bytes)
        r = subprocess.run([sys.executable, "-I", os.path.join(HERE, "jamoset.py"), "--from-svf", src, "--mask", "none",
                            "-o", out, "--check", "--report", rep, *argv], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        return r.stdout, json.load(open(rep))

    def test_a_10x4x4_face_needs_more_initial_sets_than_844_and_adaptive_finds_them(self):
        face = layout_face("1044-san", self.ks)
        _, q844 = self.fit(face, "--layout", "844")
        out, qad = self.fit(face, "--layout", "adaptive")
        self.assertIn("adaptive layout's", out)
        self.assertLess(q844["quality"]["iou_mean"], 0.99)
        self.assertGreater(qad["quality"]["iou_mean"], q844["quality"]["iou_mean"] + 0.02)
        self.assertGreater(qad["adaptive"]["sets"][0], 8)
        self.assertLessEqual(qad["adaptive"]["sets"][0], 24)

    def test_limits_are_kept(self):
        face = layout_face("1044-san", self.ks)
        _, q = self.fit(face, "--layout", "adaptive:9,4,4")
        self.assertEqual(q["adaptive"]["sets"], [9, 4, 4])

    def test_an_exact_844_face_stays_exact_with_cleanup(self):
        face = layout_face("844", self.ks)
        _, q = self.fit(face, "--layout", "844", "--clean")
        self.assertEqual(q["quality"]["iou_mean"], 1.0)
        _, q = self.fit(face, "--layout", "adaptive")
        self.assertEqual(q["quality"]["iou_mean"], 1.0)

    def test_bad_limits_are_refused(self):
        r = subprocess.run([sys.executable, "-I", os.path.join(HERE, "jamoset.py"), "--layout", "adaptive:30,4,4",
                            "--from-svf", "x", "-o", "y"], capture_output=True, text=True)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("NI 8..24", r.stderr)


if __name__ == "__main__":
    unittest.main()
