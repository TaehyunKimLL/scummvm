#!/usr/bin/env python3
"""Vector master pieces: separate a font's jamo once, as outlines, and render every size from them.

  jamovec.py master FONT -o MASTER.json [--adaptive] [--decision-px 48] [--components]
  jamovec.py render MASTER.json --size N [--bpp 1|2] [--binarize threshold|ridge] -o OUT.SVF

The per-size fit (jamoset.py --from-ttf) separates and fits pieces again at every size, from rasters. Here the
separation happens once per font:

  1. At one decision size (default 48 px, the em) every drawn syllable is rendered and its contours are given to
     the initial, medial and final as jamoaa.contour_donors does (containment groups; the other syllables sharing
     each jamo decide, the 1 bpp region fit where they do not; merged outlines split pixel by pixel).
  2. The set system: 844, or jamoadapt's adaptive splits decided on those donors.
  3. A master piece per (set, jamo) is one donor's own contours: the medoid, the syllable whose raster of that slot
     is closest (L1) to the per-pixel median of all the piece's donors, among the donors whose slot holds only
     whole contour groups (no merged outline split by pixels: those have no outline of their own) and are not
     outliers (L1 above median + 3 MAD); failing that, the medoid among the donors whose slot took most of a merged
     outline, the whole outline (it then carries a stroke of the neighbouring jamo; counted).
  4. The master stores, in font units: the pieces' contours, the union ink box of the drawn syllables (the frame of
     any size), ASCII outlines and advances, and the JSET tables.

With --components (a font whose Hangul syllables are composite glyphs: each syllable = initial + medial [+ final]
component glyphs, as GothicA1, SingleDay, Dovemayo, NeoDunggeunmo and the KS 2,479 of NanumSquareRoundR are) step 1 is
skipped: the components ARE the pieces. composite_parts() reads each syllable's components from the glyf table
(glyph.components, getComponentInfo() -> (name, transform)), flattens each one's outline with the transform applied,
and puts them in the order initial, medial, final: the file order where it is geometrically plausible (the final the
lowest, the initial up-left of the medial), else by position (the file order of some fonts is permuted for some
syllables; counted and listed in info). Each component rendered at the decision size is that slot's donor and its
contours are the slot's record, so steps 2 and 3 run unchanged. Syllables that are not composite in a partly composite
font are left out of the donors (their sets are chosen by the others, their pieces borrowed as for unused ones).

Rendering a size is only: the frame from the stored box, each master outline rasterised by exact area at that
size (jamoaa.rasterize), quantised (2 bpp as mkfont.py does; 1 bpp by coverage >= 0.5, or ridge). No fitting.
Pieces no drawn syllable uses take the piece of the set they were split from, else the same jamo's piece of the set
with most users (as the fitted path does).
"""

import argparse
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import jamoaa as A  # noqa: E402
import jamoset as J  # noqa: E402


def tables_json(t):
    return {k: getattr(t, k) for k in ("lc", "tc", "ni", "nm", "nf", "advance", "lead_class", "tail_class",
                                       "init_set", "med_set", "fin_set", "init_piece", "med_piece", "fin_piece",
                                       "compat")}


def tables_from(d):
    t = J.Tables(d["lc"], d["tc"], d["ni"], d["nm"], d["nf"], d["advance"])
    for k in ("lead_class", "tail_class", "init_set", "med_set", "fin_set", "init_piece", "med_piece", "fin_piece",
              "compat"):
        setattr(t, k, list(d[k]))
    return t


def _glyf_components(face, cp):
    """[(glyph name, transform)] of the composite glyph of @p cp, or None (flat glyph, no glyf table, not drawn)."""
    name = face.cmap.get(cp)
    try:
        glyf = face.font["glyf"]
        gl = glyf[name] if name is not None else None
    except KeyError:
        return None
    if gl is None or not gl.isComposite():
        return None
    return [c.getComponentInfo() for c in gl.components]


