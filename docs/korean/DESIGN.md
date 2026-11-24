# Korean (Hangul) support for ScummVM 2026.4.0 with svf-fonts-open: design

Status: design with the owner's decisions (section 12), 2026-11-22. Nothing in this document is implemented on `ko-2026.4`; the branch is
v2026.4.0 plus the `dists/svf-fonts-open` submodule (commit bc9abd1c92d).

Sources read for this design: the 2026.4.0 tree (`common/`, `graphics/`, `gui/`, `engines/{sci,scumm,agi}`),
branch `jamo` of the same repository (1131 commits over its upstream base 96b42d42bc8; the Korean-only diff is
780 files, +144,652/-1,672), the svf-fonts-open library (`c/include/svf.h`, `c/README.md`,
`docs/c-renderer-guide.md`, `spec/SVF.md`, `spec/renderer-design.md`, `c/bench/MEASUREMENTS.md`,
`c/cross/SIZES.md`) and the owner's recorded decisions (memory files; each is re-checked against code below).
Claims that could not be checked in code are marked UNVERIFIED.

## 1. Goals and non-goals

Goals

- Play Sierra SCI0/SCI1 parser games, AGI games and SCUMM v2-v6 games in Korean: translated text drawn in
  readable Hangul at 2x the game resolution, and Korean sentences typed into the parser games understood by the
  games' own English parser.
- One shared text core (`graphics/hires_text`, `graphics/svf`) used by every engine; each engine patch small and
  reviewable; the fork rebases onto each new ScummVM release at low cost.
- Font faces come from svf-fonts-open (SVF v3, OFL) drawn by its C renderer; the engine never carries a
  proprietary font. TrueType stays available where FreeType is (desktop), never required.
- The same engine binaries run the DOS port (DJGPP, 16 MB, no FreeType) with streamed faces.
- Every claim about text is testable: unit tests on the core, golden pixels against the Python reference,
  per-game smoke runs through the debug socket, DOSBox-X runs for DOS.

Non-goals

- Translating the games (that work lives in the harness repository `~/work/scummvm`, see section 3).
- SCI32, SCUMM v7/v8 (FT/Dig), FM-Towns, Mac, PC-Engine and SegaCD platform variants in the first rounds.
- Nancy, Harvester, AGS, Grim in the first rounds: `jamo` holds no Korean work for Nancy, Harvester and
  MediaStation at all (their diffs against v2026.4.0 are upstream commits; `git diff 96b42d42bc8 jamo --
  engines/nancy engines/harvester` is empty), and the AGS (+4,748) and Grim (+975) work is large and engine-bound.
- Upstream submission as part of this plan. `AI-GUIDELINES.md` in the 2026.4.0 tree forbids AI-authored code;
  anything sent upstream must be re-authored by a human. Section 9 only lists what would be worth that effort.
- A new SVF format feature. The format is frozen (owner, 2026-10-28); the renderer is used as is.

## 2. Findings from the `jamo` branch

Numbers are `git diff --numstat 96b42d42bc8 jamo`. The branch mixes three things that must be separated:
the Korean feature, the DOS port (`backends/platform/dos` 10,730 lines plus SDL3 patches, `--disable-gui`,
lazy audio maps, Tremor), and dev tooling (debug socket 2,100 lines in `gui/` + 1,700 in engines, `tools/korean`
10,084 lines, `docs/` 17,594 lines).

### 2.1 Shared core: `graphics/hires_text` (62 files, 16,998 lines; 30 test headers, 13,732 lines)

- One interface, `UnicodeGlyphSource` (`glyph_source.h`): `cells(cp)`, `row(cp, y)`, `advance(cp)`,
  `metrics(cp)`, `baselineRow()`, `prefetch()`, `preload()`. Rows have a fixed stride of `cellWidth * 2` pixels
  at the face's bpp; `TextCompose::expandGlyphRow` depends on that layout.
- Sources: `HiResBitmapFont` + `SvfnGlyphSource` (SVFN **v1/v2 only**, `kMaxVersion = 2` in `bitmap_font.cpp`;
  whole or streamed with a 4 KB block and a 32 KB LRU), `JamoGlyphSource` + `jamo_set.cpp` (JSET v1 block,
  OR/max composition), `TtfGlyphSource` (FreeType, `#ifdef USE_FREETYPE2`), `ScvmuniGlyphSource` (SCI `.uni`
  bundles), `MissingGlyphSource` (U+25A1 box). Combinators: `FallbackGlyphSource` (chain),
  `NormalizedGlyphSource`, `RangeRoutedGlyphSource` + `pickGlyph()` (plan: `[glyphs]` target, range chain,
  SCUMM borrowed charset, missing box, `kGame`).
- Map: `HiResFontMap::loadMap` (`font_map.cpp`, 1,998 lines) parses the INI map (`[map] version=2`, `[render]`,
  `[text]`, `[layout]`, `[fonts]`, `[font]`/`[font.N]` with `face=`, `size=`, `cell=`, `align=`, `shift=`,
  `missing=`, `advance=`, `range.<spec>=`, `[load]`, `[preload]`, `[glyphs]`, `[shadow]`, qualifiers
  `[S:engine]`, `[S:clut8|rgb]`). `compileIdPlan()` (`id_plan.cpp`) makes one `HiResIdPlan` per engine
  font id. `pickHiResMap()` (`map_pick.cpp`) looks for `HIRESTTF.MAP`, `HIRESVF.MAP`, `HIRESBIT.MAP` in the
  language folder (`ko/`) then the game folder, order TTF -> SVF -> BIT unless `hires_text_font` says otherwise.
- Layout: `TextLayout::fitLine/breakLines` (`text_layout.cpp`, 831) with `BreakRules` (Hangul word|any,
  kinsoku, Thai) over `unicode_props.cpp` (652). Engine-neutral, unit-tested.
- Planes and compositing helpers: `TextCompose` (expand coverage, `composeSpan`, `stampSpan`),
  `KeyedCompose` (CLUT8 keyed composite, MMX variant for DOS), `BandedPlane` (16-row bands, 4-bit packed),
  `HiResGlyphRenderer` (outline/drop/stroke decoration; SCUMM only).
- Config: `hires_options.cpp` (665) reads `hires_text`, `hires_text_map[_ttf|_svf|_bitmap]`, `hires_text_font`,
  `hires_text_face`, `hires_text_size`, `hires_text_scale`, `hires_text_blend`, `hires_text_advance`,
  `hires_text_log`, `render_target`, `hires_text_platform_truecolor`, `hires_text_platform_scale`.
- Coupling: no include of `engines/`, `backends/` or `gui/`; engine knowledge is data (`kHiResKeysSci|Scumm|
  Ags|Agi` masks, SCUMM borrowing in `pickGlyph`). Depends on `common/file-cache-stats.h` (new) and a
  `faceIndex` argument added to `Graphics::loadTTFFont` (`.ttc` support).
- Tests: no golden-pixel test against the Python reference; fixtures are hand-built byte by byte
  (`test/hires_test_fonts.h` bakes an SVF from NotoSansKR at test time with `HiResFontBaker`, FreeType only).

Verdict: the map model, plan compilation, layout, planes and compositing helpers are reusable. Everything that
reads, caches or composes SVF glyphs (`bitmap_font`, `glyph_source_svfn`, `glyph_source_jamo`, `jamo_set`,
`codepage_kr` as an index helper, the chain part of `glyph_source_fallback`) is replaced by the C renderer, and
the shipped `.SVF` files it read are v2: they must be converted with `tools/svf2to3.py` (the C renderer returns
`SVF_E_VERSION` for v2; svf-fonts-open already ships every face as v3).

### 2.2 SCI (98 files, +12,713/-433; plus `common/` 2.3k)

- Text arrives five ways (`sci.cpp`/`sci.h` predicates `heapStringsAreUtf8()`, `usesKoreanText()`,
  `usesHiresDoubleByteText()`, `getSciLanguageCodePage()`, ini `text_encoding=auto|euc-kr|cp949|utf8|ascii`):
  (A) legacy `Text.MAP`/`Text.Res` overlay (`engine/text_overlay.*`), (B) CP949 resources patched in place
  (Laura Bow, Camelot detection entries), (C) UTF-8 `text.NNN` patches or the same packed into
  `MESSAGE.MAP`+`RESOURCE.MSG` by `tools/korean/packvol.py`, flagged `ADGF_UTF8I18N`, (D) `sci-<lang>.str`
  runtime overlay of script strings (`engine/translation.*`, class `ScriptStrings`), (E) the ini override.
