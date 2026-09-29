#!/usr/bin/env python3
"""scumm_trs_from_patch.py --english DIR [--trs FILE [--trs-data DIR]] [--patch DIR] -o korean.trs: see README."""
import argparse
import collections
import difflib
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scummscript  # noqa: E402

WIO_ROOM, WIO_GLOBAL, WIO_LOCAL = 1, 2, 3
BOM = b'\xef\xbb\xbf'


def esc_len(b, i):
    return 2 if b[i + 1] in (1, 2, 3, 8) else 4


def res_str_len(d, p, name='bundle'):
    n = p
    while n < len(d) and d[n]:
        n += esc_len(d, n) if d[n] == 0xFF and n + 1 < len(d) else 1
    if n >= len(d):
        raise SystemExit('%s: string at offset %d runs past the end of the file' % (name, p))
    return n - p


def parse_trs(d, name):
    if d[:8] != b'SCVMTRS ':
        raise SystemExit('%s: not a SCVMTRS bundle (magic %r)' % (name, bytes(d[:8])))

    def need(p, k):
        if p + k > len(d):
            raise SystemExit('%s: truncated at offset %d (header needs %d more bytes)' % (name, p, p + k - len(d)))
    need(8, 2)
    n = struct.unpack_from('<H', d, 8)[0]
    need(10, n * 10 + 1)
    recs = [struct.unpack_from('<HII', d, 10 + i * 10) for i in range(n)]
    if sorted(r[0] for r in recs) != list(range(n)):
        raise SystemExit('%s: line index is not a permutation of 0..%d' % (name, n - 1))
    p = 10 + n * 10
    rooms = {}
    nroom = d[p]
    p += 1
    for _ in range(nroom):
        need(p, 3)
        rid, ns = d[p], struct.unpack_from('<H', d, p + 1)[0]
        p += 3
        need(p, ns * 8)
        for _ in range(ns):
            key, left, right = struct.unpack_from('<IHH', d, p)
            p += 8
            if left <= right and right >= n:
                raise SystemExit('%s: range %d..%d of room %d is outside the %d lines' % (name, left, right, rid, n))
            rooms.setdefault(rid, {})[key] = (left, right)
    for _idx, o, t in recs:
        for off in (o, t):
            if not p <= off < len(d):
                raise SystemExit('%s: string offset %d outside the body (%d..%d)' % (name, off, p, len(d)))
            res_str_len(d, off, name)
    return recs, rooms, p


def read_trs(path):
    """[(ctx, original, translation)] with ctx = (room, scriptKey) or None, and the UTF-8 flag."""
    if not os.path.isfile(path):
        raise SystemExit('%s: no such file' % path)
    with open(path, 'rb') as f:
        d = f.read()
    recs, rooms, body = parse_trs(d, path)
    pos = [0] * len(recs)
    for i, (idx, _o, _t) in enumerate(recs):
        pos[idx] = i
    ctx = {}
    for rid, keys in rooms.items():
        for key, (left, right) in keys.items():
            for s in range(left, right + 1):
                ctx[pos[s]] = (rid, key)

    def cstr(o):
        return d[o:o + res_str_len(d, o, path)]
    return [(ctx.get(i), cstr(o), cstr(t)) for i, (_idx, o, t) in enumerate(recs)], d[body:body + 3] == BOM


