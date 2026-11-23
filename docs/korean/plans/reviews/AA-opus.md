# AA round review (Claude lane, independent)

Scope: `4a8b9907b2c..618d93da62d`, non-docs commits `4f5c9a9fa36`..`a4e3435ad26` (AA goldens and suite, CLUT8 stamp record,
`rgb555` target, backend pixel format request in `GfxDefaultDriver::initScreen`, 15/16-bit tests, RGB/CLUT8 map pairing and the
CLUT8 warning, 1 bpp pair face), with `docs/korean/plans/AA-tests.md`, `DESIGN.md` 5/10 and `PATCHES.md`. Read-only on the
worktree; everything was run on a `git archive` copy of `618d93da62d` in `~/work/tmp/aa-review-claude/src` with out-of-tree
builds in `~/work/tmp/aa-review-claude/build{,32}`.

## Verdict

**Sound to build on (SCUMM S2 / DOS), with no blocker and no code major.** The code is correct where the brief asked:
565/555/1555 shifts, the alpha bit, the `rgb` qualifier for 555, the target selection and the fallback, the CLUT8 warning, and
non-Korean or `hires_text=false` targets (unchanged). The goldens come from `tools/svfline.py`, not from the code under test,
and regenerate byte-identically. The one major item is about evidence: three of the four LSL1 screenshots do not show what the
record says they show. Required before building on it: R1 to R3 below. The rest is optional.

## Verification run

| check | command / where | result |
|---|---|---|
| SCI test suite at HEAD | `configure --disable-all-engines --enable-engine=sci --enable-debug --disable-optimizations; make -j16 test` (build) | **723 tests OK** (two harness-table skips, as before) |
| SCI + SCI32 | `--enable-engine=sci,sci32`, full `make test` (build32), `ENABLE_SCI32 = 1` | **723 OK**; `default.cpp`, `init.cpp`, `hirestext.cpp`, `sci.cpp` build without warnings |
| fixtures/goldens reproducible | `TMPDIR=<scratch> python3 -I tools/korean/mkgolden.py --check` with an empty face cache (fresh `gen_aa_faces.py` run, 85 s) | rc 0; face sha256 prefixes equal the AA-tests.md table (`e68fe1ce44ba5192` NG16P ... `13c31a3d72e6eb7b` NG16J1) |
| font names in faces | `strings -n 5 *.SVF \| grep -ic nanum` | 0 in all six faces |
| mutation: `font_map.cpp:397` `case kHiResTargetRgb555:` deleted | `mut.sh` (scratch copy, rebuild, run, revert) | 4 failed: `test_rgb555`, `test_rgb555QualifiesAsRgb`, `test_screenFormatFollowsTheTarget`, `test_aaForRgbAndOneBppForClut8` (claim holds; more tests fail than the record lists) |
| mutation: `svf_draw.c:484-486` `blend555` without `+ 127u` | same | `test_rgbTargets` (547/547/793 bad px), `test_every_case_at_15_and_16_bit` (every 555 case) fail: claim holds |
| mutation: `hirestext.cpp:127-129` flag and warning deleted | same | `test_aaFaceOnClut8IsStampedAndWarnedOnce` fails: claim holds |
| mutation (mine): `default.cpp:119` `== target` -> `!= target` | same | **723 OK: the backend format request is not covered by any test** (m-1) |
| screenshots | Read of all 18 `aa-*.png`, distinct-colour count of text regions, md5 | see section "Screenshots" |
| status-row claims | PIL diff, y 0..21 | rgb565 vs rgb555 1,427 of 14,080 px (claim holds); rgb888-asked vs rgb565 0 px (holds) |

## Findings

### Blocker

None.

### Major

**M-1 (evidence) Three LSL1 screenshots show only the title picture. The record and the captions describe text that is not
in them.** `~/work/tmp/ko-screenshots/`:

- `aa-lsl1vga-rgb565-warning-box.png` (known) and `aa-lsl1vga-clut8-1bpp-pair-warning-box.png` are **byte-identical**
  (md5 `1554e81bd1d96fe563587260f3ce6748` both): the "Lounge Lizards" title with the "Saved screenshot" OSD, no warning box, no
  Hangul.
- `aa-lsl1vga-rgb565-latin-chain-warning-box.png`: the same title picture without the OSD, no text.

