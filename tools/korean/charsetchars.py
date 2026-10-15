#!/usr/bin/env python3
"""charsetchars.py GAMEDIR [--v5 NAME] [--trs korean.trs] [--validate LOG...] [-o FILE]: the characters each SCUMM
charset draws, found by walking the game's scripts (see README, "Characters each charset draws").

Output: {"cs1": "...", "cs2": "...", "unknown": "..."} on stdout (or -o): each value the sorted characters, as
translated through the bundle (or, with no bundle, the script's own CP949 text), of every string the scripts draw
with that charset. "unknown" holds the strings whose charset a script picks through a variable; they are also in
every charset the game uses.
"""
import argparse
import collections
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import scummscript as S  # noqa: E402
import scumm_trs_from_patch as T  # noqa: E402

ANY = -1                # a charset picked through a variable
ENTRY = 'E'             # the charset the script was entered with
START_OPS = (0x0A, 0x2A, 0x4A, 0x6A, 0x8A, 0xAA, 0xCA, 0xEA)
CHAIN_OPS = (0x42, 0xC2)
STOP_OPS = (0x00, 0xA0)
SENTENCE_CHARSET = 1    # drawSentence(): _string[2].charset = 1
LOCAL_FROM = 200        # script numbers from here are room-local (numGlobalScripts)
CALLABLE = ('SCRP', 'SC', 'LSCR', 'LS')
ENTRY_TAGS = ('ENCD', 'EN')
BOOT_SCRIPT = 1
NAME_KINDS = ('obname', 'objname', 'actorname')


class Instr:
    def __init__(self, at, end, op):
        self.at, self.end, self.op = at, end, op
        self.jumps, self.charsets, self.verbnew, self.texts = [], [], 0, []
        self.call = None        # ('start'|'chain', script number or None for a variable)
        self.verb = None        # the verbOps verb operand (is_var, id)
        self.override = False   # beginOverride: the jump behind it is the ESC handler, not taken in the normal flow


class Flow(S.Walker):
    """Decodes a block one instruction at a time, keeping what the analysis needs."""

    def __init__(self, *a):
        super().__init__(*a)
        self.instrs = []
        self.depth = 0

    def op(self):
        if self.depth:
            return super().op()
        at, n = self.p, len(self.strings)
        self.jumps.clear()
        nv = len(self.verb_ids)
        self.charsets.clear()
        self.verbnews = 0
        self.depth = 1
        try:
            super().op()
        finally:
            self.depth = 0
        d = self.d
        ins = Instr(at, self.p, d[at])
        ins.jumps, ins.charsets, ins.verbnew = list(self.jumps), list(self.charsets), self.verbnews
        ins.texts = [(pos, kind, s, self.verb_ids[-1] if kind == 'verb' and len(self.verb_ids) > nv
                      else self.string_ids.get(pos)) for pos, kind, s in self.strings[n:]]
        if ins.op in START_OPS or ins.op in CHAIN_OPS:
            ins.call = ('start' if ins.op in START_OPS else 'chain', None if ins.op & 0x80 else d[at + 1])
        ins.verb = self.verb_ids[-1] if len(self.verb_ids) > nv else None
        ins.override = ins.op == 0x58 and d[at + 1] != 0
        self.instrs.append(ins)


def decode(unit):
    w = Flow(unit.d, unit.start, unit.end, unit.version)
    w.run()
    return w.instrs


def succs(ins, nxt):
    """Instruction offsets that can follow ins; None stands for leaving the block."""
    if ins.op in STOP_OPS or ins.call and ins.call[0] == 'chain':
        return []
    if ins.op == 0x18:
        return list(ins.jumps)
    return list(ins.jumps) + [nxt]


def callee_key(unit, n):
    return (0, S.WIO_GLOBAL, n) if n < LOCAL_FROM else (unit.ctx[0], S.WIO_LOCAL, n)


