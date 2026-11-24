# P7: DOS port — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task by task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** DESIGN.md section 11 row P7 and section 8: the Korean tree `ko-2026.4` runs on MS-DOS (DJGPP, SDL3 as HAL,
Pentium-class PC, 16 MB): `SCI.EXE`, `SCUMM.EXE`, `AGI.EXE` and the `PLAY.EXE` launcher; the hi-res Korean text plane in
VBE 640x400 (or 640x480 line repeat) at RGB565 / RGB555 (anti-aliased faces) and CLUT8 (1 bpp faces); faces streamed from
files; the Han/Yeong key; INI and 8.3 layout; acceptance in DOSBox-X and DOSBox Staging from the two-zip layout. The
port is the topic `ko/08-dos`, rebased from `jamo` as its own patch series (decision 9), and carries no Korean logic of
its own (owner directive, DESIGN 8).

**Architecture:** first the SDL3 patch series moves onto current upstream (outside the ScummVM tree, gate = the smoke
program in DOSBox-X and Staging). Then the backend series is replayed from `jamo` onto `ko-2026.4` in topic order:
configure host -> audio/base hooks -> the backend files -> fs fixes -> build script -> host unit tests -> SCI.EXE.
Couplings to `jamo`'s own Korean code (`HiResBitmapFont`, `KeyedCompose`, `FileCacheRegistry`, the jamo
`hires_options` render-target cap) are cut, because `ko-2026.4` decides the render target in the engine
(`resolveRenderTarget`, `hiResPickScreenFormat`): the backend only says which formats it can set. Two engine-neutral
improvements land in the shared core because the DOS measurements demand them: an exact table-driven 15/16-bit blend
(`TextCompose::Blend16`, 2.9x faster, bit-identical) and the `hires_text_whole_kb` key; plus `[preload]` warm-up for
streamed faces. Everything is proven by host unit tests (CxxTest, `make test` on Linux), an exhaustive differential test
of the blend, and DOSBox-X / Staging runs whose window captures are compared with the Linux frames.

**Tech stack:** C++11 ScummVM code, DJGPP gcc 12.2 (`~/opt/djgpp`), SDL3 3.5.0-dev static for DJGPP, CWSDPMI 7,
CxxTest, Python 3 (stdlib + PIL for frame compares), DOSBox-X 2026.08.31 (`/usr/bin/dosbox-x`), DOSBox Staging 0.83
(`$DOSBOX_STAGING`), Xvfb + xdotool + xwd + ffmpeg.

Source of scope: DESIGN.md sections 5, 7, 8, 9, 10, 11 (P7 row), 12 (decisions 3, 8, 9, 11), 13; `PATCHES.md`;
`plans/AA-tests.md` (15/16-bit mandatory, section 3 "DOS (P7)" item, blend cost UNVERIFIED); `plans/P6a-*.md`; memory
notes `scummvm-dos-port`, `han-yeong-key-dos`, `svf-renderer-accuracy-policy`, `scummvm-ko-aa-rgb-only`,
`scummvm-ko-utf8-internal`; `jamo` (read with `git -C $OLD show jamo:<path>`, never checked out); the harness
(`~/work/scummvm`, read only). Every claim below was checked in code or by a run on 2026-11-23, or is marked UNVERIFIED.

---

## 0. Facts checked for this plan (2026-11-23)

Tree `/home/thkim/work/scummvm-2026.4-korean`, branch `ko-2026.4` at **`e9611c272ee`** (v2026.4.0 + P0..P4 + AA +
P6a merged at `2431230800d`). `git diff v2026.4.0 HEAD -- engines/agi` is empty (P5 not done). No debug socket in the
tree (`ls gui | grep debugsock` empty; `configure` has no `debug-socket` option).

### 0.1 SDL3 for DJGPP

- `~/opt/src/SDL3`, branch `dos-kbd-hangul` (= `7a588ba`), merge base with upstream `1ce4c5bc29` (2026-09-26). It
  carries **16 commits** over that base, not 12: `09c194c`, `11ca8a4` (irq lock), `ef2105c`, `2df4ec6` (CPUID),
  `fbd0d18`, `f6cc527` (SB shutdown, SB open-fail), `f5652a4`, `d67c987` (SB DMA probe), `c5ad490`, `1500973` (SB
  counters), `9a55c71` (SB Pro high speed), `3ed5eb3`, `45125d4` (SB ring of five), `8361783` (Han/Yeong + Hanja as
  LANG1/LANG2), and the pair `32a07f5` "give the keys a keycode" + `7a588ba` its revert (`git diff 8361783 7a588ba` is
  empty). They map onto `jamo`'s nine `backends/platform/dos/sdl3-*.patch` files (`sdl3-build.txt` lists them).
- Upstream: `release-3.4.20` (`f00dc919c`, 2026-11-03) has **no DOS port**: `git ls-tree -r --name-only release-3.4.20 |
  grep -c /dos/` = **0**, and `1ce4c5bc29` is not its ancestor (`git merge-base --is-ancestor` fails). The DOS port
  exists only on `main` (3.5.0 development): `1362030` (2026-11-23 09:01, the commit the owner measured) has 25 files
  under `src/*/dos`. (`main` moved on to `14d1db3f4` the same day; 9 commits, none touch DOS.) So the series is rebased
  onto `main`, pinned to `1362030`; a stable tag is not an option until a 3.6 release carries the DOS port.
- Upstream DOS changes since `1ce4c5bc29` (`git log 1ce4c5bc29..1362030 -- src/*/dos`): joystick cleanup
  (`b0ccb9ef0`, `ddfe1bedd`, `d307ae547`, `src/joystick/dos/SDL_sysjoystick.c` +16/-122), `SDL_GetCurrentThreadName`
  (`fff3d3516`, thread/dos), `SDL_GetCachePath` (`f2262b8c5`, filesystem/dos). None of ours is upstream.
- **Prototype:** `git rebase --onto 1362030 1ce4c5bc29` of the 16 commits in a scratch clone (`~/work/tmp/p7/sdl3`,
  branch `dos-p7`) applied **without a conflict**; the min build (`sdl3-build.txt` options) and the full build both
  compiled (2 unrelated `-Wunused-function` warnings in `src/render/SDL_render.c`); `nm` finds every link mark
  (`DOS_IRQCodeChecked`, `DOS_CPUIDChecked`, `DOS_SBShutdownChecked`, `DOS_SBOpenFailChecked`, `DOS_SBProbeChecked`,
  `DOS_SBStatsChecked`, `DOS_SBRingChunks`, `DOS_KbdHangulChecked`); version string `SDL-3.5.0-ed8a14d5e`. The smoke
  program (`~/opt/dos-dev/smoke/smoke.c`) against both builds: DOSBox-X and Staging each log `video=vesa
  audio=soundblaster`, `640x400 INDEX8 mode found`, `window ok surface 640x400 SDL_PIXELFORMAT_INDEX8`, `audio ok`,
  `timer 10ms x 1000ms -> 99 callbacks`, `done`.
- Mode lists measured with that smoke run (they decide P7c): DOSBox-X `svga_s3` 640x400 has XRGB8888, **RGB565**,
  **XRGB1555**, INDEX8; DOSBox Staging 640x400 has only XRGB8888 and INDEX8, 640x480 has XRGB8888, BGR24, RGB565,
  XRGB1555, INDEX8.

### 0.2 What the ScummVM prototype proved

Scratch tree `~/work/tmp/p7/tree` (`git clone --shared` of `ko-2026.4`, branch `p7-proto`, never pushed) + the `jamo`
DOS files (`git archive jamo backends/platform/dos backends/{graphics,events,timer,mixer,mutex}/dos
backends/midi/dos_mpu401.cpp audio/dosopl.{h,cpp} base/set-option.h base/config-load.h`) + `jamo`'s hunks of
`configure`, `backends/module.mk`, `audio/{adlib,audiostream,fmopl,mididrv,rate}.cpp`, `audio/{fmopl,mixer}.h`,
`audio/module.mk`, `base/plugins.cpp`, `image/codecs/codec.cpp`, `engines/advancedDetector.cpp` (all applied with `git
apply --3way`, **all clean**) + the adaptations of P7b Task b4 (prototype diff: `~/work/tmp/p7/proto-modified.diff`,
blend change `~/work/tmp/p7/proto-blend16.diff`). Built out of tree in `~/work/tmp/p7/build-sci`:

```
configure --host=i586-pc-msdosdjgpp --disable-all-engines --enable-engine=sci --disable-engine=sci32 ... --disable-freetype2
  --disable-detection-full --disable-translation --disable-debug-socket --enable-release      (GUI left ON)
Backend... dos, assembly routines, 16bit color, high resolution, savegame timestamp
Engines (builtin):  SCI [SCI 0-1.1 games]
make -> scummvm.exe 7,880,948 B (5,417,472 B stripped; jamo's SCI.EXE of 2026-10-17 is 5,670,989 B, built --disable-gui)
```

The build broke on exactly four couplings, all fixed in the prototype: `parseRenderTarget` lives in
`Graphics::HiResFontMap` (`graphics/hires_text/font_map.h:162`), `Audio::kVorbisRefillSamples` (a SCUMM.EXE hunk of
`audio/decoders/vorbis.h`), `OPL::_hasInstance` private (the `friend class LoggingOPL` line of `audio/fmopl.h`),
`getOutputLatencyMillis` (the `audio/mixer.h` hunk). `irqcheck.py` fails only on the missing `uart` range (no debug
socket). Runs (DOSBox-X `svga_s3`, `cputype=pentium`, `cycles=60000`, 16 MB, SB16; Staging same without cputype;
headless Xvfb, window grabbed with `xwd` + `ffmpeg`; scripts `~/work/tmp/p7/bin/dosrun.sh`, confs
`~/work/tmp/p7/run-kq1/*.conf`) with the P3 KQ1 Korean data (`~/work/tmp/p3-kq1/pack`, its `ko/` files upper-cased):

| Run | Log | Result (screenshots in `~/work/tmp/ko-screenshots/`, captions in `captions.txt`) |
|---|---|---|
| KQ1 title, default map | `DOS: mode 640x400 CLUT8`, `Hi-res text: using data:hires_text/maps/korean-default.map` | `dos-kq1-x-clut8-title.png`: Hangul buttons. The `data:` path (not 8.3) was found as `DATA\HIRES_TE\MAPS\KOREAN-D.MAP`: DOS truncates each component |
| KQ1 AA pair map, `render_target=rgb565`, typed `qhk` | `DOS: mode 640x400 RGB565@2`, `screen format RGB565@2` | `dos-kq1-x-rgb565-message.png`: status line and message box **pixel-identical** to Linux `aa-kq1-rgb565-message.png` (0 differing pixels outside the host mouse cursor) |
| same, `rgb555` | `DOS: mode 640x400 RGB555@2` | `dos-kq1-x-rgb555-message.png`: max 1 per channel against Linux (the two screenshot paths expand 5 bits differently) |
| same, `clut8` (`[font:clut8]` 1 bpp NG16J1) | `DOS: mode 640x400 CLUT8` | `dos-kq1-x-clut8-1bpp-pair-message.png`: status rows identical, box max 3 per channel (VGA DAC 6 bits) |
| Staging, `rgb565` | `DOS: mode 640x480 RGB565@2 (line repeat from 640x400)` | `dos-kq1-staging-rgb565-linerepeat-message.png` |
| DOSBox-X with a mapper file `key_kor_hanyong "key 66"`, F9 twice, `qhk` | `Hangul mode off`, `Hangul mode on` | `dos-kq1-x-rgb565-hanyeong-prompt.png`: 봐 in the parser box |
| `originalsaveload=true`, F5 + `gksrmf`, new session F7, target `kq1sv` | file `SAVES\KQ1SV.001` | `dos-kq1-x-restore-utf8-savename.png`: `한글` listed: UTF-8 save name round trip on FAT |
| same with target `kq1-rgb565` (10 characters) | file `SAVES\KQ1-RGB5.001` | listed in its own session, **empty list in a new session** (`dos-kq1-x-restore-longtarget-empty.png`): a bug P7e fixes |

