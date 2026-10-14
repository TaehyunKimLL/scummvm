#!/usr/bin/env python3
"""jamoset.py tests: every layout's tables against the research tools' selection rules, the written file read back.

  python3 tools/korean/test_jamoset.py

The reference selectors below are copied from ~/work/johab-fonts/tools/kfont.py (sel_*) and dkb_fnt.py (parts),
the independent implementation the layouts were checked against; they return the source font's glyph index.
"""

import os
import random
import struct
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import jamoset  # noqa: E402
import johab844  # noqa: E402

# The reference tables are literals typed from the research doc / kfont.py / dkb_fnt.py, not imported from jamoset,
# so a wrong entry in jamoset cannot also change the reference. Vowel order ㅏ ㅐ ㅑ ㅒ ㅓ ㅔ ㅕ ㅖ ㅗ ㅘ ㅙ ㅚ ㅛ ㅜ ㅝ ㅞ ㅟ ㅠ ㅡ ㅢ ㅣ.
KK = (0, 15)
JUNG_FIELD = (3, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, 18, 19, 20, 21, 22, 23, 26, 27, 28, 29)
I844_NOFIN = (0, 0, 0, 0, 0, 0, 0, 0, 1, 3, 3, 3, 1, 2, 4, 4, 4, 2, 1, 3, 0)
I844_FIN = (5, 5, 5, 5, 5, 5, 5, 5, 6, 7, 7, 7, 6, 6, 7, 7, 7, 6, 6, 7, 5)
F844 = (0, 2, 0, 2, 1, 2, 1, 2, 3, 0, 2, 1, 3, 3, 1, 2, 1, 3, 3, 1, 1)
R621_NOFIN = (0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 2, 2, 1, 1, 2, 2, 2, 1, 1, 2, 0)
R621_FIN = (3, 3, 3, 3, 3, 3, 3, 3, 4, 5, 5, 5, 4, 4, 5, 5, 5, 4, 4, 5, 3)
R_FCON1 = (0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 3, 3, 0, 0, 3, 1, 2, 4, 4, 4, 0, 0, 2, 1, 3, 0)
R_FCON2 = (0, 0, 5, 5, 5, 5, 5, 5, 0, 0, 5, 5, 5, 6, 8, 8, 0, 0, 8, 6, 7, 9, 9, 9, 0, 0, 7, 6, 8, 5)
R_VBASE = (0, 0, 0, 311, 314, 317, 320, 323, 0, 0, 326, 329, 332, 335, 339, 343, 0, 0, 347, 351, 355, 358, 361, 364,
           0, 0, 367, 370, 374, 378)
R_VKIND = (0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0)
# hanterm final variant by medial value: the 8x4x4 final table (research doc 3.4), measured on the real fonts (M1)
R_LMAP = (0, 0, 0, 0, 2, 0, 2, 1, 0, 0, 2, 1, 2, 3, 0, 2, 0, 0, 1, 3, 3, 1, 2, 1, 0, 0, 3, 3, 1, 1)
R_P16_INIT0 = (0, 0, 0, 0, 0, 0, 0, 0, 1, 3, 3, 3, 1, 2, 3, 3, 3, 2, 1, 3, 0)
R_P16_INIT1 = (4, 4, 4, 4, 4, 4, 4, 4, 5, 6, 6, 6, 5, 5, 6, 6, 6, 5, 5, 6, 4)
R_P16_FIN = (0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0)
R_P40 = (0, 1, 0, 1, 0, 1, 0, 1, 2, 4, 5, 4, 2, 3, 4, 5, 4, 3, 2, 4, 0)


def lvt(cp):
    i = cp - 0xAC00
    return i // 588, (i // 28) % 21, i % 28


def sel_844(L, V, T, kk_first=True):
    kk = (L in KK) == kk_first
    out = [(I844_FIN if T else I844_NOFIN)[V] * 20 + L + 1, 160 + ((0 if kk else 1) + (2 if T else 0)) * 22 + V + 1]
    if T:
        out.append(248 + F844[V] * 28 + T)
    return out


def sel_ihnm(L, V, T):
    return sel_844(L, V, T, kk_first=False)


def sel_1044(L, V, T):
    out = [(I844_NOFIN[V] + (5 if T else 0)) * 20 + L + 1,
           200 + ((0 if L in KK else 1) + (2 if T else 0)) * 22 + V + 1]
    if T:
        out.append(288 + F844[V] * 28 + T)
    return out


def sel_621(L, V, T):
    out = [(R621_FIN if T else R621_NOFIN)[V] * 20 + L + 1, 120 + (22 if T else 0) + V + 1]
    if T:
        out.append(164 + T)
    return out


def sel_kyra(L, V, T):
    out = [1 + 10 * L + 2 * I844_NOFIN[V] + (1 if T else 0),
           191 + 1 + 4 * V + (0 if L in KK else 2) + (1 if T else 0)]
    if T:
        out.append(276 + 1 + 4 * (T - 1) + F844[V])
    return out


def johab_fields(L, V, T):
    return L + 2, JUNG_FIELD[V], (T + 1 if T <= 16 else T + 2)


HT_LKIND = (0, 0, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1)


def sel_hanterm(L, V, T):
    f, m, l = johab_fields(L, V, T)
    out = [f * 10 + (R_FCON2 if l > 1 else R_FCON1)[m] - 19]
    ind = R_VBASE[m]
    if R_VKIND[m] == 0:
        ind += HT_LKIND[l]
    else:
        ind += (0 if f in (2, 17) else 1) + (2 if l > 1 else 0)
    out.append(ind)
    if l != 1:
        out.append(l * 4 + R_LMAP[m] + (397 if l < 18 else 393))
    return out


def sel_dkb16(L, V, T):
    f = T > 0
    out = [(R_P16_INIT1 if f else R_P16_INIT0)[V] * 20 + L + 1, 140 + f * 22 + V + 1]
    if f:
        out.append(184 + R_P16_FIN[V] * 28 + T)
    return out


def sel_dkb40(L, V, T):
    f = T > 0
    out = [(R_P40[V] + 6 * f) * 20 + L + 1, 240 + (2 * f + (L not in KK)) * 22 + V + 1]
    if f:
        out.append(328 + F844[V] * 28 + T)
    return out


