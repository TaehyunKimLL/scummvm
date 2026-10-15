#!/usr/bin/env python3
"""Anti-aliased (2 bpp) jamo sets from an outline font (jamoset.py --from-ttf FONT --bpp 2).

docs/superpowers/specs/2026-10-15-aa-jamo-sets-design.md has the design and the measured numbers. In short:

  1. Render. The font's outlines are read through a fontTools pen (glyf or CFF, composites decomposed), flattened,
     and rasterised here with exact-area coverage (the signed-area accumulation of font-rs / stb_truetype: a pixel's
     coverage is min(1, |sum of the signed areas of the edges left of it|), the non-zero rule up to pixels where two
     contours' edges cross). The accumulation is linear, so a contour is rasterised once and any subset of a
     syllable's contours is the sum of their buffers. The frame (cell, baseline, ink box centred) is that of
     jamoset.ttf_targets, measured on the outlines.
  2. Regions. The 1 bpp free fit of jamofit.fit_pieces, run on the syllables' coverage thresholded at 0.5, gives for
     every (set, jamo) piece the pixels it owns.
  3. Contours to jamo. A syllable's contours are grouped (a contour whose ink lies inside another one's is its hole
     or a part of it and goes with it), and each group goes to the initial, medial or final whose fitted region it
     inks most. A group whose ink falls clearly in two pieces' own pixels and that no one region (dilated a pixel)
     holds is a merged outline: it is split pixel by pixel by the regions instead. (The fit may give a whole column
     of one jamo to a neighbour's piece when every user inks it; the containment test keeps such a contour whole.) No font units or donor syllables are named anywhere: the regions come from the fit.
  4. Consensus. A piece is the per-pixel median of the AA rasters of its slot over every syllable that uses it,
     quantised to 4 levels as mkfont.py quantises its 2 bpp faces (round(v * 3 / 255), no gamma).
  5. Refinement (--aa-refine N passes, default 2). With the other two slots fixed, each piece pixel takes the level
     that minimises the summed absolute level error of the max-composed syllables against their direct renders (the
     exact per-pixel optimum, as the 1 bpp fit does with the Hamming error), only near the piece's own ink.

The engine composes a syllable by the per-pixel maximum (graphics/hires_text/glyph_source_jamo.cpp maxRow), so the
loss where two jamo share an edge pixel (coverage adds up, the maximum does not) is a floor of any piece set:
join_loss() measures it from each syllable's own contour rasters.

Only fontTools is needed (no FreeType, no numpy). It is looked up like jamofit's libraries: --pylib or
$JAMOSET_PYLIB may name the directory that holds it.
"""

import math
import os
import sys

STRADDLE = 0.2		# a group with this share of its ink in a second piece's own pixels is a merged outline ...
CONTAINED = 0.85	# ... unless one piece's region (dilated 1 px) holds this share of it: the fit only moved pixels


def load_fonttools(pylib=None):
    for d in (pylib, os.environ.get("JAMOSET_PYLIB")):
        for part in (d or "").split(os.pathsep):
            if part and part not in sys.path:
                sys.path.append(part)
    try:
        import fontTools.ttLib  # noqa: F401
        from fontTools.pens.basePen import BasePen  # noqa: F401
    except ImportError:
        import jamofit
        raise jamofit.FitError("--bpp 2 and --binarize need fontTools (pip install fonttools); to use a directory "
                               "that already holds it, set JAMOSET_PYLIB=DIR (or pass --pylib DIR)")
    import fontTools.ttLib
    return fontTools


# ---- levels -------------------------------------------------------------------------------------------------------

def quantize2(v):
    """0-255 coverage to the 2 bpp level 0-3, as tools/korean/mkfont.py does (the reader expands a level x 85)."""
    return (v * 3 + 127) // 255


def level(c):
    """Coverage 0..1 to a level."""
    return quantize2(max(0, min(255, int(round(c * 255)))))


