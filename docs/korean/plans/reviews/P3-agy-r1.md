### Status of Earlier Findings

1. **Critical #1 (`KoreanParser::arrange()` running on non-Korean games with bytes `>= 0x80`) — Addressed**
   - [vocabulary.cpp](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/parser/vocabulary.cpp#L838-L856) now gates `koreanWord` on `korean` (`_koreanVocab.isLoaded()`) and gates `arrange()` on `KoreanParser::shouldArrange(korean, sawKorean)`. Regression test `test_nonKoreanSentenceIsNotArranged` added in [korean_parser.h](file:///home/thkim/work/tmp/scummvm-review-p4/test/engines/sci/korean_parser.h).

2. **Critical #2 (`SegManager::strncpy` non-raw->raw OOB, finite `n` mid-UTF-8 cut, `%s` warning past `n`, `kStrCat`/`kStrCpy` via `strcpy_`) — Partially addressed (introduces a Critical new breakage; see below)**
   - **Addressed**: Non-raw -> raw in UTF-8 mode now copies out via `getString(src)` and routes through `strncpy(dest, text.c_str(), ...)` ([seg_manager.cpp](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/engine/seg_manager.cpp#L764-L783)). Finite-`n` mid-UTF-8 cuts now drop incomplete trailing code points via `utf8CopyPlan()` / `dropIncompleteTail()` ([utf8.cpp](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/utf8.cpp#L173-L206), [seg_manager.cpp](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/engine/seg_manager.cpp#L703-L720)).
   - **Not addressed**:
     - [seg_manager.cpp](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/engine/seg_manager.cpp#L715-L716) still logs raw `src` with `%s` (before `src = planned.c_str()`), which can read past `n` when `src` is not NUL-terminated within `n` bytes (`text.c_str()` from line 705 is already bounded and NUL-terminated).
     - `non-raw -> non-raw` ([seg_manager.cpp](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/engine/seg_manager.cpp#L795-L803)) still has no `dest_r.maxSize` or UTF-8 boundary guard (explicitly deferred in [P4-fixes.md](file:///home/thkim/work/tmp/scummvm-review-p4/docs/korean/plans/P4-fixes.md)).
     - `kStrCpy` / `kStrCat` ([kstring.cpp](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/engine/kstring.cpp#L62), [kstring.cpp](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/engine/kstring.cpp#L109)) still use `strcpy_` (intentionally kept per P3 M3).

3. **Critical #3 (`KoreanParser::arrangeTokens()` unsigned underflow & empty `pop_back()`) — Addressed**
   - [korean_parser.cpp](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/parser/korean_parser.cpp#L190-L191) adds the `role == kKoreanRoleNone || role == kKoreanRoleOf` check to `articleClass`, and `arrangeTokens()` clamps `units`, `popCount`, and `plainUnits` to `plain.size()` and copies popped tokens from `plain` ([korean_parser.cpp](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/parser/korean_parser.cpp#L275-L304)). Covered by `test_articleWithParticleBeforeNounUnit` and `test_articleWithParticleThenNounWithParticle` in [korean_parser.h](file:///home/thkim/work/tmp/scummvm-review-p4/test/engines/sci/korean_parser.h).

4. **Critical #4 (`Width`/`Draw` OOB read past `len` & `drawListControl` byte cap) — Not addressed**
   - Left unchanged after adjudication in [P3-ext-review.md](file:///home/thkim/work/tmp/scummvm-review-p4/docs/korean/plans/P3-ext-review.md). The adjudication is right that `decodeUtf8Char` stops at NUL so NUL-terminated `Common::String` callers will not read past the allocation; `drawListControl` ([controls16.cpp](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/graphics/controls16.cpp#L112)) still passes byte-based `maxChars` as `len` and may draw one extra complete syllable when a multi-byte character straddles `maxChars`.

5. **Important #5 (`GfxFontHiRes::draw()` double `shift=`) — Addressed**
   - Removed the duplicate `+ _set.getShift()` in [hirestext.cpp](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/graphics/hirestext.cpp#L160) and added `test_shiftMovesTheGlyphOnce` in [hirestext.h](file:///home/thkim/work/tmp/scummvm-review-p4/test/engines/sci/hirestext.h).

6. **Important #6 (`SciEngine::keyOf()` missing `key.room`) — Addressed**
   - [state.cpp](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/engine/state.cpp#L373-L374) now sets `if (key.isSet()) key.room = _gamestate->currentRoomNumber();`.

7. **Important #7 (`GfxText16::getLongestUtf8()` returns 0 on leading-space wraps / space-only lines) — Addressed**
   - [text16.cpp](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/graphics/text16.cpp#L866-L867) now sets `count = line.byteNext` when `!line.forced && count == 0 && line.byteNext > 0`.

8. **Important #8 (OS-IME `_lastOsImeKey` clobbered during `getSciEvent()` drain loop) — Addressed**
   - `SciEvent` now carries `uint32 osImeChar` ([event.h](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/event.h#L155)), populated in `getScummVMEvent()` ([event.cpp](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/event.cpp#L347-L350)) and copied to `_lastOsImeKey` only when a non-peek `kSciEventKeyDown` is dequeued in `getSciEvent()` ([event.cpp](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/event.cpp#L509-L513)).

9. **Important #9 (`texteditChangeUtf8` undo on Backspace/arrows, `adopt()` corruption/`_last` collision, `kgraphics.cpp` byte cursor clamp) — Partially addressed**
   - **Addressed**: [controls16.cpp](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/graphics/controls16.cpp#L186) and [controls16.cpp](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/graphics/controls16.cpp#L205-L212) now undo only when `width > widthBefore && width > rect.width() - 2`.
   - **Not addressed**: `KoreanEditLine::adopt()` mutating `text`/`_composing` before checking `writeRun()` on `maxBytes < 3` and `_last` collision ([korean_input.cpp](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/korean_input.cpp#L88-L96)), and `kgraphics.cpp` clamping the code-point cursor against `text.size()` (skipped in [P4-fixes.md](file:///home/thkim/work/tmp/scummvm-review-p4/docs/korean/plans/P4-fixes.md)).

10. **Important #10 (SCI32 `language=ko` + `ko/` crashes in `GfxText32::SwitchToFont1001OnKorean`) — Addressed**
    - [text32.cpp](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/graphics/text32.cpp#L957-L958) returns `false` in UTF-8 mode, and [cache.cpp](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/graphics/cache.cpp#L88) avoids instantiating `GfxFontKorean` when `getTextMode().utf8()`.

11. **Important #11 (`kFormat` calls `lookupText(argv[1], 0)` on TEXT resources & static `warned` map) — Not addressed**
    - Unchanged in [kstring.cpp](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/engine/kstring.cpp#L591-L601) (skipped in [P4-fixes.md](file:///home/thkim/work/tmp/scummvm-review-p4/docs/korean/plans/P4-fixes.md)).

12. **Minor #12 (`sci-korean` keymap swallows keys when `_hangulBuiltin` is false) — Partially addressed**
    - [metaengine.cpp](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/metaengine.cpp#L705-L708) now skips registering `sci-korean` for `hangul_builtin=false` or `korean_text=false`, but still registers it for `hangul_input=false` or upstream CP949 `language=ko` targets without a `ko/` pack, and [event.cpp](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/event.cpp#L322-L327) still returns `noEvent` for `kSciActionHangulToggle` when `!_hangulBuiltin`.

13. **Minor #13 (`GfxScreen::putFontPixel()` doesn't clear `_textPlane`) — Not addressed** (skipped in [P4-fixes.md](file:///home/thkim/work/tmp/scummvm-review-p4/docs/korean/plans/P4-fixes.md)).

14. **Minor #14 (`kDrawStatus` bypasses `ScriptStrings`) — Addressed**
    - [kmenu.cpp](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/engine/kmenu.cpp#L82) now routes through `g_sci->strSplitHeap(...)`.

15. **Minor #15 (`strAtUtf8` read past `maxSize` returns 0 instead of `r_acc` + warning) — Not addressed** ([kstring.cpp](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/engine/kstring.cpp#L190-L191) unchanged; skipped in [P4-fixes.md](file:///home/thkim/work/tmp/scummvm-review-p4/docs/korean/plans/P4-fixes.md)).

16. **Minor #16 (`strcheck.py` duplicates with leading zeros) — Not addressed** (skipped in [P4-fixes.md](file:///home/thkim/work/tmp/scummvm-review-p4/docs/korean/plans/P4-fixes.md)).

17. **Minor #17 (empty folder code when `text_encoding=utf8` on `UNK_LANG`) — Not addressed** (skipped in [P4-fixes.md](file:///home/thkim/work/tmp/scummvm-review-p4/docs/korean/plans/P4-fixes.md)).

---

### New Breakage Introduced by `fix-r1.diff`

#### Critical

- **[seg_manager.cpp:718, 725-734](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/engine/seg_manager.cpp#L718-L734) & [utf8.cpp:201-202](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/utf8.cpp#L201-L202) — Every UTF-8 `strcpy_` into a non-raw buffer (`SEG_TYPE_STACK` / `SEG_TYPE_LOCALS`) overwrites 1 byte past the NUL terminator and zeroes the adjacent `reg_t`'s segment.**
  - **Failure scenario**: Upstream `SegManager::strcpy_(dest, src)` passes `n = 0xFFFFFFFFU` so that on the `!dest_r.isRaw` path, line 732 (`if ((size_t)dest_r.maxSize > n) setChar(dest_r, n, 0);`) is **false** (since `dest_r.maxSize` is `int32`), and only bounded `strncpy(dest, src, n)` writes an extra NUL at offset `n`. After `fix-r1.diff`, `utf8CopyPlan()` sets `plan.limit = plan.text.size() + 1` for unbounded copies, and line 718 overwrites `n = use.limit`. Now, for a 3-byte string `"abc"` copied into a 4-byte stack/local array `buf[4]` immediately followed by another local variable or `reg_t` object pointer `obj` (with `dest_r.maxSize` covering the rest of the stack/locals segment, e.g. 100 bytes):
    1. `use.limit` is `4`, so `n` becomes `4`.
    2. Lines 726–730 copy offsets `0='a'`, `1='b'`, `2='c'`, `3='\0'` and `break`.
    3. Line 732 evaluates `100 > 4` (**true**) and executes `setChar(dest_r, 4, 0)`.
    4. `setChar()` ([seg_manager.cpp](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/engine/seg_manager.cpp#L658-L674)) writes `0` into byte 0 of `obj` **and unconditionally sets `val->setSegment(0)`**, corrupting the adjacent stack/local variable or invalidating an object/string `reg_t` pointer.
  - **Fix**: Only write the trailing NUL at offset `n` when the copy loop did not already write a NUL terminator:
    ```cpp
    uint i = 0;
    for (; i < n; i++) {
        setChar(dest_r, i, src[i]);
        if (!src[i])
            break;
    }
    if (i == n && (size_t)dest_r.maxSize > n)
        setChar(dest_r, n, 0);
    ```
    (And on line 716, log `text.c_str()` instead of raw `src` so the warning cannot read past `n`.)

VERDICT: REJECT
