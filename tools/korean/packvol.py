#!/usr/bin/env python3
"""Pack an SCI0 game's loose patch files (TEXT.nnn, SCRIPT.nnn, ...) into one
resource volume, so the engine opens one file instead of hundreds.

A fan translation ships every translated TEXT resource as a patch file. On
DOS each of them costs directory scans and open/close calls at start-up
(ResourceManager init, detection, the code-point census that reads every TEXT
resource). Packed, the same resources come out of one volume: LB1 Korean
(225 TEXT.nnn) reaches its first game frame ~0.9 s sooner in DOSBox-X at
60000 cycles (7.0 -> 6.1 s), KQ1 Korean (106) ~0.4 s sooner.

Two layouts:

  --mode msg (default)  MESSAGE.MAP + RESOURCE.MSG, the Korean fan-patch
        volume the engine already reads (ResourceManager::isKoreanMessageMap):
        with language=ko its entries replace the resources of the game's own
        volumes (not loose patch files, which still win). RESOURCE.MAP is left
        byte-identical, so detection by its md5 is unchanged. Needs
        language=ko in the target.

  --mode map  a new RESOURCE.0nn (next free number) and a rewritten
        RESOURCE.MAP whose entries for the packed resources point to it.
        Works for any language, but RESOURCE.MAP's md5 changes: a game whose
        detection entry lists it is no longer detected by that entry (ScummVM
        falls back to the generic SCI detector, if the gameid matches).

Resources are stored uncompressed (method 0) with the SCI0 volume header
{wResId, wPacked+4, wUnpacked, wCompression}; the data is the patch file
minus its 2-byte header (plus the extra header length in byte 1), exactly
what ResourceManager::processPatch() would load.

Usage:
  packvol.py SRC DST [--mode msg|map] [--types text,script] [--exclude FILE ...]
  packvol.py DIR --in-place [...]

SRC is copied to DST without the packed patch files, then the volume and map
are written into DST. --in-place writes into DIR and deletes the packed
patch files. --exclude keeps a patch file loose and out of the volume: use it
for a patch that must stay deletable (LB1's SCRIPT.414 fingerprint bypass) or
that a detection entry lists (KQ1 Korean's TEXT.000).
"""
import argparse
import os
import re
import shutil
import struct
import sys

# SCI0 resource types, index = type number (s_resTypeMapSci0)
TYPES = ['view', 'pic', 'script', 'text', 'sound', 'memory', 'vocab', 'font', 'cursor', 'patch']
PATCH_RE = re.compile(r'^([a-z]+)\.(\d{3})$', re.I)


def die(msg):
    sys.exit('packvol: ' + msg)


def find(dirpath, name):
    """Case-insensitive lookup of NAME in DIRPATH (DOS trees are upper case)."""
    for f in os.listdir(dirpath):
        if f.lower() == name.lower():
            return os.path.join(dirpath, f)
    return None


def read_sci0_map(data):
    """Entries (type, number, volume, offset) of an SCI0 RESOURCE.MAP, in file
    order: 6-bit volume, 26-bit offset. (An SCI01 map with 4-bit volumes shows
    up as volumes that do not exist, which main() refuses.)"""
    if len(data) % 6 or data[-6:] != b'\xff' * 6:
        die('RESOURCE.MAP is not an SCI0 map (no 6-byte 0xFF terminator)')
    out = []
    for i in range(0, len(data) - 6, 6):
        rid, off = struct.unpack_from('<HI', data, i)
        if rid == 0xFFFF and off == 0xFFFFFFFF:
            die('RESOURCE.MAP has a terminator before its end')
        out.append((rid >> 11, rid & 0x7FF, off >> 26, off & 0x3FFFFFF))
    return out


def load_patch(path, tname):
    raw = open(path, 'rb').read()
    if len(raw) < 3:
        die('%s: too small for a patch file' % path)
    t = raw[0] & 0x7F
    if t >= len(TYPES) or TYPES[t] != tname:
        die('%s: header type 0x%02x does not match the file name' % (path, raw[0]))
    data = raw[2 + raw[1]:]
    if len(data) + 4 > 0xFFFF:
        die('%s: %d bytes, too big for an SCI0 volume entry' % (path, len(data)))
    return t, data


