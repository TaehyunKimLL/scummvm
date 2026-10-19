#!/usr/bin/env python3
"""facecoverage.py: do a hi-res map's faces hold every character a translation bundle draws?

    facecoverage.py --trs korean.trs [--trs ...] --map M2KO.MAP [--faces DIR] [--preset U|L|both]
                    [--data DIR ...] [--base-check]

For every font id ([font.N]) of every preset, the id's face chain (face=a,b through [fonts]) must
cover each character of the bundle's text plus the code points the map draws in place of a byte
([glyphs] 0x5e = u+2026) and its missing= box. A chain covers a character when one of its faces has
it: a bitmap face (SVF) in its code point table, or - flag bit 1 - by composing the syllable from its
jamo pieces; a TrueType face in its cmap. Nothing falls back to the game's own font here: a character
only the game's glyph draws is a character the face lacks.

--base-check also requires the first face of every chain to be a base face: ASCII 0x20-0x7E and all
2,350 KS X 1001 syllables, whatever the game text uses (a jamo set composes all of them). What the
text needs beyond that belongs to the supplement face chained after the base.

Prints the missing characters per preset and font id; exit status 1 if there are any.

--hot DIR adds an informational report (never an error: a miss is a disk read, not a box): for every face
file of the map that has a preload list DIR/<STEM>.HOT (charsetchars.py --hot-dir), how many characters the
list has, how many of them the face holds, and how many characters of the bundle's text the face holds but
the list leaves out. The last number counts the whole bundle against every face, so for a face only one
charset draws with it is an upper bound; it is the number to compare between two lists of one face.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
# jamoset, mkfont: the font tools of the svf-fonts submodule
sys.path.insert(1, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), 'dists', 'svf-fonts', 'tools'))
import jamoset  # noqa: E402
import mkfont  # noqa: E402


def parse_map(text):
    """{section name (lower case, qualifiers kept): [(key, value)]} in file order, ';' comments cut."""
    sections = {}
    cur = None
    for raw in text.splitlines():
        line = mkfont._strip_ini_comment(raw).strip()
        if not line or line[0] in '#;':
            continue
        if line[0] == '[' and line[-1] == ']':
            cur = sections.setdefault(line[1:-1].strip().lower(), [])
            continue
        if cur is None or '=' not in line:
            continue
        k, _, v = line.partition('=')
        cur.append((k.strip().lower(), v.strip()))
    return sections


def section(sections, name, preset):
    """The [name] keys with [name:clut8] merged over them for the L preset."""
    out = dict(sections.get(name, []))
    if preset == 'L':
        out.update(dict(sections.get(name + ':clut8', [])))
    return out


def plan(sections, preset):
    """({font id: [face names]}, {font id: {cp}} the targets the map draws, the missing= cp or None)."""
    fonts = section(sections, 'fonts', preset)
    ids = sorted(int(n.split('.')[1]) for n in sections if n.startswith('font.') and n.split('.')[1].isdigit())
    base = section(sections, 'font', preset)
    chains = {}
    targets = {}
    glyphs = section(sections, 'glyphs', preset)
    miss = base.get('missing')
    missing = mkfont._parse_code_value(miss) if miss else None
    for i in ids:
        own = section(sections, 'font.%d' % i, preset)
        face = own.get('face', base.get('face', ''))
        names = [n.strip() for n in face.split(',') if n.strip()]
        chains[i] = [fonts.get(n, n) for n in names]
        g = dict(glyphs)
        g.update(section(sections, 'glyphs.%d' % i, preset))
        t = set()
        for v in g.values():
            if v.lower() in ('keep', 'original') or v.startswith('+') or ':' in v:
                continue
            cp = mkfont._parse_code_value(v)
            if cp is not None:
                t.add(cp)
        if own.get('missing'):
            m = mkfont._parse_code_value(own['missing'])
            if m is not None:
                t.add(m)
        elif missing is not None:
            t.add(missing)
        targets[i] = t
    return chains, targets


def svf_chars(data):
    """The code points an SVF draws: its table, and every syllable its jamo set composes."""
    s = jamoset.parse_svfn(data)
    cps = {c for c in s.glyphs if c < jamoset.PIECE_BASE}
    if s.flags & jamoset.FLAG_JAMO and s.jset:
        t = jamoset.unpack(s.jset)
        for cp in range(0xAC00, 0xD7A4):
            nums = jamoset.pieces_of(t, cp)
            lead = (cp - 0xAC00) // 588
            drawn = [n != jamoset.BLANK and jamoset.PIECE_BASE + n in s.glyphs for n in nums]
            # the initial of ㅇ (index 11) has no ink; the medial and any final must
            if drawn[1] and (drawn[0] or lead == 11) and all(drawn[2:]):
                cps.add(cp)
    return cps


_cache = {}


def face_chars(path):
    """The code points of a face file (SVF or TrueType/OpenType), cached."""
    if path not in _cache:
        with open(path, 'rb') as f:
            data = f.read()
        if data[:4] == b'SVFN':
            _cache[path] = svf_chars(data)
        else:
            from fontTools.ttLib import TTFont
            _cache[path] = set(TTFont(path, lazy=True).getBestCmap())
    return _cache[path]


def resolve(value, faces, data_dirs):
    """The file a [fonts] value names, or None. data:NAME is looked up in the data dirs, else beside the map."""
    if value.lower().startswith('data:'):
        name = value[5:]
        for d in data_dirs + [faces]:
            for cand in (os.path.join(d, name), os.path.join(d, os.path.basename(name))):
                if os.path.isfile(cand):
                    return cand
        return None
    for cand in (value, os.path.join(faces, value)):
        if os.path.isfile(cand):
            return cand
    for fn in os.listdir(faces):                       # a case-insensitive 8.3 name, as on DOS
        if fn.lower() == value.lower():
            return os.path.join(faces, fn)
    return None


def base_set():
    return set(range(0x20, 0x7F)) | {ord(c) for c in (bytes([h, l]).decode('cp949', 'ignore')
                                                       for h in range(0xB0, 0xC9) for l in range(0xA1, 0xFF)) if c}


def check(text_cps, map_text, faces, presets=('U', 'L'), data_dirs=(), base_check=False):
    """[(preset, font id, kind, [missing cps], note)] kind 'text' (the chain lacks characters the text draws),
    'base' (the first face is not a base face) or 'file' (a face that cannot be opened: note names it)."""
    sections = parse_map(map_text)
    problems = []
    base = base_set()
    for preset in presets:
        chains, targets = plan(sections, preset)
        for i, chain in chains.items():
            have = set()
            first = None
            for k, value in enumerate(chain):
                path = resolve(value, faces, list(data_dirs))
                if path is None:
                    problems.append((preset, i, 'file', [], value))
                    continue
                chars = face_chars(path)
                have |= chars
                if k == 0:
                    first = chars
            need = set(text_cps) | targets[i]
            miss = sorted(need - have)
            if miss:
                problems.append((preset, i, 'text', miss, ' + '.join(chain)))
            if base_check and first is not None and (base - first):
                problems.append((preset, i, 'base', sorted(base - first), chain[0]))
    return problems


def hot_stem(value):
    """The preload list's name for the face file a [fonts] value names: the file's stem, upper case
    (data:GBATB23.SVF -> GBATB23); charsetchars.py --hot-dir writes <stem>.HOT."""
    if value.lower().startswith('data:'):
        value = value[5:]
    return os.path.splitext(value.replace('\\', '/').rsplit('/', 1)[-1])[0].upper()


def read_hot(path):
    """The characters of a .HOT file as code points (UTF-8, '#' comment lines, whitespace skipped)."""
    cps = set()
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            if line.startswith('#'):
                continue
            cps |= {ord(c) for c in line if c > ' ' and c != '\x7f' and c != '\ufeff'}
    return cps


def hot_report(text_cps, map_text, faces, hot_dir, presets=('U', 'L'), data_dirs=()):
    """[(preset, face value, list size, in the face, text characters the face has that the list lacks)] for every
    face file of the map with a list in hot_dir (<STEM>.HOT, upper case, a case-insensitive match), one a file."""
    sections = parse_map(map_text)
    lists = {fn.upper(): os.path.join(hot_dir, fn) for fn in os.listdir(hot_dir)} if os.path.isdir(hot_dir) else {}
    rows = []
    seen = set()
    for preset in presets:
        chains, _ = plan(sections, preset)
        for chain in chains.values():
            for value in chain:
                path = resolve(value, faces, list(data_dirs))
                hot = lists.get(hot_stem(value) + '.HOT')
                if path is None or hot is None or (preset, path) in seen:
                    continue
                seen.add((preset, path))
                listed = read_hot(hot)
                have = face_chars(path)
                rows.append((preset, value, len(listed), len(listed & have), len((set(text_cps) & have) - listed)))
    return rows


def bundle_chars(paths):
    cps = set()
    for p in paths:
        for c in mkfont.chars_from_trs(p):
            cp = ord(c) if isinstance(c, str) else c
            if cp >= 0x20 and cp != 0xFFFD:
                cps.add(cp)
    return cps


def show(cps):
    return ' '.join('%s(U+%04X)' % (chr(c) if c > 0x20 else '', c) for c in cps[:40]) + (
        ' ... %d more' % (len(cps) - 40) if len(cps) > 40 else '')


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--trs', action='append', required=True)
    ap.add_argument('--map', required=True)
    ap.add_argument('--faces', help='folder of the faces (default: the map\'s folder)')
    ap.add_argument('--preset', choices=('U', 'L', 'both'), default='both')
    ap.add_argument('--data', action='append', default=[], help='folder a data: face is looked up in')
    ap.add_argument('--base-check', action='store_true')
    ap.add_argument('--hot', metavar='DIR', help='folder of preload lists (.HOT): report their coverage (informational)')
    a = ap.parse_args(argv)
    faces = a.faces or os.path.dirname(os.path.abspath(a.map))
    with open(a.map, encoding='utf-8', errors='replace') as f:
        text = f.read()
    presets = ('U', 'L') if a.preset == 'both' else (a.preset,)
    problems = check(bundle_chars(a.trs), text, faces, presets, a.data, a.base_check)
    for preset, i, kind, miss, note in problems:
        if kind == 'file':
            print('%s font %d: cannot open face %s' % (preset, i, note))
        elif kind == 'base':
            print('%s font %d: first face %s is no base face, lacks %d of ASCII + KS X 1001: %s'
                  % (preset, i, note, len(miss), show(miss)))
        else:
            print('%s font %d (%s): lacks %d: %s' % (preset, i, note, len(miss), show(miss)))
    if a.hot:
        text_cps = bundle_chars(a.trs)
        for preset, value, n, inface, lacking in hot_report(text_cps, text, faces, a.hot, presets, a.data):
            print('%s %s preload list: %d characters, the face has %d of them; the text has %d more the face holds'
                  % (preset, value, n, inface, lacking))
    if not problems:
        print('every face chain covers the text of %s (%s)' % (', '.join(os.path.basename(p) for p in a.trs),
                                                                  os.path.basename(a.map)))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
