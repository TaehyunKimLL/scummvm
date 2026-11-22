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
| 17 | eb5d99713d6 | 666 OK | [data] typed `qhk` through the built-in composer: prompt `입력` with `봐` in the field; after Return the box `에드워드 왕의 성 바깥에 서 있다. 성은 큰뱀으로 / 가득찬 해자로 둘러쌓여 있다.` on two lines (clut8). |
| 18 | 4125043d9e0 | strcheck tests: 4 OK | red: `ModuleNotFoundError: No module named strcheck` (verified); [data] check of the pack table: 380 entries, no header, rc 0 |
| 19 | (no commit) | | [data] end to end, see "Task 19 results" |
| 20 | (no commit) | | k16said acceptance pending ko/07-debugsocket (plan section 1 item 2); not run |
| 21 | this commit | | PATCHES.md, this log, acceptance run |

## Task 19 results (headless Xvfb, `~/work/tmp/p3-kq1/`, PNGs not committed: game imagery)

Pack `~/work/tmp/p3-kq1/pack` (unzipped harness pack), `language=ko`, default map (NEODG16A) after the two "map version 2" notes.

- Title (clut8): Hangul buttons `프롤로그` `게임시작` / `크레딧` `이어서 계속하기` on blue buttons, 16 px faces at 2x in the 640x400 window, legible; sha256 063533ed...
- First room (clut8 and rgb565): status line `득점: 0 of 158` and `킹스 퀘스트 I` centred, legible; the status rows y 0-16 are pixel-identical between clut8 and rgb565 (sha256 of the crop `80ef44ab...`). Whole-frame sha256: clut8 `0b4cfeed052491b5255275594d1936dd7680e4c06db36f9fcf9c88263b7d2b9e`, rgb565 `67c141ce150ecd29e2721b165811f1897e4d02804fda50924e26d790bcb33066` (they differ in the moat animation only).
- Typed `qhk` + Return (clut8 and rgb565): prompt box `입력` with `봐` in the field; after Return the box `에드워드 왕의 성 바깥에 서 있다. 성은 큰뱀으로 / 가득찬 해자로 둘러쌓여 있다.` on two lines. The two whole PNGs are byte-identical: sha256 `15b3b13a2c1ae8b1b7222e59149aedfdca7489c1bbfea7edcaf29af88be863c9`.
- Particle sentences, same room, Korean vs the English target on the same game: `문을 열어` (typed `ansdmf dufdj`) -> `당신은 데번트리의 잃어버린 보물 세개를 발견하지 못한채 왕 에드워드로 돌아가는 방법을 재강구한다.`; English `open door` -> "You reconsider the wisdom of returning to King Edward without having recovered Daventry's three lost treasures." (same meaning). `절해` (typed `wjfgo`) -> `그밖에 다른 걸 시도해 보세요, 그래햄.`; English `bow` -> "Please try something else, Sir Graham." (same meaning). An unknown word (`여어`) gives the game's "try another word" reply with the word quoted.
- Save/restore (`originalsaveload=true`, GFX=opengl): F5 + `ans` + Return wrote `kq1-ko.001`; F7 lists `문` with the game's own list arrows (not boxes); F7 + Return restores the room. The dialog's `Restore` / `Change Directory` buttons are English: they are not script strings of the sci-ko.str table (out of P3 scope). The ScummVM (launcher) save dialog with a UTF-8 description was NOT checked (no launcher run).
- Headers (`packh`: strcheck.py header + k6bake.py build, version 3, 21,742 B, sha256 964bb09d...): with the right fingerprint `[korvocab] loaded 1265 Korean words (version 3)` and no sci-ko warning; with the fingerprint set to 32 zeros `sci-ko.str was made for another game: fingerprint=000..., the game's is 87ef5027c321e18e7b81a47bb584c098; not loaded` (the table is refused; the English script strings then showing was not viewed).
- ASan build (`~/work/tmp/scummvm2026-p3asan`, `detect_leaks=0`): the same typed-command run, reply box on screen, no AddressSanitizer / runtime error line. A leak check at exit was not made (the run is killed, not quit).

## Acceptance (Task 21)

- `$BLD` `make test`: 666 OK (620 + 46); `make scummvm` links; no warning from a P3 file (touched the six new .cpp files, rebuilt: 0 warnings).
- ASan (`--enable-asan`): `make test` 666 OK, 0 sanitizer lines; the typed run above clean.
- SCI32 (`--enable-engine=sci,sci32`): `scummvm` links, 666 tests OK, no warning.
- No-engine build (`~/work/tmp/scummvm2026-p2verify`): 620 OK.
- mingw (`x86_64-w64-mingw32`, `PATH=$HOME/.local/mingw/bin`): `make test/runner` builds `test/runner.exe` (PE32+, 21.7 MB) with SCI enabled; not run (no wine).
- `test_strcheck.py` 4 OK, `test_k6bake.py` OK, `sync-svf.sh --check` rc 0, `mkgolden.py --check` rc 0.
- `git diff --stat d7f47c73a07.. -- backends gui base common graphics`: empty.
- Every commit passes `devtools/check-commit-msg.py` and ends with exactly one `Assisted-by: Claude Code`, no `Co-Authored-By`.
- k16said >= 303/310: pending ko/07-debugsocket (Task 20).

## Deviations from the plan and from the prototype

1. Code: the plan's code blocks and the prototype (`scummvm2026-p3-proto`) agree line for line (checked by script); the files were taken from the prototype and the GPL header of `sci.cpp` was added to the new .h/.cpp files (the plan says so, the prototype has none). Per-file diffs of the prototype applied as the plan's diffs.
2. TDD: red runs were real for Tasks 1-6, 8, 10, 18 (no header / 0 files / one detect line / missing module). Not possible, said in the table: Task 7 (type widening), Tasks 9, 11-17 (kernel/driver/parser hooks that need the game; exercised by the data runs), Task 16 beyond the load line.
3. `go.sh`: added `xdotool windowfocus` (alt+s did nothing without a window manager) and a retry wrapper (`go.sh` calls `go1.sh` up to 10 times; the "SDL_BlitSurface failed" start flake hit often). Screenshots from clut8 are PNG mode RGB (the screenshot action converts), not P as the plan guessed.
4. `render_target=rgb565`: no log line says the screen is RGB (the target resolves silently when offered); the evidence is that the run is clean and whole-frame identical to clut8 for a 1 bpp face, which is the expected result.
5. Not verified for want of a launcher run: the ScummVM save dialog with a UTF-8 name; the English script strings after a refused table.
6. mingw: `make test/runner` (the target is `test/runner`, the file `runner.exe`), not `make test/runner.exe`.
7. Task 19 Step 5: the references are recorded as hashes above; the first-room whole frames differ in the moat animation, so later runs compare the status rows crop (`80ef44ab...`, rows 0-16) and the reply box.
8. Notes: each task's sha is written in the next task's entry (a commit cannot name itself).
