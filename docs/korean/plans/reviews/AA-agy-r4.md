### Status of Findings from Round 2 Re-review

1. **New Breakage #1 (Critical) — 8-bit channel overflow (`256 -> 0`) in `SvfFont::plotPixel` Porter-Duff compositing**: **Addressed**
   - [svffont.cpp:64-82](file:///home/thkim/work/tmp/scummvm-review-aa/graphics/fonts/svffont.cpp#L64-L82) extracts `SvfFont::compositeOver()` and divides by the exact unrounded weight sum `den = sA * 255 + (uint)dA * (255 - sA)` before computing `oA = (den + 127) / 255`. Every channel is now a true convex combination of foreground and background (`min <= o[i] <= max <= 255`), `oA == 0` returns `bg` before dividing (`den > 0` implies `den >= 255` and `oA >= 1`), and intermediate sums (`<= 16,613,887`) cannot overflow 32-bit `uint`.
   - Verified by `test_svfFontOverExhaustive` ([svf_aa.h:883-926](file:///home/thkim/work/tmp/scummvm-review-aa/test/graphics/svf_aa.h#L883-L926)), which checks >300,000 combinations against an independent floating-point ideal Porter-Duff model and explicitly asserts the `(coverage=170, ARGB(1,255,255,255))` regression case.

2. **New Breakage #2 (Important) — `UpscaledGfxDriver::_textMode` desync after `initScreen()` (`render_target=clut8` + `rgb_rendering=true`)**: **Addressed**
   - [hirestext.cpp:116-119](file:///home/thkim/work/tmp/scummvm-review-aa/engines/sci/graphics/hirestext.cpp#L116-L119) keeps `kHiResTargetClut8` sticky when the user explicitly selects `render_target=clut8` on an RGB screen (text follows the screen only downward / across RGB targets), preserving `[font:clut8]` 1 bpp chains and the AA-on-CLUT8 warning.
   - [screen.cpp:205-208](file:///home/thkim/work/tmp/scummvm-review-aa/engines/sci/graphics/screen.cpp#L205-L208) re-hands `g_sci->getHiResText()->getComposeMode()` to `_gfxDrv->setTextPlane(...)` after `_gfxDrv->initScreen()` so `_textMode` is always synchronized with any target change made during `initScreen()`.
   - Also cleanly extracts `SciHiResText::takeStale(uint &seen)` ([hirestext.h:83-88](file:///home/thkim/work/tmp/scummvm-review-aa/engines/sci/graphics/hirestext.h#L83-L88), [cache.cpp:86-87](file:///home/thkim/work/tmp/scummvm-review-aa/engines/sci/graphics/cache.cpp#L86-L87)), covered by `test_fontsOutliveTheScreenSwitch` and `test_textFollowsTheScreenItGot` ([hirestext.h:199-291](file:///home/thkim/work/tmp/scummvm-review-aa/test/engines/sci/hirestext.h#L199-L291)).

3. **New Breakage #3 (Minor) — Stale docstring in `hirestext.h` and inconsistent `AA-tests.md` row**: **Addressed**
   - [hirestext.h:76-82](file:///home/thkim/work/tmp/scummvm-review-aa/engines/sci/graphics/hirestext.h#L76-L82) accurately documents downward-only switching and refreshing `getComposeMode()` after `initScreen()`; [AA-tests.md:233,239-240,334,346-372](file:///home/thkim/work/tmp/scummvm-review-aa/docs/korean/plans/AA-tests.md#L233) is consistent across all sections and documents the 5-run headless verification matrix.
   - Bonus hardening in [mkgolden.py:188-189](file:///home/thkim/work/tmp/scummvm-review-aa/tools/korean/mkgolden.py#L188-L189) rejects an incomplete `cache` directory *before* running the ~75 s rebuild.

---

### New Breakage Introduced by Round 3

None.

VERDICT: APPROVE
