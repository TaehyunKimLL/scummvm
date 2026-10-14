# Per-font Decoration `[shadow.N]` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a hi-res map give each SCUMM charset its own outline/shadow geometry with `[shadow.N]` sections that override `[shadow]` key by key, and have `pairing2svf.py` write them from the measured originals.

**Architecture:** The ten shadow fields of `Graphics::HiResMap` move into a `HiResShadowSpec` with a set-flag per key; `[shadow]` fills `map.shadow`, each `[shadow.N]` fills `map.shadowIds[N]`, and `map.shadowFor(id)` lays the per-id keys over `[shadow]`. `HiResGlyphRenderer::applyMap` takes a spec; SCUMM's `glyphStyle` takes the charset id and resolves with `shadowFor`.

**Tech Stack:** C++11 (ScummVM), cxxtest (`make test`), Python 3 unittest (`tools/korean`).

**Spec:** `docs/superpowers/specs/2026-10-14-per-font-shadow-design.md`

## Global Constraints

- Maps without `[shadow.N]` behave exactly as before, byte for byte (every existing test passes unchanged except mechanical renames).
- `[shadow.N]` takes all eight `[shadow]` keys: `mode offset color width style shadow shadow_color shadow_alpha`, same syntax and limits.
- Precedence: `[shadow.N:q]` > `[shadow.N]` > `[shadow:q]` > `[shadow]` > built-in defaults.
- SCUMM only; SCI/AGI maps with `[shadow.N]` get the same "does not use" warning `[shadow]` gets.
- N is the id `[font.N]`/`[glyphs.N]` use (a SCUMM charset number).
- Work in worktree `~/work/scummvm/johab` (branch `johab`); never `git add -A`; commit trailer:
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` / `Claude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4`.
- Build in `~/work/scummvm/builds/johab-test` (configured from the johab source); do not touch `builds/merge-i18n-test` or the i18n source.

## Review Focus

1. `[shadow.N]` that sets only `mode` must keep `[shadow]`'s `width`/`offset`/`color` (per-key fallback, not whole-section replace) - test in Task 2.
2. A bad value in `[shadow.6]` warns naming `[shadow.6]` (not `[shadow]`) and leaves that key falling back to `[shadow]` - test in Task 2.
3. `[shadow.6:clut8]` with render target clut8 wins over `[shadow.6]`, and `[shadow.6:clut8]` is ignored on another target - test in Task 2.
4. A charset with no `[shadow.N]` (and a map with none at all) draws exactly the old style - test in Task 3.
5. `[shadow.x]` / `[shadow.-1]` (bad id) is an unknown section warning, not a crash - test in Task 2.

---

### Task 0: Build directory for the johab worktree

**Files:** none in git.

- [ ] **Step 1: Configure**

Run:
```bash
mkdir -p ~/work/scummvm/builds/johab-test && cd ~/work/scummvm/builds/johab-test && \
~/work/scummvm/johab/configure --disable-all-engines --enable-engine=sci,scumm,agi \
  --disable-engine=sci32,scumm_7_8,he --enable-debug-socket --with-sdl-prefix=/home/thkim/.local/sysroot/usr
```
Expected: ends with the engine list, no error.

- [ ] **Step 2: Build and run the existing tests once**

Run: `cd ~/work/scummvm/builds/johab-test && make -j16 >/dev/null && make test 2>&1 | tail -1`
Expected: last line ends with `OK!`.

---

### Task 1: `HiResShadowSpec` (mechanical move, no behaviour change)

**Files:**
- Modify: `graphics/hires_text/font_map.h:248-262` (the `[shadow]` fields), `graphics/hires_text/font_map.cpp:598-609` (clear), `:1563-1660` (`buildShadowSection`)
- Modify: `graphics/hires_text/glyph_renderer.h:220`, `graphics/hires_text/glyph_renderer.cpp:114-140` (`applyMap`)
- Modify: `engines/scumm/hires_text.cpp:2462-2470` (`glyphStyle`)
- Test: `test/graphics/hires_text_font_map.h:341,450-456,471-...`, `test/graphics/hires_text_glyph_renderer.h:1936-1960,2074-2087` (renames only)

**Interfaces:**
- Produces: `struct Graphics::HiResShadowSpec` (below); `HiResMap::shadow` (a `HiResShadowSpec`); `HiResGlyphRenderer::applyShadow(GlyphStyle &, const HiResShadowSpec &, int scale)`; `HiResGlyphRenderer::applyMap(style, map, scale)` kept as `applyShadow(style, map.shadow, scale)`.

- [ ] **Step 1: Add the struct to `font_map.h`, above `struct HiResMap`**

```cpp
/**
 * One [shadow] or [shadow.N] section's keys. A key the section sets has its
 * *Set flag; HiResMap::shadowFor() lays a [shadow.N]'s set keys over [shadow].
 */
