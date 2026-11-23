# Anti-aliased (2 bpp, 8 bpp) faces: tests, 15/16-bit targets, game runs

Owner requests (2026-11-23): "AA 폰트도 테스트 해야 함"; "TrueType에서 생성할 것" (the AA faces are generated from TrueType with the
repository's tools, not picked from the shipped files); "조합형 + 완성형 AA 조합으로 테스트 해야 함" (chains of a jamo set and a
precomposed face); "AA는 CLUT 제외다" (AA is for the RGB render targets only; CLUT8 uses 1 bpp faces); "AA는 15/16비트 필수다"
(RGB555 and RGB565 are the mandatory AA targets, 24/32 bit are secondary). Until now every golden and every game run used 1 bpp
faces (NEODG16A, the NEODG16G-derived fixture).

Branch `ko-2026.4`, from `4a8b9907b2c`. Commits (oldest first): `4f5c9a9fa36` TEST AA goldens and suite; `a2babee65a9` TEST CLUT8 stamp record and strip dump; `0f5dd9512f6`
GRAPHICS rgb555 target; `c8abfd7eb05` SCI backend pixel format; `1514a82faf1` TEST 15/16-bit; `92dd7e9ff04` SCI RGB/CLUT8 map pairing and the
CLUT8 warning; `a4e3435ad26` TOOLS 1 bpp pair face; this DOCS commit.

Verification at the last code commit: `make test` SCI build (`~/work/tmp/aa-build`) **723 OK**; ASan build (`~/work/tmp/aa-asan`,
`--enable-asan`) **723 OK, 0 sanitizer lines**, and the KQ1 `pair` run at rgb565 and rgb555 under the ASan `scummvm` (0 sanitizer
lines in both logs; status-row sha256 `04e8b1964fcc382b` and `75d5af99face0452`, the same as the normal build); no-engine build
(`~/work/tmp/aa-noengine`) **644 OK** (the SCI suites are not compiled); `tools/korean/sync-svf.sh --check` rc 0;
`python3 -I tools/korean/mkgolden.py --check` rc 0 (covers the new goldens and fixtures); `test_k6bake.py`, `test_strcheck.py` OK
(TMPDIR under `~/work/tmp`). Before this work: 679 SCI / 620 no-engine.

## 1. The faces (generated from TrueType)

`tools/korean/gen_aa_faces.py OUTDIR` runs the svf-fonts-open tools (pinned submodule `3d40de8d940`, Python `-I`) on the OFL
TrueType files of `dists/svf-fonts-open/ttf/nanumgothic/` (NanumGothic Bold and Regular, "Version 3.020; PS 1; hotconv 1.0.57",
Copyright (c) 2010 NHN Corporation, SIL OFL 1.1; Bold sha256 `f96298f9fb18e364d2370f4c3ce948ac67a2b61af992d7234bc15c42b033c674`,
Regular `76f45ef4a6bcff344c837c95a7dcc26e017e38b5846d5ae0cdcb5b86be2e2d31`). `gen_aa_faces.py --print` prints the exact command
lines. Route: `mkfont.py` (FreeType through Pillow) for the precomposed faces; `jamoset.py --from-ttf --bpp 2` is the
"outline composite" route (`jamoaa.py`: exact-area coverage from the outline, pieces by the 1 bpp fit regions; fontTools only, no
FreeType; `--pylib` gets the fontTools directory because `-I` hides the user site).

