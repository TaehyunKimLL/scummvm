# AA fix round, third pass (Claude lane)

Scope: `5ff4af9ce79..4448a6a0e44` (code: `d7577920323` font sets kept across a screen switch, the text follows an RGB screen;
`06e5d3af72a` SvfFont Porter-Duff over; `412c1dbf35c` generator and cache checks) against `AA-rereview.md` (N-1..N-5) and the
coordinator's list. Everything below was run unless marked otherwise.

Scratch locations:
- source: `git archive 4448a6a0e44` in `~/work/tmp/aa-review-claude/src3`;
- ASan build: `build3` (`--enable-asan`, SCI, debug), tests and `scummvm`;
- harnesses: `h/pd.cpp` and `h/pdmodel.py`;
- game runs: `games/r3.sh`.

The worktree is untouched except for this file.

## Verdict

**Not yet: one major (M-1).** The fix for "clut8 + `rgb_rendering`: the text follows the RGB screen" switches the text's target
and its map sections. The driver keeps the compose mode it was given before the switch, so the AA text is still thresholded. Run
evidence below: 2 colours, pixel-identical to the CLUT8 stamp. The warning is suppressed. With a paired map, the user's explicit
`render_target=clut8` now gets the AA chain thresholded instead of the map's 1 bpp `[font:clut8]` chain, which is a regression
against `5ff4af9ce79`. The use-after-free fix, the SvfFont alpha fix and the generator checks are correct.

Required: M-1 (a one-line driver fix plus a run). The rest is optional. With M-1 fixed, this round is sound to build on for SCUMM
S2 and DOS.

## Second-round findings

| finding | status | evidence |
|---|---|---|
| N-1 use-after-free on the screen switch / QuickTime path | **addressed** | see (1) |
| N-2 SvfFont opaque fringes on 8-bit alpha | **addressed** | see (2) |
| N-3 clut8 + `rgb_rendering`: misleading warning, AA thresholded | **not addressed**: the warning is gone, but the text is still thresholded. The fix is incomplete and changed behaviour (M-1) | see (3) |
| N-4 stale DESIGN 10 tolerance line | **addressed** | the line now says byte-equal at the same format, and that `auto` is 888 on a desktop and 565/555 on DOS |
| N-5 KQ1 frames from before the blend fix | addressed per the record | not re-checked: no image changes were asked of me in this pass |

## (1) Use-after-free and ownership

**Tests.** ASan build of the checkout: `make test` gives 729 tests OK and 0 lines from AddressSanitizer or LeakSanitizer.
`./test/runner SciHiResTextTestSuite` gives 11 OK.

**Red check.** In a scratch copy I put back the old `delete it->_value` in `setScreenFormat` and rebuilt under ASan.
`test_fontsOutliveTheScreenSwitch` then reports `heap-use-after-free` in `Sci::GfxFontHiRes::isOriginal` called from
`getCharWidth`. The test catches the bug; the source was restored and rebuilt.

**Game runs** under the ASan binary (KQ1, four configurations, `ASAN_OPTIONS=detect_leaks=0`): 0 AddressSanitizer lines.

**Ownership review:**
- **Who holds `GfxFontHiRes`.** `hiResFont()` has a single caller, `GfxCache::getFont`. The cache is the only owner. Callers
  keep the raw pointer: `GfxText16::_font` (`text16.cpp:69-77`) and the SCI32 text and controls classes (SCI32 has no hi-res
  text). `GfxText16` refetches only when the font id changes.
  - When the generation changes, the purge runs inside the next `getFont`. The caller assigns the result straight away, so it
    never keeps a purged pointer.
  - A caller that does not change font id keeps the old `GfxFontHiRes` until it does. That font draws through its retired set,
    which is alive. So the text is stale, but memory-safe.
  - Upstream already purges inside `getFont` when the cache reaches `MAX_CACHED_FONTS`, so this is the existing pattern.
- **Memory growth.** Sets are retired only when the target really changes (`got != _target`). After a switch the driver asks
  for the new target, so the next `initScreen()` gets the same format and does not switch again. A `kShowMovie` loop cannot flip
  it, because the video's `initScreen(&format)` call no longer reports the format (next item). The only growth is one set per
  real switch, freed with `SciHiResText`. Code reading only: I did not run a loop, since no Mac data is here.
- **The QuickTime path.** `kvideo.cpp:147` calls `initScreen(&format)` only for a non-CLUT8 video format. `&format` is the
  address of a local, never null, so `srcRGBFormat` is non-null there and the guard skips `setScreenFormat`. The restore at
  `kvideo.cpp:218` is `initScreen()` (null). It reports the restored screen, which is the target's own format, so nothing
  switches. UNVERIFIED by a run (no Mac QuickTime game here); the condition is plain from the code.

## (2) SvfFont alpha

`h/pd.cpp` includes `graphics/fonts/svffont.cpp` and calls the real `plotPixel`. The inputs:
- formats ARGB8888, RGBA8888 and ABGR8888;
- text colour (250, 240, 10) at alpha 255, 128 and 0;
- backgrounds transparent black, transparent coloured, opaque, and 50 %;
- all 256 coverages.

That is 9,216 pixels. `h/pdmodel.py` compares them with two models.

**Against an ideal Porter-Duff source-over** (real arithmetic, straight alpha, source alpha = coverage × colour alpha): max
difference **0** over transparent backgrounds and over opaque backgrounds at colour alpha 255. It is **1** (integer rounding of
the source alpha) in 94-145 of 256 pixels for colour alpha 128 over opaque and 50 % backgrounds, and at alpha 255 over the 50 %
background. So the implementation is correct over.