struct HiResShadowSpec {
	HiResShadowMode mode;
	bool modeSet;
	int offset;                   ///< scaled pixels; -1 = follow the scale
	bool offsetSet;
	byte color;
	bool colorSet;
	int widthQ;                   ///< quarter pixels; -1 = from offset or the scale
	bool widthSet;
	HiResOutlineShape style;      ///< style=round|square|legacy
	bool styleSet;
	bool shiftSet;                ///< shadow=dx,dy (or none) was given
	int dx;                       ///< output pixels
	int dy;
	byte shiftColor;              ///< shadow_color; defaults to color
	bool shiftColorSet;
	byte alpha;                   ///< shadow_alpha as 0..255; 255 = solid
	bool alphaSet;

	HiResShadowSpec() { clear(); }
	void clear() {
		mode = kHiResShadowGame; modeSet = false;
		offset = -1; offsetSet = false;
		color = 0; colorSet = false;
		widthQ = -1; widthSet = false;
		style = kHiResOutlineRound; styleSet = false;
		shiftSet = false; dx = dy = 0;
		shiftColor = 0; shiftColorSet = false;
		alpha = 255; alphaSet = false;
	}
	/// This spec with @p over's set keys in place of its own.
	HiResShadowSpec overlaid(const HiResShadowSpec &over) const;
};
```

- [ ] **Step 2: In `struct HiResMap`, replace the fields at `font_map.h:248-262` with**

```cpp
	// --- [shadow], [shadow.N] ------------------------------------------------
	HiResShadowSpec shadow;                          ///< [shadow]
```

- [ ] **Step 3: `font_map.cpp`: replace the clear() lines 598-609 with `shadow.clear();`, add `overlaid` after `HiResMap::fontIdScope`**

```cpp
HiResShadowSpec HiResShadowSpec::overlaid(const HiResShadowSpec &over) const {
	HiResShadowSpec r = *this;
	if (over.modeSet) { r.mode = over.mode; r.modeSet = true; }
	if (over.offsetSet) { r.offset = over.offset; r.offsetSet = true; }
	if (over.colorSet) { r.color = over.color; r.colorSet = true; }
	if (over.widthSet) { r.widthQ = over.widthQ; r.widthSet = true; }
	if (over.styleSet) { r.style = over.style; r.styleSet = true; }
	if (over.shiftSet) { r.shiftSet = true; r.dx = over.dx; r.dy = over.dy; }
	if (over.shiftColorSet) { r.shiftColor = over.shiftColor; r.shiftColorSet = true; }
	if (over.alphaSet) { r.alpha = over.alpha; r.alphaSet = true; }
	return r;
}
```

- [ ] **Step 4: `buildShadowSection` writes a spec and names its section in warnings**

Change the signature to
`void buildShadowSection(const Common::INIFile &ini, const Common::Array<Common::String> &qualifiers, const Common::String &section, HiResShadowSpec &spec, HiResMap &out)`
and in its body: every `getKey(ini, qualifiers, "shadow", K, value)` becomes `getKey(ini, qualifiers, section, K, value)`; every `out.shadowMode = m` etc. becomes the spec field plus its set flag (`spec.mode = m; spec.modeSet = true;`, `spec.offset = ...; spec.offsetSet = true;`, `spec.color/colorSet`, `spec.widthQ = q; spec.widthSet = true;`, `spec.style = ...; spec.styleSet = true;`, `spec.shiftSet/dx/dy`, `spec.shiftColor/shiftColorSet`, `spec.alpha = ...; spec.alphaSet = true;`); every warning's `"%s: [shadow] ..."` becomes `"%s: [%s] ..."` with `section.c_str()` as the second argument. The call at the end of the build (`buildShadowSection(ini, qualifiers, out);`) becomes `buildShadowSection(ini, qualifiers, "shadow", out.shadow, out);`.

- [ ] **Step 5: `glyph_renderer`: `applyShadow` from a spec**

In `glyph_renderer.h:220` declare:
```cpp
	static void applyShadow(GlyphStyle &style, const HiResShadowSpec &spec, int scale);
	static void applyMap(GlyphStyle &style, const HiResMap &map, int scale) { applyShadow(style, map.shadow, scale); }
