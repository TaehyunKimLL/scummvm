# P6a third pass (second fix round), Claude lane

Reviewed: `4fd29b336e0..6191f7a246e` (`review-pkg-p6a-fix2/fix.diff`, `P6a-fixes.md` round 2), read-only checkout
`~/work/tmp/scummvm-review-p6a` at `6191f7a246e`. Everything was run, not read. Scratch: `~/work/tmp/p6a-review-claude/`.
- `build/`: ASan+UBSan SCUMM build of `6191f7a246e`. `scummvm-4fd29b3`: the same build of `4fd29b336e0`, kept as "before".
- `diff/`: the real-data differential.
  - `before/`, `after/`: the converter sources of each commit (`korean_text`, `trs_bundle`, `ksx1001_table`), each built
    with ASan+UBSan into its own driver `diffdrv`.
  - `mkrec.py`: collects the inputs. `cmp.py`, `expect2.py`: comparison and the independent expected output.
- `h/fuzz`, `h/fuzz2`: my harnesses, rebuilt on the final sources. `runs3/`: game runs.

## Verdict

**P6a can merge into ko-2026.4.** No blocker, no major, no new minor.
- The second round fixes N1-N4.
- On all real data on this host, the after-code differs from the before-code only where it is now right (2 strings)
  or on binary save bytes that are not text (4).
- The fuzzer's worse numbers (59 and 7117) come from inputs that no real text contains.

## Real-data differential (before = `4fd29b336e0`, after = `6191f7a246e`)

Each input goes through both drivers exactly as the engine uses it:
- **Bundle translation:** the CP949 body is converted at load (`convertTrsBodyToUtf8`), then each translation is copied
  through `toEngineText(knownUtf8)` with `srcMax = resStrLen + 1`.
- **Original:** `toEngineText` with unknown encoding.
- **In-data string:** `toEngineText` with unknown encoding.
- **Legacy resource:** `legacyCp949Resource`.

| Input | Strings | Before != after | Classification |
|---|---|---|---|
| Bundles: 13 distinct files: every `.trs` under `~/work/tmp/p6/data` (mm, mmkodir, zak, indy3, loom, indy4, mi2, mi2cp, mi2ute-win, mi1ute-u/-c/-win) and `~/work/scummvm/gamedata` (`mmkor`, `zakkor`, `loomkor`, `indy3kor`, `indy4kor`, `mi2kor` UTF-8 + `.cp949`, `mi1ute-kor` + `.cp949`, `mi2ute-kor`), deduplicated by MD5 | 58465 translations + 58465 originals | **0** | - |
| In-data strings: MI1 floppy (DISK01-04, small-header script/object blocks) and DOTT floppy (`TENTACLE.001` SCRP/LSCR/ENCD/EXCD/VERB/OBNA), every NUL-terminated text run with a high byte, plus text-like strings of the legacy saves | 21937 | **0** | - |
| Legacy saves: Loom s01, DOTT s01, MI2 s02 and s06 (CP949, made by upstream), 16 MI2 autosaves from plain 2026.4. Text-like NUL-terminated strings | 5597 | **0** | - |
| The same saves, every NUL-split piece with a high byte (binary included) | 31868 records | **6 unique** | **2 improvements**: MI2 option-screen strings with `FE 01` line breaks (`원숭이섬 2 ... FE 01 ...`). Before: not converted (rejected at the control byte 01). After: converted, equal to the independent expected output. These are the "18 -> 20 converted" in the MI2 s02 load log. **4 neutral**: unknown-encoding reads of binary save-state bytes (control bytes inside), not text |

Real-data regressions: **0**.

Absolute check of the after-code against an independent expected output:
- How the expected output is made: Python's `cp949` codec for KS X 1001 syllables, `?` for pairs that need `encoding.dat`,
  a lone byte becomes U+00xx, escapes are kept, a truncated escape is dropped.
- **44337 of 44337 CP949 bundle translations** match.
- **607 of 607** text-like legacy resources match.
- In-data strings: 10281 match, 10 do not. All 10 are 2-4 byte bytecode fragments that look like C2/C3 UTF-8 (`c3 a3`,
  `40 c3 8c`, `c2 80 5d 65`, ...), identical before and after: the documented ambiguity, no real text.
- **Game frames** (`runs3/load/*-cmp.png`): legacy saves loaded in plain 2026.4 and in this build show the same text for
  Loom, DOTT and MI2 slot 2; differences are animation and cursor only. 0 sanitizer reports.

