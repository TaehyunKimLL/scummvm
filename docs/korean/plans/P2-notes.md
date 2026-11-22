# P2 notes: deviations and acceptance (2026-11-22)

Implemented from `P2-common-korean.md`; commits `b37e99f32b8` (the plan) to the commit that adds this file, on `ko-2026.4`
(branch decision as plan section 1 item 13). Builds: `~/work/tmp/scummvm2026-p2build` (Linux), `~/work/tmp/scummvm2026-p2asan`
(ASan), `~/work/tmp/scummvm2026-p2mingw`. The code files were taken from the prototype `~/work/tmp/scummvm2026-p2-proto/`
(which carries the GPL header) and compared against the plan text afterwards: every production file and the new test files
appear in the plan's code blocks verbatim (header aside); `test/common/korean_vocab.h`, `keyboard_hangul.h` and
`test_k6bake.py` match the plan's final state, the earlier states were built from the task stages of the prototype
(`planparts/`) or by cutting the later additions. No difference between plan and prototype was found.

## Deviations from the plan

1. **Base moved.** Plan base `d6b3b963ae8` / 534 tests; HEAD was `a74a1f4ccb4` (P1 review fixes) with N0 = 543 tests. Every
   expected count is the plan's + 9 (536 -> 545 ... 608 -> 617). `git diff a74a1f4ccb4.. -- engines gui base graphics` is empty
   (the plan's check against `d6b3b963ae8` also lists the P1 review's `graphics/hires_text` changes, which are not P2's).
   The review changed no `common/` file, so Task 0's interface greps all matched unchanged.
2. **TDD red runs.** Every task was run red first (compile or link failure, or an assertion) except: Task 3 (no unit test, as the
   plan says; the check is that `sdl2-events.o` compiles), Task 9 where red was `ModuleNotFoundError`, and Task 10/11 as the
   plan describes (missing golden header; the corrupted dump row). Task 2: the `module.mk` link fix and the `hardware-input.cpp`
   entries were both prepared before the red run, so the `hardware-input.cpp` change was reverted for the run
   ("Expected (hangul.description == ...)", 1 of 2 failed) and restored for green.
3. **`TMPDIR`.** `test_k6bake.py` uses `tempfile`; it was run with `TMPDIR=$BLD/tmp` so nothing goes to `/tmp`.
4. **Plan counts.** Task 4's "23" changed lines of `hangul.h` against `jamo` is 30 by `diff | grep -c '^[<>]'` (10 removed, 20 added;
   Task 4 edit plus Task 5 addition); `hangul.cpp` is a pure addition (0 removed lines). The `korean_stem.h` test count of 7 is exact.
5. **SDL3.** `sdl3-events.cpp` was not compiled (no SDL3 on this host), as the plan says.
6. **Review checkpoints A and B** were run as the full `make test`, both `--check` scripts, the `cmp`/`diff` of the ported files
   against `jamo`, and a critical own read of the diff since the previous checkpoint; no review subagent. Found on reading, no
   code change needed: all reads of `korean_vocab.cpp` are bounded as the plan's checkpoint B lists (header size against
   `_size`, `memchr` bounded by H, 64-bit `entriesAt + count * entrySize`, 64-bit `offset + length <= _blobUnits` before any
   compare, `unload()` on every refusal path, `READ_LE_*` for every field). Observation (not a bug, plan scope): a stored word
   longer than `KoreanStems::kMaxLen` (16 code units) can never be found by `lookupExact`/`lookupAll`/`lookupJoined`; the
   harness test accounts for it (`lookedUp`). The harness masters are checked: none is affected in the nine-table run.
7. Task 13 and Task 15 needed no fix commit.

## Acceptance

- Linux `make test` (debug, no optimisations, no engines): `Running cxxtest tests (617 tests)...OK!` (N0 543 + 74)
- `./test/runner KoreanVocabTestSuite` with `korean-vocab/` (27 files made by `k6bake.py` from the nine harness masters, in
  `$BLD` and copied to the ASan build): `Running cxxtest tests (11 tests)...........OK!`
- Task 11 red run with one dump row changed: `Expected (lines[k] == want), found (가로질러\t9999\t0x0010 != 가로질러\t210\t0x0010)`,
  `Failed 1 and Skipped 0 of 11 tests`; restored: 11 tests OK.
- ASan (`--enable-asan`, 3 `-fsanitize` flags in `config.mk`): `Running cxxtest tests (617 tests)...OK!`, rc 0, no
  `Sanitizer`/`runtime error` line in the log, also with `korean-vocab/` present
- mingw (`x86_64-w64-mingw32`): `test/runner.exe` built (`PE32+ executable (console) x86-64`, 20 MB, links `hardware-input.o`); run UNVERIFIED (no wine)
- `make scummvm` (SDL2, all engines disabled): links (`LINK scummvm`, 34.7 MB), no warning from a P2 file
- `tools/korean/sync-svf.sh --check`: `sync-svf: graphics/svf matches svf-fonts-open 3d40de8d940f50ed4a28dfc1851fa776a62f42e5`, rc 0
- `python3 -I tools/korean/mkgolden.py --check`: rc 0 (no output)
- `python3 -I tools/korean/test_k6bake.py`: `Ran 8 tests ... OK` (the Task 9 stage: `Ran 7 tests ... OK`)
- `test/korean_vocab_golden.h` sha256 `29b7aa43dd19cdc3053482de84fe93687107062ffa9849dea26d4763cd9d6825`; `mini.vocab.tsv`
  `d4c58e2df35c8ad2dc16501942a83bbaf0f3400707c2773a09bec10953f53fbc` (both as the plan says)
