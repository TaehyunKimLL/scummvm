#!/usr/bin/env python3
"""Fit jamo-set pieces to precomposed 1 bpp syllables (used by jamoset.py --from-ttf / --from-svf).

A glyph here is one Python int, "gint": pixel (x, y) of a w x h cell is bit y * (w + 1) + x, so bit 0 is the top left
pixel and column w of every row is a zero guard column that keeps shifts from wrapping into the next row. Sets of
glyphs are then handled with whole-int operations, which is fast enough in plain Python (no numpy needed): the
fit below takes seconds for the 2,350 syllables of a 46x54 face.

The algorithm is the "2c hybrid" of the generation review (runs/jamo-gen-review/claude/work/proto2.py):

  1. Mask. When a mask font is given (Noto Sans KR, which has the OpenType ljmo/vjmo/tjmo jamo variants), HarfBuzz
     shapes each syllable as conjoining jamo with the font's precomposed syllables hidden, so the font's own
     initial/medial/final variants come out. For every (set, jamo) piece of the layout the variant most of its
     syllables use is rasterised, scaled and placed so that the mask font's ink box fills the target's, and dilated
     by a few pixels. A piece may only set pixels inside its mask. Without a mask font the pieces are free.
  2. Masked coordinate descent. Pieces are taken in slot order (initial, medial, final). Given the other two slots,
     a piece pixel is on exactly when, among the target syllables that use the piece and are not already inked there
     by another piece, more have ink than have not (the exact per-pixel optimum of the summed Hamming error). The
     passes repeat a fixed number of times or until the error stops falling; ties go to "off". No randomness, so the
     result is a function of the inputs.

Only syllables that are really present (non-blank, and for a TTF not mapped to .notdef) are fitted and scored.

Metrics (quality_report): per-syllable IoU and a 1 px tolerant F1 (a pixel is matched if the other glyph has ink
within one pixel, 8-neighbourhood), mean and 5th percentile (linear interpolation); and the blob metric: the number
of syllables whose composed glyph has fewer 8-connected ink components than the target's, i.e. composing merged
strokes the target keeps apart ("blob_merged"); "blob_split" counts the opposite. Stray ink: composed pixels more
than a pixel away from any target ink ("stray_px_mean" per syllable, "stray_syllables" with any) - the noise a
piece carrying a fragment of another jamo leaves where that jamo is not drawn.
"""

import os
import sys

# ---- optional libraries -------------------------------------------------------------------------------------------

_LIB_HELP = ("--from-ttf and the mask font need freetype-py and uharfbuzz (pip install freetype-py uharfbuzz); to use a "
             "directory that already holds them, set JAMOSET_PYLIB=DIR (or pass --pylib DIR)")


class FitError(Exception):
    pass


def load_libs(pylib=None):
    """Import freetype and uharfbuzz (lazily; only the outline-font paths need them). Returns (freetype, uharfbuzz)."""
    for d in (pylib, os.environ.get("JAMOSET_PYLIB")):
        for part in (d or "").split(os.pathsep):
            if part and part not in sys.path:
                sys.path.append(part)
    try:
        import freetype
        import uharfbuzz
    except ImportError as e:
        raise FitError(f"cannot import {e.name}: {_LIB_HELP}")
    return freetype, uharfbuzz


def libs_available(pylib=None):
    try:
        load_libs(pylib)
        return True
    except FitError:
        return False


# ---- gint helpers ---------------------------------------------------------------------------------------------------

def stride(w):
    return w + 1


def full_mask(w, h):
    row = (1 << w) - 1
    out = 0
    for y in range(h):
        out |= row << (y * (w + 1))
    return out


def from_rows(rows, w):
    """gint of rows given as ints with the leftmost pixel in bit w-1 (the SVFN / Glyph layout)."""
    W = w + 1
    g = 0
    for y, r in enumerate(rows):
        if r:
            g |= int(format(r, "0%db" % w)[::-1], 2) << (y * W)
    return g