| face | what | bytes | sha256 (first 16) |
|---|---|---|---|
| NG16P.SVF | precomposed 16x16 2 bpp, Bold 16 px, ASCII + 103 syllables (`FREQUENT`) | 12,248 | `e68fe1ce44ba5192` |
| NG18P.SVF | precomposed 18x18 2 bpp, Bold 18 px, ascent 16, the same code points | 14,800 | `53c4849dc118fc36` |
| NG16L.SVF | precomposed 16x16 2 bpp Latin + U+0300/U+0301, Regular 16 px | 4,688 | `41e6d733c5bbb569` |
| NG16JA.SVF | jamo set 844, 16x16 2 bpp, Bold 16 px, all 11,172 syllables (344 pieces, level error 0.233, IoU at level 2 0.822) | 24,960 | `1db34edcb203e87f` |
| NG16P8.SVF | precomposed 16x16 **8 bpp**, a few glyphs | 2,628 | `b48f7ac353ce066c` |
| NG16J1.SVF | the 1 bpp pair: jamo set 844, Bold 16 px, `--binarize threshold` (not a test fixture) | 20,064 | `13c31a3d72e6eb7b` |

* **Fixtures** (small, OFL): the first five are committed as C arrays in `test/graphics/svf_fixture_aa.h` (362 KB of source,
  60 KB of data) with the TTF names, versions and sha256 in the header; `LICENSES/COPYING.OFL` has the entry; the faces carry no
  font name (checked with `strings`). The precomposed faces keep only ASCII and the syllables of the tests, so a syllable outside
  them (똠 쀍 뷁 ...) and the compat jamo ㄱ..ㅣ are drawn by the jamo set of a chain. The jamo set is a full set: 25 KB is what a
  set costs.
* **Reproducible**: `mkgolden.py` builds the faces once into `$TMPDIR/svf-aa-faces-<hash of generator, submodule commit, TTF>`
  (a jamo set takes about 75 s) and `mkgolden.py --check` regenerates the goldens from them; NG16JA from the generator is
  byte-identical to the one built by hand from the docs/recipes.md command (`cmp`).
* **8 bpp**: no shipped face has 8 bpp (measured over `svf/`: 71 faces 2 bpp, 45 faces 1 bpp). `mkfont.py --bpp 8` makes one;
  `jamoset.py` has only `--bpp 1|2`, so there is no 8 bpp jamo set. NG16P8 is the 8 bpp fixture.
* **Cross-check against the owner's builds** (`tools/build/build_faces.py`, rebuilt from the same TTF and compared with
  `svf/dos`): GBATB17 (Gowun Batang Bold 17 px 2 bpp) **identical**, NGOTB15 (NanumGothic Bold 15 px 2 bpp) **identical**, NGOTB16
  differs only by U+3000 (present in the shipped file, absent from a rebuild, as faces.tsv says). So the generation path is the
  owner's. The shipped jamo sets GOTHB18A, MARUB20A... have no recipe in the repository ("research pipeline"), so they cannot be
  rebuilt; NG16JA uses the repository's own `jamoset.py` route.

## 2. Unit and golden tests (`test/graphics/svf_aa.h`, 21 tests; goldens `svf_golden_aa_data.h` from `tools/svfline.py`)

Twelve cases: PreOnly (NG16P), JamoOnly (NG16JA), ChainPJ [P, JA] at 1x / 2x / 3/2, ChainJP [JA, P], ChainLPJ [L, P, JA] (with the
combining mark), ChainMixedBpp [1 bpp marks, 2 bpp P, 1 bpp jamo], ChainSizes [P18, JA] at 1x / 2x / 3/2, Bpp8 [P8, P]. The text mixes
frequent syllables, rare syllables (똠 쀍 뷁), compat jamo, Latin, digits, punctuation and a combining mark. Goldens also hold the
C targets' RGB565 / RGB555 (little and big endian, bit 15 of fg565 set and clear) and RGB888 output for ChainPJ and ChainPJ3by2.

* GRAY8 through `SvfChain::draw` for every case; the same streamed (block cache, with and without the syllable cache) and mapped.
* `svf_measure_utf8` extents (width, ascent, descent) against the reference.
* The four levels 0/85/170/255 appear and nothing else at 1x; 3/2 gives other levels; 8 bpp gives other levels.
* Provenance: for each of 28 (chain, code point) pairs the chain's drawing equals the single-face drawing of the first face that has
  it, and no earlier face drew it (`svf_has` semantics: a jamo set "has" every U+AC00..U+D7A3 and the compat jamo, so **the chain
  order decides everything**: in [JA, P] the precomposed face never draws a syllable, in [P, JA] the jamo set draws only what P lacks;
  asserted for both orders, for the three face chain and for the mixed-bpp chain).
