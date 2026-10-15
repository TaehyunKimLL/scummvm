#!/usr/bin/env python3
"""Build a jamo-set SVF (SVFN flag bit 1 + JSET block) from a DOS Johab font.

A DOS Hangul system held no syllables, only positional jamo pieces (initial,
medial, final) in sets, and drew a syllable as the OR of two or three of them.
This tool writes such a font for the engine (graphics/hires_text/README.md,
"Jamo-set fonts"): the pieces become glyphs at U+F0000 + n and a JSET block
says, per syllable, which pieces to take. Every set system the research
documents is a named layout (docs/johab/2026-10-14-johab-font-research.md
section 3, docs/johab/2026-10-14-johab-font-formats.md section 2):

  844              8x4x4, the DOS standard (iolo, san, HMJ, $K*ZN16; 16/24/40/48 px)
  844-ihnm         8x4x4 with medial sets 1/3 for initials g and k (SAGA IHNM)
  844-hbios24      the jamo half of a 24x30 HBIOS / Taebaek 3.5 face ($KMYZN24, GOTHIC24, MY24, PILGI24; 243,900 bytes)
  1044-san         10x4x4 of san-1.0 (12,800 bytes)
  621              6x2x1, Apple II (6,144 bytes, byte-column glyphs)
  hanterm-johab    hanterm johab-1 BDF, 10x(3|4)x4
  dkb-printer-7x2x2    Dokkaebi printer HANGUL16/24.FNT (--face 0|1)
  dkb-printer-12x4x4   Dokkaebi printer HANGUL40/48.FNT (--face 0|1)
  kyra-lok         Kyra LoK MK15.BIT, jamo-major 10x4x4, 15x15

  python3 jamoset.py --layout 844 GAM.KOR --eng SAN.ENG -o KOGAMJ.SVF --check
  python3 jamoset.py --layout dkb-printer-12x4x4 --face 1 HANGUL48.FNT -o DKB48J.SVF

Only pieces some syllable (or compatibility jamo) uses are written, numbered
in source order. ASCII comes from --eng (a raw 8x16 DOS English font, row
major, 2048 or 4096 bytes; scaled by whole numbers to the cell) and/or --base
(an SVFN whose glyphs, other than syllables, are kept). Compatibility jamo
U+3131..U+3163 are drawn by piece: an initial's set-0 piece, a vowel's set-0
medial, a final-only consonant's set-0 final.

The raw-file readers and the selection tables come from the research tools in
~/work/johab-fonts/tools/kfont.py (g_rowmajor, g_bytecols, g_bitcols, open_*,
sel_*) and dkb_fnt.py, copied here as data and small functions.
"""

import argparse
import os
import re
import struct
import sys
import time

# ---- SVFN / JSET constants (graphics/hires_text/README.md) -------------------

MAGIC = b"SVFN"
VERSION_CMAP = 2
HEADER = "<4sHHBBHHBBBBHIII"
HEADER_V2 = 36
FLAG_VARIABLE = 1 << 0
FLAG_JAMO = 1 << 1
FLAG_MARKS_AT_ORIGIN = 1 << 2
SHADOW_NONE = 0xFF
CODEPAGE = 949
PIECE_BASE = 0xF0000
BLANK = 0xFFFF
JSET_HEADER = 12

# ---- Johab jamo helpers (kfont.py) -------------------------------------------

JUNG_FIELD = (3, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, 18, 19, 20, 21, 22, 23, 26, 27, 28, 29)  # V -> 5-bit value
KK = (0, 15)  # initials g and k; gg is NOT in the group

# Initial set by vowel (ㅏ..ㅣ) without and with a final; final set by vowel (research doc 3.2).
I844_NOFIN = (0, 0, 0, 0, 0, 0, 0, 0, 1, 3, 3, 3, 1, 2, 4, 4, 4, 2, 1, 3, 0)
I844_FIN = (5, 5, 5, 5, 5, 5, 5, 5, 6, 7, 7, 7, 6, 6, 7, 7, 7, 6, 6, 7, 5)
F844 = (0, 2, 0, 2, 1, 2, 1, 2, 3, 0, 2, 1, 3, 3, 1, 2, 1, 3, 3, 1, 1)
# iolo 6x2x1-fonts generate_hangul_syllables.py
I621_NOFIN = (0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 2, 2, 1, 1, 2, 2, 2, 1, 1, 2, 0)
I621_FIN = (3, 3, 3, 3, 3, 3, 3, 3, 4, 5, 5, 5, 4, 4, 5, 5, 5, 4, 4, 5, 3)
# hanterm-xf hfont.c convert_3_to_johabfont(): tables indexed by the 5-bit Johab medial / final value
HT_FCON1 = (0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 3, 3, 0, 0, 3, 1, 2, 4, 4, 4, 0, 0, 2, 1, 3, 0)
HT_FCON2 = (0, 0, 5, 5, 5, 5, 5, 5, 0, 0, 5, 5, 5, 6, 8, 8, 0, 0, 8, 6, 7, 9, 9, 9, 0, 0, 7, 6, 8, 5)
HT_VBASE = (0, 0, 0, 311, 314, 317, 320, 323, 0, 0, 326, 329, 332, 335, 339, 343, 0, 0, 347, 351, 355, 358, 361, 364,
            0, 0, 367, 370, 374, 378)
HT_VKIND = (0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0)
# Final variant by medial value: lcon_map[] of hanterm's own source (hfont.c in hanterm-xf 2.0.0/2.0.6, hangul.c in
# hanterm 3.1-3.3.1p18: identical in all seven). It is not the 8x4x4 final table F844: wae, oe, we, ui (15, 18, 22, 28)
# are 0, 0, 1, 0 here.
HT_LMAP = (0, 0, 0, 0, 2, 0, 2, 1, 0, 0, 2, 1, 2, 3, 0, 0, 0, 0, 0, 3, 3, 1, 1, 1, 0, 0, 3, 3, 0, 1)
# Dokkaebi printer fonts (dkb_fnt.py, from DP09/DP24/DP40/DP48.COM)
P16_INIT0 = [0] * 8 + [1, 3, 3, 3, 1, 2, 3, 3, 3, 2, 1, 3, 0]
P16_INIT1 = [4] * 8 + [5, 6, 6, 6, 5, 5, 6, 6, 6, 5, 5, 6, 4]
P16_FIN = [0] * 8 + [1] * 12 + [0]
P40_CLASS = [0, 1, 0, 1, 0, 1, 0, 1, 2, 4, 5, 4, 2, 3, 4, 5, 4, 3, 2, 4, 0]

# Compatibility jamo U+3131.. : (kind, index) with kind i = initial L, v = vowel V, f = final-only T.
COMPAT = ([("i", 0), ("i", 1), ("f", 3), ("i", 2), ("f", 5), ("f", 6), ("i", 3), ("i", 4), ("i", 5),
           ("f", 9), ("f", 10), ("f", 11), ("f", 12), ("f", 13), ("f", 14), ("f", 15), ("i", 6), ("i", 7),
           ("i", 8), ("f", 18), ("i", 9), ("i", 10), ("i", 11), ("i", 12), ("i", 13), ("i", 14), ("i", 15),
           ("i", 16), ("i", 17), ("i", 18)]
          + [("v", v) for v in range(21)])


# ---- glyph bitmaps: rows are ints, bit (w-1-x) is pixel x (MSB = left) ----------

class Glyph:
    __slots__ = ("w", "h", "rows")

    def __init__(self, w, h, rows):
        self.w, self.h, self.rows = w, h, rows

    def to_bytes(self):
        """Rows packed MSB first, padded to a byte: the SVFN 1bpp layout."""
        nb = (self.w + 7) // 8
        return b"".join((r << (nb * 8 - self.w)).to_bytes(nb, "big") for r in self.rows)

    def ink(self):
        cols = 0
        for r in self.rows:
            cols |= r
        used = [x for x in range(self.w) if cols >> (self.w - 1 - x) & 1]
        return (used[0], used[-1] - used[0] + 1) if used else (0, 0)


def g_rowmajor(d, off, w, h, bpr=None):
    bpr = bpr or (w + 7) // 8
    sh = bpr * 8 - w
    return Glyph(w, h, [int.from_bytes(d[off + y * bpr:off + (y + 1) * bpr], "big") >> sh for y in range(h)])


def g_bytecols(d, off, w, h):
    """Column-of-bytes order (iolo 6x2x1, Apple II)."""
    nb = (w + 7) // 8
    rows = [0] * h
    for x in range(nb):
        for y in range(h):
            rows[y] |= d[off + x * h + y] << (8 * (nb - 1 - x))
    return Glyph(w, h, [r >> (nb * 8 - w) for r in rows])


def g_bitcols(d, off, px):
    """Column-major, MSB = top (Dokkaebi HANGUL16/24 printer fonts)."""
    bpc = (px + 7) // 8
    rows = [0] * px
    for c in range(px):
        v = int.from_bytes(d[off + c * bpc:off + (c + 1) * bpc], "big")
        for r in range(px):
            if v >> (bpc * 8 - 1 - r) & 1:
                rows[r] |= 1 << (px - 1 - c)
    return Glyph(px, px, rows)


class SourceError(Exception):
    pass


class Source:
    """A source font's pieces: cell size and glyph(i) by the file's own piece index."""

    def __init__(self, w, h, count, getter):
        self.w, self.h, self.count, self._get = w, h, count, getter

    def glyph(self, i):
        if not 0 <= i < self.count:
            raise SourceError(f"piece {i} is outside the source's {self.count} glyphs")
        return self._get(i)


def check_blank_slots(d, off, gb, slots):
    bad = [i for i in slots if any(d[off + i * gb:off + (i + 1) * gb])]
    if len(bad) > 2:
        raise SourceError(f"not this layout: fill slots {bad[:4]} are not blank")


def slots_of(nini, nmed, nfin, med0, fin0):
    return ([s * 20 for s in range(nini)] + [med0 + s * 22 for s in range(nmed)]
            + [fin0 + s * 28 for s in range(nfin)])


