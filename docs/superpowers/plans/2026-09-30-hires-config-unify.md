# Hi-res Text Configuration Unification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace every hi-res text map key and ini key of SCI, SCUMM, AGS and the DOS backend with the one scheme of the
spec (version-2 maps, `[render]`/`[font]`/`[font.N]`/`range.<spec>`/`[glyphs]` with targeted glyphs, `render_target`,
`hires_text_blend`, one scale key), delete the old parser and old keys, rewrite every map, ini, generator, test and doc,
and pass the SCI (LOADING, M0-M3) and SCUMM (M5) acceptance suites on both DOSBoxes.

**Architecture:** The new parser is built beside the old one in `graphics/hires_text/` (pure modules: Unicode ranges, font
values, options, the map loader, the per-id compiled plan, a shared ranged glyph source), so every task keeps all engines
compiling. Each engine then switches to the new loader in its own task; the old parser is deleted once nothing uses it;
the shipped maps and generators are converted last, from whatever is committed when that task runs. The two features the
user marked as planned (SCI RGB565, CLUT8 palette-matched anti-aliasing) are separate tasks at the end.

**Tech Stack:** C++ (ScummVM, C++11 subset), CxxTest (`make test`), Python 3 (tools, harness), DJGPP 12.2 + SDL3 for
DOS, DOSBox-X 2026.08 and DOSBox Staging 0.83 through `~/work/scummvm/harness/dos/*.py`.

**Spec:** `docs/superpowers/specs/2026-09-30-hires-config-unify-design.md` (every key, value, default, precedence and
warning is defined there; section numbers below refer to it). Background and file:line inventory:
`.superpowers/sdd/2026-09-29-dos-m5-scumm/config-audit.md`.

## Global Constraints

- Branch `dos-port`, worktree `/home/thkim/work/scummvm/dos`. Harness repo `/home/thkim/work/scummvm` (branch `master`).
- **Another agent commits in the same worktree** (it is moving the SCI packages to shared 2350 fonts and edits
  `dists/engine-data/hires_text/dos/*` and `harness/dos/release/*`). So: `git add <named files>` / `git commit -- <paths>`
  only, never `git add -A`, `git add .`, `git commit -a`, or bare `git stash`. If `.git/index.lock` exists, wait and retry.
  Never edit, add or revert a file you did not create or were not told to change in your task.
- Every commit message ends with:
  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4
  ```
  Command form for every commit step: `git add <each new or changed file>` then
  `git commit -m "<subject>" -m "<body, optional>" -m "<the two trailer lines>" -- <the same paths>` (`git mv` for renames,
  `git rm` for deletions). Prefixes: `GRAPHICS:`, `TEST:`, `SCUMM:`, `SCI:`, `AGS:`, `DOS:`, `DISTS:`, `TOOLS:`, `DOCS:`; harness commits
  `harness/dos: ...`. Do not push.
- **No backward compatibility** (spec section 0): no alias, no fallback reading of an old key. Old keys are deleted, not
  deprecated. Upstream options (`rgb_rendering`, `palette_mods`, `render_mode`, `disable_dithering`, `text_encoding`) keep
  upstream behaviour.
- No DOS `#ifdef` in `engines/`, `graphics/`, `common/`: the DOS 2x limit reaches shared code only through the
  backend-registered default `hires_text_platform_scale` (spec 7.4).
- Shared set (spec section 13): `graphics/hires_text/{font_map,font_value,unicode_ranges,hires_options,id_plan,glyph_source_ranged}.{h,cpp}`
  and `test/graphics/hires_text_{font_map,font_value,unicode_ranges,hires_options,id_plan,glyph_source_ranged}.h`. Only
  `dos-port` edits them during this plan; `i18n` is not touched after Task 0 until Task 21.
- Linux unit tests. Build directories (out of tree, configured from this worktree):
  - `/home/thkim/work/scummvm/builds/linux-dos-test-scumm` (SCI + SCUMM) - the main one for every task.
  - `/home/thkim/work/scummvm/builds/linux-dos-test` (SCI only).
  - `/home/thkim/work/scummvm/builds/linux-dos-test-ags` (AGS only; created in Task 12).
  Command: `make -C <dir> -j8 test 2>&1 | tee /tmp/claude-1000/-home-thkim-work/36a78c74-674a-4d24-9ca3-7236f6e2ebde/scratchpad/test-<dir>.log | tail -5`.
  **Known baseline: exactly one failing test, `test/graphics/hires_text_ttf_fit.h:493`
  (`test_pad_rows_moves_the_glyph_down_whole`).** Any other failure is a regression. New tests raise the test count;
  record it in your report.
- DOS builds: `cd /home/thkim/work/scummvm/dos && backends/platform/dos/build-dos.sh sci` (stages
  `/home/thkim/work/scummvm/dist/dos/SCUMMVM.EXE` + `DATA/`) and `... build-dos.sh scumm` (`SCUMM.EXE`). The toolchain
  is `source ~/opt/dos-dev/env.sh` (the script sources it).
- Acceptance (harness, from `/home/thkim/work/scummvm`): `python3 harness/dos/<s>_accept.py x|staging` for
  `s` in `loading m0 m1 m2 m3 m5`. Baseline: every one prints `... <emu>: PASS` (M3's secondary music window is a known
  wobble: on a FAIL rerun once and record both). Only one DOS run at a time uses debug port 5555.
- DOS runs and `dist/dos` builds: one DOS run at a time (debug port 5555), and the other agent may be packaging from
  `dist/dos` - check with the controller before `build-dos.sh` or any `*_accept.py` run.
- Maps and fonts: **start from what is committed when your task runs**, never from a copy in this plan. Examples here show
  the shape of the result, not file contents to paste.
- Tests are written first and must fail (compile error or assertion) before the implementation; show that in the report.
- Every warning text the spec quotes is matched exactly by a test (tests read `HiResMap::warnings` or the returned
  warning arrays, never stdout).

## File Structure

| File | Responsibility | Task |
|---|---|---|
| `graphics/hires_text/unicode_ranges.{h,cpp}` (new) | block table, range-spec parsing, the compiled range table with page cache | 1 |
| `graphics/hires_text/font_value.{h,cpp}` (new) | font-value grammar, face names, `[glyphs]` value grammar incl. targets | 2 |
| `graphics/hires_text/hires_options.{h,cpp}` (new) | render target / blend / advance / origin enums and parsers, format request, scale limits, ini overrides reader | 3 |
| `graphics/hires_text/font_map.{h,cpp}` | + the version-2 loader `HiResFontMap::loadMap()` into `HiResMap`; old loader deleted in Task 13 | 4, 13 |
| `graphics/hires_text/id_plan.{h,cpp}` (new) | per-id compiled plan: precedence, chains with `same` expanded, range tables, glyph table, missing | 5 |
| `graphics/hires_text/glyph_source_ranged.{h,cpp}` (new) | `pickGlyph()` and `RangeRoutedGlyphSource`: N chains + targets + missing box | 6 |
| `engines/scumm/hires_text.{h,cpp}`, `charset.cpp`, `scumm.cpp`, `metaengine.cpp`, `dialogs.cpp` | SCUMM adapter | 7, 8 |
| `engines/sci/graphics/{hirestextsettings,cache,fontset,fontunicode,text16}.*`, `textlatin.h`, `drivers/{init,default}.cpp` | SCI adapter | 9, 10 |
| `backends/graphics/dos/dos-graphics.cpp`, `backends/platform/dos/{dos.cpp,dos-modes.h}` | DOS backend | 11 |
| `engines/ags/shared/font/hires_font_{config,plan}.{h,cpp}` | AGS adapter | 12 |
| `graphics/hires_text/glyph_source_routed.*`, `latin_advance.*` | deleted (Task 13) if nothing uses them | 13 |
| `dists/engine-data/hires_text/{dos,maps}/*` | maps rewritten | 14 |
| `tools/korean/*`, harness `harness/dos/**` | generators, acceptance scripts, package templates | 15 |
| `graphics/hires_text/README.md`, `engines/scumm/HIRES_TEXT*.md`, `dists/engine-data/hires_text/fonts/FONTS.md` | docs | 16 |

---

### Task 0: Re-sync the shared parser to `i18n`, and record the pre-change baseline

The `i18n` worktree (`/home/thkim/work/scummvm/i18n`, branch `i18n`, head `191d9b05cb2`, an ancestor of `dos-port`) lacks
three parser commits `dos-port` made: `a71922fbc55` (`[hires] missing=` + `MissingGlyphSource`), `624e6e4be7d` (its
tests), and the parser part of `ad5030e777b` (`[latin] baseline=face`). Today `cmp` differs at font_map.h byte 10544,
font_map.cpp byte 3935, test byte 3044. `i18n-win` is a detached checkout of the same commit: leave it alone.

**Files:**
- Modify (in `/home/thkim/work/scummvm/i18n`): `graphics/hires_text/font_map.{h,cpp}`, `graphics/hires_text/glyph_source_missing.{h,cpp}` (new there), `graphics/module.mk`, `test/graphics/hires_text_font_map.h`, `test/graphics/hires_text_missing.h` (new there)
- Create (harness side, not committed): `/home/thkim/work/scummvm/runs/unify-base/`

**Interfaces:**
- Consumes: nothing.
- Produces: `BASE` = the `dos-port` sha at the start of Task 1 (write it into the ledger); the archive
  `/home/thkim/work/scummvm/runs/unify-base/{dos-m1,dos-m2,dos-m5}` used by Task 17.

- [ ] **Step 1: Check the state**

```bash
cd /home/thkim/work/scummvm/i18n && git status --short && git log --oneline -1
cd /home/thkim/work/scummvm/dos && for f in graphics/hires_text/font_map.h graphics/hires_text/font_map.cpp test/graphics/hires_text_font_map.h; do cmp $f ../i18n/$f; done
```
Expected: `i18n` clean at `191d9b05cb2`; three `differ` lines. If `i18n` is dirty or has moved, stop and report.

- [ ] **Step 2: Cherry-pick the two self-contained commits**

```bash
cd /home/thkim/work/scummvm/i18n && git cherry-pick -x a71922fbc55 624e6e4be7d
```
Expected: both apply cleanly (they touch only `graphics/` and `test/graphics/`). On a conflict: `git cherry-pick --abort`
and report.

- [ ] **Step 3: Port the parser hunks of `ad5030e777b` only**

```bash
cd /home/thkim/work/scummvm/i18n
git -C ../dos show ad5030e777b -- graphics/hires_text/font_map.h graphics/hires_text/font_map.cpp test/graphics/hires_text_font_map.h | git apply --index
for f in graphics/hires_text/font_map.h graphics/hires_text/font_map.cpp test/graphics/hires_text_font_map.h; do cmp $f ../dos/$f && echo "$f identical"; done
```
Expected: three `identical` lines. If a file still differs, diff it against `dos` and bring the missing hunk over by hand
(only parser code; no SCUMM engine code goes to `i18n` here).

- [ ] **Step 4: Build and test the graphics tests on `i18n`**

```bash
mkdir -p /home/thkim/work/scummvm/builds/i18n-sync && cd /home/thkim/work/scummvm/builds/i18n-sync && \
  /home/thkim/work/scummvm/i18n/configure --disable-all-engines --with-sdl-prefix=/home/thkim/.local/sysroot/usr >/dev/null && \
  make -j8 test 2>&1 | tail -5
```
Expected: the only failure is `hires_text_ttf_fit.h:493`.

- [ ] **Step 5: Commit on `i18n`**

```bash
cd /home/thkim/work/scummvm/i18n && git commit -m "GRAPHICS: Sync shared hi-res parser with dos-port ad5030e777b

[latin] baseline=face parser hunks; missing= came with the two cherry-picks.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4"
```

- [ ] **Step 6: Record `BASE` and archive the pre-change Linux references**

Run the current acceptance on DOSBox-X once, with the current EXEs and maps, to produce Linux reference dumps from the
pre-change code:
```bash
cd /home/thkim/work/scummvm/dos && git rev-parse --short=11 HEAD   # = BASE, write to the ledger
backends/platform/dos/build-dos.sh sci >/dev/null && backends/platform/dos/build-dos.sh scumm >/dev/null
cd /home/thkim/work/scummvm && python3 harness/dos/m1_accept.py x && python3 harness/dos/m2_accept.py x && python3 harness/dos/m5_accept.py x --fresh
mkdir -p runs/unify-base && cp -a runs/dos-m1 runs/dos-m2 runs/dos-m5 runs/unify-base/
git -C dos log -1 --format=%H -- dists/engine-data/hires_text/dos > runs/unify-base/DISTS_COMMIT
```
Expected: `M1 x: PASS`, `M2 x: PASS`, `M5 x: PASS`. The archive is not committed anywhere (it is data).

---

### Task 1: Unicode ranges (block table, spec parser, compiled table)

**Files:**
- Create: `graphics/hires_text/unicode_ranges.h`, `graphics/hires_text/unicode_ranges.cpp`
- Modify: `graphics/module.mk` (add `hires_text/unicode_ranges.o` in alphabetical order)
- Test: `test/graphics/hires_text_unicode_ranges.h` (new)

**Interfaces:**
- Consumes: `Graphics::isWide(uint32)` (`graphics/hires_text/unicode_props.h`).
- Produces (namespace `Graphics`):
  ```cpp
  struct HiResSpan { uint32 lo; uint32 hi; };                 // inclusive
  enum HiResSpecKind { kHiResSpecSpan = 0, kHiResSpecWide };
  struct HiResRangeSpec { HiResSpecKind kind; HiResSpan span; Common::String spelled; };
  bool lookupUnicodeBlock(const Common::String &name, HiResSpan &out);          // case-insensitive
  bool parseRangeSpec(const Common::String &text, HiResRangeSpec &out, Common::String &error);
  struct HiResRangeScope { Common::Array<HiResRangeSpec> specs; Common::Array<int> values; }; // parallel arrays, values >= 0
  class HiResRangeTable {
  public:
      HiResRangeTable();
      void compile(const Common::Array<HiResRangeScope> &scopes);   // most specific scope first
      int lookup(uint32 cp) const;                                     // -1 = no rule
      bool empty() const;
      uint32 hash() const;                                             // stable over equal input
  };
  ```

- [ ] **Step 1: Write the failing test**

```cpp
// test/graphics/hires_text_unicode_ranges.h
#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/str.h"
#include "graphics/hires_text/unicode_ranges.h"

class HiResUnicodeRangesTestSuite : public CxxTest::TestSuite {
	static Graphics::HiResRangeSpec spec(const char *text) {
		Graphics::HiResRangeSpec s;
		Common::String error;
		TS_ASSERT(Graphics::parseRangeSpec(text, s, error));
		return s;
	}

	static Graphics::HiResRangeScope scope(const char *a, int va, const char *b = nullptr, int vb = 0,
										   const char *c = nullptr, int vc = 0) {
		Graphics::HiResRangeScope s;
		s.specs.push_back(spec(a));
		s.values.push_back(va);
		if (b) {
			s.specs.push_back(spec(b));
			s.values.push_back(vb);
		}
		if (c) {
			s.specs.push_back(spec(c));
			s.values.push_back(vc);
		}
		return s;
	}

public:
	void test_block_names() {
		Graphics::HiResSpan s;
		TS_ASSERT(Graphics::lookupUnicodeBlock("basic-latin", s));
		TS_ASSERT_EQUALS(s.lo, 0x20u);
		TS_ASSERT_EQUALS(s.hi, 0x7Eu);
		TS_ASSERT(Graphics::lookupUnicodeBlock("Hangul-Syllables", s));
		TS_ASSERT_EQUALS(s.lo, 0xAC00u);
		TS_ASSERT_EQUALS(s.hi, 0xD7A3u);
		TS_ASSERT(Graphics::lookupUnicodeBlock("general-punctuation", s));
		TS_ASSERT_EQUALS(s.lo, 0x2000u);
		TS_ASSERT(Graphics::lookupUnicodeBlock("pua", s));
		TS_ASSERT_EQUALS(s.lo, 0xE000u);
		TS_ASSERT_EQUALS(s.hi, 0xF8FFu);
		TS_ASSERT(Graphics::lookupUnicodeBlock("misc-symbols", s));
		TS_ASSERT_EQUALS(s.lo, 0x2600u);
		TS_ASSERT(!Graphics::lookupUnicodeBlock("latin", s));
	}

	void test_explicit_spans() {
		Graphics::HiResRangeSpec s = spec("U+2026");
		TS_ASSERT_EQUALS(s.kind, Graphics::kHiResSpecSpan);
		TS_ASSERT_EQUALS(s.span.lo, 0x2026u);
		TS_ASSERT_EQUALS(s.span.hi, 0x2026u);
		s = spec("u+0020-007e");
		TS_ASSERT_EQUALS(s.span.lo, 0x20u);
		TS_ASSERT_EQUALS(s.span.hi, 0x7Eu);
		s = spec("U+E000-U+E0FF");
		TS_ASSERT_EQUALS(s.span.hi, 0xE0FFu);
		s = spec("wide");
		TS_ASSERT_EQUALS(s.kind, Graphics::kHiResSpecWide);
	}

	void test_bad_specs() {
		Graphics::HiResRangeSpec s;
		Common::String error;
		TS_ASSERT(!Graphics::parseRangeSpec("U+007E-0020", s, error));   // reversed
		TS_ASSERT(!Graphics::parseRangeSpec("U+110000", s, error));      // beyond Unicode
		TS_ASSERT(!Graphics::parseRangeSpec("0x20-0x7e", s, error));     // hex form is for [glyphs] keys only
		TS_ASSERT(!Graphics::parseRangeSpec("latin", s, error));
		TS_ASSERT(!error.empty());
	}

	void test_narrowest_wins_inside_a_scope() {
		Common::Array<Graphics::HiResRangeScope> scopes;
		scopes.push_back(scope("basic-latin", 1, "U+0041-005A", 2, "U+0051", 3));
		Graphics::HiResRangeTable t;
		t.compile(scopes);
		TS_ASSERT_EQUALS(t.lookup('a'), 1);
		TS_ASSERT_EQUALS(t.lookup('B'), 2);
		TS_ASSERT_EQUALS(t.lookup('Q'), 3);
		TS_ASSERT_EQUALS(t.lookup(0x7F), -1);
		TS_ASSERT_EQUALS(t.lookup(0xAC00), -1);
	}

	void test_a_more_specific_scope_beats_any_width() {
		Common::Array<Graphics::HiResRangeScope> scopes;
		scopes.push_back(scope("basic-latin", 7));            // [font.N]
		scopes.push_back(scope("U+0041", 1));                 // [font]
		Graphics::HiResRangeTable t;
		t.compile(scopes);
		TS_ASSERT_EQUALS(t.lookup('A'), 7);
	}

	void test_wide_is_least_specific_in_its_scope() {
		Common::Array<Graphics::HiResRangeScope> scopes;
		scopes.push_back(scope("wide", 4, "U+AC00", 5));
		scopes.push_back(scope("hangul-syllables", 9));
		Graphics::HiResRangeTable t;
		t.compile(scopes);
		TS_ASSERT_EQUALS(t.lookup(0xAC00), 5);   // explicit span in scope 0
		TS_ASSERT_EQUALS(t.lookup(0xAC01), 4);   // wide in scope 0 beats scope 1
		TS_ASSERT_EQUALS(t.lookup('A'), -1);     // not wide, no span
	}

	void test_outside_the_bmp_and_empty() {
		Graphics::HiResRangeTable empty;
		TS_ASSERT(empty.empty());
		TS_ASSERT_EQUALS(empty.lookup('A'), -1);
		Common::Array<Graphics::HiResRangeScope> scopes;
		scopes.push_back(scope("U+1F600-1F64F", 2));
		Graphics::HiResRangeTable t;
		t.compile(scopes);
		TS_ASSERT_EQUALS(t.lookup(0x1F602), 2);
		TS_ASSERT_EQUALS(t.lookup(0x1F650), -1);
	}

	void test_hash_is_stable_and_discriminates() {
		Common::Array<Graphics::HiResRangeScope> a, b;
		a.push_back(scope("basic-latin", 1));
		b.push_back(scope("basic-latin", 2));
		Graphics::HiResRangeTable ta, ta2, tb;
		ta.compile(a);
		ta2.compile(a);
		tb.compile(b);
		TS_ASSERT_EQUALS(ta.hash(), ta2.hash());
		TS_ASSERT_DIFFERS(ta.hash(), tb.hash());
	}
};
```

