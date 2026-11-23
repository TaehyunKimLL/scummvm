# P6a notes: SCUMM S1, Korean text as UTF-8 inside the engine

Running log of the implementation of `P6-scumm.md` phase P6a (tasks a0-a14). Branch `ko/p6-scumm`, worktree
`~/work/scummvm-2026.4-korean-p6`, base `4211b5ac538` (ko-2026.4). Builds: `~/work/tmp/p6a-build` (SCUMM only),
`p6a-sci`, `p6a-none`, `p6a-asan`, `p6a-mingw`. Scratch: `~/work/tmp/p6a`, runs `~/work/tmp/p6/runs/a*`.
Code source: the prototype `~/work/tmp/p6/tree`; its new files equal appendix B of the plan apart from the GPL header
(and one `hires_text.h` line in `korean.cpp`, S2, left out). Tasks are committed one by one; the sha of a task is
written into the log by the next commit.

## Log

### a0 (no commit)
- `git diff v2026.4.0.. -- engines/scumm` is empty at the base. SCUMM-only build, `make test`: **N0 = 644** (the plan's 637
  was measured at another base).
- Interface greps: all present (string.cpp 2340, 1265, 1541, 1355, 1705 and 1787, 1914/1979, charset.cpp 677 ...).
- Red: the MI2 pack as shipped on the base build: `charset.cpp:1367: ... Assertion 'charOffs < 0x14000' failed`
  (`~/work/tmp/p6/runs/a0-mi2/run.log`, count 1).

### a1 e6a7b929132: KS X 1001 table
- Red: `engines/scumm/korean_text.h: No such file or directory`. Green: 645 tests (N0 + 1).

### a2 0c43e6894ad: UTF-8, escapes, CP949 -> UTF-8
- Red: `'cp949ToUtf8' is not a member of 'Scumm'`. Green: 647 (N0 + 3). In this task the two tests called
  `cp949ToUtf8()` through a local helper, as the plan says.

### a3 48478d3d97a: the boundary
- Red: `'toEngineText' is not a member of 'Scumm'`. Green: 650 (N0 + 6). Tests rewritten to the appendix B.9 form.

### a4: bundle names and the CP949 body
- Red: `trs_bundle.h: No such file or directory`. Green: 653 (N0 + 9).
- a4 sha 467b5dc6acf.

### a5: `_koreanUtf8`, the bundle, the boundary in `translateText()`
- Hooks as appendix A (hunks of loadKorFont, loadLanguageBundle, translateText x4); `korean.cpp` carries
  `transcodeLanguageBundle` and `copyTranslatedText`. **Appendix A omits the `scumm.h` declarations of `_koreanUtf8`,
  `hasTranslationBundle()`, `copyTranslatedText()`, `nextCJKChar()` ... (the prototype diff in the plan has only the
  `transcodeLanguageBundle`/`convertLoadedText` hunk); added from the prototype's scumm.h.** Also the plan's hunk headers
  have hand-written line counts (`git apply` refuses them; applied with `--recount`).
- Build: 653 tests (N0 + 9), no warning.
- [data] without extrapath, run folders `~/work/tmp/p6/runs/a5-*`: MM `50116 -> 58803` (as the plan). **Differences from the
  plan's numbers**: Zak `87138 -> 101837` (plan 102032), Indy3 `126521 -> 148511` (148571), Indy4 `813252 -> 821296`
  (821300; 7 unreadable pairs as planned), **MI2cp `738122 -> 747583` (plan 841833)**. The MI2 pack: no conversion line.
  Drawing is not there yet: Indy4 and MI2cp stop at `charOffs < 0x14000` as expected at this task.
- **Finding (red for the next commit):** the MI2 gap is real, not noise. The code of appendix B.5 splits the body at every NUL,
  but a translation can contain a NUL as an escape argument (`FF 0A 00 xx`): the part after that NUL is not converted and
  stays CP949, and the later boundary judges the whole string as not UTF-8 and reads its UTF-8 head as CP949 (garbage). A
  Python model of the engine rule (`~/work/tmp/p6a/emu.py`) gives 103708 pairs when a translation runs to its
  escape-aware end, which is the plan's `+103711`; the NUL split gives 20027. Fixed in the next commit (translations run to
  their escape-aware end, clipped at the next entry offset, which keeps MM's `sound FF 04 C8 00` working).
