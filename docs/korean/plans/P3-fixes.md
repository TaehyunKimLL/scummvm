# P3 review fixes

Findings of `P3-review.md` -> commit -> evidence. Builds: `$BLD` = `~/work/tmp/scummvm2026-p3build`.

| Finding | Commit | Evidence |
|---|---|---|
| M1 string table found case-sensitively; CP949 silently | f5d8f52909b | red: `test_translationFolderWithoutTableIsUtf8` failed (kCp949, no note); green after. The review's `upper` pack (`ko/SCI-KO.STR`, `~/work/tmp/p3-review/upper`) rerun headless: Hangul status line `득점: 0 of 158 / 킹스 퀘스트 I`, table loaded (log: `sci-ko.str has no fingerprint header ... loaded unchecked`, `korvocab loaded 1265`). Maps-only `ko/` (`pack-nostr`): `Text mode: translation folder has no sci-ko.str` and `WARNING: The ko translation folder has no usable sci-ko.str: script strings stay in English` |
| M2 kStrAt write corrupts UTF-8 | b02c20eea39 | red: the 8 new `SciUtf8TestSuite` cases did not compile (`utf8ReplaceAt` missing); green: ASCII over Hangul `A나`, Hangul over ASCII `가bcd`, Hangul over Hangul, NUL ends, at end appends, past end = byte offset (kUtf8ByteOffset), between the counts refused, buffer limit cuts the tail on a code point boundary |
| M3 kFormat result unbounded | 6ea683f95dd | see "M3: what is bounded" below; `utf8Truncate` unit-tested; headless: the diagnostic fires once in the KQ1 first room (`kFormat: the Korean result (48 bytes) is longer than the English one (44 bytes) and the buffer at 0010:0028 has no known size ...`) and the run stays byte-identical |
| m1 transient `language` key | 8e13e94cbbf | restores the old transient value or removes the key; no unit test possible (ConfMan); the headless runs still detect and start the Korean target |
| m3 `max` is bytes | documented | see below |
| m4 `kGetSaveFiles` cut | 16f9be227ab | `utf8Truncate` test: a 36-byte field holds 11 syllables (33 bytes) |
| m6 fallback tree after a failed primary parse | not changed, documented | below |
| m7 strcheck NUM / BOM | 73d00c8bac1 | red: `test_leading_zeros_like_the_engine` failed; 5 tests OK; no literal U+FEFF left in `tools/korean/*.py` |

## M3: what is bounded and what is not

- **Bounded (cut on a code point boundary, with a `warning`)**: a destination in a dynamic memory block (`SEG_TYPE_DYNMEM`),
  a SCI32 array, or a script string (the rest of the string that holds the pointer, `Script::stringBytesAt`, from the
  string table `identifyOffsets()` builds). `SegManager::strcpyBounded()` does it for `kFormat`; `kStrAt` writes pass the
  same capacity to `utf8ReplaceAt`. `SegManager::strncpy` still cuts at the segment end for any UTF-8 copy (now through
  `utf8Truncate`).
- **Not bounded (diagnostic only)**: a destination on the stack or in the script's locals/globals. The VM does not know
  the size the script declared, so only the segment end bounds it. `kFormat` formats the English text too and warns once
  per (format, length) with the key, both lengths and the destination when the Korean result is longer than the English
  one. A longer result can still overwrite what follows such a buffer. The real fix is the harness shortening the
  translation or a per-game capacity table (P4).
- UNVERIFIED: the bounded cases were not hit by a KQ1 run (the first room's Format goes to a stack/local buffer); they
  are covered by the unit tests of the string logic (`utf8Truncate`, `utf8ReplaceAt`) and by reading; `stringCapacity()`
  itself needs a running SegManager and has no test. A text resource `Format` (segment 0 source) prints the format
  as `0000:0000` and `-1:-1` in the warning.
- Cost: with a table loaded every `kFormat` formats twice (negligible; the buffer is 4 KiB).

## Not fixed, and why

- **m2 (Han/Yeong keymap defaults)**: policy, the design section 6 list; to be set by the owner (a smaller default set).
- **m3 (edit `max` = bytes)**: kept as is. It is the safe reading: the script's buffer is `max + 1` bytes, so a Korean
  line holds `max / 3` syllables (KQ1 parser: 13; a 20-character save name field: 6). More syllables need a bigger buffer
  (M3 / P4).
- **m5 (HiRes font height >= 8 px)**: input for the P4 plan: with the 16 px fallback face `GfxFontHiRes::getHeight()` is at
  least `ceil((ascent + descent) / 2)`, so a 6 px SCI font (menus, status bar, text boxes of P4 games) gets taller lines
  unless the game's map routes those ids to a smaller face or leaves them to `original`.
- **m6**: the Korean fallback tree is built only after a successful primary parse, exactly as on `jamo` (`grammar.cpp`
  there: `_koreanFallbackValid = parseGNF(...)` after the result tree) and as DESIGN line 392 describes (`said()` retry on
  the second tree). When the primary parse fails `kParse` calls `syntaxFail` and `said()` never runs, so a fallback tree
  could not be used without changing `kParse`. To be measured with k16said (Task 20) before changing.
- **m8 (thumbnails and the pre-transition frame hold no hi-res text), m9 (`undo()` after `adopt()` only)**: cosmetic /
  unreachable for a one-glyph line; recorded in `P3-review.md`.
- Nits: the per-pixel `clearLowresRect`, byte-counted `%-10s` padding and the TEXTEDIT default text are left as the
  review says (not trivial, no measured problem).
