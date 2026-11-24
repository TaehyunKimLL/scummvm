# P5 notes: AGI Korean (LSL1 first)

Running log of the implementation of `P5-agi.md`. Branch `ko/p5-agi`, worktree `~/work/scummvm-2026.4-korean-p5`, base
`ba0eecee2c4`. Builds: `~/work/tmp/p5-build` (AGI only), `p5-all` (agi+sci+scumm), `p5-asan`, `p5-none`, `p5-mingw`.
Scratch: `~/work/tmp/p5w` (run dirs `runs/`, scripts `bin/`, which copy the prototype's with the worktree's extrapath).
Code source: the prototype `~/work/tmp/p5/tree`. Checked: every unit and every hook diff of the plan's appendices A/B/C
is byte-identical to the prototype (script compare), so tasks copy the prototype's files and apply the plan's hook
hunks per task (`~/work/tmp/p5w/bin/hk.py`, `git apply --recount`, as the plan's hunk headers have hand-written counts).
Tasks are committed one by one; the sha of a task is written into the log by the next commit.

## Log

### a0 (no commit)
- Worktree exists (`git diff --stat v2026.4.0 -- engines/agi` empty). AGI-only debug build: **N0 = 648**.
- Submodule dir `dists/svf-fonts-open` was empty: filled with a local clone at the gitlink commit 3d40de8d940f
  (nested repo, parent status stays clean); `sync-svf.sh --check` OK, `mkgolden.py --check` rc 0.
- Data: `~/work/tmp/p5/data/lsl1{en,ko,ko3}` reused from the prototype; md5 LOGDIR/WORDS.TOK/VOL.1 as in 0.2 (the
  `lsl1ko3` copy already carries the header line, AGIKOR v3 dat and `HIRESBIT.MAP`).
- Red (base build, `language=ko`, lsl1ko3): `Error running game: Game data not found`
  (`agi-p5-red-base-language-ko-game-data-not-found-r2.png`, seen: the box on a black screen).

### a1: cell helpers
- Red: `make test` -> `No rule to make target 'engines/agi/textcells.o'` (module.mk named the object before the file
  existed; the plan's `textcells.h: No such file` would come one step later). Green: **653** (N0 + 5).
- D14.1 mutation (the `if (utf8) wordEndChar = 0;` removed): `Failed 2 of 653`: `fixed > 0u` and `bad == 0u`, found
  9150 != 0 invalid lines; restored.

### a1 996f87e7484 (sha of a1)

### a2: text mode
- Red: `No rule to make target 'engines/agi/textmode.o'`. Green: **658** (N0 + 10), 0 warnings.
- a2 sha 967203a3412.

### a3: translation table
- Red: `No rule to make target 'engines/agi/translation.o'`. Green: **668** (N0 + 20), 0 warnings.