```
In `glyph_renderer.cpp` rename the definition `applyMap` to `applyShadow(GlyphStyle &style, const HiResShadowSpec &spec, int scale)` and replace `map.shadowOffset`→`spec.offset`, `map.shadowStyle`→`spec.style`, `map.shadowWidthQ`→`spec.widthQ`, `map.shadowShiftSet`→`spec.shiftSet`, `map.shadowDx`→`spec.dx`, `map.shadowDy`→`spec.dy`, `map.shadowShiftColorSet`→`spec.shiftColorSet`, `map.shadowShiftColor`→`spec.shiftColor`, `map.shadowAlpha`→`spec.alpha`. Add `#include "graphics/hires_text/font_map.h"` to the header if `HiResShadowSpec` is not already visible there.

- [ ] **Step 6: SCUMM `glyphStyle`** (`hires_text.cpp:2467-2468`)

```cpp
	style.shadowColor = map.shadow.colorSet ? map.shadow.color : shadowColor;
	style.shadowMode = resolveShadow(map.shadow.mode, gameShadow, korPatchShadow);
```

- [ ] **Step 7: Rename the test accesses**

`test/graphics/hires_text_font_map.h` and `test/graphics/hires_text_glyph_renderer.h`: `m.shadowMode`→`m.shadow.mode`, `m.shadowOffset`→`m.shadow.offset`, `m.shadowColorSet`→`m.shadow.colorSet`, `m.shadowColor`→`m.shadow.color`, `m.shadowWidthQ`→`m.shadow.widthQ`, `m.shadowStyle`→`m.shadow.style`, `m.shadowShiftSet`→`m.shadow.shiftSet`, `m.shadowDx/Dy`→`m.shadow.dx/dy`, `m.shadowShiftColorSet/ShiftColor`→`m.shadow.shiftColorSet/shiftColor`, `m.shadowAlpha`→`m.shadow.alpha`; the same with `map.` in the glyph-renderer tests. Find them all with:
`grep -n "\.shadow\(Mode\|Offset\|Color\|ColorSet\|WidthQ\|Style\|ShiftSet\|Dx\|Dy\|ShiftColor\|ShiftColorSet\|Alpha\)\b" test/graphics/*.h graphics/hires_text/*.cpp engines/scumm/*.cpp`
(skip `style.` and `deco.`/`d.` - those are `GlyphStyle`/`GlyphDecoration` fields, not the map).

- [ ] **Step 8: Build and test**

Run: `cd ~/work/scummvm/builds/johab-test && make -j16 2>&1 | grep -E " error" ; make test 2>&1 | tail -1`
Expected: no errors; `OK!`.

- [ ] **Step 9: Commit**

```bash
cd ~/work/scummvm/johab
git add graphics/hires_text/font_map.h graphics/hires_text/font_map.cpp graphics/hires_text/glyph_renderer.h \
  graphics/hires_text/glyph_renderer.cpp engines/scumm/hires_text.cpp test/graphics/hires_text_font_map.h \
  test/graphics/hires_text_glyph_renderer.h
git commit -m "GRAPHICS: Gather the [shadow] keys in a HiResShadowSpec" -m "..." 
```
(message body: the fields move, a set flag per key, no behaviour change; trailer lines.)

---

### Task 2: Parse `[shadow.N]`

