# P1 notes: deviations and acceptance (2026-11-22)

Implemented from `P1-hires-core.md`; commits `3f0effc843f` (the plan) to the commit that adds this file, on `ko-2026.4`.
Builds: `~/work/tmp/scummvm2026-p1build` (Linux), `~/work/tmp/scummvm2026-p1asan` (ASan), `~/work/tmp/scummvm2026-p1mingw`.
Every code block was taken from the plan text (`blk.py`-style extraction of the fenced blocks), and the resulting
files were compared with the prototype in `~/work/tmp/scummvm2026-p1-proto/` (identical apart from the GPL header).

## Deviations from the plan

1. Branch: committed on `ko-2026.4` directly (plan section 1 item 11).
2. GPL header: every new file under `graphics/hires_text/` and `test/graphics/` that is not ported carries the header of
   `graphics/font.cpp`; the plan's code blocks and the prototype omit it. `test/common/language_folder_code.h` has none (as
   the plan says). Ported files are byte-identical to `jamo` (two test edits named by the plan).
3. TDD: every task was run red (compile or link failure, or an assertion) before its implementation, except Task 15
   (the harness has no production code: it was green at once, 534 tests, as the plan says) and Task 11 (no test; Task 12
   loads both files). Task 7 first went in with a wrong test block (the plan's line pointer was off by one in my
   extraction); caught before the commit, the commit was redone with the right test and a real red run.
4. Review fixes (own commits, found in the checkpoints, not in the plan):
   - after Task 6: `TextPlane::rowHasText/getIndex/getCoverage` indexed the band table unchecked for a row or column
     outside the plane (debug build assertion in `Common::Array`); now "no text" there. The test cases were folded into
     `test_createAndPlot` so the test count stays the plan's.
   - after Task 13: `HiResDiskFiles::isSafeDataPath` accepted `hires_text\..\..\X.SVF` (a backslash is a separator on
     Windows) and `C:` inside a component; both refused. Cases added to `test_diskFiles`. Blank lines lost by the Task 8
     insertion into `font_map.cpp` restored.
   - after Task 15: `HiResFontSet::build` called twice appended a second set of chains; it now replaces them (test in
     `test_defaultMapDraws`), and the header says the face cache must outlive the set.
   All three fixes keep the plan's 534 tests.
5. Review checkpoints A-D: run as the full `make test`, both `--check` scripts, the `cmp` of the ported files against
   `jamo` (A), and an own read of the diff since the previous checkpoint (lifetime, clipping, overflow, byte order, band
   boundaries); no review subagent. Not found wrong on reading: `compositeRect`/`presentRect` clip to plane and surface
   (`Rect::clip` asserts valid rects, as everywhere in ScummVM), scratch buffers are 256 bytes for chunks of at most 256,
   pixels are read and written native-endian with `READ_UINT16/32`, a failed face is cached as `nullptr` and warned once,
   a chain without faces routes to the original font or nothing.
6. Task 16: the mingw runner was built, not run (no wine).

## Acceptance (Tasks 16-18)

- Linux `make test` (debug, no optimisations, no engines): `Running cxxtest tests (534 tests)...OK!`
- ASan (`--enable-asan`, `-fsanitize=address`): `Running cxxtest tests (534 tests)...OK!`, no sanitizer report
- mingw (`x86_64-w64-mingw32`): `test/runner.exe` built (`PE32+ executable (console) x86-64`, 19 MB); run UNVERIFIED
- `tools/korean/sync-svf.sh --check`: `sync-svf: graphics/svf matches svf-fonts-open 3d40de8d940f50ed4a28dfc1851fa776a62f42e5`, rc 0
- `python3 -I tools/korean/mkgolden.py --check`: rc 0
- Ported files identical to `jamo` (`cmp` of the 8 production files prints nothing; tests differ only by the two named edits)
- `git diff --stat dd5f64b8b34.. -- engines backends gui base`: empty
- Upstream files touched: `LICENSES/COPYING.OFL`, `common/language.cpp`, `common/language.h`, `graphics/module.mk`,
  `test/module.mk` (all in `docs/korean/PATCHES.md`)
- Pack-map aid: `./test/runner HiResTextShippedMapsTestSuite test_packMapsWhenGiven` with `hires-maps/korean-default.map`: OK
- PNG dump (`test_dumpWhenAsked`): `clut8-stamp.png` 320 x 100 8-bit colormap, `rgb565-blend.png` and
  `rgb888-threshold.png` 320 x 100 8-bit/color RGB. Viewed: yellow `가한글 abéz` on a navy background at 16 px in the top
  third, the same line at 24 px (3/2) over a white bar below; Hangul syllables are complete and legible, the `é` keeps
  its accent mark; the RGB565 blended image shows softened (anti-aliased) edges on the 24 px line, the CLUT8 and the
  RGB888 threshold images are hard-edged and nearly identical.
- Commit messages: all pass `devtools/check-commit-msg.py` and end with `Assisted-by: Claude Code`
  (the plan's loop prints NOTRAILER for every commit because `%B` ends in an empty line, so `tail -n 1` sees that; with
  empty lines skipped, `sed '/^$/d' | tail -n 1`, it prints nothing; exactly one `Assisted-by`, no `Co-Authored-By`).