REFERENCE = {"844": sel_844, "844-ihnm": sel_ihnm, "1044-san": sel_1044, "621": sel_621, "hanterm-johab": sel_hanterm,
             "dkb-printer-7x2x2": sel_dkb16, "dkb-printer-12x4x4": sel_dkb40, "kyra-lok": sel_kyra,
             "844-hbios24": sel_844}

# 가 각 한 의 뷁 흙, by hand from the research doc's formulas (3.2, 3.3, 3.4, 6.1, formats 2.5-2.9).
SAMPLES = (0xAC00, 0xAC01, 0xD55C, 0xC758, 0xBDC1, 0xD759)
HAND = {
    # 844: 각 = initial set 5, medial set 2 (g/k, final), final set 0. 한 = h a n: initial set 5, medial set 1+2.
    # 의 = ieung ui: initial set 3, medial set 1.
    "844": ((1, 161), (101, 205, 249), (119, 227, 252), (72, 202), (148, 242, 313), (139, 245, 341)),
    "844-ihnm": ((1, 183), (101, 227, 249), (119, 205, 252), (72, 180), (148, 220, 313), (139, 223, 341)),
    "1044-san": ((1, 201), (101, 245, 289), (119, 267, 292), (72, 242), (188, 282, 353), (139, 285, 381)),
    "621": ((1, 121), (61, 143, 165), (79, 143, 168), (52, 140), (108, 158, 173), (99, 161, 173)),
    "kyra-lok": ((1, 192), (2, 193, 277), (182, 195, 289), (117, 270), (80, 255, 311), (184, 267, 312)),
    # hanterm: han uses the nieun-final medial variant (2), the others variant 1.
    "hanterm-johab": ((1, 311), (6, 312, 405), (186, 313, 417), (114, 375), (80, 362, 439), (187, 373, 440)),
    "dkb-printer-7x2x2": ((1, 141), (81, 163, 185), (99, 163, 188), (72, 160), (128, 178, 221), (119, 181, 221)),
    "dkb-printer-12x4x4": ((1, 241), (121, 285, 329), (139, 307, 332), (92, 282), (228, 322, 393), (179, 325, 421)),
}


def make_source(layout_name, seed=1):
    """Bytes of a synthetic source font of the layout, every piece with its own pixels; blank slots stay blank."""
    rnd = random.Random(seed)
    if layout_name in ("844", "844-ihnm"):
        gb, n, blanks = 32, 360, [s * 20 for s in range(8)] + [160 + s * 22 for s in range(4)] + [248 + s * 28 for s in range(4)]
    elif layout_name == "844-hbios24":
        return DosFace24.hbios(seed)
    elif layout_name == "1044-san":
        gb, n, blanks = 32, 400, [s * 20 for s in range(10)] + [200 + s * 22 for s in range(4)] + [288 + s * 28 for s in range(4)]
    elif layout_name == "621":
        gb, n, blanks = 32, 192, []
    elif layout_name == "kyra-lok":
        gb, n, blanks = 30, 385, []
    elif layout_name == "dkb-printer-7x2x2":
        gb, n, blanks = 32, 480, []
    elif layout_name == "dkb-printer-12x4x4":
        gb, n, blanks = 200, 880, []
    else:
        raise KeyError(layout_name)
    data = bytearray(rnd.randbytes(gb * n))
    for b in blanks:
        data[b * gb:(b + 1) * gb] = bytes(gb)
    if layout_name == "kyra-lok":
        for i in range(n):
            for y in range(15):
                data[i * 30 + y * 2 + 1] &= 0xFE		# 15 columns: the 16th bit is padding
    return bytes(data)


def make_bdf(seed=1):
    """A hanterm-style johab-1 BDF: 529 glyphs 16x16, ENCODING = glyph number, every glyph with its own pixels."""
    rnd = random.Random(seed)
    out = ["STARTFONT 2.1", "FONT -hanterm-test-medium-r-normal--16-160-75-75-c-160-johab-1",
           "FONTBOUNDINGBOX 16 16 0 -2", "STARTPROPERTIES 2", "FONT_ASCENT 14", "FONT_DESCENT 2", "ENDPROPERTIES",
           "CHARS 529"]
    for enc in range(529):
        out += [f"STARTCHAR g{enc}", f"ENCODING {enc}", "SWIDTH 1000 0", "DWIDTH 16 0", "BBX 16 16 0 -2", "BITMAP"]
        out += ["%04X" % (0 if enc == 0 else rnd.getrandbits(16)) for _ in range(16)]
        out.append("ENDCHAR")
    out.append("ENDFONT")
    return ("\n".join(out) + "\n").encode("latin-1")


def raw_piece(name, data, i):
    """Glyph i of a synthetic source as the SVFN 1bpp rows, read straight from the bytes (not through jamoset)."""
    if name in ("844", "844-ihnm", "1044-san"):
        return data[i * 32:(i + 1) * 32]
    if name == "844-hbios24":			# 90 byte glyphs of a 24x30 cell: ink rows 3-26 are the 24 rows
        return data[i * 90 + 9:i * 90 + 81]
    if name == "kyra-lok":			# 15 columns: two bytes a row, the 16th bit is padding (zero in make_source)
        return data[i * 30:(i + 1) * 30]
    if name == "dkb-printer-12x4x4":
        return data[i * 200:(i + 1) * 200]
    if name == "621":				# two columns of sixteen bytes
        g = data[i * 32:(i + 1) * 32]
        return bytes(b for y in range(16) for b in (g[y], g[16 + y]))
    if name == "dkb-printer-7x2x2":		# sixteen columns of two bytes, MSB = top row
        g = data[i * 32:(i + 1) * 32]
        rows = [0] * 16
        for c in range(16):
            col = g[c * 2] << 8 | g[c * 2 + 1]
            for r in range(16):
                rows[r] |= (col >> (15 - r) & 1) << (15 - c)
        return b"".join(r.to_bytes(2, "big") for r in rows)
    if name == "hanterm-johab":			# the glyph's hex rows, top first (BBX 16 16 0 -2, ascent 14)
        text = data.decode("latin-1")
        at = text.index("ENCODING %d\n" % i)
        rows = text[text.index("BITMAP\n", at) + 7:text.index("ENDCHAR", at)].split()
        return b"".join(int(r, 16).to_bytes(2, "big") for r in rows)
    raise KeyError(name)


def source_for(name):
    return make_bdf() if name == "hanterm-johab" else make_source(name)


