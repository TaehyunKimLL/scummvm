# P7 round 3: re-review (Claude lane)

**Scope**

- ScummVM `f3a649e132c..1d3a4d25206` (9 commits).
- SDL3 `35cda1007..a50037b96` (1 commit).
- `P7-fixes.md`, second half.

**What this report contains**

As asked, it lists only regressions and blockers, plus a ruling on each deferral and the evidence behind the round-2 items.

**Method and constraints**

- Scratch work is under `~/work/tmp/p7-review-claude/`:
  - `re/`: the tree at 1d3a4d25206 and its ASan+UBSan build;
  - `keep/dos_zz_r3.h`: my test;
  - `sdl/prefix-a50037b96`: the SDL3 prefix;
  - `smoke/`, `bis/`, `seq3/`: emulator runs and the per-commit builds.
- Nothing was written in a worktree except this file. Nothing went to `/tmp`.

## Verdict

**ko/p7-dos may merge into ko-2026.4.**

- **No regressions:** none found in the 10 new commits.
- **Blockers:**
  - No merge blocker.
  - Release blocker M6 (8.3 engine data names for LFN-capable DOS) is unchanged and remains a blocker for the DOS release only.
- **Deferrals:** every deferral is acceptable for the merge. Two need a check before the DOS release (see the deferrals section).

## Round-2 items, checked by running

### N1: wildcard listings through `SAVEIDX.TXT` (632e52d32e8)

**Addressed.**

My own randomized model test (`keep/dos_zz_r3.h`) drove the real `DOS::SaveFileManager` under ASan+UBSan:

- **Volume:** 12,000 operations over 30 rounds: save (compressed or not), load, remove, rename, copy, new session and listings.
- **Engine names used:**
  - save slots `t.nnn`;
  - SCI wrapped files `t-glory1.sav` and `t-memory.drv`;
  - a QFG1 target `qfg1` with a `qfg2ko` reader;
  - targets sharing 8 characters, a dotted target, `a+b`, `ab cd`, and a Hangul target.
- **Patterns:** `t.###`, `t-*`, `t-*.sav`, `qfg1-*` (the QFG import) and `*`.
- **Index damage:** 314 damage events between sessions:
  - truncation mid-line;
  - appended garbage, NUL-free control bytes, a lone tab, a line without a tab;
  - a stale line pointing at another target;
  - a line whose DOS name does not match its engine name;
  - a deleted index.

Results with an intact index:

| Check | Result |
|---|---:|
| consistency (load, remove, rename, copy) | 0 errors |
| wrong or ghost names listed | 0 |
| matching names a listing missed | 0 |
| including `qfg1-*` | 0 |

After damage:

- Wildcard listings lose engine names. There were 4,332 misses, and 4,038 entries came back under their DOS names (for example `kq1-u6f4.001`).
- Concrete long patterns still find every save. The only "miss" I counted was my test misreading `lsl1.vga-ko-*` as concrete.
- No crash and no sanitizer report.

This is the documented limit: a damaged index degrades wildcard listings to the round-2 behaviour, and nothing is lost.

**Hash collision.** I used a real colliding pair found by brute force: `lsl1sci-5088` and `lsl1sci-10192` both map to `lsl1b1uu`.

- The second target's save silently replaces the first.
- The index then names the second target.
- The first target's concrete listing still shows a slot, which now loads the other target's data.
- Probability: 1 in 1,679,616 per pair of targets sharing their first 4 characters.
- Not a regression (round 2 behaved the same). Now that the index exists, a cheap guard is possible: in `openForSaving`, refuse when the index maps the DOS name to a different engine name, ignoring case.
- Recommended, not a blocker.

The in-tree suites pass under ASan+UBSan. `USE_CLOUD` is off here, so the save-manager cases ran.

### N2: non-DOS characters become `_` before the hash

**Addressed.** Hangul, `+`, and space targets all map to ASCII in my run. No consistency or isolation errors.