* Join of two faces (section 8): advances, baseline rows, stroke weight, ascent/descent of the chain, two cell sizes (extent,
  `SvfFont` height/ascent/descent, 3/2 and 2x of them, clipping into a smaller target).
* The plane keeps the coverage: `drawCoverage` into a `TextPlane` equals the golden for every case (1x, 2x and 3/2 at hi-res scale
  2), with the levels 85 and 170 present (not thresholded) until compositing.
* Sinks: CLUT8 `kStamp` (170 and 255 survive, 85 does not); `kBlend` on RGB565, RGB555, BGR565, ARGB8888, ABGR8888, XRGB8888,
  RGBA8888 and `kThreshold`; levels 85 and 170 are strictly between background and text colour in every channel;
  `presentRect`; `SvfFont` on CLUT8 / RGB565 / RGB555 / ARGB8888 at 1x and 3/2; a mixed 1 bpp + 2 bpp line reaches the plane and
  the sinks with both coverages; 8 bpp coverage reaches the plane unchanged.

### 2.1 15 and 16 bit (mandatory AA targets)

* Every case, 5-6-5 and 5-5-5, mapped and streamed, through the C targets equals `blend16(...)` of the golden coverage over the
  background; through `TextPlane` + `compositeRect` on RGB565, RGB555 and XRGB1555 over a background that changes every pixel it
  equals the blend of the local colour (`test_every_case_at_15_and_16_bit`).
* Goldens from `svfline.Target` at RGB565 and RGB555, little and big endian (`SVF_TF_RGB16_BIGENDIAN`), with bit 15 of fg565 set
  (copied to every drawn pixel, none else) and clear; byte-identical (`test_rgbTargets`). `Graphics::PixelFormat` has no endianness,
  so the big-endian layout exists only in the C targets.
* Quantisation (`test_levelsSurviveQuantisation`, `test_eightBitRampAtFiveBits`): for white on navy, black on white, yellow on blue,
  the golden pair and a low-contrast grey pair, on RGB565, RGB555, XRGB1555 and BGR565, the four levels give four different colours
  and every channel moves monotonically toward the text colour (no banding inversion). The 8 bpp ramp white on black at 5 bits:
  32 values for each 5-bit channel, 64 for the 565 green, monotonic; **the ScummVM sink blends at 8 bits and `RGBToColor`
  truncates, so a value is up to 7 below the exact blend; the C renderer's targets blend the 5-bit channels with rounding (error at
  most 4)**. This is the quantisation data; truncation darkens an AA edge by at most 7/255 per channel and was left as it is.
* XRGB1555: the alpha bit is set on every pixel the sink writes (also over a transparent pixel), pixels without coverage keep theirs
  (`test_alphaBitOf1555`).

### 2.2 Mutation checks (each was run on a tree with exactly that change and failed the tests named; all reverted)

| mutation | tests that failed |
|---|---|
| `svf_draw.c`: a 2 bpp level becomes 255 ("1 bpp treatment") | AA `test_goldenLines`, `test_fourLevels`, `test_streamedEqualsMapped`, `test_mixedBppLine`, `test_coherence` |
| `text_compose.cpp`: RGB threshold 128 -> 60 | existing `test_clut8EqualsThreshold`, `test_composeRgb565`; AA `test_rgbSinks`, `test_presentThroughScreen` |
| `text_compose.h`: stamp threshold 128 -> 60 | existing `test_stamp`, `test_clut8EqualsThreshold`; AA `test_clut8Stamp` |
| `SvfChain::addFace` inserts at the front (chain order reversed) | 24 suites tests: AA goldens, provenance, extents, sinks, face facts; existing font set / font tests |
| `svf_has` without the composed-syllable fallback (jamo set does not "have" syllables) | AA goldens, provenance, face facts, sinks; existing font set tests |
| `svf_draw.c`: RGB555 blend without `+ 127` rounding | AA `test_rgbTargets`, `test_every_case_at_15_and_16_bit` |
| `hires_options.cpp`: auto prefers 555 over 565 | `test_rgb555`, `test_screenFormatFollowsTheTarget`, `test_aaForRgbAndOneBppForClut8` |
| `font_map.cpp`: rgb555 is not an `rgb` qualifier | `test_rgb555QualifiesAsRgb`, `test_aaForRgbAndOneBppForClut8` |
| `hirestext.cpp`: no warning for an AA face on CLUT8 | `test_aaFaceOnClut8IsStampedAndWarnedOnce` |

