# P5 notes: AGI Korean (LSL1 first)

Running log of the implementation of `P5-agi.md`. Branch `ko/p5-agi`, worktree `~/work/scummvm-2026.4-korean-p5`, base
`ba0eecee2c4`. Builds: `~/work/tmp/p5-build` (AGI only), `p5-all` (agi+sci+scumm), `p5-asan`, `p5-none`, `p5-mingw`.
Scratch: `~/work/tmp/p5w` (run dirs `runs/`, scripts `bin/`, which copy the prototype's with the worktree's extrapath).
Code source: the prototype `~/work/tmp/p5/tree`. Checked: every unit and every hook diff of the plan's appendices A/B/C
is byte-identical to the prototype (script compare), so tasks copy the prototype's files and apply the plan's hook
hunks per task (`~/work/tmp/p5w/bin/hk.py`, `git apply --recount`, as the plan's hunk headers have hand-written counts).
Tasks are committed one by one; the sha of a task is written into the log by the next commit.

## Log

### a0 (no commit)
- Worktree exists (`git diff --stat v2026.4.0 -- engines/agi` empty). AGI-only debug build: **N0 = 648**.
- Submodule dir `dists/svf-fonts-open` was empty: filled with a local clone at the gitlink commit 3d40de8d940f
  (nested repo, parent status stays clean); `sync-svf.sh --check` OK, `mkgolden.py --check` rc 0.
- Data: `~/work/tmp/p5/data/lsl1{en,ko,ko3}` reused from the prototype; md5 LOGDIR/WORDS.TOK/VOL.1 as in 0.2 (the
  `lsl1ko3` copy already carries the header line, AGIKOR v3 dat and `HIRESBIT.MAP`).
- Red (base build, `language=ko`, lsl1ko3): `Error running game: Game data not found`
  (`agi-p5-red-base-language-ko-game-data-not-found-r2.png`, seen: the box on a black screen).

### a1: cell helpers
- Red: `make test` -> `No rule to make target 'engines/agi/textcells.o'` (module.mk named the object before the file
  existed; the plan's `textcells.h: No such file` would come one step later). Green: **653** (N0 + 5).
- D14.1 mutation (the `if (utf8) wordEndChar = 0;` removed): `Failed 2 of 653`: `fixed > 0u` and `bad == 0u`, found
  9150 != 0 invalid lines; restored.

### a1 996f87e7484 (sha of a1)

### a2: text mode
- Red: `No rule to make target 'engines/agi/textmode.o'`. Green: **658** (N0 + 10), 0 warnings.
- a2 sha 967203a3412.

### a3: translation table
- Red: `No rule to make target 'engines/agi/translation.o'`. Green: **668** (N0 + 20), 0 warnings.
- a3 sha 579f2a776ef.

### a4: AgiHiResText stencils
- Red: `No rule to make target 'engines/agi/hirestext.o'`. Green: **671** (N0 + 23), 0 warnings. The exhaustive test covers
  all 11172 syllables against the fixture face drawn through an SvfChain (0 mismatches).
- a4 sha aa52ffc8892.

### a5: detection of a translation folder
- Hook `detection.cpp` applied whole (5 hunks; same as the prototype's file). Build 671, 0 warnings.
- Green: `--detect --path=lsl1ko3` lists `.../Korean` first and `.../English` second; `lsl1en` lists English only.
  `language=ko` run on lsl1ko3 reaches the title screen (`agi-p5-a5-language-ko-title.png`: seen the English title, 640x400).
- a5 sha b9490a63806.

### a6: engine wiring
- Hunks: agi.h 0,4,6,7 (the `saidscan.h` include line removed until c1/c2; `translateLogicTexts`/`rawLogicMessageCount`
  are declared here), agi.cpp 0,1,3-8, loader.h 0-2, metaengine.cpp 1 (getLanguage). **Deviations**: `rawLogicMessageCount()`
  is defined here (same hunk as textModeInputs), the plan lists it under a7; the render-mode/Hercules block (D12) sits in
  the same hunk as the hi-res text setup and is applied here instead of a8 (nothing draws Hangul before a8; English
  frames are unaffected as `_hiresText` is null for English targets). Build: 671 tests, 0 warnings.
- Runs (`--debugflags=text`, `~/work/tmp/p5w/runs/a6-*`): lsl1ko3: `agi-ko.str: 1860 entries`, no warning, `Hi-res text:`
  notes; lsl1ko (no header): `agi-ko.str has no fingerprint header ... loaded unchecked` + `This game's agi-ko.str header
  line is: #! format=1 game=lsl1 fingerprint=1d9311bbee31342e76c7570a29d7616d engine=agi`, v2 map refused (korean-default.map
  used); lsl1ko3bad (digit changed): `was made for another game ... not loaded` + `The ko translation folder has no
  usable agi-ko.str`.
- v3 fingerprint (KQ4 AGI, `ko/agi-ko.str` of one comment line): printed `fingerprint=ba58a0d9551a852acefa31e4461f3643`
  = md5(KQ4DIR + WORDS.TOK) computed independently: `_name + DIR_` works, no fix needed (the plan's UNVERIFIED item closed).
- a6 sha bc34d9b6638.

### a7: messages, object names, view descriptions
- Hunks: logic.cpp/.h, objects.cpp, sprite.cpp whole; op_cmd.cpp 0 (include) and 3 (`cmdSetGameID` from `rawTexts`).
  Build 671, 0 warnings. Run (`runs/a7`): log `Game ID: "LLLLL"` (unchanged), Lefty's reached after two Returns
  (quiz skipped by the pack), English status line (that is a9's).
- a7 sha 42e11b1856e.

### a8: display by code point, Hangul stencils
- Hunks: graphics.cpp 0-5, graphics.h 0, preagi/preagi.cpp 0 (the render-mode/Hercules block came with a6). **Deviation**:
  text.cpp hunks 0, 1 (includes) and 4 (`displayText`/`displayCharacter` by code point) and text.h hunk 1
  (`displayCharacter(uint32,...)`) are applied here, not in a9: without them the box text is walked byte by byte and
  draws AGI-font squares, so a8 could not be seen working (first run after the graphics hunks: 18 squares per line,
  `runs/a8/s1.png`). Build 671, 0 warnings.
- Run (`runs/a8/s1.png`, copied as `agi-p5-a8-thanks-box-byte-wrap.png`): the thank-you box reads
  `감사합니다.` / `이제 레저 수트를 차려입고 '레저 수트 래리'와 함께 '라운지 리저드의 땅'으로 들어갈 준비를 하세요!`
  in Hangul stencils, wrapped by bytes so the box is narrower than the prototype's: the whole-box comparison with the
  prototype frame is therefore a9's check, not a8's.
- a8 sha a9c5c2f700a.

### a9: layout in cells
- Hunks: text.cpp 5 (statusDraw), 28 (stringWordWrap through TextCells, 8000-byte buffer), 29, 30 (stringPrintf: particles,
  buffer, cut on a character); text.h 0 (the `kTextBufferBytes` enum; the `agi/hangulline.h` include line left for b1/b3),
  inv.cpp 0-3, menu.cpp 0-4, systemui.cpp 0 (include), 1 (Korean interpreter strings), 8, 9 (button widths). Build 671, 0 warnings.
  The `SYSTEMUI_SAVEDGAME_DISPLAYTEXT_BYTES` hunks (systemui.h 0/1, systemui.cpp 7) wait for d3.
- Observation for the review: the `stringPrintf` result buffer is `kTextBufferBytes` (8000) on the byte path too, so
  upstream's `assert(size < sizeof)` fires at 8000 not 2000 there; no English text is near either limit (the byte path
  differential tests of a1 cover the wrap; the assert is unreachable for the game texts).
- Run (`runs/a9`, `hires_text_log=true`): 0 `no face has` lines; thank-you box and Tab inventory are **pixel-identical** to the
  prototype's frames (`PIL ImageChops`, bbox None); Lefty's frame and the menu differ from the prototype's only inside
  the animated neon sign / HOTEL sign bbox (144..568 x 16..184, 536..568 x 16..60). Seen: box wrapped in two lines of
  cells (`이제 레저 수트를 차려입고 '레저 수트 래리'와 함께 / '라운지 리저드의 땅'으로 들어갈 준비를 하세요!`), status
  `점수:0 / 222` left and `소리:켜짐` right, menu bar `시에라 파일 행동 특별 속도` with the drop-down `래리 소개 / 도움말 <F1> /
  계산기 / 퍼즐`, inventory `가진 물건:` with the items in columns and `아무 키나 누르면 게임으로 돌아갑니다`.
  Screenshots `agi-p5a-{box,lefty,menu,inv}.png`.
- a9 sha 23ebcb9b84c.

### a10 (no commit): P5a smoke and the English gate
- Korean: `agi-p5a-{box,lefty,menu,inv}.png` (from the a9 run, captions in `captions.txt`; all four viewed, see a9).
- English gate, base binary = `~/work/tmp/p5/build-base/scummvm` (unmodified `ko-2026.4`), frames by `xwd` (`PIL ImageChops.difference().getbbox()`):
  lsl1ko3 without `language=` (Return, Return, 6 s): **None**, 0 `Hi-res text|agi-ko|korvocab` log lines; the same folder's
  Esc menu, Tab inventory and a typed `look` + Return: **None**/**None**/**None**; `lsl1en` (quiz screen) **None**;
  fan game `AGI Contest 1 Template` first frame **None** (non-empty content bbox 162,160,476,366). Runs `runs/a10-*`.
- Note: `lsl1en` has the age quiz, so Esc/Tab steps are run on the quiz-skipped pack folder.
- Cell-conversion differential tests: `test/engines/agi/cell_bounds.h` commit 7f1f0e20f7b (+4 tests: 675).

### b1: the composer line
- Red: `No rule to make target 'engines/agi/hangulline.o'`. Green: **680** (+5, incl. 50000 random key sequences), 0 warnings.
- b1 sha 38028844b0d.

### b3 (done before b2: the order is swapped) and b2
- **Deviation (order)**: text.cpp needs `AGI_KEY_CODEPOINT`/`AGI_KEY_HANGUL_TOGGLE` (keyboard.h) and the keyboard/cycle hooks
  call `TextMgr::hangulInputAvailable()/hangulToggle()/promptKeyPress(int)`, so b2 cannot compile before b3. b3 is
  committed first (with the keyboard.h macros), b2 second.
- **Deviation (d3 share)**: text.cpp hunks 24 and 27 (`stringEdit`/`stringKeyPress`) mix the composer with the save-name
  byte budget (`_inputStringMaxBytes`, `stringFitsBudget`); they are applied whole in b3. The budget stays 0 (inert)
  until d3 sets it from `askForSaveGameDescription`.
- b3 hunks: keyboard.h 0; text.h 1-6 (+ the `agi/hangulline.h` include); text.cpp 2, 3, 6-27; op_cmd.cpp 1, 4, 5;
  systemui.cpp 2; agi.cpp 2 reduced to `_text->hangulSetup(false)` (c2 changes it to `hasKoreanWords()` and adds the
  table load). Build 680, 0 warnings.
- Run (`runs/b3`, `hangul_input=true`): typed `tnfwlqdmf qh` -> prompt `] 술집을 보` with the syllable being composed inverted
  (`agi-p5-b3-prompt-preedit.png`, viewed).
- b3 sha 36edd1210b9.

### b2: keys (committed after b3), and b4 (no commit): P5b smoke
- b2 hunks: keyboard.cpp 0-1, cycle.cpp 0-3, op_test.cpp 0-1 (`condHaveKey`; the `textcells.h` include comes with it),
  agi.h 5 (`AgiAction`) and 8 (`int processAGIEvents()`), metaengine.cpp 0 and 2 (`initKeymaps`). Build 680, 0 warnings.
- b4 (runs `runs/b4-*`, `--debugflags=text,input`): composer `tnfwlqdmf qh` -> `술집을 보` with the preedit inverted (b3);
  OS IME `xdotool type "술집을 봐"` with `hangul_start=false` -> prompt `] 술집을 봐`, log `key 1c220 pressed`
  (`agi-p5b-osime-typed.png`); `ctrl+space` -> `[key] Han/Yeong -> English`, `r` before gives jamo, after gives `r`
  (`agi-p5b-toggle-english.png`).
- **Alt+S (the UNVERIFIED item)**: `alt+s` at the title, in the message box, on an empty prompt and while composing `tn`:
  ScummVM saves its screenshot (4 `scummvm-lsl1-ko-0000N.png` files, toast visible), the prompt stays empty / `수`, the log
  shows only keys `0d`, `6e`, `74`: **no `ㄴ` leak, no `73`; not reproduced, no guard added** (`agi-p5b-alt-s-composing.png`).
- b2 sha 357c95e811d.

### c1: said() core and candidates
- Red: `No rule to make target 'engines/agi/korparse.o'`. Green: **691** (N0 + 43; +11: 9 of jamo's, the 200000-pair
  `saidMatches` vs verbatim upstream loop, the 50000-sentence candidate invariants), 0 warnings. `saidscan.{h,cpp}` added to module.mk (console use, c4).
- c1 sha 1d247cc46bb.

### c2: Words and said()
- Hunks: words.cpp 0-5, words.h 0-1, op_test.cpp 2-3 (`testSaidSpec`, `compare.strings` with `agiStringsEqual` + answers),
  op_cmd.cpp 6 (`set.string` through `setStringFromMessage`), agi.h 1-3 and 9 (+ the `saidscan.h` include), agi.cpp 2
  (`loadKoreanTable`, `hangulSetup(hasKoreanWords())`), 9-10 (`setString`), 11 (`collectSaids`, declared in the same agi.h
  hunk as `testSaidSpec`, so it lands here instead of c4). Build 691, 0 warnings.
- Runs (`runs/c2-*`): `[korvocab] loaded 1217 Korean words (version 3) from agi-ko.dat`; composer `술집을 봐` + Return -> the
  look-at-bar reply in Korean (`agi-p5c-look-bar-reply.png`, viewed: `레프티 술집 밖이다. 네온 몇 개로 이런 광경을 만들 수
  있다니 놀랍지 않은가?`); English `look at bar` (with `hangul_start=false`; with the composer on, Latin keys are jamo until
  Han/Yeong is toggled) gives the same box (`agi-p5c-english-look-at-bar.png`).
- c2 sha 742532078f2.

### c3: the LSL1 said() gate on real data
- `$BLD/korean-packs/{LSL1AGI -> ~/work/tmp/p5/data/lsl1ko3, LSL1AGI-said/{pairs,saids}.tsv}` set up as the plan says.
- Without the folder: `Warning: korean-packs/LSL1AGI and LSL1AGI-said are not in the working directory: LSL1 said() check
  skipped`, passes. With it: 778 pairs, **pass 691, partial 23, nomatch 20, unknown 7, order 6, english 31** (= jamo's
  `said8.txt`). Red: expected pass 692 for one run -> `Expected (pass == 692u), found (691 != 692)`, `Failed 1 of 692`; restored.
  Build 692 (+1), 0 warnings. Objects `words.o` and `agi_test_stubs.o` are named explicitly, never `libagi.a`.
- c3 sha eef3dee78fe.

### c4: console commands
- Hunks: console.cpp 0-3, console.h 0, agi.h 10 (`getOpCodesCondTable`). Build 692, 0 warnings.
- GUI debugger (`ctrl+alt+d` under Xvfb): `saids 0` lists indices 0..274 = **275** specs = `grep -c '^0\t' saids.tsv`; `koverlay`:
  `entries 1860, stale 0, untranslated 63, missing glyphs 0, boxes over 20 lines 1` (`m37:2`);
  `parse look at bar` -> `#0: look=2 bar=203`. **`parse 술집을 봐` could not be typed**: the SDL console takes ASCII only, the
  Hangul keystrokes of xdotool are dropped (`Usage: parse <sentence>`); the Korean candidate path is covered by the c3
  gate and the c2 run instead. Screenshots `agi-p5c-console-*.png` (viewed).
- c4 sha 1e3165af88d.

## Checkpoints A and B (taken together after c4)

Method: critical read of the hooks (agi.cpp, graphics.cpp, words.cpp, logic.cpp, menu/inv/status hooks) and randomized
independent-model tests of what the diff changed, run under ASan+UBSan.
- **New tests** (commit after this note): `cell_bounds.h` +1 (`test_compareStringsEqualsUpstream`: `agiStringsEqual` vs a
  verbatim copy of upstream's `testCompareStrings`, 200000 random pairs incl. bytes >= 0x80 and 40-byte clipping) and
  `analyze_fuzz.h` (30000 random lines through `Words::analyze` with the real LSL1 dictionary and Korean table: ASCII
  words, known and made-up Hangul tokens, English words with Korean tails, stray bytes, lines cut inside a multibyte
  sequence, 700-byte lines, in exact-size heap blocks; invariants: pure (same twice), 1..4 candidates, each <= MAX_WORDS,
  unknownPos points inside the typed words). Count **694** (N0 + 46). The generator is checked to reach both outcomes.
  - Finding by the test: `unknownPos` can exceed the candidate length when a line has more than `MAX_WORDS` (20) words and
    the 21st or later is unknown (`typed.resize(MAX_WORDS)` after `unknownPos` was set). Unreachable from the 40-cell
    prompt (20 one-cell words fill it); upstream's English path writes `_egoWords[]` past 20 there. Not changed; the test
    states the bound.
- Builds: AGI normal **694** OK; ASan+UBSan (`p5-asan`, `-fsanitize=address,undefined`; **`-fno-sanitize=vptr` appended to
  CXXFLAGS in its config.mk**: with vptr checks `words.o` in the test link needs `typeinfo for Agi::AgiBase`, which the
  stub-only link does not have) **694** OK, 0 sanitizer lines in the tests; agi+sci+scumm (`p5-all`, with the P4 SCI
  `korean-packs/` links next to the AGI pack: SCI's pack test asserts a pack was seen) **814** OK; no-engine (`p5-none`) **648** OK
  (AGI tests absent). 0 compiler warnings in all.
- Real data, ASan binary (`runs/asan1`): box, composer `술집을 봐` + Return -> Korean reply, Esc menu, Tab inventory: **0
  AddressSanitizer lines**. 3 UBSan lines `global.cpp:111/115/134: load of value 219 ... not a valid value for type 'bool'`
  (`_veryFirstInitialCycle`, uninitialised upstream member; `global.cpp` is untouched by this branch: pre-existing).
- `sync-svf.sh --check` OK, `mkgolden.py --check` rc 0, tools tests `test_k6bake/strcheck/trscheck` OK (with
  `TMPDIR=~/work/tmp/p5-tmp`; with `~/work/tmp/p5w/tmp` one of them failed on a missing temp parent: environment, not code).
- Read-through findings: none that needed a fix. Observations recorded above (a9: result buffer assert at 8000 bytes on the
  byte path).
- Review tests commit 860f4db03e9 (+2: 694 total with the a1 follow-up).

### d1: the string trailer unit
- Red: `No rule to make target 'engines/agi/saved_strings.o'`. Green: **699** (+5), 0 warnings. `test/module.mk` and
  `engines/agi/module.mk` now equal the prototype's.
- d1 sha d955881bea7.

### d2: save and restore
- Hunks: saveload.cpp 0-5. **Deviation**: systemui.cpp hunk 4 (`askForSaveGameDescription` reads `stringGetText()` instead of
  `(char *)_text->_inputString`) is applied here, not in d3: b3's change of `_inputString` to `uint32` cells made the byte cast
  hand the cell bytes (`38 BB 00 00` for 문) to the description, so every commit from b3 to d2 saved a garbled name
  (seen in the first d2 run: description bytes `38 bb 00...`, list line `01:8□`). Build 699, 0 warnings.
- Data (`runs/d2`, `originalsaveload=true`): F5, Enter, composer `ans`, Enter, Enter -> `lsl1-ko.001`; gunzip: `d[35]` = **11**,
  `AGKO` at 44495 of 44550 (a 55-byte trailer at the end), description bytes `eb ac b8` (문, valid UTF-8); F7 lists `01: 문`
  and restores (log: no `broken string trailer`). The unmodified base build (`runs/d2-base`, the file renamed `lsl1-en.001`)
  lists the save (name garbled in its font) and loads it.
- d2 sha e9bbde926bb.

### d3: Korean save names
- Hunks: systemui.h 0-1, systemui.cpp 3, 5-7 (hunk 4 was taken by d2), saveload.cpp 6, op_cmd.cpp 2 (the text.cpp budget code came with b3).
  Build 699, 0 warnings.
- Data (`runs/d3`): typing `dkssudgktpdy dkssudgktpdy` as the name gives `안녕하세요 안녕하셍` = **28 bytes** (gunzip of the
  save: `d[4:35]` decodes as UTF-8, version byte 11); the verification box and the F7 list show it
  (`agi-p5d-save-name-byte-budget.png`, viewed; the list line is cut at its 31 cells).
- d3 sha 343f7a103a9.

### d4 (no commit): P5d smoke
- `runs/d4c` (`originalsaveload=true`, `hangul_start=false`): save slot 1 outside; Up x25 to the door; typed `open door` (English
  in the Korean target: box `좋아.`, then the bar inside); save slot 2; F7 slot A -> inside, F7 slot B -> outside, both
  restores correct (`agi-p5d-restored-{inside,outside}.png`), 0 `broken string trailer` lines.
- **Not done**: the password `켄이 보냈다` at the door: in this pack `open door` enters Lefty's without a password prompt
  (the password is a later puzzle), so a `set.string`/`compare.strings` round trip through a real save was not reproduced
  in play; it rests on the 5 `saved_strings` tests (trailer round trip, cuts, damages), the d2 run (trailer written and
  read, no `broken` line) and the c3 gate.
- ScummVM save dialog (`originalsaveload=false`, GMM save) with an OS-IME name: **not run** (the AGI restore list was
  checked in d2/d3; the `clipBytes` hook of `scummVMSaveLoadDialog` is a one-line change read in the diff).
