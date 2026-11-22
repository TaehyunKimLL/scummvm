# P3: SCI KQ1 end to end — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task by task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** DESIGN.md section 11 row P3: the SCI engine plays the KQ1 Korean pack. `Sci::TextMode` decides once how text is
held; `ScriptStrings` (`sci-ko.str`, with the fingerprint header of decision 4) translates script strings; text16 walks
UTF-8 code points through a `uint32` `GfxFont`; Hangul is drawn into a 2x `Graphics::TextPlane` that `GfxScreen` keeps in
step and `UpscaledGfxDriver` composites; the face resolver (`SciHiResText`) is the small rewrite of `jamo`'s `GfxCache`
code over P1's map/plan/font-set; the edit control, the built-in composer and the OS-IME surrogate key let a player type
Korean; `KoreanParser` (new unit) maps it onto the game's `said()` specs through P2's word table; detection offers a
`-ko` target; `kFormat` picks Korean particles and drops the plural "s". First task: install the engine's fallback map and
`NEODG16A.SVF` (P1 review m-1).

**Architecture:** new engine units hold the logic and are unit-tested without the engine (`textmode`, `utf8`,
`engine/script_strings`, `parser/korean_parser`, `korean_input`, `graphics/hirestext`); upstream SCI files get hooks only,
each behind `g_sci->getTextMode().utf8()` or a non-null hi-res object, so an English game runs the upstream code paths.
The SCI test suites link `engines/sci/libsci.a` into the CxxTest runner (as `jamo` did); an archive member is linked only
when a test uses it, so the tests pull no `g_sci` code.

**Tech stack:** C++11 ScummVM code, CxxTest (`make test`), Python 3 standard library for `tools/korean/strcheck.py`, Xvfb +
xdotool + ScummVM's own screenshot action for the headless smoke runs.

Source of scope: `docs/korean/DESIGN.md` section 11 row P3 (rows P2/P4 for what P3 must not do), sections 2.2, 3, 4, 5, 6,
7, 10, 12; `plans/P1-review.md` m-1; `plans/P2-common-korean.md` section 5 and `plans/P2-review.md` (the P2 fixes landed
before this plan was finished: base `d7f47c73a07`, 620 tests).

---

## 0. Facts checked for this plan (2026-11-22, this worktree)

Tree `/home/thkim/work/scummvm-2026.4-korean`, branch `ko-2026.4` at `d7f47c73a07` (P2 + its review fixes): non-engine
build `~/work/tmp/scummvm2026-p2verify` was 617 tests; after the P2 fixes the base is **620 tests** (`P2-fixes.md`).
`engines/` untouched so far.

**Prototype.** Every code block below was written and run before the plan:

- `~/work/tmp/scummvm2026-p3-tree/` = `git archive` of the tree plus every change of this plan (the P2 fix commits copied
  in), built out of tree in `~/work/tmp/scummvm2026-p3-treebuild` with
  `configure --disable-all-engines --enable-engine=sci --enable-debug --disable-optimizations`:
  `make test` → `Running cxxtest tests (666 tests)...OK!` (620 + 46 new); `make scummvm` links, no warning from a P3 file.
- `~/work/tmp/scummvm2026-p3-sci32/`: the same tree with `--enable-engine=sci,sci32` (the widened `GfxFont` is used by the
  SCI32 text code): links (`LINK scummvm`), the only warning seen was an unused variable in `kstring.cpp` that the plan's block no longer has.
- `~/work/tmp/scummvm2026-p3-proto/` holds the new files (`src/`, `test/`, `tools/`), the per-file diffs (`diff/per/`) the
  blocks below are cut from, and `jamo/` (the `jamo` sources read with `git show`).
- Headless runs of the real game (`~/work/tmp/p3-kq1/run1..run4`, script `go.sh`): with the harness pack
  `scummvm-game-kq1-0ab70f653d3-test.zip` (unzipped to `~/work/tmp/p3-kq1/zip/KQ1/GAMES/KQ1`) and `language=ko`, the title
  menu shows Hangul buttons (`프롤로그`, `게임시작`, `크레딧`, `이어서 계속하기`), the first room shows the Korean status
  line (`득점: 0 of 158  킹스 퀘스트 I`); typing `qhk` + Enter with the built-in composer gives `봐` in the prompt and the
  game answers with the Korean room description wrapped on two lines (`에드워드 왕의 성 바깥에 서 있다. 성은 큰뱀으로 /
  가득찬 해자로 둘러쌓여 있다.`); the same with `render_target=rgb565` (RGB screen, PNG mode `RGB`); with
  `originalsaveload=true`, F5 + `ans` + Enter writes `kq1-ko.001`, F7 lists `문` and Enter restores it. An English target
  on the same folder runs upstream (English title menu). Screens were viewed; they are not committed (game imagery).

Numbers the prototype measured, used as expected values:

| Fact | Value |
|---|---|
| KQ1 data on this host | `~/work/scummvm/gamedata/King's Quest 1 - Quest for the Crown (DOS 1991 Remake)/` (English DOS, matches `detection_tables.h:1701-1708`, resource.map 5,790 B, md5 of 5,000 B `7fe9399a0bec84ca5727309778d27f07`); the harness pack zip above (same game files + `ko/`) |
| KQ1 translation fingerprint (script 0 + vocab.000, this plan's definition) | `87ef5027c321e18e7b81a47bb584c098`; vocab= `4b7bd270cdc3a1ff3bcec6d56cc4662f` |
| Harness `ko/sci-ko.str` | 380 entries, no header (`strcheck.py check`: no other problem) |
| Harness `ko/sci-ko.dat` | version 2, 1,265 entries (P2); `k6bake.py build ... --game kq1sci --fingerprint 87ef... --vocab-md5 4b7b...` gives version 3, 21,742 B, sha256 `964bb09d694b804e4cb76421119eb23fc340c99a3ad106773875364616aeaecc`, loaded without a warning |
| Harness KQ1 maps (`ko/hiresvf.map`, `hiresbit.map`, `hiresttf.map`) | map version 2, refused (P1), so the engine falls back to `data:hires_text/maps/korean-default.map` (NEODG16A) |
| Upstream size of the change | 43 upstream files, 906 added lines (of which ~30 the mechanical `GfxFont` widening); 12 new engine files 1,709 lines; 6 test suites 763 lines; `strcheck.py` + test 176 lines |
| k16said KQ1 baseline (`jamo`, i18n `04d7371efa4`) | `~/work/scummvm/runs/walk/kq1-said-f1b.txt`: "335 variants of 76 commands: pass 303, partial 4, nomatch 1, unknown 2, treefail 0, english 25" (303 of 310 testable) |

ScummVM APIs used (file:line in this tree; every hook site was read):

| API / hook site | Where |
|---|---|
| `SciEngine::SciEngine` 88 (language `switch` ends 218), `run` 276, `_gfxScreen = new GfxScreen` 338, `getLanguage` 834 | `engines/sci/sci.cpp` |
| `getLanguage` 186, `kDebugLevelGame` 119, `strSplitLanguage` 282, `_resMan` 409 | `engines/sci/sci.h` |
| `debugFlagList` entry "Game" 60, `SciMetaEngineDetection::detectGames` 228, class 183, `s_sciGameTitles` 75 | `engines/sci/detection.cpp` |
| `AdvancedMetaEngineDetectionBase::identifyGame` 360 (reads `language` from ConfMan 365, filters entries by it 772), `AdvancedMetaEngineDetection<T>::identifyGame` 615 | `engines/advancedDetector.cpp`, `.h` |
| `SciEngine::strSplitLanguage` 361, `getSciLanguage` 302 (KO falls to English) | `engines/sci/engine/state.cpp` |
| `Script::identifyOffsets` 231, `offsetLookupArrayEntry` (`type`, `id`, `offset`, `stringSize`) 62, `getOffsetArray` 291, `getScriptNumber` 122 | `engines/sci/engine/script.cpp`, `.h` |
| `SegManager::strncpy(reg_t, const char *, size_t)` 694, `getString` 876, `getScriptIfLoaded` (.h 150) | `engines/sci/engine/seg_manager.cpp`, `.h` |
| `kStrCat` 41, `kStrCpy` 74, `kStrAt` 90, `kFormat` 216, `kStrLen` 442 | `engines/sci/engine/kstring.cpp` |
| `kTextSize` 345, `_k_GenericDrawControl` text split 918-923, `kNewWindow` title 1231-1232, `kDisplay` 1264 | `engines/sci/engine/kgraphics.cpp` |
| `kAddMenu` 32; `GfxMenu::kernelSetAttribute` `SCI_MENU_ATTRIBUTE_TEXT` 314 | `engines/sci/engine/kmenu.cpp`, `graphics/menu.cpp` |
| `EngineState::currentRoomNumber` 176 | `engines/sci/engine/state.h` |
| `GfxCache::getFont` 64 (`new GfxFontFromResource` 76) | `engines/sci/graphics/cache.cpp` |
| `GfxScreen::putPixel` (.h 256), `vectorPutPixel` (.h 317), ctor 42, driver created 147, `clearForRestoreGame` 225, `bitsGetDataSize` 521, `bitsSave` 547, `bitsRestore` 601 | `engines/sci/graphics/screen.h`, `.cpp` |
| `GfxPaint16::invertRectViaXOR` 204, `fillRect` 228 (invert branch 241-249) | `engines/sci/graphics/paint16.cpp` |
| `GfxDriver::driverBasedTextRendering` 67; `UpscaledGfxDriver` 90-124 (`_scaledBitmap` 117); `updateScreen` 155; `_currentPalette` 256*3 RGB (`default.cpp:149`) | `graphics/drivers/gfxdriver.h`, `gfxdriver_intern.h`, `upscaled.cpp` |
| driver table row `KO_KOR -> UpscaledGfx` 95; `getRenderMode` forces default for KO 121; `create` `requestRGB` 144 | `engines/sci/graphics/drivers/init.cpp` |
| `GfxText16::GetLongest` 198, `Width` 381, `Draw` 504, `DrawStatus` 703, `SwitchToFont1001OnKorean` 735 | `engines/sci/graphics/text16.cpp` |
| `GfxFont` 37-49 (all `uint16`); overriders `GfxFontFromResource` (.h 63-71, .cpp 264-323), `GfxFontSjis` (.h 43-45), `GfxFontKorean` (.h 43-48) | `engines/sci/graphics/scifont.h`, `fontsjis.*`, `fontkorean.*` |
| `GfxControls16::texteditCursorDraw` 124, `kernelTexteditChange` 160 | `engines/sci/graphics/controls16.cpp` |
| `EventManager::EventManager` 145, `input.character = ev.kbd.ascii` 317; `kSciKeyEtx` 59 ... `kSciKeyDelete` 76 | `engines/sci/event.cpp`, `.h` |
| `MetaEngine::initKeymaps` (metaengine.h 389); `Action::setCustomEngineActionEvent` (action.h 78); `EVENT_CUSTOM_ENGINE_ACTION_START` (events.h 89); SCI metaengine `buildEngineOptionsWidget` 203 | `engines/metaengine.h`, `backends/keymapper/action.h`, `common/events.h`, `engines/sci/metaengine.cpp` |
| `Vocabulary::Vocabulary` 32 (`loadAltInputs()` 72), `lookupWord` 452 (number check 521), `tokenizeString` 722; `parseGNF` (grammar.cpp 535); `said` (said.cpp 1006); `kParse` (kparse.cpp 87) | `engines/sci/parser/*` |
| `CXXTEST_RUNNING` guard (blocks the test build: `#if defined(X) && X` with `X` defined empty) | `engines/sci/util.h:135` |
| `isKoreanMessageMap` 3118 and the KO volume version 617: a `message.map` found through SearchMan with language KO is read as the Korean text map (the KQ1 pack's `ko/message.map` + `resource.msg`; no change needed) | `engines/sci/resource/resource.cpp` |
| `originalsaveload` key | `engines/sci/detection_options.h:107` |
| Save screenshot action `A+s` | `backends/graphics/sdl/sdl-graphics.cpp:513` |
| `install-data` 8, `dist-generic` 62; `DIST_FILES_ENGINEDATA` 438 (installed flat into `$(datadir)`) | `ports.mk`, `Makefile.common` |
| `computeStreamMD5AsString` 65; `parseBool` 240; `Array::insert_at(idx, const Array &)` 248 | `common/md5.h`, `common/util.h`, `common/array.h` |
| `ENABLE_TWINE` block 63 (the SCI block goes before it) | `test/module.mk` |

P0/P1/P2 interfaces P3 relies on (Task 0 re-greps them; nothing else of those phases is used):

| Interface | Where | Used by |
|---|---|---|
| `Graphics::TextPlane` (`create`, `exists`, `isEmpty`, `getRow`, `plot` via the font set, `clear`, `clearLowresRect`, `xorIndicesLowresRect`, `swapIndicesLowresRect`, `saveSize`/`save`/`restore`, `getIndex`/`getCoverage`) | `graphics/hires_text/text_plane.h` | GfxScreen, driver, tests |
| `Graphics::TextCompose::stampSpan`, `composeSpan`, `Mode` | `graphics/hires_text/text_compose.h` | `UpscaledGfxDriver::compositeText` |
| `HiResOptions`/`readHiResOptions`, `hiResTargetsOffered`, `resolveRenderTarget`, `resolveBlend`, `composeMode` | `graphics/hires_text/hires_options.h` | `SciHiResText` |
| `pickHiResMap`, `HiResPick`; `HiResFontMap::load`; `HiResMapQualifiers`; `HiResMap::layout.apply`, `faces`, `loadModeFor` | `map_pick.h`, `font_map.h` | `SciHiResText` |
| `compileIdPlan`; `HiResFaceCache::get`; `HiResFontSet::build/route/getAdvance/drawChar/getAscent/getDescent/getShift`; `SvfFace::getBpp` | `id_plan.h`, `font_set.h`, `svfface.h` | `SciHiResText`, `GfxFontHiRes` |
| `HiResFiles`, `HiResDiskFiles` (data roots: extrapaths, `DATA_PATH`, SearchMan) | `hires_files.h` | `SciHiResText`; test `HiResTest::MemoryFiles` (`test/graphics/hires_support.h`) |
| `Graphics::TextRun`, `TextDecoder`, `Utf8TextDecoder`, `LayoutMetrics`, `TextLayout::fitLine`, `LineSpan`, `BreakRules` | `text_layout.h` | `GfxText16::getLongestUtf8` |
| shipped `dists/engine-data/hires_text/NEODG16A.SVF`, `maps/korean-default.map` | P1 | fallback map, Task 1 |
| `Common::HangulComposer` (`isJamoKey`, `feed`, `backspace`, `text`, `empty`, `reset`), `resolveKoreanParticles` | `common/hangul.h` | `KoreanEditLine`, `kFormat` |
| `Common::koreanParticleRole`, `KoreanRole` | `common/korean_stem.h` | `Vocabulary::lookupKorean`, `KoreanParser` |
| `Common::KoreanVocabulary` (`load(path, magic, expected)`, `loadFromStream`, `isLoaded`, `getVersion`, `lookupAll`, `lookupJoined`, `kSequenceFlag`, `setDebugChannel`) | `common/korean_vocab.h` | `Vocabulary`, `KoreanParser` |
| `Common::TranslationFileHeader` (`parse`, `toString`, `present`), `checkTranslationHeader`, `kTranslationMismatch` | `common/translation_file.h` | `ScriptStrings`, `SciEngine::translationHeader` |
| `Common::translationFolderLanguages(files, "sci-%s.str")`, `translationTableName` | `common/language_folder.h` | detection, `textModeInputs`, `Vocabulary` |
| `Common::getLanguageFolderCode`, `parseLanguage` | `common/language.h` | SciEngine |
| `KEYCODE_HANGUL` and the keymapper name `HANGUL` | `common/keyboard.h`, `hardware-input.cpp` | the `HANGULTOGGLE` default binding |
| `test/korean_vocab_fixture.h` (`KoreanVocabFixture::build`) | P2 | `test/engines/sci/korean_parser.h` |
| `tools/korean/k6bake.py build ... --game --fingerprint --vocab-md5` | P2 | the version 3 KQ1 table of Task 19 |

The P2 review changed only behaviour P3 wants (BOM before the header is accepted, a version 2 table warns only when the game
names something to check, `k6bake.py` format 3 requires `--vocab-md5`); every call above was compiled and run against the
fixed tree.

## 1. Scope decisions and conflicts with DESIGN.md (listed again in the report)

1. **"EGA and VGA targets both render" (row P3).** KQ1 SCI exists only as a 16-colour EGA game (the detection entries are
   DOS EGA and Amiga; `ega320.drv`/`mcga320.drv` are both 16-colour). Read as the two screen kinds the design has for SCI
   (section 5.1 and the harness's `kq1kol`/`kq1ko` presets): the **8-bit palette screen** (`render_target=clut8`, the
   default with a 1 bpp face) and the **true-colour screen** (`render_target=rgb565`/`rgb888`). Both are smoke-tested
   (Tasks 15, 19). A VGA (256-colour) SCI game is first in P4 (LSL1 VGA).
2. **`k16said >= 303/310` needs the debug socket, which DESIGN puts in its own topic.** Section 10 and decision 8 make the
   socket an optional configure feature and the oracle, in topic `ko/07-debugsocket` (section 9), and row P3 does not list
   it. `k16said.py`/`k15parse.py` (harness) talk to the console's `parse`/`said`/`diskdump` over that socket. Resolution:
   P3 does not port the socket. P3 checks the parser in three ways it can run itself: the `KoreanParser` unit tests (Task 5,
   KQ1-shaped arrangements), the in-play typed command through the real edit control and `said()` (Task 19, `봐` ->
   the room description), and an acceptance step that is **run once the socket topic exists** (Task 20: the exact
   `k16said.py` command and the 303 bar). The row's criterion is therefore reported as "pending ko/07" until then.
3. **"first room screenshot matches reference."** No reference exists for the new tree: `jamo`'s screenshots use the v2
   non-OFL faces (HTERM16 ...), which decision 3 keeps out of the fork, so pixels cannot match. Resolution: Task 19 makes
   the reference with this tree (default map, NEODG16A), a person or agent views it, its sha256 goes into `P3-notes.md`, and
   later runs compare against it (`cmp`), on CLUT8 and RGB565. The PNGs stay outside the repository (game imagery).
4. **The KQ1 pack's maps are version 2** (harness `dos/release/kq1/HIRES*.MAP`, non-OFL faces) and are refused by P1's
   parser; converting and re-pairing them is harness data work (decision 3, the owner picks faces). P3 adds the engine's
   fallback: when the game has no loadable map, `SciHiResText` uses `data:hires_text/maps/korean-default.map` (every
   font id NEODG16A 16 px; SCI's control-range glyphs, the list arrows, stay the game's: `GfxFontHiRes::isOriginal`).
   DESIGN 5.2 already names NEODG16A as the last face of every chain; the fallback *map* is this plan's addition.
5. **Install of the fallback data (P1 review m-1) cannot go through `engine_data.mk`.** `DIST_FILES_ENGINEDATA` is
   installed flat into `$(datadir)` (`ports.mk:18`), which would put `NEODG16A.SVF` where `data:hires_text/NEODG16A.SVF`
   does not look. Task 1 adds a `DIST_FILES_HIRES_TEXT(_MAPS)` pair in `Makefile.common` installed into
   `$(datadir)/hires_text/{,maps/}` by `install-data` (like the shaders). `dists/scummvm.rc` (Windows resources), the
   `dist-generic`, macOS and Android bundles are **not** done in P3: no Windows/macOS/Android run is possible here and the
   Win32 resource archive's handling of a `hires_text/` name is UNVERIFIED; until then such builds need `extrapath` or the
   game's own map with faces in `ko/`. Listed for P8 (packaging).
6. **`hires_text_scale` for SCI is fixed at 2.** The upscaled driver is 2x (`upscaled.cpp:29-36`); `SciHiResText::kScale`.
   A `hires_text_scale=1` is ignored for SCI (the key stays for SCUMM/AGI).
7. **Hi-res text is drawn for Korean targets only.** The driver that can composite is chosen by the existing
   `init.cpp:95` row (`KO_KOR` -> `UpscaledGfx`). `jamo`'s extra `hiresText` table column for other languages is not
   ported (no other language in this round, decision 11). `TextMode` says so in a note when `text_encoding=utf8` is forced
   on another language.
8. **`render_target=rgb565` vs `rgb888`.** The SCI driver asks the backend for "RGB" (`initGraphics(..., nullptr)`), not a
   given format; P3 forces RGB on or off (`init.cpp` `requestRGB`), the exact format is the backend's (565 or 8888 both
   composite). Pinning the format is not in P3.
9. **What `language=ko` means to the engine.** `SciEngine::getLanguage()` returns `TextMode::language`: the target's
   language when the game folder holds a translation for it, else the detection entry's. A Korean target therefore runs
   upstream's KO paths that the pack needs (`ko/message.map` read as the Korean text map, `resource.cpp:617/3118`; the
   upscaled driver row) and P3 guards the one KO path it must not take (`SwitchToFont1001OnKorean`, font 1001 needs
   `korean.fnt`). Detection: `identifyGame` matches the release with the language left out when the folder has that
   translation (otherwise `language=ko` makes the advanced detector drop the English entry, `advancedDetector.cpp:772`,
   and the game falls to fallback detection, which is what `jamo` ran on).
10. **Fingerprint definition (decision 4 left it to the engine; P2 section 1 item 4).** SCI: md5 of the decompressed data
    of script 0 followed by the main vocabulary (vocab 0, else vocab 900; nothing for a game without one); `vocab=` is the
    md5 of that vocabulary alone. Computed through the resource manager, so patches count. The engine prints the expected
    header line when a table has none (that is how a packer gets it); `tools/korean/strcheck.py header` writes it.
11. **`strcheck.py` (decision 7)** is in P3 as P2 section 4 said ("arrives with the `.str` header in P3"), reduced to what
    needs no game data: header syntax, key syntax, duplicates, and setting the header. "Every key resolvable in the dumped
    script strings" needs a script dump and stays in the harness.
12. **Key names (section 7, decision 5).** `hangul_input` (auto: on when a word table loaded), `hangul_builtin` (true),
    `hangul_start` (true), `text_encoding` (auto|utf8|cp949), `korean_text` (true), `hires_text*`, `render_target`,
    `hangul_break` are read. No GUI widget (P8), no `--set` (P7/P8).
13. **Edit control rewritten small.** `jamo`'s +449 lines in `controls16.cpp` become `KoreanEditLine` (engine-free, 263
    lines, 7 tests) and a 55-line `texteditChangeUtf8` in `controls16.cpp`; upstream's byte editor is untouched for every
    other game. The pre-edit syllable is written into the line as it grows (no separate pre-edit drawing, as in `jamo`); the
    KO/EN badge (`graphics/korean_badge`) is not ported (P8 has the GUI).
14. **Left to P4 on purpose** (SCI data games): `sci11StringsStart` is ported and tested in `ScriptStrings` but not hooked
    into `Script::identifyOffsets` (it shifts SCI1.1 string ids; KQ1 is SCI01); `koreanAnswerView` and other per-game
    tables; the `kNodeValue` KQ1 room 14 workaround (not Korean); growing a DYNMEM buffer for a longer translation (P3
    cuts at a code point boundary with a warning, `SegManager::strncpy`); `|code|` font changes inside a UTF-8 line
    (`getLongestUtf8` treats a code as zero width).
15. **Greyed text** (`greyedOutput`) is drawn normal in hi-res (disabled menu items keep their colour); `jamo` did the
    same. Noted, not fixed.
16. **The old fork's debug dumps, `Text.MAP` overlay, CP949 in-place path, banked fonts, lazy audio maps**: not ported
    (decision 10, DESIGN 2.2 verdict). The harness pack's `ko/korean.uni` is not read.
17. **Upstream files touched by P3:** 43 (section 2), all in `engines/sci` plus `Makefile.common`, `ports.mk`,
    `test/module.mk`. DESIGN asks for a small hook-based diff; the largest hunks are `kstring.cpp` (the `kStrAt` byte
    access moved into a helper so the UTF-8 path can reuse it, +121), `text16.cpp` (+101: four decode loops and the layout
    adapter), `sci.cpp` (+96: text mode, fingerprint, hi-res setup), `controls16.cpp` (+70), `vocabulary.cpp` (+63). Each is
    listed in `PATCHES.md` (Task 21).
18. Branch: commit on `ko-2026.4` directly, as P0-P2 did, unless the owner names topic `ko/03-sci`.

## 2. Files

| File | New/Mod | Purpose |
|---|---|---|
| `Makefile.common`, `ports.mk` | mod | install `hires_text/` (Task 1) |
| `dists/engine-data/hires_text/maps/korean-default.map` | mod (comment) | says where the face is found once installed |
| `engines/sci/util.h` | mod (1 line) | `#ifdef CXXTEST_RUNNING` so the SCI test build compiles |
| `test/module.mk` | mod | SCI suites, `libsci.a` |
| `engines/sci/module.mk` | mod | 6 objects |
| `engines/sci/textmode.{h,cpp}` | new | `TextMode`, `TextModeInputs`, `decideTextMode` |
| `engines/sci/utf8.{h,cpp}` | port (`jamo`, code-page functions left out) | UTF-8 decode/encode, index <-> offset |
| `engines/sci/engine/script_strings.{h,cpp}` | port (`jamo` `translation.*`) + header | `ScriptStrings` |
| `engines/sci/parser/korean_parser.{h,cpp}` | new (code moved out of `jamo` `vocabulary.cpp`) | `KoreanParser` |
| `engines/sci/korean_input.{h,cpp}` | new (rewrite of `jamo` `koreaninput` + edit control logic) | `KoreanEditLine` |
| `engines/sci/graphics/hirestext.{h,cpp}` | new (replaces `jamo` `GfxCache` face code, fontset, hirestextsettings/state) | `SciHiResText`, `GfxFontHiRes` |
| `engines/sci/graphics/{scifont,fontsjis,fontkorean}.{h,cpp}` | mod | `GfxFont` takes `uint32` |
| `engines/sci/sci.{h,cpp}`, `detection.cpp` | mod | text mode, language folder, fingerprint, hi-res setup, debug channel `Korean`, `-ko` variants |
| `engines/sci/engine/{script,seg_manager}.{h,cpp}`, `state.cpp` | mod | string keys, bounded UTF-8 copies, `translated`/`keyOf`/`strSplitHeap` |
| `engines/sci/engine/kstring.cpp`, `kgraphics.cpp`, `kmenu.cpp`, `graphics/menu.cpp` | mod | UTF-8 heap, translation at display, particles |
| `engines/sci/graphics/drivers/{gfxdriver.h,gfxdriver_intern.h,upscaled.cpp,init.cpp}` | mod | text plane compositing, RGB request |
| `engines/sci/graphics/{screen.h,screen.cpp,paint16.cpp,cache.h,cache.cpp}` | mod | plane in step, `GfxFontHiRes` |
| `engines/sci/graphics/{text16.h,text16.cpp}` | mod | code points, `getLongestUtf8` |
| `engines/sci/parser/{vocabulary.h,vocabulary.cpp,grammar.cpp,said.cpp}` | mod | word table, arrangement, fallback tree |
| `engines/sci/{event.h,event.cpp,metaengine.cpp}`, `graphics/{controls16.h,controls16.cpp}` | mod | Han/Yeong, OS-IME surrogate, keymap, edit control |
| `test/engines/sci/{textmode,utf8,script_strings,korean_parser,korean_input,hirestext}.h` | new (utf8 ported) | 46 tests |
| `tools/korean/strcheck.py`, `tools/korean/test_strcheck.py` | new | table check and header (4 tests) |
| `docs/korean/PATCHES.md` | mod | `ko/03-sci` rows |
| `docs/korean/plans/P3-notes.md` | new (last task) | deviations, reference hashes, acceptance |

Style for every new file: the GPL header of `engines/sci/sci.cpp:1-20` (the code blocks omit it; `test/engines/sci/*.h`
have none, as upstream `test/`), tabs, `namespace Sci {` ... `} // End of namespace Sci`, `_camelCase` members. Ported
files keep `jamo`'s text except the edits named in their task.

## 3. Environment (every task)

```sh
SRC=/home/thkim/work/scummvm-2026.4-korean
OLD=/home/thkim/work/scummvm-korean            # read only: git -C $OLD show jamo:<path>; never check out
HAR=/home/thkim/work/scummvm/harness           # read only
BLD=~/work/tmp/scummvm2026-p3build             # out of tree, SCI enabled; never configure inside $SRC
PACK=~/work/tmp/p3-kq1/pack                    # scratch copy of a KQ1 Korean pack (game data: never into the repo)
mkdir -p $BLD && cd $BLD
heavy $SRC/configure --disable-all-engines --enable-engine=sci --enable-debug --disable-optimizations
# configure prints "Engines (builtin):\n    SCI [SCI 0-1.1 games]"
```

Build and test: `cd $BLD && heavy make -j8 test` (last line `...OK!`), `heavy make -j8 scummvm` (links `scummvm`). A
single suite: `./test/runner SciTextModeTestSuite`. Commit messages: `AREA: Subject` (e.g. `SCI: ...`, `TEST: ...`,
`TOOLS: Korean: ...`, `DOCS: Korean: ...`), a body, exactly one trailing line `Assisted-by: Claude Code`, no
`Co-Authored-By`; check each with `git log -1 --format=%B > /tmp/msg-$$ && python3 devtools/check-commit-msg.py
/tmp/msg-$$` (`TMPDIR` under `~/work/tmp` if you prefer: memory `tmp-dir-convention`).

The headless runner used by Tasks 15 and 19 (scratch, not committed): `~/work/tmp/p3-kq1/go.sh`

```sh
#!/bin/bash
# go.sh OUTDIR TARGET SECONDS [step...]: run scummvm under Xvfb with OUTDIR/run.ini, do the steps, take a screenshot.
# Steps: sleep:N, type:TEXT, click:X,Y (window coordinates), or an xdotool key name (Return, F5, alt+s).
set -u
OUT=$1; TARGET=$2; SECS=$3; shift 3
BIN=${BIN:-$HOME/work/tmp/scummvm2026-p3build/scummvm}
export SDL_AUDIODRIVER=dummy
DISP=:$(( 400 + RANDOM % 300 ))
Xvfb $DISP -screen 0 800x600x24 >/dev/null 2>&1 & XV=$!
sleep 3
DISPLAY=$DISP $BIN -c $OUT/run.ini --gfx-mode=${GFX:-surfacesdl} --scale-factor=1 --no-aspect-ratio -d1 --debugflags=Korean \
	$TARGET > $OUT/run.log 2>&1 & G=$!
sleep $SECS
WID=$(DISPLAY=$DISP xdotool search --pid $G 2>/dev/null | tail -1)
for k in "$@"; do
  case "$k" in
    sleep:*) sleep ${k#sleep:};;
    type:*) DISPLAY=$DISP xdotool type --delay 150 "${k#type:}";;
    click:*) xy=${k#click:}; DISPLAY=$DISP xdotool mousemove --window $WID ${xy%,*} ${xy#*,} click 1;;
    *) DISPLAY=$DISP xdotool key --delay 100 "$k";;
  esac
done
DISPLAY=$DISP xdotool key alt+s; sleep 2
kill $G; sleep 1; kill $XV
```

Observed on this host: `surfacesdl` sometimes dies at start under Xvfb with `SDL_BlitSurface failed` and a zenity box
(seen for 640x480 windows and once for the Korean target); rerun, or use `GFX=opengl` (then the window is 640x480 with
the 640x400 game letterboxed: add 40 to every `click:` y). The pack the runs use:

```sh
mkdir -p ~/work/tmp/p3-kq1/zip && cd ~/work/tmp/p3-kq1/zip
unzip -q -o ~/work/scummvm/dist/scummvm-game-kq1-0ab70f653d3-test.zip 'KQ1/GAMES/KQ1/*'
rm -rf $PACK && cp -r ~/work/tmp/p3-kq1/zip/KQ1/GAMES/KQ1 $PACK       # RESOURCE.00[1-3], RESOURCE.MAP, ko/...
```

`run.ini` for a run directory `$R` (`[kq1-ko]` is the Korean target, `[kq1]` the English one):

