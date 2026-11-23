# P6a fix round re-review, Claude lane

Reviewed: `0c6d8e45ced..4fd29b336e0` (`fix.diff`, `P6a-fixes.md`), read-only checkout `~/work/tmp/scummvm-review-p6a` at
`4fd29b336e0`. Method: run, not read. Scratch `~/work/tmp/p6a-review-claude/`:
- `build/`: out-of-tree SCUMM-only ASan+UBSan build of `4fd29b336e0` (`ScummVM 2026.4.011126-g4fd29b336e0`).
- `h/fuzz2.cpp` (+ `build2.sh`): new harness, the checkout's `korean_text.cpp`, `trs_bundle.cpp`, `ksx1001_table.cpp`,
  `korean_sentence.cpp` under ASan+UBSan, exact-size heap buffers with canaries, independent expected outputs.
  Output `h/fuzz2b.out`.
- `runs2/`: game runs (`loadcmp.sh`, `scan.sh`, `hovertest.sh`), `data/`: patched data copies.

## Verdict

**P6a can merge into ko-2026.4 after one more fix, N1: a one-line change, and its repro is below.** Every finding of the
first round is fixed and holds under runs. The fix round brought in one regression class (N2, latent, no real data) and
two nits. N1 is the out-of-bounds read agy reported (I1). The implementer could not reproduce it, and the fix that
claims to bound it does not cover the renderer that actually reads out of bounds. It is a memory-safety read reachable
from pack data, and the DOS port (S2) has no ASan to catch it, so fix it before S2.

## First-round findings

| Finding | Status | Run evidence (mine) |
|---|---|---|
| M1 dialog code page | **addressed** | Legacy MI2 save renamed to UTF-8 "한글 저장" (`runs2/legacy-named.s02`) plus the implementer's slot 1, original GUI F5 > Load, no `encoding.dat`: **`1. 한글 저장` / `2. 한글 저장`** (`runs2/sn1-new/scummvm-monkey2-ko-00000.png`), 0 sanitizer reports. Blank list in the scrolled bridge room: **upstream behaviour, confirmed**. Plain 2026.4 with `encoding.dat` on the same legacy save and room also lists slot 2 with no name (`runs2/sn-cmp.png`, top: plain, bottom: new) |
| m1 truncated escape | **addressed** | 300000 hostile inputs: 0 writes past the buffer, **0 past the terminator**, 0 unclean cuts (each cut is a prefix of the unbounded result on a character/escape boundary), 0 outputs with a truncated escape, **in-place (`dst == src`) equal to out-of-place in all cases**. Idempotence: 0 failures when the first pass is of unknown encoding; the remaining failures are N2 |
| m2 0xFE trail | **addressed** | 200000 generated CP949 strings (KS X 1001 syllables incl. trail FE, FF escapes with 0/high arguments, FE 01/FE 08, lone glyph bytes, Hanja) against an independent expected UTF-8: 0 misreads from escape/end handling. The only differences are the documented C2/C3 ambiguity (372) and 1 contrived case (lone glyph bytes EC AC 9E forming UTF-8 Hangul) |
| m3 per-character recovery | **addressed** | 200000 UTF-8 strings with up to 2 stray bytes, declared UTF-8 (bundle path): **0 wrong** (stray -> U+0080..U+00FF, the rest unchanged). As unknown encoding, 33180 differ: expected for strings without enough Hangul (documented heuristic, only game data takes that path) |
| m4 legacy saves as CP949 | **addressed** | Legacy saves loaded in plain 2026.4 and in this build (`runs2/load/*-cmp.png`): Loom (5 resources converted), DOTT (13), MI2 slot 2 (18), MI2 slot 6 (19). The frames are the same text; diffs are animation/cursor only. The DOTT sentence line differs between runs of plain itself (`runs2/load/dott-sent-4.png`). `legacyCp949Resource` (200000): never touches a resource with an interior NUL, no NUL, or no syllable; the source is never modified |
| m5 512-byte buffer | **addressed** | A new save ends `TENC 02` + 256 bytes (`runs2/newsave`). Loaded by the previous commit's binary (`p6a-asan`, kind-1 loader): `State loaded`, nothing converted, 0 errors; by this build: the same. `legacyCp949Buffer` (100000 CP949 messages up to 510 bytes, waits/newlines, stale tails, position on any character boundary) against an independent model: text, **position remap**, zero tail: 0 problems. Not reached in play: I could not get an autosave taken mid-message (the sign line was in none of 16 autosaves) |
| m6 ko/ case | **addressed** | MM copy with `KO/KOREAN.TRS`: `--detect` gives `V2/DOS/Korean` (previous binary: `English`), and the bundle loads (`runs2/koup`) |
| agy I1 / n1 glyph width, `_numChars` | **not addressed for v1/v2 (N1)** | see N1 |
| agy I4 v2 verb hit boxes | **addressed** | Hover scan on MM row 1 (`runs2/scan-*`): the old binary highlights `잠금 풀기` up to x 508 and `열기`/`켜기` 40 px past their text. The new build highlights inside the text only (`열기` 112..140, none at 144+; `켜기` 513..540, none at 548+). All 15 verbs highlight on their text and 13/15 not in the gap after it (`runs2/hov-new`). The 2 gap highlights are likely stale-redraw timing (`주기`, `다른 사람`): UNVERIFIED |
| agy I5 line break NUL | **addressed** | `koreanInsertBreak` 100000 runs, exact-capacity heap buffers, up to 8 inserts: 0 writes past capacity, NUL always kept, content intact. Not exercised in play near 512 |
| agy I6 inventory cut | **addressed** | `koreanCopyChars` 100000: never past the NUL, cut on a boundary, at most `maxChars`. Not reached in play (needs a renamed object) |
| English MM V1 | **unchanged** | Plain vs this build, same input script: the only frame difference is the animated cursor (`runs2/mmen-verbs.png`); 0 Korean log lines, logs equal apart from paths |