- [ ] **Step 2: Run to verify it fails**

Run: `make -C /home/thkim/work/scummvm/builds/linux-dos-test-scumm -j8 test 2>&1 | grep -m3 "unicode_ranges"`
Expected: compile error, `graphics/hires_text/unicode_ranges.h: No such file or directory`.

- [ ] **Step 3: Implement**

`unicode_ranges.h` declares exactly the interface above (GPL header as the neighbouring files). `unicode_ranges.cpp`:
- The block table is a `static const struct { const char *name; uint32 lo, hi; }` array with the 20 rows of spec 6.1;
  `lookupUnicodeBlock` compares with `equalsIgnoreCase`.
- `parseRangeSpec`: `wide` (case-insensitive) -> `kHiResSpecWide`; else a block name; else `[uU]+<1-6 hex>` optionally
  followed by `-` and `[uU]+`? `<1-6 hex>`; reject `lo > hi` and anything above 0x10FFFF; `error` gets
  `"unknown range '<text>'"` or `"malformed range '<text>'"`.
- `compile`: collect every boundary (`lo` and `hi + 1` of every span of every scope, plus 0 and 0x110000), sort, unique;
  for each elementary interval `[b_i, b_{i+1})` compute `narrow` = the value of the narrowest span covering it in the first
  scope that has one covering it, and `wide` = the same search where, per scope in order, a covering span answers, else
  that scope's `wide` spec answers; store runs `{lo, hi, narrow, wide}` (merge neighbours with equal values). Page table:
  `uint16 _page[256]` for `cp >> 8` in the BMP: `0xFFFF` = search, else the index of the single run covering the whole
  page. `lookup(cp)`: page hit or binary search -> run -> `isWide(cp) ? run.wide : run.narrow`. `hash()`: FNV-1a over the
  runs. With equal-width ties inside one scope (both spans cover the interval, same width) the first in the scope's order
  wins: the loader (Task 4) has already warned and dropped the later one, so this cannot happen from a map.

- [ ] **Step 4: Run the tests**

Run: `make -C /home/thkim/work/scummvm/builds/linux-dos-test-scumm -j8 test 2>&1 | tail -5`
Expected: only the known `hires_text_ttf_fit.h:493` failure; the new suite's 7 tests pass.

- [ ] **Step 5: Commit**

```bash
cd /home/thkim/work/scummvm/dos
git add graphics/hires_text/unicode_ranges.h graphics/hires_text/unicode_ranges.cpp test/graphics/hires_text_unicode_ranges.h
git commit -m "GRAPHICS: Unicode range specs and a compiled range table for hi-res text

<trailer>" -- graphics/hires_text/unicode_ranges.h graphics/hires_text/unicode_ranges.cpp test/graphics/hires_text_unicode_ranges.h graphics/module.mk
```
(`<trailer>` = the two lines of Global Constraints, here and in every later commit.)

---

### Task 2: Font values and `[glyphs]` values

**Files:**
- Create: `graphics/hires_text/font_value.h`, `graphics/hires_text/font_value.cpp`
- Modify: `graphics/module.mk`
- Test: `test/graphics/hires_text_font_value.h` (new)

**Interfaces:**
- Consumes: `Graphics::HiResFontMap::resolvePath(const Common::String &, const Common::Path &)` (existing, `font_map.h`).
- Produces (namespace `Graphics`):
  ```cpp
  static const uint32 kHiResTargetedCodeBase = 0x110000; // virtual cp of a [glyphs] target: base + index
  static const uint32 kHiResGameCodeBase = 0x120000;     // SCI only: "the game's font draws code", base + code
  enum HiResFaceKind { kHiResFaceFile = 0, kHiResFaceSame, kHiResFaceOriginal };
  struct HiResFaceEntry { HiResFaceKind kind; Common::String written; Common::Path path; };
  struct HiResFontValue {
      Common::Array<HiResFaceEntry> entries;
      bool empty() const; bool hasSame() const; bool endsInOriginal() const;
  };
  typedef Common::HashMap<Common::String, Common::String,
          Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> HiResFaceNames;   // [fonts]: name -> path as written
  bool isValidFaceName(const Common::String &name);
  bool parseCodePointValue(const Common::String &text, uint32 &out);           // u+XXXX, U+XXXX, 0xXXXX
  // names resolve against namesBaseDir (the map folder); literal paths against pathBaseDir.
  // false when no entry survives. Warnings are appended in spec 10.2 wording.
  bool parseFontValue(const Common::String &text, const HiResFaceNames &names,
                      const Common::Path &namesBaseDir, const Common::Path &pathBaseDir,
                      HiResFontValue &out, Common::Array<Common::String> &warnings);
  enum HiResGlyphKind { kHiResGlyphOriginal = 0, kHiResGlyphCodePoint, kHiResGlyphOffset,
                        kHiResGlyphTarget, kHiResGlyphTargetOffset };
  struct HiResGlyphRule { HiResGlyphKind kind; uint32 value; HiResFaceEntry face; };   // value: cp or offset
  bool parseGlyphRule(const Common::String &text, const HiResFaceNames &names, const Common::Path &baseDir,
                      HiResGlyphRule &out, Common::String &error);
  ```

- [ ] **Step 1: Write the failing test**

```cpp
// test/graphics/hires_text_font_value.h
#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/path.h"
#include "common/str.h"
#include "graphics/hires_text/font_value.h"

class HiResFontValueTestSuite : public CxxTest::TestSuite {
	Graphics::HiResFaceNames names() {
		Graphics::HiResFaceNames n;
		n["ko"] = "fonts/KO.SVF";
		n["sym"] = "SYMBOLS.SVF";
		return n;
	}

	bool value(const char *text, Graphics::HiResFontValue &out, Common::Array<Common::String> &w) {
		return Graphics::parseFontValue(text, names(), Common::Path("/maps", '/'), Common::Path("/games/mi2", '/'), out, w);
	}

	bool glyph(const char *text, Graphics::HiResGlyphRule &out) {
		Common::String error;
		return Graphics::parseGlyphRule(text, names(), Common::Path("/maps", '/'), out, error);
	}

public:
	void test_names_paths_and_sentinels() {
		Graphics::HiResFontValue v;
		Common::Array<Common::String> w;
		TS_ASSERT(value("ko, LATIN.TTF#1, same", v, w));
		TS_ASSERT_EQUALS(v.entries.size(), 3u);
		TS_ASSERT_EQUALS(v.entries[0].kind, Graphics::kHiResFaceFile);
		TS_ASSERT_EQUALS(v.entries[0].path.toString('/'), "/maps/fonts/KO.SVF");   // a name: map folder
		TS_ASSERT_EQUALS(v.entries[1].path.toString('/'), "/games/mi2/LATIN.TTF#1"); // a literal: pathBaseDir
		TS_ASSERT_EQUALS(v.entries[2].kind, Graphics::kHiResFaceSame);
		TS_ASSERT(v.hasSame());
		TS_ASSERT(!v.endsInOriginal());
		TS_ASSERT(w.empty());
	}

	void test_original_ends_the_chain() {
		Graphics::HiResFontValue v;
		Common::Array<Common::String> w;
		TS_ASSERT(value("sym, original, ko", v, w));
		TS_ASSERT_EQUALS(v.entries.size(), 2u);
		TS_ASSERT(v.endsInOriginal());
		TS_ASSERT_EQUALS(w.size(), 1u);
		TS_ASSERT_EQUALS(w[0], "hires_text.map: entries after 'original' are ignored: 'ko'");
	}

	void test_unknown_name_dropped() {
		Graphics::HiResFontValue v;
		Common::Array<Common::String> w;
		TS_ASSERT(!value("nosuchface", v, w));
		TS_ASSERT(v.empty());
		TS_ASSERT_EQUALS(w[0], "hires_text.map: unknown face name 'nosuchface'");
	}

	void test_face_name_grammar() {
		TS_ASSERT(Graphics::isValidFaceName("dlg_2-b"));
		TS_ASSERT(!Graphics::isValidFaceName("same"));
		TS_ASSERT(!Graphics::isValidFaceName("Original"));
		TS_ASSERT(!Graphics::isValidFaceName("data"));
		TS_ASSERT(!Graphics::isValidFaceName("a.b"));
	}

	void test_glyph_values() {
		Graphics::HiResGlyphRule r;
		TS_ASSERT(glyph("original", r));
		TS_ASSERT_EQUALS(r.kind, Graphics::kHiResGlyphOriginal);
		TS_ASSERT(glyph("u+2026", r));
		TS_ASSERT_EQUALS(r.kind, Graphics::kHiResGlyphCodePoint);
		TS_ASSERT_EQUALS(r.value, 0x2026u);
		TS_ASSERT(glyph("+0xFEE0", r));
		TS_ASSERT_EQUALS(r.kind, Graphics::kHiResGlyphOffset);
		TS_ASSERT_EQUALS(r.value, 0xFEE0u);
		TS_ASSERT(!glyph("keep", r));   // removed: the one name is 'original'
	}

	void test_targeted_glyphs() {
		Graphics::HiResGlyphRule r;
		TS_ASSERT(glyph("sym:u+2620", r));
		TS_ASSERT_EQUALS(r.kind, Graphics::kHiResGlyphTarget);
		TS_ASSERT_EQUALS(r.value, 0x2620u);
		TS_ASSERT_EQUALS(r.face.path.toString('/'), "/maps/SYMBOLS.SVF");
		TS_ASSERT(glyph("ICONS.SVF:u+e001", r));                      // PUA in a custom SVF
		TS_ASSERT_EQUALS(r.value, 0xE001u);
		TS_ASSERT_EQUALS(r.face.path.toString('/'), "/maps/ICONS.SVF");
		TS_ASSERT(glyph("data:hires_text/x.svf:u+2620", r));          // split at the last colon
		TS_ASSERT_EQUALS(r.face.written, "data:hires_text/x.svf");
		TS_ASSERT(glyph("same:u+2620", r));
		TS_ASSERT_EQUALS(r.face.kind, Graphics::kHiResFaceSame);
		TS_ASSERT(glyph("sym:+0xE000", r));
		TS_ASSERT_EQUALS(r.kind, Graphics::kHiResGlyphTargetOffset);
		TS_ASSERT(!glyph("sym, ko:u+2620", r));    // a chain is refused
		TS_ASSERT(!glyph("original:u+2620", r));   // original is not a face
		TS_ASSERT(!glyph("sym:2620", r));          // the part after the colon must be u+ or +0x
	}
};
```

- [ ] **Step 2: Run to verify it fails**

Run: `make -C /home/thkim/work/scummvm/builds/linux-dos-test-scumm -j8 test 2>&1 | grep -m3 font_value`
Expected: `font_value.h: No such file or directory`.

