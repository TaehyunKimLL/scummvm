# AA fix round re-review (Claude lane)

Scope: `618d93da62d..5ff4af9ce79` (code commits `102b6d51512`, `8cde12f534a`, `b7abdf9ba41`, `bc0d02723f7`, `f21d5e16ef7`) against
my review `AA-review.md` (= `reviews/AA-opus.md`) and the coordinator's re-check list. Every claim below was **run**, not read,
unless marked. Scratch only: `git archive 5ff4af9ce79` in `~/work/tmp/aa-review-claude/src2`, build `build2` (SCI, debug), my
own game-run copy `~/work/tmp/aa-review-claude/games` (KQ1 data symlinked, maps copied), harnesses in `~/work/tmp/aa-review-claude/h`.
The worktree was not touched except for this file.

## Verdict

**Approved: sound to build on (SCUMM S2 / DOS). No blocker, no major.** Every one of my findings is addressed and verified by
running. New: two latent minors in code paths no current target reaches (a font-set use-after-free on the Mac QuickTime video path;
opaque fringes from `SvfFont` on transparent 32-bit surfaces), one stale DESIGN line, nits. Recommended now: N-1 (a one-line guard).

## My earlier findings

| finding | status | evidence |
|---|---|---|
| M-1 LSL1 screenshots without the text | **addressed** | all 20 `aa-*.png` viewed: no duplicate files (md5), every caption has a file and every file a caption; the seven LSL1 frames show the warning box / age dialog; text-box crops have 4 colours (AA) for rgb565/rgb555 and 2 for the CLUT8 pair and NEODG16A |
| m-1 / R2 backend format request untested | **addressed** | `hiResPickScreenFormat` (pure); my mutation "filter inverted" fails `test_pickScreenFormat`; my 13-list harness and real game runs below |
| m-2 / R3 quantisation misdescribed | **addressed and exceeded** | `blendPixel` now IS the C targets' arithmetic: 45,056 cases, 0 differences against an independent model (below) |
| m-3 RGB target on a CLUT8 screen | **addressed** (`setScreenFormat`), but see N-1 for the second caller | `aaonly` map runs below; unit `test_textFollowsTheScreenItGot` |
| m-4 `auto` 565 over 888 | **addressed** (888 first) | OpenGL run: `auto` -> `ABGR8888@4 for render target rgb888` |
| m-5 cache key | **addressed** | `mkgolden.py --check` from an empty `TMPDIR` cache: rc 0 (new Bpp8 target goldens included) |
| nits (name table, debug line) | **addressed** | `targetName()`; log prints `rgb565` etc. |

## Re-checks requested

### (1) Format pick: real runs and the pure function

Built at HEAD: `make test` **727 OK**. KQ1 headless (Xvfb, `pair` map = `[font:rgb] p16,ja,neo` / `[font:clut8] j1,neo`),
`-d1 --debugflags=Korean`, log lines:

| backend | render_target | log |
|---|---|---|
| surfacesdl | rgb555 | `screen format ARGB1555@2 for render target rgb555` |
| surfacesdl | rgb565 | `RGB565@2 for render target rgb565` |
| surfacesdl | rgb888 | `render_target rgb888 is not offered by this backend; using rgb565` then `RGB565@2 ... rgb565` |
| surfacesdl | clut8 | `CLUT8 for render target clut8` |
| surfacesdl | (unset: auto) | `RGB565@2 ... rgb565` (SDL caps at the 16-bit hardware screen) |
| **opengl** | rgb555 | **`RGBA5551@2 for render target rgb555`** (alpha in bit 0; the frame renders opaque and correct, viewed) |
| opengl | rgb565 | `RGB565@2 ... rgb565` |
| **opengl** | rgb888 | **`ABGR8888@4 for render target rgb888`**: a real 32-bit game screen, which the record calls UNVERIFIED |
| opengl | clut8 | `CLUT8 ... clut8` |
| opengl | (auto) | `ABGR8888@4 ... rgb888` |

No "the backend gave" note appears: each run got the target it asked for.