- a5 sha 238d202631d.

### a5-fix: a translation with a NUL escape argument (deviation from appendix B.5)
- Red: new test `test_translation_runs_past_a_nul_argument` failed (`memcmp ... found (-56 != 0)`), 655 tests, 1 failed.
- Fix: a translation string runs to its escape-aware end (`scummTextEnd`), clipped at the next entry offset of either
  list (MM's `sound FF 04 C8 00` still ends at its NUL: the terminator before the next entry); other strings stay
  NUL-split. Extra test `test_translation_stops_at_the_next_entry` and `test_unterminated_translation_keeps_an_empty_next_entry`.
  Green: 656 tests (N0 + 12).
- Evidence: MI2cp now `738122 -> 841833` and Zak `87138 -> 102032`, exactly the plan's numbers.
- a5-fix sha 554d303e75f.

### a6 5568643c4fa: read UTF-8 where text is measured, broken and drawn
- Hooks as appendix A (hunks 0, 2-6, 15, 17-19) + `korean.cpp` (`nextCJKChar`, ... `koreanRefusalGlyph`) + the `scumm.h`
  declarations (again missing in appendix A). **Deviation:** appendix B.8 still tests `!_hiResText` in `nextCJKChar()`
  (an S2 line the plan says to drop); removed. 656 tests, no warning.
- [data] same picture: old save slots 2 and 6 on mi2cp, upstream `build-head` vs this build
  (`~/work/tmp/p6/runs/a6-mi2s{2,6}-{head,new}`): slot 2 **identical full frame** (bbox None). Slot 6: the text rows
  (y >= 300) and the top (y < 186) are identical; the only difference is the animated water (bbox 10,186-422,282), i.e.
  timing of the scene, not text (the plan saw a 30x30 cursor box instead).
- Smoke, new build, no `encoding.dat`, `~/work/tmp/p6/runs/a6-*` (grid.png per run, viewed): MI2 pack (UTF-8 bundle, no
  conversion line): `원안 및 디자인 / 론 길버트`, `카리브 해 심연의 스컵 섬`; MM, Zak (`그냥 밤 늦게, 작은 침대에...혼자서...`),
  Indy3, Indy4 (`프로그래밍 : 론 발드윈`, `목소리 관련 : 에릭 월문더`), Loom, MI1 floppy (credits in Korean), DOTT: 0
  `Assertion`, 0 `has no glyph`. Bundle sizes: MM 50116 -> 58803, Zak 102032, Indy3 148571, Loom 85771 -> 100614, **Indy4
  813252 -> 908215** (the plan's 821300 was the NUL-split number; 7 unreadable pairs as planned).
- a7 sha 9277ae02b38. a7 [data] (hover an inventory object in MI2): not reached, stays UNVERIFIED (as in the plan).

### a8 904ed3fb75e: v2 sentence composer
- Red: `korean_sentence.h: No such file or directory`. Green: 660 tests (N0 + 16; the plan's "N0 + 13" counts without my three
  a5-fix tests).

### a9: hook the sentence line
- Hooks (hunks 13, 16) + `drawKoreanSentence()` in `korean.cpp`. 660 tests, no warning.
- [data] MM pack (kids, START, Escape x2, verb, hover the sign), run `~/work/tmp/p6/runs/a9-mm`: `이게 뭐지? 표지판`
  (`scumm-p6a-mm-sentence-sign.png`), and `표지판 닫기` (object before verb). Two-object lines: UNVERIFIED in play (as the plan).
  The scripted run depends on cutscene timing (two of four attempts clicked before the verbs were live).
- a9 sha e7a2a161db5.

### a10: saves hold UTF-8
- Tests `saved_text.h` first: red `engines/scumm/saved_text.h: No such file`; green 662 tests (N0 + 18). Hooks: `saveState()`
  writes the trailer for a Korean v1-v6 target, `loadState()` reads it and calls `convertLoadedText()`.
- [data] (a) old CP949 save `monkey2-ko.s02` on mi2cp: `SCUMM: converted 20 text resources of a CP949 save to UTF-8`,
  `State loaded`, and the frame is **identical** to upstream's load of the same save (`runs/a10-mi2s2` vs `a6-mi2s2-head`,
  bbox None). (b) autosave after 10 s (`runs/a10-autosave`): gunzipped tail `... 05 00 'TENC' 01`; reloading it
  (`runs/a10-reload-new`): `State loaded`, no assertion. (c) plain 2026.4.0 (`build-plain`) loading (b)
  (`runs/a10-reload-plain`): `State loaded`, then `Assertion 'charOffs < 0x14000' failed` at the first Korean verb: **a Korean
  save of this engine cannot be read by upstream**; for the pack README (H11).
- a10 sha 67bef0f0e53.

### a11: `ko.trs` and the `ko/` folder
- Detection accepts `korean.trs`, `ko.trs` and a `ko/` folder holding `ko.trs` or `korean.trs` (appendix A hunk 7 only added
  `ko.trs`; the plan text asks for the folder too); the constructor adds the `ko` sub directory for `KO_KOR`.
- Red (`runs/a11-red`): MM copy `~/work/tmp/p6/data/mmkodir` with `ko/ko.trs`: no `Loaded N entries`. Green (`runs/a11-green`):
  `CP949 body converted to UTF-8, 50116 -> 58803 bytes`, `Loaded 1024 entries`; `scummvm --detect` on the folder reports
  `Maniac Mansion (V2/DOS/Korean)`. 662 tests unchanged (no unit test of detection exists).
- a11 sha 0433a8ec851.

### a12: developer-data check and `trscheck.py`
- `test/engines/scumm/korean_trs_data.h` (one test; with `korean-trs/` in the working directory, here
  `~/work/tmp/p6a-build/korean-trs/` with symlinks to the harness files and `<name>.allow`): 663 tests (N0 + 19). Without the
  folder: a `TS_WARN` and OK (checked from an empty cwd). **Red first:** with `.allow` absent the test failed for both sets:
  MI1 UTE `1 of 5348` (line 334: the UTF-8 stage carries a raw 0xFA, the engine gives U+00FA: the plan's "one stray byte") and
  **MI2 `1 of 8780`, not 8780/8780 as the plan's Python model says**: line 5535 is `Papier-Mâché;` in the UTF-8 file but
  `Papier-Mache;` in the harness's CP949 stage (CP949 cannot carry the accents, the stage lost them). So both sets get
  `.allow` = 1; the conversion is right, the CP949 stage is lossy for those 2 characters.
- `tools/korean/trscheck.py` + `test_trscheck.py` (6 tests, `python3 -I`): census, utf8len, equal, with the engine's rules
  (escape-aware translation extents, clip at the next entry). Real data: utf8len MM 251, Zak 295 (2 >= 256), Indy3 257,
  Loom 233, Indy4 337 (42 >= 256): the plan's numbers; MI2 245 and MI1 UTE 239 (plan 161/222: I count escape bytes, which the
  engine's buffers hold too); none >= 512. census Indy4: 5 unreadable pairs by Python's cp949 (the engine without
  `encoding.dat` reads 7), 148 charset bytes. `equal` mi2 and mi1ute: 1 line each (above).
- a12 sha fff425d12a8 (note: the helper script staged the whole tree, so this one commit, titled for the test, also holds
  `trscheck.py`, its test and the notes of a12; the next commits stage only their paths).

### a13: records
- `docs/korean/PATCHES.md`: rows for topic `ko/05-scumm`, one per upstream file. This log is `P6a-notes.md` (the plan names
  `P6-notes.md`; the run's instruction names `P6a-notes.md`).
- a13 sha fd263c4b384.

### a14: smoke and English (new build, no `encoding.dat`; run folders `~/work/tmp/p6/runs/a14-*`, frames viewed)
- MM V1 English (`a14-mmen`): English text, 0 lines with Korean/UTF-8/TENC in the log (`scumm-p6a-mm-v1-english.png`).
- MI1 UTE: UTF-8 bundle on MI1 CD data (`a14-mi1uteu2`): no assertion (the base asserted), Korean credits; CP949 bundle
  (`a14-mi1utec`): `483698 -> 552956 bytes`, exactly the size of the UTF-8 file. MI2cp (`a14-mi2cp`): `738122 -> 841833`.
- Loom (`a14-loom`): menu `표준레벨 / 연습레벨 / 전문가레벨`. Indy3 (`a14-indy3`): gym line and Korean verbs. DOTT
  (`a14-dott2`): subtitles and the title card `번역 - MONSTAR 황승주`. Zak, MI1, Indy4, MM, MI2 as in a6. 0 assertions, 0
  `has no glyph` lines in every run. Indy4 with `encoding.dat` (7 -> 5 unreadable): NOT run.
- MI1's centred credit line with 0xFA (the documented 1 px width fix): NOT verified; two runs on the same start timeline
  differ by animation, so a pixel diff proves nothing. UNVERIFIED.
- Gallery: `~/work/tmp/ko-screenshots/scumm-p6a-*.png`, captions appended.

### Review checkpoint A
- Tests (final HEAD): SCUMM-only build **664** (N0 644 + 20) OK; SCUMM+SCI build **743** OK; no-engine build **644** OK;
  ASan SCUMM build **664** OK; mingw (`x86_64-w64-mingw32`, SCUMM) `test/runner.exe` built (PE32+, run UNVERIFIED: no wine).
  `tools/korean/sync-svf.sh --check` rc 0 (`graphics/svf matches svf-fonts-open 3d40de8...`; the submodule dir of this
  worktree was empty, filled with a local clone at the gitlink commit), `python3 -I tools/korean/mkgolden.py --check` rc 0.
  `python3 -I tools/korean/test_trscheck.py` OK (6), the other tools tests OK.
- Self-read (UTF-8 vs bytes at every site; fixed-size buffers; terminators):
  * `nextCJKChar`/`cjkTrailBytes`/`koreanCharsetGlyph` read through `decodeUtf8(p, p + 4)`: it returns at the first
    non-continuation byte, so it never goes past the string's NUL. `koreanCharStart` is bounded by `s > 0` and 4 bytes,
    `koreanCharBefore` by `begin`. Verified that for a CP949 (FT) target every addLinebreaks position equals upstream's
    (cs = pos - 2 / pos - 1 reproduce the old expressions).
  * Buffers: `drawString` 512; `convertMessageToString` transBuf 2048 and its `dst >= end` guard; `loadPtrToResource`
    translateBuffer 512, `_charsetBuffer` 512, `string.cpp:157` buf[500], `verbs.cpp` buf[384], dialogs buf[256]: all copies
    go through `toEngineText` (cut on a boundary, NUL inside) or `convertMessageToString`'s own overflow `error()`;
    longest translation as UTF-8 337 bytes (Indy4) < 384. The v2 inventory `msg[20]` copy (`verbs.cpp:484`) takes OBCD names
    (English), unchanged.
  * Escapes: 0xFF/0xFE never occur in UTF-8; `scummEscapeLength` clips to the end; the cut loop never splits one (test).
  * wrapSegaCDText hunk passes the byte after the lead to `cjkTrailBytes`, correct only for CP949: the function is Sega CD
    only (never `_koreanUtf8`), inert; left as in the plan.
  * English/other targets: `_koreanUtf8` false -> `nextCJKChar` = upstream's `c + b * 256`, `cjkTrailBytes` = 1,
    `copyTranslatedText` = upstream's memcpy; the saves write no trailer; `loadLanguageBundle` runs for Korean targets only.
- Findings fixed in their own commits: (1) a translation with a NUL escape argument (a5-fix, red test); (2) a CP949
  syllable that is also valid UTF-8 (`카` = C4 AB) was left unconverted: `textIsUtf8` now refuses 2-byte characters above
  U+00FF (red test `test_cp949_syllable_that_reads_as_utf8`); `convertLoadedText` copied uninitialised stack bytes back
  into `_charsetBuffer` (cleared); (3) ASan `alloc-dealloc-mismatch` at exit of every Korean multi-font target: upstream's
  `delete _2byteMultiFontPtr[i]` of a `new byte[]` (commit 83ff54fbf39). Remaining known limit: a CP949 single syllable
  of rows C2/C3 with trail A1-BF (62 syllables) is still ambiguous with Latin-1 UTF-8 (`é` = C3 A9) and is kept as UTF-8.
- ASan runs (`ASAN_OPTIONS=detect_leaks=0`, `runs/asan2-*`, `runs/asan-save*`): MI2 pack, MI1 floppy, MM, DOTT, Indy4:
  no report (the first batch `runs/asan-*` showed the `delete` mismatch above on every set). Old CP949 save conversion and the
  autosave reload under ASan: no report.
- Upstream lines added in the nine upstream files: 112 (+26 deleted), the plan's budget was 100: the excess is the
  doc comments of the `scumm.h` declarations.

### Deviations from the plan (summary)
1. Appendix A omits the `scumm.h` declarations and has hand-written hunk counts (applied with `--recount`).
2. B.8 `!_hiResText` (S2) dropped.
3. B.5: a translation runs to its escape-aware end, clipped at the next entry (the plan's NUL split left the tail of MI2/Indy4
   translations CP949). Numbers now equal the plan's (MI2cp 841833, Zak 102032); Indy4 is 908215, not the plan's 821300.
4. a11 also accepts the `ko/` folder in detection.
5. a12: both MI2 and MI1 UTE have one line that differs from their UTF-8 stage (MI2: `Papier-Mâché`), the plan said 8780/8780.
6. Extra commits: `textIsUtf8` tightening and `delete[]`.
7. Test totals: N0 = 644 (plan 637); final N0 + 20 (the plan counts N0 + 16).
8. The commit of a12 titled TEST also holds `trscheck.py` and its test (staging slip).

## Fix round (reviews of 0c6d8e45ced)
Findings, verdicts, commits and evidence: `P6a-fixes.md`. Reports: `P6a-review.md`, `reviews/P6a-agy.md`.
- Tests at the final HEAD: SCUMM-only **677** OK, ASan SCUMM **677** OK, SCUMM+SCI **756** OK, no-engine **644** OK (N0 + 33);
  mingw `test/runner.exe` built (PE32+, run UNVERIFIED). `sync-svf.sh --check` rc 0, `mkgolden.py --check` rc 0, tools
  tests OK (`test_trscheck.py` 6). `check-commit-msg.py` ok on every commit of the round, one `Assisted-by: Claude Code`.
- Runs with the final build (no `encoding.dat`, `~/work/tmp/p6/runs/fz-*`, `fa-*` for ASan): MI2 pack, MM, Zak, Indy3, MM V1
  English (0 Korean/UTF-8/TENC lines), Indy4, MI1: 0 assertions, 0 `has no glyph`. Legacy CP949 saves: Loom slot 2
  (`fz-loom2-*`) and DOTT slot 1 (`fz-dott-*`) load to the frames of upstream's load (Loom 2 and DOTT identical; Loom slot 1
  second frame differs in one 30x30 cursor box), MI2 slot 2 identical (`f2-mi2s2`, 18 resources converted: ASCII and
  non-text resources are no longer rewritten). The Korean save name repro: `1. 한글 저장` (`f9-sn1`).
- ASan (`fa-*`): MI2 pack, MI1, MM, Indy3, Zak, Loom and DOTT legacy saves: no report.
- The first notes' claim "the engine gives U+00FA" for MI1 UTE line 334 held only for the CP949 bundle; the UTF-8 pack's
  raw 0xFA is now read per character (fix 7).

## Fix round 2 (re-reviews of 4fd29b336e0)
Findings and evidence: `P6a-fixes.md` (Round 2). Final HEAD tests: SCUMM-only **684**, ASan SCUMM **684**, SCUMM+SCI **763**,
no-engine **644**, all OK; mingw `test/runner.exe` built (run UNVERIFIED). Runs: MM with the lone-0x80 bundle under ASan,
MI2 legacy slots 2 and 6 under ASan (20 and 21 resources converted, 0 reports; slot 2 text rows identical to upstream,
only the animated water differs under ASan timing), MI2 pack and MM, English MM V1 (0 Korean/UTF-8/TENC lines).
