### Status of Findings from Round 1 Re-review

1. **New Breakage #1 (Critical) — Use-after-free in `GfxCache` & permanent RGB downgrade on `initScreen(srcRGBFormat)`**: **Addressed** *(introduces New Breakage #2 below via the expanded `CLUT8 -> RGB` switch)*
   - [default.cpp:126](file:///home/thkim/work/tmp/scummvm-review-aa/engines/sci/graphics/drivers/default.cpp#L126) now guards `setScreenFormat` with `if (!srcRGBFormat && ...)`, preventing Mac QuickTime `kShowMovie` video-mode switches from mutating hi-res text state.
   - [hirestext.cpp:77-78,122-127](file:///home/thkim/work/tmp/scummvm-review-aa/engines/sci/graphics/hirestext.cpp#L77-L78) retires replaced `HiResFontSet *` instances into `_retiredSets` (freed only in `~SciHiResText()`) and bumps `_generation`; [cache.cpp:27-31](file:///home/thkim/work/tmp/scummvm-review-aa/engines/sci/graphics/cache.cpp#L27-L31) purges `_cachedFonts` when `hires->getGeneration() != _hiresGeneration`. Verified by `test_fontsOutliveTheScreenSwitch` ([hirestext.h:193-218](file:///home/thkim/work/tmp/scummvm-review-aa/test/engines/sci/hirestext.h#L193-L218)).
2. **New Breakage #2 (Important) — `SvfFont::plotPixel` forces `alpha = 255` on 8-bit alpha surfaces and ignores `SvfPlot::a`**: **Partially addressed** *(introduces Critical New Breakage #1 below)*
   - [svffont.cpp:57-77](file:///home/thkim/work/tmp/scummvm-review-aa/graphics/fonts/svffont.cpp#L57-L77) splits `format.aBits() > 1` into a Porter-Duff `over` branch honoring `p->a` while keeping `TextCompose::blendPixel` for `aBits() <= 1`, and `test_svfFontAlphaSurfaces` ([svf_aa.h:825-873](file:///home/thkim/work/tmp/scummvm-review-aa/test/graphics/svf_aa.h#L825-L873)) covers `bgA in {0, 255}` and `fgA in {128, 255}`. However, the new integer Porter-Duff denominator can overflow `uint8` on semi-transparent backgrounds (see New Breakage #1).
3. **New Breakage #3 (Important) — `mkgolden.py` partial-cache handling & dirty-content hash + `gen_aa_faces.py --only` validation**: **Addressed**
   - [mkgolden.py:184-185](file:///home/thkim/work/tmp/scummvm-review-aa/tools/korean/mkgolden.py#L184-L185) hashes both `git status --porcelain -- tools ttf` and `git diff HEAD -- tools ttf`; [mkgolden.py:194-199](file:///home/thkim/work/tmp/scummvm-review-aa/tools/korean/mkgolden.py#L194-L199) checks cache completeness after `OSError` and exits with a clear error message if incomplete; [gen_aa_faces.py:41,115-118](file:///home/thkim/work/tmp/scummvm-review-aa/tools/korean/gen_aa_faces.py#L115-L118) validates `--only` against `KNOWN`.
4. **New Breakage #4 (Minor) — Stale comments/docs in `text_compose.h` & `AA-tests.md`**: **Addressed**
   - Fixed in [text_compose.h:73-76](file:///home/thkim/work/tmp/scummvm-review-aa/graphics/hires_text/text_compose.h#L73-L76) and [AA-tests.md:206](file:///home/thkim/work/tmp/scummvm-review-aa/docs/korean/plans/AA-tests.md#L206). All 6 retaken `aa-kq1-*` screenshots were also visually verified and match their captions.

---

### New Breakage Introduced by Round 2

#### Critical

1. **8-bit channel overflow (`256 -> 0`) in `SvfFont::plotPixel` Porter-Duff compositing turns white text black over semi-transparent backgrounds**
   - **Location**: [svffont.cpp:63-72](file:///home/thkim/work/tmp/scummvm-review-aa/graphics/fonts/svffont.cpp#L63-L72)
   - **Scenario**: `oA` is rounded to 8 bits *before* forming `den = oA * 255`, so whenever `dA * (255 - sA) % 255` is in `1..127`, `oA` rounds down and `den` is strictly smaller than the actual numerator weight sum `sA * 255 + dA * (255 - sA)`. Consequently, `o[i]` can exceed `255` and wrap when cast to `(uint8)` at line 72:
     - With 2 bpp `coverage = 170` and opaque white text (`p->a = 255`, `ch = 255`) over a faint translucent white pixel `ARGB(1, 255, 255, 255)` (`dA = 1`, `dc = 255`): `sA = 170`, `dA * (255 - sA) = 85`, `oA = 170 + (85 + 127) / 255 = 170`, `den = 43350`, `o[i] = (255 * 43350 + 255 * 85 + 21675) / 43350 = 256` → **`(uint8)256 == 0` (pure black pixel!)**.
     - Same wraparound occurs for e.g. `coverage = 254, p->a = 255, dA = 127` (`o[i] = 256 -> 0`) and `sA = 2, dA = 64` (`o[i] = 257 -> 1`). `test_svfFontAlphaSurfaces` missed this because it only tests `bgA in {0, 255}` and never `ch = dc = 255`.
   - **Fix**: Use the exact unrounded weight sum as the denominator:
     ```cpp
     const uint den = sA * 255 + (uint)dA * (255 - sA);
     const uint oA = (den + 127) / 255;
     if (!oA)
         return;
     for (int i = 0; i < 3; ++i)
         o[i] = (ch[i] * sA * 255 + dc[i] * dA * (255 - sA) + den / 2) / den;
     ```
     Because the weights sum to `den`, `o[i]` is guaranteed `<= max(ch[i], dc[i]) <= 255`. Add a semi-transparent background case (`dA = 1` and `dA = 127`, `fg = bg = 255`) to `test_svfFontAlphaSurfaces`.

---

#### Important

2. **`render_target=clut8` + `rgb_rendering=true` leaves `UpscaledGfxDriver::_textMode` stuck at `kStamp`, so AA faces are still thresholded instead of blended**
   - **Location**: [screen.cpp:155-161,205](file:///home/thkim/work/tmp/scummvm-review-aa/engines/sci/graphics/screen.cpp#L155-L161), [upscaled.cpp:175-182,210-212](file:///home/thkim/work/tmp/scummvm-review-aa/engines/sci/graphics/drivers/upscaled.cpp#L210-L212), [hirestext.cpp:116-126](file:///home/thkim/work/tmp/scummvm-review-aa/engines/sci/graphics/hirestext.cpp#L116-L126)
   - **Scenario**: In `GfxScreen::GfxScreen()`, [screen.cpp:157](file:///home/thkim/work/tmp/scummvm-review-aa/engines/sci/graphics/screen.cpp#L157) calls `_gfxDrv->setTextPlane(&hires->getPlane(), hires->getComposeMode())` **before** `_gfxDrv->initScreen()` ([screen.cpp:205](file:///home/thkim/work/tmp/scummvm-review-aa/engines/sci/graphics/screen.cpp#L205)). For `render_target=clut8` + `rgb_rendering=true` (the new behavior added in [hirestext.cpp:116-119](file:///home/thkim/work/tmp/scummvm-review-aa/engines/sci/graphics/hirestext.cpp#L116-L119) specifically so `"an AA face is then blended, not thresholded"`):
     1. At line 157, `hires->_target` is still `kHiResTargetClut8`, so `hires->getComposeMode()` returns `TextCompose::kStamp`, which `UpscaledGfxDriver` stores in `_textMode`.
     2. At line 205, `initScreen()` opens an `RGB565` screen and calls `hires->setScreenFormat(format, notes)`, switching `hires->_target` to `kHiResTargetRgb565` and loading `[font:rgb]` AA faces (`hires->getComposeMode()` is now `kBlend`).
     3. `_gfxDrv->setTextPlane` is never called again, so `UpscaledGfxDriver::_textMode` stays `kStamp`, and [upscaled.cpp:211](file:///home/thkim/work/tmp/scummvm-review-aa/engines/sci/graphics/drivers/upscaled.cpp#L211) (`_textMode == kStamp ? kThreshold : _textMode`) **still thresholds the AA faces** on the RGB screen instead of blending them.
   - **Fix**: Move the `setTextPlane` setup in [screen.cpp:155-161](file:///home/thkim/work/tmp/scummvm-review-aa/engines/sci/graphics/screen.cpp#L155-L161) to *after* `_gfxDrv->initScreen()` at line 205 (or re-call `setTextPlane` / update `_textMode` from `hires->getComposeMode()` inside `UpscaledGfxDriver::initScreen()` after `GfxDefaultDriver::initScreen()`).

---

#### Minor

3. **Stale docstring and log-table row after allowing `CLUT8 -> RGB` in `setScreenFormat`**
   - **Location**: [hirestext.h:77-79](file:///home/thkim/work/tmp/scummvm-review-aa/engines/sci/graphics/hirestext.h#L77-L79), [AA-tests.md:233](file:///home/thkim/work/tmp/scummvm-review-aa/docs/korean/plans/AA-tests.md#L233)
   - **Scenario**: [hirestext.h:77-79](file:///home/thkim/work/tmp/scummvm-review-aa/engines/sci/graphics/hirestext.h#L77-L79) still says `When the text wanted RGB and the screen's target is another...`, omitting the new `CLUT8 -> RGB` switch; [AA-tests.md:233](file:///home/thkim/work/tmp/scummvm-review-aa/docs/korean/plans/AA-tests.md#L233) still lists `screen format RGB565@2 for render target clut8` for the `1 bpp map, rgb_rendering=true` run, which now switches `_target` to `rgb565` under `fix-r2.diff`.
   - **Fix**: Update both texts to reflect the `CLUT8 -> RGB` transition.

VERDICT: APPROVE WITH CHANGES
