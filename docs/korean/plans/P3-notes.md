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
