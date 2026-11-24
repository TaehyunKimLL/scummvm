# P5 review fixes

Findings of the checkpoint reviews (`P5-notes.md`: critical read plus randomized or exhaustive tests against independent
models, ASan+UBSan) -> commit -> evidence. Builds: `$BLD` = `~/work/tmp/p5-build`.

| Finding | Commit | Evidence |
|---|---|---|
| F1 the save-name budget was the whole 30-byte field, but `saveGame()` copies the description with `strlcpy(..., SAVEDGAME_DESCRIPTION_LEN)`, which keeps 29 bytes: a name of 10 syllables (30 bytes) was accepted and shown in the verification box and then saved as 9 (terminator off-by-one) | see `git log` (`AGI: Hold a Korean save name to the bytes a save keeps`) | red: `test_saveNameBudgetSurvivesTheDescriptionCopy` failed (names of 29/30 bytes cut by the strlcpy) with the budget at 30, and the run `runs/d3b` (typed `dkssudgktpdydkssudgktpdy`): box `안녕하세요안녕하세요`, file `d[4:35]` = `안녕하세요안녕하세` (27 bytes). Green: the macro `SAVEDGAME_DESCRIPTION_BYTES` = LEN - 1 is the budget and the clip in `saveGame()`; run `runs/d3c`: the 10th syllable is refused, the box shows `안녕하세요안녕하셍` and the file holds the same 27 bytes; the plan's 28-byte example (`안녕하세요 안녕하셍`) is still accepted |
