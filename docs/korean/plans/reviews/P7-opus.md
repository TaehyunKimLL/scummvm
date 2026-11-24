# P7 (DOS port) review: Claude lane

This review covers ScummVM `ba0eecee2c4..838b29d4ec3` (26 commits, branch `ko/p7-dos`) and the SDL3 series `origin/main..dos-p7-1362030` (14 commits, HEAD `cc504b3b8`). It was done independently: the other lane's output was not read. All scratch work is under `~/work/tmp/p7-review-claude/`. No worktree was configured, edited or committed. The interrupt/DPMI reading (d) was delegated to a read-only sub-auditor. I checked its key claims against the source myself (marked "verified" below).

## Verdict

**P7 is sound to merge into ko-2026.4 once fix M1 below is made. It is not yet ready to rely on for a DOS release on real hardware until M2-M5 are fixed.**

- The shared-code part is correct and inert on other platforms. The Blend16 table blend matches an independent model bit for bit on 77 billion checks. The Linux KQ1 frame hashes are unchanged.
- There are no blockers.
- There are five majors:
  - one data-loss defect in the 8.3 save mapping (M1);
  - four hardware-level audio and interrupt defects, all unseen in the emulators (M2-M5).

Required before merge: M1.

Required before the DOS release: M2, M3, M4, M5. M6 is documented and needs the 8.3 data names of `ko/03-sci` before the release can promise a Windows 9x DOS box.

## Ranked findings

### Shared package (outside backends/platform/dos)

**Blend16 (graphics/hires_text/text_compose.{h,cpp}) is correct.**

I wrote my own model from `graphics/svf/svf_draw.c` blend565/blend555: per channel `(bg*(255-cov) + (fg8>>loss)*cov + 127)/255`, opaque alpha. Pixel layouts are hand-coded, not taken from PixelFormat. The model is in `~/work/tmp/p7-review-claude/blend/model.cpp` and was run against the in-tree `Blend16::blend`, `blendPixel`, `composeSpan` and `convertSpan`.

Nine formats were covered: RGB565, XRGB1555, ARGB1555, RGBA5551, BGR565, XBGR1555, ABGR1555, RGB444 and ARGB4444. That is a superset of what the backend offers; the emulators offer RGB565, XRGB1555, XRGB8888 and BGR24.

| Part | What it covers |
|---|---|
| (1) Exhaustive | Every dst 0..65535 × cov 1..254 × 256 colours, sweeping every 8-bit value of every channel. One table object is reused, so its slots are reused too. |
| (2) Random thrash | 20 M random (colour, cov, dst) calls per format. |
| (3) Span rows | 20 k random `composeSpan`/`convertSpan` rows (coverage 0, 255, 85, 170 and random), with a shared table and without one. |

Results:

- `-O2`: 77,116,665,552 checks, 0 mismatches (5 min 43 s).
- ASan+UBSan `-O1`, colours step 17: 5,205,323,472 checks, 0 mismatches, 0 sanitizer reports.

Mutations my model catches:

| Mutation | Mismatches |
|---|---:|
| `+126` in the green fill | 5,223,150 |
| `_alpha` dropped | 1,164,514,088 |
| Green dropped from the slot key | 137 |
| Slot chosen by `cov & 3` | 0 (cannot change results, as the notes say) |

Table checks:

- The table is 4 × (4 + 3×64×2) = 1,552 bytes and lives on the stack or for one call.
- Indices are at most 63, given loss ≥ 2.
- Coverage 255 skips the table, which is exact: `(255*fg+127)/255 = fg`. 0 is skipped too.
- There is no static state, so it is reentrant.
- The key cannot alias, because cov 0 is never asked for.

Linux identity: ASan+UBSan build of 838b29d4ec3, headless KQ1 (aa-games `pair` map). The hashes are those recorded in AA-tests section 5:

| Run | Status rows | Message box |
|---|---|---|
| rgb565 | `04e8b1964fcc382b` | `5b1e065b15f8c471` |
| rgb555 | `75d5af99face0452` | `bc9206fc6b8263a0` |

The only sanitizer lines in those runs were upstream `dbopl.cpp` UBSan reports, in untouched code.

Host speed (x86-64 `-O2`, a 400×128 box):

| Case | blendPixel loop | Blend16 |
|---|---:|---:|
| One text colour, 64-bit | 0.19-0.23 ms | 0.16 ms |
| One text colour, `-m32 -march=pentium` | 0.37 ms | 0.20 ms |

