# Jamo-set bitmap fonts Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An SVFN font flagged as jamo sets composes all 11,172 Hangul syllables at run time from positional pieces, through a table that describes any DOS set system; a tool builds such fonts from DOS-era sources.

**Architecture:** A JSET block after the SVFN code point table (flag bit 1) holds class and set tables; `JamoSetTable` parses and validates it; `JamoGlyphSource` wraps an `SvfnGlyphSource` and composes syllables by per-pixel max; `createSvfnSource()` picks it by the flag. `tools/korean/jamoset.py` writes the files from named layouts.

**Tech Stack:** C++ (ScummVM `graphics/hires_text`, cxxtest), Python 3 (Pillow not needed).

**Spec:** `docs/superpowers/specs/2026-10-14-jamo-set-fonts-design.md` (read it first; it is binding).

## Global Constraints

- Branch `jamo`, worktree `~/work/scummvm/jamo`; build dir `~/work/scummvm/builds/jamo-test`, configured as
  `~/work/scummvm/jamo/configure --disable-all-engines --enable-engine=sci,scumm,agi --disable-engine=sci32,scumm_7_8,he --enable-debug-socket --with-sdl-prefix=/home/thkim/.local/sysroot/usr`. Run `make -j16 test` there; redirect output to a file and read its tail.