**Files:**
- Modify: `graphics/hires_text/font_map.h` (`HiResMap`), `graphics/hires_text/font_map.cpp:680-700` (section families), `:781-790` (classifier), `:980-990` (key classification), end of the build (after `buildShadowSection(... "shadow" ...)`), `clear()`
- Test: `test/graphics/hires_text_font_map.h`

**Interfaces:**
- Consumes: `HiResShadowSpec`, `buildShadowSection(ini, qualifiers, section, spec, out)` from Task 1.
- Produces: `HiResMap::shadowIds` (`Common::HashMap<int, HiResShadowSpec>`); `HiResShadowSpec HiResMap::shadowFor(int id) const`.

- [ ] **Step 1: Write the failing tests** (append to the suite in `test/graphics/hires_text_font_map.h`, using its existing `load(text, m)` helper and `hasWarning(m, text)`)

```cpp
	void test_shadow_id_overrides_key_by_key() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n[shadow]\nmode=outline\nwidth=1.5\ncolor=3\n"
					   "[shadow.6]\nmode=drop\noffset=4\n", m));
		const Graphics::HiResShadowSpec s6 = m.shadowFor(6);
		TS_ASSERT_EQUALS(s6.mode, Graphics::kHiResShadowDrop);
		TS_ASSERT_EQUALS(s6.offset, 4);
		TS_ASSERT_EQUALS(s6.widthQ, 6);              // [shadow]'s 1.5 px
		TS_ASSERT(s6.colorSet);
		TS_ASSERT_EQUALS(s6.color, 3);
		const Graphics::HiResShadowSpec s2 = m.shadowFor(2);   // no [shadow.2]
		TS_ASSERT_EQUALS(s2.mode, Graphics::kHiResShadowOutline);
		TS_ASSERT_EQUALS(s2.offset, -1);
	}

	void test_shadow_id_takes_every_key() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n[shadow.3]\nmode=stroke\noffset=2\ncolor=8\nwidth=3\nstyle=square\n"
					   "shadow=-2,2\nshadow_color=1\nshadow_alpha=50\n", m));
		TS_ASSERT(m.warnings.empty());
		const Graphics::HiResShadowSpec s = m.shadowFor(3);
		TS_ASSERT_EQUALS(s.mode, Graphics::kHiResShadowStroke);
		TS_ASSERT_EQUALS(s.offset, 2);
		TS_ASSERT_EQUALS(s.color, 8);
		TS_ASSERT_EQUALS(s.widthQ, 12);
		TS_ASSERT_EQUALS(s.style, Graphics::kHiResOutlineSquare);
		TS_ASSERT(s.shiftSet);
		TS_ASSERT_EQUALS(s.dx, -2);
		TS_ASSERT_EQUALS(s.dy, 2);
		TS_ASSERT_EQUALS(s.shiftColor, 1);
		TS_ASSERT_EQUALS(s.alpha, 128);
	}

	void test_shadow_id_bad_value_names_its_section_and_falls_back() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n[shadow]\nwidth=2\n[shadow.6]\nwidth=lots\n", m));
		TS_ASSERT(hasWarning(m, "[shadow.6] width 'lots' is invalid"));
		TS_ASSERT_EQUALS(m.shadowFor(6).widthQ, 8);
	}

	void test_shadow_id_qualifier_precedence() {
		const char *text = "[map]\nversion=2\n[shadow]\noffset=1\n[shadow:clut8]\noffset=2\n"
						   "[shadow.6]\noffset=3\n[shadow.6:clut8]\noffset=4\n";
		Graphics::HiResMap clut8, rgb;
		TS_ASSERT(load(text, clut8, Graphics::kHiResKeysScumm, "clut8"));
		TS_ASSERT_EQUALS(clut8.shadowFor(6).offset, 4);
		TS_ASSERT_EQUALS(clut8.shadowFor(1).offset, 2);
		TS_ASSERT(load(text, rgb, Graphics::kHiResKeysScumm, "rgb888"));
		TS_ASSERT_EQUALS(rgb.shadowFor(6).offset, 3);
		TS_ASSERT_EQUALS(rgb.shadowFor(1).offset, 1);
	}

	void test_shadow_bad_id_is_an_unknown_section() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n[shadow.x]\nmode=drop\n", m));
		TS_ASSERT(m.shadowIds.empty());
		TS_ASSERT(!m.warnings.empty());
	}

	void test_sci_warns_on_shadow_id() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n[shadow.4]\nmode=outline\n", m, Graphics::kHiResKeysSci));
		TS_ASSERT(hasWarning(m, "SCI does not use [shadow.4] mode"));
	}
```
Before writing, read the suite's `load` overloads (around line 300-340) and use the exact parameter order they take for engine keys and target; adjust the two qualified calls in `test_shadow_id_qualifier_precedence` to match (the existing test at line 331 shows the target form: `load(text, m, Graphics::kHiResKeysSci, "pc")`).