## 3. Render-target selection: rgb555 (a real item, found by the 15/16-bit requirement)

* `render_target` had `auto|clut8|rgb565|rgb888`; a backend offering only 555 or XRGB1555 got CLUT8 although the sinks handle them
  (P1 review m-4). Added `rgb555` (value, `hiResTargetOf`: a 2 byte 5-5-5 format with or without the alpha bit, either channel order;
  `rgb` qualifier). `auto` and the fallback for a target the backend does not offer now prefer **565, then 555, then 888**, ending
  in CLUT8 (it was 888, then 565). Tests: `test_rgb555`, `test_targetFormats`, `test_rgb555QualifiesAsRgb`.
* **Found while checking "the screen really is 16 bit": `render_target=rgb565` did not choose the screen format.** It only chose
  how the text is composed; `GfxDefaultDriver::initScreen` called `initGraphics(w, h, nullptr)` for any RGB request, so the screen was
  whatever the backend started with. Fixed in `c8abfd7eb05`: the driver asks for the backend's formats of the resolved target (the
  backend's own order, since `initGraphics(list)` takes the first backend format the list has), and logs it at debug level 1:
  `Hi-res text: screen format RGB565@2 for render target 2`. Tests: `hiResTargetFormat` maps back to its target for every target;
  `SciHiResText::getScreenFormat()` follows the resolved target.
* **This host (SDL2 surface backend under Xvfb)**, from the logs of 16 runs: `render_target=rgb565` gives `RGB565@2`;
  `rgb555` gives `ARGB1555@2` (the backend's XRGB1555 entry: a 15-bit screen); `clut8` gives `CLUT8`. `rgb888` is **not** offered:
  `SurfaceSdlGraphicsManager::detectSupportedFormats` leaves out formats wider than the current hardware screen, which is 16 bit when
  the engine starts, so the engine logs `render_target rgb888 is not offered by this backend; using rgb565` and the run is an RGB565
  run. 32-bit is therefore covered by the unit path only (RGB888/ARGB8888 sinks, `hiResTargetFormat`), not by a game screenshot.
* DOS (P7): VBE 15/16-bit modes must appear in the DOS backend's `getSupportedFormats()` (RGB555, RGB565 and their order);
  with that, the selection above needs no change. The AA text path at 15/16 bit costs two blends per pixel in `composeSpan`; its speed
  on a 386/486 is not measured here (UNVERIFIED).

## 4. Chains of a jamo set and a precomposed face: pairing guidance for maps

Measured with `svfline.py` on the generated faces (NG16P, NG16JA, NG16L, NG18P):

* **Advance**: syllables step 15 px in both NG16P and NG16JA; ASCII advances are equal (A 12, b 10, space 4, comma 5): no gap or
  overlap at a join. With NG18P (18 px) a syllable steps 17 px against 15 px of the 16 px jamo set: a line mixing them has uneven
  steps (a 17 px and a 15 px cell next to each other), and the chain's cell height is 18.
* **Baseline**: all cells stand on one baseline. The faces disagree by one row on the ascent: mkfont `--clip-cell` chose 14 for
  NG16P, jamoset's default ink-box baseline 13 for NG16JA (NG16L: 13). Latin ink ends on the same row in both (asserted); a
  syllable's top row differs by at most one row. The chain's ascent is the larger (14), its descent 3, its cell height 16. Pass
  `mkfont --ascent N` equal to `jamoset --baseline N` to make them equal.
* **Weight**: the same TrueType and size, but a jamo-set syllable is a composite of pieces OR-ed by maximum, so it is lighter where
  strokes cross. Coverage sum of the composed syllable over the precomposed one for the 103 `FREQUENT` syllables: aggregate 0.92,
  median 0.89, p10 0.77, p90 1.05, range 0.66 (도) to 1.46 (출); 65 of 103 within 0.85..1.15; the tests assert 0.90..1.10 for 한 글 가.
  In the screenshots the join is visible only as slightly lighter/heavier single syllables, no seam.
* **Order**: put the precomposed face first and the jamo set after it. The jamo set draws every syllable, so [JA, P] makes the
  precomposed face useless (tested). A Latin face goes first of all; the marks of a face with flag bit 2 need to be in a face
  before the jamo set (the jamo set has no U+0301).
* **Pair** a jamo set with a precomposed face of the same family, size, weight, cell and advance; make the ascents equal; do not
  chain 18 px with 16 px faces unless the map's `size=` scales the whole chain (`[font.300:rgb] size=24` gave 3/2 on a 16 px chain,
  see screenshots).
