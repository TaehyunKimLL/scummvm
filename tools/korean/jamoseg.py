#!/usr/bin/env python3
"""Vector jamo segmentation: a syllable's outline split into its initial, medial and final as outlines.

Everything here is geometry in font units; nothing is rasterised (jamovec.py rasterises the result per size).

  1. Groups. A syllable's contours are grouped by containment: a contour whose box lies inside a larger contour's
     box and whose first point lies inside that contour (even-odd) goes with the smallest such contour (a hole, or a
     part drawn over another). A group's signature is its box and its area.
  2. Peers (whole groups). For a slot, the syllables that use the same set of that slot (844 with one medial set per
     tail class: region_layout) with the same jamo there and other jamo in the other slots ("same"), and those with
     another jamo there ("other"). A group the slot draws has a twin in the "same" syllables - a group of the same
     width and height (within TOL of an em) and area (within AREA), its centre within POS (a vowel sits lower under a
     tall initial) - and few in the "other" ones; a group another slot draws does not care. A merged outline (ㅎ and
     ㅛ of Paperlogy's 효 drawn as one contour) has twins only where both its jamo recur, which the peers exclude. The slot whose share difference is >= SURE and MARGIN ahead takes the group whole.
  3. Regions. For each (slot, set, jamo), the median box of the outlines step 2 gave that slot in the syllables
     using it. A group step 2 left open goes whole to a slot whose region holds CONTAINED of its box while no other
     holds half; otherwise it is a merged outline (ㄴ and ㅗ of NanumGothic's 녹 drawn as one contour) and is clipped
     with path booleans (skia-pathops): each slot takes the part inside its region, overlaps of two regions go to the
     slot with the better peer score, what lies outside every region to the best-scored slot.

Result per syllable: [initial, medial, final] as lists of closed polylines (font units), and how each group was
decided ("peers", "region", "clip", "best").
"""

import os
import sys

TOL = 0.02		# twin groups: width and height within this share of the em ...
AREA = 0.06		# ... area within this share ...
POS = 0.12		# ... and centre within this share of the em (a vowel sits lower under a tall initial)
SURE = 0.3
MARGIN = 0.2
CONTAINED = 0.85
SAMPLE = 24
MIN_SAME = 2


def area(c):
    a = 0.0
    n = len(c)
    for i in range(n):
        x0, y0 = c[i]
        x1, y1 = c[(i + 1) % n]
        a += x0 * y1 - x1 * y0
    return a / 2.0


def box(cs):
    xs = [x for c in cs for x, _ in c]
    ys = [y for c in cs for _, y in c]
    return (min(xs), min(ys), max(xs), max(ys))


def inside(poly, x, y):
    """Even-odd point in polygon."""
    res = False
    n = len(poly)
    for i in range(n):
        x0, y0 = poly[i]
        x1, y1 = poly[(i + 1) % n]
        if (y0 > y) != (y1 > y) and x < x0 + (y - y0) * (x1 - x0) / (y1 - y0):
            res = not res
    return res


def group(contours):
    """Groups of contour indices by containment (see the module comment)."""
    n = len(contours)
    bx = [box([c]) for c in contours]
    ar = [abs(area(c)) for c in contours]
    parent = list(range(n))
    for b in range(n):
        best = None
        for a in range(n):
            if a == b or ar[a] <= ar[b]:
                continue
            A, B = bx[a], bx[b]
            if A[0] <= B[0] and A[1] <= B[1] and A[2] >= B[2] and A[3] >= B[3] and inside(contours[a], *contours[b][0]):
                if best is None or ar[a] < ar[best]:
                    best = a
        if best is not None:
            parent[b] = best

    def root(i):
        seen = set()
        while parent[i] != i and i not in seen:
            seen.add(i)
            i = parent[i]
        return i
    out = {}
    for i in range(n):
        out.setdefault(root(i), []).append(i)
    return list(out.values())


def load_pathops(pylib=None):
    for d in (pylib, os.environ.get("JAMOSET_PYLIB")):
        for part in (d or "").split(os.pathsep):
            if part and part not in sys.path:
                sys.path.append(part)
    try:
        import pathops
        return pathops
    except ImportError:
        return None


def to_path(pathops, polys):
    p = pathops.Path()
    for c in polys:
        p.moveTo(*c[0])
        for pt in c[1:]:
            p.lineTo(*pt)
        p.close()
    return p


def rect_path(pathops, r):
    return to_path(pathops, [[(r[0], r[1]), (r[2], r[1]), (r[2], r[3]), (r[0], r[3])]])


