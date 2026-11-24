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

## P7c: graphics and faces

- c1 (no code): format matrix, run folder `~/work/tmp/p7/run-b8`, KQ1 AA pair map, `qhk`, logs `~/work/tmp/p7/shots/c1-*.log.txt`:
  | Emulator | render_target | `DOS: mode` | `Hi-res text: screen format` | first-frame free |
  |---|---|---|---|---:|
  | X | auto | `640x400 RGB565@2` | `RGB565@2 for render target rgb565` | 4724 KB |
  | X | rgb555 | `640x400 RGB555@2` | `RGB555@2 for render target rgb555` | 4724 KB |
  | X | rgb888 | `640x400 RGB888@4` (the backend's format string is `RGB888@4`, not `XRGB8888@4`) | `RGB888@4 for render target rgb888` | 3224 KB |
  | X | clut8 | `640x400 CLUT8` (b8) | `CLUT8 for render target clut8` | 5740 KB |
  | X | auto + `dos_force_fallback=true` | `640x480 RGB565@2 (line repeat from 640x400)` | `RGB565@2` | 4624 KB |
  | Staging | auto and rgb565 | `640x480 RGB565@2 (line repeat from 640x400)` | `RGB565@2` | 4624 KB |
  Frames viewed: `dos-p7-c1-x-rgb888-message.png`, `dos-p7-c1-staging-linerepeat-message.png` (window scaled to 758x569 by Staging), `dos-p7-c1-x-forced-linerepeat-message.png`,
  `dos-p7-c1-x-rgb555-message.png`: the same Korean message box in each. rgb888 and the forced fallback were not run in the prototype.

## Checkpoint B (after b9)

- `make test` (`$B/host`): 986 tests OK (N0 770 + k 216). ASan (`$B/asan`, `--enable-asan`, `grep -c fsanitize config.mk` = 3): 986 tests OK, 0 sanitizer lines.
- Non-DOS diff against the base (`git diff ba0eecee2c4 HEAD --stat -- ':!backends/*/dos' ':!backends/platform/dos' ':!test/backends' ':!docs'`): the files of section 3 only
  (31 files incl. the new `audio/dosopl.*`, `backends/midi/dos_mpu401.cpp`), each file in PATCHES.md (checked by script).
- Inertness: Linux `configure` leaves `config.mk` (minus SAVED lines) and `config.h` byte-identical to the base's; every non-DOS hunk is under `DOS_DJGPP`, `__DJGPP__`,
  `BACKEND=dos`, or a `DISABLE_*`/`AUDIO_RATE_STEREO_INT16_ONLY` define set only by the msdosdjgpp configure block; the only unconditional code is
  `Audio::Mixer::getOutputLatencyMillis()` (virtual, default 0) and `showDeviceWarning()` in `mididrv.cpp` (the same dialog on non-DOS). Self-read only (no fresh-context reviewer in this run).
- c2: `5321b2e1a79 GRAPHICS: Blend hi-res text on 15/16-bit screens by table`. TDD: red = `make test` did not compile (`Blend16` unknown). The plan's two cases plus a
  third (`test_convertSpanEqualsBlendPixel`: `convertSpan` with and without a table, coverage 0/255/partial) are in `test/graphics/hires_text_blend16.h`.
  Green: 989 tests OK. **The exhaustive case is in the suite and runs in 7.0 s at -O0** (`./test/runner HiResTextBlend16TestSuite`): 5 formats (RGB565, RGB555, ARGB1555,
  RGBA5551, BGR565) x 6 colours x coverage 1..254 x 65,536 pixels = **499,404,800 checks, 0 mismatches**; no colour cut was needed. Mutations (each reverted):
  (a) `+ 126` in the green table -> `test_everyPixelEveryCoverage` found 974,848 mismatches (the plan's prototype: 974,899; my sed changed only the green line of `fill()`) and the
  existing `svf_aa.h` cases failed too; (b) `solid` not refreshed (`lastIndex < 0`) -> `test_composeSpanEqualsBlendPixel` red; (c) `_alpha` left out -> exhaustive 199,753,728 mismatches
  (= the two alpha formats x 6 x 254 x 65,536) plus `svf_aa.h` "format 3/4 blend"; (d) slot by `cov & 3` -> still green (a slot choice cannot change results: the reason no test pins it).
- c3: `c378339d73c SCI: Keep one hi-res text blend table per updated rectangle`. DOS rebuild; KQ1 rgb565 frame: status 0, both boxes 0 differing pixels against Linux
  `aa-kq1-rgb565-message.png` (whole frame differs by the same ~1000 px as before: the river animation and cursor); rgb555: max 1 per channel against Linux `aa-kq1-rgb555-message.png`.
- c4: `81653bdde1f GRAPHICS: Let hires_text_whole_kb choose whole or streamed faces`. Red: `wholeKb` not a member (compile). Green: 990 tests; cases for the default, `64`, `-1`, `99999`, `abc`, empty,
  `64k`, `1.5` (one warning each, 256 kept) and the range ends 0 and 16384. KQ1 rgb565 on DOS after the change: status and boxes 0 differing pixels against Linux.
- c5: `7c63d38e6e4 GRAPHICS: Warm streamed faces from their [preload] lists`. Red: 2 failed tests (`coldBuild < warmBuild` 5 >= 5, reads after drawing 10 != 5, no warning for a missing list);
  green: 992 tests. Mutation (call to `warm` commented out) -> the same failures. The test uses the streamed `JAMO.SVF` fixture and an in-memory `.HOT`.
- c6 (no commit; log lines of `-d1`, window frames each ~0.16 s, DOSBox-X pentium 60000 cycles, KQ1 AA pair map, rgb565): first message box after `Return` appears within the 0.33-0.49 s frame
  window in every run: `hires_text_whole_kb=0` (every face streamed) 4 frames/0.49 s (twice), `=256` 0.49 s (twice): no measurable difference at that resolution. The loading screen's text phase:
  1105 ms streamed (`=0`) vs 1083 ms whole (`=256`) (+22 ms, +2 %). **Memory at the first frame: 4344 KB free streamed vs 4716 KB whole**: for the AA pair map (faces of 12-25 KB) each
  streamed face costs its 64 KB block cache, so whole is cheaper in memory too below 64 KB; DOS's default 64 stays right for these maps. (`dos_stats_log=5` not needed: the `DOS: memory`
  lines carry the numbers.) Frames: `~/work/tmp/p7/shots/tl-x-w{0,256}-*/`.
- c7: `f0fe2e6f57f DOS: Add a hi-res text blend bench` (`backends/platform/dos/tools/blendbench.cpp`, `build-dos.sh bench BUILD_DIR` -> `BLEND.EXE`, 373,111 B). DOSBox-X, `core=normal`, a 400x128 box,
  15,487 covered pixels, 10 reps, `equal=1` everywhere:
  | preset | A blendPixel loop | B Blend16 across rows | C Blend16 per row | plan 0.3 B |
  |---|---:|---:|---:|---:|
  | 486 `cycles=fixed 23880` (DX2-66) | 119.43 ms | **47.68 ms** (555: 47.63) | 64.39 ms (555: 61.30) | 47.67 |
  | 486 `cycles=fixed 12010` (DX-33) | 237.79 ms | **94.94 ms** (94.84) | 128.21 (122.05) | 94.91 |
  | pentium `cycles=fixed 60000` | 47.50 ms | **18.96 ms** (18.94) | 25.61 (24.38) | 18.96 |
  B and C equal the plan (within 0.1 %). A is the lean per-pixel `blendPixel` loop kept in the bench file (119 ms DX2-66), leaner than the plan's "current composeSpan" 136.86 ms (that
  one also read/wrote through the generic `readPixel`/`writePixel` and `putText`): speedup of B over A 2.5x, over the old `composeSpan` 2.9x. Still UNVERIFIED-ON-HARDWARE (H1).

