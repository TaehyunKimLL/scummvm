# P4: SCI data games — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task by task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** DESIGN.md section 11 row P4: the SCI engine of P3 plays the Korean packs of QFG2, QFG1 EGA, LSL1 EGA/VGA, KQ4,
LB1 and (conditionally, decision 10) Camelot, with the engine changed only where a game measurably needs it, every
per-game rule in one small table in `korean_parser.cpp`, and the packs' data work (headers, version 3 word tables,
v3 maps re-paired to OFL faces, LB1/Camelot already repackaged) listed as harness dependencies.

**Architecture:** one shared sub-phase (P4a) adds the few engine pieces the P4 games need and P3 deferred to P4
(`sci11StringsStart` hooked, the per-game answer-room table + `englishLine`, growing a dynamic block instead of
cutting a longer translation, the hi-res font set chosen by the font resource actually drawn, a developer-data test of
the packs' tables); then one sub-phase per game (P4b..P4f) that changes **no engine file** unless its own data step
shows a need, and proves the game with unit tests + headless scripted runs (Xvfb, xdotool, ScummVM's console and
screenshot action) + a recorded manual checklist. The k16said acceptance numbers wait for the debug socket
(`ko/07-debugsocket`), exactly as P3 Task 20 does.

**Tech stack:** C++11 ScummVM code, CxxTest (`make test`), the P2/P3 tools `tools/korean/{k6bake,strcheck}.py`
(Python 3 stdlib), Xvfb + xdotool + the SCI debugger console for the headless runs.

Source of scope: DESIGN.md section 11 rows P3/P4/P5, sections 2.2, 3, 4, 5, 6, 10, 12 (decisions 3, 4, 10);
`P3-sci-kq1.md` section 1 item 14 (what P3 left to P4), `P3-notes.md`, `P3-review.md` + `P3-fixes.md` (M3, m5, m6 are
P4 inputs), the code at `5741781806a`, `jamo` (read with `git show jamo:`), the harness (`~/work/scummvm`, read only)
and the memory notes named in the brief (each claim below re-checked against code or a run).

---

## 0. Facts checked for this plan (2026-11-22)

Tree `/home/thkim/work/scummvm-2026.4-korean`, branch `ko-2026.4` at **`5741781806a`** (P3 + the P3 review fixes
`f5d8f52909b..16f9be227ab` + their docs). `~/work/tmp/scummvm2026-p3build` (built 21:18 from the fixed tree):
`Running cxxtest tests (675 tests)...OK!` = **N0 = 675**.

**Prototype** (built and run before this plan was written):

- `~/work/tmp/scummvm2026-p4-tree/` = `git archive 73bf2f80ba7` + the 15 files the review fixes changed, copied from
  `5741781806a` (so it equals `5741781806a` outside `docs/`) + every engine/test change of P4a; `dists/svf-fonts-open`
  is a symlink to the worktree's submodule (`git archive` leaves the submodule empty). Built out of tree in
  `~/work/tmp/scummvm2026-p4-treebuild` with `configure --disable-all-engines --enable-engine=sci --enable-debug
  --disable-optimizations`. The diff against the worktree at `5741781806a` is `~/work/tmp/p4-proto/diff/all.diff` (the only difference to
  this plan's blocks: the prototype's a5 debug line also prints the id asked, `(asked 2107)`). Result: `Running cxxtest tests (679 tests)...OK!` (675 + 4), `scummvm` links, no warning.
- Game data: the harness test zips `~/work/scummvm/dist/scummvm-game-<pack>-0ab70f653d3-test.zip` unzipped to
  `~/work/tmp/p4-data/<pack>/` (English game files + `ko/`); `~/work/tmp/p4-datah/` = the same with hard links, where
  `ko/sci-ko.str` got the fingerprint header (`strcheck.py header`) and `ko/sci-ko.dat` was rebuilt as version 3 from
  the harness master (`k6bake.py build`). Nothing of it is in the repository.
- Runs: `~/work/tmp/p4-proto/r0-*` (P3 binary, packs as shipped), `r1-*` (P3 binary, headed tables), `r2-kq4`,
  `r3-lsl1*`, `r4-qfg2` (prototype binary), `r4p3-qfg2` (P3 binary, same steps: the red run). Scripts `go.sh`,
  `go1.sh`, `mkrun.sh` there (copied in section 4).

### 0.1 The P4 games on this host

| Pack (zip) | Folder | gameid | Release (detection entry, `detection_tables.h`) | SCI | Parser | Data |
|---|---|---|---|---|---|---|
| `qfg2` | `QFG2/GAMES/QFG2` | `qfg2` | DOS 1.105, interp 1.000.087 (line 5382) | SCI0 late (LZW1 volumes, `vocab.900`) | yes | zip + `gamedata/Quest for Glory 2 ... .zip` |
| `qfg1ega` | `QFG1EGA/GAMES/QFG1EGA` | `qfg1` | DOS floppy, interp 0.000.566 (line 5128) | SCI0 | yes | zip + `gamedata/Quest for Glory 1 ... .zip` |
| `lsl1ega` | `LSL1EGA/GAMES/LSL1EGA` | `lsl1sci` | 2.0, interp 1.000.575 (line 3243) | SCI1 (EGA) | no (icons) | zip + `gamedata/Leisure Suit Larry ... (Floppy DOS EGA Remake).zip` |
| `lsl1vga` | `LSL1VGA/GAMES/LSL1VGA` | `lsl1sci` | 2.1, interp 1.000.577 (line 3270) | **Middle SCI1** (console `version`, run r3), VGA 256 colours | no | zip + `gamedata/Leisure Suit Larry ... (Multi-Platform).zip` |
| `kq4` | `KQ4/GAMES/KQ4` | `kq4sci` | 1.000.111 (line 1794) | SCI0 early | yes | zip + `gamedata/King's Quest 4 ... (Floppy DOS, SCI).zip` |
| `lb1` | `LB1/GAMES/LB1` | `laurabow` | 1.000.046, interp 0.000.631 (line 2920) | SCI0 late | yes | zip + `gamedata/Laura Bow 1 ... .zip` |
| `camelot` | `CAMELOT/GAMES/CAMELOT` | `camelot` | 1.001.000, interp 0.000.685 (line 484) | SCI0 late | yes | zip + `gamedata/ConquestsOfCamelot_DOS_KR_BETA5_20260912.zip` (the old Korean beta, not used) |

**No P4 game is SCI1.1.** Consequences: `|code|` font changes never occur in P4 text (upstream processes `|` codes only
for `getSciVersion() >= SCI_VERSION_1_1`, `text16.cpp:232`, `:424`, `:552`; P3's `SciUtf8Decoder` the same, `:855`),
and the `sci11StringsStart` hook changes no P4 game (it is still done in P4a because P3 deferred it to P4; its data
check uses the QFG1 VGA pack, which is not a P4 game).

Every pack is **already in the new format** (path C + D of DESIGN 2.2): `ko/message.map` + `ko/resource.msg` (UTF-8
TEXT, built by `packvol.py`, also for QFG2's LZW1 game volumes), `ko/sci-ko.str`, `ko/sci-ko.dat` (version 2, no
header) for the parser games, the v2 maps `ko/hires{vf,bit,ttf}.map`, game faces in `ko/` (LB1, Camelot, LSL1). The
LB1 and Camelot repackaging of decision 10 was done by the harness on 2026-10-17 (`repack_sci.py`, PACKS.md "SCI game
packs"); no CP949 in-place data is in either zip. What is missing is only what this tree added since: headers, word
table version 3, v3 maps (section 5).

### 0.2 What the P3 binary already does with these packs (runs r0, r1, r2)

- `--detect` on each folder lists the Korean entry before the English one (`sci:qfg2 ... (DOS/Korean)` then
  `(DOS/English)`; same for all seven).
- `language=ko`, 25 s headless (`r0-*`): all seven start at 640x400 in Korean through the fallback map
  (`Hi-res text: using data:hires_text/maps/korean-default.map` after the two "map version 2 is not read" notes):
  LB1 copy-protection screen (suspect names, all Hangul), KQ4 copy-protection question, QFG2 piracy notice, Camelot
  and LSL1 VGA title art (no text yet). Screens viewed.
- Each prints its header lines (warnings) — these are the values of the table in section 5:
  `This game's sci-ko.str header line is: #! format=1 game=qfg2 fingerprint=97c9d236d0f581f496021ff95e091630 engine=sci`
  and the `sci-ko.dat` line with `vocab=`. LSL1 EGA and VGA have the **same** fingerprint (scripts and vocabulary are
  byte-identical, memory `lsl1-korean`).
- With the headed tables (`r1-*`): `[korvocab] loaded N Korean words (version 3)` and no header warning, all seven.
- KQ4 (`r2-kq4`): copy protection passed with any answer (the pack's `SCRIPT.701`), the intro plays with Korean
  narration boxes (`오래전에 잃어버린 아들 알렉산더가 ...`), wrapped on three lines at 2x.

### 0.3 What the prototype proved (P4a code, `scummvm2026-p4-treebuild/scummvm`)

| Check | Run | Result |
|---|---|---|
| unit tests | `make test` | `Running cxxtest tests (679 tests)...OK!` = 675 + 3 (`SciKoreanParserTestSuite`) + 1 (`SciKoreanPacksTestSuite`) |
| pack suite, three ways | `./test/runner SciKoreanPacksTestSuite` with `korean-packs/` -> headed copies / -> shipped (headerless) QFG2+KQ4 / absent | `OK!` / 4 failures (`getHeader().present()`, `version 2 != 3` for QFG2 and KQ4) / `Warning: korean-packs/ is not in the working directory` + `OK!` |
| QFG2 Aziza's door, Korean answers | `r4-qfg2`: console `send ?Trial newRoom 199`, type `enemfu` (두드려) Return, `zlvhs` (키폰) Return twice | log `answer "두드려" reads as "knock rap"`, `answer "키폰" reads as "keapon laffin"`; the door asks the next question (`마법 상점 주인에게 가장 어울리는 원소는 무엇인가?`) |
| the same with the P3 binary (red) | `r4p3-qfg2` | the door refuses: `나를 찾기 전에 누가 그대를 보냈는지부터 더 알아보게.` (text 199 #14, "learn more about who sent you") |
| LSL1 font ids | `r3-lsl1ega` with a debug line | `hi-res font set for font 59 (asked 2107)`, `font 0 (asked 0)`, `font 1207 (asked 1207)` |
| LSL1 VGA, 256 colours | `r3-lsl1vga` | age dialog `그런데 나이가 어떻게 되시나요?` with six Hangul buttons over the VGA picture; the reply box after "15세 미만"; legible, palette right |
| QFG2 room 199 text | `r4-qfg2` | `여자 목소리:` + question line + edit box with `키폰` typed (composer) |

Not run in the prototype (planned as [data] steps, UNVERIFIED until then): the DYNMEM growth in play (no P4 run has
hit a DYNMEM cut yet; the debug line exists to find one), QFG1 EGA rooms 22/29, KQ4/LB1/Camelot typed commands and
save/load, the `sci11StringsStart` hook on QFG1 VGA, rgb565 runs of the P4 games.

### 0.4 ScummVM/SCI APIs used (file:line at `5741781806a`)

| API / hook site | Where |
|---|---|
| `Script::identifyOffsets` SCI1.1 branch (`getSciVersion() >= SCI_VERSION_1_1 && <= SCI_VERSION_2_1_LATE` 401, `_codeOffset = hunkPtr - *_buf;` 483); `stringIdAtOffset` 231; `stringBytesAt` 240 | `engines/sci/engine/script.cpp` |
| `ScriptStrings::sci11StringsStart(const byte *, uint)` (.h 205, .cpp 279); its test `test_sci11StringsStart` | `engines/sci/engine/script_strings.{h,cpp}`, `test/engines/sci/script_strings.h:85` |
| `SegManager::strncpy(reg_t, const char *, size_t)` 695, `(reg_t, reg_t, size_t)` 727, `stringCapacity` 898, `strcpyBounded` 919, `allocDynmem` 963, `getSegmentType` 239; `_heap` (.h 481), `strcpyBounded` (.h 326) | `engines/sci/engine/seg_manager.{h,cpp}` |
| `DynMem` (`_size`, `_buf`, `_description`) 382; `DynMem::dereference` (`maxSize = _size - offset`) segment.cpp 241 | `engines/sci/engine/segment.{h,cpp}` |
| `utf8Truncate` 117, `utf8ReplaceAt` 128 | `engines/sci/utf8.h` |
| `strAtUtf8` 159 (write rebuilds through `strcpyBounded` 178), `kStrAt` 193 (TTS line 210), `kFormat` result `strcpyBounded` 535, `kStrLen` 575 | `engines/sci/engine/kstring.cpp` |
| `KoreanParser` 41, `askAbout` 71, end of namespace 82 | `engines/sci/parser/korean_parser.h` |
| `Vocabulary::tokenizeString` 760 (`_koreanFallback.clear()` 772), `lookupKorean` 743; `_koreanFallback` (.h 420), `hasKoreanWords` (.h 440); `WordMap`, `ResultWord`, `ResultWordList(List)` (.h 120-128) | `engines/sci/parser/vocabulary.{h,cpp}` |
| `SciEngine::getGameId` 195, `hasParser` 218, `getVocabulary` 237 | `engines/sci/sci.h` |
| `EngineState::currentRoomNumber` 176 | `engines/sci/engine/state.h` |
| `GID_QFG1` 110, `GID_QFG1VGA` 111, `GID_QFG2` 112 | `engines/sci/detection.h` |
| `GfxFontFromResource` drops the high bits of a missing font id (`if (!resMan->testResource(...)) resourceId &= 0x7ff`, "lsl1sci mixes its own internal fonts") 201-202 | `engines/sci/graphics/scifont.cpp` |
| `GfxCache::hiResFont` 66 (`getFontSet(fontId)` 73), `getFont` 77 | `engines/sci/graphics/cache.cpp` |
| driver row `KO_KOR -> UpscaledGfx` for SCI0_EARLY..SCI1_1 96 | `engines/sci/graphics/drivers/init.cpp` |
| `textModeInputs` any-case table search 255-263 (review M1 fix) | `engines/sci/sci.cpp` |
| `Common::install_null_g_system` / `uninstall_null_g_system` | `test/system/null_osystem.h:5-6` |
| `KoreanVocabulary::loadFromStream(in, magic, name, expected)` 84, `getVersion`, `size`; `TranslationFileHeader` 40-59 | `common/korean_vocab.h`, `common/translation_file.h` |
| SCI debugger console: `send` (`?Name` object lookup), `version`, `vmvars` | `engines/sci/console.cpp` (`version` prints "Emulated interpreter version" 492) |
| `test/module.mk` SCI block: `TESTS += $(srcdir)/test/engines/sci/*.h` (a new suite file is picked up by the glob) | `test/module.mk:63-66` |

### 0.5 P0-P3 interfaces P4 relies on (Task a0 re-greps them; nothing else of those phases is used)

| Interface | Phase | Used by |
|---|---|---|
| `Sci::TextMode::utf8()`, `SciEngine::getTextMode()` | P3 | every P4 hook's gate |
| `ScriptStrings::loadFromStream(in, name, expected)`, `getHeader()`, `entryCount()`, `sci11StringsStart` | P3 | pack suite, a1 |
| `KoreanParser(words, vocab, channel)`, `lookup`, `WordMap` | P3 | a2, a3 |
| `Vocabulary::tokenizeString`, `_koreanFallback`, `_parserWords`, `_koreanVocab` | P3 | a3 |
| `SegManager::strncpy`, `stringCapacity`, `strcpyBounded`; `utf8Truncate` | P3 + review fix M3 | a4 |
| `strAtUtf8` / `kStrAt` / `kStrLen` (UTF-8 paths) | P3 + review fix M2 | a3 |
| `GfxCache::hiResFont`, `SciHiResText::getFontSet(int)` | P3 | a5 |
| `Common::KoreanVocabulary` v3 header check, `Common::TranslationFileHeader` | P2 | pack suite |
| `tools/korean/strcheck.py header|check`, `tools/korean/k6bake.py build --game --fingerprint --vocab-md5` | P2/P3 | section 4 data prep |
| `korean-default.map` + `NEODG16A.SVF` (engine fallback) | P1/P3 | every run until the v3 maps exist |
| `kDebugLevelKorean` ("Korean" debug channel) | P3 | the new debug lines |

The P3 review (`P3-review.md`) and its fixes are taken as landed; P4 relies on the fixed `kStrAt` write (M2) only
through `strAtUtf8`'s unchanged signature, and on `strcpyBounded` (M3) as the one place a Korean `kFormat`/`kStrAt`
result is written. Nothing in P4 depends on a review item still open (m2 keymap defaults, m3 edit `max`, m6 fallback
tree): those are recorded in section 1.

### 0.6 UNVERIFIED (said again where used)

1. The answer-room rows (QFG1 rooms 22 and 29 with script 802; QFG2 room 199 with script 2) come from `jamo`
   commits `40e65ff93ac` and `e5295f1b411`; QFG2 199 is re-proven here (run r4), the QFG1 rows only by `jamo`'s notes
   until Task c3.
2. Whether any P4 game copies a translation into a whole DYNMEM block that is too small (the reason for Task a4):
   not observed in any run; `jamo d08e955e656` added the growth without naming the game.
3. QFG1 VGA (not a P4 game) as the data check of Task a1: its `ko/sci-ko.str` has script 990 entries (`990 3 로드`,
   `990 5 덮기`, ...); that the hook makes them land is UNVERIFIED until Task a1 Step 5 runs.
4. k16said numbers of every P4 game on this tree (needs `ko/07-debugsocket`).
5. Camelot's original save list on a UTF-8 heap with the review-M2 `kStrAt` (byte offsets past the end: unit-tested by
   `utf8ReplaceAt`'s `kUtf8ByteOffset`, not run on Camelot yet).
6. Typing through an OS IME (no IME under Xvfb); all typed checks use the built-in composer.
7. Win32/macOS/Android packaging of the data (as P3: not in scope).

## 1. Scope decisions and conflicts with DESIGN.md (listed again in the report)

1. **"No engine change except the per-game table" (row P4) vs what P3 deferred to P4.** P3 (plan section 1 item 14)
   left four engine pieces to P4: the `sci11StringsStart` hook, `koreanAnswerView` + per-game tables, growing a DYNMEM
   buffer, `|code|` font changes; the review added a per-game capacity table (M3) and the 6 px font height (m5).
   Resolution, each by measured need:
   - per-game table + `englishLine` + the `kStrAt`/`kStrLen` view: **done** (P4a a2/a3); QFG2 needs it (run r4).
   - hi-res font set by the loaded font resource: **done** (a5); LSL1 needs it (`2107 -> 59`), and it removes the
     maps' `+2048/+4096` copies. Not a per-game rule: the same mask `GfxFontFromResource` applies.
   - DYNMEM growth: **done** (a4), small and generic, with a debug line; its need is UNVERIFIED (section 0.6 item 2).
   - `sci11StringsStart` hook: **done** (a1), gated to SCI1.1 + UTF-8; no P4 game is SCI1.1 (section 0.1), checked on
     the QFG1 VGA pack.
   - `|code|` font changes: **not done**. No P4 game is SCI1.1, where upstream processes codes; it goes to the phase that
     brings the first SCI1.1 Korean game (QFG1 VGA, LB2, SQ4, PQ2 are harness packs, none in DESIGN's phases yet).
   - per-game capacity table (review M3): **not done**. When a P4 smoke run shows the `kFormat` longer-result
     diagnostic on a stack/local buffer, the **harness shortens the translation** (DESIGN section 13 risk: "per-game
     hacks creeping into engine code"); the checklist records each case.
   - `StrAt` writes reading a half-built buffer (`jamo d06073697b4`, LSL1's age quiz): **conditional** (appendix A).
     The Korean LSL1 packs skip the quiz (`SCRIPT.720` in `ko/resource.msg`), and runs r3 showed only the 3 start-up
     `non-raw` warnings upstream also has. Task d4 measures; appendix A is applied only if it shows the flood.
   Engine size: about 150 added lines in 7 upstream files + the fork's `korean_parser.{h,cpp}`, plus 2 test files (DESIGN estimate ~0.5k).
2. **k16said baselines (row P4 "each game's k16said baseline reached") need the debug socket**, which DESIGN puts in
   topic `ko/07-debugsocket` (as P3 section 1 item 2). P4 verifies the parser by the unit tests, typed commands in play
   through the real edit control and `said()`, and records the exact `k16said.py` command per game as a step that runs
   when the socket exists. Deferred: QFG2 1654/1876, QFG1E 855/956, KQ4 464/487 (`--early`), LB1 601/744, Camelot
   549/633. LSL1 has no parser (no baseline).
3. **"maps re-paired to OFL faces" (row P4)** is owner data work (decision 3: the owner picks faces). P4 does not
   commit any map. Until a game's v3 map exists it runs on the engine fallback (NEODG16A 16 px, every font id), which is
   what every check below uses; the pixel references are therefore made with the fallback map and must be re-made when
   a map lands (section 5, H2). Review m5 (fonts at least 8 px tall with the 16 px face) is input to that map work.
4. **LB1/Camelot repackaging (decision 10)** is already done in the harness (section 0.1); the remaining harness work is
   the same as for every game (headers, table v3, maps). The engine reads neither the old CP949 data nor the banked
   fonts. LB1's `en/` folder (`SCRIPT.052`/`TEXT.052` for `language=en`, PACKS.md) is **not** read by this engine (the
   "pack played in another language" handling is dropped by decision 10): an English target on the pack folder runs
   without those release patches. Harness decision (H6); Korean play is unaffected.
5. **Camelot is conditional** (decision 10). It is the last sub-phase (P4f part 2); cutting it removes Tasks f5-f7 and
   its row of the pack suite, nothing else.
6. **"smoke per game" (row P4)** = headless Xvfb runs driven by xdotool and the SCI debugger console (no socket):
   first screen on clut8 and rgb565, one game-specific check, one typed command (parser games), a save/restore with a
   Korean name. References are sha256 of crops recorded in `P4-notes.md`, PNGs never committed (game imagery).
7. **The developer-data test** (row P3's "i18n gate with real data", never written in P3) is
   `test/engines/sci/korean_packs.h`: it reads `korean-packs/<FOLDER>/ko/` from the working directory, like P1's
   `hires-maps/` and P2's `korean-vocab/`, and skips with a visible warning. DESIGN names `SCUMMVM_TEST_I18N_DATA`; the
   working-directory form is what the tree already uses twice, so it is kept (no environment variable).
8. Branch: commit on `ko-2026.4` directly, as P0-P3 did, unless the owner names topic `ko/03-sci`.

## 2. Sub-phases and their order

| Sub-phase | What | Engine files | Commits |
|---|---|---|---|
| P4a shared | a0 base; a1 `sci11StringsStart` hook; a2 `englishLine` + answer-room table; a3 answer view in `kStrAt`/`kStrLen`; a4 DYNMEM growth; a5 font set by drawn resource; a6 pack suite; a7 PATCHES.md; a8 smoke of all seven packs | `script.cpp`, `korean_parser.{h,cpp}`, `vocabulary.{h,cpp}`, `kstring.cpp`, `seg_manager.{h,cpp}`, `cache.cpp` | 6 + 1 docs |
| P4b QFG2 | headers/table v3 check, Aziza's door, parser in play, save/load, k16said (deferred) | none | 1 docs |
| P4c QFG1 EGA | same, gargoyle (29) and hut (22) | none | 1 docs |
| P4d LSL1 EGA + VGA | font ids, VGA palette text, age/quiz path, no parser | none (appendix A if d4 says so) | 1 docs (+1) |
| P4e KQ4 | early SCI0 parser, menus, copy protection, save/load | none | 1 docs |
| P4f LB1, then Camelot (conditional) | LB1 parser + its script 52 in `resource.msg`; Camelot save list (36-byte records) | none | 1-2 docs |
| P4z | acceptance, notes | none | 1 docs |

Order by risk, highest first, so a wrong assumption costs the least work:

1. **P4a first**: every game uses it; it is the only engine work.
2. **QFG2** next: the one game whose play is blocked without engine help (Aziza's door is required for the story),
   the largest translation (2,032 strings, 2,674 words), LZW1 volumes, `vocab.900`, an SCI01-era interpreter. If the
   table design is wrong, it shows here.
3. **QFG1 EGA**: the same table rows, two rooms, a different search script (802); confirms the table generalises.
4. **LSL1 EGA + VGA**: the first 256-colour SCI game with hi-res text, font ids with high bits, game faces in `ko/`,
   the quiz path (appendix A). Rendering risk, no parser risk.
5. **KQ4**: runs today; early SCI0 parser and menu bar are the only open points.
6. **LB1, Camelot**: run today; already repackaged; Camelot last because it is conditional.

Each sub-phase is buildable (P4a: its commits; P4b-f: no code), testable (its pack row of the suite + its runs) and
committable (its notes) on its own; P4b-P4f depend only on P4a, not on each other.

## 3. Files

| File | New/Mod | Task | Purpose |
|---|---|---|---|
| `engines/sci/engine/script.cpp` | mod (+7) | a1 | SCI1.1 strings start where `sci11StringsStart` says (UTF-8 games) |
| `engines/sci/parser/korean_parser.{h,cpp}` | mod (+64) | a2 | `KoreanParser::englishLine`, `KoreanAnswerRoom`, `koreanAnswerRoom()` and its table |
| `engines/sci/parser/vocabulary.{h,cpp}` | mod (+23) | a3 | `Vocabulary::koreanAnswerLine` (tokenize, keep the fallback tree, cache) |
| `engines/sci/engine/kstring.cpp` | mod (+22) | a3 | `koreanAnswerView` in `kStrAt` reads and `kStrLen` |
| `engines/sci/engine/seg_manager.{h,cpp}` | mod (+35) | a4 | `growDynmem`; used by `strncpy` and `strcpyBounded`; same-block source copied out |
| `engines/sci/graphics/cache.cpp` | mod (+4) | a5 | the font set of the resource drawn (`& 0x7ff` as `GfxFontFromResource`) |
| `test/engines/sci/korean_parser.h` | mod (+30) | a2 | 3 tests |
| `test/engines/sci/korean_packs.h` | new (98) | a6 | developer-data suite, 7 rows |
| `docs/korean/PATCHES.md` | mod | a7 | the P4a rows |
| `docs/korean/plans/P4-notes.md` | new | a8, every sub-phase | log, hashes, manual checklists, deviations |

Style as P3 (section 2 there): GPL header only on new engine files (none here), tabs, `_camelCase`, `namespace Sci`.

## 4. Environment (every task)

```sh
SRC=/home/thkim/work/scummvm-2026.4-korean
OLD=/home/thkim/work/scummvm-korean            # read only: git -C $OLD show jamo:<path>; never check out
HAR=/home/thkim/work/scummvm                   # read only (harness); data copies go under ~/work/tmp
BLD=~/work/tmp/scummvm2026-p4build             # out of tree, SCI enabled; never configure inside $SRC
P4=~/work/tmp/p4                               # scratch: data/, datah/, runs, scripts
mkdir -p $BLD $P4 && cd $BLD
heavy $SRC/configure --disable-all-engines --enable-engine=sci --enable-debug --disable-optimizations
# configure prints "Engines (builtin):\n    SCI [SCI 0-1.1 games]"
```

Build and test: `cd $BLD && heavy make -j8 test` (last line `...OK!`), `heavy make -j8 scummvm`. One suite:
`./test/runner SciKoreanParserTestSuite`. Commit messages: `AREA: Subject` (`SCI: ...`, `TEST: ...`, `DOCS: Korean:
...`), a body, exactly one trailing line `Assisted-by: Claude Code`, no `Co-Authored-By`; check each with
`git log -1 --format=%B > ~/work/tmp/p4/msg && python3 -I devtools/check-commit-msg.py ~/work/tmp/p4/msg`.

**Game data** (scratch only; game files never enter the repository):

```sh
# shipped packs (as the harness built them) and headed copies (until the harness ships headers, H1)
mkdir -p $P4/data && cd $P4/data
for z in qfg2 qfg1ega lsl1ega lsl1vga kq4 lb1 camelot; do
  mkdir -p $z && (cd $z && unzip -q -o $HAR/dist/scummvm-game-$z-0ab70f653d3-test.zip); done
cd $P4 && rm -rf datah && cp -al data datah
T=$SRC/tools/korean
# pack  gameid    fingerprint                       vocab md5                         master folder
while read g id fp vm m; do
  ko=$(ls -d datah/$g/*/GAMES/*/ko)
  rm -f $ko/sci-ko.str.new && python3 -I $T/strcheck.py header $ko/sci-ko.str \
      --line "#! format=1 game=$id fingerprint=$fp engine=sci" -o $ko/sci-ko.str.new \
    && rm $ko/sci-ko.str && mv $ko/sci-ko.str.new $ko/sci-ko.str
  python3 -I $T/strcheck.py check $ko/sci-ko.str | tail -1
  if [ "$vm" != - ]; then rm -f $ko/sci-ko.dat; python3 -I $T/k6bake.py build \
      $HAR/harness/dos/release/$m/translation/sci-ko.dat.vocab.tsv $ko/sci-ko.dat \
      --game $id --fingerprint $fp --vocab-md5 $vm | tail -1; fi
done <<'EOF'
qfg2    qfg2     97c9d236d0f581f496021ff95e091630 d8cfdb9771b88b9c4e8c7d620675e1b9 qfg2
qfg1ega qfg1     10f6eb151614d69befc60b70db1869b4 87d6713b42f26d10b7b1791449d66074 qfg1e
lsl1ega lsl1sci  71e4c864f0be154d11f0965a2906370e -                                lsl1e
lsl1vga lsl1sci  71e4c864f0be154d11f0965a2906370e -                                lsl1e
kq4     kq4sci   388d93cbfef9ba97d8d681e746b2e05a cc12dd209ebf67837113a035bb45f1e3 kq4
lb1     laurabow e7528cb4f9ae1c5d8f39c6ac1eccbe43 dfec1d84860715fffa1bdff1eec2e640 lb1
camelot camelot  b864990d81c9a5a090795686b4895d12 f0016e432faf94bcc041078483c77282 camelot
EOF
```

Expected (prototype): `... QFG2/ko/sci-ko.str: 2032 entries, header yes` and `... sci-ko.dat: SCIKOR version 3, 2674
entries, 45484 bytes, sha256 ce1ed778...`; QFG1E 175 / 2247 (38,234 B, `d7df25fd...`); LSL1 703 (no table); KQ4 222 /
1168 (20,156 B, `5c5e8e76...`); LB1 91 / 2017 (35,086 B, `139f864d...`); Camelot 118 / 5362 (93,774 B, `083e72a4...`).
When the harness ships headed zips (H1), unzip those into `$P4/datah` instead and skip the loop.

The pack suite reads `korean-packs/` in the runner's working directory:

```sh
cd $BLD && rm -rf korean-packs && mkdir korean-packs
for g in qfg2 qfg1ega lsl1ega lsl1vga kq4 lb1 camelot; do
  d=$(ls -d $P4/datah/$g/*/GAMES/*/); ln -s ${d%/} korean-packs/$(basename ${d%/}); done
```

**Headless runner** (scratch, not committed): `$P4/go1.sh` is P3's `~/work/tmp/p3-kq1/go1.sh` unchanged (Xvfb,
`windowfocus`, steps `sleep:N`, `type:TEXT`, `click:X,Y`, xdotool key names; a final `alt+s` screenshot; `BIN` env
selects the binary, default must be set to `$BLD/scummvm`); `$P4/go.sh` retries `go1.sh` up to 10 times while no PNG
appears (the SDL_BlitSurface start flake); `$P4/mkrun.sh RUNDIR TARGET GAMEID PATH [key=value...]` writes
`RUNDIR/run.ini` with `[scummvm] extrapath=$SRC/dists/engine-data autosave_period=0 screenshotpath=RUNDIR` and one
target `engineid=sci gameid=... path=... savepath=RUNDIR` plus the extra keys. Copy them from `~/work/tmp/p4-proto/`
(`sed -i 's#p4-proto#p4#' $P4/go.sh`). Debugger console: `ctrl+alt+d` opens it, `type:...` + `Return` runs a command,
`type:exit` + `Return` closes it. Intermediate screenshots: put `alt+s` among the steps.

Built-in composer keys used below (2-set): 봐 `qhk`, 문을 `ansdmf`, 열어 `dufdj`, 두드려 `enemfu`, 키폰 `zlvhs`,
펜러스 `vpsfjtm`, 에라스무스 `dpfktmanta`.

## 5. Harness-side dependency list (the harness repository is not modified by this plan)

What must exist before a game's acceptance can be run on harness output instead of the scratch copies of section 4.
Every item is harness work (`~/work/scummvm`, committed and pushed there, memory `translations-in-harness`).

| # | Item | Games | Needed by |
|---|---|---|---|
| H1 | Builders write the fingerprint header into `ko/sci-ko.str` (`tools/korean/strcheck.py header`, from this tree, decision 7) and build `ko/sci-ko.dat` as **version 3** (`tools/korean/k6bake.py build --game --fingerprint --vocab-md5`) with the values of section 4; the committed translation files and `translations.tsv` sha256 updated so `pack_common.make_zip` accepts them | all seven (LSL1: `.str` only) | pack suite row green on a harness zip; "no header warning" step of each game |
| H2 | v3 maps (`[map] version=3`, the keys P1 dropped removed) re-paired to OFL faces chosen by the owner (decision 3; `per-game-font-workflow`), for `hiresbit.map` (1 bpp) and `hiresvf.map` (2 bpp); `hiresttf.map` optional (FreeType builds). Inputs: review m5 (with a 16 px face every font id is at least 8 px tall: route 6 px menu/status fonts to a smaller face or `original`) | all seven | the "game map" acceptance line (until then: fallback map, section 1 item 3) |
| H3 | Game-only faces converted to SVF v3 (`tools/svf2to3.py` of svf-fonts-open) or replaced by OFL faces: LSL1 `SMYJO18`, `AMYJO18`, `JGODK18`, `TBGHN40` (+ `.HOT`), LB1 `DIPHY36`, `YOUTH36`, `DKBHG40`, Camelot `GBATB17O`, `NEODG16O` | LSL1, LB1, Camelot | H2 for those games |
| H4 | LSL1 maps: drop the sections repeated for font ids `+2048` and `+4096` once Task a5 is in the engine they target (they become dead; harmless if kept) | LSL1 | none (cleanup) |
| H5 | Rebuilt game zips from one harness commit (`build_sci_qfg2.py`, `build_sci_qfg1e.py`, `build_sci_lsl1e.py [--vga]`, `build_sci_kq4.py`, `repack_sci.py --game lb1|camelot`), named `scummvm-game-<pack>-<sha>-test.zip` | all | final acceptance on harness output |
| H6 | LB1 `en/` (`SCRIPT.052`, `TEXT.052`): the new engine never reads it (decision 10). Decide: drop it, or document that the English target on the pack folder lacks those release patches | LB1 | the English-target check of P4f |
| H7 | Keep the language-independent patches in the game folder: KQ4 `SCRIPT.701` (copy protection), LB1 `SCRIPT.414`, QFG2 `SCRIPT.805`; keep `SCRIPT.720` (LSL1 quiz skip) and LB1's Korean script 52 in `ko/resource.msg` | KQ4, LB1, QFG2, LSL1 | the game runs as in `jamo` |
| H8 | `k16said.py` + `k15parse.py` against this tree's socket (topic `ko/07`), walkthrough pairs per game (`walk/<game>-pairs.tsv`), `--early` for KQ4 | parser games | the deferred k16said steps |
| H9 | Any translation a smoke run shows too long for its buffer (the `kFormat` "longer than the English one" diagnostic on a stack/local buffer, or a `strcpyBounded ... cut` / `strncpy ... cut` warning): shortened in the master | per finding | the "no cut on the smoke paths" acceptance line |

Per game, before its sub-phase can close on harness output: QFG2 H1, H5 (+H2 for the map line); QFG1E H1, H5; LSL1
H1, H3, H5 (H4 optional); KQ4 H1, H5, H7; LB1 H1, H3, H5, H6, H7; Camelot H1, H3, H5. Until then every step runs on
`$P4/datah` and says so in the notes.

---

## P4a: shared engine pieces

### Task a0: Base, interfaces, data

**Files:** none.

- [ ] **Step 1: base** — `git -C $SRC log --oneline -1` shows `5741781806a` or a later commit with
  `git diff 5741781806a.. -- engines test` empty. Configure `$BLD` (section 4), `heavy make -j8 test`:
  expected `Running cxxtest tests (675 tests)...OK!` (= N0).
- [ ] **Step 2: interfaces** — each prints at least one line:

```sh
cd $SRC
grep -n "static uint sci11StringsStart" engines/sci/engine/script_strings.h
grep -n "_codeOffset = hunkPtr - \*_buf;" engines/sci/engine/script.cpp
grep -n "uint strcpyBounded\|uint32 stringCapacity" engines/sci/engine/seg_manager.h
grep -n "Common::String utf8Truncate\|Utf8WriteResult utf8ReplaceAt" engines/sci/utf8.h
grep -n "static reg_t strAtUtf8\|^reg_t kStrLen" engines/sci/engine/kstring.cpp
grep -n "void askAbout(ResultWordListList" engines/sci/parser/korean_parser.h
grep -n "ResultWordListList _koreanFallback;" engines/sci/parser/vocabulary.h
grep -n "GfxFont \*GfxCache::hiResFont" engines/sci/graphics/cache.cpp
grep -n "resourceId = resourceId & 0x7ff" engines/sci/graphics/scifont.cpp
grep -n "GID_QFG1,\|GID_QFG2," engines/sci/detection.h
grep -n "void install_null_g_system" test/system/null_osystem.h
```

- [ ] **Step 3: data** — section 4's two blocks; each of the seven `ls -d $P4/datah/<pack>/*/GAMES/*/ko` exists and the
  loop prints the counts listed there. Missing data: the tasks still run; only **[data]** steps are skipped and reported.

No commit.

### Task a1: SCI1.1 strings start where the translation counts them

**Files:** Modify `engines/sci/engine/script.cpp` (SCI1.1 branch of `identifyOffsets`).

No red unit test is possible (`identifyOffsets` needs a loaded script and `g_sci`); the string logic is already tested
(`test_sci11StringsStart`). The observable is Step 5.

- [ ] **Step 1:** after `_codeOffset = hunkPtr - *_buf;` (line 483) insert:

```cpp
		// The loop above consumed the word that ends the instances, which is
		// the first two bytes of the strings: a heap whose strings open with
		// a NUL pair loses its first two ids. A translation's keys
		// (ScriptStrings) count them, so a UTF-8 game starts where they start.
		if (getSciVersion() == SCI_VERSION_1_1 && g_sci && g_sci->getTextMode().utf8())
			scriptDataPtr = _heap.subspan(ScriptStrings::sci11StringsStart(_heap.getUnsafeDataAt(0, _heap.size()), _heap.size()));
```

  (`ScriptStrings` comes in through `sci/sci.h`, already included at line 23.) The gate keeps SCI2-2.1 (same branch)
  and every non-UTF-8 game upstream.
- [ ] **Step 2:** `heavy make -j8 scummvm test` -> `675 tests ... OK!`, no warning from `script.cpp`.
- [ ] **Step 3:** `git commit -am` with:

```
SCI: Count the first SCI1.1 strings as a translation does

Script::identifyOffsets() consumes the word that ends the object
instances of an SCI1.1 heap, which is the first two bytes of the
strings. A heap whose strings open with a NUL pair (QFG1 VGA script
990) lost its first two string ids, so every sci-<lang>.str entry
after them named the wrong string.

On a UTF-8 (translated) game the string block now starts where
ScriptStrings::sci11StringsStart() finds it; every other game keeps
the upstream numbering.

Assisted-by: Claude Code
```

- [ ] **Step 4 [data, optional, not a P4 game]:** unzip `$HAR/dist/scummvm-game-qfg1-0ab70f653d3-test.zip` to
  `$P4/data/qfg1`, `mkrun.sh $P4/a1 qfg1-ko qfg1vga <path> language=ko originalsaveload=true`, start, F7 (restore):
  the dialog buttons read `로드`/`덮기` (script 990 ids 3, 5), not neighbouring strings. Record the screen; with the
  P3 binary (`BIN=~/work/tmp/scummvm2026-p3build/scummvm`) note what it shows. Skipped and noted if the pack is absent.

### Task a2: `englishLine` and the answer-room table

**Files:** Modify `engines/sci/parser/korean_parser.h`, `korean_parser.cpp`; test `test/engines/sci/korean_parser.h`.

- [ ] **Step 1: tests first** — append to `SciKoreanParserTestSuite` (before its closing `};`):

```cpp
	void test_englishLineListsEveryWordOfEachGroup() {
		Sci::KoreanParser p(_words, _vocab);
		add("portal", kNoun, gDoor); // a synonym in the same group
		static const char *const typed[] = { "\xEB\xAC\xB8\xEC\x9D\x84", "\xEC\x97\xB4\xEC\x96\xB4", nullptr }; // 문을 열어
		ResultWordListList words;
		Common::Array<uint8> roles;
		tokens(p, typed, words, roles);
		TS_ASSERT_EQUALS(p.englishLine(words), "door portal open");
	}

	void test_englishLineIsEmptyForATokenWithNoWord() {
		Sci::KoreanParser p(_words, _vocab);
		ResultWordListList words;
		ResultWordList none;
		ResultWord r = { kNoun, 0x7FF };
		none.push_back(r);
		words.push_back(none);
		TS_ASSERT_EQUALS(p.englishLine(words), "");
	}

	void test_answerRoomTable() {
		TS_ASSERT(Sci::koreanAnswerRoom(Sci::GID_QFG2, 199));
		TS_ASSERT(Sci::koreanAnswerRoom(Sci::GID_QFG1, 22));
		TS_ASSERT(Sci::koreanAnswerRoom(Sci::GID_QFG1, 29));
		TS_ASSERT(!Sci::koreanAnswerRoom(Sci::GID_QFG1VGA, 29));
		TS_ASSERT(!Sci::koreanAnswerRoom(Sci::GID_QFG2, 198));
		TS_ASSERT(!Sci::koreanAnswerRoom(Sci::GID_KQ1, 199));
	}
```

- [ ] **Step 2: red** — `heavy make -j8 test`: compile error `'class Sci::KoreanParser' has no member named
  'englishLine'` / `'koreanAnswerRoom' is not a member of 'Sci'`.
- [ ] **Step 3: header** — in `korean_parser.h`, after `void askAbout(ResultWordListList &words) const;`:

```cpp

	/**
	 * The parsed line in the game's own English words: for each token, every
	 * English word of every group it may be, sorted, space separated; empty
	 * when a token has none. For the scripts of koreanAnswerRoom(), which
	 * search the typed line for a word instead of parsing it.
	 */
	Common::String englishLine(const ResultWordListList &words) const;
```

  and before `} // End of namespace Sci`:

```cpp
/**
 * The per-game table (DESIGN.md section 6): a room whose script checks a
 * typed answer by searching the text itself (StrLen and StrAt over the edit
 * box) for English words, with no said(). There, to those two calls only, a
 * Korean answer reads as KoreanParser::englishLine() of it.
 */
struct KoreanAnswerRoom {
	SciGameId game;
	int16 room;
	const char *what; ///< for the debug log
};

/** The table's row for @p room of @p game; nullptr when there is none. */
const KoreanAnswerRoom *koreanAnswerRoom(SciGameId game, int room);

```

- [ ] **Step 4: code** — `korean_parser.cpp`: add `#include "common/algorithm.h"` before `common/debug.h`, and before
  the closing namespace:

```cpp
Common::String KoreanParser::englishLine(const ResultWordListList &words) const {
	// Every English word of every group a token may be: a script that looks
	// for one word ("stefan") must find it whichever synonym ("baron") or
	// reading the Korean word maps to.
	Common::String line;
	for (ResultWordListList::const_iterator t = words.begin(); t != words.end(); ++t) {
		Common::Array<Common::String> found;
		for (WordMap::const_iterator w = _words.begin(); w != _words.end(); ++w) {
			bool match = false;
			for (ResultWordList::const_iterator r = w->_value.begin(); r != w->_value.end() && !match; ++r)
				for (ResultWordList::const_iterator a = t->begin(); a != t->end() && !match; ++a)
					match = a->_group == r->_group;
			if (match)
				found.push_back(w->_key);
		}
		if (found.empty())
			return Common::String();
		Common::sort(found.begin(), found.end());
		for (uint i = 0; i < found.size(); ++i) {
			if (!line.empty())
				line += ' ';
			line += found[i];
		}
	}
	return line;
}

static const KoreanAnswerRoom s_answerRooms[] = {
	// QFG1 EGA: script 802's search for the hut's rhyme and the gargoyle's answers (jamo e5295f1b411)
	{ GID_QFG1, 22, "QFG1 Baba Yaga's hut" },
	{ GID_QFG1, 29, "QFG1 the gargoyle at Erasmus's gate" },
	// QFG2: script 2's search for Aziza's questions (jamo 40e65ff93ac)
	{ GID_QFG2, 199, "QFG2 Aziza's door" }
};

const KoreanAnswerRoom *koreanAnswerRoom(SciGameId game, int room) {
	for (uint i = 0; i < ARRAYSIZE(s_answerRooms); ++i)
		if (s_answerRooms[i].game == game && s_answerRooms[i].room == room)
			return &s_answerRooms[i];
	return nullptr;
}
```

- [ ] **Step 5: green** — `heavy make -j8 test` -> `678 tests ... OK!`; `./test/runner SciKoreanParserTestSuite` OK.
- [ ] **Step 6:** commit:

```
SCI: Add the Korean answer rooms and englishLine

Some rooms check a typed answer by searching the text for English
words with StrLen and StrAt instead of calling said(): Aziza's door in
QFG2 (room 199, script 2) and, in QFG1 EGA, the gargoyle (room 29) and
Baba Yaga's hut (room 22, both script 802). A Korean answer can never
pass such a search.

koreanAnswerRoom() is the one per-game table of the Korean parser
(DESIGN.md section 6); KoreanParser::englishLine() turns a parsed
Korean line into every English word of every group each token may be,
which is what those searches look for.

Assisted-by: Claude Code
```

### Task a3: Answers read as English words in the answer rooms

**Files:** Modify `engines/sci/parser/vocabulary.h`, `vocabulary.cpp`, `engines/sci/engine/kstring.cpp`.

No red unit test is possible (kernel calls need the game); the red/green observable is Task b2 (the prototype's
r4p3 vs r4 runs, section 0.3).

- [ ] **Step 1: `vocabulary.h`** — after `ResultWordListList _koreanFallback; ...` add
  `	Common::String _answerTyped, _answerLine; ///< koreanAnswerLine()'s last answer`; after
  `bool hasKoreanWords() const ...` add:

```cpp
	/**
	 * KoreanParser::englishLine() of the typed text @p typed; empty when a
	 * word is unknown. The last answer is kept (StrAt asks once per letter).
	 */
	Common::String koreanAnswerLine(const Common::String &typed);
```

- [ ] **Step 2: `vocabulary.cpp`** — before `bool Vocabulary::tokenizeString(`:

```cpp
Common::String Vocabulary::koreanAnswerLine(const Common::String &typed) {
	if (typed == _answerTyped)
		return _answerLine;
	_answerTyped = typed;
	_answerLine.clear();
	// tokenizeString() replaces the fallback tree of the last parsed sentence: keep it.
	const ResultWordListList fallback = _koreanFallback;
	ResultWordListList words;
	char *error = nullptr;
	if (tokenizeString(words, typed.c_str(), &error) && !error)
		_answerLine = KoreanParser(_parserWords, _koreanVocab, kDebugLevelKorean).englishLine(words);
	free(error);
	_koreanFallback = fallback;
	debugC(1, kDebugLevelKorean, "answer \"%s\" reads as \"%s\"", typed.c_str(), _answerLine.c_str());
	return _answerLine;
}
```

  (`error` is `calloc`ed by `tokenizeString`, vocabulary.cpp:802, hence `free`.)
- [ ] **Step 3: `kstring.cpp`** — includes after `#include "sci/utf8.h"`: `#include "sci/parser/korean_parser.h"` and
  `#include "sci/parser/vocabulary.h"`. Before the comment `// kStrAt on a UTF-8 heap (TextMode::kUtf8)`:

```cpp
// A room of the per-game table (koreanAnswerRoom()): the typed Korean answer
// as the game's own English words, for StrLen and StrAt reads only.
static bool koreanAnswerView(EngineState *s, reg_t str, Common::String &out) {
	if (!g_sci->getTextMode().utf8() || !g_sci->hasParser() || !koreanAnswerRoom(g_sci->getGameId(), s->currentRoomNumber()))
		return false;
	const Common::String typed = s->_segMan->getString(str);
	bool ascii = true;
	for (uint i = 0; i < typed.size() && ascii; i++)
		ascii = (byte)typed[i] < 0x80;
	if (ascii)
		return false;
	out = g_sci->getVocabulary()->koreanAnswerLine(typed);
	return !out.empty();
}
```

  In `kStrAt`, between the TTS line and `if (g_sci->getTextMode().utf8()) return strAtUtf8(...)`:

```cpp
	Common::String view;
	if (argc <= 2 && koreanAnswerView(s, argv[0], view))
		return make_reg(0, offset < view.size() ? (byte)view[offset] : 0);
```

  First lines of `kStrLen`:

```cpp
	Common::String view;
	if (koreanAnswerView(s, argv[0], view))
		return make_reg(0, view.size());
```

  An English (`kSingle`) game returns at the first test of `koreanAnswerView`; an all-ASCII answer keeps the game's
  own search.
- [ ] **Step 4:** `heavy make -j8 scummvm test` -> `678 tests ... OK!`, no warning from the three files.
- [ ] **Step 5:** commit:

```
SCI: Let a Korean answer pass the answer rooms' own search

In a room of koreanAnswerRoom(), StrLen and StrAt read a typed answer
that holds Hangul as the game's own English words
(Vocabulary::koreanAnswerLine(), KoreanParser::englishLine() of the
tokenized line), so the script's substring search finds "keapon" in
"키폰". Writes and every other room are unchanged; the edit box still
shows what was typed; an answer with an unknown word reads as typed.

The fallback parse tree of the last sentence is kept across the
tokenization, so a said() retry is not disturbed.

Assisted-by: Claude Code
```

### Task a4: A dynamic block grows for a longer translation

**Files:** Modify `engines/sci/engine/seg_manager.h`, `seg_manager.cpp`.

No unit test is possible (`SegManager` needs a `ResourceManager`: its constructor calls `createClassTable()`;
`strncpy` reads `g_sci`). Verified by reading and by the debug line in the smoke runs (each sub-phase greps it).

- [ ] **Step 1: `seg_manager.h`** — after the `strcpyBounded` declaration (line 326):

```cpp

	/**
	 * Grow the dynamic memory block that starts at @p dest to @p bytes (zero-filled), for a UTF-8
	 * translation longer than the English text the script sized it for. False, and nothing changes,
	 * when @p dest is not the start of a DYNMEM block or the block is already that big.
	 */
	bool growDynmem(reg_t dest, uint32 bytes);
```

- [ ] **Step 2: `seg_manager.cpp`** — (a) in `strncpy(reg_t, const char *, size_t)` before the existing
  `if (g_sci && g_sci->getTextMode().utf8() && dest_r.maxSize > 0 && ...)` cut block:

```cpp
	if (g_sci && g_sci->getTextMode().utf8() && dest_r.maxSize >= 0 && Common::strnlen(src, n) >= (size_t)dest_r.maxSize &&
	    growDynmem(dest, Common::strnlen(src, n) + 1))
		dest_r = dereference(dest);
```

  (b) in `strncpy(reg_t, reg_t, size_t)` replace `if (src_r.isRaw) {` (the `// raw -> *` branch) by:

```cpp
	if (src_r.isRaw && g_sci && g_sci->getTextMode().utf8() && src.getSegment() == dest.getSegment()) {
		// The copy may grow the destination's block (growDynmem()), which
		// moves a source in the same block: copy it out first.
		const Common::String text((const char *)src_r.raw, Common::strnlen((const char *)src_r.raw, MIN<size_t>(n, src_r.maxSize)));
		strncpy(dest, text.c_str(), MIN<size_t>(n, text.size() + 1));
	} else if (src_r.isRaw) {
```

  (c) before `uint SegManager::strcpyBounded(`:

```cpp
bool SegManager::growDynmem(reg_t dest, uint32 bytes) {
	if (dest.getOffset() != 0 || getSegmentType(dest.getSegment()) != SEG_TYPE_DYNMEM)
		return false;
	DynMem *mem = (DynMem *)_heap[dest.getSegment()];
	if (bytes <= mem->_size)
		return false;
	byte *grown = (byte *)realloc(mem->_buf, bytes);
	if (!grown) {
		warning("SegManager::growDynmem: out of memory growing %04x:%04x to %u bytes", PRINT_REG(dest), bytes);
		return false;
	}
	memset(grown + mem->_size, 0, bytes - mem->_size);
	debugC(1, kDebugLevelKorean, "%04x:%04x (%s) grown from %u to %u bytes", PRINT_REG(dest), mem->_description.c_str(), mem->_size, bytes);
	mem->_buf = grown;
	mem->_size = bytes;
	return true;
}
```

  (d) first line of `strcpyBounded`:

```cpp
	if (g_sci && g_sci->getTextMode().utf8() && text.size() >= stringCapacity(dest))
		growDynmem(dest, text.size() + 1);
```

  `DynMem::saveLoadWithSerializer` stores `_size` and `_buf`, so a grown block survives save/restore. A stack, local or
  script-string buffer is still cut (review M3 bound), with its warning.
- [ ] **Step 3:** `heavy make -j8 scummvm test` -> `678 tests ... OK!`, no warning.
- [ ] **Step 4:** commit:

```
SCI: Grow a dynamic block for a longer Korean string

A script sizes a dynamic memory block by kStrLen, which counts code
points on a UTF-8 heap, so a translation can need up to three times
the bytes the script allocated. Such a block is only a pointer to the
script, so it can grow: SegManager::growDynmem() reallocates the whole
block (zero-filled), and strncpy() and strcpyBounded() use it before
they would cut. Any other buffer is still cut on a code point boundary
with a warning. A source in the destination's own block is copied out
before a copy that may move the block.

Assisted-by: Claude Code
```

### Task a5: The hi-res font set of the font resource drawn

**Files:** Modify `engines/sci/graphics/cache.cpp` (`GfxCache::hiResFont`).

- [ ] **Step 1:** in `hiResFont`, after the `if (!hires || !_screen->getTextPlane()) return font;` lines, before
  `getFontSet(fontId)`:

```cpp
	// The map names the font resource drawn: lsl1sci's ids carry high bits
	// that GfxFontFromResource drops (2107 is font 59), and so does this.
	if (!_resMan->testResource(ResourceId(kResourceTypeFont, fontId)))
		fontId &= 0x7ff;
	debugC(1, kDebugLevelKorean, "Hi-res font set for font %d", fontId);
```

- [ ] **Step 2:** `heavy make -j8 scummvm test` -> `678 ... OK!`.
- [ ] **Step 3 [data]:** `mkrun.sh $P4/a5 lsl1ega-ko lsl1sci $P4/datah/lsl1ega/LSL1EGA/GAMES/LSL1EGA language=ko`;
  `go.sh $P4/a5 lsl1ega-ko 15 Return sleep:10 alt+s sleep:2 Return sleep:10 alt+s sleep:2 Escape sleep:15` (with fewer steps the game may not reach the age dialog and only font 0 is logged); `grep 'font set' $P4/a5/run.log` prints `... font 0`,
  `... font 59`, `... font 1207` and **no** id >= 2048 (the prototype's extra `(asked 2107)` shows the 59 came from 2107).
- [ ] **Step 4:** commit:

```
SCI: Pick the hi-res font set of the font resource drawn

lsl1sci draws with font ids that carry high bits (2107);
GfxFontFromResource drops them when no such font exists and draws font
59. The hi-res font set is now chosen the same way, so a Korean map
names font 59 once instead of repeating its section for +2048 and
+4096.

Assisted-by: Claude Code
```

### Task a6: The packs' tables load with their game's header (developer data)

**Files:** Create `test/engines/sci/korean_packs.h` (picked up by the `test/engines/sci/*.h` glob).

- [ ] **Step 1: test** — the file (the prototype's, verbatim):

```cpp
#include <cxxtest/TestSuite.h>

#include "common/fs.h"
#include "common/korean_vocab.h"
#include "common/ptr.h"
#include "common/stream.h"

#include "sci/engine/script_strings.h"

#include "test/system/null_osystem.h"

// Developer data (DESIGN.md section 10, "i18n gate with real data"): when the
// working directory has korean-packs/<FOLDER>/ko/ (a game folder of a
// harness pack, or a link to one), its tables load with this game's header:
// sci-ko.str through ScriptStrings, sci-ko.dat through KoreanVocabulary.
// The fingerprints are what the engine prints for the game ("This game's
// sci-ko.str header line is: ..."). Passes without the directory.
class SciKoreanPacksTestSuite : public CxxTest::TestSuite {
	struct Pack {
		const char *folder;      // GAMES/<FOLDER> of the pack
		const char *game;        // detection game id
		const char *fingerprint; // script 0 + main vocabulary
		const char *vocab;       // the vocabulary alone; nullptr: no parser table
		uint strings;            // sci-ko.str entries
		uint words;              // sci-ko.dat entries
	};

	static Common::TranslationFileHeader header(const Pack &p, bool withVocab) {
		Common::TranslationFileHeader h;
		h.format = 1;
		h.game = p.game;
		h.engine = "sci";
		h.fingerprint = p.fingerprint;
		if (withVocab && p.vocab)
			h.vocab = p.vocab;
		return h;
	}

	static void check(const Common::FSNode &ko, const Pack &p) {
		Common::ScopedPtr<Common::SeekableReadStream> str(ko.getChild("sci-ko.str").createReadStream());
		TSM_ASSERT(p.folder, str);
		if (str) {
			Sci::ScriptStrings t;
			TSM_ASSERT(p.folder, t.loadFromStream(*str, "sci-ko.str", header(p, false)));
			TSM_ASSERT(p.folder, t.getHeader().present());
			TSM_ASSERT_EQUALS(p.folder, t.entryCount(), p.strings);
		}
		if (!p.vocab)
			return;
		Common::ScopedPtr<Common::SeekableReadStream> dat(ko.getChild("sci-ko.dat").createReadStream());
		TSM_ASSERT(p.folder, dat);
		if (dat) {
			Common::KoreanVocabulary v;
			TSM_ASSERT(p.folder, v.loadFromStream(*dat, "SCIKOR", "sci-ko.dat", header(p, true)));
			TSM_ASSERT_EQUALS(p.folder, v.getVersion(), 3u);
			TSM_ASSERT_EQUALS(p.folder, v.size(), p.words);
		}
	}

public:
	void setUp() override {
		Common::install_null_g_system();
	}
	void tearDown() override {
		Common::uninstall_null_g_system();
	}

	void test_packs_when_given() {
		static const Pack packs[] = {
			{ "QFG2", "qfg2", "97c9d236d0f581f496021ff95e091630", "d8cfdb9771b88b9c4e8c7d620675e1b9", 2032, 2674 },
			{ "QFG1EGA", "qfg1", "10f6eb151614d69befc60b70db1869b4", "87d6713b42f26d10b7b1791449d66074", 175, 2247 },
			{ "LSL1EGA", "lsl1sci", "71e4c864f0be154d11f0965a2906370e", nullptr, 703, 0 },
			{ "LSL1VGA", "lsl1sci", "71e4c864f0be154d11f0965a2906370e", nullptr, 703, 0 },
			{ "KQ4", "kq4sci", "388d93cbfef9ba97d8d681e746b2e05a", "cc12dd209ebf67837113a035bb45f1e3", 222, 1168 },
			{ "LB1", "laurabow", "e7528cb4f9ae1c5d8f39c6ac1eccbe43", "dfec1d84860715fffa1bdff1eec2e640", 91, 2017 },
			{ "CAMELOT", "camelot", "b864990d81c9a5a090795686b4895d12", "f0016e432faf94bcc041078483c77282", 118, 5362 },
		};
		const Common::FSNode dir(Common::Path("korean-packs"));
		if (!dir.isDirectory()) {
			TS_WARN("korean-packs/ is not in the working directory: pack table check skipped");
			return;
		}
		uint seen = 0;
		for (uint i = 0; i < ARRAYSIZE(packs); ++i) {
			const Common::FSNode ko = dir.getChild(packs[i].folder).getChild("ko");
			if (!ko.isDirectory())
				continue;
			check(ko, packs[i]);
			++seen;
		}
		TS_ASSERT(seen > 0);
	}
};
```

  (Camelot's row goes with Camelot if it is cut, decision 10.) The counts are the harness's committed tables
  (2026-10-17); a translation update in the harness changes them, and the row is edited with it (the failure names the
  folder).
- [ ] **Step 2: red [data]** — `korean-packs/` linked to the **shipped** packs (`$P4/data`, section 4 with `data` for
  `datah`): `./test/runner SciKoreanPacksTestSuite` fails per parser game with `Assertion failed:
  t.getHeader().present()` and `Expected (v.getVersion() == 3u), found (2 != 3)` (prototype: QFG2 and KQ4 shown).
- [ ] **Step 3: green [data]** — linked to `$P4/datah`: `OK!`. Without the folder: `Warning: korean-packs/ is not in the
  working directory: pack table check skipped` and `OK!`. Full `heavy make -j8 test` -> `679 tests ... OK!`.
- [ ] **Step 4:** commit:

```
TEST: Check the Korean SCI packs' tables when they are given

With korean-packs/<FOLDER>/ko/ in the working directory (the game
folder of a harness pack), SciKoreanPacksTestSuite loads each pack's
sci-ko.str and sci-ko.dat with the header the engine expects for that
game (format 1, game id, fingerprint, vocab md5) and checks the entry
counts: QFG2, QFG1 EGA, LSL1 EGA and VGA, KQ4, LB1, Camelot. Without
the folder it says it skipped.

Assisted-by: Claude Code
```

### Task a7: PATCHES.md

**Files:** Modify `docs/korean/PATCHES.md`.

- [ ] **Step 1:** add `ko/03-sci` rows (P4): `engines/sci/engine/script.cpp` (SCI1.1 string start for UTF-8 games;
  merge with the existing row), `engines/sci/parser/korean_parser.{h,cpp}` is new (not listed), `vocabulary.{h,cpp}`
  (`koreanAnswerLine`), `kstring.cpp` (answer view), `seg_manager.{h,cpp}` (`growDynmem`), `graphics/cache.cpp` (font
  set by drawn resource). Check: `git diff --stat 5741781806a.. -- engines test` lists exactly these plus
  `test/engines/sci/korean_parser.h` and the new `korean_packs.h`.
- [ ] **Step 2:** commit `DOCS: Korean: Record the P4 engine patches` (body: one line per file, then
  `Assisted-by: Claude Code`).

### Task a8: [data] Smoke of the seven packs on the P4a build

**Files:** create `docs/korean/plans/P4-notes.md` (the log; same table form as `P3-notes.md`).

- [ ] **Step 1:** for each pack `g` in `qfg2 qfg1ega lsl1ega lsl1vga kq4 lb1 camelot` with its gameid (section 4):
  `mkrun.sh $P4/a8-$g $g-ko <gameid> <datah path> language=ko`, then `go.sh $P4/a8-$g $g-ko 25` (the seven may run
  in parallel, each picks its own display). Expected per log: `[korvocab] loaded N Korean words (version 3)` for the
  parser games, no `sci-ko` header warning, `Hi-res text: using data:hires_text/maps/korean-default.map`, no
  `cut to`, no `AddressSanitizer`. View every PNG: the first screens of section 0.2.
- [ ] **Step 2:** the English target on the same folder (`mkrun.sh $P4/a8-$g-en $g <gameid> <path>`, no `language`):
  320x200 window, English text, the log has no `Hi-res text`, `korvocab` or `sci-ko` line (upstream path).
- [ ] **Step 3:** write `P4-notes.md` (base, N0 = 675, a1-a7 shas and counts, the a8 table), commit
  `DOCS: Korean: Start the P4 notes`.

### Review checkpoint A (after Task a8)

- `make test` 679 OK on `$BLD`; an ASan build (`--enable-asan`, `~/work/tmp/scummvm2026-p4asan`) 679 OK with 0
  sanitizer lines, and the QFG2 run of Task b2 on it clean; a no-engine build (`~/work/tmp/scummvm2026-p2verify`)
  still 620 OK; `sync-svf.sh --check`, `mkgolden.py --check`, `test_k6bake.py`, `test_strcheck.py` rc 0.
- Self-read for upstream inertness: every new line is behind `getTextMode().utf8()` (a1, a3, a4), `koreanAnswerRoom`
  (a3) or a non-null hi-res object (a5: `hiResFont` returns early without one). `git diff 5741781806a.. -- engines`
  has no `GID_` outside `korean_parser.cpp`.
- A reviewer (fresh context) reads the diff and this section; findings go back to the implementer (memory
  `review-fix-resume`), fixes as separate commits recorded in `P4-notes.md`.

---

## P4b: QFG2

### Task b1: [data] Tables and first screens

- [ ] **Step 1:** `korean-packs/QFG2` -> `$P4/datah/qfg2/QFG2/GAMES/QFG2`; `./test/runner SciKoreanPacksTestSuite` OK.
- [ ] **Step 2:** `mkrun.sh $P4/b1 qfg2-ko qfg2 <path> language=ko`; `go.sh $P4/b1 qfg2-ko 25`: the piracy notice in
  Hangul (`지금 사용하시는 이 소프트웨어는 ...`, ten centred lines). Same with `render_target=rgb565` (second run dir).
  Record the sha256 of the two PNGs and of the notice crop; the status line after the console teleport of Task b2
  (`퀘스트 포 글로리 2:  불의 시련  [0 / 500]`) cropped rows 0-16, which must be identical on clut8 and rgb565 (1 bpp face).

### Task b2: [data] Aziza's door (room 199)

- [ ] **Step 1: red (optional, documents the need)** — `BIN=~/work/tmp/scummvm2026-p3build/scummvm go.sh $P4/b2red
  qfg2-ko 20 Return sleep:6 ctrl+alt+d sleep:3 "type:send ?Trial newRoom 199" Return sleep:2 type:exit Return sleep:10
  Return sleep:6 type:enemfu Return sleep:6 type:zlvhs Return sleep:5 type:zlvhs Return sleep:5 alt+s`: the door
  answers `나를 찾기 전에 누가 그대를 보냈는지부터 더 알아보게.`
- [ ] **Step 2: green** — the same steps with `$BLD/scummvm` and `--debugflags=Korean` (go1.sh passes it): log
  `answer "두드려" reads as "knock rap"`, `answer "키폰" reads as "keapon laffin"`; the screen shows the next question
  (one of text 199 #9-#13, chosen by the game at random).
- [ ] **Step 3:** answer that question in Korean (manual table: #9 Rakeesh's leg -> the demon (`악마`); #10 Ja'afar's
  role -> UNVERIFIED answer; #11 element of the magic shop owner -> air (`공기`); #12 element of WIT -> UNVERIFIED;
  #13 companion of Erasmus -> `펜러스` (`vpsfjtm`), which the table reads as `fenrus mouse rat`). For each question
  the game asked, record the Korean typed, the log's `reads as` line and whether the door opened (`내 집에 자유로이
  들어오게, %s.`, text #18). A question whose Korean answer the table does not know is a harness finding (k3map, H1),
  not an engine change. The answers marked UNVERIFIED are filled in from the English game's own search words
  (`git -C $OLD show jamo:` notes or the script 2 dump) when the step is run.
- [ ] **Step 4:** the same door with an English answer (`F12` toggles the composer off, type `keapon`): passes as in
  upstream (the view is not used for ASCII).

### Task b3: [data] Parser and save/load in play

- [ ] **Step 1:** in a town room (console `send ?Trial newRoom 300` or wherever the game starts after b2's notice;
  record the room), type `qhk` (봐) Return: a Korean room description; `ansdmf enemfu` (문을 두드려) in front of a
  door: the game's Korean reply. Compare the meaning with the English target and `look` / `knock on door`.
- [ ] **Step 2:** `originalsaveload=true`: F5, a Korean name (`ans`, 문), Return; F7 lists `문`; restore returns to the
  room. Then with `originalsaveload=false`, the ScummVM save dialog shows the name (legibility needs `fonts-cjk.dat`:
  UNVERIFIED headless, review note).
- [ ] **Step 3:** `grep -c 'cut to\|grown from\|longer than the English' $P4/b*/run.log` recorded; each line found is a
  harness finding (H9) with its key.

### Task b4: k16said (runs when `ko/07-debugsocket` exists)

- [ ] **Step 1:** `python3 $HAR/harness/i18n/k16said.py --game qfg2 --bin $BLD/scummvm ...` (exact arguments as
  P3 Task 20; QFG2 without `--early`): `pass >= 1654` of 1876. Until the socket exists: recorded as pending.

### Task b5: Notes

- [ ] **Step 1:** `P4-notes.md`: the b1-b4 results, hashes, the answer table, findings; commit
  `DOCS: Korean: Record the QFG2 runs`.

### Review checkpoint B (after Task b5)

The answer-room design is right if b2 passed with no engine change beyond P4a; if a question needed more (a word the
table cannot give, a script that reads the box differently), stop and decide with the owner before P4c (DESIGN risk:
per-game hacks).

## P4c: QFG1 EGA

### Task c1: [data] Tables and first screens

- [ ] As b1 for `QFG1EGA` (gameid `qfg1`): suite row OK; first screen clut8/rgb565 recorded.

### Task c2: [data] The gargoyle (room 29) and the hut (room 22)

- [ ] **Step 1:** create a hero (the game's character screen; record the keys) or use a saved hero; teleport by the
  global (memory `qfg1e-korean`: `send ?HQ newRoom 29` breaks; use the console `vmvars g 13 29`, then walk). Expected:
  the gargoyle's questions in Hangul; Korean answers (`에라스무스` = `dpfktmanta` for "Erasmus") give log lines
  `answer "..." reads as "erasmus"` and the gargoyle lets the hero pass; the P3 binary (red, optional) does not.
- [ ] **Step 2:** room 22, Baba Yaga's hut rhyme: the Korean rhyme answer (from the harness translation, its English
  "Hut of brown, now sit down") reads as the English words; the hut sits down.
- [ ] **Step 3:** a parser command (`qhk`) and a save/load as b3; record `cut`/`grown` lines.
- [ ] **Step 4:** k16said `pass >= 855` of 956 (pending socket).
- [ ] **Step 5:** notes; commit `DOCS: Korean: Record the QFG1 EGA runs`.

Rows 22 and 29 are UNVERIFIED until Steps 1-2 run (section 0.6 item 1); if either room does not need the view (the
game parses the answer with `said()`), its row is removed in a `SCI:` commit with the run as evidence.

## P4d: LSL1 EGA and VGA

### Task d1: [data] Tables, first screens, fonts

- [ ] **Step 1:** suite rows `LSL1EGA`, `LSL1VGA` OK.
- [ ] **Step 2:** `go.sh ... 15 Return sleep:10 alt+s sleep:2 Return sleep:10 alt+s` on both: the age dialog
  `그런데 나이가 어떻게 되시나요?` with the six age buttons; log `Hi-res font set for font 59` (Task a5) and no id >= 2048.
- [ ] **Step 3:** VGA on `render_target=rgb565` and `clut8`: the dialog over the 256-colour picture; record crops of
  the dialog box (identical on both for the 1 bpp fallback face).

### Task d2: [data] Into the game

- [ ] **Step 1:** pick an adult age (Tab to `19~39세`, Return): the pack skips the quiz (`SCRIPT.720`); the first room
  with the Korean status/icon texts. Record. Click the look icon on an object: a Korean reply box.
- [ ] **Step 2:** save/load with a Korean name through the game's save dialog (LSL1 uses the SCI1 save dialog).

### Task d3: [data] Palette and text in step (VGA)

- [ ] **Step 1:** a room with palette cycling or a fade (the title's fade-in, the first room's neon if any): Hangul
  keeps its colour index through the fade (no stale bright text over a dark frame). Manual checklist item with a
  screenshot pair.

### Task d4: [data] The quiz path (decides appendix A)

- [ ] **Step 1:** in the Korean target, use the game's own quiz key (Ctrl+Alt+X on the age screen, memory
  `lsl1-korean`) to reach a quiz question; count `grep -c 'non-raw' run.log`. Upstream and the prototype show 3 (from
  `kReadNumber` at start-up). If the count grows with each question, apply appendix A (one `SCI:` commit) and rerun:
  back to 3. Otherwise record "appendix A not needed".

### Task d5: Notes

- [ ] Notes and commit `DOCS: Korean: Record the LSL1 runs`. No k16said (no parser).

## P4e: KQ4

### Task e1: [data] Tables, copy protection, intro

- [ ] **Step 1:** suite row `KQ4` OK; `go.sh $P4/e1 kq4-ko 20 type:a Return sleep:20 alt+s`: the copy-protection
  question `3쪽 세 번째 문단의 네 번째 단어는?` is passed by any answer (H7: `SCRIPT.701`); the intro narration boxes in
  Hangul (run r2).

### Task e2: [data] Menus, parser, save/load

- [ ] **Step 1:** skip the intro (wait it out: about 3 minutes, record the time) to the first room; the menu bar
  (`Esc` or the mouse at the top) shows translated titles (kAddMenu/kSetMenu, P3 Task 12).
- [ ] **Step 2:** `qhk` Return: the Korean room description; one particle sentence (`문을 열어` at a door) compared with
  the English target.
- [ ] **Step 3:** save/load with a Korean name (`originalsaveload=true`).
- [ ] **Step 4:** k16said with `--early`: `pass >= 464` of 487 (pending socket). `--early` is a k16said option (it
  walks early-SCI0 said specs); no engine change.
- [ ] **Step 5:** notes; commit `DOCS: Korean: Record the KQ4 runs`.

## P4f: LB1, then Camelot (conditional)

### Task f1: [data] LB1 tables and copy protection

- [ ] **Step 1:** suite row `LB1` OK; `go.sh $P4/f1 lb1-ko 25`: the fingerprint screen with the suspects' names in
  Hangul (`셀리`, `루디 디종`, ..., run r0). The cursor over a name is the game's magnifier (not a glitch).

### Task f2: [data] LB1 in play

- [ ] **Step 1:** pass the copy protection (the right suspect for the shown print; manual), reach the first room; `qhk`
  and one particle sentence; record replies.
- [ ] **Step 2:** a scene that uses script 52 (the Korean `SCRIPT.052` in `ko/resource.msg`, H7): the text is Korean;
  record the room.
- [ ] **Step 3:** save/load with a Korean name.
- [ ] **Step 4:** the English target on the pack folder: runs English; note it lacks `en/SCRIPT.052` (section 1 item 4,
  H6).
- [ ] **Step 5:** k16said `pass >= 601` of 744 (pending socket).

### Task f3: Notes (LB1)

- [ ] Notes; commit `DOCS: Korean: Record the LB1 runs`.

**Camelot decision point**: the owner keeps or cuts Camelot (decision 10). Cut: remove the `CAMELOT` row of the pack
suite (`TEST:` commit) and skip f4-f6.

### Task f4: [data] Camelot tables and first screens

- [ ] Suite row `CAMELOT` OK; `go.sh ... camelot-ko 40`: the title sequence, then the first Korean text box; record.

### Task f5: [data] Camelot save list (36-byte records)

- [ ] **Step 1:** two saves with Korean names (`originalsaveload=true`, `ans` and `rlf` = 길); F7: both names listed,
  the list's arrow keys and mouse move between them (memory `camelot-ko-fix`: the list used to ignore keys); restore
  the second. This exercises `kStrAt` past the end (`kUtf8ByteOffset`, review M2 fix) on real data (section 0.6 item 5).
- [ ] **Step 2:** a typed command and a particle sentence; k16said `pass >= 549` of 633 (pending socket).

### Task f6: Notes (Camelot)

- [ ] Notes; commit `DOCS: Korean: Record the Camelot runs`.

## P4z: Acceptance

### Task z1: Acceptance run and notes

- [ ] **Step 1:** `$BLD` `make test` 679 OK (or 679 minus Camelot's row: still one test); ASan build 679 OK, 0
  sanitizer lines; no-engine build 620 OK; SCI32 build (`--enable-engine=sci,sci32`) links without warning (a1 touches
  the SCI1.1-2.1 branch).
- [ ] **Step 2:** with harness zips (H5) if they exist, else `$P4/datah`: the pack suite OK; every sub-phase's first
  screen re-run and compared (`cmp` of the recorded crops).
- [ ] **Step 3:** `git diff --stat 5741781806a.. -- engines` = the nine engine files of section 3;
  `git diff 5741781806a.. -- engines | grep -c 'GID_'` = 3 (the table rows); every commit passes
  `check-commit-msg.py` with one `Assisted-by: Claude Code` and no `Co-Authored-By`.
- [ ] **Step 4:** `P4-notes.md` final section (the checklist below, ticked with evidence), commit
  `DOCS: Korean: Record the P4 acceptance run`.

## Acceptance checklist

| # | Item | Evidence |
|---|---|---|
| 1 | P4a: 679 tests OK, ASan clean, no-engine 620, SCI32 links | z1 Step 1 |
| 2 | English targets on every pack folder run upstream (no Korean log line) | a8 Step 2 |
| 3 | Every pack's `sci-ko.str` (+ `sci-ko.dat` v3) loads with its game's header, no warning | a6, a8, pack suite |
| 4 | QFG2: Aziza's door passes with Korean answers | b2 |
| 5 | QFG1 EGA: gargoyle and hut pass with Korean answers (or their rows removed with evidence) | c2 |
| 6 | LSL1: font 2107 draws through font 59's set; VGA text legible over 256-colour pictures; quiz path decided | d1-d4 |
| 7 | KQ4: menus, parser, save/load in Korean | e2 |
| 8 | LB1 (and Camelot if kept): parser, save/load (Camelot: the save list) | f2, f5 |
| 9 | No `cut to` / `longer than the English` line on the smoke paths, or each one filed as H9 | b3, c2, d2, e2, f2, f5 |
| 10 | First screens clut8 == rgb565 in the text crops (1 bpp fallback face) | b1, c1, d1, e1, f1, f4 |
| 11 | k16said baselines (QFG2 1654, QFG1E 855, KQ4 464 `--early`, LB1 601, Camelot 549) | **deferred to ko/07** |
| 12 | Game maps v3 re-paired to OFL faces | **harness H2/H3**, not in this plan |
| 13 | PATCHES.md lists every upstream file touched | a7, z1 Step 3 |

Manual checklist per game (recorded in `P4-notes.md`, a person or agent views each screen): Hangul legible at 2x on
the first screen; text boxes wrap inside their frame; status line fits; a typed Korean command answered in Korean;
the reply's meaning matches the English target's; save name shown in the game's list; nothing English left on the
smoke path except what is not a script/text string (record each).

What cannot be verified here: OS IME input (no IME under Xvfb), Windows/macOS/Android packaging, DOS (P7), k16said
(ko/07), the final look with the owner's OFL maps (H2), the ScummVM launcher save dialog's Hangul (needs
`fonts-cjk.dat`).

## Appendix A (conditional, Task d4): `StrAt` writes read only the part being built

Apply only if Task d4 shows the warning flood. In `kstring.cpp`, after `strAtByte`:

```cpp
// The string being built in @p r up to its NUL, the first reg_t slot that
// holds no characters, or the end of the buffer, byte by byte: a buffer a
// script is writing with StrAt has unwritten slots after its prefix, and
// SegManager::getString() would warn for each (LSL1's age quiz).
static Common::String strAtPrefix(SegmentRef &r) {
	Common::String str;
	for (int i = 0; i < r.maxSize; i++) {
		if (!r.isRaw && r.reg[(i + (r.skipByte ? 1 : 0)) / 2].getSegment() != 0)
			break;
		const byte b = strAtByte(r, (uint16)i, false, 0);
		if (!b)
			break;
		str += (char)b;
	}
	return str;
}
```

In `strAtUtf8`, the write branch builds `rebuilt` from `strAtPrefix(dest_r)` instead of `getString(str)` (the read
branch keeps `getString`), and in `kStrAt` the TTS line becomes
`if (argc <= 2 || !g_sci->getTextMode().utf8()) g_sci->_tts->setMessage(...)`. Commit `SCI: Read only the part a
script has built when StrAt writes UTF-8` (body: the d4 counts before/after; `jamo d06073697b4` measured 31 -> 3).
The prototype compiled this against the pre-review `kStrAt` (73bf2f80ba7); against the fixed one it is UNVERIFIED
until applied.
