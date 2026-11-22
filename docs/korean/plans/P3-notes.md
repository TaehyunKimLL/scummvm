# P3 notes: running log

Running log of the P3 implementation (plan `P3-sci-kq1.md`). One entry per task: commit, test count, deviations.
The sha of a task is recorded in the entry of the next task (a commit cannot name itself); `git log` has them all.
Base `d7f47c73a07`, N0 = 620 tests (verified, `~/work/tmp/scummvm2026-p3build`).

| Task | Commit | Tests | Notes |
|---|---|---|---|
| 0 | (none) | 620 OK | base and interfaces checked; all 12 greps print; KQ1 data and pack zip present |
| 1 | 6e7b0216a2e | | DISTS: Install the hi-res text fallback map and face |
| 2 | c9b245f6873 | 626 OK | SCI: Add TextMode; red: `sci/textmode.h: No such file or directory` |
| 3 | 10cd7d8c7e9 | 641 OK (620+21) | red: `sci/utf8.h: No such file or directory`; test file identical to jamo minus licence header |
| 4 | f5fe0bd6b49 | 649 OK (620+29) | red: header missing; warnings of the mismatch cases not shown in the runner log (make test runs quietly) |
| 5 | d929fe74d2d | 655 OK (620+35) | red: header missing |
| 6 | 65eca6bfeaf | 662 OK (620+42) | red: header missing |
| 7 | 3b78794feff | 662 OK | mechanical widening; no red possible (a type change is covered only by compiling); scummvm links; per-file diffs identical to the prototype; SCI32 build checked at checkpoint A |
| 8 | 0bf314e250d | 666 OK (620+46) | red: `sci/graphics/hirestext.h: No such file or directory` |

## Review checkpoint A (after Task 8)

- `$BLD` `make test`: 666 OK. ASan build (`~/work/tmp/scummvm2026-p3asan`): 666 OK, 0 sanitizer lines.
- No-engine build (`~/work/tmp/scummvm2026-p2verify`): 620 OK (the SCI suites are not compiled). SCI32 build (`~/work/tmp/scummvm2026-p3sci32`, `--enable-engine=sci,sci32`): `scummvm` links, no warning.
- `sync-svf.sh --check` rc 0, `mkgolden.py --check` rc 0, `test_k6bake.py` OK.
- `git diff --stat d7f47c73a07..`: only new engine/test files, the six font files, `util.h`, `module.mk`s, `Makefile.common`, `ports.mk`, the map, notes.
- Self-read: `loadFromStream` returns false on a bad header before any entry is kept; `writeRun` keeps `cursor` inside `text` (the head/tail are cut from a text `adopt()` has verified is the one the line wrote); `GfxFontHiRes` widths round up; the `SciHiResText` destructor deletes the sets before the face cache. No finding, no fix commit.
| 9 | 877ea2d3045 | 666 OK | [data] check: sci-ko.str headerless warning + header line with fingerprint 87ef5027c321e18e7b81a47bb584c098 (as the plan). No red possible (engine wiring; the observable is the data check: before this task no such lines). Per-file diffs identical to the prototype. |
| 10 | c6ebf07ac73 | 666 (not re-run, detection only; scummvm links) | [data] red: one `--detect` line (English); green: Korean + English lines, as the plan; run of [kq1-ko] logs the English release entry |
| 11 | 95464d3f0d4 | 666 OK | no red possible (kernel hooks need the game; exercised by the headless runs in Tasks 15/19). The moved kStrAt block differs from base only by `argc > 2` -> `write` and `return value`. |
| 12 | bcf2c9077e6 | 666 OK | hooks only |
| 13 | 42e178f833e | 666 OK | driver hook; verified by the smoke runs of Task 15 |
| 14 | 4c0fa3690aa | 666 OK | screen/cache hooks; verified by the smoke runs of Task 15 |
| 15 | a33b0811dbe | 666 OK | [data] smoke: title shows Hangul buttons (프롤로그 게임시작 / 크레딧 이어서 계속하기) at 2x in the 640x400 window; first room status line `득점: 0 of 158  킹스 퀘스트 I`; same on `render_target=rgb565` (status rows y0-20 pixel-identical to the clut8 shot); English target runs upstream (320x200, English title menu, no `Hi-res text` log line). Log: `Hi-res text: using data:hires_text/maps/korean-default.map` after the two "map version 2" notes. go.sh needed `xdotool windowfocus` for alt+s and a retry loop for the SDL_BlitSurface start flake. PNG mode is RGB for both (the screenshot action converts). |

## Review checkpoint B (after Task 15)

- `$BLD` `make test`: 666 OK; no-engine build 620 OK; `sync-svf.sh --check` and `mkgolden.py --check` rc 0.
- Smoke screens viewed (see Task 15): Hangul legible at 2x, status line, RGB identical in the status rows; the English target is upstream.
- Self-read, hooks and guards: kstring/kmenu/kgraphics translation lookups return "no key" when no table is loaded; text16 `getLongestUtf8` and the decode loops sit behind `getTextMode().utf8()`; the driver hook behind a non-null plane; screen hooks behind `_textPlane`; vocabulary behind `_koreanVocab.isLoaded()`; event/keymap behind `_hangulInput` / language ko.
- `bitsGetDataSize` adds `saveSize(rect)` under the same condition (`VISUAL` mask and a plane) as `bitsSave` writes `save(rect)` and `bitsRestore` reads it: symmetric (one flag byte when the rect is empty).
- `compositeText`: rect clipped to the plane, the row scratch is 1280 px (plane width 640), CLUT8 rows go through `_textScratch` so `_scaledBitmap` never keeps text.
- Two changes that touch an English game too, both defensive and left as the plan has them: `Width()`/`Draw()`/`DrawStatus()` loop `while (len-- > 0)` (upstream `while (len--)`; differs only for a negative length, which no caller passes) and `tokenizeString` stops a word at `VOCAB_MAX_WORDLENGTH - 1` bytes (upstream wrote past the buffer).
- No finding needing a fix commit.
| 16 | 083cd8b790a | 666 OK | [data] `[korvocab] loaded 1265 Korean words (version 2) from sci-ko.dat` and the sci-ko.dat header warning with vocab=4b7bd270cdc3a1ff3bcec6d56cc4662f. No red possible (needs game data); typed commands exercised in Task 19. |
| 17 | see below | 666 OK | [data] typed `qhk` through the built-in composer: prompt `입력` with `봐` in the field; after Return the box `에드워드 왕의 성 바깥에 서 있다. 성은 큰뱀으로 / 가득찬 해자로 둘러쌓여 있다.` on two lines (clut8). |
