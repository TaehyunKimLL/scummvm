# P5 review (Claude lane): AGI Korean, LSL1 AGI first

Reviewed: `ba0eecee2c4..21d05283761` (25 commits), read-only checkout `~/work/tmp/scummvm-review-p5`. Independent of the
agy lane (its files not read). Scratch: `~/work/tmp/p5-review-claude/` (`asan/` build, `harness/`, `bi/` + `src/`
intermediate builds, `runs/`).

## Verdict

**Sound to merge into ko-2026.4 and to build P7 on, after one required fix (M1).** M1 is a one-line string change.
The minor findings can follow as cards. I found no memory error, no regression on the English byte path, and no
off-by-one in the save-name budget. Every intermediate commit builds.

Required before merge:
1. **M1**: make the Korean `_textSaveGameEnterDescription` wrap to at least two lines, so the input row is inside the
   window.

Recommended (cards are fine): m1 (cap `unknownPos`), m2 (do not re-encode a UTF-8 v11 table as CP850), m3
(`text_encoding=utf8` in the application domain switches every AGI game).

## What was run

| Check | Result |
|---|---|
| (a) ASan+UBSan AGI build, out of tree (`configure --disable-all-engines --enable-engine=agi --enable-debug --disable-optimizations --enable-asan --enable-ubsan`; `-fno-sanitize=vptr` has to be **appended** to `CXXFLAGS` in `config.mk`. Passed through the environment, it comes before `-fsanitize=undefined`, which turns vptr back on, and the runner link fails on `typeinfo for Agi::AgiBase`) | `test/runner`: **702 tests OK**, 0 sanitizer lines; real-data said() gate ran (no skip warning) and asserts **778 / 691 / 23 / 20 / 7 / 6 / 31** |
| (b) Own randomized differential harness `harness/review_p5.h`, `review_words.h` (15 tests, exact-size heap blocks, under ASan+UBSan) | 14 pass; 1 fails as expected (m2). Details below |
| (c) Headless play (`runs/ko2`, `ko3`, `asan2`, `gmm`, `en-*`, `sv-*`, `ew-*`) | composer `술집을 봐` -> Korean reply; OS-IME `술집을 봐` (`hangul_start=false`) -> same reply; Tab, Esc menu, F5/F7 in Korean; 0 ASan lines; only the 3 known UBSan lines in upstream `global.cpp:111/115/134` |
| Save names (composer and OS-IME) | 9 syllables kept (27 B); the 10th and 11th refused (composer: `셍` absorbs the `ㅇ`, then `요`/`가` refused; IME: `안녕하세요안녕하세요가` -> `안녕하세요안녕하세`, 27 B); `안녕하세요안녕하세` + `abc` -> `...ab` = **29 B** kept, `c` refused; version byte 11, `AGKO` trailer; bytes after the NUL are zero |
| GMM save dialog (`originalsaveload=false`), IME `안녕하세요안녕하세요가` | saved `안녕하세요안녕하세` (27 B, cut on a character by the 28-byte clip), v11 + AGKO |
| Cross saves | base (unmodified, my own build of `ba0eecee2c4`) English save -> listed and restored in the Korean target, Korean command works after; new-build English save listed; Korean save (`...ab`) listed (name garbled in its font) and restored by the base build, `look` works |
| English identity (base vs final, same key script, `xwd` frames) | title -> Lefty's, Esc menu, Tab inventory, `look` reply, F5 slot list: **bbox None** x5; save dialog frame differs only in the neon sign; English save files the same length (44495) with no trailer, differing only in the thumbnail and the date/time; 0 Korean log lines |
| Detection (`--detect`, base vs final) | `lsl1en`, fan game `fan1`: identical; `lsl1ko3`, `kq4agi`, `lsl1ko3bad`: one extra `.../Korean` line |
| (e) Every commit `ba0eecee2c4..21d05283761` built in order (scratch clone, out of tree) | `scummvm` and `test/runner` rc 0 for all 25, **0 warnings** (runners built, not run, for the intermediate states) |

Harness results (`harness/run2.out`, `run3.out`):
- `decode`/`cellLen`/`toCells`/`isValidUtf8` against a strict RFC 3629 decoder, 200000 random byte strings: 0 mismatches.
- `clipBytes`/`clipCells` against a model (maximal prefix of whole characters), 100000: 0.
- `copyDescription` against `strncpy(dst, src, 31); dst[30] = 0` (byte path, stale destination) and against the UTF-8
  model (whole characters, zero after), 100000: 0.
- `TextCells::wordWrap` byte path against **verbatim upstream `stringWordWrap`**, 200000 texts (bytes >= 0x80, LF,
  over-long words, width 1..40): 0 differences (text, width, height).
