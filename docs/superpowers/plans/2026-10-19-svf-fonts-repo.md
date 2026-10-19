# svf-fonts repository and clean ScummVM fork - Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move every font and the font tools out of ScummVM into a private `svf-fonts` repository used as a git submodule, and
replace the tangled GitHub fork with a clean fork whose history carries no fonts and no translation data.

**Architecture:** A baseline manifest (sha256 of every face file the current `jamo` HEAD ships, plus the faces every committed
map names) is taken first and is the acceptance test of every later task. `svf-fonts` is created as a snapshot of the fonts at
`jamo` HEAD. A new clone of `jamo` has only its own commits (merge base with upstream .. `jamo`) rewritten with `git filter-repo`,
then gets the submodule, the build-file switch and an upstream merge. The old fork is deleted only after the new branch builds and
passes, and only with the user's confirmation at that moment.

**Tech Stack:** git, git-filter-repo, GitHub (`gh`), Python 3 (tools, unittest), GNU make (ScummVM build), DJGPP build
(`backends/platform/dos/build-dos.sh`), the harness release builders (`~/work/scummvm/harness/dos/release`).

**Spec:** `docs/superpowers/specs/2026-10-19-svf-fonts-repo-design.md` (commit 40ffd1479a4 on `jamo`).

## Global Constraints

- New repository: `~/work/svf-fonts`, GitHub `TaehyunKimLL/svf-fonts`, **private**. No "scummvm" in its name.
- `svf-fonts` is the master of the fonts; ScummVM only uses them, through a **git submodule**.
- `johab-fonts` stays as it is (research, decoders). It gets a private remote `TaehyunKimLL/johab-fonts`.
- `HWPHG24.SVF` = the version committed at `jamo` 40ffd1479a4.
- iolo: only the 16 px JSET faces (`*8J.SVF`, `*6J.SVF`, 18 files). No iolo precomposed faces, no `WCOM6_16`, `IYAGP16`.
- Mona, 뚝딱감자체 (TUKTAK), DeltaGlass, OG 르네상스 비밀: as TTF from `~/work/johab-fonts/ttf/`, in a candidates folder no build
  installs or packs. No baked SVFs of them.
- No SVF format change, no renderer change, no new fonts, no re-baking.
- Anti-goal: any shipped face or `fonts-cjk.dat` changing a byte; fonts or tools existing in two places; fonts committed into
  ScummVM again.
- History of the new fork: no font file and no translation data in any commit after the upstream merge base (2026-09-15).
- The old fork `TaehyunKimLL/scummvm` is deleted only after Task 7 passes and the user confirms in that moment.
- Commits end with `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`. No model names elsewhere.

## Review Focus