- [ ] **Step 3: Implement** `font_value.{h,cpp}` with the interface above. Classification order (spec 5.1): `same`,
`original`, a `[fonts]` name (`names.tryGetVal`, resolved with `HiResFontMap::resolvePath(value, namesBaseDir)`), a path
(contains `/`, `\`, `.` or starts with `data:`; resolved with `resolvePath(text, pathBaseDir)`), else the
`unknown face name` warning. `parseGlyphRule` splits at the last `:` only when the tail starts with `u+`/`U+`/`+`; a
tail that fails `parseCodePointValue` / offset parsing gives `error`; the head goes through the single-entry
classification (comma -> error `"a [glyphs] target names one face, not a chain"`, `original` -> error). Warning texts:
exactly those in the test.

- [ ] **Step 4: Run the tests.** Run: `make -C /home/thkim/work/scummvm/builds/linux-dos-test-scumm -j8 test 2>&1 | tail -5`. Expected: only the known `hires_text_ttf_fit.h:493` failure.

- [ ] **Step 5: Commit** `GRAPHICS: Font values and [glyphs] values, including targeted glyphs` with the three files and
`graphics/module.mk`.

---

### Task 3: Render options, scale limits and the ini reader

**Files:**
- Create: `graphics/hires_text/hires_options.h`, `graphics/hires_text/hires_options.cpp`
- Modify: `graphics/module.mk`
- Test: `test/graphics/hires_text_hires_options.h` (new)

**Interfaces:**
- Consumes: `Graphics::PixelFormat`, `ConfMan`.
- Produces (namespace `Graphics`):
  ```cpp
  enum HiResRenderTarget { kHiResTargetAuto = 0, kHiResTargetClut8, kHiResTargetRgb565, kHiResTargetRgb888 };
  enum HiResBlend { kHiResBlendAuto = 0, kHiResBlendOn, kHiResBlendOff };
  enum HiResAdvance { kHiResAdvanceEngine = 0, kHiResAdvanceGame, kHiResAdvanceFont, kHiResAdvanceCell };
  enum HiResOrigin { kHiResOriginGame = 0, kHiResOriginFace };
  bool parseRenderTarget(const Common::String &, HiResRenderTarget &);   // auto clut8 rgb565 rgb888, case-insensitive
  const char *renderTargetName(HiResRenderTarget);
  bool parseBlend(const Common::String &, HiResBlend &);
  bool parseAdvance(const Common::String &, HiResAdvance &);             // game font cell (never "engine")
  bool parseOrigin(const Common::String &, HiResOrigin &);
  bool formatMatchesTarget(const PixelFormat &, HiResRenderTarget);      // clut8: isCLUT8; rgb565: 2 bpp 5-6-5; rgb888: 4 bpp 8-8-8
  // want = the resolved target (auto already resolved by resolveAutoTarget). The list for initGraphics():
  // want=clut8 -> CLUT8 alone. Otherwise the formats of `supported` matching want (none for rgb565 when
  // !engineCanRgb565), then those matching the other RGB target in the order rgb888 > rgb565 (rgb565 only when
  // engineCanRgb565), then CLUT8 last. `note` gets the spec 7.1 warning when no format matching want is offered:
  // "render_target=<want> is not available here; using <what the list starts with>".
  Common::List<PixelFormat> formatRequest(HiResRenderTarget want, const Common::List<PixelFormat> &supported,
                                           bool engineCanRgb565, Common::String &note);
  HiResRenderTarget resolveAutoTarget(bool anyCoverage, HiResBlend blend);   // clut8 or rgb888
  bool blendActive(HiResBlend blend, bool faceHasCoverage, bool screenIsClut8);  // spec 7.2; on+clut8 -> false for now
  struct HiResScaleLimits { int min; int max; };
  HiResScaleLimits hiResScaleLimits();       // ConfMan "hires_text_platform_scale" registered -> {n, n}; else {1, 3}
  // engine range + platform limits; out-of-range -> warning and the clamped value (spec 7.4 texts)
  int clampScale(int requested, int engineMin, int engineMax, const HiResScaleLimits &platform,
                 const char *engineName, Common::String &warning);
  struct HiResIniOverrides {
      HiResIniOverrides();
      bool enabled;                         // hires_text (default true)
      bool mapSet; Common::String map;      // hires_text_map (empty value allowed: "no map")
      bool faceSet; Common::String face;    // hires_text_face, unparsed (names need the map)
      bool sizeSet; int size;               // hires_text_size 8..64
      bool scaleSet; int scale;             // hires_text_scale 1..3
      bool blendSet; HiResBlend blend;      // hires_text_blend
      bool advanceSet; HiResAdvance advance;// hires_text_advance
      bool targetSet; HiResRenderTarget target; // render_target
      bool log;                             // hires_text_log
  };
  typedef bool (*HiResIniGetFn)(const char *key, bool globalFallback, Common::String &value, void *ctx);
  HiResIniOverrides readHiResIni(HiResIniGetFn get, void *ctx, Common::Array<Common::String> &warnings);
  HiResIniOverrides readHiResIniFromConfMan(const Common::String &gameDomain, Common::Array<Common::String> &warnings);
  ```
  `readHiResIni` asks for `render_target` and `hires_text` with `globalFallback=true`, every other key with `false`
  (spec 11). `readHiResIniFromConfMan` implements the callback with `ConfMan.hasKey(key, gameDomain)` then, for global
  fallback, `ConfMan.hasKey(key, Common::ConfigManager::kApplicationDomain)`.

- [ ] **Step 1: Write the failing test**

```cpp
// test/graphics/hires_text_hires_options.h
#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/list.h"
#include "common/str.h"
#include "graphics/hires_text/hires_options.h"
#include "graphics/pixelformat.h"

class HiResOptionsTestSuite : public CxxTest::TestSuite {
	struct FakeIni {
		Common::HashMap<Common::String, Common::String> game, global;
	};

	static bool get(const char *key, bool globalFallback, Common::String &value, void *ctx) {
		FakeIni *ini = (FakeIni *)ctx;
		if (ini->game.tryGetVal(key, value))
			return true;
		return globalFallback && ini->global.tryGetVal(key, value);
	}

	static Graphics::PixelFormat rgb565() { return Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0); }
	static Graphics::PixelFormat xrgb1555() { return Graphics::PixelFormat(2, 5, 5, 5, 0, 10, 5, 0, 0); }
	static Graphics::PixelFormat xrgb8888() { return Graphics::PixelFormat(4, 8, 8, 8, 0, 16, 8, 0, 0); }

public:
	void test_parsers() {
		Graphics::HiResRenderTarget t;
		TS_ASSERT(Graphics::parseRenderTarget("RGB565", t));
		TS_ASSERT_EQUALS(t, Graphics::kHiResTargetRgb565);
		TS_ASSERT(!Graphics::parseRenderTarget("argb8888", t));
		TS_ASSERT(!Graphics::parseRenderTarget("truecolor", t));
		Graphics::HiResBlend b;
		TS_ASSERT(Graphics::parseBlend("off", b));
		TS_ASSERT(!Graphics::parseBlend("true", b));
		Graphics::HiResAdvance a;
		TS_ASSERT(Graphics::parseAdvance("cell", a));
		TS_ASSERT(!Graphics::parseAdvance("engine", a));
		TS_ASSERT(!Graphics::parseAdvance("ttf", a));
	}

	void test_format_matching() {
		TS_ASSERT(Graphics::formatMatchesTarget(rgb565(), Graphics::kHiResTargetRgb565));
		TS_ASSERT(!Graphics::formatMatchesTarget(xrgb1555(), Graphics::kHiResTargetRgb565));
		TS_ASSERT(Graphics::formatMatchesTarget(xrgb8888(), Graphics::kHiResTargetRgb888));
		TS_ASSERT(Graphics::formatMatchesTarget(Graphics::PixelFormat::createFormatCLUT8(), Graphics::kHiResTargetClut8));
	}

	void test_format_request_order_and_fallback() {
		Common::List<Graphics::PixelFormat> dosboxStaging;   // no rgb565 at the size
		dosboxStaging.push_back(xrgb8888());
		dosboxStaging.push_back(Graphics::PixelFormat::createFormatCLUT8());
		Common::String note;
		Common::List<Graphics::PixelFormat> got =
			Graphics::formatRequest(Graphics::kHiResTargetRgb565, dosboxStaging, true, note);
		TS_ASSERT_EQUALS(got.size(), 2u);
		TS_ASSERT(got.front() == xrgb8888());
		TS_ASSERT(got.back().isCLUT8());
		TS_ASSERT_EQUALS(note, "render_target=rgb565 is not available here; using rgb888");

		Common::List<Graphics::PixelFormat> all;
		all.push_back(rgb565());
		all.push_back(xrgb1555());
		all.push_back(xrgb8888());
		all.push_back(Graphics::PixelFormat::createFormatCLUT8());
		note.clear();
		got = Graphics::formatRequest(Graphics::kHiResTargetRgb565, all, false, note);   // engine cannot draw 565
		TS_ASSERT(got.front() == xrgb8888());
		TS_ASSERT(!note.empty());
		note.clear();
		got = Graphics::formatRequest(Graphics::kHiResTargetClut8, all, true, note);
		TS_ASSERT_EQUALS(got.size(), 1u);
		TS_ASSERT(got.front().isCLUT8());
		TS_ASSERT(note.empty());
	}

	void test_auto_target_and_blend() {
		TS_ASSERT_EQUALS(Graphics::resolveAutoTarget(false, Graphics::kHiResBlendAuto), Graphics::kHiResTargetClut8);
		TS_ASSERT_EQUALS(Graphics::resolveAutoTarget(true, Graphics::kHiResBlendAuto), Graphics::kHiResTargetRgb888);
		TS_ASSERT_EQUALS(Graphics::resolveAutoTarget(true, Graphics::kHiResBlendOff), Graphics::kHiResTargetClut8);
		TS_ASSERT_EQUALS(Graphics::resolveAutoTarget(false, Graphics::kHiResBlendOn), Graphics::kHiResTargetRgb888);
		TS_ASSERT(Graphics::blendActive(Graphics::kHiResBlendAuto, true, false));
		TS_ASSERT(!Graphics::blendActive(Graphics::kHiResBlendAuto, false, false));
		TS_ASSERT(!Graphics::blendActive(Graphics::kHiResBlendAuto, true, true));
		TS_ASSERT(!Graphics::blendActive(Graphics::kHiResBlendOff, true, false));
		TS_ASSERT(!Graphics::blendActive(Graphics::kHiResBlendOn, true, true));   // until palette-matched AA (Task 20)
	}

	void test_scale_limits() {
		Graphics::HiResScaleLimits desktop = { 1, 3 };
		Graphics::HiResScaleLimits dos = { 2, 2 };
		Common::String w;
		TS_ASSERT_EQUALS(Graphics::clampScale(3, 1, 3, desktop, "SCUMM", w), 3);
		TS_ASSERT(w.empty());
		TS_ASSERT_EQUALS(Graphics::clampScale(3, 1, 3, dos, "SCUMM", w), 2);
		TS_ASSERT_EQUALS(w, "the DOS backend runs hi-res text at 2x only; using 2");
		w.clear();
		TS_ASSERT_EQUALS(Graphics::clampScale(1, 2, 2, desktop, "SCI", w), 2);
		TS_ASSERT_EQUALS(w, "SCI draws hi-res text at 2x only; using 2");
	}

	void test_ini_domains_and_validation() {
		FakeIni ini;
		ini.global["render_target"] = "clut8";
		ini.global["hires_text_blend"] = "off";        // not read from [scummvm]
		ini.game["hires_text_scale"] = "two";
		ini.game["hires_text_face"] = "ko, original";
		ini.game["hires_text_size"] = "18";
		Common::Array<Common::String> w;
		Graphics::HiResIniOverrides o = Graphics::readHiResIni(get, &ini, w);
		TS_ASSERT(o.enabled);
		TS_ASSERT(o.targetSet);
		TS_ASSERT_EQUALS(o.target, Graphics::kHiResTargetClut8);
		TS_ASSERT(!o.blendSet);
		TS_ASSERT(!o.scaleSet);
		TS_ASSERT_EQUALS(w.size(), 1u);
		TS_ASSERT_EQUALS(w[0], "hires_text_scale 'two' is not 1, 2 or 3; ignoring it");
		TS_ASSERT(o.faceSet);
		TS_ASSERT_EQUALS(o.face, "ko, original");
		TS_ASSERT_EQUALS(o.size, 18);

		FakeIni old;
		old.game["hires_text_font"] = "x.ttf";      // removed keys are simply not read
		old.game["hires_text_alpha"] = "true";
		old.game["dos_truecolor"] = "off";
		w.clear();
		o = Graphics::readHiResIni(get, &old, w);
		TS_ASSERT(!o.faceSet && !o.blendSet && !o.targetSet);
		TS_ASSERT(w.empty());
	}
};
```

- [ ] **Step 2: Run to verify it fails.** Run: `make -C /home/thkim/work/scummvm/builds/linux-dos-test-scumm -j8 test 2>&1 | grep -m5 'error'`. Expected: `hires_options.h: No such file or directory`.
- [ ] **Step 3: Implement.** `hiResScaleLimits()` reads `ConfMan.get("hires_text_platform_scale")` (empty -> {1,3};
  a digit n -> {n,n}). `clampScale` wording: platform limit first (`the DOS backend runs hi-res text at 2x only; using 2`
  when platform is {2,2}; generic `the backend runs hi-res text at <n>x only; using <n>` otherwise), then the engine
  limit (`<engine> draws hi-res text at 2x only; using 2` for min==max, else `hires text scale <n> is out of range
  <min>..<max>; using <default>` where default is `engineMin`). Ini validation messages follow the pattern of the test
  (`<key> '<value>' is not <allowed>; ignoring it`); `hires_text_size` range 8..64.
- [ ] **Step 4: Run the tests.** Run: `make -C /home/thkim/work/scummvm/builds/linux-dos-test-scumm -j8 test 2>&1 | tail -5`. Expected: only the known `hires_text_ttf_fit.h:493` failure.
- [ ] **Step 5: Commit** `GRAPHICS: Hi-res text render target, blend, scale limits and ini overrides` (4 paths).

---

### Task 4: The version-2 map loader

**Files:**
- Modify: `graphics/hires_text/font_map.h`, `graphics/hires_text/font_map.cpp` (additions only; the old loader stays until Task 13)
- Test: `test/graphics/hires_text_font_map.h` (append a second suite class `HiResMapTestSuite`; the old suite stays until Task 13)

**Interfaces:**
- Consumes: Task 1 (`HiResRangeSpec`, `parseRangeSpec`), Task 2 (`HiResFontValue`, `HiResFaceNames`, `HiResGlyphRule`,
  `parseFontValue`, `parseGlyphRule`, `parseCodePointValue`, `isValidFaceName`), Task 3 (enums and parsers), existing
  `HiResCellMode`, `HiResAlign`, `HiResMirror`, `HiResLayoutSettings`, `HiResShadowMode`, `HiResOutlineShape`.
- Produces (namespace `Graphics`, in `font_map.h`):
  ```cpp
  typedef Common::HashMap<uint32, HiResGlyphRule> HiResGlyphTable;   // game code -> rule (ranges expanded)
  struct HiResFontScope {                    // one of [font], [font.N], each with its :q merged in
      HiResFontScope();
      HiResFontValue face;        bool faceSet;     Common::String faceText;   // text kept for re-resolution
      int size;                   bool sizeSet;
      int pixel;                  bool pixelSet;
      int shift;                  bool shiftSet;
      HiResCellMode cell;         bool cellSet;
      HiResAlign align;           bool alignSet;
      uint32 missing;             bool missingSet;  // missing=off: 0 with missingSet
      HiResAdvance advance;       bool advanceSet;
      HiResOrigin origin;         bool originSet;
      HiResMirror mirror;         bool mirrorSet;
      Common::Array<HiResRangeSpec> rangeSpecs;   Common::Array<HiResFontValue> rangeValues;
      Common::Array<HiResRangeSpec> advanceSpecs; Common::Array<HiResAdvance> advanceValues;
      Common::Array<HiResRangeSpec> originSpecs;  Common::Array<HiResOrigin> originValues;
  };
  enum HiResKeyFlag {
      kHiResKeyTarget = 1 << 0, kHiResKeyBlend = 1 << 1, kHiResKeyScale = 1 << 2, kHiResKeyGamma = 1 << 3,
      kHiResKeyTextEncoding = 1 << 4, kHiResKeyLayout = 1 << 5, kHiResKeyShift = 1 << 6, kHiResKeyCell = 1 << 7,
      kHiResKeyAlign = 1 << 8, kHiResKeyMissing = 1 << 9, kHiResKeyAdvance = 1 << 10, kHiResKeyOrigin = 1 << 11,
      kHiResKeyRange = 1 << 12, kHiResKeyMirror = 1 << 13, kHiResKeyGlyphs = 1 << 14, kHiResKeyShadow = 1 << 15
  };
  struct HiResEngineKeys { const char *engine; uint32 honoured; };
  extern const HiResEngineKeys kHiResKeysSci, kHiResKeysScumm, kHiResKeysAgs;   // spec 3.2 "Read by" columns
  struct HiResMap {
      HiResMap();
      void clear();
      int version;
      HiResRenderTarget target;   bool targetSet;
      HiResBlend blend;           bool blendSet;
      int scale;                  bool scaleSet;
      int coverageGamma;          // hundredths, 100 = off
      Common::CodePage encoding;  bool encodingSet;
      HiResLayoutSettings layout;
      HiResFaceNames faces;       // [fonts], qualified entries merged
      HiResFontScope font;        // [font]
      Common::HashMap<int, HiResFontScope> fontIds;   // [font.N]
      HiResGlyphTable glyphs;     // [glyphs]
      Common::HashMap<int, HiResGlyphTable> glyphIds; // [glyphs.N]
      // [shadow]: the same fields and meanings as HiResTextConfig's decoration block
      HiResShadowMode shadowMode; int shadowOffset; byte shadowColor; bool shadowColorSet; int shadowWidthQ;
      HiResOutlineShape shadowStyle; bool shadowShiftSet; int shadowDx; int shadowDy; byte shadowShiftColor;
      bool shadowShiftColorSet; byte shadowAlpha;
      Common::Array<Common::String> warnings;
      const HiResFontScope *fontIdScope(int id) const;
  };
  // in class HiResFontMap:
  static bool loadMap(Common::SeekableReadStream &stream, const Common::Path &mapDir,
                      const Common::Array<Common::String> &qualifiers, const HiResEngineKeys &engine, HiResMap &out);
  static bool loadMapFile(const Common::Path &mapPath, const Common::Array<Common::String> &qualifiers,
                          const HiResEngineKeys &engine, HiResMap &out);
  ```
  `loadMap` starts with `out.clear()`. It returns false (and leaves `out` cleared except `warnings`) for spec 10.1; otherwise true with every problem of
  10.2/10.3 in `out.warnings` and printed with `warning()`.

- [ ] **Step 1: Write the failing tests** (append to `test/graphics/hires_text_font_map.h`, same includes)

```cpp
class HiResMapTestSuite : public CxxTest::TestSuite {
	bool load(const char *text, Graphics::HiResMap &out, const Graphics::HiResEngineKeys &keys = Graphics::kHiResKeysScumm,
			  const char *q0 = nullptr) {
		Common::Array<Common::String> qualifiers;
		if (q0)
			qualifiers.push_back(q0);
		Common::MemoryReadStream stream((const byte *)text, strlen(text));
		return Graphics::HiResFontMap::loadMap(stream, Common::Path("/maps", '/'), qualifiers, keys, out);
	}

	static bool hasWarning(const Graphics::HiResMap &m, const char *text) {
		for (uint i = 0; i < m.warnings.size(); ++i)
			if (m.warnings[i] == text)
				return true;
		return false;
	}

public:
	void test_version_is_required() {
		Graphics::HiResMap m;
		TS_ASSERT(!load("[render]\nscale=2\n", m));
		TS_ASSERT(!load("[map]\nversion=1\n", m));
		TS_ASSERT(!load("[hires]\nscale=2\n[latin]\nmode=off\n", m));
		TS_ASSERT_EQUALS(m.warnings.back(),
			"hires_text.map: not a version 2 map (found [hires]); regenerate it (graphics/hires_text/README.md)");
		TS_ASSERT(load("[map]\nversion=2\n", m));
	}

	void test_render_section() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n[render]\ntarget=rgb565\nblend=off\nscale=3\ngamma=2.2\n", m));
		TS_ASSERT(m.targetSet);
		TS_ASSERT_EQUALS(m.target, Graphics::kHiResTargetRgb565);
		TS_ASSERT_EQUALS(m.blend, Graphics::kHiResBlendOff);
		TS_ASSERT_EQUALS(m.scale, 3);
		TS_ASSERT_EQUALS(m.coverageGamma, 220);
	}

	void test_qualified_render_wins() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n[render]\ntarget=clut8\n[render:monkey2]\ntarget=rgb888\n", m,
					   Graphics::kHiResKeysScumm, "monkey2"));
		TS_ASSERT_EQUALS(m.target, Graphics::kHiResTargetRgb888);
	}

	void test_font_scopes_and_ranges() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n[fonts]\nko=KO.SVF\nlat=LAT.SVF\n"
					   "[font]\nface=ko\nmissing=u+25a1\nrange.basic-latin=lat, same\nadvance.basic-latin=font\n"
					   "origin.U+2026=face\n"
					   "[font.4]\nface=CARD.SVF\nrange.U+0020-007E=original\nmissing=off\n", m));
		TS_ASSERT(m.font.faceSet);
		TS_ASSERT_EQUALS(m.font.face.entries[0].path.toString('/'), "/maps/KO.SVF");
		TS_ASSERT_EQUALS(m.font.missing, 0x25A1u);
		TS_ASSERT_EQUALS(m.font.rangeSpecs.size(), 1u);
		TS_ASSERT_EQUALS(m.font.rangeValues[0].entries.size(), 2u);
		TS_ASSERT_EQUALS(m.font.advanceValues[0], Graphics::kHiResAdvanceFont);
		TS_ASSERT_EQUALS(m.font.originSpecs[0].span.lo, 0x2026u);
		const Graphics::HiResFontScope *cs4 = m.fontIdScope(4);
		TS_ASSERT(cs4);
		TS_ASSERT(cs4->missingSet);
		TS_ASSERT_EQUALS(cs4->missing, 0u);
		TS_ASSERT(cs4->rangeValues[0].endsInOriginal());
	}

	void test_duplicate_and_overlapping_spans() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n[font]\nrange.basic-latin=A.SVF\nrange.U+0020-007E=B.SVF\n"
					   "range.U+0100-010F=C.SVF\nrange.U+0108-0117=D.SVF\n", m));
		TS_ASSERT_EQUALS(m.font.rangeSpecs.size(), 2u);   // the later spelling and the equal-width overlap dropped
		TS_ASSERT(hasWarning(m, "hires_text.map: [font] range.U+0020-007E repeats range.basic-latin; ignoring it"));
		TS_ASSERT(hasWarning(m, "hires_text.map: [font] range.U+0108-0117 overlaps range.U+0100-010F at the same width; ignoring it"));
	}

	void test_qualified_font_section_merges_span_by_span() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n[font.2]\nrange.basic-latin=A.SVF\nrange.U+2026=B.SVF\n"
					   "[font.2:pc]\nrange.basic-latin=C.SVF\n", m, Graphics::kHiResKeysSci, "pc"));
		const Graphics::HiResFontScope *s = m.fontIdScope(2);
		TS_ASSERT_EQUALS(s->rangeSpecs.size(), 2u);
		bool sawC = false, sawB = false;
		for (uint i = 0; i < s->rangeValues.size(); ++i) {
			sawC |= s->rangeValues[i].entries[0].written == "C.SVF";
			sawB |= s->rangeValues[i].entries[0].written == "B.SVF";
		}
		TS_ASSERT(sawC && sawB);
	}

	void test_glyphs_sections() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n[fonts]\nsym=SYM.SVF\n"
					   "[glyphs]\n0x07=original\n0x5e=u+2026\n0x21-0x23=+0xFEE0\n"
					   "[glyphs.2]\n0x07=sym:u+2620\n", m));
		TS_ASSERT_EQUALS(m.glyphs[0x07].kind, Graphics::kHiResGlyphOriginal);
		TS_ASSERT_EQUALS(m.glyphs[0x5e].value, 0x2026u);
		TS_ASSERT_EQUALS(m.glyphs[0x22].kind, Graphics::kHiResGlyphOffset);
		TS_ASSERT(m.glyphIds.contains(2));
		TS_ASSERT_EQUALS(m.glyphIds[2][0x07].kind, Graphics::kHiResGlyphTarget);
		TS_ASSERT_EQUALS(m.glyphIds[2][0x07].face.path.toString('/'), "/maps/SYM.SVF");
	}

	void test_removed_and_unknown_keys_warn() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n[latin]\nmode=off\n[font.4]\nbitmap=X.SVF\nfase=Y.SVF\n", m));
		TS_ASSERT(hasWarning(m, "hires_text.map: [latin] is not read any more; see \"Ranges\" in graphics/hires_text/README.md"));
		TS_ASSERT(hasWarning(m, "hires_text.map: unknown key [font.4] bitmap"));
		TS_ASSERT(hasWarning(m, "hires_text.map: unknown key [font.4] fase"));
		TS_ASSERT(!m.fontIdScope(4) || !m.fontIdScope(4)->faceSet);
	}

	void test_unhonoured_keys_warn_per_engine() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n[font.4]\nmirror=horizontal\n[shadow]\nmode=outline\n", m, Graphics::kHiResKeysSci));
		TS_ASSERT(hasWarning(m, "SCI does not use [font.4] mirror"));
		TS_ASSERT(hasWarning(m, "SCI does not use [shadow] mode"));
		Graphics::HiResMap s;
		TS_ASSERT(load("[map]\nversion=2\n[font]\nshift=2\ncell=glyph\n", s, Graphics::kHiResKeysScumm));
		TS_ASSERT(hasWarning(s, "SCUMM does not use [font] shift"));
		TS_ASSERT(hasWarning(s, "SCUMM does not use [font] cell"));
	}

	void test_invalid_values_are_ignored_not_substituted() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n[render]\ntarget=truecolor\nscale=9\n[font]\nadvance=ttf\n[shadow]\nmode=glow\n", m));
		TS_ASSERT(!m.targetSet);
		TS_ASSERT(!m.scaleSet);
		TS_ASSERT(!m.font.advanceSet);
		TS_ASSERT_EQUALS(m.shadowMode, Graphics::kHiResShadowGame);
		TS_ASSERT_EQUALS(m.warnings.size(), 4u);
	}

	void test_plus_is_legal_in_key_names() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n[font]\nrange.U+2026=A.SVF\n", m));
		TS_ASSERT_EQUALS(m.font.rangeSpecs.size(), 1u);
	}
};
```

- [ ] **Step 2: Run to verify it fails.** Run: `make -C /home/thkim/work/scummvm/builds/linux-dos-test-scumm -j8 test 2>&1 | grep -m5 'error'`. Expected: `HiResMap` / `loadMap` not declared.
- [ ] **Step 3: Implement** in `font_map.cpp`, reusing the existing comment stripping, `getKey()` qualifier lookup,
  `readLayoutSection`, the `[shadow]` parsers and `parseCodeValue`/`parseGlyphRange` helpers (shared with the old
  loader until Task 13):
  - `Common::INIFile ini; ini.requireKeyValueDelimiter(); ini.allowNonEnglishCharacters();`
  - Version gate first (spec 10.1), naming the first of `hires`, `latin`, `bitmap`, `encoding` found.
  - A static table of known sections and, per section, known keys + the `HiResKeyFlag` each needs; iterate every section
    and key of the file once: unknown section/key -> warning; removed section (`hires`, `latin`, `bitmap`, `encoding`,
    `sizes`, `translation`) -> the "not read any more" warning naming its replacement; a known key whose flag is missing
    from `engine.honoured` -> `"<engine> does not use [<section>] <key>"` (still parsed).
  - Section names `font.N`, `glyphs.N` with optional `:q`; `N` decimal 0..65535. Keys with a spec suffix: split at the
    first `.` (`range`, `advance`, `origin`).
  - Merge order per scope: bare section first, then each qualifier from least to most specific, later replacing earlier
    key by key; for spec keys, replacement is by span (`kHiResSpecWide` is one span of its own).
  - Duplicate spelling and equal-width overlap checks within one physical section (warning texts as in the test).
  - `[glyphs]` range keys expand into individual codes (at most 0x10000 codes per key, else a warning and the key is
    ignored); the value is parsed once per key.
- [ ] **Step 4: Run the tests.** Run: `make -C /home/thkim/work/scummvm/builds/linux-dos-test-scumm -j8 test 2>&1 | tail -5`. Expected: only the known `hires_text_ttf_fit.h:493` failure (the old suite still green).
- [ ] **Step 5: Commit** `GRAPHICS: Version 2 hi-res text map loader` (`font_map.h`, `font_map.cpp`, test).

---

### Task 5: The per-id compiled plan

**Files:**
- Create: `graphics/hires_text/id_plan.h`, `graphics/hires_text/id_plan.cpp`
- Modify: `graphics/module.mk`
- Test: `test/graphics/hires_text_id_plan.h` (new)

**Interfaces:**
- Consumes: Tasks 1-4.
- Produces (namespace `Graphics`):
  ```cpp
  struct HiResFaceChain { Common::Array<HiResFaceEntry> faces; bool endsInOriginal; };  // never contains kHiResFaceSame
  struct HiResGlyphTarget { HiResFaceEntry face; uint32 cp; };   // face.kind: File or Same (= the id chain)
  enum HiResGlyphStep { kHiResGlyphStepGame = 0, kHiResGlyphStepDraw };
  struct HiResIdPlan {
      HiResIdPlan();
      bool original;                       // spec 6.5 step 3
      HiResFaceChain idChain;
      Common::Array<HiResFaceChain> ruleChains;   // index = faceRules value
      HiResRangeTable faceRules, advanceRules, originRules;
      Common::Array<HiResAdvance> advanceValues;  // index = advanceRules value
      Common::Array<HiResOrigin> originValues;    // index = originRules value
      HiResAdvance advance;                // id-wide, kHiResAdvanceEngine = none
      HiResAdvance forcedAdvance;          // ini hires_text_advance, kHiResAdvanceEngine = none
      HiResOrigin origin;
      uint32 missing;                      // 0 = off
      int size; bool sizeSet; int pixel; int shift;
      HiResCellMode cell; HiResAlign align; HiResMirror mirror; bool mirrorSet;
      HiResGlyphTable glyphs;              // [glyphs.N] over [glyphs]
      Common::Array<HiResGlyphTarget> targets;
      const HiResFaceChain *chainFor(uint32 cp) const;   // nullptr: the game's font draws it
      HiResAdvance advanceFor(uint32 cp) const;          // forced > range > id-wide > kHiResAdvanceEngine
      HiResOrigin originFor(uint32 cp) const;
      // [glyphs] step: Game -> the game font draws `code`; Draw -> cp = real cp, or kHiResTargetedCodeBase + index
      HiResGlyphStep glyphFor(uint32 code, uint32 decoded, uint32 &cp) const;
      const HiResGlyphTarget *target(uint32 cp) const;   // for cp >= kHiResTargetedCodeBase, else nullptr
      uint32 hash() const;
  };
  // engineScope: the engine's defaults (spec 8) as a scope below [font]; mapLoaded=false ignores `map`.
  HiResIdPlan compileIdPlan(const HiResMap &map, bool mapLoaded, int id, const HiResIniOverrides &ini,
                            const HiResFontScope &engineScope, const Common::Path &mapDir,
                            const Common::Path &gameDir, Common::Array<Common::String> &warnings);
  ```

- [ ] **Step 1: Write the failing test**

```cpp
// test/graphics/hires_text_id_plan.h
#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/memstream.h"
#include "common/str.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/hires_options.h"
#include "graphics/hires_text/id_plan.h"

