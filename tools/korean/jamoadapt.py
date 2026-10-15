#!/usr/bin/env python3
"""Adaptive set systems for fitted jamo sets (jamoset.py --layout adaptive[:NI,NM,NF]).

A fixed layout (844) decides which syllables share a piece. When a font draws a jamo differently in two contexts the
844 tables put in one set, the shared piece is a compromise and both contexts compose worse. This module starts from
844 and splits sets where the syllables that share a piece disagree, up to the engine's JSET limits (8 lead classes,
8 tail classes, 24 initial / 16 medial / 16 final sets; graphics/hires_text/jamo_set.h).

Input: "donors", each present syllable's own raster per slot (initial, medial, final) - from the outline's contours
(jamoaa.contour_donors) or, for a bitmap source, the target split by the 844 region fit (jamoaa.pixel_donors). A
donor is compared as thermometer bit planes (level >= 1, >= 2, >= 3 as gints), so the L1 level distance of two
rasters is the summed popcount of the planes' XOR and the per-pixel median of a group is the per-plane majority.

  1. Classes. Initials are grouped into lead classes by how the medial donors after them look ({ㄱ,ㅋ} stays one
     class as in 844, the other 17 initials are clustered into the remaining 7), tails into tail classes by how the
     initial and medial donors above them look (no tail is class 0, the 27 tails are clustered into 7). Average
     linkage on the mean L1 distance of per-context medians.
  2. Items. A cell is (lead class, vowel, tail class); each slot's set is a function of an item, the cells pooled
     by what that slot's 844 table looks at (item_of), starting from the 844 set of the item's 844 context.
  3. Splits. For every set, a 2-medians clustering of its cells (seeded by its largest cell and the cell farthest
     from it) proposes a split; its gain is the drop in summed L1 distance of the donors to their piece's median.
     The best split over all slots is taken while the slot is under its limit, each side keeps at least
     @p min_users syllables, and the gain is at least @p min_gain of the starting cost. Lloyd passes then move
     items a split left on the wrong side (reassign). Items no present syllable falls in follow the nearest item
     with users that 844 puts in the same set.

The result is a jamoset.Layout named "adaptive" whose tables encode the mapping; pieces of a split set that no
present syllable uses take the piece of the set it was split from (Layout.fallback).
"""

import jamoset as J

LIMITS = (24, 16, 16)
CLASS_LIMIT = 8


def planes(raster, nlev):
    """Thermometer planes of a 0..255 raster: [gint of level >= k] for k = 1..nlev (nlev 1: ink >= 128)."""
    cuts = (128,) if nlev == 1 else (43, 128, 213)
    out = []
    for c in cuts:
        g = 0
        for i, v in enumerate(raster):
            if v >= c:
                g |= 1 << i
        out.append(g)
    return tuple(out)


def l1(a, b):
    return sum((x ^ y).bit_count() for x, y in zip(a, b))


def majority(items, npl):
    """Per-plane majority (the per-pixel median for thermometer codes); ties go to off."""
    n = len(items)
    out = []
    for k in range(npl):
        counts = {}
        for it in items:
            g = it[k]
            while g:
                low = g & -g
                b = low.bit_length() - 1
                counts[b] = counts.get(b, 0) + 1
                g ^= low
        m = 0
        for b, c in counts.items():
            if 2 * c > n:
                m |= 1 << b
        out.append(m)
    return tuple(out)


def group_cost(items, npl):
    if not items:
        return 0
    m = majority(items, npl)
    return sum(l1(it, m) for it in items)


def _cluster(names, dist, k):
    """Average-linkage agglomeration of @p names into @p k groups by @p dist(a, b) (None = unknown, counted high)."""
    groups = [[n] for n in names]
    cache = {}

    def d(a, b):
        key = (a, b) if a < b else (b, a)
        if key not in cache:
            v = dist(a, b)
            cache[key] = 1e18 if v is None else v
        return cache[key]
    while len(groups) > k:
        best = None
        for i in range(len(groups)):
            for j in range(i + 1, len(groups)):
                v = sum(d(a, b) for a in groups[i] for b in groups[j]) / (len(groups[i]) * len(groups[j]))
                if best is None or v < best[0]:
                    best = (v, i, j)
        _, i, j = best
        groups[i] += groups.pop(j)
    return [sorted(g) for g in groups]