The pure function and the resolver were driven together through `engines/engine.cpp`'s `findCompatibleFormat` rule, copied
verbatim (`h/pick.cpp`, linked against the real `hires_options.cpp`/`font_map.cpp`). For each of 13 backend lists and each of
`auto, clut8, rgb565, rgb555, rgb888`, the check was: the resolved RGB target gets a screen of exactly that target, the screen is
in the backend list or is CLUT8, and a target that is not offered falls back 888 > 565 > 555 > CLUT8. The lists were SDL16, SDL32,
OpenGL, 888 + CLUT8, 555 + CLUT8, 565 + 555 (no CLUT8), XRGB1555 before 565, empty, CLUT8 only, an odd order
(4444, 5551, BGR565, ARGB8888), 3-byte 888 + 4444, CLUT8 first, and BGR8888 + BGR565. **0 mismatches.** Points worth knowing,
all as designed:
- `rgb555` asked with only 888 and 565 offered gives 888, not 565.
- A 3-byte 888 screen is never chosen (CLUT8).
- Swapped channel orders (BGR565, ABGR8888) are taken as their target.

### (2) `TextCompose::blendPixel`

- **Exactness.** `h/main.cpp dump` calls the real `blendPixel` for 11 layouts: RGB565, BGR565, RGB555, BGR555, XRGB1555,
  RGBA5551, ARGB4444, RGBA4444, ARGB8888, RGBA8888 and XRGB8888. Each runs all 256 coverages x 8 fg/bg pairs (including
  near-black/near-white, low contrast and odd values), with the background pixel's alpha 0 and 255. `h/model.py` models the C
  targets independently, from `svf_draw.c` `blend565`/`blend555`/`blend888`. For RGB565, RGB555 and XRGB8888 it is the real C
  target formula with `fg565` built as mkgolden does. For the other layouts it is the same channel arithmetic at the format's
  shifts with the alpha field set to all ones. Result: **45,056 cases, 0 differences**.
- **Alpha.** `RGBToColor` writes `0xFF >> aLoss` into the alpha field, so every format gets an opaque pixel: bit 15 of
  XRGB1555, bit 0 of RGBA5551, nibble F for 4444, 0xFF for 8888. The background's alpha is overwritten, as the model expects.
  `ARGBToColor(a, ...)` would cut `a` to the field (`a >> aLoss`), which is the old 1555 bug (alpha below 128 became 0); it is
  no longer used.
- **Speed.** Measured with `-O2` (x86-64 here; `h-old`/`h-new` = `composeSpan` of `618d93da62d` against HEAD, 640-px spans,
  57 % covered, 20,000 reps):

| format | old ns/px (TSC) | new ns/px (TSC) |
|---|---|---|
| RGB565 | 5.82 (17.4) | 7.35 (22.0) |
| RGB555 | 6.16 (18.4) | 5.41 (16.2) |
| XRGB1555 | 5.81 (17.4) | 6.28 (18.8) |
| XRGB8888 | 4.64 (13.9) | 5.49 (16.4) |

  Neither version has a hardware divide: `/255` becomes a multiply. The i486 code of one channel
  (`gcc -m32 -march=i486 -O2`) is 2 `imul` + 1 `mul` + shift, and the old code had the same three divide-by-255 per pixel. So
  the change costs nothing on DOS-class CPUs: about 9 multiplies per covered pixel, roughly 120-380 cycles on a 486 (from
  instruction timings, UNVERIFIED on hardware). A table is not needed for correctness. The cheap exact win, if P7 measures it
  slow, is a `cov == 255` fast path: write `RGBToColor(fg)`, which equals the blend exactly, and 255 is the most common covered
  level of a 2 bpp face. After that, a per-span table of (channel value x 3 levels).

### (3) `SciHiResText::setScreenFormat`

At start-up it is correct (runs and unit test): the map is reloaded with the new target's sections, the font sets are dropped
before any font exists, the break rules are rebuilt with the user's `hangul_break`, and the one-time CLUT8 warning fires on the
`[font:clut8]`-less map (`aaonly` runs below). Screenshots and thumbnails need nothing new: `createScreenShot` and
`createThumbnailFromScreen` convert any 2- or 4-byte format (RGBA5551 and ABGR8888 frames from the OpenGL runs were viewed and
are correct). Savegames store no screen mode. The problem is the second caller (N-1).

