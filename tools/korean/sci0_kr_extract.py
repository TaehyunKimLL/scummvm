#!/usr/bin/env python3
"""sci0_kr_extract.py - turn an SCI0 Korean fan build for the ORIGINAL Sierra
interpreter into the fork's UTF-8 translation files, to be dropped onto the
English original.

The fan builds this was written for (Conquests of Camelot KR beta 3/5) keep
the English volumes untouched and add one volume holding

  * the translated TEXT resources and SCRIPT resources, uncompressed, with the
    Korean in EUC-KR (KS X 1001),
  * font resources with the Hangul glyphs, one bank per EUC-KR lead byte
    (bank = first bank + lead - 0xB0, glyph = trail byte), which a patched
    SCIV.EXE draws from,

and rewrite RESOURCE.MAP so the translated resources point at that volume.

Commands:

  rebuild-map KRDIR OUT.map
      Rebuild the English RESOURCE.MAP from the untouched volumes: entries
      that the fan build re-pointed at its own volume go back to the
      resource's location(s) in the original volumes (found by scanning the
      SCI0 resource headers); entries for resources that only exist in the
      new volume are dropped.

  extract ENDIR KRDIR OUTDIR [--game-name NAME]
      ENDIR: the English original; KRDIR: the fan build. Writes
        OUTDIR/text.NNN      every TEXT resource the build changed, UTF-8,
                             as a patch file (0x83 0x00 + NUL-separated
                             strings)
        OUTDIR/sci-ko.str    every script string the build changed, keyed
                             "script<TAB>id<TAB>text" by the ENGLISH script's
                             string id (Script::identifyOffsets() numbering,
                             the one dump_script_strings prints)
        OUTDIR/sci-ko.str.en English => Korean for each sci-ko.str line, for
                             review (not part of the translation)
        OUTDIR/extract-report.txt
                             counts, string-count mismatches, unmapped
                             strings, glyph-bank check

  script-strings DIR
      Print the SCI0 script string table (script, id, text) the way the
      engine's dump_script_strings does, for checking the numbering.

Only SCI0 volumes (6-byte map entries, 8-byte resource headers) and
compression methods 0 (none) and 1 (LZW) are read - that is all TEXT and
SCRIPT resources use in the games tried. Pure Python 3, no dependencies.
"""

import argparse
import os
import struct
import sys
from collections import defaultdict

TYPES = ['view', 'pic', 'script', 'text', 'sound', 'memory', 'vocab', 'font',
         'cursor', 'patch']
T_SCRIPT, T_TEXT, T_FONT = 2, 3, 7
SCI_OBJ_STRINGS = 5


def ifind(d, name):
    """Case-insensitive file lookup."""
    for f in os.listdir(d):
        if f.lower() == name.lower():
            return os.path.join(d, f)
    return None


# ---- SCI0 resource files ----------------------------------------------

def read_map(path):
    m = open(path, 'rb').read()
    ents = []
    for i in range(0, len(m) - 5, 6):
        a, b = struct.unpack_from('<HI', m, i)
        if a == 0xFFFF:
            break
        ents.append((a >> 11, a & 0x7FF, b >> 26, b & 0x3FFFFFF))
    return ents


def write_map(path, ents):
    b = b''.join(struct.pack('<HI', (t << 11) | n, (v << 26) | o) for t, n, v, o in ents)
    open(path, 'wb').write(b + b'\xff' * 6)
    return b + b'\xff' * 6


def scan_volume(path):
    """{(type, nr): [(offset, compSize, decompSize, method)]} from the
    headers laid end to end in an SCI0 volume."""
    d = open(path, 'rb').read()
    out = defaultdict(list)
    o = 0
    while o + 8 <= len(d):
        a, cs, ds, meth = struct.unpack_from('<HHHH', d, o)
        out[(a >> 11, a & 0x7FF)].append((o, cs, ds, meth))
        o += 4 + cs
    if o != len(d):
        raise ValueError('%s: resource chain ends at %d of %d' % (path, o, len(d)))
    return out


