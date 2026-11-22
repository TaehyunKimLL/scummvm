# P0: SVF library and Font adapter — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task by task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** vendor the svf-fonts-open C renderer into `graphics/svf/`, build it in every ScummVM build with one set of
compile switches, and wrap it in `Graphics::SvfFace`, `Graphics::SvfChain` and `Graphics::SvfFont` (a `Graphics::Font`),
proven by CxxTest unit tests, golden pixels from the Python reference and a JMAP status test.

**Architecture:** the six library source files (5 `.c` + `svf_internal.h`) and `svf.h` are copied by a sync script and
compiled as C99 by the C++ compiler in C mode (configure sets up no C compiler). `configure` adds
`-DSVF_JMAP=1 -DSVF_JSET_BIG=1 -DSVF_JMAP_RECOG=0` to `DEFINES` (every C and C++ unit). The glue lives in
`graphics/hires_text/` (face, chain) and `graphics/fonts/` (font adapter). Test data (two small faces and four golden
lines) is generated from the submodule by `tools/korean/mkgolden.py` and committed, so tests do not need the submodule.

**Tech stack:** C99 (vendored), C++11 ScummVM code, CxxTest (`make test`), Python 3.12 (generators only).

Source of scope: `docs/korean/DESIGN.md` section 11 row P0, decisions 1-3 (section 12), section 5.2 (glue layers 1-4),
section 9 (vendoring, configure), section 10 (tests), section 13 (the `SVF_JMAP` risk).

---

## 0. Facts checked for this plan (2026-11-22, this worktree)

Tree: `/home/thkim/work/scummvm-2026.4-korean`, branch `ko-2026.4` (`6bc2a0051da`), submodule `dists/svf-fonts-open` at
`3d40de8d940f50ed4a28dfc1851fa776a62f42e5`. No build is configured in the tree (`find . -maxdepth 2 -name config.mk`
finds nothing); build out of tree only.

ScummVM APIs used (file:line in this tree):

| API | Where |
|---|---|
| `Graphics::Font` pure virtuals `getFontHeight` 93, `getMaxCharWidth` 131, `getCharWidth(uint32)` 140, `drawChar(Surface *, uint32, int, int, uint32)` 214; virtuals `getFontName` 98, `getFontAscent` 106, `getFontDescent` 114, `getKerningOffset` 150, `getBoundingBox(uint32)` 168, `drawChar(ManagedSurface *...)` 215, `drawAlphaChar` 242-243; non-virtual `drawString(Surface *, const U32String &, ...)` 264, `getStringWidth` 307/309 | `graphics/font.h` |
| `drawStringImpl` walks the string one element at a time and calls `drawChar` per code point; for `Common::String` the element is a **byte** | `graphics/font.cpp:113-150`, `getStringWidthImpl` 99 |
| default `getBoundingBox(chr)` = `Rect(getCharWidth, getFontHeight)` (kept: a cell-wide box would cut lines short) | `graphics/font.cpp:46-48` |
| `override` on Font subclasses (style) | `graphics/fonts/bdf.h:65-70` |
| `Surface::w/h` (int16) 71/76, `pitch` 83, `format` 95, `getPixels` 110/119, `getBasePtr` 140/152, `create` 207, `free` 217, `fillRect` 498 | `graphics/surface.h` |
| `PixelFormat(bpp, rBits, gBits, bBits, aBits, rShift, gShift, bShift, aShift)` 168, `createFormatCLUT8` 184, `createFormatARGB32` 234, `RGBToColor` 261, `ARGBToColor` 278, `colorToARGB` 309, `isCLUT8` 430 | `graphics/pixelformat.h` |
| `U32String(const char *, CodePage = kUtf8)` 80 | `common/ustr.h` |
| `SeekableReadStream::size/seek/pos`, `read` 417, `err` 61, `clearErr` 71 | `common/stream.h` |
| `MemoryReadStream(const byte *, uint32, DisposeAfterUse::Flag)` 73, `read ... override` (not final) | `common/memstream.h` |
| `Common::NonCopyable` | `common/noncopyable.h:39` |
| `Common::Array::data()` 208/213, `resize` 412 | `common/array.h` |
| C rule `%.o: %.c` uses `$(CC) ... $(CFLAGS) $(CPPFLAGS)`; `CPPFLAGS := $(DEFINES) $(INCLUDES)` | `Makefile.common:172-174`, `:57` |
| configure defines no C compiler ("We don't configure a C compiler, use a C++ one in C mode") | `backends/platform/android/module.mk:15-18` |
| `append_var DEFINES` after the `RELEASE_BUILD` block | `configure:2757-2761` |
| `TESTS :=` list, `TEST_LIBS` (graphics last), `test:` target | `test/module.mk:8-13`, `:41`, `:86` |
| `install_null_g_system()` for tests that need `FSNode`/`DumpFile` | `test/system/null_osystem.cpp`, used by `test/common/encoding.h:20` |
| `Image::writePNG(WriteStream &, const Surface &, const byte *palette, uint count)` | `image/png.h:94` |
| commit subject must start `[A-Z_]+:` | `devtools/check-commit-msg.py` |
| third-party precedent: `graphics/nanosvg/` + its own `LICENSE.txt`; MIT parts listed in `LICENSES/COPYING.MIT` (NanoJPEG section) | tree |

Library facts (`dists/svf-fonts-open/c/include/svf.h`): `svf_face` is 96 bytes on 64-bit and 76 on 32-bit with
`SVF_JMAP=1` (measured: x86_64 gcc 96, mingw64 96, aarch64 96, `-m32` 76, DJGPP 76; 80 on x86_64 with `SVF_JMAP=0`).
`SVF_JMAP_RECOG` is compiled out when `SVF_JMAP` is 1, so `-DSVF_JMAP_RECOG=0` only pins it. `svf_draw.c` gives 4
`-Wpedantic` warnings in C99 (lines 883, 894, 988, 998), hence `-pedantic` is filtered for the C objects.

Measured on this host (32 cores), scratch dirs under `~/work/tmp/`:

| Step | Command | Time |
|---|---|---|
| configure Linux | `$SRC/configure --disable-all-engines --enable-debug --disable-optimizations` | 1.6 s |
| `make test` Linux, fresh | `~/.local/bin/heavy make -j16 test` | 7.5 s wall, 406 tests OK |
| same with ASan | configure `... --enable-asan`, `heavy make -j16 test` | 10.8 s, 406 tests OK |
| full binary (no engines) | `heavy make -j16` after the above | 4.8 s |
| configure mingw | see Task 12 | 1.4 s |
| mingw test runner | `heavy make -j16 test/runner` | 8.4 s; `test/runner.exe` built. **No wine on this host: running it is UNVERIFIED** |

A prototype of every new C/C++ file of this plan was compiled (gcc ASan/UBSan, mingw64, `-m32` syntax) and its 25 tests
passed against a scratch build: `~/work/tmp/scummvm2026-p0-proto/` (graphics/, test/graphics/, tools/korean/). The code
below is that prototype; where the plan and the prototype differ, the plan wins.

## 1. Deviations from DESIGN.md (decided here; listed again in the report)

1. Glue file names are `svfface.{h,cpp}` and `svfchain.{h,cpp}`, not `svf_face.h`: `graphics/libgraphics.a` would hold two
   members named `svf_face.o` (vendored `svf/svf_face.c` and the glue), and `rules.mk:82` archives with `ar cr` into an
   existing archive. GNU ar copes; other `ar`s replace by member name. Header guards follow the path
   (`GRAPHICS_HIRES_TEXT_SVFFACE_H`, `GRAPHICS_FONTS_SVFFONT_H`) as `graphics/fonts/ttf.h:22` does; the vendored `svf.h`
   keeps its own `SVF_H`.
2. The C objects are built with `CC = $(CXX) -x c -std=c99` (target-specific), not by the plain `%.o: %.c` rule the design
   names: that rule's `$(CC)` is make's default `cc`, wrong for every cross build (mingw, DJGPP, Android).
3. The `sizeof(svf_face)` guard is three parts: `static_assert`s on the three switches and on the measured sizes
   (x86_64/aarch64 96, i386 76) in `svfface.h`, plus `graphics/hires_text/svf_abi.c` exporting the C side's
   `sizeof(svf_face)` which `SvfFace`'s constructor asserts and a test checks (catches a C unit built with other switches;
   a fixed size alone would break m68k/ppc ports whose alignment differs).
4. Test fixture: not "a 3-glyph face from `jamoset.py`" but two faces made by the submodule's `tools/svf_synth.py`:
   `--jmap-variant hybrid1 --from svf/dos/NEODG16G.SVF` (14,616 B, 16x16 1 bpp, JSET v3 JMAP + v1 fallback, ASCII,
   U+25A1, all syllables; OFL Neo Dunggeunmo derivative) and `--marks` (196 B, synthetic, no font source). No shipped
   face has a JMAP block (all are JSET v1), and the JMAP face is what the status test needs. Verified: with
   `SVF_JMAP=1` its status is 0; built `SVF_JMAP=0 SVF_JMAP_RECOG=0` it opens with status `0x8` (`JSET_BAD`), no Hangul.
5. Done-criterion "a GUI dialog draws a Korean string on a CLUT8 and an RGB565 overlay (manual)": no SDL overlay is CLUT8
   (the 1-byte overlay of the surface backend is RGB332, `backends/graphics/surfacesdl/surfacesdl-graphics.cpp:1068-1071`).
   CLUT8 and RGB565 are covered by unit tests plus a PNG dump (Task 11); the GUI check is an optional throwaway patch
   (Task 13), result UNVERIFIED until someone runs it.
