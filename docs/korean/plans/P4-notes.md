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

## P4b: QFG2 (scratch data `datah/qfg2`, runs `~/work/tmp/p4/b*`)

Run harness: `go.sh` (Xvfb 800x600, xdotool), console teleport `send ?Trial newRoom 199` as in the plan. A teleported hero
has no name, so the door's `%s` prints empty (`내 집에 자유로이 들어오게, .`): not a bug of the engine.

- **b1** suite row `QFG2` OK (all seven rows OK on `datah`). Piracy notice viewed: ten centred Hangul lines, legible. clut8 frame
  sha256 `cf99f8c17e3dc48f...`, rgb565 frame `0dc79494266510e4...`; the two frames differ **only** in the mouse cursor (the cursor
  sprite is drawn at a different place in the two runs: two 32x32 squares at (300,200) and (559,368)); with those two squares
  blanked the frames are pixel-identical (checked with PIL), so the 1 bpp text is identical on clut8 and rgb565. Status line
  after the teleport (`퀘스트 포 글로리 2:  불의 시련  [0 / 500]`, y 0-16) legible. Room-199 frame sha256 (door opened) `99be3da294b2216c...`.
- **b2 red** (P3 binary, same keys): the door answers `나를 찾기 전에 누가 그대를 보냈는지부터 더 알아보게.` (text 199 #14), no
  `answer` log line (viewed).
- **b2 green** (this tree): log `answer "두드려" reads as "knock rap"`, `answer "키폰" reads as "keapon laffin"` (the cache prints a
  repeated answer once); the door asks the next question. The game picks the question at random but deterministically per
  keystroke timing: five runs asked #11 (`마법 상점 주인에게 가장 어울리는 원소는 무엇인가?`). Answers tried on it:
  `공기` (air, `rhdrl`) -> log `reads as "air"`, door goes on to the riddle (#16, `...(영어 철자로 생각해 보라.)`): **passes**;
  `악마` (demon), `와지르` (`minister vizier wazir`), `전부` (`all every`), `펜러스` (`fenrus mouse rat`) -> all refused with
  #15 `청하지 않고 온 낯선 이는 만나지 않네.` (negative controls). Riddle answer `아지자` (`dkwlwk`) reads as `aziza azizah
  enchantress sorceress` -> door opens (#18) and the hero walks in to Aziza's room (score 7/500). Answers to #9 (Rakeesh's
  leg: `악마` -> `demon`), #10 (Ja'afar: `와지르`), #12 (WIT: `전부`) and #13 (Erasmus's companion: `펜러스` -> `fenrus`) are
  **UNVERIFIED in play** (the game did not ask those questions in any of the runs); the table reads each as the expected English
  word(s), which is what the game's search needs. The correct answers to #10/#12 are my reading of the QFG2 walkthrough, not
  checked against script 2.
- **b2 step 4** English answer with F12 (composer off): `knock`, `keapon` x2 -> the same next question (#11), no `answer` log line:
  ASCII keeps the game's own search.
- **b2 ASan**: the whole door sequence to Aziza's room on `~/work/tmp/scummvm2026-p4asan` (`detect_leaks=0`): no AddressSanitizer or
  runtime-error line, same `answer` lines.
- **b3** Aziza's room (room after the door; a town room needs the printed-manual copy protection after a teleport: every
  teleport to rooms 100/300/400/500 showed `아, 그건 그렇고 ... 이 게임을 무사히 끝내려면 인쇄된 설명서에 담긴 정보가 필요합니다`, not
  passed; room 150 hangs the interpreter, an invalid room): typed `ansdmf qhk` (문을 봐) -> `현관문이다. 여기로 들어왔다.` (the
  English game answers "It's the front door. You came in here" to look at door: same meaning); `ansdmf` + Return alone ->
  `부디 용서하십시오, 에멘디. 천 번 사죄드리지만 무슨 말씀인지 이해하지 못했습니다` (Aziza's "do not understand" reply); the input box shows
  `문을` while typing. `qhk` alone in this room printed the room description (`공기는 눅눅하고 장식은 온통 바다색이다. 아지자는
  집을 깔끔하게 가꾼다.`). Save: F5 -> dialog `게임 저장` / `이 저장 게임의 설명을 입력하세요.` with Korean buttons `저장` `취소`
  (`Change Directory` button English: not a script string), typed `ans` (문), Return -> file `qfg2-ko.001`; F7 `게임 불러오기` lists `문`
  (the game's list, no boxes). The restore (Return on the list) was not run; `originalsaveload=false` ScummVM dialog not run.
- **b3 step 3** `grep -c 'cut to\|grown from\|longer than'` over all QFG2 runs: 0.
- **b4** k16said: pending `ko/07-debugsocket` (`k16said.py --game qfg2` target pass >= 1654 of 1876).
- Checkpoint B: the answer-room design held: b2 passed with no engine change beyond P4a. No stop needed.

## P4c: QFG1 EGA (scratch data `datah/qfg1ega`, runs `~/work/tmp/p4/c*`)

New driver for interactive runs: `~/work/tmp/p4/sess.sh RUNDIR start|do|shot|stop` (one long Xvfb + scummvm session, scratch only).

- **c1** suite row `QFG1EGA` OK. First screen (piracy notice, five lines + closing, Hangul legible) clut8 sha256 `efcae51666868590...`, rgb565
  `fb58e2ba227d88f4...`; the frames differ only inside one cursor-sized rectangle (300,200)-(352,232) (cursor position), the text is identical.
- **c2 hero**: character creation viewed in Korean (`CHOOSE YOUR CHARACTER` is a picture; the stats sheet `이름: 힘 지능 민첩 체력 운 ... 게임 시작 취소`
  is Korean; name `한영웅` typed with the composer, status line `영웅이 되고 싶다고? [점수 1 / 500]`, first narration `모험에 행운을 빌지, 한영웅.`).
- **c2 gargoyle (room 29)**: teleport with `vmvars g 13 29` (as the memory note says; the plan's `send ?HQ newRoom 29` breaks), then the arrival texts
  (`완전 무장을 하고 마터호른을 오른 기분이다.`, `입구 위에는 못생긴 가고일이 있다.`), the typed command `가고일에게 말해` (`rkrhdlfdprp akfgo`),
  the gargoyle's `마법사를 만나려는 자는 먼저 세 가지 질문에 답해야 한다.` and his first question `이름이 무엇이냐?` (text 29 #33) with an edit box. The hero's name
  answer is accepted; the log shows the view consulted in room 29: `answer "한영웅" reads as ""` (unknown words: read as typed, as designed) and,
  for `에라스무스` (the plan's key `dpfktmanta` is a typo: it composes `에라스뭇ㅁ`; correct `dpfktmantm`): **`answer "에라스무스" reads as "erasmus wizard"`**.
  The gargoyle then threw the hero off the gate (wrong answer for the question being asked, which the headless run could not read: after the first question
  the later question texts never showed on a screenshot taken every second). So the row for room 29 is **proved needed** (the script reads the box with
  StrLen/StrAt and the view feeds it the English words) but **a full pass of the three questions in Korean is UNVERIFIED** (questions are drawn from text 29 #33-#43;
  the log lines for the questions that came up are above). The P3-binary red was not run.
- **c2 hut (room 22)**: not run (needs the walk to Baba Yaga's hut and the rhyme); row stays UNVERIFIED per plan section 0.6 item 1.
- **c2 step 3** save: `F5` -> `이 저장 게임의 설명을 입력하십시오.` with `저장` `취소` `디렉터리 변경`, typed `ans` (문) + Return, `F7` `불러올 게임을 선택하십시오.` lists `문`
  (the `Restore` button stays English: not a script string). A typed `ansdmf qhk` in the gate room opened the game's credits window (`하이로즈 퀘스트 I 제작진`, the
  game's own Korean menu text) rather than a look reply; the parser path itself was proved on QFG2 and KQ4/LB1 runs. `grep -c 'cut to\|grown from\|longer than'` over all
  c-runs: 0.
- **c2 step 4** k16said: pending `ko/07-debugsocket` (target >= 855 of 956).

## P4d: LSL1 EGA and VGA (scratch data `datah/lsl1ega`, `datah/lsl1vga`; runs `~/work/tmp/p4/d*`)

- **d1** suite rows `LSL1EGA`, `LSL1VGA` OK. Both: the warning box (`철갑상어 보건총감의 경고` / `"레저 수트 래리: 라운지 리저드의 땅에서"에는 일부 어린이에게 부적절할 수 있는 내용이 포함되어 있습니다.`)
  and the age dialog (`거짓말하지 마세요!`, `그런데 나이가 어떻게 되시나요?`, six buttons `15세 미만 16~18세 19~39세 40~65세 66~99세 100세 초과`, blue hint `TAB 키로 항목을 선택하고 ENTER 키를 누르면 계속합니다.`)
  legible at 2x on the EGA palette and over the VGA 256-colour picture. Log `Hi-res font set for font 0 / 59 / 1207`, **no id >= 2048** (Task a5 on game data: 2107 draws through 59's set). Age frame sha256: EGA `bc52b1d67ca608f1...`,
  VGA clut8 `5e378507e9ef80d4...`. **VGA clut8 vs rgb565: the whole 640x400 frame is pixel-identical** (PIL diff 0 px, also the dialog crop 190,140-455,262).
- **d2 into the game** (EGA): Tab to a button + Return -> `그래, 그러시겠지!` (or `이상하네, 나이가 훨씬 더 들어 보이는데!` for 40~65), then `나이를 확인하기 위해 간단한 문제 다섯 개에 답해 주세요.`, then Return enters Lefty's street
  directly: **the pack skips the quiz** (SCRIPT.720). `15세 미만` + Return -> `죄송하지만 이 게임은 성인만 플레이할 수 있으며, 미성년자는 성인과 함께해야 합니다. 어른과 함께 돌아와 다시 시도하세요.` An accidental walk into the traffic showed the death
  dialog fully Korean (`말 그대로 네가 사방에 널렸군!`, `래리, 여기 교통은 정말 살인적이야! 길을 건너려는 생각은 버려!`, buttons `복원 재시작 종료`). Icon bar (pictures: no text). Look icon on the pole sign: `전봇대에 표지판이 붙어 있다.`; on the street: `이곳의 꼴을 보고 있자니, 다음에는 컴퓨터 게임에 돈을 좀 더 쓰는 편이 낫겠다고 생각한다!` (box and wrap correct).
- **FINDING (layout, EGA): a reply box that carries the look icon hides the first two syllables.** Look at the bar door (script string 100:15, `푹신하게 누벼 놓은 솜씨가 제법이지?`) shows `[eye icon]하게 누벼 놓은 솜씨가 제법이지?` (`푹신` is missing: the icon sits on the text's first 32 px).
  Measured with a temporary debug line in `GfxText16::Box` (not committed): the text rect is `4,4,144,14` in window coordinates for this box and for icon-less boxes alike (`tw 140`, left aligned, offset 0), i.e. the box is sized from the text width alone and the icon is
  drawn over the start of the text. Why the English game does not overlap (text indent for the icon in the script's own sizing, or English text short enough) was not established; no English comparison run was made (the English quiz is not skippable). Not fixed: it needs its own investigation (candidate: how the script derives the box width from `kTextSize`
  plus the icon's `CelWide`, or the text plane cleared where the icon is drawn, P3 task 14). Recommend a follow-up card.
- **d2 save**: LSL1 EGA/VGA open the game's own Korean control panel from the icon bar's slider icon (`SAVE RESTORE RESTART QUIT PLAY`, `GAME PAUSED`, `DETAIL VOLUME SPEED` are pictures, English; `점수: 0 / 222` is text). With `originalsaveload=true`: save dialog `게임 저장` / `저장 게임의 설명을 입력하세요.` with `저장 삭제 폴더 변경 취소`; restore dialog `게임 복원` / `복원할 저장 게임을 선택하세요.` lists the saved name (`Restore` is a picture).
  **FINDING (input): the Korean composer is off in LSL1** (typing `ans` gave `ans`, not `문`): `hangul_input` defaults to "the game has Korean parser words" (`event.cpp:165`) and LSL1 has no word table, so without an OS IME a Korean save name cannot be typed. With `hangul_input=true` in the ini it should work (a run with it did not produce a save in the time available: UNVERIFIED). Without `originalsaveload` the VGA game uses the ScummVM save grid (English labels, `New Save`): not checked with Hangul.
- **d3 palette** (VGA, clut8): the title/warning picture, the warning box, the age dialog and the room follow each other with the yellow boxes in the right colours; no stale text from the previous screen over the next picture (viewed frame by frame, 4 frames). A fade-specific pair was not taken (no fade of text observed).
- **d4 quiz**: no quiz question is reachable on the Korean pack (skipped by SCRIPT.720); Ctrl+Alt+X on the title and on the age dialog did nothing visible. `grep -c non-raw run.log` = 3 through the title, warning, age dialog, refusal, room, look replies (the three start-up `kReadNumber` warnings). **Appendix A is not applied** (no growth observed); the quiz path itself remains unmeasured. 
- `grep -c 'cut to\|grown from\|longer than'` over all d-runs: 0. No k16said (no parser).

## P4e: KQ4 (scratch data `datah/kq4`, runs `~/work/tmp/p4/e1`, `e1r`)

- **e1** suite row `KQ4` OK. Copy protection (`소유권을 확인하기 위해 "킹스 퀘스트 IV" 설명서를 보고 다음 질문에 답하시오. 답은 설명서에 적힌 영어 그대로 입력하시오.`, question `4쪽 세 번째 문단의 마지막 단어는?`, a random one of several: the rgb565 run drew `... 네 번째 단어는?`):
  typed `a` + Return passes (H7 `SCRIPT.701`). The credits are pictures. clut8 vs rgb565 first frame: identical except the random question's last words and the cursor.
  Intro narration boxes Korean (`그레이엄 왕이 낡은 모자를 아이들 쪽으로 던지고, 아내 발라니스 왕비가 자랑스럽게 지켜본다.`, `나예요.` 목소리가 말한다. `마법 거울을 보세요.`, Genesta's boxes with the dark title bar `제네스타` / `로젤라`,
  `... 나는 24시간 안에 죽어요.` with the 16 px digits legible). Escape does not skip the intro; **the intro took about 7 minutes of wall time here** (the plan said about 3) before control was given on the Tamir beach (`자, 이제 혼자가 되었다, 로젤라.`).
- **e2 first room**: status line `점수: 0 / 230   KQ IV The Perils of Rosella` (the `IV` is a picture glyph); typed `qhk` (봐) -> prompt `입력하세요` with `봐`, Return -> 4-line reply `동쪽에서 흘러온 강이 눈앞의 푸른 바다로 흘러든다. ...`; `바위를 봐` (`qkdnlfmf qhk`) -> `여기에는 바위가 많지 않다.` (a noun + particle sentence parsed by the Korean parser; the English game's reply to look at rock is the "not many rocks" line).
  Menu bar (Escape): `파일 동작 속도 소리` with `KQ4 정보 CTRLA` / `도움말 F1` in the pull-down, legible.
  Save (`originalsaveload=true`): F5 `게임 저장` / `저장할 게임의 설명을 입력하십시오.` with `저장 취소 디렉터리 변경`, typed `ans` (문) + Return -> `kq4-ko.001`; F7 `게임 불러오기` / `불러올 게임을 선택하십시오.` lists `문`; Return on it restores the beach room (viewed). Frame sha256: first screen `929e92f51ba7fba8...`, the `바위를 봐` reply frame (status line, prompt/reply) `f66bfc09e6ce137b...`, the restored beach `c0001d4db9ee9ea3...`.
- `grep -c 'cut to\|grown from\|longer than\|non-raw'` over the whole KQ4 run log: 0.
- **e2 step 4** k16said `--early` (target >= 464 of 487): pending `ko/07-debugsocket`.

## P4f part 1: LB1 (scratch data `datah/lb1`, runs `~/work/tmp/p4/f1`)

- **f1** suite row `LB1` OK. First screen (see a8): fingerprint screen with the suspects' names in Hangul (`셀리`, `루디 디종`, `피피`, `필스 박사`, `거트루드 디종`; `지브스`, `대령 디종`, `글로리아 스완송`, `클래런스 스패로우`; `릴리안 프룬`, `로라 보`, `에델 프룬`) and the header `돋보기와 "대령의 유산" 게임 박스에 동봉된 맵 뒷쪽을 사용해서, 다음 지문을 식별해 주세요.`, all legible; the cursor over a name is the game's magnifier.
- **f2 step 1** a click on a name passed the check at once (the pack keeps `SCRIPT.414`, H7): `이제 막이 오르려 합니다. 착석해 주세요.`, then `이전에 '대령의 유산' 공연을 관람하신 적이 있습니까?` with `예` / `아니오` (clicked 아니오), the stage credits as text (`제작 책임자 / 켄 윌리엄스`), `1925 / LA 뉴올리언스 / 툴레인 대학`, the intro boxes (`"같이가자~네가 거기 가보면 입이 딱 벌어질 걸? ..."`, `"다왔어, 로라!"`,
  the Colonel's `"다들 알다시피 나는 아주 부유한 사람일세. ..."`), Laura's and her father's long monologue boxes (7-line box, wraps inside the frame, legible), all in Korean with the grey-framed boxes. The intro to control took about 5 minutes.
- **f2 in the room** (Laura's and Lillian's bedroom): typed `qhk` (봐) -> prompt `입력하세요` + `봐`, reply `여긴 릴리안과 함께 쓰는 객실이다. 조금 낡아 보이긴 하지만 그런대로 편안해 보인다.`; `침대를 봐` (`claeofmf qhk`) -> `각 침대 위에 가방이 하나씩 놓여 있다.` (noun + object particle parsed).
- **f2 step 2** (a scene of script 52): not reached (the plot scene needs a long walk): UNVERIFIED.
- **f2 step 3** save: F5 `게임 저장` / `이 저장 게임의 설명을 입력하세요.`, `저장 취소 디렉터리 변경`; F7 `게임 복원` lists the save. **FINDING: the saved description of LB1 carries stray `ㄴ` (U+3134) characters.** Saving `ans` (문) wrote the description bytes `E3 84 B4 EB AC B8` (`ㄴ문`) to `lb1-ko.001` (gzip header of the save, decompressed), and a second save started from the selected first entry typed `rlf` (길) became `ㄴ문길ㄴ`.
  The same typing in KQ4 (`문`), QFG2 (`문`), QFG1 EGA (`문`) and, later, Camelot (`문`, `문길`) gave clean names. U+3134 is what the composer gives for the key `s`; `sci-ko.str` has no `ㄴ`. **Probable cause, found while running Camelot: the screenshot hotkey itself.** In Camelot, `alt+s` (ScummVM's screenshot action, used by every headless run here) opened the parser line with a `ㄴ` in it (two screenshots gave `ㄴㄴ`), and `qhk` followed by a screenshot read `"봔"라는 말은 낯익습니다만`. So in some states the engine's Hangul composer receives the `s` of Alt+S (LB1 dialog: shot taken with the empty edit box open, hence the leading `ㄴ`; second save: shot taken after `rlf`, hence the trailing `ㄴ`).
  In Camelot's save dialog the same shot did **not** leak (`문` clean), and `q` + two screenshots showed only `ㅂ`; so it depends on the control that has the focus. Not fixed: whether the composer should ignore keys with the Alt flag (a real user pressing Alt+S in Korean mode) is an engine question for a follow-up; for this log every typed-text check was repeated or read with that in mind (saved names that came out clean: QFG2, QFG1E, KQ4, Camelot).
- **f2 step 4** English target on the pack folder: runs English (a8: 320x200, English text, no Korean log line); it lacks `en/SCRIPT.052` as H6 says (not compared).
- **f2 step 5** k16said (target >= 601 of 744): pending `ko/07-debugsocket`.
- `grep -c 'cut to\|grown from\|longer than\|non-raw'` over the LB1 run log: 0.

## P4f part 2: Camelot (scratch data `datah/camelot`, runs `~/work/tmp/p4/f4`, decision 10: kept, data available)

- **f4** suite row `CAMELOT` OK (`[korvocab] loaded 5362 Korean words (version 3)`, no header warning). Title picture, then the menu box `카멜롯 게임 선택:` with `인트로 보기` / `새 게임 시작` / `게임 불러오기` (legible, boxed). `새 게임 시작` -> the first text box `전하의 본거지 카멜롯(Camelot)의 평면도입니다. 지체 말고 중대한 사명을 준비하소서, 전하.` (5 lines inside the frame), then Arthur's bedchamber. Frame sha256: map box `...` first text frame `...` see run dir (not recorded beyond the viewing).
- **f5 typed command**: the line prompt reads `분부를 내리소서:`; `qhk` (+ a screenshot's stray `s`, see the LB1 finding) -> `"봔"라는 말은 낯익습니다만, 그 뜻하시는 바를 헤아릴 수 없습니다.` (the game's Korean "I do not know that word"); `문을 봐` (`ansdmf qhk`) -> `나무와 쇠로 만든 묵직한 문(door)입니다.` (noun + particle sentence parsed; the `(door)` is in the translation).
  Menus (Escape): `파일 게임 속도 행동 정보` with `저장 F5` / `불러오기 F7` / `다시 시작 F9` / `종료 CTRLQ`, `소리 끔 F2`, `음량... CTRLV`, `일시 정지 CTRLP`, `반복 F3`, and the credits box `카멜롯의 정복 제작진` (Korean headings, English names).
- **f5 save list (36-byte records, review M2 path on real data)**: two saves with Korean names (`ans` -> `문`, then `rlf` typed in a dialog that starts from the selected entry -> `문길`): files `camelot-ko.001` (name bytes `EB AC B8` = 문) and `camelot-ko.002` (`EB AC B8 EA B8 B8` = 문길), both clean UTF-8. The restore dialog `게임 불러오기` / `불러올 게임을 고르십시오.` lists `문길` and `문`, legible; **the arrow keys move the selection** (Down -> `문`, Up -> `문길`), Return restores the room (viewed). No warning in the log (`cut to`, `grown from`, `longer than`, `non-raw`, `Attempt`: 0).
- **f5 step 2** k16said (target >= 549 of 633): pending `ko/07-debugsocket`.

## ASan runs (final tree, `~/work/tmp/scummvm2026-p4asan`, `detect_leaks=0`)

QFG2 (the door sequence to Aziza's room, earlier), LSL1 EGA (age dialog, a room, a look), KQ4 (copy protection, credits), LB1 (fingerprint, seat dialog), Camelot (menu, new game, bedchamber, typed line): every log has 0 `AddressSanitizer` / `runtime error` lines. The ASan runs are slower, so the LB1 and KQ4 runs only got to the screens above in the time given.
