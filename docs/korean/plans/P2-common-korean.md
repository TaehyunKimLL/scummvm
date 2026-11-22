# P2: Common Korean — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task by task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** the engine-neutral Korean pieces of DESIGN.md section 11 row P2 ("common Korean"): the 2-set Hangul composer
and the particle picker (`common/hangul`), the parser stemmer (`common/korean_stem`) with its 1,199-line corpus, the
parser word table reader at version 3 (`common/korean_vocab`: u32 offsets, fingerprint header) next to version 2, the
language-folder detection helper (`common/language_folder`), the Han/Yeong and Hanja key codes with their keymapper
names, and `tools/korean/k6bake.py`, which rebuilds the harness's KQ1 table byte for byte. No engine is touched: SCI
uses these in P3, AGI in P5.

**Architecture:** everything lives in `common/` (namespace `Common`), is free of engine, GUI and backend knowledge, and
is tested in `test/common/` (globbed by `test/module.mk`). Ported unchanged from branch `jamo` (read with `git show`,
never checked out): `korean_stem.{h,cpp}`, `hangul.{h,cpp}` (one doc paragraph corrected, one function added) and their
tests. Rewritten: `korean_vocab` (v2 + v3 reader, sorted-order and bounds checks at load, fingerprint check),
`language_folder` (a name pattern instead of a prefix, deterministic order). New: `translation_file` (the
`#! format=1 ...` header line shared by `.str` files (P3, P5) and v3 word tables), `normalizeKoreanInput()`, the
keycodes, `k6bake.py` + its tests.

**Tech stack:** C++11 ScummVM code, CxxTest (`make test`), Python 3 standard library for the tool.

Source of scope: `docs/korean/DESIGN.md` section 11 row P2 (and rows P1/P3 for what P2 must not do), sections 3 (charset
policy: code points in, UTF-8 or CP949 out at the caller), 4 (translation resources, fingerprint header), 6 (parser
games), 7 (keys), 10 (tests), 12 decisions 4, 5, 7.

---

## 0. Facts checked for this plan (2026-11-22, this worktree)

Tree `/home/thkim/work/scummvm-2026.4-korean`, branch `ko-2026.4` at `d6b3b963ae8` (P1 landed; `make test` in
`~/work/tmp/scummvm2026-p1verify` gives `Running cxxtest tests (534 tests)...OK!`). `jamo` is at `b4912e3c600`.

**Prototype.** Every file of this plan was written and run before the plan, outside the worktree:

- `~/work/tmp/scummvm2026-p2-proto/` (`Makefile` links against `~/work/tmp/scummvm2026-p1verify`): 74 new tests pass,
  also under `-fsanitize=address,undefined`; `tools/korean/test_k6bake.py`: 8 tests OK.
- `~/work/tmp/scummvm2026-p2-tree/` (a `git archive` of `d6b3b963ae8` with the prototype files and the two `module.mk`
  edits) built out of tree in `~/work/tmp/scummvm2026-p2-treebuild`: `make test` → `Running cxxtest tests (608
  tests)...OK!`, no compiler warning from a P2 file; `make scummvm` (SDL2, all engines disabled) links, so
  `sdl2-events.cpp` and `hardware-input.cpp` compile with the edits.

The code blocks below are that prototype (generated from it; GPL headers omitted as in P1, the implementer adds the
header of `common/language.cpp:1-20`). Where plan text and a code block disagree, the code block wins.

Numbers it proved, used as expected values:

| Fact | Value |
|---|---|
| harness KQ1 table (`dos/release/kq1/translation/sci-ko.dat.vocab.tsv` master, the shipped `ko/sci-ko.dat`) | v2, 1,265 entries, 16,558 B, sha256 `48cc43e4720ef98b4790366c6cecb3c2cf4e4f091a9ac3021520e0882645ed75` (= `translations.tsv`) |
| `k6bake.py build --format 2` of all nine harness masters (camelot, kq1, kq4, lb1, lsl1a, lsl2, pq2, qfg1e, qfg2) | sha256 equal to `translations.tsv` column 5 for every one |
| `k6bake.py build --format 2` of the five `k3map.json` + `k2targets.json` (kq4, pq2, qfg1e, qfg2; lsl1a `--magic AGIKOR --verbs`) | the same nine-table sha256s (`3e03b854`, `9bcab2d8`, `e80717c8`, `977975ef`, `05c11821`...) |
| KQ1 as v3 (`--game kq1`, `vocab=` given: a 115-character header line padded to 116 bytes) | 21,738 B; `convert --format 2` of it gives the v2 file byte for byte |
| v3 layouts of the nine masters | 20,116 (kq4) .. 93,738 B (camelot); no blob near 65,535 code units yet (camelot 14,645) |
| C++ reader on the nine harness tables (dev-data test, v2 and v3) | every dumped row found by `lookupExact`, in order |
| `test/korean_vocab_golden.h` from `tools/korean/testdata/mini.vocab.tsv` | sha256 `29b7aa43dd19cdc3053482de84fe93687107062ffa9849dea26d4763cd9d6825` |

ScummVM APIs used (file:line in this tree):