So the 2.5× on an emulated 486 is plausible: the gain is the avoided divisions.

1. **minor - no test catches a slot-key aliasing mutation.** `test/graphics/hires_text_blend16.h:20-34`.
   - Scenario: I dropped `(uint32)g << 16` from the key (`text_compose.h:74`) in my scratch copy. `HiResTextBlend16TestSuite` (3 tests) and `SvfAaTestSuite` stayed green. My random-thrash part found 137 mismatches.
   - Cause: the exhaustive case walks only 6 colours, and no two consecutive colours differ in only one channel.
   - Fix: add a random interleaved (colour, cov, dst) loop through one table against blendPixel, as in part (2) of my model.
2. **minor (performance) - worst case when colours alternate.** `text_compose.h:73-77`.
   - Scenario: two text colours alternating at the same `cov >> 6` bucket refill a slot per pixel, 128 divisions each. On this host that is 0.54 ms against 0.19-0.22 ms for the blendPixel loop, 2.4× slower (bench `blend/bench.cpp`, random index 0/1).
   - Impact: SCI draws one colour per string, so this is only a pathological case. `BLEND.EXE` measures one colour only.
   - Fix (optional): key the slot by `(cov >> 6) ^ (index & 1)`, or use 8 slots. Otherwise say so in the comment.
3. **nit** - P7-notes "499,404,800 checks" is miscounted: 5 × 6 × 254 × 65,536 = **499,384,320**. The test does not count its checks.
4. **nit** - `Blend16::Blend16` (`text_compose.cpp:73`) has no assert that the format is 2 bytes with every loss ≥ 2. A 2-byte format with a 7- or 8-bit channel would index `r[64]` out of bounds. No such format is offered today. Fix: `assert(fmt.bytesPerPixel == 2 && fmt.rLoss >= 2 && fmt.gLoss >= 2 && fmt.bLoss >= 2)`.
5. **minor - the inertness claim in P7-notes (Checkpoint B) is not quite right.**
   - `48053cbd02e` adds `--set` for every platform.
   - It also adds a `usage()` `#else` branch (`base/commandLine.cpp:293-303`). That branch changes behaviour on the ports that define `DISABLE_HELP_STRINGS` (configure 3721/3852/3933): they now print the error before `exit(1)`. Harmless, but not DOS-guarded.
   - Linux `configure`: `config.h` is byte-identical and `config.mk` is identical apart from the SAVED/srcdir lines (re-run here, sci,scumm, base against head).
   - Fix: list both in PATCHES.md as non-DOS behaviour, or guard the `#else` with `DOS_DJGPP`.
6. `--set` parser (`base/set-option.h`): 2 M random strings (high bytes, quotes, `=`, `;`, spaces) under ASan+UBSan against an independent model gave 0 differences. Keys are limited to `[A-Za-z0-9_-]` and the value is everything after the first `=`. No overflow or injection beyond what the command line already allows. Note that `--set=game=...` and `--set=config=...` act like the matching options. That is documented ("other ScummVM options too"), so there is no finding.
7. `hires_text_whole_kb` and `[preload]`:
   - The Linux default of 256 KB equals the old constant; DOS registers 64.
   - The parser is range-checked.
   - `warm()` bounds UTF-8 decoding by `end` and gives `svf_glyph` 2 × the cell bytes, which `svf.h:224` requires for a composed glyph.
   - No finding.
8. **minor - in-place INI writes on DJGPP.** `aba8a7a6dc3`, `backends/fs/stdiostream.cpp:50-53`.
   - Scenario: a crash, power loss or full disk while `SCUMMVM.INI` or `RUN.INI` is written leaves it truncated. PLAY's `captureChanges` never erases a key (`play-merge.h:249`), but it can store a value cut mid-line.
   - Upstream uses the same scheme for its other 8.3 ports, so this is acceptable.
   - Better fix: write to an 8.3-valid temporary name (`NAME.$$$`), then `remove` + `rename`.

### DOS package (backends/platform/dos and its host tests)

**M1 - major (data loss) - two save names that DOS truncates to one name share one file silently, and a rename between them deletes the save.** `backends/platform/dos/dos-savefile.h:166-201`, `dos-savenames.h:36-46`.

