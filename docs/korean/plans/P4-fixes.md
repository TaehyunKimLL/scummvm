# P4 fixes: the review round

Inputs: `P3-ext-review.md` (adjudication of the external P3 findings), `P4-review.md` (Opus), the raw external outputs in
`reviews/` (`P3-agy.md`, `P3-ocd.md`, `P4-agy.md`, `P4-ocd.md`). Base `8bb55159d99`. Every fix is one local commit with the
red/green evidence named; counts: `make test` 679 -> 690.

| Finding | Verdict | Commit | Evidence |
|---|---|---|---|
| P3 agy3 (blocker): article + case particle, `Array::pop_back` abort | confirmed | `95c619e2304` | red: two new unit tests aborted the runner with the `pop_back` assertion (second test), green 11 tests; in play (KQ1 first room, `rmdprp ans dksdmf qhk` and `rmdprp ansdprp wnj`): reply `그 생각을 바꾸도록 하세요, 그래햄.`, no assertion; `문을 열어` still gives `당신은 데번트리의 잃어버린 보물 세개를...` as in the P3 notes |
| P3 agy1: arrange() on any byte >= 0x80 | confirmed | `3117e6fb278` | `KoreanParser::shouldArrange()` test (compile red); the tokenizer needs the table |
| P3 agy8: OS-IME code point sampled at drain time | confirmed | `3d5a7a700c5` | `SciEvent::osImeChar`, set from the returned key-down; unit test of `osImeCodeOf()`; the draining needs the backend: by reading only, IME not testable under Xvfb |
| P4 Opus M1: QFG1 VGA heap corruption | confirmed (ours) | `ddc58af950e` | ASan on QFG1 VGA data: 0 reports, `grown from 68 to 159`, notice renders in Korean (`y-qv`); notes finding 4 corrected |
| P4 agy1 + ocd1: padding past a grown block | confirmed | `ddc58af950e` | `utf8CopyPlan()` (grow only the unbounded copy, clamp finite n, boundary cut): 3 unit tests; SegManager has no test fixture (needs a ResourceManager), so the plan is engine-free and tested, the copies checked by ASan runs of QFG1 VGA, QFG2, LSL1, KQ4, LB1, Camelot (0 reports) |
| P4 Opus M2 + agy #3: synonyms pass (그리핀 = "air") | confirmed | `295f19ec037` | red-by-reasoning before (reviewer's run); now `그리핀` reads as empty, `공기` as `air`; unit tests incl. particle (`공기로`) and `문에`; Aziza door headless: `공기` passes #11, `그리핀` refused (`청하지 않고 온 낯선 이는...`), `키폰` then `아지자` opens the door; the tokenizer no longer arranges |
| P4 agy #2 + ocd #2: view hijacks other scripts, room is int16 | confirmed | `295f19ec037` | row carries the script (QFG2 2, QFG1 802, both confirmed with the call origin: `answer view of "두드려" not used: read by script 255`); `appliesTo()` test; room is `uint16`. A Korean save name in slot 0 on slot 1: unit-tested through the decision function only, not in play |
| P3 agy5: shift applied twice | confirmed | `c6562e30d7a` | red: test showed 4 px apart for shift 2; green 2 |
| P3 agy9(a): undo only wider edits | confirmed | `64810969c32` | by reading (needs GfxText16) |
| P3 agy7: `getLongestUtf8()` returns 0 | confirmed | `4dda2c5929d` | by reading (the adjudicator's repro numbers are in P3-ext-review.md) |
| P3 agy6: `keyOf()` room | confirmed | `73629356c6f` | by reading; lookup order already tested |
| P3 agy12: keymap swallows keys with the composer off | confirmed | `12f2acc1c37` | keymap left out for `hangul_builtin=false` / `korean_text=false` |
| P3 agy14: `kDrawStatus` | confirmed | `704dc45b4d1` | `strSplitHeap` |
| P3 agy2: non-raw -> raw bound, StrCpy length on a code point | partly | `ddc58af950e` | done once with M1/#1 (copy-out + plan: clamp, boundary round-down); the raw -> non-raw and non-raw -> non-raw loops of upstream stay |
| P3 agy10: SCI2+ with kUtf8 | out of scope | `f97f463f914` | one-line guard in `SwitchToFont1001OnKorean()` (SCI32 build links) |
| P4 agy #4: `sci11StringsStart` endianness | confirmed | `173c9006f7f` | `bigEndian` parameter, test reads one heap both ways |
| P4 agy #5: pack test lookup | confirmed | `07a89e06313` | case-insensitive, warns per missing pack |
| P4 agy #6: growDynmem bound | confirmed | `ddc58af950e` | refuses > 0xFFFF on SCI16 |
| P4 agy #7: which copies grow | note | `a4e84ec0a98` | documented on `growDynmem()` |
| P4 agy #8: `getFont(1001)` | confirmed | `67e4df47ae4` | `!utf8()` |
| P4 Opus m1 | = agy #4 | `173c9006f7f` | |
| P4 Opus m2: padding after a cut | confirmed | `ddc58af950e` | padding from the real length |
| P4 Opus m3: `hiResFont` masks SCI32 ids | confirmed | `67e4df47ae4` | mask only below SCI2 |
| Alt+S leak (notes finding 3) | unresolved | `b87872963f6` | Alt guard + test added; the key was never seen by the engine (see P4-notes.md "Corrections"), so the cause stays open |

## Skipped

- P3 agy4 (false), ocd I1 (false): no change, as adjudicated.
- P3 agy9(b)(c), agy11, agy13, agy15, agy16, agy17, ocd P4 #3/#4: nits in files not touched by a fix; recorded in `P3-ext-review.md`, no commit.
- P3 agy2 loops of upstream (raw -> non-raw, non-raw -> non-raw): upstream behaviour, latent; the stack/locals bound is the P3 M3 decision.
- QFG1 hut (room 22) words are the phrases of script 22 (`hut of brown`, `now sit down`) taken as their words in order; **not run** (the hut was not reached).
  The gargoyle words are those of script 29; a full pass is still unverified (see P4-notes.md).
- Korean save name in an answer room slot 1 in play: not run.

## Checks

`make test` 690 OK (normal), 690 OK (ASan, 0 sanitizer lines in the build and in the runs below), no-engine 620 OK, SCI32 build and mingw build link with no warning,
`sync-svf.sh --check` and `mkgolden.py --check` rc 0, `test_k6bake.py` and `test_strcheck.py` OK, `check-commit-msg.py` clean on every commit
(one `Assisted-by: Claude Code`, no Co-Authored-By). Headless (clut8): KQ1 first room as above; QFG1 VGA starts (ASan, 0 reports); QFG2 door sequence on ASan and on the
normal build; English targets (QFG2, KQ4, LSL1 EGA, LB1) 320x200 with no Korean log line; ASan runs of LSL1 EGA, KQ4, LB1, Camelot clean.
