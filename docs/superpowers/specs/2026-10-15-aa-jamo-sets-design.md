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

- **Advance.** A `--bpp 2` face steps a syllable by the font's most common syllable advance when that is below the
  cell (NanumGothic Bold 21 px in a 22x24 cell steps 20, as the precomposed NGOTB21A does).
- **Rasteriser.** Outlines are read through a fontTools pen (glyf or CFF; composites decomposed) and rasterised in
  Python with exact-area signed accumulation (font-rs). It is linear in the contours, so a contour is rasterised
  once and any subset of a syllable's contours is a sum. No FreeType or numpy is needed for `--bpp 2` (the 1 bpp
  default still uses FreeType's mono renderer, unchanged). The frame (cell, ink box centred) is ttf_targets'.
- **Quantisation** is mkfont.py's (`(v * 3 + 127) // 255`, no gamma), so a jamo face matches the precomposed 2 bpp
  faces the tools already bake. Coverage 0.5 -> 128 -> level 2.
- **Regions.** A free 1 bpp fit (jamofit) of the 844 pieces to the coverage thresholded at 0.5 says which pixels
  each (set, jamo) piece owns.
- **Contour assignment** (generic: no font units, no donor glyph names). Contours are grouped by containment (90 %
  of a contour's ink inside a larger one: a hole or an overlapping part). A group that no single fitted region
  (dilated 1 px) holds 85 % of while a second piece's own pixels hold 20 % is a merged outline, split pixel by pixel
  by the regions. Otherwise the other syllables decide ("peers"): for each slot, the syllables of the same set of
  that slot with the same jamo there (and other jamo elsewhere) against those with another jamo there; a group the
  slot draws is present in the first and absent from the second, a group another slot draws is in both alike. The
  presence is the share of the group's ink in the peer's ink, half at the best one-pixel shift and half in place
  (a dilation would let the bar of ㅡ stand for the bar and tick of ㅗ). The slot whose difference is >= 0.15 and
  0.05 above the next wins whole. A group they do not settle (a merged outline: NanumGothic draws ㄴ and ㅗ of 녹
  as one contour) is split pixel by pixel by the same difference taken per pixel, and the regions decide the pixels
  no slot leads by 0.05. The region fit alone is not
  enough: the free fit hands a stroke every user of a piece shares (the bar of ㅗ ㅛ ㅜ ㅠ ㅡ under one initial set)
  to the initial, and then a composed syllable outside the fitted ones (Paperlogy draws 2,780 of 11,172) loses its
  vowel's bar (똠 drawn as 뜸). The result per syllable is a raster per slot ("donors").
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
  class; medial: lead class x tail class; final: vowel x lead class), starting from 844. Greedy: the
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

## Follow-ups (2026-10-15, later the same day)

- **Region layout.** The region fit and the peers use 844 with one medial set per tail class for all initials
  (`jamoaa.region_layout`): in 844 the initials ㄱ ㅋ have their own medial sets, the free fit gives ㄱ's stroke to
  the vowel and a vote among 가/카 cannot tell ㄱ from ㅋ's ㄱ. Peers need 2 "same" syllables in a set, else the
  pool of all syllables with (or without) a final is used. A bitmap source keeps the 844 region fit (its pixels
  are split by it).
- **Adaptive, harder.** ㄱ and ㅋ are lead classes of their own (the other 17 initials cluster into 6); splits are
  taken down to a gain of 0.05 % with 4 syllables a side, so the faces reach 24/16/16. `adapt()` reports the
  (set, jamo) groups that still conflict; `class_limit` and larger set limits simulate a bigger engine table.
- **Vector masters** (`jamovec.py master FONT -o M.json`, `jamovec.py render M.json --size N --bpp 1|2`): the
  separation once per font at a decision size (48 px), a master piece per (set, jamo) is one donor's own contours
  (the medoid of the donors' rasters, preferring donors whose slot holds whole contour groups), stored in font
  units with the frame box, ASCII and the tables; every size is only rasterised from them. Measured on Paperlogy
  Regular it scores below the per-size fit (runs/aa-jamo/report.md): one donor's outline cannot follow the
  context variation a per-size consensus averages over.
- **Vector segmentation** (`jamoseg.py`, experimental): grouping by containment, a vote by "twin" groups (same
  size and area, centre within 0.12 em) in the peers, median boxes per piece as regions, templates (the font's own
  outline of the jamo from another syllable, placed in the group's box) to find jamo inside a merged outline, and
  path-boolean clipping (skia-pathops, optional) of merged outlines. On Paperlogy, whose outlines often merge two
  jamo (거 = ㄱ+ㅓ, 효 = ㅎ ring + ㅛ) and whose vowel shapes change with the initial, it still misassigns many
  syllables (review sheets in runs/aa-jamo/page/seg-*.png); not used by the face builders.
- **Metrics**: collisions (pixels two pieces ink) and the clearance between medial and final in 국 궉 귁 개 객 게 각.