| API | Where |
|---|---|
| `File::open(const Path &)` 75 | `common/file.h` |
| `SeekableReadStream::size` 761, `read` 417, `eos` 400, `readLine(bool)` 837 | `common/stream.h` |
| `MemoryReadStream(const byte *, uint32, DisposeAfterUse::Flag = NO)` 73 | `common/memstream.h` |
| `READ_LE_UINT16`/`READ_LE_UINT32` 492/496 (also 354/355 macros) | `common/endian.h` |
| `warning` 101 | `common/textconsole.h` |
| `debugC(int, uint32, const char *, ...)` 92 (no-op stub 37 without debug) | `common/debug.h` |
| `FSNode(const Path &)` 108, `getChildren(FSList &, ListMode, bool)` 149, `getChild` 141, `getName` 168, `isDirectory` 221, `createReadStream` 262, `kListFilesOnly` 87, `kListAll` 89 | `common/fs.h` |
| `Path(const char *, char)` 251, `toString(char)` 274, `getParent` 287, `join(const char *, char)` 499 | `common/path.h` |
| `String::equalsIgnoreCase` 106, `hasSuffix` 112, `hasSuffixIgnoreCase(const char *)` 114, `hasPrefix` 117, `hasPrefixIgnoreCase(const char *)` 119, `format` 158, `decode(CodePage)` 174 | `common/str.h` |
| `BaseString::npos` 36, `compareTo` 128/129, `deleteLastChar` 142, `clear` 154, `lastChar` 183, `contains(value_type)` 212, `findFirstOf(value_type, size_t)` 235, `toLowercase` 358 | `common/str-base.h` |
| `U32String(const char *, CodePage)` 80, `U32String(const String &, CodePage)` 89, `u32char_type_t` 55 | `common/ustr.h` |
| `convertToU32String` 71, `convertFromU32String` 72 | `common/str-enc.h` |
| `ARRAYSIZE` 114 (an `int`), `isAlnum` 265, `isAlpha` 277, `isDigit` 289, `isXDigit` 301, `MIN` 63 | `common/util.h` |
| `Array::insert_at(size_type, const T &)` 242, `data()` 208/213 | `common/array.h` |
| `ScopedPtr` 572 | `common/ptr.h` |
| `KeyCode` enum, `KEYCODE_AUDIOFASTFORWARD = 356` 239, `KEYCODE_LAST` 241, `KeyState(KeyCode)` 325 | `common/keyboard.h` |
| `Event` 218 | `common/events.h` |
| `defaultKeys[]` (table 85-233 of the .cpp, entry `AUDIOFASTFORWARD` 216), `defaultModifiers[]` 235-241 (`"S"` = `KBD_SHIFT`), `KeyboardHardwareInputSet(const KeyTableEntry *, const ModifierTableEntry *)` 289, `findHardwareInput(const String &)` 296, `findHardwareInput(const Event &)` 343 | `backends/keymapper/hardware-input.cpp` |
| `HardwareInput` (`id` 56, `description` 59, `type` 62, `key` 76, `createKeyboard` 85), `KeyTableEntry` 148, `ModifierTableEntry` 157, `extern defaultKeys` 317, `defaultModifiers` 320 | `backends/keymapper/hardware-input.h` |
| `SdlEventSource::SDLToOSystemKeycode`: `case SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_GRAVE)` | `backends/events/sdl/sdl2-events.cpp:326`, `sdl3-events.cpp:315` |
| `SDL_SCANCODE_LANG1 = 144` ("Hangul/English toggle"), `LANG2 = 145`; no `SDLK_` keycode with those values (no duplicate `case`) | `/usr/include/SDL2/SDL_scancode.h:258-259` (SDL 2.30.0); SDL3: UNVERIFIED (not installed) |
| `install_null_g_system` 5, `uninstall_null_g_system` 6 | `test/system/null_osystem.h` |
| `TS_ASSERT*`, `TSM_ASSERT*`, `TS_ASSERT_LESS_THAN` | `test/cxxtest/cxxtest/TestSuite.h` |
| `TESTS := $(srcdir)/test/common/*.h ...` 8, `TEST_LIBS` 19-46 (POSIX/WIN32 objects, libraries) | `test/module.mk` |
| cxxtestgen includes a suite by absolute path in an out-of-tree build (so `__FILE__` names the source tree; P1's `hires_text_shipped_maps.h:38-40` relies on it); a quoted include in `test/foo.h` of `"common/array.h"` finds `test/common/array.h` first (the array **test suite**): the fixture uses `<common/...>` as `jamo` did | generated `test/runner.cpp`; found in the tree build |
| `MODULE_OBJS` of `common/module.mk` 3-52, alphabetical, one object a line | `common/module.mk` |
| `devtools/check-commit-msg.py` takes a message file and wants `^[A-Z_]+:` | `devtools/check-commit-msg.py:8-17` |
| `getenv` is forbidden in tests: developer data comes from a directory in the working directory (P1's `hires-maps/`) | `common/forbidden.h:256-258` |

P0/P1 interfaces P2 relies on (Task 0 re-checks them; nothing else of P0/P1 is used, so the P1 review cannot change P2
except through these):

| Interface | Where | Used by |
|---|---|---|
| `const char *getLanguageFolderCode(Language)` ("" for UNK_LANG; "zh" for every Chinese, "pt", "en" ...) | `common/language.h:112`, `.cpp:168` | `translationTableName`, `translationFolderLanguages` |
| `Language parseLanguageFolderCode(const String &)` (any case; generic language of a shared code; UNK_LANG for "" and unknown) | `common/language.h:115`, `.cpp:175` | `translationFolderLanguages` |
| P1's conventions: `test/common/*.h` glob; `__FILE__`-relative source-tree paths in tests; an optional data directory in the working directory | `test/module.mk:8`, `test/graphics/hires_text_shipped_maps.h:38-40, 86-89` | `korean_stem.h` corpus, `language_folder.h`, `korean_vocab.h` |

P2 does not use `graphics/svf`, `graphics/hires_text`, `SvfFace`, `SvfChain` or the map code.

## 1. Scope decisions and conflicts with DESIGN.md (listed again in the report)

1. **Done-criterion vs version 3.** Row P2: "`k6bake.py` in `tools/korean` rebuilds `SCIKOR.DAT` byte-identical to the
   harness copy for KQ1". The harness copy is version 2 (16,558 B) and row P2 also asks for version 3, whose bytes
   necessarily differ; and KQ1 has no `k3map.json` (harness `dos/release/SCIKOR.md`: "Camelot, KQ1, LB1 and LSL2 have
   no k3map.json"): since 2026-10-17 every table is built from a UTF-8 master `<game>/translation/sci-ko.dat.vocab.tsv`
   by the harness's `trs_master.py`. Resolution: `tools/korean/k6bake.py` reads the master (the source of all nine
   tables) and still reads `k3map.json` (the re-make path); `--format 2` reproduces the harness KQ1 table byte for byte
   (and all nine, `harness-check`); `--format 3` (default) writes the new layout, and `convert` turns one version into
   the other with identical rows (KQ1 v3 -> v2 gives the harness bytes again).
2. **Directories beyond the row.** Row P2 lists `common`, `backends/keymapper`, `test/common`. P2 also writes
   `tools/korean/` (the done-criterion's `k6bake.py`, decision 7), `test/` (two helper headers outside the
   `test/common/*.h` glob) and `backends/events/sdl/sdl{2,3}-events.cpp` (+3 lines each): without the SDL mapping
   `KEYCODE_HANGUL` is produced by no desktop backend, so neither a user nor P3's keymap can use the key. If the owner
   wants the SDL lines in P3 instead, drop Task 3; nothing else depends on it.
3. **Decomposed input.** Section 6: "a decomposed input is composed by `HangulComposer::composeSyllable`". That method
   is private and composes only the composer's own state; nothing in `jamo` composes conjoining jamo from an IME.
   P2 adds `Common::normalizeKoreanInput(U32String &)`: Unicode Hangul composition of U+1100.. jamo, full-width ASCII
   and U+3000 folded to ASCII (the section's "full-width ASCII folded"); case folding stays the engine's
   (`parserLowerCaseWord`, P3).
4. **Version 3 layout** is not specified by DESIGN beyond "u32 offsets, the same fingerprint and the vocab's md5".
   Defined here (`common/korean_vocab.h`): the v2 head, a u32 header size, the header **text** line (same syntax as the
   `.str` header plus `vocab=`), 12-byte entries `{u32 offset, u16 length, u16 group, u16 class, u16 reserved}`.
   "The vocab's md5" is read as the md5 of the game's vocabulary resource the groups come from (`vocab.000`/`vocab.900`,
   `WORDS.TOK`). Which game files make up `fingerprint=` is the engines' definition (P3 SCI, P5 AGI); P2 treats it as
   32 opaque hex digits, and `k6bake.py` takes it as an argument.
5. **The header parser is common and in P2.** Row P3 owns "`ScriptStrings` + fingerprint"; the v3 table needs the same
   header now, so `common/translation_file.{h,cpp}` (`TranslationFileHeader::parse/toString`,
   `checkTranslationHeader`) is added in P2 and P3/P5 read their `.str` first lines with it. Policy of decision 4: a
   file without a header loads with one warning; a header that differs in game, engine, fingerprint or vocab is refused
   with one warning naming each differing field and both values.
6. **File names.** DESIGN says `ko/SCIKOR.DAT`, `ko/AGIKOR.DAT`; the harness packs ship `ko/sci-ko.dat`,
   `ko/agi-ko.dat` since 2026-10-17 (lower case; `jamo` d3870b86e86 reads those first, then the old names). The P2
   loader takes any name; P3/P5 pick (recommended: `<engine>-<code>.dat`, then `SCIKOR.DAT`/`AGIKOR.DAT`).
7. **`language_folder`.** P1 already added `getLanguageFolderCode`/`parseLanguageFolderCode` (P1 plan section 1 item
   8). P2 adds `translationFolderLanguages()`, rewritten: a table name **pattern** (`"sci-%s.str"`, `"agi-%s.str"`,
   `"%s.trs"`) instead of `jamo`'s prefix, so SCUMM's `.trs` (section 4) fits, and the result sorted by folder code
   (`jamo` returned file-system order). `jamo`'s `test/common/language_folder.h` only tested the folder codes, which
   P1's `language_folder_code.h` covers; it is not ported. Detection variants (`-ko`) are P3.
8. **Not ported from `jamo`'s common diff** (section 2.5/8 place them elsewhere): lazy `encoding.dat` tables and
   `test/common/encoding_cjk_tables.h` (DOS memory, P7), `common/file-cache-stats.h` (P7), `setStreamUnbufferer` (P7),
   `VI_VNM` (not in 2026.4, P1 decision), `config-manager` and `base/` changes (`--set`, P7/P8), `DISABLE_GUI`
   confirm-exit (P7), the DOS keyboard (`backends/events/dos`, P7). The plural-"s" drop of SCI `kFormat` is P3.
9. **Keymapper scope.** P2 adds the key names `HANGUL`/`HANJA` only; the `HANGULTOGGLE` action and its default bindings
   (HANGUL, RALT, C+SPACE, S+SPACE, SCROLLOCK, F12; section 6) are engine keymaps (P3, P5).
10. **Composer doc.** `jamo`'s `hangul.h` says no IME text can arrive and that SCI writes EUC-KR into its heap; both are
    wrong under DESIGN section 3/6 (OS IME syllables arrive as KEYDOWN with the code point; heap encoding is the
    engine's `TextMode`). The paragraph is corrected; the code is unchanged.
11. **Vocabulary reader hardening** (beyond `jamo`): a table whose entries are not sorted is refused at load (the
    binary search would silently miss words), as is an empty entry or a file over 16 MB; version 1 (EUC-KR) is refused
    as in `jamo`.
12. **Keys of section 7** read in P2: none. `hangul_input`, `hangul_start`, `hangul_builtin` are read by the engines'
    event code (P3/P5), `text_encoding` by `TextMode` (P3).
13. Branch: commit on `ko-2026.4` directly, as P0 and P1 did, unless the owner names the topic branch
    `ko/02-common-korean` (DESIGN section 9).

## 2. Files

| File | New/Mod | Purpose |
|---|---|---|
| `common/keyboard.h` | mod | `KEYCODE_HANGUL` 357, `KEYCODE_HANJA` 358 |
| `backends/keymapper/hardware-input.cpp` | mod | key names `HANGUL`, `HANJA` |
| `backends/events/sdl/sdl2-events.cpp`, `sdl3-events.cpp` | mod | `SDL_SCANCODE_LANG1/2` -> the two key codes |
| `common/hangul.{h,cpp}` | port (+ edits) | `HangulComposer`, `resolveKoreanParticles`, `normalizeKoreanInput` |
| `common/korean_stem.{h,cpp}` | port | `koreanStemCandidates`, `koreanParticleRole`, `KoreanRole`, `KoreanStems` |
| `common/translation_file.{h,cpp}` | new | `TranslationFileHeader`, `checkTranslationHeader`, `isMd5String` |
| `common/korean_vocab.{h,cpp}` | rewritten | `KoreanVocabulary` v2 + v3 |
| `common/language_folder.{h,cpp}` | rewritten | `translationTableName`, `translationFolderLanguages` |
| `common/module.mk` | mod | 5 objects |
| `test/module.mk` | mod | `TEST_LIBS += backends/keymapper/hardware-input.o` |
| `test/common/keyboard_hangul.h` | new | 2 tests |
| `test/common/hangul.h`, `korean_particles.h` | port | 17 + 2 tests |
| `test/common/korean_normalize.h` | new | 3 tests |
| `test/common/korean_stem.h` (+ `korean_stem_corpus.txt`) | port (1 edit) | 30 tests |
| `test/common/translation_file.h` | new | 6 tests |
| `test/common/korean_vocab.h` | new (from `jamo`'s cases) | 11 tests |
| `test/korean_vocab_fixture.h` | new (from `jamo`'s) | v2/v3 tables built in memory |
| `test/korean_vocab_golden.h` | generated | `k6bake.py golden` of the mini master |
| `test/common/language_folder.h` + `test/common/language_folder_game/` (10 small files) | new | 3 tests |
| `tools/korean/k6bake.py`, `tools/korean/test_k6bake.py`, `tools/korean/testdata/mini.vocab.tsv` | new | the baker (decision 7) and its 8 tests |
| `docs/korean/PATCHES.md` | mod | rows for `ko/02-common-korean` |
| `docs/korean/plans/P2-notes.md` | new (last task) | deviations and acceptance run |

Size: ported 2,349 lines (`korean_stem` 910, `hangul` 467 before the additions, their tests 972) plus the 1,199-line
corpus; new or rewritten 1,696 lines of C++ (`translation_file` 268, `korean_vocab` 462, `language_folder` 144,
`hangul` additions 40, fixture and golden 180, new tests 602) and 521 lines of Python (`k6bake.py` 387, its test 134).
Row P2 says "~2.5k reused": matches.

Style for every new file: the GPL header of `common/language.cpp:1-20` (code blocks omit it; `test/common/*.h` suites
have none, as upstream's `test/common/array.h`), tabs, `namespace Common {` ... `} // End of namespace Common`,
`_camelCase` members, short comments. Ported files keep `jamo`'s bytes except the edits named in their task.

## 3. Environment (every task)

```sh
SRC=/home/thkim/work/scummvm-2026.4-korean
OLD=/home/thkim/work/scummvm-korean           # read only: git -C $OLD show jamo:<path>; never check out
HAR=/home/thkim/work/scummvm/harness          # read only
BLD=~/work/tmp/scummvm2026-p2build            # out of tree; never configure inside $SRC
mkdir -p $BLD && cd $BLD && $SRC/configure --disable-all-engines --enable-debug --disable-optimizations >/dev/null
~/.local/bin/heavy make -j16 test             # now: "Running cxxtest tests (534 tests)...OK!"
./test/runner <SuiteName>                     # one suite, e.g. ./test/runner KoreanVocabTestSuite
```

`heavy` is `~/.local/bin/heavy` (the host's OOM guard). `configure` is not changed by P2; new `.cpp` files are picked up
from `common/module.mk`. Python: always `python3 -I` (isolated mode); the tools add their own directory to `sys.path`.

Commit after each task (`git -C $SRC add <files> && git -C $SRC commit -F <msgfile>`), message: subject `AREA: Subject`
(capitals, `devtools/check-commit-msg.py` rule), a short body, and as the last line exactly `Assisted-by: Claude Code`
(no `Co-Authored-By`).

---

## Task 0: Preconditions (5 min, no commit)

- [ ] `git -C $SRC log --oneline -1` is `d6b3b963ae8` or a descendant; `git -C $SRC status --short` shows nothing but
  untracked `docs/korean/plans/P2-common-korean.md` (and `P1-review.md` if the review has landed as a file).
- [ ] If `docs/korean/plans/P1-review.md` exists, read it for findings on `common/language.*`; a change there
  (renamed function, different code for a language) must be carried into Task 12's code before starting it.
- [ ] Interfaces and anchors (each command prints one line; if one prints nothing, stop and compare with section 0):

```sh
cd $SRC
grep -n 'extern const char \*getLanguageFolderCode(Language id);' common/language.h
grep -n 'extern Language parseLanguageFolderCode(const String &code);' common/language.h
grep -n 'KEYCODE_AUDIOFASTFORWARD = 356,' common/keyboard.h
grep -n '{"AUDIOFASTFORWARD", KEYCODE_AUDIOFASTFORWARD, "Audio fast-forward"},' backends/keymapper/hardware-input.cpp
grep -c 'case SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_GRAVE): return Common::KEYCODE_TILDE;' backends/events/sdl/sdl2-events.cpp backends/events/sdl/sdl3-events.cpp
grep -n 'void install_null_g_system();' test/system/null_osystem.h
git -C $OLD rev-parse jamo                    # b4912e3c6007a859db5086427a5ff95132a81e16
ls $HAR/dos/release/translations.tsv $HAR/dos/release/kq1/translation/sci-ko.dat.vocab.tsv
```

(the `grep -c` line prints `...sdl2-events.cpp:1` and `...sdl3-events.cpp:1`.)

- [ ] If `jamo` moved, the ported files may differ from the prototype: compare
  `git -C $OLD diff b4912e3c600 jamo -- common/hangul.* common/korean_stem.* test/common/hangul.h test/common/korean_particles.h test/common/korean_stem*`
  (empty means unchanged); port from `b4912e3c600` explicitly (`git -C $OLD show b4912e3c600:<path>`) to stay with
  the prototype.
- [ ] Baseline: run the section 3 block; expect `534 tests ... OK!`. If the count differs, write it down as N0: every
  expected count below is N0 + the tests added so far (534 assumed).

## Task 1: Han/Yeong and Hanja key codes (10 min)

- [ ] **Step 1: test** — `test/common/keyboard_hangul.h`:

```cpp
#include <cxxtest/TestSuite.h>

#include "common/keyboard.h"

#include "test/system/null_osystem.h"

// The Han/Yeong and Hanja keys: key codes of their own (DESIGN.md section 6).
class KeyboardHangulTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override {
		Common::install_null_g_system();
	}

	void tearDown() override {
		Common::uninstall_null_g_system();
	}

	// Codes of their own after the upstream ones (a keymap stores the names below, not the numbers).
	void test_keyCodes() {
		TS_ASSERT_EQUALS((int)Common::KEYCODE_HANGUL, 357);
		TS_ASSERT_EQUALS((int)Common::KEYCODE_HANJA, (int)Common::KEYCODE_HANGUL + 1);
		TS_ASSERT_LESS_THAN((int)Common::KEYCODE_AUDIOFASTFORWARD, (int)Common::KEYCODE_HANGUL);
		TS_ASSERT_LESS_THAN((int)Common::KEYCODE_HANJA, (int)Common::KEYCODE_LAST);
	}
};
```

- [ ] **Step 2: run, expect a compile failure** (`KEYCODE_HANGUL` is not a member of `Common`):
  `cd $BLD && heavy make -j16 test`.
- [ ] **Step 3: implement** — in `common/keyboard.h`, after `KEYCODE_AUDIOFASTFORWARD = 356,` (line 239) and its
  following blank line, before `KEYCODE_LAST`:

```cpp
	// Input-method keys of CJK keyboards. SDL reports them as their own
	// scancodes (LANG1, LANG2): the key right of the space bar of a Korean
	// keyboard is Han/Yeong, which Windows delivers as a key of its own where
	// X11 often reports right Alt. Without codes they are KEYCODE_INVALID.
	KEYCODE_HANGUL           = 357,	// Han/Yeong: Korean input mode toggle
	KEYCODE_HANJA            = 358,	// Hanja conversion

```

- [ ] **Step 4: run** — `heavy make -j16 test` → `535 tests ... OK!`.
- [ ] **Step 5: commit**

```
COMMON: Add key codes for the Han/Yeong and Hanja keys

A Korean keyboard has a Han/Yeong key (input mode toggle) and a Hanja
key. SDL reports them as their own scancodes, but without key codes
they reach the engines as KEYCODE_INVALID.

Assisted-by: Claude Code
```

## Task 2: Keymapper names for the two keys (10 min)

- [ ] **Step 1: test** — in `test/common/keyboard_hangul.h` add `#include "backends/keymapper/hardware-input.h"`
  before `#include "common/keyboard.h"`, change the class comment to the one below, and add after `test_keyCodes()`:

```cpp
// The Han/Yeong and Hanja keys: key codes of their own, and names a keymap can bind (DESIGN.md section 6).
```

```cpp

	void test_keymapperNames() {
		Common::KeyboardHardwareInputSet keys(Common::defaultKeys, Common::defaultModifiers);
		const Common::HardwareInput hangul = keys.findHardwareInput("HANGUL");
		TS_ASSERT_EQUALS(hangul.type, Common::kHardwareInputTypeKeyboard);
		TS_ASSERT_EQUALS(hangul.key.keycode, Common::KEYCODE_HANGUL);
		TS_ASSERT_EQUALS(hangul.description, Common::U32String("Han/Yeong"));
		TS_ASSERT_EQUALS(keys.findHardwareInput("HANJA").key.keycode, Common::KEYCODE_HANJA);
		// The key with a modifier, as the engines' default bindings name it (S+HANGUL).
		TS_ASSERT_EQUALS(keys.findHardwareInput("S+HANGUL").key.flags, (byte)Common::KBD_SHIFT);
		// And back: the event a backend sends for the key finds it.
		Common::Event e;
		e.type = Common::EVENT_KEYDOWN;
		e.kbd = Common::KeyState(Common::KEYCODE_HANGUL);
		TS_ASSERT_EQUALS(keys.findHardwareInput(e).id, "HANGUL");
	}
```

- [ ] **Step 2: run, expect a link failure** (`undefined reference to Common::defaultKeys`): the test runner does not
  link the keymapper. In `test/module.mk`, before `ifdef USE_TINYGL` (line 41), add:

```make
# The Han/Yeong key names of the keymapper (test/common/keyboard_hangul.h)
TEST_LIBS += backends/keymapper/hardware-input.o

```

  Run again: it links, and `test_keymapperNames` fails (`findHardwareInput("HANGUL")` is empty).
- [ ] **Step 3: implement** — in `backends/keymapper/hardware-input.cpp`, after the line
  `{"AUDIOFASTFORWARD", KEYCODE_AUDIOFASTFORWARD, "Audio fast-forward"},` (216):

```cpp

	// Input-method keys: listed so that a keymap can bind the Han/Yeong key,
	// and a user without one can bind another key to the same action.
	{"HANGUL", KEYCODE_HANGUL, "Han/Yeong"},
	{"HANJA", KEYCODE_HANJA, "Hanja"},
```

- [ ] **Step 4: run** — `heavy make -j16 test` → `536 tests ... OK!`.
- [ ] **Step 5: commit**

```
KEYMAPPER: Add the Han/Yeong and Hanja keys

Names a keymap can bind the two Korean keyboard keys by, so an engine
can offer a Hangul toggle on Han/Yeong and a user without that key can
bind another one to it. The test runner links the keymapper's key table
to check them.

Assisted-by: Claude Code
```

## Task 3: SDL: deliver the two keys (10 min)

No unit test reaches the SDL event source (the runner has no SDL); the check is that both files compile.

- [ ] **Step 1: implement** — `backends/events/sdl/sdl2-events.cpp`, after
  `case SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_GRAVE): return Common::KEYCODE_TILDE;` (326):

```cpp
	// CJK input-method keys have no SDLK_ names: reached through their
	// scancodes. A Korean keyboard's Han/Yeong key is LANG1.
	case SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_LANG1): return Common::KEYCODE_HANGUL;
	case SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_LANG2): return Common::KEYCODE_HANJA;
```

  `backends/events/sdl/sdl3-events.cpp`, after the same line (315):

```cpp
	// CJK input-method keys; see the same lines in sdl2-events.cpp.
	case SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_LANG1): return Common::KEYCODE_HANGUL;
	case SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_LANG2): return Common::KEYCODE_HANJA;
```

- [ ] **Step 2: compile** — `cd $BLD && heavy make backends/events/sdl/sdl2-events.o` → `C++ backends/events/sdl/sdl2-events.o`,
  rc 0, no new warning from lines 327-330 (the file's existing `-Wreorder` notes are upstream's). `sdl3-events.cpp` is
  not built on this host (no SDL3): UNVERIFIED; the change is the same two `case` labels.
- [ ] **Step 3: run** — `heavy make -j16 test` → `536 tests ... OK!`.
- [ ] **Step 4: commit**

```
SDL: Map the Han/Yeong and Hanja keys

SDL has no keycode names for the LANG1 and LANG2 scancodes, so the
two keys of a Korean keyboard fell through to KEYCODE_INVALID. Map
them to KEYCODE_HANGUL and KEYCODE_HANJA.

Assisted-by: Claude Code
```

## Task 4: Port the Hangul composer and the particle picker (15 min)

- [ ] **Step 1: copy the tests**

```sh
cd $SRC
for f in hangul.h korean_particles.h; do git -C $OLD show b4912e3c600:test/common/$f > test/common/$f; done
```

- [ ] **Step 2: run, expect a compile failure** (`common/hangul.h: No such file or directory`).
- [ ] **Step 3: copy the code, correct the class comment, build it**

```sh
for f in hangul.h hangul.cpp; do git -C $OLD show b4912e3c600:common/$f > common/$f; done
```

  In `common/hangul.h` replace the two paragraphs of the class comment as in this diff (code unchanged; section 1
  item 10):

```diff
--- a/common/hangul.h
+++ b/common/hangul.h
@@ -30,21 +30,19 @@
 /**
  * Two-beolsik Hangul input composer.
  *
- * ScummVM's event layer only reports Common::EVENT_KEYDOWN with an ASCII
- * value; there is no EVENT_TEXTINPUT and no IME composition, so Korean text
- * can never arrive from the operating system's input method. This class does
- * the composition itself: it maps QWERTY keys to jamo and runs the
- * choseong/jungseong/jongseong automaton, emitting precomposed U+AC00
- * syllables.
+ * An operating system input method delivers finished syllables as
+ * Common::EVENT_KEYDOWN with KEYCODE_INVALID and the code point in ascii, but
+ * no composition (pre-edit) events, and a backend without an input method
+ * delivers only keys. This class does the composition itself: it maps QWERTY
+ * keys to jamo and runs the choseong/jungseong/jongseong automaton, emitting
+ * precomposed U+AC00 syllables.
  *
  * The composer keeps the syllable currently being assembled separate from the
  * committed text so the caller can draw it as a live pre-edit character.
  *
  * It is deliberately free of any encoding knowledge: it emits code points, and
- * a caller that needs bytes converts them. The two engines that need Korean
- * input want different encodings - AGI carries UTF-8 in an engine-side buffer,
- * SCI has to write EUC-KR into the game's own heap because that is what its
- * font indexes by - so the conversion cannot live here.
+ * a caller that needs bytes converts them into the encoding its text is kept
+ * in (an engine's text mode: UTF-8, or CP949 for a CP949 pack).
  */
 class HangulComposer {
 public:
```

  In `common/module.mk`, insert `	hangul.o \` before `	hashmap.o \` (line 18).
- [ ] **Step 4: run** — `heavy make -j16 test` → `555 tests ... OK!`;
  `git -C $OLD show b4912e3c600:common/hangul.cpp | diff - common/hangul.cpp` is empty; the `hangul.h` diff is the one
  above.
- [ ] **Step 5: commit**

```
COMMON: Add a Hangul composer and the Korean particle picker

HangulComposer turns QWERTY keys into Hangul syllables (2-set layout),
for backends without an input method and for the engines' parser
prompts. resolveKoreanParticles() picks 을/를, 이/가, 은/는, 과/와 and
(으) after a word inserted into a translated sentence. Both are taken
from the jamo branch with their tests; the class comment now matches
how text reaches the engines.

Assisted-by: Claude Code
```

## Task 5: Normalise typed Korean (10 min)

- [ ] **Step 1: test** — `test/common/korean_normalize.h`:

```cpp
#include <cxxtest/TestSuite.h>

#include "common/hangul.h"
#include "common/str.h"
#include "common/ustr.h"

// normalizeKoreanInput(): what an input method sends, brought to what a word table holds (DESIGN.md section 6).
class KoreanNormalizeTestSuite : public CxxTest::TestSuite {
	static Common::U32String n(const Common::U32String &in) {
		Common::U32String s = in;
		Common::normalizeKoreanInput(s);
		return s;
	}

	static Common::U32String cps(const uint32 *p) {
		Common::U32String s;
		for (; *p; ++p)
			s += (Common::u32char_type_t)*p;
		return s;
	}

public:
	// L V T and L V composed: 한 (U+1112 U+1161 U+11AB), 가 (U+1100 U+1161), then a precomposed 가 + T = 각.
	void test_conjoiningJamoCompose() {
		const uint32 han[] = { 0x1112, 0x1161, 0x11AB, 0 };
		const uint32 ga[] = { 0x1100, 0x1161, 0 };
		const uint32 gak[] = { 0xAC00, 0x11A8, 0 };
		const uint32 word[] = { 0x1112, 0x1161, 0x11AB, 0x1100, 0x1173, 0x11AF, 0 }; // 한글
		TS_ASSERT_EQUALS(n(cps(han)), Common::U32String("\xED\x95\x9C"));
		TS_ASSERT_EQUALS(n(cps(ga)), Common::U32String("\xEA\xB0\x80"));
		TS_ASSERT_EQUALS(n(cps(gak)), Common::U32String("\xEA\xB0\x81"));
		TS_ASSERT_EQUALS(n(cps(word)), Common::U32String("\xED\x95\x9C\xEA\xB8\x80"));
	}

	// Last and first of each range, and what does not compose.
	void test_rangeEdges() {
		const uint32 last[] = { 0x1112, 0x1175, 0x11C2, 0 };   // 힣 U+D7A3
		const uint32 lone[] = { 0x1100, 0x11A8, 0 };           // L T: no syllable
		const uint32 lvt[] = { 0xAC01, 0x11A8, 0 };            // 각 + T: already has a final
		const uint32 notT[] = { 0xAC00, 0x11A7, 0 };           // U+11A7 is no final
		const uint32 compat[] = { 0x3131, 0x314F, 0 };         // compatibility jamo stay
		TS_ASSERT_EQUALS(n(cps(last)), Common::U32String("\xED\x9E\xA3"));
		TS_ASSERT_EQUALS(n(cps(lone)), cps(lone));
		TS_ASSERT_EQUALS(n(cps(lvt)), cps(lvt));
		TS_ASSERT_EQUALS(n(cps(notT)), cps(notT));
		TS_ASSERT_EQUALS(n(cps(compat)), cps(compat));
	}

	// Full-width ASCII and the ideographic space become ASCII; case is kept.
	void test_fullwidthFolds() {
		const uint32 in[] = { 0xFF2C, 0xFF4F, 0xFF4F, 0xFF4B, 0x3000, 0xFF01, 0xFF5E, 0xFF5F, 0 };
		const uint32 out[] = { 'L', 'o', 'o', 'k', ' ', '!', '~', 0xFF5F, 0 };
		TS_ASSERT_EQUALS(n(cps(in)), cps(out));
		TS_ASSERT_EQUALS(n(Common::U32String("open door")), Common::U32String("open door"));
		TS_ASSERT_EQUALS(n(Common::U32String()), Common::U32String());
	}
};
```

- [ ] **Step 2: run, expect a compile failure** (`normalizeKoreanInput` is not a member of `Common`).
- [ ] **Step 3: implement** — declaration and definition as in this diff against the Task 4 files:

```diff
--- a/common/hangul.h
+++ b/common/hangul.h
@@ -100,6 +98,18 @@
  */
 void resolveKoreanParticles(String &s);
 
+/**
+ * Bring typed Korean text to the form a word table holds (DESIGN.md section 6):
+ * conjoining jamo (U+1100..U+1112, U+1161..U+1175, U+11A8..U+11C2), as an input
+ * method that sends decomposed text gives them, are composed into precomposed
+ * syllables by the Unicode Hangul composition (a syllable without a final also
+ * takes a following final jamo); full-width ASCII (U+FF01..U+FF5E) becomes ASCII
+ * and the ideographic space (U+3000) a space. Everything else, compatibility
+ * jamo (U+3131..) included, is kept. Case is not folded (the engine's parser
+ * does that).
+ */
+void normalizeKoreanInput(U32String &s);
+
 } // End of namespace Common
 
 #endif
--- a/common/hangul.cpp
+++ b/common/hangul.cpp
@@ -359,4 +359,32 @@
 	s = out;
 }
 
+void normalizeKoreanInput(U32String &s) {
+	U32String out;
+	for (uint i = 0; i < s.size(); ++i) {
+		u32char_type_t c = s[i];
+		if (c >= 0xFF01 && c <= 0xFF5E) {
+			out += (u32char_type_t)(c - 0xFF01 + 0x21);
+			continue;
+		}
+		if (c == 0x3000) {
+			out += (u32char_type_t)' ';
+			continue;
+		}
+		// L V -> LV
+		if (c >= 0x1100 && c <= 0x1112 && i + 1 < s.size() && s[i + 1] >= 0x1161 && s[i + 1] <= 0x1175) {
+			c = 0xAC00 + ((c - 0x1100) * 21 + (s[i + 1] - 0x1161)) * 28;
+			++i;
+		}
+		// LV T -> LVT
+		if (c >= 0xAC00 && c <= 0xD7A3 && (c - 0xAC00) % 28 == 0 && i + 1 < s.size() && s[i + 1] >= 0x11A8 &&
+		    s[i + 1] <= 0x11C2) {
+			c += s[i + 1] - 0x11A7;
+			++i;
+		}
+		out += c;
+	}
+	s = out;
+}
+
 } // End of namespace Common
```

- [ ] **Step 4: run** — `heavy make -j16 test` → `558 tests ... OK!`.
- [ ] **Step 5: commit**

```
COMMON: Normalise typed Korean text

An input method may send a syllable as conjoining jamo, and a Korean
keyboard types full-width Latin letters. Compose the jamo by the
Unicode Hangul rule and fold full-width ASCII, so a parser word table
of precomposed syllables and ASCII finds the word.

Assisted-by: Claude Code
```

## Task 6: Port the stemmer and its corpus (10 min)

- [ ] **Step 1: copy the test and the corpus, point the corpus at the test's own folder**

```sh
cd $SRC
for f in korean_stem.h korean_stem_corpus.txt; do git -C $OLD show b4912e3c600:test/common/$f > test/common/$f; done
```

  `jamo` found the corpus through a `HIRES_TEST_SRCDIR` define of its `test/module.mk`; this tree has none (P1 finds
  source files through `__FILE__`). Edit `test/common/korean_stem.h` as in this diff:

```diff
+++ b/test/common/korean_stem.h
@@ -404,7 +404,7 @@
 	// candidate that is any corpus lemma must be the right one, because
 	// that is the candidate the engine will take.
 	void test_oracle_corpus_resolves_to_the_right_lemma() {
-		Common::String text = readSource("test/common/korean_stem_corpus.txt");
+		Common::String text = readSource("korean_stem_corpus.txt");
 		TS_ASSERT_LESS_THAN(0u, text.size());
 
 		Common::Array<Common::U32String> surfaces, lemmas;
@@ -453,8 +453,9 @@
 	}
 
 private:
-	Common::String readSource(const char *rel) {
-		Common::String path = Common::String(HIRES_TEST_SRCDIR) + "/" + rel;
+	// @p name beside this file (test/common/), found through __FILE__, which the runner includes by absolute path.
+	Common::String readSource(const char *name) {
+		const Common::String path = Common::Path(__FILE__, '/').getParent().join(name).toString('/');
 		Common::FSNode node(Common::Path(path, '/'));
 		Common::SeekableReadStream *in = node.createReadStream();
 		if (!in) {
```

- [ ] **Step 2: run, expect a compile failure** (`common/korean_stem.h: No such file or directory`).
- [ ] **Step 3: copy the code, build it**

```sh
for f in korean_stem.h korean_stem.cpp; do git -C $OLD show b4912e3c600:common/$f > common/$f; done
```

  In `common/module.mk`, insert `	korean_stem.o \` after `	hashmap.o \`.
- [ ] **Step 4: run** — `heavy make -j16 test` → `588 tests ... OK!` (the corpus test checks 1,197 surface forms:
  every one reaches its lemma and the first corpus lemma among its readings is the right one, no `TS_WARN` line).
  `git -C $OLD show b4912e3c600:common/korean_stem.cpp | diff - common/korean_stem.cpp` and the same for `.h` and the
  corpus are empty.
- [ ] **Step 5: commit**

```
COMMON: Add the Korean stemmer for parser input

A player types 연다, 열어봐 or 가방을 where a parser word table holds
열다 and 가방. koreanStemCandidates() lists the readings of a typed
word, best first, by stripping endings, auxiliaries and particles
(no heap, a fixed buffer), and reports the role of the case particle
it took off. Taken from the jamo branch with its tests and the corpus
the harness made with a morphological analyser.

Assisted-by: Claude Code
```

### Review checkpoint A (after Task 6)

superpowers:requesting-code-review on Tasks 1-6. The ports are checked by `diff`:

```sh
cd $SRC
for f in common/hangul.cpp common/korean_stem.h common/korean_stem.cpp test/common/hangul.h \
         test/common/korean_particles.h test/common/korean_stem_corpus.txt; do
	git -C $OLD show b4912e3c600:$f | cmp -s - $f || echo DIFFERS $f; done
git -C $OLD show b4912e3c600:common/hangul.h | diff - common/hangul.h | grep -c '^[<>]'   # 23 (Task 4 + Task 5 edits)
git -C $OLD show b4912e3c600:test/common/korean_stem.h | diff - test/common/korean_stem.h | grep -c '^[<>]'  # 7
```

The first loop prints nothing. Focus of the read: `normalizeKoreanInput` ranges (L U+1100-1112, V U+1161-1175,
T U+11A8-11C2; LV only when `(c - 0xAC00) % 28 == 0`), the keymapper lines (no duplicate `case` value in the SDL
switch), `KEYCODE_LAST` still last.

## Task 7: The fingerprint header (15 min)

- [ ] **Step 1: test** — `test/common/translation_file.h`:

```cpp
#include <cxxtest/TestSuite.h>

#include "common/translation_file.h"

#include "test/system/null_osystem.h"

// The fingerprint header line of the translation files (DESIGN.md section 4, decision 4).
class TranslationFileHeaderTestSuite : public CxxTest::TestSuite {
	static const char *kq1() {
		return "#! format=1 game=kq1 fingerprint=0123456789abcdef0123456789abcdef engine=sci";
	}

public:
	void setUp() override {
		Common::install_null_g_system();
	}

	void tearDown() override {
		Common::uninstall_null_g_system();
	}

	void test_parse() {
		Common::TranslationFileHeader h;
		Common::String error;
		TS_ASSERT(Common::TranslationFileHeader::parse(kq1(), h, error));
		TS_ASSERT(error.empty());
		TS_ASSERT(h.present());
		TS_ASSERT_EQUALS(h.format, 1);
		TS_ASSERT_EQUALS(h.game, "kq1");
		TS_ASSERT_EQUALS(h.fingerprint, "0123456789abcdef0123456789abcdef");
		TS_ASSERT_EQUALS(h.engine, "sci");
		TS_ASSERT(h.vocab.empty());
		TS_ASSERT_EQUALS(h.toString(), kq1());
	}

	// Any order, tabs, upper-case md5, a CR line end, and a key of a later format.
	void test_parseIsLenient() {
		Common::TranslationFileHeader h;
		Common::String error;
		TS_ASSERT(Common::TranslationFileHeader::parse(
			"#!\tengine=agi  vocab=FEDCBA9876543210FEDCBA9876543210 future=x game=lsl1 "
			"fingerprint=0123456789ABCDEF0123456789ABCDEF format=1\r", h, error));
		TS_ASSERT_EQUALS(h.toString(), "#! format=1 game=lsl1 fingerprint=0123456789abcdef0123456789abcdef engine=agi "
		                               "vocab=fedcba9876543210fedcba9876543210");
	}

	void test_notAHeader() {
		Common::TranslationFileHeader h;
		Common::String error = "x";
		TS_ASSERT(!Common::TranslationFileHeader::parse("# script strings: script<TAB>id<TAB>text", h, error));
		TS_ASSERT(error.empty());
		TS_ASSERT(!h.present());
		TS_ASSERT(!Common::TranslationFileHeader::parse("0\t5\tx", h, error));
		TS_ASSERT(error.empty());
	}

	void test_badHeaders() {
		const char *bad[] = {
			"#! game=kq1 fingerprint=0123456789abcdef0123456789abcdef engine=sci",          // no format
			"#! format=2 game=kq1 fingerprint=0123456789abcdef0123456789abcdef engine=sci", // newer format
			"#! format=x game=kq1 fingerprint=0123456789abcdef0123456789abcdef engine=sci",
			"#! format=1 fingerprint=0123456789abcdef0123456789abcdef engine=sci",           // no game
			"#! format=1 game=kq1 fingerprint=0123 engine=sci",                               // short md5
			"#! format=1 game=kq1 fingerprint=0123456789abcdef0123456789abcdeg engine=sci",  // not hex
			"#! format=1 game=kq1 fingerprint=0123456789abcdef0123456789abcdef",             // no engine
			"#! format=1 game=kq1 game=kq2 fingerprint=0123456789abcdef0123456789abcdef engine=sci",
			"#! format=1 game=kq1 fingerprint=0123456789abcdef0123456789abcdef engine=sci vocab=1",
			"#! format=1 game=kq1 fingerprint=0123456789abcdef0123456789abcdef engine=sci junk",
			"#! format=1 game=k/q fingerprint=0123456789abcdef0123456789abcdef engine=sci",
		};
		for (uint i = 0; i < ARRAYSIZE(bad); ++i) {
			Common::TranslationFileHeader h;
			Common::String error;
			TSM_ASSERT(bad[i], !Common::TranslationFileHeader::parse(bad[i], h, error));
			TSM_ASSERT(bad[i], !error.empty());
			TSM_ASSERT(bad[i], !h.present());
		}
	}

	void test_check() {
		Common::TranslationFileHeader found, expected, none;
		Common::String error;
		TS_ASSERT(Common::TranslationFileHeader::parse(kq1(), found, error));
		TS_ASSERT_EQUALS(Common::checkTranslationHeader(found, none, "a"), Common::kTranslationMatches);
		expected.game = "kq1";
		expected.engine = "sci";
		expected.fingerprint = "0123456789ABCDEF0123456789ABCDEF";
		TS_ASSERT_EQUALS(Common::checkTranslationHeader(found, expected, "a"), Common::kTranslationMatches);
		expected.fingerprint = "00000000000000000000000000000000";
		TS_ASSERT_EQUALS(Common::checkTranslationHeader(found, expected, "a"), Common::kTranslationMismatch);
		expected.fingerprint.clear();
		expected.game = "kq4";
		TS_ASSERT_EQUALS(Common::checkTranslationHeader(found, expected, "a"), Common::kTranslationMismatch);
		expected.game.clear();
		expected.vocab = "00000000000000000000000000000000"; // the file names no vocab
		TS_ASSERT_EQUALS(Common::checkTranslationHeader(found, expected, "a"), Common::kTranslationMismatch);
		TS_ASSERT_EQUALS(Common::checkTranslationHeader(none, expected, "a"), Common::kTranslationUnchecked);
	}

	void test_isMd5String() {
		TS_ASSERT(Common::isMd5String("0123456789abcdefABCDEF0123456789"));
		TS_ASSERT(!Common::isMd5String(""));
		TS_ASSERT(!Common::isMd5String("0123456789abcdef0123456789abcde"));
		TS_ASSERT(!Common::isMd5String("0123456789abcdef0123456789abcdefg"));
	}
};
```

- [ ] **Step 2: run, expect a compile failure** (`common/translation_file.h` missing).
- [ ] **Step 3: implement** — `common/translation_file.h`:

```cpp
#ifndef COMMON_TRANSLATION_FILE_H
#define COMMON_TRANSLATION_FILE_H

#include "common/str.h"

namespace Common {

/**
 * The fingerprint header of a game translation file: the first line of a script string table (sci-ko.str,
 * agi-ko.str) and the header text of a version 3 parser word table (sci-ko.dat, agi-ko.dat):
 *
 *     #! format=1 game=<gameid> fingerprint=<md5> engine=<sci|agi> [vocab=<md5>]
 *
 * Fields are key=value, separated by spaces or tabs, in any order; a key of a later format is skipped. An md5 is 32
 * hex digits (written lower case, compared ignoring case). The fingerprint is the md5 an engine computes from the
 * game files the translation's keys depend on (which files is the engine's definition); vocab is the md5 of the
 * vocabulary resource a word table's groups come from.
 */
struct TranslationFileHeader {
	int format;         ///< 1; 0 when the file has no header
	String game;        ///< the game id
	String fingerprint; ///< md5, lower case
	String engine;      ///< "sci", "agi"
	String vocab;       ///< md5, lower case, or empty

	TranslationFileHeader() : format(0) {}

	bool present() const { return format != 0; }

	/**
	 * Read a header line (without its line end). A line not starting with "#!" is no header: false with @p error
	 * empty. A "#!" line that is not a valid format 1 header: false with @p error saying why. @p out is reset first.
	 */
	static bool parse(const String &line, TranslationFileHeader &out, String &error);

	/** The header line, fields in the order above, vocab only when set. */
	String toString() const;
};

/** What checkTranslationHeader() found. */
enum TranslationCheck {
	kTranslationMatches,   ///< every field the game named is the file's
	kTranslationUnchecked, ///< the file has no header (a pack made before headers): use it, unchecked
	kTranslationMismatch   ///< a field differs: do not use the file
};

/**
 * Compare a file's header with what the game expects. Every non-empty field of @p expected is compared: game and
 * engine exactly, fingerprint and vocab ignoring case. Warns once for a file without a header ("loaded unchecked")
 * and once for a mismatch (each differing field with both values, then "not loaded").
 */
TranslationCheck checkTranslationHeader(const TranslationFileHeader &found, const TranslationFileHeader &expected,
                                        const String &fileName);

/** Whether @p s is 32 hex digits. */
bool isMd5String(const String &s);

} // End of namespace Common

#endif
```

  `common/translation_file.cpp`:

```cpp
#include "common/translation_file.h"
#include "common/textconsole.h"
#include "common/util.h"

namespace Common {

bool isMd5String(const String &s) {
	if (s.size() != 32)
		return false;
	for (uint i = 0; i < s.size(); ++i)
		if (!isXDigit(s[i]))
			return false;
	return true;
}

static bool isIdentifier(const String &s) {
	if (s.empty())
		return false;
	for (uint i = 0; i < s.size(); ++i)
		if (!isAlnum(s[i]) && s[i] != '-' && s[i] != '_')
			return false;
	return true;
}

bool TranslationFileHeader::parse(const String &line, TranslationFileHeader &out, String &error) {
	out = TranslationFileHeader();
	error.clear();
	String text = line;
	while (!text.empty() && (text.lastChar() == '\r' || text.lastChar() == '\n'))
		text.deleteLastChar();
	if (!text.hasPrefix("#!"))
		return false;

	TranslationFileHeader h;
	bool seen[5] = { false, false, false, false, false };
	static const char *const kKeys[5] = { "format", "game", "fingerprint", "engine", "vocab" };
	uint i = 2;
	while (i < text.size()) {
		while (i < text.size() && (text[i] == ' ' || text[i] == '\t'))
			++i;
		const uint start = i;
		while (i < text.size() && text[i] != ' ' && text[i] != '\t')
			++i;
		if (start == i)
			break;
		const String field(text.c_str() + start, i - start);
		const size_t eq = field.findFirstOf('=');
		if (eq == String::npos || eq == 0) {
			error = "'" + field + "' is not key=value";
			return false;
		}
		const String key(field.c_str(), eq);
		const String value(field.c_str() + eq + 1);
		int k = -1;
		for (int n = 0; n < 5; ++n)
			if (key == kKeys[n])
				k = n;
		if (k < 0)
			continue; // a key of a later format
		if (seen[k]) {
			error = key + " is given twice";
			return false;
		}
		seen[k] = true;
		switch (k) {
		case 0:
			if (value.empty() || value.size() > 4) {
				error = "format=" + value + " is not a number";
				return false;
			}
			for (uint d = 0; d < value.size(); ++d) {
				if (!isDigit(value[d])) {
					error = "format=" + value + " is not a number";
					return false;
				}
			}
			h.format = (int)atoi(value.c_str());
			break;
		case 1:
			h.game = value;
			break;
		case 2:
			h.fingerprint = value;
			break;
		case 3:
			h.engine = value;
			break;
		default:
			h.vocab = value;
			break;
		}
	}

	if (!seen[0]) {
		error = "no format=";
		return false;
	}
	if (h.format != 1) {
		error = String::format("format=%d, this build reads format=1", h.format);
		return false;
	}
	if (!isIdentifier(h.game)) {
		error = "game= is missing or not an id";
		return false;
	}
	if (!isMd5String(h.fingerprint)) {
		error = "fingerprint= is not 32 hex digits";
		return false;
	}
	if (!isIdentifier(h.engine)) {
		error = "engine= is missing or not an id";
		return false;
	}
	if (seen[4] && !isMd5String(h.vocab)) {
		error = "vocab= is not 32 hex digits";
		return false;
	}
	h.fingerprint.toLowercase();
	h.vocab.toLowercase();
	out = h;
	return true;
}

String TranslationFileHeader::toString() const {
	String s = String::format("#! format=%d game=%s fingerprint=%s engine=%s", format, game.c_str(), fingerprint.c_str(),
	                          engine.c_str());
	if (!vocab.empty())
		s += " vocab=" + vocab;
	return s;
}

TranslationCheck checkTranslationHeader(const TranslationFileHeader &found, const TranslationFileHeader &expected,
                                        const String &fileName) {
	if (!found.present()) {
		warning("%s has no fingerprint header (a pack made before headers); loaded unchecked", fileName.c_str());
		return kTranslationUnchecked;
	}
	String differs;
	const struct {
		const char *key;
		const String *want;
		const String *have;
		bool anyCase;
	} fields[] = {
		{ "game", &expected.game, &found.game, false },
		{ "engine", &expected.engine, &found.engine, false },
		{ "fingerprint", &expected.fingerprint, &found.fingerprint, true },
		{ "vocab", &expected.vocab, &found.vocab, true },
	};
	for (int i = 0; i < ARRAYSIZE(fields); ++i) {
		const String &want = *fields[i].want;
		const String &have = *fields[i].have;
		if (want.empty())
			continue;
		if (fields[i].anyCase ? want.equalsIgnoreCase(have) : want == have)
			continue;
		differs += String::format("%s%s=%s, the game's is %s", differs.empty() ? "" : "; ", fields[i].key,
		                          have.empty() ? "(none)" : have.c_str(), want.c_str());
	}
	if (differs.empty())
		return kTranslationMatches;
	warning("%s was made for another game: %s; not loaded", fileName.c_str(), differs.c_str());
	return kTranslationMismatch;
}

} // End of namespace Common
```

  In `common/module.mk`, insert `	translation_file.o \` after `	translation.o \`.
- [ ] **Step 4: run** — `heavy make -j16 test` → `594 tests ... OK!`.
- [ ] **Step 5: commit**

```
COMMON: Add the fingerprint header of translation files

A game translation is keyed by numbers that only mean something for
the game version it was made from. Its first line, "#! format=1
game=... fingerprint=<md5> engine=...", says which; the engines
compare it with the md5 of their own files. Parse and write that line
and compare it with what a game expects: a file without one loads with
a warning, one made for other files is refused.

Assisted-by: Claude Code
```

## Task 8: The parser word table, versions 2 and 3 (20 min)

- [ ] **Step 1: fixture and tests** — `test/korean_vocab_fixture.h` (outside the `test/common/*.h` glob; `<...>`
  includes so `"common/array.h"` cannot resolve to `test/common/array.h`, the array test suite):

```cpp
// Korean word tables built in memory for the tests of common/korean_vocab (and later the engines' parser tests):
// the layouts of common/korean_vocab.h, version 2 and 3, rows sorted by code units with the rows of one word kept in
// the order given (a sequence must stay ordered), as tools/korean/k6bake.py writes them.

#ifndef TEST_KOREAN_VOCAB_FIXTURE_H
#define TEST_KOREAN_VOCAB_FIXTURE_H

#include <common/array.h>
#include <common/endian.h>
#include <common/str.h>
#include <common/ustr.h>

namespace KoreanVocabFixture {

struct Row {
	const char *utf8;
	uint16 group;
	uint16 cls;
};

inline int compareUnits(const Common::U32String &a, const Common::U32String &b) {
	const uint n = MIN<uint>(a.size(), b.size());
	for (uint i = 0; i < n; ++i) {
		if (a[i] != b[i])
			return a[i] < b[i] ? -1 : 1;
	}
	return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
}

inline void put16(Common::Array<byte> &out, uint16 v) {
	out.push_back((byte)(v & 0xFF));
	out.push_back((byte)(v >> 8));
}

inline void put32(Common::Array<byte> &out, uint32 v) {
	put16(out, (uint16)(v & 0xFFFF));
	put16(out, (uint16)(v >> 16));
}

/**
 * A table of @p rows. @p version 2 or 3; for 3, @p header is the header line (TranslationFileHeader::toString()),
 * written NUL-terminated and padded to a multiple of 4.
 */
inline Common::Array<byte> build(const char *magic6, const Row *rows, uint count, uint32 version = 2,
                                 const char *header = "") {
	Common::Array<Common::U32String> words;
	Common::Array<uint> order;
	for (uint i = 0; i < count; ++i) {
		words.push_back(Common::String(rows[i].utf8).decode(Common::kUtf8));
		order.push_back(i);
	}
	// Insertion sort: stable, so the rows of one word keep their order.
	for (uint i = 1; i < order.size(); ++i) {
		const uint v = order[i];
		uint j = i;
		while (j > 0 && compareUnits(words[order[j - 1]], words[v]) > 0) {
			order[j] = order[j - 1];
			--j;
		}
		order[j] = v;
	}
	Common::Array<byte> out;
	char magic[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	memcpy(magic, magic6, MIN<size_t>(strlen(magic6), 6));
	for (uint i = 0; i < 8; ++i)
		out.push_back((byte)magic[i]);
	put32(out, version);
	put32(out, count);
	if (version == 3) {
		const uint32 len = (uint32)strlen(header) + 1;
		const uint32 padded = (len + 3) & ~3u;
		put32(out, padded);
		for (uint32 i = 0; i < padded; ++i)
			out.push_back(i < len - 1 ? (byte)header[i] : 0);
	}
	Common::Array<byte> blob;
	uint32 offset = 0;
	for (uint k = 0; k < order.size(); ++k) {
		const Common::U32String &w = words[order[k]];
		if (version == 3)
			put32(out, offset);
		else
			put16(out, (uint16)offset);
		put16(out, (uint16)w.size());
		put16(out, rows[order[k]].group);
		put16(out, rows[order[k]].cls);
		if (version == 3)
			put16(out, 0);
		for (uint i = 0; i < w.size(); ++i)
			put16(blob, (uint16)w[i]);
		offset += w.size();
	}
	for (uint i = 0; i < blob.size(); ++i)
		out.push_back(blob[i]);
	return out;
}

} // End of namespace KoreanVocabFixture

#endif
```

  `test/common/korean_vocab.h` (the `jamo` cases, run on both versions where it matters, plus the v3 ones; Tasks 10
  and 11 add two more tests):

```cpp
#include <cxxtest/TestSuite.h>

#include "common/korean_stem.h"
#include "common/korean_vocab.h"
#include "common/memstream.h"
#include "common/str-enc.h"

#include "test/korean_vocab_fixture.h"
#include "test/system/null_osystem.h"

// The word table common/ owns for SCI (SCIKOR.DAT, sci-ko.dat) and AGI (AGIKOR.DAT, agi-ko.dat): one layout, the magic
// says whose it is; version 2 (no header) and version 3 (u32 offsets, fingerprint header).
class KoreanVocabTestSuite : public CxxTest::TestSuite {
	static Common::U32String u(const char *utf8) {
		return Common::String(utf8).decode(Common::kUtf8);
	}

	static const char *header() {
		return "#! format=1 game=kq1 fingerprint=0123456789abcdef0123456789abcdef engine=sci "
		       "vocab=fedcba9876543210fedcba9876543210";
	}

	static bool loadImage(Common::KoreanVocabulary &v, const Common::Array<byte> &img, const char *magicWanted,
	                      const Common::TranslationFileHeader &expected = Common::TranslationFileHeader()) {
		Common::MemoryReadStream s(img.data(), img.size());
		return v.loadFromStream(s, magicWanted, "fixture", expected);
	}

	static Common::Array<byte> rowsImage(const char *magicInFile, uint32 version, const char *head = "") {
		static const KoreanVocabFixture::Row rows[] = {
			{ "\xEC\x97\xB4\xEC\x87\xA0", 12, 0 },                               // 열쇠 key
			{ "\xEC\xA7\x91\xEB\x8B\xA4", 20, 1 },                               // 집다 get (verb)
			{ "\xEB\x93\xA4\xEC\x97\xAC\xEB\x8B\xA4\xEB\xB4\x90", 30, 0x8001 }, // 들여다봐 look+
			{ "\xEB\x93\xA4\xEC\x97\xAC\xEB\x8B\xA4\xEB\xB4\x90", 169, 0 },     //          in
		};
		return KoreanVocabFixture::build(magicInFile, rows, ARRAYSIZE(rows), version, head);
	}

	static bool loadJoinRows(Common::KoreanVocabulary &v, uint32 version) {
		static const KoreanVocabFixture::Row rows[] = {
			{ "나무", 10, 0 },       // tree
			{ "보다", 11, 1 },       // look (verb)
			{ "물을내리다", 12, 1 }, // flush (verb, stored joined)
			{ "고기", 13, 0 },       // meat
			{ "고다", 14, 1 },       // a verb that 고기 must not become
			{ "사다", 15, 1 },       // buy
			{ "웃다", 16, 1 },       // laugh
		};
		const char *head = "#! format=1 game=kq1 fingerprint=0123456789abcdef0123456789abcdef engine=sci";
		return loadImage(v, KoreanVocabFixture::build("SCIKOR", rows, ARRAYSIZE(rows), version, head), "SCIKOR");
	}

public:
	void setUp() override {
		Common::install_null_g_system();
	}

	void tearDown() override {
		Common::uninstall_null_g_system();
	}

	void test_magic_is_checked() {
		Common::KoreanVocabulary v;
		TS_ASSERT(!loadImage(v, rowsImage("SCIKOR", 2), "AGIKOR"));
		TS_ASSERT(!v.isLoaded());
		TS_ASSERT(loadImage(v, rowsImage("AGIKOR", 2), "AGIKOR"));
		TS_ASSERT_EQUALS(v.size(), 4u);
		TS_ASSERT_EQUALS(v.getVersion(), 2u);
		TS_ASSERT(!v.getHeader().present());
	}

	void test_exact_and_stemmed_lookup() {
		for (uint32 version = 2; version <= 3; ++version) {
			Common::KoreanVocabulary v;
			const char *head = "#! format=1 game=lsl1 fingerprint=0123456789abcdef0123456789abcdef engine=agi";
			TS_ASSERT(loadImage(v, rowsImage("AGIKOR", version, head), "AGIKOR"));
			uint16 g[4], c[4];
			uint8 role = 0xFF;
			TS_ASSERT_EQUALS(v.lookupAll(u("\xEC\x97\xB4\xEC\x87\xA0"), g, c, 4, role), 1u);
			TS_ASSERT_EQUALS(g[0], 12);
			TS_ASSERT_EQUALS(role, (uint8)Common::kKoreanRoleNone);
			// 열쇠를: the object particle is stripped and named
			TS_ASSERT_EQUALS(v.lookupAll(u("\xEC\x97\xB4\xEC\x87\xA0\xEB\xA5\xBC"), g, c, 4, role), 1u);
			TS_ASSERT_EQUALS(g[0], 12);
			TS_ASSERT_EQUALS(role, (uint8)Common::kKoreanRoleObject);
			// 열쇠로: with
			TS_ASSERT_EQUALS(v.lookupAll(u("\xEC\x97\xB4\xEC\x87\xA0\xEB\xA1\x9C"), g, c, 4, role), 1u);
			TS_ASSERT_EQUALS(role, (uint8)Common::kKoreanRoleWith);
			// exact only: 열쇠를 is a reading, not a stored form
			TS_ASSERT_EQUALS(v.lookupExact(u("\xEC\x97\xB4\xEC\x87\xA0\xEB\xA5\xBC"), g, c, 4), 0u);
			TS_ASSERT_EQUALS(v.lookupExact(u("\xEC\x97\xB4\xEC\x87\xA0"), g, c, 4), 1u);
			uint16 group = 0, wclass = 0;
			TS_ASSERT(v.lookup(u("\xEC\xA7\x91\xEB\x8B\xA4"), group, wclass));
			TS_ASSERT_EQUALS(group, 20);
			TS_ASSERT_EQUALS(wclass, 1);
			TS_ASSERT(!v.lookup(u("abc"), group, wclass));
		}
	}

	void test_sequence_rows_come_back_in_order() {
		Common::KoreanVocabulary v;
		TS_ASSERT(loadImage(v, rowsImage("AGIKOR", 2), "AGIKOR"));
		uint16 g[4], c[4];
		uint8 role;
		TS_ASSERT_EQUALS(v.lookupAll(u("\xEB\x93\xA4\xEC\x97\xAC\xEB\x8B\xA4\xEB\xB4\x90"), g, c, 4, role), 2u);
		TS_ASSERT_EQUALS(g[0], 30);
		TS_ASSERT(c[0] & Common::KoreanVocabulary::kSequenceFlag);
		TS_ASSERT_EQUALS(g[1], 169);
		// max is respected
		TS_ASSERT_EQUALS(v.lookupAll(u("\xEB\x93\xA4\xEC\x97\xAC\xEB\x8B\xA4\xEB\xB4\x90"), g, c, 1, role), 1u);
	}

	// Two typed words are one word only when the table stores them joined: the joined form as typed, or the first
	// word as typed with a reading of the second (물을 내린다 -> 물을내리다). A reading of the joined string must never
	// swallow the second word: 나무 보다 is tree + look, not 나무보다 read as 나무 + the comparative particle.
	void test_a_pair_joins_only_on_a_stored_joined_form() {
		for (uint32 version = 2; version <= 3; ++version) {
			Common::KoreanVocabulary v;
			TS_ASSERT(loadJoinRows(v, version));
			TS_ASSERT(v.lookupJoined(u("물을"), u("내린다")));
			TS_ASSERT(v.lookupJoined(u("물을"), u("내리다")));
			TS_ASSERT(!v.lookupJoined(u("나무"), u("보다")));
			TS_ASSERT(!v.lookupJoined(u("나무"), u("처럼")));
			TS_ASSERT(!v.lookupJoined(u("나무"), u("이다")));
			TS_ASSERT(!v.lookupJoined(u("나무를"), u("보라")));
			// An auxiliary verb typed apart is still the verb's own ending: 사 줘 is 사줘 (buy), 웃어 봐 is 웃어봐.
			TS_ASSERT(v.lookupJoined(u("사"), u("줘")));
			TS_ASSERT(v.lookupJoined(u("웃어"), u("봐")));
			uint16 g[4], c[4];
			uint8 role;
			TS_ASSERT_EQUALS(v.lookupAll(u("나무보다"), g, c, 4, role), 1u);
			TS_ASSERT_EQUALS(g[0], 10);
		}
	}

	// A noun the table holds wins over a verb reading of it: 고기 is meat, never 고다 + the -기 nominaliser.
	void test_a_stored_noun_wins_over_a_gi_reading() {
		Common::KoreanVocabulary v;
		TS_ASSERT(loadJoinRows(v, 3));
		uint16 g[4], c[4];
		uint8 role;
		TS_ASSERT_EQUALS(v.lookupAll(u("고기"), g, c, 4, role), 1u);
		TS_ASSERT_EQUALS(g[0], 13);
		TS_ASSERT_EQUALS(v.lookupAll(u("고기를"), g, c, 4, role), 1u);
		TS_ASSERT_EQUALS(g[0], 13);
		TS_ASSERT_EQUALS(v.lookupAll(u("보기"), g, c, 4, role), 1u);
		TS_ASSERT_EQUALS(g[0], 11);
		TS_ASSERT_EQUALS(v.lookupAll(u("보라"), g, c, 4, role), 1u);
		TS_ASSERT_EQUALS(g[0], 11);
	}

	void test_truncated_table_is_refused() {
		static const byte bad[] = { 'A', 'G', 'I', 'K', 'O', 'R', 0, 0, 2, 0, 0, 0, 9, 0, 0, 0 };
		Common::MemoryReadStream s(bad, sizeof(bad));
		Common::KoreanVocabulary v;
		TS_ASSERT(!v.loadFromStream(s, "AGIKOR", "bad"));
		TS_ASSERT(!v.isLoaded());
	}

	// u32 offsets: a blob past 65535 code units, which version 2 cannot address.
	void test_version3_offsets_past_64k_units() {
		Common::Array<Common::String> words;
		Common::Array<KoreanVocabFixture::Row> rows;
		// 4,500 words of 16 syllables: 72,000 code units.
		for (uint i = 0; i < 4500; ++i) {
			Common::U32String w;
			for (uint k = 0; k < 16; ++k)
				w += (Common::u32char_type_t)(0xAC00 + (k == 15 ? i % 11172 : (i / 11172 + k) % 11172));
			words.push_back(Common::convertFromU32String(w, Common::kUtf8));
		}
		for (uint i = 0; i < words.size(); ++i) {
			KoreanVocabFixture::Row r = { words[i].c_str(), (uint16)(i + 1), 0 };
			rows.push_back(r);
		}
		const char *head = "#! format=1 game=big fingerprint=0123456789abcdef0123456789abcdef engine=sci";
		Common::KoreanVocabulary v;
		TS_ASSERT(loadImage(v, KoreanVocabFixture::build("SCIKOR", rows.data(), rows.size(), 3, head), "SCIKOR"));
		TS_ASSERT_EQUALS(v.size(), 4500u);
		uint16 g[2], c[2];
		TS_ASSERT_EQUALS(v.lookupExact(words[4499].decode(Common::kUtf8), g, c, 2), 1u);
		TS_ASSERT_EQUALS(g[0], 4500);
		TS_ASSERT_EQUALS(v.lookupExact(words[0].decode(Common::kUtf8), g, c, 2), 1u);
		TS_ASSERT_EQUALS(g[0], 1);
	}

	// The fingerprint header decides: a table made for another game is refused; a version 2 table (no header) loads.
	void test_fingerprint_is_checked() {
		Common::TranslationFileHeader same, other, none;
		same.game = "kq1";
		same.fingerprint = "0123456789ABCDEF0123456789ABCDEF";
		same.vocab = "fedcba9876543210fedcba9876543210";
		other = same;
		other.fingerprint = "ffffffffffffffffffffffffffffffff";
		Common::KoreanVocabulary v;
		TS_ASSERT(loadImage(v, rowsImage("SCIKOR", 3, header()), "SCIKOR", same));
		TS_ASSERT(loadImage(v, rowsImage("SCIKOR", 3, header()), "SCIKOR", none));
		TS_ASSERT(!loadImage(v, rowsImage("SCIKOR", 3, header()), "SCIKOR", other));
		TS_ASSERT(!v.isLoaded());
		other = same;
		other.vocab = "00000000000000000000000000000000";
		TS_ASSERT(!loadImage(v, rowsImage("SCIKOR", 3, header()), "SCIKOR", other));
		TS_ASSERT(loadImage(v, rowsImage("SCIKOR", 2), "SCIKOR", same));
		TS_ASSERT_EQUALS(v.getVersion(), 2u);
	}

	void test_bad_version3_files_are_refused() {
		Common::KoreanVocabulary v;
		// engine= must match the magic
		TS_ASSERT(!loadImage(v, rowsImage("AGIKOR", 3, header()), "AGIKOR"));
		// no header line
		TS_ASSERT(!loadImage(v, rowsImage("SCIKOR", 3, "no header"), "SCIKOR"));
		// header size not a multiple of 4
		Common::Array<byte> img = rowsImage("SCIKOR", 3, header());
		img[16] = 3;
		TS_ASSERT(!loadImage(v, img, "SCIKOR"));
		// an unknown version
		img = rowsImage("SCIKOR", 2);
		img[8] = 4;
		TS_ASSERT(!loadImage(v, img, "SCIKOR"));
		// entries out of order: the last two (열쇠, 집다) swapped
		img = rowsImage("SCIKOR", 2);
		for (uint i = 0; i < 8; ++i) {
			const byte t = img[16 + 8 * 2 + i];
			img[16 + 8 * 2 + i] = img[16 + 8 * 3 + i];
			img[16 + 8 * 3 + i] = t;
		}
		TS_ASSERT(!loadImage(v, img, "SCIKOR"));
		// an entry past the blob
		img = rowsImage("SCIKOR", 2);
		img[16 + 2] = 200;
		TS_ASSERT(!loadImage(v, img, "SCIKOR"));
	}
};
```

- [ ] **Step 2: run, expect a compile failure** (`common/korean_vocab.h` missing).
- [ ] **Step 3: implement** — `common/korean_vocab.h`:

```cpp
#ifndef COMMON_KOREAN_VOCAB_H
#define COMMON_KOREAN_VOCAB_H

#include "common/path.h"
#include "common/scummsys.h"
#include "common/str.h"
#include "common/stream.h"
#include "common/translation_file.h"
#include "common/ustr.h"

namespace Common {

/**
 * Korean words for a parser game (SCI0, AGI), held outside the game's own vocabulary.
 *
 * A Korean word cannot be put into the game's vocabulary: SCI's vocab.000 loader masks every byte with 0x7F and AGI's
 * WORDS.TOK is 7-bit, so the mapping lives beside it. It maps a word to a vocabulary GROUP and the class to insert it
 * with: said() matches by group, and parseGNF() also tests the class.
 *
 * The file (sci-ko.dat / SCIKOR.DAT, agi-ko.dat / AGIKOR.DAT; tools/korean/k6bake.py writes it) holds code points,
 * sorted by code unit, so a lookup of the decoded typed word, or of a stemmer reading of it, is a binary search.
 *
 * Layout, little-endian. Version 2 (the packs made before fingerprint headers):
 *
 *     0   8  magic "SCIKOR\0\0" or "AGIKOR\0\0"
 *     8   4  version 2
 *     12  4  count
 *     16     count * { u16 offset, u16 length, u16 group, u16 class }
 *            then the UTF-16LE code units the offsets index
 *
 * Version 3 (DESIGN.md section 4, decision 4):
 *
 *     0   8  magic
 *     8   4  version 3
 *     12  4  count
 *     16  4  header size H, a multiple of 4
 *     20  H  the fingerprint header line (TranslationFileHeader, with vocab=), NUL-terminated, NUL-padded
 *     20+H   count * { u32 offset, u16 length, u16 group, u16 class, u16 reserved (0) }
 *            then the UTF-16LE code units
 *
 * Offsets and lengths count code units. Entries are sorted by their code units (unsigned, shorter first on a common
 * prefix); the entries of one word are adjacent, in the order written. A class with kSequenceFlag continues into the
 * next entry of the same word.
 */
class KoreanVocabulary {
public:
	explicit KoreanVocabulary(uint32 debugChannel = 0);
	~KoreanVocabulary() { unload(); }

	/** The engine's debug channel for the [korvocab] lines; 0 logs nothing. */
	void setDebugChannel(uint32 channel) { _debugChannel = channel; }

	/**
	 * Load a word table through SearchMan. @p magic is the file's 6-letter tag ("SCIKOR", "AGIKOR"). @p expected
	 * holds what the game knows about itself (any field empty: not compared); a version 3 file whose header differs
	 * is refused, a version 2 file loads with a warning (checkTranslationHeader()).
	 *
	 * @return false when the file is absent (no warning: no Korean words installed), malformed or refused.
	 */
	bool load(const Path &filename, const char *magic, const TranslationFileHeader &expected = TranslationFileHeader());

	/** load() once the file is open; @p name is for the warnings. */
	bool loadFromStream(SeekableReadStream &in, const char *magic, const String &name,
	                    const TranslationFileHeader &expected = TranslationFileHeader());

	void unload();

	bool isLoaded() const { return _data != nullptr; }

	/** Number of entries. */
	uint32 size() const { return _count; }

	/** 2 or 3; 0 when nothing is loaded. */
	uint32 getVersion() const { return _version; }

	/** The header of a version 3 table; not present() for version 2. */
	const TranslationFileHeader &getHeader() const { return _header; }

	/**
	 * Look up one typed word, decoded to code points: the exact word first, then the readings
	 * koreanStemCandidates() gives, best first; the first the table holds wins.
	 */
	bool lookup(const U32String &word, uint16 &group, uint16 &wclass) const;

	/**
	 * Every (group, class) pair the table holds for the word, or else for its best reading (several when the word
	 * stands for an English word in several groups, or is a sequence). @p role receives the KoreanRole of the reading
	 * that answered. Returns how many pairs were written, at most @p max.
	 */
	uint lookupAll(const U32String &word, uint16 *groups, uint16 *classes, uint max, uint8 &role) const;

	/** The pairs stored for exactly @p word, no stemmer reading. */
	uint lookupExact(const U32String &word, uint16 *groups, uint16 *classes, uint max) const;

	/**
	 * Whether two adjacent typed words are one stored word: the table holds @p first + @p second as typed, or
	 * @p first joined to a reading of @p second (물을 내린다 -> 물을내리다), or the reading of the joined string that
	 * lookupAll() takes reaches past the first word (사 줘 -> 사다). A reading that ends inside the first word dropped
	 * the second as a particle and does not count (나무 보다 is tree, look).
	 */
	bool lookupJoined(const U32String &first, const U32String &second) const;

	/** Class flag of an entry that continues into the next entry of the same word (들여다봐 is "look in"). */
	enum { kSequenceFlag = 0x8000 };

private:
	struct Entry {
		uint32 offset;
		uint16 length;
		uint16 group;
		uint16 wclass;
	};

	Entry entry(uint32 index) const;
	/** -1, 0, 1: entry @p index against @p word. */
	int compare(uint32 index, const uint16 *word, uint len) const;
	/** -1, 0, 1: entry @p a against entry @p b. */
	int compareEntries(uint32 a, uint32 b) const;
	int findIndex(const uint16 *word, uint len) const;
	uint collect(int index, uint16 *groups, uint16 *classes, uint max) const;
	bool sameWord(uint32 a, uint32 b) const;
	bool refuse(const String &name, const String &why);

	byte *_data;
	uint32 _size;
	uint32 _count;
	uint32 _version;
	uint32 _entrySize;
	const byte *_entries;
	const byte *_blob;
	uint32 _blobUnits;
	TranslationFileHeader _header;
	uint32 _debugChannel;
};

} // End of namespace Common

#endif // COMMON_KOREAN_VOCAB_H
```

  `common/korean_vocab.cpp`:

```cpp
#include "common/debug.h"
#include "common/endian.h"
#include "common/file.h"
#include "common/textconsole.h"

#include "common/korean_stem.h"
#include "common/korean_vocab.h"

namespace Common {

static const uint32 kHeadV2 = 16;
static const uint32 kHeadV3 = 20;
static const uint32 kEntryV2 = 8;
static const uint32 kEntryV3 = 12;
static const uint32 kMaxHeaderText = 1024;

#define KV_DEBUG(level, ...) \
	do { if (_debugChannel) debugC(level, _debugChannel, __VA_ARGS__); } while (0)

KoreanVocabulary::KoreanVocabulary(uint32 debugChannel) : _data(nullptr), _size(0), _count(0), _version(0),
	_entrySize(0), _entries(nullptr), _blob(nullptr), _blobUnits(0), _debugChannel(debugChannel) {
}

bool KoreanVocabulary::load(const Path &filename, const char *magic, const TranslationFileHeader &expected) {
	unload();
	File f;
	if (!f.open(filename)) {
		KV_DEBUG(1, "[korvocab] %s not found; Korean words resolve nothing", filename.toString().c_str());
		return false;
	}
	return loadFromStream(f, magic, filename.toString(), expected);
}

bool KoreanVocabulary::refuse(const String &name, const String &why) {
	warning("%s: %s", name.c_str(), why.c_str());
	unload();
	return false;
}

bool KoreanVocabulary::loadFromStream(SeekableReadStream &in, const char *magic, const String &name,
                                      const TranslationFileHeader &expected) {
	unload();
	const int64 fileSize = in.size();
	if (fileSize < (int64)kHeadV2)
		return refuse(name, "too small to be a Korean word table");
	if (fileSize > 16 * 1024 * 1024)
		return refuse(name, "larger than 16 MB, not a Korean word table");

	_data = (byte *)malloc((size_t)fileSize);
	if (!_data)
		return refuse(name, "out of memory");
	_size = (uint32)fileSize;
	if (in.read(_data, _size) != _size)
		return refuse(name, "could not be read in full");

	byte want[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	memcpy(want, magic, MIN<size_t>(strlen(magic), 6));
	if (memcmp(_data, want, 8) != 0)
		return refuse(name, String::format("not a %s word table (bad magic)", magic));

	_version = READ_LE_UINT32(_data + 8);
	_count = READ_LE_UINT32(_data + 12);
	uint32 entriesAt;
	if (_version == 2) {
		_entrySize = kEntryV2;
		entriesAt = kHeadV2;
	} else if (_version == 3) {
		if (_size < kHeadV3)
			return refuse(name, "too small for a version 3 header");
		_entrySize = kEntryV3;
		const uint32 headerSize = READ_LE_UINT32(_data + 16);
		if (headerSize == 0 || headerSize % 4 || headerSize > kMaxHeaderText || kHeadV3 + headerSize > _size)
			return refuse(name, String::format("header size %u is not valid", headerSize));
		const char *text = (const char *)_data + kHeadV3;
		const char *end = (const char *)memchr(text, 0, headerSize);
		if (!end)
			return refuse(name, "the header text has no NUL");
		const uint32 textLen = (uint32)(end - text);
		String error;
		if (!TranslationFileHeader::parse(String(text, textLen), _header, error))
			return refuse(name, "bad header: " + (error.empty() ? String("no #! line") : error));
		String engine(magic, MIN<size_t>(strlen(magic), 3));
		engine.toLowercase();
		if (_header.engine != engine)
			return refuse(name, "header engine=" + _header.engine + " in a " + magic + " table");
		entriesAt = kHeadV3 + headerSize;
	} else {
		return refuse(name, String::format("version %u; this build reads versions 2 and 3", _version));
	}

	if ((uint64)entriesAt + (uint64)_count * _entrySize > _size)
		return refuse(name, String::format("claims %u entries but is only %u bytes", _count, _size));
	_entries = _data + entriesAt;
	_blob = _entries + _count * _entrySize;
	_blobUnits = (_size - (uint32)(_blob - _data)) / 2;

	// Every entry addresses code units inside the blob, and the entries are in the order findIndex() searches:
	// checked once here, so a bad file is refused instead of giving wrong answers later.
	for (uint32 i = 0; i < _count; ++i) {
		const Entry e = entry(i);
		if (e.length == 0 || (uint64)e.offset + e.length > _blobUnits)
			return refuse(name, String::format("entry %u runs past the end of its string table", i));
		if (i > 0 && compareEntries(i - 1, i) > 0)
			return refuse(name, String::format("entry %u is out of order", i));
	}

	if (_version == 2) {
		_header = TranslationFileHeader();
		checkTranslationHeader(_header, expected, name);
	} else if (checkTranslationHeader(_header, expected, name) == kTranslationMismatch) {
		unload();
		return false;
	}
	KV_DEBUG(1, "[korvocab] loaded %u Korean words (version %u) from %s", _count, _version, name.c_str());
	return true;
}

void KoreanVocabulary::unload() {
	free(_data);
	_data = nullptr;
	_size = _count = _version = _entrySize = _blobUnits = 0;
	_entries = _blob = nullptr;
	_header = TranslationFileHeader();
}

KoreanVocabulary::Entry KoreanVocabulary::entry(uint32 index) const {
	const byte *e = _entries + index * _entrySize;
	Entry out;
	if (_version == 2) {
		out.offset = READ_LE_UINT16(e);
		e += 2;
	} else {
		out.offset = READ_LE_UINT32(e);
		e += 4;
	}
	out.length = READ_LE_UINT16(e);
	out.group = READ_LE_UINT16(e + 2);
	out.wclass = READ_LE_UINT16(e + 4);
	return out;
}

int KoreanVocabulary::compare(uint32 index, const uint16 *word, uint len) const {
	const Entry e = entry(index);
	const byte *w = _blob + e.offset * 2;
	const uint n = MIN<uint>(len, e.length);
	for (uint i = 0; i < n; ++i) {
		const uint16 a = READ_LE_UINT16(w + i * 2);
		if (a != word[i])
			return a < word[i] ? -1 : 1;
	}
	if (e.length == len)
		return 0;
	return e.length < len ? -1 : 1;
}

int KoreanVocabulary::compareEntries(uint32 a, uint32 b) const {
	const Entry ea = entry(a);
	const Entry eb = entry(b);
	const byte *wa = _blob + ea.offset * 2;
	const byte *wb = _blob + eb.offset * 2;
	const uint n = MIN<uint>(ea.length, eb.length);
	for (uint i = 0; i < n; ++i) {
		const uint16 ua = READ_LE_UINT16(wa + i * 2);
		const uint16 ub = READ_LE_UINT16(wb + i * 2);
		if (ua != ub)
			return ua < ub ? -1 : 1;
	}
	if (ea.length == eb.length)
		return 0;
	return ea.length < eb.length ? -1 : 1;
}

int KoreanVocabulary::findIndex(const uint16 *word, uint len) const {
	if (!_data || !len)
		return -1;
	int lo = 0;
	int hi = (int)_count - 1;
	while (lo <= hi) {
		const int mid = lo + (hi - lo) / 2;
		const int c = compare((uint32)mid, word, len);
		if (c == 0)
			return mid;
		if (c < 0)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return -1;
}

bool KoreanVocabulary::sameWord(uint32 a, uint32 b) const {
	const Entry ea = entry(a);
	const Entry eb = entry(b);
	return ea.length == eb.length && memcmp(_blob + ea.offset * 2, _blob + eb.offset * 2, ea.length * 2) == 0;
}

uint KoreanVocabulary::collect(int index, uint16 *groups, uint16 *classes, uint max) const {
	if (index < 0)
		return 0;
	uint32 first = (uint32)index;
	while (first > 0 && sameWord(first - 1, first))
		--first;
	uint n = 0;
	for (uint32 i = first; i < _count && n < max && (i == first || sameWord(first, i)); ++i) {
		const Entry e = entry(i);
		groups[n] = e.group;
		classes[n] = e.wclass;
		++n;
	}
	return n;
}

bool KoreanVocabulary::lookup(const U32String &word, uint16 &group, uint16 &wclass) const {
	uint8 role;
	return lookupAll(word, &group, &wclass, 1, role) == 1;
}

uint KoreanVocabulary::lookupExact(const U32String &word, uint16 *groups, uint16 *classes, uint max) const {
	if (!_data || word.empty() || word.size() > KoreanStems::kMaxLen || !max)
		return 0;
	uint16 typed[KoreanStems::kMaxLen];
	for (uint i = 0; i < word.size(); ++i) {
		if (word[i] > 0xFFFF)
			return 0;
		typed[i] = (uint16)word[i];
	}
	return collect(findIndex(typed, word.size()), groups, classes, max);
}

uint KoreanVocabulary::lookupAll(const U32String &word, uint16 *groups, uint16 *classes, uint max,
                                 uint8 &role) const {
	role = kKoreanRoleNone;
	uint n = lookupExact(word, groups, classes, max);
	if (n) {
		KV_DEBUG(2, "[korvocab] exact -> %u pair(s), group %u class %u", n, groups[0], classes[0]);
		return n;
	}
	if (!_data || word.empty() || word.size() > KoreanStems::kMaxLen || !max)
		return 0;
	// The readings of the word, best first: 연다 is stored as 열다, 가방을 as 가방.
	KoreanStems stems;
	const uint count = koreanStemCandidates(word, stems);
	for (uint i = 0; i < count; ++i) {
		n = collect(findIndex(stems.cp[i], stems.len[i]), groups, classes, max);
		if (n) {
			role = stems.role[i];
			KV_DEBUG(2, "[korvocab] stem -> %u pair(s), group %u class %u, role %u", n, groups[0], classes[0], role);
			return n;
		}
	}
	return 0;
}

bool KoreanVocabulary::lookupJoined(const U32String &first, const U32String &second) const {
	uint16 g, c;
	if (lookupExact(first + second, &g, &c, 1))
		return true;
	KoreanStems stems;
	const uint n = koreanStemCandidates(second, stems);
	for (uint i = 0; i < n; ++i) {
		U32String joined = first;
		for (uint k = 0; k < stems.len[i]; ++k)
			joined += (u32char_type_t)stems.cp[i][k];
		if (lookupExact(joined, &g, &c, 1))
			return true;
	}
	// A reading of the joined string, the one lookupAll() would take, counts only when it reaches past the first
	// word: 사 줘 -> 사다 reads the second word as the verb's auxiliary, 나무 보다 -> 나무 would drop it as a particle.
	const U32String joined = first + second;
	const uint m = koreanStemCandidates(joined, stems);
	for (uint i = 0; i < m; ++i) {
		if (!lookupExact(stems.get(i), &g, &c, 1))
			continue;
		bool insideFirst = stems.len[i] <= first.size();
		for (uint k = 0; insideFirst && k < stems.len[i]; ++k)
			insideFirst = stems.cp[i][k] == first[k];
		return !insideFirst;
	}
	return false;
}

} // End of namespace Common
```

  In `common/module.mk`, insert `	korean_vocab.o \` after `	korean_stem.o \`.
- [ ] **Step 4: run** — `heavy make -j16 test` → `603 tests ... OK!`.
- [ ] **Step 5: commit**

```
COMMON: Add the Korean parser word table, version 3

KoreanVocabulary maps Korean words to the groups and classes of a
parser game's vocabulary (SCI SCIKOR, AGI AGIKOR), trying the
stemmer's readings of a typed word. Version 3 of the table has u32
offsets and the fingerprint header, so a table made for another game
version is refused; version 2 tables of the packs out there still load,
with a warning. The reader also refuses a table whose entries are not
in order, which a binary search would silently get wrong.

Assisted-by: Claude Code
```

## Task 9: `k6bake.py` and the KQ1 byte-identity check (20 min)

- [ ] **Step 1: mini master and tests** — `tools/korean/testdata/mini.vocab.tsv` (tabs between the columns of the
  word lines; sha256 `d4c58e2df35c8ad2dc16501942a83bbaf0f3400707c2773a09bec10953f53fbc`):

```
#KOREAN-VOCAB 2 (UTF-8 master of a Korean parser word table, sci-ko.dat / agi-ko.dat; trs_master.py)
@magic SCIKOR
# A tiny table for test/korean_vocab_golden.h (tools/korean/k6bake.py golden).
@class 12 0x0800 key
@class 20 0x0080 get
@class 30 0x0080 look
@class 83 0x0800 door
@class 166 0x0800 doorway
@class 169 0x0010 in
열쇠	12	key
집다	20	get
들여다봐	30+169	look + in
문	83,166	door; doorway
가방	12	key
```

  Write it with tabs exactly:

```sh
cd $SRC && mkdir -p tools/korean/testdata && printf '%s\n' \
 '#KOREAN-VOCAB 2 (UTF-8 master of a Korean parser word table, sci-ko.dat / agi-ko.dat; trs_master.py)' \
 '@magic SCIKOR' '# A tiny table for test/korean_vocab_golden.h (tools/korean/k6bake.py golden).' \
 '@class 12 0x0800 key' '@class 20 0x0080 get' '@class 30 0x0080 look' '@class 83 0x0800 door' \
 '@class 166 0x0800 doorway' '@class 169 0x0010 in' $'열쇠\t12\tkey' $'집다\t20\tget' \
 $'들여다봐\t30+169\tlook + in' $'문\t83,166\tdoor; doorway' $'가방\t12\tkey' > tools/korean/testdata/mini.vocab.tsv
sha256sum tools/korean/testdata/mini.vocab.tsv
```

  `tools/korean/test_k6bake.py` (Task 10 adds one test):

```python
#!/usr/bin/env python3
# ScummVM - Graphic Adventure Engine
#
# ScummVM is the legal property of its developers, whose names
# are too numerous to list here. Please refer to the COPYRIGHT
# file distributed with this source distribution.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
"""Tests of k6bake.py: python3 -I tools/korean/test_k6bake.py (from anywhere; standard library only)."""

import hashlib
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import k6bake  # noqa: E402

MINI = os.path.join(HERE, "testdata", "mini.vocab.tsv")
GOLDEN = os.path.join(ROOT, "test", "korean_vocab_golden.h")
GOLDEN_ARGS = ["--game", "kq1", "--fingerprint", "0123456789abcdef0123456789abcdef",
               "--vocab-md5", "fedcba9876543210fedcba9876543210"]
FP = "0123456789abcdef0123456789abcdef"


def master(*lines):
    return "\n".join([k6bake.MASTER_TAG, "@magic SCIKOR", "@class 1 0x0800 a", "@class 2 0x0010 b"] + list(lines)) + "\n"


class K6BakeTest(unittest.TestCase):
    def test_master_rows_sorted_with_sequences_and_alternatives(self):
        magic, rows = k6bake.master_rows(open(MINI, encoding="utf-8").read())
        self.assertEqual(magic, "SCIKOR")
        self.assertEqual([w for w, _g, _c in rows], ["가방", "들여다봐", "들여다봐", "문", "문", "열쇠", "집다"])
        self.assertEqual(rows[1], ("들여다봐", 30, 0x8080))
        self.assertEqual(rows[2], ("들여다봐", 169, 0x0010))
        self.assertEqual(rows[3][1:], (83, 0x0800))
        self.assertEqual(rows[4][1:], (166, 0x0800))

    def test_version2_layout(self):
        data = k6bake.table_bytes("SCIKOR", [("가", 1, 0x0800), ("나다", 2, 0x8010)], 2)
        self.assertEqual(data.hex(), "5343494b4f520000" "02000000" "02000000"
                         "0000" "0100" "0100" "0008" "0100" "0200" "0200" "1080" "00ac" "98b0" "e4b2")

    def test_version3_layout(self):
        head = k6bake.header_line("kq1", FP.upper(), "sci")
        self.assertEqual(head, "#! format=1 game=kq1 fingerprint=%s engine=sci" % FP)
        data = k6bake.table_bytes("SCIKOR", [("가", 1, 0x0800)], 3, head)
        self.assertEqual(data[8:12], b"\x03\0\0\0")
        size = int.from_bytes(data[16:20], "little")
        self.assertEqual(size % 4, 0)
        self.assertEqual(data[20:20 + len(head)].decode("ascii"), head)
        self.assertEqual(data[20 + len(head)], 0)
        self.assertEqual(data[20 + size:], bytes.fromhex("00000000" "0100" "0100" "0008" "0000" "00ac"))

    def test_round_trip_between_versions(self):
        magic, rows = k6bake.master_rows(open(MINI, encoding="utf-8").read())
        v2 = k6bake.table_bytes(magic, rows, 2)
        v3 = k6bake.table_bytes(magic, rows, 3, k6bake.header_line("kq1", FP, "sci"))
        m2, ver2, h2, r2 = k6bake.read_table(v2)
        m3, ver3, h3, r3 = k6bake.read_table(v3)
        self.assertEqual((m2, ver2, h2, r2), ("SCIKOR", 2, None, rows))
        self.assertEqual((m3, ver3, r3), ("SCIKOR", 3, rows))
        self.assertTrue(h3.startswith("#! format=1 game=kq1 "))

    def test_cli_build_convert_dump(self):
        with tempfile.TemporaryDirectory() as tmp:
            v3 = os.path.join(tmp, "t3.dat")
            v2 = os.path.join(tmp, "t2.dat")
            self.assertEqual(k6bake.main(["k6bake", "build", MINI, v3, "--game", "kq1", "--fingerprint", FP]), 0)
            self.assertEqual(k6bake.main(["k6bake", "convert", v3, v2, "--format", "2"]), 0)
            magic, rows = k6bake.master_rows(open(MINI, encoding="utf-8").read())
            with open(v2, "rb") as f:
                self.assertEqual(f.read(), k6bake.table_bytes(magic, rows, 2))
            self.assertEqual(k6bake.main(["k6bake", "build", MINI, v3]), 2)            # format 3 needs --game
            self.assertEqual(k6bake.main(["k6bake", "build", MINI, v2, "--format", "2", "--game", "kq1"]), 2)

    def test_k3map_source(self):
        import json
        with tempfile.TemporaryDirectory() as tmp:
            k3 = os.path.join(tmp, "k3map.json")
            k2 = os.path.join(tmp, "k2targets.json")
            json.dump({"map": {"문": [83, 166], "들여다봐": "30+169", "\U00020000": 1, "\ua960": 5}}, open(k3, "w"))
            json.dump({"groups": [{"id": 83, "classes": [0x800]}, {"id": 30, "classes": [0x80, 0x10]}]}, open(k2, "w"))
            magic, rows = k6bake.source_rows(k3, ["--targets", k2])
        # U+A960 (a Hangul Jamo Extended-A letter) is not CP949: skipped, as the harness's k6bake.py did; the plane-2
        # word too.
        self.assertEqual(magic, "SCIKOR")
        self.assertEqual(rows, [("들여다봐", 30, 0x8090), ("들여다봐", 169, 0), ("문", 83, 0x800), ("문", 166, 0)])

    def test_refusals(self):
        bad = [
            "not a master\n",
            master("가\t1", "가\t2"),        # a word on two lines
            master("가\t3"),                 # a group without @class
            master("가\tx"),
            master("\U00020000\t1"),         # outside the BMP
        ]
        for text in bad:
            with self.assertRaises(k6bake.BakeError):
                k6bake.master_rows(text)
        with self.assertRaises(k6bake.BakeError):
            k6bake.header_line("kq1", "0123", "sci")
        with self.assertRaises(k6bake.BakeError):
            k6bake.header_line("k q", FP, "sci")
        rows = [("가" * 300 + chr(0xAC00 + i), 1, 0) for i in range(220)]      # 66,220 code units
        with self.assertRaises(k6bake.BakeError):
            k6bake.table_bytes("SCIKOR", rows, 2)
        self.assertEqual(k6bake.read_table(k6bake.table_bytes("SCIKOR", rows, 3, k6bake.header_line("x", FP, "sci")))[3], rows)
        unsorted = k6bake.table_bytes("SCIKOR", [("나", 1, 0), ("가", 1, 0)], 2)
        with self.assertRaises(k6bake.BakeError):
            k6bake.read_table(unsorted)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: run, expect a failure** — `python3 -I tools/korean/test_k6bake.py` → `ModuleNotFoundError: No module named 'k6bake'`.
- [ ] **Step 3: implement** — `tools/korean/k6bake.py` (`chmod +x`); the master rules are the harness's
  `trs_master.py text_to_vocab` (lines 935-976 there), the k3map rules its `k6bake.py` `main()` (lines 83-150):

```python
#!/usr/bin/env python3
# ScummVM - Graphic Adventure Engine
#
# ScummVM is the legal property of its developers, whose names
# are too numerous to list here. Please refer to the COPYRIGHT
# file distributed with this source distribution.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
"""Bake a Korean parser word table (sci-ko.dat / SCIKOR.DAT, agi-ko.dat / AGIKOR.DAT) for common/korean_vocab.

The layouts are in common/korean_vocab.h: version 2 (u16 offsets, no header; the packs made before fingerprint
headers) and version 3 (u32 offsets and the fingerprint header line of common/translation_file.h). Python 3 standard
library only.

usage:
  k6bake.py build SOURCE OUT [--format 2|3] [--game ID --fingerprint MD5 [--vocab-md5 MD5]]
                             [--targets K2TARGETS] [--magic SCIKOR|AGIKOR] [--verbs K2TARGETS]
      SOURCE is a UTF-8 master (.vocab.tsv, first line "#KOREAN-VOCAB 2 ...", the harness's trs_master.py format) or
      a k3map.json (Korean word -> groups; the classes from --targets, or for AGI --magic AGIKOR --verbs). Format 3
      (the default) needs --game and --fingerprint; the engine comes from the magic.
  k6bake.py convert IN OUT [--format 2|3] [--game ID --fingerprint MD5 [--vocab-md5 MD5]]
      rewrite a version 2 or 3 table in the other version (the rows stay byte for byte).
  k6bake.py dump DAT
      the header line (version 3) and one "word<TAB>group<TAB>0xclass" line per entry, in file order.
  k6bake.py golden SOURCE --game ID --fingerprint MD5 [--vocab-md5 MD5]
      a C++ header with the version 2 and 3 tables of SOURCE (test/korean_vocab_golden.h).
  k6bake.py harness-check HARNESS
      build every committed master of the harness (HARNESS/dos/release/<game>/translation/*-ko.dat.vocab.tsv) as
      version 2 and compare with the sha256 in HARNESS/dos/release/translations.tsv; exit 1 on a difference.
"""

import hashlib
import json
import os
import re
import struct
import sys

MASTER_TAG = "#KOREAN-VOCAB 2 (UTF-8 master of a Korean parser word table, sci-ko.dat / agi-ko.dat; trs_master.py)"
SEQ = 0x8000       # class flag: this row continues into the next row of the same word
MAGICS = ("SCIKOR", "AGIKOR")
MD5 = re.compile(r"^[0-9a-fA-F]{32}$")
ID = re.compile(r"^[A-Za-z0-9_-]+$")


class BakeError(Exception):
    pass


def units(word):
    return [ord(c) for c in word]


def sort_rows(rows):
    """Sorted by code units, unsigned, shorter first on a common prefix (Python's list order); stable."""
    return sorted(rows, key=lambda r: units(r[0]))


def header_line(game, fingerprint, engine, vocab=None):
    if not game or not ID.match(game):
        raise BakeError("--game must be a game id")
    if not fingerprint or not MD5.match(fingerprint):
        raise BakeError("--fingerprint must be 32 hex digits")
    if vocab is not None and not MD5.match(vocab):
        raise BakeError("--vocab-md5 must be 32 hex digits")
    line = "#! format=1 game=%s fingerprint=%s engine=%s" % (game, fingerprint.lower(), engine)
    if vocab:
        line += " vocab=" + vocab.lower()
    return line


def table_bytes(magic, rows, version=3, header=None):
    """The table of [(word, group, class)] (already in table order)."""
    if magic not in MAGICS:
        raise BakeError("magic must be SCIKOR or AGIKOR")
    out = bytearray(magic.encode("ascii").ljust(8, b"\0"))
    out += struct.pack("<II", version, len(rows))
    if version == 3:
        text = header.encode("ascii") + b"\0"
        text += b"\0" * (-len(text) % 4)
        out += struct.pack("<I", len(text)) + text
    elif version != 2:
        raise BakeError("format must be 2 or 3")
    entries, blob = bytearray(), bytearray()
    for word, group, cls in rows:
        u = units(word)
        if not u or any(c > 0xFFFF for c in u) or len(u) > 0xFFFF:
            raise BakeError("%r is empty, too long or outside the BMP" % word)
        if not (0 <= group <= 0xFFFF and 0 <= cls <= 0xFFFF):
            raise BakeError("%s: group or class out of range" % word)
        offset = len(blob) // 2
        if version == 2:
            if offset + len(u) > 0xFFFF:
                raise BakeError("the strings need more than 65535 code units: use format 3")
            entries += struct.pack("<HHHH", offset, len(u), group, cls)
        else:
            entries += struct.pack("<IHHHH", offset, len(u), group, cls, 0)
        blob += word.encode("utf-16-le")
    return bytes(out + entries + blob)


def read_table(data, what="table"):
    """(magic, version, header line or None, [(word, group, class)]) of a version 2 or 3 table."""
    if len(data) < 16 or data[6:8] != b"\0\0" or data[:6].decode("ascii", "replace") not in MAGICS:
        raise BakeError("%s: not a Korean parser word table" % what)
    magic = data[:6].decode("ascii")
    version, count = struct.unpack_from("<II", data, 8)
    header = None
    if version == 2:
        at, size, fmt = 16, 8, "<HHHH"
    elif version == 3:
        (hsize,) = struct.unpack_from("<I", data, 16)
        raw = data[20:20 + hsize]
        if hsize == 0 or hsize % 4 or len(raw) != hsize or b"\0" not in raw:
            raise BakeError("%s: bad header size" % what)
        header = raw[:raw.index(b"\0")].decode("ascii")
        at, size, fmt = 20 + hsize, 12, "<IHHHH"
    else:
        raise BakeError("%s: version %d, only 2 and 3 exist" % (what, version))
    blob = at + count * size
    if blob > len(data):
        raise BakeError("%s: %d entries do not fit" % (what, count))
    rows = []
    for i in range(count):
        f = struct.unpack_from(fmt, data, at + i * size)
        off, ln, group, cls = f[0], f[1], f[2], f[3]
        if blob + (off + ln) * 2 > len(data):
            raise BakeError("%s: entry %d runs past the end" % (what, i))
        rows.append((data[blob + off * 2:blob + (off + ln) * 2].decode("utf-16-le"), group, cls))
    if [units(w) for w, _g, _c in rows] != sorted(units(w) for w, _g, _c in rows):
        raise BakeError("%s: the entries are not sorted by code units" % what)
    return magic, version, header, rows


def master_rows(text):
    """(magic, rows) of a .vocab.tsv master (the harness's trs_master.py text_to_vocab rules); rows in table order."""
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    if not lines or lines[0] != MASTER_TAG:
        raise BakeError("not a Korean parser word master (first line must be: %s)" % MASTER_TAG)
    magic, classes, rows, seen = None, {}, [], set()
    for no, line in enumerate(lines[1:], 2):
        where = "line %d: " % no
        if line.startswith("@magic "):
            magic = line[7:]
            if magic not in MAGICS:
                raise BakeError(where + "@magic SCIKOR or AGIKOR")
        elif line.startswith("@class "):
            f = line.split(" ", 3)
            try:
                classes[int(f[1])] = int(f[2], 16)
            except (IndexError, ValueError):
                raise BakeError(where + "expected @class <group> <class hex> [English words]")
        elif line.startswith("#") or not line:
            continue
        else:
            f = line.split("\t")
            if len(f) not in (2, 3) or not f[0] or f[0] in seen:
                raise BakeError(where + ("%s is on two lines" % f[0] if f[0] in seen else
                                         "expected word TAB groups [TAB English]"))
            seen.add(f[0])
            if any(ord(c) > 0xFFFF for c in f[0]):
                raise BakeError(where + "a character outside the BMP")
            seq = "+" in f[1]
            try:
                groups = [int(g) for g in f[1].split("+" if seq else ",")]
            except ValueError:
                raise BakeError(where + "groups are numbers joined by , (alternatives) or + (a sequence)")
            for k, g in enumerate(groups):
                if g not in classes:
                    raise BakeError(where + "group %d has no @class line" % g)
                rows.append((f[0], g, classes[g] | (SEQ if seq and k + 1 < len(groups) else 0)))
    if magic is None:
        raise BakeError("the master has no @magic line")
    return magic, sort_rows(rows)


def k3map_rows(kmap, targets=None, verbs=None):
    """Rows of a k3map.json map (the harness's k6bake.py rules: classes ORed from targets, AGI verb flag, words not
    typeable as CP949 skipped)."""
    classof = {}
    if targets:
        for g in targets["groups"]:
            acc = 0
            for c in g.get("classes") or []:
                acc |= c
            if g.get("classes"):
                classof[g["id"]] = acc
    if verbs:
        for g in verbs["groups"]:
            classof[g["id"]] = 0x0001 if g.get("verb") else 0
    rows = []
    for word, groups in kmap.items():
        try:
            word.encode("cp949")
        except UnicodeEncodeError:
            continue
        seq = isinstance(groups, str)
        if seq:
            groups = [int(x) for x in groups.split("+")]
        groups = groups if isinstance(groups, list) else [groups]
        for n, group in enumerate(groups):
            cls = classof.get(group, 0)
            if len(word) > 0xFFFF or group > 0xFFFF or cls > 0x7FFF:
                continue
            if seq and n + 1 < len(groups):
                cls |= SEQ
            rows.append((word, group, cls))
    return sort_rows(rows)


def take(args, name, default=None):
    if name in args:
        i = args.index(name)
        if i + 1 >= len(args):
            raise BakeError(name + " needs a value")
        value = args[i + 1]
        del args[i:i + 2]
        return value
    return default


def source_rows(path, args):
    magic = take(args, "--magic")
    targets = take(args, "--targets")
    verbs = take(args, "--verbs")
    with open(path, encoding="utf-8") as f:
        text = f.read()
    if path.endswith(".json"):
        kmap = json.loads(text)["map"]
        t = json.load(open(targets, encoding="utf-8")) if targets else None
        v = json.load(open(verbs, encoding="utf-8")) if verbs else None
        return magic or "SCIKOR", k3map_rows(kmap, t, v)
    if magic or targets or verbs:
        raise BakeError("--magic, --targets and --verbs are for a k3map.json")
    return master_rows(text)


def versioned(magic, rows, args):
    version = int(take(args, "--format", "3"))
    game = take(args, "--game")
    fingerprint = take(args, "--fingerprint")
    vocab = take(args, "--vocab-md5")
    header = None
    if version == 3:
        header = header_line(game, fingerprint, magic[:3].lower(), vocab)
    elif game or fingerprint or vocab:
        raise BakeError("--game, --fingerprint and --vocab-md5 are for format 3")
    return table_bytes(magic, rows, version, header)


GPL_C = """/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
"""


def c_array(name, data):
    lines = ["static const byte %s[%d] = {" % (name, len(data))]
    for i in range(0, len(data), 16):
        lines.append("\t" + ", ".join("0x%02x" % b for b in data[i:i + 16]) + ",")
    lines.append("};")
    return "\n".join(lines)


def golden(path, args):
    magic, rows = source_rows(path, args)
    game = take(args, "--game")
    fingerprint = take(args, "--fingerprint")
    vocab = take(args, "--vocab-md5")
    v2 = table_bytes(magic, rows, 2)
    v3 = table_bytes(magic, rows, 3, header_line(game, fingerprint, magic[:3].lower(), vocab))
    return GPL_C + "\n".join([
        "",
        "// Generated by tools/korean/k6bake.py golden %s; do not edit." % os.path.basename(path),
        "// The tables of that master as version 2 and as version 3 (common/korean_vocab.h).",
        "",
        "#ifndef TEST_KOREAN_VOCAB_GOLDEN_H",
        "#define TEST_KOREAN_VOCAB_GOLDEN_H",
        "",
        "#include <common/scummsys.h>",
        "",
        c_array("kKoreanVocabGoldenV2", v2),
        "",
        c_array("kKoreanVocabGoldenV3", v3),
        "",
        "#endif",
        "",
    ])


def harness_check(root):
    release = os.path.join(root, "dos", "release")
    want = {}
    with open(os.path.join(release, "translations.tsv"), encoding="utf-8") as f:
        for line in f:
            f5 = line.rstrip("\n").split("\t")
            if len(f5) >= 5 and f5[3].endswith(".vocab.tsv"):
                want[(f5[1], f5[3])] = f5[4]
    bad = 0
    for (game, master), sha in sorted(want.items()):
        _magic, rows = master_rows(open(os.path.join(release, game, "translation", master), encoding="utf-8").read())
        data = table_bytes(_magic, rows, 2)
        got = hashlib.sha256(data).hexdigest()
        ok = got == sha
        bad += not ok
        print("%-8s %-22s %6d bytes %s %s" % (game, master, len(data), got[:16], "ok" if ok else "DIFFERS, want " + sha))
    return 1 if bad or not want else 0


def main(argv):
    args = list(argv[1:])
    if not args:
        sys.exit(__doc__)
    cmd = args.pop(0)
    try:
        if cmd == "build" and len(args) >= 2:
            src, out = args.pop(0), args.pop(0)
            magic, rows = source_rows(src, args)
            data = versioned(magic, rows, args)
        elif cmd == "convert" and len(args) >= 2:
            src, out = args.pop(0), args.pop(0)
            magic, _version, _header, rows = read_table(open(src, "rb").read(), src)
            data = versioned(magic, rows, args)
        elif cmd == "dump" and len(args) == 1:
            magic, version, header, rows = read_table(open(args[0], "rb").read(), args[0])
            print("@magic %s version %d" % (magic, version))
            if header:
                print(header)
            for w, g, c in rows:
                print("%s\t%d\t0x%04x" % (w, g, c))
            return 0
        elif cmd == "golden" and len(args) >= 1:
            src = args.pop(0)
            sys.stdout.write(golden(src, args))
            return 0
        elif cmd == "harness-check" and len(args) == 1:
            return harness_check(args[0])
        else:
            sys.exit(__doc__)
        if args:
            raise BakeError("unknown arguments: " + " ".join(args))
        # Read it back: a format mistake fails here, not as "Korean does nothing" in the engine.
        back = read_table(data, out)
        if back[3] != rows:
            raise BakeError("read-back differs from the rows written")
        with open(out, "wb") as f:
            f.write(data)
        print("%s: %s version %d, %d entries, %d bytes, sha256 %s" % (out, magic, back[1], len(rows), len(data),
                                                                    hashlib.sha256(data).hexdigest()))
        return 0
    except BakeError as e:
        print("k6bake: %s" % e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
```

- [ ] **Step 4: run** — `python3 -I tools/korean/test_k6bake.py` → `Ran 7 tests ... OK`. Then the done-criterion:

```sh
cd $HAR/dos/release
python3 -I $SRC/tools/korean/k6bake.py build kq1/translation/sci-ko.dat.vocab.tsv $BLD/kq1-sci-ko.dat --format 2
#  ...kq1-sci-ko.dat: SCIKOR version 2, 1265 entries, 16558 bytes, sha256 48cc43e4720ef98b4790366c6cecb3c2cf4e4f091a9ac3021520e0882645ed75
grep 'kq1	sci-ko.dat' translations.tsv | cut -f5          # the same sha256
python3 -I $SRC/tools/korean/k6bake.py harness-check $HAR; echo rc=$?
#  nine lines (camelot kq1 kq4 lb1 lsl1a lsl2 pq2 qfg1e qfg2) ending in "ok", rc=0
for g in kq4 pq2 qfg1e qfg2; do python3 -I $SRC/tools/korean/k6bake.py build $g/k3map.json $BLD/$g-k3.dat --format 2 --targets $g/k2targets.json; done
python3 -I $SRC/tools/korean/k6bake.py build lsl1a/k3map.json $BLD/lsl1a-k3.dat --format 2 --magic AGIKOR --verbs lsl1a/k2targets.json
#  sha256 3e03b854..., 9bcab2d8..., e80717c8..., 977975ef..., 05c11821... (translations.tsv)
python3 -I $SRC/tools/korean/k6bake.py build kq1/translation/sci-ko.dat.vocab.tsv $BLD/kq1-v3.dat --game kq1 \
	--fingerprint 0123456789abcdef0123456789abcdef --vocab-md5 fedcba9876543210fedcba9876543210
#  SCIKOR version 3, 1265 entries, 21738 bytes
python3 -I $SRC/tools/korean/k6bake.py convert $BLD/kq1-v3.dat $BLD/kq1-back.dat --format 2 && cmp $BLD/kq1-back.dat $BLD/kq1-sci-ko.dat && echo same
```

  (No file of the harness is written: every output goes to `$BLD`.)
- [ ] **Step 5: commit** (`tools/korean/k6bake.py`, `test_k6bake.py`, `testdata/mini.vocab.tsv`)

```
TOOLS: Korean: Add k6bake.py, the parser word table baker

Builds the Korean parser word tables (sci-ko.dat, agi-ko.dat) from
the UTF-8 masters the harness keeps, or from a k3map.json, as version
3 with the fingerprint header or as version 2. Version 2 output is
byte-identical to the tables the packs ship (harness-check compares
all nine); convert moves a table between the versions.

Assisted-by: Claude Code
```

## Task 10: Golden tables shared by the tool and the reader (10 min)

- [ ] **Step 1: test** — in `test/common/korean_vocab.h` add `#include "test/korean_vocab_golden.h"` after
  `#include "test/korean_vocab_fixture.h"`, and before `// u32 offsets: a blob past 65535 code units ...`:

```cpp
	// Bytes k6bake.py wrote (test/korean_vocab_golden.h) read back as the rows of its master, in both versions.
	void test_golden_tables_from_k6bake() {
		Common::KoreanVocabulary v2, v3;
		Common::MemoryReadStream s2(kKoreanVocabGoldenV2, sizeof(kKoreanVocabGoldenV2));
		Common::MemoryReadStream s3(kKoreanVocabGoldenV3, sizeof(kKoreanVocabGoldenV3));
		TS_ASSERT(v2.loadFromStream(s2, "SCIKOR", "golden v2"));
		TS_ASSERT(v3.loadFromStream(s3, "SCIKOR", "golden v3"));
		TS_ASSERT_EQUALS(v2.size(), 7u);
		TS_ASSERT_EQUALS(v3.size(), 7u);
		TS_ASSERT_EQUALS(v3.getVersion(), 3u);
		TS_ASSERT_EQUALS(v3.getHeader().toString(), Common::String(header()));
		TS_ASSERT_EQUALS(v3.getHeader().game, "kq1");
		TS_ASSERT_EQUALS(v3.getHeader().engine, "sci");
		const struct {
			const char *word;
			uint n;
			uint16 g0, c0, g1, c1;
		} want[] = {
			{ "열쇠", 1, 12, 0x0800, 0, 0 },
			{ "가방", 1, 12, 0x0800, 0, 0 },
			{ "집다", 1, 20, 0x0080, 0, 0 },
			{ "들여다봐", 2, 30, 0x8080, 169, 0x0010 },
			{ "문", 2, 83, 0x0800, 166, 0x0800 },
			{ "문들", 0, 0, 0, 0, 0 },
		};
		for (uint i = 0; i < ARRAYSIZE(want); ++i) {
			const Common::KoreanVocabulary *tables[2] = { &v2, &v3 };
			for (int t = 0; t < 2; ++t) {
				uint16 g[4] = { 0, 0, 0, 0 }, c[4] = { 0, 0, 0, 0 };
				TSM_ASSERT_EQUALS(want[i].word, tables[t]->lookupExact(u(want[i].word), g, c, 4), want[i].n);
				TSM_ASSERT_EQUALS(want[i].word, g[0], want[i].g0);
				TSM_ASSERT_EQUALS(want[i].word, c[0], want[i].c0);
				TSM_ASSERT_EQUALS(want[i].word, g[1], want[i].g1);
				TSM_ASSERT_EQUALS(want[i].word, c[1], want[i].c1);
			}
		}
	}
```

  In `tools/korean/test_k6bake.py`, before `def test_cli_build_convert_dump`:

```python
    def test_committed_golden_is_current(self):
        with open(GOLDEN, encoding="utf-8") as f:
            self.assertEqual(f.read(), k6bake.golden(MINI, list(GOLDEN_ARGS)))

```

- [ ] **Step 2: run, expect failures** — `heavy make -j16 test`: `test/korean_vocab_golden.h: No such file`;
  `python3 -I tools/korean/test_k6bake.py`: `FileNotFoundError` in `test_committed_golden_is_current`.
- [ ] **Step 3: generate**

```sh
cd $SRC && python3 -I tools/korean/k6bake.py golden tools/korean/testdata/mini.vocab.tsv --game kq1 \
	--fingerprint 0123456789abcdef0123456789abcdef --vocab-md5 fedcba9876543210fedcba9876543210 > test/korean_vocab_golden.h
sha256sum test/korean_vocab_golden.h   # 29b7aa43dd19cdc3053482de84fe93687107062ffa9849dea26d4763cd9d6825
```

  (a 104-byte v2 and a 252-byte v3 array, the GPL header first; `k6bake.py` writes it, do not edit.)
- [ ] **Step 4: run** — `heavy make -j16 test` → `604 tests ... OK!`; `python3 -I tools/korean/test_k6bake.py` →
  `Ran 8 tests ... OK`.
- [ ] **Step 5: commit**

```
TEST: Check the Korean word table reader against k6bake.py output

A small master baked by tools/korean/k6bake.py as version 2 and 3 is
kept as test data; the C++ reader must find its rows in both, and the
tool's test checks that the committed bytes are what it writes today.

Assisted-by: Claude Code
```

## Task 11: The harness tables as developer data (10 min)

- [ ] **Step 1: test** — in `test/common/korean_vocab.h` add `#include "common/fs.h"` and `#include "common/ptr.h"`
  (sorted into the `common/` includes), and before the class's closing `};`:

```cpp

	// Developer data: when the working directory has korean-vocab/ with <game>.v2.dat, <game>.v3.dat and <game>.tsv
	// (tools/korean/k6bake.py build --format 2|3 and dump of the harness masters), every word of the dump resolves
	// exactly to its rows, in both versions. Passes without the directory (as hires-maps/ in P1).
	void test_harness_tables_when_given() {
		Common::FSNode dir(Common::Path("korean-vocab"));
		Common::FSList files;
		if (!dir.isDirectory() || !dir.getChildren(files, Common::FSNode::kListFilesOnly))
			return;
		uint games = 0;
		for (uint i = 0; i < files.size(); ++i) {
			const Common::String name = files[i].getName();
			if (!name.hasSuffix(".tsv"))
				continue;
			const Common::String game(name.c_str(), name.size() - 4);
			Common::ScopedPtr<Common::SeekableReadStream> dump(files[i].createReadStream());
			TS_ASSERT(dump);
			if (!dump)
				continue;
			Common::String magic = "SCIKOR";
			Common::Array<Common::String> lines;
			while (!dump->eos()) {
				const Common::String line = dump->readLine();
				if (line.hasPrefix("@magic "))
					magic = Common::String(line.c_str() + 7, 6);
				else if (!line.empty() && line[0] != '#' && line[0] != '@')
					lines.push_back(line);
			}
			for (int version = 2; version <= 3; ++version) {
				Common::KoreanVocabulary v;
				Common::ScopedPtr<Common::SeekableReadStream> in(
					dir.getChild(game + Common::String::format(".v%d.dat", version)).createReadStream());
				TSM_ASSERT(game.c_str(), in && v.loadFromStream(*in, magic.c_str(), game));
				if (!v.isLoaded())
					continue;
				TS_ASSERT_EQUALS(v.size(), lines.size());
				uint k = 0;
				while (k < lines.size()) {
					const Common::String word(lines[k].c_str(), lines[k].findFirstOf('\t'));
					uint16 g[16], c[16];
					const uint n = v.lookupExact(word.decode(Common::kUtf8), g, c, 16);
					const bool lookedUp = word.decode(Common::kUtf8).size() <= Common::KoreanStems::kMaxLen;
					uint rows = 0;
					while (k < lines.size() && lines[k].hasPrefix(word + "\t")) {
						if (lookedUp && rows < n) {
							const Common::String want = Common::String::format("%s\t%u\t0x%04x", word.c_str(), g[rows], c[rows]);
							TSM_ASSERT_EQUALS(game.c_str(), lines[k], want);
						}
						++rows;
						++k;
					}
					if (lookedUp)
						TSM_ASSERT_EQUALS((game + " " + word).c_str(), n, rows);
				}
			}
			++games;
		}
		TS_ASSERT_LESS_THAN(0u, games);
	}
```

- [ ] **Step 2: run without data** — `heavy make -j16 test` → `605 tests ... OK!` (no `korean-vocab/` in `$BLD`: the
  test passes without looking, as P1's `hires-maps/`).
- [ ] **Step 3: run with data, first wrong, then right**

```sh
mkdir -p $BLD/korean-vocab && cd $HAR/dos/release
for m in */translation/*-ko.dat.vocab.tsv; do g=${m%%/*}; K="python3 -I $SRC/tools/korean/k6bake.py"
	$K build $m $BLD/korean-vocab/$g.v2.dat --format 2 >/dev/null
	$K build $m $BLD/korean-vocab/$g.v3.dat --game $g --fingerprint 00000000000000000000000000000000 >/dev/null
	$K dump $BLD/korean-vocab/$g.v2.dat > $BLD/korean-vocab/$g.tsv; done
cp $BLD/korean-vocab/kq1.tsv $BLD/kq1.tsv.good && sed -i '5s/\t[0-9]*\t/\t9999\t/' $BLD/korean-vocab/kq1.tsv
cd $BLD && ./test/runner KoreanVocabTestSuite      # fails: "Expected (lines[k] == want)" on the kq1 row changed
mv $BLD/kq1.tsv.good $BLD/korean-vocab/kq1.tsv && ./test/runner KoreanVocabTestSuite   # Running cxxtest tests (11 tests)...........OK!
```

  Leave `$BLD/korean-vocab/` in place (Task 15 runs with it).
- [ ] **Step 4: commit**

```
TEST: Read the harness's Korean word tables when they are given

With a korean-vocab/ folder in the working directory (version 2 and 3
tables and a dump of each, made by tools/korean/k6bake.py from the
harness masters), every dumped row must be found by the reader, in
order. Without the folder the test passes without looking.

Assisted-by: Claude Code
```

### Review checkpoint B (after Task 11)

superpowers:requesting-code-review on Tasks 7-11. Focus: every read of `korean_vocab.cpp` stays inside `_size`
(header size checked against `kHeadV3 + H <= _size`, `memchr` bounded by `H`, `entriesAt + count * entrySize <= _size`
in 64 bits, each `offset + length <= _blobUnits` in 64 bits, done before `compareEntries`); `unload()` on every refusal
path (no half-loaded table answers lookups); `checkTranslationHeader` warns once per load; the Python and C++ layouts
agree (the golden test is the guard); `harness-check` reads the harness only. Byte order: all fields little-endian
through `READ_LE_*` (no struct casts).

## Task 12: Languages of a game folder (15 min)

- [ ] **Step 1: fixture tree and test** — ten one-line files (exact names matter: the case of `JA/`, `SCI-KO.STR`,
  `Sci-Zh.Str` is part of the test):

```sh
cd $SRC/test/common && G=language_folder_game && mkdir -p $G/ko $G/JA $G/de $G/patches $G/zh &&
printf '0\t5\tx\n' > $G/ko/sci-ko.str && printf '[map]\nversion=3\n' > $G/JA/HIRESVF.MAP &&
printf 'not a translation\n' > $G/de/readme.txt && printf '0\t5\tx\n' > $G/patches/sci-ko.str &&
printf '0\t5\tx\n' > $G/sci-fr.str && printf '0\t5\tx\n' > $G/SCI-KO.STR && printf '0\t5\tx\n' > $G/sci-xx.str &&
printf 'm1:1\tx\n' > $G/agi-it.str && printf 'x\n' > $G/it.trs && printf '0\t5\tx\n' > $G/zh/Sci-Zh.Str &&
find $G -type f | sort | wc -l    # 10
```

  `test/common/language_folder.h`:

```cpp
#include <cxxtest/TestSuite.h>

#include "common/fs.h"
#include "common/language_folder.h"

#include "test/system/null_osystem.h"

// translationFolderLanguages() over the fixture game folder test/common/language_folder_game/:
//   ko/sci-ko.str  JA/HIRESVF.MAP  zh/Sci-Zh.Str  de/readme.txt  patches/sci-ko.str
//   sci-fr.str  SCI-KO.STR  sci-xx.str  agi-it.str  it.trs
class LanguageFolderTestSuite : public CxxTest::TestSuite {
	static Common::FSList game() {
		Common::FSList files;
		const Common::FSNode dir(Common::Path(__FILE__, '/').getParent().join("language_folder_game"));
		TS_ASSERT(dir.getChildren(files, Common::FSNode::kListAll));
		return files;
	}

	static Common::String codes(const Common::Array<Common::Language> &langs) {
		Common::String s;
		for (uint i = 0; i < langs.size(); ++i)
			s += Common::String(i ? " " : "") + Common::getLanguageFolderCode(langs[i]);
		return s;
	}

public:
	void setUp() override {
		Common::install_null_g_system();
	}

	void tearDown() override {
		Common::uninstall_null_g_system();
	}

	void test_tableName() {
		TS_ASSERT_EQUALS(Common::translationTableName("sci-%s.str", Common::KO_KOR), "sci-ko.str");
		TS_ASSERT_EQUALS(Common::translationTableName("%s.trs", Common::ZH_TWN), "zh.trs");
		TS_ASSERT_EQUALS(Common::translationTableName("sci-%s.str", Common::UNK_LANG), "");
		TS_ASSERT_EQUALS(Common::translationTableName("sci.str", Common::KO_KOR), "");
		TS_ASSERT_EQUALS(Common::translationTableName("%s-%s.str", Common::KO_KOR), "");
		TS_ASSERT_EQUALS(Common::translationTableName("%d-%s.str", Common::KO_KOR), "");
		TS_ASSERT_EQUALS(Common::translationTableName(nullptr, Common::KO_KOR), "");
	}

	// SCI: ko/ by its table, JA/ by its map, zh/ by its table in another case, fr by the loose file; de/ holds
	// neither, patches/ is no language, sci-xx.str no code, SCI-KO.STR the same language as ko/.
	void test_sci() {
		const Common::Array<Common::Language> l = Common::translationFolderLanguages(game(), "sci-%s.str");
		TS_ASSERT_EQUALS(codes(l), "fr ja ko zh");
		TS_ASSERT_EQUALS(l.size(), 4u);
		if (l.size() == 4) {
			TS_ASSERT_EQUALS(l[0], Common::FR_FRA);
			TS_ASSERT_EQUALS(l[2], Common::KO_KOR);
			TS_ASSERT_EQUALS(l[3], Common::ZH_ANY);
		}
	}

	void test_otherPatterns() {
		TS_ASSERT_EQUALS(codes(Common::translationFolderLanguages(game(), "agi-%s.str")), "it ja");
		TS_ASSERT_EQUALS(codes(Common::translationFolderLanguages(game(), "%s.trs")), "it ja");
		TS_ASSERT_EQUALS(codes(Common::translationFolderLanguages(game(), "sci.str")), "");
		TS_ASSERT_EQUALS(codes(Common::translationFolderLanguages(Common::FSList(), "sci-%s.str")), "");
	}
};
```

- [ ] **Step 2: run, expect a compile failure** (`common/language_folder.h` missing).
- [ ] **Step 3: implement** — `common/language_folder.h`:

```cpp
#ifndef COMMON_LANGUAGE_FOLDER_H
#define COMMON_LANGUAGE_FOLDER_H

#include "common/array.h"
#include "common/fs.h"
#include "common/language.h"
#include "common/str.h"

namespace Common {

/**
 * The name of a translation's table for a language: @p pattern with its one "%s" replaced by the language's folder
 * code (getLanguageFolderCode()): "sci-%s.str" gives "sci-ko.str", "%s.trs" "ko.trs". Empty for UNK_LANG or a pattern
 * without exactly one "%s".
 */
String translationTableName(const char *pattern, Language lang);

/**
 * The languages a game folder holds a translation in, for a detector that adds a variant per language
 * (DESIGN.md section 4). @p files are the game folder's entries, files and folders. A language counts when
 *
 * - a subfolder named by its folder code (ko/, any case) holds its table (translationTableName(@p pattern), any case)
 *   or a hi-res text map (hires*.map, any case), or
 * - the game folder itself holds a file whose name is @p pattern with a known folder code (a pack made before
 *   language folders: sci-ko.str beside the game).
 *
 * Sorted by folder code, each language once. A folder that is no language's code (patches/, kr/) is not looked into.
 */
Array<Language> translationFolderLanguages(const FSList &files, const char *pattern);

} // End of namespace Common

#endif
```

  `common/language_folder.cpp`:

```cpp
#include "common/language_folder.h"

namespace Common {

// The text before and after the one "%s" of @p pattern; false unless there is exactly one.
static bool splitPattern(const char *pattern, String &head, String &tail) {
	const char *p = pattern ? strstr(pattern, "%s") : nullptr;
	if (!p || strstr(p + 2, "%s") || strchr(p + 2, '%') || memchr(pattern, '%', p - pattern))
		return false;
	head = String(pattern, p - pattern);
	tail = String(p + 2);
	return true;
}

String translationTableName(const char *pattern, Language lang) {
	String head, tail;
	const char *code = getLanguageFolderCode(lang);
	if (!*code || !splitPattern(pattern, head, tail))
		return String();
	return head + code + tail;
}

// A language folder holds the table @p table or a hi-res text map.
static bool holdsTranslation(const FSNode &folder, const String &table) {
	FSList children;
	if (!folder.getChildren(children, FSNode::kListFilesOnly))
		return false;
	for (uint i = 0; i < children.size(); ++i) {
		const String name = children[i].getName();
		if (name.equalsIgnoreCase(table) || (name.hasPrefixIgnoreCase("hires") && name.hasSuffixIgnoreCase(".map")))
			return true;
	}
	return false;
}

Array<Language> translationFolderLanguages(const FSList &files, const char *pattern) {
	Array<Language> out;
	String head, tail;
	if (!splitPattern(pattern, head, tail))
		return out;
	for (uint i = 0; i < files.size(); ++i) {
		const FSNode &node = files[i];
		const String name = node.getName();
		Language lang = UNK_LANG;
		if (node.isDirectory()) {
			lang = parseLanguageFolderCode(name);
			if (lang == UNK_LANG || !holdsTranslation(node, translationTableName(pattern, lang)))
				continue;
		} else {
			if (name.size() <= head.size() + tail.size() || !name.hasPrefixIgnoreCase(head.c_str()) ||
			    !name.hasSuffixIgnoreCase(tail.c_str()))
				continue;
			lang = parseLanguageFolderCode(String(name.c_str() + head.size(), name.size() - head.size() - tail.size()));
			if (lang == UNK_LANG)
				continue;
		}
		// Keep the list sorted by folder code, each language once.
		const String code = getLanguageFolderCode(lang);
		uint at = 0;
		while (at < out.size() && code.compareTo(getLanguageFolderCode(out[at])) > 0)
			++at;
		if (at < out.size() && code == getLanguageFolderCode(out[at]))
			continue;
		out.insert_at(at, lang);
	}
	return out;
}

} // End of namespace Common
```

  In `common/module.mk`, insert `	language_folder.o \` after `	language.o \`.
- [ ] **Step 4: run** — `heavy make -j16 test` → `608 tests ... OK!`.
- [ ] **Step 5: commit** (the test, the ten fixture files, the code, `common/module.mk`)

```
COMMON: Find the languages a game folder has translations in

A translation keeps its files in a folder named by the language's
code (ko/), or, in older packs, beside the game (sci-ko.str). List the
languages a game folder holds a translation table or a hi-res text
map for, so a detector can offer a variant per language. The table
name is a pattern, so SCI, AGI and SCUMM files all fit.

Assisted-by: Claude Code
```

## Task 13: ASan, mingw, the full binary (15 min, no commit unless a fix is needed)

```sh
A=~/work/tmp/scummvm2026-p2asan; mkdir -p $A && cd $A
$SRC/configure --disable-all-engines --enable-debug --disable-optimizations --enable-asan >/dev/null
heavy make -j16 test          # 608 tests OK, no AddressSanitizer/UndefinedBehavior report
cp -r $BLD/korean-vocab . && ./test/runner KoreanVocabTestSuite   # 11 tests OK under ASan with the harness tables

M=~/work/tmp/scummvm2026-p2mingw; mkdir -p $M && cd $M
( . ~/.local/mingw/env.sh >/dev/null; export PATH=$MINGW_SYS/bin:$PATH
  $SRC/configure --host=x86_64-w64-mingw32 --with-sdl-prefix=$MINGW_SYS --disable-all-engines --enable-debug --disable-optimizations >/dev/null &&
  heavy make -j16 test/runner )   # test/runner.exe builds (links hardware-input.o); running it is UNVERIFIED (no wine)

cd $BLD && heavy make -j16 scummvm   # links; sdl2-events.o and hardware-input.o are built with the P2 lines
```

A fix found here is committed as `COMMON: ...` (or `TEST: ...`) with the trailer.

## Task 14: Patch list and notes (10 min)

- [ ] `docs/korean/PATCHES.md`: add rows

```
| `ko/02-common-korean` | `common/keyboard.h` | `KEYCODE_HANGUL`, `KEYCODE_HANJA` before `KEYCODE_LAST` | the Han/Yeong key reaches engines |
| `ko/02-common-korean` | `backends/keymapper/hardware-input.cpp` | `HANGUL`, `HANJA` in `defaultKeys` | keymaps can bind the keys |
| `ko/02-common-korean` | `backends/events/sdl/sdl2-events.cpp`, `sdl3-events.cpp` | `SDL_SCANCODE_LANG1/2` cases | SDL delivers the keys |
| `ko/02-common-korean` | `common/module.mk` | 5 objects (`hangul`, `korean_stem`, `korean_vocab`, `language_folder`, `translation_file`) | build the Korean common code |
| `ko/02-common-korean` | `test/module.mk` | `TEST_LIBS += backends/keymapper/hardware-input.o` | the key name test |
```

- [ ] `docs/korean/plans/P2-notes.md`: deviations from this plan (if any) and the Task 15 results, as P1-notes did.
- [ ] Commit (this plan file, untracked until now, goes in too)

```
DOCS: Korean: Record the P2 plan, its upstream files and the acceptance run

Assisted-by: Claude Code
```

## Task 15: Final acceptance

- [ ] `cd $BLD && heavy make -j16 test` → `Running cxxtest tests (608 tests)...OK!` (N0 + 74).
- [ ] With the harness tables: `cd $BLD && ./test/runner KoreanVocabTestSuite` (with `korean-vocab/` from Task 11) →
  11 tests OK.
- [ ] ASan clean; mingw `test/runner.exe` builds; `make scummvm` links (Task 13).
- [ ] `python3 -I $SRC/tools/korean/test_k6bake.py` → 8 tests OK.
- [ ] Done-criterion: `python3 -I $SRC/tools/korean/k6bake.py build $HAR/dos/release/kq1/translation/sci-ko.dat.vocab.tsv $BLD/kq1-sci-ko.dat --format 2`
  prints sha256 `48cc43e4720ef98b4790366c6cecb3c2cf4e4f091a9ac3021520e0882645ed75`, the harness's KQ1 table;
  `k6bake.py harness-check $HAR` → nine `ok`, rc 0.
- [ ] Ported files identical to `jamo` b4912e3c600 but for the named edits (Review checkpoint A commands).
- [ ] No engine touched: `git -C $SRC diff --stat d6b3b963ae8.. -- engines gui base graphics` is empty.
- [ ] Upstream files touched are exactly those of the Task 14 rows:
  `git -C $SRC diff --stat d6b3b963ae8.. -- . ':!common/hangul.*' ':!common/korean_*' ':!common/language_folder.*' ':!common/translation_file.*' ':!test/common' ':!test/korean_vocab_*' ':!tools/korean' ':!docs/korean'`
  lists only `common/keyboard.h`, `common/module.mk`, `backends/keymapper/hardware-input.cpp`,
  `backends/events/sdl/sdl2-events.cpp`, `backends/events/sdl/sdl3-events.cpp`, `test/module.mk`.
- [ ] Every commit passes `devtools/check-commit-msg.py` and ends with exactly one `Assisted-by: Claude Code` and no
  `Co-Authored-By` (empty lines skipped, the fix P1-notes item "Commit messages" found):
  `for c in $(git -C $SRC rev-list d6b3b963ae8..); do git -C $SRC log -1 --format=%B $c > $BLD/msg; python3 $SRC/devtools/check-commit-msg.py $BLD/msg >/dev/null || echo BAD $c; sed '/^$/d' $BLD/msg | tail -n 1 | grep -qx 'Assisted-by: Claude Code' || echo NOTRAILER $c; [ $(grep -c '^Assisted-by:' $BLD/msg) = 1 ] || echo TRAILERS $c; grep -q '^Co-Authored-By' $BLD/msg && echo COAUTHOR $c; done`
  prints nothing.

Mapping to DESIGN section 11, row P2:

| P2 scope item / done-criterion | Evidence |
|---|---|
| `hangul` | Task 4 (`HangulTestSuite` 17, ported) + Task 5 (`normalizeKoreanInput`, 3; section 1 item 3) |
| `korean_stem` (+ corpus) | Task 6 (`KoreanStemTestSuite` 30 incl. the 1,197-form corpus, ported) |
| `korean_vocab` v3 (u32 offsets, header) | Tasks 7-8 (`TranslationFileHeaderTestSuite` 6, `KoreanVocabTestSuite` 9), Tasks 10-11 (golden + harness tables); layout section 1 item 4 |
| `language_folder` | P1 (codes) + Task 12 (`LanguageFolderTestSuite` 3; section 1 item 7) |
| `KEYCODE_HANGUL` (+ `backends/keymapper`) | Tasks 1-3 (`KeyboardHangulTestSuite` 2; SDL compile) |
| `resolveKoreanParticles` | Task 4 (`KoreanParticlesTestSuite` 2, ported) |
| tests green | Task 15: 608 OK, ASan clean |
| `k6bake.py` in `tools/korean` rebuilds the KQ1 table byte-identical | Task 9 Step 4 / Task 15 (sha256 `48cc43e4...`; all nine tables); section 1 item 1 |
| stem rule regressions guarded by the corpus | the corpus test runs in every `make test` |

## 4. Regression data P2 can already use, and what the harness must provide

Usable now (no engine needed):

- `test/common/korean_stem_corpus.txt` (1,199 lines, made by the harness's `k9stemcorpus.py` with Kiwi from the
  Camelot and LB1 maps): committed, runs in `make test`. Regenerating it needs Kiwi and stays in the harness.
- The nine harness masters `$HAR/dos/release/<game>/translation/*-ko.dat.vocab.tsv` and `translations.tsv`:
  byte identity (`harness-check`) and the reader over every row (Task 11, `korean-vocab/`).
- The five `k3map.json`/`k2targets.json` pairs (kq4, pq2, qfg1e, qfg2, lsl1a): the legacy bake path.

Not usable before an engine exists: the `k16said.py` said() baselines of section 6 (KQ1 303/310 ... QFG1 EGA 855/956;
they need the SCI parser and the debug socket: P3/P4, AGI P5), `sci-ko.str`/`agi-ko.str` (P3/P5), smoke scripts.

The harness repository (read-only here) must provide, for P2: the masters and `translations.tsv` as they are
(2026-11-22 state: nine `.vocab.tsv` rows with sha256 in column 5), and the `#KOREAN-VOCAB 2` master format (first
line exact, `@magic`, `@class <group> <hex>`, `word TAB groups [TAB English]`, `,` alternatives, `+` sequences).
For the packs after P3 (harness work, not P2): its builders call `tools/korean/k6bake.py build MASTER OUT --game ID
--fingerprint MD5 --vocab-md5 MD5` with the md5s P3/P5 define; `trs_master.py` learns to read version 3 tables
(`vocab_rows` refuses any version but 2 today); the harness `k6bake.py` becomes a pointer to the ScummVM copy
(decision 7); `strcheck.py` (decision 7) arrives with the `.str` header in P3.

## 5. Left for later phases (do not do in P2)

- P3: `Sci::TextMode` and `text_encoding`/`korean_text`; reading the `.str` first line with `TranslationFileHeader`
  in `ScriptStrings` and the SCI fingerprint definition (which files, in which order); loading `sci-ko.dat`/`SCIKOR.DAT`
  with `KoreanVocabulary::load(..., expected)`; `korean_parser` (arrangement, `joinKoreanPairs` over `lookupJoined`,
  roles to prepositions); the SCI composer wrapper (`Sci::KoreanComposer`) and the edit control; the `HANGULTOGGLE`
  keymap action and its default bindings; the `hangul_input/start/builtin` keys; `kFormat` particles (with
  `resolveKoreanParticles`) and the plural-"s" drop; `-ko` detection variants with `translationFolderLanguages(files,
  "sci-%s.str")`; `strcheck.py`.
- P5: the same for AGI (`agi-ko.str` header, `AGIKOR`/`agi-ko.dat`, `korparse` over `lookupExact`/`lookupJoined`).
- P6: SCUMM `.trs` detection through `translationFolderLanguages(files, "%s.trs")`.
- P7: DOS keyboard (`backends/events/dos` -> `KEYCODE_HANGUL`), lazy `encoding.dat` tables and their test, file cache
  statistics.
- P8: Android Hangul keyboard layouts.