6. Commit trailers: `AI-GUIDELINES.md` forbids AI co-authorship and asks for `Assisted-by:`; the owner's fork rule
   (DESIGN section 9) accepts agent-written commits, and the caller asked for `Co-Authored-By`. Each commit below carries
   both `Assisted-by: Claude:claude-sonnet-5.5` and the requested `Co-Authored-By:` line. Nothing here goes upstream.

## 2. Files

| File | New/Mod | Purpose |
|---|---|---|
| `tools/korean/sync-svf.sh` | new | copy library from submodule, write `VERSION`; `--check` compares |
| `graphics/svf/{svf.h,svf_internal.h,svf_draw.c,svf_face.c,svf_jamo.c,svf_scale.c,svf_stream.c,LICENSE,VERSION}` | new (generated by sync) | vendored renderer, MIT |
| `graphics/module.mk` | mod | objects + C-mode rule variables |
| `configure` | mod | 3 defines in `DEFINES` |
| `graphics/hires_text/svf_abi.c` | new | C-side `sizeof(svf_face)`, `#error` on wrong switches |
| `graphics/hires_text/svfface.{h,cpp}` | new | `Graphics::SvfFace` |
| `graphics/hires_text/svfchain.{h,cpp}` | new | `Graphics::SvfChain` |
| `graphics/fonts/svffont.{h,cpp}` | new | `Graphics::SvfFont : Graphics::Font` |
| `tools/korean/mkgolden.py` | new | writes the two test data headers; `--check` |
| `test/graphics/svf_fixture.h`, `test/graphics/svf_golden_data.h` | new (generated) | faces as byte arrays, golden rows |
| `test/graphics/svf_test_util.h` | new | chain builder, hex decoder, ASCII art |
| `test/graphics/svf_build.h`, `svf_face.h`, `svf_golden.h`, `svf_font.h` | new | CxxTest suites |
| `test/module.mk` | mod | the four suites (explicit names, not a glob: data headers must not become suites) |
| `LICENSES/COPYING.MIT`, `LICENSES/COPYING.OFL` | mod | MIT section for graphics/svf; OFL note for the fixture |
| `docs/korean/PATCHES.md` | new | upstream files touched by topic `ko/00-svf-lib` (DESIGN section 9) |

Style for every new ScummVM file: the GPL header of `graphics/font.cpp:1-20` verbatim, tabs, `namespace Graphics {` ...
`} // End of namespace Graphics`, members `_camelCase`, methods `getX()`/`isX()`, `Type *name`, short comments
(AI-GUIDELINES: no lengthy explanations). Code blocks below omit the GPL header; add it.

## 3. Environment (every task)

```sh
SRC=/home/thkim/work/scummvm-2026.4-korean
BLD=~/work/tmp/scummvm2026-p0build            # out-of-tree build; never configure inside $SRC
mkdir -p $BLD && cd $BLD && $SRC/configure --disable-all-engines --enable-debug --disable-optimizations
~/.local/bin/heavy make -j16 test             # expected now: "Running cxxtest tests (406 tests)....OK!"
```

Re-run the configure line after any change to `configure`. Commits: subject `SUBSYSTEM: ...` (capitals), body short,
then the two trailer lines:

```
Assisted-by: Claude:claude-sonnet-5.5
Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>
```

Branch (DESIGN section 9): `git -C $SRC switch -c ko/00-svf-lib` from `ko-2026.4` before Task 1, unless the owner says to
commit on `ko-2026.4` directly.

---

## Task 1: Vendor the renderer with a sync script (10 min)

**Files:** create `tools/korean/sync-svf.sh`; generated `graphics/svf/*`.

- [ ] **Step 1: write the script** (exact content; prototype: `~/work/tmp/scummvm2026-p0-proto/tools/korean/sync-svf.sh`)

```sh
#!/bin/sh
# Copies the SVF C renderer from the svf-fonts-open submodule into graphics/svf and records the upstream commit.
#
#   tools/korean/sync-svf.sh            copy the files and rewrite graphics/svf/VERSION
#   tools/korean/sync-svf.sh --check    exit 1 if graphics/svf differs from the submodule's checkout
set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
src="$root/dists/svf-fonts-open"
dst="$root/graphics/svf"
files="c/include/svf.h c/src/svf_internal.h c/src/svf_draw.c c/src/svf_face.c c/src/svf_jamo.c c/src/svf_scale.c c/src/svf_stream.c LICENSE"

[ -f "$src/c/include/svf.h" ] || { echo "sync-svf: $src is empty: run git submodule update --init dists/svf-fonts-open" >&2; exit 1; }
if [ -n "$(git -C "$src" status --porcelain -- c/include c/src LICENSE)" ]; then
	echo "sync-svf: the library in $src has local changes; commit them upstream first" >&2
	exit 1
fi
commit=$(git -C "$src" rev-parse HEAD)
date=$(git -C "$src" log -1 --format=%cI HEAD)

version() {
	echo "svf-fonts-open C renderer (SVFN v3), vendored by tools/korean/sync-svf.sh"
	echo "url: $(git -C "$root" config -f .gitmodules submodule.dists/svf-fonts-open.url)"
	echo "commit: $commit"
	echo "date: $date"
	echo "build: configure adds -DSVF_JMAP=1 -DSVF_JSET_BIG=1 -DSVF_JMAP_RECOG=0 to DEFINES"
	echo "files (sha256, name):"
	for f in $files; do
		echo "$(sha256sum "$src/$f" | cut -d' ' -f1)  $(basename "$f")"
	done
}

case "${1:-}" in
"")
	mkdir -p "$dst"
	for f in $files; do
		cp "$src/$f" "$dst/$(basename "$f")"
	done
	version > "$dst/VERSION"
	echo "sync-svf: graphics/svf is svf-fonts-open $commit"
	;;
--check)
	bad=0
	for f in $files; do
		cmp -s "$src/$f" "$dst/$(basename "$f")" || { echo "sync-svf: graphics/svf/$(basename "$f") differs from $f" >&2; bad=1; }
	done
	version | cmp -s - "$dst/VERSION" || { echo "sync-svf: graphics/svf/VERSION does not name $commit" >&2; bad=1; }
	[ "$bad" = 0 ] && echo "sync-svf: graphics/svf matches svf-fonts-open $commit"
	exit "$bad"
	;;
*)
	echo "usage: $0 [--check]" >&2
	exit 2
	;;
esac
```

- [ ] **Step 2: run it, check, prove the check fails on a change**

```sh
cd $SRC && chmod +x tools/korean/sync-svf.sh && tools/korean/sync-svf.sh --check; echo rc=$?   # rc=1 (no graphics/svf yet)
tools/korean/sync-svf.sh            # "sync-svf: graphics/svf is svf-fonts-open 3d40de8d94..."
tools/korean/sync-svf.sh --check    # "... matches ..."
echo x >> graphics/svf/svf_face.c && tools/korean/sync-svf.sh --check; echo rc=$?   # rc=1, names svf_face.c
tools/korean/sync-svf.sh && ls graphics/svf   # 7 sources + LICENSE + VERSION
```

`VERSION` must list `svf.h` sha256 `c585d3c5...be50` and `svf_face.c` `ca70cf60...68e1` (values at 3d40de8).

- [ ] **Step 3: commit** — `git add tools/korean/sync-svf.sh graphics/svf`

```
GRAPHICS: SVF: Vendor the svf-fonts-open C renderer

graphics/svf holds the SVFN v3 renderer (MIT) at svf-fonts-open 3d40de8,
copied by tools/korean/sync-svf.sh, which records the commit in VERSION
and checks the copy with --check. Not built yet.
```

## Task 2: Build the library in every build (15 min)

**Files:** create `test/graphics/svf_build.h`; modify `test/module.mk`, `graphics/module.mk`, `configure`.

- [ ] **Step 1: failing test** `test/graphics/svf_build.h`

```cpp
#include <cxxtest/TestSuite.h>

#include "graphics/svf/svf.h"

class SvfBuildTestSuite : public CxxTest::TestSuite {
public:
	void test_switches() {
		TS_ASSERT_EQUALS(SVF_JMAP, 1);
		TS_ASSERT_EQUALS(SVF_JSET_BIG, 1);
		TS_ASSERT_EQUALS(SVF_JMAP_RECOG, 0);
	}

	void test_linked() {
		const uint16_t *set = nullptr;
		TS_ASSERT_EQUALS(svf_layouts(&set), 0u);	// every layout is read
		TS_ASSERT_EQUALS(svf_bpp(nullptr), 0);
	}
};
```

