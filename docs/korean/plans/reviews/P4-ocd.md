I've completed a thorough read of the brief, the implementer's report, the change diff, and the spec/plan/notes/PATCHES files in the checkout, then verified the new code against the actual source in `/home/thkim/work/tmp/scummvm-review-p4`. Here is my review.

---

## Review scope verified

Read and cross-checked against the checkout: `docs/korean/DESIGN.md` (referenced sections), `docs/korean/plans/P4-sci-data-games.md`, `P4-notes.md`, `PATCHES.md`, plus the actual engine files (`seg_manager.cpp`, `kstring.cpp`, `korean_parser.{h,cpp}`, `vocabulary.{h,cpp}`, `script.cpp`, `script_strings.cpp`, `cache.cpp`, `scifont.cpp`, `savegame.cpp`, `segment.{h,cpp}`, `grammar.cpp`, `said.cpp`, `state.h`, `sci.h/sci.cpp`).

The diff matches the checkout. The answer-room design (a2/a3), the `sci11StringsStart` hook (a1), the font-set mask (a5), the same-block `strncpy` copy-out (a4 + A-fix), the fallback-tree preservation in `koreanAnswerLine`, and the save/restore of grown `DynMem` blocks all check out on close reading. The findings below are what I believe the implementer missed.

---

## Findings

### Important

**1. `growDynmem` can overflow the block it just grew (heap overflow)**
`engines/sci/engine/seg_manager.cpp:702-704` (growth), `:676-693` (`forwardCopy` zero-pad), `:768-770` (finite-`n` path).

The growth grows the block to `strnlen(src, n) + 1`, then falls through to `forwardCopy<true>(dest_r.raw, src, n)`. `forwardCopy` zero-pads to the full `n` bytes for any finite `n` (`zeroPad = STRING && n != 0xFFFFFFFFU`, line 678). So whenever `n > strnlen(src, n) + 1` — i.e. the caller asks to copy *more* than the source's length, the classic padded-`strncpy` case — the block is grown to `strnlen+1` but `n` bytes are written, overflowing the tail.

Concrete failure scenario: in a Korean (UTF-8) game, a script does `StrCpy(dest, src, 100)` where `src` is a 3-byte raw string in a *different* segment and `dest` is a 3-byte `DYNMEM` block (`kStrCpy` with `argc>2` → `seg_manager.cpp:105` → `strncpy(reg_t,reg_t,n)` → `else if (src_r.isRaw)` at `:768` → `strncpy(dest, src_raw, 100)`). The growth condition `strnlen(src,100)=3 >= maxSize 3` grows the block to 4 bytes and — critically — *skips the pre-existing cut block* (`:706-714`) that used to truncate safely to `maxSize`. `forwardCopy` then copies `문\0` and zero-pads to 100, writing 96 bytes past the 4-byte block.

Why it's not caught: the plan itself notes "No unit test is possible (SegManager needs a ResourceManager)", and every smoke run logged `0` `grown from` lines — so this path has never executed in any run, and no test would catch it (violates brief requirement 3 and 7). ASan is clean only because the growth is never triggered.

Fix (minimal and correct): the growth is only sound for the *unbounded* copy, where `forwardCopy` does not pad. For finite `n` the source is intentionally bounded to `n`, and the cut block is the right behavior — the engine cannot know the full Korean length past `n` anyway. Restrict the growth:

```cpp
if (g_sci && g_sci->getTextMode().utf8() && dest_r.maxSize >= 0 && n == 0xFFFFFFFFU &&
    Common::strnlen(src, n) >= (size_t)dest_r.maxSize &&
    growDynmem(dest, Common::strnlen(src, n) + 1))
    dest_r = dereference(dest);
```

`strcpyBounded` already copies via `strcpy_` (`n == 0xFFFFFFFFU`), so it keeps growing correctly; this change only stops the finite-`n` overflow.

---

### Minor

**2. `KoreanAnswerRoom::room` is `int16` but `currentRoomNumber()` is `uint16`**
`engines/sci/parser/korean_parser.h:99` vs `engines/sci/engine/state.h:176`.

SCI room numbers are unsigned 16-bit. Any future answer room numbered `> 32767` would be stored as a negative `int16` and never match (silently disabling the view and re-breaking that game's answer check). The three current rows (22, 29, 199) are unaffected, but the table field should be `uint16` (or `int`) to match the type the comparison is actually performed against.

**3. The answer view keys off any non-ASCII string in the room, not just the edit box**
`engines/sci/engine/kstring.cpp:160-171`.

`koreanAnswerView` applies to *every* `kStrAt`/`kStrLen` read on *any* string that contains a non-ASCII byte while in an answer room — not only the typed-answer edit box. The gate that a string must fully tokenize to known words (`koreanAnswerLine` returns empty → view disabled) narrows this, and in the observed QFG2/QFG1 runs only the edit box actually triggered it (the hero name "한영웅" tokenizes to nothing). But a translated string in one of these rooms that happens to be composed of known words would be silently replaced by its English words on read. Worth a one-line comment or an additional "this is the edit-box reg_t" check for the room's input, since the commit message over-claims ("Writes and every other room are unchanged" — true across rooms, but not across strings within the room).

**4. `kStrLen` answer view returns `view.size()` into a `uint16` reg**
`engines/sci/engine/kstring.cpp:596-599`. `make_reg(0, view.size())` truncates to 16 bits; a very long `englishLine` (a token hitting a large synonym group) could wrap. The answers in the table are short, so this is latent only — same truncation already exists on the non-view `kStrLen` path, so it is consistent, but worth a `MIN`/assert if the answer-table approach is ever generalized.

---

## Follow-up findings the brief asked me to judge (requirement 6)

These are already in `P4-notes.md`; I add severity and P3/P4 attribution only where the brief asks.

- **QFG1 VGA heap-corruption crash — Critical, but not P4.** It reproduces on the P3 binary, so it is *not* caused by a1 (the a1 hook is never reached). It is a P3 SCI1.1 hi-res-text bug. Consequence for P4: **Task a1's `sci11StringsStart` hook remains UNVERIFIED on real data** (its one data check is the crashing game). I judge the hook correct by reading (`sci11StringsStart` advances identically to `identifyOffsets`'s per-instance `numProperties*2`), but it has no end-to-end evidence.
- **Alt+S leaking `ㄴ` into the composer — Minor, P3-caused.** Concrete fix for the follow-up: in the Hangul composer's key intake (`engines/sci/event.cpp`, the `hangul_input`/HanYeong path), ignore keys delivered with the Alt modifier set, so the screenshot action's `s` never composes. This matches the note's own suggested direction.
- **LSL1 composer off (no word table) — Minor/Important, P3-caused** (`hangul_input` auto-off for parser-less games). No Korean input without an OS IME; `hangul_input=true` still untested.
- **LSL1 look-icon hides first syllables — Minor, layout**, not P4.
- **QFG1 gargoyle (29) full pass and hut (22) row UNVERIFIED — Minor test gap.** Per the plan, room 22's row should be removed (with evidence) if that room parses via `said()` rather than StrLen/StrAt; that check did not run.

---

VERDICT: APPROVE WITH CHANGES