```ini
[scummvm]
extrapath=/home/thkim/work/scummvm-2026.4-korean/dists/engine-data
autosave_period=0
screenshotpath=$R

[kq1-ko]
engineid=sci
gameid=kq1sci
language=ko
path=$HOME/work/tmp/p3-kq1/pack
savepath=$R
```

(write `$R` and `$HOME` out; ScummVM does not expand them).

---

## Task 0: Base and interfaces

**Files:** none (checks only).

- [ ] **Step 1: base** — `git -C $SRC log --oneline -1` shows `d7f47c73a07` or a later commit whose
  `git diff d7f47c73a07.. -- engines` is empty. Configure `$BLD` (section 3), `heavy make -j8 test`: expected
  `Running cxxtest tests (620 tests)...OK!` (call this N0; every later count below is N0 + the new tests).
- [ ] **Step 2: interfaces** — each command prints at least one line:

```sh
cd $SRC
grep -n "bool create(int lowW, int lowH, int scale)" graphics/hires_text/text_plane.h
grep -n "void stampSpan\|void composeSpan" graphics/hires_text/text_compose.h
grep -n "HiResOptions readHiResOptions\|uint32 hiResTargetsOffered\|HiResRenderTarget resolveRenderTarget\|TextCompose::Mode composeMode" graphics/hires_text/hires_options.h
grep -n "bool pickHiResMap" graphics/hires_text/map_pick.h
grep -n "bool build(const HiResIdPlan\|int route(uint32\|int drawChar(uint32" graphics/hires_text/font_set.h
grep -n "LineSpan fitLine" graphics/hires_text/text_layout.h
grep -n "uint lookupAll\|bool lookupJoined\|kSequenceFlag = 0x8000" common/korean_vocab.h
grep -n "static bool parse(const String &line\|TranslationCheck checkTranslationHeader" common/translation_file.h
grep -n "Array<Language> translationFolderLanguages\|String translationTableName" common/language_folder.h
grep -n "bool koreanParticleRole" common/korean_stem.h
grep -n "void resolveKoreanParticles\|static bool isJamoKey" common/hangul.h
ls dists/engine-data/hires_text/NEODG16A.SVF dists/engine-data/hires_text/maps/korean-default.map
```

- [ ] **Step 3: data** — `ls "$HOME/work/scummvm/gamedata/King's Quest 1 - Quest for the Crown (DOS 1991 Remake)/resource.map"`
  and `ls ~/work/scummvm/dist/scummvm-game-kq1-0ab70f653d3-test.zip`. When either is missing, the tasks still run; only the
  steps marked **[data]** are skipped and reported (section "Acceptance").

No commit.

---

## Task 1: Install the fallback map and NEODG16A.SVF (P1 review m-1)

**Files:** `Makefile.common`, `ports.mk`, `dists/engine-data/hires_text/maps/korean-default.map`.

- [ ] **Step 1: red** — `cd $BLD && make install-data DESTDIR=$BLD/inst >/dev/null && find $BLD/inst -path '*hires_text*' | wc -l`
  prints `0`.
- [ ] **Step 2: Makefile.common** (after `DIST_FILES_ENGINEDATA_BIG`):

```diff
--- a/Makefile.common
+++ b/Makefile.common
@@ -438,6 +438,10 @@
 DIST_FILES_ENGINEDATA:=$(addprefix $(srcdir)/,$(DIST_FILES_ENGINEDATA_BASE_CORE) $(DIST_FILES_ENGINEDATA_BASE))
 DIST_FILES_ENGINEDATA_BIG:=$(addprefix $(srcdir)/,$(DIST_FILES_ENGINEDATA_BASE_BIG))
 
+# Hi-res text (Korean fork): the engine's fallback map and its OFL face, installed into hires_text/ of the data folder
+DIST_FILES_HIRES_TEXT:=$(addprefix $(srcdir)/dists/engine-data/hires_text/,NEODG16A.SVF NEODGM.TXT)
+DIST_FILES_HIRES_TEXT_MAPS:=$(srcdir)/dists/engine-data/hires_text/maps/korean-default.map
+
 # Shaders: install if USE_OPENGL_SHADERS is defined
 DIST_FILES_SHADERS=
 ifneq ($(USE_OPENGL_SHADERS),)
```

- [ ] **Step 3: ports.mk** (`install-data`, before the shaders):

```diff
--- a/ports.mk
+++ b/ports.mk
@@ -20,6 +20,9 @@
 	$(INSTALL) -c -m 644 "$(srcdir)/dists/org.scummvm.scummvm.desktop" "$(DESTDIR)$(datarootdir)/applications/org.scummvm.scummvm.desktop"
 	$(INSTALL) -d "$(DESTDIR)$(datarootdir)/metainfo"
 	$(INSTALL) -c -m 644 "$(srcdir)/dists/org.scummvm.scummvm.metainfo.xml" "$(DESTDIR)$(datarootdir)/metainfo/org.scummvm.scummvm.metainfo.xml"
+	$(INSTALL) -d "$(DESTDIR)$(datadir)/hires_text/maps"
+	$(INSTALL) -c -m 644 $(DIST_FILES_HIRES_TEXT) "$(DESTDIR)$(datadir)/hires_text"
+	$(INSTALL) -c -m 644 $(DIST_FILES_HIRES_TEXT_MAPS) "$(DESTDIR)$(datadir)/hires_text/maps"
 ifneq ($(DIST_FILES_SHADERS),)
 	$(INSTALL) -d "$(DESTDIR)$(datadir)/shaders"
 	$(INSTALL) -c -m 644 $(DIST_FILES_SHADERS) "$(DESTDIR)$(datadir)/shaders"
```

- [ ] **Step 4: the map's comment** — replace its lines 5-7 ("; game's ko/ folder ... ; extrapath at dists/engine-data.") by:

```
; game's ko/ folder under that name. "data:" is looked up in the extrapaths, then the ScummVM
; data folder: `make install` puts this map and the face into its hires_text/ folder; a build
; run from its source tree finds them with extrapath=dists/engine-data.
```

- [ ] **Step 5: green** — `rm -rf $BLD/inst && make install-data DESTDIR=$BLD/inst >/dev/null && find $BLD/inst -path '*hires_text*' -type f | sort`:

```
.../share/scummvm/hires_text/maps/korean-default.map
.../share/scummvm/hires_text/NEODG16A.SVF
.../share/scummvm/hires_text/NEODGM.TXT
```

  `heavy make -j8 test`: N0 tests OK (the shipped-maps test still loads the map).
- [ ] **Step 6: commit** — `git add Makefile.common ports.mk dists/engine-data/hires_text/maps/korean-default.map`;
  message `DISTS: Install the hi-res text fallback map and face` / body: "make install puts dists/engine-data/hires_text
  into the data folder's hires_text/, where data:hires_text/... looks (the engine data list is installed flat). Bundles and
  the Windows resources are not covered yet." / `Assisted-by: Claude Code`.

---

## Task 2: SCI test suites and `TextMode`

**Files:** `engines/sci/util.h`, `test/module.mk`, `engines/sci/module.mk`, new `engines/sci/textmode.{h,cpp}`,
`test/engines/sci/textmode.h`.

- [ ] **Step 1: test** — `test/engines/sci/textmode.h`:

```cpp
#include <cxxtest/TestSuite.h>

#include "sci/textmode.h"

class SciTextModeTestSuite : public CxxTest::TestSuite {
	static Sci::TextModeInputs koreanPack() {
		Sci::TextModeInputs in;
		in.detected = Common::EN_ANY;
		in.requested = Common::KO_KOR;
		in.translationFolder = true;
		in.stringTable = true;
		in.version = Sci::SCI_VERSION_01;
		return in;
	}

public:
	void test_koreanPackIsUtf8AndHires() {
		Common::String notes;
		const Sci::TextMode m = Sci::decideTextMode(koreanPack(), notes);
		TS_ASSERT_EQUALS(m.encoding, Sci::TextMode::kUtf8);
		TS_ASSERT(m.hires);
		TS_ASSERT_EQUALS(m.language, Common::KO_KOR);
		TS_ASSERT(notes.empty());
	}

	void test_englishGameUntouched() {
		Sci::TextModeInputs in = koreanPack();
		in.requested = Common::UNK_LANG;
		Common::String notes;
		const Sci::TextMode m = Sci::decideTextMode(in, notes);
		TS_ASSERT_EQUALS(m.encoding, Sci::TextMode::kSingle);
		TS_ASSERT(!m.hires);
		TS_ASSERT_EQUALS(m.language, Common::EN_ANY);
	}

	void test_languageWithoutTranslationFolder() {
		Sci::TextModeInputs in = koreanPack();
		in.translationFolder = false;
		in.stringTable = false;
		Common::String notes;
		const Sci::TextMode m = Sci::decideTextMode(in, notes);
		TS_ASSERT_EQUALS(m.language, Common::EN_ANY);
		TS_ASSERT_EQUALS(m.encoding, Sci::TextMode::kSingle);
		TS_ASSERT(notes.contains("no translation"));
	}

	void test_upstreamCp949PackKeepsItsPath() {
		Sci::TextModeInputs in;
		in.detected = Common::KO_KOR;
		in.version = Sci::SCI_VERSION_01;
		Common::String notes;
		const Sci::TextMode m = Sci::decideTextMode(in, notes);
		TS_ASSERT_EQUALS(m.encoding, Sci::TextMode::kCp949);
		TS_ASSERT(!m.hires);
		TS_ASSERT_EQUALS(m.language, Common::KO_KOR);
	}

	void test_overrides() {
		Sci::TextModeInputs in = koreanPack();
		Common::String notes;
		in.textEncoding = " CP949 ";
		TS_ASSERT_EQUALS(Sci::decideTextMode(in, notes).encoding, Sci::TextMode::kCp949);
		in.textEncoding = "latin1";
		TS_ASSERT_EQUALS(Sci::decideTextMode(in, notes).encoding, Sci::TextMode::kUtf8);
		TS_ASSERT(notes.contains("text_encoding=latin1"));
		in.textEncoding = "";
		in.hiresText = false;
		Sci::TextMode m = Sci::decideTextMode(in, notes);
		TS_ASSERT(m.utf8() && !m.hires);
		in.hiresText = true;
		in.koreanText = false;
		m = Sci::decideTextMode(in, notes);
		TS_ASSERT_EQUALS(m.encoding, Sci::TextMode::kSingle);
		TS_ASSERT_EQUALS(m.language, Common::EN_ANY);
	}

	void test_sci32HasNoHiresText() {
		Sci::TextModeInputs in = koreanPack();
		in.version = Sci::SCI_VERSION_2;
		Common::String notes;
		const Sci::TextMode m = Sci::decideTextMode(in, notes);
		TS_ASSERT(m.utf8() && !m.hires);
	}
};
```

- [ ] **Step 2: link the SCI suites** — `test/module.mk`:

```diff
--- a/test/module.mk
+++ b/test/module.mk
@@ -60,6 +60,11 @@
 	TEST_LIBS += engines/ultima/libultima.a
 endif
 
+ifeq ($(ENABLE_SCI), STATIC_PLUGIN)
+	TESTS += $(srcdir)/test/engines/sci/*.h
+	TEST_LIBS += engines/sci/libsci.a graphics/libgraphics.a common/libcommon.a
+endif
+
 ifeq ($(ENABLE_TWINE), STATIC_PLUGIN)
 	TESTS += $(srcdir)/test/engines/twine/*.h
 	TEST_LIBS += engines/twine/libtwine.a
```

  `engines/sci/util.h:135` blocks the test build (the runner defines `CXXTEST_RUNNING` empty, so `#if defined(X) && X`
  is a preprocessor error, "operator '&&' has no right operand"):

```diff
--- a/engines/sci/util.h
+++ b/engines/sci/util.h
@@ -132,7 +132,7 @@
 	typedef Derived<ValueType> derived_type;
 
 	template <typename T, template <typename> class U> friend class SciSpanImpl;
-#if defined(CXXTEST_RUNNING) && CXXTEST_RUNNING
+#ifdef CXXTEST_RUNNING
 	friend class ::SpanTestSuite;
 #endif
 
```

- [ ] **Step 3: red** — `heavy make -j8 test`: compile error `sci/textmode.h: No such file or directory`.
- [ ] **Step 4: implement** — `engines/sci/textmode.h`:

```cpp
#ifndef SCI_TEXTMODE_H
#define SCI_TEXTMODE_H

#include "common/language.h"
#include "common/str.h"

#include "sci/detection.h"

namespace Sci {

/**
 * How a game's text is held and drawn, decided once at engine start (DESIGN.md
 * section 3 policy 1). Upstream behaviour is kept whenever encoding is not kUtf8.
 */
struct TextMode {
	enum Encoding {
		kSingle, ///< the game's own single-byte text (upstream)
		kCp949,  ///< an upstream CP949 fan pack: font 1001, nothing of the fork
		kUtf8    ///< a fork translation: UTF-8 heap, ScriptStrings, hi-res text, Korean input
	};

	TextMode() : encoding(kSingle), hires(false), language(Common::UNK_LANG) {}

	Encoding encoding;
	bool hires;                 ///< text drawn into the 2x text plane
	Common::Language language;  ///< what SciEngine::getLanguage() answers

	bool utf8() const { return encoding == kUtf8; }
};

/** What decideTextMode() looks at; filled from the descriptor, ConfMan and the game folder. */
struct TextModeInputs {
	TextModeInputs() : detected(Common::UNK_LANG), requested(Common::UNK_LANG), koreanText(true),
		translationFolder(false), stringTable(false), hiresText(true), version(SCI_VERSION_NONE) {}

	Common::Language detected;   ///< the detection entry's language
	Common::Language requested;  ///< the target's language= (UNK_LANG: not set or unknown)
	bool koreanText;             ///< korean_text ([scummvm] off switch)
	Common::String textEncoding; ///< text_encoding: auto (or empty), utf8, cp949
	bool translationFolder;      ///< the requested language's folder (ko/) holds a translation
	bool stringTable;            ///< that folder (or the game folder) holds sci-<code>.str
	bool hiresText;              ///< hires_text
	SciVersion version;
};

/** The text mode for @p in; @p notes gets one line per decision a user may wonder about. */
TextMode decideTextMode(const TextModeInputs &in, Common::String &notes);

} // End of namespace Sci

#endif
```

  `engines/sci/textmode.cpp`:

```cpp
#include "sci/textmode.h"

namespace Sci {

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

	const bool korean = mode.language == Common::KO_KOR;
	Common::String enc = in.textEncoding;
	enc.trim();
	enc.toLowercase();
	if (!enc.empty() && enc != "auto" && enc != "utf8" && enc != "utf-8" && enc != "cp949") {
		notes += Common::String::format("text_encoding=%s is not auto, utf8 or cp949; auto used; ", in.textEncoding.c_str());
		enc = "auto";
	}

	if (!in.koreanText) {
		// The off switch keeps upstream exactly: a CP949 fan entry stays CP949.
		mode.encoding = (korean && in.detected == Common::KO_KOR) ? TextMode::kCp949 : TextMode::kSingle;
		if (mode.language != in.detected)
			mode.language = in.detected;
		notes += "korean_text=false; ";
		return mode;
	}

	if (enc == "utf8" || enc == "utf-8")
		mode.encoding = TextMode::kUtf8;
	else if (enc == "cp949")
		mode.encoding = TextMode::kCp949;
	else if (korean && in.stringTable)
		mode.encoding = TextMode::kUtf8;
	else if (korean)
		mode.encoding = TextMode::kCp949;
	else
		mode.encoding = TextMode::kSingle;

	if (mode.utf8()) {
		if (in.version < SCI_VERSION_0_EARLY || in.version > SCI_VERSION_1_1)
			notes += "hi-res text needs SCI0 to SCI1.1; ";
		else if (!in.hiresText)
			notes += "hires_text=false; ";
		else if (!korean)
			notes += "hi-res text is drawn for Korean only (the upscaled driver row); ";
		else
			mode.hires = true;
	}
	return mode;
}

} // End of namespace Sci
```

  `engines/sci/module.mk`: add `textmode.o \` after `sci.o \` (the full list of this phase's objects is in Task 8 Step 4).
- [ ] **Step 5: green** — `heavy make -j8 test`: N0 + 6 tests OK; `./test/runner SciTextModeTestSuite`: 6 tests OK.
- [ ] **Step 6: commit** — `SCI: Add TextMode, how a game's text is held and drawn` (body: the decision of DESIGN.md
  section 3 policy 1 in one place; upstream unchanged unless a translation is in the language folder; the SCI suites link
  libsci.a; util.h guard).

---

## Task 3: UTF-8 helpers (port)

**Files:** new `engines/sci/utf8.{h,cpp}`, `test/engines/sci/utf8.h` (from `jamo`).

