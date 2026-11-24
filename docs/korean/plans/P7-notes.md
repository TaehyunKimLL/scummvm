# P7 (DOS port) implementation notes

Running log of the P7 run (branch `ko/p7-dos` in worktree `scummvm-2026.4-korean-p7`, base `ba0eecee2c4`; the plan says
`ko/08-dos` and `e9611c272ee`: the branch name and base are the owner's later choice, the plan's file:line table was
re-checked where used). Scratch: `~/work/tmp/p7`, builds `~/work/tmp/p7-build`. Format per task: id, commit, tests,
deviations, evidence.

## Decisions (delegated by the owner)

- D-1 accepted: DOS auto gives RGB565, then RGB555, then CLUT8; XRGB8888 only with `render_target=rgb888`.
- D-2 accepted: stage truncated 8.3 names, refuse collisions, DJGPP long file names off.
- D-3 accepted: the GUI-less build is a measured gate (Task e1).
- D-4 accepted: `-march=pentium` default, 486 opt-in (`DOS_MARCH=i486`).

## Environment deviations (run rules)

- SDL3 builds go to scratch prefixes `~/work/tmp/p7/prefix/sdl3-dos` and `.../sdl3-dos-min-kbd`; the installed `~/opt/sdl3-dos*`
  were NOT touched or renamed (the plan's Task a2 renames them; the run rules forbid it). Builds use
  `SDL3_DOS=~/work/tmp/p7/prefix/sdl3-dos-min-kbd`.
- The smoke program was copied to `~/work/tmp/p7/smoke-p7` (the plan builds inside `~/opt/dos-dev/smoke`).
- The svf-fonts-open submodule dir was empty: filled with a local clone at the gitlink `3d40de8d940f` under `dists/svf-fonts-open`.

## P7a: SDL3 (repo `~/opt/src/SDL3`, branch `dos-p7-1362030`, local, not pushed)

- a1: facts of 0.1 re-checked: release-3.4.20 has 0 `/dos/` files, `1362030` has 25, 16 commits over `1ce4c5bc29`, the keycode pair
  cancels (empty diff). `git fetch` moved upstream `main` to `38dc1a5f6`; the series stays pinned on `1362030`. The 14 cherry-picks
  applied without a conflict: `1362030..HEAD` = 14 commits, `6 files changed, 539 insertions(+), 177 deletions(-)` (as the plan).
  Step 3: the only difference to `dos-kbd-hangul` in `src/audio/dos src/video/dos src/cpuinfo src/core/dos` is upstream's
  `src/cpuinfo/SDL_cpuinfo.c` (non-DOS FreeBSD/ARM elf_aux_info cleanup); nothing of ours lost.
- a2: both prefixes built into scratch (`cmake` full + min, logs `~/work/tmp/p7/a2-{full,min}.log`). All eight marks present in both
  libs (16 of 16 lines = 1). Version string `SDL-3.5.0-cc504b3b8` (= HEAD of `dos-p7-1362030`).
- a3: smoke gate, four runs (log `~/work/tmp/p7/a3-smoke.log`). All four: `video=vesa audio=soundblaster`, `640x400 INDEX8 mode found`,
  `window ok surface 640x400 SDL_PIXELFORMAT_INDEX8`, `audio ok`, `timer 10ms x 1000ms -> 99 callbacks`, `done`. Mode lists:
  DOSBox-X 640x400 XRGB8888/RGB565/XRGB1555/INDEX8 (640x480 the same four); Staging 640x400 XRGB8888/INDEX8, 640x480
  XRGB8888/BGR24/RGB565/XRGB1555/INDEX8. Identical to the plan's 0.1: gate passed.
- a4: `git format-patch` into `~/work/tmp/p7/sdl3-series`: 14 files (`0001`..`0014-dos-Deliver-the-Korean-Han-Yeong-and-Hanja-keys-as-L.patch`).

## P7b: backend series

- b0: host build of the base (`configure --disable-all-engines --enable-engine=sci,scumm --enable-debug --disable-optimizations`,
  `make -j8 test`): **N0 = 770 tests**, OK.
- b1: `5776b32518a CONFIGURE: Add the DJGPP (MS-DOS) host and the dos backend`. jamo's diff applied cleanly; the debug-socket and GUI
  defaults/help/options/blocks removed as the plan says. Linux configure after the change: `config.mk` (minus SAVED lines) and
  `config.h` identical to the base configure; DOS configure: `Backend... dos, assembly routines, 16bit color, high resolution, savegame
  timestamp`, `BACKEND := dos`, defines incl. `-DDOS_DETECTION_FILTER` (inert until the SCI module.mk hunks, not carried, plan b3).
