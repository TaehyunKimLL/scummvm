# P5 re-review (Claude lane): review round 1 fixes

Reviewed: `21d05283761..da29587d483` (13 commits; code diff `~/work/tmp/review-pkg-p5-fix/fix.diff`), read-only
checkout `~/work/tmp/scummvm-review-p5` at `da29587d483`. Scratch: `~/work/tmp/p5-review-claude/` (never `/tmp`).
Binaries:
- `asan/scummvm`: final, ASan+UBSan, vptr off.
- `scummvm-asan-old`: `21d05283761`, ASan+UBSan.
- `scummvm-asan-base`: unmodified `ba0eecee2c4`, ASan+UBSan.
- `scummvm-da29587d483...`: final, debug.
- `scummvm-base`: unmodified, debug.

## Verdict

**P5 can merge into ko-2026.4. `ko-2026.4` (`ba0eecee2c4`) is an ancestor of `da29587d483`, so the merge is a
fast-forward.** P7 can build on it.

Every finding of my first review is addressed, and each fix was checked by running it:
- I reproduced agy C1 end to end on an English target under ASan: the old build overreads the stack, the new one does not.
- The harness (22 tests) passes on the final code under ASan+UBSan.
- No regression found on the English byte path.

The fix round brings one new **minor** finding (N1: the command window's first key is re-queued behind keys already
waiting) and two documentation nits. None of them blocks the merge.

## Per finding (my review `P5-review.md`)

| Finding | Status | Evidence (run) |
|---|---|---|
| M1 save prompt left the name and a black bar on the room | **addressed** | `runs/bar` (final debug build): F5 with 9, 10 and 11-syllable composer names. The input line is inside the 2-line box (`in1.png`, `in3.png`). After save 1 the input-row band y 196..240 equals the frame before the save (bbox `None`); the full-frame diff is only the neon sign and the walker. Saves 2 and 3 happened in the alley, where the game walks Larry (same in English: `runs/ew-*`), with no remnant. All three names are 27 bytes (`안녕하세요안녕하세`, `...셍` x2). Harness `test_save_prompt_rows`: the new string wraps to >= 4 rows at **every** width 1..40, for both break modes (`hangul=word` and `hangul=any`). That is 4 rows at 30/31/40, like the English prompt at 31 |
| m1 `unknownPos` past the 20 kept words | **addressed** | Harness `test_unknown_kept_in_20_words` (real LSL1 tables): the dense line `켜` x18 + `뽁` gives `unknownPos 20`, 20 words, word 20 = `뽁`. 50000 random lines of 1..30 tokens: every candidate <= 20 words, `1 <= unknownPos <= candidates[0].size()`, and the word at `unknownPos` is the unknown one (id 0): 0 violations. The first round's `review_words` probe now reports `unknownPos 20, cand0 size 20` |
| m2 UTF-8 v11 table re-encoded as CP850 | **addressed** | End to end: a Korean save with `켄이 보냈다` in slot 1 (v11 table + AGKO) was loaded by the **unmodified** build and saved again (no trailer, slot bytes kept; `runs/rs-base`). That save was then loaded and saved again by the old and new Korean builds (ASan; `runs/rs-ko-*`). Old: `ý╝äýØ┤ Ù│┤ÙâêÙïñ` in both the v11 slot and the trailer. New: `켄이 보냈다` in both, 0 sanitizer lines. Harness `test_trailer_outcomes_and_resave`: 5000 random tables x 3 outcomes (trailer ok / cut at a random byte / absent after an unmodified-build re-save), through `readSavedStrings` + `finishSavedStrings` as `loadGame` does. Every slot equals the trailer string (ok) or the v11 slot as written (cut, absent), with zeros after the NUL: 0 errors. A CP850 slot without a trailer still converts (`caf\x82` -> `café`). The first-round test that failed now passes |
| m3 global `text_encoding=utf8` switched every AGI game | **addressed** | `runs/en2-encnew`: `[scummvm] text_encoding=utf8`, English LSL1. The only text-mode log line is `Text mode: text_encoding=utf8 ignored: no translation for this game and the key is not in its own section`; no hi-res, `korvocab` or `agi-ko` lines. Frames against the unmodified build (`runs/en2-base`, same key script): the Esc menu is bbox `None`. Lefty's, the `look` reply and the F5 list differ only in the animated neon sign and the walking pedestrian (viewed), as do `en2-new`'s frames 1-2 |
| nit: stale "10 syllables" comment | **addressed** | `systemui.cpp:411-412` now says 29 bytes, 9 syllables |
| nit: GMM clip 28 vs field 29 | not changed (safe; noted as such) | GMM path verified in the first round |
| nit: IME code points > U+FFFF | not changed; the implementer says `kbd.ascii` is 16 bits | accepted |
| nit: `set.cursor.char` first byte | not changed | accepted (no affected game) |
| nit: unchecked logic reads in `koverlay` / `rawLogicMessageCount` | **addressed** (`logicMessageCount/Offset`, `collectSaids` clamp) | Harness `test_logic_message_bounds_vs_model`: 200000 random resources of 0..63 bytes in exact-size blocks; both readers equal a model (0 differences); 22047 offsets inside, 3.47 M refused, 0 ASan |
| nit: upstream footprint, keymap defaults, b3..d1 bisect trap | not changed (recorded in P5-fixes "Not fixed") | accepted |

