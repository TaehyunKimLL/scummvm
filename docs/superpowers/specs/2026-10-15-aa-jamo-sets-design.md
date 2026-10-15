# Anti-aliased jamo sets, noise cleanup and adaptive set systems

Date: 2026-10-15. Code: `tools/korean/jamoaa.py`, `tools/korean/jamoadapt.py`, `tools/korean/jamoset.py`
(`--bpp 2`, `--clean`, `--layout adaptive[:NI,NM,NF]`, `--binarize`), tests `tools/korean/test_jamoaa.py`
(run by `test_jamoset.py`) and `test/graphics/hires_text_jamo.h`. Measurements: `~/work/scummvm/runs/aa-jamo/report.md`.

## Goal

A jamo-set face (SVFN v2, flag 0x2, JSET block) whose pieces carry 2 bpp coverage, made from an outline font, so a
true-colour game gets anti-aliased Hangul from a 30-60 KB face that composes all 11,172 syllables. The engine
already composes 1/2/8 bpp pieces by the per-pixel maximum (`glyph_source_jamo.cpp` `maxRow`); nothing changes there.
Two follow-ups apply to the 1 bpp fit too: cleaning noise out of fitted pieces, and letting the set system grow
past 8x4x4 where the font needs it (the engine allows 8 lead / 8 tail classes, 24 / 16 / 16 sets).

## Decisions

- **Rasteriser.** Outlines are read through a fontTools pen (glyf or CFF; composites decomposed) and rasterised in
  Python with exact-area signed accumulation (font-rs). It is linear in the contours, so a contour is rasterised
  once and any subset of a syllable's contours is a sum. No FreeType or numpy is needed for `--bpp 2` (the 1 bpp
  default still uses FreeType's mono renderer, unchanged). The frame (cell, ink box centred) is ttf_targets'.
- **Quantisation** is mkfont.py's (`(v * 3 + 127) // 255`, no gamma), so a jamo face matches the precomposed 2 bpp
  faces the tools already bake. Coverage 0.5 -> 128 -> level 2.
- **Regions.** A free 1 bpp fit (jamofit) of the 844 pieces to the coverage thresholded at 0.5 says which pixels
  each (set, jamo) piece owns.
- **Contour assignment** (generic: no font units, no donor glyph names). Contours are grouped by containment (90 %
  of a contour's ink inside a larger one: a hole or an overlapping part). A group goes to the slot whose fitted
  region (dilated 1 px) holds most of it. A group is a merged outline, split pixel by pixel by the regions, only
  when no single region holds 85 % of it and a second piece's own pixels hold 20 %: the free fit may hand a whole
  column of one jamo to a neighbour's piece when every user inks it, and that must not split a contour. The
  result per syllable is a raster per slot ("donors").
- **Consensus.** A piece is the per-pixel median of its donors over all syllables that use it, then refined by L1
  coordinate descent of its levels against the direct 2 bpp renders of the composed syllables (the exact per-pixel
  optimum given the other slots, as the 1 bpp fit does for Hamming error), `--aa-refine 2` passes.
- **Noise cleanup** (`--clean`; default on for `--bpp 2` and adaptive, off for the plain 1 bpp fit so existing
  faces rebuild identically): outlier donors (L1 to the median above median + 3 MAD) do not vote; a piece may only
  ink where at least a quarter of its donors do, dilated 1 px (1 bpp: as fit masks with the strict first pass;
  2 bpp: in the refinement); a level-1 pixel stays only where half the donors have ink; components of 1 px (cells
  up to 16) or 2 px with no full-coverage pixel are dropped (1 bpp: only when the composed error does not rise).
  For a bitmap source (`--from-svf`) donors are the targets split by the region fit. Measured: cleanup costs about
  0.015 IoU on the fitted syllables and halves the syllables with stray ink (composed ink more than a pixel from
  any target ink) on held-out syllables - the pieces stop carrying fragments of neighbouring jamo.
- **Adaptive sets** (`--layout adaptive[:NI,NM,NF]`, default 24,16,16). Lead classes: {ㄱ,ㅋ} plus the other 17
  initials clustered into 7 by how the medials after them look; tail classes: none plus the 27 tails clustered into
  7 by how the initials/medials above them look. A slot's set is a function of an item (initial: vowel x tail
  class; medial: lead class x tail class x vowel; final: vowel x lead class), starting from 844. Greedy: the
  2-medians split (L1 of thermometer-coded levels) with the largest drop in donor-to-median distance, while under
  the limit, each side >= 10 syllables and the gain >= 0.3 % of the start; then Lloyd passes move misplaced items.
  Unused (set, jamo) pieces take the piece of the set they were split from. From a bitmap source the regions are
  re-fitted with the found layout and the layout re-made (3 rounds). `--check` also verifies the written JSET
  tables are the layout's.
- **Ridge binarisation** (`--binarize ridge`, 1 bpp, opt-in): coverage >= `--threshold` plus 1-px ridges (a local
  maximum across a stroke with coverage >= 0.25 and >= 0.48 with its stronger neighbour), so hairlines between two
  pixel columns survive. Valley erosion is not adopted.

## Out of scope

Hinting/grid fitting of pieces, gamma, 8 bpp jamo sets, changing the engine's max composition (the join loss of
max vs sum is measured instead: it is below 0.01 levels per pixel for the fonts tried, jamo rarely share a pixel).
