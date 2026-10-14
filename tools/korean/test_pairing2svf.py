#!/usr/bin/env python3
"""pairing2svf.py: the SVFN it writes and the maps it derives.

  python3 tools/korean/test_pairing2svf.py
"""

import base64
import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pairing2svf as p2s  # noqa: E402


def face(bpp=1, adv=None, w=8, h=2, chars="A가"):
    stride = (w * bpp + 7) // 8
    cells = bytearray()
    for i, _ in enumerate(chars):
        cell = bytearray(stride * h)
        cell[0] = 0x80 >> i if bpp == 1 else 0xC0 >> (2 * i)   # one pixel, column i
        cells += cell
    f = {"key": "k", "label": "L", "w": w, "h": h, "bpp": bpp, "ascent": 1,
         "chars": chars, "data": base64.b64encode(bytes(cells)).decode()}
    if adv:
        f["adv"] = adv
    return f


class SvfnTest(unittest.TestCase):
    def test_header_cmap_and_cells(self):
        b = p2s.svfn_bytes(face())
        (magic, ver, flags, bpp, shadow, cp, count, w, h, asc, _, _, moff, doff, dsize) = \
            struct.unpack_from(p2s.HEADER, b)
        self.assertEqual((magic, ver, bpp, count, w, h, asc), (b"SVFN", 2, 1, 3, 8, 2, 1))
        self.assertFalse(flags & p2s.FLAG_VARIABLE)
        self.assertEqual(moff, 0)
        (cmap_off,) = struct.unpack_from("<I", b, 32)
        self.assertEqual(cmap_off, p2s.HEADER_V2)
        entries = [struct.unpack_from("<II", b, cmap_off + 8 * k) for k in range(count)]
        # A, 가, and the drawn U+25A1 the face lacked
        self.assertEqual(entries, [(0x41, 0), (0x25A1, 2), (0xAC00, 1)])
        cells = base64.b64decode(face()["data"])
        self.assertEqual(b[doff:doff + len(cells)], cells)
        self.assertEqual(dsize, len(cells) + 2)
        self.assertTrue(any(b[doff + len(cells):doff + dsize]))

    def test_advances_give_a_width_table_with_ink_boxes(self):
        b = p2s.svfn_bytes(face(bpp=2, adv=[5, 8]))
        (_, _, flags, _, _, _, count, _, _, _, _, _, moff, _, _) = struct.unpack_from(p2s.HEADER, b)
        self.assertTrue(flags & p2s.FLAG_VARIABLE)
        m = [struct.unpack_from("<BbBB", b, moff + 4 * i) for i in range(count)]
        self.assertEqual(m[:2], [(5, 0, 1, 0), (8, 1, 1, 0)])
        self.assertEqual(m[2][0], 8)          # the drawn box steps a cell

    def test_a_face_with_its_own_box_keeps_it(self):
        b = p2s.svfn_bytes(face(chars="A\u25a1"))
        self.assertEqual(struct.unpack_from(p2s.HEADER, b)[6], 2)

    def test_a_short_data_blob_is_refused(self):
        f = face()
        f["data"] = base64.b64encode(b"\0").decode()
        with self.assertRaises(ValueError):
            p2s.svfn_bytes(f)


TEMPLATE = """; old header
[map]
version=2
[render]
scale=2
blend=off
[render:clut8]
blend=off
[fonts]
ui=data:KO2350G.SVF
body=data:KO2350B.SVF
[fonts:clut8]
ui=data:KO2350.SVF
[font]
face=ui
missing=u+25a1
[font.1]
face=body
[font.40]
face=body
size=18
[glyphs]
0x5e = u+2026
"""


def pick(fid, key, bpp=1):
    return {"id": fid, "korean": {"key": key, "label": key, "bpp": bpp}}