def to_rows(g, w, h):
    W = w + 1
    row = (1 << w) - 1
    return [int(format((g >> (y * W)) & row, "0%db" % w)[::-1], 2) for y in range(h)]


def spread8(a, w, full):
    """The 8-neighbourhood dilation by one pixel."""
    W = w + 1
    t = a | (a << 1) | (a >> 1)
    return (t | (t << W) | (t >> W)) & full


def dilate(a, w, full, r):
    for _ in range(r):
        a = spread8(a, w, full)
    return a


def components(a, w, full):
    """Number of 8-connected ink components."""
    n = 0
    while a:
        seed = a & -a
        while True:
            nxt = spread8(seed, w, full) & a
            if nxt == seed:
                break
            seed = nxt
        a &= ~seed
        n += 1
    return n


def bbox(g, w, h):
    """(x0, y0, x1, y1) inclusive of the ink, or None."""
    if not g:
        return None
    W = w + 1
    cols = 0
    ys = []
    row = (1 << w) - 1
    for y in range(h):
        r = (g >> (y * W)) & row
        if r:
            ys.append(y)
            cols |= r
    return ((cols & -cols).bit_length() - 1, ys[0], cols.bit_length() - 1, ys[-1])


def union_bbox(glyphs, w, h):
    box = None
    for g in glyphs:
        b = bbox(g, w, h)
        if b is None:
            continue
        box = b if box is None else (min(box[0], b[0]), min(box[1], b[1]), max(box[2], b[2]), max(box[3], b[3]))
    return box


# ---- metrics ------------------------------------------------------------------------------------------------------

def iou(a, b):
    u = (a | b).bit_count()
    return 1.0 if u == 0 else (a & b).bit_count() / u


def f1_tol1(a, b, w, full):
    na, nb = a.bit_count(), b.bit_count()
    if na == 0 and nb == 0:
        return 1.0
    if na == 0 or nb == 0:
        return 0.0
    prec = (a & spread8(b, w, full)).bit_count() / na
    rec = (b & spread8(a, w, full)).bit_count() / nb
    return 0.0 if prec + rec == 0 else 2 * prec * rec / (prec + rec)


def percentile(vals, q):
    s = sorted(vals)
    if not s:
        return 0.0
    pos = (len(s) - 1) * q / 100.0
    lo = int(pos)
    hi = min(lo + 1, len(s) - 1)
    return s[lo] + (s[hi] - s[lo]) * (pos - lo)


def quality_report(target, composed, w, h):
    """@p target, @p composed: {syllable: gint} over the same present syllables. Returns the metric dict."""
    full = full_mask(w, h)
    ious, f1s = [], []
    merged = split = exact = 0
    cc_t = cc_c = 0
    stray = stray_syl = 0
    for s in sorted(target):
        t, c = target[s], composed[s]
        far = (c & ~spread8(t, w, full)).bit_count()
        stray += far
        stray_syl += far > 0
        ious.append(iou(t, c))
        f1s.append(f1_tol1(t, c, w, full))
        ct, cc_ = components(t, w, full), components(c, w, full)
        cc_t += ct
        cc_c += cc_
        merged += cc_ < ct
        split += cc_ > ct
        exact += t == c
    n = len(ious)
    return {
        "present": n,
        "iou_mean": round(sum(ious) / n, 4) if n else 0.0,
        "iou_p5": round(percentile(ious, 5), 4),
        "f1_tol1_mean": round(sum(f1s) / n, 4) if n else 0.0,
        "f1_tol1_p5": round(percentile(f1s, 5), 4),
        "exact": exact,
        "blob_merged": merged,
        "blob_split": split,
        "components_target_mean": round(cc_t / n, 3) if n else 0.0,
        "components_composed_mean": round(cc_c / n, 3) if n else 0.0,
        "stray_px_mean": round(stray / n, 4) if n else 0.0,
        "stray_syllables": stray_syl,
    }


