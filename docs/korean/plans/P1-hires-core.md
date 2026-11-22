# P1: Hi-res text core — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task by task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** the engine-neutral hi-res text core of DESIGN.md section 11 row P1: a 2x text plane of {palette index, coverage}
kept in bands, its compositing sinks (CLUT8 threshold stamp, RGB blend or threshold through a palette), Hangul text drawn
into the plane through `SvfChain`, the version 3 map (`[fonts]`, `[font]`/`[font.N]`, ranges, qualifiers) compiled per
engine font id, map picking from the game's `ko/` folder, the engine-neutral configuration keys, line breaking
(`TextLayout`, `unicode_props`), and a headless proof: CxxTest suites with golden pixels and a PNG dump. No engine is
touched: the first engine is P3 (SCI), so a throwaway screen harness lives in `test/graphics/`.

**Architecture:** `graphics/hires_text/` grows from P0's face/chain glue into the core. Ported unchanged from branch
`jamo` (read with `git show`, never checked out): `BandedPlane`, `unicode_props`, `unicode_ranges`, `TextLayout` and
their tests. New, small, built on the C renderer: `TextPlane` (two `BandedPlane`s), `TextCompose` sinks, the v3 map
parser (`font_map`), `id_plan`, `hires_options`, `hires_files` (file access, `data:` roots), `map_pick`, and `font_set`
(`HiResFaceCache` opens each face once; `HiResFontSet` holds one `SvfChain` per plan chain and draws a code point at a
time with `SvfChain::drawCoverage` into the plane). Drawing one code point per call is byte-identical to the renderer
drawing the whole line (proved against P0's golden lines, marks, U+FFFD rule and missing advance included). Engines
call `HiResFontSet` for text, `TextPlane` for erasure/invert/underbits, `TextCompose` when presenting a frame.

**Tech stack:** C++11 ScummVM code, the vendored C99 renderer (P0), CxxTest (`make test`).

Source of scope: `docs/korean/DESIGN.md` section 11 row P1 (and rows P2/P3 for what P1 must not do), section 5 (plane
model 5.1, map keys 5.2 item 6), section 7 (keys), section 10 (tests), section 12 decisions 2, 3, 5.

---

## 0. Facts checked for this plan (2026-11-22, this worktree)

Tree `/home/thkim/work/scummvm-2026.4-korean`, branch `ko-2026.4` at `dd5f64b8b34` (P0 and every fix of
`docs/korean/plans/P0-fixes.md` landed; the history was reworded, so P0 hashes in older notes are stale). A fresh
out-of-tree build of that commit (`~/work/tmp/scummvm2026-p1-base`) gives `Running cxxtest tests (437 tests)...OK!`.

**Prototype.** Every new file of this plan was written and run before the plan, outside the worktree, in
`~/work/tmp/scummvm2026-p1-proto/` (its `Makefile` links against the base build): 97 new tests pass, also under
`-fsanitize=address,undefined`. The code blocks below are that prototype (generated from it); where plan text and a code
block disagree, the code block wins. Numbers it proved, used as expected values in the tests:

| Fact | Value |
|---|---|
| `HiResFontSet::drawText` (one `drawCoverage` call per code point) vs the renderer's whole-line golden rows | identical for `Line1x` (mark, `\xff`, missing advance 6), `Font1x` (via a map), `Line2x` (`size=32`), `Line3by2` (`size=24`) |
| RGB565 navy `(0,0,128)` + yellow at coverage 0/85/170/255, read back from the screen | `0x0010`, `0x52AB`, `0xAD45`, `0xFFE0` |
| XRGB8888 from the frame index (navy) + yellow at 0/85/170/255 | `0x000080`, `0x555555`, `0xAAAA2B`, `0xFFFF00` |
| plane memory, 320x200 at 2x, one glyph row | 16 x 320 B (4-bit coverage band); index 0 holds no index band |
| `korean-default.map` + `NEODG16A.SVF` (OFL, 16 408 B), status | 0; "한글" advances 32 px |
| fixture advances: Hangul 16, `i` 8, space 8; `한글 한글 ab` broken at 72 px | `한글 한글` (72) / `ab` (16) |

ScummVM APIs used (file:line in this tree):

| API | Where |
|---|---|
| `Common::Path` `toString` 274, `getParent` 287, `join` 487/494/499, `splitComponents` 626, `Path(const char *, char)` 251 | `common/path.h` |
| `Common::FSNode(const Path &)` 108, `exists` 122, `getChild` 141, `getChildren(FSList &, ListMode, bool)` 149, `getName` 168, `getPath` 204, `isDirectory` 221, `createReadStream` 262 | `common/fs.h` |
| `ConfMan.hasKey(key)` 147, `get(key)` 148, `hasKey(key, dom)` 174, `getPath(key, dom)` 189, `kApplicationDomain` 111, `kTransientDomain` 114, `getActiveDomainName` 208 | `common/config-manager.h` |
| `SearchMan` 498, `Archive::hasFile` 152, `createReadStreamForMember` 189 | `common/archive.h` |
| extrapath is added to `SearchMan` with the default depth (so `hires_text/X.SVF` under it is not found by `SearchMan`: `HiResDiskFiles` checks the extrapaths itself) | `base/main.cpp:280-294` |
| `DATA_PATH` added with depth 4 on SDL | `backends/platform/sdl/sdl.cpp:651-657` |
| `SeekableReadStream` 745, `readLine` 837, `eos` 400, `err` 61 | `common/stream.h` |
| `MemoryReadStream(const byte *, uint32, DisposeAfterUse::Flag = NO)` 73 | `common/memstream.h` |
| `ScopedPtr` 572 | `common/ptr.h` |
| `Common::parseBool` 240 (`true/yes/1`, `false/no/0/""`), `isAlnum` 265, `isDigit` 289, `isXDigit` 301 | `common/util.h` |
| `Common::gcd` 397 | `common/algorithm.h` |
| `String::format` 158, `hasPrefixIgnoreCase` 118, `contains` 122 | `common/str.h` |
| `Array::push_back(const Array &)` 191; `HashMap::contains` 243, `getValOrDefault(key, def)` 252 | `common/array.h`, `common/hashmap.h` |
| `DisposeAfterUse` | `common/types.h:27` |
| `Rect::clip(const Rect &)` 348 | `common/rect.h` |
| `READ_UINT16`/`READ_UINT32` (native endian) 207ff | `common/endian.h` |
| `PixelFormat` `bytesPerPixel` 139, `RGBToColor` 261, `colorToRGB` 295 (expands 5/6-bit channels), `rBits` 332, `isCLUT8` 430 | `graphics/pixelformat.h` |
| `Surface::create` 207 (frees first), `copyFrom` 242, `getBasePtr`, `fillRect`, `free` | `graphics/surface.h`, `surface.cpp` |
| `Image::writePNG(WriteStream &, const Surface &, const byte *palette, uint count)` 94 | `image/png.h` |
| `Common::DumpFile::open(const Path &, bool)` 154 | `common/file.h` |
| `install_null_g_system` 5, `uninstall_null_g_system` 6, `NULL_OSYSTEM_IS_AVAILABLE` 7 | `test/system/null_osystem.h` |
| `Language` enum (no `TH_THA`, no `VI_VNM` in 2026.4) 45-87; `g_languages` 55-95; `parseLanguage` 111; `getLanguageCode` 143 | `common/language.h`, `common/language.cpp` |
| `INIFile` is **not** used: keys like `range.u+ac00-d7a3` hold `+`, refused unless `allowNonEnglishCharacters()` (`isValidChar` 31-39), it keeps `" ; comment"` in values and calls `warning()` itself | `common/formats/ini-file.cpp` |
| `TESTS :=` list 8-17 (explicit `test/graphics/svf_*.h` names, `test/common/*.h` globbed), `copy-dat` puts `encoding.dat` in `test/engine-data/` 103-107, runner includes suites by absolute path (`__FILE__` is absolute) | `test/module.mk`, generated `test/runner.cpp` |
| `MODULE_OBJS` lists objects literally (create_project does not expand variables, P0 fix M-1); `hires_text/svfchain.o`, `hires_text/svfface.o` at 28-29 | `graphics/module.mk` |
| forbidden `getenv` (tests cannot read an environment variable without the exception define, so developer data is found through a directory in the working directory, as P0's `svf-dump/`) | `common/forbidden.h:256-258` |

P0 interfaces P1 relies on (verified unchanged after the P0 fixes; Task 0 re-checks them):

| P0 interface | Used by |
|---|---|
| `SvfFace()`, `load(stream, dispose, name, wholeLimit)`, `loadWhole(stream, name)`, `loadStreamed(stream, dispose, name)`, `getError()`, `getStatus()`, `getCellHeight()`, `getName()`; streamed faces delete their stream with `DisposeAfterUse::YES` also on failure | `HiResFaceCache::get` |
| `SvfChain()`, `addFace(face, NO)` returning bool, `setScale(num, den)` (1..16), `setSyllableCache(slots)`, `has(cp)`, `getWidth(text, len)` (includes the missing advance), `drawCoverage(text, len, x, yBaseline, w, h, PlotFunc, ctx, clip)` (plots only coverage > 0 inside `clip ∩ [0,w)x[0,h)`), `getAscent`, `getDescent`, `getMaxCellWidth`, `getFaceCount`, `getFace(i)`; `PlotFunc = void (*)(void *, int16_t, int16_t, uint8_t)` | `HiResFontSet` |
| `SvfFont::encodeUtf8(cp, char[4])` (static) | `HiResFontSet` |
| tests: `SvfTest::makeChain(num, den, missing, stream, slots)`, `SvfTest::goldenBytes(rows, w, h)` (`svf_test_util.h`, which now uses `TS_ASSERT`: include `<cxxtest/TestSuite.h>` first), `kSvfFixtureMarks`, `kSvfFixtureJamo` (`svf_fixture.h`), `kSvfGolden{Line1x,Line2x,Line3by2,Font1x}{Text,X,Y,W,H,Missing,Width,Ascent,Rows}` (`svf_golden_data.h`) | P1 suites |

P1 does not depend on: `SvfFont::getFontHeight` (P0 fix M-2 changed it), `SvfChain::draw`, `SvfChain::refresh`, the
streamed block-cache internals, or `SvfFont::drawChar`.

## 1. Scope decisions and conflicts with DESIGN.md (listed again in the report)

1. **The plane is engine-neutral and in P1.** DESIGN 5.1 and row P3 put `Sci::TextLayer` (owned by `GfxScreen`) in P3.
   P1 adds `Graphics::TextPlane`, the generalised `jamo` `Sci::TextLayer` (same operations: put text, clear a low-res
   rect, XOR/swap invert, underbits save/restore, row access), stored in two `BandedPlane`s (index unpacked, coverage
   packable to 4 bits). P3's SCI layer owns a `TextPlane` instead of porting `textlayer.cpp`.
2. **No `HiResGlyphSource`.** DESIGN 5.2 item 5 and row P1 name a row-stride glyph interface over `SvfChain` (test
   `svf_glyph_source.h`). The C renderer composes jamo, scales and places marks itself, and one `drawCoverage` call per
   code point equals the whole-line output (prototype, golden rows), so P1 draws through `HiResFontSet` and has no
   per-glyph row interface. TTF (decision 2) gets a chain kind of its own when it is built; in P1 `hires_text_font=ttf`
   is accepted and reported as "not drawn by this build". Test name: `hires_text_font_set.h`.
3. **Rewritten, not trimmed.** Row P1 says "~6k reused, ~1k rewritten" and section 10 "kept and re-pointed" for
   `font_map`, `id_plan`, `map_pick`, `hires_options`, `text_compose`. Measured (prototype, without GPL headers): ported
   verbatim 2,685 lines of code (`banded_plane`, `unicode_props`, `unicode_ranges`, `text_layout`) + 1,383 lines of their
   tests; new 2,245 lines of code + 1,168 of tests (+ about 45 in `common/language.cpp`). `font_map` (h+cpp, jamo 2,504
   lines) becomes 710, `id_plan` 831 -> 160, `hires_options` 1,034 -> 262, `map_pick` 235 -> 139: the dropped keys (`[glyphs]`, `[shadow]`, `pixel=`, `gamma=`, `origin=`, `mirror=`, `[text]`),
   engine key masks, two-phase target loading and built-in TTF faces carried most of their size.
4. **`text_compose` sinks** take index and coverage spans (no `TextPixel` structs, no outline, no `keyed_compose` MMX
   path: that is a DOS/P7 optimisation). Added `convertSpan`/`presentRect`: CLUT8 hi-res frame + plane -> RGB in one pass,
   the under colour exact at 8 bits (what SCI's upscaled driver needs in P3).
5. **Keys from P1** (section 7 subset, decision 5 names): `hires_text`, `hires_text_font`, `hires_text_map`,
   `hires_text_scale`, `hires_text_blend`, `render_target`, `hangul_break`, `hires_text_log` (parsed; nothing logs yet).
   Not in P1: `text_encoding` (P3 `TextMode`), `hangul_input`, `hangul_start`, `hangul_builtin` (P2/P3), `korean_text`
   (section 7's global off switch; P3, where `TextMode` is decided).
6. **Map v3 keys.** As DESIGN 5.2 item 6, plus `advance=` (id-wide, besides `advance.<spec>=`) and `[render] target=`.
   `size=` on SVF faces means "the chain's tallest cell drawn at this many hi-res pixels" (scale `size/cell`, reduced,
   each 1..16; DESIGN does not define it for SVF). Qualifier merge order, least to most specific: `[S]`, `[S:clut8|rgb]`,
   `[S:sci|scumm|agi]`, `[S:engine:target]` (jamo's order). `[preload]` is parsed and kept; the warm-up of the block cache
   is a DOS concern and is left to P7. A version 2 map is refused with one warning naming `[map] version=3` and the keys
   to delete.
7. **`shipped_maps` done-criterion.** DESIGN section 4 says nothing per game goes into engine data, and decision 3 plus
   section 13 make the OFL re-pairing of each game's map the owner's data work. P1 therefore ships one map,
   `dists/engine-data/hires_text/maps/korean-default.map` (the engine fallback face `NEODG16A.SVF`, decision 3), and the
   face file itself (copied by `sync-svf.sh`; installing it via `engine_data.mk` waits for P3, the first user). The test
   loads every shipped map clean for every qualifier set and, when a directory `hires-maps/` exists in the working
   directory, every map in it (the harness's converted pack maps).
8. **`common/language*`.** Row P1 lists the directory, row P2 lists `language_folder`. P1 adds only
   `getLanguageFolderCode()`/`parseLanguageFolderCode()` (the `ko/` lookup of map picking needs it); the detection helper
   `translationFolderLanguages()` (`language_folder.cpp`) stays in P2. `jamo`'s table is taken without `TH_THA`/`VI_VNM`
   (not in the 2026.4 enum); `VI_VNM` is not added.
9. **`data:` roots** are the three extrapaths, `DATA_PATH`, roots added by the caller, then `SearchMan`; `jamo`'s in-tree
   default and `searchSetRoots()` are dropped. A `data:` path with `..` or an absolute part is refused.
10. **Plane write rule:** over text of the same index the larger coverage stays (the renderer's GRAY8 rule, so a one-colour
    line in the plane equals the golden rows); over another index the new pixel replaces the old.
11. Branch: commit on `ko-2026.4` directly, as P0 did (P0-notes item 1), unless the owner names a topic branch
    (`ko/01-core`, DESIGN section 9).

## 2. Files

| File | New/Mod | Purpose |
|---|---|---|
| `common/language.h`, `common/language.cpp` | mod | `getLanguageFolderCode`, `parseLanguageFolderCode` |
| `test/common/language_folder_code.h` | new | 3 tests (globbed by `test/common/*.h`) |
| `graphics/hires_text/banded_plane.{h,cpp}` | port | `BandedPlane` |
| `graphics/hires_text/unicode_props.{h,cpp}`, `unicode_ranges.{h,cpp}` | port | character properties, range specs and tables |
| `graphics/hires_text/text_layout.{h,cpp}` | port | `TextRun`, decoders, `TextLayout::breakLines` |
| `graphics/hires_text/text_plane.{h,cpp}` | new | `TextPlane` |
| `graphics/hires_text/text_compose.{h,cpp}` | new | sinks |
| `graphics/hires_text/font_map.{h,cpp}` | new | v3 map model and parser |
| `graphics/hires_text/id_plan.{h,cpp}` | new | per font id plan |
| `graphics/hires_text/hires_options.{h,cpp}` | new | keys, target/blend/scale resolution |
| `graphics/hires_text/hires_files.{h,cpp}` | new | `HiResFiles`, `HiResDiskFiles`, `findGameMapFile` |
| `graphics/hires_text/map_pick.{h,cpp}` | new | `pickHiResMap` |
| `graphics/hires_text/font_set.{h,cpp}` | new | `HiResFaceCache`, `HiResFontSet`, `HiResOriginalFont`, `HiResFontMetrics`, `decodeUtf8` |
| `graphics/module.mk` | mod | 12 objects |
| `test/module.mk` | mod | one glob line `test/graphics/hires_text_*.h` |
| `test/graphics/hires_text_*.h` | new/port | 13 suites |
| `test/graphics/hires_support.h`, `hires_screen.h` | new | memory files, coverage helper; the throwaway screen harness |
| `tools/korean/sync-svf.sh` | mod (fork file) | also copies `NEODG16A.SVF` + `NEODGM.TXT` into engine data |
| `dists/engine-data/hires_text/NEODG16A.SVF`, `NEODGM.TXT` | new (copied) | engine fallback face, OFL |
| `dists/engine-data/hires_text/maps/korean-default.map` | new | the one shipped v3 map |
| `LICENSES/COPYING.OFL` | mod | list the shipped face |
| `docs/korean/PATCHES.md` | mod | rows for `ko/01-core` |
| `docs/korean/plans/P1-notes.md` | new (last task) | deviations and acceptance run |

Style for every new file: the GPL header of `graphics/font.cpp:1-20` verbatim (code blocks below omit it), tabs,
`namespace Graphics {` ... `} // End of namespace Graphics`, `_camelCase` members, short comments (AI-GUIDELINES). Ported
files keep `jamo`'s bytes except the edits named in their task, so a reviewer can `diff` them against `git show`.

## 3. Environment (every task)

```sh
SRC=/home/thkim/work/scummvm-2026.4-korean
OLD=/home/thkim/work/scummvm-korean           # read only: git -C $OLD show jamo:<path>; never check out
BLD=~/work/tmp/scummvm2026-p1build            # out of tree; never configure inside $SRC
mkdir -p $BLD && cd $BLD && $SRC/configure --disable-all-engines --enable-debug --disable-optimizations >/dev/null
~/.local/bin/heavy make -j16 test             # now: "Running cxxtest tests (437 tests)...OK!"
./test/runner <SuiteName>                     # one suite, e.g. ./test/runner HiResTextTextPlaneTestSuite
```

`heavy` is `~/.local/bin/heavy` (the host's OOM guard); a full `make test` takes about 7 s wall. Re-run `configure` only
if `configure` changes (it does not in P1); new `.cpp` files are picked up from `graphics/module.mk`.

Commit after each task (`git -C $SRC add <files> && git -C $SRC commit`), message: subject `AREA: Subject` (capitals,
`devtools/check-commit-msg.py` rule), a short body, and as the last line exactly:

```
Assisted-by: Claude Code
```

---

## Task 0: Preconditions (5 min, no commit)

- [ ] `git -C $SRC log --oneline -1` is `dd5f64b8b34` or a descendant; `git -C $SRC status --short` is empty.
- [ ] P0 interfaces unchanged (each command prints one line; if one prints nothing, compare the header with section 0 and
  adapt the calls in `font_set.cpp` before Task 14):

```sh
cd $SRC
grep -n 'bool load(Common::SeekableReadStream \*stream, DisposeAfterUse::Flag dispose, const Common::String &name' graphics/hires_text/svfface.h
grep -n 'bool loadWhole(Common::SeekableReadStream &stream' graphics/hires_text/svfface.h
grep -n 'bool loadStreamed(Common::SeekableReadStream \*stream' graphics/hires_text/svfface.h
grep -n 'typedef void (\*PlotFunc)(void \*ctx, int16_t x, int16_t y, uint8_t coverage);' graphics/hires_text/svfchain.h
grep -n 'svf_err drawCoverage(const char \*text, uint32 len, int x, int yBaseline, int w, int h, PlotFunc plot' graphics/hires_text/svfchain.h
grep -n 'int getWidth(const char \*text, uint32 len) const;' graphics/hires_text/svfchain.h
grep -n 'static uint encodeUtf8(uint32 cp, char out\[4\]);' graphics/fonts/svffont.h
grep -n 'inline Graphics::SvfChain \*makeChain(uint num = 1, uint den = 1, uint8 missing = 0' test/graphics/svf_test_util.h
```

- [ ] Baseline: run the Section 3 block; expect `437 tests ... OK!`. If the count differs, write it down as N0: every
  "expected count" below is N0 + the tests added so far (437 is assumed).

## Task 1: Language folder codes (10 min)

- [ ] **Step 1: test** — `test/common/language_folder_code.h` (GPL header not used in `test/common`; follow `test/common/array.h`):

```cpp
#include <cxxtest/TestSuite.h>

#include "common/language.h"
#include "common/str.h"

class LanguageFolderCodeTestSuite : public CxxTest::TestSuite {
public:
	void test_folderCode() {
		TS_ASSERT_EQUALS(Common::String(Common::getLanguageFolderCode(Common::KO_KOR)), "ko");
		TS_ASSERT_EQUALS(Common::String(Common::getLanguageFolderCode(Common::ZH_TWN)), "zh");
		TS_ASSERT_EQUALS(Common::String(Common::getLanguageFolderCode(Common::PT_BRA)), "pt");
		TS_ASSERT_EQUALS(Common::String(Common::getLanguageFolderCode(Common::UNK_LANG)), "");
	}

	void test_parse() {
		TS_ASSERT_EQUALS(Common::parseLanguageFolderCode("KO"), Common::KO_KOR);
		TS_ASSERT_EQUALS(Common::parseLanguageFolderCode("en"), Common::EN_ANY);
		TS_ASSERT_EQUALS(Common::parseLanguageFolderCode("zh"), Common::ZH_ANY);
		TS_ASSERT_EQUALS(Common::parseLanguageFolderCode("kr"), Common::UNK_LANG);
		TS_ASSERT_EQUALS(Common::parseLanguageFolderCode(""), Common::UNK_LANG);
	}

	// Every language a game can be set to has a folder code.
	void test_everyLanguage() {
		for (int l = 0; Common::getLanguageCode((Common::Language)l); ++l)
			TSM_ASSERT(Common::getLanguageCode((Common::Language)l), *Common::getLanguageFolderCode((Common::Language)l));
	}
};
```

- [ ] **Step 2: run, expect a compile failure** (`getLanguageFolderCode` not declared): `cd $BLD && heavy make -j16 test`.
- [ ] **Step 3: implement** — in `common/language.h`, after `getLanguageDescription` (line 105):

```cpp
/**
 * The ISO 639-1 code of a language: the name of the folder a translation keeps its files in (ko/, ja/). Unlike
 * getLanguageCode() ("cn", "tw", "gb", "br"), every variant has its language's code: ZH_TWN is "zh", PT_BRA "pt".
 * Empty for UNK_LANG.
 */
extern const char *getLanguageFolderCode(Language id);

/** The language a folder code names (any case): the generic one of those sharing it ("en" EN_ANY, "zh" ZH_ANY). */
extern Language parseLanguageFolderCode(const String &code);
```

In `common/language.cpp`, after `getLanguageCode()` (ends line 150):

```cpp
// The ISO 639-1 code of each language (getLanguageFolderCode()). Languages that share a code are listed
// generic first: that is the one parseLanguageFolderCode() gives for it.
static const struct {
	Language id;
	const char *code;
} g_languageFolderCodes[] = {
	{ AR_ARB, "ar" }, { BG_BUL, "bg" }, { CA_ESP, "ca" }, { CS_CZE, "cs" }, { DA_DNK, "da" }, { DE_DEU, "de" },
	{ EL_GRC, "el" }, { EN_ANY, "en" }, { EN_GRB, "en" }, { EN_USA, "en" }, { ES_ESP, "es" }, { EU_ESP, "eu" },
	{ ET_EST, "et" }, { FA_IRN, "fa" }, { FI_FIN, "fi" }, { FR_FRA, "fr" }, { FR_CAN, "fr" }, { HE_ISR, "he" },
	{ HR_HRV, "hr" }, { HU_HUN, "hu" }, { IT_ITA, "it" }, { JA_JPN, "ja" }, { KO_KOR, "ko" }, { LT_LTU, "lt" },
	{ LV_LVA, "lv" }, { NL_NLD, "nl" }, { NL_BEL, "nl" }, { NB_NOR, "nb" }, { PL_POL, "pl" }, { PT_PRT, "pt" },
	{ PT_BRA, "pt" }, { RU_RUS, "ru" }, { SV_SWE, "sv" }, { SK_SVK, "sk" }, { SR_SRB, "sr" }, { TR_TUR, "tr" },
	{ UA_UKR, "uk" }, { ZH_ANY, "zh" }, { ZH_CHN, "zh" }, { ZH_TWN, "zh" },
	{ UNK_LANG, nullptr }
};

const char *getLanguageFolderCode(Language id) {
	for (uint i = 0; g_languageFolderCodes[i].code; ++i)
		if (g_languageFolderCodes[i].id == id)
			return g_languageFolderCodes[i].code;
	return "";
}

Language parseLanguageFolderCode(const String &code) {
	for (uint i = 0; !code.empty() && g_languageFolderCodes[i].code; ++i)
		if (code.equalsIgnoreCase(g_languageFolderCodes[i].code))
			return g_languageFolderCodes[i].id;
	return UNK_LANG;
}
```

(no forward declarations in the .cpp: the header has them.)

- [ ] **Step 4: run** — `heavy make -j16 test` → `440 tests ... OK!`.
- [ ] **Step 5: commit**

```
COMMON: Add the ISO 639-1 folder code of a language

Translations keep their files in a folder named by the language's
two-letter code (ko/). getLanguageCode() gives configuration codes
such as "tw" and "br", so add a separate table with one code per
language.

Assisted-by: Claude Code
```

## Task 2: Port `BandedPlane` (10 min)

- [ ] **Step 1: copy the test, add the suite glob**

```sh
cd $SRC
git -C $OLD show jamo:test/graphics/hires_text_banded_plane.h > test/graphics/hires_text_banded_plane.h
```

In `test/module.mk`, after the line `$(srcdir)/test/graphics/svf_font.h` (17) append ` \` to it and add:

```make
	$(srcdir)/test/graphics/hires_text_*.h
```

(helpers are named `hires_support.h`, `hires_screen.h` so the glob does not make suites of them.)

- [ ] **Step 2: run, expect a compile failure** (`graphics/hires_text/banded_plane.h` missing).
- [ ] **Step 3: copy the code and build it**

```sh
for f in banded_plane.h banded_plane.cpp; do git -C $OLD show jamo:graphics/hires_text/$f > graphics/hires_text/$f; done
```

In `graphics/module.mk`, insert before `hires_text/svfchain.o \` (line 28): `	hires_text/banded_plane.o \`.

- [ ] **Step 4: run** — `heavy make -j16 test` → `448 tests ... OK!`;
  `git -C $OLD show jamo:graphics/hires_text/banded_plane.cpp | diff - graphics/hires_text/banded_plane.cpp` is empty.
- [ ] **Step 5: commit**

```
GRAPHICS: Hi-res text: Add BandedPlane

A plane of one byte a pixel kept in 16-row bands that exist only while
something non-zero is in them, packed to four bits a pixel while every
value is a multiple of 17. Taken unchanged from the jamo branch with
its tests.

Assisted-by: Claude Code
```

## Task 3: Port `unicode_props` and `unicode_ranges` (10 min)

- [ ] **Step 1: tests**

```sh
cd $SRC
git -C $OLD show jamo:test/graphics/hires_text_unicode_ranges.h > test/graphics/hires_text_unicode_ranges.h
# Keep lines 1-260: lines 262-290 are a suite for TH_THA/VI_VNM, languages 2026.4 does not have.
git -C $OLD show jamo:test/graphics/hires_text_unicode_props.h | head -n 260 > test/graphics/hires_text_unicode_props.h
tail -n 1 test/graphics/hires_text_unicode_props.h      # "};"
```

- [ ] **Step 2: run, expect a compile failure.**
- [ ] **Step 3: code**

```sh
for f in unicode_props.h unicode_props.cpp unicode_ranges.h unicode_ranges.cpp; do
	git -C $OLD show jamo:graphics/hires_text/$f > graphics/hires_text/$f; done
```

`graphics/module.mk`: add `	hires_text/unicode_props.o \` and `	hires_text/unicode_ranges.o \` after
`hires_text/svfface.o \` (keep alphabetical order).

- [ ] **Step 4: run** → `462 tests ... OK!`.
- [ ] **Step 5: commit**

```
GRAPHICS: Hi-res text: Add Unicode properties and range specs

Width, combining, space, kinsoku and Thai properties by code point,
and the block names and U+ spans of the map's range keys compiled into
a lookup table. Taken unchanged from the jamo branch; its test suite
for Thai and Vietnamese language entries is left out (2026.4 has
neither).

Assisted-by: Claude Code
```

## Task 4: Port `TextLayout` (10 min)

- [ ] **Step 1: test**

```sh
cd $SRC
git -C $OLD show jamo:test/graphics/hires_text_text_layout.h |
	sed 's|kFallbackDir = "dists/engine-data"|kFallbackDir = "test/engine-data"|' > test/graphics/hires_text_text_layout.h
grep -c 'test/engine-data' test/graphics/hires_text_text_layout.h      # 1
```

(the runner runs in `$BLD`, where `copy-dat` put `encoding.dat` in `test/engine-data/`, `test/module.mk:103-107`; with
`dists/engine-data` the CP949 test skips.)

- [ ] **Step 2: run, expect a compile failure.**
- [ ] **Step 3: code** — `git -C $OLD show jamo:graphics/hires_text/text_layout.{h,cpp}` into `graphics/hires_text/`;
  `graphics/module.mk` gains `	hires_text/text_layout.o \` (after `svfface.o`, before `unicode_props.o`).
- [ ] **Step 4: run** → `487 tests ... OK!`, and no `Test skipping` line for `test_code_page_decoder_cp949`
  (`./test/runner HiResTextLayoutTestSuite 2>&1 | grep -c skipping` → 0).
- [ ] **Step 5: commit**

```
GRAPHICS: Hi-res text: Add TextLayout

Text decoded once into units (UTF-8 or a legacy code page) and broken
into lines by character class: Hangul at spaces or anywhere, kinsoku,
a Thai fallback. Taken from the jamo branch; the CP949 test looks for
encoding.dat where the test runner copies it.

Assisted-by: Claude Code
```

### Review checkpoint A (after Task 4)

superpowers:requesting-code-review, scope: Tasks 1-4. Check: every ported file is byte-identical to `jamo`
(`for f in banded_plane unicode_props unicode_ranges text_layout; do for e in h cpp; do git -C $OLD show
jamo:graphics/hires_text/$f.$e | cmp - graphics/hires_text/$f.$e; done; done` prints nothing), the two test edits are
the only test differences, the language table covers every enum value (`test_everyLanguage`), `PATCHES.md` is updated in
Task 17 (note it).

## Task 5: `TextPlane` (20 min)

- [ ] **Step 1: test** — `test/graphics/hires_text_text_plane.h`:

```cpp
#include <cxxtest/TestSuite.h>

#include "graphics/hires_text/text_plane.h"

class HiResTextTextPlaneTestSuite : public CxxTest::TestSuite {
public:
	void test_createAndPlot() {
		Graphics::TextPlane p;
		TS_ASSERT(!p.create(320, 200, 0));
		TS_ASSERT(p.create(320, 200, 2));
		TS_ASSERT_EQUALS(p.getWidth(), 640);
		TS_ASSERT_EQUALS(p.getHeight(), 400);
		TS_ASSERT(p.isEmpty());
		TS_ASSERT_EQUALS(p.getBytesHeld(), 0u);
		p.plot(10, 20, 255, 0);	// index 0 is a colour like any other
		p.plot(-1, 0, 255, 3);
		p.plot(640, 0, 255, 3);
		TS_ASSERT(!p.isEmpty());
		TS_ASSERT(p.rowHasText(20));
		TS_ASSERT(!p.rowHasText(40));
		TS_ASSERT_EQUALS(p.getCoverage(10, 20), 255);
		TS_ASSERT_EQUALS(p.getIndex(10, 20), 0);
		// One 16-row band of 640 4-bit coverage pixels; index 0 holds no index band.
		TS_ASSERT_EQUALS(p.getBytesHeld(), 16u * 320);
	}

	void test_plotRule() {
		Graphics::TextPlane p;
		p.create(8, 8, 1);
		p.plot(1, 1, 170, 5);
		p.plot(1, 1, 85, 5);	// same index: the larger coverage stays
		TS_ASSERT_EQUALS(p.getCoverage(1, 1), 170);
		p.plot(1, 1, 255, 5);
		TS_ASSERT_EQUALS(p.getCoverage(1, 1), 255);
		p.plot(1, 1, 85, 9);	// another index replaces it
		TS_ASSERT_EQUALS(p.getCoverage(1, 1), 85);
		TS_ASSERT_EQUALS(p.getIndex(1, 1), 9);
		p.plot(1, 1, 0, 2);		// coverage 0 is no text
		TS_ASSERT_EQUALS(p.getIndex(1, 1), 9);
	}

	void test_clearAndBands() {
		Graphics::TextPlane p;
		p.create(320, 200, 2);
		p.plot(4, 4, 255, 15);
		p.plot(4, 100, 255, 15);
		TS_ASSERT_EQUALS(p.getBytesHeld(), 2u * (16 * 320 + 16 * 640));
		p.clearLowresRect(Common::Rect(0, 0, 10, 10));
		TS_ASSERT_EQUALS(p.getCoverage(4, 4), 0);
		TS_ASSERT_EQUALS(p.getCoverage(4, 100), 255);
		TS_ASSERT_EQUALS(p.getBytesHeld(), 16u * 320 + 16 * 640);
		p.clear();
		TS_ASSERT(p.isEmpty());
		TS_ASSERT_EQUALS(p.getBytesHeld(), 0u);
	}

	void test_eightBitCoverageUnpacks() {
		Graphics::TextPlane p;
		p.create(16, 16, 1);
		p.plot(0, 0, 170, 1);
		p.plot(1, 0, 100, 1);	// not a multiple of 17
		TS_ASSERT_EQUALS(p.getCoverage(0, 0), 170);
		TS_ASSERT_EQUALS(p.getCoverage(1, 0), 100);
	}

	void test_invert() {
		Graphics::TextPlane p;
		p.create(10, 10, 2);
		p.plot(2, 2, 255, 0x03);
		p.plot(3, 2, 255, 0x0C);
		p.xorIndicesLowresRect(Common::Rect(0, 0, 2, 2), 0x0F);
		TS_ASSERT_EQUALS(p.getIndex(2, 2), 0x0C);
		TS_ASSERT_EQUALS(p.getIndex(3, 2), 0x03);
		TS_ASSERT_EQUALS(p.getIndex(5, 5), 0);	// no text: untouched
		p.xorIndicesLowresRect(Common::Rect(0, 0, 2, 2), 0x0F);
		TS_ASSERT_EQUALS(p.getIndex(2, 2), 0x03);
		p.swapIndicesLowresRect(Common::Rect(0, 0, 2, 2), 0x03, 0x0C);
		TS_ASSERT_EQUALS(p.getIndex(2, 2), 0x0C);
		TS_ASSERT_EQUALS(p.getIndex(3, 2), 0x03);
	}

	void test_saveRestore() {
		Graphics::TextPlane p;
		p.create(20, 20, 2);
		const Common::Rect box(1, 1, 5, 4);
		TS_ASSERT_EQUALS(p.saveSize(box), 1u);
		p.plot(3, 3, 170, 4);
		p.plot(9, 7, 255, 6);
		TS_ASSERT_EQUALS(p.saveSize(box), 1u + 2 * 8 * 6);
		byte buf[1 + 2 * 8 * 6];
		byte *out = buf;
		p.save(box, out);
		TS_ASSERT_EQUALS((uint32)(out - buf), p.saveSize(box));
		p.clearLowresRect(box);
		TS_ASSERT_EQUALS(p.getCoverage(3, 3), 0);
		const byte *in = buf;
		p.restore(box, in);
		TS_ASSERT_EQUALS(p.getCoverage(3, 3), 170);
		TS_ASSERT_EQUALS(p.getIndex(9, 7), 6);
		const byte empty = 0;
		in = &empty;
		p.restore(box, in);
		TS_ASSERT(p.isEmpty());
	}

	void test_getRow() {
		Graphics::TextPlane p;
		p.create(8, 8, 1);
		byte is[8], cs[8];
		const byte *idx, *cov;
		TS_ASSERT(!p.getRow(3, 0, 8, is, cs, idx, cov));
		p.plot(5, 3, 85, 2);
		TS_ASSERT(p.getRow(3, 4, 3, is, cs, idx, cov));
		TS_ASSERT_EQUALS(cov[0], 0);
		TS_ASSERT_EQUALS(cov[1], 85);
		TS_ASSERT_EQUALS(idx[1], 2);
	}
};
```

- [ ] **Step 2: run, expect a compile failure** (`text_plane.h` missing).
- [ ] **Step 3: implement** — `graphics/hires_text/text_plane.h`:

```cpp
#ifndef GRAPHICS_HIRES_TEXT_TEXT_PLANE_H
#define GRAPHICS_HIRES_TEXT_TEXT_PLANE_H

#include "common/noncopyable.h"
#include "common/rect.h"
#include "common/scummsys.h"

#include "graphics/hires_text/banded_plane.h"

namespace Graphics {

/**
 * Hi-res text over a low-res paletted game screen: for each pixel of the
 * screen at getScale() times its size, a palette index and a coverage
 * (0 = no text). Colours are resolved when the plane is composited, so a
 * palette change recolours text drawn earlier. Both planes are banded
 * (BandedPlane): only the rows text covers hold memory, and the coverage
 * plane keeps 1 and 2 bpp coverage at 4 bits a pixel.
 *
 * Rects named "lowres" are in game pixels; every other coordinate is a
 * hi-res pixel. Nothing here knows an engine.
 */
class TextPlane : Common::NonCopyable {
public:
	TextPlane() : _scale(0) {}

	/** A plane for a @p lowW x @p lowH screen drawn at @p scale (1..4), without text. */
	bool create(int lowW, int lowH, int scale);
	void free();
	bool exists() const { return _coverage.exists(); }

	int getWidth() const { return _coverage.width(); }
	int getHeight() const { return _coverage.height(); }
	int getScale() const { return _scale; }

	/** No pixel holds text. */
	bool isEmpty() const { return _coverage.bandsHeld() == 0; }
	/** Whether row @p y may hold text (false: it holds none). */
	bool rowHasText(int y) const { return _coverage.rowHeld(y); }

	byte getIndex(int x, int y) const { return _index.get(x, y); }
	byte getCoverage(int x, int y) const { return _coverage.get(x, y); }

	/**
	 * Text of colour @p index and coverage @p coverage at (@p x, @p y),
	 * ignored outside the plane or at coverage 0. Over text of the same
	 * index the larger coverage stays (two cells of one line, the GRAY8
	 * rule of the renderer); over another index the new text replaces it.
	 */
	void plot(int x, int y, byte coverage, byte index);

	void clear();
	void clearRect(const Common::Rect &hires);
	void clearLowresRect(const Common::Rect &lowres) { clearRect(toHires(lowres)); }
	/** An XOR invert: the index of every pixel with text in the rect is XORed with @p mask. */
	void xorIndicesLowresRect(const Common::Rect &lowres, byte mask);
	/** Inside the rect, text of index @p a becomes @p b and the other way round. */
	void swapIndicesLowresRect(const Common::Rect &lowres, byte a, byte b);

	/** The bytes save() writes for @p lowres: 1 when it holds no text. */
	uint32 saveSize(const Common::Rect &lowres) const;
	/** A flag byte, then (flag 1) each hi-res row's indices and coverages. */
	void save(const Common::Rect &lowres, byte *&out) const;
	/** Puts back what save() wrote for the same rect (flag 0 clears it). */
	void restore(const Common::Rect &lowres, const byte *&in);

	/**
	 * Pixels [@p x0, @p x0 + @p n) of row @p y: false when the row holds no
	 * text. Otherwise @p index and @p coverage point at the pixels, in the
	 * plane or in the scratch buffers (room for @p n bytes each).
	 */
	bool getRow(int y, int x0, int n, byte *indexScratch, byte *coverageScratch,
	            const byte *&index, const byte *&coverage) const;

	/** @p lowres in hi-res pixels, clipped to the plane. */
	Common::Rect toHires(const Common::Rect &lowres) const;
	uint32 getBytesHeld() const { return _index.bytesHeld() + _coverage.bytesHeld(); }

private:
	bool covers(const Common::Rect &r) const;

	int _scale;
	BandedPlane _index;
	BandedPlane _coverage;
};

} // End of namespace Graphics

#endif
```

`graphics/hires_text/text_plane.cpp`:

```cpp
#include "graphics/hires_text/text_plane.h"

#include "common/util.h"

namespace Graphics {

bool TextPlane::create(int lowW, int lowH, int scale) {
	free();
	if (lowW <= 0 || lowH <= 0 || scale < 1 || scale > 4)
		return false;
	_scale = scale;
	_index.create(lowW * scale, lowH * scale, false);
	_coverage.create(lowW * scale, lowH * scale, true);
	return true;
}

void TextPlane::free() {
	_index.free();
	_coverage.free();
	_scale = 0;
}

void TextPlane::plot(int x, int y, byte coverage, byte index) {
	if (!coverage || x < 0 || y < 0 || x >= getWidth() || y >= getHeight())
		return;
	const byte old = _coverage.get(x, y);
	if (old && _index.get(x, y) == index && old > coverage)
		return;
	_index.set(x, y, index);
	_coverage.set(x, y, coverage);
}

void TextPlane::clear() {
	_index.clear();
	_coverage.clear();
}

void TextPlane::clearRect(const Common::Rect &hires) {
	_index.fill(hires, 0);
	_coverage.fill(hires, 0);
}

Common::Rect TextPlane::toHires(const Common::Rect &lowres) const {
	Common::Rect r(lowres.left * _scale, lowres.top * _scale, lowres.right * _scale, lowres.bottom * _scale);
	r.clip(Common::Rect(getWidth(), getHeight()));
	return r;
}

void TextPlane::xorIndicesLowresRect(const Common::Rect &lowres, byte mask) {
	const Common::Rect r = toHires(lowres);
	if (!mask || r.isEmpty())
		return;
	for (int y = r.top; y < r.bottom; ++y) {
		if (!rowHasText(y))
			continue;
		for (int x = r.left; x < r.right; ++x)
			if (_coverage.get(x, y))
				_index.set(x, y, _index.get(x, y) ^ mask);
	}
}

void TextPlane::swapIndicesLowresRect(const Common::Rect &lowres, byte a, byte b) {
	const Common::Rect r = toHires(lowres);
	if (a == b || r.isEmpty())
		return;
	for (int y = r.top; y < r.bottom; ++y) {
		if (!rowHasText(y))
			continue;
		for (int x = r.left; x < r.right; ++x) {
			if (!_coverage.get(x, y))
				continue;
			const byte i = _index.get(x, y);
			if (i == a)
				_index.set(x, y, b);
			else if (i == b)
				_index.set(x, y, a);
		}
	}
}

bool TextPlane::covers(const Common::Rect &r) const {
	for (int y = r.top; y < r.bottom; ++y) {
		if (!rowHasText(y))
			continue;
		for (int x = r.left; x < r.right; ++x)
			if (_coverage.get(x, y))
				return true;
	}
	return false;
}

uint32 TextPlane::saveSize(const Common::Rect &lowres) const {
	const Common::Rect r = toHires(lowres);
	return covers(r) ? 1 + 2 * (uint32)r.width() * r.height() : 1;
}

void TextPlane::save(const Common::Rect &lowres, byte *&out) const {
	const Common::Rect r = toHires(lowres);
	const bool any = covers(r);
	*out++ = any ? 1 : 0;
	if (!any)
		return;
	for (int y = r.top; y < r.bottom; ++y) {
		_index.readBytes((uint32)y * getWidth() + r.left, out, r.width());
		out += r.width();
		_coverage.readBytes((uint32)y * getWidth() + r.left, out, r.width());
		out += r.width();
	}
}

void TextPlane::restore(const Common::Rect &lowres, const byte *&in) {
	const Common::Rect r = toHires(lowres);
	if (!*in++) {
		clearRect(r);
		return;
	}
	for (int y = r.top; y < r.bottom; ++y) {
		_index.writeBytes((uint32)y * getWidth() + r.left, in, r.width());
		in += r.width();
		_coverage.writeBytes((uint32)y * getWidth() + r.left, in, r.width());
		in += r.width();
	}
}

bool TextPlane::getRow(int y, int x0, int n, byte *indexScratch, byte *coverageScratch,
                       const byte *&index, const byte *&coverage) const {
	if (y < 0 || y >= getHeight() || !rowHasText(y))
		return false;
	coverage = _coverage.row(y, coverageScratch, x0, n);
	index = _index.row(y, indexScratch, x0, n);
	if (!index) {
		memset(indexScratch, 0, n);
		index = indexScratch;
	}
	return coverage != nullptr;
}

} // End of namespace Graphics
```

`graphics/module.mk`: `	hires_text/text_plane.o \` (alphabetical, after `text_layout.o`).

- [ ] **Step 4: run** → `494 tests ... OK!`.
- [ ] **Step 5: commit**

```
GRAPHICS: Hi-res text: Add TextPlane

Hi-res text over a low-res paletted screen as a palette index and a
coverage per pixel, each in a BandedPlane, so a palette change
recolours text drawn earlier and only rows with text hold memory.
Erasing, inverting and saving a rect work on low-res rects, the way an
engine's own screen operations do.

Assisted-by: Claude Code
```

## Task 6: `TextCompose` sinks (15 min)

- [ ] **Step 1: test** — `test/graphics/hires_text_text_compose.h`:

```cpp
#include <cxxtest/TestSuite.h>

#include "graphics/hires_text/text_compose.h"
#include "graphics/hires_text/text_plane.h"
#include "graphics/surface.h"

namespace {

// Index 0 navy, 1 yellow, 2 white.
const byte kComposePalette[256 * 3] = { 0, 0, 128, 255, 255, 0, 255, 255, 255 };

const Graphics::PixelFormat kComposeRgb565(2, 5, 6, 5, 0, 11, 5, 0, 0);
const Graphics::PixelFormat kComposeXrgb8888(4, 8, 8, 8, 0, 16, 8, 0, 0);

} // End of anonymous namespace

class HiResTextTextComposeTestSuite : public CxxTest::TestSuite {
public:
	void test_blend() {
		TS_ASSERT_EQUALS(Graphics::TextCompose::blend(0, 255, 170), 170);
		TS_ASSERT_EQUALS(Graphics::TextCompose::blend(128, 0, 170), 43);
		TS_ASSERT_EQUALS(Graphics::TextCompose::blend(10, 200, 0), 10);
		TS_ASSERT_EQUALS(Graphics::TextCompose::blend(10, 200, 255), 200);
	}

	void test_stamp() {
		byte dst[4] = { 9, 9, 9, 9 };
		const byte idx[4] = { 1, 2, 3, 4 };
		const byte cov[4] = { 0, 127, 128, 255 };
		Graphics::TextCompose::stampSpan(dst, idx, cov, 4);
		TS_ASSERT_EQUALS(dst[0], 9);
		TS_ASSERT_EQUALS(dst[1], 9);
		TS_ASSERT_EQUALS(dst[2], 3);
		TS_ASSERT_EQUALS(dst[3], 4);
	}

	// Yellow at 2 bpp levels over navy, read back from the screen.
	void test_composeRgb565() {
		uint16 dst[4];
		for (int i = 0; i < 4; ++i)
			dst[i] = (uint16)kComposeRgb565.RGBToColor(0, 0, 128);
		const byte idx[4] = { 1, 1, 1, 1 };
		const byte cov[4] = { 0, 85, 170, 255 };
		Graphics::TextCompose::composeSpan((byte *)dst, kComposeRgb565, idx, cov, 4, kComposePalette, Graphics::TextCompose::kBlend);
		TS_ASSERT_EQUALS(dst[0], 0x0010);
		TS_ASSERT_EQUALS(dst[1], 0x52AB);
		TS_ASSERT_EQUALS(dst[2], 0xAD45);
		TS_ASSERT_EQUALS(dst[3], 0xFFE0);
		Graphics::TextCompose::composeSpan((byte *)dst, kComposeRgb565, idx, cov, 4, kComposePalette, Graphics::TextCompose::kThreshold);
		TS_ASSERT_EQUALS(dst[1], 0x52AB);	// 85 < 128: untouched
		TS_ASSERT_EQUALS(dst[2], 0xFFE0);
	}

	// From the frame's own indices: the under colour is exact (navy is 128, not 132 read back from RGB565).
	void test_convertXrgb8888() {
		uint32 dst[4];
		const byte frame[4] = { 0, 0, 0, 2 };
		const byte idx[4] = { 1, 1, 1, 1 };
		const byte cov[4] = { 0, 85, 170, 255 };
		Graphics::TextCompose::convertSpan((byte *)dst, kComposeXrgb8888, frame, idx, cov, 4, kComposePalette, Graphics::TextCompose::kBlend);
		TS_ASSERT_EQUALS(dst[0], 0x000080u);
		TS_ASSERT_EQUALS(dst[1], 0x555555u);
		TS_ASSERT_EQUALS(dst[2], 0xAAAA2Bu);
		TS_ASSERT_EQUALS(dst[3], 0xFFFF00u);
		Graphics::TextCompose::convertSpan((byte *)dst, kComposeXrgb8888, frame, nullptr, nullptr, 4, kComposePalette, Graphics::TextCompose::kBlend);
		TS_ASSERT_EQUALS(dst[3], 0xFFFFFFu);
	}

	void test_compositeRectChecksPairing() {
		Graphics::TextPlane plane;
		plane.create(4, 2, 2);
		plane.plot(1, 1, 255, 1);
		Graphics::Surface s;
		s.create(8, 4, Graphics::PixelFormat::createFormatCLUT8());
		s.fillRect(Common::Rect(8, 4), 0);
		TS_ASSERT(!Graphics::TextCompose::compositeRect(plane, Common::Rect(8, 4), s, kComposePalette, Graphics::TextCompose::kBlend));
		TS_ASSERT(Graphics::TextCompose::compositeRect(plane, Common::Rect(8, 4), s, nullptr, Graphics::TextCompose::kStamp));
		TS_ASSERT_EQUALS(*(const byte *)s.getBasePtr(1, 1), 1);
		TS_ASSERT_EQUALS(*(const byte *)s.getBasePtr(2, 1), 0);
		s.free();
	}
};
```

- [ ] **Step 2: run, expect a compile failure.**
- [ ] **Step 3: implement** — `graphics/hires_text/text_compose.h`:

```cpp
#ifndef GRAPHICS_HIRES_TEXT_TEXT_COMPOSE_H
#define GRAPHICS_HIRES_TEXT_TEXT_COMPOSE_H

#include "common/rect.h"
#include "common/scummsys.h"

#include "graphics/pixelformat.h"

namespace Graphics {

struct Surface;
class TextPlane;

/**
 * The sinks of hi-res text: a TextPlane's {index, coverage} pixels put on a
 * hi-res screen. Palettes are 256 entries of 8-bit R, G, B.
 */
namespace TextCompose {

enum Mode {
	kStamp,     ///< CLUT8 screen: the index where coverage >= 128
	kThreshold, ///< RGB screen: the palette colour where coverage >= 128
	kBlend      ///< RGB screen: the palette colour blended by coverage
};

/** (dst * (255 - a) + src * a + 127) / 255, the renderer's blend. */
inline byte blend(byte dst, byte src, byte a) {
	return (byte)(((uint32)dst * (255 - a) + (uint32)src * a + 127) / 255);
}

/** CLUT8: @p dst[i] = @p index[i] where @p coverage[i] >= @p threshold. */
void stampSpan(byte *dst, const byte *index, const byte *coverage, int n, byte threshold = 128);

/**
 * RGB, over the pixels already in @p dst (2 or 4 bytes a pixel in @p fmt):
 * kThreshold writes the text colour at coverage >= 128, kBlend blends it in
 * by coverage. A pixel without coverage is not touched.
 */
void composeSpan(byte *dst, const PixelFormat &fmt, const byte *index, const byte *coverage, int n,
                 const byte *paletteRGB, Mode mode);

/**
 * RGB from a CLUT8 hi-res frame row: every pixel of @p dst becomes
 * @p frame's palette colour with the text put over it at 8 bits a channel
 * (so an RGB565 screen rounds once). @p index / @p coverage may be null:
 * no text on the row.
 */
void convertSpan(byte *dst, const PixelFormat &fmt, const byte *frame, const byte *index, const byte *coverage, int n,
                 const byte *paletteRGB, Mode mode);

/**
 * @p plane over @p dst inside @p rect (hi-res, clipped to both): kStamp on a
 * CLUT8 @p dst, kThreshold or kBlend on a 2 or 4 byte one. false (nothing
 * drawn) for any other pairing.
 */
bool compositeRect(const TextPlane &plane, const Common::Rect &rect, Surface &dst, const byte *paletteRGB, Mode mode);

/**
 * @p frame (CLUT8, the size of @p plane) and @p plane into @p dst (2 or 4
 * bytes a pixel, the same size) inside @p rect, through convertSpan().
 */
bool presentRect(const TextPlane &plane, const Surface &frame, const Common::Rect &rect, Surface &dst,
                 const byte *paletteRGB, Mode mode);

} // End of namespace TextCompose
} // End of namespace Graphics

#endif
```

`graphics/hires_text/text_compose.cpp`:

```cpp
#include "graphics/hires_text/text_compose.h"

#include "common/endian.h"
#include "common/util.h"

#include "graphics/hires_text/text_plane.h"
#include "graphics/surface.h"

namespace Graphics {
namespace TextCompose {

namespace {

enum { kChunk = 256 };

inline uint32 readPixel(const byte *p, int bpp) {
	return bpp == 2 ? READ_UINT16(p) : READ_UINT32(p);
}

inline void writePixel(byte *p, int bpp, uint32 c) {
	if (bpp == 2)
		WRITE_UINT16(p, c);
	else
		WRITE_UINT32(p, c);
}

// The text over (r, g, b); false when it leaves the pixel as it is.
inline bool putText(byte &r, byte &g, byte &b, byte index, byte coverage, const byte *pal, Mode mode) {
	if (!coverage)
		return false;
	const byte *c = pal + index * 3;
	if (mode == kBlend) {
		r = blend(r, c[0], coverage);
		g = blend(g, c[1], coverage);
		b = blend(b, c[2], coverage);
		return true;
	}
	if (coverage < 128)
		return false;
	r = c[0];
	g = c[1];
	b = c[2];
	return true;
}

bool rgbFormat(const PixelFormat &fmt) {
	return fmt.bytesPerPixel == 2 || fmt.bytesPerPixel == 4;
}

} // End of anonymous namespace

void stampSpan(byte *dst, const byte *index, const byte *coverage, int n, byte threshold) {
	for (int i = 0; i < n; ++i)
		if (coverage[i] && coverage[i] >= threshold)
			dst[i] = index[i];
}

void composeSpan(byte *dst, const PixelFormat &fmt, const byte *index, const byte *coverage, int n,
                 const byte *paletteRGB, Mode mode) {
	const int bpp = fmt.bytesPerPixel;
	for (int i = 0; i < n; ++i, dst += bpp) {
		if (!coverage[i])
			continue;
		byte r, g, b;
		fmt.colorToRGB(readPixel(dst, bpp), r, g, b);
		if (putText(r, g, b, index[i], coverage[i], paletteRGB, mode))
			writePixel(dst, bpp, fmt.RGBToColor(r, g, b));
	}
}

void convertSpan(byte *dst, const PixelFormat &fmt, const byte *frame, const byte *index, const byte *coverage, int n,
                 const byte *paletteRGB, Mode mode) {
	const int bpp = fmt.bytesPerPixel;
	for (int i = 0; i < n; ++i, dst += bpp) {
		const byte *c = paletteRGB + frame[i] * 3;
		byte r = c[0], g = c[1], b = c[2];
		if (coverage)
			putText(r, g, b, index[i], coverage[i], paletteRGB, mode);
		writePixel(dst, bpp, fmt.RGBToColor(r, g, b));
	}
}

bool compositeRect(const TextPlane &plane, const Common::Rect &rect, Surface &dst, const byte *paletteRGB, Mode mode) {
	if (!plane.exists() || !dst.getPixels())
		return false;
	const bool clut8 = dst.format.isCLUT8();
	if ((mode == kStamp) != clut8 || (!clut8 && (!rgbFormat(dst.format) || !paletteRGB)))
		return false;
	Common::Rect r(rect);
	r.clip(Common::Rect(MIN<int>(plane.getWidth(), dst.w), MIN<int>(plane.getHeight(), dst.h)));
	byte idxScratch[kChunk], covScratch[kChunk];
	for (int y = r.top; y < r.bottom; ++y) {
		if (!plane.rowHasText(y))
			continue;
		for (int x = r.left; x < r.right; x += kChunk) {
			const int n = MIN<int>(kChunk, r.right - x);
			const byte *index, *coverage;
			if (!plane.getRow(y, x, n, idxScratch, covScratch, index, coverage))
				break;
			byte *p = (byte *)dst.getBasePtr(x, y);
			if (clut8)
				stampSpan(p, index, coverage, n);
			else
				composeSpan(p, dst.format, index, coverage, n, paletteRGB, mode);
		}
	}
	return true;
}

bool presentRect(const TextPlane &plane, const Surface &frame, const Common::Rect &rect, Surface &dst,
                 const byte *paletteRGB, Mode mode) {
	if (!plane.exists() || !frame.getPixels() || !dst.getPixels() || !frame.format.isCLUT8() || !rgbFormat(dst.format) ||
	    !paletteRGB || mode == kStamp)
		return false;
	Common::Rect r(rect);
	r.clip(Common::Rect(MIN<int>(plane.getWidth(), MIN<int>(frame.w, dst.w)), MIN<int>(plane.getHeight(), MIN<int>(frame.h, dst.h))));
	byte idxScratch[kChunk], covScratch[kChunk];
	for (int y = r.top; y < r.bottom; ++y) {
		for (int x = r.left; x < r.right; x += kChunk) {
			const int n = MIN<int>(kChunk, r.right - x);
			const byte *index = nullptr, *coverage = nullptr;
			if (!plane.getRow(y, x, n, idxScratch, covScratch, index, coverage))
				coverage = nullptr;
			convertSpan((byte *)dst.getBasePtr(x, y), dst.format, (const byte *)frame.getBasePtr(x, y), index, coverage, n,
			            paletteRGB, mode);
		}
	}
	return true;
}

} // End of namespace TextCompose
} // End of namespace Graphics
```

`graphics/module.mk`: `	hires_text/text_compose.o \` (before `text_layout.o`).

- [ ] **Step 4: run** → `499 tests ... OK!`.
- [ ] **Step 5: commit**

```
GRAPHICS: Hi-res text: Add the compositing sinks

A TextPlane goes on a CLUT8 screen by threshold (the index where
coverage is 128 or more) and on a 16 or 32 bit screen through the
palette, blended or by threshold. convertSpan() turns a CLUT8 hi-res
frame and the plane into RGB in one pass, blending at 8 bits a channel.

Assisted-by: Claude Code
```

### Review checkpoint B (after Task 6)

superpowers:requesting-code-review on Tasks 5-6. Focus: no write outside the plane or surface for any rect (negative,
past the edge, empty); `getRow` scratch sizes; the plot rule (item 10 of section 1); `compositeRect` refuses CLUT8 +
blend and RGB + stamp; 2 and 4 byte pixels only; `presentRect` writes every pixel of the rect.

## Task 7: Map model and value helpers (15 min)

- [ ] **Step 1: test** — `test/graphics/hires_text_font_map.h`, the helper tests only (Task 8 replaces the file with
  the full suite):

```cpp
#include <cxxtest/TestSuite.h>

#include "common/memstream.h"

#include "graphics/hires_text/font_map.h"

class HiResTextFontMapTestSuite : public CxxTest::TestSuite {
public:
	void test_codePoints() {
		uint32 cp;
		TS_ASSERT(Graphics::HiResFontMap::parseCodePoint("U+25A1", cp));
		TS_ASSERT_EQUALS(cp, 0x25A1u);
		TS_ASSERT(Graphics::HiResFontMap::parseCodePoint("0x10ffff", cp));
		TS_ASSERT(!Graphics::HiResFontMap::parseCodePoint("u+110000", cp));
		TS_ASSERT(!Graphics::HiResFontMap::parseCodePoint("25A1", cp));
		TS_ASSERT(!Graphics::HiResFontMap::parseCodePoint("u+", cp));
	}

	void test_resolveFace() {
		Graphics::HiResFaceRef r = Graphics::HiResFontMap::resolveFace("data:hires_text/X.SVF", Common::Path("game/ko"));
		TS_ASSERT(r.data);
		TS_ASSERT_EQUALS(r.path.toString('/'), "hires_text/X.SVF");
		TS_ASSERT_EQUALS(r.key(), "data:hires_text/X.SVF");
		r = Graphics::HiResFontMap::resolveFace("A.SVF", Common::Path("game/ko"));
		TS_ASSERT(!r.data);
		TS_ASSERT_EQUALS(r.key(), "game/ko/A.SVF");
		TS_ASSERT_EQUALS(Graphics::HiResFontMap::resolveFace("/abs/B.SVF", Common::Path("game")).key(), "/abs/B.SVF");
		TS_ASSERT_EQUALS(Graphics::HiResFontMap::resolveFace("C:\\fonts\\C.SVF", Common::Path("game")).path.toString('/'), "C:/fonts/C.SVF");
	}
};
```

- [ ] **Step 2: run, expect a compile failure.**
- [ ] **Step 3: implement** — `graphics/hires_text/font_map.h`:

```cpp
#ifndef GRAPHICS_HIRES_TEXT_FONT_MAP_H
#define GRAPHICS_HIRES_TEXT_FONT_MAP_H

#include "common/array.h"
#include "common/hash-str.h"
#include "common/hashmap.h"
#include "common/path.h"
#include "common/str.h"

#include "graphics/hires_text/text_layout.h"
#include "graphics/hires_text/unicode_ranges.h"

namespace Common {
class SeekableReadStream;
}

namespace Graphics {

enum HiResRenderTarget { kHiResTargetAuto = 0, kHiResTargetClut8, kHiResTargetRgb565, kHiResTargetRgb888 };
enum HiResBlend { kHiResBlendAuto = 0, kHiResBlendOn, kHiResBlendOff };
/** advance=: the face's own advance, the game font's, or the face's cell width. */
enum HiResAdvance { kHiResAdvanceFont = 0, kHiResAdvanceGame, kHiResAdvanceCell };
/** cell=: the layout cell is the engine's own, or the face's size= (read by the engine). */
enum HiResCellMode { kHiResCellGame = 0, kHiResCellGlyph };
/** align=: the face's baseline on the game font's, centred in the cell, or the face's own line. */
enum HiResAlign { kHiResAlignGame = 0, kHiResAlignCell, kHiResAlignFont };
/** [load]: how a face file is held. */
enum HiResLoadMode { kHiResLoadAuto = 0, kHiResLoadWhole, kHiResLoadStream };

/** A face file named by a map: relative to a data folder ("data:" values) or a resolved path. */
struct HiResFaceRef {
	HiResFaceRef() : data(false) {}
	Common::String name;  ///< the [fonts] name, or the value as written
	Common::Path path;    ///< data: the part after "data:"; else the path
	bool data;
	Common::String key() const { return (data ? "data:" : "") + path.toString('/'); }
};

/** One font value: faces tried in order, then (endsInOriginal) the game's own font. */
struct HiResFontValue {
	HiResFontValue() : same(false), sameAt(0), endsInOriginal(false) {}
	Common::Array<HiResFaceRef> faces;
	bool same;            ///< a `same` entry: the enclosing chain goes where it stood
	uint sameAt;          ///< faces before `same`
	bool endsInOriginal;
	bool empty() const { return faces.empty() && !same && !endsInOriginal; }
};

/** One [font] or [font.N] section, its qualified sections merged in. */
struct HiResFontScope {
	HiResFontScope();

	HiResFontValue face;
	bool faceSet;
	int size;             ///< hi-res px of the chain's tallest cell
	bool sizeSet;
	int shift;            ///< hi-res px down, -32..32
	bool shiftSet;
	HiResCellMode cell;
	bool cellSet;
	HiResAlign align;
	bool alignSet;
	uint32 missing;       ///< code point drawn for one no face has; 0: none
	bool missingSet;
	HiResAdvance advance;
	bool advanceSet;
	Common::Array<HiResRangeSpec> rangeSpecs;
	Common::Array<HiResFontValue> rangeValues;
	Common::Array<HiResRangeSpec> advanceSpecs;
	Common::Array<HiResAdvance> advanceValues;
};

/** [layout]; unset keys keep the engine's BreakRules. */
struct HiResLayoutSettings {
	HiResLayoutSettings() : hangul(kHangulBreakWord), hangulSet(false), kinsoku(true), kinsokuSet(false), thai(true), thaiSet(false) {}
	HangulBreak hangul;
	bool hangulSet;
	bool kinsoku;
	bool kinsokuSet;
	bool thai;
	bool thaiSet;
	/** @p rules with the keys this map sets. */
	BreakRules apply(const BreakRules &rules) const;
};

typedef Common::HashMap<Common::String, HiResFaceRef, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> HiResFaceNames;

/** A version 3 map. */
struct HiResMap {
	HiResMap() { clear(); }
	void clear();

	int version;
	HiResRenderTarget target;   ///< [render] target=
	bool targetSet;
	HiResBlend blend;           ///< [render] blend=
	bool blendSet;
	int scale;                  ///< [render] scale=, 1..2; 0 auto
	bool scaleSet;
	HiResLayoutSettings layout;
	HiResFaceNames faces;       ///< [fonts]
	HiResFontScope font;        ///< [font]
	Common::HashMap<int, HiResFontScope> fontIds; ///< [font.N]
	Common::HashMap<Common::String, HiResLoadMode, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> loads; ///< [load], by face name
	Common::HashMap<Common::String, HiResFaceRef, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> preloads; ///< [preload], by face name
	Common::Array<Common::String> warnings;

	const HiResFontScope *fontIdScope(int id) const;
	HiResLoadMode loadModeFor(const HiResFaceRef &face) const;
};

/** The qualifiers a load merges: `[S:engine:target]`, `[S:engine]`, `[S:target]` over `[S]`. */
struct HiResMapQualifiers {
	HiResMapQualifiers() : target(kHiResTargetAuto) {}
	Common::String engine;      ///< sci, scumm, agi; empty: none
	HiResRenderTarget target;   ///< auto: no target section is merged
};

namespace HiResFontMap {

/** "data:" */
extern const char *const kDataPrefix;

/**
 * Parses a version 3 map. @p name names the map in warnings; relative paths
 * are taken against @p mapDir. False when the map is refused (no [map]
 * version=3, a version 2 map, a line that is not INI); every other problem
 * is a warning in @p out.warnings and the key is ignored.
 */
bool load(Common::SeekableReadStream &stream, const Common::String &name, const Common::Path &mapDir,
          const HiResMapQualifiers &qualifiers, HiResMap &out);

/** A map value as a face reference: "data:" values, absolute paths, or a path relative to @p mapDir. */
HiResFaceRef resolveFace(const Common::String &value, const Common::Path &mapDir);

/** The qualifier name of a resolved target: clut8 or rgb (empty for auto). */
const char *targetQualifier(HiResRenderTarget target);

bool parseRenderTarget(const Common::String &text, HiResRenderTarget &out);
bool parseBlend(const Common::String &text, HiResBlend &out);
bool parseBool(const Common::String &text, bool &out);
/** u+XXXX, U+XXXX or 0xXXXX (1 to 6 hex digits, at most U+10FFFF). */
bool parseCodePoint(const Common::String &text, uint32 &out);

} // End of namespace HiResFontMap

} // End of namespace Graphics

#endif
```

`graphics/hires_text/font_map.cpp`, first part (Task 8 inserts the parser):

```cpp
#include "graphics/hires_text/font_map.h"

#include "common/stream.h"
#include "common/util.h"

namespace Graphics {

HiResFontScope::HiResFontScope()
	: faceSet(false), size(0), sizeSet(false), shift(0), shiftSet(false), cell(kHiResCellGame), cellSet(false),
	  align(kHiResAlignGame), alignSet(false), missing(0), missingSet(false), advance(kHiResAdvanceFont), advanceSet(false) {
}

BreakRules HiResLayoutSettings::apply(const BreakRules &rules) const {
	BreakRules r = rules;
	if (hangulSet)
		r.hangul = hangul;
	if (kinsokuSet)
		r.kinsoku = kinsoku;
	if (thaiSet)
		r.thaiFallback = thai;
	return r;
}

void HiResMap::clear() {
	version = 0;
	target = kHiResTargetAuto;
	targetSet = false;
	blend = kHiResBlendAuto;
	blendSet = false;
	scale = 0;
	scaleSet = false;
	layout = HiResLayoutSettings();
	faces.clear();
	font = HiResFontScope();
	fontIds.clear();
	loads.clear();
	preloads.clear();
	warnings.clear();
}

const HiResFontScope *HiResMap::fontIdScope(int id) const {
	Common::HashMap<int, HiResFontScope>::const_iterator it = fontIds.find(id);
	return it == fontIds.end() ? nullptr : &it->_value;
}

HiResLoadMode HiResMap::loadModeFor(const HiResFaceRef &face) const {
	return loads.getValOrDefault(face.name, kHiResLoadAuto);
}

namespace HiResFontMap {

const char *const kDataPrefix = "data:";
const char *targetQualifier(HiResRenderTarget target) {
	switch (target) {
	case kHiResTargetClut8:
		return "clut8";
	case kHiResTargetRgb565:
	case kHiResTargetRgb888:
		return "rgb";
	default:
		return "";
	}
}

bool parseRenderTarget(const Common::String &text, HiResRenderTarget &out) {
	static const char *const names[] = { "auto", "clut8", "rgb565", "rgb888" };
	for (int i = 0; i < 4; ++i) {
		if (text.equalsIgnoreCase(names[i])) {
			out = (HiResRenderTarget)i;
			return true;
		}
	}
	return false;
}

bool parseBlend(const Common::String &text, HiResBlend &out) {
	if (text.equalsIgnoreCase("auto"))
		out = kHiResBlendAuto;
	else if (text.equalsIgnoreCase("on"))
		out = kHiResBlendOn;
	else if (text.equalsIgnoreCase("off"))
		out = kHiResBlendOff;
	else
		return false;
	return true;
}

bool parseBool(const Common::String &text, bool &out) {
	if (text.equalsIgnoreCase("on")) {
		out = true;
		return true;
	}
	if (text.equalsIgnoreCase("off")) {
		out = false;
		return true;
	}
	return !text.empty() && Common::parseBool(text, out);
}

bool parseCodePoint(const Common::String &text, uint32 &out) {
	const char *p = text.c_str();
	if ((p[0] == 'u' || p[0] == 'U') && p[1] == '+')
		p += 2;
	else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
		p += 2;
	else
		return false;
	const size_t n = strlen(p);
	if (n < 1 || n > 6)
		return false;
	uint32 v = 0;
	for (; *p; ++p) {
		if (!Common::isXDigit(*p))
			return false;
		v = v * 16 + (uint32)(Common::isDigit(*p) ? *p - '0' : (*p | 0x20) - 'a' + 10);
	}
	if (v > 0x10FFFF)
		return false;
	out = v;
	return true;
}

HiResFaceRef resolveFace(const Common::String &value, const Common::Path &mapDir) {
	HiResFaceRef ref;
	ref.name = value;
	if (value.hasPrefix(kDataPrefix)) {
		ref.data = true;
		ref.path = Common::Path(value.c_str() + strlen(kDataPrefix), '/');
	} else if (!value.empty() && (value[0] == '/' || value[0] == '\\' || (value.size() > 2 && value[1] == ':'))) {
		ref.path = Common::Path(value, value.contains('\\') ? '\\' : '/');
	} else {
		ref.path = mapDir.join(Common::Path(value, '/'));
	}
	return ref;
}
} // End of namespace HiResFontMap

} // End of namespace Graphics
```

`graphics/module.mk`: `	hires_text/font_map.o \` (after `banded_plane.o`).

- [ ] **Step 4: run** → `501 tests ... OK!`.
- [ ] **Step 5: commit**

```
GRAPHICS: Hi-res text: Add the version 3 map model

What a hi-res text map says after loading: render target, blend and
scale, the [fonts] names, [font] and [font.N] scopes with their range
rules, [load], [preload] and [layout]; face references relative to the
map or to a data folder ("data:").

Assisted-by: Claude Code
```

## Task 8: Map parser (20 min)

- [ ] **Step 1: test** — the whole `test/graphics/hires_text_font_map.h`:

```cpp
#include <cxxtest/TestSuite.h>

#include "common/memstream.h"

#include "graphics/hires_text/font_map.h"

class HiResTextFontMapTestSuite : public CxxTest::TestSuite {
	bool load(const char *text, Graphics::HiResMap &map, const char *engine = "", Graphics::HiResRenderTarget t = Graphics::kHiResTargetAuto) {
		Common::MemoryReadStream s((const byte *)text, strlen(text));
		Graphics::HiResMapQualifiers q;
		q.engine = engine;
		q.target = t;
		return Graphics::HiResFontMap::load(s, "T.MAP", Common::Path("game/ko"), q, map);
	}

	bool warned(const Graphics::HiResMap &map, const char *part) {
		for (uint i = 0; i < map.warnings.size(); ++i)
			if (map.warnings[i].contains(part))
				return true;
		return false;
	}

public:
	void test_versionGate() {
		Graphics::HiResMap m;
		TS_ASSERT(!load("[map]\nversion=2\n[font]\nface=a.svf\n", m));
		TS_ASSERT(warned(m, "T.MAP:1: map version 2 is not read: set [map] version=3"));
		TS_ASSERT(!m.font.faceSet);
		TS_ASSERT(!load("[fonts]\na=a.svf\n", m));
		TS_ASSERT(warned(m, "no [map] version=3"));
		TS_ASSERT(!load("version=3\n", m));
		TS_ASSERT(load("\xEF\xBB\xBF[map]\nversion=3\n", m));
		TS_ASSERT(m.warnings.empty());
	}

	void test_fontsAndFaces() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=3\n"
		               "[fonts]\nko=NEODG16A.SVF ; the jamo face\nfb=data:hires_text/NEODG16A.SVF\n"
		               "[font]\nface=ko,fb\nmissing=u+25a1\n"
		               "[font.300]\nface=x/BIG.SVF,same,original,ko\nsize=24\nshift=-2\ncell=glyph\nalign=font\n", m));
		TS_ASSERT_EQUALS(m.version, 3);
		TS_ASSERT_EQUALS(m.faces["KO"].path.toString('/'), "game/ko/NEODG16A.SVF");
		TS_ASSERT(m.faces["fb"].data);
		TS_ASSERT_EQUALS(m.faces["fb"].key(), "data:hires_text/NEODG16A.SVF");
		TS_ASSERT_EQUALS(m.font.face.faces.size(), 2u);
		TS_ASSERT_EQUALS(m.font.missing, 0x25A1u);
		const Graphics::HiResFontScope *s = m.fontIdScope(300);
		TS_ASSERT(s);
		TS_ASSERT_EQUALS(s->face.faces.size(), 1u);
		TS_ASSERT_EQUALS(s->face.faces[0].path.toString('/'), "game/ko/x/BIG.SVF");
		TS_ASSERT(s->face.same);
		TS_ASSERT_EQUALS(s->face.sameAt, 1u);
		TS_ASSERT(s->face.endsInOriginal);
		TS_ASSERT(warned(m, "entries after 'original' are ignored: 'ko'"));
		TS_ASSERT_EQUALS(s->size, 24);
		TS_ASSERT_EQUALS(s->shift, -2);
		TS_ASSERT_EQUALS(s->cell, Graphics::kHiResCellGlyph);
		TS_ASSERT_EQUALS(s->align, Graphics::kHiResAlignFont);
		TS_ASSERT(!m.fontIdScope(4));
	}

	void test_ranges() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=3\n[fonts]\nko=K.SVF\n[font]\nface=ko\nrange.basic-latin=original\n"
		               "range.u+ac00-d7a3=same\nadvance.basic-latin=game\nrange.nonsense=ko\nadvance=cell\n", m));
		TS_ASSERT_EQUALS(m.font.rangeSpecs.size(), 2u);
		TS_ASSERT(m.font.rangeValues[0].endsInOriginal);
		TS_ASSERT(m.font.rangeValues[1].same);
		TS_ASSERT_EQUALS(m.font.advanceValues[0], Graphics::kHiResAdvanceGame);
		TS_ASSERT_EQUALS(m.font.advance, Graphics::kHiResAdvanceCell);
		TS_ASSERT(warned(m, "unknown range 'nonsense'"));
	}

	void test_qualifiers() {
		const char *text = "[map]\nversion=3\n[render]\nblend=auto\nscale=2\n[render:clut8]\nblend=off\n"
		                   "[render:sci]\ntarget=rgb565\n[render:sci:rgb]\nblend=on\n[render:scumm]\nscale=1\n";
		Graphics::HiResMap m;
		TS_ASSERT(load(text, m));
		TS_ASSERT_EQUALS(m.blend, Graphics::kHiResBlendAuto);
		TS_ASSERT(!m.targetSet);
		TS_ASSERT_EQUALS(m.scale, 2);
		TS_ASSERT(load(text, m, "sci", Graphics::kHiResTargetClut8));
		TS_ASSERT_EQUALS(m.blend, Graphics::kHiResBlendOff);
		TS_ASSERT_EQUALS(m.target, Graphics::kHiResTargetRgb565);
		TS_ASSERT(load(text, m, "sci", Graphics::kHiResTargetRgb888));
		TS_ASSERT_EQUALS(m.blend, Graphics::kHiResBlendOn);
		TS_ASSERT(load(text, m, "scumm", Graphics::kHiResTargetRgb565));
		TS_ASSERT_EQUALS(m.scale, 1);
		TS_ASSERT(m.warnings.empty());
		TS_ASSERT(load("[map]\nversion=3\n[render:dos]\nscale=1\n", m));
		TS_ASSERT(warned(m, "unknown qualifier 'dos'"));
		TS_ASSERT(!m.scaleSet);
	}

	void test_removedAndUnknown() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=3\n[render]\ngamma=1.2\n[text]\nencoding=cp949\n[shadow]\nmode=drop\n"
		               "[glyphs]\n0x41=original\n[font]\npixel=16\nmirror=off\ncolour=3\n[bogus]\n", m));
		TS_ASSERT(warned(m, "T.MAP:4: 'gamma' was removed in map version 3; delete it"));
		TS_ASSERT(warned(m, "[text] was removed"));
		TS_ASSERT(warned(m, "[shadow] was removed"));
		TS_ASSERT(warned(m, "[glyphs] was removed"));
		TS_ASSERT(warned(m, "'pixel' was removed"));
		TS_ASSERT(warned(m, "'mirror' was removed"));
		TS_ASSERT(warned(m, "unknown key 'colour'"));
		TS_ASSERT(warned(m, "unknown section [bogus]"));
	}

	void test_loadPreloadLayout() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=3\n[fonts]\nko=K.SVF\n[load]\nko=stream\nzz=whole\n[preload]\nko=K.HOT\n"
		               "[layout]\nhangul=any\nkinsoku=off\n", m));
		TS_ASSERT_EQUALS(m.loadModeFor(m.faces["ko"]), Graphics::kHiResLoadStream);
		TS_ASSERT(warned(m, "[load] names 'zz'"));
		TS_ASSERT_EQUALS(m.preloads["ko"].path.toString('/'), "game/ko/K.HOT");
		const Graphics::BreakRules r = m.layout.apply(Graphics::BreakRules());
		TS_ASSERT_EQUALS(r.hangul, Graphics::kHangulBreakAny);
		TS_ASSERT(!r.kinsoku);
		TS_ASSERT(r.thaiFallback);
	}

	void test_codePoints() {
		uint32 cp;
		TS_ASSERT(Graphics::HiResFontMap::parseCodePoint("U+25A1", cp));
		TS_ASSERT_EQUALS(cp, 0x25A1u);
		TS_ASSERT(Graphics::HiResFontMap::parseCodePoint("0x10ffff", cp));
		TS_ASSERT(!Graphics::HiResFontMap::parseCodePoint("u+110000", cp));
		TS_ASSERT(!Graphics::HiResFontMap::parseCodePoint("25A1", cp));
		TS_ASSERT(!Graphics::HiResFontMap::parseCodePoint("u+", cp));
	}

	void test_resolveFace() {
		Graphics::HiResFaceRef r = Graphics::HiResFontMap::resolveFace("data:hires_text/X.SVF", Common::Path("game/ko"));
		TS_ASSERT(r.data);
		TS_ASSERT_EQUALS(r.path.toString('/'), "hires_text/X.SVF");
		TS_ASSERT_EQUALS(r.key(), "data:hires_text/X.SVF");
		r = Graphics::HiResFontMap::resolveFace("A.SVF", Common::Path("game/ko"));
		TS_ASSERT(!r.data);
		TS_ASSERT_EQUALS(r.key(), "game/ko/A.SVF");
		TS_ASSERT_EQUALS(Graphics::HiResFontMap::resolveFace("/abs/B.SVF", Common::Path("game")).key(), "/abs/B.SVF");
		TS_ASSERT_EQUALS(Graphics::HiResFontMap::resolveFace("C:\\fonts\\C.SVF", Common::Path("game")).path.toString('/'), "C:/fonts/C.SVF");
	}
};
```

- [ ] **Step 2: run, expect a link failure** (`Graphics::HiResFontMap::load` undefined).
- [ ] **Step 3: implement** — in `font_map.cpp` insert after the line `const char *const kDataPrefix = "data:";`:

```cpp
namespace {

struct Key {
	Common::String key, value;
	int line;
};

struct Section {
	Common::String base;    ///< lower case, without qualifiers
	Common::String engine;  ///< lower case; empty: none
	Common::String target;  ///< clut8, rgb; empty: none
	int line;
	Common::Array<Key> keys;
};

bool isEngineQualifier(const Common::String &q) {
	return q == "sci" || q == "scumm" || q == "agi";
}

bool isTargetQualifier(const Common::String &q) {
	return q == "clut8" || q == "rgb";
}

// A ';' or '#' after a space or a tab starts a comment.
Common::String stripComment(const Common::String &line) {
	for (uint i = 1; i < line.size(); ++i) {
		if ((line[i] == ';' || line[i] == '#') && (line[i - 1] == ' ' || line[i - 1] == '\t'))
			return Common::String(line.c_str(), i);
	}
	return line;
}

bool parseInt(const Common::String &text, int lo, int hi, int &out) {
	if (text.empty())
		return false;
	char *end = nullptr;
	const long v = strtol(text.c_str(), &end, 10);
	if (*end || v < lo || v > hi)
		return false;
	out = (int)v;
	return true;
}

class Loader {
public:
	Loader(const Common::String &name, const Common::Path &mapDir, HiResMap &out) : _name(name), _dir(mapDir), _out(out) {}

