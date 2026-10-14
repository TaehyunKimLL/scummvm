#!/usr/bin/env python3
"""Bake a 1 bpp SVF from a precomposed Hangul bitmap font (BDF) and an ASCII bitmap font (BDF or HBF).

The MI1/MI2 L faces for the 24 px tall cells (M1L0, M1L4, M2L0, M2L4, M2L7) are drawn from a real 24 dot Hangul face
rather than a 16 px face in a 24 px cell (tools/korean/SCUMM_FONTS.md has the commands):

    python3 bdfface.py --hangul batang24b.bdf --ascii 12x24.hbf --cell 24x24 \\
        --chars-from korean.trs -o M2L4.SVF

--hangul    a BDF whose ENCODING is a KS X 1001 GL code (CHARSET_REGISTRY ksx1001 / ksc5601, as Baekmuk's
            bdf/*.bdf) or a Unicode code point (iso10646). Only the syllables --chars-from names are written.
--ascii     a BDF or an HBF (12x24.hbf with its .bin / .bin.gz beside it); ASCII 0x20..0x7E is taken from it.
--ascent    the cell row of the ASCII baseline, which is also the SVF's ascent field (default: the ASCII face's own
            FONT_ASCENT). Syllables stand where the Hangul BDF puts them (its FONT_ASCENT as the baseline row, so a
            24 row face fills the 24 row cell); --hangul-ascent moves them.
--chars-from  what mkfont.py --chars-from reads (.trs, .map, plain text ...); only Hangul syllables are used.

The face stays precomposed (flag 5: variable width + marks at origin, as the other L faces): a syllable steps by
--hangul-advance (default: the BDF's DWIDTH of the first syllable), ASCII by its own width. U+2026 is three
square dots on the baseline and U+25A1 the missing-glyph box, both drawn here, so the SVF holds no glyph from
a face other than the two named.
"""

import argparse
import gzip
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:  # python -I drops the script's directory
    sys.path.insert(0, HERE)
import jamoset  # noqa: E402


class FaceError(Exception):
    pass


def _read(path, limit=-1):
    with open(path, "rb") as f:
        return f.read(limit)


def _gl_to_unicode(code):
    """KS X 1001 GL code (0x2121..0x7E7E) -> code point, or None."""
    try:
        return ord(bytes([(code >> 8) | 0x80, (code & 0xFF) | 0x80]).decode("cp949"))
    except (UnicodeDecodeError, TypeError):
        return None


class Face:
    """glyph(cp) -> (rows, w, advance) with rows as ints, bit (w-1-x) = pixel x, in the face's own box."""

    def __init__(self, ascent, glyphs, box_w, box_h):
        self.ascent, self._glyphs, self.box_w, self.box_h = ascent, glyphs, box_w, box_h

    def glyph(self, cp):
        return self._glyphs.get(cp)


def read_bdf(path, unicode_enc=None):
    t = _read(path).decode("latin-1")
    if not t.startswith("STARTFONT"):
        raise FaceError(f"{path}: not a BDF")
    reg = re.search(r'CHARSET_REGISTRY\s+"?([^"\n]*)', t)
    reg = (reg.group(1).lower() if reg else "")
    uni = unicode_enc if unicode_enc is not None else reg.startswith("iso10646")
    asc = re.search(r"FONT_ASCENT\s+(\d+)", t)
    desc = re.search(r"FONT_DESCENT\s+(\d+)", t)
    fb = re.search(r"FONTBOUNDINGBOX\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)", t)
    if not asc or not fb:
        raise FaceError(f"{path}: no FONT_ASCENT or FONTBOUNDINGBOX")
    ascent = int(asc.group(1))
    glyphs = {}
    for block in t.split("STARTCHAR")[1:]:
        block = block.split("ENDCHAR")[0]
        enc = re.search(r"^ENCODING\s+(-?\d+)", block, re.M)
        bbx = re.search(r"^BBX\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)", block, re.M)
        bm = re.search(r"^BITMAP[ \t]*\n(.*)", block, re.M | re.S)
        dw = re.search(r"^DWIDTH\s+(-?\d+)", block, re.M)
        if not enc or not bbx or not bm:
            continue
        code = int(enc.group(1))
        cp = code if uni else _gl_to_unicode(code)
        if cp is None or cp < 0:
            continue
        bw, bh, bx, by = map(int, bbx.groups())
        nb = (bw + 7) // 8
        lines = bm.group(1).split()
        if bw == 0 or bh == 0 or len(lines) < bh:
            glyphs[cp] = (bw, bh, bx, by, [], int(dw.group(1)) if dw else bw)
            continue
        rows = [int(x[:nb * 2], 16) >> (nb * 8 - bw) for x in lines[:bh]]
        glyphs[cp] = (bw, bh, bx, by, rows, int(dw.group(1)) if dw else bw)
    return Face(ascent, glyphs, int(fb.group(1)), int(fb.group(2))), glyphs


