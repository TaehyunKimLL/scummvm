# P6a review fixes

Round after the reviews `P6a-review.md` (Opus) and `reviews/P6a-agy.md` (agy neon) of `0c6d8e45ced`. Every finding was
checked against the code; "REAL" means reproduced or shown by a failing test or by reading the code (the evidence column
says which). Commits are on `ko/p6-scumm`. Test totals and runs are in `P6a-notes.md` (section "Fix round").

| # | Finding (reviewer ids) | Verdict | Commit | Evidence |
|---|---|---|---|---|
| 1 | `getDialogCodePage()` CP949 for a UTF-8 engine: save names `??` (Opus M1, agy I2) | REAL | 0c5c864a0c2 | red: old build lists `2. ?? ??` (`runs/f1-savename-old`); green: `1. 한글 저장` (`runs/f1-sn1`, `~/work/tmp/ko-screenshots/scumm-p6a-fix-savename-hangul.png`); `test_dialog_code_page_follows_the_engine_text`. Side finding, upstream's and not fixed: in a horizontally scrolled room (`vs->xstart` 464) 2-byte GUI text is drawn at `x + xstart`, off screen (DBG print of `printChar`); the save list there shows no name, as for any 2-byte text in upstream |
| 2 | Old CP949 saves guessed, C2/C3 rows (Opus m4, agy C3) | REAL | 3f94ad7fa1a (save), 5bd988cd86a (bundle) | `test_legacy_resource_is_converted_not_guessed` (C3 A5, C3 B2 C3 A2); old MI2 slot 2 loads to the frame of upstream's load (bbox None, `runs/f2-mi2s2`, 18 resources converted). Bundle translations are now known UTF-8 (`knownUtf8`, `test_known_utf8_is_never_read_as_cp949`). Strings of unknown encoding (MI1/DOTT floppy data) keep the heuristic, documented in `korean_text.h`; the real occurrence count was NOT re-run, the Opus scan stands (MI1 1 hit inside bytecode, DOTT 0 of 80 runs) |
| 3 | Binary v3-v5 `rtString` destroyed by the save conversion (agy C1) | REAL by construction (the old code cut a resource to its first NUL) | 3f94ad7fa1a | `legacyCp949Resource()` converts only one NUL-terminated string that fills the resource, no control byte, a KS X 1001 syllable; `test_legacy_resource_leaves_binary_and_ascii_alone` (interior NUL, no-NUL binary, padding, no terminator); nothing shrinks. No synthetic save was built, the converter is tested directly |
| 4 | `_charsetBuffer` 256 of 512 bytes, `_charsetBufPos` after conversion (agy C2, Opus m5) | REAL | b8ac3b25928, 3f94ad7fa1a | TENC kind 2 carries bytes 256..511 (kind 0/1 still load, a cut kind 2 loads without tail); the buffer is zeroed before a load; `legacyCp949Buffer()` converts the buffer and maps the position; tests `test_trailer_carries_the_rest_of_the_message_buffer`, `test_legacy_charset_buffer_remaps_its_position`. Not played mid-page (no such save), the new-save round trip is in the notes |
| 5 | `scummEscapeLength` reads 0xFE as an escape (Opus m2, agy I7) | REAL | 17a4810df5d | 4 MI1 strings; scan of all bundles: FE as lead only with codes 1 and 8 (MI2 318+64, MI1 UTE 18+17), as trail 4 (MM) 24 (Zak) 0 0 2 (Indy4) 83 177; `scummTextEnd` is FF-only, FE an escape only with 1, 2, 3, 8; `test_trail_byte_fe_is_no_escape` |
| 6 | Truncated escape copied, idempotence (Opus m1) | REAL | 17a4810df5d | `test_truncated_escape_is_dropped`, `test_boundary_is_idempotent_and_clean_on_hostile_input` (20000 inputs, failed on the old code); a bundle string ending in its escape's terminator keeps it |
| 7 | One bad byte makes the string CP949 mojibake (Opus m3) | REAL | 17a4810df5d, 5bd988cd86a | `test_one_stray_byte_does_not_make_utf8_text_cp949` (MI1 UTE line 334 text); a text with more Hangul as UTF-8 than as CP949 is read per character |
| 8 | `ko/` detection case-sensitive (Opus m6, agy I3) | REAL | 7eab251d213 | `--detect` on a scratch MM with `ko/KOREAN.TRS`: `V2/DOS/English` before, `V2/DOS/Korean` after; the bundle loads (`runs/a11-ci`). No unit seam (FSNode) |
| 9 | `getCharWidth` short-circuit, no `_numChars` guards (agy I1) | REAL by reading | 1a4ee2b171c | not reproduced with a synthetic 128-glyph charset: no seam, and real v2/v3 Korean text holds no U+0080..U+00FF. The v3 count byte 0 means 256 (guard accounts for it). Smoke reruns of MM, Zak, Indy3, MI1 in the notes |
| 10 | v2 verb hit boxes from byte length (agy I4) | REAL | f90ece39fa0 | MM debug print: verb 1 `7` bytes, drawn 0..16, old formula box 0..48; now 0..16 (all 15 verbs equal their drawn rect). The xdotool click test was not run |
| 11 | `addLinebreaks` stale `strLength`, no capacity (agy I5, Opus out-of-scope note) | REAL (also upstream) | bea082e49ef | `test_korean_line_break_insertion_keeps_the_string` (three breaks; capacity limit) |
| 12 | v2 inventory `strncpy(..., 18)` cuts UTF-8 (agy I6, Opus m3) | REAL by reading | 7ef66251a84 | `test_copy_chars_cuts_on_a_character`; not reached in play (needs a renamed object) |
| 13 | C64 sentence offsets, `koreanCharStart`, `koreanCharLength` (agy M1-M3) | REAL by reading | 2fbecfe3208 | `test_char_length_validates_the_sequence`; the C64 offsets are not run (no C64 data); `koreanCharStart` removed |
| 14 | `trscheck.py` quadratic, census ignores bad UTF-8 (agy M4); test comment (Opus n2) | REAL | 4f25ebc3e34 | MI1 UTE pack census: 2 unreadable bytes, exit 1 |

Not fixed, with the reason
- Opus n1 (charset glyph width) is item 9. Opus n2 diff budget: the fixes add more upstream lines (see `PATCHES.md`), the
  doc comments of `scumm.h` were not moved.
- agy M5 test gaps: added tests for items 1-7, 11-13; not added: `fitKoreanSentence`'s character-truncation fallback and
  prep values 0..3, `toEngineText(dst == src)`, `cp.originals == u8.originals` in `korean_trs_data.h`.
- Upstream GUI draws 2-byte text at `x + xstart` in a scrolled room (item 1 note): not P6a's, not touched.
- The ambiguity of C2/C3 A1-BF for in-data CP949 strings of unknown encoding stays a documented heuristic (item 2).
