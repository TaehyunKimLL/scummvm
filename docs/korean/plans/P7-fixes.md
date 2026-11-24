# P7 review fixes

Reports: `reviews/P7-opus.md`, `reviews/P7-agy-{shared,dos,sdl3}.md`. Status words: VERIFIED-HOST (host test/ASan/model),
VERIFIED-EMULATOR (DOSBox-X/Staging run), FIXED-BY-READING-ONLY (hardware: fixed to datasheet semantics, stays on the
UNVERIFIED-ON-HARDWARE list), REJECTED (evidence). ScummVM commits are on `ko/p7-dos`, SDL3 on `dos-p7-1362030`
(patch files `backends/platform/dos/sdl3/0015`, `0016`).

| # | Finding | Status | Commit | Evidence |
|---|---|---|---|---|
| 1 | Opus M1, agy dos 2: 8.3 save collisions, rename/copy data loss | VERIFIED-HOST | c4aa3e11b1a | mapped names are first 4 chars + 4 base-36 digits of the FNV-1a hash of the lower-cased base (`kq1-WXYZ`), distinct per target; copy/rename of equal names no-op true, of colliding names false; multi-dot bases mapped with `_`; wildcard bases left alone. Real `DefaultSaveFileManager` over a folder: 33 random targets (shared prefixes, dots, case) x 3 slots with Hangul bytes, new-session listing/loading isolated, rename/copy cases; injectivity model over 300 targets x 28 slots (the plain cut: 5,208 collisions). Mutation (plain cut) red |
| 2 | agy shared 1: `hires_text_whole_kb` DOS default dead | VERIFIED-HOST | 26045413bb2 | `confManGet` used `hasKey()`; red test (read 256), green with `hasDefault()`; a set value still wins |
| 3 | agy shared 2: Blend16 guards | VERIFIED-HOST | 2e384c449f8 | `supports()`, constructor assert, `filled` flag instead of key 0 sentinel, driver builds the table only for supported 16-bit screens; every coverage 0..255 on fresh tables at black/white, `supports` cases, 32-bit table argument ignored; key mutation (green dropped) red: 13,141 mismatches. The exhaustive suite: **5 x 6 x 254 x 65,536 = 499,384,320** checks (the earlier notes' 499,404,800 was a miscount, 5 x 6 x 255), 0 mismatches. DOS KQ1 rgb565: status line and both boxes equal the Linux AA frame (md5 bda24c37 / 1306e9e5 / c7bfb6a1 = Linux), 0 differing pixels |
| 4 | agy shared 3: default-saves.o in TEST_LIBS with USE_CLOUD | FIXED-BY-READING-ONLY | 2f33d8c8f68 | linked and the manager cases compiled only without USE_CLOUD (no libcurl here to run the cloud branch) |
| 5 | agy dos 1 + Opus: mutex unlock popfl | FIXED-BY-READING-ONLY | 7d1c106fdd8 | `sti` when saved IF set; hardware/DPMI hosts with IOPL < CPL not available; DOSBox-X runs unchanged |
| 6 | agy shared 4: `--set` keys | VERIFIED-HOST (partly REJECTED) | 3bad97d02fb | control keys refused, key normalised; fuzz 20,000 texts vs a model. REJECTED part: "session-domain keys lose to the game domain": `--set` keys go to the transient domain (processSettings stores unknown keys there; already normalised there) |
| 7 | Opus rec., agy dos 5: extrapath shadowing | VERIFIED-EMULATOR | 64d70b37021 | `--datadir=DATA` (DATA_PATH is the last root); KQ1 two-zip run finds the default map, AA frame hash equals Linux. The notes' claim about ee7f90931dc is corrected here: it forced DATA first |
| 8 | Opus M5: signal/Ctrl-C leaves devices sounding | FIXED-BY-READING-ONLY | 53837778946 | `silenceAll()` plus Sound Blaster DSP reset/DMA mask from BLASTER (A/D/H range-checked); built, irqcheck clean; no real card |
| 9 | agy dos 3: palette-only frame | REJECTED | - | SDL3 `DOSVESA_UpdateWindowFramebuffer` programs the DAC before the `numrects <= 0` return ("useful for pure palette animations", src/video/dos/SDL_dosframebuffer.c:499-509) and `SDL_UpdateWindowSurfaceRects` only checks the window; not reproduced in DOSBox (the KQ1 title's fade ends at the right palette in every run) |
| 10 | agy dos 4: heap header reads | VERIFIED-BY-READING (partly REJECTED) | 0c146484276 | REJECTED: the read before the pointer is inside the pointer's page (offset test 0x20), so it cannot fault. FIXED-BY-READING: header must carry its own linear address/size; DPMI allocate/free are no longer made inside the IRQ0 handler (alloc falls back to sbrk, free queued) |
| 11 | agy dos 6: unlocked timer installed | FIXED-BY-READING-ONLY | f4d76e30d36 | `lockIrqCode` returns bool; install refuses. NOT CHANGED: fnsave/frstor under cli on FPU-emulated machines: the handler does not run procs there (`kProcsOnMainThread`), the main-thread fnsave runs the x87 emulator correctly; no fault evidence |
| 12 | agy dos 7: stage-data.py | VERIFIED-HOST | 62076899fd0 | names validated before the cut, CLOCK$, missing/non-folder root exit 1, symlinks refused, `#` comments; 15 tests incl. 20,000 random names vs a model |
| 13 | agy shared 5: [preload] bounds | VERIFIED-HOST | a4f8695589f | 64 KB chunk, BOM, # lines, glyph cap = block-cache slots, stop on SVF_E_IO with the face name; tests: BOM list and 300,000-byte newline-less list. The BOM test does not turn red without the BOM skip (U+FEFF is ignored by `has()` anyway): the skip is harmless hardening |
| 14a | agy shared 6: OPL3 reset order | FIXED-BY-READING-ONLY | ef2d80e147b | NEW set first, cleared after both banks are silent; per bank release, attenuation, key-off, rhythm |
| 14b | agy shared 7: MPU callback / sysEx log | FIXED-BY-READING-ONLY | bc3cec2a349 | replacement honoured; log skipped for length >= 32766 |
| 14c | agy shared 8: size() clears EOF | FIXED-BY-READING-ONLY | 8ef11a38843 | flag re-set after the seek pair; the stdio trick checked on the host (EOF 1 -> 0 -> 1) |
| 15 | Opus: usage() branch, Blend16 key mutation, miscount | VERIFIED-HOST | 3bad97d02fb, 2e384c449f8 | usage message DOS-only; key mutation test; count corrected (see 3) |
| 16 | Opus M6: LFN needs 8.3 engine data names | recorded, no code | this file | README (Memory and file names) and notes: an LFN-capable DOS does not truncate; the real fix is 8.3 names for `hires_text/` and `kDefaultMap` (ko/03-sci work item) |
| 17 | SB16 close 0xDA | FIXED-BY-READING-ONLY | SDL3 35cda1007 | 0xD5/0xD9; probe-failure/open-failure clears the SB Pro stereo bit; emulators (SB16/SBPro/SB2) open and close cleanly |
| 18 | BLASTER ranges | VERIFIED-EMULATOR | 35cda1007 | A/I/D/H validated; high-DMA mask uses `4 | (ch & 3)`. The old `4 | ch` for 5-7 already selected the right channel (not a bug) |
| 19 | time constant rounding/rates | VERIFIED-HOST + EMULATOR | 35cda1007 | model: 1,152,000 (DSP, channels, rate) cases, 0 bad (no tc < 6, no high speed below DSP 2.01, within one step of brute-force nearest); SB Pro now opens at 21,739 Hz (real rate reported), SB2 22,222 Hz |
| 20 | DelayPrecise units | VERIFIED-BY-READING | 35cda1007 | `SDL_DelayPrecise` takes nanoseconds (SDL_timer.h); the 300 ms wait was ~0.3 ms and the reset pulse 3 ns; real deadlines, pulse 20 us. Emulators unchanged |
| 21 | DMA probe | FIXED-BY-READING-ONLY | 35cda1007 | count register checked first (no probe when it moves), writes under cli; remaining risk documented in the code |
| 22 | clobbers, volatile, Uint32 cursors, stats | VERIFIED-HOST | b82beef6c, 35cda1007 | cursors model: 4,000,000 checks across the 2^32 wrap, 0 bad (ASan+UBSan, `sdl3/model-test.c`); stats read into locals under cli |
| 23 | DMA 64 KB, lock coverage, IRQ checks | VERIFIED-HOST (parts REJECTED) | b82beef6c, 35cda1007 | DMA region: 177,078,272 (segment, length) cases, 0 crossing, 0 avoidable refusals; ring unlocked in FreeState; MPU-only SB16 interrupts ignored (conservative: only when mixer 0x82 shows MPU and no DSP bit). REJECTED: RingCopyOut/ISR_GetDMAHalf are `DOS_IRQ_CODE` (already in the locked range); IRQ 9 cascade is already unmasked by `DOS_HookInterrupt` (SDL_dos.c) |

## Deviations from the review brief

- The SDL3 fixes are two commits (core, audio) rather than one per item: the audio items edit one file and interleave.
- Item 2 of C (palette) and some hardware items could not be reproduced; see the table.
- UBSan: the host test runner was rebuilt with `-fsanitize=address,undefined` (`$B/ubsan`).

# Second round (reviews/P7-opus-r2.md, P7-agy-{shared,dos,sdl3}-r2.md)

Memory: not a regression (Opus): 4,720 KB is the EXE started directly, 4,400-4,412 KB is the same run under PLAY.EXE (resident, ~290 KB); the first-round fixes
changed free memory by +8..+12 KB. The README has both.

| # | Finding | Status | Commit | Evidence |
|---|---|---|---|---|
| N1 | wildcard listings lost the hashed saves (SCI file search, QFG1 -> QFG2 import) | VERIFIED-HOST + EMULATOR | 632e52d32e8 | index `SAVEIDX.TXT` (dosname TAB enginename); a listing matches the pattern against engine names; entry valid only if file exists and `dosPattern(engine) == dos name`; torn/corrupt/stale lines dropped; lost index: exact names and concrete long patterns still work (wildcard listings lose engine names: documented limit). Real manager, new session: `*`, `*.sav`, `kq1-ko-*.sav`, `qfg1-*`, `?` patterns, Hangul bytes, deleted/corrupt index, stale entry. DOSBox-X: save under target `kq1-rgb565` writes `KQ1-U6F4.001` + `SAVEIDX.TXT` (`kq1-u6f4.001<TAB>kq1-rgb565.001`), a new session's F7 lists 한글 (`r2-restore.png`). NOT run in an emulator: a real SCI wildcard FindFirst (QFG1/KQ4 packs not driven to that point) |
| N2 | non-ASCII bytes in the first 4 characters of a mapped name | VERIFIED-HOST | 632e52d32e8 | every non-DOS character becomes `_` before the full-base hash; test: Hangul-led name maps to 12 ASCII characters |
| N6 | rename/copy of a missing file onto the same name returned true | VERIFIED-HOST | 632e52d32e8 | `exists(old)`; test |
| 2 | CWSDPMI lock bit per page: unlocking the ring could unlock a shared page | FIXED-BY-READING-ONLY | SDL3 a50037b96 | ring on 4 KB pages (`SDL_aligned_alloc`), whole pages locked/unlocked; other lock pairs: statics locked forever, DMA buffers are conventional memory, ScummVM heap blocks are page aligned and whole-page. Emulators: SB16/open/close unchanged (smoke x4) |
| 3a | handler left the SB16 mixer index at 0x82 | FIXED-BY-READING-ONLY | a50037b96 | index saved and restored around the status read |
| 3b | EOI sent while the vector is chained | DEFERRED | - | the normal path already sends the same single EOI and chains to the previous handler (the BIOS IRET stub for an unused line); making it one-EOI-exact needs `_go32_dpmi_allocate_iret_wrapper` instead of chaining in `DOS_HookInterrupt`, shared by the keyboard and timer hooks; no TSR-chained setup to test |
| 4 | BLASTER I2 ignored | FIXED-BY-READING-ONLY | a50037b96 | taken as IRQ 9 (DOS_HookInterrupt unmasks slave and cascade); documented in the code |
| 5 | MPU timer callback stores unguarded; test hygiene | VERIFIED-HOST | 72ce4b07d93, e73d9bb6468 | stores under the mutex; registered-default test guarded and its default put back |
| 6 | lockText result | FIXED-BY-READING-ONLY | 46a5d2523f6 | real result returned |
| R1 | standalone configure with Tremor lacks VORBIS_REFILL_SAMPLES | VERIFIED-HOST (reading) | 68fd1f8bb81 | defined in the msdosdjgpp block |
| R2 | agi edition codec check; DOS_MARCH in config_current | FIXED-BY-READING-ONLY | fdbc9903e51 | |
| R3 | `hires_text_whole_kb` blanks | VERIFIED-HOST | 07460554461 | `" 64 "` test |
| R4 | DOS_SBGetStats unconditional sti; counters reset at open | FIXED-BY-READING-ONLY | a50037b96 | IF restored; `isr_irq_count` reset |

Triage of the rest of the recommended list (not done this round, with reasons):
- PLAY.EXE argument/path/profile bugs (drive-absolute `--path=D:\...`, `--language` ordering, relative argv[0], `ensureProfile` timing, remove-before-rename): DEFERRED, not reproduced or fixed; each needs a launcher test (host suites `dos_play_*` exist) and a DOS run.
- `build-deps.sh` unquoted paths / hardcoded env.sh, unguarded `rm -rf` in the scripts: only the rm of `$out/DATA` in `build-dos.sh` stage exists and is guarded by the outside-source check; the rest DEFERRED.
- dos_mpu401 sysEx holding cli over the polling loop; `supportedFormats()` ignoring `dos_force_fallback`; palette bounds checks; `KBD_SCRL`; `startsGame()` option values; `g_lastError` under DISABLE_GUI; `fillStats()` interruptsOn(); MPU `portPresent()`/`queryVramWindow()`; `audio/rate.cpp` assert vs error; dead defines: DEFERRED (hardware-flavoured or cosmetic; none reproduced).
- STDIOSTREAM_NO_ATOMIC_SUPPORT in-place writes on DOS: accepted risk (8.3 FAT cannot create NAME.EXT.tmp; a power loss during a write can truncate SCUMMVM.INI or a save).
- SDL3 leftovers: `SDL_dosevents.c` volatile/static parser state, probed DMA channel not persisted, `handlers[]` coverage of `RingCopyOut`/`ISR_GetDMAHalf` (both are `DOS_IRQ_CODE`: inside the linker range by construction; `irqcheck` sb range grew 356 -> 468 bytes and reports 0 indirect exits), canary globals/C++ linkage, `CPU_haveCPUID` red zone, `DOS_LockIRQCodeRange`/LTO, non-atomic 16-bit DMA count read (error at most 256 words, harmless against the half-buffer threshold except at the boundary), SB Pro stereo phase: DEFERRED.
- Opus N4: `--set` beats session-domain keys, and the same key given through `--set` and its own option is hash-order dependent: DOCUMENTED here (use one). N5: an IRQ0 timer proc that grows the sbrk heap reaches DPMI (the sbrk path): KNOWN LIMIT. M6: 8.3 engine data names: RELEASE BLOCKER for LFN-capable DOS, record only (ko/03-sci work item).

# Third round (agy set aside by the user: items 4 and 5 stay OPEN)

| # | Item | Status | Evidence |
|---|---|---|---|
| 1 | patches 0017 missing in `sdl3/` | FIXED | the 0017 file had been written into the SDL3 tree by a relative path, and `git add -A` there had committed copies of 0015/0016 into two SDL3 commits; SDL3 `14371af34` removes them (0018 exported). Verified: `git am` of 0001..0018 on the pinned base `1362030` gives a tree identical to `14371af34` (`git diff` empty) |
| 2 | `listSavefiles("short.save1")` listed `short.sav` too | VERIFIED-HOST | the concrete-pattern fallback skips names the index maps validly; test with >3-char extensions; mutation (fallback unguarded) red |
| 3 | hash-collision guard | VERIFIED-HOST | a real collision (two names of one prefix with one DOS name, found by search) is refused by `openForSaving` in the same and in a new session, the owner can still save, A still loads; mutation (guard off) red |
| 4 | `DOS_MARCH=i486` for C files / toggling not detected | OPEN | not done (agy-only) |
| 5 | SB16 invalid H must keep the 8-bit D fallback; keyboard ISR state volatile/reset | OPEN | not done (agy-only) |

# Follow-up on `ko/p7-agy-followup` (agy r2 findings; agy is a gating lane again)

| Finding | Status | Evidence |
|---|---|---|
| agy dos #3: `DOS_MARCH=i486` omits CFLAGS | REJECTED (partly) | the svf C objects take `$(CXXFLAGS)` minus C++-only flags (`graphics/module.mk:110`); a `DOS_MARCH=i486` build's `make -n` for `svf_face.o` shows `-march=i486` |
| agy dos #3: i486 -> Pentium toggle not detected | FIXED-BY-READING | `config_current()` also refuses a directory whose SAVED_CXXFLAGS has `-march=i486` when no march is asked (not run: a toggle run was blocked by the sandbox) |
| agy sdl3 r1 #3: SB16 with missing/invalid H fails to open | FIXED-BY-READING | DSP 4.x with no `H` plays 8-bit on `D` (SDL3 c9a0e91ee); smoke x4 and KQ1 (SB16 with H) still open `DMA 5` |
| agy sdl3 r1 #10: keyboard ISR ring/parser state | VERIFIED-EMULATOR | volatile ring and cursors, parser state at file scope and reset in `DOSVESA_InitKeyboard` (SDL3 8ba0dec5a); irqcheck kbd range 36 -> 52 bytes, 0 indirect exits; DOSBox-X: F9 (mapped Han/Yeong) toggles twice and the typed `qhk` frame hashes equal the Linux AA frame |
| agy sdl3 #2 (r1): bare reset on open, probed DMA channel not kept, stats stale | FIXED-BY-READING | open uses `CheckForSoundBlaster()` (waits 0xAA); probed channel stored; counters cleared on close (SDL3 c9a0e91ee) |
| agy sdl3 new: patch files committed in SDL3 | ALREADY FIXED | SDL3 14371af34 removed them; `git am` of `sdl3/0001..0020` reproduces SDL3 `c9a0e91ee` exactly (tree diff empty) |
| agy dos #3 (r2): `inIrqHandler` misses AfterCall/IF=0; `g_pendingFree` waits for a large call | FIXED-BY-READING | any timer-proc context or IF=0 counts; pending blocks are returned on the next malloc/free outside such contexts |
| agy dos #2 (r2): `getenv`/`strtoul` in the signal handler | FIXED-BY-READING | BLASTER parsed once in `installExitSignals()`; the handler does port I/O only. NOT done: using SDL's probed DMA channel (the handler cannot see SDL's statics) |
| agy dos #9: stale 8.3 comments | FIXED | README names the hashed save file; test comment says 4 characters |
| agy sdl3: double EOI on a chained vector, DMA probe reading the 8237 DREQ bits, `handlers[]`/lock short-circuit, canary globals and `extern "C"`, CPUID red zone, LTO pointer arithmetic, SB Pro stereo phase, 16-bit DMA count tearing | DEFERRED | needs a non-chaining hook or hardware to check; see round-2 triage |
| agy dos: PLAY.EXE path/argument bugs, `supportedFormats()` vs `dos_force_fallback` | DEFERRED | not reproduced here; each needs its own host test |
| agy shared: `--set` vs the 14 `sessionSettings` keys, "engine" control key, `$_pkgconfig` in configure, `default-saves.o` gating on POSIX/WIN32 | DEFERRED | none simple to verify on this host |
