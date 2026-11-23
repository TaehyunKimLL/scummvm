# P4 second re-review (Claude lane): fix round 2, 46fd164a9c9..48fdc6856af

Read: `P4-fixes.md` (round 2), `P4-notes.md` (the agy report itself was not needed: the finding is restated in P4-fixes.md),
and the code diff `git diff 46fd164a9c9 48fdc6856af -- . ':!docs'` (18 files, +214/-26) in the read-only checkout.

Own build of `48fdc6856af`, out of tree, ASan, via `heavy`:
- source `~/work/tmp/p4review-claude/src3`, build `bld3`;
- `make test` **696 tests OK**, 0 sanitizer lines; the pack suite warns and skips, as designed.

Runs are in `~/work/tmp/p4review-claude/r3-*`.

## What I missed last round

In the first re-review I checked `utf8CopyPlan()` for writes past the **buffer**. I did not check for writes past the **terminator** in each consumer of the plan.

The plan changed `n` from `0xFFFFFFFF` to `text.size() + 1`. That turned upstream's guard `(size_t)maxSize > n`, which used to be false for an unbounded copy, into true. The raw -> non-raw branch therefore wrote a second NUL at `n`, into the next byte of the stack/locals array. That byte can be the low byte of the next `reg_t`, and `setChar` also clears that slot's segment.

ASan cannot see this, and my runs did not exercise it. It is a real miss on my part.

## Critical item: **addressed**

`24ca5a5e60b` moves the raw -> non-raw store into `storeString()`/`CharSink` (`utf8.cpp:172-186`).

**UTF-8 games.** The trailing NUL is written only when the loop wrote no terminator, and never at or past `maxSize`. Every case, traced against the plan:

| Case | Plan | `storeString` | Result |
|---|---|---|---|
| Unbounded | `limit = size + 1`, text fits under the capacity | loop writes text + NUL | no extra write |
| Finite `n`, text shorter than `n` | `limit = MIN(n, cap)` | terminated in the loop | no extra write |
| Finite `n`, text exactly `n` (no NUL within `n`) | `limit = n` | NUL at `n` only if `maxSize > n` | upstream's semantics |
| Cut text | cut to `< cap` | terminated | no extra write |
| Odd pointer (`skipByte`) | — | indexes logically; `setChar` adds the skip; `LocalVariables`/`DataStack::dereference` already subtract 1 from `maxSize` | bound stays correct |
| BE | — | parity is handled inside `setChar` | unchanged |

**Non-UTF-8 games (English targets).** `storeString(..., utf8 = false)` is the upstream loop plus upstream's `maxSize > n` NUL. The only textual difference is that a `maxSize <= 0` reference is passed as 0, so the final NUL is no longer written there. A valid non-raw reference never has a negative `maxSize`: `LocalVariables` returns no `reg` then, and `DataStack` would point past its array. So English output is byte-identical for every valid pointer.

The other branches of `strncpy`, for the same class of bug:

| Branch | Path | Verdict |
|---|---|---|
| raw -> raw | `forwardCopy<true>(raw, src, limit)`; `limit` never exceeds the capacity | no extra NUL: an unbounded copy hits the NUL with `n == 0`, so padding does nothing. A finite copy pads only up to `MIN(n, cap)`. |
| non-raw -> raw, same-block raw (UTF-8) | copy-out into a `String`, then the `const char *` path with `MIN(n, size + 1)`; padding from the real `strnlen + 1` to `n`, inside `out.maxSize`, raw sources only | correct |
| non-raw -> non-raw (UTF-8) | new `i >= maxSize` stop | no write past the array, and no extra NUL |
| non-raw -> raw, non-raw -> non-raw (non-UTF-8) | untouched | byte-identical |

**Tests (`test/engines/sci/utf8.h`).** Applied to the 46fd164a9c9 code, the `storeString(t, "abc", 4, 16, true)` case fails: `mem[4]` becomes 0 where the sentinel 0xAA is expected. That is exactly the shipped bug: the plan's finite limit, not `0xFFFFFFFF`, triggers it. The `bounded` case catches a `>` written for `>=` in the array stop. The `shorter` case catches a missing NUL. The non-UTF-8 test pins upstream's NUL at `n`.

## New findings

No blocker and no major.

### Minor