In `test/module.mk` extend `TESTS` (line 8-13) with one new line after `$(srcdir)/test/image/*.h \`:

```make
	$(srcdir)/test/image/*.h \
	$(srcdir)/test/graphics/svf_build.h
```

(the previous last line gets a trailing ` \`).

- [ ] **Step 2: run, expect failure** — `cd $BLD && heavy make -j16 test` fails: link errors `undefined reference to svf_layouts` / `svf_bpp`.

- [ ] **Step 3: build rules** — in `graphics/module.mk`, after the closing `yuv_to_rgb.o` of `MODULE_OBJS` (line 72) add:

```make

# The SVF renderer of svf-fonts-open (graphics/svf, synced by tools/korean/sync-svf.sh) is C99.
# configure sets up no C compiler, so the C++ compiler builds it in C mode.
SVF_OBJS := \
	svf/svf_draw.o \
	svf/svf_face.o \
	svf/svf_jamo.o \
	svf/svf_scale.o \
	svf/svf_stream.o

MODULE_OBJS += $(SVF_OBJS)

SVF_C_OBJS := $(addprefix $(MODULE)/, $(SVF_OBJS))
$(SVF_C_OBJS): CC = $(CXX) -x c -std=c99
$(SVF_C_OBJS): CFLAGS = $(filter-out -std=% -pedantic -fno-operator-names -Wnon-virtual-dtor -Woverloaded-virtual \
	-Wno-reorder -fno-exceptions -fcheck-new -fno-rtti -Wno-inconsistent-missing-override -fvisibility-inlines-hidden, \
	$(CXXFLAGS))
```

(`SVF_C_OBJS` is reused in Task 5 for `svf_abi.o`. `-x c` must come before the source: it does, `CC` is first on the
`Makefile.common:174` command line. `vpath %.c $(srcdir)` in the generated out-of-tree Makefile finds the sources.)

- [ ] **Step 4: configure** — after the `RELEASE_BUILD` block (`configure:2757-2761`) insert:

```sh

# The SVF renderer (graphics/svf): one set of switches for every C and C++ unit,
# since SVF_JMAP changes the layout of svf_face.
append_var DEFINES "-DSVF_JMAP=1 -DSVF_JSET_BIG=1 -DSVF_JMAP_RECOG=0"
```

- [ ] **Step 5: run, expect pass**

```sh
cd $BLD && $SRC/configure --disable-all-engines --enable-debug --disable-optimizations >/dev/null
grep -c 'SVF_JMAP=1' config.mk                       # 1
heavy make -j16 test 2>&1 | grep -E '^ +C +graphics/svf|Running'
# "    C       graphics/svf/svf_draw.o" ... (5 lines), "Running cxxtest tests (408 tests)...OK!"
make -n -B graphics/svf/svf_face.o | tail -1          # starts "g++ -x c -std=c99", has -DSVF_JMAP=1, no -pedantic
```

Expected: no warning from `graphics/svf/*.c`. If the runner link fails with undefined `Common::` symbols referenced
from `libgraphics.a` (seen in a minimal runner: `Common::memset16`), append ` common/libcommon.a` at the end of the
`TEST_LIBS +=` line (`test/module.mk:41`) and record it in PATCHES.md.

- [ ] **Step 6: commit** — `git add configure graphics/module.mk test/module.mk test/graphics/svf_build.h`

```
GRAPHICS: SVF: Build the vendored renderer

The five C files are compiled by the C++ compiler in C99 mode, and
configure adds -DSVF_JMAP=1 -DSVF_JSET_BIG=1 -DSVF_JMAP_RECOG=0 to
DEFINES so that every unit sees the same svf_face layout.
```

## Task 3: Licences and the patch list (10 min)

**Files:** modify `LICENSES/COPYING.MIT`, `LICENSES/COPYING.OFL`; create `docs/korean/PATCHES.md`.

- [ ] **Step 1: `LICENSES/COPYING.MIT`** — append, in the style of the NanoJPEG section (blank lines between sections):

```



ScummVM uses the SVF bitmap font renderer of svf-fonts-open (graphics/svf/)
published under the terms of the MIT license.

Copyright (c) 2026 Taehyun Kim
```

followed by the MIT permission and warranty paragraphs exactly as in `graphics/svf/LICENSE` (lines 5-21).

- [ ] **Step 2: `LICENSES/COPYING.OFL`** — the NOTE at the top lists files the licence applies to. Add a bullet after the
list: ` * test/graphics/svf_fixture.h (kSvfFixtureJamo: font data derived from Neo Dunggeunmo, Copyright (c) 2017-2021,
Eunbin Jeong (Dalgona.); a test fixture, not shipped)`. Read the NOTE first and keep its wording style ("fillowing" typo
is upstream's; do not fix it).

- [ ] **Step 3: `docs/korean/PATCHES.md`** — a table `| Topic | Upstream file | Change | Why |` with the rows:
`ko/00-svf-lib` / `configure` / one `append_var DEFINES` after RELEASE_BUILD / svf_face layout in every unit;
`graphics/module.mk` / SVF objects + C-mode variables / build the renderer; `test/module.mk` / four suites (+ libcommon
if Task 2 needed it) / tests; `LICENSES/COPYING.MIT`, `LICENSES/COPYING.OFL` / notices. New files are not listed
(they never conflict).

- [ ] **Step 4: commit** — `git add LICENSES docs/korean/PATCHES.md`

```
LICENSES: Add the SVF renderer and the SVF test fixture

graphics/svf is MIT; the jamo test face of test/graphics is OFL font
data. docs/korean/PATCHES.md starts the list of upstream files the
Korean topics modify.
```

### Review checkpoint A (after Task 3)
Run superpowers:requesting-code-review on Tasks 1-3: script idempotent, `--check` exact, no warnings from the C
objects, configure change minimal, licence texts verbatim.

## Task 4: Test data generator and data (15 min)

**Files:** create `tools/korean/mkgolden.py`; generated `test/graphics/svf_fixture.h` (about 92 KB),
`test/graphics/svf_golden_data.h` (about 19 KB).

- [ ] **Step 1: write `tools/korean/mkgolden.py`** — copy `~/work/tmp/scummvm2026-p0-proto/tools/korean/mkgolden.py`
(verified: generates both files, `--check` passes, deterministic). Its contract, if rewriting:
  - `ROOT` = repository root (two levels above the script); imports `svfline` from `dists/svf-fonts-open/tools`.
  - Faces: in a `tempfile.TemporaryDirectory`, run `sys.executable dists/svf-fonts-open/tools/svf_synth.py
    --jmap-variant hybrid1 --from dists/svf-fonts-open/svf/dos/NEODG16G.SVF JAMO.SVF` and `svf_synth.py --marks
    MARKS.SVF` (results: 14,616 B sha256 `b5c7327efd597333...`, 196 B sha256 `6339e4e6de143904...`).
  - `svf_fixture.h`: GPL header, `// Generated by tools/korean/mkgolden.py from dists/svf-fonts-open <commit>. Do not
    edit.`, guard `TEST_GRAPHICS_SVF_FIXTURE_H`, `#include "common/scummsys.h"`, then per face a comment (size, sha256,
    origin; for the jamo face the OFL notice: "a Modified Version of Neo Dunggeunmo, Copyright (c) 2017-2021, Eunbin Jeong
    (Dalgona.) <project-neodgm@dalgona.dev>, SIL Open Font License 1.1 (LICENSES/COPYING.OFL; Reserved Font Names: see
    dists/svf-fonts-open/svf/dos/NEODGM.TXT)"; for marks: "synthetic glyphs drawn by the tool, no font source") and
    `static const byte kSvfFixtureMarks[196] = { 0x53, ... };` / `kSvfFixtureJamo[14616]`, 16 bytes a line.
  - `svf_golden_data.h`: guard `TEST_GRAPHICS_SVF_GOLDEN_DATA_H`; chain `[MARKS, JAMO]`; for each case
    `svfline.layout(chain, text, x, y, w, h, missing_advance=m, scale=s)` and `svfline.measure(...)`, emitted as
    `kSvfGolden<Name>Text[]` (C string, non-ASCII as `"\xNN"""` escapes closed after each byte), `<Name>X, Y, W, H`,
    `ScaleNum, ScaleDen, Missing`, `Width, Ascent, Descent`, `Rows[H]` (one string a row, two lowercase hex digits a pixel):

| Name | Text (UTF-8) | x | y (baseline) | w x h | scale | missing |
|---|---|---|---|---|---|---|
| `Line1x` | `가한글 ab` + `e` U+0301 + byte `0xFF` + U+2603 + `z` | 2 | 14 | 112 x 20 | 1/1 | 6 |
| `Line2x` | `한a` | 2 | 26 | 56 x 36 | 2/1 | 0 |
| `Line3by2` | `한a` | 2 | 20 | 40 x 28 | 3/2 | 0 |
| `Font1x` | `가한글 abe` U+0301 `z` | 2 | 14 | 112 x 20 | 1/1 | 0 |

  Baselines equal `top + chain ascent` (1x: max(13, 12) = 13 with top 1; 2x: 26; 3/2: S(13) = 20) so `SvfFont` at
  top `y - ascent` reproduces them. `가` (U+AC00) is answered by the JMAP block, `한글` by the v1 fallback, `0xFF` draws the
  marks face's U+FFFD, U+2603 is missing (advance 6), U+0301 is a mark over `e`.
  - `--check`: regenerate in memory, exit 1 naming each differing file. `--root DIR` writes elsewhere (for trials).

- [ ] **Step 2: generate and check**

```sh
cd $SRC && python3 tools/korean/mkgolden.py          # "wrote test/graphics/svf_fixture.h", "... svf_golden_data.h"
python3 tools/korean/mkgolden.py --check; echo rc=$?  # rc=0
grep -c '' test/graphics/svf_fixture.h               # about 960 lines
grep -n 'Width = ' test/graphics/svf_golden_data.h   # Line1x 105/13/4, Line2x 48, Line3by2 36, Font1x ...
```

- [ ] **Step 3: commit** — `git add tools/korean/mkgolden.py test/graphics/svf_fixture.h test/graphics/svf_golden_data.h`

```
TEST: Add SVF test faces and golden lines

tools/korean/mkgolden.py makes two small faces with the submodule's
svf_synth.py (a JMAP jamo face derived from the OFL Neo Dunggeunmo face
NEODG16G, and a synthetic marks face) and draws four golden lines with
its Python reference svfline.py. The results are committed so that the
tests do not need the submodule; --check regenerates and compares.
```

## Task 5: `SvfFace`, mapped faces (20 min)

**Files:** create `graphics/hires_text/svf_abi.c`, `graphics/hires_text/svfface.{h,cpp}`, `test/graphics/svf_face.h`;
modify `graphics/module.mk`, `test/module.mk`.

- [ ] **Step 1: failing tests** — `test/graphics/svf_face.h` (prototype has the full file): suite `SvfFaceTestSuite`
with, for now, `test_abi`, `test_jmapFaceOpensClean`, `test_marksFace`, `test_refusals`, `test_loadWhole`:

```cpp
#include <cxxtest/TestSuite.h>

#include "common/memstream.h"

#include "graphics/hires_text/svfface.h"

#include "test/graphics/svf_fixture.h"

class SvfFaceTestSuite : public CxxTest::TestSuite {
public:
	void test_abi() {
		TS_ASSERT_EQUALS(scummvm_svf_face_size, sizeof(svf_face));
	}

	// DESIGN section 13: a JMAP face must open with status 0 in this build.
	void test_jmapFaceOpensClean() {
		Graphics::SvfFace face;
		TS_ASSERT(face.loadFromMemory(kSvfFixtureJamo, sizeof(kSvfFixtureJamo), "jamo"));
		TS_ASSERT_EQUALS(face.getError(), SVF_OK);
		TS_ASSERT_EQUALS(face.getStatus(), 0);
		TS_ASSERT(face.isJamo());
		TS_ASSERT(face.has(0xAC00));	// listed by the JMAP block
		TS_ASSERT(face.has(0xD55C));	// answered by the fallback JSET
		TS_ASSERT(face.has('a'));
		TS_ASSERT(!face.has(0x2603));
		TS_ASSERT_EQUALS(face.getCellWidth(), 16u);
		TS_ASSERT_EQUALS(face.getCellHeight(), 16u);
		TS_ASSERT_EQUALS(face.getAscent(), 12u);
		TS_ASSERT_EQUALS(face.getBpp(), 1u);
		TS_ASSERT_EQUALS(face.getCellBytes(), 32u);
		TS_ASSERT(!face.isStreamed());
	}

	void test_marksFace() {
		Graphics::SvfFace face;
		TS_ASSERT(face.loadFromMemory(kSvfFixtureMarks, sizeof(kSvfFixtureMarks), "marks"));
		TS_ASSERT_EQUALS(face.getStatus(), 0);
		TS_ASSERT(!face.isJamo());
		TS_ASSERT(face.has(0x0301));
		TS_ASSERT(face.has(0xFFFD));
	}

	void test_refusals() {
		Graphics::SvfFace face;
		TS_ASSERT(!face.loadFromMemory(kSvfFixtureJamo, 100, "cut"));
		TS_ASSERT_DIFFERS(face.getError(), SVF_OK);
		TS_ASSERT(!face.isOpen());
		byte v2[sizeof(kSvfFixtureMarks)];
		memcpy(v2, kSvfFixtureMarks, sizeof(v2));
		v2[4] = 2;	// header version byte
		TS_ASSERT(!face.loadFromMemory(v2, sizeof(v2), "v2"));
		TS_ASSERT_EQUALS(face.getError(), SVF_E_VERSION);
	}

	void test_loadWhole() {
		Common::MemoryReadStream stream(kSvfFixtureJamo, sizeof(kSvfFixtureJamo));
		Graphics::SvfFace face;
		TS_ASSERT(face.loadWhole(stream, "jamo"));
		TS_ASSERT_EQUALS(face.getStatus(), 0);
	}
};
```

Add `$(srcdir)/test/graphics/svf_face.h` to `TESTS`. Run `heavy make -j16 test`: fails to compile
(`graphics/hires_text/svfface.h: No such file`).

- [ ] **Step 2: `graphics/hires_text/svf_abi.c`**

```c
/* sizeof(svf_face) as the C library sees it; SvfFace compares it with its own (svfface.h). */
#include "graphics/svf/svf.h"