def flow(unit, instrs, effects):
    """One block, with ENTRY standing for the charset it is entered with.

    Returns (states, calls, exits, verbnew): states[text offset] = set of charsets, calls[key] = states at the calls
    to that script, exits = states where the block stops, verbnew[verb id or None for a variable] = states at the
    "new verb" sub-ops."""
    by_at = {i.at: i for i in instrs}
    order = [i.at for i in instrs] + [unit.end]
    nxt = dict(zip(order, order[1:]))
    IN = {}
    work = []

    def push(at, st):
        if at not in by_at:
            return
        old = IN.get(at)
        if old is None or not st <= old:
            IN[at] = (old or set()) | st
            work.append(at)
    for r in unit.roots:
        push(r, {ENTRY})
    states, calls, exits = collections.defaultdict(set), collections.defaultdict(set), set()
    verbnew = collections.defaultdict(set)
    handlers = []       # (first offset of the cutscene body, ESC handler offset)
    for k, ins in enumerate(instrs):
        if ins.override and k + 1 < len(instrs) and instrs[k + 1].op == 0x18 and instrs[k + 1].jumps:
            handlers.append((instrs[k + 1].end, instrs[k + 1].jumps[0]))
    while True:
        while work:
            at = work.pop()
            ins = by_at[at]
            st = set(IN[at])
            for pos, _kind, _s, _sid in ins.texts:
                states[pos] |= st
            if ins.verbnew:
                verbnew[None if ins.verb[0] else ins.verb[1]] |= st
            for isvar, val in ins.charsets:
                st = {ANY} if isvar else {val}
            if ins.call and ins.call[1] is not None:
                key = callee_key(unit, ins.call[1])
                calls[key] |= st
                st = st | effects.get(key, set())
            if ins.override:
                # the jump behind it is skipped; ESC lands on it, from anywhere in the cutscene body
                out = [nxt[nxt[at]]] if nxt[at] in by_at and nxt[at] != unit.end else []
                if nxt[at] in by_at:
                    push(nxt[at], st)
            else:
                out = succs(ins, nxt[at])
            if not out or nxt[at] == unit.end and nxt[at] in out:
                exits |= st
            for o in out:
                if o != unit.end:
                    push(o, st)
        for body, target in handlers:
            seen = set()
            for at in order:
                if body <= at < target and at in IN:
                    seen |= IN[at]
            if seen:
                push(target, seen)
        if not work:
            break
    return states, calls, exits, verbnew


def resolve(st, entry):
    return (st - {ENTRY}) | (entry if ENTRY in st else set())


Analysis = collections.namedtuple('Analysis', 'texts verbnew rest charsets verbcs')


def analyze(units):
    """texts: [(unit, kind, bytes, set of charsets)]; verbnew: charsets verbs are created with (verbcs: per verb id, None for a variable id); rest: the charsets
    scripts leave set; charsets: every one the game sets.

    A block is entered with the charset its caller had, or, for a block nobody calls, with the one the game rests
    in (what the boot script, global script 1, leaves) or, in a room, what the room's entry script leaves."""
    work = [(u, None if u.tag == 'obname' else decode(u)) for u in units]
    effects = collections.defaultdict(set)
    res = {}
    while True:
        changed = False
        for u, instrs in work:
            if instrs is None:
                continue
            r = flow(u, instrs, effects)
            res[id(u)] = r
            if u.tag in CALLABLE:
                consts = {c for c in r[2] if c != ENTRY}
                if not consts <= effects[u.ctx]:
                    effects[u.ctx] |= consts
                    changed = True
        if not changed:
            break
    setters = {v for _u, ins in work if ins for i in ins for isvar, v in i.charsets if not isvar}
    calls_in = collections.defaultdict(set)
    entries = {}
    # The boot script's own settings: what it starts (the intro) is not the state the game rests in.
    rest = set()
    for u, instrs in work:
        if instrs is not None and u.ctx == (0, S.WIO_GLOBAL, BOOT_SCRIPT):
            rest |= {c for c in flow(u, instrs, {})[2] if c not in (ENTRY, ANY)}
    rest = rest or {0}
    room_rest = {}
    while True:
        changed = False
        for u, instrs in work:
            if instrs is None:
                continue
            if u.tag in ENTRY_TAGS:
                entry = rest | calls_in[u.ctx]
                left = {c for c in res[id(u)][2] if c not in (ENTRY, ANY)}
                room_rest[u.ctx[0]] = left | (entry if ENTRY in res[id(u)][2] else set())
        for u, instrs in work:
            if instrs is None:
                continue
            entry = (room_rest.get(u.ctx[0], rest) if u.ctx[0] and u.tag not in ENTRY_TAGS else rest) | calls_in[u.ctx]
            entries[id(u)] = entry
            for key, st in res[id(u)][1].items():
                st = resolve(st, entry)
                if not st <= calls_in[key]:
                    calls_in[key] |= st
                    changed = True
        if not changed:
            break
    texts, verbnew, verbcs = [], set(), collections.defaultdict(set)
    for u, instrs in work:
        if instrs is None:
            texts.append((u, 'obname', u.d[u.start:u.end], set(), None))
            continue
        entry = entries[id(u)]
        states, _calls, _exits, vn = res[id(u)]
        for vid, st in vn.items():
            verbcs[vid] |= resolve(st, entry)
            verbnew |= resolve(st, entry)
        for ins in instrs:
            for pos, kind, s, sid in ins.texts:
                texts.append((u, kind, s, resolve(states[pos], entry), sid))
    return Analysis(texts, verbnew, rest | set().union(*room_rest.values()) if room_rest else rest, setters | rest, verbcs)