- Heap strings stay `Common::String` bytes (CP949 or UTF-8). `GfxText16::readChar()` yields a `uint32` code
  point (`Sci::decodeUtf8Char` in `utf8.cpp`, or `decodeCodePagePair()` through a static KS X 1001 table so
  `encoding.dat` is not needed). `GfxFont` was widened from `uint16` to `uint32` (`scifont.h`) and gained
  `beginString()`/`prefetch()`. `kStrLen`/`kStrAt`/`SegManager::strncpy` count code points on a UTF-8 heap;
  `kStrAt` falls back to byte offsets past the end (Camelot's 36-byte save records, memory `camelot-ko-fix`).
- `sci-ko.str`: UTF-8 lines `script<TAB>id[<TAB>room]<TAB>text`, key = (script, string id from
  `Script::identifyOffsets()`, room|any). `SegManager::stringKey(reg_t)` maps a heap pointer back to the key;
  `kStrCpy`/`kStrCat` tag buffers so a copied string is still translatable. Lookups in `strSplitLanguage()`,
  inside `kFormat` (format string and each `%s`), and `translatedHeap()` in `kAddMenu`/`kSetMenu`. Untranslated
  strings pass through unchanged. **No version or checksum matching**: correctness rests on the detection entry
  and exact id numbering (`sci11StringsStart()` fixes a heap-start bug for SCI1.1).
- `kFormat` runs `Common::resolveKoreanParticles()` (을(를), 이(가), 은(는), 과(와), (이), (으)) and drops a plural
  `%c`/`%s` "s" after a Hangul syllable (memories `qfg2-korean`, `qfg1e-korean`, `lsl1-korean` confirmed).
- Hi-res layer: `TextLayer` (`graphics/textlayer.*`, 2x plane in 16-row bands of {palette index, coverage}),
  written by `GfxScreen::putHiresCoverageGlyph[At]()`, kept in step with the visual plane in `putPixel`,
  `bitsSave/bitsRestore`, `paint16` picture draws, inverts (`xorTextLayerColors`), dither fills; composited per
  band in `UpscaledGfxDriver::updateScreenBand()` (`drivers/upscaled.cpp` +166): CLUT8 `stampSpan`, RGB
  `composeSpan` through `_currentPalette`. `drivers/init.cpp` gains a `hiresText` column selecting the upscaled
  driver for SCI0..1.1 when `usesHiresDoubleByteText()`. EGA 16-colour games keep palette indices; invert is
  XOR 0x0F; undithering forced to default.
- Fonts: `GfxCache::getFont()` grew by 1,002 lines (`createFontSet()`, `faceChainFor()`, TTF/SVF caches,
  coverage probes, `hires_text_log` tallies). Font 1001 remains the legacy Korean id
  (`SwitchToFont1001OnKorean`, upstream `fontkorean.cpp` + `graphics/korfont.cpp`).
- Input: `EventManager` reads `sci_hangul_input`, `sci_hangul_builtin`, `sci_hangul_start`; keymapper action
  `kSciActionHangulToggle` (HANGUL, RALT, C+SPACE, S+SPACE, SCROLLOCK, F12) swallowed before scripts; OS IME:
  a KEYDOWN with `keycode == KEYCODE_INVALID` and `ascii >= 0x100` is stored in `_lastOsImeKey` and the script
  receives `kOsImeSurrogateKey` ('x') so SCI0 scripts open the prompt (memory `korean-input-path` confirmed);
  built-in composer `Common::HangulComposer` (`common/hangul.*`, 2-set QWERTY) wrapped by `Sci::KoreanComposer`
  (`graphics/koreaninput.*`); edit control `GfxControls16::kernelTexteditChange` (+449) steps by code point.
- Parser: `SCIKOR.DAT` = `Common::KoreanVocabulary` (`common/korean_vocab.*`: magic `SCIKOR\0\0`, u32 version 2,
  entries {u16 offset, u16 length, u16 group, u16 class} over a sorted UTF-16LE blob; bit 0x8000 = sequence
  word "look in"); `Vocabulary::lookupWord` tries English, then English+particle, then
  `KoreanVocabulary::lookupAll()` with `koreanStemCandidates()` (`common/korean_stem.cpp`, 798 lines, no heap);
  `tokenizeString` calls `joinKoreanPairs()`, `arrangeKorean()` (verb first, objects, role -> preposition phrase
  from the game's own vocabulary), `koreanAskAbout()`; a fallback tree is parsed (`parseGNF(..., nodes)`) and
  `said()` retries on it after `SAID_NO_MATCH`. Game hack: `koreanAnswerView()` for QFG1 rooms 22/29 and QFG2
  room 199 (substring answers). The baker `harness/k6bake.py` is **not in the repository** (it is in
  `~/work/scummvm/harness`).
- Detection: `detection.cpp` +83 synthesises a `-ko` variant when `ko/sci-ko.str` or a `hires*.map` exists
  (`common/language_folder.cpp`, `Common::translationFolderLanguages()`, `getLanguageFolderCode()`).

Verdict: keep `common/hangul`, `korean_stem`, `korean_vocab` (widen offsets, add a header), `ScriptStrings`
(add a fingerprint), `utf8.*`, `dbcs.h`, `lowercase.cpp`, the uint32 `GfxFont`, the `TextLayer` + driver
compositing, the input design. Rewrite `GfxCache` face code, `hirestextsettings/state`, `fontset`,
`fontunicode`, `textlatin`, the edit control (same behaviour, a third of the code) and move the Korean
arrangement into its own unit. Drop `Text.MAP`, the `dump_*` helpers, lazy audio maps (DOS concern).

### 2.3 SCUMM (60 files, +13,245/-292)

- Upstream 2026.4.0 already has: `korean.trs` (`SCVMTRS` bundle, `loadLanguageBundle()`/`translateText()` in
  `string.cpp`, keyed by exact original string scoped by room/script), `korean.fnt`/`korean%02d.fnt` CP949
  fonts (`charset.cpp` `loadKorFont`, `_2byteFontPtr`, `_useCJKMode`), MI1/MI2 josa handling
  (`string.cpp:1807-1973`), `isScummvmKorTarget()`.
- The branch added: `<code>.trs` naming and `hasTranslationBundle()` gate (`trs_bundle.h`), UTF-8 bundles
  (BOM or `text_encoding=utf8`; `probeLanguageBundle()` either keeps UTF-8 for the hi-res layer or transcodes to
  CP949 for the legacy fonts), lazy `TrsStore` (hash index + 32 KB cache, for DOS), save-game text re-encoding
  trailer, `korean_sentence.cpp` (v2 MM/Zak verb-last sentence line, particles by final consonant, pure
  functions with a ~60-line hook in `drawSentence`).
- Hi-res: `ScummHiResText` adapter (`hires_text.h/.cpp`, 1,263 + 2,890 lines), `HiResOverlay` (index
  `Graphics::Surface` = the engine's `_textSurface` at `_textSurfaceMultiplier` x, coverage and two decoration
  planes as `BandedPlane`), `compositeTextRows<Sink>` with `HiResIndexSink`/`HiResPalette16Sink`/
  `HiResTrueColorSink`, `expandStrip` for transitions, `findPaletteUsersRows()` for palette cycling.
- Invasive: `charset.cpp` +753 (every `printChar`/`getCharWidth`/`addLinebreaks`), `gfx.cpp` +675 (composite
  paths in `drawStripToScreen`, plus kept/traced glyph erasure woven into `restoreBackground`,
  `restoreCharsetBg`, `clearCharsetMask`, `drawBox`, `drawLine`, `Gdi::drawBitmap`, `resetBackground`,
  `initScreens`), `string.cpp` +555, `scumm.cpp` +290, `scumm.h` +270.

Verdict: keep the TRS generalisation, `korean_sentence`, the overlay/composite/sink headers; rewrite the
adapter smaller and find a narrower erasure seam; FM-Towns/Mac/v7/PCE/SegaCD excluded.

### 2.4 AGI (42 files, +3,152/-341)

- Uses the existing upstream `DISPLAY_UPSCALED_640x400` mode (`graphics.cpp` `initVideo`) with 16x16 cells;
  `drawCharacterOnDisplay(uint32 cp)` asks `Agi::HiresText::glyph(cp, rows[16])` for a 1-bpp stencil
  (coverage thresholded at 128) and paints it with AGI's XOR/OR transforms. No text plane, no blending.
- Translation `agi-<lang>.str` (`translation.*`: UTF-8 lines `m<logic>:<n>`, `o<index>`, `v<view>`), applied
  in `translateLogicTexts()` at decode time; `compare.strings` accepts the English answer too.
- `textcells.*` (one cell per code point, wordWrap), `korparse.*` + `saidscan.*` (candidate English orders
  tried against `saidMatches()`), `hangulline.*` (composer in the prompt), save version 12 for UTF-8.
- Modified: `text.cpp` +425 (buffers become `uint32`), `words.cpp` +243, `op_test.cpp` +147, `agi.cpp` +141,
  `detection.cpp` +110, others small.

Verdict: port mostly as is; it is the cleanest engine integration on the branch.

### 2.5 Everything else

- `backends/platform/dos` + `backends/{graphics,events,timer,mixer,mutex,midi}/dos`: 15.6k lines, nine SDL3
  patch files holding 14 effective commits (P7: rebased onto upstream SDL `main` at 1362030, `sdl3-build.txt`;
  release-3.4.20 has no DOS port; the jamo keycode commit and its revert cancel and are dropped), `configure` +119 (`msdosdjgpp` host,
  `--disable-gui`, `--enable-debug-socket`), `PLAY.EXE` launcher, `--set=KEY=VALUE` (`base/set-option.h`).
- `gui/options.cpp` +157: a four-row block on the Graphics tab (`render_target`, `hires_text`,
  `hires_text_font`, `hires_text_blend`) shown when `MetaEngine::hasHiResText(target)` (the one generic
  hook, `engines/metaengine.h` +13). Theme layouts bumped to STX0.9.27 and zips rebuilt.
- `common/language.cpp` +71: ISO-639-1 folder codes; `common/keyboard.h`: `KEYCODE_HANGUL`, `KEYCODE_HANJA`;
  `common/str-enc.cpp`: `encoding.dat` tables loaded one at a time (about 250 KB saved).
- Data: `dists/engine-data/hires_text/dos/*.MAP` (14 games, 8.3 names) + `.HOT` preload lists;
  `dists/engine-data/hires_text/maps/*.map` (desktop examples); `engine_data.mk` ships `SANGAM16.SVF`
  as the fallback jamo face. **SANGAM16 (san 1.0) and the faces named in `KQ1KO.MAP` (HTERM16, IYAGI16, MUNJO24,
  HWPHG24) are not in svf-fonts-open** (non-OFL; `docs/STATUS.md` of svf-fonts-open: 116 OFL faces kept, iolo and
  san 1.0 left out). The public fork needs OFL re-pairings.
- Tests: 143 files, 39,799 lines (`test/graphics` 32 files, `test/engines/scumm` 32, `sci` 18, `ags` 9,
  `agi` 11, `test/common` 8: `hangul`, `korean_stem` + 1,199-line corpus, `korean_vocab`, `encoding_cjk_tables`,
  `language_folder`), all CxxTest, linked through `test/module.mk` `+111`.
- Tools (`tools/korean`, 47 files): `makemaps.py`, `sci0_kr_extract.py`, `sci11_kr_extract.py`, `packvol.py`,
  `scumm_trs_from_patch.py`, `scummscript.py`, `charsetchars.py` (+ `.HOT` writer), `facecoverage.py`,
  `bake-supplements.py`, `engine_gate.sh`; docs `README.md`, `SCUMM_FONTS.md`, `TRANSLATION_PIPELINE.md`.
  The jamo-set tools (`jamoset.py`, `mkfont.py`) live in svf-fonts-open `tools/`.

## 3. Internal charset strategy

Where each engine's data is encoded (2026.4.0 code):

| Engine | Game strings | Korean fan data | Upstream Korean path |
|---|---|---|---|
| SCI0/SCI1 | single-byte ASCII/CP437 in TEXT, script heap, vocab | CP949 in-place patches (LB1, Camelot entries in `detection_tables.h`), banked fonts | `GfxFontKorean` for font 1001 when `KO_KOR` (`graphics/cache.cpp:69`), `SwitchToFont1001OnKorean` (`text16.cpp:462`) |
| SCUMM v2-v6 | single-byte, CP949 2-byte when `_useCJKMode` | `korean.trs` + `korean.fnt` | full CP949 path in `charset.cpp`/`string.cpp` |
| AGI | CP850/ASCII in LOGIC messages, `WORDS.TOK` 7-bit | none | none (`grep KO_KOR engines/agi` is empty) |
| GUI | UTF-8 (`Common::U32String` from `po/ko.po`, 3,258 msgids, 301 untranslated) | - | `NotoSansKR` in `fonts-cjk.dat` via `<language id='ko'>` in `scummmodern_gfx.stx` |

Facilities that exist: `Common::CodePage` has `kWindows949` and `kJohab` (`common/str-enc.h`);
`U32String::decodeWindows949` needs `encoding.dat` (`loadCJKTables`, `str-enc.cpp:142`);
`Common::String::decode(CodePage)` / `U32String::encode(CodePage)`; `svf_draw_utf8` takes UTF-8 only
(`svf.h`: "the library takes UTF-8", no code-page guessing).

Options

| Option | How | Pros | Cons |
|---|---|---|---|
| A. UTF-8 bytes in the engine's own `Common::String` containers, decoded to code points only at the font, layout and input boundaries | translation files are UTF-8; `readChar()`-style decoders in text16/charset/AGI text; `kStrAt`/`kStrLen` count code points on a UTF-8 heap | no container type changes; one conversion-free path into `svf_draw_utf8`; any Unicode (11,172 syllables, symbols); what `jamo` proved on 8 games | `kStrAt`-by-index semantics must be handled (jamo did); fixed-record scripts need byte fallback |
| B. CP949/EUC-KR everywhere (MBCS) | keep upstream's SCUMM path, extend to SCI/AGI | matches old fan patches and `korean.fnt`; smallest SCUMM diff | 2,350 KS X 1001 syllables only; `encoding.dat` at run time; every DBCS step is a special case; conversion before every renderer call |
| C. `Common::U32String` through engines | change engine string members | clean Unicode | every `char *` script/heap interface changes; huge, unrebasable diffs |

RECOMMENDATION: **A**, with B kept only as an *input* format. Policy:

1. One decision per engine at init, held in one object (`Sci::TextMode`, `Scumm::TextMode`, `Agi::TextMode`:
   `{ encoding: utf8|cp949|single, hangul: bool, hires: bool }`), replacing the scattered predicates of `jamo`
   (`heapStringsAreUtf8`, `usesKoreanText`, `usesHiresDoubleByteText`). Decided from: detection language,
   language folder contents, `text_encoding` override.
2. Text enters as UTF-8: `.str`/`.trs` files are UTF-8 (BOM optional); a CP949 fan file is transcoded to UTF-8
   once at load (`String::decode(kWindows949).encode(kUtf8)`; needs `encoding.dat`, already shipped). SCUMM
   keeps upstream's CP949 path untouched for the legacy `korean.fnt` case (phase S1), and transcodes the other
   way only there.
3. Text leaves as UTF-8: the hi-res layer gets `const char *, len` straight from the heap; savegame
   descriptions and the debugger get the same bytes (the launcher shows them with the UTF-8 GUI font;
   SCI's `kStrAt`-indexed save records keep byte offsets past the end as `jamo` does).
4. Keyboard input is encoded into the heap's encoding at the edit control (composer emits code points;
   `TextMode.encoding` says how to store them). The OS-IME surrogate-key trick stays.
5. Upstream engines stay untouched where `TextMode.encoding == single`: every new branch is behind that check.

## 4. Translation resources

Owner decisions respected: packs are self-contained (`self-contained-game-packs`), translation sources are
committed in the harness repository and builders refuse other bytes (`translations-in-harness`), UTF-8 masters
with CP949 bundles derived at pack time.

Options

| Option | Verdict |
|---|---|
| PO/gettext per game | REJECTED: no stable ids (SCI strings are numbered by script offset; `kFormat` arguments are substituted at run time; room scoping needed); `TransMan` is for the GUI |
| One engine-neutral container (`SCIKOR.DAT`-like blob holding strings + vocab + maps) | REJECTED: couples unrelated release cycles; the harness already produces per-kind files |
| Per-engine text files keyed by the engine's own ids (jamo formats) + a fingerprint header | RECOMMENDED |

Design

- Files and keys (formats from `jamo`, one header line added):
  - SCI: `ko/sci-ko.str` (`script<TAB>id[<TAB>room]<TAB>text`), UTF-8 `text.NNN` patches or `MESSAGE.MAP` +
    `RESOURCE.MSG` for whole TEXT resources, `ko/SCIKOR.DAT` for the parser. `kAddMenu`/`kSetMenu` and
    `kFormat` arguments go through the same table (object names are script strings, so titles translate).
  - SCUMM: `ko.trs` (`SCVMTRS` as upstream; UTF-8 body with BOM), upstream `korean.trs` accepted.
  - AGI: `ko/agi-ko.str` (`m<logic>:<n>`, `o<i>`, `v<view>`), `ko/AGIKOR.DAT`.
  - New header line in every `.str`: `#! format=1 game=<gameid> fingerprint=<md5 of the resource the keys
    depend on: SCI script.000+vocab.000 (or resource.map); AGI logdir+words.tok> engine=<sci|agi>`. The engine
    computes the same md5 from the files it opened (`Common::computeStreamMD5AsString`, used by the detectors)
    and refuses a mismatching table with one warning naming both values; a missing header loads with a warning
    (old packs). `SCIKOR.DAT` gets version 3: u32 offsets, the same fingerprint and the vocab's md5.
- Where files live: the game folder's language subfolder `ko/` (added to `SearchMan` with
  `addSubDirectoryMatching(gameDir, "ko")` as `jamo` does); loose files in the game folder accepted for old
  packs. Maps (`HIRESVF.MAP`, `HIRESBIT.MAP`, `HIRESTTF.MAP`) sit beside them and name shared faces with
  `data:` paths resolved through the extra path, `dists/engine-data`, the game folder and `SearchMan`
  (`font_map.cpp` `dataRoots()`, kept). Nothing per game goes into engine data.
- Picking: detection (`translationFolderLanguages()`) adds a `-ko` target variant per matched game when
  `ko/` holds a `.str`/`.trs`/map; the user picks the Korean target or sets `language=ko`.
- Fallback: an untranslated string is shown as the original (English) with no marker; `hires_text_log=true`
  logs each miss once (script, id) so the harness can diff coverage. Unknown parser words fall into the
  game's own "unknown word" line (which is itself translated: KQ1 script 0 id 12 in `sci-ko.str`).
- Tooling (harness repository, not ScummVM): existing builders `build_sci_*.py`, `build_agi_lsl1.py`,
  `build_scumm*.py`, `k16said.py` (parser check), `k18names.py`; new: `strcheck.py` (header, duplicate keys,
  escapes, every key resolvable in the dumped script strings) run by every builder. ScummVM keeps only
  `tools/korean/{sci0_kr_extract,sci11_kr_extract,packvol,scumm_trs_from_patch,scummscript,charsetchars,
  facecoverage}.py` and their tests; the shell scratch scripts stay out of the fork.

## 5. Hangul output

### 5.1 Two-times hi-res text over the 1x game screen

Per engine the pixel pipeline differs; the shared part is the plane model from `jamo`: text is a separate
2x plane of {palette index, coverage} kept in step with the game's 1x paletted frame and composited when the
frame is sent to the backend. Indices, not colours, so palette fades and cycling recolour text drawn earlier.

| Engine | Where the plane lives | How it is composited | Palette | Dirty rects / screenshots |
|---|---|---|---|---|
| SCI0/1 | `Sci::TextLayer` owned by `GfxScreen`, banded; the driver is `UpscaledGfxDriver` (upstream `drivers/gfxdriver_intern.h:90`, already used for KQ6/PQ2 hires) | `UpscaledGfxDriver::updateScreenBand()`: CLUT8 `stampSpan` (threshold), RGB `composeSpan` through the current palette | 16 EGA entries or 256 VGA, index stored; invert = XOR 0x0F | driver's own rects, 2x; screenshots are the backend's (composited) |
| SCUMM v2-v6 | `HiResOverlay` = upstream `_textSurface` at `_textSurfaceMultiplier` (the FM-Towns 2x mechanism) + coverage `BandedPlane` | `drawStripToScreen`: keyed composite (CLUT8) or blended band buffer into `_compositeBuf` | VGA 256 / EGA 16 index | strip rects scaled by m; `markPaletteUsersDirty` on cycling |
| AGI | none: 16x16 stencils into the existing 640x400 display buffer | `drawCharacterOnDisplay` paints fg/bg | EGA 16 | upstream |

Render target: `render_target=auto|clut8|rgb565|rgb555|rgb888`. `auto` = RGB when the chosen map has a
coverage face (2 bpp AA) and the backend offers it, else CLUT8; the RGB format is 32-bit (888) where the backend
offers it, else 16-bit (565), else 15-bit (555, also XRGB1555 and RGBA5551). 15 and 16 bit are required to work
(DOS VBE, backends without 32 bit) and are what is chosen there; they are not preferred on a 32-bit desktop,
because they halve the colour levels of the whole VGA picture. The SCI driver asks the backend for the one format
of the resolved target, else of the next RGB target (`hiResPickScreenFormat`; `initGraphics` takes the first backend format
its list has, so the list is that one format), and the text follows the screen it got (`SciHiResText::setScreenFormat`: another RGB format, or CLUT8, loads the map's `[font:clut8]`/`[font:rgb]`
sections again). 1-bpp faces look the same on both. Anti-aliased faces are for the RGB targets (15/16-bit
mandatory); a map keeps CLUT8 on a 1 bpp face with a `[font:clut8]` section.

### 5.2 Font technology

| | TTF (FreeType, `Graphics::loadTTFFont`, `graphics/fonts/ttf.h`) | SVF via the svf-fonts-open C renderer |
|---|---|---|
| Availability | `USE_FREETYPE2` builds only; DOS port has none (`--disable-freetype2`); fonts-cjk.dat 36 MB | every platform: C99, `<stdint.h>`/`<stddef.h>`/`<string.h>`, no malloc (`c/README.md`), DJGPP 15.4-33.6 KB text (`c/cross/SIZES.md` P1..P4) |
| Hangul | any font with Hangul | precomposed faces, or jamo sets (13-20 KB for all 11,172 syllables, `spec/renderer-design.md`) |
| Pixels | rasterised at run time, differs per FreeType build | byte-identical to the Python reference (`tools/svfline.py`), exact by default (`svf-renderer-accuracy-policy`) |
| Baked look | needs hinting tuning per size | faces baked and reviewed per size (owner's per-game workflow) |
| Memory | FreeType ~0.5 MB code + per-face cache (jamo DOS spec section 4.4) | face struct 96 B; streamed tables 8 KB (jamo face) to 46 KB (precomposed 16 px, `c-renderer-guide.md` 4.2) |
| Scaling | native | integer N = pixel copy, else area average (`svf_chain.scale_num/den`) |

RECOMMENDATION: **SVF primary everywhere; TTF optional on FreeType builds only, through the same
`Graphics::Font` seam, last in priority (`hires_text_font=auto` order becomes SVF -> BIT -> TTF)**. This
inverts `jamo`'s TTF-first order: the owner's workflow makes the 1-bpp jamo face the base every target falls
back to, then an AA face, then TTF (`per-game-font-workflow`).

What the C renderer guarantees (from `svf.h`, verified by its own `make -C c check`): file validation at open
(every table, record, JSET/JMAP block; fuzzed, safe with user files unless `SVF_JSET_TRUSTED`), mapped and
streamed faces with an LRU block cache, `svf_has`, `svf_metrics_of`, `svf_glyph`/`svf_compose` (jamo
composition, max coverage), chains (first face that has the code point, `missing_advance`), combining marks,
`svf_measure_utf8` (no glyph read), `svf_draw_utf8` into ten target kinds with clipping, integer and
fractional scaling, a composed-syllable cache (`svf_chain_cache`), invalid UTF-8 handling, thread rules (one
face/chain per thread), and byte-identical output to the Python tools.

What ScummVM glue must do: file I/O (`svf_read_fn` over `Common::SeekableReadStream`; whole-file read for
small faces), memory ownership (face structs, tables, block cache, syllable cache, scratch from
`svf_chain_scratch_bytes`), face selection (map), text decoding to UTF-8, line breaking (`TextLayout`),
plane writing (via `SVF_T_COVERAGE_CB` or `SVF_T_GRAY8` into a scratch cell, then `TextCompose`),
colours and palettes, the missing-glyph box (U+25A1 from a face, else `missing_advance`), the `[preload]`
warm-up (draw the `.HOT` list once into a null target so the block cache fills), `svf_status` checks
(`JSET_BAD`, `JMAP_UNREAD`) reported once per face, and consistent compile switches
(`-DSVF_JMAP=1 -DSVF_JSET_BIG=1 -DSVF_JMAP_RECOG=0` added to `DEFINES` by `configure` for every TU, since
`SVF_JMAP` changes `sizeof(svf_face)` and its default depends on `__unix__/_WIN32/__APPLE__/__DJGPP__`,
which leaves 3DS/Wii/PSP/Switch builds without JMAP).

Layered glue (new, `graphics/svf/` and `graphics/hires_text/`):

1. `graphics/svf/svf.h` + the six C files (vendored, section 9), compiled by the existing `%.o: %.c` rule
   (`Makefile.common:172`).
2. `Graphics::SvfFace` (`graphics/hires_text/svf_face.h`): RAII owner of one `svf_face` + buffers; opens
   mapped (file <= `svf_whole_kb`, default 256) or streamed (block 1 KB, slots from the `[load]`/`[preload]`
   rules); `status()`, `has(cp)`, `cellBytes()`.
3. `Graphics::SvfChain`: an `svf_chain` over `SvfFace`s plus syllable cache and scratch; `measure(utf8)`,
   `drawGray8(utf8, cell callback)`, `drawCoverage(utf8, plot)`.
4. `Graphics::SvfFont : Graphics::Font` (`graphics/fonts/svffont.h`): `getFontHeight` = `cell_h`,
   `getCharWidth` = `svf_metrics_of`, `drawChar` = one-code-point `svf_draw_utf8` through `SVF_T_COVERAGE_CB`
   blending into the `Surface` in its `PixelFormat` (CLUT8: threshold). Lets the GUI, AGI and any simple
   engine use SVF through the font API they already have (`Graphics::Font::drawString(U32String)`).
5. `Graphics::HiResGlyphSource` (the trimmed `UnicodeGlyphSource`: `cells`, `coverageRow`, `advance`,
   `baseline`), implemented by `SvfGlyphSource` (over `SvfChain`) and `TtfGlyphSource` (kept from `jamo`
   behind `USE_FREETYPE2`); consumed by `RangeRoutedGlyphSource` and the engine planes.
6. The map (`font_map.cpp`, kept; keys cut to what SVF needs: `[map] version=3`, `[render]`, `[fonts]`,
   `[font]`/`[font.N]` with `face=`, `size=`, `cell=`, `align=`, `shift=`, `missing=`, `advance.<spec>=`,
   `range.<spec>=original|same|<chain>`, `[load]`, `[preload]`, `[layout]`, qualifiers `:clut8|:rgb`,
   `:sci|:scumm|:agi`; dropped: `pixel=`, `gamma=`, `[shadow]`, `[glyphs]` until SCUMM needs them, `[text]`).
   A v2 map is refused with a message naming the key to change.

Face selection per game and size: the pack's `HIRESBIT.MAP` (1-bpp jamo faces) and `HIRESVF.MAP` (2-bpp AA)
name per-engine font id -> face chain -> last face `data:NEODG16A.SVF` (OFL Neo Dunggeunmo 16 px jamo set,
`svf/dos/NEODG16A.SVF`, shipped in `dists/engine-data/hires_text/` as the engine fallback replacing SANGAM16).
Bitmap faces are base + supplement (`bitmap-face-base-plus-supplement`): `facecoverage.py --base-check` in the
builder proves every translation character is covered, so no run-time box appears in packs.

Licensing hygiene: the ScummVM repository and engine data carry OFL faces from svf-fonts-open only (NOTICE.md
there lists each face's licence file); `LICENSES/COPYING.OFL` and `COPYING.MIT` already exist in the tree. The
owner's rule that packs may use any font by look (`font-licence-not-criterion`) applies to the private packs
built in the harness, never to files committed here; a builder flag `--public` refuses non-OFL faces.

## 6. Parser games (SCI0/AGI)

Input path (kept from `jamo`, verified in section 2.2):

1. OS IME (Windows, Android, ibus): finished syllables arrive as `KEYDOWN` with `keycode == KEYCODE_INVALID`,
   `ascii >= 0x100`; SCI0 scripts open the prompt only on printable ASCII, so the event manager hands the
   script `kOsImeSurrogateKey` and the edit control swaps the real code point in. AGI needs no surrogate (the
   prompt is always open) but takes the same code point event.
2. Built-in composer (`Common::HangulComposer`, 2-set): toggled by the keymapper action `HANGULTOGGLE`
   (default bindings HANGUL, RALT, C+SPACE, S+SPACE, SCROLLOCK, F12), on by default when `ko/SCIKOR.DAT` exists
   (`hangul_input`, `hangul_start`, `hangul_builtin` keys; engine-neutral names, section 7). Preedit drawn
   inverted; KO/EN badge (`graphics/korean_badge`).
3. DOS: the Han/Yeong key (`0xF2`) reaches the engine as `KEYCODE_HANGUL` (new keycode in
   `common/keyboard.h`; SDL3 patch + `backends/events/dos`); verified in DOSBox-X with a mapper file
   (`han-yeong-key-dos`).
4. Android: on-screen Hangul layouts (`dists/android/res/xml/*hangul*`, +537 lines Java/JNI) stay a later
   phase.

Matching Korean to `said()` (kept, repackaged):

- Data per game: `SCIKOR.DAT`/`AGIKOR.DAT` (Korean word -> vocabulary group + class, sequence words), built in
  the harness from `ko.tsv`, `k3map.json`, `k2targets.json` (memories `kq4-korean`, `qfg2-korean`); the baker
  `k6bake.py` moves into the ScummVM `tools/korean/` so the fork can rebuild tables (Decision 7).
- Engine: `engines/sci/parser/korean_parser.{h,cpp}` (new unit holding `arrangeKorean`, `joinKoreanPairs`,
  `koreanAskAbout`, roles -> prepositions from the game's own vocabulary) with three hooks:
  `Vocabulary::lookupWord` fallback, `parseGNF` of the second (role-free) tree, `said()` retry on it.
  Stemming is `common/korean_stem` (rule set, no data file). AGI: `korparse` candidate loop in `testSaidSpec`.
- Normalisation: NFC code points (the composer and IMEs emit precomposed syllables; a decomposed input is
  composed by `HangulComposer::composeSyllable`), full-width ASCII folded, case folded by
  `parserLowerCaseWord` (DBCS-safe).
- Game hacks (`koreanAnswerView` for QFG1/QFG2 substring rooms, Aziza's door) become a per-game table in
  `korean_parser.cpp` keyed by game id + room, not `kstring.cpp` branches.
- Verification: `k16said.py` over the debug socket (console `parse`/`said` exist upstream in
  `engines/sci/console.cpp`; the socket transport is the fork's `gui/debugsocket`, section 10). Acceptance
  numbers from `jamo`: KQ1 303/310, LB1 601/744, Camelot 549/633, KQ4 464/487 (`--early`), QFG2 1654/1876,
  QFG1 EGA 855/956 variants; a port must reproduce them.
- Degradation: no table -> English only; a word the table and stemmer do not know -> `lookupWord` returns no
  group -> the game's own "unknown word" response (translated); a sentence that parses but matches no spec ->
  the game's own "I don't understand".

## 7. GUI and configuration

The simple switch: the target's `language=ko` (existing `Common::KO_KOR`, `common/language.cpp:79`), set by
detection when the `ko/` folder exists or chosen in Edit Game > Game > Language. Everything else defaults
from it: `TextMode` on, hi-res on, SVF map auto-picked, Hangul input on when a parser table exists. A global
`korean_text=false` in `[scummvm]` turns the feature off everywhere (debug aid).

Keys (ConfMan, engine-neutral; `jamo`'s `sci_hangul_*`/`agi_hangul_*` renamed):

| Key | Values | Default | Read by |
|---|---|---|---|
| `hires_text` | bool | true | hires core |
| `hires_text_font` | auto, svf, bitmap, ttf | auto (svf -> bitmap -> ttf) | `pickHiResMap` |
| `hires_text_scale` | auto, 1, 2 | auto (2 at 320x200) | engine |
| `hires_text_blend` | auto, on, off | auto | compositor |
| `render_target` | auto, clut8, rgb565, rgb555, rgb888 | auto | engine driver choice |
| `hires_text_map` | path | - | override of the picked map |
| `text_encoding` | auto, utf8, cp949 | auto | `TextMode` |
| `hangul_input`, `hangul_start`, `hangul_builtin` | bool | auto/true/true | event manager |
| `hangul_break` | word, any | word | `TextLayout` |
| `hires_text_log` | bool | false | misses/coverage log |
| `hires_text_whole_kb` | 0..16384 | 256 (DOS 64) | a face file up to this many KB is read whole, a larger one streamed (P7) |

Surfaces:

- Edit Game: a new tab "Korean" built by a shared `GUI::KoreanOptionsWidget` (`gui/koreanoptions.{h,cpp}`,
  grown from `jamo`'s `hirestextoptions`), shown when `MetaEngine::hasHiResText(target)` (the one generic
  hook kept) or the target language is Korean; it writes the keys above into the game domain. Global
  defaults: the same widget inside Options > Misc under "Korean text" (writes the application domain).
  Per-engine engine options stay what they are (`ADExtraGuiOptionsMap`, `engines/sci/detection_options.h`);
  no new `GAMEOPTION_*` flags, so detection tables are untouched.
- Command line: existing `--language=ko` (`DO_OPTION('q', "language")`, `base/commandLine.cpp:870`) and
  `--set=key=value` (from `jamo`, `base/set-option.h`) for the rest.
- Launcher in Korean: upstream already covers it (`po/ko.po`; `NotoSansKR-Regular/Bold.otf` in
  `fonts-cjk.dat` selected by `<language id='ko'>` in `scummmodern_gfx.stx`, `ThemeEngine::loadExtraFont`).
  Work: contribute the 301 missing `ko.po` strings upstream through Weblate (human task); on non-FreeType
  desktop builds a `Graphics::SvfFont` can serve `kTextDataExtraLang` later (not in the first rounds). The
  in-game translation never depends on the GUI language.
- Savegame names and the save/load dialog show UTF-8 names with the GUI font; nothing engine-side.

## 8. DOS port

Constraints (from `scummvm-dos-port` memory, verified against `jamo` `backends/platform/dos` and the svf
benches): target real Pentium-class PC, 16 MB RAM, VBE 1.2+ (2.0 for 16 bpp); DJGPP gcc 12.2 32-bit,
SDL3 as HAL with 14 local patch commits (nine files in `jamo`), cooperative threads, `--disable-gui --disable-translation
--disable-freetype2`; three EXEs (`SCI.EXE`, `SCUMM.EXE`, `AGI.EXE`) and `PLAY.EXE`.

- Screen: no mode 13h in play; the game runs at 2x in a VESA 640x400 mode (P7, decision D-1: the backend offers
  RGB565, then XRGB1555, then CLUT8, each in 640x400 or through 640x480 with line repeat, and XRGB8888 only when
  `render_target=rgb888` asks for it; the engine picks the first: RGB565 on `auto`; CLUT8 640x400 for 1-bpp maps). The hi-res plane is
  composited in system RAM and dirty rects are copied to the LFB (`SDL_UpdateWindowSurfaceRects`). The svf
  `INDEXED8`/`RGB565`/`XRGB8888` kinds match these modes directly (`c/ports/dos/svfvesa.c` draws them).
- Fonts: streamed `svf_open_stream` with 1 KB blocks; a jamo face needs about 8 KB of tables + 64 slots
  (65 KB) for a line of syllables (`c/ports/dos/README.md`); `[preload]` lists warm the cache at load. Budget:
  `jamo` measured 1.26 MB of faces loaded for KQ1 (3.24 MB worst case with caches); streaming brings this to
  well under 0.5 MB. Speed: emulated 486DX2-66 draws a 40-code-point line in 11.9 ms mapped and 12.8 ms streamed
  warm (`c/bench/MEASUREMENTS.md`, DOS 486 table); a Pentium is several times faster; the glyphs of a four-line
  dialogue box are under a frame, but the RGB composite is not: measured in DOSBox-X (P7), one 400x128 box of
  a 2 bpp face took 137 ms on a 486DX2-66 preset with the per-pixel blend, 48 ms with the exact table blend
  (`TextCompose::Blend16`, bit-identical, 2.9x), 19 ms on a Pentium preset (real hardware: unverified, `BLEND.EXE`). The composed-syllable cache (`svf_chain_cache`) is sized to about
  200 slots (16x16 1 bpp: 40 B a slot).
- Compiled out: GUI (`DISABLE_GUI`), TTF, detection archives, scalers, cloud; the debug socket runs over a
  16550 UART (`gui/debugsocket-dosuart.cpp`, DOSBox-X nullmodem -> TCP).
- Keyboard: `sdl3-kbd-hangul.patch` + `dos-events.cpp` -> `KEYCODE_HANGUL`/`KEYCODE_HANJA`.
- Memory switches kept from `jamo`: lazy `TrsStore`, `gui_release_buffers`, lazy encoding tables.
- The port is rebased as its own patch series after the Korean core and the SCI games work on Linux
  (section 11, phase P7); it never carries Korean logic of its own (owner directive: keep DOS glue out of the
  engine).

## 9. Upstreamability and repository strategy

What could go upstream (each small, generic, human re-authored): `KEYCODE_HANGUL`/`KEYCODE_HANJA` +
keymapper names; `Common::getLanguageFolderCode()`; the SCI `GfxFont` uint32 interface; DBCS-safe
`parserLowerCaseWord`; the `sci11StringsStart()` heap fix; the KQ1 room 14 `kNodeValue` workaround; lazy
`encoding.dat` tables; `<lang>.trs` naming in SCUMM; the SCUMM v2 Korean sentence line (behind
`isScummvmKorTarget`); the `ko.po` completion. The hi-res text layer, the SVF library and the parser mapping
stay in the fork: they are a new third-party dependency plus cross-engine features upstream has no precedent
for, and the fork's code is agent-written (the owner accepted the AI-GUIDELINES conflict for the fork only).

Submodule vs vendored copy:

| | Submodule compiled in place (`dists/svf-fonts-open/c/src`) | Vendored copy `graphics/svf/` (6 .c + .h + LICENSE + VERSION) |
|---|---|---|
| Build | needs `git submodule update` before `configure`; every CI/packager must know | builds like `graphics/nanosvg`, `graphics/tinygl` (in-tree third-party) |
| Updates | `git submodule update` | `tools/korean/sync-svf.sh` copies from the submodule and records the commit in `graphics/svf/VERSION`; a test compares the two |
| Faces | the submodule stays for faces and tools | same |

RECOMMENDATION: vendor the code in `graphics/svf/`; keep the submodule for faces, tools and the Python
reference used by tests. Build: `graphics/module.mk` adds `svf/svf_face.o svf_jamo.o svf_stream.o svf_draw.o
svf_scale.o`; `configure` appends `-DSVF_JMAP=1 -DSVF_JSET_BIG=1 -DSVF_JMAP_RECOG=0` to `DEFINES` (one place,
every TU) and gains no new option (the library is unconditional, like nanosvg); `LICENSES/COPYING.MIT` already
present, `graphics/svf/LICENSE` = the MIT text; `engines.awk` untouched; no new `--enable-*` except the
existing `--enable-debug-socket` (default off in the fork's configure, on in `engine_gate.sh`).

Fork discipline (cheap rebases): one branch per release, `ko-<release>`, built as an ordered patch series
(`git rebase -i`-free, kept as topic branches merged in order): `ko/00-svf-lib`, `ko/01-core`,
`ko/02-common-korean`, `ko/03-sci`, `ko/04-agi`, `ko/05-scumm`, `ko/06-gui`, `ko/07-debugsocket`,
`ko/08-dos`, `ko/09-data-tools`. Each topic touches disjoint upstream files where possible, carries its tests,
and is rebased independently onto the next release; `docs/korean/PATCHES.md` lists every upstream file each
topic modifies with the reason (the `jamo` lesson: `gfx.cpp` +675 unscoped lines cost the most on every rebase).
Commits may be agent-written with an `Assisted-by:` trailer (owner's rule for the fork).

## 10. Testing and verification

- Unit (CxxTest, `test/module.mk` adds `test/graphics/svf*.h`, `test/graphics/hires_text*.h`,
  `test/common/korean*.h`, `test/engines/{sci,agi,scumm}/*.h`):
  - `svf_face.h`, `svf_chain.h`, `svf_font.h`: open the fixture face (a 3-glyph + tiny JSET v3 face generated
    by `tools/jamoset.py` and stored as a C array in `test/graphics/svf_fixture.h`, no submodule needed at test
    time), `status()`, `has`, widths, `drawString` into CLUT8 and RGB565 surfaces, clipping.
  - Golden pixels: `test/graphics/svf_golden.h` draws fixed lines ("한글 ab", marks, a missing glyph, 2x scale)
    and compares to arrays produced by `tools/svfline.py` from svf-fonts-open (`tools/korean/mkgolden.py`
    regenerates them; the renderer is byte-identical by contract, so this proves the *glue*: baseline, clip,
    plane writes, CLUT8 threshold).
  - Kept from `jamo` and re-pointed: `hires_text_font_map`, `id_plan`, `map_pick`, `text_layout`,
    `unicode_props`, `text_compose`, `banded_plane`, `hires_options`, `hangul`, `korean_stem` (+ corpus),
    `korean_vocab`, `language_folder`, SCI `korean_input`, `script_strings`, SCUMM `text_char_length`,
    `korean_sentence`, AGI `textcells`, `korparse`.
- Engine gates (`tools/korean/engine_gate.sh`): out-of-tree Linux build with `--enable-debug-socket`, `make
  test`, then per-game smoke scripts in the harness (headless Xvfb, debug socket): first room screenshot
  (`dump`), a typed Korean command (`key 0 <cp>`), the expected answer text (`wait text "..."`), save/load
  round trip of a Korean save name. Parser: `k16said.py` numbers must not drop below section 6's baselines.
- RGB composite arithmetic: the hi-res text sinks (`TextCompose::blendPixel`, used by `composeSpan`, `convertSpan` and
  `SvfFont`) blend in the channel widths of the screen format, `(bg * (255 - cov) + fg * cov + 127) / 255` per channel,
  the arithmetic of the C renderer's RGB565/RGB555 targets, so over one background colour the ScummVM composite equals
  the C targets byte for byte (tested against `tools/svfline.py` goldens, for 565, 555, 1555, 5551 and 8-bit formats).
- Coverage: `facecoverage.py --base-check` in every pack build; `hires_text_log` misses must be zero on the
  smoke paths.
- DOS: DOSBox-X headless (`xvfb-run dosbox-x -conf X -nopromptfolder -fastlaunch`, `cputype=pentium`) and
  DOSBox Staging; `harness/dos/play_test.py`, `twozip_test.py`; screenshots of the first screen compared to the
  Linux CLUT8 run (identical for 1-bpp maps). RGB runs: the text composite equals the C renderer's RGB565/RGB555 targets byte for
  byte when the screen format is the same (section 10, RGB composite arithmetic); `auto` gives 888 on a Linux desktop that
  offers it and 565/555 on DOS (VBE), so a DOS RGB frame equals the Linux frame of the same format (`render_target=rgb565`),
  not the 888 one; compared across formats the tolerance is the channel width (1 LSB of the narrower).
- Debug socket: kept as the test oracle (`gui/debugsocket*`, SCI and SCUMM extensions, AGI gets `state` and
  `wait text`), compiled only with `--enable-debug-socket`.

Acceptance per phase is in the table of section 11.

## 11. Phasing

| Phase | Scope | Directories touched | Tests | Done when | Risks | Size |
|---|---|---|---|---|---|---|
| P0 svf library + Font adapter | vendored `graphics/svf`, `SvfFace`, `SvfChain`, `SvfFont`, configure defines, fixture, golden test | graphics/svf, graphics/fonts, graphics/hires_text (new files only), configure, test/graphics | svf_face, svf_chain, svf_font, svf_golden | `make test` green on Linux and mingw; a GUI dialog draws a Korean string with `SvfFont` on a CLUT8 and an RGB565 overlay (manual) | switch mismatch across TUs (guarded by a static_assert on `sizeof(svf_face)` in `svf_face.h`) | ~1.5k lines + tests; reuse: none (new), jamo `glyph_source_file.cpp` read callback idea |
| P1 hires core | map v3 parser (trimmed `font_map`), `id_plan`, `map_pick`, `TextLayout`, `unicode_props`, `TextCompose`, `BandedPlane`, `HiResGlyphSource` over `SvfChain`, `hires_options` with the key table of section 7 | graphics/hires_text, common/language*, test/graphics, test/common | re-pointed jamo tests + new `svf_glyph_source.h` | every kept test passes; `shipped_maps` loads the converted OFL maps | map parser size (cut keys hard) | ~6k reused, ~1k rewritten |
| P2 common Korean | `hangul`, `korean_stem`, `korean_vocab` v3 (u32 offsets, header), `language_folder`, `KEYCODE_HANGUL`, `resolveKoreanParticles` | common, backends/keymapper, test/common | hangul, korean_stem corpus, korean_vocab | tests green; `k6bake.py` in `tools/korean` rebuilds `SCIKOR.DAT` byte-identical to the harness copy for KQ1 | stem rule regressions (corpus guards) | ~2.5k reused |
| P3 SCI KQ1 end to end | `TextMode`, `ScriptStrings` + fingerprint, `readChar`/uint32 `GfxFont`, `TextLayer` + upscaled driver compositing, `GfxCache` face resolver (rewritten, small), text16 UTF-8 layout, edit control (rewritten), composer + surrogate key, `korean_parser` unit, detection `-ko` variants, `kFormat` particles | engines/sci (graphics, engine, parser, event, detection), test/engines/sci | script_strings, korean_input, parser arrangement, i18n gate with real data (skips without `SCUMMVM_TEST_I18N_DATA`) | KQ1 KO pack plays headless: first room screenshot matches reference, `k16said` >= 303/310, save/load with Korean name, EGA and VGA targets both render | `kStrAt` index semantics; driver code drift since `jamo` (upscaled driver exists upstream, verified) | ~5k, of which ~3k reused |
| P4 SCI data games | LB1, Camelot (repackaged from the existing translations into the new format; no CP949 in-place path, no banked fonts), KQ4 (`--early`), QFG1 EGA, QFG2 (LZW1 volumes, Aziza door table), LSL1 EGA/VGA; maps re-paired to OFL faces | engines/sci (per-game tables only), harness packs | k16said per game, smoke per game | each game's `k16said` baseline reached; no engine change except the per-game table | per-game hacks creeping into engine code | ~0.5k engine, data in harness |
| P5 AGI LSL1 | `HiresText` stencils, `agi-ko.str`, `textcells`, `korparse`, `hangulline`, saves v12 | engines/agi, test/engines/agi | textcells, korparse, translation | LSL1 AGI KO plays headless; quiz skip pack works; parser candidates tested | 1-in-4 quiz stall is pre-existing (room 11) | ~3k reused |
| P6 SCUMM | S1: `<lang>.trs`, UTF-8 bundle transcoded to CP949 for upstream `korean.fnt` path, v2 `korean_sentence` (MM/Zak); S2: 2x hi-res via `HiResOverlay` + composite sinks + a slim adapter with one `printChar` seam and one erasure seam (`restoreCharsetBg`/`clearCharsetMask` only; verbs re-drawn by the script each frame are not tracked) for v4-v6 DOS | engines/scumm, test/engines/scumm | text_char_length, korean_sentence, hook census, debugsocket cond | MI1, MI2, MM, Zak, Loom, Indy3/4, DOTT first screens + dialogue verified; palette cycling in MI2 room 33 correct | the erasure seam is the risk; stop S2 and keep S1 if the slim seam fails the census | S1 ~1k, S2 ~4k (2k reused) |
| P7 DOS port | rebase `backends/platform/dos` + SDL3 patches + `--disable-gui` + lazy stores as one series; streamed faces through `SvfFace`; UART debug socket | backends, configure, base, gui (DISABLE_GUI stubs), audio | test/backends dos_* | KQ1, LB1, MI1 KO run in DOSBox-X and Staging from the two-zip layout; Han/Yeong toggle logged | SDL3 patch drift; 16 MB budget re-measured with streamed faces | ~16k reused |
| P8 GUI + Android | `KoreanOptionsWidget` tab, Options > Misc defaults, theme layout bump, `ko.po` completion, Android keyboard layouts | gui, gui/themes, dists/android, po | hirestextoptions | tab edits keys and the game picks them up on next start; Android types Hangul into KQ1 | theme zips must be rebuilt in the same commit | ~1.5k |
| P9 later engines | AGS (EUC-KR `.tra` first), Grim (`.tab`), Nancy/Harvester (new) | per engine | per engine | decided per engine after P6 | - | open |

Reuse from `jamo` per phase is stated in each row; everything named "rewritten" is replaced because it was
either superseded by the C renderer (face reading, composition, chain, cache), over-configurable (face
resolvers, settings objects), or woven into hot upstream paths (SCUMM `gfx.cpp`, SCI `GfxCache`).

## 12. Decisions (resolved by the owner, 2026-11-22)

All twelve open questions of the first draft are answered. The recommended default was taken for every one except
where noted. The numbering is kept so earlier references still work.

| # | Decision |
|---|---|
| 1 | The six C files are **vendored** into `graphics/svf/` with a sync script; the submodule `dists/svf-fonts-open` stays for faces and tools. |
| 2 | TTF stays as a **last-priority** option, compiled only on FreeType builds. |
| 3 | Anything committed to the fork carries **OFL faces only**; the engine fallback face is `NEODG16A.SVF`; existing maps naming HTERM16 / IYAGI16 / MUNJO24 / HWPHG24 / SANGAM16 are **re-paired to OFL faces for public builds**. Private packs keep the owner's look-only rule. |
| 4 | `.str` / `.DAT` get a fingerprint header (format 1; vocab v3); old headerless files load with a warning. |
| 5 | Engine-neutral key names `hangul_input/start/builtin`, `text_encoding`, `hires_text_*` replace `sci_hangul_*` / `agi_hangul_*`. |
| 6 | **SCUMM order: S1 first** (translation through the upstream CP949 path: `<lang>.trs`, UTF-8 bundle transcoded to CP949, v2 `korean_sentence`), **S2 second** (2x hi-res through `HiResOverlay`) behind a **go/no-go gate** on the slim erasure seam, decided after S1 is done. |
| 7 | `k6bake.py` and `strcheck.py` move into ScummVM `tools/korean/`. |
| 8 | The debug socket is in scope as an optional configure feature and the official test oracle. |
| 9 | The DOS port is rebased after P4 as its own series. |
| 10 | **Drop** the legacy `Text.MAP` / `Text.Res` overlay, the "Korean pack played in another language" handling **and the CP949 in-place patch path (B)**: the `detection_tables.h` entries for LB1 and Camelot that expect CP949 patched into the original resources are not carried over. **LB1 and Camelot are repackaged from their existing translations into the new format** (UTF-8 `text.NNN` / `MESSAGE.MAP`+`RESOURCE.MSG` path C, or the `sci-ko.str` path D, with a fingerprint header). **Camelot is conditional**: if it is cut from P4, so is its work (the banked fonts are not needed on the new path in any case). |
| 11 | Engine scope this round: SCI0/1, AGI, SCUMM v2-v6 DOS. AGS / Grim / Nancy / Harvester are deferred. |
| 12 | Upstream: only the candidate list of section 9 is prepared (for human re-authoring); no submission work in this plan. |

Consequence of decision 10: the old CP949 in-place data of LB1 and Camelot is not read by the new tree. Their
existing translations are converted by the harness builders (`build_sci_*.py`) into the new packaging, so the
games run through the same path as every other SCI data game (paths C/D, `ScriptStrings`, fingerprint check).
The repackaging is data work in the harness repository and is a prerequisite of their P4 acceptance.

## 13. Risks

- The C renderer's `SVF_JMAP` switch changes `sizeof(svf_face)`; a TU built with different defines corrupts
  memory silently. Mitigation: configure-level defines, a `static_assert` on the size in `svf_face.h`, and a
  unit test that opens a JMAP face and checks `svf_status == 0`.
- Every shipped face and map from `jamo` is v2; conversion and re-pairing to OFL faces is data work with a
  visible result (looks change). The owner picks faces step by step (per-game font workflow); P1 cannot finish
  for a game before its pairing.
- SCUMM's hi-res erasure seam. `jamo` needed 675 lines in `gfx.cpp`; the slim seam may miss cases (kept verbs,
  room transitions). The hook census test and the S2 go/no-go gate limit the damage.
- `kStrAt`/`kStrLen` code-point semantics on a UTF-8 heap interact with fixed-record scripts (Camelot save
  list) and substring checks (QFG doors); the per-game table in `korean_parser.cpp` must stay small or the
  UTF-8 heap decision is wrong for that game (fallback: CP949 pack for that game).
- Parser numbers are the regression yardstick; a stemmer change for one game can lower another's. The corpus
  test and per-game `k16said` baselines run on every change to `korean_stem`.
- DOS memory: streamed faces fix the font budget, but SDL3 patch drift against a newer SDL and the 16 MB total
  are only measurable on the port itself (P7 re-measures with `mem` over the socket).
- Rebase cost: each upstream release can move `text16.cpp`, `charset.cpp`, the SCI drivers. The topic-branch
  split and `PATCHES.md` make the cost visible; invasive hooks are the first thing to narrow when a rebase
  conflicts.

## Appendix A. `jamo` inventory (Korean-only diff against 96b42d42bc8; top paths by size and importance)

| Path | Lines | Purpose |
|---|---|---|
| engines/scumm/hires_text.cpp / .h | 2890 / 1263 | SCUMM hi-res adapter (config, faces, plans, drawChar) |
| graphics/hires_text/font_map.cpp / .h | 1998 / 506 | `.MAP` parser, qualifiers, path resolution |
| engines/sci/debugsocket.cpp / .h | 1100 / 243 | SCI debug socket extension (`state`, `wait`, `dump`) |
| engines/sci/graphics/cache.cpp (+1002) / cache.h (+292) | | font set and face-chain builder inside `GfxCache` |
| backends/graphics/dos/dos-graphics.cpp | 1302 | DOS VESA graphics backend |
| backends/platform/dos/dos.cpp | 1300 | `OSystem_DOS` |
| backends/timer/dos/dos-timer.cpp | 899 | 1 kHz IRQ0 timer |
| gui/debugsocket.cpp / .h / -protocol / -dosuart | 841 / 281 / 399 / 322 | generic debug socket, framing, UART transport |
| graphics/hires_text/text_layout.cpp / .h | 831 / 213 | UTF-8 decoders, line breaking (Hangul, kinsoku, Thai) |
| graphics/hires_text/glyph_source_ttf.cpp / .h | 824 / 356 | FreeType face as a glyph source |
| common/korean_stem.cpp / .h | 798 / 112 | Korean stemmer (endings, particles, irregulars) |
| graphics/hires_text/bitmap_font.cpp / .h | 756 / 320 | SVFN v1/v2 reader (superseded by svf-fonts-open) |
| engines/scumm/charset.cpp (+753) | | hi-res hooks in every charset renderer, deferred multi-font |
| engines/scumm/HIRES_TEXT.md / _SETUP.md / _DECORATIONS.md | 702 / 609 / 263 | SCUMM design notes |
| engines/scumm/text_utf8.cpp / .h | 680 / 263 | UTF-8 text helpers, bundle transcoding |
| engines/scumm/gfx.cpp (+675) | | composite paths, glyph erasure, transitions |
| graphics/hires_text/hires_options.cpp / .h | 665 / 369 | ini keys, render target resolution |
| graphics/hires_text/unicode_props.cpp | 652 | Unicode width/combining/kinsoku tables |
| graphics/hires_text/README.md | 652 | map and format reference |
| graphics/hires_text/glyph_renderer.cpp / .h | 600 / 314 | outline/drop/stroke decoration (SCUMM) |
| graphics/hires_text/id_plan.cpp / .h | 592 / 239 | per-font-id plan compilation |
| backends/platform/dos/launcher/play.cpp (+ play-*.h) | 574 (+1187) | `PLAY.EXE` launcher |
| engines/scumm/string.cpp (+555) | | bundle probe, UTF-8 drawing, v2 Korean sentence hook |
| backends/midi/dos_mpu401.cpp | 551 | MPU-401 driver |
| engines/sci/graphics/text16.cpp (+491) / .h (+106) | | code-point text loop, UTF-8 layout |
| engines/sci/parser/vocabulary.cpp (+458) / .h (+84) | | Korean lookup, arrangement, fallback tree |
| engines/scumm/trs_store.h | 449 | lazy TRS index + cache (DOS) |
| engines/sci/graphics/controls16.cpp (+449) | | Korean-aware edit control |
| engines/sci/graphics/textlayout16.cpp / .h | 436 / 383 | SCI line layout over `TextLayout` |
| graphics/hires_text/glyph_source_svfn.cpp / .h | 405 / 167 | SVFN glyph source with LRU + preload (superseded) |
| engines/sci/sci.cpp (+391) / sci.h (+138) | | text-mode predicates, ini keys, dump helpers |
| graphics/hires_text/glyph_source_ranged.cpp / .h | 387 / 155 | range routing, `pickGlyph` |
| common/hangul.cpp / .h | 362 / 105 | `HangulComposer`, `resolveKoreanParticles` |
| engines/sci/graphics/hirestextsettings.cpp / .h | 355 / 294 | font settings resolver (to be rewritten) |
| engines/sci/engine/translation.cpp / .h | 306 / 243 | `ScriptStrings` (`sci-ko.str`) |
| engines/sci/graphics/fontset.cpp / .h | 302 / 198 | multi-face SCI font (to be rewritten) |
| common/korean_vocab.cpp / .h | 294 / 161 | `SCIKOR.DAT`/`AGIKOR.DAT` reader |
| engines/scumm/korean_sentence.cpp / .h | 290 / 134 | v2 sentence-line word order |
| engines/agi/textcells.cpp / .h | 282 / 85 | code-point cells, wrap, menus |
| engines/scumm/hires_composite.h / hires_sinks.h / hires_overlay.cpp+.h | 268 / 255 / 424 | composite runs, sinks, overlay planes |
| engines/agi/text.cpp (+250/-175) | | uint32 input/prompt buffers |
| engines/agi/words.cpp (+241) / korparse.cpp / saidscan.cpp | 188 / 71 | Korean candidates for `said()` |
| engines/sci/graphics/koreaninput.cpp / .h | 221 / 182 | `KoreanComposer` (heap encoding) |
| engines/agi/hirestext.cpp / .h, translation.cpp / .h | 211 / 85, 193 / 120 | AGI stencils, `agi-ko.str` |
| engines/sci/graphics/textlayer.cpp / .h | 201 / 116 | 2x {index, coverage} bands |
| engines/sci/graphics/drivers/upscaled.cpp (+166) | | band compositing of the text layer |
| engines/sci/utf8.cpp / .h, textencoding.cpp / .h, dbcs.h | 196 / 138, 95 / 77, 125 | decoding, `text_encoding`, EUC-KR stepping |
| graphics/hires_text/map_pick.cpp, jamo_set.cpp, codepage_kr.cpp | 168, 171, 267 | map order; JSET v1 (superseded); KS X 1001 table |
| dists/engine-data/hires_text/dos/*.MAP, *.HOT; maps/*.map | 14 + 8 + 7 files | per-game maps (v2, non-OFL faces), preload lists |
| tools/korean/*.py, *.md | 10,084 total | extraction, packing, coverage, maps, pipeline docs |
| test/ (143 files) | 39,799 | CxxTest suites listed in section 2.5 |