def open_raw844(d, face=0, path=""):
    """Raw 8x4x4: 11520 (16x16), +2 B header (11522, T-Hangul), 25920 (24), 72000 (40), 103680 (48), 15584 (DKB2)."""
    px = {11520: 16, 11522: 16, 25920: 24, 72000: 40, 103680: 48, 15584: 16}.get(len(d))
    if px is None:
        raise SourceError(f"size {len(d)} is not an 8x4x4 size")
    off = 2 if len(d) == 11522 else 0
    gb = ((px + 7) // 8) * px
    check_blank_slots(d, off, gb, slots_of(8, 4, 4, 160, 248))
    return Source(px, px, 360, lambda i: g_rowmajor(d, off + i * gb, px, px))


def open_hbios24(d, face=0, path=""):
    """The jamo half of an IBM HBIOS / Taebaek 3.5 24 px face ($KMYZN24.FNT, GOTHIC24.FNT, MY24.FNT, PILGI24.FNT, 243,900
    bytes): 2,710 glyphs of 90 bytes (24 wide, 3 bytes a row, a 30 row cell with the ink in rows 3-26), the first 360 an
    8x4x4 set; the 2,350 syllables after them are not read."""
    if len(d) != 243900:
        raise SourceError(f"size {len(d)} is not an HBIOS 24x30 face (243900)")
    check_blank_slots(d, 0, 90, slots_of(8, 4, 4, 160, 248))
    return Source(24, 24, 360, lambda i: Glyph(24, 24, g_rowmajor(d, i * 90, 24, 30).rows[3:27]))


def open_ihnm(d, face=0, path=""):
    """SAGA IHNM's 8x4x4: the 360 jamo glyphs (11,520 bytes, 16x16) may be followed by other glyphs, which are ignored."""
    if len(d) < 360 * 32:
        raise SourceError(f"size {len(d)} is shorter than the 11520 bytes of 360 16x16 jamo glyphs")
    check_blank_slots(d, 0, 32, slots_of(8, 4, 4, 160, 248))
    return Source(16, 16, 360, lambda i: g_rowmajor(d, i * 32, 16, 16))


def open_1044(d, face=0, path=""):
    if len(d) != 12800:
        raise SourceError("10x4x4 san needs 12800 bytes")
    check_blank_slots(d, 0, 32, slots_of(10, 4, 4, 200, 288))
    return Source(16, 16, 400, lambda i: g_rowmajor(d, i * 32, 16, 16))


def open_621(d, face=0, path=""):
    if len(d) != 6144:
        raise SourceError("6x2x1 needs 6144 bytes")
    return Source(16, 16, 192, lambda i: g_bytecols(d, i * 32, 16, 16))


def open_kyra(d, face=0, path=""):
    if len(d) != 11550:
        raise SourceError("MK15.BIT needs 11550 bytes")
    return Source(15, 15, 385, lambda i: g_rowmajor(d, i * 30, 15, 15, 2))


def open_dkb16(d, face=0, path=""):
    """HANGUL16/24.FNT: two faces of 240 glyphs, column-major."""
    px = {15360: 16, 34560: 24}.get(len(d))
    if px is None:
        raise SourceError(f"size {len(d)} is not a Dokkaebi 7x2x2 printer font (15360 or 34560)")
    if face not in (0, 1):
        raise SourceError("--face is 0 or 1")
    gb = ((px + 7) // 8) * px
    return Source(px, px, 240, lambda i: g_bitcols(d, (face * 240 + i) * gb, px))


def open_dkb40(d, face=0, path=""):
    """HANGUL40/48.FNT: two faces of 440 glyphs, row-major."""
    px = {176000: 40, 253440: 48}.get(len(d))
    if px is None:
        raise SourceError(f"size {len(d)} is not a Dokkaebi 12x4x4 printer font (176000 or 253440)")
    if face not in (0, 1):
        raise SourceError("--face is 0 or 1")
    gb = ((px + 7) // 8) * px
    return Source(px, px, 440, lambda i: g_rowmajor(d, (face * 440 + i) * gb, px, px))


def open_bdf_glyphs(d, face=0, path=""):
    """A hanterm BDF whose ENCODING is the glyph number (johab-1); cells are the font's box."""
    t = d.decode("latin-1")
    if not t.startswith("STARTFONT"):
        raise SourceError("not a BDF")
    fb = re.search(r"FONTBOUNDINGBOX\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)", t)
    if not fb:
        raise SourceError("BDF without FONTBOUNDINGBOX")
    w, bh, _, by0 = map(int, fb.groups())
    asc = re.search(r"FONT_ASCENT\s+(\d+)", t)
    desc = re.search(r"FONT_DESCENT\s+(\d+)", t)
    ascent = int(asc.group(1)) if asc else bh + by0
    h = ascent + (int(desc.group(1)) if desc else -by0)
    chars = {}
    for block in t.split("STARTCHAR")[1:]:
        block = block.split("ENDCHAR")[0]
        enc = re.search(r"^ENCODING\s+(-?\d+)", block, re.M)
        if not enc:
            raise SourceError("BDF glyph without ENCODING")
        enc = int(enc.group(1))
        bbx = re.search(r"^BBX\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)", block, re.M)
        bm = re.search(r"^BITMAP[ \t]*\n(.*)", block, re.M | re.S)
        if not bbx or not bm:
            raise SourceError(f"BDF glyph {enc} has no BBX or BITMAP")
        bw, bhh, bx, by = map(int, bbx.groups())
        nb = (bw + 7) // 8
        lines = bm.group(1).split()
        if len(lines) < bhh or any(len(x) < nb * 2 for x in lines[:bhh]):
            raise SourceError(f"BDF glyph {enc}: BITMAP has {len(lines)} of {bhh} rows")
        chars[enc] = (bw, bhh, bx, by, [int(x[:nb * 2], 16) >> (nb * 8 - bw) for x in lines[:bhh]])
    missing = [i for i in range(529) if i not in chars]
    if missing:
        raise SourceError(f"BDF lacks glyph ENCODING {missing[0]} of the 529 johab-1 glyphs ({len(missing)} missing)")

    def get(i):
        c = chars.get(i)
        out = [0] * h
        if c:
            bw, bhh, bx, by, rows = c
            top = ascent - (by + bhh)
            for k, r in enumerate(rows):
                y = top + k
                if 0 <= y < h:
                    sh = w - (bx + bw)
                    out[y] = (r << sh) if sh >= 0 else (r >> -sh)
        return Glyph(w, h, out)

    return Source(w, h, 529, get)


# ---- layouts ----------------------------------------------------------------

class Layout:
    """A set system: classes, set selection per (lead class, vowel, tail class), source glyph per (set, jamo).

    A *_src function returns the source font's own glyph index, or None for a blank piece.
    """

    def __init__(self, name, opener, lc, tc, ni, nm, nf, lead_class, tail_class, init_set, med_set, fin_set,
                 init_src, med_src, fin_src, advance=0):
        self.name, self.opener = name, opener
        self.lc, self.tc, self.ni, self.nm, self.nf = lc, tc, ni, nm, nf
        self.lead_class, self.tail_class = lead_class, tail_class
        self.init_set, self.med_set, self.fin_set = init_set, med_set, fin_set
        self.init_src, self.med_src, self.fin_src = init_src, med_src, fin_src
        self.advance = advance


def _kk_class(L):
    return 0 if L in KK else 1


def _has_tail(T):
    return 1 if T else 0


def _844(name, flip, opener=None):
    """8x4x4; flip=1 swaps the medial sets of initials g/k and the rest (IHNM)."""
    return Layout(name, opener or (open_ihnm if flip else open_raw844), 2, 2, 8, 4, 4, _kk_class, _has_tail,
                  lambda lc, v, tc: (I844_FIN if tc else I844_NOFIN)[v],
                  lambda lc, v, tc: (lc ^ flip) + 2 * tc,
                  lambda lc, v, tc: F844[v] if tc else 0,
                  lambda s, L: s * 20 + L + 1,
                  lambda s, V: 160 + s * 22 + V + 1,
                  lambda s, T: 248 + s * 28 + T)


def _san1044():
    return Layout("1044-san", open_1044, 2, 2, 10, 4, 4, _kk_class, _has_tail,
                  lambda lc, v, tc: I844_NOFIN[v] + (5 if tc else 0),
                  lambda lc, v, tc: lc + 2 * tc,
                  lambda lc, v, tc: F844[v] if tc else 0,
                  lambda s, L: s * 20 + L + 1,
                  lambda s, V: 200 + s * 22 + V + 1,
                  lambda s, T: 288 + s * 28 + T)


def _621():
    return Layout("621", open_621, 1, 2, 6, 2, 1, lambda L: 0, _has_tail,
                  lambda lc, v, tc: (I621_FIN if tc else I621_NOFIN)[v],
                  lambda lc, v, tc: tc,
                  lambda lc, v, tc: 0,
                  lambda s, L: s * 20 + L + 1,
                  lambda s, V: 120 + s * 22 + V + 1,
                  lambda s, T: 164 + T)


def _kyra():
    return Layout("kyra-lok", open_kyra, 2, 2, 10, 4, 4, _kk_class, _has_tail,
                  lambda lc, v, tc: 2 * I844_NOFIN[v] + tc,
                  lambda lc, v, tc: 2 * lc + tc,
                  lambda lc, v, tc: F844[v] if tc else 0,
                  lambda s, L: 1 + 10 * L + s,
                  lambda s, V: 192 + 4 * V + s,
                  lambda s, T: 277 + 4 * (T - 1) + s,
                  advance=16)


def _hanterm():
    def tail_class(T):
        return 0 if T == 0 else (1 if T == 4 else 2)	# none, g-n (the nieun final), other

    def med_set(lc, v, tc):
        m = JUNG_FIELD[v]
        if HT_VKIND[m]:
            return (0 if lc == 0 else 1) + (2 if tc else 0)
        return (0, 2, 1)[tc]			# vertical vowels: none 0, nieun 2, other 1

    def med_src(s, V):
        m = JUNG_FIELD[V]
        if s >= (4 if HT_VKIND[m] else 3):
            return None				# a variant this vowel does not have
        return HT_VBASE[m] + s

    return Layout("hanterm-johab", open_bdf_glyphs, 2, 3, 10, 4, 4, _kk_class, tail_class,
                  lambda lc, v, tc: (HT_FCON2 if tc else HT_FCON1)[JUNG_FIELD[v]],
                  med_set,
                  lambda lc, v, tc: HT_LMAP[JUNG_FIELD[v]] if tc else 0,
                  lambda s, L: 10 * L + 1 + s,
                  med_src,
                  lambda s, T: 405 + 4 * (T - 1) + s)


def _dkb7():
    return Layout("dkb-printer-7x2x2", open_dkb16, 1, 2, 7, 2, 2, lambda L: 0, _has_tail,
                  lambda lc, v, tc: (P16_INIT1 if tc else P16_INIT0)[v],
                  lambda lc, v, tc: tc,
                  lambda lc, v, tc: P16_FIN[v] if tc else 0,
                  lambda s, L: s * 20 + L + 1,
                  lambda s, V: 140 + s * 22 + V + 1,
                  lambda s, T: 184 + 28 * s + T)


def _dkb12():
    return Layout("dkb-printer-12x4x4", open_dkb40, 2, 2, 12, 4, 4, _kk_class, _has_tail,
                  lambda lc, v, tc: P40_CLASS[v] + 6 * tc,
                  lambda lc, v, tc: lc + 2 * tc,
                  lambda lc, v, tc: F844[v] if tc else 0,
                  lambda s, L: s * 20 + L + 1,
                  lambda s, V: 240 + s * 22 + V + 1,
                  lambda s, T: 328 + s * 28 + T)


LAYOUTS = {l.name: l for l in (_844("844", 0), _844("844-ihnm", 1), _844("844-hbios24", 0, open_hbios24), _san1044(), _621(), _hanterm(), _dkb7(),
                               _dkb12(), _kyra())}


# ---- JSET tables -------------------------------------------------------------

class Tables:
    """The JSET block as Python lists (piece lists hold whatever numbering the caller uses)."""

    def __init__(self, lc, tc, ni, nm, nf, advance=0):
        self.lc, self.tc, self.ni, self.nm, self.nf, self.advance = lc, tc, ni, nm, nf, advance
        self.lead_class = [0] * 19
        self.tail_class = [0] * 28
        n = lc * 21 * tc
        self.init_set, self.med_set, self.fin_set = [0] * n, [0] * n, [0] * n
        self.init_piece = [BLANK] * (ni * 19)
        self.med_piece = [BLANK] * (nm * 21)
        self.fin_piece = [BLANK] * (nf * 27)
        self.compat = [BLANK] * 51


def decompose(cp):
    s = cp - 0xAC00
    return s // 588, (s // 28) % 21, s % 28


def pieces_of(t, cp):
    """The piece numbers of a syllable in draw order, as the engine reads them (JamoSetTable::pieces); None if not one."""
    if not 0xAC00 <= cp <= 0xD7A3:
        return None
    L, V, T = decompose(cp)
    cell = (t.lead_class[L] * 21 + V) * t.tc + t.tail_class[T]
    out = [t.init_piece[t.init_set[cell] * 19 + L], t.med_piece[t.med_set[cell] * 21 + V]]
    if T:
        out.append(t.fin_piece[t.fin_set[cell] * 27 + T - 1])
    return out


def tables_for(layout):
    """The layout's tables with the *source* glyph index as piece number (None -> blank)."""
    t = Tables(layout.lc, layout.tc, layout.ni, layout.nm, layout.nf, layout.advance)
    t.lead_class = [layout.lead_class(L) for L in range(19)]
    t.tail_class = [layout.tail_class(T) for T in range(28)]
    # A final set is read only for syllables with a final: for the no-final class it stays 0.
    final_classes = {t.tail_class[T] for T in range(1, 28)}
    for lc in range(layout.lc):
        for v in range(21):
            for tc in range(layout.tc):
                cell = (lc * 21 + v) * layout.tc + tc
                t.init_set[cell] = layout.init_set(lc, v, tc)
                t.med_set[cell] = layout.med_set(lc, v, tc)
                t.fin_set[cell] = layout.fin_set(lc, v, tc) if tc in final_classes else 0
    for s in range(layout.ni):
        for L in range(19):
            src = layout.init_src(s, L)
            t.init_piece[s * 19 + L] = BLANK if src is None else src
    for s in range(layout.nm):
        for V in range(21):
            src = layout.med_src(s, V)
            t.med_piece[s * 21 + V] = BLANK if src is None else src
    for s in range(layout.nf):
        for T in range(1, 28):
            src = layout.fin_src(s, T)
            t.fin_piece[s * 27 + T - 1] = BLANK if src is None else src
    for k, (kind, i) in enumerate(COMPAT):
        if kind == "i":
            t.compat[k] = t.init_piece[0 * 19 + i]
        elif kind == "v":
            t.compat[k] = t.med_piece[0 * 21 + i]
        else:
            t.compat[k] = t.fin_piece[0 * 27 + i - 1]
    return t


def blank_compat(t, have):
    """Leave the compatibility jamo (U+3131..U+3163) in @p have, which the font draws as whole glyphs, to those glyphs: the
    engine reads the piece table first (JamoGlyphSource::piecesOf), so a piece number there would hide them."""
    for k in range(51):
        if 0x3131 + k in have:
            t.compat[k] = BLANK


def used_pieces(t):
    """Piece numbers (source indices) any syllable or compatibility jamo can draw, sorted."""
    used = set()
    for cp in range(0xAC00, 0xD7A4):
        used.update(p for p in pieces_of(t, cp) if p != BLANK)
    used.update(p for p in t.compat if p != BLANK)
    return sorted(used)


def renumber(t, mapping):
    """A copy of @p t with every piece number passed through @p mapping (BLANK stays)."""
    out = Tables(t.lc, t.tc, t.ni, t.nm, t.nf, t.advance)
    out.lead_class, out.tail_class = list(t.lead_class), list(t.tail_class)
    out.init_set, out.med_set, out.fin_set = list(t.init_set), list(t.med_set), list(t.fin_set)
    # a piece no syllable can reach (an adaptive set only some lead classes use) is left blank
    conv = lambda ps: [p if p == BLANK else mapping.get(p, BLANK) for p in ps]
    out.init_piece, out.med_piece, out.fin_piece, out.compat = (conv(t.init_piece), conv(t.med_piece),
                                                                conv(t.fin_piece), conv(t.compat))
    return out


def jset_size(lc, tc, ni, nm, nf):
    return JSET_HEADER + 19 + 28 + 3 * lc * 21 * tc + 2 * (ni * 19 + nm * 21 + nf * 27 + 51)


def pack(t):
    out = bytearray(b"JSET" + struct.pack("<HBBBBBB", 1, t.lc, t.tc, t.ni, t.nm, t.nf, t.advance))
    out += bytes(t.lead_class) + bytes(t.tail_class)
    out += bytes(t.init_set) + bytes(t.med_set) + bytes(t.fin_set)
    for lst in (t.init_piece, t.med_piece, t.fin_piece, t.compat):
        out += struct.pack("<%dH" % len(lst), *lst)
    assert len(out) == jset_size(t.lc, t.tc, t.ni, t.nm, t.nf)
    return bytes(out)


def unpack(data):
    """Parse a JSET block the way the engine does (JamoSetTable); raises ValueError naming the field."""
    if len(data) < JSET_HEADER:
        raise ValueError("JSET length is shorter than its header")
    if data[:4] != b"JSET":
        raise ValueError("JSET magic is wrong")
    version, lc, tc, ni, nm, nf, adv = struct.unpack_from("<HBBBBBB", data, 4)
    if version != 1:
        raise ValueError(f"JSET version {version} is not supported")
    for name, v, hi in (("lead classes", lc, 8), ("tail classes", tc, 8), ("initial sets", ni, 24),
                        ("medial sets", nm, 16), ("final sets", nf, 16)):
        if not 1 <= v <= hi:
            raise ValueError(f"JSET {name} {v} is not 1..{hi}")
    if len(data) != jset_size(lc, tc, ni, nm, nf):
        raise ValueError(f"JSET length {len(data)} does not match its counts")
    t = Tables(lc, tc, ni, nm, nf, adv)
    p = JSET_HEADER
    t.lead_class = list(data[p:p + 19])
    p += 19
    t.tail_class = list(data[p:p + 28])
    p += 28
    for i, c in enumerate(t.lead_class):
        if c >= lc:
            raise ValueError(f"JSET leadClass[{i}] = {c} is not below {lc}")
    for i, c in enumerate(t.tail_class):
        if c >= tc:
            raise ValueError(f"JSET tailClass[{i}] = {c} is not below {tc}")
    final_classes = set(t.tail_class[1:])
    n = lc * 21 * tc
    for name, count, attr in (("initSet", ni, "init_set"), ("medSet", nm, "med_set"), ("finSet", nf, "fin_set")):
        vals = list(data[p:p + n])
        p += n
        for i, s in enumerate(vals):
            if s >= count and not (attr == "fin_set" and i % tc not in final_classes):
                raise ValueError(f"JSET {name}[{i}] = {s} is not below {count}")
        setattr(t, attr, vals)
    for attr, cnt in (("init_piece", ni * 19), ("med_piece", nm * 21), ("fin_piece", nf * 27), ("compat", 51)):
        setattr(t, attr, list(struct.unpack_from("<%dH" % cnt, data, p)))
        p += cnt * 2
    return t


# ---- SVFN reading and writing ---------------------------------------------------

class Svfn:
    pass


def parse_svfn(data):
    """An SVFN v2 file: header fields, glyph bytes and metrics by code point, and the JSET block bytes (or None)."""
    (magic, version, flags, bpp, shadow, codepage, count, cw, ch, ascent, r0, r1, metrics_off, data_off,
     data_size) = struct.unpack_from(HEADER, data)
    if magic != MAGIC or version != VERSION_CMAP:
        raise ValueError("not an SVFN v2 font")
    (cmap_off,) = struct.unpack_from("<I", data, HEADER_V2 - 4)
    f = Svfn()
    f.flags, f.bpp, f.cell_w, f.cell_h, f.ascent, f.count = flags, bpp, cw, ch, ascent, count
    f.shadow, f.codepage = shadow, codepage
    pitch = (cw * bpp + 7) // 8
    stride = pitch * ch
    f.glyphs, f.metrics = {}, {}
    for k in range(count):
        cp, idx = struct.unpack_from("<II", data, cmap_off + 8 * k)
        f.glyphs[cp] = data[data_off + idx * stride:data_off + (idx + 1) * stride]
        if flags & FLAG_VARIABLE:
            f.metrics[cp] = struct.unpack_from("<BbBB", data, metrics_off + idx * 4)[:3]
    f.jset = None
    if flags & FLAG_JAMO:
        start = cmap_off + 8 * count
        end = len(data)
        for off in ((metrics_off if flags & FLAG_VARIABLE else 0), data_off):
            if start <= off < end:
                end = off
        f.jset = data[start:end]
    return f


def write_svfn(cell_w, cell_h, ascent, glyphs, jset, extra_flags=0, shadow=SHADOW_NONE, codepage=CODEPAGE, bpp=1):
    """@p glyphs: [(cp, packed bytes, advance, ink_x, ink_w)]. Layout: header, code points, JSET, metrics, glyphs.
    @p bpp: 1, 2 or 8 (rows of cell_w * bpp bits, padded to a byte)."""
    glyphs = list(glyphs)
    n = len(glyphs)
    stride = ((cell_w * bpp + 7) // 8) * cell_h
    if any(len(g[1]) != stride for g in glyphs):
        raise ValueError("a glyph is not cell sized")
    cmap = b"".join(struct.pack("<II", cp, i) for cp, i in sorted((g[0], i) for i, g in enumerate(glyphs)))
    metrics = b"".join(struct.pack("<BbBB", min(255, g[2]), g[3], g[4], 0) for g in glyphs)
    data = b"".join(g[1] for g in glyphs)
    cmap_off = HEADER_V2
    metrics_off = cmap_off + len(cmap) + len(jset)
    data_off = metrics_off + len(metrics)
    head = struct.pack(HEADER, MAGIC, VERSION_CMAP, FLAG_VARIABLE | FLAG_JAMO | extra_flags, bpp, shadow, codepage, n,
                       cell_w, cell_h, min(255, ascent), 0, 0, metrics_off, data_off, len(data))
    return head + struct.pack("<I", cmap_off) + cmap + jset + metrics + data


# ---- the font -----------------------------------------------------------------

class EngCell:
    """A 12x24 ASCII face: glyph(cp) -> its rows as 16 bit ints (the file's two bytes, MSB left; MASCII24's right serifs and
    wide glyphs reach columns 12-15), the step @p adv (12) and the baseline row (the row below the ink of 'H')."""

    def __init__(self, w, h, getter, name, adv=None):
        self.w, self.h, self._get, self.name = w, h, getter, name
        self.adv = adv if adv is not None else w
        rows = self._get(ord("H"))
        used = [y for y, r in enumerate(rows) if r]
        self.baseline = used[-1] + 1 if used else h

    def glyph(self, cp):
        return self._get(cp)


def read_eng(d):
    """A raw 8x16 English font (2048 or 4096 bytes, row major) as bytes, or an EngCell for the 12x24 faces: an HBIOS
    $KROHN24 / Taebaek ROMAN24 / SANS24 (15,360 bytes: 256 glyphs of 60 bytes in a 16x30 cell, ink 12x24 in rows 3-26) or a
    Taebaek GASCII24 / MASCII24 .PFT (12,288 bytes: 256 glyphs of 48 bytes, 12x24 in 2 bytes a row)."""
    if len(d) == 15360:
        return EngCell(16, 24, lambda cp: g_rowmajor(d, cp * 60, 16, 30).rows[3:27], "hbios12x24", 12)
    if len(d) == 12288:
        return EngCell(16, 24, lambda cp: g_rowmajor(d, cp * 48, 16, 24).rows, "pft12x24", 12)
    if len(d) not in (2048, 4096):
        raise SourceError(f"an English font has 2048 or 4096 bytes (8x16), 15360 (HBIOS 12x24) or 12288 (PFT 12x24), "
                          f"not {len(d)}")
    return d


def eng_glyph(eng, cp, w, h, bold=False):
    """An ASCII glyph in a w x h cell: an 8x16 one scaled by whole numbers (k = h // 16, at least 1), top left; an EngCell
    one at its own size, centred in height. Returns (Glyph, advance)."""
    if isinstance(eng, EngCell):
        rows = eng.glyph(cp)
        y0 = (h - eng.h) // 2
        out = [0] * h
        for y, r in enumerate(rows):
            if 0 <= y0 + y < h:
                r = (r << (w - eng.w)) if w >= eng.w else (r >> (eng.w - w))
                out[y0 + y] = (r | (r >> 1)) if bold else r		# placed in the cell first: column 15 gains column 16
        return Glyph(w, h, out), eng.adv
    k = max(1, h // 16)
    y0 = (h - 16 * k) // 2 if h > 16 * k else 0
    rows = [0] * h
    for y in range(min(h - y0, 16 * k)):
        src = eng[cp * 16 + y // k]
        r = 0
        for x in range(min(w, 8 * k)):
            r |= (src >> (7 - x // k) & 1) << (w - 1 - x)
        rows[y0 + y] = r
    if bold:
        rows = [r | (r >> 1) for r in rows]
    return Glyph(w, h, rows), 8 * k


def clipped_row(w, spans):
    """A w bit row (MSB left) with the columns of each (x0, x1) span set, clipped to 0..w-1."""
    v = 0
    for x0, x1 in spans:
        for x in range(max(0, x0), min(w, x1 + 1)):
            v |= 1 << (w - 1 - x)
    return v


def symbol_glyphs(w, h, ascent, adv):
    """{cp: (packed, advance, ink x, ink w)} for U+2026 (three 3x3 dots on the baseline, 4 px apart, centred in @p adv) and
    U+25A1 (the missing-glyph box, a 2 px outline up to 16 px square on the baseline), drawn for faces that bring neither.
    Whatever does not fit the cell (a cell narrower than the 11 px run of dots or the 16 px box) is clipped, not wrapped."""
    left = (adv - 11) // 2
    run = clipped_row(w, [(left + k * 4, left + k * 4 + 2) for k in range(3)])
    dots = [0] * h
    for y in range(max(0, ascent - 3), min(h, ascent)):
        dots[y] = run
    n = max(0, min(16, h - 2, w))
    x0 = (w - n) // 2
    box = [0] * h
    for k in range(n):
        y = ascent - n + k
        if 0 <= y < h:
            box[y] = clipped_row(w, [(x0, x0 + n - 1)]) if k in (0, 1, n - 2, n - 1) else \
                clipped_row(w, [(x0, x0 + 1), (x0 + n - 2, x0 + n - 1)])
    out = {}
    for cp, rows, a in ((0x2026, dots, adv), (0x25A1, box, w)):
        g = Glyph(w, h, rows)
        ix, iw = g.ink()
        out[cp] = (g.to_bytes(), a, ix, iw)
    return out


class BoldSource:
    """A Source whose pieces are each ORed with themselves shifted right one pixel (a 1 px horizontal embolden; the OR of
    emboldened pieces is the emboldened syllable, so it stays a jamo set)."""

    def __init__(self, src):
        self.w, self.h, self.count, self._src = src.w, src.h, src.count, src

    def glyph(self, i):
        g = self._src.glyph(i)
        return Glyph(g.w, g.h, [r | (r >> 1) for r in g.rows])


def build(layout, data, face=0, eng=None, base=None, path=""):
    """The font file's bytes, and the (source index -> piece number) mapping."""
    return build_from_source(layout, layout.opener(data, face, path), eng, base)


def build_from_source(layout, src, eng=None, base=None, ascent=None, extra=None, keep_header=False, advance=None,
                      bold=False, symbols=False):
    """Write the font from @p src's pieces. @p extra: {cp: (packed glyph, advance, ink x, ink w)} added first (the base
    and --eng glyphs override it). @p keep_header: take the base's shadow byte, code page and all flag bits except the
    two this tool sets, not just MARKS_AT_ORIGIN. @p advance: the JSET advance byte (what a composed syllable steps
    by; 0 means the cell width), instead of the layout's. @p bold: embolden the pieces and the ASCII 1 px to the right.
    @p symbols: add U+2026 and U+25A1 (symbol_glyphs) unless the base or @p extra bring them."""
    if bold:
        src = BoldSource(src)
    tables = tables_for(layout)
    if advance is not None:
        tables.advance = advance
    parsed = parse_svfn(base) if base is not None else None
    whole = {cp for cp, g in (parsed.glyphs.items() if parsed else ()) if any(g)}
    whole |= {cp for cp, g in (extra or {}).items() if any(g[0])}
    blank_compat(tables, whole)
    used = used_pieces(tables)
    mapping = {s: n for n, s in enumerate(used)}
    pieces = renumber(tables, mapping)
    w, h = src.w, src.h
    if ascent is None and isinstance(eng, EngCell):
        ascent = (h - eng.h) // 2 + eng.baseline
    ascent = ascent or max(1, round(h * 0.875))
    glyphs = {}
    flags = 0
    shadow, codepage = SHADOW_NONE, CODEPAGE
    for s in used:
        g = src.glyph(s)
        ix, iw = g.ink()
        glyphs[PIECE_BASE + mapping[s]] = (g.to_bytes(), w, ix, iw)
    if extra:
        glyphs.update(extra)
    if base is not None:
        b = parsed
        if (b.bpp, b.cell_w, b.cell_h) != (1, w, h):
            raise SourceError(f"base is {b.cell_w}x{b.cell_h} {b.bpp} bpp, the pieces are {w}x{h} 1 bpp")
        ascent = b.ascent or ascent
        if keep_header:
            flags |= b.flags & ~(FLAG_VARIABLE | FLAG_JAMO)
            shadow, codepage = b.shadow, b.codepage
        else:
            flags |= b.flags & FLAG_MARKS_AT_ORIGIN
        for cp, gbytes in b.glyphs.items():
            if 0xAC00 <= cp <= 0xD7A3 or cp >= PIECE_BASE:
                continue
            m = b.metrics.get(cp)
            glyphs[cp] = (gbytes, m[0] if m else w, m[1] if m else 0, m[2] if m else 0)
    if eng is not None:
        for cp in range(0x20, 0x7F):
            g, adv = eng_glyph(eng, cp, w, h, bold)
            ix, iw = g.ink()
            glyphs[cp] = (g.to_bytes(), min(adv, w // 2) if w < 16 else adv, ix, iw)
    if symbols:
        adv = glyphs[0x20][1] if 0x20 in glyphs else w // 2
        for cp, g in symbol_glyphs(w, h, ascent, adv).items():
            glyphs.setdefault(cp, g)
    jset = pack(pieces)
    out = write_svfn(w, h, ascent, [(cp, g[0], g[1], g[2], g[3]) for cp, g in sorted(glyphs.items())], jset, flags,
                     shadow, codepage)
    return out, mapping, src


def blank_whole_compat(data):
    """The jamo-set face @p data with every compat jamo it has a whole glyph for left to that glyph (blank_compat), and the
    pieces nothing draws any more dropped. For faces written before build_from_source did this."""
    f = parse_svfn(data)
    if not f.flags & FLAG_JAMO or f.jset is None:
        raise ValueError("not a jamo-set font (flag bit 1 or the JSET block is missing)")
    t = unpack(f.jset)
    blank_compat(t, {cp for cp, g in f.glyphs.items() if cp < PIECE_BASE and any(g)})
    used = used_pieces(t)
    mapping = {p: n for n, p in enumerate(used)}
    t = renumber(t, mapping)
    glyphs = []
    for cp, g in f.glyphs.items():
        new = cp
        if cp >= PIECE_BASE:
            if cp - PIECE_BASE not in mapping:
                continue
            new = PIECE_BASE + mapping[cp - PIECE_BASE]
        m = f.metrics[cp]
        glyphs.append((new, g, m[0], m[1], m[2]))
    return write_svfn(f.cell_w, f.cell_h, f.ascent, glyphs, pack(t), f.flags & ~(FLAG_VARIABLE | FLAG_JAMO), f.shadow,
                      f.codepage)


# ---- composing and checking --------------------------------------------------------

def compose(svfn, t, cp):
    """The syllable as the engine draws it: the maximum coverage of its pieces at each pixel, i.e. the OR of their rows
    at 1 bpp (glyph_source_jamo.cpp maxRow). None if not a syllable."""
    nums = pieces_of(t, cp)
    if nums is None:
        return None
    bpp = getattr(svfn, "bpp", 1)
    stride = ((svfn.cell_w * bpp + 7) // 8) * svfn.cell_h
    out = bytearray(stride)
    for n in nums:
        if n == BLANK:
            continue
        piece = svfn.glyphs[PIECE_BASE + n]
        if bpp == 1:
            for k in range(stride):
                out[k] |= piece[k]
        elif bpp == 8:
            for k in range(stride):
                out[k] = max(out[k], piece[k])
        else:
            for k in range(stride):
                a, b = out[k], piece[k]
                if b:
                    v = 0
                    for sh in (6, 4, 2, 0):
                        v |= max((a >> sh) & 3, (b >> sh) & 3) << sh
                    out[k] = v
    return bytes(out)


def check(out_bytes, layout, src_bytes):
    """Parse the written font back and compose all 11,172 syllables with the engine's rule. For layout 844 at
    16x16 the result must equal johab844.py's composition of the source, byte for byte. Returns a message."""
    f = parse_svfn(out_bytes)
    if not f.flags & FLAG_JAMO or f.jset is None:
        raise ValueError("flag bit 1 or the JSET block is missing")
    t = unpack(f.jset)
    for p in [x for lst in (t.init_piece, t.med_piece, t.fin_piece) for x in lst] + t.compat:
        if p != BLANK and PIECE_BASE + p not in f.glyphs:
            raise ValueError(f"piece {p} has no glyph")
    composed = {cp: compose(f, t, cp) for cp in range(0xAC00, 0xD7A4)}
    msg = f"composed 11172 syllables from {len(f.glyphs)} glyphs, JSET {len(f.jset)} bytes"
    if layout.name == "adaptive":
        want = tables_for(layout)
        for attr in ("lead_class", "tail_class", "init_set", "med_set", "fin_set"):
            if getattr(t, attr) != getattr(want, attr):
                raise ValueError(f"the written JSET {attr} differs from the adaptive layout")
        msg += f"; tables are the adaptive layout's ({t.ni}/{t.nm}/{t.nf} sets, {t.lc}x{t.tc} classes)"
    if layout.name == "844" and (f.cell_w, f.cell_h) == (16, 16) and src_bytes is not None:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))	# python -I drops the script's directory
        import johab844
        off = 2 if len(src_bytes) == 11522 else 0
        jamo = src_bytes[off:off + 360 * 32]
        bad = [cp for cp in composed if composed[cp] != johab844.compose(jamo, cp)]
        if bad:
            raise ValueError(f"{len(bad)} syllables differ from johab844.py, first U+{bad[0]:04X}")
        msg += "; identical to johab844.py"
    return msg


# ---- fitting pieces to precomposed syllables (--from-ttf, --from-svf) ---------------------------------------

DEFAULT_MASK_FONT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "dists", "engine-data", "fonts",
                                 "fonts-cjk", "NotoSansKR-Regular.otf")


def _jamoaa():
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))	# python -I drops the script's directory
    import jamoaa
    return jamoaa


def _jamofit():
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))	# python -I drops the script's directory
    import jamofit
    return jamofit


def default_mask_font():
    """--mask-font's default: $JAMOSET_MASK_FONT, else the NotoSansKR the engine data ships, if it is there."""
    for p in (os.environ.get("JAMOSET_MASK_FONT"), DEFAULT_MASK_FONT):
        if p and os.path.exists(p):
            return p
    return None


def auto_radius(w, h):
    n = max(w, h)
    return 1 if n <= 16 else 2 if n <= 24 else 3


class FittedSource:
    """Pieces as a Source: a glyph per source index, from the fit (blank where nothing fills it)."""

    def __init__(self, w, h, rows_by_piece, count):
        self.w, self.h, self.count = w, h, count
        self._rows = rows_by_piece

    def glyph(self, i):
        return Glyph(self.w, self.h, list(self._rows.get(i) or [0] * self.h))


def piece_roles(layout):
    """{source index: (slot, set, jamo)} for every piece the layout's tables name (slot 0 initial, 1 medial, 2 final)."""
    t = tables_for(layout)
    roles = {}
    for slot, (lst, n) in enumerate(((t.init_piece, 19), (t.med_piece, 21), (t.fin_piece, 27))):
        for i, p in enumerate(lst):
            if p != BLANK:
                roles.setdefault(p, (slot, i // n, i % n))
    return t, roles


def _ancestor_piece(layout, roles, have, slot, st, jm):
    """For a layout with set ancestry (jamoadapt: Layout.fallback), the same jamo's piece in the nearest set this one
    was split from that @p have holds; else None."""
    order = getattr(layout, "fallback", None)
    if order is None or slot is None:
        return None
    for anc in order(slot, st):
        for q, r in roles.items():
            if r == (slot, anc, jm) and q in have:
                return q
    return None


AUTO_EXACT = 0.995		# a free fit this good means the face already is an OR of pieces: nothing for a mask to decide


def fit_font(layout, targets, w, h, ascent=None, eng=None, base=None, extra=None, keep_header=False,
             mask_font=None, radius=None, passes=12, pylib=None, mask_auto=False, advance=None, piece_masks=None,
             speck=0):
    """Fit the layout's pieces to @p targets ({syllable index: gint}) and write the font.

    With @p mask_auto the free fit comes first; if its mean IoU is at least AUTO_EXACT the face is itself an OR of
    pieces (a DOS face made from a jamo set) and the free pieces are kept, since a mask can only move them away from
    the exact answer; otherwise the masked fit is used. Returns (font bytes, info dict). Pieces no target syllable uses (a (set, jamo) pair the face never draws) take the
    same jamo's piece of another set (the one with most users) and, failing that, the mask font's variant.
    @p piece_masks: {piece: gint} confining the pieces (jamoaa.donor_masks; used instead of a mask font). @p speck:
    then drop components of at most this many pixels that the composed syllables do not need (jamoaa.remove_specks).
    """
    jf = _jamofit()
    t, roles = piece_roles(layout)
    syl_keys = {}
    for s in range(11172):
        ps = pieces_of(t, 0xAC00 + s)
        syl_keys[s] = tuple(None if p == BLANK else p for p in ps) + (None,) * (3 - len(ps))
    masks, mask_all, info = None, {}, {"mask_font": None}
    full = jf.full_mask(w, h)
    keys = {s: syl_keys[s] for s in targets}
    P = finfo = None
    if piece_masks is not None:
        mask_font = None
        masks = piece_masks
        info["mask_font"] = "donor support"
    if mask_font and mask_auto:
        P, finfo = jf.fit_pieces(keys, targets, w, h, None, passes)
        free_iou = jf.mean_iou(P, keys, targets)
        info["free_fit_iou"] = round(free_iou, 4)
        if free_iou >= AUTO_EXACT:
            info["mask_decision"] = "free fit kept (it already composes the face)"
            mask_font = None
        else:
            info["mask_decision"] = "masked fit (the free fit is not exact)"
            P = None
    if mask_font:
        mask_all, minfo = jf.mask_pieces(mask_font, syl_keys, w, h, targets, pylib)
        r = radius if radius is not None else auto_radius(w, h)
        masks = {k: jf.dilate(g, w, full, r) for k, g in mask_all.items()}
        info.update(minfo, mask_radius=r)
    if P is None:
        P, finfo = jf.fit_pieces(keys, targets, w, h, masks, passes, True if piece_masks is not None else None)
    users = finfo.pop("users")
    info.update(finfo)
    if speck:
        info["specks_removed"] = _jamoaa().remove_specks(P, keys, targets, w, h, speck)
    rows = {k: jf.to_rows(g, w, h) for k, g in P.items()}
    fallback = {"sibling": 0, "mask": 0, "blank": 0}
    for p in sorted(used_pieces(t)):
        if p in P:
            continue
        slot, st, jm = roles.get(p, (None, 0, 0))
        sib = [(users[q], -q) for q, (sl, s2, j2) in roles.items() if sl == slot and j2 == jm and q in P]
        anc = _ancestor_piece(layout, roles, P, slot, st, jm)
        if anc is not None:
            rows[p] = rows[anc]
            fallback["parent"] = fallback.get("parent", 0) + 1
        elif sib:
            rows[p] = rows[-max(sib)[1]]
            fallback["sibling"] += 1
        elif p in mask_all:
            rows[p] = jf.to_rows(mask_all[p], w, h)
            fallback["mask"] += 1
        else:
            fallback["blank"] += 1
    info["unseen_pieces"] = fallback
    count = max(roles) + 1 if roles else 0
    out, mapping, _ = build_from_source(layout, FittedSource(w, h, rows, count), eng, base, ascent, extra, keep_header, advance)
    info["pieces"] = len(mapping)
    return out, info


def glyph_gint(gbytes, w, h):
    nb = (w + 7) // 8
    jf = _jamofit()
    return jf.from_rows([int.from_bytes(gbytes[y * nb:(y + 1) * nb], "big") >> (nb * 8 - w) for y in range(h)], w)


def fit_report(out, targets, w, h):
    """Parse the written font back, compose every target syllable with the engine's rule, score against the targets."""
    jf = _jamofit()
    f = parse_svfn(out)
    if not f.flags & FLAG_JAMO or f.jset is None:
        raise ValueError("flag bit 1 or the JSET block is missing")
    t = unpack(f.jset)
    composed = {s: glyph_gint(compose(f, t, 0xAC00 + s), w, h) for s in targets}
    return jf.quality_report(targets, composed, w, h)


def svf_advance(face):
    """The step of the face's drawn syllables (the most common advance among them), or None for a face with no
    metrics table or one whose advance does not fit the JSET byte.

    A precomposed face keeps a syllable's advance in its metrics (neodgm's 16 in a 26 px cell); a jamo set steps by
    its JSET advance, which is the cell width when 0. Without this the composed syllable would step by the cell and
    the engine, which centres a syllable stepping less than the game cell, would no longer centre it.
    """
    count = {}
    for cp, gb in face.glyphs.items():
        m = face.metrics.get(cp)
        if m and 0xAC00 <= cp <= 0xD7A3 and any(gb):
            count[m[0]] = count.get(m[0], 0) + 1
    if not count:
        return None
    adv = max(count, key=lambda a: (count[a], a))
    return adv if 0 < adv < 256 else None


def svf_targets(face):
    """{syllable index: gint} of the non-blank precomposed syllables of a parsed 1 bpp SVFN face."""
    w, h = face.cell_w, face.cell_h
    out = {}
    for cp, gb in face.glyphs.items():
        if 0xAC00 <= cp <= 0xD7A3 and any(gb):
            out[cp - 0xAC00] = glyph_gint(gb, w, h)
    return out


# ---- Mac HangulTalk 1.0 'fbit' fonts (--layout mac-fbit-45sets) --------------------------------------------------------
#
# Gotic12/16 and Munjo12/16/24 of Korean System 4.1 (resource fork, type 'fbit'): 45 glyph sets of one cell size, a syllable
# is the OR of an initial, a medial and (with a final) a final glyph, picked by the 68k 'fdef' driver from 18 lookup tables
# (research: ~/work/johab-fonts/notes/mac.md, tools/mac_kfont.py). The set system is not a fixed table: mac_layout() derives
# the JSET tables from the font's own rule - lead and tail classes are the sets of jamo that no syllable tells apart, the
# initial sets are the distinct columns of initial glyphs, the medial sets are numbered per vowel - and every syllable
# composes to the Mac rendering bit for bit. It needs 8 lead classes, 4 tail classes and 20 initial sets, more than the
# first JSET readers took (4 / 4 / 16): the JSET limits are now 8 / 8 / 24 for classes and initial sets (16 medial, 16 final).

MAC_PLANE_TABLES = dict(A=(0x524, 32), B=(0x558, 32), C=(0x578, 32), D=(0x598, 32), Gm=(0x5b8, 32), G=(0x5d8, 10),
                        S=(0x5e2, 32), F=(0x602, 32), I=(0x622, 32), H=(0x642, 6), J=(0x648, 32), M=(0x668, 6),
                        O=(0x66e, 32), N=(0x68e, 8), K=(0x696, 32), R=(0x6b6, 32), P=(0x6d6, 32), Q=(0x6f6, 32))
MAC_LAYOUT = "mac-fbit-45sets"
MAC_REMAP = (0, 1, 0x13, 0x14, 0x15, 0x16, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0x17, 0x18, 0x12)


def mac_resources(d):
    """{type (bytes): {id: data}} of a raw Mac resource fork."""
    dofs, mofs, dlen, mlen = struct.unpack(">4I", d[:16])
    m = d[mofs:mofs + mlen]
    tl, nl, n1 = struct.unpack(">HHH", m[24:30])
    out = {}
    for i in range((n1 + 1) & 0xFFFF):
        typ, cnt1, rofs = struct.unpack(">4sHH", m[tl + 2 + i * 8:tl + 10 + i * 8])
        for j in range(cnt1 + 1):
            rid, _noff, ao = struct.unpack(">hHI", m[tl + rofs + j * 12:tl + rofs + j * 12 + 8])
            doff = dofs + (ao & 0xFFFFFF)
            ln = struct.unpack(">I", d[doff:doff + 4])[0]
            out.setdefault(typ, {})[rid] = d[doff + 4:doff + 4 + ln]
    return out


def mac_compose(tb, L, V, T):
    """Johab 5-bit fields -> [(set, glyph)] of the initial, medial and final planes (the 'fdef' driver of HangulTalk 1.0)."""
    out = []
    if L != 1:
        g = tb["A"][L]
        if T == 1:
            s = tb["B"][V] // 2
        else:
            c = tb["C"][V]
            s = 8 + (c & 0x3f) // 2
            if c & 0xc0:
                if tb["D"][T] & 0x80:
                    s += 1
                if c & 0x40:
                    g = MAC_REMAP[g]
        out.append((s, g))
    if V != 2:
        g = tb["Gm"][V]
        sel = tb["S"][V]
        if sel == 0:
            if T == 1:
                s = 18
            else:
                s = 19 + (1 if tb["D"][T] & 0x80 else 0)
                if tb["F"][L] & 0x80:
                    g = tb["G"][g]
        elif sel == 2:
            if T == 1:
                s = 21
                d0, d4 = tb["H"][g], tb["I"][L]
                mod = True
            else:
                s = 22 + tb["J"][L] // 2
                d0 = tb["K"][T]
                d4 = tb["M"][g] if d0 & 0xc0 else 0
                mod = bool(d0 & 0xc0)
            if mod and (d0 & 0xf0) & (d4 & 0xf0):
                g = (g + ((d0 + d4) & 0xf)) & 0xff
        else:
            s = 26
            if T != 1:
                s = 31
                d4 = tb["K"][T]
                d0 = tb["N"][g]
                if (d0 & 0xf0) & (d4 & 0xf0):
                    s = 36
            s += tb["O"][L] // 2
        out.append((s, g))
    if T != 1:
        s = 41
        d0 = tb["P"][V]
        if d0 & 0x80 and tb["Q"][L] & 0x80:
            s += 1
        s += (d0 & 0x3f) // 2
        out.append((s, tb["R"][T]))
    return out


class MacFbit:
    """A HangulTalk 1.0 'fbit' font file (a raw resource fork): its glyph sets and the 'fdef' lookup tables."""

    def __init__(self, data):
        try:
            res = mac_resources(data)
            self.fbit = next(iter(res[b"fbit"].values()))
            fdef = res[b"fdef"][3]
        except (KeyError, StopIteration, struct.error, IndexError):
            raise SourceError("not a HangulTalk 1.0 'fbit' resource fork (no fbit or fdef 3 resource)")
        if len(self.fbit) < 0x56 or self.fbit[2:6] != b"fbit" or len(fdef) < 0x716:
            raise SourceError("not a HangulTalk 1.0 'fbit' font")
        self.tb = {k: fdef[o:o + n] for k, (o, n) in MAC_PLANE_TABLES.items()}
        nset = struct.unpack(">H", self.fbit[0x54:0x56])[0] // 12
        if nset != 45:
            raise SourceError(f"{nset} glyph sets, not the 45 of mac-fbit-45sets")
        self.base = 0x56 + nset * 12
        if len(self.fbit) < self.base:
            raise SourceError(f"'fbit' resource is {len(self.fbit)} bytes, shorter than its {nset} set descriptors ({self.base})")
        self.sets = []
        for i in range(nset):
            w0, hi, off, h = struct.unpack(">4H", self.fbit[0x56 + i * 12:0x56 + i * 12 + 8])
            if not w0 or not h:
                raise SourceError(f"glyph set {i} has a zero width or height")
            if self.base + ((hi << 16) | off) > len(self.fbit):
                raise SourceError(f"glyph set {i} starts beyond the end of the 'fbit' resource")
            self.sets.append((w0 * 4, (hi << 16) | off, h))
        self.w, self.h = self.sets[0][0], self.sets[0][2]

    def set_glyph(self, s, g):
        bpr, off, h = self.sets[s]
        gb = bpr * h // 8
        o = self.base + off + g * gb
        v = int.from_bytes(self.fbit[o:o + gb], "big")
        return Glyph(bpr, h, [(v >> (gb * 8 - (y + 1) * bpr)) & ((1 << bpr) - 1) for y in range(h)])

    def syllable(self, s):
        """The Mac rendering of syllable index @p s (U+AC00 + s) as a Glyph."""
        L, V, T = s // 588, (s // 28) % 21, s % 28
        r = None
        for st, g in mac_compose(self.tb, L + 2, JUNG_FIELD[V], T + 1 if T <= 16 else T + 2):
            gl = self.set_glyph(st, g)
            r = gl if r is None else Glyph(gl.w, gl.h, [a | b for a, b in zip(r.rows, gl.rows)])
        return r


def ks_x_1001_syllables():
    """Syllable indices (U+AC00 + n) of the 2,350 KS X 1001 Hangul."""
    return sorted(ord(bytes([hi, lo]).decode("cp949")) - 0xAC00 for hi in range(0xB0, 0xC9) for lo in range(0xA1, 0xFF))


class MacSource:
    """The pieces of a MacFbit as a Source: piece n is the glyph numbered n over all 45 sets, in set order."""

    def __init__(self, mac):
        self.w, self.h, self.mac = mac.w, mac.h, mac
        starts = [off for _, off, _ in mac.sets] + [len(mac.fbit) - mac.base]
        self.first, n = [], 0
        for i, (bpr, off, h) in enumerate(mac.sets):
            self.first.append(n)
            n += (starts[i + 1] - off) // (bpr * h // 8)
        self.count = n

    def index(self, s, g):
        return self.first[s] + g

    def glyph(self, i):
        s = max(k for k in range(len(self.first)) if self.first[k] <= i)
        return self.mac.set_glyph(s, i - self.first[s])


def mac_layout(mac):
    """(Layout, MacSource) of a MacFbit, the JSET tables derived from its rule and checked against it by build/check."""
    src = MacSource(mac)
    jam = lambda L, V, T: (L + 2, JUNG_FIELD[V], T + 1 if T <= 16 else T + 2)

    def planes(L, V, T):
        d = {"i": None, "m": None, "f": None}
        for s, g in mac_compose(mac.tb, *jam(L, V, T)):
            d["i" if s <= 17 else "m" if s <= 40 else "f"] = (s, g)
        return d

    def classes(n, sig):
        ids, out = {}, []
        for x in range(n):
            out.append(ids.setdefault(sig(x), len(ids)))
        return out, len(ids)

    lead, lc = classes(19, lambda L: tuple((lambda d: (d["m"], d["f"] and d["f"][0]))(planes(L, V, T))
                                           for V in range(21) for T in range(28)))
    tail, tc = classes(28, lambda T: tuple((lambda d: (d["i"], d["m"], d["f"] and d["f"][0]))(planes(L, V, T))
                                           for L in range(19) for V in range(21)))
    rep_l = {c: lead.index(c) for c in range(lc)}
    rep_t = {c: tail.index(c) for c in range(tc)}
    init_cols, init_id = [], {}
    med_keys = [[] for _ in range(21)]
    fin_keys = []
    cell_init, cell_med, cell_fin = {}, {}, {}
    for c in range(lc):
        for V in range(21):
            for k in range(tc):
                L, T = rep_l[c], rep_t[k]
                col = tuple(planes(x, V, T)["i"] for x in range(19))
                if col not in init_id:
                    init_id[col] = len(init_cols)
                    init_cols.append(col)
                cell_init[c, V, k] = init_id[col]
                d = planes(L, V, T)
                if d["m"] not in med_keys[V]:
                    med_keys[V].append(d["m"])
                cell_med[c, V, k] = med_keys[V].index(d["m"])
                if d["f"] is not None:
                    if d["f"][0] not in fin_keys:
                        fin_keys.append(d["f"][0])
                    cell_fin[c, V, k] = fin_keys.index(d["f"][0])
    final_tc = {tail[T] for T in range(1, 28)}
    ni, nm, nf = len(init_cols), max(len(m) for m in med_keys), max(1, len(fin_keys))

    def init_src(s, L):
        x = init_cols[s][L]
        return None if x is None else src.index(*x)

    def med_src(s, V):
        return src.index(*med_keys[V][s]) if s < len(med_keys[V]) and med_keys[V][s] else None

    def fin_src(s, T):
        return src.index(fin_keys[s] if s < len(fin_keys) else 41, mac.tb["R"][jam(0, 0, T)[2]])

    layout = Layout(MAC_LAYOUT, None, lc, tc, ni, nm, nf, lambda L: lead[L], lambda T: tail[T],
                    lambda c, V, k: cell_init[c, V, k], lambda c, V, k: cell_med[c, V, k],
                    lambda c, V, k: cell_fin.get((c, V, k), 0), init_src, med_src, fin_src, advance=0)
    layout.final_tail_classes = final_tc
    return layout, src


MAC_GOLDEN = os.path.join(os.path.dirname(os.path.abspath(__file__)), "testdata", "mac-fbit-golden.json")


def mac_golden(source):
    """The golden entry (testdata/mac-fbit-golden.json) for the Mac font file named @p source (Munjo24.rsrc -> "munjo24"), or None.
    The golden bitmaps come from the research renderer mac_kfont.py, not from this file's mac_compose()."""
    import json
    try:
        with open(MAC_GOLDEN) as fh:
            faces = json.load(fh)["faces"]
    except (OSError, ValueError, KeyError):
        return None
    return faces.get(os.path.splitext(os.path.basename(source))[0].lower())


def mac_golden_problems(out, entry):
    """What differs between the font @p out (SVFN bytes) and a golden entry: the 2,350 KS X 1001 syllables composed from the
    font, hashed, and the sampled ones compared one by one. [] when identical."""
    import hashlib
    f = parse_svfn(out)
    t = unpack(f.jset)
    probs = []
    if (f.cell_w, f.cell_h) != (entry["w"], entry["h"]):
        return [f"cell {f.cell_w}x{f.cell_h}, golden is {entry['w']}x{entry['h']}"]
    bits = [compose(f, t, 0xAC00 + s) for s in ks_x_1001_syllables()]
    if hashlib.sha256(b"".join(bits)).hexdigest() != entry["sha256"]:
        probs.append("sha256 of the 2,350 KS X 1001 syllables differs")
    for cp, hexa in entry["samples"].items():
        if compose(f, t, int(cp, 16)).hex() != hexa:
            probs.append(f"U+{cp} differs")
    return probs


def mac_targets(mac):
    """{syllable index: gint} of the Mac rendering of all 11,172 syllables."""
    return {s: glyph_gint(mac.syllable(s).to_bytes(), mac.w, mac.h) for s in range(11172)}


def mac_compare(out, targets, w, h):
    """Compose every syllable of the written font and compare it with the Mac rendering: (quality report, the syllable
    indices that differ)."""
    f = parse_svfn(out)
    t = unpack(f.jset)
    composed = {s: glyph_gint(compose(f, t, 0xAC00 + s), w, h) for s in range(11172)}
    return _jamofit().quality_report(targets, composed, w, h), [s for s in range(11172) if composed[s] != targets[s]]


def ink_clipped(iw, ih, w, h, oy, top, bottom):
    """Whether the union ink box (iw x ih, pen-relative @p top and @p bottom rows) does not fit the w x h cell with the baseline
    at row @p oy: its top row is oy - top and its bottom row oy - bottom, which must be 0..h-1 (so oy - bottom == h is clipped)."""
    return bool(iw > w or ih > h or oy - top < 0 or oy - bottom >= h)


def ttf_targets(path, size, cell, baseline, present_only, pylib=None):
    """Render the font's 11,172 syllables 1 bpp at @p size px into a cell.

    The frame puts the union ink box of the drawn syllables in the middle of the cell (the baseline row may be
    given instead of centred). Returns (targets, w, h, ascent, info, ascii_glyphs).
    """
    jf = _jamofit()
    font = jf.OutlineFont(path, size, pylib)
    w, h = cell or (size, size)
    raws, boxes = {}, []
    for s in range(11172):
        gid = font.gid(0xAC00 + s)
        r = font.raw(gid) if (gid or not present_only) else ([], 0, 0, 0)
        raws[s] = (gid, r)
        b = jf.raw_box(r) if gid else None
        if b:
            boxes.append(b)
    if not boxes:
        raise SourceError(f"{path}: draws none of the Hangul syllables")
    left, top = min(b[0] for b in boxes), max(b[1] for b in boxes)
    right, bottom = max(b[2] for b in boxes), min(b[3] for b in boxes)
    iw, ih = right - left + 1, top - bottom + 1
    ox = -left + (w - iw) // 2
    oy = baseline if baseline is not None else top + (h - ih) // 2
    targets = {}
    for s, (gid, r) in raws.items():
        g = jf.place(r, ox, oy, w, h)
        if g or not present_only:
            targets[s] = g
    info = {"font": os.path.basename(path), "size": size, "cell": [w, h], "baseline": oy, "origin_x": ox,
            "ink_box": [iw, ih], "clipped": ink_clipped(iw, ih, w, h, oy, top, bottom),
            "drawn": sum(1 for s in raws if raws[s][0] and any(raws[s][1][0])), "fitted": len(targets)}
    ascii_glyphs = {}
    for cp in range(0x20, 0x7F):
        gid = font.gid(cp)
        r = font.raw(gid) if gid else ([], 0, 0, 0)
        g = jf.place(r, ox, oy, w, h)
        gl = Glyph(w, h, jf.to_rows(g, w, h))
        ix, iwid = gl.ink()
        ascii_glyphs[cp] = (gl.to_bytes(), min(255, int(round(font.advance(gid))) if gid else w // 2), ix, iwid)
    return targets, w, h, oy, info, ascii_glyphs


def write_report(path, info):
    import json
    with open(path, "w") as fh:
        json.dump(info, fh, indent=1, sort_keys=True, ensure_ascii=False)
        fh.write("\n")


def summary_line(label, q):
    return (f"{label}: {q['present']} syllables, IoU {q['iou_mean']:.3f} (p5 {q['iou_p5']:.3f}), "
            f"F1 tol1 {q['f1_tol1_mean']:.3f} (p5 {q['f1_tol1_p5']:.3f}), exact {q['exact']}, "
            f"blobs merged {q['blob_merged']} split {q['blob_split']}")


def _layout_arg(v):
    if v in LAYOUTS or v == MAC_LAYOUT:
        return v
    m = re.fullmatch(r"adaptive(?::(\d+),(\d+),(\d+))?", v)
    if m:
        lim = tuple(int(x) for x in m.groups()) if m.group(1) else (24, 16, 16)
        if not (8 <= lim[0] <= 24 and 4 <= lim[1] <= 16 and 4 <= lim[2] <= 16):
            raise argparse.ArgumentTypeError("adaptive limits are NI 8..24, NM 4..16, NF 4..16 (844 is 8,4,4)")
        return v
    raise argparse.ArgumentTypeError(f"unknown layout {v}")


def adaptive_limits(name):
    """(NI, NM, NF) of an 'adaptive[:NI,NM,NF]' layout argument, or None."""
    if not name.startswith("adaptive"):
        return None
    return tuple(int(x) for x in name.split(":")[1].split(",")) if ":" in name else (24, 16, 16)


def make_parser():
    ap = argparse.ArgumentParser(description="DOS Johab jamo-set font or outline font / precomposed face -> jamo-set SVF")
    ap.add_argument("source", nargs="?", help="the source font file (DOS Johab sources; not with --from-ttf/--from-svf)")
    ap.add_argument("--layout", required=True, type=_layout_arg, metavar="{" + ",".join(sorted(LAYOUTS) + [MAC_LAYOUT]) + ",adaptive[:NI,NM,NF]}",
                    help=f"a set system; {MAC_LAYOUT} takes a HangulTalk 1.0 'fbit' resource fork (see MacFbit); "
                    "adaptive (--from-ttf/--from-svf) starts from 844 and splits sets where the syllables sharing a "
                    "piece disagree, up to NI,NM,NF sets (default and most 24,16,16; jamoadapt.py)")
    ap.add_argument("--face", type=int, default=0, help="typeface of a printer font (0 or 1)")
    ap.add_argument("--eng", help="raw 8x16 English font for ASCII (2048 or 4096 bytes)")
    ap.add_argument("--base", help="SVFN v2 whose non-syllable glyphs are kept (same cell, 1 bpp)")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--advance", type=int, help="the step of a composed syllable (JSET advance byte; default: the layout's, "
                    "0 = the cell width)")
    ap.add_argument("--ascent", type=int, help="the SVF's baseline row (default: the --eng face's baseline for a 12x24 face, "
                    "else 7/8 of the cell)")
    ap.add_argument("--embolden", action="store_true", help="OR every piece and ASCII glyph with itself shifted right 1 px")
    ap.add_argument("--symbols", action="store_true", help="add U+2026 (three baseline dots) and U+25A1 (a box)")
    ap.add_argument("--check", action="store_true", help="compose all 11,172 syllables from the output and verify")
    ap.add_argument("--from-ttf", metavar="FONT", help="fit the pieces to the outline font's syllables at --size")
    ap.add_argument("--from-svf", metavar="FACE.SVF", help="decompose a 1 bpp precomposed SVFN face (its present "
                    "syllables); keeps its other glyphs, cell, baseline, metrics and flags")
    ap.add_argument("--size", type=int, help="--from-ttf: pixel size")
    ap.add_argument("--cell", help="--from-ttf: cell WxH (default SIZExSIZE)")
    ap.add_argument("--baseline", type=int, help="--from-ttf: baseline row from the cell top (default: ink box centred)")
    ap.add_argument("--present-only", action=argparse.BooleanOptionalAction, default=True,
                    help="--from-ttf: fit and score only syllables the font draws (the default; --from-svf always does). "
                    "--no-present-only also fits .notdef for every syllable the font lacks")
    ap.add_argument("--blank-compat", metavar="FACE.SVF", help="rewrite a jamo-set face with every compat jamo it has a whole "
                    "glyph for left to that glyph, and the pieces nothing draws dropped (-o OUT)")
    ap.add_argument("--mask-font", "--mask", dest="mask_font", metavar="FONT|none",
                    help="font with OpenType ljmo/vjmo/tjmo that confines the pieces. Default: $JAMOSET_MASK_FONT or "
                    "the shipped NotoSansKR when freetype/uharfbuzz are importable, used unless a free fit already "
                    "composes the target (IoU >= 0.995); an explicit FONT is always used; 'none' = free pieces")
    ap.add_argument("--mask-radius", type=int, help="mask dilation in pixels (default 1 up to 16 px, 2 up to 24, else 3)")
    ap.add_argument("--passes", type=int, default=12, help="most coordinate-descent passes (default 12)")
    ap.add_argument("--pylib", help="directory holding freetype and uharfbuzz (also $JAMOSET_PYLIB)")
    ap.add_argument("--report", metavar="FILE.json", help="write the quality report (IoU, F1, blobs) as JSON")
    ap.add_argument("--bpp", type=int, choices=(1, 2), default=1,
                    help="--from-ttf: 2 = anti-aliased pieces, 4 levels (jamoaa.py: contours assigned to the jamo of a 1 bpp "
                    "region fit, per-pixel median over the syllables, composed by the maximum; needs fontTools)")
    ap.add_argument("--aa-refine", type=int, default=2, metavar="N",
                    help="--bpp 2: passes of the level-error refinement after the median (default 2, 0 = median only)")
    ap.add_argument("--binarize", choices=("mono", "threshold", "ridge"), default="mono",
                    help="--from-ttf 1 bpp targets: mono = FreeType's monochrome renderer (the default); threshold = "
                    "exact-area coverage >= --threshold; ridge = that plus one-pixel ridges of faint strokes (fontTools)")
    ap.add_argument("--threshold", type=float, default=0.5, help="--binarize threshold|ridge: coverage cut (default 0.5)")
    ap.add_argument("--clean", action=argparse.BooleanOptionalAction, default=None,
                    help="noise cleanup (default on for --bpp 2 and --layout adaptive, off otherwise): pieces confined "
                    "to where their jamo's own contours (an outline source) or region (a bitmap source) put ink in at "
                    "least a quarter of the syllables, outlier syllables dropped from the consensus, specks the "
                    "composition does not need removed, faint AA levels only where half the syllables have them")
    return ap


def main_mac(ap, args):
    """--layout mac-fbit-45sets SOURCE.rsrc: the JSET tables from the font's own rule, then compare with the Mac rendering."""
    if not args.source or args.from_ttf or args.from_svf or args.blank_compat or args.eng or args.base:
        ap.error(f"{MAC_LAYOUT} takes the 'fbit' resource fork file as its source (no --from-*, --eng or --base)")
    started = time.time()
    try:
        with open(args.source, "rb") as fh:
            mac = MacFbit(fh.read())
        layout, src = mac_layout(mac)
        out, mapping, _ = build_from_source(layout, src, None, None, args.ascent or mac.h, None, False, args.advance,
                                            args.embolden, args.symbols)
        with open(args.output, "wb") as fh:
            fh.write(out)
        print(f"{args.output}: {len(out)} bytes, {len(mapping)} pieces of {mac.w}x{mac.h} from {args.source}; "
              f"{layout.lc} lead classes, {layout.tc} tail classes, {layout.ni}/{layout.nm}/{layout.nf} sets")
        if args.check:
            print("check:", check(out, layout, None))
            if args.embolden:
                print("mac: comparison skipped (--embolden changes the pieces; the Mac rendering is not emboldened)")
            else:
                q, bad = mac_compare(out, mac_targets(mac), mac.w, mac.h)
                print(summary_line("mac (jamoset.py's own copy of the rule, self-consistency only)", q)
                      + f"; differing syllables: {len(bad)}")
                if bad:
                    raise ValueError(f"{len(bad)} syllables differ from the Mac rendering, first U+{0xAC00 + bad[0]:04X}")
                golden = mac_golden(args.source)
                if golden is None:
                    print("golden: none for this font (no independent reference)")
                else:
                    probs = mac_golden_problems(out, golden)
                    if probs:
                        raise ValueError(f"differs from the golden rendering of {golden['key']}: {'; '.join(probs[:4])}")
                    print(f"golden: ok, 2350 KS X 1001 syllables identical to {golden['key']} (research renderer)")
        if args.report:
            write_report(args.report, {"layout": MAC_LAYOUT, "output_bytes": len(out), "pieces": len(mapping),
                                       "seconds": round(time.time() - started, 1)})
    except (SourceError, ValueError, OSError) as e:
        sys.exit(f"{args.source}: {e}")


def main_aa(ap, args, layout, eng, base, started, limits=None, clean=True):
    """--from-ttf FONT --bpp 2: the anti-aliased jamo set (jamoaa.build), its quality against the direct 2 bpp renders."""
    jamoaa = _jamoaa()
    if not args.from_ttf:
        ap.error("--bpp 2 works from an outline font (--from-ttf)")
    if not args.size:
        ap.error("--from-ttf needs --size")
    bad = [o for o, v in (("--eng", eng), ("--base", base), ("--embolden", args.embolden),
                          ("--no-present-only", not args.present_only), ("--binarize", args.binarize != "mono")) if v]
    if bad:
        ap.error(f"--bpp 2 does not take {', '.join(bad)}")
    cell = tuple(int(x) for x in args.cell.lower().split("x")) if args.cell else None
    out, info = jamoaa.build(layout, args.from_ttf, args.size, cell, args.baseline, args.passes, args.aa_refine,
                             args.symbols, args.advance, args.pylib, keep=True, clean=clean, adaptive=limits)
    ctx = info.pop("ctx")
    open(args.output, "wb").write(out)
    w, h = ctx["w"], ctx["h"]
    comp = jamoaa.composed_levels(out, ctx["direct"])
    q = jamoaa.aa_quality(ctx["direct"], comp, w, h)
    if limits:
        print(f"adaptive: {info['sets'][0]}/{info['sets'][1]}/{info['sets'][2]} sets, {info['classes'][0]} lead and "
              f"{info['classes'][1]} tail classes")
    info.update(aa_quality=q, join_loss=jamoaa.join_loss(ctx["donors"], ctx["direct"], w, h),
                output_bytes=len(out), seconds=round(time.time() - started, 1))
    print(f"{args.output}: {len(out)} bytes, {info['pieces']} pieces of {w}x{h} at 2 bpp from {args.from_ttf} in "
          f"{info['seconds']} s")
    print(f"aa: {q['present']} syllables, level error {q['level_mae']:.4f} (ink {q['level_mae_ink']:.3f}), exact ink "
          f"pixels {q['exact_pixels_ink']:.3f}, IoU at level 2 {q['iou_t2_mean']:.3f} (p5 {q['iou_t2_p5']:.3f}), "
          f"exact {q['exact']}; contours {info['contours']}")
    if args.check:
        print("check:", check(out, ctx["layout"], None))
    if args.report:
        write_report(args.report, info)


def fit_prep(layout, targets, w, h, origin, limits, clean, passes, outline=None, pylib=None, log=None, rounds=None):
    """Before a 1 bpp fit with --clean or --layout adaptive: the donors (each syllable's raster per slot: its own
    contours from @p outline (font path, size), placed at the targets' @p origin (ox, oy), when fontTools can read
    it, else the target split by a free region fit), the adaptive layout, and the donor-support masks. Donors depend
    on the region fit they are split by, so for an adaptive layout the regions are fitted again with the layout found
    and the layout made again from the new donors (@p rounds, default 3 from regions, 1 from contours). Returns
    (layout, masks or None, info)."""
    jf = _jamofit()
    aa = _jamoaa()
    info = {}
    face = None
    if outline:
        try:
            face = aa.OutlineFace(outline[0], outline[1], pylib)
        except jf.FitError as e:
            print(f"jamoset: {e}; donors from the 1 bpp regions instead", file=sys.stderr)
    rounds = rounds or (1 if face else 3)
    region_layout = LAYOUTS["844"]
    history = []
    for r in range(rounds if limits else 1):
        keys_r, _, roles_r = aa.syllable_keys(region_layout)
        P_r, _ = jf.fit_pieces({s: keys_r[s] for s in targets}, targets, w, h, None, passes)
        if face:
            donors, cinfo = aa.contour_donors(face, sorted(targets), keys_r, P_r, origin[0], origin[1], w, h, targets,
                                              roles_r)
            info["donors"], info["contours"] = "contours", cinfo
        else:
            donors = aa.pixel_donors(targets, keys_r, P_r, w, h)
            info["donors"] = "regions"
        if not limits:
            break
        import jamoadapt
        region_layout, ainfo = jamoadapt.adapt(donors, 1, limits, log=log)
        history.append(ainfo["sets"])
        info["adaptive"] = {k: v for k, v in ainfo.items() if k != "history"}
    if limits:
        layout = region_layout
        info["adaptive"]["rounds"] = history
    masks = None
    if clean:
        keys, _, _ = aa.syllable_keys(layout)
        masks = aa.donor_masks(keys, donors, w, h, 128, 0.25, 1)
    return layout, masks, info


def main():
    ap = make_parser()
    args = ap.parse_args()
    if args.layout == MAC_LAYOUT:
        return main_mac(ap, args)
    limits = adaptive_limits(args.layout)
    if limits and not (args.from_ttf or args.from_svf):
        ap.error("--layout adaptive fits pieces: give --from-ttf or --from-svf")
    layout = LAYOUTS["844"] if limits else LAYOUTS[args.layout]
    clean = args.clean if args.clean is not None else bool(limits or args.bpp == 2)
    modes = [m for m in (args.source, args.from_ttf, args.from_svf, args.blank_compat) if m]
    if len(modes) != 1:
        ap.error("give exactly one of: a source font file, --from-ttf, --from-svf, --blank-compat")
    label = modes[0]
    try:
        if args.blank_compat:
            out = blank_whole_compat(open(args.blank_compat, "rb").read())
            open(args.output, "wb").write(out)
            print(f"{args.output}: {len(out)} bytes, compat jamo with a whole glyph left to the glyph")
            return
        eng = read_eng(open(args.eng, "rb").read()) if args.eng else None
        base = open(args.base, "rb").read() if args.base else None
        if args.bpp == 2:
            return main_aa(ap, args, layout, eng, base, time.time(), limits, clean)
        if args.from_ttf or args.from_svf:
            started = time.time()
            jf = _jamofit()
            mask = args.mask_font
            mask_auto = mask is None
            if mask is None:
                mask = default_mask_font()
                if mask and not jf.libs_available(args.pylib):
                    print("jamoset: no freetype/uharfbuzz, fitting without the mask font (" + jf._LIB_HELP + ")",
                          file=sys.stderr)
                    mask = None
            elif mask.lower() == "none":
                mask = None
            elif not os.path.exists(mask):
                raise SourceError(f"mask font {mask} does not exist")
            if args.from_ttf:
                if not args.size:
                    ap.error("--from-ttf needs --size")
                cell = tuple(int(x) for x in args.cell.lower().split("x")) if args.cell else None
                if args.binarize != "mono":
                    if not args.present_only:
                        ap.error("--binarize threshold|ridge fits the drawn syllables only (--present-only)")
                    jamoaa = _jamoaa()
                    targets, w, h, ascent, tinfo, ascii_glyphs = jamoaa.outline_targets(
                        args.from_ttf, args.size, cell, args.baseline, args.binarize, args.threshold, args.pylib)
                else:
                    targets, w, h, ascent, tinfo, ascii_glyphs = ttf_targets(args.from_ttf, args.size, cell,
                                                                              args.baseline, args.present_only,
                                                                              args.pylib)
                extra = None if (eng is not None or base is not None) else ascii_glyphs
                pmasks, pinfo = None, {}
                if clean or limits:
                    layout, pmasks, pinfo = fit_prep(layout, targets, w, h, (tinfo["origin_x"], ascent), limits, clean,
                                                     args.passes, (args.from_ttf, args.size), args.pylib)
                out, info = fit_font(layout, targets, w, h, ascent, eng, base, extra, False, mask, args.mask_radius,
                                     args.passes, args.pylib, mask_auto, args.advance, pmasks,
                                     _jamoaa().speck_size(w, h) if clean else 0)
                info.update(pinfo)
                info["source"] = tinfo
            else:
                face = parse_svfn(open(args.from_svf, "rb").read())
                if face.bpp != 1:
                    raise SourceError(f"{args.from_svf} is {face.bpp} bpp, not 1")
                if face.flags & FLAG_JAMO:
                    raise SourceError(f"{args.from_svf} is already a jamo-set font")
                w, h = face.cell_w, face.cell_h
                targets = svf_targets(face)
                if not targets:
                    raise SourceError(f"{args.from_svf} has no non-blank Hangul syllables")
                pmasks, pinfo = None, {}
                if clean or limits:
                    layout, pmasks, pinfo = fit_prep(layout, targets, w, h, None, limits, clean, args.passes)
                out, info = fit_font(layout, targets, w, h, face.ascent, eng, open(args.from_svf, "rb").read(), None,
                                     True, mask, args.mask_radius, args.passes, args.pylib, mask_auto,
                                     args.advance if args.advance is not None else svf_advance(face), pmasks,
                                     _jamoaa().speck_size(w, h) if clean else 0)
                info.update(pinfo)
                info["source"] = {"face": os.path.basename(args.from_svf), "cell": [w, h], "ascent": face.ascent,
                                  "size": os.path.getsize(args.from_svf),
                                  "syllable_advance": svf_advance(face)}
            open(args.output, "wb").write(out)
            q = fit_report(out, targets, w, h)
            info.update(quality=q, layout=layout.name, output_bytes=len(out), seconds=round(time.time() - started, 1))
            print(f"{args.output}: {len(out)} bytes, {info['pieces']} pieces of {w}x{h} fitted from {label}"
                  f" in {info['seconds']} s")
            print(summary_line("fit", q))
            if limits:
                print(f"adaptive: {layout.ni}/{layout.nm}/{layout.nf} sets, {layout.lc} lead and {layout.tc} tail classes")
            if args.check:
                print("check:", check(out, layout, None))
            if args.report:
                write_report(args.report, info)
            return
        out, mapping, src = build_from_source(layout, layout.opener(open(args.source, "rb").read(), args.face, args.source),
                                               eng, base, args.ascent, None, False, args.advance, args.embolden,
                                               args.symbols)
        open(args.output, "wb").write(out)
        print(f"{args.output}: {len(out)} bytes, {len(mapping)} pieces of {src.w}x{src.h} from {args.source}")
        if args.check:
            print("check:", check(out, layout, open(args.source, "rb").read()))
    except (SourceError, ValueError, _jamofit().FitError) as e:
        sys.exit(f"{label}: {e}")


if __name__ == "__main__":
    main()