class HiResIdPlanTestSuite : public CxxTest::TestSuite {
	Graphics::HiResMap _map;
	Graphics::HiResFontScope _engine;   // SCUMM-like: basic-latin = same, advance game

	void load(const char *text) {
		const Common::String full = Common::String("[map]\nversion=2\n") + text;
		Common::MemoryReadStream s((const byte *)full.c_str(), full.size());
		Common::Array<Common::String> q;
		TS_ASSERT(Graphics::HiResFontMap::loadMap(s, Common::Path("/maps", '/'), q, Graphics::kHiResKeysScumm, _map));
	}

	Graphics::HiResIdPlan plan(int id, const Graphics::HiResIniOverrides &ini = Graphics::HiResIniOverrides()) {
		Common::Array<Common::String> w;
		return Graphics::compileIdPlan(_map, true, id, ini, _engine, Common::Path("/maps", '/'),
									   Common::Path("/games/g", '/'), w);
	}

	static Common::String first(const Graphics::HiResFaceChain *c) {
		return (c && !c->faces.empty()) ? c->faces[0].path.toString('/') : Common::String("<game>");
	}

public:
	void setUp() {
		_map.clear();
		_engine = Graphics::HiResFontScope();
		Graphics::HiResRangeSpec latin;
		Common::String error;
		Graphics::parseRangeSpec("basic-latin", latin, error);
		Graphics::HiResFontValue same;
		Graphics::HiResFaceEntry e;
		e.kind = Graphics::kHiResFaceSame;
		same.entries.push_back(e);
		_engine.rangeSpecs.push_back(latin);
		_engine.rangeValues.push_back(same);
		_engine.advanceSpecs.push_back(latin);
		_engine.advanceValues.push_back(Graphics::kHiResAdvanceGame);
	}

	void test_id_chain_precedence() {
		load("[font]\nface=A.SVF\n[font.4]\nface=B.SVF\n");
		TS_ASSERT_EQUALS(first(plan(4).chainFor(0xAC00)), "/maps/B.SVF");
		TS_ASSERT_EQUALS(first(plan(1).chainFor(0xAC00)), "/maps/A.SVF");
		Graphics::HiResIniOverrides ini;
		ini.faceSet = true;
		ini.face = "C.TTF";
		TS_ASSERT_EQUALS(first(plan(4, ini).chainFor(0xAC00)), "/games/g/C.TTF");   // ini paths: game folder
	}

	void test_range_rule_then_id_chain_appended() {
		load("[fonts]\nlat=LAT.SVF\n[font]\nface=KO.SVF\nrange.basic-latin=lat\n");
		const Graphics::HiResIdPlan p = plan(0);
		const Graphics::HiResFaceChain *c = p.chainFor('A');
		TS_ASSERT_EQUALS(c->faces.size(), 2u);
		TS_ASSERT_EQUALS(c->faces[0].path.toString('/'), "/maps/LAT.SVF");
		TS_ASSERT_EQUALS(c->faces[1].path.toString('/'), "/maps/KO.SVF");
		TS_ASSERT_EQUALS(first(p.chainFor(0xAC00)), "/maps/KO.SVF");
	}

	void test_same_expands_in_place_and_original_stops() {
		load("[fonts]\ndisp=D.TTF\n[font]\nface=KO.SVF\n[font.4]\nrange.general-punctuation=disp, same\n"
			 "range.U+2020=disp, original\n");
		const Graphics::HiResIdPlan p = plan(4);
		const Graphics::HiResFaceChain *c = p.chainFor(0x2026);
		TS_ASSERT_EQUALS(c->faces.size(), 2u);
		TS_ASSERT(!c->endsInOriginal);
		const Graphics::HiResFaceChain *d = p.chainFor(0x2020);
		TS_ASSERT_EQUALS(d->faces.size(), 1u);
		TS_ASSERT(d->endsInOriginal);
	}

	void test_engine_scope_is_below_font() {
		load("[font]\nface=KO.SVF\n");
		TS_ASSERT_EQUALS(first(plan(0).chainFor('A')), "/maps/KO.SVF");            // engine: basic-latin = same
		TS_ASSERT_EQUALS(plan(0).advanceFor('A'), Graphics::kHiResAdvanceGame);    // engine default
		TS_ASSERT_EQUALS(plan(0).advanceFor(0xAC00), Graphics::kHiResAdvanceEngine);
		load("[font]\nface=KO.SVF\nrange.basic-latin=original\nadvance.basic-latin=font\n");
		TS_ASSERT_EQUALS(plan(0).chainFor('A'), (const Graphics::HiResFaceChain *)nullptr);
		TS_ASSERT_EQUALS(plan(0).advanceFor('A'), Graphics::kHiResAdvanceFont);
	}

	void test_face_original_turns_the_id_off_except_its_own_ranges() {
		load("[font]\nface=KO.SVF\nrange.U+2026=E.SVF\n[font.2]\nface=original\nrange.U+0021=X.SVF\n");
		const Graphics::HiResIdPlan p = plan(2);
		TS_ASSERT(p.original);
		TS_ASSERT_EQUALS(p.chainFor(0xAC00), (const Graphics::HiResFaceChain *)nullptr);
		TS_ASSERT_EQUALS(p.chainFor(0x2026), (const Graphics::HiResFaceChain *)nullptr);   // [font]'s rule not applied
		TS_ASSERT_EQUALS(first(p.chainFor(0x21)), "/maps/X.SVF");
		Graphics::HiResIniOverrides ini;
		ini.faceSet = true;
		ini.face = "original";
		TS_ASSERT_EQUALS(plan(2, ini).chainFor(0x21), (const Graphics::HiResFaceChain *)nullptr);
	}

	void test_ini_advance_beats_every_key() {
		load("[font]\nface=KO.SVF\nadvance=font\nadvance.basic-latin=cell\n");
		Graphics::HiResIniOverrides ini;
		ini.advanceSet = true;
		ini.advance = Graphics::kHiResAdvanceGame;
		TS_ASSERT_EQUALS(plan(0, ini).advanceFor('A'), Graphics::kHiResAdvanceGame);
		TS_ASSERT_EQUALS(plan(0, ini).advanceFor(0xAC00), Graphics::kHiResAdvanceGame);
		TS_ASSERT_EQUALS(plan(0).advanceFor(0xAC00), Graphics::kHiResAdvanceFont);
	}

	void test_missing_per_id() {
		load("[font]\nface=KO.SVF\nmissing=u+25a1\n[font.3]\nmissing=off\n");
		TS_ASSERT_EQUALS(plan(0).missing, 0x25A1u);
		TS_ASSERT_EQUALS(plan(3).missing, 0u);
	}

	void test_glyph_steps_remap_then_rules_apply() {
		load("[fonts]\nsym=SYM.SVF\n[font]\nface=KO.SVF\norigin.general-punctuation=face\n"
			 "[glyphs]\n0x07=original\n0x5e=u+2026\n[glyphs.2]\n0x07=sym:u+2620\n0x08=ICONS.SVF:u+e001\n");
		uint32 cp = 0;
		const Graphics::HiResIdPlan p0 = plan(0);
		TS_ASSERT_EQUALS(p0.glyphFor(0x07, 0x07, cp), Graphics::kHiResGlyphStepGame);
		TS_ASSERT_EQUALS(p0.glyphFor(0x5e, 0x5e, cp), Graphics::kHiResGlyphStepDraw);
		TS_ASSERT_EQUALS(cp, 0x2026u);
		TS_ASSERT_EQUALS(p0.originFor(cp), Graphics::kHiResOriginFace);            // the remap goes through the rules
		TS_ASSERT_EQUALS(p0.glyphFor(0x41, 0x41, cp), Graphics::kHiResGlyphStepDraw);
		TS_ASSERT_EQUALS(cp, 0x41u);                                                 // no entry: decoded

		const Graphics::HiResIdPlan p2 = plan(2);
		TS_ASSERT_EQUALS(p2.glyphFor(0x07, 0x07, cp), Graphics::kHiResGlyphStepDraw);
		TS_ASSERT(cp >= Graphics::kHiResTargetedCodeBase);
		const Graphics::HiResGlyphTarget *t = p2.target(cp);
		TS_ASSERT(t);
		TS_ASSERT_EQUALS(t->cp, 0x2620u);
		TS_ASSERT_EQUALS(t->face.path.toString('/'), "/maps/SYM.SVF");
		TS_ASSERT_EQUALS(p2.chainFor(cp), (const Graphics::HiResFaceChain *)nullptr);  // range rules bypassed
		TS_ASSERT_EQUALS(p2.glyphFor(0x08, 0x08, cp), Graphics::kHiResGlyphStepDraw);
		TS_ASSERT_EQUALS(p2.target(cp)->cp, 0xE001u);                               // PUA target in a custom SVF
		TS_ASSERT_EQUALS(p2.glyphFor(0x5e, 0x5e, cp), Graphics::kHiResGlyphStepDraw);  // [glyphs] still applies to id 2
		TS_ASSERT_EQUALS(cp, 0x2026u);
	}

	void test_offset_and_fullwidth_recipe() {
		load("[font]\nface=KO.SVF\n[glyphs]\n0x21-0x7E=+0xFEE0\n0x20=u+3000\n");
		uint32 cp = 0;
		plan(0).glyphFor('A', 'A', cp);
		TS_ASSERT_EQUALS(cp, 0xFF21u);
		plan(0).glyphFor(' ', ' ', cp);
		TS_ASSERT_EQUALS(cp, 0x3000u);
	}

	void test_hash_changes_with_rules() {
		load("[font]\nface=KO.SVF\n");
		const uint32 a = plan(0).hash();
		load("[font]\nface=KO.SVF\nrange.basic-latin=original\n");
		TS_ASSERT_DIFFERS(a, plan(0).hash());
	}
};
```

- [ ] **Step 2: Run to verify it fails.** Run: `make -C /home/thkim/work/scummvm/builds/linux-dos-test-scumm -j8 test 2>&1 | grep -m5 'error'`. Expected: `id_plan.h: No such file or directory`.
- [ ] **Step 3: Implement** `compileIdPlan` per spec 5.3, 6.2, 6.5:
  - id chain: ini face (parsed with `parseFontValue(ini.face, map.faces, mapDir, gameDir, ...)`, `same` -> warning
    `hires_text_face=same has no meaning; ignoring it`) > `[font.N] face` (`same` = inherit) > `[font] face`
    (`same` -> warning, ignored). A face value that is exactly `original` sets `original`.
  - scopes for `faceRules`: `[font.N]` only when `original`, else `[font.N]`, `[font]`, `engineScope`. Rule values are
    re-indexed into `ruleChains`: expand each `same` into the id chain entries; when the value has no `same` and does
    not end in `original`, append the id chain; truncate at `original` and set `endsInOriginal`. A chain equal to just
    `original` makes `chainFor()` return nullptr.
  - `advanceRules`/`originRules` from the three scopes (always all three; `original` does not affect them).
  - numeric keys by principle 3 (ini `hires_text_size` > `[font.N]` > `[font]`; `sizeSet` false when none).
  - `glyphs` = `[glyphs]` then `[glyphs.N]` entries overwrite; targets appended in code order, virtual cp =
    `kHiResTargetedCodeBase + index`; a `same:` target keeps `kHiResFaceSame`.
  - `hash()`: FNV-1a over the chains' paths, the three tables' hashes, the value arrays, missing, size, pixel, shift,
    cell, align and the glyph table.
- [ ] **Step 4: Run the tests.** Run: `make -C /home/thkim/work/scummvm/builds/linux-dos-test-scumm -j8 test 2>&1 | tail -5`. Expected: only the known `hires_text_ttf_fit.h:493` failure.
- [ ] **Step 5: Commit** `GRAPHICS: Per-id compiled hi-res text plan` (4 paths incl. `graphics/module.mk`).

---

### Task 6: The shared ranged glyph source

**Files:**
- Create: `graphics/hires_text/glyph_source_ranged.h`, `graphics/hires_text/glyph_source_ranged.cpp`
- Modify: `graphics/module.mk`
- Test: `test/graphics/hires_text_glyph_source_ranged.h` (new)

**Interfaces:**
- Consumes: `HiResIdPlan` (Task 5), `UnicodeGlyphSource` (`glyph_source.h`).
- Produces (namespace `Graphics`):
  ```cpp
  struct HiResPick {
      enum Kind { kGame = 0, kFace } kind;
      int chain;         // 0 = idChain, i + 1 = ruleChains[i], -1 = a [glyphs] target
      int face;          // index in that chain, or the target index
      uint32 cp;         // the code point to ask the face for (the real target cp, or the missing box)
      bool missingBox;   // cp is plan.missing standing in for the asked one
  };
  // chainSources[0] parallels plan.idChain.faces, chainSources[i+1] plan.ruleChains[i].faces; nullptr = not opened.
  // targetSources parallels plan.targets (for a Same target: nullptr, the id chain is searched).
  HiResPick pickGlyph(const HiResIdPlan &plan, const Common::Array<Common::Array<UnicodeGlyphSource *> > &chainSources,
                      const Common::Array<UnicodeGlyphSource *> &targetSources, uint32 cp);
  class RangeRoutedGlyphSource : public UnicodeGlyphSource {
  public:
      RangeRoutedGlyphSource(const HiResIdPlan &plan, const Common::Array<Common::Array<UnicodeGlyphSource *> > &chainSources,
                             const Common::Array<UnicodeGlyphSource *> &targetSources,
                             DisposeAfterUse::Flag dispose = DisposeAfterUse::NO);
      HiResPick pick(uint32 cp);         // cached for the last cp
      // geometry from the first non-null source of chain 0, else the first non-null anywhere
      byte cellWidth() const override; byte cellHeight() const override;
      byte advanceNarrow() const override; byte advanceWide() const override; int bitsPerPixel() const override;
      int cells(uint32 cp) override; const byte *row(uint32 cp, int y) override;
      int advance(uint32 cp) override; bool metrics(uint32 cp, GlyphMetrics &m) override;
      uint32 glyphCount() const override;
  };
  ```
  `cells/row/advance/metrics` answer for the picked `{source, cp}`; `kGame` -> `cells()` returns 0 (the engine's own
  font then draws, as with any source lacking a glyph).

- [ ] **Step 1: Write the failing test** - use an in-memory fake source (no files):

```cpp
// test/graphics/hires_text_glyph_source_ranged.h
#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/hashmap.h"
#include "common/memstream.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/glyph_source_ranged.h"
#include "graphics/hires_text/id_plan.h"

class HiResRangedSourceTestSuite : public CxxTest::TestSuite {
	class Fake : public Graphics::UnicodeGlyphSource {
	public:
		Common::HashMap<uint32, bool> has;
		byte cellWidth() const override { return 16; }
		byte cellHeight() const override { return 16; }
		byte advanceNarrow() const override { return 8; }
		byte advanceWide() const override { return 16; }
		int bitsPerPixel() const override { return 1; }
		int cells(uint32 cp) override { return has.contains(cp) ? 1 : 0; }
		const byte *row(uint32 cp, int y) override { static byte r[2] = { 0xff, 0xff }; return has.contains(cp) ? r : nullptr; }
		uint32 glyphCount() const override { return has.size(); }
	};

	Graphics::HiResIdPlan compile(const char *text, int id) {
		const Common::String full = Common::String("[map]\nversion=2\n") + text;
		Common::MemoryReadStream s((const byte *)full.c_str(), full.size());
		Common::Array<Common::String> q, w;
		Graphics::HiResMap map;
		TS_ASSERT(Graphics::HiResFontMap::loadMap(s, Common::Path("/m", '/'), q, Graphics::kHiResKeysScumm, map));
		return Graphics::compileIdPlan(map, true, id, Graphics::HiResIniOverrides(), Graphics::HiResFontScope(),
									   Common::Path("/m", '/'), Common::Path("/g", '/'), w);
	}

public:
	void test_rule_chain_then_id_chain_by_coverage() {
		Graphics::HiResIdPlan p = compile("[font]\nface=KO.SVF\nrange.basic-latin=LAT.SVF\n", 0);
		Fake ko, lat;
		ko.has[0xAC00] = true;
		ko.has['B'] = true;
		lat.has['A'] = true;
		Common::Array<Common::Array<Graphics::UnicodeGlyphSource *> > chains;
		chains.resize(2);
		chains[0].push_back(&ko);                         // idChain = [KO]
		chains[1].push_back(&lat);                        // rule chain = [LAT, KO]
		chains[1].push_back(&ko);
		Common::Array<Graphics::UnicodeGlyphSource *> targets;
		Graphics::HiResPick a = Graphics::pickGlyph(p, chains, targets, 'A');
		TS_ASSERT_EQUALS(a.kind, Graphics::HiResPick::kFace);
		TS_ASSERT_EQUALS(a.chain, 1);
		TS_ASSERT_EQUALS(a.face, 0);
		Graphics::HiResPick b = Graphics::pickGlyph(p, chains, targets, 'B');   // LAT lacks B: falls to KO
		TS_ASSERT_EQUALS(b.chain, 1);
		TS_ASSERT_EQUALS(b.face, 1);
		Graphics::HiResPick h = Graphics::pickGlyph(p, chains, targets, 0xAC00);
		TS_ASSERT_EQUALS(h.chain, 0);
	}

