# MI1 Ultimate Talkie on the DOS port (S0-S3) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The Secret of Monkey Island, Ultimate Talkie Edition ("Midi Music" build) plays with English voice, with English or Korean subtitles, on `SCUMM.EXE` in DOSBox-X headless at Pentium 75 speed, with the spec's audio thresholds met, built from the user's own UTE files by a host script.

**Architecture:** `build-deps.sh` builds libFLAC, libogg and Tremor for DJGPP from pinned tarballs; `build-dos.sh scumm` links them (the sci edition does not). The DOS mixer subclasses `MixerImpl` to put every speech/music stream behind a decode-ahead ring (`backends/mixer/dos/prefetch.h`): rings are filled with interrupts on, from SDL3's cooperative audio thread before each interrupts-off mix piece, the first block is decoded on the main thread when the stream starts, and stream teardown is deferred out of the mutex. A sixth SDL3 patch exposes the Sound Blaster handler's counters, so a debug-socket `audio` command can report underruns, clock and decode cost; the harness gates on them. The host script `mkute.py` turns a UTE folder into an 8.3 DOS pack with 1 s seek points, `TRACK1.WAV`, an INI with targets `mi1`, `mi1ko` and `mi1kol`, and BATs.

**Tech Stack:** DJGPP 12.2 + SDL3 DOS (cooperative threads), ScummVM `audio/` (`MixerImpl`, `AudioStream`, `audio/decoders/flac.cpp`, `vorbis.cpp`), libFLAC 1.4.3, libogg 1.3.5, Tremor (xiph git 820fb32), CxxTest, Python 3.12 (stdlib only), DOSBox-X 2026.08.31, DOSBox Staging 0.83, host `flac`/`metaflac` 1.4.3.

**Spec:** `docs/superpowers/specs/2026-10-04-mi-talkie-dos-design.md` (stages S0-S3, Acceptance, Testing). Evidence: research report `/tmp/claude-1000/-home-thkim-work/36a78c74-674a-4d24-9ca3-7236f6e2ebde/scratchpad/mi_talkie_brainstorm.md` (volatile; its content is summarised here where tasks need it), the FLAC spike (branch `spike-flac` d7038f9e5eb, report `.superpowers/sdd/2026-09-29-dos-m5-scumm/flac-spike-report.md`, runs `~/work/scummvm/runs/flac-spike/`).

## Global Constraints

Copied from the spec (verbatim where the spec has the words; the source is named where it does not):

