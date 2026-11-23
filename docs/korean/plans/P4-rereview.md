# P4 re-review (Claude lane): fix round 8bb55159d99..46fd164a9c9

Read: `P4-fixes.md`, `P4-notes.md` (corrections), `P4-review.md` (mine), and the code diff
`git diff 8bb55159d99 46fd164a9c9 -- . ':!docs'` (30 files, +514/-107), all in the read-only checkout
`~/work/tmp/scummvm-review-p4`.

I built `46fd164a9c9` myself, out of tree, with ASan, via `heavy`:
- source: `~/work/tmp/p4review-claude/src2`, from `git archive`;
- build: `~/work/tmp/p4review-claude/bld2`;
- result: `make test` **690 tests OK**, 0 sanitizer lines.

My run folders are `~/work/tmp/p4review-claude/r2-*`. The run driver is `~/work/tmp/p4/go.sh`.

## My findings

| Finding | Status | Evidence (my re-runs on 46fd164a9c9 + ASan) |
|---|---|---|
| M1: QFG1 VGA, copy from a stack source into a DYNMEM block | **addressed** | `r2-qv`: 0 AddressSanitizer lines; log `0032:0000 (kMemory() critical) grown from 68 to 159 bytes`; the notice renders in Korean (screenshot viewed). The fix (`seg_manager.cpp` copy-out for `!src_r.isRaw`) matches the one I verified, and it does not pad for non-raw sources. |
| M2: `englishLine` synonyms pass wrong answers | **addressed** | QFG2 room 199, question #11: `r2-door-griffin` logs `answer "그리핀" reads as ""` and the door refuses it (`청하지 않고 온 낯선 이는 만나지 않네.`, screenshot); `r2-door-air` logs `answer "공기" reads as "air"` and the riddle (#16) follows. |
| m1: `sci11StringsStart` endianness | **addressed** | `bigEndian` parameter from `g_sci->isBE()` (`173c9006f7f`); a unit test reads one heap both ways. The hook itself is still not checked on data (restore-dialog labels): UNVERIFIED. |
| m2: padding after a cut | **addressed** | Padding now starts at `strnlen(out.raw, out.maxSize) + 1`, inside the block, and only for raw sources. |
| m3: `hiResFont` masks SCI32 ids | **addressed** | The mask only applies below SCI_VERSION_2. |

## Review of the fixes: no new blocker or major

**Checked and sound**

- **`utf8CopyPlan()` and its callers.**
  - Only the unbounded copy (`n == 0xFFFFFFFF`) at offset 0 of a DYNMEM block may grow it.
  - A finite `n` is clamped to `maxSize` and rounded down to a code-point boundary.
  - The `non-raw` destination path gets `n = limit`.
  - In the same-block / non-raw copy-out, `text.size() > n` is cut first, so `strncpy(..., MIN(n, size+1))` keeps upstream's no-NUL semantics when a finite copy fills `n`.
  - If `growDynmem` refuses, the plan is recomputed without growth, so nothing is written past the block.
  - `strcpyBounded` and the `strAtUtf8` write go through the same `strncpy`.
  - I found no caller that keeps a raw pointer across a grow. Every `strcpy_`/`strncpy` caller passes an owned `Common::String`.
  - The `> 0xFFFF` refusal on SCI16 is correct.
- **`appliesTo()` and `getCurrentCallOrigin()`.**
  - The origin's script number comes from `xs->local_segment`, i.e. the script whose code runs. For a procedure or method of script 2 or 802 called from the room, that is 2 or 802.
  - It is confirmed in play: `answer view of "두드려" not used: read by script 255, the search is script 2` (DEdit) and `answer "키폰" reads as "keapon laffin"` (script 2), both in `r2-door-*`.
  - This is the same mechanism the workaround tables rely on for kernel calls.
- **Answer-room word lists.**
  - `kGargoyle` is exactly script 29's search strings 9-24 (harness `runs/qfg1e/src/script-class.tsv`).
  - `kAziza` is script 199's strings 1-8 and 16-27 (`runs/qfg2/src/script.tsv`), lower-cased, with the two phrases split into words.
  - `kHut` is script 22's strings 20-21.
  - Simulation over the harness `k3map.json`/`k2targets.json` (`scripts/fp2.py`): every QFG2 answer word has a Korean reading (`공기/대기/바람` → air, `전부` → all, `쥐/펜러스` → `rat fenrus mouse`, `키폰` → `keapon laffin`, `위트` → wit, `아지자` → aziza). No reading comes from a substring.