	void test_missing_box_from_the_same_chain_else_game() {
		Graphics::HiResIdPlan p = compile("[font]\nface=KO.SVF\nmissing=u+25a1\n", 0);
		Fake ko;
		ko.has[0x25A1] = true;
		Common::Array<Common::Array<Graphics::UnicodeGlyphSource *> > chains;
		chains.resize(1);
		chains[0].push_back(&ko);
		Common::Array<Graphics::UnicodeGlyphSource *> targets;
		Graphics::HiResPick m = Graphics::pickGlyph(p, chains, targets, 0xD7A3);
		TS_ASSERT_EQUALS(m.kind, Graphics::HiResPick::kFace);
		TS_ASSERT(m.missingBox);
		TS_ASSERT_EQUALS(m.cp, 0x25A1u);
		ko.has.erase(0x25A1);
		TS_ASSERT_EQUALS(Graphics::pickGlyph(p, chains, targets, 0xD7A3).kind, Graphics::HiResPick::kGame);
	}

	void test_original_rule_and_no_missing_mean_game() {
		Graphics::HiResIdPlan p = compile("[font]\nface=KO.SVF\nmissing=u+25a1\nrange.basic-latin=original\n", 0);
		Fake ko;
		ko.has['A'] = true;
		ko.has[0x25A1] = true;
		Common::Array<Common::Array<Graphics::UnicodeGlyphSource *> > chains;
		chains.resize(1);
		chains[0].push_back(&ko);
		Common::Array<Graphics::UnicodeGlyphSource *> targets;
		TS_ASSERT_EQUALS(Graphics::pickGlyph(p, chains, targets, 'A').kind, Graphics::HiResPick::kGame);
	}

	void test_targets_bypass_rules_and_fall_back_to_game() {
		Graphics::HiResIdPlan p = compile("[font]\nface=KO.SVF\n[glyphs]\n0x07=SYM.SVF:u+2620\n0x08=same:u+2620\n", 0);
		Fake ko, sym;
		sym.has[0x2620] = true;
		ko.has[0x2620] = true;
		Common::Array<Common::Array<Graphics::UnicodeGlyphSource *> > chains;
		chains.resize(1);
		chains[0].push_back(&ko);
		Common::Array<Graphics::UnicodeGlyphSource *> targets;
		targets.push_back(&sym);      // target 0: SYM.SVF
		targets.push_back(nullptr);   // target 1: same
		uint32 cp = 0;
		p.glyphFor(0x07, 0x07, cp);
		Graphics::HiResPick t = Graphics::pickGlyph(p, chains, targets, cp);
		TS_ASSERT_EQUALS(t.chain, -1);
		TS_ASSERT_EQUALS(t.cp, 0x2620u);
		p.glyphFor(0x08, 0x08, cp);
		Graphics::HiResPick s = Graphics::pickGlyph(p, chains, targets, cp);
		TS_ASSERT_EQUALS(s.chain, 0);             // same: the id chain
		TS_ASSERT_EQUALS(s.cp, 0x2620u);
		sym.has.erase(0x2620);
		p.glyphFor(0x07, 0x07, cp);
		TS_ASSERT_EQUALS(Graphics::pickGlyph(p, chains, targets, cp).kind, Graphics::HiResPick::kGame);  // not the box
	}