class MapTest(unittest.TestCase):
    def setUp(self):
        fonts = [pick("0", "a"), pick("1", "b"), pick("4", "b")]
        self.aliases = {"a": "kp1", "b": "kp2"}
        self.text = p2s.make_map(TEMPLATE, "LB1", fonts, self.aliases, "auto")
        self.sections = {n: l for n, l in p2s.parse_sections(self.text) if n}

    def test_fonts_name_the_new_files_and_presets_are_gone(self):
        fonts = [l for l in self.sections["fonts"][1:] if not l.startswith(";")]
        self.assertEqual(fonts, [f"kp1={p2s.svf_name('a')}", f"kp2={p2s.svf_name('b')}",
                                 "ui=data:KO2350G.SVF", "body=data:KO2350B.SVF", "cjk=data:NotoSansKR-Bold.otf"])
        self.assertNotIn("fonts:clut8", self.sections)
        self.assertNotIn("render:clut8", self.sections)
        self.assertIn("blend=auto", self.sections["render"])

    def test_each_font_id_gets_its_face_with_the_old_one_behind_it(self):
        self.assertIn("face=kp1,ui,cjk", self.sections["font.0"])      # new section: [font]'s old face
        self.assertIn("face=kp2,body,cjk", self.sections["font.1"])
        self.assertIn("face=kp2,ui,cjk", self.sections["font.4"])
        self.assertIn("face=kp1,ui,cjk", self.sections["font"])
        # font 40 was not in the selection: the default first, its old face behind, its size stays
        self.assertIn("face=kp1,body,cjk", self.sections["font.40"])
        self.assertIn("size=18", self.sections["font.40"])
        self.assertIn("0x5e = u+2026", self.sections["glyphs"])

    def test_the_one_game_font_goes_to_font_or_font_0(self):
        agi = p2s.make_map("[map]\n[font]\nface=ui\n", "LSL1 AGI", [pick("builtin", "a")], {"a": "kp1"}, "off")
        self.assertIn("face=kp1,cjk", dict((n, l) for n, l in p2s.parse_sections(agi) if n)["font"])
        mm = p2s.make_map("[map]\n[font]\n[font.0]\nface=dlg\n", "MM", [pick("builtin", "a")], {"a": "kp1"}, "off")
        self.assertIn("face=kp1,cjk", dict((n, l) for n, l in p2s.parse_sections(mm) if n)["font.0"])

    def test_charset_ids_map_to_their_number(self):
        self.assertEqual(p2s.font_section("cs2"), "font.2")
        self.assertEqual(p2s.font_section("300"), "font.300")
        self.assertIsNone(p2s.font_section("builtin"))

    def test_measured_fonts_get_their_shadow_section(self):
        fonts = [{"id": "cs3", "lineh": 15, "korean": {"key": "a", "label": "a", "bpp": 2, "h": 30}},
                 {"id": "cs6", "lineh": 28, "korean": {"key": "b", "label": "b", "bpp": 2, "h": 56}},
                 {"id": "cs1", "lineh": 8, "korean": {"key": "a", "label": "a", "bpp": 2, "h": 16}}]
        deco = {"cs3": {"o": 2}, "cs6": {"d": [2, 2]}}
        text = p2s.make_map("[map]\n[shadow]\nmode=game\n[font]\nface=ui\n", "G", fonts,
                            {"a": "kp1", "b": "kp2"}, "auto", decorations=deco)
        s = {n: l for n, l in p2s.parse_sections(text) if n}
        self.assertIn("width=3", s["shadow.3"])          # 0.75 x 2 x 2 x (30/30)
        self.assertIn("mode=outline", s["shadow.3"])
        self.assertIn("offset=4", s["shadow.6"])         # 2 x 2 x (56/56)
        self.assertIn("mode=drop", s["shadow.6"])        # a drop even where the patch byte says outline
        # cs1 has no measurement: the patch byte (mode=game) picks the mode, the
        # sizes still grow with the face - 16 px on an 8 px line is 2x
        self.assertEqual([l for l in s["shadow.1"][1:] if l and not l.startswith(";")], ["width=1.5", "offset=2"])
        self.assertIn("mode=game", s["shadow"])
        names = [n for n, _ in p2s.parse_sections(text) if n]
        self.assertGreater(names.index("shadow.3"), names.index("fonts"))

    def test_an_outline_with_a_drop_is_a_stroke(self):
        # Indy4 cs3: a 1 px outline plus a copy at (-1, +1), Korean face 30 px on a 15 px line
        fonts = [{"id": "cs3", "lineh": 15, "korean": {"key": "a", "label": "a", "bpp": 2, "h": 30}}]
        text = p2s.make_map("[map]\n[shadow]\nmode=game\n[font]\nface=ui\n", "G", fonts, {"a": "kp1"}, "auto",
                            decorations={"cs3": {"o": 1, "d": [-1, 1]}})
        s = {n: l for n, l in p2s.parse_sections(text) if n}
        self.assertIn("mode=stroke", s["shadow.3"])
        self.assertIn("width=1.5", s["shadow.3"])        # 0.75 x 2 x 1
        self.assertIn("shadow=-2,2", s["shadow.3"])      # (-1, +1) game px x 2

    def test_a_big_face_gets_its_multiple(self):
        # 36 px on a 12 px line is 3x: drop 1 game px -> 3, unmeasured outline 0.75 x 3
        fonts = [{"id": "cs3", "lineh": 12, "korean": {"key": "a", "label": "a", "bpp": 2, "h": 36}},
                 {"id": "cs4", "lineh": 12, "korean": {"key": "a", "label": "a", "bpp": 1, "h": 36}}]
        text = p2s.make_map("[map]\n[shadow]\nmode=game\n[font]\nface=ui\n", "G", fonts, {"a": "kp1"}, "auto",
                            decorations={"cs3": {"d": [1, 1]}})
        s = {n: l for n, l in p2s.parse_sections(text) if n}
        self.assertIn("offset=3", s["shadow.3"])
        self.assertIn("width=2.25", s["shadow.4"])
        self.assertIn("offset=3", s["shadow.4"])

    def test_sci_fonts_get_no_shadow_section(self):
        text = p2s.make_map("[map]\n[font]\nface=ui\n", "LB1", [{"id": "300", "lineh": 12,
                            "korean": {"key": "a", "label": "a", "bpp": 1, "h": 24}}], {"a": "kp1"}, "off")
        self.assertNotIn("shadow.300", [n for n, _ in p2s.parse_sections(text)])


if __name__ == "__main__":
    unittest.main()