- **Reproduction 1, randomized model test.** File `~/work/tmp/p7-review-claude/dos_zz_review_savenames.h`. It runs the real `DOS::SaveFileManager` over `asan/test/dos-review-tmp`, under ASan+UBSan:
  - 12,000 operations: save (compressed or not), load, exists, remove, list, rename, and a new session.
  - Targets: `kq1-rgb565`, `kq1-rgb555`, `kq1`, `lsl1sci-ko`, `lsl1sci-en`, `monkey2-ko`, `abcdefgh`, `abcdefghi`.
  - The mapping is self-consistent: 0 errors against the DOS-truncation model, with aliasing renames excluded.
  - **Isolation is violated 218 times**: one target loads another target's save.
- **Reproduction 2, direct sequence.**
  - `openForSaving("lsl1sci-ko.001")` writes "LSL1 KO SAVE".
  - `openForSaving("lsl1sci-en.001")` writes "LSL1 EN SAVE".
  - `openForLoading("lsl1sci-ko.001")` then reads `LSL1 EN SAVE`: the KO save is gone, and the KO list shows 1 entry.
  - `renameSavefile("kq1-rgb565.001", "kq1-rgb555.001")` returns **true**, and afterwards **neither name exists**. `SaveFileManager::copySavefile` opens the same file for writing, which truncates it; `renameSavefile` then removes it.
- **Why it is realistic.**
  - ScummVM's own target names collide in the first 8 characters, for example `lsl1sci-pc` and `lsl1sci-pc-ko` (generated from gameid-platform-language).
  - The implementer's own `run-b8/c/SCUMMVM/SCUMMVM.INI` has `kq1-rgb565` and `kq1-rgb555` with one shared `savepath`.
  - PLAY keeps saves per pack and its ids are ≤ 8 characters (H-4), but `SCI.EXE` run directly uses a shared `SAVES`.
- **Minimal fix:**
  - (a) In `DOS::SaveFileManager`, refuse `openForSaving` (return nullptr with an error) when another configured target with the same effective savepath has the same 8-character base. Do the same check once at engine start and warn. Alternatively, map bases longer than 8 characters to `first-4 + 4-hex CRC16(full target)`, so different targets never share a name.
  - (b) Make `renameSavefile`/`copySavefile` return false when `dosPattern(old) == dosPattern(new)`.
  - Add the isolation property to `test/backends/dos_savenames.h`.
- The existing `dos_savenames.h` tests only the single-target round trip.
- Separately (nit): `dosPattern` cuts a base containing `#` or `?` but not `*`, while `engineName` leaves all three alone (`dos-savenames.h:41` vs `:53`).

**M6 - major (release scope, documented, UNVERIFIED-ON-HARDWARE) - engine data under long names is not found in an LFN-capable DOS.**

- P7-notes e6, emulator-verified there: with `lfn=true` the map is not found, with or without `_CRT0_FLAG_NO_LFN`, and the buttons are empty.
- D-2's staging therefore works only where DOS truncates.
- This is not a P7 merge blocker; the README says so. The release must either ship 8.3 engine data names (`kDefaultMap`, folder `hires_text/`, topic `ko/03-sci`) or state "MS-DOS / LFN off only".

**minor - the transient `extrapath=DATA` hides a game's own extrapath from SearchMan.** `ee7f90931dc`, `dos.cpp:225-226`.

- `ConfigManager::get` checks the transient domain first, and `base/main.cpp:281-284` adds only `ConfMan.getPath("extrapath")`.
- So a pack or user `[target] extrapath=X` is no longer searched for engine files. `hires_files.cpp:46-51` still searches all three domains, which is why hi-res text works.
- The commit message ("an extrapath of the user's still comes after it") is wrong for the game domain.
- Fix: have PLAY write `extrapath=DATA` into `RUN.INI [scummvm]` when the pack has none (main.cpp:290 adds the app-domain extrapath as well). Alternatively, add `DATA` as a hi-res root only under `__DJGPP__` and drop the transient key.

**`stage-data.py`: no blocker.**

- The 11 in-tree tests are OK.
- Hostile names: `..`, `.`, `a..b`, `con.txt`, `é.txt` and `ǆ.x` are refused. Traversal through PREFIX or `data:` values is refused, because `..` has two dots.
- nit: `str.upper()` runs after the 8-character cut and is not ASCII-checked:
  - `ﬀﬀﬀﬀﬀﬀﬀﬀ.map` becomes `FFFFFFFFFFFFFFFF.MAP` (a 16-character base);
  - `straße.txt` becomes `STRASSE.TXT`.
  - The sources are trusted engine data. Fix: `if not component.isascii(): raise`, before the cut.