Memory (the backend's own log lines, 16 MB DOSBox = `phys_total=14896` KB, GUI compiled in): `DOS: memory engine
dpmi_free=8964`, first frame **5812 KB free at CLUT8, 4716 KB at RGB565, 4616 KB at RGB565 line repeat**. Each run also
logged `WARNING: Unable to write configuration file: C:\SCUMMVM\SCUMMVM.INI!` (twice): `backends/fs/stdiostream.cpp`
writes `NAME.EXT.tmp` for an atomic write, which 8.3 DOS cannot create (`jamo` fixed it, P7b Task b5).

### 0.3 The RGB composite on old CPUs (measured)

`TextCompose::blendPixel` (`graphics/hires_text/text_compose.cpp:60-70`) loops over three channels reading the
`PixelFormat` fields and divides by 255 three times a pixel; `composeSpan` (`:84-93`) calls it for every covered pixel
and the SCI driver (`engines/sci/graphics/drivers/upscaled.cpp:184-214`, `compositeText`) runs it for every updated
rectangle that overlaps text, i.e. on every frame an animation under a dialogue box updates. Bench
`~/work/tmp/p7/blendbench/{bench,bench2}.cpp` (links the `text_compose.o` of the DOS build, DJGPP `-O2`, default
`-march=pentium`), one dialogue box at 2x = 400 x 128 pixels, coverage of a 2 bpp face (70 % empty, 8 % 85, 7 % 170,
15 % 255; 15,487 covered pixels), DOSBox-X `core=normal`, times on emulated `uclock()`:

| CPU preset (DOSBox-X) | current `composeSpan` | + cov 255 fast path | + `Blend16` tables kept across rows (this plan) | `Blend16` remade per row |
|---|---:|---:|---:|---:|
| `cputype=486 cycles=fixed 23880` ("486DX2-66") | 136.86 ms (8.8 µs a covered pixel) | 78.58 ms | **47.67 ms** | 64.39 ms |
| `cputype=486 cycles=fixed 12010` ("486DX-33") | 272.49 ms | 156.46 ms | **94.91 ms** | 128.19 ms |
| `cputype=pentium cycles=fixed 60000` (not a calibrated preset) | 54.43 ms | 31.25 ms | **18.96 ms** | 25.61 ms |

RGB555 rows are the same to 0.1 ms. Every variant's output equals the current one (`equal=1`). The table variant is
exact by construction (it tabulates `blendPixel`'s own integers) and proven: host test `~/work/tmp/p7/hosttest/exh.cpp`
compared `Blend16::blend` and `composeSpan` with `blendPixel` for 5 formats (RGB565, RGB555, ARGB1555, RGBA5551,
BGR565) x 6 colours x coverage 1..254 x all 65,536 pixels: **499,404,800 checks, 0 mismatches**; with `+ 127` changed
to `+ 126` in the green table: 974,899 mismatches (the test catches it). The rebuilt SCI.EXE's KQ1 rgb565 frame is again
pixel-identical to Linux. DESIGN 8's "one dialogue box of four lines is therefore under a frame" holds for drawing the
glyphs into the plane (svf bench) but **not** for the RGB composite (137 ms on the DX2-66 preset before this plan, 48
ms after). DOSBox-X counts instructions, not clocks; a real 486's `imul` (13-42 clocks) makes the old path relatively
worse and the table path relatively better: UNVERIFIED-ON-HARDWARE. A 386 was not measured (DESIGN's target is a
Pentium-class PC).

Decision taken here (within the accuracy policy, which only constrains approximations): the **cov == 255 fast path and
the exact lookup table are both needed**; no approximate blend (`SVF_FAST`-style) is introduced.

### 0.4 Face memory (measured, `~/work/tmp/p7/facemem/facemem.c` over `svf_stream_tables_size`)

| Face | Kind | File | Streamed tables | Cell | Note |
|---|---|---:|---:|---:|---|
| NEODG16A (engine fallback) | 1 bpp jamo set | 16,408 | 8,153 | 32 | whole under the 256 KB default |
| NG16J1 (AA-tests pair) | 1 bpp jamo set | 20,064 | 8,137 | 32 | |
| NG16JA | 2 bpp jamo set | 24,960 | 8,137 | 64 | |
| NG16P (103 syllables) | 2 bpp precomposed | 12,248 | 3,068 | 64 | test fixture |
| GBATB17 (svf/dos) | 2 bpp precomposed 17 px | 245,564 | **50,492** | 64 | whole at 256 KB (245 KB) or streamed (50 KB + cache) |
| GBATB23 | 2 bpp 23 px | 338,948 | 39,148 | 144 | streamed at the default |
| MARUS24A / MARUS24 | 2 / 1 bpp 24 px | 107,508 / 87,956 | 32,333 | 144 / 72 | |
| PAPLG18 | 2 bpp jamo 18 px | 69,408 | 29,341 | 90 | |

So a jamo face streams with ~8 KB of tables (DESIGN 8's figure holds) plus the block cache (`SvfFace::kDefaultSlots`
64 x `kDefaultBlockSize` 1 KB, `graphics/hires_text/svfface.h:55-58`) = ~73 KB; a precomposed AA face of 2,350+
syllables needs 30-50 KB of tables + the cache = 95-115 KB streamed, against 108-340 KB whole. `HiResFaceCache` reads a
face whole up to `wholeLimit` (default 256 KB, `font_set.h:58`; the SCI glue passes no limit,
`engines/sci/graphics/hirestext.cpp:80`) and gives every chain a 64-slot syllable cache (`font_set.h:109`,
`font_set.cpp:120`). The `[preload]` section is parsed (`font_map.cpp:628-636`) but **nothing reads it** (grep: no
user of `HiResMap::preloads`).

### 0.5 APIs and places used (file:line at `e9611c272ee` unless marked `jamo:`)

| What | Where |
|---|---|
| render target enum, parser | `graphics/hires_text/font_map.h:40` (`kHiResTargetAuto, Clut8, Rgb565, Rgb888, Rgb555`), `:162` `HiResFontMap::parseRenderTarget` |
| engine picks the target from what the backend offers | `graphics/hires_text/hires_options.cpp:167-181` `hiResPickScreenFormat`, `:183-191` `hiResTargetsOffered`, `:199-207` `resolveRenderTarget` with `kPreference = {888, 565, 555}` |
| options struct and parser | `graphics/hires_text/hires_options.h:44-56`, `hires_options.cpp:29-32`, `:49-101` |
| blend, spans | `graphics/hires_text/text_compose.h` (`blendPixel`, `composeSpan`, `convertSpan`), `text_compose.cpp:54-153` |
| SCI composite | `engines/sci/graphics/drivers/upscaled.cpp:171` (call), `:184-214` `compositeText` |
| face cache, syllable cache | `graphics/hires_text/font_set.h:56-69`, `font_set.cpp:36-74`, `:100-124` |
| SvfFace constants, load modes | `graphics/hires_text/svfface.h:55-58`, `:66-76`; `font_map.h:49` `HiResLoadMode` |
| `data:` lookup | `graphics/hires_text/hires_files.cpp:45-55` (extrapath roots, `DATA_PATH`), `:58-67` `isSafeDataPath`, `:87-100` `locate` |
| engine fallback map | `engines/sci/graphics/hirestext.cpp:35` `kDefaultMap = "data:hires_text/maps/korean-default.map"` |
| Hangul toggle | `engines/sci/event.cpp:321-326` (`kSciActionHangulToggle`, `debugC(1, kDebugLevelKorean, "Hangul mode %s")`) |
| UTF-8 decode helper | `graphics/hires_text/font_set.h:134` `decodeUtf8` |
| saves | `backends/saves/default/default-saves.cpp:74-87` (`listSavefiles`, case-insensitive `matchString`), `:167` (`createWriteStream(false)`: saves are not atomic) |
| atomic writes | `backends/fs/stdiostream.cpp:38-48` (`STDIOSTREAM_NO_ATOMIC_SUPPORT` for DC, Atari, RISC OS) |
| DJGPP startup flags | `~/opt/djgpp/i586-pc-msdosdjgpp/sys-include/crt0.h:129` `_CRT0_FLAG_NO_LFN 0x0400`; `jamo:backends/platform/dos/dos.cpp:96` `int _crt0_startup_flags = _CRT0_FLAG_NONMOVE_SBRK;` |
| jamo couplings to cut | `jamo:backends/platform/dos/dos.cpp:50-51` (bitmap_font, keyed_compose includes), `:62` (file-cache-stats), `:83` (debugsocket-dosuart), `:147` (`FileCacheRegistry::summary`), `:207` (`KeyedCompose::setMmxAllowed`), `:246` (`hires_text_platform_scale`), `:260` (`HiResBitmapFont::setStreamThreshold`); `jamo:backends/graphics/dos/dos-graphics.cpp:129` (`hires_text_platform_truecolor`), `:141-158` (`getSupportedFormats` + cap); `jamo:backends/platform/dos/dos-modes.h:109-131` (`supportedFormats`), `:140-148` (`renderTargetCap`); `jamo:backends/mixer/dos/dos-mixer.cpp:44` (`kVorbisRefillSamples` assert) |
| kept as is | `jamo:backends/events/dos/dos-events.cpp:113` (`KEYCODE_HANGUL`, present in `ko-2026.4` from P2), `jamo:backends/platform/dos/dos-irq.cpp:70,160` (`DOS_KbdHangulChecked`) |
| build script | `jamo:backends/platform/dos/build-dos.sh` (builds in `$src/build-dos*`: in tree, replaced), `jamo:backends/platform/dos/module.mk` (wraps `malloc`, `_read`, `__dpmi_int`, `_exit`; links `-lemu`) |
| irq checker | `jamo:backends/platform/dos/irqcheck.py:35-47` `RANGES` (with `uart`), `:83-87` missing range = problem |
| launcher | `jamo:backends/platform/dos/launcher/{play.cpp,play-home.h,play-merge.h,play-sound.h,build-play.sh}`; `play-home.h:321-333` engine -> EXE (`agi` -> `AGI.EXE`) |
| host tests of the backend | `jamo:test/backends/dos_*.h` (18 files), wired by `jamo:test/module.mk` (`$(srcdir)/test/backends/dos_*.h`, skipped when `BACKEND=dos`) |

### 0.6 UNVERIFIED (said again where used)

- U1: that the `jamo` GUI-less build (`--disable-gui`, 25 upstream files) is still needed for the 16 MB budget: the
  prototype ran with the GUI in and had 4.6-5.8 MB free at the first frame (KQ1). SCUMM.EXE with codecs and MI1's
  talkie were not run (P7e Task e1 measures; decision D-3).
- U2: `[preload]` warm-up timing gains on DOS (the code is new, P7c Task c5).
- U3: LB1 and MI1 on DOS (not run in the prototype; P7g).
- U4: Staging with a mapper file for the Han/Yeong key (only DOSBox-X has `kor_hanyong`).
- U5: the exact bytes of the composer's output on DOS for a non-QWERTY DOS keyboard layout (KEYB KR is not loaded in
  the emulators).
- U6: the `jamo` SCUMM.EXE codec patches (Tremor chunk size, Vorbis refill 1024) against `ko-2026.4`'s
  `audio/decoders/vorbis.{h,cpp}` (+504 lines on `jamo`): not applied in the prototype.

### 0.7 UNVERIFIED-ON-HARDWARE (only a real PC can settle these; listed again in the final checklist)

- H1: real 486/Pentium speed of the composite and of glyph drawing (DOSBox-X counts instructions; `BLEND.EXE` of Task
  c7 is the program to run).
- H2: VBE 2.0 cards' mode lists (which of 640x400 / 640x480 at 15/16/32 bpp exist; S3, Cirrus, ATI, Matrox), LFB on
  ISA/VLB/PCI, and the bus cost of 16 vs 32 bpp flushes.
- H3: 6-bit vs 8-bit DAC in the 8 bpp mode (palette fidelity of CLUT8 text).
- H4: Sound Blaster IRQ/DMA variants (the SDL3 SB patches were proven on 86Box and an OPL3-SA3 by the owner before;
  not re-proven after the rebase).
- H5: Korean keyboards' Han/Yeong and Hanja scancodes (0xF2 / 0xF1, press only) through the BIOS/keyboard controller.
- H6: MS-DOS 6.22 / 7.1 8.3 truncation of `data:` paths (DOSBox-X proved it; real DOS does it the same way by
  specification, not by test), and Windows 9x DOS boxes with LFN (Task b4 turns LFN off: DJGPP `_CRT0_FLAG_NO_LFN`).
- H7: memory with real CWSDPMI + HIMEM/EMM386 configurations and a swap file on a real disk; `FILES=` limits.
- H8: the trailing-space `SET` quirk of real MS-DOS in the BATs (memory `scummvm-dos-port`).

---

## 1. Scope decisions and conflicts with DESIGN.md (listed again in the report)

1. **SDL3 patch count and base.** DESIGN 2.5/8 say "nine SDL3 patches" on `1ce4c5bc29`: they are nine patch *files*
   holding 14 effective commits (+ a keycode/revert pair that cancels). The series moves to upstream `main` `1362030`
   (3.5.0-dev); `release-3.4.20` has no DOS port at all (0.1). The pair is dropped in the rebase.
2. **Screen format order on DOS (conflict, owner decision D-1).** DESIGN 8: "RGB565 if the card has it, else
   XRGB8888, else 640x480 RGB565 with line repeat". `ko-2026.4`'s engine prefers 888 > 565 > 555 of what the backend
   offers (`hires_options.cpp:206-207`), so a card offering XRGB8888 at 640x400 (DOSBox-X and Staging both do) would get
   32 bit under `auto`. Plan: the DOS backend **offers XRGB8888 only when `render_target=rgb888` is set**, so `auto` on
   DOS = RGB565 (exact 640x400, else 640x480 line repeat), then XRGB1555, then CLUT8. This differs from DESIGN 8 in one
   case: a card with 640x400 XRGB8888 and only 640x480 RGB565 gets 565 + line repeat, not 8888 (half the bus traffic
   per frame; the measured flush of 640x480 is 8.2 ms at 16 bpp against 16.2 ms at 32 bpp on the DX2-66 preset,
   svf-fonts-open `c/bench/MEASUREMENTS.md` "Flush"). D-1 asks the owner to confirm; the alternative is one line in
   `DOS::supportedFormats`.
3. **Mode 13h.** DESIGN 8: "no mode 13h in play"; the brief lists "CLUT8 (mode 13h and VBE 8 bpp) with 1 bpp faces".
   Korean text needs the 2x plane, which 320x200 cannot hold, so mode 13h (or VBE 320x200x8) is used only by targets
   that draw no hi-res text (English targets, `hires_text=false`); every Korean target runs at VBE 640x400x8 (or a 15/16
   bpp mode). No 1x Korean path is added (the CP949 in-place path was dropped, decision 10).
4. **`data:` names are not 8.3.** `kDefaultMap` is `data:hires_text/maps/korean-default.map` (`hirestext.cpp:35`); the
   engine data folder is `hires_text/`. DOS truncates every component (`HIRES_TE\MAPS\KOREAN-D.MAP`, proven in DOSBox-X).
   Plan: no engine rename; the DOS staging script writes the truncated names and **refuses collisions**, and the backend
   sets `_CRT0_FLAG_NO_LFN` so a Windows 9x DOS box truncates the same way. (Renaming the engine data to 8.3 everywhere is
   the alternative; it would touch `ko/03-sci`; owner decision D-2.)
5. **Debug socket.** The P7 row names "UART debug socket"; `ko/07-debugsocket` is not in the tree (P3 Task 20, P4 b4 are
   pending on it too). P7 builds with `--disable-debug-socket`, `irqcheck.py` treats the `uart` range as optional, and
   acceptance uses window captures + log lines. The UART transport is the conditional sub-phase P7h. The memory
   re-measure that DESIGN 13 puts "over the socket" uses the backend's own `DOS: memory ...` log lines instead.
6. **The composite is not "under a frame"** on a 486 (0.3); the shared core gets an exact faster blend (P7c). This
   touches `graphics/hires_text/text_compose.{h,cpp}` (`ko/01-core`) and one call in `upscaled.cpp` (`ko/03-sci`), not
   DOS files.
7. **AGI.EXE.** AGI Korean (P5) is not implemented. P7 builds `AGI.EXE` from the unchanged upstream AGI engine
   (English games only) so `PLAY.EXE`'s engine table (`play-home.h:326`) has its EXE; no Korean AGI pack exists or is
   accepted. When P5 lands, AGI.EXE is rebuilt with no DOS change (P5's acceptance adds the DOS run).
8. **MI1 KO on DOS uses P6a (S1).** The P7 row accepts "KQ1, LB1, MI1 KO". `ko-2026.4` has SCUMM S1 only (UTF-8
   internal, legacy `korean.fnt` glyphs at 1x); `jamo`'s 2x SCUMM maps (`M1KO.MAP` etc.) belong to S2, which is not in the
   tree. MI1 KO is accepted on DOS as the S1 picture (the Linux P6a frame), at 320x200 CLUT8.
9. **Whole-file limit.** DESIGN 5.2 names `svf_whole_kb` (default 256); the code has a constant default and no key.
   P7c adds the engine-neutral key `hires_text_whole_kb` (default 256 everywhere; the DOS backend registers its own
   default, 64). This touches `graphics/hires_text/hires_options.{h,cpp}` and `engines/sci/graphics/hirestext.cpp:80`.
10. **Target names.** A target name longer than 8 characters loses its saves across sessions on DOS (0.2, last row).
    DESIGN 7 does not mention it. Plan: the DOS save manager maps 8.3 names back (P7e Task e4), and pack IDs/targets
    stay <= 8 characters by rule (harness dependency H-4).
11. **`-march`.** DJGPP's default is `-march=pentium -mtune=pentium` (`gcc -Q --help=target`); the settled target is a
    Pentium-class PC, so the build keeps it. A 486 then is not guaranteed to work (gcc rarely emits a Pentium-only
    instruction, but nothing checks it). `jamo`'s 486 measures (CPUID patch, `-lemu` for a 486SX) stay; a 486 build is
    `DOS_MARCH=i486` in `build-dos.sh` (Task b6), not the shipped one. Owner decision D-4.
12. **`--disable-gui`** (DESIGN 8 "compiled out: GUI") becomes a measured gate (D-3, Task e1): the GUI-less series
    touches 25 upstream files (`jamo`: `engines/engine.cpp` +78, `scumm.cpp` +269 incl. other work, `gui/*`, `base/main.cpp`,
    ...), the costliest part of every rebase. If every EXE keeps >= 2 MB free at its worst measured point with the GUI
    in, the series is not carried.

Decisions asked of the owner (the plan proceeds with the stated default and records the answer in `P7-notes.md`):
**D-1** 8888 only on request (default yes). **D-2** truncated staging vs 8.3 rename of engine data (default: staging).
**D-3** carry `--disable-gui` only if the memory gate fails (default). **D-4** ship `-march=pentium` (default).

---

## 2. Sub-phases and their order

| Sub-phase | Tasks | What | Depends on |
|---|---:|---|---|
| P7a SDL3 | 4 | rebase the series onto upstream `1362030`, build both prefixes, smoke gate, export the patch files | - |
| P7b Backend series | 10 | configure host, audio/base hooks, backend files adapted to `ko-2026.4`, fs fixes, out-of-tree build script, irqcheck, host tests, SCI.EXE first run, PATCHES.md | P7a |
| P7c Graphics and faces | 7 | DOS format list + tests, exact `Blend16` (TDD, exhaustive), SCI driver uses it, `hires_text_whole_kb`, `[preload]` warm-up, mode matrix runs, `BLEND.EXE` | P7b |
| P7d Input | 3 | Han/Yeong and Hanja on DOS, composer typing, mapper-file test | P7b |
| P7e Memory and 8.3 | 6 (e2 conditional) | memory gate, GUI-less series if needed, 8.3 data staging, save-name mapping, budget doc, LFN off | P7b, P7c |
| P7f EXEs, launcher, packages | 6 | PLAY.EXE, SCUMM.EXE + codecs, AGI.EXE, INI layout, engine zip staging, README | P7b, P7e |
| P7g Acceptance | 6 | runner, KQ1, LB1, MI1 from the two-zip layout in both emulators, Han/Yeong logged, notes | all above |
| P7h UART debug socket (conditional) | 3 | only after `ko/07-debugsocket` is merged | P7g, ko/07 |

Total **45 tasks** (44 when e2 is skipped, 42 when P7h waits). Review checkpoints after P7a, P7b, P7c, P7e and P7g.

---

## 3. Files

New (topic `ko/08-dos`; from `jamo` unless marked new):

- `backends/platform/dos/*` (dos.cpp, dos.h, dos-exit.*, dos-heap.*, dos-irq.*, dos-loading.*, dos-memory.h,
  dos-modes.h, dos-savefile.h, dos-silence.h, exit-trace.h, game-screen.h, lazy-overlay.h, line-repeat.h,
  loading-*.h, cursor-convert.h, soft-cursor.h, pit-chain.h, blaster.h, module.mk, README.md, build-dos.sh,
  build-deps.sh, irqcheck.py, detection-filter.py, mkcaption.py, `dos-vorbis-selftest.*` (SCUMM.EXE), `launcher/*`);
  **new**: `dos-savenames.h` (Task e4), `stage-data.py` + `test_stage_data.py` (Task e3), `tools/blendbench.cpp`
  (Task c7), `sdl3/*.patch` (14 files from `git format-patch`, replacing `jamo`'s nine `sdl3-*.patch`), `sdl3-build.txt`
  (rewritten).
- `backends/{graphics,events,timer,mixer,mutex}/dos/*`, `backends/midi/dos_mpu401.cpp`, `audio/dosopl.{h,cpp}`,
  `base/set-option.h`, `base/config-load.h` (only if Task b2 carries `--set=`; see there).
- `test/backends/dos_*.h` (host suites), **new** `test/backends/dos_savenames.h`, `test/graphics/hires_text_blend16.h`.

Modified upstream files (rows for `PATCHES.md`, Task b9): `configure`, `backends/module.mk`, `audio/adlib.cpp`,
`audio/audiostream.cpp`, `audio/fmopl.{h,cpp}`, `audio/mididrv.cpp`, `audio/mixer.h`, `audio/module.mk`,
`audio/rate.cpp`, `base/plugins.cpp`, `image/codecs/codec.cpp`, `engines/advancedDetector.cpp`,
`backends/fs/stdiostream.cpp`, `backends/fs/posix/posix-iostream.cpp`, `test/module.mk`; SCUMM.EXE only:
`audio/decoders/vorbis.{h,cpp}`; shared core (topics `ko/01-core`, `ko/03-sci`): `graphics/hires_text/text_compose.{h,cpp}`,
`graphics/hires_text/hires_options.{h,cpp}`, `graphics/hires_text/font_set.{h,cpp}`,
`engines/sci/graphics/drivers/upscaled.cpp`, `engines/sci/graphics/hirestext.cpp`. Conditional (D-3): the `jamo`
`DISABLE_GUI` hunks.

---

## 4. Environment (every task)

```sh
SRC=/home/thkim/work/scummvm-2026.4-korean        # the worktree you commit in (branch for the topic: ko/08-dos)
OLD=/home/thkim/work/scummvm-korean                # read only: git -C $OLD show jamo:<path>; never check out
HAR=/home/thkim/work/scummvm                       # read only (harness)
SDLSRC=~/opt/src/SDL3                              # SDL3 git (branches dos-*), upstream remote = origin
P7=~/work/tmp/p7                                   # scratch: prototype, runs, benches (never /tmp)
B=~/work/tmp/p7-build                              # build dirs: $B/host (Linux tests), $B/sci, $B/scumm, $B/agi
source ~/opt/dos-dev/env.sh                        # DJGPP on PATH, $SDL3_DOS, $CWSDPMI_EXE, $DOSBOX_STAGING
export TMPDIR=~/work/tmp/p7/tmp && mkdir -p $TMPDIR
```

- Builds are out of tree, never inside `$SRC` or any worktree; heavy steps run through `heavy` (`~/.local/bin/heavy`,
  `agentwork.slice`, at most 3 at once).
- Linux host test build (the CxxTest suites, including the DOS host suites):
  `mkdir -p $B/host && cd $B/host && heavy $SRC/configure --disable-all-engines --enable-engine=sci,scumm --enable-debug
  --disable-optimizations && heavy make -j8 test` -> last line `...OK!`. Task b0 records the count as **N0**.
- DOS builds: `heavy $SRC/backends/platform/dos/build-dos.sh sci|scumm|agi $B/<edition>` (Task b6 makes the build dir an
  argument). The prototype's equivalent configure line is in 0.2.
- Emulator runs: `$P7/bin/dosrun.sh EMU CONF OUT.png SECONDS [steps...]` (EMU `x` = DOSBox-X, `s` = Staging; steps:
  `sleep:N`, `type:TEXT`, xdotool key names); it starts its own Xvfb, grabs the emulator window with `xwd` and converts
  with `ffmpeg`. The prototype's confs are templates (`$P7/run-kq1/x-*.conf`, `s-*.conf`; DOSBox-X: `[sdl] output =
  surface`, `showmenu = false`; `[dosbox] machine = svga_s3`, `memsize = 16`; `[cpu] cputype = pentium`, `cycles =
  60000`; `[sblaster] sbtype = sb16`).
- Commit messages: `AREA: Subject` (`DOS: ...`, `CONFIGURE: ...`, `AUDIO: ...`, `BACKENDS: ...`, `GRAPHICS: ...`,
  `SCI: ...`, `TEST: ...`, `DOCS: Korean: ...`), a body, exactly one trailing line `Assisted-by: Claude Code`, no
  `Co-Authored-By`. Check: `git log -1 --format=%B > $TMPDIR/msg && python3 -I $SRC/devtools/check-commit-msg.py
  $TMPDIR/msg`.
- Korean keys of the built-in 2-set composer used below: 봐 `qhk`, 한글 `gksrmf`, 문을 `ansdmf`.

---

## 5. Harness-side dependencies (the harness repository is not modified by this plan)

- **H-1** `harness/dos/release/build_engine.py`: read the new staging (`build-dos.sh stage`, Task f5: `DATA\` with
  truncated `HIRES_TE\` names, SCI.EXE/SCUMM.EXE/AGI.EXE/PLAY.EXE/CWSDPMI.EXE) instead of `dist/dos` in the source tree.
- **H-2** game packs for DOS: `ko/` file names 8.3 and upper case (KQ1's are already 8.3), maps `version=3` (the P3/P4
  packs ship `version=2` maps, which the engine refuses and falls back to the default map, as the prototype logged),
  faces named `data:` with 8.3 names, `.HOT` lists for streamed faces (`[preload]`).
- **H-3** `play_test.py` / `twozip_test.py`: the new EXE set and log lines (`DOS: mode ...`, `Hi-res text: screen format
  ...`); window capture is already there (`dosgame.py:281-316` `capture_window`). Until then P7g uses `$P7/bin/dosrun.sh`.
- **H-4** pack IDs and targets <= 8 characters (`kq1kol`, `lb1ko`, `mi1ko`), `ID.INI` without `path=`.
- **H-5** the LB1 KO and MI1 KO packs in the new formats (P4 f-tasks, P6a) as `scummvm-dos-game-<id>-<sha>.zip`.
- **H-6** `k16said.py` over the UART (`--com`), only for P7h.

---

## P7a: SDL3 on current upstream

### Task a1: Rebase the patch series onto upstream `1362030`

**Files:** `~/opt/src/SDL3` (new branch `dos-p7-1362030`; the old branches stay).

- [ ] **Step 1:** fetch and confirm the facts of 0.1:

```sh
cd $SDLSRC && git fetch origin main --tags
git ls-tree -r --name-only release-3.4.20 | grep -c '/dos/'           # expect 0
git merge-base --is-ancestor 1ce4c5bc29 release-3.4.20 || echo "base not in 3.4.20"   # expect the echo
git ls-tree -r --name-only 1362030 | grep -c '/dos/'                  # expect 25
git log --oneline 1ce4c5bc29..dos-kbd-hangul | wc -l                  # expect 16
git diff --stat 8361783 7a588ba | wc -l                               # expect 0 (the keycode pair cancels)
```

- [ ] **Step 2:** rebase the 14 effective commits (the pair left out):

```sh
git checkout -b dos-p7-1362030 1362030
git cherry-pick 09c194c 11ca8a4 ef2105c 2df4ec6 fbd0d18 f6cc527 f5652a4 d67c987 c5ad490 1500973 9a55c71 3ed5eb3 45125d4 8361783
git log --oneline 1362030..HEAD | wc -l                               # expect 14
git diff --stat 1362030 HEAD | tail -1                                # expect "6 files changed, 539 insertions(+), 177 deletions(-)"
```

  Expected: no conflict (the prototype rebased all 16 cleanly). On a conflict stop and record it in `P7-notes.md`; the
  only upstream DOS edits since the base are joystick, thread name and cache path (0.1).

- [ ] **Step 3:** `git diff dos-kbd-hangul dos-p7-1362030 -- src/audio/dos src/video/dos src/cpuinfo src/core/dos`
  shows only upstream's changes, none of ours lost: read it.

### Task a2: Build both prefixes and check the link marks

- [ ] **Step 1:** keep the old prefixes (rename, never delete), then build:

```sh
mv ~/opt/sdl3-dos ~/opt/sdl3-dos.1ce4c5b && mv ~/opt/sdl3-dos-min-kbd ~/opt/sdl3-dos-min-kbd.1ce4c5b
T=$SDLSRC/build-scripts/i586-pc-msdosdjgpp.cmake
heavy cmake -S $SDLSRC -B $P7/sdl3-full -DCMAKE_TOOLCHAIN_FILE=$T -DCMAKE_BUILD_TYPE=Release \
  -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DCMAKE_INSTALL_PREFIX=$HOME/opt/sdl3-dos
heavy cmake --build $P7/sdl3-full -j8 && heavy cmake --install $P7/sdl3-full
heavy cmake -S $SDLSRC -B $P7/sdl3-min -DCMAKE_TOOLCHAIN_FILE=$T -DCMAKE_BUILD_TYPE=Release \
  -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_EXAMPLES=OFF -DSDL_LEAN_AND_MEAN=ON \
  -DSDL_RENDER=OFF -DSDL_GPU=OFF -DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF -DSDL_JOYSTICK=ON -DSDL_HAPTIC=OFF -DSDL_HIDAPI=OFF \
  -DSDL_SENSOR=OFF -DSDL_VIRTUAL_JOYSTICK=OFF -DSDL_CAMERA=OFF -DSDL_DUMMYCAMERA=OFF -DSDL_DIALOG=OFF -DSDL_TRAY=OFF \
  -DSDL_POWER=OFF -DSDL_DISKAUDIO=OFF -DSDL_DUMMYAUDIO=OFF -DSDL_DUMMYVIDEO=OFF -DSDL_OFFSCREEN=OFF \
  "-DCMAKE_C_FLAGS=-ffunction-sections -DSDL_DISABLE_STB" -DCMAKE_INSTALL_PREFIX=$HOME/opt/sdl3-dos-min-kbd
heavy cmake --build $P7/sdl3-min -j8 && heavy cmake --install $P7/sdl3-min
```

  (Configure in a fresh build dir: `SDL_REVISION` is read at configure time, `jamo:sdl3-build.txt`.)
- [ ] **Step 2:** marks and version:

```sh
for p in ~/opt/sdl3-dos ~/opt/sdl3-dos-min-kbd; do for m in DOS_IRQCodeChecked DOS_CPUIDChecked DOS_SBShutdownChecked \
  DOS_SBOpenFailChecked DOS_SBProbeChecked DOS_SBStatsChecked DOS_SBRingChunks DOS_KbdHangulChecked; do
  printf "%s %s " $p $m; i586-pc-msdosdjgpp-nm $p/lib/libSDL3.a | grep -c " [TDR] _$m\$"; done; done   # every line ends in 1
strings ~/opt/sdl3-dos-min-kbd/lib/libSDL3.a | grep -m1 'SDL-3\.5\.0-'                                   # SDL-3.5.0-<short sha of HEAD>
```

### Task a3: Smoke gate in both emulators

- [ ] **Step 1:** rebuild the smoke program against each prefix and run it:

```sh
cd ~/opt/dos-dev/smoke
for p in ~/opt/sdl3-dos ~/opt/sdl3-dos-min-kbd; do
  i586-pc-msdosdjgpp-gcc -O2 -I$p/include smoke.c -o SMOKE.EXE -L$p/lib -lSDL3 -lm
  rm -f SMOKE.LOG; timeout 120 xvfb-run -a dosbox-x -conf smoke-x.conf -nopromptfolder -fastlaunch >/dev/null 2>&1
  grep -v '^mode' SMOKE.LOG; grep '^mode 640x4' SMOKE.LOG
  rm -f SMOKE.LOG; timeout 120 xvfb-run -a $DOSBOX_STAGING -conf smoke.conf >/dev/null 2>&1
  grep -v '^mode' SMOKE.LOG; grep '^mode 640x4' SMOKE.LOG
done
```

  Expected for all four runs (prototype): `video=vesa audio=soundblaster`, `640x400 INDEX8 mode found`, `window ok
  surface 640x400 SDL_PIXELFORMAT_INDEX8`, `audio ok`, `timer 10ms x 1000ms -> 99 callbacks` (95-100), `done`;
  DOSBox-X lists 640x400 XRGB8888/RGB565/XRGB1555/INDEX8, Staging 640x400 XRGB8888/INDEX8 and 640x480
  XRGB8888/BGR24/RGB565/XRGB1555/INDEX8. **Gate:** any difference stops P7 here.
- [ ] **Step 2:** record the four logs' first lines and the mode lists in `$SRC/docs/korean/plans/P7-notes.md` (new
  file, created here, committed in Task b9 with the PATCHES rows).

### Task a4: Export the series for the ScummVM tree

- [ ] **Step 1:** `mkdir -p $P7/sdl3-series && git -C $SDLSRC format-patch -o $P7/sdl3-series 1362030..dos-p7-1362030`
  -> 14 files `0001-...patch` .. `0014-dos-Deliver-the-Korean-Han-Yeong-and-Hanja-keys-as-L.patch`.
- [ ] **Step 2:** they are committed in Task b4 as `backends/platform/dos/sdl3/` with the new `sdl3-build.txt` (base
  `1362030`, `git am` of the 14 files, both cmake lines of Task a2, the mark list). Nothing to commit here.

### Review checkpoint A (after Task a4)

A reviewer with fresh context reads `git -C $SDLSRC log -p 1362030..dos-p7-1362030 -- src/audio/dos` against
`git -C $SDLSRC log -p 1ce4c5bc29..dos-kbd-hangul -- src/audio/dos` (the same hunks, other context) and the four smoke
logs. Findings go back to the implementer (memory `review-fix-resume`).

---

## P7b: The backend series on `ko-2026.4`

### Task b0: Base and counts

- [ ] `cd $SRC && git switch -c ko/08-dos ko-2026.4 && git log -1 --oneline` (expect `e9611c272ee` or the newer
  `ko-2026.4` head; re-grep the file:line table of 0.5 if newer). Host build of section 4 -> record **N0** (`Running
  cxxtest tests (N0 tests)...OK!`) in `P7-notes.md`.

### Task b1: The DJGPP host and the dos backend in configure

**Files:** `configure`, `backends/module.mk`.

- [ ] **Step 1:** apply `jamo`'s hunks without the GUI and debug-socket parts:

```sh
cd $SRC && git -C $OLD diff 96b42d42bc8 jamo -- configure backends/module.mk > $TMPDIR/b1.diff
git apply --3way $TMPDIR/b1.diff      # the prototype applied it cleanly
```

  Then remove from `configure` again what this task does not carry: the `_debug_socket=auto` / `_gui=yes` defaults, the
  two `--disable-debug-socket` / `--disable-gui` help entries and option cases, and the two blocks "Check whether to
  build the debug socket" and "Check whether to build the GUI" (they come back with `ko/07-debugsocket` and, if D-3 says
  so, Task e2). Keep: `msdosdjgpp` in `get_system_exe_extension`, the `i?86-pc-msdosdjgpp)` host case, `msdosdjgpp` in
  the `gnu++` list, the `msdosdjgpp)` CXXFLAGS block (`-Wno-format`, `-ffunction-sections`, `--gc-sections`), the
  `i?86-pc-msdosdjgpp)` backend block (`_backend="dos"`, the `DISABLE_*` defines), `dos)` in the backend case
  (`-DDOS_DJGPP`, `pkg-config sdl3`), `dos` in the 16-bit list, `msdosdjgpp` in the POSIX list and the plain-`fseek` case.
- [ ] **Step 2:** a Linux configure is unchanged: `cd $B/host && heavy $SRC/configure --disable-all-engines
  --enable-engine=sci,scumm --enable-debug --disable-optimizations | grep -E '^Backend'` -> `Backend... sdl ...` as
  before; `diff <(grep -v SAVED config.mk) <(...)` against a configure of `ko-2026.4` shows no difference.
- [ ] **Step 3:** the DOS configure (prototype line of 0.2 into `$B/sci`, with `PKG_CONFIG_LIBDIR=$SDL3_DOS/lib/pkgconfig`
  where `SDL3_DOS=~/opt/sdl3-dos-min-kbd`): `Backend... dos, assembly routines, 16bit color, high resolution, savegame
  timestamp`, `config.mk` has `BACKEND := dos` and `DEFINES += ... -DDOS_DETECTION_FILTER -DDOS_DJGPP ...`.
- [ ] **Step 4:** commit:

```
CONFIGURE: Add the DJGPP (MS-DOS) host and the dos backend

i?86-pc-msdosdjgpp selects the dos backend: SDL3 (pkg-config sdl3)
as the hardware layer, no dynamic modules, networking, OpenGL,
scalers or software OPL emulators, and the defines that leave out
code no DOS game reaches (Windows/Mac video codecs, SCI Mac audio
and FM-Towns/PC-98 music, detection archives, the NES APU). Every
function gets its own section and the link drops the unused ones:
the program lives in a 16 MB machine.

The null mixer is linked for the dos backend as for null (a PC
without a Sound Blaster), and the MPU-401 driver.

Assisted-by: Claude Code
```

### Task b2: Audio and plugin hooks

**Files:** `audio/{adlib,audiostream,fmopl,mididrv,rate}.cpp`, `audio/{fmopl,mixer}.h`, `audio/module.mk`,
`base/plugins.cpp`, `audio/dosopl.{h,cpp}`, `backends/midi/dos_mpu401.cpp`.

- [ ] **Step 1:** `git -C $OLD diff 96b42d42bc8 jamo -- audio/adlib.cpp audio/audiostream.cpp audio/fmopl.cpp
  audio/fmopl.h audio/mididrv.cpp audio/mixer.h audio/module.mk audio/rate.cpp base/plugins.cpp > $TMPDIR/b2.diff &&
  git apply --3way $TMPDIR/b2.diff` (clean in the prototype), and `git -C $OLD archive jamo audio/dosopl.h
  audio/dosopl.cpp backends/midi/dos_mpu401.cpp | tar -x -C $SRC`.
- [ ] **Step 2:** the `LoggingOPL` (`opl_log=` key, about 250 lines in `fmopl.cpp`) is a debugging aid of `jamo`; keep
  it only if the owner wants the register log on DOS (it is compiled for every platform). Default: **drop it** (remove
  the class and the `friend` line, keep `createUnlogged` folded back into `Config::create`). Record the choice.
- [ ] **Step 3:** Linux host build `make -j8 && make test` -> N0 OK, no new warning in `audio/`.
- [ ] **Step 4:** commit `AUDIO: Drive the hardware OPL and MPU-401 of a DOS PC` (body: the OPL chip at 0x388 used
  directly, detection returns no driver when the chip cannot serve the type so SCI falls back to OPL2, device warnings
  go to the log on DOS where no dialog is shown, a 16-bit-only rate converter, the latency hook for lip sync; trailer).

### Task b3: Leave out decoders no DOS game reaches

**Files:** `image/codecs/codec.cpp`, `engines/advancedDetector.cpp`.

- [ ] `git -C $OLD diff 96b42d42bc8 jamo -- image/codecs/codec.cpp engines/advancedDetector.cpp | git apply --3way`;
  Linux build and N0 OK; commit `DOS: Build without video codecs and detection archives` (body names
  `DISABLE_VIDEO_CODECS`, `DISABLE_DETECTION_ARCHIVES`, both set only by the msdosdjgpp configure block). The SCI
  `DISABLE_SCI_*` and `DOS_DETECTION_FILTER` hunks (`engines/sci/{console,engine/kvideo,sound/music,sound/audio,
  detection,module.mk}`) are **not** carried now: they save executable size only; Task e1 decides with numbers.

### Task b4: The backend files, adapted to `ko-2026.4`

**Files:** new `backends/platform/dos/*`, `backends/{graphics,events,timer,mixer,mutex}/dos/*`,
`backends/platform/dos/sdl3/*.patch` (Task a4), `backends/platform/dos/sdl3-build.txt`.

- [ ] **Step 1:** copy: `git -C $OLD archive jamo backends/platform/dos backends/graphics/dos backends/events/dos
  backends/timer/dos backends/mixer/dos backends/mutex/dos | tar -x -C $SRC`; then `git rm -q --cached` nothing yet;
  delete `backends/platform/dos/sdl3-*.patch` and the `jamo`-only tools this series does not use
  (`mkute.py`, `test_mkute.py`, `check-tremor-cache.sh`, `test_check_tremor_cache.py`, `tremor-check.c`, `patches/` move
  to Task f2 with SCUMM.EXE); copy `$P7/sdl3-series/*.patch` to `backends/platform/dos/sdl3/`.
- [ ] **Step 2 (`dos.cpp`):** remove the includes `graphics/hires_text/bitmap_font.h`,
  `graphics/hires_text/keyed_compose.h`, `common/file-cache-stats.h`; wrap `#include "gui/debugsocket-dosuart.h"` and
  every `GUI::DosUart::irqCount()` use (`jamo:dos.cpp:718`, `:744`, `:845`) in `#ifdef USE_DEBUG_SOCKET` with `0`
  otherwise; delete the `FileCacheRegistry` block (`:144-151`), the `KeyedCompose::setMmxAllowed` lines (`:206-207`),
  the `dos_svf_preload_kb` block (`:249-262`; replaced by `hires_text_whole_kb`, Task c4), and
  `ConfMan.registerDefault("hires_text_platform_scale", "2")` (`:246`; `ko-2026.4` has no such key). Set
  `int _crt0_startup_flags = _CRT0_FLAG_NONMOVE_SBRK | _CRT0_FLAG_NO_LFN;` (`:96`) with the comment "8.3 names on every
  DOS: a Windows 9x box truncates the data: paths as MS-DOS does (stage-data.py writes them so)".
- [ ] **Step 3 (`dos-modes.h`, `dos-graphics.cpp`):** include `graphics/hires_text/font_map.h` instead of
  `hires_options.h`; delete `autoTrueColorName` and its registration (`jamo:dos-graphics.cpp:126-130`); replace
  `supportedFormats` with:

```cpp
/**
 * What getSupportedFormats() reports for a w x h game: the true-colour
 * formats chooseMode() can set (exactly or via the line-repeat fallback),
 * then CLUT8, which is always there. The engine picks among them
 * (Graphics::resolveRenderTarget(): 888, then 565, then 555 of what is
 * offered), so 4-byte XRGB8888 is offered only when @p cap asks for rgb888:
 * on a DOS bus 16 bit costs half the copy, and DESIGN section 8 makes 5-6-5,
 * then 1-5-5-5, the DOS default. clut8 reports CLUT8 alone.
 */
inline Common::List<Graphics::PixelFormat> supportedFormats(const Common::Array<VideoMode> &modes, uint w, uint h,
															 Graphics::HiResRenderTarget cap = Graphics::kHiResTargetAuto) {
	Common::Array<Graphics::PixelFormat> order;
	if (cap == Graphics::kHiResTargetRgb888)
		order.push_back(xrgb8888());
	if (cap != Graphics::kHiResTargetClut8) {
		order.push_back(rgb565());
		order.push_back(xrgb1555());
	}
	Common::List<Graphics::PixelFormat> out;
	for (uint i = 0; i < order.size(); ++i)
		if (chooseMode(modes, w, h, order[i], false).index >= 0)
			out.push_back(order[i]);
	out.push_back(Graphics::PixelFormat::createFormatCLUT8());
	return out;
}
```

  and in `renderTargetCap` call `Graphics::HiResFontMap::parseRenderTarget`; the warning text names `auto, clut8,
  rgb565, rgb555 or rgb888`.
- [ ] **Step 4 (`dos-mixer.cpp`):** the `static_assert(Audio::kVorbisRefillSamples == 1024, ...)` (`jamo:44`) moves
  under `#ifdef USE_VORBIS` (it returns with the vorbis hunk in Task f2).
- [ ] **Step 5:** rewrite `sdl3-build.txt`: base `libsdl-org/SDL 1362030` (2026-11-23), "release-3.4.20 has no DOS
  port", `git -C SDL3 checkout 1362030 && git -C SDL3 am backends/platform/dos/sdl3/*.patch`, the two cmake lines of
  Task a2, the mark list (eight symbols), the "what stays / what goes" paragraphs of `jamo`'s file unchanged.
- [ ] **Step 6:** build the DOS edition by hand as in 0.2 (Task b6 adds the script): `make` ends with `scummvm.exe`; no
  error. Commit:

```
DOS: Add the MS-DOS backend

OSystem_DOS over SDL3 (DJGPP): VESA modes through SDL's window
surface with a 640x480 line-repeat fallback for 640x400 pictures, a
1 kHz IRQ0 timer, cli/popf mutexes, the Sound Blaster through SDL3 and
the null mixer without one, the keyboard and mouse, saves that yield
to the audio thread, a loading screen, exit tracing and memory locking
for the interrupt handlers.

The backend offers the screen formats it can set; the engine chooses.
XRGB8888 is offered only when render_target asks for rgb888, so the
automatic choice on DOS is 5-6-5, then 1-5-5-5, then CLUT8. Long file
names are off: every DOS truncates names to 8.3 the same way.

The SDL3 patches it needs are in sdl3/, on SDL 1362030 (sdl3-build.txt).

Assisted-by: Claude Code
```

### Task b5: 8.3-safe writes and fast sizes on DJGPP

**Files:** `backends/fs/stdiostream.cpp`, `backends/fs/posix/posix-iostream.cpp`.

- [ ] **Step 1 (red, emulator):** the Task b4 build logs `WARNING: Unable to write configuration file: C:\SCUMMVM\SCUMMVM.INI!`
  twice on the KQ1 run of 0.2 (it did in the prototype).
- [ ] **Step 2:** in `stdiostream.cpp` after the RISC OS block add

```cpp
#if defined(__DJGPP__)
// DOS file names are 8.3: NAME.EXT.tmp cannot be created
#define STDIOSTREAM_NO_ATOMIC_SUPPORT
#endif
```

  and in `posix-iostream.cpp` `PosixIoStream::size()` the `jamo` `#if defined(__DJGPP__) return StdioStream::size();`
  branch (DJGPP's `fstat` takes about 200 ms under DOSBox and SCI asks a volume's size per resource). The `jamo`
  `unbuffer()` machinery (+85 lines) is **not** carried: nothing in `ko-2026.4` calls `Common::unbufferStream`.
- [ ] **Step 3 (green):** rebuild, rerun: no "Unable to write" line; `SCUMMVM.INI` gained the `versioninfo=` line.
- [ ] **Step 4:** commit `BACKENDS: Write files in place and size them by seeking on DJGPP` (body: the two reasons;
  trailer).

### Task b6: The build script, out of tree

**Files:** `backends/platform/dos/build-dos.sh`, `backends/platform/dos/module.mk`, `backends/platform/dos/irqcheck.py`.

- [ ] **Step 1:** `build-dos.sh EDITION BUILD_DIR [configure args]`: `out="$2"` (required, refused when it is inside `$src`:
  `case "$(realpath -m "$out")/" in "$src"/*) echo "build-dos.sh: build outside the source tree" >&2; exit 1;; esac`);
  the svf-fonts submodule check names `dists/svf-fonts-open/svf/dos/NEODG16A.SVF` (the old `dists/svf-fonts/...SANGAM16.SVF`
  does not exist in this tree); `conf_args` gain `--disable-debug-socket` only when `configure --help` lists it (after
  ko/07), never `--disable-gui` unless Task e2 ran; the SDL mark checks become one loop over the eight names of Task a2;
  a new check that `$SDL3_DOS/lib/libSDL3.a` carries the revision of `sdl3-build.txt`:
  `strings "$sdl_lib" | grep -q "SDL-3\.5\.0-" || { echo "build-dos.sh: $sdl_lib is not SDL 3.5.0 (sdl3-build.txt)"; exit 1; }`;
  `DOS_MARCH` (empty = DJGPP default pentium; `i486` adds `-march=i486 -mtune=pentium` to CXXFLAGS, D-4); staging
  moves to `stage` (Task f5).
- [ ] **Step 2 (`irqcheck.py`):** the `uart` range is optional:

```python
# Ranges a build may leave out whole: the UART debug transport exists only with --enable-debug-socket.
OPTIONAL = {"uart"}
```

  and in `main`, before the loop, `where = {n: a for a, _, n in syms}`; in the loop replace the missing-range branch by

```python
        if "Begin" not in r or "End" not in r:
            if tag in OPTIONAL and not any(h in where for h in handlers):
                print("irqcheck: %s: not in this build" % tag)
                continue
            problems.append("%s: no range (labels _dosIrqBegin_%s/_dosIrqEnd_%s missing)" % (tag, tag, tag))
            continue
```

- [ ] **Step 3:** `heavy $SRC/backends/platform/dos/build-dos.sh sci $B/sci` -> `irqcheck: uart: not in this build`,
  `irqcheck: dos [...] 0 indirect exit(s)`, `kbd`, `sb`, `timer [...] 2 indirect exit(s)` (prototype values), rc 0;
  `build-dos.sh sci $SRC/build-dos` -> "build outside the source tree", rc 1.
- [ ] **Step 4:** commit `DOS: Build out of tree and check SDL 3.5.0` (trailer).

### Task b7: Host unit tests of the backend

**Files:** `test/backends/dos_*.h` (from `jamo`), `test/module.mk`.

- [ ] **Step 1:** `git -C $OLD archive jamo test/backends | tar -x -C $SRC` keeping only `dos_*.h`; wire them as
  `jamo` did: `TESTS += $(srcdir)/test/backends/dos_*.h` inside `ifneq ($(BACKEND),dos)` (the DOS build never runs
  them). `dos_prefetch.h`, `dos_audio_config.h`, `dos_audio_stats.h` need the mixer headers only; `dos_play_*.h` include
  the launcher headers (no ScummVM code).
- [ ] **Step 2 (red):** `make test` fails to compile `dos_modes.h` (`autoTrueColorName`, the old cap lists). Rewrite
  the fixtures and cases for the new contract (measured lists of 0.1):

```cpp
	static Common::Array<DOS::VideoMode> staging() {
		// DOSBox Staging 0.83 svga_s3, measured 2026-11-23 (P7 plan 0.1): 640x400 only 8888 and 8 bpp.
		Common::Array<DOS::VideoMode> m;
		const DOS::VideoMode l[] = { { 640, 400, DOS::xrgb8888() }, { 640, 400, Graphics::PixelFormat::createFormatCLUT8() },
			{ 640, 480, DOS::xrgb8888() }, { 640, 480, DOS::rgb565() }, { 640, 480, DOS::xrgb1555() },
			{ 640, 480, Graphics::PixelFormat::createFormatCLUT8() }, { 320, 200, Graphics::PixelFormat::createFormatCLUT8() } };
		for (uint i = 0; i < ARRAYSIZE(l); ++i)
			m.push_back(l[i]);
		return m;
	}
	static Common::Array<DOS::VideoMode> dosboxX() {
		Common::Array<DOS::VideoMode> m = staging();
		const DOS::VideoMode e = { 640, 400, DOS::rgb565() }, f = { 640, 400, DOS::xrgb1555() };
		m.push_back(e);
		m.push_back(f);
		return m;
	}
	static bool sameList(const Common::List<Graphics::PixelFormat> &got, const Graphics::PixelFormat *want, uint n) {
		if (got.size() != n)
			return false;
		uint i = 0;
		for (Common::List<Graphics::PixelFormat>::const_iterator it = got.begin(); it != got.end(); ++it, ++i)
			if (!(*it == want[i]))
				return false;
		return true;
	}

public:
	void test_auto_offers_16_then_15_bit_then_clut8() {
		const Graphics::PixelFormat want[] = { DOS::rgb565(), DOS::xrgb1555(), Graphics::PixelFormat::createFormatCLUT8() };
		TS_ASSERT(sameList(DOS::supportedFormats(dosboxX(), 640, 400), want, 3));
		TS_ASSERT(sameList(DOS::supportedFormats(staging(), 640, 400), want, 3));	// both through the 640x480 line repeat
	}
	void test_rgb888_only_when_asked() {
		const Graphics::PixelFormat want[] = { DOS::xrgb8888(), DOS::rgb565(), DOS::xrgb1555(),
			Graphics::PixelFormat::createFormatCLUT8() };
		TS_ASSERT(sameList(DOS::supportedFormats(dosboxX(), 640, 400, Graphics::kHiResTargetRgb888), want, 4));
	}
	void test_clut8_alone() {
		const Graphics::PixelFormat want[] = { Graphics::PixelFormat::createFormatCLUT8() };
		TS_ASSERT(sameList(DOS::supportedFormats(dosboxX(), 640, 400, Graphics::kHiResTargetClut8), want, 1));
	}
	void test_engine_auto_picks_565_on_dos() {
		// The engine's choice over the DOS list (an AA map, nothing configured): 5-6-5, not 8888.
		const uint32 offered = Graphics::hiResTargetsOffered(DOS::supportedFormats(dosboxX(), 640, 400));
		Common::String note;
		TS_ASSERT_EQUALS(Graphics::resolveRenderTarget(Graphics::HiResOptions(), nullptr, true, offered, note),
		                 Graphics::kHiResTargetRgb565);
		Graphics::PixelFormat f;
		TS_ASSERT(Graphics::hiResPickScreenFormat(Graphics::kHiResTargetRgb555, DOS::supportedFormats(dosboxX(), 640, 400), f));
		TS_ASSERT(f == DOS::xrgb1555());
	}
```

  Keep `jamo`'s `findExactMode`, `chooseMode` and `formatsSize` cases (their fixtures change only by the extra
  modes); delete `test_auto_true_colour_follows_the_cards_modes` and the cap tests that assert the old lists; keep
  `test_render_target_cap_only_while_a_game_runs` with `rgb555` added as valid. `dos_modes.h` now needs
  `graphics/libgraphics.a` (already in `TEST_LIBS`, `test/module.mk:50`).