# ---- characters --------------------------------------------------------------------------------------------------

def cp949_units(b):
    """The engine's CP949 reading (charLength): a lead byte 0x81..0xFE always takes the next byte with it."""
    i = 0
    while i < len(b):
        if 0x81 <= b[i] <= 0xFE and i + 1 < len(b):
            yield b[i:i + 2].decode('cp949', 'replace')[:1]
            i += 2
        else:
            yield b[i:i + 1].decode('cp949', 'replace')
            i += 1


def text_chars(b, utf8):
    """The characters a string draws: its text runs, no FF escapes, no control codes."""
    out = set()
    for t in T.toks(b):
        if t[0] == 't':
            for c in (t[1].decode('utf-8', 'replace') if utf8 else cp949_units(t[1])):
                if c >= ' ' and c != '\ufffd' and c != '\x7f':
                    out.add(c)
    return out


def escapes(b):
    return {t[1] for t in T.toks(b) if t[0] == 'e'}


class Bundle:
    def __init__(self, path):
        self.path = path
        with open(path, 'rb') as f:
            data = f.read()
        self.look = T.trs_lookup(data, path)
        ents, self.utf8 = T.read_trs(path)
        self.all = collections.defaultdict(list)
        for _ctx, o, t in ents:
            self.all[o].append(t)

    def line(self, unit, kind, b):
        """The engine's lookup: the script's range, the room's, then the whole file."""
        t = self.look(b, T.ctx_key(S.Rec(unit.ctx[0], unit.ctx[1], unit.ctx[2], kind, b, 0, 0, 0)))
        return [t] if t is not None else []

    def every(self, b):
        return self.all.get(b, [])


def verb_charsets(analysis, sid):
    """The charsets a verb is created with; every verb's when its id is not a constant or never created."""
    if sid is not None and not sid[0] and sid[1] in analysis.verbcs:
        return set(analysis.verbcs[sid[1]]) | analysis.verbcs.get(None, set())
    return set(analysis.verbnew)


def decorate(analysis, bundle, sentence=SENTENCE_CHARSET):
    """-> (charsets: {n: set of chars}, unknown: set of chars, used: sorted charset numbers)."""
    used = sorted(analysis.charsets | {sentence})
    sets = {c: set() for c in used}
    unknown = set()
    users = collections.defaultdict(set)         # FF escape code -> charsets of the prints that carry it
    strings = collections.defaultdict(set)       # string variable -> charsets of the prints that carry FF 07 <it>

    def lines(unit, kind, b):
        if bundle is None:
            return [(b, False)]
        got = bundle.every(b) if kind in NAME_KINDS or kind == 'verb' else bundle.line(unit, kind, b)
        return [(g, bundle.utf8) for g in got] or [(b, True)]

    pending = []
    for unit, kind, b, cs, sid in analysis.texts:
        ls = lines(unit, kind, b)
        chars, esc, refs = set(), set(), set()
        for g, u8 in ls:
            chars |= text_chars(g, u8)
        for g in [b] + [g for g, _ in ls]:
            esc |= escapes(g)
            refs |= {struct.unpack('<H', t[2][:2])[0] for t in T.toks(g) if t[0] == 'e' and t[1] == 7 and len(t[2]) >= 2}
        if ANY in cs:
            target = set(used)
            unknown |= chars
        elif kind in ('print', 'printEgo', 'savestring'):
            target = set(cs)
        elif kind == 'string':
            target = set(cs)        # kept if nothing refers to the string
        elif kind == 'verb':
            target = verb_charsets(analysis, sid) | {sentence}      # and the sentence line, which shows the verb
        else:
            target = {sentence}     # names: the sentence line, and below the verbs that show names
        target.discard(ANY)
        if kind in ('print', 'printEgo'):
            for e in esc:
                users[e] |= target
            for r in refs:
                strings[r] |= target
        pending.append((kind, chars, target, sid, esc))
    shows_names = set()                          # charsets of the verbs whose text carries a name (inventory)
    for kind, chars, target, sid, esc in pending:
        if kind == 'verb' and 6 in esc:
            shows_names |= target
    for kind, chars, target, sid, esc in pending:
        if kind in NAME_KINDS:
            target |= users[6] | shows_names
        elif kind == 'verb':
            target |= users[5]
        elif kind == 'string' and sid is not None and not sid[0] and strings.get(sid[1]):
            target = set(strings[sid[1]])
        for c in target:
            sets.setdefault(c, set()).update(chars)
    return sets, unknown, used