- nit: `CLOCK$` is not in `DEVICES`.

**`build-dos.sh`: nit.** `$src` is the logical `cd && pwd` path while `$out` is the physical `realpath -m`. With a symlinked source path the "outside the source tree" check can be bypassed. The `rm -rf "$out/DATA"` in stage is quoted and limited to DIST_DIR. Fix: `src="$(cd ... && pwd -P)"`.

**Host tests of the DOS pure parts:**

- ASan+UBSan, `--enable-asan` plus `-fsanitize=undefined`, sci only: **960 tests OK, 0 runtime errors, 0 ASan reports**.
- All 17 `Dos*`/`PlayHome` suites ran.
- The DOS suites are compiled into the host test runner only (`test/module.mk`: `ifneq ($(BACKEND),dos)`).
- The DOS objects are built only with `BACKEND=dos` (`backends/module.mk`, `audio/module.mk`).

**Interrupt, DPMI and audio defects.** The ScummVM side is listed here; the SDL3 side is in the next section.

- **M5 - major (definite, verified) - a fatal signal leaves the Sound Blaster running.** `backends/platform/dos/dos-exit.cpp:313-318`.
  - `fatalSignal()` stops only the PIT.
  - After SIGSEGV, SIGFPE, SIGILL, SIGABRT or Ctrl-C/Break (which `installExitSignals` turns into an exit), the DSP stays in auto-init with DMA unmasked over freed DOS memory. The speaker loops noise until a reboot or another SB program, and OPL/MPU notes hang.
  - Fix: in `fatalSignal`, after `stopTimerHardware()`, call `DOS::silenceAll()`, reset the DSP and mask the DMA channel. Port I/O only.
- **minor (suspected, verified code) - `DosMutex::unlock` restores IF with `popfl`.** `backends/mutex/dos/dos-mutex.cpp:46`. At IOPL < CPL, `popf` does not change IF. Under a DPMI host other than CWSDPMI (the `lockAll` path in `dos-irq.cpp` exists for those), interrupts stay off after the first lock. `irqRestore()` and SDL's own code already use `if (flags & 0x200) sti`; use the same here.
- **minor - the SB IRQ handler never checks that the interrupt is its own** (SDL `SDL_dosaudio_sb.c:167-190`).
  - On an SB16 the MPU-401 shares the IRQ. The UART ACK during `dos_mpu401.cpp:205-223`, or MIDI-in bytes left unread (ScummVM reads only while it writes), advance the ring by a chunk. Unread input can hold the line high, so audio stops.
  - With IRQ 7 or 15, a spurious interrupt eats a chunk and sends an EOI.
  - Suspected (hardware). Fix: read mixer register 0x82; check the ISR via OCW3 for IRQ 7 and 15.
- **minor - `irqcheck.py` proves less than its name suggests.**
  - It allows data by symbol-name regex, not by what is locked at run time.
  - It cannot see memory reached through registers.
  - It attributes an access to the nearest preceding symbol.
  - The `stage` step does not re-check the stripped binaries.
  - It is still a useful guard, and all current `isr_*` data is locked. Its README text should say exactly what it proves.

Checked and correct (sub-auditor, line-level):

- PIT divisor 1193, mode 2, BIOS chain every 65,536 clocks, restored on every exit path.
- Timer handler FPU save/restore.
- PIC mask save/restore.
- OPL delays, detection and reset.
- MPU-401 reset, UART mode, ACK, and timeouts.
- Heap `cli` only around bookkeeping.
- The SB and keyboard handlers call no libc, heap or SDL functions, and all their data is locked.
- DJGPP wrappers run on locked stacks.

### SDL3 package (14 patches)

Smoke gate re-run with **my own build** of `smoke.c` against `~/work/tmp/p7/prefix/sdl3-dos-min-kbd` (`SDL-3.5.0-cc504b3b8` = `dos-p7-1362030`):

| Emulator | Result | 640×400 formats | 640×480 formats |
|---|---|---|---|
| DOSBox-X | `video=vesa audio=soundblaster`, `640x400 INDEX8 mode found`, window/surface ok, `audio ok`, `timer 10ms x 1000ms -> 99 callbacks`, `done` | XRGB8888, RGB565, XRGB1555, INDEX8 | — |
| Staging | the same | XRGB8888, INDEX8 | XRGB8888, BGR24, RGB565, XRGB1555, INDEX8 |

