# P3 external reviews: adjudication

Inputs: the Gemini "agy" lane (`scratch/review-lanes/scummvm-p3-agy/out.md`, findings 1-17, verdict REJECT) and the
opencode DeepSeek lane (`scratch/review-lanes/scummvm-p3-ocd/out.md`, finding I1, verdict APPROVE WITH CHANGES).
Code: read-only checkout `~/work/tmp/scummvm-review-p3` at `5741781806a` (end of P3). Every finding was checked against
that code, and against the branch head: P4 (`git diff 5741781806a..HEAD -- engines`, 9 files) changes none of the cited
lines except as noted per finding (kStrAt/kStrLen gain the answer-room view, `SegManager::strncpy` gains `growDynmem` for
a raw source, `KoreanParser` gains `englishLine()`); every finding below holds at HEAD too.

Evidence (scratch `~/work/tmp/p3-adjudicate/`, nothing in the worktree edited):

- `tests/adjudicate.h` + `build.sh`: a cxxtest runner built against `~/work/tmp/scummvm2026-p3build` (libsci/libgraphics
  of P3) with the checkout's headers; `out/runner` -> 4 tests OK (agy 1, 4, 5, 7; output quoted below).
- Headless KQ1 runs (`go1.sh`/`go2.sh`/`go3.sh`, Xvfb + xdotool, target `kq1-ko` on the P3 review pack
  `~/work/tmp/p3-review/pack`): `run/`, `run2/` (agy 3), `imeA/`, `keyA/`, `imeB/`, `imeD/` (agy 8, ocd I1).
- `dup.str` + `strcheck.py` (agy 16); `kq1dump.txt` (`k6bake.py dump` of the KQ1 `sci-ko.dat`).

## Verdicts