	void warn(int line, const Common::String &msg) {
		_out.warnings.push_back(Common::String::format("%s:%d: %s", _name.c_str(), line, msg.c_str()));
	}

	bool read(Common::SeekableReadStream &stream, Common::Array<Section> &sections) {
		int lineNo = 0;
		while (!stream.eos() && !stream.err()) {
			Common::String line = stream.readLine();
			++lineNo;
			if (lineNo == 1 && line.size() >= 3 && (byte)line[0] == 0xEF && (byte)line[1] == 0xBB && (byte)line[2] == 0xBF)
				line.erase(0, 3);
			line.trim();
			if (line.empty() || line[0] == ';' || line[0] == '#')
				continue;
			line = stripComment(line);
			line.trim();
			if (line[0] == '[') {
				if (line.lastChar() != ']') {
					warn(lineNo, "a section line must end with ']'");
					return false;
				}
				Section s;
				s.line = lineNo;
				Common::String body(line.c_str() + 1, line.size() - 2);
				body.toLowercase();
				const char *colon = strchr(body.c_str(), ':');
				s.base = colon ? Common::String(body.c_str(), colon) : body;
				while (colon) {
					const char *next = strchr(colon + 1, ':');
					const Common::String q = next ? Common::String(colon + 1, next) : Common::String(colon + 1);
					if (isEngineQualifier(q) && s.engine.empty())
						s.engine = q;
					else if (isTargetQualifier(q) && s.target.empty())
						s.target = q;
					else {
						warn(lineNo, "unknown qualifier '" + q + "' (sci, scumm, agi, clut8, rgb); section ignored");
						s.base.clear();
						break;
					}
					colon = next;
				}
				sections.push_back(s);
				continue;
			}
			const char *eq = strchr(line.c_str(), '=');
			if (!eq || sections.empty()) {
				warn(lineNo, sections.empty() ? "a key before the first section" : "not a key=value line");
				return false;
			}
			Key k;
			k.key = Common::String(line.c_str(), eq);
			k.value = Common::String(eq + 1);
			k.key.trim();
			k.key.toLowercase();
			k.value.trim();
			k.line = lineNo;
			sections.back().keys.push_back(k);
		}
		return true;
	}