def read_hbf(path):
    """An HBF (HBF_CODE_RANGE lines naming bitmap files beside it): single-byte ranges only; returns the same
    glyph tuples as read_bdf."""
    t = _read(path).decode("latin-1")
    if "HBF_START_FONT" not in t:
        raise FaceError(f"{path}: not an HBF")
    bb = re.search(r"HBF_BITMAP_BOUNDING_BOX\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)", t)
    asc = re.search(r"FONT_ASCENT\s+(\d+)", t)
    if not bb or not asc:
        raise FaceError(f"{path}: no HBF_BITMAP_BOUNDING_BOX or FONT_ASCENT")
    bw, bh, bx, by = map(int, bb.groups())
    nb = (bw + 7) // 8
    gsize = nb * bh
    glyphs = {}
    for m in re.finditer(r"HBF_CODE_RANGE\s+0x([0-9A-Fa-f]+)-0x([0-9A-Fa-f]+)\s+(\S+)\s+(\d+)", t):
        lo, hi, name, off = int(m.group(1), 16), int(m.group(2), 16), m.group(3), int(m.group(4))
        if hi > 0xFF:
            raise FaceError(f"{path}: a code range above 0xFF is not an ASCII face")
        bin_path = os.path.join(os.path.dirname(os.path.abspath(path)), name)
        if os.path.exists(bin_path):
            data = _read(bin_path)
        elif os.path.exists(bin_path + ".gz"):
            with gzip.open(bin_path + ".gz") as gf:
                data = gf.read()
        else:
            raise FaceError(f"{path}: bitmap file {name} (or {name}.gz) is missing")
        for code in range(lo, hi + 1):
            p = off + (code - lo) * gsize
            if p + gsize > len(data):
                break
            rows = [int.from_bytes(data[p + y * nb:p + (y + 1) * nb], "big") >> (nb * 8 - bw) for y in range(bh)]
            glyphs[code] = (bw, bh, bx, by, rows, bw)
    return Face(int(asc.group(1)), glyphs, bw, bh), glyphs


def read_face(path, unicode_enc=None):
    head = _read(path, 64)
    return read_hbf(path) if b"HBF_START_FONT" in head else read_bdf(path, unicode_enc)


def place(glyph, cell_w, cell_h, ascent):
    """The glyph's rows in the cell: its baseline (BDF y offset 0) on row @p ascent, its origin on column 0. Ink
    that falls outside the cell is dropped; returns (rows, clipped)."""
    bw, bh, bx, by, rows, adv = glyph
    top = ascent - (by + bh)
    out = [0] * cell_h
    clipped = False
    for k, r in enumerate(rows):
        y = top + k
        if not 0 <= y < cell_h:
            clipped = clipped or bool(r)
            continue
        sh = cell_w - (bx + bw)
        v = (r << sh) if sh >= 0 else (r >> -sh)
        if sh < 0 and r & ((1 << -sh) - 1):
            clipped = True
        if bx < 0:
            clipped = True
        out[y] = v & ((1 << cell_w) - 1)
    return out, clipped


def dots(cell_w, cell_h, ascent, adv):
    """U+2026: three 3x3 dots with their bottom on the baseline row, 4 px apart, centred in the advance."""
    left = (adv - 11) // 2
    rows = [0] * cell_h
    for y in range(ascent - 3, ascent):
        v = 0
        for k in range(3):
            v |= 0b111 << (cell_w - (left + k * 4 + 3))
        rows[y] = v
    return rows


def box(cell_w, cell_h, ascent):
    """U+25A1: a 2 px square outline, 16 px wide and 16 px tall, standing on the baseline."""
    n = 16
    x0 = (cell_w - n) // 2
    rows = [0] * cell_h
    for k in range(n):
        y = ascent - n + k
        if not 0 <= y < cell_h:
            continue
        if k in (0, 1, n - 2, n - 1):
            v = ((1 << n) - 1) << (cell_w - x0 - n)
        else:
            v = (0b11 << (cell_w - x0 - 2)) | (0b11 << (cell_w - x0 - n))
        rows[y] = v
    return rows


