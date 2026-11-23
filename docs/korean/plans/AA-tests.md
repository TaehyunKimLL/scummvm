# Anti-aliased (2 bpp, 8 bpp) faces: tests, 15/16-bit targets, game runs

Owner requests (2026-11-23): "AA 폰트도 테스트 해야 함"; "TrueType에서 생성할 것" (the AA faces are generated from TrueType with the
repository's tools, not picked from the shipped files); "조합형 + 완성형 AA 조합으로 테스트 해야 함" (chains of a jamo set and a
precomposed face); "AA는 CLUT 제외다" (AA is for the RGB render targets only; CLUT8 uses 1 bpp faces); "AA는 15/16비트 필수다"
(RGB555 and RGB565 are the mandatory AA targets, 24/32 bit are secondary). Until now every golden and every game run used 1 bpp
faces (NEODG16A, the NEODG16G-derived fixture).

Branch `ko-2026.4`, from `4a8b9907b2c`. Commits (oldest first): `4f5c9a9fa36` TEST AA goldens and suite; `a2babee65a9` TEST CLUT8 stamp record and strip dump; `0f5dd9512f6`
GRAPHICS rgb555 target; `c8abfd7eb05` SCI backend pixel format; `1514a82faf1` TEST 15/16-bit; `92dd7e9ff04` SCI RGB/CLUT8 map pairing and the
CLUT8 warning; `a4e3435ad26` TOOLS 1 bpp pair face; `618d93da62d` DOCS. Review round: `c3e6f4262f6` reviews, `102b6d51512` TOOLS generator, `8cde12f534a` GRAPHICS blend and 1555 alpha, `b7abdf9ba41` GRAPHICS auto order and format pick, `bc0d02723f7` SCI text follows the screen, `f21d5e16ef7` SCI one screen format, then this DOCS commit.

Verification at the last code commit (`f21d5e16ef7`; the review-round fixes are in section 9): `make test` SCI build
(`~/work/tmp/aa-build`) **727 OK**; ASan build (`~/work/tmp/aa-asan`, `--enable-asan`) **727 OK, 0 sanitizer lines**, and the KQ1
`pair` run at rgb565 and rgb555 under the ASan `scummvm` (0 sanitizer lines in both logs; the same status-row and message-box
sha256 as the normal build); SCI + SCI32 build (`~/work/tmp/aa-sci32`) **727 OK**, no warning in the log; no-engine build
(`~/work/tmp/aa-noengine`) **646 OK** (the SCI suites are not compiled); `tools/korean/sync-svf.sh --check` rc 0;
`python3 -I tools/korean/mkgolden.py --check` rc 0 (covers the goldens and fixtures); `test_k6bake.py`, `test_strcheck.py` rc 0
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
* Sinks: CLUT8 `kStamp` (170 and 255 survive, 85 does not). `kBlend` and `kThreshold` through `compositeRect`, `presentRect` and
  `SvfFont` on RGB565, BGR565, RGB555, ARGB1555, RGBA5551, BGR555, XRGB8888, ARGB8888, ABGR8888 and RGBA8888, **every pixel equal to
  the C renderer's pixel taken from the tools/svfline.py goldens** (Rgb565 / Rgb555 / Rgb888 rows, re-packed into the format's channel
  shifts, alpha field opaque; the expected value is not computed with the sinks' formula). `SvfFont` is drawn on a transparent
  surface where the format has alpha, and the text pixels come out opaque, also the faint levels. A mixed 1 bpp + 2 bpp line reaches
  the plane and the sinks with both coverages; 8 bpp coverage reaches the plane unchanged and through the 16-bit C targets.

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
  32 values for each 5-bit channel, 64 for the 565 green, monotonic. **Rounding (changed in the review round):** the hi-res text
  sinks and `SvfFont` blend in the channel widths of the screen format, `(bg * (255 - cov) + fg * cov + 127) / 255` per channel
  (`TextCompose::blendPixel`), the arithmetic of the C renderer's RGB565/RGB555 targets, so over one background colour the
  composite equals the C targets byte for byte, for all 256 coverages. The value read back (bit replication) is within 4.1 of 255
  of the exact blend (half a 5-bit step). Before, the sinks blended at 8 bits and `RGBToColor` truncated: **the earlier text of
  this section ("up to 7 below", "darkens") was wrong**: measured by the Opus reviewer, that arithmetic differed from the C
  targets by one LSB in either direction (about 17 % of edge pixels, symmetric, mean 0; 248 reads back as 255, 7 above) and lay
  within 7.5/255 of the exact blend. The game screen itself is quantised to the screen format first (the driver converts the
  CLUT8 scaled bitmap to the 15/16-bit composite buffer), then the text is blended over it, which is what the C targets do too;
  `convertSpan` (frame colour from the palette) cuts the frame colour to the channels first for the same reason.
