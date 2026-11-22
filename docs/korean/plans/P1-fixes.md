# P1 review fixes (2026-11-22)

Fixes for `P1-review.md`. Each fix is its own commit; every code fix has a test that was run red first (the mutant
check for m-5 is named below). Tests: 534 -> 543 (normal and ASan builds), mingw `test/runner.exe` builds (not run).

| Finding | Commit | Evidence |
|---|---|---|
| M-1 qualified/duplicate range and advance rules | `d8b7cdb97d5` | `test_qualifiedRangesOverride`, `test_qualifierOrder`, `test_duplicateSpanInOneSection` (id_plan suite) failed with `b` where `c` was expected; stale comment in `unicode_ranges.cpp` corrected |
| m-1 map comment (face not found in a source build) | `61dcb5e3c65` | comment only, no test can fail on it; the shipped-map tests still load the map; no install lines added (first P3 task). The map's redundant `[render]` defaults dropped (nit) |
| m-2 jamo sequence and tone marks | `a3a8ca69a9b` | `test_hangul_jamo_sequence_and_tone_marks_do_not_break` failed (break between L and V, 3 lines at width 1) |
| m-3 font id spellings, `[map]` keys, surrogate `missing=` | `7c8017ff409` | `test_fontIdSpellings`, `test_unknownMapKeyAndSurrogateMissing` failed first |
| m-5 CRLF parser case | `7c8017ff409` | `test_crlfLineEnds` (it passed at once: the parser was already right; the test pins it) |
| m-5 `test_scaledCoverageBlends` | `fd5a5adb0f2` | with `convertSpan` forced to threshold the test fails (`blended != 0xFFE0`), as do the compose tests |
| m-5 `readHiResOptions` via ConfMan | `d501a3361f5` | `test_readThroughConfMan` (practical: the transient domain works under the null OSystem); failed on the untrimmed scale |
| nit: untrimmed `hires_text_scale` | `d501a3361f5` | same test |
| nit: `readBytes`/`writeBytes` on an empty plane | `8785ea18a44` | `test_bytesOfAnEmptyBandedPlane` died with SIGFPE before the fix |
| nit: `C:` absolute test mismatch | `dab97bd3e26` | resolveFace test of a bare `C:` failed (`game/C:`) |

## Not fixed

- m-4 (RGB555/1555/4444 backends get CLUT8): not required; a scope choice.
- nit `find()` order on case clashes: a test would need two files differing in case on disk; not trivial, skipped.
- nits skipped: `searchman:` map dir for relative faces, nearest-ratio `size=`, alpha note in `text_compose.h`, int16
  overflow comment in `toHires`, `NEODGM.TXT` wording.

## Note on the ported files

`text_layout.cpp` (m-2) and `banded_plane.cpp` (empty-plane guard) are no longer byte-identical to the `jamo` branch;
`P1-notes.md`'s acceptance line "identical to jamo" holds for the P1 commit `d6b3b963ae8`, not for the fixes.
