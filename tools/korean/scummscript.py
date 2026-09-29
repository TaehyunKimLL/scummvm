#!/usr/bin/env python3
"""List the strings of a SCUMM v4 or v5 game's scripts, with their context.

Every string the engine hands to translateText() comes from a script (print,
printEgo, verbOps name, setObjectName, actorOps name, stringOps load) or from
an object's name. Finding them needs the scripts walked opcode by opcode: a
translation that changed a string's length moved every jump after it, so byte
offsets do not line up between releases, and a string can hold a NUL-like 0x00
inside an 0xFF escape.

The walker only decodes operand sizes (the engine's o4_/o5_ handlers,
engines/scumm/script_v5.cpp and script_v4.cpp); it does not follow jumps, so
it reads each script block linearly from its first instruction to its end.

Context is what ScummEngine::translateText() keys a .trs range on:
(room, where, number) - where 1 (WIO_ROOM) for entry/exit/object code and
object names, 2 (WIO_GLOBAL) for global scripts (room 0), 3 (WIO_LOCAL) for
local scripts.

    scummscript.py <gamedir> [--v5 NAME] [--limit N]

v4 is the LucasArts floppy layout (000.LFL index, DISK0N.LEC XOR 0x69,
6-byte little-endian block headers); v5 is <name>.000/.001 (LECF, XOR 0x69).
"""
import argparse
import collections
import os
import struct

WIO_ROOM, WIO_GLOBAL, WIO_LOCAL = 1, 2, 3


def dexor(raw, key=0x69):
    return bytes(b ^ key for b in raw)


def res_str_len(d, p):
    """ScummEngine::resStrLen for v4/v5: 0xFF + code, + 2 argument bytes unless 1, 2, 3, 8."""
    n = p
    while d[n] != 0:
        if d[n] == 0xFF:
            n += 2 if d[n + 1] in (1, 2, 3, 8) else 4
        else:
            n += 1
    return n - p


class BadOpcode(Exception):
    pass