def write_trs(groups):
    order = sorted(groups)
    index = []
    ranges = collections.OrderedDict()
    for g in order:
        left = len(index)
        for orig in sorted(groups[g]):
            index.append((orig, groups[g][orig]))
        ranges.setdefault(g[0], []).append((g[1], left, len(index) - 1))
    n = len(index)
    if n >= 0x10000:
        raise SystemExit('%d translated lines: a .trs holds at most 65535' % n)
    if len(ranges) >= 256 or any(room > 255 for room in ranges):
        raise SystemExit('%d rooms (highest %d): a .trs holds rooms 0..255, at most 255 of them'
                         % (len(ranges), max(ranges)))
    # translateText() binary-searches with memcmp: the file order and every range must be sorted.
    fileorder = sorted(range(n), key=lambda k: (index[k][0], k))
    header = 8 + 2 + n * 10 + 1 + sum(3 + 8 * len(v) for v in ranges.values())
    body = bytearray(BOM)
    offs = {}
    for k in fileorder:
        orig, trans = index[k]
        o1 = header + len(body)
        body += orig + b'\0'
        o2 = header + len(body)
        body += trans + b'\0'
        offs[k] = (o1, o2)
    out = bytearray(b'SCVMTRS ')
    out += struct.pack('<H', n)
    for k in fileorder:
        out += struct.pack('<HII', k, *offs[k])
    out.append(len(ranges))
    for room, scripts in ranges.items():
        out.append(room)
        out += struct.pack('<H', len(scripts))
        for key, left, right in scripts:
            out += struct.pack('<IHH', key, left, right)
    return bytes(out + body)


def trs_lookup(data, name='bundle'):
    """translateText(): the (room, script) range, then the room's WIO_ROOM range, then the whole file."""
    recs, rooms, _body = parse_trs(data, name)
    n = len(recs)
    lineindex = [0] * n
    for i, (idx, _o, _t) in enumerate(recs):
        lineindex[idx] = i

    def cstr(o):
        return data[o:o + res_str_len(data, o, name)]

    def chop(text, left, right, use_index):
        while left <= right:
            mid = (left + right) // 2
            i = lineindex[mid] if use_index else mid
            orig = cstr(recs[i][1])
            a, b = text + b'\0', orig + b'\0'
            m = min(len(a), len(b))
            if a[:m] == b[:m]:
                return cstr(recs[i][2])
            if a[:m] < b[:m]:
                right = mid - 1
            else:
                left = mid + 1
        return None

    def lookup(text, ctx):
        room, key = ctx
        r = rooms.get(room, {}).get(key)
        if r:
            t = chop(text, r[0], r[1], True)
            if t is not None:
                return t
        if room:
            r = rooms.get(room, {}).get(WIO_ROOM << 16)
            if r:
                t = chop(text, r[0], r[1], True)
                if t is not None:
                    return t
        return chop(text, 0, n - 1, False)
    return lookup


def toks(b):
    out, run, i = [], bytearray(), 0
    while i < len(b):
        if b[i] == 0xFF and i + 1 < len(b):
            if run:
                out.append(('t', bytes(run)))
                run = bytearray()
            n = esc_len(b, i)
            out.append(('e', b[i + 1], b[i + 2:i + n]))
            i += n
        else:
            run.append(b[i])
            i += 1
    if run:
        out.append(('t', bytes(run)))
    return out


def join(ts):
    out = bytearray()
    for t in ts:
        out += t[1] if t[0] == 't' else bytes([0xFF, t[1]]) + t[2]
    return bytes(out)


def strip_voice(b):
    return join([t for t in toks(b) if not (t[0] == 'e' and t[1] == 0x0A)])


def masked(b):
    # CD keys carry voice escapes and their own variable numbers; the floppy pads names with '@'.
    out = []
    for t in toks(strip_voice(b)):
        out.append(('e', t[1], b'\0\0') if t[0] == 'e' and t[1] in (4, 5, 6, 7) else t)
    return join(out).rstrip(b'@')


def var_args(b):
    return [(t[1], t[2]) for t in toks(b) if t[0] == 'e' and t[1] in (4, 5, 6, 7)]


def remap(trans, src_key, dst_key, warn):
    m = dict(zip(var_args(strip_voice(src_key)), var_args(dst_key)))
    by_code = collections.defaultdict(set)
    for (c, _a), dst in zip(var_args(strip_voice(src_key)), var_args(dst_key)):
        by_code[c].add(dst[1])
    out = []
    for t in toks(strip_voice(trans)):
        if t[0] == 'e' and t[1] in (4, 5, 6, 7):
            if (t[1], t[2]) in m:
                t = ('e', t[1], m[(t[1], t[2])][1])
            elif len(by_code[t[1]]) == 1:
                t = ('e', t[1], next(iter(by_code[t[1]])))
            else:
                warn.append(dst_key)
        out.append(t)
    return join(out)