- [ ] **Step 1: test** — `git -C $OLD show jamo:test/engines/sci/utf8.h | sed -n '22,$p' > test/engines/sci/utf8.h`
  (drops `jamo`'s licence comment lines 1-21; 15 tests; none uses the code-page functions).
- [ ] **Step 2: red** — `heavy make -j8 test`: `sci/utf8.h: No such file or directory`.
- [ ] **Step 3: implement** — `jamo`'s `engines/sci/utf8.{h,cpp}` without `decodeCodePagePair`, `encodeCodePagePair`,
  `koreanBankAndSlot` (they need `jamo`'s `codepage_kr`, and the CP949 path is not ported: decision 10). Header:

```cpp
#ifndef SCI_UTF8_H
#define SCI_UTF8_H

#include "common/scummsys.h"

namespace Sci {

/**
 * Decode one UTF-8 sequence at @p p.
 *
 * Returns the code point and sets @p outBytes to the number of bytes
 * consumed, 1..4. A byte that does not start a well-formed sequence - a
 * stray continuation byte, an over-long form, a truncated tail - is returned
 * as itself with @p outBytes = 1, so a walk always advances and a corrupt
 * byte becomes one visible wrong glyph rather than a hang or a skip.
 *
 * This is the single decoder for SCI text once the TextMode is kUtf8: the
 * kernel string ops count with it and GfxText16 walks with it, so "how
 * many characters" and "where does the next one start" cannot disagree.
 *
 * Kept apart from util.h on purpose: this is a pure function with no
 * dependency on the engine, and it is unit-tested, which util.h's SciSpan
 * friend declarations make awkward.
 */
uint32 decodeUtf8Char(const byte *p, int &outBytes);

/** Number of code points in a NUL-terminated UTF-8 string. */
uint32 utf8Length(const byte *p);

/**
 * Encode @p codePoint (up to U+10FFFF; larger values become U+FFFD) as
 * UTF-8 into @p out, which has room for 4 bytes. Returns the byte count.
 */
int encodeUtf8Char(uint32 codePoint, byte *out);

/**
 * Where a write of the @p index-th code point lands in a UTF-8 string that
 * is being built left to right: the byte offset of that code point, and
 * past the terminator one byte per missing index (so an ASCII string keeps
 * the byte-for-index answer it always had).
 */
uint32 utf8WriteOffset(const byte *p, uint32 index);

/**
 * Byte offset of the @p index-th code point in a NUL-terminated UTF-8
 * string, or the offset of the terminator when @p index is past the end.
 */
uint32 utf8OffsetOf(const byte *p, uint32 index);

/**
 * Whether @p index lies beyond both the character count and the byte length
 * of the NUL-terminated UTF-8 string @p p. A script that reads that far past
 * a string's end is addressing the buffer's own layout, such as a list of
 * fixed-size records (a record of R bytes holds at most R - 1 bytes of
 * text, so the next record's index R is past it), and there the index is a
 * byte offset. An index up to the byte length stays a character index.
 */
bool utf8IndexIsPastEnd(const byte *p, uint32 index);

/**
 * Index of the code point that starts at byte @p offset of the
 * NUL-terminated UTF-8 string @p p: the number of code points before it.
 * The inverse of utf8OffsetOf(); an offset inside a character counts that
 * character as before it, and one past the terminator is the length.
 */
uint32 utf8IndexOfOffset(const byte *p, uint32 offset);

/**
 * Byte offset where the code point that ends at byte @p pos of the
 * @p len-byte UTF-8 text @p p begins (what BackSpace and Left step to).
 * A byte that is not part of a well-formed sequence is one step by itself.
 * @p pos must be 1..len. @p p must be NUL-terminated at @p len (a heap
 * string is), so a truncated tail stops at the terminator.
 */
uint32 utf8PrevBoundary(const byte *p, uint32 len, uint32 pos);

/**
 * Byte offset just after the code point that begins at byte @p pos (what
 * Delete and Right step to); @p pos + 1 for a malformed byte; @p pos
 * itself at @p len. @p pos must be a boundary.
 */
uint32 utf8NextBoundary(const byte *p, uint32 len, uint32 pos);

} // End of namespace Sci

#endif
```

  Source:

```cpp
#include "common/str.h"
#include "common/util.h"

#include "sci/utf8.h"

namespace Sci {

uint32 decodeUtf8Char(const byte *p, int &outBytes) {
	const uint32 lead = p[0];
	outBytes = 1;
	if (lead < 0x80)
		return lead;

	// Lead byte decides the length; every following byte must be 10xxxxxx.
	int len;
	uint32 cp;
	if (lead >= 0xC2 && lead <= 0xDF) { len = 2; cp = lead & 0x1F; }
	else if (lead >= 0xE0 && lead <= 0xEF) { len = 3; cp = lead & 0x0F; }
	else if (lead >= 0xF0 && lead <= 0xF4) { len = 4; cp = lead & 0x07; }
	else
		return lead;	// continuation byte or invalid lead: pass through

	for (int i = 1; i < len; i++) {
		const byte b = p[i];
		if ((b & 0xC0) != 0x80)
			return lead;	// truncated: pass the lead through, advance 1
		cp = (cp << 6) | (b & 0x3F);
	}

	// Reject over-long encodings and surrogates, which a well-formed
	// encoder never produces and which would let two byte strings mean
	// the same text.
	if ((len == 3 && cp < 0x800) || (len == 4 && (cp < 0x10000 || cp > 0x10FFFF)) ||
	    (cp >= 0xD800 && cp <= 0xDFFF))
		return lead;

	outBytes = len;
	return cp;
}

uint32 utf8Length(const byte *p) {
	uint32 n = 0;
	int bytes;
	while (*p) {
		decodeUtf8Char(p, bytes);
		p += bytes;
		n++;
	}
	return n;
}

int encodeUtf8Char(uint32 cp, byte *out) {
	if (cp > 0x10FFFF)
		cp = 0xFFFD;
	if (cp < 0x80) {
		out[0] = (byte)cp;
		return 1;
	}
	if (cp < 0x800) {
		out[0] = (byte)(0xC0 | (cp >> 6));
		out[1] = (byte)(0x80 | (cp & 0x3F));
		return 2;
	}
	if (cp < 0x10000) {
		out[0] = (byte)(0xE0 | (cp >> 12));
		out[1] = (byte)(0x80 | ((cp >> 6) & 0x3F));
		out[2] = (byte)(0x80 | (cp & 0x3F));
		return 3;
	}
	out[0] = (byte)(0xF0 | (cp >> 18));
	out[1] = (byte)(0x80 | ((cp >> 12) & 0x3F));
	out[2] = (byte)(0x80 | ((cp >> 6) & 0x3F));
	out[3] = (byte)(0x80 | (cp & 0x3F));
	return 4;
}

uint32 utf8WriteOffset(const byte *p, uint32 index) {
	const byte *start = p;
	int bytes;
	while (*p && index) {
		decodeUtf8Char(p, bytes);
		p += bytes;
		index--;
	}
	return (uint32)(p - start) + index;
}

uint32 utf8OffsetOf(const byte *p, uint32 index) {
	const byte *start = p;
	int bytes;
	while (*p && index--) {
		decodeUtf8Char(p, bytes);
		p += bytes;
	}
	return (uint32)(p - start);
}

bool utf8IndexIsPastEnd(const byte *p, uint32 index) {
	return index > Common::strnlen((const char *)p, 0xFFFFFFFFU);
}

uint32 utf8IndexOfOffset(const byte *p, uint32 offset) {
	uint32 pos = 0, index = 0;
	int bytes;
	while (p[pos] && pos < offset) {
		decodeUtf8Char(p + pos, bytes);
		pos += bytes;
		index++;
	}
	return index;
}

uint32 utf8PrevBoundary(const byte *p, uint32 len, uint32 pos) {
	// A code point is at most 4 bytes: try each start that would end at pos
	// and take the earliest one that decodes to exactly that span.
	for (uint32 back = 4; back >= 2; back--) {
		if (pos < back)
			continue;
		const uint32 start = pos - back;
		int bytes;
		decodeUtf8Char(p + start, bytes);
		if ((uint32)bytes == back && start + back <= len)
			return start;
	}
	return pos - 1;
}

uint32 utf8NextBoundary(const byte *p, uint32 len, uint32 pos) {
	if (pos >= len)
		return pos;
	int bytes;
	decodeUtf8Char(p + pos, bytes);
	return MIN<uint32>(pos + bytes, len);
}

} // End of namespace Sci
```

  `module.mk`: `utf8.o \` after `textmode.o \`.
- [ ] **Step 4: green** — N0 + 21 tests OK.
- [ ] **Step 5: commit** — `SCI: Add the UTF-8 helpers of a Korean heap`.

---

## Task 4: `ScriptStrings` with the fingerprint header

**Files:** new `engines/sci/engine/script_strings.{h,cpp}`, `test/engines/sci/script_strings.h`.

- [ ] **Step 1: test**:

```cpp
#include <cxxtest/TestSuite.h>

#include <common/memstream.h>

#include "sci/engine/script_strings.h"

class SciScriptStringsTestSuite : public CxxTest::TestSuite {
	static bool load(Sci::ScriptStrings &t, const char *text, const Common::TranslationFileHeader &expected = Common::TranslationFileHeader()) {
		Common::MemoryReadStream s((const byte *)text, strlen(text));
		return t.loadFromStream(s, "sci-ko.str", expected);
	}

	static Common::TranslationFileHeader kq1() {
		Common::TranslationFileHeader h;
		h.format = 1;
		h.game = "kq1sci";
		h.engine = "sci";
		h.fingerprint = "0123456789abcdef0123456789abcdef";
		return h;
	}

public:
	void test_entriesAndRooms() {
		Sci::ScriptStrings t;
		TS_ASSERT(load(t, "\xEF\xBB\xBF# comment\n0\t5\t\xEB\x81\x84\xEA\xB8\xB0\n0\t5\t14\troom\n12\t3\ta\\nb\tc\n"));
		Common::String out;
		TS_ASSERT(t.lookup(Sci::ScriptStrings::Key(0, 5), out));
		TS_ASSERT_EQUALS(out, "\xEB\x81\x84\xEA\xB8\xB0");
		TS_ASSERT(t.lookup(Sci::ScriptStrings::Key(0, 5, 14), out));
		TS_ASSERT_EQUALS(out, "room");
		TS_ASSERT(t.lookup(Sci::ScriptStrings::Key(0, 5, 15), out));
		TS_ASSERT_EQUALS(out, "\xEB\x81\x84\xEA\xB8\xB0");
		TS_ASSERT(t.lookup(Sci::ScriptStrings::Key(12, 3), out));
		TS_ASSERT_EQUALS(out, "a\nb\tc");
		TS_ASSERT(!t.lookup(Sci::ScriptStrings::Key(12, 4), out));
		TS_ASSERT(!t.getHeader().present());
	}

	void test_malformedLineRefusesTable() {
		Sci::ScriptStrings t;
		TS_ASSERT(!load(t, "0\t5\tok\n0\tx\tbad\n"));
		TS_ASSERT(!t.isLoaded());
	}

	void test_headerMatches() {
		Sci::ScriptStrings t;
		TS_ASSERT(load(t, "#! format=1 game=kq1sci fingerprint=0123456789ABCDEF0123456789abcdef engine=sci\n0\t5\tx\n", kq1()));
		TS_ASSERT(t.getHeader().present());
		TS_ASSERT_EQUALS(t.entryCount(), 1u);
	}

	void test_headerMismatchRefuses() {
		Sci::ScriptStrings t;
		TS_ASSERT(!load(t, "#! format=1 game=kq1sci fingerprint=ffffffffffffffffffffffffffffffff engine=sci\n0\t5\tx\n", kq1()));
		TS_ASSERT(!t.isLoaded());
		TS_ASSERT(!load(t, "#! format=1 game=kq4sci fingerprint=0123456789abcdef0123456789abcdef engine=sci\n0\t5\tx\n", kq1()));
	}

	void test_badHeaderRefuses() {
		Sci::ScriptStrings t;
		TS_ASSERT(!load(t, "#! format=1 game=kq1sci fingerprint=nothex engine=sci\n0\t5\tx\n", kq1()));
	}

	void test_headerlessLoadsUnchecked() {
		Sci::ScriptStrings t;
		TS_ASSERT(load(t, "# script strings\n0\t5\tx\n", kq1()));
		TS_ASSERT(t.isLoaded());
	}

	void test_bufferTags() {
		Sci::ScriptStrings t;
		TS_ASSERT(load(t, "997\t19\tA\n995\t6\t.\n"));
		const uint32 buf = Sci::ScriptStrings::bufferId(3, 0x10);
		t.tagBuffer(buf, Sci::ScriptStrings::Key(997, 19), "You carry");
		t.appendToBuffer(buf, "You carry", Sci::ScriptStrings::Key(), Sci::ScriptStrings::Key::kNoBuffer, " clip");
		t.appendToBuffer(buf, "You carry clip", Sci::ScriptStrings::Key(995, 6), Sci::ScriptStrings::Key::kNoBuffer, "!");
		const Sci::ScriptStrings::Key k = t.keyOf(buf, "You carry clip!");
		TS_ASSERT(k.isBuffer());
		Common::String out;
		TS_ASSERT(t.lookup(k, out));
		TS_ASSERT_EQUALS(out, "A clip.");
		TS_ASSERT(!t.keyOf(buf, "other").isSet());
	}

	void test_sci11StringsStart() {
		// header (4), one instance: magic 0x1234, 3 properties -> strings at 4 + 2*0 + 6.
		const byte heap[] = { 0, 0, 0, 0, 0x34, 0x12, 3, 0, 0, 0, 0, 0, 'h', 'i', 0 };
		TS_ASSERT_EQUALS(Sci::ScriptStrings::sci11StringsStart(heap, sizeof(heap)), 10u);
	}
};
```

- [ ] **Step 2: red** — `sci/engine/script_strings.h: No such file or directory`.
- [ ] **Step 3: implement** — ported from `jamo` `engine/translation.{h,cpp}` (renamed after the class); edits: the header
  line (`load`/`loadFromStream` take the expected header, `getHeader()`), `resolveParticles` dropped (callers use
  `Common::resolveKoreanParticles`), `_bufferTags` cleared on load, file name in every warning. Header:

```cpp
#ifndef SCI_ENGINE_SCRIPT_STRINGS_H
#define SCI_ENGINE_SCRIPT_STRINGS_H

#include "common/scummsys.h"
#include "common/str.h"
#include "common/array.h"
#include "common/hashmap.h"
#include "common/translation_file.h"

namespace Common {
class SeekableReadStream;
}

namespace Sci {

/**
 * Translations for the strings a fan translation cannot patch: the ones
 * embedded in scripts.
 *
 * A fan translation replaces the game's TEXT resources with patch files
 * (text.000, text.079, ...) - the way Sierra's own localisations and every
 * existing fan patch work, and the way the engine has always loaded them.
 * Those strings need no help from this class: the resource IS the
 * translation.
 *
 * What a patch cannot reach is a string inside a script's own string block:
 * an inventory name, a parser reply, "You are carrying nothing!". The code
 * refers to it by absolute offset (lofsa), so a longer translation would
 * shift every string after it and every offset into them. KQ1 has 93 such
 * strings that reach the screen and appear in no TEXT resource. They are
 * translated at run time instead, from this table, keyed by where they
 * live: (script number, string id), the id being the one
 * Script::identifyOffsets() assigns at load and SegManager::stringKey()
 * reports at display.
 *
 * The file is sci-<lang>.str next to the game, one entry per line:
 *
 *     script <TAB> id [<TAB> room] <TAB> text
 *
 * UTF-8, '#' comments, "\n" for a newline in the text. A room number makes
 * the entry apply in that room only, for a line that means different things
 * in different places; an entry without one applies anywhere and loses to
 * one with a matching room. The language is not in the file: it is the
 * language the game was detected as, which chooses the file.
 */
class ScriptStrings {
public:
	/** Where a string lives. */
	struct Key {
		static const uint16 kAnyRoom = 0xFFFF;

		static const uint32 kNoBuffer = 0xFFFFFFFF;

		uint16 script;
		uint16 id;
		uint16 room;
		/**
		 * Set only by keyOf() for a buffer built by StrCpy + StrCat: the
		 * key then names the whole buffer, script/id/room are its first
		 * part's, and lookup() answers with the parts joined (see
		 * appendToBuffer()).
		 */
		uint32 buffer;

		Key() : script(0xFFFF), id(0xFFFF), room(kAnyRoom), buffer(kNoBuffer) {}
		Key(uint16 s, uint16 i, uint16 r = kAnyRoom) : script(s), id(i), room(r), buffer(kNoBuffer) {}
		bool isSet() const { return script != 0xFFFF; }
		bool isBuffer() const { return buffer != kNoBuffer; }
	};

	ScriptStrings() : _loaded(false), _present(false) {}

	/**
	 * Load sci-<lang>.str through SearchMan (the language folder ko/ is in it
	 * with language=ko). @p languageCode is Common::getLanguageFolderCode()'s
	 * code ("ko"). @p expected is what the game knows about itself
	 * (gameTranslationHeader()); a table whose header differs is refused, one
	 * without a header loads with a warning (DESIGN.md decision 4).
	 */
	bool load(const Common::String &languageCode, const Common::TranslationFileHeader &expected = Common::TranslationFileHeader());

	/**
	 * Parse a table from a stream; what load() does once the file is open.
	 * A UTF-8 BOM is skipped; a first line starting with "#!" is the
	 * fingerprint header. A malformed line rejects the whole table (a wrong
	 * key would draw a translation in the wrong place, unnoticed); a
	 * duplicate key is a warning and the later line wins. @p name is for the
	 * warnings.
	 */
	bool loadFromStream(Common::SeekableReadStream &in, const Common::String &name = "sci-ko.str",
	                    const Common::TranslationFileHeader &expected = Common::TranslationFileHeader());

	/** The header the table had; not present() for a table without one. */
	const Common::TranslationFileHeader &getHeader() const { return _header; }

	enum LineKind { kLineSkip, kLineEntry, kLineError };

	/**
	 * Parse one line of the table, line end already stripped: a blank or
	 * '#' line is skipped; anything else must be an entry, or kLineError
	 * with @p error saying why. Numbers are decimal digits, 0..65534.
	 */
	static LineKind parseLine(const Common::String &line, Key &key,
	                          Common::String &text, const char *&error);

	bool isLoaded() const { return _loaded; }

	/**
	 * The file was found and parsed, entries or not. A table with no
	 * entries (comments only) is present but not loaded.
	 */
	bool isPresent() const { return _present; }

	/** Every translated string, for collecting the translation's code points. */
	void collectTexts(Common::Array<Common::String> &out) const;
	uint entryCount() const { return _entries.size(); }

	/**
	 * The translation for the string at @p key, or false to keep the
	 * original. A room-specific entry wins in its room; an any-room entry
	 * answers everywhere else. @p out receives UTF-8.
	 */
	bool lookup(const Key &key, Common::String &out) const;

	/**
	 * Remember that @p buffer now holds the script string at @p key.
	 *
	 * A script string reaches the screen as a stack buffer: the script
	 * kStrCpy's it first, and the buffer is what kDisplay / kDrawControl are
	 * handed - by then nothing says which string it was. So the copy records
	 * (buffer -> key, text written) here, and the display looks it up.
	 *
	 * The buffer is reused freely, so the record carries the text and
	 * keyOf() answers only while the buffer still holds it. Measured on KQ1
	 * through the inventory and two parser replies: 28 lookups, 0 stale.
	 *
	 * @p buffer is bufferId(segment, offset) rather than a reg_t: reg_t's
	 * accessors consult the SCI version, which would drag the engine into
	 * anything that links this class (the unit tests).
	 */
	void tagBuffer(uint32 buffer, const Key &key, const Common::String &text);

	/**
	 * Record that StrCat appended @p text to @p buffer, which held
	 * @p before. @p key is the appended string's own key when it is a
	 * script string; otherwise, if @p source (a bufferId, or kNoBuffer) is
	 * itself a tagged buffer still holding @p text, its parts are
	 * appended; otherwise @p text is appended as plain text.
	 *
	 * Only a tag that still matches @p before grows: an untagged or stale
	 * buffer stays untagged, so the display sees exactly what it saw
	 * before StrCat was followed.
	 *
	 * After the call the tag holds the parts in order - the StrCpy'd
	 * script string, then each appended string with its key or none - and
	 * their joined text, which is what the buffer now holds.
	 */
	void appendToBuffer(uint32 buffer, const Common::String &before, const Key &key,
	                    uint32 source, const Common::String &text);

	/**
	 * Record a plain copy of @p text from @p source into @p buffer: the
	 * buffer gets @p source's tag if @p source still holds @p text;
	 * otherwise nothing changes.
	 */
	void copyBufferTag(uint32 buffer, uint32 source, const Common::String &text);

	/**
	 * The key recorded for @p buffer, if it still holds @p text. For a
	 * buffer of more than one part (appendToBuffer()) it is a buffer key:
	 * lookup() with it gives each part translated, or as it was when it
	 * has no translation, joined; false when no part has one.
	 */
	Key keyOf(uint32 buffer, const Common::String &text) const;

	static uint32 bufferId(uint16 segment, uint16 offset) { return ((uint32)segment << 16) | offset; }

	/**
	 * Offset, in an SCI1.1 heap resource, of the first string: the end of
	 * the object instances. String id 1 starts here even when it is empty,
	 * which is how the extractor (tools/korean/sci11_kr_extract.py) counts.
	 * @p size bounds every read; a truncated heap yields @p size.
	 */
	static uint sci11StringsStart(const byte *heap, uint size);

private:
	/// lookup() for one place, never a buffer key.
	bool lookupPlace(const Key &key, Common::String &out) const;
	/// lookup() for a buffer key: the parts joined, false if none translates.
	bool lookupBuffer(uint32 buffer, Common::String &out) const;

	/// (script, id, room) packed exactly; no two places share a value.
	static uint64 placeId(uint16 script, uint16 id, uint16 room) {
		return ((uint64)room << 32) | ((uint32)script << 16) | id;
	}

	bool _loaded;
	bool _present;
	Common::TranslationFileHeader _header;
	Common::HashMap<uint64, Common::String> _entries;

	/// One string a buffer was built from: a script string (key set) or text.
	struct BufferPart {
		Key key;
		Common::String text;
	};

	/// parts[0] is the StrCpy'd script string; text is the parts joined.
	struct BufferTag {
		Common::Array<BufferPart> parts;
		Common::String text;
	};
	Common::HashMap<uint32, BufferTag> _bufferTags;
};

} // End of namespace Sci

#endif // SCI_ENGINE_SCRIPT_STRINGS_H
```

  Source:

```cpp
#include "sci/engine/script_strings.h"

#include "common/file.h"
#include "common/stream.h"
#include "common/textconsole.h"
#include "common/util.h"

namespace Sci {

bool ScriptStrings::load(const Common::String &languageCode, const Common::TranslationFileHeader &expected) {
	// sci-<code>.str: a folder can carry one table per language, and the
	// target's language, not the file, says which applies.
	const Common::String name = Common::String::format("sci-%s.str", languageCode.c_str());
	Common::File f;
	if (!f.open(Common::Path(name)))
		return false;
	return loadFromStream(f, name, expected);
}

// "\n" in the file is a newline in the string; nothing else is escaped.
static Common::String unescape(const Common::String &s) {
	Common::String out;
	for (uint i = 0; i < s.size(); i++) {
		if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') {
			out += '\n';
			i++;
		} else {
			out += s[i];
		}
	}
	return out;
}

// One or more ASCII digits at @p p, value 0..65534 (0xFFFF is kAnyRoom).
// Advances @p p past the digits.
static bool readNum(const Common::String &line, uint &p, uint16 &out) {
	const uint start = p;
	uint32 v = 0;
	while (p < line.size() && Common::isDigit(line[p])) {
		v = v * 10 + (line[p] - '0');
		p++;
		if (v > 65534)
			return false;
	}
	out = (uint16)v;
	return p > start;
}

ScriptStrings::LineKind ScriptStrings::parseLine(const Common::String &line, Key &key,
		Common::String &text, const char *&error) {
	if (line.empty() || line[0] == '#')
		return kLineSkip;

	// script <TAB> id <TAB> text
	// script <TAB> id <TAB> room <TAB> text
	// The key is checked byte for byte: a key that parsed loosely would put
	// a translation in the wrong place, which nobody would notice.
	uint p = 0;
	if (!readNum(line, p, key.script) || p >= line.size() || line[p] != '\t') {
		error = "bad script number";
		return kLineError;
	}
	p++;
	if (!readNum(line, p, key.id) || p >= line.size() || line[p] != '\t') {
		error = "bad string id";
		return kLineError;
	}
	p++;

	// The third field is the room only if it is a number AND another tab
	// follows it; otherwise the text starts there and runs to the end of
	// the line, tabs included.
	key.room = Key::kAnyRoom;
	uint q = p;
	uint16 room;
	if (readNum(line, q, room) && q < line.size() && line[q] == '\t') {
		key.room = room;
		p = q + 1;
	}
	text = Common::String(line.c_str() + p);
	return kLineEntry;
}

bool ScriptStrings::loadFromStream(Common::SeekableReadStream &in, const Common::String &name,
                                   const Common::TranslationFileHeader &expected) {
	_entries.clear();
	_bufferTags.clear();
	_loaded = false;
	_present = false;
	_header = Common::TranslationFileHeader();

	Common::HashMap<uint64, uint> firstLine;
	uint lineNo = 0;
	while (!in.eos()) {
		Common::String line = in.readLine();
		lineNo++;
		if (lineNo == 1) {
			if (line.hasPrefix("\xEF\xBB\xBF"))
				line.erase(0, 3);
			Common::String error;
			if (!Common::TranslationFileHeader::parse(line, _header, error) && !error.empty()) {
				warning("ScriptStrings: %s: %s, not loaded", name.c_str(), error.c_str());
				return false;
			}
			if (Common::checkTranslationHeader(_header, expected, name) == Common::kTranslationMismatch)
				return false;
			if (_header.present())
				continue;
		}

		Key key;
		Common::String text;
		const char *error = nullptr;
		const LineKind kind = parseLine(line, key, text, error);
		if (kind == kLineSkip)
			continue;
		if (kind == kLineError) {
			warning("ScriptStrings: %s line %u: %s, not loaded", name.c_str(), lineNo, error);
			_entries.clear();
			return false;
		}

		// The later line wins, so a fix can be appended to a generated
		// file; say so, in case it was not meant.
		const uint64 place = placeId(key.script, key.id, key.room);
		if (firstLine.contains(place))
			warning("ScriptStrings: %s line %u duplicates line %u", name.c_str(), lineNo, firstLine[place]);
		else
			firstLine[place] = lineNo;
		_entries[place] = unescape(text);
	}

	_present = true;
	_loaded = !_entries.empty();
	return _loaded;
}

void ScriptStrings::collectTexts(Common::Array<Common::String> &out) const {
	for (Common::HashMap<uint64, Common::String>::const_iterator it = _entries.begin(); it != _entries.end(); ++it)
		out.push_back(it->_value);
}

bool ScriptStrings::lookup(const Key &key, Common::String &out) const {
	if (!_loaded || !key.isSet())
		return false;
	if (key.isBuffer())
		return lookupBuffer(key.buffer, out);
	return lookupPlace(key, out);
}

bool ScriptStrings::lookupPlace(const Key &key, Common::String &out) const {
	if (key.room != Key::kAnyRoom) {
		const uint64 exact = placeId(key.script, key.id, key.room);
		if (_entries.contains(exact)) {
			out = _entries.getVal(exact);
			return true;
		}
	}
	const uint64 any = placeId(key.script, key.id, Key::kAnyRoom);
	if (_entries.contains(any)) {
		out = _entries.getVal(any);
		return true;
	}
	return false;
}

void ScriptStrings::tagBuffer(uint32 buffer, const Key &key, const Common::String &text) {
	BufferPart p;
	p.key = key;
	p.key.buffer = Key::kNoBuffer;
	p.text = text;
	BufferTag t;
	t.parts.push_back(p);
	t.text = text;
	_bufferTags[buffer] = t;
}

// A tag after StrCat. The PQ2 inventory (script 995) builds its sentence
// in one buffer: StrCpy "You are carrying..." (997:19), then per item
// StrCat the item name kFormat wrote into another buffer (" 머니 클립",
// already Korean: kFormat translates its %s), then StrCat "." (995:6);
// Print (255) StrCpy's the result into its own buffer and draws that
// (measured: kStrCpy/kStrCat/strSplitHeap traced in room 1). The tag
// follows the buffer: it keeps every part with its own key - a script
// string's, or none for text - and the joined text, which is what the
// buffer holds while nobody else writes to it; a StrCpy of the buffer
// carries the tag along (copyBufferTag()). The display asks keyOf() with
// the text it read; a match gives a buffer key, and lookup() joins the
// parts, each its translation or, without one, its own text:
// "가진 것..." + " 머니 클립" + ".". A part with no translation is
// byte for byte what the script wrote, so a buffer none of whose parts is
// translated answers false and the display keeps the original.
//
// Only an existing tag grows, and only while it still matches: StrCat
// on an untagged or reused buffer leaves it untagged. Nothing is tagged
// unless a translation is loaded (kStrCpy/kStrCat check), so another
// language or an untranslated game is not touched.
void ScriptStrings::appendToBuffer(uint32 buffer, const Common::String &before, const Key &key,
                                   uint32 source, const Common::String &text) {
	if (!_bufferTags.contains(buffer))
		return;
	BufferTag &t = _bufferTags.getVal(buffer);
	if (t.text != before) {
		// Reused since it was tagged; the tag can never match again.
		_bufferTags.erase(buffer);
		return;
	}
	if (key.isSet() && !key.isBuffer()) {
		BufferPart p;
		p.key = key;
		p.text = text;
		t.parts.push_back(p);
	} else if (source != Key::kNoBuffer && source != buffer && _bufferTags.contains(source)
	           && _bufferTags.getVal(source).text == text) {
		const Common::Array<BufferPart> &from = _bufferTags.getVal(source).parts;
		for (uint i = 0; i < from.size(); ++i)
			t.parts.push_back(from[i]);
	} else {
		BufferPart p;
		p.text = text;
		t.parts.push_back(p);
	}
	t.text += text;
}

void ScriptStrings::copyBufferTag(uint32 buffer, uint32 source, const Common::String &text) {
	if (buffer == source)
		return;
	if (_bufferTags.contains(source) && _bufferTags.getVal(source).text == text) {
		const BufferTag t = _bufferTags.getVal(source);
		_bufferTags[buffer] = t;
	}
	// Otherwise the buffer's own tag stays: keyOf() already refuses it
	// once the text differs, exactly as before StrCat was followed.
}

bool ScriptStrings::lookupBuffer(uint32 buffer, Common::String &out) const {
	if (!_bufferTags.contains(buffer))
		return false;
	const BufferTag &t = _bufferTags.getVal(buffer);
	Common::String joined;
	bool any = false;
	for (uint i = 0; i < t.parts.size(); ++i) {
		Common::String part;
		if (lookupPlace(t.parts[i].key, part)) {
			any = true;
			joined += part;
		} else {
			joined += t.parts[i].text;
		}
	}
	if (!any)
		return false;
	out = joined;
	return true;
}

uint ScriptStrings::sci11StringsStart(const byte *heap, uint size) {
	// A 4-byte header, then instances that each start with the 0x1234 magic
	// and a property count. The first word that is not a magic belongs to
	// the strings: it must not be consumed (Script::identifyOffsets() did,
	// so a heap whose strings open with a NUL pair lost its first two).
	if (size < 4)
		return size;
	uint p = (heap[2] | (heap[3] << 8)) * 2 + 4;
	while (p + 4 <= size && (heap[p] | (heap[p + 1] << 8)) == 0x1234) {
		const uint props = heap[p + 2] | (heap[p + 3] << 8);
		if (props < 2)
			break;
		p += props * 2;
	}
	return p < size ? p : size;
}

ScriptStrings::Key ScriptStrings::keyOf(uint32 buffer, const Common::String &text) const {
	if (!_bufferTags.contains(buffer))
		return Key();
	const BufferTag &t = _bufferTags.getVal(buffer);
	// The buffer has been reused for something else: the tag is stale.
	if (t.text != text)
		return Key();
	Key k = t.parts[0].key;
	if (t.parts.size() > 1)
		k.buffer = buffer;
	return k;
}

} // End of namespace Sci
```

  `module.mk`: `engine/script_strings.o \` after `engine/script_patches.o \`.
- [ ] **Step 4: green** — N0 + 29 tests OK. The runner prints the warnings of the cases: "has no fingerprint header ... loaded
  unchecked" for each headerless table, "was made for another game: fingerprint=ffff..., the game's is 0123..." and
  "game=kq4sci, the game's is kq1sci", and "fingerprint=nothex is not an md5"-style text for the bad header.
- [ ] **Step 5: commit** — `SCI: Add ScriptStrings, the script string table of a translation` (body: format of DESIGN
  section 4; header refused on mismatch, headerless loads with a warning; buffer tags as jamo).

---

## Task 5: `KoreanParser`

**Files:** new `engines/sci/parser/korean_parser.{h,cpp}`, `test/engines/sci/korean_parser.h`.

- [ ] **Step 1: test** (a KQ1-shaped vocabulary built in memory with P2's fixture):

```cpp
#include <cxxtest/TestSuite.h>

#include <common/memstream.h>

#include "../../korean_vocab_fixture.h"
#include "sci/parser/korean_parser.h"

using Sci::ResultWord;
using Sci::ResultWordList;
using Sci::ResultWordListList;
using Sci::WordMap;

// Groups and classes as a SCI0 vocabulary gives them (class << 4).
class SciKoreanParserTestSuite : public CxxTest::TestSuite {
	enum { kVerb = 0x800, kNoun = 0x100, kPrep = 0x010 };
	enum { gOpen = 0x10, gDoor = 0x20, gGive = 0x30, gBowl = 0x40, gWoodcutter = 0x50, gTo = 0x60, gIn = 0x61,
	       gLook = 0x70, gAsk = 0x80, gAbout = 0x90, gTree = 0xA0, gHole = 0xA1 };

	WordMap _words;
	Common::Array<byte> _table;
	Common::KoreanVocabulary _vocab;

	void add(const char *w, int cls, int group) {
		ResultWord r = { cls, group };
		_words[w].push_back(r);
	}

	// Tokens of a typed sentence through the parser's lookup; the roles alongside.
	void tokens(Sci::KoreanParser &p, const char *const *typed, ResultWordListList &out, Common::Array<uint8> &roles) {
		for (; *typed; ++typed) {
			ResultWordList w;
			ResultWordListList extra;
			uint8 role = 0;
			TSM_ASSERT(*typed, p.lookup(*typed, w, extra, role) > 0);
			out.push_back(w);
			roles.push_back(role);
			for (ResultWordListList::const_iterator e = extra.begin(); e != extra.end(); ++e) {
				out.push_back(*e);
				roles.push_back(0);
			}
		}
	}

	static Common::String groups(const ResultWordListList &l) {
		Common::String s;
		for (ResultWordListList::const_iterator t = l.begin(); t != l.end(); ++t)
			s += Common::String::format("%s%x", s.empty() ? "" : " ", t->front()._group);
		return s;
	}

public:
	void setUp() override {
		_words.clear();
		add("open", kVerb, gOpen);
		add("door", kNoun, gDoor);
		add("give", kVerb, gGive);
		add("bowl", kNoun, gBowl);
		add("woodcutter", kNoun, gWoodcutter);
		add("to", kPrep, gTo);
		add("in", kPrep, gIn);
		add("look", kVerb, gLook);
		add("ask", kVerb, gAsk);
		add("about", 0x008, gAbout);
		add("tree", kNoun, gTree);
		add("hole", kNoun, gHole);
		static const KoreanVocabFixture::Row rows[] = {
			{ "\xEB\xAC\xB8", gDoor, kNoun },                         // 문
			{ "\xEC\x97\xB4\xEB\x8B\xA4", gOpen, kVerb },             // 열다
			{ "\xEA\xB7\xB8\xEB\xA6\x87", gBowl, kNoun },             // 그릇
			{ "\xEB\x82\x98\xEB\xAC\xB4\xEA\xBE\xBC", gWoodcutter, kNoun }, // 나무꾼
			{ "\xEC\xA3\xBC\xEB\x8B\xA4", gGive, kVerb },             // 주다
			{ "\xEB\x93\xA4\xEC\x97\xAC\xEB\x8B\xA4\xEB\xB3\xB4\xEB\x8B\xA4", gLook, kVerb | Common::KoreanVocabulary::kSequenceFlag }, // 들여다보다
			{ "\xEB\x93\xA4\xEC\x97\xAC\xEB\x8B\xA4\xEB\xB3\xB4\xEB\x8B\xA4", gIn, kPrep },
			{ "\xEB\x82\x98\xEB\xAC\xB4", gTree, kNoun },             // 나무
			{ "\xEA\xB5\xAC\xEB\xA9\x8D", gHole, kNoun },             // 구멍
			{ "\xEB\xAC\xBC\xEC\x96\xB4\xEB\xB3\xB4\xEB\x8B\xA4", gAsk, kVerb }, // 물어보다
		};
		_table = KoreanVocabFixture::build("SCIKOR", rows, ARRAYSIZE(rows));
		Common::MemoryReadStream s(_table.data(), _table.size());
		TS_ASSERT(_vocab.loadFromStream(s, "SCIKOR", "fixture"));
	}

	void test_objectVerbBecomesVerbObject() {
		Sci::KoreanParser p(_words, _vocab);
		static const char *const typed[] = { "\xEB\xAC\xB8\xEC\x9D\x84", "\xEC\x97\xB4\xEC\x96\xB4", nullptr }; // 문을 열어
		ResultWordListList words, fallback;
		Common::Array<uint8> roles;
		tokens(p, typed, words, roles);
		p.arrange(words, roles, fallback);
		TS_ASSERT_EQUALS(groups(words), "10 20");
		TS_ASSERT(fallback.empty());
	}

	void test_particleBecomesPreposition() {
		Sci::KoreanParser p(_words, _vocab);
		// 나무꾼에게 그릇을 줘 -> give bowl to woodcutter; the fallback drops the "to".
		static const char *const typed[] = { "\xEB\x82\x98\xEB\xAC\xB4\xEA\xBE\xBC\xEC\x97\x90\xEA\xB2\x8C",
		                                     "\xEA\xB7\xB8\xEB\xA6\x87\xEC\x9D\x84", "\xEC\xA4\x98", nullptr };
		ResultWordListList words, fallback;
		Common::Array<uint8> roles;
		tokens(p, typed, words, roles);
		TS_ASSERT_EQUALS(roles[0], (uint8)Common::kKoreanRoleTo);
		p.arrange(words, roles, fallback);
		TS_ASSERT_EQUALS(groups(words), "30 40 60 50");
		TS_ASSERT_EQUALS(groups(fallback), "30 50 40");
	}

	void test_sequenceWordAndNounUnit() {
		Sci::KoreanParser p(_words, _vocab);
		// 나무 구멍을 들여다봐 -> look in tree hole
		static const char *const typed[] = { "\xEB\x82\x98\xEB\xAC\xB4", "\xEA\xB5\xAC\xEB\xA9\x8D\xEC\x9D\x84",
		                                     "\xEB\x93\xA4\xEC\x97\xAC\xEB\x8B\xA4\xEB\xB4\x90", nullptr };
		ResultWordListList words, fallback;
		Common::Array<uint8> roles;
		tokens(p, typed, words, roles);
		p.arrange(words, roles, fallback);
		TS_ASSERT_EQUALS(groups(words), "70 61 a0 a1");
	}

	void test_askGetsAbout() {
		Sci::KoreanParser p(_words, _vocab);
		static const char *const typed[] = { "\xEB\xAC\xB8", "\xEB\xAC\xBC\xEC\x96\xB4\xEB\xB4\x90", nullptr }; // 문 물어봐
		ResultWordListList words, fallback;
		Common::Array<uint8> roles;
		tokens(p, typed, words, roles);
		p.arrange(words, roles, fallback);
		TS_ASSERT_EQUALS(groups(words), "80 90 20");
	}

	void test_joinPairs() {
		Sci::KoreanParser p(_words, _vocab);
		// 들여다 봐: the table holds the joined word.
		TS_ASSERT_EQUALS(p.joinPairs("\xEB\x93\xA4\xEC\x97\xAC\xEB\x8B\xA4 \xEB\xB4\x90"),
		                 "\xEB\x93\xA4\xEC\x97\xAC\xEB\x8B\xA4\xEB\xB4\x90");
		TS_ASSERT_EQUALS(p.joinPairs("open  door"), "open door");
	}

	void test_unknownWord() {
		Sci::KoreanParser p(_words, _vocab);
		ResultWordList w;
		ResultWordListList extra;
		uint8 role;
		TS_ASSERT_EQUALS(p.lookup("\xEC\x9E\x90\xEC\x84\xB8\xED\x9E\x88", w, extra, role), 0u); // 자세히
		TS_ASSERT(w.empty());
	}
};
```

- [ ] **Step 2: red** — `sci/parser/korean_parser.h: No such file or directory`.
- [ ] **Step 3: implement** — `jamo` `vocabulary.cpp:768-1025` (`joinKoreanPairs`, `hasClass`, `onlyClass`,
  `isPrepositionOnly`, `koreanPreposition`, `arrangeKorean`, `koreanAskAbout`, `arrangeTokens`) moved into a class that
  holds references to the game's `WordMap` and the word table; `heapStringsAreUtf8()` is gone (UTF-8 only), the lookup
  loop of `jamo`'s `lookupWord` (sequence words) is `lookup()`. `englishLine` is not ported (it serves `koreanAnswerView`,
  P4). Header:

```cpp
#ifndef SCI_PARSER_KOREAN_PARSER_H
#define SCI_PARSER_KOREAN_PARSER_H

#include "common/array.h"
#include "common/korean_stem.h"
#include "common/korean_vocab.h"
#include "common/str.h"

#include "sci/parser/vocabulary.h"

namespace Sci {

/**
 * Korean sentences for the game's English parser (DESIGN.md section 6): the
 * words come from the pack's word table (Common::KoreanVocabulary), and
 * the sentence is put into the order the said() specs were written for.
 * Holds no state of its own between calls; the game's words and the table
 * belong to Vocabulary. Text is UTF-8 (a TextMode::kUtf8 game).
 */
class KoreanParser {
public:
	KoreanParser(const WordMap &words, const Common::KoreanVocabulary &vocab, uint32 debugChannel = 0);

	enum { kMaxMeanings = 8 };

	/**
	 * The table's (class, group) pairs for one typed word: into @p retval,
	 * and for a sequence word (들여다봐: look, in) its further pairs as one
	 * token each into @p extra. @p role: the particle the word lost.
	 * Returns the pairs found; 0 leaves both lists as they were.
	 */
	uint lookup(const Common::String &word, ResultWordList &retval, ResultWordListList &extra, uint8 &role) const;

	/** @p sentence with adjacent Korean words the table holds as one word joined (사 줘 -> 사줘). */
	Common::String joinPairs(const char *sentence) const;

	/**
	 * Puts the verb first, the plain objects next and each particle's noun
	 * as an English preposition phrase last (나무꾼에게 그릇을 줘 -> give
	 * bowl to woodcutter). @p roles: one Common::KoreanRole per token.
	 * @p fallback: the same without the particle phrases, when there were
	 * any (said() is retried on it), else empty.
	 */
	void arrange(ResultWordListList &words, const Common::Array<uint8> &roles, ResultWordListList &fallback) const;

	/** The English prepositions of @p role that the game's vocabulary has. */
	void preposition(uint8 role, ResultWordList &out) const;

	/** ask/tell + a bare topic: "about" is put before it; tell + to + someone is the game's talk. */
	void askAbout(ResultWordListList &words) const;

private:
	bool arrangeTokens(const Common::Array<ResultWordList> &tok, const Common::Array<uint8> &role, int verb, uint verbTail,
	                   const Common::Array<uint> &unitStart, bool useRoles, ResultWordListList &out) const;

	const WordMap &_words;
	const Common::KoreanVocabulary &_vocab;
	uint32 _debugChannel;
};

} // End of namespace Sci

#endif
```

  Source:

```cpp
#include "common/debug.h"
#include "common/ustr.h"

#include "sci/parser/korean_parser.h"

namespace Sci {

KoreanParser::KoreanParser(const WordMap &words, const Common::KoreanVocabulary &vocab, uint32 debugChannel) :
	_words(words), _vocab(vocab), _debugChannel(debugChannel) {
}

uint KoreanParser::lookup(const Common::String &word, ResultWordList &retval, ResultWordListList &extra, uint8 &role) const {
	uint16 groups[kMaxMeanings], classes[kMaxMeanings];
	const Common::U32String kword(word, Common::kUtf8);
	role = Common::kKoreanRoleNone;
	const uint n = _vocab.lookupAll(kword, groups, classes, kMaxMeanings, role);
	// A sequence word (들여다봐: look, in) is its first pair here and one
	// extra token per further pair.
	const bool sequence = n > 1 && (classes[0] & Common::KoreanVocabulary::kSequenceFlag);
	for (uint i = 0; i < n; ++i) {
		ResultWord tmp = { (uint16)(classes[i] & ~Common::KoreanVocabulary::kSequenceFlag), groups[i] };
		if (sequence && i > 0) {
			ResultWordList one;
			one.push_back(tmp);
			extra.push_back(one);
		} else {
			retval.push_back(tmp);
		}
	}
	return n;
}

Common::String KoreanParser::joinPairs(const char *sentence) const {
	Common::Array<Common::String> words;
	Common::String cur;
	for (const char *p = sentence; ; ++p) {
		if (*p == ' ' || *p == '\0') {
			if (!cur.empty())
				words.push_back(cur);
			cur.clear();
			if (!*p)
				break;
		} else {
			cur += *p;
		}
	}
	Common::String out;
	for (uint i = 0; i < words.size(); ++i) {
		if (i + 1 < words.size() && (byte)words[i][0] >= 0x80 && (byte)words[i + 1][0] >= 0x80) {
			const Common::String joined = words[i] + words[i + 1];
			const Common::U32String first(words[i], Common::kUtf8), second(words[i + 1], Common::kUtf8);
			if (_vocab.lookupJoined(first, second)) {
				debugC(1, _debugChannel, "[parse] joined '%s' + '%s'", words[i].c_str(), words[i + 1].c_str());
				words[i] = joined;
				words.remove_at(i + 1);
			}
		}
		if (!out.empty())
			out += ' ';
		out += words[i];
	}
	return out;
}

static bool hasClass(const ResultWordList &w, uint16 mask) {
	for (ResultWordList::const_iterator r = w.begin(); r != w.end(); ++r)
		if (r->_class & mask)
			return true;
	return false;
}

// Every pair of the token has the class bit.
static bool onlyClass(const ResultWordList &w, uint16 mask) {
	if (w.empty())
		return false;
	for (ResultWordList::const_iterator r = w.begin(); r != w.end(); ++r)
		if (!(r->_class & mask))
			return false;
	return true;
}

// The preposition classes: 0x010 is VOCAB_CLASS_PREPOSITION; "about" and
// "with" carry 0x008 in every SCI0 vocabulary seen, their own class, and
// the grammar places them like prepositions.
static const uint16 kPrepositionClasses = (VOCAB_CLASS_PREPOSITION << 4) | 0x008;

// A token that is a preposition and nothing else: 안 (in), 위 (on), 대해
// (about). Korean puts it after its noun, English before.
static bool isPrepositionOnly(const ResultWordList &w) {
	return onlyClass(w, kPrepositionClasses);
}

void KoreanParser::preposition(uint8 role, ResultWordList &out) const {
	static const char *const kTo[] = { "to", nullptr };
	static const char *const kAt[] = { "in", "on", "at", "into", nullptr };
	static const char *const kFrom[] = { "from", "in", nullptr };
	static const char *const kWith[] = { "with", "into", "to", nullptr };
	const char *const *names;
	switch (role) {
	case Common::kKoreanRoleTo: names = kTo; break;
	case Common::kKoreanRoleAt: names = kAt; break;
	case Common::kKoreanRoleFrom: names = kFrom; break;
	case Common::kKoreanRoleWith: names = kWith; break;
	default: return;
	}
	for (; *names; ++names) {
		WordMap::const_iterator it = _words.find(*names);
		if (it == _words.end())
			continue;
		for (ResultWordList::const_iterator r = it->_value.begin(); r != it->_value.end(); ++r) {
			if (!(r->_class & kPrepositionClasses))
				continue;
			bool dup = false;
			for (ResultWordList::const_iterator o = out.begin(); o != out.end() && !dup; ++o)
				dup = o->_group == r->_group;
			if (!dup)
				out.push_back(*r);
		}
	}
}

void KoreanParser::arrange(ResultWordListList &words, const Common::Array<uint8> &roles, ResultWordListList &fallback) const {
	Common::Array<ResultWordList> tok;
	Common::Array<uint8> role;
	uint i = 0;
	for (ResultWordListList::const_iterator it = words.begin(); it != words.end(); ++it, ++i) {
		tok.push_back(*it);
		role.push_back(i < roles.size() ? roles[i] : (uint8)Common::kKoreanRoleNone);
	}

	// Korean puts the verb last ("옷을 가져와"), the said() specs expect it
	// first ("get clothes"): move a trailing verb to the front.
	// Either verb class: Laura Bow 1's "use" is an indicative verb only,
	// and its said() specs take it as the command all the same.
	const uint16 verbClass = (VOCAB_CLASS_IMPERATIVE_VERB | VOCAB_CLASS_INDICATIVE_VERB) << 4;
	const uint16 nounClass = VOCAB_CLASS_NOUN << 4;
	// A word that is noun and verb at once (KQ1's fiddle) stays the
	// object: the last token is the verb unless the first is a verb and
	// nothing else while the last could be a noun ("look 나무를").
	// A sequence verb's tail (들여다봐: look, in) follows the verb: the
	// trailing prepositions and adverbs are stepped over to find it, and
	// go out right after it.
	uint last = tok.size();
	while (last > 1 && (isPrepositionOnly(tok[last - 1]) || onlyClass(tok[last - 1], VOCAB_CLASS_ADVERB << 4)) &&
	       hasClass(tok[last - 2], verbClass))
		--last;
	int verb = -1;
	if (last > 1 && hasClass(tok[last - 1], verbClass) &&
	    !(hasClass(tok.front(), verbClass) && !hasClass(tok.front(), nounClass) && hasClass(tok[last - 1], nounClass)))
		verb = (int)last - 1;
	else if (!tok.empty() && hasClass(tok.front(), verbClass))
		verb = 0;
	const uint verbTail = (verb >= 0 && (uint)verb == last - 1) ? tok.size() - last : 0;

	// A noun unit is the noun with the bare or 의-marked nouns before it
	// that modify it: 나무 구멍 안을 is "in tree hole", as the game's own
	// "look in tree hole" parses. @p unitStart[k] is the first token of
	// the unit token k ends.
	// An article (그 as "the") belongs to its noun too.
	const uint16 articleClass = VOCAB_CLASS_ARTICLE << 4;
	Common::Array<uint> unitStart;
	for (uint k = 0; k < tok.size(); ++k) {
		uint start = k;
		const bool head = hasClass(tok[k], nounClass) && !isPrepositionOnly(tok[k]);
		while (head && start > 0 && (int)(start - 1) != verb &&
		       ((hasClass(tok[start - 1], nounClass) && !isPrepositionOnly(tok[start - 1]) &&
		         (role[start - 1] == Common::kKoreanRoleNone || role[start - 1] == Common::kKoreanRoleOf)) ||
		        onlyClass(tok[start - 1], articleClass)))
			--start;
		unitStart.push_back(start);
	}

	// The primary arrangement, and the fallback for said() when a case
	// particle became a preposition: the same without those, so a spec
	// that wants the bare noun ("climb [<up] / beanstalk" against 콩나무에
	// 올라가) gets it. Postpositions (안, 대해) are kept in both.
	const bool rolePhrases = arrangeTokens(tok, role, verb, verbTail, unitStart, true, words);
	fallback.clear();
	if (rolePhrases)
		arrangeTokens(tok, role, verb, verbTail, unitStart, false, fallback);
	askAbout(words);
	askAbout(fallback);
}

void KoreanParser::askAbout(ResultWordListList &words) const {
	// "펜러스 물어봐" means "ask about fenrus", not "ask fenrus": Korean
	// often drops 에 대해. A conversation game's specs want the topic after
	// "about" (QFG2: ask , tell / / *), so a bare topic of ask/tell gets it.
	if (words.size() < 2)
		return;
	WordMap::const_iterator about = _words.find("about");
	if (about == _words.end())
		return;
	ResultWordList aboutWord;
	for (ResultWordList::const_iterator r = about->_value.begin(); r != about->_value.end(); ++r)
		if (r->_class & kPrepositionClasses)
			aboutWord.push_back(*r);
	if (aboutWord.empty())
		return;
	bool isAskOrTell = false;
	static const char *const kVerbs[] = { "ask", "tell" };
	for (uint v = 0; v < ARRAYSIZE(kVerbs) && !isAskOrTell; v++) {
		WordMap::const_iterator it = _words.find(kVerbs[v]);
		if (it == _words.end())
			continue;
		for (ResultWordList::const_iterator r = it->_value.begin(); r != it->_value.end() && !isAskOrTell; ++r)
			for (ResultWordList::const_iterator f = words.front().begin(); f != words.front().end() && !isAskOrTell; ++f)
				isAskOrTell = f->_group == r->_group;
	}
	if (!isAskOrTell)
		return;
	const uint16 nounClass = VOCAB_CLASS_NOUN << 4;
	ResultWordListList::iterator second = words.begin();
	++second;
	// "샤민에게 말해" is "talk to shameen": tell with nothing to tell but
	// someone to tell it to becomes the game's talk.
	if (words.size() == 3 && isPrepositionOnly(*second) && second->front()._group != aboutWord.front()._group) {
		WordMap::const_iterator tell = _words.find("tell");
		WordMap::const_iterator talk = _words.find("talk");
		bool isTell = false;
		if (tell != _words.end())
			for (ResultWordList::const_iterator r = tell->_value.begin(); r != tell->_value.end(); ++r)
				isTell = isTell || words.front().front()._group == r->_group;
		if (isTell && talk != _words.end()) {
			words.front() = talk->_value;
			return;
		}
	}
	// Only a topic straight after the verb: "to shameen" (에게) or an
	// "about" already there leave the line as it is.
	if (isPrepositionOnly(*second) || !hasClass(*second, nounClass))
		return;
	for (ResultWordListList::const_iterator t = words.begin(); t != words.end(); ++t)
		for (ResultWordList::const_iterator r = t->begin(); r != t->end(); ++r)
			if (r->_group == aboutWord.front()._group)
				return;
	words.insert(second, aboutWord);
}

bool KoreanParser::arrangeTokens(const Common::Array<ResultWordList> &tok, const Common::Array<uint8> &role, int verb, uint verbTail,
                               const Common::Array<uint> &unitStart, bool useRoles, ResultWordListList &out) const {
	const uint16 nounClass = VOCAB_CLASS_NOUN << 4;
	Common::Array<ResultWordList> plain, phrases;
	uint plainUnits = 0; // tokens of plain that belong to the last unit
	bool rolePhrases = false;
	for (uint k = 0; k < tok.size(); ++k) {
		if ((int)k == verb || (verb >= 0 && k > (uint)verb && k <= (uint)verb + verbTail))
			continue;
		if (isPrepositionOnly(tok[k]) && plainUnits && hasClass(plain.back(), nounClass)) {
			// 구멍 안을 봐: the postposition and its noun unit become "in hole".
			phrases.push_back(tok[k]);
			for (uint u = plain.size() - plainUnits; u < plain.size(); ++u)
				phrases.push_back(plain[u]);
			for (uint u = 0; u < plainUnits; ++u)
				plain.pop_back();
			plainUnits = 0;
			continue;
		}
		// 성배에 대해: the postposition after the noun takes it, not the 에.
		const bool nextIsPostposition = k + 1 < tok.size() && (int)(k + 1) != verb && isPrepositionOnly(tok[k + 1]);
		ResultWordList prep;
		if (useRoles && !isPrepositionOnly(tok[k]) && !nextIsPostposition)
			preposition(role[k], prep);
		if (!prep.empty()) {
			// 나무꾼에게: "to woodcutter", after the plain object.
			rolePhrases = true;
			phrases.push_back(prep);
			for (uint u = unitStart[k]; u <= k; ++u)
				phrases.push_back(tok[u]);
			for (uint u = unitStart[k]; u < k; ++u)
				plain.pop_back();
			plainUnits = 0;
			continue;
		}
		plain.push_back(tok[k]);
		plainUnits = k + 1 - unitStart[k];
	}

	out.clear();
	if (verb >= 0)
		for (uint k = (uint)verb; k <= (uint)verb + verbTail; ++k)
			out.push_back(tok[k]);
	for (uint k = 0; k < plain.size(); ++k)
		out.push_back(plain[k]);
	for (uint k = 0; k < phrases.size(); ++k)
		out.push_back(phrases[k]);
	return rolePhrases;
}

} // End of namespace Sci
```

  `module.mk`: `parser/korean_parser.o \` after `parser/grammar.o \`.
- [ ] **Step 4: green** — N0 + 35 tests OK.
- [ ] **Step 5: commit** — `SCI: Move the Korean word order into KoreanParser`.

---

## Task 6: `KoreanEditLine`

**Files:** new `engines/sci/korean_input.{h,cpp}`, `test/engines/sci/korean_input.h`.

- [ ] **Step 1: test**:

```cpp
#include <cxxtest/TestSuite.h>

#include "sci/korean_input.h"

// U+BB38 문 = EB AC B8, U+C744 을 = EC 9D 84, U+C5F4 열 = EC 97 B4, U+3141 ㅁ = E3 85 81
class SciKoreanInputTestSuite : public CxxTest::TestSuite {
	typedef Sci::KoreanEditLine L;

	static void type(L &line, const char *keys, Common::String &text, uint &cursor, uint max = 40) {
		for (const char *k = keys; *k; ++k)
			line.key((byte)*k, false, true, 0, text, cursor, max);
	}

public:
	void test_composesSyllablesInPlace() {
		L line;
		Common::String text;
		uint cursor = 0;
		line.key('a', false, true, 0, text, cursor, 40);
		TS_ASSERT_EQUALS(text, "\xE3\x85\x81");     // pre-edit ㅁ is in the text
		TS_ASSERT(line.isComposing());
		type(line, "ns", text, cursor);
		TS_ASSERT_EQUALS(text, "\xEB\xAC\xB8");     // 문
		type(line, "dmf", text, cursor);
		TS_ASSERT_EQUALS(text, "\xEB\xAC\xB8\xEC\x9D\x84"); // 문을
		TS_ASSERT_EQUALS(cursor, text.size());
		line.key(' ', false, true, 0, text, cursor, 40);
		TS_ASSERT(!line.isComposing());
		type(line, "duf", text, cursor);
		TS_ASSERT_EQUALS(text, "\xEB\xAC\xB8\xEC\x9D\x84 \xEC\x97\xB4"); // 문을 열
	}

	void test_backspaceTakesJamoThenSyllables() {
		L line;
		Common::String text;
		uint cursor = 0;
		type(line, "ans", text, cursor);
		line.key(L::kKeyBackspace, false, true, 0, text, cursor, 40);
		TS_ASSERT_EQUALS(text, "\xEB\xAC\xB4");     // 무
		line.key(L::kKeyBackspace, false, true, 0, text, cursor, 40);
		line.key(L::kKeyBackspace, false, true, 0, text, cursor, 40);
		TS_ASSERT(text.empty());
		TS_ASSERT(!line.isComposing());
		text = "ab\xEB\xAC\xB8";
		cursor = text.size();
		line.key(L::kKeyBackspace, false, true, 0, text, cursor, 40);
		TS_ASSERT_EQUALS(text, "ab");               // a whole code point
		TS_ASSERT_EQUALS(cursor, 2u);
	}

	void test_cursorStepsByCodePoint() {
		L line;
		Common::String text("\xEB\xAC\xB8x");
		uint cursor = text.size();
		line.key(L::kKeyLeft, false, false, 0, text, cursor, 40);
		TS_ASSERT_EQUALS(cursor, 3u);
		line.key(L::kKeyLeft, false, false, 0, text, cursor, 40);
		TS_ASSERT_EQUALS(cursor, 0u);
		line.key(L::kKeyDelete, false, false, 0, text, cursor, 40);
		TS_ASSERT_EQUALS(text, "x");
	}

	void test_englishModeAndOverflow() {
		L line;
		Common::String text;
		uint cursor = 0;
		TS_ASSERT_EQUALS(line.key('a', false, false, 0, text, cursor, 2), L::kChanged);
		TS_ASSERT_EQUALS(text, "a");
		TS_ASSERT_EQUALS(line.key('a', false, true, 0, text, cursor, 2), L::kRefused); // 3 bytes do not fit
		TS_ASSERT_EQUALS(text, "a");
		TS_ASSERT(!line.isComposing());
	}

	void test_osImeSyllable() {
		L line;
		Common::String text("x");
		uint cursor = 1;
		// The script opened the prompt with the surrogate 'x' in it.
		TS_ASSERT(line.adopt(text, cursor, false, 0xBB38, 40));
		TS_ASSERT_EQUALS(text, "\xEB\xAC\xB8");
		line.remember(text);
		TS_ASSERT_EQUALS(line.key('x', false, false, 0xC744, text, cursor, 40), L::kChanged);
		TS_ASSERT_EQUALS(text, "\xEB\xAC\xB8\xEC\x9D\x84");
	}

	void test_firstKeyOfANewPromptIsComposed() {
		L line;
		Common::String text("a");
		uint cursor = 1;
		TS_ASSERT(line.adopt(text, cursor, true, 0, 40));
		TS_ASSERT_EQUALS(text, "\xE3\x85\x81");
		line.remember(text);
		TS_ASSERT(!line.adopt(text, cursor, true, 0, 40));
		type(line, "ns", text, cursor);
		TS_ASSERT_EQUALS(text, "\xEB\xAC\xB8");
	}

	void test_undo() {
		L line;
		Common::String text;
		uint cursor = 0;
		type(line, "an", text, cursor);
		line.key('s', false, true, 0, text, cursor, 40);
		line.undo(text, cursor);
		TS_ASSERT_EQUALS(text, "\xEB\xAC\xB4");     // 무 again
		line.key('s', false, true, 0, text, cursor, 40);
		TS_ASSERT_EQUALS(text, "\xEB\xAC\xB8");
	}
};
```

- [ ] **Step 2: red** — `sci/korean_input.h: No such file or directory`.
- [ ] **Step 3: implement** — header:

```cpp
#ifndef SCI_KOREAN_INPUT_H
#define SCI_KOREAN_INPUT_H

#include "common/hangul.h"
#include "common/str.h"

namespace Sci {

/**
 * The text of a SCI edit control (the parser prompt) while a Korean
 * (TextMode::kUtf8) game runs: UTF-8 bytes, a cursor that is a byte offset
 * on a code point boundary, Hangul composed by the built-in two-set
 * composer (DESIGN.md section 6) and finished syllables of an OS input
 * method. Knows nothing of the engine: GfxControls16 hands it the keys.
 *
 * The syllable being composed is written into the text as it grows (the
 * pre-edit is drawn as text), from runStart() to the cursor.
 */
class KoreanEditLine {
public:
	/** SCI key codes the line handles (engines/sci/event.h kSciKey*). */
	enum {
		kKeyBackspace = 8,
		kKeyEtx = 3,
		kKeyHome = 0x4700,
		kKeyUp = 0x4800,
		kKeyLeft = 0x4B00,
		kKeyRight = 0x4D00,
		kKeyEnd = 0x4F00,
		kKeyDelete = 0x5300
	};
	/** The key the event manager gives a script for an OS-IME syllable (SCI0 opens the prompt only on ASCII). */
	enum { kOsImeSurrogateKey = 'x' };

	enum Result {
		kIgnored, ///< not a key of the line
		kChanged, ///< text or cursor changed
		kRefused  ///< the key would overflow the field: nothing changed
	};

	KoreanEditLine() : _runStart(0), _composing(false) {}

	/**
	 * Before a key: a text the line did not write last (another control,
	 * the script changed it) drops the composition. A prompt the script just
	 * opened with its first key in it is taken over: the surrogate 'x' of an
	 * OS-IME syllable @p osImeCp becomes that syllable, and in Hangul mode a
	 * jamo key becomes the first jamo. True when @p text changed.
	 */
	bool adopt(Common::String &text, uint &cursor, bool hangulMode, uint32 osImeCp, uint maxBytes);

	/**
	 * One key. @p ctrl: Control held. @p osImeCp: the syllable behind a
	 * surrogate key (0 for none). @p maxBytes: the field's room.
	 */
	Result key(uint16 key, bool ctrl, bool hangulMode, uint32 osImeCp, Common::String &text, uint &cursor, uint maxBytes);

	/** Undoes the last key that returned kChanged (the field could not show it). */
	void undo(Common::String &text, uint &cursor);

	/** Ends the composition; the syllable stays in the text. */
	void commit() { _composing = false; _hangul.reset(); }
	/** After the control wrote @p text back. */
	void remember(const Common::String &text) { _last = text; }

	bool isComposing() const { return _composing; }
	uint runStart() const { return _runStart; }

	static uint prevBoundary(const Common::String &text, uint pos);
	static uint nextBoundary(const Common::String &text, uint pos);
	static Common::String encode(uint32 cp);

private:
	/** The composer's text in place of [runStart, cursor). */
	bool writeRun(Common::String &text, uint &cursor, uint maxBytes);
	void snapshot(const Common::String &text, uint cursor);

	Common::HangulComposer _hangul;
	uint _runStart;
	bool _composing;
	Common::String _last;

	Common::HangulComposer _undoHangul;
	uint _undoRunStart;
	bool _undoComposing;
	Common::String _undoText;
	uint _undoCursor;
};

} // End of namespace Sci

#endif
```

  Source:

```cpp
#include "sci/korean_input.h"
#include "sci/utf8.h"

namespace Sci {

Common::String KoreanEditLine::encode(uint32 cp) {
	byte buf[4];
	const int n = encodeUtf8Char(cp, buf);
	return Common::String((const char *)buf, n);
}

uint KoreanEditLine::prevBoundary(const Common::String &text, uint pos) {
	return pos == 0 ? 0 : utf8PrevBoundary((const byte *)text.c_str(), text.size(), pos);
}

uint KoreanEditLine::nextBoundary(const Common::String &text, uint pos) {
	return utf8NextBoundary((const byte *)text.c_str(), text.size(), pos);
}

void KoreanEditLine::snapshot(const Common::String &text, uint cursor) {
	_undoHangul = _hangul;
	_undoRunStart = _runStart;
	_undoComposing = _composing;
	_undoText = text;
	_undoCursor = cursor;
}

void KoreanEditLine::undo(Common::String &text, uint &cursor) {
	_hangul = _undoHangul;
	_runStart = _undoRunStart;
	_composing = _undoComposing;
	text = _undoText;
	cursor = _undoCursor;
}

bool KoreanEditLine::writeRun(Common::String &text, uint &cursor, uint maxBytes) {
	const Common::U32String run = _hangul.text();
	Common::String bytes;
	for (uint i = 0; i < run.size(); ++i)
		bytes += encode(run[i]);
	const Common::String head(text.c_str(), _runStart);
	const Common::String tail(text.c_str() + cursor);
	if (head.size() + bytes.size() + tail.size() > maxBytes)
		return false;
	text = head + bytes + tail;
	cursor = _runStart + bytes.size();
	if (_hangul.empty())
		_composing = false;
	return true;
}

bool KoreanEditLine::adopt(Common::String &text, uint &cursor, bool hangulMode, uint32 osImeCp, uint maxBytes) {
	if (text == _last)
		return false;
	commit();
	_last = text;
	if (text.size() != 1 || cursor != 1)
		return false;
	if (osImeCp >= 0x100 && text[0] == kOsImeSurrogateKey) {
		const Common::String s = encode(osImeCp);
		if (s.size() > maxBytes)
			return false;
		text = s;
		cursor = s.size();
		return true;
	}
	if (hangulMode && Common::HangulComposer::isJamoKey(text[0])) {
		const char first = text[0];
		text.clear();
		cursor = 0;
		_runStart = 0;
		_composing = true;
		_hangul.feed(first);
		return writeRun(text, cursor, maxBytes);
	}
	return false;
}

KoreanEditLine::Result KoreanEditLine::key(uint16 key, bool ctrl, bool hangulMode, uint32 osImeCp,
                                            Common::String &text, uint &cursor, uint maxBytes) {
	snapshot(text, cursor);
	if (cursor > text.size())
		cursor = text.size();

	if (osImeCp >= 0x100 && key == kOsImeSurrogateKey && !ctrl) {
		commit();
		const Common::String s = encode(osImeCp);
		if (text.size() + s.size() > maxBytes)
			return kRefused;
		text.insertString(s, cursor);
		cursor += s.size();
		return kChanged;
	}

	if (hangulMode && !ctrl && key < 0x80 && Common::HangulComposer::isJamoKey((char)key)) {
		if (!_composing) {
			_hangul.reset();
			_runStart = cursor;
			_composing = true;
		}
		_hangul.feed((char)key);
		if (!writeRun(text, cursor, maxBytes)) {
			undo(text, cursor);
			return kRefused;
		}
		return kChanged;
	}

	if (key == kKeyBackspace && _composing) {
		_hangul.backspace();
		writeRun(text, cursor, maxBytes);
		return kChanged;
	}

	// Any other key ends the syllable, then acts on code points.
	commit();
	switch (key) {
	case kKeyBackspace:
		if (cursor == 0)
			return kIgnored;
		{
			const uint from = prevBoundary(text, cursor);
			text.erase(from, cursor - from);
			cursor = from;
		}
		return kChanged;
	case kKeyDelete:
		if (cursor >= text.size())
			return kIgnored;
		text.erase(cursor, nextBoundary(text, cursor) - cursor);
		return kChanged;
	case kKeyHome:
		cursor = 0;
		return kChanged;
	case kKeyEnd:
		cursor = text.size();
		return kChanged;
	case kKeyLeft:
		if (cursor == 0)
			return kIgnored;
		cursor = prevBoundary(text, cursor);
		return kChanged;
	case kKeyRight:
		if (cursor >= text.size())
			return kIgnored;
		cursor = nextBoundary(text, cursor);
		return kChanged;
	case kKeyEtx:
		if (!ctrl)
			return kIgnored;
		text.clear();
		cursor = 0;
		return kChanged;
	default:
		break;
	}
	if (ctrl && key == 'c') {
		text.clear();
		cursor = 0;
		return kChanged;
	}
	if (key > 31 && key < 127 && !ctrl) {
		if (text.size() + 1 > maxBytes)
			return kRefused;
		text.insertChar((char)key, cursor++);
		return kChanged;
	}
	return kIgnored;
}

} // End of namespace Sci
```

  `module.mk`: `korean_input.o \` after `event.o \`.
- [ ] **Step 4: green** — N0 + 42 tests OK.
- [ ] **Step 5: commit** — `SCI: Add KoreanEditLine, the parser prompt's Korean editing`.

---

## Task 7: `GfxFont` takes a `uint32` code point

**Files:** `engines/sci/graphics/{scifont.h,scifont.cpp,fontsjis.h,fontsjis.cpp,fontkorean.h,fontkorean.cpp}`.

- [ ] **Step 1: edit** (mechanical; every `uint16 chr` parameter of the `GfxFont` interface and its three subclasses):

```sh
cd $SRC/engines/sci/graphics
sed -i -E 's/(isDoubleByte|getCharWidth|getCharHeight|getCharData)\(uint16 chr\)/\1(uint32 chr)/; s/(draw|drawToBuffer)\(uint16 chr,/\1(uint32 chr,/' \
	scifont.h scifont.cpp fontsjis.h fontsjis.cpp fontkorean.h fontkorean.cpp
grep -c "uint32 chr" scifont.h scifont.cpp fontsjis.h fontsjis.cpp fontkorean.h fontkorean.cpp   # 10 5 3 3 4 4
grep -n "uint16 chr" scifont.* fontsjis.* fontkorean.*                                           # nothing
```

  The resulting header diffs (for review; the `.cpp` definitions change the same way):

```diff
--- a/engines/sci/graphics/scifont.h
+++ b/engines/sci/graphics/scifont.h
@@ -41,11 +41,11 @@
 
 	virtual GuiResourceId getResourceId() { return 0; }
 	virtual byte getHeight() { return 0; }
-	virtual bool isDoubleByte(uint16 chr) { return false; }
-	virtual byte getCharWidth(uint16 chr) { return 0; }
-	virtual byte getCharHeight(uint16 chr) { return 0; }
-	virtual void draw(uint16 chr, int16 top, int16 left, byte color, bool greyedOutput) {}
-	virtual void drawToBuffer(uint16 chr, int16 top, int16 left, byte color, bool greyedOutput, byte *buffer, int16 width, int16 height) {}
+	virtual bool isDoubleByte(uint32 chr) { return false; }
+	virtual byte getCharWidth(uint32 chr) { return 0; }
+	virtual byte getCharHeight(uint32 chr) { return 0; }
+	virtual void draw(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput) {}
+	virtual void drawToBuffer(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput, byte *buffer, int16 width, int16 height) {}
 };
 
 
@@ -60,16 +60,16 @@
 
 	GuiResourceId getResourceId() override;
 	uint8 getHeight() override;
-	uint8 getCharWidth(uint16 chr) override;
-	void draw(uint16 chr, int16 top, int16 left, byte color, bool greyedOutput) override;
+	uint8 getCharWidth(uint32 chr) override;
+	void draw(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput) override;
 #ifdef ENABLE_SCI32
 	// SCI2/2.1 equivalent
-	void drawToBuffer(uint16 chr, int16 top, int16 left, byte color, bool greyedOutput, byte *buffer, int16 width, int16 height) override;
+	void drawToBuffer(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput, byte *buffer, int16 width, int16 height) override;
 #endif
 
 private:
-	uint8 getCharHeight(uint16 chr) override;
-	SciSpan<const byte> getCharData(uint16 chr);
+	uint8 getCharHeight(uint32 chr) override;
+	SciSpan<const byte> getCharData(uint32 chr);
 
 	ResourceManager *_resMan;
 	GfxScreen *_screen;
```

```diff
--- a/engines/sci/graphics/fontsjis.h
+++ b/engines/sci/graphics/fontsjis.h
@@ -40,9 +40,9 @@
 
 	GuiResourceId getResourceId() override;
 	byte getHeight() override;
-	bool isDoubleByte(uint16 chr) override;
-	byte getCharWidth(uint16 chr) override;
-	void draw(uint16 chr, int16 top, int16 left, byte color, bool greyedOutput) override;
+	bool isDoubleByte(uint32 chr) override;
+	byte getCharWidth(uint32 chr) override;
+	void draw(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput) override;
 
 private:
 	GfxScreen *_screen;
```

```diff
--- a/engines/sci/graphics/fontkorean.h
+++ b/engines/sci/graphics/fontkorean.h
@@ -40,12 +40,12 @@
 
 	GuiResourceId getResourceId();
 	byte getHeight();
-	bool isDoubleByte(uint16 chr);
-	byte getCharWidth(uint16 chr);
-	void draw(uint16 chr, int16 top, int16 left, byte color, bool greyedOutput);
+	bool isDoubleByte(uint32 chr);
+	byte getCharWidth(uint32 chr);
+	void draw(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput);
 #ifdef ENABLE_SCI32
 	// SCI2/2.1 equivalent
-	void drawToBuffer(uint16 chr, int16 top, int16 left, byte color, bool greyedOutput, byte *buffer, int16 width, int16 height);
+	void drawToBuffer(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput, byte *buffer, int16 width, int16 height);
 #endif
 
 private:
```

- [ ] **Step 2: check** — `heavy make -j8 scummvm test`: links, N0 + 42 tests OK (callers pass `uint16`, which widens).
  With SCI32 (it calls `drawToBuffer`): `--enable-engine=sci,sci32` builds (section 0).
- [ ] **Step 3: commit** — `SCI: Pass a code point to GfxFont as uint32` (body: a UTF-8 game passes code points; packed
  double-byte values are unchanged; upstreamable, DESIGN section 9).

---

## Task 8: `SciHiResText` and `GfxFontHiRes`

**Files:** new `engines/sci/graphics/hirestext.{h,cpp}`, `test/engines/sci/hirestext.h`, `engines/sci/module.mk`.

- [ ] **Step 1: test** (the P1 fixture face `JAMO.SVF` through `HiResTest::MemoryFiles`):

```cpp
#include <cxxtest/TestSuite.h>

#include "test/graphics/hires_support.h"
#include "test/system/null_osystem.h"

#include "sci/graphics/hirestext.h"

class SciHiResTextTestSuite : public CxxTest::TestSuite {
	// The game's own font: 4 px wide, 8 high; records what it draws.
	class FakeFont : public Sci::GfxFont {
	public:
		Common::String *drawn;
		explicit FakeFont(Common::String *d) : drawn(d) {}
		byte getHeight() override { return 8; }
		byte getCharWidth(uint32 chr) override { return 4; }
		void draw(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput) override {
			*drawn += Common::String::format("%c@%d,%d ", (char)chr, left, top);
		}
	};

	static const char *gameMap() {
		return "[map]\nversion=3\n[fonts]\njamo=data:fixture/JAMO.SVF\n"
		       "[font]\nface=jamo\nrange.basic-latin=original\n"
		       "[font.300:clut8]\nshift=2\n";
	}

	static uint32 allTargets() {
		return (1 << Graphics::kHiResTargetClut8) | (1 << Graphics::kHiResTargetRgb565) | (1 << Graphics::kHiResTargetRgb888);
	}

public:
	void setUp() override {
		Common::install_null_g_system();
	}
	void tearDown() override {
		Common::uninstall_null_g_system();
	}

	void test_picksTheGameMap() {
		HiResTest::MemoryFiles files;
		files.addText("game/ko/HIRESVF.MAP", gameMap());
		Sci::SciHiResText t;
		Common::Array<Common::String> notes;
		TS_ASSERT(t.setup(&files, Graphics::HiResOptions(), Common::Path("game"), "ko", allTargets(), notes));
		TS_ASSERT(t.isActive());
		TS_ASSERT_EQUALS(t.getPick().path.toString('/'), "game/ko/HIRESVF.MAP");
		// 1 bpp face: CLUT8 (the stamp) even where RGB is offered.
		TS_ASSERT_EQUALS(t.getTarget(), Graphics::kHiResTargetClut8);
		TS_ASSERT(!t.wantsRgb());
		TS_ASSERT(t.getFontSet(0));
		TS_ASSERT_EQUALS(t.getFontSet(300)->getShift(), 2); // the :clut8 section was merged
	}

	void test_fallsBackToTheEngineMap() {
		HiResTest::MemoryFiles files;
		files.addText("data/hires_text/maps/korean-default.map",
		              "[map]\nversion=3\n[fonts]\njamo=data:fixture/JAMO.SVF\n[font]\nface=jamo\n");
		Sci::SciHiResText t;
		Common::Array<Common::String> notes;
		TS_ASSERT(t.setup(&files, Graphics::HiResOptions(), Common::Path("game"), "ko", allTargets(), notes));
		TS_ASSERT_EQUALS(notes.back(), Common::String("using ") + Sci::SciHiResText::kDefaultMap);
	}

	void test_offWhenTheKeySaysSo() {
		HiResTest::MemoryFiles files;
		files.addText("game/ko/HIRESVF.MAP", gameMap());
		Graphics::HiResOptions o;
		o.enabled = false;
		Sci::SciHiResText t;
		Common::Array<Common::String> notes;
		TS_ASSERT(!t.setup(&files, o, Common::Path("game"), "ko", allTargets(), notes));
		TS_ASSERT(!t.getFontSet(0));
	}

	void test_fontDrawsHangulIntoThePlane() {
		HiResTest::MemoryFiles files;
		files.addText("game/ko/HIRESVF.MAP", gameMap());
		Sci::SciHiResText t;
		Common::Array<Common::String> notes;
		TS_ASSERT(t.setup(&files, Graphics::HiResOptions(), Common::Path("game"), "ko", allTargets(), notes));
		TS_ASSERT(t.createPlane(320, 200));
		Common::String drawn;
		Sci::GfxFontHiRes font(new FakeFont(&drawn), *t.getFontSet(0), t.getPlane());
		TS_ASSERT(font.getHeight() >= 8);
		TS_ASSERT_EQUALS(font.getCharWidth('A'), 4);          // the game's font
		const byte w = font.getCharWidth(0xAC00);              // 가: the face, low-res
		TS_ASSERT(w >= 4 && w <= 16);
		font.draw('A', 10, 20, 15, false);
		TS_ASSERT_EQUALS(drawn, "A@20,10 ");
		font.draw(0x18, 10, 30, 15, false);                    // a SCI symbol below U+0020: the game's
		TS_ASSERT_EQUALS(drawn, "A@20,10 \x18@30,10 ");
		TS_ASSERT(t.getPlane().isEmpty());
		font.draw(0xAC00, 10, 20, 15, false);
		TS_ASSERT(!t.getPlane().isEmpty());
		// Ink only inside the character's hi-res box, in colour 15.
		int ink = 0;
		for (int y = 0; y < t.getPlane().getHeight(); ++y)
			for (int x = 0; x < t.getPlane().getWidth(); ++x)
				if (t.getPlane().getCoverage(x, y)) {
					++ink;
					TS_ASSERT(x >= 40 && x < 40 + 2 * w && y >= 20 && y < 20 + 2 * font.getHeight());
					TS_ASSERT_EQUALS(t.getPlane().getIndex(x, y), 15);
				}
		TS_ASSERT(ink > 10);
		// A low-res fill over the character clears it (what GfxScreen does).
		t.getPlane().clearLowresRect(Common::Rect(20, 10, 20 + w, 10 + font.getHeight()));
		TS_ASSERT(t.getPlane().isEmpty());
	}
};
```

- [ ] **Step 2: red** — `sci/graphics/hirestext.h: No such file or directory`.
- [ ] **Step 3: implement** — header:

```cpp
#ifndef SCI_GRAPHICS_HIRESTEXT_H
#define SCI_GRAPHICS_HIRESTEXT_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/noncopyable.h"
#include "common/path.h"
#include "common/str.h"

#include "graphics/hires_text/font_set.h"
#include "graphics/hires_text/hires_files.h"
#include "graphics/hires_text/hires_options.h"
#include "graphics/hires_text/map_pick.h"
#include "graphics/hires_text/text_compose.h"
#include "graphics/hires_text/text_plane.h"

#include "sci/graphics/scifont.h"

namespace Sci {

/**
 * The hi-res text of one SCI game (DESIGN.md section 5.1): the picked map,
 * its faces, one Graphics::HiResFontSet per SCI font id, and the 2x text
 * plane GfxScreen keeps in step and UpscaledGfxDriver composites.
 */
class SciHiResText : Common::NonCopyable {
public:
	/** The map the engine falls back to when the game has none: the shipped OFL face. */
	static const char *const kDefaultMap;
	enum { kScale = 2 };

	SciHiResText();
	~SciHiResText();

	/**
	 * Reads the hires_text_* keys, picks the game's map (language folder,
	 * game folder, else kDefaultMap), resolves the render target against the
	 * backend's @p offeredTargets (Graphics::hiResTargetsOffered()) and loads
	 * the map again with that target's qualifiers. False: no hi-res text
	 * (the game draws as upstream). Each step a user may wonder about leaves
	 * a line in @p notes.
	 */
	bool setup(const Common::Path &gameDir, const Common::String &langFolder, uint32 offeredTargets,
	           Common::Array<Common::String> &notes);
	/** setup() with files of the caller's own (tests). */
	bool setup(Graphics::HiResFiles *files, const Graphics::HiResOptions &options, const Common::Path &gameDir,
	           const Common::String &langFolder, uint32 offeredTargets, Common::Array<Common::String> &notes);

	bool isActive() const { return _active; }
	Graphics::HiResRenderTarget getTarget() const { return _target; }
	bool wantsRgb() const { return _active && _target != Graphics::kHiResTargetClut8; }
	Graphics::TextCompose::Mode getComposeMode() const { return Graphics::composeMode(_target, _blend); }
	const Graphics::HiResPick &getPick() const { return _pick; }
	/** Line breaking: the map's [layout], then hangul_break. */
	const Graphics::BreakRules &getBreakRules() const { return _breakRules; }

	/** The plane for a @p lowW x @p lowH screen (created on the first call). */
	Graphics::TextPlane &getPlane() { return _plane; }
	bool createPlane(int lowW, int lowH) { return _plane.exists() || _plane.create(lowW, lowH, kScale); }

	/** The font set of SCI font @p fontId, built on first use; nullptr when the map leaves the id to the game's font. */
	const Graphics::HiResFontSet *getFontSet(int fontId);

private:
	bool anyCoverageFace(Common::Array<Common::String> &notes);

	bool _active;
	Graphics::HiResFiles *_files;
	bool _ownFiles;
	Graphics::HiResFaceCache *_faces;
	Graphics::HiResPick _pick;
	Graphics::HiResRenderTarget _target;
	Graphics::HiResBlend _blend;
	Graphics::BreakRules _breakRules;
	Graphics::TextPlane _plane;
	Common::HashMap<int, Graphics::HiResFontSet *> _sets; ///< nullptr: the id is the game's font
};

/**
 * A SCI font whose code points the map routes to faces are drawn into the
 * text plane at SciHiResText::kScale; the rest (range ...=original) by the
 * game's own font. Widths are low-res pixels, rounded up.
 */
class GfxFontHiRes : public GfxFont {
public:
	/** Takes @p original (deleted with this font); @p set and @p plane belong to the SciHiResText. */
	GfxFontHiRes(GfxFont *original, const Graphics::HiResFontSet &set, Graphics::TextPlane &plane);
	~GfxFontHiRes() override;

	GuiResourceId getResourceId() override { return _original->getResourceId(); }
	byte getHeight() override { return _height; }
	byte getCharWidth(uint32 chr) override;
	void draw(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput) override;

	GfxFont *getOriginal() const { return _original; }

private:
	bool isOriginal(uint32 chr) const;

	GfxFont *_original;
	const Graphics::HiResFontSet &_set;
	Graphics::TextPlane &_plane;
	byte _height;
	int _lineHeight; ///< hi-res px of the faces' ascent + descent
};

} // End of namespace Sci

#endif
```

  Source:

```cpp
#include "common/ptr.h"
#include "common/stream.h"
#include "common/textconsole.h"

#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/id_plan.h"
#include "graphics/hires_text/svfchain.h"
#include "graphics/hires_text/svfface.h"

#include "sci/graphics/hirestext.h"

namespace Sci {

const char *const SciHiResText::kDefaultMap = "data:hires_text/maps/korean-default.map";

SciHiResText::SciHiResText() : _active(false), _files(nullptr), _ownFiles(false), _faces(nullptr),
	_target(Graphics::kHiResTargetClut8), _blend(Graphics::kHiResBlendAuto) {
}

SciHiResText::~SciHiResText() {
	for (Common::HashMap<int, Graphics::HiResFontSet *>::iterator it = _sets.begin(); it != _sets.end(); ++it)
		delete it->_value;
	delete _faces;
	if (_ownFiles)
		delete _files;
}

bool SciHiResText::setup(const Common::Path &gameDir, const Common::String &langFolder, uint32 offeredTargets,
                         Common::Array<Common::String> &notes) {
	Common::Array<Common::String> warnings;
	const Graphics::HiResOptions options = Graphics::readHiResOptions(warnings);
	notes.push_back(warnings);
	Graphics::HiResFiles *files = new Graphics::HiResDiskFiles();
	const bool ok = setup(files, options, gameDir, langFolder, offeredTargets, notes);
	_ownFiles = true;
	return ok;
}

bool SciHiResText::setup(Graphics::HiResFiles *files, const Graphics::HiResOptions &options, const Common::Path &gameDir,
                         const Common::String &langFolder, uint32 offeredTargets, Common::Array<Common::String> &notes) {
	_files = files;
	_ownFiles = false;
	Graphics::HiResMapQualifiers q;
	q.engine = "sci";
	if (!Graphics::pickHiResMap(options, gameDir, langFolder, q, *_files, _pick, notes)) {
		if (!options.enabled || (options.mapSet && options.map.empty()))
			return false;
		// No map of the game's own: the engine's (DESIGN.md section 5.2, the
		// last face of every chain is NEODG16A).
		Graphics::HiResOptions fallback = options;
		fallback.mapSet = true;
		fallback.map = kDefaultMap;
		if (!Graphics::pickHiResMap(fallback, gameDir, langFolder, q, *_files, _pick, notes))
			return false;
		notes.push_back(Common::String("using ") + kDefaultMap);
	}
	_faces = new Graphics::HiResFaceCache(*_files);

	Common::String note;
	_target = Graphics::resolveRenderTarget(options, &_pick.map, anyCoverageFace(notes), offeredTargets, note);
	if (!note.empty())
		notes.push_back(note);
	_blend = Graphics::resolveBlend(options, &_pick.map);

	// Load the map again with the target's sections ([font:clut8], [font:rgb]).
	q.target = _target;
	Common::ScopedPtr<Common::SeekableReadStream> s(_files->open(_pick.path));
	Graphics::HiResMap map;
	if (s && Graphics::HiResFontMap::load(*s, _pick.path.baseName(), _pick.path.getParent(), q, map))
		_pick.map = map;
	else
		notes.push_back("the map did not load again with the target's sections");
	for (uint i = 0; i < _pick.map.warnings.size(); ++i)
		notes.push_back(_pick.map.warnings[i]);
	_breakRules = _pick.map.layout.apply(Graphics::BreakRules());
	if (options.hangulBreakSet)
		_breakRules.hangul = options.hangulBreak;
	_active = true;
	return true;
}

bool SciHiResText::anyCoverageFace(Common::Array<Common::String> &notes) {
	bool any = false;
	for (Graphics::HiResFaceNames::const_iterator it = _pick.map.faces.begin(); it != _pick.map.faces.end(); ++it) {
		Graphics::SvfFace *face = _faces->get(it->_value, _pick.map.loadModeFor(it->_value), notes);
		any = any || (face && face->getBpp() > 1);
	}
	return any;
}

const Graphics::HiResFontSet *SciHiResText::getFontSet(int fontId) {
	if (!_active)
		return nullptr;
	if (_sets.contains(fontId))
		return _sets[fontId];
	Common::Array<Common::String> warnings;
	Graphics::HiResFontSet *set = new Graphics::HiResFontSet();
	if (!set->build(Graphics::compileIdPlan(_pick.map, fontId, warnings), _pick.map, *_faces, warnings)) {
		delete set;
		set = nullptr;
	}
	for (uint i = 0; i < warnings.size(); ++i)
		warning("Hi-res text, font %d: %s", fontId, warnings[i].c_str());
	_sets[fontId] = set;
	return set;
}

GfxFontHiRes::GfxFontHiRes(GfxFont *original, const Graphics::HiResFontSet &set, Graphics::TextPlane &plane) :
	_original(original), _set(set), _plane(plane) {
	_lineHeight = _set.getAscent() + _set.getDescent();
	const int s = SciHiResText::kScale;
	_height = (byte)MAX<int>(_original->getHeight(), (_lineHeight + s - 1) / s);
}

GfxFontHiRes::~GfxFontHiRes() {
	delete _original;
}

// SCI fonts keep symbols (the list arrows of the file dialogs) below U+0020:
// never text, always the game's glyph.
bool GfxFontHiRes::isOriginal(uint32 chr) const {
	return chr < 0x20 || _set.route(chr) < 0;
}

byte GfxFontHiRes::getCharWidth(uint32 chr) {
	if (isOriginal(chr))
		return _original->getCharWidth(chr);
	const int s = SciHiResText::kScale;
	return (byte)MIN<int>(255, (_set.getAdvance(chr) + s - 1) / s);
}

void GfxFontHiRes::draw(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput) {
	if (isOriginal(chr)) {
		_original->draw(chr, top, left, color, greyedOutput);
		return;
	}
	// The faces' line is centred in the SCI line; shift= moves it down.
	const int s = SciHiResText::kScale;
	const int baseline = top * s + (_height * s - _lineHeight) / 2 + _set.getAscent() + _set.getShift();
	_set.drawChar(chr, left * s, baseline, _plane, color);
}

} // End of namespace Sci
```

- [ ] **Step 4: module.mk** — this phase's six objects in their places:

```diff
--- a/engines/sci/module.mk
+++ b/engines/sci/module.mk
@@ -5,8 +5,11 @@
 	detection_internal.o \
 	dialogs.o \
 	event.o \
+	korean_input.o \
 	metaengine.o \
 	sci.o \
+	textmode.o \
+	utf8.o \
 	util.o \
 	engine/features.o \
 	engine/file.o \
@@ -33,6 +36,7 @@
 	engine/script.o \
 	engine/scriptdebug.o \
 	engine/script_patches.o \
+	engine/script_strings.o \
 	engine/selector.o \
 	engine/seg_manager.o \
 	engine/segment.o \
@@ -50,6 +54,7 @@
 	graphics/cursor.o \
 	graphics/fontkorean.o \
 	graphics/fontsjis.o \
+	graphics/hirestext.o \
 	graphics/macfont.o \
 	graphics/maciconbar.o \
 	graphics/menu.o \
@@ -80,6 +85,7 @@
 	graphics/drivers/win16col.o \
 	graphics/drivers/win256col.o \
 	parser/grammar.o \
+	parser/korean_parser.o \
 	parser/said.o \
 	parser/vocabulary.o \
 	resource/decompressor.o \
```

- [ ] **Step 5: green** — N0 + 46 tests OK. `./test/runner SciHiResTextTestSuite`: 4 tests OK.
- [ ] **Step 6: commit** — `SCI: Add SciHiResText, the faces and text plane of a Korean game` (body: picks the map,
  falls back to the shipped one, resolves the target and reloads the map with its sections, one font set per font id;
  GfxFontHiRes draws routed code points into the plane, the rest and SCI's symbols below U+0020 with the game's font).

### Review checkpoint A (after Task 8)

Full `make test` (N0 + 46), ASan build of the same tree (`--enable-asan`, section "Acceptance") on the six SCI suites,
`git diff --stat d7f47c73a07..` shows only `engines/sci` new files, the six font files, `util.h`, `module.mk`s,
`Makefile.common`, `ports.mk`, the map. Read: `ScriptStrings::loadFromStream` refuses on a bad first line before any entry
is kept; `KoreanEditLine::writeRun` never leaves `cursor` outside `text`; `GfxFontHiRes` widths round up (a 15 px Hangul
advance is 8 low-res px); the `SciHiResText` destructor deletes sets before the face cache. A review subagent may be used
(fresh context, give it this plan section and the diff, not the prototype).

---

## Task 9: The engine decides the text mode; script string keys

**Files:** `engines/sci/sci.h`, `engines/sci/sci.cpp`, `engines/sci/engine/{state.cpp,script.h,script.cpp,seg_manager.h,seg_manager.cpp}`,
`engines/sci/detection.cpp` (debug channel only).

This task adds every `SciEngine` member P3 needs (text mode, script strings, hi-res text, the `SciAction` enum used in
Task 17), the language folder, the fingerprint and the table load, and the string keys the translation lookups use
(`Script::stringIdAtOffset`, `SegManager::stringKey`, `SciEngine::translated/keyOf/strSplitHeap`). The hi-res setup block
in `run()` does nothing visible until the driver and screen hooks (Tasks 13-14) exist. The files depend on each other
(`state.cpp` defines what `sci.h` declares and calls `stringKey`), so they build and commit together.

- [ ] **Step 1: sci.h**:

```diff
--- a/engines/sci/sci.h
+++ b/engines/sci/sci.h
@@ -26,9 +26,12 @@
 #include "common/macresman.h"
 #include "common/util.h"
 #include "common/random.h"
+#include "common/fs.h"
 #include "sci/engine/vm_types.h"	// for Selector
 #include "sci/debug.h"	// for DebugState
 #include "sci/detection.h" // Shared code between detection and engine
+#include "sci/textmode.h"
+#include "sci/engine/script_strings.h"
 
 struct ADGameDescription;
 
@@ -76,6 +79,7 @@
 class GfxRemap32;
 class GfxPorts;
 class GfxScreen;
+class SciHiResText;
 class GfxText16;
 class GfxText32;
 class GfxTransitions;
@@ -117,6 +121,13 @@
 	kDebugLevelWorkarounds,
 	kDebugLevelVideo,
 	kDebugLevelGame,
+	kDebugLevelKorean,
+};
+
+/** Keymapper actions of the engine. */
+enum SciAction {
+	kSciActionNone = 0,
+	kSciActionHangulToggle ///< Han/Yeong: the built-in Hangul composer on or off
 };
 
 
@@ -279,11 +290,27 @@
 	 *						If nullptr is passed then no subtitle will be added to the returned string.
 	 * @return				The processed string.
 	 */
-	Common::String strSplitLanguage(const char *str, uint16 *splitLanguage, const char *sep);
+	Common::String strSplitLanguage(const char *str, uint16 *splitLanguage, const char *sep,
+	                                const ScriptStrings::Key &key = ScriptStrings::Key());
 	Common::String strSplit(const char *str, const char *sep) {
 		return strSplitLanguage(str, NULL, sep);
 	}
 
+	/** The header line a translation of this game carries (DESIGN.md section 4); vocab= for a word table. */
+	Common::TranslationFileHeader translationHeader(bool withVocab) const;
+	/** How text is held and drawn (DESIGN.md section 3); decided in the constructor. */
+	const TextMode &getTextMode() const { return _textMode; }
+	/** The script string table (sci-<code>.str) of a TextMode::kUtf8 game. */
+	ScriptStrings &scriptStrings() { return _scriptStrings; }
+	/** The hi-res text of a TextMode::kUtf8 game; nullptr without. */
+	SciHiResText *getHiResText() const { return _hiresText; }
+	/** @p str, or its translation when @p key has one. */
+	Common::String translated(const Common::String &str, const ScriptStrings::Key &key) const;
+	/** The key of the heap string @p ptr (holding @p str): a script string's, or a tagged buffer's. */
+	ScriptStrings::Key keyOf(reg_t ptr, const Common::String &str) const;
+	/** strSplitLanguage() of the heap string @p ptr, translated. */
+	Common::String strSplitHeap(reg_t ptr, const Common::String &str, uint16 *splitLanguage, const char *sep);
+
 	kLanguage getSciLanguage();
 	void setSciLanguage(kLanguage lang);
 	void setSciLanguage();
@@ -407,6 +434,11 @@
 	const ADGameDescription *_gameDescription;
 	const SciGameId _gameId;
 	ResourceManager *_resMan; /**< The resource manager */
+	TextMode _textMode;
+	Common::String _textModeNotes;
+	TextModeInputs textModeInputs(const Common::FSNode &gameDataDir, SciVersion version) const;
+	ScriptStrings _scriptStrings;
+	SciHiResText *_hiresText;
 	ScriptPatcher *_scriptPatcher; /**< The script patcher */
 	EngineState *_gamestate;
 	Kernel *_kernel;
```

- [ ] **Step 2: sci.cpp**:

```diff
--- a/engines/sci/sci.cpp
+++ b/engines/sci/sci.cpp
@@ -23,6 +23,10 @@
 #include "common/config-manager.h"
 #include "common/debug-channels.h"
 #include "common/translation.h"
+#include "common/fs.h"
+#include "common/language_folder.h"
+#include "common/md5.h"
+#include "common/memstream.h"
 
 #include "engines/advancedDetector.h"
 #include "engines/util.h"
@@ -65,6 +69,8 @@
 #include "sci/graphics/palette16.h"
 #include "sci/graphics/remap.h"
 #include "sci/graphics/screen.h"
+#include "sci/graphics/hirestext.h"
+#include "graphics/hires_text/hires_options.h"
 #include "sci/graphics/text16.h"
 #include "sci/graphics/transitions.h"
 
@@ -126,6 +132,7 @@
 	_gameDescription(desc),
 	_gameId(gameId),
 	_resMan(nullptr),
+	_hiresText(nullptr),
 	_scriptPatcher(nullptr),
 	_gamestate(nullptr),
 	_kernel(nullptr),
@@ -215,6 +222,70 @@
 	default:
 		break;
 	}
+
+	// The fork's translations (DESIGN.md sections 3, 4): the language folder
+	// (ko/) goes before the game folder, so its message.map, patches and
+	// tables win.
+	_textMode = decideTextMode(textModeInputs(gameDataDir, SCI_VERSION_NONE), _textModeNotes);
+	if (_textMode.utf8())
+		SearchMan.addSubDirectoryMatching(gameDataDir, Common::getLanguageFolderCode(_textMode.language), 1);
+}
+
+TextModeInputs SciEngine::textModeInputs(const Common::FSNode &gameDataDir, SciVersion version) const {
+	TextModeInputs in;
+	in.detected = _gameDescription->language;
+	if (ConfMan.hasKey("language"))
+		in.requested = Common::parseLanguage(ConfMan.get("language"));
+	in.koreanText = !ConfMan.hasKey("korean_text") || ConfMan.getBool("korean_text");
+	if (ConfMan.hasKey("text_encoding"))
+		in.textEncoding = ConfMan.get("text_encoding");
+	Common::FSList files;
+	if (in.requested != Common::UNK_LANG && gameDataDir.getChildren(files, Common::FSNode::kListAll)) {
+		const Common::Array<Common::Language> langs = Common::translationFolderLanguages(files, "sci-%s.str");
+		for (uint i = 0; i < langs.size(); ++i)
+			in.translationFolder = in.translationFolder || langs[i] == in.requested;
+		const Common::String table = Common::translationTableName("sci-%s.str", in.requested);
+		const Common::FSNode folder = gameDataDir.getChild(Common::getLanguageFolderCode(in.requested));
+		in.stringTable = gameDataDir.getChild(table).exists() || (folder.isDirectory() && folder.getChild(table).exists());
+	}
+	in.hiresText = !ConfMan.hasKey("hires_text") || ConfMan.getBool("hires_text");
+	in.version = version;
+	return in;
+}
+
+static void appendResource(ResourceManager *resMan, const ResourceId &id, Common::Array<byte> &out) {
+	Resource *res = resMan->findResource(id, false);
+	if (!res)
+		return;
+	const byte *data = res->getUnsafeDataAt(0, res->size());
+	for (uint32 i = 0; i < res->size(); ++i)
+		out.push_back(data[i]);
+}
+
+static Common::String md5Of(const Common::Array<byte> &bytes) {
+	Common::MemoryReadStream s(bytes.data(), bytes.size());
+	return Common::computeStreamMD5AsString(s);
+}
+
+Common::TranslationFileHeader SciEngine::translationHeader(bool withVocab) const {
+	// DESIGN.md section 4: the md5 of what the keys depend on, script 0
+	// followed by the main vocabulary (vocab.000, else vocab.900) when the
+	// game has one; vocab= is the md5 of that vocabulary alone.
+	Common::TranslationFileHeader h;
+	h.format = 1;
+	h.game = _gameDescription->gameId;
+	h.engine = "sci";
+	Common::Array<byte> script, vocab;
+	appendResource(_resMan, ResourceId(kResourceTypeScript, 0), script);
+	appendResource(_resMan, ResourceId(kResourceTypeVocab, 0), vocab);
+	if (vocab.empty())
+		appendResource(_resMan, ResourceId(kResourceTypeVocab, 900), vocab);
+	Common::Array<byte> both(script);
+	both.insert_at(both.size(), vocab);
+	h.fingerprint = md5Of(both);
+	if (withVocab && !vocab.empty())
+		h.vocab = md5Of(vocab);
+	return h;
 }
 
 SciEngine::~SciEngine() {
@@ -247,6 +318,7 @@
 	delete _gfxRemap16;
 	delete _gfxCursor;
 	delete _gfxScreen;
+	delete _hiresText;
 
 	delete _audio;
 	delete _sync;
@@ -297,6 +369,16 @@
 	_scriptPatcher = new ScriptPatcher();
 	SegManager *segMan = new SegManager(_resMan, _scriptPatcher);
 
+	if (_textMode.utf8()) {
+		_textMode = decideTextMode(textModeInputs(Common::FSNode(ConfMan.getPath("path")), getSciVersion()), _textModeNotes);
+		if (!_textModeNotes.empty())
+			debugC(1, kDebugLevelKorean, "Text mode: %s", _textModeNotes.c_str());
+		const Common::TranslationFileHeader expected = translationHeader(false);
+		const Common::String code = Common::getLanguageFolderCode(_textMode.language);
+		if (_scriptStrings.load(code, expected) && !_scriptStrings.getHeader().present())
+			warning("This game's sci-%s.str header line is: %s", code.c_str(), expected.toString().c_str());
+	}
+
 	// Load the Mac executable and fonts if available
 	if (getSciVersion() < SCI_VERSION_2 && getPlatform() == Common::kPlatformMacintosh) {
 		loadMacExecutable();
@@ -334,6 +416,19 @@
 		if (renderMode != Common::kRenderDefault)
 			undither = false;
 
+		if (_textMode.hires) {
+			_hiresText = new SciHiResText();
+			Common::Array<Common::String> notes;
+			const uint32 offered = Graphics::hiResTargetsOffered(g_system->getSupportedFormats());
+			if (!_hiresText->setup(Common::Path(ConfMan.getPath("path")), Common::getLanguageFolderCode(_textMode.language),
+			                       offered, notes)) {
+				delete _hiresText;
+				_hiresText = nullptr;
+			}
+			for (uint i = 0; i < notes.size(); ++i)
+				debugC(1, kDebugLevelKorean, "Hi-res text: %s", notes[i].c_str());
+		}
+
 		// Initialize the game screen
 		_gfxScreen = new GfxScreen(_resMan, renderMode);
 		_gfxScreen->enableUndithering(undither);
@@ -832,7 +927,7 @@
 }
 
 Common::Language SciEngine::getLanguage() const {
-	return _gameDescription->language;
+	return _textMode.language;
 }
 
 bool SciEngine::isLanguageRTL() const {
```

- [ ] **Step 3: the `Korean` debug channel** — `detection.cpp:60`, after the "Game" entry:
  `{Sci::kDebugLevelKorean, "Korean", "Korean text, input and parser"},` (the rest of `detection.cpp` is Task 10).
- [ ] **Step 4: script** — `stringIdAtOffset` (the id `identifyOffsets` gave the string holding an offset):

```diff
--- a/engines/sci/engine/script.h
+++ b/engines/sci/engine/script.h
@@ -289,6 +289,8 @@
 	 * Get the offset array
 	 */
 	const offsetLookupArrayType *getOffsetArray() { return &_offsetLookupArray; };
+	/** The id identifyOffsets() gave the string that holds byte @p offset; 0 when no string does. */
+	uint16 stringIdAtOffset(uint32 offset) const;
 	uint16 getOffsetObjectCount() { return _offsetLookupObjectCount; };
 	uint16 getOffsetStringCount() { return _offsetLookupStringCount; };
 	uint16 getOffsetSaidCount() { return _offsetLookupSaidCount; };
```

```diff
--- a/engines/sci/engine/script.cpp
+++ b/engines/sci/engine/script.cpp
@@ -228,6 +228,15 @@
 	applySaidWorkarounds();
 }
 
+uint16 Script::stringIdAtOffset(uint32 offset) const {
+	for (uint i = 0; i < _offsetLookupArray.size(); ++i) {
+		const offsetLookupArrayEntry &e = _offsetLookupArray[i];
+		if (e.type == SCI_SCR_OFFSET_TYPE_STRING && offset >= e.offset && offset < e.offset + e.stringSize)
+			return e.id;
+	}
+	return 0;
+}
+
 void Script::identifyOffsets() {
 	offsetLookupArrayEntry arrayEntry;
 	SciSpan<const byte> scriptDataPtr;
```

- [ ] **Step 5: seg_manager** — `stringKey`, and a copy into a buffer too small for a UTF-8 translation is cut at a code
  point boundary (upstream writes past it):

```diff
--- a/engines/sci/engine/seg_manager.h
+++ b/engines/sci/engine/seg_manager.h
@@ -28,6 +28,7 @@
 #include "sci/engine/vm.h"
 #include "sci/engine/vm_types.h"
 #include "sci/engine/segment.h"
+#include "sci/engine/script_strings.h"
 #ifdef ENABLE_SCI32
 #include "sci/graphics/celobj32.h" // kLowResX, kLowResY
 #endif
@@ -308,6 +309,9 @@
 	 */
 	Common::String getString(reg_t pointer);
 
+	/** Where the string at @p pointer lives in a script, as a ScriptStrings key; unset for anything else. */
+	ScriptStrings::Key stringKey(reg_t pointer) const;
+
 
 	/**
 	 * Copies a string from src to dest.
```

```diff
--- a/engines/sci/engine/seg_manager.cpp
+++ b/engines/sci/engine/seg_manager.cpp
@@ -21,6 +21,7 @@
 
 #include "sci/sci.h"
 #include "sci/engine/seg_manager.h"
+#include "sci/utf8.h"
 #include "sci/engine/state.h"
 #include "sci/engine/script.h"
 #ifdef ENABLE_SCI32
@@ -698,6 +699,23 @@
 		return;
 	}
 
+	Common::String cut;
+	if (g_sci && g_sci->getTextMode().utf8() && dest_r.maxSize > 0 && Common::strnlen(src, n) >= (size_t)dest_r.maxSize) {
+		// A translation longer than the English buffer: cut it on a code
+		// point boundary instead of writing past the buffer.
+		uint32 end = 0;
+		while (end < (uint32)dest_r.maxSize - 1) {
+			int bytes;
+			decodeUtf8Char((const byte *)src + end, bytes);
+			if (end + bytes > (uint32)dest_r.maxSize - 1)
+				break;
+			end += bytes;
+		}
+		warning("SegManager::strncpy: \"%s\" cut to %u bytes", src, end);
+		cut = Common::String(src, end);
+		src = cut.c_str();
+		n = end + 1;
+	}
 
 	if (dest_r.isRaw) {
 		forwardCopy<true>(dest_r.raw, (const byte *)src, n);
@@ -873,6 +891,18 @@
 }
 
 
+ScriptStrings::Key SegManager::stringKey(reg_t pointer) const {
+	if (pointer.isNull())
+		return ScriptStrings::Key();
+	const Script *script = getScriptIfLoaded(pointer.getSegment());
+	if (!script)
+		return ScriptStrings::Key();
+	const uint16 id = script->stringIdAtOffset(pointer.getOffset());
+	if (!id)
+		return ScriptStrings::Key();
+	return ScriptStrings::Key((uint16)script->getScriptNumber(), id);
+}
+
 Common::String SegManager::getString(reg_t pointer) {
 	Common::String ret;
 	if (pointer.isNull())
```

- [ ] **Step 6: state.cpp** — `translated`, `keyOf`, `strSplitHeap`, the key parameter of `strSplitLanguage`:

```diff
--- a/engines/sci/engine/state.cpp
+++ b/engines/sci/engine/state.cpp
@@ -358,7 +358,29 @@
 	setSciLanguage(getSciLanguage());
 }
 
-Common::String SciEngine::strSplitLanguage(const char *str, uint16 *languageSplitter, const char *sep) {
+Common::String SciEngine::translated(const Common::String &str, const ScriptStrings::Key &key) const {
+	Common::String t;
+	return _scriptStrings.lookup(key, t) ? t : str;
+}
+
+ScriptStrings::Key SciEngine::keyOf(reg_t ptr, const Common::String &str) const {
+	if (!_scriptStrings.isLoaded())
+		return ScriptStrings::Key();
+	ScriptStrings::Key key = _gamestate->_segMan->stringKey(ptr);
+	if (!key.isSet())
+		key = _scriptStrings.keyOf(ScriptStrings::bufferId(ptr.getSegment(), ptr.getOffset()), str);
+	return key;
+}
+
+Common::String SciEngine::strSplitHeap(reg_t ptr, const Common::String &str, uint16 *languageSplitter, const char *sep) {
+	return strSplitLanguage(str.c_str(), languageSplitter, sep, keyOf(ptr, str));
+}
+
+Common::String SciEngine::strSplitLanguage(const char *str, uint16 *languageSplitter, const char *sep,
+                                           const ScriptStrings::Key &key) {
+	Common::String t;
+	if (_scriptStrings.lookup(key, t))
+		return t;
 	kLanguage activeLanguage = getSciLanguage();
 	kLanguage subtitleLanguage = K_LANG_NONE;
 
```

- [ ] **Step 7: build** — `heavy make -j8 scummvm test`: links; tests unchanged (N0 + 46).
- [ ] **Step 8: [data] check** — make `$PACK` (section 3), then
  `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 15 $BLD/scummvm -c $R/run.ini kq1-ko 2>&1 | grep -E "sci-ko|header"`:

```
WARNING: sci-ko.str has no fingerprint header (a pack made before headers); loaded unchecked!
WARNING: This game's sci-ko.str header line is: #! format=1 game=kq1sci fingerprint=87ef5027c321e18e7b81a47bb584c098 engine=sci!
```

  (Before Task 10 `language=ko` runs on fallback detection; the lines above appear either way.)
- [ ] **Step 9: commit** — `SCI: Decide the text mode and load the script string table` (body: TextMode in the
  constructor and again with the version in run(); ko/ in SearchMan before the game folder; getLanguage() answers the
  target's language when the folder has its translation; the fingerprint of DESIGN decision 4 for SCI; string keys).

---

## Task 10: `-ko` targets and running the release a translation was made for

**Files:** `engines/sci/detection.cpp`.

- [ ] **Step 1: [data] red** — `SDL_VIDEODRIVER=dummy $BLD/scummvm --detect --path=$PACK | grep kq1sci` lists one line
  (`... (SCI/DOS/English)`).
- [ ] **Step 2: implement**:

```diff
--- a/engines/sci/detection.cpp
+++ b/engines/sci/detection.cpp
@@ -20,6 +20,9 @@
  */
 
 #include "engines/advancedDetector.h"
+#include "common/config-manager.h"
+#include "common/fs.h"
+#include "common/language_folder.h"
 #include "base/plugins.h"
 #include "common/file.h"
 #include "common/hashmap.h"
@@ -58,6 +61,7 @@
 	{Sci::kDebugLevelWorkarounds, "Workarounds", "Notifies when workarounds are triggered"},
 	{Sci::kDebugLevelVideo, "Video", "Video (SEQ, VMD, RBT) debugging"},
 	{Sci::kDebugLevelGame, "Game", "Debug calls from game scripts"},
+	{Sci::kDebugLevelKorean, "Korean", "Korean text, input and parser"},
 	{Sci::kDebugLevelGC, "GC", "Garbage Collector debugging"},
 	{Sci::kDebugLevelResMan, "ResMan", "Resource manager debugging"},
 	{Sci::kDebugLevelOnStartup, "OnStartup", "Enter debugger at start of game"},
@@ -217,6 +221,8 @@
 
 	DetectedGames detectGames(const Common::FSList &fslist, uint32 skipADFlags, bool skipIncomplete) override;
 
+	Common::Error identifyGame(DetectedGame &game, const void **descriptor) override;
+
 	ADDetectedGame fallbackDetect(const FileMap &allFiles, const Common::FSList &fslist, ADDetectedGameExtraInfo **extra) const override;
 
 	void dumpDetectionEntries() const override;
@@ -225,9 +231,59 @@
 	void addFileToDetectedGame(const Common::Path &name, const FileMap &allFiles, MD5Properties md5Prop, ADDetectedGame &game) const;
 };
 
+// The languages the game folder holds a fork translation in (ko/sci-ko.str, ko/hires*.map; DESIGN.md section 4).
+static Common::Array<Common::Language> translationLanguages(const Common::FSList &fslist) {
+	return Common::translationFolderLanguages(fslist, "sci-%s.str");
+}
+
+Common::Error SciMetaEngineDetection::identifyGame(DetectedGame &game, const void **descriptor) {
+	// A target made for a translation (language=ko) runs the release the
+	// translation was made for: that release's entry is matched with the
+	// language left out, and the engine takes the language from the target.
+	Common::FSList files;
+	const Common::Language wanted = ConfMan.hasKey("language") ? Common::parseLanguage(ConfMan.get("language")) : Common::UNK_LANG;
+	bool translated = false;
+	if (wanted != Common::UNK_LANG && ConfMan.hasKey("path") &&
+	    Common::FSNode(ConfMan.getPath("path")).getChildren(files, Common::FSNode::kListAll)) {
+		const Common::Array<Common::Language> langs = translationLanguages(files);
+		for (uint i = 0; i < langs.size(); ++i)
+			translated = translated || langs[i] == wanted;
+	}
+	if (!translated)
+		return AdvancedMetaEngineDetection::identifyGame(game, descriptor);
+	const Common::String language = ConfMan.get("language");
+	ConfMan.set("language", "", Common::ConfigManager::kTransientDomain);
+	Common::Error err = AdvancedMetaEngineDetection::identifyGame(game, descriptor);
+	ConfMan.set("language", language, Common::ConfigManager::kTransientDomain);
+	return err;
+}
+
 DetectedGames SciMetaEngineDetection::detectGames(const Common::FSList &fslist, uint32 skipADFlags, bool skipIncomplete) {
 	DetectedGames games = AdvancedMetaEngineDetection::detectGames(fslist, skipADFlags, skipIncomplete);
 
+	// One more target per translation in the game folder: the same release
+	// in that language (gameid-ko), offered first.
+	const Common::Array<Common::Language> translations = translationLanguages(fslist);
+	DetectedGames variants;
+	for (uint t = 0; t < translations.size(); ++t) {
+		for (uint i = 0; i < games.size(); ++i) {
+			const DetectedGame &game = games[i];
+			if (game.language == translations[t] || game.hasUnknownFiles)
+				continue;
+			const char *title = game.gameId.c_str();
+			for (const PlainGameDescriptor *pgd = s_sciGameTitles; pgd->gameId; ++pgd)
+				if (game.gameId.equals(pgd->gameId))
+					title = pgd->description;
+			DetectedGame variant = game;
+			variant.language = translations[t];
+			variant.description = DetectedGame(getName(), game.gameId, title, translations[t], game.platform, game.extra).description;
+			variant.preferredTarget = game.preferredTarget + "-" + Common::getLanguageFolderCode(translations[t]);
+			variants.push_back(variant);
+		}
+	}
+	for (uint i = 0; i < variants.size(); ++i)
+		games.insert_at(i, variants[i]);
+
 	for (DetectedGame &game : games) {
 		const GameIdStrToEnum *g = gameIdStrToEnum;
 		for (; g->gameidStr; ++g) {
```

- [ ] **Step 3: [data] green** — the same command:

```
sci:kq1sci   King's Quest I: Quest for the Crown (SCI/DOS/Korean)    .../pack
sci:kq1sci   King's Quest I: Quest for the Crown (SCI/DOS/English)   .../pack
```

  and a dummy run of `[kq1-ko]` logs `Running King's Quest I: Quest for the Crown (SCI/DOS/English)` (the release entry,
  not fallback detection; the language comes from the target).
- [ ] **Step 4: commit** — `SCI: Offer a target per translation in the game folder`.

---

## Task 11: `kStrAt`, `kStrLen`, `kFormat`, `kStrCpy`, `kStrCat` on a UTF-8 heap

**Files:** `engines/sci/engine/kstring.cpp`.

- [ ] **Step 1: implement** (the byte access of `kStrAt` moves into `strAtByte` unchanged so the UTF-8 path can write
  bytes through it; every new branch is behind `getTextMode().utf8()` or a loaded table):

```diff
--- a/engines/sci/engine/kstring.cpp
+++ b/engines/sci/engine/kstring.cpp
@@ -28,6 +28,9 @@
 #include "sci/engine/state.h"
 #include "sci/engine/selector.h"
 #include "sci/engine/tts.h"
+#include "sci/utf8.h"
+
+#include "common/hangul.h"
 
 namespace Sci {
 
@@ -52,8 +55,17 @@
 		s2 = g_sci->strSplit(s2.c_str(), nullptr);
 	}
 
+	const Common::String before = s1;
 	s1 += s2;
 	s->_segMan->strcpy_(argv[0], s1.c_str());
+	if (g_sci->scriptStrings().isLoaded()) {
+		// The buffer keeps the keys of its parts (ScriptStrings::appendToBuffer()).
+		ScriptStrings::Key key = s->_segMan->stringKey(argv[1]);
+		if (key.isSet())
+			key.room = s->currentRoomNumber();
+		g_sci->scriptStrings().appendToBuffer(ScriptStrings::bufferId(argv[0].getSegment(), argv[0].getOffset()), before, key,
+		                                      ScriptStrings::bufferId(argv[1].getSegment(), argv[1].getOffset()), s2);
+	}
 	return argv[0];
 }
 
@@ -72,6 +84,18 @@
 
 
 reg_t kStrCpy(EngineState *s, int argc, reg_t *argv) {
+	if (argc <= 2 && g_sci->scriptStrings().isLoaded()) {
+		// A copied script string stays translatable: the buffer is tagged with its key.
+		ScriptStrings::Key key = s->_segMan->stringKey(argv[1]);
+		const uint32 dest = ScriptStrings::bufferId(argv[0].getSegment(), argv[0].getOffset());
+		const Common::String text = s->_segMan->getString(argv[1]);
+		if (key.isSet()) {
+			key.room = s->currentRoomNumber();
+			g_sci->scriptStrings().tagBuffer(dest, key, text);
+		} else {
+			g_sci->scriptStrings().copyBufferTag(dest, ScriptStrings::bufferId(argv[1].getSegment(), argv[1].getOffset()), text);
+		}
+	}
 	if (argc > 2) {
 		int length = argv[2].toSint16();
 
@@ -87,37 +111,13 @@
 }
 
 
-reg_t kStrAt(EngineState *s, int argc, reg_t *argv) {
-	if (argv[0] == SIGNAL_REG) {
-		warning("Attempt to perform kStrAt() on a signal reg");
-		return NULL_REG;
-	}
-
-	SegmentRef dest_r = s->_segMan->dereference(argv[0]);
-	if (!dest_r.isValid()) {
-		warning("Attempt to StrAt at invalid pointer %04x:%04x", PRINT_REG(argv[0]));
-		return NULL_REG;
-	}
-
+// One byte of a string, read and (@p write) replaced by @p newvalue.
+static byte strAtByte(SegmentRef &dest_r, uint16 offset, bool write, byte newvalue) {
 	byte value;
-	byte newvalue = 0;
-	uint16 offset = argv[1].toUint16();
-	if (argc > 2)
-		newvalue = argv[2].toSint16();
-
-	g_sci->_tts->setMessage(s->_segMan->getString(argv[0]));
-
-	// in kq5 this here gets called with offset 0xFFFF
-	//  (in the desert wheng getting the staff)
-	if ((int)offset >= dest_r.maxSize) {
-		warning("kStrAt offset %X exceeds maxSize", offset);
-		return s->r_acc;
-	}
-
 	// FIXME: Move this to segman
 	if (dest_r.isRaw) {
 		value = dest_r.raw[offset];
-		if (argc > 2) /* Request to modify this char */
+		if (write) /* Request to modify this char */
 			dest_r.raw[offset] = newvalue;
 	} else {
 		if (dest_r.skipByte)
@@ -131,7 +131,7 @@
 
 		if (!oddOffset) {
 			value = tmp.getOffset() & 0x00ff;
-			if (argc > 2) { /* Request to modify this char */
+			if (write) { /* Request to modify this char */
 				uint16 tmpOffset = tmp.toUint16();
 				tmpOffset &= 0xff00;
 				tmpOffset |= newvalue;
@@ -140,7 +140,7 @@
 			}
 		} else {
 			value = tmp.getOffset() >> 8;
-			if (argc > 2)  { /* Request to modify this char */
+			if (write) { /* Request to modify this char */
 				uint16 tmpOffset = tmp.toUint16();
 				tmpOffset &= 0x00ff;
 				tmpOffset |= newvalue << 8;
@@ -150,7 +150,71 @@
 		}
 	}
 
-	return make_reg(0, value);
+	return value;
+}
+
+// kStrAt on a UTF-8 heap (TextMode::kUtf8): the index counts code points,
+// as kStrLen does. An index past both the code points and the bytes is a
+// byte offset (a script walking fixed-size records, DESIGN.md section 13).
+static reg_t strAtUtf8(EngineState *s, SegmentRef &dest_r, reg_t str, uint16 index, bool write, uint16 newvalue) {
+	const Common::String text = s->_segMan->getString(str);
+	const byte *p = (const byte *)text.c_str();
+	if (!write) {
+		if (utf8IndexIsPastEnd(p, index))
+			return make_reg(0, (int)index < dest_r.maxSize ? strAtByte(dest_r, index, false, 0) : 0);
+		const uint32 at = utf8OffsetOf(p, index);
+		if (at >= text.size())
+			return NULL_REG;
+		int bytes;
+		return make_reg(0, decodeUtf8Char(p + at, bytes) & 0xFFFF);
+	}
+	const uint32 at = utf8WriteOffset(p, index);
+	uint16 old = 0;
+	if (at < text.size()) {
+		int bytes;
+		old = decodeUtf8Char(p + at, bytes) & 0xFFFF;
+	}
+	byte enc[4];
+	const int n = encodeUtf8Char(newvalue, enc);
+	if ((int)(at + n) > dest_r.maxSize) {
+		warning("kStrAt: code point %u does not fit the buffer", index);
+		return s->r_acc;
+	}
+	for (int i = 0; i < n; i++)
+		strAtByte(dest_r, (uint16)(at + i), true, enc[i]);
+	return make_reg(0, old);
+}
+
+reg_t kStrAt(EngineState *s, int argc, reg_t *argv) {
+	if (argv[0] == SIGNAL_REG) {
+		warning("Attempt to perform kStrAt() on a signal reg");
+		return NULL_REG;
+	}
+
+	SegmentRef dest_r = s->_segMan->dereference(argv[0]);
+	if (!dest_r.isValid()) {
+		warning("Attempt to StrAt at invalid pointer %04x:%04x", PRINT_REG(argv[0]));
+		return NULL_REG;
+	}
+
+	byte newvalue = 0;
+	uint16 offset = argv[1].toUint16();
+	if (argc > 2)
+		newvalue = argv[2].toSint16();
+
+	g_sci->_tts->setMessage(s->_segMan->getString(argv[0]));
+
+	if (g_sci->getTextMode().utf8())
+		return strAtUtf8(s, dest_r, argv[0], offset, argc > 2, argc > 2 ? argv[2].toUint16() : 0);
+
+	// in kq5 this here gets called with offset 0xFFFF
+	//  (in the desert wheng getting the staff)
+	if ((int)offset >= dest_r.maxSize) {
+		warning("kStrAt offset %X exceeds maxSize", offset);
+		return s->r_acc;
+	}
+
+	return make_reg(0, strAtByte(dest_r, offset, argc > 2, newvalue));
 }
 
 
@@ -213,6 +277,15 @@
 ** Formats the text from text.textresnr (offset index_inside_res) or heap_text_addr according to
 ** the supplied parameters and writes it to the targ_address.
 */
+// Whether the text [start, end) ends in a precomposed Hangul syllable (UTF-8).
+static bool endsWithHangulSyllable(const char *start, const char *end) {
+	if (end - start < 3)
+		return false;
+	const byte *t = (const byte *)end - 3;
+	const uint32 cp = ((t[0] & 0x0F) << 12) | ((t[1] & 0x3F) << 6) | (t[2] & 0x3F);
+	return (t[0] & 0xF0) == 0xE0 && cp >= 0xAC00 && cp <= 0xD7A3;
+}
+
 reg_t kFormat(EngineState *s, int argc, reg_t *argv) {
 	enum {
 		ALIGN_NONE   = 0,
@@ -249,6 +322,8 @@
 
 	int index = (startarg == 3) ? argv[2].toUint16() : 0;
 	Common::String source_str = g_sci->getKernel()->lookupText(position, index);
+	if (position.getSegment())
+		source_str = g_sci->translated(source_str, g_sci->keyOf(position, source_str));
 	const char* source = source_str.c_str();
 
 	debugC(kDebugLevelStrings, "Formatting \"%s\"", source);
@@ -311,6 +386,11 @@
 
 				Common::String tempsource = g_sci->getKernel()->lookupText(reg,
 				                                  arguments[paramindex + 1]);
+				if (reg.getSegment())
+					tempsource = g_sci->translated(tempsource, g_sci->keyOf(reg, tempsource));
+				// A plural "s" after a Korean word is dropped (사과s).
+				if (g_sci->getTextMode().utf8() && (tempsource == "s" || tempsource == "S") && endsWithHangulSyllable(targetbuf, target))
+					tempsource.clear();
 				int slen = tempsource.size();
 				int extralen = strLength - slen;
 				assert((target - targetbuf) + extralen <= maxsize);
@@ -371,6 +451,8 @@
 					while (strLength-- > 1)
 						*target++ = ' '; /* Format into the text */
 				char argchar = arguments[paramindex++];
+				if (g_sci->getTextMode().utf8() && (argchar == 's' || argchar == 'S') && endsWithHangulSyllable(targetbuf, target))
+					argchar = 0;
 				if (argchar)
 					*target++ = argchar;
 				mode = 0;
@@ -434,12 +516,21 @@
 
 	*target = 0; /* Terminate string */
 
+	if (g_sci->getTextMode().utf8()) {
+		// 을(를), 이(가) ... after the inserted words.
+		Common::String formatted(targetbuf);
+		Common::resolveKoreanParticles(formatted);
+		s->_segMan->strcpy_(dest, formatted.c_str());
+		return dest;
+	}
 	s->_segMan->strcpy_(dest, targetbuf);
 
 	return dest; /* Return target addr */
 }
 
 reg_t kStrLen(EngineState *s, int argc, reg_t *argv) {
+	if (g_sci->getTextMode().utf8())
+		return make_reg(0, utf8Length((const byte *)s->_segMan->getString(argv[0]).c_str()));
 	return make_reg(0, s->_segMan->strlen(argv[0]));
 }
 
```

- [ ] **Step 2: build** — links, N0 + 46. Read the moved block against `kstring.cpp:113-150` of the base: identical but for
  `argc > 2` -> `write` and `return value`.
- [ ] **Step 3: commit** — `SCI: Count code points and translate kFormat on a Korean heap` (body: kStrAt index = code
  point, past both ends a byte offset (fixed records); kFormat translates its format and %s strings, drops a plural s after
  Hangul, resolves particles; StrCpy/StrCat keep buffer tags).

---

## Task 12: Translate what is displayed

**Files:** `engines/sci/engine/{kgraphics.cpp,kmenu.cpp}`, `engines/sci/graphics/menu.cpp`.

- [ ] **Step 1: implement**:

```diff
--- a/engines/sci/engine/kgraphics.cpp
+++ b/engines/sci/engine/kgraphics.cpp
@@ -371,7 +371,7 @@
 	}
 
 	uint16 languageSplitter = 0;
-	Common::String splitText = g_sci->strSplitLanguage(text.c_str(), &languageSplitter, separator);
+	Common::String splitText = g_sci->strSplitHeap(argv[1], text, &languageSplitter, separator);
 
 	int16 textWidth;
 	int16 textHeight;
@@ -916,10 +916,10 @@
 	switch (type) {
 	case SCI_CONTROLS_TYPE_BUTTON:
 	case SCI_CONTROLS_TYPE_TEXTEDIT:
-		splitText = g_sci->strSplitLanguage(text.c_str(), &languageSplitter, nullptr);
+		splitText = g_sci->strSplitHeap(textReference, text, &languageSplitter, nullptr);
 		break;
 	case SCI_CONTROLS_TYPE_TEXT:
-		splitText = g_sci->strSplitLanguage(text.c_str(), &languageSplitter, g_sci->getGameId() == GID_PQ2 ? "\r" : "\r----------\r");
+		splitText = g_sci->strSplitHeap(textReference, text, &languageSplitter, g_sci->getGameId() == GID_PQ2 ? "\r" : "\r----------\r");
 		break;
 	default:
 		break;
@@ -1230,7 +1230,7 @@
 	Common::String title;
 	if (argv[4 + argextra].getSegment()) {
 		title = s->_segMan->getString(argv[4 + argextra]);
-		title = g_sci->strSplit(title.c_str(), nullptr);
+		title = g_sci->strSplitHeap(argv[4 + argextra], title, nullptr, nullptr);
 	}
 
 	return g_sci->_gfxPorts->kernelNewWindow(rect1, rect2, style, priority, colorPen, colorBack, title.c_str());
@@ -1276,7 +1276,8 @@
 	}
 
 	uint16 languageSplitter = 0;
-	Common::String splitText = g_sci->strSplitLanguage(text.c_str(), &languageSplitter, g_sci->getGameId() == GID_PQ2 ? "\r" : "\r----------\r");
+	Common::String splitText = g_sci->strSplitLanguage(text.c_str(), &languageSplitter, g_sci->getGameId() == GID_PQ2 ? "\r" : "\r----------\r",
+	                                                   textp.getSegment() ? g_sci->keyOf(textp, text) : ScriptStrings::Key());
 
 	return g_sci->_gfxPaint16->kernelDisplay(splitText.c_str(), languageSplitter, argc, argv);
 }
```

```diff
--- a/engines/sci/engine/kmenu.cpp
+++ b/engines/sci/engine/kmenu.cpp
@@ -32,6 +32,8 @@
 reg_t kAddMenu(EngineState *s, int argc, reg_t *argv) {
 	Common::String title = s->_segMan->getString(argv[0]);
 	Common::String content = s->_segMan->getString(argv[1]);
+	title = g_sci->translated(title, g_sci->keyOf(argv[0], title));
+	content = g_sci->translated(content, g_sci->keyOf(argv[1], content));
 
 	g_sci->_gfxMenu->kernelAddEntry(title, content, argv[1]);
 	return s->r_acc;
```

```diff
--- a/engines/sci/graphics/menu.cpp
+++ b/engines/sci/graphics/menu.cpp
@@ -313,6 +313,7 @@
 		break;
 	case SCI_MENU_ATTRIBUTE_TEXT:
 		itemEntry->text = _segMan->getString(value);
+		itemEntry->text = g_sci->translated(itemEntry->text, g_sci->keyOf(value, itemEntry->text));
 		itemEntry->textVmPtr = value;
 		// We assume here that no script ever creates a separatorLine dynamically
 		break;
```

- [ ] **Step 2: build** — links, N0 + 46.
- [ ] **Step 3: commit** — `SCI: Translate script strings where they are displayed`.

---

## Task 13: The upscaled driver composites the text plane

**Files:** `engines/sci/graphics/drivers/{gfxdriver.h,gfxdriver_intern.h,upscaled.cpp,init.cpp}`.

- [ ] **Step 1: implement**:

```diff
--- a/engines/sci/graphics/drivers/gfxdriver.h
+++ b/engines/sci/graphics/drivers/gfxdriver.h
@@ -30,6 +30,7 @@
 
 namespace Graphics {
 	class Cursor;
+	class TextPlane;
 }
 
 namespace Sci {
@@ -65,6 +66,12 @@
 	virtual bool supportsPalIntensity() const = 0;
 	virtual bool supportsHiResGraphics() const = 0;
 	virtual bool driverBasedTextRendering() const = 0;
+	/**
+	 * Hi-res text (DESIGN.md section 5.1): @p plane is composited over every
+	 * screen update by @p mode (a Graphics::TextCompose::Mode). False when
+	 * the driver cannot (not a 2x upscaled screen of the plane's size).
+	 */
+	virtual bool setTextPlane(const Graphics::TextPlane *plane, int mode) { return false; }
 	uint16 numColors() const { return _numColors; }
 	byte pixelSize() const { return _pixelSize; }
 
```

```diff
--- a/engines/sci/graphics/drivers/gfxdriver_intern.h
+++ b/engines/sci/graphics/drivers/gfxdriver_intern.h
@@ -24,6 +24,7 @@
 #define SCI_GRAPHICS_DRIVERS_GFXDRIVER_INTERN_H
 
 #include "common/platform.h"
+#include "common/array.h"
 #include "sci/graphics/drivers/gfxdriver.h"
 
 namespace Sci {
@@ -102,6 +103,7 @@
 	Common::Point getRealCoords(Common::Point &pos) const override;
 	void drawTextFontGlyph(const byte *src, int pitch, int hiresDestX, int hiresDestY, int hiresW, int hiresH, int transpColor, const PaletteMod *palMods, const byte *palModMapping) override; // For HiRes fonts.
 	bool driverBasedTextRendering() const override { return true; }
+	bool setTextPlane(const Graphics::TextPlane *plane, int mode) override;
 protected:
 	UpscaledGfxDriver(uint16 scaledW, uint16 scaledH, int16 textAlignX, bool scaleCursor, bool rgbRendering);
 	void updateScreen(int destX, int destY, int w, int h, const PaletteMod *palMods, const byte *palModMapping);
@@ -115,7 +117,12 @@
 	uint16 _vScaleMult;
 	uint16 _vScaleDiv;
 	byte *_scaledBitmap;
+	const Graphics::TextPlane *_textPlane;
+	int _textMode;
+	Common::Array<byte> _textScratch;
 private:
+	/** The text plane over @p buff (w x h at destX, destY); a copy when @p buff is _scaledBitmap. */
+	void compositeText(byte *&buff, int &pitch, int destX, int destY, int w, int h);
 	virtual void renderBitmap(const byte *src, int pitch, int dx, int dy, int w, int h, int &realWidth, int &realHeight);
 	const bool _scaleCursor;
 	uint16 _cursorWidth;
```

```diff
--- a/engines/sci/graphics/drivers/upscaled.cpp
+++ b/engines/sci/graphics/drivers/upscaled.cpp
@@ -22,6 +22,8 @@
 
 #include "common/system.h"
 #include "graphics/cursorman.h"
+#include "graphics/hires_text/text_compose.h"
+#include "graphics/hires_text/text_plane.h"
 #include "sci/graphics/drivers/gfxdriver_intern.h"
 
 namespace Sci {
@@ -32,7 +34,7 @@
 
 UpscaledGfxDriver::UpscaledGfxDriver(uint16 scaledW, uint16 scaledH, int16 textAlignX, bool scaleCursor, bool rgbRendering) :
 	GfxDefaultDriver(scaledW, scaledH, false, rgbRendering), _textAlignX(textAlignX), _scaleCursor(scaleCursor), _needCursorBuffer(false),
-	_scaledBitmap(nullptr), _renderScaled(nullptr), _renderGlyph(nullptr), _cursorWidth(0), _cursorHeight(0), _hScaleMult(2), _vScaleMult(2), _vScaleDiv(1) {
+	_scaledBitmap(nullptr), _renderScaled(nullptr), _renderGlyph(nullptr), _cursorWidth(0), _cursorHeight(0), _hScaleMult(2), _vScaleMult(2), _vScaleDiv(1), _textPlane(nullptr), _textMode(0) {
 	_virtualW = 320;
 	_virtualH = 200;
 }
@@ -165,9 +167,52 @@
 		pitch = _screenW *_pixelSize;
 	}
 
+	if (_textPlane && !_textPlane->isEmpty())
+		compositeText(buff, pitch, destX, destY, w, h);
 	g_system->copyRectToScreen(buff, pitch, destX, destY, w, h);
 }
 
+bool UpscaledGfxDriver::setTextPlane(const Graphics::TextPlane *plane, int mode) {
+	if (plane && (_hScaleMult != 2 || _vScaleMult != 2 || _vScaleDiv != 1 || plane->getWidth() != _screenW ||
+	              plane->getHeight() > _screenH))
+		return false;
+	_textPlane = plane;
+	_textMode = mode;
+	return true;
+}
+
+void UpscaledGfxDriver::compositeText(byte *&buff, int &pitch, int destX, int destY, int w, int h) {
+	const Common::Rect r = Common::Rect(destX, destY, destX + w, destY + h).findIntersectingRect(
+		Common::Rect(_textPlane->getWidth(), _textPlane->getHeight()));
+	if (r.isEmpty())
+		return;
+	const byte *scb = _scaledBitmap + destY * _screenW * _srcPixelSize + destX * _srcPixelSize;
+	if (buff == scb) {
+		// Never into _scaledBitmap: the next update would keep the text.
+		_textScratch.resize(w * h * _pixelSize);
+		for (int y = 0; y < h; ++y)
+			memcpy(&_textScratch[y * w * _pixelSize], buff + y * pitch, w * _pixelSize);
+		buff = _textScratch.data();
+		pitch = w * _pixelSize;
+	}
+	byte indexRow[640 * 2], coverageRow[640 * 2];
+	for (int y = r.top; y < r.bottom; ++y) {
+		const byte *index, *coverage;
+		const int n = MIN<int>(r.width(), ARRAYSIZE(indexRow));
+		if (!_textPlane->getRow(y, r.left, n, indexRow, coverageRow, index, coverage))
+			continue;
+		byte *dst = buff + (y - destY) * pitch + (r.left - destX) * _pixelSize;
+		// The screen format is known only after initScreen(): CLUT8 stamps,
+		// RGB blends or thresholds as the text's mode says.
+		if (_pixelSize == 1)
+			Graphics::TextCompose::stampSpan(dst, index, coverage, n);
+		else
+			Graphics::TextCompose::composeSpan(dst, _format, index, coverage, n, _currentPalette,
+			                                   _textMode == Graphics::TextCompose::kStamp ? Graphics::TextCompose::kThreshold
+			                                                                         : (Graphics::TextCompose::Mode)_textMode);
+	}
+}
+
 void UpscaledGfxDriver::adjustCursorBuffer(uint16 newWidth, uint16 newHeight) {
 	// For configs which need/have the composite buffer for other purposes, we can skip this.
 	if (!_compositeBuffer)
```

```diff
--- a/engines/sci/graphics/drivers/init.cpp
+++ b/engines/sci/graphics/drivers/init.cpp
@@ -22,6 +22,7 @@
 
 #include "sci/graphics/drivers/gfxdriver.h"
 #include "sci/sci.h"
+#include "sci/graphics/hirestext.h"
 #include "common/config-manager.h"
 #include "common/language.h"
 #include "common/platform.h"
@@ -142,6 +143,9 @@
 	GfxDriver *result = nullptr;
 
 	int requestRGB = (int)(ConfMan.hasKey("palette_mods") && ConfMan.getBool("palette_mods")) || (ConfMan.hasKey("rgb_rendering") && ConfMan.getBool("rgb_rendering"));
+	// Hi-res text picks its own target (render_target, the map's faces).
+	if (g_sci->getHiResText())
+		requestRGB = g_sci->getHiResText()->wantsRgb() ? 1 : 0;
 	SciVersion version = getSciVersion();
 	SciGameId gameId = g_sci->getGameId();
 	Common::Language lang = g_sci->getLanguage();
```

  Notes: the plane is never written into `_scaledBitmap` (the next low-res update would keep stale text): on CLUT8 the
  rows go through `_textScratch`; on RGB they are composed into `_compositeBuffer` after the conversion, so a palette
  change (`setPalette` -> `updateScreen`) recolours text. `setTextPlane` is called before `initScreen`, so the pixel size
  is checked per update, not there (the prototype first refused RGB that way).
- [ ] **Step 2: build** — links, N0 + 46.
- [ ] **Step 3: commit** — `SCI: Composite the hi-res text plane in the upscaled driver`.

---

## Task 14: The screen keeps the plane in step; the cache hands out hi-res fonts

**Files:** `engines/sci/graphics/{screen.h,screen.cpp,paint16.cpp,cache.h,cache.cpp}`.

- [ ] **Step 1: implement**:

```diff
--- a/engines/sci/graphics/screen.h
+++ b/engines/sci/graphics/screen.h
@@ -30,6 +30,7 @@
 #include "graphics/sjis.h"
 #include "graphics/korfont.h"
 #include "graphics/pixelformat.h"
+#include "graphics/hires_text/text_plane.h"
 
 #include "common/rendermode.h"
 
@@ -120,6 +121,12 @@
 	}
 	void enableUndithering(bool flag);
 
+	/** The hi-res text plane (SciHiResText), or nullptr when the game draws its own text only. */
+	Graphics::TextPlane *getTextPlane() const { return _textPlane; }
+	void clearTextRect(const Common::Rect &lowres) { if (_textPlane && !_textPlane->isEmpty()) _textPlane->clearLowresRect(lowres); }
+	void xorTextRect(const Common::Rect &lowres, byte mask) { if (_textPlane) _textPlane->xorIndicesLowresRect(lowres, mask); }
+	void swapTextColors(const Common::Rect &lowres, byte a, byte b) { if (_textPlane) _textPlane->swapIndicesLowresRect(lowres, a, b); }
+
 	void putMacChar(const Graphics::Font *commonFont, int16 x, int16 y, uint16 chr, byte color);
 	void putKanjiChar(Graphics::FontSJIS *commonFont, int16 x, int16 y, uint16 chr, byte color);
 	void putHangulChar(Graphics::FontKorean *commonFont, int16 x, int16 y, uint16 chr, byte color);
@@ -237,6 +244,8 @@
 	 */
 	byte *_hiresGlyphBuffer;
 
+	Graphics::TextPlane *_textPlane;
+
 	/**
 	 * This here holds a translation for vertical+horizontal coordinates between native
 	 * (visual) and actual (display) screen.
@@ -253,7 +262,7 @@
 
 	// pixel related code, in header so that it can be inlined for performance
 public:
-	void putPixel(int16 x, int16 y, byte drawMask, byte color, byte priority, byte control) {
+	void putPixel(int16 x, int16 y, byte drawMask, byte color, byte priority, byte control, bool keepText = false) {
 		if (_upscaledHires == GFX_SCREEN_UPSCALED_480x300) {
 			putPixel480x300(x, y, drawMask, color, priority, control);
 			return;
@@ -266,6 +275,8 @@
 			_visualScreen[offset] = color;
 			if (_paletteMapScreen)
 				_paletteMapScreen[offset] = _curPaletteMapValue;
+			if (!keepText)
+				clearTextRect(Common::Rect(x, y, x + 1, y + 1));
 
 			switch (_upscaledHires) {
 			case GFX_SCREEN_UPSCALED_DISABLED:
@@ -334,6 +345,7 @@
 			_displayScreen[offset] = color;
 			if (_paletteMapScreen)
 				_paletteMapScreen[offset] = _curPaletteMapValue;
+			clearTextRect(Common::Rect(x, y, x + 1, y + 1));
 
 		}
 		if (drawMask & GFX_SCREEN_MASK_PRIORITY) {
```

```diff
--- a/engines/sci/graphics/screen.cpp
+++ b/engines/sci/graphics/screen.cpp
@@ -31,6 +31,7 @@
 #include "sci/sci.h"
 #include "sci/engine/state.h"
 #include "sci/graphics/screen.h"
+#include "sci/graphics/hirestext.h"
 #include "sci/graphics/view.h"
 #include "sci/graphics/palette16.h"
 #include "sci/graphics/scifx.h"
@@ -38,7 +39,7 @@
 
 namespace Sci {
 
-GfxScreen::GfxScreen(ResourceManager *resMan, Common::RenderMode renderMode) : _resMan(resMan), _hiresGlyphBuffer(nullptr) {
+GfxScreen::GfxScreen(ResourceManager *resMan, Common::RenderMode renderMode) : _resMan(resMan), _hiresGlyphBuffer(nullptr), _textPlane(nullptr) {
 
 	// Scale the screen, if needed
 	_upscaledHires = GFX_SCREEN_UPSCALED_DISABLED;
@@ -150,6 +151,15 @@
 	if (_gfxDrv->driverBasedTextRendering())
 		_hiresGlyphBuffer = new byte[16 * 16]();
 
+	// Hi-res text (DESIGN.md section 5.1): a 2x plane the driver composites.
+	SciHiResText *hires = g_sci->getHiResText();
+	if (hires && hires->createPlane(_displayWidth, _displayHeight)) {
+		if (_gfxDrv->setTextPlane(&hires->getPlane(), hires->getComposeMode()))
+			_textPlane = &hires->getPlane();
+		else
+			warning("Hi-res text: the graphics driver cannot show it; the game's own font is used");
+	}
+
 	_displayPixels = _displayWidth * _displayHeight;
 
 	// Allocate visual, priority, control and display screen
@@ -210,6 +220,8 @@
 	free(_paletteMapScreen);
 	delete[] _backupScreen;
 	delete[] _hiresGlyphBuffer;
+	if (_textPlane)
+		_gfxDrv->setTextPlane(nullptr, 0);
 	delete _gfxDrv;
 }
 
@@ -230,6 +242,8 @@
 	memset(_displayScreen, 0, _displayPixels);
 	memset(&_ditheredPicColors, 0, sizeof(_ditheredPicColors));
 	_fontIsUpscaled = false;
+	if (_textPlane)
+		_textPlane->clear();
 	copyToScreen();
 }
 
@@ -535,6 +549,8 @@
 				byteCount += rectHeight * rectWidth; // _paletteMapScreen (upscaled hires)
 		}
 	}
+	if ((mask & GFX_SCREEN_MASK_VISUAL) && _textPlane)
+		byteCount += _textPlane->saveSize(rect); // the text plane
 	if (mask & GFX_SCREEN_MASK_PRIORITY) {
 		byteCount += pixels; // _priorityScreen
 	}
@@ -553,6 +569,8 @@
 		bitsSaveDisplayScreen(rect, _displayScreen, memoryPtr);
 		if (_paletteMapScreen)
 			bitsSaveDisplayScreen(rect, _paletteMapScreen, memoryPtr);
+		if (_textPlane)
+			_textPlane->save(rect, memoryPtr);
 	}
 	if (mask & GFX_SCREEN_MASK_PRIORITY) {
 		bitsSaveScreen(rect, _priorityScreen, _width, memoryPtr);
@@ -610,6 +628,11 @@
 		bitsRestoreDisplayScreen(rect, memoryPtr, _displayScreen);
 		if (_paletteMapScreen)
 			bitsRestoreDisplayScreen(rect, memoryPtr, _paletteMapScreen);
+		if (_textPlane) {
+			const byte *in = memoryPtr;
+			_textPlane->restore(rect, in);
+			memoryPtr = in;
+		}
 	}
 	if (mask & GFX_SCREEN_MASK_PRIORITY) {
 		bitsRestoreScreen(rect, memoryPtr, _priorityScreen, _width);
```

```diff
--- a/engines/sci/graphics/paint16.cpp
+++ b/engines/sci/graphics/paint16.cpp
@@ -212,9 +212,10 @@
 	for (int16 y = r.top; y < r.bottom; y++) {
 		for (int16 x = r.left; x < r.right; x++) {
 			byte curVisual = _screen->getVisual(x, y);
-			_screen->putPixel(x, y, GFX_SCREEN_MASK_VISUAL, curVisual ^ 0x0f, 0, 0);
+			_screen->putPixel(x, y, GFX_SCREEN_MASK_VISUAL, curVisual ^ 0x0f, 0, 0, true);
 		}
 	}
+	_screen->xorTextRect(r, 0x0f);
 }
 
 void GfxPaint16::eraseRect(const Common::Rect &rect) {
@@ -242,12 +243,13 @@
 				for (x = r.left; x < r.right; x++) {
 					byte curVisual = _screen->getVisual(x, y);
 					if (curVisual == color) {
-						_screen->putPixel(x, y, GFX_SCREEN_MASK_VISUAL, priority, 0, 0);
+						_screen->putPixel(x, y, GFX_SCREEN_MASK_VISUAL, priority, 0, 0, true);
 					} else if (curVisual == priority) {
-						_screen->putPixel(x, y, GFX_SCREEN_MASK_VISUAL, color, 0, 0);
+						_screen->putPixel(x, y, GFX_SCREEN_MASK_VISUAL, color, 0, 0, true);
 					}
 				}
 			}
+			_screen->swapTextColors(r, color, priority);
 		} else { // just fill rect with color
 			for (y = r.top; y < r.bottom; y++) {
 				for (x = r.left; x < r.right; x++) {
```

```diff
--- a/engines/sci/graphics/cache.h
+++ b/engines/sci/graphics/cache.h
@@ -41,6 +41,8 @@
 	~GfxCache();
 
 	GfxFont *getFont(GuiResourceId fontId);
+	/** @p font, or a GfxFontHiRes over it when the game draws hi-res text. */
+	GfxFont *hiResFont(GfxFont *font, GuiResourceId fontId);
 	GfxView *getView(GuiResourceId viewId);
 
 	int16 kernelViewGetCelWidth(GuiResourceId viewId, int16 loopNo, int16 celNo);
```

```diff
--- a/engines/sci/graphics/cache.cpp
+++ b/engines/sci/graphics/cache.cpp
@@ -27,6 +27,8 @@
 #include "sci/engine/state.h"
 #include "sci/engine/selector.h"
 #include "sci/graphics/cache.h"
+#include "sci/graphics/hirestext.h"
+#include "sci/graphics/screen.h"
 #include "sci/graphics/scifont.h"
 #include "sci/graphics/fontsjis.h"
 #include "sci/graphics/fontkorean.h"
@@ -61,6 +63,17 @@
 	_cachedViews.clear();
 }
 
+GfxFont *GfxCache::hiResFont(GfxFont *font, GuiResourceId fontId) {
+	// A Korean (TextMode::kUtf8) game draws through the map's faces into
+	// the text plane; the font keeps the game's own glyphs for what the map
+	// leaves to it.
+	SciHiResText *hires = g_sci->getHiResText();
+	if (!hires || !_screen->getTextPlane())
+		return font;
+	const Graphics::HiResFontSet *set = hires->getFontSet(fontId);
+	return set ? new GfxFontHiRes(font, *set, *_screen->getTextPlane()) : font;
+}
+
 GfxFont *GfxCache::getFont(GuiResourceId fontId) {
 	if (_cachedFonts.size() >= MAX_CACHED_FONTS)
 		purgeFontCache();
@@ -73,7 +86,7 @@
 		else if ((fontId == 900) && (g_sci->getLanguage() == Common::JA_JPN))
 			_cachedFonts[fontId] = new GfxFontSjis(_screen, fontId);
 		else
-			_cachedFonts[fontId] = new GfxFontFromResource(_resMan, _screen, fontId);
+			_cachedFonts[fontId] = hiResFont(new GfxFontFromResource(_resMan, _screen, fontId), fontId);
 	}
 
 	return _cachedFonts[fontId];
```

  What keeps the plane right: every visual `putPixel`/`vectorPutPixel` clears the text under it (window and picture
  draws), except the two invert loops, which keep the text and invert its index (`xorTextRect`, `swapTextColors`); saved
  bits (windows, menus) carry the plane rect (`TextPlane::save`, one flag byte when empty); a restored game clears it.
- [ ] **Step 2: build** — links, N0 + 46.
- [ ] **Step 3: commit** — `SCI: Keep the hi-res text plane in step with the screen`.

---

## Task 15: text16 walks code points

**Files:** `engines/sci/graphics/{text16.h,text16.cpp}`.

- [ ] **Step 1: implement**:

```diff
--- a/engines/sci/graphics/text16.h
+++ b/engines/sci/graphics/text16.h
@@ -89,6 +89,8 @@
 private:
 	void init();
 	bool SwitchToFont1001OnKorean(const char *text, uint16 languageSplitter);
+	/** GetLongest() of a TextMode::kUtf8 game: Graphics::TextLayout over the font's widths. */
+	int16 getLongestUtf8(const char *&textPtr, int16 maxWidth);
 	bool SwitchToFont900OnSjis(const char *text, uint16 languageSplitter);
 	static bool isJapaneseNewLine(int16 curChar, int16 nextChar);
 	int16 macGetLongest(const Common::String &text, uint start, const Graphics::Font *font, int16 maxWidth, int16 *lineWidth);
```

```diff
--- a/engines/sci/graphics/text16.cpp
+++ b/engines/sci/graphics/text16.cpp
@@ -36,6 +36,9 @@
 #include "sci/graphics/scifont.h"
 #include "sci/graphics/screen.h"
 #include "sci/graphics/text16.h"
+#include "sci/graphics/hirestext.h"
+#include "sci/utf8.h"
+#include "graphics/hires_text/text_layout.h"
 
 namespace Sci {
 
@@ -196,6 +199,8 @@
 //  Conquests of the Longbow - talking with Lobb - one text box of the dialogue contains a longer word,
 //                                                 that will be broken into 2 lines (bug #5159)
 int16 GfxText16::GetLongest(const char *&textPtr, int16 maxWidth, GuiResourceId orgFontId) {
+	if (g_sci->getTextMode().utf8())
+		return getLongestUtf8(textPtr, maxWidth);
 	uint16 curChar = 0;
 	const char *textStartPtr = textPtr;
 	const char *lastSpacePtr = nullptr;
@@ -389,9 +394,15 @@
 	if (_font) {
 		bool escapedNewLine = false;
 		text += from;
-		while (len--) {
-			uint16 curChar = (*(const byte *)text++);
-			if (_font->isDoubleByte(curChar)) {
+		const bool utf8 = g_sci->getTextMode().utf8();
+		while (len-- > 0) {
+			uint32 curChar = (*(const byte *)text++);
+			if (utf8 && curChar >= 0x80) {
+				int bytes;
+				curChar = decodeUtf8Char((const byte *)text - 1, bytes);
+				text += bytes - 1;
+				len -= bytes - 1;
+			} else if (_font->isDoubleByte(curChar)) {
 				curChar |= (*(const byte *)text++) << 8;
 				len--;
 			} else if (escapedNewLine) {
@@ -511,9 +522,15 @@
 	rect.bottom = rect.top + _ports->_curPort->fontHeight;
 	text += from;
 	bool escapedNewLine = false;
-	while (len--) {
-		uint16 curChar = (*(const byte *)text++);
-		if (_font->isDoubleByte(curChar)) {
+	const bool utf8 = g_sci->getTextMode().utf8();
+	while (len-- > 0) {
+		uint32 curChar = (*(const byte *)text++);
+		if (utf8 && curChar >= 0x80) {
+			int bytes;
+			curChar = decodeUtf8Char((const byte *)text - 1, bytes);
+			text += bytes - 1;
+			len -= bytes - 1;
+		} else if (_font->isDoubleByte(curChar)) {
 			curChar |= (*(const byte *)text++) << 8;
 			len--;
 		} else if (escapedNewLine) {
@@ -712,13 +729,20 @@
 		str = Common::convertBiDiString(strOrig, g_sci->getLanguage());
 
 	const byte *text = (const byte *)str.c_str();
-	uint16 textLen = str.size();
+	int16 textLen = str.size();
 	Common::Rect rect;
 
 	rect.top = _ports->_curPort->curTop;
 	rect.bottom = rect.top + _ports->_curPort->fontHeight;
-	while (textLen--) {
-		uint16 curChar = *text++;
+	const bool utf8 = g_sci->getTextMode().utf8();
+	while (textLen-- > 0) {
+		uint32 curChar = *text++;
+		if (utf8 && curChar >= 0x80) {
+			int bytes;
+			curChar = decodeUtf8Char(text - 1, bytes);
+			text += bytes - 1;
+			textLen -= bytes - 1;
+		}
 		switch (curChar) {
 		case 0:
 			break;
@@ -733,6 +757,9 @@
 
 // Check for Korean strings, and use font 1001 to render them
 bool GfxText16::SwitchToFont1001OnKorean(const char *text, uint16 languageSplitter) {
+	// A UTF-8 translation draws Hangul through the hi-res fonts, not font 1001.
+	if (g_sci->getTextMode().utf8())
+		return false;
 	const byte* ptr = (const byte *)text;
 	if (languageSplitter != 0x6b23) { // #k prefix as language splitter
 		// Check if the text contains at least one Korean character
@@ -772,6 +799,71 @@
 // Instead, we detect the newline sequences during string processing loops and
 // apply the substitute characters on the fly. PQ2 is the only game known to use
 // this feature. "\n" appears in most of its Japanese strings.
+namespace {
+
+// SCI text as layout units: CR LF, CR and LF end a line; in SCI1.1 a |code|
+// is a zero-width control unit (GfxText16::CodeProcessing()).
+class SciUtf8Decoder : public Graphics::TextDecoder {
+public:
+	explicit SciUtf8Decoder(bool codes) : _codes(codes) {}
+	int decode(const byte *p, const byte *end, uint32 &cp, byte &flags) const override {
+		if (*p == 0x0D || *p == 0x0A) {
+			cp = '\n';
+			flags = Graphics::kUnitNewline;
+			return (*p == 0x0D && p + 1 < end && p[1] == 0x0A) ? 2 : 1;
+		}
+		if (_codes && *p == '|') {
+			const byte *q = p + 1;
+			while (q < end && *q != '|')
+				++q;
+			cp = Graphics::kControlUnit;
+			flags = Graphics::kUnitControl;
+			return (q < end ? q + 1 : end) - p;
+		}
+		return _utf8.decode(p, end, cp, flags);
+	}
+
+private:
+	bool _codes;
+	Graphics::Utf8TextDecoder _utf8;
+};
+
+class SciFontMetrics : public Graphics::LayoutMetrics {
+public:
+	explicit SciFontMetrics(GfxFont *font) : _font(font) {}
+	int advance(uint32 cp) override { return _font->getCharWidth(cp); }
+
+private:
+	GfxFont *_font;
+};
+
+} // End of anonymous namespace
+
+int16 GfxText16::getLongestUtf8(const char *&textPtr, int16 maxWidth) {
+	GetFont();
+	if (!_font || !*textPtr)
+		return 0;
+	// One line at a time: the run ends at the first newline.
+	const char *end = textPtr;
+	while (*end && *end != 0x0D && *end != 0x0A)
+		++end;
+	if (*end == 0x0D && end[1] == 0x0A)
+		end += 2;
+	else if (*end)
+		++end;
+	Graphics::TextRun run;
+	run.decode((const byte *)textPtr, end - textPtr, SciUtf8Decoder(getSciVersion() >= SCI_VERSION_1_1));
+	SciFontMetrics metrics(_font);
+	SciHiResText *hires = g_sci->getHiResText();
+	const Graphics::LineSpan line = Graphics::TextLayout::fitLine(run, 0, maxWidth, metrics,
+	                                                              hires ? hires->getBreakRules() : Graphics::BreakRules());
+	// As the byte loop does: a line ended by a newline counts the newline;
+	// a wrapped line counts what is drawn and skips the spaces after it.
+	const int16 count = line.forced ? line.byteNext : line.byteEnd;
+	textPtr += line.byteNext;
+	return count;
+}
+
 bool GfxText16::isJapaneseNewLine(int16 curChar, int16 nextChar) {
 	return g_sci->getLanguage() == Common::JA_JPN &&
 		curChar == '\\' && (nextChar == 'n' || nextChar == 'N' || nextChar == 'r' || nextChar == 'R');
```

- [ ] **Step 2: build** — `heavy make -j8 scummvm test`: links, N0 + 46.
- [ ] **Step 3: [data] smoke, title and first room, two screens** — run directory `~/work/tmp/p3-kq1/s16-clut8` with the
  `run.ini` of section 3, and `s16-rgb` with `render_target=rgb565` added to `[kq1-ko]`:

```sh
cd ~/work/tmp/p3-kq1/s16-clut8 && timeout 200 ../go.sh $PWD kq1-ko 25                  # title menu
cd ~/work/tmp/p3-kq1/s16-clut8 && timeout 240 ../go.sh $PWD kq1-ko 25 click:412,325 sleep:35   # first room
grep -E "Hi-res text|cannot show" run.log
python3 -c "from PIL import Image; print(Image.open('scummvm-kq1-ko-00000.png').mode)"   # P (clut8), RGB (rgb565)
```

  Expected: `Hi-res text: using data:hires_text/maps/korean-default.map` (after the two "map version 2" notes), no
  "cannot show"; view the PNGs: Hangul buttons `프롤로그 게임시작 크레딧 이어서 계속하기`, then the status line
  `득점: 0 of 158 ... 킹스 퀘스트 I` over the castle moat. The English target (`[kq1]`, `GFX=opengl`, click y + 40) still
  shows `Introduction / Begin Game / Credits / Continue Game` and logs no `Hi-res text` line.
- [ ] **Step 4: commit** — `SCI: Lay out and draw UTF-8 text by code point`.

### Review checkpoint B (after Task 15)

`make test`, both smoke screens viewed, the English target unchanged. Read every hook for its guard: an English game must
take none of the new branches (`getTextMode().utf8()` false, `getHiResText()` null, `getTextPlane()` null,
`scriptStrings().isLoaded()` false). Check `compositeText` clipping (rect clipped to the plane; row scratch 1280 px >= the
640 px plane) and that `bitsGetDataSize` counts exactly what `bitsSave` writes (`saveSize`).

---

## Task 16: The parser reads Korean

**Files:** `engines/sci/parser/{vocabulary.h,vocabulary.cpp,grammar.cpp,said.cpp}`.

- [ ] **Step 1: implement** — table load (`sci-<code>.dat`, else `SCIKOR.DAT`, with the game's header incl. `vocab=`),
  `lookupKorean` (English word + particles, then the table through `KoreanParser`), the UTF-8-safe case fold, the
  arrangement after tokenizing, the second tree in `parseGNF` and the `said()` retry:

```diff
--- a/engines/sci/parser/vocabulary.h
+++ b/engines/sci/parser/vocabulary.h
@@ -26,6 +26,7 @@
 #include "common/hashmap.h"
 #include "common/hash-str.h"
 #include "common/list.h"
+#include "common/korean_vocab.h"
 
 #include "sci/sci.h"
 #include "sci/engine/vm_types.h"
@@ -207,6 +208,8 @@
 	 * @param word_len	length of the word to look up
 	 */
 	void lookupWord(ResultWordList &retval, const char *word, int word_len);
+	/** The Korean word table's reading of @p word (UTF-8). */
+	bool lookupKorean(ResultWordList &retval, const Common::String &word);
 
 	/**
 	 * Looks up a single word in the words list, taking into account suffixes, and updating parent_retval if a matching prefix is found
@@ -266,7 +269,7 @@
 	 *			nodes or if the sentence structure in 'words' is not part of the
 	 *			language described by the grammar passed in 'rules'.
 	 */
-	int parseGNF(const ResultWordListList &words, bool verbose = false);
+	int parseGNF(const ResultWordListList &words, bool verbose = false, ParseTreeNode *nodes = nullptr);
 
 	/**
 	 * Find and store reference for future pronouns
@@ -409,6 +412,12 @@
 	ParseRuleList *_parserRules; /**< GNF rules used in the parser algorithm */
 	Common::Array<parse_tree_branch_t> _parserBranches;
 	WordMap _parserWords;
+
+	// Korean input (DESIGN.md section 6; KoreanParser)
+	Common::KoreanVocabulary _koreanVocab;
+	uint8 _koreanRole;                     ///< the particle lookupWord()'s Korean word lost
+	ResultWordListList _koreanExtraTokens; ///< the further tokens of a sequence word
+	ResultWordListList _koreanFallback;    ///< the sentence without particle phrases, for said()
 	SynonymList _synonyms; /**< The list of synonyms */
 	Common::Array<Common::List<AltInput> > _altInputs;
 
@@ -426,6 +435,11 @@
 	// Parser data:
 	reg_t parser_event; /**< The event passed to Parse() and later used by Said() */
 	bool parserIsValid; /**< If something has been correctly parsed */
+
+	/** Whether a Korean word table (sci-<code>.dat) is loaded. */
+	bool hasKoreanWords() const { return _koreanVocab.isLoaded(); }
+	ParseTreeNode _parserNodesFallback[VOCAB_TREE_NODES]; /**< The tree of _koreanFallback */
+	bool _koreanFallbackValid; /**< said() is tried on _parserNodesFallback too */
 };
 
 /**
```

```diff
--- a/engines/sci/parser/vocabulary.cpp
+++ b/engines/sci/parser/vocabulary.cpp
@@ -22,6 +22,8 @@
 // Main vocabulary support functions and word lookup
 
 #include "sci/parser/vocabulary.h"
+#include "sci/parser/korean_parser.h"
+#include "common/language_folder.h"
 #include "sci/resource/resource.h"
 #include "sci/engine/state.h"
 #include "sci/engine/kernel.h"
@@ -71,6 +73,20 @@
 
 	loadAltInputs();
 
+	_koreanRole = Common::kKoreanRoleNone;
+	_koreanFallbackValid = false;
+	if (g_sci->hasParser() && g_sci->getTextMode().utf8() && !_foreign) {
+		// sci-ko.dat (the packs since 2026-10-17), else SCIKOR.DAT; the
+		// header must be this game's (DESIGN.md decision 4).
+		_koreanVocab.setDebugChannel(kDebugLevelKorean);
+		const Common::TranslationFileHeader expected = g_sci->translationHeader(true);
+		const Common::String name = Common::translationTableName("sci-%s.dat", g_sci->getLanguage());
+		if (!_koreanVocab.load(Common::Path(name), "SCIKOR", expected))
+			_koreanVocab.load(Common::Path("SCIKOR.DAT"), "SCIKOR", expected);
+		if (_koreanVocab.isLoaded() && _koreanVocab.getVersion() < 3)
+			warning("This game's %s header line is: %s", name.c_str(), expected.toString().c_str());
+	}
+
 	parser_event = NULL_REG;
 	parserIsValid = false;
 
@@ -451,6 +467,8 @@
 // we assume that *word points to an already lowercased word
 void Vocabulary::lookupWord(ResultWordList& retval, const char *word, int word_len) {
 	retval.clear();
+	_koreanRole = Common::kKoreanRoleNone;
+	_koreanExtraTokens.clear();
 
 	Common::String tempword(word, word_len);
 
@@ -518,6 +536,9 @@
 	if (!retval.empty())
 		return;
 
+	if (_koreanVocab.isLoaded() && lookupKorean(retval, tempword))
+		return;
+
 	// No match so far? Check if it's a number.
 	if (getSciVersion() > SCI_VERSION_0_EARLY) {
 		char *tester;
@@ -719,14 +740,40 @@
 	0xa5, 0xa5, 0xf3, 0xf3, 0xf5, 0xf5, 0xf7, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff  // 0xf0
 };
 
+bool Vocabulary::lookupKorean(ResultWordList &retval, const Common::String &word) {
+	// door를, fenrus에: an English word of the game with Korean particles.
+	uint split = 0;
+	while (split < word.size() && (byte)word[split] < 0x80)
+		++split;
+	uint8 role;
+	if (split > 0 && split < word.size() && Common::koreanParticleRole(Common::U32String(word.c_str() + split, Common::kUtf8), role)) {
+		lookupWord(retval, word.c_str(), split);
+		if (!retval.empty()) {
+			_koreanRole = role;
+			return true;
+		}
+	}
+	KoreanParser parser(_parserWords, _koreanVocab, kDebugLevelKorean);
+	return parser.lookup(word, retval, _koreanExtraTokens, _koreanRole) > 0;
+}
+
 bool Vocabulary::tokenizeString(ResultWordListList &retval, const char *sentence, char **error) {
 	char currentWord[VOCAB_MAX_WORDLENGTH] = "";
 	int pos_in_sentence = 0;
 	unsigned char c;
 	int wordLen = 0;
 	const byte *lcMap = lowerCaseMap;
+	const bool korean = _koreanVocab.isLoaded();
+	Common::String joined;
+	Common::Array<uint8> roles;
+	bool sawKorean = false;
 
 	*error = nullptr;
+	_koreanFallback.clear();
+	if (korean) {
+		joined = KoreanParser(_parserWords, _koreanVocab, kDebugLevelKorean).joinPairs(sentence);
+		sentence = joined.c_str();
+	}
 
 	if (g_sci->getLanguage() == Common::RU_RUS)
 		lcMap = lowerCaseMap866;
@@ -734,8 +781,9 @@
 	do {
 		c = sentence[pos_in_sentence++];
 
-		if (Common::isAlnum(c) || (c == '-' && wordLen) || (c >= 0x80)) {
-			currentWord[wordLen] = lcMap[c];
+		if ((Common::isAlnum(c) || (c == '-' && wordLen) || (c >= 0x80)) && wordLen < VOCAB_MAX_WORDLENGTH - 1) {
+			// UTF-8 bytes are not CP437 letters: only ASCII is folded.
+			currentWord[wordLen] = (g_sci->getTextMode().utf8() && c >= 0x80) ? c : lcMap[c];
 			++wordLen;
 		} else if (c == ' ' || c == '\0') {
 			// Continue on this word. Words may contain a '-', but may not start with
@@ -760,6 +808,15 @@
 
 				// Copy into list
 				retval.push_back(lookup_result);
+				bool koreanWord = false;
+				for (int k = 0; k < wordLen && !koreanWord; ++k)
+					koreanWord = (byte)currentWord[k] >= 0x80;
+				sawKorean = sawKorean || koreanWord;
+				roles.push_back(koreanWord ? _koreanRole : (uint8)Common::kKoreanRoleNone);
+				for (ResultWordListList::const_iterator x = _koreanExtraTokens.begin(); x != _koreanExtraTokens.end(); ++x) {
+					retval.push_back(*x);
+					roles.push_back(Common::kKoreanRoleNone);
+				}
 			}
 
 			wordLen = 0;
@@ -767,6 +824,10 @@
 
 	} while (c); // Until terminator is hit
 
+	// Korean word order to the order of the said() specs.
+	if (sawKorean && !retval.empty())
+		KoreanParser(_parserWords, _koreanVocab, kDebugLevelKorean).arrange(retval, roles, _koreanFallback);
+
 	return true;
 }
 
```

```diff
--- a/engines/sci/parser/grammar.cpp
+++ b/engines/sci/parser/grammar.cpp
@@ -532,7 +532,14 @@
 	return rulepos;
 }
 
-int Vocabulary::parseGNF(const ResultWordListList &words, bool verbose) {
+int Vocabulary::parseGNF(const ResultWordListList &words, bool verbose, ParseTreeNode *nodes) {
+	// A Korean sentence's second tree (the words without particle
+	// phrases) is parsed into _parserNodesFallback for said().
+	const bool primary = !nodes;
+	if (primary) {
+		nodes = _parserNodes;
+		_koreanFallbackValid = false;
+	}
 	Console *con = g_sci->getSciDebugger();
 	// Get the start rules:
 	ParseRuleList *work = _vocab_clone_rule_list_by_id(_parserRules, _parserBranches[0].data[1]);
@@ -611,27 +618,33 @@
 	{
 		int temp, pos;
 
-		_parserNodes[0].type = kParseTreeBranchNode;
-		_parserNodes[0].left = &_parserNodes[1];
-		_parserNodes[0].right = &_parserNodes[2];
-
-		_parserNodes[1].type = kParseTreeLeafNode;
-		_parserNodes[1].value = 0x141;
-		_parserNodes[1].right = nullptr;
-
-		_parserNodes[2].type = kParseTreeBranchNode;
-		_parserNodes[2].left = nullptr;
-		_parserNodes[2].right = nullptr;
+		nodes[0].type = kParseTreeBranchNode;
+		nodes[0].left = &nodes[1];
+		nodes[0].right = &nodes[2];
+
+		nodes[1].type = kParseTreeLeafNode;
+		nodes[1].value = 0x141;
+		nodes[1].right = nullptr;
+
+		nodes[2].type = kParseTreeBranchNode;
+		nodes[2].left = nullptr;
+		nodes[2].right = nullptr;
 
 		pos = 2;
 
-		temp = _vbpt_append(_parserNodes, &pos, 2, _parserBranches[0].id);
+		temp = _vbpt_append(nodes, &pos, 2, _parserBranches[0].id);
 		//_vbpt_write_subexpression(nodes, &pos, results[_vocab_rule_list_length(results)].rule, 0, temp);
-		_vbpt_write_subexpression(_parserNodes, &pos, results->rule, 0, temp);
+		_vbpt_write_subexpression(nodes, &pos, results->rule, 0, temp);
 	}
 
 	freeRuleList(results);
 
+	if (primary && !_koreanFallback.empty()) {
+		ResultWordListList fallback = _koreanFallback;
+		synonymizeTokens(fallback);
+		_koreanFallbackValid = parseGNF(fallback, false, _parserNodesFallback) == 0;
+	}
+
 	return 0;
 }
 
```

```diff
--- a/engines/sci/parser/said.cpp
+++ b/engines/sci/parser/said.cpp
@@ -1015,6 +1015,9 @@
 		if (verbose)
 			vocab_dump_parse_tree("Said-tree", said_tree);
 		int retval = augment_parse_nodes(parse_tree_ptr, said_tree);
+		// A Korean sentence matches by its tree without particle phrases too.
+		if (!retval && voc->_koreanFallbackValid)
+			retval = augment_parse_nodes(voc->_parserNodesFallback, said_tree);
 
 		if (!retval)
 			return SAID_NO_MATCH;
```

- [ ] **Step 2: build** — links, N0 + 46.
- [ ] **Step 3: [data] check** — dummy run of `[kq1-ko]` with `-d1 --debugflags=Korean`:
  `[korvocab] loaded 1265 Korean words (version 2) from sci-ko.dat` and the "This game's sci-ko.dat header line is: ...
  vocab=4b7bd270cdc3a1ff3bcec6d56cc4662f" warning.
- [ ] **Step 4: commit** — `SCI: Parse Korean sentences with the pack's word table`.

---

## Task 17: Typing Korean

**Files:** `engines/sci/{event.h,event.cpp,metaengine.cpp}`, `engines/sci/graphics/{controls16.h,controls16.cpp}`.

- [ ] **Step 1: implement**:

```diff
--- a/engines/sci/event.h
+++ b/engines/sci/event.h
@@ -157,10 +157,19 @@
 	SciEvent getSciEvent(SciEventType mask);
 	void flushEvents();
 
+	/** The built-in Hangul composer is on (Han/Yeong toggles it; DESIGN.md section 6). */
+	bool hangulMode() const { return _hangulMode; }
+	/** The OS-IME syllable behind the last surrogate key a script got, or 0. */
+	uint32 lastOsImeKey() const { return _lastOsImeKey; }
+
 private:
 	SciEvent getScummVMEvent();
 
 	const bool _fontIsExtended;
+	bool _hangulInput;   ///< Korean typing: a TextMode::kUtf8 game with hangul_input
+	bool _hangulBuiltin; ///< hangul_builtin: the composer may be switched on
+	bool _hangulMode;
+	uint32 _lastOsImeKey;
 	Common::List<SciEvent> _events;
 #ifdef ENABLE_SCI32
 public:
```

```diff
--- a/engines/sci/event.cpp
+++ b/engines/sci/event.cpp
@@ -25,6 +25,9 @@
 
 #include "sci/sci.h"
 #include "sci/event.h"
+#include "sci/korean_input.h"
+#include "sci/parser/vocabulary.h"
+#include "common/config-manager.h"
 #include "sci/console.h"
 #include "sci/engine/state.h"
 #include "sci/engine/kernel.h"
@@ -142,13 +145,27 @@
 	{ Common::EVENT_MBUTTONUP   , kSciEventMouseRelease }
 };
 
+// A key of DESIGN.md section 7 that may say auto: @p autoValue then.
+static bool autoBoolKey(const char *key, bool autoValue) {
+	bool value;
+	if (!ConfMan.hasKey(key) || ConfMan.get(key).equalsIgnoreCase("auto") || !Common::parseBool(ConfMan.get(key), value))
+		return autoValue;
+	return value;
+}
+
 EventManager::EventManager(bool fontIsExtended) :
-	_fontIsExtended(fontIsExtended)
+	_fontIsExtended(fontIsExtended), _lastOsImeKey(0)
 #ifdef ENABLE_SCI32
 	, _hotRectanglesActive(false)
 	, _activeRectIndex(-1)
 #endif
-	{}
+	{
+	// Korean typing: on by default when the game has a Korean word table.
+	const bool korean = g_sci->getTextMode().utf8();
+	_hangulInput = korean && autoBoolKey("hangul_input", g_sci->getVocabulary() && g_sci->getVocabulary()->hasKoreanWords());
+	_hangulBuiltin = _hangulInput && autoBoolKey("hangul_builtin", true);
+	_hangulMode = _hangulBuiltin && autoBoolKey("hangul_start", true);
+}
 
 EventManager::~EventManager() {
 }
@@ -301,6 +318,14 @@
 		}
 	}
 
+	// The Han/Yeong action toggles the composer; the script never sees it.
+	if (ev.type == Common::EVENT_CUSTOM_ENGINE_ACTION_START && ev.customType == kSciActionHangulToggle) {
+		if (_hangulBuiltin)
+			_hangulMode = !_hangulMode;
+		debugC(1, kDebugLevelKorean, "Hangul mode %s", _hangulMode ? "on" : "off");
+		return noEvent;
+	}
+
 	// Handle keyboard events for the rest of the function
 	if (ev.type != Common::EVENT_KEYDOWN && ev.type != Common::EVENT_KEYUP) {
 		return noEvent;
@@ -315,6 +340,14 @@
 	const Common::KeyCode scummVMKeycode = ev.kbd.keycode;
 
 	input.character = ev.kbd.ascii;
+	if (_hangulInput && ev.type == Common::EVENT_KEYDOWN) {
+		// An OS input method's syllable: SCI0 scripts open the parser prompt
+		// only on printable ASCII, so the script gets a surrogate and the
+		// edit control puts the syllable in (memory korean-input-path).
+		_lastOsImeKey = (ev.kbd.keycode == Common::KEYCODE_INVALID && ev.kbd.ascii >= 0x100) ? ev.kbd.ascii : 0;
+		if (_lastOsImeKey)
+			input.character = KoreanEditLine::kOsImeSurrogateKey;
+	}
 
 	if (scummVMKeycode >= Common::KEYCODE_KP0 && scummVMKeycode <= Common::KEYCODE_KP9 && !(scummVMKeyFlags & Common::KBD_NUM)) {
 		// TODO: Leaky abstractions from SDL should not be handled in game
```

```diff
--- a/engines/sci/metaengine.cpp
+++ b/engines/sci/metaengine.cpp
@@ -21,6 +21,8 @@
 
 #include "engines/advancedDetector.h"
 #include "base/plugins.h"
+#include "backends/keymapper/action.h"
+#include "backends/keymapper/keymapper.h"
 #include "common/config-manager.h"
 #include "common/file.h"
 #include "common/ptr.h"
@@ -200,6 +202,7 @@
 	ADDetectedGame fallbackDetectExtern(uint md5Bytes, const FileMap &allFiles, const Common::FSList &fslist, ADDetectedGameExtraInfo **extra) const override;
 
 	void registerDefaultSettings(const Common::String &target) const override;
+	Common::KeymapArray initKeymaps(const char *target) const override;
 	GUI::OptionsContainerWidget *buildEngineOptionsWidget(GUI::GuiObject *boss, const Common::String &name, const Common::String &target) const override;
 };
 
@@ -690,6 +693,26 @@
 	}
 }
 
+Common::KeymapArray SciMetaEngine::initKeymaps(const char *target) const {
+	Common::KeymapArray keymaps = AdvancedMetaEngine::initKeymaps(target);
+	// Korean targets (DESIGN.md section 6): Han/Yeong switches the built-in
+	// Hangul composer; a keyboard without the key has the others.
+	if (ConfMan.get("language", target) != "ko")
+		return keymaps;
+	Common::Keymap *keymap = new Common::Keymap(Common::Keymap::kKeymapTypeGame, "sci-korean", _("Korean input"));
+	Common::Action *act = new Common::Action("HANGULTOGGLE", _("Han/Yeong (Korean input on/off)"));
+	act->setCustomEngineActionEvent(kSciActionHangulToggle);
+	act->addDefaultInputMapping("HANGUL");
+	act->addDefaultInputMapping("RALT");
+	act->addDefaultInputMapping("C+SPACE");
+	act->addDefaultInputMapping("S+SPACE");
+	act->addDefaultInputMapping("SCROLLOCK");
+	act->addDefaultInputMapping("F12");
+	keymap->addAction(act);
+	keymaps.push_back(keymap);
+	return keymaps;
+}
+
 GUI::OptionsContainerWidget *SciMetaEngine::buildEngineOptionsWidget(GUI::GuiObject *boss, const Common::String &name, const Common::String &target) const {
 	return new OptionsWidget(boss, name, target);
 }
```

```diff
--- a/engines/sci/graphics/controls16.h
+++ b/engines/sci/graphics/controls16.h
@@ -22,6 +22,8 @@
 #ifndef SCI_GRAPHICS_CONTROLS16_H
 #define SCI_GRAPHICS_CONTROLS16_H
 
+#include "sci/korean_input.h"
+
 namespace Sci {
 
 enum controlStateFlags {
@@ -67,6 +69,8 @@
 
 	void drawListControl(Common::Rect rect, reg_t obj, int16 maxChars, int16 count, const Common::String *entries, GuiResourceId fontId, int16 upperPos, int16 cursorPos, bool isAlias);
 	void texteditCursorDraw(Common::Rect rect, const char *text, uint16 curPos);
+	/** kernelTexteditChange() of a TextMode::kUtf8 game: code points, Hangul (KoreanEditLine). */
+	void texteditChangeUtf8(reg_t controlObject, reg_t eventObject);
 	void texteditCursorErase();
 	int getPicNotValid();
 
@@ -78,6 +82,7 @@
 
 	// Textedit-Control related
 	Common::Rect _texteditCursorRect;
+	KoreanEditLine _koreanLine;
 	bool _texteditCursorVisible;
 	uint32 _texteditBlinkTime;
 };
```

```diff
--- a/engines/sci/graphics/controls16.cpp
+++ b/engines/sci/graphics/controls16.cpp
@@ -39,6 +39,7 @@
 #include "sci/graphics/screen.h"
 #include "sci/graphics/text16.h"
 #include "sci/graphics/controls16.h"
+#include "sci/utf8.h"
 
 namespace Sci {
 
@@ -122,6 +123,25 @@
 }
 
 void GfxControls16::texteditCursorDraw(Common::Rect rect, const char *text, uint16 curPos) {
+	if (!_texteditCursorVisible && g_sci->getTextMode().utf8()) {
+		// curPos counts code points (the script's cursor).
+		const byte *p = (const byte *)text;
+		const uint32 at = utf8OffsetOf(p, curPos);
+		int16 textWidth = 0;
+		int bytes;
+		for (uint32 i = 0; i < at; i += bytes)
+			textWidth += _text16->_font->getCharWidth(decodeUtf8Char(p + i, bytes));
+		const uint32 under = p[at] ? decodeUtf8Char(p + at, bytes) : 0;
+		_texteditCursorRect.left = rect.left + textWidth;
+		_texteditCursorRect.top = rect.top;
+		_texteditCursorRect.bottom = _texteditCursorRect.top + _text16->_font->getHeight();
+		_texteditCursorRect.right = _texteditCursorRect.left + (under ? _text16->_font->getCharWidth(under) : 1);
+		_paint16->invertRect(_texteditCursorRect);
+		_paint16->bitsShow(_texteditCursorRect);
+		_texteditCursorVisible = true;
+		texteditSetBlinkTime();
+		return;
+	}
 	if (!_texteditCursorVisible) {
 		int16 textWidth = 0;
 		for (int16 i = 0; i < curPos; i++) {
@@ -157,7 +177,57 @@
 	_texteditBlinkTime = g_system->getMillis() + (30 * 1000 / 60);
 }
 
+void GfxControls16::texteditChangeUtf8(reg_t controlObject, reg_t eventObject) {
+	const uint16 maxChars = readSelectorValue(_segMan, controlObject, SELECTOR(max));
+	const reg_t textReference = readSelector(_segMan, controlObject, SELECTOR(text));
+	if (textReference.isNull())
+		error("kEditControl called on object that doesn't have a text reference");
+	Common::String text = _segMan->getString(textReference);
+	// The script's cursor counts code points; the line works in bytes.
+	uint cursor = utf8OffsetOf((const byte *)text.c_str(), readSelectorValue(_segMan, controlObject, SELECTOR(cursor)));
+	EventManager *events = g_sci->getEventManager();
+	bool changed = _koreanLine.adopt(text, cursor, events->hangulMode(), events->lastOsImeKey(), maxChars);
+
+	if (!eventObject.isNull() && readSelectorValue(_segMan, eventObject, SELECTOR(type)) == kSciEventKeyDown) {
+		const uint16 key = readSelectorValue(_segMan, eventObject, SELECTOR(message));
+		const uint16 modifiers = readSelectorValue(_segMan, eventObject, SELECTOR(modifiers));
+		changed = _koreanLine.key(key, (modifiers & kSciKeyModCtrl) != 0, events->hangulMode(), events->lastOsImeKey(), text,
+		                          cursor, maxChars) == KoreanEditLine::kChanged || changed;
+	}
+
+	if (changed) {
+		const GuiResourceId oldFontId = _text16->GetFontId();
+		const Common::Rect rect = g_sci->_gfxCompare->getNSRect(controlObject);
+		_text16->SetFont(readSelectorValue(_segMan, controlObject, SELECTOR(font)));
+		int16 width, height;
+		_text16->StringWidth(text, -1, width, height);
+		if (width >= rect.width()) {
+			// The line is full: the key is taken back.
+			_koreanLine.undo(text, cursor);
+		}
+		const uint16 index = utf8IndexOfOffset((const byte *)text.c_str(), cursor);
+		texteditCursorErase();
+		_paint16->eraseRect(rect);
+		_text16->Box(text.c_str(), false, rect, SCI_TEXT16_ALIGNMENT_LEFT, -1);
+		_paint16->bitsShow(rect);
+		texteditCursorDraw(rect, text.c_str(), index);
+		_text16->SetFont(oldFontId);
+		_segMan->strcpy_(textReference, text.c_str());
+		_koreanLine.remember(text);
+		writeSelectorValue(_segMan, controlObject, SELECTOR(cursor), index);
+	} else if (g_system->getMillis() >= _texteditBlinkTime) {
+		_paint16->invertRect(_texteditCursorRect);
+		_paint16->bitsShow(_texteditCursorRect);
+		_texteditCursorVisible = !_texteditCursorVisible;
+		texteditSetBlinkTime();
+	}
+}
+
 void GfxControls16::kernelTexteditChange(reg_t controlObject, reg_t eventObject) {
+	if (g_sci->getTextMode().utf8()) {
+		texteditChangeUtf8(controlObject, eventObject);
+		return;
+	}
 	uint16 cursorPos = readSelectorValue(_segMan, controlObject, SELECTOR(cursor));
 	uint16 maxChars = readSelectorValue(_segMan, controlObject, SELECTOR(max));
 	reg_t textReference = readSelector(_segMan, controlObject, SELECTOR(text));
```

- [ ] **Step 2: build** — links, N0 + 46.
- [ ] **Step 3: commit** — `SCI: Type Korean into the parser prompt` (body: Han/Yeong action HANGULTOGGLE with the
  section 6 bindings on Korean targets; hangul_input/builtin/start; OS-IME syllables through the surrogate key; the edit
  control of a UTF-8 game goes through KoreanEditLine).

---

## Task 18: `strcheck.py`

**Files:** new `tools/korean/strcheck.py`, `tools/korean/test_strcheck.py`.

- [ ] **Step 1: test**:

```python
#!/usr/bin/env python3
"""Tests of strcheck.py (run: python3 -I tools/korean/test_strcheck.py)."""

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import strcheck  # noqa: E402

HDR = "#! format=1 game=kq1sci fingerprint=87ef5027c321e18e7b81a47bb584c098 engine=sci"


class StrCheckTest(unittest.TestCase):
    def test_good_table(self):
        header, entries, problems = strcheck.check([HDR + "\n", "# c\n", "0\t5\tA\n", "0\t5\t14\tB\n", "12\t3\ta\\nb\tc\n"])
        self.assertEqual(header["game"], "kq1sci")
        self.assertEqual(entries, 3)
        self.assertEqual(problems, [])

    def test_bad_lines(self):
        _, _, problems = strcheck.check(["0\tx\tA\n", "0\t5\tA\n", "0\t5\tB\n", "70000\t1\tC\n"])
        texts = [t for _, t in problems]
        self.assertIn((1, "not script<TAB>id[<TAB>room]<TAB>text"), problems)
        self.assertIn((3, "duplicates line 2 (the later line wins)"), problems)
        self.assertIn((4, "not script<TAB>id[<TAB>room]<TAB>text"), problems)
        self.assertTrue(any(t.startswith("no fingerprint") for t in texts))

    def test_bad_headers(self):
        for line in ("#!format=1 game=a fingerprint=%s engine=sci" % ("0" * 32),
                     "#! format=2 game=a fingerprint=%s engine=sci" % ("0" * 32),
                     "#! format=1 game=a fingerprint=xyz engine=sci",
                     "#! format=1 fingerprint=%s engine=sci" % ("0" * 32)):
            with self.assertRaises(ValueError):
                strcheck.parse_header(line)

    def test_set_header(self):
        d = tempfile.mkdtemp(dir=os.environ.get("TMPDIR") or ".")
        try:
            p = os.path.join(d, "sci-ko.str")
            with open(p, "w", encoding="utf-8") as f:
                f.write("﻿#! format=1 game=old fingerprint=%s engine=sci\n0\t5\tA\n" % ("0" * 32))
            self.assertEqual(strcheck.main(["header", p, "--line", HDR]), 0)
            with open(p, encoding="utf-8") as f:
                self.assertEqual(f.read(), HDR + "\n0\t5\tA\n")
            self.assertEqual(strcheck.main(["check", p]), 0)
            self.assertEqual(strcheck.main(["header", p, "--line", "#! bad"]), 2)
        finally:
            for n in os.listdir(d):
                os.remove(os.path.join(d, n))
            os.rmdir(d)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: red** — `TMPDIR=$BLD python3 -I tools/korean/test_strcheck.py`: `ModuleNotFoundError: No module named 'strcheck'`.
- [ ] **Step 3: implement**:

```python
#!/usr/bin/env python3
"""strcheck.py: check a script string table (sci-ko.str, agi-ko.str) and set its header.

The table is UTF-8 text: an optional fingerprint header first
(#! format=1 game=<id> fingerprint=<md5> engine=<sci|agi>, DESIGN.md section 4),
then '#' comments, blank lines and entries script<TAB>id[<TAB>room]<TAB>text
(numbers 0..65534; "\\n" is the only escape). The engine refuses the whole
table on one bad line, so this tool says which line it is.

usage: strcheck.py check FILE
       strcheck.py header FILE --line "#! format=1 ..." [-o OUT]
       (the engine prints the line for a game: "This game's sci-ko.str header line is: ...")
"""

import argparse
import re
import sys

HEADER = re.compile(r"^#![ \t]")
FIELD = re.compile(r"^([a-z]+)=(\S*)$")
MD5 = re.compile(r"^[0-9a-fA-F]{32}$")
NUM = re.compile(r"^(0|[1-9][0-9]{0,4})$")


def parse_header(line):
    """-> dict of the header fields; ValueError when it is not a format 1 header."""
    if not HEADER.match(line):
        raise ValueError("not a header line (it must start with '#! ')")
    fields = {}
    for tok in line[2:].split():
        m = FIELD.match(tok)
        if not m:
            raise ValueError("field %r is not key=value" % tok)
        fields[m.group(1)] = m.group(2)
    if fields.get("format") != "1":
        raise ValueError("format=%s is not 1" % fields.get("format"))
    for key in ("game", "engine"):
        if not fields.get(key):
            raise ValueError("%s= is missing" % key)
    for key in ("fingerprint", "vocab"):
        if key in fields and not MD5.match(fields[key]):
            raise ValueError("%s=%s is not an md5" % (key, fields[key]))
    if "fingerprint" not in fields:
        raise ValueError("fingerprint= is missing")
    return fields


def check(lines):
    """-> (header or None, entries, problems); problems are (line number, text)."""
    problems = []
    header = None
    seen = {}
    entries = 0
    for n, raw in enumerate(lines, 1):
        line = raw.rstrip("\r\n")
        if n == 1 and line.startswith("﻿"):
            line = line[1:]
        if n == 1 and line.startswith("#!"):
            try:
                header = parse_header(line)
            except ValueError as e:
                problems.append((n, "header: %s" % e))
            continue
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) < 3 or not NUM.match(parts[0]) or not NUM.match(parts[1]) or int(parts[0]) > 65534 or int(parts[1]) > 65534:
            problems.append((n, "not script<TAB>id[<TAB>room]<TAB>text"))
            continue
        room = "any"
        if len(parts) >= 4 and NUM.match(parts[2]) and int(parts[2]) <= 65534:
            room = parts[2]
        key = (parts[0], parts[1], room)
        if key in seen:
            problems.append((n, "duplicates line %d (the later line wins)" % seen[key]))
        else:
            seen[key] = n
        entries += 1
    if header is None:
        problems.append((1, "no fingerprint header: the engine loads the table unchecked"))
    return header, entries, problems


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("check")
    c.add_argument("file")
    h = sub.add_parser("header")
    h.add_argument("file")
    h.add_argument("--line", required=True)
    h.add_argument("-o", "--out")
    a = ap.parse_args(argv)
    try:
        lines = open(a.file, encoding="utf-8").read().splitlines(True)
    except (OSError, UnicodeDecodeError) as e:
        print("strcheck: %s: %s" % (a.file, e), file=sys.stderr)
        return 2
    if a.cmd == "check":
        header, entries, problems = check(lines)
        for n, text in problems:
            print("%s:%d: %s" % (a.file, n, text))
        print("%s: %d entries, header %s" % (a.file, entries, "yes" if header else "no"))
        return 1 if any(not t.startswith("no fingerprint") and "duplicates" not in t for _, t in problems) else 0
    try:
        parse_header(a.line)
    except ValueError as e:
        print("strcheck: --line: %s" % e, file=sys.stderr)
        return 2
    if lines and lines[0].lstrip("﻿").startswith("#!"):
        lines = lines[1:]
    out = open(a.out or a.file, "w", encoding="utf-8", newline="")
    out.write(a.line + "\n")
    out.writelines(lines)
    out.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: green** — `Ran 4 tests ... OK`. [data] `python3 -I tools/korean/strcheck.py check $PACK/ko/sci-ko.str`:
  `...:1: no fingerprint header: the engine loads the table unchecked` and `...: 380 entries, header no`, rc 0.
- [ ] **Step 5: commit** — `TOOLS: Korean: Add strcheck.py, the script string table check`.

---

## Task 19: [data] KQ1 end to end: typed command, save/load, both screens, headers

**Files:** none in the repository (`P3-notes.md` gets the results in Task 21).

- [ ] **Step 1: typed command** — run directory `s20-clut8` (section 3 `run.ini`):

```sh
timeout 260 ../go.sh $PWD kq1-ko 25 click:412,325 sleep:35 type:qhk sleep:1 alt+s sleep:1 Return sleep:5
```

  Screenshot 0: the prompt `입력` with `봐` in the field; screenshot 1: the room description
  `에드워드 왕의 성 바깥에 서 있다. 성은 큰뱀으로 / 가득찬 해자로 둘러쌓여 있다.` in a box, two lines. Same in `s20-rgb`
  (`render_target=rgb565`): mode `RGB`, same text. With 1 bpp faces the two must agree pixel for pixel inside the text:
  `python3 -c` comparing the box region of both PNGs converted to RGB (expected: identical).
- [ ] **Step 2: a particle sentence** — `type:ansdmf duf` + Return in the first room (문을 열어 = open door; the gate) and
  `type:anstl qhk` ... any two walkthrough lines of `~/work/scummvm/runs/walk/kq1-pairs.tsv`: the reply must be the one
  the English command gives (run the English target with the English command and compare the replies' meaning). Record
  both in `P3-notes.md`.
- [ ] **Step 3: save and restore with a Korean name** — `originalsaveload=true` in `[kq1-ko]`, `GFX=opengl`:

```sh
GFX=opengl timeout 300 ../go.sh $PWD kq1-ko 25 click:412,365 sleep:35 F5 sleep:3 type:ans sleep:1 Return sleep:4 F7 sleep:4
ls kq1-ko.001
```

  Screenshot: the restore dialog lists `문`, its list arrows are the game's glyphs (not boxes). Then the same with
  `... F7 sleep:4 Return sleep:5`: the room comes back. The ScummVM save dialog (default) shows the name too (launcher
  load list: UTF-8 description).
- [ ] **Step 4: the headers** — make a headered pack and check the three cases:

```sh
cp -r $PACK ~/work/tmp/p3-kq1/packh
python3 -I $SRC/tools/korean/strcheck.py header ~/work/tmp/p3-kq1/packh/ko/sci-ko.str \
	--line "#! format=1 game=kq1sci fingerprint=87ef5027c321e18e7b81a47bb584c098 engine=sci"
python3 -I $SRC/tools/korean/k6bake.py build $HAR/dos/release/kq1/translation/sci-ko.dat.vocab.tsv \
	~/work/tmp/p3-kq1/packh/ko/sci-ko.dat --game kq1sci --fingerprint 87ef5027c321e18e7b81a47bb584c098 \
	--vocab-md5 4b7bd270cdc3a1ff3bcec6d56cc4662f
# -> SCIKOR version 3, 1265 entries, 21742 bytes, sha256 964bb09d...
```

  A dummy run on `packh` logs `[korvocab] loaded 1265 Korean words (version 3)` and no `sci-ko` warning; with the header's
  fingerprint changed to 32 zeros: `sci-ko.str was made for another game: fingerprint=000..., the game's is 87ef...; not
  loaded` (and the game then shows the English script strings).
- [ ] **Step 5: the reference** — the first-room PNGs of Task 15 (`s16-clut8`, `s16-rgb`) are the P3 reference:
  `sha256sum` both into `P3-notes.md`; a later run must `cmp` equal (the game's opening is deterministic up to the
  moat animation: take the shot at the same step; if the water differs, compare the status line and box rows only, the
  rows y 0-15 and the text box, with the PIL crop recorded in the notes).

No commit.

---

## Task 20: k16said acceptance (runs when ko/07-debugsocket exists)

Not runnable in P3 (section 1 item 2). The step the socket topic's plan must end with, against a build with P3 and the
socket:

```sh
cd $HAR/i18n
python3 k16said.py --bin <build>/scummvm --game $PACK --gameid kq1sci --lang ko \
	--pairs ~/work/scummvm/runs/walk/kq1-pairs.tsv --targets ~/work/scummvm/runs/kq1/kor/k2targets.json \
	--report <out>/kq1-said.txt
head -2 <out>/kq1-said.txt
# required: "pass" >= 303 in "335 variants of 76 commands: pass N, ..."
```

`k15parse.py` writes `sci_hangul_input=true` into its ini; the new key is `hangul_input` (auto is already on with a table),
so no change is needed for the parse; the harness tool stays read-only. Until then P3-notes records the criterion as
pending.

---

## Task 21: PATCHES.md, notes, acceptance run

- [ ] **Step 1: PATCHES.md** — one row per upstream file of section 2 under `ko/03-sci` (file, change, why), plus
  `Makefile.common`/`ports.mk` (install `hires_text/`) and `test/module.mk` (SCI suites).
- [ ] **Step 2: acceptance runs** (all from scratch build dirs):
  - `$BLD`: `heavy make -j8 test` → N0 + 46 tests OK; `make scummvm` links with no warning from a P3 file
    (`grep -E "warning" make.log | grep -E "engines/sci/(textmode|utf8|korean_input|engine/script_strings|parser/korean_parser|graphics/hirestext)"` empty).
  - ASan: `heavy $SRC/configure --disable-all-engines --enable-engine=sci --enable-debug --disable-optimizations --enable-asan`
    in `~/work/tmp/scummvm2026-p3asan`, `make test` OK, no `Sanitizer` line; one [data] dummy run of `[kq1-ko]` for 20 s
    with the typed command of Task 19 under Xvfb: no sanitizer report.
  - SCI32: `--enable-engine=sci,sci32` builds (`~/work/tmp/scummvm2026-p3sci32`).
  - No-engine build (`~/work/tmp/scummvm2026-p2verify` configuration): N0 tests OK (the SCI suites are not compiled).
  - mingw: `x86_64-w64-mingw32` configure with SCI, `test/runner.exe` builds (not run: no wine).
  - `python3 -I tools/korean/test_strcheck.py`, `test_k6bake.py`, `sync-svf.sh --check`, `mkgolden.py --check`.
  - `git diff --stat d7f47c73a07.. -- backends gui base common graphics` empty (P3 touches none of them).
  - Each commit message passes `devtools/check-commit-msg.py` and ends with exactly one `Assisted-by: Claude Code`.
- [ ] **Step 3: P3-notes.md** — deviations, the Task 19 results (screens described, sha256 of the references, the
  particle sentences), what was skipped for want of data or display, the k16said status.
- [ ] **Step 4: commit** — `DOCS: Korean: Record the P3 patches, deviations and the acceptance run` (adds this plan if it is
  still untracked, PATCHES.md, P3-notes.md).

---

## Acceptance checklist (row P3 "Done when" and "Tests")

| Criterion | How it is verified | Without KQ1 data | Without a display |
|---|---|---|---|
| tests `script_strings`, `korean_input`, parser arrangement | Tasks 4, 6, 5: 8 + 7 + 6 tests; plus `textmode` 6, `utf8` 15, `hirestext` 4 | run (no data needed) | run |
| i18n gate with real data (skips without data) | Tasks 15, 16, 19 [data] steps; `SCUMMVM_TEST_I18N_DATA` is not used: `getenv` is forbidden in tests (`common/forbidden.h`), so the real-data gate is the scripted headless run, skipped with a note when the pack is missing | skipped, reported | dummy-driver steps run (`SDL_VIDEODRIVER=dummy`: table loads, header lines, detection); screenshots skipped |
| KQ1 KO pack plays headless; first room screenshot matches reference | Task 15 Step 3 + Task 19 Step 5 (reference made and recorded in P3) | not verifiable | not verifiable (needs Xvfb; it is on this host) |
| `k16said >= 303/310` | Task 20 (needs ko/07-debugsocket); in P3: Task 5 unit tests + Task 19 Steps 1-2 typed in play | not verifiable | not verifiable |
| save/load with a Korean name | Task 19 Step 3 (original dialog: `문` saved, listed, restored) | not verifiable | not verifiable |
| EGA and VGA targets both render | read as CLUT8 and RGB screens (section 1 item 1): Tasks 15 and 19 on `render_target=clut8` (default) and `rgb565`; the composite code is shared with the P1 sink tests | not verifiable | not verifiable |
| `TextMode`, `ScriptStrings` + fingerprint, `readChar`/uint32 `GfxFont`, `TextLayer` + compositing, `GfxCache` resolver, text16 UTF-8, edit control, composer + surrogate, `korean_parser`, `-ko` variants, `kFormat` particles | Tasks 2-17 (each its build, units where the logic is engine-free) | built and unit-tested | same |
| fallback face installed (P1 review m-1) | Task 1 (`make install-data`) | yes | yes |

## 4. Left for later phases (do not do in P3)

- ko/07-debugsocket: the socket, the SCI `state`/`wait`/`dump` extensions; then Task 20.
- P4: per-game tables (`koreanAnswerView`, Aziza door), the `sci11StringsStart` hook, DYNMEM growth for long translations,
  `|code|` font changes in UTF-8 lines, other SCI packs' maps converted to v3 with OFL faces (harness), the `kNodeValue`
  workaround.
- P7: streamed faces, `[preload]` warm-up, the DOS keyboard.
- P8: the Korean options tab (keys above), the KO/EN badge, `scummvm.rc`/bundle packaging of `hires_text/`.
- Harness (not this repository): convert `dos/release/kq1/HIRES*.MAP` to map version 3 with OFL faces; write the
  fingerprint header into `sci-ko.str` and build `sci-ko.dat` as version 3 (`strcheck.py header`, `k6bake.py --format 3`);
  rename `sci_hangul_*` keys in `k15parse.py` when it is next edited.