# Operand shapes of the opcodes without sub-opcodes. B1/W1: byte/word that is
# a variable when the opcode's 0x80 bit is set (B2/W2: 0x40, B3/W3: 0x20);
# R: a variable (result or operand, + a word when bit 0x2000 is set);
# J: jump offset; b/w: literal byte/word; V: word list up to 0xFF.
_SHAPES = [
    (0x01, 'B1 W2 W3', 0xE0), (0x02, 'B1', 0x80), (0x03, 'R B1', 0x80),
    (0x04, 'R W1 J', 0x80), (0x06, 'R B1', 0x80), (0x07, 'W1 B2', 0xC0),
    (0x08, 'R W1 J', 0x80), (0x09, 'B1 W2', 0xC0), (0x0A, 'B1 V', 0xE0),
    (0x0B, 'R W1 W2', 0xC0), (0x0D, 'B1 B2 b', 0xC0), (0x0E, 'B1 W2', 0xC0),
    (0x10, 'R W1', 0x80), (0x11, 'B1 B2', 0xC0), (0x12, 'W1', 0x80),
    (0x15, 'R W1 W2', 0xC0), (0x16, 'R B1', 0x80), (0x17, 'R W1', 0x80),
    (0x19, 'B1 X W2 W3', 0xE0), (0x1A, 'R W1', 0x80), (0x1B, 'R W1', 0x80),
    (0x1C, 'B1', 0x80), (0x1D, 'W1 C J', 0x80), (0x1E, 'B1 W2 W3', 0xE0),
    (0x1F, 'B1 B2 J', 0xC0), (0x22, 'R B1', 0x80), (0x23, 'R W1', 0x80),
    (0x24, 'W1 B2 w w', 0xC0), (0x25, 'W1 B2', 0xC0), (0x29, 'W1 B2', 0xC0),
    (0x2D, 'B1 B2', 0xC0), (0x31, 'R B1', 0x80), (0x32, 'W1', 0x80),
    (0x34, 'R W1 W2', 0xC0), (0x35, 'R B1 B2', 0xC0), (0x36, 'B1 W2', 0xC0),
    (0x37, 'W1 B2 V', 0xC0), (0x38, 'R W1 J', 0x80), (0x3A, 'R W1', 0x80),
    (0x3B, 'R B1', 0x80), (0x3C, 'B1', 0x80), (0x3D, 'R B1 B2', 0xC0),
    (0x42, 'B1 V', 0x80), (0x43, 'R W1', 0x80), (0x44, 'R W1 J', 0x80),
    (0x48, 'R W1 J', 0x80), (0x50, 'W1', 0x80), (0x52, 'B1', 0x80),
    (0x56, 'R B1', 0x80), (0x57, 'R W1', 0x80), (0x5A, 'R W1', 0x80),
    (0x5B, 'R W1', 0x80), (0x5D, 'W1 C', 0x80), (0x60, 'B1', 0x80),
    (0x62, 'B1', 0x80), (0x63, 'R B1', 0x80), (0x66, 'R W1', 0x80),
    (0x67, 'R B1', 0x80), (0x68, 'R B1', 0x80), (0x6B, 'W1', 0x80),
    (0x6C, 'R B1', 0x80), (0x6E, 'W1', 0x80), (0x70, 'B1 b b', 0x80),
    (0x71, 'R B1', 0x80), (0x72, 'B1', 0x80), (0x78, 'R W1 J', 0x80),
    (0x7B, 'R B1', 0x80), (0x7C, 'R B1', 0x80), (0x0F, 'R W1', 0x80),
    (0x05, 'W1 D', 0x80),
    (0x00, '', 0), (0xA0, '', 0), (0x20, '', 0), (0x80, '', 0), (0xC0, '', 0),
    (0x18, 'J', 0), (0x2E, 'b b b', 0), (0x2B, 'R', 0), (0x46, 'R', 0),
    (0xC6, 'R', 0), (0x28, 'R J', 0), (0xA8, 'R J', 0), (0x40, 'V', 0),
    (0x58, 'b', 0), (0x98, 'b', 0), (0x4C, 'V', 0), (0xA7, '', 0),
    (0xAB, 'S3', 0), (0x26, 'R n', 0x80),
]


def _table():
    t = {}
    for base, shape, bits in _SHAPES:
        combos = [0]
        for bit in (0x80, 0x40, 0x20):
            if bits & bit:
                combos += [c | bit for c in combos]
        for c in combos:
            t.setdefault(base | c, shape.split())
    return t


SHAPES = _table()

# startScript also uses bits 0x20/0x40 as flags.
for _op in (0x2A, 0x4A, 0x6A, 0xAA, 0xCA, 0xEA):
    SHAPES[_op] = ['B1', 'V']


