# P6a notes: SCUMM S1, Korean text as UTF-8 inside the engine

Running log of the implementation of `P6-scumm.md` phase P6a (tasks a0-a14). Branch `ko/p6-scumm`, worktree
`~/work/scummvm-2026.4-korean-p6`, base `4211b5ac538` (ko-2026.4). Builds: `~/work/tmp/p6a-build` (SCUMM only),
`p6a-sci`, `p6a-none`, `p6a-asan`, `p6a-mingw`. Scratch: `~/work/tmp/p6a`, runs `~/work/tmp/p6/runs/a*`.
Code source: the prototype `~/work/tmp/p6/tree`; its new files equal appendix B of the plan apart from the GPL header
(and one `hires_text.h` line in `korean.cpp`, S2, left out). Tasks are committed one by one; the sha of a task is
written into the log by the next commit.

## Log

### a0 (no commit)
- `git diff v2026.4.0.. -- engines/scumm` is empty at the base. SCUMM-only build, `make test`: **N0 = 644** (the plan's 637
  was measured at another base).
- Interface greps: all present (string.cpp 2340, 1265, 1541, 1355, 1705 and 1787, 1914/1979, charset.cpp 677 ...).
- Red: the MI2 pack as shipped on the base build: `charset.cpp:1367: ... Assertion 'charOffs < 0x14000' failed`
  (`~/work/tmp/p6/runs/a0-mi2/run.log`, count 1).

### a1 e6a7b929132: KS X 1001 table
- Red: `engines/scumm/korean_text.h: No such file or directory`. Green: 645 tests (N0 + 1).

### a2 0c43e6894ad: UTF-8, escapes, CP949 -> UTF-8
- Red: `'cp949ToUtf8' is not a member of 'Scumm'`. Green: 647 (N0 + 3). In this task the two tests called
  `cp949ToUtf8()` through a local helper, as the plan says.

### a3 48478d3d97a: the boundary
- Red: `'toEngineText' is not a member of 'Scumm'`. Green: 650 (N0 + 6). Tests rewritten to the appendix B.9 form.

### a4: bundle names and the CP949 body
- Red: `trs_bundle.h: No such file or directory`. Green: 653 (N0 + 9).