def unlzw(src, size):
    """SCI0 LZW (method 1): LSB-first codes, 9..12 bits, 256 reset, 257 end.
    Mirrors DecompressorLZW::unpackLZW()."""
    out = bytearray()
    bitbuf = nbits = pos = 0
    width, table, limit = 9, 258, 512
    offs = [0] * 4096
    lens = [0] * 4096
    while len(out) < size:
        while nbits < width:
            if pos >= len(src):
                return bytes(out)
            bitbuf |= src[pos] << nbits
            pos += 1
            nbits += 8
        code = bitbuf & ((1 << width) - 1)
        bitbuf >>= width
        nbits -= width
        if code == 257:
            break
        if code == 256:
            width, table, limit = 9, 258, 512
            continue
        if code >= table:
            raise ValueError('LZW code %x beyond table %x' % (code, table))
        start = len(out)
        if code <= 255:
            out.append(code)
        else:
            for i in range(lens[code]):
                if len(out) >= size:
                    break
                out.append(out[offs[code] + i])
        if table >= 4096:
            continue
        if table == limit and width < 12:
            width += 1
            limit = 1 << width
        offs[table] = start
        lens[table] = len(out) - start + 1
        table += 1
    return bytes(out)


class Game:
    def __init__(self, d, mapname='resource.map'):
        self.dir = d
        self.map = read_map(ifind(d, mapname))
        self.vols = {}
        self.where = {}
        for t, n, v, o in self.map:
            self.where.setdefault((t, n), (v, o))

    def volume(self, v):
        if v not in self.vols:
            self.vols[v] = open(ifind(self.dir, 'resource.%03d' % v), 'rb').read()
        return self.vols[v]

    def ids(self, t):
        return sorted({n for tt, n, v, o in self.map if tt == t})

    def raw(self, t, n):
        """(volume, method, data) of the resource, decompressed."""
        v, o = self.where[(t, n)]
        d = self.volume(v)
        a, cs, ds, meth = struct.unpack_from('<HHHH', d, o)
        if (a >> 11, a & 0x7FF) != (t, n):
            raise ValueError('%s.%d: header at %d:%d says %d.%d' % (TYPES[t], n, v, o, a >> 11, a & 0x7FF))
        body = d[o + 8:o + 4 + cs]
        if meth == 0:
            data = body[:ds]
        elif meth == 1:
            data = unlzw(body, ds)
        else:
            raise ValueError('%s.%d: compression %d not supported' % (TYPES[t], n, meth))
        return v, meth, data


# ---- script strings (Script::identifyOffsets, SCI0) -------------------

def script_strings(data, early=False):
    """[(id, offset, bytes)] - every NUL-terminated string in the script's
    string blocks, numbered from 1 in file order, empty ones included (they
    take an id in the engine too)."""
    out = []
    p = 2 if early else 0
    sid = 0
    while p + 2 <= len(data):
        btype = struct.unpack_from('<H', data, p)[0]
        if btype == 0:
            break
        bsize = struct.unpack_from('<H', data, p + 2)[0]
        if bsize < 4:
            raise ValueError('bad block size')
        body = p + 4
        end = p + bsize
        if btype == SCI_OBJ_STRINGS:
            q = body
            while q < end:
                if end - q == 1 and data[q] == 0:
                    break
                z = data.find(b'\0', q, end)
                if z < 0:
                    break
                sid += 1
                out.append((sid, q, data[q:z]))
                q = z + 1
        p = end
    return out


def escape(s, engine=False):
    """One line of a .str file. A CR LF pair is one line break to the SCI0
    text code, so it becomes one \\n; engine=True escapes each of CR and LF
    the way dump_script_strings does (for comparing against its output)."""
    if engine:
        return s.replace('\r', '\\n').replace('\n', '\\n').replace('\t', ' ')
    return s.replace('\r\n', '\\n').replace('\r', '\\n').replace('\n', '\\n').replace('\t', ' ')


def decode(b, report, where):
    try:
        return b.decode('cp949')
    except UnicodeDecodeError as e:
        report.append('  %s: not cp949 (%s), undecodable bytes replaced' % (where, e))
        return b.decode('cp949', 'replace')


# ---- commands ---------------------------------------------------------

def cmd_rebuild_map(kr, out):
    ents = read_map(ifind(kr, 'resource.map'))
    newvol = max(v for t, n, v, o in ents)
    vols = {}
    for v in range(1, newvol):
        p = ifind(kr, 'resource.%03d' % v)
        if p:
            for k, ls in scan_volume(p).items():
                for l in ls:
                    vols.setdefault(k, []).append((v, l[0]))
    want = defaultdict(int)
    for t, n, v, o in ents:
        if v == newvol:
            want[(t, n)] += 1
    used = defaultdict(int)
    res = []
    dropped = 0
    for t, n, v, o in ents:
        if v != newvol:
            res.append((t, n, v, o))
            continue
        locs = sorted(vols.get((t, n), []))
        if not locs:
            dropped += 1
            continue
        if len(locs) != want[(t, n)]:
            print('warning: %s.%d: %d map entries, %d copies in the volumes'
                  % (TYPES[t], n, want[(t, n)], len(locs)), file=sys.stderr)
        l = locs[min(used[(t, n)], len(locs) - 1)]
        used[(t, n)] += 1
        res.append((t, n, l[0], l[1]))
    b = write_map(out, res)
    import hashlib
    print('%s: %d entries (%d dropped), %d bytes, md5 %s, md5(first 5000) %s'
          % (out, len(res), dropped, len(b), hashlib.md5(b).hexdigest(),
             hashlib.md5(b[:5000]).hexdigest()))