## P7d: input

- d1 (no code): `dos-events.cpp:113-115` maps `SDL_SCANCODE_LANG1` to `KEYCODE_HANGUL` and `LANG2` to `KEYCODE_HANJA` (re-read, unchanged). DOSBox-X mapper file (`~/work/tmp/p7/run-b8/han.map`: letters, digits,
  Enter, Esc, Backspace, Tab, Space, arrows, **F5, F7** added, `key_kor_hanyong "key 66"`, and `key_kor_hancha "key 67"`: **DOSBox-X's name for the Hanja key is `kor_hancha`**, not
  `kor_hanja`): run `Right Return sleep:20 F9 sleep:1 F9 sleep:1 F10 sleep:1 type:qhk`: log `Hangul mode off`, `Hangul mode on`; the parser box shows 봐 (`dos-p7-d1-han-yeong-bwa.png`).
  Hanja: the key reaches `KEYCODE_HANJA`, but SCI has no Hanja action (nothing to log): U4/H5 (Staging has no key mapper for these keys).
- d2: built-in composer (no OS IME on DOS): `ansdmf qhk` + Return -> the room reply **"이 쇠창살 문은 왕의 성(으로)의 유일한 입구이다."** (the cursor hides two syllables; `dos-p7-d2-door-reply.png`);
  `qhk` then BackSpace -> 보 (one jamo removed); Shift+q -> the double consonant, shown clipped to the preedit cell (a composer display matter, not compared with Linux here).
  U5/H5: KEYB KR / Korean layouts change nothing for the composer (it reads US scancodes) - not testable in the emulators.
