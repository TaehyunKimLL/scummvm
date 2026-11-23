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
- a4 sha 467b5dc6acf.

### a5: `_koreanUtf8`, the bundle, the boundary in `translateText()`
- Hooks as appendix A (hunks of loadKorFont, loadLanguageBundle, translateText x4); `korean.cpp` carries
  `transcodeLanguageBundle` and `copyTranslatedText`. **Appendix A omits the `scumm.h` declarations of `_koreanUtf8`,
  `hasTranslationBundle()`, `copyTranslatedText()`, `nextCJKChar()` ... (the prototype diff in the plan has only the
  `transcodeLanguageBundle`/`convertLoadedText` hunk); added from the prototype's scumm.h.** Also the plan's hunk headers
  have hand-written line counts (`git apply` refuses them; applied with `--recount`).
- Build: 653 tests (N0 + 9), no warning.
- [data] without extrapath, run folders `~/work/tmp/p6/runs/a5-*`: MM `50116 -> 58803` (as the plan). **Differences from the
  plan's numbers**: Zak `87138 -> 101837` (plan 102032), Indy3 `126521 -> 148511` (148571), Indy4 `813252 -> 821296`
  (821300; 7 unreadable pairs as planned), **MI2cp `738122 -> 747583` (plan 841833)**. The MI2 pack: no conversion line.
  Drawing is not there yet: Indy4 and MI2cp stop at `charOffs < 0x14000` as expected at this task.
- **Finding (red for the next commit):** the MI2 gap is real, not noise. The code of appendix B.5 splits the body at every NUL,
  but a translation can contain a NUL as an escape argument (`FF 0A 00 xx`): the part after that NUL is not converted and
  stays CP949, and the later boundary judges the whole string as not UTF-8 and reads its UTF-8 head as CP949 (garbage). A
  Python model of the engine rule (`~/work/tmp/p6a/emu.py`) gives 103708 pairs when a translation runs to its
  escape-aware end, which is the plan's `+103711`; the NUL split gives 20027. Fixed in the next commit (translations run to
  their escape-aware end, clipped at the next entry offset, which keeps MM's `sound FF 04 C8 00` working).
- a5 sha 238d202631d.

### a5-fix: a translation with a NUL escape argument (deviation from appendix B.5)
- Red: new test `test_translation_runs_past_a_nul_argument` failed (`memcmp ... found (-56 != 0)`), 655 tests, 1 failed.
- Fix: a translation string runs to its escape-aware end (`scummTextEnd`), clipped at the next entry offset of either
  list (MM's `sound FF 04 C8 00` still ends at its NUL: the terminator before the next entry); other strings stay
  NUL-split. Extra test `test_translation_stops_at_the_next_entry` and `test_unterminated_translation_keeps_an_empty_next_entry`.
  Green: 656 tests (N0 + 12).
- Evidence: MI2cp now `738122 -> 841833` and Zak `87138 -> 102032`, exactly the plan's numbers.