* XRGB1555: the alpha bit is set on every pixel the sink writes (also over a transparent pixel), pixels without coverage keep theirs
  (`test_alphaBitOf1555`).

### 2.2 Mutation checks (re-run in the review round: each change applied to the worktree alone, the tests rebuilt and run, the file reverted)

The first record of this table was wrong for four rows (the Opus review reproduced only 3 of 9; the `auto` mutation does not fail
`test_screenFormatFollowsTheTarget`, the qualifier mutation fails more than 2 tests). Every row below was actually run again; the
"failed" column is the list the runner printed (existing suites in italics).

| mutation | failed |
|---|---|
| `svf_draw.c`: a 2 bpp level becomes 255 ("1 bpp treatment") | 5: AA `test_goldenLines`, `test_fourLevels`, `test_streamedEqualsMapped`, `test_mixedBppLine`, `test_coherence` |
| `text_compose.cpp`: RGB threshold 128 -> 60 | 4: *`test_clut8EqualsThreshold`, `test_composeRgb565`*; AA `test_rgbSinks`, `test_presentThroughScreen` |
| `text_compose.h`: CLUT8 stamp threshold 128 -> 60 | 3: *`test_stamp`, `test_clut8EqualsThreshold`*; AA `test_clut8Stamp` |
| `SvfChain::addFace` inserts at the front (chain order reversed) | 28: 16 AA tests (goldens, provenance, extents, sinks, face facts ...), 3 SCI hi-res text tests, *2 font set, 3 SvfFont, 4 SvfGolden tests* |
| `svf_has` without the composed-syllable fallback | the runner aborts on an array assertion in an existing font test after 34 failed tests (AA goldens, provenance, sinks, face facts; *font set, SvfFace, SvfFont, SvfGolden*) |
| `svf_draw.c`: RGB555 blend without `+ 127` | 2: AA `test_rgbTargets`, `test_every_case_at_15_and_16_bit` |
| `hires_options.cpp`: auto prefers 565 over 888 | 4: `test_rgb555`, SCI `test_aaForRgbAndOneBppForClut8`, `test_autoPicksByWhatIsOffered`, `test_textFollowsTheScreenItGot` |
| `hires_options.cpp`: auto prefers 555 over 565 | 2: `test_rgb555`, SCI `test_autoPicksByWhatIsOffered` (**not** `test_screenFormatFollowsTheTarget`: it passes an explicit target; the new test does the auto case) |
| `font_map.cpp`: rgb555 is not an `rgb` qualifier | 5: `test_rgb555`, `test_rgb555QualifiesAsRgb`, SCI `test_aaForRgbAndOneBppForClut8`, `test_autoPicksByWhatIsOffered`, `test_screenFormatFollowsTheTarget` |
| `hirestext.cpp`: no CLUT8 warning | 2: SCI `test_aaFaceOnClut8IsStampedAndWarnedOnce`, `test_textFollowsTheScreenItGot` |
| `hirestext.cpp`: warning every time (no once-guard) | 2: the same two (a second font id warns again) |
| `text_compose.cpp`: `blendPixel` without `+ 127` (truncating) | 10: AA `test_every_case_at_15_and_16_bit`, `test_rgbSinks`, `test_presentThroughScreen`, `test_svfFontSurfaces`, `test_eightBitRampAtFiveBits`; *`test_composeRgb565`, `test_convertXrgb8888`, SvfFont `test_rgb565`, `test_argb32`, `test_rgb332`* |
| `svffont.cpp`: alpha blended by coverage (the old 1555 behaviour) | 2: AA `test_svfFontSurfaces` (ARGB1555 / RGBA5551 on a transparent surface), *SvfFont `test_argb32`* |
| `hires_options.cpp`: `hiResPickScreenFormat` filter inverted | 1: `test_pickScreenFormat` |
| `hires_options.cpp`: `hiResPickScreenFormat` fallback order 888, 565, 555 -> 888, 555, 565 | 1: `test_pickScreenFormat` |
| `hirestext.cpp`: `setScreenFormat` ignores the screen | 1: SCI `test_textFollowsTheScreenItGot` |
| `init.cpp`: `requestRGB` overridden by `wantsRgb()` again | **0: no unit test** (the driver needs a running engine); covered by the documented run in section 9 only |