- [ ] **Step 2: Run them to see them fail**

Run: `cd ~/work/scummvm/builds/johab-test && make test 2>&1 | grep -A3 "shadow_id\|shadow_bad_id" | head -20`
Expected: compile error (`shadowFor`/`shadowIds` not members) or assertion failures.

- [ ] **Step 3: `HiResMap` gains the per-id table** (`font_map.h`, after `HiResShadowSpec shadow;`)

```cpp
	Common::HashMap<int, HiResShadowSpec> shadowIds;  ///< [shadow.N]

	/// [shadow] with [shadow.@p id]'s set keys laid over it.
	HiResShadowSpec shadowFor(int id) const;
```
and in `font_map.cpp`: `shadowIds.clear();` in `clear()`, and

```cpp
HiResShadowSpec HiResMap::shadowFor(int id) const {
	Common::HashMap<int, HiResShadowSpec>::const_iterator it = shadowIds.find(id);
	return it == shadowIds.end() ? shadow : shadow.overlaid(it->_value);
}
```

- [ ] **Step 4: Classify `shadow.N`**

Add `kHSecShadowId` after `kHSecShadow` in the family enum (`font_map.cpp:~690`), update the `id` comment to include it, and in the classifier next to the `glyphs.` block (`~781`):

```cpp
	if (info.base.hasPrefixIgnoreCase("shadow.")) {
		int id;
		if (parseSectionId(Common::String(info.base.c_str() + 7), id)) {
			info.family = kHSecShadowId;
			info.id = id;
			return;
		}
		info.family = kHSecUnknown;
		return;
	}
```
In the key-classification switch (`~987`) make `case kHSecShadow:` also cover `case kHSecShadowId:`.

- [ ] **Step 5: Build the per-id specs** (after `buildShadowSection(ini, qualifiers, "shadow", out.shadow, out);`)

```cpp
	Common::Array<int> shadowIds;
	collectRelevantIds(ini, qualifiers, "shadow.", shadowIds);
	for (uint i = 0; i < shadowIds.size(); ++i) {
		HiResShadowSpec spec;
		buildShadowSection(ini, qualifiers, Common::String::format("shadow.%d", shadowIds[i]), spec, out);
		out.shadowIds[shadowIds[i]] = spec;
	}
```

- [ ] **Step 6: Run the tests**

Run: `cd ~/work/scummvm/builds/johab-test && make -j16 2>&1 | grep -E " error"; make test 2>&1 | tail -1`
Expected: `OK!`. If `test_shadow_bad_id_is_an_unknown_section` fails because no warning is emitted for `[shadow.x]`, check how `[glyphs.x]` (kHSecUnknown) is reported and assert that same wording instead.

- [ ] **Step 7: Commit**

```bash
git add graphics/hires_text/font_map.h graphics/hires_text/font_map.cpp test/graphics/hires_text_font_map.h
git commit -m "GRAPHICS: Read [shadow.N], a charset's own decoration over [shadow]"
```
(body: per-key overlay, qualifier order, warnings name the section; trailer.)

---

### Task 3: SCUMM draws each charset with its own spec

**Files:**
- Modify: `engines/scumm/hires_text.h:656` (`glyphStyle`), `:1146` (`drawRows`), `engines/scumm/hires_text.cpp:1170-1200` (`drawRows`), `:1297`, `:1356` (its two callers), `:2462-2470` (`glyphStyle`)
- Test: `test/engines/scumm/hires_shadow_rule.h`

