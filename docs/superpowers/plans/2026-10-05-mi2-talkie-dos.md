# MI2 Ultimate Talkie on the DOS port (S4) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Monkey Island 2: LeChuck's Revenge, Ultimate Talkie Edition plays with English voice (`monkey2.sog`, Ogg Vorbis) and English or Korean subtitles on `SCUMM.EXE` in DOSBox-X headless at Pentium 75 speed, with the spec's audio thresholds met, built from the user's own UTE files by the same host script as MI1.

**Architecture:** Nothing new in the engine or the mixer. MI1's S1-S3 work (decode-ahead `PrefetchPool`, 4096-frame SDL3 Sound Blaster buffers, the stats socket command, the mouth hold-back, `mkute.py`, `ute_audio.py`) is on `dos-port` and already channel- and rate-aware; `build-dos.sh scumm` already links Tremor and libogg, so the engine opens `monkey2.sog` through the same `Sound::startTalkSound` path. This plan adds only what MI2 needs around it: (1) a host Tremor decode checker (`tremor-check`) that proves all 6808 clips decode with the same codec the DOS build uses; (2) an MI2 profile in `mkute.py` (`--game mi2`: pack folder `MI2UTE`, targets `mi2`/`mi2ko`/`mi2kol`); (3) harness targets `mi2u`/`mi2uko`/`mi2ukol` on that folder; (4) an opt-in DOS-side measurement, `dos_vorbis_selftest`, that opens, primes and decodes a sample of clips with the TSC, because the Tremor CPU at P75 and the per-line `ov_open` cost are the two numbers the spec leaves unmeasured; (5) `ute_audio.py` generalised from hardwired MI1 to game profiles; (6) the gates, modes, Korean and fallback runs, lip-sync and the release kit for MI2.

**Tech Stack:** as the MI1 plan (DJGPP 12.2 + SDL3 DOS, Tremor + libogg for DOS, Python 3.12 stdlib, DOSBox-X 2026.08.31), plus host gcc for `tremor-check` and host ffmpeg (libvorbis) for the `mkute` test fixtures.

**Spec:** `docs/superpowers/specs/2026-10-04-mi-talkie-dos-design.md` (stage S4, Acceptance, Testing). Predecessor plan: `docs/superpowers/plans/2026-10-04-mi1-talkie-dos.md` (this plan reuses its tasks by number, "MI1 Task N").

## Rulings

Made on 2026-10-05 under the standing directive "묻지말고 진행한다" (rule, record, go on). Each is also a ledger line.