def src_pieces(layout, data):
    return layout.opener(data, 0, "")


class TablesMatchResearchTools(unittest.TestCase):
    def test_all_syllables_every_layout(self):
        for name, layout in jamoset.LAYOUTS.items():
            t = jamoset.tables_for(layout)
            ref = REFERENCE[name]
            for cp in range(0xAC00, 0xD7A4):
                self.assertEqual(jamoset.pieces_of(t, cp), ref(*lvt(cp)), f"{name} U+{cp:04X}")

    def test_hand_checked_syllables(self):
        for name, expected in HAND.items():
            t = jamoset.tables_for(jamoset.LAYOUTS[name])
            for cp, want in zip(SAMPLES, expected):
                self.assertEqual(tuple(jamoset.pieces_of(t, cp)), want, f"{name} U+{cp:04X}")

    def test_no_final_syllable_has_two_pieces(self):
        for name, layout in jamoset.LAYOUTS.items():
            t = jamoset.tables_for(layout)
            self.assertEqual(len(jamoset.pieces_of(t, 0xAC00)), 2, name)
            self.assertEqual(len(jamoset.pieces_of(t, 0xAC01)), 3, name)

    def test_844_uses_344_pieces(self):
        t = jamoset.tables_for(jamoset.LAYOUTS["844"])
        used = jamoset.used_pieces(t)
        self.assertEqual(len(used), 344)		# 360 minus the 16 blank fill slots
        self.assertEqual(len(set(used)), 344)

    def test_garbage_final_set_of_the_no_final_class_is_zero(self):
        for layout in jamoset.LAYOUTS.values():
            t = jamoset.tables_for(layout)
            for lc in range(t.lc):
                for v in range(21):
                    cell = (lc * 21 + v) * t.tc + t.tail_class[0]
                    if t.tail_class[0] not in {t.tail_class[k] for k in range(1, 28)}:
                        self.assertEqual(t.fin_set[cell], 0, layout.name)


class CompatJamo(unittest.TestCase):
    def test_844_compat_pieces(self):
        t = jamoset.tables_for(jamoset.LAYOUTS["844"])
        self.assertEqual(t.compat[0], 1)				# U+3131 g: initial set 0
        self.assertEqual(t.compat[3], 3)				# U+3134 n: initial 2 -> 2+1
        self.assertEqual(t.compat[2], 248 + 3)			# U+3133 gs: a final-only consonant, final set 0
        self.assertEqual(t.compat[0x314F - 0x3131], 161)		# U+314F a: medial set 0
        self.assertEqual(t.compat[0x3163 - 0x3131], 160 + 20 + 1)	# U+3163 i
        self.assertEqual(len(t.compat), 51)
        self.assertNotIn(jamoset.BLANK, t.compat)

    def test_every_layout_draws_every_compat_jamo(self):
        for layout in jamoset.LAYOUTS.values():
            t = jamoset.tables_for(layout)
            self.assertNotIn(jamoset.BLANK, t.compat, layout.name)


