# P6 baseline: what SCUMM does today with the Korean data on this host

Status: measured 2026-11-23 on `ko-2026.4` at `4a8b9907b2c` (P0-P4; `git diff --stat v2026.4.0 HEAD -- engines/scumm` is
empty) and on plain `v2026.4.0` (`7d2709da513`). Nothing here changed the repository; every run used scratch copies under
`~/work/tmp/p6/`. Screenshots: `~/work/tmp/ko-screenshots/scumm-*.png`, one line each in `captions.txt` there.

## 1. How it was run

- Builds (out of tree, `heavy`): `~/work/tmp/p6/build-head` (branch head) and `~/work/tmp/p6/build-plain` (a detached
  worktree `~/work/tmp/p6/plain` at `v2026.4.0`), both
  `configure --disable-all-engines --enable-engine=scumm --enable-debug --disable-optimizations` (no `scumm_7_8`: v7/v8 are
  out of scope, decision 11). `make test` in `build-head`: `Running cxxtest tests (620 tests)...OK!` (a SCUMM-only build
  has no SCI suites; the 696 of the brief is the SCI build).
- Data: the harness test zips `~/work/scummvm/dist/scummvm-game-{mm,zak,indy3,indy4,loom,mi1,mi2,dott,mi1ute-win,mi2ute-win}-0ab70f653d3-test.zip`
  unzipped to `~/work/tmp/p6/data/<pack>/`; two hand-made folders: `data/mi2cp` (the MI2 pack with
  `gamedata/mi2kor/korean.trs.cp949` as `KOREAN.TRS`) and `data/mi1ute-{u,c}` (`gamedata/mi1en` MONKEY1.000/001 = MI1 CD
  English, with the MI1 UTE `korean.trs` UTF-8 or `.cp949` and its `korean0N.fnt`).
- Runs: `~/work/tmp/p6/bin/{mkrun.sh,sess.sh,autoshots.sh}` (copies of the P4 `sess.sh`: Xvfb 800x600, `--scale-factor=2`,
  xdotool clicks/keys, Alt+S screenshots, `-d2`, a retry loop for the known `SDL_BlitSurface` start flake). Run folders
  `~/work/tmp/p6/runs/<set>-{head,plain,tree}` hold `run.ini`, `run.log`, the PNGs and a `grid.png` montage. Every
  screenshot named below was viewed.
- Encoding census of every bundle: `~/work/tmp/p6/py/trsenc.py` (escape-aware: `0xFF`/`0xFE`, code, two argument bytes
  unless the code is 1, 2, 3 or 8, as `convertMessageToString()` treats v1-v6 text).

## 2. The data sets

