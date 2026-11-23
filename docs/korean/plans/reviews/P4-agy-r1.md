### Status of Earlier Findings

1. **Critical #1 — Heap buffer overflow in `SegManager::strncpy(reg_t, const char*, size_t)` after `growDynmem()`**: **Addressed** (with a new regression on the `raw -> non-raw` branch; see below).
   - `utf8CopyPlan()` ([utf8.cpp:185-206](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/utf8.cpp#L185-L206)) restricts `growDynmem()` to unbounded copies (`n == 0xFFFFFFFFU`), clamps finite `n` to `capacity`, rounds down mid-code-point cuts via `dropIncompleteTail()`, and [seg_manager.cpp:702-720](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/engine/seg_manager.cpp#L702-L720) re-plans with `growable = false` if `growDynmem()` refuses. The raw-buffer zero-padding overflow is eliminated.

2. **Important #2 — `koreanAnswerView()` intercepting save lists and edit controls in answer rooms**: **Addressed**.
   - `KoreanAnswerRoom` now stores `uint16 room` and `uint16 script` (`802` for QFG1 rooms 22/29, `2` for QFG2 room 199; [korean_parser.h:111-116](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/parser/korean_parser.h#L111-L116), [korean_parser.cpp:356-362](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/parser/korean_parser.cpp#L356-L362)), and `koreanAnswerView()` gates on `row->appliesTo(s->getCurrentCallOrigin().scriptNr)` ([kstring.cpp:174-178](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/engine/kstring.cpp#L174-L178)). Covered by `test_answerViewOnlyForTheSearchScript()` ([korean_parser.h:235-242](file:///home/thkim/work/tmp/scummvm-review-p4/test/engines/sci/korean_parser.h#L235-L242)).

3. **Important #3 — `Vocabulary::koreanAnswerLine()` running `arrange()`/`askAbout()` and expanding all synonyms**: **Addressed**.
   - `Vocabulary::koreanAnswerLine()` now calls `tokenize(..., false)` ([vocabulary.cpp:773,855](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/parser/vocabulary.cpp#L773)), skipping `arrange()`/`askAbout()`, and keys its cache by `(&_answerRoom, _answerTyped)` ([vocabulary.cpp:762-764](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/parser/vocabulary.cpp#L762-L764)).
   - `KoreanParser::englishLine()` ([korean_parser.cpp:318-354](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/parser/korean_parser.cpp#L318-L354)) now emits only whole English words from each room's allow-list (`kAziza`, `kGargoyle`, `kHut`), eliminating substring/synonym false positives such as `그리핀` (`airplane`) matching `"air"` (tested in `test_englishLineIsNotASubstringSearch()` at [korean_parser.h:176-222](file:///home/thkim/work/tmp/scummvm-review-p4/test/engines/sci/korean_parser.h#L176-L222)).
   - Caveat (already noted in [P4-fixes.md:39](file:///home/thkim/work/tmp/scummvm-review-p4/docs/korean/plans/P4-fixes.md#L39)): QFG1 room 22 (Baba Yaga's hut) remains unverified in play.

4. **Important #4 — `ScriptStrings::sci11StringsStart()` hardcoded little-endian and unverified on data**: **Addressed**.
   - Added `bool bigEndian = false` to `ScriptStrings::sci11StringsStart()` ([script_strings.cpp:279-299](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/engine/script_strings.cpp#L279-L299)), passed `g_sci->isBE()` at [script.cpp:490](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/engine/script.cpp#L490), added big-endian test coverage ([script_strings.h:89-91](file:///home/thkim/work/tmp/scummvm-review-p4/test/engines/sci/script_strings.h#L89-L91)), and verified QFG1 VGA startup under ASan.

5. **Important #5 — Test gaps and `korean_packs.h` case-sensitivity / silent partial skip**: **Addressed** (with one residual gap).
   - [korean_packs.h:40-47,50,60,95-99](file:///home/thkim/work/tmp/scummvm-review-p4/test/engines/sci/korean_packs.h#L40-L47) now resolves `<FOLDER>`, `ko`, `sci-ko.str`, and `sci-ko.dat` case-insensitively and emits `TS_WARN` for every missing pack.
   - Added 3 unit tests for `utf8CopyPlan()` ([utf8.h:312-357](file:///home/thkim/work/tmp/scummvm-review-p4/test/engines/sci/utf8.h#L312-L357)) and 2 tests for `englishLine()` / `appliesTo()` ([korean_parser.h:153-242](file:///home/thkim/work/tmp/scummvm-review-p4/test/engines/sci/korean_parser.h#L153-L242)).
   - Residual gap: `SegManager::strncpy()` itself has no unit test (only `utf8CopyPlan()` does), which allowed the `raw -> non-raw` regression below to slip through.

6. **Minor #6 — `growDynmem()` upper bound**: **Addressed** — [seg_manager.cpp:957-958](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/engine/seg_manager.cpp#L957-L958) rejects `bytes > 0xFFFFU` on SCI16.

7. **Minor #7 — Pre-checked callers bypassing `growDynmem()`**: **Addressed** — documented on `growDynmem()` ([seg_manager.h:332-333](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/engine/seg_manager.h#L332-L333)).

8. **Minor #8 — `GfxCache::getFont(1001)` in UTF-8 mode**: **Addressed** — gated with `&& !g_sci->getTextMode().utf8()` ([cache.cpp:88](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/graphics/cache.cpp#L88)), and `hiResFont()`'s `0x7ff` mask is now gated to `getSciVersion() < SCI_VERSION_2` ([cache.cpp:75](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/graphics/cache.cpp#L75)).

9. **Follow-ups (Req 6)**:
   - **QFG1 VGA startup crash**: **Addressed** — traced to `SegManager::strncpy(reg_t, reg_t)` copying `non-raw -> raw` without growing/bounding and fixed in [seg_manager.cpp:764-777](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/engine/seg_manager.cpp#L764-L777) (QFG1 VGA now starts cleanly under ASan with 0 reports).
   - **Alt+S leak**: Alt guard + unit test added ([korean_input.cpp:100-102](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/korean_input.cpp#L100-L102), [controls16.cpp:196](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/graphics/controls16.cpp#L196)); underlying Xvfb/SDL source remains open as recorded in [P4-notes.md:226-228](file:///home/thkim/work/tmp/scummvm-review-p4/docs/korean/plans/P4-notes.md#L226-L228).

---

### New Breakage Introduced by `fix-r1.diff`

#### Critical

1. **`SegManager::strncpy()` corrupts the byte / `reg_t` immediately past the NUL terminator on every unbounded UTF-8 copy (`strcpy_`) into a non-raw (stack or locals) buffer**
   - **Location**: [seg_manager.cpp:718,724-734](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/engine/seg_manager.cpp#L718) and [utf8.cpp:201-202](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/utf8.cpp#L201-L202)
   - **Failure scenario**: Every `strcpy_()`, `strcpyBounded()`, `kStrCpy` (2-arg), `kFormat`, `kStrCat`, and `kGetFarText` calls `SegManager::strncpy(dest, src, 0xFFFFFFFFU)`. When `dest` is a non-raw segment (`SEG_TYPE_STACK` or `SEG_TYPE_LOCALS`, where `dest_r.maxSize` is the remaining byte size of the entire stack/locals array, not just the string buffer):
     1. Previously, `n` remained `0xFFFFFFFFU`, so line 732 (`if ((size_t)dest_r.maxSize > n)`) was always **false**.
     2. Now, `utf8CopyPlan()` sets `plan.limit = plan.text.size() + 1` ([utf8.cpp:201-202](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/utf8.cpp#L201-L202)), and line 718 overwrites `n = use.limit` unconditionally in UTF-8 mode — even when the string fits without truncation.
     3. In the `raw -> non-raw` loop ([seg_manager.cpp:726-730](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/engine/seg_manager.cpp#L726-L730)), `i = 0..text.size()` copies the string **and** its terminating `'\0'` at offset `text.size()`, then breaks.
     4. Line 732 (`(size_t)dest_r.maxSize > n`) is now **true** whenever anything follows the string in the locals/stack segment, so line 733 executes `setChar(dest_r, n, 0)` — writing an extra NUL at offset `text.size() + 1` (one byte **past** the NUL terminator).
     5. Concrete breakage:
        - A script declares `[buf 2] nextVar` (4-byte buffer followed by an integer or object pointer `nextVar`) and executes `kStrCpy(@buf, "abc")` (3 bytes + NUL = 4 bytes, exact fit). `n` becomes `4`, `dest_r.maxSize > 4` is true, and `setChar(dest_r, 4, 0)` ([seg_manager.cpp:662-674](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/engine/seg_manager.cpp#L662-L674)) executes **`nextVar.setSegment(0)`** (destroying any object/string/list pointer in `nextVar`) and **zeroes `nextVar`'s low byte** (corrupting any integer in `nextVar`).
        - Likewise, in a 36-byte-stride save-list array on the stack/locals (`SRDialog`), writing a 35-byte UTF-8 name (+ 1 NUL = 36 bytes) into slot 0 sets `n = 36` and executes `setChar(dest_r, 36, 0)`, overwriting the first byte of slot 1 with `'\0'` and hiding all subsequent save slots.
     - ASan cannot detect this because the corrupted `reg_t` lies inside the valid `LocalVariables::_locals` or `DataStack::_entries` C++ allocation.
   - **Fix**: In [seg_manager.cpp:724-734](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/engine/seg_manager.cpp#L724-L734), track whether the loop already wrote the NUL terminator and only write the trailing NUL at `n` when it did not:
     ```cpp
     bool terminated = false;
     for (uint i = 0; i < n; i++) {
         setChar(dest_r, i, src[i]);
         if (!src[i]) {
             terminated = true;
             break;
         }
     }
     // Put an ending NUL to terminate the string
     if (!terminated && (size_t)dest_r.maxSize > n)
         setChar(dest_r, n, 0);
     ```
     (Alternatively, for `unbounded` copies in `utf8CopyPlan()` / `strncpy()`, keep `n = 0xFFFFFFFFU` since `planned.c_str()` is already NUL-terminated and bounded to `capacity - 1` bytes — tracking `terminated` fixes both unbounded and finite-`n` short-string cases cleanly.)

#### Minor

2. **`SciEngine::keyOf()` unconditionally overwrites `key.room` even when `key` came from a buffer tagged in an earlier room**
   - **Location**: [state.cpp:369-375](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/engine/state.cpp#L369-L375)
   - **Failure scenario**: When `kStrCpy` tags a single-part buffer in room A, it records `key.room = roomA` ([kstring.cpp:95-96](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/engine/kstring.cpp#L95-L96)). If that buffer is later displayed in room B, [state.cpp:373-374](file:///home/thkim/work/tmp/scummvm-review-p4/engines/sci/engine/state.cpp#L373-L374) overwrites `key.room = roomB`, discarding the copy-time room and missing a room-A-specific translation entry.
   - **Fix**: Only assign `key.room = _gamestate->currentRoomNumber();` when `key.isSet() && key.room == ScriptStrings::Key::kAnyRoom` (so direct script pointers get the current room while already-room-tagged copies keep their copy-time room).

VERDICT: REJECT