# ---- the fit ------------------------------------------------------------------------------------------------------

def _bits(g):
    """Positions of the set bits of @p g, ascending."""
    out = []
    s = bin(g)[:1:-1]
    i = s.find("1")
    while i >= 0:
        out.append(i)
        i = s.find("1", i + 1)
    return out


STRICT_PASSES = 1


def fit_pieces(syl_pieces, targets, w, h, masks=None, passes=12, strict_first=None):
    """Fit pieces to targets.

    @p syl_pieces: {syllable: (initial piece key, medial key, final key or None)}; a key is any hashable (None or
    BLANK-like entries are given as None and skipped); each key belongs to one slot. @p targets: {syllable: gint}, the
    present syllables only. @p masks: {key: gint} limiting where a piece may have ink, or None for free pieces.
    @p strict_first: start with the strict pass (default: for free pieces only; the donor-support masks of --clean
    are loose enough to need it too).
    Returns ({key: gint} for the keys that some target uses, info dict).
    """
    syls = sorted(targets)
    uses, slot_of = {}, {}
    for k, s in enumerate(syls):
        for slot, key in enumerate(syl_pieces[s]):
            if key is None:
                continue
            if slot_of.setdefault(key, slot) != slot:
                raise FitError("a piece is used in two slots")
            uses[key] = uses.get(key, 0) | (1 << k)
    rcol = {}
    for k, s in enumerate(syls):
        bit = 1 << k
        for x in _bits(targets[s]):
            rcol[x] = rcol.get(x, 0) | bit
    slot_keys = [sorted((key for key in uses if slot_of[key] == sl), key=repr) for sl in range(3)]
    full = full_mask(w, h)
    P = {key: 0 for key in uses}

    def ink_of(slot):
        ink = {}
        for key in slot_keys[slot]:
            u = uses[key]
            for x in _bits(P[key]):
                ink[x] = ink.get(x, 0) | u
        return ink

    def error():
        i0, i1, i2 = ink_of(0), ink_of(1), ink_of(2)
        err = 0
        for x in set(rcol) | set(i0) | set(i1) | set(i2):
            err += (rcol.get(x, 0) ^ (i0.get(x, 0) | i1.get(x, 0) | i2.get(x, 0))).bit_count()
        return err

    hist = []
    for it in range(passes):
        # Free pieces start from nothing, and a plain majority in the first pass lets an initial piece swallow
        # whatever most vowels draw (the medial pieces are still empty); the swallowed pixels then hide the medials'
        # own ink from them and the error never falls. So the first passes ask for ink in every user of a pixel.
        strict = it < STRICT_PASSES and (masks is None if strict_first is None else strict_first)
        for slot in (0, 1, 2):
            o1, o2 = [j for j in range(3) if j != slot]
            a, b = ink_of(o1), ink_of(o2)
            for key in slot_keys[slot]:
                u = uses[key]
                allowed = full if masks is None or key not in masks else masks[key] & full
                new = 0
                for x in _bits(allowed):
                    free = u & ~(a.get(x, 0) | b.get(x, 0))
                    if free:
                        on = (rcol.get(x, 0) & free).bit_count()
                        tot = free.bit_count()
                        if on == tot if strict else on * 2 > tot:
                            new |= 1 << x
                P[key] = new
        err = error()
        hist.append(err)
        if it >= STRICT_PASSES and len(hist) > 1 and hist[-1] >= hist[-2]:
            break
    return P, {"passes": len(hist), "hamming_history": hist, "users": {key: uses[key].bit_count() for key in uses}}


def mean_iou(P, syl_pieces, targets):
    """Mean IoU of the OR of each syllable's pieces (@p P: {key: gint}) against its target."""
    tot = 0.0
    for s, t in targets.items():
        c = 0
        for key in syl_pieces[s]:
            if key is not None:
                c |= P[key]
        tot += iou(t, c)
    return tot / len(targets) if targets else 0.0


