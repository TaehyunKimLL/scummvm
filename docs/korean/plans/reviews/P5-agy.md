### Critical

1. **Stack-buffer-overflow read in `AgiEngine::testSaid()` for any `said()` with >= 8 word groups (breaks English targets too)**
   - **Location**: [op_test.cpp:149-151](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/op_test.cpp#L149-L151), [op_test.cpp:296-303](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/op_test.cpp#L296-L303), [op_test.cpp:370-372](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/op_test.cpp#L370-L372)
   - **Failure scenario**: `testIfCode()` copies only 16 bytes of logic bytecode into a stack buffer (`uint8 p[16]; memmove(p, (code + ip), 16);`) and passes `p` to `condSaid()`, which calls `vm->testSaid(p[0], p + 1)`. Thus `cc` (`p + 1`) only holds 15 valid bytes (at most 7 `uint16` word IDs). Unlike upstream, which read `cc` lazily only while input words matched, P5 eagerly unpacks all `nwords` upfront:
     ```cpp
     uint16 spec[256];
     for (int i = 0; i < nwords; i++)
         spec[i] = READ_LE_UINT16(cc + 2 * i);
     ```
     Whenever any AGI logic (English or Korean, regardless of player input length) evaluates a `said()` with `nwords >= 8`, `i = 7` reads `p[15]` and `p[16]`, causing an out-of-bounds stack read (ASan crash / UB) and comparing the player's sentence against garbage stack bytes for word 8+.
   - **Fix**: In `condSaid()`, read directly from the logic bytecode (`code + ip + 1`, bounds-checked against `state->_curLogic->size`) instead of `p + 1`, and verify `ip + 1 + 2 * nwords <= state->_curLogic->size` before reading `spec`.

---

### Important

2. **Broken `AGKO` trailer fallback in `loadGame()` mojibakes version 11 UTF-8 strings via CP850 conversion**
   - **Location**: [saveload.cpp:761-767](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/saveload.cpp#L761-L767), [saved_strings.cpp:71-107](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/saved_strings.cpp#L71-L107)
   - **Failure scenario**: `saveGame()` writes valid UTF-8 prefixes into the version 11 string table via `legacySlot()`. On load, if the `AGKO` trailer is truncated or corrupted, `readSavedStrings()` leaves the v11 UTF-8 slots intact and returns `kSavedStringsBroken`, and `loadGame()` logs `"its strings are read from the version 11 table"`. However, line 765 checks `if (r != kSavedStringsRead)` and runs `convertLegacyStrings()`, which decodes those already-UTF-8 bytes as `Common::kDos850` and re-encodes them to UTF-8. The same garbling happens if a Korean save is loaded and re-saved by unmodified upstream ScummVM (which drops the `AGKO` trailer while preserving the UTF-8 v11 slots).
   - **Fix**: Do not run `convertLegacyStrings()` on `kSavedStringsBroken` (only on `kSavedStringsNone`), and in `convertLegacyStrings()` skip slots that are already valid UTF-8 (`TextCells::isValidUtf8(slot)`), or sanitize invalid UTF-8 slots on `kSavedStringsBroken` without CP850 transcoding.

3. **`TextMgr::hangulToggle()` draws onto active Menus, Inventory, Message Boxes, and Save Slot dialogs**
   - **Location**: [text.cpp:153-169](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/text.cpp#L153-L169), [keyboard.cpp:414-418](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/keyboard.cpp#L414-L418)
   - **Failure scenario**: Start composing a Hangul syllable on the command prompt (`_hangulLine.composing() == true`, `_promptEnabled == true`), then open the menu (`ESC`), inventory (`TAB`), save dialog (`F5`), or a message box (none of these commit `_hangulLine`), and press Han/Yeong (`Ctrl+Space`, `F12`, `RALT`, `HANGUL`). Because `inString` is `false` and `_promptEnabled` is still `true`, `hangulToggle()` enters `inputEditOn(); hangulEndRun(cells, len); inputEditOff();`, calling `displayCharacter(0x08)` and `displayCharacter(cells[len - 1])` at whatever cursor position and text attributes the active menu/inventory/dialog left behind—corrupting the on-screen UI and leaving the prompt's preedit cell inverted when the overlay closes. The same happens when `commandpromptwindow=true` is enabled.
   - **Fix**: Gate screen redraw in `hangulToggle()` on actually being in the getString/getNumber loop or at the live prompt (`!_vm->cycleInnerLoopIsActive() && !_messageState.window_Active && _promptEnabled && !_optionCommandPromptWindow`); otherwise commit `_hangulLine` without drawing. Also commit `_hangulLine` when entering menus/dialogs or in `promptRedraw()`.

4. **`cmdSetSimple` (30-byte clip) vs `saveGame` (29-byte clip) breaks automatic save/restore (`set.simple`) for Korean names**
   - **Location**: [op_cmd.cpp:975](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/op_cmd.cpp#L975), [saveload.cpp:94](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/saveload.cpp#L94), [saveload.cpp:107](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/saveload.cpp#L107), [systemui.cpp:727](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/systemui.cpp#L727), [systemui.cpp:766](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/systemui.cpp#L766)
   - **Failure scenario**: P5-fixes F1 reduced `saveGame()`'s UTF-8 clip to `SAVEDGAME_DESCRIPTION_BYTES` (29 bytes) to match `Common::strlcpy(..., SAVEDGAME_DESCRIPTION_LEN)` (30 = 29 + NUL). However, `cmdSetSimple()` still passes `sizeof(state->automaticSaveDescription)` (31) to `TextCells::copyDescription()`, which clips to **30** bytes. If a game calls `set.simple` with a 10-syllable Korean string (30 UTF-8 bytes, e.g. `"안녕하세요안녕하세요"`), `automaticSaveDescription` stores 30 bytes, `saveGameAutomatic()` saves 27 bytes (9 syllables), and `figureOutAutomaticSavedGameSlot()` compares `strcmp(savedGameEntry->description, automaticSaveDescription)` (27 vs 30 bytes), which never matches—so automatic restore fails and every automatic save allocates a new slot instead of overwriting. (`readSavedGameSlots()` at `systemui.cpp:727` likewise clips to `SYSTEMUI_SAVEDGAME_DESCRIPTION_LEN` = 30 instead of 29.)
   - **Fix**: In UTF-8 mode, clip `automaticSaveDescription` in `cmdSetSimple()` to `SAVEDGAME_DESCRIPTION_BYTES` (29) while zero-filling all 31 bytes (keeping 30-byte clip on the non-UTF-8 path), and clip UTF-8 descriptions in `readSavedGameSlots()` to `SAVEDGAME_DESCRIPTION_BYTES`.

5. **`Console::Cmd_KOverlay` missing-glyph check never detects missing glyphs for standard maps**
   - **Location**: [console.cpp:824-827](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/console.cpp#L824-L827), [hirestext.cpp:121-144](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/hirestext.cpp#L121-L144)
   - **Failure scenario**: `Cmd_KOverlay` tests `cp < 0x80 || (_vm->_hiresText && _vm->_hiresText->glyph(cp, rows))` to report untranslatable/missing characters. However, for maps whose chain does not end in `original` (including `korean-default.map` and `HIRESBIT.MAP` with `missing=u+25a1`), `HiResFontSet::route(cp)` returns chain `0` and `AgiHiResText::glyph()` draws the fallback `□` box and returns `true` (`hirestext.cpp:132` itself has to query `_set->getChain(_set->route(cp))->has(cp)` for `hires_text_log`). Consequently, an overlay containing characters absent from all loaded faces (e.g., Hanja or symbols) renders as `□` in-game while `koverlay` falsely reports `missing glyphs 0`.
   - **Fix**: Add `bool AgiHiResText::hasGlyph(uint32 cp) const` checking `_set && (c = _set->route(cp)) >= 0 && _set->getChain(c)->has(cp)`, and call `hasGlyph(cp)` in `Cmd_KOverlay`.

6. **`commandpromptwindow=true` drops OS-IME input and breaks the first key of built-in Hangul composition**
   - **Location**: [text.cpp:906-912](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/text.cpp#L906-L912), [text.cpp:1092-1119](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/text.cpp#L1092-L1119)
   - **Failure scenario**: With `commandpromptwindow=true` ("Pause when entering commands"), `promptKeyPress()` runs before `hangulEditKey()` and checks `if (acceptableInput && !codePoint) promptCommandWindow(false, newKey);`.
     - Typing via OS IME (`codePoint == true`) is silently dropped and never opens the command window.
     - Typing via the built-in Hangul composer (e.g., `tnfwlqdmf` for `술집을`) opens the dialog pre-filled with raw ASCII `'t'` instead of composing `'ㅅ'`, garbling the first syllable.
   - **Fix**: Widen `promptCommandWindow()`'s `newKey` parameter to `int`, encode `codePoint` to UTF-8 into `commandText`, and when `_hangulInput && _hangulMode && HangulLine::isJamoKey(newKey)` open the window empty and feed `newKey` into `stringKeyPress()` (or compose the initial jamo in the dialog's `HangulLine`).

7. **Detection mismatch between `detectGames()` and `detectGame()` for map-only translation folders (plus empty-string truthiness bug)**
   - **Location**: [detection.cpp:356](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/detection.cpp#L356), [detection.cpp:407-419](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/detection.cpp#L407-L419)
   - **Failure scenario**: `detectGames()` calls `overlayLanguages()` -> `Common::translationFolderLanguages(fslist, "agi-%s.str")`, which recognizes a `ko/` subfolder containing only `hires*.map` (no `agi-ko.str`), matching `decideTextMode()` ([textmode.cpp:61-65](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/textmode.cpp#L61-L65)). However, `detectGame()` only checks for `agi-%s.str`. A game folder with `ko/HIRESBIT.MAP` and a missing/misspelt `agi-ko.str` is added by the launcher as `lsl1-ko` and then fails to launch with `"Game data not found"`. Additionally, `Common::getLanguageFolderCode(language)` returns `""` (never `nullptr`) for unmapped languages ([language.cpp:150-165](file:///home/thkim/work/tmp/scummvm-review-p5/common/language.cpp#L150-L165)), so `if (!overlay && code)` still probes `getSubDirectory("")` / `"agi-.str"`.
   - **Fix**: Check `*code != '\0'` and use `Common::translationFolderLanguages()` (or mirror `overlayLanguages()`) inside `detectGame()` so `detectGames()` and `detectGame()` agree.

8. **`Words::analyze()` can return `unknownPos > MAX_WORDS` and drop the unknown word, masked by a weakened fuzz test**
   - **Location**: [words.cpp:542-631](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/words.cpp#L542-L631), [words.cpp:645-646](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/words.cpp#L645-L646), [analyze_fuzz.h:183-188](file:///home/thkim/work/tmp/scummvm-review-p5/test/engines/agi/analyze_fuzz.h#L183-L188)
   - **Failure scenario**: The comment in `analyze_fuzz.h:185-186` claims `> MAX_WORDS` (20) is unreachable from the 40-cell prompt, and weakens the assertion to `a.unknownPos <= a.candidates[0].size() || a.candidates[0].size() == MAX_WORDS`. That assumption is false: (a) a Korean sequence word emitting 2 AGI words from 1–2 cells + space fits 11×2 = 22 words plus an unknown word inside 40 cells; and (b) `cmdParse()` passes `stringPrintf()` output (up to 8,000 bytes) into `parseUsingDictionary()`. When an unknown word lands past index 20, `typed.resize(MAX_WORDS)` drops the unknown word while `out.unknownPos` stays `> 20` (and wraps `mod 256` into `VM_VAR_WORD_NOT_FOUND` if >= 256), leaving `VM_VAR_WORD_NOT_FOUND` pointing past `_egoWordCount` (20) and `Console::Cmd_Parse` unable to print the unknown word.
   - **Fix**: Enforce the `MAX_WORDS` cap while building `typed` (stop pushing once `typed.size() == MAX_WORDS`, or truncate before recording `unknownPos` so `unknownPos <= typed.size() <= MAX_WORDS` always holds), and restore the strict invariant `TS_ASSERT(a.unknownPos <= a.candidates[0].size())` in `analyze_fuzz.h`.

9. **Unchecked logic resource bounds in `rawLogicMessageCount()`, `collectSaids()`, `Cmd_KOverlay`, and unclipped negative-coordinate writes in `drawCoverageOnDisplay()`**
   - **Location**: [agi.cpp:734-735](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/agi.cpp#L734-L735), [agi.cpp:1072](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/agi.cpp#L1072), [console.cpp:796-801](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/console.cpp#L796-L801), [graphics.cpp:1287-1299](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/graphics.cpp#L1287-L1299), [textcells.cpp:194-198](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/textcells.cpp#L194-L198), [inv.cpp:70](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/inv.cpp#L70)
   - **Failure scenario**:
     - `rawLogicMessageCount()`, `collectSaids()`, and `Cmd_KOverlay` index `data + 2 + READ_LE_UINT16(data)` and message offset entries without checking against `_game.dirLogic[nr].len`; a malformed or truncated logic volume causes an out-of-bounds heap read.
     - `TextCells::dropDownColumn()` returns `(screenColumns - 1) - itemCells`, which becomes negative for any translated menu item >= 39 cells; similarly `inv.cpp:70` computes `39 - MIN(cellLen, 40) = -1` for a 40-cell translated object name. `GfxMgr::drawCoverageOnDisplay()` calls `putFontPixelOnDisplay(x, y, cx, cy, ...)` without bounds-checking `x + cx` or `y + cy` against `[0, _displayScreenWidth)` / `[0, _displayScreenHeight)`, causing a heap buffer underflow/overflow on `_displayScreen`.
   - **Fix**: Validate `len >= 2`, `2 + bytecodeSize < len`, and offset table bounds against `_game.dirLogic[nr].len` in `rawLogicMessageCount()`, `collectSaids()`, and `Cmd_KOverlay`. Clip pixel coordinates in `drawCoverageOnDisplay()` (and `copyDisplayRectToScreen()`), and clamp `dropDownColumn()` >= 1 and `inv.cpp:70` >= 0.

---

### Minor

10. **Amiga v3 `logicDirectoryName()` returns `"dir"` instead of `"dirs"`**
    - **Location**: [loader.h:179](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/loader.h#L179), [loader_v3.cpp:82-85](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/loader_v3.cpp#L82-L85)
    - **Failure scenario**: For Amiga v3 games, `AgiLoader_v3::_name` is empty and the directory file is named `"dirs"`, not `_name + "dir"`. `logicDirectoryName()` returns `"dir"`, so `gameFingerprint()` fails to open the directory file and returns an empty fingerprint.
    - **Fix**: Return `_name.empty() ? Common::Path("dirs") : Common::Path(_name + DIR_)` (or store the detected directory filename during `detectGame()`).

11. **Unit test coverage gaps for P5 edge cases**
    - **Location**: [saved_strings.h](file:///home/thkim/work/tmp/scummvm-review-p5/test/engines/agi/saved_strings.h), [textcells.h](file:///home/thkim/work/tmp/scummvm-review-p5/test/engines/agi/textcells.h), [analyze_fuzz.h:183-188](file:///home/thkim/work/tmp/scummvm-review-p5/test/engines/agi/analyze_fuzz.h#L183-L188)
    - **Failure scenario**: `saved_strings.h` tests `readSavedStrings()` and `convertLegacyStrings()` in isolation, so it did not catch the `loadGame()` fallback calling `convertLegacyStrings()` on `kSavedStringsBroken` (#2). `textcells.h` does not test `dropDownColumn()` with `itemCells >= screenColumns` (#9) or `wordWrap()` with `maxWidth <= 0`. `analyze_fuzz.h` loosened its assertion instead of catching #8.
    - **Fix**: Add regression tests covering the broken-trailer UTF-8 preservation path, `dropDownColumn()` clamping, `testSaid()` with `nwords >= 8`, and the strict `unknownPos <= candidates[0].size()` invariant.

12. **`strcheck.py` header detection fragility and minor cleanup**
    - **Location**: [strcheck.py:175](file:///home/thkim/work/tmp/scummvm-review-p5/tools/korean/strcheck.py#L175), [metaengine.cpp:22](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/metaengine.cpp#L22), [metaengine.cpp:35](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/metaengine.cpp#L35), [text.h:221-223](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/text.h#L221-L223), [text.cpp:1176](file:///home/thkim/work/tmp/scummvm-review-p5/engines/agi/text.cpp#L1176)
    - **Failure scenario**: `detect_engine()` checks `" engine=agi" in first`, which misses tab-separated headers and false-matches hypothetical prefixes like `engine=agi2`. `metaengine.cpp` includes `"common/config-manager.h"` twice. Comments in `text.h:221-223` and `text.cpp:1176,1186` still refer to `agi_hangul_*`, the removed KO/EN badge, and ASCII-only save names (superseded by D8).
    - **Fix**: Parse whitespace-delimited `key=value` tokens (`"engine=agi" in first.split()[1:]`) in `strcheck.py`, remove the duplicate `#include`, and update the stale comments.

---

### Verified Solid
- `TextCells::decode()`, `clipCells()`, `clipBytes()`, `wordWrap()` (including D14.1 `wordEndChar = 0` fix and `hangulAny` line splitting), and `agiStringsEqual()` are well-bounded and preserve upstream byte-path behavior.
- `HangulLine`, `TextOverlay` (header/fingerprint checks, escape handling, UTF-8 rejection), `SaidScanner` (bounds-checked opcode walk), `korparse` (`buildKoreanCandidates`, `saidMatches`), and `AgiHiResText` stencil centering/fallback map selection are clean and deterministic.
- `PATCHES.md` matches the upstream AGI files modified by the diff.

VERDICT: REJECT
