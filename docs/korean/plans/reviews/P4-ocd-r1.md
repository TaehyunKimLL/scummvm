I've read the fix diff in full, the implementer's note, and the checkout's `P4-fixes.md`, then re-verified the changed code against the actual source in `/home/thkim/work/tmp/scummvm-review-p4`. Here is the re-review.

---

## Findings I raised earlier — disposition

**1. `growDynmem` finite-`n` heap overflow — ADDRESSED.**
`engines/sci/engine/seg_manager.cpp:702-720` + `engines/sci/utf8.cpp` (`utf8CopyPlan`, `dropIncompleteTail`). The raw `growDynmem(strnlen+1)` on a finite `n` is gone. `utf8CopyPlan()` grows *only* the unbounded copy (`n == 0xFFFFFFFFU`), clamps a finite `n` to `min(n, capacity)`, cuts on a code-point boundary, and `dropIncompleteTail` rounds a finite `n` that ends mid-syllable down. The exact failure I described (`StrCpy(dest, src, 100)` into a small block) is now covered by `test_copyPlan_finiteLimitIsClampedToTheBuffer` (limit clamps to the 16-byte buffer). Evidence is concrete: QFG1 VGA (the one data path that actually grows) runs under ASan with `grown from 68 to 159` and 0 reports.

**2. `int16 room` vs `uint16 currentRoomNumber()` — ADDRESSED.**
`KoreanAnswerRoom::room` is now `uint16` (`korean_parser.h:655`); the test asserts `sizeof(aziza->room) == 2u`.

**3. Answer view hijacking non-search scripts — ADDRESSED.**
`koreanAnswerView` (`kstring.cpp:160-181`) now gates on `row->appliesTo(s->getCurrentCallOrigin().scriptNr)`, and `englishLine` emits only the room's `words` list (`korean_parser.cpp:315-350`), so a synonym that merely *contains* a search word no longer passes (test `test_englishLineIsNotASubstringSearch`). Confirmed in play: `answer view of "두드려" not used: read by script 255`.

**4. `kStrLen` view-size truncation to `uint16` — ADDRESSED (moot).**
The view is now bounded to the room's short `words` list, so the truncation I flagged is no longer reachable.

**5. Alt+S leaking `ㄴ` into the composer (follow-up) — PARTIALLY ADDRESSED.**
`KoreanEditLine::key()` now takes an `alt` parameter and returns `kIgnored`, and `texteditChangeUtf8` passes `(modifiers & kSciKeyModAlt)`. The note honestly records this as "unresolved" — the key was never actually seen by the engine, so the true source of the stray `ㄴ` is still open, but the defensive guard is in place and unit-tested.

---

## New breakage / remaining concerns

### Important

**6. QFG1 hut (room 22) search-word list is unverified, and phrase-order likely breaks a natural Korean answer**
`engines/sci/parser/korean_parser.cpp:586` (`kHut = { "hut", "of", "brown", "now", "sit", "down" }`), used by `englishLine` at `:317-336`.

`englishLine` now emits search words in **typed-token order**, and `koreanAnswerLine` deliberately tokenizes with `arrange=false` (`vocabulary.cpp:773`). The hut rhyme is searched as the English phrase "hut of brown, now sit down". A Korean answer typed in Korean word order (noun-before-adjective, verb-last, no separate "of"/"down" tokens) maps to something like "brown hut now sit" — which does not contain the contiguous phrase "hut of brown" and lacks "of"/"down". If script 802 does a phrase/substring search, the row fails. The note itself says the hut "was not reached" and the full gargoyle pass is "still unverified".

This is the same acceptance item I flagged last round (checklist #5), and the fix round does not close it. It is data-level rather than a code defect, and the plan explicitly permits removing the row with evidence. **Fix:** run room 22 (and the full gargoyle pass) with the harness data; if a natural Korean rhyme answer does not pass, either adjust `kHut`/the translation to align the word order, or remove the room-22 row with the run as evidence.

### Minor

**7. Per-room search words are now hardcoded engine data.** `kAziza`/`kGargoyle`/`kHut` (`korean_parser.cpp:578-586`) grow the "one small table" from 3 rows of (game, room) to 3 rows plus ~50 search strings. Adding or correcting any answer room now means an engine edit, not a data edit. Not a bug, but worth noting against the plan's original "one small per-game table, data stays in the harness" goal.

**8. `getCurrentCallOrigin()` inside `kStrAt`/`kStrLen`.** The call is correctly placed after the `koreanAnswerRoom` + non-ASCII gates, so it is cheap and only runs in the three rooms. No defect, just noting it is a workaround-oriented API now also used on the hot-ish answer-read path; it is confirmed correct in play (script 2 vs 255).

---

No new memory-safety or interface-mismatch defects found. The `strncpy` rewrite, `utf8CopyPlan`, the `SciEvent::osImeChar` field (all four aggregate initializers updated), the `sci11StringsStart` endianness parameter, and the case-insensitive pack lookup all check out on reading and are covered by the new unit tests.

VERDICT: APPROVE WITH CHANGES
