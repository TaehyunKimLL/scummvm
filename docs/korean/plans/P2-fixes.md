# P2 review fixes (2026-11-22)

Findings of `P2-review.md` against the commits that fixed them (all on `ko-2026.4`, after `8b24c4944e7`). Every fix was run
red first (a failing test) except where stated.

| Finding | Commit | Evidence |
|---|---|---|
| M1 BOM hides the header | `24cd4517afc` | `test_byteOrderMark` failed (`parse` false), now: BOM + header ok, BOM + header for another game is a mismatch, BOM without header is no header |
| m6 test scratch under `/tmp` | `d931b09c86a` | `test_k6bake.py` uses `$TMPDIR` or `k6bake-test/` in the working directory, removed when empty (behaviour change, no red test) |
| m2 word longer than `kMaxLen` | `10002e4eeb6` | `test_word_longer_than_the_reader_looks_up_is_refused` failed ("BakeError not raised"); the `lookedUp` mask is gone from `korean_vocab.h` (it cannot go red: all nine masters are at most 8 units) |
| m3 `--vocab-md5` for format 3 | `87d7df98498` | `test_format3_requires_the_vocab_md5` failed (rc 0 != 2); KQ1 `--format 2` sha256 `48cc43e4720e...` re-run, `harness-check` nine `ok` |
| m5 silent skip | `5165de3fe92` | `TS_WARN` (TS_TRACE prints nothing in this runner): "korean-vocab/ is not in the working directory: harness table check skipped" shown without the folder |
| m7 tracebacks | `5b3ff34ad25` | `test_bad_input_is_a_message_not_a_traceback` (10 cases) failed with `ValueError`; now `k6bake: ...`, rc 2 |
| m4 Latin `-ng` | `3f42395dbab` | `test_latin_ng_ends_in_a_consonant` failed (`king가`); `dog`, lone `g` stay vowel-final |
| n9 `#!format=1`, `format=0001` | `e9a7b91b5cf` | two new bad-header cases failed, now refused |
| n8 v2 warning, n10 stream position, n11 composer cap | `11c281e3f87` | `test_stream_position_is_ignored` failed; v2 load with an empty `expected` no longer warns; the cap is documented |
| n12 corpus is a guard, not an oracle | this commit | sentence in `P2-notes.md` |

Not fixed: nothing of the list. Deviation: m3 and m2 were not red-testable against real data (see above).

Verification: Linux `make test` 620 tests OK; ASan 620 OK, no sanitizer line (with `korean-vocab/`); `KoreanVocabTestSuite` 12
tests OK with the 27 harness files; mingw `test/runner.exe` built (not run); `make scummvm` links; `sync-svf.sh --check` and
`mkgolden.py --check` rc 0; `test_k6bake.py` 11 tests OK.