| Set | Game / release (detection) | Korean text comes from | Bundle (sha256/16) | Font |
|---|---|---|---|---|
| mm | `maniac` V2/DOS/Korean, MD5 `b250d0f9...` | `KOREAN.TRS` CP949, 1024 lines, 41 rooms | `41edc57508e360b2` | `KOREAN00.FNT` 8x8 |
| mm (EN) | `maniac` V1/DOS/English (same pack, `GAMES/MM`) | none | - | - |
| zak | `zak` V2/DOS/Korean | `KOREAN.TRS` CP949, 1737 lines | `873417ba3ee7fca3` | `KOREAN00.FNT` 8x8 |
| indy3 | `indy3` VGA/DOS/Korean (v3) | `KOREAN.TRS` CP949, 2143 lines | `c30a2b46ca06a1db` | `KOREAN00/01.FNT` |
| loom | `loom` VGA/DOS/Korean (CD talkie data, v4) | `KOREAN.TRS` CP949, 1150 lines | `89bb2aea5f097c84` | `KOREAN01/02.FNT` |
| mi1 | `monkey` VGA/DOS/Korean (floppy, MD5 `2d1e891f...`) | inside the game data (`DISK0N.LEC`, DUMB patch); no `.trs` | - | `KOREAN00-04.FNT` |
| mi2 | `monkey2` "Unofficial SE Talkie v0.2"/DOS/Korean | `KOREAN.TRS` **UTF-8, BOM at the body start**, 8780 lines | `052ad23b747f6b1f` | `KOREAN00-08.FNT` |
| mi2cp | same game | `gamedata/mi2kor/korean.trs.cp949` (the harness's byte-fixed CP949 stage of the same text) | `1ea2d11f8e076355` | same |
| indy4 | `atlantis` CD/DOS/Korean | `KOREAN.TRS` CP949, 9099 lines | `4323dc20fb65a9b9` | `KOREAN01-03.FNT` |
| dott | `tentacle` Floppy/DOS/Korean (MD5 `e47bcf70...`) | inside the game data (`TENTACLE.001`); no `.trs` | - | `KOREAN00-08.FNT` |
| mi1ute-u / -c | `monkey` CD/DOS/Korean = **MI1 CD** data + the MI1 UTE bundle (UTF-8 BOM / CP949) | `korean.trs`, 5348 lines | `31461ae64d91f485` (UTF-8) | `korean00-04.fnt` |

Not run, with the reason: FT, Dig (`scumm_7_8`; v7/v8 out of scope, decision 11); `indy3mac`, `indy3towns`, `loomtowns`,
`loompce`, `mi1segacd`, `mi2towns`, `zaktowns`, FM-Towns/Mac/PCE/SegaCD (excluded platforms); MI2 UTE with the real UTE
`MONKEY2` data and `mi2ute-kor/korean.trs` (CP949, 2 bytes away from `.cp949`; the MI2 pack data is the same SE Talkie
release, so `mi2cp` stands for it); MI1 UTE with its own data (only the MI1 CD data `gamedata/mi1en` is on the host: the
bundle's in-game lines are keyed on UTE strings and do not match it, its credits do); the DOS port.

## 3. Results, branch head (`build-head`)

Every Korean target: `language=ko` in `run.ini` (detection already reports Korean for all of them: `korean.trs` in the
folder, or the MD5 tables for MI1 floppy and DOTT). Log of each: `Loading Korean Multi Font System`, one
`#N, size ..., height =H` warning per `koreanNN.fnt`, `loadLanguageBundle: Loaded N entries` where a bundle exists.

| Set | Detects | Starts | Text path | What the screen shows (viewed) | Problems |
|---|---|---|---|---|---|
| mm | yes, Korean | yes | upstream: `korean.trs` + `korean00.fnt` 8x8, 1x | kid select, intro, gate scene; verbs Korean (`밀기 열기 향하기 잠금 풀기 켜기 ...`); dialogue Korean (`자 가자.`, `여기 정말 위험할 수도 있어.`) | (1) **sentence line keeps English order and English object names**: `이게 뭐지? sign` (`scumm-mm-ko-sentence-sign.png`) although the bundle has `sign -> 표지판` (master line 718): v2 `drawSentence()` (`string.cpp:926`) appends OBCD names that `loadPtrToResource()` never translated; (2) the intro credits draw a Korean role line over the previous one (garbled cells, `scumm-mm-ko-credits-overlap.png`), the English V1 credits are clean; (3) the top pixel row of each 8x8 verb row looks clipped by the row above (`scumm-mm-ko-verbs.png`) |
| mm (EN) | yes, English V1 | yes | none | English credits and dialogue, no Korean line in the log (`scumm-mm-en.png`) | none |
| zak | yes, Korean | yes | upstream | credits `기획 및 디자인` + English names; office intro `흠...`; bedroom with Korean verbs and inventory `티켓` (`scumm-zak-ko.png`) | same sentence-line rule as MM (not probed separately) |
| indy3 | yes, Korean | yes | upstream | `안녕하세요, 마커스!`; gym: `원한다면 언제든지 F5키를 눌러 게임을 저장할 수 있다네. 자! 그럼 일단 달려!! ㅡ ㅡ>` and Korean verbs (`scumm-indy3-ko*.png`) | first verb row clipped as MM |
| loom | yes, Korean | yes | upstream | CD-audio notice (the pack's `CDDA.SOU` is empty), then the difficulty menu `표준레벨 / 연습레벨 / 전문가레벨` (`scumm-loom-ko-menu.png`), LucasArts and LOOM titles | dialogue not reached in the scripted run |
| mi1 | yes, Korean (MD5) | yes | upstream: CP949 inside the game data, `korean00-04.fnt` | credits `애니메이션 / 스티브 퍼셀, ...` over the title, `제 이름은 가이브러시 스립우드랍니다. 전 해적이 되고 싶어요!` (`scumm-mi1-ko.png`) | none seen |
| **mi2** | yes, Korean | **crashes at the first Korean string**: `charset.cpp:1367: CharsetRendererClassic::prepareDraw: Assertion 'charOffs < 0x14000' failed` (six starts, every one) | upstream reads the UTF-8 body as CP949 | nothing Korean is ever drawn | a UTF-8 lead byte (0xEA-0xED) is not a Korean lead for `is2ByteCharacter()` (`charset.h:60`, 0xB0-0xD0), so it is looked up as a 1-byte glyph past the charset's table |
| mi2cp | yes | yes | upstream | difficulty screen `선택하세요: 원숭이섬 2 ...`; `카리브 해 심연의 스컵 섬`; campfire `아냐. 난 보물을 찾고 있는 중이야.` (`scumm-mi2-cp949-ko.png`) | none seen |
| indy4 | yes, Korean | yes | upstream | credits `프로그래밍 : 마이클 스템리`, `목소리 관련 : 에릭 월문더`; close-up dialogue in two shadowed lines (`scumm-indy4-ko.png`) | none seen |
| dott | yes, Korean (MD5) | yes | upstream: CP949 inside the data, `korean00-08.fnt` | intro, lobby subtitle `래번, 큰문 안쪽을 확보해.` (`scumm-dott-ko.png`) | none seen |
| mi1ute-u | yes (CD/DOS/Korean) | crashes like mi2 after the title | UTF-8 body read as CP949 | last frame: the title (`scumm-mi1ute-utf8-last-before-crash.png`) | same assertion |
| mi1ute-c | yes | yes | upstream | `원안 및 디자인 / 론 길버트` over the title (`scumm-mi1ute-cp949-ko.png`) | in-game lines English (data mismatch, see section 2) |

## 4. Branch versus plain 2026.4.0

Same runs on `build-plain`: MM (credits overlap and the `sign` line identical in kind), MI2 with the UTF-8 pack (the same
assertion, `.../plain/engines/scumm/charset.cpp:1367`), MI2 with the CP949 bundle, Indy3 and Zak (Korean text on the same
screens; `runs/plain-ab.png`). Exact frame hashes are not comparable (timing of the scripted keys), the behaviour is the
same, as expected from the empty `engines/scumm` diff. The branch's shared changes (`graphics/hires_text`, `graphics/svf`,
`common/` Korean units) are not reached by SCUMM.

## 5. Facts for P6 found on the way

1. **The UTF-8 bundles are the shipped MI2 and MI1 UTE files** (`translations-in-harness`: "MI1/MI2 ship UTF-8"); both
   crash upstream and on the branch. Every other SCUMM bundle is CP949 and works upstream.
2. A **UTF-8 to CP949 conversion of the translated strings only, escape-aware, reproduces the harness's CP949 stage byte
   for byte**: MI2 8780/8780 lines identical to `korean.trs.cp949`; MI1 UTE 5347/5348 (line 1854 has a stray `0xFA` in the
   CP949 stage, which the UTF-8 file cannot carry) (`~/work/tmp/p6/py/trs2cp949.py`). The originals (lookup keys) must not
   be touched: they are the game's bytes.
3. Characters with no glyph in `korean.fnt` (the font holds the 2350 KS X 1001 syllables, rows 0xB0-0xC8;
   `get2byteCharPtr()` `charset.cpp:254` computes `((lo - 0xB0) * 94 + hi - 0xA1)` with no bounds check, and
   `is2ByteCharacter()` accepts leads up to 0xD0): MI2 2, MI1 UTE 1 (`퉝`), Indy4 37 (35 CP949-extension syllables such as
   `쓇`, `Ⅰ`, `├`), Indy3/Loom/MM/Zak 0. A lead below 0xB0 gives a negative index (read before the font buffer), a lead
   0xC9-0xD0 an index past it. Upstream bug, harmless only by luck so far.
4. The bundle's escape set is `0xFF` **and `0xFE`** (MI2 uses `FE 01` as its line break; `_newLineCharacter = 0xFE` for
   Korean, `charset.cpp:153`); an escape's argument bytes can be 0, so a string's end must be found escape-aware.
5. `encoding.dat` is needed for any conversion (`U32String::encode(kWindows949)` loads it, `str-enc.cpp:137`); without it
   every Hangul becomes `?`.
6. Old Korean saves (`~/work/scummvm/saves/monkey2-ko.s01..s11`, 2026-09-06, gzip, no `TENC` trailer, CP949 resources)
   exist and are the compatibility case for P6a's trailer.
7. Upstream's text destinations (`printCharIntern()`, `charset.cpp:1227`): the main screen with two buffers draws into
   `_textSurface`; a screen without two buffers (verbs, the v1-v3 text line) and `ignoreCharsetMask` draw into the 1x
   virtual screen; `_blitAlso` draws into the back buffer. Every `_textSurface` fill in `gfx.cpp` is already scaled by
   `_textSurfaceMultiplier` (lines 356, 1250-1270, 1535, 1603-1614, 2349, 4459) because FM-Towns runs at 2x. This is the
   ground the S2 gate stands on (P6-scumm.md section 6).

## 6. Prototype of P6a on the same data (`~/work/tmp/p6/tree`, `build-tree`)

`git archive 4a8b9907b2c` + the P6a code of `P6-scumm.md` (diff: `cd ~/work/tmp/p6/tree && git diff`), built like
`build-head`: `Running cxxtest tests (628 tests)...OK!` (620 + 8 new). On real data (`extrapath` = the worktree's
`dists/engine-data`, for `encoding.dat`):

| Run | Result (viewed) |
|---|---|
| `mi2u-tree` (the MI2 pack as shipped, UTF-8) | log `loadLanguageBundle: UTF-8 body converted to CP949, 841835 -> 738125 bytes`, `2 characters have no glyph in korean.fnt and show as '?'`; no assertion; Korean dialogue `나 좀 여기서 꺼내 주지 않을래?`, credits `원안 및 디자인 / 론 길버트`, `카리브 해 심연의 스컵 섬`, campfire lines (`scumm-proto-mi2-utf8-*.png`) |
| `mi2u-noenc-tree` (no `encoding.dat`) | `encoding.dat is not found`, then `The UTF-8 translation bundle cannot be converted ... not used`; the game plays in English, no assertion (`scumm-proto-mi2-no-encodingdat.png`) |
| `mi1uteu-tree` | `552956 -> 483700 bytes`, 3 `?`; Korean credits; in-game lines English (data mismatch) |
| `mm-tree2` | `이게 뭐지? 표지판` on the sentence line (`scumm-proto-mm-sentence-sign.png`); two-object lines not reached in play (MM shows the object on hover only for "What is") |
| `mi2save-tree` | `--save-slot=2` with `saves/monkey2-ko.s02` (old CP949 save) on the UTF-8 pack: `State loaded`, Korean verbs and inventory (`scumm-proto-mi2-old-save.png`) |

## 7. Not checked (honest list)

- Hangul save names (no IME in the headless runs; SCUMM has no parser, so input is only the save dialog), the games'
  original save/load screens with a Korean name, the GMM with a Korean description.
- Two-object v2 sentences in play, Zak's sentence line, the 40-character cut of the English path on Korean bytes.
- Loom dialogue, MI1/Indy4/DOTT beyond the first scenes, palette cycling (MI2 room 33), room transitions with text up.
- FT and Dig (v7), every non-DOS platform, the DOS port, ASan runs.
