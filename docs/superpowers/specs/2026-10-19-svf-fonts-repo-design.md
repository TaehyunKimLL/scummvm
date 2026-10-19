# svf-fonts: a font repository apart from ScummVM (design)

Date: 2026-10-19. Status: draft for the user's approval.

## Why

The SVF bitmap fonts (SVFN format, jamo sets) were made for the ScummVM hi-res Korean text, and today live inside the ScummVM
tree (`dists/engine-data/hires_text/`, `tools/korean/`). Two things went wrong on 2026-10-18 because of that:

- scripts in another repository (`~/work/johab-fonts/scripts/build_*_svf.py`) wrote straight into the ScummVM tree: 127 new SVFs
  (83 MB, uncommitted) and an overwrite of the shipped `HWPHG24.SVF` that KQ1KO.MAP and KQ4KO.MAP use (restored since);
- neither the `jamo` branch nor `johab-fonts` has a remote copy.

The user also wants the fonts to stop being a ScummVM thing: a **generic bitmap font environment** that other targets can use,
first of all embedded ones (ESP32, Raspberry Pi Pico), where a jamo set (all 11,172 syllables in 13-20 KB) is the strong point.
No embedded product is chosen yet.

## The larger plan (only step 1 is designed here)

1. **This document**: the `svf-fonts` repository - SVF format spec, the font tools, the font files; ScummVM uses it as a git
   submodule.
2. A portable C renderer library (SVF parse, JSET compose, 1 bpp / AA drawing, integer and AA scaling; no malloc, fonts read from
   flash or streamed), taken out of ScummVM's `graphics/hires_text`; ScummVM then uses it.
3. A web preview / font picker built on that library compiled to WASM, so the picker shows exactly what the engine draws.
4. Embedded examples (ESP32, Pico), when a use exists.

Step 1 leaves room for 2-4 (folders, licence) but builds none of them.

## Decisions made by the user

- Name **svf-fonts**: `~/work/svf-fonts`, GitHub `TaehyunKimLL/svf-fonts`, private. No "scummvm" in the name.
- `svf-fonts` is the **master** of the fonts: fonts are made, changed, rebuilt and checked only there. ScummVM only uses them.
- ScummVM takes `svf-fonts` as a **git submodule**.
- The font tools are kept in one place with the fonts (they move to `svf-fonts`).
- `johab-fonts` stays as it is (collection and decoding research). Only SVF-related things come over from it.
- `HWPHG24.SVF` stays the committed version (the agy replacement was reverted, 2026-10-19).
- iolo 6x2x1 / 8x4x4 fonts: only the original-size **16 px JSET** faces (`*8J.SVF`, `*6J.SVF`, 18 files) go in. The precomposed
  12/16/24/32 px renders are dropped: they are TTF re-renders of the 16 px design (12/24 px are visibly damaged, 16 px differs from
  the JSET composition in thousands of syllables, 32 px is a plain 2x).
- Resizing is the renderer's job, not baked files: whole multiples by pixel replication; non-whole factors through AA
  resampling, acceptable as a fallback. (Renderer work is step 2, not here.)
- Fonts that are TrueType at the source (Mona, 뚝딱감자체 / TUKTAK, DeltaGlass, OG 르네상스 비밀) go in **as TTF** for now, not as
  baked SVFs. (DOS builds have no FreeType; a TTF face picked for a DOS game gets baked then.)

## Goals

- One repository holds every SVF face, the TTF faces the maps use, their licence texts and FACES-style indexes, the SVF format
  spec, and the tools that make and check faces.
- ScummVM builds, installs and packages exactly as today, reading the fonts through the submodule.
- Nothing outside `svf-fonts` writes into it implicitly: tools take the output path as an argument.
- `svf-fonts` has a private remote. `johab-fonts` gets a private remote too (`TaehyunKimLL/johab-fonts`) (*my call*).

## Non-goals