Other fix-round items checked by running:
- **agy C1, end to end on an English target** (`runs/c1-*`). I wrote `mklogic.py`, which copies the fan game `fan1`,
  appends a rewritten logic 0 to `VOL.0` and repoints `LOGDIR`. The new logic 0 starts with
  `if (said(1 x N)) {}`, N = 8 or 40; the game is detected as `agi-fanmade`. Under ASan, typing a 1-word line, then a
  9-word line:
  - **old `21d05283761`**: aborts on the **1-word** line, for N = 8 and for N = 40:
    `AddressSanitizer: unknown-crash ... READ of size 2 ... in Agi::AgiEngine::testSaid(unsigned char, unsigned char*)`
    from `condSaid`. It reads past `testIfCode`'s 16-byte stack copy `p`. Every typed line triggers it, because all
    N words are unpacked before matching.
  - **unmodified upstream `ba0eecee2c4`**: survives the 1-word line but aborts on the **9-word** line (N = 8), with the
    same stack read in `testSaid`. Upstream's lazy loop also leaves `p[16]` once 8 typed words reach the 8th spec word.
    This is a pre-existing upstream bug that the fix also removes.
  - **new `da29587d483`**: N = 8 and N = 40, both lines: alive, 0 ASan/UBSan reports beyond the 3 known
    `global.cpp` lines. The replies are the game's own: "This is an empty room." for `look`, the template's unknown-word
    log for 9 words, since `said(1 x 40)` does not match 1 or 9 words.
- **`readSaidSpec`/`saidMatches` against a verbatim copy of upstream's lazy `testSaid` loop**
  (`test_readSaidSpec_vs_upstream_lazy`): 300000 specs, nwords 0..255, a third cut at a random byte, in exact-size
  heap blocks, 0..20 typed words. The upstream loop reads the same whole words, with the words a cut logic does not
  hold as 0. Results: 199864 complete and 90012 truncated compared, 0 differences, 0 ASan.
  A half word at the cut is read as 0 by `readSaidSpec`, as documented. My first model read its low byte and so
  disagreed 38 times; that was the model, not the code.
- **automatic description** (`test_automatic_description_24_to_33`): 45618 Korean/ASCII descriptions of 24..33 bytes
  go through the same steps as the code: `set.simple` copy (29), `saveGame` clip + `strlcpy(30)`, `readSavedGameSlots`
  clip(29) + `strncpy(30)`, then `figureOutAutomaticSavedGameSlot`'s `strcmp`. All match, the result is valid UTF-8 of
  <= 29 bytes with zeros after it. The byte path equals `strncpy(31)` + NUL on 20000 random byte strings.
- **`dropDownColumn` clamp**: for every column 0..39 and 0..80 cells, the result is never < 1, and it equals
  upstream's value wherever that is >= 1. The inventory clamp was checked by reading only (`MAX(0, ...)`).