Unit tests in the ASan+UBSan build: **677 tests OK**, 0 `runtime error`.

## New findings

### N1 (major, required): v1/v2 `CharsetRendererV2` reads past its 128-glyph font; the new guard misses it

- `charset.cpp:962` (`printChar`) and `:1053` (`drawChar`) guard `chr >= (_numChars ? _numChars : 256)`. But
  `CharsetRendererV2` (MM/Zak v1-v2) never sets `_numChars`: it stays 0, which the guard reads as 256. Its built-in font
  `englishCharsetDataV2` is 1024 bytes, 128 glyphs, so `_fontPtr + chr * 8` for U+0080..U+00FF reads up to 1 KiB past it.
- Reproduction: copy of the MM Korean data whose bundle translation of 열기 has one lone `0x80` byte
  (`data/mmfa/KOREAN.TRS`, one byte changed; `cp949ToUtf8()` makes it U+0080). Start, pick kids, START, Escape x2
  (`runs2/mmfa/run.log`):
  `ERROR: AddressSanitizer: global-buffer-overflow ... in Scumm::CharsetRendererPC::drawBits1Kor ... CharsetRendererV3::printChar ... ScummEngine::drawString ... ScummEngine::drawVerb ... o2_verbOps`,
  `0x...620 is located 0 bytes after global variable 'englishCharsetDataV2' ... of size 1024`. With `0xFA` instead the
  read lands inside the next global (no ASan report) and a garbage glyph shows next to 열 (`runs2/mmfa`, first run).
- Why the implementer could not reproduce it: they reasoned from the V3 guard and from real data (no U+0080..U+00FF in
  MM/Zak text), and did not see that V2's `_numChars` is 0. Upstream reads out of bounds too on such a byte, but this
  round claims the bound. Reachable from a pack's stray byte, or from a stray byte that the new tolerant path maps to
  U+0080..U+00FF.
- Fix: set `_numChars = 128` in `CharsetRendererV2`'s constructor (the font is always `englishCharsetDataV2`-sized), or
  make the V2 guard `chr >= 128`. Add a unit test or rerun this repro.

### N2 (minor): text kept as "known UTF-8" is not stable when it enters the boundary again as unknown

- `toEngineText(knownUtf8)` keeps 2-byte characters above U+00FF (`utf8Valid(..., allowWide = true)`, and
  `tolerantUtf8(..., true)`). When the same bytes come back through the unknown path, `textIsUtf8()` rejects them
  (above U+00FF) and the per-text vote can send them to CP949. Example from the harness:
  - in: `42 DE B0 01 38 20`
  - known: unchanged
  - second pass as unknown: `42 C3 9E 3F 38 20`
  - Longer texts become whole-string mojibake: `... C7 B9 ...` kept as U+01F9, then read as the CP949 syllable C7 B9.
- This is the engine's real path: a translation (known) is stored by `loadPtrToResource()` and later redrawn through
  `convertMessageToString()` -> `translateText()` (not found) -> `copyTranslatedText()` as unknown.
- Harness: 5744 of 300000 hostile inputs, **all with a known first pass**. Real data: no UTF-8 bundle has such a character
  (first-round scan), so latent.
- Fix: in the known paths emit only engine text that `textIsUtf8()` accepts: map a 2-byte character above U+00FF (no
  glyph anyway) to `?` or to its bytes as U+0080..U+00FF. Then add the property "known output is accepted as unknown"
  to the hostile-input test.

### N3 (nit): `memmove(dst, nullptr, 0)` in `toEngineText()`

- `korean_text.cpp:332`: when the conversion produces nothing (`buf` empty: for example a lone `FF`, now dropped),
  `buf.begin()` is null and `memmove(dst, text, 0)` is undefined behaviour. UBSan: `null pointer passed as argument 2`.
- New with the dropped escape (the old code always copied the escape). Harmless in practice.
- Fix: `if (at) memmove(...)`.

### N4 (nit): "never shrinks" is not true

- `convertLoadedText()` / `korean_text.h` say a converted legacy resource is never shorter. It is shorter when a pair
  cannot be read (Hanja without `encoding.dat`, invalid trail: 2 bytes -> `?`) or a truncated escape is dropped: 30 of
  33461 conversions in the harness, e.g. `CC EC C5 CC 50 47 BD 33 00` -> `3F ED 84 8D 50 47 3F 00`.
- Harmless: `o5_stringOps` bounds-checks against the new size.
- Fix the comment, or allocate `MAX(out.size(), size)`.

## Items the implementer left

- **Upstream GUI draw in scrolled rooms:** confirmed upstream behaviour (see M1). Not P6a's.
- **In-data CP949 guess** (C2/C3 A1-BF for strings of unknown encoding): acceptable as documented.
  - My first-round scan of MI1/DOTT floppy data stands: no real text affected.
  - The generated-CP949 run shows the vote adds no misreads beyond that ambiguity and one contrived case.
- **Tests not added:**
  - `toEngineText(dst == src)`: my harness covers it, and it holds.
  - `fitKoreanSentence` truncation / prep 0..3: low risk.
  - `cp.originals == u8.originals` in `korean_trs_data.h`: worth one line.
  - Add N1's and N2's properties when fixing.
- **Not run by me:** a legacy save taken mid-message, loaded in play (the converter is model-checked); C64 offsets (no
  data); v2 inventory with a renamed object.