class Walker:
    """Linear operand-size walker; collects (offset, kind, bytes) strings."""

    ACTOR_CONV = [1, 0, 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 20]

    def __init__(self, d, start, end, version):
        self.d, self.p, self.end = d, start, end
        self.v = version
        self.small = version <= 4
        self.strings = []

    # -- operands
    def b(self):
        x = self.d[self.p]
        self.p += 1
        return x

    def w(self):
        x = struct.unpack_from('<H', self.d, self.p)[0]
        self.p += 2
        return x

    def var(self):
        x = self.w()
        if x & 0x2000:
            self.w()
        return x

    def B(self, op, mask):
        self.var() if op & mask else self.b()

    def W(self, op, mask):
        self.var() if op & mask else self.w()

    def vararg(self):
        while True:
            s = self.b()
            if s == 0xFF:
                return
            self.W(s, 0x80)

    def text(self, kind):
        n = res_str_len(self.d, self.p)
        self.strings.append((self.p, kind, self.d[self.p:self.p + n]))
        self.p += n + 1

    def raw_text(self, kind):
        n = self.d.index(b'\0', self.p) - self.p
        self.strings.append((self.p, kind, self.d[self.p:self.p + n]))
        self.p += n + 1

    def run(self):
        while self.p < self.end:
            self.op()
        return self.strings

    # -- sub-opcode families
    def parse_string(self, kind):
        while True:
            s = self.b()
            if s == 0xFF:
                return
            c = s & 0xF
            if c in (0, 3, 8):
                self.W(s, 0x80); self.W(s, 0x40)
            elif c == 1:
                self.B(s, 0x80)
            elif c == 2:
                self.W(s, 0x80)
            elif c in (4, 6, 7):
                pass
            elif c == 15:
                self.text(kind)
                return
            else:
                raise BadOpcode('print sub-op %02x' % s)

    def actor_ops(self, op):
        self.B(op, 0x80)
        while True:
            s = self.b()
            if s == 0xFF:
                return
            c = s & 0x1F
            if self.small:
                c = self.ACTOR_CONV[c - 1]
            if c in (0, 1, 3, 4, 6, 12, 14, 16, 19, 22, 23):
                self.B(s, 0x80)
            elif c in (2, 5, 11):
                self.B(s, 0x80); self.B(s, 0x40)
            elif c == 7:
                self.B(s, 0x80); self.B(s, 0x40); self.B(s, 0x20)
            elif c in (8, 10, 18, 20, 21):
                pass
            elif c == 9:
                self.W(s, 0x80)
            elif c == 13:
                self.text('actorname')
            elif c == 17:
                self.B(s, 0x80)
                if self.v != 4:
                    self.B(s, 0x40)
            else:
                raise BadOpcode('actorOps sub-op %02x' % s)

    def verb_ops(self, op):
        self.B(op, 0x80)
        while True:
            s = self.b()
            if s == 0xFF:
                return
            c = s & 0x1F
            if c in (1, 20):
                self.W(s, 0x80)
            elif c == 2:
                self.text('verb')
            elif c in (3, 4, 16, 18, 23):
                self.B(s, 0x80)
            elif c == 5:
                self.W(s, 0x80); self.W(s, 0x40)
            elif c in (6, 7, 8, 9, 17, 19):
                pass
            elif c == 22:
                self.W(s, 0x80); self.B(s, 0x40)
            else:
                raise BadOpcode('verbOps sub-op %02x' % s)

    def room_ops(self):
        s = self.b()
        c = s & 0x1F
        B, W = self.B, self.W
        if c in (1, 2, 3):
            W(s, 0x80); W(s, 0x40)
        elif c == 4:
            W(s, 0x80); W(s, 0x40)
            if not self.small:
                W(s, 0x20); s = self.b(); B(s, 0x80)
        elif c in (5, 6):
            pass
        elif c == 7:
            B(s, 0x80); B(s, 0x40); s = self.b(); B(s, 0x80); B(s, 0x40); s = self.b(); B(s, 0x40)
        elif c == 8:
            B(s, 0x80); B(s, 0x40); B(s, 0x20)
        elif c in (9, 16):
            B(s, 0x80); B(s, 0x40)
        elif c == 10:
            W(s, 0x80)
        elif c in (11, 12):
            W(s, 0x80); W(s, 0x40); W(s, 0x20); s = self.b(); B(s, 0x80); B(s, 0x40)
        elif c in (13, 14):
            B(s, 0x80); self.raw_text('savestring')
        elif c == 15:
            B(s, 0x80); s = self.b(); B(s, 0x80); B(s, 0x40); s = self.b(); B(s, 0x80)
        else:
            raise BadOpcode('roomOps sub-op %02x' % s)

    def string_ops(self):
        s = self.b()
        c = s & 0x1F
        if c == 1:
            self.B(s, 0x80); self.text('string')
        elif c in (2, 5):
            self.B(s, 0x80); self.B(s, 0x40)
        elif c == 3:
            self.B(s, 0x80); self.B(s, 0x40); self.B(s, 0x20)
        elif c == 4:
            self.var(); self.B(s, 0x80); self.B(s, 0x40)
        else:
            raise BadOpcode('stringOps sub-op %02x' % s)

    # -- one instruction
    def op(self):
        at = self.p
        op = self.b()
        B, W = self.B, self.W
        small = self.small
        if small and op & 0x1F == 0x05:            # v4 drawObject: obj, x, y
            W(op, 0x80); W(op, 0x40); W(op, 0x20)
        elif small and op in (0x0F, 0x8F):         # v4 ifState
            W(op, 0x80); B(op, 0x40); self.w()
        elif small and op in (0x22, 0xA2):         # v4 saveLoadGame
            self.var(); B(op, 0x80)
        elif small and op in (0x5C, 0xDC):         # v4 oldRoomEffect
            s = self.b()
            if s & 0x1F == 3:
                W(s, 0x80)
        elif op in (0x2F, 0x6F, 0xAF, 0xEF, 0x4F, 0xCF):   # ifState / ifNotState
            W(op, 0x80); B(op, 0x40); self.w()
        elif op in (0x14, 0x94):
            B(op, 0x80); self.parse_string('print')
        elif op == 0xD8:
            self.parse_string('printEgo')
        elif op in (0x54, 0xD4):
            W(op, 0x80); self.text('objname')
        elif op in (0x7A, 0xFA):
            self.verb_ops(op)
        elif op in (0x13, 0x53, 0x93, 0xD3):
            self.actor_ops(op)
        elif op in (0x33, 0x73, 0xB3, 0xF3):
            self.room_ops()
        elif op == 0x27:
            self.string_ops()
        elif op in (0x0C, 0x8C):                   # resourceRoutines
            s = self.b()
            if s != 17:
                B(s, 0x80)
            c = s & 0x3F
            if c == 20:
                W(s, 0x40)
            elif c in (35, 37):
                B(s, 0x40)
            elif c == 36:
                B(s, 0x40); self.b()
        elif op == 0x2C:                           # cursorCommand
            s = self.b()
            c = s & 0x1F
            if c == 10:
                B(s, 0x80); B(s, 0x40)
            elif c == 11:
                B(s, 0x80); B(s, 0x40); B(s, 0x20)
            elif c in (12, 13):
                B(s, 0x80)
            elif c == 14:
                self.vararg()
        elif op in (0x30, 0xB0):                   # matrixOps
            s = self.b()
            if s & 0x1F in (1, 2, 3):
                B(s, 0x80); B(s, 0x40)
        elif op in (0x3F, 0x7F, 0xBF, 0xFF):       # drawBox
            W(op, 0x80); W(op, 0x40); s = self.b(); W(s, 0x80); W(s, 0x40); B(s, 0x20)
        elif op in (0x5C, 0xDC):
            s = self.b()
            if s & 0x1F == 3:
                W(s, 0x80)
        elif op == 0xCC:                           # pseudoRoom
            self.b()
            while self.b() != 0:
                pass
        elif op == 0xAC:                           # expression
            self.var()
            while True:
                s = self.b()
                if s == 0xFF:
                    break
                if s & 0x1F == 1:
                    W(s, 0x80)
                elif s & 0x1F == 6:
                    self.op()
        elif op == 0xAE:                           # wait
            s = self.b()
            if s & 0x1F == 1:
                B(s, 0x80)
        elif op == 0xA7 and small:
            raise BadOpcode('saveLoadVars not decoded')
        else:
            shape = SHAPES.get(op)
            if shape is None:
                raise BadOpcode('opcode %02x at %x' % (op, at))
            self.shape(op, shape)

    def shape(self, op, shape):
        for a in shape:
            if a == 'R':
                self.var()
            elif a == 'J':
                self.w()
            elif a == 'b':
                self.b()
            elif a == 'w':
                self.w()
            elif a == 'V':
                self.vararg()
            elif a == 'C':
                while True:
                    s = self.b()
                    if s == 0xFF:
                        break
                    self.W(s, 0x80)
            elif a == 'X':                         # doSentence 0xFE: stop, no objects
                if not op & 0x80 and self.d[self.p - 1] == 0xFE:
                    return
            elif a == 'D':                         # v5 drawObject sub-op
                s = self.b()
                if s & 0x1F == 1:
                    self.W(s, 0x80); self.W(s, 0x40)
                elif s & 0x1F == 2:
                    self.W(s, 0x80)
            elif a == 'S3':                        # saveRestoreVerbs
                s = self.b()
                self.B(s, 0x80); self.B(s, 0x40); self.B(s, 0x20)
            elif a == 'n':                         # setVarRange values
                for _ in range(self.b()):
                    self.w() if op & 0x80 else self.b()
            else:
                mask = {'1': 0x80, '2': 0x40, '3': 0x20}[a[1]]
                (self.B if a[0] == 'B' else self.W)(op, mask)


