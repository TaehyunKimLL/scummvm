# P6: SCUMM — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task by task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** DESIGN.md section 11 row P6 and decision 6: **S1 (P6a)** makes every Korean SCUMM v2-v6 DOS pack of the harness
play through upstream's CP949 path, including the shipped UTF-8 bundles that crash today, finds a bundle by its language
code (`ko.trs`), reorders the v2 sentence line (Maniac Mansion, Zak), keeps Korean saves convertible (text trailer) and
stops `korean.fnt` reads outside the font; **S2 (P6b)** first runs a time-boxed spike that decides, on real data and by
fixed criteria, whether 2x hi-res text can be added through a slim seam over upstream's `_textSurface`; only on "go" does
the 2x plan of P6b section 7 run.

**Architecture:** SCUMM's runtime text stays **CP949**, as upstream. A UTF-8 bundle is converted once at load (translated
strings only, SCUMM escapes kept, a character with no `korean.fnt` glyph becomes `?`), so nothing downstream changes and
saves stay upstream-compatible. The new code lives in pure, unit-tested units (`trs_bundle`, `korean_sentence`,
`saved_text`) plus one engine glue file (`korean.cpp`); upstream files get about 45 lines (prototype measured). S2 keeps
upstream layout and line breaking (the 1x `korean.fnt` metrics) and only replaces glyph pixels: the 2-byte glyph is drawn
from an SVF face into `_textSurface` at `_textSurfaceMultiplier = 2`, whose erasure upstream already scales for FM-Towns.

**Tech stack:** C++11 ScummVM code, CxxTest (`make test`), `encoding.dat` (CP949 tables), the P0/P1 hi-res core
(`graphics/hires_text`, `graphics/svf`) for S2 only, Xvfb + xdotool + Alt+S screenshots for game runs, Python 3 stdlib for
`tools/korean/trscheck.py`.

Source of scope: DESIGN.md sections 2.3, 3, 4, 5, 9, 10, 11 (row P6), 12 (decisions 6, 10, 11); `P3-sci-kq1.md`,
`P4-sci-data-games.md` and their notes (working method); `PATCHES.md`; `P6-scumm-baseline.md` (Part 1 of this brief, the
empirical baseline: read it first); branch `jamo` (`git -C /home/thkim/work/scummvm-korean show jamo:engines/scumm/<file>`,
read only); the harness `~/work/scummvm` (read only).

---

## 0. Facts checked for this plan (2026-11-23)

Tree `/home/thkim/work/scummvm-2026.4-korean`, branch `ko-2026.4`. The baseline was measured at `4a8b9907b2c`; while it ran,
`4f5c9a9fa36` and `a2babee65a9` (AA SVF test suites, `test/module.mk` +1 line, no engine file) landed. **Base of this plan:
`a2babee65a9`**; `git diff --stat v2026.4.0 a2babee65a9 -- engines/scumm` is empty. N0 = the `make test` count of a
SCUMM-only build at the base: **N0 = 637** (`~/work/tmp/p6/build-head` rebuilt at `a2babee65a9`: `Running cxxtest tests
(637 tests)...OK!`; it was 620 at `4a8b9907b2c`, the AA suites add 17).

### 0.1 What works today (from `P6-scumm-baseline.md`)

| Set | Today (branch = plain 2026.4.0) | P6a target |
|---|---|---|
| MM V2, Zak V2 (CP949 bundle, `korean00.fnt` 8x8) | play in Korean; sentence line `이게 뭐지? sign` (English object, English order) | `이게 뭐지? 표지판`, `왕에게 티켓 주기` |
| Indy3 VGA, Loom CD, Indy4 CD (CP949 bundle) | play in Korean | unchanged (+ font guard for Indy4's 37 out-of-font codes) |
| MI1 VGA floppy, DOTT floppy (Korean inside the game data) | play in Korean | unchanged |
| **MI2 SE Talkie pack, MI1 UTE (UTF-8 bundle, BOM)** | **assertion `charOffs < 0x14000` (`charset.cpp:1367`) at the first Korean string** | play in Korean (prototype: proven) |
| MM V1 English | English | unchanged |

### 0.2 Prototype (built and run before this plan was written)

`~/work/tmp/p6/tree` = `git archive 4a8b9907b2c` + every P6a change of this plan except Tasks a5 (ko/ folder), a10 and
a11 (the diff: `~/work/tmp/p6/proto.diff`, 18 files, +1242/-4; upstream files: `charset.cpp` +10/-1, `detection_internal.h`
+1/-1, `module.mk` +4, `saveload.cpp` +8, `scumm.h` +5, `scumm_v2.h` +2, `string.cpp` +10/-2, `test/module.mk` +5). Built in
`~/work/tmp/p6/build-tree` (SCUMM only): `Running cxxtest tests (632 tests)...OK!` (620 + 12), `scummvm` links, no warning.

| Check | Run (`~/work/tmp/p6/runs/`) | Result |
|---|---|---|
| MI2 UTF-8 pack as shipped | `mi2u-tree`, `mi2-final-tree` | `UTF-8 body converted to CP949, 841835 -> 738125 bytes`, `2 characters ... show as '?'`; Korean dialogue, credits, captions, dialogue choices; no assertion |
| same without `encoding.dat` | `mi2u-noenc-tree` | warning, bundle not used, English text, no assertion |
| MI1 UTE UTF-8 bundle on MI1 CD data | `mi1uteu-tree` | `552956 -> 483700 bytes`, 3 `?`; Korean credits (in-game lines do not match this data) |
| MM V2 sentence line | `mm-tree2` | `이게 뭐지? 표지판` (was `이게 뭐지? sign`) |
| old MI2 Korean save (2026-09-06, CP949, no trailer) | `mi2save-tree` | `State loaded`, Korean verbs/inventory |
| trailer round trip | `mi2trailer-tree` | autosave ends with `TENC\x00`; reloads; **plain 2026.4.0 loads it too** (`mi2trailer-plain`) |
| UTF-8 -> CP949 = the harness's CP949 stage | `~/work/tmp/p6/py/trs2cp949.py` (Python model of Task a3) | MI2 8780/8780 identical; MI1 UTE 5347/5348 (one stray byte) |

### 0.3 Decisions this plan takes (each re-checkable)

1. **CP949 at run time, also in S2.** Upstream's whole Korean path (line breaking `charset.cpp:664-700`, particles
   `string.cpp:1807-1980`, `checkKSCode`, `get2byteCharPtr`) is CP949; a UTF-8 runtime would touch all of it. S2 decodes
   the CP949 pair to a code point at the glyph seam only.
2. **`?` for a character with no glyph in `korean.fnt`** (outside the 2350 KS X 1001 syllables + ASCII), counted and
   warned once. The CP949 stage of the harness keeps such codes (MI2: 2), which upstream draws from outside the font
   buffer; Task a6 makes those blank.
3. **The trailer is `jamo`'s** (`'TENC'` + one byte, after the serialized state; `jamo:engines/scumm/text_utf8.cpp:563`),
   written as 0 (CP949) for a Korean target with a bundle only, so English saves stay byte-identical to upstream.
4. **No lazy `TrsStore`** in P6: desktop memory is not a concern (MI2: 738 KB after conversion, 1.6 MB peak during it);
   the DOS port (P7) measures and decides (`jamo:engines/scumm/trs_store.h` is the reference).
5. **No `Scumm::TextMode`** in S1 (DESIGN 3.1): the encoding is always CP949, the only decisions are upstream's
   `isScummvmKorTarget()` and the new `hasTranslationBundle()`. S2 adds one object for the hi-res decision.
6. **S2 scope = v4-v6 DOS** (Loom CD, MI1, MI2, Indy4, DOTT), as DESIGN row P6. v2/v3 (MM, Zak, Indy3) stay 1x: their
   verbs and sentence line are drawn into the 1x verb screen each frame.

### 0.4 ScummVM APIs used (file:line at `a2babee65a9`, all verified with grep)

| API / hook site | Where |
|---|---|
| `ScummEngine::loadLanguageBundle` 2333 (`openFile(file, "korean.trs")` 2340, body read 2404-2406), `searchTranslatedLine` 2413, `translateText` 2445 | `engines/scumm/string.cpp` |
| `ScummEngine_v2::drawSentence` 926 (verb copied 937-940), `drawPreposition` | `string.cpp`, `script_v2.cpp:1028` |
| `convertMessageToString` 1573 (translation 1591), particles 1807-1980 | `string.cpp` |
| `TranslatedLine` 1803, `TranslationRange` 1808, `TranslationRoom` 1816, `_languageBuffer` 1822, `_numTranslatedLines` 1823, `_translatedLines` 1824, `_existLanguageFile` 1821, `isScummvmKorTarget()` 1792, `loadLanguageBundle()` 634, `translateText` 1773, `_charsetBuffer[512]` 1719, `_textSurface` 1703, `_textSurfaceMultiplier` 1704 | `engines/scumm/scumm.h` |
| `isScummvmKorTarget` 48, `loadCJKFont` 55, `loadKorFont` 186, `get2byteCharPtr` 248 (KO index 254), `CharsetRendererPC::drawBits1` 739, `drawBits1Kor` 814, `printCharIntern` 1227 (destination choice 1280-1296), `prepareDraw` 1340 (assert 1367) | `engines/scumm/charset.cpp` |
| `checkKSCode` 42, `is2ByteCharacter` 60 (KO: lead 0xB0-0xD0) | `engines/scumm/charset.h` |
| `checkJongsung` 29 / 178 | `engines/scumm/ks_check.{h,cpp}` |
| `loadPtrToResource` 1217 (translation 1233-1243), `openFile` 177 | `engines/scumm/resource.cpp` |
| `getObjOrActorName` 1258 (OBCD name, never translated) | `engines/scumm/object.cpp` |
| `saveState(out, writeHeader)` 626 (serializer 642-644), `loadState` 683 (serializer 827-829, `delete in` 830) | `engines/scumm/saveload.cpp` |
| `ResourceManager::createResource` 194, `_types` 177, `Resource::_address` 93 / `_size` 98 | `engines/scumm/resource.h` |
| `detectLanguage` 216 (`korean.trs` 237) | `engines/scumm/detection_internal.h` |
| `drawStripToScreen` 636 (composite 686-770, CJK m=2 branch 797-802), `restoreBackground` 1194 (`_textSurface` fills 1250-1270), `restoreCharsetBg` 1280, `clearCharsetMask` 1330, `clearTextSurface` 1334, other `_textSurface` fills 356, 1535, 1603-1614, 2349, 4459 | `engines/scumm/gfx.cpp` |
| `_textSurfaceMultiplier = 2` (Towns 1288, Mac Indy3/Loom 1438), screen size `* m` 1487-1488, `_textSurface.create` 1729, `_compositeBuf` 1821 | `engines/scumm/scumm.cpp` |
| `cyclePalette` 736, `setDirtyColors` 590, `updatePalette` 1653 | `engines/scumm/palette.cpp` |
| `Common::translationTableName(pattern, lang)` | `common/language_folder.h:38` |
| `U32String(const char *, uint32, CodePage)` 83, `U32String::encode(String &, CodePage, char)` 128 | `common/ustr.h` |
| `loadCJKTables` (`encoding.dat`) 137, `encodeWindows949` 603 | `common/str-enc.cpp` |
| test `encoding.dat`: `test/engine-data/encoding.dat` rule | `test/module.mk:112-114` |
| S2: `Graphics::TextPlane` (`create(lowW, lowH, scale)` 49, `plot` 72, `clearLowresRect` 76), `HiResFontSet::drawChar(cp, x, yBaseline, plane, index, clip)` 103, `pickHiResMap` 51, `stampSpan`/`composeSpan` 53/60 | `graphics/hires_text/{text_plane,font_set,map_pick,text_compose}.h` |

---

## 1. File map