- Ruling: one `mkute.py` for both games (`--game mi1|mi2`); MI2 pack folder `MI2UTE`, INI sections `mi2`, `mi2ko`, `mi2kol`; MI1 output stays byte-identical (Task 2 proves it).
- Ruling: harness keys `mi2u`, `mi2uko`, `mi2ukol` on folder `MI2UTE` (the keys `mi2`, `mi2kol`, `mi2ko` stay the DUMB/floppy-data M5 targets on folder `MI2`, as `mi1ko`/`mi1kol` stayed for MI1). Gating targets are `mi2u` and `mi2ukol`; `mi2uko` (U preset) is recorded, not gating, as `mi1uko` was.
- Ruling: no SVF rebake. The UTE MI2 text equals the floppy text the shipped `M2*.SVF` already cover (checked in Task 0 Step 4); if that check fails, stop: a rebake is then a design question.
- Ruling: the authoritative "every clip decodes" check is the host `tremor-check` (same Tremor source and the same `ov_*` calls as the DOS build, built on the host by `build-deps.sh host`), over all 6808 clips: 9.6 s measured, 0 bad.
- Ruling: the Tremor CPU and the open+prime cost come from `dos_vorbis_selftest` run under DOSBox-X `cycles=fixed 40000`, `cputype=pentium_mmx` (the P75 setting every gate uses). Verdict thresholds (mine, because the spec gives only an estimate): decode CPU at 48 kHz mono at most 50 % of the machine, and open+prime of one line at most 100 ms (inside the spec's 150 ms mouth-to-voice budget). A miss goes to Task 9 (`-D_LOW_ACCURACY_`), not to a spec change.
- Ruling: the selftest is compiled only `#ifdef USE_VORBIS`, so `SCI.EXE` does not change; Task 6 proves it by `cmp` of the stripped EXE against the Task 0 baseline and runs the SCI gate only if it differs.
- Ruling: no unit test for the DOS-only C++ (the Linux CxxTest build compiles from `dos`, not `dos-mi2`, and the code needs the DJGPP TSC); its checks are a host `g++ -fsyntax-only`, the DOS build, and the DOSBox measurement. Parsing and verdict logic live in Python with unit tests.
- Ruling: `ute_audio.py` is edited only after the MI1 Task 16 acceptance agent has finished (a concurrent edit would change the file under a running gate). Additive-only edits to `scummgame.py` and `m5_points.py` may go earlier.
- Ruling: one DOSBox-X run at a time machine-wide. Tasks marked "DOSBox: yes" wait while the MI1 acceptance pass is running.
- Ruling: builds in `dos-mi2` run as `nice -n 19 taskset -c 0-3` with `SCUMM_DOS_DIST=~/work/scummvm/dos-mi2/dist/dos` and `SCUMM_DOS_SRC=~/work/scummvm/dos-mi2` exported for every harness command; `SCUMM_LINUX_BIN` stays the default. `dos-mi2` is rebased on `dos-port` (never merged) before the first DOSBox task if `dos-port` has moved.
- Ruling: `build-deps.sh host` does `rm -rf ~/opt/flac-host` and rebuilds; it runs when no other session is using `metaflac` (the MI1 acceptance pass does not).
- Ruling: the MI2 gate constants (start loop, window, Ctrl+T stages, minimum speech lines) start as MI1's and are fixed by the Task 7 census, not guessed.

## Global Constraints

Copied from the spec (verbatim where the spec has the words; the source is named where it does not):

- "`SCI.EXE` is unchanged." (spec, Decision 1). As in the MI1 plan: SCI.EXE gets no codec and no new feature. Task 6 compares it to the baseline.
- "Distribution: no speech or music data is distributed. A host script builds the pack from the user's own UTE files." (spec, Decision 3). `monkey2.sog` appears in no commit and no release zip.
- "Make the Ultimate Talkie Edition (UTE) ... work properly in `SCUMM.EXE` on the DOS port (Pentium-class, about 16 MB RAM)" (spec, Goal). Every DOSBox run is `memsize = 16`.
- "MI2's `.sog` is used as shipped, with no transcoding." (spec, Decision 1)
- "Keep DOS glue out of the engine" (user directive of 2026-10-03, memory note `scummvm-dos-port.md`): the selftest is `backends/platform/dos`; nothing under `engines/` or `audio/` changes in this plan. If the Task 7/8 measurements force an engine or `audio/` change, it is a generic change in its own `SCUMM:`/`AUDIO:` commit, and that is a stop-and-report, not part of this plan.
- Acceptance (spec, S4 and the MI1 lines that apply to both games): every talk line plays with no "SFX file not found" or "did not find sound" warning; voice+text, voice only, text only and Ctrl+T work; Korean subtitles over the English voice; P75 (40000 cycles) with 4096 frames gives 0 underruns, clock >= 99.5 %, >= 9.5 loops/s, swap 0; mouth-to-voice under about 150 ms; with the speech file absent the game still runs text-only.
- "Tremor CPU for MI2 speech is an estimate" (spec, Risks): 20-30 % at P75. Task 7 measures it.

Project rules (from the earlier plans and the user):

- Code: branch `dos-mi2` (from `dos-port` d966f6a5fbc), worktree `~/work/scummvm/dos-mi2`. Harness: `~/work/scummvm` (git root, branch `master`), files under `harness/`; `dos-mi2` is a nested repo, untracked there. `git add` named files only. Never a bare `git stash`. Never push, never merge. Never touch `harness/i18n/kq1plan.py`, `harness/i18n/kq1walkthrough.py`, or any `dump.mid`.
- Commit trailer: the attribution lines the executing session's system prompt gives. The commands below carry this planning session's lines (`Co-Authored-By: Claude Code <noreply@anthropic.com>`, `Claude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4`) as one `-m` paragraph; an executor whose prompt gives other lines substitutes them.
- 8.3 names for everything on the DOS side; target names at most 8 characters (`mi2ukol` is 7).
- Game data is read-only. Game-derived files (packs, saves, recordings) stay under `~/work/scummvm/runs`, `~/work/scummvm/saves`, `~/work/scummvm/gamedata`: never in a git commit.
- Toolchain: `source ~/opt/dos-dev/env.sh` before any DJGPP command. Linux env:
  `export PATH=$HOME/.local/sysroot/usr/bin:$PATH PKG_CONFIG_PATH=$HOME/.local/sysroot/usr/lib/x86_64-linux-gnu/pkgconfig LD_LIBRARY_PATH=$HOME/.local/sysroot/usr/lib/x86_64-linux-gnu`
- Redirect the harness at the new worktree in every shell that runs it:
  `export SCUMM_DOS_DIST=$HOME/work/scummvm/dos-mi2/dist/dos SCUMM_DOS_SRC=$HOME/work/scummvm/dos-mi2`
- Ledger: `~/work/scummvm/dos-mi2/.superpowers/sdd/2026-10-05-mi2-talkie-dos/progress.md` (git-ignored). Every ruling, measured number, deviation and review verdict goes there.

## Executor notes: reviews, models, DOSBox

- Every review is triple (user rule): a Claude subagent reviewer, a read-only agy/Gemini review and a read-only hermes review with `-m zen-glm-5-3`, in parallel, on the same package. From `~/work/scummvm/dos-mi2`:
  - agy: `agy --mode plan --model gemini-3.1-pro-high --sandbox --add-dir .superpowers/sdd/2026-10-05-mi2-talkie-dos -p "<read-only, file-read/search tools only, no edits or builds; package path; findings by severity with file:line, scenario and fix; final VERDICT line>" > .superpowers/sdd/2026-10-05-mi2-talkie-dos/agy-<name>.md`
  - hermes: `hermes -p dosreview chat -Q --max-turns 40 --run-budget 1500 --in "$PWD" --query-file .superpowers/sdd/2026-10-05-mi2-talkie-dos/<name>-prompt.md > .superpowers/sdd/2026-10-05-mi2-talkie-dos/glm-<name>.md` (the `dosreview` profile is read-only by hook).
  - The MI1 SDD workspace has the working wrappers: copy `agy-review.sh` and `glm-review.sh` from `~/work/scummvm/dos/.superpowers/sdd/2026-10-04-mi1-talkie-dos/` in Task 0.
  - Verify each external finding against the code before acting; a review passes only when all are clean or the remaining items are adjudicated in the ledger.
- Models: pass `model` explicitly on every Agent call. Sonnet for routine subagents (all implementers here, scoped re-reviews, measurement runs). Opus only for the Task 13 final review. This plan has no design-level code of the MI1 Task 4/6 kind (no mixer, no concurrency), so the only mid-plan review is the Task 4/5 triple review at sonnet.
- DOSBox: one run at a time; a gate run takes minutes. Each task below says "DOSBox: yes/no". Tasks 0 (Step 5 only), 7, 8, 9, 10, 11, 12 (Step 6 only) and 13 need it; the rest do not and can run while another session uses DOSBox, except that none may change `~/work/scummvm/dos/dist/dos`.
- Hermes workers (user-approved distribution, 2026-10-05; Linux-only tasks are the pilot): Tasks 1 and 3 are mechanical and fit a flash-class worker; one card per task, worktree `dos-mi2`, report file as the completion contract; tracking cards stay blocked. Not required: a sonnet subagent may do them.

## Numbers in this plan: what was measured on 2026-10-05 and what was not

| fact | measured |
|---|---|
| `monkey2.sog` | 135,277,340 B; Ogg Vorbis; index = uint32 BE n, then n 16-byte BE entries (org_offset, new_offset, tag_bytes, size), 6808 clips; clip data starts at `new_offset + n + 4 + tag_bytes` (tag_bytes = 2) |
| decode of all 6808 clips with host Tremor | 9.6 s, 0 bad, 593,963,273 samples |
| sample rates | 22050-48236 Hz (not "about 48 kHz": two clips are 22 kHz, the rest 44.1-48 kHz); one clip (index 6583) is stereo |
| game data | `gamedata/mi2kor/`: monkey2.000/.001/.sog, korean.trs, korean00..05,07,08.fnt (no 06); music is AdLib only |
| DOS link | `build-dos.sh scumm` already links libFLAC, libogg, Tremor (`USE_TREMOR`, `USE_VORBIS`); `.sog` is reachable on DOS |
| harness | `scummgame.TARGETS` has `mi2`, `mi2kol`, `mi2ko` on folder `MI2` (floppy data); no UTE targets |

Not measured (the tasks named measure them): Tremor CPU at P75 for 44-48 kHz mono (Task 7), `ov_open` + first-block cost per line (Task 7), the MI2 intro's speech-line density and gate constants (Task 7), mouth-to-voice offset on MI2 (Task 11), Ctrl+T timing in MI2 (Task 10), the U preset's loop rate (Task 8, informational).

## File structure

Code, branch `dos-mi2` (worktree `~/work/scummvm/dos-mi2`):
- `backends/platform/dos/mkute.py`, `test_mkute.py` - modify (Task 2).
- `backends/platform/dos/build-deps.sh` - modify (Task 1); `backends/platform/dos/tremor-check.c` - create (Task 1).
- `backends/platform/dos/dos-vorbis-selftest.h`, `dos-vorbis-selftest.cpp` - create; `dos.cpp`, `module.mk` - modify (Task 4).
- `backends/platform/dos/README.md` - modify (Task 12).

Harness, `~/work/scummvm` (master):
- `harness/dos/scummgame.py`, `m5_points.py` - additive (Task 3).
- `harness/dos/ute_audio.py`, `test_ute_audio.py` - modify (Task 5).
- `harness/dos/release/build_mi2ute.py` - create (Task 12).

Runs (never committed): `runs/mi2ute/pack/` (the pack), `runs/mi2ute/<run>/` (gate runs).

---

### Task 0: Preflight - baselines for the new worktree  [DOSBox: Step 5 only]

**Files:**
- Create: `~/work/scummvm/dos-mi2/.superpowers/sdd/2026-10-05-mi2-talkie-dos/progress.md`, `agy-review.sh`, `glm-review.sh` (all git-ignored)

**Interfaces:**
- Consumes: nothing.
- Produces: a ledger with the HEAD, the stripped sizes of the baseline `SCUMM.EXE`/`SCI.EXE` of `dos-mi2`, copies `SCUMM-base.EXE`/`SCI-base.EXE`, the harness unit-test baseline, and (Step 4) the evidence that `M2*.SVF` need no rebake.

- [ ] **Step 1: Clean tree and branch check**

```bash
cd ~/work/scummvm/dos-mi2 && git status --short && git branch --show-current && git log --oneline -1 && git merge-base --is-ancestor d966f6a5fbc HEAD && echo OK-BASE
cd ~/work/scummvm/dos && git log --oneline -1
```
Expected: no `status` output; `dos-mi2`; `OK-BASE`. If `dos` HEAD is no longer `d966f6a5fbc`, rebase now: `cd ~/work/scummvm/dos-mi2 && git rebase dos-port` (no local commits yet, so it is a fast-forward), and record the new HEAD. Never `git stash`.

- [ ] **Step 2: Ledger and review wrappers**

```bash
D=~/work/scummvm/dos-mi2/.superpowers/sdd/2026-10-05-mi2-talkie-dos; mkdir -p $D
cp ~/work/scummvm/dos/.superpowers/sdd/2026-10-04-mi1-talkie-dos/{agy-review.sh,glm-review.sh} $D/
printf '# MI2 Ultimate Talkie - ledger\n\nPlan: docs/superpowers/plans/2026-10-05-mi2-talkie-dos.md\nHEAD at start: %s\n\n## Rulings\n(copy the plan'"'"'s Rulings section here)\n' "$(git -C ~/work/scummvm/dos-mi2 log --oneline -1)" > $D/progress.md
git -C ~/work/scummvm/dos-mi2 check-ignore -v .superpowers/sdd/2026-10-05-mi2-talkie-dos/progress.md
```
Expected: `check-ignore` prints the ignoring rule (the ledger is not committable). If it prints nothing, add `.superpowers/` to `.git/info/exclude` of the worktree (not to a tracked file).

- [ ] **Step 3: DOS builds and sizes at the baseline**

```bash
cd ~/work/scummvm/dos-mi2 && source ~/opt/dos-dev/env.sh
nice -n 19 taskset -c 0-3 backends/platform/dos/build-dos.sh scumm 2>&1 | tail -3
nice -n 19 taskset -c 0-3 backends/platform/dos/build-dos.sh sci 2>&1 | tail -3
D=.superpowers/sdd/2026-10-05-mi2-talkie-dos
cp dist/dos/SCUMM.EXE $D/SCUMM-base.EXE && cp dist/dos/SCI.EXE $D/SCI-base.EXE
for e in SCUMM SCI; do cp dist/dos/$e.EXE $D/$e.s && i586-pc-msdosdjgpp-strip $D/$e.s && echo $e $(stat -c %s $D/$e.s); done
```
Expected: both builds end with the `ls -la dist/dos` listing; sizes recorded. (The build directories `build-dos-scumm/` and `build-dos/` are inside `dos-mi2`; they do not touch `dos`. First build is a full build: use the `nice`/`taskset` prefix.)

- [ ] **Step 4: The UTE MI2 text needs no rebake**

```bash
cd ~/work/scummvm/gamedata && ls -l mi2kor/ && sha256sum mi2kor/korean.trs mi2kor/korean0[0-58].fnt
ls ~/work/scummvm/dos-mi2/dists/engine-data/hires_text/dos/M2*.SVF ~/work/scummvm/dos-mi2/dists/engine-data/hires_text/dos/M2KO.MAP
```
`gamedata/mi2kor/` is the UTE data (monkey2.sog is in it) with the Kor. Project's `korean.trs`/fonts that the floppy M5 targets already use. Expected: the files listed in the numbers table, and the `M2*.SVF` and `M2KO.MAP` present. Record the hashes. Ruling holds (no rebake) unless the Task 7 Korean run finds missing-glyph lines for Hangul: then stop and report.

- [ ] **Step 5: Harness baseline, including the text-only Korean M5 runs  [DOSBox: yes]**

```bash
cd ~/work/scummvm/harness/dos && python3 -m unittest test_dosgame_conf test_ute_audio 2>&1 | tail -3
cd ~/work/scummvm && export SCUMM_DOS_DIST=$HOME/work/scummvm/dos-mi2/dist/dos SCUMM_DOS_SRC=$HOME/work/scummvm/dos-mi2
python3 harness/dos/m5_accept.py x mi2 mi2kol mi2ko 2>&1 | tail -2
```
Expected: `OK`; `M5 x: PASS`. Record both. Run the second command only when no other DOSBox run is active (MI1 acceptance); until then record "pending" and do Task 1-4 first. A FAIL here is a baseline: record it, and from then on the gate is "no line worse".

- [ ] **Step 6: Linux unit-test baseline of the engine tree**

The Linux CxxTest build (`~/work/scummvm/builds/linux-dos-test-scumm`) compiles from `dos`, not from `dos-mi2`, and this plan changes no code the tests cover. Record: "CxxTest not rerun: no tests touch the changed files" and the `dos` baseline failure list from `.superpowers/sdd/2026-10-04-mi1-talkie-dos/baseline-test.log` in `~/work/scummvm/dos`.

---

### Task 1: Host Tremor checker in `build-deps.sh`  [DOSBox: no]

**Files:**
- Create: `backends/platform/dos/tremor-check.c`
- Modify: `backends/platform/dos/build-deps.sh`

**Interfaces:**
- Consumes: the Tremor tarball and libogg the script already fetches (`~/opt/src/flac-dos/{libogg-1.3.5.tar.xz,tremor.tar.gz}`).
- Produces: `$DOS_FLAC_HOST/bin/tremor-check` (default `~/opt/flac-host/bin/tremor-check`). Usage: `tremor-check FILE < "start size" lines`. Per bad clip it prints `FAIL <n> <start> <why>`; at the end `clips N bad B rate min-max mono M pcm P`; exit status 1 if any clip is bad.

- [ ] **Step 1: Create `tremor-check.c`**

Create `backends/platform/dos/tremor-check.c` with exactly:

```c
/* tremor-check: decode Ogg Vorbis clips of one file with Tremor, the decoder
 * SCUMM.EXE links, and report any that do not decode.
 *
 *   tremor-check FILE < clips.txt     clips.txt: "start size" per line
 *
 * Each clip is read whole and opened through ov_open_callbacks() on a memory
 * stream that can seek, as ScummVM's Vorbis decoder opens it. Prints one line
 * "FAIL <n> <start> <why>" per bad clip and, at the end,
 * "clips <n> bad <b> rate <min>-<max> mono <m> pcm <samples>"; exits 1 if any
 * clip is bad. Built by build-deps.sh host (Tremor and libogg for this machine).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tremor/ivorbiscodec.h>
#include <tremor/ivorbisfile.h>

typedef struct { const unsigned char *p; long size, pos; } Mem;

static size_t mread(void *dst, size_t sz, size_t n, void *h) {
	Mem *m = (Mem *)h;
	size_t want = sz * n, have = (size_t)(m->size - m->pos);
	if (want > have)
		want = have - have % (sz ? sz : 1);
	memcpy(dst, m->p + m->pos, want);
	m->pos += (long)want;
	return sz ? want / sz : 0;
}

static int mseek(void *h, ogg_int64_t off, int whence) {
	Mem *m = (Mem *)h;
	ogg_int64_t to = whence == SEEK_SET ? off : whence == SEEK_CUR ? m->pos + off : m->size + off;
	if (to < 0 || to > m->size)
		return -1;
	m->pos = (long)to;
	return 0;
}

static long mtell(void *h) { return ((Mem *)h)->pos; }

int main(int argc, char **argv) {
	FILE *f;
	long start, size, n = 0, bad = 0, mono = 0, rmin = 0, rmax = 0;
	long long pcm = 0;
	static const ov_callbacks cb = { mread, mseek, NULL, mtell };
	if (argc != 2) {
		fprintf(stderr, "usage: tremor-check FILE < clips.txt\n");
		return 2;
	}
	f = fopen(argv[1], "rb");
	if (!f) {
		perror(argv[1]);
		return 2;
	}
	while (scanf("%ld %ld", &start, &size) == 2) {
		unsigned char *buf = (unsigned char *)malloc(size);
		static char pcmbuf[4096];
		Mem m;
		OggVorbis_File vf;
		const char *why = NULL;
		long got, rate = 0;
		int bs;
		long long total = 0;
		n++;
		if (!buf || fseek(f, start, SEEK_SET) || (long)fread(buf, 1, size, f) != size) {
			printf("FAIL %ld %ld cannot read the clip\n", n - 1, start);
			bad++;
			free(buf);
			continue;
		}
		m.p = buf; m.size = size; m.pos = 0;
		if (ov_open_callbacks(&m, &vf, NULL, 0, cb) < 0) {
			why = "ov_open_callbacks failed";
		} else {
			vorbis_info *vi = ov_info(&vf, -1);
			rate = vi->rate;
			if (vi->channels == 1)
				mono++;
			while ((got = ov_read(&vf, pcmbuf, sizeof pcmbuf, &bs)) > 0)
				total += got / 2 / vi->channels;
			if (got < 0)
				why = "ov_read error";
			else if (total != ov_pcm_total(&vf, -1))
				why = "decoded length differs from ov_pcm_total";
			ov_clear(&vf);
		}
		if (why) {
			printf("FAIL %ld %ld %s\n", n - 1, start, why);
			bad++;
		} else {
			if (!rmin || rate < rmin) rmin = rate;
			if (rate > rmax) rmax = rate;
			pcm += total;
		}
		free(buf);
	}
	fclose(f);
	printf("clips %ld bad %ld rate %ld-%ld mono %ld pcm %lld\n", n, bad, rmin, rmax, mono, pcm);
	return bad ? 1 : 0;
}
```

- [ ] **Step 2: Apply the `build-deps.sh` change**

The change factors `fetch_tremor()` out of `build_codecs` (it checks the pinned git commit id), makes `build_host` also build libogg 1.3.5 into `$work/hinst`, compiles the Tremor objects with host gcc (`-O2 -DBYTE_ORDER=1234 -DLITTLE_ENDIAN=1234 -DBIG_ENDIAN=4321`) and links `tremor-check.c` with `libogg.a` into `$host/bin/tremor-check`. Write this diff to `.superpowers/sdd/2026-10-05-mi2-talkie-dos/build-deps.diff` and apply it from the worktree root:

```diff
--- a/backends/platform/dos/build-deps.sh
+++ b/backends/platform/dos/build-deps.sh
@@ -1,11 +1,13 @@
 #!/bin/bash
-# Build what SCUMM.EXE links for compressed audio, and the host tools the MI1
+# Build what SCUMM.EXE links for compressed audio, and the host tools the
 # Ultimate Talkie pack script (mkute.py) needs.
 # Usage: backends/platform/dos/build-deps.sh [codecs|host|all]   (default: all)
 #   codecs: libFLAC 1.4.3, libogg 1.3.5 and Tremor (integer Vorbis) for DJGPP,
 #           static, -O2 -march=i586 -mtune=pentium, no asm/SSE -> $DOS_CODECS
 #           (default ~/opt/codecs-dos). build-dos.sh scumm links them.
-#   host:   flac and metaflac 1.4.3 for this machine -> $DOS_FLAC_HOST
+#   host:   flac and metaflac 1.4.3 (mkute.py, MI1) and tremor-check (mkute.py
+#           --game mi2 --test-clips: libogg and Tremor for this machine, and
+#           tremor-check.c linked to them) for this machine -> $DOS_FLAC_HOST
 #           (default ~/opt/flac-host).
 # Sources come from $DOS_DEPS_SRC (default ~/opt/src/flac-dos); a missing
 # tarball is downloaded. libFLAC and libogg are checked by SHA-256; Tremor has
@@ -16,6 +18,7 @@
 src="${DOS_DEPS_SRC:-$HOME/opt/src/flac-dos}"
 codecs="${DOS_CODECS:-$HOME/opt/codecs-dos}"
 host="${DOS_FLAC_HOST:-$HOME/opt/flac-host}"
+here="$(cd "$(dirname "$0")" && pwd)"
 work="$(mktemp -d "${TMPDIR:-/tmp}/dos-deps.XXXXXX")"
 trap 'rc=$?; [ $rc = 0 ] && rm -rf "$work"; exit $rc' EXIT
 
@@ -44,6 +47,14 @@
 	mkdir -p "$work/$2"
 	tar -xf "$src/$1" -C "$work/$2" --strip-components=1
 }
+fetch_tremor() {
+	fetch "$TREMOR_TAR" "$TREMOR_URL"
+	got="$(gzip -dc "$src/$TREMOR_TAR" | git get-tar-commit-id)" || got=none
+	if [ "$got" != "$TREMOR_COMMIT" ]; then
+		echo "build-deps.sh: $src/$TREMOR_TAR is tremor $got, not $TREMOR_COMMIT" >&2
+		exit 1
+	fi
+}
 run_logged() {	# log command...
 	local log="$1"; shift
 	if ! "$@" >>"$log" 2>&1; then
@@ -63,19 +74,34 @@
 		--disable-xmms-plugin --disable-thorough-tests --disable-version-from-git &&
 	  run_logged "$work/hflac.log" make -j"$(nproc)" LDFLAGS=-all-static &&	# libtool: plain -static is not enough
 	  run_logged "$work/hflac.log" make install LDFLAGS=-all-static )
+	# libogg and Tremor for this machine (static, in the work dir), and tremor-check
+	fetch "$OGG_TAR" "$OGG_URL"; check_sha "$OGG_TAR" "$OGG_SHA"
+	fetch_tremor
+	unpack "$OGG_TAR" hogg
+	unpack "$TREMOR_TAR" htremor
+	( cd "$work/hogg" &&
+	  run_logged "$work/hogg.log" ./configure --prefix="$work/hinst" --disable-shared --enable-static &&
+	  run_logged "$work/hogg.log" make -j"$(nproc)" &&
+	  run_logged "$work/hogg.log" make install )
+	mkdir -p "$work/hinst/include/tremor"
+	cp "$work/htremor/ivorbiscodec.h" "$work/htremor/ivorbisfile.h" "$work/htremor/config_types.h" \
+		"$work/hinst/include/tremor/"
+	for f in $TREMOR_OBJS; do
+		run_logged "$work/htremor.log" gcc -O2 -DBYTE_ORDER=1234 -DLITTLE_ENDIAN=1234 -DBIG_ENDIAN=4321 \
+			-I"$work/hinst/include" -c "$work/htremor/$f.c" -o "$work/htremor/$f.o"
+	done
+	run_logged "$work/htremor.log" gcc -O2 -Wall -I"$work/hinst/include" "$here/tremor-check.c" \
+		$(for f in $TREMOR_OBJS; do echo "$work/htremor/$f.o"; done) "$work/hinst/lib/libogg.a" \
+		-o "$host/bin/tremor-check"
 	"$host/bin/metaflac" --version
+	echo "tremor-check built: $host/bin/tremor-check"
 }
 
 build_codecs() (
 	source ~/opt/dos-dev/env.sh
 	fetch "$FLAC_TAR" "$FLAC_URL"; check_sha "$FLAC_TAR" "$FLAC_SHA"
 	fetch "$OGG_TAR" "$OGG_URL"; check_sha "$OGG_TAR" "$OGG_SHA"
-	fetch "$TREMOR_TAR" "$TREMOR_URL"
-	got="$(gzip -dc "$src/$TREMOR_TAR" | git get-tar-commit-id)" || got=none
-	if [ "$got" != "$TREMOR_COMMIT" ]; then
-		echo "build-deps.sh: $src/$TREMOR_TAR is tremor $got, not $TREMOR_COMMIT" >&2
-		exit 1
-	fi
+	fetch_tremor
 	rm -rf "$codecs"
 	mkdir -p "$codecs/share/licenses"
 	# libogg (Tremor's bitstream layer)
```

```bash
cd ~/work/scummvm/dos-mi2 && P=.superpowers/sdd/2026-10-05-mi2-talkie-dos/build-deps.diff && git apply --check $P && git apply $P && git status --short
```
Expected: ` M backends/platform/dos/build-deps.sh`. If `--check` fails because `build-deps.sh` moved on `dos-port`, apply the hunks by hand: the intent is in the paragraph above.

- [ ] **Step 3: Build and run the host tool**

```bash
cd ~/work/scummvm/dos-mi2/backends/platform/dos && DOS_FLAC_HOST=/tmp/claude-1000/tremor-host-try ./build-deps.sh host 2>&1 | tail -4
```
Expected: ends with `tremor-check built: /tmp/claude-1000/tremor-host-try/bin/tremor-check` (about 10 s). This scratch location keeps the real `~/opt/flac-host` untouched until the commit; then run the real thing once: `./build-deps.sh host 2>&1 | tail -2` (rebuilds `~/opt/flac-host`, including `metaflac`; see the Ruling about timing).

- [ ] **Step 4: The all-clips census on the real file** (after Task 2 Step 3: it needs `mkute.check_sof` with the magic argument)

```bash
cd ~/work/scummvm/dos-mi2/backends/platform/dos && python3 - <<'PYEOF'
import os, subprocess, sys, time
sys.path.insert(0, ".")
import mkute
p = os.path.expanduser("~/work/scummvm/gamedata/mi2kor/monkey2.sog")
clips = mkute.check_sof(p, b"OggS", "Ogg Vorbis")
t = time.time()
inp = "".join("%d %d\n" % c for c in clips)
r = subprocess.run([os.path.expanduser("~/opt/flac-host/bin/tremor-check"), p], input=inp, text=True, capture_output=True)
print(len(clips), "clips;", r.stdout.strip().splitlines()[-1], "; rc", r.returncode, "; %.1f s" % (time.time() - t))
PYEOF
```
Expected: `6808 clips; clips 6808 bad 0 rate 22050-48236 mono 6807 pcm 593963273 ; rc 0 ; 10 s` (about). Record the line in the ledger.

- [ ] **Step 5: Commit**

```bash
cd ~/work/scummvm/dos-mi2 && git add backends/platform/dos/tremor-check.c backends/platform/dos/build-deps.sh
git commit -m "DOS: Build a host Tremor decode checker with build-deps.sh host" -m "$(printf 'Co-Authored-By: Claude Code <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4')"
```

---

### Task 2: The MI2 profile in `mkute.py`, and the real pack  [DOSBox: no]

**Files:**
- Modify: `backends/platform/dos/mkute.py`, `backends/platform/dos/test_mkute.py`

**Interfaces:**
- Consumes: Task 1's `tremor-check` (for `--test-clips`).
- Produces: `mkute.py --game mi2 [--korean DIR] --tremor-check PATH --test-clips SRC OUT` builds `OUT/GAMES/MI2UTE/{MONKEY2.000,MONKEY2.001,MONKEY2.SOG[,KOREAN.TRS,KOREAN00..05,07,08.FNT]}` plus `SCUMMVM.INI` (sections `mi2`, `mi2ko`, `mi2kol`) and the BATs; `runs/mi2ute/pack/` holds the real pack (Step 5). The MI1 profile output is unchanged.

The change adds a `GAMES` dict (`mi1`, `mi2`: pack folder, gameid, target, map, title, speech file, magic, kind, files, Korean files), keeps `PACK`/`UTE_FILES`/`KOREAN_FILES` as MI1 aliases, generalises `check_sof(path, magic=b"fLaC", kind="FLAC")` (the error says "not %s"), adds `tremor_clips()` (feeds "start size" lines to `tremor-check`; a non-zero exit raises `PackError("... Tremor cannot decode N clips, first: ...")`), makes `targets()`/`ini_text()` per-game, keeps the MI1-only checks (WAV `track1`, `metaflac`) under `game == "mi1"`, and adds `--game {mi1,mi2}` and `--tremor-check` to `main`. New tests in `Mi2Test` (english_pack, korean_pack with no KOREAN06.FNT, not_ogg, tremor_rejects_truncated_clip, missing_sog) use an `ffmpeg` Vorbis fixture and are skipped without `tremor-check` or `ffmpeg`.

- [ ] **Step 1: Apply the diff (mkute.py and its tests)**

Write this diff to `.superpowers/sdd/2026-10-05-mi2-talkie-dos/mkute.diff` and apply it from the worktree root:

```diff
--- a/backends/platform/dos/mkute.py
+++ b/backends/platform/dos/mkute.py
@@ -1,25 +1,32 @@
 #!/usr/bin/env python3
-"""Make the DOS pack of The Secret of Monkey Island, Ultimate Talkie Edition
-("Midi Music" build), for SCUMM.EXE, from your own copy of the game.
+"""Make the DOS pack of The Secret of Monkey Island (--game mi1, the default) or
+Monkey Island 2: LeChuck's Revenge (--game mi2), Ultimate Talkie Edition, for
+SCUMM.EXE, from your own copy of the game.
+
+  python3 mkute.py UTE_DIR OUT_DIR [--game mi1|mi2] [--korean KOREAN_DIR]
+                   [--metaflac PATH] [--flac PATH] [--tremor-check PATH]
+                   [--test-clips]
 
-  python3 mkute.py UTE_DIR OUT_DIR [--korean KOREAN_DIR] [--metaflac PATH]
-                   [--flac PATH] [--test-clips]
-
-UTE_DIR     the "Ultimate Talkie Version with Midi Music" folder: monkey.000,
+UTE_DIR     mi1: the "Ultimate Talkie Version with Midi Music" folder: monkey.000,
             monkey.001, monkey.sof, track1.flac, track25.flac ... track29.flac
+            mi2: the Monkey Island 2 folder: monkey2.000, monkey2.001, monkey2.sog
 KOREAN_DIR  optional: the ScummVM Kor. Project's translation of this edition
-            (korean.trs, korean00.fnt ... korean04.fnt); adds the Korean targets
-OUT_DIR     gets MI1UTE\\: GAMES\\MI1UTE\\ (the game), MI1UTE.INI and MI1.BAT
-            (with --korean also MI1KO.BAT, MI1KOL.BAT). Copy MI1UTE\\ to the
-            DOS machine (not to a CD: speech seeks need a hard disk) and run a
-            BAT from it.
+            (korean.trs, korean00.fnt ... korean04.fnt; mi2: korean00 ... 05,
+            07, 08); adds the Korean targets
+OUT_DIR     gets MI1UTE\\ or MI2UTE\\: GAMES\\<pack>\\ (the game), <pack>.INI and
+            MI1.BAT / MI2.BAT (with --korean also MI?KO.BAT, MI?KOL.BAT). Copy the
+            folder to the DOS machine (not to a CD: speech seeks need a hard disk)
+            and run a BAT from it.
 
 It copies the files under 8.3 names (track25.flac -> TRACK25.FLA), stores
 track1.flac, which is a WAV in this build, as TRACK1.WAV, adds a seek point
 every second to TRACK25-29.FLA with metaflac (without them a seek stalls the
-game for seconds on DOS), checks MONKEY.SOF's clip index (--test-clips also
-decodes every clip with flac), and writes the INI and the BATs. Your files are
-not changed. metaflac and flac come with FLAC (https://xiph.org/flac/).
+game for seconds on DOS; mi1 only), checks the speech file's clip index
+(MONKEY.SOF, FLAC clips; MONKEY2.SOG, Ogg Vorbis clips), and writes the INI and
+the BATs. --test-clips also decodes every clip: mi1 with flac, mi2 with
+tremor-check (the Tremor decoder SCUMM.EXE links; build-deps.sh host builds it).
+Your files are not changed. metaflac and flac come with FLAC
+(https://xiph.org/flac/).
 """
 import argparse
 import os
@@ -29,10 +36,24 @@
 import subprocess
 import sys
 
-PACK = "MI1UTE"
-UTE_FILES = ["monkey.000", "monkey.001", "monkey.sof", "track1.flac"] + ["track%d.flac" % n for n in range(25, 30)]
-KOREAN_FILES = ["korean.trs"] + ["korean%02d.fnt" % n for n in range(5)]
+# One profile per game. Everything the two packs differ in is here.
+GAMES = {
+    "mi1": dict(
+        pack="MI1UTE", gameid="monkey", target="mi1", map="data:M1KO.MAP",
+        title="The Secret of Monkey Island", speech="monkey.sof", magic=b"fLaC", kind="FLAC",
+        files=["monkey.000", "monkey.001", "monkey.sof", "track1.flac"] + ["track%d.flac" % n for n in range(25, 30)],
+        korean=["korean.trs"] + ["korean%02d.fnt" % n for n in range(5)]),
+    "mi2": dict(
+        pack="MI2UTE", gameid="monkey2", target="mi2", map="data:M2KO.MAP",
+        title="Monkey Island 2: LeChuck's Revenge", speech="monkey2.sog", magic=b"OggS", kind="Ogg Vorbis",
+        files=["monkey2.000", "monkey2.001", "monkey2.sog"],
+        korean=["korean.trs"] + ["korean%02d.fnt" % n for n in (0, 1, 2, 3, 4, 5, 7, 8)]),
+}
+PACK = GAMES["mi1"]["pack"]
+UTE_FILES = GAMES["mi1"]["files"]
+KOREAN_FILES = GAMES["mi1"]["korean"]
 HOME_DEFAULT = "C:\\SCUMMVM"
+TREMOR_CHECK = os.path.join(os.environ.get("DOS_FLAC_HOST", os.path.expanduser("~/opt/flac-host")), "bin", "tremor-check")
 _POINT = re.compile(r"^\s*point \d+: sample_number=(\d+)", re.M)
 _PLACEHOLDER = 0xFFFFFFFFFFFFFFFF
 
@@ -74,12 +95,13 @@
     return r.stdout.decode("utf-8", "replace")
 
 
-def check_sof(path):
-    """MONKEY.SOF's clip index as SCUMM reads it (sound.cpp setupSfxFile and
+def check_sof(path, magic=b"fLaC", kind="FLAC"):
+    """The speech file's clip index as SCUMM reads it (sound.cpp setupSfxFile and
     startTalkSound): a big-endian index size, 16 bytes a clip (original
-    offset, offset after the index, tag bytes, FLAC bytes), sorted by original
-    offset (the engine bsearches it); each clip's data, after its tags, is FLAC
-    and ends inside the file. Returns [(start, size)] of every clip's FLAC data."""
+    offset, offset after the index, tag bytes, data bytes), sorted by original
+    offset (the engine bsearches it); each clip's data, after its tags, starts
+    with `magic` (kind: FLAC for MONKEY.SOF, Ogg Vorbis for MONKEY2.SOG) and
+    ends inside the file. Returns [(start, size)] of every clip's data."""
     size = os.path.getsize(path)
     with open(path, "rb") as f:
         head = f.read(4)
@@ -100,8 +122,8 @@
             if start + csize > size:
                 raise PackError("%s: clip %d (original offset %d) ends past the file" % (path, i // 16, org))
             f.seek(start)
-            if f.read(4) != b"fLaC":
-                raise PackError("%s: clip %d (original offset %d) is not FLAC" % (path, i // 16, org))
+            if f.read(len(magic)) != magic:
+                raise PackError("%s: clip %d (original offset %d) is not %s" % (path, i // 16, org, kind))
             clips.append((start, csize))
     return clips
 
@@ -116,6 +138,22 @@
     log("MONKEY.SOF: all %d clips decode" % len(clips))
 
 
+def tremor_clips(tool, path, clips, log):
+    """Every clip of MONKEY2.SOG through tremor-check (build-deps.sh host), which
+    opens and decodes each with Tremor, the decoder SCUMM.EXE links."""
+    try:
+        r = subprocess.run([tool, path], input="".join("%d %d\n" % c for c in clips).encode("ascii"),
+                           capture_output=True, check=False)
+    except OSError as e:
+        raise PackError("cannot run %s: %s" % (tool, e))
+    out = r.stdout.decode("utf-8", "replace").strip().splitlines()
+    if r.returncode:
+        bad = [l for l in out if l.startswith("FAIL")]
+        raise PackError("%s: Tremor cannot decode %d clips, first: %s" % (
+            path, len(bad), bad[0] if bad else r.stderr.decode("utf-8", "replace").strip()))
+    log("MONKEY2.SOG: all %d clips decode with Tremor (%s)" % (len(clips), out[-1]))
+
+
 def seek_points(metaflac, path):
     out = run([metaflac, "--list", "--block-type=SEEKTABLE", path])
     return [int(s) for s in _POINT.findall(out) if int(s) != _PLACEHOLDER]
@@ -155,31 +193,33 @@
     return "\r\n".join(lines) + "\r\n"
 
 
-def targets(korean):
-    t = [("mi1", "MI1", "The Secret of Monkey Island (Ultimate Talkie, English)", "en", None, None)]
+def targets(korean, game="mi1"):
+    g = GAMES[game]
+    t = [(g["target"], g["target"].upper(), "%s (Ultimate Talkie, English)" % g["title"], "en", None, None)]
     if korean:
-        t += [("mi1ko", "MI1KO", "The Secret of Monkey Island (Ultimate Talkie, Korean, anti-aliased)",
-               "ko", "data:M1KO.MAP", None),
-              ("mi1kol", "MI1KOL", "The Secret of Monkey Island (Ultimate Talkie, Korean, 8-bit font)",
-               "ko", "data:M1KO.MAP", "clut8")]
+        t += [(g["target"] + "ko", g["target"].upper() + "KO",
+               "%s (Ultimate Talkie, Korean, anti-aliased)" % g["title"], "ko", g["map"], None),
+              (g["target"] + "kol", g["target"].upper() + "KOL",
+               "%s (Ultimate Talkie, Korean, 8-bit font)" % g["title"], "ko", g["map"], "clut8")]
     return t
 
 
-def ini_text(korean):
+def ini_text(korean, game="mi1"):
     """The pack's INI (PLAY copies it to the profile once), CRLF, no path=."""
-    bats = ", ".join("%s.BAT" % b for _, b, _, _, _, _ in targets(korean))
-    out = ["# Ready-to-run configuration: The Secret of Monkey Island, Ultimate Talkie",
-           "# Edition (Midi Music build), for SCUMM.EXE. Written by MKUTE.PY.",
+    g = GAMES[game]
+    bats = ", ".join("%s.BAT" % b for _, b, _, _, _, _ in targets(korean, game))
+    out = ["# Ready-to-run configuration: %s, Ultimate Talkie" % g["title"],
+           "# Edition%s, for SCUMM.EXE. Written by MKUTE.PY." % (" (Midi Music build)" if game == "mi1" else ""),
            "# Started by %s. PLAY copies this file to" % bats,
-           "# C:\\SCUMMVM\\GAMES\\MI1UTE\\SCUMMVM.INI once and gives the game directory",
+           "# C:\\SCUMMVM\\GAMES\\%s\\SCUMMVM.INI once and gives the game directory" % g["pack"],
            "# itself, so there is no path= line.",
            "",
            "[scummvm]",
-           "lastselectedgame=mi1",
+           "lastselectedgame=%s" % g["target"],
            "music_driver=adlib",
            ""]
-    for name, _, desc, lang, mp, rt in targets(korean):
-        out += ["[%s]" % name, "engineid=scumm", "gameid=monkey", "description=%s" % desc,
+    for name, _, desc, lang, mp, rt in targets(korean, game):
+        out += ["[%s]" % name, "engineid=scumm", "gameid=%s" % g["gameid"], "description=%s" % desc,
                 "language=%s" % lang, "platform=pc",
                 "# Voice and subtitles. Ctrl+T in the game cycles voice and text, text only,",
                 "# voice only.",
@@ -192,38 +232,46 @@
     return "\r\n".join(out)
 
 
-def make_pack(ute, out, korean=None, metaflac="metaflac", flac="flac", test_clips=False, log=print):
-    missing = [n for n in UTE_FILES if not find(ute, n)]
+def make_pack(ute, out, korean=None, metaflac="metaflac", flac="flac", test_clips=False, log=print,
+              game="mi1", tremor=None):
+    g = GAMES[game]
+    missing = [n for n in g["files"] if not find(ute, n)]
     if missing:
-        raise PackError('%s lacks %s (the UTE "Midi Music" folder has them)' % (ute, ", ".join(missing)))
+        raise PackError('%s lacks %s (the %s folder has them)' % (
+            ute, ", ".join(missing), "UTE \"Midi Music\"" if game == "mi1" else "Monkey Island 2 UTE"))
     if korean:
-        missing = [n for n in KOREAN_FILES if not find(korean, n)]
+        missing = [n for n in g["korean"] if not find(korean, n)]
         if missing:
             raise PackError("%s lacks %s" % (korean, ", ".join(missing)))
-    if not is_wav(find(ute, "track1.flac")):
-        raise PackError('%s is not a WAV file: is this the "Midi Music" build?' % find(ute, "track1.flac"))
-    run([metaflac, "--version"])
-    clips = check_sof(find(ute, "monkey.sof"))
-    log("MONKEY.SOF: %d clips, index in order, every clip FLAC" % len(clips))
+    if game == "mi1":
+        if not is_wav(find(ute, "track1.flac")):
+            raise PackError('%s is not a WAV file: is this the "Midi Music" build?' % find(ute, "track1.flac"))
+        run([metaflac, "--version"])
+    speech = find(ute, g["speech"])
+    clips = check_sof(speech, g["magic"], g["kind"])
+    log("%s: %d clips, index in order, every clip %s" % (g["speech"].upper(), len(clips), g["kind"]))
     if test_clips:
-        run([flac, "--version"])
-        decode_clips(flac, find(ute, "monkey.sof"), clips, log)
-    pack = os.path.join(out, PACK)
-    game = os.path.join(pack, "GAMES", PACK)
+        if game == "mi1":
+            run([flac, "--version"])
+            decode_clips(flac, speech, clips, log)
+        else:
+            tremor_clips(tremor or TREMOR_CHECK, speech, clips, log)
+    pack = os.path.join(out, g["pack"])
+    gamedir = os.path.join(pack, "GAMES", g["pack"])
     if os.path.exists(pack):
         shutil.rmtree(pack)
-    os.makedirs(game)
-    for n in UTE_FILES + (KOREAN_FILES if korean else []):
-        src = find(korean if n in KOREAN_FILES else ute, n)
-        dst = os.path.join(game, dos_name(n))
+    os.makedirs(gamedir)
+    for n in g["files"] + (g["korean"] if korean else []):
+        src = find(korean if n in g["korean"] else ute, n)
+        dst = os.path.join(gamedir, dos_name(n))
         shutil.copyfile(src, dst)
         if n.startswith("track") and n != "track1.flac":
             log("%s: %d seek points" % (dos_name(n), add_seekpoints(metaflac, dst)))
-    with open(os.path.join(pack, PACK + ".INI"), "wb") as f:
-        f.write(ini_text(bool(korean)).encode("ascii"))
-    for target, bat, _, _, _, _ in targets(bool(korean)):
+    with open(os.path.join(pack, g["pack"] + ".INI"), "wb") as f:
+        f.write(ini_text(bool(korean), game).encode("ascii"))
+    for target, bat, _, _, _, _ in targets(bool(korean), game):
         with open(os.path.join(pack, bat + ".BAT"), "wb") as f:
-            f.write(game_bat(PACK, target).encode("ascii"))
+            f.write(game_bat(g["pack"], target).encode("ascii"))
     log("pack: %s" % pack)
     return pack
 
@@ -236,9 +284,11 @@
     ap.add_argument("--metaflac", default="metaflac")
     ap.add_argument("--flac", default="flac")
     ap.add_argument("--test-clips", action="store_true")
+    ap.add_argument("--game", choices=sorted(GAMES), default="mi1")
+    ap.add_argument("--tremor-check", default=TREMOR_CHECK)
     a = ap.parse_args(argv)
     try:
-        make_pack(a.ute, a.out, a.korean, a.metaflac, a.flac, a.test_clips)
+        make_pack(a.ute, a.out, a.korean, a.metaflac, a.flac, a.test_clips, game=a.game, tremor=a.tremor_check)
     except PackError as e:
         print("mkute: %s" % e, file=sys.stderr)
         return 1
--- a/backends/platform/dos/test_mkute.py
+++ b/backends/platform/dos/test_mkute.py
@@ -138,5 +138,90 @@
         self.assertEqual(len(mkute.check_sof(os.path.join(self.ute, "monkey.sof"))), 2)
 
 
+TREMOR = os.environ.get("TREMOR_CHECK", mkute.TREMOR_CHECK)
+
+
+def write_ogg(path, secs=1, rate=44100):
+    """A mono Ogg Vorbis file from the host ffmpeg (libvorbis); returns False without one."""
+    try:
+        r = subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i",
+                            "sine=frequency=440:sample_rate=%d:duration=%d" % (rate, secs),
+                            "-ac", "1", "-c:a", "libvorbis", path], capture_output=True)
+    except OSError:
+        return False
+    return r.returncode == 0
+
+
+@unittest.skipUnless(os.path.exists(TREMOR), "tremor-check not built (build-deps.sh host)")
+class Mi2Test(unittest.TestCase):
+    def setUp(self):
+        self.tmp = tempfile.TemporaryDirectory()
+        self.ute = os.path.join(self.tmp.name, "mi2")
+        self.kor = os.path.join(self.tmp.name, "kor")
+        self.out = os.path.join(self.tmp.name, "out")
+        os.makedirs(self.ute)
+        os.makedirs(self.kor)
+        ogg = os.path.join(self.tmp.name, "a.ogg")
+        if not write_ogg(ogg):
+            self.skipTest("no host ffmpeg with libvorbis")
+        with open(ogg, "rb") as f:
+            self.ogg = f.read()
+        for n in ("monkey2.000", "monkey2.001"):
+            with open(os.path.join(self.ute, n), "wb") as f:
+                f.write(n.encode() * 10)
+        self.sog = os.path.join(self.ute, "monkey2.sog")
+        write_sof(self.sog, [(8, b"\0\1", self.ogg), (500, b"\0\2", self.ogg)])
+        with open(os.path.join(self.kor, "korean.trs"), "wb") as f:
+            f.write(b"SCVMTRS \0\0")
+        for n in (0, 1, 2, 3, 4, 5, 7, 8):
+            with open(os.path.join(self.kor, "korean%02d.fnt" % n), "wb") as f:
+                f.write(bytes([0, 0, 8, 8]))
+
+    def tearDown(self):
+        self.tmp.cleanup()
+
+    def make(self, korean=False, test_clips=True):
+        return mkute.make_pack(self.ute, self.out, korean=self.kor if korean else None, game="mi2",
+                               test_clips=test_clips, tremor=TREMOR, log=lambda *a: None)
+
+    def test_english_pack(self):
+        pack = self.make()
+        game = os.path.join(pack, "GAMES", "MI2UTE")
+        self.assertEqual(set(os.listdir(game)), {"MONKEY2.000", "MONKEY2.001", "MONKEY2.SOG"})
+        self.assertEqual(sorted(os.listdir(pack)), ["GAMES", "MI2.BAT", "MI2UTE.INI"])
+        ini = open(os.path.join(pack, "MI2UTE.INI"), "rb").read().decode("ascii")
+        self.assertIn("[mi2]\r\n", ini)
+        self.assertIn("gameid=monkey2\r\n", ini)
+        self.assertNotIn("[mi2ko]", ini)
+        self.assertEqual(open(os.path.join(pack, "MI2.BAT"), "rb").read().decode("ascii"),
+                         mkute.game_bat("MI2UTE", "mi2"))
+
+    def test_korean_pack(self):
+        pack = self.make(korean=True)
+        game = os.path.join(pack, "GAMES", "MI2UTE")
+        self.assertTrue({"KOREAN.TRS"} | {"KOREAN%02d.FNT" % n for n in (0, 1, 2, 3, 4, 5, 7, 8)} <= set(os.listdir(game)))
+        self.assertNotIn("KOREAN06.FNT", os.listdir(game))
+        self.assertEqual(sorted(f for f in os.listdir(pack) if f.endswith(".BAT")), ["MI2.BAT", "MI2KO.BAT", "MI2KOL.BAT"])
+        ini = open(os.path.join(pack, "MI2UTE.INI"), "rb").read().decode("ascii")
+        for s in ("[mi2]", "[mi2ko]", "[mi2kol]", "hires_text_map=data:M2KO.MAP", "render_target=clut8"):
+            self.assertIn(s, ini)
+
+    def test_not_ogg(self):
+        write_sof(self.sog, [(8, b"\0\1", b"fLaC" + b"\0" * 40)])
+        with self.assertRaisesRegex(mkute.PackError, "not Ogg Vorbis"):
+            self.make()
+
+    def test_tremor_rejects_truncated_clip(self):
+        write_sof(self.sog, [(8, b"\0\1", self.ogg), (500, b"\0\2", self.ogg[:len(self.ogg) // 2])])
+        self.make(test_clips=False)
+        with self.assertRaisesRegex(mkute.PackError, "Tremor cannot decode"):
+            self.make(test_clips=True)
+
+    def test_missing_sog(self):
+        os.remove(self.sog)
+        with self.assertRaisesRegex(mkute.PackError, "monkey2.sog"):
+            self.make()
+
+
 if __name__ == "__main__":
     unittest.main(verbosity=2)
```

```bash
cd ~/work/scummvm/dos-mi2 && P=.superpowers/sdd/2026-10-05-mi2-talkie-dos/mkute.diff && git apply --check $P && git apply $P && git status --short
```
Expected: ` M backends/platform/dos/mkute.py` and ` M backends/platform/dos/test_mkute.py`.

- [ ] **Step 2: Unit tests, and MI1 byte-identity**

```bash
cd ~/work/scummvm/dos-mi2/backends/platform/dos && python3 test_mkute.py 2>&1 | tail -4
```
Expected: `OK`. The MI2 tests are skipped, with the reason, if `~/opt/flac-host/bin/tremor-check` or `ffmpeg` is missing: do Task 1 Steps 1-3 first so they run (record the skip count; 0 skipped is the goal). The existing MI1 tests are unchanged and assert the MI1 pack content. In addition prove the generated text is identical to the old script's:

```bash
cd ~/work/scummvm/dos-mi2/backends/platform/dos && python3 - <<'PYEOF'
import importlib.util, subprocess
def load(name, src):
    spec = importlib.util.spec_from_loader(name, loader=None)
    m = importlib.util.module_from_spec(spec)
    exec(compile(src, name, "exec"), m.__dict__)
    return m
old = load("old", subprocess.check_output(["git", "show", "HEAD:backends/platform/dos/mkute.py"], text=True))
new = load("new", open("mkute.py").read())
for k in (False, True):
    assert old.ini_text(k) == new.ini_text(k, "mi1"), k
    assert old.targets(k) == new.targets(k, "mi1"), k
assert old.PACK == new.PACK and old.UTE_FILES == new.UTE_FILES and old.KOREAN_FILES == new.KOREAN_FILES
print("MI1 text identical")
PYEOF
```
Expected: `MI1 text identical`. (If `mkute.py` computes paths from `__file__` at import, write the old copy to a file instead: `git show HEAD:backends/platform/dos/mkute.py > /tmp/claude-1000/mkute_old.py`, and import it by path.)

- [ ] **Step 3: Commit**

```bash
cd ~/work/scummvm/dos-mi2 && git add backends/platform/dos/mkute.py backends/platform/dos/test_mkute.py
git commit -m "DOS: Teach mkute.py the MI2 Ultimate Talkie pack (monkey2.sog, Tremor-checked)" -m "$(printf 'Co-Authored-By: Claude Code <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4')"
```

- [ ] **Step 4: Task 1 Step 4 now (the 6808-clip census)**

Run it. Record the result line in the ledger.

- [ ] **Step 5: Build the real pack**

```bash
cd ~/work/scummvm/dos-mi2/backends/platform/dos && python3 mkute.py --game mi2 --korean ~/work/scummvm/gamedata/mi2kor --tremor-check ~/opt/flac-host/bin/tremor-check --test-clips ~/work/scummvm/gamedata/mi2kor ~/work/scummvm/runs/mi2ute/pack 2>&1 | tail -6
ls -l ~/work/scummvm/runs/mi2ute/pack/GAMES/MI2UTE
```
(If the script's argument order differs, `python3 mkute.py --help`; the MI1 Task 3 invocation in the MI1 plan is the model.) Expected: `MONKEY2.SOG: all 6808 clips decode`; the folder holds MONKEY2.000, MONKEY2.001, MONKEY2.SOG, KOREAN.TRS, KOREAN00..05,07,08.FNT (8.3 names); `SCUMMVM.INI` has `[mi2]`, `[mi2ko]`, `[mi2kol]`. Record the file sizes and the INI.

---

### Task 3: Harness targets and staging for the MI2 pack  [DOSBox: no]

**Files:**
- Modify (harness, master): `harness/dos/scummgame.py`, `harness/dos/m5_points.py` (additive only)

**Interfaces:**
- Consumes: Task 2's pack folder.
- Produces: `scummgame.TARGETS` keys `mi2u`, `mi2uko`, `mi2ukol` (folder `MI2UTE`); `m5_points.GAME_FILES["MI2UTE"]`; a Linux speech smoke run proving the engine opens `monkey2.sog` and prints no warning.

- [ ] **Step 1: Targets**

In `harness/dos/scummgame.py`, after the `"mi1ukol"` entry add:
```python
    # MI2 Ultimate Talkie as mkute.py --game mi2 packs it (monkey2.sog); the pack's INI calls these
    # mi2, mi2ko and mi2kol, names the floppy-data targets above already have in this table.
    "mi2u": ("gameid=monkey2\nlanguage=en\nspeech_mute=false\n", "MI2UTE"),
    "mi2uko": ("gameid=monkey2\nlanguage=ko\nspeech_mute=false\nhires_text_map=data:M2KO.MAP\n", "MI2UTE"),
    "mi2ukol": ("gameid=monkey2\nlanguage=ko\nspeech_mute=false\nhires_text_map=data:M2KO.MAP\nrender_target=clut8\n", "MI2UTE"),
```
(Read the existing `mi2ko`/`mi2kol` entries first and copy their `hires_text_map` and preset lines exactly; the lines above are the shape.) Update the comment above `TARGETS` with one sentence: "`mi2u`/`mi2uko`/`mi2ukol` are the MI2 Ultimate Talkie pack (Ogg Vorbis speech in MONKEY2.SOG)."

In `harness/dos/m5_points.py`, after `UTE_PACK` add `UTE2_PACK = os.path.join(ROOT, "runs/mi2ute/pack/MI2UTE")` only if the MI1 constant is a folder named like that (check: `UTE_PACK` ends in `MI1UTE`, and the `GAME_FILES` entry then joins `"GAMES", "MI1UTE"`; mirror that exactly), and to `GAME_FILES` add:
```python
    # UTE as mkute.py --game mi2 packs it: Ogg Vorbis speech, and the Kor. Project's translation.
    "MI2UTE": (os.path.join(UTE2_PACK, "GAMES", "MI2UTE"),
               ["MONKEY2.000", "MONKEY2.001", "MONKEY2.SOG", "KOREAN.TRS"]
               + ["KOREAN%02d.FNT" % n for n in (0, 1, 2, 3, 4, 5, 7, 8)]),
```
(`os.path.join(GD, src)` with an absolute `src` gives `src`.)

- [ ] **Step 2: Sanity checks**

```bash
cd ~/work/scummvm/harness/dos && python3 -c "
import scummgame, m5_points
for t in ('mi2u','mi2uko','mi2ukol'): print(t, scummgame.game_folder(t))
d = m5_points.stage_game('mi2u', '/tmp/claude-1000/stage-mi2u'); import os; print(sorted(os.listdir(d)))"
python3 -m unittest test_dosgame_conf test_ute_audio 2>&1 | tail -2
```
Expected: three lines with `MI2UTE`; 12 files; `OK`.

- [ ] **Step 3: Linux speech smoke run (the engine opens monkey2.sog)**

```bash
cd ~/work/scummvm && python3 - <<'EOF'
import os, re, sys
sys.path.insert(0, "harness/dos")
import scummgame, m5_points
out = os.path.expanduser("~/work/scummvm/runs/mi2ute/linux-smoke")
g = scummgame.launch_linux("mi2u", out)
g.wait("loops 1500", freeze=False)
g.close()
log = open(os.path.join(out, "run.log"), encoding="utf-8", errors="replace").read()
print(len(re.findall(r"(?i)SFX file not found|did not find sound|startTalkSound:", log)), "warning lines")
EOF
```
Expected: `0 warning lines`. `launch_linux` may need the pack folder staged through `stage_game`: it does that for the other targets; if it reports a missing folder, stage with `m5_points.stage_game`. The Linux binary reads `.sog` with libvorbis, so this proves the file/index wiring, not Tremor. For Linux speech *timing* use `strace -f -tt` as `ute_audio.linux_speech_times` does (Task 5 generalises it).

- [ ] **Step 4: Commit (harness repo)**

```bash
cd ~/work/scummvm && git add harness/dos/scummgame.py harness/dos/m5_points.py
git commit -m "dos: MI2 Ultimate Talkie targets mi2u, mi2uko, mi2ukol on the mkute pack" -m "$(printf 'Co-Authored-By: Claude Code <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4')"
```

---

### Task 4: `dos_vorbis_selftest` - measure Tremor on the target CPU  [DOSBox: no (build only)]

**Files:**
- Create: `backends/platform/dos/dos-vorbis-selftest.h`, `backends/platform/dos/dos-vorbis-selftest.cpp`
- Modify: `backends/platform/dos/dos.cpp`, `backends/platform/dos/module.mk`

**Interfaces:**
- Consumes: the Tremor-linked `SCUMM.EXE` (`USE_VORBIS`), `Audio::makeVorbisStream`, the DOS `haveTsc()`/`irqRdtsc()`.
- Produces: INI keys `dos_vorbis_selftest=<path>` (empty = off), `dos_vorbis_selftest_clips` (default 24), `dos_vorbis_selftest_from` (default -1 = spread evenly; N >= 0 = consecutive from index N). At `initBackend` it logs one line:
  `DOS: vorbis selftest clips=%u stereo=%u rate=%u-%u samples=%u decode_kcyc=%u open_us=%u/%u prime_us=%u/%u openprime_max_us=%u tsc_per_us=%u` (`a/b` = mean/max microseconds; `decode_kcyc` = kilocycles of the prime read plus the rest, not the open; `samples` counts all channels). `SCI.EXE` is unchanged (guarded by `USE_VORBIS`).

- [ ] **Step 1: Create the two files**

`backends/platform/dos/dos-vorbis-selftest.h`:
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

#ifndef BACKENDS_PLATFORM_DOS_VORBIS_SELFTEST_H
#define BACKENDS_PLATFORM_DOS_VORBIS_SELFTEST_H

#include "common/str.h"

namespace DOS {

/**
 * dos_vorbis_selftest=<speech file>: opens @p clips clips of an Ogg Vorbis
 * speech file (monkey2.sog) the way SCUMM does for a talk line (a File, a
 * SeekableSubReadStream, makeVorbisStream; spread evenly over the index, or
 * the consecutive clips from index @p from if that is not negative), decodes the first 4096 frames
 * (what the mixer's priming costs) and then the rest, with the TSC, and logs
 * one "DOS: vorbis selftest" line. Does nothing, and says so, without Vorbis.
 */
void vorbisSelftest(const Common::String &path, uint clips, int from);

} // End of namespace DOS

#endif
```

`backends/platform/dos/dos-vorbis-selftest.cpp`:
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

#include "common/scummsys.h"

#include "backends/platform/dos/dos-vorbis-selftest.h"

#include "common/endian.h"
#include "common/file.h"
#include "common/substream.h"
#include "common/system.h"
#include "common/array.h"
#include "common/archive.h"
#include "common/fs.h"

#ifdef USE_VORBIS
#include "audio/audiostream.h"
#include "audio/decoders/vorbis.h"
#include "backends/platform/dos/dos-irq.h"
#endif

namespace DOS {

#ifdef USE_VORBIS

namespace {

struct Clip {
	uint32 start, size;
};

bool readIndex(const Common::Path &name, Common::Array<Clip> &clips) {
	Common::File f;
	if (!f.open(name))
		return false;
	const uint32 n = f.readUint32BE();
	if (f.err() || n == 0 || n % 16 || n + 4 > (uint32)f.size())
		return false;
	for (uint32 i = 0; i < n; i += 16) {
		f.readUint32BE();	// original offset
		const uint32 newOff = f.readUint32BE();
		const uint32 tags = f.readUint32BE();
		Clip c;
		c.size = f.readUint32BE();
		c.start = newOff + n + 4 + tags;
		clips.push_back(c);
	}
	return !f.err();
}

} // End of anonymous namespace

void vorbisSelftest(const Common::String &path, uint wanted, int from) {
	// The file is found through SearchMan, as the engine finds MONKEY2.SOG.
	const Common::Path full(path, '/');
	const Common::Path name = full.getLastComponent();
	SearchMan.addDirectory("dosVorbisSelftest", Common::FSNode(full.getParent()), 0, 1);
	Common::Array<Clip> clips;
	if (!readIndex(name, clips) || clips.empty()) {
		SearchMan.remove("dosVorbisSelftest");
		g_system->logMessage(LogMessageType::kInfo,
			Common::String::format("DOS: vorbis selftest: cannot read the clip index of %s\n", path.c_str()).c_str());
		return;
	}
	if (!DOS::haveTsc()) {
		SearchMan.remove("dosVorbisSelftest");
		g_system->logMessage(LogMessageType::kInfo, "DOS: vorbis selftest: no TSC\n");
		return;
	}
	// TSC cycles per microsecond, from a 100 ms stretch of the millisecond clock
	const uint32 m0 = g_system->getMillis();
	while (g_system->getMillis() == m0)
		;
	const uint64 c0 = DOS::irqRdtsc();
	const uint32 m1 = g_system->getMillis();
	while (g_system->getMillis() - m1 < 100)
		;
	uint32 tscPerUs = (uint32)((DOS::irqRdtsc() - c0) / 100000);
	if (!tscPerUs)
		tscPerUs = 1;

	if (from >= (int)clips.size())
		from = clips.size() - 1;
	if (wanted > clips.size() - (from < 0 ? 0 : from))
		wanted = clips.size() - (from < 0 ? 0 : from);
	uint done = 0, stereo = 0, rateMin = 0, rateMax = 0;
	uint64 samples = 0, decode = 0, openSum = 0, primeSum = 0;
	uint32 openMax = 0, primeMax = 0, openPrimeMax = 0;
	static int16 buf[4096 * 2];
	for (uint i = 0; i < wanted; ++i) {
		const Clip &c = clips[from >= 0 ? from + i : (uint32)((uint64)i * clips.size() / wanted)];
		const uint64 t0 = DOS::irqRdtsc();
		Common::File *file = new Common::File;
		if (!file->open(name)) {
			delete file;
			continue;
		}
		Audio::SeekableAudioStream *s = Audio::makeVorbisStream(
			new Common::SeekableSubReadStream(file, c.start, c.start + c.size, DisposeAfterUse::YES),
			DisposeAfterUse::YES);
		const uint64 t1 = DOS::irqRdtsc();
		if (!s)
			continue;
		const int ch = s->isStereo() ? 2 : 1;
		const uint rate = s->getRate();
		int got = s->readBuffer(buf, 4096 * ch);
		const uint64 t2 = DOS::irqRdtsc();
		uint64 n = got;
		while (!s->endOfData() && (got = s->readBuffer(buf, 4096 * ch)) > 0)
			n += got;
		const uint64 t3 = DOS::irqRdtsc();
		delete s;
		++done;
		stereo += ch == 2;
		rateMin = !rateMin || rate < rateMin ? rate : rateMin;
		rateMax = rate > rateMax ? rate : rateMax;
		samples += n;
		decode += t3 - t1;
		openSum += t1 - t0;
		primeSum += t2 - t1;
		const uint32 openUs = (uint32)((t1 - t0) / tscPerUs), primeUs = (uint32)((t2 - t1) / tscPerUs);
		openMax = openUs > openMax ? openUs : openMax;
		primeMax = primeUs > primeMax ? primeUs : primeMax;
		openPrimeMax = openUs + primeUs > openPrimeMax ? openUs + primeUs : openPrimeMax;
	}
	SearchMan.remove("dosVorbisSelftest");
	// decode: kilocycles of the prime read + the rest (not the open); samples: all channels
	g_system->logMessage(LogMessageType::kInfo, Common::String::format(
		"DOS: vorbis selftest clips=%u stereo=%u rate=%u-%u samples=%u decode_kcyc=%u "
		"open_us=%u/%u prime_us=%u/%u openprime_max_us=%u tsc_per_us=%u\n",
		done, stereo, rateMin, rateMax, (uint)samples, (uint)(decode / 1000),
		done ? (uint)(openSum / tscPerUs / done) : 0, openMax,
		done ? (uint)(primeSum / tscPerUs / done) : 0, primeMax, openPrimeMax, tscPerUs).c_str());
}

#else

void vorbisSelftest(const Common::String &, uint, int) {
	g_system->logMessage(LogMessageType::kInfo, "DOS: vorbis selftest: this build has no Vorbis\n");
}

#endif

} // End of namespace DOS
```


- [ ] **Step 2: Wire it in**

```diff
--- a/backends/platform/dos/dos.cpp
+++ b/backends/platform/dos/dos.cpp
@@ -53,6 +53,9 @@
 #include "backends/platform/dos/dos-heap.h"
 #include "backends/platform/dos/dos-irq.h"
 #include "backends/platform/dos/dos-memory.h"
+#ifdef USE_VORBIS
+#include "backends/platform/dos/dos-vorbis-selftest.h"
+#endif
 #include "backends/platform/dos/dos-loading.h"
 #include "backends/platform/dos/dos-silence.h"
 #include "backends/platform/dos/blaster.h"
@@ -223,6 +226,14 @@
 	// physical memory at the start; 0 is off.
 	ConfMan.registerDefault("dos_pagefault_selftest", 0);
 	ConfMan.registerDefault("dos_pagefault_selftest_hz", 100);
+#ifdef USE_VORBIS
+	// dos_vorbis_selftest=<speech file, e.g. D:/MONKEY2.SOG>: opens, primes and decodes
+	// dos_vorbis_selftest_clips clips (from index dos_vorbis_selftest_from on, or spread over the file)
+	// with the TSC and logs the cost (dos-vorbis-selftest.cpp).
+	ConfMan.registerDefault("dos_vorbis_selftest", "");
+	ConfMan.registerDefault("dos_vorbis_selftest_clips", 24);
+	ConfMan.registerDefault("dos_vorbis_selftest_from", -1);
+#endif
 	// dos_loading_screen=false: no loading screen (DOS::Loading), the
 	// launcher's mode set at once as before.
 	ConfMan.registerDefault("dos_loading_screen", true);
@@ -305,6 +316,11 @@
 	if (ConfMan.getInt("dos_pagefault_selftest") != 0)
 		DOS::pagefaultSelftestStart(ConfMan.getInt("dos_pagefault_selftest"),
 			ConfMan.getInt("dos_pagefault_selftest_hz"));
+#ifdef USE_VORBIS
+	if (!ConfMan.get("dos_vorbis_selftest").empty())
+		DOS::vorbisSelftest(ConfMan.get("dos_vorbis_selftest"), ConfMan.getInt("dos_vorbis_selftest_clips"),
+			ConfMan.getInt("dos_vorbis_selftest_from"));
+#endif
 }
 
 static volatile uint32 g_selftestCalls = 0;
--- a/backends/platform/dos/module.mk
+++ b/backends/platform/dos/module.mk
@@ -12,6 +12,11 @@
 	../../mutex/dos/dos-mutex.o \
 	../../timer/dos/dos-timer.o
 
+# dos-vorbis-selftest.cpp: only where Vorbis is linked (SCUMM.EXE), so SCI.EXE is unchanged.
+ifdef USE_VORBIS
+MODULE_OBJS += dos-vorbis-selftest.o
+endif
+
 # dos-heap.cpp: the heap functions run with interrupts off.
 LDFLAGS += -Wl,--wrap=malloc,--wrap=free,--wrap=realloc,--wrap=calloc,--wrap=memalign
 # dos-loading.cpp: counts the bytes files give, for the loading screen.
```
Apply with `git apply` from the worktree root (`--check` first).

- [ ] **Step 3: Host syntax check**

```bash
cd ~/work/scummvm/dos-mi2 && export PATH=$HOME/.local/sysroot/usr/bin:$PATH PKG_CONFIG_PATH=$HOME/.local/sysroot/usr/lib/x86_64-linux-gnu/pkgconfig LD_LIBRARY_PATH=$HOME/.local/sysroot/usr/lib/x86_64-linux-gnu
g++ -std=c++11 -fsyntax-only -Wall -Wextra -DHAVE_CONFIG_H -I. -I../builds/linux-dos-scumm backends/platform/dos/dos-vorbis-selftest.cpp 2>&1 | grep -E "dos-vorbis-selftest\.(cpp|h):[0-9]+:[0-9]+: (error|warning)"; echo done
```
Expected: only `done`. (The `DOS::haveTsc`/`irqRdtsc` declarations come from `dos-irq.h`: if the host check cannot see them, add `-I backends/platform/dos` and keep the include; the real check is the DOS build in Step 4. Both the `USE_VORBIS` code and the stub branch compile.)

- [ ] **Step 4: DOS build of both editions**

```bash
cd ~/work/scummvm/dos-mi2 && source ~/opt/dos-dev/env.sh
nice -n 19 taskset -c 0-3 backends/platform/dos/build-dos.sh scumm 2>&1 | tail -3
nice -n 19 taskset -c 0-3 backends/platform/dos/build-dos.sh sci 2>&1 | tail -3
D=.superpowers/sdd/2026-10-05-mi2-talkie-dos
for e in SCUMM SCI; do cp dist/dos/$e.EXE $D/$e.n && i586-pc-msdosdjgpp-strip $D/$e.n && echo $e $(stat -c %s $D/$e.n) $(cmp -s $D/$e.n $D/$e.s && echo same || echo differs); done
```
Expected: both builds succeed. `SCUMM` differs (the selftest adds a few KB); `SCI` is `same` as the Task 0 stripped baseline. If SCI differs, Task 6's SCI gate becomes mandatory.

- [ ] **Step 5: Commit (do not run DOSBox yet)**

```bash
cd ~/work/scummvm/dos-mi2 && git add backends/platform/dos/dos-vorbis-selftest.h backends/platform/dos/dos-vorbis-selftest.cpp backends/platform/dos/dos.cpp backends/platform/dos/module.mk
git commit -m "DOS: Add dos_vorbis_selftest, a TSC measurement of Tremor open/prime/decode cost" -m "$(printf 'Co-Authored-By: Claude Code <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4')"
```

- [ ] **Step 6: Triple review of Tasks 1-4 (code side)**

Package `git diff d966f6a5fbc..HEAD` (in `dos-mi2`) as `.superpowers/sdd/2026-10-05-mi2-talkie-dos/review-code-1.diff` with a short brief. Sonnet Claude subagent + agy + hermes glm, in parallel (see Executor notes). Ask specifically: the index arithmetic in `readIndex`/`check_sof`, the `SearchMan` add/remove paths in `vorbisSelftest` (every early return removes the directory), MI1 pack unchanged, `SCI.EXE` unaffected, `build-deps.sh` re-runnable. Fix what is confirmed; log verdicts in the ledger.

---

### Task 5: Generalise `ute_audio.py` to game profiles, add `census` and `tremor`  [DOSBox: no]

**Waits for:** the MI1 Task 16 acceptance agent to finish (this task edits the file it runs).

**Files:**
- Modify (harness, master): `harness/dos/ute_audio.py`, `harness/dos/test_ute_audio.py`

**Interfaces:**
- Consumes: Tasks 3-4.
- Produces: `PROFILES`/`use_game(game)`; `--game {mi1,mi2}` on every subcommand (inferred from the `--target` prefix when absent: `mi2*` -> mi2, else mi1); `launch(..., gamedir=None)` defaulting to the active profile's pack folder; `parse_vorbis_selftest(text)`, `tremor_cpu(v, rate=48000)`, `tremor_verdict(v)`; subcommands `census` and `tremor`; `linux_speech_times(text, name)`. Without `--game`/an `mi2` target, behaviour is exactly MI1's (all existing tests pass unchanged).

- [ ] **Step 1: Failing tests first**

Append to `harness/dos/test_ute_audio.py` (before the `if __name__` block):

```python
VS = ("DOS: vorbis selftest clips=24 stereo=0 rate=22050-48236 samples=2400000 decode_kcyc=3000000 "
      "open_us=8000/20000 prime_us=30000/60000 openprime_max_us=70000 tsc_per_us=40")


class GameProfileTest(unittest.TestCase):
    def tearDown(self):
        ute_audio.use_game("mi1")

    def test_mi1_is_default(self):
        self.assertEqual(os.path.basename(ute_audio.GAMEDIR), "MI1UTE")
        self.assertEqual(ute_audio.SPEECH, "MONKEY.SOF")

    def test_use_game_mi2(self):
        ute_audio.use_game("mi2")
        self.assertEqual(os.path.basename(ute_audio.GAMEDIR), "MI2UTE")
        self.assertEqual(ute_audio.SPEECH, "MONKEY2.SOG")
        self.assertEqual(ute_audio.GAMEID, "monkey2")
        self.assertTrue(ute_audio.RUNS.endswith("runs/mi2ute"))

    def test_game_from_target(self):
        self.assertEqual(ute_audio.game_of("mi2ukol"), "mi2")
        self.assertEqual(ute_audio.game_of("mi1uko"), "mi1")
        self.assertEqual(ute_audio.game_of("mi1"), "mi1")

    def test_speech_times_by_name(self):
        text = ("12:00:01.500000 openat(AT_FDCWD, \"/x/MONKEY2.SOG\", O_RDONLY) = 5\n"
                "12:00:03.250000 openat(AT_FDCWD, \"/x/monkey2.sog\", O_RDONLY) = 6\n"
                "12:00:04.000000 openat(AT_FDCWD, \"/x/monkey.sof\", O_RDONLY) = 7\n")
        self.assertEqual(ute_audio.linux_speech_times(text, "monkey2.sog"), [43201.5, 43203.25])


class TremorTest(unittest.TestCase):
    def test_parse(self):
        v = ute_audio.parse_vorbis_selftest("junk\n" + VS + "\nmore")
        self.assertEqual(v["clips"], 24)
        self.assertEqual((v["rate_min"], v["rate_max"]), (22050, 48236))
        self.assertEqual(v["openprime_max_us"], 70000)
        self.assertEqual(v["tsc_per_us"], 40)
        with self.assertRaises(RuntimeError):
            ute_audio.parse_vorbis_selftest("DOS: vorbis selftest: no TSC")

    def test_cpu_and_verdict(self):
        v = ute_audio.parse_vorbis_selftest(VS)
        # 3e9 cycles / 2.4e6 samples = 1250 cycles per sample; x 48000 = 6.0e7 cycles/s on a 4.0e7 machine
        self.assertAlmostEqual(ute_audio.tremor_cpu(v), 150.0)
        ok, why = ute_audio.tremor_verdict(v)
        self.assertFalse(ok)
        v["decode_kcyc"] = 400000   # 166.7 cycles per sample -> 20 %
        ok, why = ute_audio.tremor_verdict(v)
        self.assertTrue(ok, why)
        v["openprime_max_us"] = 150000
        self.assertFalse(ute_audio.tremor_verdict(v)[0])
```
(`tremor_cpu` = decode cycles per sample x rate / (tsc_per_us x 1e6) x 100, i.e. the share of the emulated CPU one 48 kHz mono stream needs.) Run `cd harness/dos && python3 -m unittest test_ute_audio 2>&1 | tail -3`: expected FAIL (no `use_game`).

- [ ] **Step 2: Profiles**

In `ute_audio.py` replace the module constants block (the `RUNS`/`GAMEDIR`/`TOWN_SAVE`/`LIPSYNC_RAW`/`LOOKOUT_START`/`WINDOW`/`MIN_*`/`CTRLT_*` lines) with a profile table and a setter, keeping the names as module globals so the rest of the file is unchanged:

```python
PROFILES = {
    "mi1": dict(runs="mi1ute", folder="MI1UTE", gameid="monkey", speech="MONKEY.SOF", target="mi1",
                korean="mi1ukol", korean_u="mi1uko", town_save="saves/mi1ute-town.s01",
                lookout_start=900, window=1200, min_speech=20,
                ctrlt=(("voice+text", 60), ("text", 60), ("voice", 100)), ctrlt_step=3),
    # PROVISIONAL: copied from mi1; `ute_audio.py census --game mi2` (plan Task 7) fixes them.
    "mi2": dict(runs="mi2ute", folder="MI2UTE", gameid="monkey2", speech="MONKEY2.SOG", target="mi2u",
                korean="mi2ukol", korean_u="mi2uko", town_save=None,
                lookout_start=900, window=1200, min_speech=20,
                ctrlt=(("voice+text", 60), ("text", 60), ("voice", 100)), ctrlt_step=3),
}


def game_of(target):
    return "mi2" if target.startswith("mi2") else "mi1"


def use_game(game):
    p = PROFILES[game]
    g = globals()
    g["GAME"] = game
    g["RUNS"] = os.path.join(ROOT, "runs", p["runs"])
    g["GAMEDIR"] = os.path.join(ROOT, "runs", p["runs"], "pack", "GAMES", p["folder"])
    g["GAMEID"], g["SPEECH"] = p["gameid"], p["speech"]
    g["TOWN_SAVE"] = os.path.join(ROOT, p["town_save"]) if p["town_save"] else None
    g["LIPSYNC_RAW"] = os.path.join(g["RUNS"], "lipsync-sb16-40000", "host.raw")
    g["LOOKOUT_START"], g["WINDOW"], g["MIN_SPEECH"] = p["lookout_start"], p["window"], p["min_speech"]
    g["CTRLT_STAGES"], g["CTRLT_STEP"] = p["ctrlt"], p["ctrlt_step"]


use_game("mi1")
```
Check first that `m5_points.UTE_PACK` (used by the old `GAMEDIR`) equals `runs/mi1ute/pack/MI1UTE`'s parent layout (`UTE_PACK/GAMES/MI1UTE`): `GAMEDIR` for mi1 must stay the identical path; keep `os.path.join(m5_points.UTE_PACK, "GAMES", "MI1UTE")` for mi1 if the layouts differ. Keep `MIN_CLOCK`, `MIN_LOOPS`, `MAX_PIECE_MS` as they are (spec thresholds, both games).

- [ ] **Step 3: Replace the MI1 hardwiring**

Make each change; every one is a literal replacement of an MI1 name by the active profile's:
- `launch(name, target, cycles, gamedir=GAMEDIR, ...)` -> `gamedir=None` and `gamedir = gamedir or GAMEDIR` inside (the default was bound at definition time).
- `cmd_lipsync`: `scummgame.dos_ini("mi1")` and `launch(name, "mi1", ...)` -> `PROFILES[GAME]["target"]`.
- `cmd_modes`: the hardwired `loops 1050` wait -> `LOOKOUT_START + 150` (MI1: 1050, unchanged).
- `cmd_fallback`: `"MONKEY.SOF"` -> `SPEECH`, target `"mi1"` -> the profile target; the symlink loop compares `f.upper() != SPEECH`.
- `cmd_spacing`: `gameid="monkey"` -> `GAMEID`.
- `linux_speech_times(strace_text)` -> `linux_speech_times(strace_text, name=None)`: the regex is built from `re.escape((name or SPEECH).lower())` (`monkey\.sof` today); callers pass the profile's name.
- `cmd_gate`: `choices=["lookout","town"]` -> `["lookout","intro","town","save"]`; `intro` = `lookout` (both: boot, wait `LOOKOUT_START`, sample `WINDOW`), `save` = `town`; the `min_speech` argument passed to `gate()` is `MIN_SPEECH`; the town/save save path comes from `TOWN_SAVE` and `preflight` stops with "this game has no save for that scenario" when it is `None`.
- `preflight`: `GAMEDIR` missing message names the active game; add: the speech file `SPEECH` must exist in `GAMEDIR` except for `fallback`.
- `main`: a parent parser with `--game {mi1,mi2}` shared by every subparser; after `parse_args`, `use_game(a.game or game_of(getattr(a, "target", "") or ""))`. The `gate`/`modes` default `--target` stays `mi1` when the game is mi1 and becomes `mi2u` when `--game mi2` is given without a target (set the default to `None` and resolve it from the profile); `korean` default `--target` likewise resolves to the profile's `korean`.
Update the module docstring's usage lines to show `--game mi2`.

- [ ] **Step 4: `parse_vorbis_selftest`, `tremor_cpu`, `tremor_verdict`, and the `tremor` and `census` commands**

```python
VORBIS_RE = re.compile(
    r"vorbis selftest clips=(\d+) stereo=(\d+) rate=(\d+)-(\d+) samples=(\d+) decode_kcyc=(\d+) "
    r"open_us=(\d+)/(\d+) prime_us=(\d+)/(\d+) openprime_max_us=(\d+) tsc_per_us=(\d+)")
VORBIS_KEYS = ("clips", "stereo", "rate_min", "rate_max", "samples", "decode_kcyc", "open_us", "open_max_us",
               "prime_us", "prime_max_us", "openprime_max_us", "tsc_per_us")
MAX_TREMOR_CPU = 50.0        # ruling: percent of the P75 machine one 48 kHz mono stream may take
MAX_OPENPRIME_MS = 100.0     # ruling: one line's ov_open + first 4096 frames, inside the 150 ms budget


def parse_vorbis_selftest(text):
    m = VORBIS_RE.search(text)
    if not m:
        raise RuntimeError("no 'vorbis selftest clips=...' line (the build has no Vorbis, no TSC, or the file did not open)")
    return dict(zip(VORBIS_KEYS, map(int, m.groups())))


def tremor_cpu(v, rate=48000):
    per_sample = v["decode_kcyc"] * 1000.0 / v["samples"]
    return per_sample * rate / (v["tsc_per_us"] * 1e6) * 100.0


def tremor_verdict(v):
    cpu = tremor_cpu(v)
    fails = []
    if cpu > MAX_TREMOR_CPU:
        fails.append("Tremor needs %.1f%% of the machine at 48 kHz (limit %.0f%%)" % (cpu, MAX_TREMOR_CPU))
    if v["openprime_max_us"] > MAX_OPENPRIME_MS * 1000:
        fails.append("open+prime of one line takes up to %.0f ms (limit %.0f ms)" % (v["openprime_max_us"] / 1000.0, MAX_OPENPRIME_MS))
    return (not fails), "; ".join(fails)
```
`cmd_tremor(a)`: `launch("tremor", target, a.cycles, ini=with_keys(dos_ini, dos_vorbis_selftest="D:/" + SPEECH, dos_vorbis_selftest_clips=str(a.clips), dos_vorbis_selftest_from=str(a.from_clip)))` for the active game's target; `D:` is where `dosgame` mounts the game (`path=D:\` in the INI). Boot, `g.wait("loops 20", freeze=False)`, read the run log (the existing `read()` helper), `parse_vorbis_selftest`, print the parsed numbers, `tremor_cpu` and the verdict, and finish with the line `TREMOR <target> <cycles>: PASS|FAIL (<why>)`. Options: `--cycles` (40000), `--clips` (24), `--from-clip` (-1). `--game mi2` is the only sensible use, but nothing forbids `--game mi1` (it prints "this build has no Vorbis" -> RuntimeError message; the MI1 speech is FLAC).
`cmd_census(a)`: boot the active game's `--target` with audio stats on (as `cmd_gate` does), `g.wait("loops N", freeze=False)` in steps of `--step` (default 50) up to `--loops` (default 3000), reading the `audio` stats each step with `reading(g)`; print one row per step `loops speech_total delta` and at the end `FIRST SPEECH loop=<n>`, `SPEECH IN FIRST <loops>: <k>`, `LINES PER 1000 LOOPS: <x>`, and a suggested `lookout_start` (the first speech loop minus 200, rounded down to 100, minimum 300) and `window` (the smallest of 1200, 1800, 2400, 3000 that holds `MIN_SPEECH` speech lines after `lookout_start`). It changes nothing; the plan's Task 7 copies the suggestion into `PROFILES`.
Register both in `main`'s subparsers (`--target`, `--cycles`, plus the options above).

- [ ] **Step 5: Tests green, no regression, commit**

```bash
cd ~/work/scummvm/harness/dos && python3 -m unittest test_dosgame_conf test_ute_audio 2>&1 | tail -3
python3 ute_audio.py gate --help | head -3; python3 ute_audio.py gate --game mi2 --help | head -3
```
Expected: `OK` with the new tests included; both help calls work. A mi1 invocation without `--game` behaves as before (the existing tests are the proof; nothing here runs DOSBox). Then:
```bash
cd ~/work/scummvm && git add harness/dos/ute_audio.py harness/dos/test_ute_audio.py
git commit -m "dos: ute_audio.py takes a game profile (mi1, mi2) and gains census and tremor commands" -m "$(printf 'Co-Authored-By: Claude Code <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4')"
```

---

### Task 6: Final DOS builds, sizes, and the SCI.EXE check  [DOSBox: no (yes only if SCI.EXE differs)]

**Files:** none (ledger).

**Interfaces:**
- Consumes: Tasks 1-5.
- Produces: `dist/dos/` in `dos-mi2` with the build that Tasks 7-12 run; the sizes table; the SCI verdict.

- [ ] **Step 1: Builds and sizes**

```bash
cd ~/work/scummvm/dos-mi2 && source ~/opt/dos-dev/env.sh
nice -n 19 taskset -c 0-3 backends/platform/dos/build-dos.sh scumm 2>&1 | tail -1
nice -n 19 taskset -c 0-3 backends/platform/dos/build-dos.sh sci 2>&1 | tail -1
D=.superpowers/sdd/2026-10-05-mi2-talkie-dos
for e in SCUMM SCI; do cp dist/dos/$e.EXE $D/$e.f && i586-pc-msdosdjgpp-strip $D/$e.f && echo $e $(stat -c %s $D/$e.f) base $(stat -c %s $D/$e.s); done
cmp $D/SCI.f $D/SCI.s && echo SCI-IDENTICAL
python3 backends/platform/dos/irqcheck.py dist/dos/SCUMM.EXE 2>&1 | tail -1
```
Expected: `SCUMM` larger than base by the selftest only (a few KB; Tremor is already linked); `SCI-IDENTICAL`; `irqcheck.py` passes. Record in the ledger.

- [ ] **Step 2: If SCI.EXE differs (it should not)**

Run the Task 0 Step 5 SCI lines (`m0..m3_accept.py x`, `loading_accept.py x`) and compare to the baseline; any worse line is a stop-and-report (Ruling: the selftest must not leak into SCI).

- [ ] **Step 3: Linux unit tests unchanged**

`cd ~/work/scummvm/harness/dos && python3 -m unittest test_dosgame_conf test_ute_audio 2>&1 | tail -1` -> `OK`; `cd ~/work/scummvm/dos-mi2/backends/platform/dos && python3 test_mkute.py 2>&1 | tail -1` -> `OK`.

---

### Task 7: Smoke run, Tremor CPU and open cost, and the MI2 gate constants  [DOSBox: yes]

**Files:** `harness/dos/ute_audio.py` (the `PROFILES["mi2"]` constants only); ledger.

**Interfaces:**
- Consumes: Tasks 2-6; an idle DOSBox (MI1 acceptance finished).
- Produces: the measured Tremor CPU and per-line open+prime cost; MI2's `lookout_start`, `window`, `min_speech` (and `ctrlt` stages if Task 10 needs them) committed in `PROFILES["mi2"]`.

- [ ] **Step 1: Environment and rebase**

```bash
export SCUMM_DOS_DIST=$HOME/work/scummvm/dos-mi2/dist/dos SCUMM_DOS_SRC=$HOME/work/scummvm/dos-mi2
cd ~/work/scummvm/dos-mi2 && git log --oneline -1 dos-port
```
If `dos-port` moved beyond `d966f6a5fbc`, `git rebase dos-port` in `dos-mi2`, rerun Task 6 Step 1, record.

- [ ] **Step 2: Smoke run**

```bash
cd ~/work/scummvm && python3 harness/dos/ute_audio.py census --game mi2 --target mi2u --cycles 40000 --loops 600 --step 100 2>&1 | tail -12
```
Expected: the game boots under DOSBox (pack mounted as `D:`), loops advance, and either speech counts rise or none yet (the intro may be silent that early). No warnings. A crash or hang here is the first blocker: capture the run log in `runs/mi2ute/` and stop with it.

- [ ] **Step 3: Tremor CPU and open cost on the real file**

```bash
python3 harness/dos/ute_audio.py tremor --game mi2 --cycles 40000 --clips 24 2>&1 | tail -8
python3 harness/dos/ute_audio.py tremor --game mi2 --cycles 40000 --clips 12 --from-clip 100 2>&1 | tail -3
```
Expected: the parsed numbers and `TREMOR mi2u 40000: PASS`. Record every number in the ledger (cycles per sample, CPU %, open/prime mean and max, rate range). Verdict FAIL -> Task 9 before Task 8 (the gate would fail for a known reason). Also run once with `--cycles 30000` to bracket the P75 setting; informational.

- [ ] **Step 4: Census and constants**

```bash
python3 harness/dos/ute_audio.py census --game mi2 --target mi2u --cycles 40000 --loops 3000 --step 50 2>&1 | tail -30
python3 harness/dos/ute_audio.py census --game mi2 --target mi2u --cycles 40000 --loops 3000 --step 50 2>&1 | tail -5
```
The second run is a repeat: the suggested `lookout_start`/`window` must agree (determinism of the intro). Copy the suggestion into `PROFILES["mi2"]` (`lookout_start`, `window`, `min_speech`); if the intro never reaches 20 speech lines in 3000 loops, lower `min_speech` to the count the intro reaches in 2400 loops and say so in the ledger (a Ruling), and add the optional `save` scenario: stage the M5 route-B MI2 save (`runs/dos-m5/census/saves-made/mi2.s03`) as `TOWN_SAVE` in the `mi2` profile (`town_save`) and rerun the census from it.

- [ ] **Step 5: Commit the constants (harness repo)**

```bash
cd ~/work/scummvm/harness/dos && python3 -m unittest test_ute_audio 2>&1 | tail -1
cd ~/work/scummvm && git add harness/dos/ute_audio.py
git commit -m "dos: MI2 gate constants from the intro speech census" -m "$(printf 'Co-Authored-By: Claude Code <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4')"
```

---

### Task 8: The S1-style audio gate for MI2 at Pentium 75  [DOSBox: yes]

**Files:** ledger.

**Interfaces:**
- Consumes: Task 7's constants and an idle DOSBox.
- Produces: gate result lines for `mi2u` and `mi2ukol` (gating) and `mi2uko` (informational), in the ledger.

- [ ] **Step 1: Gate runs**

```bash
cd ~/work/scummvm
python3 harness/dos/ute_audio.py gate --game mi2 --target mi2u --scenario intro --cycles 40000 | tail -1
python3 harness/dos/ute_audio.py gate --game mi2 --target mi2ukol --scenario intro --cycles 40000 | tail -1
python3 harness/dos/ute_audio.py gate --game mi2 --target mi2uko --scenario intro --cycles 40000 | tail -1
```
If Task 7 added a save scenario, add `--scenario save` for `mi2u`. Expected: PASS on the first two: 0 underruns, clock >= 99.5 %, >= 9.5 loops/s, swap 0, at least `min_speech` lines, no `SFX file not found`/`did not find sound`/`startTalkSound:` line. The `mi2uko` line (U preset, hi-res true colour) is recorded, not gating: if it fails only on loops/s, note it as a follow-up as for `mi1uko`.

- [ ] **Step 2: Decide**

All gating lines PASS -> go to Task 10 (skip Task 9). Any FAIL -> read which metric: underruns/clock/piecemax point at Tremor CPU or the prime on the main thread -> Task 9; loops/s alone -> compare with the Task 7 Tremor number and the MI1 baseline (`mi1` gate lookout loops/s); no other cause may be guessed: capture `runs/mi2ute/<run>/` and stop with it. Record the reading in the ledger.

---

### Task 9 (conditional: only if Task 7 Step 3 or Task 8 failed on Tremor cost): the low-accuracy Tremor build  [DOSBox: yes]

**Files:**
- Modify: `backends/platform/dos/build-deps.sh` (the DOS Tremor compile flags only)

**Interfaces:**
- Consumes: the failing numbers of Task 7/8.
- Produces: Tremor built with `-D_LOW_ACCURACY_`, re-measured; or a stop-and-report if still failing.

- [ ] **Step 1: The flag**

In `build-deps.sh`, in the DJGPP Tremor compile (the `TREMOR_OBJS` loop in `build_codecs`, flags `-O2 -march=i586 -mtune=pentium`), add `-D_LOW_ACCURACY_` (Tremor's 32-bit-multiply path, for CPUs without fast 64-bit multiplies; it lowers precision slightly, not audibly in speech). Rebuild the codecs and `SCUMM.EXE`: `rm -rf ~/opt/flac-dos` is NOT needed if the script rebuilds `libvorbisidec.a` when its flags change; otherwise delete only the Tremor object directory the script names. Check `git diff` shows only the flag.

- [ ] **Step 2: Remeasure**

Rerun Task 7 Step 3 and Task 8 Step 1. Expected: `TREMOR ... PASS` and the gate lines PASS, with the Tremor CPU lower than before (record both numbers). If still failing: stop and report the numbers; any further step (a lower-rate resample, smaller `kPrimeSamples`, a decode thread) is an engine/mixer design change outside this plan.

- [ ] **Step 3: Commit**

```bash
cd ~/work/scummvm/dos-mi2 && git add backends/platform/dos/build-deps.sh
git commit -m "DOS: Build Tremor with _LOW_ACCURACY_ for Pentium-class speech decoding" -m "$(printf 'Co-Authored-By: Claude Code <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4')"
```
Then rerun Task 6 Step 1 (sizes; SCI unchanged because SCI links no Tremor) and Task 8.

---

### Task 10: Voice/subtitle modes, Ctrl+T, Korean, and the speech-file-absent fallback  [DOSBox: yes]

**Files:** `harness/dos/ute_audio.py` only if the MI2 Ctrl+T stages need adjusting; ledger.

**Interfaces:**
- Consumes: Tasks 7-8.
- Produces: `MODES mi2u: PASS`, `KOREAN mi2ukol: PASS`, `KOREAN mi2uko: PASS`, `FALLBACK: PASS` (MI2).

- [ ] **Step 1: Run**

```bash
cd ~/work/scummvm
python3 harness/dos/ute_audio.py modes --game mi2 --target mi2u | tail -1
python3 harness/dos/ute_audio.py korean --game mi2 --target mi2ukol | tail -1
python3 harness/dos/ute_audio.py korean --game mi2 --target mi2uko | tail -1
python3 harness/dos/ute_audio.py fallback --game mi2 | tail -1
```
Expected: `MODES mi2u: PASS` (INI voice+text, voice only, text only, and Ctrl+T cycling), `KOREAN mi2ukol: PASS`, `KOREAN mi2uko: PASS`, `FALLBACK: PASS` (no `monkey2.sog` -> "SFX file not found" -> the game still runs, text only).

- [ ] **Step 2: If `modes` fails on timing only**

The Ctrl+T stages (`ctrlt`: voice+text 60, text 60, voice 100 loops, step 3) were set from MI1's dialogue pacing. If the failure is "no speech/text within N loops" in a stage, widen that stage in `PROFILES["mi2"]["ctrlt"]` to what the Task 7 census shows (loops between speech lines), record the Ruling, rerun. Any other failure: capture the run dir and stop.

- [ ] **Step 3: If `korean` reports missing glyphs for Hangul**

That contradicts the no-rebake Ruling: stop and report the code points (`hires_text_log=true` lines `has no glyph`). Non-Hangul symbols may be listed in the ledger and accepted, as for MI1.

- [ ] **Step 4: Commit (only if `ute_audio.py` changed)**

```bash
cd ~/work/scummvm && git add harness/dos/ute_audio.py
git commit -m "dos: MI2 Ctrl+T stage timing" -m "$(printf 'Co-Authored-By: Claude Code <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4')"
```

---

### Task 11: Mouth-to-voice offset and line spacing on MI2  [DOSBox: yes]

**Files:** ledger.

**Interfaces:**
- Consumes: Task 8's passing gate; MI1 Task 13's mouth hold-back (already in `dos-port`).
- Produces: `LIPSYNC sb16 40000: PASS`, `LIPSYNC sbpro2 40000: PASS`, `SPACING: PASS` for MI2.

- [ ] **Step 1: Run**

```bash
cd ~/work/scummvm
python3 harness/dos/ute_audio.py lipsync --game mi2 --sb sb16 --cycles 40000 | tail -1
python3 harness/dos/ute_audio.py lipsync --game mi2 --sb sbpro2 --cycles 40000 | tail -1
python3 harness/dos/ute_audio.py spacing --game mi2 | tail -1
```
Expected: median mouth-to-voice offset under about 150 ms on both cards, and `SPACING: PASS` (the first 20 gaps between speech starts within 0.5 s of the Linux reference; `linux_speech_times` now matches `monkey2.sog`). The lipsync run reads the intro window: if MI2's intro has no speech-synchronised mouth (no animated talkers in the first lines), the scenario needs MI2's own mouth-marker source: `LIPSYNC` uses engine log markers the MI1 plan's Task 12 added (`startTalkSound`/mouth events) and they are generic, so the same markers appear; if the run reports "no markers", read `cmd_lipsync` and point its wait at the first loop with speech from the Task 7 census, and record the Ruling.

- [ ] **Step 2: If the median exceeds 150 ms**

The hold-back computes the output latency from the mixer; MI2's rate range (22-48 kHz) changes the latency per frame, and the hold-back is rate-aware only if MI1 Task 13 used `getOutputRate()`. Inspect `Sound::startTalkSound`'s hold-back call (grep for the Task 13 commit: `git log --oneline -20 --grep="mouth"`). Report the per-clip offsets by rate in the ledger and stop: a change to `engines/scumm` is a generic `SCUMM:` commit outside this plan.

---

### Task 12: Release: the data-free MI2UTE kit, README, licences, two-zip run  [DOSBox: Step 6 only]

**Files:**
- Create (harness, master): `harness/dos/release/build_mi2ute.py`
- Modify (code): `backends/platform/dos/README.md`

**Interfaces:**
- Consumes: `harness/dos/release/build_mi1ute.py` (113 lines; argparse `--repo --dist --work --suffix`; zips via `pc.make_zip` and `pc.zip_name("game", "mi1ute", ...)`), `twozip_test.py` (`BAT=… ID=…` environment variables and a pack folder), `build_engine.py` (licences).
- Produces: `build_mi2ute.py` writing a game zip named `pc.zip_name("game", "mi2ute", ...)` that holds the INI/BATs/README for the pack and **no game data** (no `.000/.001/.SOG`, no fonts, no `KOREAN.TRS`), with the user's own pack folder merging over it; the engine zip's licence texts (`VORBIS.TXT` already staged by `build-dos.sh`) unchanged.

- [ ] **Step 1: Read the model**

`cd ~/work/scummvm/harness/dos/release && sed -n 1,113p build_mi1ute.py && ls`. The MI2 script differs only in text: game id `mi2ute`, folder `MI2UTE`, the title "Monkey Island 2: LeChuck's Revenge Ultimate Talkie Edition", the speech file `MONKEY2.SOG` (Ogg Vorbis), the BAT names (`MI2.BAT`, `MI2KO.BAT`, `MI2KOL.BAT` from `mkute.py`'s MI2 profile) and the user instructions: build the pack with `mkute.py --game mi2 ...`.

- [ ] **Step 2: Write `build_mi2ute.py`**

Copy `build_mi1ute.py` to `build_mi2ute.py` and change exactly those strings; keep the structure, arguments and the data-free guarantee (the file list written into the zip is generated from `mkute`'s per-game text files, never from the game folder). Do not factor the two scripts into one: a sibling copy keeps the MI1 release byte-stable (Ruling).

- [ ] **Step 3: README**

In `backends/platform/dos/README.md`, find the MI1 Ultimate Talkie section and add an MI2 paragraph: `mkute.py --game mi2`, the pack folder `MI2UTE`, the targets `mi2`, `mi2ko`, `mi2kol`, the `--tremor-check` option (host Tremor check via `build-deps.sh host`), the `dos_vorbis_selftest*` keys (diagnostic, off by default), and the measured Tremor CPU and per-line open cost from Task 7. State plainly: Ogg Vorbis speech is decoded by Tremor, music is AdLib (no CD tracks).

- [ ] **Step 4: Build the kit and check it is data-free**

```bash
cd ~/work/scummvm && python3 harness/dos/release/build_mi2ute.py --repo dos-mi2 --dist dos-mi2/dist/dos --work runs/mi2ute/release --suffix test 2>&1 | tail -3
unzip -l runs/mi2ute/release/*mi2ute*.zip | tail -20
unzip -l runs/mi2ute/release/*mi2ute*.zip | grep -ci "\.000\|\.001\|\.sog\|\.trs\|\.fnt"
```
(Match the real argument spelling to `build_mi1ute.py`'s argparse.) Expected: the zip builds; the final `grep -c` prints `0`.

- [ ] **Step 5: Commit**

```bash
cd ~/work/scummvm/dos-mi2 && git add backends/platform/dos/README.md
git commit -m "DOS: Document the MI2 Ultimate Talkie pack and the Vorbis selftest" -m "$(printf 'Co-Authored-By: Claude Code <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4')"
cd ~/work/scummvm && git add harness/dos/release/build_mi2ute.py
git commit -m "dos: Release builder for the data-free MI2 Ultimate Talkie kit" -m "$(printf 'Co-Authored-By: Claude Code <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4')"
```

- [ ] **Step 6: Two-zip run  [DOSBox: yes]**

Unzip the engine zip and the MI2 game zip into one scratch folder, merge `runs/mi2ute/pack/GAMES/MI2UTE` over it as a user would, and run `twozip_test.py` as MI1 Task 15 Step 4 does, with `BAT=MI2.BAT ID=monkey2` and the pack folder. Expected: as MI1 (the BAT starts the game, reaches loop N, no warning). Record the line.

---

### Task 13: Final acceptance (MI2), SCI check, final review  [DOSBox: yes]

**Files:** ledger (acceptance table). No code unless a check fails (then: the owning task, a failing test first).

**Interfaces:**
- Consumes: everything above.
- Produces: the acceptance table and the final triple-review verdicts.

- [ ] **Step 1: Builds and unit tests from HEAD**

```bash
cd ~/work/scummvm/dos-mi2 && source ~/opt/dos-dev/env.sh
nice -n 19 taskset -c 0-3 backends/platform/dos/build-dos.sh scumm 2>&1 | tail -1
nice -n 19 taskset -c 0-3 backends/platform/dos/build-dos.sh sci 2>&1 | tail -1
python3 backends/platform/dos/test_mkute.py 2>&1 | tail -1
cd ~/work/scummvm/harness/dos && python3 -m unittest test_dosgame_conf test_ute_audio 2>&1 | tail -1
```
Expected: dist listings; `OK`; `OK`.

- [ ] **Step 2: Every talk line plays**

Data side (all 6808 clips): rebuild the pack with `--test-clips` as in Task 2 Step 5 -> `MONKEY2.SOG: all 6808 clips decode` (host Tremor, same decoder as the DOS build). Game side: every gate run in Step 3 shows no `startTalkSound:`/`did not find sound`/`SFX file not found` line (part of `gate()`), and at least `min_speech` speech lines in the window. Headless runs cannot reach all 6808 lines; the all-clips decode plus the engine's own bsearch over the same index is the coverage for the rest - say so in the report. The selftest's open+prime numbers (Task 7) cover the per-line cost at the extremes of the rate range (it samples clips spread over the file).

- [ ] **Step 3: The audio gate**

```bash
cd ~/work/scummvm && export SCUMM_DOS_DIST=$HOME/work/scummvm/dos-mi2/dist/dos SCUMM_DOS_SRC=$HOME/work/scummvm/dos-mi2
python3 harness/dos/ute_audio.py gate --game mi2 --target mi2u --scenario intro --cycles 40000 | tail -1
python3 harness/dos/ute_audio.py gate --game mi2 --target mi2ukol --scenario intro --cycles 40000 | tail -1
python3 harness/dos/ute_audio.py gate --game mi2 --target mi2uko --scenario intro --cycles 40000 | tail -1
python3 harness/dos/ute_audio.py tremor --game mi2 --cycles 40000 | tail -1
```
Expected: PASS on the first two (0 underruns, clock >= 99.5 %, >= 9.5 loops/s, swap 0); the `mi2uko` line recorded, not gating; `TREMOR mi2u 40000: PASS`. The ledger table lists the measured Tremor CPU % and open+prime ms next to the spec's 20-30 % estimate.

- [ ] **Step 4: Modes, Korean, fallback, lip sync, spacing**

Rerun Task 10 Step 1 and Task 11 Step 1 on the final build. Expected: all PASS as there.

- [ ] **Step 5: Packaging flow**

Rerun Task 12 Steps 4 and 6 on the final engine zip. Expected as there.

- [ ] **Step 6: SCI regression and MI1 non-regression**

`cmp` of the stripped SCI.EXE against the Task 0 baseline (Task 6 Step 1); if identical the SCI gate is not rerun, else rerun the Task 0 Step 5 SCI lines. Then one MI1 gate run on the new build: `python3 harness/dos/ute_audio.py gate --target mi1 --scenario lookout --cycles 40000 | tail -1` (needs the MI1 pack at `runs/mi1ute/pack`), expected PASS: the MI2 changes did not disturb MI1. Also the text-only floppy Korean targets: `python3 harness/dos/m5_accept.py x mi2 mi2kol mi2ko | tail -1` -> `M5 x: PASS` (Task 0 Step 5's baseline).

- [ ] **Step 7: Final triple review (opus + agy + hermes glm)**

Package: `git diff d966f6a5fbc..HEAD` in `dos-mi2` (code) and the harness commits since Task 3. Reviewer brief: the spec's S4, this plan, the ledger's acceptance table. Ask specifically: Global Constraints (SCI.EXE has no codec; no game data in any commit or the kit zip; nothing in `engines/` or `audio/` changed; MI1 pack output unchanged), the thresholds in `tremor_verdict` against the spec, and `ute_audio.py` MI1 behaviour unchanged. Fix what the reviewers (or the adjudication) require, with a re-review of the fixes. Opus for the Claude reviewer; pass `model: "opus"`.

- [ ] **Step 8: Report**

Ledger: the acceptance table (each spec acceptance line, the command, the result line), sizes (stripped SCUMM.EXE/SCI.EXE vs Task 0), measured Tremor CPU and open+prime numbers, the MI2 gate constants and why, open follow-ups. Nothing is pushed or merged.

---

## Out of scope / follow-ups

- MI2 Music beyond AdLib: none exists in the UTE data; the MI1 CD-track variants (S5) are a different plan.
- S6: real-hardware cost of Tremor and per-line seeks into a 135 MB file on IDE/CF; the U preset's loop rate at P75 (Task 8, informational).
- Resampling or pre-converting the 44-48 kHz clips to a lower rate to reduce decode cost: only if Task 9 fails, and then it is a design change (the spec says "no transcoding").
- Merging `dos-mi2` into `dos-port`, pushing, and the user check-in.