	void test_source_answers_for_the_pick() {
		Graphics::HiResIdPlan p = compile("[font]\nface=KO.SVF\nrange.basic-latin=LAT.SVF\n", 0);
		Fake ko, lat;
		ko.has[0xAC00] = true;
		lat.has['A'] = true;
		Common::Array<Common::Array<Graphics::UnicodeGlyphSource *> > chains;
		chains.resize(2);
		chains[0].push_back(&ko);
		chains[1].push_back(&lat);
		chains[1].push_back(&ko);
		Graphics::RangeRoutedGlyphSource r(p, chains, Common::Array<Graphics::UnicodeGlyphSource *>());
		TS_ASSERT_EQUALS(r.cells('A'), 1);
		TS_ASSERT_EQUALS(r.cells(0xAC00), 1);
		TS_ASSERT_EQUALS(r.cells('Z'), 0);
		TS_ASSERT_EQUALS(r.cellHeight(), 16);
	}
};
```

- [ ] **Step 2: Run to verify it fails.** Run: `make -C /home/thkim/work/scummvm/builds/linux-dos-test-scumm -j8 test 2>&1 | grep -m5 'error'`. Expected: `glyph_source_ranged.h: No such file or directory`.
- [ ] **Step 3: Implement.** `pickGlyph`: `cp >= kHiResTargetedCodeBase` -> target: file target with the glyph ->
  `{kFace, -1, index, t.cp}`; `same` target -> search chain 0 for `t.cp`; otherwise `kGame`. Else
  `chain = plan.chainFor(cp)`; nullptr -> `kGame`; search its faces in order (`cells(cp) > 0`); none -> if
  `!chain->endsInOriginal && plan.missing` search the same chain for `plan.missing` (`missingBox = true`); else `kGame`.
  `RangeRoutedGlyphSource` keeps copies of the arrays and of the plan, caches the last `{cp, pick}`.
- [ ] **Step 4: Advance helper for the engines.** In `graphics/hires_text/latin_advance.{h,cpp}` add (beside the old
  `latinAdvanceGamePx`, which Task 13 deletes):
  ```cpp
  // advance=game|font as latinAdvanceGamePx() computes metrics=game|font; kHiResAdvanceCell and
  // kHiResAdvanceEngine return -1: the caller applies its cell or its own rule.
  int advanceGamePx(HiResAdvance advance, int gameWidth, int faceAdvanceHires, int scale);
  ```
  and a test in `test/graphics/hires_text_latin_advance.h`:
  ```cpp
  void test_advance_game_px_matches_the_old_metrics() {
  	for (int w = 0; w < 12; ++w)
  		for (int a = -1; a < 30; ++a) {
  			TS_ASSERT_EQUALS(Graphics::advanceGamePx(Graphics::kHiResAdvanceGame, w, a, 2),
  							 Graphics::latinAdvanceGamePx(Graphics::kHiResMetricsGame, w, a, 2));
  			TS_ASSERT_EQUALS(Graphics::advanceGamePx(Graphics::kHiResAdvanceFont, w, a, 2),
  							 Graphics::latinAdvanceGamePx(Graphics::kHiResMetricsFont, w, a, 2));
  		}
  	TS_ASSERT_EQUALS(Graphics::advanceGamePx(Graphics::kHiResAdvanceCell, 8, 20, 2), -1);
  	TS_ASSERT_EQUALS(Graphics::advanceGamePx(Graphics::kHiResAdvanceEngine, 8, 20, 2), -1);
  }
  ```
  (`test_advance_game_px_matches_the_old_metrics` is rewritten in Task 13 to fixed expected values when the old function
  goes.)
- [ ] **Step 5: Run the tests.** Run: `make -C /home/thkim/work/scummvm/builds/linux-dos-test-scumm -j8 test 2>&1 | tail -5`. Expected: only the known `hires_text_ttf_fit.h:493` failure.
- [ ] **Step 6: Commit** `GRAPHICS: A glyph source routed by the compiled Unicode range plan` (the ranged source files,
  `latin_advance.{h,cpp}`, both tests, `graphics/module.mk`).

---

### Task 7: SCUMM adapter - version-2 map, faces by range, `[glyphs]` with targets, missing, advance, origin

After this task SCUMM reads only version-2 maps (the shipped MI maps stop loading until Task 14 - expected; M5 is not run
here). The screen-format request in `scumm.cpp` is left as it is; it is fed from `blend` instead of `alpha` (Task 8
rewrites it).

**Files:**
- Modify: `engines/scumm/hires_text.h`, `engines/scumm/hires_text.cpp` (config loading at `loadConfig` ~2560-2760,
  `resolveCharsetFonts`, `faceForCodePoint` ~1821-1920, the `glyphOverride` call sites at ~1211/1507/1549/1596/1638,
  `latinBaselineShift`, `latinFaceStep`, `advancePlaced`, `cellRuleAdvance`, `wideStepsByFace`, `loadFonts`,
  `loadBitmapFile`, `namedBitmapHasCoverage`, `usesPerGlyph`, `probeSimpleFonts`), `engines/scumm/charset.cpp` (origin
  call sites of `latinBaselineShift`/`latinGlyphOffsets`)
- Test (rewrite to the new API and keys): `test/engines/scumm/hires_latin_same.h` (rename to `hires_range_rules.h`),
  `hires_glyph_advance.h`, `hires_latin_advance.h`, `hires_latin_baseline.h` (rename to `hires_origin.h`),
  `hires_wide_advance.h`, `hires_mirror.h`, `hires_shadow_rule.h`, `hires_glyph_source.h`, `hires_alpha_default.h`,
  `hires_scale_rule.h`, and any other `test/engines/scumm/*.h` that builds a map (`git grep -l "loadFromStream\|HiResTextConfig" test/engines/scumm`)
- Create: `test/engines/scumm/hires_glyph_targets.h`

**Interfaces:**
- Consumes: `HiResMap`, `HiResFontMap::loadMapFile/loadMap`, `kHiResKeysScumm` (Task 4); `HiResIdPlan`,
  `compileIdPlan` (Task 5); `pickGlyph`, `HiResPick` (Task 6); `readHiResIniFromConfMan`, `blendActive`,
  `resolveAutoTarget`, `advanceGamePx` (Tasks 3, 6).
- Produces (class `Scumm::ScummHiResText`, used by Task 8 and the tests):
  ```cpp
  void adoptMap(const Graphics::HiResMap &map, const Graphics::HiResIniOverrides &ini = Graphics::HiResIniOverrides());  // replaces adoptConfig()
  bool addFace(const Common::String &resolvedPath, Common::SeekableReadStream &stream);   // replaces addBitmapFont(); key = HiResFaceEntry::path.toString('/')
  Graphics::UnicodeGlyphSource *sourceForFace(const Common::String &resolvedPath) const;   // tests
  const Graphics::HiResIdPlan &planFor(int charsetId) const;
  Graphics::UnicodeGlyphSource *perGlyphSourceFor(int charsetId, uint32 &cp) const;       // kept; now [glyphs] + pickGlyph + nearestFont
  static Graphics::HiResFontScope engineScope();   // spec 8: range.basic-latin=same, advance.basic-latin=game
  const Graphics::HiResMap &map() const;           // replaces config(); shadow/layout/encoding readers use it
  ```
  Removed: `adoptConfig`, `addBitmapFont`, `CharsetFonts` (replaced by `Graphics::HiResIdPlan _plans[kMaxFonts]`),
  `_latinFaces`, `_latinSingleFace`, `_latinTtfFaces`, `latinTtfFaceFor`, `_singleFace` (a `[bitmap] single` concept),
  `_ttfPath`/`_ttfFromIni` (the ini face is inside the plans), `_metricsFromIni`, every read of `korean_*` keys.

Behaviour to implement (spec 5, 6, 8):
- `loadConfig()`: `hires_text=false` -> off (unchanged meaning, now through `readHiResIniFromConfMan`); map path per spec 4
  (`hires_text_map` relative to the game folder; unset -> `<game>/hires_text.map` if present; empty -> no map);
  `loadMapFile(path, {gameid, "v<N>"}, kHiResKeysScumm, _map)`; `_config.encoding` default from language, map `[text]
  encoding` wins; compile `_plans[i] = compileIdPlan(_map, haveMap, i, ini, engineScope(), mapDir, gameDir, warnings)`
  for i in 0..19. Scale: `clampScale(ini > map > 2, 1, 3, hiResScaleLimits(), "SCUMM", w)`. Blend wanted =
  `ini.blendSet ? ini.blend : map.blend` fed to the existing `wantsAlpha()` as
  `blend == on || (blend == auto && anyFaceHasCoverage)` (Task 8 replaces this with `render_target`).
- A loaded version-2 map always uses per-glyph placement (`_perGlyph = true`); the map-less `probeSimpleFonts()` form
  keeps the old `sourceFor()` path and builds its faces directly (it has no plan; its Latin companion routing goes: ASCII
  from the charset's own font).
- Faces are opened per distinct `(resolved path, pixel size)` into `_sources` (SVF sniffed by magic in every chain -
  `openTtfChain` gains the `SVFN` check that `sci/graphics/cache.cpp` `svfnSource()` has). An SVF whose cell height differs
  from the first SVF of its charset's id chain is refused: warning
  `hires_text.map: <path>: cell height <h> differs from <first path>'s <h0> on charset <N>; not used`.
- `faceForCodePoint(charsetId, cp, ...)`: `plan.glyphFor(chr, decoded, cp)` (Game -> declined), then
  `pickGlyph(plan, chainSources, targetSources, cp)`; `kFace` -> map `{chain, face}` back to the `Face*`; `kGame` with a
  real cp -> try `nearestFont()` borrowing exactly as today (never for a target); then the `missing` box is already in
  `pickGlyph`. Targets are opened at load; a target face lacking its cp is warned once:
  `hires_text.map: [glyphs] 0x<code> -> <face>:U+<cp>: the face has no such glyph; the game's font draws it`.
- Advance: `plan.advanceFor(cp)`: `Game`/`Font` -> `advanceGamePx()`; `Cell` -> the charset's cell rule (the old half);
  `Engine` -> the current C31 code path (`wideStepsByFace()`/`cellRuleAdvance()` with no metrics key). The remapped
  code's old exclusions (`hires_text.cpp` ~1596-1597, ~1638) are removed: the rules of the remapped cp apply (spec 6.5).
- Origin: `plan.originFor(cp) == kHiResOriginFace` replaces `latinBaselineByFace()`; it now applies to any code point,
  not only ASCII.
- `mirror` from the plan (`mirrorSet`), else the game table, as today.
- `[shadow]`, `[layout]` read from `_map` with unchanged meanings.

- [ ] **Step 1: Write the failing tests.** `test/engines/scumm/hires_glyph_targets.h` (new) - fixture as in today's
  `hires_latin_same.h` (`makeFont()`, `setUp` installing the null OSystem), with these helpers and cases:

```cpp
	bool open(Scumm::ScummHiResText &hr, Scumm::HiResOverlay &overlay, const char *body) {
		const Common::String text = Common::String("[map]\nversion=2\n[render]\nblend=off\n[font]\nmissing=u+25a1\n") + body;
		Graphics::HiResMap m;
		Common::Array<Common::String> q;
		Common::MemoryReadStream s((const byte *)text.c_str(), text.size());
		if (!Graphics::HiResFontMap::loadMap(s, Common::Path("/tmp/t", '/'), q, Graphics::kHiResKeysScumm, m))
			return false;
		hr.useOverlay(&overlay);
		hr.adoptMap(m);
		hr.noteGameCharset(kCs, 8, 8);
		hr.setCharsetGrid(kCs, 8, 8);
		return true;
	}

	bool add(Scumm::ScummHiResText &hr, const char *path, const Common::Array<uint32> &cps) {
		const Common::Array<byte> bytes = makeFont(cps);
		Common::MemoryReadStream ms(bytes.begin(), bytes.size());
		return hr.addFace(path, ms);
	}

	void test_targeted_glyph_draws_that_face_and_code_point() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\n[glyphs.4]\n0x07 = SYM.SVF:u+2620\n"));
		Common::Array<uint32> own, sym;
		own.push_back(0x2620);          // the id chain has it too: must not be used
		sym.push_back(0x2620);
		TS_ASSERT(add(hr, "/tmp/t/OWN.SVF", own));
		TS_ASSERT(add(hr, "/tmp/t/SYM.SVF", sym));
		uint32 cp = 0x07;
		TS_ASSERT_EQUALS(hr.perGlyphSourceFor(kCs, cp), hr.sourceForFace("/tmp/t/SYM.SVF"));
		TS_ASSERT_EQUALS(cp, 0x2620u);
	}

	void test_targeted_pua_glyph_in_a_custom_svf() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\n[glyphs]\n0x07 = ICONS.SVF:u+e001\n"));
		Common::Array<uint32> own, icons;
		own.push_back('A');
		icons.push_back(0xE001);
		TS_ASSERT(add(hr, "/tmp/t/OWN.SVF", own));
		TS_ASSERT(add(hr, "/tmp/t/ICONS.SVF", icons));
		uint32 cp = 0x07;
		TS_ASSERT_EQUALS(hr.perGlyphSourceFor(kCs, cp), hr.sourceForFace("/tmp/t/ICONS.SVF"));
		TS_ASSERT_EQUALS(cp, 0xE001u);
	}

	void test_target_lacking_the_glyph_falls_back_to_the_game_not_the_box() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\n[glyphs]\n0x07 = SYM.SVF:u+2620\n"));
		Common::Array<uint32> own, sym;
		own.push_back(0x25A1);          // the box exists, and is still not used
		sym.push_back('x');
		TS_ASSERT(add(hr, "/tmp/t/OWN.SVF", own));
		TS_ASSERT(add(hr, "/tmp/t/SYM.SVF", sym));
		uint32 cp = 0x07;
		TS_ASSERT(!hr.perGlyphSourceFor(kCs, cp));
	}

	void test_same_target_uses_the_id_chain_and_bypasses_ranges() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\nrange.misc-symbols=original\n[glyphs]\n0x07 = same:u+2620\n"));
		Common::Array<uint32> own;
		own.push_back(0x2620);
		TS_ASSERT(add(hr, "/tmp/t/OWN.SVF", own));
		uint32 cp = 0x07;
		TS_ASSERT_EQUALS(hr.perGlyphSourceFor(kCs, cp), hr.sourceForFace("/tmp/t/OWN.SVF"));
		uint32 direct = 0x2620;         // the same code point through the rules: original
		TS_ASSERT(!hr.perGlyphSourceFor(kCs, direct));
	}

	void test_target_face_is_not_a_fallback_for_other_codes() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\n[glyphs]\n0x07 = SYM.SVF:u+2620\n"));
		Common::Array<uint32> own, sym;
		own.push_back('A');
		sym.push_back(0xAC00);
		TS_ASSERT(add(hr, "/tmp/t/OWN.SVF", own));
		TS_ASSERT(add(hr, "/tmp/t/SYM.SVF", sym));
		uint32 cp = 0xAC00;
		Graphics::UnicodeGlyphSource *src = hr.perGlyphSourceFor(kCs, cp);
		TS_ASSERT(src != hr.sourceForFace("/tmp/t/SYM.SVF"));   // the box from OWN, or nothing - never SYM
	}

	void test_original_in_glyphs_declines() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\n[glyphs]\n0x5f = original\n"));
		Common::Array<uint32> own;
		own.push_back('_');
		TS_ASSERT(add(hr, "/tmp/t/OWN.SVF", own));
		uint32 cp = 0x5f;
		TS_ASSERT(!hr.perGlyphSourceFor(kCs, cp));
	}
```

  Rewrite the existing suites to the new keys and API, keeping every behaviour they pin down, with these mappings (the
  old test name -> the new case, which must exist after the rewrite):
  - `hires_latin_same.h` -> `hires_range_rules.h`: `range.basic-latin=same` prefers the charset's own face over a
    `range`-named Latin face elsewhere; without it the named Latin face wins; `same` borrows the nearest charset when this
    one has none; the missing box is drawn for **a Hangul syllable the SVF lacks** (new: was ASCII only) and for ASCII.
  - `hires_latin_advance.h`, `hires_glyph_advance.h`, `hires_wide_advance.h`: `metrics=` cases become `advance=`/
    `advance.basic-latin=`/`advance.wide=`; a new case: `advance.U+2026=game` gives the remapped `0x5e` the game width.
  - `hires_latin_baseline.h` -> `hires_origin.h`: `baseline=face` becomes `origin.basic-latin=face`; the old "remapped
    code keeps the game offsets" case is inverted: with `origin.general-punctuation=face`, `0x5e -> u+2026` sits on the
    face baseline.
  - `hires_mirror.h`: `mirror=true` -> `mirror=horizontal`; a `[font.N]` with only `mirror=` still leaves placement alone.
  - `hires_alpha_default.h`: `alpha=` cases become `[render] blend=`; the C17 "map without alpha blends coverage fonts" is
    `blend=auto` with a 2 bpp SVF -> blending wanted; 1 bpp -> not.
  - SVF through `face=`: a `[font.4] face=CARD.SVF` opens as a bitmap font (was `bitmap=`).
  - An SVF of a different cell height on the same charset is refused with the warning text above.

- [ ] **Step 2: Run to verify they fail**
  Run: `make -C /home/thkim/work/scummvm/builds/linux-dos-test-scumm -j8 test 2>&1 | grep -m5 "error:"`
  Expected: compile errors (`adoptMap`, `addFace`, `sourceForFace` not members).
- [ ] **Step 3: Implement** the behaviour list above. Remove the dead code the list names; do not leave compatibility
  shims. Keep `debug(1, ...)` lines for loaded faces (`hi-res font N <- <path>`), they are read by the harness logs.
- [ ] **Step 4: Run the tests.** Run: `for d in linux-dos-test-scumm linux-dos-scumm; do make -C /home/thkim/work/scummvm/builds/$d -j8 test 2>&1 | tail -3; done`.
  Expected: each only the known `hires_text_ttf_fit.h:493` failure; report the test counts.
- [ ] **Step 5: Linux smoke with a hand-written v2 map.** Write
  `/tmp/claude-1000/-home-thkim-work/36a78c74-674a-4d24-9ca3-7236f6e2ebde/scratchpad/M2KOU-v2.MAP` by converting the
  committed `dists/engine-data/hires_text/dos/M2KOU.MAP` with spec 6.6, run the Linux build
  (`builds/linux-dos-scumm/scummvm`) on `mi2ko` with `extrapath` = a scratch copy of `dist/dos/DATA` holding that map,
  and take one dump at the difficulty card with the harness client (`harness/dos/scummgame.py`, as `m5_points.py` does
  for point A). Look at the image yourself: Korean text, `!` and `"` from the SVF, no tofu. Put the PNG path in the
  report.
- [ ] **Step 6: Commit** `SCUMM: Hi-res text reads version 2 maps: faces by Unicode range, [glyphs] targets` with the
  exact list of modified/renamed/created files (`git add` each; `git mv` for renames).

---

### Task 8: SCUMM render target, blend, scale and the options dialog

**Files:**
- Modify: `engines/scumm/scumm.cpp` (~1560-1660, the `initGraphics` format requests), `engines/scumm/hires_text.{h,cpp}`,
  `engines/scumm/metaengine.cpp` (~951-967), `engines/scumm/dialogs.cpp` (~1225-1259, ~1702)
- Test: `test/engines/scumm/hires_render_target.h` (new), `test/engines/scumm/hires_scale_rule.h` (rewrite)

**Interfaces:**
- Consumes: Task 3 (`formatRequest`, `resolveAutoTarget`, `blendActive`, `clampScale`, `hiResScaleLimits`), Task 7.
- Produces:
  ```cpp
  // ScummHiResText, pure (no ConfMan, no OSystem):
  static Graphics::HiResRenderTarget wantedTarget(const Graphics::HiResMap &map, bool mapLoaded,
                                                  const Graphics::HiResIniOverrides &ini, int gameVersion,
                                                  bool anyCoverage, Common::String &warning);
  static int resolvedScale(const Graphics::HiResMap &map, bool mapLoaded, const Graphics::HiResIniOverrides &ini,
                           const Graphics::HiResScaleLimits &platform, Common::String &warning);
  Graphics::HiResRenderTarget renderTarget() const;   // what loadConfig() resolved
  ```
  `wantedTarget`: v >= 7 -> `clut8` (warning `SCUMM v7+ keeps a paletted screen; render_target=<x> ignored` when an
  explicit non-clut8 target was asked); explicit ini/map target -> it; `auto` -> `resolveAutoTarget(anyCoverage, blend)`.

Behaviour:
- `scumm.cpp`: the non-Towns path asks `initGraphics(w, h, Graphics::formatRequest(renderTarget(), _system->getSupportedFormats(), true, note))`;
  `note` non-empty -> `warning("SCUMM: %s", note.c_str())`. `setAlphaActive(Graphics::blendActive(blend, anyCoverage, chosen.isCLUT8()))`.
  A 2-byte screen uses `HiResPalette16Sink` (exists, gfx.cpp ~773). The warning `SCUMM: no 32bpp screen available ...`
  is replaced by the `formatRequest` note. The FM-Towns 16-bit path is unchanged.
- Scale: `resolvedScale()` replaces the `hires_text_scale`/`korean_hires_scale` readers; `ConfMan.getInt` is not used.
- Options dialog: the checkbox "Smooth the hi-res text" shows `blendActive(...)` of the current settings; on save it
  writes `hires_text_blend=on|off` only when the checkbox state differs from the state it was opened with. The ExtraGui
  option `hires_text_alpha` in `metaengine.cpp` is renamed `hires_text_blend` and loses its default (no `true` default
  is registered).

- [ ] **Step 1: Write the failing test**

```cpp
// test/engines/scumm/hires_render_target.h
#include <cxxtest/TestSuite.h>

#include "common/memstream.h"
#include "engines/scumm/hires_text.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/hires_options.h"

class ScummHiResRenderTargetTestSuite : public CxxTest::TestSuite {
	Graphics::HiResMap map(const char *render) {
		const Common::String text = Common::String("[map]\nversion=2\n[render]\n") + render;
		Common::MemoryReadStream s((const byte *)text.c_str(), text.size());
		Common::Array<Common::String> q;
		Graphics::HiResMap m;
		TS_ASSERT(Graphics::HiResFontMap::loadMap(s, Common::Path("/m", '/'), q, Graphics::kHiResKeysScumm, m));
		return m;
	}

public:
	void test_explicit_map_target() {
		Common::String w;
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::wantedTarget(map("target=rgb565\n"), true, Graphics::HiResIniOverrides(), 5, true, w),
						 Graphics::kHiResTargetRgb565);
	}

	void test_ini_beats_map() {
		Graphics::HiResIniOverrides ini;
		ini.targetSet = true;
		ini.target = Graphics::kHiResTargetClut8;
		Common::String w;
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::wantedTarget(map("target=rgb888\n"), true, ini, 5, true, w),
						 Graphics::kHiResTargetClut8);
		TS_ASSERT(w.empty());
	}

	void test_auto_follows_coverage_and_blend() {
		Common::String w;
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::wantedTarget(map("target=auto\n"), true, Graphics::HiResIniOverrides(), 5, false, w),
						 Graphics::kHiResTargetClut8);
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::wantedTarget(map("blend=auto\n"), true, Graphics::HiResIniOverrides(), 5, true, w),
						 Graphics::kHiResTargetRgb888);
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::wantedTarget(map("blend=off\n"), true, Graphics::HiResIniOverrides(), 5, true, w),
						 Graphics::kHiResTargetClut8);
	}

	void test_v7_stays_paletted() {
		Common::String w;
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::wantedTarget(map("target=rgb888\n"), true, Graphics::HiResIniOverrides(), 7, true, w),
						 Graphics::kHiResTargetClut8);
		TS_ASSERT_EQUALS(w, "SCUMM v7+ keeps a paletted screen; render_target=rgb888 ignored");
	}

	void test_scale_default_limits_and_dos() {
		Common::String w;
		const Graphics::HiResScaleLimits desktop = { 1, 3 };
		const Graphics::HiResScaleLimits dos = { 2, 2 };
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::resolvedScale(map("target=auto\n"), true, Graphics::HiResIniOverrides(), desktop, w), 2);
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::resolvedScale(map("scale=3\n"), true, Graphics::HiResIniOverrides(), desktop, w), 3);
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::resolvedScale(map("scale=3\n"), true, Graphics::HiResIniOverrides(), dos, w), 2);
		TS_ASSERT_EQUALS(w, "the DOS backend runs hi-res text at 2x only; using 2");
		Graphics::HiResIniOverrides ini;
		ini.scaleSet = true;
		ini.scale = 1;
		w.clear();
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::resolvedScale(map("scale=3\n"), true, ini, desktop, w), 1);
	}
};
```

- [ ] **Step 2: Run to verify it fails.** Run: `make -C /home/thkim/work/scummvm/builds/linux-dos-test-scumm -j8 test 2>&1 | grep -m5 'error'`. Expected: `wantedTarget` is not a member of `Scumm::ScummHiResText`.
- [ ] **Step 3: Implement** the behaviour list.
- [ ] **Step 4: Run the tests.** Run: `for d in linux-dos-test-scumm linux-dos-scumm; do make -C /home/thkim/work/scummvm/builds/$d -j8 test 2>&1 | tail -3; done`. Expected: each only the known `hires_text_ttf_fit.h:493` failure.
- [ ] **Step 5: Linux check of the three targets.** With the Task 7 scratch map, run `mi2ko` on
  `builds/linux-dos-scumm/scummvm` three times with `render_target=rgb888`, `rgb565`, `clut8` in the scratch ini; each
  run's log line `SCUMM: hi-res text blending into <format>` (or its absence for clut8) matches, and one dump per run at
  point A shows the text (look at them). Record the three formats in the report.
- [ ] **Step 6: Commit** `SCUMM: Hi-res text render target, blend and scale from the unified keys`.

---

### Task 9: SCI adapter - version-2 map, faces by range, `[glyphs]`, missing, advance

After this task SCI reads only version-2 maps (the shipped SCI maps stop loading until Task 14 - expected).

**Files:**
- Modify: `engines/sci/graphics/hirestextsettings.{h,cpp}` (`FontSettings`, `HiresTextOverrides` removed,
  `resolveFontSettings`, `warnScummOnlyMapKeys` removed, `unicodeBundleKey`), `engines/sci/graphics/cache.{h,cpp}`
  (`resolveHiresText` ~87-260, `fontSettingsFor`, `toHiResLatinMode` removed, `unicodeFaceFor`, `faceChainFor`,
  `applyMissing`, the `RoutedGlyphSource` construction ~522-602), `engines/sci/graphics/fontset.{h,cpp}`,
  `engines/sci/graphics/fontunicode.{h,cpp}`, `engines/sci/graphics/text16.{h,cpp}` (`glyphChar` ~963-986, the face
  tally), `engines/sci/graphics/textlatin.h` (LatinMode and `TextCompose::latinFullwidth`/`asciiGoesToUnicodeFace`
  replaced), `engines/sci/graphics/textlayout16.cpp` (~54)
- Test (rewrite): `test/engines/sci/hirestextsettings.h`, `test/engines/sci/textlatin.h` (rename to `textrules.h`),
  `test/engines/sci/glyphplacement.h`, `test/engines/sci/i18n_gates.h`

**Interfaces:**
- Consumes: Tasks 3-6.
- Produces (namespace `Sci`):
  ```cpp
  struct FontSettings {                  // geometry + the compiled plan; the Latin fields are gone
      FontSettings();
      static const int kDefaultCell = 16;
      Graphics::HiResIdPlan plan;
      bool original;                     // plan.original with no own ranges: the resource font only, no .uni
      Common::String facePath;           // plan.idChain.faces[0].path, native separators; empty = none
      Common::Array<Common::String> faceChain;
      int size; int cell; int baseline /* = shift */; Graphics::HiResAlign align; int pixel;
  };
  Graphics::HiResFontScope sciEngineScope();   // range.basic-latin=original, advance.basic-latin=game (spec 8)
  FontSettings resolveFontSettings(const Graphics::HiResMap &map, bool mapLoaded, int fontId,
                                   const Graphics::HiResIniOverrides &ini, const Common::Path &mapDir,
                                   const Common::Path &gameDir);
  Common::String unicodeBundleKey(const Common::String &mainPath, int size, uint32 planHash);
  namespace TextCompose {
      // the [glyphs] step for SCI: the code GfxText16::glyphChar() returns. Game -> Graphics::kHiResGameCodeBase + chr.
      uint32 glyphCode(const Graphics::HiResIdPlan &plan, uint32 chr);
      // whether a (post-glyphCode) code is drawn by the Unicode face set rather than the resource font
      bool goesToUnicodeFace(const Graphics::HiResIdPlan &plan, uint32 code);
  }
  ```
  `GfxFontSet`/`GfxFontUnicodeAdapter` decode `code >= kHiResGameCodeBase` to "resource font draws `code - base`" and
  `code >= kHiResTargetedCodeBase` (and `< kHiResGameCodeBase`) to a target, answered by the ranged source.

Behaviour:
- `resolveHiresText()`: ini through `readHiResIniFromConfMan(domain)`; `hires_text=false` -> the layer as if no map and
  no ini keys; map path per spec 4 (relative `hires_text_map` = game folder, no longer the current directory);
  `loadMap(stream, mapDir, {platform}, kHiResKeysSci, _hiresMap)`. Every `hires_text_latin*`, `hires_text_font*`,
  `hires_text_metrics` reader is deleted.
- Per font id: `resolveFontSettings()`; the chains of `plan.idChain` and every `plan.ruleChains[i]` and targets are
  opened with the existing `singleFace()`/`svfnSource()`/`ttfSource()` (a target TrueType face: `kProbesDefault`, its fit
  probes = its target cps); the Unicode source is a `Graphics::RangeRoutedGlyphSource` over them (replacing
  `RoutedGlyphSource`). The bundle key includes `plan.hash()`.
- ASCII routing: `TextCompose::goesToUnicodeFace(plan, cp)` replaces `asciiGoesToUnicodeFace(chr, mode)` at
  `fontset.cpp` ~108/215/288 and `fontunicode.cpp` ~232/240/330/337/366/383/407: true iff `plan.chainFor(cp)` is non-null
  (for `cp < 0x80` the engine scope makes it null unless a map rule says otherwise).
- `glyphChar()`: `TextCompose::glyphCode(plan, chr)` replaces `latinFullwidth()`; the fullwidth recipe is a map
  `[glyphs]` now; `readChar()` still returns the raw character.
- Advance: `plan.advanceFor(cp)`: `Game`/`Font` -> `Graphics::advanceGamePx()` (was `latinAdvanceGamePx(metrics, ...)`);
  `Cell` -> the narrow/wide cell (was `half`); `Engine` -> today's path (cell for a legacy code-page game, per-glyph
  advances for a UTF-8 translation).
- `missing` per font id from the plan (was map-wide only); `applyMissing()` warns once per chain without the box glyph:
  `hires_text.map: missing=U+%04X has no effect: %s has no glyph for it` (text unchanged).
- The `hires_text_log` tally: `kTextFaceLatin` becomes `kTextFaceRule` ("a range rule's chain drew it"), printed as
  `rule=N`.

- [ ] **Step 1: Write the failing tests.** In `test/engines/sci/hirestextsettings.h` (rewrite; keep the existing
  helpers that build a map from a string, switched to `loadMap(..., kHiResKeysSci, ...)`), at least:

```cpp
	void test_default_latin_is_the_resource_font() {
		Sci::FontSettings s = settings("[font]\nface=KO.SVF\n", 4);
		TS_ASSERT(!Sci::TextCompose::goesToUnicodeFace(s.plan, 'A'));
		TS_ASSERT(Sci::TextCompose::goesToUnicodeFace(s.plan, 0xAC00));
	}

	void test_range_same_routes_ascii_to_the_face() {
		Sci::FontSettings s = settings("[font]\nface=KO.SVF\nrange.basic-latin=same\nadvance.basic-latin=font\n", 4);
		TS_ASSERT(Sci::TextCompose::goesToUnicodeFace(s.plan, 'A'));
		TS_ASSERT_EQUALS(s.plan.advanceFor('A'), Graphics::kHiResAdvanceFont);
	}

	void test_per_id_advance_game() {
		Sci::FontSettings s = settings("[font]\nface=KO.SVF\nrange.basic-latin=same\nadvance.basic-latin=font\n"
									   "[font.8]\nadvance.basic-latin=game\n", 8);
		TS_ASSERT_EQUALS(s.plan.advanceFor(' '), Graphics::kHiResAdvanceGame);
	}

	void test_fullwidth_recipe_via_glyphs() {
		Sci::FontSettings s = settings("[font]\nface=KO.SVF\n[glyphs]\n0x21-0x7E=+0xFEE0\n0x20=u+3000\n", 0);
		TS_ASSERT_EQUALS(Sci::TextCompose::glyphCode(s.plan, 'A'), 0xFF21u);
		TS_ASSERT_EQUALS(Sci::TextCompose::glyphCode(s.plan, ' '), 0x3000u);
		TS_ASSERT(Sci::TextCompose::goesToUnicodeFace(s.plan, 0xFF21));
	}

	void test_glyphs_original_is_the_resource_font() {
		Sci::FontSettings s = settings("[font]\nface=KO.SVF\nrange.basic-latin=same\n[glyphs]\n0x40=original\n", 0);
		const uint32 code = Sci::TextCompose::glyphCode(s.plan, '@');
		TS_ASSERT_EQUALS(code, Graphics::kHiResGameCodeBase + '@');
		TS_ASSERT(!Sci::TextCompose::goesToUnicodeFace(s.plan, code));
	}

	void test_targeted_glyph_code() {
		Sci::FontSettings s = settings("[font]\nface=KO.SVF\n[glyphs]\n0x2605=ICONS.SVF:u+e001\n", 0);
		const uint32 code = Sci::TextCompose::glyphCode(s.plan, 0x2605);
		TS_ASSERT(code >= Graphics::kHiResTargetedCodeBase && code < Graphics::kHiResGameCodeBase);
		TS_ASSERT_EQUALS(s.plan.target(code)->cp, 0xE001u);
		TS_ASSERT(Sci::TextCompose::goesToUnicodeFace(s.plan, code));
	}

	void test_face_original_keeps_the_resource_font_whole() {
		Sci::FontSettings s = settings("[font]\nface=KO.SVF\n[font.2]\nface=original\n", 2);
		TS_ASSERT(s.original);
		TS_ASSERT(!Sci::TextCompose::goesToUnicodeFace(s.plan, 0xAC00));
	}

	void test_ini_paths_are_game_folder_relative() {
		Graphics::HiResIniOverrides ini;
		ini.faceSet = true;
		ini.face = "fonts/X.TTF";
		Sci::FontSettings s = settingsWithIni("[font]\nface=KO.SVF\n", 0, ini);
		TS_ASSERT_EQUALS(s.facePath, Common::Path("/games/kq1/fonts/X.TTF", '/').toString(Common::Path::kNativeSeparator));
	}

	void test_shift_cell_align_size() {
		Sci::FontSettings s = settings("[font]\nface=KO.SVF\nshift=-1\nalign=cell\n[font.40]\ncell=glyph\nsize=18\n", 40);
		TS_ASSERT_EQUALS(s.baseline, -1);
		TS_ASSERT_EQUALS(s.align, Graphics::kHiResAlignCell);
		TS_ASSERT_EQUALS(s.cell, 18);
		TS_ASSERT_EQUALS(s.size, 18);
	}
