# P4 notes: running log

Running log of the P4 implementation (plan `P4-sci-data-games.md`). One entry per task: commit, test count,
deviations. All game checks ran on **scratch copies** of the harness packs (`~/work/tmp/p4/data` shipped zips,
`~/work/tmp/p4/datah` the same with the fingerprint header and version 3 word table added as the plan's section 4
describes); the harness was not modified. Nothing of the game data is in the repository.
Base `5741781806a`, N0 = 675 tests (verified in `~/work/tmp/scummvm2026-p4build`, 2026-11-22).

| Task | Commit | Tests | Notes |
|---|---|---|---|
| a0 | (none) | 675 OK | all interface greps print; the data loop printed the counts of plan section 4 (QFG2 2032/2674, QFG1E 175/2247, LSL1 703 x2, KQ4 222/1168, LB1 91/2017, Camelot 118/5362) |
| a1 | dcd7a393a01 | 675 OK | no red possible (as the plan); no warning. Step 4 [data] (QFG1 VGA, not a P4 game): see "Findings" 1 |
| a2 | da895ce3e83 | 678 OK | red: `'class Sci::KoreanParser' has no member named 'englishLine'`, `'koreanAnswerRoom' is not a member of 'Sci'` (verified); green 678, suite 9 tests |
| a3 | db358317f68 | 678 OK | no unit red possible (plan) |
| a4 | 23db615a414 | 678 OK | no unit test possible (plan) |
| a5 | 67467f56bb4 | 678 OK | [data] `grep 'font set'` on the LSL1 EGA run: fonts 0, 59, 1207, no id >= 2048 (the P3 binary was not run) |
| a6 | 1f820cde153 | 679 OK | red on shipped packs: per game `getHeader().present()` fails, and `version 2 != 3` for QFG2, QFG1EGA, KQ4, LB1, CAMELOT (all five parser games); green on `datah`; absent folder: Warning + OK |
| a7 | 963fb651e90 | | PATCHES.md rows; `git diff --stat 5741781806a.. -- engines test` = the nine engine files + the two test files |
| a8 | (this commit) | | smoke below |
| A-fix | 1ac1d513f83 | 679 OK | `SCI: Keep strncpy's padding and upstream paths in the same-block copy` (self-read finding, see checkpoint A) |

## a8: first screens, Korean target (640x400, clut8, `language=ko`, 25 s) and English target (320x200)

Every Korean log: the two "map version 2 is not read" notes, then `Hi-res text: using data:hires_text/maps/korean-default.map`;
the parser games print `[korvocab] loaded N Korean words (version 3)` (QFG2 2674, QFG1EGA 2247, KQ4 1168, LB1 2017,
Camelot 5362) and no `sci-ko` warning; no `cut to`, `grown from`, `longer than`, `AddressSanitizer` line.
All screenshots viewed:

| Pack | Screen (Korean) | sha256 (first 16) | English target sha256 |
|---|---|---|---|
| qfg2 | piracy notice, ten centred Hangul lines in a cyan box, legible at 2x; the sword cursor overlaps one line | cf99f8c17e3dc48f | 4cf7b2f4333cfb76 |
| qfg1ega | same notice (`지금 사용 중인 이 소프트웨어는 ...`), five lines + closing, legible | efcae51666868590 | 8c1618c703010382 |
| lsl1ega | title art only (the picture, no script text yet) | 8b9b057fd0a8a15b | c75e4694b6320104 |
| lsl1vga | title art only, 256 colours, palette right | ba89b0cb12d18f66 | 65581a7b93af3e3f |
| kq4 | copy-protection box `소유권을 확인하기 위해 "킹스 퀘스트 IV" 설명서를 보고 ...`, question `4쪽 세 번째 문단의 마지막 단어는?` in the edit box; legible, wrapped inside the white box | 2b02c3f2492d2258 | 2b83847640a45f97 |
| lb1 | fingerprint screen: header line and the suspects' names in Hangul (`셀리`, `루디 디종`, ...) left/right of the magnifier, three names under it, all legible, nothing cut | 5ea4387c70713f85 | f05aa35279e8e859 |
| camelot | title picture (`executive producer Ken Williams` is part of the picture) | 39b141e8b8a2182f | f260d2717eed4ae6 |

English targets: 320x200, English text (LB1 fingerprint screen viewed: English names), zero `Hi-res text` / `korvocab`
/ `sci-ko` lines in all seven logs. go.sh note: the LSL1 English runs need 25 s (at 20 s no screenshot appears and the
retry loop spins; not an engine problem).

## Findings

1. **QFG1 VGA (not a P4 game) crashes on start with a heap corruption, with the P3 binary too** (`malloc(): invalid
   size (unsorted)` / `free(): invalid next size (fast)` after `Hi-res font set for font 300`, with `language=ko` on the
   shipped `scummvm-game-qfg1` pack). It is therefore not caused by a1 (a1 is not in the P3 binary). Task a1 Step 4
   (restore-dialog labels `로드`/`덮기`) could not be run; the hook stays UNVERIFIED on data. Separate card needed (SCI1.1
   hi-res text), not P4.

## Review checkpoint A (after Task a8)

- `$BLD` `make test`: 679 OK. ASan build (`~/work/tmp/scummvm2026-p4asan`, `--enable-asan`): 679 OK, 0 sanitizer lines in
  the build log. No-engine build (`~/work/tmp/scummvm2026-p2verify`): 620 OK. `sync-svf.sh --check` rc 0 (svf-fonts-open
  3d40de8d...), `mkgolden.py --check` rc 0, `test_k6bake.py` 11 OK, `test_strcheck.py` 5 OK (a first run of
  test_strcheck failed only because `$TMPDIR` had been removed by test_k6bake's cleanup; rerun with it recreated: OK).
- Self-read (buffer sizes, offsets, inertness): every new line is behind `getTextMode().utf8()` (a1, a3, a4),
  `koreanAnswerRoom` (a3) or a non-null hi-res object (a5). `git diff 5741781806a.. -- engines | grep -c GID_` = 3 (the
  table rows). One finding: the same-block copy-out in `strncpy(reg_t, reg_t, n)` applied to every raw string segment and
  lost the NUL padding of a limited `strncpy`; fixed in `1ac1d513f83` (DYNMEM only, pads to a finite limit). No unit
  test is possible (SegManager needs a ResourceManager), the fix is covered by the QFG2/QFG1 runs only in so far as they
  copy strings. `koreanAnswerLine` keeps the fallback tree; its cache key is the typed text (a changed word table would need
  a new typed text, the table never changes during play).
- ASan run of QFG2 (Task b2) at the end, see b2.
- The "fresh reviewer" of the plan is not available inside this run; the self-read above stands in for it (deviation).
