# AA fix round, fourth pass (Claude lane)

## Scope and setup

Commits `4448a6a0e44..fcc083e56cc`:
- `5a88c37c1d9`: `SvfFont::compositeOver`
- `54a15ba204a`: an explicit `render_target=clut8` wins; compose-mode refresh; `takeStale()`
- `6f30c3a617c`: the cache check runs first

Reviewed against `AA-rereview2.md` (M-1, n-1..n-3), agy's Critical and the coordinator's four checks. Everything below was run.

Scratch (all under `~/work/tmp/aa-review-claude/`, never `/tmp`):
- source: `git archive fcc083e56cc` in `src4`;
- builds: `b4n` (SCI), `b4a` (SCI + ASan) and `b4s` (SCI + SCI32);
- harness: `h/pd2.cpp`;
- KQ1 matrix: `games/r4.sh` and `r4b.sh`, my copy of the `~/work/tmp/aa-games` setup, run with the ASan binary.

The worktree is untouched except for this file.

## Verdict

**Approved. Sound to build on for SCUMM S2 / DOS. No blocker, no major, no minor.**

- **M-1** is resolved by the decision (an explicit clut8 wins) and the runs confirm it.
- **agy's Critical** (the channel wrap) is gone: an exhaustive run over 100 M pixels found 0 channels outside their inputs.
- **Tests and scripts:** all three builds, the ASan runs and both `--check` scripts are clean.

Four nits are left (end of file), all optional.

## Earlier findings

| finding | status | evidence |
|---|---|---|
| M-1 clut8 + `rgb_rendering` thresholded the AA text without a warning; the paired map lost its 1 bpp chain | **addressed (by decision)** | (2): the AA-only map is stamped and warned, the paired map keeps its 1 bpp chain, every RGB target blends |
| agy Critical: wrap in the over-rounding | **addressed** | (1): 0 channels outside [min, max] in 100,663,296 pixels |
| n-1 TTF comments | **addressed** | the comments now say "ideal Porter-Duff over" |
| n-2 half-built cache regenerated, then refused | **addressed** | refused at once: rc 1 in 0 s, with the "exists but lacks faces" message |
| n-3 `GfxCache` generation path untested | **partly** (as the record says) | `takeStale()` is tested; my scratch test (2) drives it on every switch path; `GfxCache::getFont` itself is covered by reading (3) |

## (1) `SvfFont::compositeOver`: exhaustive, against an independent exact model

**Setup.** `h/pd2.cpp` calls the real `Graphics::SvfFont::compositeOver` (from `src4/graphics/fonts/svffont.cpp`).

The sweep is exhaustive: coverage 0..255 x text alpha 0..255 x background alpha 0..255, for all three formats (ARGB8888, RGBA8888,
ABGR8888). Each pixel carries channel pairs (text/background) 255/255, 255/0 and 0/255 in one run, and 0/0, 128/37 and 1/254 in a
second. That is 33,554,432 pixels per format.

The model is a double-precision source-over with straight alpha:
- `sa = cov/255 * a/255`
- `ea = sa + da(1 - sa)`
- `ch = (t*sa + b*da*(1 - sa)) / ea`
- the pixel is unchanged when `ea = 0`.

The model is written independently of the code.

| check | ARGB8888 / RGBA8888 / ABGR8888 (identical) |
|---|---|
| channels outside [min, max] of the two inputs | **0** |
| alpha falls as coverage rises (any text alpha, any background alpha) | **0** cases: monotone |
| max \|alpha - exact\| | **0.994** (none above 1) |
| max premultiplied error \|o*oa - e*ea\| / 255 | **1.416** (record: within 1.5) |
| max straight-colour error at output alpha >= 255 / 128 / 32 / 8 | 0.99 / 1.43 / 4.17 / 15.04 |
| max straight-colour error at output alpha 1..7 | up to 255 |

The large straight-colour errors occur only where the result is nearly transparent. The source alpha is rounded first
(`(cov*a + 127)/255`): for example, coverage 1 with text alpha 1 gives `sA = 0`, so the pixel keeps the background colour, where
the exact model has the text colour at alpha 0.004. The visible quantity is the premultiplied value, and that stays within
1.42/255. So the output is correct and bounded; see nit r-2 for the cheap exact variant.

**Alpha convention.** The output is straight (not premultiplied) alpha. That is what ScummVM's surface blitters expect:
`graphics/blit/blit-alpha.*` blends with straight alpha, and the only premultiplied blend in the tree is the OpenGL backend's
cursor (`opengl-graphics.cpp:720`). Over an opaque pixel the alpha stays 255 and the colour equals the coverage blend. Formats
with 0 or 1 alpha bit still go through `TextCompose::blendPixel` (opaque text), unchanged and exact as measured in round 2.

## (2) clut8 / `rgb_rendering` matrix and the compose mode

KQ1 headless with the ASan binary. 21 runs, 0 AddressSanitizer lines. On the SDL frames I counted the colours inside the
message box (cyan interior rows located per frame). OpenGL frames are window-scaled to 640x480 and filtered, so their colour
counts mean nothing; for OpenGL only the log line is used.

