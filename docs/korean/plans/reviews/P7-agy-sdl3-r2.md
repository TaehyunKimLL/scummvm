### Status of Previous Findings

1. **[Critical #1] DMA auto-probe blindly reprograms/unmasks/masks unrelated ISA DMA channels (0, 1, 3)** — **Partially addressed**
   - **Evidence:** [SDL_dosaudio_sb.c:487-517](file:///home/thkim/work/tmp/sdl3-review-p7/src/audio/dos/SDL_dosaudio_sb.c#L487-L517) adds `DOSSOUNDBLASTER_DMACountMoves(configured)`, which avoids probing when `BLASTER`'s `D` is right and only `I` is wrong. However, when `configured` is not moving, [SDL_dosaudio_sb.c:518-540](file:///home/thkim/work/tmp/sdl3-review-p7/src/audio/dos/SDL_dosaudio_sb.c#L518-L540) still blindly reprograms, unmasks, and leaves masked all remaining channels in `{0, 1, 3}` (acknowledged in comment `:519-520`) without first reading 8237A DREQ status bits (`inportb(0x08) & 0xF0`) to restrict probing to channels actually requested by the card.

2. **[Critical #2] `CheckForSoundBlaster()` 300 us vs 300 ms unit bug, `ResetSoundBlasterDSP()` < 3 us pulse, and raw reset in `OpenDevice`** — **Partially addressed**
   - **Evidence:** `ResetSoundBlasterDSP()` now waits `SDL_US_TO_NS(20)` ([SDL_dosaudio_sb.c:50](file:///home/thkim/work/tmp/sdl3-review-p7/src/audio/dos/SDL_dosaudio_sb.c#L50)) and `CheckForSoundBlaster()` now polls until a 300 ms clock deadline ([SDL_dosaudio_sb.c:825-832](file:///home/thkim/work/tmp/sdl3-review-p7/src/audio/dos/SDL_dosaudio_sb.c#L825-L832)). However, `DOSSOUNDBLASTER_OpenDevice` at [SDL_dosaudio_sb.c:621](file:///home/thkim/work/tmp/sdl3-review-p7/src/audio/dos/SDL_dosaudio_sb.c#L621) still calls raw `ResetSoundBlasterDSP()` without waiting for `0xAA`, leaving `0xAA` unread in `base+0xA` and risking issuing `0xD1` before reset completes; `CheckForSoundBlaster()` also still reads `base+0xA` only once without draining stale pre-reset bytes.

3. **[Critical #3] Unvalidated `BLASTER` DMA (`D`/`H`) & IRQ causing OOB table reads / port `0x00` / DMA 2 & 4 clobber** — **Addressed**
   - **Evidence:** [SDL_dosaudio_sb.c:872-903](file:///home/thkim/work/tmp/sdl3-review-p7/src/audio/dos/SDL_dosaudio_sb.c#L872-L903) validates `A` (`0x200..0x2F0`, step 16), `I` (`0..15`, `!= 2`), `D` (`0, 1, 3`), and `H` (`5..7`); high-DMA masking uses `0x04 | (channel & 3)` at `:251` and `:347`. (See New Breakage #3 for a fallback regression when `H` is rejected on SB16.)

4. **[Critical #4] Missing `"memory"` clobber in `DOS_DisableInterrupts()`/`DOS_EnableInterrupts()`, non-volatile `isr_dma_channel`, and unlocked caller pointers under `cli` in `DOS_SBGetStats`** — **Addressed**
   - **Evidence:** [SDL_dos.h:150-158](file:///home/thkim/work/tmp/sdl3-review-p7/src/core/dos/SDL_dos.h#L150-L158) adds `: : : "memory"` to both helpers; [SDL_dosaudio_sb.c:115](file:///home/thkim/work/tmp/sdl3-review-p7/src/audio/dos/SDL_dosaudio_sb.c#L115) makes `isr_dma_channel` `volatile`; [SDL_dosaudio_sb.c:315-331](file:///home/thkim/work/tmp/sdl3-review-p7/src/audio/dos/SDL_dosaudio_sb.c#L315-L331) snapshots counters to locals under `cli`/`sti` and writes caller out-pointers with interrupts re-enabled.

5. **[Critical #5] Signed 32-bit overflow UB in `isr_ring_read` / `isr_ring_write` after ~3.38 hours** — **Addressed**
   - **Evidence:** [SDL_dosaudio_sb.c:107-108](file:///home/thkim/work/tmp/sdl3-review-p7/src/audio/dos/SDL_dosaudio_sb.c#L107-L108) and [SDL_dosaudio_sb.h:42-43](file:///home/thkim/work/tmp/sdl3-review-p7/src/audio/dos/SDL_dosaudio_sb.h#L42-L43) convert all four ring counters to `volatile Uint32`, with unsigned differences and masks at [SDL_dosaudio_sb.c:135](file:///home/thkim/work/tmp/sdl3-review-p7/src/audio/dos/SDL_dosaudio_sb.c#L135), `:189`, `:232`, `:319`, `:764`.

6. **[Critical #6] 64 KB DMA boundary crossing in `DOS_AllocateDMAMemory` for `len > 32768`** — **Addressed**
   - **Evidence:** [SDL_dos.c:82-95](file:///home/thkim/work/tmp/sdl3-review-p7/src/core/dos/SDL_dos.c#L82-L95) checks `{ 0, len, up }` (where `up` aligns to the next 64 KB boundary) with exact last-byte check `((physical + off) >> 16) == ((physical + off + len - 1) >> 16)` and frees + returns `NULL` if none fits.

7. **[Important #7] `DOS_LockIRQCode` `handlers[]` omits `RingCopyOut`/`ISR_GetDMAHalf`, and DPMI-locked `ring_buffer` leaked on free** — **Partially addressed**
   - **Evidence:** `hidden->ring_locked` and `DOS_UnlockData` / `DOS_UnlockRange` were added ([SDL_dosaudio_sb.c:259-261](file:///home/thkim/work/tmp/sdl3-review-p7/src/audio/dos/SDL_dosaudio_sb.c#L259-L261), `:701-702`, [SDL_dos.c:99-109](file:///home/thkim/work/tmp/sdl3-review-p7/src/core/dos/SDL_dos.c#L99-L109)) — see New Breakage #2 for a CWSDPMI page-sharing bug with unaligned unlock. `handlers[]` at [SDL_dosaudio_sb.c:699](file:///home/thkim/work/tmp/sdl3-review-p7/src/audio/dos/SDL_dosaudio_sb.c#L699) still checks only `SoundBlasterIRQHandler` (omitting `RingCopyOut` and `ISR_GetDMAHalf`; rejected in [fix-r1-note.md](file:///home/thkim/work/scratch/review-lanes/scummvm-p7-sdl3-agy/fix-r1-note.md), though runtime verification of every non-inlined ISR callee is the purpose of `handlers[]`), and `:701-719` still invoke remaining lock calls even after an earlier lock fails.

8. **[Important #8] Pre-SB16 low sample-rate `time_constant` underflow and unclamped SB16 frequency** — **Addressed**
   - **Evidence:** [SDL_dosaudio_sb.c:548-561](file:///home/thkim/work/tmp/sdl3-review-p7/src/audio/dos/SDL_dosaudio_sb.c#L548-L561) adds `DOSSOUNDBLASTER_TimeConstant` clamping hardware rate to `[4000, max_hw]`, gating high-speed on DSP >= 2.01 (also at `:438`), and reporting exact achieved `real_freq`; [SDL_dosaudio_sb.c:588-599](file:///home/thkim/work/tmp/sdl3-review-p7/src/audio/dos/SDL_dosaudio_sb.c#L588-L599) clamps SB16 `freq` to `[5000, 44100]`.

9. **[Important #9] SB16 `0xDA` vs `0xD5`/`0xD9`; probe-failure SB Pro stereo cleanup; stale `isr_irq_count`; unpersisted probed DMA channel** — **Partially addressed**
   - **Evidence:** `CloseDevice:789-790` now sends `0xD5` + `0xD9` ([SDL_dosaudio_sb.c:789-790](file:///home/thkim/work/tmp/sdl3-review-p7/src/audio/dos/SDL_dosaudio_sb.c#L789-L790)), and `OpenFailed:283-286` clears SB Pro mixer `0x0E` bit 1 ([SDL_dosaudio_sb.c:283-286](file:///home/thkim/work/tmp/sdl3-review-p7/src/audio/dos/SDL_dosaudio_sb.c#L283-L286)). However, `isr_irq_count` is still never reset in `OpenDevice` or `FreeState` (`:266-275`, `:681-686`), `isr_underruns`/`isr_min_avail` are not cleared in `FreeState`, and `soundblaster_dma_channel` is still not updated when `ProbeDMAChannel` succeeds (`:531-534`).

10. **[Important #10] Non-`volatile` `keyevents_head`/`keyevents_ringbuffer` and unreset static parser state (`hangul_down`, `hanja_down`, `is_extended`, `pause_sequence_remaining`)** — **Not addressed**
    - **Evidence:** [SDL_dosevents.c](file:///home/thkim/work/tmp/sdl3-review-p7/src/video/dos/SDL_dosevents.c) was not modified in `fix-r1.diff` (`:265-267`, `:291-294`, `:416-434` unchanged).

11. **[Important #11] Downstream ScummVM canary globals, undeclared `DOS_SBGetStats` (no NULL guards), and C++ linkage mismatch in `SDL_dos.h`** — **Not addressed**
    - **Evidence:** All nine `DOS_*` canary globals and `DOS_SBGetStats` remain unchanged, and [SDL_dos.h:91-96](file:///home/thkim/work/tmp/sdl3-review-p7/src/core/dos/SDL_dos.h#L91-L96) remains outside `extern "C"` (`:126-220`).

12. **[Minor #12] `change.diff` reverted unrelated upstream non-DOS commits** — **Not addressed** (`fix-r1.diff` is incremental on the same un-rebased branch).

13. **[Minor #13] `CPU_haveCPUID` `"+m"` x86-64 red-zone interaction** — **Not addressed** ([SDL_cpuinfo.c:176-192](file:///home/thkim/work/tmp/sdl3-review-p7/src/cpuinfo/SDL_cpuinfo.c#L176-L192) unchanged).

14. **[Minor #14] `DOS_LockIRQCodeRange` pointer-subtraction UB and missing `.globl` for LTO** — **Not addressed** ([SDL_dos.c:43](file:///home/thkim/work/tmp/sdl3-review-p7/src/core/dos/SDL_dos.c#L43), [SDL_dos.h:72-79](file:///home/thkim/work/tmp/sdl3-review-p7/src/core/dos/SDL_dos.h#L72-L79) unchanged).

15. **[Minor #15] SB Pro stereo L/R phase quirk and redundant `0xD3` after high-speed DSP reset** — **Not addressed** ([SDL_dosaudio_sb.c:415-424](file:///home/thkim/work/tmp/sdl3-review-p7/src/audio/dos/SDL_dosaudio_sb.c#L415-L424), `:793-801` unchanged).

16. **[Minor #16] Non-atomic 16-bit 8237A counter read in `ISR_GetDMAHalf()`** — **Not addressed** ([SDL_dosaudio_sb.c:146-160](file:///home/thkim/work/tmp/sdl3-review-p7/src/audio/dos/SDL_dosaudio_sb.c#L146-L160) unchanged).

---

### New Breakage Introduced by `fix-r1.diff`

1. **[Important] `SoundBlasterIRQHandler` clobbers the SB16 mixer index port `base+0x04` without saving/restoring it, and sends a double EOI when chaining an MPU-401 interrupt**
   - **Location:** [SDL_dosaudio_sb.c:173-180](file:///home/thkim/work/tmp/sdl3-review-p7/src/audio/dos/SDL_dosaudio_sb.c#L173-L180)
   - **Failure scenario:**
     - Every SB16 interrupt writes `0x82` to `isr_mixer_base` (`base + 0x04`) and never restores the previous index. If main-thread mixer code (e.g., volume adjustment) is interrupted between `outportb(base + 4, reg)` and `outportb(base + 5, val)` / `inportb(base + 5)`, its data-port access silently hits register `0x82` instead of `reg`.
     - When `st` indicates an MPU-401-only interrupt (`(st & 0x04) && !(st & 0x03)`), the handler calls `DOS_EndOfInterrupt(soundblaster_irq)` and returns. Because `DOS_HookInterrupt` installs a **chained** vector via `_go32_dpmi_chain_protected_mode_interrupt_vector` ([SDL_dos.c:151](file:///home/thkim/work/tmp/sdl3-review-p7/src/core/dos/SDL_dos.c#L151)), control then chains to the previous handler (e.g., an MPU-401 ISR), which sends a **second EOI**, potentially clearing a different lower-priority in-service IRQ bit on the 8259 PIC.
   - **Fix:** Save `const Uint8 saved_idx = DOS_InPort8(isr_mixer_base);` before writing `0x82` and restore `DOS_OutPort8(isr_mixer_base, saved_idx);` immediately after reading `isr_mixer_base + 1`. On the MPU-401-only return path, omit `DOS_EndOfInterrupt` so the chained MPU-401 handler owns EOI (or switch SB away from chaining to a dedicated IRET wrapper).

2. **[Important] `DOS_UnlockData` on an unaligned `SDL_calloc` `ring_buffer` can unlock adjacent locked heap objects (e.g., thread stacks) under CWSDPMI**
   - **Location:** [SDL_dosaudio_sb.c:259-261](file:///home/thkim/work/tmp/sdl3-review-p7/src/audio/dos/SDL_dosaudio_sb.c#L259-L261), [SDL_dosaudio_sb.c:666](file:///home/thkim/work/tmp/sdl3-review-p7/src/audio/dos/SDL_dosaudio_sb.c#L666), [SDL_dos.c:99-109](file:///home/thkim/work/tmp/sdl3-review-p7/src/core/dos/SDL_dos.c#L99-L109), [SDL_dos.h:117-119](file:///home/thkim/work/tmp/sdl3-review-p7/src/core/dos/SDL_dos.h#L117-L119)
   - **Failure scenario:** `hidden->ring_buffer` is allocated with `SDL_calloc` (not 4 KB page-aligned), so its first and last 4 KB pages can share pages with other locked heap allocations—such as live thread stacks locked by `_go32_dpmi_lock_data` in [SDL_dos_scheduler.c:138-146](file:///home/thkim/work/tmp/sdl3-review-p7/src/core/dos/SDL_dos_scheduler.c#L138-L146). Contrary to the comment at [SDL_dos.h:117](file:///home/thkim/work/tmp/sdl3-review-p7/src/core/dos/SDL_dos.h#L117), CWSDPMI tracks locks with a **single PTE bit** rather than a per-page lock count. Unlocking an unaligned `ring_buffer` clears the lock bit on those shared boundary pages, allowing CWSDPMI to page out another thread's stack and trigger a fatal page fault on context switch or IRQ.
   - **Fix:** Allocate `hidden->ring_buffer` on exclusive 4 KB pages via `SDL_aligned_alloc(4096, aligned_size)` (with `aligned_size = (hidden->ring_size + 4095) & ~4095`, zeroed via `SDL_memset`, unlocked over `aligned_size`, and freed via `SDL_aligned_free`).

3. **[Important] Rejecting invalid/8-bit `H` in `BLASTER` on an SB16 causes `OpenDevice` to fail instead of falling back to 8-bit `D` as documented**
   - **Location:** [SDL_dosaudio_sb.c:869-903](file:///home/thkim/work/tmp/sdl3-review-p7/src/audio/dos/SDL_dosaudio_sb.c#L869-L903), [SDL_dosaudio_sb.c:933](file:///home/thkim/work/tmp/sdl3-review-p7/src/audio/dos/SDL_dosaudio_sb.c#L933), [SDL_dosaudio_sb.c:624-629](file:///home/thkim/work/tmp/sdl3-review-p7/src/audio/dos/SDL_dosaudio_sb.c#L624-L629)
   - **Failure scenario:** On an SB16 (`soundblaster_version >= 4`) with `BLASTER=A220 I5 D1 H1` (or `BLASTER=A220 I5 D1` with no `H`), the new parser ignores `H1`, leaving `soundblaster_highdma_channel == -1` and `soundblaster_dma_channel == 1`. Although the new comment at `:869` states `"or the 8-bit path is used"`, line `:933` still sets `soundblaster_is_sb16 = !FORCE_SB_8BIT && (soundblaster_version >= 4)` (`true`), so `OpenDevice:624-629` selects `soundblaster_highdma_channel` (`-1`) and fails to open audio completely instead of falling back to 8-bit playback on `D1`.
   - **Fix:** Set `soundblaster_is_sb16 = !FORCE_SB_8BIT && (soundblaster_version >= 4) && (soundblaster_highdma_channel >= 0);` at [SDL_dosaudio_sb.c:933](file:///home/thkim/work/tmp/sdl3-review-p7/src/audio/dos/SDL_dosaudio_sb.c#L933).

VERDICT: REJECT