def order_parts(boxes, has_final):
    """Indices into @p boxes (ink boxes (x0, y0, x1, y1) of the components in file order, y up, or None for an empty
    component) in the order initial, medial[, final]. The file order is kept where it is plausible: the final's
    centre lower than the others', the initial up-left of the medial (its centre's y - x the larger); otherwise the
    final is the lowest and the initial the more up-left of the other two. Empty components keep their file place."""
    k = len(boxes)
    order = list(range(k))
    if None in boxes:
        return order

    def cen(i):
        b = boxes[i]
        return (b[0] + b[2]) / 2.0, (b[1] + b[3]) / 2.0

    def upleft(i):
        x, y = cen(i)
        return y - x
    if has_final and k == 3:
        if cen(2)[1] < min(cen(0)[1], cen(1)[1]) and upleft(0) >= upleft(1):
            return order
        fin = min(range(3), key=lambda i: cen(i)[1])
        a, b = [i for i in range(3) if i != fin]
        return [a, b, fin] if upleft(a) >= upleft(b) else [b, a, fin]
    if k == 2:
        return order if upleft(0) >= upleft(1) else [1, 0]
    return order


def composite_parts(face, cp, has_final=None):
    """([contours of the initial, of the medial, of the final], reordered, n) of the composite syllable @p cp: each
    component flattened (pen-relative pixels, y up) with its transform; n is the contour count of the whole glyph
    in file order (the numbering of face.contours(cp)). None if the glyph is not a composite of 2 or 3 components
    that fits the syllable (3 for a final, 2 without), or reordered=True when the file order was permuted."""
    comps = _glyf_components(face, cp)
    if not comps:
        return None
    s = cp - 0xAC00
    has_final = s % 28 != 0 if has_final is None else has_final
    if len(comps) != 2 + bool(has_final):
        return None
    from fontTools.pens.transformPen import TransformPen
    parts, boxes, first = [], [], []
    total = 0
    for name, t in comps:
        pen = A._FlatPen(face.glyphs, face.scale)
        face.glyphs[name].draw(TransformPen(pen, t))
        cs = [c for c in pen.contours if len(c) >= 3]
        parts.append(cs)
        boxes.append(A.glyph_box(cs))
        first.append(total)
        total += len(cs)
    order = order_parts(boxes, has_final)
    return [(parts[i], list(range(first[i], first[i] + len(parts[i])))) for i in order], order != sorted(order), total


def component_donors(face, cps, ox, oy, D, say):
    """(donors, record, info) of the composite syllables among @p cps (see the module comment)."""
    n = D * D
    donors, record = {}, {}
    info = {"mode": "components", "drawn": len(cps), "composite": 0, "flat_left_out": 0, "bad_shape": 0,
            "reordered": 0, "reordered_syllables": "", "contour_mismatch": 0}
    swapped = []
    for cp in cps:
        s = cp - 0xAC00
        r = composite_parts(face, cp)
        if r is None:
            info["flat_left_out" if _glyf_components(face, cp) is None else "bad_shape"] += 1
            continue
        parts, reordered, total = r
        if total != len(face.contours(cp)):
            info["contour_mismatch"] += 1
            continue
        info["composite"] += 1
        if reordered:
            info["reordered"] += 1
            swapped.append(chr(cp))
        d, rec = [], []
        for cs, idx in parts:
            cov = A.coverage(A.add([A.rasterize(c, ox, oy, D, D) for c in cs], n)) if cs else [0.0] * n
            d.append(bytes(int(round(v * 255)) for v in cov))
            rec.append((idx, len(d) - 1))
        donors[s] = d + [None] * (3 - len(d))
        record[s] = [(idx, slot) for idx, slot in rec if idx]
    info["reordered_syllables"] = "".join(swapped[:200])
    say(f"components: {info}")
    return donors, record, info