def pack_rows(rows, w):
    nb = (w + 7) // 8
    return b"".join((r << (nb * 8 - w)).to_bytes(nb, "big") for r in rows)


def ink(rows, w):
    cols = 0
    for r in rows:
        cols |= r
    used = [x for x in range(w) if cols >> (w - 1 - x) & 1]
    return (used[0], used[-1] - used[0] + 1) if used else (0, 0)


def write_face(cell_w, cell_h, ascent, glyphs):
    """@p glyphs: {cp: (packed, advance, ink x, ink w)}. A plain variable-width SVFN v2 with the flags and header of
    the other L faces (flags 5, no code page, shadow byte 0xFF)."""
    items = sorted(glyphs.items())
    n = len(items)
    cmap = b"".join(struct.pack("<II", cp, i) for i, (cp, g) in enumerate(items))
    metrics = b"".join(struct.pack("<BbBB", min(255, g[1]), g[2], g[3], 0) for cp, g in items)
    data = b"".join(g[0] for cp, g in items)
    cmap_off = jamoset.HEADER_V2
    metrics_off = cmap_off + len(cmap)
    data_off = metrics_off + len(metrics)
    head = struct.pack(jamoset.HEADER, jamoset.MAGIC, jamoset.VERSION_CMAP,
                       jamoset.FLAG_VARIABLE | jamoset.FLAG_MARKS_AT_ORIGIN, 1, jamoset.SHADOW_NONE, 0, n, cell_w,
                       cell_h, min(255, ascent), 0, 0, metrics_off, data_off, len(data))
    return head + struct.pack("<I", cmap_off) + cmap + metrics + data


def fill_rows(placed, absent, cell_w, cell_h, mask_font=None, pylib=None):
    """Rows for the syllables @p absent that the face lacks (a KS X 1001 face holds 2,350 of the 11,172): jamo pieces
    are fitted to the face's own syllables @p placed ({code point: rows}) the way jamoset.py --from-svf does and the
    missing ones composed from them. Returns {code point: rows}."""
    jf = jamoset._jamofit()
    layout = jamoset.LAYOUTS["844"]
    targets = {cp - 0xAC00: jf.from_rows(rows, cell_w) for cp, rows in placed.items() if any(rows)}
    out, _ = jamoset.fit_font(layout, targets, cell_w, cell_h, None, None, None, None, False, mask_font, None, 12,
                              pylib, mask_font is not None)
    f = jamoset.parse_svfn(out)
    t = jamoset.unpack(f.jset)
    res = {}
    for cp in absent:
        g = jamoset.compose(f, t, cp)
        nb = (cell_w + 7) // 8
        res[cp] = [int.from_bytes(g[y * nb:(y + 1) * nb], "big") >> (nb * 8 - cell_w) for y in range(cell_h)]
    return res


def build(hangul, ascii_face, cell_w, cell_h, ascent, chars, hangul_advance=None, hangul_ascent=None, fill=False,
          mask_font=None, pylib=None):
    """Returns (font bytes, info dict). @p ascent is the cell row of the ASCII baseline (and of the dots and the box);
    syllables stand on @p hangul_ascent (default: the Hangul face's own FONT_ASCENT)."""
    hface, hglyphs = hangul
    aface, aglyphs = ascii_face
    out = {}
    clipped = []
    missing = []
    adv_h = hangul_advance
    for cp in sorted(c for c in chars if 0xAC00 <= c <= 0xD7A3):
        g = hglyphs.get(cp)
        if g is None or not g[4]:
            missing.append(cp)
            continue
        if adv_h is None:
            adv_h = g[5]
        rows, clip = place(g, cell_w, cell_h, hangul_ascent if hangul_ascent is not None else hface.ascent)
        if clip:
            clipped.append(cp)
        ix, iw = ink(rows, cell_w)
        out[cp] = (pack_rows(rows, cell_w), adv_h, ix, iw)
    if adv_h is None:
        raise FaceError("no requested Hangul syllable is in the Hangul face")
    filled = 0
    if fill and missing:
        placed = {}
        for cp, g in hglyphs.items():
            if 0xAC00 <= cp <= 0xD7A3 and g[4]:
                placed[cp] = place(g, cell_w, cell_h, hangul_ascent if hangul_ascent is not None else hface.ascent)[0]
        for cp, rows in fill_rows(placed, missing, cell_w, cell_h, mask_font, pylib).items():
            ix, iw = ink(rows, cell_w)
            out[cp] = (pack_rows(rows, cell_w), adv_h, ix, iw)
            filled += 1
    for cp in range(0x20, 0x7F):
        g = aglyphs.get(cp)
        if g is None:
            continue
        rows, clip = place(g, cell_w, cell_h, ascent)
        if clip:
            clipped.append(cp)
        ix, iw = ink(rows, cell_w)
        out[cp] = (pack_rows(rows, cell_w), g[5], ix, iw)
    sp = aglyphs.get(0x20)
    adv_sp = sp[5] if sp else (aface.box_w or 12)
    dots_rows = dots(cell_w, cell_h, ascent, adv_sp)
    ix, iw = ink(dots_rows, cell_w)
    out[0x2026] = (pack_rows(dots_rows, cell_w), adv_sp, ix, iw)
    box_rows = box(cell_w, cell_h, ascent)
    ix, iw = ink(box_rows, cell_w)
    out[0x25A1] = (pack_rows(box_rows, cell_w), hangul_advance or adv_h, ix, iw)
    info = {"cell": [cell_w, cell_h], "ascent": ascent, "syllables": sum(1 for c in out if 0xAC00 <= c <= 0xD7A3),
            "syllable_advance": adv_h, "missing_syllables": len(missing) - filled, "filled": filled, "clipped": len(clipped), "glyphs": len(out)}
    return write_face(cell_w, cell_h, ascent, out), info


