#!/usr/bin/env python3
"""Build a UTF-8 SCVMTRS bundle (korean.trs) for an English SCUMM v4/v5 release
from existing Korean translations of that game.

Sources, in the order they are tried for every string of the English release:

  --trs FILE      a .trs made for another release of the game (CP949 or UTF-8),
                  e.g. the ScummVM Kor. Project's Monkey Island Ultimate Talkie
                  bundle. Its keys are that release's script strings: talkie
                  voice escapes (FF 0A xx xx) are dropped, variable escapes
                  (FF 04-07 xx xx) compared by code only and re-pointed at the
                  English release's variables, a line the English release
                  joins with FF 03 (wait) is looked up piece by piece, and
                  what is left is matched fuzzily within the same room
                  (ratio >= 0.9, same escapes and numbers).
  --patch DIR     an in-place EUC-KR patch of the same release (the 2005 DUMB
                  Monkey Island VGA floppy patch): both builds' scripts are
                  walked opcode by opcode (scummscript.py) and paired string
                  by string, block by block.
  (none)          the English string stays, and is left out of the bundle.

    scumm_trs_from_patch.py --english EN_DIR [--trs FILE] [--patch DIR]
                            -o korean.trs [--table provenance.tsv] [--report stats.txt]

The bundle is keyed the way ScummEngine::translateText() searches: the
original bytes exactly as the English script holds them, ranges per
(room, WIO_ROOM), (room, local script) and (0, global script), both orders
sorted by memcmp. Its body starts with the UTF-8 BOM (trs_bundle.h).

Legacy text is converted per character: KS X 1001 / CP949 Hangul, jamo and
symbols become UTF-8; SCUMM escapes and bytes that are the game's own glyphs
(MI1's 0xFA nonbreaking space, 0x88/0x82 for e-circumflex) are kept as raw
bytes, which a UTF-8 bundle draws with the game's font (kRawGameByteBase).
"""
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


# ---------------------------------------------------------------- .trs

def esc_len(b, i):
    return 2 if b[i + 1] in (1, 2, 3, 8) else 4


def res_str_len(d, p):
    n = p
    while d[n]:
        n += esc_len(d, n) if d[n] == 0xFF else 1
    return n - p


def read_trs(path):
    """[(ctx, original, translation)], ctx = (room, scriptKey) or None; and the UTF-8 flag."""
    d = open(path, 'rb').read()
    assert d[:8] == b'SCVMTRS ', path
    n = struct.unpack_from('<H', d, 8)[0]
    p = 10
    recs = []
    for _ in range(n):
        recs.append(struct.unpack_from('<HII', d, p))
        p += 10
    pos = [0] * n
    for i, (idx, _o, _t) in enumerate(recs):
        pos[idx] = i
    ctx = {}
    nroom = d[p]
    p += 1
    for _ in range(nroom):
        rid = d[p]
        ns = struct.unpack_from('<H', d, p + 1)[0]
        p += 3
        for _ in range(ns):
            key, left, right = struct.unpack_from('<IHH', d, p)
            p += 8
            for s in range(left, right + 1):
                ctx[pos[s]] = (rid, key)
    utf8 = d[p:p + 3] == BOM

    def cstr(o):
        return d[o:o + res_str_len(d, o)]
    return [(ctx.get(i), cstr(o), cstr(t)) for i, (_idx, o, t) in enumerate(recs)], utf8


def write_trs(groups):
    """groups: {(room, scriptKey): {original: translation}} -> bundle bytes."""
    order = sorted(groups)                              # index order: by room, then key
    index = []                                          # (original, translation, group)
    ranges = collections.OrderedDict()
    for g in order:
        left = len(index)
        for orig in sorted(groups[g]):
            index.append((orig, groups[g][orig]))
        ranges.setdefault(g[0], []).append((g[1], left, len(index) - 1))
    n = len(index)
    assert n < 0x10000 and len(ranges) < 256
    # file order: every entry sorted by original; idx = its position in index order
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
    assert len(out) == header
    return bytes(out + body)


_cache = {}


def read_trs_cached(data):
    if id(data) in _cache:
        return _cache[id(data)]
    n = struct.unpack_from('<H', data, 8)[0]
    p = 10
    lines = []
    lineindex = [0] * n
    for i in range(n):
        idx, o, t = struct.unpack_from('<HII', data, p)
        p += 10
        lineindex[idx] = i
        lines.append((o, t))
    rooms = {}
    nroom = data[p]
    p += 1
    for _ in range(nroom):
        rid = data[p]
        ns = struct.unpack_from('<H', data, p + 1)[0]
        p += 3
        for _ in range(ns):
            key, left, right = struct.unpack_from('<IHH', data, p)
            p += 8
            rooms.setdefault(rid, {})[key] = (left, right)

    def cstr(o):
        return data[o:o + res_str_len(data, o)]

    def chop(text, left, right, use_index):
        while left <= right:
            mid = (left + right) // 2
            i = lineindex[mid] if use_index else mid
            orig = cstr(lines[i][0])
            a, b = text + b'\0', orig + b'\0'
            m = min(len(a), len(b))
            if a[:m] == b[:m]:
                return cstr(lines[i][1])
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
                return t, 1
        if room:
            r = rooms.get(room, {}).get(WIO_ROOM << 16)
            if r:
                t = chop(text, r[0], r[1], True)
                if t is not None:
                    return t, 2
        t = chop(text, 0, n - 1, False)
        return (t, 3) if t is not None else (None, 0)
    _cache[id(data)] = (lookup, None)
    return _cache[id(data)]


