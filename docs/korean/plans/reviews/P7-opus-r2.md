# P7 fix round: re-review (Claude lane)

Scope:

- ScummVM `838b29d4ec3..f3a649e132c`: 17 code and docs commits, plus `54506d703cb` (the kept review reports).
- SDL3 `cc504b3b8..35cda1007`: `b82beef6c` and `35cda1007` (patches 0015 and 0016).
- Record checked against `P7-fixes.md`.

Method: everything was run, not only read. Scratch is under `~/work/tmp/p7-review-claude/`:

| Folder | What is in it |
|---|---|
| `re/` | HEAD tree and its ASan+UBSan build |
| `bis/`, `playmem/` | memory measurements |
| `sdl/` | my own SDL3 prefixes (old and new tip, min and full) |
| `sbmodel/` | SB models |
| `blend/` | Blend16 model |
| `paltest/` | palette-only test |
| `heap/` | `largeHeader` test |
| `seq2/` | per-commit host builds |

No worktree was configured, edited or committed. Nothing went to `/tmp`.

## Verdict

**P7 can merge into ko-2026.4. M1 is fixed and the fixes passed every run below.**

One **new major** must be fixed before the DOS release: N1, wildcard listings of mapped save names. It is a regression the M1 fix introduced.

M6 (8.3 engine data names, LFN) remains a release blocker, as recorded.

The −300 KB memory "regression" is **not a regression**. It compares a direct SCI.EXE run with a run under PLAY.

## Per-finding status (my round-1 findings, then the implementer's rejections)