- "`SCI.EXE` is unchanged." (spec, Decision 1). This plan reads it as: SCI.EXE gets no codec and no new feature; it does link the shared DOS mixer and SDL3 changes of S1 (the spec's S1 says "SCI regression runs are required"), so every task that touches the mixer or SDL3 ends with the SCI gate.
- "Distribution: no speech or music data is distributed. A host script builds the pack from the user's own UTE files." (spec, Decision 3)
- "Make the Ultimate Talkie Edition (UTE) ... work properly in `SCUMM.EXE` on the DOS port (Pentium-class, about 16 MB RAM)" (spec, Goal). Every DOSBox run is `memsize = 16`.
- "SDL3 threads on DOS are cooperative: they yield only in `SDL_Delay` and the event pump." (spec, Starting point)
- "Keep DOS glue out of the engine" (user directive of 2026-10-03, memory note `scummvm-dos-port.md`): DOS-only code lives in `backends/` (mixer, platform/dos) and SDL3 patches; nothing under `engines/` gets a `DOS_DJGPP` block.
- Upstream-near engine changes minimal (M5 plan principle 1/4): a change to `engines/scumm` or `audio/` is generic, in its own commit (prefixes `SCUMM:`, `AUDIO:`), separate from `DOS:`/`TEST:` commits. In this plan only the conditional Task 13 touches them.
- "Midi Music variant only" for this plan; MI2 (S4), CD-track variants (S5), hardware pass (S6) are later plans.

Project rules (from the earlier plans and the user):

- Code: branch `dos-port`, worktree `~/work/scummvm/dos`. Harness: `~/work/scummvm` (git root, branch `master`), files under `harness/`. `git add` named files only. Never a bare `git stash` (use a WIP commit or a separate worktree). Never push. Never touch `harness/i18n/kq1plan.py`, `harness/i18n/kq1walkthrough.py`, or any `dump.mid` (`~/work/scummvm/dos/dump.mid`, `~/work/scummvm/dump.mid`, `harness/dos/dump.mid`).
- Commit trailer: the attribution lines the executing session's system prompt gives. The commit commands below carry this planning session's lines (`Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`, `Claude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4`) as one `-m` paragraph; an executor whose prompt gives other lines substitutes them.
- 8.3 names for everything on the DOS side; target names at most 8 characters.
- Game data is read-only. Game-derived files (packs, saves, recordings) stay under `~/work/scummvm/runs`, `~/work/scummvm/saves`, `~/work/scummvm/gamedata`: never in a git commit.
- Toolchain: `source ~/opt/dos-dev/env.sh` before any DJGPP command. Linux test env (all Linux build/test commands):
  `export PATH=$HOME/.local/sysroot/usr/bin:$PATH PKG_CONFIG_PATH=$HOME/.local/sysroot/usr/lib/x86_64-linux-gnu/pkgconfig LD_LIBRARY_PATH=$HOME/.local/sysroot/usr/lib/x86_64-linux-gnu`
- Ledger: `.superpowers/sdd/2026-10-04-mi1-talkie-dos/progress.md` (git-ignored). Every ruling, measured number, deviation and review verdict goes there.

## Executor notes: reviews and models

- Every review is dual (user rule): a Claude subagent reviewer and, in parallel, a read-only agy/Gemini review on the same package: `agy --mode plan --model gemini-3.1-pro-high --sandbox --add-dir .superpowers/sdd/2026-10-04-mi1-talkie-dos -p "<read-only, file-read/search tools only, no edits or builds; package path; findings by severity with file:line, scenario and fix; final VERDICT line>" > .superpowers/sdd/2026-10-04-mi1-talkie-dos/agy-<name>.md`, run from `~/work/scummvm/dos`. Verify each agy finding against the code before acting; a review passes only when both are clean or the remaining items are adjudicated in the ledger.
- Models: pass `model` explicitly on every Agent call. Sonnet for routine subagents (implementers of Tasks 0-12 and 14-15, scoped re-reviews, measurement runs). Opus only for design-level work and final reviews: the Task 4 + 6 design review (prefetch and teardown), the conditional Task 13 if it runs (it changes the engine and `audio/`), and the final whole-branch review in Task 16. Escalate a sonnet implementer to opus only when it is blocked after two fix rounds.

## Numbers in this plan: what was re-checked on 2026-10-04 and what was not

Re-verified while writing this plan (dos-port ea562950b77):

| fact | measured |
|---|---|
| stripped `SCUMM.EXE`, no codec (same configure flags as `build-dos.sh scumm`) | 3,954,688 B |
| + libFLAC (`~/opt/flac-dos`) | 4,094,464 B (+139,776; the research report said +141,824 on a 7.6 MB base) |
| + libFLAC + Tremor/libogg (scratchpad `tremor-dos`) | 4,209,152 B (+114,688 more; report: +115,200); total +254,464 B |
| configure with `--with-flac-prefix`/`--with-tremor-prefix`/`--with-ogg-prefix` | `Ogg yes, Vorbis no, Tremor yes, FLAC yes`; `config.h` has `USE_TREMOR`, `USE_VORBIS`, `USE_FLAC`; features string contains `Tremor FLAC `; `irqcheck.py` passes |
| `monkey.sof` (Midi Music folder) | 468,853,164 B; index 70,288 B = 4393 clips, sorted by original offset; every clip's data starts with `fLaC` at `new_offset + index_size + 4 + num_tags` |
| `track25-29.flac` (Midi Music folder) | 44.1 kHz stereo; SEEKTABLE 12/12/12/5/8 points (~10 s apart); total samples 5199872/5206016/5224960/2028032/3353088 |
| `track1.flac` | a RIFF WAVE, PCM 16-bit mono 44.1 kHz |
| `/tmp/ScummVM-Kor-Trs/The Secret Of Monkey Island (Ultimate Talkie Edition DOS)/` | exists: `korean.trs`, `korean00-04.fnt`, `README.md`; the five `.fnt` are byte-identical to `gamedata/mi1kor/korean0N.fnt` (cells 11x12, 8x8, 9x9, 8x8, 13x12) |
| codec sources | `~/opt/src/flac-dos/{flac-1.4.3.tar.xz, libogg-1.3.5.tar.xz, tremor.tar.gz}`; Tremor tarball = git commit 820fb3237ea81af44c9cc468c8b4e20128e3e5ad |
| SDL3 Sound Blaster ring | `RING_BUFFER_CHUNKS 4` in `~/opt/src/SDL3` (branch `dos-sb-probe`, d67c987); `isr_irq_count` exists; no underrun counter |
| host `metaflac` | not installed system-wide; only in the volatile scratchpad (`flac/hostflac`) - Task 1 builds a durable one |

Not re-verified (taken from the spike/research, to be measured by the tasks named): FLAC speech CPU 2-3 % (Task 10), seek cost 12 ms with 1 s seek points (Task 10, town scenario), cli max with prefetch 0.3-1.3 ms (Task 10), lip-sync lag ~0.4 s at 4096 frames (Task 12), conventional memory 64 KB at 4096 frames (S6), Tremor CPU for MI2 (S4). One spike number is a risk for the gate: `f4096-midi` measured clock 99.4 % (< 99.5 %) with a 4.8 ms interrupts-off piece caused by stream teardown under the mutex; the gate relies on Task 4/6's deferred teardown removing it.

## Decision: how prefetch is driven

Prefetch runs **on SDL3's audio thread, inside `DosMixerManager::sdlCallback()`, before every 256-frame mix piece, with interrupts on**; the first block (`kPrimeSamples`) is decoded **on the main thread** inside `PrefetchMixer::playStream()` (called from `Sound::startTalkSound()` for speech, `DefaultAudioCDManager::play()` for tracks); teardown (decoder delete, file close) runs in `PrefetchPool::reap()` at the end of each callback and at the next `wrap()`.

Reasons, from the code and the spike:
1. SDL3's DOS threads are cooperative: the audio thread runs only while the main thread yields (`SDL_Delay()` in `OSystem_DOS::delayMillis()`, the event pump). So "the audio callback" *is* "the engine loop's waits": the main thread is parked in `SDL_Delay`, never in the middle of engine code, when prefetch runs. No lock is needed between the two.
2. The spike did exactly this (`flacPrefetchAll()` at the top of `sdlCallback`, `audio/decoders/flac.cpp` `PrefetchStream`): 0 prefetch misses in every run, longest interrupts-off piece 0.3-1.3 ms, clock 100.1 %, 10 loops/s.
3. A timer callback is ruled out: timer procs run in the IRQ0 handler (M3 ruling), where file reads (INT 21h) and tens of milliseconds of FLAC decoding are not allowed.
4. Main-thread stalls (room loads) block the audio thread too, prefetch or not; they are covered by the device buffer (4096 frames, a 372 ms ring at 44.1 kHz), not by the ring.
5. Prefetching before *each piece* (not once per callback as in the spike) means a ring only has to cover one 256-frame piece, so a 16384-sample ring never runs dry even when a callback asks for several device buffers after a stall.

What is lifted from `spike-flac` and what is rewritten:

| spike file / function | here |
|---|---|
| `audio/decoders/flac.cpp` `PrefetchStream`, `flacPrefetchAll()`, `dosPrefetchWrap()`; `vorbis.cpp` wrap | rewritten as `backends/mixer/dos/prefetch.h` (`PrefetchPool`, `PrefetchProxy`, `PrefetchMixer`): backend-only, no `#ifdef DOS_DJGPP` in `audio/`, wraps any speech/music stream (FLAC, Tremor, VOC, WAV), slot pool so that teardown can be deferred and a proxy deleted from any context is safe |
| `backends/mixer/dos/dos-mixer.cpp` stats block (`spikeLog`, `g_st`, RDTSC per piece) | rewritten: counters in `DosMixerManager::_stats`, read on demand by the debug socket's `audio` command (`DOS::audioStats()`); no log file, no engine counter (`g_spikeLoops`) - loops come from the SCUMM socket's own loop counter |
| `dos_audio_frames` ConfMan key | kept (name and meaning), default 4096, validated (`dos-audio-config.h`) |
| `dos_audio_prefetch` key | dropped: prefetch is always on |
| `spike/flac/sdl3-sb-counters.patch` | rewritten as `sdl3-sb-stats.patch` against today's SDL3 (the spike patch no longer applies: the handler now has `isr_irq_count` and `ISR_Fill`) |
| `build-dos.sh` `DOS_FLAC`/`DOS_TREMOR`/`DOS_SDL3`/`DOS_OUT` | replaced by `build-deps.sh` + `DOS_CODECS` prefix; the scumm edition always links the codecs |
| `spike/flac/run.py` (own DOSBox conf, no socket) | replaced by `harness/dos/ute_audio.py` on `dosgame.launch` (debug socket, `cycles`/`cputype`/`core`/`audio_file` parameters) |
| `spike/flac/analyze.py` (AUDSTAT table) | replaced by `ute_audio.metrics()`/`gate()` on two `audio` readings |
| `spike/flac/evidence.py`, `wavan.py` (numpy/scipy, audioop) | not used: the lip-sync measurement uses synthetic markers (`audio_mark.py`, stdlib only) |
| engine/backends RDTSC debug lines (`SPIKE:` in `scumm.cpp`, `sound.cpp`, `default-audiocd.cpp`, `dos.cpp`, `dos-graphics.cpp`) | not ported |

## File structure

Code repo (`~/work/scummvm/dos`):

| file | responsibility |
|---|---|
| `backends/platform/dos/build-deps.sh` (new) | build libFLAC, libogg, Tremor for DJGPP into `$DOS_CODECS` (`~/opt/codecs-dos`) and host `flac`/`metaflac` into `$DOS_FLAC_HOST` (`~/opt/flac-host`), from SHA-pinned tarballs |
| `backends/platform/dos/build-dos.sh` | scumm edition links the codecs and stages `FLAC.TXT`/`VORBIS.TXT`; sci edition refuses codecs; refuses an SDL3 without `DOS_SBStatsChecked` |
| `backends/platform/dos/sdl3-sb-stats.patch` (new), `sdl3-build.txt` | SDL3: underrun/min-ring counters and `DOS_SBGetStats()` |
| `backends/platform/dos/dos-irq.cpp` | references `DOS_SBStatsChecked` |
| `backends/mixer/dos/prefetch.h` (new, header-only, platform-neutral) | `DOS::PrefetchPool`, `DOS::PrefetchProxy`, `DOS::PrefetchMixer` |
| `backends/mixer/dos/dos-audio-config.h` (new, header-only) | `DOS::audioDeviceFrames()` |
| `backends/mixer/dos/dos-audio-stats.h` (new, header-only) | `DOS::AudioStats`, `DOS::formatAudioStats()`, `DOS::audioStats()` declaration |
| `backends/mixer/dos/dos-mixer.{h,cpp}` | the DOS glue: SDL hint, pool and mixer, per-piece prefetch, reap, stats, latency estimate, speech hook, PC-speaker mark |
| `backends/platform/dos/dos.cpp` | comment of `mixerSelftest()` (buffer size) |
| `gui/debugsocket.{h,cpp}` | `audio` command (DOS body, like `rtc`/`mem`) |
| `test/backends/dos_prefetch.h`, `dos_audio_config.h`, `dos_audio_stats.h` (new) | CxxTest (picked up by the `dos_*.h` glob in `test/module.mk`) |
| `backends/platform/dos/mkute.py`, `test_mkute.py` (new) | the MI1 UTE host script and its tests |
| `dists/engine-data/hires_text/dos/M1U{0,1,2,4}.SVF`, `M1L{0,1,2,4}.SVF` | rebaked over DUMB text + UTE `korean.trs` |
| `tools/korean/SCUMM_FONTS.md` | the new bake commands |
| conditional (Task 13): `audio/mixer.h`, `engines/scumm/sound.{h,cpp}`, `test/audio/mixer_latency.h` | output-latency query and speech-timer hold-back |

Harness (`~/work/scummvm/harness`):

| file | responsibility |
|---|---|
| `dos/dosgame.py` | `launch(..., cycles=, cputype=, core=, audio_file=)` |
| `dos/test_dosgame_conf.py` (new) | `_conf` keeps its old output by default |
| `dos/audio_mark.py` (new) | click/marker detection in a DOSBox-X recording |
| `dos/ute_audio.py` (new) | `gate`, `lipsync`, `modes`, `fallback`, `korean` runs |
| `dos/test_ute_audio.py` (new) | unit tests for both |
| `dos/scummgame.py`, `dos/m5_points.py` | targets `mi1`, `mi1uko`, `mi1ukol` on the mkute pack |
| `dos/release/build_mi1ute.py` (new), `dos/release/build_engine.py`, `dos/twozip_test.py` | data-free MI1UTE kit zip; licences in the engine zip; a pack folder as D: |

---

### Task 0: Preflight - rescue volatile data, record baselines

**Files:**
- Create: `~/work/scummvm/gamedata/mi1ute-kor/` (copy, not committed)
- Create: `.superpowers/sdd/2026-10-04-mi1-talkie-dos/progress.md`

**Interfaces:**
- Consumes: nothing.
- Produces: `~/work/scummvm/gamedata/mi1ute-kor/{korean.trs, korean00.fnt .. korean04.fnt, README.md}`; ledger with the Linux test baseline (failing test list), SCI gate baseline (M0-M3, LOADING lines), stripped sizes of the HEAD `SCUMM.EXE`/`SCI.EXE`, copies `.superpowers/sdd/2026-10-04-mi1-talkie-dos/{SCUMM,SCI}-base.EXE`.

- [ ] **Step 1: Clean tree check**

Run: `cd ~/work/scummvm/dos && git status --short && git log --oneline -1`
Expected: no output from `status` (or only untracked files that are not yours: leave them); HEAD `ea562950b77` or a later docs commit. If tracked files are modified, stop and ask: the baseline must be HEAD. Never `git stash`.

- [ ] **Step 2: Rescue the UTE Korean translation**

```bash
K="/tmp/ScummVM-Kor-Trs/The Secret Of Monkey Island (Ultimate Talkie Edition DOS)"
mkdir -p ~/work/scummvm/gamedata/mi1ute-kor
cp -p "$K"/korean.trs "$K"/korean0[0-4].fnt "$K"/README.md ~/work/scummvm/gamedata/mi1ute-kor/
sha256sum ~/work/scummvm/gamedata/mi1ute-kor/*
```
Expected: six files; record the hashes in the ledger. If `/tmp/ScummVM-Kor-Trs` is gone, stop and ask the user for the archive (https://archive.org/details/monkeyisland1and2ute, ScummVM Kor. Project rev 1 2022-07-09).

- [ ] **Step 3: Linux unit-test baseline at HEAD**

```bash
export PATH=$HOME/.local/sysroot/usr/bin:$PATH PKG_CONFIG_PATH=$HOME/.local/sysroot/usr/lib/x86_64-linux-gnu/pkgconfig LD_LIBRARY_PATH=$HOME/.local/sysroot/usr/lib/x86_64-linux-gnu
mkdir -p ~/work/scummvm/dos/.superpowers/sdd/2026-10-04-mi1-talkie-dos
cd ~/work/scummvm/builds/linux-dos-test-scumm && make -j6 test 2>&1 | tee ~/work/scummvm/dos/.superpowers/sdd/2026-10-04-mi1-talkie-dos/baseline-test.log | tail -15
```
Expected: a CxxTest summary. The known failure is `test/graphics/hires_text_ttf_fit.h:493` (`test_pad_rows_moves_the_glyph_down_whole`), but that baseline is unproven: write the exact `Failed N and Skipped M of T tests` line and every failing assertion's file:line into the ledger. Later tasks compare against this list, not against "one known failure".

- [ ] **Step 4: DOS builds and sizes at HEAD**

```bash
cd ~/work/scummvm/dos && source ~/opt/dos-dev/env.sh
backends/platform/dos/build-dos.sh scumm 2>&1 | tail -3 && backends/platform/dos/build-dos.sh sci 2>&1 | tail -3
D=.superpowers/sdd/2026-10-04-mi1-talkie-dos; mkdir -p $D
cp dist/dos/SCUMM.EXE $D/SCUMM-base.EXE && cp dist/dos/SCI.EXE $D/SCI-base.EXE
for e in SCUMM SCI; do cp dist/dos/$e.EXE $D/$e.s && i586-pc-msdosdjgpp-strip $D/$e.s && echo $e $(stat -c %s $D/$e.s); done
```
Expected: both builds end with the `ls -la dist/dos` listing; `SCUMM 3954688` (within a few KB) and an `SCI` number. Record both.

- [ ] **Step 5: SCI and loading gate baseline**

```bash
cd ~/work/scummvm
for m in m0 m1 m2 m3; do python3 harness/dos/${m}_accept.py x 2>&1 | tail -1; done
python3 harness/dos/m3_accept.py staging 2>&1 | tail -1
python3 harness/dos/loading_accept.py x 2>&1 | tail -2
python3 harness/dos/loading_accept.py staging 2>&1 | tail -2
```
Expected: `M0 x: PASS`, `M1 x: PASS`, `M2 x: PASS`, `M3 x: PASS`, `M3 staging: PASS`, `LOADING x: PASS`, `LOADING staging: PASS`. Record every line. A FAIL here is a pre-existing failure: record it, tell the controller, and from then on the gate is "no line worse than this baseline".

- [ ] **Step 6: Ledger**

Create `.superpowers/sdd/2026-10-04-mi1-talkie-dos/progress.md` with: plan path, HEAD, Steps 2-5 results, and the heading `## Rulings`. No commit (the ledger is git-ignored; gamedata is never committed).

---

### Task 1: Codec and host-tool builds (`build-deps.sh`) [S0]

**Files:**
- Create: `backends/platform/dos/build-deps.sh`
- Modify: `backends/platform/dos/README.md` (one paragraph: "Codecs")

**Interfaces:**
- Consumes: tarballs in `$DOS_DEPS_SRC` (default `~/opt/src/flac-dos`).
- Produces: `$DOS_CODECS` (default `~/opt/codecs-dos`): `lib/libFLAC.a`, `lib/libogg.a`, `lib/libvorbisidec.a`, `include/FLAC/`, `include/ogg/`, `include/tremor/{ivorbiscodec.h,ivorbisfile.h,config_types.h}`, `share/licenses/{FLAC,OGG,TREMOR}.TXT`. `$DOS_FLAC_HOST` (default `~/opt/flac-host`): `bin/flac`, `bin/metaflac` (host, static).

- [ ] **Step 1: Write the script**

```bash
#!/bin/bash
# Build what SCUMM.EXE links for compressed audio, and the host tools the MI1
# Ultimate Talkie pack script (mkute.py) needs.
# Usage: backends/platform/dos/build-deps.sh [codecs|host|all]   (default: all)
#   codecs: libFLAC 1.4.3, libogg 1.3.5 and Tremor (integer Vorbis) for DJGPP,
#           static, -O2 -march=i586 -mtune=pentium, no asm/SSE -> $DOS_CODECS
#           (default ~/opt/codecs-dos). build-dos.sh scumm links them.
#   host:   flac and metaflac 1.4.3 for this machine -> $DOS_FLAC_HOST
#           (default ~/opt/flac-host).
# Sources come from $DOS_DEPS_SRC (default ~/opt/src/flac-dos); a missing
# tarball is downloaded. libFLAC and libogg are checked by SHA-256; Tremor has
# no release, so its tarball must be xiph's tremor at the commit below
# (git get-tar-commit-id).
set -e
what="${1:-all}"
src="${DOS_DEPS_SRC:-$HOME/opt/src/flac-dos}"
codecs="${DOS_CODECS:-$HOME/opt/codecs-dos}"
host="${DOS_FLAC_HOST:-$HOME/opt/flac-host}"
work="$(mktemp -d "${TMPDIR:-/tmp}/dos-deps.XXXXXX")"
trap 'rm -rf "$work"' EXIT

FLAC_TAR=flac-1.4.3.tar.xz
FLAC_SHA=6c58e69cd22348f441b861092b825e591d0b822e106de6eb0ee4d05d27205b70
FLAC_URL=https://downloads.xiph.org/releases/flac/flac-1.4.3.tar.xz
OGG_TAR=libogg-1.3.5.tar.xz
OGG_SHA=c4d91be36fc8e54deae7575241e03f4211eb102afb3fc0775fbbc1b740016705
OGG_URL=https://downloads.xiph.org/releases/ogg/libogg-1.3.5.tar.xz
TREMOR_TAR=tremor.tar.gz
TREMOR_COMMIT=820fb3237ea81af44c9cc468c8b4e20128e3e5ad
TREMOR_URL=https://gitlab.xiph.org/xiph/tremor/-/archive/$TREMOR_COMMIT/tremor-$TREMOR_COMMIT.tar.gz
CFLAGS_DOS="-O2 -march=i586 -mtune=pentium"
TREMOR_OBJS="block codebook floor0 floor1 info mapping0 mdct registry res012 sharedbook synthesis vorbisfile window"

fetch() {	# tarball url
	if [ ! -f "$src/$1" ]; then
		mkdir -p "$src"
		curl -fL -o "$src/$1" "$2"
	fi
}
check_sha() {	# tarball sha256
	echo "$2  $src/$1" | sha256sum -c --quiet - || { echo "build-deps.sh: $src/$1 has the wrong SHA-256" >&2; exit 1; }
}
unpack() {	# tarball dir
	mkdir -p "$work/$2"
	tar -xf "$src/$1" -C "$work/$2" --strip-components=1
}
run_logged() {	# log command...
	local log="$1"; shift
	if ! "$@" >>"$log" 2>&1; then
		tail -30 "$log" >&2
		echo "build-deps.sh: failed: $* (log $log)" >&2
		exit 1
	fi
}

build_host() {
	fetch "$FLAC_TAR" "$FLAC_URL"; check_sha "$FLAC_TAR" "$FLAC_SHA"
	unpack "$FLAC_TAR" hflac
	rm -rf "$host"
	( cd "$work/hflac" &&
	  run_logged "$work/hflac.log" ./configure --prefix="$host" --disable-shared --enable-static \
		--disable-ogg --disable-cpplibs --disable-examples --disable-doxygen-docs \
		--disable-xmms-plugin --disable-thorough-tests --disable-version-from-git &&
	  run_logged "$work/hflac.log" make -j"$(nproc)" &&
	  run_logged "$work/hflac.log" make install )
	"$host/bin/metaflac" --version
}

build_codecs() (
	source ~/opt/dos-dev/env.sh
	fetch "$FLAC_TAR" "$FLAC_URL"; check_sha "$FLAC_TAR" "$FLAC_SHA"
	fetch "$OGG_TAR" "$OGG_URL"; check_sha "$OGG_TAR" "$OGG_SHA"
	fetch "$TREMOR_TAR" "$TREMOR_URL"
	got="$(gzip -dc "$src/$TREMOR_TAR" | git get-tar-commit-id)"
	if [ "$got" != "$TREMOR_COMMIT" ]; then
		echo "build-deps.sh: $src/$TREMOR_TAR is tremor $got, not $TREMOR_COMMIT" >&2
		exit 1
	fi
	rm -rf "$codecs"
	mkdir -p "$codecs/share/licenses"
	# libogg (Tremor's bitstream layer)
	unpack "$OGG_TAR" ogg
	( cd "$work/ogg" &&
	  run_logged "$work/ogg.log" ./configure --host=i586-pc-msdosdjgpp --prefix="$codecs" \
		--disable-shared --enable-static CFLAGS="$CFLAGS_DOS" &&
	  run_logged "$work/ogg.log" make -j"$(nproc)" &&
	  run_logged "$work/ogg.log" make install )
	cp "$work/ogg/COPYING" "$codecs/share/licenses/OGG.TXT"
	# libFLAC: the FLAC spike's configuration (no asm, no SSE/AVX, no Ogg FLAC, no C++, no programs)
	unpack "$FLAC_TAR" flac
	( cd "$work/flac" &&
	  run_logged "$work/flac.log" ./configure --host=i586-pc-msdosdjgpp --prefix="$codecs" \
		--enable-static --disable-shared --disable-ogg --disable-asm-optimizations --disable-sse \
		--disable-avx --disable-cpplibs --disable-programs --disable-examples --disable-doxygen-docs \
		--disable-xmms-plugin --disable-stack-smash-protection --disable-thorough-tests \
		--disable-oggtest --disable-rpath --disable-version-from-git --disable-multithreading \
		CFLAGS="$CFLAGS_DOS" &&
	  run_logged "$work/flac.log" make -j"$(nproc)" &&
	  run_logged "$work/flac.log" make install )
	cp "$work/flac/COPYING.Xiph" "$codecs/share/licenses/FLAC.TXT"
	# Tremor: its autotools do not know DJGPP; the library is these sources
	unpack "$TREMOR_TAR" tremor
	for f in $TREMOR_OBJS; do
		run_logged "$work/tremor.log" i586-pc-msdosdjgpp-gcc $CFLAGS_DOS \
			-DBYTE_ORDER=1234 -DLITTLE_ENDIAN=1234 -DBIG_ENDIAN=4321 \
			-I"$codecs/include" -c "$work/tremor/$f.c" -o "$work/tremor/$f.o"
	done
	run_logged "$work/tremor.log" i586-pc-msdosdjgpp-ar rcs "$codecs/lib/libvorbisidec.a" \
		$(for f in $TREMOR_OBJS; do echo "$work/tremor/$f.o"; done)
	mkdir -p "$codecs/include/tremor"
	cp "$work/tremor/ivorbiscodec.h" "$work/tremor/ivorbisfile.h" "$work/tremor/config_types.h" \
		"$codecs/include/tremor/"
	cp "$work/tremor/COPYING" "$codecs/share/licenses/TREMOR.TXT"
	ls -l "$codecs/lib"/*.a
)

case "$what" in
	codecs) build_codecs ;;
	host) build_host ;;
	all) build_host; build_codecs ;;
	*) echo "usage: build-deps.sh [codecs|host|all]" >&2; exit 1 ;;
esac
```

Note: `build_host` runs without `env.sh` so that the host compiler, not DJGPP's, is found; `build_codecs` is a subshell that sources it.

- [ ] **Step 2: Run it**

Run: `cd ~/work/scummvm/dos && chmod +x backends/platform/dos/build-deps.sh && backends/platform/dos/build-deps.sh all`
Expected: `metaflac 1.4.3`, then three `.a` files listed under `~/opt/codecs-dos/lib`.

- [ ] **Step 3: Verify the outputs**

```bash
source ~/opt/dos-dev/env.sh
i586-pc-msdosdjgpp-nm ~/opt/codecs-dos/lib/libFLAC.a | grep -c ' T _FLAC__stream_decoder_new$'
i586-pc-msdosdjgpp-nm ~/opt/codecs-dos/lib/libvorbisidec.a | grep -c ' T _ov_open_callbacks$'
i586-pc-msdosdjgpp-nm ~/opt/codecs-dos/lib/libogg.a | grep -c ' T _ogg_sync_init$'
ls ~/opt/codecs-dos/share/licenses ~/opt/codecs-dos/include/tremor
~/opt/flac-host/bin/flac --version
```
Expected: `1`, `1`, `1`; `FLAC.TXT OGG.TXT TREMOR.TXT`; `config_types.h ivorbiscodec.h ivorbisfile.h`; `flac 1.4.3`.

- [ ] **Step 4: README paragraph**

Add to `backends/platform/dos/README.md` (after the SDL3 build section):

```markdown
### Codecs (SCUMM.EXE only)

`backends/platform/dos/build-deps.sh codecs` builds libFLAC 1.4.3, libogg 1.3.5
and Tremor (xiph git 820fb32, integer Vorbis) for DJGPP into ~/opt/codecs-dos
(`DOS_CODECS` moves it); `build-dos.sh scumm` links them and stages their
licences as FLAC.TXT and VORBIS.TXT. `build-deps.sh host` builds flac and
metaflac for this machine into ~/opt/flac-host, for `mkute.py` and its tests.
SCI.EXE links no codec.
```

- [ ] **Step 5: Commit**

```bash
git add backends/platform/dos/build-deps.sh backends/platform/dos/README.md
git commit -m "DOS: Build libFLAC, libogg and Tremor for SCUMM.EXE, and host metaflac" -m "$(printf 'Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4')"
```

---

### Task 2: SCUMM.EXE links FLAC and Tremor; SCI.EXE does not [S0]

**Files:**
- Modify: `backends/platform/dos/build-dos.sh`

**Interfaces:**
- Consumes: `$DOS_CODECS` from Task 1.
- Produces: `dist/dos/SCUMM.EXE` with `USE_FLAC` and `USE_TREMOR`; `dist/dos/FLAC.TXT`, `dist/dos/VORBIS.TXT`; `dist/dos/SCI.EXE` without codecs.

- [ ] **Step 1: Write the failing check**

Run: `cd ~/work/scummvm/dos && grep -c -a 'Tremor FLAC ' dist/dos/SCUMM.EXE`
Expected: `0` (today's build has no codec).

- [ ] **Step 2: Edit `build-dos.sh`**

After the `sdl3-sb-probe` check and before `if [ "$edition" = scumm ]`, add nothing; replace the edition block and the `conf_args` codec line as follows.

Replace:
```bash
if [ "$edition" = scumm ]; then
	out="$src/build-dos-scumm"
	engine_args=(--enable-engine=scumm --disable-engine=scumm_7_8,he)
	exe=SCUMM.EXE
else
	out="$src/build-dos"
	engine_args=(--enable-engine=sci --disable-engine=sci32)
	exe=SCI.EXE
fi
```
with:
```bash
# SCUMM.EXE plays compressed speech and CD tracks (the Ultimate Talkie
# editions: FLAC MONKEY.SOF, Vorbis MONKEY2.SOG, FLAC tracks) with the
# libraries build-deps.sh builds; SCI.EXE links none.
codecs="${DOS_CODECS:-$HOME/opt/codecs-dos}"
if [ "$edition" = scumm ]; then
	out="$src/build-dos-scumm"
	engine_args=(--enable-engine=scumm --disable-engine=scumm_7_8,he)
	exe=SCUMM.EXE
	for l in libFLAC.a libogg.a libvorbisidec.a; do
		if [ ! -f "$codecs/lib/$l" ]; then
			echo "build-dos.sh: no $codecs/lib/$l (set DOS_CODECS)." >&2
			echo "  Run backends/platform/dos/build-deps.sh codecs." >&2
			exit 1
		fi
	done
	codec_args=(--disable-vorbis --with-tremor-prefix="$codecs" --with-ogg-prefix="$codecs"
		--with-flac-prefix="$codecs" --disable-mad)
else
	out="$src/build-dos"
	engine_args=(--enable-engine=sci --disable-engine=sci32)
	exe=SCI.EXE
	codec_args=(--disable-vorbis --disable-tremor --disable-flac --disable-mad)
fi
```
and in `conf_args` replace the line
```bash
	--disable-vorbis --disable-tremor --disable-flac --disable-mad
```
with
```bash
	"${codec_args[@]}"
```
After the existing `if ! config_current || ! grep -q '^DISABLE_GUI = 1$' config.mk; then ... fi` block, add:
```bash
if [ "$edition" = scumm ] && ! { grep -q '^#define USE_FLAC$' config.h && grep -q '^#define USE_TREMOR$' config.h; }; then
	echo "build-dos.sh: configure did not take libFLAC and Tremor from $codecs; not building." >&2
	exit 1
fi
if [ "$edition" = sci ] && grep -qE '^#define USE_(FLAC|TREMOR|VORBIS|MAD)$' config.h; then
	echo "build-dos.sh: the sci edition must link no codec; not building." >&2
	exit 1
fi
```
After `cp scummvm.exe "$src/dist/dos/$exe"`, add:
```bash
if [ "$edition" = scumm ]; then
	# The codecs' BSD licences ask for their notices next to the program.
	cp "$codecs/share/licenses/FLAC.TXT" "$src/dist/dos/FLAC.TXT"
	cat "$codecs/share/licenses/OGG.TXT" "$codecs/share/licenses/TREMOR.TXT" > "$src/dist/dos/VORBIS.TXT"
fi
```
Update the usage comment at the top: `#   scumm: build-dos-scumm/, engines/scumm (not scumm_7_8, he), FLAC + Tremor -> dist/dos/SCUMM.EXE`.

- [ ] **Step 3: Build both editions**

```bash
source ~/opt/dos-dev/env.sh
backends/platform/dos/build-dos.sh scumm 2>&1 | grep -E "Checking for (Ogg|Vorbis|Tremor|FLAC)|scummvm.exe failed|not building" ; backends/platform/dos/build-dos.sh scumm 2>&1 | tail -3
grep -E '^#define USE_(FLAC|TREMOR|VORBIS)$' build-dos-scumm/config.h
backends/platform/dos/build-dos.sh sci 2>&1 | tail -3
grep -cE '^#define USE_(FLAC|TREMOR|VORBIS|MAD)$' build-dos/config.h
```
Expected: `Checking for Ogg... yes`, `Vorbis... no`, `Tremor... yes`, `FLAC >= 1.1.3... yes` (first configure only); `#define USE_TREMOR`, `#define USE_VORBIS`, `#define USE_FLAC`; sci grep prints `0`. Both builds end with the `ls -la dist/dos` listing (the irqcheck passed).

- [ ] **Step 4: Verify features, sizes, licences**

```bash
grep -c -a 'Tremor FLAC ' dist/dos/SCUMM.EXE; grep -c -a 'Tremor FLAC ' dist/dos/SCI.EXE
for e in SCUMM SCI; do cp dist/dos/$e.EXE /tmp/claude-1000/$e.s && i586-pc-msdosdjgpp-strip /tmp/claude-1000/$e.s && echo $e $(stat -c %s /tmp/claude-1000/$e.s); done
head -3 dist/dos/FLAC.TXT dist/dos/VORBIS.TXT
```
Expected: `1` then `0`; `SCUMM` about 4,209,152 (Task 0 size + about 254 KB; record the exact growth); `SCI` equal to the Task 0 number; both licence files start with a Xiph copyright line.

- [ ] **Step 5: Boot regression (no audio change yet)**

Run: `cd ~/work/scummvm && python3 harness/dos/m0_accept.py x 2>&1 | tail -1 && python3 harness/dos/loading_accept.py x mi2 2>&1 | tail -1`
Expected: `M0 x: PASS`, `LOADING x: PASS`.

- [ ] **Step 6: Commit**

```bash
git add backends/platform/dos/build-dos.sh
git commit -m "DOS: Link FLAC and Tremor into SCUMM.EXE only" -m "$(printf 'Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4')"
```

---

### Task 3: The MI1 UTE pack host script (`mkute.py`) [S3]

**Files:**
- Create: `backends/platform/dos/mkute.py`
- Create: `backends/platform/dos/test_mkute.py`
- Output (not committed): `~/work/scummvm/runs/mi1ute/pack/MI1UTE/`

**Interfaces:**
- Consumes: host `flac`/`metaflac` (Task 1), the UTE folder, `gamedata/mi1ute-kor` (Task 0).
- Produces: `mkute.make_pack(ute, out, korean=None, metaflac="metaflac", flac="flac", test_clips=False, log=print) -> str` (the pack path `<out>/MI1UTE`); `mkute.check_sof(path) -> list[(start, size)]`; `mkute.dos_name(name) -> str`; `mkute.ini_text(korean: bool) -> str` (CRLF); `mkute.game_bat(game_id, target) -> str`; `mkute.PackError`. The pack: `MI1UTE/GAMES/MI1UTE/{MONKEY.000, MONKEY.001, MONKEY.SOF, TRACK1.WAV, TRACK25.FLA..TRACK29.FLA[, KOREAN.TRS, KOREAN00.FNT..KOREAN04.FNT]}`, `MI1UTE/MI1UTE.INI`, `MI1UTE/MI1.BAT[, MI1KO.BAT, MI1KOL.BAT]`. Harness tasks read `~/work/scummvm/runs/mi1ute/pack/MI1UTE` as `m5_points.UTE_PACK`.

- [ ] **Step 1: Write the failing tests** (`backends/platform/dos/test_mkute.py`)

```python
#!/usr/bin/env python3
"""Tests for mkute.py. Needs host flac/metaflac (build-deps.sh host):
FLAC_HOST names their bin directory (default ~/opt/flac-host/bin).

    python3 backends/platform/dos/test_mkute.py
"""
import hashlib
import os
import struct
import subprocess
import sys
import tempfile
import unittest
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkute  # noqa: E402

BIN = os.environ.get("FLAC_HOST", os.path.expanduser("~/opt/flac-host/bin"))
FLAC = os.path.join(BIN, "flac")
METAFLAC = os.path.join(BIN, "metaflac")


def write_wav(path, secs, channels=2, rate=44100):
    with wave.open(path, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(rate)
        frames = bytearray()
        for i in range(secs * rate):
            v = (i * 37) % 2000 - 1000
            frames += struct.pack("<h", v) * channels
        w.writeframes(bytes(frames))


def write_sof(path, clips):
    """A MONKEY.SOF with the given clips: [(original offset, tag bytes, data)]."""
    index = b""
    body = b""
    for org, tags, data in clips:
        index += struct.pack(">IIII", org, len(body), len(tags), len(data))
        body += tags + data
    with open(path, "wb") as f:
        f.write(struct.pack(">I", len(index)) + index + body)


def md5(path):
    with open(path, "rb") as f:
        return hashlib.md5(f.read()).hexdigest()


class MkuteTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.ute = os.path.join(self.tmp.name, "ute")
        self.kor = os.path.join(self.tmp.name, "kor")
        self.out = os.path.join(self.tmp.name, "out")
        os.makedirs(self.ute)
        os.makedirs(self.kor)
        for n in ("monkey.000", "monkey.001"):
            with open(os.path.join(self.ute, n), "wb") as f:
                f.write(n.encode() * 10)
        write_sof(os.path.join(self.ute, "monkey.sof"),
                  [(8, b"\0\1", b"fLaC" + b"\0" * 60), (500, b"\0\2", b"fLaC" + b"\0" * 30)])
        write_wav(os.path.join(self.ute, "track1.flac"), 1, channels=1)
        wav = os.path.join(self.tmp.name, "t.wav")
        write_wav(wav, 3)
        for n in range(25, 30):
            subprocess.run([FLAC, "-s", "-f", "-o", os.path.join(self.ute, "track%d.flac" % n), wav], check=True)
        with open(os.path.join(self.kor, "korean.trs"), "wb") as f:
            f.write(b"SCVMTRS \0\0")
        for n in range(5):
            with open(os.path.join(self.kor, "korean%02d.fnt" % n), "wb") as f:
                f.write(bytes([0, 0, 8, 8]))

    def tearDown(self):
        self.tmp.cleanup()

    def make(self, korean=False):
        return mkute.make_pack(self.ute, self.out, korean=self.kor if korean else None,
                               metaflac=METAFLAC, flac=FLAC, log=lambda *a: None)

    def test_dos_names(self):
        self.assertEqual(mkute.dos_name("track25.flac"), "TRACK25.FLA")
        self.assertEqual(mkute.dos_name("monkey.sof"), "MONKEY.SOF")
        self.assertEqual(mkute.dos_name("track1.flac"), "TRACK1.WAV")
        self.assertEqual(mkute.dos_name("korean00.fnt"), "KOREAN00.FNT")

    def test_english_pack(self):
        before = md5(os.path.join(self.ute, "track25.flac"))
        pack = self.make()
        game = os.path.join(pack, "GAMES", "MI1UTE")
        want = {"MONKEY.000", "MONKEY.001", "MONKEY.SOF", "TRACK1.WAV"} | {"TRACK%d.FLA" % n for n in range(25, 30)}
        self.assertEqual(set(os.listdir(game)), want)
        self.assertEqual(sorted(os.listdir(pack)), ["GAMES", "MI1.BAT", "MI1UTE.INI"])
        for n in range(25, 30):
            pts = mkute.seek_points(METAFLAC, os.path.join(game, "TRACK%d.FLA" % n))
            self.assertGreaterEqual(len(pts), 3)
        self.assertEqual(md5(os.path.join(self.ute, "track25.flac")), before)   # input untouched
        ini = open(os.path.join(pack, "MI1UTE.INI"), "rb").read().decode("ascii")
        self.assertIn("[mi1]\r\n", ini)
        self.assertNotIn("[mi1ko]", ini)
        self.assertIn("speech_mute=false\r\n", ini)
        self.assertFalse(any(l.startswith("path=") for l in ini.split("\r\n")))
        bat = open(os.path.join(pack, "MI1.BAT"), "rb").read().decode("ascii")
        self.assertEqual(bat, mkute.game_bat("MI1UTE", "mi1"))
        self.assertIn("%SVH%\\PLAY MI1UTE mi1\r\n", bat)

    def test_korean_pack(self):
        pack = self.make(korean=True)
        game = os.path.join(pack, "GAMES", "MI1UTE")
        self.assertTrue({"KOREAN.TRS"} | {"KOREAN%02d.FNT" % n for n in range(5)} <= set(os.listdir(game)))
        self.assertEqual(sorted(f for f in os.listdir(pack) if f.endswith(".BAT")), ["MI1.BAT", "MI1KO.BAT", "MI1KOL.BAT"])
        ini = open(os.path.join(pack, "MI1UTE.INI"), "rb").read().decode("ascii")
        for s in ("[mi1]", "[mi1ko]", "[mi1kol]", "hires_text_map=data:M1KO.MAP", "render_target=clut8", "language=ko"):
            self.assertIn(s, ini)
        self.assertTrue(all(l.endswith("\r") or l == "" for l in ini.split("\n")[:-1]))

    def test_missing_file(self):
        os.remove(os.path.join(self.ute, "track27.flac"))
        with self.assertRaisesRegex(mkute.PackError, "track27.flac"):
            self.make()

    def test_track1_must_be_wav(self):
        with open(os.path.join(self.ute, "track1.flac"), "wb") as f:
            f.write(b"fLaC" + b"\0" * 40)
        with self.assertRaisesRegex(mkute.PackError, "not a WAV"):
            self.make()

    def test_sof_index_checks(self):
        p = os.path.join(self.tmp.name, "bad.sof")
        write_sof(p, [(500, b"", b"fLaC"), (8, b"", b"fLaC")])
        with self.assertRaisesRegex(mkute.PackError, "out of order"):
            mkute.check_sof(p)
        write_sof(p, [(8, b"\0\1", b"OggS")])
        with self.assertRaisesRegex(mkute.PackError, "not FLAC"):
            mkute.check_sof(p)
        self.assertEqual(len(mkute.check_sof(os.path.join(self.ute, "monkey.sof"))), 2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
```

- [ ] **Step 2: Run them to see them fail**

Run: `cd ~/work/scummvm/dos && python3 backends/platform/dos/test_mkute.py 2>&1 | tail -3`
Expected: `ModuleNotFoundError: No module named 'mkute'`.

- [ ] **Step 3: Write `backends/platform/dos/mkute.py`**

```python
#!/usr/bin/env python3
"""Make the DOS pack of The Secret of Monkey Island, Ultimate Talkie Edition
("Midi Music" build), for SCUMM.EXE, from your own copy of the game.

  python3 mkute.py UTE_DIR OUT_DIR [--korean KOREAN_DIR] [--metaflac PATH]
                   [--flac PATH] [--test-clips]

UTE_DIR     the "Ultimate Talkie Version with Midi Music" folder: monkey.000,
            monkey.001, monkey.sof, track1.flac, track25.flac ... track29.flac
KOREAN_DIR  optional: the ScummVM Kor. Project's translation of this edition
            (korean.trs, korean00.fnt ... korean04.fnt); adds the Korean targets
OUT_DIR     gets MI1UTE\\: GAMES\\MI1UTE\\ (the game), MI1UTE.INI and MI1.BAT
            (with --korean also MI1KO.BAT, MI1KOL.BAT). Copy MI1UTE\\ to the
            DOS machine (not to a CD: speech seeks need a hard disk) and run a
            BAT from it.

It copies the files under 8.3 names (track25.flac -> TRACK25.FLA), stores
track1.flac, which is a WAV in this build, as TRACK1.WAV, adds a seek point
every second to TRACK25-29.FLA with metaflac (without them a seek stalls the
game for seconds on DOS), checks MONKEY.SOF's clip index (--test-clips also
decodes every clip with flac), and writes the INI and the BATs. Your files are
not changed. metaflac and flac come with FLAC (https://xiph.org/flac/).
"""
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys

PACK = "MI1UTE"
UTE_FILES = ["monkey.000", "monkey.001", "monkey.sof", "track1.flac"] + ["track%d.flac" % n for n in range(25, 30)]
KOREAN_FILES = ["korean.trs"] + ["korean%02d.fnt" % n for n in range(5)]
HOME_DEFAULT = "C:\\SCUMMVM"
_POINT = re.compile(r"^\s*point \d+: sample_number=(\d+)", re.M)
_PLACEHOLDER = 0xFFFFFFFFFFFFFFFF


class PackError(Exception):
    pass


def find(folder, name):
    """The file in folder called name in any letter case, or None."""
    for f in os.listdir(folder):
        p = os.path.join(folder, f)
        if f.lower() == name.lower() and os.path.isfile(p):
            return p
    return None


def dos_name(name):
    """The 8.3 upper-case name in the pack."""
    if name.lower() == "track1.flac":
        return "TRACK1.WAV"
    base, ext = os.path.splitext(name)
    return (base[:8] + ext[:4]).upper()


def is_wav(path):
    with open(path, "rb") as f:
        head = f.read(12)
    return head[:4] == b"RIFF" and head[8:12] == b"WAVE"


def run(cmd, data=None):
    try:
        r = subprocess.run(cmd, input=data, capture_output=True, check=False)
    except OSError as e:
        raise PackError("cannot run %s: %s" % (cmd[0], e))
    if r.returncode:
        raise PackError("%s failed: %s" % (" ".join(cmd[:2]), r.stderr.decode("utf-8", "replace").strip()))
    return r.stdout.decode("utf-8", "replace")


def check_sof(path):
    """MONKEY.SOF's clip index as SCUMM reads it (sound.cpp setupSfxFile and
    startTalkSound): a big-endian index size, 16 bytes a clip (original
    offset, offset after the index, tag bytes, FLAC bytes), sorted by original
    offset (the engine bsearches it); each clip's data, after its tags, is FLAC
    and ends inside the file. Returns [(start, size)] of every clip's FLAC data."""
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        head = f.read(4)
        if len(head) < 4:
            raise PackError("%s: too short for a clip index" % path)
        n = struct.unpack(">I", head)[0]
        if n == 0 or n % 16 or n + 4 > size:
            raise PackError("%s: index size %d is not a multiple of 16 inside the file" % (path, n))
        idx = f.read(n)
        clips = []
        last = -1
        for i in range(0, n, 16):
            org, new, tags, csize = struct.unpack(">IIII", idx[i:i + 16])
            if org <= last:
                raise PackError("%s: clip %d is out of order (original offset %d after %d)" % (path, i // 16, org, last))
            last = org
            start = new + n + 4 + tags
            if start + csize > size:
                raise PackError("%s: clip %d (original offset %d) ends past the file" % (path, i // 16, org))
            f.seek(start)
            if f.read(4) != b"fLaC":
                raise PackError("%s: clip %d (original offset %d) is not FLAC" % (path, i // 16, org))
            clips.append((start, csize))
    return clips


def decode_clips(flac, path, clips, log):
    with open(path, "rb") as f:
        for i, (start, csize) in enumerate(clips):
            f.seek(start)
            run([flac, "-t", "-s", "-"], f.read(csize))
            if (i + 1) % 500 == 0:
                log("  %d/%d clips decoded" % (i + 1, len(clips)))
    log("MONKEY.SOF: all %d clips decode" % len(clips))


def seek_points(metaflac, path):
    out = run([metaflac, "--list", "--block-type=SEEKTABLE", path])
    return [int(s) for s in _POINT.findall(out) if int(s) != _PLACEHOLDER]


def add_seekpoints(metaflac, path):
    """A seek point every second; at least one per whole second of audio after."""
    run([metaflac, "--add-seekpoint=1s", path])
    total = int(run([metaflac, "--show-total-samples", path]).strip())
    rate = int(run([metaflac, "--show-sample-rate", path]).strip())
    pts = seek_points(metaflac, path)
    if rate <= 0 or len(pts) < total // rate:
        raise PackError("%s: %d seek points for %d s of audio" % (path, len(pts), total // max(rate, 1)))
    return len(pts)


def game_bat(game_id, target):
    """The game BAT of the DOS packs (the harness's pack_common.game_bat):
    PLAY.EXE through SCUMMVM_HOME (default C:\\SCUMMVM), the swap file from
    SCUMMVM_SWAP. Plain MS-DOS 5 commands."""
    lines = [
        "@echo off",
        "set SVH=%SCUMMVM_HOME%",
        'if "%SVH%"=="" set SVH=' + HOME_DEFAULT,
        "for %%h in (%SVH%) do set SVH=%%h",
        "if exist %SVH%\\PLAY.EXE goto run",
        "echo PLAY.EXE is not in %SVH%.",
        "echo Unzip the engine zip there, or SET SCUMMVM_HOME to the folder that has it.",
        "goto end",
        ":run",
        "echo Loading ScummVM...",
        'if not "%SCUMMVM_SWAP%"=="" %SVH%\\CWSDPMI -s%SCUMMVM_SWAP%\\CWSDPMI.SWP',
        "%SVH%\\PLAY " + game_id + " " + target,
        ":end",
        "set SVH=",
    ]
    return "\r\n".join(lines) + "\r\n"


def targets(korean):
    t = [("mi1", "MI1", "The Secret of Monkey Island (Ultimate Talkie, English)", "en", None, None)]
    if korean:
        t += [("mi1ko", "MI1KO", "The Secret of Monkey Island (Ultimate Talkie, Korean, anti-aliased)",
               "ko", "data:M1KO.MAP", None),
              ("mi1kol", "MI1KOL", "The Secret of Monkey Island (Ultimate Talkie, Korean, 8-bit font)",
               "ko", "data:M1KO.MAP", "clut8")]
    return t


def ini_text(korean):
    """The pack's INI (PLAY copies it to the profile once), CRLF, no path=."""
    bats = ", ".join("%s.BAT" % b for _, b, _, _, _, _ in targets(korean))
    out = ["# Ready-to-run configuration: The Secret of Monkey Island, Ultimate Talkie",
           "# Edition (Midi Music build), for SCUMM.EXE. Written by MKUTE.PY.",
           "# Started by %s. PLAY copies this file to" % bats,
           "# C:\\SCUMMVM\\GAMES\\MI1UTE\\SCUMMVM.INI once and gives the game directory",
           "# itself, so there is no path= line.",
           "",
           "[scummvm]",
           "lastselectedgame=mi1",
           "music_driver=adlib",
           ""]
    for name, _, desc, lang, mp, rt in targets(korean):
        out += ["[%s]" % name, "engineid=scumm", "gameid=monkey", "description=%s" % desc,
                "language=%s" % lang, "platform=pc",
                "# Voice and subtitles. Ctrl+T in the game cycles voice and text, text only,",
                "# voice only.",
                "subtitles=true", "speech_mute=false"]
        if mp:
            out += ["extrapath=DATA", "hires_text_map=%s" % mp]
        if rt:
            out.append("render_target=%s" % rt)
        out.append("")
    return "\r\n".join(out)


def make_pack(ute, out, korean=None, metaflac="metaflac", flac="flac", test_clips=False, log=print):
    missing = [n for n in UTE_FILES if not find(ute, n)]
    if missing:
        raise PackError('%s lacks %s (the UTE "Midi Music" folder has them)' % (ute, ", ".join(missing)))
    if korean:
        missing = [n for n in KOREAN_FILES if not find(korean, n)]
        if missing:
            raise PackError("%s lacks %s" % (korean, ", ".join(missing)))
    if not is_wav(find(ute, "track1.flac")):
        raise PackError('%s is not a WAV file: is this the "Midi Music" build?' % find(ute, "track1.flac"))
    run([metaflac, "--version"])
    clips = check_sof(find(ute, "monkey.sof"))
    log("MONKEY.SOF: %d clips, index in order, every clip FLAC" % len(clips))
    if test_clips:
        run([flac, "--version"])
        decode_clips(flac, find(ute, "monkey.sof"), clips, log)
    pack = os.path.join(out, PACK)
    game = os.path.join(pack, "GAMES", PACK)
    if os.path.exists(pack):
        shutil.rmtree(pack)
    os.makedirs(game)
    for n in UTE_FILES + (KOREAN_FILES if korean else []):
        src = find(korean if n in KOREAN_FILES else ute, n)
        dst = os.path.join(game, dos_name(n))
        shutil.copyfile(src, dst)
        if n.startswith("track") and n != "track1.flac":
            log("%s: %d seek points" % (dos_name(n), add_seekpoints(metaflac, dst)))
    with open(os.path.join(pack, PACK + ".INI"), "wb") as f:
        f.write(ini_text(bool(korean)).encode("ascii"))
    for target, bat, _, _, _, _ in targets(bool(korean)):
        with open(os.path.join(pack, bat + ".BAT"), "wb") as f:
            f.write(game_bat(PACK, target).encode("ascii"))
    log("pack: %s" % pack)
    return pack


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("ute")
    ap.add_argument("out")
    ap.add_argument("--korean")
    ap.add_argument("--metaflac", default="metaflac")
    ap.add_argument("--flac", default="flac")
    ap.add_argument("--test-clips", action="store_true")
    a = ap.parse_args(argv)
    try:
        make_pack(a.ute, a.out, a.korean, a.metaflac, a.flac, a.test_clips)
    except PackError as e:
        print("mkute: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Run the tests**

Run: `python3 backends/platform/dos/test_mkute.py 2>&1 | tail -3`
Expected: `Ran 6 tests` ... `OK`.

- [ ] **Step 5: Build the personal pack from the real data (not committed)**

```bash
U="$HOME/work/scummvm/gamedata/mi1all/Secret of Monkey Island, The (Multi-Platform)/The Secret of Monkey Island Ultimate Talkie Version/Ultimate Talkie Version with Midi Music"
python3 backends/platform/dos/mkute.py "$U" ~/work/scummvm/runs/mi1ute/pack --korean ~/work/scummvm/gamedata/mi1ute-kor \
  --metaflac ~/opt/flac-host/bin/metaflac --flac ~/opt/flac-host/bin/flac --test-clips
ls -l ~/work/scummvm/runs/mi1ute/pack/MI1UTE ~/work/scummvm/runs/mi1ute/pack/MI1UTE/GAMES/MI1UTE
```
Expected: `MONKEY.SOF: 4393 clips, index in order, every clip FLAC`, progress lines, `MONKEY.SOF: all 4393 clips decode`, then seek points `TRACK25.FLA` >= 117, `TRACK26.FLA` >= 118, `TRACK27.FLA` >= 118, `TRACK28.FLA` >= 45, `TRACK29.FLA` >= 76; 15 game files; `MI1UTE.INI`, `MI1.BAT`, `MI1KO.BAT`, `MI1KOL.BAT`. Record the counts in the ledger.

- [ ] **Step 6: Commit**

```bash
git add backends/platform/dos/mkute.py backends/platform/dos/test_mkute.py
git commit -m "DOS: Add mkute.py, the MI1 Ultimate Talkie pack builder" -m "$(printf 'Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4')"
```

---

### Task 4: Decode-ahead core (`prefetch.h`) with Linux tests [S1]

**Files:**
- Create: `backends/mixer/dos/prefetch.h`
- Create: `test/backends/dos_prefetch.h`

**Interfaces:**
- Consumes: `Audio::AudioStream` (`audio/audiostream.h`), `Audio::MixerImpl` (`audio/mixer_intern.h`).
- Produces (namespace `DOS`):
  - `class PrefetchPool { static const int kSlots = 4, kRingSamples = 16384, kPrimeSamples = 4096; enum State { kFree, kLive, kOrphan }; Audio::AudioStream *wrap(Audio::AudioStream *parent, int markMillis = 0); void prefetchAll(); void reap(); uint32 misses() const; int countSlots(State) const; int read(Slot &, int16 *, int); }`
  - `class PrefetchProxy : public Audio::AudioStream` (made only by `wrap`)
  - `class PrefetchMixer : public Audio::MixerImpl { PrefetchMixer(PrefetchPool &pool, uint sampleRate, bool stereo, uint outBufSize); typedef void (*SpeechHook)(void *ctx); typedef bool (*WrapGuard)(); void setSpeechHook(SpeechHook, void *); void setWrapGuard(WrapGuard); void setMarkMillis(int); uint32 speechStarts() const; uint32 musicStarts() const; }` - wraps `kSpeechSoundType`/`kMusicSoundType` streams given with `DisposeAfterUse::YES`.

- [ ] **Step 1: Write the failing tests** (`test/backends/dos_prefetch.h`)

```cpp
#include <cxxtest/TestSuite.h>

#include "backends/mixer/dos/prefetch.h"
#include "../system/null_osystem.h"

namespace {

// Sample i of the stream is (i & 0x7fff), so a reader can check the order.
class FakeStream : public Audio::AudioStream {
public:
	FakeStream(int total, bool stereo, int *deleted, int rate = 44100)
		: _total(total), _pos(0), _stereo(stereo), _rate(rate), _deleted(deleted) {}
	~FakeStream() override { (*_deleted)++; }
	int readBuffer(int16 *buffer, const int numSamples) override {
		const int n = MIN(numSamples, _total - _pos);
		for (int i = 0; i < n; i++)
			buffer[i] = (int16)((_pos + i) & 0x7fff);
		_pos += n;
		return n;
	}
	bool isStereo() const override { return _stereo; }
	int getRate() const override { return _rate; }
	bool endOfData() const override { return _pos >= _total; }
	int pos() const { return _pos; }

private:
	int _total, _pos;
	bool _stereo;
	int _rate;
	int *_deleted;
};

void countCall(void *ctx) {
	(*(int *)ctx)++;
}

bool refuse() {
	return false;
}

} // End of anonymous namespace

class DosPrefetchPoolTestSuite : public CxxTest::TestSuite {
public:
	void test_wrap_primes_on_the_spot_and_only_reap_deletes() {
		int deleted = 0;
		DOS::PrefetchPool pool;
		FakeStream *f = new FakeStream(100000, false, &deleted);
		Audio::AudioStream *p = pool.wrap(f);
		TS_ASSERT_DIFFERS(p, (Audio::AudioStream *)f);
		TS_ASSERT_EQUALS(f->pos(), DOS::PrefetchPool::kPrimeSamples);
		TS_ASSERT_EQUALS(pool.countSlots(DOS::PrefetchPool::kLive), 1);
		delete p;
		TS_ASSERT_EQUALS(deleted, 0);
		TS_ASSERT_EQUALS(pool.countSlots(DOS::PrefetchPool::kOrphan), 1);
		pool.reap();
		TS_ASSERT_EQUALS(deleted, 1);
		TS_ASSERT_EQUALS(pool.countSlots(DOS::PrefetchPool::kFree), DOS::PrefetchPool::kSlots);
	}

	void test_reads_come_in_order_across_the_ring_edge() {
		int deleted = 0;
		DOS::PrefetchPool pool;
		const int total = 3 * DOS::PrefetchPool::kRingSamples + 123;
		Audio::AudioStream *p = pool.wrap(new FakeStream(total, false, &deleted));
		int16 buf[777];
		int got = 0;
		bool inOrder = true;
		while (!p->endOfData()) {
			pool.prefetchAll();
			const int n = p->readBuffer(buf, 777);
			for (int i = 0; i < n; i++)
				inOrder = inOrder && buf[i] == (int16)((got + i) & 0x7fff);
			got += n;
		}
		TS_ASSERT(inOrder);
		TS_ASSERT_EQUALS(got, total);
		TS_ASSERT_EQUALS(pool.misses(), 0u);
		TS_ASSERT(p->endOfStream());
		delete p;
		pool.reap();
	}

	void test_a_dry_ring_decodes_in_place_and_counts_a_miss() {
		int deleted = 0;
		DOS::PrefetchPool pool;
		Audio::AudioStream *p = pool.wrap(new FakeStream(100000, false, &deleted));
		int16 buf[DOS::PrefetchPool::kPrimeSamples];
		TS_ASSERT_EQUALS(p->readBuffer(buf, DOS::PrefetchPool::kPrimeSamples), DOS::PrefetchPool::kPrimeSamples);
		TS_ASSERT_EQUALS(pool.misses(), 0u);
		TS_ASSERT_EQUALS(p->readBuffer(buf, 100), 100);
		TS_ASSERT_EQUALS(buf[0], (int16)DOS::PrefetchPool::kPrimeSamples);
		TS_ASSERT_EQUALS(pool.misses(), 1u);
		delete p;
		pool.reap();
	}

	void test_stereo_reads_from_the_parent_stay_whole_frames() {
		int deleted = 0;
		DOS::PrefetchPool pool;
		FakeStream *f = new FakeStream(20002, true, &deleted);
		Audio::AudioStream *p = pool.wrap(f);
		pool.prefetchAll();
		TS_ASSERT_EQUALS(f->pos() % 2, 0);
		TS_ASSERT(p->isStereo());
		delete p;
		pool.reap();
	}

	void test_a_full_pool_hands_the_stream_back() {
		int deleted = 0;
		DOS::PrefetchPool pool;
		Audio::AudioStream *p[DOS::PrefetchPool::kSlots];
		for (int i = 0; i < DOS::PrefetchPool::kSlots; i++)
			p[i] = pool.wrap(new FakeStream(10000, false, &deleted));
		FakeStream *extra = new FakeStream(10000, false, &deleted);
		TS_ASSERT_EQUALS(pool.wrap(extra), (Audio::AudioStream *)extra);
		delete extra;
		for (int i = 0; i < DOS::PrefetchPool::kSlots; i++)
			delete p[i];
		pool.reap();
		TS_ASSERT_EQUALS(deleted, DOS::PrefetchPool::kSlots + 1);
	}

	void test_the_marker_comes_first() {
		int deleted = 0;
		DOS::PrefetchPool pool;
		Audio::AudioStream *p = pool.wrap(new FakeStream(1000, false, &deleted, 44100), 10);
		int16 buf[442];
		TS_ASSERT_EQUALS(p->readBuffer(buf, 442), 442);
		// 441 frames (10 ms) of a 1002 Hz square (period 44 frames), then the stream
		TS_ASSERT_EQUALS(buf[0], 16000);
		TS_ASSERT_EQUALS(buf[21], 16000);
		TS_ASSERT_EQUALS(buf[22], -16000);
		TS_ASSERT_EQUALS(buf[44], 16000);
		TS_ASSERT_EQUALS(buf[441], 0);
		delete p;
		pool.reap();
	}

	void test_the_pool_deletes_what_it_still_holds() {
		int deleted = 0;
		{
			DOS::PrefetchPool pool;
			Audio::AudioStream *a = pool.wrap(new FakeStream(10000, false, &deleted));
			Audio::AudioStream *b = pool.wrap(new FakeStream(10000, false, &deleted));
			delete a;
			delete b;
		}
		TS_ASSERT_EQUALS(deleted, 2);
	}
};

class DosPrefetchMixerTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::install_null_g_system();	// Channel::mix() reads getMillis()
#endif
	}
	void tearDown() override {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::uninstall_null_g_system();
#endif
	}

	void test_speech_and_music_are_wrapped_not_sfx_nor_borrowed_streams() {
		int deleted = 0;
		DOS::PrefetchPool pool;
		DOS::PrefetchMixer *mixer = new DOS::PrefetchMixer(pool, 44100, true, 1024);
		mixer->setReady(true);
		Audio::Mixer &m = *mixer;
		Audio::SoundHandle h1, h2, h3, h4;
		FakeStream borrowed(5000, false, &deleted);
		m.playStream(Audio::Mixer::kSpeechSoundType, &h1, new FakeStream(5000, false, &deleted));
		m.playStream(Audio::Mixer::kMusicSoundType, &h2, new FakeStream(5000, true, &deleted));
		m.playStream(Audio::Mixer::kSFXSoundType, &h3, new FakeStream(5000, false, &deleted));
		m.playStream(Audio::Mixer::kSpeechSoundType, &h4, &borrowed, -1, Audio::Mixer::kMaxChannelVolume, 0,
		             DisposeAfterUse::NO);
		TS_ASSERT_EQUALS(pool.countSlots(DOS::PrefetchPool::kLive), 2);
		TS_ASSERT_EQUALS(mixer->speechStarts(), 1u);
		TS_ASSERT_EQUALS(mixer->musicStarts(), 1u);
		delete mixer;
		TS_ASSERT_EQUALS(deleted, 1);	// the SFX stream; the wrapped two wait for reap()
		pool.reap();
		TS_ASSERT_EQUALS(deleted, 3);
	}

	void test_a_finished_line_is_freed_by_reap_not_under_the_mixer() {
		int deleted = 0;
		DOS::PrefetchPool pool;
		DOS::PrefetchMixer *mixer = new DOS::PrefetchMixer(pool, 44100, true, 1024);
		mixer->setReady(true);
		Audio::Mixer &m = *mixer;
		Audio::SoundHandle h;
		m.playStream(Audio::Mixer::kSpeechSoundType, &h, new FakeStream(3000, false, &deleted));
		byte buf[1024 * 4];
		for (int i = 0; i < 10 && m.isSoundHandleActive(h); i++) {
			pool.prefetchAll();
			mixer->mixCallback(buf, sizeof(buf));
		}
		TS_ASSERT(!m.isSoundHandleActive(h));
		TS_ASSERT_EQUALS(deleted, 0);
		TS_ASSERT_EQUALS(pool.countSlots(DOS::PrefetchPool::kOrphan), 1);
		pool.reap();
		TS_ASSERT_EQUALS(deleted, 1);
		TS_ASSERT_EQUALS(pool.misses(), 0u);
		delete mixer;
	}

	void test_the_speech_hook_runs_once_a_line_and_the_guard_can_refuse() {
		int deleted = 0, calls = 0;
		DOS::PrefetchPool pool;
		DOS::PrefetchMixer *mixer = new DOS::PrefetchMixer(pool, 44100, true, 1024);
		mixer->setReady(true);
		mixer->setSpeechHook(countCall, &calls);
		mixer->setWrapGuard(refuse);
		Audio::Mixer &m = *mixer;
		Audio::SoundHandle h;
		m.playStream(Audio::Mixer::kSpeechSoundType, &h, new FakeStream(3000, false, &deleted));
		TS_ASSERT_EQUALS(calls, 1);
		TS_ASSERT_EQUALS(pool.countSlots(DOS::PrefetchPool::kLive), 0);
		delete mixer;
		TS_ASSERT_EQUALS(deleted, 1);
	}

	void test_the_marker_goes_in_front_of_speech_only() {
		int deleted = 0;
		DOS::PrefetchPool pool;
		byte buf[64 * 4];
		{
			DOS::PrefetchMixer mixer(pool, 44100, true, 1024);
			mixer.setReady(true);
			mixer.setMarkMillis(10);
			Audio::Mixer &m = mixer;
			Audio::SoundHandle h;
			m.playStream(Audio::Mixer::kSpeechSoundType, &h, new FakeStream(3000, false, &deleted));
			mixer.mixCallback(buf, sizeof(buf));
			TS_ASSERT(ABS(((int16 *)buf)[0]) > 10000);
		}
		pool.reap();
		{
			DOS::PrefetchMixer mixer(pool, 44100, true, 1024);
			mixer.setReady(true);
			mixer.setMarkMillis(10);
			Audio::Mixer &m = mixer;
			Audio::SoundHandle h;
			m.playStream(Audio::Mixer::kMusicSoundType, &h, new FakeStream(3000, false, &deleted));
			mixer.mixCallback(buf, sizeof(buf));
			TS_ASSERT_EQUALS(((int16 *)buf)[0], 0);
		}
		pool.reap();
		TS_ASSERT_EQUALS(deleted, 2);
	}
};
```

- [ ] **Step 2: Run the tests to see them fail**

Run (Linux env from Global Constraints): `cd ~/work/scummvm/builds/linux-dos-test-scumm && make -j6 test 2>&1 | grep -m3 -E "prefetch.h|error"`
Expected: `fatal error: backends/mixer/dos/prefetch.h: No such file or directory`.

- [ ] **Step 3: Write `backends/mixer/dos/prefetch.h`**

```cpp
/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef BACKENDS_MIXER_DOS_PREFETCH_H
#define BACKENDS_MIXER_DOS_PREFETCH_H

#include "audio/audiostream.h"
#include "audio/mixer_intern.h"
#include "common/scummsys.h"
#include "common/util.h"

namespace DOS {

/**
 * Decode-ahead rings for the mixer's expensive streams: speech and music
 * (FLAC, Vorbis, VOC and WAV read from the file as they play).
 *
 * On DOS the mixer's Common::Mutex is cli, and MixerImpl::mixCallback()
 * reads every stream under it: a FLAC frame decoded there holds IRQ0 off
 * for 14-42 ms (the FLAC spike) and getMillis() falls behind. A wrapped
 * stream is read ahead into a ring by prefetchAll(), interrupts on,
 * before each mix piece; under the mutex its proxy only copies. Teardown
 * waits too: when the mixer deletes a proxy (a finished line, stopHandle())
 * the slot is only marked, and reap() - interrupts on - deletes the decoder
 * and closes its file.
 *
 * Contexts: wrap() and its prime run on the main thread; prefetchAll() and
 * reap() on SDL3's audio thread, which is cooperative and runs only while
 * the main thread yields, so the two never interleave. A proxy may be
 * deleted from any context, an interrupt included: that only sets its
 * slot's state, which nothing but reap() acts on.
 */
class PrefetchPool {
public:
	static const int kSlots = 4;
	/** Samples per ring: 32 KB, 186 ms of 44.1 kHz stereo or 372 ms mono. */
	static const int kRingSamples = 16384;
	/** Samples decoded on the main thread when a stream starts. */
	static const int kPrimeSamples = 4096;

	enum State { kFree = 0, kLive = 1, kOrphan = 2 };

	struct Slot {
		volatile int state;
		Audio::AudioStream *parent;
		int16 *ring;
		int head;		///< next sample to read
		int fill;		///< samples held
		int channels;
		int markLeft;	///< marker samples still to send before the stream's own
		int markPos;	///< marker samples sent
		int markPeriod;	///< marker period, in frames
	};

	PrefetchPool() : _misses(0) {
		for (int i = 0; i < kSlots; i++) {
			Slot &s = _slots[i];
			s.state = kFree;
			s.parent = nullptr;
			s.ring = nullptr;
			s.head = s.fill = 0;
			s.channels = 1;
			s.markLeft = s.markPos = 0;
			s.markPeriod = 2;
		}
	}

	/** Deletes every stream still held. Delete the mixer (and so every proxy) first. */
	~PrefetchPool() {
		for (int i = 0; i < kSlots; i++)
			release(_slots[i]);
	}

	/**
	 * Main thread. A proxy that reads @p parent through a ring, primed with
	 * kPrimeSamples; @p parent itself when no slot or memory is free.
	 * @p markMillis > 0 puts a 1 kHz square of that length in front of the
	 * stream (dos_audio_mark: the lip-sync measurement finds it in a recording).
	 */
	Audio::AudioStream *wrap(Audio::AudioStream *parent, int markMillis = 0);

	/** Audio thread, interrupts on: tops every live ring up. */
	void prefetchAll() {
		for (int i = 0; i < kSlots; i++)
			if (_slots[i].state == kLive)
				fill(_slots[i], kRingSamples);
	}

	/** Interrupts on: deletes the streams whose proxies the mixer let go of. */
	void reap() {
		for (int i = 0; i < kSlots; i++)
			if (_slots[i].state == kOrphan)
				release(_slots[i]);
	}

	/** Reads a ring could not serve, which were decoded under the mixer's mutex. */
	uint32 misses() const { return _misses; }

	int countSlots(State st) const {
		int n = 0;
		for (int i = 0; i < kSlots; i++)
			n += (_slots[i].state == st) ? 1 : 0;
		return n;
	}

	/** The proxy's readBuffer(), under the mixer's mutex: copies; decodes only on a miss. */
	int read(Slot &s, int16 *buf, int n) {
		int done = 0;
		while (done < n && s.markLeft > 0) {
			const int frame = s.markPos / s.channels;
			buf[done++] = ((frame / (s.markPeriod / 2)) & 1) ? -16000 : 16000;
			s.markPos++;
			s.markLeft--;
		}
		while (done < n && s.fill > 0) {
			const int k = MIN(n - done, MIN(s.fill, kRingSamples - s.head));
			memcpy(buf + done, s.ring + s.head, k * sizeof(int16));
			s.head = (s.head + k) % kRingSamples;
			s.fill -= k;
			done += k;
		}
		if (done < n && !s.parent->endOfData()) {
			_misses++;
			const int got = s.parent->readBuffer(buf + done, n - done);
			if (got > 0)
				done += got;
		}
		return done;
	}

private:
	void fill(Slot &s, int limit) {
		while (s.fill < limit && !s.parent->endOfData()) {
			const int tail = (s.head + s.fill) % kRingSamples;
			int n = MIN(limit - s.fill, kRingSamples - tail);
			n -= n % s.channels;	// whole frames: the ring's edge stays on a frame
			if (n <= 0)
				break;
			const int got = s.parent->readBuffer(s.ring + tail, n);
			if (got <= 0)
				break;	// nothing now (a queue that is empty for the moment)
			s.fill += got;
		}
	}

	void release(Slot &s) {
		if (s.state == kFree)
			return;
		delete s.parent;
		free(s.ring);
		s.parent = nullptr;
		s.ring = nullptr;
		s.head = s.fill = 0;
		s.markLeft = s.markPos = 0;
		s.state = kFree;
	}

	Slot _slots[kSlots];
	uint32 _misses;
};

/** What the mixer's channel holds in place of a wrapped stream. */
class PrefetchProxy : public Audio::AudioStream {
public:
	PrefetchProxy(PrefetchPool &pool, PrefetchPool::Slot &slot, int rate, bool stereo)
		: _pool(pool), _slot(slot), _rate(rate), _stereo(stereo) {}
	~PrefetchProxy() override { _slot.state = PrefetchPool::kOrphan; }

	int readBuffer(int16 *buffer, const int numSamples) override { return _pool.read(_slot, buffer, numSamples); }
	bool isStereo() const override { return _stereo; }
	int getRate() const override { return _rate; }
	bool endOfData() const override {
		return _slot.markLeft == 0 && _slot.fill == 0 && _slot.parent->endOfData();
	}
	bool endOfStream() const override {
		return _slot.markLeft == 0 && _slot.fill == 0 && _slot.parent->endOfStream();
	}

private:
	PrefetchPool &_pool;
	PrefetchPool::Slot &_slot;
	const int _rate;
	const bool _stereo;
};

inline Audio::AudioStream *PrefetchPool::wrap(Audio::AudioStream *parent, int markMillis) {
	if (!parent)
		return nullptr;
	reap();
	Slot *s = nullptr;
	for (int i = 0; i < kSlots && !s; i++)
		if (_slots[i].state == kFree)
			s = &_slots[i];
	if (!s)
		return parent;
	int16 *ring = (int16 *)malloc(kRingSamples * sizeof(int16));
	if (!ring)
		return parent;
	const bool stereo = parent->isStereo();
	const int rate = parent->getRate();
	s->parent = parent;
	s->ring = ring;
	s->head = s->fill = 0;
	s->channels = stereo ? 2 : 1;
	s->markPeriod = MAX(2, rate / 1000);
	s->markLeft = (markMillis > 0) ? rate * markMillis / 1000 * s->channels : 0;
	s->markPos = 0;
	fill(*s, kPrimeSamples);
	s->state = kLive;
	return new PrefetchProxy(*this, *s, rate, stereo);
}

/**
 * MixerImpl that wraps the speech and music streams it is given to own
 * (DisposeAfterUse::YES) in the pool's rings. Streams it borrows, and
 * sound effects (short, in memory), play as they are.
 */
class PrefetchMixer : public Audio::MixerImpl {
public:
	typedef void (*SpeechHook)(void *ctx);
	typedef bool (*WrapGuard)();

	PrefetchMixer(PrefetchPool &pool, uint sampleRate, bool stereo, uint outBufSize)
		: Audio::MixerImpl(sampleRate, stereo, outBufSize), _pool(pool), _hook(nullptr), _hookCtx(nullptr),
		  _guard(nullptr), _markMillis(0), _speechStarts(0), _musicStarts(0) {}

	/** Called at every speech stream's start, before its prime. */
	void setSpeechHook(SpeechHook fn, void *ctx) {
		_hook = fn;
		_hookCtx = ctx;
	}
	/** When it returns false the stream plays unwrapped (DOS: interrupts are off). */
	void setWrapGuard(WrapGuard fn) { _guard = fn; }
	/** dos_audio_mark: a 1 kHz square of this length in front of every speech stream. */
	void setMarkMillis(int ms) { _markMillis = ms; }
	uint32 speechStarts() const { return _speechStarts; }
	uint32 musicStarts() const { return _musicStarts; }

	void playStream(SoundType type, Audio::SoundHandle *handle, Audio::AudioStream *input, int id, byte volume,
	                int8 balance, DisposeAfterUse::Flag autofreeStream, bool permanent, bool reverseStereo) override {
		if (input && autofreeStream == DisposeAfterUse::YES && (type == kSpeechSoundType || type == kMusicSoundType)) {
			if (type == kSpeechSoundType) {
				_speechStarts++;
				if (_hook)
					_hook(_hookCtx);
			} else {
				_musicStarts++;
			}
			if (!_guard || _guard())
				input = _pool.wrap(input, type == kSpeechSoundType ? _markMillis : 0);
		}
		Audio::MixerImpl::playStream(type, handle, input, id, volume, balance, autofreeStream, permanent, reverseStereo);
	}

private:
	PrefetchPool &_pool;
	SpeechHook _hook;
	void *_hookCtx;
	WrapGuard _guard;
	int _markMillis;
	uint32 _speechStarts;
	uint32 _musicStarts;
};

} // End of namespace DOS

#endif
```

- [ ] **Step 4: Run the tests**

Run: `cd ~/work/scummvm/builds/linux-dos-test-scumm && make -j6 test 2>&1 | tail -6`
Expected: the same `Failed N ... of T` failure list as the Task 0 baseline, with T larger by 11 (the new tests all pass). Grep the log for `dos_prefetch`: no failure lines.

- [ ] **Step 5: Commit**

```bash
cd ~/work/scummvm/dos
git add backends/mixer/dos/prefetch.h test/backends/dos_prefetch.h
git commit -m "DOS: Decode speech and music ahead of the mixer, outside its mutex" -m "$(printf 'Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4')"
```

---

### Task 5: SDL3 Sound Blaster counters (`sdl3-sb-stats.patch`) [S1]

**Files:**
- Modify (SDL3 source, its own git repo): `~/opt/src/SDL3/src/audio/dos/SDL_dosaudio_sb.c` on a new branch `dos-sb-stats` from `dos-sb-probe` (d67c987)
- Create: `backends/platform/dos/sdl3-sb-stats.patch`
- Modify: `backends/platform/dos/sdl3-build.txt`, `backends/platform/dos/build-dos.sh`, `backends/platform/dos/dos-irq.cpp`

**Interfaces:**
- Consumes: SDL3 at d67c987 with the five patches.
- Produces: `extern "C" void DOS_SBGetStats(int *irqs, int *underruns, int *queued, int *minAvail, int *chunk, int resetMin);` and `extern "C" const int DOS_SBStatsChecked;` in `~/opt/sdl3-dos-min/lib/libSDL3.a` (and `~/opt/sdl3-dos`).

- [ ] **Step 1: Branch the SDL3 source**

Run: `git -C ~/opt/src/SDL3 status --short && git -C ~/opt/src/SDL3 switch -c dos-sb-stats d67c987`
Expected: no status output (clean), `Switched to a new branch 'dos-sb-stats'`.

- [ ] **Step 2: Edit `src/audio/dos/SDL_dosaudio_sb.c`**

After `static volatile int isr_irq_count = 0; ...` add:
```c
static volatile int isr_underruns = 0;          // interrupts that found the ring empty (played silence)
static volatile int isr_min_avail = 0x7fffffff; // least ring data at an interrupt since the last reset
```
In `SoundBlasterIRQHandler`, after `const int avail = isr_ring_write - isr_ring_read; // both are monotonic` add:
```c
    if (avail < isr_min_avail) {
        isr_min_avail = avail;
    }
```
and in its `else` branch, after `ISR_Fill(dma_dst, isr_silence_value, isr_chunk_size);` add `isr_underruns++;`.
After `locked = (DOS_LockVariable(isr_irq_count) == 0) && locked;` add:
```c
        locked = (DOS_LockVariable(isr_underruns) == 0) && locked;
        locked = (DOS_LockVariable(isr_min_avail) == 0) && locked;
```
Where OpenDevice sets `isr_ring_read = 0;` and `isr_ring_write = 0;` (the block that also sets `isr_ring_buffer = hidden->ring_buffer;`), add `isr_underruns = 0;` and `isr_min_avail = 0x7fffffff;`.
After `const int DOS_SBProbeChecked = 1;` add:
```c
/* Marks a build with DOS_SBGetStats(): the ScummVM DOS port's audio statistics
   (its debug socket's `audio` command, the output latency for lip sync) read
   the handler's counters through it, and the port refuses (and fails to link
   against) an SDL3 without it. */
extern const int DOS_SBStatsChecked;
const int DOS_SBStatsChecked = 1;

/* One reading of the handler's counters, taken with interrupts off: interrupts
   so far, how many of them found the ring empty, bytes in the ring now, the
   least bytes in it at an interrupt since the last reading with reset_min (-1
   if no interrupt came), and bytes per DMA half. */
extern void DOS_SBGetStats(int *irqs, int *underruns, int *queued, int *min_avail, int *chunk, int reset_min);
void DOS_SBGetStats(int *irqs, int *underruns, int *queued, int *min_avail, int *chunk, int reset_min)
{
    Uint32 flags;
    __asm__ __volatile__("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    *irqs = isr_irq_count;
    *underruns = isr_underruns;
    *queued = isr_ring_write - isr_ring_read;
    *min_avail = (isr_min_avail == 0x7fffffff) ? -1 : isr_min_avail;
    *chunk = isr_chunk_size;
    if (reset_min) {
        isr_min_avail = 0x7fffffff;
    }
    __asm__ __volatile__("pushl %0; popfl" : : "r"(flags) : "memory");
}
```

- [ ] **Step 3: Commit in SDL3 and write the patch file**

```bash
git -C ~/opt/src/SDL3 commit -am "dos: Count Sound Blaster underruns and let the program read the handler's counters"
cd ~/work/scummvm/dos
{ cat <<'EOF'
SDL3 change the ScummVM DOS port needs for its audio statistics and lip sync:
the Sound Blaster handler counts the interrupts that found the ring empty
(underruns: silence played) and the least data in the ring at an interrupt,
and DOS_SBGetStats() reads those, the interrupt count, the ring's fill and the
DMA half size with interrupts off. The build defines DOS_SBStatsChecked, which
build-dos.sh looks for and the port links against.

Base: the five earlier patches applied (~/opt/src/SDL3 branch dos-sb-stats =
dos-sb-probe d67c987 + this). Apply after those five (git apply skips this text):
  git -C SDL3 apply sdl3-irq-code.patch && git -C SDL3 apply sdl3-cpuid.patch \
    && git -C SDL3 apply sdl3-sb-shutdown.patch && git -C SDL3 apply sdl3-sb-open-fail.patch \
    && git -C SDL3 apply sdl3-sb-probe.patch && git -C SDL3 apply sdl3-sb-stats.patch

EOF
  git -C ~/opt/src/SDL3 diff d67c987 HEAD; } > backends/platform/dos/sdl3-sb-stats.patch
```

- [ ] **Step 4: Rebuild and install both SDL3 builds**

```bash
source ~/opt/dos-dev/env.sh && cd ~/opt/src/SDL3
for b in build-dos-min build-dos; do rm -f $b/CMakeCache.txt; done
```
then configure `build-dos-min` with the exact `cmake -S SDL3 -B SDL3/build-dos-min ...` command of `backends/platform/dos/sdl3-build.txt` (run from `~/opt/src`), and `build-dos` with the options in `sdl3-irq-code.patch`'s header (prefix `~/opt/sdl3-dos`); then:
```bash
cd ~/opt/src && cmake --build SDL3/build-dos-min && cmake --install SDL3/build-dos-min && cmake --build SDL3/build-dos && cmake --install SDL3/build-dos
for l in ~/opt/sdl3-dos-min/lib/libSDL3.a ~/opt/sdl3-dos/lib/libSDL3.a; do i586-pc-msdosdjgpp-nm $l | grep -E ' [TDR] _DOS_SB(GetStats|StatsChecked)$'; done
```
Expected: four lines (`T _DOS_SBGetStats`, `R _DOS_SBStatsChecked` for each library).

- [ ] **Step 5: Port side: mark check and reference**

In `build-dos.sh`, after the `sdl3-sb-probe.patch` check, add:
```bash
# And sdl3-sb-stats.patch: the mixer reads the Sound Blaster handler's
# counters (underruns, ring fill) through DOS_SBGetStats().
if ! "$DJGPP_PREFIX/bin/i586-pc-msdosdjgpp-nm" "$sdl_lib" 2>/dev/null | grep -q ' [TDR] _DOS_SBStatsChecked$'; then
	echo "build-dos.sh: $sdl_lib lacks the Sound Blaster counters (no DOS_SBStatsChecked)." >&2
	echo "  Rebuild SDL3 as backends/platform/dos/sdl3-build.txt says (with sdl3-sb-stats.patch)." >&2
	exit 1
fi
```
In `dos-irq.cpp`, after the `DOS_SBProbeChecked` declaration add:
```cpp
// And sdl3-sb-stats.patch's: the mixer's statistics and output latency read
// the Sound Blaster handler's counters (DOS_SBGetStats()).
extern "C" const int DOS_SBStatsChecked;
```
and in `chooseLockRegime()` after `(void)*(const volatile int *)&DOS_SBProbeChecked;` add `(void)*(const volatile int *)&DOS_SBStatsChecked;`.
In `sdl3-build.txt`: name the sixth patch in the list of patches and in the apply commands (`git -C SDL3 apply sdl3-sb-stats.patch` after `sdl3-sb-probe.patch`), the branch `dos-sb-stats`, and "without sdl3-sb-stats.patch the port's audio statistics and lip-sync latency have no counters; the link fails on DOS_SBStatsChecked".

- [ ] **Step 6: Build and regress**

```bash
cd ~/work/scummvm/dos && backends/platform/dos/build-dos.sh scumm 2>&1 | tail -2 && backends/platform/dos/build-dos.sh sci 2>&1 | tail -2
SDL3_DOS_MIN=$HOME/opt/sdl3-dos backends/platform/dos/build-dos.sh sci 2>&1 | tail -1; backends/platform/dos/build-dos.sh sci 2>&1 | tail -1
cd ~/work/scummvm && python3 harness/dos/m3_accept.py x 2>&1 | tail -1
```
Expected: builds end with the dist listing (the irqcheck covers the changed `sb` handler); `M3 x: PASS`. (The second pair of sci builds proves both SDL3 trees link and leaves dist on the min build.)

- [ ] **Step 7: Commit**

```bash
git add backends/platform/dos/sdl3-sb-stats.patch backends/platform/dos/sdl3-build.txt backends/platform/dos/build-dos.sh backends/platform/dos/dos-irq.cpp
git commit -m "DOS: Require SDL3's Sound Blaster counters (sdl3-sb-stats.patch)" -m "$(printf 'Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4')"
```

---

### Task 6: The DOS mixer on the prefetch pool, 4096-frame buffers [S1]

**Files:**
- Create: `backends/mixer/dos/dos-audio-config.h`, `test/backends/dos_audio_config.h`
- Modify: `backends/mixer/dos/dos-mixer.h`, `backends/mixer/dos/dos-mixer.cpp`, `backends/platform/dos/dos.cpp` (comment only)

**Interfaces:**
- Consumes: `DOS::PrefetchPool`, `DOS::PrefetchMixer` (Task 4).
- Produces: `DOS::kDefaultAudioFrames = 4096`, `int DOS::audioDeviceFrames(const Common::String &value)`; ConfMan key `dos_audio_frames` (read in `[scummvm]` at backend init); `DosMixerManager` members `_pool`, `_prefetchMixer`, `_deviceFrames`, `_mixRate`, `_devRate`, `_devBytesPerFrame`, `_soundBlaster` used by Task 7.

- [ ] **Step 1: Write the failing test** (`test/backends/dos_audio_config.h`)

```cpp
#include <cxxtest/TestSuite.h>
#include "backends/mixer/dos/dos-audio-config.h"

class DosAudioConfigTestSuite : public CxxTest::TestSuite {
public:
	void test_default_and_junk() {
		TS_ASSERT_EQUALS(DOS::audioDeviceFrames(""), 4096);
		TS_ASSERT_EQUALS(DOS::audioDeviceFrames("4k"), 4096);
		TS_ASSERT_EQUALS(DOS::audioDeviceFrames("-2048"), 4096);
		TS_ASSERT_EQUALS(DOS::audioDeviceFrames("1234567"), 4096);
	}
	void test_powers_of_two_in_range() {
		TS_ASSERT_EQUALS(DOS::audioDeviceFrames("512"), 512);
		TS_ASSERT_EQUALS(DOS::audioDeviceFrames("2048"), 2048);
		TS_ASSERT_EQUALS(DOS::audioDeviceFrames("8192"), 8192);
		TS_ASSERT_EQUALS(DOS::audioDeviceFrames("0512"), 512);
	}
	void test_rounded_down_and_out_of_range() {
		TS_ASSERT_EQUALS(DOS::audioDeviceFrames("3000"), 2048);
		TS_ASSERT_EQUALS(DOS::audioDeviceFrames("8191"), 4096);
		TS_ASSERT_EQUALS(DOS::audioDeviceFrames("256"), 4096);
		TS_ASSERT_EQUALS(DOS::audioDeviceFrames("16384"), 4096);
	}
};
```

- [ ] **Step 2: Run it to see it fail**

Run: `cd ~/work/scummvm/builds/linux-dos-test-scumm && make -j6 test 2>&1 | grep -m1 "dos-audio-config.h"`
Expected: `fatal error: backends/mixer/dos/dos-audio-config.h: No such file or directory`.

- [ ] **Step 3: Write `backends/mixer/dos/dos-audio-config.h`** (license header as in `prefetch.h`)

```cpp
#ifndef BACKENDS_MIXER_DOS_AUDIO_CONFIG_H
#define BACKENDS_MIXER_DOS_AUDIO_CONFIG_H

#include "common/scummsys.h"
#include "common/str.h"

namespace DOS {

/** dos_audio_frames' default: 93 ms buffers and a 372 ms ring at 44100 Hz. */
const int kDefaultAudioFrames = 4096;
const int kMinAudioFrames = 512;
/** SDL3's Sound Blaster driver takes at most 32 KB a buffer: 8192 16-bit stereo frames. */
const int kMaxAudioFrames = 8192;

/**
 * The device buffer size dos_audio_frames asks for: decimal digits, rounded
 * down to a power of two (the driver's ring needs one); anything else, or a
 * value outside [512, 8192], gives the default.
 */
inline int audioDeviceFrames(const Common::String &value) {
	if (value.empty() || value.size() > 6)
		return kDefaultAudioFrames;
	int v = 0;
	for (uint i = 0; i < value.size(); i++) {
		const char c = value[i];
		if (c < '0' || c > '9')
			return kDefaultAudioFrames;
		v = v * 10 + (c - '0');
	}
	if (v < kMinAudioFrames || v > kMaxAudioFrames)
		return kDefaultAudioFrames;
	int p = kMinAudioFrames;
	while (p * 2 <= v)
		p *= 2;
	return p;
}

} // End of namespace DOS

#endif
```

- [ ] **Step 4: Run the test**

Run: `make -j6 test 2>&1 | tail -4`
Expected: baseline failures only; three more tests than after Task 4.

- [ ] **Step 5: Rewrite `backends/mixer/dos/dos-mixer.h`**

Keep the license header. Replace the class comment's buffer sentence and the class:

```cpp
#ifndef BACKENDS_MIXER_DOS_H
#define BACKENDS_MIXER_DOS_H

#include "backends/mixer/mixer.h"

struct SDL_AudioStream;

namespace DOS {
class PrefetchPool;
class PrefetchMixer;
}

/**
 * ScummVM's mixer on SDL3's DOS Sound Blaster driver: 16-bit stereo,
 * dos_audio_frames-frame device buffers (4096 by default), mixed at the
 * rate the card was opened at.
 *
 * SDL3 opens the card at its default rate, 44100 Hz, whatever the stream
 * asks for; the driver brings cards before the SB16 down to 22050 Hz.
 * A stream at another rate than the card's is converted on SDL's audio
 * thread, in floating point, which costs a DOS machine far more than the
 * mixer's own rate conversion. So once the device is open the mixer takes
 * the card's rate (as SdlMixerManager does); output_rate in [scummvm]
 * overrides it.
 *
 * SDL3's DOS threads are cooperative. Its audio thread runs only when the
 * main thread yields (SDL_Delay() in delayMillis(), the event pump) and
 * asks sdlCallback() for data then; the Sound Blaster IRQ copies SDL's
 * ring to the DMA buffer. The callback runs MixerImpl::mixCallback(),
 * which holds the mixer's Common::Mutex -- interrupts off -- so it mixes
 * in short pieces (the IRQ0 timer must not miss a tick), and it calls SDL
 * only after each piece, with interrupts back on: SDL3's DOS mutex does an
 * unconditional sti. Speech and music streams are decoded ahead outside
 * the mutex (prefetch.h): before each piece the callback tops their rings
 * up, interrupts on, and after the last it frees what the mixer let go of.
 *
 * If there is no audio device, init() leaves the mixer unset (as
 * SdlMixerManager does) and the caller falls back to NullMixerManager.
 */
class DosMixerManager : public MixerManager {
public:
	DosMixerManager();
	~DosMixerManager() override;

	void init() override;

	void suspendAudio() override;
	int resumeAudio() override;

	/**
	 * Sample frames handed to SDL so far. SDL buffers ahead, so this
	 * equals what the card took only over the long run.
	 */
	uint32 framesMixed() const { return _framesMixed; }

	/**
	 * framesMixed() and getMillis() as of the end of the last callback.
	 * The audio thread only runs while the main thread yields, so the
	 * pair is consistent when read between yields.
	 */
	void lastCallback(uint32 &frames, uint32 &millis) const {
		frames = _framesMixed;
		millis = _callbackMillis;
	}

private:
	static void sdlCallback(void *userdata, SDL_AudioStream *stream, int additionalAmount, int totalAmount);

	/**
	 * Preallocated, so the callback never allocates. Bigger requests are
	 * served in several passes.
	 */
	static const uint kBufferBytes = 16384;
	/**
	 * One mixCallback() call, interrupts off: 256 frames (5.8 ms at
	 * 44100 Hz) mix in ~0.14 ms per channel on DOSBox at 60000 cycles,
	 * well inside the IRQ0 timer's millisecond.
	 */
	static const uint kMixPieceBytes = 256 * 4;

	SDL_AudioStream *_stream;
	byte *_buffer;
	bool _subsystemInitialized;
	volatile uint32 _framesMixed;
	volatile uint32 _callbackMillis;
	DOS::PrefetchPool *_pool;
	DOS::PrefetchMixer *_prefetchMixer;	///< _mixer, as what it is
	int _deviceFrames;		///< dos_audio_frames
	int _mixRate;			///< the mixer's rate
	int _devRate;			///< the card's rate
	int _devBytesPerFrame;	///< the card's sample format x channels
	bool _soundBlaster;		///< SDL's driver is "soundblaster"
};

#endif
```

- [ ] **Step 6: Rewrite `backends/mixer/dos/dos-mixer.cpp`**

```cpp
/* (license header unchanged) */

#include "common/scummsys.h"

#if defined(DOS_DJGPP)

#include <SDL3/SDL.h>

#include "backends/mixer/dos/dos-mixer.h"
#include "backends/mixer/dos/dos-audio-config.h"
#include "backends/mixer/dos/prefetch.h"
#include "backends/platform/dos/dos-exit.h"
#include "common/config-manager.h"
#include "common/debug.h"
#include "common/system.h"
#include "common/textconsole.h"

namespace {

// A stream started with interrupts off (from a timer proc in IRQ0) plays
// unwrapped: wrapping would allocate and decode inside the interrupt.
bool interruptsOn() {
	uint32 flags;
	__asm__ __volatile__("pushfl; popl %0" : "=r"(flags));
	return (flags & 0x200) != 0;
}

} // End of anonymous namespace

DosMixerManager::DosMixerManager()
	: _stream(nullptr), _buffer(new byte[kBufferBytes]), _subsystemInitialized(false), _framesMixed(0),
	  _callbackMillis(0), _pool(nullptr), _prefetchMixer(nullptr), _deviceFrames(DOS::kDefaultAudioFrames),
	  _mixRate(0), _devRate(0), _devBytesPerFrame(0), _soundBlaster(false) {
}

DosMixerManager::~DosMixerManager() {
	if (_mixer)
		_mixer->setReady(false);
	if (_stream)
		SDL_DestroyAudioStream(_stream);	// closes the device it opened: no callback after this
	if (_subsystemInitialized)
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
	DOS::soundBlasterClosed();
	// The mixer's channels hold the proxies, the pool their streams: the
	// mixer goes first (MixerManager's destructor then deletes nothing).
	delete _mixer;
	_mixer = nullptr;
	delete _pool;
	delete[] _buffer;
}

void DosMixerManager::init() {
	// dos_audio_frames: device buffer frames, a power of two from 512 to
	// 8192, 4096 by default. The Sound Blaster driver keeps four buffers in
	// its ring: at 44100 Hz 4096 frames give 93 ms interrupts and 372 ms of
	// cushion for a main thread that does not yield (a room load on a
	// Pentium 75 blocks it for more than 186 ms: the FLAC spike saw 1-2
	// underruns at 2048 frames and none at 4096). The DMA buffer is two
	// buffers (32 KB at 16-bit stereo), and SDL takes twice that below 1 MB
	// so that it does not cross a 64 KB page.
	ConfMan.registerDefault("dos_audio_frames", DOS::kDefaultAudioFrames);
	_deviceFrames = DOS::audioDeviceFrames(ConfMan.get("dos_audio_frames"));
	SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, Common::String::format("%d", _deviceFrames).c_str());
	if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
		warning("DOS: no audio: %s", SDL_GetError());
		return;
	}
	_subsystemInitialized = true;

	const bool rateSet = ConfMan.hasKey("output_rate") && ConfMan.getInt("output_rate") > 0;
	SDL_AudioSpec spec;
	spec.format = SDL_AUDIO_S16;
	spec.channels = 2;
	spec.freq = rateSet ? ConfMan.getInt("output_rate") : 44100;
	_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, sdlCallback, this);
	if (!_stream) {
		warning("DOS: no audio device: %s", SDL_GetError());
		return;
	}

	// For the way out (DOS::soundBlasterClosed()): SDL3 masks the card's
	// IRQ whatever it was, and leaves an SB16's transfer running.
	const char *driver = SDL_GetCurrentAudioDriver();
	_soundBlaster = driver && strcmp(driver, "soundblaster") == 0;
	if (_soundBlaster)
		DOS::noteSoundBlasterOpen();

	// The card's rate is known only now: SDL asks for 44100 Hz, and the
	// driver brings cards before the SB16 down to 22050. Mix at that rate
	// unless output_rate says otherwise; the device is still paused.
	SDL_AudioSpec device;
	int frames = 0;
	if (SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(_stream), &device, &frames)) {
		if (!rateSet && device.freq > 0 && device.freq != spec.freq) {
			spec.freq = device.freq;
			if (!SDL_SetAudioStreamFormat(_stream, &spec, nullptr))
				warning("DOS: audio stream at %d Hz: %s", spec.freq, SDL_GetError());
		}
		_devRate = device.freq;
		_devBytesPerFrame = SDL_AUDIO_BYTESIZE(device.format) * device.channels;
		debug(1, "DOS: audio %s at %d Hz, %d channels, format 0x%x, %d frames; mixer at %d Hz",
			SDL_GetCurrentAudioDriver(), device.freq, device.channels, (uint)device.format, frames, spec.freq);
	}
	_mixRate = spec.freq;

	_pool = new DOS::PrefetchPool();
	_prefetchMixer = new DOS::PrefetchMixer(*_pool, spec.freq, true, frames > 0 ? frames : _deviceFrames);
	_prefetchMixer->setWrapGuard(interruptsOn);
	_mixer = _prefetchMixer;
	_mixer->setReady(true);
	// Streams from SDL_OpenAudioDeviceStream() start paused.
	SDL_ResumeAudioStreamDevice(_stream);
}

// On SDL3's audio thread, interrupts on.
void DosMixerManager::sdlCallback(void *userdata, SDL_AudioStream *stream, int additionalAmount, int totalAmount) {
	DosMixerManager *manager = (DosMixerManager *)userdata;
	// Whole frames (4 bytes); more than asked for is fine.
	uint left = (additionalAmount > 0) ? ((uint)additionalAmount + 3) & ~3u : 0;
	while (left > 0) {
		const uint n = MIN(left, kBufferBytes);
		for (uint off = 0; off < n; off += kMixPieceBytes) {
			// Interrupts on: decode ahead, so that the piece below
			// (interrupts off) only copies the speech and music rings.
			manager->_pool->prefetchAll();
			manager->_mixer->mixCallback(manager->_buffer + off, MIN(kMixPieceBytes, n - off));
		}
		// Interrupts are on again: SDL may be called.
		SDL_PutAudioStreamData(stream, manager->_buffer, n);
		manager->_framesMixed += n / 4;
		left -= n;
	}
	// What the mixer let go of (a finished line's decoder and file) is
	// freed here, interrupts on, not under its mutex.
	manager->_pool->reap();
	manager->_callbackMillis = g_system->getMillis();
}

void DosMixerManager::suspendAudio() {
	if (_stream)
		SDL_PauseAudioStreamDevice(_stream);
	_audioSuspended = true;
}

int DosMixerManager::resumeAudio() {
	if (!_audioSuspended)
		return -2;
	if (_stream && !SDL_ResumeAudioStreamDevice(_stream))
		return -1;
	_audioSuspended = false;
	return 0;
}

#endif
```

In `backends/platform/dos/dos.cpp` `mixerSelftest()`, replace the comment line `// The SDL buffers the device takes are 2048 frames, too coarse for a` with `// The SDL buffers the device takes are dos_audio_frames (4096 by default), too coarse for a`.

- [ ] **Step 7: Build both editions and smoke-test**

```bash
cd ~/work/scummvm/dos && source ~/opt/dos-dev/env.sh
backends/platform/dos/build-dos.sh scumm 2>&1 | tail -1 && backends/platform/dos/build-dos.sh sci 2>&1 | tail -1
cd ~/work/scummvm && python3 - <<'EOF'
import os, sys
sys.path.insert(0, "harness/dos")
import dosgame, scummgame
out = os.path.expanduser("~/work/scummvm/runs/mi1ute/smoke6")
g = dosgame.launch("x", os.path.expanduser("~/work/scummvm/runs/mi1ute/pack/MI1UTE/GAMES/MI1UTE"), out,
                   gameid="mi1", extra_ini=scummgame.dos_ini("mi1") + "speech_mute=false\n", debuglevel=1,
                   exe="SCUMM.EXE", client=scummgame.ScummGame,
                   extra_conf="[sblaster]\nsbtype = sb16\noplmode = opl3\n")
g.wait("loops 1500", freeze=False)
g.close()
log = open(os.path.join(out, "c", "SCUMMVM.LOG"), encoding="latin-1").read()
print([l for l in log.splitlines() if "DOS: audio" in l])
print("warnings:", [l for l in log.splitlines() if "startTalkSound" in l or "stream is 0" in l])
EOF
```
Expected: one line `DOS: audio soundblaster at 44100 Hz, 2 channels, format 0x8010, 4096 frames; mixer at 44100 Hz`; `warnings: []` (with `MONKEY.SOF` in the pack, lines now play: no "SFX file not found").

- [ ] **Step 8: SCI regression for the mixer change**

Run: `python3 harness/dos/m3_accept.py x 2>&1 | tail -1 && python3 harness/dos/m0_accept.py x 2>&1 | tail -1`
Expected: `M3 x: PASS`, `M0 x: PASS` (M3's `self` part runs the mixer self-test on the new buffers).

- [ ] **Step 9: Commit**

```bash
cd ~/work/scummvm/dos
git add backends/mixer/dos/dos-audio-config.h test/backends/dos_audio_config.h backends/mixer/dos/dos-mixer.h backends/mixer/dos/dos-mixer.cpp backends/platform/dos/dos.cpp
git commit -m "DOS: Mix through the prefetch pool, free streams outside the mutex, 4096-frame buffers" -m "$(printf 'Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4')"
```

- [ ] **Step 10: Design review (Tasks 4 + 6 together)**

Dual review per the executor notes, Claude reviewer on **opus**, package `git diff <Task 3 commit>..HEAD -- backends/mixer test/backends`. Questions for the reviewers: can any path delete a stream under the mixer mutex (count every `delete` reachable from `mixCallback`/`stopHandle`); can `prefetchAll()` touch a slot `reap()` freed; is the proxy safe when deleted from IRQ0; is destruction order safe at exit; does a borrowed or SFX stream ever get wrapped. Record verdicts in the ledger.

---

### Task 7: Audio statistics, the `audio` socket command, speech hook, latency estimate [S1/S2 measurement]

**Files:**
- Create: `backends/mixer/dos/dos-audio-stats.h`, `test/backends/dos_audio_stats.h`
- Modify: `backends/mixer/dos/dos-mixer.h`, `backends/mixer/dos/dos-mixer.cpp`, `gui/debugsocket.h`, `gui/debugsocket.cpp`

**Interfaces:**
- Consumes: `DOS_SBGetStats()` (Task 5), `DOS::haveTsc()`/`DOS::irqRdtsc()` (`backends/platform/dos/dos-irq.h`), Task 6 members.
- Produces:
  - `struct DOS::AudioStats { uint32 millis; uint64 tsc; uint32 pieces; uint64 pieceMaxTsc; uint64 prefetchTsc; uint64 mixTsc; uint32 misses; uint32 speech; uint32 music; int sbIrqs, sbUnderruns, sbQueued, sbMinAvail, sbChunk; uint32 rate, frames; }`
  - `Common::String DOS::formatAudioStats(const AudioStats &)` - the line `ms=<u> tsc=<u> pieces=<u> piecemax=<u> prefetch=<u> mix=<u> misses=<u> speech=<u> music=<u> irqs=<d> under=<d> queued=<d> minavail=<d> chunk=<d> rate=<u> frames=<u>`; `piecemax` is since the previous reading.
  - `bool DOS::audioStats(AudioStats &)`; `uint32 DosMixerManager::outputLatencyMillis() const`; debug-socket command `audio` (DOS: that line; elsewhere `FAIL no DOS mixer here`; DOS without audio: `FAIL no audio device`).
  - ConfMan `dos_audio_mark` (bool, `[scummvm]`): a 10 ms 2 kHz PC-speaker tone at every speech start and a 10 ms 1 kHz square in front of the speech stream.
  - Log line per speech start (debuglevel 1): `DOS: speech <n> latency <ms> ms`.

- [ ] **Step 1: Write the failing test** (`test/backends/dos_audio_stats.h`)

```cpp
#include <cxxtest/TestSuite.h>
#include "backends/mixer/dos/dos-audio-stats.h"

class DosAudioStatsTestSuite : public CxxTest::TestSuite {
public:
	void test_line() {
		DOS::AudioStats s;
		s.millis = 1000;
		s.tsc = 40000000ULL;
		s.pieces = 7;
		s.pieceMaxTsc = 12000;
		s.prefetchTsc = 300;
		s.mixTsc = 400;
		s.misses = 0;
		s.speech = 3;
		s.music = 1;
		s.sbIrqs = 50;
		s.sbUnderruns = 2;
		s.sbQueued = 49152;
		s.sbMinAvail = 16384;
		s.sbChunk = 16384;
		s.rate = 44100;
		s.frames = 4096;
		TS_ASSERT_EQUALS(DOS::formatAudioStats(s),
			"ms=1000 tsc=40000000 pieces=7 piecemax=12000 prefetch=300 mix=400 misses=0 speech=3 music=1 "
			"irqs=50 under=2 queued=49152 minavail=16384 chunk=16384 rate=44100 frames=4096");
	}
	void test_no_sound_blaster() {
		DOS::AudioStats s;
		TS_ASSERT_EQUALS(DOS::formatAudioStats(s),
			"ms=0 tsc=0 pieces=0 piecemax=0 prefetch=0 mix=0 misses=0 speech=0 music=0 "
			"irqs=-1 under=-1 queued=-1 minavail=-1 chunk=-1 rate=0 frames=0");
	}
};
```

- [ ] **Step 2: Run it to see it fail**

Run: `cd ~/work/scummvm/builds/linux-dos-test-scumm && make -j6 test 2>&1 | grep -m1 "dos-audio-stats.h"`
Expected: `fatal error: backends/mixer/dos/dos-audio-stats.h: No such file or directory`.

- [ ] **Step 3: Write `backends/mixer/dos/dos-audio-stats.h`** (license header as before)

```cpp
#ifndef BACKENDS_MIXER_DOS_AUDIO_STATS_H
#define BACKENDS_MIXER_DOS_AUDIO_STATS_H

#include "common/scummsys.h"
#include "common/str.h"

namespace DOS {

/**
 * What the DOS mixer has done so far, for the debug socket's `audio`
 * command (the harness's audio gate). Times are time stamp counter cycles
 * (0 without a TSC); the Sound Blaster fields come from
 * sdl3-sb-stats.patch and are -1 without a Sound Blaster.
 */
struct AudioStats {
	uint32 millis = 0;		///< getMillis() at the reading
	uint64 tsc = 0;			///< the TSC at the reading
	uint32 pieces = 0;		///< interrupts-off mix pieces so far
	uint64 pieceMaxTsc = 0;	///< the longest of them since the last reading
	uint64 prefetchTsc = 0;	///< decoding ahead so far (interrupts on)
	uint64 mixTsc = 0;		///< mixing so far (interrupts off)
	uint32 misses = 0;		///< reads a ring could not serve
	uint32 speech = 0;		///< speech streams started
	uint32 music = 0;		///< music streams started (CD tracks)
	int sbIrqs = -1;		///< Sound Blaster interrupts so far
	int sbUnderruns = -1;	///< of them, with the ring empty (silence played)
	int sbQueued = -1;		///< bytes in the ring now
	int sbMinAvail = -1;	///< least bytes in the ring at an interrupt since the last reading
	int sbChunk = -1;		///< bytes per DMA half
	uint32 rate = 0;		///< mixer rate
	uint32 frames = 0;		///< device buffer frames (dos_audio_frames)
};

inline Common::String formatAudioStats(const AudioStats &s) {
	return Common::String::format(
		"ms=%u tsc=%llu pieces=%u piecemax=%llu prefetch=%llu mix=%llu misses=%u speech=%u music=%u "
		"irqs=%d under=%d queued=%d minavail=%d chunk=%d rate=%u frames=%u",
		(uint)s.millis, (unsigned long long)s.tsc, (uint)s.pieces, (unsigned long long)s.pieceMaxTsc,
		(unsigned long long)s.prefetchTsc, (unsigned long long)s.mixTsc, (uint)s.misses, (uint)s.speech,
		(uint)s.music, s.sbIrqs, s.sbUnderruns, s.sbQueued, s.sbMinAvail, s.sbChunk, (uint)s.rate, (uint)s.frames);
}

/** Fills @p s from the running DOS mixer (dos-mixer.cpp); false without one. */
bool audioStats(AudioStats &s);

} // End of namespace DOS

#endif
```

- [ ] **Step 4: Run the test**

Run: `make -j6 test 2>&1 | tail -4`
Expected: baseline failures only; two more tests.

- [ ] **Step 5: DOS mixer additions**

In `dos-mixer.h`: add `#include "backends/mixer/dos/dos-audio-stats.h"`; in the public section add:
```cpp
	/**
	 * Milliseconds between handing the mixer a sample now and the card
	 * playing it, as far as can be told: what SDL's stream and the Sound
	 * Blaster's ring hold, the DMA half queued at the last interrupt and,
	 * on average, half of the one playing. 0 without a Sound Blaster.
	 */
	uint32 outputLatencyMillis() const;

	/** The counters behind the debug socket's `audio` (DOS::audioStats()). */
	void fillStats(DOS::AudioStats &s);
```
in the private section add `static void onSpeech(void *ctx);` after `sdlCallback`, and at the end of the member list (after `_soundBlaster`, so the constructor's list stays in declaration order):
```cpp
	DOS::AudioStats _stats;	///< pieces, pieceMaxTsc, prefetchTsc, mixTsc
	bool _haveTsc;
	bool _mark;				///< dos_audio_mark
```
In `dos-mixer.cpp`:
- add includes `#include <pc.h>`, `#include "backends/platform/dos/dos-irq.h"`, `#include "backends/mixer/dos/dos-audio-stats.h"`;
- after the includes add `extern "C" void DOS_SBGetStats(int *irqs, int *underruns, int *queued, int *minAvail, int *chunk, int resetMin);`;
- in the anonymous namespace add:
```cpp
DosMixerManager *s_manager = nullptr;

// dos_audio_mark: a 10 ms 2 kHz tone on the PC speaker (PIT channel 2),
// which reaches the speaker at once, unlike the Sound Blaster's queue.
void speakerClick() {
	const uint16 div = 1193182 / 2000;
	outportb(0x43, 0xB6);
	outportb(0x42, div & 0xff);
	outportb(0x42, div >> 8);
	outportb(0x61, inportb(0x61) | 3);
	const uint32 t0 = g_system->getMillis();
	while (g_system->getMillis() - t0 < 10) {
	}
	outportb(0x61, inportb(0x61) & ~3);
}
```
- constructor init list: add `, _haveTsc(false), _mark(false)`;
- destructor: before `delete _mixer;` add `if (s_manager == this) s_manager = nullptr;`;
- `init()`: after `_prefetchMixer->setWrapGuard(interruptsOn);` add:
```cpp
	_prefetchMixer->setSpeechHook(onSpeech, this);
	ConfMan.registerDefault("dos_audio_mark", false);
	_mark = ConfMan.getBool("dos_audio_mark");
	if (_mark)
		_prefetchMixer->setMarkMillis(10);
	_haveTsc = DOS::haveTsc();
```
and after `_mixer->setReady(true);` add `s_manager = this;`;
- replace `sdlCallback` with:
```cpp
// On SDL3's audio thread, interrupts on.
void DosMixerManager::sdlCallback(void *userdata, SDL_AudioStream *stream, int additionalAmount, int totalAmount) {
	DosMixerManager *manager = (DosMixerManager *)userdata;
	DOS::AudioStats &st = manager->_stats;
	const bool tsc = manager->_haveTsc;
	// Whole frames (4 bytes); more than asked for is fine.
	uint left = (additionalAmount > 0) ? ((uint)additionalAmount + 3) & ~3u : 0;
	while (left > 0) {
		const uint n = MIN(left, kBufferBytes);
		for (uint off = 0; off < n; off += kMixPieceBytes) {
			// Interrupts on: decode ahead, so that the piece below
			// (interrupts off) only copies the speech and music rings.
			const uint64 t0 = tsc ? DOS::irqRdtsc() : 0;
			manager->_pool->prefetchAll();
			const uint64 t1 = tsc ? DOS::irqRdtsc() : 0;
			manager->_mixer->mixCallback(manager->_buffer + off, MIN(kMixPieceBytes, n - off));
			if (tsc) {
				const uint64 t2 = DOS::irqRdtsc();
				st.prefetchTsc += t1 - t0;
				st.mixTsc += t2 - t1;
				if (t2 - t1 > st.pieceMaxTsc)
					st.pieceMaxTsc = t2 - t1;
			}
			st.pieces++;
		}
		// Interrupts are on again: SDL may be called.
		SDL_PutAudioStreamData(stream, manager->_buffer, n);
		manager->_framesMixed += n / 4;
		left -= n;
	}
	// What the mixer let go of (a finished line's decoder and file) is
	// freed here, interrupts on, not under its mutex.
	manager->_pool->reap();
	manager->_callbackMillis = g_system->getMillis();
}
```
- add the new functions:
```cpp
uint32 DosMixerManager::outputLatencyMillis() const {
	if (!_stream || !_soundBlaster || _devRate <= 0 || _devBytesPerFrame <= 0 || _mixRate <= 0)
		return 0;
	int irqs, under, queued, minAvail, chunk;
	DOS_SBGetStats(&irqs, &under, &queued, &minAvail, &chunk, 0);
	const int streamBytes = SDL_GetAudioStreamQueued(_stream);
	const uint64 streamMs = (streamBytes > 0) ? (uint64)(streamBytes / 4) * 1000 / _mixRate : 0;
	const uint64 deviceMs = (uint64)((queued + chunk + chunk / 2) / _devBytesPerFrame) * 1000 / _devRate;
	return (uint32)(streamMs + deviceMs);
}

void DosMixerManager::fillStats(DOS::AudioStats &s) {
	s = _stats;
	s.millis = g_system->getMillis();
	s.tsc = _haveTsc ? DOS::irqRdtsc() : 0;
	s.misses = _pool ? _pool->misses() : 0;
	s.speech = _prefetchMixer ? _prefetchMixer->speechStarts() : 0;
	s.music = _prefetchMixer ? _prefetchMixer->musicStarts() : 0;
	if (_soundBlaster)
		DOS_SBGetStats(&s.sbIrqs, &s.sbUnderruns, &s.sbQueued, &s.sbMinAvail, &s.sbChunk, 1);
	s.rate = _mixRate;
	s.frames = _deviceFrames;
	_stats.pieceMaxTsc = 0;
}

// Main thread, at every speech stream's start (PrefetchMixer::playStream).
void DosMixerManager::onSpeech(void *ctx) {
	DosMixerManager *m = (DosMixerManager *)ctx;
	if (m->_mark)
		speakerClick();
	debug(1, "DOS: speech %u latency %u ms", m->_prefetchMixer->speechStarts(), m->outputLatencyMillis());
}

bool DOS::audioStats(DOS::AudioStats &s) {
	if (!s_manager)
		return false;
	s_manager->fillStats(s);
	return true;
}
```
(`DOS::audioStats` is defined at file scope after the anonymous namespace, so it sees `s_manager`.)

- [ ] **Step 6: The socket command**

In `gui/debugsocket.cpp`, in the `#if defined(DOS_DJGPP)` include block add `#include "backends/mixer/dos/dos-audio-stats.h"`. Before `if (cmd == "record") {` add:
```cpp
	if (cmd == "audio") {
#if defined(DOS_DJGPP)
		DOS::AudioStats st;
		out = DOS::audioStats(st) ? DOS::formatAudioStats(st) : Common::String("FAIL no audio device");
#else
		out = "FAIL no DOS mixer here";
#endif
		return true;
	}
```
In `gui/debugsocket.h`, after the `mem` entry of the command list add:
```cpp
 *   audio                DOS: the mixer's counters now, one line of name=value
 *                        (backends/mixer/dos/dos-audio-stats.h): getMillis()
 *                        and the TSC, decode and mix time, the longest
 *                        interrupts-off piece since the last `audio`, ring
 *                        misses, speech/music starts, the Sound Blaster's
 *                        interrupts, underruns and ring fill; elsewhere FAIL
```

- [ ] **Step 7: Build, Linux tests, DOS smoke**

```bash
cd ~/work/scummvm/dos && source ~/opt/dos-dev/env.sh
backends/platform/dos/build-dos.sh scumm 2>&1 | tail -1 && backends/platform/dos/build-dos.sh sci 2>&1 | tail -1
(export PATH=$HOME/.local/sysroot/usr/bin:$PATH PKG_CONFIG_PATH=$HOME/.local/sysroot/usr/lib/x86_64-linux-gnu/pkgconfig LD_LIBRARY_PATH=$HOME/.local/sysroot/usr/lib/x86_64-linux-gnu; cd ~/work/scummvm/builds/linux-dos-test-scumm && make -j6 test 2>&1 | tail -3)
cd ~/work/scummvm && python3 - <<'EOF'
import os, sys
sys.path.insert(0, "harness/dos")
import dosgame, scummgame
out = os.path.expanduser("~/work/scummvm/runs/mi1ute/smoke7")
g = dosgame.launch("x", os.path.expanduser("~/work/scummvm/runs/mi1ute/pack/MI1UTE/GAMES/MI1UTE"), out,
                   gameid="mi1", extra_ini=scummgame.dos_ini("mi1") + "speech_mute=false\n", debuglevel=1,
                   exe="SCUMM.EXE", client=scummgame.ScummGame,
                   extra_conf="[sblaster]\nsbtype = sb16\noplmode = opl3\n")
g.wait("loops 1300", freeze=False)
print(g.cmd("audio"))
g.close()
log = open(os.path.join(out, "c", "SCUMMVM.LOG"), encoding="latin-1").read()
print([l for l in log.splitlines() if "DOS: speech" in l][:3])
EOF
```
Expected: Linux baseline failures only; the `audio` line has `tsc=` > 0, `speech=` >= 1, `irqs=` > 0, `chunk=16384`, `rate=44100`, `frames=4096`; three `DOS: speech 1 latency <n> ms` lines with n roughly 100-600.

- [ ] **Step 8: Commit** (two commits: GUI is common code)

```bash
cd ~/work/scummvm/dos
git add backends/mixer/dos/dos-audio-stats.h test/backends/dos_audio_stats.h backends/mixer/dos/dos-mixer.h backends/mixer/dos/dos-mixer.cpp
git commit -m "DOS: Count the mixer's work and the Sound Blaster's underruns; mark speech starts" -m "$(printf 'Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4')"
git add gui/debugsocket.h gui/debugsocket.cpp
git commit -m "GUI: Add the debug socket's audio command (DOS mixer counters)" -m "$(printf 'Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4')"
```

---

### Task 8: Harness - DOSBox CPU/audio options, the audio gate and the marker analysis

**Files (harness repo):**
- Modify: `harness/dos/dosgame.py`
- Create: `harness/dos/test_dosgame_conf.py`, `harness/dos/audio_mark.py`, `harness/dos/ute_audio.py`, `harness/dos/test_ute_audio.py`
- Modify: `harness/dos/m5_points.py` (`UTE_PACK` constant only; Task 14 changes the rest)

**Interfaces:**
- Consumes: the `audio` command (Task 7), the pack (Task 3).
- Produces:
  - `dosgame.launch(..., cycles=60000, cputype=None, core=None, audio_file=None)`; `dosgame._conf(out, gamedir, gameid, port, extra_conf="", exe="SCI.EXE", cpu="cycles = 60000\n")`
  - `audio_mark.load_raw(path) -> list[int]`, `audio_mark.tone_share(x, start, f) -> float`, `audio_mark.onsets(x, f) -> list[float]`, `audio_mark.pair(clicks, marks, max_s=2.0) -> list[(click_index, delay_s)]`
  - `ute_audio.parse_stats(line) -> dict`, `metrics(s0, s1, loop0, loop1, cycles) -> dict`, `gate(m, swap_bytes, warnings, min_speech=0, min_prefetch_pct=0.0) -> list[str]`, `log_warnings(text) -> list[str]`, `with_keys(ini, **kv) -> str`, `sample_window(g, loops, step=10) -> (talk, text, hangul)`, `judge(mode, talk, text, speech) -> str|None`, `linux_speech_times(strace_text) -> list[float]`, `spacing_check(dos, linux, n=20, tol=0.5) -> str|None`; CLI `ute_audio.py gate|lipsync|modes|fallback|korean|spacing`
  - `m5_points.UTE_PACK = ~/work/scummvm/runs/mi1ute/pack/MI1UTE`

- [ ] **Step 1: Write the failing tests**

`harness/dos/test_dosgame_conf.py`:
```python
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dosgame  # noqa: E402


class ConfTest(unittest.TestCase):
    def test_default_cpu_section_is_unchanged(self):
        c = dosgame._conf("/o", "/g", "kq1sci", 1234)
        self.assertIn("[cpu]\ncycles = 60000\n[serial]\n", c)

    def test_cpu_lines(self):
        c = dosgame._conf("/o", "/g", "mi1", 1234, exe="SCUMM.EXE",
                          cpu="cycles = fixed 40000\ncputype = pentium\ncore = normal\n")
        self.assertIn("[cpu]\ncycles = fixed 40000\ncputype = pentium\ncore = normal\n[serial]\n", c)
        self.assertIn("SCUMM.EXE mi1\n", c)


if __name__ == "__main__":
    unittest.main()
```

`harness/dos/test_ute_audio.py`:
```python
import math
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import audio_mark  # noqa: E402
import ute_audio  # noqa: E402

S0 = ("ms=100000 tsc=4000000000 pieces=10 piecemax=20000 prefetch=100 mix=200 misses=0 speech=0 music=0 "
      "irqs=1000 under=3 queued=49152 minavail=16384 chunk=16384 rate=44100 frames=4096")
S1 = ("ms=220000 tsc=8800000000 pieces=99 piecemax=40000 prefetch=96000100 mix=48000200 misses=0 speech=25 "
      "music=0 irqs=2290 under=3 queued=49152 minavail=0 chunk=16384 rate=44100 frames=4096")


def square(f, secs, amp=12000, rate=44100):
    n = int(secs * rate)
    half = rate / (2.0 * f)
    return [amp if int(i / half) % 2 == 0 else -amp for i in range(n)]


class StatsTest(unittest.TestCase):
    def test_parse(self):
        s = ute_audio.parse_stats(S0)
        self.assertEqual(s["under"], 3)
        self.assertEqual(s["tsc"], 4000000000)
        with self.assertRaises(RuntimeError):
            ute_audio.parse_stats("FAIL no audio device")

    def test_metrics_and_gate(self):
        m = ute_audio.metrics(ute_audio.parse_stats(S0), ute_audio.parse_stats(S1), 900, 2100, 40000)
        self.assertAlmostEqual(m["secs"], 120.0)
        self.assertAlmostEqual(m["clock"], 100.0)
        self.assertAlmostEqual(m["loops"], 10.0)
        self.assertEqual(m["under"], 0)
        self.assertEqual(m["speech"], 25)
        self.assertAlmostEqual(m["piecemax_ms"], 1.0)
        self.assertAlmostEqual(m["prefetch_pct"], 2.0)
        self.assertEqual(ute_audio.gate(m, 0, [], min_speech=20), [])
        bad = ute_audio.gate(dict(m, under=2, clock=99.4, loops=9.4, misses=1, piecemax_ms=2.5), 4096,
                             ["startTalkSound: did not find sound at offset 5"], min_speech=30)
        self.assertEqual(len(bad), 8)

    def test_log_warnings_and_keys(self):
        log = "DOS: speech 1 latency 400 ms\nWARNING: startTalkSound: SFX file not found!\nfine\n"
        self.assertEqual(ute_audio.log_warnings(log), ["WARNING: startTalkSound: SFX file not found!"])
        ini = ute_audio.with_keys("gameid=monkey\nsubtitles=true\nspeech_mute=false\n", subtitles="false")
        self.assertEqual(ini, "gameid=monkey\nspeech_mute=false\nsubtitles=false\n")

    def test_linux_speech_times(self):
        log = ('4242 12:00:01.000000 openat(AT_FDCWD, "/p/MONKEY.SOF", O_RDONLY) = 5\n'
               '4242 12:00:05.500000 openat(AT_FDCWD, "/p/MONKEY.SOF", O_RDONLY) = 6\n'
               '4242 12:00:05.600000 openat(AT_FDCWD, "/p/MONKEY.SOF", O_RDONLY|O_DIRECTORY) = -1 ENOENT (No such file)\n'
               '4242 12:00:07.250000 openat(AT_FDCWD, "/p/MONKEY.SOF", O_RDONLY) = 7\n')
        self.assertEqual(ute_audio.linux_speech_times(log), [43205.5, 43207.25])

    def test_spacing_check(self):
        linux = [0.0, 2.0, 5.0, 6.5]
        self.assertIsNone(ute_audio.spacing_check([10.0, 12.2, 15.1, 16.4], linux, n=3))
        self.assertIsNotNone(ute_audio.spacing_check([10.0, 12.9, 15.1, 16.4], linux, n=3))
        self.assertIsNotNone(ute_audio.spacing_check([10.0], linux, n=3))

    def test_judge(self):
        self.assertIsNone(ute_audio.judge("voice+text", 20, 18, 6))
        self.assertIsNone(ute_audio.judge("voice", 20, 1, 6))
        self.assertIsNone(ute_audio.judge("text", 20, 19, 0))
        self.assertIsNotNone(ute_audio.judge("text", 20, 19, 2))
        self.assertIsNotNone(ute_audio.judge("voice", 20, 15, 6))
        self.assertIsNotNone(ute_audio.judge("voice+text", 3, 3, 1))


class MarkTest(unittest.TestCase):
    def test_click_then_marker(self):
        rate = 44100
        x = [0] * rate                                   # 1 s silence
        x += square(2000, 0.010)                          # click at 1.000 s
        x += [0] * int(0.390 * rate)
        x += square(1000, 0.010)                          # marker at 1.400 s
        x += [int(8000 * math.sin(i * 0.37) * math.sin(i * 0.011)) for i in range(rate)]   # "speech"
        x += [0] * rate
        x += square(2000, 0.010)                          # click at 3.410 s
        x += [0] * int(0.150 * rate)
        x += square(1000, 0.010)                          # marker at 3.570 s
        clicks = audio_mark.onsets(x, 2000.0)
        marks = audio_mark.onsets(x, 1000.0)
        self.assertEqual(len(clicks), 2)
        self.assertEqual(len(marks), 2)
        pairs = audio_mark.pair(clicks, marks)
        self.assertEqual([i for i, _ in pairs], [0, 1])
        self.assertAlmostEqual(pairs[0][1], 0.400, delta=0.004)
        self.assertAlmostEqual(pairs[1][1], 0.160, delta=0.004)

    def test_unpaired_click(self):
        p = audio_mark.pair([1.0, 5.0], [1.25])
        self.assertEqual([i for i, _ in p], [0])
        self.assertAlmostEqual(p[0][1], 0.25)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run them to see them fail**

Run: `cd ~/work/scummvm/harness/dos && python3 -m unittest test_dosgame_conf test_ute_audio 2>&1 | tail -3`
Expected: errors (`_conf() got an unexpected keyword argument 'cpu'`, `No module named 'audio_mark'`).

- [ ] **Step 3: `dosgame.py` changes**

Replace `_conf`:
```python
def _conf(out, gamedir, gameid, port, extra_conf="", exe="SCI.EXE", cpu="cycles = 60000\n"):
    return """%s[dosbox]
machine = svga_s3
memsize = 16
[cpu]
%s[serial]
serial1 = nullmodem port:%d transparent:1
[autoexec]
mount c "%s"
mount d "%s"
c:
%s %s
exit
""" % (extra_conf, cpu, port, os.path.join(out, "c"), gamedir, exe, gameid)
```
In `launch`, add parameters `cycles=60000, cputype=None, core=None, audio_file=None` after `raw_colours=False`, document them in the docstring ("cycles: DOSBox `cycles` value (60000 or 'fixed 40000'); cputype/core: DOSBox [cpu] lines when given (the audio gate uses pentium/normal so that RDTSC counts emulated cycles); audio_file: record the emulator's output there through SDL's disk audio driver (s16le stereo 44100)"), and replace
```python
        f.write(_conf(out, gamedir, gameid, port, extra_conf, exe))
```
with
```python
        cpu = "cycles = %s\n" % cycles + ("cputype = %s\n" % cputype if cputype else "") + ("core = %s\n" % core if core else "")
        f.write(_conf(out, gamedir, gameid, port, extra_conf, exe, cpu))
```
and
```python
    env = dict(os.environ, SDL_AUDIODRIVER="dummy", DISPLAY=display)
```
with
```python
    env = dict(os.environ, SDL_AUDIODRIVER="dummy", DISPLAY=display)
    if audio_file:
        env.update(SDL_AUDIODRIVER="disk", SDL_DISKAUDIOFILE=audio_file)
```

In `m5_points.py`, after `GD = os.path.join(ROOT, "gamedata")` add:
```python
# The MI1 Ultimate Talkie pack mkute.py builds from the UTE "Midi Music"
# folder and gamedata/mi1ute-kor (plan 2026-10-04 Task 3); personal use.
UTE_PACK = os.path.join(ROOT, "runs/mi1ute/pack/MI1UTE")
```

- [ ] **Step 4: Write `harness/dos/audio_mark.py`**

```python
"""Find SCUMM.EXE's lip-sync marks in a DOSBox-X recording (dos_audio_mark=true):
the PC-speaker tone (2 kHz, 10 ms) made when a speech line starts, which
reaches the speaker at once, and the square (1 kHz, 10 ms) put in front of the
line's audio, which goes through the mixer and the Sound Blaster's queue. The
delay between the two is how far the voice lags the mouth (the mouth timer
starts with the line). Stdlib only.

    python3 audio_mark.py host.raw     (s16le stereo 44100, SDL's disk driver)
"""
import array
import math
import statistics
import sys

RATE = 44100
WIN = 220   # 5 ms
HOP = 110   # 2.5 ms


def load_raw(path):
    """The recording's mid channel (L+R)/2."""
    a = array.array("h")
    with open(path, "rb") as f:
        a.frombytes(f.read())
    if sys.byteorder == "big":
        a.byteswap()
    return [(a[i] + a[i + 1]) // 2 for i in range(0, len(a) - 1, 2)]


def tone_share(x, start, f, rate=RATE, win=WIN):
    """The share of the window's energy at frequency f (Goertzel), 0..1."""
    k = 2.0 * math.cos(2.0 * math.pi * f / rate)
    s1 = s2 = 0.0
    e = 0.0
    for v in x[start:start + win]:
        s0 = v + k * s1 - s2
        s2 = s1
        s1 = s0
        e += v * v
    if e <= 0:
        return 0.0
    p = s1 * s1 + s2 * s2 - k * s1 * s2
    return min(1.0, 2.0 * p / (win * e))


def onsets(x, f, share=0.6, min_rms=500.0, gap_s=0.1, rate=RATE):
    """Start times (s) of bursts of tone f: a window whose energy is mostly f,
    after at least gap_s without one."""
    out = []
    last = -1e9
    floor = min_rms * min_rms * WIN
    for start in range(0, len(x) - WIN, HOP):
        seg = x[start:start + WIN]
        if sum(v * v for v in seg) < floor:
            continue
        if tone_share(x, start, f, rate) >= share:
            t = start / float(rate)
            if t - last > gap_s:
                out.append(t)
            last = t
    return out


def pair(clicks, marks, max_s=2.0):
    """[(click index, delay s)]: each click with the first marker after it within max_s."""
    out = []
    j = 0
    for i, c in enumerate(clicks):
        while j < len(marks) and marks[j] < c:
            j += 1
        if j < len(marks) and marks[j] - c <= max_s:
            out.append((i, marks[j] - c))
            j += 1
    return out


def main():
    x = load_raw(sys.argv[1])
    clicks, marks = onsets(x, 2000.0), onsets(x, 1000.0)
    pairs = pair(clicks, marks)
    for i, d in pairs:
        print("line %3d at %8.3f s: voice %.0f ms after the mouth" % (i + 1, clicks[i], d * 1000))
    if pairs:
        ms = [d * 1000 for _, d in pairs]
        print("%d clicks, %d markers, %d pairs: median %.0f ms, max %.0f ms" % (
            len(clicks), len(marks), len(pairs), statistics.median(ms), max(ms)))


if __name__ == "__main__":
    main()
```

- [ ] **Step 5: Write `harness/dos/ute_audio.py`**

```python
#!/usr/bin/env python3
"""MI1 Ultimate Talkie on SCUMM.EXE under DOSBox-X (plan
docs/superpowers/plans/2026-10-04-mi1-talkie-dos.md, spec
docs/superpowers/specs/2026-10-04-mi-talkie-dos-design.md).

    ute_audio.py gate     [--target mi1] [--scenario lookout|town] [--cycles 40000] [--frames N]
    ute_audio.py lipsync  [--sb sb16|sbpro2] [--cycles 40000]
    ute_audio.py modes    [--target mi1]
    ute_audio.py fallback
    ute_audio.py korean   [--target mi1ukol]
    ute_audio.py spacing  (after `lipsync --sb sb16`)

gate: two `audio` readings LOOKOUT_START and WINDOW loops apart (lookout: the
intro's lookout dialogue, 25 lines; town: a save in Melee town, where an
ambient FLAC track plays) on a Pentium (cputype=pentium, core=normal, fixed
cycles, so RDTSC counts emulated cycles): 0 underruns, clock >= 99.5 %,
>= 9.5 loops/s, swap 0 (the spec's acceptance), no prefetch miss, longest
interrupts-off piece <= 2 ms (M5 plan Task 16 revised), no speech warning.
lipsync: dos_audio_mark=true, music and effects at volume 0, the emulator's
output recorded; audio_mark finds the click/marker pairs. modes/fallback/korean:
the spec's voice and subtitle acceptance, from `state` samples every 10 loops.
Each command ends with one line `<NAME> ...: PASS` or `... FAIL <why>`.
Runs go to ~/work/scummvm/runs/mi1ute/<name>/.
"""
import argparse
import os
import re
import shutil
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import audio_mark  # noqa: E402
import dosgame  # noqa: E402
import m5_points  # noqa: E402
import scummgame  # noqa: E402

ROOT = os.path.expanduser("~/work/scummvm")
RUNS = os.path.join(ROOT, "runs/mi1ute")
GAMEDIR = os.path.join(m5_points.UTE_PACK, "GAMES", "MI1UTE")
TOWN_SAVE = os.path.join(ROOT, "saves/mi1ute-town.s01")

LOOKOUT_START = 900     # loops: after the credits, before the first lookout line (~loop 1100)
WINDOW = 1200           # loops: ~120 s at 10 loops/s
MIN_CLOCK = 99.5
MIN_LOOPS = 9.5
MAX_PIECE_MS = 2.0
WARNINGS = ("startTalkSound:", "did not find sound", "SFX file not found", "stream is 0",
            "out of mixer slots")
MODES = {"voice+text": dict(subtitles="true", speech_mute="false"),
         "voice": dict(subtitles="false", speech_mute="false"),
         "text": dict(subtitles="true", speech_mute="true")}
SOUND_CONF = """[sblaster]
sbtype = %s
oplmode = opl3
[mixer]
rate = 44100
[speaker]
pcspeaker = true
[midi]
mpu401 = none
[dos]
lfn = false
"""


def parse_stats(line):
    """The `audio` reply as {name: int}."""
    if line.startswith("FAIL"):
        raise RuntimeError("audio: " + line)
    return {k: int(v) for k, v in (kv.split("=", 1) for kv in line.split())}


def metrics(s0, s1, loop0, loop1, cycles):
    dtsc = s1["tsc"] - s0["tsc"]
    if dtsc <= 0:
        raise RuntimeError("no time stamp counter in the readings")
    secs = dtsc / (cycles * 1000.0)
    return {"secs": secs,
            "clock": 100.0 * (s1["ms"] - s0["ms"]) / (secs * 1000.0),
            "loops": (loop1 - loop0) / secs,
            "under": s1["under"] - s0["under"],
            "misses": s1["misses"] - s0["misses"],
            "speech": s1["speech"] - s0["speech"],
            "music": s1["music"],
            "piecemax_ms": s1["piecemax"] / float(cycles),
            "prefetch_pct": 100.0 * (s1["prefetch"] - s0["prefetch"]) / dtsc,
            "mix_pct": 100.0 * (s1["mix"] - s0["mix"]) / dtsc}


def gate(m, swap_bytes, warnings, min_speech=0, min_prefetch_pct=0.0):
    """The thresholds; [] when all hold."""
    bad = []
    if m["under"] != 0:
        bad.append("underruns %d" % m["under"])
    if m["clock"] < MIN_CLOCK:
        bad.append("clock %.2f%%" % m["clock"])
    if m["loops"] < MIN_LOOPS:
        bad.append("loops/s %.2f" % m["loops"])
    if swap_bytes:
        bad.append("swap %d bytes" % swap_bytes)
    if m["misses"] != 0:
        bad.append("prefetch misses %d" % m["misses"])
    if m["piecemax_ms"] > MAX_PIECE_MS:
        bad.append("interrupts-off piece %.2f ms" % m["piecemax_ms"])
    if m["speech"] < min_speech:
        bad.append("speech lines %d < %d" % (m["speech"], min_speech))
    if m["prefetch_pct"] < min_prefetch_pct:
        bad.append("decode %.1f%% < %.1f%% (no ambient track?)" % (m["prefetch_pct"], min_prefetch_pct))
    bad += ["log: " + w for w in warnings]
    return bad


def log_warnings(text):
    return [l.strip() for l in text.splitlines() if any(w in l for w in WARNINGS)]


def with_keys(ini, **kv):
    """ini lines with each key of kv set to its value (replaced, or added at the end)."""
    lines = [l for l in ini.splitlines() if l.split("=", 1)[0] not in kv]
    return "\n".join(lines + ["%s=%s" % (k, v) for k, v in kv.items()]) + "\n"


def judge(mode, talk, text, speech):
    """None if the window's samples fit the voice mode, else why not."""
    if talk < 5:
        return "only %d talking samples" % talk
    r = text / float(talk)
    if mode in ("voice+text", "text") and r < 0.6:
        return "text in %.0f%% of talking samples" % (100 * r)
    if mode == "voice" and r > 0.2:
        return "text in %.0f%% of talking samples in voice-only" % (100 * r)
    if mode == "text" and speech != 0:
        return "%d speech lines in text-only" % speech
    if mode != "text" and speech < 1:
        return "no speech line"
    return None


def linux_speech_times(strace_text):
    """Seconds (time of day) of each open of MONKEY.SOF in an `strace -f -tt
    -e trace=openat` log - SCUMM opens the file again for every line
    (sound.cpp startTalkSound) - the first (the index read at start) left out."""
    times = []
    for l in strace_text.splitlines():
        m = re.search(r"(\d+):(\d+):(\d+\.\d+) openat\(.*monkey\.sof", l, re.I)
        if m and "ENOENT" not in l:
            h, mi, s = m.groups()
            times.append(int(h) * 3600 + int(mi) * 60 + float(s))
    return times[1:]


def spacing_check(dos, linux, n=20, tol=0.5):
    """None if the first n gaps between speech starts agree within tol s
    (the spec: clip order and spacing within 0.5 s of Linux), else why not."""
    if len(dos) < n + 1 or len(linux) < n + 1:
        return "too few lines (DOS %d, Linux %d)" % (len(dos), len(linux))
    gd = [b - a for a, b in zip(dos, dos[1:])][:n]
    gl = [b - a for a, b in zip(linux, linux[1:])][:n]
    worst = max(range(n), key=lambda i: abs(gd[i] - gl[i]))
    if abs(gd[worst] - gl[worst]) > tol:
        return "gap %d: DOS %.2f s, Linux %.2f s" % (worst + 1, gd[worst], gl[worst])
    return None


def read(path):
    try:
        with open(path, "rb") as f:
            return f.read().decode("latin-1")
    except OSError:
        return ""


def swap_bytes(out):
    p = os.path.join(out, "c", "CWSDPMI.SWP")
    return os.path.getsize(p) if os.path.exists(p) else 0


def launch(name, target, cycles, gamedir=GAMEDIR, sb="sb16", ini=None, scummvm_ini="", audio=False, c_files=None):
    out = os.path.join(RUNS, name)
    g = dosgame.launch("x", gamedir, out, gameid=target,
                       extra_ini=ini if ini is not None else scummgame.dos_ini(target),
                       scummvm_ini=scummvm_ini, debuglevel=1, extra_conf=SOUND_CONF % sb,
                       exe="SCUMM.EXE", client=scummgame.ScummGame, c_files=c_files,
                       cycles="fixed %d" % cycles, cputype="pentium", core="normal",
                       audio_file=os.path.join(out, "host.raw") if audio else None)
    return g, out


def reading(g):
    return parse_stats(g.cmd("audio")), g.state()["loop"]


def sample_window(g, loops, step=10):
    """`state` every `step` loops for `loops` loops: (samples with an actor
    talking, of them with any text drawn, of them with Hangul in the text)."""
    talk = text = hangul = 0
    for _ in range(loops // step):
        g.wait("loops %d" % step, freeze=False)
        st = g.state()
        if not st.get("talking"):
            continue
        talk += 1
        texts = [t.get("text", "") for t in st.get("texts", [])]
        if any(t.strip() for t in texts):
            text += 1
        if any(any(u"\uac00" <= ch <= u"\ud7a3" for ch in t) for t in texts):
            hangul += 1
    return talk, text, hangul


def cmd_gate(a):
    name = "gate-%s-%s-%d%s" % (a.target, a.scenario, a.cycles, "-f%d" % a.frames if a.frames else "")
    c_files = {"SAVES\\%s.s01" % a.target: TOWN_SAVE} if a.scenario == "town" else None
    g, out = launch(name, a.target, a.cycles, c_files=c_files,
                    scummvm_ini="dos_audio_frames=%d\n" % a.frames if a.frames else "")
    try:
        if a.scenario == "town":
            r = g.cmd("load 1", timeout=300)
            if not r.startswith("OK"):
                raise RuntimeError("load 1: " + r)
            g.wait("loops 100", freeze=False)
        else:
            g.wait("loops %d" % LOOKOUT_START, freeze=False)
        s0, l0 = reading(g)
        g.wait("loops %d" % WINDOW, freeze=False)
        s1, l1 = reading(g)
        mem = g.cmd("mem")
    finally:
        g.close()
    m = metrics(s0, s1, l0, l1, a.cycles)
    bad = gate(m, swap_bytes(out), log_warnings(read(os.path.join(out, "c", "SCUMMVM.LOG"))),
               min_speech=20 if a.scenario == "lookout" else 0,
               min_prefetch_pct=5.0 if a.scenario == "town" else 0.0)
    print("%s: %.0f s, clock %.2f%%, %.2f loops/s, %d underruns, %d misses, piece max %.2f ms, decode %.1f%%, "
          "mix %.1f%%, %d speech, music starts %d, min ring %d B, %s" % (
              name, m["secs"], m["clock"], m["loops"], m["under"], m["misses"], m["piecemax_ms"],
              m["prefetch_pct"], m["mix_pct"], m["speech"], m["music"], s1["minavail"], mem))
    print("GATE %s %s %d: %s" % (a.target, a.scenario, a.cycles, "PASS" if not bad else "FAIL " + "; ".join(bad)))
    return not bad


def cmd_lipsync(a):
    name = "lipsync-%s-%d" % (a.sb, a.cycles)
    ini = with_keys(scummgame.dos_ini("mi1"), music_volume="0", sfx_volume="0")
    g, out = launch(name, "mi1", a.cycles, sb=a.sb, ini=ini,
                    scummvm_ini="dos_audio_mark=true\ndebugflags=SOUND\n", audio=True)
    try:
        g.wait("loops %d" % (LOOKOUT_START + 900), freeze=False)
    finally:
        g.close()
    x = audio_mark.load_raw(os.path.join(out, "host.raw"))
    clicks, marks = audio_mark.onsets(x, 2000.0), audio_mark.onsets(x, 1000.0)
    pairs = audio_mark.pair(clicks, marks)
    log = read(os.path.join(out, "c", "SCUMMVM.LOG"))
    est = [int(v) for v in re.findall(r"DOS: speech \d+ latency (\d+) ms", log)]
    held = [int(v) for v in re.findall(r"startTalkSound: mouth held back (\d+) ms", log)]
    offs = [d * 1000.0 for _, d in pairs]
    if len(pairs) < 15:
        print("LIPSYNC %s %d: FAIL only %d click/marker pairs (%d clicks, %d markers, %d log lines)" % (
            a.sb, a.cycles, len(pairs), len(clicks), len(marks), len(est)))
        return False
    print("%d pairs: voice after mouth start median %.0f ms, max %.0f ms" % (
        len(offs), statistics.median(offs), max(offs)))
    ok = statistics.median(offs) <= 150.0
    if len(est) == len(clicks):
        err = [d * 1000.0 - est[i] for i, d in pairs]
        print("backend estimate: offset - estimate median %.0f ms, max |.| %.0f ms" % (
            statistics.median(err), max(abs(e) for e in err)))
    if held and len(held) == len(clicks):
        res = [d * 1000.0 - held[i] for i, d in pairs]
        print("engine hold-back applied: residual median %.0f ms, max |.| %.0f ms" % (
            statistics.median(res), max(abs(r) for r in res)))
        ok = abs(statistics.median(res)) <= 150.0
    print("LIPSYNC %s %d: %s" % (a.sb, a.cycles, "PASS" if ok else "FAIL offset over 150 ms (Task 13 needed)"))
    return ok


def cmd_modes(a):
    fails = []
    for mode, keys in MODES.items():
        g, out = launch("modes-%s-%s" % (a.target, mode.replace("+", "")), a.target, 60000,
                        ini=with_keys(scummgame.dos_ini(a.target), **keys))
        try:
            g.wait("loops %d" % LOOKOUT_START, freeze=False)
            s0, _ = reading(g)
            talk, text, _ = sample_window(g, 600)
            s1, _ = reading(g)
        finally:
            g.close()
        why = judge(mode, talk, text, s1["speech"] - s0["speech"])
        print("INI %s: %d talking samples, %d with text, %d speech lines: %s" % (
            mode, talk, text, s1["speech"] - s0["speech"], why or "ok"))
        if why:
            fails.append("INI %s: %s" % (mode, why))
    # Ctrl+T: the engine's v5 voice mode starts at 0 whatever the INI says, so the
    # presses give voice+text (1), text only (2), voice only (0) in that order.
    g, out = launch("modes-%s-ctrlt" % a.target, a.target, 60000,
                    ini=with_keys(scummgame.dos_ini(a.target), **MODES["voice+text"]))
    try:
        g.wait("loops 1050", freeze=False)
        for expected in ("voice+text", "text", "voice"):
            g.freeze()
            g.cmd("key 116 20 1")
            g.wait("loops 20", freeze=False)
            s0, _ = reading(g)
            talk, text, _ = sample_window(g, 120)
            s1, _ = reading(g)
            why = judge(expected, talk, text, s1["speech"] - s0["speech"])
            print("Ctrl+T -> %s: %d talking, %d with text, %d speech: %s" % (
                expected, talk, text, s1["speech"] - s0["speech"], why or "ok"))
            if why:
                fails.append("Ctrl+T %s: %s" % (expected, why))
    finally:
        g.close()
    print("MODES %s: %s" % (a.target, "PASS" if not fails else "FAIL " + "; ".join(fails)))
    return not fails


def cmd_fallback(a):
    nosof = os.path.join(RUNS, "nosof")
    os.makedirs(nosof, exist_ok=True)
    for f in os.listdir(nosof):
        os.remove(os.path.join(nosof, f))
    for f in os.listdir(GAMEDIR):
        if f.upper() != "MONKEY.SOF":
            os.symlink(os.path.join(GAMEDIR, f), os.path.join(nosof, f))
    g, out = launch("fallback", "mi1", 60000, gamedir=nosof)
    try:
        g.wait("loops %d" % LOOKOUT_START, freeze=False)
        s0, _ = reading(g)
        talk, text, _ = sample_window(g, 600)
        s1, _ = reading(g)
    finally:
        g.close()
    log = read(os.path.join(out, "c", "SCUMMVM.LOG"))
    fails = []
    why = judge("text", talk, text, s1["speech"] - s0["speech"])
    if why:
        fails.append(why)
    if "SFX file not found" not in log:
        fails.append("no 'SFX file not found' line: was MONKEY.SOF found after all?")
    print("FALLBACK: %d talking samples, %d with text, %d speech lines" % (talk, text, s1["speech"] - s0["speech"]))
    print("FALLBACK: %s" % ("PASS" if not fails else "FAIL " + "; ".join(fails)))
    return not fails


def cmd_korean(a):
    g, out = launch("korean-%s" % a.target, a.target, 60000)
    try:
        g.wait("loops %d" % LOOKOUT_START, freeze=False)
        s0, _ = reading(g)
        talk, text, hangul = sample_window(g, 800)
        s1, _ = reading(g)
    finally:
        g.close()
    log = read(os.path.join(out, "c", "SCUMMVM.LOG"))
    missing = [l for l in log.splitlines()
               if re.search(r"HiResText: U\+([0-9A-F]{4}) has no glyph", l)
               and 0xAC00 <= int(re.search(r"U\+([0-9A-F]{4})", l).group(1), 16) <= 0xD7A3]
    fails = []
    speech = s1["speech"] - s0["speech"]
    if talk < 10 or hangul < 0.6 * talk:
        fails.append("Hangul in %d of %d talking samples" % (hangul, talk))
    if speech < 5:
        fails.append("%d speech lines" % speech)
    if missing:
        fails.append("%d Hangul glyphs missing, e.g. %s" % (len(missing), missing[0]))
    fails += ["log: " + w for w in log_warnings(log)]
    print("KOREAN %s: %d talking, %d with text, %d with Hangul, %d speech lines" % (a.target, talk, text, hangul, speech))
    print("KOREAN %s: %s" % (a.target, "PASS" if not fails else "FAIL " + "; ".join(fails)))
    return not fails


def cmd_spacing(a):
    """The Linux reference under strace against the DOS clicks of the last
    `lipsync --sb sb16` run: same lines, same gaps (spec, Testing)."""
    out = os.path.join(RUNS, "spacing-linux")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(os.path.join(out, "saves"))
    wrap = os.path.join(out, "scummvm-strace")
    with open(wrap, "w") as f:
        f.write('#!/bin/sh\nexec strace -f -tt -e trace=openat -o %s/strace.log %s "$@"\n' % (out, scummgame.LINUX_SCUMM))
    os.chmod(wrap, 0o755)
    g = scummgame.ScummGame.launch(GAMEDIR, out=out, gameid="monkey", binary=wrap, headless=False, engineid="scumm",
                                   extra_ini="language=en\nplatform=pc\nsubtitles=true\nspeech_mute=false\n"
                                             "music_driver=adlib\nrandom_seed=1\n")
    try:
        g.wait("loops %d" % (LOOKOUT_START + 900), freeze=False)
    finally:
        g.close()
    linux = linux_speech_times(read(os.path.join(out, "strace.log")))
    dos = audio_mark.onsets(audio_mark.load_raw(os.path.join(RUNS, "lipsync-sb16-40000", "host.raw")), 2000.0)
    why = spacing_check(dos, linux)
    print("SPACING: DOS %d lines, Linux %d lines" % (len(dos), len(linux)))
    print("SPACING: %s" % ("PASS" if not why else "FAIL " + why))
    return not why


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("spacing")
    p = sub.add_parser("gate")
    p.add_argument("--target", default="mi1")
    p.add_argument("--scenario", default="lookout", choices=["lookout", "town"])
    p.add_argument("--cycles", type=int, default=40000)
    p.add_argument("--frames", type=int, default=0)
    p = sub.add_parser("lipsync")
    p.add_argument("--sb", default="sb16", choices=["sb16", "sbpro2"])
    p.add_argument("--cycles", type=int, default=40000)
    p = sub.add_parser("modes")
    p.add_argument("--target", default="mi1")
    sub.add_parser("fallback")
    p = sub.add_parser("korean")
    p.add_argument("--target", default="mi1ukol")
    a = ap.parse_args()
    ok = {"gate": cmd_gate, "lipsync": cmd_lipsync, "modes": cmd_modes,
          "fallback": cmd_fallback, "korean": cmd_korean, "spacing": cmd_spacing}[a.cmd](a)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
```

- [ ] **Step 6: Run the unit tests**

Run: `cd ~/work/scummvm/harness/dos && python3 -m unittest test_dosgame_conf test_ute_audio -v 2>&1 | tail -3`
Expected: `Ran 10 tests` ... `OK`.

- [ ] **Step 7: Live check of the plumbing (no gate yet)**

Run: `cd ~/work/scummvm && python3 harness/dos/ute_audio.py gate --cycles 60000 2>&1 | tail -2`
Expected: a numbers line and a `GATE mi1 lookout 60000:` line (PASS expected at 60000; any FAIL here is a finding for Task 10, not a reason to change thresholds). Also run `python3 harness/dos/m0_accept.py x | tail -1` → `M0 x: PASS` (dosgame default output unchanged).

- [ ] **Step 8: Commit (harness repo)**

```bash
cd ~/work/scummvm
git add harness/dos/dosgame.py harness/dos/test_dosgame_conf.py harness/dos/audio_mark.py harness/dos/ute_audio.py harness/dos/test_ute_audio.py harness/dos/m5_points.py
git commit -m "dos: MI1 UTE audio gate, lip-sync marks, DOSBox CPU and recording options" -m "$(printf 'Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4')"
```

---

### Task 9: A Melee town save for the ambient-track scenario

**Files:**
- Output (not committed): `~/work/scummvm/saves/mi1ute-town.s01`; ledger: room number, track number, clicks.

**Interfaces:**
- Consumes: Linux reference `builds/linux-dos-scumm/scummvm` (FLAC on), `scummgame.launch_linux`, the pack (via Task 14's `GAME_FILES`; before Task 14 lands, stage by hand as below).
- Produces: `TOWN_SAVE` used by `ute_audio.py gate --scenario town`.

- [ ] **Step 1: Linux run under strace, to the first user control**

```bash
cd ~/work/scummvm && python3 - <<'EOF'
import os, sys, shutil
sys.path.insert(0, "harness/dos")
import scummgame
out = os.path.expanduser("~/work/scummvm/runs/mi1ute/town-linux")
shutil.rmtree(out, ignore_errors=True); os.makedirs(os.path.join(out, "saves"))
wrap = os.path.join(out, "scummvm-strace")
open(wrap, "w").write("#!/bin/sh\nexec strace -f -e trace=openat -o %s/strace.log %s \"$@\"\n" % (out, scummgame.LINUX_SCUMM))
os.chmod(wrap, 0o755)
games = os.path.join(out, "game")
os.symlink(os.path.expanduser("~/work/scummvm/runs/mi1ute/pack/MI1UTE/GAMES/MI1UTE"), games)
g = scummgame.ScummGame.launch(games, out=out, gameid="monkey", binary=wrap, headless=False, engineid="scumm",
                               extra_ini="language=en\nplatform=pc\nsubtitles=true\nmusic_driver=adlib\nrandom_seed=1\n")
print(g.wait("userput", timeout=4000), g.state())
import code; code.interact(local=dict(g=g))
EOF
```
Expected: the loop number where control is given (after the lookout dialogue) and a state with the ego near the lookout.

- [ ] **Step 2: Walk to Melee town (interactive, recorded)**

In the console: use `g.dump(out + "/a")` and view `<out>/a_out.bin` (or `harness/dos/dosgame.py` dump helpers) to see the room; `g.click(x, y)` on the exit (game pixels 320x200), then `g.wait("loops 60")` and `g.state()["room"]`. After each room change check `grep -ci 'TRACK2[5-9]' <out>/strace.log`. The readme says the ambient tracks are "Melee Town and Monkey Island river": from the lookout take the path down to the dock and the street of Melee town. Stop in the first room where the grep count becomes non-zero; note the room and the track file name.

- [ ] **Step 3: Save and keep**

In the console: `g.cmd("save 1")` (reply `OK`), then `g.close()`. SciGame.launch names the ini section `g` and saves into `<out>/saves`, so copy: `cp ~/work/scummvm/runs/mi1ute/town-linux/saves/g.s01 ~/work/scummvm/saves/mi1ute-town.s01` (if `ls <out>/saves` shows another name, copy that one and record it). Record in the ledger: room number, `TRACKnn.FLA`, the clicks used, the save's size and sha256.

- [ ] **Step 4: Check the save on DOS**

Run: `cd ~/work/scummvm && python3 harness/dos/ute_audio.py gate --scenario town --cycles 60000 2>&1 | tail -2`
Expected: `music starts` >= 1 and `decode` >= 5 % in the numbers line (an ambient FLAC track decodes in the window); PASS or FAIL lines feed Task 10. No commit (no repo file changed).

---

### Task 10: The S1 gate at Pentium 75

**Files:**
- Ledger only (numbers). If a fix is needed, it goes back to the task that owns the code (Task 4/6/7) with a new failing test first.

**Interfaces:**
- Consumes: Tasks 6-9.
- Produces: the ledger table "S1 gate" (scenario, cycles, clock, loops/s, underruns, misses, piece max, decode %, mix %, DPMI free, swap).

- [ ] **Step 1: Lookout and town at 40000 cycles (P75)**

```bash
cd ~/work/scummvm
python3 harness/dos/ute_audio.py gate --scenario lookout --cycles 40000 2>&1 | tail -2
python3 harness/dos/ute_audio.py gate --scenario town --cycles 40000 2>&1 | tail -2
```
Expected: `GATE mi1 lookout 40000: PASS` and `GATE mi1 town 40000: PASS` (0 underruns, clock >= 99.5 %, >= 9.5 loops/s, swap 0, 0 misses, piece max <= 2 ms, >= 20 speech lines at the lookout, decode >= 5 % in town).

- [ ] **Step 2: Reference points (not gating)**

```bash
python3 harness/dos/ute_audio.py gate --scenario lookout --cycles 60000 2>&1 | tail -2
python3 harness/dos/ute_audio.py gate --scenario lookout --cycles 40000 --frames 2048 2>&1 | tail -2
python3 harness/dos/ute_audio.py gate --scenario town --cycles 32090 2>&1 | tail -2
```
Expected: 60000 PASS; record the 2048-frame and P60 (32090) results as they come (the spike saw 1-2 underruns at 2048: that is why 4096 is the default). Compare the decode/mix percentages with the spike table (`midi-40000-pf1`: 1.4 % + 3.1 %; `utes` 40000: 7.4 + 34.9 %); a difference over 2x is a finding to explain in the ledger.

- [ ] **Step 3: On failure**

A FAIL is not fixed by changing a threshold. Use superpowers:systematic-debugging: `piece max` over 2 ms with 0 misses points at work under the mutex other than decoding (`reap` not reached? a stream not wrapped? check `SCUMMVM.LOG` for `DOS: speech` and `pool.countSlots`); underruns with a good clock point at main-thread stalls (try `--frames 8192` to confirm, then report: the default is the spec's 4096); misses point at the ring size or a parent returning short reads. Record the ruling; the fix goes into the owning task's files with a test.

---

### Task 11: SCI regression after the mixer and SDL3 changes

**Files:** none (ledger).

**Interfaces:**
- Consumes: `dist/dos/SCI.EXE`, `dist/dos/SCUMM.EXE` built from HEAD.
- Produces: ledger lines compared with the Task 0 baseline.

- [ ] **Step 1: Build both editions from HEAD**

Run: `cd ~/work/scummvm/dos && source ~/opt/dos-dev/env.sh && backends/platform/dos/build-dos.sh sci 2>&1 | tail -1 && backends/platform/dos/build-dos.sh scumm 2>&1 | tail -1 && grep -cE '^#define USE_(FLAC|TREMOR|VORBIS|MAD)$' build-dos/config.h`
Expected: both dist listings; `0` (SCI.EXE has no codec).

- [ ] **Step 2: The gate**

```bash
cd ~/work/scummvm
for m in m0 m1 m2 m3; do python3 harness/dos/${m}_accept.py x 2>&1 | tail -1; done
python3 harness/dos/m3_accept.py staging 2>&1 | tail -1
python3 harness/dos/loading_accept.py x 2>&1 | tail -2
python3 harness/dos/loading_accept.py staging 2>&1 | tail -2
```
Expected: every line as in Task 0 Step 5 (all PASS if the baseline was all PASS). A new FAIL blocks the plan: debug it (systematic-debugging), fix in the owning task's code, rerun this task.

---

### Task 12: S2 - measure the mouth-to-voice offset

**Files:** none (ledger: the decision).

**Interfaces:**
- Consumes: `dos_audio_mark` (Task 7), `ute_audio.py lipsync` (Task 8).
- Produces: ledger ruling "S2: hook needed (median X ms SB16, Y ms SB Pro) / not needed", which decides whether Task 13 runs.

- [ ] **Step 1: SB16 (44.1 kHz) and SB Pro 2 (22.05 kHz, 8-bit)**

```bash
cd ~/work/scummvm
python3 harness/dos/ute_audio.py lipsync --sb sb16 --cycles 40000 2>&1 | tail -4
python3 harness/dos/ute_audio.py lipsync --sb sbpro2 --cycles 40000 2>&1 | tail -4
```
Expected: at least 15 pairs each; a median and max offset; the backend estimate's error line (offset minus `DOS: speech N latency L ms`). The research predicts about 0.4 s at SB16/4096 frames, and about twice that on the SB Pro (4096 frames are 186 ms there): expect `FAIL offset over 150 ms (Task 13 needed)` on both.

- [ ] **Step 2: Decide**

If either median is over 150 ms: Task 13 runs. Also check the estimate: if `offset - estimate` has a median beyond +-100 ms, the estimate in `outputLatencyMillis()` misses a term; record it, and before Task 13 adjust the estimate (with a ledger note of the measured term: e.g. the time until the next callback) so that the predicted residual is within 150 ms. If both medians are 150 ms or less: skip Task 13, record the ruling.

---

### Task 13 (conditional: runs only if Task 12 found a median over 150 ms): hold the mouth back by the output latency [S2]

**Files:**
- Modify: `audio/mixer.h` (one virtual)
- Create: `test/audio/mixer_latency.h`
- Modify: `backends/mixer/dos/prefetch.h`, `test/backends/dos_prefetch.h`, `backends/mixer/dos/dos-mixer.cpp`
- Modify: `engines/scumm/sound.h`, `engines/scumm/sound.cpp`

**Interfaces:**
- Consumes: `DosMixerManager::outputLatencyMillis()` (Task 7).
- Produces: `virtual uint32 Audio::Mixer::getOutputLatencyMillis() const` (default 0); `DOS::PrefetchMixer::setLatencyProvider(LatencyFn fn, void *ctx)` with `typedef uint32 (*LatencyFn)(void *ctx)`; SCUMM `Sound::_speechTimerDelay`; log line `startTalkSound: mouth held back <ms> ms` (debug channel `SOUND`).

- [ ] **Step 1: Write the failing tests**

`test/audio/mixer_latency.h`:
```cpp
#include <cxxtest/TestSuite.h>
#include "audio/mixer_intern.h"

class MixerLatencyTestSuite : public CxxTest::TestSuite {
public:
	void test_a_mixer_knows_no_latency_by_default() {
		Audio::MixerImpl mixer(44100, true, 1024);
		const Audio::Mixer &m = mixer;
		TS_ASSERT_EQUALS(m.getOutputLatencyMillis(), 0u);
	}
};
```
Append to `DosPrefetchMixerTestSuite` in `test/backends/dos_prefetch.h` (and add `uint32 fixedLatency(void *ctx) { return *(uint32 *)ctx; }` to the anonymous namespace):
```cpp
	void test_the_latency_comes_from_the_provider() {
		DOS::PrefetchPool pool;
		DOS::PrefetchMixer mixer(pool, 44100, true, 1024);
		const Audio::Mixer &m = mixer;
		TS_ASSERT_EQUALS(m.getOutputLatencyMillis(), 0u);
		uint32 ms = 412;
		mixer.setLatencyProvider(fixedLatency, &ms);
		TS_ASSERT_EQUALS(m.getOutputLatencyMillis(), 412u);
	}
```

- [ ] **Step 2: Run them to see them fail**

Run: `cd ~/work/scummvm/builds/linux-dos-test-scumm && make -j6 test 2>&1 | grep -m2 "getOutputLatencyMillis\|setLatencyProvider"`
Expected: `error: 'const class Audio::Mixer' has no member named 'getOutputLatencyMillis'`.

- [ ] **Step 3: `audio/mixer.h`**

In `class Mixer`, after `virtual uint getOutputBufSize() const = 0;` add:
```cpp
	/**
	 * How long a sample handed to the mixer now takes to reach the speaker,
	 * in milliseconds, as far as the backend can tell (the audio it has
	 * already queued for the device). An engine that drives animation from
	 * its own timer, such as lip sync, can hold it back by this much.
	 * 0 when the backend does not know.
	 */
	virtual uint32 getOutputLatencyMillis() const { return 0; }
```

- [ ] **Step 4: `prefetch.h` and the DOS mixer**

In `PrefetchMixer`: add `typedef uint32 (*LatencyFn)(void *ctx);`, members `LatencyFn _latencyFn; void *_latencyCtx;` (initialised to `nullptr` in the constructor's list), and:
```cpp
	/** Where getOutputLatencyMillis() comes from (DOS: the Sound Blaster's queue). */
	void setLatencyProvider(LatencyFn fn, void *ctx) {
		_latencyFn = fn;
		_latencyCtx = ctx;
	}
	uint32 getOutputLatencyMillis() const override { return _latencyFn ? _latencyFn(_latencyCtx) : 0; }
```
In `dos-mixer.cpp`, anonymous namespace: `uint32 latencyOf(void *ctx) { return ((const DosMixerManager *)ctx)->outputLatencyMillis(); }`; in `init()` after `setSpeechHook(...)`: `_prefetchMixer->setLatencyProvider(latencyOf, this);`.

- [ ] **Step 5: SCUMM**

`engines/scumm/sound.h`: after `uint _curSoundPos;` add
```cpp
	uint _speechTimerDelay;	// speech timer ticks to skip before _curSoundPos counts (output latency)
```
`engines/scumm/sound.cpp`: in the constructor's list after `_curSoundPos(0),` add `_speechTimerDelay(0),`. Replace `incrementSpeechTimer()` and `resetSpeechTimer()` with:
```cpp
void Sound::incrementSpeechTimer() {
	Common::StackLock lock(_speechTimerMutex);

	if (!_soundsPaused) {
		if (_speechTimerDelay)
			_speechTimerDelay--;
		else
			_curSoundPos++;
	}
}

void Sound::resetSpeechTimer() {
	Common::StackLock lock(_speechTimerMutex);
	_curSoundPos = 0;
	_speechTimerDelay = 0;
}
```
In `startTalkSound()`, replace
```cpp
			} else {
				_mixer->playStream(Audio::Mixer::kSpeechSoundType, handle, input, id);
			}
```
(the block under `if (!_vm->_imuseDigital) {` near the end) with
```cpp
			} else {
				_mixer->playStream(Audio::Mixer::kSpeechSoundType, handle, input, id);
				// The mouth follows _curSoundPos, which counts from here; a backend
				// with a deep output queue plays the voice that much later, so hold
				// the count back by it.
				if (mode == DIGI_SND_MODE_TALKIE) {
					const uint32 ms = _mixer->getOutputLatencyMillis();
					if (ms) {
						Common::StackLock lock(_speechTimerMutex);
						_speechTimerDelay = (uint)(ms * (_vm->getTimerFrequency() / 4) / 1000);
						debugC(DEBUG_SOUND, "startTalkSound: mouth held back %u ms", (uint)ms);
					}
				}
			}
```

- [ ] **Step 6: Tests, both builds, Linux SCUMM build**

```bash
(export PATH=$HOME/.local/sysroot/usr/bin:$PATH PKG_CONFIG_PATH=$HOME/.local/sysroot/usr/lib/x86_64-linux-gnu/pkgconfig LD_LIBRARY_PATH=$HOME/.local/sysroot/usr/lib/x86_64-linux-gnu; cd ~/work/scummvm/builds/linux-dos-test-scumm && make -j6 test 2>&1 | tail -3; cd ../linux-dos-scumm && make -j6 2>&1 | tail -1)
cd ~/work/scummvm/dos && source ~/opt/dos-dev/env.sh && backends/platform/dos/build-dos.sh scumm 2>&1 | tail -1 && backends/platform/dos/build-dos.sh sci 2>&1 | tail -1
```
Expected: baseline failures only (+2 tests); both builds link.

- [ ] **Step 7: Measure the residual**

```bash
cd ~/work/scummvm
python3 harness/dos/ute_audio.py lipsync --sb sb16 --cycles 40000 2>&1 | tail -4
python3 harness/dos/ute_audio.py lipsync --sb sbpro2 --cycles 40000 2>&1 | tail -4
```
Expected: an `engine hold-back applied: residual median ...` line with |median| <= 150 ms and `LIPSYNC sb16 40000: PASS`, `LIPSYNC sbpro2 40000: PASS`.

- [ ] **Step 8: Commits (three: common audio, engine, backend)**

```bash
cd ~/work/scummvm/dos
git add audio/mixer.h test/audio/mixer_latency.h
git commit -m "AUDIO: Let a backend report how long its output queue is" -m "$(printf 'Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4')"
git add engines/scumm/sound.h engines/scumm/sound.cpp
git commit -m "SCUMM: Hold the mouth back by the mixer's output latency" -m "$(printf 'Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4')"
git add backends/mixer/dos/prefetch.h test/backends/dos_prefetch.h backends/mixer/dos/dos-mixer.cpp
git commit -m "DOS: Report the Sound Blaster's queue as the mixer's output latency" -m "$(printf 'Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4')"
```

- [ ] **Step 9: Review on opus (engine + common audio change), dual with agy**

Package: the three commits. Ask: is the change inert for every other backend (default 0), is `_speechTimerDelay` reset on every path that resets `_curSoundPos`, is the tick conversion right for FM-Towns' timer hack, is the mutex use consistent with the IRQ0 timer proc.

---

### Task 14: Korean subtitles over the English voice: targets, fonts [S3]

**Files:**
- Modify: `dists/engine-data/hires_text/dos/M1U0.SVF`, `M1U1.SVF`, `M1U2.SVF`, `M1U4.SVF`, `M1L0.SVF`, `M1L1.SVF`, `M1L2.SVF`, `M1L4.SVF` (rebaked)
- Modify: `tools/korean/SCUMM_FONTS.md`
- Modify (harness): `harness/dos/scummgame.py`, `harness/dos/m5_points.py`

**Interfaces:**
- Consumes: `gamedata/mi1ute-kor` (Task 0), the pack (Task 3), `M1KO.MAP` (unchanged).
- Produces: harness targets `mi1` (UTE English, voice on), `mi1uko` (U preset), `mi1ukol` (L preset) on game folder `MI1UTE`; `m5_points.GAME_FILES["MI1UTE"]` = the pack's 15 files. (Harness keys `mi1ko`/`mi1kol` stay the DUMB floppy targets of M5; the pack's INI names its Korean targets `mi1ko`/`mi1kol`.)

- [ ] **Step 1: Harness targets and staging**

In `harness/dos/scummgame.py` replace the `"mi1"` entry and add two after it:
```python
    "mi1": ("gameid=monkey\nlanguage=en\nspeech_mute=false\n", "MI1UTE"),
    # The Ultimate Talkie data with the ScummVM Kor. Project's korean.trs
    # (gamedata/mi1ute-kor), English voice under Korean text; the pack's INI
    # (mkute.py) calls these mi1ko and mi1kol, names the DUMB floppy targets
    # above already have in this table.
    "mi1uko": ("gameid=monkey\nlanguage=ko\nspeech_mute=false\nhires_text_map=data:M1KO.MAP\n", "MI1UTE"),
    "mi1ukol": ("gameid=monkey\nlanguage=ko\nspeech_mute=false\nhires_text_map=data:M1KO.MAP\nrender_target=clut8\n", "MI1UTE"),
```
and update the comment above `TARGETS` (the `mi1` sentence): "`mi1` is the Ultimate Talkie Edition's "Midi Music" build as mkute.py packs it (FLAC speech in MONKEY.SOF, voice and subtitles); `mi1uko`/`mi1ukol` add its Korean text."
In `harness/dos/m5_points.py` replace the `"MI1UTE"` entry of `GAME_FILES` with:
```python
    # UTE Midi Music as mkute.py packs it (plan 2026-10-04 Task 3): 8.3 names,
    # 1 s seek points in TRACK25-29.FLA, TRACK1.WAV, FLAC speech, and the
    # ScummVM Kor. Project's korean.trs + fonts for mi1uko/mi1ukol.
    "MI1UTE": (os.path.join(UTE_PACK, "GAMES", "MI1UTE"),
               ["MONKEY.000", "MONKEY.001", "MONKEY.SOF", "TRACK1.WAV"]
               + ["TRACK%d.FLA" % n for n in range(25, 30)]
               + ["KOREAN.TRS"] + ["KOREAN%02d.FNT" % n for n in range(5)]),
```
(`os.path.join(GD, src)` with an absolute `src` gives `src`.)

- [ ] **Step 2: Census on Linux: which charsets the UTE text uses**

```bash
cd ~/work/scummvm && python3 - <<'EOF'
import os, re, sys
sys.path.insert(0, "harness/dos")
import scummgame
out = os.path.expanduser("~/work/scummvm/runs/mi1ute/census-ko")
g = scummgame.launch_linux("mi1ukol", out, extra_ini="hires_text_log=true\n")
g.wait("loops 1700", freeze=False)
g.close()
log = open(os.path.join(out, "run.log"), encoding="utf-8", errors="replace").read()
print(sorted(set(re.findall(r"HRTEXT charset=(\d+)", log))))
print(len(re.findall(r"has no glyph", log)), "missing-glyph lines")
EOF
```
Expected: a charset list that is a subset of `['0', '1', '2', '3', '4']` (M1KO.MAP has `[font.0]`..`[font.4]`), and a non-zero missing-glyph count (the shipped SVFs hold only the DUMB text's syllables). If a charset outside 0-4 shows up, stop and report it to the controller: the map then needs a new `[font.N]` section and SVF, which is a design question.

- [ ] **Step 3: Rebake M1U/M1L over the DUMB text and the UTE translation**

```bash
cd ~/work/scummvm/dos
G=~/work/scummvm/gamedata D=dists/engine-data/hires_text/dos W=/tmp/claude-1000/m1bake
mkdir -p $W
python3 tools/korean/scummtext.py $G/mi1kor $W/mi1.txt
{ cat $W/mi1.txt; printf '\xe2\x80\xa6\xe2\x84\xa2\n'; } > $W/m1u-extra.txt
{ cat $W/mi1.txt; printf '\xe2\x80\xa6\n'; } > $W/m1l-extra.txt
tools/korean/bake-scumm-fonts.sh tools/korean/scumm-fonts/m1u.tsv $G/mi1ute-kor $D $G/mi1ute-kor/korean.trs $W/m1u-extra.txt 2026,2122
tools/korean/bake-scumm-fonts.sh tools/korean/scumm-fonts/m1l.tsv $G/mi1ute-kor $D $G/mi1ute-kor/korean.trs $W/m1l-extra.txt 2026
ls -l $D/M1U?.SVF $D/M1L?.SVF
```
Expected: one `== M1xn.SVF: charset ...` line per plan row and no error; the eight SVFs are larger than before. If the L bake stops on `--require` (neodgm lacking a code point the bundle needs), record the code points in the ledger and rerun the L bake with the bundle as a plain text input instead (no `--require`):
```bash
python3 -c "import sys; sys.path.insert(0,'tools/korean'); import mkfont; print(''.join(sorted({chr(c) if isinstance(c,int) else c for c in mkfont.chars_from_trs(sys.argv[1])})))" $G/mi1ute-kor/korean.trs > $W/ute-trs.txt
cat $W/ute-trs.txt $W/m1l-extra.txt > $W/m1l-union.txt
tools/korean/bake-scumm-fonts.sh tools/korean/scumm-fonts/m1l.tsv $G/mi1ute-kor $D $W/m1l-union.txt $D/M1KO.MAP 2026
```
(the dropped code points then draw the map's `missing=` box in the L preset, as MI2's six UHC syllables do).

- [ ] **Step 4: Document the bake**

In `tools/korean/SCUMM_FONTS.md`, replace the two MI1 bake lines (`m1u.tsv ... mi1.txt $D/M1KO.MAP 2026,2122` and `m1l.tsv ... 2026`) with the Step 3 commands and one sentence: "The MI1 SVFs cover the DUMB floppy text (mi1ko/mi1kol/mi1kop of the MI1 game zip) and the Ultimate Talkie korean.trs (the MI1UTE pack's mi1ko/mi1kol); both use M1KO.MAP and the same cells (the UTE translation ships the DUMB korean0N.fnt byte for byte)."

- [ ] **Step 5: Verify on Linux (no missing Hangul) and on DOS**

Rerun the Step 2 script: expected `0 missing-glyph lines` for Hangul (any remaining line must name a non-Hangul symbol; list them in the ledger). Then:
```bash
cd ~/work/scummvm/dos && source ~/opt/dos-dev/env.sh && backends/platform/dos/build-dos.sh scumm 2>&1 | tail -1
cd ~/work/scummvm && python3 harness/dos/ute_audio.py korean --target mi1ukol 2>&1 | tail -2 && python3 harness/dos/ute_audio.py korean --target mi1uko 2>&1 | tail -2
python3 harness/dos/m5_accept.py x mi1kol mi1ko 2>&1 | tail -1
```
Expected: `KOREAN mi1ukol: PASS`, `KOREAN mi1uko: PASS`; `M5 x: PASS` for the DUMB floppy targets (they share the rebaked SVFs; m5_accept redoes its Linux reference because dist/dos/DATA changed).

- [ ] **Step 6: Commits**

```bash
cd ~/work/scummvm/dos
git add dists/engine-data/hires_text/dos/M1U0.SVF dists/engine-data/hires_text/dos/M1U1.SVF dists/engine-data/hires_text/dos/M1U2.SVF dists/engine-data/hires_text/dos/M1U4.SVF dists/engine-data/hires_text/dos/M1L0.SVF dists/engine-data/hires_text/dos/M1L1.SVF dists/engine-data/hires_text/dos/M1L2.SVF dists/engine-data/hires_text/dos/M1L4.SVF tools/korean/SCUMM_FONTS.md
git commit -m "DISTS: Bake the MI1 Korean fonts over the Ultimate Talkie translation too" -m "$(printf 'Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4')"
cd ~/work/scummvm && git add harness/dos/scummgame.py harness/dos/m5_points.py
git commit -m "dos: MI1 Ultimate Talkie targets mi1, mi1uko, mi1ukol on the mkute pack" -m "$(printf 'Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4')"
```

---

### Task 15: Release - data-free MI1UTE kit, codec licences, two-zip run [S3]

**Files (harness repo):**
- Create: `harness/dos/release/build_mi1ute.py`
- Modify: `harness/dos/release/build_engine.py`, `harness/dos/twozip_test.py`

**Interfaces:**
- Consumes: `backends/platform/dos/mkute.py`, `dist/dos/{FLAC.TXT,VORBIS.TXT}`, `pack_common` (`make_zip`, `zip_name`, `leftovers`, `short_sha`, `CRLF`, `sh`).
- Produces: `~/work/scummvm/dist/scummvm-dos-game-mi1ute-<sha>[-<suffix>].zip` with `MI1UTE\{MKUTE.PY, README.TXT, KOREAN.TXT}` (no game data); engine zip with `FLAC.TXT`, `VORBIS.TXT`; `twozip_test.py` accepts a pack folder as GAME_ZIP.

- [ ] **Step 1: `build_engine.py` ships the licences**

After the `for n in STATIC_DOCS:` loop add:
```python
    # SCUMM.EXE links libFLAC and Tremor/libogg (BSD): their notices (build-dos.sh stages them).
    for n in ("FLAC.TXT", "VORBIS.TXT"):
        p = os.path.join(a.exe_dir, n)
        if not os.path.isfile(p):
            sys.exit("%s is not in %s: build SCUMM.EXE with backends/platform/dos/build-dos.sh scumm" % (n, a.exe_dir))
        crlf_copy(p, os.path.join(T, n))
```
and add `FLAC.TXT, VORBIS.TXT` to the file list in the module docstring.

- [ ] **Step 2: `twozip_test.py` takes a pack folder**

Replace
```python
with zipfile.ZipFile(gzip_) as z:
    top = z.namelist()[0].split("/")[0]
    z.extractall(out)
pack = os.path.join(out, top)
```
with
```python
if os.path.isdir(gzip_):
    # A pack folder (MI1UTE as mkute.py writes it, game data the user made):
    # mounted read-only as it is, not copied.
    pack = os.path.abspath(gzip_)
else:
    with zipfile.ZipFile(gzip_) as z:
        top = z.namelist()[0].split("/")[0]
        z.extractall(out)
    pack = os.path.join(out, top)
```
and in the usage docstring: `GAME_ZIP  a game zip, or an unzipped pack folder (e.g. runs/mi1ute/pack/MI1UTE)`.

- [ ] **Step 3: Write `harness/dos/release/build_mi1ute.py`**

```python
#!/usr/bin/env python3
"""Build the MI1 Ultimate Talkie "kit" zip: no game data (spec decision 3),
only the host script that makes the pack from the user's own UTE files, and
its instructions.

    build_mi1ute.py [--repo DIR] [--dist DIR] [--work DIR] [--suffix S]

Output: scummvm-dos-game-mi1ute-<sha>[-<suffix>].zip, top folder MI1UTE\\
holding MKUTE.PY (backends/platform/dos/mkute.py of --repo), README.TXT and
KOREAN.TXT.
"""
import argparse
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import pack_common as pc  # noqa: E402
from pack_common import CRLF, sh  # noqa: E402

ROOT = os.path.expanduser("~/work/scummvm")

README = """\
The Secret of Monkey Island, Ultimate Talkie Edition, for ScummVM on DOS
=======================================================================

This kit holds no game data. You need your own copy of the Ultimate Talkie
Edition (UTE) of The Secret of Monkey Island, "Ultimate Talkie Version with
Midi Music" build: monkey.000, monkey.001, monkey.sof (the voice, 469 MB),
track1.flac and track25.flac to track29.flac. The engine zip (SCUMM.EXE,
PLAY.EXE) must be installed in C:\\SCUMMVM.

1. On a PC with Python 3 and FLAC's flac and metaflac programs
   (https://xiph.org/flac/), run:

     python3 MKUTE.PY <your UTE folder> <output folder>

   For Korean text too, add --korean <folder>, the folder of the ScummVM
   Kor. Project's translation of this edition (korean.trs, korean00.fnt to
   korean04.fnt). --test-clips also checks that every voice clip decodes.

2. Copy <output folder>\\MI1UTE to the DOS machine's hard disk (not to a CD:
   the voice needs fast seeks), go into it and run:

     MI1      English text and voice
     MI1KO    Korean text, English voice (true colour screen)
     MI1KOL   Korean text, English voice (8-bit screen)

Voice and subtitles are both on. In the game, Ctrl+T cycles voice and text,
text only, voice only. Without MONKEY.SOF the game still runs, text only.
The music is AdLib or MIDI (SOUND.BAT of the engine zip); the ambient tracks
of Melee town play from TRACK25-29.FLA. A Pentium 75 or faster is needed.
"""

KOREAN = """\
원숭이 섬의 비밀 얼티밋 토키 에디션 - ScummVM DOS 판
====================================================

이 묶음에는 게임 자료가 없습니다. 얼티밋 토키 에디션(UTE)의 "Ultimate Talkie
Version with Midi Music" 폴더(monkey.000, monkey.001, monkey.sof, track1.flac,
track25.flac ~ track29.flac)가 직접 있어야 합니다. 엔진 zip 은 C:\\SCUMMVM 에.

1. Python 3 와 FLAC 의 flac, metaflac 가 있는 PC 에서:

     python3 MKUTE.PY <UTE 폴더> <출력 폴더> --korean <한글 번역 폴더>

   한글 번역 폴더는 ScummVM Kor. Project 의 이 판 번역(korean.trs,
   korean00.fnt ~ korean04.fnt)입니다.

2. <출력 폴더>\\MI1UTE 를 DOS 하드 디스크에 복사하고(CD 는 안 됨) 그 안에서
   MI1KO(트루컬러) 또는 MI1KOL(8비트)을 실행합니다. 음성은 영어, 자막은 한글.

게임 안에서 Ctrl+T 로 음성+자막, 자막만, 음성만을 바꿉니다. MONKEY.SOF 가
없으면 자막만으로 진행합니다. 펜티엄 75 이상이 필요합니다.
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=os.path.join(ROOT, "dos"))
    ap.add_argument("--dist", default=os.path.join(ROOT, "dist"))
    ap.add_argument("--work", default="/tmp/mi1ute-release")
    ap.add_argument("--suffix", default=None)
    a = ap.parse_args()
    T = os.path.join(a.work, "MI1UTE")
    shutil.rmtree(a.work, ignore_errors=True)
    os.makedirs(T)
    shutil.copy2(os.path.join(a.repo, "backends/platform/dos/mkute.py"), os.path.join(T, "MKUTE.PY"))
    with open(os.path.join(T, "README.TXT"), "wb") as f:
        f.write(CRLF(README).encode("ascii"))
    with open(os.path.join(T, "KOREAN.TXT"), "wb") as f:
        f.write(CRLF(KOREAN).encode("utf-8"))
    bad = pc.leftovers(T)
    if bad:
        sys.exit("leftover files: %s" % bad)
    print(pc.make_zip(T, os.path.join(a.dist, pc.zip_name("game", "mi1ute", a.repo, a.suffix))))


if __name__ == "__main__":
    main()
```

- [ ] **Step 4: Build the zips and run the two-zip flow on the pack**

```bash
cd ~/work/scummvm
python3 harness/dos/release/build_engine.py --suffix mi1ute-test 2>&1 | tail -1
python3 harness/dos/release/build_mi1ute.py --suffix test 2>&1 | tail -1
unzip -l dist/scummvm-dos-game-mi1ute-*-test.zip
E=$(ls -t dist/scummvm-dos-engine-*-mi1ute-test.zip | head -1)
unzip -l $E | grep -E "FLAC.TXT|VORBIS.TXT|SCUMM.EXE"
BAT=MI1 ID=MI1UTE python3 harness/dos/twozip_test.py $E runs/mi1ute/pack/MI1UTE runs/mi1ute/twozip-mi1 x 2>&1 | tail -25
BAT=MI1KOL ID=MI1UTE python3 harness/dos/twozip_test.py $E runs/mi1ute/pack/MI1UTE runs/mi1ute/twozip-mi1kol x 2>&1 | tail -25
```
Expected: the kit zip lists exactly `MI1UTE/MKUTE.PY`, `MI1UTE/README.TXT`, `MI1UTE/KOREAN.TXT`; the engine zip has `SCUMMVM/FLAC.TXT`, `SCUMMVM/VORBIS.TXT`, `SCUMMVM/SCUMM.EXE`; each twozip JSON shows events `game frame` and `program exited`, `"d_changed": []`, `"saves_dir": true`, and `log_head` naming `SCUMM.EXE`.

- [ ] **Step 5: Commit (harness)**

```bash
git add harness/dos/release/build_mi1ute.py harness/dos/release/build_engine.py harness/dos/twozip_test.py
git commit -m "dos release: data-free MI1 Ultimate Talkie kit, codec licences, pack folders in twozip_test" -m "$(printf 'Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4')"
```

---

### Task 16: Final acceptance (MI1, Midi variant), SCI regression, final review

**Files:** ledger (results table). No code unless a check fails (then: the owning task, a failing test first).

**Interfaces:**
- Consumes: everything above.
- Produces: the ledger's acceptance table and the final dual review verdicts.

- [ ] **Step 1: Builds and unit tests from HEAD**

```bash
cd ~/work/scummvm/dos && source ~/opt/dos-dev/env.sh && backends/platform/dos/build-dos.sh scumm 2>&1 | tail -1 && backends/platform/dos/build-dos.sh sci 2>&1 | tail -1
python3 backends/platform/dos/test_mkute.py 2>&1 | tail -1
(export PATH=$HOME/.local/sysroot/usr/bin:$PATH PKG_CONFIG_PATH=$HOME/.local/sysroot/usr/lib/x86_64-linux-gnu/pkgconfig LD_LIBRARY_PATH=$HOME/.local/sysroot/usr/lib/x86_64-linux-gnu; cd ~/work/scummvm/builds/linux-dos-test-scumm && make -j6 test 2>&1 | tail -3)
cd ~/work/scummvm/harness/dos && python3 -m unittest test_dosgame_conf test_ute_audio 2>&1 | tail -1
```
Expected: dist listings; `OK`; the Task 0 failure list and nothing else; `OK`.

- [ ] **Step 2: Every talk line plays**

Data side (all 4393 clips): rebuild the pack with `--test-clips` as in Task 3 Step 5 → `MONKEY.SOF: all 4393 clips decode`. Game side: the gate runs below must show no `startTalkSound:`/`did not find sound`/`SFX file not found` line (they are part of `gate()`), and the lookout window at least 20 speech lines. (Headless runs cannot reach all 4393 lines; the index check plus the engine's own bsearch over the same index is the coverage for the rest - say so in the report.)

- [ ] **Step 3: The audio gate (spec thresholds, P75, 4096 frames)**

```bash
cd ~/work/scummvm
python3 harness/dos/ute_audio.py gate --target mi1 --scenario lookout --cycles 40000 | tail -1
python3 harness/dos/ute_audio.py gate --target mi1 --scenario town --cycles 40000 | tail -1
python3 harness/dos/ute_audio.py gate --target mi1ukol --scenario lookout --cycles 40000 | tail -1
python3 harness/dos/ute_audio.py gate --target mi1uko --scenario lookout --cycles 40000 | tail -1
```
Expected: PASS on the first three. The `mi1uko` line (U preset, true-colour 640x400) is recorded but not gating: its loop rate is bounded by the hi-res true-colour drawing, which this spec does not change; if it fails on loops/s only, note it as a follow-up.

- [ ] **Step 4: Voice/subtitle modes, Korean, fallback, lip sync**

```bash
python3 harness/dos/ute_audio.py modes --target mi1 | tail -1
python3 harness/dos/ute_audio.py korean --target mi1ukol | tail -1
python3 harness/dos/ute_audio.py korean --target mi1uko | tail -1
python3 harness/dos/ute_audio.py fallback | tail -1
python3 harness/dos/ute_audio.py lipsync --sb sb16 | tail -1
python3 harness/dos/ute_audio.py lipsync --sb sbpro2 | tail -1
python3 harness/dos/ute_audio.py spacing | tail -1
```
Expected: `MODES mi1: PASS` (INI voice+text, voice only, text only, and Ctrl+T), `KOREAN mi1ukol: PASS`, `KOREAN mi1uko: PASS`, `FALLBACK: PASS`, `LIPSYNC sb16 40000: PASS`, `LIPSYNC sbpro2 40000: PASS` (with Task 13, or without it if Task 12 measured <= 150 ms), `SPACING: PASS` (the first 20 gaps between speech starts within 0.5 s of the Linux reference: clip order and spacing, spec Testing).

- [ ] **Step 5: Packaging flow**

Rerun Task 15 Step 4's two `twozip_test.py` commands on the final engine zip. Expected as there.

- [ ] **Step 6: SCI regression**

Rerun Task 11 Step 2. Expected: every line as in the Task 0 baseline.

- [ ] **Step 7: Staging spot check**

Run: `python3 harness/dos/loading_accept.py staging mi1kol 2>&1 | tail -1` (the DUMB target on the rebaked SVFs under the other emulator).
Expected: `LOADING staging: PASS`.

- [ ] **Step 8: Final dual review (opus + agy)**

Package: `git diff ea562950b77..HEAD` (code) and the harness commits since Task 0. Reviewer brief: the spec, this plan, the ledger's acceptance table. Ask specifically: Global Constraints (SCI.EXE has no codec; no game data in any commit or the kit zip; no DOS ifdef in `engines/`; engine/common changes generic and separate), the prefetch/teardown concurrency argument, and the harness thresholds matching the spec. Fix what both reviewers (or the adjudication) require, with a re-review of the fixes.

- [ ] **Step 9: Report**

Ledger: acceptance table (each spec acceptance line, the command, the result line), sizes (stripped SCUMM.EXE/SCI.EXE vs Task 0), measured S1/S2 numbers, open follow-ups. Nothing is pushed or merged.

---

## Out of scope / follow-ups (later plans)

- S4: MI2 speech, `monkey2.sog` through Tremor (built and linked by this plan, not exercised); measure mono 48 kHz Tremor CPU at P75 first.
- S5: the Original CD and SE CD track variants (FLAC music for the whole game, 37-43 % CPU at P75/P60 by the spike), their seek points and packing.
- S6: 86Box or real hardware: true Pentium cost, IDE/CF and FAT16 cost of per-line seeks into a 469 MB file, SB Pro 8-bit conversion cost, OPL timing, conventional memory with PLAY.EXE resident and the 64 KB DMA allocation at 4096 frames.
- Pre-SB16 cards: 4096 frames are 186 ms buffers at 22050 Hz (a 0.74 s ring); a rate-dependent default (2048 at 22050) could halve the latency if S6 shows no stalls.
- The U preset's loop rate at P75 (Task 16 Step 3, informational).
- Merging `dos-port` into `i18n`, pushing, and the user check-in that the memory notes ask for.
