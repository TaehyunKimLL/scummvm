#!/usr/bin/env python3
"""Turn a cp949 Korean fan build of an SCI1.1 game into UTF-8 translation
files for the fork's engine, on top of the game's English files.

Written for the Quest for Glory I VGA remake Korean build (RESOURCE.000 +
loose .HEP/.SCR/.MSG patches, 2020s fan translation), which changes:

  * the MESSAGE resources (type 15), in cp949, same records as the English
    ones (talker, noun/verb/cond/seq and the 3 extra bytes are kept);
  * the strings in the HEAP resources (type 17), written over the English
    text in place, so they never get longer than the English string and a
    shorter one leaves the rest of the English text behind a NUL;
  * the game's fonts, which are not used here (hi-res text draws the glyphs).

Scripts, TEXT, vocab and everything else are byte-identical to the English
game and are left alone.

Output (OUTDIR):

  RESOURCE.MSG + MESSAGE.MAP   the changed messages, UTF-8, in the Korean
        message volume the engine reads with language=ko
        (ResourceManager::isKoreanMessageMap): SCI1.1 entry headers
        {u8 type, u16 number, u16 size, u16 size, u16 method 0}, and a map of
        {0x80|type, u16 offset} ... {0xFF, u16 size} + {u16 number, u32 offset}.
  NNN.MSG   a message the English game has as a loose patch (29, 95, 815):
        a loose patch always wins over RESOURCE.MSG, so the Korean one has to
        be a loose patch too, under the same name.
  sci-ko.str   the changed heap strings, "script<TAB>id<TAB>text", the id
        being the one Script::identifyOffsets() gives the English string
        (translation.h).

Strings grow when cp949 hangul (2 bytes) become UTF-8 (3 bytes), so each
message is rebuilt with new string offsets and the header size field fixed.

Usage:
  sci11_kr_extract.py ENDIR KRDIR OUTDIR [--encoding cp949]

ENDIR is the English game, KRDIR the Korean build, both as installed
(RESOURCE.MAP, RESOURCE.000 and the loose patch files).
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sci_dcl  # noqa: E402

T_MESSAGE, T_HEAP, T_FONT = 15, 17, 7
PATCH_EXT = {0: 'V56', 1: 'P56', 2: 'SCR', 3: 'TEX', 4: 'SND', 5: 'MEM', 6: 'VOC', 7: 'FNT', 8: 'CUR',
             9: 'PAT', 10: 'BIT', 11: 'PAL', 15: 'MSG', 17: 'HEP'}
SCRIPT_OBJECT_MAGIC = 0x1234


def die(msg):
    sys.exit('sci11_kr_extract: ' + msg)


class Game:
    """The resources of an SCI1.1 game directory: loose patch files win over
    the volume, as in the engine."""

    def __init__(self, path):
        self.path = path
        names = {n.upper(): n for n in os.listdir(path)}
        for need in ('RESOURCE.MAP', 'RESOURCE.000'):
            if need not in names:
                die('%s: no %s' % (path, need))
        self.names = names
        with open(os.path.join(path, names['RESOURCE.000']), 'rb') as f:
            self.vol = f.read()
        self.map = self._read_map(os.path.join(path, names['RESOURCE.MAP']))

    @staticmethod
    def _read_map(path):
        with open(path, 'rb') as f:
            d = f.read()
        dirs, p = [], 0
        while True:
            t, off = d[p] & 0x1F, struct.unpack_from('<H', d, p + 1)[0]
            p += 3
            dirs.append((t, off))
            if t == 0x1F:
                break
        out = {}
        for (t, off), (_, nxt) in zip(dirs, dirs[1:]):
            for q in range(off, nxt, 6):
                num, o = struct.unpack_from('<HI', d, q)
                out[(t, num)] = o & 0x0FFFFFFF
        return out

    def loose_name(self, key):
        ext = PATCH_EXT.get(key[0])
        return self.names.get('%d.%s' % (key[1], ext)) if ext else None

    def volume(self, key):
        o = self.map[key]
        _, num, packed, unpacked, method = struct.unpack_from('<BHHHH', self.vol, o)
        data = self.vol[o + 9:o + 9 + packed]
        if method == 0:
            return data
        if method in (18, 19, 20):
            return sci_dcl.explode(data, unpacked)
        raise ValueError('%s: compression method %d' % (key, method))

    def loose(self, key):
        name = self.loose_name(key)
        if not name:
            return None
        with open(os.path.join(self.path, name), 'rb') as f:
            d = f.read()
        return d[2 + d[1]:]

    def has_loose(self, key):
        return self.loose_name(key) is not None

    def get(self, key):
        d = self.loose(key)
        return d if d is not None else self.volume(key)

    def keys(self, rtype):
        out = {k for k in self.map if k[0] == rtype}
        ext = PATCH_EXT[rtype]
        for n in self.names:
            stem, _, e = n.partition('.')
            if e == ext and stem.isdigit():
                out.add((rtype, int(stem)))
        return sorted(out)


# ---- messages ------------------------------------------------------------

def msg_layout(d):
    ver = struct.unpack_from('<I', d, 0)[0]
    if ver // 1000 == 3:
        hs, rs = 8, 10
    elif ver // 1000 == 4:
        hs, rs = 10, 11
    else:
        raise ValueError('message version %d' % ver)
    cnt = struct.unpack_from('<H', d, hs - 2)[0]
    return hs, rs, cnt


def msg_strings(d):
    """The strings in record order: [(key tuple, bytes)]."""
    hs, rs, cnt = msg_layout(d)
    out = []
    for i in range(cnt):
        r = d[hs + i * rs:hs + (i + 1) * rs]
        off = struct.unpack_from('<H', r, 5)[0]
        end = d.find(b'\0', off)
        out.append((tuple(r[:4]), d[off:end if end >= 0 else len(d)]))
    return out


def decode(s, encoding, stats):
    try:
        return s.decode(encoding)
    except UnicodeDecodeError:
        stats['undecodable'] += 1
        return s.decode('latin-1')


def convert_message(d, encoding, stats):
    """Re-encode every string of the message resource D to UTF-8 and lay the
    resource out again; None when a string is not valid in ENCODING."""
    hs, rs, cnt = msg_layout(d)
    first = min(struct.unpack_from('<H', d, hs + i * rs + 5)[0] for i in range(cnt))
    old_len, size_field = len(d), struct.unpack_from('<H', d, 4)[0]
    tail = d[first:]
    if not tail.endswith(b'\0'):
        raise ValueError('string area does not end with NUL')
    segs = tail[:-1].split(b'\0')
    new_off, pos, body = {}, hs + cnt * rs, bytearray()
    if first != pos:
        raise ValueError('string area starts at %d, records end at %d' % (first, pos))
    old = first
    for s in segs:
        new_off[old] = pos + len(body)
        body += decode(s, encoding, stats).encode('utf-8') + b'\0'
        old += len(s) + 1
    out = bytearray(d[:hs + cnt * rs])
    for i in range(cnt):
        p = hs + i * rs + 5
        o = struct.unpack_from('<H', out, p)[0]
        if o not in new_off:
            raise ValueError('record %d points into a string' % i)
        if new_off[o] > 0xFFFF:
            raise ValueError('string offset %d does not fit 16 bits' % new_off[o])
        struct.pack_into('<H', out, p, new_off[o])
    out += body
    new_size = size_field + len(out) - old_len
    if not 0 <= new_size <= 0xFFFF:
        raise ValueError('size field %d does not fit 16 bits' % new_size)
    struct.pack_into('<H', out, 4, new_size)
    return bytes(out)


def decodable(d, encoding):
    hs, rs, cnt = msg_layout(d)
    try:
        for _, s in msg_strings(d):
            s.decode(encoding)
    except UnicodeDecodeError:
        return False
    return True


# ---- heaps ----------------------------------------------------------------

def heap_strings(h):
    """[(id, offset, bytes)] in the id order of Script::identifyOffsets() for
    SCI1.1 (the strings after the object instances, up to the end-of-string
    offset in the heap header)."""
    end = struct.unpack_from('<H', h, 0)[0]
    p = struct.unpack_from('<H', h, 2)[0] * 2 + 4
    while struct.unpack_from('<H', h, p)[0] == SCRIPT_OBJECT_MAGIC:
        nprops = struct.unpack_from('<H', h, p + 2)[0]
        p += 4 + nprops * 2 - 4
    out = []
    while p < end:
        e = h.find(b'\0', p, end + 1)
        if e < 0:
            break
        out.append((len(out) + 1, p, h[p:e]))
        p = e + 1
    return out


def escape(text):
    return text.replace('\r\n', '\\n').replace('\n', '\\n').replace('\r', '\\n')


# ---- output ---------------------------------------------------------------

def build_msg_volume(res):
    """res: sorted [(number, data)] of type-15 resources -> (RESOURCE.MSG, MESSAGE.MAP)."""
    vol, offs = bytearray(), {}
    for n, data in res:
        offs[n] = len(vol)
        vol += struct.pack('<BHHHH', T_MESSAGE, n, len(data), len(data), 0) + data
    head = 6
    body = b''.join(struct.pack('<HI', n, offs[n]) for n, _ in res)
    mp = struct.pack('<BH', 0x80 | T_MESSAGE, head) + struct.pack('<BH', 0xFF, head + len(body)) + body
    return bytes(vol), mp


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('endir')
    ap.add_argument('krdir')
    ap.add_argument('outdir')
    ap.add_argument('--encoding', default='cp949')
    ap.add_argument('--text-dump', metavar='FILE',
                    help='also write every converted message string, one per line (for mkfont.py --chars-from)')
    a = ap.parse_args()

    en, kr = Game(a.endir), Game(a.krdir)
    os.makedirs(a.outdir, exist_ok=True)
    stats = {'undecodable': 0}

    volume_msgs, loose_msgs, skipped = [], [], []
    for key in kr.keys(T_MESSAGE):
        if key not in en.map and not en.has_loose(key):
            skipped.append((key[1], 'not in the English game'))
            continue
        k = kr.get(key)
        if not decodable(k, a.encoding):
            if key in kr.map:
                k = kr.volume(key)
                skipped.append((key[1], 'loose patch is not %s, volume copy used' % a.encoding))
            if not decodable(k, a.encoding):
                skipped.append((key[1], 'not %s, left English' % a.encoding))
                continue
        if k == en.get(key):
            continue
        data = convert_message(k, a.encoding, stats)
        (loose_msgs if en.has_loose(key) else volume_msgs).append((key[1], data))

    if volume_msgs:
        vol, mp = build_msg_volume(sorted(volume_msgs))
        with open(os.path.join(a.outdir, 'RESOURCE.MSG'), 'wb') as f:
            f.write(vol)
        with open(os.path.join(a.outdir, 'MESSAGE.MAP'), 'wb') as f:
            f.write(mp)
    for n, data in loose_msgs:
        with open(os.path.join(a.outdir, '%d.MSG' % n), 'wb') as f:
            f.write(bytes([0x80 | T_MESSAGE, 0]) + data)
    if a.text_dump:
        with open(a.text_dump, 'w', encoding='utf-8', newline='\n') as f:
            for _, data in volume_msgs + loose_msgs:
                for _, s in msg_strings(data):
                    f.write(s.decode('utf-8').replace('\n', ' ') + '\n')

    lines = ['# Quest for Glory I VGA, Korean heap strings (script, id, text), UTF-8.',
             '# Made by tools/korean/sci11_kr_extract.py from the cp949 fan build.']
    nstr = 0
    for key in sorted(set(kr.keys(T_HEAP)) & (set(en.keys(T_HEAP)))):
        e, k = en.get(key), kr.get(key)
        if e == k:
            continue
        for sid, off, s in heap_strings(e):
            end = k.find(b'\0', off)
            t = k[off:end]
            if t == s:
                continue
            lines.append('%d\t%d\t%s' % (key[1], sid, escape(decode(t, a.encoding, stats))))
            nstr += 1
    with open(os.path.join(a.outdir, 'sci-ko.str'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')

    fonts = [k[1] for k in kr.keys(T_FONT) if k in en.map and kr.get(k) != en.get(k)]
    print('messages: %d in RESOURCE.MSG, %d loose %s' % (len(volume_msgs), len(loose_msgs), [n for n, _ in loose_msgs]))
    print('heap strings: %d in sci-ko.str' % nstr)
    print('fonts changed in the build, not converted: %s' % fonts)
    if stats['undecodable']:
        print('strings that were not valid %s (kept as latin-1): %d' % (a.encoding, stats['undecodable']))
    for n, why in skipped:
        print('note: %d: %s' % (n, why))


if __name__ == '__main__':
    main()