| Finding | Status | Evidence (run here) |
|---|---|---|
| M1 save-name collisions, rename/copy data loss | **ADDRESSED** (see N1 for a regression it introduced) | My randomized model over the real `DOS::SaveFileManager` at HEAD (ASan+UBSan). 12,000 ops: save (compressed or not), load, exists, remove, list, rename, copy, new session. Targets share 8-char prefixes, contain dots, differ in case, and include 12 bytes of Hangul. Result: **0 consistency errors, 0 isolation violations** (round 1: 218). `lsl1sci-ko.001` now reads its own data. Rename/copy of names equal ignoring case is a no-op returning true; names that only map to the same file are refused. FNV-1a mapping: 200,000 random bases (ASCII and high bytes) against my independent Python implementation, **0 differences**. It lower-cases ASCII only with `(byte)` arithmetic, so it does not depend on locale or endianness and is the same on every run. Collision rate: 100,000 targets sharing one 4-char prefix gave 2,861 colliding names against ~2,977 expected from the birthday bound at 36^4. Idempotent on mapped and short bases. A new session lists names mapped back. |
| M2 SB16 close `0xDA` | **ADDRESSED** (reading) | `0xD5` then `0xD9`, then `0xD3`. The ScummVM DSP reset at close stays. Hardware-only. |
| M3 BLASTER A/I/D/H ranges | **ADDRESSED**; partly disagree on I2 (minor N3) | A is 0x200..0x2F0 in steps of 16, D ∈ {0,1,3}, H ∈ {5,6,7}, I ∈ 0..15 except 2. The high mask now uses `4 | (ch & 3)`. Smoke "audio ok" on SB16, SB Pro 2, SB2 and SB1 emulation. |
| M4 time constant / real rate | **ADDRESSED** | My model (ASan+UBSan): 768,000 cases (DSP 1.5/2.00/2.01/3.2 × mono/stereo × every rate 1..96,000 Hz), **0 bad**. Checked: OpenDevice → StartPlayback is idempotent (same TC and rate the second time), TC ≥ 6, no high-speed below DSP 2.01, the reported rate equals `1e6/(256−tc)/ch`, and the TC is within one step of the brute-force nearest. |
| M5 fatal signal leaves devices sounding | **ADDRESSED** (reading) | `fatalSignal` now calls `silenceAll()` plus a DSP reset and DMA masks taken from BLASTER (`dos-exit.cpp:310-366`). The ranges are checked and only port I/O is used. Hardware-only. |
| M6 LFN / 8.3 engine data | **NOT addressed (recorded)**: still a release blocker | README and P7-fixes item 16. |
| minor: in-tree test misses the slot-key mutation | **ADDRESSED** | Green dropped from the key: the new `test_keyHoldsEveryChannel` goes red with **13,141** mismatches. Restored: 6/6 OK. |
| minor: Blend16 format assert | **ADDRESSED** | `supports()` against my independent criterion over 6,718,464 (bpp, bits, shifts) formats: **0 mismatches**. My Blend16 model now covers coverage **0..255** on 9 formats: **77,720,645,328 checks, 0 bad** at -O2, and 5,243,072,208 checks, 0 bad under ASan+UBSan. |
| nit: 499,404,800 count | **ADDRESSED** | 5 × 6 × 254 × 65,536 = **499,384,320**, as now in the notes. |
| minor: `usage()` `#else` reached other ports | **ADDRESSED** | `#elif defined(DOS_DJGPP)`. |
| minor: transient `extrapath=DATA` | **ADDRESSED** | `--datadir=DATA`, so `DATA_PATH` is the last root. My KQ1 run at HEAD finds the maps. |
| minor: `DosMutex` `popfl` | **ADDRESSED** (reading) | `sti` only when the saved IF was set. |
| minor: probe disturbs other channels / no cli | **PARTLY ADDRESSED** | `ProgramDMA` now runs under `cli`. The probe runs only when the configured channel's count register does not move. **If the register does not move**, the old probe of 0/1/3 runs unchanged: other channels are programmed and left masked, with no restore (documented in the code). If it moves, it returns "open" without interrupts and logs "check BLASTER's I". Acceptable for a BLASTER that is wrong. |
| minor: IRQ ownership (MPU-401 shared, spurious 7/15) | **PARTLY ADDRESSED** | Only on SB16: mixer 0x82 with the MPU bit set and no DSP bit is EOI'd and ignored. Spurious IRQ 7/15 on pre-SB16 cards is still treated as the card's. Hardware-only. |
| minor: 64 KB DMA trick > 32 KB | **ADDRESSED** | My exhaustive model: every conventional segment 0..0x9FFF × every even length 2..65,536 = **1,342,177,280 cases, 0 crossings, 0 refusals** (the analytic existence check says none is avoidable). |
| minor: signed ring cursors | **ADDRESSED** | `Uint32`. My model: 8,000,000 random writer/reader steps that start within 4 rings of 2^32: **0 bad**. |
| nits: `DelayPrecise` units, ring unlock, `CLOCK$`, `isascii` before the cut, symlinked `$src` | **ADDRESSED** | `stage-data.py`: 15 tests OK. My hostile names (`straße`, `ﬀ…`, `ı`, `CLOCK$`, `A+B`, space, devices) are all refused now. |
| minor: in-place INI writes | **NOT addressed** | Accepted risk, as on the other 8.3 ports. |
| Implementer rejection 9: palette-only frames | **AGREE** | **Emulator test** (`paltest/PAL.EXE`, DOSBox-X `pentium_mmx`, SDL3 35cda1007, INDEX8 640×400): four palette-only `SDL_UpdateWindowSurfaceRects(w, NULL, 0)` calls each reach the DAC (read back through 0x3C7/0x3C9: 15,7,2 → 30,15,5 → 45,22,7 → 60,30,10, as wanted). This holds with the direct-FB hint on and off. The MI1 intro showed no palette-only change to look at (sprite sparkles only), so the dedicated test is the evidence. |
| Implementer rejection 10: heap header read | **AGREE** | Host test (ASan+UBSan) of the extracted `largeHeader()`: every byte offset of a page whose previous page is `PROT_NONE`, with zeroed, garbage and valid headers. 12,288 pointers, **0 faults**, exactly 1 recognised (offset `sizeof(LargeHeader)`). The offset test runs before the read, so the read stays in the pointer's page. |
| Implementer rejection: session-domain keys (`--set`) | **AGREE**, with a remark (minor N4) | |
| Implementer rejection: IRQ 9 cascade already unmasked | **DISAGREE on the reason, minor** | `DOS_HookInterrupt` unmasks master bit 2 only when the hooked IRQ is > 7. My point was BLASTER `I2`, which on an AT arrives as IRQ 9. The fix now *ignores* `I2`, so such a card has no digital audio (N3). |
| Implementer rejection: `RingCopyOut`/`ISR_GetDMAHalf` in the locked range | **AGREE** | `DOS_IRQ_CODE`. |
| Implementer rejection: FPU-emulator `fnsave` | No objection | Not a round-1 finding of mine. |

## Memory regression (−300 KB): answer

