### Status of Round 1 Findings

#### Critical Findings (Round 1)
1. **`dos-mutex.cpp` `popfl` hangs on `IOPL < CPL` DPMI hosts** — **Addressed**
   - [dos-mutex.cpp:44-50](file:///home/thkim/work/tmp/scummvm-review-p7/backends/mutex/dos/dos-mutex.cpp#L44-L50) now restores interrupts via `if (g_savedFlags & 0x200) __asm__ __volatile__("sti" : : : "memory");`. (Minor residual: `g_lockDepth == 0` is still checked without `cli`, safe only for balanced calls.)
2. **8.3 savefile collisions, destructive `renameSavefile()`/`copySavefile()`, and `dosPattern()` edge cases** — **Addressed** (with one minor edge case noted below)
   - [dos-savenames.h:44-103](file:///home/thkim/work/tmp/scummvm-review-p7/backends/platform/dos/dos-savenames.h#L44-L103) replaces plain truncation with `mappedBase()` (first 4 chars with `.`→`_` + 4 base-36 FNV-1a digits), guards `*`/`?`/`#` symmetrically via `hasWildcard()`, and handles multi-dot bases.
   - [dos-savefile.h:134-148](file:///home/thkim/work/tmp/scummvm-review-p7/backends/platform/dos/dos-savefile.h#L134-L148) overrides `renameSavefile()`/`copySavefile()` to no-op equal names and refuse colliding 8.3 pairs.
   - Verified by randomized 300-target injectivity/invertibility and real `DefaultSaveFileManager` 33-target isolation tests in [dos_savenames.h:47-181](file:///home/thkim/work/tmp/scummvm-review-p7/test/backends/dos_savenames.h#L47-L181).
3. **Palette-only frames passing `(nullptr, 0)` to `SDL_UpdateWindowSurfaceRects()`** — **Withdrawn (implementer rejection valid)**
   - Confirmed against SDL3's `SDL_UpdateWindowSurfaceRects()` contract: it validates only the window/surface and delegates directly to `UpdateWindowFramebuffer`, where `DOSVESA_UpdateWindowFramebuffer` programs the VGA DAC before returning on `numrects <= 0`.
4. **`dos-heap.cpp`: OOB header read + DPMI memory calls from ISR** — **Partially addressed**
   - *Header read*: Implementer's page-fault rejection is **valid on x86**—because `((uintptr)ptr & 4095) == 32` guarantees `ptr - 32` lies at offset `0` of the **same** 4 KB page as `ptr`, it cannot fault into an unmapped previous page; [dos-heap.cpp:119](file:///home/thkim/work/tmp/scummvm-review-p7/backends/platform/dos/dos-heap.cpp#L119) also hardens validation by checking `h->linear` and `h->blockSize`.
   - *DPMI calls from ISR*: [dos-heap.cpp:135-210](file:///home/thkim/work/tmp/scummvm-review-p7/backends/platform/dos/dos-heap.cpp#L135-L210) guards `kProcsInIrq0` via `inIrqHandler()` and queues deferred frees in `g_pendingFree`, but misses `kProcsAfterCall` / mutex-locked callers (`IF == 0`) and can leak `g_pendingFree` (see New Breakage #3 below).

#### Important Findings (Round 1)
5. **`extrapath=DATA` in `kTransientDomain` shadowing user `SCUMMVM.INI`** — **Addressed**
   - Removed from [dos.cpp:219-227](file:///home/thkim/work/tmp/scummvm-review-p7/backends/platform/dos/dos.cpp#L219-L227) and replaced with `--datadir=DATA` in [build-dos.sh:150-152](file:///home/thkim/work/tmp/scummvm-review-p7/backends/platform/dos/build-dos.sh#L150-L152) so `DATA_PATH` is searched last in [hires_files.cpp:52-54](file:///home/thkim/work/tmp/scummvm-review-p7/graphics/hires_text/hires_files.cpp#L52-L54).
6. **IRQ0 timer unlocked install + `fnsave`/`frstor` under `cli` on `kProcsOnMainThread`** — **Partially addressed**
   - [dos-irq.cpp:190-206](file:///home/thkim/work/tmp/scummvm-review-p7/backends/platform/dos/dos-irq.cpp#L190-L206) and [dos-timer.cpp:678-686](file:///home/thkim/work/tmp/scummvm-review-p7/backends/timer/dos/dos-timer.cpp#L678-L686) now check `lockIrqCode()` and `lockIrqData()` before installing IRQ0 (small fallback hole in `lockText()` noted below).
   - `fnsave`/`frstor` in `runDeferredProcs()` for `kProcsOnMainThread` was left unchanged ([dos-timer.cpp:324](file:///home/thkim/work/tmp/scummvm-review-p7/backends/timer/dos/dos-timer.cpp#L324), [dos-timer.cpp:510](file:///home/thkim/work/tmp/scummvm-review-p7/backends/timer/dos/dos-timer.cpp#L510)); acceptable as lower-priority since `runDeferredProcs()` runs on the main thread rather than preempting `emu387`.
7. **`stage-data.py` pre-truncation validation, `CLOCK$`, missing roots/symlinks, `#` comments** — **Addressed**
   - Fixed in [stage-data.py:15-105](file:///home/thkim/work/tmp/scummvm-review-p7/backends/platform/dos/stage-data.py#L15-L105) with a 20,000-name randomized model test and root/symlink/`#` tests in [test_stage_data.py:42-155](file:///home/thkim/work/tmp/scummvm-review-p7/backends/platform/dos/test_stage_data.py#L42-L155).
8. **`PLAY.EXE` bugs (backslashes on drive-absolute `--path=D:\...`, `--language` checked before clearing `gameDir` / ignoring `--path=`, relative `argv[0]`, premature `ensureProfile()`)** — **Not addressed**
   - [play-home.h:97-115](file:///home/thkim/work/tmp/scummvm-review-p7/backends/platform/dos/launcher/play-home.h#L97-L115) and [play.cpp:404-574](file:///home/thkim/work/tmp/scummvm-review-p7/backends/platform/dos/launcher/play.cpp#L404-L574) are unchanged.
9. **`build-dos.sh` / `build-deps.sh` (`DOS_MARCH=i486` omitting `CFLAGS` & `config_current()`, missing `agi` no-codec check, unguarded `rm -rf`, unquoted paths, hardcoded `env.sh`)** — **Not addressed**
   - Only `--datadir=DATA` was added to [build-dos.sh](file:///home/thkim/work/tmp/scummvm-review-p7/backends/platform/dos/build-dos.sh); lines 22, 43, 72-73, 158-209 are unchanged.
10. **`dos_mpu401.cpp` (`cli` held across full SysEx + 15-bit log length overflow)** — **Partially addressed**
    - Log length overflow fixed at [dos_mpu401.cpp:357](file:///home/thkim/work/tmp/scummvm-review-p7/backends/midi/dos_mpu401.cpp#L357) (`length + 2 >= 0x8000`).
    - Holding `Common::StackLock` (`cli`) across the entire `sysEx()` polling loop at [dos_mpu401.cpp:351-352](file:///home/thkim/work/tmp/scummvm-review-p7/backends/midi/dos_mpu401.cpp#L351-L352) is **not addressed**.
11. **`dos-modes.h` `supportedFormats()` ignoring `dos_force_fallback=true` + palette bounds checks** — **Not addressed**
    - [dos-modes.h:91-106](file:///home/thkim/work/tmp/scummvm-review-p7/backends/platform/dos/dos-modes.h#L91-L106) and [dos-graphics.cpp:334-343](file:///home/thkim/work/tmp/scummvm-review-p7/backends/graphics/dos/dos-graphics.cpp#L334-L343), [dos-graphics.cpp:1147-1151](file:///home/thkim/work/tmp/scummvm-review-p7/backends/graphics/dos/dos-graphics.cpp#L1147-L1151) are unchanged.
12. **SDL3 patch 0007 DMA channel bounds + `DOS_SBGetStats()` unconditional `sti`** — **Partially addressed**
    - DMA/port/IRQ bounds checking added in [0016-dos-Harden-the-Sound-Blaster-driver-units-rates-rang.patch](file:///home/thkim/work/tmp/scummvm-review-p7/backends/platform/dos/sdl3/0016-dos-Harden-the-Sound-Blaster-driver-units-rates-rang.patch) (`IsSoundBlasterPresent()`).
    - `DOS_SBGetStats()` still executes unconditional `DOS_EnableInterrupts()` (`sti`), and [dos-mixer.cpp:277-289](file:///home/thkim/work/tmp/scummvm-review-p7/backends/mixer/dos/dos-mixer.cpp#L277-L289) (`fillStats()`) does not check `interruptsOn()`.
13. **In-place `SCUMMVM.INI` writes (`STDIOSTREAM_NO_ATOMIC_SUPPORT`) / `PLAY.EXE` `remove` before `rename`** — **Not addressed**
    - [stdiostream.cpp:50-53](file:///home/thkim/work/tmp/scummvm-review-p7/backends/fs/stdiostream.cpp#L50-L53) and [play.cpp:187-200](file:///home/thkim/work/tmp/scummvm-review-p7/backends/platform/dos/launcher/play.cpp#L187-L200) are unchanged.

#### Minor Findings (Round 1)
- **Minor 1 (`Blend16` guards / stack usage)** — **Addressed** ([text_compose.h:70-88](file:///home/thkim/work/tmp/scummvm-review-p7/graphics/hires_text/text_compose.h#L70-L88), [text_compose.cpp:73-107](file:///home/thkim/work/tmp/scummvm-review-p7/graphics/hires_text/text_compose.cpp#L73-L107), [upscaled.cpp:200-202](file:///home/thkim/work/tmp/scummvm-review-p7/engines/sci/graphics/drivers/upscaled.cpp#L200-L202), [hires_text_blend16.h:104-156](file:///home/thkim/work/tmp/scummvm-review-p7/test/graphics/hires_text_blend16.h#L104-L156)).
- **Minor 2 (`dos-events.cpp` missing `KBD_SCRL`)** — **Not addressed** ([dos-events.cpp:86-94](file:///home/thkim/work/tmp/scummvm-review-p7/backends/events/dos/dos-events.cpp#L86-L94)).
- **Minor 3 (`dos-loading.cpp` `startsGame()` option values)** — **Not addressed** ([dos-loading.cpp:205-234](file:///home/thkim/work/tmp/scummvm-review-p7/backends/platform/dos/dos-loading.cpp#L205-L234)).
- **Minor 4 (`dos.cpp` `g_lastError` gated on `DISABLE_GUI`)** — **Not addressed**.
- **Minor 5 (SDL3 patch trailers)** — Ignored per note.
- **Minor 6 (Host tests)** — **Partially addressed** (`dos_savenames.h` and `test_stage_data.py` thoroughly upgraded; `dos_play_home.h` and `dos_modes.h` unchanged).

---

### New Breakage & Regressions Introduced by the Fix Round

#### Critical

1. **`DOS_UnlockData(*hidden->ring_buffer, hidden->ring_size)` can unlock adjacent locked IRQ memory on CWSDPMI r7**
   - **Location**: [0015-dos-Place-the-DMA-buffer-inside-one-64-KB-page-for-a.patch](file:///home/thkim/work/tmp/scummvm-review-p7/backends/platform/dos/sdl3/0015-dos-Place-the-DMA-buffer-inside-one-64-KB-page-for-a.patch) (`DOS_UnlockRange`, `SDL_dos.h:DOS_UnlockData`), [0016-dos-Harden-the-Sound-Blaster-driver-units-rates-rang.patch](file:///home/thkim/work/tmp/scummvm-review-p7/backends/platform/dos/sdl3/0016-dos-Harden-the-Sound-Blaster-driver-units-rates-rang.patch) (`DOSSOUNDBLASTER_FreeState`)
   - **Failure scenario**: `hidden->ring_buffer` is allocated from the `sbrk` heap via `SDL_malloc()` (not 4 KB page-aligned). On CWSDPMI r7 (and DPMI 0.9 hosts without per-page lock reference counts), `PT_LOCKED` is a **1-bit flag per 4 KB PTE** (not a counter, contradicting the comment `// (the DPMI host counts locks)` in patch 0015). Calling `__dpmi_unlock_linear_region` on `ring_buffer` clears the lock bit on the entire first and last 4 KB pages it overlaps—silently unlocking any other locked `sbrk` allocations sharing those boundary pages (such as DJGPP's `malloc`'d IRQ0/keyboard IRET wrappers and 64 KB interrupt stack, or all locked heap pages under `DOS::lockAll()`). After audio close/reopen/suspend, the next hardware interrupt in real mode can hit a fatal page fault.
   - **Fix**: Only unlock `ring_buffer` if it is allocated as dedicated whole 4 KB pages (e.g., `memalign(4096, roundUp4096(ring_size))` and never unlocked when global memory locking is active), or keep it locked until process exit as DJGPP's `_go32_dpmi_lock_data` intends.

#### Important

2. **`silenceSoundBlaster()` in `fatalSignal()` is not async-signal-safe and can poke the wrong DMA channel / absent hardware**
   - **Location**: [dos-exit.cpp:36-88](file:///home/thkim/work/tmp/scummvm-review-p7/backends/platform/dos/dos-exit.cpp#L36-L88) (`fix-r1.diff:36-89`)
   - **Failure scenario**:
     1. `silenceSoundBlaster()` calls `getenv("BLASTER")` and `strtoul()` inside a `SIGSEGV`/`SIGFPE`/`SIGINT` handler.
     2. It ignores whether SDL3 actually opened a Sound Blaster (poking `base + 6` and masking DMA even when running with `NullMixerManager` or no card) and uses `BLASTER`'s `D` channel rather than the channel SDL3's DMA probe (`0007`) actually switched to—leaving the probed DMA channel unmasked and active on cards whose `BLASTER` `D` is wrong.
   - **Fix**: Record `sbOpened`, `sbPort`, and the actual active `sbDmaChannel` (8-bit or 16-bit) in static `volatile` globals when SDL3 logs its opened/probed SB config (extending `DOS::noteSoundBlasterConfig()`), and use only those globals + port I/O in `silenceSoundBlaster()`.

3. **`dos-heap.cpp`: `inIrqHandler()` misses `kProcsAfterCall` / mutex-held (`IF == 0`) callers, and `g_pendingFree` leaks ≥ 256 KB blocks until another large call**
   - **Location**: [dos-heap.cpp:135-158](file:///home/thkim/work/tmp/scummvm-review-p7/backends/platform/dos/dos-heap.cpp#L135-L158), [dos-heap.cpp:189-210](file:///home/thkim/work/tmp/scummvm-review-p7/backends/platform/dos/dos-heap.cpp#L189-L210)
   - **Failure scenario**:
     1. `inIrqHandler()` checks only `DosTimerManager::procsContext() == kProcsInIrq0`. When a deferred timer proc runs in `kProcsAfterCall` (`runDeferredProcs()`, interrupts off) or any main-thread caller holds a `Common::Mutex` (`IF == 0`), `inIrqHandler()` returns `false` and calls `__dpmi_allocate_memory()` / `__dpmi_free_memory()` while interrupts are disabled (which can cause CWSDPMI to re-enable interrupts during paging/DPMI).
     2. `releasePending()` is only invoked from `largeAlloc()` and `largeFree()`. A ≥ 256 KB block freed inside IRQ0 stays queued on `g_pendingFree`—leaking that DPMI memory—indefinitely if no subsequent ≥ 256 KB allocation or free occurs.
   - **Fix**: Check `DosTimerManager::procsContext() != DosTimerManager::kProcsNone || !DosTimerManager::interruptsEnabled()` in `inIrqHandler()` (rename e.g. `cannotCallDpmi()`), and drain `g_pendingFree` in `__wrap_malloc` / `__wrap_free` whenever `g_pendingFree && !cannotCallDpmi()` (one cheap branch when empty).

4. **SDL3 patch `0016`: `SoundBlasterIRQHandler` clobbers the SB16 mixer index port `base + 4`**
   - **Location**: [0016-dos-Harden-the-Sound-Blaster-driver-units-rates-rang.patch](file:///home/thkim/work/tmp/scummvm-review-p7/backends/platform/dos/sdl3/0016-dos-Harden-the-Sound-Blaster-driver-units-rates-rang.patch) (`fix-r1.diff:585-592`)
   - **Failure scenario**: On every SB16 IRQ, the handler writes `0x82` to `isr_mixer_base` (`base + 4`) without saving and restoring the previous mixer index register. Any foreground code reading/writing another mixer register (`base + 4` followed by `base + 5`) that is preempted by an SB16 IRQ will read/write register `0x82` instead. (When bit 2 is set without bits 0–1, it also sends EOI without acknowledging or chaining the MPU-401 IRQ.)
   - **Fix**: Save `const Uint8 saved_idx = DOS_InPort8(isr_mixer_base);` before writing `0x82` and restore `DOS_OutPort8(isr_mixer_base, saved_idx);` immediately after reading `isr_mixer_base + 1`.

5. **Uninvestigated `-300 KB` KQ1 RGB565 free-memory regression (4,720 KB → 4,412 KB)**
   - **Location**: Reported in `fix-r1-note.md`; prime code-level suspects in this diff are:
     1. [hires_options.cpp:40-46](file:///home/thkim/work/tmp/scummvm-review-p7/graphics/hires_text/hires_options.cpp#L40-L46) (`26045413bb2`): fixing `confManGet()` to check `ConfMan.hasDefault()` made DOS's `hires_text_whole_kb=64` live for the first time (previously dead, running at 256 KB), switching any 65–256 KB faces from whole to streamed (each streamed face holds a 65 KB block cache + tables + stdio buffer).
     2. [build-dos.sh:150-152](file:///home/thkim/work/tmp/scummvm-review-p7/backends/platform/dos/build-dos.sh#L150-L152) (`64d70b37021`): `--datadir=DATA` sets `DATA_PATH="DATA"` where it previously pointed to nonexistent `/usr/local/share/scummvm`, potentially causing an additional `FSDirectory` tree/cache or asset scan under `DATA`.
     3. [font_set.cpp:95-97](file:///home/thkim/work/tmp/scummvm-review-p7/graphics/hires_text/font_set.cpp#L95-L97) (`a4f8695589f`): `Common::Array<byte> text(64 * 1024)` allocates 64 KB from `sbrk` (`< 256 KB`), which DJGPP never returns to DPMI.
   - **Fix**: Bisect and verify the exact commit causing the `-308 KB` drop (cannot run builds in this read-only review) and confirm whether `hires_text_whole_kb` threshold or `DATA_PATH` needs tuning.

#### Minor

6. **`SaveFileManager::renameSavefile(a, a)` / `copySavefile(a, a)` returns `true` for nonexistent files**
   - **Location**: [dos-savefile.h:134-148](file:///home/thkim/work/tmp/scummvm-review-p7/backends/platform/dos/dos-savefile.h#L134-L148)
   - **Failure scenario**: Calling `renameSavefile("missing.001", "missing.001")` returns `true` instead of `false` (diverging from `DefaultSaveFileManager`).
   - **Fix**: Return `exists(oldName)` instead of `true` when `oldName.equalsIgnoreCase(newName)`.

7. **`lockIrqCode()` fallback ignores `lockText()` failure**
   - **Location**: [dos-irq.cpp:94-97](file:///home/thkim/work/tmp/scummvm-review-p7/backends/platform/dos/dos-irq.cpp#L94-L97), [dos-irq.cpp:194-199](file:///home/thkim/work/tmp/scummvm-review-p7/backends/platform/dos/dos-irq.cpp#L194-L199)
   - **Failure scenario**: If `!inside` triggers `lockText()` and `lockLinear()` fails, `lockIrqCode()` still returns `true`, allowing `DosTimerManager::install()` to hook IRQ0 on unlocked code.
   - **Fix**: Make `lockText()` return `bool` and `return lockText();` in `lockIrqCode()`.

8. **`PosixIoStream::size()` can consume a byte if a file grew after EOF**
   - **Location**: [posix-iostream.cpp:39-44](file:///home/thkim/work/tmp/scummvm-review-p7/backends/fs/posix/posix-iostream.cpp#L39-L44)
   - **Failure scenario**: If `fp` previously hit EOF and the underlying file grew before `size()` is called, `(void)fgetc(fp)` reads a byte and advances the file position inside a `const` `size()` call.
   - **Fix**: Guard with `if (wasEof && ftell(fp) == n) (void)fgetc(fp);`.

9. **Stale documentation/comments for hashed 8.3 save names**
   - **Location**: [README.md:126-128](file:///home/thkim/work/tmp/scummvm-review-p7/backends/platform/dos/README.md#L126-L128) (still states `KQ1-RGB5.001` plain truncation), [dos_savenames.h:18](file:///home/thkim/work/tmp/scummvm-review-p7/test/backends/dos_savenames.h#L18) (comment says "keeps 5 characters" instead of 4).
   - **Fix**: Update both comments to match `mappedBase()` (4 chars + 4 base-36 hash digits).

VERDICT: REJECT