#if !SVF_JMAP || !SVF_JSET_BIG || SVF_JMAP_RECOG
#error "graphics/svf: configure must add -DSVF_JMAP=1 -DSVF_JSET_BIG=1 -DSVF_JMAP_RECOG=0 to DEFINES"
#endif

const size_t scummvm_svf_face_size = sizeof(svf_face);
```

In `graphics/module.mk`: add `hires_text/svf_abi.o` to `SVF_OBJS` (it is C), and `hires_text/svfface.o` to
`MODULE_OBJS` after `hotspot_renderer.o` (alphabetical).

- [ ] **Step 3: `graphics/hires_text/svfface.h`**

```cpp
#ifndef GRAPHICS_HIRES_TEXT_SVFFACE_H
#define GRAPHICS_HIRES_TEXT_SVFFACE_H

#include "common/array.h"
#include "common/noncopyable.h"
#include "common/str.h"
#include "common/types.h"

#include "graphics/svf/svf.h"

namespace Common {
class SeekableReadStream;
}

// configure adds these switches to DEFINES, so every C and C++ unit sees one svf_face layout.
static_assert(SVF_JMAP == 1, "graphics/svf needs -DSVF_JMAP=1: re-run configure");
static_assert(SVF_JSET_BIG == 1, "graphics/svf needs -DSVF_JSET_BIG=1: re-run configure");
static_assert(SVF_JMAP_RECOG == 0, "graphics/svf needs -DSVF_JMAP_RECOG=0: re-run configure");
#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) || defined(_M_ARM64)
static_assert(sizeof(svf_face) == 96, "svf_face is not the SVF_JMAP layout");
#elif defined(__i386__) || defined(_M_IX86)
static_assert(sizeof(svf_face) == 76, "svf_face is not the SVF_JMAP layout");
#endif

extern "C" const size_t scummvm_svf_face_size;

namespace Graphics {

/**
 * One SVF face (svf-fonts-open, SVFN v3) opened with the renderer in graphics/svf: read whole, used in
 * place, or streamed (tables read at open, glyph rows through a block cache). It must not move while
 * open: never keep one by value in a container.
 */
class SvfFace : Common::NonCopyable {
public:
	enum {
		kDefaultWholeLimit = 256 * 1024,
		kMaxWholeSize = 16 * 1024 * 1024,
		kDefaultBlockSize = 1024,
		kDefaultSlots = 64
	};

	SvfFace();
	~SvfFace();

	/** Opens @p data in place; it must stay valid and unchanged while the face is open. */
	bool loadFromMemory(const byte *data, uint32 size, const Common::String &name);
	/** Reads the whole stream (at most kMaxWholeSize bytes) and opens the copy. */
	bool loadWhole(Common::SeekableReadStream &stream, const Common::String &name);
	/** Streams the face; with DisposeAfterUse::YES the stream is deleted on close(), also when this fails. Below 2 slots: no cache. */
	bool loadStreamed(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose, const Common::String &name,
	                  uint16 blockSize = kDefaultBlockSize, uint16 slots = kDefaultSlots);
	/** loadWhole() up to @p wholeLimit bytes, else loadStreamed() with the defaults. */
	bool load(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose, const Common::String &name,
	          uint32 wholeLimit = kDefaultWholeLimit);
	void close();

	bool isOpen() const { return _open; }
	bool isStreamed() const { return _stream != nullptr; }
	svf_err getError() const { return _error; }
	const Common::String &getName() const { return _name; }

	uint16 getStatus() const { return svf_status(&_face); }
	bool isJamo() const { return svf_is_jamo(&_face) != 0; }
	bool has(uint32 cp) const { return svf_has(&_face, cp) != 0; }
	uint getBpp() const { return svf_bpp(&_face); }
	uint getCellWidth() const { return svf_cell_w(&_face); }
	uint getCellHeight() const { return svf_cell_h(&_face); }
	uint getAscent() const { return svf_ascent(&_face); }
	uint getCellBytes() const { return (uint)svf_cell_bytes(&_face); }
	uint32 getStreamReads() const { return svf_stream_reads(&_face); }

	svf_face *raw() { return &_face; }
	const svf_face *raw() const { return &_face; }

private:
	bool finishOpen(svf_err err);
	static long readCallback(void *ctx, uint32_t offset, uint8_t *dst, uint32_t len);

	svf_face _face;
	bool _open;
	svf_err _error;
	Common::String _name;
	Common::Array<byte> _data;
	Common::Array<byte> _tables;
	Common::Array<byte> _cache;
	Common::SeekableReadStream *_stream;
	DisposeAfterUse::Flag _disposeStream;
};

} // End of namespace Graphics

#endif
```

- [ ] **Step 4: `graphics/hires_text/svfface.cpp`** (mapped part; `loadStreamed`/`load`/`readCallback` in Task 6 —
for now `loadStreamed` and `load` may `return finishOpen(SVF_E_ARG);`)

```cpp
#include "graphics/hires_text/svfface.h"

#include "common/stream.h"
#include "common/textconsole.h"

namespace Graphics {

SvfFace::SvfFace() : _open(false), _error(SVF_E_ARG), _stream(nullptr), _disposeStream(DisposeAfterUse::NO) {
	memset(&_face, 0, sizeof(_face));
	assert(scummvm_svf_face_size == sizeof(svf_face));
}

SvfFace::~SvfFace() {
	close();
}

void SvfFace::close() {
	memset(&_face, 0, sizeof(_face));
	_open = false;
	_data.clear();
	_tables.clear();
	_cache.clear();
	if (_stream && _disposeStream == DisposeAfterUse::YES)
		delete _stream;
	_stream = nullptr;
	_disposeStream = DisposeAfterUse::NO;
}

bool SvfFace::loadFromMemory(const byte *data, uint32 size, const Common::String &name) {
	close();
	_name = name;
	return finishOpen(svf_open_mapped(&_face, data, size));
}

bool SvfFace::loadWhole(Common::SeekableReadStream &stream, const Common::String &name) {
	close();
	_name = name;
	const int64 size = stream.size();
	if (size <= 0 || size > kMaxWholeSize)
		return finishOpen(size <= 0 ? SVF_E_IO : SVF_E_NOMEM);
	_data.resize((uint)size);
	if (!stream.seek(0) || stream.read(_data.data(), (uint32)size) != (uint32)size)
		return finishOpen(SVF_E_IO);
	return finishOpen(svf_open_mapped(&_face, _data.data(), _data.size()));
}

bool SvfFace::finishOpen(svf_err err) {
	_error = err;
	if (err != SVF_OK) {
		if (err == SVF_E_VERSION)
			warning("SVF %s: not an SVFN version 3 face (convert version 2 with tools/svf2to3.py)", _name.c_str());
		else
			warning("SVF %s: cannot open the face (svf_err %d)", _name.c_str(), (int)err);
		close();
		return false;
	}
	_open = true;
	const uint16 status = svf_status(&_face);
	if (status & (SVF_FACE_JSET_BAD | SVF_FACE_JSET_UNREAD | SVF_FACE_JMAP_BAD | SVF_FACE_JMAP_UNREAD))
		warning("SVF %s: its jamo block is not read (status 0x%04x): Hangul syllables are not composed", _name.c_str(), status);
	return true;
}

} // End of namespace Graphics
```

`finishOpen` is the one place status is reported (once per open, DESIGN section 5.2).

- [ ] **Step 5: run** — `heavy make -j16 test`: `Running cxxtest tests (413 tests)...OK!` (406 + 2 + 5).

- [ ] **Step 6: commit** — `git add graphics/hires_text graphics/module.mk test/module.mk test/graphics/svf_face.h`

```
GRAPHICS: Add SvfFace, an SVF face opened from memory or a stream