def to_json(sets, unknown):
    out = {'cs%d' % c: ''.join(sorted(s)) for c, s in sorted(sets.items())}
    out['unknown'] = ''.join(sorted(unknown))
    return out


def is_syllable(c):
    return '가' <= c <= '힣'


def sizes(js):
    return {k: (sum(map(is_syllable, v)), len(v)) for k, v in js.items()}


# ---- validation ---------------------------------------------------------------------------------------------------

LOG_START = re.compile(r'^SCUMM text: loop (\d+), charset (\d+), ', re.M)


def read_log(path):
    """[(charset, text)] from a textlog json (a list of {charset, text}) or a run.log's "SCUMM text:" lines."""
    with open(path, encoding='utf-8', errors='replace') as f:
        raw = f.read()
    if raw.lstrip().startswith('['):
        return [(int(e['charset']), e['text']) for e in json.loads(raw)]
    out = []
    starts = list(LOG_START.finditer(raw))
    for i, m in enumerate(starts):
        end = starts[i + 1].start() if i + 1 < len(starts) else len(raw)
        chunk = raw[m.end():end]
        k = chunk.find(", text '")
        if k < 0:
            continue
        body = chunk[k + len(", text '"):]
        e = body.find("'\n")
        body = body[:e] if e >= 0 else body.rstrip('\n').rstrip("'")
        out.append((int(m.group(2)), body))
    return out


def validate(sets, entries, ascii_too=False):
    """-> (lines checked, misses [(charset, text, missing chars)]) over the distinct (charset, text) pairs."""
    seen = set(entries)
    misses = []
    for cs, text in sorted(seen):
        have = sets.get(cs, set())
        gone = sorted({c for c in text if c >= ' ' and c != '\ufffd' and (ascii_too or c > '\x7f') and c not in have})
        if gone:
            misses.append((cs, text, ''.join(gone)))
    return len(seen), misses


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('gamedir')
    ap.add_argument('--v5', metavar='NAME', help='v5 file name stem (monkey for monkey.000)')
    ap.add_argument('--trs', help='the translation bundle (default: none, the script text is Korean already)')
    ap.add_argument('--validate', nargs='+', metavar='LOG', help='engine text logs to check against the sets')
    ap.add_argument('--ascii', action='store_true', help='validate ASCII characters too')
    ap.add_argument('--sentence-charset', type=int, default=SENTENCE_CHARSET)
    ap.add_argument('-o', '--out', help='write the JSON here')
    a = ap.parse_args()
    units = list(S.units(a.gamedir, a.v5))
    an = analyze(units)
    bundle = Bundle(a.trs) if a.trs else None
    sets, unknown, used = decorate(an, bundle, a.sentence_charset)
    js = to_json(sets, unknown)
    text = json.dumps(js, ensure_ascii=False, indent=1) + '\n'
    if a.out:
        open(a.out, 'w', encoding='utf-8').write(text)
    else:
        sys.stdout.write(text)
    print('# charsets used: %s; left set by scripts: %s; verbs created with: %s' % (
        used, sorted(an.rest), sorted(an.verbnew)), file=sys.stderr)
    for k, (syl, n) in sizes(js).items():
        print('# %-8s %5d syllables %5d chars' % (k, syl, n), file=sys.stderr)
    if a.validate:
        every = set()
        for path in a.validate:
            entries = read_log(path)
            every |= set(entries)
            n, misses = validate(sets, entries, a.ascii)
            print('# %s: %d distinct lines, %d with chars outside their charset (%.1f%% covered)' % (
                path, n, len(misses), 100.0 * (n - len(misses)) / max(n, 1)), file=sys.stderr)
            for cs, t, gone in misses:
                print('#   cs%d missing %s in %r' % (cs, gone, t[:60]), file=sys.stderr)
        n, misses = validate(sets, every, a.ascii)
        print('# all logs: %d distinct (charset, text) lines, %d misses, %.1f%% covered' % (
            n, len(misses), 100.0 * (n - len(misses)) / max(n, 1)), file=sys.stderr)


if __name__ == '__main__':
    main()
