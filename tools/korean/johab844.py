#!/usr/bin/env python3
"""Put the Hangul of a raw 8x4x4 Johab bitmap font into an SVFN font.

A DOS-era Johab font (san-1.0 .KOR, Iyagi, Hanme-style raw files) holds no
syllables, only 360 jamo pieces: 8 initial sets of 20, 4 medial sets of 22
and 4 final sets of 28, each set's slot 0 blank. A syllable is the OR of its
initial, medial and (if any) final piece, the sets chosen from the vowel and
whether there is a final (docs/johab/2026-10-14-johab-font-research.md, 3).

The output is the base SVFN with every Hangul syllable it carries redrawn
from the Johab font; everything else (ASCII, symbols, compatibility jamo)
stays the base's, so a map keeps the same coverage. The base must be a 16x16
1 bpp font (KO2350.SVF), the size of a 16x16 8x4x4 file. If it carries a
width table, a redrawn syllable keeps its advance and gets its own ink box.

With --eng, the printable ASCII comes from a raw 8x16 DOS English font
(san-1.0 .ENG, 128 or 256 glyphs of 16 bytes) instead, drawn at the left of
the cell and stepping 8: the DOS Hangul systems paired each Johab font with
one, so the Latin keeps the face's look.

  python3 johab844.py GAM.KOR KO2350.SVF KOGAM.SVF --eng SAN.ENG
"""

import argparse
import struct
import sys

PIECES = 360
CELL = 16
GLYPH = CELL * CELL // 8

# Initial set by vowel (ㅏ..ㅣ), without and with a final; final set by vowel.
I844_NOFIN = (0, 0, 0, 0, 0, 0, 0, 0, 1, 3, 3, 3, 1, 2, 4, 4, 4, 2, 1, 3, 0)
I844_FIN = (5, 5, 5, 5, 5, 5, 5, 5, 6, 7, 7, 7, 6, 6, 7, 7, 7, 6, 6, 7, 5)
F844 = (0, 2, 0, 2, 1, 2, 1, 2, 3, 0, 2, 1, 3, 3, 1, 2, 1, 3, 3, 1, 1)
# Initials ㄱ and ㅋ take medial sets 0/2; the rest 1/3 (ㄲ is not in the group).
KK = (0, 15)

HEADER = "<4sHHBBHHBBBBHIII"
HEADER_V1 = struct.calcsize(HEADER)
FLAG_VARIABLE = 1


def pieces(cp):
    """The piece indices a syllable is drawn from, or None for a non-syllable."""
    if not 0xAC00 <= cp <= 0xD7A3:
        return None
    s = cp - 0xAC00
    lead, vowel, tail = s // 588, (s // 28) % 21, s % 28
    medial_set = (0 if lead in KK else 1) + (2 if tail else 0)
    out = [(I844_FIN if tail else I844_NOFIN)[vowel] * 20 + lead + 1,
           160 + medial_set * 22 + vowel + 1]
    if tail:
        out.append(248 + F844[vowel] * 28 + tail)
    return out


def compose(jamo, cp):
    glyph = bytearray(GLYPH)
    for i in pieces(cp):
        piece = jamo[i * GLYPH:(i + 1) * GLYPH]
        for k in range(GLYPH):
            glyph[k] |= piece[k]
    return bytes(glyph)


def read_svfn(path):
    data = open(path, "rb").read()
    (magic, version, flags, bpp, shadow, codepage, count, cell_w, cell_h,
     ascent, r0, r1, metrics_off, data_off, data_size) = struct.unpack_from(HEADER, data)
    if magic != b"SVFN" or version != 2:
        sys.exit(f"{path}: not an SVFN v2 (cmap) font")
    if bpp != 1 or (cell_w, cell_h) != (CELL, CELL):
        sys.exit(f"{path}: need a 16x16 1 bpp base, got {cell_w}x{cell_h} {bpp} bpp")
    (cmap_off,) = struct.unpack_from("<I", data, HEADER_V1)
    cmap = [struct.unpack_from("<II", data, cmap_off + 8 * k) for k in range(count)]
    return data, data_off, (metrics_off if flags & FLAG_VARIABLE else 0), cmap


def ink_columns(glyph):
    """(first inked column, inked width) of a 16x16 1 bpp glyph; (0, 0) if blank."""
    cols = 0
    for y in range(CELL):
        cols |= int.from_bytes(glyph[2 * y:2 * y + 2], "big")
    if not cols:
        return 0, 0
    used = [x for x in range(CELL) if cols >> (CELL - 1 - x) & 1]
    return used[0], used[-1] - used[0] + 1


def main():
    ap = argparse.ArgumentParser(description="raw 8x4x4 Johab Hangul -> SVFN (over a base SVFN)")
    ap.add_argument("johab", help="raw 8x4x4 16x16 font (11520 bytes)")
    ap.add_argument("base", help="SVFN v2, 16x16 1 bpp, for everything that is not a syllable")
    ap.add_argument("output")
    ap.add_argument("--eng", help="raw 8x16 English font for ASCII 0x21-0x7E (2048 or 4096 bytes)")
    args = ap.parse_args()

    eng = None
    if args.eng:
        eng = open(args.eng, "rb").read()
        if len(eng) not in (2048, 4096):
            sys.exit(f"{args.eng}: {len(eng)} bytes, a raw 8x16 font has 2048 or 4096")

    jamo = open(args.johab, "rb").read()
    if len(jamo) != PIECES * GLYPH:
        sys.exit(f"{args.johab}: {len(jamo)} bytes, a 16x16 8x4x4 font has {PIECES * GLYPH}")
    blank = [s * 20 for s in range(8)] + [160 + s * 22 for s in range(4)] + [248 + s * 28 for s in range(4)]
    if any(any(jamo[i * GLYPH:(i + 1) * GLYPH]) for i in blank):
        sys.exit(f"{args.johab}: a set's slot 0 is not blank; not an 8x4x4 layout")

    data, data_off, metrics_off, cmap = read_svfn(args.base)
    out = bytearray(data)
    redrawn = 0
    latin = 0
    for cp, index in cmap:
        if eng and 0x21 <= cp <= 0x7E:
            # Each 8-pixel row goes in the left byte of the cell's 2-byte row.
            glyph = bytearray(GLYPH)
            glyph[0::2] = eng[cp * 16:cp * 16 + 16]
            at = data_off + index * GLYPH
            out[at:at + GLYPH] = glyph
            if metrics_off:
                m = metrics_off + index * 4
                ink_x, ink_w = ink_columns(glyph)
                out[m:m + 3] = struct.pack("<BbB", 8, ink_x, ink_w)
            latin += 1
        if pieces(cp) is not None:
            at = data_off + index * GLYPH
            glyph = compose(jamo, cp)
            out[at:at + GLYPH] = glyph
            if metrics_off:
                # Metrics per glyph: advance, bearingX, ink width, reserved.
                m = metrics_off + index * 4
                ink_x, ink_w = ink_columns(glyph)
                out[m + 1:m + 3] = struct.pack("<bB", ink_x, ink_w)
            redrawn += 1
    open(args.output, "wb").write(out)
    if eng and not metrics_off:
        print(f"{args.output}: the base has no width table; ASCII keeps the full cell step")
    print(f"{args.output}: {redrawn} syllables from {args.johab}, {latin} ASCII from {args.eng}, "
          f"{len(cmap) - redrawn - latin} other glyphs from {args.base}")


if __name__ == "__main__":
    main()