* **Mixed bpp in one chain** works (1 bpp coverage 255 next to 2 bpp levels, `ChainMixedBpp`, `test_mixedBppLine`), but on RGB the
  1 bpp glyphs look hard-edged beside AA ones: use it as a fallback at the end of a chain, not in the middle.
* **A map for both screens** (tested, `test_aaForRgbAndOneBppForClut8`; run in the games below):

```
[fonts]
p16=data:NG16P.SVF     ; precomposed 2 bpp, NanumGothic Bold 16 px
ja=data:NG16JA.SVF     ; jamo set 2 bpp, the same
j1=data:NG16J1.SVF     ; jamo set 1 bpp of the same TTF (jamoset --binarize threshold)
[font]
face=j1,neo
[font:rgb]
face=p16,ja,neo
[font:clut8]
face=j1,neo
```

`auto` picks RGB (16-bit first) because a face of the map has coverage; `render_target=clut8` resolves the 1 bpp chain.
NG16J1, fitted by `--binarize threshold`, forms a few syllables badly at 16 px (는 reads like 눈 in LSL1's warning box): a 1 bpp
face for CLUT8 should be a hand-made bitmap face such as NEODG16A.

## 5. Game runs (headless Xvfb, xdotool, ScummVM's Alt+S; scripts `~/work/tmp/aa-games/`)

Build `~/work/tmp/aa-build` (`--disable-all-engines --enable-engine=sci --enable-debug --disable-optimizations`), game data
symlinked from the P3/P4 scratch copies (never in the repository), the generated faces in a scratch `extrapath`
(`~/work/tmp/aa-games/extra`, `data:NG16P.SVF`), the maps in the game folders' `ko/` (`hires_text_map=ko/aa-<map>.map`). KQ1 (SCI0
EGA) and LSL1 VGA (256 colours, run as RGB). Pixel formats from the log line above.