- Commit trailer, every commit:
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` and
  `Claude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4`.
- `git add` files by name; never `git add -A`, never bare `git stash`; never push or merge.
- Never commit runs/, dist/, gamedata/, *.bin dumps.
- Match the surrounding code: tabs, ScummVM style, comment density of `glyph_source_svfn.cpp`; Python like `tools/korean/johab844.py`.
- A font without flag bit 1 must behave byte for byte as before (existing tests stay green).
- One emulator/DOSBox at a time; do not rebuild the shared SDL3.

## Review Focus

1. A JSET whose sizes do not add up to the bytes left in the file (truncated file) - must load as plain SVFN with one warning, never read past the buffer, streamed or whole.
2. Syllables with no final whose finSet entry is garbage - must not be read (tail 0 skips the final).
3. Streamed fonts (`loadStreamed`, data > HIRES_SVF_STREAM_MIN): the JSET must be read into memory with the tables, pieces read through the inner source.
4. Row pointer lifetime: composing must copy piece rows before asking for the next piece.
5. 2bpp and 8bpp pieces: max per pixel in the font's own packing, not OR of bytes.

---

### Task 1: JamoSetTable - parse and validate the JSET block

**Files:**
- Create: `graphics/hires_text/jamo_set.h`, `graphics/hires_text/jamo_set.cpp`
- Modify: `graphics/module.mk` (add `hires_text/jamo_set.o`), `graphics/hires_text/bitmap_font.{h,cpp}` (rename the reserved bit 1 to `kFlagJamoSets`; when set, read the block that starts at `cmapOff + glyphs*8` into memory in both `loadWhole` and `loadStreamed`; expose `const JamoSetTable *jamoSets() const` (nullptr when absent or invalid) and `const Common::String &jamoSetError() const`)
- Test: `test/graphics/hires_text_jamo.h` (new; `test/module.mk` already globs `hires_text*.h`)

**Interfaces:**
- Produces:
  ```cpp
  struct JamoSetTable {
  	byte leadClasses, tailClasses, initSets, medSets, finSets, advance;
  	byte leadClass[19], tailClass[28];
  	Common::Array<byte> initSet, medSet, finSet;      // LC*21*TC each
  	Common::Array<uint16> initPiece, medPiece, finPiece; // NI*19, NM*21, NF*27
  	uint16 compatPiece[51];
  	/// Pieces (piece numbers) of syllable cp in draw order; 2 or 3; false if not a syllable.
  	bool pieces(uint32 cp, uint16 out[3], int &count) const;
  	/// Piece number for compatibility jamo cp, or 0xFFFF.
  	uint16 compat(uint32 cp) const;
  };
  /// Parses @p size bytes at @p data; false with @p error set when malformed.
  bool parseJamoSetTable(const byte *data, uint32 size, JamoSetTable &out, Common::String &error);
  static const uint32 kJamoPieceBase = 0xF0000;
  ```
- Validation: magic/version; 1<=LC,TC<=4; 1<=NI,NM,NF<=16; every class < its count; every set < its count; block length exact; piece numbers either 0xFFFF or < 0x10000 (whether a glyph exists is checked in Task 2, which has the font).

- [ ] Step 1: write tests in `test/graphics/hires_text_jamo.h`: a helper building a valid 8x4x4 JSET (lead classes: ㄱ,ㅋ -> 0, others 1; tail classes: 0 -> 0, else 1; the 844 selection tables from `tools/korean/johab844.py`; pieces numbered as johab844's `pieces()`); assert `pieces()` for 가 (U+AC00), 각, 뷁 (U+BDC1), 흙 equal johab844.py's piece numbers (compute them by hand from its formulas and write the literals in the test); a 2-piece result for no-tail syllables; `compat(0x3131)`; each malformed case (bad magic, LC=5, class out of range, set out of range, truncated by one byte) returns false with an error naming the field.
- [ ] Step 2: run `make -j16 test` -> fails to compile (no jamo_set.h).
- [ ] Step 3: implement `jamo_set.{h,cpp}`; wire into bitmap_font (flag rename, block read, accessors; an invalid block leaves `jamoSets()` nullptr and the error string set; the font still loads).
- [ ] Step 4: add tests that a whole-loaded and a streamed SVFN with the flag expose the table, and that one with a truncated block loads, `jamoSets()` nullptr, error set. Run `make -j16 test` -> green.
- [ ] Step 5: commit `GRAPHICS: Parse the JSET jamo-set table of an SVFN`.

### Task 2: JamoGlyphSource - compose syllables

**Files:**
- Create: `graphics/hires_text/glyph_source_jamo.{h,cpp}`; Modify: `graphics/module.mk`
- Test: `test/graphics/hires_text_jamo.h`

**Interfaces:**
- Consumes: Task 1's `JamoSetTable`, `HiResBitmapFont::jamoSets()`; `SvfnGlyphSource` (glyph_source_svfn.h).
- Produces: `class JamoGlyphSource : public UnicodeGlyphSource` constructed `(HiResBitmapFont *font, DisposeAfterUse::Flag)`; owns an inner `SvfnGlyphSource`; `static bool usable(const HiResBitmapFont &font, Common::String &why)` = table present and every non-0xFFFF piece has a glyph at kJamoPieceBase+n.

Behaviour per spec "The engine": syllables composed (max per pixel at the font's bpp, rows stride cellWidth()*2 like SvfnGlyphSource, second cell blank only when the syllable is narrower than 2 cells - a syllable is 2 cells), compat jamo by piece, all else delegated; `cells()` 2 for syllables; `advance()` the table's advance or cellWidth(); `metrics()` like SvfnGlyphSource's for delegated cps, wide=true for syllables; `baselineRow()` inner's; `glyphCount()` inner's; LRU cache of composed rows sized like SvfnGlyphSource (HIRES_SVF_CACHE_KB); `prefetch()` replaces syllables in the list by their piece code points before calling the inner prefetch, and removes the syllables it can compose.

- [ ] Step 1: tests: synthetic 1bpp 8x4x4 font, 16x16, where piece n has a single set pixel at a position derived from n (e.g. x = n % 16, y = n / 16 % 16) - assert the composed 가/각/뷁 rows have exactly the pixels of their pieces; a 2bpp font where two pieces overlap at one pixel with levels 1 and 3 -> 3; compat ㄱ = its piece; 'A' from the inner font; a syllable when a piece glyph is missing -> `usable()` false; cache: drawing 300 different syllables then 가 again re-composes correctly.
- [ ] Step 2: `make -j16 test` -> fails (no class).
- [ ] Step 3: implement.
- [ ] Step 4: `make -j16 test` -> green.
- [ ] Step 5: commit `GRAPHICS: Compose Hangul from a jamo-set font`.

### Task 3: Pick it from the file

**Files:** Modify `graphics/hires_text/glyph_source_file.cpp`, `graphics/hires_text/README.md` (SVFN format section: flag bit 1, the JSET layout copied from the spec); Test `test/graphics/hires_text_jamo.h`.

- [ ] Step 1: tests: `createSvfnSource()` on the synthetic jamo font returns a source that draws 가; on a jamo font with an invalid table returns a plain SVFN source and the error is warned once (use the existing warning capture the other hires tests use; find it with grep "hasWarning\|warnings" test/graphics); on KO2350-like plain font unchanged.
- [ ] Step 2: run -> fail. Step 3: implement (`usable()` false -> plain source + `warning("%s: jamo sets ignored: %s", name, why)` once). Step 4: green. Step 5: commit `GRAPHICS: Open a jamo-set SVF as a composing source`.

### Task 4: tools/korean/jamoset.py

**Files:** Create `tools/korean/jamoset.py`, `tools/korean/test_jamoset.py`. Reuse SVFN writing from `tools/korean/pairing2svf.py` (`svfn_bytes`) or `johab844.py`; readers for raw 8x4x4 / 10x4x4 / 6x2x1 / printer files from `~/work/johab-fonts/tools/kfont.py` (copy the needed functions in, crediting the source file in a comment).

Layouts (each a function returning the JSET tables and the source-glyph -> piece mapping), rules from `docs/johab/2026-10-14-johab-font-research.md` 3.1-3.4 and `docs/johab/2026-10-14-johab-font-formats.md` 2.1-2.9: `844`, `844-ihnm`, `1044-san`, `621`, `hanterm-johab`, `dkb-printer-7x2x2`, `dkb-printer-12x4x4`, `kyra-lok`.

CLI: `jamoset.py --layout NAME SRC [--face N] [--eng ENG | --base SVF] -o OUT [--check]`. Output: SVFN v2, flag bit 1, pieces at U+F0000+n, ASCII (and with --base, every non-Hangul glyph of the base), compat jamo via compatPiece (lead jamo -> its set-0 initial piece, vowels -> set-0 medial, tail-only jamo -> set-0 final).

- [ ] Step 1: tests: for 844, composing every syllable in Python from the written tables equals `johab844.compose()` for all 11,172 (build a synthetic 360-piece source in the test with distinct pixels per piece); for each other layout, 5 syllables per layout hand-checked against the research doc's formulas; the written file parses back (header flag, JSET length).
- [ ] Step 2: run `python3 tools/korean/test_jamoset.py` -> fails. Step 3: implement. Step 4: green.
- [ ] Step 5: commit `TOOLS: Build jamo-set SVF fonts from DOS Johab sources`.

### Task 5: End to end

- [ ] Step 1: build `KOGAMJ.SVF` from `~/work/johab-fonts` san-1.0 `GAM.KOR` + `SAN.ENG` (find them with `find ~/work/johab-fonts -iname 'GAM.KOR' -o -iname 'SAN.ENG'`), layout 844, into `~/work/scummvm/runs/jamo/` (not committed); record its size vs KO2350.SVF.
- [ ] Step 2: render check without a game: a small test in `test/graphics/hires_text_jamo.h` is not possible with a file outside the tree, so instead write `runs/jamo/render.py` that composes "다람쥐 헌 쳇바퀴에 타고파 뷁 흙 똠" with jamoset's Python composer to a PNG, and run one SCUMM or SCI target in the `builds/jamo-test` binary with a hi-res map whose `[fonts]` names `KOGAMJ.SVF` (copy an existing map from `dists/engine-data/hires_text/` and change the face; use the debug socket `state` / `textlog` and a screenshot the way `tools/korean/shot.sh` does). Save the screenshot in runs/jamo/.
- [ ] Step 3: report sizes, screenshot path, and any differences between the engine's and Python's composition.

No commit in Task 5 unless a fix was needed (then commit the fix with its test).