- [ ] **Step 3 (green):** `make test` -> **N0 + k** OK (k = the DOS suites' case count, printed per suite with
  `./test/runner DosModesTestSuite` etc.); record k.
- [ ] **Step 4:** mutation check: change `rgb565()` before `xrgb1555()` order in `supportedFormats` -> 
  `test_auto_offers_16_then_15_bit_then_clut8` fails; drop the `cap == Rgb888` condition -> `test_engine_auto_picks_565_on_dos`
  fails. Revert. Commit `TEST: Test the DOS backend's pure parts on the host` (trailer).

### Task b8: SCI.EXE first run (gate)

- [ ] **Step 1:** stage a run folder as the prototype did (`$P7/run-kq1/c`: `SCUMMVM\SCI.EXE`, `CWSDPMI.EXE`,
  `DATA\HIRES_TE\NEODG16A.SVF`, `DATA\HIRES_TE\MAPS\KOREAN-D.MAP`, `DATA\ENCODING.DAT`, `KQ1\` = the P3 pack with
  upper-case `KO\` names, `SCUMMVM.INI` with `extrapath=C:\SCUMMVM\DATA` and targets of <= 8 characters).
- [ ] **Step 2:** `$P7/bin/dosrun.sh x $P7/run-kq1/x.conf $P7/shots/b8-title.png 40` and the `rgb565` and `clut8` runs
  of 0.2 with `Right Return sleep:20 type:qhk Return sleep:6`. Expected log lines and frames: those of the 0.2 table
  (title identical to `dos-kq1-x-clut8-title.png`; rgb565 status line + box identical to Linux
  `aa-kq1-rgb565-message.png` outside the cursor), compared with the PIL snippet:

```python
from PIL import Image, ImageChops
def diff(a, b, box):
    d = ImageChops.difference(Image.open(a).convert('RGB').crop(box), Image.open(b).convert('RGB').crop(box))
    return sum(1 for p in d.getdata() if p != (0, 0, 0))
# boxes: status (0,0,640,20), message left (115,170,310,252), right (352,170,525,252) - the cursor sits between
```

  **Gate:** any other result stops P7b.

### Task b9: PATCHES.md and notes

- [ ] Add one `ko/08-dos` row per upstream file of section 3 (file, change, why) to `docs/korean/PATCHES.md`; commit
  with `P7-notes.md`: `DOCS: Korean: Record the DOS series in PATCHES.md` (trailer).

### Review checkpoint B (after Task b9)

- `make test` N0 + k OK; a Linux ASan host build (`--enable-asan`, `$B/asan`) N0 + k OK, 0 sanitizer lines.
- `git diff ko-2026.4..ko/08-dos --stat -- ':!backends/*/dos' ':!backends/platform/dos' ':!test/backends' ':!docs'`
  lists only the files of section 3, each in PATCHES.md.
- Upstream inertness: every non-DOS hunk is behind `DOS_DJGPP`, `__DJGPP__`, `BACKEND=dos`, or a `DISABLE_*` define set
  only by the msdosdjgpp block; a Linux configure + build is byte-identical in `config.h`/`config.mk` to `ko-2026.4`'s.
- A reviewer with fresh context reads the diff; findings go back to the implementer; fixes are separate commits listed
  in `P7-notes.md`.

---

## P7c: Graphics and faces

### Task c1: DOS format list in the emulators (no code: confirms Task b4/b7 end to end)

- [ ] Runs with `render_target` = `auto` (AA pair map), `rgb565`, `rgb555`, `rgb888`, `clut8`, and `auto` +
  `dos_force_fallback=true`, in DOSBox-X; `auto` and `rgb565` in Staging. Expected `DOS: mode` / `Hi-res text: screen
  format` lines:

| Emulator | render_target | expected |
|---|---|---|
| X | auto | `640x400 RGB565@2` / `RGB565@2 for render target rgb565` |
| X | rgb555 | `640x400 RGB555@2` (XRGB1555) |
| X | rgb888 | `640x400 XRGB8888@4` (format string as the backend logs it) |
| X | clut8 | `640x400 CLUT8` |
| X | auto + `dos_force_fallback=true` | `640x480 RGB565@2 (line repeat from 640x400)` |
| Staging | auto, rgb565 | `640x480 RGB565@2 (line repeat from 640x400)` |

  Record the lines in `P7-notes.md`. (rgb888 and the forced fallback were not run in the prototype.)

### Task c2: `TextCompose::Blend16`, exact and table-driven (TDD)

**Files:** `graphics/hires_text/text_compose.{h,cpp}`, new `test/graphics/hires_text_blend16.h`.

- [ ] **Step 1 (red):** the test (exhaustive over 15/16-bit formats; differential against `blendPixel`):

```cpp
#include <cxxtest/TestSuite.h>

#include "graphics/hires_text/text_compose.h"

// Blend16 is blendPixel by table: every 2-byte pixel, every partial coverage, colours at the channel edges and between.
class HiResTextBlend16TestSuite : public CxxTest::TestSuite {
	static const Graphics::PixelFormat *formats(uint &n) {
		static const Graphics::PixelFormat f[] = {
			Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0),  // RGB565
			Graphics::PixelFormat(2, 5, 5, 5, 0, 10, 5, 0, 0),  // RGB555
			Graphics::PixelFormat(2, 5, 5, 5, 1, 10, 5, 0, 15), // ARGB1555
			Graphics::PixelFormat(2, 5, 5, 5, 1, 11, 6, 1, 0),  // RGBA5551
			Graphics::PixelFormat(2, 5, 6, 5, 0, 0, 5, 11, 0)   // BGR565
		};
		n = ARRAYSIZE(f);
		return f;
	}

public:
	void test_everyPixelEveryCoverage() {
		static const byte fg[][3] = { { 0, 0, 0 }, { 255, 255, 255 }, { 255, 255, 85 }, { 0, 170, 170 }, { 83, 7, 201 }, { 170, 0, 0 } };
		uint nf;
		const Graphics::PixelFormat *f = formats(nf);
		uint32 bad = 0;
		for (uint i = 0; i < nf; ++i) {
			Graphics::TextCompose::Blend16 table(f[i]);
			for (uint c = 0; c < ARRAYSIZE(fg); ++c)
				for (uint cov = 1; cov < 255; ++cov)
					for (uint32 d = 0; d < 65536; ++d)
						if (table.blend((uint16)d, fg[c][0], fg[c][1], fg[c][2], (byte)cov) !=
						    (uint16)Graphics::TextCompose::blendPixel(f[i], d, fg[c][0], fg[c][1], fg[c][2], (byte)cov))
							++bad;
		}
		TS_ASSERT_EQUALS(bad, 0u);
	}

	void test_composeSpanEqualsBlendPixel() {
		// Random rows through composeSpan, with a table across rows and without one; coverage 0, 255 and partial.
		uint nf;
		const Graphics::PixelFormat *f = formats(nf);
		byte pal[256 * 3];
		uint32 s = 7;
		for (int i = 0; i < 768; ++i) {
			s = s * 1103515245 + 12345;
			pal[i] = (byte)(s >> 16);
		}
		for (uint i = 0; i < nf; ++i) {
			Graphics::TextCompose::Blend16 table(f[i]);
			for (int row = 0; row < 16; ++row) {
				uint16 dst[640], viaTable[640], ref[640];
				byte idx[640], cov[640];
				for (int x = 0; x < 640; ++x) {
					s = s * 1103515245 + 12345;
					dst[x] = viaTable[x] = ref[x] = (uint16)(s >> 8);
					idx[x] = (byte)(s >> 3);
					cov[x] = (x % 7 == 0) ? 0 : (x % 5 == 0) ? 255 : (byte)(s >> 13);
				}
				Graphics::TextCompose::composeSpan((byte *)dst, f[i], idx, cov, 640, pal, Graphics::TextCompose::kBlend);
				Graphics::TextCompose::composeSpan((byte *)viaTable, f[i], idx, cov, 640, pal, Graphics::TextCompose::kBlend, &table);
				for (int x = 0; x < 640; ++x) {
					const byte *c = pal + idx[x] * 3;
					const uint16 want = cov[x] ? (uint16)Graphics::TextCompose::blendPixel(f[i], ref[x], c[0], c[1], c[2], cov[x]) : ref[x];
					TS_ASSERT_EQUALS(dst[x], want);
					TS_ASSERT_EQUALS(viaTable[x], want);
				}
			}
		}
	}
};
```

  `make test` does not compile (`Blend16` unknown).
- [ ] **Step 2 (green):** in `text_compose.h` before `stampSpan`:

```cpp
/**
 * blendPixel() on a 2-byte format by table. For one text colour and coverage the blended value of every channel value is
 * computed once (at most 32 + 64 + 32 entries) and then looked up: three lookups and an OR a pixel instead of three
 * multiply-divides, which an emulated 486DX2-66 runs at 8.8 us a covered pixel (P7 plan, section 0.3). The result is
 * blendPixel()'s, bit for bit. Four slots, chosen by coverage >> 6, keep the pairs a line of a 2 bpp face alternates
 * between (85 and 170); any other coverage only costs a rebuild. Keep one object across the rows of a rectangle.
 */
