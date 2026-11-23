# P6: SCUMM — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task by task. Steps use checkbox (`- [ ]`) syntax for tracking.

Revision 2 (2026-11-23; the Python model of Task a12 also checked: converting `mi2kor/korean.trs.cp949` by decision 0.3.2 gives `korean.trs` for 8780/8780 lines, MI1 UTE 5347/5348): **SCUMM holds Korean text as UTF-8 inside the engine** (owner: "내부적으로는 UTF-8 로 가야함",
DESIGN.md section 3). Revision 1 (CP949 at run time) is rejected; its text is kept in scratch
(`~/work/tmp/p6/plan/P6-scumm.cp949-version.md`) for the record only.

**Goal:** DESIGN.md section 11 row P6 and decision 6. **S1 (P6a):** every Korean SCUMM v1-v6 DOS game of the harness
plays with its text held as UTF-8: CP949 bundles (MM, Zak, Indy3, Loom, Indy4) and the Korean inside MI1/DOTT's data are
converted once where text enters the engine; UTF-8 bundles (MI2, MI1 UTE: today an assertion) are used as they are;
upstream's `korean.fnt` fonts keep working through a code point -> KS X 1001 table; the v2 sentence line (MM, Zak) is in
Korean order; saves hold UTF-8 and old CP949 saves convert on load. **S2 (P6b):** a gate decides whether 2x hi-res
text (SVF faces taking code points; DOTT in Single Day AA as the owner's acceptance item) goes ahead.

**Architecture:** one boundary, `ScummEngine::translateText()`, which upstream already calls wherever script text becomes
an engine string for a Korean target (`loadPtrToResource()`, `convertMessageToString()` and, through it, every name,
verb and string piece): its output is always UTF-8 (`toEngineText()`: valid UTF-8 copied, anything else read as CP949,
so it is idempotent). CP949 is never held. The text layer reads UTF-8 at the five places upstream combines two bytes
(`nextCJKChar()`, `cjkTrailBytes()`); `get2byteCharPtr()` maps a code point to a `korean.fnt` glyph through a static
2350-entry table (no `encoding.dat` for Hangul), and refuses anything else visibly. Pure units carry the logic and the
tests; upstream files get about +84/-26 lines (prototype measured).

**Tech stack:** C++11 ScummVM code, CxxTest, a generated KS X 1001 table (`tools/korean/gen_ksx1001.py`), Python 3 stdlib
for the checker, Xvfb + xdotool + Alt+S screenshots; S2: the P0/P1 hi-res core.

Source of scope: DESIGN.md sections 2.3, 3, 4, 5, 9, 10, 11 (row P6), 12 (decisions 6, 10, 11); `P3-sci-kq1.md`,
`P4-sci-data-games.md` + notes (working method; P3's review classes: buffer bounds, terminator off-by-one);
`PATCHES.md`; `P6-scumm-baseline.md` (Part 1, read it first); branch `jamo` (read only); harness `~/work/scummvm` (read only).

---

## 0. Facts checked for this plan (2026-11-23)

Tree `/home/thkim/work/scummvm-2026.4-korean`, branch `ko-2026.4`. Base of this plan: **`a2babee65a9`** or later with
`engines/scumm` unchanged against `v2026.4.0` (true up to `a4e3435ad26`, the head when this revision was written). N0 = 637
(SCUMM-only build at `a2babee65a9`, measured); the prototype below was built at `4a8b9907b2c` (N0 there 620).

### 0.1 What works today (from `P6-scumm-baseline.md`)

| Set | Today (branch = plain 2026.4.0) | After P6a |
|---|---|---|
| MM V2, Zak V2 (CP949 bundle, 8x8 font) | Korean; sentence line `이게 뭐지? sign` | UTF-8 inside; `이게 뭐지? 표지판` |
| Indy3, Loom, Indy4 (CP949 bundle) | Korean | UTF-8 inside, same picture |
| MI1 VGA floppy, DOTT floppy (CP949 inside the data) | Korean | UTF-8 inside, same picture (MI1: one width fix) |
| **MI2 pack, MI1 UTE (UTF-8 bundle)** | **assertion `charOffs < 0x14000`** | Korean |
| MM V1 English | English | unchanged (no Korean path) |

### 0.2 Prototype (built and run before this revision was written)

`~/work/tmp/p6/tree` = `git archive 4a8b9907b2c` + the code of this plan + the S2 spike; diff
`~/work/tmp/p6/proto-utf8.diff`; S1 upstream hunks alone: `~/work/tmp/p6/utf8-s1-upstream.diff` (appendix A). Built in
`~/work/tmp/p6/build-tree` (SCUMM only): `Running cxxtest tests (635 tests)...OK!` (620 + 15), no warning.
Runs in `~/work/tmp/p6/runs/u8/`, `u8cmp/`, `dott-sd-*` (no `extrapath`, so **no `encoding.dat`**, unless stated):

| Check | Result |
|---|---|
| MI2 pack as shipped (UTF-8 bundle) | no assertion, Korean dialogue, credits, captions; nothing converted |
| MI2 + CP949 bundle | `CP949 body converted to UTF-8, 738122 -> 841833 bytes`, 0 unreadable |
| MM, Zak, Indy3 bundles | converted (`50116 -> 58803`, `87138 -> 102032`, `126521 -> 148571` bytes), 0 unreadable |
| Indy4 bundle | `813252 -> 821300` bytes, **7** pairs unreadable without `encoding.dat` (`B3 14`, `CE 14`, `BC 62`, ...: bytes of 4 garbage lines of the bundle), **5** with it |
| MI1 floppy (CP949 in the data) | Korean credits and dialogue; 0 unreadable; the charset glyph 0xFA (MI1's narrow separator) kept as U+00FA |
| DOTT floppy (CP949 in the data) | Korean subtitles, verbs |
| **same picture as the CP949 path** | MI2 old save slot 2 (verbs + inventory): **full 640x400 frame identical** to `build-head` (upstream CP949); slot 6 (dialogue choices): identical except a 30x30 box at the cursor. MI1 credit line with 0xFA separators: **1 px shift at 1x**: upstream measures 0xFA as a 2-byte character and swallows the next byte, the UTF-8 path measures the glyph it draws (`scumm-utf8-mi1-credit-width-fix.png`) |
| MM sentence line | `이게 뭐지? 표지판` |
| old CP949 save (`saves/monkey2-ko.s02`) | `converted 20 text resources of a CP949 save to UTF-8`, frame identical to upstream's load of the same save |
| new save | autosave ends `TENC\x01`; reloads; **plain 2026.4.0 loads it and stops at the first Korean verb with the same assertion** (`charOffs < 0x14000`): a UTF-8 save is not readable by upstream |
| English MM V1 | no Korean/UTF-8/TENC line in the log, English text |
| S2 spike, DOTT, Single Day AA on the UTF-8 path | RGB555/RGB565 and CLUT8 1 bpp as in section 5a; the glyph seam takes the code point directly |
| buffer audit (`~/work/tmp/p6/py`) | longest translation as UTF-8: MM 251, Zak **295**, Indy3 257, Loom 233, Indy4 337, MI2 161, MI1 UTE 222 bytes; Zak and Indy3 exceed `drawString()`'s `buf[270]`, where `convertMessageToString()` calls `error()` on a full buffer (string.cpp:1704): the buffer becomes 512 (Task a6) |

### 0.3 Decisions (each re-checkable)

1. **UTF-8 inside, CP949 only as input** (owner). Boundary = `translateText()` (all script text for a Korean target goes
   through it upstream, `resource.cpp:1233`, `string.cpp:1591`, and the verb/name/string pieces of
   `convertMessageToString()` recurse into it) + `loadLanguageBundle()` for the bundle. `toEngineText()` is idempotent, so
   a stored UTF-8 resource passing again is unchanged.
2. **Upstream's pairing rule for CP949 input**: a lead 0xB0-0xD0 takes the next byte (`is2ByteCharacter()`, `charset.h:60`);
   every other byte >= 0x80 is a glyph of the game's charset and becomes U+0080..U+00FF, which the renderer draws from the
   charset again. This keeps the 1x picture (MI1's 0xFA). Hanja/extension pairs need `encoding.dat`; else `?`.
3. **Static KS X 1001 table** (2350 code points, 4.7 KB, generated): Hangul needs no `encoding.dat`. `encoding.dat` is
   needed only for CP949 Hanja/symbol/extension pairs (Indy4: 2 of 7), which `korean.fnt` cannot draw anyway.
4. **Refusal**: a code point `korean.fnt` lacks is drawn as a `?` scaled into the 2-byte cell (metrics unchanged), logged
   once. In S2 the SVF chain draws it instead.
5. **Saves hold UTF-8**, marked by the trailer `TENC` 1 (jamo's format) for every Korean v1-v6 target; no trailer or 0 =
   CP949, converted on load. Upstream cannot read a new Korean save (measured): stated in the notes and README.
6. FT/Dig/COMI (v7/v8) keep upstream's CP949 path (`_koreanUtf8` only for `_game.version < 7`).
7. No lazy `TrsStore` (P7 measures), S2 targets v4-v6 DOS (DESIGN row P6).

### 0.4 ScummVM APIs and sites (file:line at `a2babee65a9`)

| Site | Where |
|---|---|
| `loadLanguageBundle` 2333 (`openFile(file, "korean.trs")` 2340, body read 2404-2406), `translateText` 2445 (copies 2475, 2492, 2503, 2511) | `engines/scumm/string.cpp` |
| CJK combine: CHARSET_1 1262-1266, `drawString` `buf[270]` 1355 + combine 1539-1541, wrap prediction 881, particles 1904-1922 / 1971-1990, `convertMessageToString` overflow `error()` 1704 | `string.cpp` |
| `getStringWidth` CJK branch 556-575, `addLinebreaks` 651-700, `get2byteCharPtr` 248 (KO 254), `loadKorFont` 186, `printChar` 1080 | `engines/scumm/charset.cpp` |
| `checkKSCode` 42, `is2ByteCharacter` 60 | `engines/scumm/charset.h` |
| `loadPtrToResource` 1217 (translation 1233-1243) | `engines/scumm/resource.cpp` |
| `saveState` 626 (serializer 642-644), `loadState` 683 (827-830) | `engines/scumm/saveload.cpp` |
| `_charsetBuffer[512]` 1719, `_existLanguageFile` 1821, `isScummvmKorTarget()` 1792, `translateText` 1773 | `engines/scumm/scumm.h` |
| `ResourceManager::createResource` 194, `_types` 177 | `engines/scumm/resource.h` |
| `detectLanguage` 216 (`korean.trs` 237) | `engines/scumm/detection_internal.h` |
| `Common::translationTableName` 38 | `common/language_folder.h` |
| `U32String(const char *, uint32, CodePage)` 83 (only for non-KS CP949 pairs) | `common/ustr.h` |

---

## 1. File map

| File | Task | New / modified | Responsibility |
|---|---|---|---|
| `tools/korean/gen_ksx1001.py` | a1 | new | writes `engines/scumm/ksx1001_table.cpp` |
| `engines/scumm/ksx1001_table.cpp` | a1 | new (generated) | `kKsx1001Hangul[2350]` |
| `engines/scumm/korean_text.{h,cpp}` | a1-a3 | new | table lookups, escapes, UTF-8 decode/encode, CP949 -> UTF-8, the `toEngineText()` boundary, finals |
| `engines/scumm/trs_bundle.{h,cpp}` | a4 | new | names, BOM, CP949 body -> UTF-8 |
| `engines/scumm/korean.cpp` | a5-a10 | new | engine glue: bundle conversion, `copyTranslatedText`, `nextCJKChar` and friends, refusal glyph, sentence line, save conversion |
| `engines/scumm/korean_sentence.{h,cpp}` | a8 | new | v2 sentence composer (UTF-8) |
| `engines/scumm/saved_text.{h,cpp}` | a10 | new | trailer |
| `engines/scumm/{string.cpp,charset.cpp,scumm.h,scumm_v2.h,saveload.cpp,detection_internal.h,scumm.cpp,module.mk}` | a5-a11 | modified | hooks (appendix A) |
| `test/module.mk`, `test/engines/scumm/{korean_text,trs_bundle,korean_sentence,saved_text,korean_trs_data}.h` | a1-a12 | new / modified | tests |
| `tools/korean/trscheck.py` + test | a12 | new | bundle census, CP949 -> UTF-8 lengths, equivalence with a UTF-8 stage |
| `docs/korean/PATCHES.md`, `docs/korean/plans/P6-notes.md` | a13 | modified / new | records |

## 2. Environment (every task)

```sh
SRC=/home/thkim/work/scummvm-2026.4-korean
OLD=/home/thkim/work/scummvm-korean            # read only: git -C $OLD show jamo:<path>
HAR=/home/thkim/work/scummvm                   # read only
BLD=~/work/tmp/scummvm2026-p6build             # out of tree; never configure inside $SRC
P6=~/work/tmp/p6                               # scratch: data/, runs/, bin/, py/, tree/ (the prototype)
mkdir -p $BLD && cd $BLD
heavy $SRC/configure --disable-all-engines --enable-engine=scumm --enable-debug --disable-optimizations
```

Build/test: `cd $BLD && heavy make -j8 scummvm test`. A SCUMM+SCI build (`~/work/tmp/scummvm2026-p6sci`) and an ASan build
run at the checkpoints. Commit messages: `AREA: Subject`, body, exactly one trailing line `Assisted-by: Claude Code`, no
`Co-Authored-By`; check with `python3 -I devtools/check-commit-msg.py`. GPL header on every new file (copy from
`engines/scumm/ks_check.h`). Runs: `$P6/bin/{mkrun.sh,autoshots.sh,sess.sh}` as in revision 1 (section 12): no
`extrapath` unless a step says so (the point is that Hangul needs no `encoding.dat`). Data: `$P6/data/*` (Part 1).

Code of every new file is in appendix B (copied from the prototype, which built and passed); upstream hunks in appendix A.
Tasks quote the parts each step needs.

---

## P6a: S1, Korean text as UTF-8 inside the engine

### Task a0: Base, N0, interfaces, data, red

**Files:** none.

- [ ] **Step 1:** `git -C $SRC diff v2026.4.0.. -- engines/scumm test/engines/scumm` is empty; configure `$BLD`,
  `heavy make -j8 scummvm test` -> record N0 (`Running cxxtest tests (N0 tests)...OK!`; 637 at `a2babee65a9`).
- [ ] **Step 2: interfaces** — each prints at least one line:

```sh
cd $SRC/engines/scumm
grep -n 'openFile(file, "korean.trs");' string.cpp
grep -n "c += \*buffer++ \* 256; //LE" string.cpp
grep -n "c += buf\[i++\] \* 256;" string.cpp
grep -n "byte buf\[270\];" string.cpp
grep -n "error(\"convertMessageToString: buffer overflow\")" string.cpp
grep -n "memcpy(trans_buff, translatedText, MIN<int>(resStrLen(translatedText) + 1, transBufferSize));" string.cpp   # 3 lines
grep -n "} else if (_useCJKMode && curChar & 0x80) { // CJK char" string.cpp
grep -n "if (checkKSCode(k1, k2)) {" string.cpp                                       # 2 lines
grep -n "idx = ((idx % 256) - 0xb0) \* 94 + (idx / 256) - 0xa1;" charset.cpp
grep -n "lastKoreanLineBreak = pos - 2;" charset.cpp
grep -n "return (c >= 0xB0 && c <= 0xD0);" charset.h
grep -n "saveLoadWithSerializer(ser);" saveload.cpp                                  # two
```

- [ ] **Step 3: red** — the MI2 pack as shipped stops the base build:

```sh
R=$P6/runs/a0-mi2; $P6/bin/mkrun.sh $R mi2ko monkey2 $P6/data/mi2/MI2/GAMES/MI2 language=ko platform=pc
SCALE=2 $P6/bin/autoshots.sh $R mi2ko $BLD/scummvm click:48,128 sleep:15 shot; grep -c "charOffs < 0x14000" $R/run.log   # 1
```

No commit.

### Task a1: The KS X 1001 table (and the SCUMM test suites)

**Files:** create `tools/korean/gen_ksx1001.py`, `engines/scumm/ksx1001_table.cpp`, `engines/scumm/korean_text.h`
(declarations of the table and `ksx1001Index`/`ksx1001CodePoint` only, the rest grows in a2/a3),
`engines/scumm/korean_text.cpp`, `test/engines/scumm/korean_text.h`; modify `engines/scumm/module.mk`, `test/module.mk`.

- [ ] **Step 1: test module** — `test/module.mk`, before `ifeq ($(ENABLE_TWINE), STATIC_PLUGIN)`:

```make
ifeq ($(ENABLE_SCUMM), STATIC_PLUGIN)
	TESTS += $(srcdir)/test/engines/scumm/*.h
	TEST_LIBS += engines/scumm/libscumm.a common/libcommon.a
endif

```

- [ ] **Step 2: failing test** — `test/engines/scumm/korean_text.h` with `test_ksx1001_table` (appendix B.9; the helper
  `engineText()` and the other tests are added in a2/a3).
- [ ] **Step 3: red** — `engines/scumm/korean_text.h: No such file or directory`.
- [ ] **Step 4: generator** — `tools/korean/gen_ksx1001.py` (appendix B.1); run
  `python3 -I tools/korean/gen_ksx1001.py engines/scumm/ksx1001_table.cpp engines/scumm/ks_check.h` (second argument:
  the file whose first 20 lines are the GPL header). It asserts that the 2350 syllables in row order are ascending
  code points (verified: `2350 True 0xac00 0xd79d`), so the index is a binary search and the reverse an array read.
- [ ] **Step 5: implement** — `korean_text.h`/`.cpp` with `kKsx1001Hangul`, `ksx1001Index`, `ksx1001CodePoint`
  (appendix B.2/B.3, first two functions); `module.mk`: `korean_text.o ksx1001_table.o` after `ks_check.o`.
- [ ] **Step 6: green** — `N0 + 1 ... OK!`.
- [ ] **Step 7: commit** — `SCUMM: Add the KS X 1001 Hangul table of korean.fnt` (body: why a table: korean.fnt glyph
  numbers and CP949 Hangul without encoding.dat; generated, ascending; trailer).

### Task a2: UTF-8, escapes, CP949 -> UTF-8

**Files:** `engines/scumm/korean_text.{h,cpp}`, `test/engines/scumm/korean_text.h`.

- [ ] **Step 1: failing tests** — `test_cp949_to_utf8_keeps_escapes`, `test_utf8_judgement` and the helper `engineText()`
  (appendix B.9). The first checks `한 FF 04 B0 A1 글` -> UTF-8 with the escape's arguments untouched, and `BC FE` (a
  syllable whose trail is 0xFE) followed by the escape `FE 01`.
- [ ] **Step 2: red** — `'cp949ToUtf8' is not a member of 'Scumm'`. (In this task the two tests call `cp949ToUtf8()`
  and `textIsUtf8()` directly; Task a3 introduces `engineText()`/`toEngineText()` and rewrites them to the appendix B.9
  form.)
- [ ] **Step 3: implement** — `scummEscapeLength`, `scummTextEnd`, `decodeUtf8`, `appendUtf8`, `textIsUtf8`,
  `cp949ToUtf8` (appendix B.3). `cp949ToUtf8` follows upstream's pairing (decision 0.3.2); a non-KS pair asks
  `Common::U32String(pair, 2, kWindows949)` and refuses `?`/U+FFFD answers (what a missing `encoding.dat` gives).
- [ ] **Step 4: green** — `N0 + 3`.
- [ ] **Step 5: commit** — `SCUMM: Read Korean CP949 text as UTF-8` (trailer).

### Task a3: The engine-text boundary

**Files:** same.

- [ ] **Step 1: failing tests** — `test_boundary_is_idempotent`, `test_cut_on_a_character` (a 6-byte buffer keeps `한` of
  `한글`; an escape is never cut), `test_final_bits` (appendix B.9).
- [ ] **Step 2: red** — `'toEngineText' is not a member`.
- [ ] **Step 3: implement** — `toEngineText`, `koreanFinalBits` (appendix B.3). Discipline from the P3 review: the
  output always ends in a NUL inside `dstSize`, the cut is at a character or escape boundary, never inside.
- [ ] **Step 4: green** — `N0 + 6`.
- [ ] **Step 5: commit** — `SCUMM: Add the boundary where Korean text becomes UTF-8` (trailer).

### Task a4: Bundle names and the CP949 body

**Files:** create `engines/scumm/trs_bundle.{h,cpp}`, `test/engines/scumm/trs_bundle.h`; `module.mk` (`trs_bundle.o`).

- [ ] **Step 1: failing tests** — `test_names`, `test_nul_ends_a_bundle_string` (MM's original `sound FF 04 C8 00`: the
  string ends at that NUL, as the bundle builder wrote it), `test_body_to_utf8` (appendix B.10).
- [ ] **Step 2: red** — `trs_bundle.h: No such file`.
- [ ] **Step 3: implement** — appendix B.4/B.5. Strings of the body are NUL-split (an escape-aware split broke MM's
  bundle in the prototype: `malformed`, then English verbs); translations are converted, originals and the tail copied;
  the result gets a BOM.
- [ ] **Step 4: green** — `N0 + 9`.
- [ ] **Step 5: commit** — `SCUMM: Convert a CP949 translation bundle to UTF-8 at load` (trailer).

### Task a5: `_koreanUtf8`, the bundle, the boundary in `translateText()`

**Files:** create `engines/scumm/korean.cpp` (appendix B.8, functions `transcodeLanguageBundle`, `copyTranslatedText`);
modify `string.cpp`, `charset.cpp` (`loadKorFont`), `scumm.h` (appendix A).

- [ ] **Step 1: hooks** — appendix A: `loadKorFont()` sets `_koreanUtf8 = (_game.version < 7)`; `loadLanguageBundle()`
  tries `trsBundleNames()` and converts a body without BOM; the four `memcpy(trans_buff, ...)` of `translateText()`
  become `copyTranslatedText(trans_buff, X, transBufferSize)`; `scumm.h` declarations.
- [ ] **Step 2: build** — `N0 + 9 ... OK!`, no warning.
- [ ] **Step 3: [data]** — without `extrapath`: MM, Zak, Indy3, Indy4, MI2cp: `CP949 body converted to UTF-8` with the
  sizes of section 0.2; MI2 pack: no conversion line. (Drawing needs a6; until then Korean shows as `?` or garbage: do
  not judge pictures here.)
- [ ] **Step 4: commit** — `SCUMM: Hold Korean text as UTF-8` (body: the boundary, why translateText, idempotence, v7+
  untouched; trailer).

### Task a6: Read UTF-8 where text is measured, broken and drawn

**Files:** `korean.cpp` (`nextCJKChar`, `cjkTrailBytes`, `koreanCharsetGlyph`, `koreanCharStart`, `koreanBreakableAt`,
`koreanCharBefore`, `koreanRefusalGlyph`), `charset.cpp`, `string.cpp`, `scumm.h` (appendix A).

- [ ] **Step 1: hooks** — the five byte sites: CHARSET_1 and `drawString` combine through `nextCJKChar()`; wrap
  prediction, `getStringWidth()` and `addLinebreaks()` step with `cjkTrailBytes()`/`koreanCharsetGlyph()`;
  `addLinebreaks()`'s Korean break rule uses `koreanCharStart`/`koreanBreakableAt`/`koreanCharBefore` (same rule as
  upstream, positions from the character start instead of fixed byte offsets); `get2byteCharPtr()` takes a code point
  through `ksx1001Index()` and returns `koreanRefusalGlyph()` for none (also a bound for FT's CP949 index);
  `drawString()`'s `buf[270]` becomes 512 (section 0.2 buffer audit).
- [ ] **Step 2: build + tests** — `N0 + 9 ... OK!`.
- [ ] **Step 3: [data] same picture** — two loads of the same old save on the same CP949 data, `build-head` (upstream)
  vs `$BLD`:

```sh
for b in $P6/build-head/scummvm $BLD/scummvm; do for s in 2 6; do r=$P6/runs/a6-mi2s$s-$(basename $(dirname $b))
  $P6/bin/mkrun.sh $r monkey2-ko monkey2 $P6/data/mi2cp language=ko platform=pc; cp $HAR/saves/monkey2-ko.s0$s $r/
  XARGS=--save-slot=$s SCALE=2 $P6/bin/autoshots.sh $r monkey2-ko $b sleep:6 shot; done; done
python3 - <<'EOF'   # prototype: slot 2 identical, slot 6 differs only in the cursor box
import glob; from PIL import Image, ImageChops
for s in '26':
    a,b=[sorted(glob.glob(f'/home/thkim/work/tmp/p6/runs/a6-mi2s{s}-{d}/*.png'))[-1] for d in ('build-head','scummvm2026-p6build')]
    print(s, ImageChops.difference(Image.open(a).convert('RGB'), Image.open(b).convert('RGB')).getbbox())
EOF
```

  Then the smoke of the baseline (MI2 pack, MM, Zak, Indy3, Indy4, Loom, MI1, DOTT): Korean on every screen, no
  `Assertion`, `has no glyph` at most once per run (MI2: none expected; Indy4: the garbage lines only if reached). MI1:
  the credit line with 0xFA is 1 px (1x) further right than upstream: record it as the deliberate width fix.
- [ ] **Step 4: commit** — `SCUMM: Measure and draw Korean UTF-8 text` (body: the five sites, the table, the refusal
  glyph, the drawString buffer and why; trailer).

### Task a7: Particles of MI1/MI2 from the shown name

**Files:** `string.cpp` (`convertNameMessage`, `convertStringMessage`; appendix A).

- [ ] **Step 1:** in UTF-8 mode `_krStrPost = koreanFinalBits(dst, dst + increment)`: the final of the name as shown
  (upstream read the raw name, which for a translated OBCD name is the English key and gave the vowel form). The particle
  codes (`codeIdx`, `codeWalkTo`, CP949 bytes) need no change: they pass `convertMessageToString()` -> `translateText()`
  and arrive as UTF-8.
- [ ] **Step 2:** tests unchanged (`koreanFinalBits` is tested in a3); **[data]** MI2: hover an inventory object with
  "Pick up" and read `<name>을/를 ...` (UNVERIFIED in the prototype: not reached).
- [ ] **Step 3: commit** — `SCUMM: Choose Korean particles from the name shown` (trailer).

### Task a8: The v2 sentence composer (UTF-8)

**Files:** create `engines/scumm/korean_sentence.{h,cpp}`, `test/engines/scumm/korean_sentence.h` (appendix B.6/B.7/B.11);
`module.mk`.

- [ ] **Step 1:** tests first (four: finals and particles, compose, fit never cuts a character, applies) -> red
  (`korean_sentence.h: No such file`); implement; green `N0 + 13`.
- [ ] **Step 2: commit** — `SCUMM: Add the Korean word order of the v2 sentence line` (trailer).

### Task a9: Hook the sentence line

**Files:** `korean.cpp` (`drawKoreanSentence`), `string.cpp` (3 lines in `drawSentence`), `scumm_v2.h` (appendix A, B.8).

- [ ] **Step 1:** hook + function; `N0 + 13 ... OK!`.
- [ ] **Step 2: [data]** — MM (`sess.sh`: pick two kids, START, Esc twice, "이게 뭐지?" at 280,342, hover 60,215):
  `이게 뭐지? 표지판` (prototype: shown). Two-object lines: UNVERIFIED in play (H5).
- [ ] **Step 3: commit** — `SCUMM: Draw the Korean v2 sentence line in Korean order` (trailer).

### Task a10: Saves hold UTF-8

**Files:** create `engines/scumm/saved_text.{h,cpp}`, `test/engines/scumm/saved_text.h` (appendix B.12); `korean.cpp`
(`convertLoadedText`); `saveload.cpp`; `module.mk`.

- [ ] **Step 1:** tests (`test_trailer_round_trip`, `test_refusal_index`) -> red -> implement -> `N0 + 15`.
- [ ] **Step 2: [data]** — (a) old CP949 save `monkey2-ko.s02` on MI2cp: `converted 20 text resources of a CP949 save to
  UTF-8`, frame identical to upstream's load (prototype); (b) autosave after 20 s ends `TENC\x01` and reloads; (c) plain
  2026.4.0 loading (b): `State loaded`, then `Assertion charOffs < 0x14000` at the first Korean verb (prototype): write it
  into P6-notes.md and the pack README text the harness ships (H11).
- [ ] **Step 3: commit** — `SCUMM: Save Korean text as UTF-8 and convert older saves` (trailer).

### Task a11: `ko.trs` and the `ko/` folder

As revision 1 Task a5 (unchanged design, not prototyped): `detection_internal.h` accepts `ko.trs` and `ko/{ko,korean}.trs`;
the constructor adds `SearchMan.addSubDirectoryMatching(gameDataDir, "ko")` for `_language == Common::KO_KOR` in the
"Add default file directories." block (`scumm.cpp` after 1054). [data]: a scratch MM copy with `ko/ko.trs`.
Commit `SCUMM: Find a Korean translation in the ko folder`.

### Task a12: Developer-data check and `trscheck.py`

- `test/engines/scumm/korean_trs_data.h`: with a folder `korean-trs/` in the runner's working directory holding
  `<name>.cp949.trs` and `<name>.utf8.trs` pairs (MI2: `gamedata/mi2kor/korean.trs.cp949` / `korean.trs`; MI1 UTE the
  same), converting the CP949 one gives the UTF-8 one's translations line for line (the UTF-8 files are the harness's
  conversion of the same text). Expected from the Python model: MI2 8780/8780, MI1 UTE 5347/5348 (one stray byte); a
  warning and OK without the folder. `N0 + 16`.
- `tools/korean/trscheck.py` (+ test): `census` (encoding, unreadable pairs, charset bytes), `utf8len` (longest
  translation as UTF-8 against the engine's buffers: 512 `loadPtrToResource`/`_charsetBuffer`/`drawString`, 256 the v2
  sentence parts), `equal CP949 UTF8`. The harness builders run it (H2).
- Commits `TEST: Check CP949 bundles against their UTF-8 stage when given`, `TOOLS: Korean: Add trscheck.py, the SCUMM
  bundle check`.

### Task a13: Records

`PATCHES.md` rows (topic `ko/05-scumm`) for each upstream file of appendix A; `P6-notes.md` (per task: commit, tests,
deviations, run folders). Commit `DOCS: Korean: Record the SCUMM UTF-8 text patches`.

### Task a14: Smoke and English

The baseline's sets on `$BLD/scummvm` without `encoding.dat`, then Indy4 with it (unreadable 7 -> 5); MM V1 English and
MI1 CD English (`language=en`): no Korean/UTF-8/TENC line, picture as `build-plain`. Screenshots
`~/work/tmp/ko-screenshots/scumm-p6a-*.png` with captions.

### Review checkpoint A

- `make test` N0 + 16 OK on `$BLD`, the SCUMM+SCI build and ASan (+ ASan runs of MI2 pack and MI1 floppy: no report).
- Self-read against P3's review classes: every write into a fixed buffer goes through `toEngineText()` (NUL inside the
  size, cut on a boundary) or a buffer sized for UTF-8 (`drawString` 512); `nextCJKChar()` never reads past a NUL
  (`decodeUtf8` stops at a non-continuation byte); `koreanCharStart()` never goes before `str` (loop bound) .
- Upstream lines (appendix A) <= 100 added; new files as appendix B.
- Fresh reviewer; findings go back to the implementer by resume.

**P6a acceptance checklist**

- [ ] MI2 pack and MI1 UTE bundle play in Korean; no assertion on any set; Hangul needs no `encoding.dat`.
- [ ] Same 1x picture as upstream on CP949 data (MI2 slot 2 frame identical), except the documented MI1 width fix.
- [ ] MM/Zak sentence line in Korean order with bundle object names.
- [ ] Old CP949 saves load (converted); new saves `TENC 1`; upstream's failure on them documented.
- [ ] `korean.fnt` never read outside its 2350 glyphs; out-of-font characters show `?`, logged once.
- [ ] English targets unchanged; PATCHES.md, notes, commit messages checked.

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

### 5a. Owner request 2026-11-23: DOTT in Single Day Regular, anti-aliased (prototype already run)

"DOTT는 Single Day Regular 폰트를 AA로 적용해보자": Day of the Tentacle is the concrete target of the S2 spike. Owner rules
that apply: AA (2 bpp) only on RGB targets (RGB555 and RGB565 required, XRGB8888 supported); a palette (CLUT8) screen
keeps a 1 bpp face or the game's own font. DOTT is a 256-colour game, so the AA text is blended over the palette frame at
the final RGB composition of the 2x frame (what the SCI `TextLayer` + upscaled driver do).

**Faces** (scratch `~/work/tmp/p6/fonts/`, not in the repository, not in svf-fonts-open):

- Source: `SingleDay-Regular.ttf` (Google Fonts `ofl/singleday`, 1.00, DXKorea, OFL 1.1; HTTP 200, 816,524 bytes,
  sha256 `716ff67a4b0675b35c26d60a4bb83173f7d153ab754474ed36c3369593ca1ca8`) and `OFL.txt` (sha256 `397e83b0...`). All
  11,172 syllables are TrueType composites, 95 ASCII glyphs, 1024 units/em, ascent 779.
- Generated with the submodule's tools, the route of `tools/korean/gen_aa_faces.py` (not edited; the AA agent's file):
  `jamoset.py --layout 844 --from-ttf SingleDay-Regular.ttf --size N --bpp 2 --check` for N = 16, 18, 20, 24
  (`SD{16,18,20,24}JA.SVF`, 19-27 KB each, about 110 s each, `check: composed 11172 syllables`; the 24 px log:
  `level error 0.0993, IoU at level 2 0.883`), `mkfont.py --size N --cell N --width N --bpp 2 --clip-cell --unicode
  0020-007E --only-listed` for the Latin faces `SD{16,18,20,24}L.SVF`, and a precomposed reference `SD24P.SVF` (all
  syllables, 1.46 MB). sha256 (first 16): SD16JA `d9b551b4a53ca060`, SD18JA `c5d329d8acee638b`, SD20JA `ed866ca4d491f467`,
  SD24JA `26e98a5e566680f2`, SD24L `09afebf513f8c079`.
- **Cross-check with the shipped `svf/jamo-extra/SGDAY{16,18,20,24}A.SVF`: not byte-identical, and cannot be**: `FACES.TSV`
  says SGDAY* were made by the research pipeline that is not in the repository (sets 24/16/16, 8x8 classes, own Latin),
  while `jamoset.py --layout 844` gives an 8x4x4 set. Pixel check over 300 random KS X 1001 syllables drawn with
  `svfline.py` (`~/work/tmp/p6/fonts/cmpfaces.py`): mean |level difference| generated vs shipped 0.16 (both sizes); vs
  the TTF precomposed reference at 24 px: generated 0.110, shipped 0.166; ink: shipped 14 % heavier and its advance is
  wider (`scumm-dott-singleday-faces.png`: row 2 generated, row 3 shipped). One visible flaw of the generated 24 px set:
  `고` draws its vowel bar low (row 2). Either face can serve; the shipped one is the reviewed one (owner's choice, H10).

**Spike code** (`~/work/tmp/p6/tree`, on top of the P6a prototype; first run on the CP949 revision, `proto-s2.diff`, then
re-run on the UTF-8-internal path, `proto-utf8.diff`, where the seam takes the code point `nextCJKChar()` produced and the
RGB555/RGB565/CLUT8 frames came out the same: `scumm-utf8-dott-singleday-*.png`; built, not unit-tested):
`engines/scumm/hires_text.{h,cpp}` (`HiResTextSpike`, 330 lines) and hooks in upstream files: `charset.cpp` (m = 2 in
`loadKorFont` for a Korean DOS v4-v6 game; `drawHiResChar` before `printCharIntern`), `charset.h` (+1), `scumm.cpp`
(RGB `initGraphics`, plane creation/free), `gfx.cpp` (+7: the composite branch in `drawStripToScreen`, pitch for the
existing CJK m = 2 scaling), `palette.cpp` (+9: palette into the composite, every screen marked dirty, cursor redone),
`cursor.cpp` (+22: 8-bit cursor converted and doubled for the RGB screen). Upstream lines of S2 alone: about +80; **no
erasure hook**: the glyph pixels are in `_textSurface`, whose fills upstream already scales; coverage counts only while
`_textSurface` still holds the index it was drawn with. Scratch keys: `hires_text_spike`, `hires_text_render=clut8|rgb565|
rgb555|rgb888`, `hires_text_faces="16:a,b;18:...;24:..."` (chain by cell), `hires_text_decor=outline|drop|none`,
`hires_text_pick=height|fit`.

**Result on the harness DOTT pack** (`runs/dott-sd-*`, screenshots `~/work/tmp/ko-screenshots/scumm-dott-singleday-*.png`,
all viewed):

| Mode | What the frame shows |
|---|---|
| RGB565, AA, outline (1 px ring + 2 px drop in the game's Korean shadow colour) | intro and lobby dialogue (`나는 오늘밤 공연 준비해야 해.`), verbs (`건네기 줍기 사용하기 / 열기 보기 밀기 / 닫기 말걸기 당기기`), sentence line (`밀기: 문`), inventory: all at 2x, smooth, **legible over the busy lobby** |
| RGB555, AA, outline | same, colours right (`걸어가기: 문`) |
| XRGB8888, AA, drop only | readable, weaker over light areas |
| RGB565, AA, no decoration | orange text over the orange/blue lobby barely readable: **the handwriting face needs the outline** |
| CLUT8, shipped `SGDAY24.SVF` 1 bpp, outline | legible, jagged, no AA (the owner's CLUT8 rule) |
| 1x original `korean0N.fnt` (S1) | heavy built-in outline, coarse |

Measured facts for the plan: the 2-byte boxes at 2x are 20x24 (dialogue, verbs: `korean02/03/05/08`, 10x12 at 1x) and
16x16 (sentence line, `korean01` 8x8); 1-byte boxes 10x18/12x18; the 24 px face is picked by box height and its 24 px
advance overhangs the 20 px box by 4 px (`사용하기` touches the inventory frame); DOTT text uses **`^` (0x5E) as a game
glyph** (the old fork's map: `[glyphs] 0x5e = original`, also 0x07, 0x7F, and 0x5F in fonts 1 and 4): the spike drew a
Single Day caret there (wrong); 1-byte characters get no shadow from upstream (`prepareDraw` sets none), so `.` and
Latin are drawn without the outline. Palette fades of the intro and the room change to the lobby recomposite correctly
(no stale colours seen); the cursor is shown doubled. Not checked: palette cycling (DOTT has none in these rooms),
dissolve transitions, save/load, the GMM over the 2x screen, cutscene-skip mid line beyond the one Esc.

**DOTT / Single Day acceptance item of the gate (G7, added to the table of Task b6):** GO needs, on the DOTT harness pack
at RGB565 **and** RGB555 (XRGB8888 one frame), a screenshot set showing (a) a dialogue line over the lobby with every
syllable in Single Day AA, legible at 1:1 without zoom (outline on), (b) the verbs, sentence line and inventory of the
lobby in Single Day AA, no glyph cut by the verb/inventory frames, (c) `^`, `.`, digits and Latin in the same lines drawn
right (game glyph for `^`, outlined Latin), (d) the same frames at CLUT8 with the 1 bpp pair (`SGDAY*` 1 bpp) and no AA,
(e) a palette fade and a room change with text up, no stale or miscoloured glyph, (f) the English DOTT target unchanged
(320x200, no hi-res). Each frame named in the notes with its pixel format.

### 6. The spike (time box: 2 working days; scratch branch, never merged)

A first cut of b1-b3 already runs on DOTT (section 5a; its code is the starting point, rewritten in the spike branch with
the census and counters). Branch `p6b-spike` in a scratch clone (`git -C $SRC worktree add ~/work/tmp/p6/spike` after P6a), build
`~/work/tmp/scummvm2026-p6spike`. Scope: CLUT8 output with a 1-bpp face (`data:hires_text/NEODG16A.SVF` or `SGDAY*` 1 bpp) **and** the DOTT RGB AA
case of section 5a (Single Day), Korean only (Latin keeps the game charset, pixel-doubled), targets Loom CD, MI1 VGA, MI2,
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

- [ ] At the `is2byte` branch, when the hi-res object exists: the character is already its code point (P6a: UTF-8 inside,
  `nextCJKChar()`; with hi-res on, `nextCJKChar()` does not refuse syllables outside KS X 1001), draw it with
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
| G7 | DOTT in Single Day AA (section 5a): items (a)-(f) shown on RGB565, RGB555 (+1 frame XRGB8888) and CLUT8 1 bpp | screenshots in the notes, each with its pixel format |

Write the decision, the table filled, and the screenshots list into `P6-notes.md`; owner sign-off before section 7.

### 7. Conditional on GO: the 2x plan (outline tasks; each expands like P6a at the start of P6b proper)

Every task: failing unit test first where a pure unit exists, exact files below, [data] step on the gate's saves, one
commit `SCUMM: ...` with the trailer. The code of the spike is rewritten, not merged.

| Task | Files | What | Test / verification |
|---|---|---|---|
| b7 | new `engines/scumm/hires_text.{h,cpp}`; `scumm.h` (+1 member), `charset.cpp` (`loadKorFont`) | `Scumm::HiResText`: decides on (DOS v4-v6 Korean, `hires_text`, a map from `Graphics::pickHiResMap` with `hires_text_map*` keys), owns the `HiResFontSet` per charset id (`[font.N]` = SCUMM charset id), sets m = 2 | unit: decision table over (version, platform, language, key, map present); [data] the five targets open at 640x400, English at 320x200 |
| b8 | new `engines/scumm/hires_composite.{h,cpp}`; `gfx.cpp` (one call in `drawStripToScreen`) | keyed 2x composite for DOS (b1's function, plus the 2-bytes-per-pixel path through `_16BitPalette`) | unit: composite of a 4x2 strip with a transparent/opaque text pattern, CLUT8 and RGB565, byte-exact |
| b9 | `charset.cpp` (`printCharIntern` is2byte branch, `drawBits1Kor`), `hires_text.cpp` | the glyph seam of b2: the code point (UTF-8 engine text) straight to the SVF chain, glyph into `_textSurface` inside the 2x box of the 1x cell, shadow | unit: `HiResText::glyphBox()` (1x cell -> 2x box, baseline) for 8x8, 16x9, 24x12 cells; [data] scenario 1-2 |
| b10 | `charset.cpp` (classic renderer 1-byte path) | Latin at 2x: the game charset glyph pixel-doubled, or the map's Latin face when the map routes ASCII | unit: doubling of a 1-bpp row; [data] mixed lines (`Special Thanks 브래드 ...`) |
| b11 | as b3 found (at most one hook) | `vs`/`back` destinations | the gate counts, then 0 artefacts |
| b12 | `hires_text.cpp`; `gfx.cpp` (`clearTextSurface`, `restoreBackground`: one call each, only if the spike's index-match rule shows ghosts) | **AA (2 bpp) on RGB output only (RGB555/565 required, 888 supported; CLUT8 keeps 1 bpp)**, with the outline decoration (section 5a); the spike's way: the frame doubled and the text blended through the palette in one pass, `palette.cpp` marks every screen dirty on a change, `cursor.cpp` converts the 8-bit cursor; a `Graphics::TextPlane` coverage beside `_textSurface`, composite via `Graphics::composeSpan` through the current palette; coverage cleared where `_textSurface` is cleared (two calls); the index-match rule (coverage counts only where the plane's index equals `_textSurface`'s) covers the other fill sites | unit: compose of a 2-bpp glyph over 3 palettes; [data] MI2/DOTT at `render_target=rgb565` |
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
| UTF-8 bundle plays without encoding.dat | MI2 pack; MI1 UTE bundle on MI1 CD data | run, viewed |
| CP949 bundle -> UTF-8 | MM, Zak, Indy3, Loom, Indy4 packs; `mi2kor/korean.trs.cp949` | run log sizes; a12 data suite vs `mi2kor/korean.trs` |
| CP949 inside the data -> UTF-8 | MI1 VGA floppy, DOTT floppy packs | run, viewed |
| same 1x picture | MI2 old saves 2 and 6 loaded by `build-head` and `$BLD` | pixel diff |
| width fix | MI1 credit line with 0xFA | pixel diff, viewed |
| sentence line | MM pack | run, viewed |
| saves | `saves/monkey2-ko.s02` (CP949, no trailer), autosave round trip, plain build on a new save | runs |
| buffers | `trscheck.py utf8len` over all bundles | tool |
| S2 gate | v4-v6 packs + saves at the scenarios, DOTT Single Day item G7 | runs, viewed |

## 9. UNVERIFIED

1. Every engine path that shows text without `translateText()` for a Korean target: the five byte sites were found by
   grep (section 0.4); a path that bypasses both would show raw CP949 in MI1/DOTT as `?` (none seen in the runs, not
   proven absent: e.g. v6 string arrays printed by a path other than `convertStringMessage`).
2. An English string with bytes >= 0x80 that happen to be valid UTF-8 would pass the boundary unconverted (judged
   improbable; the CP949 pairing rule keeps bytes 0x80-0xFF of the game's charset as U+0080..U+00FF either way).
3. Particles of MI1/MI2 (`을/를` after a name) in play (Task a7).
4. Two-object v2 sentence lines; Zak's sentence line.
5. A `jamo`-era UTF-8 save (`TENC 1`): loads unchanged by design; none on the host.
6. Indy4's lines with unreadable pairs reached in play.
7. The `ko/` folder (Task a11, not prototyped).
8. Zak/Indy3's longest lines (295/257 bytes as UTF-8) reaching `drawString()`: the buffer is 512 now; which path prints
   them was not checked.
9. Save names typed with Hangul (GMM, original save screens), the debugger output.
10. All of S2 beyond the spike (section 7), and the spike items listed in section 5a.
11. N0 of the SCUMM+SCI build at the base.

## 10. Conflicts with DESIGN.md

Revision 1's conflict "CP949 at run time" is gone: this revision follows DESIGN section 3 (UTF-8 inside, CP949 input
only). Remaining:

1. **No `Scumm::TextMode` object** (DESIGN 3 policy 1): the only decision is `_koreanUtf8` (Korean target, v1-v6),
   beside upstream's `isScummvmKorTarget()`; S2 adds the hi-res object.
2. **DESIGN 11 row P6 tests** "text_char_length, debugsocket cond": `korean_text` covers the character-length rules;
   the debug socket is `ko/07` (not on the branch).
3. **Lazy `TrsStore`** deferred to P7.
4. **Language folder**: `ko/` accepted (a11), no separate `-ko` detection variant.
5. **UTF-8 bundles by BOM only** (no `text_encoding=utf8` key).
6. **Palette cycling in MI2 room 33** applies with S2 only.
7. **Unknown characters**: `?` in the 2-byte cell instead of DESIGN 4's "original shown" (that rule is for untranslated
   strings, which still show the original).
8. **Map v3 `[glyphs]`** (DOTT `^`, 0x07, 0x7F, 0x5F) and **decoration** (`[shadow]`): needed by S2 (section 5a).
9. **AA policy**: the owner's RGB-only rule over DESIGN 5.1's `render_target=auto`.
10. **Saves**: DESIGN 3 point 3 says the launcher shows UTF-8 names; it says nothing on upstream compatibility. A Korean
    UTF-8 save is not readable by upstream ScummVM (measured): a consequence of the owner's decision, recorded.
11. **Width of charset glyphs in Korean lines** (MI1 0xFA): measured as drawn, not as upstream measured it (1 px shift
    on such centred lines): deliberate, recorded.

## 11. Harness-side dependencies (owner's data work)

| # | What | Needed by |
|---|---|---|
| H1 | Nothing for MI1/MI2: the UTF-8 `KOREAN.TRS` they ship is used as it is. Keep the BOM. The CP949 bundles may stay CP949 (converted at load) or ship as UTF-8 with a BOM (no conversion, no `encoding.dat` question at all): the owner's choice | a4, a5 |
| H2 | `trscheck.py census/utf8len/equal` in the SCUMM builders: report unreadable pairs (Indy4: 7, the bytes of 4 garbage lines; fix them in the master), out-of-font characters (MI2 2, MI1 UTE 1) and any translation over 511 bytes as UTF-8 (none today) | a12 |
| H3 | **`encoding.dat` is no longer needed for Hangul** (static table). Only CP949 Hanja/symbol/extension pairs need it (Indy4: 2 of 7 pairs); with H1's UTF-8 bundles it is not needed at all | a2 |
| H4 | MM/Zak: every OBCD object name has a bundle line (the 2026-10-15 fill covered `kind: object`; a builder check that lists room objects without a translation) | a8 |
| H5 | Saves at the gate scenarios for Loom, MI1, MI2, Indy4, DOTT (made with the P6a build) and one MM/Zak save with an inventory item for the two-object sentence line | a8, b5 |
| H6 | If any `jamo`-era UTF-8 Korean save exists, a copy for Task a10 | a10 |
| H11 | Pack README text: a Korean save made by this engine cannot be loaded by upstream ScummVM (it stops at the first Korean text) | a10 |
| H7 | MI1 UTE: the matching UTE game data, or accept credits-only verification | a13 |
| H8 | S2 only: SCUMM maps re-paired to OFL faces in map v3 (`HIRESBIT.MAP` 1-bpp first, then `HIRESVF.MAP`), per charset id with the 2x cell sizes of each game's `koreanNN.fnt` (MM 8x8 -> 16 px, MI2 font 0 12 px -> 24 px, ...); `.HOT` preload lists for P7 | b7 |
| H9 | Decide whether packs move `KOREAN.TRS` into `ko/ko.trs` (optional; both work after a5) | a5 |
| H10 | DOTT map for S2 (owner, map v3, OFL faces): `[font.N:scumm:rgb]` = Single Day 2 bpp AA chain per DOTT charset id by its 2x box (ids below inferred from the `koreanNN.fnt` sizes and the boxes the spike logged, UNVERIFIED per id): ids 2, 3, 5, 8 (10x12 -> 20x24 box): 24 px (`SGDAY24A.SVF`, or the generated `SD24JA` + Latin; with 24 px advance overhanging a 20 px box, or a 20 px face for the verbs); id 1 (8x8 -> 16x16, sentence line): `SGDAY16A`; id 4 (11 -> 22): 20 px; id 7 (18 -> 36) and id 0 (27 -> 54, title): 24 px scaled or the game font; `[font.N:scumm:clut8]` = the 1 bpp `SGDAY{16,20,24}.SVF`; `missing=` the original glyph for `^` (0x5E), 0x07, 0x7F and 0x5F in ids 1 and 4 (the old fork's `[glyphs]`, which map v3 dropped: needs a key, conflict 10.9); the outline decoration as a map key (conflict 10.10). Which Single Day build (shipped research build or the TTF route) is the owner's pick | b7, G7 |

## 12. Scratch tools used (not in the repository)

`~/work/tmp/p6/bin/{mkrun.sh,sess.sh,autoshots.sh,grid.py}`, `~/work/tmp/p6/py/{trsinfo,trsenc,trs2cp949}.py`, the
prototype `~/work/tmp/p6/tree` + `proto-utf8.diff` (revision 1: `proto.diff`, `proto-s2.diff`), `~/work/tmp/p6/gen/gen_ksx1001.py`, runs in `~/work/tmp/p6/runs/`. `sess.sh` takes `XARGS` (extra command-line
arguments, e.g. `--save-slot=2`), `SCALE`, `WAIT`, `DBG` from the environment.

## Appendix A. S1 upstream hunks (prototype, S2 spike lines removed; hunk line counts approximate)

```diff
diff --git a/engines/scumm/charset.cpp b/engines/scumm/charset.cpp
index 190e1818..d52574da 100644
--- a/engines/scumm/charset.cpp
+++ b/engines/scumm/charset.cpp
@@ -21,6 +21,9 @@
 
 #include "graphics/font.h"
 #include "scumm/charset.h"
+#include "scumm/korean_text.h"
+#include "common/ustr.h"
 #include "scumm/file.h"
 #include "scumm/scumm.h"
 #include "scumm/macgui/macgui.h"
@@ -187,6 +190,8 @@ void ScummEngine::loadKorFont() {
 	Common::File fp;
 	int numChar = 2350;
 	_useCJKMode = true;
+	// Korean text is UTF-8 inside the engine (korean_text.h); The Dig/FT/COMI keep their own CP949 path.
+	_koreanUtf8 = (_game.version < 7);
 
 	if (_game.version < 7 || _game.id == GID_FT)
 		_useMultiFont = true;
@@ -251,7 +267,11 @@ byte *ScummEngine::get2byteCharPtr(int idx) {
 
 	switch (_language) {
 	case Common::KO_KOR:
-		idx = ((idx % 256) - 0xb0) * 94 + (idx / 256) - 0xa1;
+		// A code point (UTF-8 text) or a CP949 pair (FT): the glyph of korean.fnt, or the refusal glyph when the font
+		// has none, never a read outside the font.
+		idx = _koreanUtf8 ? ksx1001Index(idx) : ((idx % 256) - 0xb0) * 94 + (idx / 256) - 0xa1;
+		if (idx < 0 || idx >= 2350)
+			return koreanRefusalGlyph();
 		break;
 	case Common::JA_JPN:
 		if ((_game.id == GID_MONKEY && _game.platform == Common::kPlatformSegaCD) || _isIndy4Jap) {
@@ -551,6 +571,12 @@ int CharsetRenderer::getStringWidth(int arg, const byte *text) {
 					}
 				}
 			} else if (chr & 0x80) {
+				const int g = _vm->koreanCharsetGlyph(text + pos - 1);
+				if (g >= 0) {	// UTF-8 text: a glyph of the game's charset (U+0080..U+00FF)
+					width += getCharWidth(g);
+					pos++;
+					continue;
+				}
 				if (_vm->_game.platform == Common::kPlatformSegaCD) {
 					// Special character: this one has to be rendered as a space (0x20)
 					// and also inherits its rendering dimensions.
@@ -567,7 +593,7 @@ int CharsetRenderer::getStringWidth(int arg, const byte *text) {
 					}
 				}
 
-				pos++;
+				pos += _vm->cjkTrailBytes(text + pos - 1);
 				continue;
 			}
 		}
@@ -655,8 +681,11 @@ void CharsetRenderer::addLinebreaks(int a, byte *str, int pos, int maxwidth) {
 					// This is the only way to get an accurate text formatting in the MI1 intro.
 					chr = (int8)str[pos++] | (chr << 8);
 				curw += getCharWidth(chr);
-			} else if (chr & 0x80) {
+			} else if (_vm->koreanCharsetGlyph(str + pos - 1) >= 0) {
+				curw += getCharWidth(_vm->koreanCharsetGlyph(str + pos - 1));	// UTF-8: a charset glyph
 				pos++;
+			} else if (chr & 0x80) {
+				pos += _vm->cjkTrailBytes(str + pos - 1);
 				curw += _vm->_2byteWidth;
 				// Original keeps glyph width and character dimensions separately
 				if (_vm->_language == Common::KO_KOR || _vm->_language == Common::ZH_TWN) {
@@ -669,15 +698,17 @@ void CharsetRenderer::addLinebreaks(int a, byte *str, int pos, int maxwidth) {
 			if (_vm->isScummvmKorTarget() && !_center) {
 				// Break Korean words at any character
 				// Used in Korean fan translated games
+				// cs: where the character starts (CP949: 2 bytes before pos; UTF-8: its own length)
+				const int cs = (chr & 0x80) ? _vm->koreanCharStart(str, pos) : pos - 1;
 				if (chr & 0x80) {
-					if (checkKSCode(chr, str[pos - 1])
-					    && !(pos - 4 >= origPos && str[pos - 3] == '`' && str[pos - 4] == ' ')  // prevents hanging quotation mark at the end of line
-					    && !(pos - 4 >= origPos && str[pos - 3] == '\'' && str[pos - 4] == ' ') // prevents hanging single quotation mark at the end of line
-					    && !(pos - 3 >= origPos && str[pos - 3] == '('))  // prevents hanging parenthesis at the end of line
-						lastKoreanLineBreak = pos - 2;
+					if (_vm->koreanBreakableAt(str + cs)
+					    && !(cs - 2 >= origPos && str[cs - 1] == '`' && str[cs - 2] == ' ')  // prevents hanging quotation mark at the end of line
+					    && !(cs - 2 >= origPos && str[cs - 1] == '\'' && str[cs - 2] == ' ') // prevents hanging single quotation mark at the end of line
+					    && !(cs - 1 >= origPos && str[cs - 1] == '('))  // prevents hanging parenthesis at the end of line
+						lastKoreanLineBreak = cs;
 				} else {
-					if (chr == '(' && pos - 3 >= origPos && checkKSCode(str[pos - 3], str[pos - 2]))
-						lastKoreanLineBreak = pos - 1;
+					if (chr == '(' && _vm->koreanCharBefore(str + origPos, str + cs))
+						lastKoreanLineBreak = cs;
 				}
 			}
 		} else {
diff --git a/engines/scumm/detection_internal.h b/engines/scumm/detection_internal.h
index 8c59c384..742cae80 100644
--- a/engines/scumm/detection_internal.h
+++ b/engines/scumm/detection_internal.h
@@ -234,7 +234,7 @@ static Common::Language detectLanguage(const Common::FSList &fslist, byte id, co
 	if (id != GID_CMI && id != GID_DIG) {
 		// Detect Korean fan translated games
 		Common::FSNode langFile;
-		if (searchFSNode(fslist, "korean.trs", langFile)) {
+		if (searchFSNode(fslist, "korean.trs", langFile) || searchFSNode(fslist, "ko.trs", langFile)) {
 			debugC(0, kDebugGlobalDetection, "Korean fan translation detected");
 			return Common::KO_KOR;
 		}
diff --git a/engines/scumm/module.mk b/engines/scumm/module.mk
index b0f7e3eb..9a178dcf 100644
--- a/engines/scumm/module.mk
+++ b/engines/scumm/module.mk
@@ -42,6 +42,13 @@ MODULE_OBJS := \
 	imuse/drivers/pcspk.o \
 	input.o \
 	ks_check.o \
+	korean_sentence.o \
+	korean.o \
+	trs_bundle.o \
+	korean_text.o \
+	ksx1001_table.o \
+	saved_text.o \
 	macgui/macgui.o \
 	macgui/macgui_dialogwindow.o \
 	macgui/macgui_impl.o \
diff --git a/engines/scumm/saveload.cpp b/engines/scumm/saveload.cpp
index d1d7a764..6c7ff836 100644
--- a/engines/scumm/saveload.cpp
+++ b/engines/scumm/saveload.cpp
@@ -38,6 +38,7 @@
 #include "scumm/scumm_v0.h"
 #include "scumm/scumm_v7.h"
 #include "scumm/scumm_v8.h"
+#include "scumm/saved_text.h"
 #include "scumm/sound.h"
 #include "scumm/he/sprite_he.h"
 #include "scumm/verbs.h"
@@ -642,6 +643,9 @@ bool ScummEngine::saveState(Common::SeekableWriteStream *out, bool writeHeader)
 	Serializer ser(nullptr, out);
 	ser.setVersion(CURRENT_VER);
 	saveLoadWithSerializer(ser);
+	// After the state, where a build without it never reads (saved_text.h). Korean saves only.
+	if (_koreanUtf8)
+		writeSavedTextEncoding(*out, true);
 	return true;
 }
 
@@ -827,8 +831,12 @@ bool ScummEngine::loadState(int slot, bool compat, Common::String &filename) {
 	Serializer ser(in, nullptr);
 	ser.setVersion(hdr.ver);
 	saveLoadWithSerializer(ser);
+	const SavedTextEncoding savedText = readSavedTextEncoding(*in);
 	delete in;
 
+	if (_koreanUtf8)
+		convertLoadedText(savedText);
+
 	// Init NES costume data
 	if (_game.platform == Common::kPlatformNES) {
 		if (hdr.ver < VER(47))
diff --git a/engines/scumm/scumm.h b/engines/scumm/scumm.h
index 356a3602..8fc9773e 100644
--- a/engines/scumm/scumm.h
+++ b/engines/scumm/scumm.h
@@ -632,6 +632,9 @@ protected:
 	void setupCostumeRenderer();
 
 	virtual void loadLanguageBundle();
+	void transcodeLanguageBundle(uint32 bodySize);
+	/** Convert the translated text of a UTF-8 save to CP949 (saved_text.h); @p saved is a SavedTextEncoding. */
+	void convertLoadedText(int saved);
 	void loadCJKFont();
 	void loadKorFont();
 	void setupMusic(int midi);
diff --git a/engines/scumm/scumm_v2.h b/engines/scumm/scumm_v2.h
index 62f732b5..004660a5 100644
--- a/engines/scumm/scumm_v2.h
+++ b/engines/scumm/scumm_v2.h
@@ -112,6 +112,8 @@ protected:
 	void adaptCursorToVideoMode();
 
 	void drawPreposition(int index);
+	/** The Korean sentence line (korean_sentence.h); true when it was drawn. */
+	bool drawKoreanSentence();
 
 	void walkActorToObject(int actor, int obj);
 
diff --git a/engines/scumm/string.cpp b/engines/scumm/string.cpp
index ebb998ec..2f49e1d1 100644
--- a/engines/scumm/string.cpp
+++ b/engines/scumm/string.cpp
@@ -40,6 +40,8 @@
 #include "scumm/scumm_v2.h"
 #include "scumm/scumm_v6.h"
 #include "scumm/scumm_v7.h"
+#include "scumm/korean_text.h"
+#include "scumm/trs_bundle.h"
 #include "scumm/verbs.h"
 #include "scumm/he/sound_he.h"
 
@@ -879,7 +881,7 @@ void ScummEngine::wrapSegaCDText() {
 					// let's just skip it...
 					ptrToCurLine++;
 				} else if (_useCJKMode && curChar & 0x80) { // CJK char
-					ptrToCurLine++;
+					ptrToCurLine += cjkTrailBytes(ptrToCurLine);
 				}
 
 				// Line breaks and string termination
@@ -939,6 +941,9 @@ void ScummEngine_v2::drawSentence() {
 	else
 		return;
 
+	if (drawKoreanSentence())
+		return;
+
 	if (VAR(VAR_SENTENCE_OBJECT1) > 0) {
 		temp = getObjOrActorName(VAR(VAR_SENTENCE_OBJECT1));
 		if (temp) {
@@ -1260,10 +1265,8 @@ void ScummEngine::displayDialog() {
 		}
 
 		if (c & 0x80 && _useCJKMode) {
-			if (is2ByteCharacter(_language, c)) {
-				byte *buffer = _charsetBuffer + _charsetBufPos;
-				c += *buffer++ * 256; //LE
-				_charsetBufPos = buffer - _charsetBuffer;
+			if (_koreanUtf8 || is2ByteCharacter(_language, c)) {
+				c = nextCJKChar(c, _charsetBuffer, _charsetBufPos);
 
 				// The JAP Sega CD version of Monkey Island 1 doesn't just calculate
 				// the font height, but instead relies on the actual string height.
@@ -1352,7 +1355,9 @@ int ScummEngine::countNumberOfWaits() {
 }
 
 void ScummEngine::drawString(int a, const byte *msg, Common::TextToSpeechManager::Action ttsAction) {
-	byte buf[270];
+	// 512, not 270: Korean UTF-8 text is up to 1.5 times its CP949 length (Zak's longest line: 208 -> 295 bytes),
+	// and convertMessageToString() stops the engine on a full buffer.
+	byte buf[512];
 	byte *space;
 	int i, c;
 	byte fontHeight = 0;
@@ -1537,8 +1542,8 @@ void ScummEngine::drawString(int a, const byte *msg, Common::TextToSpeechManager
 				}
 			}
 			if (c & 0x80 && _useCJKMode) {
-				if (is2ByteCharacter(_language, c))
-					c += buf[i++] * 256;
+				if (_koreanUtf8 || is2ByteCharacter(_language, c))
+					c = nextCJKChar(c, buf, i);
 			}
 
 			// With the code above, we risk missing the termination character.
@@ -1904,7 +1909,9 @@ int ScummEngine::convertNameMessage(byte *dst, int dstSize, int var) {
 			int increment = convertMessageToString(ptr, dst, dstSize);
 			// Save the final consonant (jongsung) of the last Korean character
 			// Used by Korean fan translated games (monkey1, monkey2)
-			if (isScummvmKorTarget() && _useCJKMode) {
+			if (_koreanUtf8) {
+				_krStrPost = koreanFinalBits(dst, dst + increment);	// of the name as shown (UTF-8)
+			} else if (isScummvmKorTarget() && _useCJKMode) {
 				_krStrPost = 0;
 				int len = resStrLen(ptr);
 				if (len >= 2) {
@@ -1971,7 +1978,9 @@ int ScummEngine::convertStringMessage(byte *dst, int dstSize, int var) {
 			int increment = convertMessageToString(ptr, dst, dstSize);
 			// Save the final consonant (jongsung) of the last Korean character
 			// Used by Korean fan translated games (monkey1, monkey2)
-			if (isScummvmKorTarget() && _useCJKMode) {
+			if (_koreanUtf8) {
+				_krStrPost = koreanFinalBits(dst, dst + increment);
+			} else if (isScummvmKorTarget() && _useCJKMode) {
 				_krStrPost = 0;
 				for (int i = resStrLen(ptr); i > 1; i--) {
 					byte k1 = ptr[i - 2];
@@ -2337,7 +2346,10 @@ void ScummEngine::loadLanguageBundle() {
 	}
 
 	ScummFile file(this);
-	openFile(file, "korean.trs");
+	Common::Array<Common::Path> names;
+	trsBundleNames(_language, names);
+	for (uint n = 0; n < names.size() && !file.isOpen(); n++)
+		openFile(file, names[n]);
 
 	if (!file.isOpen()) {
 		_existLanguageFile = false;
@@ -2407,6 +2419,9 @@ void ScummEngine::loadLanguageBundle() {
 	file.read(_languageBuffer, size - bodyPos);
 	file.close();
 
+	if (_koreanUtf8 && !trsBodyIsUtf8(_languageBuffer, size - bodyPos))
+		transcodeLanguageBundle(size - bodyPos);	// a CP949 bundle: translations to UTF-8, once
+
 	debug(2, "loadLanguageBundle: Loaded %d entries", _numTranslatedLines);
 }
 
@@ -2472,7 +2487,7 @@ void ScummEngine::translateText(const byte *text, byte *trans_buff, int transBuf
 					const byte *translatedText = searchTranslatedLine(text, scrpRange, true);
 					if (translatedText) {
 						debug(7, "translateText: Found by heuristic #1");
-						memcpy(trans_buff, translatedText, MIN<int>(resStrLen(translatedText) + 1, transBufferSize));
+						copyTranslatedText(trans_buff, translatedText, transBufferSize);
 						return;
 					}
 				}
@@ -2489,7 +2504,7 @@ void ScummEngine::translateText(const byte *text, byte *trans_buff, int transBuf
 					const byte *translatedText = searchTranslatedLine(text, scrpRange, true);
 					if (translatedText) {
 						debug(7, "translateText: Found by heuristic #2");
-						memcpy(trans_buff, translatedText, MIN<int>(resStrLen(translatedText) + 1, transBufferSize));
+						copyTranslatedText(trans_buff, translatedText, transBufferSize);
 						return;
 					}
 				}
@@ -2500,7 +2515,7 @@ void ScummEngine::translateText(const byte *text, byte *trans_buff, int transBuf
 		const byte *translatedText = searchTranslatedLine(text, TranslationRange(0, _numTranslatedLines - 1), false);
 		if (translatedText) {
 			debug(7, "translateText: Found by full search");
-			memcpy(trans_buff, translatedText, MIN<int>(resStrLen(translatedText) + 1, transBufferSize));
+			copyTranslatedText(trans_buff, translatedText, transBufferSize);
 			return;
 		}
 
@@ -2508,7 +2523,7 @@ void ScummEngine::translateText(const byte *text, byte *trans_buff, int transBuf
 	}
 
 	// Default: just copy the string
-	memcpy(trans_buff, text, MIN<int>(resStrLen(text) + 1, transBufferSize));
+	copyTranslatedText(trans_buff, text, transBufferSize);
 }
 
 bool ScummEngine::reverseIfNeeded(const byte *text, byte *reverseBuf, int reverseBufSize) const {
diff --git a/test/module.mk b/test/module.mk
index c2726f72..e0ceb1cc 100644
--- a/test/module.mk
+++ b/test/module.mk
@@ -65,6 +65,11 @@ ifeq ($(ENABLE_SCI), STATIC_PLUGIN)
 	TEST_LIBS += engines/sci/libsci.a graphics/libgraphics.a common/libcommon.a
 endif
 
+ifeq ($(ENABLE_SCUMM), STATIC_PLUGIN)
+	TESTS += $(srcdir)/test/engines/scumm/*.h
+	TEST_LIBS += engines/scumm/libscumm.a common/libcommon.a
+endif
+
 ifeq ($(ENABLE_TWINE), STATIC_PLUGIN)
 	TESTS += $(srcdir)/test/engines/twine/*.h
 	TEST_LIBS += engines/twine/libtwine.a
```

## Appendix B. New files (prototype, built and tested)

### B.1 tools/korean/gen_ksx1001.py

```python
#!/usr/bin/env python3
"""gen_ksx1001.py OUT.cpp: the 2350 KS X 1001 Hangul syllables (CP949 B0A1..C8FE, 94 a row) as Unicode code points, in
row order, which is also ascending Unicode order (checked). Used by engines/scumm/korean_text.cpp so that korean.fnt
glyph numbers and CP949 Hangul decode without encoding.dat. Run with python3 -I."""
import sys
cps = [ord(bytes([l, t]).decode('euc_kr')) for l in range(0xB0, 0xC9) for t in range(0xA1, 0xFF)]
assert len(cps) == 2350 and all(a < b for a, b in zip(cps, cps[1:]))
GPL = open(sys.argv[2]).read().split("\n")[:20] if len(sys.argv) > 2 else []
out = GPL + ["// Generated by tools/korean/gen_ksx1001.py: do not edit.", "// The 2350 KS X 1001 Hangul syllables, glyph order of korean.fnt (CP949 B0A1..C8FE), ascending.", "",
       '#include "scumm/korean_text.h"', "", "namespace Scumm {", "", "const uint16 kKsx1001Hangul[2350] = {"]
for i in range(0, len(cps), 12):
    out.append("\t" + ", ".join("0x%04X" % c for c in cps[i:i + 12]) + ",")
out += ["};", "", "} // End of namespace Scumm", ""]
open(sys.argv[1], "w").write("\n".join(out))
```

### B.2 engines/scumm/korean_text.h

```cpp
// (GPL header of engines/scumm/ks_check.h)

#ifndef SCUMM_KOREAN_TEXT_H
#define SCUMM_KOREAN_TEXT_H

#include "common/array.h"
#include "common/scummsys.h"

namespace Scumm {

/**
 * Korean text of the SCUMM engine (v1-v6), held as UTF-8 (DESIGN.md section 3).
 *
 * In Korean mode every message string the engine holds is UTF-8 bytes; the SCUMM escapes stay as they are: 0xFF or
 * 0xFE, a code, and two argument bytes unless the code is 1, 2, 3 or 8 (resStrLen(), convertMessageToString()).
 * Neither 0xFE nor 0xFF occurs in UTF-8, so an escape is found without knowing the text around it. CP949 is an input
 * format only: a CP949 bundle and the Korean inside MI1/DOTT's data are converted once, by toEngineText(), where text
 * enters the engine (ScummEngine::translateText()). korean.fnt is indexed through kKsx1001Hangul: no encoding.dat.
 */

/** The 2350 KS X 1001 Hangul syllables in korean.fnt glyph order (CP949 B0A1..C8FE), ascending code points. */
extern const uint16 kKsx1001Hangul[2350];

/** The korean.fnt glyph number of @p cp, or -1 when the font has none. */
int ksx1001Index(uint32 cp);

/** The code point of the CP949 pair if it is one of the 2350 syllables, else 0. */
uint32 ksx1001CodePoint(byte lead, byte trail);

/** Bytes of the escape at @p p (0xFF/0xFE + code [+ 2]) clipped to @p end; 0 when @p p is no escape. */
int scummEscapeLength(const byte *p, const byte *end);

/** The NUL that ends the text at @p s, escapes skipped (an argument byte may be 0), or @p end. */
const byte *scummTextEnd(const byte *s, const byte *end);

/** One strict UTF-8 code point at @p p and its length; 0xFFFFFFFF (length 1) for a byte that starts none. */
uint32 decodeUtf8(const byte *p, const byte *end, int &len);

/** Appends the UTF-8 of @p cp. */
void appendUtf8(uint32 cp, Common::Array<byte> &out);

/** Whether [@p p, @p end) (escapes skipped) is valid UTF-8; plain ASCII is. */
bool textIsUtf8(const byte *p, const byte *end);

/**
 * CP949 [@p src, @p end) to UTF-8, appended to @p out (no NUL), by upstream's rule: escapes (0xFF/0xFE) copied, a
 * lead 0xB0-0xD0 takes the next byte (so a trail 0xFE is no escape), any other byte >= 0x80 is a charset glyph and
 * becomes U+0080..U+00FF. The 2350 syllables come from kKsx1001Hangul; Hanja and extension syllables through
 * encoding.dat when it is there; what cannot be read becomes '?', counted in @p unmapped.
 */
void cp949ToUtf8(const byte *src, const byte *end, Common::Array<byte> &out, uint &unmapped);

/**
 * The engine-text boundary: copies the text at @p src (up to its NUL, escape-aware) into @p dst as UTF-8. Text that is
 * already valid UTF-8 (a translation, a stored resource, ASCII) is copied unchanged, anything else is read as CP949.
 * So calling it twice changes nothing. At most @p dstSize - 1 bytes are written, cut after the last whole character
 * or escape that fits, then a NUL. Returns the length written.
 */
int toEngineText(const byte *src, int srcMax, byte *dst, int dstSize, uint *unmapped = nullptr);

/** The jongsung bits of the last Hangul syllable in [@p p, @p end) (bit 0: a final, bit 1: ㄹ); 0 without one. */
int koreanFinalBits(const byte *p, const byte *end);

} // End of namespace Scumm

#endif
```

### B.3 engines/scumm/korean_text.cpp

```cpp
// (GPL header of engines/scumm/ks_check.h)

#include "common/debug.h"
#include "common/str.h"
#include "common/ustr.h"

#include "scumm/korean_text.h"

namespace Scumm {

int ksx1001Index(uint32 cp) {
	int lo = 0, hi = 2349;
	while (lo <= hi) {
		const int mid = (lo + hi) / 2;
		if (kKsx1001Hangul[mid] == cp)
			return mid;
		if (kKsx1001Hangul[mid] < cp)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return -1;
}

uint32 ksx1001CodePoint(byte lead, byte trail) {
	if (lead < 0xB0 || lead > 0xC8 || trail < 0xA1 || trail > 0xFE)
		return 0;
	return kKsx1001Hangul[(lead - 0xB0) * 94 + (trail - 0xA1)];
}

int scummEscapeLength(const byte *p, const byte *end) {
	if (p >= end || (*p != 0xFF && *p != 0xFE))
		return 0;
	int n = 2;
	if (p + 1 < end && p[1] != 1 && p[1] != 2 && p[1] != 3 && p[1] != 8)
		n = 4;
	return (int)MIN<ptrdiff_t>(n, end - p);
}

const byte *scummTextEnd(const byte *s, const byte *end) {
	while (s < end) {
		const int esc = scummEscapeLength(s, end);
		if (esc)
			s += esc;
		else if (*s)
			s++;
		else
			break;
	}
	return s;
}

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

void appendUtf8(uint32 cp, Common::Array<byte> &out) {
	if (cp < 0x80) {
		out.push_back(cp);
	} else if (cp < 0x800) {
		out.push_back(0xC0 | (cp >> 6));
		out.push_back(0x80 | (cp & 0x3F));
	} else if (cp < 0x10000) {
		out.push_back(0xE0 | (cp >> 12));
		out.push_back(0x80 | ((cp >> 6) & 0x3F));
		out.push_back(0x80 | (cp & 0x3F));
	} else {
		out.push_back(0xF0 | (cp >> 18));
		out.push_back(0x80 | ((cp >> 12) & 0x3F));
		out.push_back(0x80 | ((cp >> 6) & 0x3F));
		out.push_back(0x80 | (cp & 0x3F));
	}
}

bool textIsUtf8(const byte *p, const byte *end) {
	while (p < end && *p) {
		const int esc = scummEscapeLength(p, end);
		if (esc) {
			p += esc;
			continue;
		}
		int len;
		if (decodeUtf8(p, end, len) == 0xFFFFFFFF)
			return false;
		p += len;
	}
	return true;
}

void cp949ToUtf8(const byte *src, const byte *end, Common::Array<byte> &out, uint &unmapped) {
	// The pairing rule is upstream's (is2ByteCharacter(): a lead 0xB0-0xD0 takes the next byte, whatever it is);
	// every other byte >= 0x80 is a glyph of the game's own charset (MI1's 0xFA, say) and becomes U+0080..U+00FF,
	// which the renderer draws from the charset again. So the 1x picture stays upstream's.
	const byte *p = src;
	while (p < end && *p) {
		const int esc = scummEscapeLength(p, end);
		if (esc) {
			for (int i = 0; i < esc; i++)
				out.push_back(p[i]);
			p += esc;
			continue;
		}
		const byte c = *p;
		if (c < 0x80) {
			out.push_back(c);
			p++;
			continue;
		}
		if (c < 0xB0 || c > 0xD0 || p + 1 >= end || !p[1]) {
			appendUtf8(c, out);
			p++;
			continue;
		}
		uint32 cp = ksx1001CodePoint(c, p[1]);
		if (!cp) {
			// Hanja, or a CP949 extension syllable (trail < 0xA1): only encoding.dat knows it.
			const char pair[2] = { (char)c, (char)p[1] };
			const Common::U32String u(pair, 2, Common::kWindows949);
			if (u.size() == 1 && u[0] != '?' && u[0] >= 0x100 && u[0] != 0xFFFD)
				cp = u[0];
		}
		if (cp) {
			appendUtf8(cp, out);
		} else {
			debug(1, "Korean text: CP949 %02X %02X cannot be read, '?' written", c, p[1]);
			out.push_back('?');
			unmapped++;
		}
		p += 2;
	}
}

int toEngineText(const byte *src, int srcMax, byte *dst, int dstSize, uint *unmapped) {
	if (dstSize <= 0)
		return 0;
	const byte *end = scummTextEnd(src, src + srcMax);
	Common::Array<byte> buf;
	const byte *text;
	uint32 len;
	if (textIsUtf8(src, end)) {
		text = src;
		len = end - src;
	} else {
		uint n = 0;
		cp949ToUtf8(src, end, buf, n);
		if (unmapped)
			*unmapped += n;
		text = buf.begin();
		len = buf.size();
	}
	// Cut after the last whole character or escape that fits, never inside one.
	uint32 at = 0;
	const byte *tEnd = text + len;
	while (at < len) {
		int n = scummEscapeLength(text + at, tEnd);
		if (!n)
			decodeUtf8(text + at, tEnd, n);
		if ((int)(at + n) > dstSize - 1)
			break;
		at += n;
	}
	memmove(dst, text, at);
	dst[at] = 0;
	return at;
}

int koreanFinalBits(const byte *p, const byte *end) {
	int bits = 0;
	while (p < end && *p) {
		const int esc = scummEscapeLength(p, end);
		if (esc) {
			p += esc;
			continue;
		}
		int len;
		const uint32 cp = decodeUtf8(p, end, len);
		if (cp >= 0xAC00 && cp <= 0xD7A3) {
			const int jong = (cp - 0xAC00) % 28;
			bits = (jong ? 1 : 0) | (jong == 8 ? 2 : 0);
		}
		p += len;
	}
	return bits;
}

} // End of namespace Scumm
```

### B.4 engines/scumm/trs_bundle.h

```cpp
// (GPL header of engines/scumm/ks_check.h)

#ifndef SCUMM_TRS_BUNDLE_H
#define SCUMM_TRS_BUNDLE_H

#include "common/array.h"
#include "common/language.h"
#include "common/path.h"

namespace Scumm {

/**
 * The .trs translation bundle (the "SCVMTRS " file of string.cpp's loadLanguageBundle()) beyond what upstream reads.
 *
 * Names: "<code>.trs" (Common::translationTableName("%s.trs", lang): "ko.trs") first; a Korean game also takes the
 * established "korean.trs".
 *
 * Body (the bytes after the room table): one that starts with the UTF-8 BOM (EF BB BF) holds UTF-8 translations and is
 * used as it is; any other Korean body is CP949 and its translations are converted to UTF-8 once at load
 * (convertTrsBodyToUtf8()): the engine holds Korean text as UTF-8 (korean_text.h). The originals, the lookup keys, are
 * the game's own bytes and are never touched.
 */

void trsBundleNames(Common::Language lang, Common::Array<Common::Path> &out);

/** Whether @p body starts with EF BB BF. */
bool trsBodyIsUtf8(const byte *body, uint32 size);

/**
 * Rebuild a CP949 body into @p out (a BOM first, so the result is marked UTF-8): the strings at @p translations are
 * converted with cp949ToUtf8(), every other string is copied byte for byte. Both offset lists (relative to the body,
 * each at a string start) are moved to the same strings; false when an offset is not at a string start.
 */
bool convertTrsBodyToUtf8(const byte *body, uint32 size, Common::Array<uint32> &originals,
                          Common::Array<uint32> &translations, Common::Array<byte> &out, uint &unmapped);

} // End of namespace Scumm

#endif
```

### B.5 engines/scumm/trs_bundle.cpp

```cpp
// (GPL header of engines/scumm/ks_check.h)

#include "common/hashmap.h"
#include "common/language_folder.h"

#include "scumm/korean_text.h"
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

bool convertTrsBodyToUtf8(const byte *body, uint32 size, Common::Array<uint32> &originals,
                          Common::Array<uint32> &translations, Common::Array<byte> &out, uint &unmapped) {
	out.clear();
	unmapped = 0;
	out.push_back(0xEF);
	out.push_back(0xBB);
	out.push_back(0xBF);

	Common::HashMap<uint32, bool> isTranslation;
	for (uint i = 0; i < translations.size(); i++)
		isTranslation.setVal(translations[i], true);
	Common::HashMap<uint32, uint32> moved;
	const byte *end = body + size;
	uint32 pos = 0;
	while (pos < size) {
		const byte *s = body + pos;
		// The bundle's strings are NUL-terminated as the builder wrote them (an escape argument 0 ends one too:
		// MM's original "sound \xff\x04\xc8" is followed by its NUL).
		const byte *e = (const byte *)memchr(s, 0, end - s);
		if (!e)
			e = end;
		moved.setVal(pos, out.size());
		if (isTranslation.contains(pos))
			cp949ToUtf8(s, e, out, unmapped);
		else
			for (const byte *q = s; q < e; q++)
				out.push_back(*q);
		out.push_back(0);
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

} // End of namespace Scumm
```

### B.6 engines/scumm/korean_sentence.h

```cpp
// (GPL header of engines/scumm/ks_check.h)

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
 * particle on B ("왕에게 티켓 주기"). Text is UTF-8, the engine's Korean text
 * encoding (korean_text.h). Pure functions: nothing here needs a running engine.
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

/** Bytes of the UTF-8 character at @p p (never past a NUL). */
int koreanCharLength(const char *p);

/** The final of the last character of @p noun ('@' padding and end spaces skipped). */
KoreanFinal koreanFinalOf(const Common::String &noun);

/** The particle @p josa in the form @p final takes (UTF-8). */
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

### B.7 engines/scumm/korean_sentence.cpp

```cpp
// (GPL header of engines/scumm/ks_check.h)

#include "scumm/korean_sentence.h"
#include "scumm/korean_text.h"

namespace Scumm {

namespace {

// [josa][consonant, vowel, rieul, unknown], UTF-8
static const char *const kJosa[][4] = {
	{ "", "", "", "" },
	{ "\xec\x9d\x84", "\xeb\xa5\xbc", "\xec\x9d\x84", "\xec\x9d\x84(\xeb\xa5\xbc)" },	// 을 를 을 을(를)
	{ "\xec\x9d\x80", "\xeb\x8a\x94", "\xec\x9d\x80", "\xec\x9d\x80(\xeb\x8a\x94)" },	// 은 는 은 은(는)
	{ "\xec\x9d\xb4", "\xea\xb0\x80", "\xec\x9d\xb4", "\xec\x9d\xb4(\xea\xb0\x80)" },	// 이 가 이 이(가)
	{ "\xea\xb3\xbc", "\xec\x99\x80", "\xea\xb3\xbc", "\xea\xb3\xbc(\xec\x99\x80)" },	// 과 와 과 과(와)
	{ "\xec\x9c\xbc\xeb\xa1\x9c", "\xeb\xa1\x9c", "\xeb\xa1\x9c", "(\xec\x9c\xbc)\xeb\xa1\x9c" },	// 으로 로 로 (으)로
	{ "\xec\x97\x90", "\xec\x97\x90", "\xec\x97\x90", "\xec\x97\x90" },	// 에
	{ "\xec\x97\x90\xea\xb2\x8c", "\xec\x97\x90\xea\xb2\x8c", "\xec\x97\x90\xea\xb2\x8c", "\xec\x97\x90\xea\xb2\x8c" },	// 에게
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
	// UTF-8; a byte that starts no sequence counts as one character, and never step past a NUL.
	const byte c = (byte)*p;
	const int n = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
	for (int i = 1; i < n; i++) {
		if (!p[i])
			return i;
	}
	return n;
}

KoreanFinal koreanFinalOf(const Common::String &noun) {
	const Common::String s = clean(noun);
	if (s.empty())
		return kKoFinalUnknown;
	const uint start = lastCharStart(s);
	const byte *c = (const byte *)s.c_str() + start;
	int len;
	const uint32 cp = decodeUtf8(c, (const byte *)s.c_str() + s.size(), len);
	if (cp < 0xAC00 || cp > 0xD7A3)
		return kKoFinalNone;	// Latin, digits, symbols: read as vowel-final
	const int jong = (cp - 0xAC00) % 28;
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

### B.8 engines/scumm/korean.cpp (S1 part; in the prototype nextCJKChar also tests `!_hiResText`, an S2 line)

```cpp
// (GPL header of engines/scumm/ks_check.h)

#include "common/str.h"

#include "scumm/charset.h"
#include "scumm/korean_sentence.h"
#include "scumm/resource.h"
#include "scumm/saved_text.h"
#include "scumm/scumm_v2.h"
#include "scumm/korean_text.h"
#include "scumm/trs_bundle.h"
#include "scumm/verbs.h"

namespace Scumm {

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
	// Unlock (8) and Fix (6) ... with <B>: B is the tool (MM verb ids; jamo kMMVerbFix/kMMVerbUnlock; Zak has no
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

void ScummEngine::transcodeLanguageBundle(uint32 bodySize) {
	Common::Array<uint32> originals, translations;
	for (int i = 0; i < _numTranslatedLines; i++) {
		originals.push_back(_translatedLines[i].originalTextOffset);
		translations.push_back(_translatedLines[i].translatedTextOffset);
	}
	Common::Array<byte> body;
	uint unmapped = 0;
	if (!convertTrsBodyToUtf8(_languageBuffer, bodySize, originals, translations, body, unmapped)) {
		warning("The CP949 translation bundle is malformed (an entry inside a string): not used");
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
	if (unmapped)
		warning("CP949 translation bundle: %u characters could not be read and show as '?'", unmapped);
	debug(2, "loadLanguageBundle: CP949 body converted to UTF-8, %u -> %u bytes", bodySize, body.size());
}

static const int kKoreanRefusal = 0xFFFD;

int ScummEngine::nextCJKChar(int c, const byte *buf, int &pos) {
	if (!_koreanUtf8)
		return c + buf[pos++] * 256;
	int len;
	const byte *p = buf + pos - 1;
	const uint32 cp = decodeUtf8(p, p + 4, len);	// a NUL is no continuation byte, so it stops the read
	pos += len - 1;
	// U+0080..U+00FF is a glyph of the game's charset (cp949ToUtf8()). korean.fnt has the 2350 KS X 1001 syllables;
	// anything else is refused visibly (once logged).
	if (cp >= 0x80 && cp < 0x100)
		return cp;
	if (cp == 0xFFFFFFFF || (!_hiResText && ksx1001Index(cp) < 0)) {
		static bool warned = false;
		if (!warned) {
			warning("Korean text: U+%04X has no glyph in korean.fnt, drawn as '?' (further ones not logged)", cp);
			warned = true;
		}
		return kKoreanRefusal;
	}
	return cp;
}

int ScummEngine::koreanCharsetGlyph(const byte *p) const {
	if (!_koreanUtf8 || (p[0] != 0xC2 && p[0] != 0xC3))
		return -1;
	int len;
	const uint32 cp = decodeUtf8(p, p + 2, len);
	return (cp >= 0x80 && cp < 0x100) ? (int)cp : -1;
}

int ScummEngine::cjkTrailBytes(const byte *p) const {
	if (!_koreanUtf8)
		return 1;
	int len;
	decodeUtf8(p, p + 4, len);
	return len - 1;
}

int ScummEngine::koreanCharStart(const byte *str, int pos) const {
	if (!_koreanUtf8)
		return pos - 2;
	int s = pos - 1;
	while (s > 0 && pos - s < 4 && (str[s] & 0xC0) == 0x80)
		s--;
	return s;
}

bool ScummEngine::koreanBreakableAt(const byte *p) const {
	if (!_koreanUtf8)
		return checkKSCode(p[0], p[1]);
	int len;
	const uint32 cp = decodeUtf8(p, p + 4, len);
	return cp >= 0xAC00 && cp <= 0xD7A3;
}

bool ScummEngine::koreanCharBefore(const byte *begin, const byte *p) const {
	if (!_koreanUtf8)
		return p - 2 >= begin && checkKSCode(p[-2], p[-1]);
	const byte *s = p - 1;
	while (s > begin && p - s < 4 && (*s & 0xC0) == 0x80)
		s--;
	return s >= begin && koreanBreakableAt(s);
}

void ScummEngine::copyTranslatedText(byte *dst, const byte *src, int dstSize) {
	if (_koreanUtf8)
		toEngineText(src, resStrLen(src) + 1, dst, dstSize);
	else
		memcpy(dst, src, MIN<int>(resStrLen(src) + 1, dstSize));
}

byte *ScummEngine::koreanRefusalGlyph() {
	// '?' scaled into the current 2-byte cell, so the line keeps its metrics.
	static const byte kQuestion[8] = { 0x3C, 0x66, 0x06, 0x0C, 0x18, 0x18, 0x00, 0x18 };
	static byte glyph[512];
	const int w = CLIP(_2byteWidth, 1, 32), h = CLIP(_2byteHeight, 1, 32);
	const int stride = (w + 7) / 8;
	memset(glyph, 0, sizeof(glyph));
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			if (kQuestion[y * 8 / h] & (0x80 >> (x * 8 / w)))
				glyph[y * stride + x / 8] |= 0x80 >> (x % 8);
	return glyph;
}

void ScummEngine::convertLoadedText(int saved) {
	// A save of this engine holds UTF-8 (trailer 1). One without the trailer (upstream, the CP949 builds) holds CP949:
	// every text resource goes through the engine-text boundary, which leaves UTF-8 and ASCII as they are.
	if (saved == kSavedTextUtf8)
		return;
	static const ResType kTextTypes[] = { rtVerb, rtObjectName, rtActorName, rtString };
	int converted = 0;
	Common::Array<byte> tmp;
	for (uint t = 0; t < ARRAYSIZE(kTextTypes); t++) {
		const ResType type = kTextTypes[t];
		if (type == rtString && _game.version >= 6)
			continue;	// a v6 string resource is an array, not text
		for (ResId idx = 0; idx < _res->_types[type].size(); idx++) {
			const byte *ptr = _res->_types[type][idx]._address;
			const uint32 size = _res->_types[type][idx]._size;
			if (!ptr || !size)
				continue;
			if (type == rtVerb && (idx >= _numVerbs || _verbs[idx].type != kTextVerbType))
				continue;
			if (textIsUtf8(ptr, ptr + size))
				continue;
			tmp.resize(size * 2 + 4);
			const int len = toEngineText(ptr, size, tmp.begin(), tmp.size());
			byte *dst = _res->createResource(type, idx, len + 1);
			memcpy(dst, tmp.begin(), len + 1);
			converted++;
		}
	}
	if (!textIsUtf8(_charsetBuffer, _charsetBuffer + sizeof(_charsetBuffer))) {
		byte msg[sizeof(_charsetBuffer)];
		toEngineText(_charsetBuffer, sizeof(_charsetBuffer), msg, sizeof(msg));
		memcpy(_charsetBuffer, msg, sizeof(msg));
		converted++;
	}
	debug(1, "SCUMM: converted %d text resources of a CP949 save to UTF-8", converted);
}

} // End of namespace Scumm
```

### B.9 test/engines/scumm/korean_text.h

```cpp
#include <cxxtest/TestSuite.h>

#include "engines/scumm/korean_text.h"

// Korean engine text: UTF-8 inside, CP949 converted at the boundary (engines/scumm/korean_text.h).
class ScummKoreanTextTestSuite : public CxxTest::TestSuite {
	static Common::String engineText(const char *src, int dstSize = 256, uint *unmapped = nullptr) {
		byte dst[256];
		const int n = Scumm::toEngineText((const byte *)src, strlen(src) + 1, dst, dstSize, unmapped);
		return Common::String((const char *)dst, n);
	}

public:
	void test_ksx1001_table() {
		TS_ASSERT_EQUALS(Scumm::kKsx1001Hangul[0], 0xAC00);	// 가
		TS_ASSERT_EQUALS(Scumm::kKsx1001Hangul[2349], 0xD79D);	// 힝
		for (int i = 1; i < 2350; i++)
			TS_ASSERT_LESS_THAN(Scumm::kKsx1001Hangul[i - 1], Scumm::kKsx1001Hangul[i]);
		TS_ASSERT_EQUALS(Scumm::ksx1001Index(0xAC00), 0);
		TS_ASSERT_EQUALS(Scumm::ksx1001Index(0xD55C), (0xC7 - 0xB0) * 94 + (0xD1 - 0xA1));	// 한 = C7 D1
		TS_ASSERT_EQUALS(Scumm::ksx1001Index(0xB620), -1);	// 똠: not in KS X 1001
		TS_ASSERT_EQUALS(Scumm::ksx1001CodePoint(0xC7, 0xD1), 0xD55Cu);
		TS_ASSERT_EQUALS(Scumm::ksx1001CodePoint(0xA1, 0xA6), 0u);
	}

	void test_cp949_to_utf8_keeps_escapes() {
		uint unmapped = 0;
		// 한 FF 04 xx xx 글: an escape's arguments are not text.
		TS_ASSERT_EQUALS(engineText("\xc7\xd1\xff\x04\xb0\xa1\xb1\xdb", 256, &unmapped), "\xed\x95\x9c\xff\x04\xb0\xa1\xea\xb8\x80");
		// A trail byte 0xFE is part of its syllable (BC FE), FE 01 is an escape.
		TS_ASSERT_EQUALS(engineText("\xbc\xfe\xfe\x01" "a", 256, &unmapped), "\xec\x88\xad\xfe\x01" "a");
		TS_ASSERT_EQUALS(unmapped, 0u);
	}

	void test_boundary_is_idempotent() {
		const Common::String once = engineText("\xc7\xd1\xb1\xdb ok");	// 한글 ok
		TS_ASSERT_EQUALS(once, "\xed\x95\x9c\xea\xb8\x80 ok");
		TS_ASSERT_EQUALS(engineText(once.c_str()), once);
		TS_ASSERT_EQUALS(engineText("Give"), "Give");
	}

	void test_cut_on_a_character() {
		// 한글 is 6 bytes of UTF-8; a 6-byte buffer holds 5 + NUL: only 한.
		TS_ASSERT_EQUALS(engineText("\xed\x95\x9c\xea\xb8\x80", 6), "\xed\x95\x9c");
		// An escape is never cut either: "a" FF 04 xx xx in 5 bytes keeps "a".
		TS_ASSERT_EQUALS(engineText("a\xff\x04\x01\x02", 5), "a");
	}

	void test_final_bits() {
		const byte king[] = "\xec\x99\x95";	// 왕
		const byte table[] = "\xed\x85\x8c\xec\x9d\xb4\xeb\xb8\x94";	// 테이블
		const byte key[] = "\xec\x97\xb4\xec\x87\xa0 ab";	// 열쇠 ab
		TS_ASSERT_EQUALS(Scumm::koreanFinalBits(king, king + sizeof(king)), 1);
		TS_ASSERT_EQUALS(Scumm::koreanFinalBits(table, table + sizeof(table)), 3);
		TS_ASSERT_EQUALS(Scumm::koreanFinalBits(key, key + sizeof(key)), 0);
	}

	void test_utf8_judgement() {
		const byte utf8[] = "\xea\xb0\x80\xff\x04\xb0\xa1";
		const byte cp949[] = "\xc7\xd1";
		TS_ASSERT(Scumm::textIsUtf8(utf8, utf8 + sizeof(utf8)));
		TS_ASSERT(!Scumm::textIsUtf8(cp949, cp949 + sizeof(cp949)));
	}
};
```

### B.10 test/engines/scumm/trs_bundle.h

```cpp
#include <cxxtest/TestSuite.h>

#include "engines/scumm/korean_text.h"
#include "engines/scumm/trs_bundle.h"

// Bundle names and the CP949 -> UTF-8 rebuild of a body (engines/scumm/trs_bundle.h).
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
		Scumm::trsBundleNames(Common::UNK_LANG, names);
		TS_ASSERT_EQUALS(names.size(), 0u);
		const byte bom[] = { 0xEF, 0xBB, 0xBF, 'a' };
		TS_ASSERT(Scumm::trsBodyIsUtf8(bom, 4));
		TS_ASSERT(!Scumm::trsBodyIsUtf8(bom + 1, 3));
	}

	void test_nul_ends_a_bundle_string() {
		// MM's bundle: an original ends in an escape whose argument is 0 ("sound " FF 04 C8 00); the next string
		// starts after that NUL. A trail byte 0xFE (BC FE) stays part of its syllable.
		const byte body[] = { 's', 0xFF, 0x04, 0xC8, 0x00, 0xBC, 0xFE, 0x00 };
		Common::Array<uint32> orig, trans;
		orig.push_back(0);
		trans.push_back(5);
		Common::Array<byte> out;
		uint unmapped = 0;
		TS_ASSERT(Scumm::convertTrsBodyToUtf8(body, sizeof(body), orig, trans, out, unmapped));
		TS_ASSERT_EQUALS(Common::String((const char *)&out[trans[0]]), "\xec\x88\xad");
		TS_ASSERT_EQUALS(unmapped, 0u);
	}

	void test_body_to_utf8() {
		// original "Hi" @0, translation 안녕 @3 (CP949 BE C8 B3 E7), original 0x99 @8, translation "x" @10
		const byte body[] = { 'H', 'i', 0, 0xBE, 0xC8, 0xB3, 0xE7, 0, 0x99, 0, 'x', 0 };
		Common::Array<uint32> orig, trans;
		orig.push_back(0); orig.push_back(8);
		trans.push_back(3); trans.push_back(10);
		Common::Array<byte> out;
		uint unmapped = 0;
		TS_ASSERT(Scumm::convertTrsBodyToUtf8(body, sizeof(body), orig, trans, out, unmapped));
		TS_ASSERT(Scumm::trsBodyIsUtf8(out.begin(), out.size()));
		TS_ASSERT_EQUALS(orig[0], 3u);
		TS_ASSERT_EQUALS(Common::String((const char *)&out[trans[0]]), "\xec\x95\x88\xeb\x85\x95");
		TS_ASSERT_EQUALS(out[orig[1]], 0x99);	// the game's key byte, untouched
		TS_ASSERT_EQUALS(Common::String((const char *)&out[trans[1]]), "x");
		TS_ASSERT_EQUALS(unmapped, 0u);
		trans[1] = 11;	// inside a string
		TS_ASSERT(!Scumm::convertTrsBodyToUtf8(body, sizeof(body), orig, trans, out, unmapped));
	}
};
```

### B.11 test/engines/scumm/korean_sentence.h

```cpp
#include <cxxtest/TestSuite.h>

#include "engines/scumm/korean_sentence.h"

// The v2 sentence line in Korean (engines/scumm/korean_sentence.h). Strings are UTF-8.
#define KO_TICKET "\xed\x8b\xb0\xec\xbc\x93"   // 티켓 (final ㅅ)
#define KO_KING   "\xec\x99\x95"           // 왕 (consonant-final)
#define KO_DOOR   "\xeb\xac\xb8"           // 문 (consonant-final)
#define KO_KEY    "\xec\x97\xb4\xec\x87\xa0"   // 열쇠 (vowel-final)
#define KO_GIVE   "\xec\xa3\xbc\xea\xb8\xb0"   // 주기
#define KO_OPEN   "\xec\x97\xb4\xea\xb8\xb0"   // 열기
#define KO_WHAT   "\xec\x9d\xb4\xea\xb2\x8c \xeb\xad\x90\xec\xa7\x80?" // 이게 뭐지?

class ScummKoreanSentenceTestSuite : public CxxTest::TestSuite {
	static int bytesWide(const Common::String &s, void *) { return (int)s.size() * 2; }

public:
	void test_final_and_josa() {
		TS_ASSERT_EQUALS(Scumm::koreanFinalOf(KO_KING), Scumm::kKoFinalConsonant);
		TS_ASSERT_EQUALS(Scumm::koreanFinalOf(KO_TICKET "@@"), Scumm::kKoFinalConsonant);
		TS_ASSERT_EQUALS(Scumm::koreanFinalOf(KO_KEY " "), Scumm::kKoFinalNone);
		TS_ASSERT_EQUALS(Scumm::koreanFinalOf("\xed\x85\x8c\xec\x9d\xb4\xeb\xb8\x94"), Scumm::kKoFinalRieul); // 테이블
		TS_ASSERT_EQUALS(Scumm::koreanFinalOf("sign"), Scumm::kKoFinalNone);
		TS_ASSERT_EQUALS(Scumm::koreanFinalOf(""), Scumm::kKoFinalUnknown);
		TS_ASSERT_EQUALS(Scumm::koreanJosa(Scumm::kKoFinalRieul, Scumm::kJosaMeans), "\xeb\xa1\x9c");
		TS_ASSERT_EQUALS(Scumm::koreanJosa(Scumm::kKoFinalConsonant, Scumm::kJosaMeans), "\xec\x9c\xbc\xeb\xa1\x9c");
	}

	void test_compose() {
		Scumm::KoreanSentenceParts p;
		p.verb = KO_GIVE;
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(p), KO_GIVE);
		p.objA = KO_TICKET; p.hasA = true; p.prep = 4;
		// B not chosen yet: "...에게 티켓 주기"
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(p), "...\xec\x97\x90\xea\xb2\x8c " KO_TICKET " " KO_GIVE);
		p.objB = KO_KING; p.hasB = true; p.bIsActor = true;
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(p), KO_KING "\xec\x97\x90\xea\xb2\x8c " KO_TICKET " " KO_GIVE);
		// Open door with key (MM "unlock ... with"): the tool takes (으)로; 열쇠 is vowel-final -> 로.
		Scumm::KoreanSentenceParts q;
		q.verb = KO_OPEN; q.objA = KO_DOOR; q.hasA = true; q.objB = KO_KEY; q.hasB = true; q.prep = 2; q.instrumentB = true;
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(q), KO_KEY "\xeb\xa1\x9c " KO_DOOR " " KO_OPEN);
		// "What is" stays first.
		Scumm::KoreanSentenceParts w;
		w.verb = KO_WHAT; w.objA = "sign"; w.hasA = true;
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(w), KO_WHAT " sign");
	}

	void test_fit_never_cuts_a_character() {
		Scumm::KoreanSentenceParts p;
		p.verb = KO_GIVE; p.objA = KO_TICKET KO_TICKET KO_TICKET; p.hasA = true;
		// 4 px a byte; "티켓티켓티켓 주기" = 17 bytes = 68 px; 40 px leaves A two characters + " 주기".
		const Common::String fit = Scumm::fitKoreanSentence(p, 40, bytesWide, nullptr);
		TS_ASSERT_EQUALS(fit, KO_TICKET KO_TICKET " " KO_GIVE);
		TS_ASSERT(fit.size() * 2 <= 40);
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

### B.12 engines/scumm/saved_text.{h,cpp} and test/engines/scumm/saved_text.h

```cpp
// (GPL header of engines/scumm/ks_check.h)

#ifndef SCUMM_SAVED_TEXT_H
#define SCUMM_SAVED_TEXT_H

#include "common/stream.h"

namespace Scumm {

/**
 * The encoding of the translated text a Korean save holds (verbs, object and actor names, strings, the message being
 * printed: what loadPtrToResource() stored). A trailer after the serialized state, where a build without it never
 * reads: tag 'TENC' and one byte, 1 for UTF-8 (the jamo branch's format, kept). This engine writes 1. A save without a
 * trailer (upstream, the CP949 builds) or with 0 holds CP949 and is converted on load (ScummEngine::convertLoadedText()).
 */
enum SavedTextEncoding {
	kSavedTextUnknown = -1, ///< no trailer
	kSavedTextLegacy = 0,   ///< CP949
	kSavedTextUtf8 = 1
};

void writeSavedTextEncoding(Common::WriteStream &out, bool utf8);

/** The trailer at the stream's position; kSavedTextUnknown without one. */
SavedTextEncoding readSavedTextEncoding(Common::SeekableReadStream &in);

} // End of namespace Scumm

#endif
```

```cpp
// (GPL header of engines/scumm/ks_check.h)

#include "common/endian.h"

#include "scumm/saved_text.h"

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

} // End of namespace Scumm
```

```cpp
#include <cxxtest/TestSuite.h>

#include "common/file.h"
#include "common/fs.h"
#include "common/archive.h"
#include "common/memstream.h"

#include "engines/scumm/saved_text.h"
#include "engines/scumm/korean_text.h"

#include "test/system/null_osystem.h"

// The text-encoding trailer of a Korean save (engines/scumm/saved_text.h) and the font index guard (trs_bundle.h).
class ScummSavedTextTestSuite : public CxxTest::TestSuite {
public:
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

	void test_refusal_index() {
		// korean.fnt has 2350 glyphs: a code point outside them has no index (the engine draws its '?').
		TS_ASSERT_EQUALS(Scumm::ksx1001Index(0x3131), -1);	// ㄱ (compatibility jamo)
		TS_ASSERT_EQUALS(Scumm::ksx1001Index(0x2026), -1);	// …
		TS_ASSERT_EQUALS(Scumm::ksx1001Index(0xD79D), 2349);
	}
};
```

`engines/scumm/ksx1001_table.cpp` is generated (B.1), 227 lines; not repeated here.
