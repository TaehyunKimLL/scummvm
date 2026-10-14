#!/usr/bin/env python3
"""decoration.py: the decoration it reads off a 2bpp SCUMM face.

  python3 tools/korean/scumm-fonts/test_decoration.py
"""

import os
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import decoration as deco  # noqa: E402


def letter(w=12, h=14):
    """A plain 'L'-ish letter: colour 1 only, room round it."""
    g = [[0] * w for _ in range(h)]
    for y in range(3, 10):
        g[y][4] = g[y][5] = 1
    for x in range(4, 9):
        g[9][x] = 1
    return g


def dropped(g, dx, dy):
    out = [r[:] for r in g]
    for y, r in enumerate(g):
        for x, v in enumerate(r):
            if v == 1 and not g[y + dy][x + dx]:
                out[y + dy][x + dx] = 2
    return out


def outlined(g, reach, box=None):
    """Colour 2 round the letter: reach on every side, or box = (left, up, right, down)."""
    left, up, right, down = box or (reach,) * 4
    out = [r[:] for r in g]
    h, w = len(g), len(g[0])
    for y in range(h):
        for x in range(w):
            if g[y][x]:
                continue
            # pixel (x, y) is covered when a letter pixel lies within the box reaching to it
            if any(g[y - j][x - i] == 1 for j in range(-up, down + 1) for i in range(-left, right + 1)
                   if 0 <= y - j < h and 0 <= x - i < w):
                out[y][x] = 2
    return out


class ClassifyTest(unittest.TestCase):
    def test_a_one_pixel_drop(self):
        self.assertEqual(deco.classify([dropped(letter(), 1, 1)] * 5), {"d": [1, 1]})

    def test_a_two_pixel_drop(self):
        self.assertEqual(deco.classify([dropped(letter(), 2, 2)] * 5), {"d": [2, 2]})

    def test_a_one_pixel_outline(self):
        self.assertEqual(deco.classify([outlined(letter(), 1)] * 5), {"o": 1})

    def test_a_two_pixel_outline(self):
        self.assertEqual(deco.classify([outlined(letter(), 2)] * 5), {"o": 2})

    def test_an_outline_with_a_drop_down_left(self):
        # Indy4 cs3: 2 px left and below, 1 px up and right - a 1 px outline plus a
        # copy at (-1, +1), the engine's stroke
        g = outlined(letter(), 0, box=(2, 1, 1, 2))
        self.assertEqual(deco.classify([g] * 5), {"o": 1, "d": [-1, 1]})

    def test_a_filled_drop_is_a_drop_with_fill(self):
        # DOTT cs5: colour 2 fills right, below and the corner
        g = outlined(letter(), 0, box=(0, 0, 1, 1))
        self.assertEqual(deco.classify([g] * 5), {"d": [1, 1], "fill": True})

    def test_no_colour_2_is_no_decoration(self):
        self.assertIsNone(deco.classify([letter()] * 5))

    def test_a_drop_with_a_stray_pixel_is_still_a_drop(self):
        g = dropped(letter(), 1, 1)
        g[0][0] = 2
        self.assertEqual(deco.classify([g] * 5), {"d": [1, 1]})


def classic_font(glyphs, bpp=2):
    """A CharsetRendererClassic font blob: bpp, height, numChars, offsets, glyphs."""
    n = 128
    body = bytearray()
    offs = [0] * n
    base = 4 + 4 * n
    for c, rows in glyphs.items():
        h, w = len(rows), len(rows[0])
        bits = "".join(format(v, "0%db" % bpp) for r in rows for v in r)
        bits += "0" * (-len(bits) % 8)
        offs[c] = base + len(body)
        body += bytes([w, h, 0, 0]) + int(bits, 2).to_bytes(len(bits) // 8, "big")
    return bytes([bpp, 14]) + struct.pack("<H", n) + struct.pack("<%dI" % n, *offs) + bytes(body)


class ReadTest(unittest.TestCase):
    def test_glyphs_of_a_classic_font(self):
        g = dropped(letter(), 1, 1)
        bpp, rows = deco.classic_glyphs(classic_font({ord("A"): g, ord("b"): g}))
        self.assertEqual(bpp, 2)
        self.assertEqual(sorted(rows), [ord("A"), ord("b")])
        self.assertEqual(rows[ord("A")], g)

    def test_a_v4_charset_file(self):
        g = outlined(letter(), 1)
        blob = classic_font({ord("A"): g})
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "904.LFL")
            with open(path, "wb") as f:
                f.write(b"\0" * 21 + blob)
            faces = deco.read_v4([path])
        self.assertEqual(list(faces), ["cs4"])
        self.assertEqual(deco.classify(list(faces["cs4"][1].values())), {"o": 1})

    def test_a_v3_charset_file(self):
        # Indy3: 98.LFL is charset 1, XOR 0xFF; u16 size, then [4]=numChars,
        # [5]=height, a width table and numChars 8x8 1bpp glyphs
        n = 128
        res = bytearray(4) + bytes([n, 8]) + bytes([8] * n) + bytes(8 * n)
        res[6 + n + 8 * ord("A")] = 0x18
        blob = struct.pack("<H", len(res) + 2) + bytes(res)
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "98.LFL")
            with open(path, "wb") as f:
                f.write(bytes(b ^ 0xFF for b in blob))
            faces = deco.read_v3([path])
        self.assertEqual(list(faces), ["cs1"])
        bpp, glyphs = faces["cs1"]
        self.assertEqual(bpp, 1)
        self.assertEqual(glyphs[ord("A")][0], [0, 0, 0, 1, 1, 0, 0, 0])
        self.assertEqual(deco.face_decoration(bpp, glyphs), {"none": True})


class FaceTest(unittest.TestCase):
    def test_a_1bpp_classic_face_has_none(self):
        # DOTT cs4: one colour, and CharsetRendererClassic draws Latin text unshadowed
        self.assertEqual(deco.face_decoration(1, {ord("A"): letter()}), {"none": True})

    def test_a_2bpp_face_is_classified(self):
        self.assertEqual(deco.face_decoration(2, {ord("A"): dropped(letter(), 1, 1)}), {"d": [1, 1]})

    def test_a_2bpp_face_without_colour_2_has_none(self):
        self.assertEqual(deco.face_decoration(2, {ord("A"): letter()}), {"none": True})


if __name__ == "__main__":
    unittest.main()