class Blend16 {
public:
	explicit Blend16(const PixelFormat &fmt);
	/** blendPixel(fmt, dst, r, g, b, cov); @p cov 1..254 (0 and 255 are the caller's: nothing, or the colour). */
	uint16 blend(uint16 dst, byte r, byte g, byte b, byte cov) {
		Slot &s = _slot[cov >> 6];
		const uint32 key = (uint32)r << 24 | (uint32)g << 16 | (uint32)b << 8 | cov;
		if (s.key != key)
			fill(s, key, r, g, b, cov);
		return (uint16)(s.r[dst >> _rShift & _rMask] | s.g[dst >> _gShift & _gMask] | s.b[dst >> _bShift & _bMask] | _alpha);
	}

private:
	struct Slot {
		uint32 key;	// r << 24 | g << 16 | b << 8 | cov; cov 0 (never asked for) marks an empty slot
		uint16 r[64], g[64], b[64];
	};
	void fill(Slot &s, uint32 key, byte r, byte g, byte b, byte cov);

	byte _rShift, _gShift, _bShift, _rLoss, _gLoss, _bLoss;
	uint16 _rMask, _gMask, _bMask, _alpha;
	Slot _slot[4];
};
```

  and the two signatures gain a last parameter `Blend16 *table = nullptr` (`composeSpan`, `convertSpan`; doc line: "On a
  2-byte format kBlend goes through @p table when given (else one made for this call)"). In `text_compose.cpp` add
  `#include "common/ptr.h"` and, replacing the anonymous-namespace `putText` and the two span functions:

```cpp
Blend16::Blend16(const PixelFormat &fmt) {
	_rShift = fmt.rShift;
	_gShift = fmt.gShift;
	_bShift = fmt.bShift;
	_rLoss = fmt.rLoss;
	_gLoss = fmt.gLoss;
	_bLoss = fmt.bLoss;
	_rMask = (uint16)((1u << (8 - _rLoss)) - 1);
	_gMask = (uint16)((1u << (8 - _gLoss)) - 1);
	_bMask = (uint16)((1u << (8 - _bLoss)) - 1);
	_alpha = (uint16)fmt.RGBToColor(0, 0, 0);	// the alpha field, opaque, as blendPixel() sets it
	for (int i = 0; i < 4; ++i)
		_slot[i].key = 0;
}

void Blend16::fill(Slot &s, uint32 key, byte r, byte g, byte b, byte cov) {
	// blendPixel()'s arithmetic, one channel value at a time.
	const uint32 fr = r >> _rLoss, fg = g >> _gLoss, fb = b >> _bLoss;
	for (uint32 v = 0; v <= _rMask; ++v)
		s.r[v] = (uint16)(((v * (255 - cov) + fr * cov + 127) / 255) << _rShift);
	for (uint32 v = 0; v <= _gMask; ++v)
		s.g[v] = (uint16)(((v * (255 - cov) + fg * cov + 127) / 255) << _gShift);
	for (uint32 v = 0; v <= _bMask; ++v)
		s.b[v] = (uint16)(((v * (255 - cov) + fb * cov + 127) / 255) << _bShift);
	s.key = key;
}

namespace {

// The pixel @p dst of @p fmt with the text of palette index @p index at @p coverage put over it. Coverage 255 is the
// colour itself (blendPixel gives (255 * fg + 127) / 255 = fg per channel), without the arithmetic.
inline uint32 putText(const PixelFormat &fmt, uint32 dst, byte index, byte coverage, const byte *pal, Mode mode,
                      Blend16 *table) {
	const byte *c = pal + index * 3;
	if (mode == kBlend && coverage != 255) {
		if (table)
			return table->blend((uint16)dst, c[0], c[1], c[2], coverage);
		return blendPixel(fmt, dst, c[0], c[1], c[2], coverage);
	}
	return coverage < 128 ? dst : fmt.RGBToColor(c[0], c[1], c[2]);
}

} // End of anonymous namespace

void composeSpan(byte *dst, const PixelFormat &fmt, const byte *index, const byte *coverage, int n,
                 const byte *paletteRGB, Mode mode, Blend16 *table) {
	const int bpp = fmt.bytesPerPixel;
	Common::ScopedPtr<Blend16> own;
	if (bpp == 2 && mode == kBlend && !table) {
		own.reset(new Blend16(fmt));
		table = own.get();
	} else if (bpp != 2) {
		table = nullptr;
	}
	if (table && mode == kBlend) {
		// The 2-byte blend, the hot path on DOS (15/16-bit VBE): the colour of the last index is kept for coverage 255.
		int lastIndex = -1;
		uint16 solid = 0;
		for (int i = 0; i < n; ++i, dst += 2) {
			const byte cov = coverage[i];
			if (!cov)
				continue;
			const byte *c = paletteRGB + index[i] * 3;
			if (cov == 255) {
				if (index[i] != lastIndex) {
					lastIndex = index[i];
					solid = (uint16)fmt.RGBToColor(c[0], c[1], c[2]);
				}
				WRITE_UINT16(dst, solid);
			} else {
				WRITE_UINT16(dst, table->blend(READ_UINT16(dst), c[0], c[1], c[2], cov));
			}
		}
		return;
	}
	for (int i = 0; i < n; ++i, dst += bpp) {
		if (!coverage[i])
			continue;
		const uint32 px = readPixel(dst, bpp);
		writePixel(dst, bpp, putText(fmt, px, index[i], coverage[i], paletteRGB, mode, table));
	}
}

void convertSpan(byte *dst, const PixelFormat &fmt, const byte *frame, const byte *index, const byte *coverage, int n,
                 const byte *paletteRGB, Mode mode, Blend16 *table) {
	const int bpp = fmt.bytesPerPixel;
	if (bpp != 2)
		table = nullptr;
	for (int i = 0; i < n; ++i, dst += bpp) {
		const byte *c = paletteRGB + frame[i] * 3;
		uint32 px = fmt.RGBToColor(c[0], c[1], c[2]);
		if (coverage && coverage[i])
			px = putText(fmt, px, index[i], coverage[i], paletteRGB, mode, table);
		writePixel(dst, bpp, px);
	}
}
```

  In `compositeRect` and `presentRect`, after `byte idxScratch[kChunk], covScratch[kChunk];`:
  `Common::ScopedPtr<Blend16> table(dst.format.bytesPerPixel == 2 && mode == kBlend ? new Blend16(dst.format) : nullptr);`
  and pass `table.get()` as the last argument of their `composeSpan` / `convertSpan` calls.