## 3. Render-target selection: rgb555 and the screen format (real items, found by the 15/16-bit requirement)

* `render_target` had `auto|clut8|rgb565|rgb888`; a backend offering only 555 or XRGB1555 got CLUT8 although the sinks handle them
  (P1 review m-4). Added `rgb555` (value, `hiResTargetOf`: a 2 byte 5-5-5 format with or without an alpha bit, either channel order;
  `rgb` qualifier). **`auto` (and the fallback for a target the backend does not offer) prefers 888 where the backend offers it,
  then 565, then 555, ending in CLUT8** (owner delegate decision in the review round): 15 and 16 bit are required to work and are
  what is chosen where 32 bit is not offered (DOS VBE, backends without it), but they are not preferred on a 32-bit desktop because
  they halve the colour levels of the whole VGA picture (a 6-bit DAC level pair merges at 5 bits). Tests: `test_rgb555`,
  `test_targetFormats`, `test_targetNames`, `test_rgb555QualifiesAsRgb`, `test_autoPicksByWhatIsOffered` (auto with an AA map).
* **`render_target=rgb565` did not choose the screen format** (found by checking that the screen really is 16 bit; fixed in
  `c8abfd7eb05`): the driver called `initGraphics(w, h, nullptr)` for any RGB request. The driver now asks the backend for a format
  of the resolved target and logs it at debug level 1 (`Hi-res text: screen format RGB565@2 for render target rgb565`).
* **The first fix was itself wrong, found by the retaken runs** (`f21d5e16ef7`): `initGraphics(list)` takes the first BACKEND format
  the list contains (`engines/engine.cpp findCompatibleFormat`), so a list of the target's formats plus the other RGB formats gave
  the backend's favourite: `render_target=rgb555` got RGB565 on SDL (log: "the backend gave a rgb565 screen"). The pure function
  `hiResPickScreenFormat(target, supported, out)` now returns one format: the backend's first of the target, else of the next RGB
  target (888, 565, 555); the driver passes exactly that format (an empty list, so CLUT8, when no RGB format is offered). The unit
  test models the backend's rule (`backendGives`): for an SDL-like list the single 555 gives XRGB1555 while a list with 565 in it
  gives 565; OpenGL-like, 888-only, 555-only, missing-target, no-RGB and CLUT8-only lists are covered.