def legacy_to_utf8(b, odd=None):
    out = bytearray()
    i = 0
    while i < len(b):
        c = b[i]
        if c == 0xFF and i + 1 < len(b):
            n = esc_len(b, i)
            out += b[i:i + n]
            i += n
            continue
        if c < 0x80:
            out.append(c)
            i += 1
            continue
        if i + 1 < len(b):
            pair = b[i:i + 2]
            try:
                ch = pair.decode('cp949')
            except UnicodeDecodeError:
                ch = None
            if ch and len(ch) == 1:
                cp = ord(ch)
                if 0xAC00 <= cp <= 0xD7A3 or 0x3131 <= cp <= 0x318E or (0xA1 <= c <= 0xAC and b[i + 1] >= 0xA1):
                    out += ch.encode('utf-8')
                    i += 2
                    continue
                if odd is not None:
                    odd[ch] += 1
        out.append(c)              # not Hangul: a raw byte, drawn with the game's own glyph (0xFA hard space)
        i += 1
    return bytes(out)


def utf8_ok(b):
    i = 0
    while i < len(b):
        c = b[i]
        if c == 0xFF and i + 1 < len(b) and b[i + 1] < 0x20:
            i += esc_len(b, i)
            continue
        if c < 0x80:
            i += 1
            continue
        n = 2 if c & 0xE0 == 0xC0 else 3 if c & 0xF0 == 0xE0 else 4 if c & 0xF8 == 0xF0 else 1
        if n > 1 and all(i + k < len(b) and b[i + k] & 0xC0 == 0x80 for k in range(1, n)):
            try:
                b[i:i + n].decode('utf-8')
            except UnicodeDecodeError:
                return False
            i += n
            continue
        i += 1
    return True


def show(b, utf8=True):
    s = []
    for t in toks(b):
        if t[0] == 'e':
            s.append('\\xff\\x%02x%s' % (t[1], ''.join('\\x%02x' % x for x in t[2])))
        else:
            s.append(t[1].decode('utf-8' if utf8 else 'latin-1', 'backslashreplace'))
    return ''.join(s).replace('\t', '\\t').replace('\n', '\\n')


HAS_WORD = re.compile(rb'[A-Za-z]')


def ctx_key(r):
    if r.where == WIO_GLOBAL:
        return (0, (WIO_GLOBAL << 16) | (r.number & 0xFFFF))
    if r.where == WIO_LOCAL:
        return (r.room, (WIO_LOCAL << 16) | r.number)
    return (r.room, WIO_ROOM << 16)


def _shape(t):
    return (not t.strip(), tuple(x[1] for x in toks(t) if x[0] == 'e'),
            len(t) - len(t.rstrip(b'@')), t.count(b'^'), t.count(b'`'))


def _align(a, b):
    def score(x, y):
        s = 2.0 if x[0] == y[0] else -2.0
        s += 1.0 if x[1] == y[1] else -0.5
        s += 0.5 if x[2] == y[2] else 0.0
        s += 0.25 * (x[3] == y[3]) + 0.25 * (x[4] == y[4])
        return s
    gap = -1.0
    n, m = len(a), len(b)
    dp = [[0.0] * (m + 1) for _ in range(n + 1)]
    for i in range(1, n + 1):
        dp[i][0] = i * gap
    for j in range(1, m + 1):
        dp[0][j] = j * gap
    for i in range(1, n + 1):
        for j in range(1, m + 1):
            dp[i][j] = max(dp[i - 1][j - 1] + score(a[i - 1], b[j - 1]), dp[i - 1][j] + gap, dp[i][j - 1] + gap)
    pairs = []
    i, j = n, m
    while i and j:
        if dp[i][j] == dp[i - 1][j - 1] + score(a[i - 1], b[j - 1]):
            pairs.append((i - 1, j - 1))
            i, j = i - 1, j - 1
        elif dp[i][j] == dp[i - 1][j] + gap:
            i -= 1
        else:
            j -= 1
    return pairs[::-1]


