# P7-mmx notes: an MMX text composite for the DOS port

Branch `ko/mmx` (from `ko-2026.4` @ `d3da2f58225`), worktree `scummvm-2026.4-korean-mmx`. Scratch: `~/work/tmp/mmx-impl`.

## What was done

- `graphics/hires_text/text_compose.cpp`: `composeBlend16MMX()` (`__attribute__((target("mmx")))`, noinline), called from
  `composeSpan()` for the 2-byte kBlend path when `g_useMMX`. Only that function holds MMX instructions (checked with
  objdump on the DJGPP object: both exits are followed by `emms`). The rest of the object stays at the build's `-march`.
- Gate: `mmxSupported()` = compiled in (`__DJGPP__ && __i386__`, or `-DHIRES_TEXT_MMX` on any x86 host) and CPUID leaf 1 EDX bit 23
  (`__get_cpuid` returns 0 on a CPU without CPUID). `setUseMMX(true)` is a no-op unless supported. **Default off**; nothing in the
  engine turns it on yet (an owner decision, see open items). Not compiled on other targets (the functions return false).
- Two exact modes (`setMMXMode`): 1 (default) = runs: a block of 4 coverages all 0 is skipped, all 255 is stored from the palette,
  any other block goes pixel by pixel through the `Blend16` table; 0 = vector arithmetic on partial blocks too.
- Vector arithmetic (mode 0): per channel `y = v*(255-a) + f*a + 127`, `q = (y + 1 + (y >> 8)) >> 8` equals `y / 255` for all
  y < 65535 (checked in Python for 0..65534; here y <= 63*255+127), 16-bit lanes (`pmullw`), coverage-0 lanes keep the old pixel.

## Design choice and why

An exact *table* does not vectorise (MMX has no gather), so the choices were vector arithmetic or run skip/store with the table
for partial coverage. Measured in DOSBox-X (`cputype=pentium_mmx`, `core=normal`, `cycles=fixed 60000`, 400x128 box, 70 % empty,
8 % at 85, 7 % at 170, 15 % at 255, `MMXB.EXE`): scalar 28.25 ms, vector arithmetic 26.92 ms, runs 24.68 ms (RGB565; RGB555 28.43 /
26.92 / 24.88). Runs mode is the fastest and the simplest, so it is the default. Gain about 13 % in the emulator.

## Verification (all run)

- Differential test `test/graphics/hires_text_mmx.h` (host, `-DHIRES_TEXT_MMX`, both modes, 5 formats: RGB565, RGB555, ARGB1555,
  RGBA5551, BGR565) against `blendPixel()` (no shared arithmetic): per mode 83,886,080 px (every 16-bit pixel x every coverage 0..255,
  palette rotating) + 83,886,080 px (every colour index x coverage x spread backgrounds) + 83,886,080 px (every background channel x
  colour channel x coverage x format, channels at offsets 0/85/170) + 102,252,659 px random rows (lengths 0..1023, blocks of 0/255/mixed)
  + overrun/EMMS check. Total 2 modes x 353,911,558 = 707,823,116 px; 0 mismatches.
- Host `make test`: 1075 tests OK (5 are new: the MMX suite; the others unchanged and scalar). Note the task text said 1011; this tree
  already counts more, I did not re-derive the 1011.
- ASan + UBSan (`-fsanitize=address,undefined -fno-sanitize=vptr`, vptr off: the build has no RTTI): all 1075 OK, MMX suite OK.
- Mutation check (each made the MMX suite fail, source restored): rounding 127 -> 126 (4 of 5 tests fail), drop the zero-coverage
  keep mask (1 of 5), wrong stored colour in runs mode (4 of 5), zero-block skip tested on 3 bytes only (1 of 5).
- DOS: `MMXB.EXE` (`build-dos.sh mmxbench BUILD_DIR`, needs a built sci edition) under DOSBox-X with a private conf:
  `pentium_mmx`: mmx_cpuid=1, 4000 random rows per format x 2 modes + the box: diffs=0, equal=1. `pentium`: mmx_cpuid=0, scalar only,
  scalar output as before (28.25 / 28.39 ms), equal=1 (nothing to compare).

## UNVERIFIED-ON-HARDWARE

- Real Pentium MMX / K6 / 486 timing: DOSBox-X counts instructions, so the 13 % is not a hardware number; MMX load/store and
  `pmullw` latencies, unaligned 8-byte accesses (dst is only 2-byte aligned) and cache effects are not modelled.
- EMMS cost and the FPU/MMX mode switch with a real FPU user in between: not exercised.
- A CPU that reports the MMX bit but traps (e.g. an emulator with a broken CR0.EM): not tested.
- The engine does not enable MMX; whether to call `setUseMMX(true)` at start-up on DOS is open until a hardware measurement.