- `wordWrap` UTF-8 (hangul=word) against my own cell transliteration of upstream's loop, 200000 texts incl. 2/3/4-byte
  and malformed sequences: 0. `hangul=any`: every line <= width, output valid UTF-8, content kept when not truncated.
  The only "violation" is height 21, which upstream's byte loop also returns once it breaks at 20.
- `savedGameLine` against verbatim upstream `createSavedGameDisplayText` (byte path); in UTF-8, 31 cells and <= 124 B: 0.
- `agiStringsEqual` against verbatim upstream `testCompareStrings` (incl. the 40-byte strlcpy), 200000: 0.
- `saidMatches` against upstream `testSaid`'s loop (incl. the read one past the consumed words), 300000: 0.
- AGKO trailer: round trip of 3000 random tables; truncation at every length -> never `Read`, table untouched;
  60000 random mutations -> read 9119 / broken 50359 / none 522, every accepted slot < stride and valid UTF-8: 0 errors.
- `legacySlot`, `convertLegacyStrings` bounds (cells <= 39, bytes < stride, valid UTF-8): 0. The re-encoding case fails (m2).
- `HangulLine` random keys and backspaces on an exact `maxCells + 1` heap array: never past `maxCells`, always NUL-terminated.
- `TextOverlay::loadFromStream` on 30000 hostile files (NUL, bad UTF-8, huge numbers, BOM, CRLF, header pieces); plus
  BOM + CRLF + duplicate (later wins), `m256:1`, `m1:0`, missing TAB, bad UTF-8 and an escape case; a wrong
  fingerprint -> refused and `isPresent()` false: all as documented.

## Findings

### M1 (major): the Korean save-name prompt leaves the typed name and a black bar on the room after every in-game save

`engines/agi/systemui.cpp:159` (`_textSaveGameEnterDescription` for `KO_KOR`) together with `askForSaveGameDescription()`
(`systemui.cpp:379-412`: `drawMessageBox(text, 0, 31, true)`, then `charPos_SetInsideWindow(3, 0)` at 391 and
`clearBlockInsideWindow(3, 0, 31, 0)`).

The input line goes on window row 3. Upstream's text, `"How would you like to describe this saved game?\n\n"`, wraps at
31 cells to 2 lines + 2 blank = 4 rows, so row 3 is inside the window. The Russian string even breaks it by hand.
`"저장할 게임에 붙일 설명을 입력하세요.\n\n"` is 21 cells: 1 line + 2 blank = 3 rows. The input row lands on the box's
bottom border, outside the saved background, so closing the window does not erase it.

Reproduction: `runs/ko3`, `originalsaveload=true`, target `lsl1-ko`. Keys: Return, Return, F5, Return, composer `rkskek`
(`가나다`), Return, Return.
- `d1.png`: the black input bar sits on the box's lower border.
- `d2.png`, the verification box: `가나다` and black bar pieces show left and right of the box.
- `d3.png`, back in the game after the save: `가나다` and the bar are still drawn over Lefty's front at the input row.

The same script on the English target (`runs/ew-base`, `ew-new`) leaves nothing. The screenshots `n12.png`
(`runs/ko2`) and `z5.png` (`runs/asan2`) show the same.

Fix: two lines, e.g. `"저장할 게임에 붙일\n설명을 입력하세요.\n\n"` (or any text of at least 2 wrapped lines). Better
still, make `askForSaveGameDescription` place the input row after the wrapped text height. Re-check `d3` afterwards.

### m1 (minor): `unknownPos` past the candidate length is reachable from the prompt

`engines/agi/words.cpp` `Words::analyze()`: `out.unknownPos = typed.size()` is set before `typed.resize(MAX_WORDS)`.
`applyParse()` then writes it into `VM_VAR_WORD_NOT_FOUND`.

P5-notes says this "can exceed ... Unreachable from the 40-cell prompt". That is wrong: one syllable can give two words.
`harness/review_words.h` (real LSL1 tables) reports `best words/cell 켜 = 1.00`. The 37-cell line `켜 x18 + 뽁` (fits
the 38-cell prompt) gives `unknownPos 37, cand0 size 20`. The game then sees an unknown-word number of 37 with 20 ego
words. LSL1's `m0:1..8` (`%m10%w1?` ... `%w8?`) never fire, so a typo at the end of a long line gets no "what's X?"
reply. No memory error: `%wN` takes its N from the script.

Random 38-cell lines from walkthrough tokens did not hit it (0 of 50000); the dense line does.

Fix: when the unknown word lies past `MAX_WORDS`, keep it as word `MAX_WORDS` (replace the last kept word) and set
`unknownPos = MAX_WORDS`. Also correct the note and the fuzz test's stated bound.

### m2 (minor): a v11 table written in UTF-8 is converted again as CP850 when the trailer is broken or absent

`engines/agi/saveload.cpp:760-767` with `saved_strings.cpp` `convertLegacyStrings()`.