def pair_patch(en, ko):
    """({en index: patch text}, [patch-only records]); blocks the patch grew are aligned on _shape()."""
    def blocks(recs):
        b = collections.OrderedDict()
        for i, r in enumerate(recs):
            b.setdefault((r.disk, r.block), []).append(i)
        return b
    eb, kb = blocks(en), blocks(ko)
    pairs, added = {}, []
    for key in list(eb) + [k for k in kb if k not in eb]:
        ei, ki = eb.get(key, []), kb.get(key, [])
        ctx = lambda recs, ix: [(recs[i].room, recs[i].where, recs[i].number, recs[i].kind) for i in ix]
        if len(ei) == len(ki) and ctx(en, ei) == ctx(ko, ki):
            for a, b in zip(ei, ki):
                pairs[a] = ko[b].text
            continue
        matched = set()
        for a, b in _align([_shape(en[i].text) for i in ei], [_shape(ko[i].text) for i in ki]):
            if ctx(en, [ei[a]]) == ctx(ko, [ki[b]]):
                pairs[ei[a]] = ko[ki[b]].text
                matched.add(b)
        added += [ko[ki[b]] for b in range(len(ki)) if b not in matched]
    return pairs, added


class Ute:
    def __init__(self, recs, utf8):
        self.utf8 = utf8
        self.by_mask = collections.defaultdict(list)
        self.by_room = collections.defaultdict(list)
        for ctx, orig, trans in recs:
            if not trans or trans == orig:
                continue
            m = masked(orig)
            self.by_mask[m].append((ctx, orig, trans))
            if ctx:
                self.by_room[ctx[0]].append((m, ctx, orig, trans))

    @staticmethod
    def pick(cands, ctx):
        same = [c for c in cands if c[0] == ctx]
        if same:
            return same[0]
        room = [c for c in cands if c[0] and c[0][0] == ctx[0]]
        if room:
            return room[0]
        cnt = collections.Counter(c[2] for c in cands)
        best = cnt.most_common(1)[0][0]
        return [c for c in cands if c[2] == best][0]

    def exact(self, seg, ctx, warn):
        m = masked(seg)
        suffix = b''
        if m not in self.by_mask and m.endswith(b'\xff\x02'):
            m, suffix = m[:-2], b'\xff\x02'
        if m not in self.by_mask:
            return None
        c = self.pick(self.by_mask[m], ctx)
        pad = len(seg) - len(seg.rstrip(b'@'))
        return remap(c[2], c[1], seg, warn).rstrip(b'@') + suffix + b'@' * pad

    def lookup(self, text, ctx, warn):
        t = self.exact(text, ctx, warn)
        if t is not None:
            return t, 'ute'
        segs = text.split(b'\xff\x03')
        if len(segs) > 1:
            parts = [self.exact(s, ctx, warn) for s in segs]
            if all(p is not None for p in parts):
                return b'\xff\x03'.join(parts), 'ute-split'
        return None, None

    def fuzzy(self, text, ctx, warn, cutoff=0.9):
        # same escapes and numbers: "1 piece of eight" must not take "175 pieces of eight"
        m = masked(text)
        codes = [t[1] for t in toks(m) if t[0] == 'e']
        digits = re.findall(rb'[0-9]+', m)
        best, score = None, cutoff
        for om, octx, orig, trans in self.by_room.get(ctx[0], []):
            if [t[1] for t in toks(om) if t[0] == 'e'] != codes or re.findall(rb'[0-9]+', om) != digits:
                continue
            r = difflib.SequenceMatcher(None, m, om, autojunk=False).ratio()
            if r > score or (r == score and best and octx == ctx):
                best, score = (orig, trans), r
        if not best:
            return None, None, 0
        return remap(best[1], best[0], text, warn), best[0], score


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--english', required=True, help='the English release the bundle is for')
    ap.add_argument('--trs', help='a translation bundle made for another release (primary source)')
    ap.add_argument('--trs-data', help="that release's game data: check the bundle's keys against its scripts")
    ap.add_argument('--patch', help='an in-place patch of the English release (fallback source)')
    ap.add_argument('-o', '--output', required=True)
    ap.add_argument('--table', help='provenance table (TSV, UTF-8)')
    ap.add_argument('--report', help='statistics and examples (text)')
    ap.add_argument('--no-fuzzy', action='store_true', help='no fuzzy matching against the other release')
    a = ap.parse_args()
    for opt, path, isdir in (('--english', a.english, True), ('--patch', a.patch, True),
                             ('--trs-data', a.trs_data, True), ('--trs', a.trs, False)):
        if path and not (os.path.isdir(path) if isdir else os.path.isfile(path)):
            raise SystemExit('%s %s: no such %s' % (opt, path, 'folder' if isdir else 'file'))

    rep = []

    def say(s=''):
        rep.append(s)
        print(s)

    en = scummscript.game_strings(a.english)
    say('English strings: %d in %s' % (len(en), a.english))

    dumb, added = {}, []
    if a.patch:
        ko = scummscript.game_strings(a.patch)
        dumb, added = pair_patch(en, ko)
        say('patch strings: %d, paired %d, only in the patch %d' % (len(ko), len(dumb), len(added)))

    ute = None
    if a.trs:
        recs, utf8 = read_trs(a.trs)
        ute = Ute(recs, utf8)
        say('bundle entries: %d (%s)' % (len(recs), 'UTF-8' if utf8 else 'CP949'))
        if a.trs_data:
            other = scummscript.game_strings(a.trs_data)
            have = set(r.text for r in other)
            keys = set(o for _c, o, _t in recs)
            say("bundle keys found in that release's scripts: %d of %d; its scripts' strings with no key: %d"
                % (len(keys & have), len(keys), len(set(t for t in have if t.strip()) - keys)))

    odd = collections.Counter()
    warn = []
    groups = collections.defaultdict(dict)
    rows = []
    seen = {}
    prov_rec = collections.Counter()
    conflicts = []
    diffs = []
    fuzzy_list = []
    left_english = []
    none_list = []
    for i, r in enumerate(en):
        ctx = ctx_key(r)
        k = (ctx, r.text)
        if k in seen:
            prov_rec[seen[k]] += 1
            continue
        d = dumb.get(i)
        dumb_u = legacy_to_utf8(d, odd) if d is not None and d != r.text else None
        trans, prov, note = None, 'none', ''
        if not HAS_WORD.search(r.text) and dumb_u is None:
            prov = 'n/a'
        else:
            if ute and HAS_WORD.search(r.text):
                t, how = ute.lookup(r.text, ctx, warn)
                if t is None and not a.no_fuzzy:
                    t, src, score = ute.fuzzy(r.text, ctx, warn)
                    if t is not None:
                        how = 'ute-fuzzy'
                        note = '%.2f %s' % (score, show(strip_voice(src), False))
                        fuzzy_list.append((r, src, score))
                if t is not None:
                    trans = t if ute.utf8 else legacy_to_utf8(t, odd)
                    prov = how
            if trans is None and dumb_u is not None:
                trans, prov = dumb_u, 'dumb'
            if trans is None:
                prov = 'none'
                if d is not None and d == r.text:
                    left_english.append(r)
                else:
                    none_list.append(r)
        if trans is not None and prov.startswith('ute') and dumb_u is not None and dumb_u != trans:
            diffs.append((r, trans, dumb_u))
        seen[k] = prov
        prov_rec[prov] += 1
        rows.append((r, prov, trans, dumb_u, note))
        if trans is not None and trans != r.text:
            if not utf8_ok(trans):
                raise SystemExit('room %d script %d: translation of %s is not valid UTF-8 plus game bytes: %r'
                                 % (r.room, r.number, show(r.text, False), trans))
            g = groups[ctx]
            if r.text in g and g[r.text] != trans:
                conflicts.append((r, g[r.text], trans))
                continue
            g[r.text] = trans

    data = write_trs(groups)
    with open(a.output, 'wb') as f:
        f.write(data)
    n_entries = sum(len(g) for g in groups.values())

    look = trs_lookup(data, a.output)
    bad = []
    for r, prov, trans, _d, _n in rows:
        want = trans if trans is not None and trans != r.text else None
        got = look(r.text, ctx_key(r))
        if got != want:
            bad.append((r, prov, want, got))
    say('bundle: %s, %d bytes, %d entries in %d ranges over %d rooms; self-check mismatches %d of %d'
        % (a.output, len(data), n_entries, len(groups), len(set(g[0] for g in groups)), len(bad), len(rows)))
    for r, prov, want, got in bad[:20]:
        say('  %d/%d/%d %s [%s] %s: want %s, engine finds %s' % (
            r.room, r.where, r.number, r.kind, prov, show(r.text, False),
            show(want) if want is not None else '(English)', show(got) if got is not None else '(English)'))
    say('same English, two translations in one range (first kept): %d' % len(conflicts))
    for r, first, other in conflicts[:20]:
        say('  %d/%d/%d %s: %s | %s' % (r.room, r.where, r.number, show(r.text, False), show(first), show(other)))

    dist = collections.Counter(p for _r, p, _t, _d, _n in rows)
    say('')
    say('provenance, distinct (context, English) strings: %d' % len(rows))
    for p in ('ute', 'ute-split', 'ute-fuzzy', 'dumb', 'none', 'n/a'):
        say('  %-10s %5d' % (p, dist[p]))
    say('provenance, every string occurrence: %d' % sum(prov_rec.values()))
    for p in ('ute', 'ute-split', 'ute-fuzzy', 'dumb', 'none', 'n/a'):
        say('  %-10s %5d' % (p, prov_rec[p]))
    say('variable escapes that could not be re-pointed: %d' % len(warn))
    if odd:
        say('legacy pairs kept as raw game bytes (not Hangul/jamo/symbols): %s'
            % ' '.join('%s x%d' % kv for kv in odd.most_common(20)))
    say('')
    say('left English by the patch (paired, unchanged, has letters) and not in the bundle: %d'
        % len(left_english))
    for r in left_english[:40]:
        say('  %d/%d/%d %s  %s' % (r.room, r.where, r.number, r.kind, show(r.text, False)))
    say('no translation anywhere (no patch pair): %d' % len(none_list))
    for r in none_list[:40]:
        say('  %d/%d/%d %s  %s' % (r.room, r.where, r.number, r.kind, show(r.text, False)))
    if a.patch:
        say('patch strings with no English counterpart (additions, not carried): %d' % len(added))
        for r in added[:40]:
            say('  %d/%d/%d %s  %s' % (r.room, r.where, r.number, r.kind, show(legacy_to_utf8(r.text))))
    say('')
    say('fuzzy matches (English release vs the bundle\'s key): %d' % len(fuzzy_list))
    for r, src, score in fuzzy_list[:60]:
        say('  %.2f  %s' % (score, show(r.text, False)))
        say('        %s' % show(strip_voice(src), False))
    say('')
    longer = sum(1 for _r, t, dd in diffs if len(t.decode('utf-8', 'replace')) > len(dd.decode('utf-8', 'replace')))
    say('bundle and patch both translate, and differ: %d (bundle longer in %d)' % (len(diffs), longer))
    for r, t, dd in diffs[:25]:
        say('  EN   %s' % show(r.text, False))
        say('  UTE  %s' % show(t))
        say('  DUMB %s' % show(dd))
    if a.report:
        with open(a.report, 'w', encoding='utf-8') as f:
            f.write('\n'.join(rep) + '\n')
    if a.table:
        with open(a.table, 'w', encoding='utf-8') as f:
            f.write('room\twhere\tscript\tkind\tprovenance\tenglish\tkorean\tdumb\tnote\n')
            for r, prov, trans, dumb_u, note in rows:
                if any(c[0] is r for c in conflicts):
                    note = (note + ' ' if note else '') + 'conflict: first translation in this range kept'
                f.write('%d\t%d\t%d\t%s\t%s\t%s\t%s\t%s\t%s\n' % (
                    r.room, r.where, r.number, r.kind, prov, show(r.text, False),
                    show(trans) if trans is not None else '', show(dumb_u) if dumb_u else '', note))
    if bad:
        raise SystemExit('%d English strings do not come back from the bundle as the table says' % len(bad))


if __name__ == '__main__':
    main()
