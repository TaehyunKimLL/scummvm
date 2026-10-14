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
             "dkb-printer-7x2x2": sel_dkb16, "dkb-printer-12x4x4": sel_dkb40, "kyra-lok": sel_kyra}

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


if __name__ == "__main__":
    unittest.main()