On `kSavedStringsBroken`, and on `None` for a Korean save that went through an English or unmodified build and was
saved again, the slots are `legacySlot()` UTF-8. `hasHighBytes` is true, so they are decoded as CP850.
Harness `test_legacy_slot_and_broken_trailer_conversion`: `켄이 보냈다` becomes `ý╝äýØ┤ Ù│┤ÙâêÙïñ`.

Fix: in `convertLegacyStrings` leave a slot alone when `TextCells::isValidUtf8(slot)` (CP850 text with high bytes is
practically never valid UTF-8), or skip the conversion on `kSavedStringsBroken`.

### m3 (minor): `text_encoding=utf8` in the application domain switches every AGI game, English and fan games included

`engines/agi/textmode.cpp:57-58`: `enc == "utf8"` gives `kUtf8` with no translation present. DESIGN section 7 says the
global "Korean text" widget writes these keys to the application domain.

Reproduction `runs/enc`: `[scummvm] text_encoding=utf8`, target `lsl1` on the plain English folder `lsl1en`. The log
shows `Hi-res text: using data:hires_text/maps/korean-default.map`, i.e. UTF-8 mode, the 640x400 hi-res text path,
AGKO trailers on saves and the Korean parser path for any high byte. A Russian, Hebrew or French fan game would have
its 0x80-0xFF glyphs converted as CP850 and drawn as `?`. This breaks "English targets behave byte-identically" under a
global setting.

Fix: in AGI, honour `utf8` only for the requested language's translation folder or table, or only when the key is in
the game domain.

### Nits
- `systemui.cpp:409-410` (line 410): the comment says "held to that, 10 syllables". Since F1 it is 29 bytes, 9 syllables.
- `saveload.cpp:812`: the GMM path clips to 28 bytes, the in-game field allows 29. Both are safe, but they differ.
- `keyboard.cpp:147` `AGI_KEY_CODEPOINT | event.kbd.ascii` and `text.cpp:879/1206` `newKey & 0xFFFF`: an IME character
  in U+10000..U+1FFFF is folded to U+0000..U+FFFF (a wrong character). One at U+20000+ becomes a key >=
  `AGI_KEY_HANGUL_TOGGLE` and is dropped. Hangul is unaffected. Clamp to BMP or drop anything above U+FFFF explicitly.
- `op_cmd.cpp:2108` `set.cursor.char` takes the first byte of the (now UTF-8) message. A game with a non-ASCII cursor
  character would get a lead byte. Seen by reading only; I found no affected game, and LSL1 uses `_`.
- `console.cpp` `koverlay` and `agi.cpp` `rawLogicMessageCount()` read the message count and offsets of a raw logic
  without checking against the resource size. Debugger only.
- Upstream footprint: about 250 lines of Korean parsing inside `words.cpp` and about 190 of console commands inside
  `console.cpp` could live in new files (`korparse.cpp` already exists) to cut rebase surface. `PATCHES.md`'s `ko/04-agi`
  rows match the touched files.
- Commits b3 (`36edd1210b9`) through d1 (`d955881bea7`) still pass `(char *)_text->_inputString` (now `uint32[]`) in
  `askForSaveGameDescription` (`systemui.cpp:426/431` at those commits). Every target, English included, saves only the
  first character as its name in those states. It is fixed in d2, but it is a bisect trap; the implementer's record
  already says so.
- The Korean keymap (`metaengine.cpp`) also takes `S+SPACE`, `RALT`, `F12` and `SCROLLOCK` for every `language=ko`
  target, even when `_hangulInput` ends up false (no table). It is SCI's precedent and an owner choice; noted only.

### Checked and found sound
- The F1 budget is right: `SAVEDGAME_DESCRIPTION_BYTES` = 29 for composer, IME and mixed input. `saveGame()` clips on
  a character before `strlcpy(..., 30)`.
- F2: `copyDescription` equals upstream `strncpy` + terminator on the byte path.
- The `set.simple`, menu cut (`addMenu` loop rewrite is equivalent), `testSaid` rewrite, `testCompareStrings` rewrite,
  `promptEchoLine` loop guard, `stringSet` and `readSavedGameSlots` are equivalent to upstream on the byte path; the
  menu cut is equivalent by reading, the rest by differential test.
- The hooks are inert for non-UTF-8 targets: keyboard OS-IME rule, `processAGIEvents` wide keys, `drawCharacterOnDisplay`
  (`_hiresText` null), `initVideo`, `hangulSetup`, the AGKO write and read, `getLanguage`, detection (no overlay -> no
  change), keymap (`language=ko` only). The exception is m3.
- The AGKO reader is all-or-nothing on hostile or truncated input. Upstream ignores the trailer (the base build loads a
  Korean save). Version-11 saves without a trailer load.
- The stencils are clipped inside the 16x16 cell (`drawChar` with `&clip`). Cell positions are the same as AGI's font
  cells. The preedit is inverted through `charAttrib`.