So AA-tests.md section 5 makes these claims with no image behind them: "LSL1 VGA `pair` ... rgb565 ... warning box with
wrapped text"; "LSL1 VGA `pair` clut8 ... the 1 bpp pair; `는`/`눈` look-alikes in 있는, 에는"; "LSL1 VGA `pairl` rgb565".
The only LSL1 image that shows text is `aa-lsl1vga-rgb555-age-dialog.png`: 7 colours in the dialog crop and 37 in the bottom
hint text, so blended. An LSL1 run at RGB565 and LSL1 at CLUT8 with the 1 bpp pair are not shown anywhere. Fix (R1): retake
the three after the warning box is up (wait for the text before Alt+S, as in the KQ1 runs), or strike the claims from
section 5 and from captions.txt.

### Minor

**m-1 The fix in `c8abfd7eb05` (the screen format follows `render_target`) has no test.** `engines/sci/graphics/drivers/default.cpp:112-123`.
Inverting the filter at `:119` (`!= target`) passes all 723 tests (table above). `test_screenFormatFollowsTheTarget` tests
`SciHiResText::getScreenFormat()`, and production uses that only as the fallback push at `:122`. That push cannot take effect
(next item). Fix (R2): move the selection into a pure function in `graphics/hires_text/hires_options.cpp`, for example
`Common::List<PixelFormat> hiResScreenFormats(HiResRenderTarget, const Common::List<PixelFormat> &supported)`. Call it from
`initScreen` and test it with an SDL-like list (565, XRGB1555, 555, CLUT8), an OpenGL-like list (RGBA8888, RGB565, RGBA5551,
...) and a list without the target.

**m-2 The quantisation is described wrongly in the record and in a test comment. The behaviour itself is acceptable
(judgement below).** AA-tests.md 2.1 says the sink "truncates, so a value is up to 7 below the exact blend ... truncation
darkens an AA edge by at most 7/255". `test/graphics/svf_aa.h:982-984` says "no value is further than half a channel step from
the exact blend", but the test asserts `maxErr <= 7` (`:1015`), which is a full 5-bit step. Measured over every (bg, fg) pair
of 5/6-bit channel values and coverage 1..255 (script `~/work/tmp/aa-review-claude/quant.py`, which reproduces
`TextCompose::composeSpan` = `colorToRGB` bit-replication expand, 8-bit blend with `+127`, `RGBToColor` truncation, against
`blend565`/`blend555` of `svf_draw.c:466-487`):

| channel | sink - C target (LSB) | pixels that differ | at the 2 bpp levels 85/170 | sink vs exact blend (8-bit units) | C target vs exact |
|---|---|---|---|---|---|
| 5 bit | -1: 24,138, 0: 212,844, +1: 24,138 | 18.5 % | 16.6 % | -7.50 .. +7.50 | -4.86 .. +4.86 |
| 6 bit | -1: 99,042, 0: 846,396, +1: 99,042 | 19.0 % | 16.7 % | -3.50 .. +3.50 | -2.96 .. +2.96 |

The error is **symmetric with mean 0**, not a darkening. The 5-bit expand maps 31 to 255, so an 8-bit blend of 248 comes back
as 255, which is 7 *above*. Fix (R3): correct AA-tests.md 2.1 and the comment at `svf_aa.h:982-984`: "within one LSB of the C
target in about 17 % of edge pixels, at most 7.5/255 from the exact blend, either direction".

**Judgement on the policy question: acceptable, not a bug.**

- The "byte-identical to `tools/svfline.py`, exact by default" contract (DESIGN 5.2, `svf-renderer-accuracy-policy`) covers
  the C renderer's output. In ScummVM that output is the GRAY8/coverage plane. It is byte-exact here: every AA case's plane
  equals the svfline golden (`test_planeKeepsCoverage`, `test_every_case_at_15_and_16_bit`).
- Colour compositing is glue in `TextCompose`. It never goes through the C RGB targets.
- The deviation stays inside the bound the policy grants a blend (+-1 LSB per channel) and inside DESIGN 10's tolerance for RGB
  runs (1 LSB).
- Linux and DOS use the same `composeSpan`, so the screens match each other exactly.

What is missing is only the documentation: DESIGN does not say that the ScummVM RGB composite is not the C target's
arithmetic. Optional, not required: blend in the target's channel width for 2-byte formats. Extract each channel with
`fmt.rShift` and the loss values, then apply `(b * (255 - a) + f * a + 127) / 255`. That is the `blend565`/`blend555`
arithmetic, so the output matches the C targets bit for bit. It also skips the expand and truncate steps, which helps on a
386/486 (speed UNVERIFIED).

