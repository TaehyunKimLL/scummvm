# P5: AGI (Leisure Suit Larry 1 AGI first) — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task by task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** DESIGN.md section 11 row P5: a Korean AGI game, Leisure Suit Larry in the Land of the Lounge Lizards (AGI
2.440, DOS, 1.00 1987-06-01) first, plays headless on Linux from the harness pack: translated messages, object names, view
descriptions, menus, status line and interpreter strings drawn in Hangul at AGI's 640x400 (2x) text cell; Korean typed
with the built-in composer or an OS input method and understood by the game's own `said()`; saves keep the UTF-8 strings
and Korean save names; the pack with the quiz skipped works; English targets and every other AGI game stay upstream byte
for byte.

**Architecture:** one decision at start, `Agi::TextMode` (`kSingle` = upstream, `kUtf8` = a fork translation), held by
`AgiBase` and tested at every new branch (DESIGN.md section 3 policy 1, 5). In UTF-8 mode text is UTF-8 in the engine's own
`char *`/`Common::String` containers (the owner's "UTF-8 internally for all engines", the SCUMM P6a pattern): the
translation `ko/agi-ko.str` replaces message pointers at logic decode time, untranslated code-page text is made UTF-8 at
the same boundary, and every layout site counts **cells** (one code point = one cell of AGI's 40x25 grid; bytes only where
memory is sized). Drawing stays in AGI's existing upscaled 640x400 CLUT8 display buffer: a code point that a face of the
map has is a 16x16 **1 bpp stencil** painted with AGI's own foreground/background/invert/greyed rules
(`GfxMgr::drawCharacterOnDisplay`); the stencil comes from the P1 hi-res core (`Graphics::HiResFontSet` over `SvfChain`)
drawn into a 16x16 scratch `TextPlane`. No text plane on screen, no compositor, no RGB path: AGI is EGA 16 colours on a
CLUT8 screen, so faces are 1 bpp (owner: AA = RGB targets only, CLUT8 uses 1 bpp faces). Input: the edit lines (prompt,
get.string) hold code points (`uint32` cells); OS-IME characters arrive as a pseudo-key above every AGI key code; the
built-in composer edits a run at the end of the line. The parser maps Korean words through `ko/agi-ko.dat` (P2
`Common::KoreanVocabulary` v3, magic `AGIKOR`) to WORDS.TOK groups and tries up to four English orders against each
`said()`. Saves stay **version 11** (upstream loads them) with the full UTF-8 strings in an `AGKO` trailer.

**Tech stack:** C++11 ScummVM code, CxxTest, the P0/P1 SVF core (`graphics/hires_text`), P2 common Korean pieces
(`common/hangul`, `korean_stem`, `korean_vocab`, `translation_file`, `language_folder`), Python 3 stdlib tools
(`tools/korean/strcheck.py`, `k6bake.py`), Xvfb + xdotool + `xwd` screenshots.

Sources: DESIGN.md (sections 2.4, 3, 4, 5, 6, 7, 10, 11 row P5, 12); PATCHES.md; the finished phases' plans, notes, fixes and
reviews (`P3-sci-kq1.md`, `P4-sci-data-games.md`, `P3-fixes.md`, `P4-fixes.md`, `P6-scumm.md`, `P6a-fixes.md`,
`AA-tests.md`, `reviews/*`: bug classes byte-vs-code-point offsets, fixed buffers in bytes, terminator off-by-one, a cut
inside a multibyte sequence, hooks that must be inert for non-Korean targets; every external-lane finding came from an
exhaustive or randomized test); branch `jamo` of `~/work/scummvm-korean` (read only: `git -C $OLD show jamo:<path>`);
harness `~/work/scummvm` (read only).

---

## 0. Facts checked for this plan (2026-11-23)

### 0.1 Trees and the base

| Fact | Evidence |
|---|---|
| Base: `ko-2026.4` at `e9611c272ee` (P0..P4 + AA + P6a merged). `engines/agi` and `test/engines/agi` are untouched | `git diff --stat v2026.4.0 HEAD -- engines/agi test/engines/agi` prints nothing |
| `jamo`'s AGI work sits on an upstream base whose `engines/agi` equals v2026.4.0 | `git -C $OLD diff --stat 96b42d42bc8 v2026.4.0 -- engines/agi` prints nothing; `git -C $OLD diff 96b42d42bc8 jamo -- engines/agi` (42 files, 4784 diff lines) applies with `git apply` on a fresh `git archive` of the base without a conflict (done for the prototype) |
| **N0 = 648** tests, AGI-only debug build of the base | `~/work/tmp/p5/build-base`: `Running cxxtest tests (648 tests)...OK!` |
| Upstream AGI draws text into its own display buffer: `initGraphics(_displayScreenWidth, _displayScreenHeight)` with no pixel format (CLUT8), 640x400 when `_font->isFontHires() \|\| forceHires` | `engines/agi/graphics.cpp:154-163`, `:209` |
| No debug socket in this tree (DESIGN topic `ko/07`); SCI's k16said acceptance is pending it too | `ls gui \| grep -i socket` empty; `P3-notes.md` Task 20, `P4-notes.md` b4/c2/e2/f2/f5 |
| SCI has no KO/EN badge in this tree (jamo's `graphics/korean_badge` was not carried) | `grep -rln badge engines/sci graphics common` finds only a script patch comment |

### 0.2 Game data on this host

| Data | Where | md5 / identity |
|---|---|---|
| LSL1 AGI 1.00 1987-06-01, DOS EGA, AGI 2.440 (English) | `~/work/scummvm/runs/agi/lsl1-en/` (also in `gamedata/Leisure Suit Larry in the Land of the Lounge Lizards (Multi-Platform).zip`, folder `(DOS)/Leisure Suit Larry EGA`) | LOGDIR `1fe764e66857e7f305a5f03ca3f4971d` (detection entry `detection_tables.h:542`), WORDS.TOK `988a1baaf3ee0d9cd678274dc2f219f2`, OBJECT `aa7bce1ca83f181c92c1feea2c119eec`, VOL.0 `28dabb6408d2eb4757ee28686308a475`, VOL.1 `7ddb49cf212d5fab3c2edf86ee895886`, VOL.2 `89103f5b48fa49c00eed42f4b19808e1` |
| The Korean pack | `~/work/scummvm/dist/scummvm-game-lsl1agi-0ab70f653d3-test.zip` (`LSL1AGI/GAMES/LSL1AGI/`) | same files except VOL.1 `7951b500204797474f96709f83620c18` (logic 6 patched: age quiz skipped, `build_agi_lsl1.py skip_quiz`); `ko/agi-ko.str` `d759edb2da85431f9f2becc7f4e69e46` (1860 entries, **no header**), `ko/agi-ko.dat` `d410981098b13e927adab1e8101f1a72` (AGIKOR **version 2**, 1217 words), `ko/hiresbit.map` (**version 2**: refused by the P1 map parser) |
| Walkthrough pairs and said() specs | `~/work/scummvm/runs/lsl1a/parse/lsl1a-pairs.tsv` (778 pairs), `runs/lsl1a/src/saids.tsv` (2274 specs) | jamo's `k16said_agi.py` result on them: `runs/lsl1a/parse/said8.txt`: **778 variants: pass 691, partial 23, nomatch 20, unknown 7, order 6, english 31** |
| Other AGI data | `gamedata/agi/` = AGI demo 2 (`agidemo`, LOGDIR `852ac303...`, not LSL1); `gamedata/King's Quest 4 - The Perils of Rosella (Floppy DOS, AGI).zip` (AGI v3: for the v3 fingerprint check only); `gamedata/agifan/*` (fan games, English) | |
| Fingerprint of LSL1 1.00 (this plan's definition, D5) | computed by the prototype from LOGDIR + WORDS.TOK | `fingerprint=1d9311bbee31342e76c7570a29d7616d`, `vocab=988a1baaf3ee0d9cd678274dc2f219f2` |

### 0.3 Prototype (built and run before this plan was written)

`~/work/tmp/p5/tree` = `git archive e9611c272ee` + `git apply` of jamo's AGI diff + the changes of this plan (new
`AgiHiResText` over the P1 core, `TextMode`, headers, `AGKO` save trailer, byte budget, the fixes of D14, tests). Diff:
`~/work/tmp/p5/proto.diff` (55 files, +4824/-348, tests included); upstream hooks one file each: `~/work/tmp/p5/plan/hook-*.diff`; jamo
unit deltas `~/work/tmp/p5/plan/delta-*.diff`. Build `~/work/tmp/p5/build-proto` (AGI only): **693 tests OK** (N0 + 45; with `korean-packs/` in the build folder the LSL1 said() gate runs and passes),
no compiler warning. Runs `~/work/tmp/p5/runs/r*/` (scripts `~/work/tmp/p5/bin/{mkrun.sh,sess.sh,xwd2png.py}`; `sess.sh
xshot NAME` grabs the window with `xwd`, so no Alt+S reaches the game). Screenshots `~/work/tmp/ko-screenshots/agi-p5*.png`
(captions in `captions.txt` there).

| Check | Result |
|---|---|
| Pack as shipped (str without header, dat v2, map v2), `language=ko` | Korean thank-you box, status line `점수:0 / 222  소리:켜짐`, menus, inventory; the v2 map is refused and the engine map `data:hires_text/maps/korean-default.map` (NEODG16A) is used; `agi-ko.str has no fingerprint header ... loaded unchecked` and the header line the game expects is printed; `[korvocab] loaded 1217 Korean words (version 2)` (`agi-p5proto-lsl1-thanks-box.png`, `-status-line.png`, `-menu.png`, `-inventory.png`) |
| Pack converted to the new formats in scratch (`~/work/tmp/p5/data/lsl1ko3`: header line on agi-ko.str, `k6bake.py convert --format 3`, map version 3) | no warning, the pack's own map; `[korvocab] loaded 1217 Korean words (version 3)` (`-v3-header-pack.png`) |
| One fingerprint digit changed | `agi-ko.str was made for another game: fingerprint=0d93..., the game's is 1d93...; not loaded!`, English messages (`-fingerprint-mismatch-refused.png`) |
| Composer: typed `tnfwlqdmf qhk` | prompt `술집을 봐`, the syllable being composed inverted; Return -> the game's look-at-bar reply in Korean, wrapped in cells (`-prompt-preedit.png`, `-look-bar-reply.png`) |
| OS IME: `xdotool type "술집을 봐"` with `hangul_start=false` | the backend delivers each syllable as `KEYDOWN`, `keycode == KEYCODE_INVALID`, `ascii` = code point (log `key 1c220 pressed` = `AGI_KEY_CODEPOINT \| U+C220`); same reply. **The OS-IME path is testable under Xvfb** (`-osime-typed.png`, `-osime-reply.png`) |
| said() judgement offline (unit test with the real data, D10) | 778 variants: pass 691, partial 23, nomatch 20, unknown 7, order 6, english 31: **identical to jamo's said8.txt**, with the v2 and the converted v3 table |
| English target, same folder, no `language=` | frame pixel-identical to the unmodified build's (PIL difference bbox `None`) (`-english-target-unchanged.png`) |
| Unmodified build with `language=ko` (red for Task a6) | `Error running game: Game data not found` (`agi-p5-red-base-language-ko-game-data-not-found.png`) |
| Save (`originalsaveload=true`) with a Korean name typed by the composer | the name is held to the 30-byte field (`안녕하세요 안녕하셍`, 28 bytes); file version **11** + `AGKO` trailer (55 bytes); restore list shows the name; restored; **the unmodified build lists and loads the same save** (`-save-name-byte-budget.png`, `-restore-list-korean-name.png`, `-restored.png`, `-upstream-lists-korean-save.png`, `-upstream-loads-korean-save.png`) |
| `hires_text_log=true` on the first room + reply | no `no face has U+....` line |
| Randomized tests found two bugs in jamo's code that DESIGN called "port as is" | `TextCells::wordWrap` in UTF-8: a word cut at the width that ended in a line feed skipped the byte after the cut, a lead byte, so the output was invalid UTF-8 (5642 of 100000 random Hangul lines) and characters were lost (3508); fixed (D14.1). The byte path keeps upstream's identical loss (differential test). Upstream reports 21 lines after a stop at HEIGHT_MAX (20): upstream behaviour, kept |
| `Alt+S` | in one early run two `ㄴ` appeared in the prompt and could not be reproduced afterwards (alt+s during the title, the box, an empty prompt and a composing prompt: no leak, `--debugflags=input` shows no key). UNVERIFIED; Task b4 repeats the check with `xshot` |

### 0.4 Decisions (each re-checkable)

- **D1 Drawing = 16x16 stencils in AGI's 640x400 display, from the P1 core.** AGI paints every text cell's background
  itself (`drawCharacterOnDisplay`, `graphics.cpp:1219-1254`), its screen is CLUT8 with EGA indices (`initGraphics` without
  a format, `graphics.cpp:209`), and it has no driver layer like SCI's `UpscaledGfxDriver` to composite a plane in. A text
  plane would add a compositor for no gain: with 1 bpp faces (the only kind allowed on CLUT8) a stencil and a plane give
  the same pixels. So `AgiHiResText::glyph()` draws one code point through `HiResFontSet::drawChar()` into a 16x16
  `TextPlane` at scale 1 and reads coverage >= 128 back as 16 `uint16` rows; `GfxMgr` paints them. jamo did the same
  with its own glyph sources (`RangeRoutedGlyphSource`, superseded). The map is read with qualifiers `engine=agi`,
  `target=clut8` (so `[font:clut8]` sections apply); an anti-aliased face is cut at 128 with one warning, as SCI does.
- **D2 Basic Latin is always AGI's own font** (doubled 8x8, or the Hercules font upstream picks): `glyph()` returns false
  below U+0080 whatever the map says, so an untranslated (ASCII) line is pixel-identical to upstream's 640x400 rendering
  and punctuation inside Korean lines keeps the game's look (as jamo's maps' `range.basic-latin=original`).
- **D3 Text mode.** `TextMode{encoding: kSingle|kUtf8, hires, language}` decided in the `AgiEngine` constructor from the
  detection entry, `language=`, the game folder (`translationFolderLanguages(files, "agi-%s.str")`), `korean_text`,
  `text_encoding` (auto|utf8; `cp949` is not an AGI encoding: a note, auto) and `hires_text`, exactly as SCI's
  `decideTextMode` (`engines/sci/textmode.cpp`). `AgiBase::getLanguage()` answers the translation's language in UTF-8 mode
  (so SystemUI's interpreter strings and the TTS follow it).
- **D4 `ko/agi-<code>.str`**: jamo's format (`m<logic>:<n>`, `o<index>`, `v<view>`, TAB, text; escapes `\n \t \\`; `#`
  comments; a malformed line refuses the whole table; later duplicate wins) **plus** the P2 header line
  (`Common::TranslationFileHeader`, `checkTranslationHeader`: mismatch refused, no header loaded with a warning and the
  expected line printed) **plus** a text that is not valid UTF-8 is a malformed line (a CP949/Latin-1 paste would draw each
  stray byte as a cell).
- **D5 Fingerprint** = md5 of the logic directory file followed by WORDS.TOK (DESIGN section 4 "logdir+words.tok"):
  `LOGDIR` for v2, `<name>DIR` for v3 (`AgiLoader::logicDirectoryName()`, new virtual); no fingerprint where the game has no
  such file (disk images, v1: game and engine are still compared). `vocab=` = md5(WORDS.TOK). The quiz-skip patch changes
  VOL.1 only, so the pack's fingerprint is the original's.
- **D6 Parser table `ko/agi-<code>.dat`** (magic `AGIKOR`, `KoreanVocabulary` v3 with the header above + `vocab=`; v2
  loads with a warning). DESIGN section 4 names it `AGIKOR.DAT`; the pack and SCI (`ko/sci-ko.dat`) use the
  `<engine>-<code>.dat` name: **conflict noted, the pack's name kept**.
- **D7 Saves stay version 11** with the whole UTF-8 strings and their English answers in a trailer after the last field
  upstream reads (`AGKO`, `agi/saved_strings.h`); the version 11 string table holds each slot cut to whole characters
  (<= 39 bytes). DESIGN row P5 says "saves v12" (jamo's): rejected because a fork-only version number collides with
  upstream's next bump and locks the save out of upstream ScummVM; measured: the unmodified build lists and loads a
  Korean session's save (0.3). Old jamo v12 saves are not read (no Korean AGI save exists outside jamo test runs).
- **D8 Korean save names** are allowed (jamo's D8 made them ASCII only), held to the description field's 30 bytes
  (`SAVEDGAME_DESCRIPTION_LEN`): a key, an IME character or a composer jamo that would pass 30 bytes is refused. The
  save list line is built by cells (`TextCells::savedGameLine`), its buffers sized for 31 cells of 4 bytes.
- **D9 Input.** OS IME: `KEYDOWN` with `KEYCODE_INVALID` and `ascii >= 0x100` (SCI's `KoreanEditLine::osImeCodeOf` rule)
  becomes `AGI_KEY_CODEPOINT | cp` (0x10000+, never a controller or a script key; `processAGIEvents` hands it to the prompt
  or get.string only). No surrogate key: AGI's prompt is always open (DESIGN section 6 item 1, confirmed in 0.3). Composer:
  jamo's `HangulLine` over `Common::HangulComposer`, on the `uint32` cells; `HANGULTOGGLE` keymap action with SCI's
  defaults and gate (`engines/sci/metaengine.cpp:696-735`: `language=ko`, not with `hangul_builtin=false`,
  `korean_text=false` or `hangul_input=false`). Keys `hangul_input` (auto = a word table is loaded), `hangul_builtin`
  (true), `hangul_start` (true) as SCI's `EventManager` (`event.cpp:162-168`). No KO/EN badge (SCI has none in this tree;
  P8).
- **D10 Parser.** jamo's `korparse` (token lookup, candidates a-d, `saidMatches` = the interpreter's loop) and `Words::analyze`
  kept; `testSaid` tries the candidates best first and commits the one that matched. `compare.strings` accepts a translated
  answer's English original (`AgiGame::stringAnswer`, filled by `set.string` from the raw message). The acceptance
  number is reproduced **offline** by a CxxTest suite on the real data (k16said_agi.py's rules over `Words::analyze` +
  `saidMatches`), because the debug socket is not in the tree; the socket run stays for `ko/07`.
- **D11 Console:** `parse`, `said`, `saids`, `koverlay` (jamo) kept for the GUI debugger and the future socket;
  `agistate`, `prompt`, `string` are socket-oriented and go with `ko/07`.
- **D12 Render mode:** with hi-res text the render mode becomes EGA (AGI256 keeps VGA) and `herculesfont` is turned off
  for the session (jamo D3/F11): the 640x400 cell needs the doubled 8x8 PC font for ASCII and EGA colours.
- **D13 Untranslated text with bytes >= 0x80** becomes UTF-8 from CP850 at decode time (jamo; `TextOverlay::legacyToUtf8`).
  LSL1's messages have no such byte (UNVERIFIED for other games: the PC font is CP437-shaped; CP850 and CP437 differ above
  0x9B).
- **D14 Fixes to jamo's code, each with a test that failed before:**
  1. `TextCells::wordWrap` (UTF-8): the piece of a word cut at the width no longer takes that word's line feed (a lead
     byte was skipped). Byte path unchanged (upstream's).
  2. `stringPrintf`/`stringWordWrap` result buffers are 2000 bytes upstream (`text.cpp:1183`, `:1305`); a UTF-8 box of 20
     lines x 40 cells is 2400+ bytes: buffers become `2000 * 4` and a longer UTF-8 result is cut on a character boundary
     with a warning (the `assert` stays for the byte path).
  3. `set.game.id` reads the game's own message (`rawTexts`), so a translated id message never changes the id saves carry.
  4. `set.simple`'s automatic save description (`op_cmd.cpp:972-974`, `strncpy` into 31 bytes) is cut on a character.
  5. The save list line and its buffers count cells (D8).
  6. TTS: `_ttsEncoding = kUtf8` in UTF-8 mode (`agi.cpp:573` would hand UTF-8 to the TTS as CP850).
  7. The prompt line is parsed from `promptGetText()` (UTF-8 from the cells), never from the cell array's bytes.

### 0.5 ScummVM/AGI APIs and sites (file:line in `ko-2026.4`, verified; the prototype compiles against all of them)

| Site | Where |
|---|---|
| `AgiEngine::agiInit` 79 (`loadWords` 159), ctor 414, `initialize` 482 (`initRenderMode` 518, `_text->init` 534, `_loader->init` 553, `setupOpCodes` 556, TTS encoding 565-573), `AgiGame::getString` 839, `setString` 855 | `engines/agi/agi.cpp` |
| `MAX_STRINGLEN` 84, `strings[][]` 411, `automaticSaveDescription` 433, `LOGDIR` 72, `DIR_` 71 | `engines/agi/agi.h` |
| `GfxMgr::initVideo` 77 (hires switch 154-163, `initGraphics` 209), `putFontPixelOnDisplay` 474, `drawCharacter` 1160, `drawStringOnDisplay` 1188/1200, `drawCharacterOnDisplay` 1219 | `engines/agi/graphics.cpp` |
| `TextMgr::display` 240, `displayText` 340, `displayCharacter` 365, `drawMessageBox` 504, `statusDraw` 652, `promptKeyPress` 794, `promptEchoLine` 904, `promptRedraw` 925, `promptCommandWindow` 967, `stringSet` 1000, `stringEdit` 1013, `stringKeyPress` 1071, `stringWordWrap` 1182 (buffer 1183), `stringPrintf` 1304 (buffer 1305, assert 1380) | `engines/agi/text.cpp` |
| `Words::clearEgoWords` 172, `cleanUpInput` 215, `findWordInDictionary` 247, `parseUsingDictionary` 324 | `engines/agi/words.cpp` |
| `condHaveKey` 121, `testCompareStrings` 233, `testSaid` 329 | `engines/agi/op_test.cpp` |
| `cmdLog` 826, `cmdSetSimple` 972-977, `cmdSetGameID` 1756-1763, `cmdGetString` 1999, `cmdGetNum` 2059, `cmdSetString` 2136 | `engines/agi/op_cmd.cpp` |
| `processAGIEvents` 214 | `engines/agi/cycle.cpp` |
| `processScummVMEvents` 65 (KEYDOWN 136), `doPollKeyboard` 385 | `engines/agi/keyboard.cpp` |
| `decodeLogic` 32, `unloadLogic` 98 / `decodeObjects` 28 / `showObject` 395 | `logic.cpp` / `objects.cpp` / `sprite.cpp` |
| `GfxMenu::addMenu` 70, `addMenuItem` 116 / `getPlayerInventory` 43 | `menu.cpp` / `inv.cpp` |
| `SystemUI::SystemUI` 30 (language switch 78, FR 124), `askForCommand` 258, `askForSaveGameDescription` 357, `createSavedGameDisplayText` 589-612, `readSavedGameSlots` 694, `askForSavedGameVerification` 808, `askForVerification` 821 | `engines/agi/systemui.cpp` |
| `saveGame` 70 (description 86-101, version 103, strings 219, AGIPal 330), `loadGame` 345 (strings 571, AGIPal 731), `scummVMSaveLoadDialog` 759 (desc cut 775), `getSavegameInformation` 852 | `engines/agi/saveload.cpp` |
| `AgiBase::getLanguage` 57; `class AgiMetaEngine` 235 (no `initKeymaps` upstream) | `engines/agi/metaengine.cpp` |
| `AgiMetaEngineDetection::detectGame` 308 | `engines/agi/detection.cpp` |
| `AgiLoader::init` (virtual) 36, v2 `loadDir(..., LOGDIR)` `loader_v2.cpp:116`, v3 `_name` `loader_v3.cpp:36-50`, dir path `:86-90` | `engines/agi/loader.h`, `loader_v2.cpp`, `loader_v3.cpp` |
| `Graphics::HiResFontSet::build` 85, `getAscent` 92, `getDescent` 93, `route` 97, `getAdvance` 98, `drawChar` 103; `HiResFaceCache` 56/58 | `graphics/hires_text/font_set.h` |
| `TextPlane::create` 49, `free` 50, `getCoverage` 64, `clear` 74 | `graphics/hires_text/text_plane.h` |
| `pickHiResMap` 51 / `HiResMapQualifiers` 134, `HiResLayoutSettings::hangulSet` 98 / `compileIdPlan` 66 / `readHiResOptions` 64, `hangulBreakSet` 54 / `kHangulBreakAny` 153 / `HiResDiskFiles` 55 | `map_pick.h` / `font_map.h` / `id_plan.h` / `hires_options.h` / `text_layout.h` / `hires_files.h` |
| `TranslationFileHeader::parse` 55, `toString` 58, `checkTranslationHeader` 73 | `common/translation_file.h` |
| `KoreanVocabulary::load` 81, `loadFromStream` 84 | `common/korean_vocab.h` |
| `HangulComposer` 49, `isJamoKey` 57, `resolveKoreanParticles` 101 | `common/hangul.h` |
| `koreanStemCandidates` 99, `koreanParticleRole` 108, `kKoreanRole*` 43-49 | `common/korean_stem.h` |
| `translationTableName` 37, `translationFolderLanguages` 50 / `getLanguageFolderCode` 112 | `common/language_folder.h` / `common/language.h` |
| `KEYCODE_HANGUL` 245 / `"HANGUL"` 220 | `common/keyboard.h` / `backends/keymapper/hardware-input.cpp` |
| `computeStreamMD5AsString` 65 | `common/md5.h` |
| SCI patterns copied: `decideTextMode` (`engines/sci/textmode.cpp`), `textModeInputs`/`translationHeader` (`engines/sci/sci.cpp:243-305`), `identifyGame`/`detectGames` variants (`engines/sci/detection.cpp`), keymap gate (`engines/sci/metaengine.cpp:696`), `autoBoolKey` (`engines/sci/event.cpp:149`) | |
| Test helpers: `HiResTest::MemoryFiles` (`test/graphics/hires_support.h:38`), `kSvfFixtureJamo` (16x16 1 bpp NEODG16 jamo fixture, `test/graphics/svf_fixture.h:53`) | |

---

## 1. File map

| File | Task | New / modified | Responsibility |
|---|---|---|---|
| `engines/agi/textcells.{h,cpp}` | a1, d3 | new (jamo + delta) | decode, cells, clips, wrap, `agiStringsEqual`, `isValidUtf8`, `utf8Bytes`, `savedGameLine` |
| `engines/agi/textmode.{h,cpp}` | a2 | new | `TextMode`, `decideTextMode` |
| `engines/agi/translation.{h,cpp}` | a3 | new (jamo + delta) | `TextOverlay` (agi-<code>.str) with header, UTF-8 check |
| `engines/agi/hirestext.{h,cpp}` | a4 | new (rewritten) | `AgiHiResText`: map, font set, 16x16 stencils |
| `engines/agi/hangulline.{h,cpp}` | b1 | new (jamo minus badge) | composer run on `uint32` cells |
| `engines/agi/korparse.{h,cpp}`, `saidscan.{h,cpp}` | c1 | new (jamo, unchanged) | said() core and Korean candidates; said() spec scanner (console) |
| `engines/agi/saved_strings.{h,cpp}` | d1 | new | `AGKO` trailer, version 11 slots, legacy conversion |
| `engines/agi/{agi.h,agi.cpp,loader.h,metaengine.cpp,detection.cpp,logic.{h,cpp},objects.cpp,sprite.cpp,op_cmd.cpp,graphics.{h,cpp},preagi/preagi.cpp,text.{h,cpp},inv.cpp,menu.cpp,systemui.{h,cpp},keyboard.{h,cpp},cycle.cpp,op_test.cpp,words.{h,cpp},console.{h,cpp},saveload.cpp,module.mk}` | a5-d3 | modified | hooks (appendix B, one section per file) |
| `test/module.mk`, `test/engines/agi/{textcells,textmode,translation,hirestext,hangul_line,korparse,saved_strings,korean_said_data}.h`, `test/engines/agi/agi_test_stubs.cpp` | a1-d1 | new / modified | tests (appendix C) |
| `tools/korean/strcheck.py`, `tools/korean/test_strcheck.py` | e1 | modified | `agi` key syntax and escapes |
| `docs/korean/PATCHES.md`, `docs/korean/plans/P5-notes.md` | every task, e2 | modified / new | records |

## 2. Environment (every task)

```sh
SRC=/home/thkim/work/scummvm-2026.4-korean-p5   # worktree of branch ko/p5-agi (Task a0)
OLD=/home/thkim/work/scummvm-korean             # read only: git -C $OLD show jamo:<path>
HAR=/home/thkim/work/scummvm                    # read only
BLD=~/work/tmp/scummvm2026-p5build              # AGI only, out of tree; never configure inside $SRC
P5=~/work/tmp/p5                                # scratch: data/, runs/, bin/, said/, plan/ (hooks and deltas), tree/ (prototype)
mkdir -p $BLD && cd $BLD
heavy $SRC/configure --disable-all-engines --enable-engine=agi --enable-debug --disable-optimizations
```

Build/test: `cd $BLD && heavy make -j12 scummvm test`. Checkpoint builds: `~/work/tmp/scummvm2026-p5all`
(`--enable-engine=agi,sci,scumm`, the combined `test/module.mk`), `~/work/tmp/scummvm2026-p5asan` (AGI, `--enable-asan`),
`~/work/tmp/scummvm2026-p5none` (`--disable-all-engines`: the AGI test section must vanish). The real-data suites read
`$BLD/korean-packs/` (Task c3): `LSL1AGI` -> a pack game folder, `LSL1AGI-said/{pairs.tsv,saids.tsv}`.

Runs: `$P5/bin/mkrun.sh RUNDIR TARGET GAMEID PATH [key=value...]` writes `RUNDIR/run.ini` (engineid=agi, extrapath =
`$SRC/dists/engine-data`); `$P5/bin/sess.sh RUNDIR start TARGET [BIN]` / `do STEP...` (`sleep:N`, `type:TEXT`, key names) /
`xshot NAME` (xwd of the window -> `RUNDIR/NAME.png`, no key sent) / `stop`. Retry `start` up to 8 times (the SDL
`SDL_BlitSurface failed` start flake). Use absolute RUNDIRs. Set `--debugflags=text,input` with `DBG=5 XARGS=...`. Never
`/tmp`.

Game folders (scratch copies, built once in a0): `$P5/data/lsl1en` (English, from `$HAR/runs/agi/lsl1-en`),
`$P5/data/lsl1ko` (the pack's `GAMES/LSL1AGI` as shipped), `$P5/data/lsl1ko3` (the same converted to the new formats,
Task a0 step 5).

Commit messages: `AREA: Subject` (imperative, <= 72 chars), a blank line, a body that says why, exactly one trailing line
`Assisted-by: Claude Code`, no `Co-Authored-By`; check every commit with `python3 -I devtools/check-commit-msg.py`. GPL
header on every new file (copy from `engines/agi/agi.cpp`). After each task: `make test` count = previous + the task's new
tests, no new warning (`grep -c 'warning:' make.out` = 0 for the files touched), a line in `P5-notes.md` (sha, red, green,
data evidence).

---

## 3. Tasks

Sub-phases are independently buildable, testable and committable: P5a leaves a game that reads and draws Korean but
types English; P5b adds typing; P5c the Korean parser; P5d saves; P5e tools and records. Each task lists its hunks; the
exact code is in appendices A (new units), B (upstream hooks, as unified diffs against `ko-2026.4`) and C (tests).

### P5a: text mode, translation table, drawing (11 tasks)

#### Task a0: branch, baseline, data (no commit, 15 min)
- [ ] `git -C /home/thkim/work/scummvm-2026.4-korean worktree add -b ko/p5-agi ../scummvm-2026.4-korean-p5 ko-2026.4`;
  `git -C $SRC diff --stat v2026.4.0 -- engines/agi` prints nothing.
- [ ] Configure and build `$BLD` (section 2). Expected: `Running cxxtest tests (648 tests)...OK!` (N0; write the number
  into `P5-notes.md`; if it differs, use the measured N0 in every later count).
- [ ] Data: `mkdir -p $P5/data && cp -r $HAR/runs/agi/lsl1-en $P5/data/lsl1en`; unzip
  `$HAR/dist/scummvm-game-lsl1agi-0ab70f653d3-test.zip` into `$P5/packx` and copy `LSL1AGI/GAMES/LSL1AGI` to
  `$P5/data/lsl1ko`. `md5sum $P5/data/lsl1ko/{LOGDIR,WORDS.TOK,VOL.1}` = `1fe764e6...`, `988a1baa...`, `7951b500...`
  (0.2). (Already present from the prototype: reuse.)
- [ ] Red: `$BLD/scummvm` on `$P5/data/lsl1ko` with `language=ko`: `Error running game: Game data not found` (screenshot
  as `agi-p5-red-base-language-ko-game-data-not-found.png`).
- [ ] Converted copy (needed from a5 on): `cp -r $P5/data/lsl1ko $P5/data/lsl1ko3`; in `ko/`: prepend
  `#! format=1 game=lsl1 fingerprint=1d9311bbee31342e76c7570a29d7616d engine=agi` to `agi-ko.str`;
  `python3 $SRC/tools/korean/k6bake.py convert agi-ko.dat agi-ko3.dat --format 3 --game lsl1 --fingerprint
  1d9311bbee31342e76c7570a29d7616d --vocab-md5 988a1baaf3ee0d9cd678274dc2f219f2 && mv agi-ko3.dat agi-ko.dat` (expected:
  `AGIKOR version 3, 1217 entries, 21332 bytes`); replace `hiresbit.map` by `HIRESBIT.MAP` of appendix A.9. These are
  scratch stand-ins for the harness pack of section 7.

#### Task a1: cell helpers (`TextCells`) (20 min)
Files: `engines/agi/textcells.{h,cpp}` (new), `engines/agi/module.mk`, `test/module.mk`, `test/engines/agi/textcells.h`.
- [ ] Write the test first: appendix C.1 (`AgiTextCellsTestSuite`, 5 tests: exhaustive decode over every input of up to
  three bytes and 2M four-byte leads against an RFC 3629 reference decoder; 300000 random ASCII lines x every width
  against upstream's `stringWordWrap` loop (verbatim copy), byte path identical, UTF-8 path identical except the D14.1
  case; 100000 random Hangul/ASCII lines: valid UTF-8 out, no line over the width, reported width = widest line,
  height <= 21, every character kept; clips are the longest whole prefix; `savedGameLine` equals upstream's
  `createSavedGameDisplayText` on the byte path). Add the AGI block of appendix B:test_module.mk to `test/module.mk` with
  `engines/agi/textcells.o` only so far.
- [ ] Red: `make test` -> `agi/textcells.h: No such file or directory`.
- [ ] `git -C $OLD show jamo:engines/agi/textcells.h > $SRC/engines/agi/textcells.h` (same for `.cpp`), then apply
  `$P5/plan/delta-textcells.h.diff` and `delta-textcells.cpp.diff` (appendix A.1: `isValidUtf8`, `utf8Bytes`,
  `savedGameLine`, the D14.1 fix); add `textcells.o` to `engines/agi/module.mk`.
- [ ] Green: N0 + 5. Remove D14.1 for a moment and run: `test_wrapKeepsCharactersWholeOnRandomHangul` fails with
  thousands of cases (prototype: 5642 invalid, 3508 lost); put it back.
- [ ] Commit `AGI: Add the cell helpers of UTF-8 text` (body: one cell per code point in UTF-8 text mode, per byte
  otherwise; wrap equals upstream's loop on the byte path; a cut word keeps its line feed in UTF-8).

#### Task a2: text mode (10 min)
Files: `engines/agi/textmode.{h,cpp}` (appendix A.2), module.mk, `test/engines/agi/textmode.h` (C.2), test/module.mk
(`engines/agi/textmode.o`).
- [ ] Test first (5 tests: English stays upstream; Korean folder + table -> UTF-8 + hires; language without translation
  changes nothing; folder without table is still UTF-8 with a note; `korean_text=false`, `hires_text=false`,
  `text_encoding=cp949` (note, auto), `text_encoding=utf8`). Red: header missing. Green: +5.
- [ ] Commit `AGI: Decide once at start how the game's text is held`.

#### Task a3: the translation table (15 min)
Files: `engines/agi/translation.{h,cpp}` (jamo + `$P5/plan/delta-translation.*.diff`, appendix A.3), module.mk,
`test/engines/agi/translation.h` (C.3), test/module.mk (`engines/agi/translation.o`).
- [ ] Test first: jamo's `agi_overlay.h` (8 tests) renamed `AgiTranslationTestSuite`, `load()` taking an expected
  header, plus `test_header_matches_or_refuses` and `test_text_must_be_utf8`. Red: `loadFromStream` has no header
  parameter / no UTF-8 check. Green: +10.
- [ ] Commit `AGI: Read agi-<lang>.str with its fingerprint header`.

#### Task a4: `AgiHiResText` stencils (20 min)
Files: `engines/agi/hirestext.{h,cpp}` (appendix A.4), module.mk, `test/engines/agi/hirestext.h` (C.4), test/module.mk
(`engines/agi/hirestext.o graphics/libgraphics.a common/libcommon.a`).
- [ ] Test first (3 tests): below U+0080 always false (AGI's font); **all 11172 syllables**: the stencil equals the
  fixture face drawn straight through an `SvfChain` at the cell's pen position, cut at 128, and has ink (proves the
  glue: centring, baseline, clip, bit order); `hires_text=false` -> inactive. Red: header missing. Green: +3 (prototype:
  0 mismatches over 11172).
- [ ] Commit `AGI: Draw a hi-res text map's characters as 16x16 stencils` (body: D1, D2; CLUT8 => 1 bpp, an AA face is
  cut at 128 with one warning).

#### Task a5: detection of a translation folder (15 min)
Files: `detection.cpp` (B:detection.cpp: jamo's `overlayLanguages()` with the pattern `agi-%s.str`, `allowLanguages()`,
`identifyGame()`, `detectGames()` variants offered first as `<target>-ko`, `detectGame()` dropping the language filter
when the folder holds that language's table).
- [ ] Red (a0): `Game data not found`. Green: `scummvm -c run.ini lsl1-ko` with `language=ko` runs (English text: the
  engine reads the table from a6 on); `scummvm --detect --path=$P5/data/lsl1ko3` lists the Korean variant (preferred
  target `lsl1-ko`) before the English one; an English-only folder (`$P5/data/lsl1en`) lists only the English one.
- [ ] Commit `AGI: Offer and run a folder's translation as its own target`.

#### Task a6: engine wiring: text mode, language folder, header, overlay (20 min)
Files (appendix B, one section per file): `agi.h` (B:agi.h: `TextMode _textMode`, `_textModeNotes`, `textIsUtf8()`, `AgiHiResText *_hiresText`,
`hiresTextActive()`, `hiresHangulAny()`, `TextOverlay _overlay`, `loadTranslationOverlay()`, `textModeInputs()`,
`translationHeader()`, `rawLogicMessageCount()`), `agi.cpp` (B:agi.cpp: the hunks of the constructor, `initialize()`,
`loadTranslationOverlay`, `textModeInputs`, `appendFile`/`md5Of`/`translationHeader`, TTS `kUtf8`; **not yet**
`setString`/`setStringFromMessage`/`collectSaids`/`loadKoreanTable`/`hangulSetup` (c2, b3, c4)), `loader.h` (B:loader.h
`logicDirectoryName`), `metaengine.cpp` (B:metaengine.cpp `getLanguage` hunk only).
- [ ] No unit seam (needs an engine): evidence is the run. Red: after a5 the log of a `language=ko` run has no
  `agi-ko.str` line.
- [ ] Build; run `$P5/data/lsl1ko3` with `language=ko` and `--debugflags=text`; the log: `agi-ko.str: 1860 entries` (no
  warning: the header matched) and `Hi-res text: ...` notes; English messages still (a7 lays them in). Run
  `$P5/data/lsl1ko` (no header): `agi-ko.str has no fingerprint header ... loaded unchecked` + `This game's agi-ko.str
  header line is: #! format=1 game=lsl1 fingerprint=1d9311bbee31342e76c7570a29d7616d engine=agi`. `lsl1ko3` with one
  fingerprint digit changed: `... was made for another game ...; not loaded` + `The ko translation folder has no usable
  agi-ko.str`.
- [ ] v3 fingerprint check (UNVERIFIED in the prototype): unzip the KQ4 AGI zip into `$P5/data/kq4agi`, put a
  `ko/agi-ko.str` holding one comment line, run with `language=ko`: the printed header line has a 32-digit
  fingerprint (`<name>DIR` + WORDS.TOK). If the v3 loader's `_name` is empty (`loader_v3.cpp:86-90` uses another path),
  `logicDirectoryName()` must return that path: fix and note.
- [ ] Commit `AGI: Load the translation of the target's language at start`.

#### Task a7: messages, object names, view descriptions (15 min)
Files: `logic.{h,cpp}` (B:logic.cpp/B:logic.h: `rawTexts`, `utf8Pool`, `translateLogicTexts()` at `decodeLogic`, freed in
`unloadLogic`), `agi.cpp` (`rawLogicMessageCount`), `objects.cpp` (B:objects.cpp), `sprite.cpp` (B:sprite.cpp), `op_cmd.cpp` (B:op_cmd.cpp:
`cmdSetGameID` from `rawTexts`, D14.3 only).
- [ ] Data: first room + thank-you box text from the table (log still shows `Game ID: "LLLLL"`). With `hires` not yet
  drawing (a8), Hangul shows as AGI-font garbage: expected at this task; the check is the log and `koverlay` later.
- [ ] Commit `AGI: Lay translated messages, object names and view descriptions over the game's`.

#### Task a8: display by code point, Hangul stencils (15 min)
Files: `graphics.{h,cpp}` (B:graphics.cpp/B:graphics.h: 640x400 when `hiresTextActive()`, `drawCharacter`/`drawCharacterOnDisplay` take
`uint32`, the stencil branch, `drawCoverageOnDisplay`, UTF-8 decode in `drawStringOnDisplay`, `?` for code points AGI's
font lacks), `preagi/preagi.cpp` (B:preagi_preagi.cpp cast), `agi.cpp` (render mode EGA, `herculesfont` off: D12).
- [ ] Data: `$P5/data/lsl1ko3` `language=ko`: thank-you box in Hangul (compare with `agi-p5proto-lsl1-thanks-box.png`;
  the box text area must be equal; check with PIL on the rows of the box).
- [ ] Commit `AGI: Draw text by code point and Hangul from the hi-res text map`.

#### Task a9: layout in cells (20 min)
Files: `text.{h,cpp}` hunks of `displayText`, `displayCharacter`, `statusDraw`, `stringWordWrap` (via `TextCells`),
`stringPrintf` (particles, D14.2 buffers `kTextBufferBytes`), `inv.cpp` (B:inv.cpp), `menu.cpp` (B:menu.cpp), `systemui.cpp` (Korean
interpreter strings, button widths; B:systemui.cpp). The prompt/string hunks are b3's, the save-name hunks d3's.
- [ ] Unit evidence: a1's tests (TextMgr needs an engine). By reading: both result buffers are `kTextBufferBytes`
  (8000), a UTF-8 result is cut with `clipBytes`, the byte path keeps upstream's `assert`.
- [ ] Data: thank-you box, Lefty's status line, Esc menu, Tab inventory (compare with the prototype's screenshots).
- [ ] Commit `AGI: Count cells, not bytes, where translated text is laid out`.

#### Task a10: P5a smoke and the English gate (no commit, 15 min)
- [ ] Korean: `language=ko` on `lsl1ko3`: thank-you box, Lefty's, menu, inventory: four `xshot`s into
  `~/work/tmp/ko-screenshots/agi-p5a-*.png` with captions.
- [ ] English: the same folder without `language=`: the Lefty's frame (`Return`, `Return`, 6 s) is pixel-identical to
  the base binary's (`PIL ImageChops.difference(...).getbbox() is None`); no `Hi-res text`, `agi-ko`, `korvocab` log line.
  Also `$P5/data/lsl1en` and one fan game (`gamedata/agifan/AGI Contest 1 Template`) on both binaries: identical first frames.
- [ ] ASan build: the Korean run, 0 `AddressSanitizer` lines.

**Checkpoint A (after a10): review** (section 4).

### P5b: input (4 tasks)

#### Task b1: the composer line (10 min)
Files: `engines/agi/hangulline.{h,cpp}` (jamo minus the badge: `$P5/plan/delta-hangulline.*.diff`), module.mk,
`test/engines/agi/hangul_line.h` (C.5: jamo's `agi_hangul_line.h`, 4 tests, + `test_randomKeysKeepTheRunInside`: 50000
random sequences of jamo keys, backspaces and commits with a random budget: the line never exceeds the budget, cells
before `runStart()` never change, the run equals `HangulComposer::text()` of the same keys).
- [ ] Red/green: +5. Commit `AGI: Add the Hangul composer line of the edit fields`.

#### Task b2: keys (15 min)
Files: `keyboard.{h,cpp}` (B:keyboard.cpp/B:keyboard.h: `AGI_KEY_CODEPOINT`, `AGI_KEY_HANGUL_TOGGLE`, the OS-IME `KEYDOWN` rule only when
`textIsUtf8()`, the keymapper action, the toggle handled once in `doPollKeyboard`), `cycle.cpp` (B:cycle.cpp: `processAGIEvents`
returns `int`; a wide key goes to the prompt / get.string only), `op_test.cpp` (B:op_test.cpp `condHaveKey` hunk: a code point is
a key but sets `VM_VAR_KEY` 0), `agi.h` (`AgiAction`, `int processAGIEvents()`), `metaengine.cpp` (B:metaengine.cpp `initKeymaps`).
- [ ] No unit seam (events need the engine). Commit `AGI: Take OS input method characters and the Han/Yeong key`.

#### Task b3: edit lines by code point, composer (20 min)
Files: `text.{h,cpp}` hunks of the constructor, `hangulSetup`, `redrawInputTail`, `hangulEndRun`, `hangulEditKey`,
`hangulToggle`, `promptReset`, `promptKeyPress`, `promptGetText`/`promptPreviousGetText`/`promptSetFromText`/
`stringGetText`, `promptCancelLine`, `promptEchoLine` (D14 F4: echo in English, stops when refused), `promptRedraw`,
`promptCommandWindow`, `stringSet`, `stringEdit`, `stringKeyPress` (cells `uint32 _prompt[42]`, `_promptPrevious[42]`,
`_inputString[42]`); `op_cmd.cpp` (`cmdLog`, `cmdGetString`, `cmdGetNum` read `*GetText()`); `systemui.cpp`
(`askForCommand`); `agi.cpp` (`agiInit`: `_text->hangulSetup(false)` after `loadWords`; c2 passes
`_words->hasKoreanWords()`: until then the composer is on only with `hangul_input=true`).
- [ ] Commit `AGI: Edit the prompt and get.string by code point, with the composer`.

#### Task b4: P5b smoke (no commit, 20 min)
- [ ] With `hangul_input=true` (no table yet): type `tnfwlqdmf qhk` -> prompt `술집을 봐`, preedit inverted (`xshot`).
- [ ] `hangul_start=false`, `xdotool type "술집을 봐"` (OS IME): the same prompt; log `key 1c220 pressed`.
- [ ] Toggle: `ctrl+space` with the composer on: `r` gives `r` after, `ㄱ` before; log `[key] Han/Yeong -> English`.
- [ ] Alt+S (0.3 UNVERIFIED): with the composer on, press `alt+s` at the title, in a message box, on an empty prompt and
  while composing; `xshot` the prompt after each (no `ㄴ`); `--debugflags=input` shows no `73` key. If a `ㄴ` appears,
  red: guard the composer against `KBD_ALT` in `processScummVMEvents` (enqueue nothing for Alt+letter when the keymapper
  consumed it) and add the case to `P5-notes.md`.
- [ ] English target: typing `look at bar` gives the English reply; frames as a10.

### P5c: the Korean parser (4 tasks)

#### Task c1: said() core and candidates (15 min)
Files: `engines/agi/korparse.{h,cpp}`, `saidscan.{h,cpp}` (`git show jamo:...` unchanged), module.mk,
`test/engines/agi/korparse.h` (C.6: jamo's `agi_korparse.h`, 9 tests, + `test_saidMatchesIsTheInterpretersLoop`: 200000
random (word list, spec) pairs with ids from {1, 9999, 0..5}: `saidMatches()` equals a verbatim copy of upstream's
`testSaid` loop (`op_test.cpp:329-381` of v2026.4.0); `test_candidatesKeepUnitsAndLimits`: 50000 random typed
sentences: at most 4 candidates, each at most `maxWords`, no duplicate, a `cont` word always right after its unit's
first word, candidate (c) = the typed order).
- [ ] Red/green: +11. Commit `AGI: Add the said() core and the Korean candidates`.

#### Task c2: Words and said() (20 min)
Files: `words.{h,cpp}` (B:words.cpp/B:words.h: `ParseResult`, `analyze`, `applyParse`, `commitCandidate`, `lookupJoinedPair`,
`loadKoreanTable` (v3 header from `translationHeader(true)`), `loadKoreanTableFromStream`, `findPrepositions`,
`hasKoreanWords`; `cleanUpInput`/`findWordInDictionary` made `const`; the Korean path entered only when `textIsUtf8()`
and the line has a byte >= 0x80; `_vm &&` guard on the language), `op_test.cpp` (`testSaid` -> `testSaidSpec`,
`testCompareStrings` with `agiStringsEqual` + answers), `agi.h`/`agi.cpp` (`MAX_STRINGBYTES`, `stringAnswer[]`,
`setString` clipping cells and bytes, `setStringFromMessage`), `op_cmd.cpp` (`cmdSetString` -> `setStringFromMessage`),
`agi.cpp` `agiInit` (`loadKoreanTable` when UTF-8).
- [ ] Data: `술집을 봐` (composer) -> the look-at-bar reply (`agi-p5proto-lsl1-look-bar-reply.png`); log `[korvocab]
  loaded 1217 Korean words (version 3)`; English `look at bar` in the Korean target gives the same reply.
- [ ] Commit `AGI: Match Korean commands against the game's said() specs`.

#### Task c3: the LSL1 said() gate on real data (15 min)
Files: `test/engines/agi/korean_said_data.h` (C.7), `test/engines/agi/agi_test_stubs.cpp` (C.8), test/module.mk (adds
`engines/agi/korparse.o engines/agi/words.o test/engines/agi/agi_test_stubs.o`; explicit objects, never `libagi.a`, so the
six engine symbols `words.o` names come from the stubs and nothing else of the engine is linked).
- [ ] `mkdir -p $BLD/korean-packs/LSL1AGI-said && ln -sfn $P5/data/lsl1ko3 $BLD/korean-packs/LSL1AGI && cp
  $HAR/runs/lsl1a/parse/lsl1a-pairs.tsv $BLD/korean-packs/LSL1AGI-said/pairs.tsv && cp $HAR/runs/lsl1a/src/saids.tsv
  $BLD/korean-packs/LSL1AGI-said/saids.tsv`.
- [ ] Without the folder: `Warning: korean-packs/LSL1AGI and LSL1AGI-said are not in the working directory`, passes.
  With it: **778 / pass 691 / partial 23 / nomatch 20 / unknown 7 / order 6 / english 31** (= jamo's `said8.txt`). Red: set
  the expected pass to 692 for one run -> the assertion names `691 != 692`; restore.
- [ ] Commit `TEST: AGI: Judge the LSL1 walkthrough pairs against its said() specs`.

#### Task c4: console commands (10 min)
Files: `console.{h,cpp}` (B:console.cpp/B:console.h: `koverlay`, `saids`, `parse`, `said`), `agi.h`/`agi.cpp` (`collectSaids`,
`getOpCodesCondTable`).
- [ ] Data (GUI debugger, `ctrl+alt+d` under Xvfb, `type:`): `parse 술집을 봐` prints `#0: 봐=... 술집을=...`; `saids 0`
  lists logic 0's specs (compare the count with `grep -c '^0\t' saids.tsv`); `koverlay`: `entries 1860`, `stale 0`,
  `missing glyphs 0` with NEODG16A. `xshot` the console.
- [ ] Commit `AGI: Add the parse, said, saids and koverlay console commands`.

**Checkpoint B (after c4): review** (section 4).

### P5d: saves (4 tasks)

#### Task d1: the string trailer unit (15 min)
Files: `engines/agi/saved_strings.{h,cpp}` (appendix A.5), module.mk, `test/engines/agi/saved_strings.h` (C.9: 2000 random
tables round trip; every cut of a trailer -> never `kSavedStringsRead`, the table untouched; 20000 random byte damages ->
`Read` only with valid UTF-8 shorter than the stride; no trailer -> `None`; `legacySlot` 13 syllables = 39 bytes; CP850
conversion).
- [ ] Red/green: +5. Commit `AGI: Add the UTF-8 string trailer of a version 11 save`.

#### Task d2: save and restore (15 min)
Files: `saveload.cpp` (B:saveload.cpp: version 11 kept; UTF-8 mode writes `legacySlot()`s and the trailer after AGIPal, reads it
back or converts the v11 table; description cut on a character).
- [ ] Data: F5 with `originalsaveload=true`, name `ans` (문) -> file `lsl1-ko.001`; `python3 -c "import gzip;d=gzip.decompress(open(F,'rb').read());print(d[35],d.rfind(b'AGKO'),len(d))"`
  -> `11`, trailer at the end; F7 restores (log without `broken string trailer`). The unmodified binary lists and loads it
  (English target, same save path).
- [ ] Commit `AGI: Save and restore the UTF-8 strings of a translated game`.

#### Task d3: Korean save names (15 min)
Files: `text.{h,cpp}` (`_inputStringMaxBytes`, `stringFitsBudget`, the composer trial and the code-point/ASCII budget
in `stringKeyPress`), `systemui.{h,cpp}` (`SYSTEMUI_SAVEDGAME_DISPLAYTEXT_BYTES`, `createSavedGameDisplayText` through
`TextCells::savedGameLine`, `askForSaveGameDescription` sets the budget, `readSavedGameSlots` cuts on a character),
`saveload.cpp` (`scummVMSaveLoadDialog` `clipBytes`), `op_cmd.cpp` (`cmdSetSimple`, D14.4).
- [ ] Unit: `test_savedGameLine` is a1's. Data: type `dkssudgktpdy dkssudgktpdy` as the name: `안녕하세요 안녕하셍`
  (28 bytes; the jamo that would start a 31st byte refused); the verification box and the restore list show it; the file's
  description bytes are valid UTF-8 (`d[4:35]`).
- [ ] Commit `AGI: Let a translated game's save names be Korean`.

#### Task d4: P5d smoke (no commit, 10 min)
- [ ] Save in slot 1 (Korean name), walk into Lefty's, save slot 2, restore slot 1: Lefty's outside; restore slot 2:
  inside; `set.string`-dependent text (the password `켄이 보냈다` typed at the door) after a restore still accepted
  (string 0..23 and answers come back from the trailer).
- [ ] ScummVM save dialog (`originalsaveload=false`, GMM save) with a Korean name typed through xdotool's OS-IME path:
  cut to 28 bytes on a character; listed in the AGI restore list.

### P5e: tools and records (2 tasks)

#### Task e1: `strcheck.py` for AGI tables (15 min)
Files: `tools/korean/strcheck.py`, `tools/korean/test_strcheck.py`.
- [ ] Test first: `check` of an AGI table (detected from the header's `engine=agi`, or `--engine agi`): accepts `m0:1`,
  `o3`, `v121`, escapes `\n \t \\`; refuses `m0:0`, `m256:1`, `o256`, `q1`, a missing TAB, a text that is not UTF-8,
  duplicate keys (warning, as the engine); `header` works on AGI tables. Red: `check` reports `m0:1` as a bad SCI key.
- [ ] Commit `TOOLS: Korean: Check agi-<lang>.str tables`.

#### Task e2: records (15 min)
- [ ] `PATCHES.md`: a `ko/04-agi` block, one row per upstream file of appendix B with the reason (section 1 table).
- [ ] `P5-notes.md` complete (every task's evidence; the final numbers of section 5).
- [ ] Commit `DOCS: Korean: Record P5 (AGI)`.

---

## 4. Review checkpoints

At A (after a10), B (after c4) and the end (after e2): one Claude reviewer (Opus) and the external lanes of the owner's
review setup (`dual-review` memory: agy lane + opencode lane for important reviews), each in its own scratch copy, read
only, with this brief:

- Review the diff `ko-2026.4..ko/p5-agi` for the bug classes of P3/P4/P6a: byte vs code point offsets and widths;
  fixed buffers sized in bytes (`strings[][]`, `_prompt`, `_inputString`, `description[31]`, `displayText`, the 2000-byte
  result buffers, `id[8]`, `automaticSaveDescription`); terminators (`toCells` capacity - 1, `readOne` `len >= stride`);
  a cut inside a multibyte sequence anywhere text is shortened; hooks that are not inert when `textIsUtf8()` is false
  (every new branch must test it; Russian/Hebrew/French fan paths unchanged).
- **Every finding must come with a failing test or a reproduced run.** Required of the reviewers, not optional: write
  randomized or exhaustive tests against the pure units (TextCells, TextOverlay, AgiHiResText, HangulLine, korparse,
  saved_strings) and differential tests against upstream's loops (wrap, said, compare.strings, save list line); the
  prototype's own randomized tests found D14.1 in code DESIGN had called "port as is".
- Fixes go back to the task's implementer (review-fix-resume), each as one commit with red/green evidence, recorded in
  `P5-fixes.md` like `P4-fixes.md`.

## 5. Final acceptance checklist

- [ ] `make test` in `$BLD`: N0 + 45 (a1 5, a2 5, a3 10, a4 3, b1 5, c1 11, c3 1, d1 5; prototype: 693 = 648 + 45) + the
  reviewers' additions, OK;
  with `korean-packs/` present the LSL1 said() gate passes with 691/23/20/7/6/31; ASan build: same count, 0 sanitizer
  lines; combined `agi,sci,scumm` build: links, all suites pass; no-engine build: the AGI tests are absent, OK.
- [ ] No compiler warning in `engines/agi` (normal and ASan); every commit passes `check-commit-msg.py`.
- [ ] LSL1 AGI KO (pack in the new formats) plays headless: thank-you box, status line, menu, inventory, a typed Korean
  command (composer and OS IME) answered, a Korean-named save restored; screenshots `agi-p5-*.png` with captions.
- [ ] Quiz-skip pack: the game opens with the thank-you message and Lefty's (no quiz), both languages.
- [ ] English target and `lsl1en`, one fan game: frames identical to the base binary; no Korean log line.
- [ ] A save made in the Korean session loads in the base binary (version 11).
- [ ] `hires_text_log=true` on the smoke path: 0 `no face has` lines.
- [ ] `PATCHES.md`, `P5-notes.md`, `P5-fixes.md` committed; harness dependencies of section 7 handed over (comment on the
  agentws card or a handoff), the jamo-only pieces left out listed (badge, `agistate`/`prompt`/`string`, v12 saves, DOS
  `#ifndef DISABLE_GUI` in `font.cpp`/`keyboard.cpp` -> P7).

## 6. What cannot be verified here

- DOS (`AGI.EXE`, VESA 640x400 CLUT8, the Han/Yeong scancode from a real keyboard): P7.
- A hardware `KEYCODE_HANGUL` press (xdotool sends the other bindings: `ctrl+space`, `shift+space`, `F12`, `Scroll_Lock`).
- OS IMEs with a visible pre-edit (Windows, Android, ibus): xdotool delivers finished syllables only.
- Other render modes and platforms with Korean (Amiga, Apple IIgs, Atari ST, Mac, CGA, Hercules): forced to EGA (D12);
  no Korean data for them.
- An AGI v3 translation (only the v3 fingerprint line is checked on KQ4 AGI, a5), AGI v1 and disk-image games.
- TTS output of Korean (the encoding is set; no speech engine under Xvfb).
- The LSL1 age quiz itself in Korean (the pack skips it; jamo saw a 1-in-4 stall in room 11 on the base binary too).
- The socket-driven `k16said_agi.py` and the harness's `lsl1_play.py`: need `ko/07-debugsocket`.

## 7. Harness-side dependencies (`~/work/scummvm`, not changed by this plan)

1. `build_agi_lsl1.py` / `agi_game_pack.py`: write the header line `#! format=1 game=lsl1
   fingerprint=1d9311bbee31342e76c7570a29d7616d engine=agi` into `ko/agi-ko.str` (strcheck `header`), bake `ko/agi-ko.dat`
   as `AGIKOR` version 3 (`k6bake.py build --format 3 --magic AGIKOR --game lsl1 --fingerprint ... --vocab-md5
   988a1baaf3ee0d9cd678274dc2f219f2`), ship `ko/HIRESBIT.MAP` in map version 3 naming an OFL v3 face
   (`data:hires_text/NEODG16A.SVF`, or a v3 conversion of KO1's NEODG16.SVF), run `strcheck.py check` (Task e1) in the
   builder.
2. Pack texts (`KOREAN.TXT`, README): the keys are `hangul_input`, `hangul_builtin`, `hangul_start` (not `agi_hangul_*`);
   save names may be Korean (10 syllables); saves are version 11 and load in official ScummVM (strings cut, names
   garbled); the KO/EN badge is gone (P8).
3. `runs/lsl1a/parse/lsl1a-pairs.tsv` and `runs/lsl1a/src/saids.tsv` stay where they are (Task c3 copies them); a changed
   `agi-ko.dat` must come with new expected numbers in `korean_said_data.h`.
4. `harness/i18n/agigame.py`, `k16said_agi.py`, `lsl1_play.py`: socket commands `agistate`/`prompt`/`string` and
   `agi_hangul_start` move with `ko/07`.

## 8. UNVERIFIED (as of this plan)

- The `ㄴ` leak seen once with Alt+S (0.3): cause unknown, not reproduced (Task b4 checks it).
- The v3 `logicDirectoryName()` when `_name` is empty (`loader_v3.cpp:86-90`): Task a5 checks on KQ4 AGI.
- CP850 vs CP437 for untranslated high bytes (D13).
- `Common::String::toLowercase()` on bytes >= 0x80 in upstream's plain-ASCII parse path: not reached (the Korean path
  takes every line with a byte >= 0x80; its `asciiLower` folds ASCII only).
- Combined `agi,sci,scumm` test link (explicit AGI objects next to `libsci.a`, `libscumm.a`): not built in the prototype.
- jamo's `korean_keymap.h` / `agi_text_wiring.h` tests read the source tree (`AGI_TEST_SRCDIR`); not carried. The keymap
  gate and the toggle are checked in play (b4) only.

## 9. Conflicts with DESIGN.md

| DESIGN | This plan | Why |
|---|---|---|
| Row P5 "saves v12" (and 2.4) | version 11 + `AGKO` trailer (D7) | no collision with upstream's next version; upstream loads the save (measured) |
| Section 4 `ko/AGIKOR.DAT` | `ko/agi-ko.dat` (D6) | the pack's and SCI's naming (`ko/sci-ko.dat`, PATCHES.md) |
| Section 6 KO/EN badge (`graphics/korean_badge`) | none | SCI has none in this tree; GUI work is P8 |
| Section 2.4 "port mostly as is" | ported, with D14's fixes | randomized tests found a UTF-8 wrap bug in jamo's code |
| Section 10 "AGI gets `state` and `wait text`" over the socket | console `parse/said/saids/koverlay` only; the gate is an offline CxxTest | the socket is `ko/07`, not in the tree |
| Row P5 "textcells, korparse, translation" tests | + textmode, hirestext (exhaustive), hangul_line, saved_strings, said gate | method of the finished phases |
| jamo D8 (save names ASCII only; not in DESIGN) | Korean save names, 30-byte budget (D8) | DESIGN section 7: save names are UTF-8 shown by the GUI font |

---

## Appendix A: new units

Every new `.h`/`.cpp` starts with the GPL header of `engines/agi/agi.cpp` (lines 1-20), left out below. Files marked
"jamo + delta" are `git -C $OLD show jamo:engines/agi/<file>` with the unified diff shown applied (`patch -p0` from
`$SRC`, or by hand).

### A.1 `engines/agi/textcells.{h,cpp}` (jamo + delta: `isValidUtf8`, `utf8Bytes`, `savedGameLine`, D14.1)

```diff
--- jamo:engines/agi/textcells.h
+++ engines/agi/textcells.h
@@ -53,6 +53,19 @@
 int16 cellCount(const uint32 *cells);
 bool isHangulSyllable(uint32 cp);
 
+/** Every byte of @p text up to its NUL belongs to a well-formed UTF-8 character (decode() never falls back to a byte). */
+bool isValidUtf8(const char *text);
+
+/** The UTF-8 bytes of @p cells[0..len). */
+int utf8Bytes(const uint32 *cells, int16 len);
+
+/**
+ * SystemUI's save list line for @p description: "NN:" then the description,
+ * cut to 28 cells (and on a character boundary), padded with @p fill to 31
+ * cells. As createSavedGameDisplayText() for bytes when @p utf8 is false.
+ */
+Common::String savedGameLine(const char *description, int16 slotId, char fill, bool utf8);
+
 /**
  * GfxMenu::addMenuItem()'s drop-down item column (left to right languages):
  * under the menu name, or moved left so that an item of itemCells ends at
```

```diff
--- jamo:engines/agi/textcells.cpp
+++ engines/agi/textcells.cpp
@@ -155,6 +155,36 @@
 	return cp >= 0xAC00 && cp <= 0xD7A3;
 }
 
+int utf8Bytes(const uint32 *cells, int16 len) {
+	int bytes = 0;
+	for (int16 i = 0; i < len; i++)
+		bytes += cells[i] < 0x80 ? 1 : cells[i] < 0x800 ? 2 : cells[i] < 0x10000 ? 3 : 4;
+	return bytes;
+}
+
+bool isValidUtf8(const char *text) {
+	while (*text) {
+		uint32 cp;
+		const int n = decode(text, true, cp);
+		if (n == 1 && (byte)*text >= 0x80)
+			return false;
+		text += n;
+	}
+	return true;
+}
+
+Common::String savedGameLine(const char *description, int16 slotId, char fill, bool utf8) {
+	enum { kLineCells = 31, kPrefixCells = 3 }; // SYSTEMUI_SAVEDGAME_DISPLAYTEXT_LEN, _PREFIX_LEN
+	char prefix[8];
+	Common::sprintf_s(prefix, "%02d:", slotId);
+	Common::String line(prefix, 3);
+	const Common::String text = clipCells(description, kLineCells - kPrefixCells, utf8);
+	line += text;
+	for (int16 cells = kPrefixCells + cellLen(text.c_str(), utf8); fill && cells < kLineCells; cells++)
+		line += fill;
+	return line;
+}
+
 int16 dropDownColumn(int16 menuColumn, int16 itemCells, int16 screenColumns) {
 	if (itemCells + menuColumn < (screenColumns - 1))
 		return menuColumn;
@@ -196,7 +226,7 @@
 			uint32 cp;
 			curReadPos += (int16)decode(&originalText[curReadPos], utf8, cp);
 		}
-		const byte wordEndChar = originalText[curReadPos];
+		byte wordEndChar = originalText[curReadPos];
 		int16 wordLen = curReadPos - wordStartPos;
 		int16 wordCells = measure(&originalText[wordStartPos], wordLen, utf8);
 
@@ -233,6 +263,12 @@
 				wordLen = bytesForMaxWidth;
 				wordCells = maxWidth;
 				curReadPos = wordStartPos + wordLen;
+				// The rest of the word comes next: a line feed after the whole word is
+				// not this piece's. (Upstream's byte loop takes it here and skips the
+				// byte after the cut, a character lost; kept for the byte path, which
+				// stays upstream's. In UTF-8 the skipped byte would be a lead byte.)
+				if (utf8)
+					wordEndChar = 0;
 			}
 			out += '\n';
 			if (lineWidth > boxWidth)
```

### A.2 `engines/agi/textmode.{h,cpp}` (new)

```cpp
#ifndef AGI_TEXTMODE_H
#define AGI_TEXTMODE_H

#include "common/language.h"
#include "common/str.h"

namespace Agi {

/**
 * How an AGI game's text is held and drawn, decided once at engine start
 * (DESIGN.md section 3 policy 1). Upstream behaviour is kept whenever
 * encoding is kSingle: every new branch of the engine tests utf8().
 */
struct TextMode {
	enum Encoding {
		kSingle, ///< the game's own single-byte text (upstream; Russian, Hebrew, French fan fonts included)
		kUtf8    ///< a fork translation: agi-<code>.str, UTF-8 strings, cells = code points, hi-res text, Korean input
	};

	TextMode() : encoding(kSingle), hires(false), language(Common::UNK_LANG) {}

	Encoding encoding;
	bool hires;                 ///< 16x16 face stencils in the 640x400 display
	Common::Language language;  ///< what AgiBase::getLanguage() answers

	bool utf8() const { return encoding == kUtf8; }
};

/** What decideTextMode() looks at; filled from the descriptor, ConfMan and the game folder. */
struct TextModeInputs {
	TextModeInputs() : detected(Common::UNK_LANG), requested(Common::UNK_LANG), koreanText(true),
		translationFolder(false), stringTable(false), hiresText(true) {}

	Common::Language detected;   ///< the detection entry's language
	Common::Language requested;  ///< the target's language= (UNK_LANG: not set or unknown)
	bool koreanText;             ///< korean_text ([scummvm] off switch)
	Common::String textEncoding; ///< text_encoding: auto (or empty), utf8; cp949 is not an AGI encoding
	bool translationFolder;      ///< the requested language's folder (ko/) holds a translation
	bool stringTable;            ///< that folder (or the game folder) holds agi-<code>.str
	bool hiresText;              ///< hires_text
};

/** The text mode for @p in; @p notes gets one line per decision a user may wonder about. */
TextMode decideTextMode(const TextModeInputs &in, Common::String &notes);

} // End of namespace Agi

#endif
```

```cpp
#include "agi/textmode.h"

namespace Agi {

TextMode decideTextMode(const TextModeInputs &in, Common::String &notes) {
	TextMode mode;
	notes.clear();

	// The target's language wins over the detection entry only when the game
	// folder has a translation for it: language=ko on a plain English folder
	// changes nothing.
	mode.language = in.detected;
	if (in.requested != Common::UNK_LANG && in.requested != in.detected) {
		if (in.translationFolder)
			mode.language = in.requested;
		else
			notes += "language set, but the game folder has no translation for it; ";
	}

	Common::String enc = in.textEncoding;
	enc.trim();
	enc.toLowercase();
	if (!enc.empty() && enc != "auto" && enc != "utf8" && enc != "utf-8") {
		notes += Common::String::format("text_encoding=%s is not auto or utf8 for AGI; auto used; ", in.textEncoding.c_str());
		enc = "auto";
	}

	if (!in.koreanText) {
		mode.language = in.detected;
		notes += "korean_text=false; ";
		return mode;
	}

	const bool translated = mode.language != in.detected;
	if (enc == "utf8" || enc == "utf-8")
		mode.encoding = TextMode::kUtf8;
	else if (translated && in.stringTable)
		mode.encoding = TextMode::kUtf8;
	else if (translated && in.translationFolder) {
		// A translation folder is a translation even when its table is missing or misspelt.
		mode.encoding = TextMode::kUtf8;
		notes += "translation folder has no agi string table; ";
	}

	if (mode.utf8()) {
		if (!in.hiresText)
			notes += "hires_text=false: characters AGI's font lacks draw as '?'; ";
		else
			mode.hires = true;
	}
	return mode;
}

} // End of namespace Agi
```

### A.3 `engines/agi/translation.{h,cpp}` (jamo + delta: header, UTF-8 check)

```diff
--- jamo:engines/agi/translation.h
+++ engines/agi/translation.h
@@ -26,6 +26,7 @@
 #include "common/hashmap.h"
 #include "common/str.h"
 #include "common/str-enc.h"
+#include "common/translation_file.h"
 
 namespace Common {
 class SeekableReadStream;
@@ -69,10 +70,12 @@
 	TextOverlay() : _present(false), _loaded(false) {}
 
 	/** agi-<languageCode>.str through SearchMan; true when present. */
-	bool load(const Common::String &languageCode);
+	bool load(const Common::String &languageCode, const Common::TranslationFileHeader &expected = Common::TranslationFileHeader());
 
 	/** The table from an open stream; true when it parsed (entries or not). */
-	bool loadFromStream(Common::SeekableReadStream &in);
+	bool loadFromStream(Common::SeekableReadStream &in, const Common::String &name = "agi-ko.str",
+	                    const Common::TranslationFileHeader &expected = Common::TranslationFileHeader());
+	const Common::TranslationFileHeader &getHeader() const { return _header; }
 
 	/**
 	 * One line, its end stripped: blank and '#' lines are skipped; anything
@@ -113,6 +116,7 @@
 	bool _present;
 	bool _loaded;
 	Common::HashMap<uint32, Common::String> _entries;
+	Common::TranslationFileHeader _header;
 };
 
 } // End of namespace Agi
```

```diff
--- jamo:engines/agi/translation.cpp
+++ engines/agi/translation.cpp
@@ -20,6 +20,7 @@
  */
 
 #include "agi/translation.h"
+#include "agi/textcells.h"
 
 #include "common/file.h"
 #include "common/stream.h"
@@ -28,16 +29,12 @@
 
 namespace Agi {
 
-bool TextOverlay::load(const Common::String &languageCode) {
+bool TextOverlay::load(const Common::String &languageCode, const Common::TranslationFileHeader &expected) {
 	const Common::String name = Common::String::format("agi-%s.str", languageCode.c_str());
 	Common::File f;
 	if (!f.open(Common::Path(name)))
 		return false;
-	if (!loadFromStream(f)) {
-		warning("TextOverlay: %s is malformed, ignored", name.c_str());
-		return false;
-	}
-	return true;
+	return loadFromStream(f, name, expected);
 }
 
 Common::String TextOverlay::unescape(const Common::String &s) {
@@ -109,23 +106,41 @@
 		return kLineError;
 	}
 	text = Common::String(line.c_str() + p + 1);
+	if (!TextCells::isValidUtf8(text.c_str())) {
+		// decode() would draw each stray byte as a character of its own
+		error = "the text is not UTF-8";
+		return kLineError;
+	}
 	return kLineEntry;
 }
 
-bool TextOverlay::loadFromStream(Common::SeekableReadStream &in) {
+bool TextOverlay::loadFromStream(Common::SeekableReadStream &in, const Common::String &name,
+                                 const Common::TranslationFileHeader &expected) {
 	_entries.clear();
 	_present = false;
 	_loaded = false;
+	_header = Common::TranslationFileHeader();
 
 	Common::HashMap<uint32, uint> firstLine;
 	uint lineNo = 0;
 	while (!in.eos()) {
 		Common::String line = in.readLine();
 		lineNo++;
-		if (lineNo == 1 && line.hasPrefix("\xEF\xBB\xBF"))
-			line.erase(0, 3);
 		if (line.hasSuffix("\r"))
 			line.deleteLastChar();
+		if (lineNo == 1) {
+			if (line.hasPrefix("\xEF\xBB\xBF"))
+				line.erase(0, 3);
+			Common::String error;
+			if (!Common::TranslationFileHeader::parse(line, _header, error) && !error.empty()) {
+				warning("TextOverlay: %s: %s, not loaded", name.c_str(), error.c_str());
+				return false;
+			}
+			if (Common::checkTranslationHeader(_header, expected, name) == Common::kTranslationMismatch)
+				return false;
+			if (_header.present())
+				continue;
+		}
 
 		Key key;
 		Common::String text;
@@ -134,7 +149,7 @@
 		if (kind == kLineSkip)
 			continue;
 		if (kind == kLineError) {
-			warning("TextOverlay: line %u: %s", lineNo, error);
+			warning("TextOverlay: %s line %u: %s, not loaded", name.c_str(), lineNo, error);
 			_entries.clear();
 			return false;
 		}
```

### A.4 `engines/agi/hirestext.{h,cpp}` (new, replaces jamo's `HiresText`)

```cpp
#ifndef AGI_HIRESTEXT_H
#define AGI_HIRESTEXT_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/noncopyable.h"
#include "common/path.h"
#include "common/str.h"

#include "graphics/hires_text/font_set.h"
#include "graphics/hires_text/hires_files.h"
#include "graphics/hires_text/hires_options.h"
#include "graphics/hires_text/map_pick.h"
#include "graphics/hires_text/text_plane.h"

namespace Agi {

/**
 * The hi-res text of a Korean AGI game (DESIGN.md section 5.1, AGI row): no
 * text plane on screen. AGI already draws text in its own 640x400 display
 * buffer (GfxMgr DISPLAY_UPSCALED_640x400, CLUT8, EGA indices), one 16x16
 * cell per character, and paints every cell's background. So a face glyph
 * is a 1 bpp stencil the display paints in the cell's foreground over its
 * background, with AGI's invert and greyed-menu transforms. The screen is
 * CLUT8: the map is read with its [font:clut8] sections, a 1 bpp face draws
 * as it is, an anti-aliased face is cut at coverage 128 (with one warning).
 *
 * Basic Latin (below U+0080) is always AGI's own font, doubled, as the
 * English game draws it, so an untranslated line looks exactly as upstream.
 */
class AgiHiResText : Common::NonCopyable {
public:
	/** The engine's map when the game has none (the shipped OFL face). */
	static const char *const kDefaultMap;
	enum { kCell = 16 };

	AgiHiResText();
	~AgiHiResText();

	/** ConfMan's keys, the disk; false (and inactive) when hires_text=false or no map and no face opens. */
	bool setup(const Common::Path &gameDir, const Common::String &langFolder, Common::Array<Common::String> &notes);
	/** setup() with files of the caller's own (tests). */
	bool setup(Graphics::HiResFiles *files, const Graphics::HiResOptions &options, const Common::Path &gameDir,
	           const Common::String &langFolder, Common::Array<Common::String> &notes);

	bool isActive() const { return _set != nullptr; }
	/** [layout] hangul=any (or hangul_break=any): a Hangul word may break between syllables. */
	bool hangulAny() const { return _hangulAny; }
	const Graphics::HiResPick &getPick() const { return _pick; }

	/**
	 * The 16x16 stencil of @p cp, bit 15 of rows[y] the leftmost pixel; false
	 * when AGI's own font draws it: below U+0080, or the map leaves it to the
	 * game (range=original, no face has it and the chain ends in original).
	 * A narrower glyph is centred, the faces' line centred in the cell (shift= moves it down).
	 */
	bool glyph(uint32 cp, uint16 rows[16]);

	/** How many times the anti-aliased-face warning was given (0 or 1). */
	uint coverageWarnings() const { return _coverageWarnings; }

private:
	void clear();

	Graphics::HiResFiles *_files;
	bool _ownFiles;
	Graphics::HiResFaceCache *_faces;
	Graphics::HiResPick _pick;
	Graphics::HiResFontSet *_set;
	Graphics::TextPlane _cell; ///< one 16x16 cell, scale 1: the stencil is read from it
	bool _hangulAny;
	uint _coverageWarnings;
	bool _log;                               ///< hires_text_log
	Common::HashMap<uint32, bool> _missLogged;
};

} // End of namespace Agi

#endif
```

```cpp
#include "common/ptr.h"
#include "common/stream.h"
#include "common/textconsole.h"

#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/id_plan.h"
#include "graphics/hires_text/svfchain.h"
#include "graphics/hires_text/svfface.h"

#include "agi/hirestext.h"

namespace Agi {

const char *const AgiHiResText::kDefaultMap = "data:hires_text/maps/korean-default.map";

AgiHiResText::AgiHiResText() : _files(nullptr), _ownFiles(false), _faces(nullptr), _set(nullptr), _hangulAny(false),
	_coverageWarnings(0), _log(false) {
}

AgiHiResText::~AgiHiResText() {
	clear();
}

void AgiHiResText::clear() {
	delete _set;
	_set = nullptr;
	delete _faces;
	_faces = nullptr;
	if (_ownFiles)
		delete _files;
	_files = nullptr;
	_ownFiles = false;
	_cell.free();
}

bool AgiHiResText::setup(const Common::Path &gameDir, const Common::String &langFolder, Common::Array<Common::String> &notes) {
	Common::Array<Common::String> warnings;
	const Graphics::HiResOptions options = Graphics::readHiResOptions(warnings);
	notes.push_back(warnings);
	Graphics::HiResFiles *files = new Graphics::HiResDiskFiles();
	const bool ok = setup(files, options, gameDir, langFolder, notes);
	_ownFiles = true; // setup() kept @p files in _files, whatever it returned
	return ok;
}

bool AgiHiResText::setup(Graphics::HiResFiles *files, const Graphics::HiResOptions &options, const Common::Path &gameDir,
                         const Common::String &langFolder, Common::Array<Common::String> &notes) {
	clear();
	_files = files;
	Graphics::HiResMapQualifiers q;
	q.engine = "agi";
	q.target = Graphics::kHiResTargetClut8; // AGI's display is CLUT8 (EGA indices): [font:clut8] sections apply
	if (!Graphics::pickHiResMap(options, gameDir, langFolder, q, *_files, _pick, notes)) {
		if (!options.enabled || (options.mapSet && options.map.empty()))
			return false;
		Graphics::HiResOptions fallback = options;
		fallback.mapSet = true;
		fallback.map = kDefaultMap;
		if (!Graphics::pickHiResMap(fallback, gameDir, langFolder, q, *_files, _pick, notes))
			return false;
		notes.push_back(Common::String("using ") + kDefaultMap);
	}
	for (uint i = 0; i < _pick.map.warnings.size(); ++i)
		notes.push_back(_pick.map.warnings[i]);
	if (!_pick.map.fontIds.empty())
		notes.push_back("AGI has one font: the map's [font.N] sections are not used");

	_faces = new Graphics::HiResFaceCache(*_files);
	Common::Array<Common::String> warnings;
	Graphics::HiResFontSet *set = new Graphics::HiResFontSet();
	if (!set->build(Graphics::compileIdPlan(_pick.map, 0, warnings), _pick.map, *_faces, warnings) || !set->getChainCount()) {
		delete set;
		set = nullptr;
	}
	notes.push_back(warnings);
	if (!set) {
		notes.push_back("no face of the map could be opened");
		return false;
	}
	for (uint c = 0; c < set->getChainCount() && !_coverageWarnings; ++c)
		for (uint f = 0; f < set->getChain(c)->getFaceCount() && !_coverageWarnings; ++f)
			if (set->getChain(c)->getFace(f)->getBpp() > 1) {
				++_coverageWarnings;
				warning("Hi-res text: an anti-aliased face on AGI's CLUT8 screen is cut at coverage 128; "
				        "give the map a [font:clut8] section with a 1 bpp face");
			}
	if (!_cell.create(kCell, kCell, 1)) {
		delete set;
		return false;
	}
	_set = set;
	_log = options.log;
	_hangulAny = _pick.map.layout.hangulSet && _pick.map.layout.hangul == Graphics::kHangulBreakAny;
	if (options.hangulBreakSet)
		_hangulAny = options.hangulBreak == Graphics::kHangulBreakAny;
	return true;
}

bool AgiHiResText::glyph(uint32 cp, uint16 rows[16]) {
	if (!_set || cp < 0x80 || _set->route(cp) < 0)
		return false;
	_cell.clear();
	const int advance = CLIP<int>(_set->getAdvance(cp), 0, kCell);
	const int x = (kCell - advance) / 2;
	const int line = _set->getAscent() + _set->getDescent();
	const int baseline = (kCell - line) / 2 + _set->getAscent(); // drawChar() adds shift=
	const Common::Rect clip(0, 0, kCell, kCell);
	_set->drawChar(cp, x, baseline, _cell, 1, &clip);
	// hires_text_log: each character no face has (drawn as the missing box) once, for the harness's coverage diff
	if (_log && !_set->getChain(_set->route(cp))->has(cp) && !_missLogged.contains(cp)) {
		_missLogged[cp] = true;
		warning("Hi-res text: no face has U+%04X", cp);
	}
	for (int y = 0; y < kCell; ++y) {
		uint16 bits = 0;
		for (int px = 0; px < kCell; ++px)
			if (_cell.getCoverage(px, y) >= 128)
				bits |= (uint16)(0x8000 >> px);
		rows[y] = bits;
	}
	return true;
}

} // End of namespace Agi
```

### A.5 `engines/agi/saved_strings.{h,cpp}` (new, replaces jamo's `savestrings`)

```cpp
#ifndef AGI_SAVED_STRINGS_H
#define AGI_SAVED_STRINGS_H

#include "common/endian.h"
#include "common/str.h"
#include "common/stream.h"

namespace Agi {

/**
 * The script strings of a save made in UTF-8 text mode (TextMode::kUtf8).
 *
 * The save stays version 11, readable by upstream ScummVM: its string table
 * holds each slot's longest prefix of whole characters that fits the 40-byte
 * field (legacySlot()). The whole UTF-8 strings and the English originals
 * compare.strings also accepts (AgiGame::stringAnswer) follow the version 11
 * body as a trailer, which upstream's loader never reads:
 *
 *     "AGKO" u8 version (1) u8 count, then count x (u8 length, bytes) for the
 *     strings, then count x (u8 length, bytes) for the answers.
 *
 * Every length is below the slot's byte stride and every string is valid
 * UTF-8, or the whole trailer is refused (the version 11 table is used).
 */
enum {
	kSavedStringsTag = MKTAG('A', 'G', 'K', 'O'),
	kSavedStringsVersion = 1,
	kLegacySlotBytes = 40 ///< MAX_STRINGLEN: a version 11 slot, terminator included
};

/** The version 11 slot for @p text: its longest prefix of whole UTF-8 characters of at most 39 bytes, NUL padded. */
void legacySlot(const char *text, char out[kLegacySlotBytes]);

/** The trailer for @p count slots of @p table (@p stride bytes apart) and @p answers. */
void writeSavedStrings(Common::WriteStream &out, const char *table, uint stride, uint count, const Common::String *answers);

enum SavedStringsResult {
	kSavedStringsNone,   ///< no trailer at the stream's position (an upstream save, or one made in English): nothing changed
	kSavedStringsRead,   ///< table and answers replaced
	kSavedStringsBroken  ///< a trailer that is cut or malformed: nothing changed
};

/** Reads the trailer at the stream's position into @p table and @p answers, all or nothing. */
SavedStringsResult readSavedStrings(Common::ReadStream &in, char *table, uint stride, uint count, Common::String *answers);

/**
 * A save without the trailer read in UTF-8 text mode: each of the @p count
 * slots holds the game's code page; turn a slot with bytes >= 0x80 into
 * UTF-8, cut to @p maxCells cells and @p stride - 1 bytes. ASCII stays as it is.
 */
void convertLegacyStrings(char *table, uint stride, uint count, int16 maxCells);

} // End of namespace Agi

#endif
```

```cpp
#include "common/array.h"
#include "common/str-enc.h"
#include "common/util.h"

#include "agi/saved_strings.h"
#include "agi/textcells.h"

namespace Agi {

void legacySlot(const char *text, char out[kLegacySlotBytes]) {
	memset(out, 0, kLegacySlotBytes);
	const Common::String cut = TextCells::clipBytes(text, kLegacySlotBytes - 1, true);
	memcpy(out, cut.c_str(), cut.size());
}

static void writeOne(Common::WriteStream &out, const char *s, uint stride) {
	// A slot is at most stride - 1 bytes by construction (setString()); the
	// clip only guards a table someone else filled.
	const Common::String cut = TextCells::clipBytes(s, (int16)MIN<uint>(stride - 1, 255), true);
	out.writeByte((byte)cut.size());
	out.write(cut.c_str(), cut.size());
}

void writeSavedStrings(Common::WriteStream &out, const char *table, uint stride, uint count, const Common::String *answers) {
	out.writeUint32BE(kSavedStringsTag);
	out.writeByte(kSavedStringsVersion);
	out.writeByte((byte)count);
	for (uint i = 0; i < count; i++)
		writeOne(out, table + i * stride, stride);
	for (uint i = 0; i < count; i++)
		writeOne(out, answers[i].c_str(), stride);
}

static bool readOne(Common::ReadStream &in, uint stride, Common::String &out) {
	const uint len = in.readByte();
	if (in.eos() || in.err() || len >= stride)
		return false;
	Common::Array<char> buf;
	buf.resize(len + 1);
	if (len && in.read(buf.data(), len) != len)
		return false;
	buf[len] = 0;
	if (strlen(buf.data()) != len || !TextCells::isValidUtf8(buf.data()))
		return false;
	out = Common::String(buf.data(), len);
	return true;
}

SavedStringsResult readSavedStrings(Common::ReadStream &in, char *table, uint stride, uint count, Common::String *answers) {
	const uint32 tag = in.readUint32BE();
	if (in.eos() || tag != kSavedStringsTag)
		return kSavedStringsNone;
	const byte version = in.readByte();
	const byte n = in.readByte();
	if (in.eos() || version != kSavedStringsVersion || n != count)
		return kSavedStringsBroken;
	Common::Array<Common::String> strings, originals;
	strings.resize(count);
	originals.resize(count);
	for (uint i = 0; i < count; i++)
		if (!readOne(in, stride, strings[i]))
			return kSavedStringsBroken;
	for (uint i = 0; i < count; i++)
		if (!readOne(in, stride, originals[i]))
			return kSavedStringsBroken;
	for (uint i = 0; i < count; i++) {
		memset(table + i * stride, 0, stride);
		memcpy(table + i * stride, strings[i].c_str(), strings[i].size());
		answers[i] = originals[i];
	}
	return kSavedStringsRead;
}

void convertLegacyStrings(char *table, uint stride, uint count, int16 maxCells) {
	for (uint i = 0; i < count; i++) {
		char *slot = table + i * stride;
		if (!TextCells::hasHighBytes(slot))
			continue;
		const Common::String u = Common::String(slot).decode(Common::kDos850).encode(Common::kUtf8);
		const Common::String cells = TextCells::clipCells(u.c_str(), maxCells, true);
		const Common::String cut = TextCells::clipBytes(cells.c_str(), (int16)(stride - 1), true);
		memset(slot, 0, stride);
		memcpy(slot, cut.c_str(), cut.size());
	}
}

} // End of namespace Agi
```

### A.6 `engines/agi/hangulline.{h,cpp}` (jamo + delta: the badge removed)

```diff
--- jamo:engines/agi/hangulline.h
+++ engines/agi/hangulline.h
@@ -23,7 +23,6 @@
 #define AGI_HANGULLINE_H
 
 #include "common/hangul.h"
-#include "graphics/korean_badge.h"
 
 namespace Agi {
 
@@ -71,23 +70,6 @@
 	int16 _runStart;
 };
 
-/** What the KO/EN badge shows. */
-enum KoreanBadge {
-	kKoreanBadgeNone,
-	kKoreanBadgeEnglish,
-	kKoreanBadgeKorean
-};
-
-/**
- * D10: the KO/EN badge is up exactly while Korean typing is on for the game
- * (agi_hangul_input in UTF-8 text mode); it shows the mode; otherwise
- * nothing is drawn. (The decision, apart from the backend call below.)
- */
-KoreanBadge koreanBadgeFor(bool hangulInput, bool hangulMode);
-
-/** Put koreanBadgeFor() on screen. show() is idempotent, so this may run often. */
-void updateKoreanBadge(Graphics::KoreanInputIndicator &badge, bool hangulInput, bool hangulMode);
-
 } // End of namespace Agi
 
 #endif
```

```diff
--- jamo:engines/agi/hangulline.cpp
+++ engines/agi/hangulline.cpp
@@ -64,18 +64,4 @@
 	_runStart = len;
 }
 
-KoreanBadge koreanBadgeFor(bool hangulInput, bool hangulMode) {
-	if (!hangulInput)
-		return kKoreanBadgeNone;
-	return hangulMode ? kKoreanBadgeKorean : kKoreanBadgeEnglish;
-}
-
-void updateKoreanBadge(Graphics::KoreanInputIndicator &badge, bool hangulInput, bool hangulMode) {
-	const KoreanBadge b = koreanBadgeFor(hangulInput, hangulMode);
-	if (b == kKoreanBadgeNone)
-		badge.clear();
-	else
-		badge.show(b == kKoreanBadgeKorean);
-}
-
 } // End of namespace Agi
```

### A.7 `engines/agi/korparse.{h,cpp}`, `engines/agi/saidscan.{h,cpp}` (jamo, unchanged)

```sh
for f in korparse.h korparse.cpp saidscan.h saidscan.cpp; do git -C $OLD show jamo:engines/agi/$f > $SRC/engines/agi/$f; done
sha256sum $SRC/engines/agi/{korparse,saidscan}.{h,cpp}   # expected:
# f77f1878aeeddf55d31e32f99aec88808755c1ac8ac9d421d46a032b146124a9  korparse.h
# 64fa216c641a28ca84b80ec49a8954a0d095a1d2b3b78e303a436dc5cc18f3b5  korparse.cpp
# 06ff48d4c8bb5089a32f5065c707b886af99a44879e098f202ead8a197994b02  saidscan.h
# 27bb8b7474c38589af4ac6108d6e60ac1b8eea489c486c2f97d2bb94c960549b  saidscan.cpp
```

### A.9 Scratch map `$P5/data/lsl1ko3/ko/HIRESBIT.MAP` (Task a0; the harness pack's map in version 3, section 7)

```ini
; LSL1 AGI, Korean (P5 prototype, scratch): the pack's map in version 3.
[map]
version=3

[fonts]
ui=data:hires_text/NEODG16A.SVF

[font]
face=ui
missing=u+25a1
range.basic-latin=original
```

## Appendix B: upstream hooks (unified diffs against `ko-2026.4`, `-U2`)

One section per file, the prototype's diff (`~/work/tmp/p5/plan/hook-*.diff`). The task that owns each hunk is named in
section 3 by function; `git apply --include=...` or apply by hand.

### B:agi.cpp

```diff
diff --git a/engines/agi/agi.cpp b/engines/agi/agi.cpp
index 95d4ad40..78357a7f 100644
--- a/engines/agi/agi.cpp
+++ b/engines/agi/agi.cpp
@@ -22,4 +22,7 @@
 #include "common/md5.h"
 #include "common/file.h"
+#include "common/fs.h"
+#include "common/ptr.h"
+#include "common/language.h"
 #include "common/memstream.h"
 #include "common/savefile.h"
@@ -42,8 +45,12 @@
 #include "agi/font.h"
 #include "agi/graphics.h"
+#include "agi/hirestext.h"
+#include "common/language_folder.h"
+#include "common/translation_file.h"
 #include "agi/inv.h"
 #include "agi/loader.h"
 #include "agi/sprite.h"
 #include "agi/text.h"
+#include "agi/textcells.h"
 #include "agi/keyboard.h"
 #include "agi/menu.h"
@@ -155,4 +162,7 @@ int AgiEngine::agiInit() {
 	if (ec == errOK)
 		ec = _loader->loadWords();
+	if (ec == errOK && textIsUtf8())
+		_words->loadKoreanTable();
+	_text->hangulSetup(_words->hasKoreanWords());
 
 	// Load logic 0 into memory
@@ -345,4 +355,5 @@ struct GameSettings {
 AgiBase::AgiBase(OSystem *syst, const AGIGameDescription *gameDesc) : Engine(syst), _gameDescription(gameDesc) {
 	_noSaveLoadAllowed = false;
+	_hiresText = nullptr;
 
 	_rnd = new Common::RandomSource("agi");
@@ -356,4 +367,13 @@ AgiBase::~AgiBase() {
 	delete _rnd;
 	delete _sound;
+	delete _hiresText;
+}
+
+bool AgiBase::hiresTextActive() const {
+	return _hiresText && _hiresText->isActive();
+}
+
+bool AgiBase::hiresHangulAny() const {
+	return _hiresText && _hiresText->hangulAny();
 }
 
@@ -418,4 +438,14 @@ AgiEngine::AgiEngine(OSystem *syst, const AGIGameDescription *gameDesc) : AgiBas
 	memset(&_debug, 0, sizeof(struct AgiDebug));
 
+	// The fork's translations (DESIGN.md sections 3, 4): decided once from
+	// the target's language and the game folder; the language folder (ko/)
+	// goes before the game folder in SearchMan, so its files win.
+	const Common::FSNode gameDataDir(ConfMan.getPath("path"));
+	_textMode = decideTextMode(textModeInputs(gameDataDir), _textModeNotes);
+	if (!_textModeNotes.empty())
+		debugC(1, kDebugLevelText, "Text mode: %s", _textModeNotes.c_str());
+	if (_textMode.utf8() && *Common::getLanguageFolderCode(_textMode.language))
+		SearchMan.addSubDirectoryMatching(gameDataDir, Common::getLanguageFolderCode(_textMode.language), 1);
+
 	_game.mouseEnabled = ConfMan.getBool("mousesupport");
 	_game.mouseHidden = !_game.mouseEnabled;
@@ -516,6 +546,35 @@ void AgiEngine::initialize() {
 	}
 
+	// The text mode decides the screen mode and the render mode (DESIGN.md
+	// section 5.1, AGI row), so the hi-res text is set up before either.
 	initRenderMode();
 
+	if (_textMode.hires) {
+		_hiresText = new AgiHiResText();
+		Common::Array<Common::String> notes;
+		if (!_hiresText->setup(ConfMan.getPath("path"), Common::getLanguageFolderCode(_textMode.language), notes)) {
+			warning("AGI: no hi-res text map or face could be opened; characters AGI's font lacks draw as '?'");
+			delete _hiresText;
+			_hiresText = nullptr;
+		}
+		for (uint i = 0; i < notes.size(); ++i)
+			debugC(1, kDebugLevelText, "Hi-res text: %s", notes[i].c_str());
+		if (_hiresText) {
+			// D3: Korean is drawn in the upscaled PC EGA rendering only, so
+			// every other render mode (VGA too, preflight ruling F11) gives
+			// way to EGA. An AGI256 game keeps the VGA it needs for its
+			// 256-colour pictures.
+			if (_renderMode != Common::kRenderEGA && !(getFeatures() & GF_AGI256)) {
+				warning("AGI: render mode %s is not used with this translation; EGA instead",
+				        Common::getRenderModeDescription(_renderMode));
+				_renderMode = Common::kRenderEGA;
+			}
+			if (ConfMan.getBool("herculesfont")) {
+				warning("AGI: the Hercules font is not used with this translation");
+				ConfMan.setBool("herculesfont", false, Common::ConfigManager::kTransientDomain);
+			}
+		}
+	}
+
 	setDebugger(new Console(this));
 	_words = new Words(this);
@@ -552,5 +611,8 @@ void AgiEngine::initialize() {
 	}
 	_loader->init();
-	
+
+	if (textIsUtf8())
+		loadTranslationOverlay();
+
 	// finally set up actual VM opcodes, because we should now have figured out the right AGI version
 	setupOpCodes(getVersion());
@@ -574,7 +636,106 @@ void AgiEngine::initialize() {
 		break;
 	}
+	if (textIsUtf8())
+		_ttsEncoding = Common::CodePage::kUtf8;
 #endif
 }
 
+// Whether @p folder holds a file named @p name, in any case.
+static bool folderHasFile(const Common::FSNode &folder, const Common::String &name) {
+	Common::FSList children;
+	if (!folder.getChildren(children, Common::FSNode::kListFilesOnly))
+		return false;
+	for (uint i = 0; i < children.size(); ++i)
+		if (children[i].getName().equalsIgnoreCase(name))
+			return true;
+	return false;
+}
+
+TextModeInputs AgiEngine::textModeInputs(const Common::FSNode &gameDataDir) const {
+	TextModeInputs in;
+	in.detected = _gameDescription->desc.language;
+	if (ConfMan.hasKey("language"))
+		in.requested = Common::parseLanguage(ConfMan.get("language"));
+	in.koreanText = !ConfMan.hasKey("korean_text") || ConfMan.getBool("korean_text");
+	if (ConfMan.hasKey("text_encoding"))
+		in.textEncoding = ConfMan.get("text_encoding");
+	Common::FSList files;
+	if (in.requested != Common::UNK_LANG && gameDataDir.getChildren(files, Common::FSNode::kListAll)) {
+		const Common::Array<Common::Language> langs = Common::translationFolderLanguages(files, "agi-%s.str");
+		for (uint i = 0; i < langs.size(); ++i)
+			in.translationFolder = in.translationFolder || langs[i] == in.requested;
+		const Common::String table = Common::translationTableName("agi-%s.str", in.requested);
+		const Common::String code = Common::getLanguageFolderCode(in.requested);
+		for (uint i = 0; i < files.size() && !in.stringTable && !table.empty(); ++i) {
+			if (files[i].isDirectory() ? files[i].getName().equalsIgnoreCase(code) : files[i].getName().equalsIgnoreCase(table))
+				in.stringTable = !files[i].isDirectory() || folderHasFile(files[i], table);
+		}
+	}
+	in.hiresText = !ConfMan.hasKey("hires_text") || ConfMan.getBool("hires_text");
+	return in;
+}
+
+// The bytes of @p name through SearchMan, appended to @p out; false when it cannot be read.
+static bool appendFile(const char *name, Common::Array<byte> &out) {
+	Common::File f;
+	if (!f.open(Common::Path(name)))
+		return false;
+	const uint32 size = f.size();
+	const uint32 at = out.size();
+	out.resize(at + size);
+	return f.read(out.data() + at, size) == size;
+}
+
+static Common::String md5Of(const Common::Array<byte> &bytes) {
+	Common::MemoryReadStream s(bytes.data(), bytes.size());
+	return Common::computeStreamMD5AsString(s);
+}
+
+Common::TranslationFileHeader AgiEngine::translationHeader(bool withVocab) const {
+	// DESIGN.md section 4: the md5 of what the keys depend on, the logic
+	// directory (m<logic>:<n> keys) followed by WORDS.TOK; vocab= is the md5
+	// of WORDS.TOK alone (agi-<code>.dat maps Korean words to its groups).
+	Common::TranslationFileHeader h;
+	h.format = 1;
+	h.game = _gameDescription->desc.gameId;
+	h.engine = "agi";
+	Common::Array<byte> dir, words;
+	const Common::Path dirName = _loader ? _loader->logicDirectoryName() : Common::Path();
+	if (dirName.empty() || !appendFile(dirName.toString().c_str(), dir) || !appendFile("words.tok", words))
+		return h; // no fingerprint to compare (disk images, AGI v1): game and engine only
+	Common::Array<byte> both(dir);
+	both.insert_at(both.size(), words);
+	h.fingerprint = md5Of(both);
+	if (withVocab)
+		h.vocab = md5Of(words);
+	return h;
+}
+
+void AgiEngine::loadTranslationOverlay() {
+	const Common::String code = Common::getLanguageFolderCode(_textMode.language);
+	if (code.empty())
+		return;
+	const Common::TranslationFileHeader expected = translationHeader(false);
+	if (_overlay.load(code, expected)) {
+		if (!_overlay.getHeader().present())
+			warning("This game's agi-%s.str header line is: %s", code.c_str(), expected.toString().c_str());
+		debugC(1, kDebugLevelText, "agi-%s.str: %u entries", code.c_str(), _overlay.entryCount());
+	} else if (!_overlay.isPresent()) {
+		warning("The %s translation folder has no usable agi-%s.str: the game's text stays in English", code.c_str(), code.c_str());
+	}
+}
+
+bool AgiEngine::rawLogicMessageCount(int16 logicNr, int &count) {
+	if (logicNr < 0 || logicNr >= MAX_DIRECTORY_ENTRIES || _game.dirLogic[logicNr].offset == _EMPTY)
+		return false;
+	uint8 *data = _loader->loadVolumeResource(&_game.dirLogic[logicNr]);
+	if (!data)
+		return false;
+	const uint16 bytecodeSize = READ_LE_UINT16(data);
+	count = data[2 + bytecodeSize];
+	free(data);
+	return true;
+}
+
 bool AgiEngine::promptIsEnabled() {
 	return _text->promptIsEnabled();
@@ -855,5 +1016,12 @@ const char *AgiGame::getString(int number) {
 void AgiGame::setString(int number, const char *str) {
 	if (0 <= number && number <= MAX_STRINGS) {
-		Common::strlcpy(strings[number], str, MAX_STRINGLEN);
+		if (_vm->textIsUtf8()) {
+			// 39 cells, cut on a character boundary.
+			const Common::String clipped = TextCells::clipCells(str, MAX_STRINGLEN - 1, true);
+			Common::strlcpy(strings[number], clipped.c_str(), MAX_STRINGBYTES);
+			stringAnswer[number].clear();
+		} else {
+			Common::strlcpy(strings[number], str, MAX_STRINGLEN);
+		}
 	} else {
 		// Occurs in Groza, number = 150
@@ -862,4 +1030,15 @@ void AgiGame::setString(int number, const char *str) {
 }
 
+void AgiGame::setStringFromMessage(int number, uint16 textIndex) {
+	setString(number, _curLogic->texts[textIndex]);
+	if (!_vm->textIsUtf8() || number < 0 || number > MAX_STRINGS || !_curLogic->rawTexts)
+		return;
+	const char *raw = _curLogic->rawTexts[textIndex];
+	if (raw && raw != _curLogic->texts[textIndex]) {
+		const Common::String english = TextOverlay::legacyToUtf8(raw, Common::kDos850);
+		stringAnswer[number] = TextCells::clipCells(english.c_str(), MAX_STRINGLEN - 1, true);
+	}
+}
+
 void AgiGame::setSpeedLevel(byte s) {
 	speedLevel = s;
@@ -882,3 +1061,17 @@ void AgiGame::setSpeedLevel(byte s) {
 }
 
+void AgiEngine::collectSaids(int16 onlyLogic, Common::Array<SaidSpec> &out) {
+	if (getVersion() < 0x2000)
+		return; // v1 said() opcodes differ
+	for (int16 nr = 0; nr < MAX_DIRECTORY_ENTRIES; nr++) {
+		if ((onlyLogic >= 0 && nr != onlyLogic) || _game.dirLogic[nr].offset == _EMPTY)
+			continue;
+		uint8 *data = _loader->loadVolumeResource(&_game.dirLogic[nr]);
+		if (!data)
+			continue;
+		SaidScanner::scan(data + 2, READ_LE_UINT16(data), _opCodes, _opCodesCond, nr, out);
+		free(data);
+	}
+}
+
 } // End of namespace Agi
```

### B:agi.h

```diff
diff --git a/engines/agi/agi.h b/engines/agi/agi.h
index b3ced461..b618d4fe 100644
--- a/engines/agi/agi.h
+++ b/engines/agi/agi.h
@@ -45,4 +45,7 @@
 #include "agi/logic.h"
 #include "agi/sound.h"
+#include "agi/textmode.h"
+#include "agi/translation.h"
+#include "agi/saidscan.h"
 
 namespace Common {
@@ -83,4 +86,7 @@ namespace Agi {
 #define MAX_STRINGS                24         // MAX_STRINGS + 1 used for get.num
 #define MAX_STRINGLEN              40
+// MAX_STRINGLEN counts cells (39 and a terminator, as always); a UTF-8
+// string of that many cells needs up to 4 bytes a cell.
+#define MAX_STRINGBYTES            (MAX_STRINGLEN * 4 + 1)
 #define MAX_CONTROLLER_KEYMAPPINGS 39
 
@@ -409,5 +415,11 @@ struct AgiGame {
 	AgiControllerKeyMapping controllerKeyMapping[MAX_CONTROLLER_KEYMAPPINGS];
 
-	char strings[MAX_STRINGS + 1][MAX_STRINGLEN]; /**< strings */
+	char strings[MAX_STRINGS + 1][MAX_STRINGBYTES]; /**< strings */
+	/**
+	 * UTF-8 text mode: the English original of a translated message that
+	 * set.string copied into the slot, so compare.strings accepts the
+	 * player typing either (design 5.3, D2). Cleared by any other write.
+	 */
+	Common::String stringAnswer[MAX_STRINGS + 1];
 
 	// directory entries for resources
@@ -462,4 +474,5 @@ struct AgiGame {
 	const char *getString(int number);
 	void setString(int number, const char *str);
+	void setStringFromMessage(int number, uint16 textIndex);
 	/**
 	 * Sets the speed level and displays a message box.
@@ -545,4 +558,5 @@ struct AgiGame {
 class AgiLoader;
 class GfxFont;
+class AgiHiResText;
 class GfxMgr;
 class SpritesMgr;
@@ -571,4 +585,10 @@ struct ImageStackElement {
 #define KEY_QUEUE_SIZE 16
 
+/** Keymapper custom engine actions. */
+enum AgiAction {
+	kAgiActionNone = 0,
+	kAgiActionHangulToggle = 1
+};
+
 class AgiBase : public ::Engine {
 protected:
@@ -606,4 +626,14 @@ public:
 	bool _noSaveLoadAllowed;
 
+	/** How the game's text is held and drawn (agi/textmode.h); decided once at start. */
+	TextMode _textMode;
+	Common::String _textModeNotes;
+	/** A fork translation: every displayed string is UTF-8 and every layout site counts cells (code points). */
+	bool textIsUtf8() const { return _textMode.utf8(); }
+	/** The 16x16 face stencils (TextMode::hires with a map); nullptr otherwise. */
+	AgiHiResText *_hiresText;
+	bool hiresTextActive() const;
+	bool hiresHangulAny() const;
+
 	virtual bool promptIsEnabled() {
 		return false;
@@ -728,4 +758,12 @@ private:
 public:
 	Common::Array<AgiObject> _objects;    // objects in the game
+	TextOverlay _overlay;                 // agi-<lang>.str, when the target has one
+	void loadTranslationOverlay();
+	TextModeInputs textModeInputs(const Common::FSNode &gameDataDir) const;
+	/** The fingerprint header the game's agi-<code>.str (and with @p withVocab agi-<code>.dat) must carry. */
+	Common::TranslationFileHeader translationHeader(bool withVocab) const;
+	void translateLogicTexts(int16 logicNr);
+	/** Message count of a logic read straight from its volume (no load); false if absent. */
+	bool rawLogicMessageCount(int16 logicNr, int &count);
 
 	SavedGameSlotIdArray getSavegameSlotIds();
@@ -805,5 +843,5 @@ public:
 public:
 	void decrypt(uint8 *mem, int len);
-	uint16 processAGIEvents();
+	int processAGIEvents();
 	int runGame();
 
@@ -865,4 +903,6 @@ public:
 	bool testPosn(uint8 n, uint8 x1, uint8 y1, uint8 x2, uint8 y2);
 	bool testSaid(uint8 nwords, uint8 *cc);
+	bool testSaidSpec(const uint16 *spec, int nwords);
+	void collectSaids(int16 onlyLogic, Common::Array<SaidSpec> &out);
 	bool testController(uint8 cont);
 	bool testCompareStrings(uint8 s1, uint8 s2);
@@ -973,4 +1013,5 @@ private:
 public:
 	const AgiOpCodeEntry *getOpCodesTable() { return _opCodes; }
+	const AgiOpCodeEntry *getOpCodesCondTable() { return _opCodesCond; }
 
 private:
```

### B:console.cpp

```diff
diff --git a/engines/agi/console.cpp b/engines/agi/console.cpp
index 011b8da5..22004ab8 100644
--- a/engines/agi/console.cpp
+++ b/engines/agi/console.cpp
@@ -23,5 +23,9 @@
 #include "agi/opcodes.h"
 #include "agi/graphics.h"
+#include "agi/hirestext.h"
 #include "agi/loader.h"
+#include "agi/textcells.h"
+#include "agi/words.h"
+#include "agi/korparse.h"
 
 #include "agi/preagi/preagi.h"
@@ -29,4 +33,5 @@
 #include "agi/preagi/winnie.h"
 
+#include "common/algorithm.h"
 #include "common/file.h"
 
@@ -59,4 +64,8 @@ Console::Console(AgiEngine *vm) : GUI::Debugger() {
 	registerCmd("disableautosave", WRAP_METHOD(Console, Cmd_DisableAutomaticSave));
 	registerCmd("diskdump",        WRAP_METHOD(Console, Cmd_DiskDump));
+	registerCmd("koverlay",        WRAP_METHOD(Console, Cmd_KOverlay));
+	registerCmd("saids",           WRAP_METHOD(Console, Cmd_Saids));
+	registerCmd("parse",           WRAP_METHOD(Console, Cmd_Parse));
+	registerCmd("said",            WRAP_METHOD(Console, Cmd_Said));
 }
 
@@ -752,3 +761,186 @@ bool WinnieConsole::Cmd_CurRoom(int argc, const char **argv) {
 }
 
+bool Console::Cmd_KOverlay(int argc, const char **argv) {
+	if (!_vm->textIsUtf8()) {
+		debugPrintf("no translation overlay (UTF-8 text mode is off)\n");
+		return true;
+	}
+	debugPrintf("entries %u\n", _vm->_overlay.entryCount());
+	Common::Array<TextOverlay::Key> keys;
+	_vm->_overlay.collectKeys(keys);
+	Common::Array<Common::String> stale;
+	for (uint i = 0; i < keys.size(); i++) {
+		const TextOverlay::Key &k = keys[i];
+		bool ok;
+		if (k.kind == TextOverlay::kKeyObject) {
+			ok = k.number < _vm->_game.numObjects;
+		} else if (k.kind == TextOverlay::kKeyView) {
+			ok = k.number < MAX_DIRECTORY_ENTRIES && _vm->_game.dirView[k.number].offset != _EMPTY;
+		} else {
+			int count = 0;
+			ok = _vm->rawLogicMessageCount(k.logic, count) && k.number <= count;
+		}
+		if (!ok)
+			stale.push_back(TextOverlay::keyName(k));
+	}
+	debugPrintf("stale %u\n", stale.size());
+	for (uint i = 0; i < stale.size() && i < 20; i++)
+		debugPrintf("  %s\n", stale[i].c_str());
+	uint untranslated = 0;
+	for (int16 nr = 0; nr < MAX_DIRECTORY_ENTRIES; nr++) {
+		if (_vm->_game.dirLogic[nr].offset == _EMPTY)
+			continue;
+		uint8 *data = _vm->_loader->loadVolumeResource(&_vm->_game.dirLogic[nr]);
+		if (!data)
+			continue;
+		const uint16 size = READ_LE_UINT16(data);
+		const int count = data[2 + size];
+		const int pos = 2 + size;
+		for (int n = 1; n <= count; n++) {
+			const uint16 off = READ_LE_UINT16(data + pos + 3 + (n - 1) * 2);
+			if (off && !_vm->_overlay.message(nr, n))
+				untranslated++;
+		}
+		free(data);
+	}
+	debugPrintf("untranslated %u\n", untranslated);
+
+	// Characters no face can draw, and message boxes that would not fit
+	// (more than 20 wrapped lines at AGI's default box width, 30 cells).
+	Common::Array<uint32> missing;
+	Common::Array<Common::String> tall;
+	for (uint i = 0; i < keys.size(); i++) {
+		const TextOverlay::Key &k = keys[i];
+		const Common::String *t = k.kind == TextOverlay::kKeyObject ? _vm->_overlay.object(k.number)
+		                        : k.kind == TextOverlay::kKeyView   ? _vm->_overlay.view(k.number)
+		                                                            : _vm->_overlay.message(k.logic, k.number);
+		if (!t)
+			continue;
+		for (const char *p = t->c_str(); *p;) {
+			uint32 cp;
+			p += TextCells::decode(p, true, cp);
+			if (cp == '\n' || cp == '\t')
+				continue;
+			uint16 rows[16];
+			const bool ok = cp < 0x80 || (_vm->_hiresText && _vm->_hiresText->glyph(cp, rows));
+			if (!ok && Common::find(missing.begin(), missing.end(), cp) == missing.end())
+				missing.push_back(cp);
+		}
+		if (k.kind != TextOverlay::kKeyObject) {
+			Common::String out;
+			int16 w, h;
+			bool truncated = false;
+			TextCells::wordWrap(t->c_str(), 30, true, _vm->hiresHangulAny(), out, w, h, &truncated);
+			if (truncated || h > 20)
+				tall.push_back(TextOverlay::keyName(k));
+		}
+	}
+	debugPrintf("missing glyphs %u\n", missing.size());
+	for (uint i = 0; i < missing.size() && i < 20; i++)
+		debugPrintf("  U+%04X\n", missing[i]);
+	debugPrintf("boxes over 20 lines %u\n", tall.size());
+	for (uint i = 0; i < tall.size() && i < 20; i++)
+		debugPrintf("  %s\n", tall[i].c_str());
+	return true;
+}
+
+static Common::String joinArgs(int from, int to, const char **argv) {
+	Common::String s;
+	for (int i = from; i < to; i++) {
+		if (i > from)
+			s += ' ';
+		s += argv[i];
+	}
+	return s;
+}
+
+static Common::String describe(const AgiSentence &s) {
+	static const char *const roles[] = { "", "obj", "of", "to", "at", "from", "with", "", "" };
+	Common::String out;
+	for (uint i = 0; i < s.size(); i++) {
+		out += Common::String::format("%s%s=%u", i ? " " : "", s[i].word.c_str(), s[i].id);
+		if (s[i].role)
+			out += Common::String::format("[%s]", roles[MIN<uint>(s[i].role, 8)]);
+		if (s[i].verb)
+			out += "[v]";
+	}
+	return out;
+}
+
+bool Console::Cmd_Saids(int argc, const char **argv) {
+	const int16 only = (argc > 1 && strcmp(argv[1], "*")) ? (int16)atoi(argv[1]) : -1;
+	Common::Array<SaidSpec> specs;
+	_vm->collectSaids(only, specs);
+	for (uint i = 0; i < specs.size(); i++) {
+		Common::String line = Common::String::format("%u %u %u", i, specs[i].logic, specs[i].offset);
+		for (uint k = 0; k < specs[i].words.size(); k++)
+			line += Common::String::format(" %u", specs[i].words[k]);
+		debugPrintf("%s\n", line.c_str());
+	}
+	return true;
+}
+
+bool Console::Cmd_Parse(int argc, const char **argv) {
+	if (argc < 2) {
+		debugPrintf("Usage: %s <sentence>\n", argv[0]);
+		return true;
+	}
+	Words::ParseResult r;
+	_vm->_words->analyze(joinArgs(1, argc, argv).c_str(), r);
+	if (r.unknownPos && !r.candidates.empty() && r.unknownPos <= r.candidates[0].size())
+		debugPrintf("unknown %u %s\n", r.unknownPos, r.candidates[0][r.unknownPos - 1].word.c_str());
+	else if (r.unknownPos)
+		debugPrintf("unknown %u\n", r.unknownPos);
+	for (uint i = 0; i < r.candidates.size(); i++)
+		debugPrintf("#%u: %s\n", i, describe(r.candidates[i]).c_str());
+	return true;
+}
+
+bool Console::Cmd_Said(int argc, const char **argv) {
+	int sep = argc;
+	for (int i = 1; i < argc; i++) {
+		if (!strcmp(argv[i], "--")) {
+			sep = i;
+			break;
+		}
+	}
+	if (sep < 2) {
+		debugPrintf("Usage: %s <sentence> [-- g1 g2 ...]\n", argv[0]);
+		return true;
+	}
+	Words::ParseResult r;
+	_vm->_words->analyze(joinArgs(1, sep, argv).c_str(), r);
+	if (r.unknownPos) {
+		debugPrintf("unknown %u\n", r.unknownPos);
+		return true;
+	}
+	if (sep < argc) {
+		Common::Array<uint16> spec;
+		for (int i = sep + 1; i < argc; i++)
+			spec.push_back((uint16)atoi(argv[i]));
+		for (uint k = 0; k < r.candidates.size(); k++) {
+			if (saidMatches(r.candidates[k], spec.data(), spec.size())) {
+				debugPrintf("match rank %u\n", k);
+				return true;
+			}
+		}
+		debugPrintf("no match\n");
+		return true;
+	}
+	Common::Array<SaidSpec> specs;
+	_vm->collectSaids(-1, specs);
+	uint matches = 0;
+	for (uint i = 0; i < specs.size(); i++) {
+		for (uint k = 0; k < r.candidates.size(); k++) {
+			if (saidMatches(r.candidates[k], specs[i].words.data(), specs[i].words.size())) {
+				debugPrintf("spec %u logic %u offset %u rank %u\n", i, specs[i].logic, specs[i].offset, k);
+				matches++;
+				break;
+			}
+		}
+	}
+	debugPrintf("matches %u\n", matches);
+	return true;
+}
+
 } // End of namespace Agi
```

### B:console.h

```diff
diff --git a/engines/agi/console.h b/engines/agi/console.h
index 58233f55..ab19a3c4 100644
--- a/engines/agi/console.h
+++ b/engines/agi/console.h
@@ -68,4 +68,8 @@ private:
 	bool Cmd_DisableAutomaticSave(int argc, const char **argv);
 	bool Cmd_DiskDump(int argc, const char **argv);
+	bool Cmd_KOverlay(int argc, const char **argv);
+	bool Cmd_Saids(int argc, const char **argv);
+	bool Cmd_Parse(int argc, const char **argv);
+	bool Cmd_Said(int argc, const char **argv);
 
 	bool parseInteger(const char *argument, int &result);
```

### B:cycle.cpp

```diff
diff --git a/engines/agi/cycle.cpp b/engines/agi/cycle.cpp
index 81421b47..b040c4b6 100644
--- a/engines/agi/cycle.cpp
+++ b/engines/agi/cycle.cpp
@@ -212,9 +212,14 @@ void AgiEngine::interpretCycle() {
 
 // We return the current key, or 0 if no key was pressed
-uint16 AgiEngine::processAGIEvents() {
+int AgiEngine::processAGIEvents() {
 	ScreenObjEntry *screenObjEgo = &_game.screenObjTable[SCREENOBJECTS_EGO_ENTRY];
 
 	wait(10);
-	uint16 key = doPollKeyboard();
+	// AGI_KEY_CODEPOINT (UTF-8 text mode) goes to the prompt and get.string
+	// only; the mouse, the controllers, the menus and the message boxes see
+	// no key. Han/Yeong never gets here: doPollKeyboard() handles it.
+	const int rawKey = doPollKeyboard();
+	const bool wideKey = rawKey >= AGI_KEY_CODEPOINT;
+	uint16 key = wideKey ? 0 : (uint16)rawKey;
 
 	if (!cycleInnerLoopIsActive()) {
@@ -248,5 +253,8 @@ uint16 AgiEngine::processAGIEvents() {
 		// no inner loop active at the moment, regular processing
 
-		if (key) {
+		if (wideKey) {
+			if (_text->promptIsEnabled())
+				_text->promptKeyPress(rawKey);
+		} else if (key) {
 			if (!handleController(key)) {
 				// Only set VAR_KEY, when no controller/direction was detected
@@ -268,5 +276,7 @@ uint16 AgiEngine::processAGIEvents() {
 		case CYCLE_INNERLOOP_GETSTRING: // loop called from TextMgr::stringEdit()
 		case CYCLE_INNERLOOP_GETNUMBER:
-			if (key) {
+			if (wideKey) {
+				_text->stringKeyPress(rawKey);
+			} else if (key) {
 				_text->stringKeyPress(key);
 			}
@@ -320,5 +330,5 @@ uint16 AgiEngine::processAGIEvents() {
 	_gfx->updateScreen();
 
-	return key;
+	return wideKey ? rawKey : key;
 }
 
```

### B:detection.cpp

```diff
diff --git a/engines/agi/detection.cpp b/engines/agi/detection.cpp
index 6e194571..0804858b 100644
--- a/engines/agi/detection.cpp
+++ b/engines/agi/detection.cpp
@@ -23,5 +23,10 @@
 #include "common/system.h"
 #include "common/debug.h"
+#include "common/fs.h"
+#include "common/language.h"
+#include "common/language_folder.h"
 #include "common/md5.h"
+#include "common/ptr.h"
+#include "common/tokenizer.h"
 
 #include "base/plugins.h"
@@ -118,4 +123,8 @@ public:
 	ADDetectedGames detectGame(const Common::FSNode &parent, const FileMap &allFiles, Common::Language language, Common::Platform platform, const Common::String &extra, uint32 skipADFlags, bool skipIncomplete) override;
 
+	DetectedGames detectGames(const Common::FSList &fslist, uint32 skipADFlags, bool skipIncomplete) override;
+
+	Common::Error identifyGame(DetectedGame &game, const void **descriptor) override;
+
 private:
 	static void getPotentialDiskImages(const FileMap &allFiles, const char * const *imageExtensions, size_t extensionCount, Common::Array<Common::Path> &imageFiles);
@@ -303,4 +312,80 @@ ADDetectedGame AgiMetaEngineDetection::fallbackDetect(const FileMap &allFilesXXX
 }
 
+// The languages of the translations in a game folder: a language folder
+// named by its ISO 639-1 code holding agi-<code>.str (the UTF-8 translation
+// table laid over the original resources, agi/translation.cpp) or a hi-res
+// text map, ko\agi-ko.str; or agi-<code>.str in the game folder itself (the
+// packs before the language folders), as SCI's sci-<code>.str. AgiEngine
+// reads that folder first with language=<code>.
+static Common::Array<Common::Language> overlayLanguages(const Common::FSList &fslist) {
+	return Common::translationFolderLanguages(fslist, "agi-%s.str");
+}
+
+// The launcher's Language list offers only the languages a target's GUI
+// options name: add each of the folder's translations to a game's, as SCI
+// does.
+static void allowLanguages(DetectedGame &game, const Common::Array<Common::Language> &languages) {
+	for (uint i = 0; i < languages.size(); ++i) {
+		const Common::String option = getGameGUIOptionsDescriptionLanguage(languages[i]);
+		if (!game.getGUIOptions().contains(option))
+			game.appendGUIOptions(option);
+	}
+}
+
+Common::Error AgiMetaEngineDetection::identifyGame(DetectedGame &game, const void **descriptor) {
+	const Common::Error err = AdvancedMetaEngineDetection::identifyGame(game, descriptor);
+	// Started, the target's GUI options are rewritten from the entry it
+	// matched (AdvancedMetaEngineBase::createInstance()): the folder's
+	// translations go on again, or the Language list would lose them.
+	Common::FSList files;
+	if (err.getCode() == Common::kNoError && ConfMan.hasKey("path") &&
+		Common::FSNode(ConfMan.getPath("path")).getChildren(files, Common::FSNode::kListAll))
+		allowLanguages(game, overlayLanguages(files));
+	return err;
+}
+
+DetectedGames AgiMetaEngineDetection::detectGames(const Common::FSList &fslist, uint32 skipADFlags, bool skipIncomplete) {
+	DetectedGames games = AdvancedMetaEngineDetection::detectGames(fslist, skipADFlags, skipIncomplete);
+
+	// A folder with the original files and an agi-<lang>.str overlay matches
+	// the original's entry only, so the launcher would add it in the shipped
+	// language and the overlay would never load (the engine reads it for the
+	// target's language). Offer the translation too, and first, as SCI does
+	// for sci-<lang>.str; launching it goes through detectGame()'s overlay
+	// rule below.
+	const Common::Array<Common::Language> overlays = overlayLanguages(fslist);
+	const uint matched = games.size();
+	DetectedGames variants;
+	for (uint m = 0; m < overlays.size(); ++m) {
+		for (uint i = 0; i < matched; ++i) {
+			const DetectedGame &game = games[i];
+			if (game.language == overlays[m] || game.hasUnknownFiles)
+				continue;
+			const char *title = game.gameId.c_str();
+			for (const PlainGameDescriptor *pgd = agiGames; pgd->gameId; ++pgd)
+				if (game.gameId.equals(pgd->gameId))
+					title = pgd->description;
+			// A copy keeps the original's support level, matched files and
+			// add-ability; only the language and what follows from it change.
+			DetectedGame variant = game;
+			variant.language = overlays[m];
+			variant.description = DetectedGame(getName(), game.gameId, title, overlays[m], game.platform, game.extra).description;
+			variant.preferredTarget = game.preferredTarget + "-" + Common::getLanguageFolderCode(overlays[m]);
+			// The options are the original's, its language among them: the
+			// variant played in the original's language is the original.
+			variants.push_back(variant);
+		}
+	}
+	// Collected apart and put in front once: inserting while walking the
+	// originals would shift them under the loop.
+	for (uint i = 0; i < variants.size(); ++i)
+		games.insert_at(i, variants[i]);
+	// Every one of them, the originals too, may be played in each of the
+	// folder's translations (allowLanguages()).
+	for (DetectedGame &game : games)
+		allowLanguages(game, overlays);
+	return games;
+}
+
 /**
  * Detection override for handling disk images after file-based detection.
@@ -315,10 +400,29 @@ ADDetectedGames AgiMetaEngineDetection::detectGame(
 	bool skipIncomplete) {
 
+	// A target set to another language than the table's (language=ko on an
+	// English game) is a translation overlay, not a different release: when
+	// agi-<lang>.str sits in the game folder or in its language folder
+	// (ko\agi-ko.str), detect without the language filter. The engine reads
+	// the overlay itself (loadTranslationOverlay).
+	Common::Language detectLanguage = language;
+	if (language != Common::UNK_LANG && language != Common::EN_ANY) {
+		const char *code = Common::getLanguageFolderCode(language);
+		const Common::String overlayName = Common::String::format("agi-%s.str", code);
+		bool overlay = allFiles.contains(Common::Path(overlayName, Common::Path::kNoSeparator));
+		if (!overlay && code) {
+			Common::FSDirectory dir(parent);
+			Common::ScopedPtr<Common::FSDirectory> langDir(dir.getSubDirectory(code));
+			overlay = langDir && langDir->hasFile(Common::Path(overlayName));
+		}
+		if (overlay)
+			detectLanguage = Common::UNK_LANG;
+	}
+
 	// Run the file-based detection first, if it finds a match then do not search for disk images.
-	ADDetectedGames matched = AdvancedMetaEngineDetection::detectGame(parent, allFiles, language, platform, extra, skipADFlags, skipIncomplete);
+	ADDetectedGames matched = AdvancedMetaEngineDetection::detectGame(parent, allFiles, detectLanguage, platform, extra, skipADFlags, skipIncomplete);
 
 	// Detect games within PC disk images. This detection will find one game at most.
 	if (matched.empty() &&
-		(language == Common::UNK_LANG || language == Common::EN_ANY) &&
+		(detectLanguage == Common::UNK_LANG || detectLanguage == Common::EN_ANY) &&
 		(platform == Common::kPlatformUnknown || platform == Common::kPlatformDOS)) {
 		ADDetectedGame game = detectPcDiskImageGame(allFiles, skipADFlags);
@@ -330,5 +434,5 @@ ADDetectedGames AgiMetaEngineDetection::detectGame(
 	// Detect games within Apple II disk images. This detection will find one game at most.
 	if (matched.empty() &&
-		(language == Common::UNK_LANG || language == Common::EN_ANY) &&
+		(detectLanguage == Common::UNK_LANG || detectLanguage == Common::EN_ANY) &&
 		(platform == Common::kPlatformUnknown || platform == Common::kPlatformApple2)) {
 		ADDetectedGame game = detectA2DiskImageGame(allFiles, skipADFlags);
```

### B:graphics.cpp

```diff
diff --git a/engines/agi/graphics.cpp b/engines/agi/graphics.cpp
index 1bb1d202..ffbd6a2d 100644
--- a/engines/agi/graphics.cpp
+++ b/engines/agi/graphics.cpp
@@ -30,8 +30,10 @@
 #include "agi/agi.h"
 #include "agi/graphics.h"
+#include "agi/hirestext.h"
 #include "agi/mouse_cursor.h"
 #include "agi/palette.h"
 #include "agi/picture.h"
 #include "agi/text.h"
+#include "agi/textcells.h"
 
 namespace Agi {
@@ -152,5 +154,5 @@ void GfxMgr::initVideo() {
 	}
 
-	if (_font->isFontHires() || forceHires) {
+	if (_font->isFontHires() || forceHires || _vm->hiresTextActive()) {
 		// Upscaling enable
 		_upscaledHires = DISPLAY_UPSCALED_640x400;
@@ -1158,5 +1160,5 @@ void GfxMgr::drawDisplayRectCGA(int16 x, int16 y, int16 width, int16 height, byt
  * Draw a character to the display screen using text row and column coordinates
  */
-void GfxMgr::drawCharacter(int16 row, int16 column, byte character, byte foreground, byte background, bool disabledLook) {
+void GfxMgr::drawCharacter(int16 row, int16 column, uint32 character, byte foreground, byte background, bool disabledLook) {
 	int16 x = column;
 	int16 y = row;
@@ -1186,7 +1188,9 @@ void GfxMgr::drawCharacter(int16 row, int16 column, byte character, byte foregro
  */
 void GfxMgr::drawStringOnDisplay(int16 x, int16 y, const char *text, byte foregroundColor, byte backgroundColor) {
+	const bool utf8 = _vm->textIsUtf8();
 	while (*text) {
-		drawCharacterOnDisplay(x, y, *text, foregroundColor, backgroundColor);
-		text++;
+		uint32 character;
+		text += TextCells::decode(text, utf8, character);
+		drawCharacterOnDisplay(x, y, character, foregroundColor, backgroundColor);
 		x += _displayFontWidth;
 	}
@@ -1217,5 +1221,26 @@ void GfxMgr::drawStringOnDisplay(int16 x, int16 adjX, int16 y, int16 adjY, const
  * Draw a character to the display screen using text row and column coordinates
  */
-void GfxMgr::drawCharacterOnDisplay(int16 x, int16 y, const byte character, byte foreground, byte background, byte transformXOR, byte transformOR) {
+void GfxMgr::drawCharacterOnDisplay(int16 x, int16 y, uint32 character, byte foreground, byte background, byte transformXOR, byte transformOR) {
+	// A face draws everything the map does not leave to AGI's own font
+	// (D5: basic Latin stays AGI's, scaled 2x2 as today).
+	if (_vm->_hiresText && _upscaledHires == DISPLAY_UPSCALED_640x400) {
+		uint16 rows[16];
+		if (_vm->_hiresText->glyph(character, rows)) {
+			drawCoverageOnDisplay(x, y, rows, foreground, background, transformXOR, transformOR);
+			return;
+		}
+	}
+
+	// AGI's font has 256 glyphs. In UTF-8 text mode a code point above
+	// ASCII that no face drew (no map, or the map leaves it to AGI's font)
+	// has no glyph there: '?' rather than whatever CP437 has at its low
+	// byte.
+	if (_vm->textIsUtf8()) {
+		if (character >= 0x80)
+			character = '?';
+	} else {
+		character = (byte)character; // the old byte path
+	}
+
 	int16       curX, curY;
 	const byte *fontData;
@@ -1255,4 +1280,24 @@ void GfxMgr::drawCharacterOnDisplay(int16 x, int16 y, const byte character, byte
 }
 
+/**
+ * A 16x16 face glyph into a 640x400 text cell: foreground where the stencil
+ * is set, background elsewhere, with the same invert (XOR) and greyed-menu
+ * (OR 0x55/0xAA, per 8x8 font row = 2 display rows) looks AGI's font gets.
+ */
+void GfxMgr::drawCoverageOnDisplay(int16 x, int16 y, const uint16 *rows, byte foreground, byte background, byte transformXOR, byte transformOR) {
+	for (int16 cy = 0; cy < 16; cy++) {
+		const byte orPattern = transformOR ? (((cy >> 1) & 1) ? (byte)(transformOR ^ 0xFF) : transformOR) : 0;
+		for (int16 cx = 0; cx < 16; cx++) {
+			bool on = (rows[cy] & (0x8000 >> cx)) != 0;
+			if (transformXOR)
+				on = !on;
+			if (orPattern & (0x80 >> ((cx >> 1) & 7)))
+				on = true;
+			putFontPixelOnDisplay(x, y, cx, cy, on ? foreground : background, true);
+		}
+	}
+	copyDisplayRectToScreen(x, y, _displayFontWidth, _displayFontHeight);
+}
+
 #define SHAKE_VERTICAL_PIXELS 4
 #define SHAKE_HORIZONTAL_PIXELS 4
```

### B:graphics.h

```diff
diff --git a/engines/agi/graphics.h b/engines/agi/graphics.h
index 320019fa..5250778b 100644
--- a/engines/agi/graphics.h
+++ b/engines/agi/graphics.h
@@ -196,8 +196,9 @@ private:
 
 public:
-	void drawCharacter(int16 row, int16 column, byte character, byte foreground, byte background, bool disabledLook);
+	void drawCharacter(int16 row, int16 column, uint32 character, byte foreground, byte background, bool disabledLook);
 	void drawStringOnDisplay(int16 x, int16 y, const char *text, byte foreground, byte background);
 	void drawStringOnDisplay(int16 x, int16 adjX, int16 y, int16 adjY, const char *text, byte foregroundColor, byte backgroundColor);
-	void drawCharacterOnDisplay(int16 x, int16 y, byte character, byte foreground, byte background, byte transformXOR = 0, byte transformOR = 0);
+	void drawCharacterOnDisplay(int16 x, int16 y, uint32 character, byte foreground, byte background, byte transformXOR = 0, byte transformOR = 0);
+	void drawCoverageOnDisplay(int16 x, int16 y, const uint16 *rows, byte foreground, byte background, byte transformXOR, byte transformOR);
 
 	void shakeScreen(int16 repeatCount);
```

### B:inv.cpp

```diff
diff --git a/engines/agi/inv.cpp b/engines/agi/inv.cpp
index c1cbc997..4b53156b 100644
--- a/engines/agi/inv.cpp
+++ b/engines/agi/inv.cpp
@@ -26,4 +26,5 @@
 #include "agi/keyboard.h"
 #include "agi/systemui.h"
+#include "agi/textcells.h"
 
 namespace Agi {
@@ -67,5 +68,5 @@ void InventoryMgr::getPlayerInventory() {
 				if (inventoryEntry.column > 1) {
 					// right side, adjust column accordingly
-					inventoryEntry.column -= Common::strnlen(inventoryEntry.name, FONT_COLUMN_CHARACTERS);
+					inventoryEntry.column -= MIN<int16>(TextCells::cellLen(inventoryEntry.name, _vm->textIsUtf8()), FONT_COLUMN_CHARACTERS);
 				}
 			} else {
@@ -73,5 +74,5 @@ void InventoryMgr::getPlayerInventory() {
 				if (inventoryEntry.column == 1) {
 					// right side, adjust column accordingly
-					inventoryEntry.column = FONT_COLUMN_CHARACTERS - 1 - Common::strnlen(inventoryEntry.name, FONT_COLUMN_CHARACTERS);
+					inventoryEntry.column = FONT_COLUMN_CHARACTERS - 1 - MIN<int16>(TextCells::cellLen(inventoryEntry.name, _vm->textIsUtf8()), FONT_COLUMN_CHARACTERS);
 				} else {
 					// left side, adjust column accordingly
@@ -100,5 +101,5 @@ void InventoryMgr::getPlayerInventory() {
 		inventoryEntry.name = _systemUI->getInventoryTextNothing();
 		inventoryEntry.row = 2;
-		inventoryEntry.column = 19 - (strlen(inventoryEntry.name) / 2);
+		inventoryEntry.column = 19 - (TextCells::cellLen(inventoryEntry.name, _vm->textIsUtf8()) / 2);
 		_array.push_back(inventoryEntry);
 	}
```

### B:keyboard.cpp

```diff
diff --git a/engines/agi/keyboard.cpp b/engines/agi/keyboard.cpp
index 3c19773e..fc4a94e4 100644
--- a/engines/agi/keyboard.cpp
+++ b/engines/agi/keyboard.cpp
@@ -130,6 +130,22 @@ void AgiEngine::processScummVMEvents() {
 			}
 			break;
+		case Common::EVENT_CUSTOM_ENGINE_ACTION_START:
+			// Han/Yeong as a keymapper action: it enters the queue as a
+			// pseudo-key above every AGI key, so no script ever sees it.
+			if (event.customType == kAgiActionHangulToggle) {
+				if (_text != nullptr && _text->hangulInputAvailable())
+					keyEnqueue(AGI_KEY_HANGUL_TOGGLE);
+			}
+			break;
 		case Common::EVENT_KEYDOWN:
 			key = event.kbd.ascii;
+			// UTF-8 text mode: text with no key - an OS input method's finished
+			// syllable, Sci::EventManager::isOsImeCharacter()'s test - is one
+			// character of the edit line. Without an overlay it is dropped below,
+			// as it always was (key > 0xFF).
+			if (textIsUtf8() && event.kbd.keycode == Common::KEYCODE_INVALID && event.kbd.ascii >= 0x100) {
+				keyEnqueue(AGI_KEY_CODEPOINT | event.kbd.ascii);
+				break;
+			}
 			if (event.kbd.keycode >= Common::KEYCODE_KP0 && event.kbd.keycode <= Common::KEYCODE_KP9) {
 				if (!(event.kbd.flags & Common::KBD_NUM)) {
@@ -391,4 +407,14 @@ int AgiEngine::doPollKeyboard() {
 
 		debugC(3, kDebugLevelInput, "key %02x pressed", key);
+
+		// Han/Yeong (UTF-8 text mode) is handled here, once for every loop
+		// and every wait - the prompt, get.string, message boxes, menus,
+		// the inventory, have.key, waitKey/waitAnyKey - and is never a key
+		// press. D8 (the save name ignores it) is hangulToggle()'s.
+		if (key == AGI_KEY_HANGUL_TOGGLE) {
+			if (_text != nullptr)
+				_text->hangulToggle();
+			key = 0;
+		}
 	}
 
```

### B:keyboard.h

```diff
diff --git a/engines/agi/keyboard.h b/engines/agi/keyboard.h
index ae76a8b7..5bb9a3b4 100644
--- a/engines/agi/keyboard.h
+++ b/engines/agi/keyboard.h
@@ -83,4 +83,8 @@ public:
 #define AGI_KEY_HOME        0x4700  // Home
 #define AGI_KEY_END         0x4f00  // End *
+// UTF-8 text mode only; above every AGI key code (and outside the uint16 the
+// controller table holds), so neither reaches handleController() or a script.
+#define AGI_KEY_CODEPOINT     0x10000  // | code point: an OS input method's character
+#define AGI_KEY_HANGUL_TOGGLE 0x20000  // the Han/Yeong keymap action
 
 #define AGI_MOUSE_BUTTON_LEFT  0xF101  // Left mouse button
```

### B:loader.h

```diff
diff --git a/engines/agi/loader.h b/engines/agi/loader.h
index 969df381..95e5afb9 100644
--- a/engines/agi/loader.h
+++ b/engines/agi/loader.h
@@ -36,4 +36,11 @@ public:
 	virtual void init() {}
 
+	/**
+	 * The file of the logic directory (LOGDIR, or the v3 game's <name>DIR):
+	 * what a translation's message keys depend on. Empty where the game has
+	 * no such file (disk images, v1).
+	 */
+	virtual Common::Path logicDirectoryName() const { return Common::Path(); }
+
 	/**
 	 * Loads all AGI directory entries from disk and and populates
@@ -153,4 +160,5 @@ public:
 	AgiLoader_v2(AgiEngine *vm) : _hasV3VolumeFormat(false), AgiLoader(vm) {}
 
+	Common::Path logicDirectoryName() const override { return Common::Path(LOGDIR); }
 	int loadDirs() override;
 	uint8 *loadVolumeResource(AgiDir *agid) override;
@@ -169,4 +177,5 @@ public:
 
 	void init() override;
+	Common::Path logicDirectoryName() const override { return Common::Path(_name + DIR_); }
 	int loadDirs() override;
 	uint8 *loadVolumeResource(AgiDir *agid) override;
```

### B:logic.cpp

```diff
diff --git a/engines/agi/logic.cpp b/engines/agi/logic.cpp
index 04b6b018..20c990b0 100644
--- a/engines/agi/logic.cpp
+++ b/engines/agi/logic.cpp
@@ -21,4 +21,5 @@
 
 #include "agi/agi.h"
+#include "agi/textcells.h"
 
 namespace Agi {
@@ -85,4 +86,7 @@ int AgiEngine::decodeLogic(int16 logicNr) {
 	}
 
+	if (textIsUtf8())
+		translateLogicTexts(logicNr);
+
 	// set loaded flag
 	dirLogic.flags |= RES_LOADED;
@@ -90,4 +94,47 @@ int AgiEngine::decodeLogic(int16 logicNr) {
 }
 
+/**
+ * UTF-8 text mode: point every message at its translation, or at a UTF-8
+ * copy of the game's own text. The game's bytes stay reachable through
+ * rawTexts (compare.strings accepts an answer's English original, design
+ * 5.3). Overlay strings live as long as the engine; converted ones go in
+ * one block per logic, freed with it.
+ */
+void AgiEngine::translateLogicTexts(int16 logicNr) {
+	AgiLogic &logic = _game.logics[logicNr];
+	logic.rawTexts = (const char **)calloc(1 + logic.numTexts, sizeof(char *));
+	if (!logic.rawTexts)
+		return;
+	Common::Array<char> pool;
+	Common::Array<int> poolOffset;
+	poolOffset.resize(logic.numTexts);
+	for (int i = 0; i < logic.numTexts; i++) {
+		logic.rawTexts[i] = logic.texts[i];
+		poolOffset[i] = -1;
+		const Common::String *t = _overlay.message(logicNr, i + 1);
+		if (t) {
+			logic.texts[i] = t->c_str();
+			continue;
+		}
+		if (!TextCells::hasHighBytes(logic.texts[i]))
+			continue;
+		const Common::String u = TextOverlay::legacyToUtf8(logic.texts[i], Common::kDos850);
+		poolOffset[i] = pool.size();
+		for (uint k = 0; k < u.size(); k++)
+			pool.push_back(u[k]);
+		pool.push_back('\0');
+	}
+	if (pool.empty())
+		return;
+	logic.utf8Pool = (char *)malloc(pool.size());
+	if (!logic.utf8Pool)
+		return;
+	memcpy(logic.utf8Pool, pool.data(), pool.size());
+	for (int i = 0; i < logic.numTexts; i++) {
+		if (poolOffset[i] >= 0)
+			logic.texts[i] = logic.utf8Pool + poolOffset[i];
+	}
+}
+
 /**
  * Unload logic resource
@@ -105,4 +152,8 @@ void AgiEngine::unloadLogic(int16 logicNr) {
 		free(logic.texts);
 		logic.texts = nullptr;
+		free(logic.rawTexts);
+		logic.rawTexts = nullptr;
+		free(logic.utf8Pool);
+		logic.utf8Pool = nullptr;
 		logic.numTexts = 0;
 		dirLogic.flags &= ~RES_LOADED;
```

### B:logic.h

```diff
diff --git a/engines/agi/logic.h b/engines/agi/logic.h
index 11ed02a7..99c9f119 100644
--- a/engines/agi/logic.h
+++ b/engines/agi/logic.h
@@ -35,4 +35,6 @@ struct AgiLogic {
 	int numTexts;       /**< number of messages */
 	const char **texts; /**< message list */
+	const char **rawTexts; /**< UTF-8 text mode: the game's own bytes of each message (texts[] is translated) */
+	char *utf8Pool;        /**< UTF-8 text mode: untranslated messages converted from the code page */
 
 	void reset() {
@@ -43,4 +45,6 @@ struct AgiLogic {
 		numTexts = 0;
 		texts = nullptr;
+		rawTexts = nullptr;
+		utf8Pool = nullptr;
 	}
 
```

### B:menu.cpp

```diff
diff --git a/engines/agi/menu.cpp b/engines/agi/menu.cpp
index 922c2b86..9df3bd26 100644
--- a/engines/agi/menu.cpp
+++ b/engines/agi/menu.cpp
@@ -27,4 +27,5 @@
 #include "agi/keyboard.h"
 #include "agi/menu.h"
+#include "agi/textcells.h"
 
 namespace Agi {
@@ -83,5 +84,6 @@ void GfxMenu::addMenu(const char *menuText) {
 			menuEntry->text = "Special ";
 
-	menuEntry->textLen = menuEntry->text.size();
+	const bool utf8 = _vm->textIsUtf8();
+	menuEntry->textLen = TextCells::cellLen(menuEntry->text.c_str(), utf8);
 
 	if (!_vm->isLanguageRTL()) {
@@ -90,9 +92,11 @@ void GfxMenu::addMenu(const char *menuText) {
 		// Original interpreter had graphical issues in this case
 		// TODO: this whole code needs to get reworked anyway to support different types of menu bars depending on platform
+		// Cut by cells, never inside a character (a UTF-8 translation).
 		curColumnEnd += menuEntry->textLen;
-		while ((menuEntry->textLen) && (curColumnEnd > 40)) {
-			menuEntry->text.deleteLastChar();
-			menuEntry->textLen--;
-			curColumnEnd--;
+		if (menuEntry->textLen && curColumnEnd > 40) {
+			const int16 keep = MAX<int16>(0, menuEntry->textLen - (curColumnEnd - 40));
+			menuEntry->text = TextCells::clipCells(menuEntry->text.c_str(), keep, utf8);
+			curColumnEnd -= menuEntry->textLen - keep;
+			menuEntry->textLen = keep;
 		}
 	}
@@ -131,5 +135,5 @@ void GfxMenu::addMenuItem(const char *menuItemText, uint16 controllerSlot) {
 	menuItemEntry->enabled = true;
 	menuItemEntry->text = menuItemText;
-	menuItemEntry->textLen = menuItemEntry->text.size();
+	menuItemEntry->textLen = TextCells::cellLen(menuItemEntry->text.c_str(), _vm->textIsUtf8());
 	menuItemEntry->controllerSlot = controllerSlot;
 
@@ -141,12 +145,15 @@ void GfxMenu::addMenuItem(const char *menuItemText, uint16 controllerSlot) {
 	}
 
-	if (curMenuEntry->itemCount == 0) {
+	if (_vm->textIsUtf8() && !_vm->isLanguageRTL()) {
+		// UTF-8 text mode: place the drop-down by its longest item so far
+		// and move the items already added with it, so no item runs past
+		// column 39.
+		_setupMenuItemColumn = TextCells::dropDownColumn(curMenuEntry->column, curMenuEntry->maxItemTextLen, FONT_COLUMN_CHARACTERS);
+		for (uint nr = curMenuEntry->firstItemNr; nr < _itemArray.size(); nr++)
+			_itemArray[nr]->column = _setupMenuItemColumn;
+	} else if (curMenuEntry->itemCount == 0) {
 		if (!_vm->isLanguageRTL()) {
 			// for first menu item of menu calculated column
-			if (menuItemEntry->textLen + curMenuEntry->column < (FONT_COLUMN_CHARACTERS - 1)) {
-				_setupMenuItemColumn = curMenuEntry->column;
-			} else {
-				_setupMenuItemColumn = (FONT_COLUMN_CHARACTERS - 1) - menuItemEntry->textLen;
-			}
+			_setupMenuItemColumn = TextCells::dropDownColumn(curMenuEntry->column, menuItemEntry->textLen, FONT_COLUMN_CHARACTERS);
 		} else {
 			_setupMenuItemColumn = curMenuEntry->column + curMenuEntry->textLen - menuItemEntry->textLen;
```

### B:metaengine.cpp

```diff
diff --git a/engines/agi/metaengine.cpp b/engines/agi/metaengine.cpp
index deecb1c1..f608447d 100644
--- a/engines/agi/metaengine.cpp
+++ b/engines/agi/metaengine.cpp
@@ -30,4 +30,9 @@
 #include "graphics/surface.h"
 
+#include "backends/keymapper/action.h"
+#include "backends/keymapper/keymapper.h"
+
+#include "common/config-manager.h"
+
 #include "base/plugins.h"
 #include "engines/advancedDetector.h"
@@ -56,4 +61,9 @@ Common::Platform AgiBase::getPlatform() const {
 
 Common::Language AgiBase::getLanguage() const {
+	// A translation overlay names the language the player reads, whatever
+	// the game was detected as (SQ0 is an English game; with agi-ko.str it
+	// is played in Korean). As SciEngine::getLanguage().
+	if (_textMode.utf8())
+		return _textMode.language;
 	if (_gameDescription->desc.language != Common::UNK_LANG)
 		return _gameDescription->desc.language;
@@ -251,6 +261,41 @@ public:
 
 	bool hasFeature(MetaEngineFeature f) const override;
+
+	Common::KeymapArray initKeymaps(const char *target) const override;
 };
 
+Common::KeymapArray AgiMetaEngine::initKeymaps(const char *target) const {
+	using namespace Common;
+
+	KeymapArray keymaps = MetaEngine::initKeymaps(target);
+
+	// Korean targets only (DESIGN.md section 6), where the composer can be
+	// used: as SCI's keymap (engines/sci/metaengine.cpp).
+	if (ConfMan.get("language", target) != "ko")
+		return keymaps;
+	bool value;
+	if ((ConfMan.hasKey("hangul_builtin", target) && Common::parseBool(ConfMan.get("hangul_builtin", target), value) && !value) ||
+	    (ConfMan.hasKey("korean_text", target) && Common::parseBool(ConfMan.get("korean_text", target), value) && !value) ||
+	    (ConfMan.hasKey("hangul_input", target) && Common::parseBool(ConfMan.get("hangul_input", target), value) && !value))
+		return keymaps;
+
+	Keymap *hangulKeymap = new Keymap(Keymap::kKeymapTypeGame, "agi-hangul", _("AGI - Korean input"));
+	Action *act = new Action("HANGULTOGGLE", _("Han/Yeong (Korean input on/off)"));
+	act->setCustomEngineActionEvent(Agi::kAgiActionHangulToggle);
+	// SCI's defaults (engines/sci/metaengine.cpp): the dedicated key, what
+	// X11 calls it, the two IME toggles, and two keys Windows' Korean IME
+	// lets through to DOSBox-X.
+	act->addDefaultInputMapping("HANGUL");
+	act->addDefaultInputMapping("RALT");
+	act->addDefaultInputMapping("C+SPACE");
+	act->addDefaultInputMapping("S+SPACE");
+	act->addDefaultInputMapping("SCROLLOCK");
+	act->addDefaultInputMapping("F12");
+	hangulKeymap->addAction(act);
+
+	keymaps.push_back(hangulKeymap);
+	return keymaps;
+}
+
 bool AgiMetaEngine::hasFeature(MetaEngineFeature f) const {
 	return
```

### B:module.mk

```diff
diff --git a/engines/agi/module.mk b/engines/agi/module.mk
index 34c16d86..4189c8b6 100644
--- a/engines/agi/module.mk
+++ b/engines/agi/module.mk
@@ -10,6 +10,9 @@ MODULE_OBJS := \
 	global.o \
 	graphics.o \
+	hangulline.o \
+	hirestext.o \
 	inv.o \
 	keyboard.o \
+	korparse.o \
 	loader.o \
 	loader_a2.o \
@@ -31,5 +34,7 @@ MODULE_OBJS := \
 	picture.o \
 	picture_gal.o \
+	saidscan.o \
 	saveload.o \
+	saved_strings.o \
 	sound.o \
 	sound_2gs.o \
@@ -41,5 +46,8 @@ MODULE_OBJS := \
 	sprite.o \
 	systemui.o \
+	textmode.o \
 	text.o \
+	textcells.o \
+	translation.o \
 	view.o \
 	words.o \
```

### B:objects.cpp

```diff
diff --git a/engines/agi/objects.cpp b/engines/agi/objects.cpp
index 3ceea062..91f70c81 100644
--- a/engines/agi/objects.cpp
+++ b/engines/agi/objects.cpp
@@ -21,4 +21,5 @@
 
 #include "agi/agi.h"
+#include "agi/textcells.h"
 
 #include "common/textconsole.h"
@@ -73,4 +74,16 @@ int AgiEngine::decodeObjects(uint8 *mem, uint32 flen) {
 		if (_objects[i].name == "?" && _objects[i].location == EGO_OWNED)
 			_objects[i].location = 0;
+
+		// UTF-8 text mode: the translated name, or the game's own in UTF-8.
+		// "?" (the unused object) is never translated.
+		if (textIsUtf8() && _objects[i].name != "?") {
+			const Common::String *t = _overlay.object(i);
+			if (t) {
+				_objects[i].name = *t;
+			} else {
+				if (TextCells::hasHighBytes(_objects[i].name.c_str()))
+					_objects[i].name = TextOverlay::legacyToUtf8(_objects[i].name.c_str(), Common::kDos850);
+			}
+		}
 	}
 	debug(0, "Reading objects: %d objects read.", _game.numObjects);
```

### B:op_cmd.cpp

```diff
diff --git a/engines/agi/op_cmd.cpp b/engines/agi/op_cmd.cpp
index 6d27452b..16724408 100644
--- a/engines/agi/op_cmd.cpp
+++ b/engines/agi/op_cmd.cpp
@@ -23,4 +23,5 @@
 
 #include "agi/agi.h"
+#include "agi/textcells.h"
 #include "agi/inv.h"
 #include "agi/sprite.h"
@@ -828,10 +829,10 @@ void cmdLog(AgiGame *state, AgiEngine *vm, uint8 *parameter) {
 	if (state->_curLogic->texts != nullptr && (textNr - 1) <= state->_curLogic->numTexts) {
 		byte currentRoom = vm->getVar(VM_VAR_CURRENT_ROOM);
-		const char *inputLine = (char *)vm->_text->_promptPrevious;
+		const Common::String inputLineText = vm->_text->promptPreviousGetText();
 		const char *message = state->_curLogic->texts[textNr - 1];
 
 		Common::String logMessage = Common::String::format("Room %hhu\nInput line: %s\n%s\n",
 														   currentRoom,
-														   inputLine,
+														   inputLineText.c_str(),
 														   vm->_text->stringPrintf(message));
 
@@ -971,6 +972,7 @@ void cmdSetSimple(AgiGame *state, AgiEngine *vm, uint8 *parameter) {
 	// Try to get description for automatic saves
 	const char *textPtr = state->getString(stringNr);
-	strncpy(state->automaticSaveDescription, textPtr, sizeof(state->automaticSaveDescription));
-	state->automaticSaveDescription[sizeof(state->automaticSaveDescription) - 1] = '\0';
+	// UTF-8 text mode: whole characters only
+	const Common::String description = TextCells::clipBytes(textPtr, sizeof(state->automaticSaveDescription) - 1, vm->textIsUtf8());
+	Common::strlcpy(state->automaticSaveDescription, description.c_str(), sizeof(state->automaticSaveDescription));
 
 	// enable automatic saving if description isn't empty
@@ -1757,6 +1759,9 @@ void cmdSetGameID(AgiGame *state, AgiEngine *vm, uint8 *parameter) {
 	uint16 textNr = parameter[0];
 
-	if (state->_curLogic->texts && (textNr - 1) <= state->_curLogic->numTexts)
-		Common::strlcpy(state->id, state->_curLogic->texts[textNr - 1], 8);
+	if (state->_curLogic->texts && (textNr - 1) <= state->_curLogic->numTexts) {
+		// the game's own text: a translation never changes the id saves carry
+		const char *const *texts = state->_curLogic->rawTexts ? state->_curLogic->rawTexts : state->_curLogic->texts;
+		Common::strlcpy(state->id, texts[textNr - 1], 8);
+	}
 	else
 		state->id[0] = 0;
@@ -2048,5 +2053,5 @@ void cmdGetString(AgiGame *state, AgiEngine *vm, uint8 *parameter) {
 	// copy string to destination
 	// TODO: not sure if set all the time or only when ENTER is pressed
-	vm->_game.setString(stringDestNr, (char *)textMgr->_inputString);
+	vm->_game.setString(stringDestNr, textMgr->stringGetText().c_str());
 
 	textMgr->charPos_Pop();
@@ -2091,5 +2096,5 @@ void cmdGetNum(AgiGame *state, AgiEngine *vm, uint8 *parameter) {
 	textMgr->promptRedraw();
 
-	number = atoi((char *)textMgr->_inputString);
+	number = atoi(textMgr->stringGetText().c_str());
 	vm->setVar(numberDestVarNr, number);
 
@@ -2138,5 +2143,5 @@ void cmdSetString(AgiGame *state, AgiEngine *vm, uint8 *parameter) {
 	uint16 textNr = parameter[1] - 1;
 
-	state->setString(stringNr, state->_curLogic->texts[textNr]);
+	state->setStringFromMessage(stringNr, textNr);
 }
 
```

### B:op_test.cpp

```diff
diff --git a/engines/agi/op_test.cpp b/engines/agi/op_test.cpp
index bc843b58..0cf2a107 100644
--- a/engines/agi/op_test.cpp
+++ b/engines/agi/op_test.cpp
@@ -23,5 +23,7 @@
 #include "agi/agi.h"
 #include "agi/graphics.h"
+#include "agi/keyboard.h"
 #include "agi/opcodes.h"
+#include "agi/textcells.h"
 #include "agi/words.h"
 
@@ -130,9 +132,10 @@ void condHaveKey(AgiGame *state, AgiEngine *vm, uint8 *p) {
 	// we are not really an inner loop, but we stop processAGIEvents() from doing regular cycle work by setting it up
 	vm->cycleInnerLoopActive(CYCLE_INNERLOOP_HAVEKEY);
-	uint16 key = vm->processAGIEvents();
+	int key = vm->processAGIEvents();
 	vm->cycleInnerLoopInactive();
 	if (key) {
 		debugC(5, kDebugLevelInput, "keypress = %02x", key);
-		vm->setVar(VM_VAR_KEY, key);
+		// a Korean character is "a key" but has no AGI key code
+		vm->setVar(VM_VAR_KEY, key >= AGI_KEY_CODEPOINT ? 0 : key);
 		state->testResult = true;
 #ifdef USE_TTS
@@ -232,54 +235,18 @@ void condUnknown(AgiGame *state, AgiEngine *vm, uint8 *p) {
 
 bool AgiEngine::testCompareStrings(uint8 s1, uint8 s2) {
-	char ms1[MAX_STRINGLEN];
-	char ms2[MAX_STRINGLEN];
-	int j, k, l;
-
-	Common::strlcpy(ms1, _game.getString(s1), MAX_STRINGLEN);
-	Common::strlcpy(ms2, _game.getString(s2), MAX_STRINGLEN);
-
-	l = strlen(ms1);
-	for (k = 0, j = 0; k < l; k++) {
-		switch (ms1[k]) {
-		case 0x20:
-		case 0x09:
-		case '-':
-		case '.':
-		case ',':
-		case ':':
-		case ';':
-		case '!':
-		case '\'':
-			break;
-
-		default:
-			ms1[j++] = tolower(ms1[k]);
-			break;
-		}
-	}
-	ms1[j] = 0x0;
-
-	l = strlen(ms2);
-	for (k = 0, j = 0; k < l; k++) {
-		switch (ms2[k]) {
-		case 0x20:
-		case 0x09:
-		case '-':
-		case '.':
-		case ',':
-		case ':':
-		case ';':
-		case '!':
-		case '\'':
-			break;
-
-		default:
-			ms2[j++] = tolower(ms2[k]);
-			break;
-		}
-	}
-	ms2[j] = 0x0;
-
-	return !strcmp(ms1, ms2);
+	const char *a = _game.getString(s1);
+	const char *b = _game.getString(s2);
+	if (TextCells::agiStringsEqual(a, b))
+		return true;
+	if (!textIsUtf8())
+		return false;
+	// D2: a translated answer also matches its English original.
+	if (s1 <= MAX_STRINGS && !_game.stringAnswer[s1].empty() &&
+	    TextCells::agiStringsEqual(_game.stringAnswer[s1].c_str(), b))
+		return true;
+	if (s2 <= MAX_STRINGS && !_game.stringAnswer[s2].empty() &&
+	    TextCells::agiStringsEqual(a, _game.stringAnswer[s2].c_str()))
+		return true;
+	return false;
 }
 
@@ -328,58 +295,36 @@ bool AgiEngine::testObjRight(uint8 n, uint8 x1, uint8 y1, uint8 x2, uint8 y2) {
 // When player has entered something, it is parsed elsewhere
 bool AgiEngine::testSaid(uint8 nwords, uint8 *cc) {
-	AgiGame *state = &_game;
-	AgiEngine *vm = state->_vm;
-	Words *words = vm->_words;
-	int n = words->getEgoWordCount();
-	int z = 0;
-
-	if (vm->getFlag(VM_FLAG_SAID_ACCEPTED_INPUT) || !vm->getFlag(VM_FLAG_ENTERED_CLI))
+	if (getFlag(VM_FLAG_SAID_ACCEPTED_INPUT) || !getFlag(VM_FLAG_ENTERED_CLI))
 		return false;
-
-	// FR:
-	// I think the reason for the code below is to add some speed....
-	//
-	//      if (nwords != num_ego_words)
-	//              return false;
-	//
-	// In the disco scene in Larry 1 when you type "examine blonde",
-	// inside the logic is expected ( said("examine", "blonde", "rol") )
-	// where word("rol") = 9999
-	//
-	// According to the interpreter code 9999 means that whatever the
-	// user typed should be correct, but it looks like code 9999 means that
-	// if the string is empty at this point, the entry is also correct...
-	//
-	// With the removal of this code, the behavior of the scene was
-	// corrected
-
-	for (int c = 0; nwords && n; c++, nwords--, n--) {
-		z = READ_LE_UINT16(cc);
-		cc += 2;
-
-		switch (z) {
-		case 9999:  // rest of line (empty string counts to...)
-			nwords = 1;
-			break;
-		case 1: // any word
-			break;
-		default:
-			if (words->getEgoWordId(c) != z)
-				return false;
-			break;
+	uint16 spec[256];
+	for (int i = 0; i < nwords; i++)
+		spec[i] = READ_LE_UINT16(cc + 2 * i);
+	return testSaidSpec(spec, nwords);
+}
+
+// The words must match the spec positionally (saidMatches(), the
+// interpreter's loop). A Korean line has up to four orderings: each said()
+// tries them best first, and the first that matches becomes the line, so
+// every later said(), %w and word.to.string see one sentence (design 5.5).
+bool AgiEngine::testSaidSpec(const uint16 *spec, int nwords) {
+	if (_words->hasCandidates()) {
+		for (uint i = 0; i < _words->candidateCount(); i++) {
+			if (saidMatches(_words->candidate(i), spec, nwords)) {
+				_words->commitCandidate(i);
+				setFlag(VM_FLAG_SAID_ACCEPTED_INPUT, true);
+				return true;
+			}
 		}
-	}
-
-	// The entry string should be entirely parsed, or last word = 9999
-	if (n && z != 9999)
 		return false;
-
-	// The interpreter string shouldn't be entirely parsed, but next
-	// word must be 9999.
-	if (nwords != 0 && READ_LE_UINT16(cc) != 9999)
+	}
+	AgiSentence ego;
+	for (int c = 0; c < _words->getEgoWordCount(); c++) {
+		AgiWord w;
+		w.id = _words->getEgoWordId(c);
+		ego.push_back(w);
+	}
+	if (!saidMatches(ego, spec, nwords))
 		return false;
-
 	setFlag(VM_FLAG_SAID_ACCEPTED_INPUT, true);
-
 	return true;
 }
```

### B:preagi_preagi.cpp

```diff
diff --git a/engines/agi/preagi/preagi.cpp b/engines/agi/preagi/preagi.cpp
index 98b88058..51d87a68 100644
--- a/engines/agi/preagi/preagi.cpp
+++ b/engines/agi/preagi/preagi.cpp
@@ -164,5 +164,5 @@ void PreAgiEngine::drawStr(int row, int col, int attr, const char *buffer) {
 
 		default:
-			_gfx->drawCharacter(row, col, code, foreground, background, false);
+			_gfx->drawCharacter(row, col, (byte)code, foreground, background, false);
 
 			if (++col == 320 / 8) {
```

### B:saveload.cpp

```diff
diff --git a/engines/agi/saveload.cpp b/engines/agi/saveload.cpp
index c6e0e39e..b83b7472 100644
--- a/engines/agi/saveload.cpp
+++ b/engines/agi/saveload.cpp
@@ -43,4 +43,6 @@
 #include "agi/systemui.h"
 #include "agi/words.h"
+#include "agi/saved_strings.h"
+#include "agi/textcells.h"
 
 #define SAVEGAME_CURRENT_VERSION 11
@@ -86,5 +88,11 @@ int AgiEngine::saveGame(const Common::String &fileName, const Common::String &de
 	const char *descriptionStringC;
 	Common::U32String hebDesc;
-	if (_game._vm->getLanguage() != Common::HE_ISR) {
+	Common::String utf8Desc;
+	if (textIsUtf8()) {
+		// A description from ScummVM's save dialog may be any Unicode: cut
+		// it on a character boundary to what strlcpy below keeps.
+		utf8Desc = TextCells::clipBytes(descriptionString.c_str(), SAVEDGAME_DESCRIPTION_LEN - 1, true);
+		descriptionStringC = utf8Desc.c_str();
+	} else if (_game._vm->getLanguage() != Common::HE_ISR) {
 		descriptionStringC = descriptionString.c_str();
 	} else {
@@ -216,6 +224,14 @@ int AgiEngine::saveGame(const Common::String &fileName, const Common::String &de
 
 	// game.ev_keyp
-	for (i = 0; i < MAX_STRINGS; i++)
-		out->write(_game.getString(i), MAX_STRINGLEN);
+	for (i = 0; i < MAX_STRINGS; i++) {
+		if (textIsUtf8()) {
+			// whole characters only; the full strings follow in the trailer
+			char slot[kLegacySlotBytes];
+			legacySlot(_game.getString(i), slot);
+			out->write(slot, MAX_STRINGLEN);
+		} else {
+			out->write(_game.getString(i), MAX_STRINGLEN);
+		}
+	}
 
 	// record info about loaded resources
@@ -330,4 +346,8 @@ int AgiEngine::saveGame(const Common::String &fileName, const Common::String &de
 	out->writeSint16BE(_gfx->getAGIPalFileNum());
 
+	// UTF-8 text mode: the whole strings after everything upstream reads
+	if (textIsUtf8())
+		writeSavedStrings(*out, &_game.strings[0][0], MAX_STRINGBYTES, MAX_STRINGS, _game.stringAnswer);
+
 	out->finalize();
 	if (out->err()) {
@@ -568,6 +588,12 @@ int AgiEngine::loadGame(const Common::String &fileName, bool checkId) {
 	}
 
-	for (i = 0; i < MAX_STRINGS; i++)
+	// No slot may keep a stale English alternative across a restore.
+	for (i = 0; i <= MAX_STRINGS; i++)
+		_game.stringAnswer[i].clear();
+	for (i = 0; i < MAX_STRINGS; i++) {
+		memset(_game.strings[i], 0, MAX_STRINGBYTES);
 		in->read(_game.strings[i], MAX_STRINGLEN);
+		_game.strings[i][MAX_STRINGLEN - 1] = 0;
+	}
 
 	for (i = 0; i < MAX_DIRECTORY_ENTRIES; i++) {
@@ -731,4 +757,14 @@ int AgiEngine::loadGame(const Common::String &fileName, bool checkId) {
 		_gfx->setAGIPal(in->readSint16BE());
 
+	// UTF-8 text mode: the whole strings of the trailer, else the version 11
+	// table in the game's code page made UTF-8
+	if (textIsUtf8()) {
+		const SavedStringsResult r = readSavedStrings(*in, &_game.strings[0][0], MAX_STRINGBYTES, MAX_STRINGS, _game.stringAnswer);
+		if (r == kSavedStringsBroken)
+			warning("Saved game %s has a broken string trailer: its strings are read from the version 11 table", fileName.c_str());
+		if (r != kSavedStringsRead)
+			convertLegacyStrings(&_game.strings[0][0], MAX_STRINGBYTES, MAX_STRINGS, MAX_STRINGLEN - 1);
+	}
+
 	delete in;
 	debugC(3, kDebugLevelSavegame, "Closed %s", fileName.c_str());
@@ -774,5 +810,5 @@ int AgiEngine::scummVMSaveLoadDialog(bool isSave) {
 
 		if (desc.size() > 28)
-			desc = Common::String(desc.c_str(), 28);
+			desc = TextCells::clipBytes(desc.c_str(), 28, textIsUtf8());
 	} else {
 		dialog = new GUI::SaveLoadChooser(false);
```

### B:sprite.cpp

```diff
diff --git a/engines/agi/sprite.cpp b/engines/agi/sprite.cpp
index 69770ead..76e1ff9f 100644
--- a/engines/agi/sprite.cpp
+++ b/engines/agi/sprite.cpp
@@ -24,4 +24,5 @@
 #include "agi/graphics.h"
 #include "agi/text.h"
+#include "agi/textcells.h"
 
 namespace Agi {
@@ -416,5 +417,18 @@ void SpritesMgr::showObject(int16 viewNr) {
 	showSprite(&screenObj);
 
-	_vm->_text->messageBox((char *)_vm->_game.views[viewNr].description);
+	// UTF-8 text mode: the translated description (v<view>), or the game's
+	// own in UTF-8.
+	const char *description = (const char *)_vm->_game.views[viewNr].description;
+	Common::String utf8Description;
+	if (_vm->textIsUtf8() && description) {
+		const Common::String *t = _vm->_overlay.view(viewNr);
+		if (t)
+			utf8Description = *t;
+		else
+			utf8Description = TextCells::hasHighBytes(description) ? TextOverlay::legacyToUtf8(description, Common::kDos850)
+			                                                       : Common::String(description);
+		description = utf8Description.c_str();
+	}
+	_vm->_text->messageBox(description);
 
 	_gfx->block_restore(screenObj.xPos, (screenObj.yPos - screenObj.ySize + 1), screenObj.xSize, screenObj.ySize, backgroundBuffer);
```

### B:systemui.cpp

```diff
diff --git a/engines/agi/systemui.cpp b/engines/agi/systemui.cpp
index 40cd61af..cd5e725d 100644
--- a/engines/agi/systemui.cpp
+++ b/engines/agi/systemui.cpp
@@ -25,4 +25,5 @@
 #include "agi/keyboard.h"
 #include "agi/systemui.h"
+#include "agi/textcells.h"
 
 namespace Agi {
@@ -142,4 +143,25 @@ SystemUI::SystemUI(AgiEngine *vm, GfxMgr *gfx, TextMgr *text) {
 		_textRestoreGameVerify = "Merci de confirmer.\n\n%s\n\ndans :\n%s\n\n\"Entr\x82\x65\" pour accepter.\n\"Esc\" pour annuler.";
 		break;
+	case Common::KO_KOR:
+		// UTF-8: a Korean overlay switched the engine into UTF-8 text mode.
+		_textStatusScore = "\xEC\xA0\x90\xEC\x88\x98:%v3 / %v7"; // 점수:%v3 / %v7
+		_textStatusSoundOn = "\xEC\x86\x8C\xEB\xA6\xAC:\xEC\xBC\x9C\xEC\xA7\x90"; // 소리:켜짐
+		_textStatusSoundOff = "\xEC\x86\x8C\xEB\xA6\xAC:\xEA\xBA\xBC\xEC\xA7\x90"; // 소리:꺼짐
+		_textEnterCommand = "\xEB\xAA\x85\xEB\xA0\xB9\xEC\x9D\x84 \xEC\x9E\x85\xEB\xA0\xA5\xED\x95\x98\xEC\x84\xB8\xEC\x9A\x94\n\n"; // 명령을 입력하세요
+		_textPause = "\xEA\xB2\x8C\xEC\x9E\x84\xEC\x9D\x84 \xEC\x9E\xA0\xEC\x8B\x9C \xEB\xA9\x88\xEC\xB7\x84\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4.\nEnter \xED\x82\xA4\xEB\xA5\xBC \xEB\x88\x84\xEB\xA5\xB4\xEB\xA9\xB4 \xEA\xB3\x84\xEC\x86\x8D\xED\x95\xA9\xEB\x8B\x88\xEB\x8B\xA4."; // 게임을 잠시 멈췄습니다.\nEnter 키를 누르면 계속합니다.
+		_textRestart = "Enter: \xEA\xB2\x8C\xEC\x9E\x84\xEC\x9D\x84 \xEC\xB2\x98\xEC\x9D\x8C\xEB\xB6\x80\xED\x84\xB0 \xEB\x8B\xA4\xEC\x8B\x9C \xEC\x8B\x9C\xEC\x9E\x91\n\nEsc: \xEC\x9D\xB4 \xEA\xB2\x8C\xEC\x9E\x84\xEC\x9D\x84 \xEA\xB3\x84\xEC\x86\x8D"; // Enter: 게임을 처음부터 다시 시작\n\nEsc: 이 게임을 계속
+		_textQuit = "Enter: \xEA\xB2\x8C\xEC\x9E\x84 \xEB\x81\x9D\xEB\x82\xB4\xEA\xB8\xB0\nEsc: \xEA\xB3\x84\xEC\x86\x8D\xED\x95\x98\xEA\xB8\xB0"; // Enter: 게임 끝내기\nEsc: 계속하기
+		_textInventoryYouAreCarrying = "\xEA\xB0\x80\xEC\xA7\x84 \xEB\xAC\xBC\xEA\xB1\xB4:"; // 가진 물건:
+		_textInventoryNothing = "\xEC\x97\x86\xEC\x9D\x8C"; // 없음
+		_textInventorySelectItems = "Enter: \xEA\xB3\xA0\xEB\xA5\xB4\xEA\xB8\xB0, Esc: \xEC\xB7\xA8\xEC\x86\x8C"; // Enter: 고르기, Esc: 취소
+		_textInventoryReturnToGame = "\xEC\x95\x84\xEB\xAC\xB4 \xED\x82\xA4\xEB\x82\x98 \xEB\x88\x84\xEB\xA5\xB4\xEB\xA9\xB4 \xEA\xB2\x8C\xEC\x9E\x84\xEC\x9C\xBC\xEB\xA1\x9C \xEB\x8F\x8C\xEC\x95\x84\xEA\xB0\x91\xEB\x8B\x88\xEB\x8B\xA4"; // 아무 키나 누르면 게임으로 돌아갑니다
+		_textSaveGameSelectSlot = "\xED\x99\x94\xEC\x82\xB4\xED\x91\x9C \xED\x82\xA4\xEB\xA1\x9C \xEA\xB2\x8C\xEC\x9E\x84\xEC\x9D\x84 \xEC\xA0\x80\xEC\x9E\xA5\xED\x95\xA0 \xEC\xB9\xB8\xEC\x9D\x84 \xEA\xB3\xA0\xEB\xA5\xB4\xEC\x84\xB8\xEC\x9A\x94. Enter: \xEA\xB7\xB8 \xEC\xB9\xB8\xEC\x97\x90 \xEC\xA0\x80\xEC\x9E\xA5, Esc: \xEC\xA0\x80\xEC\x9E\xA5\xED\x95\x98\xEC\xA7\x80 \xEC\x95\x8A\xEC\x9D\x8C."; // 화살표 키로 게임을 저장할 칸을 고르세요. Enter: 그 칸에 저장, Esc: 저장하지 않음.
+		_textSaveGameEnterDescription = "\xEC\xA0\x80\xEC\x9E\xA5\xED\x95\xA0 \xEA\xB2\x8C\xEC\x9E\x84\xEC\x97\x90 \xEB\xB6\x99\xEC\x9D\xBC \xEC\x84\xA4\xEB\xAA\x85\xEC\x9D\x84 \xEC\x9E\x85\xEB\xA0\xA5\xED\x95\x98\xEC\x84\xB8\xEC\x9A\x94.\n\n"; // 저장할 게임에 붙일 설명을 입력하세요.
+		_textSaveGameVerify = "\xEB\x8B\xA4\xEC\x9D\x8C \xEC\x84\xA4\xEB\xAA\x85\xEC\x9C\xBC\xEB\xA1\x9C \xEA\xB2\x8C\xEC\x9E\x84\xEC\x9D\x84 \xEC\xA0\x80\xEC\x9E\xA5\xED\x95\xA9\xEB\x8B\x88\xEB\x8B\xA4:\n\n%s\n\n\xED\x8C\x8C\xEC\x9D\xBC:\n%s\n\nEnter: \xEA\xB3\x84\xEC\x86\x8D\nEsc: \xEC\xB7\xA8\xEC\x86\x8C"; // 다음 설명으로 게임을 저장합니다:\n\n%s\n\n파일:\n%s\n\nEnter: 계속\nEsc: 취소
+		_textRestoreGameNoSlots = "\xEB\xB6\x88\xEB\x9F\xAC\xEC\x98\xAC \xEA\xB2\x8C\xEC\x9E\x84\xEC\x9D\xB4 \xEC\x97\x86\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4.\n\n ScummVM \xEC\xA0\x80\xEC\x9E\xA5 \xED\x8F\xB4\xEB\x8D\x94\n\nEnter \xED\x82\xA4\xEB\xA5\xBC \xEB\x88\x84\xEB\xA5\xB4\xEC\x84\xB8\xEC\x9A\x94."; // 불러올 게임이 없습니다.\n\n ScummVM 저장 폴더\n\nEnter 키를 누르세요.
+		_textRestoreGameSelectSlot = "\xED\x99\x94\xEC\x82\xB4\xED\x91\x9C \xED\x82\xA4\xEB\xA1\x9C \xEB\xB6\x88\xEB\x9F\xAC\xEC\x98\xAC \xEA\xB2\x8C\xEC\x9E\x84\xEC\x9D\x84 \xEA\xB3\xA0\xEB\xA5\xB4\xEC\x84\xB8\xEC\x9A\x94. Enter: \xEB\xB6\x88\xEB\x9F\xAC\xEC\x98\xA4\xEA\xB8\xB0, Esc: \xEB\xB6\x88\xEB\x9F\xAC\xEC\x98\xA4\xEC\xA7\x80 \xEC\x95\x8A\xEC\x9D\x8C."; // 화살표 키로 불러올 게임을 고르세요. Enter: 불러오기, Esc: 불러오지 않음.
+		_textRestoreGameError = "\xEA\xB2\x8C\xEC\x9E\x84\xEC\x9D\x84 \xEB\xB6\x88\xEB\x9F\xAC\xEC\x98\xA4\xEC\xA7\x80 \xEB\xAA\xBB\xED\x96\x88\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4.\nEnter \xED\x82\xA4\xEB\xA5\xBC \xEB\x88\x84\xEB\xA5\xB4\xEB\xA9\xB4 \xEB\x81\x9D\xEB\x83\x85\xEB\x8B\x88\xEB\x8B\xA4."; // 게임을 불러오지 못했습니다.\nEnter 키를 누르면 끝냅니다.
+		_textRestoreGameVerify = "\xEB\x8B\xA4\xEC\x9D\x8C \xEC\x84\xA4\xEB\xAA\x85\xEC\x9D\x98 \xEA\xB2\x8C\xEC\x9E\x84\xEC\x9D\x84 \xEB\xB6\x88\xEB\x9F\xAC\xEC\x98\xB5\xEB\x8B\x88\xEB\x8B\xA4:\n\n%s\n\n\xED\x8C\x8C\xEC\x9D\xBC:\n%s\n\nEnter: \xEA\xB3\x84\xEC\x86\x8D\nEsc: \xEC\xB7\xA8\xEC\x86\x8C"; // 다음 설명의 게임을 불러옵니다:\n\n%s\n\n파일:\n%s\n\nEnter: 계속\nEsc: 취소
+		break;
 	default:
 		break;
@@ -296,5 +318,5 @@ bool SystemUI::askForCommand(Common::String &commandText) {
 
 	commandText.clear();
-	commandText += (char *)_text->_inputString;
+	commandText += _text->stringGetText();
 	return true;
 }
@@ -385,5 +407,9 @@ bool SystemUI::askForSaveGameDescription(int16 slotId, Common::String &newDescri
 
 	_vm->cycleInnerLoopActive(CYCLE_INNERLOOP_GETSTRING);
-	_text->stringEdit(30); // only allow up to 30 characters
+	// The description field is 30 bytes (SAVEDGAME_DESCRIPTION_LEN): a
+	// UTF-8 name is held to that, 10 syllables, not to 30 cells.
+	_text->_inputStringMaxBytes = _vm->textIsUtf8() ? SAVEDGAME_DESCRIPTION_LEN : 0;
+	_text->stringEdit(30);
+	_text->_inputStringMaxBytes = 0;
 
 	_text->charAttrib_Pop();
@@ -402,10 +428,10 @@ bool SystemUI::askForSaveGameDescription(int16 slotId, Common::String &newDescri
 
 	// Now verify that the user really wants to do this
-	if (!askForSavedGameVerification(_textSaveGameVerify, _textSaveGameVerifyButton1, _textSaveGameVerifyButton2, (char *)_text->_inputString, slotId)) {
+	if (!askForSavedGameVerification(_textSaveGameVerify, _textSaveGameVerifyButton1, _textSaveGameVerifyButton2, _text->stringGetText().c_str(), slotId)) {
 		return false;
 	}
 
 	newDescription.clear();
-	newDescription += (char *)_text->_inputString;
+	newDescription += _text->stringGetText();
 	return true;
 }
@@ -590,4 +616,11 @@ void SystemUI::createSavedGameDisplayText(char *destDisplayText, const char *act
 	int16 actualDescriptionLen = 0;
 
+	if (_vm->textIsUtf8()) {
+		// cells, not bytes: a Korean description is 3 bytes a syllable
+		Common::strlcpy(destDisplayText, TextCells::savedGameLine(actualDescription, slotId, fillUpChar, true).c_str(),
+		                SYSTEMUI_SAVEDGAME_DISPLAYTEXT_BYTES + 1);
+		return;
+	}
+
 	// clear with spaces
 	memset(destDisplayText, fillUpChar, SYSTEMUI_SAVEDGAME_DISPLAYTEXT_LEN);
@@ -692,5 +725,5 @@ void SystemUI::readSavedGameSlots(bool filterNonexistant, bool withAutoSaveSlot)
 		savedGameEntry.isValid = saveIsValid;
 		memset(savedGameEntry.description, 0, sizeof(savedGameEntry.description));
-		strncpy(savedGameEntry.description, saveDescription.c_str(), SYSTEMUI_SAVEDGAME_DESCRIPTION_LEN);
+		strncpy(savedGameEntry.description, TextCells::clipBytes(saveDescription.c_str(), SYSTEMUI_SAVEDGAME_DESCRIPTION_LEN, _vm->textIsUtf8()).c_str(), SYSTEMUI_SAVEDGAME_DESCRIPTION_LEN);
 		createSavedGameDisplayText(savedGameEntry.displayText, saveDescription.c_str(), curSlotId, true);
 
@@ -806,5 +839,5 @@ void SystemUI::drawSavedGameSlotSelector(bool active) {
 
 bool SystemUI::askForSavedGameVerification(const char *verifyText, const char *verifyButton1, const char *verifyButton2, const char *actualDescription, int16 slotId) {
-	char displayDescription[SYSTEMUI_SAVEDGAME_DISPLAYTEXT_LEN + 1];
+	char displayDescription[SYSTEMUI_SAVEDGAME_DISPLAYTEXT_BYTES + 1];
 	Common::String userActionVerify;
 	Common::String savedGameFilename = _vm->getSaveStateName(slotId);
@@ -867,5 +900,5 @@ bool SystemUI::askForVerification(const char *verifyText, const char *button1Tex
 		if (button1Text) {
 			buttonEntry.text = button1Text;
-			buttonEntry.textWidth = strlen(button1Text) * _gfx->getDisplayFontWidth();
+			buttonEntry.textWidth = TextCells::cellLen(button1Text, _vm->textIsUtf8()) * _gfx->getDisplayFontWidth();
 			buttonEntry.isDefault = true;
 			_buttonArray.push_back(buttonEntry);
@@ -873,5 +906,5 @@ bool SystemUI::askForVerification(const char *verifyText, const char *button1Tex
 		if (button2Text) {
 			buttonEntry.text = button2Text;
-			buttonEntry.textWidth = strlen(button2Text) * _gfx->getDisplayFontWidth();
+			buttonEntry.textWidth = TextCells::cellLen(button2Text, _vm->textIsUtf8()) * _gfx->getDisplayFontWidth();
 			buttonEntry.isDefault = false;
 			_buttonArray.push_back(buttonEntry);
```

### B:systemui.h

```diff
diff --git a/engines/agi/systemui.h b/engines/agi/systemui.h
index 62a137f1..c3d4f30d 100644
--- a/engines/agi/systemui.h
+++ b/engines/agi/systemui.h
@@ -30,4 +30,6 @@ namespace Agi {
 #define SYSTEMUI_SAVEDGAME_DISPLAYTEXT_LEN 31
 #define SYSTEMUI_SAVEDGAME_DISPLAYTEXT_PREFIX_LEN 3
+// UTF-8 text mode: 31 cells of up to 4 bytes
+#define SYSTEMUI_SAVEDGAME_DISPLAYTEXT_BYTES (SYSTEMUI_SAVEDGAME_DISPLAYTEXT_LEN * 4)
 
 struct SystemUISavedGameEntry {
@@ -36,5 +38,5 @@ struct SystemUISavedGameEntry {
 	bool  isValid;
 	char description[SYSTEMUI_SAVEDGAME_DESCRIPTION_LEN + 1]; // actual description
-	char displayText[SYSTEMUI_SAVEDGAME_DISPLAYTEXT_LEN + 1]; // modified description, meant for display purposes only
+	char displayText[SYSTEMUI_SAVEDGAME_DISPLAYTEXT_BYTES + 1]; // modified description, meant for display purposes only
 };
 typedef Common::Array<SystemUISavedGameEntry> SystemUISavedGameArray;
```

### B:text.cpp

```diff
diff --git a/engines/agi/text.cpp b/engines/agi/text.cpp
index cc02816b..d5b601a7 100644
--- a/engines/agi/text.cpp
+++ b/engines/agi/text.cpp
@@ -22,4 +22,5 @@
 #include "common/config-manager.h"
 #include "common/unicode-bidi.h"
+#include "common/hangul.h"
 #include "agi/agi.h"
 #include "agi/sprite.h"     // for commit_both()
@@ -27,4 +28,5 @@
 #include "agi/keyboard.h"
 #include "agi/text.h"
+#include "agi/textcells.h"
 #include "agi/systemui.h"
 #include "agi/words.h"
@@ -71,4 +73,8 @@ TextMgr::TextMgr(AgiEngine *vm, Words *words, GfxMgr *gfx) {
 	_inputStringCursorPos = 0;
 	_inputString[0] = 0;
+	_inputStringMaxBytes = 0;
+
+	_hangulInput = false;
+	_hangulMode = false;
 
 	configureScreen(2);
@@ -88,4 +94,77 @@ TextMgr::~TextMgr() {
 void TextMgr::init(SystemUI *systemUI) {
 	_systemUI = systemUI;
+
+}
+
+// A key of DESIGN.md section 7 that may say auto: @p autoValue then.
+static bool autoBoolKey(const char *key, bool autoValue) {
+	bool value;
+	if (!ConfMan.hasKey(key) || ConfMan.get(key).equalsIgnoreCase("auto") || !Common::parseBool(ConfMan.get(key), value))
+		return autoValue;
+	return value;
+}
+
+void TextMgr::hangulSetup(bool table) {
+	// Korean typing: on by default when the game has a Korean word table (as SCI's EventManager).
+	_hangulInput = _vm->textIsUtf8() && autoBoolKey("hangul_input", table) && autoBoolKey("hangul_builtin", true);
+	_hangulMode = _hangulInput && autoBoolKey("hangul_start", true);
+	_hangulLine.reset();
+}
+
+void TextMgr::redrawInputTail(const uint32 *cells, int16 oldLen, int16 from, int16 newLen) {
+	for (int16 i = from; i < oldLen; i++)
+		displayCharacter(0x08);
+	for (int16 i = from; i < newLen; i++) {
+		const bool preedit = (i == newLen - 1) && _hangulLine.preedit() != 0;
+		if (preedit) {
+			charAttrib_Push();
+			charAttrib_Set(_textAttrib.background, _textAttrib.foreground);
+		}
+		displayCharacter(cells[i]);
+		if (preedit)
+			charAttrib_Pop();
+	}
+}
+
+void TextMgr::hangulEndRun(const uint32 *cells, int16 len) {
+	const bool wasComposing = _hangulLine.composing();
+	_hangulLine.commit(len);
+	if (wasComposing && len > 0)
+		redrawInputTail(cells, len, len - 1, len);
+}
+
+bool TextMgr::hangulEditKey(int newKey, uint32 *cells, int16 &cursorPos, int16 maxCells) {
+	const bool jamo = newKey >= 0x20 && newKey < 0x7F && Common::HangulComposer::isJamoKey((char)newKey);
+	if (!jamo && !(newKey == AGI_KEY_BACKSPACE && _hangulLine.composing()))
+		return false;
+
+	const int16 oldLen = cursorPos;
+	const int16 from = _hangulLine.composing() ? _hangulLine.runStart() : oldLen;
+	int16 len = cursorPos;
+	if (jamo)
+		_hangulLine.feed((char)newKey, cells, len, maxCells);
+	else
+		_hangulLine.backspace(cells, len);
+	redrawInputTail(cells, oldLen, MIN<int16>(from, len), len);
+	cursorPos = len;
+	return true;
+}
+
+void TextMgr::hangulToggle() {
+	if (!_hangulInput)
+		return;
+	const bool inString = _vm->cycleInnerLoopIsActive() &&
+		(_vm->_game.cycleInnerLoopType == CYCLE_INNERLOOP_GETSTRING || _vm->_game.cycleInnerLoopType == CYCLE_INNERLOOP_GETNUMBER);
+	uint32 *cells = inString ? _inputString : _prompt;
+	const int16 len = inString ? _inputStringCursorPos : _promptCursorPos;
+	if (_hangulLine.composing() && (inString || _promptEnabled)) {
+		inputEditOn();
+		hangulEndRun(cells, len);
+		inputEditOff();
+	} else {
+		_hangulLine.commit(len);
+	}
+	_hangulMode = !_hangulMode;
+	debugC(1, kDebugLevelInput, "[key] Han/Yeong -> %s", _hangulMode ? "Korean" : "English");
 }
 
@@ -351,17 +430,16 @@ void TextMgr::displayText(const char *textPtr, bool disabledLook) {
 	}
 
+	// One character per cell: a code point in UTF-8 text mode, else a byte
+	// (the Russian, Hebrew and French fonts have glyphs at 0x80-0xFF).
+	const bool utf8 = _vm->textIsUtf8();
 	const char *curTextPtr = textPtr;
-
-	while (1) {
-		byte curCharacter = *curTextPtr;
-		if (!curCharacter)
-			break;
-
-		curTextPtr++;
-		displayCharacter(curCharacter, disabledLook);
+	while (*curTextPtr) {
+		uint32 character;
+		curTextPtr += TextCells::decode(curTextPtr, utf8, character);
+		displayCharacter(character, disabledLook);
 	}
 }
 
-void TextMgr::displayCharacter(byte character, bool disabledLook) {
+void TextMgr::displayCharacter(uint32 character, bool disabledLook) {
 	TextPos_Struct charCurPos;
 
@@ -664,5 +742,5 @@ void TextMgr::statusDraw(bool ttsVoiceScore, bool ttsVoiceSound) {
 			charPos_Set(_statusRow, 1);
 		else
-			charPos_Set(_statusRow, FONT_COLUMN_CHARACTERS - Common::strnlen(statusTextPtr, FONT_COLUMN_CHARACTERS) - 1);
+			charPos_Set(_statusRow, FONT_COLUMN_CHARACTERS - MIN<int16>(TextCells::cellLen(statusTextPtr, _vm->textIsUtf8()), FONT_COLUMN_CHARACTERS) - 1);
 		displayText(statusTextPtr);
 
@@ -777,4 +855,5 @@ int16 TextMgr::promptRow_Get() {
 
 void TextMgr::promptReset() {
+	_hangulLine.reset();
 	_promptCursorPos = 0;
 	memset(_prompt, 0, sizeof(_prompt));
@@ -792,32 +871,40 @@ bool TextMgr::promptIsEnabled() {
 }
 
-void TextMgr::promptKeyPress(uint16 newKey) {
+void TextMgr::promptKeyPress(int newKey) {
 	int16 maxChars = 0;
 	int16 scriptsInputLen = _vm->getVar(VM_VAR_MAX_INPUT_CHARACTERS);
+	const bool utf8 = _vm->textIsUtf8();
+	// An OS input method's character (keyboard.cpp) is one cell like a key.
+	const bool codePoint = newKey >= AGI_KEY_CODEPOINT && newKey < AGI_KEY_HANGUL_TOGGLE;
+	const uint32 cell = codePoint ? (uint32)(newKey & 0xFFFF) : (uint32)newKey;
 
 	bool acceptableInput = false;
 
-	// FEATURE: Sierra didn't check for valid characters (filtered out umlauts etc.)
-	// In text-mode this sort of worked at least with the DOS interpreter
-	// but as soon as invalid characters were used in graphics mode they weren't properly shown
-	switch (_vm->getLanguage()) {
-	case Common::RU_RUS:
-	case Common::HE_ISR:
-		if ((newKey >= 0x20) && (newKey <= 0xff))
-			acceptableInput = true;
-		break;
-	case Common::FR_FRA:
-		if ((newKey >= 0x20) && (newKey != 0x5e) && (newKey <= 0xff))
-			acceptableInput = true;
-		break;
-	default:
-		if ((newKey >= 0x20) && (newKey <= 0x7f))
-			acceptableInput = true;
-		break;
+	if (codePoint) {
+		acceptableInput = utf8 && cell >= 0x80;
+	} else {
+		// FEATURE: Sierra didn't check for valid characters (filtered out umlauts etc.)
+		// In text-mode this sort of worked at least with the DOS interpreter
+		// but as soon as invalid characters were used in graphics mode they weren't properly shown
+		switch (_vm->getLanguage()) {
+		case Common::RU_RUS:
+		case Common::HE_ISR:
+			if ((newKey >= 0x20) && (newKey <= 0xff))
+				acceptableInput = true;
+			break;
+		case Common::FR_FRA:
+			if ((newKey >= 0x20) && (newKey != 0x5e) && (newKey <= 0xff))
+				acceptableInput = true;
+			break;
+		default:
+			if ((newKey >= 0x20) && (newKey <= 0x7f))
+				acceptableInput = true;
+			break;
+		}
 	}
 
 	if (_optionCommandPromptWindow) {
 		// Forward to command prompt window, using last command
-		if (acceptableInput) {
+		if (acceptableInput && !codePoint) {
 			promptCommandWindow(false, newKey);
 		}
@@ -828,5 +915,5 @@ void TextMgr::promptKeyPress(uint16 newKey) {
 		maxChars = TEXT_STRING_MAX_SIZE - 4;
 	} else {
-		maxChars = TEXT_STRING_MAX_SIZE - strlen(_vm->_game.getString(0)); // string 0 is the prompt string prefix
+		maxChars = TEXT_STRING_MAX_SIZE - TextCells::cellLen(_vm->_game.getString(0), _vm->textIsUtf8()); // string 0 is the prompt string prefix
 	}
 
@@ -839,4 +926,14 @@ void TextMgr::promptKeyPress(uint16 newKey) {
 	inputEditOn();
 
+	// Built-in Hangul composer (Korean mode): a jamo key edits the syllable
+	// run at the end of the line; backspace edits it first; anything else
+	// ends it as it stands.
+	if (_hangulInput && _hangulMode && hangulEditKey(newKey, _prompt, _promptCursorPos, maxChars)) {
+		promptRememberForAutoComplete();
+		inputEditOff();
+		return;
+	}
+	hangulEndRun(_prompt, _promptCursorPos);
+
 	switch (newKey) {
 	case AGI_KEY_BACKSPACE: {
@@ -860,9 +957,10 @@ void TextMgr::promptKeyPress(uint16 newKey) {
 
 			memcpy(&_promptPrevious, &_prompt, sizeof(_prompt));
+			const Common::String line = promptGetText();
 #ifdef USE_TTS
-			_vm->sayText((char *)&_prompt);
+			_vm->sayText(line);
 #endif
 			// parse text
-			_vm->_words->parseUsingDictionary((char *)&_prompt);
+			_vm->_words->parseUsingDictionary(line.c_str());
 
 			_promptCursorPos = 0;
@@ -873,15 +971,13 @@ void TextMgr::promptKeyPress(uint16 newKey) {
 	}
 	default:
-		if (maxChars > _promptCursorPos) {
-			if (acceptableInput) {
-				_prompt[_promptCursorPos] = newKey;
-				_promptCursorPos++;
-				_prompt[_promptCursorPos] = 0;
-				displayCharacter(newKey);
-				if (_vm->isLanguageRTL())
-					promptRedraw();
-
-				promptRememberForAutoComplete();
-			}
+		if (maxChars > _promptCursorPos && acceptableInput) {
+			_prompt[_promptCursorPos] = cell;
+			_promptCursorPos++;
+			_prompt[_promptCursorPos] = 0;
+			displayCharacter(cell);
+			if (_vm->isLanguageRTL())
+				promptRedraw();
+
+			promptRememberForAutoComplete();
 		}
 		break;
@@ -891,4 +987,24 @@ void TextMgr::promptKeyPress(uint16 newKey) {
 }
 
+Common::String TextMgr::promptGetText() const {
+	return TextCells::fromCells(_prompt, _vm->textIsUtf8());
+}
+
+Common::String TextMgr::promptPreviousGetText() const {
+	return TextCells::fromCells(_promptPrevious, _vm->textIsUtf8());
+}
+
+void TextMgr::promptSetFromText(const Common::String &text) {
+	TextCells::toCells(text.c_str(), _prompt, ARRAYSIZE(_prompt), _vm->textIsUtf8());
+}
+
+bool TextMgr::stringFitsBudget(const uint32 *cells, int16 len) const {
+	return !_inputStringMaxBytes || TextCells::utf8Bytes(cells, len) <= _inputStringMaxBytes;
+}
+
+Common::String TextMgr::stringGetText() const {
+	return TextCells::fromCells(_inputString, _vm->textIsUtf8());
+}
+
 void TextMgr::promptCancelLine() {
 	if (_optionCommandPromptWindow) {
@@ -903,5 +1019,5 @@ void TextMgr::promptCancelLine() {
 
 void TextMgr::promptEchoLine() {
-	int16 previousLen = strlen((char *)_promptPrevious);
+	int16 previousLen = TextCells::cellCount(_promptPrevious);
 
 	if (_optionCommandPromptWindow) {
@@ -914,7 +1030,16 @@ void TextMgr::promptEchoLine() {
 		inputEditOn();
 
+		// F4: the echo replays the previous line as keys; in Korean mode its
+		// ASCII would be composed into Hangul. Replay in English.
+		const bool hangulMode = _hangulMode;
+		_hangulMode = false;
 		while (_promptPrevious[_promptCursorPos]) {
-			promptKeyPress(_promptPrevious[_promptCursorPos]);
+			const uint32 c = _promptPrevious[_promptCursorPos];
+			const int16 before = _promptCursorPos;
+			promptKeyPress((_vm->textIsUtf8() && c >= 0x80) ? (int)(AGI_KEY_CODEPOINT | c) : (int)c);
+			if (_promptCursorPos == before)
+				break; // refused (budget full): the old loop spun here
 		}
+		_hangulMode = hangulMode;
 		promptRememberForAutoComplete();
 
@@ -941,10 +1066,10 @@ void TextMgr::promptRedraw() {
 		if (!_vm->isLanguageRTL()) {
 			displayText(textPtr);
-			displayText((char *)&_prompt);
+			displayText(promptGetText().c_str());
 			inputEditOff();
 		} else {
-			charPos_Set(_promptRow, FONT_COLUMN_CHARACTERS - 2 - Common::strnlen((const char *)_prompt, FONT_COLUMN_CHARACTERS));
+			charPos_Set(_promptRow, FONT_COLUMN_CHARACTERS - 2 - MIN<int16>(TextCells::cellCount(_prompt), FONT_COLUMN_CHARACTERS));
 			inputEditOff();
-			displayText((char *)&_prompt);
+			displayText(promptGetText().c_str());
 			displayText(textPtr);
 			charPos_Set(_promptRow, FONT_COLUMN_CHARACTERS - 1);
@@ -969,5 +1094,5 @@ void TextMgr::promptCommandWindow(bool recallLastCommand, uint16 newKey) {
 
 	if (recallLastCommand) {
-		commandText += Common::String((char *)_promptPrevious);
+		commandText += promptPreviousGetText();
 	}
 	if (newKey) {
@@ -983,9 +1108,9 @@ void TextMgr::promptCommandWindow(bool recallLastCommand, uint16 newKey) {
 		if (commandText.size()) {
 			// Something actually was entered?
-			Common::strcpy_s((char *)_prompt, sizeof(_prompt), commandText.c_str());
+			promptSetFromText(commandText);
 			promptRememberForAutoComplete(true);
 			memcpy(&_promptPrevious, &_prompt, sizeof(_prompt));
 			// parse text
-			_vm->_words->parseUsingDictionary((char *)&_prompt);
+			_vm->_words->parseUsingDictionary(promptGetText().c_str());
 
 			_prompt[0] = 0;
@@ -999,6 +1124,5 @@ bool TextMgr::stringWasEntered() {
 
 void TextMgr::stringSet(const char *text) {
-	strncpy((char *)_inputString, text, sizeof(_inputString));
-	_inputString[sizeof(_inputString) - 1] = 0; // terminator
+	TextCells::toCells(text, _inputString, ARRAYSIZE(_inputString), _vm->textIsUtf8());
 }
 
@@ -1012,5 +1136,5 @@ int16 TextMgr::stringGetMaxLen() {
 
 void TextMgr::stringEdit(int16 stringMaxLen) {
-	int16 inputStringLen = strlen((const char *)_inputString);
+	int16 inputStringLen = TextCells::cellCount(_inputString);
 
 	// Remember current position for predictive dialog
@@ -1040,5 +1164,5 @@ void TextMgr::stringEdit(int16 stringMaxLen) {
 			charPos_Set(_textPos.row, stringMaxLen + 2 - _inputStringCursorPos);
 		inputEditOff();
-		displayText((const char *)_inputString);
+		displayText(stringGetText().c_str());
 	}
 
@@ -1049,4 +1173,8 @@ void TextMgr::stringEdit(int16 stringMaxLen) {
 	_inputStringEntered = false;
 
+	// The composer starts afresh on this line (a run on the prompt is left
+	// as it stands); the save name shows English (D8).
+	_hangulLine.commit(_inputStringCursorPos);
+
 	if (!_vm->isLanguageRTL())
 		inputEditOff();
@@ -1056,4 +1184,8 @@ void TextMgr::stringEdit(int16 stringMaxLen) {
 	} while (_vm->cycleInnerLoopIsActive() && !(_vm->shouldQuit() || _vm->_restartGame));
 
+	// The line is closed: its run with it. _inputStringAscii is still set
+	// here (SystemUI clears it), so the badge is put back by mode alone.
+	_hangulLine.reset();
+
 	inputEditOn();
 	// Fix deletion of first letter after ENTER.
@@ -1062,5 +1194,5 @@ void TextMgr::stringEdit(int16 stringMaxLen) {
 			displayCharacter(0x08);
 		displayCharacter(' ');
-		displayText((const char *)_inputString);
+		displayText(stringGetText().c_str());
 	}	
 
@@ -1069,7 +1201,33 @@ void TextMgr::stringEdit(int16 stringMaxLen) {
 }
 
-void TextMgr::stringKeyPress(uint16 newKey) {
+void TextMgr::stringKeyPress(int newKey) {
+	const bool utf8 = _vm->textIsUtf8();
+	const bool codePoint = newKey >= AGI_KEY_CODEPOINT && newKey < AGI_KEY_HANGUL_TOGGLE;
+	const uint32 cell = codePoint ? (uint32)(newKey & 0xFFFF) : (uint32)newKey;
+
 	inputEditOn();
 
+	// The composer works for get.string alone: get.num takes digits only.
+	const bool composerHere = _hangulInput && _hangulMode && _vm->_game.cycleInnerLoopType == CYCLE_INNERLOOP_GETSTRING;
+	if (composerHere && _inputStringMaxBytes && newKey >= 0x20 && newKey < 0x7F &&
+	    Common::HangulComposer::isJamoKey((char)newKey)) {
+		// The save name's field is bytes: a jamo that would make the line too long is refused.
+		uint32 trial[ARRAYSIZE(_inputString)];
+		memcpy(trial, _inputString, sizeof(trial));
+		int16 len = _inputStringCursorPos;
+		HangulLine line = _hangulLine;
+		line.feed((char)newKey, trial, len, _inputStringMaxLen);
+		if (!stringFitsBudget(trial, len)) {
+			inputEditOff();
+			return;
+		}
+	}
+	if (composerHere && hangulEditKey(newKey, _inputString, _inputStringCursorPos, _inputStringMaxLen)) {
+		stringRememberForAutoComplete();
+		inputEditOff();
+		return;
+	}
+	hangulEndRun(_inputString, _inputStringCursorPos);
+
 	switch (newKey) {
 	case 0x3:       // ctrl-c
@@ -1094,5 +1252,5 @@ void TextMgr::stringKeyPress(uint16 newKey) {
 				displayCharacter(' ');
 				inputEditOff();
-				displayText((const char *)_inputString);
+				displayText(stringGetText().c_str());
 			}
 
@@ -1107,5 +1265,5 @@ void TextMgr::stringKeyPress(uint16 newKey) {
 		_inputStringEntered = true;
 #ifdef USE_TTS
-		_vm->sayText((const char *)_inputString);
+		_vm->sayText(stringGetText());
 #endif
 
@@ -1127,37 +1285,46 @@ void TextMgr::stringKeyPress(uint16 newKey) {
 			bool acceptableInput = false;
 
-			// FEATURE: Sierra didn't check for valid characters (filtered out umlauts etc.)
-			// In text-mode this sort of worked at least with the DOS interpreter
-			// but as soon as invalid characters were used in graphics mode they weren't properly shown
-			switch (_vm->getLanguage()) {
-			case Common::RU_RUS:
-			case Common::HE_ISR:
-				if ((newKey >= 0x20) && (newKey <= 0xff))
-					acceptableInput = true;
-				break;
-			case Common::FR_FRA:
-				if ((newKey >= 0x20) && (newKey != 0x5e) && (newKey <= 0xff))
-					acceptableInput = true;
-				break;
-			default:
-				if ((newKey >= 0x20) && (newKey <= 0x7f))
-					acceptableInput = true;
-				break;
+			if (codePoint) {
+				acceptableInput = utf8 && cell >= 0x80;
+			} else {
+				// FEATURE: Sierra didn't check for valid characters (filtered out umlauts etc.)
+				switch (_vm->getLanguage()) {
+				case Common::RU_RUS:
+				case Common::HE_ISR:
+					if ((newKey >= 0x20) && (newKey <= 0xff))
+						acceptableInput = true;
+					break;
+				case Common::FR_FRA:
+					if ((newKey >= 0x20) && (newKey != 0x5e) && (newKey <= 0xff))
+						acceptableInput = true;
+					break;
+				default:
+					if ((newKey >= 0x20) && (newKey <= 0x7f))
+						acceptableInput = true;
+					break;
+				}
+			}
+
+			if (acceptableInput && _inputStringMaxBytes) {
+				uint32 trial[ARRAYSIZE(_inputString)];
+				memcpy(trial, _inputString, sizeof(trial));
+				trial[_inputStringCursorPos] = cell;
+				acceptableInput = stringFitsBudget(trial, _inputStringCursorPos + 1);
 			}
 
 			if (acceptableInput) {
-				if ((_vm->_game.cycleInnerLoopType == CYCLE_INNERLOOP_GETSTRING) || ((newKey >= '0') && (newKey <= '9'))) {
+				if ((_vm->_game.cycleInnerLoopType == CYCLE_INNERLOOP_GETSTRING) || ((cell >= '0') && (cell <= '9'))) {
 					// Additionally check for GETNUMBER-mode, if character is a number
 					// Sierra also did not do this
-					_inputString[_inputStringCursorPos] = newKey;
+					_inputString[_inputStringCursorPos] = cell;
 					_inputStringCursorPos++;
 					_inputString[_inputStringCursorPos] = 0;
 					if (!_vm->isLanguageRTL()) {
-						displayCharacter(newKey);
+						displayCharacter(cell);
 					} else {
-						for(int i = 0; i < _inputStringCursorPos ; i++)
+						for (int i = 0; i < _inputStringCursorPos; i++)
 							displayCharacter(0x08);
 						inputEditOff();
-						displayText((const char *)_inputString);
+						displayText(stringGetText().c_str());
 					}
 
@@ -1181,101 +1348,15 @@ void TextMgr::stringRememberForAutoComplete(bool entered) {
  */
 char *TextMgr::stringWordWrap(const char *originalText, int16 maxWidth, int16 *calculatedWidthPtr, int16 *calculatedHeightPtr) {
-	static char resultWrappedBuffer[2000];
+	// UTF-8 text mode: up to 4 bytes a cell (2000 cells as before)
+	static char resultWrappedBuffer[kTextBufferBytes];
 	int16 boxWidth = 0;
 	int16 boxHeight = 0;
-	int16 lineWidth = 0; // width of current line
-
-	int16 lineWidthLeft = maxWidth; // width left of current line
-
-	int16 wordStartPos = 0;
-	int16 curReadPos = 0;
-	int16 curWritePos = 0;
-
-	//memset(resultWrappedBuffer, 0, sizeof(resultWrappedBuffer)); for debugging
-
-	// Good testcases:
-	// King's Quest 1 intro:              the scrolling text is filled up with spaces, so that old lines are erased
-	// Apple IIgs restart system UI:      spaces used to make the window larger
-	// Gold Rush Stagecoach path room 60: "  Lake Michigan!", with max length 9 -> should get split into "  Lake" / "Michigan!"
-
-	while (originalText[curReadPos]) {
-		// Try to find out length of next word
-
-		// If first character is a space, skip it, so that we process at least this space
-		if (originalText[curReadPos] == ' ')
-			curReadPos++;
-
-		while (originalText[curReadPos]) {
-			if (originalText[curReadPos] == ' ')
-				break;
-			if (originalText[curReadPos] == 0x0A)
-				break;
-			curReadPos++;
-		}
-		byte wordEndChar = originalText[curReadPos];
-
-		// Calculate word length
-		int16 wordLen = curReadPos - wordStartPos;
-
-		if (wordLen >= lineWidthLeft) {
-			// Not enough space left
-
-			// If first character right after the new line is a space, skip over it
-			if (wordLen) {
-				if (originalText[wordStartPos] == ' ') {
-					wordStartPos++;
-					wordLen--;
-				}
-			}
-
-			if (wordLen > maxWidth) {
-				// Word way too long, split it in half
-				curReadPos = curReadPos - (wordLen - maxWidth);
-				wordLen = maxWidth;
-			}
-
-			// Add new line
-			resultWrappedBuffer[curWritePos++] = 0x0A;
-			if (lineWidth > boxWidth)
-				boxWidth = lineWidth;
-			boxHeight++; lineWidth = 0;
-			lineWidthLeft = maxWidth;
-
-			// Reached absolute maximum? -> exit now
-			if (boxHeight >= HEIGHT_MAX)
-				break;
-		}
-
-		// Copy current word over
-		memcpy(&resultWrappedBuffer[curWritePos], &originalText[wordStartPos], wordLen);
-		lineWidth += wordLen;
-		lineWidthLeft -= wordLen;
-		curWritePos += wordLen;
-
-		if (wordEndChar == 0x0A) {
-			// original text had a new line, so force it
-			curReadPos++;
-
-			resultWrappedBuffer[curWritePos++] = 0x0A;
-			if (lineWidth > boxWidth)
-				boxWidth = lineWidth;
-			boxHeight++; lineWidth = 0;
-			lineWidthLeft = maxWidth;
-
-			// Reached absolute maximum? -> exit now
-			if (boxHeight >= HEIGHT_MAX)
-				break;
-		}
-
-		wordStartPos = curReadPos;
-	}
-
-	resultWrappedBuffer[curWritePos] = 0;
-
-	if (curReadPos > 0) {
-		if (lineWidth > boxWidth)
-			boxWidth = lineWidth;
-		boxHeight++;
-	}
+	Common::String wrapped;
+	// Widths are cells (the 40-column grid), positions bytes: TextCells
+	// keeps the two apart and never cuts inside a character (K5).
+	const bool utf8 = _vm->textIsUtf8();
+	TextCells::wordWrap(originalText, maxWidth, utf8, utf8 && _vm->hiresHangulAny(), wrapped, boxWidth, boxHeight);
+	Common::strlcpy(resultWrappedBuffer, TextCells::clipBytes(wrapped.c_str(), sizeof(resultWrappedBuffer) - 1, utf8).c_str(),
+	                sizeof(resultWrappedBuffer));
 
 	if (calculatedWidthPtr) {
@@ -1303,5 +1384,5 @@ static void safeStrcat(Common::String &p, const char *t) {
  */
 char *TextMgr::stringPrintf(const char *originalText) {
-	static char resultPrintfBuffer[2000];
+	static char resultPrintfBuffer[kTextBufferBytes];
 	Common::String resultString;
 	char z[16];
@@ -1378,6 +1459,15 @@ char *TextMgr::stringPrintf(const char *originalText) {
 	}
 
+	// %0 %w %s %m %g put words into a translation whose particle follows
+	// them as a marker: the particle after the inserted noun is resolved.
+	if (_vm->textIsUtf8())
+		Common::resolveKoreanParticles(resultString);
+
+	if (_vm->textIsUtf8() && resultString.size() >= sizeof(resultPrintfBuffer)) {
+		warning("stringPrintf: %u bytes cut to %u", resultString.size(), (uint)sizeof(resultPrintfBuffer) - 1);
+		resultString = TextCells::clipBytes(resultString.c_str(), sizeof(resultPrintfBuffer) - 1, true);
+	}
 	assert(resultString.size() < sizeof(resultPrintfBuffer));
-	Common::strlcpy(resultPrintfBuffer, resultString.c_str(), 2000);
+	Common::strlcpy(resultPrintfBuffer, resultString.c_str(), sizeof(resultPrintfBuffer));
 	return resultPrintfBuffer;
 }
```

### B:text.h

```diff
diff --git a/engines/agi/text.h b/engines/agi/text.h
index 54d61530..095abdf6 100644
--- a/engines/agi/text.h
+++ b/engines/agi/text.h
@@ -23,6 +23,13 @@
 #define AGI_TEXT_H
 
+#include "common/str.h"
+
+#include "agi/hangulline.h"
+
 namespace Agi {
 
+/** stringPrintf()/stringWordWrap() result buffers: 2000 cells of up to 4 bytes (UTF-8 text mode). */
+enum { kTextBufferBytes = 2000 * 4 };
+
 struct TextPos_Struct {
 	int16 row;
@@ -123,5 +130,5 @@ public:
 	void displayAdjustRTL(int16 textRow, int16 textColumn, char *text, int16 calculatedWidth);
 	void displayText(const char *textPtr, bool disabledLook = false);
-	void displayCharacter(byte character, bool disabledLook = false);
+	void displayCharacter(uint32 character, bool disabledLook = false);
 
 	void displayTextInsideWindow(const char *textPtr, int16 windowRow, int16 windowColumn);
@@ -169,6 +176,8 @@ public:
 	int16 _promptRow;
 	int16 _promptCursorPos;
-	byte  _prompt[42];
-	byte  _promptPrevious[42];
+	// One slot is one cell: a code point (a byte without an overlay). The
+	// index is the column, so the 1:1 relation is in the type (K2 45516bb90d5).
+	uint32 _prompt[42];
+	uint32 _promptPrevious[42];
 
 	bool inputGetEditStatus();
@@ -185,5 +194,5 @@ public:
 	void promptRow_Set(int16 row);
 	int16 promptRow_Get();
-	void promptKeyPress(uint16 newKey);
+	void promptKeyPress(int newKey);
 	void promptCancelLine();
 	void promptEchoLine();
@@ -194,4 +203,10 @@ public:
 	void promptCommandWindow(bool recallLastCommand, uint16 newKey);
 
+	/** The line as text: UTF-8 in UTF-8 text mode, else the bytes typed. */
+	Common::String promptGetText() const;
+	Common::String promptPreviousGetText() const;
+	void promptSetFromText(const Common::String &text); // cells only; the cursor is left alone
+	Common::String stringGetText() const;
+
 	int16 _inputStringRow;
 	int16 _inputStringColumn;
@@ -199,5 +214,31 @@ public:
 	int16 _inputStringMaxLen;
 	int16 _inputStringCursorPos;
-	byte  _inputString[42];
+	uint32 _inputString[42];
+	int16 _inputStringMaxBytes; // UTF-8 text mode: the line's byte budget (the save name's 30-byte field); 0 = none
+	/** @p cells[0..len) within _inputStringMaxBytes as UTF-8. */
+	bool stringFitsBudget(const uint32 *cells, int16 len) const;
+
+	// Korean typing (UTF-8 text mode). agi_hangul_input puts up the KO/EN
+	// badge (D10); the built-in composer and Han/Yeong also need
+	// agi_hangul_builtin (default true). agi_hangul_start is the first mode.
+	bool _hangulInput; // composer and Han/Yeong
+	bool _hangulMode;  // Korean (true) or English
+	HangulLine _hangulLine;
+	bool hangulInputAvailable() const { return _hangulInput; }
+	bool hangulMode() const { return _hangulMode; }
+	uint32 hangulPreedit() const { return (_hangulInput && _hangulMode) ? _hangulLine.preedit() : 0; }
+	void hangulToggle();
+	/** Korean typing from hangul_input/hangul_builtin/hangul_start (DESIGN.md section 7); @p table: agi-ko.dat loaded. */
+	void hangulSetup(bool table);
+	/** Redraw cells[from..newLen) after erasing back from oldLen; the preedit cell inverted. */
+	void redrawInputTail(const uint32 *cells, int16 oldLen, int16 from, int16 newLen);
+	/** End a syllable run on @p cells, redrawing its last cell plainly. */
+	void hangulEndRun(const uint32 *cells, int16 len);
+	/**
+	 * The composer's share of an edit line (the prompt, get.string): a jamo
+	 * key edits the syllable run at the end of @p cells, a backspace edits it
+	 * first while one is being composed. True when @p newKey was consumed.
+	 */
+	bool hangulEditKey(int newKey, uint32 *cells, int16 &cursorPos, int16 maxCells);
 
 	bool stringWasEntered();
@@ -206,5 +247,5 @@ public:
 	void stringSet(const char *text);
 	void stringEdit(int16 stringMaxLen);
-	void stringKeyPress(uint16 newKey);
+	void stringKeyPress(int newKey);
 	void stringRememberForAutoComplete(bool entered = false); // for auto-completion
 
```

### B:words.cpp

```diff
diff --git a/engines/agi/words.cpp b/engines/agi/words.cpp
index 4d2d326d..b81abc68 100644
--- a/engines/agi/words.cpp
+++ b/engines/agi/words.cpp
@@ -23,4 +23,6 @@
 #include "agi/words.h"
 
+#include "common/korean_stem.h"
+#include "common/language_folder.h"
 #include "common/textconsole.h"
 
@@ -176,4 +178,6 @@ void Words::clearEgoWords() {
 	}
 	_egoWordCount = 0;
+	// a restore or a reset must not leave a Korean line's orderings behind
+	_candidates.clear();
 }
 
@@ -213,5 +217,5 @@ static bool isCharInvalid(const char curChar) {
 }
 
-void Words::cleanUpInput(const char *rawUserInput, Common::String &cleanInput) {
+void Words::cleanUpInput(const char *rawUserInput, Common::String &cleanInput) const {
 	cleanInput.clear();
 
@@ -245,5 +249,5 @@ void Words::cleanUpInput(const char *rawUserInput, Common::String &cleanInput) {
 }
 
-int16 Words::findWordInDictionary(const Common::String &userInputLowercase, uint16 userInputLen, uint16 userInputPos, uint16 &foundWordLen) {
+int16 Words::findWordInDictionary(const Common::String &userInputLowercase, uint16 userInputLen, uint16 userInputPos, uint16 &foundWordLen) const {
 	uint16 userInputLeft = userInputLen - userInputPos;
 	uint16 wordStartPos = userInputPos;
@@ -326,4 +330,19 @@ void Words::parseUsingDictionary(const char *rawUserInput) {
 	debugC(2, kDebugLevelScripts, "parse: userinput = \"%s\"", rawUserInput);
 
+	_candidates.clear();
+	// UTF-8 text mode: a line with Korean in it goes through the Korean
+	// parse; a plain ASCII line keeps the original code below, unchanged.
+	if (_vm->textIsUtf8()) {
+		bool high = false;
+		for (const char *p = rawUserInput; *p && !high; p++)
+			high = (byte)*p >= 0x80;
+		if (high) {
+			ParseResult r;
+			analyze(rawUserInput, r);
+			applyParse(r);
+			return;
+		}
+	}
+
 	// Reset result
 	clearEgoWords();
@@ -460,3 +479,236 @@ void Words::convertRussianUserInput(Common::String &userInputLowercase) {
 }
 
+static Common::String asciiLower(const Common::String &s) {
+	Common::String out(s);
+	for (uint i = 0; i < out.size(); i++) {
+		if ((byte)out[i] < 0x80)
+			out.setChar(tolower((byte)out[i]), i);
+	}
+	return out;
+}
+
+bool Words::lookupJoinedPair(const Common::String &first, const Common::String &second, AgiSentence &out) const {
+	// The pair as stored: 켄이 보냈다 is the one entry 켄이보냈다.
+	if (lookupKoreanToken(_korean, first + second, out, true))
+		return true;
+	// A joined pair takes the role of its second word: a reading of the
+	// second word (its particle or ending undone) joined to the first word
+	// as typed, taken only when the table stores that joined form. The
+	// first word is never read, so no reading can swallow it (나다 -> 나).
+	Common::KoreanStems stems;
+	const uint n = Common::koreanStemCandidates(second.decode(Common::kUtf8), stems);
+	const Common::U32String head = first.decode(Common::kUtf8);
+	for (uint i = 0; i < n; i++) {
+		if (!stems.len[i])
+			continue;
+		Common::U32String joined = head;
+		for (uint k = 0; k < stems.len[i]; k++)
+			joined += (Common::u32char_type_t)stems.cp[i][k];
+		AgiSentence words;
+		if (!lookupKoreanToken(_korean, joined.encode(Common::kUtf8), words, true))
+			continue;
+		if (!words.empty())
+			words.back().role = stems.role[i];
+		for (uint k = 0; k < words.size(); k++)
+			out.push_back(words[k]);
+		return true;
+	}
+	return false;
+}
+
+void Words::analyze(const char *rawUserInput, ParseResult &out) const {
+	out.candidates.clear();
+	out.unknownPos = 0;
+	Common::String userInput;
+	cleanUpInput(rawUserInput, userInput);
+	Common::String lower = asciiLower(userInput);
+	if (_vm && _vm->getLanguage() == Common::RU_RUS)
+		convertRussianUserInput(lower);
+
+	AgiSentence typed;
+	bool sawKorean = false;
+	const uint16 len = userInput.size();
+	uint16 pos = 0;
+	while (pos < len) {
+		if (userInput[pos] == ' ')
+			pos++;
+		if (pos >= len)
+			break;
+		uint16 end = pos;
+		while (end < len && userInput[end] != ' ')
+			end++;
+		const Common::String token(userInput.c_str() + pos, end - pos);
+
+		if ((byte)userInput[pos] >= 0x80) {
+			// Two Korean words the table knows only together - LSL1's password
+			// 켄이 보냈다 is one WORDS.TOK entry ("ken sent me") - are tried
+			// joined first, as SCI's joinKoreanPairs, and taken whole; stored
+			// forms only (the second word may lose its particle or ending,
+			// the first is taken as typed), so a reading never eats two words.
+			uint16 next = end + 1;
+			if (next < len && (byte)userInput[next] >= 0x80) {
+				uint16 nextEnd = next;
+				while (nextEnd < len && userInput[nextEnd] != ' ')
+					nextEnd++;
+				const Common::String second(userInput.c_str() + next, nextEnd - next);
+				AgiSentence joined;
+				if (lookupJoinedPair(token, second, joined)) {
+					sawKorean = true;
+					for (uint i = 0; i < joined.size(); i++) {
+						joined[i].word = token + " " + second;
+						typed.push_back(joined[i]);
+					}
+					pos = nextEnd;
+					continue;
+				}
+			}
+			// a Korean word: AGIKOR.DAT, exact then the stemmer's readings
+			AgiSentence words;
+			if (!lookupKoreanToken(_korean, token, words)) {
+				AgiWord unknown;
+				unknown.word = token;
+				typed.push_back(unknown);
+				out.unknownPos = typed.size();
+				break;
+			}
+			sawKorean = true;
+			for (uint i = 0; i < words.size(); i++)
+				typed.push_back(words[i]);
+			pos = end;
+			continue;
+		}
+
+		uint16 split = pos;
+		while (split < end && (byte)userInput[split] < 0x80)
+			split++;
+		if (split < end) {
+			// an English word with a Korean particle: door를, box에
+			uint8 role = Common::kKoreanRoleNone;
+			const Common::U32String tail = Common::String(userInput.c_str() + split, end - split).decode(Common::kUtf8);
+			const Common::String head(lower.c_str() + pos, split - pos);
+			uint16 found = 0;
+			const int16 id = findWordInDictionary(head, head.size(), 0, found);
+			if (Common::koreanParticleRole(tail, role) && id != DICTIONARY_RESULT_UNKNOWN && found == head.size()) {
+				sawKorean = true;
+				if (id != DICTIONARY_RESULT_IGNORE) {
+					AgiWord w;
+					w.word = token;
+					w.id = id;
+					w.role = role;
+					typed.push_back(w);
+				}
+				pos = end;
+				continue;
+			}
+			AgiWord unknown;
+			unknown.word = token;
+			typed.push_back(unknown);
+			out.unknownPos = typed.size();
+			break;
+		}
+
+		// plain English: the original longest match (it may span "pick up")
+		uint16 found = 0;
+		const int16 id = findWordInDictionary(lower, len, pos, found);
+		if (id != DICTIONARY_RESULT_IGNORE) {
+			AgiWord w;
+			w.word = Common::String(userInput.c_str() + pos, found);
+			w.id = (id == DICTIONARY_RESULT_UNKNOWN) ? 0 : id;
+			typed.push_back(w);
+			if (id == DICTIONARY_RESULT_UNKNOWN) {
+				out.unknownPos = typed.size();
+				break;
+			}
+		}
+		pos += found;
+	}
+	if (typed.size() > MAX_WORDS)
+		typed.resize(MAX_WORDS);
+	if (sawKorean && !out.unknownPos)
+		buildKoreanCandidates(typed, _preps, MAX_WORDS, out.candidates);
+	else
+		out.candidates.push_back(typed);
+}
+
+void Words::applyParse(const ParseResult &r) {
+	clearEgoWords();
+	_candidates = r.candidates;
+	uint16 count = 0;
+	if (!r.candidates.empty()) {
+		const AgiSentence &first = r.candidates[0];
+		for (uint i = 0; i < first.size() && count < MAX_WORDS; i++, count++) {
+			_egoWords[count].id = first[i].id;
+			_egoWords[count].word = first[i].word;
+		}
+	}
+	_egoWordCount = count;
+	if (r.unknownPos)
+		_vm->setVar(VM_VAR_WORD_NOT_FOUND, r.unknownPos);
+	_vm->setFlag(VM_FLAG_ENTERED_CLI, _egoWordCount > 0);
+	_vm->setFlag(VM_FLAG_SAID_ACCEPTED_INPUT, false);
+	debugC(2, kDebugLevelScripts, "korean parse: %u word(s), %u candidate(s), unknown %u",
+	       _egoWordCount, _candidates.size(), r.unknownPos);
+}
+
+void Words::commitCandidate(uint i) {
+	if (i >= _candidates.size())
+		return;
+	const AgiSentence chosen = _candidates[i];
+	clearEgoWords();
+	uint16 count = 0;
+	for (uint k = 0; k < chosen.size() && count < MAX_WORDS; k++, count++) {
+		_egoWords[count].id = chosen[k].id;
+		_egoWords[count].word = chosen[k].word;
+	}
+	_egoWordCount = count;
+	_candidates.clear();
+}
+
+void Words::loadKoreanTable() {
+	_korean.unload();
+	_preps = AgiPrepositions();
+	_korean.setDebugChannel(kDebugLevelText);
+	// agi-<code>.dat in the language folder, the "AGIKOR" table (common/korean_vocab.h).
+	const Common::String name = Common::translationTableName("agi-%s.dat", _vm->getLanguage());
+	if (name.empty() || !_korean.load(Common::Path(name), "AGIKOR", _vm->translationHeader(true)))
+		return;
+	findPrepositions();
+}
+
+bool Words::loadKoreanTableFromStream(Common::SeekableReadStream &in, const Common::TranslationFileHeader &expected) {
+	_korean.unload();
+	_preps = AgiPrepositions();
+	if (!_korean.loadFromStream(in, "AGIKOR", "agi-ko.dat", expected))
+		return false;
+	findPrepositions();
+	return true;
+}
+
+void Words::findPrepositions() {
+	// The English preposition a Korean case particle stands for, as this
+	// game spells it with a group that is not ignored (SQ0: "in" is 169,
+	// "to" and "with" are group 0, so nothing is inserted for them).
+	static const char *const kTo[] = { "to", nullptr };
+	static const char *const kAt[] = { "in", "on", "at", "into", nullptr };
+	static const char *const kFrom[] = { "from", nullptr };
+	static const char *const kWith[] = { "with", nullptr };
+	static const char *const kOf[] = { "of", nullptr };
+	const struct { uint8 role; const char *const *words; } table[] = {
+		{ Common::kKoreanRoleTo, kTo }, { Common::kKoreanRoleAt, kAt }, { Common::kKoreanRoleFrom, kFrom },
+		{ Common::kKoreanRoleWith, kWith }, { Common::kKoreanRoleOf, kOf },
+	};
+	for (uint t = 0; t < ARRAYSIZE(table); t++) {
+		for (const char *const *w = table[t].words; *w; w++) {
+			const Common::String word(*w);
+			uint16 found = 0;
+			const int16 id = findWordInDictionary(word, word.size(), 0, found);
+			if (id > 0 && id != 1 && id != 9999 && found == word.size()) {
+				_preps.group[table[t].role] = id;
+				_preps.word[table[t].role] = word;
+				break;
+			}
+		}
+	}
+}
+
 } // End of namespace Agi
```

### B:words.h

```diff
diff --git a/engines/agi/words.h b/engines/agi/words.h
index 6222f81c..c643dc6e 100644
--- a/engines/agi/words.h
+++ b/engines/agi/words.h
@@ -23,4 +23,7 @@
 #define AGI_WORDS_H
 
+#include "common/korean_vocab.h"
+#include "agi/korparse.h"
+
 namespace Agi {
 
@@ -67,7 +70,34 @@ public:
 	void parseUsingDictionary(const char *rawUserInput);
 
+	/** A parse without side effects: the console's parse/said use it, and so does a Korean line. */
+	struct ParseResult {
+		Common::Array<AgiSentence> candidates; ///< [0] becomes the ego words
+		uint16 unknownPos;                     ///< 1-based typed position of the unknown word, 0 = none
+		ParseResult() : unknownPos(0) {}
+	};
+	void analyze(const char *rawUserInput, ParseResult &out) const;
+
+	/** UTF-8 text mode: AGIKOR.DAT and the game's groups for to/in/on/at/into/from/with/of. */
+	void loadKoreanTable();
+	bool hasKoreanWords() const { return _korean.isLoaded(); }
+	/** Tests: the table from @p in, checked against @p expected (no engine needed). */
+	bool loadKoreanTableFromStream(Common::SeekableReadStream &in, const Common::TranslationFileHeader &expected);
+	bool hasCandidates() const { return _candidates.size() > 1; }
+	uint candidateCount() const { return _candidates.size(); }
+	const AgiSentence &candidate(uint i) const { return _candidates[i]; }
+	/** said() matched candidate @p i: it becomes the ego words (%w, word.to.string). */
+	void commitCandidate(uint i);
+
 private:
-	void  cleanUpInput(const char *userInput, Common::String &cleanInput);
-	int16 findWordInDictionary(const Common::String &userInputLowercase, uint16 userInputLen, uint16 userInputPos, uint16 &foundWordLen);
+	Common::KoreanVocabulary _korean;
+	AgiPrepositions _preps;
+	Common::Array<AgiSentence> _candidates;
+	void applyParse(const ParseResult &r);
+	/** A Korean pair typed as two words that the table stores as one (켄이 보냈다). */
+	void findPrepositions();
+	bool lookupJoinedPair(const Common::String &first, const Common::String &second, AgiSentence &out) const;
+
+	void  cleanUpInput(const char *userInput, Common::String &cleanInput) const;
+	int16 findWordInDictionary(const Common::String &userInputLowercase, uint16 userInputLen, uint16 userInputPos, uint16 &foundWordLen) const;
 
 	bool handleSpeedCommands(const Common::String &userInputLowercase);
```

### B:test_module.mk

```diff
diff --git a/test/module.mk b/test/module.mk
index dc60fe18..c7da8710 100644
--- a/test/module.mk
+++ b/test/module.mk
@@ -67,4 +67,15 @@ ifeq ($(ENABLE_SCI), STATIC_PLUGIN)
 endif
 
+ifeq ($(ENABLE_AGI), STATIC_PLUGIN)
+	TESTS += $(srcdir)/test/engines/agi/*.h
+	# The Korean units of the AGI engine, named one by one (not libagi.a, so
+	# nothing of the engine is pulled in): words.o needs six engine symbols,
+	# which agi_test_stubs.o defines and the tests never call.
+	TEST_LIBS += engines/agi/textcells.o engines/agi/textmode.o engines/agi/translation.o \
+		engines/agi/hirestext.o engines/agi/hangulline.o engines/agi/korparse.o \
+		engines/agi/saved_strings.o engines/agi/words.o test/engines/agi/agi_test_stubs.o \
+		graphics/libgraphics.a common/libcommon.a
+endif
+
 ifeq ($(ENABLE_SCUMM), STATIC_PLUGIN)
 	TESTS += $(srcdir)/test/engines/scumm/*.h
```

## Appendix C: tests

### C.1 `test/engines/agi/textcells.h`

```cpp
#include <cxxtest/TestSuite.h>

#include "common/str.h"
#include "common/ustr.h"

#include "agi/textcells.h"

// TextCells: one character is one cell of AGI's 40x25 grid. In UTF-8 text
// mode a character is a code point, else a byte (the Russian, Hebrew and
// French fan fonts). The tests compare against the upstream byte loop and a
// reference decoder over every input of a class or a large random sample:
// the review rounds of P3/P4/P6a found their bugs that way.
class AgiTextCellsTestSuite : public CxxTest::TestSuite {
	// A small deterministic generator (no g_system needed).
	struct Lcg {
		uint32 s;
		explicit Lcg(uint32 seed) : s(seed) {}
		uint32 next() { s = s * 1103515245u + 12345u; return s >> 8; }
		uint32 below(uint32 n) { return next() % n; }
	};

	// engines/agi/text.cpp stringWordWrap() of v2026.4.0, the loop verbatim
	// (only the static buffer became a local one).
	static void upstreamWrap(const char *originalText, int16 maxWidth, Common::String &out, int16 &w, int16 &h) {
		char resultWrappedBuffer[2000];
		int16 boxWidth = 0, boxHeight = 0, lineWidth = 0, lineWidthLeft = maxWidth;
		int16 wordStartPos = 0, curReadPos = 0, curWritePos = 0;
		while (originalText[curReadPos]) {
			if (originalText[curReadPos] == ' ')
				curReadPos++;
			while (originalText[curReadPos]) {
				if (originalText[curReadPos] == ' ')
					break;
				if (originalText[curReadPos] == 0x0A)
					break;
				curReadPos++;
			}
			byte wordEndChar = originalText[curReadPos];
			int16 wordLen = curReadPos - wordStartPos;
			if (wordLen >= lineWidthLeft) {
				if (wordLen) {
					if (originalText[wordStartPos] == ' ') {
						wordStartPos++;
						wordLen--;
					}
				}
				if (wordLen > maxWidth) {
					curReadPos = curReadPos - (wordLen - maxWidth);
					wordLen = maxWidth;
				}
				resultWrappedBuffer[curWritePos++] = 0x0A;
				if (lineWidth > boxWidth)
					boxWidth = lineWidth;
				boxHeight++; lineWidth = 0;
				lineWidthLeft = maxWidth;
				if (boxHeight >= 20)
					break;
			}
			memcpy(&resultWrappedBuffer[curWritePos], &originalText[wordStartPos], wordLen);
			lineWidth += wordLen;
			lineWidthLeft -= wordLen;
			curWritePos += wordLen;
			if (wordEndChar == 0x0A) {
				curReadPos++;
				resultWrappedBuffer[curWritePos++] = 0x0A;
				if (lineWidth > boxWidth)
					boxWidth = lineWidth;
				boxHeight++; lineWidth = 0;
				lineWidthLeft = maxWidth;
				if (boxHeight >= 20)
					break;
			}
			wordStartPos = curReadPos;
		}
		resultWrappedBuffer[curWritePos] = 0;
		if (curReadPos > 0) {
			if (lineWidth > boxWidth)
				boxWidth = lineWidth;
			boxHeight++;
		}
		out = resultWrappedBuffer;
		w = boxWidth;
		h = boxHeight;
	}

	// RFC 3629 by the letter: the code point and length of a well-formed
	// sequence at @p p, or 0 (TextCells::decode() then takes one byte).
	static int referenceDecode(const byte *p, uint32 &cp) {
		const byte b = p[0];
		int n;
		uint32 lo = 0x80, hi = 0xBF;
		if (b < 0x80) { cp = b; return 1; }
		else if (b >= 0xC2 && b <= 0xDF) { n = 2; cp = b & 0x1F; }
		else if (b == 0xE0) { n = 3; cp = 0; lo = 0xA0; }
		else if (b >= 0xE1 && b <= 0xEC) { n = 3; cp = b & 0x0F; }
		else if (b == 0xED) { n = 3; cp = 0x0D; hi = 0x9F; }
		else if (b >= 0xEE && b <= 0xEF) { n = 3; cp = b & 0x0F; }
		else if (b == 0xF0) { n = 4; cp = 0; lo = 0x90; }
		else if (b >= 0xF1 && b <= 0xF3) { n = 4; cp = b & 0x07; }
		else if (b == 0xF4) { n = 4; cp = 4; hi = 0x8F; }
		else return 0;
		for (int i = 1; i < n; i++) {
			const byte c = p[i];
			if (c < (i == 1 ? lo : 0x80) || c > (i == 1 ? hi : 0xBF))
				return 0;
			cp = (cp << 6) | (c & 0x3F);
		}
		return n;
	}

	static void randomAscii(Lcg &r, char *buf, uint maxLen) {
		static const char alphabet[] = "abcdefghij  \n.,!-";
		const uint len = r.below(maxLen);
		for (uint i = 0; i < len; i++)
			buf[i] = alphabet[r.below(sizeof(alphabet) - 1)];
		buf[len] = 0;
	}

public:
	// Every byte string of up to three bytes (and all four-byte leads with
	// sampled tails): decode() is the reference decoder, a malformed
	// sequence is its first byte, and it never reads past the NUL.
	void test_decodeIsTheReferenceOnEveryShortInput() {
		byte buf[5];
		uint mismatches = 0;
		for (uint a = 1; a < 256; a++) {
			for (uint b = 0; b < 256; b++) {
				for (uint c = 0; c < 256; c++) {
					if (a < 0x80 && (b || c))
						continue; // one cell whatever follows
					buf[0] = a; buf[1] = b; buf[2] = c; buf[3] = 0; buf[4] = 0;
					if (!b) buf[2] = 0;
					uint32 want = 0, got = 0;
					const int n = referenceDecode(buf, want);
					const int m = Agi::TextCells::decode((const char *)buf, true, got);
					if (n ? (m != n || got != want) : (m != 1 || got != a))
						mismatches++;
				}
			}
		}
		Lcg r(4);
		for (uint i = 0; i < 2000000; i++) {
			buf[0] = 0xF0 + r.below(16); buf[1] = r.below(256); buf[2] = r.below(256); buf[3] = r.below(256); buf[4] = 0;
			uint32 want = 0, got = 0;
			const int n = referenceDecode(buf, want);
			const int m = Agi::TextCells::decode((const char *)buf, true, got);
			if (n ? (m != n || got != want) : (m != 1 || got != buf[0]))
				mismatches++;
		}
		TS_ASSERT_EQUALS(mismatches, 0u);
	}

	// Upstream drops the byte after a word cut at the width when that word
	// ended in a line feed (the cut piece takes the line feed). The UTF-8 path
	// does not (the byte could be a lead byte); this is the only difference.
	static bool cutWordEndsInLineFeed(const char *text, int16 width) {
		int16 run = 0;
		for (const char *p = text; *p; p++) {
			if (*p == ' ') {
				run = 0;
			} else if (*p == '\n') {
				if (run > width)
					return true;
				run = 0;
			} else {
				run++;
			}
		}
		return false;
	}

	// The byte path (utf8 false) is upstream's loop on 300000 random lines and
	// every width; ASCII with utf8 true is too, apart from the one fix above.
	void test_wrapEqualsUpstreamOnRandomAscii() {
		Lcg r(1);
		char text[160];
		uint differ = 0, differUtf8 = 0, fixed = 0;
		for (uint i = 0; i < 300000; i++) {
			randomAscii(r, text, sizeof(text) - 1);
			const int16 width = 1 + r.below(40);
			Common::String a, b, c;
			int16 aw, ah, bw, bh, cw, ch;
			upstreamWrap(text, width, a, aw, ah);
			Agi::TextCells::wordWrap(text, width, false, false, b, bw, bh);
			Agi::TextCells::wordWrap(text, width, true, false, c, cw, ch);
			if (a != b || aw != bw || ah != bh)
				differ++;
			if (a != c || aw != cw || ah != ch) {
				if (cutWordEndsInLineFeed(text, width))
					fixed++;
				else
					differUtf8++;
			}
		}
		TS_ASSERT_EQUALS(differ, 0u);
		TS_ASSERT_EQUALS(differUtf8, 0u);
		TS_ASSERT(fixed > 0u); // the generator does reach the fixed case
	}

	// Mixed Hangul/ASCII: no line holds more cells than the width, no break
	// falls inside a character, the reported size is the text's, and every
	// character of the input comes out in order (spaces at breaks dropped).
	void test_wrapKeepsCharactersWholeOnRandomHangul() {
		Lcg r(2);
		static const char *const pieces[] = { "\xEA\xB0\x80", "\xED\x9E\xA3", "\xEB\xB4\x90", "a", "bc", " ", " ", "\n", "!" };
		uint bad = 0;
		for (uint i = 0; i < 100000; i++) {
			Common::String in;
			const uint n = r.below(120);
			for (uint k = 0; k < n; k++)
				in += pieces[r.below(ARRAYSIZE(pieces))];
			const int16 width = 1 + r.below(40);
			const bool any = r.below(2);
			Common::String out;
			int16 w, h;
			bool truncated = false;
			Agi::TextCells::wordWrap(in.c_str(), width, true, any, out, w, h, &truncated);
			if (!Agi::TextCells::isValidUtf8(out.c_str()))
				bad++;
			int16 maxCells = 0, lines = out.empty() ? 0 : 1;
			Common::String line;
			for (uint p = 0; p <= out.size(); p++) {
				if (p == out.size() || out[p] == '\n') {
					maxCells = MAX<int16>(maxCells, Agi::TextCells::cellLen(line.c_str(), true));
					line.clear();
					if (p < out.size())
						lines++;
				} else {
					line += out[p];
				}
			}
			// 21: upstream counts the line after a stop at HEIGHT_MAX (20) too
			if (maxCells > width || w != maxCells || h > 21)
				bad++;
			if (!truncated) {
				Common::String a, b;
				for (uint p = 0; p < in.size(); p++)
					if (in[p] != ' ' && in[p] != '\n')
						a += in[p];
				for (uint p = 0; p < out.size(); p++)
					if (out[p] != ' ' && out[p] != '\n')
						b += out[p];
				if (a != b)
					bad++;
			}
		}
		TS_ASSERT_EQUALS(bad, 0u);
	}

	// clipBytes() and clipCells() never cut a character and are the longest such prefix.
	void test_clipsAreTheLongestWholePrefix() {
		Lcg r(3);
		static const char *const pieces[] = { "\xEA\xB0\x80", "\xF0\x9F\x98\x80", "\xC3\xA9", "a" };
		uint bad = 0;
		for (uint i = 0; i < 50000; i++) {
			Common::String in;
			const uint n = r.below(30);
			for (uint k = 0; k < n; k++)
				in += pieces[r.below(ARRAYSIZE(pieces))];
			const int16 maxBytes = r.below(60);
			const Common::String c = Agi::TextCells::clipBytes(in.c_str(), maxBytes, true);
			if ((int)c.size() > maxBytes || !Agi::TextCells::isValidUtf8(c.c_str()) || !in.hasPrefix(c))
				bad++;
			if (c.size() < in.size()) {
				uint32 cp;
				const int next = Agi::TextCells::decode(in.c_str() + c.size(), true, cp);
				if ((int)c.size() + next <= maxBytes)
					bad++; // one more character would have fitted
			}
			const int16 maxCells = r.below(20);
			const Common::String d = Agi::TextCells::clipCells(in.c_str(), maxCells, true);
			if (Agi::TextCells::cellLen(d.c_str(), true) != MIN<int16>(maxCells, Agi::TextCells::cellLen(in.c_str(), true)) || !in.hasPrefix(d))
				bad++;
		}
		TS_ASSERT_EQUALS(bad, 0u);
	}

	// The save list line: the byte path is SystemUI::createSavedGameDisplayText() of v2026.4.0; the UTF-8 path is 31 cells.
	void test_savedGameLine() {
		Lcg r(5);
		uint bad = 0;
		for (uint i = 0; i < 20000; i++) {
			char desc[40];
			randomAscii(r, desc, 36);
			for (char *p = desc; *p; p++)
				if (*p == '\n')
					*p = 'x';
			const int16 slot = r.below(100);
			const char fill = r.below(2) ? ' ' : 0;
			// upstream
			char up[32];
			memset(up, fill, 31);
			char id[3];
			Common::sprintf_s(id, "%02d", slot);
			memcpy(up, id, 2);
			up[2] = ':';
			int16 len = strlen(desc);
			if (len > 28)
				len = 28;
			if (len > 0)
				memcpy(up + 3, desc, len);
			up[31] = 0;
			if (Agi::TextCells::savedGameLine(desc, slot, fill, false) != Common::String(up))
				bad++;
		}
		TS_ASSERT_EQUALS(bad, 0u);
		const Common::String ko = Agi::TextCells::savedGameLine("\xEA\xB0\x80\xEA\xB0\x80\xEA\xB0\x80", 7, ' ', true);
		TS_ASSERT_EQUALS(Agi::TextCells::cellLen(ko.c_str(), true), 31);
		TS_ASSERT(ko.hasPrefix("07:\xEA\xB0\x80"));
	}
};
```

### C.2 `test/engines/agi/textmode.h`

```cpp
#include <cxxtest/TestSuite.h>

#include "agi/textmode.h"

// AGI's text mode (agi/textmode.h): upstream (kSingle) unless the target's
// language has a translation in the game folder.
class AgiTextModeTestSuite : public CxxTest::TestSuite {
	static Agi::TextModeInputs korean() {
		Agi::TextModeInputs in;
		in.detected = Common::EN_ANY;
		in.requested = Common::KO_KOR;
		in.translationFolder = true;
		in.stringTable = true;
		return in;
	}

public:
	void test_englishStaysUpstream() {
		Agi::TextModeInputs in;
		in.detected = Common::EN_ANY;
		Common::String notes;
		const Agi::TextMode m = Agi::decideTextMode(in, notes);
		TS_ASSERT(!m.utf8());
		TS_ASSERT(!m.hires);
		TS_ASSERT_EQUALS(m.language, Common::EN_ANY);
	}

	void test_koreanTranslationIsUtf8AndHires() {
		Common::String notes;
		const Agi::TextMode m = Agi::decideTextMode(korean(), notes);
		TS_ASSERT(m.utf8());
		TS_ASSERT(m.hires);
		TS_ASSERT_EQUALS(m.language, Common::KO_KOR);
		TS_ASSERT(notes.empty());
	}

	void test_languageWithoutTranslationChangesNothing() {
		Agi::TextModeInputs in = korean();
		in.translationFolder = false;
		in.stringTable = false;
		Common::String notes;
		const Agi::TextMode m = Agi::decideTextMode(in, notes);
		TS_ASSERT(!m.utf8());
		TS_ASSERT_EQUALS(m.language, Common::EN_ANY);
		TS_ASSERT(notes.contains("no translation"));
	}

	void test_folderWithoutTableIsStillUtf8() {
		Agi::TextModeInputs in = korean();
		in.stringTable = false;
		Common::String notes;
		TS_ASSERT(Agi::decideTextMode(in, notes).utf8());
		TS_ASSERT(notes.contains("no agi string table"));
	}

	void test_offSwitchesAndKeys() {
		Agi::TextModeInputs in = korean();
		in.koreanText = false;
		Common::String notes;
		Agi::TextMode m = Agi::decideTextMode(in, notes);
		TS_ASSERT(!m.utf8());
		TS_ASSERT_EQUALS(m.language, Common::EN_ANY);
		in = korean();
		in.hiresText = false;
		m = Agi::decideTextMode(in, notes);
		TS_ASSERT(m.utf8());
		TS_ASSERT(!m.hires);
		in = korean();
		in.textEncoding = "cp949";
		m = Agi::decideTextMode(in, notes);
		TS_ASSERT(m.utf8()); // cp949 is no AGI encoding: auto
		TS_ASSERT(notes.contains("not auto or utf8"));
		Agi::TextModeInputs en;
		en.detected = Common::EN_ANY;
		en.textEncoding = "utf8";
		TS_ASSERT(Agi::decideTextMode(en, notes).utf8());
	}
};
```

### C.3 `test/engines/agi/translation.h`

```cpp
#include <cxxtest/TestSuite.h>

#include "common/memstream.h"
#include "agi/translation.h"
#include "common/translation_file.h"

// The AGI overlay agi-<lang>.str (design 8.1): key TAB text, keys
// m<logic>:<n> (n 1-based, as scripts number messages), o<index> and v<view>
// (a view's description: show.obj's inventory close-up text).
class AgiTranslationTestSuite : public CxxTest::TestSuite {
	static bool load(Agi::TextOverlay &o, const char *text,
	                 const Common::TranslationFileHeader &expected = Common::TranslationFileHeader()) {
		Common::MemoryReadStream s((const byte *)text, strlen(text));
		return o.loadFromStream(s, "agi-ko.str", expected);
	}

	static Common::TranslationFileHeader lsl1() {
		Common::TranslationFileHeader h;
		h.format = 1;
		h.game = "lsl1";
		h.engine = "agi";
		h.fingerprint = "1d9311bbee31342e76c7570a29d7616d";
		return h;
	}

public:
	void test_entries_and_lookup() {
		Agi::TextOverlay o;
		TS_ASSERT(load(o, "\xEF\xBB\xBF# SQ0\nm5:1\t\xEB\xAC\xB8\nm0:12\tGame\\nmenu\no3\t\xEC\x97\xB4\xEC\x87\xA0\n\n"));
		TS_ASSERT(o.isPresent());
		TS_ASSERT(o.isLoaded());
		TS_ASSERT_EQUALS(o.entryCount(), 3u);
		TS_ASSERT(o.message(5, 1));
		TS_ASSERT_EQUALS(*o.message(5, 1), "\xEB\xAC\xB8");
		TS_ASSERT_EQUALS(*o.message(0, 12), "Game\nmenu");
		TS_ASSERT_EQUALS(*o.object(3), "\xEC\x97\xB4\xEC\x87\xA0");
		TS_ASSERT(!o.message(5, 2));
		TS_ASSERT(!o.object(5));
		TS_ASSERT(!o.message(3, 0));
	}

	void test_view_descriptions_are_their_own_keys() {
		Agi::TextOverlay o;
		TS_ASSERT(load(o, "o3\tobject\nv3\tview\nm3:3\tmessage\n"));
		TS_ASSERT_EQUALS(o.entryCount(), 3u);
		TS_ASSERT_EQUALS(*o.object(3), "object");
		TS_ASSERT_EQUALS(*o.view(3), "view");
		TS_ASSERT_EQUALS(*o.message(3, 3), "message");
		TS_ASSERT(!o.view(4));
	}

	void test_comments_only_is_present_but_not_loaded() {
		Agi::TextOverlay o;
		TS_ASSERT(load(o, "# nothing yet\n"));
		TS_ASSERT(o.isPresent());
		TS_ASSERT(!o.isLoaded());
	}

	void test_a_bad_line_rejects_the_table() {
		static const char *const bad[] = {
			"m5\tno colon\n", "m5:x\tbad number\n", "q1\tunknown prefix\n", "m5:1 no tab\n",
			"o\tno index\n", "m5:0\tmessage zero\n", "m70000:1\ttoo big\n",
		};
		for (uint i = 0; i < ARRAYSIZE(bad); ++i) {
			Agi::TextOverlay o;
			const Common::String text = Common::String("m1:1\tok\n") + bad[i];
			TS_ASSERT(!load(o, text.c_str()));
			TS_ASSERT(!o.isPresent());
			TS_ASSERT(!o.message(1, 1));
		}
	}

	// DESIGN.md section 4: the header names the game; another game's table is refused whole.
	void test_header_matches_or_refuses() {
		const char *head = "#! format=1 game=lsl1 fingerprint=1d9311bbee31342e76c7570a29d7616d engine=agi\n";
		Agi::TextOverlay o;
		TS_ASSERT(load(o, (Common::String(head) + "m1:1\tok\n").c_str(), lsl1()));
		TS_ASSERT(o.getHeader().present());
		TS_ASSERT_EQUALS(*o.message(1, 1), "ok");
		Common::TranslationFileHeader other = lsl1();
		other.fingerprint = "0d9311bbee31342e76c7570a29d7616d";
		Agi::TextOverlay p;
		TS_ASSERT(!load(p, (Common::String(head) + "m1:1\tok\n").c_str(), other));
		TS_ASSERT(!p.message(1, 1));
		Common::TranslationFileHeader sci = lsl1();
		sci.engine = "sci";
		Agi::TextOverlay q;
		TS_ASSERT(!load(q, (Common::String(head) + "m1:1\tok\n").c_str(), sci));
		// no header: loaded unchecked (a pack made before headers)
		Agi::TextOverlay r;
		TS_ASSERT(load(r, "m1:1\tok\n", lsl1()));
		TS_ASSERT(!r.getHeader().present());
		// a broken header line is refused, not read as an entry
		Agi::TextOverlay t;
		TS_ASSERT(!load(t, "#! format=2 game=lsl1\nm1:1\tok\n", lsl1()));
	}

	// A text that is not UTF-8 (a CP949 or Latin-1 line pasted in) rejects the table.
	void test_text_must_be_utf8() {
		static const char *const bad[] = { "m1:1\t\xB9\xAE\n", "m1:1\tcaf\xE9\n", "m1:1\t\xEA\xB0\n", "m1:1\t\xED\xA0\x80\n" };
		for (uint i = 0; i < ARRAYSIZE(bad); ++i) {
			Agi::TextOverlay o;
			TS_ASSERT(!load(o, bad[i]));
		}
	}

	void test_duplicate_key_later_wins() {
		Agi::TextOverlay o;
		TS_ASSERT(load(o, "m1:1\tfirst\nm1:1\tsecond\n"));
		TS_ASSERT_EQUALS(*o.message(1, 1), "second");
		TS_ASSERT_EQUALS(o.entryCount(), 1u);
	}

	void test_escapes() {
		TS_ASSERT_EQUALS(Agi::TextOverlay::unescape("a\\nb\\tc\\\\d\\q"), "a\nb\tc\\d\\q");
	}

	void test_parse_line_keys() {
		Agi::TextOverlay::Key k;
		Common::String t;
		const char *err = nullptr;
		TS_ASSERT_EQUALS(Agi::TextOverlay::parseLine("m12:3\tx\ty", k, t, err), Agi::TextOverlay::kLineEntry);
		TS_ASSERT_EQUALS(k.kind, Agi::TextOverlay::kKeyMessage);
		TS_ASSERT_EQUALS(k.logic, 12);
		TS_ASSERT_EQUALS(k.number, 3);
		TS_ASSERT_EQUALS(t, "x\ty");
		TS_ASSERT_EQUALS(Agi::TextOverlay::keyName(k), "m12:3");
		TS_ASSERT_EQUALS(Agi::TextOverlay::parseLine("o7\t", k, t, err), Agi::TextOverlay::kLineEntry);
		TS_ASSERT_EQUALS(k.kind, Agi::TextOverlay::kKeyObject);
		TS_ASSERT_EQUALS(Agi::TextOverlay::keyName(k), "o7");
		TS_ASSERT_EQUALS(Agi::TextOverlay::parseLine("v121\tring", k, t, err), Agi::TextOverlay::kLineEntry);
		TS_ASSERT_EQUALS(k.kind, Agi::TextOverlay::kKeyView);
		TS_ASSERT_EQUALS(k.number, 121);
		TS_ASSERT_EQUALS(Agi::TextOverlay::keyName(k), "v121");
		TS_ASSERT_EQUALS(Agi::TextOverlay::parseLine("v300\tx", k, t, err), Agi::TextOverlay::kLineError);
		TS_ASSERT_EQUALS(Agi::TextOverlay::parseLine("", k, t, err), Agi::TextOverlay::kLineSkip);
		TS_ASSERT_EQUALS(Agi::TextOverlay::parseLine("# c", k, t, err), Agi::TextOverlay::kLineSkip);
	}

	// Review focus 1: an untranslated message with code-page bytes becomes
	// UTF-8 before it reaches the UTF-8 decoder.
	void test_legacy_bytes_become_utf8() {
		TS_ASSERT_EQUALS(Agi::TextOverlay::legacyToUtf8("caf\x82", Common::kDos850), "caf\xC3\xA9");
		TS_ASSERT_EQUALS(Agi::TextOverlay::legacyToUtf8("plain", Common::kDos850), "plain");
	}
};
```

### C.4 `test/engines/agi/hirestext.h`

```cpp
#include <cxxtest/TestSuite.h>

#include "test/graphics/hires_support.h"
#include "test/system/null_osystem.h"

#include "graphics/hires_text/svfchain.h"
#include "graphics/hires_text/svfface.h"

#include "agi/hirestext.h"

#include "common/ptr.h"

// AgiHiResText: a 16x16 1 bpp stencil per character for AGI's 640x400 display.
class AgiHiResTextTestSuite : public CxxTest::TestSuite {
	static const char *gameMap() {
		return "[map]\nversion=3\n[fonts]\njamo=data:fixture/JAMO.SVF\n[font]\nface=jamo\nmissing=u+25a1\n";
	}

	struct Gray {
		byte px[16 * 16];
	};
	static void plot(void *ctx, int16_t x, int16_t y, uint8_t coverage) {
		Gray *g = (Gray *)ctx;
		if (x >= 0 && y >= 0 && x < 16 && y < 16)
			g->px[y * 16 + x] = MAX<byte>(g->px[y * 16 + x], coverage);
	}

public:
	void setUp() override {
		Common::install_null_g_system();
	}
	void tearDown() override {
		Common::uninstall_null_g_system();
	}

	void test_basicLatinIsAgisOwnFont() {
		HiResTest::MemoryFiles files;
		files.addText("game/ko/HIRESBIT.MAP", gameMap());
		Agi::AgiHiResText t;
		Common::Array<Common::String> notes;
		TS_ASSERT(t.setup(&files, Graphics::HiResOptions(), Common::Path("game"), "ko", notes));
		uint16 rows[16];
		for (uint32 cp = 0; cp < 0x80; cp++)
			TS_ASSERT(!t.glyph(cp, rows));
		TS_ASSERT(t.glyph(0xAC00, rows));
	}

	// Every Hangul syllable: the stencil is the face drawn straight through
	// the chain at the cell's pen position and cut at coverage 128 (the glue:
	// centring, baseline, clip, bit order), and has ink.
	void test_everySyllableIsTheFaceInItsCell() {
		HiResTest::MemoryFiles files;
		files.addText("game/ko/HIRESBIT.MAP", gameMap());
		Agi::AgiHiResText t;
		Common::Array<Common::String> notes;
		TS_ASSERT(t.setup(&files, Graphics::HiResOptions(), Common::Path("game"), "ko", notes));
		// the reference chain: the same fixture face, opened on its own
		Graphics::SvfFace face;
		TS_ASSERT(face.loadFromMemory(kSvfFixtureJamo, sizeof(kSvfFixtureJamo), "JAMO.SVF"));
		Graphics::SvfChain chain;
		TS_ASSERT(chain.addFace(&face));
		const int line = chain.getAscent() + chain.getDescent();
		uint bad = 0, empty = 0;
		for (uint32 cp = 0xAC00; cp <= 0xD7A3; cp++) {
			uint16 rows[16];
			if (!t.glyph(cp, rows)) {
				bad++;
				continue;
			}
			char utf8[4];
			utf8[0] = (char)(0xE0 | (cp >> 12));
			utf8[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
			utf8[2] = (char)(0x80 | (cp & 0x3F));
			const int advance = CLIP<int>(chain.getWidth(utf8, 3), 0, 16);
			Gray g;
			memset(g.px, 0, sizeof(g.px));
			chain.drawCoverage(utf8, 3, (16 - advance) / 2, (16 - line) / 2 + chain.getAscent(), 16, 16, plot, &g);
			uint ink = 0;
			for (int y = 0; y < 16; y++) {
				uint16 want = 0;
				for (int x = 0; x < 16; x++)
					if (g.px[y * 16 + x] >= 128)
						want |= (uint16)(0x8000 >> x);
				if (want != rows[y])
					bad++;
				for (int x = 0; x < 16; x++)
					ink += (rows[y] >> x) & 1;
			}
			if (!ink)
				empty++;
		}
		TS_ASSERT_EQUALS(bad, 0u);
		TS_ASSERT_EQUALS(empty, 0u);
	}

	void test_offWithoutAMapWhenHiresTextIsOff() {
		HiResTest::MemoryFiles files;
		Graphics::HiResOptions off;
		off.enabled = false;
		Agi::AgiHiResText t;
		Common::Array<Common::String> notes;
		TS_ASSERT(!t.setup(&files, off, Common::Path("game"), "ko", notes));
		TS_ASSERT(!t.isActive());
		uint16 rows[16];
		TS_ASSERT(!t.glyph(0xAC00, rows));
	}
};
```

### C.5 `test/engines/agi/hangul_line.h`

```cpp
#include <cxxtest/TestSuite.h>

#include "agi/hangulline.h"

#include "common/hangul.h"

// The built-in composer driving AGI's code-point edit line: a jamo key
// rewrites the run at the end of the line (committed syllables + the one in
// progress), a non-jamo key ends it, a jamo that would grow the line past
// its budget is refused and changes nothing (review focus 2).
class AgiHangulLineTestSuite : public CxxTest::TestSuite {
	static void feed(Agi::HangulLine &h, const char *keys, uint32 *cells, int16 &len, int16 max) {
		for (; *keys; keys++)
			h.feed(*keys, cells, len, max);
	}

public:
	void test_dufthl_is_yeolsoe() {
		Agi::HangulLine h;
		uint32 cells[42] = { 0 };
		int16 len = 0;
		feed(h, "dufthl", cells, len, 40);
		TS_ASSERT_EQUALS(len, 2);
		TS_ASSERT_EQUALS(cells[0], 0xC5F4u); // 열
		TS_ASSERT_EQUALS(cells[1], 0xC1E0u); // 쇠
		TS_ASSERT_EQUALS(cells[2], 0u);
		TS_ASSERT(h.composing());
		TS_ASSERT_EQUALS(h.preedit(), 0xC1E0u);
	}

	void test_backspace_removes_jamo_then_syllables() {
		Agi::HangulLine h;
		uint32 cells[42] = { 0 };
		int16 len = 0;
		feed(h, "dufthl", cells, len, 40);
		TS_ASSERT(h.backspace(cells, len));
		TS_ASSERT_EQUALS(len, 2);
		TS_ASSERT_DIFFERS(cells[1], 0xC1E0u); // 쇠 lost a jamo
		int guard = 0;
		while (h.backspace(cells, len) && guard++ < 20) {
		}
		TS_ASSERT_EQUALS(len, 0);
		TS_ASSERT_EQUALS(cells[0], 0u);
		TS_ASSERT(!h.composing());
		TS_ASSERT(!h.backspace(cells, len));
	}

	void test_a_non_jamo_key_ends_the_run() {
		Agi::HangulLine h;
		uint32 cells[42] = { 0 };
		int16 len = 0;
		feed(h, "gks", cells, len, 40);
		TS_ASSERT_EQUALS(len, 1);
		TS_ASSERT_EQUALS(cells[0], 0xD55Cu); // 한
		TS_ASSERT(!h.feed('1', cells, len, 40));
		TS_ASSERT(!h.composing());
		TS_ASSERT_EQUALS(len, 1);
		feed(h, "rk", cells, len, 40); // a new run after the committed 한
		TS_ASSERT_EQUALS(len, 2);
		TS_ASSERT_EQUALS(cells[0], 0xD55Cu);
		TS_ASSERT_EQUALS(cells[1], 0xAC00u); // 가
		TS_ASSERT_EQUALS(h.runStart(), 1);
	}

	void test_budget_refuses_a_jamo_without_change() {
		Agi::HangulLine h;
		uint32 cells[42] = { 0 };
		int16 len = 39;
		for (int i = 0; i < 39; i++)
			cells[i] = 'a';
		feed(h, "rkr", cells, len, 40); // 각 at cell 39
		TS_ASSERT_EQUALS(len, 40);
		TS_ASSERT_EQUALS(cells[39], 0xAC01u);
		TS_ASSERT(h.feed('k', cells, len, 40)); // 가가 would need 41
		TS_ASSERT_EQUALS(len, 40);
		TS_ASSERT_EQUALS(cells[39], 0xAC01u);
		TS_ASSERT_EQUALS(cells[40], 0u);
		TS_ASSERT(h.composing());
	}
	// 50000 random runs of jamo keys, other keys, backspaces and commits under a
	// random budget: the line never passes the budget, the cells before the
	// run never change, and the run is the composer's text for the same keys.
	void test_randomKeysKeepTheRunInside() {
		static const char keys[] = "rRseEfaqQtTdwWczxvgkoiOjpuPhynbml1 .";
		uint32 seed = 7;
		uint bad = 0;
		for (uint round = 0; round < 50000; round++) {
			uint32 cells[42];
			int16 len = 0;
			cells[0] = 0;
			seed = seed * 1103515245u + 12345u;
			const int16 budget = 1 + (seed >> 8) % 40;
			Agi::HangulLine line;
			Common::HangulComposer model;
			uint32 before[42];
			for (uint k = 0; k < 30; k++) {
				seed = seed * 1103515245u + 12345u;
				const uint pick = (seed >> 8) % (sizeof(keys) + 2);
				memcpy(before, cells, sizeof(cells));
				const int16 keep = line.composing() ? line.runStart() : len; // what this key may not touch
				if (pick == sizeof(keys)) { // backspace
					if (line.backspace(cells, len))
						model.backspace();
				} else if (pick == sizeof(keys) + 1) { // a commit (Enter, a mode switch)
					line.commit(len);
					model.reset();
				} else {
					const char c = keys[pick % (sizeof(keys) - 1)];
					const Common::HangulComposer was = model;
					if (line.feed(c, cells, len, budget)) {
						model.feed(c);
						if ((int16)(line.runStart() + model.text().size()) > budget)
							model = was; // refused: the line did not change
					} else {
						model.reset(); // not a jamo: the run ended
						if (len < budget) {
							cells[len++] = (byte)c;
							cells[len] = 0;
						}
					}
				}
				if (len > budget)
					bad++;
				for (int16 i = 0; i < MIN<int16>(keep, len); i++)
					if (cells[i] != before[i])
						bad++;
				if (line.composing()) {
					const Common::U32String run = model.text();
					if ((int16)(line.runStart() + run.size()) != len)
						bad++;
					for (uint i = 0; i < run.size() && line.runStart() + (int16)i < len; i++)
						if (cells[line.runStart() + i] != run[i])
							bad++;
				}
			}
		}
		TS_ASSERT_EQUALS(bad, 0u);
	}
};
```

### C.6 `test/engines/agi/korparse.h`

```cpp
#include <cxxtest/TestSuite.h>

#include "common/korean_stem.h"
#include "common/korean_vocab.h"
#include "common/memstream.h"
#include "../../korean_vocab_fixture.h"
#include "agi/korparse.h"

// Design 5.5: Korean puts the verb last; AGI's said() is positional, so the
// typed words are re-ordered into at most four candidates, best first:
// (a) verbs, plain objects, role-marked nouns; (b) (a) with the game's own
// preposition before a role-marked noun; (c) as typed; (d) X의 Y swapped.
class AgiKorParseTestSuite : public CxxTest::TestSuite {
	Common::KoreanVocabulary _voc;

	static Agi::AgiWord w(uint16 id, uint8 role = Common::kKoreanRoleNone, bool verb = false) {
		Agi::AgiWord a;
		a.id = id;
		a.role = role;
		a.verb = verb;
		a.korean = true;
		return a;
	}
	static Common::Array<uint16> ids(const Agi::AgiSentence &s) {
		Common::Array<uint16> out;
		for (uint i = 0; i < s.size(); i++)
			out.push_back(s[i].id);
		return out;
	}
	static Common::Array<uint16> v(uint16 a, uint16 b = 0, uint16 c = 0, uint16 d = 0) {
		Common::Array<uint16> out;
		const uint16 all[4] = { a, b, c, d };
		for (int i = 0; i < 4 && all[i]; i++)
			out.push_back(all[i]);
		return out;
	}

public:
	void setUp() {
		static const KoreanVocabFixture::Row rows[] = {
			{ "\xEC\x97\xB4\xEC\x87\xA0", 12, 0 },                       // 열쇠 key
			{ "\xEC\xA7\x91\xEC\x96\xB4", 20, 1 },                       // 집어 get (verb)
			{ "\xEB\x84\xA3\xEC\x96\xB4", 50, 1 },                       // 넣어 put (verb)
			{ "\xEC\x83\x81\xEC\x9E\x90", 40, 0 },                       // 상자 box
			{ "\xEB\x82\x98\xEB\xAC\xB4", 60, 0 },                       // 나무 tree
			{ "\xEA\xB5\xAC\xEB\xA9\x8D", 61, 0 },                       // 구멍 hole
			{ "\xEB\xB4\x90", 30, 1 },                                   // 봐 look (verb)
			{ "\xEC\xA2\x80", 0, 0 },                                    // 좀 (ignored)
			{ "\xEB\x93\xA4\xEC\x97\xAC\xEB\x8B\xA4\xEB\xB4\x90", 30, 0x8001 }, // 들여다봐 look+
			{ "\xEB\x93\xA4\xEC\x97\xAC\xEB\x8B\xA4\xEB\xB4\x90", 169, 0 },    //          in
		};
		const Common::Array<byte> img = KoreanVocabFixture::build("AGIKOR", rows, ARRAYSIZE(rows));
		Common::MemoryReadStream s(img.data(), img.size());
		TS_ASSERT(_voc.loadFromStream(s, "AGIKOR", "fixture"));
	}

	void test_said_core_is_the_interpreters() {
		using Agi::saidMatches;
		Agi::AgiSentence s;
		s.push_back(w(20));
		s.push_back(w(12));
		const uint16 exact[] = { 20, 12 }, any[] = { 1, 12 }, rolTail[] = { 20, 9999 }, shorter[] = { 20 },
		             longer[] = { 20, 12, 9999 }, wrong[] = { 12, 20 };
		TS_ASSERT(saidMatches(s, exact, 2));
		TS_ASSERT(saidMatches(s, any, 2));
		TS_ASSERT(saidMatches(s, rolTail, 2));
		TS_ASSERT(!saidMatches(s, shorter, 1));
		TS_ASSERT(saidMatches(s, longer, 3));
		TS_ASSERT(!saidMatches(s, wrong, 2));
		Agi::AgiSentence one;
		one.push_back(w(20));
		TS_ASSERT(!saidMatches(one, exact, 2));
	}

	void test_token_lookup() {
		Agi::AgiSentence out;
		TS_ASSERT(Agi::lookupKoreanToken(_voc, "\xEC\x97\xB4\xEC\x87\xA0\xEB\xA5\xBC", out)); // 열쇠를
		TS_ASSERT_EQUALS(out.size(), 1u);
		TS_ASSERT_EQUALS(out[0].id, 12);
		TS_ASSERT_EQUALS(out[0].role, (uint8)Common::kKoreanRoleObject);
		TS_ASSERT_EQUALS(out[0].word, "\xEC\x97\xB4\xEC\x87\xA0\xEB\xA5\xBC");
		out.clear();
		TS_ASSERT(Agi::lookupKoreanToken(_voc, "\xEC\xA2\x80", out)); // 좀: known, ignored
		TS_ASSERT_EQUALS(out.size(), 0u);
		out.clear();
		TS_ASSERT(Agi::lookupKoreanToken(_voc, "\xEB\x93\xA4\xEC\x97\xAC\xEB\x8B\xA4\xEB\xB4\x90", out)); // look in
		TS_ASSERT_EQUALS(ids(out), v(30, 169));
		TS_ASSERT(out[0].verb);
		out.clear();
		TS_ASSERT(!Agi::lookupKoreanToken(_voc, "\xEB\xAA\xA8\xEB\xA6\x84", out)); // 모름: unknown
		out.clear();
		TS_ASSERT(!Agi::lookupKoreanToken(_voc, "\xEC\x97\xB4\xEC\x87\xA0\xEB\xA5\xBC", out, true)); // exact only
	}

	void test_exact_only_takes_a_stored_form() {
		Agi::AgiSentence out;
		TS_ASSERT(Agi::lookupKoreanToken(_voc, "\xEC\x97\xB4\xEC\x87\xA0", out, true)); // 열쇠
		TS_ASSERT_EQUALS(ids(out), v(12));
		TS_ASSERT_EQUALS(out[0].role, (uint8)Common::kKoreanRoleNone);
	}

	// 5.5: 들여다봐 = look+in. The tail of a sequence stays behind its verb
	// in every re-ordering, or said(look, in, box) is never reached.
	void test_sequence_tail_stays_with_its_verb() {
		Agi::AgiSentence typed;
		TS_ASSERT(Agi::lookupKoreanToken(_voc, "\xEC\x83\x81\xEC\x9E\x90\xEB\xA5\xBC", typed)); // 상자를
		TS_ASSERT(Agi::lookupKoreanToken(_voc, "\xEB\x93\xA4\xEC\x97\xAC\xEB\x8B\xA4\xEB\xB4\x90", typed)); // 들여다봐
		TS_ASSERT_EQUALS(ids(typed), v(40, 30, 169));
		TS_ASSERT(!typed[1].cont);
		TS_ASSERT(typed[2].cont);
		Common::Array<Agi::AgiSentence> c;
		Agi::buildKoreanCandidates(typed, Agi::AgiPrepositions(), 20, c);
		TS_ASSERT_EQUALS(c.size(), 2u);
		TS_ASSERT_EQUALS(ids(c[0]), v(30, 169, 40));
		TS_ASSERT_EQUALS(ids(c[1]), v(40, 30, 169));
		const uint16 spec[] = { 30, 169, 40 };
		TS_ASSERT(Agi::saidMatches(c[0], spec, 3));
	}

	void test_of_swap_moves_a_sequence_whole() {
		Agi::AgiSentence typed;
		typed.push_back(w(60, Common::kKoreanRoleOf));          // 나무의
		typed.push_back(w(61));                                 // 구멍에 (a sequence:
		Agi::AgiWord tail = w(62, Common::kKoreanRoleAt);       // the role is on its tail)
		tail.cont = true;
		typed.push_back(tail);
		typed.push_back(w(30, Common::kKoreanRoleNone, true));  // 봐
		Common::Array<Agi::AgiSentence> c;
		Agi::buildKoreanCandidates(typed, Agi::AgiPrepositions(), 20, c);
		TS_ASSERT_EQUALS(c.size(), 3u);
		TS_ASSERT_EQUALS(ids(c[0]), v(30, 60, 61, 62));
		TS_ASSERT_EQUALS(ids(c[1]), v(60, 61, 62, 30));
		TS_ASSERT_EQUALS(ids(c[2]), v(30, 61, 62, 60)); // (d): 구멍 moved whole
	}

	void test_verb_last_becomes_verb_first() {
		Agi::AgiSentence typed;
		typed.push_back(w(12));
		typed.push_back(w(20, Common::kKoreanRoleNone, true));
		Common::Array<Agi::AgiSentence> c;
		Agi::buildKoreanCandidates(typed, Agi::AgiPrepositions(), 20, c);
		TS_ASSERT_EQUALS(c.size(), 2u);
		TS_ASSERT_EQUALS(ids(c[0]), v(20, 12));
		TS_ASSERT_EQUALS(ids(c[1]), v(12, 20));
	}

	void test_role_nouns_follow_and_take_the_games_preposition() {
		Agi::AgiPrepositions preps;
		preps.group[Common::kKoreanRoleAt] = 169;
		preps.word[Common::kKoreanRoleAt] = "in";
		Agi::AgiSentence typed;
		typed.push_back(w(40, Common::kKoreanRoleAt));          // 상자에
		typed.push_back(w(12, Common::kKoreanRoleObject));      // 열쇠를
		typed.push_back(w(50, Common::kKoreanRoleNone, true));  // 넣어
		Common::Array<Agi::AgiSentence> c;
		Agi::buildKoreanCandidates(typed, preps, 20, c);
		TS_ASSERT_EQUALS(c.size(), 3u);
		TS_ASSERT_EQUALS(ids(c[0]), v(50, 12, 40));
		TS_ASSERT_EQUALS(ids(c[1]), v(50, 12, 169, 40));
		TS_ASSERT_EQUALS(c[1][2].word, "in");
		TS_ASSERT_EQUALS(ids(c[2]), v(40, 12, 50));
	}

	void test_of_swaps_and_duplicates_drop() {
		Agi::AgiSentence typed;
		typed.push_back(w(60, Common::kKoreanRoleOf));          // 나무의
		typed.push_back(w(61));                                 // 구멍
		typed.push_back(w(30, Common::kKoreanRoleNone, true));  // 봐
		Common::Array<Agi::AgiSentence> c;
		Agi::buildKoreanCandidates(typed, Agi::AgiPrepositions(), 20, c);
		TS_ASSERT_EQUALS(c.size(), 2u);          // (d) equals (a) and is dropped
		TS_ASSERT_EQUALS(ids(c[0]), v(30, 61, 60));
		TS_ASSERT_EQUALS(ids(c[1]), v(60, 61, 30));
	}

	void test_max_words_clips_every_candidate() {
		Agi::AgiSentence typed;
		for (int i = 0; i < 6; i++)
			typed.push_back(w(100 + i));
		typed.push_back(w(20, Common::kKoreanRoleNone, true));
		Common::Array<Agi::AgiSentence> c;
		Agi::buildKoreanCandidates(typed, Agi::AgiPrepositions(), 3, c);
		for (uint i = 0; i < c.size(); i++)
			TS_ASSERT_LESS_THAN_EQUALS(c[i].size(), 3u);
		TS_ASSERT_LESS_THAN_EQUALS(c.size(), 4u);
	}
	// op_test.cpp testSaid() of v2026.4.0, the loop verbatim over an id array.
	static bool upstreamSaid(const uint16 *ego, int n, const uint16 *cc, int nwords) {
		int z = 0;
		for (int c = 0; nwords && n; c++, nwords--, n--) {
			z = *cc++;
			switch (z) {
			case 9999:
				nwords = 1;
				break;
			case 1:
				break;
			default:
				if (ego[c] != z)
					return false;
				break;
			}
		}
		if (n && z != 9999)
			return false;
		if (nwords != 0 && *cc != 9999)
			return false;
		return true;
	}

	// 200000 random word lists and specs over ids {0..5, 1, 9999}: saidMatches() is the interpreter's loop.
	void test_saidMatchesIsTheInterpretersLoop() {
		static const uint16 alphabet[] = { 0, 2, 3, 4, 5, 1, 9999 };
		uint32 seed = 3;
		uint differ = 0;
		for (uint round = 0; round < 200000; round++) {
			seed = seed * 1103515245u + 12345u;
			const int n = (seed >> 8) % 6, nwords = (seed >> 16) % 6;
			uint16 ego[8], spec[9];
			Agi::AgiSentence s;
			for (int i = 0; i < n; i++) {
				seed = seed * 1103515245u + 12345u;
				ego[i] = alphabet[(seed >> 8) % 5]; // typed words never are 1 or 9999
				s.push_back(w(ego[i]));
			}
			for (int i = 0; i < nwords + 1; i++) {
				seed = seed * 1103515245u + 12345u;
				spec[i] = alphabet[(seed >> 8) % ARRAYSIZE(alphabet)];
			}
			if (Agi::saidMatches(s, spec, nwords) != upstreamSaid(ego, n, spec, nwords))
				differ++;
		}
		TS_ASSERT_EQUALS(differ, 0u);
	}

	// 50000 random typed sentences: at most four candidates, none longer than
	// maxWords, no two with the same ids, a sequence's tail (cont) never
	// separated from the word before it, and (c) "as typed" always there when
	// it fits.
	void test_candidatesKeepUnitsAndLimits() {
		uint32 seed = 9;
		uint bad = 0;
		for (uint round = 0; round < 50000; round++) {
			Agi::AgiSentence typed;
			seed = seed * 1103515245u + 12345u;
			const uint n = 1 + (seed >> 8) % 7;
			for (uint i = 0; i < n; i++) {
				seed = seed * 1103515245u + 12345u;
				Agi::AgiWord a = w((uint16)(10 + (seed >> 8) % 9), (uint8)((seed >> 12) % 7), ((seed >> 16) % 3) == 0);
				a.cont = i > 0 && ((seed >> 20) % 5) == 0;
				typed.push_back(a);
			}
			Agi::AgiPrepositions preps;
			preps.group[Common::kKoreanRoleTo] = 7;
			preps.group[Common::kKoreanRoleAt] = 8;
			seed = seed * 1103515245u + 12345u;
			const uint maxWords = 1 + (seed >> 8) % 8;
			Common::Array<Agi::AgiSentence> out;
			Agi::buildKoreanCandidates(typed, preps, maxWords, out);
			if (out.empty() || out.size() > 4)
				bad++;
			bool asTyped = false;
			for (uint k = 0; k < out.size(); k++) {
				if (out[k].size() > maxWords)
					bad++;
				for (uint j = 0; j < k; j++)
					if (ids(out[j]) == ids(out[k]))
						bad++;
				// a cont word follows the word it continued in the typed line
				for (uint i = 0; i < out[k].size(); i++)
					if (out[k][i].cont && (i == 0 || out[k][i - 1].id == 7 || out[k][i - 1].id == 8))
						bad++;
				Agi::AgiSentence clipped = typed;
				if (clipped.size() > maxWords)
					clipped.resize(maxWords);
				asTyped = asTyped || ids(out[k]) == ids(clipped);
			}
			if (!asTyped && out.size() < 4)
				bad++;
		}
		TS_ASSERT_EQUALS(bad, 0u);
	}
};
```

### C.7 `test/engines/agi/korean_said_data.h`

```cpp
#include <cxxtest/TestSuite.h>

#include "common/fs.h"
#include "common/ptr.h"
#include "common/stream.h"
#include "common/translation_file.h"

#include "agi/agi.h"
#include "agi/korparse.h"
#include "agi/words.h"

#include "test/system/null_osystem.h"

// Developer data (DESIGN.md section 10, the i18n gate with real data): with
// korean-packs/LSL1AGI/ (the pack's game folder, or a link to it) and
// korean-packs/LSL1AGI-said/{pairs.tsv,saids.tsv} (harness
// runs/lsl1a/parse/lsl1a-pairs.tsv and runs/lsl1a/src/saids.tsv) in the
// working directory, every walkthrough pair is judged as the harness's
// k16said_agi.py does through the debug socket: the Korean variant passes
// when it matches every said() spec its English command matches, and the
// first spec it matches is the English's first. Passes without the data.
class AgiKoreanSaidDataTestSuite : public CxxTest::TestSuite {
	static Common::FSNode child(const Common::FSNode &dir, const char *name) {
		Common::FSList list;
		dir.getChildren(list, Common::FSNode::kListAll);
		for (uint i = 0; i < list.size(); ++i)
			if (list[i].getName().equalsIgnoreCase(name))
				return list[i];
		return dir.getChild(name);
	}

	static Common::Array<int> matches(Agi::Words &w, const Common::String &text, const Common::Array<Common::Array<uint16> > &specs,
	                                  bool &unknown) {
		Agi::Words::ParseResult r;
		w.analyze(text.c_str(), r);
		Common::Array<int> out;
		unknown = r.unknownPos != 0;
		if (unknown)
			return out;
		for (uint i = 0; i < specs.size(); i++)
			for (uint k = 0; k < r.candidates.size(); k++)
				if (Agi::saidMatches(r.candidates[k], specs[i].data(), specs[i].size())) {
					out.push_back(i);
					break;
				}
		return out;
	}

public:
	void setUp() override {
		Common::install_null_g_system();
	}
	void tearDown() override {
		Common::uninstall_null_g_system();
	}

	void test_lsl1_said_baseline_when_given() {
		const Common::FSNode packs = Common::FSNode(Common::Path("korean-packs"));
		const Common::FSNode game = child(packs, "LSL1AGI");
		const Common::FSNode said = child(packs, "LSL1AGI-said");
		if (!game.isDirectory() || !said.isDirectory()) {
			TS_WARN("korean-packs/LSL1AGI and LSL1AGI-said are not in the working directory: LSL1 said() check skipped");
			return;
		}
		Agi::Words w(nullptr);
		Common::ScopedPtr<Common::SeekableReadStream> tok(child(game, "WORDS.TOK").createReadStream());
		TS_ASSERT(tok);
		if (!tok)
			return;
		TS_ASSERT_EQUALS(w.loadDictionary(*tok), 0);
		Common::ScopedPtr<Common::SeekableReadStream> dat(child(child(game, "ko"), "agi-ko.dat").createReadStream());
		TS_ASSERT(dat);
		if (!dat)
			return;
		Common::TranslationFileHeader h; // what AgiEngine::translationHeader(true) gives for LSL1 1.00 (DOS)
		h.format = 1;
		h.game = "lsl1";
		h.engine = "agi";
		h.fingerprint = "1d9311bbee31342e76c7570a29d7616d";
		h.vocab = "988a1baaf3ee0d9cd678274dc2f219f2";
		TS_ASSERT(w.loadKoreanTableFromStream(*dat, h));

		Common::Array<Common::Array<uint16> > specs;
		Common::ScopedPtr<Common::SeekableReadStream> st(child(said, "saids.tsv").createReadStream());
		while (st && !st->eos()) {
			const Common::String line = st->readLine();
			const char *p = strchr(line.c_str(), '\t');
			p = p ? strchr(p + 1, '\t') : nullptr; // logic, offset, then the words
			if (!p)
				continue;
			Common::Array<uint16> spec;
			for (p++; *p;) {
				char *end = nullptr;
				spec.push_back((uint16)strtoul(p, &end, 10));
				if (end == p)
					break;
				for (p = end; *p == ' '; p++)
					;
			}
			specs.push_back(spec);
		}
		TS_ASSERT_EQUALS(specs.size(), 2274u);

		uint pass = 0, partial = 0, nomatch = 0, unknown = 0, order = 0, english = 0, total = 0;
		Common::ScopedPtr<Common::SeekableReadStream> pt(child(said, "pairs.tsv").createReadStream());
		while (pt && !pt->eos()) {
			const Common::String line = pt->readLine();
			const char *tab = strchr(line.c_str(), '\t');
			if (!tab)
				continue;
			Common::String en(line.c_str(), tab), ko(tab + 1);
			en.trim();
			ko.trim();
			if (en.empty() || ko.empty())
				continue;
			en.toLowercase();
			total++;
			bool unk;
			const Common::Array<int> hits = matches(w, en, specs, unk);
			if (hits.empty()) {
				english++;
				continue;
			}
			const Common::Array<int> k = matches(w, ko, specs, unk);
			if (unk) {
				unknown++;
				continue;
			}
			uint in = 0;
			for (uint a = 0; a < hits.size(); a++)
				for (uint b = 0; b < k.size(); b++)
					if (k[b] == hits[a]) {
						in++;
						break;
					}
			if (in == hits.size())
				(k[0] == hits[0] ? pass : order)++;
			else if (in)
				partial++;
			else
				nomatch++;
		}
		// k16said_agi.py with the jamo build on the shipped agi-ko.dat
		// (harness runs/lsl1a/parse/said8.txt): 778 variants, pass 691,
		// partial 23, nomatch 20, unknown 7, order 6, english 31.
		TS_ASSERT_EQUALS(total, 778u);
		TS_ASSERT_EQUALS(pass, 691u);
		TS_ASSERT_EQUALS(partial, 23u);
		TS_ASSERT_EQUALS(nomatch, 20u);
		TS_ASSERT_EQUALS(unknown, 7u);
		TS_ASSERT_EQUALS(order, 6u);
		TS_ASSERT_EQUALS(english, 31u);
	}
};
```

### C.8 `test/engines/agi/agi_test_stubs.cpp`

```cpp
// The engine symbols engines/agi/words.o refers to, for the test runner,
// which links the Korean units of the AGI engine one by one. The tests use
// Words with no engine (Words(nullptr)): analyze() and the table loaders
// never reach these; each one fails the test if it is called.

#include "agi/agi.h"

#include "common/textconsole.h"

namespace Agi {

void AgiBase::setFlag(int16, bool) { error("AGI test stub: setFlag"); }
void AgiEngine::setVar(int16, byte) { error("AGI test stub: setVar"); }
void AgiGame::setSpeedLevel(byte) { error("AGI test stub: setSpeedLevel"); }
Common::Language AgiBase::getLanguage() const { error("AGI test stub: getLanguage"); }
Common::Platform AgiBase::getPlatform() const { error("AGI test stub: getPlatform"); }
Common::TranslationFileHeader AgiEngine::translationHeader(bool) const { error("AGI test stub: translationHeader"); }

} // End of namespace Agi
```

### C.9 `test/engines/agi/saved_strings.h`

```cpp
#include <cxxtest/TestSuite.h>

#include "common/memstream.h"
#include "common/str.h"

#include "agi/saved_strings.h"
#include "agi/textcells.h"

// The UTF-8 string trailer of a version 11 save (agi/saved_strings.h).
class AgiSavedStringsTestSuite : public CxxTest::TestSuite {
	enum { kCount = 24, kStride = 161 }; // MAX_STRINGS, MAX_STRINGBYTES

	struct Lcg {
		uint32 s;
		explicit Lcg(uint32 seed) : s(seed) {}
		uint32 below(uint32 n) { s = s * 1103515245u + 12345u; return (s >> 8) % n; }
	};

	static void randomTable(Lcg &r, char *table, Common::String *answers) {
		static const char *const pieces[] = { "\xEA\xB0\x80", "\xED\x9E\xA3", "a", "Z", " ", "\xC3\xA9", "\xF0\x9F\x98\x80" };
		memset(table, 0, kCount * kStride);
		for (uint i = 0; i < kCount; i++) {
			Common::String s, a;
			const uint n = r.below(40); // up to 39 cells, as setString()
			for (uint k = 0; k < n; k++)
				s += pieces[r.below(ARRAYSIZE(pieces))];
			s = Agi::TextCells::clipCells(s.c_str(), 39, true);
			memcpy(table + i * kStride, s.c_str(), s.size());
			if (r.below(3) == 0)
				for (uint k = 0; k < r.below(20); k++)
					a += pieces[r.below(4)];
			answers[i] = a;
		}
	}

public:
	void test_roundTripOfRandomTables() {
		Lcg r(11);
		uint bad = 0;
		for (uint round = 0; round < 2000; round++) {
			char table[kCount * kStride], back[kCount * kStride];
			Common::String answers[kCount], backAnswers[kCount];
			randomTable(r, table, answers);
			Common::MemoryWriteStreamDynamic out(DisposeAfterUse::YES);
			Agi::writeSavedStrings(out, table, kStride, kCount, answers);
			Common::MemoryReadStream in(out.getData(), out.size());
			memset(back, 0x55, sizeof(back));
			if (Agi::readSavedStrings(in, back, kStride, kCount, backAnswers) != Agi::kSavedStringsRead)
				bad++;
			for (uint i = 0; i < kCount; i++)
				if (strcmp(table + i * kStride, back + i * kStride) || answers[i] != backAnswers[i])
					bad++;
		}
		TS_ASSERT_EQUALS(bad, 0u);
	}

	// Every cut of a trailer, and random byte damage: never a partial
	// table, never a string that is not UTF-8, never a read past the end.
	void test_cutOrDamagedTrailersChangeNothingOrStayUtf8() {
		Lcg r(12);
		char table[kCount * kStride];
		Common::String answers[kCount];
		randomTable(r, table, answers);
		Common::MemoryWriteStreamDynamic out(DisposeAfterUse::YES);
		Agi::writeSavedStrings(out, table, kStride, kCount, answers);
		uint bad = 0;
		for (uint32 cut = 0; cut < out.size(); cut++) {
			char back[kCount * kStride];
			Common::String backAnswers[kCount];
			memset(back, 0x55, sizeof(back));
			Common::MemoryReadStream in(out.getData(), cut);
			if (Agi::readSavedStrings(in, back, kStride, kCount, backAnswers) == Agi::kSavedStringsRead)
				bad++;
			for (uint i = 0; i < sizeof(back); i++)
				if (back[i] != 0x55)
					bad++;
		}
		Common::Array<byte> bytes;
		for (uint32 i = 0; i < out.size(); i++)
			bytes.push_back(out.getData()[i]);
		for (uint round = 0; round < 20000; round++) {
			Common::Array<byte> d(bytes);
			for (uint k = 0; k < 1 + r.below(4); k++)
				d[r.below(d.size())] = r.below(256);
			char back[kCount * kStride];
			Common::String backAnswers[kCount];
			memset(back, 0, sizeof(back));
			Common::MemoryReadStream in(d.data(), d.size());
			if (Agi::readSavedStrings(in, back, kStride, kCount, backAnswers) == Agi::kSavedStringsRead)
				for (uint i = 0; i < kCount; i++)
					if (!Agi::TextCells::isValidUtf8(back + i * kStride) || strlen(back + i * kStride) >= kStride ||
					    !Agi::TextCells::isValidUtf8(backAnswers[i].c_str()))
						bad++;
		}
		TS_ASSERT_EQUALS(bad, 0u);
	}

	void test_noTrailerIsNone() {
		const byte upstreamTail[] = { 0x00, 0x00 }; // end of a version 11 save: nothing after AGIPal
		Common::MemoryReadStream in(upstreamTail, 0);
		char table[kCount * kStride];
		Common::String answers[kCount];
		TS_ASSERT_EQUALS(Agi::readSavedStrings(in, table, kStride, kCount, answers), Agi::kSavedStringsNone);
		Common::MemoryReadStream other(upstreamTail, sizeof(upstreamTail));
		TS_ASSERT_EQUALS(Agi::readSavedStrings(other, table, kStride, kCount, answers), Agi::kSavedStringsNone);
	}

	// The version 11 slot: the longest whole-character prefix of at most 39 bytes.
	void test_legacySlotCutsOnACharacter() {
		char slot[Agi::kLegacySlotBytes];
		Common::String ko;
		for (int i = 0; i < 20; i++)
			ko += "\xEA\xB0\x80";
		Agi::legacySlot(ko.c_str(), slot);
		TS_ASSERT_EQUALS(strlen(slot), 39u); // 13 syllables
		TS_ASSERT(Agi::TextCells::isValidUtf8(slot));
		Agi::legacySlot("abc", slot);
		TS_ASSERT_EQUALS(Common::String(slot), "abc");
		for (int i = 3; i < Agi::kLegacySlotBytes; i++)
			TS_ASSERT_EQUALS(slot[i], 0);
	}

	void test_legacyStringsBecomeUtf8() {
		char table[2 * kStride];
		memset(table, 0, sizeof(table));
		Common::strlcpy(table, "caf\x82", kStride); // CP850 e acute
		Common::strlcpy(table + kStride, "plain", kStride);
		Agi::convertLegacyStrings(table, kStride, 2, 39);
		TS_ASSERT_EQUALS(Common::String(table), "caf\xC3\xA9");
		TS_ASSERT_EQUALS(Common::String(table + kStride), "plain");
	}
};
```

