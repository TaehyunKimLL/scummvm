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
