Static functional review of P6a (read-only; no build or tests executed).

## Critical

### C1. `convertLoadedText()` corrupts and truncates binary `rtString` resources in SCUMM v3–v5 saves
- **Location:** [korean.cpp:207-229](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/korean.cpp#L207-L229) (called from [saveload.cpp:837-838](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/saveload.cpp#L837-L838))
- **Failure scenario:** `convertLoadedText()` skips `rtString` only when `_game.version >= 6`, assuming all v3–v5 `rtString` resources are NUL-terminated text. In SCUMM v3–v5, `o5_stringOps` ([script_v5.cpp:3041-3123](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/script_v5.cpp#L3041-L3123)) and engine code (e.g., Loom's `rtString` 21 at [scumm.cpp:2308](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/scumm.cpp#L2308), Indy4's `.iq` data and MI1/MI2 `.cfg` data at [script_v5.cpp:2548-2623](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/script_v5.cpp#L2548-L2623)) also use `rtString` as arbitrary binary byte arrays. When loading a legacy CP949 save containing a 32-byte binary `rtString` such as `[0x0F, 0x90, 0x00, 0x05, ...]`:
  1. `textIsUtf8(ptr, ptr + size)` returns `false` on `0x90`.
  2. `toEngineText()` stops at the first `0x00` byte (`scummTextEnd`) and re-encodes `0x90` to `0xC2 0x90`.
  3. `_res->createResource(type, idx, len + 1)` reallocates and shrinks the 32-byte array to 4 bytes, destroying all bytes after index 2 and shifting indices. Subsequent `o5_stringOps` accesses fail with OOB warnings and return `0`.
- **Fix:** For `rtString` in v3–v5, only convert resources that are actually single NUL-terminated strings filling the resource (`scummTextEnd(ptr, ptr + size) == ptr + size - 1`) and contain no binary control bytes outside SCUMM escapes / CR / LF and no unmapped CP949 pairs; never shrink a multi-byte array past interior NULs.

### C2. Savegame `_charsetBuffer` is truncated to 256 bytes, and `_charsetBufPos` is not adjusted when converting legacy saves
- **Location:** [saveload.cpp:1639](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/saveload.cpp#L1639), [scumm.h:1722](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/scumm.h#L1722), [korean.cpp:230-236](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/korean.cpp#L230-L236)
- **Failure scenario:** `_charsetBuffer` is `byte[512]`, and P6a depends on UTF-8 lines exceeding 255 bytes (real bundles: Zak 295 B, Indy3 257 B, Indy4 337 B). However, `saveload.cpp:1639` still syncs only **256 bytes** (`s.syncBytes(_charsetBuffer, 256, VER(8))`):
  1. Saving during any UTF-8 dialogue line `>= 256` bytes saves only bytes `0..255` with no NUL terminator (potentially cutting a 3-byte Hangul syllable mid-sequence). Loading overwrites bytes `0..255` while bytes `256..511` keep stale pre-load memory, so dialogue resumes into garbage/previous-session text.
  2. Loading a legacy CP949 save created mid-dialogue (`_charsetBufPos > 0`, e.g., paused at a `WAIT` code) converts `_charsetBuffer` from 2-byte CP949 to 3-byte UTF-8 without updating `_charsetBufPos`. On resume, `_charsetBufPos` points to the wrong byte offset (often mid-UTF-8 syllable), replaying prior text and emitting `'?'` refusal glyphs. Also `textIsUtf8(_charsetBuffer, _charsetBuffer + sizeof(_charsetBuffer))` scans into stale bytes `256..511` if bytes `0..255` lack a NUL.
- **Fix:** Zero `_charsetBuffer` before deserializing in `loadState()`. For UTF-8 saves, persist the remaining `_charsetBuffer[256..511]` (e.g., in the `TENC` trailer when `_haveMsg` is active) so long lines round-trip without truncation. In `convertLoadedText()`, convert `_charsetBuffer[0.._charsetBufPos)` and `_charsetBuffer[_charsetBufPos..256)` separately (or compute the converted prefix length) and update `_charsetBufPos` to the new UTF-8 offset.

### C3. 62 KS X 1001 syllables (`0xC2/0xC3 0xA1..0xBF`) are misclassified as UTF-8 Latin-1 in legacy saves and CP949 in-data text
- **Location:** [korean_text.cpp:119-136](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/korean_text.cpp#L119-L136) (`textIsUtf8`), [korean_text.cpp:181-198](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/korean_text.cpp#L181-L198) (`toEngineText`), [korean.cpp:221-224](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/korean.cpp#L221-L224) (`convertLoadedText`)
- **Failure scenario:** `textIsUtf8()` rejects 2-byte UTF-8 with `cp > 0xFF` (`0xC4..0xDF`), but still accepts `0xC2 0x80..0xBF` and `0xC3 0x80..0xBF` (`U+0080..U+00FF`). All 62 KS X 1001 syllables in `0xC2 0xA1..0xBF` (`짙 짚 짜 짝 ...`) and `0xC3 0xA1..0xBF` (`찼 창 찾 채 책 ... 처 척 천 철 첨 첫 청 체 ...`) have trail bytes matching `(b & 0xC0) == 0x80`:
  - `책` (`C3 A5`) reads as UTF-8 `U+00E5` (`å`), `창` (`C3 A2`) as `U+00E2` (`â`), `철창` (`C3 B2 C3 A2`) as `U+00F2 U+00E2` (`òâ`), `천` (`C3 B1`) as `U+00F1` (`ñ`).
  - Any CP949 object/verb/actor name or short line made solely of ASCII + those syllables (in MI1/DOTT in-data CP949 builds, or in a legacy CP949 save where `convertLoadedText()` already knows `saved != kSavedTextUtf8` yet still checks `textIsUtf8()`) is skipped by conversion and passed to `nextCJKChar()`, which returns `0x80..0xFF` as a game charset glyph — rendering Latin-1 garbage or causing an out-of-bounds charset read when `chr >= _numChars` (see I1).
- **Fix:**
  1. In `convertLoadedText()`, when `saved != kSavedTextUtf8`, the save is known to be CP949: call `cp949ToUtf8()` directly (skipping only pure-ASCII strings) instead of guessing with `textIsUtf8()` / `toEngineText()`.
  2. At the `translateText()` boundary, separate known-UTF-8 sources (loaded UTF-8 `.trs` translations, already-converted engine resources) from CP949 game-data strings instead of relying on byte-pattern guessing for `C2/C3 A1..BF`.

---

## Important

### I1. `getCharWidth()` breaks `koreanCharsetGlyph()` width measurement, and `U+0080..U+00FF` lacks `_numChars` bounds checks
- **Location:** [charset.cpp:469-480](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/charset.cpp#L469-L480), [charset.cpp:562-567](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/charset.cpp#L562-L567), [charset.cpp:672-674](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/charset.cpp#L672-L674), [charset.cpp:741-751](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/charset.cpp#L741-L751), [charset.cpp:960](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/charset.cpp#L960), [charset.cpp:1385](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/charset.cpp#L1385)
- **Failure scenario:**
  1. `getStringWidth()` and `addLinebreaks()` decode UTF-8 `U+0080..U+00FF` via `koreanCharsetGlyph()` and call `getCharWidth(g)` to measure the game charset glyph's width. However, both `CharsetRendererClassic::getCharWidth()` (`if (_vm->_useCJKMode && chr >= 0x80) return _vm->_2byteWidth / 2;`) and `CharsetRendererV3::getCharWidth()` (`if (_vm->_useCJKMode && (chr & 0x80)) spacing = _vm->_2byteWidth / 2;`) short-circuit before reading `_fontPtr` / `_widthTable`. In `CharsetRendererClassic`, `printChar(g)` advances by the true glyph width from `prepareDraw(g)`, so centering and wrapping disagree with rendered widths for charset glyphs (e.g., MI1's `0xFA`, MI2's `Papier-Mâché`).
  2. Neither `prepareDraw()` (`_fontPtr + chr * 4 + 4`), `CharsetRendererV3::printChar()` (`_fontPtr + chr * 8`), nor `getCharWidth()` checks `chr < _numChars`. Because `nextCJKChar()` returns any `U+0080..U+00FF` verbatim ([korean.cpp:126-127](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/korean.cpp#L126-L127)), any `U+0080..U+00FF` character in a game whose active charset has `< 256` entries (e.g., 128-glyph v2/v3 fonts) reads out of bounds past the font/offset table (or triggers `assert(charOffs < 0x14000)` in `prepareDraw()`).
- **Fix:** In `CharsetRendererClassic::getCharWidth()` and `CharsetRendererV3::getCharWidth()`, do not short-circuit `chr < 0x100` when `_vm->_koreanUtf8` is true, and add `if (chr >= _numChars)` bounds guards to `getCharWidth()`, `prepareDraw()`, `CharsetRendererV3::printChar()`, and `CharsetRendererV3::drawChar()`.

### I2. `getDialogCodePage()` still returns `Common::kWindows949` when `_koreanUtf8` is true
- **Location:** [string.cpp:2539-2542](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/string.cpp#L2539-L2542); callers [string.cpp:170](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/string.cpp#L170), [dialogs.cpp:587](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/dialogs.cpp#L587), [saveload.cpp:1033-1034](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/saveload.cpp#L1033-L1034), [charset.h:120](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/charset.h#L120)
- **Failure scenario:** Because engine text is now UTF-8 and `encoding.dat` is not required:
  1. `showMessageDialog()` (with ScummVM GUI) passes UTF-8 `buf` into `U32String((char *)buf, getDialogCodePage())`, decoding UTF-8 bytes as CP949 -> mojibake or all `?` without `encoding.dat`.
  2. `InfoDialog::queryResString()` decodes UTF-8 `rtString` resources as CP949 -> mojibake/all `?`.
  3. `ScummEngine::getSavegameName()` encodes UTF-8 savegame names to CP949 (`temp.encode(getDialogCodePage())`), turning every Korean character into `?` without `encoding.dat` (and producing CP949 for an engine that expects UTF-8).
  4. TTS (`CharsetRenderer::convertText()`) decodes UTF-8 as CP949.
- **Fix:** Return `_koreanUtf8 ? Common::kUtf8 : Common::kWindows949` in `ScummEngine::getDialogCodePage()` for `Common::KO_KOR`.

### I3. `detectLanguage()` looks up `ko/ko.trs` and `ko/korean.trs` case-sensitively
- **Location:** [detection_internal.h:238-240](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/detection_internal.h#L238-L240)
- **Failure scenario:** `langDir.getChild("ko.trs").exists() || langDir.getChild("korean.trs").exists()` is case-sensitive on POSIX/Linux filesystems. Placing an existing uppercase `KOREAN.TRS` or `KO.TRS` inside `ko/` fails Korean detection on Linux (the game is detected as English), even though `SearchMan` opens files case-insensitively. (Compare `SYSTM/GAME.TRS` at lines 249–252 immediately below.)
- **Fix:** Enumerate `langDir.getChildren(subList, Common::FSNode::kListFilesOnly)` and check `searchFSNode(subList, "ko.trs", langFile) || searchFSNode(subList, "korean.trs", langFile)`.

### I4. SCUMM v2 verb click hitboxes use UTF-8 byte length, causing overlapping hitboxes
- **Location:** [verbs.cpp:1186](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/verbs.cpp#L1186)
- **Failure scenario:** For `_game.version <= 2`, `drawVerb()` sets `vs->curRect.right = pixelXOffset + MIN<int>(vs->curRect.left + (getResourceSize(rtVerb, verb) - 1) * 8, _screenWidth)`. With UTF-8 verbs stored in `rtVerb`, each Hangul syllable is 3 bytes: a 3-syllable verb (`향하기`, 9 bytes) gets a 72 px hitbox (visual width ~27 px) and a 4-syllable verb (`잠금 풀기`, 13 bytes) gets a 104 px hitbox, extending into the next verb column in MM/Zak. Because `findVerbAtPos()` checks slots in reverse order, clicking a verb in the adjacent column can activate the wrong verb.
- **Fix:** When `_koreanUtf8` is true, compute `vs->curRect.right` from `_charset->_str.right` (as v3+ already does) instead of `(getResourceSize(rtVerb, verb) - 1) * 8`.

### I5. `addLinebreaks()` Korean mid-word break does not increment `strLength` and has no buffer capacity check
- **Location:** [charset.cpp:605](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/charset.cpp#L605), [charset.cpp:726-733](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/charset.cpp#L726-L733)
- **Failure scenario:** `strLength` is computed once before the loop, but each Korean mid-word break inserts a `0x0D` byte via `memmove(breakPtr + 1, breakPtr, strLength - lastKoreanLineBreak + 1)` without incrementing `strLength`. On the 2nd Korean line break in a dialogue line, `memmove` leaves out the trailing NUL and final byte; on the 3rd break, 2 bytes are dropped — slicing the final 3-byte UTF-8 syllable (producing invalid UTF-8 / `'?'` or reading past the dropped NUL). Additionally, inserting `0x0D` has no check against `_charsetBuffer`'s 512-byte limit, risking a buffer overrun on long UTF-8 lines.
- **Fix:** Increment `strLength++` on each inserted `0x0D` and enforce the destination buffer capacity before `memmove` (stopping line-break insertion if `strLength + 1 >= maxLen`).

### I6. `redrawV2Inventory()` truncates UTF-8 renamed objects at byte 18, slicing multi-byte characters
- **Location:** [verbs.cpp:480-489](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/verbs.cpp#L480-L489)
- **Failure scenario:** Contrary to [P6a-notes.md:152](file:///home/thkim/work/tmp/scummvm-review-p6a/docs/korean/plans/P6a-notes.md#L152), `getObjOrActorName(obj)` returns a UTF-8 `rtObjectName` whenever a v2 script renames an object via `o5_setObjectName` ([script_v2.cpp:146](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/script_v2.cpp#L146)). `strncpy((char *)msg, (const char *)tmp, 18)` cuts at byte 18 regardless of UTF-8 character boundaries (e.g., 1 ASCII space + 6 Hangul syllables = 19 bytes gets cut mid-character), drawing a trailing `'?'` refusal glyph.
- **Fix:** When `_koreanUtf8` is active, copy with character-boundary truncation (e.g., `toEngineText(tmp, resStrLen(tmp) + 1, msg, maxChars + 1)` or truncate after translation by display width rather than raw byte count).

### I7. `scummEscapeLength()` treats `0xFE` as a 2-/4-byte escape, disagreeing with `resStrLen()` and `convertMessageToString()`
- **Location:** [korean_text.cpp:50-57](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/korean_text.cpp#L50-L57) vs [script.cpp:1567](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/script.cpp#L1567) (`resStrLen`) and [string.cpp:1628](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/string.cpp#L1628) (`convertMessageToString`)
- **Failure scenario:** `scummEscapeLength()` treats both `0xFF` and `0xFE` as escapes (2 or 4 bytes), whereas `resStrLen()` and `convertMessageToString()` only recognize `0xFF`. If a translated string contains `0xFE` with a `0x00` argument byte, `copyTranslatedText()` passes `resStrLen(src) + 1` to `toEngineText()`, truncating the string at the `0x00`. Conversely, if CP949 in-data text contains a stray `0xFE` charset glyph byte followed by Hangul, `scummEscapeLength()` swallows the next 1–3 bytes as an escape and leaves the following CP949 bytes unconverted.
- **Fix:** Align `scummEscapeLength()` with `resStrLen()` / `convertMessageToString()` (only `0xFF` is a script/bundle escape, or if `0xFE` truly appears with arguments in bundles, handle `0xFE` consistently in `resStrLen()` as well).

---

## Minor

### M1. `drawKoreanSentence()` omits C64 1-pixel X/Y offsets
- **Location:** [korean.cpp:78-85](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/korean.cpp#L78-L85) vs [string.cpp:932-933](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/string.cpp#L932-L933)
- **Failure scenario:** `koreanSentenceApplies()` allows C64 (`platform != kPlatformNES`), and line 81 sets C64 text color, but `_string[2].xpos/ypos` and `sentenceline` omit the 1 px `pixelXOffset`/`pixelYOffset` used in `ScummEngine_v2::drawSentence()`, misaligning the sentence bar by 1 px on C64.
- **Fix:** Apply `pixelXOffset` and `pixelYOffset` matching `ScummEngine_v2::drawSentence()`.

### M2. `koreanCharStart()` walks back into the previous Hangul syllable on a stray continuation byte
- **Location:** [korean.cpp:155-162](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/korean.cpp#L155-L162), [charset.cpp:675-696](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/charset.cpp#L675-L696)
- **Failure scenario:** If a stray `0x80..0xBF` byte follows a Hangul syllable in `addLinebreaks()`, `cjkTrailBytes()` returns 0, then `koreanCharStart(str, pos)` scans backwards past the stray byte into the preceding Hangul syllable's lead byte, re-testing the previous syllable as the break candidate.
- **Fix:** Record `const int cs = pos - 1;` before advancing `pos += _vm->cjkTrailBytes(...)` instead of scanning backward after the fact.

### M3. `koreanCharLength()` does not validate UTF-8 continuation bytes
- **Location:** [korean_sentence.cpp:130-139](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/korean_sentence.cpp#L130-L139)
- **Failure scenario:** `koreanCharLength()` returns 2/3/4 solely from the first byte (`>= 0xC0 / 0xE0 / 0xF0`), so an invalid lead byte or 2-byte escape (`0xFF 0x01`) skips up to 4 bytes and consumes adjacent characters.
- **Fix:** Delegate to `decodeUtf8()` (already included via `korean_text.h`), which validates continuation bytes and returns `len = 1` on invalid bytes.

### M4. `tools/korean/trscheck.py` is O(N² log N) and `census` ignores invalid UTF-8 in UTF-8 bundles
- **Location:** [trscheck.py:32-45](file:///home/thkim/work/tmp/scummvm-review-p6a/tools/korean/trscheck.py#L32-L45), [trscheck.py:167-185](file:///home/thkim/work/tmp/scummvm-review-p6a/tools/korean/trscheck.py#L167-L185), [trscheck.py:231-235](file:///home/thkim/work/tmp/scummvm-review-p6a/tools/korean/trscheck.py#L231-L235)
- **Failure scenario:** `Bundle.text()` calls `self.starts()` (sorting all entry offsets) on every line lookup, making large 5,500-line bundles take ~30M operations. Also, `census()` on a UTF-8 bundle counts invalid UTF-8 bytes under `out_of_font` rather than `unreadable`, returning exit code `0` on corrupt UTF-8 bundles.
- **Fix:** Cache `self._starts` in `Bundle.__init__`, and count invalid UTF-8 sequences as `unreadable` (exit 1) in `census()`.

### M5. Test gaps and documentation drift
- **Location:** [korean_sentence.h:48-55](file:///home/thkim/work/tmp/scummvm-review-p6a/test/engines/scumm/korean_sentence.h#L48-L55), [korean_text.h:65-78](file:///home/thkim/work/tmp/scummvm-review-p6a/test/engines/scumm/korean_text.h#L65-L78), [korean_trs_data.h:69-72,113-122](file:///home/thkim/work/tmp/scummvm-review-p6a/test/engines/scumm/korean_trs_data.h#L69-L72), [PATCHES.md](file:///home/thkim/work/tmp/scummvm-review-p6a/docs/korean/PATCHES.md)
- **Details:**
  - `test_fit_never_cuts_a_character()` has a stale comment (`4 px a byte; 17 bytes = 68 px; two characters` vs actual `2 px/byte`, 25 bytes, 4 characters) and never exercises `fitKoreanSentence()`'s fallback character-truncation loop ([korean_sentence.cpp:194-206](file:///home/thkim/work/tmp/scummvm-review-p6a/engines/scumm/korean_sentence.cpp#L194-L206)), nor `prep` values `0..3` or `bIsActor == false`.
  - `ScummKoreanTextTestSuite` does not test `C2/C3 A1..BF` CP949 collisions (see C3) or `toEngineText()` edge cases (`dstSize <= 1`, `dst == src` overlap).
  - `ScummKoreanTrsDataTestSuite` does not assert that `cp.originals` remain byte-identical to `u8.originals`, nor does its `text()` helper bound by the next entry offset as `trs_bundle.cpp` does.
  - `docs/korean/PATCHES.md` was not updated with the P6a upstream diff entries (and the upstream diff adds 112 lines vs the plan's 100-line budget noted in `P6a-notes.md`).
- **Fix:** Expand unit tests for the uncovered paths (especially C1–C3, I1–I2, and `fitKoreanSentence` fallback), verify `cp.originals` in `korean_trs_data.h`, and update `docs/korean/PATCHES.md`.

VERDICT: REJECT