| Finding | Verdict | Severity | One line |
|---|---|---|---|
| agy 1 | CONFIRMED | major | `arrange()` runs on any sentence with a byte >= 0x80 in non-Korean games (German/French/Russian parser games) |
| agy 2 | PARTLY | minor | non-raw -> raw `strncpy` has no dest bound (upstream code, latent); length-bounded StrCpy can cut a code point; the rest duplicates P3 M3 |
| agy 3 | CONFIRMED | **blocker** | `그에게 문 안을 봐` / `그에게 문에게 줘` abort the game (`Array::pop_back` assert) |
| agy 4 | FALSE | nit | no read past the buffer: `decodeUtf8Char` stops at the NUL; only the byte cap of a list entry may show one whole extra syllable |
| agy 5 | CONFIRMED | minor | `shift=` is applied twice (drawn: shift=3 moves the glyph 6 hi-res px) |
| agy 6 | CONFIRMED | minor | `keyOf()` keys have no room: a room-only `sci-ko.str` entry is never found for a direct script string (no shipped table uses rooms) |
| agy 7 | CONFIRMED | minor | leading spaces + a word wider than the box: `getLongestUtf8()` returns 0 and `Size()`/`Box()` drop the rest of the text |
| agy 8 | CONFIRMED | major | `_lastOsImeKey` is sampled when events are drained, not when the script consumes them: a syllable committed together with Space/Enter is lost |
| agy 9 | PARTLY | minor | (a) over-wide line undoes every edit key: real, minor; (b) `adopt()` with `max < 3` / `_last` collision: nit; (c) cursor clamp in bytes: no effect (nit) |
| agy 10 | OUT OF SCOPE | nit | SCI32 + `kUtf8` + `KO_KOR` can call `setFont(1001)`; not reachable with any shipped pack or a default config |
| agy 11 | PARTLY | nit | the kFormat diagnostic's key lookup is wrong for a text-resource format; diagnostic only (partly recorded in P3-fixes) |
| agy 12 | CONFIRMED | minor | the `sci-korean` keymap swallows Shift/Ctrl+Space, RAlt, F12, ScrollLock also when the built-in composer is off or the game is CP949 |
| agy 13 | CONFIRMED | nit | `putFontPixel()` does not clear the hi-res plane; no game case observed |
| agy 14 | CONFIRMED | minor | `kDrawStatus` does not translate a direct script string (latent: KQ1 and the P4 games format their status lines) |
| agy 15 | CONFIRMED | nit | UTF-8 `kStrAt` read past the buffer returns 0, not `r_acc` + warning (only KQ5's 0xFFFF case relies on it) |
| agy 16 | CONFIRMED | nit | `strcheck.py` misses `05/007` vs `5/7` duplicates |
| agy 17 | CONFIRMED | nit | `text_encoding=utf8` on an `UNK_LANG` target looks for `sci-.str` (warning only) |
| ocd I1 | FALSE | - | the event that opens the prompt is not passed to `kEditControl` again; one `문` / one `ㅁ` in play |

## Per finding

### agy 1: Korean arrangement on non-Korean games (CONFIRMED, major)

`engines/sci/parser/vocabulary.cpp:766` computes `korean = _koreanVocab.isLoaded()` but uses it only for `joinPairs`
(773-776). `koreanWord`/`sawKorean` (811-814) are set from the bytes alone, and line 828 is
`if (sawKorean && !retval.empty()) KoreanParser(...).arrange(retval, roles, _koreanFallback);` with no `korean` test.
The tokenizer accepts bytes >= 0x80 for every game (upstream: German umlauts, Russian CP866 via `lowerCaseMap866`), so a
German, French, Spanish, Russian, Hebrew or upstream-CP949 parser game sends every sentence with such a byte through
`arrange()` (verb move, postposition move, `askAbout()`). HEAD: unchanged (the line is 845). The P3 review's upstream
audit ("arrange ... behind `_koreanVocab.isLoaded()`") was wrong on this point.

Reproduction (`test_agy1_arrangeOnNonKoreanSentence`, empty `KoreanVocabulary`, all roles none, the tokens of
`lege schlüssel auf tisch` = put key on table, verb/noun/prep/noun):

```
[agy1] typed order 10 20 30 40 -> said() sees 10 40 30 20
```

i.e. `said()` sees "put table on key"; a spec `put/key/on/table` no longer matches. Any verb-first sentence with
noun + preposition + noun is turned around, and a sentence whose last word is also a verb gets that word moved to the
front.

Fix: `if (korean && sawKorean && !retval.empty())` (and compute `koreanWord` only when `korean`). A unit test cannot
reach `tokenizeString()` without a `SciEngine`; add the gate plus a comment, and extend the upstream audit list.

### agy 2: seg_manager bounds (PARTLY, minor)

- Non-raw -> raw copy (`seg_manager.cpp:744-750` at P3, 771-778 at HEAD): `dest_r.raw[i] = c` until the source NUL
  with no `dest_r.maxSize` test. This is upstream code (identical at the P3 base `d7f47c73a07:747-753`) and unchanged by
  P4 (`growDynmem` covers a raw source only). With translations, Korean text that `kFormat` wrote into a stack or locals
  array (non-raw) and a script `StrCpy` into a smaller DYNMEM block or a script-segment string could write past the
  host allocation. No such script path was found in KQ1 or the P4 runs (the P4 ASan runs are clean), so it is latent.
  Fix (recommended): in that branch stop at `dest_r.maxSize - 1` and, in UTF-8 mode, go through
  `strncpy(dest, getString(src).c_str(), n)` so the code-point cut and `growDynmem` apply.
- `kStrCpy(dest, src, len)` with `0 <= len < strlen`: `strncpy(reg_t, const char*, n)` copies `n` bytes (C `strncpy`
  semantics, upstream), so a Korean source can be cut inside a code point. Real but rare (a script `StrCpy` with an
  explicit length over Korean text); the stray lead byte draws as a pass-through glyph. Fix (recommended): in UTF-8 mode
  round `n` down to `utf8PrevBoundary` when it cuts a sequence.
- `warning("... \"%s\" ...", src)` at 707: prints `src` to its NUL, which may lie past `n`; the source is a heap string
  that ends in a NUL within its segment. Debug noise only.
- `kStrCat`/`kStrCpy` through `strcpy_` instead of `strcpyBounded`: DUPLICATE of P3-review M3 / P3-fixes "M3: what is
  bounded" (stack/locals and script destinations bounded by the segment end only). A DYNMEM destination is already
  bounded by `strncpy`'s UTF-8 cut. `kStrCpy` copies the English heap string (translation happens at display), so the
  copy is as long as upstream unless the source is a Korean buffer.

### agy 3: arrangeTokens underflow (CONFIRMED, blocker)

`korean_parser.cpp:186-191`: a noun unit takes the article before it (`onlyClass(tok[start - 1], articleClass)`) without
the role test the noun branch has. KQ1's `sci-ko.dat` has `그` as an article (`그	1	0x0020`, `kq1dump.txt`), and the
stemmer gives `그에게` the role To. `arrangeTokens()` then moves the article to `phrases` (role -> "to"), but
`unitStart` of the next noun still points at it, so `plainUnits` counts a token that is not in `plain`:

- `그에게 문 안을 봐` (keys `rmdprp ans dksdmf qhk`): at `안`, `plain.size() - plainUnits` = 1 - 2 underflows, the copy
  loop is skipped and the second `pop_back()` (line 276) hits an empty array.
- `그에게 문에게 줘` (keys `rmdprp ansdprp wnj`): the role branch pops `unitStart[k]..k-1` = the article (line 292) from
  an empty `plain`.

Both typed into KQ1 at the first room abort the game:

```
run/run.log (ASan build), run2/run.log (release build, the second sentence):
scummvm: .../common/array.h:201: void Common::Array<T>::pop_back() [with T = Common::List<Sci::ResultWord>]: Assertion `_size > 0' failed.
```

With `NDEBUG` the `_size` wraps and the next push writes out of bounds. HEAD: unchanged (lines 190/277/293), and P4's
`koreanAnswerLine()` runs the same tokenizer from `kStrLen`/`kStrAt` in the QFG answer rooms.

Fix: give the article the noun's role condition,
`(onlyClass(tok[start - 1], articleClass) && (role[start - 1] == Common::kKoreanRoleNone || role[start - 1] == Common::kKoreanRoleOf))`,
and make `arrangeTokens()` defensive: `plainUnits = MIN<uint>(k + 1 - unitStart[k], plain.size())` after the push, and
in the role branch pop only `MIN<uint>(k - unitStart[k], plainUnits)` entries and copy the popped ones from `plain`
(not `tok[unitStart[k]..k-1]`) so a token is never emitted twice. Unit tests: both sentences with an article fixture.

### agy 4: Width/Draw past `len` (FALSE, nit)

`decodeUtf8Char` (`utf8.cpp:29-58`) stops at the first byte that is not `10xxxxxx`, and a NUL is not; every caller of
`Width`/`Draw` passes a NUL-terminated string (`Common::String::c_str()` or a heap string), so it cannot read past the
buffer (`test_agy4_decodeStopsAtNul`: `EA 00` and `EA B0 00` decode as the lead byte, 1 byte). What remains:
`drawListControl` (`controls16.cpp:112`, upstream) caps a list entry at `maxChars` *bytes*; when the cap falls inside a
syllable, `Draw` draws that whole syllable and stops (`len` goes negative, `while (len-- > 0)` ends). That is one complete
syllable more than the byte cap, never garbage. Optional: cap on a code-point boundary in UTF-8 mode.

### agy 5: `shift=` twice (CONFIRMED, minor)

`hirestext.cpp:160` adds `_set.getShift()` to the baseline and `HiResFontSet::drawChar()` adds `_plan.shift` again
(`font_set.cpp:211`). Drawn (`test_agy5_shiftAppliedTwice`, fixture JAMO face, `[font.300] shift=3`):

```
[agy5] top ink row: shift=0 -> 21, shift=3 -> 27 (moved 6 hi-res px)
```

KQ1's maps use `shift=0` only, so nothing visible in P3; the P4 packs' (still version 2) `hiresbit.map` files carry
`shift=-1` for font 0 (QFG1 EGA, QFG2, Camelot), which would become -2 once converted. Fix: drop `+ _set.getShift()` in
`GfxFontHiRes::draw()`, and make the test above (non-zero shift, measured ink row) part of `SciHiResTextTestSuite`.

### agy 6: `keyOf()` without a room (CONFIRMED, minor)

`state.cpp:366-373`: `stringKey()` returns `room = kAnyRoom`; `lookupPlace()` (`script_strings.cpp:172-186`) tries the
exact room only when `key.room != kAnyRoom`. `kStrCpy`/`kStrCat` set the room (`kstring.cpp:65, 93`), `keyOf()` does
not, so a string drawn straight from the script heap (`kDisplay`, `kTextSize`, controls, `kAddMenu`, `kFormat` of a heap
format) never finds a room-only entry. No shipped table has a room entry (KQ1 and the seven P4 `sci-ko.str`: 0 lines
with a room field), hence minor. Fix: in `keyOf()`, `if (key.isSet()) key.room = _gamestate->currentRoomNumber();`
(room entry first, any-room fallback is kept by `lookupPlace`), and a test through `keyOf`.

### agy 7: `getLongestUtf8()` returns 0 while advancing (CONFIRMED, minor)

`text16.cpp:842-866` returns `line.byteEnd` for a line not ended by a newline. `fitLine()` trims trailing spaces; on
leading spaces followed by a word wider than the box the only break is after the spaces, so the line is "the spaces"
and trims to nothing (`test_agy7_leadingSpacesThenLongWord`, 8 px advance, 64 px box):

```
[agy7] leading spaces + long word: forced=0 byteEnd=0 byteNext=3
[agy7] spaces only: byteEnd=0 byteNext=3
```

`Size()` (498-500) and `Box()` (640-642) stop on `charCount == 0`, so everything after those spaces is neither measured
nor drawn. Needs an unbreakable run wider than the box (Hangul breaks at spaces with the default `hangul=word`), so it is
rare; a trailing spaces-only line only loses its (blank) height. Fix: when `!line.forced && line.byteEnd == 0 &&
line.byteNext > 0`, return `line.byteNext` (draw the blank line, as the byte loop does), or skip the spaces and fit again.

### agy 8: OS-IME code point sampled at drain time (CONFIRMED, major)

`event.cpp:343-350` sets `_lastOsImeKey` for every key-down that `getScummVMEvent()` converts (0 for an ordinary key);
`getSciEvent()` (490-495) drains *all* pending events into `_events` before it returns one; the edit control reads
`events->lastOsImeKey()` (`controls16.cpp:189, 194`) only when the script later handles the surrogate `'x'`. So when the
IME's syllable and the next key arrive in one poll, the value belongs to the last drained key:

- syllable + Space/Enter (how Windows/ibus IMEs commit the last syllable of a word): `_lastOsImeKey = 0` when `'x'` is
  handled; with the built-in composer on, `'x'` is fed as the jamo `ㅌ`, with it off a literal `x` is inserted;
- two committed syllables in one poll: both `'x'` become the second syllable (`나나` for `가나`).

The SDL backend turns each `SDL_TEXTINPUT` into its own key-down (`sdl2-events.cpp:759-777`), so nothing upstream
serializes them. Headless: the syllable typed 300 ms before the space shows `문` (`imeD/`); typed with no delay
(`imeB/`, `xdotool type --delay 0 "문 "`) the syllable is gone, which fits the trace, but xdotool's keymap remapping can
also drop a fast Unicode key, so the run is supporting only; the verdict rests on the code trace. Fix: carry the code
point on the event (`SciEvent::osImeChar`, set in `getScummVMEvent()`), and in `getSciEvent()` set `_lastOsImeKey` from
the event actually returned (non-peek key-down; 0 otherwise). That also covers the `adopt()` case where the prompt's
first `kEditControl` call already carries the next key.

### agy 9: edit line details (PARTLY, minor)

- (a) `controls16.cpp:203-207`: the width test runs for every change, upstream only for an inserted character
  (`controls16.cpp:335-349`). A line already at or over the box width (a prefilled or restored text wider than the field;
  the hi-res advance of a syllable can differ from the width the text was entered with) then refuses Backspace, Delete
  and cursor keys. Real, needs an over-wide line: minor. Fix: undo only when `key()` inserted or composed (return a
  distinct result, or compare the new width with the old one and undo only when it grew).
- (b) `korean_input.cpp:88-95`: with `maxBytes < 3` the local `text` is cleared and `_composing` set before `writeRun()`
  refuses; `changed` stays false, so nothing is written back, and the next jamo composes from the stored state. And
  `_last == text` makes `adopt()` skip a new prompt that opens with the same single ASCII letter the previous line
  ended as (only after typing that line in English mode). Both nit. Fix: test the fit before changing state; reset
  `_last` when a prompt is drawn empty.
- (c) `kgraphics.cpp:945-949`: clamps a code-point cursor against the byte length. A too-large cursor is clamped by
  `utf8OffsetOf()` (stops at the NUL) wherever it is used, so no effect. Nit: compare with `utf8Length` in UTF-8 mode.

### agy 10: SCI32 with `kUtf8` (OUT OF SCOPE, nit)

True by reading: `decideTextMode()` gives `kUtf8` + `KO_KOR` for SCI2+ when a `ko/` folder with `sci-ko.str` exists or
`text_encoding=utf8` is set (`test_sci32HasNoHiresText` asserts `utf8()`), and `GfxText32::SwitchToFont1001OnKorean()`
(`text32.cpp:954-971`) has no UTF-8 guard: a UTF-8 continuation byte in `B0..BF` followed by the next syllable's lead
`EA..ED` matches its CP949 test, `setFont(1001)` follows and `GfxFontKorean` errors out when `korean.fnt` is missing.
Not user-reachable today: no SCI32 Korean UTF-8 pack exists (SCI32 is a non-goal, DESIGN section 1), and an upstream
CP949 SCI32 game keeps `kCp949` unless the user forces `text_encoding=utf8` (then it has `korean.fnt`, and its CP949 text
takes the upstream path). Cheap guard recommended: in `decideTextMode()`, `in.version >= SCI_VERSION_2` -> not `kUtf8`
(note it), and/or `if (g_sci->getTextMode().utf8()) return false;` in `SwitchToFont1001OnKorean()`.

### agy 11: kFormat diagnostic (PARTLY, nit)

`kstring.cpp:560-570`: for a text-resource format (`argv[1]` segment 0) the key lookup reads entry 0 of that resource
(`lookupText(argv[1], 0)`; entry 0 exists, no error) and keys `bufferId(0, resnr)`; the warning prints `-1:-1`, which
P3-fixes already records. `warned` is a function static, so a second game in the same session can miss a warning.
Diagnostic only. Fix if touched: key only heap formats, include `argv[2]` in `once`, make `warned` a `SciEngine` member.

### agy 12: keymap active without the composer (CONFIRMED, minor)

`metaengine.cpp:696-713` adds the `sci-korean` keymap whenever the target's `language` is `ko`; `event.cpp:322-327`
returns `noEvent` for the action even when `_hangulBuiltin` is false. So on an OS-IME target (`hangul_builtin=false`), a
`korean_text=false` target, or an upstream CP949 Korean game with `language=ko`, Shift+Space (a space typed with Shift
held), Ctrl+Space, RAlt, F12 and ScrollLock are swallowed. Overlaps P3-review m2 (defaults), but the gating is new.
Fix: pass the event through (`return` the key) when `!_hangulBuiltin`, and/or register the keymap only when the target
can use the composer.

### agy 13: `putFontPixel()` and the plane (CONFIRMED, nit)

`screen.h:393-425`: `putPixel()` clears the plane under the pixel (`clearTextRect`, 348), `putFontPixel()` does not. An
original-routed glyph drawn over hi-res ink without an erase in between leaves the hi-res ink on top. Text is normally
drawn into an erased rectangle (erase goes through `putPixel`), and no case was seen in the KQ1/P4 runs. Fix if seen:
`clearTextRect` per font pixel in the non-upscaled-font path.

### agy 14: `kDrawStatus` (CONFIRMED, minor)

`kmenu.cpp:82` uses `strSplit(text)` and never asks `keyOf()`. A game that passes a script string (or a `StrCpy`'d one)
straight to `DrawStatus` keeps it in English; KQ1 and the P4 games build the status line with `Format` (P4-notes: Korean
status lines in QFG1, KQ4, ...), so latent. Fix: `g_sci->strSplitHeap(textReference, text, nullptr, nullptr)`.

### agy 15: UTF-8 `kStrAt` past the buffer (CONFIRMED, nit)

`kstring.cpp:163-164`: a read with an index past the string and past `maxSize` returns 0; the byte path warns and
returns `s->r_acc` (the KQ5 0xFFFF workaround, 215-219). Only KQ5 is known to rely on it, and it is not a target. Fix:
return `s->r_acc` with the same warning.

### agy 16: `strcheck.py` duplicates (CONFIRMED, nit)

`strcheck.py:70-75` keys duplicates on the raw strings. `dup.str` with `05	007	A` and `5	7	B` -> `dup.str: 2 entries,
header yes`, rc 0, no duplicate reported, while the engine's `readNum` reads both as (5, 7). Fix: key on `int()` values.

### agy 17: empty folder code (CONFIRMED, nit)

`sci.cpp:230-231` and `393`: with `text_encoding=utf8` forced on a target detected as `UNK_LANG`, the code is `""`;
`addSubDirectoryMatching(dir, "", 1)` matches nothing and `run()` warns "The  translation folder has no usable sci-.str".
User-forced configuration, warning only. Fix: skip both when the code is empty.

### ocd I1: adopt + key double processing (FALSE)

The claim needs the key that opened the prompt to reach `texteditChangeUtf8()` again as `eventObject`. It does not: the
SCI0 `User` handler claims that key-down, puts its character in the input line and opens the dialog, whose loop asks
for *new* events; the first `kEditControl` call with the one-character text carries a later (or null) event. `adopt()`
takes over the character already in the text, `key()` handles the new event: two different keys. In play:

- `imeA/`: the OS-IME syllable `문` opening the prompt -> the field shows `문` once (not `문문`).
- `keyA/`: the jamo key `a` opening the prompt with the composer on -> `ㅁ` once (not `ㅃ`/`ㅁㅁ`).
- the P3 reference run `ansdmf dufdj` (prompt opened by `a`) parses as 문을 열어 and gets the open-door reply.

The related real problem (the code point `adopt()` reads may already belong to a later key) is agy 8.

## Required fixes (blocker / major)

1. **agy 3** (blocker): article units take the noun's role test in `KoreanParser::arrange()` (`korean_parser.cpp:186-191`),
   and `arrangeTokens()` clamps `plainUnits` and its pops to `plain.size()`, copying popped tokens from `plain`; unit tests
   for `그에게 문 안을 봐` and `그에게 문에게 줘` with an article fixture.
2. **agy 1** (major): gate the arrangement on the Korean table: `if (korean && sawKorean && !retval.empty())` in
   `Vocabulary::tokenizeString()` (`koreanWord` only when `korean`); add the line to the upstream-inertness audit.
3. **agy 8** (major): carry the OS-IME code point on the `SciEvent` and set `_lastOsImeKey` from the event returned to
   the script in `getSciEvent()` (non-peek key-down), not from the last drained one.

## Recommended

- agy 5: remove the second `shift` in `GfxFontHiRes::draw()` and add the drawing test (before the P4 maps with `shift=-1`
  are converted to version 3).
- agy 9 (a): undo only edits that grew the line.
- agy 7: never return 0 while advancing in `getLongestUtf8()`.
- agy 6: set the room in `SciEngine::keyOf()`.
- agy 12: do not swallow the toggle when the built-in composer is off.
- agy 14: `strSplitHeap` in `kDrawStatus`.
- agy 2: bound the non-raw -> raw copy by `dest_r.maxSize` (UTF-8 cut) and round a length-bounded `StrCpy` down to a code
  point boundary in UTF-8 mode.
- agy 10: keep SCI2+ out of `kUtf8` in `decideTextMode()` (one line) or guard `SwitchToFont1001OnKorean()`.
- Nits when the files are touched: agy 4 (code-point cap in `drawListControl`), 9 (b)(c), 11, 13, 15, 16, 17.
