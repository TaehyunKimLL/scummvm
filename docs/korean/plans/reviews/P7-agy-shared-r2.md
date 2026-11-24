### Status of Previous Findings

1. **#1 (Critical) — `hires_text_whole_kb` DOS default (64 KB) dead:** **Addressed.**
   - [hires_options.cpp](file:///home/thkim/work/tmp/scummvm-review-p7/graphics/hires_text/hires_options.cpp#L40-L46) now checks `if (!ConfMan.hasKey(key) && !ConfMan.hasDefault(key)) return false;`.
   - [hires_text_hires_options.h](file:///home/thkim/work/tmp/scummvm-review-p7/test/graphics/hires_text_hires_options.h#L353-L363) adds `test_registeredDefaultIsRead()` verifying `ConfMan.registerDefault("hires_text_whole_kb", 64)` and transient override.

2. **#2 (Critical) — `Blend16` OOB write, `cov == 0` sentinel collision, unguarded stack construction in SCI:** **Addressed.**
   - [text_compose.h](file:///home/thkim/work/tmp/scummvm-review-p7/graphics/hires_text/text_compose.h#L70-L89) and [text_compose.cpp](file:///home/thkim/work/tmp/scummvm-review-p7/graphics/hires_text/text_compose.cpp#L73-L107) add `Blend16::supports()`, constructor `assert(supports(fmt))`, and an explicit `bool filled` flag replacing the `key == 0` sentinel.
   - All four dispatch sites in [text_compose.cpp](file:///home/thkim/work/tmp/scummvm-review-p7/graphics/hires_text/text_compose.cpp#L130-L217) and [upscaled.cpp](file:///home/thkim/work/tmp/scummvm-review-p7/engines/sci/graphics/drivers/upscaled.cpp#L201-L202) check `Blend16::supports(...)`, with expanded tests in [hires_text_blend16.h](file:///home/thkim/work/tmp/scummvm-review-p7/test/graphics/hires_text_blend16.h#L104-L156).

3. **#3 (Critical) — `test/module.mk` breaks `make test` with `USE_CLOUD` and on non-POSIX/non-WIN32 hosts:** **Partially addressed.**
   - The `USE_CLOUD` link/runtime failure is fixed by gating `backends/saves/default/default-saves.o` and `-DDOS_SAVE_MANAGER_TEST=1` under `ifndef USE_CLOUD` ([test/module.mk](file:///home/thkim/work/tmp/scummvm-review-p7/test/module.mk#L28-L31), [dos_savenames.h](file:///home/thkim/work/tmp/scummvm-review-p7/test/backends/dos_savenames.h#L3-L5)).
   - **Remaining minor gap:** On a non-POSIX, non-WIN32 host without `USE_CLOUD`, `default-saves.o` is still added to `TEST_LIBS` even though `test/system/null_osystem.o` (which compiles `backends/saves/savefile.cpp` / `Common::OutSaveFile`) is only added under `ifdef POSIX` or `ifdef WIN32` ([test/module.mk](file:///home/thkim/work/tmp/scummvm-review-p7/test/module.mk#L34-L52)). Gate lines 28–31 on `POSIX`/`WIN32` as well.

4. **#4 (Important) — `--set=KEY=VALUE` session-domain precedence, `-`/`_` non-determinism, CLI control-key injection:** **Partially addressed (implementer's rejection of the `sessionSettings` bug is incorrect).**
   - Control-key injection is mostly blocked by `isControlSetKey()` ([set-option.h](file:///home/thkim/work/tmp/scummvm-review-p7/base/set-option.h#L56-L71), [commandLine.cpp](file:///home/thkim/work/tmp/scummvm-review-p7/base/commandLine.cpp#L780-L783); note `"engine"` is still missing from `control[]`), and repeated `--set` keys normalize `-` to `_` via `normalizeSetKey()`.
   - **Not addressed (wrongly rejected in [P7-fixes.md](file:///home/thkim/work/tmp/scummvm-review-p7/docs/korean/plans/P7-fixes.md#L15)):** `P7-fixes.md` claims normalized `--set` keys go to `kTransientDomain`. That is only true for *hyphenated* entries in `sessionSettings[]` (`opl-driver` -> `opl_driver`). All **14 single-word entries** in `sessionSettings[]` ([commandLine.cpp](file:///home/thkim/work/tmp/scummvm-review-p7/base/commandLine.cpp#L2315-L2348)) — `fullscreen`, `subtitles`, `talkspeed`, `savepath`, `extrapath`, `scaler`, `shader`, `filtering`, `themepath`, `iconspath`, `screenshotpath`, `soundfont`, `renderer`, `logfile` — contain no hyphen, so `normalizeSetKey()` leaves them unchanged, `x._key == *sessionKey` (`:2379`) matches, and `processSettings()` stores them in **`kSessionDomain`**, which has lower priority than `[game]` in `ConfigManager::get()`. Thus `--set=fullscreen=true kq1`, `--set=subtitles=true kq1`, or `--set=extrapath=DATA kq1` (even though `extrapath` is explicitly listed as allowed in [command_line_set.h](file:///home/thkim/work/tmp/scummvm-review-p7/test/base/command_line_set.h#L52)) still silently loses to `[kq1]` in `SCUMMVM.INI`, violating [commandLine.cpp](file:///home/thkim/work/tmp/scummvm-review-p7/base/commandLine.cpp#L114-L115).
   - **Not addressed (CLI + `--set` mix):** Passing a hyphenated CLI option together with `--set`, e.g. `--save-slot=1 --set=save-slot=2`, still inserts both `"save-slot"` and `"save_slot"` into `settings` (`StringMap`) and writes them to `kTransientDomain` in non-deterministic hash-bucket order in `processSettings()` (`:2361-L2381`).

5. **#5 (Important) — `[preload]` bounds, UTF-8 BOM, `SVF_E_IO`, warning label:** **Addressed.**
   - [font_set.cpp](file:///home/thkim/work/tmp/scummvm-review-p7/graphics/hires_text/font_set.cpp#L95-L127) reads at most 64 KB (`kMaxListBytes`), skips a leading UTF-8 BOM, tracks `#` comments across line breaks, caps distinct warmed glyphs at `SvfFace::kDefaultSlots` (64), and stops on `SVF_E_IO` with a warning naming `ref.name`; tested in [hires_text_font_set.h](file:///home/thkim/work/tmp/scummvm-review-p7/test/graphics/hires_text_font_set.h#L250-L278).
   - Minor residuals: the missing-file warning at [font_set.cpp](file:///home/thkim/work/tmp/scummvm-review-p7/graphics/hires_text/font_set.cpp#L89) still prints `list.name` instead of `ref.name`, and the BOM test omits a `#` comment after the BOM (so as `P7-fixes.md` notes, it does not fail if the BOM skip is removed).

6. **#6 (Important) — `DosOPL::OPL::reset()` OPL3 `NEW` bit and register silencing order:** **Addressed.**
   - [dosopl.cpp](file:///home/thkim/work/tmp/scummvm-review-p7/audio/dosopl.cpp#L195-L219) sets `0x105 = 1` before clearing both banks, writes `0x80..0x95 = 0xFF`, `0x40..0x55 = 0x3F`, `0xB0..0xB8 = 0`, `0xBD = 0`, zeroes the remaining registers per bank, and clears `0x105 = 0` last before `initDualOpl2OnOpl3(_type)`.

7. **#7 (Important) — `MidiDriver_DosMPU::setTimerCallback()` replacement and `sysEx()` log ring corruption:** **Partially addressed.**
   - `sysEx()` ring overflow is fixed by `length + 2 >= 0x8000` ([dos_mpu401.cpp](file:///home/thkim/work/tmp/scummvm-review-p7/backends/midi/dos_mpu401.cpp#L357)).
   - `setTimerCallback()` now replaces the callback, but introduces an IRQ0 race (see New Breakage #1 below).

8. **#8 (Important) — `PosixIoStream::size()` on DJGPP clearing `eos()`:** **Addressed.**
   - [posix-iostream.cpp](file:///home/thkim/work/tmp/scummvm-review-p7/backends/fs/posix/posix-iostream.cpp#L39-L44) saves `feof(fp)` and calls `(void)fgetc(fp)` after `StdioStream::size()` to restore `_IOEOF` on EOF streams.

9. **#9 (Important) — `configure` hardcoded `pkg-config` and missing `-DVORBIS_REFILL_SAMPLES=1024`:** **Not addressed.**
   - [configure](file:///home/thkim/work/tmp/scummvm-review-p7/configure#L3892-L3934) and [configure](file:///home/thkim/work/tmp/scummvm-review-p7/configure#L4429-L4433) were not modified: DOS SDL3 still runs literal `pkg-config` before `$_pkgconfig` detection, and standalone `./configure --host=i586-pc-msdosdjgpp` with Vorbis/Tremor still fails the `static_assert` in [dos-mixer.cpp](file:///home/thkim/work/tmp/scummvm-review-p7/backends/mixer/dos/dos-mixer.cpp#L45).

10. **#10 (Minor) — `STDIOSTREAM_NO_ATOMIC_SUPPORT` in-place writes:** **Not addressed** ([stdiostream.cpp](file:///home/thkim/work/tmp/scummvm-review-p7/backends/fs/stdiostream.cpp#L50-L53) unchanged).
11. **#11 (Minor) — Dead defines in `configure` and `DOS_VORBIS_SETUP_CPPFLAGS` in `audio/module.mk`:** **Not addressed** ([configure](file:///home/thkim/work/tmp/scummvm-review-p7/configure#L3916-L3934), [audio/module.mk](file:///home/thkim/work/tmp/scummvm-review-p7/audio/module.mk#L177-L181) unchanged).
12. **#12 (Minor) — `hires_text_whole_kb` whitespace trimming:** **Not addressed** ([hires_options.cpp](file:///home/thkim/work/tmp/scummvm-review-p7/graphics/hires_text/hires_options.cpp#L101-L108) unchanged).
13. **#13 (Minor) — `portPresent()` first `0xFF` exit and `queryVramWindow()` `gran <= size`:** **Not addressed** ([dos_mpu401.cpp](file:///home/thkim/work/tmp/scummvm-review-p7/backends/midi/dos_mpu401.cpp#L231-L240), [dos-graphics.cpp](file:///home/thkim/work/tmp/scummvm-review-p7/backends/graphics/dos/dos-graphics.cpp#L837) unchanged).
14. **#14 (Minor) — `audio/rate.cpp` `assert()` vs `error()` and `hires_text_blend16.h` coverage:** **Partially addressed** ([hires_text_blend16.h](file:///home/thkim/work/tmp/scummvm-review-p7/test/graphics/hires_text_blend16.h#L104-L156) expanded; [rate.cpp](file:///home/thkim/work/tmp/scummvm-review-p7/audio/rate.cpp#L561) unchanged).

---

### New Breakages Introduced by `fix-r1.diff`

#### Important
1. **IRQ0 race when replacing an active callback in `MidiDriver_DosMPU::setTimerCallback()`**
   - **Location:** [dos_mpu401.cpp](file:///home/thkim/work/tmp/scummvm-review-p7/backends/midi/dos_mpu401.cpp#L157-L169)
   - **Scenario:** When `setTimerCallback(param2, proc2)` replaces an already-active callback (`first == false`), `countingTimerProc()` is live inside IRQ0 at 100 Hz. Lines 161–162 write `_clientParam = timerParam;` and `_clientProc = timerProc;` with interrupts enabled (no `_mutex`). If IRQ0 preempts between the two stores, `countingTimerProc()` (`:175-176`) invokes the **old** `_clientProc` with the **new** `_clientParam`, causing type confusion or a crash.
   - **Fix:** Guard the `_clientParam` / `_clientProc` assignment under `Common::StackLock lock(_mutex);` (scoped before calling `MidiDriver_MPU401::setTimerCallback()` when `first` is true).

#### Minor
2. **`DOS::SaveFileManager::renameSavefile()` / `copySavefile()` return `true` for equal names even when the file does not exist**
   - **Location:** [dos-savefile.h](file:///home/thkim/work/tmp/scummvm-review-p7/backends/platform/dos/dos-savefile.h#L134-L148)
   - **Scenario:** Upstream `SaveFileManager::copySavefile()` / `renameSavefile()` return `false` when `oldName` does not exist; `if (oldName.equalsIgnoreCase(newName)) return true;` reports success for a nonexistent save.
   - **Fix:** Return `exists(oldName)` instead of `true`.

3. **`test_registeredDefaultIsRead()` permanently mutates singleton `ConfMan._defaultsDomain` and omits `#if NULL_OSYSTEM_IS_AVAILABLE`**
   - **Location:** [hires_text_hires_options.h](file:///home/thkim/work/tmp/scummvm-review-p7/test/graphics/hires_text_hires_options.h#L353-L363)
   - **Scenario:** `uninstall_null_g_system()` does not reset `ConfMan`, so `registerDefault("hires_text_whole_kb", 64)` leaks into subsequent tests in the same runner process (and omitting `#if NULL_OSYSTEM_IS_AVAILABLE` fails to compile on hosts without `null_osystem`).
   - **Fix:** Guard with `#if NULL_OSYSTEM_IS_AVAILABLE` and ensure tests do not depend on `wholeKb` remaining unregistered afterward.

*(Other changes in `fix-r1.diff` — `dos-mutex.cpp` `sti` restore, `dos-timer.cpp` `lockIrqCode`/`lockIrqData` failure returns, `usage()` `#elif defined(DOS_DJGPP)`, and `dos-savenames.h` FNV-1a base-36 mapping — verified clean.)*

VERDICT: APPROVE WITH CHANGES
