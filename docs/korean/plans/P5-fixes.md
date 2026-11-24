# P5 review fixes

Findings of the checkpoint reviews (`P5-notes.md`: critical read plus randomized or exhaustive tests against independent
models, ASan+UBSan) -> commit -> evidence. Builds: `$BLD` = `~/work/tmp/p5-build`.

| Finding | Commit | Evidence |
|---|---|---|
| F1 the save-name budget was the whole 30-byte field, but `saveGame()` copies the description with `strlcpy(..., SAVEDGAME_DESCRIPTION_LEN)`, which keeps 29 bytes: a name of 10 syllables (30 bytes) was accepted and shown in the verification box and then saved as 9 (terminator off-by-one) | see `git log` (`AGI: Hold a Korean save name to the bytes a save keeps`) | red: `test_saveNameBudgetSurvivesTheDescriptionCopy` failed (names of 29/30 bytes cut by the strlcpy) with the budget at 30, and the run `runs/d3b` (typed `dkssudgktpdydkssudgktpdy`): box `안녕하세요안녕하세요`, file `d[4:35]` = `안녕하세요안녕하세` (27 bytes). Green: the macro `SAVEDGAME_DESCRIPTION_BYTES` = LEN - 1 is the budget and the clip in `saveGame()`; run `runs/d3c`: the 10th syllable is refused, the box shows `안녕하세요안녕하셍` and the file holds the same 27 bytes; the plan's 28-byte example (`안녕하세요 안녕하셍`) is still accepted |
| F2 `cmdSetSimple`'s automatic save description went through `clipBytes` + `strlcpy`, which leaves the bytes of an earlier, longer description after the NUL: an English save file would differ from upstream's (`strncpy` zero-fills the 31 bytes the file holds) | `AGI: Keep set.simple's description buffer zero after the text` | red: `test_automaticDescriptionIsUpstreamsOnTheBytePath` failed with stale bytes after the text (`mine[i] == 0`, found 0x97 ...) with `copyDescription` in the hook's `strlcpy` form; green with a zero fill + copy: equals `strncpy(dst, src, 31); dst[30] = 0` on 20000 random descriptions with stale buffers, UTF-8 cut on a character |

## Review round 1 (reports `reviews/P5-opus.md`, `reviews/P5-agy.md`)

| Finding | Verdict | Commit | Evidence |
|---|---|---|---|
| agy C1: `testSaid` unpacked all `nwords` (up to 255) from `p`, the 16-byte copy `testIfCode` hands to the opcode, on English targets too | **valid** (read: `spec[i] = READ_LE_UINT16(cc + 2 * i)` for i < nwords over a 16-byte `p`; upstream read lazily, at most `min(nwords, typed words)` words) | `AGI: Read said() words from the logic, never past it` | red: `test_saidSpecIsReadWithinTheBytecode` did not compile (no bounded reader); no end-to-end ASan repro (no synthetic logic builder; the overread is by code reading). Green: `readSaidSpec` reads from the logic bytecode (`testIfCode` passes `code + ip` and `size - ip`); 100000 random specs, nwords up to 255, a third truncated, in exact-size heap blocks: bounded, and `saidMatches` equals upstream's lazy loop over the same bytes on every complete spec (0 differences; typed lines 0..20 words). `condSaid` (debugger only) clamps at the 16 bytes it has |