1. **The Korean keymap's table check is narrower than the engine's lookup.**
   - Where: `metaengine.cpp:709-722`, from `32474a89593`.
   - The check looks only for `getChild("ko")` (case-sensitive on Linux) containing `sci-ko.dat`.
   - The engine finds the folder with `SearchMan.addSubDirectoryMatching` (case-insensitive). It loads `sci-<code>.dat`, or else `SCIKOR.DAT` from anywhere in the search path (`vocabulary.cpp:84-86`). DESIGN section 6 (line 376) even says the composer is on by default when `ko/SCIKOR.DAT` exists.
   - So a pack with `ko/SCIKOR.DAT` (the older harness naming, e.g. `dist/win-staging/scikor.dat`), or a `KO/` folder, gets the composer **on** (table loaded, `hangul_start` true) but **no Han/Yeong binding at all**. The player cannot switch to Latin input, e.g. for KQ4's "type the manual's English word" copy protection.
   - All current packs under `~/work/tmp/p4/data*` and the KQ1 pack use `ko/sci-ko.dat`, so this is latent. UNVERIFIED in play.
   - Fix: share one helper "would the word table load" between the engine and `initKeymaps` (both names, case-insensitive folder and name). Or register the keymap for every `language=ko` target and make the action a no-op while the composer is off. The latter still swallows the keys, so prefer the helper.
2. **Integration coverage gaps (not a bug found).**
   - Nothing tests that `SegManager::strncpy` passes `getTextMode().utf8()` and the plan's `limit` into `storeString`; that wiring is exactly where the original bug sat. A wrong flag or a `maxSize` in `reg_t` units would pass every test.
   - The `maxSize == n` case of an unterminated finite copy is not pinned either: a `>=` for `>` in the final NUL condition would pass.
   - The engine-free `storeString` is the right unit. Add the two cases. Also add one case that composes `utf8CopyPlan()` + `storeString()` the way `strncpy` does: unbounded `"abc"` into a 16-byte array, expecting the sentinel at 4.

### Nit

3. **The UTF-8 non-raw -> non-raw stop at `maxSize` writes no NUL and may stop inside a code point** (`seg_manager.cpp:797-798`). It is reachable only when the source is longer than the whole rest of the destination's stack/locals array. Readers are bounded by `maxSize`, so at worst one garbage glyph. Optionally round down to a code-point boundary.
4. **`adopt()` leaves `_runStart = 0` after a failed first key.** It is harmless: `_composing` is false, and `_runStart` is set again at the next run start (`korean_input.cpp:130`).

## Other round-2 changes: checked, no finding

- **`Key::inRoom()`.** It gives a key with no room the current room, and keeps the room of a tag made in room A when the buffer is drawn in room B. That fixes my nit 5 from the last round. `lookupPlace` still falls back to any-room.
- **`adopt()` snapshot.** It restores `text`, `cursor`, the composer and `_composing` when the first jamo does not fit. A unit test covers it.
- **Edit-control cursor clamp (`kgraphics.cpp:946-954`).** It counts code points only for UTF-8; non-UTF-8 is `text.size()` as upstream.
- **`kStrAt` past the buffer.** UTF-8 only; it now returns `r_acc` with upstream's warning, as the byte path does for KQ5's 0xFFFF.
- **`kFormat` diagnostic.**
  - It looks for a key only for heap formats (segment != 0). The text-resource format (`lookupText(.., 0)`) no longer passes as a script string.
  - The once-only map is now `SciEngine::_formatWarned`, so it is reset per engine instance. A public member is acceptable for a diagnostic.
- **Empty folder code.** `sci.cpp` skips both `addSubDirectoryMatching("")` and the `sci-.str` lookup.
- **`strcheck.py`.** It compares keys numerically (`007` = `7`); the non-numeric lines are already rejected above. A test was added.
- **Answer-room comments.** The comments now state the phrase and hut limits. These were my last round's minors 1-2, left as documented limits. I accept that for P5.

## Re-runs on 48fdc6856af (ASan)

- **QFG1 VGA Korean** (`r3-qv`): 0 AddressSanitizer lines, `grown from 68 to 159 bytes`, the notice renders.
- **QFG2 Aziza's door, question #11:**
  - `그리핀` reads as "" and is refused (`청하지 않고 온 낯선 이는 만나지 않네.`, `r3-door-griffin`).
  - `공기` reads as "air" and the riddle follows (`r3-door-air3`; two earlier `air` runs had only missed the last screenshot because the ASan build was slower than the 8 s wait; with 10 s and more screenshots the riddle screen is there).
  - 0 `cut to` lines, 0 AddressSanitizer lines.
- **English targets:** byte-identical by construction for every changed line. The non-UTF-8 paths are upstream code or the pinned `storeString(..., false)`. I did not repeat English runs this round; round 1's `r2-qv-en`/`r2-qfg2-en` were clean.

VERDICT: the critical NUL-past-terminator regression is addressed; no new blocker or major (2 minors: the keymap table check, and integration-test gaps; 2 nits). 48fdc6856af is a sound base for P5. Fix minor 1 before shipping a pack with `SCIKOR.DAT` or a `KO/` folder, and add the minor 2 tests the next time `strncpy` is touched.