SvfFace owns an svf_face and its buffers. svf_abi.c exports the C
side's sizeof(svf_face), which SvfFace and a test compare with the C++
side's; svfface.h refuses to build with other renderer switches. A face
whose jamo block is not read is reported once when it opens.
```

## Task 6: `SvfFace`, streamed faces (15 min)

- [ ] **Step 1: failing tests** — add to `SvfFaceTestSuite` (plus the helper class before the suite):

```cpp
namespace {

class SvfFlakyStream : public Common::MemoryReadStream {
public:
	SvfFlakyStream(const byte *data, uint32 size) : Common::MemoryReadStream(data, size), fail(false) {}
	uint32 read(void *dataPtr, uint32 dataSize) override {
		return fail ? 0 : Common::MemoryReadStream::read(dataPtr, dataSize);
	}
	bool fail;
};

} // End of anonymous namespace
```

```cpp
	void test_loadStreamed() {
		Graphics::SvfFace face;
		TS_ASSERT(face.loadStreamed(new Common::MemoryReadStream(kSvfFixtureJamo, sizeof(kSvfFixtureJamo)),
		                            DisposeAfterUse::YES, "jamo", 1024, 8));
		TS_ASSERT(face.isStreamed());
		TS_ASSERT_EQUALS(face.getStatus(), 0);
		TS_ASSERT(face.isJamo());
		TS_ASSERT(face.getStreamReads() > 0);
	}

	void test_loadPicksMappedOrStreamed() {
		Graphics::SvfFace small, big;
		TS_ASSERT(small.load(new Common::MemoryReadStream(kSvfFixtureJamo, sizeof(kSvfFixtureJamo)), DisposeAfterUse::YES, "a"));
		TS_ASSERT(!small.isStreamed());
		TS_ASSERT(big.load(new Common::MemoryReadStream(kSvfFixtureJamo, sizeof(kSvfFixtureJamo)), DisposeAfterUse::YES, "b", 4096));
		TS_ASSERT(big.isStreamed());
	}

	void test_streamReadFailure() {
		SvfFlakyStream stream(kSvfFixtureJamo, sizeof(kSvfFixtureJamo));
		Graphics::SvfFace face;
		TS_ASSERT(face.loadStreamed(&stream, DisposeAfterUse::NO, "flaky", 1024, 0));
		stream.fail = true;
		byte cell[64];
		TS_ASSERT_EQUALS(svf_glyph(face.raw(), 'a', cell, sizeof(cell)), SVF_E_IO);
	}
```

Run: the three fail (stubs).

- [ ] **Step 2: implement** in `svfface.cpp`:

```cpp
bool SvfFace::loadStreamed(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose, const Common::String &name,
                           uint16 blockSize, uint16 slots) {
	close();
	_name = name;
	if (!stream)
		return finishOpen(SVF_E_ARG);
	_stream = stream;
	_disposeStream = dispose;
	const int64 size = stream->size();
	if (size <= 0 || size > (int64)0xFFFFFFFF)
		return finishOpen(SVF_E_RANGE);

	svf_err err = SVF_OK;
	const size_t need = svf_stream_tables_size(readCallback, _stream, (uint32)size, &err);
	if (need == 0)
		return finishOpen(err != SVF_OK ? err : SVF_E_NOMEM);
	_tables.resize(need);
	if (slots >= 2)
		_cache.resize((uint)slots * ((uint)blockSize + 8));
	err = svf_open_stream(&_face, readCallback, _stream, (uint32)size, _tables.data(), _tables.size(),
	                      _cache.empty() ? nullptr : _cache.data(), _cache.size(), blockSize);
	return finishOpen(err);
}

bool SvfFace::load(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose, const Common::String &name,
                   uint32 wholeLimit) {
	if (stream && stream->size() > 0 && stream->size() <= (int64)wholeLimit) {
		const bool ok = loadWhole(*stream, name);
		if (dispose == DisposeAfterUse::YES)
			delete stream;
		return ok;
	}
	return loadStreamed(stream, dispose, name);
}

long SvfFace::readCallback(void *ctx, uint32_t offset, uint8_t *dst, uint32_t len) {
	Common::SeekableReadStream *stream = (Common::SeekableReadStream *)ctx;
	if (len > 0x7FFFFFFF || !stream->seek(offset))	// long is 32 bits on Windows
		return -1;
	const uint32 got = stream->read(dst, len);
	if (stream->err()) {
		stream->clearErr();
		return -1;
	}
	return (long)got;
}
```

(`svf_read_fn` uses `uint32_t`/`uint8_t`: keep those exact types in the callback; `uint32` is not `uint32_t` on every
platform.)

- [ ] **Step 3: run** — 416 tests OK.

- [ ] **Step 4: commit**

```
GRAPHICS: SvfFace: Stream large faces through a block cache

Faces up to 256 KB are read whole, larger ones are streamed with 1 KB
blocks and 64 cache slots. A failed read reaches the renderer as an
error for the glyph being drawn.
```

## Task 7: `SvfChain`, metrics and measuring (15 min)

**Files:** create `graphics/hires_text/svfchain.{h,cpp}`, `test/graphics/svf_test_util.h`, `test/graphics/svf_golden.h`;
modify `graphics/module.mk` (`hires_text/svfchain.o`), `test/module.mk` (`svf_golden.h`).

- [ ] **Step 1: test helper** `test/graphics/svf_test_util.h` (guard `TEST_GRAPHICS_SVF_TEST_UTIL_H`; copy the
prototype): `SvfTest::makeChain(uint num = 1, uint den = 1, uint8 missing = 0, bool streamJamo = false, uint16 slots = 8)`
returns a new chain `{ marks (mapped), jamo (mapped, or streamed with `slots` block cache slots) }` with both faces
owned (`DisposeAfterUse::YES`), then `setScale(num, den)`, `setMissingAdvance(missing)`;
`SvfTest::goldenBytes(rows, w, h)` decodes the hex rows into a `Common::Array<byte>`;
`SvfTest::asciiArt(px, w, h)` returns rows of `.` (0), `#` (255), `+` (other) for `TS_TRACE`.

- [ ] **Step 2: failing test** — `test/graphics/svf_golden.h`, suite `SvfGoldenTestSuite`, first only:

```cpp
	void test_measure() {
		Graphics::SvfChain *chain = SvfTest::makeChain(1, 1, kSvfGoldenLine1xMissing);
		svf_extent e;
		TS_ASSERT_EQUALS(chain->measure(kSvfGoldenLine1xText, strlen(kSvfGoldenLine1xText), e), SVF_OK);
		TS_ASSERT_EQUALS(e.width, kSvfGoldenLine1xWidth);
		TS_ASSERT_EQUALS(e.ascent, kSvfGoldenLine1xAscent);
		TS_ASSERT_EQUALS(e.descent, kSvfGoldenLine1xDescent);
		TS_ASSERT_EQUALS(chain->getAscent(), 13);
		TS_ASSERT_EQUALS(chain->getDescent(), 4);
		delete chain;
		chain = SvfTest::makeChain(3, 2);
		TS_ASSERT_EQUALS(chain->getWidth(kSvfGoldenLine3by2Text, strlen(kSvfGoldenLine3by2Text)), kSvfGoldenLine3by2Width);
		TS_ASSERT_EQUALS(chain->getAscent(), 20);
		TS_ASSERT(!chain->setScale(0, 1));
		TS_ASSERT(!chain->setScale(17, 1));
		delete chain;
	}
```

- [ ] **Step 3: `graphics/hires_text/svfchain.h`**

