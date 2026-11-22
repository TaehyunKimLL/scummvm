# P0 review: SVF library and Font adapter (2026-11-22)

Independent read-only review of `ko-2026.4` at `7087a3f702b` (`v2026.4.0` + 15 commits, `git diff v2026.4.0 HEAD -- . ':!docs'`,
32 files). Reviewed against `docs/korean/DESIGN.md` (sections 5, 9-12) and the P0 plan + notes, checking the code, not the notes.
Scratch work: `~/work/tmp/p0-review/` (create_project build, C compile checks, an ASan/UBSan edge probe `edge.cpp`).

## What was verified (passes)

| Check | Result |
|---|---|
| ASan `make test` in `~/work/tmp/scummvm2026-p0asan` (re-run, not taken from the notes) | `Running cxxtest tests (433 tests)...OK!`, no sanitizer report |
| `tools/korean/sync-svf.sh --check`, `python3 -I tools/korean/mkgolden.py --check` | both rc 0 |
| `devtools/check-commit-msg.py` on each of the 15 commits | 12 ok, 3 FAIL (the three pre-P0 commits, see M-5) |
| `LICENSES/COPYING.MIT` section vs `graphics/svf/LICENSE` lines 5-21 | byte-identical |
| vendored C with the real `make -n -B graphics/svf/svf_face.o` flags (`g++ -x c -std=c99 -Wall -Wshadow -W ...`, no `-pedantic`), gcc 15 | 0 warnings, all 6 C units |
| `sizeof(svf_face)` with the three defines: `-m64` 96, `-m32` 76 | matches the `static_assert`s |
| `.deps/*.d` written for `graphics/svf/*.o`; `clean` removes them (`DEPDIRS` from `MODULE_DIRS`, `rules.mk:14`) | ok |
| Library contract checks the glue relies on: `svf_read_fn` "anything but len fails the read" (`svf_stream.c:61`); stream cache slot = `block_size + 8`; syllable cache slot = `8 + largest cell`; `svf_chain_scratch_bytes` recomputed in `SvfChain::update()` on every `addFace`/`setScale`/`setSyllableCache`; coverage callback clipped to `clip ∩ [0,w)x[0,h)` (`svf_draw.c:454-462`, `blit_cb` 1143-1156) | glue matches `svf.h` |
| Edge probe under ASan+UBSan (`~/work/tmp/p0-review/edge.cpp`): 256 scale ratios 1..16/1..16 drawn on a streamed no-block-cache chain with a syllable cache; `addFace` after shrinking the scratch at 1/16 then drawing at 16/1; `SvfFont::drawChar` at 16x on 3x3 RGB565/ARGB8888/RGB332/CLUT8 surfaces at x,y in {-300,-255,-1,0,2,3,300,-32768,32767}; clip rect larger than and before the target; a face `close()`d while in a chain; U32String with U+D800, U+110000, 0xFFFFFFFF; empty chain through `SvfFont` | all clean, no OOB plot call, no crash |
| Upstream files touched | exactly those in `docs/korean/PATCHES.md` (+ new `.gitmodules`, submodule gitlink) |
| Style: tabs, GPL headers, path-based header guards, `// End of namespace`, `override` | ok (3 continuation lines aligned with spaces after a tab, which is ScummVM practice) |
| Golden tests: `Font1x` through `Font::drawString(U32String)` reproduces the whole-line Python layout including the mark (`drawStringImpl` advances `getCharWidth(mark)` = 0 and the library anchors a lone mark at the pen), so the per-code-point `drawChar` path is really exercised | legitimate |

## Findings (ranked)

### Major

**M-1. create_project builds (MSVC, Xcode, CMake, Code::Blocks) cannot compile or link the renderer.**
`graphics/module.mk:77-88`, `graphics/hires_text/svf_abi.c:276`, `graphics/hires_text/svfface.h:39`.
Two independent causes:
1. `MODULE_OBJS += $(SVF_OBJS)`: create_project's module.mk parser (`devtools/create_project/create_project.cpp:2270-2330`) does not
   expand make variables (only `$(KYRARPG_COMMON_OBJ)` is special-cased), so the six C objects are never added to the project.
   Reproduction (`~/work/tmp/p0-review/cp`): `g++ -std=c++11 -o create_project devtools/create_project/*.cpp -Idevtools/create_project`,
   then `./create_project $SRC --cmake --disable-all-engines`; `grep -i svf CMakeLists.txt` lists only `svffont.cpp`,
   `svfchain.cpp`, `svfface.cpp` — no `graphics/svf/*.c`, no `svf_abi.c`. `svfface.cpp` references `svf_open_mapped` etc.: link failure.