	// The sections named @p base that apply, least specific first: S, S:t, S:e, S:e:t.
	Common::Array<const Section *> select(const Common::Array<Section> &sections, const Common::String &base,
	                                      const HiResMapQualifiers &q) {
		Common::Array<const Section *> out;
		const Common::String t = targetQualifier(q.target);
		for (int rank = 0; rank < 4; ++rank) {
			for (uint i = 0; i < sections.size(); ++i) {
				const Section &s = sections[i];
				if (s.base != base)
					continue;
				const bool e = !s.engine.empty(), tq = !s.target.empty();
				if (e && s.engine != q.engine)
					continue;
				if (tq && s.target != t)
					continue;
				if ((int)e * 2 + (int)tq == rank)
					out.push_back(&s);
			}
		}
		return out;
	}

	bool parseValue(const Common::String &text, int line, HiResFontValue &out) {
		out = HiResFontValue();
		Common::String rest = text;
		while (!rest.empty()) {
			const char *comma = strchr(rest.c_str(), ',');
			Common::String item = comma ? Common::String(rest.c_str(), comma) : rest;
			rest = comma ? Common::String(comma + 1) : Common::String();
			item.trim();
			if (item.empty())
				continue;
			if (out.endsInOriginal) {
				warn(line, "entries after 'original' are ignored: '" + item + "'");
				continue;
			}
			if (item.equalsIgnoreCase("original")) {
				out.endsInOriginal = true;
			} else if (item.equalsIgnoreCase("same")) {
				if (out.same)
					warn(line, "'same' twice; the second is ignored");
				out.same = true;
				out.sameAt = out.faces.size();
			} else if (_out.faces.contains(item)) {
				out.faces.push_back(_out.faces[item]);
			} else if (item.contains('.') || item.contains('/') || item.hasPrefix(kDataPrefix)) {
				out.faces.push_back(resolveFace(item, _dir));
			} else {
				warn(line, "unknown face name '" + item + "'");
			}
		}
		return !out.empty();
	}

