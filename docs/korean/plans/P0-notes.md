# P0 notes: deviations and acceptance (2026-11-22)

Implemented from `P0-svf-library-and-font-adapter.md`; Tasks 1-11 are commits `1022e6ac642..3965bb04e93` on `ko-2026.4`.
Builds: `~/work/tmp/scummvm2026p0build` (Linux), `~/work/tmp/scummvm2026-p0asan` (ASan), `~/work/tmp/scummvm2026-p0mingw`.

## Deviations from the plan

1. Branch: committed on `ko-2026.4` directly (the caller named that branch), not on a new `ko/00-svf-lib`.
2. `Common::memset16` link issue (Task 2 Step 5, UNVERIFIED in the plan): did not occur. The real test runner links with
   the unchanged `TEST_LIBS` at 408, 413 ... 433 tests, Linux, ASan and mingw. `libcommon.a` was NOT appended, so
   PATCHES.md is accurate as written (no libcommon row).
3. `TS_TRACE` output (ASCII art of a failing golden line): the runner (`cxxtestgen --no-std --no-eh`) prints nothing for
   `TS_TRACE`. A deliberately broken golden row fails 3 tests (`test_line1x`, `test_streamed`, `test_syllableCache`) with
   `Assertion failed: same`; the art is not shown. Left as is (test helper unchanged, per the plan).
4. `sizeof(svf_face)` guard on this host: x86_64 branch of `svfface.h` compiles (96 bytes) and `test_abi` passes; the i386
   branch (76) was not built here.
5. TDD order: for Tasks 5, 7, 8 and 9 the failing run was observed only as a compile or link failure (missing header
   or object) or was folded into the implementation run (Task 8: `draw` already lived in the chain file of Task 7);
   Tasks 6 and 10 were run red (3 and 4 failing tests) before the implementation. Task 8's red check was replaced by
   breaking one golden row (item 3).
6. Header comments in `svfface.h`, `svfchain.h`, `svffont.h` are the prototype's (longer doxygen); code and signatures are
   identical to the plan's blocks (diffed). `test_measure` follows the plan (adds `getAscent/getDescent/setScale` checks that
   the prototype lacked).
7. Review checkpoints A, B, C: run as full test suite plus both `--check` scripts, plus the "submodule renamed, `make test`"
   check of B (424 tests OK without `dists/svf-fonts-open`); no code review subagent was run.
8. Task 13 (manual GUI check) skipped, nothing committed. Task 12: no wine on the host, `test/runner.exe` not run.
9. This notes file and the plan itself (untracked until now) are committed together in the last commit.

## Acceptance (Task 14)

- `tools/korean/sync-svf.sh --check`: `sync-svf: graphics/svf matches svf-fonts-open 3d40de8d940f50ed4a28dfc1851fa776a62f42e5`
- `python3 tools/korean/mkgolden.py --check`: rc 0
- Linux `make test` (debug, no optimisations, no engines): `Running cxxtest tests (433 tests)...OK!`
- ASan (`--enable-asan`) `make test`: `Running cxxtest tests (433 tests)...OK!`, no sanitizer report
- mingw (`x86_64-w64-mingw32`): `test/runner.exe` built (`PE32+ executable (console) x86-64`, 16 MB); the C objects
  use `x86_64-w64-mingw32-g++ -x c -std=c99`; run UNVERIFIED (no wine)
- PNG dump: `clut8.png` 240 x 40 8-bit colormap, `rgb565.png` 240 x 40 8-bit/color RGB; the RGB565 image shows yellow
  `가한글 abéz` on navy at 2x
- Commits: 11, each subject matches `[A-Z_]+:` (`devtools/check-commit-msg.py` rc 0) and carries both trailers
- Upstream files touched: `configure`, `graphics/module.mk`, `test/module.mk`, `LICENSES/COPYING.MIT`, `LICENSES/COPYING.OFL`
  (all in `docs/korean/PATCHES.md`)