- **`hasGlyph`** with the **real** LSL1 map (`korean-packs/LSL1AGI/ko/HIRESBIT.MAP`, engine data on SearchMan):
  11172 of 11172 syllables, false for `A` and 0x7F.
- **Han/Yeong in the Esc menu** (`runs/tgl-*`; `tn`, Esc, Ctrl+Space, menu frames before/after). Old: diff
  `(161, 98, 192, 112)`, the syllable drawn into the menu. New: `None`, and the prompt shows `] 수` plainly after the
  menu closes.
- **Map-only `ko/` folder** (`data/lsl1maponly`, `ko/HIRESBIT.MAP` only): `--detect` lists the Korean and English
  entries, and the Korean target starts (title -> Lefty's in Korean UI, `runs/maponly/g.png`). The log gives the
  expected `translation folder has no agi string table` and `no usable agi-ko.str` notes.
- **Command window** (`commandpromptwindow=true`, `runs/cpw-*`), typed at xdotool's 150 ms delay: OS-IME `술집을 봐`,
  composer `tnfwlqdmf qh` -> `술집을 봐` and the Korean reply; English `look at bar` -> the English reply. Fast typing:
  see N1.
- **Fix-round commits**: every commit `e3963a935d8..da29587d483` builds `scummvm` and `test/runner` with 0 warnings,
  and **runs** the suite green with the said() gate (`inter2.log`: 702 -> 703 -> 704 -> 705 -> 706 -> 707).
  The final ASan+UBSan suite: 707 OK, 0 sanitizer lines. Each fix commit carries one `Assisted-by: Claude Code`.
- **First-round harness rerun** on the final code (ASan+UBSan): all 15 original tests pass, including the conversion
  test that failed in round 1. 22 tests in total with the new ones, 0 sanitizer lines.

## New findings

### N1 (minor): the key that opens the command window is queued behind keys already waiting

`engines/agi/text.cpp` `promptCommandWindow()` -> `AgiEngine::enqueueKey()` (`keyboard.cpp`), which is `keyEnqueue()`,
an append at the queue's **end**. The doc comment in `agi.h` says "Puts @p key first".

When the player types faster than one key per poll, the following keys are already in the queue, so the opening key
lands after them. Reproduction: `runs/cpw-fastcomp2` and `cpw-fastcomp3`, Korean target, `commandpromptwindow=true`,
`xdotool type --delay 1 tnfwl`. The window holds `ㅜㄹ짓` (keys n f w l t) instead of `술지`, twice out of two.
The same fast typing at the normal prompt (`runs/cpw-fastprompt`) gives `술지`. At 150 ms per key it works (the cases
above). The English byte path still appends the key to `commandText` and is unaffected.

Fix: insert at the queue's front, e.g. `_keyQueueStart = (_keyQueueStart + KEY_QUEUE_SIZE - 1) % KEY_QUEUE_SIZE;
_keyQueue[_keyQueueStart] = key;` (dropping the oldest if full), or hand the key to the window's `stringKeyPress`
once its edit loop starts.

### N2 (nit): `PATCHES.md` not updated for the fix round's upstream-file changes

The `ko/04-agi` rows do not mention:
- `testIfCode()`/`condSaid()` reading said() from the logic, which **changes the English path too** and removes
  upstream's own overread (above);
- `parseUsingDictionary()` stopping at `MAX_WORDS` on the **English** path (upstream writes `_egoWords[20+]`);
- the `dropDownColumn` >= 1 clamp, which on the English path moves a first menu item of >= 39 cells from upstream's
  column <= 0 (box drawn at a negative x) to column 1;
- the inventory column clamp;
- `enqueueKey`;
- the Amiga `logicDirectoryName`.

These are upstream bug fixes, but they are byte-path changes, so they belong in the patch list.

### N3 (nit): unverified pieces stay unverified

The pixel clip in `drawCoverageOnDisplay` and the Amiga `dirs` path have no seam and no data here. I read them only;
they look right.