- d3: **Alt+x after `qh` typed a jamo on DOS** (보 became 볻-like: a trailing ㅌ; `dos-p7-d2-d3-backspace-shiftq-altx-zoom.png`): the P4 item 3 leak is not a Linux screenshot-key artefact; there is no Alt+S screenshot
  action on DOS and the composer still takes the Alt-modified key. No DOS code change (the key arrives as a normal KEYDOWN with the Alt modifier); the fix belongs to the SCI composer
  (topic `ko/03-sci`: ignore events with Alt/Ctrl in `korean_input`), recorded here as an open item for that topic.

## P7e: memory, 8.3 names, saves (e1, e2, e5, e6 below once SCUMM.EXE/AGI.EXE exist)

- e3: `f328b6d32a2 DOS: Stage engine data under the names DOS gives them`. `test_stage_data.py` (11 cases) red (no `stage-data.py`), then green. Beyond the plan's code: a file that needs a folder's name (or a folder
  that has a file's name) is refused, nothing is written when any collision is found (it validates first, copies after), `--maps` also checks `[preload]` values, the map is read as `utf-8-sig`, an unnameable
  `data:` value counts as missing instead of raising. Real use: `python3 -I stage-data.py dists/engine-data/hires_text=hires_text DATA --maps .../*.map` -> `3 files` (NEODGM.TXT, NEODG16A.SVF, MAPS/KOREAN-D.MAP), rc 0.
  Mutations (base cut to 9, no collision check, extension cut to 4) each turn tests red (7, 3 and 1 failures); NOTE a Python bytecode cache keyed by mtime+size served a stale mutated module once
  (same-length edit within a second): `PYTHONDONTWRITEBYTECODE=1` for the test runs.
- e4: `19592df25ed DOS: List saves of targets longer than eight characters`. **Deviation from the plan, with evidence.** The plan's `listSavefiles` (engine names from the cache, then the 8.3 files under the engine's names) and
  "opening, removing and saving need nothing: DOS truncates the long name" is not enough: `DefaultSaveFileManager` finds a file by an exact (case-insensitive) cache key, and in a NEW session the keys are the
  directory's 8.3 names, so `openForLoading`/`exists`/`removeSavefile("kq1-rgb565.001")` find nothing: the list would show 한글 and the restore of the listed entry would fail. The host test
  (the real manager over `test/dos-savenames-tmp`, write -> list in a new session -> load -> exists -> remove) turns red for exactly those; so every single-file operation now goes to the base manager under `dosPattern(name)` and a
  listing is made for the cut pattern and mapped back by `engineName()`. TDD: red 1 (the file was written under the long name on the host), mutations (load/exists unmapped, listing not mapped back) each red; 995 tests green.
  DOS: red run (`SCI-c5.exe`, new session F7, target `kq1-rgb565`): empty list (`dos-p7-e4-longtarget-restore-list-empty-before.png`); green: the list shows 한글 (`dos-p7-e4-longtarget-restore-list-fixed.png`) and Return restores
  (`dos-p7-e4-longtarget-restored.png`). Limitation: two targets that share their first 8 characters share a save file (pack ids/targets stay <= 8, H-4).
