# Map-Declared Game Languages Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Prerequisite: hires-config-unify Task 18 complete** (`docs/superpowers/plans/2026-09-30-hires-config-unify.md`: the
version-2 loader, both engine adapters on it, `HIRESTXT.MAP` as the default map, the unified ini keys, the repacked
packages). Tasks 19-21 of that plan are independent of this one.

**Goal:** A game folder holds its original release plus translations; the game map's `[translation.<lang>]` sections add
those languages to the target's Language setting, and choosing one loads its resources, encoding and language map, for
SCI and SCUMM, with the Korean overlay detection entries and `.trs`-by-name discovery removed.

**Architecture:** A shared reader (`graphics/hires_text/map_languages`) turns a game map's `[translation.*]` sections
into validated `HiResTranslation` records and chooses the text language from `language=`. Detection and every engine
start write the declared languages into the target's `guioptions` (the channel the launcher's popup already reads); a
one-line Advanced Detector hook stops `language=` from filtering out the original's entry. Each engine then loads the
chosen section's files explicitly (no `SearchMan` mounting) and takes its encoding from the section.

**Tech Stack:** C++ (ScummVM, C++11 subset), CxxTest (`make test`), Python 3 (tools, harness), DJGPP 12.2 + SDL3 for
DOS, DOSBox-X 2026.08 and DOSBox Staging 0.83 through `~/work/scummvm/harness/dos/*.py`.

**Spec:** `docs/superpowers/specs/2026-09-30-map-languages-design.md` (section numbers below refer to it; `U<n>` refers to
`docs/superpowers/specs/2026-09-30-hires-config-unify-design.md`). Every key, rule and warning text is defined there.

## Global Constraints

- Branch `dos-port`, worktree `/home/thkim/work/scummvm/dos`. Harness repo `/home/thkim/work/scummvm` (branch `master`).
- **Another agent may commit in the same worktree.** `git add <named files>` / `git commit -- <paths>` only; never
  `git add -A`, `git add .`, `git commit -a`, or bare `git stash`. If `.git/index.lock` exists, wait and retry. Never edit,
  add or revert a file your task does not name.
- Every commit message ends with:
  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4
  ```
  Command form: `git add <each new or changed file>` then
  `git commit -m "<subject>" -m "<body, optional>" -m "<the two trailer lines>" -- <the same paths>` (`git rm` for
  deletions). Prefixes: `GRAPHICS:`, `TEST:`, `SCUMM:`, `SCI:`, `ENGINES:`, `GUI:`, `DISTS:`, `TOOLS:`, `DOCS:`; harness
  commits `harness/dos: ...`. Do not push.
- **No backward compatibility** (spec 0.4): the removed detection entries, `.trs` names, `sci-<lang>.str` naming,
  `Text.MAP` overlay and `ADGF_UTF8I18N` are deleted, not aliased or deprecated.
- No DOS `#ifdef` in `engines/`, `graphics/`, `common/`, `gui/`.
- Shared set (U13) gains `graphics/hires_text/map_languages.{h,cpp}` and `test/graphics/hires_text_map_languages.h`;
  only `dos-port` edits it; the `i18n` sync stays gated (unify Task 21).
- Linux unit tests. Build directories (out of tree, configured from this worktree):
  - `/home/thkim/work/scummvm/builds/linux-dos-test-scumm` (SCI + SCUMM) - the main one for every task.
  - `/home/thkim/work/scummvm/builds/linux-dos-test` (SCI only), `/home/thkim/work/scummvm/builds/linux-dos-test-ags` (AGS).
  Command: `make -C <dir> -j8 test 2>&1 | tee /tmp/claude-1000/-home-thkim-work/36a78c74-674a-4d24-9ca3-7236f6e2ebde/scratchpad/test-<dir>.log | tail -5`.
  **Known baseline: exactly one failing test, `test/graphics/hires_text_ttf_fit.h:493`
  (`test_pad_rows_moves_the_glyph_down_whole`)**, unless the unify ledger recorded a different baseline at its Task 17 -
  then that one. Any other failure is a regression. Record the test count in every report.
- Linux game runs: `/home/thkim/work/scummvm/builds/linux-dos-test-scumm/scummvm` (the binary the build dir makes).
- DOS builds: `cd /home/thkim/work/scummvm/dos && backends/platform/dos/build-dos.sh sci` (stages
  `/home/thkim/work/scummvm/dist/dos/SCUMMVM.EXE` + `DATA/`) and `... build-dos.sh scumm` (`SCUMM.EXE`).
- Acceptance (harness, from `/home/thkim/work/scummvm`): `python3 harness/dos/<s>_accept.py x|staging` for `s` in
  `loading m0 m1 m2 m3 m5`. Baseline: every one prints `... <emu>: PASS` (M3's secondary music window is a known wobble:
  on a FAIL rerun once and record both). One DOS run at a time (debug port 5555); check with the controller before
  `build-dos.sh` or any `*_accept.py` run.
- Maps, fonts and packages: start from what is committed / built when your task runs, never from a copy in this plan.
- Tests are written first and must fail (compile error or assertion) before the implementation; show that in the report.
- Every warning text the spec quotes is matched exactly by a test (tests read `warnings` arrays, never stdout).

## File Structure

| File | Responsibility | Task |
|---|---|---|
| `graphics/hires_text/map_languages.{h,cpp}` (new) | read `[translation.*]`, validate, resolve paths, choose the text language, language tags | 1 |
| `graphics/hires_text/font_map.{h,cpp}` | translation section family; language-map role warnings; `kHiResKeyTranslation` | 1 |
| `engines/advancedDetector.{h,cpp}` | `languageForMatching()` hook in `identifyGame()` | 2 |
| `engines/sci/detection.cpp`, `detection_internal.{h,cpp}`, `detection_tables.h`, `detection.h`, `metaengine.cpp` | SCI detection: overlay entries removed, tags, hook, `translationUsable()` | 2 |
| `engines/sci/sci.{h,cpp}`, `textencoding.{h,cpp}`, `engine/translation.{h,cpp}`, `engine/text_overlay.*` (deleted) | SCI language choice and encoding | 3 |
| `engines/sci/resource/resource.{h,cpp}`, `resource_intern.h`, `graphics/cache.cpp` | SCI translation sources, `dir` patches, `.uni` lookup, map choice | 4 |
| `engines/scumm/translation_rules.h` (new), `detection.cpp`, `detection_internal.h`, `trs_bundle.h`, `metaengine.cpp` | SCUMM detection, tags, choice at `createInstance()` | 5 |
| `engines/scumm/scumm.{h,cpp}`, `string.cpp`, `charset.cpp`, `hires_text.{h,cpp}`, `text_utf8.{h,cpp}` | SCUMM loading from the section | 6 |
| `engines/metaengine.h`, `gui/languagelabel.{h,cpp}` (new), `gui/editgamedialog.cpp`, `gui/module.mk`, `engines/{sci,scumm}/metaengine.cpp` | popup label from `name=` | 7 |
| `dists/engine-data/hires_text/dos/*.MAP`, `dists/engine-data/hires_text/dos/games/*/HIRESTX{T,L}.MAP` (new), `test/graphics/hires_text_shipped_maps.h`, `tools/korean/*` | maps and generators | 8 |
| harness `harness/dos/langdirs.py` (new), `m1_accept.py`, `m2_accept.py`, `m5_accept.py`, `scummgame.py` | acceptance on the one-folder layout | 9 |
| `graphics/hires_text/README.md`, `tools/korean/TRANSLATION_PIPELINE.md`, `engines/scumm/HIRES_TEXT_SETUP.md` | docs | 10 |
| harness `harness/dos/release/{build_sci_lb2,repack_sci,build_scumm}.py`, templates | packages | 12 |

---

### Task 0: Coordination, baseline references and the overlay-entry table

**Files:** none in the worktree. Create (not committed): `/home/thkim/work/scummvm/runs/langs-base/`.