* **Owner's call (left as is):** an `rgb555` request on a backend that offers 888 and not 555 falls to 888, not 565 (the fallback order is 888, 565, 555 whatever was asked).
* **The text follows the screen it got** (`SciHiResText::setScreenFormat`, called by the driver after `initGraphics`): an RGB text
  whose screen came out as another RGB format, or as CLUT8, takes the screen's target: the map is loaded again with that target's
  sections (`[font:clut8]` or `[font:rgb]`), the font sets are dropped, a note is logged, and the AA-on-CLUT8 warning applies when
  the map has no `[font:clut8]`. It also follows CLUT8 to RGB: `render_target=clut8` with the user's `rgb_rendering=true` gives an RGB565 screen (**a behaviour change**: before the first review round the screen was CLUT8, in the first round an RGB screen with the text still CLUT8); the `[font:rgb]` chain is then used, an AA face is blended instead of thresholded, and the CLUT8-screen warning is not given. The Mac QuickTime path (`kvideo.cpp` `initScreen(&videoFormat)`) does not report its temporary video format, and the replaced font sets are kept alive (generation counter, `GfxCache` rebuilds its hi-res fonts): an ASan test reported the use after free of the first version.
* `init.cpp` no longer clears the user's `rgb_rendering` / `palette_mods` when the hi-res text uses 1 bpp faces (it only upgrades the
  request). No unit test (the driver needs a running engine); run: KQ1 with a 1 bpp-only map and `rgb_rendering=true`, `render_target`
  unset: `Hi-res text: screen format RGB565@2 for render target clut8` (RGB screen, CLUT8 text); the same map without it:
  `CLUT8 for render target clut8`.
* **This host (SDL2 surface backend under Xvfb)**: `rgb565` gives `RGB565@2`; `rgb555` gives `ARGB1555@2` (the backend's XRGB1555: a
  15-bit screen); `clut8` gives `CLUT8`. `rgb888` is **not** offered: `SurfaceSdlGraphicsManager::detectSupportedFormats` leaves out
  formats wider than the current hardware screen, which is 16 bit when the engine starts, so the engine logs `render_target rgb888 is
  not offered by this backend; using rgb565` and the run is an RGB565 run. `auto` therefore also gives RGB565 here. 32 bit is covered
  by the unit path only (RGB888/ARGB8888 sinks, `hiResPickScreenFormat`), not by a game screenshot.
* DOS (P7): VBE 15/16-bit modes must appear in the DOS backend's `getSupportedFormats()` (RGB555, RGB565 and their order); with that
  the selection above needs no change. The blend now costs three multiply-divides per pixel; its speed on a 386/486 is not measured
  (UNVERIFIED; a lookup table is the obvious step if it matters).

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

`auto` picks RGB (32-bit where offered, else 16, else 15 bit) because a face of the map has coverage; `render_target=clut8` resolves the 1 bpp chain.
NG16J1, fitted by `--binarize threshold`, forms a few syllables badly at 16 px (는 reads like 눈 in LSL1's warning box): a 1 bpp
face for CLUT8 should be a hand-made bitmap face such as NEODG16A.

## 5. Game runs (headless Xvfb, xdotool, ScummVM's Alt+S; scripts `~/work/tmp/aa-games/`)

Build `~/work/tmp/aa-build` (`--disable-all-engines --enable-engine=sci --enable-debug --disable-optimizations`), game data
symlinked from the P3/P4 scratch copies (never in the repository), the generated faces in a scratch `extrapath`
(`~/work/tmp/aa-games/extra`, `data:NG16P.SVF`), the maps in the game folders' `ko/` (`hires_text_map=ko/aa-<map>.map`). KQ1 (SCI0
EGA) and LSL1 VGA (256 colours, run as RGB). Pixel formats are from the log line of section 3. The frames were taken in two rounds:
the first (binary of `618d93da62d`) gave the chain variants; the first review round retook the pair map
frames with the final binary, the LSL1 frames after the warning box was on screen (the script retakes every 5 s until the yellow
box is present, with 5 s of quiet before each shot so no "Saved screenshot" toast is on screen) and the KQ1 CLUT8 frame with the
same room message as the RGB one. All frames named below were viewed.