2. create_project has its own define list (`create_project.cpp:456-533`); the three `configure` defines never reach it. On `_WIN32`/
   `__APPLE__` `svf.h:437-444` defaults `SVF_JMAP_RECOG` to 1, so `svf_abi.c` hits `#error` and every includer of `svfface.h` fails
   `static_assert(SVF_JMAP_RECOG == 0)` — before the link error above.
Minimal fix: (a) list the six objects literally in `MODULE_OBJS` (keep `SVF_C_OBJS := $(addprefix $(MODULE)/, svf/svf_draw.o ... hires_text/svf_abi.o)`
for the target-specific `CC`/`CFLAGS`; `MODULE_OBJS += $(SVF_OBJS)` goes); (b) add `SVF_JMAP=1`, `SVF_JSET_BIG=1`, `SVF_JMAP_RECOG=0`
to `setup.defines` in `create_project.cpp` (MSVC `PreprocessorDefinitions`, CMake `add_definitions`, Xcode `GCC_PREPROCESSOR_DEFINITIONS`
all take `NAME=value`), and record `devtools/create_project/create_project.cpp` in `PATCHES.md`. Alternatively drop the `RECOG` assert
(it is compiled out when `SVF_JMAP` is 1) so the header defaults suffice on desktop; the C-file listing is needed either way.

### Minor

**M-2. `getFontHeight()` is ascent+descent across faces, not a cell height.** `graphics/fonts/svffont.cpp:65-67`, `svfchain.cpp:212-226`.
DESIGN 5.2 item 4 says `getFontHeight = cell_h`. With faces of different ascents the sum exceeds every cell: the test fixture gives 17
for two 16-px faces (`test_metrics` asserts 17). Line spacing in GUI/AGI consumers will be one row taller than the face. Not wrong for
a chain, but P1 (hires `TextLayout`) must not assume `getFontHeight() == cell_h`. Fix: either document it in `svffont.h` and DESIGN, or
return `max(scaled cell_h)` and keep ascent/descent as they are.

**M-3. Streamed open fails on a stream that legitimately returns short reads.** `graphics/hires_text/svfface.cpp:239-249`.
`readCallback` makes one `read()` call and returns its count; the library treats any count != len as a failed read. Reproduction
(`edge.cpp` case A: a `MemoryReadStream` subclass capped at 7 bytes per read): `loadStreamed` returns false, `getError()` = 6 (`SVF_E_IO`).
ScummVM file streams return full reads except at EOF, so the impact today is nil, but a wrapping/decompressing stream in P7 (DOS
lazy stores) could hit it. Fix: loop `while (got < len)` until `read` returns 0 or `err()`.

**M-4. C++-only flags leak into the C compiles on some ports (warnings).** `graphics/module.mk:86-88`.
The filter list misses `-fno-threadsafe-statics` and `-fuse-cxa-atexit` (`configure:3365-3366`, NDS). Reproduction:
`g++ -x c -std=c99 -fno-threadsafe-statics -fuse-cxa-atexit ... -c graphics/hires_text/svf_abi.c` prints
`cc1: warning: command-line option '-fno-threadsafe-statics' is valid for C++/ObjC++ but not for C` (x2). Warnings only; add both to
the `filter-out` list.

**M-5. Three of the 15 commits fail the commit-message hook and the AI attribution rule.** `bc9abd1c92d`, `db4b5732416`,
`6bc2a0051da` (submodule add, two DOCS commits): subjects lack the `SUBSYSTEM:` prefix (`devtools/check-commit-msg.py` rc 1 for each)
and carry `Co-Authored-By` without `Assisted-by:` (AI-GUIDELINES.md lines 29, 49). The 12 P0 commits are fine (both trailers, prefix
ok). These predate the P0 tasks but are inside the reviewed range and on the same branch. Fix only if the branch is ever rewritten
(`reword` to `DOCS: ...` / `DISTS: ...`); otherwise note it in P0-notes.

**M-6. `test_edges` asserts nothing.** `test/graphics/svf_font.h:113-126`. It draws off-surface cells and relies on ASan to notice an
out-of-bounds write; in the normal Linux/mingw runner a broken clip would pass (the 20x10 surface has no guard band to inspect).
Fix: draw on a larger surface filled with a sentinel, pass a sub-surface (`Surface::getSubArea` or a hand-made `Surface` pointing into
the middle) and assert the sentinel band is untouched; also assert at least one pixel inside changed.

