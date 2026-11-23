Read-only review of phase P3 (`d7f47c73a07..5741781806a`) against `docs/korean/DESIGN.md`, `docs/korean/plans/P3-sci-kq1.md`, `P3-notes.md`, `P3-fixes.md`, and `P3-review.md`. Findings below exclude issues already raised in `P3-review.md`.

### Critical

1. **Non-Korean localized games run `KoreanParser::arrange()` on any sentence containing a byte `>= 0x80` (violates Requirement 1)**
   - **Location:** [vocabulary.cpp:811-830](file:///home/thkim/work/tmp/scummvm-review-p3/engines/sci/parser/vocabulary.cpp#L811-L830)
   - **Failure scenario:** `sawKorean` is set whenever any byte in `currentWord` is `>= 0x80`, without checking `korean` (`_koreanVocab.isLoaded()`). In any German, French, Spanish, Polish, Russian, Hebrew, or upstream CP949-Korean SCI0/01 game (where `_koreanVocab` is not loaded and `TextMode` is not `kUtf8`), typing any word with an accented/Cyrillic/Hebrew/CP949 character (e.g., German `schau baum an`, `öffne tür`, Russian `открой дверь`) sets `sawKorean = true` and invokes `KoreanParser::arrange()`, which reorders trailing verbs/prepositions and runs `askAbout()` on the non-Korean token list, breaking `said()` matching.
   - **Fix:** Gate `koreanWord`/`sawKorean` and the `arrange()` call on `korean` (i.e. `_koreanVocab.isLoaded()`): `if (korean) { ... }` and `if (korean && sawKorean && !retval.empty())`.

2. **Heap buffer overflow on `kStrCpy` from non-raw buffers (`SEG_TYPE_STACK` / `SEG_TYPE_LOCALS`) and mid-code-point truncation on bounded `kStrCpy`**
   - **Location:** [seg_manager.cpp:702-710](file:///home/thkim/work/tmp/scummvm-review-p3/engines/sci/engine/seg_manager.cpp#L702-L710), [seg_manager.cpp:753-773](file:///home/thkim/work/tmp/scummvm-review-p3/engines/sci/engine/seg_manager.cpp#L753-L773), [kstring.cpp:60](file:///home/thkim/work/tmp/scummvm-review-p3/engines/sci/engine/kstring.cpp#L60), [kstring.cpp:99-108](file:///home/thkim/work/tmp/scummvm-review-p3/engines/sci/engine/kstring.cpp#L99-L108)
   - **Failure scenario:**
     - `SegManager::strncpy(reg_t dest, reg_t src, size_t n)` only enters the UTF-8 bounds-checked `strncpy(reg_t, const char*, size_t)` overload when `src_r.isRaw` is true. When `src` is a non-raw stack/locals array (where `kFormat` writes longer Korean strings) and `dest` is a smaller `SEG_TYPE_DYNMEM` or `SEG_TYPE_SCRIPT` buffer, lines 756–772 copy with `n == 0xFFFFFFFFU` and **no `dest_r.maxSize` check**, overflowing the host `dest_r.raw` allocation.
     - In `strncpy(reg_t, const char*, size_t)`, when `kStrCpy(dest, src, len)` passes a positive `len < dest_r.maxSize`, the UTF-8 truncation branch is skipped (`strnlen < dest_r.maxSize`), slicing multi-byte Hangul mid-sequence and omitting NUL termination on raw destinations. On line 707, `warning("... \"%s\" ...", src, ...)` also reads `src` past `n` if `src` is not NUL-terminated.
     - `kStrCat` (`kstring.cpp:60`) and `kStrCpy` (`kstring.cpp:107`) call `strcpy_` instead of `strcpyBounded`, so writing a longer Korean string into a `SEG_TYPE_SCRIPT` string overwrites adjacent strings/objects in the script segment.
   - **Fix:** In UTF-8 mode, handle non-raw `src` in `SegManager::strncpy(reg_t, reg_t, size_t)` through `getString(src)` -> `strncpy(dest, str.c_str(), n)`; in `strncpy(reg_t, const char*, size_t)`, truncate on a code-point boundary whenever `n` cuts a UTF-8 sequence (using `MIN(n, (size_t)dest_r.maxSize)`), print `cut.c_str()` instead of raw `src` in the warning, and use `strcpyBounded` in `kStrCat`/`kStrCpy`.

3. **Unsigned underflow OOB read and `pop_back()` on empty `Common::Array` in `KoreanParser::arrangeTokens()`**
   - **Location:** [korean_parser.cpp:183-192](file:///home/thkim/work/tmp/scummvm-review-p3/engines/sci/parser/korean_parser.cpp#L183-L192), [korean_parser.cpp:270-298](file:///home/thkim/work/tmp/scummvm-review-p3/engines/sci/parser/korean_parser.cpp#L270-L298)
   - **Failure scenario:** `unitStart` includes a preceding `articleClass` token (`line 189`) without checking its `role`. If an article carries a particle role (e.g. `그에게` -> `kKoreanRoleTo`), `arrangeTokens()` moves `tok[0]` to `phrases` instead of `plain` (`plain.size() == 0`), yet `unitStart[1]` is still `0`:
     - On `"그에게 문에게 줘"` (article+role, noun+role, verb), line 291 executes `for (uint u = 0; u < 1; ++u) plain.pop_back();` on empty `plain` (assertion crash / `_size` underflow to `UINT_MAX`).
     - On `"그에게 문 안을 봐"` (article+role, noun, postposition, verb), `k = 1` sets `plainUnits = 1 + 1 - 0 = 2` while `plain.size() == 1`; at `k = 2` (`안`), line 273 computes `plain.size() - plainUnits` (`1u - 2u = 0xFFFFFFFF`), reading `plain[0xFFFFFFFF]` out of bounds and popping an empty array.
   - **Fix:** Require `role[start - 1] == Common::kKoreanRoleNone || role[start - 1] == Common::kKoreanRoleOf` for `articleClass` as well in `unitStart`, and in `arrangeTokens()` track `plainUnits` strictly from tokens actually pushed to `plain` (clamping pops to `MIN<uint>(..., plain.size())` and copying from `plain`, not `tok[unitStart[k]..k-1]`, so moved tokens cannot be duplicated or underflow `plain`).

4. **Out-of-bounds read past `len` in `GfxText16::Width` / `GfxText16::Draw`, triggered by `GfxControls16::drawListControl`**
   - **Location:** [text16.cpp:398-405](file:///home/thkim/work/tmp/scummvm-review-p3/engines/sci/graphics/text16.cpp#L398-L405), [text16.cpp:526-533](file:///home/thkim/work/tmp/scummvm-review-p3/engines/sci/graphics/text16.cpp#L526-L533), [controls16.cpp:112](file:///home/thkim/work/tmp/scummvm-review-p3/engines/sci/graphics/controls16.cpp#L112)
   - **Failure scenario:** `decodeUtf8Char(p, bytes)` has no length bound and reads up to 3 bytes ahead assuming NUL termination. `drawListControl()` calls `_text16->Draw(textString.c_str(), 0, MIN<int16>(maxChars, listEntry.size()), ...)` where `maxChars` is applied as a **byte** count. For a 10-syllable (30-byte) Korean save/list entry with `maxChars = 20`, at byte 18 (`len == 2`) `decodeUtf8Char` reads past `text + 20` (bytes 18–20), draws a 7th syllable exceeding the cap, and drives `len` negative. On any `from, len` slice not NUL-terminated at `from + len`, `decodeUtf8Char` reads 1–3 bytes out of bounds.
   - **Fix:** Add a length-bounded `decodeUtf8Char(const byte *p, int remaining, int &outBytes)` and pass `len + 1` as `remaining` in `Width`/`Draw`/`DrawStatus`. In `drawListControl()`, slice UTF-8 entries on a code-point boundary (`utf8OffsetOf` or box width) instead of raw byte count `maxChars`.

---

### Important

5. **`GfxFontHiRes::draw()` applies `shift=` twice (untested in `SciHiResTextTestSuite`)**
   - **Location:** [hirestext.cpp:159-161](file:///home/thkim/work/tmp/scummvm-review-p3/engines/sci/graphics/hirestext.cpp#L159-L161) vs [font_set.cpp:211](file:///home/thkim/work/tmp/scummvm-review-p3/graphics/hires_text/font_set.cpp#L211); [hirestext.h:51](file:///home/thkim/work/tmp/scummvm-review-p3/test/engines/sci/hirestext.h#L51)
   - **Failure scenario:** `GfxFontHiRes::draw()` computes `baseline = ... + _set.getShift()` and passes `baseline` to `_set.drawChar(...)`, which itself adds `+ _plan.shift` (`font_set.cpp:211`). Any map using `shift=N` shifts glyphs vertically by `2 * N` hi-res pixels. `test_picksTheGameMap` only checks `getShift() == 2` without drawing font 300, and `test_fontDrawsHangulIntoThePlane` draws only font 0 (`shift == 0`), so the suite misses it.
   - **Fix:** Remove `+ _set.getShift()` from `GfxFontHiRes::draw()` (`hirestext.cpp:160`) and add a drawing test with non-zero `shift=` comparing actual plotted Y coordinates.

6. **`SciEngine::keyOf()` never sets `key.room`, breaking room-scoped `sci-ko.str` entries for direct script strings**
   - **Location:** [state.cpp:366-373](file:///home/thkim/work/tmp/scummvm-review-p3/engines/sci/engine/state.cpp#L366-L373)
   - **Failure scenario:** `SegManager::stringKey(ptr)` returns `Key(script, id)` with `room = Key::kAnyRoom` (`0xFFFF`). While `kStrCpy` and `kStrCat` set `key.room = s->currentRoomNumber()`, `SciEngine::keyOf()` does not. Any script string passed directly (without `kStrCpy`) to `kDisplay`, `kTextSize`, `_k_GenericDrawControl`, `kNewWindow`, `kAddMenu`, `GfxMenu::kernelSetAttribute`, or `kFormat` is looked up with `room == kAnyRoom`, silently ignoring room-specific entries (`script<TAB>id<TAB>room<TAB>text`). `SciScriptStringsTestSuite` constructs `Key(0, 5, 14)` manually and never tests `keyOf()`.
   - **Fix:** In `SciEngine::keyOf()`, if `key.isSet()` after `stringKey(ptr)`, set `key.room = _gamestate->currentRoomNumber();`.

7. **`GfxText16::getLongestUtf8()` returns `0` on leading-space wraps or space-only lines, aborting `Size()` and `Box()`**
   - **Location:** [text16.cpp:842-865](file:///home/thkim/work/tmp/scummvm-review-p3/engines/sci/graphics/text16.cpp#L842-L865), [text16.cpp:498-500](file:///home/thkim/work/tmp/scummvm-review-p3/engines/sci/graphics/text16.cpp#L498-L500), [text16.cpp:640-642](file:///home/thkim/work/tmp/scummvm-review-p3/engines/sci/graphics/text16.cpp#L640-L642)
   - **Failure scenario:** `TextLayout::fitLine()` trims trailing spaces from `line.byteEnd`. When a line is spaces-only (`"   "`, no newline) or starts with spaces followed by a word wider than `maxWidth` (`"   긴단어..."`), `fitLine` sets `line.forced = false`, `line.byteEnd = 0`, `line.byteNext > 0`. `getLongestUtf8()` advances `textPtr` and returns `0`; both `Size()` and `Box()` hit `if (charCount == 0) break;` and silently drop the entire remainder of the text.
   - **Fix:** When `!line.forced && line.byteEnd == 0 && line.byteNext > 0`, return `line.byteNext` (or re-fit from the new `textPtr` for leading spaces) so `getLongestUtf8()` never returns `0` while advancing `textPtr`.

8. **OS-IME syllables are lost or duplicated when `EventManager::getSciEvent()` drains multiple events**
   - **Location:** [event.cpp:343-350](file:///home/thkim/work/tmp/scummvm-review-p3/engines/sci/event.cpp#L343-L350), [event.cpp:491-495](file:///home/thkim/work/tmp/scummvm-review-p3/engines/sci/event.cpp#L491-L495)
   - **Failure scenario:** `getSciEvent()` drains all pending ScummVM events into `_events` before returning the first one, while `getScummVMEvent()` stores the OS-IME code point in a single scalar `EventManager::_lastOsImeKey`. If a player using Windows/ibus/Android IME commits `"가"` followed by Space/Enter (or commits two syllables `"가나"` in one poll batch), the second event overwrites `_lastOsImeKey` with `0` or `나` before the script pops the first `'x'` surrogate—producing literal `"x "` or `"나나"`.
   - **Fix:** Store `uint32 osImeChar` on `SciEvent` itself (populated in `getScummVMEvent()`), set `_lastOsImeKey = event.osImeChar` when an event is dequeued in `getSciEvent()`, and clear it once consumed.

9. **`texteditChangeUtf8()` undoes Backspace/Delete/cursor keys on over-wide lines; `KoreanEditLine::adopt()` corrupts state on `writeRun()` failure and collides on `_last`; `kgraphics.cpp` clamps UTF-8 cursor to byte length**
   - **Location:** [controls16.cpp:203-207](file:///home/thkim/work/tmp/scummvm-review-p3/engines/sci/graphics/controls16.cpp#L203-L207), [korean_input.cpp:73-98](file:///home/thkim/work/tmp/scummvm-review-p3/engines/sci/korean_input.cpp#L73-L98), [kgraphics.cpp:945-949](file:///home/thkim/work/tmp/scummvm-review-p3/engines/sci/engine/kgraphics.cpp#L945-L949)
   - **Failure scenario:**
     - Unlike upstream (`controls16.cpp:335-349`), `texteditChangeUtf8` runs `if (width >= rect.width()) _koreanLine.undo(text, cursor);` for **all** `changed` keys, not just insertions. If an edit field is pre-populated with text wider than `rect.width()` (e.g., a long save name), Backspace/Delete/Left/Right/Home/End are all undone, trapping the user.
     - In `KoreanEditLine::adopt()` (`korean_input.cpp:89-95`), if `maxBytes < 3`, `text` is cleared and `_composing` is set `true` before `writeRun()` returns `false`, leaving corrupted state. Also, if a previous edit closed on a single ASCII jamo letter (e.g., `"a"` in English mode), `_last == "a"` prevents `adopt()` from composing `'ㅁ'` on a new prompt opened with `'a'` after toggling Hangul on.
     - In `_k_GenericDrawControl` (`kgraphics.cpp:945-949`), `cursorPos` (a code-point index in UTF-8 mode) is clamped against `text.size()` (bytes), writing an out-of-range code-point index back into `SELECTOR(cursor)`.
   - **Fix:** Only enforce `width >= rect.width()` undo when a character was inserted/composed; in `adopt()`, check fit before mutating `text`/`_composing` (or snapshot+undo on failure); in `_k_GenericDrawControl`, clamp against `utf8Length` in UTF-8 mode.

10. **SCI32 games with `language=ko` + `ko/` folder enter `TextMode::kUtf8` + `KO_KOR` and crash in `GfxText32::SwitchToFont1001OnKorean`**
    - **Location:** [textmode.cpp:57-79](file:///home/thkim/work/tmp/scummvm-review-p3/engines/sci/textmode.cpp#L57-L79), [text32.cpp:345](file:///home/thkim/work/tmp/scummvm-review-p3/engines/sci/graphics/text32.cpp#L345), [text32.cpp:801](file:///home/thkim/work/tmp/scummvm-review-p3/engines/sci/graphics/text32.cpp#L801), [text32.cpp:954-971](file:///home/thkim/work/tmp/scummvm-review-p3/engines/sci/graphics/text32.cpp#L954-L971)
    - **Failure scenario:** `decideTextMode()` allows `encoding = kUtf8` and `language = KO_KOR` for `SCI_VERSION_2+` (with `hires = false`, even tested in `test_sci32HasNoHiresText`), and `SciEngine::run()` loads `sci-ko.str`. However, `GfxText32` has no UTF-8 decoder and `GfxText32::SwitchToFont1001OnKorean()` lacks the `if (g_sci->getTextMode().utf8()) return false;` guard that `GfxText16` has. UTF-8 Hangul byte pairs (`0xB0..0xC8`, `0xA1..0xFE`) trigger `setFont(1001)` -> `GfxFontKorean` -> fatal `error("Could not load ScummVM's 'korean.fnt'")`.
    - **Fix:** Keep `mode.encoding = kSingle` and `mode.language = in.detected` in `decideTextMode()` for `in.version >= SCI_VERSION_2` (SCI32 is out of scope per `DESIGN.md` section 1), and guard `GfxText32::SwitchToFont1001OnKorean()` with `if (g_sci->getTextMode().utf8()) return false;`.

11. **`kFormat` length-diagnostic calls `lookupText(argv[1], 0)` on TEXT resources and uses a process-global static `warned` map**
    - **Location:** [kstring.cpp:560-570](file:///home/thkim/work/tmp/scummvm-review-p3/engines/sci/engine/kstring.cpp#L560-L570)
    - **Failure scenario:** When `kFormat(dest, textResNr, index, ...)` formats a TEXT resource (`argv[1].getSegment() == 0`) with a translated `%s` argument that makes `korean.size() > english.size()`, line 565 calls `lookupText(argv[1], 0)` (wrong index, spurious out-of-bounds warning if entry 0 is absent) and passes segment 0 to `keyOf()` (bogus `bufferId(0, textResNr)` lookup), while `once` omits `argv[2]`. In addition, `static Common::HashMap<uint32, bool> warned` persists across games in the same ScummVM session.
    - **Fix:** Only call `keyOf()` when `argv[1].getSegment() != 0` (using `_segMan->getString(argv[1])`), include `argv[2]` in `once` for segment-0 formats, and move `warned` to a `SciEngine` member.

---

### Minor

12. **`EventManager` swallows `kSciActionHangulToggle` keys even when `_hangulBuiltin` is false**
    - **Location:** [event.cpp:322-327](file:///home/thkim/work/tmp/scummvm-review-p3/engines/sci/event.cpp#L322-L327), [metaengine.cpp:700](file:///home/thkim/work/tmp/scummvm-review-p3/engines/sci/metaengine.cpp#L700)
    - **Failure scenario:** If `language=ko` is set on a target with `hangul_builtin=false` (OS IME only), `hangul_input=false`, `korean_text=false`, or an upstream CP949 Korean game, `initKeymaps()` still registers `sci-korean` (`RALT`, `C+SPACE`, `S+SPACE`, `SCROLLOCK`, `F12`) and `event.cpp:322-327` swallows those events unconditionally instead of letting them pass to the game.
    - **Fix:** Only register the keymap when `korean_text` and `hangul_builtin` are not false (and/or only swallow `kSciActionHangulToggle` when `_hangulBuiltin` is true).

13. **`GfxScreen::putFontPixel()` does not clear `_textPlane`**
    - **Location:** [screen.h:393-425](file:///home/thkim/work/tmp/scummvm-review-p3/engines/sci/graphics/screen.h#L393-L425)
    - **Failure scenario:** When an `original`-routed glyph (symbols `< 0x20`, or ASCII under `range.basic-latin=original`) is drawn via `GfxFontFromResource::draw()` -> `putFontPixel()` over pixels that still have coverage in `_textPlane` (without an intervening `eraseRect`), the old hi-res glyph remains composited on top.
    - **Fix:** Call `clearTextRect(Common::Rect(x, actualY, x + 1, actualY + 1))` in `putFontPixel()` for the non-upscaled display path.

14. **`kDrawStatus` bypasses `ScriptStrings`**
    - **Location:** [kmenu.cpp:82](file:///home/thkim/work/tmp/scummvm-review-p3/engines/sci/engine/kmenu.cpp#L82)
    - **Failure scenario:** `kDrawStatus` calls `g_sci->strSplit(text.c_str(), nullptr)` instead of `g_sci->strSplitHeap(textReference, text, nullptr, nullptr)`. Any game passing a direct or `kStrCpy`'d script string to `kDrawStatus` (rather than a `kFormat` result as in KQ1) stays in English.
    - **Fix:** Use `g_sci->strSplitHeap(textReference, text, nullptr, nullptr)`.

15. **`strAtUtf8()` read past `dest_r.maxSize` returns `0` instead of `s->r_acc` and skips the warning**
    - **Location:** [kstring.cpp:163-164](file:///home/thkim/work/tmp/scummvm-review-p3/engines/sci/engine/kstring.cpp#L163-L164)
    - **Failure scenario:** Upstream `kStrAt` (`kstring.cpp:217-220`) warns and returns `s->r_acc` when `offset >= dest_r.maxSize` (required by KQ5's `0xFFFF` call). In UTF-8 mode, `strAtUtf8()` silently returns `make_reg(0, 0)`.
    - **Fix:** When `utf8IndexIsPastEnd(p, index)` and `(int)index >= dest_r.maxSize`, emit the warning and return `s->r_acc`.

16. **`strcheck.py` duplicate detection misses numerically equal keys with leading zeros**
    - **Location:** [strcheck.py:70-75](file:///home/thkim/work/tmp/scummvm-review-p3/tools/korean/strcheck.py#L70-L75)
    - **Failure scenario:** `strcheck.py` stores `key = (parts[0], parts[1], room)` as raw strings, so `05\t007\tA` and `5\t7\tB` are not reported as duplicates even though `ScriptStrings` parses both to `(5, 7, kAnyRoom)` and warns on load.
    - **Fix:** Normalize `key = (int(parts[0]), int(parts[1]), int(room) if room != "any" else None)`.

17. **`SciEngine` constructor passes empty folder code to `SearchMan` when `text_encoding=utf8` is forced on `UNK_LANG`**
    - **Location:** [sci.cpp:230-231](file:///home/thkim/work/tmp/scummvm-review-p3/engines/sci/sci.cpp#L230-L231)
    - **Failure scenario:** With `text_encoding=utf8` on a fallback/unknown-language target (`detected == UNK_LANG`), `getLanguageFolderCode(UNK_LANG)` returns `""`, calling `SearchMan.addSubDirectoryMatching(gameDataDir, "", 1)` and attempting to open `sci-.str`.
    - **Fix:** Guard `SearchMan.addSubDirectoryMatching` and `_scriptStrings.load` on `*code != '\0'`.

VERDICT: REJECT