**Interfaces:**
- Consumes: `HiResMap::shadowFor(int)`, `HiResGlyphRenderer::applyShadow(style, spec, scale)`.
- Produces: `static GlyphStyle ScummHiResText::glyphStyle(const HiResMap &map, int charsetId, int scale, int gameShadow, bool korPatchShadow, byte color, byte shadowColor);`

- [ ] **Step 1: Failing tests** (append to `HiResShadowRuleTestSuite`)

```cpp
	void test_each_charset_takes_its_own_shadow_section() {
		Graphics::HiResMap map;
		map.shadow.mode = Graphics::kHiResShadowOutline;
		map.shadow.modeSet = true;
		Graphics::HiResShadowSpec cs3;
		cs3.widthQ = 12; cs3.widthSet = true;            // 3 px
		map.shadowIds[3] = cs3;
		Graphics::HiResShadowSpec cs6;
		cs6.mode = Graphics::kHiResShadowDrop; cs6.modeSet = true;
		cs6.offset = 4; cs6.offsetSet = true;
		map.shadowIds[6] = cs6;

		const Graphics::GlyphStyle s3 = Scumm::ScummHiResText::glyphStyle(map, 3, 2, 0, false, 15, 0);
		TS_ASSERT_EQUALS(s3.shadowMode, Graphics::kHiResShadowOutline);
		TS_ASSERT_EQUALS(s3.outlineQ, 12);
		const Graphics::GlyphStyle s6 = Scumm::ScummHiResText::glyphStyle(map, 6, 2, 0, false, 15, 0);
		TS_ASSERT_EQUALS(s6.shadowMode, Graphics::kHiResShadowDrop);
		TS_ASSERT_EQUALS(s6.shadowOffset, 4);
		const Graphics::GlyphStyle s1 = Scumm::ScummHiResText::glyphStyle(map, 1, 2, 0, false, 15, 0);
		TS_ASSERT_EQUALS(s1.outlineQ, 6);                // the scale's 0.75 game px
		TS_ASSERT_EQUALS(s1.shadowOffset, 1);
	}

	void test_a_map_without_shadow_ids_draws_as_before() {
		Graphics::HiResMap map;
		for (int id = 0; id < 9; ++id) {
			const Graphics::GlyphStyle a = Scumm::ScummHiResText::glyphStyle(map, id, 2, 0, true, 15, 0);
			Graphics::GlyphStyle b;
			b.color = 15;
			b.shadowColor = 0;
			b.shadowMode = Scumm::ScummHiResText::resolveShadow(map.shadow.mode, 0, true);
			Graphics::HiResGlyphRenderer::applyShadow(b, map.shadow, 2);
			TS_ASSERT_EQUALS(a.shadowMode, b.shadowMode);
			TS_ASSERT_EQUALS(a.outlineQ, b.outlineQ);
			TS_ASSERT_EQUALS(a.shadowOffset, b.shadowOffset);
		}
	}
```
Also update the existing `drawn()` helper in the same file to the new signature: `glyphStyle(map, 0, scale, shadow, korPatch, 15, 0)`.

- [ ] **Step 2: Run to see them fail**

Run: `cd ~/work/scummvm/builds/johab-test && make test 2>&1 | grep -m3 "error\|glyphStyle"`
Expected: compile error on the new `glyphStyle` argument.

- [ ] **Step 3: `glyphStyle` with the charset id** (`hires_text.h:656`, `hires_text.cpp:2462`)

```cpp
Graphics::GlyphStyle ScummHiResText::glyphStyle(const Graphics::HiResMap &map, int charsetId, int scale,
												 int gameShadow, bool korPatchShadow,
												 byte color, byte shadowColor) {
	const Graphics::HiResShadowSpec spec = map.shadowFor(charsetId);
	Graphics::GlyphStyle style;
	style.color = color;
	style.shadowColor = spec.colorSet ? spec.color : shadowColor;
	style.shadowMode = resolveShadow(spec.mode, gameShadow, korPatchShadow);
	Graphics::HiResGlyphRenderer::applyShadow(style, spec, MAX(1, scale));
	return style;
}
```
Update its doc comment: "its geometry from @p map's [shadow] keys, with [shadow.@p charsetId]'s over them".