# ---- outline fonts ------------------------------------------------------------------------------------------------

_REV = bytes(int(format(b, "08b")[::-1], 2) for b in range(256))


class OutlineFont:
    """A FreeType face rasterised 1 bpp, unhinted, at a (possibly fractional) pixel size."""

    def __init__(self, path, px, pylib=None):
        self.ft, _ = load_libs(pylib)
        try:
            self.face = self.ft.Face(path)
        except Exception as e:
            raise FitError(f"{path}: cannot open as a font ({e})")
        self.path, self.px = path, px
        self.face.set_char_size(int(round(px * 64)))
        self.flags = self.ft.FT_LOAD_RENDER | self.ft.FT_LOAD_TARGET_MONO | self.ft.FT_LOAD_NO_HINTING
        self.upem = self.face.units_per_EM

    def gid(self, cp):
        return self.face.get_char_index(cp)

    def raw(self, gid, dx=0.0, dy=0.0):
        """(rows, width, left, top): rows are ints with pixel 0 in bit 0; left/top are FreeType's bitmap_left/top."""
        ft = self.ft
        self.face.set_transform(ft.Matrix(0x10000, 0, 0, 0x10000), ft.Vector(int(round(dx * 64)), int(round(dy * 64))))
        self.face.load_glyph(gid, self.flags)
        g = self.face.glyph
        b = g.bitmap
        rows, width, pitch = b.rows, b.width, b.pitch
        if not rows or not width:
            return [], 0, g.bitmap_left, g.bitmap_top
        buf = bytes(b.buffer)
        mask = (1 << width) - 1
        out = [int.from_bytes(bytes(_REV[v] for v in buf[r * pitch:(r + 1) * pitch]), "little") & mask
               for r in range(rows)]
        return out, width, g.bitmap_left, g.bitmap_top

    def advance(self, gid):
        self.face.set_transform(self.ft.Matrix(0x10000, 0, 0, 0x10000), self.ft.Vector(0, 0))
        self.face.load_glyph(gid, self.ft.FT_LOAD_NO_HINTING)
        return self.face.glyph.advance.x / 64.0


def place(raw, ox, oy, w, h):
    """The raw bitmap with its pen origin at column @p ox, baseline row @p oy, clipped to the w x h cell, as a gint."""
    rows, width, left, top = raw
    W = w + 1
    rowmask = (1 << w) - 1
    g = 0
    for r, v in enumerate(rows):
        y = oy - top + r
        if not 0 <= y < h:
            continue
        sh = ox + left
        v = (v << sh) if sh >= 0 else (v >> -sh)
        g |= (v & rowmask) << (y * W)
    return g


def raw_box(raw):
    """(left, top, right, bottom) in pen-relative pixels of the ink of a raw bitmap, or None."""
    rows, width, left, top = raw
    if not rows:
        return None
    cols = 0
    ys = [r for r, v in enumerate(rows) if v]
    if not ys:
        return None
    for v in rows:
        cols |= v
    return (left + (cols & -cols).bit_length() - 1, top - ys[0], left + cols.bit_length() - 1, top - ys[-1])