# A record: (room, where, number, kind, text, disk, block, offset). block is
# the block's ordinal in its file - the same across two builds of one game
# even when the block sizes differ.
Rec = collections.namedtuple('Rec', 'room where number kind text disk block offset')


def _walk_small(d, off, end, out, lf):
    while off + 6 <= end:
        sz = struct.unpack_from('<I', d, off)[0]
        tag = d[off + 4:off + 6]
        if sz < 6 or off + sz > end:
            break      # a sound block's size field does not cover its data; nothing after it matters
        out.append((lf, tag, off, sz))
        if tag in (b'LE', b'RO'):
            _walk_small(d, off + 6, off + sz, out, lf)
        elif tag == b'LF':
            _walk_small(d, off + 8, off + sz, out, (struct.unpack_from('<H', d, off + 6)[0], off))
        off += sz
    return out


def v4_strings(gamedir):
    idx = open(os.path.join(gamedir, '000.LFL'), 'rb').read()
    if idx[4:6] != b'RN':
        idx = dexor(idx)
    glob = {}
    off = 0
    while off + 6 <= len(idx):
        sz = struct.unpack_from('<I', idx, off)[0]
        if idx[off + 4:off + 6] == b'0S':
            for i in range(struct.unpack_from('<H', idx, off + 6)[0]):
                room, o = struct.unpack_from('<BI', idx, off + 8 + i * 5)
                if room:
                    glob[(room, o)] = i       # offset from the room's RO block
        off += sz
    out = []
    for disk in range(1, 10):
        path = os.path.join(gamedir, 'DISK%02d.LEC' % disk)
        if not os.path.exists(path):
            continue
        d = dexor(open(path, 'rb').read())
        for bi, (lf, tag, boff, sz) in enumerate(_walk_small(d, 0, len(d), [], None)):
            if lf is None:
                continue
            room, lfoff = lf
            body, end = boff + 6, boff + sz
            if tag == b'SC':
                ctx, start = (0, WIO_GLOBAL, glob.get((room, boff - lfoff - 8), -1)), body
            elif tag in (b'EN', b'EX'):
                ctx, start = (room, WIO_ROOM, 0), body
            elif tag == b'LS':
                ctx, start = (room, WIO_LOCAL, d[body]), body + 1
            elif tag == b'OC':
                name = boff + d[boff + 18]
                n = res_str_len(d, name)
                out.append(Rec(room, WIO_ROOM, 0, 'obname', d[name:name + n], disk, bi, name))
                entries = []
                q = boff + 19
                while d[q]:
                    entries.append(struct.unpack_from('<H', d, q + 1)[0])
                    q += 3
                if not entries:
                    continue
                ctx, start = (room, WIO_ROOM, 0), max(boff + min(entries), name + n + 1)
            else:
                continue
            try:
                strings = Walker(d, start, end, 4).run()
            except (BadOpcode, IndexError) as e:
                raise SystemExit('%s: %s block at 0x%x (room %d): %s' % (path, tag.decode(), boff, room, e))
            out += [Rec(*ctx, kind, s, disk, bi, so) for so, kind, s in strings]
    return out