```cpp
#ifndef GRAPHICS_HIRES_TEXT_SVFCHAIN_H
#define GRAPHICS_HIRES_TEXT_SVFCHAIN_H

#include "common/array.h"
#include "common/noncopyable.h"
#include "common/rect.h"
#include "common/types.h"

#include "graphics/hires_text/svfface.h"

namespace Graphics {

/**
 * A fallback chain of SvfFaces drawn by svf_draw_utf8: each code point comes from the first face that
 * has it. Owns the scratch cell and the optional composed-syllable cache. Text is UTF-8. One chain is
 * not drawn from two threads at once.
 */
class SvfChain : Common::NonCopyable {
public:
	typedef void (*PlotFunc)(void *ctx, int16_t x, int16_t y, uint8_t coverage);

	SvfChain();
	~SvfChain();

	/** Appends an open face (at most 255); deleted with the chain when @p dispose is YES. */
	bool addFace(SvfFace *face, DisposeAfterUse::Flag dispose = DisposeAfterUse::NO);
	uint getFaceCount() const { return _faces.size(); }
	SvfFace *getFace(uint i) const { return _faces[i]; }

	void setMissingAdvance(uint8 advance);
	/** num/den each 1..16, kept reduced. */
	bool setScale(uint num, uint den);
	uint getScaleNum() const { return _chain.scale_num ? _chain.scale_num : 1; }
	uint getScaleDen() const { return _chain.scale_den ? _chain.scale_den : 1; }
	/** A composed-syllable cache of @p slots cells (0: none). */
	bool setSyllableCache(uint slots);

	int getAscent() const;	// largest scaled ascent of the faces
	int getDescent() const;	// largest scaled cell height minus scaled ascent
	int getMaxCellWidth() const;
	bool has(uint32 cp) const;

	svf_err measure(const char *text, uint32 len, svf_extent &extent) const;
	int getWidth(const char *text, uint32 len) const;

	svf_err draw(const char *text, uint32 len, int x, int yBaseline, svf_target &target) const;
	/** Draws through SVF_T_COVERAGE_CB on a w x h area, clipped to @p clip (nullptr: the whole area). */
	svf_err drawCoverage(const char *text, uint32 len, int x, int yBaseline, int w, int h, PlotFunc plot, void *ctx,
	                     const Common::Rect *clip = nullptr) const;

	const svf_chain *raw() const { return &_chain; }

	static int scaleLength(int v, uint num, uint den);	// svf.h "Lengths": round half up, symmetric
	static int scaleDimension(int v, uint num, uint den);	// ceil(v * num / den)

private:
	void update();

	Common::Array<SvfFace *> _faces;
	Common::Array<DisposeAfterUse::Flag> _dispose;
	Common::Array<svf_face *> _rawFaces;
	svf_chain _chain;
	uint _cacheSlots;
	Common::Array<byte> _syllableCache;
	mutable Common::Array<byte> _scratch;
};

} // End of namespace Graphics

#endif
```

- [ ] **Step 4: `svfchain.cpp`** — prototype file in full; the parts that matter:

```cpp
void SvfChain::update() {
	_chain.faces = _rawFaces.empty() ? nullptr : _rawFaces.data();
	_chain.count = (uint8)_rawFaces.size();
	_scratch.resize((uint)svf_chain_scratch_bytes(&_chain));

	_syllableCache.clear();
	if (_cacheSlots && !_rawFaces.empty()) {
		uint cell = 0;
		for (uint i = 0; i < _faces.size(); ++i)
			cell = MAX(cell, _faces[i]->getCellBytes());
		_syllableCache.resize(_cacheSlots * (8 + cell));
		if (svf_chain_cache(&_chain, _syllableCache.data(), _syllableCache.size()) != SVF_OK)
			_syllableCache.clear();
	} else {
		svf_chain_cache(&_chain, nullptr, 0);
	}
}
```

`update()` runs after `addFace`, `setScale`, `setSyllableCache` (the renderer requires `svf_chain_cache` again after the
faces change, and the scratch size depends on faces and scale). `addFace` rejects a null or closed face and a 256th face.
`setScale` reduces by the gcd (a local `gcd`) and returns false outside 1..16. `scaleLength(v)` =
`(v * num + den / 2) / den` (negative: `-scaleLength(-v)`), `scaleDimension(v)` = `(v * num + den - 1) / den`.
`getAscent` = max over faces of `scaleLength(face ascent)`; `getDescent` = max of `scaleDimension(cell_h) -
scaleLength(ascent)`; `getMaxCellWidth` = max of `scaleLength(missing_advance)` and `scaleDimension(cell_w)`.
`measure` = `svf_measure_utf8`; `getWidth` = its `width` or 0. The destructor zeroes `_chain` and deletes owned faces.

- [ ] **Step 5: run** (after adding `hires_text/svfchain.o` and `svf_golden.h`) — 417 tests OK. **Commit**

```
GRAPHICS: Add SvfChain, a fallback chain of SVF faces

The chain owns the renderer's scratch cell and an optional composed-
syllable cache, and gives line metrics at the chain's scale.
```

## Task 8: `SvfChain` drawing against the golden lines (15 min)

- [ ] **Step 1: failing tests** — in `SvfGoldenTestSuite` add a private helper and the cases:

```cpp
#define SVF_GOLDEN(p) p##Text, p##X, p##Y, p##W, p##H, p##ScaleNum, p##ScaleDen, p##Missing, p##Rows

	void checkGray8(const char *text, int x, int y, int w, int h, int num, int den, int missing, const char *const *rows,
	                bool streamed = false, uint cacheSlots = 0, const Common::Rect *clip = nullptr) {
		Graphics::SvfChain *chain = SvfTest::makeChain(num, den, missing, streamed);
		chain->setSyllableCache(cacheSlots);
		Common::Array<byte> buf;
		buf.resize(w * h);
		memset(buf.data(), 0, buf.size());
		svf_target t;
		TS_ASSERT_EQUALS(svf_target_init(&t, SVF_T_GRAY8, buf.data(), buf.size(), w, h, w), SVF_OK);
		if (clip) {
			t.clip_x0 = clip->left;
			t.clip_y0 = clip->top;
			t.clip_x1 = clip->right;
			t.clip_y1 = clip->bottom;
		}
		TS_ASSERT_EQUALS(chain->draw(text, strlen(text), x, y, t), SVF_OK);
		if (cacheSlots)	// a second, cached draw must not change a pixel
			TS_ASSERT_EQUALS(chain->draw(text, strlen(text), x, y, t), SVF_OK);
		Common::Array<byte> expected = SvfTest::goldenBytes(rows, w, h);
		for (int yy = 0; clip && yy < h; ++yy)
			for (int xx = 0; xx < w; ++xx)
				if (!clip->contains(xx, yy))
					expected[yy * w + xx] = 0;
		const bool same = memcmp(buf.data(), expected.data(), buf.size()) == 0;
		TS_ASSERT(same);
		if (!same) {
			TS_TRACE(("got" + SvfTest::asciiArt(buf.data(), w, h)).c_str());
			TS_TRACE(("expected" + SvfTest::asciiArt(expected.data(), w, h)).c_str());
		}
		delete chain;
	}

public:
	void test_line1x() { checkGray8(SVF_GOLDEN(kSvfGoldenLine1x)); }
	void test_line2x() { checkGray8(SVF_GOLDEN(kSvfGoldenLine2x)); }
	void test_line3by2() { checkGray8(SVF_GOLDEN(kSvfGoldenLine3by2)); }
	void test_streamed() { checkGray8(SVF_GOLDEN(kSvfGoldenLine1x), true); }
	void test_syllableCache() { checkGray8(SVF_GOLDEN(kSvfGoldenLine1x), true, 16); }
	void test_clip() {
		const Common::Rect clip(10, 4, 60, 14);
		checkGray8(SVF_GOLDEN(kSvfGoldenLine1x), false, 0, &clip);
	}

	void test_cachedStreamedReadsNothing() {
		Graphics::SvfChain *chain = SvfTest::makeChain(1, 1, 0, true, 0);	// no block cache: every glyph is read
		chain->setSyllableCache(16);
		Common::Array<byte> buf;
		buf.resize(64 * 20);
		svf_target t;
		svf_target_init(&t, SVF_T_GRAY8, buf.data(), buf.size(), 64, 20, 64);
		const char *text = "\xed\x95\x9c\xea\xb8\x80";	// two syllables
		chain->draw(text, strlen(text), 0, 14, t);
		const uint32 reads = chain->getFace(1)->getStreamReads();
		chain->draw(text, strlen(text), 0, 14, t);
		TS_ASSERT_EQUALS(chain->getFace(1)->getStreamReads(), reads);
		delete chain;
	}
```

Note: these tests prove the **glue** (chain set-up, scale, missing advance, clip, streaming, cache): the renderer itself
is byte-identical to `svfline.py` by its own contract (`make -C dists/svf-fonts-open/c check`).

- [ ] **Step 2: implement `draw` and `drawCoverage`**

```cpp
svf_err SvfChain::draw(const char *text, uint32 len, int x, int yBaseline, svf_target &target) const {
	if (x < -32768 || x > 32767 || yBaseline < -32768 || yBaseline > 32767)
		return SVF_OK;	// the renderer's coordinates are int16: nothing of it can be on a target
	return svf_draw_utf8(&_chain, text, len, (int16_t)x, (int16_t)yBaseline, &target, _scratch.data(), _scratch.size());
}

svf_err SvfChain::drawCoverage(const char *text, uint32 len, int x, int yBaseline, int w, int h, PlotFunc plot, void *ctx,
                               const Common::Rect *clip) const {
	svf_target t;
	const svf_err err = svf_target_init(&t, SVF_T_COVERAGE_CB, nullptr, 0, (int16_t)MIN(w, 32767), (int16_t)MIN(h, 32767), 0);
	if (err != SVF_OK)
		return err;
	t.plot = plot;
	t.ctx = ctx;
	if (clip) {
		t.clip_x0 = clip->left;
		t.clip_y0 = clip->top;
		t.clip_x1 = clip->right;
		t.clip_y1 = clip->bottom;
	}
	return draw(text, len, x, yBaseline, t);
}
```

- [ ] **Step 3: run** — 424 tests OK; on purpose break one golden row locally to see the `TS_TRACE` ASCII art, then
`git checkout test/graphics/svf_golden_data.h`. Also run once with ASan (Task 12 commands) before committing.

- [ ] **Step 4: commit**

```
TEST: Compare SvfChain lines with the Python reference

Lines at 1x, 2x and 3/2 with a mark, invalid UTF-8 and a missing code
point, clipped, streamed and with the syllable cache match the bytes of
svf-fonts-open tools/svfline.py.
```

### Review checkpoint B (after Task 8)
superpowers:requesting-code-review on Tasks 4-8: ownership (who deletes faces/streams), `svf_face` never moves, scratch
size after every change, `TS_TRACE` output readable, no submodule needed by `make test` (rename
`dists/svf-fonts-open` temporarily, `heavy make test`, restore).