# ---------------------------------------------------------------- text

def toks(b):
    """[('t', bytes)] text runs and [('e', code, args)] escapes."""
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
    """Drop talkie voice escapes (FF 0A xx xx)."""
    return join([t for t in toks(b) if not (t[0] == 'e' and t[1] == 0x0A)])


def masked(b):
    """Comparison key: no voice escapes, FF 04-07 arguments hidden, no trailing '@' padding."""
    out = []
    for t in toks(strip_voice(b)):
        out.append(('e', t[1], b'\0\0') if t[0] == 'e' and t[1] in (4, 5, 6, 7) else t)
    return join(out).rstrip(b'@')


def var_args(b):
    return [(t[1], t[2]) for t in toks(b) if t[0] == 'e' and t[1] in (4, 5, 6, 7)]


def remap(trans, src_key, dst_key, warn):
    """Point a translation's variable escapes at dst_key's variables."""
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
    """EUC-KR/CP949 game text -> UTF-8; escapes and the game's own glyph bytes stay raw."""
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
                    odd[ch] += 1   # a Hanja or unusual pair: its lead is taken as a game glyph
        out.append(c)              # the game's own character
        i += 1
    return bytes(out)


def utf8_ok(b):
    """Every high byte is either part of valid UTF-8 or a raw byte that cannot pair up."""
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


# ---------------------------------------------------------------- pairing

def ctx_key(r):
    if r.where == WIO_GLOBAL:
        return (0, (WIO_GLOBAL << 16) | (r.number & 0xFFFF))
    if r.where == WIO_LOCAL:
        return (r.room, (WIO_LOCAL << 16) | r.number)
    return (r.room, WIO_ROOM << 16)


def _shape(t):
    """What a translation keeps of a line: blank or not, escapes, '@' padding, '^' and '`' marks."""
    return (not t.strip(), tuple(x[1] for x in toks(t) if x[0] == 'e'),
            len(t) - len(t.rstrip(b'@')), t.count(b'^'), t.count(b'`'))


def _align(a, b):
    """Global alignment of two lists of shapes; [(i, j)] of the matched pairs."""
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
    """Pair two builds' strings block by block; returns ({en index: ko text}, [ko additions]).

    A block with as many strings on both sides pairs them in order (the
    patch kept the scripts' structure). Where the patch added strings -
    its own credits and chapter cards, blank prints - the two lists are
    aligned on what a translation keeps: blank or not, the escape codes,
    '@' padding, '^' and '`' marks."""
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
    """Look-ups into the other release's bundle."""

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
        """The same room's closest key, if it differs only in wording: same
        escapes, same numbers ("1 piece" is not "175 pieces"), ratio >= cutoff."""
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


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--english', required=True, help='the English release the bundle is for')
    ap.add_argument('--trs', help='a translation bundle made for another release (primary source)')
    ap.add_argument('--trs-data', help="that release's game data: check the bundle's keys against its scripts")
    ap.add_argument('--patch', help='an in-place patch of the English release (fallback source)')
    ap.add_argument('-o', '--output', required=True)
    ap.add_argument('--table', help='provenance table (TSV, UTF-8)')
    ap.add_argument('--report', help='statistics and examples (text)')
    ap.add_argument('--no-fuzzy', action='store_true')
    a = ap.parse_args()

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
            assert utf8_ok(trans), trans
            g = groups[ctx]
            if r.text in g and g[r.text] != trans:
                conflicts.append((r, g[r.text], trans))
                continue
            g[r.text] = trans

    data = write_trs(groups)
    open(a.output, 'wb').write(data)
    n_entries = sum(len(g) for g in groups.values())

    # every string of the English release must come back as the table says
    look, _ = read_trs_cached(data)
    bad = 0
    for r, prov, trans, _d, _n in rows:
        got, _h = look(r.text, ctx_key(r))
        if trans is not None and trans != r.text and got is None:
            bad += 1
    say('bundle: %s, %d bytes, %d entries in %d ranges over %d rooms; self-check misses %d'
        % (a.output, len(data), n_entries, len(groups), len(set(g[0] for g in groups)), bad))

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
        open(a.report, 'w', encoding='utf-8').write('\n'.join(rep) + '\n')
    if a.table:
        with open(a.table, 'w', encoding='utf-8') as f:
            f.write('room\twhere\tscript\tkind\tprovenance\tenglish\tkorean\tdumb\tnote\n')
            for r, prov, trans, dumb_u, note in rows:
                f.write('%d\t%d\t%d\t%s\t%s\t%s\t%s\t%s\t%s\n' % (
                    r.room, r.where, r.number, r.kind, prov, show(r.text, False),
                    show(trans) if trans is not None else '', show(dumb_u) if dumb_u else '', note))


if __name__ == '__main__':
    main()