- No change to the SVF format or to the renderer (step 2).
- No web picker or WASM build (step 3); the existing `tools/korean/mapgen` work is not finished or published here.
- No new fonts made; no re-baking of existing faces.
- `johab-fonts` is not merged or restructured.

## Anti-goals (would make this a failure even if it works)

- Any shipped game changing its look: every face a committed map names must be byte-identical after the move.
- Fonts or tools existing in two places that can drift (as `tools/korean/fontlab` vs `johab-fonts/tools` do today).
- Font files committed into ScummVM again after the move.

## What moves, what stays

| Today (ScummVM `jamo`) | After |
| --- | --- |
| `dists/engine-data/hires_text/dos/*.SVF`, `*.TXT` (licences) | `svf-fonts` |
| `dists/engine-data/hires_text/jamo-extra/` (faces, FACES.TSV) | `svf-fonts` |
| `dists/engine-data/hires_text/fonts/<family>/` (TTF + licences, FONTS.md) | `svf-fonts` |
| `dists/engine-data/hires_text/dos/*.MAP`, `*.HOT`, `hires_text/maps/*.map` | **stay** in ScummVM (game decisions) |
| SVF format part of `graphics/hires_text/README.md` (header, JSET, per-glyph dy) | spec moves to `svf-fonts`; README links to it |
| Font tools in `tools/korean/`: `jamoset.py`, `jamovec.py`, `jamoadapt.py`, `jamoaa.py`, `jamofit.py`, `jamoseg.py`, `johab844.py`, `mkfont.py`, `svfdy.py`, `bdfface.py`, `facecoverage.py`, `fontmetrics.py`, `fontplan.py`, `glyphheight.py`, `latinink.py`, `pairing2svf.py`, `bake-supplements.py`, `bake-*-fonts.sh`, their tests and `testdata` | `svf-fonts` |
| `tools/korean/fontlab/` (untracked copies of `johab-fonts/tools` decoders, already drifted) | deleted; the decoders stay owned by `johab-fonts` (*my call*) |
| `tools/korean/mapgen/` (untracked, 241 MB, imports `jamoset`) | not moved or committed in step 1; it gets redone in step 3 (*my call*) |
| Translation / game tools (`sci0_kr_extract.py`, `sci11_kr_extract.py`, `packvol.py`, `scummscript.py`, `charsetchars.py`, `findtext.py`, `scumm-fonts/` game charset extracts, ...) | stay |
| `johab-fonts/scripts/build_*_svf.py` | `svf-fonts`, with the `johab-fonts` checkout and the output path as arguments (no hard-coded paths) |
| New, uncommitted (agy, 2026-10-18): iolo JSET x18 | `svf-fonts` |
| New, uncommitted: iolo precomposed x~70, `WCOM6_16`, `IYAGP16` | dropped |
| New, uncommitted: Mona / TUKTAK / DGLAS / OGREN SVFs | dropped; their TTF sources go in instead (from `johab-fonts/ttf/`), in a candidates folder that no build installs or packs until a map uses the face (*my call*), so binaries and packs stay as today |

## How ScummVM uses it

- Submodule at a path inside the ScummVM tree; every place that reads the moved folders reads them there instead:
  `Makefile.common` (`DIST_DIRS_HIRES_TEXT`), `dists/engine-data/engine_data.mk` (`SANGAM16.SVF`, `SAN10.TXT`), `ports.mk`,
  `backends/platform/android/android.mk`, `backends/platform/dos/build-dos.sh`, `backends/platform/dos/mkcaption.py`,
  `engines/sci/graphics/hirestextstate.cpp` (if it names a path), tests under `test/` that load faces.
- The harness (`TaehyunKimLL/scummvm-hires-harness`) release builders read the faces from the ScummVM tree today
  (`dos/release/build_font_pack.py`, `build_engine.py`, `tools/stage_win_jamo.py`, ...); they follow the new path.
