# P1 review: hi-res core (2026-11-22)

Independent read-only review of `ko-2026.4` from `dd5f64b8b34` (P0 + fixes) to `d6b3b963ae8` (20 commits,
`git diff dd5f64b8b34 HEAD -- . ':!docs'`, 49 files, +8204/-4). Reviewed against `docs/korean/DESIGN.md` (sections 5, 7,
10-12), the P1 plan (section 1 deviations taken as approved) and `P1-notes.md`, checking the code, not the notes. The eight
ported production files were reviewed as new (`cmp` against `jamo` prints nothing for all eight; the two test edits are
the ones the plan names). Scratch work: `~/work/tmp/p1-review/` (`cp/` create_project build, `edge.cpp` ASan+UBSan
probe with the twelve `hires_text` units recompiled under `-fsanitize=address,undefined`, `uni.cpp`+`uni_check.py`
Unicode table check, `qual.cpp` qualifier probe, `box.cpp` shipped-face probe, `build.sh`).

## What was verified (passes)

| Check | Result |
|---|---|
| `~/work/tmp/scummvm2026-p1verify/test/runner` (re-run) | `Running cxxtest tests (534 tests)...OK!` |
| `~/work/tmp/scummvm2026-p1asan/test/runner` with `ASAN_OPTIONS=detect_leaks=1` (re-run) | 534 OK, no sanitizer or leak report |
| `tools/korean/sync-svf.sh --check`; worktree `git status` | match at `3d40de8d940f`; clean |
| `devtools/check-commit-msg.py` on each of the 20 commits (message written to a file: the script takes a path) | 20 ok; each ends in exactly one `Assisted-by: Claude Code`, no `Co-Authored-By` |
| `git diff --check` (whitespace), tabs, GPL header + path-based guard on every new `graphics/` and `test/graphics/hires_text_*` file, `// End of namespace`, `override` | clean (the five test headers without a GPL header are the ported `jamo` suites and `test/common/*`, as upstream `test/` practice) |
| Upstream files touched vs `PATCHES.md` | exactly `LICENSES/COPYING.OFL`, `common/language.{h,cpp}`, `graphics/module.mk`, `test/module.mk`; `engines backends gui base` untouched |
| create_project (P0 fix M-1 regression): `~/work/tmp/p1-review/cp/create_project $SRC --cmake` and `--msvc --msvc-version 17` | all 12 new `graphics/hires_text/*.cpp` plus the three P0 units listed in `CMakeLists.txt` and `scummvm.vcxproj` (15 `hires_text` sources each) |
| `common/language.cpp` folder table vs the `Language` enum of this tree (40 values) | every value present, ISO 639-1 codes right (`uk` for `UA_UKR`, `nb`, `he`), generic-first ordering gives `EN_ANY`/`ZH_ANY`/`NL_NLD`/`PT_PRT`/`FR_FRA` on parse; `test_everyLanguage` walks the whole enum |
| `unicode_props.cpp` tables (generated from Unicode 16.0) vs this host's Python `unicodedata` 15.0 over all 0x110000 code points (`uni_check.py`) | `isCombining` 1 diff (U+1171E, Mn -> Lo in 16.0), `isSpaceSeparator` 0, `isWide` 188 diffs all in `2630-2637 268A-268F 4DC0-4DFF 1D300-1D356 1D360-1D376` (EAW changes of 15.1/16.0), `isInkless` only the four Hangul fillers the header documents: the tables are what they claim |
| `BandedPlane` fuzz vs a flat reference (5 shapes incl. 1x1, 3x17 partial last band, 641 wide, unpackable; set/fill/readBytes/writeBytes/copyFrom/row/toSurface, band presence invariant) | 0 mismatches, ASan/UBSan clean |
| `TextPlane` at 640x480 scale 4 (2560x1920): corner plots, out-of-plane plots, save/restore of a rect crossing the edge, every reader on a never-created plane | correct; two corner pixels hold 122 880 B (one index band + two coverage bands) |
| `TextCompose` on RGB565, RGB555, ARGB1555, ARGB4444, BGR565, XRGB/ARGB/RGBA/ABGR8888 with a rect larger than plane and surface: threshold leaves coverage < 128, writes the palette colour at >= 128; blend equals `blend()` applied to the under colour *as the format gives it back* and stays within one channel quantum of the 8-bit ideal; 3-byte formats refused; `presentRect` with three different sizes; stamp with a rect outside the plane | all as documented, no OOB (`edge.out` section B2) |
| Golden provenance of `test_composeRgb565`: `0x52AB` recomputed by hand from navy read back as (0,0,132) and `blend` | matches; the test comment states the 132 |
| Map parser hostile input (`edge.out` section C): CRLF, CR-only, no final newline, empty, `[`, `[]`, `=`, `size=` empty, `range.=`, 24-digit ints, `[font.-1]`/`[font.70000]`/`[font.1e3]`, `version=3.0`, double BOM, NUL byte, `same,same,original`, 200 kB value, 20 000 `[font.N]`, 3000 random alphabets through `load`+`compileIdPlan` | no crash, no UB, refusals and warnings as designed (CRLF values are clean: `path=game/ko/K.SVF`) |
| `isSafeDataPath` (section D): `..` anywhere, `/x`, `\x`, `C:x`, `C:/x`, `x/C:y`, `//server/share`, `\\server\share`, `x\y` refused; `.`, `x/./y`, `~/x` (not expanded when joined under a root), trailing `/` accepted | right |
| `TextLayout` fuzz: 20 000 random strings (Latin, Hangul, conjoining jamo, Thai, marks, kinsoku, escapes, invalid UTF-8, astral) x random rules x `maxWidth` -1..11: lines contiguous, `next > first`, byte offsets consistent, over-wide lines only in the documented "mark after hanging spaces" case (`test_zero_width_after_hanging_space_is_no_emergency`) | 181 049 lines, no UB |
| `Utf8TextDecoder::decode` vs `Graphics::decodeUtf8` (font_set) over every 3-byte prefix of the 2^24 space | 0 differences (both RFC 3629: overlongs, surrogates, > U+10FFFF rejected) |
| `HiResRangeTable` vs a brute-force reference of the documented precedence, 300 random scope sets x 3000 code points + every span edge | 0 mismatches |
| `HiResFontSet` with the fixture faces: `[load] stream`, a missing face (cached once as failure, warned once), `size=17`/`255` (warned, drawn unscaled), `size=4` (1/4), `face=gone` (build false), `face=gone,original` (build true), drawText at x/y in {-100000, -1, 40000, 2^31-648} with and without a clip outside the plane, `measure` of invalid UTF-8, `drawChar(0xFFFFFFFF)`, `getAdvance(0xD800)` | no crash, no UB |
| Shipped `NEODG16A.SVF` (`box.cpp`): loads whole, status 0, cell 16; has U+25A1 (the map's `missing=`), ASCII, U+AC00..U+D7A3, compat jamo; lacks U+FFFD, conjoining jamo, U+2026, é | consistent with the map; invalid UTF-8 is skipped, not boxed |
| `SvfChain::addFace` default `DisposeAfterUse::NO` (`svfchain.h:51`): the cache owns the faces, the set owns the chains; `build()` twice replaces | lifetime right |
| Qualifier merge order for scalars: `[S]`, `[S:target]`, `[S:engine]`, `[S:engine:target]` (`font_map.cpp:182-202`), ties in file order | as the plan says (and M-1 below for range keys) |

## Findings (ranked)

### Blocker

None.

### Major

**M-1. A range or advance rule in a qualified section never overrides the base section's rule for the same span.**
`graphics/hires_text/font_map.cpp:296-310` appends every `range.<spec>` / `advance.<spec>` of the merged sections in
least-specific-first order; `unicode_ranges.cpp:178-192` (`narrowestCovering`) keeps the *first* of equal-width spans. So
`[font:sci]`, `[font:rgb]`, `[font.N:clut8]` can change `size=`, `face=`, `shift=` (scalars are assigned last-wins) but
their range rules lose to `[font]` / `[font.N]`, silently. The same holds for two rules on one span inside one section
(first wins, no warning), the opposite of the scalar rule, and the comment at `unicode_ranges.cpp:174-177` claims the
loader de-duplicates ("later line is ignored is enforced by the loader before this point"): the trimmed loader does not.

Reproduction (`~/work/tmp/p1-review/qual.cpp`, output verbatim):
```
qualified [font:sci] range vs [font] (expect c, cell, size 24): engine=sci id=0 -> chain 1 = b; advance=0; warnings=0
[font.1:sci] vs [font.1] (expect c): engine=sci id=1 -> chain 1 = b; advance=0; warnings=0
duplicate span in one section (scalars: last wins; ranges: ?): engine= id=0 -> chain 1 = b; advance=0; warnings=0
```
(map: `[font] face=a range.hangul-syllables=b advance.hangul-syllables=font` then `[font:sci] range.hangul-syllables=c
advance.hangul-syllables=cell`; loaded with `engine=sci` the Hangul chain is still `b` and the advance still `font`.)

This is the most likely use of a qualifier in a pack map (a 1 bpp face on `:clut8`, an AA face on `:rgb`, per range), and
P3's SCI maps will hit it. Minimal fix: in `parseScope`, before pushing a spec, search `s.rangeSpecs` / `s.advanceSpecs`
for an entry with the same `kind` and `span` (compare spans, not `spelled`, as the design says) and overwrite its value
(later = more specific wins); warn when the duplicate is in the same section (`list[i]` index equal). Add a test to
`hires_text_font_map.h` / `hires_text_id_plan.h` with the map above, and fix the stale comment in `unicode_ranges.cpp`.

### Minor

**m-1. The shipped face and map cannot be reached by a running ScummVM yet, and the map's comment says they can.**
`dists/engine-data/hires_text/maps/korean-default.map:4-6` says `data:` is looked up "in the extrapaths, then the
ScummVM data folder (dists/engine-data in a source build)". `HiResDiskFiles` (`hires_files.cpp:45-55`) roots are the three
extrapaths and `DATA_PATH` (`/usr/local/share/scummvm` here), then `SearchMan`; in a source-tree run `SearchMan` holds
`DATA_PATH` (depth 4, `backends/platform/sdl/sdl.cpp:651-657`) and `.` at depth 1 (`sdl.cpp:662`), so
`hires_text/NEODG16A.SVF` under `dists/engine-data` is found by nothing. Nothing installs or packages the two files either
(`dists/engine-data/engine_data.mk`, `dists/scummvm.rc`, Android assets untouched; the plan defers this to P3). The test
`test_defaultMapDraws` adds the root by hand, so it does not prove discoverability. Fix now: correct the comment ("the
ScummVM data folder: install it, or point extrapath at dists/engine-data"); P3 must add the `engine_data.mk` /
`scummvm.rc` lines (and keep `svf_face` whole-read in mind for the `.rc` resource path).

**m-2. `hangul=any` breaks inside a conjoining jamo sequence; tone marks break in both modes.**
`text_layout.cpp:260-284` (`isHangul`, `breaksAsWide`) and `711-717` (`isClusterBoundary`). A leading consonant jamo
(U+1100-U+115F) is East Asian Wide, the vowel/trailing jamo (U+1160-U+11FF, U+D7B0-U+D7FF) are not and are `Lo`, not
`Mn`, so under `any` a line may end between L and V (UAX #14 LB26 forbids it); U+302E/U+302F (Hangul tone marks, `Mc`,
wide, not in `isHangul`) allow a break before them even under `word`. Probe output:
`jamo L V T units=3 flags=16,0,0 canBreakBefore(1)=1 canBreakBefore(2)=0 (any)`. Impact is low: the packs use precomposed
syllables and `NEODG16A` has no conjoining jamo. Fix: an `isHangulTrailing(cp)` (V, T, tone marks) treated like
`kUnitCombining` in `canBreakBefore` (`if (fb & kUnitCombining)` branch) and `isClusterBoundary`.

**m-3. Font id and code point spellings the parser accepts or drops silently.** `font_map.cpp:456-460, 569-575, 387-408`.
`[font.01]` next to `[font.1]` is dropped without a warning (`fontIds.contains(1)` after the first base string wins);
`[font. 2]` is accepted (`strtol` skips blanks); unknown keys in `[map]` are not warned (every other section is);
`missing=u+d800` is accepted (surrogates encode as the invalid byte and draw nothing, so the `missing=` silently does
nothing); `0x` is accepted by `missing=` but refused by `range.` (`unicode_ranges.cpp:118`). Probe section C shows each.
Fix: canonicalise the id (`Common::String::format("font.%d", id)`) when selecting sections and warn on a second spelling;
refuse surrogates in `parseCodePoint`; warn unknown `[map]` keys.

**m-4. Backends whose only 16-bit format is 555/1555/4444 get CLUT8 although the sinks handle them.**
`hires_options.cpp:140-148` (`hiResTargetOf`) returns `auto` for anything but 565 and 32-bit 888, so
`resolveRenderTarget` never offers RGB there, while `composeSpan`/`convertSpan` are correct on RGB555, ARGB1555,
ARGB4444, BGR565 (probe B2). DESIGN names only the three targets, so this is a scope choice; either accept any 2-byte
RGB format as the `rgb565` target or say in `hires_options.h` that such backends draw by threshold on CLUT8.

**m-5. Tests that prove less than their name.** `test/graphics/hires_text_screen.h:119-128`
`test_scaledCoverageBlends` only asserts that the plane holds a coverage that is not a multiple of 17; it never presents
the scaled line on RGB and checks a blended pixel, so a `presentRect` that stamped instead of blending would pass. No map
test has CRLF line ends (works: probe C). `readHiResOptions` (the ConfMan path) is untested. `test_dumpWhenAsked` and
`test_packMapsWhenGiven` are developer aids (as P0's). Fix: present `Scene` on RGB565 with `kBlend` and assert one pixel
of the 24 px line is strictly between navy and yellow; add a CRLF case to `test_versionGate`.

### Nits

- `font_map.cpp:416` vs `hires_files.cpp:114`: the absolute-path test is `size() > 2 && [1] == ':'` in `resolveFace` and
  `size() > 1` in `findGameMapFile`; a bare `C:` is relative in one and absolute in the other.
- `hires_files.cpp:68-85`: `find()` takes the first case-insensitive match in `getChildren()` order; with `HIRESVF.MAP`
  and `hiresvf.map` both present on a case-sensitive file system the pick depends on `readdir` order. Prefer an exact-case
  match, else the first in sorted order.
- `banded_plane.cpp:224-253`: `readBytes`/`writeBytes` divide by `_w` (0 on a plane that was never created); only
  `TextPlane::save/restore` guard it, through the empty clipped rect. Add `if (!exists()) return;`.
- `map_pick.cpp:57`: a map found through `SearchMan` gets `mapDir` = `searchman:...`, so relative `face=`/`[preload]`
  values in it resolve to paths `FSNode` cannot open; only `data:` values work in such a map (true of the shipped one).
- `font_set.cpp:113-118`: `size=` that is not an N/M with N, M <= 16 (17 on a 16 px cell) warns and draws at 1:1; picking
  the nearest representable ratio would be kinder to map authors.
- `text_compose.cpp:88,100`: `RGBToColor` writes opaque alpha on formats with an alpha channel; right for a screen, worth
  one sentence in `text_compose.h` for an engine that composes into an overlay.
- `text_plane.cpp:64-68`: `toHires` builds a `Common::Rect` (int16) from `lowres * scale`; a low-res coordinate >= 8192 at
  scale 4 overflows (unreachable with game screens; a comment would do).
- `hires_options.cpp:76`: `hires_text_scale` compares `"1"`/`"2"` untrimmed; `"1 "` is a warning.
- `dists/engine-data/hires_text/NEODGM.TXT` is the submodule's notice verbatim and describes 18 faces of which one is
  shipped; fine as provenance, but a reader may look for the others.
- `korean-default.map` is 19 lines and fine; `[render] scale=auto blend=auto` are the defaults and could go.

## Things checked and found correct (so nobody re-reviews them)

- `BandedPlane`: packed nibble read/write, `nonZero` counting through `set`/`fill`/`unpack`, whole-band drop on a full
  clear, `unpack()` dropping bands on OOM with one warning, `copyFrom` after `free()`, `row()` with `n` past the row end,
  `readBytes` using `dst` as its scratch; `Common::Array::resize` value-initialises `Band`.
- `TextPlane`: `plot` rule 10 of the plan (same index: larger coverage stays; other index: replaces), index 0 text holds no
  index band and `getRow` zero-fills, `save`/`restore` symmetric for the same clipped rect, `xor`/`swap` touch only pixels
  with coverage, every reader returns "no text" outside the plane (P1-notes fix after Task 6, verified under ASan).
- `TextCompose`: pixels read/written native-endian through `READ_UINT16/32` and the format's shifts, so big-endian hosts
  are right by construction; `kChunk` 256 buffers for spans of at most 256; `compositeRect`/`presentRect` clip to plane
  and every surface; mode/format pairing refused for CLUT8+blend, RGB+stamp, 3 bpp.
- `TextLayout` (ported): `fitLine` always advances (`next > from`), emergency split at cluster boundaries, forced lines,
  hanging spaces, Thai segmentation cache invalidated by `decode()`/`assign()`; `CodePageTextDecoder` never reads past
  `end`; `TextRun::decode` clamps a decoder that over-reports.
- Map parser: BOM on line 1 only, `readLine` strips CR, comments only after a blank, `[map]` gate ignores qualifiers, a
  version 2 map keeps its warnings when refused, `[fonts]` names validated, `[load]`/`[preload]` must name a `[fonts]`
  entry, `parseInt` via `strtol` with range checks (overflow clamps and is refused), `parseCodePoint` <= U+10FFFF.
- `compileIdPlan`: `same` expansion incl. an outer chain ending in `original`, chain de-duplication by face key,
  `[font.N]` scope first then `[font]` in the range tables, scalars `[font.N]` over `[font]` over defaults.
- Options: the eight keys of decision 5 / section 7 with the design's defaults; invalid values warn and keep the default;
  `hiResFontOrder` drops TTF (not built) with a note; `resolveRenderTarget` always offers CLUT8, falls back
  888 -> 565 -> clut8 with a note; `resolveScale` 2 up to 320x240; `ConfMan.hasKey/get` search transient, game, app.
- `HiResDiskFiles`: extrapaths read in the same three domains as `base/main.cpp:280-294`; `DATA_PATH` guarded by
  `#ifdef`; `searchman:` prefix round-trips through `open()`; `locate()` of a non-`data:` ref checks existence only.
- `map_pick`: `hires_text_map` (set, empty = off), language folder before game folder, a kind skipped with one note each
  for not-found / does-not-load (with the first parser warning) / face-missing; explicit maps are not face-checked.
- `HiResFontSet`: chains opened once per face through the cache (`opens == 2` for two sets), `size/cell` reduced by
  `gcd`, syllable cache 64 slots per chain, `route()` falls to the game font for an `original` chain or a chain without
  faces, `missing=` substituted only when the chain has it, `kHiResAdvanceCell` uses the scaled max cell width
  (`svfchain.cpp:139-144`), per-code-point `drawCoverage` reproduces the renderer's whole-line golden rows incl. the mark
  and the missing advance (`test_perCodePointIsTheGoldenLine`, `test_sizeScales` at 2/1 and 3/2).
- `common/language`: new functions declared `extern` like their neighbours; `parseLanguageFolderCode("")` is `UNK_LANG`.
- Build: `graphics/module.mk` objects literal and alphabetical; `test/module.mk` glob `hires_text_*.h` does not catch the
  helpers `hires_screen.h`/`hires_support.h`; `LICENSES/COPYING.OFL` entry names the face and its notice file.

## Verdict

**P1 is a sound base for P2 and P3.** No blocker; the code is correct on every path exercised (ASan/UBSan clean on the
edge probe and both full runners; the ported units match `jamo` byte for byte and their tables match Unicode 16.0; the
compositing maths and byte order are right on every 2- and 4-byte format; the parser survives hostile input; the golden
tests prove the glue against the renderer's own rows). One semantic bug must be fixed before P3 writes qualified maps:

Required:
1. **M-1**: qualified-section (and duplicate) `range.`/`advance.` rules must override the base section's rule for the same
   span (later wins, warn on a duplicate within one section); fix the stale comment in `unicode_ranges.cpp`; add a test.

Required before P3 ships the engine fallback (can be the first P3 task):
2. **m-1**: install/package `dists/engine-data/hires_text/` (`engine_data.mk`, `scummvm.rc`) and correct the map's comment
   now.

Recommended in the same fix-up commit (small, no design impact):
3. **m-2** Hangul V/T jamo and tone marks as part of the previous cluster.
4. **m-3** `[font.N]` spelling canonicalisation and warnings; refuse surrogate `missing=`.
5. **m-5** make `test_scaledCoverageBlends` present on RGB and add a CRLF parser case.

Not required: m-4 (scope choice), nits.