**M-7. Composed-syllable cache is not refreshed when a chain face is reopened (UNVERIFIED).** `graphics/hires_text/svfchain.cpp:183-200`.
`svf.h:308-320`: "Every slot is emptied here: call it again after changing c->faces or reopening a face of the chain." `update()` runs on
`addFace`/`setScale`/`setSyllableCache` only; `SvfFace::loadFromMemory/loadStreamed` on a face already in a chain leaves stale cells
keyed by (face index, code point) and a `cache_cell` sized for the old face. No fixture with a different cell size exists to show the
overflow, so unverified; the API allows it (`getFace(i)->load...`). Fix: document "do not reopen a face while it is in a chain" on
`SvfChain::addFace`, or add `SvfChain::refresh()` that P1 calls after a reload.

**M-8. Runtime ABI guard disappears in release builds; static guard covers three ABIs only.** `svfface.cpp:150` uses `assert`
(`-DNDEBUG` under `--enable-release`, `configure:3930`); `svfface.h:40-44` asserts 96/76 for x86_64/aarch64/i386 only. armv7, ppc, mips,
riscv, m68k release builds have no check at all, and x32 / aarch64-ILP32 (`__x86_64__`/`__aarch64__` with `__ILP32__`) would fail the
96 assert wrongly (UNVERIFIED: no toolchain to run). Fix: compare `scummvm_svf_face_size` in `finishOpen()` and return `SVF_E_ARG` with
a `warning()` instead of `assert`; gate the 96 branch on `!defined(__ILP32__)`.

**M-9. Dumb compile branch has no `%.o: %.c` rule (UNVERIFIED).** `Makefile.common:196-213` (the `else` of `ifdef CXX_UPDATE_DEP_FLAG`)
defines no C rule, so make's built-in `%.o: %.c` runs without `$(MKDIR) $(*D)`: an out-of-tree build with a compiler that is neither
GNU-compatible nor ICC fails on `graphics/svf/`. Every compiler `configure` currently supports sets the flag (`__GNUC__` covers clang),
so this is theoretical; add the rule for symmetry if the branch is kept.

### Nits

- `graphics/module.mk:30-31`: `hires_text/svfchain.o`, `hires_text/svfface.o` are listed after `hotspot_renderer.o`; alphabetical order
  puts `hires_text/` before `hotspot_renderer.o`.
- `graphics/hires_text/svfchain.cpp:253-254`: `draw()` returns `SVF_OK` and draws nothing when `x` or `yBaseline` is outside int16; a
  line starting far left but reaching into the target is dropped whole. Clamp instead of refuse, or return `SVF_E_RANGE`.
- `graphics/hires_text/svfchain.h:47`, `svfchain.cpp:153-155`: when `addFace` returns false with `DisposeAfterUse::YES` the caller still
  owns the face; `test/graphics/svf_test_util.h:20-26` ignores both the load and the add result, so a failed load leaks (tests only).
  Say so in the doc comment.
- `tools/korean/sync-svf.sh:29`: `sha256sum` is GNU coreutils (macOS has `shasum -a 256`); `git -C "$src"` on a non-git export of the
  submodule aborts with git's message under `set -e`.