	void parseScope(const Common::Array<const Section *> &list, HiResFontScope &s) {
		for (uint i = 0; i < list.size(); ++i) {
			for (uint k = 0; k < list[i]->keys.size(); ++k) {
				const Key &kv = list[i]->keys[k];
				const Common::String &v = kv.value;
				if (kv.key == "face") {
					HiResFontValue fv;
					if (parseValue(v, kv.line, fv)) {
						s.face = fv;
						s.faceSet = true;
					}
				} else if (kv.key == "size") {
					if (parseInt(v, 4, 255, s.size))
						s.sizeSet = true;
					else
						warn(kv.line, "size '" + v + "' is not 4..255");
				} else if (kv.key == "shift") {
					if (parseInt(v, -32, 32, s.shift))
						s.shiftSet = true;
					else
						warn(kv.line, "shift '" + v + "' is not -32..32");
				} else if (kv.key == "cell") {
					if (v.equalsIgnoreCase("game") || v.equalsIgnoreCase("glyph")) {
						s.cell = v.equalsIgnoreCase("game") ? kHiResCellGame : kHiResCellGlyph;
						s.cellSet = true;
					} else
						warn(kv.line, "cell '" + v + "' is not game or glyph");
				} else if (kv.key == "align") {
					if (v.equalsIgnoreCase("game"))
						s.align = kHiResAlignGame;
					else if (v.equalsIgnoreCase("cell"))
						s.align = kHiResAlignCell;
					else if (v.equalsIgnoreCase("font"))
						s.align = kHiResAlignFont;
					else {
						warn(kv.line, "align '" + v + "' is not game, cell or font");
						continue;
					}
					s.alignSet = true;
				} else if (kv.key == "missing") {
					if (v.equalsIgnoreCase("off")) {
						s.missing = 0;
						s.missingSet = true;
					} else if (parseCodePoint(v, s.missing))
						s.missingSet = true;
					else
						warn(kv.line, "missing '" + v + "' is not a code point or off");
				} else if (kv.key == "advance") {
					if (parseAdvance(v, s.advance))
						s.advanceSet = true;
					else
						warn(kv.line, "advance '" + v + "' is not font, game or cell");
				} else if (kv.key.hasPrefix("range.") || kv.key.hasPrefix("advance.")) {
					const bool range = kv.key.hasPrefix("range.");
					HiResRangeSpec spec;
					Common::String error;
					if (!parseRangeSpec(Common::String(kv.key.c_str() + (range ? 6 : 8)), spec, error)) {
						warn(kv.line, error);
						continue;
					}
					if (range) {
						HiResFontValue fv;
						if (!parseValue(v, kv.line, fv))
							continue;
						s.rangeSpecs.push_back(spec);
						s.rangeValues.push_back(fv);
					} else {
						HiResAdvance a;
						if (!parseAdvance(v, a)) {
							warn(kv.line, "advance '" + v + "' is not font, game or cell");
							continue;
						}
						s.advanceSpecs.push_back(spec);
						s.advanceValues.push_back(a);
					}
				} else if (kv.key == "pixel" || kv.key == "origin" || kv.key.hasPrefix("origin.") || kv.key == "mirror") {
					warn(kv.line, "'" + kv.key + "' was removed in map version 3; delete it");
				} else {
					warn(kv.line, "unknown key '" + kv.key + "'");
				}
			}
		}
	}

	static bool parseAdvance(const Common::String &v, HiResAdvance &out) {
		if (v.equalsIgnoreCase("font"))
			out = kHiResAdvanceFont;
		else if (v.equalsIgnoreCase("game"))
			out = kHiResAdvanceGame;
		else if (v.equalsIgnoreCase("cell"))
			out = kHiResAdvanceCell;
		else
			return false;
		return true;
	}

private:
	Common::String _name;
	Common::Path _dir;
	HiResMap &_out;
};

} // End of anonymous namespace
```

and before `} // End of namespace HiResFontMap`:

```cpp
bool load(Common::SeekableReadStream &stream, const Common::String &name, const Common::Path &mapDir,
          const HiResMapQualifiers &qualifiers, HiResMap &out) {
	out.clear();
	Loader loader(name, mapDir, out);
	Common::Array<Section> sections;
	if (!loader.read(stream, sections))
		return false;

	// [map] version: the gate.
	Common::Array<const Section *> list = loader.select(sections, "map", HiResMapQualifiers());
	for (uint i = 0; i < list.size(); ++i)
		for (uint k = 0; k < list[i]->keys.size(); ++k)
			if (list[i]->keys[k].key == "version" && !parseInt(list[i]->keys[k].value, 1, 99, out.version))
				out.version = -1;
	if (out.version != 3) {
		if (out.version == 2)
			loader.warn(1, "map version 2 is not read: set [map] version=3 and delete pixel=, gamma=, origin=, mirror=, "
			               "[text], [shadow] and [glyphs]");
		else
			loader.warn(1, "no [map] version=3: not a hi-res text map of this ScummVM");
		const Common::Array<Common::String> w = out.warnings;
		out.clear();
		out.warnings = w;
		return false;
	}

	// Sections nobody reads any more, and unknown ones.
	for (uint i = 0; i < sections.size(); ++i) {
		const Common::String &b = sections[i].base;
		if (b.empty() || b == "map" || b == "render" || b == "layout" || b == "fonts" || b == "font" ||
		    b == "load" || b == "preload")
			continue;
		if (b.hasPrefix("font.")) {
			int id;
			if (!parseInt(Common::String(b.c_str() + 5), 0, 65535, id))
				loader.warn(sections[i].line, "[" + b + "]: the font id must be 0..65535");
			continue;
		}
		if (b == "text" || b == "shadow" || b.hasPrefix("shadow.") || b == "glyphs" || b.hasPrefix("glyphs."))
			loader.warn(sections[i].line, "[" + b + "] was removed in map version 3; delete it");
		else
			loader.warn(sections[i].line, "unknown section [" + b + "]");
	}

	list = loader.select(sections, "render", qualifiers);
	for (uint i = 0; i < list.size(); ++i) {
		for (uint k = 0; k < list[i]->keys.size(); ++k) {
			const Key &kv = list[i]->keys[k];
			if (kv.key == "target") {
				if (parseRenderTarget(kv.value, out.target))
					out.targetSet = true;
				else
					loader.warn(kv.line, "target '" + kv.value + "' is not auto, clut8, rgb565 or rgb888");
			} else if (kv.key == "blend") {
				if (parseBlend(kv.value, out.blend))
					out.blendSet = true;
				else
					loader.warn(kv.line, "blend '" + kv.value + "' is not auto, on or off");
			} else if (kv.key == "scale") {
				if (kv.value.equalsIgnoreCase("auto")) {
					out.scale = 0;
					out.scaleSet = true;
				} else if (parseInt(kv.value, 1, 2, out.scale))
					out.scaleSet = true;
				else
					loader.warn(kv.line, "scale '" + kv.value + "' is not auto, 1 or 2");
			} else if (kv.key == "gamma" || kv.key == "pixel") {
				loader.warn(kv.line, "'" + kv.key + "' was removed in map version 3; delete it");
			} else {
				loader.warn(kv.line, "unknown key '" + kv.key + "'");
			}
		}
	}

	list = loader.select(sections, "layout", qualifiers);
	for (uint i = 0; i < list.size(); ++i) {
		for (uint k = 0; k < list[i]->keys.size(); ++k) {
			const Key &kv = list[i]->keys[k];
			bool b;
			if (kv.key == "hangul") {
				if (kv.value.equalsIgnoreCase("word") || kv.value.equalsIgnoreCase("any")) {
					out.layout.hangul = kv.value.equalsIgnoreCase("any") ? kHangulBreakAny : kHangulBreakWord;
					out.layout.hangulSet = true;
				} else
					loader.warn(kv.line, "hangul '" + kv.value + "' is not word or any");
			} else if ((kv.key == "kinsoku" || kv.key == "thai") && HiResFontMap::parseBool(kv.value, b)) {
				(kv.key == "kinsoku" ? out.layout.kinsoku : out.layout.thai) = b;
				(kv.key == "kinsoku" ? out.layout.kinsokuSet : out.layout.thaiSet) = true;
			} else {
				loader.warn(kv.line, "unknown key or value '" + kv.key + "=" + kv.value + "'");
			}
		}
	}

	list = loader.select(sections, "fonts", qualifiers);
	for (uint i = 0; i < list.size(); ++i) {
		for (uint k = 0; k < list[i]->keys.size(); ++k) {
			const Key &kv = list[i]->keys[k];
			bool valid = !kv.key.empty() && kv.key != "same" && kv.key != "original" && kv.key != "data";
			for (uint c = 0; valid && c < kv.key.size(); ++c)
				valid = Common::isAlnum(kv.key[c]) || kv.key[c] == '_' || kv.key[c] == '-';
			if (!valid || kv.value.empty()) {
				loader.warn(kv.line, "'" + kv.key + "' is not a face name (letters, digits, _ and -; not same, original, data)");
				continue;
			}
			HiResFaceRef ref = resolveFace(kv.value, mapDir);
			ref.name = kv.key;
			out.faces[kv.key] = ref;
		}
	}

	list = loader.select(sections, "load", qualifiers);
	for (uint i = 0; i < list.size(); ++i) {
		for (uint k = 0; k < list[i]->keys.size(); ++k) {
			const Key &kv = list[i]->keys[k];
			HiResLoadMode m;
			if (kv.value.equalsIgnoreCase("auto"))
				m = kHiResLoadAuto;
			else if (kv.value.equalsIgnoreCase("whole"))
				m = kHiResLoadWhole;
			else if (kv.value.equalsIgnoreCase("stream"))
				m = kHiResLoadStream;
			else {
				loader.warn(kv.line, "load '" + kv.value + "' is not auto, whole or stream");
				continue;
			}
			if (!out.faces.contains(kv.key))
				loader.warn(kv.line, "[load] names '" + kv.key + "', which [fonts] does not");
			else
				out.loads[kv.key] = m;
		}
	}

	list = loader.select(sections, "preload", qualifiers);
	for (uint i = 0; i < list.size(); ++i) {
		for (uint k = 0; k < list[i]->keys.size(); ++k) {
			const Key &kv = list[i]->keys[k];
			if (!out.faces.contains(kv.key))
				loader.warn(kv.line, "[preload] names '" + kv.key + "', which [fonts] does not");
			else
				out.preloads[kv.key] = resolveFace(kv.value, mapDir);
		}
	}

	loader.parseScope(loader.select(sections, "font", qualifiers), out.font);
	for (uint i = 0; i < sections.size(); ++i) {
		int id;
		if (!sections[i].base.hasPrefix("font.") || !parseInt(Common::String(sections[i].base.c_str() + 5), 0, 65535, id) ||
		    out.fontIds.contains(id))
			continue;
		loader.parseScope(loader.select(sections, sections[i].base, qualifiers), out.fontIds[id]);
	}
	return true;
}
```