| run | screen | chain / map | what the screenshots show |
|---|---|---|---|
| KQ1 `pair` rgb565 | RGB565@2 | [NG16P, NG16JA] | status line, message box, parser prompt `입력 / 봐`, menu `파일 행동 속도 소리`: smooth AA, edges blended (5 colours in a text crop against 3 for CLUT8), no clipping, baseline on the game's, cell 16 hi-res px = 8 game px; the box is sized from the text |
| KQ1 `pair` rgb555 | ARGB1555@2 | same | the same frame; status rows differ from rgb565 in 1,427 of 14,080 pixels (5 bit against 6 bit green quantisation of the edges); legible, no colour fringe, no wrong colour |
| KQ1 `pair` clut8 | CLUT8 | [NG16J1, neo] by `[font:clut8]` | the 1 bpp pair: hard edges, legible, heavier; picked by the section (log: screen format CLUT8) |
| KQ1 `pair` rgb888 asked | RGB565@2 | same | `not offered; using rgb565`, pixel-identical status rows to rgb565 (sha256 below) |
| KQ1 `neo` rgb565 | RGB565@2 | [NEODG16A] | the 1 bpp Neo Dunggeunmo fallback at the same 16 px: crisp, thinner (911 against 1,060 weighted pixels in the line measured in section 6), pixel-font look; the AA NanumGothic is smoother and about 17 % heavier |
| KQ1 `pairl` rgb565 | RGB565@2 | [NG16L, NG16P, NG16JA] | Latin (`득점: 0 of 158`) also AA, lighter weight (Regular) than the Hangul (Bold) |
| KQ1 `pairjp` rgb565 | RGB565@2 | [NG16JA, NG16P] | every syllable from the jamo set: same layout, a little lighter |
| KQ1 `pairs` rgb565 | RGB565@2 | message boxes [NG18P, NG16JA] | 18 px syllables and 16 px jamo-set syllables in one box, box and wrap right; the uneven step (17/15 px) is visible as slightly uneven spacing |
| KQ1 `pair32` rgb565 | RGB565@2 | 16 px chain at `size=24` | 3/2 scale: larger, smooth text, no clipping in the box |
| KQ1 `aaonly` clut8 | CLUT8 | [NG16P, NG16JA], no `[font:clut8]` | the defined fallback: stamped at 128, log `an anti-aliased face on a CLUT8 screen is stamped at coverage >= 128; give the map a [font:clut8] section with a 1 bpp face` (once) |
| LSL1 VGA `pair` rgb555 / rgb565 | ARGB1555@2 / RGB565@2 | [NG16P, NG16JA] | warning box with wrapped text, the age dialog with its six buttons: AA text over the 256-colour picture and the yellow boxes, correct colours (black on yellow, white on black title bars); the mouse cursor covers some text |
| LSL1 VGA `pair` clut8 | CLUT8 | [NG16J1, neo] | the 1 bpp pair; `는`/`눈` look-alikes in "있는", "에는" |
| LSL1 VGA `pairl`, `neo` rgb565 | RGB565@2 | three faces; 1 bpp NEODG16A | as KQ1 |

Syllables in KQ1's `sci-ko.str`: 563 distinct Hangul syllables, 8,038 occurrences; **473 distinct syllables (3,897 occurrences, 48 %)
are not in NG16P** (it has 103), so every message box shows both faces. No seam is visible at the join in any frame.
Stable frames (sha256 of the status rows, y 0..21, 640 px, first 16 hex): every rgb565 run whose chain is [NG16P, NG16JA]
(`pair`, `pair` with rgb888 asked, `pairs`, `pair32`, `aaonly`) `04e8b1964fcc382b`; rgb555 `75d5af99face0452`; CLUT8 pair
`243d507e788da6c3`; `neo` rgb565 `c86c949eb46372bf`; `pairl` `347b8ce834209438`; `pairjp` `a5a5279336a1aca1`. Message-box frames differ between
runs (the typed `qhk` and the screenshot hotkey add `ㄴ`, as in P4-notes), so their hashes are not stable.
Screenshots: `~/work/tmp/ko-screenshots/aa-*.png` (captions name the face, size, recipe and the pixel format).

## 6. CLUT8 stamp of AA text (informational only)

**Informational only: superseded by the owner decision of 2026-11-23 ("AA는 CLUT 제외다"): AA faces are not used on CLUT8.** The measurement below was made before that decision and is kept as the data behind the defined fallback (section 7).

On CLUT8 (SCI 16-colour EGA and 256-colour games) `TextCompose::stampSpan` writes the text index where coverage >= 128
(`kStamp`, default threshold 128); `SvfFont` thresholds the same way on a CLUT8 surface. So of the four levels of a 2 bpp
face (coverage 0, 85, 170, 255) the levels **170 and 255 survive, 85 is dropped**. At 3/2 the coverage has other values and the
same cut at 128 applies. There is no ramp: a CLUT8 game's palette has no spare entries for one.