### (4) `init.cpp`

The mutation that puts back the old override fails **0 tests** (as the record says). Runs:

| map | render_target | rgb_rendering | screen | note |
|---|---|---|---|---|
| neo1 (1 bpp only) | unset | true | `RGB565@2 ... clut8` | the user's RGB is kept (was CLUT8 before the fix) |
| neo1 | unset | unset | `CLUT8 ... clut8` | unchanged |
| pair | clut8 | true | `RGB565@2 ... clut8` | **behaviour change**: explicit `render_target=clut8` + `rgb_rendering` used to give a CLUT8 screen, now RGB |
| aaonly (AA, no `[font:clut8]`) | clut8 | true | `RGB565@2 ... clut8` | warning "an anti-aliased face on a **CLUT8 screen** is stamped" on an RGB screen (N-3) |
| aaonly | clut8 | unset | `CLUT8 ... clut8` + warning | correct |

The only path that now behaves differently is the user asking for RGB (`rgb_rendering` or `palette_mods`) while the text
resolves to CLUT8: the screen is now RGB and the text is thresholded (`kStamp` becomes `kThreshold` in the driver). For 1 bpp
faces the result is identical. That is the intended "never take away the user's RGB".

### (5) `auto` 888 > 565 > 555 and DESIGN 10

The composite arithmetic statement in DESIGN 10 (equal to the C targets byte for byte over one background colour) is true
(item 2). But line 539, "RGB runs compared with a tolerance of 1 LSB", is now stale twice over:
- With `auto` = 888 on a 32-bit Linux desktop, a DOS 565/555 run differs by the screen quantisation of the whole picture, far
  more than 1 LSB.
- When the Linux run pins the DOS format, the same `blendPixel` and `RGBToColor` code makes the screens identical, so no
  tolerance is needed.

See N-4.

### (6) Mutation table rows re-run (scratch copy, rebuilt, reverted)

| row | record | my run |
|---|---|---|
| `hiResPickScreenFormat` filter inverted | 1: `test_pickScreenFormat` | **1: `test_pickScreenFormat`** |
| `blendPixel` without `+ 127` | 10 (list) | **10, the same list**: `test_composeRgb565`, `test_convertXrgb8888`, AA `test_eightBitRampAtFiveBits`, `test_every_case_at_15_and_16_bit`, `test_presentThroughScreen`, `test_rgbSinks`, `test_svfFontSurfaces`, SvfFont `test_argb32`, `test_rgb332`, `test_rgb565` |
| `setScreenFormat` ignores the screen | 1: `test_textFollowsTheScreenItGot` | **1: the same** |
| `init.cpp` override restored | 0 | **0** (727 OK) |

Four of four rows match.

### (7) Screenshots

All 20 `aa-*.png` match their captions. The LSL1 frames:
- rgb565 and rgb555 warning box: 4 colours in the box, AA.
- CLUT8 1 bpp pair: 2 colours; 에눈 / 있눈 / 있숩니다 visible as captioned.
- Latin chain: the box is narrower, re-wrapped by the Latin advances.
- NEODG16A: 2 colours.
- Age dialog at 565 and 555: 39,208 px differ between the two, the 15- vs 16-bit picture.

KQ1 CLUT8 pair: 성윤 is visible as captioned. Nit N-5.

### The two items left undone

- **`UpscaledGfxDriver` double quantisation: acceptable, not a defect.** The C targets also blend over a background that is
  already in the target format (`blend(t->fg565, t->bg565, v)`). Blending over the cut screen pixel is exactly the
  arithmetic DESIGN 10 now claims. Blending against the exact palette colour would make the ScummVM composite differ from the
  C targets, and it costs a second conversion pass.
- **`init.cpp` without a unit test: acceptable.** The behaviour is a one-line condition, and I covered it by mutation (0 tests)
  and by the four runs above. Optional: a pure `bool hiResRequestRgb(bool userRgb, bool textWantsRgb)` beside
  `hiResPickScreenFormat` makes it testable.

