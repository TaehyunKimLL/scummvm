I'll re-review the fix round. Let me read the fix diff and notes first.
I've read the full fix diff, the implementer's note, and the adjudication record in the checkout (`P3-ext-review.md`, `P4-fixes.md`). Here is my re-review.

## Findings from my earlier review

**I1 (OS-IME syllable / first jamo double-processed on a freshly-opened prompt) — not addressed, correctly adjudicated FALSE.**

The fix diff leaves the `adopt()` + `key()` double-call in `texteditChangeUtf8` unchanged (`controls16.cpp:189-195` still calls both), consistent with "left unchanged". The adjudication (`P3-ext-review.md:253-264`) is sound: my finding required the *same* key-down that opened the prompt to reach `texteditChangeUtf8` again as `eventObject`, which does not happen — the SCI0 `User` handler claims that key-down, writes its character into the line, and opens the dialog whose loop then asks for *new* events. `adopt()` takes over the character already in the text; `key()` handles a different, later event. Play evidence (`imeA/` → `문` once, `keyA/` → `ㅁ` once) confirms no double-insertion. I accept the adjudication. The genuinely-related problem (the OS-IME code point could already belong to a later drained key) is a different finding (agy 8), and it *is* fixed here via `SciEvent::osImeChar` travelling with the event (`event.cpp:344-350`, `getSciEvent:503-511`).

## New breakage introduced by the fix round

None of Critical or Important severity. The round is a careful, well-tested rework: `utf8CopyPlan`/`dropIncompleteTail` (bounded, grow-only-on-unbounded copy), the `arrangeTokens` `plainUnits`/pop clamping, `shouldArrange` gating, the endian-correct `sci11StringsStart`, `keyOf` room scoping, the `kDrawStatus` translation hook, the SCI32 font-1001 guard, the shift-once fix, and the Alt-key/OS-IME-drain input fixes. I checked the areas most likely to regress:

- **`SegManager::strncpy` / `strcpyBounded` / `growDynmem`** (`seg_manager.cpp:695-804, 950-988`): the copy plan never writes past `maxSize`; grow is only requested for `dest.getOffset()==0` DYNMEM blocks, the SCI16 `>0xFFFF` cap is in place, and the source is always copied out before any `realloc` that could move it (no use-after-free).
- **`SciEvent` aggregate initializers**: all six are updated with the new `osImeChar` field (verified by grep), so no uninitialized read.
- **`getCurrentCallOrigin().scriptNr`** (`kstring.cpp:174`): valid API (`state.h:211`, `SciCallOrigin::scriptNr`), and reached only for the three answer rooms (never KQ1).
- **`kDrawStatus`** (`kmenu.cpp:82`): `textReference` is null-checked before `strSplitHeap`.

One negligible nit (Minor, not actionable): `utf8CopyPlan`'s `dropIncompleteTail` (`utf8.cpp:181-187`) classifies lead bytes `0xC0`/`0xC1` and `0xF5`–`0xFF` as multi-byte leads (`need` 2 or 4) and drops them at the tail of a finite-`n` copy, whereas `decodeUtf8Char` treats those bytes as lone bytes (`bytes=1`). The divergence only affects already-invalid UTF-8 (which never occurs in a Korean translation), so it has no real-world effect and does not warrant a change.

VERDICT: APPROVE