- [ ] **Step 4: run** → `507 tests ... OK!`.
- [ ] **Step 5: commit**

```
GRAPHICS: Hi-res text: Read version 3 maps

A small INI reader of its own (keys like range.u+ac00-d7a3, comments
after a value) merges [S], [S:target], [S:engine] and [S:engine:target]
sections in that order. A version 2 map is refused with a message
naming what to change; removed and unknown keys are warnings, kept in
the map for the caller to print once.

Assisted-by: Claude Code
```

## Task 9: Id plans (15 min)

- [ ] **Step 1: test** — `test/graphics/hires_text_id_plan.h`:

```cpp
#include <cxxtest/TestSuite.h>

#include "common/memstream.h"

#include "graphics/hires_text/id_plan.h"

class HiResTextIdPlanTestSuite : public CxxTest::TestSuite {
	Graphics::HiResMap load(const char *text) {
		Common::MemoryReadStream s((const byte *)text, strlen(text));
		Graphics::HiResMap m;
		Graphics::HiResFontMap::load(s, "T.MAP", Common::Path("g"), Graphics::HiResMapQualifiers(), m);
		return m;
	}

	Common::String names(const Graphics::HiResFaceChain &c) {
		Common::String s;
		for (uint i = 0; i < c.faces.size(); ++i)
			s += c.faces[i].name + " ";
		return s + (c.original ? "original" : "-");
	}

public:
	void test_precedence() {
		const Graphics::HiResMap m = load("[map]\nversion=3\n[fonts]\na=A.SVF\nb=B.SVF\nc=C.SVF\n"
		                                  "[font]\nface=a,b\nmissing=u+25a1\nsize=16\n"
		                                  "[font.300]\nface=c,same\nsize=24\nshift=1\n");
		Common::Array<Common::String> w;
		const Graphics::HiResIdPlan p0 = Graphics::compileIdPlan(m, 0, w);
		TS_ASSERT_EQUALS(names(p0.chains[0]), "a b -");
		TS_ASSERT_EQUALS(p0.size, 16);
		TS_ASSERT_EQUALS(p0.missing, 0x25A1u);
		const Graphics::HiResIdPlan p300 = Graphics::compileIdPlan(m, 300, w);
		TS_ASSERT_EQUALS(names(p300.chains[0]), "c a b -");
		TS_ASSERT_EQUALS(p300.size, 24);
		TS_ASSERT_EQUALS(p300.shift, 1);
		TS_ASSERT_EQUALS(p300.missing, 0x25A1u);
		TS_ASSERT(w.empty());
	}

	void test_ranges() {
		const Graphics::HiResMap m = load("[map]\nversion=3\n[fonts]\na=A.SVF\nb=B.SVF\n"
		                                  "[font]\nface=a\nrange.basic-latin=original\nadvance.basic-latin=game\n"
		                                  "[font.1]\nrange.u+0041-005a=b,same\n");
		Common::Array<Common::String> w;
		const Graphics::HiResIdPlan p = Graphics::compileIdPlan(m, 1, w);
		TS_ASSERT_EQUALS(p.chains.size(), 3u);
		TS_ASSERT_EQUALS(names(p.chains[p.chainFor('A')]), "b a -");	// [font.1]'s narrower rule
		TS_ASSERT_EQUALS(names(p.chains[p.chainFor('a')]), "original");
		TS_ASSERT_EQUALS(p.chainFor(0xAC00), 0u);
		TS_ASSERT_EQUALS(p.advanceFor('a'), Graphics::kHiResAdvanceGame);
		TS_ASSERT_EQUALS(p.advanceFor(0xAC00), Graphics::kHiResAdvanceFont);
	}

	void test_noFace() {
		const Graphics::HiResMap m = load("[map]\nversion=3\n");
		Common::Array<Common::String> w;
		const Graphics::HiResIdPlan p = Graphics::compileIdPlan(m, 7, w);
		TS_ASSERT(p.chains[0].empty());
		TS_ASSERT_EQUALS(w.size(), 1u);
		TS_ASSERT_EQUALS(w[0], "font 7: no face= names a face");
	}
};
```

- [ ] **Step 2: run, expect a compile failure.**
- [ ] **Step 3: implement** — `graphics/hires_text/id_plan.h`:

```cpp
#ifndef GRAPHICS_HIRES_TEXT_ID_PLAN_H
#define GRAPHICS_HIRES_TEXT_ID_PLAN_H

#include "common/array.h"
#include "common/str.h"

#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/unicode_ranges.h"

namespace Graphics {

/** A face chain with every `same` expanded: faces in order, then (original) the game's font. */
struct HiResFaceChain {
	HiResFaceChain() : original(false) {}
	Common::Array<HiResFaceRef> faces;
	bool original;
	bool empty() const { return faces.empty() && !original; }
};

/**
 * One engine font id's settings, every precedence resolved: [font.N] over
 * [font] over the defaults. chains[0] is the id's own chain; a range rule
 * routes a code point to another chain.
 */
struct HiResIdPlan {
	HiResIdPlan();

	int id;
	Common::Array<HiResFaceChain> chains;
	HiResRangeTable faceRules;        ///< value: index into chains
	HiResRangeTable advanceRules;     ///< value: a HiResAdvance
	HiResAdvance advance;
	uint32 missing;
	int size;                         ///< 0: the faces' own size
	int shift;
	HiResCellMode cell;
	HiResAlign align;

	/** The chain that draws @p cp: 0 unless a range rule names another. */
	uint chainFor(uint32 cp) const;
	HiResAdvance advanceFor(uint32 cp) const;
};

/** Compiles id @p id of @p map (a map with no [font.N] for it uses [font]). */
HiResIdPlan compileIdPlan(const HiResMap &map, int id, Common::Array<Common::String> &warnings);

} // End of namespace Graphics

#endif
```

`graphics/hires_text/id_plan.cpp`:

```cpp
#include "graphics/hires_text/id_plan.h"

namespace Graphics {

HiResIdPlan::HiResIdPlan()
	: id(-1), advance(kHiResAdvanceFont), missing(0), size(0), shift(0), cell(kHiResCellGame), align(kHiResAlignGame) {
}

uint HiResIdPlan::chainFor(uint32 cp) const {
	const int v = faceRules.lookup(cp);
	return v < 0 ? 0 : (uint)v;
}

HiResAdvance HiResIdPlan::advanceFor(uint32 cp) const {
	const int v = advanceRules.lookup(cp);
	return v < 0 ? advance : (HiResAdvance)v;
}

namespace {

// @p value with its `same` replaced by @p outer.
HiResFaceChain expand(const HiResFontValue &value, const HiResFaceChain &outer) {
	HiResFaceChain c;
	for (uint i = 0; i <= value.faces.size(); ++i) {
		if (value.same && i == value.sameAt) {
			c.faces.push_back(outer.faces);
			if (outer.original) {
				// The outer chain ends in the game font: nothing after it is reached.
				c.original = true;
				return c;
			}
		}
		if (i < value.faces.size())
			c.faces.push_back(value.faces[i]);
	}
	c.original = value.endsInOriginal;
	return c;
}

bool sameChain(const HiResFaceChain &a, const HiResFaceChain &b) {
	if (a.original != b.original || a.faces.size() != b.faces.size())
		return false;
	for (uint i = 0; i < a.faces.size(); ++i)
		if (a.faces[i].key() != b.faces[i].key())
			return false;
	return true;
}

uint addChain(Common::Array<HiResFaceChain> &chains, const HiResFaceChain &c) {
	for (uint i = 0; i < chains.size(); ++i)
		if (sameChain(chains[i], c))
			return i;
	chains.push_back(c);
	return chains.size() - 1;
}

} // End of anonymous namespace

HiResIdPlan compileIdPlan(const HiResMap &map, int id, Common::Array<Common::String> &warnings) {
	HiResIdPlan plan;
	plan.id = id;
	const HiResFontScope *own = map.fontIdScope(id);
	const HiResFontScope &base = map.font;

	// Scalars: [font.N], then [font].
#define HIRES_PICK(field) \
	if (own && own->field##Set) \
		plan.field = own->field; \
	else if (base.field##Set) \
		plan.field = base.field;
	HIRES_PICK(advance)
	HIRES_PICK(missing)
	HIRES_PICK(size)
	HIRES_PICK(shift)
	HIRES_PICK(cell)
	HIRES_PICK(align)
#undef HIRES_PICK

	// The id chain: [font] face, then [font.N] face with `same` = [font]'s.
	const HiResFaceChain none;
	HiResFaceChain chain = base.faceSet ? expand(base.face, none) : none;
	if (own && own->faceSet)
		chain = expand(own->face, chain);
	if (chain.empty())
		warnings.push_back(Common::String::format("font %d: no face= names a face", id));
	plan.chains.push_back(chain);

	// Range rules, [font.N] first; `same` is the id chain.
	Common::Array<HiResRangeScope> faceScopes, advanceScopes;
	const HiResFontScope *scopes[2] = { own, &base };
	for (int s = 0; s < 2; ++s) {
		if (!scopes[s])
			continue;
		HiResRangeScope f, a;
		for (uint i = 0; i < scopes[s]->rangeSpecs.size(); ++i) {
			f.specs.push_back(scopes[s]->rangeSpecs[i]);
			f.values.push_back((int)addChain(plan.chains, expand(scopes[s]->rangeValues[i], plan.chains[0])));
		}
		for (uint i = 0; i < scopes[s]->advanceSpecs.size(); ++i) {
			a.specs.push_back(scopes[s]->advanceSpecs[i]);
			a.values.push_back((int)scopes[s]->advanceValues[i]);
		}
		faceScopes.push_back(f);
		advanceScopes.push_back(a);
	}
	plan.faceRules.compile(faceScopes);
	plan.advanceRules.compile(advanceScopes);
	return plan;
}

} // End of namespace Graphics
```

`graphics/module.mk`: `	hires_text/id_plan.o \` (after `font_map.o`).

- [ ] **Step 4: run** → `510 tests ... OK!`.
- [ ] **Step 5: commit**

```
GRAPHICS: Hi-res text: Compile a map into per font id plans

[font.N] over [font] over the defaults, resolved once: the id's face
chain with "same" expanded, every range rule's chain, and range tables
for chains and advances.

Assisted-by: Claude Code
```

## Task 10: Options (15 min)

- [ ] **Step 1: test** — `test/graphics/hires_text_hires_options.h`:

```cpp
#include <cxxtest/TestSuite.h>

#include "common/hash-str.h"
#include "common/hashmap.h"

#include "graphics/hires_text/hires_options.h"

namespace {

typedef Common::HashMap<Common::String, Common::String> OptionKeys;

bool getOptionKey(const char *key, Common::String &value, void *ctx) {
	const OptionKeys &keys = *(const OptionKeys *)ctx;
	if (!keys.contains(key))
		return false;
	value = keys[key];
	return true;
}

Graphics::HiResOptions parseOptions(OptionKeys &keys, Common::Array<Common::String> &w) {
	return Graphics::parseHiResOptions(getOptionKey, &keys, w);
}

} // End of anonymous namespace

class HiResTextHiResOptionsTestSuite : public CxxTest::TestSuite {
public:
	void test_defaults() {
		OptionKeys k;
		Common::Array<Common::String> w;
		const Graphics::HiResOptions o = parseOptions(k, w);
		TS_ASSERT(o.enabled);
		TS_ASSERT_EQUALS(o.font, Graphics::kHiResFontAuto);
		TS_ASSERT(!o.mapSet);
		TS_ASSERT_EQUALS(o.scale, 0);
		TS_ASSERT_EQUALS(o.blend, Graphics::kHiResBlendAuto);
		TS_ASSERT_EQUALS(o.target, Graphics::kHiResTargetAuto);
		TS_ASSERT(!o.hangulBreakSet);
		TS_ASSERT(!o.log);
		TS_ASSERT(w.empty());
	}

	void test_values() {
		OptionKeys k;
		k["hires_text"] = "false";
		k["hires_text_font"] = "Bitmap";
		k["hires_text_map"] = "";
		k["hires_text_scale"] = "1";
		k["hires_text_blend"] = "off";
		k["render_target"] = "rgb565";
		k["hangul_break"] = "any";
		k["hires_text_log"] = "true";
		Common::Array<Common::String> w;
		const Graphics::HiResOptions o = parseOptions(k, w);
		TS_ASSERT(!o.enabled);
		TS_ASSERT_EQUALS(o.font, Graphics::kHiResFontBitmap);
		TS_ASSERT(o.mapSet);
		TS_ASSERT(o.map.empty());
		TS_ASSERT_EQUALS(o.scale, 1);
		TS_ASSERT_EQUALS(o.blend, Graphics::kHiResBlendOff);
		TS_ASSERT_EQUALS(o.target, Graphics::kHiResTargetRgb565);
		TS_ASSERT_EQUALS(o.hangulBreak, Graphics::kHangulBreakAny);
		TS_ASSERT(o.log);
		TS_ASSERT(w.empty());
	}

	void test_badValues() {
		OptionKeys k;
		k["hires_text_scale"] = "3";
		k["render_target"] = "rgb555";
		Common::Array<Common::String> w;
		const Graphics::HiResOptions o = parseOptions(k, w);
		TS_ASSERT_EQUALS(o.scale, 0);
		TS_ASSERT_EQUALS(o.target, Graphics::kHiResTargetAuto);
		TS_ASSERT_EQUALS(w.size(), 2u);
		TS_ASSERT_EQUALS(w[0], "hires_text_scale '3' is not auto, 1 or 2; ignoring it");
	}

	void test_fontOrder() {
		Common::Array<Graphics::HiResFontKind> o = Graphics::hiResFontOrder(Graphics::kHiResFontAuto);
		TS_ASSERT_EQUALS(o.size(), 2u);
		TS_ASSERT_EQUALS(o[0], Graphics::kHiResFontSvf);
		TS_ASSERT_EQUALS(o[1], Graphics::kHiResFontBitmap);
		o = Graphics::hiResFontOrder(Graphics::kHiResFontBitmap);
		TS_ASSERT_EQUALS(o[0], Graphics::kHiResFontBitmap);
		TS_ASSERT_EQUALS(o[1], Graphics::kHiResFontSvf);
		TS_ASSERT_EQUALS(Graphics::hiResFontOrder(Graphics::kHiResFontTtf).size(), 2u);
	}

	void test_targets() {
		Common::List<Graphics::PixelFormat> formats;
		formats.push_back(Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0));
		formats.push_back(Graphics::PixelFormat::createFormatCLUT8());
		const uint32 offered = Graphics::hiResTargetsOffered(formats);
		TS_ASSERT_EQUALS(offered, (1u << Graphics::kHiResTargetClut8) | (1u << Graphics::kHiResTargetRgb565));
		Graphics::HiResOptions o;
		Common::String note;
		TS_ASSERT_EQUALS(Graphics::resolveRenderTarget(o, nullptr, false, offered, note), Graphics::kHiResTargetClut8);
		TS_ASSERT_EQUALS(Graphics::resolveRenderTarget(o, nullptr, true, offered, note), Graphics::kHiResTargetRgb565);
		o.blend = Graphics::kHiResBlendOff;
		TS_ASSERT_EQUALS(Graphics::resolveRenderTarget(o, nullptr, true, offered, note), Graphics::kHiResTargetClut8);
		o.target = Graphics::kHiResTargetRgb888;
		TS_ASSERT_EQUALS(Graphics::resolveRenderTarget(o, nullptr, true, offered, note), Graphics::kHiResTargetRgb565);
		TS_ASSERT_EQUALS(note, "render_target rgb888 is not offered by this backend; using rgb565");
		Graphics::HiResMap m;
		m.target = Graphics::kHiResTargetClut8;
		m.targetSet = true;
		o = Graphics::HiResOptions();
		TS_ASSERT_EQUALS(Graphics::resolveRenderTarget(o, &m, true, offered, note), Graphics::kHiResTargetClut8);
		TS_ASSERT(note.empty());
	}

	void test_modeAndScale() {
		TS_ASSERT_EQUALS(Graphics::composeMode(Graphics::kHiResTargetClut8, Graphics::kHiResBlendOn), Graphics::TextCompose::kStamp);
		TS_ASSERT_EQUALS(Graphics::composeMode(Graphics::kHiResTargetRgb565, Graphics::kHiResBlendOff), Graphics::TextCompose::kThreshold);
		TS_ASSERT_EQUALS(Graphics::composeMode(Graphics::kHiResTargetRgb888, Graphics::kHiResBlendAuto), Graphics::TextCompose::kBlend);
		Graphics::HiResOptions o;
		TS_ASSERT_EQUALS(Graphics::resolveScale(o, nullptr, 320, 200), 2);
		TS_ASSERT_EQUALS(Graphics::resolveScale(o, nullptr, 640, 480), 1);
		Graphics::HiResMap m;
		m.scale = 1;
		m.scaleSet = true;
		TS_ASSERT_EQUALS(Graphics::resolveScale(o, &m, 320, 200), 1);
		o.scale = 2;
		TS_ASSERT_EQUALS(Graphics::resolveScale(o, &m, 320, 200), 2);
	}
};
```

- [ ] **Step 2: run, expect a compile failure.**
- [ ] **Step 3: implement** — `graphics/hires_text/hires_options.h`:

```cpp
#ifndef GRAPHICS_HIRES_TEXT_HIRES_OPTIONS_H
#define GRAPHICS_HIRES_TEXT_HIRES_OPTIONS_H

#include "common/array.h"
#include "common/list.h"
#include "common/str.h"

#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/text_compose.h"
#include "graphics/pixelformat.h"

namespace Graphics {

/** hires_text_font: which of a game's maps is used. */
enum HiResFontKind { kHiResFontAuto = 0, kHiResFontSvf, kHiResFontBitmap, kHiResFontTtf };

/**
 * The configuration keys of hi-res text read from P1 on (DESIGN section 7):
 * hires_text, hires_text_font, hires_text_map, hires_text_scale,
 * hires_text_blend, render_target, hangul_break, hires_text_log.
 */
struct HiResOptions {
	HiResOptions();

	bool enabled;                  ///< hires_text (true)
	HiResFontKind font;            ///< hires_text_font (auto)
	Common::String map;            ///< hires_text_map; set and empty: no map
	bool mapSet;
	int scale;                     ///< hires_text_scale: 0 auto, 1, 2
	HiResBlend blend;              ///< hires_text_blend (auto)
	HiResRenderTarget target;      ///< render_target (auto)
	HangulBreak hangulBreak;       ///< hangul_break (word)
	bool hangulBreakSet;
	bool log;                      ///< hires_text_log (false)
};

/** One key lookup: true with @p value when @p key is set. */
typedef bool (*HiResConfigGetFn)(const char *key, Common::String &value, void *ctx);

/** Reads every key through @p get; a bad value is a warning in @p warnings and keeps the default. */
HiResOptions parseHiResOptions(HiResConfigGetFn get, void *ctx, Common::Array<Common::String> &warnings);
/** parseHiResOptions() over ConfMan: the transient, active game and application domains. */
HiResOptions readHiResOptions(Common::Array<Common::String> &warnings);

const char *hiResFontKindName(HiResFontKind kind);
/** HIRESVF.MAP, HIRESBIT.MAP, HIRESTTF.MAP. */
const char *hiResFontKindMap(HiResFontKind kind);
/** Whether this build draws @p kind's faces (P1: no TrueType). */
bool hiResFontKindBuilt(HiResFontKind kind);
/** @p preferred first (unless auto), then SVF, bitmap, TrueType; kinds the build cannot draw left out. */
Common::Array<HiResFontKind> hiResFontOrder(HiResFontKind preferred);

/** Bit (1 << target) for clut8, rgb565 and rgb888 when some format of @p formats is that target. */
uint32 hiResTargetsOffered(const Common::List<PixelFormat> &formats);
/** clut8 for CLUT8, rgb565 for 2 bytes 5-6-5, rgb888 for 4 bytes 8-8-8; auto for anything else. */
HiResRenderTarget hiResTargetOf(const PixelFormat &format);

/**
 * The screen hi-res text draws on: render_target unless auto, else the map's
 * target unless auto, else rgb (rgb888, then rgb565) when a face has coverage,
 * blending is not off and the backend offers it, else clut8. A wanted target
 * the backend does not offer falls back rgb888 -> rgb565 -> clut8 with a note.
 * @p offered: hiResTargetsOffered() bits.
 */
HiResRenderTarget resolveRenderTarget(const HiResOptions &options, const HiResMap *map, bool anyCoverage, uint32 offered,
                                      Common::String &note);
/** hires_text_blend unless auto, else the map's blend. */
HiResBlend resolveBlend(const HiResOptions &options, const HiResMap *map);
/** kStamp on clut8; kThreshold with blend off; kBlend otherwise. */
TextCompose::Mode composeMode(HiResRenderTarget target, HiResBlend blend);
/** hires_text_scale unless auto, else the map's scale unless auto, else 2 up to 320x240 and 1 above. */
int resolveScale(const HiResOptions &options, const HiResMap *map, int gameWidth, int gameHeight);

} // End of namespace Graphics

#endif
```

`graphics/hires_text/hires_options.cpp`:

```cpp
#include "graphics/hires_text/hires_options.h"

#include "common/config-manager.h"