def build_master(path, adaptive=None, decision_px=48, passes=12, pylib=None, log=None, class_limit=None,
                 components=False):
    """The master dict of the outline font @p path (see the module comment). @p components: True takes each
    syllable's pieces from its composite glyph's components (False: the raster separation; "auto": components if
    most drawn syllables are composite)."""
    import jamofit as jf
    say = log or (lambda *a: None)
    D = decision_px
    face = A.OutlineFace(path, D, pylib)
    n = D * D
    cps = [0xAC00 + s for s in range(11172) if face.has(0xAC00 + s)]
    if not cps:
        raise J.SourceError(f"{path}: draws none of the Hangul syllables")
    ox, oy, finfo = A.frame(face, cps, D, D, None)
    if components == "auto":
        components = sum(_glyf_components(face, cp) is not None for cp in cps) * 2 > len(cps)
    if components:
        donors, record, cinfo = component_donors(face, cps, ox, oy, D, say)
        if not donors:
            raise J.SourceError(f"{path}: no composite syllable glyphs (--components needs a glyf table with "
                                "composite Hangul syllables)")
    else:
        targets, direct = {}, {}
        for cp in cps:
            cov = A.render_coverage(face, cp, ox, oy, D, D)
            g = A.binarize(cov, D, D, "threshold", 0.5)
            if g:
                targets[cp - 0xAC00] = g
        say(f"decision renders: {len(cps)} syllables at {D} px")
        keys844, _, roles844 = A.syllable_keys(A.region_layout())
        P844, _ = jf.fit_pieces({s: keys844[s] for s in targets}, targets, D, D, None, passes)
        say("region fit done")
        record = {}
        donors, cinfo = A.contour_donors(face, sorted(targets), keys844, P844, ox, oy, D, D, targets, roles844, record)
        say(f"contours: {cinfo}")
    ainfo = None
    if adaptive:
        import jamoadapt
        layout, ainfo = jamoadapt.adapt(donors, 2, tuple(adaptive), log=say, class_limit=class_limit)
    else:
        layout = J.LAYOUTS["844"]
    keys, t, roles = A.syllable_keys(layout)
    stacks = {}
    for s in sorted(donors):
        for slot, k in enumerate(keys[s]):
            if k is not None and donors[s][slot] is not None:
                stacks.setdefault(k, []).append(s)
    sc = face.scale
    pieces = {}
    stats = {"pieces": 0, "medoid_clean": 0, "medoid_with_merged_outline": 0, "empty": 0, "outlier_donors": 0}
    for k, users in stacks.items():
        slot = roles[k][0]
        med = A.median_consensus([donors[s][slot] for s in users])
        dist = {s: sum(abs(a - b) for a, b in zip(donors[s][slot], med)) for s in users}
        ds = sorted(dist.values())
        md = ds[len(ds) // 2]
        mad = sorted(abs(x - md) for x in ds)[len(ds) // 2]
        outl = [s for s in users if len(users) >= 5 and dist[s] > md + 3 * 1.4826 * mad]
        stats["outlier_donors"] += len(outl)

        def whole(s):
            return [g for g, to in record.get(s, ()) if to == slot]

        def mostly(s):
            # the slot's whole groups and the merged ones it took most of
            return [g for g, to in record.get(s, ()) if to == slot or (isinstance(to, dict) and to.get(slot, 0) >= 0.5)]

        def split(s):
            return any(isinstance(to, dict) and to.get(slot, 0) > 0 for _, to in record.get(s, ()))
        clean = [s for s in users if whole(s) and not split(s) and s not in outl]
        some = [s for s in users if mostly(s)]
        if clean:
            best = min(clean, key=lambda s: (dist[s], s))
            stats["medoid_clean"] += 1
            idx = sorted(i for g in whole(best) for i in g)
        elif some:
            best = min(some, key=lambda s: (dist[s], s))
            stats["medoid_with_merged_outline"] += 1
            idx = sorted(i for g in mostly(best) for i in g)
        else:
            stats["empty"] += 1		# no outline of its own: borrowed like an unused piece (fill_unseen)
            continue
        cs = face.contours(0xAC00 + best)
        pieces[k] = {"donor": chr(0xAC00 + best), "users": len(users), "outliers": len(outl),
                     "distance": round(dist[best] / 255.0 / n, 4),
                     "contours": [[[round(x / sc, 2), round(y / sc, 2)] for x, y in cs[i]] for i in idx]}
        stats["pieces"] += 1
    users = {k: len(v) for k, v in stacks.items() if k in pieces}
    fallback = A.fill_unseen(layout, pieces, users, roles, t)
    boxes = [A.glyph_box(face.contours(cp)) for cp in cps]
    box = [min(b[0] for b in boxes if b) / sc, min(b[1] for b in boxes if b) / sc,
           max(b[2] for b in boxes if b) / sc, max(b[3] for b in boxes if b) / sc]
    ascii_ = {}
    for cp in range(0x20, 0x7F):
        if cp in face.cmap:
            ascii_[str(cp)] = {"advance": round(face.advance(cp) / sc, 2),
                               "contours": [[[round(x / sc, 2), round(y / sc, 2)] for x, y in c]
                                            for c in face.contours(cp)]}
    adv = {}
    for cp in cps:
        a = round(face.advance(cp) / sc)
        adv[a] = adv.get(a, 0) + 1
    master = {"font": os.path.basename(path), "upem": face.upem, "decision_px": D, "layout": layout.name,
              "drawn": len(cps), "box": [round(v, 2) for v in box], "syllable_advance": max(adv, key=lambda a: (adv[a], a)),
              "tables": tables_json(t), "roles": {str(k): v for k, v in roles.items()},
              "pieces": {str(k): v for k, v in pieces.items()}, "ascii": ascii_,
              "info": {"contours": cinfo, "medoids": stats, "unseen_pieces": fallback,
                       "adaptive": {k: v for k, v in (ainfo or {}).items() if k != "history"} or None}}
    return master


def frame_of(master, size, cell=None, baseline=None):
    """(w, h, ox, oy) of @p master at @p size: the stored union box centred, as jamoaa.frame does."""
    w, h = cell or (size, size)
    sc = size / float(master["upem"])
    x0, y0, x1, y1 = master["box"]
    eps = 1e-6
    left = int(math.floor(x0 * sc + eps))
    right = int(math.ceil(x1 * sc - eps)) - 1
    top = int(math.ceil(y1 * sc - eps))
    bottom = int(math.floor(y0 * sc + eps))
    iw, ih = right - left + 1, top - bottom
    return w, h, -left + (w - iw) // 2, (baseline if baseline is not None else top + (h - ih) // 2)


def piece_levels(master, size, bpp=2, binarize="threshold", threshold=0.5, cell=None, baseline=None):
    """{piece number: levels (0..3, or 0/1 at 1 bpp)} of every master piece at @p size, and the tables: for
    measuring a master without writing it (a simulated table past the engine's limits cannot be written)."""
    w, h, ox, oy = frame_of(master, size, cell, baseline)
    sc = size / float(master["upem"])
    n = w * h
    out = {}
    for k, pc in master["pieces"].items():
        acc = [0.0] * n
        for c in pc["contours"]:
            for i, v in enumerate(A.rasterize([(x * sc, y * sc) for x, y in c], ox, oy, w, h)):
                if v:
                    acc[i] += v
        cov = A.coverage(acc)
        if bpp == 2:
            out[int(k)] = [A.level(v) for v in cov]
        else:
            g = A.binarize(cov, w, h, binarize, threshold)
            W = w + 1
            out[int(k)] = [1 if g >> (y * W + x) & 1 else 0 for y in range(h) for x in range(w)]
    return out, tables_from(master["tables"])


def render(master, size, bpp=2, binarize="threshold", threshold=0.5, cell=None, baseline=None, symbols=False):
    """The SVF bytes of @p master at @p size px: no separation or fitting, the master outlines rasterised."""
    w, h = cell or (size, size)
    sc = size / float(master["upem"])
    x0, y0, x1, y1 = master["box"]
    eps = 1e-6
    left = int(math.floor(x0 * sc + eps))
    right = int(math.ceil(x1 * sc - eps)) - 1
    top = int(math.ceil(y1 * sc - eps))
    bottom = int(math.floor(y0 * sc + eps))
    iw, ih = right - left + 1, top - bottom
    ox = -left + (w - iw) // 2
    oy = baseline if baseline is not None else top + (h - ih) // 2
    n = w * h

    def raster(contours):
        acc = [0.0] * n
        for c in contours:
            for i, v in enumerate(A.rasterize([(x * sc, y * sc) for x, y in c], ox, oy, w, h)):
                if v:
                    acc[i] += v
        cov = A.coverage(acc)
        if bpp == 2:
            return [A.level(v) for v in cov]
        g = A.binarize(cov, w, h, binarize, threshold)
        W = w + 1
        return [1 if g >> (y * W + x) & 1 else 0 for y in range(h) for x in range(w)]

    def pack(lv):
        if bpp == 2:
            return A.pack_levels(lv, w, h)
        nb = (w + 7) // 8
        out = bytearray(nb * h)
        for i, v in enumerate(lv):
            if v:
                out[(i // w) * nb + (i % w) // 8] |= 0x80 >> (i % w % 8)
        return bytes(out)
    t = tables_from(master["tables"])
    sa = int(round(master["syllable_advance"] * sc))
    if 0 < sa < w:
        t.advance = sa
    extra = {}
    for cps, a in master["ascii"].items():
        lv = raster(a["contours"])
        ix, iwd = A.ink_columns(lv, w, h)
        extra[int(cps)] = (pack(lv), min(255, int(round(a["advance"] * sc))), ix, iwd)
    if symbols:
        adv = extra[0x20][1] if 0x20 in extra else w // 2
        nb = (w + 7) // 8
        for cp, (gb, a, ix, iwd) in J.symbol_glyphs(w, h, oy, adv).items():
            if cp not in extra:
                rows = [int.from_bytes(gb[y * nb:(y + 1) * nb], "big") >> (nb * 8 - w) for y in range(h)]
                lv = [(3 if bpp == 2 else 1) if rows[y] >> (w - 1 - x) & 1 else 0 for y in range(h) for x in range(w)]
                extra[cp] = (pack(lv), a, ix, iwd)
    J.blank_compat(t, {cp for cp, g in extra.items() if any(g[0])})
    used = J.used_pieces(t)
    mapping = {p: i for i, p in enumerate(used)}
    jset = J.pack(J.renumber(t, mapping))
    glyphs = []
    for p in used:
        pc = master["pieces"].get(str(p))
        lv = raster(pc["contours"]) if pc else [0] * n
        ix, iwd = A.ink_columns(lv, w, h)
        glyphs.append((J.PIECE_BASE + mapping[p], pack(lv), w, ix, iwd))
    for cp, (gb, a, ix, iwd) in sorted(extra.items()):
        glyphs.append((cp, gb, a, ix, iwd))
    glyphs.sort()
    return J.write_svfn(w, h, oy, glyphs, jset, bpp=bpp), {"origin_x": ox, "baseline": oy, "pieces": len(mapping)}


def main():
    ap = argparse.ArgumentParser(description="vector master jamo pieces: separate once, render any size")
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("master")
    m.add_argument("font")
    m.add_argument("-o", "--output", required=True)
    m.add_argument("--adaptive", nargs="?", const="24,16,16", help="adaptive sets up to NI,NM,NF (default 24,16,16)")
    m.add_argument("--decision-px", type=int, default=48)
    m.add_argument("--class-limit", type=int, help="lead/tail classes (default 8; more only to simulate a bigger table)")
    m.add_argument("--components", action="store_true",
                   help="take the pieces from the composite syllable glyphs' components (no raster separation)")
    m.add_argument("--pylib")
    r = sub.add_parser("render")
    r.add_argument("master")
    r.add_argument("--size", type=int, required=True)
    r.add_argument("--bpp", type=int, choices=(1, 2), default=2)
    r.add_argument("--binarize", choices=("threshold", "ridge"), default="threshold")
    r.add_argument("--cell")
    r.add_argument("--baseline", type=int)
    r.add_argument("--symbols", action="store_true")
    r.add_argument("--check", action="store_true")
    r.add_argument("-o", "--output", required=True)
    args = ap.parse_args()
    if args.cmd == "master":
        lim = tuple(int(x) for x in args.adaptive.split(",")) if args.adaptive else None
        mst = build_master(args.font, lim, args.decision_px, pylib=args.pylib, log=lambda *a: print(*a, flush=True),
                           class_limit=args.class_limit, components=args.components)
        with open(args.output, "w") as fh:
            json.dump(mst, fh, ensure_ascii=False)
        print(f"{args.output}: {len(mst['pieces'])} pieces, {mst['info']['medoids']}")
    else:
        mst = json.load(open(args.master))
        cell = tuple(int(x) for x in args.cell.lower().split("x")) if args.cell else None
        out, info = render(mst, args.size, args.bpp, args.binarize, 0.5, cell, args.baseline, args.symbols)
        open(args.output, "wb").write(out)
        print(f"{args.output}: {len(out)} bytes, {info['pieces']} pieces at {args.size} px, {args.bpp} bpp")
        if args.check:
            print("check:", J.check(out, J.LAYOUTS["844"], None))


if __name__ == "__main__":
    main()