def main():
    ap = argparse.ArgumentParser(description="1 bpp SVF from a precomposed Hangul BDF and an ASCII BDF/HBF")
    ap.add_argument("--hangul", required=True, help="Hangul BDF (KS X 1001 or Unicode ENCODING)")
    ap.add_argument("--ascii", required=True, help="ASCII BDF or HBF")
    ap.add_argument("--cell", default="24x24", help="cell WxH (default 24x24)")
    ap.add_argument("--ascent", type=int, help="cell row of the ASCII baseline (default: the ASCII face's FONT_ASCENT)")
    ap.add_argument("--hangul-ascent", type=int, help="cell row of the Hangul BDF's baseline (default: its FONT_ASCENT)")
    ap.add_argument("--chars-from", nargs="+", required=True, help="files naming the characters (mkfont.py rules)")
    ap.add_argument("--hangul-advance", type=int, help="syllable advance (default: the BDF's DWIDTH)")
    ap.add_argument("--fill-absent", action="store_true", help="compose the requested syllables the face lacks (outside "
                    "KS X 1001) from jamo pieces fitted to the face (jamoset.py --from-svf's fit; roughly drawn, but "
                    "not a missing-glyph box)")
    ap.add_argument("--mask-font", help="--fill-absent: mask font as for jamoset.py (default: $JAMOSET_MASK_FONT or the "
                    "shipped NotoSansKR when freetype/uharfbuzz import; 'none' = free pieces)")
    ap.add_argument("--pylib", help="directory holding freetype and uharfbuzz (also $JAMOSET_PYLIB)")
    ap.add_argument("-o", "--output", required=True)
    args = ap.parse_args()
    try:
        import mkfont
        cps, counts = mkfont.collect_chars_from_files(args.chars_from)
        cell_w, cell_h = (int(x) for x in args.cell.lower().split("x"))
        hangul = read_face(args.hangul)
        ascii_face = read_face(args.ascii, unicode_enc=True)
        ascent = args.ascent if args.ascent is not None else ascii_face[0].ascent
        mask = None
        if args.fill_absent and (args.mask_font or "").lower() != "none":
            jf = jamoset._jamofit()
            mask = args.mask_font or jamoset.default_mask_font()
            if mask and not jf.libs_available(args.pylib):
                print("bdfface: no freetype/uharfbuzz, fitting without the mask font", file=sys.stderr)
                mask = None
        data, info = build(hangul, ascii_face, cell_w, cell_h, ascent, set(cps), args.hangul_advance,
                           args.hangul_ascent, args.fill_absent, mask, args.pylib)
    except (FaceError, OSError) as e:
        sys.exit(f"bdfface: {e}")
    with open(args.output, "wb") as f:
        f.write(data)
    print(f"{args.output}: {len(data)} bytes, {info['syllables']} syllables, cell {cell_w}x{cell_h}, ascent {ascent}, "
          f"step {info['syllable_advance']}, {info['filled']} absent syllables composed, "
          f"{info['missing_syllables']} left absent, "
          f"{info['clipped']} glyphs clipped")


if __name__ == "__main__":
    main()