The mode lists match the notes.

- **M2 - major (definite, verified; pre-existing upstream line kept by the series) - SB16 close stops a 16-bit auto-init transfer with the 8-bit command.** `SDL_dosaudio_sb.c:698-699` sends `0xDA` (exit *8-bit* auto-init) for 16-bit. The correct command is `0xD9`, or `0xD5` to halt at once. The card keeps requesting DMA and raising IRQs on masked lines. ScummVM hides it by a DSP reset in `soundBlasterClosed()`; plain SDL3 does not. Fix: `0xD5`, `0xD9`, or simply `CheckForSoundBlaster()` as the high-speed path does.
- **M3 - major (definite, verified) - BLASTER `H`, `D` and `I` are not range-checked.** `SDL_dosaudio_sb.c:782-790`. A legal SB16 setting with H equal to D, or H < 4, gives:
  - `high_page_ports[ch]` of 0, so the page byte goes to port 0;
  - a garbage DMA mode `0x58|(ch-4)`;
  - address port `0xC0+(ch-4)*4` = 0xB4.
  
  Fix: accept H only in 5..7, otherwise use the 8-bit controller with 16-bit DSP commands. Accept D only in {0,1,3} and I only in 2..15.
- **M4 - major (definite, verified; upstream arithmetic the series moved) - the pre-SB16 time constant is truncated and the real rate is not reported.** `SDL_dosaudio_sb.c:381-382`. On an SB Pro in stereo at 22,050 Hz: 1000000/44100 = 22, so TC is 234 and the real rate is 45,454 Hz, 22,727 Hz per channel. That is 3.1 % fast in pitch, tempo and lip-sync on every SB Pro. Fix: round the divisor and set `device->spec.freq = 1000000/(256-TC)/channels` before `SDL_UpdatedAudioDeviceFormat`.
- **minor - the 64 KB-boundary trick is valid only up to 32 KB** (`src/core/dos/SDL_dos.c:84`). ScummVM's 4096 frames × 4 bytes = 32 KB is exactly safe. Fix: cap `buffer_size` or align to a 64 KB / 128 KB window.
- **minor - the DMA auto-probe (patch 0007) programs and unmasks channels 0, 1 and 3 without saving their state.** `SDL_dosaudio_sb.c:438-484`. That destroys an ECP or other card's set-up. The transfers are read-mode only, so no memory is written, and channel 2 (floppy) is skipped correctly. `ProgramDMA` also runs with interrupts on while the handler is hooked, and a late IRQ's `out 0x0C` can split the address write. Fix: `cli` around `ProgramDMA`; probe only when BLASTER lacks D, or restore the masks.
- **minor:**
  - IRQ 2 is unmasked only on the master; the slave bit for IRQ 9 is never unmasked (`SDL_dos.c:133-138`).
  - `DOS_UnhookInterrupt(..., true)` masks the IRQ whatever its earlier state (`SDL_dos.c:150-155`).
  - The ring cursors are signed `int` and overflow after about 3.4 h (UB, `SDL_dosaudio_sb.c:100-107`).
  - The scheduler issues `hlt` at CPL 3, which raises #GP under CWSDPMI if no thread is runnable (`SDL_dos_scheduler.c:246`).
  - The SB Pro stereo set-up does not turn the mixer filter (0x0E bit 5) off and has no priming byte.
  - All are suspected or hardware-only except the signed overflow.
- **nit:**
  - The keyboard ring has no overflow check and `head`/`tail` are not volatile (`SDL_dosevents.c:265-267`).
  - The DSP reset pulse of 3 µs is truncated to 3 uclock ticks (about 2.5 µs).
  - The ring is locked and never unlocked before `SDL_free`.
- **Han/Yeong patch 0014: no finding.**
  - It handles both forms: press-only 0xF2/0xF1, and 0x72/0x71 make with an F2/F1 break.
  - LANG1/LANG2 go to `KEYCODE_HANGUL`/`KEYCODE_HANJA` (`dos-events.cpp:113-115`).
  - The extended prefix is excluded, and other keys are untouched.
  - Emulator-verified by the implementer (d1); a real keyboard is H5.

### Builds, commits, guidelines