**No commit of the fix round causes it. It is a measurement artefact.** The 4,720 KB figure comes from direct `SCI.EXE` runs (run-b8 or run-kq1 style). The 4,412 KB figure (`~/work/tmp/p7/shots/fin-x.log.txt`, `accept/`) comes from the **PLAY.EXE two-zip layout**. There, PLAY stays resident as the parent under the same CWSDPMI, and the engine already starts with ~290 KB less: `engine dpmi_free` 8,660 against 8,944-8,976.

The implementer's own **pre-fix** PLAY-layout logs show the same level: `shots/g2-x-reply.log.txt`, 2026-11-24 01:39, `engine 8668`, `first-frame 4424`.

My measurements (DOSBox-X 16 MB, KQ1 `kq1aa`/`kq1-aa` RGB565; each pair of runs is deterministic):

| Layout | ScummVM | SDL3 | engine KB | first-frame KB |
|---|---|---|---:|---:|
| direct SCI.EXE | 838b29d4ec3 | cc504b3b8 | 8,944 | 4,692 |
| direct | 838b29d4ec3 | 35cda1007 | 8,940 | 4,688 |
| direct | f3a649e132c | cc504b3b8 | 8,948 | 4,700 |
| direct | f3a649e132c | 35cda1007 | 8,952 | 4,700 |
| PLAY | 838b29d4ec3 (my build) | 35cda1007 | 8,652 | 4,400 |
| PLAY | f3a649e132c (my build) | cc504b3b8 | 8,656 | 4,404 |
| PLAY | f3a649e132c (implementer's staged EXE) | | 8,664 | 4,412 |

So the fixes change first-frame memory by +8 to +12 KB, and no bisect was needed. The PLAY layout costs ~290 KB.

Against the 16 MB target and the 2,048 KB gate: 4.4 MB free is more than twice the gate. The worst case on record stays `rgb888` at 3,224 KB, measured direct. **Under PLAY that becomes ≈ 2.93 MB (estimate: 3,224 − 290), still above 2,048 KB.** The README memory section should state the PLAY overhead (~290 KB) and quote PLAY-layout numbers, since PLAY is how users run it.

## Other runs

- **Host, per commit:** all 17 states `838b29d4ec3..f3a649e132c` build and pass on Linux, incrementally (sci,scumm, -O0). Tests go 1000 → 1002 → 1003 → 1006 → 1008 → 1009, with 0 compiler warnings. The only failing test at every state, as at the round-1 base, is `HiResTextShippedMapsTestSuite::test_diskFiles`. It fails only in trees made with `git archive` and is not caused by P7.
- **HEAD under ASan+UBSan:** sci,scumm, 1009 tests, the same single environmental failure, **0 runtime errors, 0 ASan reports**.
- **`--set`:** 2 M random option strings against the split model, plus 500,000 control-key and near-miss keys (random case, `-`/`_`, a suffix) against my model of `isControlSetKey`/`normalizeSetKey`. ASan+UBSan, **0 differences**.
- **Smoke gates:** min and full prefixes of 35cda1007 built here, × DOSBox-X and Staging, all four "audio ok / timer 99 callbacks / done". The 640×400 and 640×480 format lists are as in round 1. Extra DOSBox-X runs with `sbtype` sbpro2, sb2 and sb1 are also "audio ok".
- **KQ1 RGB565 at HEAD** (my SCI.EXE f3a649e132c + SDL3 35cda1007, DOSBox-X): the `qhk` room message frame against Linux `aa-kq1-rgb565-message.png` has status rows **0** differing pixels. The message box differs in 380 pixels, all inside the cursor's box (320,200)-(342,232), so 0 outside it. I could not reproduce the md5 crop definitions behind bda24c37/1306e9e5/c7bfb6a1, so I compared pixels directly, which is stricter.
- **DOS builds:** SCI and SCUMM at HEAD, irqcheck clean.

## New findings

**N1 — major (regression from c4aa3e11b1a; host-reproduced): wildcard listings no longer find mapped names.**

- `dosPattern` leaves a base with a wildcard alone (`dos-savenames.h:79-80`), but the files on disk now carry hashed bases.
- So any engine listing with a wildcard in the base finds nothing:
  - SCI's `kFileIOFindFirst` on wrapped game files (`<target>-*.ext`, `engines/sci/engine/file.cpp:577-581`);
  - the QFG character import (`addAsVirtualFiles(..., "qfg1-*")`, `file.cpp:558-565`).
- Reproduction: real manager, new session, at HEAD against 838b29d4ec3. Files saved: `kq1-ko-glory1.sav`, `qfg1-glory1.sav`.

  | Listing | HEAD | Round-1 code |
  |---|---|---|
  | `kq1-ko-*.sav` | 0 | 1 (`kq1-ko-g.sav`) |
  | `qfg1-*` | 0 | 1 (`qfg1-glo.sav`) |
  | `*.sav` | `kq1-a8vo.sav`, `qfg14p4p.sav`: names the engine never wrote, so they unwrap to nothing | |

- The QFG1 → QFG2 character import is a real use for the Korean QFG2 on DOS.
- Fix: keep a small index file in the save folder (DOS name → engine name), written by `openForSaving` and rename/copy and pruned by remove, and match wildcard patterns against the engine names in it. Add the wildcard cases to the save test.
- Not a merge blocker, because round 1 was already lossy for these names. It must be fixed before the DOS release.

**N2 — minor (UNVERIFIED-ON-HARDWARE): non-ASCII and DOS-invalid bytes in mapped names.**

- `mappedBase` keeps the first 4 bytes raw (`dos-savenames.h:64-65`), and short bases keep every byte.
- A target that starts with Hangul gives `\xed\x95\x9c\xea` + hash. Characters such as `+ , ; = [ ]` and space pass through too.
- MS-DOS upper-cases some high bytes through the country table (CP437 e.g. 0x81 → 0x9A, 0x95 → 'O'). The name listed back then differs from ScummVM's ASCII-only case-insensitive match, so such saves disappear from new-session listings. DOS also rejects the invalid characters.
- ScummVM's generated targets are ASCII, so this is unlikely.
- Fix: map any byte outside `[a-z0-9_-]` to `_` in the prefix, and send bases containing such bytes through the hash path.

**N3 — minor: BLASTER `I2` is now ignored** (`SDL_dosaudio_sb.c:879-884`).

- An SB 1.x/2.0 jumpered to IRQ 2 is a real setting; on an AT it raises IRQ 9.
- With `I2` ignored, `soundblaster_irq` stays −1 and there is no digital audio. Before, `I2` hooked vector 0x0A, which works only through the BIOS IRQ 9 redirect.
- Fix: accept 2 as 9, hooking vector 0x71 and unmasking slave bit 1 plus cascade bit 2.

**N4 — minor: two spellings of one key can both reach the transient domain** (`base/commandLine.cpp:780-783` with `processSettings` 2370-2383).

- `--set` stores the key already normalised (`save_slot`), while the own option stores `save-slot`.
- Both are converted to `save_slot` in `processSettings`, and which one wins depends on HashMap iteration, not command-line order.
- For keys in `sessionSettings` (`music-volume`, …) the `--set` value always wins, because it lands in the transient domain. That is acceptable and should be documented.
- Remaining behaviour (no separate map applied after normal processing): acceptable for this round. Control keys are refused and nothing new is reachable.
- Fix (optional): store `--set` pairs in their own map and apply them last.

**N5 — minor (pre-existing, not introduced): IRQ0 procs can still reach DPMI through the small heap.**

- `0c146484276` stops large-block DPMI calls inside the IRQ0 handler.
- But `smallAlloc` → `__real_malloc` → `sbrk` grows the heap with a DPMI allocate when the arena is full, from a timer proc too.
- Fix (optional): pre-grow the sbrk arena at start, or refuse growth in `kProcsInIrq0` and defer it.

**N6 — nit: no-op rename semantics changed.** `renameSavefile("x.001", "X.001")` on a file that does not exist now returns **true** (round 1: false). SCI's `kFileIORename` would report success for a missing file. Fix: return `exists(old)` for the equal-name case.

**N7 — nit: `lockIrqCode` returns true after the fallback `lockText()`** (`dos-irq.cpp:107-109`) even if `lockText()` failed. So the timer can still install unlocked in that branch. Fix: make `lockText()` return its result.

## Required before the DOS release

- N1: wildcard listing of mapped names.
- M6: 8.3 engine data names, or state "LFN off only".
- README: memory numbers in the PLAY layout.

Recommended: N2, N3, N6, N7.

Hardware-only items stay on the UNVERIFIED-ON-HARDWARE list: M2, M5, the probe paths, IRQ ownership, the real SB Pro rates and N2.