def entries_first(entries):
    first = {}
    for t, n, v, o in entries:
        first.setdefault((t, n), (v, o))
    return first


def vol_entry(t, n, data):
    return struct.pack('<HHHH', (t << 11) | n, len(data) + 4, len(data), 0) + data


def build_volume(res):
    """res: sorted list of (type, number, data) -> (volume bytes, {(t,n): offset})."""
    vol = bytearray()
    offs = {}
    for t, n, data in res:
        offs[(t, n)] = len(vol)
        vol += vol_entry(t, n, data)
    if len(vol) > 0x3FFFFFF:
        die('volume would exceed 64 MB')
    return bytes(vol), offs


def build_msg_map(res, offs):
    """The SCI1-style map the Korean message.map reader expects: a directory of
    (0x80|type, u16 offset) ending with (0xFF, file size), then per type
    (u16 number, u32 plain offset into RESOURCE.MSG)."""
    types = sorted(set(t for t, _, _ in res))
    head = 3 * (len(types) + 1)
    body = bytearray()
    directory = bytearray()
    for t in types:
        directory += struct.pack('<BH', 0x80 | t, head + len(body))
        for tt, n, _ in res:
            if tt == t:
                body += struct.pack('<HI', n, offs[(t, n)])
    total = head + len(body)
    if total > 0xFFFF:
        die('MESSAGE.MAP would exceed 64 KB')
    directory += struct.pack('<BH', 0xFF, total)
    return bytes(directory + body)


def build_sci0_map(entries, res, offs, volno):
    packed = {(t, n) for t, n, _ in res}
    out = bytearray()
    seen = set()
    for t, n, v, o in entries:
        if (t, n) in packed:
            if (t, n) not in seen:
                out += struct.pack('<HI', (t << 11) | n, (volno << 26) | offs[(t, n)])
            seen.add((t, n))
        else:
            out += struct.pack('<HI', (t << 11) | n, (v << 26) | o)
    for t, n, _ in res:
        if (t, n) not in seen:
            out += struct.pack('<HI', (t << 11) | n, (volno << 26) | offs[(t, n)])
    return bytes(out + b'\xff' * 6)


def check_volume(vol, res, offs):
    for t, n, data in res:
        o = offs[(t, n)]
        rid, packed, unpacked, method = struct.unpack_from('<HHHH', vol, o)
        assert rid == (t << 11) | n and packed == len(data) + 4 and unpacked == len(data) and method == 0
        assert vol[o + 8:o + 8 + len(data)] == data