- **Article/role fix in `arrangeTokens`.** The `MIN` bounds remove the `pop_back` underflow. The emitted tokens now come from `plain`, the same tokens without duplicates.
- **Tokenizer gating.** `shouldArrange(korean, sawKorean)`, and `koreanWord` only when the Korean table is loaded. This removes P3's rearrangement of umlaut/CP866 lines in non-Korean parser games (an improvement for non-Korean targets).
- **OS-IME path.** The syllable now travels with the event (`osImeChar`), and `_lastOsImeKey` is set when the script takes the key-down. That is correct and inert when `_hangulInput` is off (`osImeChar` is 0).
- **Alt guard.** `KoreanEditLine::key(..., alt)` returns `kIgnored` before the snapshot. Harmless: Alt+letter is a shortcut in SCI, and the Han/Yeong RAlt is consumed by the keymap.
- **English targets.**
  - `kDrawStatus` now calls `strSplitHeap()`. With no translation loaded, `lookup()` is false, so it is `strSplitLanguage(str, nullptr, nullptr)`, the same as the old `strSplit(str, nullptr)`.
  - Every other change is behind `utf8()`, the hi-res object, the Korean table, or `language=ko`.
  - Re-runs on the new ASan build: English QFG1 VGA (`r2-qv-en`) and English QFG2 (`r2-qfg2-en`) give 0 AddressSanitizer lines and 0 Korean/hi-res/korvocab log lines, and both reached a screenshot.

### Minor (new)

1. **Phrase answers are split into words, but the search is a substring search.**
   - Where: `korean_parser.cpp`, `kAziza` (and `kHut`).
   - The comment says "a line holding them all contains the phrase". That holds only if the script searches word by word, which it does not: it searches for `Wizards Institute` and `Institute of Technocery`.
   - The view emits each token's matches in list order and in typed order. Korean has no token for "of", and `마법사` reads only as `wizard`, not `wizards`. So `마법사 연구소` reads `wizard institute`, and `테크노서리 연구소` reads `technocery institute`; neither contains either phrase.
   - For that question only `위트` → `wit` can pass. This also relies on script 2 matching case-insensitively, because the string is `WIT`: UNVERIFIED. The game picks its question by key timing, and none of my runs drew it.
   - Fix: let a row carry phrases. When the line's tokens cover a phrase's content words (`wizards`/`wizard` + `institute`, or `institute` + `technocery`), emit the phrase itself. Add a unit test.
2. **The hut row (QFG1 room 22) is inert.**
   - The Korean table has no reading for `brown`, `now` or `of`, so `갈색 오두막아, 앉아라` fails to tokenize, and the view stays empty.
   - The translation tells the player the English rhyme (`resource.msg.scitext.tsv:506`, `'Hut of brown, now sit down'(갈색 오두막아, 앉아라)`), and typed ASCII passes without the view.
   - No harm, but the row promises something it cannot do. Either add phrase rows as in item 1, or say in the row's comment that the rhyme is typed in English.
3. **The Korean keymap is still registered where the composer is off by default.**
   - Where: `metaengine.cpp:702-708`.
   - It only checks explicit `hangul_builtin=false` and `korean_text=false`. It does not check `hangul_input=false`, nor the auto default: `hangul_input` is off in a game without a word table, e.g. LSL1, as notes finding 2 says.
   - There, Shift+Space, Ctrl+Space, RAlt, F12 and ScrollLock are still swallowed for a composer that does nothing.
   - The keymap is built before the game is loaded, so the auto case needs either the table check at that point or a runtime keymap enable/disable.

### Nit

4. **The article tests only prove there is no abort.** `test_articleWithParticleBeforeNounUnit` asserts `words.size() >= 3`; the second test checks only the first group. Assert the arranged group sequence instead.
5. **`keyOf()` overwrites the room of a single-part buffer tag** with the current room (`state.cpp:372-373`). A buffer tagged in room A and drawn in room B then gets B's room entry for that string id. Keep the tag's room when the key comes from a tag.
6. **A destination with `maxSize <= 0`** (a pointer at the very end of a block) is still unbounded in `utf8CopyPlan`, as upstream is. Latent.

## The implementer's open items

- **Alt+S leak.** The debug line saw only the Alt key-down, and the saved `ㄴ` bytes (LB1) are real. So something turns a screenshot into an `s` jamo outside `getScummVMEvent`'s key path, or the harness's `xdotool key alt+s` delivers the `s` by another route (SDL text input). The Alt guard is harmless but cannot be shown to fix it.
  - This does not block P5: it is SCI-only, and AGI's composer has its own event path, which P5 should check from the start.
  - Recommend a card: reproduce with a real key press rather than xdotool (or log SDL `TEXTINPUT`) before changing code.
- **QFG1 gargoyle (full pass) and hut: unverified.** The word lists now match the scripts, and item 2 shows the hut row is inert by construction. A gargoyle pass belongs on the QFG1 EGA release card, not before P5.
- **Fixes verified by reading only** (shift, `getLongestUtf8`, undo, `keyOf` room, IME drain): I read the diffs and found them correct, apart from nit 5. All are SCI text/input code that AGI does not share.

## What must still be done before P5

Nothing blocking. Minor items 1-3 and the Alt+S card belong to the SCI follow-up (the QFG release work); P5 does not depend on them.

VERDICT: the fix round is sound. M1, M2 and m1-m3 are addressed (M1/M2 re-run on 46fd164a9c9 + ASan, 690 tests OK). There are no new blockers or majors, three new minors and three nits. P4 is a sound base for P5.