**Interfaces:**
- Consumes: the unify plan's end state.
- Produces: `BASE` (the `dos-port` sha now, written into the ledger); `runs/langs-base/{dos-m1,dos-m2,dos-m5}` (copies of
  the acceptance dumps made now, old layout); `runs/langs-base/ko-entries.tsv` (the table Task 2 removes by);
  `runs/langs-base/lb2-ko.log` (Task 4's LB2 comparison).

- [ ] **Step 1: Check the prerequisite.**
  ```bash
  cd /home/thkim/work/scummvm/dos && git log --oneline -1 && grep -c "kHSecReserved" graphics/hires_text/font_map.cpp && ls /home/thkim/work/scummvm/dist/scummvm-dos-*.zip | tail -8
  ```
  Expected: the unify ledger (`.superpowers/sdd/<its folder>/progress.md`, the controller names it) records Task 18
  done; `font_map.cpp` still has the reserved family; six fresh zips (LB1, LB2, KQ1, Camelot, MI1, MI2) from unify Task 18.
  Otherwise stop and report.
- [ ] **Step 2: Acceptance dumps, old layout.** Run (controller's permission first):
  ```bash
  cd /home/thkim/work/scummvm && for s in m1 m2; do python3 harness/dos/${s}_accept.py x; done && python3 harness/dos/m5_accept.py x --fresh
  mkdir -p runs/langs-base && cp -a runs/dos-m1 runs/dos-m2 runs/dos-m5 runs/langs-base/
  ```
  Expected: `M1 x: PASS`, `M2 x: PASS`, `M5 x: PASS`.
- [ ] **Step 3: LB2 reference log.** Unpack the unify Task 18 LB2 zip to `runs/langs-base/lb2pkg` and run its `lb2ko`
  target on Linux for 60 s with resource logging:
  ```bash
  cd /home/thkim/work/scummvm/runs/langs-base && unzip -q ../../dist/scummvm-dos-sci-lb2-*-f2350.zip -d lb2pkg
  cd lb2pkg/SCUMMVM && timeout 60 /home/thkim/work/scummvm/builds/linux-dos-test-scumm/scummvm -c SCUMMVM.INI -d1 lb2ko > ../../lb2-ko.log 2>&1; grep -c "message.map\|RESOURCE.MSG" ../../lb2-ko.log
  ```
  Record the count (a non-zero number: the overlay is read).
- [ ] **Step 4: Overlay-entry table.** For every `Common::KO_KOR` entry in `engines/sci/detection_tables.h`, find the
  English entry of the same `gameid` whose `resource.map`/`resource.00N`/`resmap`/`ressci` md5+size are identical. Write
  `runs/langs-base/ko-entries.tsv` with columns `line gameid kind english_line` where `kind` is `overlay` (all non-overlay
  files equal an English entry's; the extra files are only `resource.msg`, `message.map`, `text.NNN`) or `original` (a
  map or volume differs from every English entry). A Python one-off reading the file is fine (not committed).
  Expected per spec 8: overlay = Castle of Dr. Brain, EcoQuest, GK1 (2), GK2, KQ1 (2), KQ5, KQ6 (3), LB2, SQ4; original =
  EcoQuest 2, LB1 (fork). Report any difference to the controller before Task 2.
- [ ] **Step 5: Ledger.** Append `BASE`, the PASS lines, the LB2 count and the table to the ledger. No commit.

---

### Task 1: The shared language manifest reader

**Files:**
- Create: `graphics/hires_text/map_languages.h`, `graphics/hires_text/map_languages.cpp`,
  `test/graphics/hires_text_map_languages.h`
- Modify: `graphics/module.mk` (add `hires_text/map_languages.o`), `graphics/hires_text/font_map.h`,
  `graphics/hires_text/font_map.cpp` (section family, `kHiResKeyTranslation`, language-map role),
  `test/graphics/hires_text_font_map.h` (the reserved-section test becomes the three tests below)

**Interfaces:**
- Consumes: `HiResFontMap::resolvePath()`, `HiResFontMap::parseCodePage()`, `HiResFontMap::loadMap()`/`loadMapFile()`,
  `HiResEngineKeys`, `HiResIniOverrides`, `FontFileExistsFn`/`fontFileExists` (`font_face.h`).
- Produces (namespace `Graphics`, `map_languages.h`):
  ```cpp
  enum HiResTranslationKey { kHiResTrTrs = 1 << 0, kHiResTrMessages = 1 << 1, kHiResTrStrings = 1 << 2, kHiResTrDir = 1 << 3 };
  struct HiResTranslationRules {
  	const char *engine;     // "SCI", "SCUMM"
  	uint32 keys;            // kHiResTr* this engine reads
  	uint32 needOneOf;       // at least one of these must be set
  	const char *needText;   // "SCUMM needs trs=" / "SCI needs messages=, strings= or dir="
  };
  extern const HiResTranslationRules kHiResTranslationSci;    // keys = Messages|Strings|Dir, needOneOf = same
  extern const HiResTranslationRules kHiResTranslationScumm;  // keys = Trs|Dir, needOneOf = Trs
  struct HiResTranslation {
  	HiResTranslation();
  	Common::String code;          // lower-cased section suffix ("ko")
  	Common::Language language;    // Common::parseLanguage(code)
  	Common::String name;          // UTF-8, <= 60 bytes, may be empty
  	Common::CodePage encoding;    // Common::kUtf8 or Common::kWindows949
  	Common::Path dir, map, trs, strings, messagesMap, messagesVolume;   // resolved (U4); empty = unset
  };
  typedef bool (*HiResPathTestFn)(const Common::Path &path);
  struct HiResManifestFs {
  	FontFileExistsFn fileExists;      // a file exists
  	HiResPathTestFn dirExists;        // a folder exists
  	HiResPathTestFn isVersion2Map;    // the file loads as a version-2 map
  };
  HiResManifestFs hiResRealFs();
  struct HiResLanguageManifest {
  	HiResLanguageManifest();
  	void clear();
  	Common::Path mapPath;                         // the game map read; empty = none
  	bool manifestOnly;                            // no section but [map] and [translation.*]
  	Common::Array<HiResTranslation> translations; // declared (spec 3.3 items 1-4), file order
  	Common::Array<Common::String> warnings;
  	const HiResTranslation *find(Common::Language lang) const;
  };
  bool readLanguageManifest(Common::SeekableReadStream &stream, const Common::Path &mapPath,
  						  const HiResTranslationRules &rules, Common::Language gameLanguage,
  						  HiResLanguageManifest &out, const HiResManifestFs &fs = hiResRealFs());
  bool readLanguageManifestFile(const Common::Path &mapPath, const HiResTranslationRules &rules,
  							  Common::Language gameLanguage, HiResLanguageManifest &out);
  Common::Path resolveGameMapPath(const Common::Path &gameDir, const HiResIniOverrides &ini);   // U4
  Common::Path defaultGameMapPath(const Common::Path &gameDir);   // <gameDir>/HIRESTXT.MAP if present (case-insensitive), else empty
  bool sameGameLanguage(Common::Language a, Common::Language b);   // equal, or both EN_ANY/EN_GRB/EN_USA
  typedef bool (*HiResUsableFn)(const HiResTranslation &tr, void *ctx, Common::String &why);
  struct HiResLanguageChoice {
  	HiResLanguageChoice();
  	const HiResTranslation *translation;   // null: no translation
  	Common::Language textLanguage;
  	bool overrideUpstream;                 // "not declared" rows of spec 6: the engine applies ScummVM's override
  };
  HiResLanguageChoice chooseHiResLanguage(const HiResLanguageManifest &m, Common::Language detected,
  										Common::Language ini /* UNK_LANG = unset */, HiResUsableFn usable,
  										void *ctx, Common::Array<Common::String> &warnings);
  Common::String hiResLanguageTags(const HiResLanguageManifest &m, HiResUsableFn usable, void *ctx);  // "lang_Korean lang_Japanese"
  ```
  In `font_map.h`: `kHiResKeyTranslation = 1 << 16` added to `HiResKeyFlag` and to `kHiResKeysSci`/`kHiResKeysScumm`
  (not AGS); `enum HiResMapRole { kHiResGameMap, kHiResLanguageMap };` and a field `HiResMapRole role` (default
  `kHiResGameMap`) in `HiResMapLoadOptions` (added by unify Task 6b, beside `target` and `quiet`), so the Task 6b
  `loadMap(..., options)` overloads carry it and a language map is loaded with the same render target as a game map.

- [ ] **Step 1: Write the failing tests**

```cpp
// test/graphics/hires_text_map_languages.h
#include <cxxtest/TestSuite.h>

#include "common/memstream.h"
#include "common/str.h"
#include "graphics/hires_text/map_languages.h"

namespace {
bool allFiles(const Common::Path &) { return true; }
bool noFiles(const Common::Path &) { return false; }
bool okUsable(const Graphics::HiResTranslation &, void *, Common::String &) { return true; }
bool noUtf8(const Graphics::HiResTranslation &tr, void *, Common::String &why) {
	if (tr.encoding == Common::kUtf8) {
		why = "SCUMM draws UTF-8 text only in v1-v6 PC releases";
		return false;
	}
	return true;
}
}

class HiResMapLanguagesTestSuite : public CxxTest::TestSuite {
	static bool read(const char *text, Graphics::HiResLanguageManifest &m,
					 const Graphics::HiResTranslationRules &rules = Graphics::kHiResTranslationScumm,
					 Common::Language game = Common::EN_ANY, bool files = true) {
		Common::MemoryReadStream s((const byte *)text, strlen(text));
		Graphics::HiResManifestFs fs;
		fs.fileExists = files ? allFiles : noFiles;
		fs.dirExists = files ? allFiles : noFiles;
		fs.isVersion2Map = allFiles;
		return Graphics::readLanguageManifest(s, Common::Path("/g/MI2/HIRESTXT.MAP", '/'), rules, game, m, fs);
	}

	static bool has(const Graphics::HiResLanguageManifest &m, const char *w) {
		for (uint i = 0; i < m.warnings.size(); ++i)
			if (m.warnings[i] == w)
				return true;
		return false;
	}

public:
	void test_scumm_section_is_read_and_paths_are_map_relative() {
		Graphics::HiResLanguageManifest m;
		TS_ASSERT(read("[map]\nversion=2\n[translation.ko]\nname=mi2kor\nencoding=cp949\n"
					   "trs=KO/KOREAN.TRS\ndir=KO\nmap=data:M2KO.MAP\n", m));
		TS_ASSERT_EQUALS(m.warnings.size(), 0u);
		TS_ASSERT(m.manifestOnly);
		TS_ASSERT_EQUALS(m.translations.size(), 1u);
		const Graphics::HiResTranslation &t = m.translations[0];
		TS_ASSERT_EQUALS(t.code, "ko");
		TS_ASSERT_EQUALS(t.language, Common::KO_KOR);
		TS_ASSERT_EQUALS(t.name, "mi2kor");
		TS_ASSERT_EQUALS(t.encoding, Common::kWindows949);
		TS_ASSERT_EQUALS(t.trs.toString('/'), "/g/MI2/KO/KOREAN.TRS");
		TS_ASSERT_EQUALS(t.dir.toString('/'), "/g/MI2/KO");
		TS_ASSERT(m.find(Common::KO_KOR) == &m.translations[0]);
		TS_ASSERT(m.find(Common::JA_JPN) == nullptr);
	}

	void test_sci_messages_pair_and_utf8_alias() {
		Graphics::HiResLanguageManifest m;
		TS_ASSERT(read("[map]\nversion=2\n[translation.ko]\nencoding=utf8\n"
					   "messages=KO/MESSAGE.MAP, KO/RESOURCE.MSG\nstrings=KO/SCI-KO.STR\n", m,
					   Graphics::kHiResTranslationSci));
		TS_ASSERT_EQUALS(m.warnings.size(), 0u);
		TS_ASSERT_EQUALS(m.translations[0].encoding, Common::kUtf8);
		TS_ASSERT_EQUALS(m.translations[0].messagesMap.toString('/'), "/g/MI2/KO/MESSAGE.MAP");
		TS_ASSERT_EQUALS(m.translations[0].messagesVolume.toString('/'), "/g/MI2/KO/RESOURCE.MSG");
		TS_ASSERT_EQUALS(m.translations[0].strings.toString('/'), "/g/MI2/KO/SCI-KO.STR");
	}

	void test_sections_that_are_ignored() {
		Graphics::HiResLanguageManifest m;
		TS_ASSERT(read("[map]\nversion=2\n"
					   "[translation.xx]\nencoding=utf-8\ntrs=A.TRS\n"
					   "[translation.ko:pc]\nencoding=utf-8\ntrs=A.TRS\n"
					   "[translation.en]\nencoding=utf-8\ntrs=A.TRS\n"
					   "[translation.de]\ntrs=A.TRS\n"
					   "[translation.fr]\nencoding=cp932\ntrs=A.TRS\n"
					   "[translation.ja]\nencoding=cp949\ntrs=A.TRS\n"
					   "[translation.it]\nencoding=utf-8\n", m));
		TS_ASSERT_EQUALS(m.translations.size(), 0u);
		TS_ASSERT(has(m, "HIRESTXT.MAP: [translation.xx]: 'xx' is not a ScummVM language code; section ignored"));
		TS_ASSERT(has(m, "HIRESTXT.MAP: [translation.ko:pc]: a translation section takes no qualifier; section ignored"));
		TS_ASSERT(has(m, "HIRESTXT.MAP: [translation.en]: English is the game's own language; section ignored"));
		TS_ASSERT(has(m, "HIRESTXT.MAP: [translation.de]: encoding is missing; section ignored"));
		TS_ASSERT(has(m, "HIRESTXT.MAP: [translation.fr] encoding 'cp932' is not utf-8 or cp949; section ignored"));
		TS_ASSERT(has(m, "HIRESTXT.MAP: [translation.ja] encoding cp949 is for ko only; section ignored"));
		TS_ASSERT(has(m, "HIRESTXT.MAP: [translation.it]: SCUMM needs trs=; section ignored"));
	}

	void test_missing_files_and_bad_messages() {
		Graphics::HiResLanguageManifest m;
		TS_ASSERT(read("[map]\nversion=2\n[translation.ko]\nencoding=cp949\ntrs=KO/KOREAN.TRS\n", m,
					   Graphics::kHiResTranslationScumm, Common::EN_ANY, false));
		TS_ASSERT(has(m, "HIRESTXT.MAP: [translation.ko] trs=KO/KOREAN.TRS: no such file; section ignored"));
		Graphics::HiResLanguageManifest n;
		TS_ASSERT(read("[map]\nversion=2\n[translation.ko]\nencoding=cp949\nmessages=KO/MESSAGE.MAP\n", n,
					   Graphics::kHiResTranslationSci));
		TS_ASSERT(has(n, "HIRESTXT.MAP: [translation.ko] messages needs the map and the volume, in that order; section ignored"));
		TS_ASSERT_EQUALS(n.translations.size(), 0u);
	}

	void test_bad_map_long_name_and_sci_needs() {
		Graphics::HiResLanguageManifest m;
		const char *text = "[map]\nversion=2\n[translation.ko]\nencoding=cp949\ntrs=A.TRS\nmap=B.MAP\n"
						   "name=0123456789012345678901234567890123456789012345678901234567890\n";
		Common::MemoryReadStream s((const byte *)text, strlen(text));
		Graphics::HiResManifestFs fs;
		fs.fileExists = allFiles;
		fs.dirExists = allFiles;
		fs.isVersion2Map = noFiles;
		TS_ASSERT(Graphics::readLanguageManifest(s, Common::Path("/g/MI2/HIRESTXT.MAP", '/'),
												 Graphics::kHiResTranslationScumm, Common::EN_ANY, m, fs));
		TS_ASSERT(has(m, "HIRESTXT.MAP: [translation.ko] name is longer than 60 bytes or not UTF-8; ignored"));
		TS_ASSERT(has(m, "HIRESTXT.MAP: [translation.ko] map=B.MAP: not a version 2 map; section ignored"));
		TS_ASSERT_EQUALS(m.translations.size(), 0u);
		Graphics::HiResLanguageManifest n;
		TS_ASSERT(read("[map]\nversion=2\n[translation.ko]\nencoding=cp949\n", n, Graphics::kHiResTranslationSci));
		TS_ASSERT(has(n, "HIRESTXT.MAP: [translation.ko]: SCI needs messages=, strings= or dir=; section ignored"));
		Graphics::HiResLanguageManifest d;
		TS_ASSERT(read("[map]\nversion=2\n[translation.ko]\nencoding=cp949\ndir=KO\n", d,
					   Graphics::kHiResTranslationSci, Common::EN_ANY, false));
		TS_ASSERT(has(d, "HIRESTXT.MAP: [translation.ko] dir=KO: no such folder; section ignored"));
	}

	void test_keys_of_the_other_engine_and_unknown_keys() {
		Graphics::HiResLanguageManifest m;
		TS_ASSERT(read("[map]\nversion=2\n[translation.ko]\nencoding=cp949\ntrs=A.TRS\nstrings=B.STR\nfoo=1\n", m));
		TS_ASSERT_EQUALS(m.translations.size(), 1u);
		TS_ASSERT(has(m, "SCUMM does not use [translation.ko] strings"));
		TS_ASSERT(has(m, "HIRESTXT.MAP: unknown key [translation.ko] foo"));
		TS_ASSERT(m.translations[0].strings.empty());
	}

	void test_font_sections_make_it_not_manifest_only() {
		Graphics::HiResLanguageManifest m;
		TS_ASSERT(read("[map]\nversion=2\n[font]\nface=A.SVF\n[translation.ko]\nencoding=cp949\ntrs=A.TRS\n", m));
		TS_ASSERT(!m.manifestOnly);
	}

	void test_choice_rows_of_spec_6() {
		Graphics::HiResLanguageManifest m;
		TS_ASSERT(read("[map]\nversion=2\n[translation.ko]\nencoding=utf-8\ntrs=A.TRS\n", m));
		Common::Array<Common::String> w;
		Graphics::HiResLanguageChoice c = Graphics::chooseHiResLanguage(m, Common::EN_ANY, Common::UNK_LANG, okUsable, nullptr, w);
		TS_ASSERT(!c.translation);
		TS_ASSERT_EQUALS(c.textLanguage, Common::EN_ANY);
		c = Graphics::chooseHiResLanguage(m, Common::EN_ANY, Common::EN_USA, okUsable, nullptr, w);
		TS_ASSERT(!c.translation);
		TS_ASSERT_EQUALS(c.textLanguage, Common::EN_ANY);
		c = Graphics::chooseHiResLanguage(m, Common::EN_ANY, Common::KO_KOR, okUsable, nullptr, w);
		TS_ASSERT(c.translation == &m.translations[0]);
		TS_ASSERT_EQUALS(c.textLanguage, Common::KO_KOR);
		TS_ASSERT_EQUALS(w.size(), 0u);
		c = Graphics::chooseHiResLanguage(m, Common::EN_ANY, Common::KO_KOR, noUtf8, nullptr, w);
		TS_ASSERT(!c.translation);
		TS_ASSERT_EQUALS(c.textLanguage, Common::EN_ANY);
		TS_ASSERT_EQUALS(w.back(), "HIRESTXT.MAP: language=ko: SCUMM draws UTF-8 text only in v1-v6 PC releases; the game runs in English");
		c = Graphics::chooseHiResLanguage(m, Common::EN_ANY, Common::JA_JPN, okUsable, nullptr, w);
		TS_ASSERT(!c.translation);
		TS_ASSERT(c.overrideUpstream);
		TS_ASSERT_EQUALS(c.textLanguage, Common::JA_JPN);
		TS_ASSERT_EQUALS(w.back(), "HIRESTXT.MAP: language=ja is not declared by /g/MI2/HIRESTXT.MAP (declared: ko); ScummVM's language override applies");
	}

	void test_tags_list_usable_languages_only() {
		Graphics::HiResLanguageManifest m;
		TS_ASSERT(read("[map]\nversion=2\n[translation.ko]\nencoding=cp949\ntrs=A.TRS\n"
					   "[translation.ja]\nencoding=utf-8\ntrs=B.TRS\n", m));
		TS_ASSERT_EQUALS(Graphics::hiResLanguageTags(m, okUsable, nullptr), "lang_Korean lang_Japanese");
		TS_ASSERT_EQUALS(Graphics::hiResLanguageTags(m, noUtf8, nullptr), "lang_Korean");
	}

	void test_same_game_language() {
		TS_ASSERT(Graphics::sameGameLanguage(Common::EN_ANY, Common::EN_GRB));
		TS_ASSERT(Graphics::sameGameLanguage(Common::KO_KOR, Common::KO_KOR));
		TS_ASSERT(!Graphics::sameGameLanguage(Common::EN_ANY, Common::KO_KOR));
	}
};
```

  In `test/graphics/hires_text_font_map.h`, replace `test_translation_lang_section_is_reserved` with:

```cpp
	// A game map's [translation.<lang>] belongs to map_languages: the font
	// loader skips it silently.
	void test_translation_section_is_skipped_in_a_game_map() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n[translation.ko]\nencoding=cp949\ntrs=A.TRS\n", m));
		TS_ASSERT_EQUALS(m.warnings.size(), 0u);
	}

	void test_translation_section_and_text_encoding_in_a_language_map() {
		Graphics::HiResMap m;
		const char *text = "[map]\nversion=2\n[text]\nencoding=cp949\n[translation.ko]\nencoding=cp949\n";
		Common::MemoryReadStream s((const byte *)text, strlen(text));
		Common::Array<Common::String> q;
		Graphics::HiResMapLoadOptions options;
		options.role = Graphics::kHiResLanguageMap;
		TS_ASSERT(Graphics::HiResFontMap::loadMap(s, Common::Path("/d", '/'), q, Graphics::kHiResKeysScumm, m, options));
		TS_ASSERT(hasWarning(m, "HIRESTXT.MAP: [translation.ko] is read only in the game's map; ignored"));
		TS_ASSERT(hasWarning(m, "HIRESTXT.MAP: [text] encoding is not read in a language map; the translation's encoding applies"));
		TS_ASSERT(!m.encodingSet);
	}

	void test_ags_does_not_use_translation_sections() {
		Graphics::HiResMap m;
		const char *text = "[map]\nversion=2\n[translation.ko]\nencoding=cp949\n";
		Common::MemoryReadStream s((const byte *)text, strlen(text));
		Common::Array<Common::String> q;
		TS_ASSERT(Graphics::HiResFontMap::loadMap(s, Common::Path("/d", '/'), q, Graphics::kHiResKeysAgs, m));
		TS_ASSERT(hasWarning(m, "AGS does not use [translation.ko]"));
	}
```


- [ ] **Step 2: Run to verify they fail.**
  Run: `make -C /home/thkim/work/scummvm/builds/linux-dos-test-scumm -j8 test 2>&1 | grep -m5 error`
  Expected: `map_languages.h: No such file or directory` / `kHiResLanguageMap` not declared.
- [ ] **Step 3: Implement.**
  - `map_languages.cpp`: `Common::INIFile ini; ini.requireKeyValueDelimiter(); ini.allowNonEnglishCharacters();` load; if
    `[map] version` is not `2`, return false with `out` cleared (the font loader reports U10.1; this reader stays silent).
    Iterate sections in file order; `manifestOnly` = every section is `map` or starts with `translation.`. For each
    `translation.<rest>`: qualifier check (a `:` in the name), `Common::parseLanguage(rest)`, `sameGameLanguage()`,
    then keys (`name`, `encoding`, `map`, `dir`, `trs`, `messages`, `strings`) with `rules.keys` deciding "does not use";
    paths through `HiResFontMap::resolvePath(value, mapPath.getParent())`; `messages` split at the one comma, trimmed;
    existence through `fs`; `map` through `fs.isVersion2Map`; `needOneOf`. Warning texts exactly as the tests.
    `hiResRealFs()`: `fontFileExists`, `Common::FSNode(p).isDirectory()`, and a check that opens the file and calls
    `HiResFontMap::loadMap()` into a scratch `HiResMap` with `kHiResLanguageMap` and returns its result (its warnings are
    dropped: the engine reports them when it loads the map for real).
  - `resolveGameMapPath()`: `ini.mapSet ? (ini.map.empty() ? Common::Path() : HiResFontMap::resolvePath(ini.map, gameDir)) :
    defaultGameMapPath(gameDir)`; `defaultGameMapPath()` lists `gameDir`'s children and returns the one named
    `HIRESTXT.MAP` case-insensitively. If the unify adapters (SCUMM `hires_text.cpp` `loadConfig()`, SCI `cache.cpp`) each
    carry their own copy of this rule, leave them for Tasks 4 and 6.
  - `chooseHiResLanguage()`: spec 6 rows in order; the "declared" warning's language name is
    `Common::getLanguageDescription(detected)`; the "not declared" warning lists `code`s joined by `, ` and is added only
    when `m.translations` is non-empty.
  - `font_map.cpp`: rename `kHSecReserved` to `kHSecTranslation`. Game map: skip it silently if the engine honours
    `kHiResKeyTranslation`, else `"<engine> does not use [<section>]"` (U10.3 form, no `HIRESTXT.MAP:` prefix). Language map: the "read only in the
    game's map" warning, and `[text] encoding` gives its warning and is not stored.
- [ ] **Step 4: Run the tests.** Expected: the new suite passes; overall only the known baseline failure.
- [ ] **Step 5: Commit** `GRAPHICS: Read the languages a hi-res text map declares` (`map_languages.{h,cpp}`,
  `graphics/module.mk`, `font_map.{h,cpp}`, both test files).

---

### Task 2: SCI detection - overlay entries removed, language tags, detector hook

**Files:**
- Modify: `engines/advancedDetector.h`, `engines/advancedDetector.cpp`, `engines/sci/detection.cpp`,
  `engines/sci/detection_internal.h`, `engines/sci/detection_internal.cpp`, `engines/sci/detection_tables.h`,
  `engines/sci/detection.h`, `engines/sci/metaengine.cpp`
- Test: `test/engines/sci/map_languages.h` (new)

**Interfaces:**
- Consumes: Task 1 (`readLanguageManifestFile`, `defaultGameMapPath`, `resolveGameMapPath`, `readHiResIniFromConfMan`,
  `hiResLanguageTags`, `kHiResTranslationSci`), `runs/langs-base/ko-entries.tsv` (Task 0).
- Produces:
  ```cpp
  // engines/advancedDetector.h, class AdvancedMetaEngineDetectionBase, protected:
  virtual Common::Language languageForMatching(const Common::FSNode &gameDir, Common::Language iniLanguage) const { return iniLanguage; }
  // engines/sci/detection_internal.h
  bool sciTranslationUsable(SciVersion version, const Graphics::HiResTranslation &tr, Common::String &why);
  Common::String sciMapLanguageTags(const Common::Path &gameMap, Common::Language detected, SciVersion version);
  ```

- [ ] **Step 1: Write the failing tests**

```cpp
// test/engines/sci/map_languages.h
#include <cxxtest/TestSuite.h>

#include "engines/sci/detection_internal.h"
#include "graphics/hires_text/map_languages.h"

class SciMapLanguagesTestSuite : public CxxTest::TestSuite {
public:
	void test_sci0_to_sci11_are_usable() {
		Graphics::HiResTranslation tr;
		tr.language = Common::KO_KOR;
		tr.encoding = Common::kWindows949;
		Common::String why;
		TS_ASSERT(Sci::sciTranslationUsable(Sci::SCI_VERSION_0_EARLY, tr, why));
		TS_ASSERT(Sci::sciTranslationUsable(Sci::SCI_VERSION_1_1, tr, why));
		tr.encoding = Common::kUtf8;
		TS_ASSERT(Sci::sciTranslationUsable(Sci::SCI_VERSION_1_1, tr, why));
	}

	void test_sci32_is_not() {
		Graphics::HiResTranslation tr;
		tr.language = Common::KO_KOR;
		tr.encoding = Common::kWindows949;
		Common::String why;
		TS_ASSERT(!Sci::sciTranslationUsable(Sci::SCI_VERSION_2, tr, why));
		TS_ASSERT_EQUALS(why, "SCI draws translations only in SCI0-SCI1.1 games");
	}

	void test_no_map_no_tags() {
		TS_ASSERT_EQUALS(Sci::sciMapLanguageTags(Common::Path(), Common::EN_ANY, Sci::SCI_VERSION_1_1), "");
	}
};
```

- [ ] **Step 2: Run to verify it fails** (`sciTranslationUsable` not declared).
- [ ] **Step 3: Implement.**
  - `advancedDetector.cpp` `identifyGame()`: after `language = Common::parseLanguage(ConfMan.get("language"))` and after
    `dir` is known, `language = languageForMatching(dir, language);`.
  - `detection_internal.cpp`: `sciTranslationUsable()` (`version <= SCI_VERSION_1_1`, else the `why` above; the `cp949`
    for `ko` rule is already Task 1's); `sciMapLanguageTags()` = empty for an empty path, else
    `readLanguageManifestFile(path, kHiResTranslationSci, detected, m)` and `hiResLanguageTags(m, usable, &version)`.
  - `detection.cpp` `SciMetaEngineDetection`: in `detectGames()`, after the existing language restore, append
    `sciMapLanguageTags(defaultGameMapPath(fslist.begin()->getParent().getPath()), game.language, g->version)`. Override
    `languageForMatching()`: if `iniLanguage` is declared by `resolveGameMapPath(gameDir.getPath(),
    readHiResIniFromConfMan(ConfMan.getActiveDomainName(), w))`'s manifest, return `Common::UNK_LANG`.
  - `metaengine.cpp` `createInstance()`: build the tags as `getGameGUIOptionsDescriptionLanguage(desc->language)` (+
    English for `ADGF_ADDENGLISH`) + `sciMapLanguageTags(<target's game map>, desc->language, g->version)`, instead of
    carrying the stored `lang_` tags forward.
  - `detection_tables.h`: delete every row the Task 0 table marks `overlay` (keep `original`); `detection.h`: delete
    `ADGF_UTF8I18N` and its comment. `git grep -n ADGF_UTF8I18N` then lists only `sci.cpp` (Task 3 removes those).
    If the user ruled (spec 13 item 1) to keep GK1/GK2, keep those three rows and say so in the commit body.
- [ ] **Step 4: Run the tests** (Task 3 has not removed the `ADGF_UTF8I18N` uses yet: if `sci.cpp` no longer compiles,
  replace `(_gameDescription->flags & ADGF_UTF8I18N)` by `false` there in this task and note it).
  Then a Linux check with a staged LB2 folder (Task 0's `lb2pkg` copy: move `GAMES/LB2KO/MESSAGE.MAP`, `RESOURCE.MSG`
  into `GAMES/LB2KO/KO/` and write the spec 11.1 `HIRESTXT.MAP` next to them):
  ```bash
  S=/home/thkim/work/scummvm/runs/langs-stage && rm -rf $S && mkdir -p $S && cp -a /home/thkim/work/scummvm/runs/langs-base/lb2pkg/SCUMMVM/GAMES/LB2KO $S/LB2 && mkdir $S/LB2/KO && mv $S/LB2/MESSAGE.MAP $S/LB2/RESOURCE.MSG $S/LB2/KO/
  printf '[map]\nversion=2\n[translation.ko]\nencoding=cp949\nmessages=KO/MESSAGE.MAP, KO/RESOURCE.MSG\n' > $S/LB2/HIRESTXT.MAP
  /home/thkim/work/scummvm/builds/linux-dos-test-scumm/scummvm --detect --path=$S/LB2 2>&1 | tail -3
  ```
  Expected: `laurabow2` English DOS CD (and the Windows twin), no Korean entry. And
  `scummvm --path=$S/LB2 --add` writes a target whose `guioptions` contain `lang_English lang_Korean`.
- [ ] **Step 5: Commit** `SCI: Detect the original release; add the languages its hi-res text map declares`
  (`advancedDetector.{h,cpp}` go in the same commit, subject prefix `SCI:` with body line
  `ENGINES: languageForMatching() hook in identifyGame()`).

---

### Task 3: SCI - choose the text language, encoding from the section

**Files:**
- Modify: `engines/sci/sci.h`, `engines/sci/sci.cpp`, `engines/sci/textencoding.h`, `engines/sci/textencoding.cpp`,
  `engines/sci/engine/translation.h`, `engines/sci/engine/translation.cpp`, `engines/sci/module.mk`
- Delete: `engines/sci/engine/text_overlay.h`, `engines/sci/engine/text_overlay.cpp` (and every use)
- Test: `test/engines/sci/translation.h` (the `load()` tests), `test/engines/sci/korean_text.h` or a new
  `test/engines/sci/text_language.h` for the encoding rules

**Interfaces:**
- Consumes: Task 1 (`chooseHiResLanguage`, `HiResTranslation`), Task 2 (`sciTranslationUsable`).
- Produces:
  ```cpp
  // engines/sci/textencoding.h
  struct SciTextRules { bool heapUtf8; bool korean; Common::CodePage codePage; };
  SciTextRules sciTextRules(const Graphics::HiResTranslation *tr, TextEncodingSetting enc, Common::Language textLanguage,
                            Common::CodePage languageCodePage);
  // engines/sci/sci.h (SciEngine)
  const Graphics::HiResTranslation *translation() const;   // null: none
  const Graphics::HiResLanguageManifest &languageManifest() const;   // the game map's, read in the constructor
  Common::Language getLanguage() const;                    // = _textLanguage
  // engines/sci/engine/translation.h
  bool ScriptStrings::load(const Common::Path &path);      // replaces load(languageCode)
  ```

- [ ] **Step 1: Write the failing tests**

```cpp
// test/engines/sci/text_language.h
#include <cxxtest/TestSuite.h>

#include "engines/sci/textencoding.h"
#include "graphics/hires_text/map_languages.h"

class SciTextLanguageTestSuite : public CxxTest::TestSuite {
public:
	void test_translation_encoding_wins() {
		Graphics::HiResTranslation tr;
		tr.language = Common::KO_KOR;
		tr.encoding = Common::kWindows949;
		Sci::SciTextRules r = Sci::sciTextRules(&tr, Sci::kTextEncodingUtf8, Common::KO_KOR, Common::kWindows949);
		TS_ASSERT(!r.heapUtf8);
		TS_ASSERT(r.korean);
		TS_ASSERT_EQUALS(r.codePage, Common::kWindows949);
		tr.encoding = Common::kUtf8;
		r = Sci::sciTextRules(&tr, Sci::kTextEncodingEucKr, Common::KO_KOR, Common::kWindows949);
		TS_ASSERT(r.heapUtf8);
		TS_ASSERT(!r.korean);
	}

	void test_no_translation_keeps_text_encoding_and_language() {
		Sci::SciTextRules r = Sci::sciTextRules(nullptr, Sci::kTextEncodingAuto, Common::EN_ANY, Common::kLatin1);
		TS_ASSERT(!r.heapUtf8);
		TS_ASSERT(!r.korean);
		TS_ASSERT_EQUALS(r.codePage, Common::kLatin1);
		r = Sci::sciTextRules(nullptr, Sci::kTextEncodingEucKr, Common::EN_ANY, Common::kLatin1);
		TS_ASSERT(r.korean);
		TS_ASSERT_EQUALS(r.codePage, Common::kWindows949);
		r = Sci::sciTextRules(nullptr, Sci::kTextEncodingAuto, Common::KO_KOR, Common::kWindows949);   // an in-place Korean original
		TS_ASSERT(r.korean);
	}
};
```

  In `test/engines/sci/translation.h`, add (the loader reads a file path now; write the table to the scratchpad):

```cpp
	void test_load_takes_a_path() {
		const char *dir = "/tmp/claude-1000/-home-thkim-work/36a78c74-674a-4d24-9ca3-7236f6e2ebde/scratchpad";
		Common::Path p = Common::Path(dir, '/').appendComponent("T.STR");
		Common::DumpFile f;
		TS_ASSERT(f.open(p));
		f.writeString("3\t7\tsomething\n");
		f.close();
		Sci::ScriptStrings s;
		TS_ASSERT(s.load(p));
		TS_ASSERT(s.isLoaded());
	}
```

- [ ] **Step 2: Run to verify they fail** (`sciTextRules` / `load(const Common::Path &)` not declared).
- [ ] **Step 3: Implement.**
  - `sciTextRules()`: with `tr`: `heapUtf8 = tr->encoding == kUtf8`, `korean = tr->encoding == kWindows949`,
    `codePage = korean ? kWindows949 : languageCodePage`; without: `heapUtf8 = textEncodingHeapIsUtf8(enc, false)`,
    `korean = textEncodingKorean(enc, textLanguage)`, `codePage = textEncodingCodePage(enc, languageCodePage)`.
  - `SciEngine` constructor, after `_textEncoding` is read and before anything asks for the language: resolve the game map
    (`resolveGameMapPath(ConfMan.getPath("path"), readHiResIniFromConfMan(...))`), `readLanguageManifestFile(...,
    kHiResTranslationSci, _gameDescription->language, _manifest)`, print its warnings, then
    `chooseHiResLanguage(_manifest, _gameDescription->language, ConfMan.hasKey("language") ?
    Common::parseLanguage(ConfMan.get("language")) : Common::UNK_LANG, usable, &version, w)`; store `_translation`
    (a copy, or null) and `_textLanguage` (`overrideUpstream` keeps the ini language, as today's `getLanguage()` did).
    With a translation and `text_encoding` set: `warning("SCI: text_encoding is ignored: [translation.%s] encoding=%s
    applies", ...)`. Debug line of spec 6 row 2.
  - `getLanguage()` returns `_textLanguage`. `heapStringsAreUtf8()`, `usesKoreanText()`, `getSciLanguageCodePage()` read
    `sciTextRules(_translation ? &*_translation : nullptr, _textEncoding, _textLanguage, languageCodePage(_textLanguage))`
    (compute once, store `_textRules`). Remove `_utf8Manifest`, every `ADGF_UTF8I18N` use and every `TextOverlay` use
    (`usesHiresDoubleByteText()`, `run()`, `translationCodePoints()`, the getters).
  - `run()`: `_scriptStrings.load(_translation->strings)` when a translation has `strings`, else nothing.
  - `ScriptStrings::load(const Common::Path &)`: open with `Common::File::open(Common::FSNode(path))`; warning text
    `ScriptStrings: <path> is malformed, ignored`.
- [ ] **Step 4: Run the tests.** Expected: pass; only the baseline failure. `git grep -n "TextOverlay\|_utf8Manifest\|ADGF_UTF8I18N\|sci-%s.str" engines/ test/` prints nothing.
- [ ] **Step 5: Commit** `SCI: Take the text language and its encoding from the map's translation section`.

---

### Task 4: SCI - translation resource sources, `dir`, `.uni`, language map

**Files:**
- Modify: `engines/sci/resource/resource.h`, `engines/sci/resource/resource.cpp`,
  `engines/sci/resource/resource_intern.h`, `engines/sci/sci.cpp` (`run()`), `engines/sci/graphics/cache.cpp`
- Test: `test/engines/sci/resource_names.h` (new)

**Interfaces:**
- Consumes: Task 3 (`SciEngine::translation()`), Task 1 (`resolveGameMapPath`, `HiResLanguageManifest::manifestOnly`).
- Produces:
  ```cpp
  // resource.h (ResourceManager)
  void addTranslationSources(const Graphics::HiResTranslation &tr);   // after addAppropriateSources()
  static bool parsePatchFileName(const Common::String &fileName, ResourceType type, uint16 &number);  // shared by both patch scans
  // resource_intern.h (ResourceSource)
  bool isTranslation() const;   // set by the constructors addTranslationSources() uses
  class TranslationDirectoryResourceSource : public ResourceSource;  // scans an FSNode folder, not SearchMan
  ```

- [ ] **Step 1: Write the failing test**

```cpp
// test/engines/sci/resource_names.h
#include <cxxtest/TestSuite.h>

#include "engines/sci/resource/resource.h"

class SciResourceNamesTestSuite : public CxxTest::TestSuite {
public:
	void test_sci0_and_sci1_patch_names() {
		uint16 n = 0;
		TS_ASSERT(Sci::ResourceManager::parsePatchFileName("text.001", Sci::kResourceTypeText, n));
		TS_ASSERT_EQUALS(n, 1);
		TS_ASSERT(Sci::ResourceManager::parsePatchFileName("TEXT.052", Sci::kResourceTypeText, n));
		TS_ASSERT_EQUALS(n, 52);
		TS_ASSERT(Sci::ResourceManager::parsePatchFileName("100.msg", Sci::kResourceTypeMessage, n));
		TS_ASSERT_EQUALS(n, 100);
		TS_ASSERT(!Sci::ResourceManager::parsePatchFileName("textfile.001", Sci::kResourceTypeText, n));
		TS_ASSERT(!Sci::ResourceManager::parsePatchFileName("100x.msg", Sci::kResourceTypeMessage, n));
	}
};
```

- [ ] **Step 2: Run to verify it fails** (`parsePatchFileName` not declared).
- [ ] **Step 3: Implement.**
  - Extract the name rule of `readResourcePatches()` (`resource.cpp` "SCI1 scheme"/"SCI0 scheme" block) into
    `parsePatchFileName()`; `readResourcePatches()` calls it (no behaviour change for the game folder).
  - `ResourceSource` gets `bool _translation = false` + `isTranslation()`; `PatchResourceSource`,
    `ExtMapResourceSource`, `VolumeResourceSource` get constructors taking `const Common::FSNode *` (the two latter
    already have one) and a `translation` flag. `ResourceManager` owns the nodes in a `Common::List<Common::FSNode>
    _translationNodes` (stable addresses).
  - `addTranslationSources(tr)`: if `tr.dir` is set, add a `TranslationDirectoryResourceSource(tr.dir)` whose
    `scanSource()` lists the folder (`Common::FSNode(tr.dir).getChildren(list, kListFilesOnly)`), runs
    `parsePatchFileName()` for each patchable type (same type loop as `readResourcePatches()`), and calls
    `processPatch()` with a translation `PatchResourceSource` on the file's node. If `tr.messagesMap` is set, add
    `VolumeResourceSource(volumeNode, addExternalMap(mapNode, 0 /*volume*/), 0, translation)`. Call
    `scanNewSources()` once at the end.
  - Replace `isKoreanMessageMap(source)` by `source->isTranslation()` at its three uses, and the language test at
    `ResourceSource::loadResource()` (`... && g_sci->getLanguage() == Common::KO_KOR`) by `isTranslation()`. Delete
    `isKoreanMessageMap()`.
  - `SciEngine::run()`: `if (_translation) _resMan->addTranslationSources(*_translation);` right after
    `addAppropriateSources()`.
  - `GfxCache`: the map it loads is `_translation->map` when set, else the game map unless `_manifest.manifestOnly`,
    else none (replace the adapter's own path rule by `resolveGameMapPath()` if it still has one). Language maps load with
    `kHiResLanguageMap`. `loadUniBundle()`: when a translation has `dir`, try each of the three names through
    `Common::FSDirectory(tr.dir).createReadStreamForMember(name)` first (`GfxFontUnicode::load()` gains a
    `Common::SeekableReadStream *` overload if it only takes a name).
- [ ] **Step 4: Run the tests**, then the LB2 check on the Task 2 staging folder with a target
  `language=ko` (`scummvm -c <scratch ini> -d1 lb2ko`, 60 s, same as Task 0 Step 3). Expected: the log shows the
  translation's resources read from `KO/RESOURCE.MSG` - the same count of Korean message lines as `lb2-ko.log` - and no
  `Using fallback detection`. Run again with no `language=`: English messages, and the log has no `KO/` access.
- [ ] **Step 5: Commit** `SCI: Load a translation's resources from its section, not by name or language`.

---

### Task 5: SCUMM - detection and the language choice

**Files:**
- Create: `engines/scumm/translation_rules.h`
- Modify: `engines/scumm/detection.cpp`, `engines/scumm/detection_internal.h`, `engines/scumm/trs_bundle.h`,
  `engines/scumm/metaengine.cpp`, `engines/scumm/scumm.h`, `engines/scumm/scumm.cpp` (constructor: store the translation)
- Test: `test/engines/scumm/translation_rules.h` (new), `test/engines/scumm/trs_bundle.h` (reduced to `trsBodyIsUtf8`)

**Interfaces:**
- Consumes: Task 1.
- Produces:
  ```cpp
  // engines/scumm/translation_rules.h (namespace Scumm, header-only, used by detection and engine)
  struct TranslationRelease { byte version; byte heversion; Common::Platform platform; byte id; };
  bool translationUsable(const TranslationRelease &r, const Graphics::HiResTranslation &tr, Common::String &why);
  // "why": "SCUMM draws UTF-8 text only in v1-v6 PC releases" / "SCUMM draws cp949 text only in v1-v6 releases and Full Throttle"
  // engines/scumm/scumm.h (ScummEngine)
  void setTranslation(const Graphics::HiResTranslation *tr, Common::Language detected);   // before init()
  const Graphics::HiResTranslation *translation() const;
  ```

- [ ] **Step 1: Write the failing tests**

```cpp
// test/engines/scumm/translation_rules.h
#include <cxxtest/TestSuite.h>

#include "engines/scumm/scumm.h"
#include "engines/scumm/translation_rules.h"

class ScummTranslationRulesTestSuite : public CxxTest::TestSuite {
	static Scumm::TranslationRelease rel(byte v, Common::Platform p, byte id = Scumm::GID_MONKEY2) {
		Scumm::TranslationRelease r;
		r.version = v;
		r.heversion = 0;
		r.platform = p;
		r.id = id;
		return r;
	}

public:
	void test_utf8_needs_a_code_point_renderer() {
		Graphics::HiResTranslation tr;
		tr.language = Common::KO_KOR;
		tr.encoding = Common::kUtf8;
		Common::String why;
		TS_ASSERT(Scumm::translationUsable(rel(5, Common::kPlatformDOS), tr, why));
		TS_ASSERT(!Scumm::translationUsable(rel(5, Common::kPlatformFMTowns), tr, why));
		TS_ASSERT_EQUALS(why, "SCUMM draws UTF-8 text only in v1-v6 PC releases");
		TS_ASSERT(!Scumm::translationUsable(rel(7, Common::kPlatformDOS, Scumm::GID_DIG), tr, why));
	}

	void test_cp949_follows_the_korean_patch_rule() {
		Graphics::HiResTranslation tr;
		tr.language = Common::KO_KOR;
		tr.encoding = Common::kWindows949;
		Common::String why;
		TS_ASSERT(Scumm::translationUsable(rel(5, Common::kPlatformDOS), tr, why));
		TS_ASSERT(Scumm::translationUsable(rel(7, Common::kPlatformDOS, Scumm::GID_FT), tr, why));
		TS_ASSERT(!Scumm::translationUsable(rel(7, Common::kPlatformDOS, Scumm::GID_DIG), tr, why));
		TS_ASSERT_EQUALS(why, "SCUMM draws cp949 text only in v1-v6 releases and Full Throttle");
	}
};
```

  `test/engines/scumm/trs_bundle.h`: delete the five name tests (`test_korean_keeps_its_established_name` ..
  `test_round_trip_over_every_language`); keep or add one `trsBodyIsUtf8` test.

- [ ] **Step 2: Run to verify it fails** (`translation_rules.h` missing).
- [ ] **Step 3: Implement.**
  - `translationUsable()`: UTF-8: today's `supported` expression from `probeLanguageBundle()` (`string.cpp` ~2462-2466,
    with `!(Mac && (Loom || Indy3))`); cp949: `version < 7 || id == GID_FT`. Move, do not copy: `probeLanguageBundle()`
    stops checking (Task 6).
  - Remove `detectLanguageBundle()` and the `.trs` branch of `detectLanguage()` (`detection_internal.h` ~217-261);
    remove `getTrsBundleName()`, `getTrsBundleNames()`, `getTrsBundleLanguage()` from `trs_bundle.h` (keep
    `trsBodyIsUtf8()` and the format comment, reworded: the section names the file and its language).
  - `ScummMetaEngineDetection::detectGames()`: after `appendGUIOptions(language)`, read
    `defaultGameMapPath(<the folder of fslist>)` with `kHiResTranslationScumm` and `D = x->language`, and append
    `hiResLanguageTags(m, usable, &release)` (release from `x->game`).
  - `ScummMetaEngine::createInstance()`: keep `D = res.language` before the "Language override" block; replace that
    block: read the target's game map; `chooseHiResLanguage(m, D, ini, usable, &release, w)` with a start-time `usable`
    that also checks the bundle (Task 6's `checkTrsBundle()`; until Task 6 lands, `translationUsable()` only); print `w`;
    `res.language = choice.textLanguage`. The `updateGameGUIOptions()` call gets the language tags
    `getGameGUIOptionsDescriptionLanguage(D) + " " + hiResLanguageTags(...)`. After the engine is constructed:
    `engine->setTranslation(choice.translation, D)` (store a copy in the engine).
- [ ] **Step 4: Run the tests.** Then on Linux with a staged MI2 folder:
  ```bash
  S=/home/thkim/work/scummvm/runs/langs-stage && mkdir -p $S/MI2/KO && cp /home/thkim/work/scummvm/gamedata/mi2kor/monkey2.00[01] $S/MI2/ && cp /home/thkim/work/scummvm/gamedata/mi2kor/korean.trs /home/thkim/work/scummvm/gamedata/mi2kor/korean0*.fnt $S/MI2/KO/
  printf '[map]\nversion=2\n[translation.ko]\nencoding=cp949\ntrs=KO/korean.trs\ndir=KO\n' > $S/MI2/HIRESTXT.MAP
  /home/thkim/work/scummvm/builds/linux-dos-test-scumm/scummvm --detect --path=$S/MI2 2>&1 | tail -2
  ```
  Expected: `monkey2` English, and an added target's `guioptions` contain `lang_English lang_Korean`.
- [ ] **Step 5: Commit** `SCUMM: Detect the original release; add the languages its hi-res text map declares`.

---

### Task 6: SCUMM - load the translation from its section

**Files:**
- Modify: `engines/scumm/string.cpp` (`probeLanguageBundle()`, `loadLanguageBundle()`, `getLanguageBundleFilename()`),
  `engines/scumm/charset.cpp` (`loadKorFont()`, `loadCJKCells()`), `engines/scumm/hires_text.{h,cpp}` (`loadConfig()`),
  `engines/scumm/text_utf8.{h,cpp}` (`decideTrsUtf8()` replaced), `engines/scumm/scumm.cpp`, `engines/scumm/metaengine.cpp`
- Test: `test/engines/scumm/trs_utf8.h`

**Interfaces:**
- Consumes: Task 5 (`translation()`, `setTranslation()`), Task 1.
- Produces:
  ```cpp
  // engines/scumm/text_utf8.h
  enum TrsCheck { kTrsOk, kTrsNotABundle, kTrsBomButCp949 };
  TrsCheck checkTrsEncoding(const byte *data, uint32 size, Common::CodePage encoding);   // replaces decideTrsUtf8()
  bool checkTrsBundle(const Common::Path &path, Common::CodePage encoding, Common::String &why);   // reads the file; why = spec 10.4 text tail
  // engines/scumm/scumm.h
  Common::SeekableReadStream *openTranslationFile(const char *name);   // dir first, null if absent
  ```

- [ ] **Step 1: Write the failing tests** (append to `test/engines/scumm/trs_utf8.h`; the file already builds bundles in
  memory - use its helper, here called `makeBundle(bool bom)`; if it has another name, use that one):

```cpp
	void test_encoding_comes_from_the_section() {
		Common::Array<byte> utf8 = makeBundle(true), legacy = makeBundle(false);
		TS_ASSERT_EQUALS(Scumm::checkTrsEncoding(utf8.begin(), utf8.size(), Common::kUtf8), Scumm::kTrsOk);
		TS_ASSERT_EQUALS(Scumm::checkTrsEncoding(legacy.begin(), legacy.size(), Common::kUtf8), Scumm::kTrsOk);
		TS_ASSERT_EQUALS(Scumm::checkTrsEncoding(legacy.begin(), legacy.size(), Common::kWindows949), Scumm::kTrsOk);
		TS_ASSERT_EQUALS(Scumm::checkTrsEncoding(utf8.begin(), utf8.size(), Common::kWindows949), Scumm::kTrsBomButCp949);
		const byte junk[16] = { 'N', 'O', 'P', 'E' };
		TS_ASSERT_EQUALS(Scumm::checkTrsEncoding(junk, sizeof(junk), Common::kUtf8), Scumm::kTrsNotABundle);
	}
```

  Delete the `decideTrsUtf8` tests (the ini `text_encoding` and "looks like UTF-8" rules are gone).
- [ ] **Step 2: Run to verify it fails.**
- [ ] **Step 3: Implement.**
  - `checkTrsEncoding()` on `parseTrsHeader()` + `trsBodyIsUtf8()`; `checkTrsBundle()` opens `Common::File` on
    `Common::FSNode(path)`, and maps `kTrsNotABundle` to `"trs=<path> is not an SCVMTRS bundle"`, `kTrsBomButCp949` to
    `"trs=<path> starts with a UTF-8 BOM but encoding is cp949"` (`<path>` as the section wrote it). `createInstance()`'s
    start-time `usable` (Task 5) calls it after `translationUsable()`; `chooseHiResLanguage()` composes the spec 10.4
    text `HIRESTXT.MAP: language=ko: <why>; the game runs in English`, as for every refusal.
  - `probeLanguageBundle()`: no name list, no `text_encoding`. No translation: return with nothing set. With one:
    `_trsBundlePath = translation()->trs`; `_textUtf8 = encoding == kUtf8` and the rest of today's UTF-8 branch (hi-res
    on: `useUtf8Text()`; off: `legacyTextPage(_language)` or '?' with today's warning); cp949: legacy.
  - `loadLanguageBundle()`: open `_trsBundlePath` with `Common::File::open(Common::FSNode(path))`, not `openFile()`.
  - `openTranslationFile(name)`: `Common::FSDirectory(translation()->dir).createReadStreamForMember(name)` when a
    translation has `dir`, else null. `loadKorFont()` and `loadCJKCells()`: try it first for `korean.fnt` and
    `korean%02d.fnt`, then `fp.open(name)` as today.
  - `ScummHiResText::loadConfig()`: the map is `translation()->map` (as `kHiResLanguageMap`) when set, else the game map
    unless manifest-only, else none (use `resolveGameMapPath()`; delete the adapter's own copy of the rule if it has one).
    The default code page is the translation's `encoding` when a translation is chosen, else as today (`[text]
    encoding`, else the language).
- [ ] **Step 4: Run the tests**, then on the Task 5 staging folder: `language=ko` target with `hires_text_map` unset and
  `[translation.ko] map=` pointing at `dists/engine-data/hires_text/dos/M2KO.MAP` (absolute path); `-d1` log shows
  `loadLanguageBundle: Loaded 8780 entries` and the language map read; no `language=`: no bundle loaded.
- [ ] **Step 5: Commit** `SCUMM: Load the translation, its fonts and its map from the map's translation section`.

---

### Task 7: Language popup label from `name=`

**Files:**
- Create: `gui/languagelabel.h`, `gui/languagelabel.cpp`, `test/gui/languagelabel.h`
- Modify: `gui/module.mk`, `test/module.mk` (`TEST_LIBS += gui/languagelabel.o`), `engines/metaengine.h`,
  `gui/editgamedialog.cpp`, `engines/sci/metaengine.cpp`, `engines/scumm/metaengine.cpp`

**Interfaces:**
- Consumes: Task 1 (`readLanguageManifestFile`, `resolveGameMapPath`).
- Produces:
  ```cpp
  // engines/metaengine.h, class MetaEngine
  virtual void getLanguageLabels(const Common::String &target, Common::HashMap<int, Common::U32String> &labels) const {}
  // gui/languagelabel.h
  namespace GUI { Common::U32String languagePopupLabel(const char *description, const Common::U32String &label); }
  ```

- [ ] **Step 1: Write the failing test**

```cpp
// test/gui/languagelabel.h
#include <cxxtest/TestSuite.h>

#include "gui/languagelabel.h"

class LanguageLabelTestSuite : public CxxTest::TestSuite {
public:
	void test_label_is_appended() {
		TS_ASSERT_EQUALS(GUI::languagePopupLabel("Korean", Common::U32String("2014 fan translation")),
						 Common::U32String("Korean - 2014 fan translation"));
	}
	void test_no_label_keeps_the_name() {
		TS_ASSERT_EQUALS(GUI::languagePopupLabel("Korean", Common::U32String()), Common::U32String("Korean"));
	}
};
```

- [ ] **Step 2: Run to verify it fails.**
- [ ] **Step 3: Implement.** `languagePopupLabel()` as tested. `EditGameDialog::addGameControls()`: when
  `enginePlugin` is loaded, `enginePlugin->get<MetaEngine>().getLanguageLabels(_domain, labels)` before the popup loop,
  and `appendEntry(languagePopupLabel(l->description, labels.getValOrDefault(l->id)), l->id)`. SCI and SCUMM
  `getLanguageLabels()`: the target's game map (`resolveGameMapPath(ConfMan.getPath("path", target), <ini of target>)`),
  its manifest, `labels[tr.language] = Common::U32String(tr.name, Common::kUtf8)` for each translation with a name.
  (`readHiResIniFromConfMan()` takes the domain name, so the target's own keys are read.)
- [ ] **Step 4: Run the tests**; on Linux, open the launcher on the staged LB2 target, Edit Game: the popup lists
  `English` and `Korean - <name>` (take a screenshot with the harness screenshot helper if there is one, else describe it
  in the report).
- [ ] **Step 5: Commit** `GUI: Label a map-declared language with its translation's name`.

---

### Task 8: Maps and generators

**Files:**
- Modify: `dists/engine-data/hires_text/dos/{CAM,KQ1,LB1,LB2,M2}KO.MAP` (language maps, one per game with both presets
  since unify Task 14: drop `[text] encoding` and say why in a comment), `test/graphics/hires_text_shipped_maps.h`,
  `tools/korean/makemaps.py`, `tools/korean/inifix.py`, `tools/korean/README.md`
- Create: `dists/engine-data/hires_text/dos/games/{LB1,LB2,KQ1,CAMELOT,MI2}/HIRESTXT.MAP` (the game maps the packages
  ship; spec 11; one per game - the presets are the language map's `:clut8` sections, chosen by `render_target`)

**Interfaces:**
- Consumes: Task 1 (`kHiResLanguageMap`, `readLanguageManifest`).
- Produces: game maps whose `[translation.ko]` paths are relative to the game folder (`KO/...`) and whose `map=` is
  `data:<X>KO.MAP`; `encoding` per game: LB2 and MI2 `cp949`, LB1, KQ1, Camelot `utf-8` (check each
  against the current package: the text resources' actual bytes decide; record the evidence in the report).
  `M1KO.MAP` is untouched (MI1 is a Korean original, spec 11.3).

- [ ] **Step 1: Coordination check** (as unify Task 14 Step 1): `git status --short dists/engine-data/hires_text/`
  clean, else stop.
- [ ] **Step 2: Extend the failing test.** In `hires_text_shipped_maps.h`: load the DOS `*KO.MAP` language maps with
  `options.role = kHiResLanguageMap` for every render target (zero warnings, as unify Task 14's loop); add
  ```cpp
	void test_game_maps_declare_ko() {
		Common::FSNode games = tree().getChild("dists").getChild("engine-data").getChild("hires_text").getChild("dos").getChild("games");
		Common::FSList dirs;
		TS_ASSERT(games.getChildren(dirs, Common::FSNode::kListDirectoriesOnly));
		for (uint i = 0; i < dirs.size(); ++i) {
			const bool scumm = dirs[i].getName() == "MI2";
			Common::FSList files;
			TS_ASSERT(dirs[i].getChildren(files, Common::FSNode::kListFilesOnly));
			for (uint j = 0; j < files.size(); ++j) {
				Common::File f;
				TS_ASSERT(f.open(files[j]));
				Graphics::HiResManifestFs fs;
				fs.fileExists = anyPath;
				fs.dirExists = anyPath;
				fs.isVersion2Map = anyPath;
				Graphics::HiResLanguageManifest m;
				TS_ASSERT(Graphics::readLanguageManifest(f, files[j].getPath(),
					scumm ? Graphics::kHiResTranslationScumm : Graphics::kHiResTranslationSci, Common::EN_ANY, m, fs));
				TS_ASSERT(m.manifestOnly);
				TS_ASSERT(m.find(Common::KO_KOR));
				TS_ASSERT_EQUALS(m.warnings.size(), 0u);
			}
		}
	}
  ```
  with `static bool anyPath(const Common::Path &) { return true; }` in the suite. Run: fails (no `games/`, and the
  language maps warn about `[text] encoding`).
- [ ] **Step 3: Write the maps.** Language maps: remove `[text] encoding` lines, keep everything else. Game maps as spec
  11.1/11.2, one `[translation.ko]` each: LB2 `messages=KO/MESSAGE.MAP, KO/RESOURCE.MSG`; LB1, Camelot the same plus
  `strings=KO/SCI-KO.STR`; KQ1 the same plus `dir=KO` (TEXT.000, KOREAN.UNI); MI2 `trs=KO/KOREAN.TRS`, `dir=KO`. `name=`
  in ASCII (spec 5.4).
- [ ] **Step 4: Generators.** `makemaps.py` writes language maps without `[text] encoding` and gains
  `--game-map <dir> --lang ko --encoding cp949 --map data:X.MAP [--trs ...|--messages ...] [--strings ...] [--dir KO]`
  that writes a manifest-only game map; `inifix.py` writes targets without `language=en` for originals and without a
  separate Korean `path=`. `tools/korean/README.md`: the two commands.
- [ ] **Step 5: Run the tests.** Expected: pass; only the baseline failure.
- [ ] **Step 6: Commit** `DISTS: Game maps that declare Korean; language maps without [text] encoding` (maps, test) and
  `TOOLS: Generate game maps and one-folder targets` (tools).

---

### Task 9: Harness on the one-folder layout

**Files (harness repo `/home/thkim/work/scummvm`):**
- Create: `harness/dos/langdirs.py`
- Modify: `harness/dos/m1_accept.py`, `harness/dos/m2_accept.py`, `harness/dos/m5_accept.py`, `harness/dos/scummgame.py`

**Interfaces:**
- Consumes: Task 8 game maps.
- Produces: `langdirs.stage(game) -> path` building, under `~/work/scummvm/runs/langdirs/<GAME>`, the original folder
  plus `KO/` and the game maps (with `map=` rewritten to absolute paths of `dos/dists/engine-data/hires_text/dos/*.MAP`
  for Linux runs; DOS runs copy the maps into the run's `DATA\` and keep `data:`), from the sources the scripts use
  today: KQ1 = `dist/scummvm-sci-i18n-ef8dc87f-win64/gamedata/kq1-ko` (English volumes to the root; `text.*`,
  `sci-ko.str`, `korean.uni` to `KO/`; game map `strings=KO/sci-ko.str`, `dir=KO`, `encoding=utf-8`); MI2 =
  `gamedata/mi2kor` (`korean.trs`, `korean0*.fnt` to `KO/`). LB1 stays on `lb1-ko-patch` (an in-place Korean original,
  detected by its kept entry: `language=ko` there is spec 6 row 1).

- [ ] **Step 1:** `langdirs.py` with a `--selftest` that stages KQ1 and MI2 into a temp dir and asserts: no `text.*`,
  `sci-ko.str`, `korean.uni`, `korean.trs` or `korean*.fnt` at the root; the game map parses (`configparser`) with
  `[translation.ko]`. Run `python3 harness/dos/langdirs.py --selftest` - fails before the file exists, passes after.
- [ ] **Step 2:** `m1_accept.py`/`m2_accept.py`: KQ1 from `langdirs.stage("KQ1")`; ini `language=ko` and
  `hires_text_map=HIRESTXT.MAP` (the game map, both presets) instead of the language map; the L runs keep
  `render_target=clut8` (unify Task 15). `scummgame.py`: `mi2ko` / `mi2kol` use the staged MI2 folder and its game map
  (`mi2kol` with `render_target=clut8`); `mi2` drops `language=en`.
- [ ] **Step 3:** Run `python3 harness/dos/m1_accept.py x` and `python3 harness/dos/m5_accept.py x` (controller's
  permission). Expected: PASS.
- [ ] **Step 4: Commit** `harness/dos: stage one-folder language layouts for the acceptance runs`.

---

### Task 10: Docs

**Files:**
- Modify: `graphics/hires_text/README.md` (new section "Languages": spec 3-6 and 10 in user terms, the two examples of
  spec 11), `tools/korean/TRANSLATION_PIPELINE.md` (sections 1.1, 1.2, 1.5, 2: file names no longer carry the language;
  the game map does; the removed entries), `engines/scumm/HIRES_TEXT_SETUP.md` (the `korean.trs` placement),
  `docs/superpowers/specs/2026-09-30-map-languages-design.md` (only if a task changed a warning text: make the spec match
  the tests)

- [ ] **Step 1:** Write the sections. **Step 2:** Old-name scan:
  ```bash
  cd /home/thkim/work/scummvm/dos && git grep -nE "korean\.trs|sci-<lang>|sci-ko\.str|Text\.MAP|ADGF_UTF8I18N|language=en" -- '*.md' ':!docs/superpowers/plans/*' ':!.superpowers/*'
  ```
  Expected: only explanatory "was / now" lines in the README's conversion notes and the pipeline doc's history section.
- [ ] **Step 3: Commit** `DOCS: Game languages declared by the hi-res text map`.

---

### Task 11: Regression gates

**Files:** none (fix commits only if a gate fails, in the failing task's area and prefix).

- [ ] **Step 1: Rebuild** every Linux build dir and both DOS EXEs (commands of unify Task 17 Step 1).
- [ ] **Step 2: Linux unit tests** in `linux-dos-test`, `linux-dos-test-scumm`, `linux-dos-test-ags`: only the baseline
  failure; record counts.
- [ ] **Step 3: SCI suites** `loading m0 m1 m2 m3` on `x` and `staging`: PASS (M3 rule as in Global Constraints).
- [ ] **Step 4: M5** `x --fresh` and `staging`: PASS.
- [ ] **Step 5: Same pixels as before.** Compare every `.bin` dump under `runs/dos-m1`, `runs/dos-m2`, `runs/dos-m5`
  against `runs/langs-base/` with the script of unify Task 17 Step 5 (base `langs-base`). Expected: **no** `DIFF` and no
  `GONE`: the translations, fonts and maps are the same bytes, only their places and declarations moved. English MI2
  (`mi2`) points included: the manifest-only game map draws as no map.
- [ ] **Step 6: LB2** on Linux from the Task 12 layout (or the Task 2 staging if Task 12 has not run): the Task 4 count
  equals `runs/langs-base/lb2-ko.log`'s.
- [ ] **Step 7: Ledger** - counts, PASS lines, comparisons.

---

### Task 12: Regenerate the packages (LB2 English + Korean in one folder)

**Files (harness repo):** `harness/dos/release/build_sci_lb2.py`, `harness/dos/release/repack_sci.py`,
`harness/dos/release/build_scumm.py`, `harness/dos/release/README.scumm.tmpl`, `harness/dos/release/KOREAN.scumm.tmpl`;
zips in `/home/thkim/work/scummvm/dist/`.

**Interfaces:**
- Consumes: Task 8 game maps and language maps, Task 11's EXEs.
- Produces: `scummvm-dos-sci-{lb2,lb1,kq1,camelot}-<sha>-lang.zip`, `scummvm-dos-scumm-{mi1,mi2}-<sha>-lang.zip`.

- [ ] **Step 1: Coordination check** (`git -C /home/thkim/work/scummvm status --short harness/dos/release` clean).
- [ ] **Step 2: LB2.** `build_sci_lb2.py`: `GAMES\LB2\` = the 114 English DOS CD files (one `RESOURCE.AUD`) +
  `GAMES\LB2\KO\MESSAGE.MAP`, `KO\RESOURCE.MSG` (the patch's two files, unmodified: keep the `EXPECT_MD5_5000`/`EXPECT_SIZE`
  check for `RESOURCE.MSG`, now at `KO\`) + `HIRESTXT.MAP` from `dists/.../games/LB2/`. Drop the
  "Layout ruling ... option (c)" docstring paragraph: one folder serves both now. `DATA\` = `LB2KO.MAP`,
  fonts, `ENCODING.DAT`, licences; `lb2kol` carries `render_target=clut8`. `SCUMMVM.INI` = spec 11.1 (`lb2`, `lb2ko`, `lb2kol`; `lastselectedgame=lb2ko`);
  `EXAMPLE.INI` shows `lb2` and says "choose Korean under Edit Game > Language, or `language=ko`"; BATs `LB2.BAT`,
  `LB2KO.BAT`, `LB2KOL.BAT`; README: the folder layout of spec 11.1 and that speech is English.
- [ ] **Step 3: LB1, KQ1, Camelot** (`repack_sci.py`): one `GAMES\<G>\` per game = the English folder + `KO\` with the
  Korean folder's extra files (`RESOURCE.MSG`, `MESSAGE.MAP`, `SCI-KO.STR`; KQ1 also `TEXT.000`, `KOREAN.UNI`), the
  Korean folder dropped; verify first that every other file of the Korean folder is byte-identical to the English one
  (Task 0 found them identical; fail the build otherwise). Targets `<g>`, `<g>ko`, `<g>kol` on the one path.
- [ ] **Step 4: MI2, MI1** (`build_scumm.py`): MI2 as spec 11.2 (`KOREAN.TRS`, `KOREAN0*.FNT` into `KO\`); `mi2` without
  `language=en`. MI1 keeps its layout (Korean original), new EXE only.
- [ ] **Step 5: Verify.** `unzip -t` each; `harness/dos/release/verify_scumm.sh` on the SCUMM zips; for every zip:
  `unzip -l <zip> | grep -cE "GAMES/[^/]+KO/"` prints `0`, and `unzip -p <zip> '*SCUMMVM.INI' | grep -E "language=en|dos_truecolor|rgb_rendering"`
  prints nothing. LB2 zip size is within 5% of the unify Task 18 LB2 zip (one `RESOURCE.AUD`).
- [ ] **Step 6: Smoke** from the unpacked packages, DOSBox-X: `LB2KO.BAT` first Korean message, `LB2.BAT` the same
  point in English, MI2 `mi2ko` difficulty card, KQ1 L title. Look at each capture.
- [ ] **Step 7: Record** names, sizes, sha256 in the ledger. Old zips stay. Commit the harness changes:
  `harness/dos: one-folder language packages (LB2 English + Korean)`.

---

## Self-review notes (for the executor)

- Spec coverage: 3 (keys, validation, `dir`) -> Tasks 1, 4, 6; 4 (game/language map, manifest-only, presets) -> Tasks
  1, 4, 6, 8; 5.1-5.3 -> Tasks 1, 2, 5; 5.4 -> Task 7; 6 -> Tasks 1, 3, 5; 7.1 -> Tasks 5, 6; 7.2 -> Tasks 3, 4; 7.3 ->
  Task 1; 8 -> Tasks 2, 3, 5; 9 -> Tasks 1, 3, 5, 6 (behaviour) and 10 (docs); 10 -> the tests of Tasks 1, 2, 3, 5, 6;
  11 -> Tasks 8, 12.
- Names used across tasks: `HiResTranslation`, `HiResLanguageManifest`, `readLanguageManifest(File)`,
  `resolveGameMapPath`, `defaultGameMapPath`, `chooseHiResLanguage`, `hiResLanguageTags`, `kHiResTranslationSci/Scumm`,
  `kHiResLanguageMap`, `kHiResKeyTranslation`, `sciTranslationUsable`, `sciMapLanguageTags`, `sciTextRules`,
  `Scumm::translationUsable`, `checkTrsBundle`, `checkTrsEncoding`, `openTranslationFile`, `getLanguageLabels`,
  `languagePopupLabel`, `languageForMatching`.