namespace Graphics {

HiResOptions::HiResOptions()
	: enabled(true), font(kHiResFontAuto), mapSet(false), scale(0), blend(kHiResBlendAuto), target(kHiResTargetAuto),
	  hangulBreak(kHangulBreakWord), hangulBreakSet(false), log(false) {
}

namespace {

void bad(Common::Array<Common::String> &warnings, const char *key, const Common::String &value, const char *allowed) {
	warnings.push_back(Common::String::format("%s '%s' is not %s; ignoring it", key, value.c_str(), allowed));
}

bool confManGet(const char *key, Common::String &value, void *) {
	if (!ConfMan.hasKey(key))
		return false;
	value = ConfMan.get(key);
	return true;
}

} // End of anonymous namespace

HiResOptions parseHiResOptions(HiResConfigGetFn get, void *ctx, Common::Array<Common::String> &warnings) {
	HiResOptions o;
	Common::String v;
	bool b;
	if (get("hires_text", v, ctx)) {
		if (HiResFontMap::parseBool(v, b))
			o.enabled = b;
		else
			bad(warnings, "hires_text", v, "true or false");
	}
	if (get("hires_text_font", v, ctx)) {
		static const char *const names[] = { "auto", "svf", "bitmap", "ttf" };
		bool found = false;
		for (int i = 0; i < 4 && !found; ++i)
			if (v.equalsIgnoreCase(names[i])) {
				o.font = (HiResFontKind)i;
				found = true;
			}
		if (!found)
			bad(warnings, "hires_text_font", v, "auto, svf, bitmap or ttf");
	}
	if (get("hires_text_map", v, ctx)) {
		o.map = v;
		o.mapSet = true;
	}
	if (get("hires_text_scale", v, ctx)) {
		if (v.equalsIgnoreCase("auto"))
			o.scale = 0;
		else if (v == "1" || v == "2")
			o.scale = v[0] - '0';
		else
			bad(warnings, "hires_text_scale", v, "auto, 1 or 2");
	}
	if (get("hires_text_blend", v, ctx) && !HiResFontMap::parseBlend(v, o.blend))
		bad(warnings, "hires_text_blend", v, "auto, on or off");
	if (get("render_target", v, ctx) && !HiResFontMap::parseRenderTarget(v, o.target))
		bad(warnings, "render_target", v, "auto, clut8, rgb565 or rgb888");
	if (get("hangul_break", v, ctx)) {
		if (v.equalsIgnoreCase("word") || v.equalsIgnoreCase("any")) {
			o.hangulBreak = v.equalsIgnoreCase("any") ? kHangulBreakAny : kHangulBreakWord;
			o.hangulBreakSet = true;
		} else
			bad(warnings, "hangul_break", v, "word or any");
	}
	if (get("hires_text_log", v, ctx)) {
		if (HiResFontMap::parseBool(v, b))
			o.log = b;
		else
			bad(warnings, "hires_text_log", v, "true or false");
	}
	return o;
}

HiResOptions readHiResOptions(Common::Array<Common::String> &warnings) {
	return parseHiResOptions(confManGet, nullptr, warnings);
}

const char *hiResFontKindName(HiResFontKind kind) {
	static const char *const names[] = { "auto", "svf", "bitmap", "ttf" };
	return names[kind];
}

const char *hiResFontKindMap(HiResFontKind kind) {
	switch (kind) {
	case kHiResFontSvf:
		return "HIRESVF.MAP";
	case kHiResFontBitmap:
		return "HIRESBIT.MAP";
	case kHiResFontTtf:
		return "HIRESTTF.MAP";
	default:
		return "";
	}
}

bool hiResFontKindBuilt(HiResFontKind kind) {
	return kind == kHiResFontSvf || kind == kHiResFontBitmap;
}

Common::Array<HiResFontKind> hiResFontOrder(HiResFontKind preferred) {
	Common::Array<HiResFontKind> order;
	const HiResFontKind all[] = { preferred, kHiResFontSvf, kHiResFontBitmap, kHiResFontTtf };
	for (int i = 0; i < 4; ++i) {
		bool seen = false;
		for (uint j = 0; j < order.size(); ++j)
			seen = seen || order[j] == all[i];
		if (all[i] != kHiResFontAuto && !seen && hiResFontKindBuilt(all[i]))
			order.push_back(all[i]);
	}
	return order;
}

HiResRenderTarget hiResTargetOf(const PixelFormat &f) {
	if (f.isCLUT8())
		return kHiResTargetClut8;
	if (f.bytesPerPixel == 2 && f.rBits() == 5 && f.gBits() == 6 && f.bBits() == 5)
		return kHiResTargetRgb565;
	if (f.bytesPerPixel == 4 && f.rBits() == 8 && f.gBits() == 8 && f.bBits() == 8)
		return kHiResTargetRgb888;
	return kHiResTargetAuto;
}

uint32 hiResTargetsOffered(const Common::List<PixelFormat> &formats) {
	uint32 bits = 0;
	for (Common::List<PixelFormat>::const_iterator it = formats.begin(); it != formats.end(); ++it) {
		const HiResRenderTarget t = hiResTargetOf(*it);
		if (t != kHiResTargetAuto)
			bits |= 1u << t;
	}
	return bits;
}

HiResBlend resolveBlend(const HiResOptions &options, const HiResMap *map) {
	if (options.blend != kHiResBlendAuto)
		return options.blend;
	return map && map->blendSet ? map->blend : kHiResBlendAuto;
}

HiResRenderTarget resolveRenderTarget(const HiResOptions &options, const HiResMap *map, bool anyCoverage, uint32 offered,
                                      Common::String &note) {
	note.clear();
	HiResRenderTarget want = options.target;
	if (want == kHiResTargetAuto && map && map->targetSet)
		want = map->target;
	offered |= 1u << kHiResTargetClut8;
	if (want == kHiResTargetAuto) {
		if (!anyCoverage || resolveBlend(options, map) == kHiResBlendOff)
			return kHiResTargetClut8;
		if (offered & (1u << kHiResTargetRgb888))
			return kHiResTargetRgb888;
		return (offered & (1u << kHiResTargetRgb565)) ? kHiResTargetRgb565 : kHiResTargetClut8;
	}
	if (offered & (1u << want))
		return want;
	HiResRenderTarget got = kHiResTargetClut8;
	if (offered & (1u << kHiResTargetRgb888))
		got = kHiResTargetRgb888;
	else if (offered & (1u << kHiResTargetRgb565))
		got = kHiResTargetRgb565;
	note = Common::String::format("render_target %s is not offered by this backend; using %s",
	                              want == kHiResTargetRgb565 ? "rgb565" : "rgb888",
	                              got == kHiResTargetClut8 ? "clut8" : got == kHiResTargetRgb565 ? "rgb565" : "rgb888");
	return got;
}

TextCompose::Mode composeMode(HiResRenderTarget target, HiResBlend blend) {
	if (target == kHiResTargetClut8)
		return TextCompose::kStamp;
	return blend == kHiResBlendOff ? TextCompose::kThreshold : TextCompose::kBlend;
}

int resolveScale(const HiResOptions &options, const HiResMap *map, int gameWidth, int gameHeight) {
	if (options.scale)
		return options.scale;
	if (map && map->scaleSet && map->scale)
		return map->scale;
	return (gameWidth <= 320 && gameHeight <= 240) ? 2 : 1;
}

} // End of namespace Graphics
```

`graphics/module.mk`: `	hires_text/hires_options.o \` (after `hires_text/font_map.o`; alphabetical among `hires_text/`).

- [ ] **Step 4: run** → `516 tests ... OK!`.
- [ ] **Step 5: commit**

```
GRAPHICS: Hi-res text: Read the configuration keys

hires_text, hires_text_font, hires_text_map, hires_text_scale,
hires_text_blend, render_target, hangul_break and hires_text_log,
through a lookup callback (ConfMan in the game, a table in tests), and
the rules that turn them and the map into a screen target, a blend
mode and a scale.

Assisted-by: Claude Code
```

## Task 11: Ship the fallback face and the default map (10 min)

- [ ] **Step 1:** extend `tools/korean/sync-svf.sh` (a fork file). After the `files=...` line add:

```sh
# The engine's fallback face (DESIGN decision 3), copied into engine data with its notice.
faces="svf/dos/NEODG16A.SVF svf/dos/NEODGM.TXT"
facedst="$root/dists/engine-data/hires_text"
```

add `$faces` to the dirty check (`git -C "$src" status --porcelain -- c/include c/src LICENSE $faces`); in the copy
branch, after the library loop:

```sh
	mkdir -p "$facedst"
	for f in $faces; do
		cp "$src/$f" "$facedst/$(basename "$f")"
	done
```

and in `--check`, after the library loop:

```sh
	for f in $faces; do
		cmp -s "$src/$f" "$facedst/$(basename "$f")" || { echo "sync-svf: $facedst/$(basename "$f") differs from $f" >&2; bad=1; }
	done
```

Update the usage comment at the top ("... into graphics/svf, and the fallback face into dists/engine-data/hires_text").
`VERSION` is unchanged (it lists library files only).

- [ ] **Step 2: run**

```sh
cd $SRC && tools/korean/sync-svf.sh && tools/korean/sync-svf.sh --check   # "matches svf-fonts-open 3d40de8..."
ls -l dists/engine-data/hires_text/      # NEODG16A.SVF 16408, NEODGM.TXT
git status --short graphics/svf           # empty: the library did not change
```

- [ ] **Step 3:** `dists/engine-data/hires_text/maps/korean-default.map`:

```ini
; Hi-res text map: Korean in Neo Dunggeunmo (SIL OFL 1.1, svf-fonts-open svf/dos/NEODG16A.SVF,
; licence NEODGM.TXT), a 16 px 1 bpp jamo face with every Hangul syllable, at 2x the game screen.
;
; The engine's last resort and a starting point for a pack's own HIRESBIT.MAP: copy it into the
; game's ko/ folder under that name. "data:" is looked up in the extrapaths, then the ScummVM
; data folder (dists/engine-data in a source build).
[map]
version=3

[render]
scale=auto
blend=auto

[fonts]
neodgm=data:hires_text/NEODG16A.SVF

[font]
face=neodgm
missing=u+25a1
```

- [ ] **Step 4:** `LICENSES/COPYING.OFL`: after the `test/graphics/svf_fixture.h` entry (lines 20-21) add

```
 * dists/engine-data/hires_text/NEODG16A.SVF (Neo Dunggeunmo jamo face, Copyright (c) 2017-2021,
   Eunbin Jeong (Dalgona.); see dists/engine-data/hires_text/NEODGM.TXT)
```

- [ ] **Step 5: commit** (no new test here; Task 12 loads both files)

```
DISTS: Add the Korean fallback face and the default hi-res text map

NEODG16A.SVF, the 16 pixel Neo Dunggeunmo jamo face (SIL OFL 1.1),
copied from svf-fonts-open by tools/korean/sync-svf.sh, and a version 3
map that draws every font id with it. Neither is installed yet.

Assisted-by: Claude Code
```

## Task 12: Files and `data:` roots (15 min)

- [ ] **Step 1: tests** — `test/graphics/hires_support.h` (helpers, not a suite):

```cpp
#ifndef TEST_GRAPHICS_HIRES_SUPPORT_H
#define TEST_GRAPHICS_HIRES_SUPPORT_H

#include "common/hash-str.h"
#include "common/hashmap.h"
#include "common/memstream.h"
#include "common/str.h"

#include "graphics/hires_text/hires_files.h"
#include "graphics/hires_text/text_plane.h"

#include "test/graphics/svf_fixture.h"

namespace HiResTest {

// Files held in memory, by path ("game/ko/HIRESVF.MAP"); names matched ignoring case.
class MemoryFiles : public Graphics::HiResFiles {
public:
	MemoryFiles() : opens(0) {
		addBytes("data/fixture/MARKS.SVF", kSvfFixtureMarks, sizeof(kSvfFixtureMarks));
		addBytes("data/fixture/JAMO.SVF", kSvfFixtureJamo, sizeof(kSvfFixtureJamo));
	}

	void addText(const char *path, const char *text) {
		addBytes(path, (const byte *)text, strlen(text));
	}
	void addBytes(const char *path, const byte *data, uint32 size) {
		Entry e;
		e.data = data;
		e.size = size;
		_files[Common::String(path)] = e;
	}

	Common::Path find(const Common::Path &dir, const Common::String &name) override {
		const Common::String want = dir.join(Common::Path(name, '/')).toString('/');
		for (Map::const_iterator it = _files.begin(); it != _files.end(); ++it)
			if (it->_key.equalsIgnoreCase(want))
				return Common::Path(it->_key, '/');
		return Common::Path();
	}
	Common::Path locate(const Graphics::HiResFaceRef &ref) override {
		const Common::Path p = ref.data ? Common::Path("data").join(ref.path) : ref.path;
		return _files.contains(p.toString('/')) ? p : Common::Path();
	}
	Common::SeekableReadStream *open(const Common::Path &path) override {
		Map::const_iterator it = _files.find(path.toString('/'));
		if (it == _files.end())
			return nullptr;
		++opens;
		return new Common::MemoryReadStream(it->_value.data, it->_value.size);
	}

	int opens;

private:
	struct Entry {
		const byte *data;
		uint32 size;
	};
	typedef Common::HashMap<Common::String, Entry> Map;
	Map _files;
};

// The coverage plane of @p plane as w x h bytes (for the golden rows).
inline Common::Array<byte> coverageOf(const Graphics::TextPlane &plane) {
	Common::Array<byte> out;
	out.resize(plane.getWidth() * plane.getHeight());
	for (int y = 0; y < plane.getHeight(); ++y)
		for (int x = 0; x < plane.getWidth(); ++x)
			out[y * plane.getWidth() + x] = plane.getCoverage(x, y);
	return out;
}

} // End of namespace HiResTest

#endif
```

and `test/graphics/hires_text_shipped_maps.h` (Task 14 adds a fourth test that draws with the shipped face):

```cpp
#include <cxxtest/TestSuite.h>

#include "common/fs.h"
#include "common/ptr.h"
#include "common/stream.h"

#include "graphics/hires_text/hires_files.h"

#include "test/system/null_osystem.h"

class HiResTextShippedMapsTestSuite : public CxxTest::TestSuite {
	// test/graphics/<this file> -> the source tree
	static Common::Path tree() {
		return Common::Path(__FILE__, '/').getParent().getParent().getParent();
	}

	// Every .map in @p dir loads without a warning for every qualifier set; returns how many there were.
	int checkDir(const Common::Path &dir) {
		Common::FSList files;
		if (!Common::FSNode(dir).getChildren(files, Common::FSNode::kListFilesOnly))
			return 0;
		int n = 0;
		const char *engines[] = { "", "sci", "scumm", "agi" };
		const Graphics::HiResRenderTarget targets[] = { Graphics::kHiResTargetAuto, Graphics::kHiResTargetClut8,
		                                                Graphics::kHiResTargetRgb565 };
		for (uint i = 0; i < files.size(); ++i) {
			const Common::String name = files[i].getName();
			if (!name.hasSuffixIgnoreCase(".map"))
				continue;
			++n;
			for (int e = 0; e < 4; ++e) {
				for (int t = 0; t < 3; ++t) {
					Common::ScopedPtr<Common::SeekableReadStream> s(files[i].createReadStream());
					Graphics::HiResMapQualifiers q;
					q.engine = engines[e];
					q.target = targets[t];
					Graphics::HiResMap m;
					TSM_ASSERT(name.c_str(), s && Graphics::HiResFontMap::load(*s, name, dir, q, m));
					for (uint w = 0; w < m.warnings.size(); ++w)
						TS_FAIL(m.warnings[w].c_str());
				}
			}
		}
		return n;
	}

public:
	void setUp() override {
		Common::install_null_g_system();
	}

	void tearDown() override {
		Common::uninstall_null_g_system();
	}

	void test_shippedMapsAreVersion3AndClean() {
		TS_ASSERT(checkDir(tree().join("dists/engine-data/hires_text/maps")) >= 1);
	}

	// Developer aid: maps converted for the packs (harness), when a directory or link hires-maps is in the working directory.
	void test_packMapsWhenGiven() {
		if (Common::FSNode(Common::Path("hires-maps")).isDirectory())
			TS_ASSERT(checkDir(Common::Path("hires-maps")) >= 1);
	}

	void test_diskFiles() {
		Graphics::HiResDiskFiles files;
		const Common::Path data = tree().join("dists/engine-data");
		files.addDataRoot(data);
		TS_ASSERT_EQUALS(files.find(data, "HIRES_TEXT/maps/KOREAN-DEFAULT.MAP"), data.join("hires_text/maps/korean-default.map"));
		TS_ASSERT(files.find(data, "hires_text/maps/none.map").empty());
		const Graphics::HiResFaceRef ref = Graphics::HiResFontMap::resolveFace("data:hires_text/NEODG16A.SVF", Common::Path());
		TS_ASSERT_EQUALS(files.locate(ref), data.join("hires_text/NEODG16A.SVF"));
		TS_ASSERT(files.locate(Graphics::HiResFontMap::resolveFace("data:../engine-data/hires_text/NEODG16A.SVF", Common::Path())).empty());
		TS_ASSERT(!Graphics::HiResDiskFiles::isSafeDataPath(Common::Path("/etc/passwd")));
		TS_ASSERT(Graphics::HiResDiskFiles::isSafeDataPath(Common::Path("hires_text/X.SVF")));
	}
};
```

- [ ] **Step 2: run, expect a compile failure** (`hires_files.h` missing).
- [ ] **Step 3: implement** — `graphics/hires_text/hires_files.h`:

```cpp
#ifndef GRAPHICS_HIRES_TEXT_HIRES_FILES_H
#define GRAPHICS_HIRES_TEXT_HIRES_FILES_H

#include "common/array.h"
#include "common/path.h"
#include "common/str.h"

#include "graphics/hires_text/font_map.h"

namespace Common {
class SeekableReadStream;
}

namespace Graphics {

/** How hi-res text reaches its files: maps, faces. Tests give their own. */
class HiResFiles {
public:
	virtual ~HiResFiles() {}
	/** The file @p name ("X.MAP", "ko/X.MAP") under @p dir, each component matched ignoring case; empty if none. */
	virtual Common::Path find(const Common::Path &dir, const Common::String &name) = 0;
	/** Where @p ref is: a data: reference through the data folders; empty if it is nowhere. */
	virtual Common::Path locate(const HiResFaceRef &ref) = 0;
	/** @p path (from find() or locate()) opened for reading; nullptr if it cannot be. */
	virtual Common::SeekableReadStream *open(const Common::Path &path) = 0;
};

/**
 * The file system: find() lists @p dir; locate() takes a data: reference in
 * each of the extrapaths (command line, game, application), then DATA_PATH,
 * then the roots given to addDataRoot(), and finally SearchMan (the path
 * then starts with "searchman:").
 */
class HiResDiskFiles : public HiResFiles {
public:
	HiResDiskFiles();
	void addDataRoot(const Common::Path &dir) { _roots.push_back(dir); }
	const Common::Array<Common::Path> &getDataRoots() const { return _roots; }

	Common::Path find(const Common::Path &dir, const Common::String &name) override;
	Common::Path locate(const HiResFaceRef &ref) override;
	Common::SeekableReadStream *open(const Common::Path &path) override;

	/** "data:" part @p relative is safe: not empty, not absolute, no "..". */
	static bool isSafeDataPath(const Common::Path &relative);

private:
	Common::Array<Common::Path> _roots;
};

/**
 * The map file @p name for a game: a data: or absolute value as it says; a
 * bare name in the language folder @p langFolder (ko) of @p gameDir first,
 * then in @p gameDir. Empty if there is none.
 */
Common::Path findGameMapFile(HiResFiles &files, const Common::Path &gameDir, const Common::String &langFolder,
                             const Common::String &name);

} // End of namespace Graphics

#endif
```

`graphics/hires_text/hires_files.cpp`:

```cpp
#include "graphics/hires_text/hires_files.h"

#include "common/archive.h"
#include "common/config-manager.h"
#include "common/fs.h"

namespace Graphics {

namespace {

const char *const kSearchManPrefix = "searchman:";

void addRoot(Common::Array<Common::Path> &roots, const Common::Path &p) {
	if (p.empty())
		return;
	for (uint i = 0; i < roots.size(); ++i)
		if (roots[i] == p)
			return;
	roots.push_back(p);
}

} // End of anonymous namespace

HiResDiskFiles::HiResDiskFiles() {
	if (ConfMan.hasKey("extrapath", Common::ConfigManager::kTransientDomain))
		addRoot(_roots, ConfMan.getPath("extrapath", Common::ConfigManager::kTransientDomain));
	if (!ConfMan.getActiveDomainName().empty() && ConfMan.hasKey("extrapath", ConfMan.getActiveDomainName()))
		addRoot(_roots, ConfMan.getPath("extrapath", ConfMan.getActiveDomainName()));
	if (ConfMan.hasKey("extrapath", Common::ConfigManager::kApplicationDomain))
		addRoot(_roots, ConfMan.getPath("extrapath", Common::ConfigManager::kApplicationDomain));
#ifdef DATA_PATH
	addRoot(_roots, Common::Path(DATA_PATH));
#endif
}

bool HiResDiskFiles::isSafeDataPath(const Common::Path &relative) {
	const Common::String s = relative.toString('/');
	if (s.empty() || s[0] == '/' || s[0] == '\\' || (s.size() > 1 && s[1] == ':'))
		return false;
	const Common::StringArray parts = relative.splitComponents();
	for (uint i = 0; i < parts.size(); ++i)
		if (parts[i] == "..")
			return false;
	return true;
}

Common::Path HiResDiskFiles::find(const Common::Path &dir, const Common::String &name) {
	// Each component of @p name ("ko/HIRESVF.MAP") is matched ignoring case.
	Common::FSNode node(dir);
	const Common::StringArray parts = Common::Path(name, '/').splitComponents();
	for (uint c = 0; c < parts.size(); ++c) {
		Common::FSList list;
		const bool last = c + 1 == parts.size();
		if (!node.isDirectory() || !node.getChildren(list, last ? Common::FSNode::kListFilesOnly : Common::FSNode::kListDirectoriesOnly))
			return Common::Path();
		uint i = 0;
		while (i < list.size() && !list[i].getName().equalsIgnoreCase(parts[c]))
			++i;
		if (i == list.size())
			return Common::Path();
		node = list[i];
	}
	return parts.empty() ? Common::Path() : node.getPath();
}

Common::Path HiResDiskFiles::locate(const HiResFaceRef &ref) {
	if (!ref.data)
		return Common::FSNode(ref.path).exists() ? ref.path : Common::Path();
	if (!isSafeDataPath(ref.path))
		return Common::Path();
	for (uint i = 0; i < _roots.size(); ++i) {
		const Common::Path p = _roots[i].join(ref.path);
		if (Common::FSNode(p).exists())
			return p;
	}
	if (SearchMan.hasFile(ref.path))
		return Common::Path(kSearchManPrefix).appendInPlace(ref.path);
	return Common::Path();
}

Common::SeekableReadStream *HiResDiskFiles::open(const Common::Path &path) {
	const Common::String s = path.toString('/');
	if (s.hasPrefix(kSearchManPrefix))
		return SearchMan.createReadStreamForMember(Common::Path(s.c_str() + strlen(kSearchManPrefix), '/'));
	Common::FSNode node(path);
	return node.exists() && !node.isDirectory() ? node.createReadStream() : nullptr;
}

Common::Path findGameMapFile(HiResFiles &files, const Common::Path &gameDir, const Common::String &langFolder,
                             const Common::String &name) {
	if (name.empty())
		return Common::Path();
	if (name.hasPrefix(HiResFontMap::kDataPrefix) || name[0] == '/' || name[0] == '\\' || (name.size() > 1 && name[1] == ':'))
		return files.locate(HiResFontMap::resolveFace(name, gameDir));
	if (name.contains('/'))
		return files.locate(HiResFontMap::resolveFace(name, gameDir));
	if (!langFolder.empty()) {
		const Common::Path inLang = files.find(gameDir, langFolder + "/" + name);
		if (!inLang.empty())
			return inLang;
	}
	return files.find(gameDir, name);
}

} // End of namespace Graphics
```

`graphics/module.mk`: `	hires_text/hires_files.o \` (before `hires_options.o`).

- [ ] **Step 4: run** → `519 tests ... OK!`. Then the pack-map aid:
  `cd $BLD && mkdir -p hires-maps && cp $SRC/dists/engine-data/hires_text/maps/korean-default.map hires-maps/ &&
  ./test/runner HiResTextShippedMapsTestSuite test_packMapsWhenGiven && rm -r hires-maps` → `OK!`.
- [ ] **Step 5: commit**

```
GRAPHICS: Hi-res text: Find maps and faces

HiResFiles is how hi-res text reaches its files; HiResDiskFiles finds a
name in a folder ignoring case, and a data: path in the extrapaths,
DATA_PATH and SearchMan, refusing ".." and absolute paths. A game's map
is looked for in its language folder (ko/) first.

Assisted-by: Claude Code
```

## Task 13: Map picking (15 min)

- [ ] **Step 1: test** — `test/graphics/hires_text_map_pick.h`:

```cpp
#include <cxxtest/TestSuite.h>

#include "graphics/hires_text/map_pick.h"

#include "test/graphics/hires_support.h"

namespace {

const char kSvfMap[] = "[map]\nversion=3\n[fonts]\nko=data:fixture/JAMO.SVF\n[font]\nface=ko\n";
const char kBitMap[] = "[map]\nversion=3\n[fonts]\nko=data:fixture/MARKS.SVF\n[font]\nface=ko\n";
const char kGoneMap[] = "[map]\nversion=3\n[fonts]\nko=data:fonts/GONE.SVF\n[font]\nface=ko\n";
const char kOldMap[] = "[map]\nversion=2\n";

} // End of anonymous namespace

class HiResTextMapPickTestSuite : public CxxTest::TestSuite {
	bool pick(HiResTest::MemoryFiles &files, const Graphics::HiResOptions &o, Graphics::HiResPick &out,
	          Common::Array<Common::String> &notes) {
		return Graphics::pickHiResMap(o, Common::Path("game"), "ko", Graphics::HiResMapQualifiers(), files, out, notes);
	}

public:
	void test_order() {
		HiResTest::MemoryFiles files;
		files.addText("game/ko/HIRESBIT.MAP", kBitMap);
		files.addText("game/HiResVF.map", kSvfMap);
		Graphics::HiResOptions o;
		Graphics::HiResPick p;
		Common::Array<Common::String> notes;
		TS_ASSERT(pick(files, o, p, notes));
		TS_ASSERT_EQUALS(p.kind, Graphics::kHiResFontSvf);
		TS_ASSERT_EQUALS(p.path.toString('/'), "game/HiResVF.map");
		o.font = Graphics::kHiResFontBitmap;
		TS_ASSERT(pick(files, o, p, notes));
		TS_ASSERT_EQUALS(p.kind, Graphics::kHiResFontBitmap);
		TS_ASSERT_EQUALS(p.path.toString('/'), "game/ko/HIRESBIT.MAP");
	}

	void test_languageFolderFirst() {
		HiResTest::MemoryFiles files;
		files.addText("game/HIRESVF.MAP", kOldMap);
		files.addText("game/KO/HIRESVF.MAP", kSvfMap);
		Graphics::HiResOptions o;
		Graphics::HiResPick p;
		Common::Array<Common::String> notes;
		TS_ASSERT(pick(files, o, p, notes));
		TS_ASSERT_EQUALS(p.path.toString('/'), "game/KO/HIRESVF.MAP");
	}

	void test_skips() {
		HiResTest::MemoryFiles files;
		files.addText("game/HIRESVF.MAP", kGoneMap);
		files.addText("game/HIRESBIT.MAP", kOldMap);
		Graphics::HiResOptions o;
		Graphics::HiResPick p;
		Common::Array<Common::String> notes;
		TS_ASSERT(!pick(files, o, p, notes));
		TS_ASSERT_EQUALS(notes.size(), 2u);
		TS_ASSERT_EQUALS(notes[0], "svf map HIRESVF.MAP: face ko is missing");
		TS_ASSERT(notes[1].hasPrefix("bitmap map HIRESBIT.MAP: does not load: HIRESBIT.MAP:1: map version 2 is not read"));
	}