class WrittenFile(unittest.TestCase):
    def test_844_composes_like_johab844_for_all_syllables(self):
        layout = jamoset.LAYOUTS["844"]
        data = make_source("844")
        eng = bytes(random.Random(5).randbytes(4096))
        out, mapping, src = jamoset.build(layout, data, eng=eng)
        f = jamoset.parse_svfn(out)
        self.assertTrue(f.flags & jamoset.FLAG_JAMO)
        self.assertEqual(len(f.jset), jamoset.jset_size(2, 2, 8, 4, 4))
        t = jamoset.unpack(f.jset)
        self.assertEqual(len(mapping), 344)
        for cp in range(0xAC00, 0xD7A4):
            self.assertEqual(jamoset.compose(f, t, cp), johab844.compose(data, cp), f"U+{cp:04X}")
        msg = jamoset.check(out, layout, data)
        self.assertIn("identical to johab844.py", msg)

    def test_every_layout_composes_the_or_of_its_source_pieces(self):
        for name, layout in jamoset.LAYOUTS.items():
            data = source_for(name)
            src = layout.opener(data, 0, "")
            out, mapping, _ = jamoset.build(layout, data)
            f = jamoset.parse_svfn(out)
            self.assertEqual((f.cell_w, f.cell_h), (src.w, src.h), name)
            t = jamoset.unpack(f.jset)
            ref = REFERENCE[name]
            for cp in range(0xAC00, 0xD7A4):
                want = bytearray(((src.w + 7) // 8) * src.h)
                for i in ref(*lvt(cp)):
                    for k, b in enumerate(raw_piece(name, data, i)):
                        want[k] |= b
                self.assertEqual(jamoset.compose(f, t, cp), bytes(want), f"{name} U+{cp:04X}")

    def test_header_and_block_placement(self):
        layout = jamoset.LAYOUTS["844"]
        out, _, _ = jamoset.build(layout, make_source("844"), eng=bytes(4096))
        (magic, version, flags, bpp, shadow, codepage, count, cw, ch, ascent, r0, r1, metrics_off, data_off,
         data_size) = struct.unpack_from(jamoset.HEADER, out)
        (cmap_off,) = struct.unpack_from("<I", out, 32)
        self.assertEqual((magic, version, bpp, cw, ch), (b"SVFN", 2, 1, 16, 16))
        self.assertEqual(flags, 3)					# proportional + jamo sets
        self.assertEqual(cmap_off, 36)
        jset_off = 36 + 8 * count
        self.assertEqual(out[jset_off:jset_off + 4], b"JSET")
        # The block ends exactly where the metrics table starts, the length the engine requires.
        self.assertEqual(metrics_off, jset_off + jamoset.jset_size(2, 2, 8, 4, 4))
        self.assertEqual(data_off, metrics_off + 4 * count)
        self.assertEqual(len(out), data_off + data_size)
        self.assertEqual(count, 344 + 95)			# pieces and ASCII 0x20..0x7E

    def test_ascii_from_eng_sits_at_the_left_and_steps_8(self):
        eng = bytearray(4096)
        eng[ord("A") * 16 + 3] = 0xC3
        out, _, _ = jamoset.build(jamoset.LAYOUTS["844"], make_source("844"), eng=bytes(eng))
        f = jamoset.parse_svfn(out)
        self.assertEqual(f.glyphs[ord("A")][3 * 2:3 * 2 + 2], bytes([0xC3, 0]))
        self.assertEqual(f.metrics[ord("A")], (8, 0, 8))
        self.assertEqual(f.metrics[ord(" ")][0], 8)

    def test_kyra_ascii_stays_a_narrow_cell(self):
        out, _, _ = jamoset.build(jamoset.LAYOUTS["kyra-lok"], make_source("kyra-lok"), eng=bytes(4096))
        f = jamoset.parse_svfn(out)
        self.assertEqual(f.cell_w, 15)
        self.assertEqual(f.metrics[ord("A")][0], 7)	# not past half the cell, or it would take two cells
        self.assertEqual(unpack_advance(out), 16)

    def test_base_keeps_non_syllable_glyphs_and_refuses_another_cell(self):
        w = h = 16
        stride = 32
        glyphs = [(ord("A"), bytes(range(32)), 9, 1, 7), (0xAC00, bytes([0xFF] * 32), 16, 0, 16),
                  (0x2605, bytes([0x0F] * 32), 16, 4, 4)]
        base = bytearray(jamoset.write_svfn(w, h, 14, glyphs, b""))
        struct.pack_into("<H", base, 6, 1)		# a plain proportional font: no jamo flag
        out, _, _ = jamoset.build(jamoset.LAYOUTS["844"], make_source("844"), base=bytes(base))
        f = jamoset.parse_svfn(out)
        self.assertEqual(f.glyphs[ord("A")], bytes(range(32)))
        self.assertEqual(f.metrics[ord("A")], (9, 1, 7))
        self.assertEqual(f.glyphs[0x2605], bytes([0x0F] * 32))
        self.assertNotIn(0xAC00, f.glyphs)		# syllables come from pieces only
        self.assertEqual(f.ascent, 14)
        small = jamoset.write_svfn(8, 8, 7, [(65, bytes(8), 8, 0, 0)], b"")
        small = bytearray(small)
        struct.pack_into("<H", small, 6, 1)
        with self.assertRaises(jamoset.SourceError):
            jamoset.build(jamoset.LAYOUTS["844"], make_source("844"), base=bytes(small))

    def test_wrong_source_size_is_refused(self):
        for name in jamoset.LAYOUTS:
            with self.assertRaises(jamoset.SourceError, msg=name):
                jamoset.LAYOUTS[name].opener(b"\0" * 1234, 0, "")

    def test_844_refuses_a_file_whose_fill_slots_are_not_blank(self):
        data = bytearray(make_source("844"))
        for s in (0, 20, 40):
            data[s * 32] = 1
        with self.assertRaises(jamoset.SourceError):
            jamoset.LAYOUTS["844"].opener(bytes(data), 0, "")

    def test_printer_face_selects_the_second_typeface(self):
        data = make_source("dkb-printer-12x4x4")
        layout = jamoset.LAYOUTS["dkb-printer-12x4x4"]
        a = layout.opener(data, 0, "").glyph(1).to_bytes()
        b = layout.opener(data, 1, "").glyph(1).to_bytes()
        self.assertEqual(a, data[200:400])
        self.assertEqual(b, data[440 * 200 + 200:440 * 200 + 400])


def unpack_advance(out):
    return jamoset.unpack(jamoset.parse_svfn(out).jset).advance


class JsetCodec(unittest.TestCase):
    def good(self):
        return jamoset.pack(jamoset.renumber(jamoset.tables_for(jamoset.LAYOUTS["844"]),
                                             {s: s for s in range(360)}))

    def test_round_trip(self):
        t = jamoset.unpack(self.good())
        self.assertEqual(jamoset.pieces_of(t, 0xAC01), [101, 205, 249])

    def test_malformed_blocks_are_refused_like_the_engine_does(self):
        g = self.good()
        cases = {
            "magic": b"X" + g[1:],
            "length": g[:-1],
            "length extra": g + b"\0",
            "lead classes": g[:6] + b"\x05" + g[7:],
            "leadClass": g[:12 + 3] + b"\x02" + g[12 + 4:],
            "initSet": g[:59 + 5] + b"\x08" + g[59 + 6:],
        }
        for what, data in cases.items():
            with self.assertRaises(ValueError, msg=what) as cm:
                jamoset.unpack(data)
            self.assertIn(what.split()[0], str(cm.exception))


class ReviewFixes(unittest.TestCase):
    def test_ihnm_accepts_trailing_glyphs_and_reads_the_first_360(self):
        # SOL-1: the IHNM file is the 360 jamo glyphs and then other glyphs; only the jamo are read.
        layout = jamoset.LAYOUTS["844-ihnm"]
        base = make_source("844")
        data = base + random.Random(9).randbytes(126 * 32)
        self.assertEqual(len(data), 15552)
        out, mapping, src = jamoset.build(layout, data)
        out0, _, _ = jamoset.build(layout, base)
        self.assertEqual(out, out0)
        with self.assertRaises(jamoset.SourceError):
            layout.opener(base[:-1], 0, "")
        # the plain 8x4x4 layout keeps its exact-size whitelist
        with self.assertRaises(jamoset.SourceError):
            jamoset.LAYOUTS["844"].opener(data, 0, "")

    def test_base_keeps_the_marks_at_origin_flag(self):
        # SOL-2: flag bit 2 of the base survives, with the negative-bearing mark it governs.
        mark = (0x0301, bytes([0x0F] * 32), 0, -3, 4)
        for flags, want in ((1 | 4, 7), (1, 3)):
            base = bytearray(jamoset.write_svfn(16, 16, 14, [mark], b""))
            struct.pack_into("<H", base, 6, flags)
            out, _, _ = jamoset.build(jamoset.LAYOUTS["844"], make_source("844"), base=bytes(base))
            f = jamoset.parse_svfn(out)
            self.assertEqual(f.flags, want)
            self.assertEqual(f.metrics[0x0301], (0, -3, 4))
            self.assertEqual(f.glyphs[0x0301], bytes([0x0F] * 32))

    def test_bdf_without_a_required_encoding_is_refused(self):
        text = make_bdf().decode("latin-1")
        cut = text.replace("ENCODING 311\n", "ENCODING 9999\n")
        self.assertNotEqual(cut, text)
        with self.assertRaises(jamoset.SourceError) as cm:
            jamoset.open_bdf_glyphs(cut.encode("latin-1"))
        self.assertIn("311", str(cm.exception))
        nolines = text.replace("ENCODING 311\n", "")
        with self.assertRaises(jamoset.SourceError):
            jamoset.open_bdf_glyphs(nolines.encode("latin-1"))

    def test_bdf_with_a_truncated_bitmap_is_refused(self):
        text = make_bdf().decode("latin-1")
        start = text.index("ENCODING 311\n")
        bm = text.index("BITMAP\n", start) + 7
        first_row_end = text.index("\n", bm) + 1
        cut = text[:first_row_end] + text[text.index("ENDCHAR", start):]
        with self.assertRaises(jamoset.SourceError) as cm:
            jamoset.open_bdf_glyphs(cut.encode("latin-1"))
        self.assertIn("311", str(cm.exception))

    def test_an_explicitly_present_blank_glyph_stays_valid(self):
        src = jamoset.open_bdf_glyphs(make_bdf())
        self.assertEqual(src.glyph(0).rows, [0] * 16)
        self.assertEqual(src.count, 529)

    def test_hanterm_final_variant_of_wae_oe_we_ui_follows_the_8x4x4_table(self):
        # M1: the stems of the finals were measured against each vowel's stroke in the real johabm16/p16/iyagi16
        # fonts: wae/we (stroke like ae/e) take variant 2, oe/ui (stroke like eo) variant 1.
        t = jamoset.tables_for(jamoset.LAYOUTS["hanterm-johab"])
        def fin(L, V, T):
            return jamoset.pieces_of(t, 0xAC00 + (L * 21 + V) * 28 + T)[2]
        # gwaen (L0 wae V10 T4 nieun), goen (V11), gwen (V15), gui (V19): final glyph 405 + 4*(T-1) + variant
        self.assertEqual(fin(0, 10, 4), 405 + 12 + 2)
        self.assertEqual(fin(0, 11, 4), 405 + 12 + 1)
        self.assertEqual(fin(0, 15, 4), 405 + 12 + 2)
        self.assertEqual(fin(0, 19, 4), 405 + 12 + 1)


class CommandLine(unittest.TestCase):
    def run_tool(self, *args):
        return subprocess.run([sys.executable, "-I", os.path.join(HERE, "jamoset.py")] + list(args),
                              capture_output=True, text=True)

    def test_build_and_check(self):
        with tempfile.TemporaryDirectory() as d:
            src, eng, out = (os.path.join(d, n) for n in ("GAM.KOR", "SAN.ENG", "OUT.SVF"))
            open(src, "wb").write(make_source("844"))
            open(eng, "wb").write(bytes(4096))
            r = self.run_tool("--layout", "844", src, "--eng", eng, "-o", out, "--check")
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertIn("identical to johab844.py", r.stdout)
            self.assertIn("344 pieces", r.stdout)
            self.assertTrue(os.path.getsize(out) < 24000)

    def test_wrong_file_exits_with_a_message(self):
        with tempfile.TemporaryDirectory() as d:
            src = os.path.join(d, "X.KOR")
            open(src, "wb").write(b"\0" * 100)
            r = self.run_tool("--layout", "844", src, "-o", os.path.join(d, "o.svf"))
            self.assertNotEqual(r.returncode, 0)
            self.assertIn("not an 8x4x4 size", r.stderr)


# ---- fitting pieces to precomposed faces (jamofit.py, --from-svf / --from-ttf) -------------------------------------

import jamofit  # noqa: E402

NOTO = jamoset.default_mask_font()
LIBS = jamofit.libs_available()
PYLIB = []
if LIBS:		# the tool runs under python -I, which ignores PYTHONPATH: hand it the directory
    import freetype
    PYLIB = ["--pylib", os.path.dirname(os.path.dirname(os.path.abspath(freetype.__file__)))]


def ks2350():
    out = []
    for hi in range(0xB0, 0xC9):
        for lo in range(0xA1, 0xFF):
            out.append(ord(bytes([hi, lo]).decode("cp949")) - 0xAC00)
    return sorted(out)


def region_source(seed=3, cell=16):
    """A raw 8x4x4 source whose pieces keep to separate regions (initial top left, medial right, final bottom), every
    piece with its own random pixels, so some zero-error decomposition of the OR of any three exists."""
    rnd = random.Random(seed)
    assert cell == 16
    data = bytearray(360 * 32)
    for i in range(360):
        if i < 160:
            if i % 20 == 0:
                continue
            xs, ys = range(0, 9), range(0, 10)
        elif i < 248:
            if (i - 160) % 22 == 0:
                continue
            xs, ys = range(9, 16), range(0, 11)
        else:
            if (i - 248) % 28 == 0:
                continue
            xs, ys = range(0, 16), range(11, 16)
        for y in ys:
            row = 0
            for x in xs:
                if rnd.random() < 0.4:
                    row |= 0x8000 >> x
            data[i * 32 + y * 2:i * 32 + y * 2 + 2] = row.to_bytes(2, "big")
    return bytes(data)


ASCII_METRICS = {cp: (8 + cp % 3, cp % 2, 5 + cp % 4) for cp in range(0x20, 0x7F)}


def make_face(syllables, w=16, h=16, ascent=12, flags=5, shadow=255, codepage=0, seed=9):
    """A plain 1 bpp SVFN v2 face: ASCII with random glyphs and metrics, @p syllables {index: packed glyph}, one
    compatibility jamo. Returns (bytes, {cp: glyph bytes})."""
    rnd = random.Random(seed)
    nb = (w + 7) // 8
    glyphs = {cp: rnd.randbytes(nb * h) for cp in ASCII_METRICS}
    glyphs[0x3131] = rnd.randbytes(nb * h)
    for s, g in syllables.items():
        glyphs[0xAC00 + s] = g
    cps = sorted(glyphs)
    n = len(cps)
    cmap_off = 36
    metrics_off = cmap_off + 8 * n
    data_off = metrics_off + 4 * n
    metrics = b"".join(struct.pack("<BbBB", *(ASCII_METRICS.get(cp, (w, 0, w))), 0) for cp in cps)
    head = struct.pack(jamoset.HEADER, b"SVFN", 2, flags, 1, shadow, codepage, n, w, h, ascent, 0, 0, metrics_off,
                       data_off, n * nb * h)
    out = head + struct.pack("<I", cmap_off) + b"".join(struct.pack("<II", cp, i) for i, cp in enumerate(cps))
    out += metrics + b"".join(glyphs[cp] for cp in cps)
    return out, glyphs


def composed_face(present, blank=(), seed=3):
    """(face bytes, source bytes, {syllable: target glyph}) of the 844 pieces of region_source over @p present."""
    src = region_source(seed)
    target = {s: (bytes(32) if s in blank else johab844.compose(src, 0xAC00 + s)) for s in present}
    face, _ = make_face(target)
    return face, src, target


class FitFromFace(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.present = ks2350()
        cls.tmp = tempfile.TemporaryDirectory()
        cls.dir = cls.tmp.name
        cls.face, cls.src, cls.target = composed_face(cls.present)
        cls.path = os.path.join(cls.dir, "FACE.SVF")
        open(cls.path, "wb").write(cls.face)
        cls.out = os.path.join(cls.dir, "OUT.SVF")
        cls.r = subprocess.run([sys.executable, "-I", os.path.join(HERE, "jamoset.py"), "--layout", "844",
                                "--from-svf", cls.path, "--mask", "none", "-o", cls.out, "--check",
                                "--report", os.path.join(cls.dir, "r.json")], capture_output=True, text=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_ran(self):
        self.assertEqual(self.r.returncode, 0, self.r.stderr)
        self.assertIn("composed 11172 syllables", self.r.stdout)

    def test_composition_equals_the_target_for_every_present_syllable(self):
        out = open(self.out, "rb").read()
        f = jamoset.parse_svfn(out)
        t = jamoset.unpack(f.jset)
        for s in self.present:
            self.assertEqual(jamoset.compose(f, t, 0xAC00 + s), self.target[s], f"U+{0xAC00 + s:04X}")

    def test_report_is_perfect(self):
        import json
        q = json.load(open(os.path.join(self.dir, "r.json")))["quality"]
        self.assertEqual(q["present"], 2350)
        self.assertEqual((q["iou_mean"], q["iou_p5"], q["f1_tol1_mean"], q["exact"]), (1.0, 1.0, 1.0, 2350))
        self.assertEqual((q["blob_merged"], q["blob_split"]), (0, 0))
        self.assertIn("IoU 1.000", self.r.stdout)

    def test_parses_back_as_a_jamo_set_font(self):
        f = jamoset.parse_svfn(open(self.out, "rb").read())
        self.assertTrue(f.flags & jamoset.FLAG_JAMO)
        t = jamoset.unpack(f.jset)
        self.assertEqual((t.lc, t.tc, t.ni, t.nm, t.nf), (2, 2, 8, 4, 4))
        self.assertTrue(all(0xAC00 + s not in f.glyphs for s in self.present))
        self.assertTrue(any(cp >= jamoset.PIECE_BASE for cp in f.glyphs))

    def test_keeps_non_hangul_glyphs_cell_baseline_metrics_and_flags(self):
        _, want = make_face({})
        f = jamoset.parse_svfn(open(self.out, "rb").read())
        for cp, g in want.items():
            self.assertEqual(f.glyphs[cp], g, f"U+{cp:04X}")
        for cp, m in ASCII_METRICS.items():
            self.assertEqual(f.metrics[cp], m, f"U+{cp:04X}")
        self.assertEqual((f.cell_w, f.cell_h, f.ascent, f.bpp), (16, 16, 12, 1))
        self.assertEqual((f.flags & ~jamoset.FLAG_JAMO, f.shadow, f.codepage), (5, 255, 0))
        self.assertTrue(f.flags & jamoset.FLAG_JAMO)

    def test_deterministic(self):
        out2 = os.path.join(self.dir, "OUT2.SVF")
        r = subprocess.run([sys.executable, "-I", os.path.join(HERE, "jamoset.py"), "--layout", "844", "--from-svf",
                            self.path, "--mask", "none", "-o", out2], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(open(out2, "rb").read(), open(self.out, "rb").read())


class FitPresentOnly(unittest.TestCase):
    def test_blank_syllables_do_not_pull_pieces_toward_empty(self):
        present = ks2350()
        rnd = random.Random(11)
        blank = set(rnd.sample(present, 1600))		# 68 % blank: every pixel's majority would be off
        face, src, target = composed_face(present, blank)
        layout = jamoset.LAYOUTS["844"]
        targets = jamoset.svf_targets(jamoset.parse_svfn(face))
        self.assertEqual(sorted(targets), sorted(set(present) - blank))
        out, info = jamoset.fit_font(layout, targets, 16, 16, 12, base=face, keep_header=True)
        f = jamoset.parse_svfn(out)
        t = jamoset.unpack(f.jset)
        for s in targets:
            self.assertEqual(jamoset.compose(f, t, 0xAC00 + s), target[s], f"U+{0xAC00 + s:04X}")
        q = jamoset.fit_report(out, targets, 16, 16)
        self.assertEqual((q["present"], q["iou_mean"]), (750, 1.0))

    def test_unseen_pieces_borrow_a_sibling_not_blank(self):
        present = ks2350()[:600]
        face, src, target = composed_face(present)
        targets = jamoset.svf_targets(jamoset.parse_svfn(face))
        out, info = jamoset.fit_font(jamoset.LAYOUTS["844"], targets, 16, 16, 12, base=face, keep_header=True)
        self.assertGreater(info["unseen_pieces"]["sibling"], 0)
        f = jamoset.parse_svfn(out)
        t = jamoset.unpack(f.jset)
        seen = [set(), set(), set()]
        for s in present:
            for k, v in enumerate(jamoset.decompose(0xAC00 + s)):
                seen[k].add(v)
        checked = 0
        for cp in range(0xAC00, 0xD7A4):		# a syllable made of jamo the face draws never loses a piece
            if all(v in seen[k] for k, v in enumerate(jamoset.decompose(cp))):
                checked += 1
                for p in jamoset.pieces_of(t, cp):
                    self.assertTrue(any(f.glyphs[jamoset.PIECE_BASE + p]), f"U+{cp:04X} piece {p}")
        self.assertGreater(checked, 1000)


class SyllableAdvance(unittest.TestCase):
    """--from-svf keeps the step of the face's syllables (a 16 px glyph in a 26 px cell steps 16, and the engine
    centres what steps less than the game cell); without metrics the JSET advance stays 0, the cell width."""

    def face(self, advance, w=24):
        present = ks2350()[:30]
        glyphs = {s: bytes([0xFF] * (((w + 7) // 8) * 16)) for s in present}
        out, _ = make_face(glyphs, w=w)
        f = jamoset.parse_svfn(out)
        # one more syllable at the odd step, the rest at @p advance
        for cp in f.glyphs:
            if 0xAC00 <= cp <= 0xD7A3:
                f.metrics[cp] = (advance, 1, advance - 2)
        f.metrics[0xAC00 + present[0]] = (advance + 4, 0, 4)
        return f

    def test_modal_advance_of_the_drawn_syllables(self):
        self.assertEqual(jamoset.svf_advance(self.face(16)), 16)

    def test_no_metrics_or_too_wide_gives_none(self):
        f = self.face(16)
        f.metrics = {}
        self.assertIsNone(jamoset.svf_advance(f))
        self.assertIsNone(jamoset.svf_advance(self.face(300)))

    def test_written_into_the_jset_block(self):
        present = ks2350()
        face, _, _ = composed_face(present)
        targets = jamoset.svf_targets(jamoset.parse_svfn(face))
        out, _ = jamoset.fit_font(jamoset.LAYOUTS["844"], targets, 16, 16, 12, base=face, keep_header=True, advance=12)
        self.assertEqual(unpack_advance(out), 12)
        out, _ = jamoset.fit_font(jamoset.LAYOUTS["844"], targets, 16, 16, 12, base=face, keep_header=True)
        self.assertEqual(unpack_advance(out), 0)


class Metrics(unittest.TestCase):
    W = H = 8

    def g(self, *rows):
        return jamofit.from_rows([int(r, 2) for r in rows] + [0] * (self.H - len(rows)), self.W)

    def test_rows_round_trip(self):
        g = self.g("10000001", "01100110", "00000001")
        self.assertEqual(jamofit.to_rows(g, 8, 8)[:3], [0x81, 0x66, 0x01])
        self.assertEqual(g & 1, 1)				# top left pixel is bit 0

    def test_components_are_eight_connected(self):
        full = jamofit.full_mask(8, 8)
        self.assertEqual(jamofit.components(self.g("11000000", "00110000", "00001100"), 8, full), 1)
        self.assertEqual(jamofit.components(self.g("10000001", "00000000", "10000001"), 8, full), 4)
        self.assertEqual(jamofit.components(0, 8, full), 0)
        self.assertEqual(jamofit.components(self.g("10000001"), 8, full), 2)	# no wrap from the last column to the next row

    def test_iou_and_f1(self):
        full = jamofit.full_mask(8, 8)
        a, b = self.g("11110000"), self.g("00111100")
        self.assertAlmostEqual(jamofit.iou(a, b), 2 / 6)
        self.assertEqual(jamofit.iou(0, 0), 1.0)
        self.assertEqual(jamofit.f1_tol1(a, a, 8, full), 1.0)
        shifted = self.g("01111000")
        self.assertEqual(jamofit.f1_tol1(a, shifted, 8, full), 1.0)		# one pixel off is within tolerance
        self.assertLess(jamofit.f1_tol1(a, self.g("00000000", "00000000", "11110000"), 8, full), 1.0)

    def test_blob_metric_counts_merged_strokes(self):
        two = self.g("10001000", "10001000")
        one = self.g("11111000", "10001000")
        same = self.g("10001000", "10001000")
        q = jamofit.quality_report({0: two, 1: two, 2: one}, {0: one, 1: same, 2: one}, 8, 8)
        self.assertEqual((q["blob_merged"], q["blob_split"], q["present"]), (1, 0, 3))
        q = jamofit.quality_report({0: one}, {0: two}, 8, 8)
        self.assertEqual((q["blob_merged"], q["blob_split"]), (0, 1))

    def test_percentile(self):
        self.assertEqual(jamofit.percentile([1, 2, 3, 4, 5], 50), 3)
        self.assertAlmostEqual(jamofit.percentile(list(range(101)), 5), 5)


class FitErrors(unittest.TestCase):
    def run_tool(self, *args, env=None):
        e = dict(os.environ)
        e.pop("JAMOSET_PYLIB", None)
        e.update(env or {})
        return subprocess.run([sys.executable, "-I", os.path.join(HERE, "jamoset.py")] + list(args),
                              capture_output=True, text=True, env=e)

    def test_one_mode_only(self):
        r = self.run_tool("--layout", "844", "-o", "/dev/null")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("exactly one", r.stderr)

    def test_already_jamo_set_face_is_refused(self):
        with tempfile.TemporaryDirectory() as d:
            src, out = os.path.join(d, "G.KOR"), os.path.join(d, "J.SVF")
            open(src, "wb").write(make_source("844"))
            self.assertEqual(self.run_tool("--layout", "844", src, "-o", out).returncode, 0)
            r = self.run_tool("--layout", "844", "--from-svf", out, "--mask", "none", "-o", os.path.join(d, "x.svf"))
            self.assertNotEqual(r.returncode, 0)
            self.assertIn("already a jamo-set", r.stderr)

    @unittest.skipIf(LIBS, "freetype and uharfbuzz are importable here")
    def test_missing_libraries_say_what_to_install(self):
        r = self.run_tool("--layout", "844", "--from-ttf", "/nonexistent.ttf", "--size", "16", "-o", "/dev/null")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("pip install freetype-py uharfbuzz", r.stderr)
        self.assertIn("JAMOSET_PYLIB", r.stderr)


@unittest.skipUnless(LIBS and NOTO, "needs freetype-py, uharfbuzz and the NotoSansKR mask font")
class FitFromOutlineFont(unittest.TestCase):
    def test_from_ttf_with_noto_masks(self):
        import json
        with tempfile.TemporaryDirectory() as d:
            out, rep = os.path.join(d, "N.SVF"), os.path.join(d, "r.json")
            r = subprocess.run([sys.executable, "-I", os.path.join(HERE, "jamoset.py"), "--layout", "844",
                                "--from-ttf", NOTO, "--size", "16", "--present-only", "-o", out, "--check",
                                "--report", rep] + PYLIB, capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            f = jamoset.parse_svfn(open(out, "rb").read())
            self.assertTrue(f.flags & jamoset.FLAG_JAMO)
            self.assertEqual((f.cell_w, f.cell_h), (16, 16))
            self.assertIn(0x41, f.glyphs)					# ASCII from the font
            q = json.load(open(rep))
            self.assertGreater(q["quality"]["present"], 11000)
            self.assertGreater(q["quality"]["f1_tol1_mean"], 0.9)
            self.assertEqual(q["mask_font"], os.path.basename(NOTO))

    def test_present_only_skips_notdef(self):
        jf = jamoset._jamofit()
        # Noto has all syllables, so a font with none of them (U+AC00 range empty) is simulated by the flag alone
        targets, w, h, ascent, info, _ = jamoset.ttf_targets(NOTO, 16, None, None, True)
        self.assertEqual(len(targets), info["fitted"])
        self.assertTrue(all(targets.values()))


class DosFace24(unittest.TestCase):
    """The HBIOS 24x30 jamo half, the 12x24 ASCII faces, --advance/--ascent/--embolden/--symbols."""

    @staticmethod
    def hbios(seed=3):
        """243,900 bytes: 2,710 glyphs of 90 bytes; the first 360 an 8x4x4 set (ink in rows 3-26), blank slots blank."""
        rnd = random.Random(seed)
        data = bytearray(90 * 2710)
        for i in range(360):
            if i in [s * 20 for s in range(8)] + [160 + s * 22 for s in range(4)] + [248 + s * 28 for s in range(4)]:
                continue
            for y in range(3, 27):
                data[i * 90 + y * 3:i * 90 + y * 3 + 3] = rnd.randbytes(3)
        return bytes(data)

    def test_hbios_pieces_are_the_cropped_rows(self):
        data = self.hbios()
        src = jamoset.open_hbios24(data)
        self.assertEqual((src.w, src.h, src.count), (24, 24, 360))
        g = src.glyph(1)
        self.assertEqual(g.rows[0], int.from_bytes(data[90 + 9:90 + 12], "big"))		# file row 3 is cell row 0
        self.assertEqual(g.rows[23], int.from_bytes(data[90 + 26 * 3:90 + 27 * 3], "big"))

    def test_hbios_refuses_other_sizes(self):
        with self.assertRaises(jamoset.SourceError):
            jamoset.open_hbios24(bytes(243899))

    def test_layout_composes_like_844(self):
        data = self.hbios()
        out, _, _ = jamoset.build_from_source(jamoset.LAYOUTS["844-hbios24"], jamoset.open_hbios24(data))
        f = jamoset.parse_svfn(out)
        t = jamoset.unpack(f.jset)
        ref = jamoset.LAYOUTS["844"]
        self.assertEqual(jamoset.tables_for(jamoset.LAYOUTS["844-hbios24"]).init_set, jamoset.tables_for(ref).init_set)
        self.assertEqual(len(jamoset.compose(f, t, 0xD55C)), 72)

    def test_12x24_eng_faces(self):
        pft = bytearray(12288)
        pft[ord("H") * 48 + 2 * 20] = 0xFF		# row 20, columns 0-7 (12 wide, left aligned in 2 bytes)
        e = jamoset.read_eng(bytes(pft))
        self.assertEqual((e.w, e.h, e.baseline), (12, 24, 21))
        hb = bytearray(15360)
        hb[ord("H") * 60 + (3 + 5) * 2] = 0xFF		# cell row 5 (file row 8) of a 16x30 cell
        e = jamoset.read_eng(bytes(hb))
        self.assertEqual(e.baseline, 6)
        self.assertEqual(e.glyph(ord("H"))[5], 0xFF0)

    def test_eng_goes_at_the_left_steps_12_and_sets_the_ascent(self):
        pft = bytearray(12288)
        pft[ord("A") * 48 + 2 * 20] = 0xC0
        pft[ord("H") * 48 + 2 * 20] = 0xC0
        e = jamoset.read_eng(bytes(pft))
        out, _, _ = jamoset.build_from_source(jamoset.LAYOUTS["844-hbios24"], jamoset.open_hbios24(self.hbios()), e,
                                              advance=24)
        f = jamoset.parse_svfn(out)
        self.assertEqual(f.ascent, 21)
        self.assertEqual(f.metrics[ord("A")][0], 12)
        self.assertEqual(jamoset.unpack(f.jset).advance, 24)
        nb = 3
        row = int.from_bytes(f.glyphs[ord("A")][20 * nb:21 * nb], "big")
        self.assertEqual(row, 0xC00 << 12)

    def test_embolden_ors_each_piece_and_ascii_with_itself_shifted_right(self):
        data = self.hbios()
        plain = jamoset.build_from_source(jamoset.LAYOUTS["844-hbios24"], jamoset.open_hbios24(data))[0]
        bold = jamoset.build_from_source(jamoset.LAYOUTS["844-hbios24"], jamoset.open_hbios24(data), bold=True)[0]
        a, b = jamoset.parse_svfn(plain), jamoset.parse_svfn(bold)
        n = jamoset.PIECE_BASE + 5
        pa = int.from_bytes(a.glyphs[n][:3], "big")
        self.assertEqual(int.from_bytes(b.glyphs[n][:3], "big"), pa | (pa >> 1))

    def test_symbols_are_drawn_unless_the_base_has_them(self):
        out, _, _ = jamoset.build_from_source(jamoset.LAYOUTS["844-hbios24"], jamoset.open_hbios24(self.hbios()),
                                              ascent=21, symbols=True)
        f = jamoset.parse_svfn(out)
        rows = [int.from_bytes(f.glyphs[0x2026][y * 3:y * 3 + 3], "big") for y in range(24)]
        self.assertEqual([y for y, r in enumerate(rows) if r], [18, 19, 20])
        self.assertIn(0x25A1, f.glyphs)

    def test_command_line(self):
        with tempfile.TemporaryDirectory() as d:
            src, eng, out = (os.path.join(d, n) for n in ("GOTHIC24.FNT", "GASCII24.PFT", "O.SVF"))
            pft = bytearray(12288)
            pft[ord("H") * 48 + 40] = 0xFF
            for path, blob in ((src, self.hbios()), (eng, bytes(pft))):
                with open(path, "wb") as fh:
                    fh.write(blob)
            r = subprocess.run([sys.executable, os.path.join(HERE, "jamoset.py"), "--layout", "844-hbios24", src, "--eng",
                                eng, "--advance", "24", "--symbols", "--embolden", "--check", "-o", out],
                               capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertIn("composed 11172", r.stdout)
            with open(out, "rb") as fh:
                self.assertEqual(jamoset.parse_svfn(fh.read()).ascent, 21)


if __name__ == "__main__":
    unittest.main()