```
  (`settings(map, id)` = load with `mapDir=/maps`, `gameDir=/games/kq1`, empty ini, then `resolveFontSettings`;
  `settingsWithIni` the same with an ini.) Rewrite `textlatin.h` -> `textrules.h` so each old Latin-mode test becomes the
  recipe of spec 6.6 and asserts the same routing/advance; rewrite `glyphplacement.h` and `i18n_gates.h` map strings to
  version 2.
- [ ] **Step 2: Run to verify they fail.** Run: `make -C /home/thkim/work/scummvm/builds/linux-dos-test -j8 test 2>&1 | grep -m5 'error'`. Expected: compile errors (`goesToUnicodeFace`, `glyphCode`, the new `resolveFontSettings` signature).
- [ ] **Step 3: Implement** the behaviour list.
- [ ] **Step 4: Run the tests.** Run: `for d in linux-dos-test linux-dos-test-scumm; do make -C /home/thkim/work/scummvm/builds/$d -j8 test 2>&1 | tail -3; done`. Expected: each only the known `hires_text_ttf_fit.h:493` failure.
- [ ] **Step 5: Linux pixel check against the pre-change references.** Convert the committed `KQ1KOL.MAP`, `KQ1KOU.MAP`,
  `LB1KOL.MAP`, `LB1KOU.MAP` by spec 6.6 into a scratch `DATA` copy (with `[render] target=rgb888` in the U maps, as
  `rgb_rendering` is still upstream and still set by the M2 harness ini). Rebuild `builds/linux-dos-noft` and
  `builds/linux-dos-test` and run only the Linux halves of M1/M2 through their own scripts with `MAPS` pointed at the
  scratch copy (edit a scratch copy of each script, not the harness). Compare every `linux/*.bin` dump with
  `runs/unify-base/dos-m1|dos-m2/...` (`cmp`). Expected: identical, except where `runs/unify-base/DISTS_COMMIT` differs
  from the current `git log -1 --format=%H -- dists/engine-data/hires_text/dos` (another agent changed fonts/maps since:
  say so, and compare only the unaffected games).
- [ ] **Step 6: Commit** `SCI: Hi-res text reads version 2 maps: faces by Unicode range, [glyphs], per-id missing`.

---

### Task 10: SCI render target, blend and scale

**Files:**
- Modify: `engines/sci/graphics/drivers/init.cpp` (~157-170, `requestRGB`), `engines/sci/graphics/drivers/default.cpp`
  (~100-135, `initScreen`), `engines/sci/graphics/hirestextsettings.{h,cpp}`, `engines/sci/graphics/cache.{h,cpp}`,
  `engines/sci/sci.{h,cpp}`, `engines/sci/module.mk`; create `engines/sci/graphics/hirestextstate.{h,cpp}`; the SCI code that decides whether hi-res
  glyph coverage is blended (find it: `git grep -n "bitsPerPixel()\|isCLUT8()\|bytesPerPixel" engines/sci/graphics/{fontunicode,text16,screen,cache}.cpp`;
  today it blends whenever the screen is RGB)
- Test: `test/engines/sci/hiresrender.h` (new)

**Interfaces:**
- Consumes: Task 3, Task 9.
- Produces (namespace `Sci`):
  ```cpp
  struct SciRenderChoice { bool requestRGB; Graphics::HiResRenderTarget target; Common::String warning; };
  // upstreamRgb = rgb_rendering || palette_mods; target from ini > [scummvm] > map (auto when unset)
  SciRenderChoice chooseSciRender(bool upstreamRgb, Graphics::HiResRenderTarget target, bool anyCoverage,
                                  Graphics::HiResBlend blend);
  int sciHiresScale(const Graphics::HiResMap &map, bool mapLoaded, const Graphics::HiResIniOverrides &ini,
                    const Graphics::HiResScaleLimits &platform, Common::String &warning);   // always 2
  ```
  `chooseSciRender`: `auto` + upstreamRgb -> `{true, auto}` (upstream request unchanged: the driver's own format choice);
  `auto` otherwise -> `resolveAutoTarget(anyCoverage, blend)`, `requestRGB = target != clut8`; explicit `clut8` +
  upstreamRgb -> `{false, clut8, "render_target=clut8 wins over rgb_rendering/palette_mods; the screen stays paletted"}`;
  explicit `rgb565` -> `{true, rgb565}` (then `formatRequest(..., engineCanRgb565=false, ...)` turns it into rgb888 with
  its note until Task 19); explicit `rgb888` -> `{true, rgb888}`.

Behaviour:
- Load order: the driver is created in `GfxScreen`'s constructor (`screen.cpp` ~148), and `GfxCache` - which loads the
  map today - is created later (`sci.cpp` ~794). So move the ini + map loading out of `GfxCache::resolveHiresText()` into
  a new `Sci::HiresTextState` (`engines/sci/graphics/hirestextstate.{h,cpp}`: holds the `HiResIniOverrides`, the
  `HiResMap`, `mapLoaded`, `mapDir`, `gameDir`), created by `SciEngine` just before `GfxScreen` is constructed and
  reachable as `g_sci->hiresTextState()`. `GfxCache` keeps the "does hi-res text apply" gate (`hiresTextApplies()`), which
  it evaluates exactly as today, and reads the rest from the state. Add `hirestextstate.o` to `engines/sci/module.mk`.
- `drivers/init.cpp create()`: `requestRGB` from `chooseSciRender(upstreamRgb, target, anyCoverage, blend)` with the
  target and blend of `g_sci->hiresTextState()` (`anyCoverage` = some face of some plan is 2/8 bpp SVF or TrueType,
  sniffed from the file headers without opening glyph data).
- `GfxDefaultDriver::initScreen()`: when the choice's target is not `auto`, pass
  `Graphics::formatRequest(target, g_system->getSupportedFormats(), false, note)` to `initGraphics()`; `auto` keeps the
  upstream code path byte for byte.
- Blending: glyph coverage is blended iff `Graphics::blendActive(blend, faceHasCoverage, screenIsClut8)`.
- Scale: `sciHiresScale()` gives 2 and at most one warning (`SCI draws hi-res text at 2x only; using 2` or the DOS one).

- [ ] **Step 1: Write the failing test**

```cpp
// test/engines/sci/hiresrender.h
#include <cxxtest/TestSuite.h>

#include "engines/sci/graphics/hirestextsettings.h"
#include "graphics/hires_text/hires_options.h"

class SciHiresRenderTestSuite : public CxxTest::TestSuite {
public:
	void test_auto_keeps_upstream_rgb_rendering() {
		Sci::SciRenderChoice c = Sci::chooseSciRender(true, Graphics::kHiResTargetAuto, false, Graphics::kHiResBlendAuto);
		TS_ASSERT(c.requestRGB);
		TS_ASSERT_EQUALS(c.target, Graphics::kHiResTargetAuto);
		TS_ASSERT(c.warning.empty());
	}

	void test_auto_without_upstream_follows_coverage() {
		TS_ASSERT(!Sci::chooseSciRender(false, Graphics::kHiResTargetAuto, false, Graphics::kHiResBlendAuto).requestRGB);
		Sci::SciRenderChoice c = Sci::chooseSciRender(false, Graphics::kHiResTargetAuto, true, Graphics::kHiResBlendAuto);
		TS_ASSERT(c.requestRGB);
		TS_ASSERT_EQUALS(c.target, Graphics::kHiResTargetRgb888);
		TS_ASSERT(!Sci::chooseSciRender(false, Graphics::kHiResTargetAuto, true, Graphics::kHiResBlendOff).requestRGB);
	}

	void test_explicit_clut8_beats_rgb_rendering() {
		Sci::SciRenderChoice c = Sci::chooseSciRender(true, Graphics::kHiResTargetClut8, true, Graphics::kHiResBlendAuto);
		TS_ASSERT(!c.requestRGB);
		TS_ASSERT_EQUALS(c.warning, "render_target=clut8 wins over rgb_rendering/palette_mods; the screen stays paletted");
	}

	void test_explicit_rgb_targets() {
		TS_ASSERT_EQUALS(Sci::chooseSciRender(false, Graphics::kHiResTargetRgb888, false, Graphics::kHiResBlendAuto).target,
						 Graphics::kHiResTargetRgb888);
		TS_ASSERT(Sci::chooseSciRender(false, Graphics::kHiResTargetRgb565, false, Graphics::kHiResBlendAuto).requestRGB);
	}

	void test_scale_is_two() {
		Graphics::HiResMap m;
		m.scale = 3;
		m.scaleSet = true;
		Common::String w;
		const Graphics::HiResScaleLimits desktop = { 1, 3 };
		TS_ASSERT_EQUALS(Sci::sciHiresScale(m, true, Graphics::HiResIniOverrides(), desktop, w), 2);
		TS_ASSERT_EQUALS(w, "SCI draws hi-res text at 2x only; using 2");
	}
};
```
- [ ] **Step 2: Run to verify it fails.** Run: `make -C /home/thkim/work/scummvm/builds/linux-dos-test -j8 test 2>&1 | grep -m5 'error'`. Expected: `chooseSciRender` / `sciHiresScale` not declared.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run the tests.** Run: `for d in linux-dos-test linux-dos-test-scumm; do make -C /home/thkim/work/scummvm/builds/$d -j8 test 2>&1 | tail -3; done`. Expected: each only the known `hires_text_ttf_fit.h:493` failure.
- [ ] **Step 5: Linux check** with the Task 9 scratch maps: KQ1 U map with `target=rgb888` and **no** `rgb_rendering`
  in the scratch ini gives a 4-byte screen and blended text (dump T, look at it); the same with `render_target=clut8`
  gives CLUT8 and a hard stencil; `rgb_rendering=true` + `render_target=auto` with the L map behaves as upstream (RGB).
- [ ] **Step 6: Commit** `SCI: Hi-res text render target, blend and scale from the unified keys`.

---

### Task 11: DOS backend - `render_target` caps the formats, `dos_truecolor` removed, 2x only

**Files:**
- Modify: `backends/platform/dos/dos-modes.h` (`supportedFormats` ~80-98), `backends/graphics/dos/dos-graphics.cpp`
  (`getSupportedFormats` ~133-140), `backends/platform/dos/dos.cpp` (~170-181 `registerDefault`s)
- Test: `test/backends/dos_modes.h`

**Interfaces:**
- Consumes: `Graphics::HiResRenderTarget`, `parseRenderTarget`, `formatMatchesTarget` (Task 3).
- Produces:
  ```cpp
  namespace DOS {
  // every format chooseMode() can set for w x h (exact or line repeat), cheapest first (rgb565, xrgb1555, xrgb8888),
  // filtered by `cap` (auto: no filter; clut8: none; rgb565: 5-6-5 only; rgb888: 4-byte 8-8-8 only), then CLUT8.
  Common::List<Graphics::PixelFormat> supportedFormats(const Common::Array<VideoMode> &modes, uint w, uint h,
                                                       Graphics::HiResRenderTarget cap = Graphics::kHiResTargetAuto);
  }
  ```
  `DosGraphicsManager::getSupportedFormats()` reads `ConfMan.get("render_target")` (active domain, then `[scummvm]`); an
  invalid value -> one `warning("DOS: render_target '%s' is not auto, clut8, rgb565 or rgb888; using auto")`.
  `OSystem_DOS::initBackend()`: remove `registerDefault("dos_truecolor", ...)` and its comment; add
  `ConfMan.registerDefault("hires_text_platform_scale", "2");` with a comment citing spec 7.4.

- [ ] **Step 1: Write the failing tests** (append to `test/backends/dos_modes.h`, reusing its `dosboxX()`/`staging()` mode lists; replace every `true` last argument by `Graphics::kHiResTargetAuto` and `false` by `Graphics::kHiResTargetClut8`)

```cpp
	void test_render_target_caps_the_list() {
		Common::List<Graphics::PixelFormat> x565 = DOS::supportedFormats(dosboxX(), 640, 400, Graphics::kHiResTargetRgb565);
		TS_ASSERT_EQUALS(x565.size(), 2u);
		TS_ASSERT(x565.front() == DOS::rgb565());
		TS_ASSERT(x565.back().isCLUT8());

		// Staging has no 640x400 5-6-5: the 640x480 line-repeat mode serves it.
		Common::List<Graphics::PixelFormat> s565 = DOS::supportedFormats(staging(), 640, 400, Graphics::kHiResTargetRgb565);
		TS_ASSERT(s565.front() == DOS::rgb565());

		Common::List<Graphics::PixelFormat> x888 = DOS::supportedFormats(dosboxX(), 640, 400, Graphics::kHiResTargetRgb888);
		TS_ASSERT(x888.front() == DOS::xrgb8888());
		TS_ASSERT_EQUALS(x888.size(), 2u);

		Common::List<Graphics::PixelFormat> clut = DOS::supportedFormats(dosboxX(), 640, 400, Graphics::kHiResTargetClut8);
		TS_ASSERT_EQUALS(clut.size(), 1u);
		TS_ASSERT(clut.front().isCLUT8());
	}
```
  (If `staging()`'s list has no 640x480 5-6-5 mode either, the expectation is `s565.size() == 1` (CLUT8 only): check the
  list in the file and assert what the modes allow, with a comment naming the mode.)
- [ ] **Step 2: Run to verify it fails.** Run: `make -C /home/thkim/work/scummvm/builds/linux-dos-test -j8 test 2>&1 | grep -m5 'error'`. Expected: no matching `DOS::supportedFormats` overload.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run the tests.** Run: `for d in linux-dos-test linux-dos-test-scumm; do make -C /home/thkim/work/scummvm/builds/$d -j8 test 2>&1 | tail -3; done`. Expected: each only the known `hires_text_ttf_fit.h:493` failure.
- [ ] **Step 5: Build both EXEs** (`build-dos.sh sci`, `build-dos.sh scumm`); both link. `git grep -n dos_truecolor -- backends engines graphics test` prints nothing.
- [ ] **Step 6: Commit** `DOS: render_target caps the screen formats; dos_truecolor removed; hi-res text at 2x only`.

---

### Task 12: AGS adapter

**Files:**
- Modify: `engines/ags/shared/font/hires_font_config.{h,cpp}`, `engines/ags/shared/font/hires_font_plan.cpp`
- Test: `test/engines/ags/hires_font_plan.h` (rewrite)
- Create (build dir, not committed): `/home/thkim/work/scummvm/builds/linux-dos-test-ags`

**Interfaces:**
- Consumes: Tasks 3-5 (`loadMap`, `kHiResKeysAgs`, `readHiResIniFromConfMan`, `compileIdPlan` for the face chain and
  size of each AGS font number, `clampScale`, `blendActive`).
- Produces: `AGS3::HiResFontConfig` keeps its public methods (`alpha()` renamed `blend()` returning
  `Graphics::HiResBlend`; `scale()`), reading `[render] scale/blend/gamma`, `[layout]`, `[fonts]`, `[font]`/`[font.N]`
  `face/size/pixel`, and the ini `hires_text`, `hires_text_map`, `hires_text_face`, `hires_text_size`,
  `hires_text_scale`, `hires_text_blend`. Default scale stays 1 (spec 8). `alpha()` default `true` becomes `blend=auto`
  (blend when the face has coverage and the target is 16/32-bit - the same result).

- [ ] **Step 1: Create the AGS test build**

```bash
mkdir -p /home/thkim/work/scummvm/builds/linux-dos-test-ags && cd /home/thkim/work/scummvm/builds/linux-dos-test-ags && \
  /home/thkim/work/scummvm/dos/configure --disable-all-engines --enable-engine=ags --enable-debug-socket \
  --with-sdl-prefix=/home/thkim/.local/sysroot/usr >/dev/null && make -j8 test 2>&1 | tail -5
```
Expected: builds; record the baseline failure set (the known `hires_text_ttf_fit.h:493` plus nothing else is expected;
if AGS tests fail already at `BASE`, record them as that build's baseline and do not fix them here).
- [ ] **Step 2: Rewrite the failing tests**: every map string to version 2 (`[hires] scale=2` -> `[render] scale=2`,
  `[hires] face=` -> `[font] face=`, `alpha=` -> `blend=`), plus new cases: `hires_text=false` gives no plan;
  `range.basic-latin=X` in an AGS map adds `AGS does not use [font] range.basic-latin` to the warnings; a relative
  `hires_text_face` resolves against the game folder.
- [ ] **Step 3: Run to verify they fail.** Run: `make -C /home/thkim/work/scummvm/builds/linux-dos-test-ags -j8 test 2>&1 | grep -m5 'error'`. Expected: compile errors against the old `HiResTextConfig` API.
- [ ] **Step 4: Implement** the Produces list.
- [ ] **Step 5: Run the tests.** Run: `make -C /home/thkim/work/scummvm/builds/linux-dos-test-ags -j8 test 2>&1 | tail -5` and the same on `linux-dos-test-scumm`. Expected: only the baseline failures recorded in Step 1.
- [ ] **Step 6: Commit** `AGS: Hi-res fonts read the unified map and ini keys`.

---

### Task 13: Delete the old parser and every old key

**Files:**
- Modify: `graphics/hires_text/font_map.{h,cpp}` (delete `HiResTextConfig`, `HiResFontIdSettings`,
  `LegacyFontMapOptions`, `HiResLatinMode`, `HiResMetricsSource`, `HiResFontRole`, `HiResGlyphAction`,
  `HiResGlyphOverride`, `load()`, `loadFromStream()`, `parseRole()`, `getKeyEither()` and every helper only they used),
  `graphics/hires_text/latin_advance.{h,cpp}` (delete `latinAdvanceGamePx`), `graphics/module.mk`
- Delete: `graphics/hires_text/glyph_source_routed.{h,cpp}` (if `git grep RoutedGlyphSource` shows only the ranged one
  after Task 9)
- Test: delete the old `HiResFontMapTestSuite` from `test/graphics/hires_text_font_map.h` (keep `HiResMapTestSuite`);
  rewrite the graphics tests that still use the old types (`git grep -l "HiResTextConfig\|loadFromStream\|HiResMetrics\|HiResLatinMode" test/`:
  expected among `hires_text_glyph_mirror.h`, `hires_text_codepage_kr.h`, `hires_text_text_layout.h`,
  `hires_text_glyph_source.h`, `hires_text_font_baker.h`, `hires_text_latin_advance.h`, `hires_text_glyph_source_file.h`,
  `hires_text_glyph_renderer.h`, `hires_text_bitmap_font.h`, `hires_text_routed*.h`)

**Interfaces:**
- Consumes: Tasks 7-12 finished (nothing uses the old types).
- Produces: nothing new.

- [ ] **Step 1: Delete** the listed code and tests; rewrite the listed tests to the version-2 API. Where a test only
  checked an old alias or an old key's parsing, delete it (no-compat ruling) and say so in the report.
- [ ] **Step 2: Old-key gate**

```bash
cd /home/thkim/work/scummvm/dos && git grep -nE "HiResTextConfig|HiResFontIdSettings|LegacyFontMapOptions|HiResLatinMode|HiResMetricsSource|latinAdvanceGamePx|RoutedGlyphSource\b|loadFromStream\(|korean_alpha_text|korean_hires_scale|korean_ttf_map|hires_text_font\b|hires_text_font_size|hires_text_latin|hires_text_metrics|hires_text_alpha|dos_truecolor|latin_font|latin_face|latin_space" -- engines graphics backends test gui base
```
Expected: no output. Then `git grep -nE "\[(hires|latin|bitmap|encoding)\]" -- engines graphics backends test` shows only
the removed-section table and its tests in `font_map.cpp` / `hires_text_font_map.h`.
- [ ] **Step 3: Run the tests** on `linux-dos-test`, `linux-dos-test-scumm`, `linux-dos-test-ags`. Expected: each at its
  baseline (the one known failure).
- [ ] **Step 4: Build both DOS EXEs** (`build-dos.sh sci`, `build-dos.sh scumm`).
- [ ] **Step 5: Commit** `GRAPHICS: Remove the version 1 hi-res text map parser and its keys`.

---

### Task 14: Rewrite the shipped maps (start from what is committed)

**Files:**
- Modify: `dists/engine-data/hires_text/dos/{CAM,KQ1,LB1,LB2}KO{L,U}.MAP`, `M{1,2}KO{L,U}.MAP` (12),
  `dists/engine-data/hires_text/maps/{korean-default,kq1-ko,mi1-styled,scumm-2x-neodgm,ft-keyed-galmuri9}.map` (5)
- Create: `test/graphics/hires_text_shipped_maps.h`

**Interfaces:**
- Consumes: the version-2 loader and every adapter.
- Produces: version-2 maps that load with zero warnings for their engine.

- [ ] **Step 1: Coordination check.** The SCI package migration to shared 2350 fonts is (or was) in flight by another
  agent. Run:
  ```bash
  cd /home/thkim/work/scummvm/dos && git status --short dists/engine-data/hires_text/ && git log --oneline -5 -- dists/engine-data/hires_text/
  ```
  If any file under `dists/engine-data/hires_text/` has uncommitted changes, **stop** and report to the controller (do
  not convert a file someone else is editing). Otherwise record the last commit sha; convert exactly those committed
  files.
- [ ] **Step 2: Write the failing test** (loads every shipped map with its engine's keys; the map list is read from
  disk, so a new map is covered without editing the test):

```cpp
// test/graphics/hires_text_shipped_maps.h
#include <cxxtest/TestSuite.h>

#include "common/fs.h"
#include "common/str.h"
#include "graphics/hires_text/font_map.h"

class HiResShippedMapsTestSuite : public CxxTest::TestSuite {
	// test/graphics/<this file> -> the source tree
	static Common::FSNode tree() {
		return Common::FSNode(Common::Path(__FILE__, '/').getParent().getParent().getParent());
	}

	static const Graphics::HiResEngineKeys &keysFor(const Common::String &name) {
		// DOS maps: M1*/M2* are SCUMM, the rest SCI. Shared maps: named per engine below.
		if (name.hasPrefixIgnoreCase("M1") || name.hasPrefixIgnoreCase("M2") || name.hasPrefix("mi1-") ||
			name.hasPrefix("scumm-") || name.hasPrefix("ft-") || name == "korean-default.map")
			return Graphics::kHiResKeysScumm;
		return Graphics::kHiResKeysSci;
	}

	void checkDir(const char *rel, const char *suffix) {
		Common::FSNode dir = tree().getChild("dists").getChild("engine-data").getChild("hires_text").getChild(rel);
		if (!dir.isDirectory()) {
			TS_WARN(Common::String::format("%s not found; skipped", rel).c_str());
			return;
		}
		Common::FSList files;
		TS_ASSERT(dir.getChildren(files, Common::FSNode::kListFilesOnly));
		int n = 0;
		for (uint i = 0; i < files.size(); ++i) {
			const Common::String name = files[i].getName();
			if (!name.hasSuffixIgnoreCase(suffix))
				continue;
			++n;
			Common::Array<Common::String> q;
			Graphics::HiResMap m;
			TSM_ASSERT(name.c_str(), Graphics::HiResFontMap::loadMapFile(files[i].getPath(), q, keysFor(name), m));
			for (uint w = 0; w < m.warnings.size(); ++w)
				TS_FAIL((name + ": " + m.warnings[w]).c_str());
		}
		TS_ASSERT(n > 0);
	}