| File | Sub-phase | New / modified | Responsibility |
|---|---|---|---|
| `engines/scumm/trs_bundle.{h,cpp}` | a1-a3, a6 | new | bundle names, BOM, escapes, UTF-8 -> CP949 of a string and of a body, `koreanFontIndex()` |
| `engines/scumm/korean_sentence.{h,cpp}` | a7 | new | pure v2 sentence composer (CP949) |
| `engines/scumm/saved_text.{h,cpp}` | a9 | new | trailer write/read, UTF-8 judgement, conversion |
| `engines/scumm/korean.cpp` | a4, a8, a9 | new | engine glue: `transcodeLanguageBundle`, `drawKoreanSentence`, `convertLoadedText` |
| `engines/scumm/string.cpp` | a4, a8 | modified | names loop + conversion call in `loadLanguageBundle`; 3-line hook in `drawSentence` |
| `engines/scumm/scumm.h`, `scumm_v2.h` | a4, a8, a9 | modified | `hasTranslationBundle()`, three declarations |
| `engines/scumm/charset.cpp` | a6 | modified | font index guard in `get2byteCharPtr` |
| `engines/scumm/saveload.cpp` | a9 | modified | trailer after the state, conversion after load |
| `engines/scumm/detection_internal.h`, `engines/scumm/scumm.cpp` | a5 | modified | `ko.trs` and `ko/` folder (detection; `SearchMan` in the constructor) |
| `engines/scumm/module.mk` | a1, a4, a7, a9 | modified | objects |
| `test/module.mk` | a1 | modified | SCUMM suites |
| `test/engines/scumm/{trs_bundle,korean_sentence,saved_text,korean_trs_data}.h` | a1-a10 | new | tests |
| `tools/korean/trscheck.py`, `tools/korean/test_trscheck.py` | a11 | new | bundle check + UTF-8/CP949 equivalence (the harness's builders call it) |
| `docs/korean/PATCHES.md`, `docs/korean/plans/P6-notes.md` | a12 | modified / new | records |
| S2 files | P6b | see section 7 | |

## 2. Environment (every task)

```sh
SRC=/home/thkim/work/scummvm-2026.4-korean
OLD=/home/thkim/work/scummvm-korean            # read only: git -C $OLD show jamo:<path>; never check out
HAR=/home/thkim/work/scummvm                   # read only (harness); data copies go under ~/work/tmp
BLD=~/work/tmp/scummvm2026-p6build             # out of tree, SCUMM enabled; never configure inside $SRC
P6=~/work/tmp/p6                               # scratch: data/, runs/, bin/, py/ (Part 1 left them there)
mkdir -p $BLD && cd $BLD
heavy $SRC/configure --disable-all-engines --enable-engine=scumm --enable-debug --disable-optimizations
# configure lists "SCUMM [v0-v6 games]" under "Engines (builtin):" (no scumm_7_8, no HE: not needed)
```

Build and test: `cd $BLD && heavy make -j8 scummvm test` (last line `...OK!`). One suite: `./test/runner
ScummTrsBundleTestSuite`. A second build with SCI too (`--enable-engine=scumm,sci`) in `~/work/tmp/scummvm2026-p6sci`
runs at each review checkpoint, so the SCI suites stay green. Commit messages: `AREA: Subject` (`SCUMM: ...`, `TEST: ...`,
`DOCS: Korean: ...`), a body, exactly one trailing line `Assisted-by: Claude Code`, no `Co-Authored-By`; check each with
`git log -1 --format=%B > $P6/msg && python3 -I devtools/check-commit-msg.py $P6/msg`.

Style: GPL header on every new `.h/.cpp` (copy from `engines/scumm/ks_check.h`), tabs, `namespace Scumm`, `_camelCase`
members, comments say why. New code is never reached by a non-Korean game: every hook is behind `isScummvmKorTarget()`
and/or `hasTranslationBundle()`.

**Game data** (scratch only, never in the repository; Part 1 already made it):

```sh
cd $P6/data   # mm zak indy3 indy4 loom mi1 mi2 dott (+ mi1ute-win mi2ute-win) from $HAR/dist/scummvm-game-<p>-0ab70f653d3-test.zip
ls mm/MM/GAMES/MMKO/KOREAN.TRS zak/ZAK/GAMES/ZAKKO/KOREAN.TRS mi2/MI2/GAMES/MI2/KOREAN.TRS mi2cp/KOREAN.TRS mi1ute-u/korean.trs
# missing? for p in mm zak indy3 indy4 loom mi1 mi2 dott; do mkdir -p $p && unzip -q -o $HAR/dist/scummvm-game-$p-0ab70f653d3-test.zip -d $p; done
```

Runs: `$P6/bin/mkrun.sh RUNDIR TARGET GAMEID PATH key=value...` writes a `run.ini` (engineid scumm, `savepath=RUNDIR`,
`screenshotpath=RUNDIR`); add `extrapath=$SRC/dists/engine-data` so `encoding.dat` is found. `$P6/bin/autoshots.sh RUNDIR
TARGET BIN step...` (steps: `sleep:N`, `click:x,y`, `move:x,y`, a key name, `shot`; `XARGS="--save-slot=N"` loads a save;
`SCALE=2`). `$P6/bin/sess.sh RUNDIR start|do|shot|stop` for interactive sessions. Coordinates are window pixels at
`--scale-factor=2`. View every screenshot you cite.

---

## P6a: S1, translation through the upstream CP949 path

### Task a0: Base, N0, interfaces, data

**Files:** none.

- [ ] **Step 1: base** — `git -C $SRC log --oneline -1` is `a2babee65a9` or later with `git -C $SRC diff a2babee65a9.. --
  engines/scumm test/engines` empty. Configure `$BLD` (section 2), `heavy make -j8 scummvm test`; record the count as N0
  (`Running cxxtest tests (N0 tests)...OK!`).
- [ ] **Step 2: interfaces** — each prints at least one line:

```sh
cd $SRC
grep -n 'openFile(file, "korean.trs");' engines/scumm/string.cpp
grep -n "file.read(_languageBuffer, size - bodyPos);" engines/scumm/string.cpp
grep -n "void ScummEngine_v2::drawSentence" engines/scumm/string.cpp
grep -n "bool isScummvmKorTarget();" engines/scumm/scumm.h
grep -n "bool _existLanguageFile = false;" engines/scumm/scumm.h
grep -n "idx = ((idx % 256) - 0xb0) \* 94 + (idx / 256) - 0xa1;" engines/scumm/charset.cpp
grep -n "byte checkJongsung(byte hi, byte lo);" engines/scumm/ks_check.h
grep -n 'searchFSNode(fslist, "korean.trs", langFile)' engines/scumm/detection_internal.h
grep -n "saveLoadWithSerializer(ser);" engines/scumm/saveload.cpp       # two lines: save and load
grep -n "byte \*createResource(ResType type, ResId idx, uint32 size);" engines/scumm/resource.h
grep -n "String translationTableName" common/language_folder.h
grep -n "test/engine-data/encoding.dat:" test/module.mk
```

- [ ] **Step 3: red on data** — the MI2 pack as shipped crashes the base build:

```sh
B=$P6/bin; R=$P6/runs/a0-mi2
$B/mkrun.sh $R mi2ko monkey2 $P6/data/mi2/MI2/GAMES/MI2 language=ko platform=pc extrapath=$SRC/dists/engine-data
SCALE=2 $B/autoshots.sh $R mi2ko $BLD/scummvm click:48,128 sleep:15 shot; grep -c "Assertion .charOffs < 0x14000. failed" $R/run.log
```

  Expected: `1` (and `START FAILED`/no screenshot after the click). Missing data: tasks still run; **[data]** steps are
  skipped and reported in the notes.

No commit.

### Task a1: Bundle names, BOM, escapes (and the SCUMM test suites)

**Files:** Create `engines/scumm/trs_bundle.h`, `engines/scumm/trs_bundle.cpp`, `test/engines/scumm/trs_bundle.h`;
modify `engines/scumm/module.mk`, `test/module.mk`.

- [ ] **Step 1: test module** — in `test/module.mk`, before `ifeq ($(ENABLE_TWINE), STATIC_PLUGIN)`, add:

```make
ifeq ($(ENABLE_SCUMM), STATIC_PLUGIN)
	TESTS += $(srcdir)/test/engines/scumm/*.h
	TEST_LIBS += engines/scumm/libscumm.a common/libcommon.a
endif

```

  (The engine library comes after the general ones and pulls `common` symbols again; the same reason `libcommon` is
  listed twice above. Only the objects a test references are linked, so `libscumm.a` brings no engine object in.)
- [ ] **Step 2: failing test** — create `test/engines/scumm/trs_bundle.h` with the first two tests (the file grows in
  Tasks a2, a3):

```cpp
#include <cxxtest/TestSuite.h>

#include "common/archive.h"
#include "common/file.h"
#include "common/fs.h"

#include "engines/scumm/trs_bundle.h"

#include "test/system/null_osystem.h"

// UTF-8 bundles converted for korean.fnt (engines/scumm/trs_bundle.h).
class ScummTrsBundleTestSuite : public CxxTest::TestSuite {
public:
	void test_names() {
		Common::Array<Common::Path> names;
		Scumm::trsBundleNames(Common::KO_KOR, names);
		TS_ASSERT_EQUALS(names.size(), 2u);
		TS_ASSERT_EQUALS(names[0].toString(), "ko.trs");
		TS_ASSERT_EQUALS(names[1].toString(), "korean.trs");
		Scumm::trsBundleNames(Common::DE_DEU, names);
		TS_ASSERT_EQUALS(names.size(), 1u);
		TS_ASSERT_EQUALS(names[0].toString(), "de.trs");
		Scumm::trsBundleNames(Common::UNK_LANG, names);
		TS_ASSERT_EQUALS(names.size(), 0u);
	}

	void test_bom_and_escapes() {
		const byte bom[] = { 0xEF, 0xBB, 0xBF, 'a' };
		TS_ASSERT(Scumm::trsBodyIsUtf8(bom, 4));
		TS_ASSERT(!Scumm::trsBodyIsUtf8(bom + 1, 3));
		const byte esc[] = { 0xFF, 0x04, 0x05, 0x00, 0xFE, 0x01, 'x', 0x00 };
		TS_ASSERT_EQUALS(Scumm::trsEscapeLength(esc, esc + 8), 4);
		TS_ASSERT_EQUALS(Scumm::trsEscapeLength(esc + 4, esc + 8), 2);
		TS_ASSERT_EQUALS(Scumm::trsEscapeLength(esc + 6, esc + 8), 0);
		// The argument byte 0 of FF 04 does not end the string.
		TS_ASSERT_EQUALS(Scumm::trsStringEnd(esc, esc + 8), esc + 7);
	}
};
```

- [ ] **Step 3: red** — `heavy make -j8 test`: expected compile error `engines/scumm/trs_bundle.h: No such file or
  directory`.
- [ ] **Step 4: implement** — `engines/scumm/trs_bundle.h` (GPL header, then):

```cpp
#ifndef SCUMM_TRS_BUNDLE_H
#define SCUMM_TRS_BUNDLE_H

#include "common/array.h"
#include "common/language.h"
#include "common/path.h"

namespace Scumm {

/**
 * The .trs translation bundle (the "SCVMTRS " file of string.cpp's loadLanguageBundle()) beyond what upstream reads:
 * its name per language, and a UTF-8 body.
 *
 * Names: a bundle is "<code>.trs" (Common::translationTableName("%s.trs", lang): "ko.trs"); a Korean game also takes
 * the established "korean.trs". The first that exists is used.
 *
 * Body: the bytes after the room table. A body that starts with the UTF-8 BOM (EF BB BF) holds UTF-8 strings; the
 * engine's Korean renderer draws KS X 1001 code (CP949, 2350 syllables in korean.fnt), so such a body is transcoded
 * once at load by transcodeTrsBody(). The SCUMM escapes inside a string (0xFF or 0xFE, a code, and two argument bytes
 * unless the code is 1, 2, 3 or 8; ScummEngine::resStrLen() and convertMessageToString() for v1-v6) are copied as they
 * are; only the text between them is transcoded.
 */

/** The bundle names tried for @p lang, in order; empty for UNK_LANG. */
void trsBundleNames(Common::Language lang, Common::Array<Common::Path> &out);

/** Whether @p body (the bytes after the room table) is UTF-8: it starts with EF BB BF. */
bool trsBodyIsUtf8(const byte *body, uint32 size);

/** Bytes of the escape at @p p (0xFF/0xFE + code [+ 2]), clipped to @p end; 0 when @p p is no escape. */
int trsEscapeLength(const byte *p, const byte *end);

/** The NUL that ends the string at @p s (escapes skipped: their argument bytes may be 0), or @p end. */
const byte *trsStringEnd(const byte *s, const byte *end);

} // End of namespace Scumm

#endif
```

  `engines/scumm/trs_bundle.cpp` (GPL header, then):

```cpp
#include "common/language_folder.h"

#include "scumm/trs_bundle.h"

namespace Scumm {

void trsBundleNames(Common::Language lang, Common::Array<Common::Path> &out) {
	out.clear();
	const Common::String name = Common::translationTableName("%s.trs", lang);
	if (!name.empty())
		out.push_back(Common::Path(name));
	if (lang == Common::KO_KOR)
		out.push_back(Common::Path("korean.trs"));
}

bool trsBodyIsUtf8(const byte *body, uint32 size) {
	return body && size >= 3 && body[0] == 0xEF && body[1] == 0xBB && body[2] == 0xBF;
}

int trsEscapeLength(const byte *p, const byte *end) {
	if (p >= end || (*p != 0xFF && *p != 0xFE))
		return 0;
	int n = 2;
	if (p + 1 < end && p[1] != 1 && p[1] != 2 && p[1] != 3 && p[1] != 8)
		n = 4;
	return (int)MIN<ptrdiff_t>(n, end - p);
}

const byte *trsStringEnd(const byte *s, const byte *end) {
	while (s < end) {
		const int esc = trsEscapeLength(s, end);
		if (esc)
			s += esc;	// an argument byte may be 0
		else if (*s)
			s++;
		else
			break;
	}
	return s;
}

} // End of namespace Scumm
```

  In `engines/scumm/module.mk`, after `ks_check.o \` add `trs_bundle.o \`.
- [ ] **Step 5: green** — `heavy make -j8 scummvm test`: `N0 + 2 tests ... OK!`; `./test/runner ScummTrsBundleTestSuite`:
  `OK!`.
- [ ] **Step 6: commit**

```
SCUMM: Name a translation bundle after its language

A .trs bundle says nothing about its language inside, so the name has
to. Upstream reads only korean.trs. A bundle is now looked up as
<code>.trs first (ko.trs, de.trs; Common::translationTableName), and a
Korean game still takes korean.trs.

The helpers that find a UTF-8 body (it starts with a BOM) and step over
the engine's text escapes (0xFF or 0xFE, a code, two argument bytes
unless the code is 1, 2, 3 or 8, the arguments possibly 0) come with
it; the next commits use them. The SCUMM unit tests are built when the
engine is.

Assisted-by: Claude Code
```

### Task a2: A UTF-8 string to CP949 for korean.fnt

**Files:** Modify `engines/scumm/trs_bundle.{h,cpp}`, `test/engines/scumm/trs_bundle.h`.

- [ ] **Step 1: failing test** — add these private helpers at the top of the class (before `public:`):

```cpp
	bool _added = false;
	bool _tables = false;

	void useTables() {
		Common::install_null_g_system();
		if (!Common::File::exists("encoding.dat")) {
			Common::FSNode dir("test/engine-data");
			if (dir.exists()) {
				SearchMan.addDirectory("scumm_trs_bundle", dir);
				_added = true;
			}
		}
		_tables = Common::File::exists("encoding.dat");
	}

	void dropTables() {
		if (_added)
			SearchMan.remove("scumm_trs_bundle");
		_added = false;
		Common::uninstall_null_g_system();
	}

	Common::String conv(const char *s, uint &replaced) {
		Common::Array<byte> out;
		const byte *p = (const byte *)s;
		const byte *end = Scumm::trsStringEnd(p, p + strlen(s) + 1);
		TS_ASSERT(Scumm::transcodeTrsString(p, end, out, replaced));
		return Common::String((const char *)out.begin(), out.size() - 1);
	}
```

  and the test:

```cpp
	void test_string_to_cp949() {
		useTables();
		if (_tables) {
			uint replaced = 0;
			// "가 a" FF 03 "나": text runs converted, the escape kept.
			TS_ASSERT_EQUALS(conv("\xea\xb0\x80 a\xff\x03\xeb\x82\x98", replaced), "\xb0\xa1 a\xff\x03\xb3\xaa");
			TS_ASSERT_EQUALS(replaced, 0u);
			// FF 04 with argument bytes >= 0x80 is not text.
			TS_ASSERT_EQUALS(conv("\xff\x04\x90\x81\xea\xb0\x80", replaced), "\xff\x04\x90\x81\xb0\xa1");
			// 똠 (CP949 extension, no glyph in korean.fnt), a KS X 1001 symbol (lead A1) and a stray byte: '?'.
			replaced = 0;
			TS_ASSERT_EQUALS(conv("\xeb\x98\xa0\xe2\x80\xa6\xfa", replaced), "???");
			TS_ASSERT_EQUALS(replaced, 3u);
		} else {
			TS_SKIP("encoding.dat not found");
		}
		dropTables();
	}
```

- [ ] **Step 2: red** — `heavy make -j8 test`: `'transcodeTrsString' is not a member of 'Scumm'`.
- [ ] **Step 3: implement** — in `trs_bundle.h`, before the namespace end:

```cpp
/** Whether the CP949 pair @p lead, @p trail has a glyph in korean.fnt (KS X 1001 Hangul, rows 0xB0-0xC8). */
inline bool trsPairInKoreanFont(byte lead, byte trail) {
	return lead >= 0xB0 && lead <= 0xC8 && trail >= 0xA1 && trail <= 0xFE;
}

/**
 * Transcode the UTF-8 string [@p src, @p end) (escapes kept; @p end from trsStringEnd()) to CP949 for korean.fnt and
 * append it, with its NUL, to @p out. A character with no glyph in korean.fnt (outside the 2350 KS X 1001 syllables
 * and ASCII) and a byte that is not UTF-8 become '?'; @p replaced counts them. Returns false when the CP949 tables
 * (encoding.dat) cannot be loaded.
 */