- `tools/korean/mkgolden.py:19-20`: without the submodule the script dies with an `ImportError` traceback (no message like the shell
  script's); line 106 has trailing whitespace inside the list literal.
- `test/graphics/svf_font.h:130-149`: `test_dumpWhenAsked` is a developer aid, not a test (asserts only when `svf-dump/` exists); and
  `TS_TRACE` art in `svf_golden.h:41-42` is a no-op under `--no-std --no-eh` (already noted in P0-notes item 3).
- `graphics/fonts/svffont.h:14-16`: the note that `Common::String` overloads draw each byte is correct and important (probe:
  `getStringWidth(String("한"))` = 0 vs 16 for the `U32String`); consider also saying it in DESIGN 5.2 item 4 for engine authors.
- `LICENSES/COPYING.OFL:20-21`: the fixture entry sits in a list introduced as "files distributed along with our theme files"; wording
  is slightly off but the notice content is right.

## Things checked and found correct (so nobody re-reviews them)

- Lifetime: `svf_face` holds pointers into `_data`/`_tables`/`_cache` and `_stream`; all are members of a `NonCopyable` object, sized
  before `svf_open_*`, never reallocated while open, freed only in `close()` after the struct is zeroed. `load()` with `YES` deletes the
  stream after a whole read; a failed `loadStreamed` deletes it through `close()` (`_stream` set before the first early return).
- `readCallback`: `uint32_t`/`uint8_t`/`long` types match `svf_read_fn`; `len > 0x7FFFFFFF` guard for 32-bit `long`; `seek(uint32)` fine;
  `err()` cleared after a failed read. `size() <= 0` and `> 0xFFFFFFFF` refused (`SVF_E_RANGE`; probe F: `size()` = -1 → err 4).
- Block-cache sizing `slots * (blockSize + 8)` and syllable cache `slots * (8 + max cell)` match `svf.h`; bad `blockSize` (0, 48, 65535)
  is refused by the library with `SVF_E_ARG` (probe E), no allocation issue.
- `scaleLength`/`scaleDimension` match `svf_scale.c:17,23` (round half up / ceil), so `getAscent/getDescent` agree with the drawn cell.
- `drawCoverage`/`plotPixel`: the library clips every call to `clip ∩ [0,w)x[0,h)`, so `getBasePtr(x, y)` is always inside the surface;
  2/4-byte stores use the format's `bytesPerPixel`, colours go through `PixelFormat::colorToARGB/ARGBToColor` (no hard-coded shifts,
  endianness-neutral; `a = 0xFF` for formats without alpha). CLUT8 threshold at 128 as documented; RGB332 (1 byte, not CLUT8) blends.
- `encodeUtf8`: surrogates and > U+10FFFF become the invalid byte 0xFF, which the library decodes as U+FFFD-if-present, else skipped.
- `Font` interface: `drawChar(Surface *, uint32, int, int, uint32) const` overrides the pure virtual (`font.h:214`); `ManagedSurface`
  overload, `getBoundingBox`, `getKerningOffset` keep their defaults, which `drawStringImpl` handles (mark width 0 is drawn, not skipped).
- `configure`: one `append_var DEFINES` before every host-specific block; `DEFINES` is never reset afterwards; the test runner gets
  `$(CPPFLAGS)` (`test/module.mk:93`), so `test_switches` is meaningful. `-DSVF_JMAP_RECOG=0` only pins a switch that `svf.h` compiles out.
- `make` build: `-x c` precedes the source (`Makefile.common:174`), `-std=%` and `-pedantic` filtered, `.d` files via `-MMD -MF`,
  `vpath %.c` for out-of-tree; ccache/clang prefixes are transparent to `-x c`; Vita's `CC=` in `sdl/module.mk:76` is overridden per target.
- `sync-svf.sh --check` compares every file byte-for-byte and the full regenerated `VERSION` (commit, committer date, sha256 list);
  refuses a dirty submodule; absent submodule → clear message, exit 1.
- `mkgolden.py`: faces and lines regenerated in a temp dir, outputs byte-compared; `--check` passes against the committed headers; the
  fixture header carries the OFL notice and sha256 of each face; the Python reference (`svfline.layout/measure`) is the oracle, so the
  golden tests compare the C renderer against an independent implementation (a broken blit/compose/scale would fail them).

## Verdict

**P0 is mergeable as the base for P1 with one required fix: M-1.** The glue is correct on every path I could exercise (ASan/UBSan clean
on the edge probe and the full suite; the library contracts the glue depends on are met), the tests test what they claim (except
`test_edges`, M-6), the build integration is right for every `configure`-driven port (Linux, mingw, DJGPP, Android, consoles), and the
licence files and sync/golden scripts are sound. The create_project family (MSVC, Xcode/iOS, CMake) is broken twice over by M-1 and
must be fixed before any Windows/macOS/iOS build of the fork; it does not block Linux-based P1 work.

Required before P1 builds on anything but `configure` ports:
1. **M-1**: list the six C objects literally in `MODULE_OBJS`; add the three defines to `create_project.cpp`; add that file to `PATCHES.md`.

Recommended in the same fix-up commit (small, no design impact):
2. **M-2**: decide and document `getFontHeight` (chain ascent+descent vs cell height) before P1's `TextLayout` depends on it.
3. **M-3**: loop in `readCallback`.
4. **M-4**: filter `-fno-threadsafe-statics -fuse-cxa-atexit`.
5. **M-6**: give `test_edges` a sentinel band.
6. **M-7/M-8**: document "no reopen while in a chain" (or add `refresh()`); replace the constructor `assert` with a checked `finishOpen` failure.

Not required: M-5 (history rewrite only), M-9, nits.