- Maps keep naming faces as today (`data:hires_text/fonts/<family>/<file>`, bare SVF names): installed and packaged layouts do
  not change, only where the build copies them from.

## A clean ScummVM fork without fonts or translations (user, 2026-10-19)

The current GitHub fork `TaehyunKimLL/scummvm` is tangled (public branches `dos-port`, `ime-port`, `i18n`, `hires-*`, `wt/*`,
fonts committed in them). The user's decision:

- **Delete `TaehyunKimLL/scummvm` and fork again from `scummvm/scummvm`** (option "가"). GitHub allows one fork per account, so the
  old one must go first. This is irreversible: before deleting, every branch of the old fork is kept locally (a mirror clone or a
  `git bundle` of all refs), and the user confirms the deletion at that moment.
- The Korean bitmap / DOS work comes over from local `jamo`, which already contains `dos-port`, `ime-port`, `i18n` and `hires-text`
  (checked: all are ancestors of `jamo`). `fork/hires-korean` (9 old commits) is not carried.
- The work history is rewritten so that **fonts and translation data never appear in it** (`git filter-repo` on the 1,123 commits
  `jamo` has over its merge base with upstream, 2026-09-15); then current upstream `master` is merged in.
- Removed from history:
  - fonts: `hires_text/dos/*.SVF`, the face licence `*.TXT`, `hires_text/jamo-extra/`, `hires_text/fonts/` (TTF, licences,
    FONTS.md), `tools/korean/scumm-fonts/*.tsv` (game charset extracts) (*my call*);
  - translation data: `dists/engine-data/scikor.dat`, `tools/korean/trs-fixes/*.tsv` (translations stay in the private harness);
  - `dists/engine-data/fonts-cjk.dat` goes back to the upstream version; the Korean build of it moves to `svf-fonts`, made by
    `devtools/make-fonts-cjk-ko.py` (*my call*).
- Kept: all code, maps (`*.MAP`, `*.HOT`, `maps/*.map`), tools, tests (including `test/common/korean_stem_corpus.txt`), docs.
- The fonts come back only through the `svf-fonts` submodule, added on top of the rewritten history.
- Everything that names the `fork` remote follows the new fork: local clones and worktrees (`~/work/scummvm`, `dos`, `jamo`,
  agentws worktrees), the harness, docs.

## Order

1. Push safety: `johab-fonts` and `svf-fonts` get private remotes; the old fork's branches and local `jamo` are kept in bundles.
2. Create `svf-fonts` from the current `jamo` tree's fonts and font tools; push.
3. Rewrite the `jamo` work history without fonts and translations; merge upstream `master`; add the `svf-fonts` submodule and switch
   the build files; build and test (DOS, Linux, Windows cross, Android as today) with every shipped face byte-identical.
4. With the user's confirmation, delete the old GitHub fork, fork again, push the rewritten branch.
5. Point every clone, worktree and the harness at the new fork.

## Constraints
- Fonts are not distributed publicly (licence is not a selection criterion); the repository is private. Licence texts still
  travel with every face.
- Size: about 133 MB of fonts today. Plain git (no LFS) unless the builder finds a reason.
- History of `svf-fonts`: the builder decides whether the fonts' ScummVM history comes along (`git filter-repo` keeping only the
  moved paths) or the repository starts from a snapshot that names the source commit.

## Left to the builder

- Folder layout inside `svf-fonts` (keep room for `lib/` (step 2), `web/` (step 3), `examples/` (step 4)).
- The submodule path in ScummVM and the exact edits to the build files.
- A check that every face named by a committed map - ScummVM's `dos/*.MAP` and `maps/*.map` **and the harness game packs' maps** (*my call*) - exists and is byte-identical to before.
- Tests under `test/` that load faces from the old paths (e.g. `test/hires_test_fonts.h`) follow the submodule path.
- Where the tools' tests run (they must pass in `svf-fonts`).