### The fuzzer's 59 and 7117

- **59 generated-CP949 misreads** (`h/mis.txt`): a valid 3-byte UTF-8 Hangul (lead E0-EF) appears in all 59.
  - In 53 its first byte is a lone charset-glyph byte E0-EF from the generator.
  - In the other 7 it starts on a trail byte after a sequence misaligned by such a lone byte.
  - Real CP949 Korean text has leads B0-C8 only, and its lone glyph bytes (MI1 0xFA) are invalid UTF-8 leads.
  - The 531 real in-data strings that do carry lone C2-EF bytes next to Hangul convert identically before and after
    (the 0 above).
  - None are in the C2/C3 rows.
- **7117 UTF-8-with-strays wrong (unknown encoding):** UTF-8 lines without Hangul, ambiguous by construction. Every
  source of known UTF-8 is read as UTF-8 (0 wrong); unknown sources on this host are CP949 game data and binary.

## Findings of the second review

| Finding | Status | Evidence |
|---|---|---|
| N1 V2 charset `_numChars` | **addressed** | Same repro as before (one lone `0x80` in a copy of the MM bundle, `data/mmfa`), final build: **0 ASan reports**. `열A` is drawn; U+0080 is refused, not read (`runs3/mmfa/scummvm-mmko-00000.png`). See the `_numChars` audit below |
| N2 known/unknown stability | **addressed** | Hostile 300000 in `fuzz2`: **0 idempotence failures**, including known -> unknown re-entry. `fuzz` 300000: 0 idempotence, 0 invalid outputs, 0 failed checks |
| N3 `memmove(nullptr, 0)` | **addressed** | `if (at)` guard; 0 UBSan reports in `fuzz`/`fuzz2`/the differential/`make test` |
| N4 "never shrinks" comment | **addressed** | Comment corrected. Shorter outputs are still 29 of 33437 (unreadable pairs -> `?`), harmless |

**`_numChars` audit** (`grep _numChars`):
- `CharsetRendererCommon` starts it at 0. Classic (v4-v6, and TownsClassic) reads the 16-bit count from the header. V3
  (and PCE, TownsV3) reads the count byte. V2 now sets 128 (`englishCharsetDataV2`/replacement copies) or the Russian
  table's size.
- NES, Mac, V7, Nut never take the Korean UTF-8 path (`_koreanUtf8` is v1-v6 DOS Korean only) or use their own tables.
- The guards (`getCharWidth` Classic/V3, `prepareDraw`, V3 `printChar`/`drawChar`) are `_koreanUtf8 && chr >= 0x80 &&
  chr >= _numChars`. They fail closed: no width, nothing drawn. A V3 count byte of 0 now refuses 0x80+, which is safe
  (upstream's `_widthTable` would be wrong for 0 anyway).

## Remaining items

- **Escape rule consistency** (grep): `scummTextEnd`, `cp949TextEnd`: 0xFF only. `textEscape`: 0xFF with any code,
  0xFE with 1/2/3/8, used by `cp949ToUtf8`, `utf8HangulCount`, `cp949PairCount`, `tolerantUtf8`, `cutOnBoundary`,
  `legacyCp949Resource`, `legacyCp949Buffer`, `koreanCopyChars`. `utf8Valid` inlines the same rule. Two exceptions:
  - `koreanFinalBits` (`korean_text.cpp:488`) still uses `scummEscapeLength` (0xFE with any code). It only reads engine
    output, where a raw 0xFE outside an escape cannot occur (it is written as C3 BE), so the result is the same. Nit:
    make it `textEscape` for uniformity.
  - `trs_bundle.cpp:77` uses `scummTextEnd`, unchanged and clipped at the next entry, as before.
- **Idempotence on the final code**: hostile, generated and `fuzz` runs: 0 failures. In place (`dst == src`): equal to
  out of place. Cuts on character/escape boundaries: 0 violations. `legacyCp949Buffer` model check 100000: 0 problems.
  `koreanInsertBreak`/`koreanCopyChars` 100000 each: 0 problems.
- **Unit tests** in the ASan+UBSan build of `6191f7a246e`: **684 OK**, 0 `runtime error`.
- **Not repeated**: English MM V1 and the verb hit-box scans (this round's charset change is behind `_koreanUtf8`;
  pass 2 showed them unchanged). A legacy save taken mid-message is still not reached in play (model-checked only).