| map | options | log | box colours / black px |
|---|---|---|---|
| AA-only | clut8 + `rgb_rendering` | `RGB565@2 for render target clut8` + warning | **2 / 3,446**, identical to the plain CLUT8 stamp |
| AA-only | clut8 | `CLUT8 ... clut8` + warning | 2 / 3,446 |
| AA-only | rgb565 + `rgb_rendering` | `RGB565@2 ... rgb565` | 4 (blended) |
| AA-only | rgb565 | `RGB565@2 ... rgb565` | 4 / 2,223 |
| AA-only | rgb555 | `ARGB1555@2 ... rgb555` | 4 / 2,223 |
| AA-only | auto | `RGB565@2 ... rgb565` | (no box in the frame; log only) |
| paired | clut8 + `rgb_rendering` | `RGB565@2 ... clut8`, no warning | **2 / 3,440**: the 1 bpp `[font:clut8]` chain (not the 3,446 AA stamp) |
| paired | clut8 | `CLUT8 ... clut8`, no warning | 2 / 3,440 |
| paired | rgb565 (+/- `rgb_rendering`) | `RGB565@2 ... rgb565` | 4 / 2,223 |
| paired | rgb555 | `ARGB1555@2 ... rgb555` | 4 / 2,223 |
| paired | auto | `RGB565@2 ... rgb565` | 4 / 2,223 |
| both | OpenGL rgb888 / auto | `ABGR8888@4 for render target rgb888` | (log) |
| both | OpenGL clut8 + `rgb_rendering` | `ABGR8888@4 for render target clut8` (+ warning for AA-only) | (log) |

**Compose mode on every path.** I added a scratch unit test, `test_reviewComposeModeEveryPath` (in `src4` only, not committed).
It covers nine `setScreenFormat` paths:
- rgb565, rgb555, rgb888 and auto onto CLUT8: the target becomes clut8, the mode kStamp, one warning;
- rgb888 onto 565: rgb565, kBlend;
- rgb565 onto XRGB1555: rgb555, kBlend;
- rgb565 with blend off onto RGBA8888: rgb888, kThreshold;
- clut8 onto RGB565: stays clut8, kStamp, warned;
- same-format: no change.

For each path it checks the target, `getComposeMode()`, the warning count and `takeStale()`: true exactly when the set changed,
and false on a second call. **1 test OK.**

`GfxScreen` passes `getComposeMode()` to the driver again right after `initScreen()` (`screen.cpp:206-208`), so every path above
reaches the driver.

Observation: no reachable switch changes the driver's behaviour. RGB to RGB keeps kBlend or kThreshold. RGB to CLUT8 is stamped by
the driver anyway (`_pixelSize == 1`). A clut8 text never switches now. So the refresh is defensive, and the record's "the matrix
runs are its evidence" proves nothing about it (nit r-3). It is correct and harmless.

## (3) `takeStale()` and generation: consumers of sets and fonts

`grep` over `engines/sci`:
- `getFontSet` is called only in `GfxCache::hiResFont` (`cache.cpp:79`).
- `GfxFontHiRes` is created only there and owned only by `GfxCache::_cachedFonts`.
- `getComposeMode` is used only by `screen.cpp:157` and `:208`.
- `getTarget` is used only by the driver (`default.cpp:118`, `:134`).

The only holder of a `GfxFont *` outside the cache in SCI16 is `GfxText16::_font` (`text16.cpp:68-80`).
- It refetches only when the font id changes. The purge then happens inside that same `getFont` call, and `_font` is reassigned
  from its result, so it never holds a purged pointer.
- `SciFontMetrics` (`text16.cpp:833`) lives within one `getLongestUtf8` call, which does not refetch.
- GfxControls16, GfxPaint16 and GfxMenu go through `GfxText16` and keep no font pointer. GfxText32 and GfxControls32 are SCI32,
  which has no hi-res text.
- ASan runs: test suite and 21 game runs clean.

Residual (nit r-4): after a generation change `GfxText16::_font` keeps the old font for the same id until the id changes. The old
font draws through its retired set, which stays alive, so this is memory-safe. No reachable path switches after fonts exist, so it
is not observable today.

Memory growth: one retired set per real switch, released with `SciHiResText`. The video path cannot cause switches (round 3).

## (4) Builds and scripts

| build (scratch, from `fcc083e56cc`) | result |
|---|---|
| SCI (`b4n`) | **730 tests OK** |
| SCI + ASan (`b4a`) | **730 OK, 0 sanitizer lines** |
| SCI + SCI32 (`b4s`) | **730 OK** |
| `mkgolden.py --check` from an empty `TMPDIR` | rc 0 |
| the same with one face removed from the cache | rc 1 in 0 s, "exists but lacks faces" (refused before the rebuild) |
| `tools/korean/sync-svf.sh --check` | rc 0, "graphics/svf matches svf-fonts-open 3d40de8d940" |

## New findings

Blocker, major, minor: none.

Nits:
- **r-1:** the warning text. With render_target=clut8 + `rgb_rendering`, the decided path, the warning says "an anti-aliased face
  on a **CLUT8 screen**" while the screen is RGB565 or ABGR8888 (runs above). Say "on the clut8 render target".
- **r-2:** the straight-colour error near alpha 0 (table in (1)) comes from rounding `sA` before using it. Weighting with the
  unrounded product instead (`w = cov*a` out of 65025; `den = w*255 + dA*(65025 - w)`) makes the colour exact up to the final
  rounding, at the same cost. Optional: the premultiplied error is already within 1.42/255.
- **r-3:** the record (9.2) gives the matrix runs as evidence for the compose-mode refresh. No reachable path changes the mode,
  so the runs cannot show it. Call it defensive, or keep a scratch test like mine.
- **r-4:** `GfxText16::_font` may keep a stale (still valid) font for the same id after a generation change, until the id
  changes. Not reachable today.

## Required

None.