class Shaper:
    """HarfBuzz shaping of conjoining jamo with the font's precomposed syllables hidden, so the font's own
    ljmo/vjmo/tjmo variants are used (what a copy of the font with U+AC00..D7A3 removed from its cmap would give)."""

    def __init__(self, path, pylib=None):
        ft, hb = load_libs(pylib)
        self.hb = hb
        self.ft = ft.Face(path)
        self.font = hb.Font(hb.Face(hb.Blob.from_file_path(path), 0))
        ff = hb.FontFuncs()
        face = self.ft

        def nominal(font, cp, data):
            return 0 if 0xAC00 <= cp <= 0xD7A3 else face.get_char_index(cp)

        def hadvance(font, gid, data):
            face.load_glyph(gid, ft.FT_LOAD_NO_SCALE)
            return face.glyph.advance.x

        ff.set_nominal_glyph_func(nominal)
        ff.set_glyph_h_advance_func(hadvance)
        self._ff = ff
        self.font.funcs = ff
        self.upem = self.ft.units_per_EM

    def shape(self, L, V, T):
        """[(glyph id, x, y)] in font units: pen position plus offset, for the syllable's jamo sequence."""
        s = chr(0x1100 + L) + chr(0x1161 + V) + (chr(0x11A7 + T) if T else "")
        b = self.hb.Buffer()
        b.add_str(s)
        b.guess_segment_properties()
        self.hb.shape(self.font, b, {})
        pen, out = 0, []
        for i, p in zip(b.glyph_infos, b.glyph_positions):
            out.append((i.codepoint, pen + p.x_offset, p.y_offset))
            pen += p.x_advance
        return out

    def syllable_box(self, cp):
        """Control box (xmin, ymin, xmax, ymax) in font units of the font's own precomposed glyph, or None."""
        gid = self.ft.get_char_index(cp)
        if not gid:
            return None
        self.ft.load_glyph(gid, self.ft_flag())
        bb = self.ft.glyph.outline.get_bbox()
        return (bb.xMin, bb.yMin, bb.xMax, bb.yMax) if bb.xMax > bb.xMin else None

    def ft_flag(self):
        import freetype
        return freetype.FT_LOAD_NO_SCALE | freetype.FT_LOAD_NO_BITMAP


def mask_frame(shaper, targets, w, h):
    """Pixel size and pen origin that put the mask font's syllable ink box on the targets' ink box.

    Returns (px, ox, oy). The scale is the larger one that still fits both dimensions; the boxes are centred.
    """
    tb = union_bbox(targets.values(), w, h)
    if tb is None:
        raise FitError("no present syllable has ink")
    boxes = [b for b in (shaper.syllable_box(0xAC00 + s) for s in targets) if b]
    if not boxes:
        raise FitError("the mask font has none of the syllables")
    xmin, ymin = min(b[0] for b in boxes), min(b[1] for b in boxes)
    xmax, ymax = max(b[2] for b in boxes), max(b[3] for b in boxes)
    tw, th = tb[2] - tb[0] + 1, tb[3] - tb[1] + 1
    sc = min(tw / (xmax - xmin), th / (ymax - ymin))
    cx, cy = tb[0] + tw / 2.0, tb[1] + th / 2.0
    ox = int(round(cx - (xmin + xmax) / 2.0 * sc))
    oy = int(round(cy + (ymin + ymax) / 2.0 * sc))
    return sc * shaper.upem, ox, oy


def mask_pieces(mask_path, syl_keys, w, h, targets, pylib=None):
    """Rasterise the mask font's variant for each piece key, in the frame that fits @p targets.

    @p syl_keys: {syllable: (initial key, medial key, final key or None)} for ALL syllables that should vote.
    Returns ({key: gint}, info). A piece is the (glyph, offset) most of its syllables are shaped to.
    """
    shaper = Shaper(mask_path, pylib)
    px, ox, oy = mask_frame(shaper, targets, w, h)
    rast = OutlineFont(mask_path, px, pylib)
    sc = px / shaper.upem
    votes = {}
    for s, keys in syl_keys.items():
        L, V, T = s // 588, (s // 28) % 21, s % 28
        shaped = shaper.shape(L, V, T)
        if len(shaped) != (3 if T else 2):
            continue
        for key, glyph in zip(keys, shaped):
            if key is not None:
                v = votes.setdefault(key, {})
                v[glyph] = v.get(glyph, 0) + 1
    out = {}
    for key, v in votes.items():
        gid, gx, gy = max(sorted(v), key=lambda g: v[g])
        out[key] = place(rast.raw(gid, gx * sc, gy * sc), ox, oy, w, h)
    return out, {"mask_font": os.path.basename(mask_path), "mask_px": round(px, 2), "mask_origin": [ox, oy]}