1. A map naming a face by a path that only resolved because the face sat next to the map (`dos/*.MAP` naming bare `X.SVF`):
   after the move the DOS `DATA\` folder and the packs must still hold the face next to the map - Task 5 and Task 8 compare the
   staged file lists, not only the face bytes.
2. `fonts-cjk.dat`: upstream's file is restored in history, but the binaries must keep installing the Korean one; a build that
   silently installs upstream's would change every map that names a built-in face - Task 5 checks its sha256 in the install.
3. Harness game packs carry their own maps (`HIRESVF.MAP`, `HIRESBIT.MAP`, `HIRESTTF.MAP`); a face only they name must be in
   `svf-fonts` - Task 1's manifest includes them.
4. Other local clones and agentws worktrees still pointing at the old fork after the swap - Task 9 lists every remote URL.
5. A filter that also rewrites upstream commits (changed SHAs, the fork no longer shares history with `scummvm/scummvm`) -
   Task 4 checks that the merge base SHA is unchanged.

---

### Task 0: Safety copies and johab-fonts remote

**Files:**
- Modify: `~/work/johab-fonts/.gitignore` (add `ttf/`)
- Create: `~/work/johab-fonts/downloads/ttf.tsv` (rows for `ttf/*`: local_path, url, bytes, sha256, fetched, note; Mona URL
  `https://github.com/MonadABXY/mona-font`, user-supplied files with `url` = `user-supplied` and the date 2026-10-18)

- [ ] **Step 1:** Bundle every ref of the ScummVM clones: `git -C ~/work/scummvm/jamo bundle create ~/work/backup/scummvm-jamo-2026-10-19.bundle --all`, same for `~/work/scummvm` (`scummvm-main-…bundle`). Verify: `git bundle verify` prints "is okay" for both.
- [ ] **Step 2:** Bundle the old fork as GitHub has it: `git clone --mirror https://github.com/TaehyunKimLL/scummvm.git ~/work/backup/scummvm-fork-mirror.git` then `git -C … bundle create ~/work/backup/scummvm-fork-2026-10-19.bundle --all`. Save the two PR comments: `gh api repos/TaehyunKimLL/scummvm/issues/comments > ~/work/backup/scummvm-fork-pr-comments.json` and `…/pulls/comments > …-review-comments.json`. Verify: the bundle lists 23 heads and 3 tags (`git bundle list-heads | grep -c refs/heads` = 23).
- [ ] **Step 3:** In `johab-fonts`: add `ttf/` to `.gitignore`, write `downloads/ttf.tsv`, commit the decoder/doc changes now in the tree (`tools/{kfont,dkb_fnt,hwp1_fnt,bdf2ttf}.py`, `scripts/mac_ttf_add_unicode.py`, `docs/`, `notes/`). Do **not** commit `scripts/build_*_svf.py` (Task 3 moves them). Verify: `git status --porcelain` shows only the six `scripts/build_*` files and `samples/bold_gothic_16_18_comparison.png`.
- [ ] **Step 4:** `gh repo create TaehyunKimLL/johab-fonts --private --source ~/work/johab-fonts --push`. Verify: `git -C ~/work/johab-fonts status -sb` shows `## main...origin/main`.

### Task 1: Baseline face manifest and map-face check

**Files:**
- Create: `~/work/svf-fonts/tools/facecheck.py`, `~/work/svf-fonts/tools/test_facecheck.py`
- Create: `~/work/svf-fonts/manifest/baseline-2026-10-19.tsv` (generated)

**Interfaces:**
- Produces: `facecheck.py manifest --git <repo> --rev <rev> --path <dir>... -o <tsv>` (rows `path<TAB>sha256<TAB>bytes` of the
  committed blobs, not the working tree); `facecheck.py verify <tsv> --map <old-prefix>=<new-root>...` (exit 0 when every row's
  file exists under its new root with the same sha256; prints each mismatch); `facecheck.py mapfaces --maps <dir>... --root <prefix>=<dir>...`
  (exit 0 when every face a map names - `[fonts]` values, bare names and `data:` paths, `#N` suffix stripped - is found: a bare
  name under a root with prefix `.`, a `data:hires_text/fonts/x/y.ttf` path under the root whose prefix it starts with; prints
  `MISSING <map> <face>`).

- [ ] **Step 1: Write the failing tests** in `test_facecheck.py` (unittest, temp git repo fixture):
  `test_manifest_reads_committed_blob_not_worktree` (a file changed in the worktree after commit keeps the committed sha256);
  `test_verify_reports_changed_byte` (exit 1, output names the path); `test_verify_maps_prefix` (`dists/engine-data/hires_text/dos=svf/dos` passes for an identical copy);
  `test_mapfaces_bare_and_data_paths` (map with `a=FOO.SVF`, `b=data:hires_text/fonts/x/y.ttf#0` against `--root .=<d1> --root hires_text/fonts=<d2>` holding both: exit 0; remove one: exit 1 and `MISSING`).
- [ ] **Step 2:** Run `python3 -m unittest -v tools/test_facecheck.py` in `~/work/svf-fonts` - FAIL (no module).
- [ ] **Step 3:** Implement `facecheck.py` (`git ls-tree -r` + `git cat-file --batch` for the manifest; INI parse with the map rules of `graphics/hires_text/README.md` "`[fonts]`, paths and font values").
- [ ] **Step 4:** Run the tests - PASS.
- [ ] **Step 5:** Generate the baseline from `jamo` at 40ffd1479a4: paths `dists/engine-data/hires_text/dos`, `dists/engine-data/hires_text/jamo-extra`, `dists/engine-data/hires_text/fonts`, `dists/engine-data/fonts-cjk.dat` (map files `*.MAP`, `*.HOT`, `*.map` excluded from the manifest; they stay in ScummVM). Run `mapfaces` over `jamo/dists/engine-data/hires_text/{dos,maps}` and every map under `~/work/scummvm/harness/dos/release` with `--root .=hires_text/dos --root .=hires_text/jamo-extra --root hires_text/fonts=hires_text/fonts` (relative to a `git archive` of 40ffd1479a4's `dists/engine-data`): it must exit 0 now (if it does not, report the misses to the user before going on - that is a pre-existing problem, not ours).
- [ ] **Step 6:** `git init ~/work/svf-fonts`, commit `tools/facecheck.py`, its test and the manifest.

### Task 2: svf-fonts content and remote

**Files (all in `~/work/svf-fonts`):**
- Create: `svf/dos/` (SVF + licence TXT + `FACES`-type files from `hires_text/dos`, no maps), `svf/jamo-extra/` (all of it),
  `svf/iolo/` (the 18 JSET files from `jamo/dists/engine-data/hires_text/dos/{AD24,DKBY,HANME,PILGI,SAM,SAN,SANS,SERIF,THIN,U4K}8J.SVF`,
  `{HGKEY,HLINK,JOONG,MR128,ROMAX,SCRN9,WCOMM,WOORI}6J.SVF` + `LICENSE-MIT.txt`, `LICENSE-OFL.txt` and an `IOLO.TXT` naming the two
  source repos from `~/work/johab-fonts/dos/raw/gh_iolo__{6x2x1,8x4x4}-fonts`), `ttf/<family>/` (all of `hires_text/fonts/`, incl.
  `FONTS.md`), `ttf/candidates/{mona,tuktak,deltaglass,ogren}/` (TTFs + licence/readme from `johab-fonts/ttf/`; Mona: the
  `ttf/mona/LICENSE` and only the `.ttf` files), `cjk/fonts-cjk.dat` (blob of `dists/engine-data/fonts-cjk.dat` at 40ffd1479a4),
  `spec/SVF.md`, `README.md`.

- [ ] **Step 1:** Copy the committed blobs (from `git archive 40ffd1479a4 -- <paths>`, not the working tree) into the layout above.
- [ ] **Step 2:** `spec/SVF.md`: the SVF sections of `jamo/graphics/hires_text/README.md` - "Bitmap fonts (G2)", "Whole or streamed",
  "Preloading", "Jamo-set fonts (flags bit 1)", "Per-glyph dy" - copied verbatim with a header naming the source commit.
- [ ] **Step 3:** `README.md`: what the repo is (generic bitmap font environment; ScummVM is one user), the layout, the master rule
  ("fonts are made and changed only here; tools write only to a path given as an argument"), room for `lib/` (C renderer),
  `web/` (WASM preview), `examples/` (ESP32, Pico) as future steps - no empty folders.
- [ ] **Step 4:** Verify: `python3 tools/facecheck.py verify manifest/baseline-2026-10-19.tsv --map dists/engine-data/hires_text/dos=svf/dos --map dists/engine-data/hires_text/jamo-extra=svf/jamo-extra --map dists/engine-data/hires_text/fonts=ttf --map dists/engine-data/fonts-cjk.dat=cjk/fonts-cjk.dat` - exit 0. `mapfaces` over the same maps as Task 1 with `--root .=svf/dos --root .=svf/jamo-extra --root hires_text/fonts=ttf` - exit 0.
- [ ] **Step 5:** Commit ("svf-fonts: fonts from scummvm jamo 40ffd1479a4 …"), `gh repo create TaehyunKimLL/svf-fonts --private --source . --push`. Verify `## main...origin/main`.

### Task 3: Font tools into svf-fonts

**Files:**
- Move (from `jamo/tools/korean/` to `svf-fonts/tools/`): `jamoset.py jamovec.py jamoadapt.py jamoaa.py jamofit.py jamoseg.py johab844.py mkfont.py svfdy.py bdfface.py facecoverage.py fontmetrics.py fontplan.py glyphheight.py latinink.py pairing2svf.py bake-supplements.py bake-dos-fonts.sh bake-game-fonts.sh bake-qfg1-fonts.sh test_jamoset.py test_jamoaa.py test_mkfont.py test_bdfface.py test_facecoverage.py test_pairing2svf.py testdata/` and `devtools/make-fonts-cjk-ko.py`.
- Create: `svf-fonts/tools/build/iolo_jset.py` (from `johab-fonts/scripts/build_iolo_svfs.py`, JSET part only, with
  `--iolo621 DIR --iolo844 DIR --out DIR` and no default paths); `svf-fonts/tools/attic/` with the other five
  `johab-fonts/scripts/build_*_svf.py` unchanged and an `attic/README.md` saying they hard-code old paths and are not run.
- Delete from johab-fonts working tree: the six `scripts/build_*_svf.py`.

**Interfaces:**
- Consumes: none.
- Produces: tools importable from `svf-fonts/tools` (`sys.path` insert of their own dir, as today). Every script that writes a
  font takes its output path as an argument; a grep for `/home/` and `scummvm/jamo` in `svf-fonts/tools` (outside `attic/`) finds nothing.

- [ ] **Step 1:** Before moving, list every script in `jamo/tools/korean` that imports a moved module (`git grep -l -E "import (jamoset|mkfont|jamoaa|jamovec|jamoadapt|johab844|bdfface|facecoverage|svfdy)"`): each must move too or take the `svf-fonts/tools` path; record the decision in the commit message.
- [ ] **Step 2:** Move, fix paths (`testdata`, `dists/engine-data/hires_text/…` defaults -> required arguments or `svf/…` relative to the repo root).
- [ ] **Step 3:** `iolo_jset.py` rebuilds the 18 JSETs: run it to a temp dir and `cmp` each against `svf/iolo/` - all identical.
- [ ] **Step 4:** Run `python3 -m unittest discover -s tools -p 'test_*.py' -v` in `svf-fonts` - all pass (same count as before the move in `jamo`, run there first and note the number).
- [ ] **Step 5:** Commit and push `svf-fonts`. (The deletion from `jamo/tools/korean` happens in Task 5 on the rewritten branch.)

### Task 4: Rewrite the work history without fonts and translations

**Files:** new clone `~/work/scummvm-korean` (from `~/work/scummvm/jamo`, branch `jamo`).

- [ ] **Step 1:** Install git-filter-repo (`pip install --user git-filter-repo`); `git clone --no-local ~/work/scummvm/jamo ~/work/scummvm-korean`; add remote `upstream` = `https://github.com/scummvm/scummvm.git`, fetch `master`. Record `B=$(git merge-base upstream/master jamo)` (expected `503d0747781…`).
- [ ] **Step 2:** `git filter-repo --force --refs $B..jamo --invert-paths` with: `--path-glob 'dists/engine-data/hires_text/dos/*.SVF'`, `--path-glob 'dists/engine-data/hires_text/dos/*.TXT'`, `--path dists/engine-data/hires_text/jamo-extra/`, `--path dists/engine-data/hires_text/fonts/`, `--path-glob 'tools/korean/scumm-fonts/*.tsv'`, `--path dists/engine-data/scikor.dat`, `--path tools/korean/trs-fixes/`, `--path dists/engine-data/fonts-cjk.dat`. (Before running, list `git ls-tree -r --name-only jamo -- dists/engine-data/hires_text/dos | grep -v -E '\.(SVF|TXT|MAP|HOT)$'`: any other kind found there is decided with the user's rules - font data out, maps in.)
- [ ] **Step 3:** Restore upstream's `fonts-cjk.dat` in one commit: `git checkout $B -- dists/engine-data/fonts-cjk.dat`.
- [ ] **Step 4: Verify:** `git merge-base upstream/master jamo` still prints `$B` (upstream commits untouched); `git log --all --format= --name-only $B..jamo | grep -E '\.(SVF|ttf|otf|ttc)$|jamo-extra/|scikor\.dat|trs-fixes/|scumm-fonts/.*\.tsv'` prints nothing; `git rev-list --count $B..jamo` is 1,124 minus the commits that became empty (filter-repo prunes them; note the number); the tree outside the removed paths equals `jamo` 40ffd1479a4 (`git diff --stat 40ffd1479a4 jamo -- . ':!dists/engine-data/hires_text' ':!tools/korean/scumm-fonts' ':!tools/korean/trs-fixes' ':!dists/engine-data/scikor.dat' ':!dists/engine-data/fonts-cjk.dat'` across the two repos via a fetch of the old clone: empty).

### Task 5: Submodule and build switch

**Files (in `~/work/scummvm-korean`):**
- Add submodule: `dists/svf-fonts` -> `https://github.com/TaehyunKimLL/svf-fonts.git`.
- Modify: `Makefile.common:440-447`, `dists/engine-data/engine_data.mk:114-115`, `dists/engine-data/engine_data_big.mk:3`,
  `ports.mk:28`, `backends/platform/android/android.mk:53-55`, `backends/platform/dos/build-dos.sh:198`,
  `backends/platform/dos/mkcaption.py:11`, `devtools/module.mk:54`, `dists/engine-data/README:47,73`,
  `test/engines/ags/hires_font_plan.h:256`, `test/engines/sci/glyphplacement.h:475`, `graphics/hires_text/README.md` (SVF sections
  replaced by a link to `dists/svf-fonts/spec/SVF.md`), `tools/korean/README.md`, `tools/korean/SCUMM_FONTS.md`,
  `tools/korean/TRANSLATION_PIPELINE.md` (tools now in `dists/svf-fonts/tools`).
- Delete: the font tools moved in Task 3 from `tools/korean/`, and `devtools/make-fonts-cjk-ko.py`.

**Interfaces:**
- Consumes: `svf-fonts` layout of Task 2 (`svf/dos`, `svf/jamo-extra`, `ttf/<family>`, `cjk/fonts-cjk.dat`).
- Produces: unchanged install layout: `hires_text/fonts/<family>/…` for TTF (with `HIRES_TEXT_FONTS=1`), DOS `DATA\` with the SVFs
  next to the maps, `fonts-cjk.dat` = the Korean one from `cjk/`, Android assets as today.

- [ ] **Step 1:** Before switching, in the old `jamo` tree: record the install listings with sha256 - Linux `make install DESTDIR=$T/old` (configure as the existing `builds/` Linux build), DOS `build-dos.sh` `dist/dos/` tree, Android assets folder if the toolchain is present (else note it skipped). Save as `$T/old-*.tsv`.
- [ ] **Step 2:** Add the submodule, make the edits; `ttf/candidates/` must be excluded from every install glob.
- [ ] **Step 3:** Rebuild the same three; listings `$T/new-*.tsv`. Verify: `diff old new` empty for each (paths and sha256).
- [ ] **Step 4:** `make test` - same pass count as on old `jamo`; the two tests that read faces read them from `dists/svf-fonts`.
- [ ] **Step 5:** `git grep -n -E "hires_text/(dos|jamo-extra|fonts)/[^ ]*\.(SVF|ttf|otf)" -- ':!docs'` points only at `dists/svf-fonts` or at installed (`data:`) paths; `git ls-files | grep -E '\.(SVF|ttf|otf|ttc)$'` lists only upstream fonts and `test/fonts/noto-thai/`.
- [ ] **Step 6:** Commit.

### Task 6: Merge upstream master

- [ ] **Step 1:** `git fetch upstream master && git merge upstream/master` on `jamo` in `~/work/scummvm-korean`; resolve conflicts keeping both sides' intent; list each conflicted file and the resolution in the merge commit message.
- [ ] **Step 2:** Rerun Task 5 Steps 3-5 (listings vs `$T/old-*.tsv` for the files we own; upstream's own data may differ and is listed separately for the user) and `make test`. DOS build must succeed.

### Task 7: Harness on the submodule

**Files:** `~/work/scummvm/harness`: `dos/release/build_font_pack.py`, `dos/release/build_engine.py`, `tools/stage_win_jamo.py`,
`tools/stage_win_i18n.py`, `dos/release/PACKS.md`, `dos/release/TRANSLATING.md`, `dos/scummgame.py`, `dos/m1_accept.py`,
`dos/m2_accept.py`, `i18n/hirestray.py`, `dos/release/game_maps.py` (every reader of `dists/engine-data/hires_text/{dos,jamo-extra,fonts}`).

- [ ] **Step 1:** Before switching: build every font pack (`build_font_pack.py` for `bitmap-ko1`, `ttf-ko1`, `jamo-ko1`, `jamo-extra`) from old `jamo`; list zip members + sha256.
- [ ] **Step 2:** Point the readers at `<scummvm tree>/dists/svf-fonts/…` (font root as an argument where a script has a repo argument).
- [ ] **Step 3:** Rebuild the packs from `~/work/scummvm-korean`; member lists + sha256 identical (zip names may differ only in the commit id they carry).
- [ ] **Step 4:** Commit and push the harness.

### Task 8: Replace the GitHub fork (user confirmation)

- [ ] **Step 1:** Show the user: the bundles from Task 0, the Task 5-7 results, and that deleting `TaehyunKimLL/scummvm` removes 23 branches, 3 tags and 3 closed PRs (comments saved). Wait for an explicit yes.
- [ ] **Step 2:** `gh repo delete TaehyunKimLL/scummvm --yes` (needs the `delete_repo` scope: `gh auth refresh -s delete_repo` first).
- [ ] **Step 3:** `gh repo fork scummvm/scummvm --clone=false`; verify `gh api repos/TaehyunKimLL/scummvm --jq .parent.full_name` = `scummvm/scummvm`.
- [ ] **Step 4:** In `~/work/scummvm-korean`: remote `origin` = the new fork; push `jamo` (name kept: `jamo`) and set it as the default branch; no other branches, no tags.
- [ ] **Step 5:** Verify a fresh clone works: `git clone --recurse-submodules https://github.com/TaehyunKimLL/scummvm.git -b jamo $T/fresh`, then Task 5 Step 3's DOS listing from `$T/fresh` equals `$T/old-dos.tsv`.

### Task 9: Point every checkout at the new fork

- [ ] **Step 1:** List remotes of every clone and worktree: `~/work/scummvm` (+ `dos`, `jamo`, `dos-*`, `i18n*`, `win`, `johab`, `repo/scummvm`, `.worktrees/*`), `~/scummvm-i18n/*`, `~/.local/share/agentws/work/scummvm-*/*` (`git -C <d> remote -v`).
- [ ] **Step 2:** Show the user the list; on agreement, set `fork`/`origin` URLs that pointed at the old fork to the new one, and mark the old `jamo` clone as superseded (a `SUPERSEDED.md` naming `~/work/scummvm-korean`) instead of deleting it.
- [ ] **Step 3:** Update the memories and the wiki pages that name `~/work/scummvm/jamo` or the old fork (`MEMORY.md` entries, `agentos-wiki` pages found by grep), and the agentws project registration if it names the path.
