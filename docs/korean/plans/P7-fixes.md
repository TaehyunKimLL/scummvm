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
