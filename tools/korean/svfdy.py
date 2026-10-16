#!/usr/bin/env python3
"""svfdy.py - set the per-glyph vertical delta (dy) of an SVFN font (graphics/hires_text/README.md, "Per-glyph dy").

A glyph with dy is drawn dy rows below its cell position (negative: above); its ink may then leave the cell, e.g. a
descender below it. The dy is metrics byte 3 of the glyph, the font-wide range header bytes 17 (dy_min <= 0) and 18
(dy_max >= 0); an engine that predates them ignores both and draws every glyph at its cell position. Only a font with a
metrics table (flags bit 0) and a code point table (version 2) can carry it.

The file is patched in place of a copy: every byte but those is kept, so a jamo-set face's pieces, JSET block and
everything else stay byte-identical. The range is worked out again from all glyphs.

  svfdy.py IN.svf OUT.svf --range 0x20-0x7e --dy 3        # one dy for a range of code points
  svfdy.py IN.svf OUT.svf --set 0x67,0x6a,0x70-0x71,0x79=3 --set 0x5e=-1
  svfdy.py IN.svf OUT.svf --json dy.json                  # {"0x67": 3, "106": 3, "y": 3}
  svfdy.py IN.svf OUT.svf --clear                          # every dy back to 0
  svfdy.py IN.svf --show                                   # the range and the glyphs with a dy

Code points the font lacks are skipped (counted). A dy on a jamo piece (U+F0000..) is refused: the engine composes a
syllable inside its cell.
"""

import argparse
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import jamoset  # noqa: E402

DY_MIN_AT, DY_MAX_AT = 17, 18


def parse_cps(text):
    """'0x20-0x7e,0x2026,a' -> [code points]. A single character stands for itself."""
    out = []
    for part in text.split(","):
        part = part.strip()
        if not part:
            continue
        if len(part) == 1:
            out.append(ord(part))
            continue
        if "-" in part[1:]:
            k = part.index("-", 1)
            lo, hi = int(part[:k], 0), int(part[k + 1:], 0)
            if hi < lo:
                raise ValueError(f"range {part} runs backwards")
            out.extend(range(lo, hi + 1))
        else:
            out.append(int(part, 0))
    return out


def json_dy(path):
    """{code point: dy} from a JSON object whose keys are '0x67', '103' or 'g'."""
    with open(path, encoding="utf-8") as fh:
        raw = json.load(fh)
    out = {}
    for k, v in raw.items():
        cp = ord(k) if len(k) == 1 else int(k, 0)
        out[cp] = int(v)
    return out


def table(data):
    """(cmap {cp: index}, metrics_off, count, cell_h) of a version 2 SVFN with a metrics table."""
    (magic, version, flags, _bpp, _sh, _cpg, count, _cw, ch, _asc, _r0, _r1, metrics_off, _do,
     _ds) = struct.unpack_from(jamoset.HEADER, data)
    if magic != jamoset.MAGIC:
        raise ValueError("not an SVFN font")
    if version != jamoset.VERSION_CMAP:
        raise ValueError(f"version {version}: only a version 2 font (code point table) can be given a dy")
    if not flags & jamoset.FLAG_VARIABLE:
        raise ValueError("the font has no metrics table (flags bit 0), so no place for a dy")
    (cmap_off,) = struct.unpack_from("<I", data, jamoset.HEADER_V2 - 4)
    cmap = {}
    for k in range(count):
        cp, idx = struct.unpack_from("<II", data, cmap_off + 8 * k)
        cmap[cp] = idx
    if metrics_off + 4 * count > len(data):
        raise ValueError("the metrics table runs past the end of the file")
    return cmap, metrics_off, count, ch


def current(data):
    """{index: dy} of every glyph, and the header range."""
    _cmap, metrics_off, count, _ch = table(data)
    dys = {i: struct.unpack_from("<b", data, metrics_off + 4 * i + 3)[0] for i in range(count)}
    return dys, struct.unpack_from("<bb", data, DY_MIN_AT)


