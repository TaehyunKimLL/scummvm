# P0 review fixes (2026-11-22)

Findings of `P0-review.md` and the commits that fix them. Test totals after all fixes: 437 tests OK on the normal build
and on the ASan build (4 new tests: short reads, addFace refusal, out-of-range draw, refresh; two tests
tightened: edges, metrics); the mingw `test/runner.exe` rebuilds (not run: no wine).

| Finding | Commit | Evidence |
|---|---|---|
| M-1 create_project | 300ca9a9cc5 | `create_project --cmake/--msvc/--xcode` in `~/work/tmp/p0fix-cp`: the six C files are listed; CMakeLists has `add_definitions(-DSVF_JMAP=1 -DSVF_JSET_BIG=1 -DSVF_JMAP_RECOG=0)`, MSVC props and the Xcode project carry `SVF_JMAP_RECOG=0`. The review's suggested `setup.defines` alone does not reach the CMake generator (it writes its own list), so `cmake.cpp` is patched too (PATCHES.md) |
| M-9 no-dependency-tracking branch | 1c05dc49f60 | reproduced (`CXX_UPDATE_DEP_FLAG` deleted from a scratch `config.mk`: `Fatal error: can't create graphics/svf/svf_face.o`), rule added, same build compiles |
| M-4 C++-only flags | 7d29fef2480 | `make -n` with `CXXFLAGS="-fno-threadsafe-statics -fuse-cxa-atexit -Wall"`: none reaches the C object (the flags are not set by this host's configure, so shown by override). Also the module.mk ordering nit |
| M-8 ABI guard | 9ea97345fd0 | runtime comparison in every load (no `assert`), static size asserts removed; 433 tests OK. The mismatch path itself is not exercised by a test (it needs a C unit built with other switches) |
| M-3 short reads | bd2eb90bef9 | `test_streamShortReads` failed before the fix (open error), passes after |
| M-2 font height | 3677efd4190 | `test_metrics` 16 (was 17), `test_scaledMetrics` 24 |
| M-6 edge test | d62205189f5 | sentinel band; with the clip widened by 30 the test fails with 1328 stray pixels |
| M-7 stale cache, nits (draw range, ownership) | 51758a368e0 | `SvfChain::refresh()`, `test_refreshAfterReopen` (output identical after reopen + refresh; staleness itself is not reproduced: the fixture has no second face size), `test_addFaceRefusal`, `test_drawOutOfRange` |
| nits: scripts | 0e032a7dd55 | empty/non-git submodule gives a message from both scripts; `shasum -a 256` fallback (not run: `sha256sum` exists here) |
| M-5 commit messages | history rewrite | all commits after v2026.4.0 reworded: `devtools/check-commit-msg.py` rc 0 on each, single trailer `Assisted-by: Claude Code`; `git diff ko-2026.4-pre-rewrite <rewritten HEAD>` was empty. Backup branch `ko-2026.4-pre-rewrite` kept |

Not fixed: nits on `test_dumpWhenAsked` being a developer aid, `TS_TRACE` being silent, the OFL wording, and the suggestion to
repeat the `Common::String` note in DESIGN 5.2 (DESIGN.md is not edited here).