- Ported files: `cmp` against `jamo` b4912e3c600 prints nothing for `korean_stem.{h,cpp}`, the tests `hangul.h`, `korean_particles.h`
  and the corpus; `hangul.cpp` differs only by the added `normalizeKoreanInput` (0 removed lines); `hangul.h` and
  `test/common/korean_stem.h` differ by the named edits only
- `git diff --stat a74a1f4ccb4.. -- engines gui base graphics`: empty
- Upstream files touched: `common/keyboard.h`, `common/module.mk`, `backends/keymapper/hardware-input.cpp`,
  `backends/events/sdl/sdl2-events.cpp`, `sdl3-events.cpp`, `test/module.mk` (all in `docs/korean/PATCHES.md`)
- Commit messages: all pass `devtools/check-commit-msg.py` and end with exactly one `Assisted-by: Claude Code`, no
  `Co-Authored-By` (the plan's loop run for `d6b3b963ae8..`, empty lines skipped: prints nothing)

### Done-criterion run (k6bake, harness read-only, outputs in `$BLD`), real output

```
~/work/tmp/scummvm2026-p2build/kq1-sci-ko.dat: SCIKOR version 2, 1265 entries, 16558 bytes, sha256 48cc43e4720ef98b4790366c6cecb3c2cf4e4f091a9ac3021520e0882645ed75
48cc43e4720ef98b4790366c6cecb3c2cf4e4f091a9ac3021520e0882645ed75
camelot  sci-ko.dat.vocab.tsv    72202 bytes faaa0b36bf9c62ad ok
kq1      sci-ko.dat.vocab.tsv    16558 bytes 48cc43e4720ef98b ok
kq4      sci-ko.dat.vocab.tsv    15360 bytes 3e03b8549630dd17 ok
lb1      sci-ko.dat.vocab.tsv    26890 bytes 99e5a2c604965df4 ok
lsl1a    agi-ko.dat.vocab.tsv    16340 bytes 05c118218a2c13ec ok
lsl2     sci-ko.dat.vocab.tsv    17190 bytes 7d2c16b0a899558d ok
pq2      sci-ko.dat.vocab.tsv    29158 bytes 9bcab2d835ae6a39 ok
qfg1e    sci-ko.dat.vocab.tsv    29122 bytes e80717c84b03fe40 ok
qfg2     sci-ko.dat.vocab.tsv    34664 bytes 977975ef786a22dc ok
rc=0
~/work/tmp/scummvm2026-p2build/kq4-k3.dat: SCIKOR version 2, 1168 entries, 15360 bytes, sha256 3e03b8549630dd179f8e951ce1c1cccd0c0bd6fa62fdcb54dcedab0399501a1d
~/work/tmp/scummvm2026-p2build/pq2-k3.dat: SCIKOR version 2, 2202 entries, 29158 bytes, sha256 9bcab2d835ae6a39ec603d91ab344f25198b9a5eb9468f876bf46460ba68b14c
~/work/tmp/scummvm2026-p2build/qfg1e-k3.dat: SCIKOR version 2, 2247 entries, 29122 bytes, sha256 e80717c84b03fe40e0903ef5716e9fa1419b0fd7558a3780b4eb8168a727754e
~/work/tmp/scummvm2026-p2build/qfg2-k3.dat: SCIKOR version 2, 2674 entries, 34664 bytes, sha256 977975ef786a22dcfaa767feefc5a048f1a2eb6696a758ef1d54e5bee3b2b2ac
~/work/tmp/scummvm2026-p2build/lsl1a-k3.dat: AGIKOR version 2, 1217 entries, 16340 bytes, sha256 05c118218a2c13ec08b34e6fd7439c06e5246bfdfcd6307432dc3eb17e8c0d02
~/work/tmp/scummvm2026-p2build/kq1-v3.dat: SCIKOR version 3, 1265 entries, 21738 bytes, sha256 5362eed1096702c8868f99366948e057fb972763a743d3183e190b4dff3a3d05
~/work/tmp/scummvm2026-p2build/kq1-back.dat: SCIKOR version 2, 1265 entries, 16558 bytes, sha256 48cc43e4720ef98b4790366c6cecb3c2cf4e4f091a9ac3021520e0882645ed75
same
```

The first sha256 line is the `translations.tsv` column 5 of the KQ1 row; `harness-check` reads all nine masters. The five
`k3map.json` builds equal the `3e03b854`, `9bcab2d8`, `e80717c8`, `977975ef`, `05c11821` rows above. KQ1 as version 3 is 21,738 bytes
and `convert --format 2` of it equals the version 2 file (`same`).

## After the review (`P2-review.md`, fixes in `P2-fixes.md`)

- The Task 11 command that builds the version 3 tables of the harness masters needs `--vocab-md5` now (review m3):
  `... build $m $BLD/korean-vocab/$g.v3.dat --game $g --fingerprint 000...0 --vocab-md5 fedcba9876543210fedcba9876543210`.
  `--format 2` is unchanged: the KQ1 table is again 1265 entries, 16558 bytes, sha256
  `48cc43e4720ef98b4790366c6cecb3c2cf4e4f091a9ac3021520e0882645ed75`, and `harness-check` reports nine `ok`.
- `k6bake.py` refuses words longer than 16 code units (`KoreanStems::kMaxLen`); the harness-table test no longer skips them.
- `test/common/korean_particles.h` and `common/hangul.h` (ported from `jamo`) now differ from `jamo` by the `ng` rule (m4).
- The stem corpus `korean_stem_corpus.txt` is a regression guard, not an independent oracle: it was filtered by the Python
  mirror of the stemmer rules, so a form the rules never reached is not in it. The independent measure is `k16said` (P3/P4).
- Totals after the fixes: Linux and ASan `make test` 620 tests OK, `KoreanVocabTestSuite` 12 tests OK with `korean-vocab/`.