def from_path(path):
    out, cur = [], None
    for verb, pts in path.segments:
        if verb == "moveTo":
            cur = [pts[0]]
        elif verb == "lineTo":
            cur.append(pts[0])
        elif verb in ("qCurveTo", "curveTo"):
            cur.extend(pts)		# the clip of polylines is polylines; keep any point given
        elif verb in ("closePath", "endPath"):
            if cur and len(cur) >= 3:
                out.append([(round(x, 2), round(y, 2)) for x, y in cur])
            cur = None
    return out


def dilated(pathops, path, r):
    """@p path grown by @p r (its stroke outline of width 2r, united with it)."""
    st = pathops.Path(path)
    st.stroke(2 * r, pathops.LineCap.BUTT_CAP, pathops.LineJoin.MITER_JOIN, 2)	# no round joins: those are conics
    return pathops.op(path, st, pathops.PathOp.UNION)


def moved(pathops, polys, dx, dy):
    return to_path(pathops, [[(x + dx, y + dy) for x, y in c] for c in polys])


def jamo(s, slot):
    return (s // 588, (s // 28) % 21, s % 28)[slot]


class Segmenter:
    def __init__(self, outlines, keys, roles, upem, pylib=None):
        """@p outlines: {syllable: [closed polylines in font units]} of the drawn syllables; @p keys/@p roles: the
        region layout's (jamoaa.syllable_keys(jamoaa.region_layout()))."""
        self.outlines, self.keys, self.roles, self.upem = outlines, keys, roles, upem
        self.tol = TOL * upem
        self.pathops = load_pathops(pylib)
        self.groups = {}
        for s, cs in outlines.items():
            gs = []
            for g in group(cs):
                polys = [cs[i] for i in g]
                gs.append((g, box(polys), abs(sum(area(c) for c in polys))))
            self.groups[s] = gs
        self.by_set, self.by_slot = {}, {}
        for s in sorted(outlines):
            for slot, k in enumerate(keys[s]):
                if k is not None and k in roles:
                    self.by_set.setdefault((slot, roles[k][1]), []).append(s)
                    self.by_slot.setdefault((slot, s % 28 > 0), []).append(s)
        self._peers = {}

    def peers(self, s, slot):
        key = (s, slot)
        if key in self._peers:
            return self._peers[key]
        k = self.keys[s][slot]
        mine = jamo(s, slot)
        others = [j for j in range(3) if j != slot and not (j == 2 and s % 28 == 0)]
        out = ([], [])
        for pool in (self.by_set.get((slot, self.roles[k][1]), ()), self.by_slot.get((slot, s % 28 > 0), ())):
            same = [p for p in pool if p != s and jamo(p, slot) == mine and all(jamo(p, j) != jamo(s, j) for j in others)]
            other = [p for p in pool if jamo(p, slot) != mine]
            if len(same) >= MIN_SAME and other:
                st = max(1, len(same) // SAMPLE)
                ot = max(1, len(other) // SAMPLE)
                out = (same[::st][:SAMPLE], other[::ot][:SAMPLE])
                break
        self._peers[key] = out
        return out

    def twin(self, b, a, p):
        t, pos = self.tol, POS * self.upem
        w, h, cx, cy = b[2] - b[0], b[3] - b[1], (b[0] + b[2]) / 2, (b[1] + b[3]) / 2
        for _, b2, a2 in self.groups[p]:
            if (abs(w - (b2[2] - b2[0])) <= t and abs(h - (b2[3] - b2[1])) <= t and abs(a - a2) <= AREA * max(a, a2, 1e-9)
                    and abs(cx - (b2[0] + b2[2]) / 2) <= pos and abs(cy - (b2[1] + b2[3]) / 2) <= pos):
                return True
        return False

    def scores(self, s, gi):
        _, b, a = self.groups[s][gi]
        out = {}
        for slot in range(3):
            if self.keys[s][slot] is None:
                continue
            same, other = self.peers(s, slot)
            if same and other:
                out[slot] = (sum(self.twin(b, a, p) for p in same) / len(same)
                             - sum(self.twin(b, a, p) for p in other) / len(other))
        return out

    def run(self, log=None):
        """{syllable: [[polylines] per slot]}, {syllable: [(group, decision, slot or {slot: share})]}, counts."""
        decided, sc_all = {}, {}
        counts = {"groups": 0, "peers": 0, "region": 0, "clip": 0, "best": 0}
        for s, gs in self.groups.items():
            for gi in range(len(gs)):
                counts["groups"] += 1
                sc = self.scores(s, gi)
                sc_all[(s, gi)] = sc
                r = sorted(((v, k) for k, v in sc.items()), reverse=True)
                if r and r[0][0] >= SURE and (len(r) == 1 or r[0][0] - r[1][0] >= MARGIN):
                    decided[(s, gi)] = r[0][1]
        # regions: median box of the outlines the peers gave each (slot, piece)
        acc = {}
        for s, gs in self.groups.items():
            per = {}
            for gi, (_, b, _) in enumerate(gs):
                k = decided.get((s, gi))
                if k is not None:
                    q = per.get(k)
                    per[k] = b if q is None else (min(q[0], b[0]), min(q[1], b[1]), max(q[2], b[2]), max(q[3], b[3]))
            for k, b in per.items():
                acc.setdefault((k, self.keys[s][k]), []).append(b)
                acc.setdefault((k, "jamo", jamo(s, k), s % 28 > 0), []).append(b)
        med = lambda v: sorted(v)[len(v) // 2]
        self.regions = {key: tuple(med([b[i] for b in bs]) for i in range(4)) for key, bs in acc.items()}
        tp = self.templates(decided) if self.pathops else {}
        counts.update(template=0, template_split=0)
        out, record = {}, {}
        for s, gs in self.groups.items():
            cs = self.outlines[s]
            slots = [[] for _ in range(3)]
            rec = []
            present = [k for k in range(3) if self.keys[s][k] is not None]
            reg = {}
            for k in present:
                r = self.regions.get((k, self.keys[s][k])) or self.regions.get((k, "jamo", jamo(s, k), s % 28 > 0))
                if r:
                    reg[k] = r
            for gi, (g, b, _) in enumerate(gs):
                polys = [cs[i] for i in g]
                k = decided.get((s, gi))
                if k is not None:
                    counts["peers"] += 1
                    slots[k].extend(polys)
                    rec.append((g, "peers", k))
                    continue
                sc = sc_all[(s, gi)]
                if tp:
                    done = self.by_template(s, polys, b, present, tp, slots, rec, g, counts)
                    if done:
                        continue
                ba = max(1e-9, (b[2] - b[0]) * (b[3] - b[1]))
                ov = {}
                for k2, r in reg.items():
                    w = max(0.0, min(b[2], r[2]) - max(b[0], r[0]))
                    h = max(0.0, min(b[3], r[3]) - max(b[1], r[1]))
                    ov[k2] = w * h / ba
                ranked = sorted(((v, k2) for k2, v in ov.items()), reverse=True)
                best = max(present, key=lambda k2: (sc.get(k2, -9), ov.get(k2, 0)))
                if ranked and ranked[0][0] >= CONTAINED and (len(ranked) == 1 or ranked[1][0] < 0.5):
                    counts["region"] += 1
                    slots[ranked[0][1]].extend(polys)
                    rec.append((g, "region", ranked[0][1]))
                    continue
                po = self.pathops
                if po is None or len(reg) < 2:
                    counts["best"] += 1
                    slots[best].extend(polys)
                    rec.append((g, "best", best))
                    continue
                # clip: each slot its region, overlaps to the better peer score, the rest to the best slot
                order = sorted(reg, key=lambda k2: -sc.get(k2, -9))
                gp = to_path(po, polys)
                taken = None
                shares = {}
                total = abs(gp.area) or 1.0
                for k2 in order:
                    rp = rect_path(po, reg[k2])
                    if taken is not None:
                        rp = po.op(rp, taken, po.PathOp.DIFFERENCE)
                    part = po.op(gp, rp, po.PathOp.INTERSECTION)
                    taken = rp if taken is None else po.op(taken, rp, po.PathOp.UNION)
                    pl = from_path(part)
                    if pl:
                        slots[k2].extend(pl)
                        shares[k2] = shares.get(k2, 0) + abs(part.area) / total
                rest = po.op(gp, taken, po.PathOp.DIFFERENCE)
                pl = from_path(rest)
                if pl:
                    slots[best].extend(pl)
                    shares[best] = shares.get(best, 0) + abs(rest.area) / total
                counts["clip"] += 1
                rec.append((g, "clip", {k2: round(v, 3) for k2, v in shares.items()}))
            out[s] = slots
            record[s] = rec
        if log:
            log(f"segmentation: {counts}")
        return out, record, counts

    def by_template(self, s, polys, b, present, tp, slots, rec, g, counts):
        """Find the font's own outline of each of the syllable's jamo (a template from another syllable) inside the
        group: one that covers it gets it whole; two or more found in it split it, each slot the part of the group
        within a little of its placed template, what is left to the nearest. False if neither."""
        po = self.pathops
        gp = to_path(po, polys)
        fits = {}
        for k in present:
            cand = tp.get((k, self.keys[s][k])) or tp.get((k, "jamo", jamo(s, k), s % 28 > 0))
            cand = [c for c in (cand or ()) if c[2] != s]
            if cand:
                fits[k] = self.place(gp, b, cand[0][0], cand[0][1])
        whole = [(c, k) for k, (_, i, c) in fits.items() if c >= 0.8 and i >= 0.7]
        if whole:
            k = max(whole)[1]
            slots[k].extend(polys)
            rec.append((g, "template", k))
            counts["template"] += 1
            return True
        found = sorted(((i, k) for k, (_, i, c) in fits.items() if i >= 0.6), reverse=True)
        if len(found) < 2:
            return False
        r = 0.02 * self.upem
        taken = None
        shares = {}
        total = abs(gp.area) or 1.0
        centres = {}
        for _, k in found:
            pp = fits[k][0]
            bb = pp.bounds
            centres[k] = ((bb[0] + bb[2]) / 2, (bb[1] + bb[3]) / 2)
            zone = dilated(po, pp, r)
            if taken is not None:
                zone = po.op(zone, taken, po.PathOp.DIFFERENCE)
            part = po.op(gp, zone, po.PathOp.INTERSECTION)
            taken = zone if taken is None else po.op(taken, zone, po.PathOp.UNION)
            pl = from_path(part)
            if pl:
                slots[k].extend(pl)
                shares[k] = shares.get(k, 0) + abs(part.area) / total
        for c in from_path(po.op(gp, taken, po.PathOp.DIFFERENCE)):
            bx = box([c])
            cx, cy = (bx[0] + bx[2]) / 2, (bx[1] + bx[3]) / 2
            k = min(centres, key=lambda k2: (centres[k2][0] - cx) ** 2 + (centres[k2][1] - cy) ** 2)
            slots[k].append(c)
            shares[k] = shares.get(k, 0) + abs(area(c)) / total
        rec.append((g, "template_split", {k2: round(v, 3) for k2, v in shares.items()}))
        counts["template_split"] += 1
        return True

    def templates(self, decided):
        """{(slot, piece key) and (slot, "jamo", jamo, has final): [(polys, box)]} of the groups the peers decided:
        the font's own outline of a jamo in a context, to find inside a merged outline."""
        tp = {}
        for (s, gi), k in decided.items():
            g, b, _ = self.groups[s][gi]
            polys = [self.outlines[s][i] for i in g]
            tp.setdefault((k, self.keys[s][k]), []).append((polys, b, s))
            tp.setdefault((k, "jamo", jamo(s, k), s % 28 > 0), []).append((polys, b, s))
        # the medoid by box of each list first
        for key, lst in tp.items():
            ws = sorted(b[2] - b[0] for _, b, _ in lst)
            hs = sorted(b[3] - b[1] for _, b, _ in lst)
            mw, mh = ws[len(ws) // 2], hs[len(hs) // 2]
            lst.sort(key=lambda t: (abs(t[1][2] - t[1][0] - mw) + abs(t[1][3] - t[1][1] - mh), t[2]))
        return tp

    def place(self, gp, gbox, polys, tbox):
        """The template @p polys placed in the group's box at the best of five alignments (four corners and the
        centre): (placed path, share of the template inside the group, share of the group it covers)."""
        po = self.pathops
        ta = abs(to_path(po, polys).area) or 1.0
        ga = abs(gp.area) or 1.0
        best = None
        for ax, ay in ((0, 0), (1, 0), (0, 1), (1, 1), (0.5, 0.5)):
            dx = (gbox[0] - tbox[0]) * (1 - ax) + (gbox[2] - tbox[2]) * ax
            dy = (gbox[1] - tbox[1]) * (1 - ay) + (gbox[3] - tbox[3]) * ay
            pp = moved(po, polys, dx, dy)
            inter = abs(po.op(gp, pp, po.PathOp.INTERSECTION).area)
            if best is None or inter > best[1]:
                best = (pp, inter)
        return best[0], best[1] / ta, best[1] / ga


# ---- registration and cutting ------------------------------------------------------------------------------------------

CHO = "ㄱㄲㄴㄷㄸㄹㅁㅂㅃㅅㅆㅇㅈㅉㅊㅋㅌㅍㅎ"
JUNG = "ㅏㅐㅑㅒㅓㅔㅕㅖㅗㅘㅙㅚㅛㅜㅝㅞㅟㅠㅡㅢㅣ"
JONG = "ㄱㄲㄳㄴㄵㄶㄷㄹㄺㄻㄼㄽㄾㄿㅀㅁㅂㅄㅅㅆㅇㅈㅊㅋㅌㅍㅎ"
VERTICAL = set("ㅏㅐㅑㅒㅓㅔㅕㅖㅣ")
HOLED = set("ㅁㅂㅃㅇㅍㅎㄻㄼㄿㅀㅄ")	# jamo drawn with a closed counter (a ring)
HORIZONTAL = set("ㅗㅛㅜㅠㅡ")


def need_pathops(pylib=None):
    po = load_pathops(pylib)
    if po is None:
        raise ImportError("jamoseg cutting needs skia-pathops (pip install skia-pathops); to use a directory that "
                          "already holds it, set JAMOSET_PYLIB=DIR (or pass --pylib DIR)")
    return po


def letters(s):
    """The syllable's jamo as compatibility letters (initial, medial, final or None)."""
    L, V, T = s // 588, (s // 28) % 21, s % 28
    return CHO[L], JUNG[V], (JONG[T - 1] if T else None)


def vclass(v):
    return "v" if v in VERTICAL else "h" if v in HORIZONTAL else "m"


def fitted(polys, tb, B):
    """@p polys (box @p tb) mapped onto box @p B (x0, y0, x1, y1): translate and scale x and y apart."""
    sx = (B[2] - B[0]) / max(1e-9, tb[2] - tb[0])
    sy = (B[3] - B[1]) / max(1e-9, tb[3] - tb[1])
    return [[(B[0] + (x - tb[0]) * sx, B[1] + (y - tb[1]) * sy) for x, y in c] for c in polys]


class Cutter:
    """Each jamo of a syllable located by registering a template of it (the font's own compatibility letter, or
    later the median-shaped instance where the jamo is its own outline) onto the syllable; contour groups go to the
    jamo whose template covers them, and a group two templates share (a merged outline) is cut with a straight line
    across the junction into closed vector pieces (path booleans)."""

    def __init__(self, outlines, compat, upem, pylib=None, decisions=None):
        """@p decisions: {syllable char: {group index: {"assign": slot} or {"cut": [[x0, y0], [x1, y1]], "side":
        "above"|"below"|"left"|"right", "side_slot": slot, "other_slot": slot}}}: recorded choices (a reviewer's,
        e.g. a vision model picking a cut on a gridded render) that override the rules for those groups."""
        self.po = need_pathops(pylib)
        self.outlines, self.compat, self.upem = outlines, compat, upem
        self.prior = {}
        self.decisions = decisions or {}

    def decided_cut(self, G, d):
        """The group split by a recorded cut: the strip beyond the segment on @p d["side"] to side_slot, the rest to
        other_slot. Returns ({slot: path}, line)."""
        po = self.po
        if "region" in d:		# a polygon that is side_slot's: its edges inside the outline are the cuts
            rp = to_path(po, [d["region"]])
            part = po.op(G, rp, po.PathOp.INTERSECTION)
            rest = po.op(G, part, po.PathOp.DIFFERENCE)
            r = d["region"]
            return {d["side_slot"]: part, d["other_slot"]: rest}, [(tuple(r[i]), tuple(r[(i + 1) % len(r)])) for i in range(len(r))]
        (x0, y0), (x1, y1) = d["cut"]
        big = 4 * self.upem
        side = d["side"]
        if side == "below":
            r = (min(x0, x1), -big, max(x0, x1), (y0 + y1) / 2)
        elif side == "above":
            r = (min(x0, x1), (y0 + y1) / 2, max(x0, x1), big)
        elif side == "left":
            r = (-big, min(y0, y1), (x0 + x1) / 2, max(y0, y1))
        else:
            r = ((x0 + x1) / 2, min(y0, y1), big, max(y0, y1))
        part = po.op(G, rect_path(po, r), po.PathOp.INTERSECTION)
        rest = po.op(G, part, po.PathOp.DIFFERENCE)
        return {d["side_slot"]: part, d["other_slot"]: rest}, ((x0, y0), (x1, y1))

    def default_boxes(self, s, sb):
        """Starting boxes of the syllable's jamo in the syllable's box @p sb: the usual Hangul layout."""
        x0, y0, x1, y1 = sb
        w, h = x1 - x0, y1 - y0
        cho, jung, jong = letters(s)
        top0 = y0 + 0.38 * h if jong else y0			# the part above the final
        hh = y1 - top0
        out = {}
        c = vclass(jung)
        if c == "v":
            out[0] = (x0, top0, x0 + 0.58 * w, y1)
            out[1] = (x0 + 0.55 * w, top0, x1, y1)
        elif c == "h":
            out[0] = (x0 + 0.1 * w, top0 + 0.45 * hh, x1 - 0.1 * w, y1)
            out[1] = (x0, top0, x1, top0 + 0.55 * hh)
        else:
            out[0] = (x0, top0 + 0.45 * hh, x0 + 0.6 * w, y1)
            out[1] = (x0, top0, x1, y1)
        if jong:
            out[2] = (x0, y0, x1, y0 + 0.36 * h)
        return out

    def template(self, s, k):
        ch = letters(s)[k]
        key = (k, ch, vclass(letters(s)[1]), letters(s)[2] is not None)
        if key in getattr(self, "learned", {}):
            return self.learned[key]
        polys = self.compat.get(ch)
        return (polys, box(polys)) if polys else None

    def score(self, S, T, others, B=None, B0=None):
        """Ink of the syllable the template covers, less the template ink outside the syllable and the ink it shares
        with the other templates (two jamo do not draw the same stroke), less a little for straying from the
        starting box's proportions (a jamo is stretched, not reshaped)."""
        po = self.po
        inter = abs(po.op(S, T, po.PathOp.INTERSECTION).area)
        out = abs(T.area) - inter
        clash = sum(abs(po.op(T, o, po.PathOp.INTERSECTION).area) for o in others)
        pen = 0.0
        if B is not None and B0 is not None:
            import math
            r = lambda b: math.log(max(1e-9, (b[2] - b[0])) / max(1e-9, (b[3] - b[1])))
            pen = 0.15 * abs(r(B) - r(B0)) * abs(T.area)
        return inter - out - clash - pen

    def register(self, s, owned=None):
        """{slot: (registered path, box)} of syllable @p s. A template is fitted first whole (its box), then each of
        its contour groups on its own (ㅎ's tick and bar, and its ring, move apart as the syllable stretches them)."""
        po = self.po
        cs = self.outlines[s]
        S = to_path(po, cs)
        sb = box(cs)
        boxes = {}
        for k, B in self.default_boxes(s, sb).items():
            boxes[k] = self.prior.get((s, k)) or B
        tps = {k: self.template(s, k) for k in boxes}
        boxes = {k: b for k, b in boxes.items() if tps[k]}
        units = {}		# (slot, part) -> [polys, own box, current box, start box]
        for k in boxes:
            polys, tb = tps[k]
            whole = fitted(polys, tb, boxes[k])
            for pi, g in enumerate(group(polys)):
                pp = [whole[i] for i in g]
                b = box(pp)
                units[(k, pi)] = [[polys[i] for i in g], box([polys[i] for i in g]), b, b]
        paths = {u: to_path(po, fitted(v[0], v[1], v[2])) for u, v in units.items()}
        owned = owned or {}

        def score(u, P, others, B, Bs):
            # a template must not take a ring the structure rule already gave another jamo
            v = self.score(S, P, others, B, Bs)
            for k2, op in owned.items():
                if k2 != u[0]:
                    v -= 2 * abs(po.op(P, op, po.PathOp.INTERSECTION).area)
            return v
        steps = [0.04 * self.upem, 0.02 * self.upem, 0.01 * self.upem]
        for st in steps:
            for _ in range(4):
                moved_any = False
                for u in list(units):
                    polys, tb, B0, Bs = units[u]
                    others = [p for v, p in paths.items() if v != u]
                    best = score(u, paths[u], others, B0, Bs)
                    for e in range(4):
                        for d in (st, -st):
                            B = list(units[u][2])
                            B[e] += d
                            if B[2] - B[0] < 0.03 * self.upem or B[3] - B[1] < 0.03 * self.upem:
                                continue
                            P = to_path(po, fitted(polys, tb, B))
                            v = score(u, P, others, tuple(B), Bs)
                            if v > best + 1e-6:
                                best, units[u][2], paths[u] = v, tuple(B), P
                                moved_any = True
                if not moved_any:
                    break
        out = {}
        for k in boxes:
            us = [u for u in units if u[0] == k]
            p = paths[us[0]]
            for u in us[1:]:
                p = po.op(p, paths[u], po.PathOp.UNION)
            bs = [units[u][2] for u in us]
            out[k] = (p, (min(b[0] for b in bs), min(b[1] for b in bs), max(b[2] for b in bs), max(b[3] for b in bs)))
        return out

    def cut_pair(self, G, ta, tb, ka, kb):
        """(part of @p G for slot ka, part for kb, the cut line) - a straight cut across the junction between the
        registered templates @p ta and @p tb (path, box)."""
        po = self.po
        A, B = ta[1], tb[1]
        ca = ((A[0] + A[2]) / 2, (A[1] + A[3]) / 2)
        cb = ((B[0] + B[2]) / 2, (B[1] + B[3]) / 2)
        g = G.bounds
        m = 0.03 * self.upem
        big = 4 * self.upem
        if abs(cb[0] - ca[0]) >= abs(cb[1] - ca[1]):		# side by side: a vertical cut
            if cb[0] > ca[0]:
                c = (A[2] + B[0]) / 2
                half = (c, g[1] - big, g[2] + big, g[3] + big)
            else:
                c = (A[0] + B[2]) / 2
                half = (g[0] - big, g[1] - big, c, g[3] + big)
            line = ((c, max(g[1], B[1] - m)), (c, min(g[3], B[3] + m)))
        else:							# one above the other: a horizontal cut
            if cb[1] < ca[1]:
                c = (A[1] + B[3]) / 2
                half = (g[0] - big, g[1] - big, g[2] + big, c)
            else:
                c = (A[3] + B[1]) / 2
                half = (g[0] - big, c, g[2] + big, g[3] + big)
            line = ((max(g[0], B[0] - m), c), (min(g[2], B[2] + m), c))
        zone = po.op(rect_path(po, half), rect_path(po, (B[0] - m, B[1] - m, B[2] + m, B[3] + m)), po.PathOp.INTERSECTION)
        pb = po.op(G, zone, po.PathOp.INTERSECTION)
        pa = po.op(G, pb, po.PathOp.DIFFERENCE)
        return pa, pb, line

    def rings(self, s, groups):
        """The structure rule for counters: {group index: (slot of the ring, ring box)} for the groups that hold a
        hole (a contour inside another), given to the syllable's jamo that are drawn with a counter (ㅇ ㅎ ㅁ ㅂ ...):
        the only one, or with two of them (홍) the upper hole to the initial, the lower to the final. The ring box
        is the hole grown by the ring's thickness, measured on the side of the outline farthest from the hole."""
        cs = self.outlines[s]
        lets = letters(s)
        ring_slots = [k for k in (0, 2) if lets[k] and lets[k] in HOLED]
        found = []
        for gi, g in enumerate(groups):
            if len(g) < 2:
                continue
            outer = max(g, key=lambda i: abs(area(cs[i])))
            holes = [i for i in g if i != outer]
            hb = box([cs[i] for i in holes])
            ob = box([cs[outer]])
            t = min(hb[0] - ob[0], ob[2] - hb[2], hb[1] - ob[1], ob[3] - hb[3])
            t = max(t, max(min(hb[0] - ob[0], ob[2] - hb[2]), min(hb[1] - ob[1], ob[3] - hb[3])) * 0 + t)
            # the thinnest free side is the ring's stroke; a merged side is much thicker
            sides = sorted([hb[0] - ob[0], ob[2] - hb[2], hb[1] - ob[1], ob[3] - hb[3]])
            t = sides[0] if sides[0] > 0 else sides[1]
            found.append((gi, ((hb[1] + hb[3]) / 2), (hb[0] - t, hb[1] - t, hb[2] + t, hb[3] + t)))
        out = {}
        if not ring_slots or not found:
            return out
        if len(ring_slots) == 1:
            for gi, _, rb in found:
                out[gi] = (ring_slots[0], rb)
        else:
            found.sort(key=lambda f: -f[1])
            out[found[0][0]] = (ring_slots[0], found[0][2])
            for gi, _, rb in found[1:]:
                out[gi] = (ring_slots[1], rb)
        return out

    def cut_ring(self, G, ring_box, ks, others_reg):
        """A merged outline holding a counter: the part within the ring box goes to the ring's jamo @p ks, the rest
        (cut by the box's straight sides) to the other jamo whose registered template covers more of it."""
        po = self.po
        rp = rect_path(po, ring_box)
        ring = po.op(G, rp, po.PathOp.INTERSECTION)
        rest = po.op(G, rp, po.PathOp.DIFFERENCE)
        if abs(rest.area) < 0.15 * abs(G.area) or not others_reg:	# the ring's own outline, a little wider
            return {ks: G}, []
        k_o = max(others_reg, key=lambda k: abs(po.op(rest, others_reg[k][0], po.PathOp.INTERSECTION).area))
        b = ring_box
        lines = [((b[0], b[1]), (b[2], b[1])), ((b[0], b[3]), (b[2], b[3])), ((b[0], b[1]), (b[0], b[3])),
                 ((b[2], b[1]), (b[2], b[3]))]
        rb = rest.bounds
        lines = [l for l in lines if rb[0] - 1 <= l[0][0] <= rb[2] + 1 and rb[1] - 1 <= l[0][1] <= rb[3] + 1 or
                 rb[0] - 1 <= l[1][0] <= rb[2] + 1 and rb[1] - 1 <= l[1][1] <= rb[3] + 1]
        return {ks: ring, k_o: rest}, lines

    def segment(self, s):
        po = self.po
        cs = self.outlines[s]
        groups = group(cs)
        rings = self.rings(s, groups)
        owned = {}
        for gi, (k, rb) in rings.items():
            p = po.op(to_path(po, [cs[i] for i in groups[gi]]), rect_path(po, rb), po.PathOp.INTERSECTION)
            owned[k] = p if k not in owned else po.op(owned[k], p, po.PathOp.UNION)
        reg = self.register(s, owned)
        slots = [[] for _ in range(3)]
        rec, cuts = [], []
        for gi, g in enumerate(groups):
            polys = [cs[i] for i in g]
            G = to_path(po, polys)
            dec = self.decisions.get(chr(0xAC00 + s), {}).get(str(gi))
            if dec:
                if "assign" in dec:
                    slots[dec["assign"]].extend(polys)
                    rec.append((g, "decided", dec["assign"]))
                else:
                    parts, line = self.decided_cut(G, dec)
                    for k, p in parts.items():
                        slots[k].extend(from_path(p))
                    if isinstance(line, list):
                        bb = G.bounds		# draw only the region's edges that cross the outline's box
                        cuts.extend(l for l in line if all(bb[0] - 5 <= x <= bb[2] + 5 and bb[1] - 5 <= y <= bb[3] + 5 for x, y in l))
                    else:
                        cuts.append(line)
                    rec.append((g, "decided-cut", {k: round(abs(p.area) / (abs(G.area) or 1), 3) for k, p in parts.items()}))
                continue
            if gi in rings:
                ks, rb = rings[gi]
                parts, lines = self.cut_ring(G, rb, ks, {k: v for k, v in reg.items() if k != ks})
                for k, p in parts.items():
                    slots[k].extend(from_path(p))
                cuts.extend(lines)
                rec.append((g, "ring" if not lines else "ring-cut", {k: round(abs(p.area) / (abs(G.area) or 1), 3)
                                                                      for k, p in parts.items()}))
                continue
            ga = abs(G.area) or 1.0
            inter = {k: abs(po.op(G, p, po.PathOp.INTERSECTION).area) for k, (p, _) in reg.items()}
            cover = {k: v / ga for k, v in inter.items()}
            # a jamo is in the group when the group holds a fair share of it or it a fair share of the group
            within = {k: inter[k] / (abs(reg[k][0].area) or 1.0) for k in reg}
            r = sorted(((v, k) for k, v in cover.items()), reverse=True)
            if not r:
                continue
            many = [k for v, k in r if v >= 0.08 and within[k] >= 0.3]
            if len(many) < 2:
                slots[r[0][1]].extend(polys)
                rec.append((g, "whole", r[0][1]))
                continue
            # a merged outline: cut the later slots off one by one (the final, then the medial)
            many.sort()
            rest = G
            shares = {}
            while len(many) > 1:
                kb = many.pop()
                ka = many[-1]
                pa, pb, line = self.cut_pair(rest, reg[ka], reg[kb], ka, kb)
                slots[kb].extend(from_path(pb))
                shares[kb] = round(abs(pb.area) / ga, 3)
                cuts.append(line)
                rest = pa
            slots[many[0]].extend(from_path(rest))
            shares[many[0]] = round(abs(rest.area) / ga, 3)
            rec.append((g, "cut", shares))
        return slots, rec, cuts, reg

    def run(self, syllables=None, log=None):
        parts, recs, cuts, regs = {}, {}, {}, {}
        counts = {"groups": 0, "whole": 0, "cut": 0, "ring": 0, "ring-cut": 0, "decided": 0, "decided-cut": 0}
        for s in (syllables if syllables is not None else sorted(self.outlines)):
            sl, rec, cu, rg = self.segment(s)
            parts[s], recs[s], cuts[s], regs[s] = sl, rec, cu, {k: v[1] for k, v in rg.items()}
            for _, how, _ in rec:
                counts["groups"] += 1
                counts[how] += 1
        if log:
            log(f"cutting: {counts}")
        return parts, recs, cuts, regs, counts