def cmd_script_strings(d, early):
    g = Game(d)
    for n in g.ids(T_SCRIPT):
        for sid, off, s in script_strings(g.raw(T_SCRIPT, n)[2], early):
            print('%d\t%d\t%s' % (n, sid, escape(s.decode('latin-1'), engine=True)))


def check_glyph_banks(kr, en, used, report):
    """The fan build's Hangul banks: font resources absent from the English
    game. Check each used syllable has ink at bank[lead-0xB0][trail]."""
    banks = [n for n in kr.ids(T_FONT) if (T_FONT, n) not in en.where]
    if not banks:
        report.append('glyph banks: none (no new font resources)')
        return 0
    report.append('glyph banks: fonts %d-%d (%d)' % (banks[0], banks[-1], len(banks)))
    fonts = {n: kr.raw(T_FONT, n)[2] for n in banks}
    # Group consecutive runs of 25 (one per lead 0xB0-0xC8)
    runs = [banks[i:i + 25] for i in range(0, len(banks), 25)]
    unresolved = 0
    for run in runs:
        h = struct.unpack_from('<H', fonts[run[0]], 4)[0]
        missing = []
        for ch in sorted(used):
            e = ch.encode('cp949')
            if len(e) != 2 or not 0xB0 <= e[0] <= 0xC8:
                missing.append(ch)
                continue
            i = e[0] - 0xB0
            if i >= len(run):
                missing.append(ch)
                continue
            f = fonts[run[i]]
            o = struct.unpack_from('<H', f, 6 + 2 * e[1])[0]
            w, gh = f[o], f[o + 1]
            if not any(f[o + 2:o + 2 + gh * ((w + 7) // 8)]):
                missing.append(ch)
        extra = 0
        for i, n in enumerate(run):
            f = fonts[n]
            cnt = struct.unpack_from('<H', f, 2)[0]
            for t in range(min(cnt, 256)):
                o = struct.unpack_from('<H', f, 6 + 2 * t)[0]
                w, gh = f[o], f[o + 1]
                if any(f[o + 2:o + 2 + gh * ((w + 7) // 8)]):
                    try:
                        c = bytes([0xB0 + i, t]).decode('cp949')
                    except UnicodeDecodeError:
                        c = None
                    if c not in used:
                        extra += 1
        report.append('  banks %d-%d (%dpx): %d used syllables without a glyph %s; '
                      '%d glyphs no string uses'
                      % (run[0], run[-1], h, len(missing), ''.join(missing[:40]), extra))
        unresolved = max(unresolved, len(missing))
    return unresolved


def cmd_extract(endir, krdir, outdir, early, name):
    en, kr = Game(endir), Game(krdir)
    newvol = max(v for t, n, v, o in kr.map)
    os.makedirs(outdir, exist_ok=True)
    report = []
    used = set()

    # TEXT
    texts = [n for n in kr.ids(T_TEXT) if kr.where[(T_TEXT, n)][0] == newvol]
    report.append('TEXT resources in the new volume: %d' % len(texts))
    nstr = 0
    for n in texts:
        k = kr.raw(T_TEXT, n)[2]
        parts = k.split(b'\0')
        if parts and parts[-1] == b'':
            parts = parts[:-1]
        u = [decode(p, report, 'text.%03d#%d' % (n, i)) for i, p in enumerate(parts)]
        for s in u:
            used.update(c for c in s if ord(c) > 127)
        if (T_TEXT, n) in en.where:
            e = en.raw(T_TEXT, n)[2].split(b'\0')
            if e and e[-1] == b'':
                e = e[:-1]
            if len(e) != len(u):
                report.append('  text.%03d: %d strings, English has %d' % (n, len(u), len(e)))
        else:
            report.append('  text.%03d: not in the English game' % n)
        nstr += len(u)
        body = b''.join(s.encode('utf-8') + b'\0' for s in u)
        open(os.path.join(outdir, 'text.%03d' % n), 'wb').write(b'\x83\x00' + body)
    report.append('  %d strings written' % nstr)

    # SCRIPT strings
    scripts = [n for n in kr.ids(T_SCRIPT) if kr.where[(T_SCRIPT, n)][0] == newvol]
    report.append('SCRIPT resources in the new volume: %d' % len(scripts))
    lines = []
    unmapped = []
    mapped = 0
    for n in scripts:
        if (T_SCRIPT, n) not in en.where:
            report.append('  script.%03d: not in the English game' % n)
            continue
        es = script_strings(en.raw(T_SCRIPT, n)[2], early)
        ks = script_strings(kr.raw(T_SCRIPT, n)[2], early)
        # The fan build pads its string blocks with extra NULs (each one an
        # empty string with an id of its own), so the numbering differs;
        # the non-empty strings stay in the English order.
        ne = [x for x in es if x[2]]
        nk = [x for x in ks if x[2]]
        if len(ne) == len(nk):
            pairs = list(zip(ne, nk))
        else:
            import difflib
            report.append('  script.%03d: %d non-empty strings, English %d - aligned by diff'
                          % (n, len(nk), len(ne)))
            sm = difflib.SequenceMatcher(None, [x[2] for x in ne], [x[2] for x in nk], autojunk=False)
            pairs = []
            for tag, a0, a1, b0, b1 in sm.get_opcodes():
                if tag == 'equal' or (tag == 'replace' and a1 - a0 == b1 - b0):
                    pairs += list(zip(ne[a0:a1], nk[b0:b1]))
                else:
                    for x in nk[b0:b1]:
                        unmapped.append('%d\t?\t%s' % (n, escape(decode(x[2], report, 'script.%d' % n))))
        for e, k in pairs:
            if e[2] == k[2]:
                continue
            t = decode(k[2], report, 'script.%03d#%d' % (n, k[0]))
            used.update(c for c in t if ord(c) > 127)
            lines.append((n, e[0], escape(t), escape(e[2].decode('latin-1'))))
            mapped += 1
    with open(os.path.join(outdir, 'sci-ko.str'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('# %s (SCI0, DOS English), Korean.\n' % name)
        f.write('# script<TAB>id<TAB>text - ids from dump_script_strings (English scripts).\n')
        f.write('# This file is also the UTF-8 manifest.\n')
        for n, sid, t, e in lines:
            f.write('%d\t%d\t%s\n' % (n, sid, t))
    report.append('  %d script strings mapped, %d unmapped' % (mapped, len(unmapped)))
    report += ['  unmapped: ' + u for u in unmapped]
    with open(os.path.join(outdir, 'sci-ko.str.en'), 'w', encoding='utf-8', newline='\n') as f:
        for n, sid, t, e in lines:
            f.write('%d\t%d\t%s\t=>\t%s\n' % (n, sid, e, t))

    # Other resource types the build replaced (for the record)
    other = defaultdict(int)
    for (t, n), (v, o) in kr.where.items():
        if v == newvol and t not in (T_TEXT, T_SCRIPT):
            other[TYPES[t]] += 1
    report.append('other resources in the new volume: %s' % dict(other))

    hangul = {c for c in used if 0xAC00 <= ord(c) <= 0xD7A3}
    report.append('characters: %d distinct non-ASCII, %d Hangul syllables; others: %s'
                  % (len(used), len(hangul), ''.join(sorted(used - hangul))))
    unresolved = check_glyph_banks(kr, en, used, report)
    report.append('unresolved glyphs: %d' % unresolved)
    open(os.path.join(outdir, 'extract-report.txt'), 'w', encoding='utf-8').write('\n'.join(report) + '\n')
    print('\n'.join(report))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    a = sub.add_parser('rebuild-map')
    a.add_argument('krdir')
    a.add_argument('out')
    a = sub.add_parser('extract')
    a.add_argument('endir')
    a.add_argument('krdir')
    a.add_argument('outdir')
    a.add_argument('--early', action='store_true', help='SCI_VERSION_0_EARLY scripts (2-byte prefix)')
    a.add_argument('--game-name', default='Conquests of Camelot')
    a = sub.add_parser('script-strings')
    a.add_argument('dir')
    a.add_argument('--early', action='store_true')
    args = ap.parse_args()
    if args.cmd == 'rebuild-map':
        cmd_rebuild_map(args.krdir, args.out)
    elif args.cmd == 'extract':
        cmd_extract(args.endir, args.krdir, args.outdir, args.early, args.game_name)
    else:
        cmd_script_strings(args.dir, args.early)


if __name__ == '__main__':
    main()