bool transcodeTrsString(const byte *src, const byte *end, Common::Array<byte> &out, uint &replaced);
```

  In `trs_bundle.cpp` add `#include "common/str.h"` and `#include "common/ustr.h"`, then, before `trsStringEnd`:

```cpp
namespace {

/** One code point of strict UTF-8 at @p p (< @p end), its length in @p len; 0xFFFFFFFF for a byte that is not UTF-8. */
uint32 decodeUtf8(const byte *p, const byte *end, int &len) {
	const byte c = p[0];
	len = 1;
	if (c < 0x80)
		return c;
	int n;
	uint32 cp;
	if (c >= 0xC2 && c <= 0xDF) {
		n = 2; cp = c & 0x1F;
	} else if (c >= 0xE0 && c <= 0xEF) {
		n = 3; cp = c & 0x0F;
	} else if (c >= 0xF0 && c <= 0xF4) {
		n = 4; cp = c & 0x07;
	} else {
		return 0xFFFFFFFF;
	}
	if (end - p < n)
		return 0xFFFFFFFF;
	for (int i = 1; i < n; i++) {
		if ((p[i] & 0xC0) != 0x80)
			return 0xFFFFFFFF;
		cp = (cp << 6) | (p[i] & 0x3F);
	}
	if ((n == 3 && cp < 0x800) || (n == 4 && (cp < 0x10000 || cp > 0x10FFFF)) || (cp >= 0xD800 && cp <= 0xDFFF))
		return 0xFFFFFFFF;
	len = n;
	return cp;
}

/** Whether encoding.dat's CP949 table is there: U+AC00 must give B0 A1. */
bool cp949Available() {
	static int state = -1;
	if (state < 0) {
		Common::U32String ga;
		ga += (Common::u32char_type_t)0xAC00;
		const Common::String e = ga.encode(Common::kWindows949);
		state = (e.size() == 2 && (byte)e[0] == 0xB0 && (byte)e[1] == 0xA1) ? 1 : 0;
	}
	return state == 1;
}

/** Transcode a run of text (no escape inside) and append it. */
void transcodeRun(const byte *p, const byte *end, Common::Array<byte> &out, uint &replaced) {
	Common::U32String run;
	while (p < end) {
		int len;
		const uint32 cp = decodeUtf8(p, end, len);
		if (cp == 0xFFFFFFFF) {
			run += (Common::u32char_type_t)'?';
			replaced++;
		} else {
			run += (Common::u32char_type_t)cp;
		}
		p += len;
	}
	Common::String enc;
	run.encode(enc, Common::kWindows949, '?');

	// Walk the code points and their bytes together: < 0x80 gives one byte, the rest two bytes or one '?'.
	uint at = 0;
	for (uint i = 0; i < run.size(); i++) {
		const byte b = (byte)enc[at];
		if (run[i] < 0x80) {
			out.push_back(b);
			at++;
		} else if (b >= 0x81 && at + 1 < enc.size()) {
			const byte t = (byte)enc[at + 1];
			if (trsPairInKoreanFont(b, t)) {
				out.push_back(b);
				out.push_back(t);
			} else {
				out.push_back('?');
				replaced++;
			}
			at += 2;
		} else {
			out.push_back('?');
			replaced++;
			at++;
		}
	}
}

} // End of anonymous namespace
```

  and after `trsStringEnd`:

```cpp
bool transcodeTrsString(const byte *src, const byte *end, Common::Array<byte> &out, uint &replaced) {
	if (!cp949Available())
		return false;
	const byte *run = src;
	const byte *p = src;
	while (p < end) {
		const int esc = trsEscapeLength(p, end);
		if (esc) {
			transcodeRun(run, p, out, replaced);
			for (int i = 0; i < esc; i++)
				out.push_back(p[i]);
			p += esc;
			run = p;
		} else {
			p++;
		}
	}
	transcodeRun(run, p, out, replaced);
	out.push_back(0);
	return true;
}
```

  Note `cp949Available()` caches its answer per process: `loadCJKTables()` itself runs once (`cjk_tables_loaded`), so a
  later `encoding.dat` cannot be picked up anyway.
- [ ] **Step 4: green** — `N0 + 3 ... OK!`; with `test/engine-data` renamed away the test prints `Skipped` (rename back).
- [ ] **Step 5: commit**

```
SCUMM: Convert a UTF-8 translation string for korean.fnt

The Korean renderer draws CP949 pairs from korean.fnt, which holds the
2350 KS X 1001 syllables. A UTF-8 string is converted run by run
between the engine's escapes, which are copied unchanged (their
argument bytes are not text). A character the font has no glyph for, a
CP949 extension syllable, a symbol or a byte that is not UTF-8, becomes
'?' and is counted, so the caller can say how many.

The conversion needs encoding.dat; without it the function says so
instead of producing a string of question marks.

Assisted-by: Claude Code
```

### Task a3: A UTF-8 bundle body, offsets moved

**Files:** Modify `engines/scumm/trs_bundle.{h,cpp}`, `test/engines/scumm/trs_bundle.h`.

- [ ] **Step 1: failing test**

```cpp
	void test_body_moves_offsets() {
		useTables();
		if (_tables) {
			// BOM, original "Hi" @3, translation "안녕" @6 (6 bytes UTF-8), original "\x99" @13, translation "x" @15.
			const byte body[] = { 0xEF, 0xBB, 0xBF, 'H', 'i', 0, 0xEC, 0x95, 0x88, 0xEB, 0x85, 0x95, 0,
			                      0x99, 0, 'x', 0 };
			Common::Array<uint32> orig, trans;
			orig.push_back(3); orig.push_back(13);
			trans.push_back(6); trans.push_back(15);
			Common::Array<byte> out;
			uint replaced = 0;
			TS_ASSERT(Scumm::transcodeTrsBody(body, sizeof(body), orig, trans, out, replaced));
			TS_ASSERT_EQUALS(out.size(), 15u);
			TS_ASSERT_EQUALS(orig[0], 3u);
			TS_ASSERT_EQUALS(trans[0], 6u);
			TS_ASSERT_EQUALS(Common::String((const char *)&out[trans[0]]), "\xbe\xc8\xb3\xe7");
			// The original keeps its byte 0x99 (it is the game's key), unconverted.
			TS_ASSERT_EQUALS(orig[1], 11u);
			TS_ASSERT_EQUALS(out[orig[1]], 0x99);
			TS_ASSERT_EQUALS(trans[1], 13u);
			TS_ASSERT_EQUALS(replaced, 0u);
			// An offset inside a string is refused.
			trans[1] = 16;
			TS_ASSERT(!Scumm::transcodeTrsBody(body, sizeof(body), orig, trans, out, replaced));
		} else {
			TS_SKIP("encoding.dat not found");
		}
		dropTables();
	}
```

- [ ] **Step 2: red** — `'transcodeTrsBody' is not a member of 'Scumm'`.
- [ ] **Step 3: implement** — declaration in `trs_bundle.h`:

```cpp
/**
 * Rebuild a UTF-8 body (BOM first) into @p out: the strings at @p translations are transcoded (transcodeTrsString()),
 * every other string (the originals, the lookup keys, and any tail) is copied byte for byte. Both offset lists
 * (relative to the body, each at a string start) are moved to the same strings in @p out. False, with the outputs
 * unspecified, when an offset is not at a string start or the CP949 tables cannot be loaded.
 */
bool transcodeTrsBody(const byte *body, uint32 size, Common::Array<uint32> &originals,
                      Common::Array<uint32> &translations, Common::Array<byte> &out, uint &replaced);
```

  `trs_bundle.cpp`: add `#include "common/hashmap.h"`, then:

```cpp
bool transcodeTrsBody(const byte *body, uint32 size, Common::Array<uint32> &originals,
                      Common::Array<uint32> &translations, Common::Array<byte> &out, uint &replaced) {
	out.clear();
	replaced = 0;
	if (!trsBodyIsUtf8(body, size) || !cp949Available())
		return false;

	// The BOM stays (no entry points at it), so offsets keep their meaning: relative to the body.
	out.push_back(body[0]);
	out.push_back(body[1]);
	out.push_back(body[2]);

	// Only translations are transcoded: an original is the game's own bytes, the key translateText() compares.
	Common::HashMap<uint32, bool> isTranslation;
	for (uint i = 0; i < translations.size(); i++)
		isTranslation.setVal(translations[i], true);

	Common::HashMap<uint32, uint32> moved;
	const byte *end = body + size;
	uint32 pos = 3;
	while (pos < size) {
		const byte *s = body + pos;
		const byte *e = trsStringEnd(s, end);
		moved.setVal(pos, out.size());
		if (isTranslation.contains(pos)) {
			if (!transcodeTrsString(s, e, out, replaced))
				return false;
		} else {
			for (const byte *q = s; q < e; q++)
				out.push_back(*q);
			out.push_back(0);
		}
		pos = (uint32)(e - body) + 1;
	}

	Common::Array<uint32> *lists[2] = { &originals, &translations };
	for (int l = 0; l < 2; l++) {
		for (uint i = 0; i < lists[l]->size(); i++) {
			uint32 to;
			if (!moved.tryGetVal((*lists[l])[i], to))
				return false;
			(*lists[l])[i] = to;
		}
	}
	return true;
}
```

  Known limit (recorded in the notes, not handled): a string that is both an original and a translation (an
  untranslated line sharing one string) is transcoded, so an original byte >= 0x80 in it would become `?`. No shipped
  bundle shares strings (`trscheck.py`, Task a11, reports it).
- [ ] **Step 4: green** — `N0 + 4 ... OK!`.
- [ ] **Step 5: commit**

```
SCUMM: Rebuild a UTF-8 translation bundle as CP949

The bundle's body holds the original strings (the game's own bytes,
which translateText() compares) and the translations, back to back.
Only the translations are converted; every other string, and any
bytes after the last one, are copied as they are. Each line's two
offsets are moved to the same strings in the rebuilt body, and an
offset that does not start a string makes the whole rebuild fail.

Assisted-by: Claude Code
```

### Task a4: Read `ko.trs` and UTF-8 bundles in `loadLanguageBundle()`

**Files:** Create `engines/scumm/korean.cpp`; modify `engines/scumm/string.cpp`, `engines/scumm/scumm.h`,
`engines/scumm/module.mk`.

No unit test can reach `loadLanguageBundle()` (it needs a running engine); the red is Task a0 Step 3, the green Step 4.

- [ ] **Step 1: string.cpp** — add `#include "scumm/trs_bundle.h"` after `#include "scumm/scumm_v7.h"`. In
  `ScummEngine::loadLanguageBundle()` replace

```cpp
	ScummFile file(this);
	openFile(file, "korean.trs");
```

  with

```cpp
	ScummFile file(this);
	Common::Array<Common::Path> names;
	trsBundleNames(_language, names);
	for (uint n = 0; n < names.size() && !file.isOpen(); n++)
		openFile(file, names[n]);
```

  and after `file.close();` (the one after `file.read(_languageBuffer, size - bodyPos);`) add

```cpp
	if (trsBodyIsUtf8(_languageBuffer, size - bodyPos))
		transcodeLanguageBundle(size - bodyPos);
```

  (The function returns early for a non-Korean target, `string.cpp:2334`, so nothing changes for other languages; the
  `ScummEngine_v7` override for FT is not touched.)
- [ ] **Step 2: scumm.h** — after `virtual void loadLanguageBundle();` add
  `void transcodeLanguageBundle(uint32 bodySize);`; after `bool hasLocalizer();` add

```cpp
	/** Whether a .trs translation bundle was loaded (loadLanguageBundle()). */
	bool hasTranslationBundle() const { return _existLanguageFile; }
```

- [ ] **Step 3: korean.cpp** (GPL header; this file collects the engine-side Korean glue of P6a):

```cpp
#include "common/str.h"

#include "scumm/scumm.h"
#include "scumm/trs_bundle.h"

namespace Scumm {

void ScummEngine::transcodeLanguageBundle(uint32 bodySize) {
	Common::Array<uint32> originals, translations;
	for (int i = 0; i < _numTranslatedLines; i++) {
		originals.push_back(_translatedLines[i].originalTextOffset);
		translations.push_back(_translatedLines[i].translatedTextOffset);
	}
	Common::Array<byte> body;
	uint replaced = 0;
	if (!transcodeTrsBody(_languageBuffer, bodySize, originals, translations, body, replaced)) {
		warning("The UTF-8 translation bundle cannot be converted for korean.fnt (is encoding.dat missing?): not used");
		_existLanguageFile = false;
		return;
	}
	for (int i = 0; i < _numTranslatedLines; i++) {
		_translatedLines[i].originalTextOffset = originals[i];
		_translatedLines[i].translatedTextOffset = translations[i];
	}
	delete[] _languageBuffer;
	_languageBuffer = new byte[body.size()];
	memcpy(_languageBuffer, body.begin(), body.size());
	if (replaced)
		warning("UTF-8 translation bundle: %u characters have no glyph in korean.fnt and show as '?'", replaced);
	debug(2, "loadLanguageBundle: UTF-8 body converted to CP949, %u -> %u bytes", bodySize, body.size());
}

} // End of namespace Scumm
```

  `module.mk`: after `trs_bundle.o \` add `korean.o \`.
- [ ] **Step 4: green + [data]** — `heavy make -j8 scummvm test`: `N0 + 4 ... OK!`, no warning. Then:

```sh
B=$P6/bin; E=extrapath=$SRC/dists/engine-data; D=$P6/data
$B/mkrun.sh $P6/runs/a4-mi2 mi2ko monkey2 $D/mi2/MI2/GAMES/MI2 language=ko platform=pc $E
SCALE=2 $B/autoshots.sh $P6/runs/a4-mi2 mi2ko $BLD/scummvm click:48,128 sleep:12 shot sleep:12 shot sleep:12 shot sleep:12 shot Escape sleep:10 shot
grep -h "UTF-8\|Loaded\|Assertion" $P6/runs/a4-mi2/run.log
$B/mkrun.sh $P6/runs/a4-mi2-noenc mi2ko monkey2 $D/mi2/MI2/GAMES/MI2 language=ko platform=pc
SCALE=2 $B/autoshots.sh $P6/runs/a4-mi2-noenc mi2ko $BLD/scummvm click:48,128 sleep:15 shot
$B/mkrun.sh $P6/runs/a4-mi1ute mi1ko monkey $D/mi1ute-u language=ko platform=pc $E
SCALE=2 $B/autoshots.sh $P6/runs/a4-mi1ute mi1ko $BLD/scummvm Escape sleep:12 shot sleep:12 shot sleep:12 shot
```

  Expected (prototype values): `UTF-8 translation bundle: 2 characters have no glyph ...`, `UTF-8 body converted to CP949,
  841835 -> 738125 bytes`, `Loaded 8780 entries`, no `Assertion`; screenshots: Korean `나 좀 여기서 꺼내 주지 않을래?`, the
  credits, `카리브 해 심연의 스컵 섬`. No-encoding run: `cannot be converted ... not used`, English text. MI1 UTE:
  `552956 -> 483700 bytes`, 3 `?`, Korean credits. View them; record in the notes.
- [ ] **Step 5: commit**

```
SCUMM: Read UTF-8 Korean translation bundles

