# P2 review: common Korean (2026-11-22)

Independent read-only review of `a74a1f4ccb4..8b24c4944e7` on `ko-2026.4` (13 commits, 40 files, +5,815), the code read as
new including the files ported from `jamo` `b4912e3c600`. Plan and notes were not taken as proof; every claim below that is
not marked UNVERIFIED was run. Scratch: `~/work/tmp/p2-review/` (ASan/UBSan harness `fuzz.cpp`, `build.sh`, `create_project`
build under `cp/`), existing builds `~/work/tmp/scummvm2026-p2build` (617 tests + `korean-vocab/`) and `-p2verify`.

## Verdict

**P2 is a sound base for P3+.** No blocker. One major (a cheap API gap in the shared header parser that would silently
defeat the fingerprint policy for BOM'd `.str` files), a handful of minors, mostly in `k6bake.py`. The state machines,
the table reader and the stemmer held up under property checks and ~86,000 ASan/UBSan mutants with no finding.

Required before P3 relies on these interfaces (all small):

1. `TranslationFileHeader::parse` must skip a leading UTF-8 BOM (major, below).
2. `k6bake.py` must refuse a word longer than `KoreanStems::kMaxLen` (16 code units) and `test_harness_tables_when_given`
   must drop its `lookedUp` mask, so "every stored word is findable" is proved, not assumed (minor 2).
3. Decide the `vocab=` policy for version 3 word tables: either `k6bake.py build/convert --format 3` requires
   `--vocab-md5`, or P3 must not put `vocab` into `expected` (minor 3). As it stands a table built without it is refused the
   moment P3 passes the game's vocab md5.

Everything else is optional polish.

## Findings (ranked)

### Major

**M1. A UTF-8 BOM hides the fingerprint header** — `common/translation_file.cpp:52` (`text.hasPrefix("#!")`).
DESIGN section 4 says `.str`/`.trs` files are "UTF-8 (BOM optional)" and decision 4 makes the header the guard against a
table made for another game. A `.str` saved with a BOM (every Windows editor's default for UTF-8) parses as "no header":
`parse` returns false with `error` empty, the caller treats it as a pre-header pack and loads it *unchecked*, and a header
naming another game is never compared. Reproduction (harness, `./fuzz ... ` prints it):

```
BOM header: parse=0 present=0 error=''
```

for the line `"\xEF\xBB\xBF#! format=1 game=kq1 fingerprint=... engine=sci"`. The v3 word table is not affected (binary,
k6bake writes ASCII). Fix (3 lines): in `parse`, after the CR/LF trim, `if (text.hasPrefix("\xEF\xBB\xBF")) text = String(text.c_str() + 3);`
and a test line in `test/common/translation_file.h::test_parseIsLenient`. Doing it in the shared parser means P3 and P5
cannot each forget it.

### Minor

**m2. `k6bake.py` writes words the reader can never find; the harness test masks it** — `tools/korean/k6bake.py:98`
(only `> 0xFFFF` is refused), `common/korean_vocab.cpp:240` (`word.size() > KoreanStems::kMaxLen` returns 0),
`test/common/korean_vocab.h:315-326` (`lookedUp` skips such words). Reproduction:

```
$ printf '...\n가나다라마바사아자차카타파하가나다\t1\tseventeen\n문\t1\tdoor\n' > long.vocab.tsv
$ python3 -I tools/korean/k6bake.py build long.vocab.tsv long.dat --format 2        # rc=0, no warning
$ ./fuzz ... long.dat 가나다라마바사아자차카타파하가나다
lookupExact(가나다라마바사아자차카타파하가나다, 17 cps) = 0
```

Is it a bug for real vocabulary? **No, today**: the nine harness masters were scanned (`korean-vocab/*.tsv`): the longest
stored word is 8 code units (`벽걸이타피스트리`, `물속으로들어가다`, `브라키오사우루스`), half the cap; a parser word is a
syllable word or a joined phrase, 16 is ample. It is a contract gap: the baker and the reader disagree about the format's
domain, and the test that should catch it has been taught to look away. Fix: `table_bytes` raises `BakeError` for
`len(u) > 16` (name the constant and `common/korean_stem.h` in the message; `read_table` likewise, so `convert`/`dump` of an
old table reports it); `test_k6bake.py` gets a refusal case; `korean_vocab.h` test drops `lookedUp` and asserts `n == rows`
unconditionally. The 16 then lives in two places (Python and C++): acceptable, the golden test and the harness dump test
pin them together.

**m3. `vocab=` is optional for a version 3 word table but DESIGN makes it part of v3** — `k6bake.py:257-258`
(`header_line(..., vocab)` with `vocab=None` allowed), `common/translation_file.cpp:174-180` (an expected `vocab` against a
file without one is a mismatch: `vocab=(none)`), `test/common/translation_file.h::test_check` pins that behaviour.
DESIGN section 4: "`SCIKOR.DAT` gets version 3: u32 offsets, the same fingerprint and the vocab's md5". Reproduction:
`k6bake.py build long.vocab.tsv x.dat --game kq1 --fingerprint 0123...` → rc 0, header without `vocab=`; such a table is
refused by `KoreanVocabulary::load` as soon as the engine fills `expected.vocab` (the `test_fingerprint_is_checked` path
`other.vocab = "000..."` shows the refusal). Pick one: (a) `build`/`convert --format 3` require `--vocab-md5` (one `if` in
`versioned`, keep `golden` as is), or (b) P3 leaves `expected.vocab` empty. (a) matches DESIGN.

**m4. Latin "ng" endings read as a vowel** — `common/hangul.cpp:303-304` (`l` → ㄹ, `m`/`n` → consonant, else vowel).
`king이(가)` → `king가` (harness sample output); Korean reads `king` as 킹, final ㅇ, so 이. Same for any `-ng` name
(`Wang`, `Ling`). The design note says "by l, m, n ending in a consonant", so this is a known approximation; a one-line
extension (`g` preceded by `n` → 1) covers the common case. Not a P2 blocker: `kFormat` particles are P3.

**m5. The developer-data test skips silently** — `test/common/korean_vocab.h:279-282` returns without a word when
`korean-vocab/` is absent (P1's `test_packMapsWhenGiven` does the same). The 617-test run then looks identical with and
without the 27 harness tables. Add `TS_TRACE("korean-vocab/ not in the working directory: harness table check skipped")`
(CxxTest prints traces without failing) so the acceptance log shows which run had the data.

**m6. `test_k6bake.py` writes under `/tmp` by default** — `tools/korean/test_k6bake.py:85, 98` (`tempfile.TemporaryDirectory()`;
`python3 -I -c "import tempfile; print(tempfile.gettempdir())"` → `/tmp`). The notes ran it with `TMPDIR` set; the
convention of this host is scratch under `~/work/tmp`. Pass `dir=` (e.g. a `k6bake-test/` under the current working
directory, as the C++ tests take developer data from the working directory) or document `TMPDIR` in the module docstring.
No other P2 test or tool writes anything (`korean_vocab.h`, `korean_stem.h`, `language_folder.h` only read).

**m7. `k6bake.py` crashes with a traceback instead of a `k6bake:` message on bad input** — all reproduced, rc 1 with a
Python traceback rather than rc 2:

- `build … --format x` → `ValueError` (`k6bake.py:252`)
- `dump` of a v3 file shorter than 20 bytes → `struct.error` (`:123`; the v2 path checks `len(data) < 16`, v3 does not)
- a v3 header with a non-ASCII byte → `UnicodeDecodeError` (`:127`)
- missing/invalid `--targets`/`--verbs` JSON, a master without a `map` key → `OSError`/`JSONDecodeError`/`KeyError` (`:242-244`)

Fix: `int()` and the JSON loads inside `try` → `BakeError`; `len(data) < 20` check for v3; `decode("ascii")` → `BakeError`.
Pure ergonomics; no wrong table is ever written (the read-back check at `:373` guards that).

### Nit

**n8.** `KoreanVocabulary::loadFromStream` warns "has no fingerprint header … loaded unchecked" for every version 2 table
even when `expected` is empty (`korean_vocab.cpp:128-130`): noise in tools and tests (each fixture load prints it). Skip
the check when no field of `expected` is set, or let the caller decide.

**n9.** `TranslationFileHeader::parse` accepts `format=0001` (`atoi`) and `#!format=1` (no blank after `#!`); `game`/`engine`
compare case-sensitively while md5s do not. All documented or harmless; `toString` always writes the canonical form.

**n10.** `loadFromStream` assumes the stream is at position 0 (`in.size()` then `read(_size)`): a stream handed over after a
partial read is "could not be read in full". Document it or `seek(0)`.

**n11.** `HangulComposer::_out` has no cap and `text()` copies it per keystroke; the edit control owns the limit (P3). Say so
in the class comment.

**n12.** The stem corpus (`korean_stem_corpus.txt`) was filtered by the Python mirror of these rules ("kept the forms the
Python mirror … resolved to the right entry", `korean_stem.h:399-405`): it is a regression guard, not an independent
oracle, so a form the rules never reached is not in it. Fine for row P2 ("stem rule regressions (corpus guards)"); the
independent measure is `k16said` in P3/P4.

## What was checked and found sound

**HangulComposer / normalizeKoreanInput** (`common/hangul.cpp`). Read the automaton over the full key map (33 keys, 19/21/27
jamo, 7 compound vowels, 11 compound finals): vowel steals a pending final with the compound split, a consonant after a
lone vowel starts a new syllable (the `_jong`-without-`_cho` dead path is unreachable from `feed`, as the comment says),
ㄸ/ㅃ/ㅉ never become a final, double finals only from the table, backspace peels compound jong/jung then cho then one
committed character. Property check (harness `checkComposer`): 90,000 random sequences of jamo keys, shifted keys, ASCII
and backspace/flush — the jamo stream of `text()` (syllables and compat jamo decomposed, compounds split) always equals
the typed jamo stream; a backspace removes exactly one jamo or one whole committed character; `preedit()` is always the
last code point of `text()`; every output is a syllable, a compat jamo or the ASCII byte. `0 bad` on four seeds.
`normalizeKoreanInput`: L+V, LV+T, L+V+T, range edges (U+1112/U+1175/U+11C2 → 힣, U+11A7 not a final, LVT + T kept),
full-width U+FF01..FF5E and U+3000 — matches Unicode composition; compat jamo kept as documented. Non-BMP input passes
through. No crash on random U32 input under ASan/UBSan.

**Particle picker** (`resolveKoreanParticles`). Final-consonant rule with the ㄹ exception ((으) drops after ㄹ, 을/이/은/과
keep their consonant form), digits by Korean numeral (1/7/8 → ㄹ; 0/3/6 → consonant; 2/4/5/9 → vowel; multi-digit numbers
end on the right digit so 100 → 백, 12 → 십이 work), quotes/brackets looked through, nothing readable (marker first, a
space, a non-Hangul code point) keeps both forms. 45,000 random byte strings with markers, truncated UTF-8 and bytes ≥
0x80: ASan clean, output never longer than input. Only m4 above.

**Stemmer** (`common/korean_stem.cpp`, byte-identical to `jamo`). Every `head()`/`last()` is guarded by an `endsWith*` that
demands a non-empty remainder; `Collector::add` bounds `cp[48][16]` and rejects non-BMP; recursion in `processStem`,
`stripParticles`, `peelParticles` always shortens the string or steps down the particle stack (≤ 3 levels); input is capped
at 16 code units so cost is a constant. 45,000 random words (Hangul-only and mixed, up to 20 code units, non-BMP included):
no sanitizer finding, candidate lengths always in 1..16. The 30 suite tests (1,197 corpus forms, 100% reached and first-right)
pass in `p2build`.

**Fingerprint header** (`common/translation_file.cpp`). Hostile lines: 45,000 random and mutated headers under ASan/UBSan,
20,000-character lines, CR/LF, tabs, any key order, unknown keys skipped, duplicate keys refused, `format` ≤ 4 digits,
md5 case folded on parse and compared case-insensitively, `toString` round-trips every accepted header. Only M1 (BOM)
and n9.

**Word table reader** (`common/korean_vocab.cpp`). Bounds: file < 16 B / > 16 MB, v3 head, header size (0, %4, ≤ 1024,
within file), NUL in header text, 64-bit `entriesAt + count * entrySize`, per-entry `offset + length ≤ blobUnits` in 64-bit,
`length == 0` refused, adjacent-order check (non-decreasing, so duplicates of one word stay adjacent for `collect`),
`unload()` on every refusal, `READ_LE_*` for every field, u16/u32 offset by version, v2/v3 detection by the version word,
unknown version refused. Binary search uses the same order as the load check (unsigned code units, shorter first), so a
sorted table can never miss. Mutation fuzz of the real 18 tables built by `k6bake.py` from the nine harness masters
(v2 and v3, 15 KB–94 KB; bit flips, byte overwrites, truncation, random u32/u16 fields, 0xFFFFFFFF count/header size,
header text corruption, appended garbage), each mutant loaded and, when accepted, hammered with `lookupExact`/`lookupAll`/
`lookupJoined` on stored and random words: 86,400 mutants (4 seeds), 23,000 accepted, 0 sanitizer reports, `isLoaded()`
false after every refusal. The 11 suite tests plus the harness-dump test over 27 files pass in `p2build`.

**k6bake.py.** Deterministic (stable sort on code units, no timestamps, dict order is file order); `--format 2` of the
KQ1 master reproduces the harness sha256 `48cc43e4…` (notes' run re-read; `harness-check` logic reads the sha from
`translations.tsv`, so it is not a self-comparison); the read-back check refuses to write a table it cannot re-read as the
same rows; the version 2 65,535-unit limit is enforced (test `test_refusals`). Universal newlines handle CRLF masters; a
BOM at the file start is refused with the master-tag message. `python3 -I` works for the tests (the test adds its own
directory to `sys.path`). Tests: layouts are checked against literal bytes, the committed golden is checked against the
generator *and* the C++ reader checks the golden's rows against the master's content (`test_golden_tables_from_k6bake`),
so a generator that froze wrong bytes would fail the C++ side. `tools/korean/testdata/mini.vocab.tsv` covers alternatives,
a sequence, and a prefix pair (`문`/`문들` via the C++ test). Only m2, m3, m6, m7.

**translationFolderLanguages** (`common/language_folder.cpp`). Pattern with exactly one `%s` (any other `%` refused),
prefix/suffix matched ignoring case, folder code parsed ignoring case, subfolder counted by its table or a `hires*.map`,
`patches/` and unknown codes skipped, result sorted by folder code and deduplicated (`zh` variants collapse to `ZH_ANY`,
`SCI-KO.STR` beside `ko/` counts once). All names fit 8.3 (`sci-ko.str`, `ko/`). FSNode use is `getChildren(kListFilesOnly)`
per language folder only. The fixture folder (10 files, 5 folders, mixed case) exercises each rule; 3 tests pass.

**Key codes and keymapper.** `KEYCODE_HANGUL = 357`, `KEYCODE_HANJA = 358` sit after `KEYCODE_AUDIOFASTFORWARD = 356`
and before `KEYCODE_LAST`; every `KEYCODE_LAST` user in the tree sizes an array by it (qdengine, watchmaker, hpl1, ags,
gamos, icb), so the two new codes only grow those arrays. `sdl2-events.cpp:163` maps any code in `[NUMLOCK, LAST)` to
ascii 0, so the new keys carry no character. `defaultKeys` gains `HANGUL`/`HANJA` in the key block (plain descriptions
like the neighbouring entries; `_s()` is only used for the mouse block); the names resolve by string and by event, with
`S+` modifier (2 tests). SDL2: `SDL_SCANCODE_LANG1 = 144`, `LANG2 = 145` (`/usr/include/SDL2/SDL_scancode.h:258-259`), no
`SDLK_` of those values, so `case SDL_SCANCODE_TO_KEYCODE(...)` cannot duplicate a label; `sdl2-events.o` compiles and links
in the SDL2 `make scummvm`. SDL3: not compiled by the configure here, but checked against the SDL 3.5.0 headers of
`~/opt/sdl3-dos/include/SDL3`: same scancode values (`SDL_scancode.h:267-268`), `SDL_SCANCODE_TO_KEYCODE` is
`(X | SDLK_SCANCODE_MASK)` (`SDL_keycode.h:62-63`), no `SDLK_LANG1/2`; `g++ -fsyntax-only` of `sdl3-events.cpp` with those
headers and `-DUSE_SDL3`: rc 0, no error. Delivery of LANG1 by a real Korean keyboard on X11/Windows/SDL3: UNVERIFIED
(no such keyboard here; DESIGN section 7 notes X11 often sends right Alt instead).

**Build integration.** `create_project` built in `~/work/tmp/p2-review/cp/` (CMake, rc 0): `--cmake` output
`CMakeLists.txt` and `--msvc --msvc-version 17` output `scummvm.vcxproj` + `.filters` each list all five of
`common/hangul.cpp`, `korean_stem.cpp`, `korean_vocab.cpp`, `language_folder.cpp`, `translation_file.cpp` (513 ClCompile
entries). `common/module.mk` keeps alphabetical order; `test/module.mk` adds `hardware-input.o` to `TEST_LIBS` (links on
Linux and mingw per the notes).

**Upstream hygiene.** GPL header and `COMMON_*_H` guard in every new production file, tabs throughout (leading-space lines
are the comment blocks only), `namespace Common {` … `} // End of namespace Common`, no trailing whitespace in the new
files. Ported files: `korean_stem.{h,cpp}`, `test/common/hangul.h`, `korean_particles.h`, the corpus are byte-identical to
`jamo` `b4912e3c600` (`cmp`); `hangul.cpp` is a pure addition (`normalizeKoreanInput`, 0 removed lines); `hangul.h` differs
by the corrected doc paragraph and the new declaration; `test/common/korean_stem.h` differs only by the `__FILE__`
corpus path (works in-tree and out-of-tree: cxxtestgen passes the suite path as given, `./test/common/…` or absolute).
`PATCHES.md` rows name exactly the six modified upstream files of the diff (`keyboard.h`, `module.mk`, `hardware-input.cpp`,
`sdl2-events.cpp`, `sdl3-events.cpp`, `test/module.mk`). All 14 commit messages pass `devtools/check-commit-msg.py` (rc 0 each)
and end with one `Assisted-by: Claude Code`, no `Co-Authored-By`. Tests: no time, randomness or environment dependence;
`getenv` not used; developer data from the working directory; nothing written to disk by any C++ test.

## Reproduction commands

```sh
cd ~/work/tmp/p2-review
./build.sh                                        # ASan/UBSan harness against ~/work/tmp/scummvm2026-p2verify
./fuzz ~/work/tmp/scummvm2026-p2build/korean-vocab 1500 11 long.dat 가나다라마바사아자차카타파하가나다
#   vocab fuzz: 18 files, 27000 mutants ... rc=0; BOM header: parse=0 ...; lookupExact(..., 17 cps) = 0; king이(가) -> king가
cd cp && cmake ~/work/scummvm-2026.4-korean/devtools/create_project/cmake && make && cd out \
  && ../create_project ~/work/scummvm-2026.4-korean --cmake --disable-all-engines \
  && ../create_project ~/work/scummvm-2026.4-korean --msvc --msvc-version 17 --disable-all-engines \
  && grep -l 'common.hangul.cpp' CMakeLists.txt scummvm.vcxproj
g++ -fsyntax-only -std=c++11 -Wno-multichar -DHAVE_CONFIG_H -DSDL_BACKEND -DUSE_SDL3 -DPOSIX \
  -I ~/work/tmp/scummvm2026-p2verify -I ~/work/scummvm-2026.4-korean -I ~/work/scummvm-2026.4-korean/engines \
  -I ~/opt/sdl3-dos/include ~/work/scummvm-2026.4-korean/backends/events/sdl/sdl3-events.cpp     # rc 0
cd ~/work/tmp/scummvm2026-p2build && ./test/runner KoreanVocabTestSuite   # 11 tests OK with korean-vocab/ (27 files)
```