**m-3 A screen that ends up CLUT8 while the text target is RGB gets no AA warning and misleading messages.**
`default.cpp:121-123`, `hirestext.cpp:124`. When no backend format has the target, `formatList` is `{getScreenFormat()}`.
`initGraphics(list)` intersects that with the backend's list (`engines/engine.cpp:481-492` `findCompatibleFormat`), finds
nothing and returns CLUT8. So the push never selects a format, and the comment "else the 8-bit screen" describes the
accident, not the code. On that screen the `[font:rgb]` AA chain is stamped (`upscaled.cpp:207`, `_pixelSize == 1`). The
one-time CLUT8 warning is not given because `_target` is still RGB, and the log says "RGB rendering not available in this
ScummVM build" (`default.cpp:148-149`). Reachability is UNVERIFIED: it needs `getSupportedFormats()` to change between
`SciEngine::run` (`sci.cpp:443`) and `initScreen`. Fix: in `initScreen`, after `_format` is known, warn once ("hi-res text wanted
rgbNNN, the backend gave CLUT8: AA faces are stamped"). Or let the driver tell `SciHiResText` the real format so that
`getFontSet` picks the `[font:clut8]` chain.

**m-4 `auto` now prefers RGB565 even where RGB888 is offered. This changes the game's own VGA picture on desktops.**
`graphics/hires_text/hires_options.cpp:190-198`. Before this round `auto` took 888 when offered. A VGA DAC has 6 bits per
channel. RGB565 keeps 5 bits of red and blue, so adjacent DAC levels merge in pairs (64 to 32 levels); RGB555 does that to
green too. CLUT8 and 888 keep the palette exact. Every Korean VGA target with an AA map (LSL1 VGA, QFG/KQ5 class games) loses
that palette precision on a 32-bit desktop, for the whole picture, not only the text. DESIGN 5.1 records the order, so this
may be intended. The owner's words were "15/16 bit required, 24/32 secondary". Confirm with the owner: either keep it, or
prefer 888 when offered and the game is VGA (DOS VBE would still get 565/555 because it offers no 888).

**m-5 The face cache key does not include the rasteriser version.** `tools/korean/mkgolden.py` `aa_faces()` keys the cache
directory on the generator, the TTFs and the submodule commit. `mkfont.py` rasterises through Pillow's FreeType, whose version
is not in the key. After a Pillow or FreeType upgrade a stale cache keeps passing `--check` and hides a change in the
generated faces. Fix: add `PIL.__version__`, `PIL.features.version("freetype2")` and `fontTools.version` to the key.
Related nit: `gen_aa_faces.pylib()` probes with a plain `python3 -c` (no `-I`, so the user site loads) and passes `--pylib ""`
when fontTools is missing. Fail with a message instead.

### Nit

- `hirestext.cpp:123-130`: the warning test checks only the flag (`warnedCoverageOnClut8`). Deleting just the `warning(...)`
  call would pass. Acceptable for a log line.
- `hires_options.cpp:207` duplicates the name table of `font_map.cpp:405`. Expose `HiResFontMap::targetName()` and use it in
  both, and in the debug line at `default.cpp:129-131`, which prints the target as a number (`render target 2`).
- `hirestext.cpp:124-129`: `!_warnedCoverage` is tested in both loops. A `break` after the warning reads better.
- `test_dumpWhenAsked` writes into the current directory when `aa-dump/` exists there. It runs only on request. Fine, but
  document the trigger next to the test.

## Questions the brief asked, answered

- **Byte order, shifts, alpha.** `hiResTargetFormat` 565 = `(2,5,6,5,0,11,5,0,0)` and 555 = `(2,5,5,5,0,10,5,0,0)`, both
  correct. `hiResTargetOf` takes 5-5-5 with or without the alpha bit and in either channel order (tested). The sink reads and
  writes native-endian `uint16`, which is right for ScummVM surfaces. `RGBToColor` sets the 1555 alpha bit on every written
  pixel (`test_alphaBitOf1555`). The game's own pixels get it too, through `_internalPalette` built by `RGBToColor`.
  Big-endian 555/565 exists only in the C targets and is golden-tested there.
- **Backend refusal.** The list passed to `initGraphics` is a subset of the backend's own list, so the transaction is no more
  likely to fail than before. If it fails, the upstream "Could not initialize color format" dialog shows and the backend keeps
  its old format (m-3 covers the text side).
- **rgb888 on SDL.** `SurfaceSdlGraphicsManager` lists no format wider than the current hardware screen. The record's
  fallback note and the pixel identity hold (0 px difference).
- **Screenshots and thumbnails.** `Graphics::createScreenShot` and `createThumbnailFromScreen` (`graphics/scaler/thumbnail_intern.cpp:170-210, 281`)
  convert 1-, 2- and 4-byte screens, ARGB1555 included, to 565. SCI saves use `Graphics::saveThumbnail`. Both show the
  composited text. Palette-mode (CLUT8) screenshots are also converted to 565, which is why the CLUT8 and RGB565 LSL1 files can
  be byte-identical. Nothing new is needed here.
- **Mouse cursor in RGB.** This is the upstream `rgb_rendering` path: `replaceCursorPalette(_currentPalette)` in
  `UpscaledGfxDriver::setPalette`, and the backend converts the CLUT8 cursor. No change in this round. The cursor covering text
  in the LSL1 shot is the backend overlay, not a bug.
- **Non-Korean and `hires_text=false` targets.** The new branch requires `g_sci->getHiResText()`. That is created only for
  `_textMode.hires` (`sci.cpp:440`) and deleted when `setup` fails, which includes `!options.enabled`
  (`hirestext.cpp:66-68`). Otherwise `initScreen` takes the untouched `else` (`default.cpp:124-126`), and English targets are
  unchanged. Out of scope, from an earlier round: `drivers/init.cpp:147-148` replaces the user's `rgb_rendering` and
  `palette_mods` with `wantsRgb()` for any Korean target, so a Korean CLUT8 run ignores `rgb_rendering=true`. Worth one line
  in DESIGN.
- **SCI32.** Builds and passes (table). The change touches only the SCI16 driver.
- **Goldens independent?** Yes for coverage and the C targets: `svfline.py`, Python, with no shared code. The sink checks
  restate the sink's formula (`MIX` plus `RGBToColor`), so they catch a change to the formula but not a wrong formula. The
  quantisation tests (`test_levelsSurviveQuantisation`, `test_eightBitRampAtFiveBits`) are the property checks that cover
  that, with the comment fix in m-2.
- **Flakiness.** None seen. Every test is deterministic, and the one file-writing test runs only on request.
- **Licence hygiene.** Only NanumGothic (OFL 1.1, with Reserved Font Names). The faces carry no name (`strings`: 0). The
  fixture header gives the TTF sha256 and the OFL pointer. `LICENSES/COPYING.OFL` has the entry. NG16J1 and the other run-time
  faces are not committed.
- **Upstream hygiene.** `default.cpp` and `init.cpp` are in PATCHES.md (`ko/03-sci`), and `test/module.mk` (`svf_aa.h`) and
  `LICENSES/COPYING.OFL` are there too. No upstream file is changed outside PATCHES.md. Style: tabs, `Common::` containers.
  The `debugC` is behind `kDebugLevelKorean`.

## Screenshots that do not prove their caption

| file | caption claims | shows |
|---|---|---|
| `aa-lsl1vga-rgb565-warning-box.png` | warning box, AA chain, RGB565 | title picture + "Saved screenshot" OSD only; byte-identical to the next |
| `aa-lsl1vga-clut8-1bpp-pair-warning-box.png` | warning box, 1 bpp NG16J1 pair, 눈/는 look-alikes | the same file (md5 `1554e81b...`): title picture only |
| `aa-lsl1vga-rgb565-latin-chain-warning-box.png` | three-face chain NG16L, NG16P, NG16JA | title picture, no text |

The other 15 match their captions. KQ1 RGB crops have 4-5 colours in the message box (more at 3/2: 14). The CLUT8 crops have
2. The stamped-AA CLUT8 frame differs from the 1 bpp-pair frame in 482 status-row pixels, so the faces differ, as claimed. The
policy strip shows the 12 rows described. Two things cannot be judged from the images: the uneven 17/15 px step in
`aa-kq1-rgb565-18px-message.png` and the one-time warning of the stamped run, which is a log claim.

## Required fix list

1. **R1 (M-1)**: retake the three LSL1 screenshots with the warning box on screen (rgb565 `pair`, clut8 `pair`, rgb565
   `pairl`), or strike their rows and claims from AA-tests.md section 5 and captions.txt.
2. **R2 (m-1)**: make the backend format request a pure function and unit-test it (SDL-like, OpenGL-like and missing-target
   lists), so that inverting `default.cpp:119` fails a test.
3. **R3 (m-2)**: correct the quantisation text in AA-tests.md 2.1 and `svf_aa.h:982-984` (symmetric, +-1 LSB against the C
   target, +-7.5/255 against the exact blend), and add one line to DESIGN 5.1 or 10 saying the RGB composite is glue arithmetic
   within 1 LSB of the C targets.

Optional: m-3 (warn when an RGB text target gets a CLUT8 screen), m-4 (owner to confirm 565 over 888 on desktops), m-5 (cache
key), the exact-width blend in m-2, nits.