The Korean fan translations of Monkey Island 1 (Ultimate Talkie) and 2
now ship their bundle in UTF-8, marked by a BOM at the start of the
body. Upstream drew those bytes as CP949: a UTF-8 lead byte is no
Korean lead byte, so it was looked up as a single-byte glyph past the
end of the charset and the engine stopped at an assertion in
prepareDraw() at the first Korean line.

loadLanguageBundle() now finds the bundle as <code>.trs or
korean.trs and converts a UTF-8 body to CP949 once, so everything after
it, line breaking and particles included, sees what a CP949 bundle
gives it. Characters korean.fnt cannot show become '?' with one
warning; without encoding.dat the bundle is not used and the game plays
in English.

Assisted-by: Claude Code
```

### Task a5: `ko.trs` and the `ko/` folder in detection and in the engine

**Files:** Modify `engines/scumm/detection_internal.h`, `engines/scumm/scumm.cpp`.

DESIGN section 4 puts a translation in the language folder `ko/`. Upstream detects a Korean SCUMM game only by
`korean.trs` beside the game files (`detection_internal.h:237`) and opens files from the game folder.

- [ ] **Step 1: detection** — replace

```cpp
		if (searchFSNode(fslist, "korean.trs", langFile)) {
```

  with

```cpp
		Common::FSNode koDir;
		if (searchFSNode(fslist, "korean.trs", langFile) || searchFSNode(fslist, "ko.trs", langFile) ||
		    (searchFSNode(fslist, "ko", koDir) && koDir.isDirectory() &&
		     (koDir.getChild("ko.trs").exists() || koDir.getChild("korean.trs").exists()))) {
```

  (`FSNode::getChild` is case-sensitive on POSIX; the packs ship upper case `KOREAN.TRS` beside the game, which
  `searchFSNode` matches case-insensitively. A `ko/` folder holds lower-case names, the harness's rule since 2026-10-17.)
- [ ] **Step 2: engine search path** — in the constructor `ScummEngine::ScummEngine` (`scumm.cpp`), in the block
  "Add default file directories." (after the Mac `rooms 3` line, 1054), add:

```cpp
	// A Korean translation may live in the language folder ko/ (DESIGN section 4); its .trs and fonts are then
	// found like the game's files.
	if (_language == Common::KO_KOR)
		SearchMan.addSubDirectoryMatching(gameDataDir, "ko");
```

  (`gameDataDir` is the `FSNode` of line 1016; `_language` is set from the detection result at line 141;
  `addSubDirectoryMatching(const FSNode &, const String &caselessName, int priority = 0, int depth = 1, bool flat =
  false)`, `common/archive.h:397`.) UNVERIFIED until Step 3: that `ScummFile`/`openFile()` (`resource.cpp:177`, which
  calls `file.open(filename)`) and `loadKorFont()` (`Common::File::open`) look through `SearchMan` for these names.

- [ ] **Step 3: [data]** — a scratch copy of the MM pack with `KOREAN.TRS` moved to `ko/ko.trs` and the fonts kept beside
  the game: `$BLD/scummvm --detect --path=<copy>` lists `(V2/DOS/Korean)`; the a8 sentence check of Task a8 Step 4 passes
  on it. A copy with `ko/ko.trs` and `ko/korean00.fnt`: the game starts and shows Korean verbs.
- [ ] **Step 4: commit** — `SCUMM: Find a Korean translation in the ko folder` (body: why, the two places, case rule;
  trailer).

### Task a6: Draw blank for a code `korean.fnt` has no glyph for

**Files:** Modify `engines/scumm/trs_bundle.h`, `engines/scumm/charset.cpp`; create `test/engines/scumm/saved_text.h`
(the suite grows in Task a9; this task adds its first test).

`get2byteCharPtr()` (`charset.cpp:254`) indexes `korean.fnt` with `((lead - 0xB0) * 94 + trail - 0xA1)` without a bound,
while `is2ByteCharacter()` accepts leads 0xB0-0xD0: a symbol (lead < 0xB0) reads before the font, Hanja (lead 0xC9-0xD0)
past it. Indy4's CP949 bundle has 37 such codes, MI2's CP949 stage 2 (baseline section 5).

- [ ] **Step 1: failing test** — `test/engines/scumm/saved_text.h`:

```cpp
#include <cxxtest/TestSuite.h>

#include "common/archive.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/memstream.h"

#include "engines/scumm/trs_bundle.h"

#include "test/system/null_osystem.h"

// The font index guard (trs_bundle.h) and the text trailer of a Korean save (saved_text.h, Task a9).
class ScummSavedTextTestSuite : public CxxTest::TestSuite {
public:
	void test_font_index() {
		TS_ASSERT_EQUALS(Scumm::koreanFontIndex(0xB0, 0xA1), 0);	// 가
		TS_ASSERT_EQUALS(Scumm::koreanFontIndex(0xC8, 0xFE), 2349);	// 힝
		TS_ASSERT_EQUALS(Scumm::koreanFontIndex(0xA1, 0xA6), -1);	// a symbol row
		TS_ASSERT_EQUALS(Scumm::koreanFontIndex(0xC9, 0xA1), -1);	// past the syllables
		TS_ASSERT_EQUALS(Scumm::koreanFontIndex(0xBE, 0x41), -1);	// a CP949 extension trail
	}
};
```

- [ ] **Step 2: red** — `'koreanFontIndex' is not a member of 'Scumm'`.
- [ ] **Step 3: implement** — `trs_bundle.h`, after `trsPairInKoreanFont`:

```cpp
/**
 * The glyph number of the CP949 pair @p lead, @p trail in korean.fnt (2350 KS X 1001 syllables, 94 a row from
 * 0xB0A1), or -1 when the font has no glyph for it: get2byteCharPtr() then draws a blank instead of reading outside
 * the font.
 */
inline int koreanFontIndex(byte lead, byte trail) {
	if (!trsPairInKoreanFont(lead, trail))
		return -1;
	const int idx = (lead - 0xB0) * 94 + (trail - 0xA1);
	return idx < 2350 ? idx : -1;
}
```

  `charset.cpp`: `#include "scumm/trs_bundle.h"` after `#include "scumm/charset.h"`; in `get2byteCharPtr` replace the
  Korean case by

```cpp
	case Common::KO_KOR: {
		// A code korean.fnt has no glyph for (a symbol, a CP949 extension syllable, Hanja) draws blank instead of
		// reading outside the font.
		static const byte kBlankGlyph[256] = { 0 };
		idx = koreanFontIndex(idx % 256, idx / 256);
		if (idx < 0)
			return const_cast<byte *>(kBlankGlyph);
		break;
	}
```

  (256 bytes cover the largest Korean font on the host: `korean00.fnt` of DOTT, 81 bytes a glyph.)
- [ ] **Step 4: green** — `N0 + 5 ... OK!`. **[data]** Indy4: every Korean target of section 2 still shows the screens of
  the baseline (Task a13 covers it); reaching one of Indy4's 37 lines in play is UNVERIFIED (no save at one).
- [ ] **Step 5: commit** — `SCUMM: Do not read outside korean.fnt for a code it lacks` (body: the formula, the two
  directions, the counts per bundle, the blank; trailer).

### Task a7: The Korean v2 sentence, as pure functions

**Files:** Create `engines/scumm/korean_sentence.{h,cpp}`, `test/engines/scumm/korean_sentence.h`; modify
`engines/scumm/module.mk`. Reference: `jamo:engines/scumm/korean_sentence.{h,cpp}` (UTF-8 + CP949; here CP949 only).

- [ ] **Step 1: failing test** — `test/engines/scumm/korean_sentence.h`:

```cpp
#include <cxxtest/TestSuite.h>

#include "engines/scumm/korean_sentence.h"

// The v2 sentence line in Korean (engines/scumm/korean_sentence.h). Strings are CP949.
#define KO_TICKET "\xc6\xbc\xc4\xcf"   // 티켓 (final ㅅ)
#define KO_KING   "\xbf\xd5"           // 왕 (final ㅇ)
#define KO_DOOR   "\xb9\xae"           // 문 (final ㄴ)
#define KO_KEY    "\xbf\xad\xbc\xe8"   // 열쇠 (vowel-final)
#define KO_GIVE   "\xc1\xd6\xb1\xe2"   // 주기
#define KO_OPEN   "\xbf\xad\xb1\xe2"   // 열기
#define KO_WHAT   "\xc0\xcc\xb0\xd4 \xb9\xb9\xc1\xf6?" // 이게 뭐지?

class ScummKoreanSentenceTestSuite : public CxxTest::TestSuite {
	static int bytesWide(const Common::String &s, void *) { return (int)s.size() * 4; }

public:
	void test_final_and_josa() {
		TS_ASSERT_EQUALS(Scumm::koreanFinalOf(KO_KING), Scumm::kKoFinalConsonant);
		TS_ASSERT_EQUALS(Scumm::koreanFinalOf(KO_TICKET "@@"), Scumm::kKoFinalConsonant);
		TS_ASSERT_EQUALS(Scumm::koreanFinalOf(KO_KEY " "), Scumm::kKoFinalNone);
		TS_ASSERT_EQUALS(Scumm::koreanFinalOf("\xc5\xd7\xc0\xcc\xba\xed"), Scumm::kKoFinalRieul); // 테이블
		TS_ASSERT_EQUALS(Scumm::koreanFinalOf("sign"), Scumm::kKoFinalNone);
		TS_ASSERT_EQUALS(Scumm::koreanFinalOf(""), Scumm::kKoFinalUnknown);
		TS_ASSERT_EQUALS(Scumm::koreanJosa(Scumm::kKoFinalRieul, Scumm::kJosaMeans), "\xb7\xce");
		TS_ASSERT_EQUALS(Scumm::koreanJosa(Scumm::kKoFinalConsonant, Scumm::kJosaMeans), "\xc0\xb8\xb7\xce");
	}

	void test_compose() {
		Scumm::KoreanSentenceParts p;
		p.verb = KO_GIVE;
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(p), KO_GIVE);
		p.objA = KO_TICKET; p.hasA = true; p.prep = 4;
		// B not chosen yet: "...에게 티켓 주기"
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(p), "...\xbf\xa1\xb0\xd4 " KO_TICKET " " KO_GIVE);
		p.objB = KO_KING; p.hasB = true; p.bIsActor = true;
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(p), KO_KING "\xbf\xa1\xb0\xd4 " KO_TICKET " " KO_GIVE);
		// Open door with key (MM "unlock ... with"): the tool takes (으)로; 열쇠 is vowel-final -> 로.
		Scumm::KoreanSentenceParts q;
		q.verb = KO_OPEN; q.objA = KO_DOOR; q.hasA = true; q.objB = KO_KEY; q.hasB = true; q.prep = 2; q.instrumentB = true;
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(q), KO_KEY "\xb7\xce " KO_DOOR " " KO_OPEN);
		// "What is" stays first.
		Scumm::KoreanSentenceParts w;
		w.verb = KO_WHAT; w.objA = "sign"; w.hasA = true;
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(w), KO_WHAT " sign");
	}

	void test_fit_never_cuts_a_character() {
		Scumm::KoreanSentenceParts p;
		p.verb = KO_GIVE; p.objA = KO_TICKET KO_TICKET KO_TICKET; p.hasA = true;
		// 4 px a byte: "티켓티켓티켓 주기" = 17 bytes = 68 px; at 40 px A keeps two characters.
		const Common::String fit = Scumm::fitKoreanSentence(p, 40, bytesWide, nullptr);
		TS_ASSERT_EQUALS(fit, KO_TICKET " " KO_GIVE);
		TS_ASSERT(fit.size() * 4 <= 40);
	}

	void test_applies() {
		TS_ASSERT(Scumm::koreanSentenceApplies(2, Common::kPlatformDOS, Common::KO_KOR, true));
		TS_ASSERT(!Scumm::koreanSentenceApplies(1, Common::kPlatformDOS, Common::KO_KOR, true));
		TS_ASSERT(!Scumm::koreanSentenceApplies(2, Common::kPlatformNES, Common::KO_KOR, true));
		TS_ASSERT(!Scumm::koreanSentenceApplies(2, Common::kPlatformDOS, Common::EN_ANY, true));
		TS_ASSERT(!Scumm::koreanSentenceApplies(2, Common::kPlatformDOS, Common::KO_KOR, false));
		TS_ASSERT(!Scumm::koreanVerbTranslated("Give"));
		TS_ASSERT(Scumm::koreanVerbTranslated(KO_GIVE));
	}
};
```

- [ ] **Step 2: red** — `engines/scumm/korean_sentence.h: No such file or directory`.
- [ ] **Step 3: implement** — `korean_sentence.h` (GPL header):

```cpp
#ifndef SCUMM_KOREAN_SENTENCE_H
#define SCUMM_KOREAN_SENTENCE_H

#include "common/language.h"
#include "common/platform.h"
#include "common/str.h"

namespace Scumm {

/**
 * The SCUMM v2 sentence line (Maniac Mansion, Zak McKracken) in Korean.
 *
 * ScummEngine_v2::drawSentence() builds the line itself: verb, object A, a hard-coded preposition, object B, in
 * English order ("Give ticket to King"). A Korean bundle translates the parts but cannot reorder them, so for a
 * Korean target with a bundle the parts go through composeKoreanSentence(): verb last, the preposition turned into a
 * particle on B ("왕에게 티켓 주기"). Text is CP949 (the runtime encoding of SCUMM's Korean path; a UTF-8 bundle is
 * converted at load, trs_bundle.h). Pure functions: nothing here needs a running engine.
 */

enum KoreanFinal {
	kKoFinalNone,       ///< vowel-final syllable; also Latin letters, digits, symbols
	kKoFinalConsonant,  ///< final consonant other than ㄹ
	kKoFinalRieul,      ///< final ㄹ (takes 로, not 으로)
	kKoFinalUnknown     ///< nothing to judge by (the "..." placeholder): both forms
};

enum KoreanJosa {
	kJosaNone,
	kJosaObject,   ///< 을/를
	kJosaTopic,    ///< 은/는
	kJosaSubject,  ///< 이/가
	kJosaAnd,      ///< 과/와
	kJosaMeans,    ///< (으)로
	kJosaAt,       ///< 에
	kJosaTo        ///< 에게
};

/** Bytes of the CP949 character at @p p: 1 for ASCII, 2 for a lead byte (never past a NUL). */
int koreanCharLength(const char *p);

/** The final of the last character of @p noun ('@' padding and end spaces skipped). */
KoreanFinal koreanFinalOf(const Common::String &noun);

/** The particle @p josa in the form @p final takes (CP949). */
Common::String koreanJosa(KoreanFinal final, KoreanJosa josa);

/** One v2 sentence, each part already translated. */
struct KoreanSentenceParts {
	Common::String verb;
	Common::String objA;
	Common::String objB;
	bool hasA = false;
	bool hasB = false;
	int prep = 0;              ///< v2 preposition: 0 none, 1 in, 2 with, 3 on, 4 to
	bool bIsActor = false;     ///< B is a person (to: 에게, else 에)
	bool instrumentB = false;  ///< with: B is the tool (Unlock/Fix ... with key)
};

/** The sentence in Korean order ("<B>에게 <A> <verb>"; an unchosen B is "..." with both particle forms). */
Common::String composeKoreanSentence(const KoreanSentenceParts &parts);

typedef int (*KoreanWidthFunc)(const Common::String &text, void *ctx);

/**
 * composeKoreanSentence() fitted to @p maxWidth as @p width measures it: the wider object name loses its last
 * character until the line fits (the particle stays the full name's); if the names cannot shrink further, the line
 * is cut after its last whole character that fits. Never cuts inside a character.
 */
Common::String fitKoreanSentence(const KoreanSentenceParts &parts, int maxWidth, KoreanWidthFunc width, void *ctx);

/** A v2 game (not v1, not NES) in Korean with a bundle loaded. Everything else keeps the English-order line. */
bool koreanSentenceApplies(int version, Common::Platform platform, Common::Language language, bool hasBundle);

/** Whether @p verb has a non-ASCII byte: a bundle that left verbs English keeps the English order. */
bool koreanVerbTranslated(const Common::String &verb);

} // End of namespace Scumm

#endif
```

  `korean_sentence.cpp` (GPL header):

```cpp
#include "scumm/korean_sentence.h"
#include "scumm/ks_check.h"

namespace Scumm {

namespace {

// [josa][consonant, vowel, rieul, unknown], CP949
static const char *const kJosa[][4] = {
	{ "", "", "", "" },
	{ "\xc0\xbb", "\xb8\xa6", "\xc0\xbb", "\xc0\xbb(\xb8\xa6)" },                 // 을 를 을 을(를)
	{ "\xc0\xba", "\xb4\xc2", "\xc0\xba", "\xc0\xba(\xb4\xc2)" },                 // 은 는 은 은(는)
	{ "\xc0\xcc", "\xb0\xa1", "\xc0\xcc", "\xc0\xcc(\xb0\xa1)" },                 // 이 가 이 이(가)
	{ "\xb0\xfa", "\xbf\xcd", "\xb0\xfa", "\xb0\xfa(\xbf\xcd)" },                 // 과 와 과 과(와)
	{ "\xc0\xb8\xb7\xce", "\xb7\xce", "\xb7\xce", "(\xc0\xb8)\xb7\xce" },         // 으로 로 로 (으)로
	{ "\xbf\xa1", "\xbf\xa1", "\xbf\xa1", "\xbf\xa1" },                           // 에
	{ "\xbf\xa1\xb0\xd4", "\xbf\xa1\xb0\xd4", "\xbf\xa1\xb0\xd4", "\xbf\xa1\xb0\xd4" }, // 에게
};

struct PrepRule {
	KoreanJosa person;
	KoreanJosa thing;
	KoreanJosa tool;
};

static const PrepRule kPrepRules[5] = {
	{ kJosaNone, kJosaNone, kJosaNone  }, // 0 none: "<A> <B> <verb>"
	{ kJosaAt,   kJosaAt,   kJosaAt    }, // 1 in:   "<B>에 <A> <verb>"
	{ kJosaAt,   kJosaAt,   kJosaMeans }, // 2 with: "<B>에 ...", tool "<B>(으)로 ..."
	{ kJosaAt,   kJosaAt,   kJosaAt    }, // 3 on
	{ kJosaTo,   kJosaAt,   kJosaTo    }, // 4 to:   person "<B>에게", thing "<B>에"
};

static const char kPlaceholder[] = "...";

Common::String clean(const Common::String &s) {
	Common::String out;
	for (uint i = 0; i < s.size(); i++) {
		if (s[i] != '@')
			out += s[i];
	}
	out.trim();
	return out;
}

uint lastCharStart(const Common::String &s) {
	uint pos = 0, last = 0;
	while (pos < s.size()) {
		last = pos;
		pos += koreanCharLength(s.c_str() + pos);
	}
	return last;
}

Common::String dropLastChar(const Common::String &s) {
	Common::String out(s.c_str(), lastCharStart(s));
	while (!out.empty() && out.lastChar() == ' ')
		out.deleteLastChar();
	return out;
}

int charCount(const Common::String &s) {
	int n = 0;
	for (uint pos = 0; pos < s.size(); n++)
		pos += koreanCharLength(s.c_str() + pos);
	return n;
}

void join(Common::String &line, const Common::String &part) {
	if (part.empty())
		return;
	if (!line.empty())
		line += ' ';
	line += part;
}

Common::String compose(const Common::String &verb, const Common::String &a, const Common::String &b,
                       const KoreanSentenceParts &p, KoreanFinal bFinal) {
	if (!p.hasA || a.empty())
		return verb;

	Common::String line;
	// "What is" asks: "이게 뭐지? 램프" reads, "램프 이게 뭐지?" does not.
	if (!verb.empty() && verb.lastChar() == '?') {
		join(line, verb);
		join(line, a);
		return line;
	}

	const bool unnamedB = p.hasB && b.empty();
	if (p.prep >= 1 && p.prep <= 4 && !unnamedB) {
		const PrepRule &rule = kPrepRules[p.prep];
		const KoreanJosa josa = p.instrumentB ? rule.tool : ((!p.hasB || p.bIsActor) ? rule.person : rule.thing);
		join(line, (p.hasB ? b : Common::String(kPlaceholder)) + koreanJosa(p.hasB ? bFinal : kKoFinalUnknown, josa));
		join(line, a);
		join(line, verb);
		return line;
	}

	join(line, a);
	if (p.hasB)
		join(line, b);
	join(line, verb);
	return line;
}

} // End of anonymous namespace

int koreanCharLength(const char *p) {
	const byte c = (byte)*p;
	if (c >= 0x81 && c <= 0xFE && p[1])
		return 2;
	return 1;
}

KoreanFinal koreanFinalOf(const Common::String &noun) {
	const Common::String s = clean(noun);
	if (s.empty())
		return kKoFinalUnknown;
	const uint start = lastCharStart(s);
	const byte *c = (const byte *)s.c_str() + start;
	if (s.size() - start != 2 || !(c[0] >= 0xB0 && c[0] <= 0xC8 && c[1] >= 0xA1 && c[1] <= 0xFE))
		return kKoFinalNone;	// Latin, digits, symbols: read as vowel-final
	const int jong = checkJongsung(c[0], c[1]);
	if (jong == 0)
		return kKoFinalNone;
	return (jong == 8) ? kKoFinalRieul : kKoFinalConsonant;
}

Common::String koreanJosa(KoreanFinal final, KoreanJosa josa) {
	int form;
	switch (final) {
	case kKoFinalConsonant: form = 0; break;
	case kKoFinalNone:      form = 1; break;
	case kKoFinalRieul:     form = 2; break;
	default:                form = 3; break;
	}
	return kJosa[josa][form];
}

Common::String composeKoreanSentence(const KoreanSentenceParts &parts) {
	const Common::String b = clean(parts.objB);
	return compose(clean(parts.verb), clean(parts.objA), b, parts, koreanFinalOf(b));
}

Common::String fitKoreanSentence(const KoreanSentenceParts &parts, int maxWidth, KoreanWidthFunc width, void *ctx) {
	const Common::String verb = clean(parts.verb);
	Common::String a = clean(parts.objA);
	Common::String b = clean(parts.objB);
	const KoreanFinal bFinal = koreanFinalOf(b);

	Common::String line;
	for (;;) {
		line = compose(verb, a, b, parts, bFinal);
		if (width(line, ctx) <= maxWidth)
			return line;
		const bool canA = parts.hasA && charCount(a) > 1;
		const bool canB = parts.hasB && charCount(b) > 1;
		if (!canA && !canB)
			break;
		if (canB && (!canA || width(b, ctx) > width(a, ctx)))
			b = dropLastChar(b);
		else
			a = dropLastChar(a);
	}

	Common::String fit;
	uint pos = 0;
	while (pos < line.size()) {
		const int n = koreanCharLength(line.c_str() + pos);
		Common::String next(line.c_str(), pos + n);
		if (width(next, ctx) > maxWidth)
			break;
		fit = next;
		pos += n;
	}
	while (!fit.empty() && fit.lastChar() == ' ')
		fit.deleteLastChar();
	return fit;
}

bool koreanSentenceApplies(int version, Common::Platform platform, Common::Language language, bool hasBundle) {
	return version == 2 && platform != Common::kPlatformNES && language == Common::KO_KOR && hasBundle;
}

bool koreanVerbTranslated(const Common::String &verb) {
	for (uint i = 0; i < verb.size(); i++) {
		if ((byte)verb[i] >= 0x80)
			return true;
	}
	return false;
}

} // End of namespace Scumm
```

  `module.mk`: `korean_sentence.o \` after `korean.o \`.
- [ ] **Step 4: green** — `N0 + 9 ... OK!`.
- [ ] **Step 5: commit** — `SCUMM: Add the Korean word order of the v2 sentence line` (body: what the v2 engine does,
  verb last, particles by final consonant from `checkJongsung`, `...` placeholder, fit by width; port of `jamo`, CP949
  only; trailer).

### Task a8: Hook the sentence line

**Files:** Modify `engines/scumm/string.cpp`, `engines/scumm/scumm_v2.h`, `engines/scumm/korean.cpp`.

- [ ] **Step 1: hook** — `scumm_v2.h`, after `void drawPreposition(int index);`:

```cpp
	/** The Korean sentence line (korean_sentence.h); true when it was drawn. */
	bool drawKoreanSentence();
```

  `string.cpp`, in `ScummEngine_v2::drawSentence()`, after the block that copies the verb into `_sentenceBuf` (ends with
  `else\n\t\treturn;`):

```cpp
	if (drawKoreanSentence())
		return;
```

- [ ] **Step 2: korean.cpp** — add includes `scumm/charset.h`, `scumm/korean_sentence.h`, `scumm/scumm_v2.h`, and:

```cpp
namespace {

int sentenceWidth(const Common::String &text, void *ctx) {
	return static_cast<CharsetRenderer *>(ctx)->getStringWidth(0, (const byte *)text.c_str());
}

} // End of anonymous namespace

bool ScummEngine_v2::drawKoreanSentence() {
	// _sentenceBuf holds the verb, translated when its resource was loaded (loadPtrToResource()).
	if (!koreanSentenceApplies(_game.version, _game.platform, _language, hasTranslationBundle()) ||
	    !koreanVerbTranslated(_sentenceBuf))
		return false;

	KoreanSentenceParts parts;
	parts.verb = _sentenceBuf;
	parts.prep = VAR(VAR_SENTENCE_PREPOSITION);
	if (parts.prep < 0 || parts.prep > 4)
		parts.prep = 0;

	// Object names come from the room's OBCD, which upstream never translates: look each up in the bundle.
	byte buf[256];
	const int obj1 = VAR(VAR_SENTENCE_OBJECT1);
	const int obj2 = VAR(VAR_SENTENCE_OBJECT2);
	const byte *name;
	if (obj1 > 0 && (name = getObjOrActorName(obj1)) != nullptr) {
		translateText(name, buf, sizeof(buf));
		parts.objA = (const char *)buf;
		parts.hasA = true;
	}
	if (obj2 > 0 && (name = getObjOrActorName(obj2)) != nullptr) {
		translateText(name, buf, sizeof(buf));
		parts.objB = (const char *)buf;
		parts.hasB = true;
		parts.bIsActor = objIsActor(obj2);
	}
	// Unlock (8) and Fix (6) ... with <B>: B is the tool (MM's verb ids, jamo's kMMVerbUnlock/kMMVerbFix; Zak has no
	// such verb).
	const int verb = VAR(VAR_SENTENCE_VERB);
	parts.instrumentB = parts.prep == 2 && _game.id == GID_MANIAC && (verb == 6 || verb == 8);

	_sentenceBuf = fitKoreanSentence(parts, _virtscr[kVerbVirtScreen].w, sentenceWidth, _charset);

	_string[2].charset = 1;
	_string[2].ypos = _virtscr[kVerbVirtScreen].topline;
	_string[2].xpos = 0;
	_string[2].right = _virtscr[kVerbVirtScreen].w - 1;
	_string[2].color = (_game.platform == Common::kPlatformC64) ? 16 : 13;

	Common::Rect sentenceline(0, _virtscr[kVerbVirtScreen].topline, _virtscr[kVerbVirtScreen].w - 1,
	                          _virtscr[kVerbVirtScreen].topline + 8);
	restoreBackground(sentenceline);
	drawString(2, (const byte *)_sentenceBuf.c_str());
	return true;
}
```

  The 40-character cut of the English path is replaced by the pixel fit (40 "characters" are 40 bytes, 20 Hangul).
  The TTS `_voiceNextString` of the upstream path is not set here (UNVERIFIED whether a Korean TTS voice matters).
- [ ] **Step 3: green** — `N0 + 9 ... OK!`.
- [ ] **Step 4: [data]** — MM (prototype: `이게 뭐지? 표지판`):

```sh
R=$P6/runs/a8-mm; $P6/bin/mkrun.sh $R mmko maniac $P6/data/mm/MM/GAMES/MMKO language=ko platform=pc extra=V2 extrapath=$SRC/dists/engine-data
for i in 1 2 3 4 5; do WAIT=15 $P6/bin/sess.sh $R start mmko $BLD/scummvm && break; done
$P6/bin/sess.sh $R do click:170,240 sleep:1 click:250,240 sleep:1 click:580,150 sleep:25 Escape sleep:8 Escape sleep:10
$P6/bin/sess.sh $R do click:280,342 sleep:1 move:60,215 sleep:2; $P6/bin/sess.sh $R shot      # "이게 뭐지? 표지판"
$P6/bin/sess.sh $R stop
```

  Then the English V1 target of the same pack: its sentence line is `What is sign` style English (unchanged). Zak: the
  same "What is" probe on any object; record the line. A two-object line (`...에게 티켓 주기`) needs an item in the
  inventory: **UNVERIFIED in play** unless a save near one is available (harness H5).
- [ ] **Step 5: commit** — `SCUMM: Draw the Korean v2 sentence line in Korean order` (body: the 3-line hook, object names
  looked up in the bundle because OBCD names are never translated, width fit instead of the byte cut, gates; trailer).

### Task a9: The text trailer of a Korean save

**Files:** Create `engines/scumm/saved_text.{h,cpp}`; modify `test/engines/scumm/saved_text.h`,
`engines/scumm/saveload.cpp`, `engines/scumm/scumm.h`, `engines/scumm/korean.cpp`, `engines/scumm/module.mk`.

- [ ] **Step 1: failing tests** — add `#include "engines/scumm/saved_text.h"` and:

```cpp
	void test_trailer_round_trip() {
		Common::MemoryWriteStreamDynamic out(DisposeAfterUse::YES);
		out.writeByte(0x42);	// the end of a state
		Scumm::writeSavedTextEncoding(out, true);
		Common::MemoryReadStream in(out.getData(), out.size());
		in.readByte();
		TS_ASSERT_EQUALS(Scumm::readSavedTextEncoding(in), Scumm::kSavedTextUtf8);

		Common::MemoryWriteStreamDynamic out0(DisposeAfterUse::YES);
		Scumm::writeSavedTextEncoding(out0, false);
		Common::MemoryReadStream in0(out0.getData(), out0.size());
		TS_ASSERT_EQUALS(Scumm::readSavedTextEncoding(in0), Scumm::kSavedTextLegacy);

		// No trailer: an upstream save ends with the state.
		const byte none[] = { 1, 2 };
		Common::MemoryReadStream in1(none, sizeof(none));
		TS_ASSERT_EQUALS(Scumm::readSavedTextEncoding(in1), Scumm::kSavedTextUnknown);
		const byte other[] = { 'X', 'E', 'N', 'C', 1 };
		Common::MemoryReadStream in2(other, sizeof(other));
		TS_ASSERT_EQUALS(Scumm::readSavedTextEncoding(in2), Scumm::kSavedTextUnknown);
	}

	void test_looks_utf8() {
		const byte utf8[] = "\xea\xb0\x80 a\xff\x04\xb0\xa1";	// 가 a, an escape with KS-looking arguments
		TS_ASSERT(Scumm::savedTextLooksUtf8(utf8, utf8 + sizeof(utf8)));
		const byte cp949[] = "\xc7\xd1\xb1\xdb";	// 한글
		TS_ASSERT(!Scumm::savedTextLooksUtf8(cp949, cp949 + sizeof(cp949)));
		const byte ascii[] = "Give";
		TS_ASSERT(!Scumm::savedTextLooksUtf8(ascii, ascii + sizeof(ascii)));
	}

	void test_convert() {
		Common::install_null_g_system();
		bool added = false;
		if (!Common::File::exists("encoding.dat")) {
			Common::FSNode dir("test/engine-data");
			if (dir.exists()) {
				SearchMan.addDirectory("scumm_saved_text", dir);
				added = true;
			}
		}
		if (Common::File::exists("encoding.dat")) {
			const byte text[] = "\xec\xa3\xbc\xea\xb8\xb0";	// 주기
			Common::Array<byte> out;
			TS_ASSERT(Scumm::convertSavedText(text, text + sizeof(text), out));
			TS_ASSERT_EQUALS(Common::String((const char *)out.begin()), "\xc1\xd6\xb1\xe2");
		}
		if (added)
			SearchMan.remove("scumm_saved_text");
		Common::uninstall_null_g_system();
	}
```

- [ ] **Step 2: red** — `engines/scumm/saved_text.h: No such file or directory`.
- [ ] **Step 3: implement** — `saved_text.h` (GPL header):

```cpp
#ifndef SCUMM_SAVED_TEXT_H
#define SCUMM_SAVED_TEXT_H

#include "common/array.h"
#include "common/stream.h"

namespace Scumm {

/**
 * The encoding of the translated text a Korean save holds (verbs, object and actor names, strings, the message being
 * printed: what loadPtrToResource() stored). This tree always holds CP949 (a UTF-8 bundle is converted at load,
 * trs_bundle.h). The branch `jamo` held UTF-8 for a UTF-8 bundle and marked its saves with a trailer after the
 * serialized state: tag 'TENC' and one byte, 1 for UTF-8. The same trailer is written here (0) and read, so a `jamo`
 * save is converted on load. A build without it never reads past the state.
 */
enum SavedTextEncoding {
	kSavedTextUnknown = -1, ///< no trailer (upstream, or an older save)
	kSavedTextLegacy = 0,   ///< CP949
	kSavedTextUtf8 = 1
};

void writeSavedTextEncoding(Common::WriteStream &out, bool utf8);

/** The trailer at the stream's position; kSavedTextUnknown without one (the position is then unspecified). */
SavedTextEncoding readSavedTextEncoding(Common::SeekableReadStream &in);

/**
 * Whether the text [@p p, @p end) looks UTF-8: escapes skipped (trs_bundle.h), at least one multi-byte sequence and
 * every byte >= 0x80 part of a valid one. A CP949 syllable is never valid UTF-8 (its trail byte 0xA1-0xFE is no
 * continuation after a lead 0xC2-0xC8, and 0xB0-0xBF is no lead).
 */
bool savedTextLooksUtf8(const byte *p, const byte *end);

/** CP949 for korean.fnt of the UTF-8 text [@p p, @p end) with its NUL (transcodeTrsString()). */
bool convertSavedText(const byte *p, const byte *end, Common::Array<byte> &out);

} // End of namespace Scumm

#endif
```

  `saved_text.cpp` (GPL header):

```cpp
#include "common/endian.h"

#include "scumm/saved_text.h"
#include "scumm/trs_bundle.h"

namespace Scumm {

static const uint32 kSavedTextTag = MKTAG('T', 'E', 'N', 'C');

void writeSavedTextEncoding(Common::WriteStream &out, bool utf8) {
	out.writeUint32BE(kSavedTextTag);
	out.writeByte(utf8 ? 1 : 0);
}

SavedTextEncoding readSavedTextEncoding(Common::SeekableReadStream &in) {
	if (in.size() - in.pos() < 5)
		return kSavedTextUnknown;
	if (in.readUint32BE() != kSavedTextTag)
		return kSavedTextUnknown;
	const byte v = in.readByte();
	if (in.err())
		return kSavedTextUnknown;
	return v ? kSavedTextUtf8 : kSavedTextLegacy;
}

bool savedTextLooksUtf8(const byte *p, const byte *end) {
	bool multi = false;
	while (p < end && *p) {
		const int esc = trsEscapeLength(p, end);
		if (esc) {
			p += esc;
			continue;
		}
		const byte c = *p;
		if (c < 0x80) {
			p++;
			continue;
		}
		int n = (c >= 0xC2 && c <= 0xDF) ? 2 : (c >= 0xE0 && c <= 0xEF) ? 3 : (c >= 0xF0 && c <= 0xF4) ? 4 : 0;
		if (!n || end - p < n)
			return false;
		for (int i = 1; i < n; i++) {
			if ((p[i] & 0xC0) != 0x80)
				return false;
		}
		multi = true;
		p += n;
	}
	return multi;
}

bool convertSavedText(const byte *p, const byte *end, Common::Array<byte> &out) {
	out.clear();
	uint replaced = 0;
	return transcodeTrsString(p, trsStringEnd(p, end), out, replaced);
}

} // End of namespace Scumm
```

  `module.mk`: `saved_text.o \` after `korean_sentence.o \`. `scumm.h`, after the `transcodeLanguageBundle` line:

```cpp
	/** Convert the translated text of a UTF-8 save to CP949 (saved_text.h); @p saved is a SavedTextEncoding. */
	void convertLoadedText(int saved);
```

  `saveload.cpp`: `#include "scumm/saved_text.h"` after `#include "scumm/scumm_v8.h"`; in
  `saveState(Common::SeekableWriteStream *out, bool writeHeader)` after `saveLoadWithSerializer(ser);`:

```cpp
	// After the state, where a build without it never reads (saved_text.h). Korean saves only.
	if (isScummvmKorTarget() && hasTranslationBundle())
		writeSavedTextEncoding(*out, false);
```

  in `loadState()` replace `saveLoadWithSerializer(ser);\n\tdelete in;` by:

```cpp
	saveLoadWithSerializer(ser);
	const SavedTextEncoding savedText = readSavedTextEncoding(*in);
	delete in;

	if (isScummvmKorTarget() && hasTranslationBundle())
		convertLoadedText(savedText);
```

  `korean.cpp`: includes `scumm/resource.h`, `scumm/saved_text.h`, `scumm/verbs.h`, and:

```cpp
void ScummEngine::convertLoadedText(int saved) {
	// What loadPtrToResource() stores, translated. A v6+ string resource is an array, not text; a verb that is an
	// image holds no text.
	static const ResType kTextTypes[] = { rtVerb, rtObjectName, rtActorName, rtString };
	struct Item {
		ResType type;
		ResId idx;
	};
	Common::Array<Item> items;
	bool anyUtf8 = false;
	for (uint t = 0; t < ARRAYSIZE(kTextTypes); t++) {
		const ResType type = kTextTypes[t];
		if (type == rtString && _game.version >= 6)
			continue;
		for (ResId idx = 0; idx < _res->_types[type].size(); idx++) {
			const byte *ptr = _res->_types[type][idx]._address;
			if (!ptr)
				continue;
			if (type == rtVerb && (idx >= _numVerbs || _verbs[idx].type != kTextVerbType))
				continue;
			const Item item = { type, idx };
			items.push_back(item);
			if (savedTextLooksUtf8(ptr, ptr + _res->_types[type][idx]._size))
				anyUtf8 = true;
		}
	}
	const byte *msgEnd = _charsetBuffer + sizeof(_charsetBuffer);
	if (savedTextLooksUtf8(_charsetBuffer, msgEnd))
		anyUtf8 = true;

	// A trailer says so; a save without one (a `jamo` save made before the trailer) is judged by its text.
	if (saved == kSavedTextLegacy || (saved == kSavedTextUnknown && !anyUtf8))
		return;

	Common::Array<byte> out;
	int converted = 0;
	for (uint i = 0; i < items.size(); i++) {
		const byte *ptr = _res->_types[items[i].type][items[i].idx]._address;
		if (!savedTextLooksUtf8(ptr, ptr + _res->_types[items[i].type][items[i].idx]._size))
			continue;
		if (!convertSavedText(ptr, ptr + _res->_types[items[i].type][items[i].idx]._size, out))
			break;	// no encoding.dat: leave the save as it is
		byte *dst = _res->createResource(items[i].type, items[i].idx, out.size());
		memcpy(dst, out.begin(), out.size());
		converted++;
	}
	if (savedTextLooksUtf8(_charsetBuffer, msgEnd) && convertSavedText(_charsetBuffer, msgEnd, out)) {
		// CP949 is never longer than UTF-8 (2 bytes for 3), so the converted message fits.
		memset(_charsetBuffer, 0, sizeof(_charsetBuffer));
		memcpy(_charsetBuffer, out.begin(), MIN<uint>(out.size(), sizeof(_charsetBuffer) - 1));
		converted++;
	}
	debug(1, "SCUMM: converted %d text resources of a UTF-8 save to CP949", converted);
}
```

- [ ] **Step 4: green** — `N0 + 12 ... OK!` (prototype: 632 = 620 + 12 at the old base; here 649).
- [ ] **Step 5: [data]** — three runs (prototype results in brackets):

```sh
B=$P6/bin; E=extrapath=$SRC/dists/engine-data; D=$P6/data
# old CP949 save on the UTF-8 pack [State loaded, Korean verbs]
R=$P6/runs/a9-old; $B/mkrun.sh $R monkey2-ko monkey2 $D/mi2/MI2/GAMES/MI2 language=ko platform=pc $E
cp $HAR/saves/monkey2-ko.s02 $R/; XARGS=--save-slot=2 SCALE=2 $B/autoshots.sh $R monkey2-ko $BLD/scummvm sleep:8 shot
# round trip: autosave after 20 s, then reload it [s00 ends with TENC 00; State loaded]
R=$P6/runs/a9-rt; $B/mkrun.sh $R monkey2-ko monkey2 $D/mi2/MI2/GAMES/MI2 language=ko platform=pc $E
sed -i 's/^autosave_period=0/autosave_period=20/' $R/run.ini; cp $HAR/saves/monkey2-ko.s02 $R/
XARGS=--save-slot=2 SCALE=2 $B/autoshots.sh $R monkey2-ko $BLD/scummvm sleep:45 shot
python3 -c "import gzip,sys; d=gzip.open(sys.argv[1]).read(); print(d[-5:])" $R/monkey2-ko.s00     # b'TENC\x00'
# the same save on plain 2026.4.0 with the CP949 data [State loaded]
R2=$P6/runs/a9-plain; $B/mkrun.sh $R2 monkey2-ko monkey2 $D/mi2cp language=ko platform=pc; cp $R/monkey2-ko.s00 $R2/
XARGS=--save-slot=0 SCALE=2 $B/autoshots.sh $R2 monkey2-ko $P6/build-plain/scummvm sleep:8 shot
```

  An English target's save has no trailer: save MM V1 English by autosave and check the last 5 bytes are not `TENC`.
  A `jamo` UTF-8 save is not on this host: the conversion path is **UNVERIFIED on data** (unit-tested only); if the
  owner has one (H6), load it here and expect `converted N text resources`.
- [ ] **Step 6: commit** — `SCUMM: Mark the text encoding of a Korean save` (body: trailer format = jamo's, written 0
  for Korean targets with a bundle only, read after the state, conversion of jamo UTF-8 saves, older saves judged by
  their text, upstream reads such a save unchanged (it never reads past the state); trailer).

### Task a10: Developer-data check: conversion equals the harness's CP949 stage

**Files:** Create `test/engines/scumm/korean_trs_data.h`.

A suite in the style of P4's `korean_packs.h`: it runs only when a folder `korean-trs/` is in the runner's working
directory, so CI without data passes.

- [ ] **Step 1: test** (it is green as soon as the data is there; its red is a wrong conversion):

```cpp
#include <cxxtest/TestSuite.h>

#include "common/archive.h"
#include "common/debug.h"
#include "common/file.h"
#include "common/fs.h"

#include "engines/scumm/trs_bundle.h"

#include "test/system/null_osystem.h"

// korean-trs/<name>.utf8.trs and korean-trs/<name>.cp949.trs (developer data, not in the repository): converting the
// first gives the translations of the second, line for line, except the counted characters korean.fnt lacks.
class ScummKoreanTrsDataTestSuite : public CxxTest::TestSuite {
	struct Bundle {
		Common::Array<uint32> orig, trans;
		Common::Array<byte> body;
	};

	static bool read(const Common::FSNode &node, Bundle &b) {
		Common::SeekableReadStream *s = node.createReadStream();
		if (!s)
			return false;
		const uint32 size = s->size();
		if (s->readUint32BE() != MKTAG('S', 'C', 'V', 'M') || s->readUint32BE() != MKTAG('T', 'R', 'S', ' ')) {
			delete s;
			return false;
		}
		const int n = s->readUint16LE();
		Common::Array<uint16> idx;
		for (int i = 0; i < n; i++) {
			idx.push_back(s->readUint16LE());
			b.orig.push_back(s->readUint32LE());
			b.trans.push_back(s->readUint32LE());
		}
		const int rooms = s->readByte();
		for (int r = 0; r < rooms; r++) {
			s->readByte();
			const int scripts = s->readUint16LE();
			s->skip(scripts * 8);
		}
		const uint32 bodyPos = s->pos();
		for (int i = 0; i < n; i++) {
			b.orig[i] -= bodyPos;
			b.trans[i] -= bodyPos;
		}
		b.body.resize(size - bodyPos);
		s->read(b.body.begin(), b.body.size());
		delete s;
		return true;
	}

public:
	void test_utf8_bundles_match_cp949_stage() {
		Common::FSNode dir("korean-trs");
		if (!dir.exists()) {
			TS_WARN("korean-trs/ is not in the working directory: bundle data check skipped");
			return;
		}
		Common::install_null_g_system();
		SearchMan.addDirectory("scumm_trs_data_tables", Common::FSNode("test/engine-data"));
		Common::FSList files;
		dir.getChildren(files, Common::FSNode::kListFilesOnly);
		for (uint f = 0; f < files.size(); f++) {
			Common::String name = files[f].getName();
			if (!name.hasSuffix(".utf8.trs"))
				continue;
			name.erase(name.size() - 9);
			Bundle u, c;
			TS_ASSERT(read(files[f], u));
			TS_ASSERT(read(dir.getChild(name + ".cp949.trs"), c));
			Common::Array<byte> out;
			uint replaced = 0;
			TS_ASSERT(Scumm::transcodeTrsBody(u.body.begin(), u.body.size(), u.orig, u.trans, out, replaced));
			uint same = 0;
			for (uint i = 0; i < u.trans.size(); i++) {
				const Common::String a((const char *)&out[u.trans[i]]);
				const Common::String b((const char *)&c.body[c.trans[i]]);
				if (a == b)
					same++;
			}
			debug("%s: %u of %u lines identical, %u characters as '?'", name.c_str(), same, u.trans.size(), replaced);
			// Every line that differs differs only by the '?' the conversion counted.
			TS_ASSERT_LESS_THAN_EQUALS(u.trans.size() - same, replaced);
		}
		SearchMan.remove("scumm_trs_data_tables");
		Common::uninstall_null_g_system();
	}
};
```

- [ ] **Step 2:** `heavy make -j8 test`: `N0 + 13 ... OK!` with the warning `korean-trs/ is not in the working directory`.
- [ ] **Step 3: [data]** — `mkdir -p $BLD/korean-trs && cp $HAR/gamedata/mi2kor/korean.trs $BLD/korean-trs/mi2.utf8.trs &&
  cp $HAR/gamedata/mi2kor/korean.trs.cp949 $BLD/korean-trs/mi2.cp949.trs` (the same for `mi1ute-kor` as `mi1ute.*`), then
  `cd $BLD && ./test/runner ScummKoreanTrsDataTestSuite` -> `OK!` (no warning line). The per-bundle numbers come from
  `trscheck.py equal` (Task a11): MI2 at least 8778 of 8780 lines identical with 2 `?`, MI1 UTE at least 5345 of 5348
  with 3 `?` (the Python model kept the out-of-font codes and got 8780/8780 and 5347/5348; the engine writes `?` for
  them, so at most that many lines differ). Remove `$BLD/korean-trs` afterwards (`rm -r "$BLD"/korean-trs`).
- [ ] **Step 4: commit** — `TEST: Check UTF-8 bundles against their CP949 stage when given` (trailer).

### Task a11: `tools/korean/trscheck.py`

**Files:** Create `tools/korean/trscheck.py`, `tools/korean/test_trscheck.py`.

The harness builders (H2) and the reviewer need the same answers without a build: the encoding census of a bundle, its
out-of-font characters, and UTF-8/CP949 equivalence. Port of `~/work/tmp/p6/py/{trsinfo,trsenc,trs2cp949}.py` (Part 1),
stdlib only, run with `python3 -I`.

- [ ] **Step 1: tests first** — `test_trscheck.py` (unittest) builds two tiny bundles in a temp dir with the module's
  own writer (`write_bundle(lines, rooms, body_bom)`), and asserts: `census()` counts escapes `FF 04 xx xx` / `FE 01` as
  non-text; a UTF-8 bundle whose translation holds `똠` reports one out-of-font character; `equal()` of a UTF-8 bundle
  and its hand-made CP949 twin returns `(n, n)`; a bundle whose translation offset points inside a string is rejected
  with exit code 1. Run: `python3 -I tools/korean/test_trscheck.py` -> `FAILED (errors=4)` (module missing).
- [ ] **Step 2: implement** — subcommands `census FILE...` (lines, rooms, body offset, BOM, valid runs, out-of-font
  characters by count), `equal UTF8 CP949` (the a3 rule: translations only, escapes kept, `?` for out-of-font; prints
  `<identical> of <lines> identical, <q> characters as '?'`, exit 1 when a differing line is not explained by its `?`),
  `shared FILE` (offsets used as both original and translation; exit 1 if any). Same parsing as
  `loadLanguageBundle()`: magic, u16 count, per line u16 index + u32 + u32, u8 rooms, per room u8 id + u16 count +
  8 bytes per script, body.
- [ ] **Step 3: green** — `python3 -I tools/korean/test_trscheck.py` -> `OK`; **[data]**
  `python3 -I tools/korean/trscheck.py equal $HAR/gamedata/mi2kor/korean.trs $HAR/gamedata/mi2kor/korean.trs.cp949`
  -> `8778 of 8780 identical, 2 characters as '?'` (or more identical, if both `?` fall in one line), exit 0; `census` over the eight bundles of the baseline gives its
  section 5 counts (Indy4: 35 extension syllables + 2 symbols).
- [ ] **Step 4: commit** — `TOOLS: Korean: Add trscheck.py, the SCUMM bundle check` (the prefix of the other
  `tools/korean` commits; trailer).

### Task a12: Records

**Files:** Modify `docs/korean/PATCHES.md`; create `docs/korean/plans/P6-notes.md`.

- [ ] **Step 1:** `PATCHES.md`, topic `ko/05-scumm`, one row per upstream file: `engines/scumm/string.cpp`
  (bundle names loop, UTF-8 conversion call, sentence hook), `scumm.h` (`hasTranslationBundle`, two declarations),
  `scumm_v2.h` (`drawKoreanSentence`), `charset.cpp` (font index guard), `saveload.cpp` (trailer write/read +
  conversion call), `detection_internal.h` (`ko.trs`, `ko/`), `scumm.cpp` (`ko/` in `SearchMan`), `module.mk`
  (5 objects), `test/module.mk` (SCUMM suites).
- [ ] **Step 2:** `git diff --stat a2babee65a9.. -- engines test` lists exactly the files of section 1 for P6a.
- [ ] **Step 3:** `P6-notes.md`: one row per task (commit, test count, deviations), the [data] results with the run
  folders, and the UNVERIFIED list of section 9 as it stands.
- [ ] **Step 4: commit** — `DOCS: Korean: Record the SCUMM translation patches` (trailer).

### Task a13: Smoke over every data set; English unaffected

**Files:** none (notes only).

- [ ] **Step 1:** the runs of the baseline (its section 3 table), on `$BLD/scummvm`, each with `extrapath`: mm, mm EN,
  zak, indy3, loom, mi1, mi2 (UTF-8 pack), mi2cp, indy4, dott, mi1ute-u, mi1ute-c. Same steps as
  `~/work/tmp/p6/runs/*-head/auto.out` show (`autoshots.sh ... shot sleep:10 shot sleep:10 shot sleep:15 shot sleep:15 shot
  Escape sleep:8 shot sleep:15 shot`; MI2: click 48,128 first; Loom: click 318,236 then 320,142). Expected: every set as
  the baseline, except mi2 and mi1ute-u, which now show Korean; zero `Assertion` lines; the sentence line as Task a8.
- [ ] **Step 2: English** — MM V1 English and one English v5 game (MI1 CD `gamedata/mi1en`, `language=en`): log has no
  `Korean` / `UTF-8` line; first screenshots look as on `build-plain` (view both).
- [ ] **Step 3:** copy the new screenshots to `~/work/tmp/ko-screenshots/scumm-p6a-*.png` with caption lines; notes.

### Review checkpoint A (after Task a13)

- `$BLD` `make test`: `N0 + 13 ... OK!`; `~/work/tmp/scummvm2026-p6sci` (`--enable-engine=scumm,sci`): N0(sci) + 13 OK;
  an ASan build (`--enable-asan`, `~/work/tmp/scummvm2026-p6asan`): tests OK and the a4/a9 MI2 runs with
  `ASAN_OPTIONS=detect_leaks=0` print no AddressSanitizer line (the a6 guard is what ASan would have caught on Indy4).
- Self-read: every hook behind `isScummvmKorTarget()` / `hasTranslationBundle()` / `koreanSentenceApplies()`;
  `git diff a2babee65a9.. -- engines/scumm | grep -c '^+' ` for upstream files only (not new files) <= 60.
- A fresh reviewer (another agent; the review lanes of the owner's practice) reads the diff against this plan; findings
  go back to the implementer (resume, not a new session).

**P6a acceptance checklist**

- [ ] The MI2 pack and the MI1 UTE bundle as shipped (UTF-8) play in Korean; no assertion on any set.
- [ ] `ko.trs` beside the game and `ko/ko.trs` are found (detection says Korean; the game shows Korean).
- [ ] Without `encoding.dat` a UTF-8 bundle is refused with one warning and the game plays in English.
- [ ] MM/Zak sentence line: object names from the bundle, Korean order; English targets unchanged.
- [ ] Saves: old CP949 saves load; new Korean saves carry `TENC 00` and load on plain 2026.4.0; English saves carry no
      trailer.
- [ ] `korean.fnt` is never read outside its 2350 glyphs (unit test; ASan quiet on the runs).
- [ ] `trscheck.py equal` on MI2/MI1 UTE: every differing line explained by its `?`.
- [ ] PATCHES.md and P6-notes.md complete; commit messages pass `check-commit-msg.py`.

---

## P6b: S2, 2x hi-res text behind a go/no-go gate

### 5. Why a gate, and what it decides

`jamo` drew 2x text through 675 lines in `gfx.cpp` because it kept its own plane and had to erase it wherever upstream
erases game pixels. The bet of DESIGN decision 6 is a **slim seam**: put the hi-res glyph pixels **into upstream's own
`_textSurface` at `_textSurfaceMultiplier = 2`**, the FM-Towns mechanism, so every erasure upstream already scales
(`gfx.cpp` 356, 1250-1270, 1535, 1603-1614, 2349, 4459; `scumm.cpp` 4279) erases the hi-res glyphs too, and keep upstream's
layout (the 1x `korean.fnt` metrics) so line breaking, centring and `_charset->_str` rectangles do not change. What the
slim seam cannot cover by itself (baseline section 5 item 7): text upstream draws **into the 1x virtual screen** (verbs,
the v1-v3 text line, `ignoreCharsetMask`) and **into the back buffer** (`_blitAlso`). Only real data can say whether
those cases are rare and handleable with one or two hooks, or everywhere (then S2 stops and S1 stays, DESIGN 13).

### 6. The spike (time box: 2 working days; scratch branch, never merged)

Branch `p6b-spike` in a scratch clone (`git -C $SRC worktree add ~/work/tmp/p6/spike` after P6a), build
`~/work/tmp/scummvm2026-p6spike`. Scope: **CLUT8 output, 1-bpp jamo face** (`data:hires_text/NEODG16A.SVF`, the engine's
fallback face, 16 px; P1), Korean only (Latin keeps the game charset, pixel-doubled), targets Loom CD, MI1 VGA, MI2,
Indy4, DOTT. No config UI, no RGB, no coverage plane: the questions are the seam's size and its artefacts.

#### Task b0: Spike base and measurements to take

**Files:** none. Record in `P6-notes.md` under "Spike": the base commit, the targets, the scenario list (Task b5), and
the metrics G1-G6 below as an empty table.

#### Task b1: m = 2 for a Korean DOS v4-v6 target

**Files (spike):** `engines/scumm/charset.cpp` (`loadKorFont`), `engines/scumm/gfx.cpp` (`drawStripToScreen`), new
`engines/scumm/hires_composite.{h,cpp}`.

- [ ] Set `_textSurfaceMultiplier = 2` in `loadKorFont()` when `_game.version >= 4 && _game.version <= 6 &&
  _game.platform == Common::kPlatformDOS && ConfMan.getBool("hires_text")` (the P1 key; default true). The screen then
  opens at 640x400 (`scumm.cpp:1487`), `_textSurface` and `_compositeBuf` at 2x (`scumm.cpp:1729`, `1821`).
- [ ] `drawStripToScreen` (gfx.cpp:686-770) composites a 1x game strip with a 2x text strip only for FM-Towns; the
  generic 8-bit loop reads the source at the text plane's width (wrong at m = 2). Add, before `// Compose the text over the
  game graphics`, a branch `if (m == 2 && _game.platform == Common::kPlatformDOS) { compositeStrip2x(...); src =
  _compositeBuf; pitch = width * m; x *= m; y *= m; width *= m; height *= m; goto blit; }` (in the plan proper: a helper
  call and an early copy, no `goto`), with

```cpp
// hires_composite.cpp (UNVERIFIED: written for the spike, not yet built)
void compositeStrip2x(const byte *src, int srcPitch, const byte *text, int textPitch, byte *dst, int width, int height) {
	for (int y = 0; y < height; y++) {
		for (int r = 0; r < 2; r++) {
			const byte *s = src + y * srcPitch;
			const byte *t = text + (2 * y + r) * textPitch;
			byte *d = dst + (2 * y + r) * width * 2;
			for (int x = 0; x < width; x++) {
				const byte bg = s[x];
				d[2 * x] = (t[2 * x] == CHARSET_MASK_TRANSPARENCY) ? bg : t[2 * x];
				d[2 * x + 1] = (t[2 * x + 1] == CHARSET_MASK_TRANSPARENCY) ? bg : t[2 * x + 1];
			}
		}
	}
}
```

- [ ] Measure: first screens of the five targets at 640x400 with the game charset doubled (`scale2x` path of
  `CharsetRendererTownsV3::drawBits1` shows how; the classic renderer needs the same doubling: count the lines).

#### Task b2: The glyph seam

**Files (spike):** `engines/scumm/charset.cpp` (`printCharIntern`, the `is2byte` branch at 1294-1296), new
`engines/scumm/hires_text.{h,cpp}` (spike-sized: < 300 lines).

- [ ] At the `is2byte` branch, when the hi-res object exists: decode the CP949 pair (`chr & 0xFF` lead, `chr >> 8` trail)
  to a code point (`Common::U32String(const char *, 2, Common::kWindows949)`), draw it with
  `Graphics::HiResFontSet::drawChar(cp, x2, yBaseline2, plane, colourIndex)` into a scratch `Graphics::TextPlane` cell and
  stamp coverage >= 128 into `_textSurface` at `(_left * 2, (_top - _screenTop) * 2)` **inside the 2x box of the 1x cell**
  (`_2byteWidth * 2` x `_2byteHeight * 2`), whatever the destination upstream chose (`_textSurface`, `*vs` or the back
  buffer). Shadow: draw the shadow colour at +1/+1 (2x) first when `_2byteShadow` (upstream's Korean shadow rule,
  `drawBits1Kor`).
- [ ] Count every destination taken: add a spike-only `debug(1, "HIRES dest %s", ...)` per call: `text`, `vs`, `back`.

#### Task b3: Text that upstream draws into the 1x screen or the back buffer

- [ ] `vs` destination (verbs, inventory names, the MI2 dialogue choices, the sentence line of v4+): the glyph now sits
  in `_textSurface`, which upstream composites over every virtual screen (`drawStripToScreen` uses
  `vs->topline + top`). Erasure: `restoreBackground()` fills `_textSurface` for a screen without two buffers with
  `backColor` at 2x (gfx.cpp:1263-1264), which is **opaque**, not transparent: count how often this hides game pixels
  that upstream would show (verb backgrounds are solid; expected harmless) and record it.
- [ ] `back` destination (`_blitAlso`): text meant to persist across `restoreCharsetBg()`. In `_textSurface` it would be
  cleared by `clearTextSurface()` (gfx.cpp:1325) where upstream keeps it. Count the cases per game in the scenario runs;
  the fix if needed is to draw such glyphs into a second 2x "persistent" layer cleared only by `initScreens`/room change
  (one more hook) — measure before writing it.

#### Task b4: Census test (stays after the spike if "go")

**Files:** `test/engines/scumm/hires_census.h`.

- [ ] A test that reads `engines/scumm/{gfx,charset,string,scumm,gfx_gui}.cpp` from the source tree (path from a
  `-DSCUMM_TEST_SRCDIR` define in `test/module.mk`, as `jamo`'s `hires_hook_census.h`) and counts the statements that
  write `_textSurface` (`fill(` / `fillRect(` / `memset(`/`memcpy(` with `_textSurface`) and the `printCharIntern`
  destination choices; it compares with a committed table (file, function, count). An upstream rebase that adds a write
  site fails the test, which is the rebase signal DESIGN 9 asks for.

#### Task b5: Gate runs

Scenario list per target (each a save made with the S1 build at that point, harness H5, or a scripted path; screenshots
before/after, viewed):

| # | Scenario | Loom | MI1 | MI2 | Indy4 | DOTT |
|---|---|---|---|---|---|---|
| 1 | dialogue over a moving actor, several lines | x | x | x | x | x |
| 2 | dialogue replaced by the next line (erase + redraw) | x | x | x | x | x |
| 3 | verbs / inventory redraw (hover, scroll arrows) | - | x | x | x | x |
| 4 | dialogue choices (MI2/Indy4/DOTT conversation menu) | - | x | x | x | x |
| 5 | room change while a line is up | x | x | x | x | x |
| 6 | cutscene skip (Esc) mid-line | x | x | x | x | x |
| 7 | save + load with a line up; GMM open/close | x | x | x | x | x |
| 8 | message banner (pause, `Ctrl+P`) over text | x | x | x | x | x |
| 9 | credits/titles over a picture (`_blitAlso` candidates) | x | x | x | x | x |
| 10 | palette cycling with text (MI2 room 33; Loom distaff) | x | - | x | - | - |
| 11 | screen transition (dissolve/scroll) with text | x | x | x | x | x |
| 12 | English target of the same build | x | x | x | x | x |

#### Task b6: Gate decision

**GO** only if all hold; else **NO-GO** (S1 stays the product; P6 closes after P6a; the spike report and census are
committed as docs):

| | Criterion | Measured how |
|---|---|---|
| G1 | Seam size: upstream lines changed for S2-min <= 300 in <= 5 upstream files, and **no new erasure hook** for the CLUT8 path (`gfx.cpp` fills untouched) | `git diff --stat` of the spike against P6a |
| G2 | Zero stale glyphs, zero missing glyphs, zero misplaced glyphs over scenarios 1-9 and 11 on all five targets | viewed screenshots, listed per scenario in the notes |
| G3 | The `vs`/`back` destinations: either zero artefacts as in G2, or each fixable with at most one hook (named, <= 20 lines) | Task b2/b3 counts |
| G4 | Palette cycling (scenario 10) correct in CLUT8 (expected: free, the index is in `_textSurface`) | MI2 room 33 screenshots over 3 s |
| G5 | English targets and a Korean target with `hires_text=false`: first frames identical to P6a's (sha256 of the screenshot after the same scripted steps, or viewed) | scenario 12 |
| G6 | Speed: no visible slowdown; `drawStripToScreen` at 2x no more than 2x the 1x time in a `perf` sample of MI2 room 33 | `perf record` on the spike binary, 20 s |

Write the decision, the table filled, and the screenshots list into `P6-notes.md`; owner sign-off before section 7.

### 7. Conditional on GO: the 2x plan (outline tasks; each expands like P6a at the start of P6b proper)

Every task: failing unit test first where a pure unit exists, exact files below, [data] step on the gate's saves, one
commit `SCUMM: ...` with the trailer. The code of the spike is rewritten, not merged.

| Task | Files | What | Test / verification |
|---|---|---|---|
| b7 | new `engines/scumm/hires_text.{h,cpp}`; `scumm.h` (+1 member), `charset.cpp` (`loadKorFont`) | `Scumm::HiResText`: decides on (DOS v4-v6 Korean, `hires_text`, a map from `Graphics::pickHiResMap` with `hires_text_map*` keys), owns the `HiResFontSet` per charset id (`[font.N]` = SCUMM charset id), sets m = 2 | unit: decision table over (version, platform, language, key, map present); [data] the five targets open at 640x400, English at 320x200 |
| b8 | new `engines/scumm/hires_composite.{h,cpp}`; `gfx.cpp` (one call in `drawStripToScreen`) | keyed 2x composite for DOS (b1's function, plus the 2-bytes-per-pixel path through `_16BitPalette`) | unit: composite of a 4x2 strip with a transparent/opaque text pattern, CLUT8 and RGB565, byte-exact |
| b9 | `charset.cpp` (`printCharIntern` is2byte branch, `drawBits1Kor`), `hires_text.cpp` | the glyph seam of b2: CP949 -> code point, glyph into `_textSurface` inside the 2x box of the 1x cell, shadow | unit: `HiResText::glyphBox()` (1x cell -> 2x box, baseline) for 8x8, 16x9, 24x12 cells; [data] scenario 1-2 |
| b10 | `charset.cpp` (classic renderer 1-byte path) | Latin at 2x: the game charset glyph pixel-doubled, or the map's Latin face when the map routes ASCII | unit: doubling of a 1-bpp row; [data] mixed lines (`Special Thanks 브래드 ...`) |
| b11 | as b3 found (at most one hook) | `vs`/`back` destinations | the gate counts, then 0 artefacts |
| b12 | `hires_text.cpp`; `gfx.cpp` (`clearTextSurface`, `restoreBackground`: one call each) | **AA (2 bpp) on RGB output**: a `Graphics::TextPlane` coverage beside `_textSurface`, composite via `Graphics::composeSpan` through the current palette; coverage cleared where `_textSurface` is cleared (two calls); the index-match rule (coverage counts only where the plane's index equals `_textSurface`'s) covers the other fill sites | unit: compose of a 2-bpp glyph over 3 palettes; [data] MI2/DOTT at `render_target=rgb565` |
| b13 | `palette.cpp` (`setDirtyColors` or `updatePalette`: one call) | RGB only: re-composite the strips whose text uses a changed index (`jamo:engines/scumm/hires_palette_users.h` as reference) | unit: rows-to-redraw from an index set; [data] MI2 room 33 at rgb565 |
| b14 | `gfx.cpp` transitions (`dissolveEffect`, `scrollEffect`; UNVERIFIED which ones read `_compositeBuf`) | transitions at m = 2 (`jamo`'s `expandStrip`) | [data] scenario 11 |
| b15 | `test/engines/scumm/hires_census.h` (from b4), `test/module.mk` (`SCUMM_TEST_SRCDIR`) | the census table final | `make test` |
| b16 | `PATCHES.md`, notes | records; `ko/05-scumm` rows for every S2 hook | `git diff --stat` = the table |
| b17 | none | smoke: scenario list on all five targets, CLUT8 1-bpp and RGB AA; English | viewed; checkpoint C |

Review checkpoint B after b11 (CLUT8 complete, the product's default on DOS), checkpoint C after b17 (with an ASan run of
MI2 scenarios 1-7). **S2 acceptance:** MI1, MI2, Loom, Indy4, DOTT first screens + dialogue at 2x with SVF faces; MI2
room 33 palette cycling correct (CLUT8 and RGB); English targets unchanged; G1's upstream-line budget held (<= 600 lines
for all of S2).

---

## 8. How each check is verified with the data on this host

| Check | Data | Kind |
|---|---|---|
| UTF-8 bundle plays | MI2 pack (`dist/...mi2...zip`), MI1 UTE bundle on MI1 CD data | run, viewed |
| conversion = CP949 stage | `gamedata/mi2kor/korean.trs{,.cp949}`, `gamedata/mi1ute-kor/korean.trs{,.cp949}` | a10 suite + `trscheck.py equal` |
| no encoding.dat | same, no `extrapath` | run |
| `ko.trs`, `ko/` | scratch copy of the MM pack | `--detect` + run |
| font guard | unit; Indy4 lines with the 37 codes | unit; in-play UNVERIFIED |
| sentence line | MM pack ("What is" + sign) | run, viewed; two-object line UNVERIFIED |
| trailer | `saves/monkey2-ko.s02` (old), autosave round trip, plain build | runs |
| jamo UTF-8 saves | none on the host | UNVERIFIED (H6) |
| S2 gate | the five v4-v6 packs + saves at the scenarios | runs, viewed, counts |

## 9. UNVERIFIED (as of this plan)

1. `openFile()` / `Common::File` find `ko/ko.trs` and `ko/korean0N.fnt` after `addSubDirectoryMatching` (Task a5).
2. Two-object v2 sentence lines in play (`왕에게 티켓 주기`); Zak's sentence line; TTS of the Korean line.
3. A `jamo` UTF-8 save converted on load (no such save on the host).
4. Indy4's out-of-font codes reached in play (blank instead of garbage).
5. MI1 UTE in-game text on the real UTE data (only MI1 CD data here).
6. The a5 search path does not change English games (it is gated, but `addSubDirectoryMatching` on a missing folder is
   expected to be a no-op: check the log).
7. All of P6b section 7 code, and `compositeStrip2x` of the spike (written, not built).
8. Whether `_blitAlso` text exists in the five S2 targets at all, and how often (Task b3).
9. Which transition effects read `_compositeBuf` at m = 2 (b14).
10. N0 of the SCUMM+SCI build at `a2babee65a9` (the SCUMM-only N0 = 637 is measured).

## 10. Conflicts with DESIGN.md

1. **Runtime encoding** (DESIGN 3, policy points 2-3: text enters as UTF-8, the hi-res layer gets UTF-8): SCUMM keeps
   CP949 in memory for S1 (as DESIGN 3 allows for S1) **and for S2** (decision 0.3.1): the hi-res seam decodes CP949
   pairs. Consequence: S2 can show only what CP949 encodes (all 11,172 syllables via the extension, if the S2 transcoder
   then keeps extension codes for hi-res instead of `?`; a per-target switch in b7).
2. **`Scumm::TextMode`** (DESIGN 3 policy 1): not introduced in P6a; S2 adds `HiResText` only.
3. **DESIGN 11 row P6 tests** "text_char_length, debugsocket cond": not in this plan; `text_char_length` (UTF-8 lengths)
   has no use with a CP949 runtime, the debug socket is `ko/07` and not on the branch. The hook census is in P6b (b4/b15).
4. **Lazy `TrsStore`** (DESIGN 8 "memory switches kept", brief "if needed"): deferred to P7 with a measurement.
5. **Language folder** (DESIGN 4: files in `ko/`, detection adds a `-ko` variant): P6a accepts `ko/` (Task a5) but adds no
   `-ko` target variant: SCUMM detection already reports Korean from the bundle, and one folder is one game in SCUMM's
   detector (`detectLanguage` returns one language).
6. **DESIGN 2.3 "UTF-8 bundles (BOM or `text_encoding=utf8`)"**: BOM only; no shipped UTF-8 bundle lacks the BOM
   (measured: MI2, MI1 UTE).
7. **DESIGN 11 P6 "Done when"** lists palette cycling in MI2 room 33 for P6 as a whole; with a NO-GO it does not apply
   (CLUT8 1x upstream path cycles as upstream does).
8. **Unknown characters**: DESIGN 4 "untranslated string shown as the original"; for a translated string with a
   character the 1x font lacks, this plan shows `?` (the old CP949 stage showed garbage or read outside the font).

## 11. Harness-side dependencies (owner's data work)

| # | What | Needed by |
|---|---|---|
| H1 | Nothing for MI1/MI2: the UTF-8 `KOREAN.TRS` they ship is what P6a reads. Keep shipping it with the BOM | a4 |
| H2 | `trscheck.py census/equal/shared` in the SCUMM builders (`build_scumm.py`, `build_mi1ute.py`, `build_mi2ute.py`): refuse a bundle with a shared original/translation string; report out-of-font characters (MI2 2, MI1 UTE 1, Indy4 37) and fix them in the masters (`trs_master.py`) where a real syllable was meant | a11, a6 |
| H3 | `encoding.dat` reachable for UTF-8 packs: desktop installs carry it; the DOS engine zip's `DATA\` must keep it (P7); state it in README.TXT | a4 |
| H4 | MM/Zak: every OBCD object name has a bundle line (the 2026-10-15 fill covered `kind: object`; a builder check that lists room objects without a translation) | a8 |
| H5 | Saves at the gate scenarios for Loom, MI1, MI2, Indy4, DOTT (made with the P6a build) and one MM/Zak save with an inventory item for the two-object sentence line | a8, b5 |
| H6 | If any `jamo`-era UTF-8 Korean save exists (MI1 UTE / MI2 after the UTF-8 switch), a copy for Task a9 | a9 |
| H7 | MI1 UTE: the matching UTE game data, or accept credits-only verification | a13 |
| H8 | S2 only: SCUMM maps re-paired to OFL faces in map v3 (`HIRESBIT.MAP` 1-bpp first, then `HIRESVF.MAP`), per charset id with the 2x cell sizes of each game's `koreanNN.fnt` (MM 8x8 -> 16 px, MI2 font 0 12 px -> 24 px, ...); `.HOT` preload lists for P7 | b7 |
| H9 | Decide whether packs move `KOREAN.TRS` into `ko/ko.trs` (optional; both work after a5) | a5 |

## 12. Scratch tools used (not in the repository)

`~/work/tmp/p6/bin/{mkrun.sh,sess.sh,autoshots.sh,grid.py}`, `~/work/tmp/p6/py/{trsinfo,trsenc,trs2cp949}.py`, the
prototype `~/work/tmp/p6/tree` + `proto.diff`, runs in `~/work/tmp/p6/runs/`. `sess.sh` takes `XARGS` (extra command-line
arguments, e.g. `--save-slot=2`), `SCALE`, `WAIT`, `DBG` from the environment.