def v5_strings(gamedir, name):
    idx = dexor(open(os.path.join(gamedir, name + '.000'), 'rb').read())
    glob = {}
    off = 0
    while off + 8 <= len(idx):
        sz = struct.unpack_from('>I', idx, off + 4)[0]
        if idx[off:off + 4] == b'DSCR':
            n = struct.unpack_from('<H', idx, off + 8)[0]
            rooms = idx[off + 10:off + 10 + n]
            offs = struct.unpack_from('<%dI' % n, idx, off + 10 + n)
            for i in range(n):
                if rooms[i]:
                    glob[(rooms[i], offs[i])] = i
        off += sz
    d = dexor(open(os.path.join(gamedir, name + '.001'), 'rb').read())
    loff = d.index(b'LOFF')
    roomat = {}
    for i in range(d[loff + 8]):
        r, o = struct.unpack_from('<BI', d, loff + 9 + i * 5)
        roomat[o] = r

    blocks = []

    def walk(off, end, obcd):
        while off + 8 <= end:
            tag = d[off:off + 4]
            sz = struct.unpack_from('>I', d, off + 4)[0]
            if sz < 8 or off + sz > end:
                break
            blocks.append((tag, off, sz, obcd))
            if tag in (b'LECF', b'LFLF', b'ROOM'):
                walk(off + 8, off + sz, None)
            elif tag == b'OBCD':
                walk(off + 8, off + sz, off)
            off += sz
    walk(0, len(d), None)

    out = []
    room = room_off = None
    for bi, (tag, boff, sz, obcd) in enumerate(blocks):
        if tag == b'ROOM':
            room, room_off = roomat.get(boff), boff
            continue
        body, end = boff + 8, boff + sz
        if tag == b'SCRP':
            ctx, start = (0, WIO_GLOBAL, glob.get((room, boff - room_off), -1)), body
        elif tag in (b'ENCD', b'EXCD'):
            ctx, start = (room, WIO_ROOM, 0), body
        elif tag == b'LSCR':
            ctx, start = (room, WIO_LOCAL, d[body]), body + 1
        elif tag == b'OBNA':
            n = res_str_len(d, body)
            out.append(Rec(room, WIO_ROOM, 0, 'obname', d[body:body + n], 0, bi, body))
            continue
        elif tag == b'VERB':
            entries = []
            q = body
            while d[q]:
                entries.append(struct.unpack_from('<H', d, q + 1)[0])
                q += 3
            if not entries:
                continue
            ctx, start = (room, WIO_ROOM, 0), boff + min(entries)
        else:
            continue
        try:
            strings = Walker(d, start, end, 5).run()
        except (BadOpcode, IndexError) as e:
            raise SystemExit('%s block at 0x%x (room %s): %s' % (tag.decode(), boff, room, e))
        out += [Rec(*ctx, kind, s, 0, bi, so) for so, kind, s in strings]
    return out


def game_strings(gamedir, v5name=None):
    if v5name:
        return v5_strings(gamedir, v5name)
    if os.path.exists(os.path.join(gamedir, '000.LFL')):
        return v4_strings(gamedir)
    for n in os.listdir(gamedir):
        if n.lower().endswith('.000'):
            return v5_strings(gamedir, n[:-4])
    raise SystemExit('%s: no 000.LFL and no <name>.000' % gamedir)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('gamedir')
    ap.add_argument('--v5', metavar='NAME', help='v5 file name stem (monkey for monkey.000)')
    ap.add_argument('--limit', type=int, default=0)
    a = ap.parse_args()
    recs = game_strings(a.gamedir, a.v5)
    for r in recs[:a.limit or len(recs)]:
        print('%d\t%d\t%d\t%s\t%r' % (r.room, r.where, r.number, r.kind, r.text))
    kinds = collections.Counter(r.kind for r in recs)
    print('# scummscript: %d strings (%s)' % (len(recs), ', '.join('%s %d' % kv for kv in kinds.most_common())))


if __name__ == '__main__':
    main()