### N6: a rename or copy onto the same name now returns `exists(old)`

**Addressed.** It is part of the model's rename/copy checks: 0 errors.

### SB ring on its own pages, IRQ 2 → 9, mixer index restore (a50037b96)

**Addressed.** I rebuilt the SDL3 prefix from a50037b96 myself. It matches the exported tree.

Smoke gates, DOSBox-X: SB16, SB Pro 2, SB2 and SB16 at IRQ 10 all reached "first interrupt seen" and "audio ok", with 99 timer callbacks. Staging passed the plain smoke gate ("audio ok", 99 callbacks); its log was not captured.

IRQ 2 → 9, emulator-verified:

- Card on IRQ 9 with `SET BLASTER=A220 I2 D1 H5 T6`: log "BLASTER I2 taken as IRQ 9", "irq=9", "first interrupt seen" (DMA 5 on SB16, DMA 1 on SB Pro 2).
- DOSBox-X ignores `irq=2` in its own configuration, which is why the card was put on 9.

Ring:

- `alloc_size = (ring_size + 4095) & ~4095` with 4 KB alignment, so lock and unlock cover the same whole pages.

Mixer index save/restore:

- It is harmless even if base+4 does not read back on some card: the main thread always writes the index before the data.
- No main-thread SB16 mixer access during playback exists today.
- Hardware-only.

### Other changes in the round

All are correct by reading:

- `lockText()` now returns its result.
- The MPU timer callback is replaced under the mutex.
- `VORBIS_REFILL_SAMPLES` is defined only in the DJGPP configure block.
- `whole_kb` is trimmed.

### Builds and emulator runs

- **Per commit:** all 9 states build and test on the Linux host (sci,scumm, -O0). Tests 1009 → 1010, 0 warnings. The only failure is the environment-only `HiResTextShippedMapsTestSuite::test_diskFiles`, the same at every state since the base.
- **HEAD under ASan+UBSan:** 1010 tests, 0 sanitizer reports.
- **DOS SCI.EXE at 1d3a4d25206** with SDL3 a50037b96:
  - irqcheck clean; the sb range grew from 356 to 468 bytes with 0 indirect exits;
  - KQ1 RGB565 status row and message box: 0 differing pixels against the Linux AA frame outside the cursor;
  - first frame 4,688 KB free when run directly (4,700 KB in round 2; −12 KB from the page-aligned ring and the index).

## Deferrals

| Deferred item | Ruling |
|---|---|
| 3b: one EOI while the vector is chained | Acceptable. Behaviour is unchanged; it needs an IRET wrapper shared with the keyboard and timer hooks and a chained setup to test. |
| PLAY.EXE argument, path and profile bugs | Acceptable for the merge (not reproduced). **Check before the DOS release:** PLAY is the user's entry point, and the drive-absolute `--path` case needs one DOS run. |
| `build-deps.sh` quoting, `rm -rf` | Acceptable. The only real `rm -rf` (stage `$out/DATA`) is guarded. |
| MPU-401 sysEx keeping `cli` over the polling loop | Acceptable for the merge. **Check before the DOS release** with an MT-32/GM setup: a long sysEx such as an MT-32 init holds interrupts off for tens of ms. IRQ0 ticks are caught up by the TSC logic, and the SB ring of five chunks should cover it. Hardware-only. |
| `supportedFormats()` vs `dos_force_fallback`, and the cosmetic or hardware-flavoured list | Acceptable. |
| SDL3 leftovers (parser-state volatility, probe not persisted, non-atomic 16-bit DMA count read, SB Pro stereo phase, …) | Acceptable. All are hardware-only or cosmetic, and none is reproducible in the emulators. |
| N4 (`--set` against own option, documented), N5 (sbrk growth inside IRQ0, known limit) | Acceptable. |
| M6: 8.3 engine data names | Remains a **DOS release blocker** (ko/03-sci work item), not a merge blocker. |
