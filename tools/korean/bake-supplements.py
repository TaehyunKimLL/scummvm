#!/usr/bin/env python3
"""bake-supplements.py PLAN.TSV --trs korean.trs --map X.MAP --faces DIR [--fonts DIR] [--check]

Bakes the supplement faces of a game's bitmap fonts. A base face holds ASCII and the 2,350 KS X 1001 syllables
(or composes every syllable from jamo pieces); the supplement holds the rest of what the bundle's text and the map
draw - Latin-1 letters, symbols, a syllable outside KS X 1001 - and the map chains it after the base
(face=dlg,dlgx). Each supplement has the cell, bit depth and baseline of its base, read from the base file, and
only the characters the base lacks: the characters the translation draws and the map's [glyphs] / missing= targets
of every font id (and preset) whose chain starts with that base, minus the base's own.

PLAN.TSV, one supplement a line (tab separated; the first four fields are those of scumm-fonts/*.tsv, so
game_maps.py's face-to-TrueType table reads them too):

    <supplement name>  <base SVF name>  <TTF[|fallback TTF]>  <size>  [extra mkfont.py flags]

The TTF is the face the base was baked from; "A|B" bakes each character A's cmap lacks from B at the same
size and baseline (mkfont.py --fallback-ttf), for a base whose own source has no Latin-1 accents. The
TTF fields may start with $FONTS. The bake fails, and removes the file, if the result lacks a wanted character.

A base that already has every character gets no supplement (the plan row is skipped and says so); --check
only lists what each would hold.
"""
import argparse
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
# jamoset, mkfont: the font tools of the svf-fonts submodule
SVF_TOOLS = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), 'dists', 'svf-fonts',
                         'tools')
sys.path.insert(1, SVF_TOOLS)
import facecoverage as fc  # noqa: E402
import jamoset  # noqa: E402


def read_plan(path):
    rows = []
    with open(path, encoding='utf-8') as f:
        for n, line in enumerate(f, 1):
            line = line.rstrip('\r\n')
            if not line.strip() or line.lstrip().startswith('#'):
                continue
            f_ = line.split('\t')
            if len(f_) < 4:
                raise SystemExit('%s line %d: expected name, base, ttf, size [flags]' % (path, n))
            rows.append((f_[0], f_[1], f_[2], int(f_[3]), f_[4].split() if len(f_) > 4 and f_[4].strip() else []))
    return rows


def needed(map_text, faces, text_cps, base_name):
    """The characters the chains starting with base_name must still get from a supplement."""
    sections = fc.parse_map(map_text)
    want = set()
    base_path = None
    for preset in ('U', 'L'):
        chains, targets = fc.plan(sections, preset)
        for i, chain in chains.items():
            if not chain:
                continue
            path = fc.resolve(chain[0], faces, [])
            if path is None or os.path.basename(path).lower() != base_name.lower():
                continue
            base_path = path
            want |= (set(text_cps) | targets[i]) - fc.face_chars(path)
    return base_path, sorted(want)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('plan')
    ap.add_argument('--trs', action='append', required=True)
    ap.add_argument('--map', required=True)
    ap.add_argument('--faces', required=True, help='folder of the base faces')
    ap.add_argument('-o', '--out', help='folder for the supplements (default: --faces)')
    ap.add_argument('--fonts', default=os.environ.get('FONTS', os.path.expanduser('~/scummvm-i18n/fonts')))
    ap.add_argument('--check', action='store_true', help='list the characters, bake nothing')
    a = ap.parse_args(argv)
    out = a.out or a.faces
    os.makedirs(out, exist_ok=True)
    with open(a.map, encoding='utf-8', errors='replace') as f:
        map_text = f.read()
    cps = fc.bundle_chars(a.trs)
    here = os.path.dirname(os.path.abspath(__file__))
    for name, base, ttf, size, flags in read_plan(a.plan):
        path, want = needed(map_text, a.faces, cps, base)
        if path is None:
            raise SystemExit('%s: no font id of %s starts its chain with %s' % (name, a.map, base))
        if not want:
            print('== %s: %s already has every character; no supplement ==' % (name, base))
            continue
        print('== %s: %d for %s: %s ==' % (name, len(want), base, fc.show(want)))
        if a.check:
            continue
        with open(path, 'rb') as f:
            svf = jamoset.parse_svfn(f.read())
        with tempfile.NamedTemporaryFile('w', suffix='.txt', encoding='utf-8', delete=False) as t:
            t.write(''.join(chr(c) for c in want) + '\n')
        try:
            main_ttf, _, fallback = ttf.partition('|')
            cmd = [sys.executable, os.path.join(SVF_TOOLS, 'mkfont.py'), main_ttf.replace('$FONTS', a.fonts),
                   os.path.join(out, name), '--size', str(size), '--cell', str(svf.cell_h), '--width',
                   str(svf.cell_w), '--bpp', str(svf.bpp), '--clip-cell', '--ascent', str(svf.ascent),
                   '--only-listed', '--chars-from', t.name,
                   '--require'] + flags
            if fallback:
                cmd += ['--fallback-ttf', fallback.replace('$FONTS', a.fonts)]
            subprocess.run(cmd, check=True)
            lacking = set(want) - fc.face_chars(os.path.join(out, name))
            if lacking:
                os.unlink(os.path.join(out, name))
                raise SystemExit('%s lacks %s: no face of the row has them' % (name, fc.show(sorted(lacking))))
        finally:
            os.unlink(t.name)
    return 0


if __name__ == '__main__':
    sys.exit(main())