public:
	void test_dos_maps_are_version_2_and_clean() { checkDir("dos", ".MAP"); }
	void test_shared_maps_are_version_2_and_clean() { checkDir("maps", ".map"); }
};
```
  (`korean-default.map` serves SCUMM, SCI and AGS: if a SCUMM-only key in it warns under SCI, split its SCUMM-only part
  into `[shadow:v5]`-style qualified sections or accept the SCUMM key set for it, and say which in the map's comment.)
- [ ] **Step 3: Run to verify it fails** (every map is still version 1).
- [ ] **Step 4: Convert each map** by spec 6.6, by hand, keeping and rewriting its comments (they explain `alpha=` and
  `[latin]`: say what the new keys do instead). Per map:
  - `[map] version=2` first.
  - `[hires] scale/alpha` -> `[render] scale`, `blend=auto` (U) / `blend=off` (L), and `target=rgb888` (U) /
    `target=clut8` (L). SCI U maps previously relied on `rgb_rendering=true` in the ini: `target=rgb888` replaces it.
  - `[hires] face/missing/align/baseline/size/pixel/cell` -> `[font]` (`baseline` -> `shift`).
  - `[latin] mode=proportional` + `metrics=X` -> `range.basic-latin=same` + `advance.basic-latin=X` (SCUMM: the engine
    scope already says `same`; write it anyway - maps are read by people).
  - `[latin] font=same` -> `range.basic-latin=same`; `[latin] baseline=face` -> `origin.basic-latin=face`, and for the
    MI maps also `origin.general-punctuation=face` for the remapped ellipsis (spec 12.2).
  - `[font.N] bitmap=X` -> `[font.N] face=X`; `metrics=` -> `advance.basic-latin=` (SCI; it was Latin-only there) or
    `advance=` (SCUMM).
  - `[encoding] codepage` -> `[text] encoding`; `[glyphs] = keep` -> `= original`; `[glyphs:csN]` -> `[glyphs.N]`.
  - `mi1-styled.map` `latin_font=display_latin` -> `range.basic-latin=display_latin` (+ `range.general-punctuation=
    display_latin, same`).
- [ ] **Step 5: Run the tests.** Run: `make -C /home/thkim/work/scummvm/builds/linux-dos-test-scumm -j8 test 2>&1 | tail -5`. Expected: the shipped-maps suite passes; overall only the known `hires_text_ttf_fit.h:493` failure.
- [ ] **Step 6: Font coverage gate per range.** For every converted DOS map, run the existing bake check the fonts were
  made with (`tools/korean/mkfont.py --require` as `tools/korean/bake-dos-fonts.sh` / `bake-scumm-fonts.sh` call it; see
  their `--require` lines) against each SVF the map names, with the `[glyphs]` targets as required glyphs of their face.
  Expected: no missing glyph. Do not rebake fonts in this task; a missing glyph is reported, not fixed.
- [ ] **Step 7: Commit** `DISTS: Hi-res text maps in the version 2 scheme` - the 17 maps and the test, each named.

---

### Task 15: Generators, harness and package templates

**Files:**
- Modify (dos worktree): `tools/korean/makemaps.py`, `tools/korean/inifix.py`, `tools/korean/fontplan.py`,
  `tools/korean/build-debian.sh`, `tools/korean/fontcheck.sh`, `tools/korean/README.md`, `tools/korean/SCUMM_FONTS.md`
- Modify (harness repo `/home/thkim/work/scummvm`): `harness/dos/m1_accept.py`, `harness/dos/m2_accept.py`,
  `harness/dos/m5_accept.py`, `harness/dos/m5_points.py`, `harness/dos/m5_census.py`, `harness/dos/m5_sheets.py`,
  `harness/dos/scummgame.py`, `harness/dos/release/{repack_sci.py,build_sci_lb2.py,build_scumm.py,README.scumm.tmpl,KOREAN.scumm.tmpl}`;
  delete `harness/dos/release/lb2/LB2KO{L,U}.MAP` if the generator can read `dists/` instead (the LB2 maps moved there)
- Regenerate (not tracked): `/home/thkim/work/scummvm/gamedata/*/hires_text*.map` (16)

**Interfaces:**
- Consumes: Task 14 maps.
- Produces: generators that emit only version-2 maps and new ini keys; acceptance scripts whose ini uses
  `render_target=clut8` where they used `dos_truecolor=off`, and no `rgb_rendering` for the U presets.

- [ ] **Step 1: Coordination check** as in Task 14 Step 1, for `harness/dos/release/` in the harness repo
  (`git -C /home/thkim/work/scummvm status --short harness/dos/release`); stop on uncommitted changes there.
- [ ] **Step 2: Old-key scan (must end empty)**

```bash
cd /home/thkim/work/scummvm && grep -rnE "dos_truecolor|hires_text_alpha|hires_text_font|hires_text_latin|hires_text_metrics|korean_alpha_text|korean_hires_scale|\[hires\]|\[latin\]|\[bitmap\]|bitmap=|alpha=|rgb_rendering" harness/dos dos/tools/korean
```
  Record the hits (the to-do list of this task).
- [ ] **Step 3: Update the generators.** `makemaps.py` writes version-2 maps (spec 3) only. Check it by hand-converting
  one map first (`gamedata/mi2kor/hires_text.map`, spec 6.6) and diffing the generator's output against it: identical
  except comments. `inifix.py`/`fontplan.py`/`build-debian.sh`/`fontcheck.sh` write and read the new ini keys only.
  Regenerate the 16 gamedata maps (untracked: nothing to commit). Gate: every generated file has `[map]` with
  `version=2` and none of the Step 2 patterns (`grep -LE "^version=2" gamedata/*/hires_text*.map` prints nothing). Then
  load one of them in the Linux build (`builds/linux-dos-test-scumm/scummvm` on its game, `-d1`) and check the log has
  no `hires_text.map:` warning. Look at `gamedata/mi2kor/hires_text.map` and `gamedata/indy3kor/hires_text.map` by eye;
  these used the legacy non-per-glyph path and now use per-glyph placement - say so in the report.
- [ ] **Step 4: Update the harness.** `m2_accept.py` "KQ1 off" run: `render_target=clut8` (expected mode 640x400 CLUT8);
  the U runs drop `rgb_rendering=true`. `m5_accept.py` A2 extra run: `mi2ko` with `render_target=clut8`: expected
  `DOS: mode 640x400 CLUT8` and **no** `not available` warning (the old check for SCUMM's "no 32bpp screen available"
  warning goes). Package generators write `SCUMMVM.INI`/`EXAMPLE.INI`/`README.TXT` with the new keys: `[scummvm]
  render_target=auto` with a comment (`clut8` = the old `dos_truecolor=off`), no `rgb_rendering` for U targets.
- [ ] **Step 5: Re-run Step 2's scan.** Expected: no hits except `rgb_rendering` where a harness test deliberately checks
  upstream behaviour (name each remaining hit in the report).
- [ ] **Step 6: Commit** twice: dos worktree `TOOLS: Korean map and ini generators write the unified keys`; harness repo
  `harness/dos: unified hi-res keys (render_target, version 2 maps)`.

---

### Task 16: Docs

**Files:**
- Modify: `graphics/hires_text/README.md` (becomes the single key reference: copy spec sections 3-11 in user terms, with
  the two examples of spec 12 and the "Ranges" section the loader's warnings point to), `engines/scumm/HIRES_TEXT.md`,
  `engines/scumm/HIRES_TEXT_SETUP.md`, `engines/scumm/HIRES_TEXT_DECORATIONS.md`,
  `dists/engine-data/hires_text/fonts/FONTS.md`, `docs/superpowers/specs/2026-09-28-scummvm-dos-sdl3-design.md` (the
  `dos_truecolor` mentions -> `render_target`)

- [ ] **Step 1:** Rewrite. The engine docs keep only engine specifics (the engine scope of spec 8, SCUMM's charset
  numbers, mirror, shadow) and link to `graphics/hires_text/README.md` for every key. Remove "there is no off switch yet"
  (`HIRES_TEXT.md:64-70`).
- [ ] **Step 2: Old-key scan on docs**
  ```bash
  cd /home/thkim/work/scummvm/dos && git grep -nE "dos_truecolor|hires_text_alpha|hires_text_font|hires_text_latin|hires_text_metrics|korean_alpha_text|korean_hires_scale|\[hires\]|\[latin\]|\[bitmap\]|bitmap=" -- '*.md'
  ```
  Expected: hits only in `docs/superpowers/plans/*`, `.superpowers/`, the audit, and the conversion table of the README
  ("old -> new", kept for people converting their own maps).
- [ ] **Step 3: Commit** `DOCS: One hi-res text key reference for SCI, SCUMM, AGS and DOS`.

---

### Task 17: Regression gates

**Files:** none (fix commits only if a gate fails; a fix goes to the task's area with that area's prefix).

- [ ] **Step 1: Rebuild everything**
  ```bash
  for d in linux-dos-test linux-dos-test-scumm linux-dos-noft linux-dos-scumm linux-dos-test-ags; do make -C /home/thkim/work/scummvm/builds/$d -j8 >/dev/null || echo "BUILD FAIL $d"; done
  cd /home/thkim/work/scummvm/dos && backends/platform/dos/build-dos.sh sci >/dev/null && backends/platform/dos/build-dos.sh scumm >/dev/null
  ```
- [ ] **Step 2: Linux unit tests.** `make test` in `linux-dos-test`, `linux-dos-test-scumm`, `linux-dos-test-ags`.
  Expected: each fails exactly `test/graphics/hires_text_ttf_fit.h:493`. Record the counts.
- [ ] **Step 3: SCI regression (10 runs)**
  ```bash
  cd /home/thkim/work/scummvm && for e in x staging; do for s in loading m0 m1 m2 m3; do python3 harness/dos/${s}_accept.py $e; done; done
  ```
  Expected: `LOADING/M0/M1/M2/M3 <emu>: PASS` x 2 (M3: one rerun allowed, both recorded).
- [ ] **Step 4: M5 acceptance**
  ```bash
  cd /home/thkim/work/scummvm && python3 harness/dos/m5_accept.py x --fresh && python3 harness/dos/m5_accept.py staging
  ```
  Expected: `M5 x: PASS`, `M5 staging: PASS` (A2 now with the `render_target=clut8` run).
- [ ] **Step 5: Before/after on Linux references**
  ```bash
  cd /home/thkim/work/scummvm/runs && python3 - <<'PY'
import filecmp, os
base, new = "unify-base", "."
for top in ("dos-m1", "dos-m2", "dos-m5"):
    for root, _, files in os.walk(os.path.join(base, top)):
        rel = os.path.relpath(root, base)
        if "linux" not in rel:
            continue
        for f in files:
            if not f.endswith(".bin"):
                continue
            a, b = os.path.join(root, f), os.path.join(new, rel, f)
            if not os.path.exists(b):
                print("GONE", os.path.join(rel, f))
            elif not filecmp.cmp(a, b, shallow=False):
                print("DIFF", os.path.join(rel, f))
PY
  ```
  Expected: no `DIFF` for SCI (`dos-m1`, `dos-m2`). For `dos-m5` a `DIFF` is allowed only at points that draw
  `0x5e`/`0x5c`/`0x60` in MI1/MI2 (the remapped glyphs now follow the range rules, spec 6.5 step 2) or a Hangul character
  missing from an SVF (now the box, spec 6.4); render each differing `_out` pair to PNG and look at both. Anything else
  is a regression. If `runs/unify-base/DISTS_COMMIT` differs from the current commit of `dists/engine-data/hires_text/dos`,
  list which games' fonts changed and exclude those from this comparison with a note.
- [ ] **Step 6: Controller visual check**: one U and one L window capture per emulator from Step 3 (KQ1 L) and Step 4
  (mi2ko B, mi2kol A): Korean text present, no tofu, no black window.
- [ ] **Step 7: Ledger** - append the counts, PASS lines and any allowed differences to
  `.superpowers/sdd/<this plan's folder>/progress.md` (the controller names the folder).

---

### Task 18: Repack the packages

**Files:** none in the worktree; zips in `/home/thkim/work/scummvm/dist/`.

- [ ] **Step 1:** `harness/dos/release/build_scumm.py` (MI1, MI2), `harness/dos/release/repack_sci.py` (engine, KQ1,
  LB1, Camelot), `harness/dos/release/build_sci_lb2.py` (LB2), invoked as their `--help` and the last run
  (`.superpowers/sdd/2026-09-29-dos-m5-scumm/repack-report.md`) describe, with the Task 17 EXEs. Every zip's
  `SCUMMVM.INI`/`EXAMPLE.INI`/`README.TXT` carry the new keys; every `DATA/*.MAP` is the `dists/` version-2 file.
- [ ] **Step 2:** `unzip -t` each; `harness/dos/release/verify_scumm.sh` on the SCUMM zips; `unzip -p <zip> '*SCUMMVM.INI' | grep -E "dos_truecolor|rgb_rendering|hires_text_alpha"` prints nothing for every zip.
- [ ] **Step 3:** Smoke from the unpacked packages (not `dist/dos`), DOSBox-X: MI2 U (`mi2ko`) difficulty card, LB1 U
  one dialogue line, KQ1 L title. Look at each capture.
- [ ] **Step 4:** Record names, sizes, sha256 in the ledger. Old zips stay.

---

### Task 19 [LATER - planned feature]: SCI RGB565 output

Spec 7.3. Separate from the unification; run only when the controller schedules it.

**Files:** `engines/sci/graphics/` hi-res compositor (the code Task 10 found that blends into 4-byte pixels),
`engines/sci/graphics/drivers/default.cpp`, `engines/sci/graphics/hirestextsettings.cpp` (`formatRequest(...,
engineCanRgb565=true, ...)`), test `test/engines/sci/hiresrender.h`.

- [ ] **Step 1: Failing test**: blending coverage 128 of white text over black into a 5-6-5 pixel gives `0x8410`
  (R 16/31, G 32/63, B 16/31 with round-half-up), coverage 255 gives `0xFFFF`, 0 leaves the pixel.
- [ ] **Step 2-4:** implement a 2-byte sink beside the 4-byte one; `chooseSciRender` no longer demotes `rgb565`;
  tests green.
- [ ] **Step 5:** DOS: LB1 U with `render_target=rgb565` on X (640x400 5-6-5) and Staging (640x480 line repeat); M2-style
  dump comparison against Linux run with the same ini; add the case to `harness/dos/m2_accept.py`.
- [ ] **Step 6: Commit** `SCI: Hi-res text on an RGB565 screen`.

### Task 20 [LATER - planned feature]: palette-matched anti-aliasing on CLUT8 (`blend=on`)

Spec 7.2. Shared quantiser + both engines.

**Files:** create `graphics/hires_text/palette_blend.{h,cpp}` + `test/graphics/hires_text_palette_blend.h`; modify
`Graphics::blendActive` (on + clut8 -> true), SCUMM `HiResIndexSink` path (gfx.cpp ~802), the SCI compositor.

- [ ] **Step 1: Failing tests** for `class HiResPaletteBlender { void setPalette(const byte *rgb, int n); byte blend(byte text, byte under, int level) const; }`:
  level 0 -> `under`, top level -> `text`, a mid level between black (0) and white (15) in a 16-grey palette -> the
  nearest grey; the cache is invalidated by `setPalette`; levels = 4 for 2 bpp, 8 for 8 bpp.
- [ ] **Step 2-4:** implement; wire both engines; remove the "on with clut8" warning.
- [ ] **Step 5:** MI2 L map with `blend=on` on DOS: loops/s in the scrolled room (`progress.md` Task 12 method) stays
  within 10% of `blend=off`; M5 PASS unchanged for the shipped maps (they do not set `on`).
- [ ] **Step 6: Commit** `GRAPHICS: Palette-matched anti-aliasing for hi-res text on paletted screens`.

### Task 21 [GATED - user decision]: sync point 2 with `i18n`

Spec section 13. The new shared set cannot move to `i18n` without the SCI/SCUMM/AGS adapter changes that consume it.
The controller asks the user which way (merge `dos-port` into `i18n`, or port this plan's commits `BASE..<Task 18>` onto
`i18n`), then: carry it out, `cmp` every shared-set file between the two worktrees (all identical), run `make test` in an
`i18n` build with sci+scumm+ags (baseline: the one known failure), and commit `Sync shared hi-res parser with dos-port <sha>`.