Measured with `svfline.py` on "한글 게임을 시작합니다. Hello 123" (cell 16, one line):

| face | pixels with level 85 / 170 / 255 | stamped at >= 128 | stamped at >= 64 | coverage-weighted ink (sum / 255) |
|---|---|---|---|---|
| NEODG16A (1 bpp, the shipped fallback) | 0 / 0 / 911 | 911 | 911 | 911 |
| NG16P (NanumGothic Bold 16 px, precomposed 2 bpp) | 293 / 300 / 762 | 1062 | 1355 | 1060 |
| NG16JA (the same as a jamo set 844, 2 bpp) | 375 / 433 / 657 | 1090 | 1465 | 1071 |

* The threshold 128 keeps the ink of the face: the stamped pixel count (1062, 1090) equals the coverage-weighted ink (1060, 1071)
  within 2 %, because the dropped 85 pixels and the kept 170 pixels balance. The stamped AA text is about 17 % heavier than the
  1 bpp NEODG16A line, which is the difference of the two designs (NanumGothic Bold against Neo Dunggeunmo), not a loss of the
  stamp.
* Lowering the threshold to 64 (keeping level 85) makes the stroke 28 % heavier than the AA design and the text clots
  (`test_dumpWhenAsked`, `aa-dump/clut8-policy.png`: rows 5, 8, 11 of the strip).
* The strip (3 faces/chains x stamp 128, stamp 64, blend; each pixel 3x3): the 1 bpp face and the 128 stamps of the 2 bpp faces
  are equally crisp and legible; the stamped 2 bpp text has the 16 px shapes of NanumGothic with one-pixel strokes where the AA
  outline was thin, no gaps in the syllables; the 64 stamp is clearly worse (blobs); the blend is the smooth reference.

**Finding: the threshold 128 keeps the ink of an AA face on CLUT8; no code change.** The evidence does not show the stamped AA text
to be worse than the 1 bpp text. A lower threshold is worse; a ramp needs palette entries the games do not have.

## 7. What the engine does with an AA face on a CLUT8 target

A map that names an AA face for CLUT8 without a `[font:clut8]` section keeps working: the plane holds the coverage (85, 170, 255),
`TextCompose::kStamp` writes the index where coverage >= 128, so levels 170 and 255 survive and 85 is dropped (section 6: the ink
is kept within 2 %, the text legible). That is the defined fallback. The engine now logs one warning per run, when the first font
set with such a chain is built: `an anti-aliased face on a CLUT8 screen is stamped at coverage >= 128; give the map a [font:clut8]
section with a 1 bpp face` (`SciHiResText::getFontSet`, 8 lines, `test_aaFaceOnClut8IsStampedAndWarnedOnce`, seen in the
`aaonly` clut8 run's log). It is not given for a map whose CLUT8 chain is 1 bpp.

## 8. Bugs found, undone items

* Found and fixed: `render_target` did not select the screen's pixel format (`c8abfd7eb05`); `rgb555` was not a target and `auto`
  preferred 888 over the 16/15-bit formats (`0f5dd9512f6`).
* Found, not a defect of the engine: SDL's format list caps at the current hardware depth, so `rgb888` falls back to RGB565 here;
  a 1 bpp jamo set fitted with `--binarize threshold` (NG16J1) forms some syllables badly.
* UNVERIFIED / not done: a game screen at a real 32-bit format (the backend does not offer it here); DOS VBE 15/16-bit modes and
  speed (P7); mingw build; 8 bpp faces in a game run (the 8 bpp face is a unit fixture only; jamoset cannot make an 8 bpp jamo set);
  RGB555 with a big-endian layout in ScummVM (PixelFormat has none, only the C targets); SCI32 and other engines (SCI0/1 `default`
  driver only); the 1 bpp NG16J1 of the CLUT8 side is a generated face, not a fixture.