## Task 9: `SvfFont` metrics (15 min)

**Files:** create `graphics/fonts/svffont.{h,cpp}`, `test/graphics/svf_font.h`; modify `graphics/module.mk`
(`fonts/svffont.o` after `fonts/newfont.o`), `test/module.mk` (`svf_font.h`).

- [ ] **Step 1: failing tests** — suite `SvfFontTestSuite`:

```cpp
	void test_metrics() {
		Graphics::SvfFont font(SvfTest::makeChain(), DisposeAfterUse::YES);
		TS_ASSERT_EQUALS(font.getFontAscent(), 13);	// marks face: ascent 13 of 16
		TS_ASSERT_EQUALS(font.getFontDescent(), 4);	// jamo face: 16 - 12
		TS_ASSERT_EQUALS(font.getFontHeight(), 17);
		TS_ASSERT_EQUALS(font.getMaxCharWidth(), 16);
		TS_ASSERT_EQUALS(font.getCharWidth(0xD55C), 16);
		TS_ASSERT_EQUALS(font.getCharWidth('a'), 8);
		TS_ASSERT_EQUALS(font.getCharWidth(0x0301), 0);	// a mark
		TS_ASSERT_EQUALS(font.getCharWidth(0x2603), 0);	// no face has it, missing advance 0
		TS_ASSERT_EQUALS(font.getStringWidth(Common::U32String(kSvfGoldenFont1xText)), kSvfGoldenFont1xWidth);
		TS_ASSERT_EQUALS(font.getFontName(), "marks");
	}

	void test_scaledMetrics() {
		Graphics::SvfFont font(SvfTest::makeChain(3, 2), DisposeAfterUse::YES);
		TS_ASSERT_EQUALS(font.getFontAscent(), 20);
		TS_ASSERT_EQUALS(font.getCharWidth(0xD55C), 24);
	}

	void test_encodeUtf8() {
		char b[4];
		TS_ASSERT_EQUALS(Graphics::SvfFont::encodeUtf8('a', b), 1u);
		TS_ASSERT_EQUALS(Graphics::SvfFont::encodeUtf8(0xD55C, b), 3u);
		TS_ASSERT_EQUALS(memcmp(b, "\xed\x95\x9c", 3), 0);
		TS_ASSERT_EQUALS(Graphics::SvfFont::encodeUtf8(0x1F600, b), 4u);
		TS_ASSERT_EQUALS(Graphics::SvfFont::encodeUtf8(0xD800, b), 1u);
		TS_ASSERT_EQUALS((byte)b[0], 0xFF);
	}
```

- [ ] **Step 2: `graphics/fonts/svffont.h`**

```cpp
#ifndef GRAPHICS_FONTS_SVFFONT_H
#define GRAPHICS_FONTS_SVFFONT_H

#include "common/types.h"

#include "graphics/font.h"

namespace Graphics {

class SvfChain;

/**
 * A Graphics::Font over an SvfChain. CLUT8 surfaces get the colour index where coverage >= 128; other
 * 1, 2 and 4 byte formats are blended over the existing pixel. Pass non-ASCII text as
 * Common::U32String: the Common::String overloads of Font draw each byte as a code point.
 */
class SvfFont : public Font {
public:
	SvfFont(SvfChain *chain, DisposeAfterUse::Flag dispose);
	~SvfFont() override;

	int getFontHeight() const override;
	Common::String getFontName() const override;
	int getFontAscent() const override;
	int getFontDescent() const override;
	int getMaxCharWidth() const override;
	int getCharWidth(uint32 chr) const override;
	void drawChar(Surface *dst, uint32 chr, int x, int y, uint32 color) const override;

	SvfChain &getChain() const { return *_chain; }

	/** UTF-8 of @p cp; a surrogate or a value past U+10FFFF becomes the invalid byte 0xFF (drawn as the renderer draws invalid UTF-8). */
	static uint encodeUtf8(uint32 cp, char out[4]);

private:
	SvfChain *_chain;
	DisposeAfterUse::Flag _dispose;
};

} // End of namespace Graphics

#endif
```

- [ ] **Step 3: metrics part of `svffont.cpp`** — `getFontHeight` = `ascent + descent` of the chain; `getFontAscent` /
`getFontDescent` / `getMaxCharWidth` forward to the chain; `getFontName` = first face's name or empty;
`getCharWidth(chr)` = `_chain->getWidth(utf8 of chr)` (one-code-point measure: marks give 0, missing code points the
scaled missing advance, exactly as the line walker); `encodeUtf8` as in the prototype (1-4 bytes, 0xFF for
U+D800..DFFF and > U+10FFFF). `getBoundingBox` and `getKerningOffset` keep the `Font` defaults (section 0).
`drawChar` may be an empty body until Task 10.

- [ ] **Step 4: run** — 427 tests OK. **Commit**

```
GRAPHICS: Add SvfFont, a Graphics::Font over an SVF chain

Character widths come from the renderer's own measure, so marks advance
0 and code points no face has advance the chain's missing advance.
```

## Task 10: `SvfFont` drawing (20 min)

- [ ] **Step 1: failing tests** — in `SvfFontTestSuite` (prototype has the full text): helpers `blend(format, bg, fg,
cov)` (per ARGB channel `(bg * (255 - cov) + fg * cov + 127) / 255` after `colorToARGB`, then `ARGBToColor`; cov 0 = bg,
255 = fg), `pixelAt(surface, x, y)`, and

```cpp
	void checkSurface(const Graphics::PixelFormat &format, uint num, uint den, const char *text, int x, int y, int w, int h,
	                  const char *const *rows, uint32 bg, uint32 fg) {
		Graphics::SvfFont font(SvfTest::makeChain(num, den), DisposeAfterUse::YES);
		Graphics::Surface s;
		s.create(w, h, format);
		s.fillRect(Common::Rect(w, h), bg);
		font.drawString(&s, Common::U32String(text), x, y - font.getFontAscent(), w, fg);
		const Common::Array<byte> cov = SvfTest::goldenBytes(rows, w, h);
		int bad = 0;
		for (int yy = 0; yy < h; ++yy)
			for (int xx = 0; xx < w; ++xx) {
				const byte c = cov[yy * w + xx];
				const uint32 want = format.isCLUT8() ? (c >= 128 ? fg : bg) : blend(format, bg, fg, c);
				if (pixelAt(s, xx, yy) != want)
					++bad;
			}
		TS_ASSERT_EQUALS(bad, 0);
		s.free();
	}
```

with the cases: `test_clut8` (Font1x, bg 3, fg 7), `test_rgb565` (`PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0)`, Font1x and
Line3by2 at 3/2 — the 3/2 line has partial coverage, so the blend is checked), `test_argb32`
(`createFormatARGB32()`, Line3by2), `test_rgb332` (`PixelFormat(1, 3, 3, 2, 0, 5, 2, 0, 0)`, the 1-byte overlay format
of the SDL surface backend: blended, not thresholded), and `test_edges`: a 20x10 CLUT8 surface, `drawChar(&s, 0xD55C,
x, y, 1)` at 2x for every x in {-40, -20, -7, 0, 9, 19, 40} and y in {-40, -9, 0, 5, 9, 40}, plus x = 100000 and
y = -100000 (must not crash; under ASan no report).

Why per-character drawing equals the golden line: `drawStringImpl` (`graphics/font.cpp:113`) moves the pen by
`getCharWidth`, which is the renderer's own advance; a mark drawn alone at the pen lands at `pen - originX`, where the
line walker puts it too (`svf.h` "A mark ... anchorX ... is the current pen"). Overlap is only the mark over `e`:
CLUT8 threshold of max = union of thresholds, so CLUT8 is exact; the RGB tests use texts without marks.

- [ ] **Step 2: implement `drawChar`** (coverage callback; the renderer's RGB565/XRGB8888 kinds do not read the
background and assume one byte order, so they are not used here):

```cpp
namespace {

struct SvfPlot {
	Surface *dst;
	uint32 color;
	bool threshold;
	uint8 a, r, g, b;
};

uint8 blendChannel(uint8 bg, uint8 fg, uint8 coverage) {
	return (uint8)(((uint)bg * (255 - coverage) + (uint)fg * coverage + 127) / 255);
}

void plotPixel(void *ctx, int16_t x, int16_t y, uint8_t coverage) {
	const SvfPlot *p = (const SvfPlot *)ctx;
	void *pixel = p->dst->getBasePtr(x, y);
	if (p->threshold) {
		if (coverage >= 128)
			*(byte *)pixel = (byte)p->color;
		return;
	}
	const PixelFormat &format = p->dst->format;
	uint32 out = p->color;
	if (coverage < 255) {
		uint32 bg;
		if (format.bytesPerPixel == 1)
			bg = *(const byte *)pixel;
		else if (format.bytesPerPixel == 2)
			bg = *(const uint16 *)pixel;
		else
			bg = *(const uint32 *)pixel;
		uint8 a, r, g, b;
		format.colorToARGB(bg, a, r, g, b);
		out = format.ARGBToColor(blendChannel(a, p->a, coverage), blendChannel(r, p->r, coverage),
		                         blendChannel(g, p->g, coverage), blendChannel(b, p->b, coverage));
	}
	if (format.bytesPerPixel == 1)
		*(byte *)pixel = (byte)out;
	else if (format.bytesPerPixel == 2)
		*(uint16 *)pixel = (uint16)out;
	else
		*(uint32 *)pixel = out;
}

} // End of anonymous namespace

void SvfFont::drawChar(Surface *dst, uint32 chr, int x, int y, uint32 color) const {
	if (!dst || !dst->getPixels() || dst->w <= 0 || dst->h <= 0)
		return;
	const PixelFormat &format = dst->format;
	if (format.bytesPerPixel != 1 && format.bytesPerPixel != 2 && format.bytesPerPixel != 4)
		return;	// as TTFFont (graphics/fonts/ttf.cpp:772-776)

	SvfPlot p;
	p.dst = dst;
	p.color = color;
	p.threshold = format.isCLUT8();
	format.colorToARGB(color, p.a, p.r, p.g, p.b);

	char text[4];
	const uint len = encodeUtf8(chr, text);
	_chain->drawCoverage(text, len, x, y + _chain->getAscent(), dst->w, dst->h, plotPixel, &p);
}
```