	void test_namedMap() {
		HiResTest::MemoryFiles files;
		files.addText("game/HIRESVF.MAP", kSvfMap);
		files.addText("game/ko/MINE.MAP", kBitMap);
		Graphics::HiResOptions o;
		o.mapSet = true;
		o.map = "mine.map";
		Graphics::HiResPick p;
		Common::Array<Common::String> notes;
		TS_ASSERT(pick(files, o, p, notes));
		TS_ASSERT_EQUALS(p.path.toString('/'), "game/ko/MINE.MAP");
		TS_ASSERT_EQUALS(p.kind, Graphics::kHiResFontAuto);
		o.map.clear();
		TS_ASSERT(!pick(files, o, p, notes));
		o = Graphics::HiResOptions();
		o.enabled = false;
		TS_ASSERT(!pick(files, o, p, notes));
	}
};
```

- [ ] **Step 2: run, expect a compile failure.**
- [ ] **Step 3: implement** — `graphics/hires_text/map_pick.h`:

```cpp
#ifndef GRAPHICS_HIRES_TEXT_MAP_PICK_H
#define GRAPHICS_HIRES_TEXT_MAP_PICK_H

#include "common/array.h"
#include "common/path.h"
#include "common/str.h"

#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/hires_options.h"

namespace Graphics {

class HiResFiles;

/** The map a game draws hi-res text with. */
struct HiResPick {
	HiResPick() : kind(kHiResFontAuto) {}
	Common::Path path;
	HiResFontKind kind;  ///< auto: named by hires_text_map
	HiResMap map;
};

/**
 * Picks the map: hires_text_map when set (an empty value: no map), else the
 * first kind of hiResFontOrder(hires_text_font) whose map is in the game's
 * language folder or game folder, loads, and names at least one face that
 * locate() finds in each of its font values. Each kind passed over leaves a
 * line in @p notes. False when no map is left (or hires_text is off).
 */
bool pickHiResMap(const HiResOptions &options, const Common::Path &gameDir, const Common::String &langFolder,
                  const HiResMapQualifiers &qualifiers, HiResFiles &files, HiResPick &out,
                  Common::Array<Common::String> &notes);

/** Whether a value names no file (only original) or one @p files locates. */
bool hiResValueAvailable(const HiResFontValue &value, HiResFiles &files);

} // End of namespace Graphics

#endif
```

`graphics/hires_text/map_pick.cpp`:

```cpp
#include "graphics/hires_text/map_pick.h"

#include "common/ptr.h"
#include "common/stream.h"

#include "graphics/hires_text/hires_files.h"

namespace Graphics {

bool hiResValueAvailable(const HiResFontValue &value, HiResFiles &files) {
	for (uint i = 0; i < value.faces.size(); ++i)
		if (!files.locate(value.faces[i]).empty())
			return true;
	return value.faces.empty();
}

namespace {

bool scopeAvailable(const HiResFontScope &s, HiResFiles &files, Common::String &missing) {
	if (s.faceSet && !hiResValueAvailable(s.face, files)) {
		missing = s.face.faces[0].name;
		return false;
	}
	for (uint i = 0; i < s.rangeValues.size(); ++i) {
		if (!hiResValueAvailable(s.rangeValues[i], files)) {
			missing = s.rangeValues[i].faces[0].name;
			return false;
		}
	}
	return true;
}

bool loadPath(HiResFiles &files, const Common::Path &path, const Common::String &name, const HiResMapQualifiers &q,
              HiResMap &map) {
	Common::ScopedPtr<Common::SeekableReadStream> s(files.open(path));
	return s && HiResFontMap::load(*s, name, path.getParent(), q, map);
}

} // End of anonymous namespace

bool pickHiResMap(const HiResOptions &options, const Common::Path &gameDir, const Common::String &langFolder,
                  const HiResMapQualifiers &qualifiers, HiResFiles &files, HiResPick &out,
                  Common::Array<Common::String> &notes) {
	if (!options.enabled) {
		notes.push_back("hires_text is off");
		return false;
	}
	if (options.mapSet) {
		if (options.map.empty()) {
			notes.push_back("hires_text_map is empty: no map");
			return false;
		}
		out.path = findGameMapFile(files, gameDir, langFolder, options.map);
		out.kind = kHiResFontAuto;
		if (out.path.empty()) {
			notes.push_back("hires_text_map " + options.map + ": not found");
			return false;
		}
		if (!loadPath(files, out.path, options.map, qualifiers, out.map)) {
			notes.push_back("hires_text_map " + options.map + ": does not load");
			notes.push_back(out.map.warnings);
			return false;
		}
		return true;
	}
	if (options.font == kHiResFontTtf)
		notes.push_back("hires_text_font=ttf: this build draws no TrueType hi-res text");
	const Common::Array<HiResFontKind> order = hiResFontOrder(options.font);
	for (uint i = 0; i < order.size(); ++i) {
		const char *name = hiResFontKindMap(order[i]);
		const Common::Path path = findGameMapFile(files, gameDir, langFolder, name);
		if (path.empty()) {
			notes.push_back(Common::String::format("%s map %s: not in the game folder", hiResFontKindName(order[i]), name));
			continue;
		}
		HiResMap map;
		if (!loadPath(files, path, name, qualifiers, map)) {
			notes.push_back(Common::String::format("%s map %s: does not load%s%s", hiResFontKindName(order[i]), name,
			                                       map.warnings.empty() ? "" : ": ",
			                                       map.warnings.empty() ? "" : map.warnings[0].c_str()));
			continue;
		}
		Common::String missing;
		bool ok = scopeAvailable(map.font, files, missing);
		for (Common::HashMap<int, HiResFontScope>::const_iterator it = map.fontIds.begin(); ok && it != map.fontIds.end(); ++it)
			ok = scopeAvailable(it->_value, files, missing);
		if (!ok) {
			notes.push_back(Common::String::format("%s map %s: face %s is missing", hiResFontKindName(order[i]), name,
			                                       missing.c_str()));
			continue;
		}
		out.path = path;
		out.kind = order[i];
		out.map = map;
		return true;
	}
	return false;
}

} // End of namespace Graphics
```

`graphics/module.mk`: `	hires_text/map_pick.o \` (after `id_plan.o`).

- [ ] **Step 4: run** → `523 tests ... OK!`.
- [ ] **Step 5: commit**

```
GRAPHICS: Hi-res text: Pick a game's map

hires_text_map when set; otherwise HIRESVF.MAP, then HIRESBIT.MAP (or
the kind hires_text_font names first), each from the language folder
before the game folder, skipping a map that does not load or whose
faces are not there. TrueType maps are not drawn by this build.

Assisted-by: Claude Code
```

### Review checkpoint C (after Task 13)

superpowers:requesting-code-review on Tasks 7-13. Focus: the parser never reads past a line and refuses only what
section 1 item 6 says; warnings carry `NAME:line:`; qualifier merge order; `same`/`original` expansion in `id_plan`
(`same` inside a chain that ends in `original`); `data:` safety (`..`, absolute, drive letters); `HiResDiskFiles` on a
case-sensitive file system (`ko` vs `KO`); no `warning()` call in the parser (callers print `map.warnings` once).

## Task 14: Font sets: open faces, draw into the plane (20 min)

- [ ] **Step 1: tests** — `test/graphics/hires_text_font_set.h`:

```cpp
#include <cxxtest/TestSuite.h>

#include "graphics/hires_text/font_set.h"
#include "graphics/hires_text/id_plan.h"
#include "graphics/hires_text/svfchain.h"
#include "graphics/hires_text/text_plane.h"

#include "test/graphics/hires_support.h"
#include "test/graphics/svf_golden_data.h"
#include "test/graphics/svf_test_util.h"

namespace {

const char kFixtureMap[] =
	"[map]\nversion=3\n"
	"[fonts]\nmarks=data:fixture/MARKS.SVF\njamo=data:fixture/JAMO.SVF\n"
	"[font]\nface=marks,jamo\n"
	"[font.2]\nsize=32\n"
	"[font.3]\nsize=24\n"
	"[font.4]\nface=jamo\nrange.basic-latin=original\n"
	"[font.5]\nface=jamo\nmissing=u+25a1\nadvance=cell\nadvance.basic-latin=game\n";

// Records what the game's own font is asked to draw.
class FakeOriginal : public Graphics::HiResOriginalFont {
public:
	int getAdvance(uint32 cp) const override { return 5; }
	void drawChar(uint32 cp, int x, int yBaseline) const override {
		drawn += Common::String::format("%c@%d,%d ", (char)cp, x, yBaseline);
	}
	mutable Common::String drawn;
};

} // End of anonymous namespace

class HiResTextFontSetTestSuite : public CxxTest::TestSuite {
	// A plane of the golden line's size at scale 1, drawn through @p set.
	bool drawsGolden(const Graphics::HiResFontSet &set, const char *text, int x, int y, int w, int h, const char *const *rows) {
		Graphics::TextPlane plane;
		plane.create(w, h, 1);
		set.drawText(text, strlen(text), x, y, plane, 7);
		const Common::Array<byte> want = SvfTest::goldenBytes(rows, w, h);
		const Common::Array<byte> got = HiResTest::coverageOf(plane);
		for (int i = 0; i < w * h; ++i) {
			if (got[i] != want[i])
				return false;
			if (got[i] && plane.getIndex(i % w, i / w) != 7)
				return false;
		}
		return true;
	}

	bool loadSet(HiResTest::MemoryFiles &files, Graphics::HiResFaceCache &cache, int id, Graphics::HiResFontSet &set) {
		Common::MemoryReadStream s((const byte *)kFixtureMap, strlen(kFixtureMap));
		Graphics::HiResMap map;
		if (!Graphics::HiResFontMap::load(s, "FIXTURE.MAP", Common::Path("game"), Graphics::HiResMapQualifiers(), map))
			return false;
		Common::Array<Common::String> w;
		const Graphics::HiResIdPlan plan = Graphics::compileIdPlan(map, id, w);
		return set.build(plan, map, cache, w) && w.empty();
	}

public:
	// Drawn one code point at a time, the line is byte for byte the renderer's line (marks, U+FFFD rule, missing advance).
	void test_perCodePointIsTheGoldenLine() {
		Graphics::HiResFontSet set;
		set.addChain(SvfTest::makeChain(1, 1, kSvfGoldenLine1xMissing), false, DisposeAfterUse::YES);
		TS_ASSERT(drawsGolden(set, kSvfGoldenLine1xText, kSvfGoldenLine1xX, kSvfGoldenLine1xY, kSvfGoldenLine1xW,
		                      kSvfGoldenLine1xH, kSvfGoldenLine1xRows));
		TS_ASSERT_EQUALS(set.measure(kSvfGoldenLine1xText, strlen(kSvfGoldenLine1xText)), kSvfGoldenLine1xWidth);
	}

	void test_mapPipelineDrawsTheGoldenLine() {
		HiResTest::MemoryFiles files;
		Graphics::HiResFaceCache cache(files);
		Graphics::HiResFontSet set;
		TS_ASSERT(loadSet(files, cache, 0, set));
		TS_ASSERT(drawsGolden(set, kSvfGoldenFont1xText, kSvfGoldenFont1xX, kSvfGoldenFont1xY, kSvfGoldenFont1xW,
		                      kSvfGoldenFont1xH, kSvfGoldenFont1xRows));
		TS_ASSERT_EQUALS(set.getAscent(), kSvfGoldenFont1xAscent);
	}

	// size=32 on 16 px cells is 2/1, size=24 is 3/2: the scaled golden lines.
	void test_sizeScales() {
		HiResTest::MemoryFiles files;
		Graphics::HiResFaceCache cache(files);
		Graphics::HiResFontSet two, threeHalves;
		TS_ASSERT(loadSet(files, cache, 2, two));
		TS_ASSERT(loadSet(files, cache, 3, threeHalves));
		TS_ASSERT_EQUALS(cache.getCount(), 2u);
		TS_ASSERT_EQUALS(files.opens, 2);
		TS_ASSERT(drawsGolden(two, kSvfGoldenLine2xText, kSvfGoldenLine2xX, kSvfGoldenLine2xY, kSvfGoldenLine2xW,
		                      kSvfGoldenLine2xH, kSvfGoldenLine2xRows));
		TS_ASSERT(drawsGolden(threeHalves, kSvfGoldenLine3by2Text, kSvfGoldenLine3by2X, kSvfGoldenLine3by2Y,
		                      kSvfGoldenLine3by2W, kSvfGoldenLine3by2H, kSvfGoldenLine3by2Rows));
	}

	void test_originalRange() {
		HiResTest::MemoryFiles files;
		Graphics::HiResFaceCache cache(files);
		Graphics::HiResFontSet set;
		TS_ASSERT(loadSet(files, cache, 4, set));
		TS_ASSERT_EQUALS(set.route('a'), -1);
		TS_ASSERT_EQUALS(set.route(0xD55C), 0);
		FakeOriginal orig;
		Graphics::TextPlane plane;
		plane.create(64, 20, 1);
		const int end = set.drawText("a\xed\x95\x9c" "b", 5, 2, 14, plane, 1, nullptr, &orig);
		TS_ASSERT_EQUALS(orig.drawn, "a@2,14 b@23,14 ");
		TS_ASSERT_EQUALS(end, 28);
	}

	// missing= draws the box for a code point no face has; advance= picks the cell or the game's advance.
	void test_missingAndAdvance() {
		HiResTest::MemoryFiles files;
		Graphics::HiResFaceCache cache(files);
		Graphics::HiResFontSet plain, set;
		TS_ASSERT(loadSet(files, cache, 0, plain));
		TS_ASSERT(loadSet(files, cache, 5, set));
		const char snowman[] = "\xe2\x98\x83";
		const char box[] = "\xe2\x96\xa1";
		Graphics::TextPlane a, b;
		a.create(32, 20, 1);
		b.create(32, 20, 1);
		TS_ASSERT_EQUALS(plain.drawText(snowman, 3, 0, 14, a, 1), 0);
		TS_ASSERT(a.isEmpty());
		set.drawText(snowman, 3, 0, 14, a, 1);
		plain.drawText(box, 3, 0, 14, b, 1);
		TS_ASSERT(!a.isEmpty());
		TS_ASSERT(HiResTest::coverageOf(a) == HiResTest::coverageOf(b));
		FakeOriginal orig;
		TS_ASSERT_EQUALS(set.getAdvance(0xD55C, &orig), 16);
		TS_ASSERT_EQUALS(plain.getAdvance('i', &orig), 8);
		TS_ASSERT_EQUALS(set.getAdvance('i', &orig), 5);
		TS_ASSERT_EQUALS(set.getAdvance('i'), 8);
		TS_ASSERT_EQUALS(set.getAdvance(0x0301, &orig), 0);
	}

	// TextLayout breaks Hangul at spaces (hangul=word) with the set's advances.
	void test_layout() {
		HiResTest::MemoryFiles files;
		Graphics::HiResFaceCache cache(files);
		Graphics::HiResFontSet set;
		TS_ASSERT(loadSet(files, cache, 0, set));
		const char text[] = "\xed\x95\x9c\xea\xb8\x80 \xed\x95\x9c\xea\xb8\x80 ab";	// 한글 한글 ab
		Graphics::TextRun run;
		run.decode((const byte *)text, strlen(text), Graphics::Utf8TextDecoder());
		Graphics::HiResFontMetrics metrics(set);
		Common::Array<Graphics::LineSpan> lines;
		Graphics::TextLayout::breakLines(run, 72, metrics, Graphics::BreakRules(), lines);
		TS_ASSERT_EQUALS(lines.size(), 2u);
		TS_ASSERT_EQUALS(lines[0].byteEnd, 13u);
		TS_ASSERT_EQUALS(lines[0].width, 72);
		TS_ASSERT_EQUALS(lines[1].byteStart, 14u);
		TS_ASSERT_EQUALS(lines[1].width, 16);
	}
};
```

and in `test/graphics/hires_text_shipped_maps.h` add the includes `graphics/hires_text/font_set.h`,
`graphics/hires_text/id_plan.h`, `graphics/hires_text/svfchain.h`, `graphics/hires_text/text_plane.h` (after
`hires_files.h`) and, after `test_diskFiles`:

```cpp
	// The engine's fallback map draws Hangul with the shipped OFL face.
	void test_defaultMapDraws() {
		Graphics::HiResDiskFiles files;
		const Common::Path data = tree().join("dists/engine-data");
		files.addDataRoot(data);
		const Common::Path path = files.find(data, "hires_text/maps/korean-default.map");
		Common::ScopedPtr<Common::SeekableReadStream> s(files.open(path));
		Graphics::HiResMap map;
		TS_ASSERT(s && Graphics::HiResFontMap::load(*s, "korean-default.map", path.getParent(), Graphics::HiResMapQualifiers(), map));
		Common::Array<Common::String> w;
		Graphics::HiResFaceCache cache(files);
		Graphics::HiResFontSet set;
		TS_ASSERT(set.build(Graphics::compileIdPlan(map, 0, w), map, cache, w));
		TS_ASSERT(w.empty());
		if (!set.getChainCount() || !set.getChain(0)->getFaceCount())
			return;
		TS_ASSERT_EQUALS(set.getChain(0)->getFace(0)->getStatus(), 0);
		Graphics::TextPlane plane;
		plane.create(40, 10, 2);
		const char text[] = "\xed\x95\x9c\xea\xb8\x80";	// 한글
		TS_ASSERT_EQUALS(set.drawText(text, strlen(text), 0, 14, plane, 15), 32);
		TS_ASSERT(!plane.isEmpty());
	}
```

- [ ] **Step 2: run, expect a compile failure** (`font_set.h` missing).
- [ ] **Step 3: implement** — `graphics/hires_text/font_set.h`:

```cpp
#ifndef GRAPHICS_HIRES_TEXT_FONT_SET_H
#define GRAPHICS_HIRES_TEXT_FONT_SET_H

#include "common/array.h"
#include "common/hash-str.h"
#include "common/hashmap.h"
#include "common/noncopyable.h"
#include "common/rect.h"
#include "common/types.h"
#include "common/str.h"

#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/id_plan.h"
#include "graphics/hires_text/text_layout.h"

namespace Graphics {

class HiResFiles;
class SvfChain;
class SvfFace;
class TextPlane;

/** The game's own font, for code points a map leaves to it (range.<spec>=original). Hi-res pixels. */
class HiResOriginalFont {
public:
	virtual ~HiResOriginalFont() {}
	virtual int getAdvance(uint32 cp) const = 0;
	virtual void drawChar(uint32 cp, int x, int yBaseline) const = 0;
};

/**
 * Every face a game's map names, each opened once: whole or streamed as the
 * map's [load] says (auto: whole up to @p wholeLimit bytes).
 */
class HiResFaceCache : Common::NonCopyable {
public:
	explicit HiResFaceCache(HiResFiles &files, uint32 wholeLimit = 256 * 1024);
	~HiResFaceCache();

	/** The face @p ref names; nullptr, with one warning, when it cannot be opened. */
	SvfFace *get(const HiResFaceRef &ref, HiResLoadMode mode, Common::Array<Common::String> &warnings);
	uint getCount() const { return _faces.size(); }

private:
	HiResFiles &_files;
	uint32 _wholeLimit;
	Common::HashMap<Common::String, SvfFace *> _faces; ///< by HiResFaceRef::key(); nullptr: failed
};

/**
 * One engine font id drawn in hi-res: the id plan's chains opened as
 * SvfChains, and text drawn one code point at a time into a TextPlane.
 * Advances, pen and baseline are hi-res pixels.
 */
class HiResFontSet : Common::NonCopyable {
public:
	HiResFontSet();
	~HiResFontSet();

	/** Opens @p plan's chains through @p faces. False when the id chain has no face that opens and is not original. */
	bool build(const HiResIdPlan &plan, const HiResMap &map, HiResFaceCache &faces, Common::Array<Common::String> &warnings);
	/** Adds a chain (tests, callers with faces of their own); the set deletes it with DisposeAfterUse::YES. */
	void addChain(SvfChain *chain, bool original, DisposeAfterUse::Flag dispose);
	void setRules(const HiResIdPlan &plan) { _plan = plan; }

	uint getChainCount() const { return _chains.size(); }
	SvfChain *getChain(uint i) const { return _chains[i]; }
	int getAscent() const;
	int getDescent() const;
	int getShift() const { return _plan.shift; }

	/** The chain that draws @p cp, or -1 when the game's own font does. */
	int route(uint32 cp) const;
	int getAdvance(uint32 cp, const HiResOriginalFont *original = nullptr) const;
	/** The pen's travel over @p len bytes of UTF-8. */
	int measure(const char *text, uint32 len, const HiResOriginalFont *original = nullptr) const;

	/** Draws @p cp in @p index with its baseline at @p yBaseline; returns the advance. */
	int drawChar(uint32 cp, int x, int yBaseline, TextPlane &plane, byte index, const Common::Rect *clip = nullptr,
	             const HiResOriginalFont *original = nullptr) const;
	/** Draws @p len bytes of UTF-8 from pen @p x; returns the pen after it. */
	int drawText(const char *text, uint32 len, int x, int yBaseline, TextPlane &plane, byte index,
	             const Common::Rect *clip = nullptr, const HiResOriginalFont *original = nullptr) const;

	enum { kSyllableCacheSlots = 64 };

private:
	uint32 substitute(uint32 cp, int chain) const;
	bool hasReplacement() const;

	HiResIdPlan _plan;
	Common::Array<SvfChain *> _chains;
	Common::Array<bool> _original;
	Common::Array<DisposeAfterUse::Flag> _dispose;
};

/** TextLayout's metrics over a font set: advances in hi-res pixels. */
class HiResFontMetrics : public LayoutMetrics {
public:
	HiResFontMetrics(const HiResFontSet &set, const HiResOriginalFont *original = nullptr) : _set(set), _original(original) {}
	int advance(uint32 cp) override;

private:
	const HiResFontSet &_set;
	const HiResOriginalFont *_original;
};

/** One code point of UTF-8 at @p p (p < end): its bytes (>= 1); @p invalid for a bad sequence (one byte). */
int decodeUtf8(const char *p, const char *end, uint32 &cp, bool &invalid);

} // End of namespace Graphics

#endif
```

`graphics/hires_text/font_set.cpp`:

```cpp
#include "graphics/hires_text/font_set.h"

#include "common/algorithm.h"
#include "common/stream.h"
#include "common/util.h"

#include "graphics/fonts/svffont.h"
#include "graphics/hires_text/hires_files.h"
#include "graphics/hires_text/svfchain.h"
#include "graphics/hires_text/text_plane.h"
#include "graphics/hires_text/unicode_props.h"

