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
- b2: `0be71be8539 AUDIO: Drive the hardware OPL and MPU-401 of a DOS PC`. jamo's hunks applied cleanly. `LoggingOPL` (`opl_log=`) dropped as
  the plan's default (class, `friend` line, the five includes it needed; `createUnlogged` folded back into `Config::create`; the
  `Config::kOpl2` qualifications it forced reverted), so `audio/fmopl.h` is unchanged against the base. Host `make test`: 770 OK, 0 warnings.
- b3: `c7a87a12cb6 DOS: Build without video codecs and detection archives`. jamo's `advancedDetector.cpp` hunk also had an `#ifndef DISABLE_GUI` around
  the pirate-copy dialog: removed (GUI-less series is Task e2, conditional). Host 770 OK.
- b4: `85dab16ddf0 DOS: Add the MS-DOS backend`. Couplings cut as the plan says. `dos-vorbis-selftest.*` and `patches/` also left for Task f2 (module.mk
  and dos.cpp already guard them with `USE_VORBIS`/`USE_TREMOR`). The UART interrupt count in the page-fault selftest is `uartIrqCount()`
  (0 without `USE_DEBUG_SOCKET`). `hires_text_whole_kb` default is registered in Task c4, not here. `sdl3-build.txt` rewritten (base 1362030,
  `git am` of `sdl3/*.patch`, eight marks, the two cmake lines). The 14 patch files are in `backends/platform/dos/sdl3/`. DOS build by hand:
  `scummvm.exe` 7,871,786 B unstripped (5,408,256 B stripped), 2 compiler warnings.
- b5: `aba8a7a6dc3 BACKENDS: Write files in place and size them by seeking on DJGPP`. Red run (b4 binary, stripped, DOSBox-X KQ1 title): two
  `WARNING: Unable to write configuration file: C:\SCUMMVM\SCUMMVM.INI!`; green run: none, and SCUMMVM.INI gained `versioninfo=2026.4.0`,
  `lastselectedgame=` and `savepath=`.
- b6: `751f661cf2d DOS: Build out of tree and check SDL 3.5.0`. `build-dos.sh sci $B/sci`: rc 0, `irqcheck: uart: not in this build`, `dos [..] 68 bytes, 0 indirect`,
  `kbd 36 bytes 0 indirect`, `sb 356 bytes 0 indirect`, `timer 1316 bytes, 2 indirect exit(s)` (as the prototype); `build-dos.sh sci $SRC/build-dos` ->
  "build outside the source tree", rc 1. Staging (`dist/dos`) left out of the script until Task f5; `check-tremor-cache.sh` call is conditional until f2.
  The `SDL3_DOS_MIN` environment variable points at the scratch prefix (`~/work/tmp/p7/prefix/sdl3-dos-min-kbd`).
- b7: `8d8262d73ae TEST: Test the DOS backend's pure parts on the host`. Red: `make test` did not compile (`autoTrueColorName`). Green: **986 tests = N0 770 + k 216**
  OK (`DosModesTestSuite` 15 cases). Mutations: (1) `xrgb1555` before `rgb565` in `supportedFormats` -> `test_auto_offers_16_then_15_bit_then_clut8` fails (2 failed
  tests); (2) always offering XRGB8888 -> `test_engine_auto_picks_565_on_dos` (and the auto/clut8 list tests) fail. Both reverted. `test_render_target_without_rgb565_hardware`
  and the old caps tests were rewritten for the new contract (rgb555 added to the cap test).
- b8: gate. Run folder `~/work/tmp/p7/run-b8` (KQ1 P3 pack with upper-case `KO\`, stripped SCI.EXE, the engine's own `korean-default.map` and `NEODG16A.SVF`
  under `DATA\HIRES_TE`). The prototype's `x.conf` runs the launcher unless a target is given: the confs use `RUN.BAT kq1-ko` etc. Results (screenshots seen):
  `dos-p7-b8-title-clut8.png` (title: Korean buttons 프롤로그 / 게임시작 / 크레딧 / 이어서 계속하기 on the KQ1 banner), `dos-p7-b8-rgb565-message.png` (status `득점: 0 of 158`,
  `킹스 퀘스트 I`, message box 에드워드 왕의 성 바깥에 서 있다...: `DOS: mode 640x400 RGB565@2`, `screen format RGB565@2 for render target rgb565`; PIL compare against Linux
  `aa-kq1-rgb565-message.png`: status 0, left box 0, right box 0 differing pixels), `dos-p7-b8-clut8-message.png` (`640x400 CLUT8`; status 0, boxes max 3 per channel against Linux). Memory lines:
  rgb565 `engine dpmi_free=8976`, `first-frame 4724 KB`; clut8 first frame 5740 KB. Log also says (as before) that the pack's `HIRESVF.MAP`/`HIRESBIT.MAP` are map version 2 (harness dependency H-2).
- b9: PATCHES.md rows for every upstream file of section 3 carried so far (`ko/08-dos`).