| run | screen | chain / map | what the screenshot shows |
|---|---|---|---|
| KQ1 `pair` rgb565 | RGB565@2 | [NG16P, NG16JA] | `aa-kq1-rgb565-message.png`: the status line `득점: 0 of 158  킹스 퀘스트 I` and the room message `에드워드 왕의 성 바깥에 서 있다. ...` in two lines: smooth AA (5 colours in a text crop, 3 on CLUT8), no clipping, the baseline on the game's, the box sized from the text; `-parser-prompt.png` (`입력`, typed 봐), `-menu.png` (`파일 행동 속도 소리`, `도움말 F1`) |
| KQ1 `pair` rgb555 | ARGB1555@2 | same | `aa-kq1-rgb555-message.png`: the same frame at 15 bit; the message box differs from rgb565 in 3,498 of its pixels (5-bit against 6-bit green quantisation of the edges and of the box colours); legible, no colour fringe, no wrong colour. The first run asked for rgb555 and got RGB565 (the bug of section 3); the retake shows `ARGB1555@2` |
| KQ1 `pair` clut8 | CLUT8 | [NG16J1, neo] by `[font:clut8]` | `aa-kq1-clut8-1bpp-pair-message.png`: the same message in the 1 bpp pair: hard edges, legible, heavier; a few syllables look off (성은 reads like 성윤: the threshold-fitted jamo set) |
| KQ1 `pair` rgb888 asked | RGB565@2 | same | `aa-kq1-rgb888-requested-fell-back-to-rgb565.png`: log `not offered; using rgb565`; status rows identical to the rgb565 run |
| LSL1 VGA `pair` rgb565 | RGB565@2 | [NG16P, NG16JA] | `aa-lsl1vga-rgb565-warning-box.png` (box with three wrapped lines and the title bar `철갑상어 보건총감의 경고`), `-age-dialog.png` (question and six buttons): AA text over the 256-colour picture and the yellow boxes, black on yellow and white on black correct; the mouse cursor covers some text in one frame |
| LSL1 VGA `pair` rgb555 | ARGB1555@2 | same | `aa-lsl1vga-rgb555-age-dialog.png`, `-rgb555-warning-box.png`: the same at 15 bit; the whole picture is 15-bit (28,546 pixels of the box region differ from rgb565, picture included) |
| LSL1 VGA `pair` clut8 | CLUT8 | [NG16J1, neo] | `aa-lsl1vga-clut8-1bpp-pair-warning-box.png`: the 1 bpp pair; `에는` reads 에눈, `있는` 있눈, `있습니다` 있숩니다 |
| LSL1 VGA `pairl` rgb565 | RGB565@2 | [NG16L, NG16P, NG16JA] | `aa-lsl1vga-rgb565-latin-chain-warning-box.png`: the quotes and colon come from the Latin face, the box width follows its advances |
| LSL1 VGA `neo` rgb565 | RGB565@2 | [NEODG16A] | `aa-lsl1vga-rgb565-neodgm-1bpp-warning-box.png`: the 1 bpp fallback at the same size: crisp, thinner |
| English KQ1 | 320x200 CLUT8 | none | `title.png` is 320x200, no `Hi-res text` line in the log: unchanged |
| KQ1 1 bpp map, `rgb_rendering=true` | RGB565@2, text target clut8 | [NEODG16A] | log `screen format RGB565@2 for render target clut8` (the user's request kept); without the key `CLUT8 for render target clut8` |

The chain-variant frames (retaken in the second review round with the final binary, all viewed and all the same room message): `aa-kq1-rgb565-neodgm-1bpp-message.png`
(NEODG16A at the same 16 px against the AA message: crisp and thinner, the AA NanumGothic about 17 % heavier), `-latin-chain-message.png`
(Latin also AA, lighter than the Hangul), `-jamo-first-message.png` (reversed chain: every syllable from the jamo set), `-18px-message.png`
(NG18P + NG16JA in one box: the 17 against 15 px step makes the spacing slightly uneven), `-3by2-message.png` (the 16 px chain at `size=24`),
`-clut8-aa-face-stamped-message.png` (informational: no `[font:clut8]`, stamped at 128, one warning logged; with `rgb_rendering=true` the
same map gives an RGB565 screen and no warning). The hashes below are those of the final binary (unchanged by the later fixes, which do not
touch the SCI text path).

Syllables in KQ1's `sci-ko.str`: 563 distinct Hangul syllables, 8,038 occurrences; **473 distinct syllables (3,897 occurrences, 48 %)
are not in NG16P** (it has 103), so every message box shows both faces. No seam is visible at the join in any frame.
Stable frames (sha256 of the status rows, y 0..21, 640 px, first 16 hex; the room-message frame is stable too): rgb565 chain
[NG16P, NG16JA] status rows `04e8b1964fcc382b`, message box (110,165)-(530,260) `5b1e065b15f8c471` (also in the rgb888-asked run and under
ASan); rgb555 status rows `75d5af99face0452`, message box `bc9206fc6b8263a0` (also under ASan); CLUT8 pair status rows `243d507e788da6c3`,
message box `3be2b4f27669b8a8`. **The status-row hashes did not change with the rounding fix** (black on white gives the same 5/6-bit
values with truncation and with rounding at the levels 85 and 170); the message-box hashes were first recorded after it. Frames with a typed
`qhk` can show a stray `ㄴ` (the screenshot hotkey leaks its `s` into an open parser line, as in P4-notes), so the prompt frame is not hashed.

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
  preferred 888 over the 16/15-bit formats (`0f5dd9512f6`; the 888-first order was restored in the review round); the format list
  given to `initGraphics` gave the backend's favourite, so rgb555 got RGB565 (`f21d5e16ef7`); the sinks were not the C targets'
  arithmetic and `SvfFont` made ARGB1555 edges transparent (section 9); the text did not follow a screen that came out as CLUT8 or
  another RGB format; `init.cpp` cleared the user's `rgb_rendering`.
* Found, not a defect of the engine: SDL's format list caps at the current hardware depth, so `rgb888` falls back to RGB565 here;
  a 1 bpp jamo set fitted with `--binarize threshold` (NG16J1) forms some syllables badly (not usable as the CLUT8 face of a shipped
  map; NEODG16A is).
* UNVERIFIED / not done: a game screen at a real 32-bit format (the backend does not offer it here); DOS VBE 15/16-bit modes and
  speed (P7); mingw build (not built: no cross toolchain run in this round); 8 bpp faces in a game run (the 8 bpp face is a unit
  fixture only; jamoset cannot make an 8 bpp jamo set); RGB555 with a big-endian layout in ScummVM (PixelFormat has none, only the C
  targets); engines other than SCI; the 1 bpp NG16J1 of the CLUT8 side is a generated face, not a fixture; the double
  quantisation of the CLUT8 scaled bitmap in `UpscaledGfxDriver::updateScreen` (the bitmap is cut to the screen format, then the
  text is blended over it, as the C targets do; blending against the exact palette colour with `convertSpan` would need a second
  conversion pass over every updated rectangle and was not done: documented in section 2.1); a lookup table for the blend.

## 9. Review round (2026-11-23): Opus and agy reviews of `618d93da62d`

Reports: `docs/korean/plans/reviews/AA-opus.md`, `reviews/AA-agy.md`. Each agy finding was checked against the code first.

| finding | verdict | fix |
|---|---|---|
| R1 (both): three LSL1 screenshots show only the title picture, one file missing, the KQ1 CLUT8 caption does not match, the strip clips a digit | real | frames retaken (section 5, viewed), captions rewritten, the strip is 290 px wide, the claims without an image struck |
| agy 1 / Opus m-1, m-3: the format list could never match a missing target; the text keeps its RGB target on a CLUT8 screen (no `[font:clut8]`, no warning) | real | `hiResPickScreenFormat` (pure, tested) and `SciHiResText::setScreenFormat` (`f21d5e16ef7`, `bc0d02723f7`); the first list version was itself wrong (section 3) |
| agy 2: `SvfFont` on ARGB1555 makes faint edge pixels transparent | real (red: `svffont-1555-alpha` mutation fails the new test) | `blendPixel` sets alpha opaque (`8cde12f534a`) |
| agy 3 / Opus m-2, R3: truncating sinks, false statements | real | channel-width blend equal to the C targets (`8cde12f534a`); the statements in 2.1, `text_compose.h` and the test comments corrected; DESIGN 10 has the rule |
| agy 7 / Opus: `init.cpp` clears `rgb_rendering` | real | only upgrades (`bc0d02723f7`); run in section 3 |
| agy 5: the `auto` mutation claim is false, "warned once" untested, the sink checks restate the formula | real | table 2.2 re-run; `test_autoPicksByWhatIsOffered`; once across font ids; sink checks against the svfline goldens |
| Opus m-4: `auto` prefers 565 over 888 | decision | 888 first again (`b7abdf9ba41`) |
| agy 6 / Opus m-5: generator hygiene, cache key | real | `102b6d51512` (sys.executable, explicit fontTools error, `--only`, shutil, unique temp directory, tool versions and submodule state in the key) |
| agy 8, 10: comment, parallel name arrays, include order, copyright note | real | `targetName()`, the include moved up, the comment, the NanumGothic copyright and Reserved Font Names beside the `COPYING.OFL` entry |
| agy 4 "double quantisation" in `updateScreen` | not changed | section 8 |

### 9.1 Second round (re-reviews of `5ff4af9ce79`: Opus approved, agy approve with changes)

Reports: `reviews/AA-opus-r2.md`, `reviews/AA-agy-r2.md`.

| finding | verdict | commit | evidence |
|---|---|---|---|
| use after free in `setScreenFormat` (deleted sets under the font cache) and the Mac QuickTime `initScreen(&videoFormat)` mutating the text | real | `d7577920323` | red: the new `test_fontsOutliveTheScreenSwitch` made the ASan build report `heap-use-after-free` in `GfxFontHiRes::getCharWidth` before the fix; after it the ASan suite is clean. The old sets are kept, a generation counter makes `GfxCache` rebuild its hi-res fonts, the driver reports the screen only when `srcRGBFormat` is null. The QuickTime path itself was not run (no Mac data): the guard is the `!srcRGBFormat` condition |
| `SvfFont` on 8-bit alpha surfaces: opaque dark fringes | real | `06e5d3af72a` | red: `test_svfFontAlphaSurfaces` failed for all three 8-bit formats (547 and 1,138 pixels) before the fix; Porter-Duff over now, exact per pixel for transparent and opaque backgrounds, colour alpha 255 and 128; 1-bit alpha stays opaque |
| clut8 + `rgb_rendering` + AA map: RGB screen, "CLUT8 screen" warning, AA thresholded | real | `d7577920323` | `test_textFollowsTheScreenItGot`; run: `Hi-res text: the backend gave a rgb565 screen, not clut8: the text follows the screen`, no warning (the same map without `rgb_rendering`: CLUT8 screen and the warning). **Behaviour change**, recorded in section 3 |
| stale comments and docs (`convertSpan`, "16-bit first", the 1 LSB tolerance in DESIGN 10) | real | docs commit of this round | DESIGN 10 now says: equal to the C targets byte for byte at the same screen format; `auto` gives 888 on a desktop that offers it, 565/555 on DOS |
| `mkgolden.py` cache robustness, content hash, `--only` validation | real | `412c1dbf35c` | `gen_aa_faces.py x --only NG16Q` exits 2; `mkgolden.py --check` rc 0 |
| `rgb555` falls to 888 when 565 is not asked and 888 is offered | left | | owner's call (section 3) |
| six `aa-kq1-*` screenshots from before the blend fix | real | | retaken with the final binary, viewed; status rows and message box hashes unchanged (section 5) |

Mutations of this round (applied alone, tests rebuilt, file reverted): `svffont.cpp` without the 8-bit-alpha branch fails
`test_svfFontAlphaSurfaces`; `setScreenFormat` ignoring a CLUT8 text fails `test_textFollowsTheScreenItGot`; the generation not bumped fails
`test_fontsOutliveTheScreenSwitch`.

Totals after the round: SCI 729 OK, ASan 729 OK (0 sanitizer lines), SCI+SCI32 729 OK, no-engine 647 OK.