def apply(data, want, clear=False):
    """The font @p data with the glyphs of @p want {cp: dy} given that dy (and every other glyph's kept, or 0 when
    @p clear), the header range recomputed. Returns (bytes, {"set": n, "missing": [cps]})."""
    cmap, metrics_off, count, ch = table(data)
    out = bytearray(data)
    if clear:
        for i in range(count):
            out[metrics_off + 4 * i + 3] = 0
    missing, n = [], 0
    for cp, dy in sorted(want.items()):
        if cp >= jamoset.PIECE_BASE:
            raise ValueError(f"U+{cp:04X} is a jamo piece: the engine composes a syllable in its cell, no dy")
        if not -128 <= dy <= 127:
            raise ValueError(f"dy {dy} of U+{cp:04X} does not fit a signed byte")
        if cp not in cmap:
            missing.append(cp)
            continue
        struct.pack_into("<b", out, metrics_off + 4 * cmap[cp] + 3, dy)
        n += 1
    vals = [struct.unpack_from("<b", out, metrics_off + 4 * i + 3)[0] for i in range(count)]
    lo, hi = jamoset.dy_range({i: v for i, v in enumerate(vals)}, ch)
    struct.pack_into("<bb", out, DY_MIN_AT, lo, hi)
    return bytes(out), {"set": n, "missing": missing, "dy_min": lo, "dy_max": hi}


def verify(before, after):
    """Only header bytes 17/18 and metrics byte 3 of a glyph may differ; the result parses and its dy checks."""
    _cmap, metrics_off, count, _ch = table(before)
    allowed = {DY_MIN_AT, DY_MAX_AT} | {metrics_off + 4 * i + 3 for i in range(count)}
    if len(before) != len(after):
        raise ValueError("length changed")
    bad = [k for k in range(len(before)) if before[k] != after[k] and k not in allowed]
    if bad:
        raise ValueError(f"{len(bad)} bytes outside the dy fields changed, first at {bad[0]}")
    f = jamoset.parse_svfn(after)
    jamoset.check_dy(f)
    return f


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input")
    ap.add_argument("output", nargs="?")
    ap.add_argument("--range", help="code points (e.g. 0x20-0x7e,0x2026) given --dy")
    ap.add_argument("--dy", type=int, help="the dy for --range")
    ap.add_argument("--set", action="append", default=[], metavar="CPS=DY", help="code points and their dy; repeatable")
    ap.add_argument("--json", help="a JSON object {code point: dy}")
    ap.add_argument("--clear", action="store_true", help="every glyph's dy to 0 first")
    ap.add_argument("--show", action="store_true", help="print the range and the glyphs that have a dy")
    a = ap.parse_args(argv)

    data = open(a.input, "rb").read()
    if a.show and not a.output:
        f = jamoset.parse_svfn(data)
        print(f"{a.input}: dy range {f.dy_min}..{f.dy_max}, {len(f.dy)} glyphs with a dy")
        for cp, v in sorted(f.dy.items()):
            print(f"  U+{cp:04X} {chr(cp) if 0x20 < cp < 0x7f else ''} {v:+d}")
        return 0
    if not a.output:
        ap.error("an output file is needed (or --show)")
    want = {}
    if (a.range is None) != (a.dy is None):
        ap.error("--range and --dy go together")
    if a.range is not None:
        for cp in parse_cps(a.range):
            want[cp] = a.dy
    for item in a.set:
        if "=" not in item:
            ap.error(f"--set {item}: CPS=DY")
        cps, dy = item.rsplit("=", 1)
        for cp in parse_cps(cps):
            want[cp] = int(dy)
    if a.json:
        want.update(json_dy(a.json))
    if not want and not a.clear:
        ap.error("nothing to set: --range/--dy, --set, --json or --clear")
    out, info = apply(data, want, a.clear)
    verify(data, out)
    with open(a.output, "wb") as fh:
        fh.write(out)
    miss = f", {len(info['missing'])} code points not in the font" if info["missing"] else ""
    print(f"{a.output}: dy set on {info['set']} glyphs, range {info['dy_min']}..{info['dy_max']}{miss}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