- [ ] **Step 4: Pass the charset id down**

`drawRows` (`hires_text.h:1146`, `hires_text.cpp:1170`) gains `int charsetId` right after `uint32 cp, int width,`; its `glyphStyle(_map, _scale, ...)` call (`:1200`) becomes `glyphStyle(_map, charsetId, _scale, gameShadow, _korPatchShadow, color, shadowColor)`. Its two callers pass the charset id they already have: `drawGlyphPlaced(..., int chr, int charsetId, ...)` at `:1297` and `drawChar(..., int chr, int charsetId, ...)` at `:1356` - insert `charsetId` after `width` in each `drawRows(...)` call.

- [ ] **Step 5: Build and test**

Run: `cd ~/work/scummvm/builds/johab-test && make -j16 2>&1 | grep -E " error"; make test 2>&1 | tail -1`
Expected: `OK!`.

- [ ] **Step 6: Commit**

```bash
git add engines/scumm/hires_text.h engines/scumm/hires_text.cpp test/engines/scumm/hires_shadow_rule.h
git commit -m "SCUMM: Decorate each charset with its own [shadow.N]"
```

---

### Task 4: `pairing2svf.py` writes `[shadow.N]` from the measurements

**Files:**
- Modify: `tools/korean/pairing2svf.py` (`make_map`, `main`)
- Create: `tools/korean/scumm-fonts/original-decoration.json`
- Test: `tools/korean/test_pairing2svf.py`

**Interfaces:**
- Consumes: the selection JSON (`games[].fonts[]` with `id`, `lineh`, `korean.h`).
- Produces: `make_map(..., decorations=None)`; `decorations` maps a page game name to `{font id: {"o": reach} | {"d": [dx, dy]}}`.

- [ ] **Step 1: The measured table** (`tools/korean/scumm-fonts/original-decoration.json`; values from `runs/fontmeasure/vision/shadow-measure.md`)

```json
{
 "MI1 EGA/VGA floppy": {"cs2": {"o": 1}, "cs4": {"o": 1}},
 "MI1 CD": {"cs2": {"o": 1}, "cs4": {"o": 1}, "cs6": {"d": [1, 1]}},
 "MI2 DOS VGA": {"cs2": {"o": 1}, "cs4": {"o": 1}, "cs6": {"d": [1, 1]}, "cs7": {"o": 2}, "cs8": {"o": 1}},
 "Indy4 DOS CD": {"cs2": {"o": 1}, "cs3": {"o": 2}, "cs4": {"o": 2}},
 "Indy4 DOS floppy": {"cs2": {"o": 1}, "cs3": {"o": 2}, "cs4": {"o": 2}},
 "Loom CD (VGA talkie)": {"cs2": {"o": 1}, "cs4": {"o": 1}},
 "DOTT DOS floppy": {"cs2": {"o": 1}, "cs3": {"d": [1, 1]}, "cs5": {"d": [1, 1]}, "cs6": {"d": [2, 2]}}
}
```

- [ ] **Step 2: Failing test** (append to `MapTest` in `test_pairing2svf.py`)

```python
    def test_measured_fonts_get_their_shadow_section(self):
        fonts = [{"id": "cs3", "lineh": 15, "korean": {"key": "a", "label": "a", "bpp": 2, "h": 30}},
                 {"id": "cs6", "lineh": 28, "korean": {"key": "b", "label": "b", "bpp": 2, "h": 56}},
                 {"id": "cs1", "lineh": 8, "korean": {"key": "a", "label": "a", "bpp": 2, "h": 16}}]
        deco = {"cs3": {"o": 2}, "cs6": {"d": [2, 2]}}
        text = p2s.make_map("[map]\n[shadow]\nmode=game\n[font]\nface=ui\n", "G", fonts,
                            {"a": "kp1", "b": "kp2"}, "auto", decorations=deco)
        s = {n: l for n, l in p2s.parse_sections(text) if n}
        self.assertIn("width=3", s["shadow.3"])          # 0.75 x 2 x 2 x (30/30)
        self.assertIn("offset=4", s["shadow.6"])         # 2 x 2 x (56/56)
        self.assertNotIn("shadow.1", s)
        self.assertIn("mode=game", s["shadow"])
```