def _ctx_medians(syls, slot_of, key_of, P, npl):
    """{(name, context): majority of the donors of slot @p slot_of} grouped by @p key_of(s) -> (name, context)."""
    acc = {}
    for s in syls:
        d = P[s][slot_of]
        if d is None:
            continue
        acc.setdefault(key_of(s), []).append(d)
    return {k: majority(v, npl) for k, v in acc.items()}


def _profile_dist(med, a, b):
    ctx_a = {c: m for (n, c), m in med.items() if n == a}
    tot = cnt = 0
    for (n, c), m in med.items():
        if n == b and c in ctx_a:
            tot += l1(ctx_a[c], m)
            cnt += 1
    return tot / cnt if cnt else None


def classes(P, npl):
    """(lead class per initial [19], tail class per tail [28]) from the donors (see the module comment)."""
    syls = sorted(P)
    medj = _ctx_medians(syls, 1, lambda s: (s // 588, ((s // 28) % 21, s % 28 > 0)), P, npl)
    rest = [L for L in range(19) if L not in J.KK]
    present = {n for n, _ in medj}
    known = [L for L in rest if L in present]
    groups = _cluster(known, lambda a, b: _profile_dist(medj, a, b), min(CLASS_LIMIT - 1, len(known))) if known else []
    lead = [0] * 19
    for ci, g in enumerate(groups):
        for L in g:
            lead[L] = ci + 1
    if groups:
        big = 1 + max(range(len(groups)), key=lambda i: len(groups[i]))
        for L in rest:
            if L not in present:
                lead[L] = big
    else:
        for L in rest:
            lead[L] = 1
    medc = _ctx_medians(syls, 0, lambda s: (s % 28, (s // 28) % 21), P, npl)
    medv = _ctx_medians(syls, 1, lambda s: (s % 28, (s // 28) % 21), P, npl)

    def tdist(a, b):
        x, y = _profile_dist(medc, a, b), _profile_dist(medv, a, b)
        if x is None and y is None:
            return None
        return (x or 0) + (y or 0)
    tails = [T for T in range(1, 28) if any(n == T for n, _ in medc) or any(n == T for n, _ in medv)]
    tgroups = _cluster(tails, tdist, min(CLASS_LIMIT - 1, len(tails))) if tails else []
    tail = [0] * 28
    for ci, g in enumerate(tgroups):
        for T in g:
            tail[T] = ci + 1
    if tgroups:
        big = 1 + max(range(len(tgroups)), key=lambda i: len(tgroups[i]))
        for T in range(1, 28):
            if T not in tails:
                tail[T] = big
    else:
        for T in range(1, 28):
            tail[T] = 1
    return lead, tail


def _844_set(slot, lc844, V, T):
    tc844 = 1 if T else 0
    if slot == 0:
        return (J.I844_FIN if tc844 else J.I844_NOFIN)[V]
    if slot == 1:
        return lc844 + 2 * tc844
    return J.F844[V]


def item_of(slot, cell):
    """The context a slot's set is chosen by: cells pooled so each holds enough syllables. An initial's set by (vowel,
    tail class) over all lead classes, a medial's by (lead class, tail class, vowel), a final's by (vowel, lead class)
    over all tail classes (a final's own tail is its jamo). These are what the 844 tables look at, at finer classes."""
    lc, v, tc = cell
    return ((v, tc), (lc, tc, v), (v, lc))[slot]


def adapt(donors, nlev, limits=LIMITS, min_gain=0.003, min_users=10, log=None):
    """The adaptive Layout from @p donors {syllable: [raster 0..255 per slot or None]}. Returns (layout, info)."""
    say = log or (lambda *a: None)
    P = {s: [None if d is None else planes(d, nlev) for d in ds] for s, ds in donors.items()}
    npl = 1 if nlev == 1 else 3
    lead, tail = classes(P, npl)
    LC, TC = max(lead) + 1, max(tail) + 1
    cell_of = lambda s: (lead[s // 588], (s // 28) % 21, tail[s % 28])
    jamo_of = (lambda s: s // 588, lambda s: (s // 28) % 21, lambda s: s % 28)
    cells = [(lc, v, tc) for lc in range(LC) for v in range(21) for tc in range(TC)]
    items = [sorted({item_of(k, c) for c in cells}) for k in range(3)]
    # the starting mapping: every item to the 844 set of its 844 context (lead class 0 is {ㄱ,ㅋ}, tail class 0 none)
    mapping = [{}, {}, {}]
    for c in cells:
        for k in range(3):
            mapping[k].setdefault(item_of(k, c), _844_set(k, 0 if c[0] == 0 else 1, c[1], c[2]))
    counts = [8, 4, 4]
    parent = [dict(), dict(), dict()]
    by_item = [{}, {}, {}]
    for s in P:
        for k in range(3):
            if P[s][k] is not None:
                by_item[k].setdefault(item_of(k, cell_of(s)), []).append(s)

    def members(slot, st):
        return [c for c in items[slot] if mapping[slot][c] == st and c in by_item[slot]]

    def cost_of(slot, item_list):
        groups = {}
        for c in item_list:
            for s in by_item[slot].get(c, ()):
                groups.setdefault(jamo_of[slot](s), []).append(P[s][slot])
        return sum(group_cost(v, npl) for v in groups.values())

    def users(slot, item_list):
        return sum(len(by_item[slot].get(c, ())) for c in item_list)

    def propose(slot, st):
        cl = members(slot, st)
        if len(cl) < 2:
            return None
        base = cost_of(slot, cl)
        prof = {}
        for c in cl:
            g = {}
            for s in by_item[slot][c]:
                g.setdefault(jamo_of[slot](s), []).append(P[s][slot])
            prof[c] = {j: (majority(v, npl), len(v)) for j, v in g.items()}

        def cdist(a, b):
            tot = cnt = 0
            for j, (m, n) in prof[a].items():
                if j in prof[b]:
                    tot += l1(m, prof[b][j][0])
                    cnt += 1
            return tot / cnt if cnt else 0
        seed_a = max(cl, key=lambda c: (sum(n for _, n in prof[c].values()), c))
        seed_b = max(cl, key=lambda c: (cdist(seed_a, c), c))
        if seed_b == seed_a:
            return None
        side = {c: (cdist(seed_b, c) < cdist(seed_a, c)) for c in cl}
        for _ in range(8):
            meds = []
            for want in (False, True):
                g = {}
                for c in cl:
                    if side[c] == want:
                        for s in by_item[slot][c]:
                            g.setdefault(jamo_of[slot](s), []).append(P[s][slot])
                meds.append({j: majority(v, npl) for j, v in g.items()})
            changed = False
            for c in cl:
                costs = []
                for k in (0, 1):
                    e = 0
                    for s in by_item[slot][c]:
                        j = jamo_of[slot](s)
                        m = meds[k].get(j) or meds[1 - k].get(j)
                        e += l1(P[s][slot], m) if m else 0
                    costs.append(e)
                new = costs[1] < costs[0]
                if new != side[c]:
                    side[c] = new
                    changed = True
            if not changed:
                break
        a = [c for c in cl if not side[c]]
        b = [c for c in cl if side[c]]
        if not a or not b or users(slot, a) < min_users or users(slot, b) < min_users:
            return None
        gain = base - cost_of(slot, a) - cost_of(slot, b)
        return (gain, b)

    start = [sum(cost_of(slot, members(slot, st)) for st in range(counts[slot])) for slot in range(3)]
    total0 = sum(start)
    proposals = {}
    history = []
    while True:
        for slot in range(3):
            for st in range(counts[slot]):
                if (slot, st) not in proposals:
                    proposals[(slot, st)] = propose(slot, st)
        best = None
        for (slot, st), pr in proposals.items():
            if pr is None or counts[slot] >= limits[slot]:
                continue
            if best is None or pr[0] > best[0]:
                best = (pr[0], slot, st, pr[1])
        if best is None or best[0] < min_gain * max(1, total0):
            break
        gain, slot, st, moved = best
        new = counts[slot]
        counts[slot] += 1
        parent[slot][new] = st
        moved = set(moved)
        for c in items[slot]:
            if c in moved:
                mapping[slot][c] = new
        proposals.pop((slot, st), None)
        history.append({"slot": ("initial", "medial", "final")[slot], "split": st, "new": new, "gain": gain})
        say(f"split {history[-1]}")
    moves = reassign(items, mapping, counts, by_item, P, jamo_of, npl)
    # items no present syllable falls in follow the nearest item with users that the 844 tables put in the same set
    for slot in range(3):
        used = [c for c in items[slot] if c in by_item[slot]]
        for c in items[slot]:
            if c in by_item[slot] or not used:
                continue
            same = [u for u in used if _844_of_item(slot, u) == _844_of_item(slot, c)] or used
            near = min(same, key=lambda u: (sum(1 for x, y in zip(u, c) if x != y), u))
            mapping[slot][c] = mapping[slot][near]
    end = [sum(cost_of(slot, members(slot, st)) for st in range(counts[slot])) for slot in range(3)]
    cell_map = [{c: mapping[k][item_of(k, c)] for c in cells} for k in range(3)]
    layout = make_layout(lead, tail, cell_map, counts, parent)
    info = {"lead_class": lead, "tail_class": tail, "sets": counts, "classes": [LC, TC], "splits": len(history),
            "items_moved": moves, "cost_start": start, "cost_end": end, "history": history}
    return layout, info


def _844_of_item(slot, item):
    if slot == 0:
        v, tc = item
        return _844_set(0, 1, v, tc)
    if slot == 1:
        lc, tc, v = item
        return _844_set(1, 0 if lc == 0 else 1, v, tc)
    v, lc = item
    return _844_set(2, 0 if lc == 0 else 1, v, 1)


def reassign(items, mapping, counts, by_item, P, jamo_of, npl, rounds=4):
    """Lloyd passes over the items with users: each moves to the set (of its slot) whose medians its donors are
    closest to (a jamo the set has no median for counts as it does in the item's own set). A 2-medians split can
    leave a few small items on the wrong side that no further split may move (each side keeps min_users). Returns
    the moves made."""
    moved = 0
    for slot in range(3):
        for _ in range(rounds):
            groups = {}
            for c, ss in by_item[slot].items():
                for s in ss:
                    groups.setdefault((mapping[slot][c], jamo_of[slot](s)), []).append(P[s][slot])
            med = {k: majority(v, npl) for k, v in groups.items()}
            changes = 0
            for c in items[slot]:
                ds = [(jamo_of[slot](s), P[s][slot]) for s in by_item[slot].get(c, ())]
                if not ds:
                    continue
                cur = mapping[slot][c]
                best = None
                for st in range(counts[slot]):
                    if not any((st, j) in med for j, _ in ds):
                        continue
                    e = sum(l1(d, med[(st, j)] if (st, j) in med else med[(cur, j)]) for j, d in ds)
                    if best is None or e < best[0] or (e == best[0] and st == cur):
                        best = (e, st)
                if best and best[1] != cur:
                    mapping[slot][c] = best[1]
                    changes += 1
            moved += changes
            if not changes:
                break
    return moved


def make_layout(lead, tail, mapping, counts, parent):
    """A jamoset.Layout from per-cell set mappings ({(lc, v, tc): set} per slot)."""
    NI, NM, NF = counts
    LC, TC = max(lead) + 1, max(tail) + 1
    lay = J.Layout("adaptive", None, LC, TC, NI, NM, NF,
                   lambda L: lead[L], lambda T: tail[T],
                   lambda lc, v, tc: mapping[0][(lc, v, tc)],
                   lambda lc, v, tc: mapping[1][(lc, v, tc)],
                   lambda lc, v, tc: mapping[2][(lc, v, tc)],
                   lambda s, L: s * 19 + L,
                   lambda s, V: NI * 19 + s * 21 + V,
                   lambda s, T: NI * 19 + NM * 21 + s * 27 + T - 1)
    lay.parent = parent

    def fallback(slot, st):
        """Sets to borrow an unused (set, jamo) piece from: the sets it was split from, nearest first."""
        out = []
        while st in parent[slot]:
            st = parent[slot][st]
            out.append(st)
        return out
    lay.fallback = fallback
    return lay
