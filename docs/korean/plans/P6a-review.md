# P6a review, Claude lane (independent)

Reviewed: `4211b5ac538..0c6d8e45ced` (SCUMM S1, Korean text as UTF-8 inside the engine), read-only checkout
`~/work/tmp/scummvm-review-p6a`. The other lanes' outputs were not read. Line numbers are the checkout's.

Scratch (all mine, nothing in a worktree): `~/work/tmp/p6a-review-claude/`
- `build/`: out-of-tree SCUMM-only build of the checkout, `--enable-asan` plus `-fsanitize=undefined`
  (`ScummVM 2026.4.011111-g0c6d8e45ced`, clean HEAD).
- `h/fuzz.cpp` (+ `build.sh`): differential/hostile harness over `korean_text.cpp`, `trs_bundle.cpp`,
  `ksx1001_table.cpp`, `korean_sentence.cpp` compiled from the checkout under ASan+UBSan, plus verbatim copies of the
  `korean.cpp` helpers. `h/enc.cpp`: CP949 codec behaviour without `encoding.dat`.
- `amb.py`, `endscan.py`, `gamescan.py`, `blkscan2.py`, `blkend.py`, `inv.py`: real-data scans; `runs/`: game runs.

## Verdict

**Sound to merge into ko-2026.4 and to build S2/DOS on, after one required fix (M1).** No blocker. The converters are
memory-safe under hostile input (no write past the buffer, none past the terminator, output always engine UTF-8), the
KS X 1001 table is exact, the hooks are inert for non-Korean targets and for v7/v8, and the real bundles convert
correctly. M1 is the one place where the "UTF-8 inside" decision was not carried through, and it is visible in play
under P6a's own acceptance condition (no `encoding.dat`). The minors are cheap; m1-m3 should go in before S2 because
S2 (DOS) inherits `toEngineText()` as its only boundary.

Required: **M1**. Recommended before S2: m1, m2, m3, m4. The rest at leisure.

## Findings

### M1 (major): `getDialogCodePage()` still says CP949 for a UTF-8 engine; Korean save names show as `??`

- `engines/scumm/string.cpp:2541` returns `Common::kWindows949` for `KO_KOR`, but for v1-v6 the engine text is now
  UTF-8. Every consumer decodes or encodes engine text with it:
  - `saveload.cpp:1034` `getSavegameName()`: the UTF-8 description of a save is re-encoded to CP949 for the original
    GUI's save/load list (`gfx_gui.cpp:2286`, `script_v4.cpp:259`). Without `encoding.dat` `U32String::encode(kWindows949)`
    gives `?` per character (`h/enc`: `encode 949: 3F 3F 20 6F 6B` for "한글 ok"). With it, the CP949 bytes come back through
    `toEngineText()` and hit the C2/C3 ambiguity (a save named 책 or 창 shows `å`/`â`).
  - `dialogs.cpp:587` `InfoDialog::queryResString()` (pause, restart, quit texts taken from game strings) and
    `string.cpp:170` `showMessageDialog()` (print-to-dialog, `original_gui=false`): UTF-8 bytes decoded as CP949
    (`h/enc`: `utf8-as-949: U+FFFD U+FFFD U+FFFD`). With the current data the pause text came out English (static
    fallback; `runs/mi2gui/pause2.png`, `runs/mi1gui/pause.png`), so this sub-path is UNVERIFIED in play.
  - `saveload.cpp:355` `copyHeapSaveGameToFile()` decodes a name with it too.
- Reproduction (in play, default `original_gui`, no `encoding.dat`): autosave of this build renamed to UTF-8 "한글 저장"
  as slot 2 (`runs/savename/monkey2-ko.s02`), `scummvm -x 2 monkey2-ko` (mi2cp), F5, Load:
  `runs/savename/scummvm-monkey2-ko-00001.png` lists **`2. ?? ??`**. Log: `State loaded`, 0 sanitizer reports.
- Fix (one line): `case Common::KO_KOR: return _koreanUtf8 ? Common::kUtf8 : Common::kWindows949;` Then
  `getSavegameName()` keeps UTF-8 (drawn by the engine path unchanged), the dialogs decode correctly, and FT/v7 keep CP949.

### m1 (minor): an escape clipped at the end is copied, so the result's NUL becomes an escape argument

- `korean_text.cpp:51-57` clips an escape to `end`; `cp949ToUtf8()` copies the clipped escape and `toEngineText()`
  (`:181`) keeps it. Every later reader (`resStrLen()`, `convertMessageToString()`, `scummTextEnd()`) then takes the
  terminating NUL as the escape's argument and reads past it, e.g. `resource.cpp` `loadPtrToResource()`
  `len = resStrLen(translateBuffer) + 1` over a 512-byte stack buffer whose tail is uninitialised.