- [ ] **Step 3: Run it to see it fail**

Run: `cd ~/work/scummvm/johab/tools/korean && python3 -I test_pairing2svf.py 2>&1 | tail -3`
Expected: `TypeError: make_map() got an unexpected keyword argument 'decorations'`.

- [ ] **Step 4: Implement** (in `pairing2svf.py`)

Add to `make_map`'s signature `decorations=None` and, before building `fonts_sec`:

```python
    # A charset whose original face has a measured outline or drop gets it
    # back, grown to the Korean face: [shadow.N] over the map's [shadow].
    shadow_secs = []
    for f in fonts:
        m = (decorations or {}).get(f["id"])
        n = font_section(f["id"])
        if not m or not n:
            continue
        ratio = f["korean"].get("h", 0) / (2 * f.get("lineh", 0)) if f.get("lineh") else 1.0
        lines = [f"[shadow.{n.split('.')[1]}]"]
        if "o" in m:
            width = round(0.75 * 2 * m["o"] * ratio * 4) / 4
            lines.append(f"width={width:g}")
        else:
            lines.append(f"offset={max(1, round(2 * max(m['d']) * ratio))}")
        shadow_secs.append(lines)
```
and append `for lines in shadow_secs: out += lines` after the template sections are written (before the final join). In `main`, load
`tools/korean/scumm-fonts/original-decoration.json` (next to the script: `os.path.join(os.path.dirname(os.path.abspath(__file__)), "scumm-fonts", "original-decoration.json")`) and pass `decorations=table.get(g["game"])` to `make_map`. Keep the existing tests' calls working (the keyword defaults to `None`).

- [ ] **Step 5: Run the tests**

Run: `cd ~/work/scummvm/johab/tools/korean && python3 -I test_pairing2svf.py 2>&1 | tail -3`
Expected: `OK`.

- [ ] **Step 6: Commit**

```bash
git add tools/korean/pairing2svf.py tools/korean/test_pairing2svf.py tools/korean/scumm-fonts/original-decoration.json
git commit -m "TOOLS: Give a paired charset its original's decoration size"
```

---

### Task 5: Document and check in the games

**Files:**
- Modify: `engines/scumm/HIRES_TEXT_DECORATIONS.md` (the "Map keys" section), `graphics/hires_text/README.md` (the section list)

- [ ] **Step 1: Docs**

In `HIRES_TEXT_DECORATIONS.md` after the `[shadow]` key block add a `### Per charset: [shadow.N]` subsection: what N is, the per-key fallback, the qualifier order, the measured table (copy the spec's table), and the Indy4/DOTT example map. In `graphics/hires_text/README.md` add `[shadow.N]` wherever `[shadow]` and `[glyphs.N]` are listed as sections.

- [ ] **Step 2: In-game check**

Generate a map with `pairing2svf.py` from a selection holding Indy4 CD cs3/cs4 and DOTT cs6 (use the page's export or a hand-written selection like `runs/pairing-out/sample-selection.json`), copy the map and SVFs into the game folders under `/tmp/claude-1000/-home-thkim-work/36a78c74-674a-4d24-9ca3-7236f6e2ebde/scratchpad/` (Indy4: unpack `~/work/scummvm/dist/scummvm-game-indy4-*`; DOTT: `zdott`), run each with `~/work/scummvm/builds/johab-test/scummvm` under Xvfb `:78` (`runs/loomfade/gup.sh` with `BIN=`), and take a screenshot where that charset's Korean text is on screen (Indy4 credits for cs3; DOTT title for cs6). Compare with the page preview at "원본 비례". One emulator at a time; kill by PID.

- [ ] **Step 3: Commit**

```bash
git add engines/scumm/HIRES_TEXT_DECORATIONS.md graphics/hires_text/README.md
git commit -m "DOCS: Describe [shadow.N]"
```