The renderer clips to the target (`w`, `h` = the surface), so `plotPixel` never sees a pixel outside it.

- [ ] **Step 3: run** — 432 tests OK (and ASan, Task 12). **Commit**

```
GRAPHICS: SvfFont: Draw on CLUT8, 16 and 32 bit surfaces

Glyphs reach the surface through the renderer's coverage callback:
CLUT8 takes the colour index at coverage 128 and above, other formats
blend over the existing pixel. Checked against the golden lines at 1x
and 3/2 on CLUT8, RGB565, ARGB8888 and RGB332.
```

## Task 11: Developer PNG dump (10 min)

- [ ] **Step 1: test** — in `SvfFontTestSuite` (includes `common/file.h`, `common/fs.h`, `image/png.h`,
`test/system/null_osystem.h`):

```cpp
	// Developer aid: with a directory svf-dump in the working directory, writes the Font1x line at 2x
	// on CLUT8 and RGB565 as svf-dump/clut8.png and svf-dump/rgb565.png.
	void test_dumpWhenAsked() {
		Common::install_null_g_system();
		if (Common::FSNode(Common::Path("svf-dump")).isDirectory()) {
			const Graphics::PixelFormat formats[] = { Graphics::PixelFormat::createFormatCLUT8(), Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0) };
			const char *names[] = { "svf-dump/clut8.png", "svf-dump/rgb565.png" };
			for (int i = 0; i < 2; ++i) {
				Graphics::SvfFont font(SvfTest::makeChain(2, 1), DisposeAfterUse::YES);
				Graphics::Surface s;
				s.create(240, 40, formats[i]);
				s.fillRect(Common::Rect(240, 40), i ? formats[i].RGBToColor(0, 0, 128) : 0);
				font.drawString(&s, Common::U32String(kSvfGoldenFont1xText), 4, 2, 236, i ? formats[i].RGBToColor(255, 255, 0) : 1);
				Common::DumpFile out;
				const byte palette[6] = { 0, 0, 128, 255, 255, 0 };
				TS_ASSERT(out.open(Common::Path(names[i])));
				TS_ASSERT(Image::writePNG(out, s, i ? nullptr : palette, 2));
				s.free();
			}
		}
		Common::uninstall_null_g_system();
	}
```

- [ ] **Step 2: run**

```sh
cd $BLD && heavy make -j16 test                      # 433 tests OK, nothing written
mkdir -p svf-dump && ./test/runner SvfFontTestSuite test_dumpWhenAsked && file svf-dump/*.png
# clut8.png: PNG image data, 240 x 40, 8-bit colormap; rgb565.png: 240 x 40, 8-bit/color RGB
```

Open `svf-dump/rgb565.png`: yellow `가한글 abéz` on navy at 2x (verified in the prototype). Needs `USE_PNG`
(configured on this host); without it `writePNG` fails and the test reports it only when `svf-dump` exists.

- [ ] **Step 3: commit**

```
TEST: Let the SvfFont tests dump PNG files on request

With a directory svf-dump in the working directory the test writes the
test line on CLUT8 and RGB565 surfaces as PNG files.
```

### Review checkpoint C (after Task 11)
superpowers:requesting-code-review on Tasks 9-11 (Font contract: `drawChar` never writes outside the surface; colour
semantics per format; `Common::String` limitation documented in the header).

## Task 12: ASan and mingw (15 min, no commit unless a fix is needed)

```sh
A=~/work/tmp/scummvm2026-p0asan; mkdir -p $A && cd $A
$SRC/configure --disable-all-engines --enable-debug --disable-optimizations --enable-asan >/dev/null
heavy make -j16 test          # 433 tests OK, no AddressSanitizer/UndefinedBehavior report (about 11 s)

M=~/work/tmp/scummvm2026-p0mingw; mkdir -p $M && cd $M
( . ~/.local/mingw/env.sh >/dev/null; export PATH=$MINGW_SYS/bin:$PATH
  $SRC/configure --host=x86_64-w64-mingw32 --with-sdl-prefix=$MINGW_SYS --disable-all-engines --enable-debug --disable-optimizations >/dev/null &&
  heavy make -j16 test/runner )   # builds test/runner.exe; "C graphics/svf/..." lines use x86_64-w64-mingw32-g++ -x c
make -n -B graphics/svf/svf_face.o | tail -1    # must start with x86_64-w64-mingw32-g++ -x c -std=c99
```

Running `test/runner.exe` needs Windows or wine (not installed here): UNVERIFIED; copy it to a Windows machine or
install wine, then `wine test/runner.exe`. If a fix is needed, commit it as `BUILD: ...` with the trailers.

## Task 13: Manual GUI check (optional, throwaway, never committed)

In `gui/about.cpp`, `AboutDialog::drawDialog` (line 271), at the end of the function:

```cpp
	static Graphics::SvfFont *svfTest = nullptr;	// THROWAWAY: P0 manual check
	if (!svfTest) {
		Graphics::SvfFace *face = new Graphics::SvfFace();
		Common::FSNode node(Common::Path("/home/thkim/work/scummvm-2026.4-korean/dists/svf-fonts-open/svf/dos/NEODG16A.SVF"));
		face->load(node.createReadStream(), DisposeAfterUse::YES, "NEODG16A");
		Graphics::SvfChain *chain = new Graphics::SvfChain();
		chain->addFace(face, DisposeAfterUse::YES);
		svfTest = new Graphics::SvfFont(chain, DisposeAfterUse::YES);
	}
	Graphics::ManagedSurface *screen = g_gui.theme()->renderer()->getActiveSurface();
	svfTest->drawString(screen, Common::U32String("\xed\x95\x9c\xea\xb8\x80 SVF"), _x + 8, _y + 8, 200,
	                    screen->format.RGBToColor(255, 255, 0));
```

(includes: `common/fs.h`, `graphics/fonts/svffont.h`, `graphics/hires_text/svfchain.h`, `graphics/VectorRenderer.h`.)
Build `heavy make -j16` in `$BLD`, run `./scummvm`, open About. `renderer()` (`gui/ThemeEngine.h:669`) and
`getActiveSurface()` (`graphics/VectorRenderer.h:311`) exist; whether the text survives the dialog's redraw is
UNVERIFIED. Revert with `git checkout gui/about.cpp`. Report what was seen (and the overlay format, printed with
`warning("%s", screen->format.toString().c_str())`), do not commit.

## Task 14: Final acceptance

- [ ] `cd $SRC && tools/korean/sync-svf.sh --check` → matches 3d40de8.
- [ ] `python3 tools/korean/mkgolden.py --check` → rc 0.
- [ ] `cd $BLD && heavy make -j16 test` → `Running cxxtest tests (433 tests)...OK!` (406 upstream + 27 new; adjust if
  upstream test count differs, but no failure).
- [ ] ASan run (Task 12) clean.
- [ ] mingw `test/runner.exe` builds (run UNVERIFIED without wine).
- [ ] `git -C $SRC status` clean; `git log --oneline ko-2026.4..` shows the commits of Tasks 1-11, each subject
  `[A-Z_]+:` (`python3 devtools/check-commit-msg.py <(git log -1 --format=%B)` exit 0), each with both trailers.
- [ ] `docs/korean/PATCHES.md` lists every upstream file the diff touches:
  `git diff --stat ko-2026.4.. -- . ':!graphics/svf' ':!graphics/hires_text' ':!graphics/fonts/svffont.*' ':!test/graphics/svf*' ':!tools/korean' ':!docs/korean'`
  shows only `configure`, `graphics/module.mk`, `test/module.mk`, `LICENSES/*`.

Mapping to DESIGN section 11, row P0:

| P0 done-criterion / scope item | Evidence |
|---|---|
| vendored `graphics/svf` + sync script + VERSION | Task 1, `sync-svf.sh --check` |
| configure defines, one place, every TU | Task 2 (`config.mk` DEFINES), `svf_build.h::test_switches` |
| static_assert guarding `sizeof(svf_face)` (risk "switch mismatch across TUs") | Task 5: `svfface.h` asserts, `svf_abi.c` `#error`, `test_abi` |
| licence/credits | Task 3 (`LICENSES/COPYING.MIT`, `COPYING.OFL`; no `devtools/credits.pl` entry: it credits people, and the vendored nanosvg has none either) |
| `SvfFace` (whole read + streamed via `svf_read_fn`) | Tasks 5-6, `SvfFaceTestSuite` |
| `SvfChain` | Tasks 7-8 |
| `SvfFont : Graphics::Font` (CLUT8 threshold, RGB blend) | Tasks 9-10 |
| tests `svf_face`, `svf_chain`, `svf_font`, `svf_golden` | `svf_face.h`, `svf_golden.h` (holds the chain tests), `svf_font.h`, plus `svf_build.h` |
| golden pixels vs `tools/svfline.py` | Task 4 data, Task 8 tests |
| JMAP status test (`svf_status == 0` on a JMAP face) | `SvfFaceTestSuite::test_jmapFaceOpensClean` |
| `make test` green on Linux | Task 14 |
| `make test` green on mingw | build verified; run UNVERIFIED (no wine) |
| GUI dialog draws Korean with `SvfFont` on CLUT8 and RGB565 (manual) | CLUT8/RGB565/RGB332 unit tests + PNG dump (Task 11); GUI throwaway check Task 13 (UNVERIFIED) |