- Harness: 300000 hostile strings, **20235 idempotence failures, every one of them "second pass = first pass + the
  swallowed NUL"** (e.g. in `34 FF` -> `34 FF`, again -> `34 FF 00`). No other idempotence failure exists.
- How real text gets there: `scummTextEnd()` is not pair-aware (next item), so CP949 `<lead> FE x` followed by a real
  `FF` escape with a 0 argument ends the text inside that escape (`B0 FE 41 FF 0A 00 00 ..` -> output
  `걘 A FF 0A` + NUL). Not present in the scanned data (below), so latent.
- Fix: in `cp949ToUtf8()` and in the cut loop of `toEngineText()`, drop an escape whose full length does not fit before
  `end` (treat it as the end of the text). That also makes `toEngineText()` idempotent on all inputs.

### m2 (minor): `scummTextEnd()` reads CP949 trail `FE` as an escape; 4 real MI1 strings overrun their NUL

- `scummTextEnd()`/`scummEscapeLength()` (`korean_text.cpp:50-70`) see `0xFE` everywhere, but in CP949 a lead `B0-C8`
  takes a trail `FE` (25 syllables, e.g. 숭 = `BC FE`). Then `FE <next> <x> <y>` is skipped as one escape and can swallow
  the terminator.
- Real data (`blkend.py`, script/object blocks of the MI1 floppy): **DISK03 1, DISK04 3 strings where the escape-aware
  end runs past the true NUL**: `bf f8 bc fe c0 cc 00 ..` (원숭이), `b4 c0 bd bc c7 d1 20 b3 ce ba fe c1 f6 00 ..`. They
  survive only because `copyTranslatedText()` passes `srcMax = resStrLen() + 1` (which ignores `FE`) and
  `cp949ToUtf8()` stops at the real NUL. The CP949 bundles have 10 such translations (MI1 UTE 4, MI2cp 3, Zak 3,
  `endscan.py`); there the clip at the next entry saves them: the engine rule equals a pair-aware end on every real
  translation (only MM's deliberate `sound FF 04 C8` differs, byte-identical output).
- Fix: give the CP949 side its own end: in `cp949ToUtf8()` find the end itself (a lead `B0-D0` takes the next byte before
  the escape test), and use that in `toEngineText()`'s non-UTF-8 branch and in `convertTrsBodyToUtf8()`. With m1 this
  closes the class.

### m3 (minor): one truncated or stray byte turns the whole string into CP949 mojibake

- `toEngineText()` (`korean_text.cpp:188`) judges the whole string: valid UTF-8 or else all of it is CP949. A UTF-8 text
  with one bad byte, or cut inside a character, is reinterpreted completely.
- Real data: the MI1 UTE **UTF-8** pack (`mi1ute-u`, `mi1ute-win`) line 334 `세인트\xFA키츠(St.\xFAKitts)` (raw 0xFA in a
  BOM bundle; `amb.py`: "UTF8 bundle line judged NOT utf8: 1"). Harness: engine output
  `C3 AC C2 84 EB AA 84 C2 9D EB AA 85 ...` = mojibake for the whole line. The notes say "the engine gives U+00FA": true
  only for the CP949 bundle (that is what `korean_trs_data.h` compares); with the UTF-8 pack the line is garbled.
- Truncations before the boundary that UTF-8 makes more likely: `verbs.cpp:486` v2 inventory `strncpy(msg, name, 18)` of a
  renamed object (rtObjectName holds translated UTF-8): 89 MM and 108 Zak translations fit 18 bytes in CP949 but not in
  UTF-8 (`inv.py`, e.g. `외계 점액 게임` 14 -> 20 bytes). Which of them are renamed inventory objects: UNVERIFIED.
- Fix: in `toEngineText()` accept a text that is valid UTF-8 apart from an incomplete final sequence (drop the tail), and
  read a lone invalid byte inside otherwise valid UTF-8 as U+0080..U+00FF instead of switching the whole string; in a BOM
  bundle never guess (its translations are UTF-8 by declaration). For the inventory, cut with
  `toEngineText(tmp, resStrLen(tmp) + 1, msg, maxChars + 1)` when `_koreanUtf8`. Also fix line 334 of the pack.

### m4 (minor): old CP949 saves are guessed, not converted; 19 real names come back as Latin-1

- `korean.cpp:221` `convertLoadedText()` skips a resource when `textIsUtf8()` is true, although a save without the
  trailer is known to hold CP949. The remaining ambiguity (rows C2/C3, trail A1-BF, 62 syllables) occurs in the real
  bundles (`amb.py`, CP949 translations whose high bytes all fall in it): **Loom 11 (`책`, `짚` x10), Indy4 4
  (`철창@@..`, `창?@@..` x2, `책@@..`), Indy3 3 (`책@@..`), Zak 1 (`책`)**. The `@`-padded ones are object names/verbs, which
  is exactly what a save stores (rtObjectName, rtVerb). Harness: `toEngineText(C3 B6 C3 A2 40 40)` returns it unchanged
  (`öâ@@`). Live play is not affected (bundles are converted unconditionally); only the one-time migration.
- In MI1/DOTT floppy game data no real text falls in the ambiguity (`blkscan2.py`: MI1 1 hit inside bytecode of an LS block
  (`... 08 00 c3 a3 00 03 40 ...`, not a print), DOTT 0 of 80 Korean runs).
- Fix: for `saved != kSavedTextUtf8`, convert with `cp949ToUtf8()` directly (escape-aware end as in m2), no
  `textIsUtf8()` test; and keep the old size: `createResource(type, idx, MAX(len + 1, size))` (a v5 string resource
  shrunk to its text length changes which `o5_stringOps` set/get-char indexes the script may use).

### m5 (minor): a message longer than 256 bytes is cut in a save

- `saveload.cpp:1639` `s.syncBytes(_charsetBuffer, 256, VER(8))` while the buffer is 512 and `_charsetBufPos`/`_haveMsg`
  are restored. UTF-8 makes such lines real: Zak 2 and Indy4 42 translations are >= 256 bytes (notes, a12). Saving while
  such a message is between pages restores 256 bytes plus whatever the running engine had; the cut can fall inside a
  character. Not memory-unsafe (a NUL always exists in the 512 bytes). In play: UNVERIFIED.
- `convertLoadedText()` (`korean.cpp:230-236`) converts an old save's `_charsetBuffer` but leaves `_charsetBufPos` as a
  CP949 offset (resumes mid-character for an old save loaded mid-message).
- Fix: put bytes 256..511 of `_charsetBuffer` into the TENC trailer (bump its byte to 2, accept 1); map
  `_charsetBufPos` through the conversion (count characters before it) or reset it to the start of the next line.

### m6 (minor): `ko/` detection is case-sensitive inside the folder

- `detection_internal.h:240` `langDir.getChild("ko.trs").exists() || ...("korean.trs")`: on POSIX `ko/KOREAN.TRS` (the
  packs' own spelling, all current bundles are uppercase `KOREAN.TRS`) is not detected, while the engine's lookup through
  `SearchMan.addSubDirectoryMatching(gameDataDir, "ko")` would find it. English games are unaffected (detection needs a
  `.trs` file). Fix: list the folder's children and use `searchFSNode()` for both names.

### n1 (nit): charset glyphs U+0080..U+00FF are measured as half a Hangul cell

- `getStringWidth()`/`addLinebreaks()` use `getCharWidth(g)`, which in CJK mode returns `_2byteWidth / 2` for `chr >= 0x80`
  (`charset.cpp:472`, `:744`), not the charset glyph's width that `printChar()` advances. Upstream measured such a byte as
  `_2byteWidth + 1` and skipped the next byte. Neither equals the drawn width; the comment in `cp949ToUtf8()` ("the 1x
  picture stays upstream's") is not exact for lines with such glyphs (MI1's 0xFA). Matches the notes' UNVERIFIED item.

### n2 (nit): upstream budget and records

- Lines added in upstream files: 108 in the eight `engines/scumm` files + 5 in `test/module.mk` = 113 (notes: 112, plan:
  100). Acceptable, but the `scumm.h` doc comments could move to `korean_text.h`/`korean.cpp`.
- `test/engines/scumm/trs_bundle.h` `test_unterminated_translation_keeps_an_empty_next_entry`: the comment says "an empty
  string at 5", the entry is at 7.
- Tests that would pass with a broken implementation: no test exercises the UTF-8 bundle through the boundary (m3 would
  have shown), no property test of idempotence (m1), nothing covers `getDialogCodePage()` (M1).

### Out of scope, pre-existing upstream (not counted)

- `charset.cpp:727` Korean line break inserts a byte with `memmove(breakPtr + 1, breakPtr, strLength - lastKoreanLineBreak + 1)`
  where `strLength` is computed once before the loop: at the second insertion the copy ends one byte short and the NUL is
  overwritten by the last character, so the line runs on into stale buffer bytes. Same in upstream and P6a (UTF-8 does
  not change the number of insertions); worth a separate upstreamable fix (`strLength++` after each insertion).

## What was checked and holds

- **KS X 1001 table**: regenerated from Python's codec (`cp949` and `euc_kr`, B0A1..C8FE): all 2350 entries equal, strictly
  ascending; `tools/korean/gen_ksx1001.py` output is byte-identical to `engines/scumm/ksx1001_table.cpp`.
- **Harness (ASan+UBSan)**, `h/fuzz`: `toEngineText()` 300000 hostile strings (escapes FF/FE with 0x00/0x80-0xFF arguments,
  C2/C3, CP949 leads, lone continuations, 3-byte leads, NULs; exact-size heap source so reads past `srcMax` would trap;
  exact-size destination filled with a canary): 0 sanitizer reports, 0 writes past the buffer, **0 writes past the
  terminator**, return always `<= dstSize - 1`, output always passes `textIsUtf8()`. Boundary cases: a 3-byte character and a
  4-byte escape straddling the limit are never split (`a가b` at sizes 2-4 keeps `a`; `a FF 0A 12 34 b` at 2-5 keeps `a`).
  `convertTrsBodyToUtf8()` 100000 random bodies (unterminated, truncated, NUL arguments): 0 reports, every moved offset
  at a string start, every translation terminated. `fitKoreanSentence()`/`koreanFinalOf()` 50000: 0 reports. Verbatim
  copies of `nextCJKChar`/`koreanCharsetGlyph`/`cjkTrailBytes`/`koreanCharStart`/`koreanBreakableAt`/`koreanCharBefore`
  walked over 300000 NUL-terminated strings as `getStringWidth()` does: never past the NUL, `nextCJKChar` and
  `cjkTrailBytes` agree on the length, `koreanCharStart` always inside `[0, pos)`.
- **Real bundles**: every CP949 translation's engine end equals a pair-aware end (m2 detail); no offset is shared between
  originals and translations; no UTF-8 bundle carries a 2-byte character above U+00FF (so the `textIsUtf8()` tightening
  of ea2997aa954 rejects no real UTF-8 text), the only rejected UTF-8 line is m3's stray 0xFA.
- **Inertness**: with `_koreanUtf8 == false` every helper reduces to upstream's expression (`nextCJKChar` = `c + b * 256`,
  `cjkTrailBytes` = 1, `koreanCharStart` = `pos - 2`, `koreanBreakableAt`/`koreanCharBefore` = the old `checkKSCode`
  tests at `pos-2/pos-1` and `pos-3/pos-2`, the hanging-mark guards at `pos-3/pos-4`), `copyTranslatedText` = the old
  `memcpy`, no trailer written, the trailer read is harmless (`size - pos < 5` -> unknown, nothing converted).
  `_koreanUtf8 = version < 7`: The Dig/FT/COMI untouched except FT's `get2byteCharPtr` now refuses an out-of-range index
  instead of reading outside the font (safer). `wrapSegaCDText` change is Sega CD only.
- **Particles**: the hard-coded CP949 particle pairs of `convertVerbMessage`/`convertStringMessage` (`C0 BB`, `B8 A6`, ...,
  `C7 E2 C7 D8`) never look like UTF-8 (leads C0/C7, continuation bytes B0-BF), so they are converted at the boundary.
- **Buffers**: `drawString` 512, `_charsetBuffer` 512 with `convertMessageToString`'s `error()` guard, `transBuf` 2048,
  `translateBuffer` 512, `verbs.cpp` 384, `decodeParseString` 512: longest UTF-8 translation 337 bytes (Indy4); no stack
  buffer in the text path that a real line overflows. The `korean.cpp` refusal glyph (static 512, w/h clamped to 32) and
  `convertLoadedText`'s `tmp` (`size * 2 + 4` >= worst-case 2x growth) are in bounds.
- **Runs** with the ASan+UBSan build of HEAD (`runs/`): MI2 UTF-8 pack (no conversion line, Korean menu,
  `runs/mi2/scummvm-mi2ko-00000.png`), MM Korean (`CP949 body converted to UTF-8, 50116 -> 58803 bytes`, Korean verbs,
  `runs/mm/scummvm-mmko-00001.png`), MI1 floppy (Korean credits, `runs/mi1gui/pause.png`), MI2cp with a TENC save: 0
  sanitizer reports, 0 assertions. **English MM V1** (`runs/mmen`): 0 log lines matching
  korean/utf-8/cp949/TENC/trs/glyph/bundle. `make test` in the ASan+UBSan build: **664 tests OK**, 0 `runtime error`
  (the two data tests warn and skip without their folders, as documented).
- Not attributed to P6a: with `original_gui=false` and no theme files the pause dialog crashed in
  `GUI::ThemeEngine::getStringWidth` (builtin theme without fonts in an out-of-tree build); with the theme path set it
  opened normally.