- e1 memory gate (DOSBox-X 16 MB, phys_total 14,896 KB; stripped sizes: SCI.EXE 5,413,376, SCUMM.EXE 5,688,832, AGI.EXE 3,940,864, PLAY.EXE 279,040; jamo's GUI-less SCI.EXE was 5,670,989): free `dpmi_free` at `first-frame`:
  SCI KQ1 5-6-5 4,720 KB, 1-5-5-5 4,724, line repeat 4,624, 8 bpp 5,740, `rgb888` 3,224, 5-6-5 all faces streamed 4,344, LB1 CLUT8 5,472 (Staging 5,472); SCUMM MI1 KO 6,272 (5,960 from the two-zip PLAY run); AGI demo 8,748.
  `engine` phase: SCI 8,968-8,976, SCUMM 6,672, AGI 10,400. Worst measured point 3,224 KB (asked-for 32 bit) >= 2,048: **D-3 gate passed with the GUI in; Task e2 (GUI-less series) not carried.** Not measured: the quit-time line, a game
  played for long (the gate is at the measured points the plan names).
- e2: skipped (the memory gate passed).
- e5: `92f2d41f141 DOS: Document the memory budget` (README memory section with the numbers above; the Ultimate Talkie/mkute and Vorbis-selftest sections of jamo's README removed: not carried).
- e6: LFN check, `[dos] lfn=true ver=7.1` in DOSBox-X (confs `run-b8/x-lfn.conf`), KQ1 title: **with `_CRT0_FLAG_NO_LFN` the map is NOT found, and without the flag (a test build, `SCI-lfn.exe`) it is not found either**
  (`Hi-res text: hires_text_map data:hires_text/maps/korean-default.map: not found`, English-less empty buttons, `e6-lfn-on-flag.png`, `e6-lfn-on-noflag.png`); with `lfn=false` it is found (b8). The plan's premise (the flag makes a Windows 9x
  box truncate like MS-DOS) does not hold in this emulator: in LFN mode a long name given to the short-name API is not truncated. D-2's staging therefore works where DOS truncates (MS-DOS, LFN off) and not in an LFN-capable DOS box; the
  real fix is 8.3 engine data names (D-2's alternative, topic `ko/03-sci`: `kDefaultMap`, folder `hires_text/`). README says so. H6 stands (real MS-DOS 6.22/7.1 and a Windows 9x box unverified).

## P7f: EXEs, launcher, packages

- f1: `3d7da5d05a3 DOS: Build the launcher out of tree`: PLAY.EXE 279,040 B (as the plan), `build-dos.sh play $B/play`; `dos_play_*` host suites pass in `make test`.
- f2: `1c072dd9986 DOS: Build SCUMM.EXE with FLAC and Tremor`: `config.h` has `USE_FLAC`, `USE_TREMOR`, `USE_VORBIS`; irqcheck clean; stripped 5,688,832 B. Carried: `kVorbisRefillSamples` (vorbis.h, vorbis_intern.h), `patches/tremor-chunksize.patch`,
  `build-deps.sh` trimmed to the codecs (not run: the codecs of `~/opt/codecs-dos` were used). **Not carried (U6 answered): the Vorbis setup cache (`vorbis.cpp` +504, `vorbis.h` +42), `dos-vorbis-selftest.*`, `check-tremor-cache.sh`** (MI2 speech
  opening speed; the `dos.cpp` selftest keys and the module.mk line went with them). MI1 KO (pack of `~/work/tmp/p6/data/mi1`, S1, no map): credits and the Lookout dialogue in Korean at 320x200 CLUT8
  (`dos-p7-f2-mi1-credits.png`, `dos-p7-f2-mi1-lookout-dialogue.png`); the title credits frame against the Linux P6a frame `a6-mi1/scummvm-mi1ko-00002.png`: max 4 per channel (6-bit DAC); the Lookout instant has no Linux frame of P6a to compare.
- f3: AGI.EXE built (3,940,864 B stripped, irqcheck clean), runs the English AGI Demo 2 (detected with `AGI.EXE --add`) at 320x200 CLUT8: Sierra start screen, first frame 8,748 KB free (`dos-p7-f3-agi-demo.png`). No Korean claim.
  Note: `--add --path=C:\AGIGAME` on the DOS command line stored a punycode path (`xn--CAGIGAME-kaa04dzm`: backslashes are not separators for `--path`); PLAY gives forward-slash paths.
- f4 (documentation only, in the README): PLAY's real layout (`RUN.INI` made per run from the pack's `ID.INI`, `USER.INI`, `SAVES\`; never copies the pack INI) differs from the plan's text and from jamo's README, which is fixed.
- f5: `9c4d1ffe3eb DOS: Stage the engine package out of tree`: `DOS_BUILD=$B build-dos.sh stage DIST`: SCI.EXE 5,413,376; SCUMM.EXE 5,688,832; AGI.EXE 3,940,864; PLAY.EXE 279,040; CWSDPMI.EXE 21,325; FLAC.TXT 1,509; VORBIS.TXT 2,932;
  DATA\ENCODING.DAT 179,670; DATA\HIRES_TE\NEODG16A.SVF 16,408, NEODGM.TXT 2,451, MAPS\KOREAN-D.MAP 647. Refuses an in-tree DIST_DIR; keeps other files of the folder.
- f6: `987ea3cf94e DOS: Describe the Korean text options of the DOS build`.
- Two fixes found by the acceptance runs (not in the plan): (1) `ee7f90931dc DOS: Look for engine data in DATA beside the EXE`: a PLAY-made RUN.INI has no `extrapath`, so `data:hires_text/...` was not found
  (buttons empty on the KQ1 title in the two-zip layout); the backend now adds `DATA` (relative to the home folder) to the transient extrapaths; the search-set entry is named `dosdata` (a "SearchSet::add: archive 'DATA' already present" warning).
  (2) `48053cbd02e BASE: Add --set=KEY=VALUE for any configuration key`: PLAY's `--8bit`/`--set` pass `--set=` to the engine, which `ko-2026.4` did not know (jamo's `base/commandLine.cpp` hunk + `set-option.h` + its test; `config-load.h` is GUI-less work and not carried).
  Checked: `PLAY KQ1KO ... --8bit kq1aa` -> `DOS: mode 640x400 CLUT8`.

## P7g: acceptance (scratch runner `~/work/tmp/p7/bin/accept.sh`, not committed: the harness owns acceptance scripts, H-3)

Layout: the staged engine package copied to `C:\SCUMMVM` (+ the six AA pair faces of the prototype's DATA, harness dependency H-2), the game pack mounted read-only as `D:` (`mount d <dir> -ro`), `PLAY --install ID`, then `PLAY ID -d1 --debugflags=Korean [args] target`.
md5 of every file of D: before and after each run: unchanged (KQ1 15 files, LB1 56, MI1 35).

- g2 KQ1 KO (pack = the P3 pack with the prototype's upper-case `KO\` incl. `AA-PAIR.MAP`; targets `kq1ko` default map, `kq1aa` AA pair rgb565): DOSBox-X: title with Hangul buttons (`dos-p7-g2-x-kq1-title-twozip.png`), `kq1aa`: `DOS: mode 640x400
  RGB565@2`, `Hi-res text: screen format RGB565@2`, Han/Yeong F9 twice -> `Hangul mode off` / `on`, `qhk` -> room description, **status and both boxes 0 differing pixels against Linux** (`dos-p7-g2-x-kq1-reply.png`); F5 `gksrmf` -> `GAMES\KQ1KO\SAVES\KQ1AA.001`, new
  session F7 lists 한글, Return restores (`dos-p7-g2-x-kq1-restore-list.png`). DOSBox Staging: `640x480 RGB565@2 (line repeat from 640x400)`, reply frame and restore list (`dos-p7-g2-staging-kq1-*.png`, window 758x569). No Han/Yeong in Staging (U4).
- g3 LB1 KO (P4-format pack `~/work/tmp/p4-data/lb1/LB1`, `PLAY LB1 --language=ko lb1`): **partial**. Both emulators: the fingerprint screen in Hangul, a click (xdotool mousedown/up; a plain `click` did not register) passes it, the Hangul box 'the curtain is rising', the Hangul
  question 'have you seen The Colonel's Bequest before?' with 예/아니오 (`dos-p7-g3-x-lb1-*.png`; Staging identical). Mode 640x400 CLUT8, first frame 5,472 KB free. The pack's own `HIRESVF.MAP`/`HIRESBIT.MAP` are map version 2, so the engine's default map draws (H-2).
  **Not reached: the typed command (`문을 봐`) and save/restore** (the intro to the first prompt needs many timed clicks); U3 stays open for LB1 typing and saves.
- g4 MI1 KO (pack of `p6/data/mi1`, own `MI1KO.INI` with an S1 target, `engineid=scumm`): PLAY picks SCUMM.EXE; both emulators: credits and the Lookout dialogue in Korean at 320x200 CLUT8 (`dos-p7-g4-*-mi1-montage.png`); first frame 5,960 / 5,956 KB free.
  **Not done: a save with a Korean name through the original save screen** (not attempted).

- g5 memory and timing record (one table; DOSBox-X pentium 60000 cycles, 16 MB; free KB at `first-frame`; EXE sizes stripped):
  | Game, emulator | EXE | screen | free | notes |
  |---|---|---|---:|---|
  | KQ1 AA pair, X | SCI 5,413,376 | RGB565 | 4,720 | first message box within 0.33-0.49 s of Return; loading text phase 1,083 ms (whole) / 1,105 ms (all streamed, 4,344 KB) |
  | KQ1, X | | RGB555 / CLUT8 / RGB888 | 4,724 / 5,740 / 3,224 | |
  | KQ1, X forced and Staging | | 640x480 line repeat RGB565 | 4,624 | |
  | LB1, X and Staging | | CLUT8 | 5,472 | protection screen + two dialogs only |
  | MI1, X / Staging (PLAY) | SCUMM 5,688,832 | CLUT8 320x200 | 5,960 / 5,956 | direct run 6,272 |
  | AGI demo, X | AGI 3,940,864 | CLUT8 320x200 | 8,748 | |
  `BLEND.EXE`: B (Blend16 across rows) 47.68 / 94.94 / 18.96 ms (DX2-66 / DX-33 / Pentium 60000), A (per-pixel blendPixel loop) 119.43 / 237.79 / 47.50 ms. DESIGN 8's faces budget ("well under 0.5 MB"): the KQ1 pair map's faces are whole
  (12-25 KB each) or, streamed, 8 KB of tables + 64 KB cache per face; whole costs less memory below the 64 KB limit (measured +372 KB for streaming six small faces).
- g6: DESIGN.md: section 2.5 (14 SDL3 commits, upstream main 1362030, no DOS port in release-3.4.20), section 7 (`hires_text_whole_kb` row), section 8 (14 patch commits, the D-1 screen-format order, the measured composite cost). Section 5.2's `svf_whole_kb`
  name is left as the design text; the key is `hires_text_whole_kb`.

## Conflicts of section 1 and decisions as answered

D-1: auto = RGB565, then XRGB1555, then CLUT8 (the engine picks the first of what the backend offers); XRGB8888 only for `render_target=rgb888` (tests `dos_modes.h`, runs c1). D-2: staging written (`stage-data.py`), collisions refused, `_CRT0_FLAG_NO_LFN` set;
**e6 showed an LFN-capable DOS does not truncate, so the LFN premise fails (see e6)**. D-3: gate passed, no GUI-less series. D-4: `-march=pentium` default, `DOS_MARCH=i486` in `build-dos.sh` (not exercised: no 486 build was run). Conflict 3 (mode 13h): Korean targets run at
VBE 640x400 (the runs), English targets at 320x200 (MI1, AGI). Conflict 5 (debug socket): `--disable-debug-socket` not passed (configure lacks the option), `uart` range optional in `irqcheck.py`. Conflict 7: AGI.EXE builds and runs English only.
Conflict 8: MI1 KO on DOS is the S1 picture.

## Checkpoints

- Host `make test` (`$B/host`): **1000 tests OK** (N0 770 + DOS suites 216 + Blend16 3 + options 1 + font set 2 + save names 3 + `--set` 5 = 1000: 986 after b7, 989 after c2, 990 c4, 992 c5, 995 e4, 1000 with `--set`); ASan (`$B/asan`, `-fsanitize`): 1000 tests OK, 0 sanitizer lines.
- Exhaustive Blend16 test in the suite: 499,404,800 checks, 0 mismatches, 7.3 s at -O0; mutations (a) (b) (c) red, (d) green (documented).
- `tools/korean/sync-svf.sh --check`: `sync-svf: graphics/svf matches svf-fonts-open 3d40de8d940f50ed4a28dfc1851fa776a62f42e5`, rc 0. `python3 -I tools/korean/mkgolden.py --check`: no output, rc 0. Tools tests `tools/korean/test_{k6bake,strcheck,trscheck}.py` rc 0; `backends/platform/dos/test_stage_data.py` 11 OK.
- `devtools/check-commit-msg.py` on all 26 commits of `ba0eecee2c4..HEAD`: 0 bad; each has exactly one `Assisted-by: Claude Code`, no `Co-Authored-By`.
- Self-read of the diff since checkpoint B (text_compose, font_set warm, hires_options, dos-savefile, stage-data, build-dos.sh, dos.cpp): the Blend16 differential tests (random rows with and without a table, `convertSpan`) are the randomized differential; `compositeRect`/`presentRect`
  with a table are exercised only through the DOS game runs (frames 0 differing pixels), not by a host test.

## Not done

P7h (UART debug socket) skipped as instructed. Task e2 not needed. LB1: typed command and save/restore not reached; MI1: Korean-name save through the original save screen not attempted. No fresh-context reviewers for checkpoints A, B, C, E, G (self-read only).
The staged DATA does not carry the AA pair faces (they came from the prototype's DATA for the KQ1 runs: harness H-2). The pack's version-2 maps (KQ1, LB1) fall back to the engine's default map (H-2).
The Alt-key leak into the composer (d3) is an open item for `ko/03-sci`. PLAY's `--8bit` is checked; its `--set` and the `--language` of LB1 are exercised only through those runs.

## UNVERIFIED-ON-HARDWARE (all runs above are emulators: DOSBox-X 2026.08.31, DOSBox Staging 0.83)

H1 real 486/Pentium speed of composite and glyphs (BLEND.EXE and a KQ1 dialogue); H2 VBE mode lists/LFB of real cards and 16 vs 32 bpp flush cost; H3 DAC width at 8 bpp; H4 Sound Blaster variants after the SDL rebase (the smoke gate proved SB16 detection and the timer in both emulators only);
H5 Han/Yeong (0xF2) and Hanja (0xF1) of a real Korean keyboard (and Hanja has no engine action); H6 8.3 truncation on MS-DOS 6.22/7.1 and in a Windows 9x DOS box (e6 suggests LFN-capable DOS does not truncate); H7 memory with HIMEM/EMM386, CWSDPMI swap, `FILES=`;
H8 the BATs' `SET` trailing-space quirk (BATs not exercised; PLAY was started from autoexec). U4 Staging has no mapper for the Han/Yeong key.