- **Every commit builds and passes on the Linux host, in plan order.** All 27 states (`ba0eecee2c4` + 26) built incrementally, sci,scumm, `-O0`, `make test`. Test counts go 770 → 986 (`8d8262d73ae`) → 989 → 990 → 992 → 995 → 1000 (`48053cbd02e`), with 0 compiler warnings.
  - The one failing test at every state, **including the base**, is `HiResTextShippedMapsTestSuite::test_diskFiles`. It fails because `git archive` leaves the submodule-backed files out, so it is not P7.
  - The first attempt reused mtimes from `git archive` and gave false link errors; it was discarded.
- **DOS builds at HEAD** (my builds from `838b29d4ec3` with the scratch SDL3 prefix):
  - `SCI.EXE`, `SCUMM.EXE`, `AGI.EXE` and `PLAY.EXE` (279,040 B) all build.
  - irqcheck is clean for each: `dos` 68 B, `kbd` 36 B, `sb` 356 B, `timer` 1316 B with 2 indirect exits.
  - Warnings are upstream only (`array.h`, `scumm/saveload.cpp`).
  - DOS builds per commit were not run.
- **Commit messages:** subjects are `AREA: Verb ...`, each has one `Assisted-by: Claude Code` and none has Co-Authored-By.
  - nit: AI-GUIDELINES.md asks for `Assisted-by: AGENT_NAME:MODEL_VERSION`. That convention was already used before P7.
  - nit: several code comments are long explanations (for example `dos-savefile.h:95-103`, `:154-161`), against "lengthy LLM-oriented explanations are forbidden".

## Emulator-verified facts vs hardware-only risks

**Verified in this review (DOSBox-X 2026.08.31; Staging 0.83 for the smoke gate):**

- The SDL3 smoke gate passes in both emulators, with the mode lists above.
- KQ1 Korean with **my stripped SCI.EXE from 838b29d4ec3** (copy of `run-b8`, target `kq1-aa`, `--set=render_target=rgb565`):
  - log `DOS: mode 640x400 RGB565@2`, `Hi-res text: screen format RGB565@2 for render target rgb565`;
  - first frame `dpmi_free=4692`;
  - the `qhk` room message frame against Linux `aa-kq1-rgb565-message.png`: status rows **0** differing pixels; message box 380 differing, **all inside the mouse cursor's box (320,200)-(342,232), 0 outside**.
  - This also exercises `--set`.
  - The implementer's `x-aa565.conf` passes `--render-target=`, which this build rejects ("Unrecognized option"): a stale scratch conf, not a code defect.
- The implementer's `dos-p7-c1-x-rgb555-message.png` against Linux `aa-kq1-rgb555-message.png` differs by at most 1 per channel outside the cursor (5-bit expansion), as claimed.
- Screenshots viewed and matching their captions: `g2-x-kq1-reply`, `e4-longtarget-restore-list-fixed`, `d2-door-reply`, `g4-x-mi1-montage`, `g3-x-lb1-seen-before-dialog`.
- **Host verification:** Blend16 exactness (exhaustive), the Linux frame hashes, the 8.3 save mapping consistency, and the collision data loss (M1).

**Hardware-only (UNVERIFIED-ON-HARDWARE, judged by reading):**

- M2: 16-bit close command.
- M3: H/D/I range.
- M4: SB Pro rate.
- The IRQ-sharing items.
- The DMA probe's effect on other devices.
- The IRQ 2/9 cascade.
- M5's audible effect (the code path itself is definite).
- H1-H8 of P7-notes as listed there: real 486 speed, VBE lists, DAC width, SB variants, a real Korean keyboard, 8.3 truncation in MS-DOS and Win9x (M6), memory managers.

## Required fix list

1. Before merge: **M1**. Refuse, or make distinct, save names of two targets that share an 8.3 base; make rename/copy between aliasing names a refused no-op; add an isolation test.
2. Before the DOS release:
   - **M5**: silence the SB, OPL and MPU in `fatalSignal`.
   - **M2**: `0xD5`/`0xD9` or a reset at SB16 close.
   - **M3**: BLASTER H/D/I validation.
   - **M4**: round the SB Pro time constant and report the real rate.
   - **M6**: 8.3 engine data names, or state "LFN off only".
3. Recommended: the `extrapath` shadowing; `DosMutex` IF restore; `cli` around `ProgramDMA` and saving probe state; the Blend16 key-aliasing test; the Blend16 format assert; the 499,384,320 count fix.