## New findings

### Blocker, major

None.

### Minor

**N-1 Latent use-after-free: `setScreenFormat` also runs on the Mac QuickTime path and drops font sets that cached fonts
still reference.**
- **Where:** `engines/sci/graphics/drivers/default.cpp:126-133` (after the if/else, so for every caller),
  `engines/sci/graphics/hirestext.cpp:111-127`.
- **How it happens:** `kShowMovie` (Mac) calls `initScreen(&videoFormat)` mid-game (`engines/sci/engine/kvideo.cpp:147`). If
  the video's format maps to another RGB target (OpenGL: RGBA8888 against an `rgb565` text), `setScreenFormat` deletes every
  `HiResFontSet`.
- **Why that is a use-after-free:** `GfxFontHiRes` keeps `const HiResFontSet &_set` (`hirestext.h:136`), and the
  `GfxFontHiRes` objects stay cached in `GfxCache::_cachedFonts`. The next text draw reads freed memory.
- **Lasting effect:** the text target stays switched to the video's format, and the `initScreen()` after the video then asks
  for that format.
- **Reach:** only a Mac SCI release with QuickTime and a Korean map, which is outside this round's scope. UNVERIFIED by a run:
  no such game here. The chain is in the code.
- **Fix (one line):** call `setScreenFormat` only when `!srcRGBFormat`. Or make `setScreenFormat` refuse to drop sets once any
  exist: switch only at the first `initScreen`.

**N-2 Latent: `SvfFont` on a transparent 32-bit surface now writes opaque dark fringes.**
- **Where:** `graphics/fonts/svffont.cpp:55-58`, `graphics/hires_text/text_compose.cpp:60-70`.
- **What happens:** measured with the real `blendPixel`, white text at coverage 85 over a transparent black ARGB8888 pixel
  gives `0xff555555`, an opaque grey. Before, it gave alpha 85 white; ScummVM's TTF over-operator (`graphics/fonts/ttf.cpp:640-657`)
  gives the same alpha 85 white.
- **Cause:** the 1555 fix (opaque for 1-bit alpha) is applied to every alpha width. `SvfFont` has no production user yet; the
  GUI and AGI are planned to use it.
- **Fix:** keep the channel-width blend over opaque pixels. Over a pixel whose alpha is not full, either use the TTF
  over-operator (formats with `aBits() > 1`) or write the text colour when coverage >= 128 (1-bit alpha).

**N-3 A misleading warning and a threshold where blending was possible.** With `render_target=clut8`, `rgb_rendering=true`
and a map whose only chain is AA, the screen is RGB565, yet the log says "an anti-aliased face on a **CLUT8 screen**", and the
AA faces are thresholded at 128 on an RGB screen (run above). Fix: word the warning "on a clut8 render target", or let
`setScreenFormat` move a CLUT8 text onto an RGB screen when the map has no `[font:clut8]`.

**N-4 DESIGN 10 line 539 is stale.** It says "RGB runs compared with a tolerance of 1 LSB". Replace with: "RGB runs: Linux with
`render_target` pinned to the DOS screen's format, identical (same `blendPixel` and `RGBToColor` arithmetic)". With `auto` the
Linux desktop now picks 888.

### Nit

- **N-5:** six KQ1 frames (`18px`, `3by2`, `jamo-first`, `latin-chain`, `neodgm`, `clut8-aa-face-stamped`; file time 18:51)
  predate `8cde12f534a`, so their edge pixels come from the old arithmetic. The captions make no pixel claim. Retake them if
  they are ever used as pixel references.
- `hiResPickScreenFormat` gives 888 for `rgb555` when 565 is also offered. The fallback order is documented; 565 would be the
  nearer substitute for a 15-bit request. Owner's call.

## Required

None blocking. Recommended now: **N-1** (`if (!srcRGBFormat)` around the `setScreenFormat` call), **N-4** (one DESIGN line).
Before `SvfFont` gets a GUI or AGI user: **N-2**.