- [ ] **Step 3:** `make test` -> N0 + k + 2 OK (the exhaustive case takes about 3 s at `-O0`; if more than 10 s, cut
  the colours to three and say so in the comment). Every existing AA/compose test still green (they assert the same
  pixels: `svf_aa.h` `test_every_case_at_15_and_16_bit`, `hires_text_text_compose.h`).
- [ ] **Step 4 (mutations, each must turn a test red, then revert):** (a) `+ 126` instead of `+ 127` in the green
  table -> `test_everyPixelEveryCoverage` (prototype: 974,899 mismatches); (b) `solid` not refreshed when the index
  changes (`lastIndex` check removed) -> `test_composeSpanEqualsBlendPixel`; (c) `_alpha` left out -> both, on ARGB1555
  / RGBA5551; (d) slot by `cov & 3` instead of `cov >> 6` -> still green (a slot choice cannot change results: record
  that as the reason no test pins it).
- [ ] **Step 5:** commit:

```
GRAPHICS: Blend hi-res text on 15/16-bit screens by table

composeSpan() blended every covered pixel with blendPixel(): three
channel loops over the PixelFormat fields and three divisions by 255.
An emulated 486DX2-66 spent 137 ms compositing one 400x128 dialogue
box, on every frame an animation under the box updates.

Coverage 255 now writes the colour (what the blend gives there), and
a 2-byte format looks the partial levels up in Blend16, which tabulates
blendPixel()'s own integers for one colour and coverage at a time: the
same box takes 48 ms. The result is unchanged bit for bit (tested over
every pixel and coverage of five 15/16-bit formats).

Assisted-by: Claude Code
```

### Task c3: The SCI driver keeps one table per rectangle

**Files:** `engines/sci/graphics/drivers/upscaled.cpp` (`compositeText`, `:184-214`).