def pack_levels(levels, w, h, bpp=2):
    """Rows of levels packed as the SVFN stores them: MSB first, the leftmost pixel in the top bits, rows padded to a
    byte."""
    pitch = (w * bpp + 7) // 8
    out = bytearray(pitch * h)
    for y in range(h):
        for x in range(w):
            v = levels[y * w + x]
            if v:
                bit = x * bpp
                out[y * pitch + bit // 8] |= v << (8 - bpp - bit % 8)
    return bytes(out)


def unpack_levels(data, w, h, bpp):
    pitch = (w * bpp + 7) // 8
    mask = (1 << bpp) - 1
    out = []
    for y in range(h):
        for x in range(w):
            bit = x * bpp
            out.append((data[y * pitch + bit // 8] >> (8 - bpp - bit % 8)) & mask)
    return out


def ink_columns(levels, w, h):
    """(ink x, ink width) of a level raster, (0, 0) if blank."""
    cols = [x for x in range(w) if any(levels[y * w + x] for y in range(h))]
    return (cols[0], cols[-1] - cols[0] + 1) if cols else (0, 0)


# ---- rasterising ------------------------------------------------------------------------------------------------------

def rasterize(poly, ox, oy, w, h):
    """Signed coverage buffer (w * h floats, row major) of one closed polyline given in pen-relative pixels with y up,
    the pen at column @p ox and the baseline on row @p oy's bottom edge... precisely: point (x, y) is at cell position
    (ox + x, oy - y), pixel (X, Y) spans [X, X + 1) x [Y, Y + 1). Clipped to the cell; sum buffers of several contours
    and pass the sum to coverage()."""
    pts = [(ox + x, oy - y) for x, y in poly]
    if len(pts) < 2:
        return [0.0] * (w * h)
    xs = [p[0] for p in pts]
    x0 = min(0, int(math.floor(min(xs))))
    W = max(w, int(math.ceil(max(xs)))) - x0 + 2
    a = [0.0] * (W * h + 2)
    n = len(pts)
    for i in range(n):
        px0, py0 = pts[i]
        px1, py1 = pts[(i + 1) % n]
        if py0 == py1:
            continue
        if py0 < py1:
            d = 1.0
        else:
            d = -1.0
            px0, py0, px1, py1 = px1, py1, px0, py0
        if py1 <= 0 or py0 >= h:
            continue
        px0 -= x0
        px1 -= x0
        dxdy = (px1 - px0) / (py1 - py0)
        x = px0
        if py0 < 0:
            x -= py0 * dxdy
        ystart = max(0, int(math.floor(py0)))
        yend = min(h, int(math.ceil(py1)))
        for y in range(ystart, yend):
            dy = min(y + 1.0, py1) - max(float(y), py0)
            xnext = x + dxdy * dy
            dd = dy * d
            lo, hi = (x, xnext) if x < xnext else (xnext, x)
            lof = math.floor(lo)
            x0i = int(lof)
            hic = math.ceil(hi)
            x1i = int(hic)
            base = y * W
            if x1i <= x0i + 1:
                xmf = 0.5 * (x + xnext) - lof
                a[base + x0i] += dd - dd * xmf
                a[base + x0i + 1] += dd * xmf
            else:
                s = 1.0 / (hi - lo)
                x0f = lo - lof
                a0 = 0.5 * s * (1.0 - x0f) * (1.0 - x0f)
                x1f = hi - hic + 1.0
                am = 0.5 * s * x1f * x1f
                a[base + x0i] += dd * a0
                if x1i == x0i + 2:
                    a[base + x0i + 1] += dd * (1.0 - a0 - am)
                else:
                    a1 = s * (1.5 - x0f)
                    a[base + x0i + 1] += dd * (a1 - a0)
                    for xi in range(x0i + 2, x1i - 1):
                        a[base + xi] += dd * s
                    a2 = a1 + (x1i - x0i - 3) * s
                    a[base + x1i - 1] += dd * (1.0 - a2 - am)
                a[base + x1i] += dd * am
            x = xnext
    out = [0.0] * (w * h)
    c0 = -x0
    for y in range(h):
        acc = 0.0
        base = y * W
        row = y * w
        for X in range(c0 + w):
            acc += a[base + X]
            if X >= c0:
                out[row + X - c0] = acc
    return out


def coverage(signed):
    """Coverage 0..1 of a (summed) signed buffer."""
    return [min(1.0, abs(v)) for v in signed]


def add(bufs, n):
    out = [0.0] * n
    for b in bufs:
        for i, v in enumerate(b):
            if v:
                out[i] += v
    return out


class OutlineFace:
    """An outline font read with fontTools at @p size px (1 em = size px, as FreeType's set_char_size at 72 dpi)."""

    def __init__(self, path, size, pylib=None, font_number=0):
        ft = load_fonttools(pylib)
        try:
            self.font = ft.ttLib.TTFont(path, fontNumber=font_number, lazy=True)
            self.cmap = self.font.getBestCmap() or {}
            self.glyphs = self.font.getGlyphSet()
        except Exception as e:
            import jamofit
            raise jamofit.FitError(f"{path}: cannot open as a font ({e})")
        self.upem = self.font["head"].unitsPerEm
        self.size = size
        self.scale = size / float(self.upem)
        self._hmtx = self.font["hmtx"]
        self._cache = {}

    def has(self, cp):
        return bool(self.contours(cp))

    def contours(self, cp):
        """The glyph's closed contours as polylines in pen-relative pixels, y up (curves flattened)."""
        if cp in self._cache:
            return self._cache[cp]
        name = self.cmap.get(cp)
        out = []
        if name is not None and name in self.glyphs:
            pen = _FlatPen(self.glyphs, self.scale)
            self.glyphs[name].draw(pen)
            out = [c for c in pen.contours if len(c) >= 3]
        if len(self._cache) > 64:
            self._cache.clear()
        self._cache[cp] = out
        return out

    def advance(self, cp):
        name = self.cmap.get(cp)
        if name is None:
            return 0.0
        return self._hmtx[name][0] * self.scale


def _flat_pen_class():
    from fontTools.pens.basePen import BasePen

    class FlatPen(BasePen):
        def __init__(self, glyph_set, scale):
            BasePen.__init__(self, glyph_set)
            self.sc = scale
            self.contours = []
            self.cur = None

        def _moveTo(self, p):
            self._flush()
            self.cur = [p]

        def _lineTo(self, p):
            self.cur.append(p)

        def _steps(self, *pts):
            ln = sum(math.hypot(pts[i + 1][0] - pts[i][0], pts[i + 1][1] - pts[i][1]) for i in range(len(pts) - 1))
            return max(2, min(24, int(math.ceil(ln * self.sc))))

        def _qCurveToOne(self, p1, p2):
            p0 = self._getCurrentPoint()
            n = self._steps(p0, p1, p2)
            for k in range(1, n + 1):
                t = k / n
                u = 1 - t
                self.cur.append((u * u * p0[0] + 2 * u * t * p1[0] + t * t * p2[0],
                                 u * u * p0[1] + 2 * u * t * p1[1] + t * t * p2[1]))

        def _curveToOne(self, p1, p2, p3):
            p0 = self._getCurrentPoint()
            n = self._steps(p0, p1, p2, p3)
            for k in range(1, n + 1):
                t = k / n
                u = 1 - t
                self.cur.append((u ** 3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t ** 3 * p3[0],
                                 u ** 3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t ** 3 * p3[1]))

        def _closePath(self):
            self._flush()

        def _endPath(self):
            self._flush()

        def _flush(self):
            if self.cur:
                c = [(x * self.sc, y * self.sc) for x, y in self.cur]
                if len(c) > 1 and c[0] == c[-1]:
                    c.pop()
                self.contours.append(c)
            self.cur = None

    return FlatPen


_FLAT = None


def _FlatPen(glyph_set, scale):
    global _FLAT
    if _FLAT is None:
        _FLAT = _flat_pen_class()
    return _FLAT(glyph_set, scale)


def glyph_box(contours):
    xs = [x for c in contours for x, _ in c]
    ys = [y for c in contours for _, y in c]
    return (min(xs), min(ys), max(xs), max(ys)) if xs else None


EPS = 1e-6


def frame(face, cps, w, h, baseline):
    """(ox, oy, info): the pen column and baseline row that centre the union ink box of @p cps in the w x h cell (the
    baseline may be given instead), as jamoset.ttf_targets does with FreeType's bitmaps."""
    boxes = [b for b in (glyph_box(face.contours(cp)) for cp in cps) if b]
    if not boxes:
        raise ValueError("draws none of the Hangul syllables")
    left = int(math.floor(min(b[0] for b in boxes) + EPS))
    right = int(math.ceil(max(b[2] for b in boxes) - EPS)) - 1
    top = int(math.ceil(max(b[3] for b in boxes) - EPS))
    bottom = int(math.floor(min(b[1] for b in boxes) + EPS))
    iw, ih = right - left + 1, top - bottom
    ox = -left + (w - iw) // 2
    oy = baseline if baseline is not None else top + (h - ih) // 2
    clipped = iw > w or ih > h or oy - top < 0 or oy - bottom > h
    return ox, oy, {"origin_x": ox, "baseline": oy, "ink_box": [iw, ih], "clipped": bool(clipped)}


def render_coverage(face, cp, ox, oy, w, h):
    n = w * h
    return coverage(add([rasterize(c, ox, oy, w, h) for c in face.contours(cp)], n))


def direct_levels(face, cp, ox, oy, w, h):
    """The syllable rendered whole and quantised: what a precomposed 2 bpp face would hold."""
    return [level(c) for c in render_coverage(face, cp, ox, oy, w, h)]


# ---- binarising (1 bpp targets from coverage) --------------------------------------------------------------------------

def binarize(gray, w, h, mode, threshold=0.5, ridge_min=0.25, ridge_pair=0.48):
    """A gint of the coverage @p gray: 'threshold' keeps coverage >= @p threshold; 'ridge' also keeps a pixel that is a
    local maximum across a stroke (vertically or horizontally, against both neighbours) with coverage >= @p ridge_min
    and, with its stronger neighbour, >= @p ridge_pair, so a hairline that falls between two pixel columns stays one
    pixel wide instead of vanishing (the rule of the Paperlogy pack generator, runs/paperlogy/report.md 4.3; its
    valley erosion is left out)."""
    W = w + 1
    g = 0
    at = lambda x, y: gray[y * w + x] if 0 <= x < w and 0 <= y < h else 0.0
    for y in range(h):
        for x in range(w):
            v = gray[y * w + x]
            on = v >= threshold
            if not on and mode == "ridge" and v >= ridge_min:
                up, dn, lf, rt = at(x, y - 1), at(x, y + 1), at(x - 1, y), at(x + 1, y)
                on = ((v >= up and v >= dn and v + max(up, dn) >= ridge_pair) or
                      (v >= lf and v >= rt and v + max(lf, rt) >= ridge_pair))
            if on:
                g |= 1 << (y * W + x)
    return g


def outline_targets(path, size, cell, baseline, mode, threshold, pylib=None):
    """jamoset.ttf_targets' result, from this rasteriser: the drawn syllables binarised by @p mode ('threshold' or
    'ridge'), and ASCII the same way. Returns (targets, w, h, ascent, info, ascii_glyphs)."""
    import jamofit as jf
    import jamoset
    face = OutlineFace(path, size, pylib)
    w, h = cell or (size, size)
    cps = [0xAC00 + s for s in range(11172) if face.has(0xAC00 + s)]
    ox, oy, info = frame(face, cps, w, h, baseline)
    targets = {}
    for cp in cps:
        g = binarize(render_coverage(face, cp, ox, oy, w, h), w, h, mode, threshold)
        if g:
            targets[cp - 0xAC00] = g
    info.update(font=os.path.basename(path), size=size, cell=[w, h], drawn=len(cps), fitted=len(targets),
                binarize=mode, threshold=threshold)
    ascii_glyphs = {}
    for cp in range(0x20, 0x7F):
        g = binarize(render_coverage(face, cp, ox, oy, w, h), w, h, mode, threshold) if face.has(cp) else 0
        gl = jamoset.Glyph(w, h, jf.to_rows(g, w, h))
        ix, iw = gl.ink()
        adv = int(round(face.advance(cp))) if cp in face.cmap else w // 2
        ascii_glyphs[cp] = (gl.to_bytes(), min(255, adv), ix, iw)
    return targets, w, h, oy, info, ascii_glyphs


# ---- contours to jamo -------------------------------------------------------------------------------------------------

def group_contours(signed):
    """Groups (lists of indices) of a syllable's contour buffers: a contour whose ink lies (90 %) inside a larger
    one's goes with the smallest such one (a hole, or a stroke drawn over another)."""
    cov = [coverage(s) for s in signed]
    area = [sum(c) for c in cov]
    parent = list(range(len(signed)))
    for b in range(len(signed)):
        if area[b] <= 0:
            continue
        best = None
        for a in range(len(signed)):
            if a == b or area[a] <= area[b]:
                continue
            inside = sum(min(p, q) for p, q in zip(cov[a], cov[b]))
            if inside >= 0.9 * area[b] and (best is None or area[a] < area[best]):
                best = a
        if best is not None:
            parent[b] = best

    def root(i):
        seen = set()
        while parent[i] != i and i not in seen:
            seen.add(i)
            i = parent[i]
        return i
    groups = {}
    for i in range(len(signed)):
        groups.setdefault(root(i), []).append(i)
    return list(groups.values())


def _pixels(g, w, h):
    W = w + 1
    out = set()
    for y in range(h):
        r = (g >> (y * W)) & ((1 << w) - 1)
        x = 0
        while r:
            if r & 1:
                out.add(y * w + x)
            r >>= 1
            x += 1
    return out


PEER_SURE = 0.15	# a group (not a merged outline) whose presence one slot's jamo explains this much ...
PEER_MARGIN = 0.05	# ... and this much more than any other slot's jamo is that slot's
PEER_MIN = 2		# "same" peers needed in a set before the wider pool is used
PEER_PIXEL = 0.05	# a pixel of a group split by its peers: its slot leads by this much, else the regions decide


class Peers:
    """Which slot a contour group belongs to, judged by the other syllables. For a slot, take the syllables that use
    the same set of that slot (the jamo drawn in the same place): those with the same jamo there (and other jamo in
    the other slots: "same"), and those with another jamo there ("other"). A group the slot draws is in the first and
    much less in the second; a group another slot draws does not care which jamo this slot has, so it is in both
    alike. The score is the difference of the presence. This decides what a 1 bpp region fit cannot: the free fit
    hands a stroke every user of a piece shares (the bar of ㅗ, ㅛ, ㅜ, ㅠ, ㅡ under one initial set) to the initial,
    slot order first, and the same bar is in every syllable of that initial set and of the matching final set."""

    def __init__(self, targets, keys, roles, w, h, sample=24):
        import jamofit as jf
        full = jf.full_mask(w, h)
        self.w, self.h, self.sample = w, h, sample
        W = w + 1
        # each peer's ink at the 9 one-pixel shifts: a stroke may sit a pixel off between syllables
        self.dil = {}
        for s, g in targets.items():
            sh = []
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    d = dy * W + dx
                    v = (g << d) if d >= 0 else (g >> -d)
                    sh.append(v & full)
            self.dil[s] = sh
        self.keys, self.roles = keys, roles
        self.by_set, self.by_slot = {}, {}
        for s in sorted(targets):
            for slot, k in enumerate(keys[s]):
                if k is not None and k in roles:
                    self.by_set.setdefault((slot, roles[k][1]), []).append(s)
                    self.by_slot.setdefault((slot, s % 28 > 0), []).append(s)
        self._cache = {}

    @staticmethod
    def jamo(s, slot):
        return (s // 588, (s // 28) % 21, s % 28)[slot]

    def _sample(self, cand):
        step = max(1, len(cand) // self.sample)
        return cand[::step][:self.sample]

    def peers(self, s, slot):
        """(same, other): syllables of the same set of @p slot with the same jamo there and other jamo elsewhere, and
        with another jamo there."""
        key = (s, slot)
        if key in self._cache:
            return self._cache[key]
        k = self.keys[s][slot]
        mine = self.jamo(s, slot)
        others = [j for j in range(3) if j != slot and not (j == 2 and s % 28 == 0)]
        out = ([], [])
        # the slot's set (the jamo in the same place); if it has no such peers (a medial set only the initials ㄱ ㅋ
        # use, say), every syllable with a final or every one without, as @p s
        for pool in (self.by_set.get((slot, self.roles[k][1]), ()) if k in self.roles else (),
                     self.by_slot.get((slot, s % 28 > 0), ())):
            same = [p for p in pool if p != s and self.jamo(p, slot) == mine
                    and all(self.jamo(p, j) != self.jamo(s, j) for j in others)]
            other = [p for p in pool if self.jamo(p, slot) != mine]
            # a set only a few syllables share (the medial set of the initials ㄱ ㅋ: 가 has only 카) is no evidence:
            # ㄱ is inside ㅋ, so ㄱ would look like the medial's
            if len(same) >= PEER_MIN and other:
                out = (self._sample(same), self._sample(other))
                break
        if len(self._cache) > 4096:
            self._cache.clear()
        self._cache[key] = out
        return out

    def presence(self, g, n, ps):
        # half at the best one-pixel shift, half in place (index 4 is the unshifted ink): a shift also lines up a
        # neighbour's stroke a pixel away, the in-place share tells those apart
        return sum(max((g & v).bit_count() for v in self.dil[p]) + (g & self.dil[p][4]).bit_count()
                   for p in ps) / (2 * n * len(ps))

    def pixel_scores(self, s, g):
        """{slot: {pixel bit: share of "same" peers inked there (within a pixel) - share of "other" peers}} for the
        ink of @p g: which jamo explains each pixel of a merged outline (ㄴ and ㅗ drawn as one contour)."""
        out = {}
        bits = []
        x = g
        while x:
            low = x & -x
            bits.append(low)
            x ^= low
        for slot in range(3):
            if self.keys[s][slot] is None:
                continue
            same, other = self.peers(s, slot)
            if not (same and other):
                continue
            def share(ps):
                acc = {b: 0 for b in bits}
                for p in ps:
                    u = 0
                    for v in self.dil[p]:
                        u |= v
                    for b in bits:
                        if u & b:
                            acc[b] += 1
                return {b: c / len(ps) for b, c in acc.items()}
            a, o = share(same), share(other)
            out[slot] = {b: a[b] - o[b] for b in bits}
        return out

    def scores(self, s, g):
        """{slot: presence of the group's ink @p g (gint) in the "same" peers minus in the "other" peers}."""
        n = g.bit_count()
        out = {}
        if not n:
            return out
        for slot in range(3):
            if self.keys[s][slot] is None:
                continue
            same, other = self.peers(s, slot)
            if same and other:
                out[slot] = self.presence(g, n, same) - self.presence(g, n, other)
        return out


def assign(groups_cov, cores, w, h, peer_scores=None, peer_pixels=None):
    """Give each contour group's coverage to a slot. @p groups_cov: coverage lists (0..1); @p cores: the syllable's
    fitted 1 bpp pieces as gints per slot (None where the syllable has no final). @p peer_scores: a function of a
    group's coverage giving {slot: score} (Peers.scores); a group that one slot's peers clearly explain better goes
    there whole (a merged outline does not: two slots' jamo explain parts of it). @p peer_pixels: a function of a group's coverage giving {slot:
    {pixel index: score}} (Peers.pixel_scores); a group they do not settle whole is split pixel by pixel by them where
    a slot leads by PEER_PIXEL, else by the regions.
    Returns ([coverage per slot], {"groups", "straddled", "orphans", "by_peers"})."""
    import jamofit as jf
    n = w * h
    full = jf.full_mask(w, h)
    slots = [k for k in range(3) if cores[k] is not None]
    core_px = {k: _pixels(cores[k], w, h) for k in slots}
    near = {k: [_pixels(jf.dilate(cores[k], w, full, r), w, h) for r in (1, 2, 3)] for k in slots}
    own = {k: core_px[k] - set().union(*[core_px[j] for j in slots if j != k]) for k in slots}
    out = [[0.0] * n for _ in range(3)]
    info = {"groups": 0, "straddled": 0, "orphans": 0, "by_peers": 0, "to": []}

    def score(cov, k):
        # how much of the group the piece's region (dilated a pixel) holds; its own pixels break ties
        return sum(cov[i] for i in near[k][0]) + 0.01 * sum(cov[i] for i in core_px[k])

    def split(cov, pix):
        """Pixel by pixel; returns {slot: share of the group's coverage it took}."""
        took = {}
        for i, v in enumerate(cov):
            if not v:
                continue
            owners = []
            if pix:
                sc = sorted(((d.get(i, -1.0), k) for k, d in pix.items() if k in slots), reverse=True)
                if sc and sc[0][0] >= PEER_PIXEL and (len(sc) == 1 or sc[0][0] - sc[1][0] >= PEER_PIXEL):
                    owners = [sc[0][1]]
            if not owners:
                owners = [k for k in slots if i in core_px[k]]
            for r in range(3):
                if owners:
                    break
                owners = [k for k in slots if i in near[k][r]]
            for k in owners or [max(slots, key=lambda k: sum(cov[j] for j in near[k][0]))]:
                out[k][i] = max(out[k][i], v)
                took[k] = took.get(k, 0.0) + v
        tot = sum(cov) or 1.0
        return {k: round(v / tot, 3) for k, v in took.items()}

    for cov in groups_cov:
        total = sum(cov)
        if total <= 0:
            info["to"].append(None)
            continue
        info["groups"] += 1
        if peer_scores is not None:
            ps = sorted(((v, k) for k, v in peer_scores(cov).items() if k in slots), reverse=True)
            if ps and ps[0][0] >= PEER_SURE and (len(ps) == 1 or ps[0][0] - ps[1][0] >= PEER_MARGIN):
                info["by_peers"] += 1
                k = ps[0][1]
                for i, v in enumerate(cov):
                    if v:
                        out[k][i] = min(1.0, out[k][i] + v)
                info["to"].append(k)
                continue
        excl = sorted(((sum(cov[i] for i in own[k]) / total, k) for k in slots), reverse=True)
        inside = max(sum(cov[i] for i in near[k][0]) for k in slots) / total
        if inside < CONTAINED and len(excl) > 1 and excl[1][0] >= STRADDLE and excl[1][0] * total >= 1.0:
            info["straddled"] += 1
            info["to"].append(split(cov, peer_pixels(cov) if peer_pixels else None))
            continue
        if peer_pixels is not None:
            pix = peer_pixels(cov)
            if pix:
                info["by_peer_pixels"] = info.get("by_peer_pixels", 0) + 1
                info["to"].append(split(cov, pix))
                continue
        scores = [(score(cov, k), excl_k, k) for excl_k, k in excl]
        best = max(scores)
        if best[0] <= 0:
            info["orphans"] += 1
            for r in (1, 2):
                sc = [(sum(cov[i] for i in near[k][r]), k) for k in slots]
                if max(sc)[0] > 0:
                    best = (0, 0, max(sc)[1])
                    break
        k = best[2]
        for i, v in enumerate(cov):
            if v:
                out[k][i] = min(1.0, out[k][i] + v)
        info["to"].append(k)
    return out, info


# ---- consensus and refinement -------------------------------------------------------------------------------------------

def median_consensus(stack):
    """Per-pixel median of equal-length byte rasters (the mean of the middle two for an even count), as ints."""
    n = len(stack)
    out = []
    for i in range(len(stack[0])):
        vals = sorted(r[i] for r in stack)
        out.append(vals[n // 2] if n % 2 else (vals[n // 2 - 1] + vals[n // 2] + 1) // 2)
    return out


def refine(pieces, syl_keys, targets, w, h, allowed, passes):
    """L1 coordinate descent on the pieces' levels (in place). @p pieces: {key: [level]}; @p syl_keys: {syllable:
    (keys per slot, None where absent)}; @p targets: {syllable: [level]}; @p allowed: {key: set of pixel indices}.
    Returns the summed level error after each pass."""
    n = w * h
    users = {}
    for s, keys in syl_keys.items():
        for slot, k in enumerate(keys):
            if k is not None:
                users.setdefault(k, []).append((s, slot))

    def total():
        err = 0
        for s, keys in syl_keys.items():
            ps = [pieces[k] for k in keys if k is not None]
            t = targets[s]
            for i in range(n):
                err += abs(max(p[i] for p in ps) - t[i])
        return err
    hist = [total()]
    for _ in range(passes):
        for slot in (0, 1, 2):
            for key in sorted((k for k in users if users[k][0][1] == slot), key=repr):
                us = users[key]
                others = []
                for s, sl in us:
                    ps = [pieces[k] for j, k in enumerate(syl_keys[s]) if k is not None and j != sl]
                    others.append(([max(p[i] for p in ps) for i in range(n)] if ps else [0] * n, targets[s]))
                cur = pieces[key]
                for i in allowed.get(key, ()):
                    best = None
                    for lv in (cur[i], 0, 1, 2, 3):
                        e = 0
                        for o, t in others:
                            e += abs((lv if lv > o[i] else o[i]) - t[i])
                        if best is None or e < best[0]:
                            best = (e, lv)
                    cur[i] = best[1]
        hist.append(total())
        if hist[-1] >= hist[-2]:
            break
    return hist



def robust_consensus(stack, k=3.0):
    """Per-pixel median after dropping outlier donors: a donor whose L1 distance to the plain median is above the
    median distance + @p k x 1.4826 x MAD (with at least 5 donors) does not vote. Returns (median, donors kept)."""
    m = median_consensus(stack)
    if len(stack) < 5:
        return m, len(stack)
    d = [sum(abs(a - b) for a, b in zip(r, m)) for r in stack]
    sd = sorted(d)
    med = sd[len(sd) // 2]
    mad = sorted(abs(x - med) for x in d)[len(d) // 2]
    cut = med + k * 1.4826 * mad
    kept = [r for r, x in zip(stack, d) if x <= cut]
    if len(kept) == len(stack) or len(kept) < 3:
        return m, len(stack)
    return median_consensus(kept), len(kept)


def support(stack, cut):
    """Per-pixel share of the donors with coverage >= @p cut."""
    n = len(stack)
    return [sum(1 for r in stack if r[i] >= cut) / n for i in range(len(stack[0]))]


def components_of(g, w, full):
    """The 8-connected components of a gint, as gints."""
    import jamofit as jf
    out = []
    while g:
        seed = g & -g
        while True:
            nxt = jf.spread8(seed, w, full) & g
            if nxt == seed:
                break
            seed = nxt
        out.append(seed)
        g &= ~seed
    return out


def speck_size(w, h):
    """Components this small are specks: 1 px up to 16 px cells, 2 px above."""
    return 1 if max(w, h) <= 16 else 2


def remove_specks(P, syl_keys, targets, w, h, size):
    """1 bpp: drop each piece's components of at most @p size pixels whose removal does not raise the summed Hamming
    error of the composed syllables that use the piece (a speck the majority does not need). In place; returns the
    number removed."""
    import jamofit as jf
    full = jf.full_mask(w, h)
    users = {}
    for s, ks in syl_keys.items():
        if s in targets:
            for k in ks:
                if k is not None:
                    users.setdefault(k, []).append(s)
    removed = 0
    for key in sorted(P, key=repr):
        for comp in components_of(P[key], w, full):
            if comp.bit_count() > size:
                continue
            delta = 0
            for s in users.get(key, ()):
                others = 0
                for k in syl_keys[s]:
                    if k is not None and k != key:
                        others |= P[k]
                lost = comp & ~others
                delta += (lost & targets[s]).bit_count() - (lost & ~targets[s]).bit_count()
            if delta <= 0:
                P[key] &= ~comp
                removed += 1
    return removed


def aa_clean(levels, sup, w, h, size):
    """2 bpp: a faint (level 1) pixel stays only where half the donors have ink (@p sup: per-pixel share); then
    components of at most @p size pixels and no full-coverage pixel are dropped. Returns (levels, pixels changed)."""
    out = [0 if (v == 1 and sup[i] < 0.5) else v for i, v in enumerate(levels)]
    W = w + 1
    g = 0
    for i, v in enumerate(out):
        if v:
            g |= 1 << ((i // w) * W + i % w)
    import jamofit as jf
    for comp in components_of(g, w, jf.full_mask(w, h)):
        px = _pixels(comp, w, h)
        if len(px) <= size and max(out[i] for i in px) < 3:
            for i in px:
                out[i] = 0
    return out, sum(1 for a, b in zip(levels, out) if a != b)


def donor_masks(keys, donors, w, h, cut, share=0.25, radius=1, syllables=None):
    """{piece: gint}: the pixels where at least @p share of the piece's donors have coverage >= @p cut, dilated by
    @p radius: where the piece's jamo is, by its own contours. The fit and the refinement may only ink there."""
    import jamofit as jf
    full = jf.full_mask(w, h)
    stacks = stacks_of(keys, donors, syllables)
    out = {}
    W = w + 1
    for k, st in stacks.items():
        sup = support(st, cut)
        g = 0
        for i, v in enumerate(sup):
            if v >= share:
                g |= 1 << ((i // w) * W + i % w)
        out[k] = jf.dilate(g, w, full, radius)
    return out


def stacks_of(keys, donors, syllables=None):
    """{piece: [donor rasters]} over @p syllables (default: every syllable with donors)."""
    stacks = {}
    for s in sorted(donors if syllables is None else syllables):
        if s not in donors:
            continue
        for slot, k in enumerate(keys[s]):
            if k is not None and donors[s][slot] is not None:
                stacks.setdefault(k, []).append(donors[s][slot])
    return stacks


def contour_donors(face, syllables, keys, P, ox, oy, w, h, targets=None, roles=None, record=None):
    """{syllable: [coverage bytes 0..255 per slot, None where the syllable has no final]}: the syllable's contours
    given to its initial, medial and final - by the other syllables sharing a jamo (Peers, over @p targets) where
    they decide, else by the fitted 1 bpp regions @p P of the pieces @p keys name. With @p record (a dict), record
    [syllable] = [(contour indices of a group, its slot, or for a group split pixel by pixel {slot: share of its
    coverage})]. Returns (donors, info)."""
    n = w * h
    W = w + 1
    donors = {}
    info = {"groups": 0, "straddled": 0, "orphans": 0, "by_peers": 0, "syllables_straddled": 0}
    peers = Peers(targets, keys, roles, w, h) if targets and roles else None
    for s in syllables:
        signed = [rasterize(c, ox, oy, w, h) for c in face.contours(0xAC00 + s)]
        groups = group_contours(signed)
        gcov = [coverage(add([signed[i] for i in g], n)) for g in groups]
        cores = [P.get(k, 0) if k is not None else None for k in keys[s]]
        ps = pp = None
        if peers is not None:
            def ink(cov, cut):
                g = 0
                for i, v in enumerate(cov):
                    if v >= cut:
                        g |= 1 << ((i // w) * W + i % w)
                return g

            def ps(cov, s=s):
                return peers.scores(s, ink(cov, 0.5))

            def pp(cov, s=s):
                res = peers.pixel_scores(s, ink(cov, 1e-9))
                return {k: {(b.bit_length() - 1) // W * w + (b.bit_length() - 1) % W: v for b, v in d.items()}
                        for k, d in res.items()}
        slots, ai = assign(gcov, cores, w, h, ps, pp)
        if record is not None:
            record[s] = [(g, k) for g, k in zip(groups, ai["to"]) if k is not None]
        info["by_peer_pixels"] = info.get("by_peer_pixels", 0) + ai.get("by_peer_pixels", 0)
        for k2 in ("groups", "straddled", "orphans", "by_peers"):
            info[k2] += ai[k2]
        info["syllables_straddled"] += ai["straddled"] > 0
        donors[s] = [None if keys[s][j] is None else bytes(int(round(v * 255)) for v in slots[j]) for j in range(3)]
    return donors, info


def pixel_donors(targets, keys, P, w, h):
    """Donors for a bitmap source (no contours): each target pixel goes to the pieces of the syllable whose fitted
    region holds it, else to the nearest one (up to 3 px), else to the medial."""
    import jamofit as jf
    full = jf.full_mask(w, h)
    n = w * h
    donors = {}
    for s, t in targets.items():
        ks = keys[s]
        px = _pixels(t, w, h)
        near = {j: [_pixels(jf.dilate(P.get(k, 0), w, full, r), w, h) for r in range(4)]
                for j, k in enumerate(ks) if k is not None}
        out = {j: bytearray(n) for j in near}
        for i in px:
            for r in range(4):
                owners = [j for j in near if i in near[j][r]]
                if owners:
                    break
            for j in owners or [1]:
                out[j][i] = 255
        donors[s] = [bytes(out[j]) if j in out else None for j in range(3)]
    return donors

# ---- metrics ------------------------------------------------------------------------------------------------------------

def aa_quality(targets, composed, w, h):
    """@p targets, @p composed: {syllable: levels (bytes or list)}. Level error over all cell pixels and over the ink
    (pixels either has), the share of ink pixels exactly right, IoU of the level >= 2 masks (a 1 bpp threshold),
    syllables composed exactly, composed / target ink (summed levels), and stray ink: composed pixels of level >= 2
    more than a pixel away from any target ink (per syllable, and the syllables with any)."""
    import jamofit as jf
    n = w * h
    err = errink = npx_ink = exact_ink = ink_t = ink_c = exact = 0
    ious, maes = [], []
    full = jf.full_mask(w, h)
    W = w + 1
    stray = stray_syl = 0
    for s in sorted(targets):
        t, c = targets[s], composed[s]
        tg = cg = 0
        for i in range(n):
            if t[i]:
                tg |= 1 << ((i // w) * W + i % w)
            if c[i] >= 2:
                cg |= 1 << ((i // w) * W + i % w)
        far = (cg & ~jf.spread8(tg, w, full)).bit_count()
        stray += far
        stray_syl += far > 0
        e_s = 0
        inter = union = 0
        for i in range(n):
            a, b = t[i], c[i]
            d = abs(a - b)
            e_s += d
            if a or b:
                npx_ink += 1
                errink += d
                exact_ink += d == 0
            ink_t += a
            ink_c += b
            ta, tb = a >= 2, b >= 2
            inter += ta and tb
            union += ta or tb
        err += e_s
        exact += e_s == 0
        maes.append(e_s / n)
        ious.append(1.0 if union == 0 else inter / union)
    m = len(targets)
    return {
        "present": m,
        "level_mae": round(err / (m * n), 4) if m else 0.0,
        "level_mae_ink": round(errink / npx_ink, 4) if npx_ink else 0.0,
        "exact_pixels_ink": round(exact_ink / npx_ink, 4) if npx_ink else 1.0,
        "exact_pixels": round(1 - (npx_ink - exact_ink) / (m * n), 4) if m else 1.0,
        "iou_t2_mean": round(sum(ious) / m, 4) if m else 0.0,
        "iou_t2_p5": round(jf.percentile(ious, 5), 4),
        "level_mae_p95": round(jf.percentile(maes, 95), 4),
        "exact": exact,
        "ink_ratio": round(ink_c / ink_t, 4) if ink_t else 1.0,
        "stray_px_mean": round(stray / m, 4) if m else 0.0,
        "stray_syllables": stray_syl,
    }


def collisions(pieces, bpp):
    """Pixels two or more of a syllable's pieces ink (1 bpp: any ink; 2 bpp: level >= 2 in both): where the jamo
    of the composed syllable run into each other."""
    cut = 1 if bpp == 1 else 2
    n = len(pieces[0]) if pieces else 0
    return sum(1 for i in range(n) if sum(1 for p in pieces if p[i] >= cut) >= 2)


CLEARANCE_TEST = "국궉귁개객게각"


def clearance(med, fin, direct, w, h, bpp):
    """(composed gap, the original has a gap) between the medial's lowest ink and the final's highest: the gap is the
    blank rows between them in the composed pieces; the original has one when the direct render has a blank row
    just above the final's top (any of the 2 rows above it)."""
    cut = 1 if bpp == 1 else 2
    rows = lambda lv: [y for y in range(h) if any(lv[y * w + x] >= cut for x in range(w))]
    mr, fr = rows(med), rows(fin)
    if not mr or not fr:
        return None, False
    ftop, mbot = fr[0], max(y for y in mr if y < fr[0]) if any(y < fr[0] for y in mr) else mr[-1]
    blank = lambda y: 0 <= y < h and not any(direct[y * w + x] >= cut for x in range(w))
    return ftop - mbot - 1, any(blank(y) for y in (ftop - 1, ftop - 2))


def join_loss(donors, direct, w, h):
    """What composing by the maximum costs by itself: each syllable's own per-slot contour rasters (@p donors:
    {syllable: [bytes 0..255 per slot, None where absent]}) composed by max and by a clamped sum, quantised, against the direct
    render's levels (@p direct). ink_lost_max: the share of the direct levels the maximum loses."""
    n = w * h
    e_max = e_sum = lost = ink = 0
    for s, ds in donors.items():
        t = direct[s]
        rs = [r for r in ds if r is not None]
        for i in range(n):
            vals = [r[i] for r in rs]
            mx = quantize2(max(vals))
            sm = quantize2(min(255, sum(vals)))
            e_max += abs(mx - t[i])
            e_sum += abs(sm - t[i])
            lost += max(0, t[i] - mx)
            ink += t[i]
    m = len(donors) * n
    return {"level_mae_max": round(e_max / m, 4) if m else 0.0, "level_mae_sum": round(e_sum / m, 4) if m else 0.0,
            "ink_lost_max": round(lost / ink, 4) if ink else 0.0}


# ---- the build -----------------------------------------------------------------------------------------------------------

def region_layout():
    """844 with one medial set per tail class for all initials: the layout of the region fit and of the peers. In 844
    the initials ㄱ ㅋ have their own medial sets; the free fit then gives ㄱ's stroke, which every user of such a set
    draws, to the vowel, and a peer vote among them (가 has only 카) cannot tell ㄱ from ㅋ's ㄱ."""
    import jamoset as J
    lay = J._844("844-regions", 0)
    lay.med_set = lambda lc, v, tc: 1 + 2 * tc
    return lay


def syllable_keys(layout):
    """({syllable: (initial, medial, final piece or None)} for all 11,172, tables, roles)."""
    import jamoset as J
    t, roles = J.piece_roles(layout)
    keys = {}
    for s in range(11172):
        ps = J.pieces_of(t, 0xAC00 + s)
        keys[s] = tuple(None if p == J.BLANK else p for p in ps) + (None,) * (3 - len(ps))
    return keys, t, roles


def fill_unseen(layout, pieces, users, roles, t):
    """Pieces no fitted syllable uses: the set it was split from (adaptive), else the same jamo's piece of the set
    with most users. In place; returns the counts."""
    import jamoset as J
    fallback = {"parent": 0, "sibling": 0, "blank": 0}
    for p in J.used_pieces(t):
        if p in pieces:
            continue
        slot, st, jm = roles.get(p, (None, 0, 0))
        anc = J._ancestor_piece(layout, roles, users, slot, st, jm)
        sib = [(users[q], -q) for q, (sl, s2, j2) in roles.items() if sl == slot and j2 == jm and q in users]
        if anc is not None:
            pieces[p] = pieces[anc]
            fallback["parent"] += 1
        elif sib:
            pieces[p] = pieces[-max(sib)[1]]
            fallback["sibling"] += 1
        else:
            fallback["blank"] += 1
    return fallback


CLEAN_DEFAULTS = {"robust": True, "masks": True, "share": 0.25, "radius": 1, "faint": True, "specks": True}


def build(layout, path, size, cell=None, baseline=None, passes=12, refine_passes=2, symbols=False, advance=None,
          pylib=None, log=None, keep=False, clean=True, adaptive=None, train=None, clean_opts=None):
    """The 2 bpp jamo-set face of the outline font @p path at @p size. @p layout: a jamoset Layout (ignored when
    @p adaptive gives the set limits (NI, NM, NF): the layout is then made by jamoadapt from the donors). @p clean:
    robust consensus, pieces confined to their donors' support, faint levels only where half the donors have ink,
    specks dropped. @p train: fit on these syllables only (the rest are still scored: a hold-out). Returns (font
    bytes, info); with @p keep, info["ctx"] holds what the metrics need."""
    import jamofit as jf
    import jamoset as J
    say = log or (lambda *a: None)
    co = dict(CLEAN_DEFAULTS, **(clean_opts or {})) if clean else {k: False for k in CLEAN_DEFAULTS}
    face = OutlineFace(path, size, pylib)
    w, h = cell or (size, size)
    n = w * h
    cps = [0xAC00 + s for s in range(11172) if face.has(0xAC00 + s)]
    if not cps:
        raise J.SourceError(f"{path}: draws none of the Hangul syllables")
    ox, oy, finfo = frame(face, cps, w, h, baseline)
    direct, targets = {}, {}
    for cp in cps:
        cov = render_coverage(face, cp, ox, oy, w, h)
        s = cp - 0xAC00
        direct[s] = bytes(level(c) for c in cov)
        g = binarize(cov, w, h, "threshold", 0.5)
        if g:
            targets[s] = g
    fit_syl = sorted(direct if train is None else (set(train) & set(direct)))
    say(f"rendered {len(cps)} syllables")
    keys844, t844, roles844 = syllable_keys(region_layout())
    tt = {s: targets[s] for s in fit_syl if s in targets}
    P844, fit_info = jf.fit_pieces({s: keys844[s] for s in tt}, tt, w, h, None, passes)
    donors, cinfo = contour_donors(face, sorted(direct), keys844, P844, ox, oy, w, h, tt, roles844)
    say(f"contours: {cinfo}")
    ainfo = None
    if adaptive:
        import jamoadapt
        layout, ainfo = jamoadapt.adapt({s: donors[s] for s in fit_syl}, 2, tuple(adaptive), log=say)
    keys, t, roles = syllable_keys(layout)
    stacks = stacks_of(keys, donors, fit_syl)
    pieces, sup, kept = {}, {}, 0
    for k, st in stacks.items():
        if co["robust"]:
            m, nk = robust_consensus(st)
            kept += len(st) - nk
        else:
            m = median_consensus(st)
        pieces[k] = [quantize2(v) for v in m]
        sup[k] = support(st, 43)
    users = {k: len(st) for k, st in stacks.items()}
    median_pieces = {k: list(v) for k, v in pieces.items()}
    hist = []
    full = jf.full_mask(w, h)
    W = w + 1
    if co["masks"]:
        masks = donor_masks(keys, donors, w, h, 43, co["share"], co["radius"], fit_syl)
        allowed = {k: sorted(_pixels(masks[k], w, h)) for k in pieces}
    else:
        # unconfined, as the free 1 bpp fit: a piece may take any pixel that lowers the composed error
        allowed = {k: list(range(n)) for k in pieces}
    if refine_passes:
        hist = refine(pieces, {s: keys[s] for s in fit_syl}, {s: list(direct[s]) for s in fit_syl}, w, h, allowed,
                      refine_passes)
        say(f"refined: level error {hist}")
    cleaned = 0
    if co["faint"] or co["specks"]:
        size_px = speck_size(w, h) if co["specks"] else 0
        for k in pieces:
            pieces[k], c = aa_clean(pieces[k], sup[k] if co["faint"] else [1.0] * n, w, h, size_px)
            cleaned += c
    fallback = fill_unseen(layout, pieces, users, roles, t)
    extra = {}
    for cp in range(0x20, 0x7F):
        if cp not in face.cmap:
            continue
        lv = [level(c) for c in render_coverage(face, cp, ox, oy, w, h)] if face.has(cp) else [0] * n
        ix, iw = ink_columns(lv, w, h)
        extra[cp] = (pack_levels(lv, w, h), min(255, int(round(face.advance(cp)))), ix, iw)
    if symbols:
        adv = extra[0x20][1] if 0x20 in extra else w // 2
        nb = (w + 7) // 8
        for cp, (gb, a, ix, iw) in J.symbol_glyphs(w, h, oy, adv).items():
            if cp not in extra:
                rows = [int.from_bytes(gb[y * nb:(y + 1) * nb], "big") >> (nb * 8 - w) for y in range(h)]
                lv = [3 if rows[y] >> (w - 1 - x) & 1 else 0 for y in range(h) for x in range(w)]
                extra[cp] = (pack_levels(lv, w, h), a, ix, iw)
    tables = J.tables_for(layout)
    if advance is None:
        # a composed syllable steps as the font's syllables do (their most common advance), when that is less than
        # the cell: a 22 px cell for a 21 px face would otherwise space the text a pixel or two wider than the font
        adv = {}
        for cp in cps:
            a = int(round(face.advance(cp)))
            adv[a] = adv.get(a, 0) + 1
        common = max(adv, key=lambda a: (adv[a], a))
        advance = common if 0 < common < w else None
    if advance is not None:
        tables.advance = advance
    J.blank_compat(tables, {cp for cp, g in extra.items() if any(g[0])})
    used = J.used_pieces(tables)
    mapping = {p: i for i, p in enumerate(used)}
    jset = J.pack(J.renumber(tables, mapping))
    glyphs = []
    for p in used:
        lv = pieces.get(p, [0] * n)
        ix, iw = ink_columns(lv, w, h)
        glyphs.append((J.PIECE_BASE + mapping[p], pack_levels(lv, w, h), w, ix, iw))
    for cp, (gb, a, ix, iw) in sorted(extra.items()):
        glyphs.append((cp, gb, a, ix, iw))
    glyphs.sort()
    out = J.write_svfn(w, h, oy, glyphs, jset, bpp=2)
    finfo.update(font=os.path.basename(path), size=size, cell=[w, h], drawn=len(cps))
    info = {"source": finfo, "pieces": len(mapping), "region_fit": {k: v for k, v in fit_info.items() if k != "users"},
            "contours": cinfo, "unseen_pieces": fallback, "refine_history": hist, "bpp": 2, "clean": bool(clean),
            "outlier_donors_dropped": kept, "cleaned_pixels": cleaned, "layout": layout.name,
            "sets": [layout.ni, layout.nm, layout.nf], "classes": [layout.lc, layout.tc],
            "syllable_advance": tables.advance}
    if ainfo:
        info["adaptive"] = {k: v for k, v in ainfo.items() if k != "history"}
    if keep:
        info["ctx"] = {"direct": direct, "donors": donors, "keys": keys, "median": median_pieces, "pieces": pieces,
                       "mapping": mapping, "w": w, "h": h, "roles": roles, "layout": layout, "fit_syl": fit_syl,
                       "ox": ox, "oy": oy}
    return out, info


def composed_levels(out, syllables):
    """{syllable: levels} composed by the engine rule from the written face."""
    import jamoset as J
    f = J.parse_svfn(out)
    t = J.unpack(f.jset)
    return {s: bytes(unpack_levels(J.compose(f, t, 0xAC00 + s), f.cell_w, f.cell_h, f.bpp)) for s in syllables}


JAMO_NAMES = ("ㄱㄲㄴㄷㄸㄹㅁㅂㅃㅅㅆㅇㅈㅉㅊㅋㅌㅍㅎ", "ㅏㅐㅑㅒㅓㅔㅕㅖㅗㅘㅙㅚㅛㅜㅝㅞㅟㅠㅡㅢㅣ",
              "ㄱㄲㄳㄴㄵㄶㄷㄹㄺㄻㄼㄽㄾㄿㅀㅁㅂㅄㅅㅆㅇㅈㅊㅋㅌㅍㅎ")


def piece_name(role):
    slot, st, jm = role
    return f"{('cho', 'jung', 'jong')[slot]} set{st + 1} {JAMO_NAMES[slot][jm]}"


def blame(ctx, composed, syllables, worst=40):
    """Which piece costs most: for each syllable and slot, the level error saved by putting the syllable's own contour
    raster in place of the piece. Returns {"worst": [(char, error, blamed piece, saving)], "pieces": [(piece, users,
    syllables it is the main cause for, summed saving)]}."""
    w, h = ctx["w"], ctx["h"]
    n = w * h
    pieces, keys, donors, direct, roles = ctx["pieces"], ctx["keys"], ctx["donors"], ctx["direct"], ctx["roles"]
    per_piece = {}
    rows = []
    for s in syllables:
        t = direct[s]
        cur = composed[s]
        e0 = sum(abs(cur[i] - t[i]) for i in range(n))
        gains = []
        for j, k in enumerate(keys[s]):
            if k is None or donors[s][j] is None:
                continue
            own = [quantize2(v) for v in donors[s][j]]
            others = [pieces[q] for j2, q in enumerate(keys[s]) if q is not None and j2 != j]
            e = 0
            for i in range(n):
                v = max([own[i]] + [o[i] for o in others])
                e += abs(v - t[i])
            gains.append((e0 - e, k))
        g, k = max(gains)
        d = per_piece.setdefault(k, [0, 0.0])
        d[0] += 1 if g > 0 else 0
        d[1] += max(0, g)
        rows.append((e0 / n, chr(0xAC00 + s), piece_name(roles[k]) if k in roles else str(k), g))
    rows.sort(reverse=True)
    users = {}
    for s in syllables:
        for k in keys[s]:
            if k is not None:
                users[k] = users.get(k, 0) + 1
    top = sorted(per_piece.items(), key=lambda kv: -kv[1][1])[:20]
    return {"worst": [(c, round(e, 4), p, g) for e, c, p, g in rows[:worst]],
            "pieces": [(piece_name(roles[k]) if k in roles else str(k), users.get(k, 0), v[0], round(v[1], 1))
                       for k, v in top]}