namespace Graphics {

HiResFaceCache::HiResFaceCache(HiResFiles &files, uint32 wholeLimit) : _files(files), _wholeLimit(wholeLimit) {
}

HiResFaceCache::~HiResFaceCache() {
	for (Common::HashMap<Common::String, SvfFace *>::iterator it = _faces.begin(); it != _faces.end(); ++it)
		delete it->_value;
}

SvfFace *HiResFaceCache::get(const HiResFaceRef &ref, HiResLoadMode mode, Common::Array<Common::String> &warnings) {
	const Common::String key = ref.key();
	if (_faces.contains(key))
		return _faces[key];
	SvfFace *face = nullptr;
	const Common::Path path = _files.locate(ref);
	Common::SeekableReadStream *stream = path.empty() ? nullptr : _files.open(path);
	if (!stream) {
		warnings.push_back(Common::String::format("face %s (%s): not found", ref.name.c_str(), key.c_str()));
	} else {
		face = new SvfFace();
		bool ok;
		if (mode == kHiResLoadWhole) {
			ok = face->loadWhole(*stream, ref.name);
			delete stream;
		} else if (mode == kHiResLoadStream) {
			ok = face->loadStreamed(stream, DisposeAfterUse::YES, ref.name);
		} else {
			ok = face->load(stream, DisposeAfterUse::YES, ref.name, _wholeLimit);
		}
		if (!ok) {
			warnings.push_back(Common::String::format("face %s (%s): not an SVF v3 face (error %d)", ref.name.c_str(),
			                                          key.c_str(), (int)face->getError()));
			delete face;
			face = nullptr;
		} else if (face->getStatus()) {
			warnings.push_back(Common::String::format("face %s (%s): status 0x%x, some glyphs may not draw", ref.name.c_str(),
			                                          key.c_str(), face->getStatus()));
		}
	}
	_faces[key] = face;
	return face;
}

HiResFontSet::HiResFontSet() {
}

HiResFontSet::~HiResFontSet() {
	for (uint i = 0; i < _chains.size(); ++i)
		if (_dispose[i] == DisposeAfterUse::YES)
			delete _chains[i];
}

void HiResFontSet::addChain(SvfChain *chain, bool original, DisposeAfterUse::Flag dispose) {
	_chains.push_back(chain);
	_original.push_back(original);
	_dispose.push_back(dispose);
}

bool HiResFontSet::build(const HiResIdPlan &plan, const HiResMap &map, HiResFaceCache &faces,
                         Common::Array<Common::String> &warnings) {
	_plan = plan;
	for (uint c = 0; c < plan.chains.size(); ++c) {
		SvfChain *chain = new SvfChain();
		int tallest = 0;
		for (uint f = 0; f < plan.chains[c].faces.size(); ++f) {
			const HiResFaceRef &ref = plan.chains[c].faces[f];
			SvfFace *face = faces.get(ref, map.loadModeFor(ref), warnings);
			if (face && chain->addFace(face))
				tallest = MAX<int>(tallest, face->getCellHeight());
		}
		if (plan.size && tallest && plan.size != tallest) {
			const int g = Common::gcd(plan.size, tallest);
			if (!chain->setScale(plan.size / g, tallest / g))
				warnings.push_back(Common::String::format("font %d: size=%d cannot scale %d px cells (num and den 1..16)",
				                                          plan.id, plan.size, tallest));
		}
		if (chain->getFaceCount())
			chain->setSyllableCache(kSyllableCacheSlots);
		addChain(chain, plan.chains[c].original, DisposeAfterUse::YES);
	}
	return !_chains.empty() && (_chains[0]->getFaceCount() || _original[0]);
}

int HiResFontSet::getAscent() const {
	int a = 0;
	for (uint i = 0; i < _chains.size(); ++i)
		if (_chains[i]->getFaceCount())
			a = MAX(a, _chains[i]->getAscent());
	return a;
}

int HiResFontSet::getDescent() const {
	int d = 0;
	for (uint i = 0; i < _chains.size(); ++i)
		if (_chains[i]->getFaceCount())
			d = MAX(d, _chains[i]->getDescent());
	return d;
}

int HiResFontSet::route(uint32 cp) const {
	if (_chains.empty())
		return -1;
	uint c = _plan.chainFor(cp);
	if (c >= _chains.size())
		c = 0;
	const SvfChain *chain = _chains[c];
	if (chain->getFaceCount() && chain->has(cp))
		return (int)c;
	if (_original[c])
		return -1;
	return chain->getFaceCount() ? (int)c : -1;
}

bool HiResFontSet::hasReplacement() const {
	const int c = route(0xFFFD);
	return c >= 0 && _chains[c]->has(0xFFFD);
}

uint32 HiResFontSet::substitute(uint32 cp, int chain) const {
	const SvfChain *c = _chains[chain];
	if (!c->has(cp) && _plan.missing && c->has(_plan.missing))
		return _plan.missing;
	return cp;
}

int HiResFontSet::getAdvance(uint32 cp, const HiResOriginalFont *original) const {
	const int c = route(cp);
	if (c < 0)
		return original ? original->getAdvance(cp) : 0;
	switch (_plan.advanceFor(cp)) {
	case kHiResAdvanceGame:
		if (original)
			return original->getAdvance(cp);
		break;
	case kHiResAdvanceCell:
		return Unicode::isCombining(cp) ? 0 : _chains[c]->getMaxCellWidth();
	default:
		break;
	}
	char utf8[4];
	const uint n = SvfFont::encodeUtf8(substitute(cp, c), utf8);
	return _chains[c]->getWidth(utf8, n);
}

namespace {

struct PlotTarget {
	TextPlane *plane;
	byte index;
};

void plotToPlane(void *ctx, int16_t x, int16_t y, uint8_t coverage) {
	PlotTarget *t = (PlotTarget *)ctx;
	t->plane->plot(x, y, coverage, t->index);
}

} // End of anonymous namespace

int HiResFontSet::drawChar(uint32 cp, int x, int yBaseline, TextPlane &plane, byte index, const Common::Rect *clip,
                           const HiResOriginalFont *original) const {
	const int c = route(cp);
	if (c < 0) {
		if (original)
			original->drawChar(cp, x, yBaseline);
	} else {
		char utf8[4];
		const uint n = SvfFont::encodeUtf8(substitute(cp, c), utf8);
		PlotTarget t = { &plane, index };
		_chains[c]->drawCoverage(utf8, n, x, yBaseline + _plan.shift, plane.getWidth(), plane.getHeight(), plotToPlane, &t, clip);
	}
	return getAdvance(cp, original);
}

int decodeUtf8(const char *text, const char *end, uint32 &cp, bool &invalid) {
	const byte *p = (const byte *)text;
	const byte b = p[0];
	invalid = false;
	int n;
	uint32 min;
	if (b < 0x80) {
		cp = b;
		return 1;
	} else if (b >= 0xC2 && b <= 0xDF) {
		n = 2;
		cp = b & 0x1F;
		min = 0x80;
	} else if (b >= 0xE0 && b <= 0xEF) {
		n = 3;
		cp = b & 0x0F;
		min = 0x800;
	} else if (b >= 0xF0 && b <= 0xF4) {
		n = 4;
		cp = b & 0x07;
		min = 0x10000;
	} else {
		n = 0;
		min = 0;
	}
	if (n == 0 || end - text < n) {
		invalid = true;
		cp = 0xFFFD;
		return 1;
	}
	for (int i = 1; i < n; ++i) {
		if ((p[i] & 0xC0) != 0x80) {
			invalid = true;
			cp = 0xFFFD;
			return 1;
		}
		cp = (cp << 6) | (p[i] & 0x3F);
	}
	if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
		invalid = true;
		cp = 0xFFFD;
		return 1;
	}
	return n;
}

int HiResFontSet::measure(const char *text, uint32 len, const HiResOriginalFont *original) const {
	int w = 0;
	const char *end = text + len;
	while (text < end) {
		uint32 cp;
		bool invalid;
		text += decodeUtf8(text, end, cp, invalid);
		if (invalid && !hasReplacement())
			continue;
		w += getAdvance(cp, original);
	}
	return w;
}

int HiResFontSet::drawText(const char *text, uint32 len, int x, int yBaseline, TextPlane &plane, byte index,
                           const Common::Rect *clip, const HiResOriginalFont *original) const {
	const char *end = text + len;
	while (text < end) {
		uint32 cp;
		bool invalid;
		text += decodeUtf8(text, end, cp, invalid);
		// The renderer's rule: a bad sequence is U+FFFD where a face has it, else nothing.
		if (invalid && !hasReplacement())
			continue;
		x += drawChar(cp, x, yBaseline, plane, index, clip, original);
	}
	return x;
}

int HiResFontMetrics::advance(uint32 cp) {
	if (cp == kControlUnit || cp == '\n')
		return 0;
	return _set.getAdvance(cp, _original);
}

} // End of namespace Graphics
```

`graphics/module.mk`: `	hires_text/font_set.o \` (after `font_map.o`).

- [ ] **Step 4: run** → `530 tests ... OK!` (6 font set tests + `test_defaultMapDraws`).
- [ ] **Step 5: commit**

```
GRAPHICS: Hi-res text: Draw Hangul into the text plane

HiResFaceCache opens each face a map names once, whole or streamed as
[load] says. HiResFontSet turns an id plan into SvfChains (size= scales
a chain) and draws text one code point at a time into a TextPlane,
which gives the renderer's own line pixel for pixel: code points a
range leaves to the game go to the engine's font, missing= draws a box,
advance= takes the face's, the game's or the cell's advance.
HiResFontMetrics lets TextLayout break lines with those advances.

Assisted-by: Claude Code
```

## Task 15: Throwaway screen harness and PNG dump (15 min)

- [ ] **Step 1: harness** — `test/graphics/hires_screen.h` (test only; stands in for an engine until P3):

```cpp
#ifndef TEST_GRAPHICS_HIRES_SCREEN_H
#define TEST_GRAPHICS_HIRES_SCREEN_H

#include "graphics/hires_text/text_compose.h"
#include "graphics/hires_text/text_plane.h"
#include "graphics/surface.h"

namespace HiResTest {

/**
 * A throwaway stand-in for an engine (P1 has none): a low-res CLUT8 game
 * frame with its palette and a TextPlane at its scale, presented the way an
 * upscaling driver will: the frame pixel-doubled, then the plane over it.
 */
class FakeScreen {
public:
	FakeScreen(int w, int h, int scale) : _scale(scale) {
		frame.create(w, h, Graphics::PixelFormat::createFormatCLUT8());
		memset(palette, 0, sizeof(palette));
		plane.create(w, h, scale);
		_big.create(w * scale, h * scale, Graphics::PixelFormat::createFormatCLUT8());
	}
	~FakeScreen() {
		frame.free();
		_big.free();
	}

	void setColor(byte index, byte r, byte g, byte b) {
		palette[index * 3] = r;
		palette[index * 3 + 1] = g;
		palette[index * 3 + 2] = b;
	}

	/** The frame at the plane's scale, the plane over it, in @p out (created in @p format). */
	bool present(Graphics::Surface &out, const Graphics::PixelFormat &format, Graphics::TextCompose::Mode mode) {
		for (int y = 0; y < _big.h; ++y)
			for (int x = 0; x < _big.w; ++x)
				*(byte *)_big.getBasePtr(x, y) = *(const byte *)frame.getBasePtr(x / _scale, y / _scale);
		out.create(_big.w, _big.h, format);
		const Common::Rect all(_big.w, _big.h);
		if (format.isCLUT8()) {
			out.copyFrom(_big);
			return Graphics::TextCompose::compositeRect(plane, all, out, nullptr, mode);
		}
		return Graphics::TextCompose::presentRect(plane, _big, all, out, palette, mode);
	}

	Graphics::Surface frame;
	byte palette[256 * 3];
	Graphics::TextPlane plane;

private:
	int _scale;
	Graphics::Surface _big;
};

} // End of namespace HiResTest

#endif
```

- [ ] **Step 2: tests** — `test/graphics/hires_text_screen.h`:

```cpp
#include <cxxtest/TestSuite.h>

#include "common/file.h"
#include "common/fs.h"
#include "common/memstream.h"

#include "graphics/hires_text/font_set.h"
#include "graphics/hires_text/id_plan.h"
#include "image/png.h"

#include "test/graphics/hires_screen.h"
#include "test/graphics/hires_support.h"
#include "test/graphics/svf_golden_data.h"
#include "test/system/null_osystem.h"

namespace {

const char kScreenMap[] = "[map]\nversion=3\n[fonts]\nmarks=data:fixture/MARKS.SVF\njamo=data:fixture/JAMO.SVF\n"
                          "[font]\nface=marks,jamo\n[font.1]\nsize=24\n";

const Graphics::PixelFormat kScreenRgb565(2, 5, 6, 5, 0, 11, 5, 0, 0);
const Graphics::PixelFormat kScreenXrgb8888(4, 8, 8, 8, 0, 16, 8, 0, 0);

// A 160x50 game screen: navy (0) with a white (2) bar under the text, yellow (1) text from the fixture faces.
struct Scene {
	Scene() : screen(160, 50, 2), cache(files) {
		screen.setColor(0, 0, 0, 128);
		screen.setColor(1, 255, 255, 0);
		screen.setColor(2, 255, 255, 255);
		screen.frame.fillRect(Common::Rect(160, 50), 0);
		screen.frame.fillRect(Common::Rect(0, 30, 160, 40), 2);
		Common::MemoryReadStream s((const byte *)kScreenMap, strlen(kScreenMap));
		Graphics::HiResFontMap::load(s, "SCREEN.MAP", Common::Path("game"), Graphics::HiResMapQualifiers(), map);
		Common::Array<Common::String> w;
		set0.build(Graphics::compileIdPlan(map, 0, w), map, cache, w);
		set1.build(Graphics::compileIdPlan(map, 1, w), map, cache, w);
		set0.drawText(kSvfGoldenFont1xText, strlen(kSvfGoldenFont1xText), 8, 20, screen.plane, 1);
		set1.drawText(kSvfGoldenFont1xText, strlen(kSvfGoldenFont1xText), 8, 76, screen.plane, 1);
	}
	HiResTest::FakeScreen screen;
	HiResTest::MemoryFiles files;
	Graphics::HiResFaceCache cache;
	Graphics::HiResMap map;
	Graphics::HiResFontSet set0, set1;
};

} // End of anonymous namespace

class HiResTextScreenTestSuite : public CxxTest::TestSuite {
public:
	// A 1 bpp face looks the same on CLUT8 and on RGB without blending.
	void test_clut8EqualsThreshold() {
		Scene s;
		Graphics::Surface a, b;
		TS_ASSERT(s.screen.present(a, Graphics::PixelFormat::createFormatCLUT8(), Graphics::TextCompose::kStamp));
		TS_ASSERT(s.screen.present(b, kScreenXrgb8888, Graphics::TextCompose::kThreshold));
		int text = 0;
		bool same = true;
		for (int y = 0; y < a.h; ++y) {
			for (int x = 0; x < a.w; ++x) {
				const byte i = *(const byte *)a.getBasePtr(x, y);
				text += i == 1;
				const uint32 want = s.screen.palette[i * 3] << 16 | s.screen.palette[i * 3 + 1] << 8 | s.screen.palette[i * 3 + 2];
				same = same && *(const uint32 *)b.getBasePtr(x, y) == want;
			}
		}
		TS_ASSERT(same);
		TS_ASSERT(text > 500);
		a.free();
		b.free();
	}

	// Text keeps its index: a palette change recolours it without a redraw; a cleared box takes it away.
	void test_paletteAndClear() {
		Scene s;
		int x0 = -1, y0 = -1;
		for (int y = 0; y < s.screen.plane.getHeight() && x0 < 0; ++y)
			for (int x = 0; x < s.screen.plane.getWidth() && x0 < 0; ++x)
				if (s.screen.plane.getCoverage(x, y) == 255) {
					x0 = x;
					y0 = y;
				}
		TS_ASSERT(x0 >= 0);
		Graphics::Surface out;
		s.screen.present(out, kScreenRgb565, Graphics::TextCompose::kBlend);
		TS_ASSERT_EQUALS(*(const uint16 *)out.getBasePtr(x0, y0), 0xFFE0);
		s.screen.setColor(1, 255, 0, 0);
		s.screen.present(out, kScreenRgb565, Graphics::TextCompose::kBlend);
		TS_ASSERT_EQUALS(*(const uint16 *)out.getBasePtr(x0, y0), 0xF800);
		s.screen.plane.clearLowresRect(Common::Rect(160, 50));
		s.screen.present(out, kScreenRgb565, Graphics::TextCompose::kBlend);
		TS_ASSERT_EQUALS(*(const uint16 *)out.getBasePtr(x0, y0), 0x0010);
		TS_ASSERT(s.screen.plane.isEmpty());
		out.free();
	}

	// The 3/2 line has coverage between the 2 bpp levels: blended on RGB, stamped at 128 on CLUT8.
	void test_scaledCoverageBlends() {
		Scene s;
		bool partial = false;
		for (int y = 60; y < 100 && !partial; ++y)
			for (int x = 0; x < 320 && !partial; ++x) {
				const byte c = s.screen.plane.getCoverage(x, y);
				partial = c && c % 17;
			}
		TS_ASSERT(partial);
	}

	// Developer aid: with a directory hires-dump in the working directory, writes the scene as PNG files.
	void test_dumpWhenAsked() {
		Common::install_null_g_system();
		if (Common::FSNode(Common::Path("hires-dump")).isDirectory()) {
			Scene s;
			const Graphics::PixelFormat formats[] = { Graphics::PixelFormat::createFormatCLUT8(), kScreenRgb565, kScreenXrgb8888 };
			const Graphics::TextCompose::Mode modes[] = { Graphics::TextCompose::kStamp, Graphics::TextCompose::kBlend,
			                                              Graphics::TextCompose::kThreshold };
			const char *names[] = { "hires-dump/clut8-stamp.png", "hires-dump/rgb565-blend.png", "hires-dump/rgb888-threshold.png" };
			for (int i = 0; i < 3; ++i) {
				Graphics::Surface out;
				TS_ASSERT(s.screen.present(out, formats[i], modes[i]));
				Common::DumpFile f;
				TS_ASSERT(f.open(Common::Path(names[i])));
				TS_ASSERT(Image::writePNG(f, out, i ? nullptr : s.screen.palette, 3));
				out.free();
			}
		}
		Common::uninstall_null_g_system();
	}
};
```

- [ ] **Step 3: run** → `534 tests ... OK!` (no production code: the harness exercises Tasks 5-14 together; if a test fails,
  the bug is in those, fix it there).
- [ ] **Step 4: dump and look**

```sh
cd $BLD && mkdir -p hires-dump && ./test/runner HiResTextScreenTestSuite test_dumpWhenAsked && file hires-dump/*.png
# clut8-stamp.png: PNG image data, 320 x 100, 8-bit colormap
# rgb565-blend.png, rgb888-threshold.png: PNG image data, 320 x 100, 8-bit/color RGB
```

Open `hires-dump/rgb565-blend.png`: yellow `가한글 abéz` on navy at 16 px, and the same line at 24 px (3/2, blended edges)
over a white bar (verified in the prototype). Needs `USE_PNG` (configured on this host). Remove `hires-dump/` after.

- [ ] **Step 5: commit**

```
TEST: Add a stand-in screen for hi-res text

A low-res CLUT8 frame, its palette and a TextPlane presented the way an
upscaling driver will: CLUT8 by threshold, RGB blended or by threshold.
Checks that a 1 bpp face looks the same on CLUT8 and RGB, that text
follows palette changes and erasure, and writes PNG files on request.

Assisted-by: Claude Code
```

### Review checkpoint D (after Task 15)

superpowers:requesting-code-review on Tasks 14-15, with the prototype's golden argument in hand (section 0). Focus:
`HiResFaceCache` ownership (every `SvfFace` deleted once, a failed open cached as `nullptr` and warned once, streams
deleted on every path); `HiResFontSet::build` with a chain whose faces all fail (`route` -> original or nothing, never a
null chain dereference); `size=` with ratios over 16 (warning, unscaled); `drawText` on invalid UTF-8; that the per
code point path stays equal to the line path (the golden tests are the guard).

## Task 16: ASan and mingw (15 min, no commit unless a fix is needed)

```sh
A=~/work/tmp/scummvm2026-p1asan; mkdir -p $A && cd $A
$SRC/configure --disable-all-engines --enable-debug --disable-optimizations --enable-asan >/dev/null
heavy make -j16 test          # 534 tests OK, no AddressSanitizer/UndefinedBehavior report

M=~/work/tmp/scummvm2026-p1mingw; mkdir -p $M && cd $M
( . ~/.local/mingw/env.sh >/dev/null; export PATH=$MINGW_SYS/bin:$PATH
  $SRC/configure --host=x86_64-w64-mingw32 --with-sdl-prefix=$MINGW_SYS --disable-all-engines --enable-debug --disable-optimizations >/dev/null &&
  heavy make -j16 test/runner )   # test/runner.exe builds; running it is UNVERIFIED (no wine on this host)
```

A fix found here is committed as `GRAPHICS: Hi-res text: ...` (or `TEST: ...`) with the trailer.

## Task 17: Patch list and notes (10 min)

- [ ] `docs/korean/PATCHES.md`: add rows

```
| `ko/01-core` | `common/language.h`, `common/language.cpp` | `getLanguageFolderCode`, `parseLanguageFolderCode` and their table | the ko/ folder of a translation |
| `ko/01-core` | `graphics/module.mk` | 12 `hires_text/` objects | build the hi-res text core |
| `ko/01-core` | `test/module.mk` | one glob, `test/graphics/hires_text_*.h` | tests |
| `ko/01-core` | `LICENSES/COPYING.OFL` | entry for `dists/engine-data/hires_text/NEODG16A.SVF` | notice for the shipped face |
```

- [ ] `docs/korean/plans/P1-notes.md`: deviations from this plan (if any) and the Task 18 results, as P0-notes did.
- [ ] Commit

```
DOCS: Record the P1 plan, its upstream files and the acceptance run

Assisted-by: Claude Code
```

(this plan file, untracked until now, goes into this commit.)

## Task 18: Final acceptance

- [ ] `cd $BLD && heavy make -j16 test` → `Running cxxtest tests (534 tests)...OK!` (N0 + 97).
- [ ] ASan build (Task 16) clean; mingw `test/runner.exe` builds.
- [ ] `cd $SRC && tools/korean/sync-svf.sh --check` and `python3 -I tools/korean/mkgolden.py --check` → rc 0.
- [ ] Ported files identical to `jamo` (Review checkpoint A command) except the two named test edits.
- [ ] No engine touched: `git -C $SRC diff --stat dd5f64b8b34.. -- engines backends gui base` is empty.
- [ ] Upstream files touched are exactly those of `PATCHES.md`:
  `git -C $SRC diff --stat dd5f64b8b34.. -- . ':!graphics/hires_text' ':!test/graphics' ':!test/common/language_folder_code.h' ':!tools/korean' ':!docs/korean' ':!dists/engine-data/hires_text'`
  lists only `common/language.h`, `common/language.cpp`, `graphics/module.mk`, `test/module.mk`, `LICENSES/COPYING.OFL`.
- [ ] Every commit subject passes `devtools/check-commit-msg.py` and ends with `Assisted-by: Claude Code`:
  `for c in $(git -C $SRC rev-list dd5f64b8b34..); do git -C $SRC log -1 --format=%B $c > $BLD/msg; python3 $SRC/devtools/check-commit-msg.py $BLD/msg >/dev/null || echo BAD $c; git -C $SRC log -1 --format=%B $c | tail -n 1 | grep -qx 'Assisted-by: Claude Code' || echo NOTRAILER $c; done`
  prints nothing (the script takes a message file, `devtools/check-commit-msg.py:8-12`).
- [ ] PNG dump looked at (Task 15 Step 4).

Mapping to DESIGN section 11, row P1:

| P1 scope item / done-criterion | Evidence |
|---|---|
| map v3 parser (trimmed `font_map`) | Tasks 7-8, `HiResTextFontMapTestSuite` (8); rewritten, section 1 item 3 |
| `id_plan` | Task 9, `HiResTextIdPlanTestSuite` (3) |
| `map_pick` | Task 13, `HiResTextMapPickTestSuite` (4) |
| `TextLayout`, `unicode_props` (+ `unicode_ranges`) | Tasks 3-4, 39 ported tests; Task 14 `test_layout` over a font set |
| `TextCompose` | Task 6 (5 tests), Task 15 (CLUT8 = threshold RGB for 1 bpp; palette follows) |
| `BandedPlane` | Task 2 (8 ported tests); the storage of `TextPlane` (Task 5, 7 tests) |
| `HiResGlyphSource` over `SvfChain` | replaced by `HiResFontSet` over `SvfChain` (section 1 item 2); golden rows through it (Task 14) |
| `hires_options` with the key table of section 7 | Task 10; keys from P1 in section 1 item 5 |
| `common/language*` | Task 1 |
| every kept test passes | the four ported suites run unchanged but for two named edits; `make test` 534 OK |
| `shipped_maps` loads the converted OFL maps | Task 12/14 `HiResTextShippedMapsTestSuite`: the shipped OFL map loads clean and draws Hangul with the shipped face; harness maps through `hires-maps/` (section 1 item 7) |
| new `svf_glyph_source.h` | named `hires_text_font_set.h` (section 1 item 2) |
| headless proof: golden pixels, PNG dump | Tasks 14-15 |

## 4. Left for later phases (do not do in P1)

- P2: `translationFolderLanguages`, `hangul`, `korean_stem`, `korean_vocab`, `KEYCODE_HANGUL`, particles.
- P3: `TextMode` and `text_encoding`/`korean_text`; SCI's text layer as a `TextPlane` owner; the upscaled driver calling
  `TextCompose::presentRect`/`compositeRect`; `GfxCache` asking `pickHiResMap` + `compileIdPlan` + `HiResFontSet`;
  installing `NEODG16A.SVF` and `korean-default.map` (`dists/engine-data/engine_data.mk`, `DIST_FILES`); printing
  `HiResMap::warnings` and pick notes once (`hires_text_log`).
- P7: `[preload]` warm-up of streamed faces, an MMX/keyed CLUT8 composite if DOS needs it.
- TTF hi-res faces (decision 2): a chain kind next to `SvfChain` in `HiResFontSet`, `hiResFontKindBuilt(ttf)` true under
  `USE_FREETYPE2`.
- Converting each game's v2 map (harness data, owner's OFL pairing, decision 3).