- [ ] After `byte indexRow[640 * 2], coverageRow[640 * 2];` add
  `// One blend table for the rectangle's rows (a 2-byte screen; it is not used on others).` and
  `Graphics::TextCompose::Blend16 table(_format);`, and pass `_pixelSize == 2 ? &table : nullptr` as the last argument of
  the `composeSpan` call. No new test (the SCI suites already composite through the driver's path only via game runs):
  run the KQ1 rgb565 and rgb555 runs of 0.2 on Linux (`$B/host/scummvm`, P3's `go1.sh`) and DOS: frames identical to
  the ones before the change (the prototype's DOS rgb565 frame after the change: 0 differing pixels against before and
  against Linux). Commit `SCI: Keep one hi-res text blend table per updated rectangle` (trailer).

### Task c4: `hires_text_whole_kb`

**Files:** `graphics/hires_text/hires_options.{h,cpp}`, `engines/sci/graphics/hirestext.cpp`,
`test/graphics/hires_text_hires_options.h`, `backends/platform/dos/dos.cpp`.

- [ ] **Step 1 (red):** in `test_defaults` add `TS_ASSERT_EQUALS(o.wholeKb, 256);`; in `test_values` set
  `k["hires_text_whole_kb"] = "64"` and assert `o.wholeKb == 64`; in `test_badValues` `k["hires_text_whole_kb"] = "-1"`
  (and `"99999"`) keep 256 with one warning each.
- [ ] **Step 2 (green):** `HiResOptions` gains `int wholeKb; ///< hires_text_whole_kb (256): a face file of at most
  this many KB is read whole, a larger one streamed` (initialised to 256 in the constructor, `hires_options.cpp:29-32`);
  `parseHiResOptions` reads it after `hires_text_log`:

```cpp
	if (get("hires_text_whole_kb", v, ctx)) {
		char *end = nullptr;
		const long kb = strtol(v.c_str(), &end, 10);
		if (!v.empty() && end && !*end && kb >= 0 && kb <= 16384)	// SvfFace::kMaxWholeSize is 16 MB
			o.wholeKb = (int)kb;
		else
			bad(warnings, "hires_text_whole_kb", v, "a number of KB from 0 to 16384");
	}
```

  (`readHiResOptions` already goes through the same `get`.) In `hirestext.cpp:80`:
  `_faces = new Graphics::HiResFaceCache(*_files, (uint32)options.wholeKb * 1024);`. The DOS backend registers
  `ConfMan.registerDefault("hires_text_whole_kb", 64);` in `initBackend` (comment: a streamed precomposed face costs its
  30-50 KB tables + a 65 KB block cache, against 100-340 KB whole; jamo measured the per-seek cost of streaming under
  DOSBox and chose whole-file reads up to 512 KB for MI1 talkie fonts: revisit with Task g6's numbers).
- [ ] **Step 3:** `make test` green; DOS KQ1 rgb565 run with a `[load]`-free AA map logs no change in frames; DESIGN 7's
  key table gets a row in Task g6 (docs). Commit `GRAPHICS: Let hires_text_whole_kb choose whole or streamed faces`
  (trailer).

### Task c5: `[preload]` warm-up for streamed faces

**Files:** `graphics/hires_text/font_set.{h,cpp}`, `test/graphics/hires_text_font_set.h`.

- [ ] **Step 1 (red):** a font-set test with a streamed fixture face (`HiResLoadStream` through a `[load]` line of a
  test map) and a `[preload]` naming a `.HOT` file in the test's memory file set: after `build()`,
  `face->getStreamReads()` (`svfface.h:92`) is > 0 and drawing the HOT text afterwards adds **0** reads. Model it on the
  existing streamed-face case of `hires_text_font_set.h` (re-grep its helper names in Task c5 Step 0).
- [ ] **Step 2 (green):** `HiResFaceCache` gains `void warm(SvfFace *face, const HiResFaceRef &ref, const HiResMap &map,
  Common::Array<Common::String> &warnings);` and a `Common::HashMap<Common::String, bool> _warmed;`; `HiResFontSet::build`
  calls it after each successful `faces.get` (`font_set.cpp:109`). Body:

```cpp
void HiResFaceCache::warm(SvfFace *face, const HiResFaceRef &ref, const HiResMap &map, Common::Array<Common::String> &warnings) {
	// [preload] lists the characters a game draws with a streamed face: reading their glyphs once at load fills the block
	// cache, so the first dialogue line does not seek the font file glyph by glyph (DESIGN 5.2, 8).
	if (!face || !face->isStreamed() || _warmed.contains(ref.key()) || !map.preloads.contains(ref.name))
		return;
	_warmed[ref.key()] = true;
	const HiResFaceRef &list = map.preloads[ref.name];
	const Common::Path path = _files.locate(list);
	Common::ScopedPtr<Common::SeekableReadStream> s(path.empty() ? nullptr : _files.open(path));
	if (!s) {
		warnings.push_back(Common::String::format("preload %s (%s): not found", list.name.c_str(), list.key().c_str()));
		return;
	}
	Common::Array<byte> cell(face->getCellBytes() * 2);	// a composed syllable of a streamed face reads pieces into the second half
	while (!s->eos() && !s->err()) {
		const Common::String line = s->readLine();
		if (line.hasPrefix("#"))
			continue;
		for (const char *p = line.c_str(), *end = p + line.size(); p < end;) {
			uint32 cp;
			bool invalid;
			p += decodeUtf8(p, end, cp, invalid);
			if (!invalid && face->has(cp))
				svf_glyph(face->raw(), cp, cell.data(), cell.size());
		}
	}
}
```

  (`svf_glyph` reads a table glyph or, with a JSET, composes U+AC00..U+D7A3 and the compat jamo, `graphics/svf/svf.h:196-202`;
  `svf_has` is true for exactly those, `:183-186`; a composed glyph of a streamed face needs `2 * svf_cell_bytes`,
  `:255-259`.)
- [ ] **Step 3:** `make test` green; mutation: skip the `warm` call -> the new test fails. Commit `GRAPHICS: Warm streamed
  faces from their [preload] lists` (trailer). U2: the DOS speed gain is measured in Task g6, not assumed.

### Task c6: Glyph and composite timing in a game (DOS)

- [ ] KQ1 with the AA pair map at rgb565, `-d1 --debugflags=Korean` plus the backend's `dos_stats_log=5`: record the
  `DOS: memory` and timer lines and how long the first message box takes to appear after `Return` (time from the
  xdotool key to the frame: take frames every 0.5 s with a loop of `xwd`). Compare `hires_text_whole_kb=0` (everything
  streamed) and `=256`. Numbers into `P7-notes.md`. No commit.

### Task c7: `BLEND.EXE` for real hardware

**Files:** new `backends/platform/dos/tools/blendbench.cpp`, `build-dos.sh` (target `bench`).

- [ ] Commit the prototype bench (`$P7/blendbench/bench2.cpp`, which times the tree's `composeSpan` with a table across
  rows and without one on a 400 x 128 box, and `bench.cpp`'s reference variant A = the old per-pixel `blendPixel` loop,
  kept in the bench file itself so it stays comparable) as one program that appends to `BLEND.TXT`; `build-dos.sh bench
  $B/sci` links it against `$B/sci/graphics/hires_text/text_compose.o`. Expected in DOSBox-X at the three presets of 0.3:
  the 0.3 numbers within 1 %. This is the program for H1 on a real PC. Commit `DOS: Add a hi-res text blend bench`
  (trailer).

### Review checkpoint C (after Task c7)

`make test` green in `$B/host` and `$B/asan`; the exhaustive test and the four mutations re-run by the reviewer; the
DOS frames of Task c1/c3 compared with Linux by the PIL snippet (0 pixels at rgb565; <= 1 per channel at rgb555; <= 3 at
CLUT8); `svf-renderer-accuracy-policy` check: no approximation introduced (Blend16 is exact). Findings back to the
implementer.

---

## P7d: Input

### Task d1: Han/Yeong and Hanja on DOS

- [ ] **Step 1:** `backends/events/dos/dos-events.cpp:113` already maps `SDL_SCANCODE_LANG1` to
  `Common::KEYCODE_HANGUL` (present in `ko-2026.4`, P2) and LANG2 to `KEYCODE_HANJA`: re-read, no change expected.
- [ ] **Step 2:** the DOSBox-X mapper test (prototype): write `$P7/run-kq1/han.map` with the letters, digits, Enter,
  Esc, Backspace, Tab, Space, arrows, **F5, F7** (missing in the prototype, so F5/F7 did nothing there) and
  `key_kor_hanyong "key 66"` (F9), add `mapperfile = <path>` to `[sdl]`; run `Right Return sleep:20 F9 sleep:1 F9
  sleep:1 type:qhk sleep:2`. Expected log `Hangul mode off` then `Hangul mode on` (`event.cpp:325`), the parser box
  shows 봐. Then the same with `key_kor_hanja` (if DOSBox-X has it: `strings /usr/bin/dosbox-x | grep kor_` lists only
  `kor_hanyong` here) - U4/H5 otherwise.
- [ ] **Step 3:** no commit (no code). The run script goes into Task g1.

### Task d2: Typing without an OS input method

- [ ] DOS has no OS IME: the built-in composer is the only path (`hangul_builtin` default true; `sci-ko.dat` turns
  Hangul input on). Runs: `qhk` -> 봐 (proven: the room description), `ansdmf qhk` -> 문을 봐 (KQ1's own Korean reply
  about the castle door: record the text), Backspace inside a syllable (one jamo removed), Shift for a double consonant
  (`Q` -> ㅃ). Record the frames. U5: KEYB KR / Korean keyboard layouts in DOS change nothing for the
  composer (it reads US scancodes); hardware item H5.

### Task d3: Alt-modified keys

- [ ] P4 notes item 3 (an `alt+s` leaking an `ㄴ` into the composer) is a Linux screenshot-key artefact; on DOS there
  is no Alt+S screenshot action. Check that Alt+letter does nothing in Korean mode on DOS (a run with `alt+x`): record.
  No code unless it types a jamo; then the fix belongs to the SCI composer (topic `ko/03-sci`), not to DOS.

---

## P7e: Memory, 8.3 names, saves

### Task e1: Memory gate (decision D-3)

- [ ] Run SCI.EXE (KQ1 rgb565 + AA map, LB1 when H-5 delivers), SCUMM.EXE (MI1 KO, after Task f2) and AGI.EXE (an
  English AGI game) in DOSBox-X with `memsize = 16`; record every `DOS: memory <phase> dpmi_free=... phys_total=...`
  line (phases `engine`, `first-frame`, and the quit line) and the stripped EXE sizes. Gate: **every EXE keeps >= 2048
  KB `dpmi_free` at its worst phase with the GUI compiled in** -> D-3 = no GUI-less series (Task e2 skipped). The
  prototype: SCI.EXE 4616-5812 KB at the first frame.
- [ ] Record in `P7-notes.md` with the jamo comparison (jamo SCI.EXE 5,670,989 B built without GUI).

### Task e2 (conditional on Task e1 failing): the GUI-less build

- [ ] Carry `jamo`'s `DISABLE_GUI` hunks (`git -C $OLD diff 96b42d42bc8 jamo -- <file> | grep -n DISABLE_GUI` per file:
  `backends/events/default/default-events.cpp`, `base/{commandLine,main}.cpp`, `common/config-manager.{h,cpp}`,
  `engines/{advancedDetector,engine,metaengine}.cpp`, `engines/engine.h`, `engines/module.mk`,
  `engines/sci/{metaengine,engine/guest_additions}.cpp`, `engines/scumm/{input,metaengine,scumm}.cpp`,
  `engines/scumm/macgui/macgui.cpp`, `engines/agi/{font,keyboard}.cpp`, `gui/{ThemeEngine,debugger,error,message,
  saveload}.cpp`, `gui/{debugger,message}.h`, `gui/module.mk`) plus the configure `--disable-gui` block, as one commit
  per directory, each a PATCHES.md row. Only the `DISABLE_GUI` lines: the rest of those `jamo` hunks is other work.
  Re-run Task e1.

### Task e3: 8.3 data staging

**Files:** new `backends/platform/dos/stage-data.py`, `backends/platform/dos/test_stage_data.py`.

- [ ] **Step 1 (red):** `test_stage_data.py` (unittest, stdlib): `dos_name("hires_text")` = `"HIRES_TE"`,
  `dos_name("korean-default.map")` = `"KOREAN-D.MAP"`, `dos_name("NEODG16A.SVF")` unchanged, `dos_name("a.b.c")` raises
  (two dots), `dos_name("con.map")` raises (device name); `stage()` of a temp tree `hires_text/maps/korean-default.map`
  + `hires_text/NEODG16A.SVF` writes `DATA/HIRES_TE/MAPS/KOREAN-D.MAP` and `DATA/HIRES_TE/NEODG16A.SVF`; two sources
  `hires_text/maps/korean-default.map` and `hires_text/maps/korean-defaults.map` -> `StageError` naming both;
  `check_maps(DATA, [map])` resolves every `data:` value of `[fonts]` in a map to a staged file and reports the missing
  ones.
- [ ] **Step 2 (green):**

```python
#!/usr/bin/env python3
"""stage-data.py SRC_DIR... DATA_DIR [--maps MAP...]: copy engine data into a DOS DATA folder with the names DOS gives them.

MS-DOS (and DJGPP with long names off, _CRT0_FLAG_NO_LFN) truncates every path component to 8.3 and upper-cases it, so
data:hires_text/maps/korean-default.map opens DATA\\HIRES_TE\\MAPS\\KOREAN-D.MAP. This writes exactly those names and
refuses two sources that would get the same one. --maps checks that every data: face of each map resolves.
"""
import os, re, shutil, sys

DEVICES = {"CON", "PRN", "AUX", "NUL"} | {"COM%d" % i for i in range(1, 10)} | {"LPT%d" % i for i in range(1, 10)}
VALID = re.compile(r"^[A-Z0-9_\-!#$%&'(){}@^`~]+$")


class StageError(Exception):
    pass


def dos_name(component):
    if component.count(".") > 1:
        raise StageError("%s: more than one dot" % component)
    base, _, ext = component.partition(".")
    base, ext = base[:8].upper(), ext[:3].upper()
    if not base or not VALID.match(base) or (ext and not VALID.match(ext)) or base in DEVICES:
        raise StageError("%s: no DOS name" % component)
    return base + ("." + ext if ext else "")


def dos_path(rel):
    return "/".join(dos_name(c) for c in rel.replace("\\", "/").split("/") if c)


def stage(sources, data_dir):
    """sources: [(root, prefix)] - every file under root is staged as prefix/<its path>."""
    seen = {}
    for root, prefix in sources:
        for dirpath, _, files in os.walk(root):
            for f in sorted(files):
                rel = os.path.relpath(os.path.join(dirpath, f), root)
                rel = (prefix + "/" + rel) if prefix else rel
                d = dos_path(rel)
                if d in seen:
                    raise StageError("%s and %s both become %s" % (seen[d], rel, d))
                seen[d] = rel
                out = os.path.join(data_dir, *d.split("/"))
                os.makedirs(os.path.dirname(out), exist_ok=True)
                shutil.copyfile(os.path.join(dirpath, f), out)
    return seen


def check_maps(data_dir, maps):
    missing = []
    for m in maps:
        section = None
        with open(m, encoding="utf-8") as fh:
            for line in fh:
                line = line.split(";", 1)[0].strip()
                if line.startswith("["):
                    section = line.strip("[]").split(":", 1)[0].lower()
                elif section == "fonts" and "=" in line:
                    value = line.split("=", 1)[1].strip()
                    if value.startswith("data:") and not os.path.isfile(os.path.join(data_dir, *dos_path(value[5:]).split("/"))):
                        missing.append("%s: %s" % (m, value))
    return missing


def main(argv):
    maps = []
    if "--maps" in argv:
        i = argv.index("--maps")
        argv, maps = argv[:i], argv[i + 1:]
    if len(argv) < 3:
        print(__doc__.strip().splitlines()[0])
        return 2
    *srcs, data = argv[1:]
    try:
        staged = stage([(s.split("=", 1)[0], s.split("=", 1)[1] if "=" in s else "") for s in srcs], data)
    except StageError as e:
        print("stage-data.py: %s" % e, file=sys.stderr)
        return 1
    missing = check_maps(data, maps)
    for m in missing:
        print("stage-data.py: not staged: %s" % m, file=sys.stderr)
    print("stage-data.py: %d files into %s" % (len(staged), data))
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
```

  Usage by `build-dos.sh stage`: `python3 -I stage-data.py $src/dists/engine-data/hires_text=hires_text $DIST/DATA
  --maps $src/dists/engine-data/hires_text/maps/*.map` -> `stage-data.py: 3 files into .../DATA` (NEODG16A.SVF,
  NEODGM.TXT, MAPS/KOREAN-D.MAP), rc 0. `ENCODING.DAT` is copied beside it (SCUMM P6a transcodes CP949 once at load).
- [ ] **Step 3:** `python3 -I backends/platform/dos/test_stage_data.py` -> `OK`; commit `DOS: Stage engine data under
  the names DOS gives them` (trailer).

### Task e4: Saves of long target names

**Files:** new `backends/platform/dos/dos-savenames.h`, `backends/platform/dos/dos-savefile.h`, new
`test/backends/dos_savenames.h`.

- [ ] **Step 1 (red):** host test:

```cpp
#include <cxxtest/TestSuite.h>
#include "backends/platform/dos/dos-savenames.h"

class DosSaveNamesTestSuite : public CxxTest::TestSuite {
public:
	void test_patternTruncated() {
		TS_ASSERT_EQUALS(DOS::dosPattern("kq1-rgb565.###"), "kq1-rgb5.###");
		TS_ASSERT_EQUALS(DOS::dosPattern("kq1sv.###"), "kq1sv.###");
		TS_ASSERT_EQUALS(DOS::dosPattern("monkey2-ko.s##"), "monkey2-.s##");
		TS_ASSERT_EQUALS(DOS::dosPattern("*"), "*");
	}
	void test_backToTheEngineName() {
		TS_ASSERT_EQUALS(DOS::engineName("kq1-rgb565.###", "KQ1-RGB5.001"), "kq1-rgb565.001");
		TS_ASSERT_EQUALS(DOS::engineName("monkey2-ko.s##", "MONKEY2-.S12"), "monkey2-ko.s12");
		TS_ASSERT_EQUALS(DOS::engineName("kq1sv.###", "KQ1SV.001"), "KQ1SV.001");	// short names are left as listed
		TS_ASSERT_EQUALS(DOS::engineName("k*.###", "KQ1-RGB5.001"), "KQ1-RGB5.001");	// a wildcard base cannot be mapped
	}
};
```

- [ ] **Step 2 (green):** `dos-savenames.h`:

```cpp
#ifndef BACKENDS_PLATFORM_DOS_SAVENAMES_H
#define BACKENDS_PLATFORM_DOS_SAVENAMES_H

#include "common/str.h"

namespace DOS {

/**
 * DOS keeps 8.3 names: a save "kq1-rgb565.001" is written as KQ1-RGB5.001 (the open truncates), and a later listing
 * with the engine's pattern "kq1-rgb565.###" finds nothing. dosPattern() is the pattern as DOS names it (the part
 * before the last dot cut to 8, the part after it to 3); engineName() gives a listed file back under the name the
 * engine would open it by, when the engine's base was cut.
 */
inline Common::String dosPattern(const Common::String &pattern) {
	const size_t dot = pattern.findLastOf('.');
	if (dot == Common::String::npos)
		return pattern.size() > 8 && !pattern.contains('*') ? Common::String(pattern.c_str(), 8) : pattern;
	Common::String base(pattern.c_str(), dot), ext(pattern.c_str() + dot + 1);
	if (base.size() > 8 && !base.contains('*'))
		base = Common::String(base.c_str(), 8);
	if (ext.size() > 3 && !ext.contains('*'))
		ext = Common::String(ext.c_str(), 3);
	return base + "." + ext;
}

inline Common::String engineName(const Common::String &pattern, const Common::String &listed) {
	const size_t pdot = pattern.findLastOf('.'), ldot = listed.findLastOf('.');
	if (pdot == Common::String::npos || ldot == Common::String::npos)
		return listed;
	const Common::String base(pattern.c_str(), pdot);
	if (base.size() <= 8 || base.contains('*') || base.contains('?') || base.contains('#'))
		return listed;
	Common::String ext(listed.c_str() + ldot + 1);
	ext.toLowercase();
	return base + "." + ext;
}

} // End of namespace DOS