**Against `TTFFont::renderGlyph`** (`graphics/fonts/ttf.cpp:636-657`, modelled in double precision as in C):
- Equal within 1 (TTF truncates) only for colour alpha 255 over an opaque background.
- Over a transparent background TTF gives up to 250 difference. TTF computes `sR * sAn + dR * dAn * (1 - sAn) / oAn` and divides
  only the second term by `oAn`, so over a transparent pixel it writes the colour pre-multiplied by coverage with alpha = coverage.
  That darkens the text: the very fringe N-2 was about.
- TTF also ignores the colour's alpha below full coverage.

So the new code is right and TTF is not. The comments that say "as TTFFont::renderGlyph" are wrong: `svffont.cpp:58`,
`test/graphics/svf_aa.h:826` and the record. That is nit n-1.

## (3) Text follows the screen: clut8 + `rgb_rendering`

KQ1 runs with the ASan binary (log line, then colour count inside the message box):

| map | render_target | rgb_rendering | log | message box colours |
|---|---|---|---|---|
| aaonly (AA only) | clut8 | true | `the backend gave a rgb565 screen, not clut8: the text follows the screen`; `RGB565@2 for render target rgb565`; no warning | **2** (cyan 22,054, black 3,446) |
| aaonly | clut8 | unset | `CLUT8 for render target clut8` + AA-on-CLUT8 warning | **2** (cyan 22,054, black 3,446): the same pixels |
| pair (`[font:rgb]` AA, `[font:clut8]` 1 bpp) | clut8 | true | as the first row | **2** (22,054 / 3,446) |
| aaonly | rgb565 | unset | `RGB565@2 for render target rgb565` | **4** (cyan, black, `(57,170,173)`, `(24,85,82)`): blended |

**M-1 (major) After the CLUT8→RGB switch the driver still composes in the old mode, so the "follow" does not blend.**
`engines/sci/graphics/screen.cpp:157` hands the driver `hires->getComposeMode()` (kStamp for a clut8 text) before
`_gfxDrv->initScreen()` runs at `:205`. `setScreenFormat` then changes `_target` to rgb565, but nothing tells the driver.
`UpscaledGfxDriver::compositeText` (`upscaled.cpp:205-210`) maps the stale kStamp to kThreshold on the RGB screen.

The record (9.1 and section 3: "an AA face is then blended, not thresholded") and the code comment at `hirestext.cpp:116-117`
say otherwise.

Consequences:
- **aaonly map:** the AA face is thresholded at 128, as before, but the one-time warning is gone.
- **pair map:** `render_target=clut8` with `rgb_rendering` used to give the map's 1 bpp `[font:clut8]` chain (`5ff4af9ce79` run in
  AA-rereview.md). Now it gives the AA `[font:rgb]` chain, thresholded. That is the defined poor fallback, on a map that has a
  proper 1 bpp face, with no warning.
- The RGB→RGB and RGB→CLUT8 switches are unaffected: the same mode, or the driver stamps because `_pixelSize == 1`.

Fix: after `initScreen`, give the driver the text's current mode. Either move the `setTextPlane(...)` call in
`GfxScreen::GfxScreen` after `_gfxDrv->initScreen()`, or call `_gfxDrv->setTextPlane(&hires->getPlane(), hires->getComposeMode())`
again after `setScreenFormat`. Then run the first row again and expect 4 colours.

Decide also (owner) whether an explicit `render_target=clut8` should follow an RGB screen at all. Keeping the `[font:clut8]`
chain and only the compose mode would honour the explicit setting.

**Other consumers of the target after a switch:**
- The map is reloaded with the new sections (correct).
- The 2x `TextPlane` holds indices and coverage, independent of the target (unaffected).
- The cursor is unaffected: `replaceCursorPalette`, and the upstream `rgb_rendering` path.
- Screenshots and thumbnails convert whatever screen there is.
- `wantsRgb()` is true after the switch. It only matters at the next `initScreen()`, which then picks an rgb565 format: the same
  screen.
- The driver compose mode is the one consumer that is not updated (M-1).

## (4) Generator and docs

- `mkgolden.py --check` from an empty `TMPDIR`: rc 0.
- `gen_aa_faces.py x --only NG16Q`: rc 2, "unknown face NG16Q (known: ...)".
- Half-built cache (one `.SVF` removed): `mkgolden.py --check` regenerates all faces (about 75 s), then exits 1 with
  "exists but lacks faces: remove it and run again". That is safe, but wasted work and a manual step. Nit n-2: check the cache's
  completeness before generating, and replace a broken cache directory instead of refusing.
- DESIGN 10 is correct for what the code does: byte-equal at the same format; across formats, 1 LSB of the narrower format,
  consistent with my first-round numbers.

## New findings

- **Blocker:** none.
- **Major:** **M-1** (above).
- **Minor:** none.
- **Nit:**
  - **n-1:** "as TTFFont::renderGlyph" in `svffont.cpp:58`, `svf_aa.h:826` and the record. Say "Porter-Duff source-over
    (straight alpha)". TTF's own blend darkens over transparent pixels and ignores colour alpha; that is upstream behaviour, so
    leave TTF alone.
  - **n-2:** a half-built face cache costs a full regeneration, then a refusal (above).
  - **n-3:** `test_fontsOutliveTheScreenSwitch` proves the old font stays valid, but no test drives `GfxCache::getFont` across
    a generation change. The purge needs a `ResourceManager`. Acceptable; the record could say the cache side is covered by
    reading only.

## Required

1. **M-1:** update the driver's text compose mode after `setScreenFormat`, or set it after `initScreen`. Re-run
   `aaonly`/`pair` with `render_target=clut8` and `rgb_rendering=true` and expect a blended box (4 colours). Correct the record's
   claim.
