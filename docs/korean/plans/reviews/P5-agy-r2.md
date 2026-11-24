### Status of Round 1 Findings

1. **Critical — Stack-buffer-overflow read in `AgiEngine::testSaid()` for `nwords >= 8`**: **Addressed.**
   - [op_test.cpp:372-377](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/op_test.cpp#L372-L377) dispatches `condSaid` directly to `testSaid(code + ip, MAX<int>(0, state->_curLogic->size - ip))` (`size` and `cIP` are signed `int` in [logic.h:32-34](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/logic.h#L32-L34)); `condSaid()` clamps to 16 bytes ([op_test.cpp:293](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/op_test.cpp#L293)).
   - [korparse.cpp:205-215](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/korparse.cpp#L205-L215) zero-fills `spec[257]`, checks `availBytes < 1`, and reads at most `MIN(nwords, (availBytes - 1) / 2)` words, verified by 100,000 exact-size heap differential specs in `test_saidSpecIsReadWithinTheBytecode()` ([korparse.h:288-358](file:///home/thkim/work/tmp/scummvm-review-p5/test/engines/agi/korparse.h#L288-L358)).

2. **Important — Broken `AGKO` trailer fallback mojibakes UTF-8 v11 strings via CP850**: **Addressed.**
   - [saved_strings.cpp:96-112](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/saved_strings.cpp#L96-L112) and [saveload.cpp:762-766](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/saveload.cpp#L762-L766): `finishSavedStrings()` converts only on `kSavedStringsNone`, and `convertLegacyStrings()` skips slots that are already valid UTF-8, preserving UTF-8 on both truncated trailers and upstream-resaved files. Covered by 20,000 randomized tables in [saved_strings.h:158-192](file:///home/thkim/work/tmp/scummvm-review-p5/test/engines/agi/saved_strings.h#L158-L192).

3. **Important — `TextMgr::hangulToggle()` draws onto active Menus, Inventory, Message Boxes, and Save dialogs**: **Addressed.**
   - [text.cpp:160-170](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/text.cpp#L160-L170) gates screen redraw on `lineIsShown = inString || (_promptEnabled && !_vm->cycleInnerLoopIsActive())`, committing without drawing when any overlay inner loop is active. (Minor visual quirk remains if a prompt cell was inverted before opening a menu and Han/Yeong is pressed inside the menu: the cell stays inverted until the prompt next redraws, but it no longer corrupts overlays.)

4. **Important — `cmdSetSimple` / `readSavedGameSlots` 30-byte vs `saveGame` 29-byte mismatch**: **Addressed.**
   - [op_cmd.cpp:975-979](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/op_cmd.cpp#L975-L979), [textcells.cpp:158-162](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/textcells.cpp#L158-L162), and [systemui.cpp:729](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/systemui.cpp#L729) clip UTF-8 descriptions to `SAVEDGAME_DESCRIPTION_BYTES` (29) while preserving upstream's 30-byte byte-path behavior and full zero-fill, verified by `test_automaticDescriptionMatchesTheSavedOne()` ([cell_bounds.h:438-456](file:///home/thkim/work/tmp/scummvm-review-p5/test/engines/agi/cell_bounds.h#L438-L456)).

5. **Important — `Console::Cmd_KOverlay` missing-glyph check broken for standard maps**: **Addressed.**
   - Added `AgiHiResText::hasGlyph(uint32 cp) const` ([hirestext.cpp:121-123](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/hirestext.cpp#L121-L123)) checking `_set->getChain(_set->route(cp))->has(cp)`, used by [console.cpp:824](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/console.cpp#L824) and verified by `test_hasGlyphSaysWhatTheFaceHas()` ([hirestext.h:50-63](file:///home/thkim/work/tmp/scummvm-review-p5/test/engines/agi/hirestext.h#L50-L63)).

6. **Important — `commandpromptwindow=true` drops OS-IME input and breaks first built-in Hangul jamo**: **Partially addressed — introduces a new Important bug (see below).**
   - [text.cpp:912](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/text.cpp#L912) and [text.cpp:1096-1117](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/text.cpp#L1096-L1117) now accept code points and pass the opening key to the command window's edit loop, which works when keys arrive one per frame, but breaks whenever multiple keys are buffered (see New Breakage #1).

7. **Important — Detection mismatch between `detectGames()` and `detectGame()` for map-only translation folders**: **Addressed.**
   - [detection.cpp:408-420](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/detection.cpp#L408-L420) now uses `overlayLanguages(files)`, matching `detectGames()` and removing the empty-string `code` probe.

8. **Important — `Words::analyze()` returning `unknownPos > MAX_WORDS`**: **Addressed.**
   - [words.cpp:629-638](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/words.cpp#L629-L638) keeps the unknown word at index `MAX_WORDS - 1` with `unknownPos = typed.size() <= MAX_WORDS`, [words.cpp:371-373](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/words.cpp#L371-L373) also bounds the English `parseUsingDictionary()` loop at `MAX_WORDS`, and [analyze_fuzz.h:185-188](file:///home/thkim/work/tmp/scummvm-review-p5/test/engines/agi/analyze_fuzz.h#L185-L188) restores the strict invariant.

9. **Important — Unchecked logic resource bounds and unclipped `drawCoverageOnDisplay()`**: **Addressed.**
   - Added overflow-safe, bounds-checked `logicMessageCount()` / `logicMessageOffset()` ([saidscan.cpp:71-90](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/saidscan.cpp#L71-L90)), wired into `rawLogicMessageCount()` ([agi.cpp:736](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/agi.cpp#L736)) and `Cmd_KOverlay()` ([console.cpp:796-801](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/console.cpp#L796-L801)), and tested with 60,000 random resources in [logic_bounds.h](file:///home/thkim/work/tmp/scummvm-review-p5/test/engines/agi/logic_bounds.h).
   - `collectSaids()` clamps bytecode length to `len - 2` ([agi.cpp:1073-1075](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/agi.cpp#L1073-L1075)); `dropDownColumn()` clamps `>= 1` ([textcells.cpp:197](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/textcells.cpp#L197)); inventory columns clamp `>= 0` ([inv.cpp:70-77](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/inv.cpp#L70-L77)); `drawCoverageOnDisplay()` clips pixels to `[0, _displayScreenWidth)` × `[0, _displayScreenHeight)` ([graphics.cpp:1296-1297](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/graphics.cpp#L1296-L1297)), and `copyDisplayRectToScreen()` already clamps the blit rect ([graphics.cpp:309-318](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/graphics.cpp#L309-L318)).

10. **Minor — Amiga v3 `logicDirectoryName()`**: **Addressed.**
    - [loader.h:179-184](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/loader.h#L179-L184) returns `"dirs"` on Amiga and `Common::Path()` when `_name.empty()`.

11. **Minor — Unit test gaps**: **Addressed.**
    - Regression tests added for `readSaidSpec`, `finishSavedStrings`, `copyDescription`, `hasGlyph`, `logicMessageCount/Offset`, clamped `dropDownColumn`, global `text_encoding=utf8`, and strict `unknownPos` in `analyze_fuzz.h`.

12. **Minor — `strcheck.py` header parsing, duplicate include, stale comments**: **Partially addressed.**
    - Duplicate `#include "common/config-manager.h"` removed ([metaengine.cpp:32-36](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/metaengine.cpp#L32-L36)) and stale comments in `text.h`, `text.cpp:1198`, and `systemui.cpp:411` fixed. `strcheck.py:175` was intentionally left as cosmetic, and one stale comment remains at [text.cpp:1188](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/text.cpp#L1188) (`the save name shows English (D8)`).

*(Other fixes in `fix-r1.diff` — the 2-line Korean save prompt in [systemui.cpp:159](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/systemui.cpp#L159) and ignoring global `text_encoding=utf8` on untranslated games in [agi.cpp:662-663](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/agi.cpp#L662-L663) / [textmode.cpp:57-64](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/textmode.cpp#L57-L64) — also check out cleanly.)*

---

### New Breakage Introduced by the Fix

1. **Important — `AgiEngine::enqueueKey()` appends to the tail of `_keyQueue` instead of the front, scrambling characters in `commandpromptwindow=true` on any multi-character OS-IME commit or fast typing**
   - **Location**: [text.cpp:1108-1117](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/text.cpp#L1108-L1117), [agi.h:849-850](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/agi.h#L849-L850), [keyboard.cpp:401-403](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/keyboard.cpp#L401-L403), [keyboard.h:27-36](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/keyboard.h#L27-L36)
   - **Failure scenario**: `isKeypress()` calls `processScummVMEvents()`, which drains all pending OS events in one `while (_eventMan->pollEvent(event))` loop into the FIFO ring buffer `_keyQueue`. When a real OS IME commits a word (e.g., `"술집을"` delivered as three consecutive `EVENT_KEYDOWN` events in one frame) or a fast typist presses `t`, `n`, `f` in one 10 ms tick:
     1. `_keyQueue` holds `['술', '집', '을']` (or `['t', 'n', 'f']`).
     2. `getKeypress()` pops `'술'` (or `'t'`), leaving `['집', '을']` (or `['n', 'f']`) still queued.
     3. `promptCommandWindow()` calls `_vm->enqueueKey(pendingKey)`—whose doc comment claims *"Puts @p key first in the line the next edit loop reads"*—but `enqueueKey()` calls `keyEnqueue(key)`, which writes to `_keyQueue[_keyQueueEnd++]` (the **back** of the queue).
     4. `_keyQueue` now holds `['집', '을', '술']` (or `['n', 'f', 't']`), so `askForCommand()` receives **`"집을술"`** instead of `"술집을"`, and fast `tnf` (`술`) composes into **`"ㅜㄹㅅ"`**!
     *(The headless `xdotool` check missed this because `xdotool` inserts inter-key delays so `_keyQueue` happened to be empty on each key.)*
   - **Fix**: Either prepend to the head of `_keyQueue` (`enqueueKeyFront()`: decrement `_keyQueueStart` modulo `KEY_QUEUE_SIZE` when the queue is not full), or—simpler and immune to queue capacity—pass `pendingKey` directly into `SystemUI::askForCommand(commandText, pendingKey)` and call `_text->stringKeyPress(pendingKey)` once right after `stringEdit()` initializes the input line and before entering its `processAGIEvents()` loop.

VERDICT: APPROVE WITH CHANGES