#endif
```

  and in `DOS::SaveFileManager` (`dos-savefile.h`):

```cpp
	Common::StringArray listSavefiles(const Common::String &pattern) override {
		// The engine's own names (this session's saves, from the cache), then the 8.3 files under the engine's names.
		Common::StringArray out = DefaultSaveFileManager::listSavefiles(pattern);
		const Common::String dos = dosPattern(pattern);
		if (dos == pattern)
			return out;
		const Common::StringArray listed = DefaultSaveFileManager::listSavefiles(dos);
		for (uint i = 0; i < listed.size(); ++i) {
			const Common::String name = engineName(pattern, listed[i]);
			bool have = false;
			for (uint j = 0; j < out.size() && !have; ++j)
				have = out[j].equalsIgnoreCase(name);
			if (!have)
				out.push_back(name);
		}
		return out;
	}
```

  Opening, removing and saving need nothing: DOS truncates the long name to the same file.
- [ ] **Step 3 (green, emulator):** the prototype's red run (target `kq1-rgb565`, save, new session, F7) now lists
  `한글`; the short-target run unchanged. Frames into `~/work/tmp/ko-screenshots/dos-kq1-x-restore-longtarget-fixed.png`.
- [ ] **Step 4:** `make test` green; commit `DOS: List saves of targets longer than eight characters` (trailer).

### Task e5: Memory budget and CWSDPMI

- [ ] `backends/platform/dos/README.md` gets a "Memory" section with Task e1's table (EXE, phase, free KB at 16 MB),
  the face numbers of 0.4, the rule "CWSDPMI swaps to `CWSDPMI.SWP` when `-s` names it (the BATs' optional line); a 16 MB
  machine runs every measured game without swap", `FILES=40` (kept from `jamo`'s README). UNVERIFIED-ON-HARDWARE H7.
  Commit `DOS: Document the memory budget` (trailer).

### Task e6: LFN off, checked

- [ ] A DOSBox-X run with `[dos] lfn = true` and `ver = 7.1` (DOSBox-X then offers the LFN API): the KQ1 title still
  finds `KOREAN-D.MAP` (the backend's `_CRT0_FLAG_NO_LFN`, Task b4); without the flag (a test build) the map is not found
  (`Hi-res text:` log names no map; the title shows English buttons). Record both. No commit.

### Review checkpoint E (after Task e6)

Reviewer re-runs `test_stage_data.py`, `make test`, the long-target save run; reads Task e1's numbers and D-3.

---

## P7f: EXEs, launcher, packages

### Task f1: PLAY.EXE

- [ ] `launcher/*` came with Task b4 unchanged; `build-dos.sh play $B/play` runs `launcher/build-play.sh $B/play/PLAY.EXE`
  (expect `PLAY.EXE` ~279,040 B, as `jamo`'s engine zip of 2026-10-17); the host suites `dos_play_home.h`,
  `dos_play_merge.h`, `dos_play_sound.h` pass in `make test` (Task b7). Check `play-home.h:321-333`: `agi` -> `AGI.EXE`,
  `scumm` -> `SCUMM.EXE` then `SCI.EXE`, else `SCI.EXE` then `SCUMM.EXE`. No code change expected; commit only if
  `build-play.sh` needed the out-of-tree argument.

### Task f2: SCUMM.EXE with its codecs

- [ ] Build the codecs (`jamo:backends/platform/dos/build-deps.sh codecs` into `~/opt/codecs-dos`, already present on
  this host) and carry the SCUMM.EXE-only pieces: `audio/decoders/vorbis.h` `VORBIS_REFILL_SAMPLES` (+54 lines on
  `jamo`), the `jamo` `vorbis.cpp` setup-internals change only if `build-dos.sh scumm` needs it (U6),
  `dos-vorbis-selftest.*`, `check-tremor-cache.sh`, `patches/tremor-chunksize.patch`, the `static_assert` of Task b4
  Step 4 restored under `USE_VORBIS`. `heavy build-dos.sh scumm $B/scumm` -> `#define USE_FLAC`, `#define USE_TREMOR` in
  `config.h`, irqcheck clean. Engines: `--enable-engine=scumm --disable-engine=scumm_7_8,he`.
- [ ] Run MI1 KO (H-5 pack, P6a format) in DOSBox-X: the first room and a dialogue line in Korean through `korean.fnt`
  (S1, 320x200); compare with the Linux P6a frame of the same room (identical at CLUT8 up to the DAC's 6 bits). Commit
  `DOS: Build SCUMM.EXE with FLAC and Tremor` (trailer).

### Task f3: AGI.EXE (English)

- [ ] `heavy build-dos.sh agi $B/agi` (`--enable-engine=agi`, no codecs). Run one English AGI game the owner has (e.g.
  the LSL1 AGI data of P5's later work, English) in DOSBox-X: first screen at 640x400 CLUT8 or 320x200 (whatever the
  upstream AGI render mode picks). Record; no Korean claim (scope decision 7).

### Task f4: INI layout

- [ ] Document and test (in `play_test`-style runs) the layout `jamo` settled (memory `scummvm-dos-port`): HOME =
  PLAY's folder (`C:\SCUMMVM`) with `SCI.EXE`, `SCUMM.EXE`, `AGI.EXE`, `PLAY.EXE`, `CWSDPMI.EXE`, `DATA\`,
  `DEFAULT.INI`; per game `HOME\GAMES\ID\{SCUMMVM.INI, SAVES\, SCUMMVM.LOG, EXITLOG.TXT, SOUND.INI}`; pack =
  `GAMES\ID\` + `ID.INI` (no `path=`). Korean keys in a pack's `ID.INI`: `language=ko` (detection picks the `-ko`
  target), and only where the pack needs them `hires_text_map=`, `render_target=`, `hires_text_whole_kb=`. Targets and
  IDs <= 8 characters (H-4). README section; no code.

### Task f5: Engine zip staging

- [ ] `build-dos.sh stage DIST_DIR` (new subcommand): copies `$B/sci/scummvm.exe` -> `SCI.EXE`, `$B/scumm` ->
  `SCUMM.EXE` (+ `FLAC.TXT`, `VORBIS.TXT`), `$B/agi` -> `AGI.EXE`, `$B/play/PLAY.EXE`, `$CWSDPMI_EXE`, then
  `stage-data.py` into `DIST_DIR/DATA` (+ `ENCODING.DAT`), all stripped (`i586-pc-msdosdjgpp-strip`). Refuses a
  `DIST_DIR` inside `$src`. Output listing in `P7-notes.md`. Harness H-1 consumes it. Commit `DOS: Stage the engine
  package out of tree` (trailer).

### Task f6: README

- [ ] `backends/platform/dos/README.md`: render targets on DOS (auto = 565 / 1555 / CLUT8; `rgb888` explicit), the
  Han/Yeong key, `hires_text_whole_kb`, the 8.3 rules, the three EXEs and what AGI.EXE does now. Commit `DOS: Describe
  the Korean text options of the DOS build` (trailer).

---

## P7g: Acceptance

### Task g1: The runner

- [ ] Until H-3, a scratch runner `$P7/accept/run.py` (Python, stdlib + PIL) wraps `dosrun.sh`: lays out `c/SCUMMVM`
  from the staged engine dir and `d/` from an unzipped game zip, mounts D: read-only (`mount d <dir> -ro` in the conf),
  writes `RUN.BAT` (`C:\SCUMMVM\PLAY --install ID`, then `C:\SCUMMVM\PLAY ID target`), captures frames at given
  steps, and checks the log lines it is told to expect. Not committed (the harness owns acceptance scripts; H-3).

### Task g2: KQ1 KO, two-zip layout, both emulators

- [ ] DOSBox-X and Staging: install, title (Hangul buttons), first room, `qhk` reply, save `gksrmf` + restore in a new
  session, Han/Yeong toggle logged (X only). Expected: as 0.2; frames compared to Linux by the PIL snippet; D: unchanged
  (md5 of every file before and after).

### Task g3: LB1 KO

- [ ] Same steps with the H-5 LB1 pack (P4 format). Expected: Hangul title/menu, a typed command answered in Korean
  (P4 notes give `문을 봐` -> `나무와 쇠로 만든 묵직한 문(door)입니다.` for LB1's door), save/restore. U3 until run.

### Task g4: MI1 KO

- [ ] SCUMM.EXE through PLAY (`engineid=scumm`), both emulators: title, first dialogue line in Korean (S1, 1x
  `korean.fnt`), save with a Korean name through the original save screen and reload. U3 until run.

### Task g5: Memory and timing record

- [ ] One table in `P7-notes.md`: per game and emulator the `DOS: memory` lines, EXE sizes, Task c6 timings,
  `BLEND.EXE` numbers. Compare with DESIGN 8's budget ("well under 0.5 MB" for faces): faces = whole sizes + streamed
  tables + 73 KB cache per streamed face (0.4).

### Task g6: Notes and DESIGN follow-ups

- [ ] `P7-notes.md`: task results, decisions D-1..D-4 as answered, the conflicts of section 1, the UNVERIFIED and
  UNVERIFIED-ON-HARDWARE lists with what each run settled. DESIGN.md: section 7 key table gets `hires_text_whole_kb`;
  section 8 gets the measured composite numbers and the screen-format order of D-1; section 2.5's "nine SDL3 patches"
  corrected. Commit `DOCS: Korean: Record the DOS port` (trailer).

### Review checkpoint G (after Task g6)

A reviewer with fresh context: reads the whole `ko-2026.4..ko/08-dos` diff and `P7-notes.md`; re-runs `make test` (host
and ASan), the exhaustive blend test, `test_stage_data.py`, Task b8's gate run, one Staging run; checks every
screenshot named in the notes exists and was viewed. Findings back to the implementer; fixes as separate commits.

---

## P7h: UART debug socket (conditional: only after `ko/07-debugsocket` is merged)

### Task h1: The UART transport

- [ ] Carry `jamo:gui/debugsocket-dosuart.{h,cpp}` (+322 lines) onto the merged generic socket; `configure` auto-enables
  the socket for `msdosdjgpp` only with `--enable-debug-socket` (DESIGN 9: off by default in the fork); the `#ifdef
  USE_DEBUG_SOCKET` of Task b4 then compiles the counters in. `irqcheck.py` finds the `uart` range again.

### Task h2: DOSBox-X nullmodem

- [ ] `[serial] serial1 = nullmodem server:127.0.0.1 port:<free>`; host pacing 2 ms/byte (memory `scummvm-dos-port`);
  `state`, `dump`, `wait text` answered from SCI.EXE.

### Task h3: k16said on DOS

- [ ] H-6: `k16said.py --com` for KQ1: the DOS result equals the Linux one (>= 303/310, DESIGN 6).

---

## 6. Final acceptance checklist

Verified in this plan's runs (emulators and host):

- [ ] SDL3 series on upstream `1362030`, both prefixes, all eight marks, smoke identical in DOSBox-X and Staging (a1-a3).
- [ ] `ko/08-dos` diff touches only the files of section 3, each in `PATCHES.md`; Linux configure/build unchanged (B).
- [ ] `make test` N0 + k + (c2, c4, c5, e4 cases) OK on Linux and ASan; exhaustive blend test 0 mismatches; mutations red.
- [ ] SCI.EXE: KQ1 KO in DOSBox-X at CLUT8 / RGB565 / RGB555 / forced line repeat and in Staging (line repeat), frames
  equal to Linux (0 px at 565; <= 1 at 555; <= 3 at CLUT8), log lines of Task c1.
- [ ] `auto` on DOS picks RGB565 (D-1); `rgb888` only when asked.
- [ ] Han/Yeong toggle logged (`Hangul mode off/on`) in DOSBox-X; 봐 typed.
- [ ] Saves: UTF-8 description round trip; a > 8-character target lists its saves in a new session (e4).
- [ ] INI written on DOS (no "Unable to write configuration file").
- [ ] `data:` paths through 8.3 staging; LFN on or off, same result (e6).
- [ ] Memory table: every EXE >= 2 MB free at its worst phase at 16 MB (or the GUI-less series carried, D-3).
- [ ] KQ1, LB1, MI1 KO from the two-zip layout in DOSBox-X and Staging (g2-g4); AGI.EXE runs an English game (f3).
- [ ] `irqcheck.py` clean for SCI.EXE, SCUMM.EXE, AGI.EXE.
- [ ] Composite timing recorded (0.3 numbers reproduced by `BLEND.EXE` within 1 %).

UNVERIFIED-ON-HARDWARE (separate; the owner runs these on a real PC, results into `P7-notes.md`):

- [ ] H1 `BLEND.EXE` and a KQ1 dialogue on a real 486DX2-66 / Pentium (speed of composite and glyphs).
- [ ] H2 VBE mode lists and LFB on the owner's cards; 16 vs 32 bpp flush cost.
- [ ] H3 DAC width in 8 bpp (CLUT8 text colours).
- [ ] H4 Sound Blaster variants after the SDL rebase (86Box/YMF719 and a real SB16).
- [ ] H5 Han/Yeong and Hanja keys of a real Korean keyboard.
- [ ] H6 8.3 truncation on MS-DOS 6.22/7.1 and in a Windows 9x DOS box (LFN off).
- [ ] H7 memory with HIMEM/EMM386 and a CWSDPMI swap file; `FILES=` at the README's value.
- [ ] H8 the BATs' `SET` trailing-space handling on real MS-DOS.