def check_msg_map(m, res, offs, vol):
    # parse it the way ResourceManager::readResourceMapSCI1 does for a Korean message.map
    dirs = []
    i = 0
    while True:
        t, o = struct.unpack_from('<BH', m, i)
        i += 3
        dirs.append((t & 0x1F, o))
        if t & 0x1F == 0x1F:
            break
    assert dirs[-1][1] == len(m)
    got = {}
    for (t, o), (_, nxt) in zip(dirs, dirs[1:]):
        for k in range((nxt - o) // 6):
            n, off = struct.unpack_from('<HI', m, o + 6 * k)
            got[(t, n)] = off
    assert got == offs, 'MESSAGE.MAP does not read back'
    check_volume(vol, res, offs)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('src')
    ap.add_argument('dst', nargs='?')
    ap.add_argument('--in-place', action='store_true')
    ap.add_argument('--mode', choices=['msg', 'map'], default='msg')
    ap.add_argument('--types', default=','.join(TYPES), help='comma-separated types to pack (default: all)')
    ap.add_argument('--exclude', action='append', default=[], metavar='FILE',
                    help='patch file to leave loose (repeatable), e.g. SCRIPT.414')
    ap.add_argument('--force', action='store_true', help='overwrite an existing MESSAGE.MAP/RESOURCE.MSG (msg mode)')
    a = ap.parse_args()

    if a.in_place == bool(a.dst):
        die('give either DST or --in-place')
    src = a.src
    mapfile = find(src, 'resource.map')
    if not mapfile:
        die('%s has no RESOURCE.MAP' % src)
    types = [t.strip().lower() for t in a.types.split(',') if t.strip()]
    for t in types:
        if t not in TYPES:
            die('unknown type %s (SCI0 types: %s)' % (t, ', '.join(TYPES)))
    excl = {e.lower() for e in a.exclude}

    entries = read_sci0_map(open(mapfile, 'rb').read())
    vols = {int(f.split('.')[1]) for f in os.listdir(src) if re.match(r'^resource\.\d{3}$', f, re.I)}
    for t, n, v, o in entries:
        if v not in vols:
            die('RESOURCE.MAP names volume %d, which is missing (not a plain SCI0 map?)' % v)

    res = []
    packed_files = []
    for f in sorted(os.listdir(src)):
        m = PATCH_RE.match(f)
        if not m or m.group(1).lower() not in types or f.lower() in excl:
            continue
        t, data = load_patch(os.path.join(src, f), m.group(1).lower())
        res.append((t, int(m.group(2)), data))
        packed_files.append(f)
    if not res:
        die('no patch files to pack')
    res.sort(key=lambda r: (r[0], r[1]))
    known = {(t, n) for t, n, _, _ in entries}
    new = [(t, n) for t, n, _ in res if (t, n) not in known]

    vol, offs = build_volume(res)
    check_volume(vol, res, offs)
    if a.mode == 'msg':
        if not a.force:
            for name in ('message.map', 'resource.msg'):
                if find(src, name):
                    die('%s already exists (a Korean fan patch volume?); --force to replace it' % name)
        m = build_msg_map(res, offs)
        check_msg_map(m, res, offs, vol)
        outputs = {'MESSAGE.MAP': m, 'RESOURCE.MSG': vol}
    else:
        volno = max(vols) + 1
        if volno > 63:
            die('no free volume number')
        m = build_sci0_map(entries, res, offs, volno)
        # read it back: first entry per id wins, as in readResourceMapSCI0
        first = {}
        for t, n, v, o in read_sci0_map(m):
            first.setdefault((t, n), (v, o))
        for (t, n), o in offs.items():
            assert first[(t, n)] == (volno, o), 'RESOURCE.MAP does not read back'
        for t, n, v, o in entries:
            if (t, n) not in offs:
                assert first[(t, n)] == entries_first(entries)[(t, n)]
        outputs = {os.path.basename(mapfile): m, 'RESOURCE.%03d' % volno: vol}

    if a.in_place:
        dst = src
    else:
        dst = a.dst
        if os.path.exists(dst):
            die('%s exists' % dst)
        skip = set(packed_files)
        shutil.copytree(src, dst, ignore=lambda d, names: [x for x in names if d == src and x in skip])
    for name, data in outputs.items():
        old = find(dst, name)
        if old and os.path.basename(old) != name:
            os.remove(old)
        with open(os.path.join(dst, name), 'wb') as f:
            f.write(data)
    if a.in_place:
        for f in packed_files:
            os.remove(os.path.join(src, f))

    by_type = {}
    for t, n, _ in res:
        by_type[TYPES[t]] = by_type.get(TYPES[t], 0) + 1
    print('packed %d patch files (%s) into %s: %d bytes' % (
        len(res), ', '.join('%d %s' % (c, t) for t, c in sorted(by_type.items())), ' + '.join(outputs), len(vol)))
    if new:
        print('  %d not in RESOURCE.MAP (added): %s' % (len(new), ' '.join('%s.%03d' % (TYPES[t], n) for t, n in new)))
    left = [f for f in os.listdir(dst) if PATCH_RE.match(f) and f.split('.')[0].lower() in TYPES]
    if left:
        print('  left loose: %s' % ' '.join(sorted(left)))
    if a.mode == 'msg':
        print('  RESOURCE.MAP unchanged; the volume is read only with language=ko')
    else:
        print('  RESOURCE.MAP rewritten: its md5 changed (detection entries that list it no longer match)')


if __name__ == '__main__':
    main()
