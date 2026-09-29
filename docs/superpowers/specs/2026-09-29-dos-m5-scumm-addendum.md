# ScummVM DOS 포트 — M5 설계 부록: SCUMM (Monkey Island 1/2)

- 날짜: 2026-09-29
- 본 설계: `docs/superpowers/specs/2026-09-28-scummvm-dos-sdl3-design.md` (원칙 1–4, 3.5 로딩 화면, 4 폰트/프리셋, 5 사운드/타이머, 7 빌드, 8 수락). 이 문서는 그 위에 **달라지는 것만** 적는다.
- 계획: `docs/superpowers/plans/2026-09-29-dos-m5-scumm.md`
- 상태: 사용자 결정 반영, 구현 전

## 1. 결정 (사용자 확정)

| 항목 | 결정 |
|---|---|
| 실행 파일 | 엔진마다 따로. `SCUMMVM.EXE`(SCI, 지금 그대로) + 새 `SCUMM.EXE`(SCUMM v0–v6 만: `scumm_7_8`, `he` 빼고). 백엔드는 같은 `backends/platform/dos`. `build-dos.sh` 첫 인수로 `sci`(기본)/`scumm` |
| 게임·모드 | MI1 한국어(DUMB 2005 EUC-KR 제자리 패치, VGA 플로피): **L**(8비트, KS X 1001 2350 SVF), **U**(트루컬러, 안티에일리어싱, 두 얼굴 방식), **패치 폰트 모드**(패치 자신의 `korean0N.fnt`). MI2: 영어, 한국어 L, 한국어 U (`korean.trs`) |
| 폰트 선택 | SCUMM charset(대사, 동사/문장줄, 제목·크레디트 카드 등)마다 후보 한글 얼굴을 원래 영어 charset 렌더링 옆에 놓은 비교 시트를 만들고 **사용자가 고른다**. 컨트롤러는 추측하지 않는다(계획 Task 8 에서 멈춘다) |
| 사운드 | M3 의 OPL(`dosopl`)·MPU-401(UART) 그대로. MI1/MI2 모두 iMUSE(AdLib 은 `MidiDriver_ADLIB` → OPL, MT-32 는 `IMuseDriver_MT32` → MPU-401). SB PCM 은 게임이 쓸 때만(SBL 리소스) SDL3 로 |
| 배포 | MI1 zip, MI2 zip (개인용 — 상용 게임 포함). KQ1/LB1 zip 과 같은 배치(`SCUMMVM\`, BAT 는 `echo Loading ScummVM...`) |
| 영어 MI1 | 데이터가 아직 없다. 데이터 경로가 생기면 도는 선택 과제로 둔다 |

## 2. 확인된 사실 (2026-09-29, 코드·데이터로 확인)

- **MI1 VGA 플로피는 SCUMM v4 다** (`detection_tables.h`: `GID_MONKEY_VGA`, version 4, `000.LFL`+`DISK0N.LEC`+`90N.LFL`). MI2 는 v5. 둘 다 `scumm` 엔진 본체(v0–v6)로 돈다 — "v5 게임" 이라는 묶음은 실제로 v4+v5 다. Linux 하네스의 MI1 타깃은 `gameid=monkey extra=VGA language=ko`.
- 두 게임 모두 음악은 iMUSE (`ScummEngine::setupMusic`, version≥3). AdLib 은 `MidiDriver::createMidi(detectDevice(MDT_ADLIB))` → `audio/adlib.cpp` → `OPL::Config::create` → DOS 에서 `dosopl`. 타이머 요청 250 Hz 는 M3 처럼 `RealChip` 이 100 Hz 로 누른다. iMUSE 는 타이머 프로시저(IRQ0 안)에서 돈다 — M3 의 분할 힙 전제(타이머 프로시저는 256 KB 미만 블록만)를 SCUMM 사운드 리소스도 지켜야 한다(MI2 사운드 리소스 최대 크기를 Task 2 에서 잰다).
- **SCUMM 의 시뮬레이션은 루프 단위로 결정적이다**: `go()` 의 `delta` 는 벽시계가 아니라 `VAR(VAR_TIMER_NEXT)` 이고, 늦은 프레임은 `waitForTimer` 가 다음 루프에서 기다림 없이 따라잡는다. 루프 수와 입력이 들어간 루프 번호가 같으면 상태가 같다(예외: `VAR_MUSIC_TIMER` 등 실시간 음악 값, `random_seed` 없는 난수). 그래서 **동결(freeze)** 만 있으면 DOS 와 리눅스의 덤프를 바이트 단위로 맞댈 수 있다(3.3).
- **SCUMM hi-res 텍스트는 SCI 와 다른 길이다** (`engines/scumm/hires_text.cpp`, `HIRES_TEXT.md`): 텍스트 표면을 `scale`(DOS 에선 2) 배로 키워 게임 위에 합성한다. 백엔드 요청 크기는 320×200×scale = **640×400**. 맵이 `alpha=true` 면 SCUMM 은 SCI 와 달리 맵을 따른다 — `getSupportedFormats()` 의 4바이트 포맷(DOS: XRGB8888)을 고르고 없으면 CLUT8 로 떨어지며 블렌딩을 끈다. 그러니 **SCUMM U 는 `rgb_rendering` 이 아니라 맵의 `alpha=true`** 로 켠다.
- SCUMM 은 `hires_text_map` 의 상대 경로를 **게임 디렉터리 기준**으로 푼다(`HiResFontMap::resolvePath(value, gameDir)`). SCI 의 `DATA/KQ1KOU.MAP` 식은 SCUMM 에서 틀린다 → SCUMM 타깃은 `extrapath=DATA` + `hires_text_map=data:M2KOU.MAP` (`data:` 는 extrapath 에서 찾는다).
- 한국어 패치 폰트가 격자를 정한다 (`korean0N.fnt` 머리말, 실측):

  | charset | MI1 | MI2 | 2배 셀(W×H) |
  |---|---|---|---|
  | 0 | 11×12 (shadow 2) | 11×12 | 22×24 |
  | 1, 3 | 8×8 | 8×8 | 16×16 |
  | 2 | 9×9 | 9×9 | 18×18 |
  | 4 | 13×12 | 13×12 | 26×24 |
  | 5, 8 | — | 8×8 | 16×16 |
  | 7 | — | 12×12 (shadow 3) | 24×24 |

- `gamedata/mi2kor/korean.trs` 는 `SCVMTRS` 이진 묶음이고 본문은 **CP949** 다(BOM 없음, 문자열이 EUC-KR 로 디코드된다). 사용자가 말한 "UTF-8" 과 다르다 — 엔진은 둘 다 받으므로(`useUtf8Text()`) 계획은 파일의 BOM 으로 가린다. CP949 디코드는 `encoding.dat`(공통 `str-enc.cpp`)가 있어야 한다.
- MI2 폴더의 `monkey2.sog`(135 MB, Ogg 음성)는 이 빌드에 Vorbis 가 없어 쓸 수 없다 → 배포에서 뺀다. `hr*.fnt`/`svfn*.fnt`/`*.map`(리눅스 실험용)도 뺀다. 게임 폴더에 `hires_text.map` 이 있으면 SCUMM 이 자동으로 읽으므로 DOS 게임 폴더에는 절대 두지 않는다(8.3 에선 `HIRES_TE.MAP` 로 잘려 안 읽히지만 의존하지 않는다).
- 기존 디버그 소켓은 SCUMM 에서도 범용 명령은 된다(`key`/`type`/`click`/`move`/`wait frames`/`dump`(=`lockScreen`, `<path>`·`.txt`·`.pal`)/`shot`/`save`/`load`/`millis`/`rtc`, 콘솔의 `room`·`scumm_room`·`actor`…). 없는 것: 게임 상태 조건 대기, 동결, 엔진 버퍼(주 가상 화면, hi-res 텍스트 면) 덤프. SCI 는 이것을 `engines/sci/debugsocket.*` 확장으로 가진다.
- 로딩 화면(3.5)은 엔진 무관이다: 이정표는 창 제목·`engineInit`·첫 모드·첫 `updateScreen`·첫 내용 프레임이고, 한국어 캡션은 `language=ko` 로 정한다. 320×200 에도 들어간다(캡션 108 px). 달라질 수 있는 것은 "게임 프레임" 판정뿐 — MI 인트로는 어두운 화면으로 시작한다.
- 백엔드에 SCI 전용 흔적 두 곳: `timerSelftest()` 가 `"SCUMMVM.EXE"` 를 연다, `fatalError()` 가 `SCUMMVM.LOG` 를 가리킨다. `setShakePos` 는 이미 있다.

## 3. 구조 변화

### 3.1 빌드

```
build-dos.sh [sci|scumm] [configure 추가 인수]
  sci   → build-dos/       --enable-engine=sci --disable-engine=sci32       → dist/dos/SCUMMVM.EXE
  scumm → build-dos-scumm/ --enable-engine=scumm --disable-engine=scumm_7_8,he → dist/dos/SCUMM.EXE
```

나머지 configure 인수(코덱·FreeType 끔 등)는 두 판이 같다. 첫 인수가 `sci`/`scumm` 이 아니면 예전처럼 `sci` + 그 인수(하위 호환). 두 판은 출력 디렉터리가 달라 서로의 `config.mk` 를 건드리지 않는다.

### 3.2 설정·로그 공유 (결정)

- **`SCUMMVM.INI` 하나를 두 EXE 가 같이 쓴다.** 둘 다 현재 디렉터리의 `scummvm.ini` 를 읽고 쓴다(`OSystem` 기본). 타깃마다 `engineid=` 가 있으므로 섞이지 않고, ConfMan 은 모르는 엔진의 섹션도 지우지 않는다. BAT 이 어느 EXE 를 부를지 정한다(`MI2KO.BAT` → `SCUMM mi2ko`). SCI 타깃을 `SCUMM.EXE` 로 부르면 "엔진 없음" 오류로 끝난다 — README 에 적는다.
- 로그도 `SCUMMVM.LOG` 하나(추가 쓰기). 대신 백엔드가 실행마다 첫 줄에 `DOS: <EXE 이름> <버전>` 을 남기고, 자기 검사·오류 문구는 `argv[0]` 의 이름을 쓴다(하드코딩 제거).
- 세이브는 `SAVES\<target>.sNN` — 타깃 이름은 8자 이하: `mi1ko`, `mi1kol`, `mi1kop`, `mi2`, `mi2ko`, `mi2kol`.

### 3.3 SCUMM 디버그 소켓 확장 (범용 i18n/테스트 기능, DOS 와 별도 커밋)

SCI 선례대로 `engines/scumm/debugsocket.{h,cpp}` 의 `Scumm::DebugSocket : GUI::DebugSocketExtension`, `ScummDebugger::debugSocketOpened()` 에서 붙인다. 최소 명령:

| 명령 | 내용 |
|---|---|
| `state` | JSON 한 줄: `loop`(scummLoop 횟수), `room`, `ego`(x,y), `talking`, `haveMsg`, `userPut`, `cursor`, `frozen`, 최근 문자열 8개(`charset`, UTF-8 텍스트, 원래 바이트 16진, 텍스트 면 좌표 사각형) |
| `wait <cond> [freeze]` | 조건이 참이 되는 루프 끝에서 `OK <loop>`(한도 넘으면 `TIMEOUT`). 조건: `room == N`, `text "부분문자열"`(UTF-8), `seen <16진 바이트>`(게임이 그린 원래 바이트), `talkdone`, `userput`, `loops N`, `timeout N`(루프). `freeze` 면 그 루프에서 동결 |
| `freeze` / `run [N]` | 동결: `go()` 가 `scummLoop()` 를 건너뛰고 `waitForTimer()`(이벤트·화면·소켓)만 돈다. `run` 은 대기 중인 입력이 모두 게임에 넘어간 뒤 풀고, `N` 이 있으면 N 루프 뒤 다시 동결 |
| `dump <prefix>` | `<prefix>_low.bin`(주 가상 화면 8비트), `_vrb.bin`(동사 가상 화면), `_layer.bin`(hi-res 텍스트 면), `_cov.bin`(알파일 때 커버리지 면), `_pal.bin`(768B), `_out.bin`(`lockScreen`) 과 각각의 `.txt`(`w h bpp format`) — 모두 8.3 이내 |

입력이 들어가는 루프를 고정하는 규칙: 하네스는 **동결 상태에서만** 입력을 보내고 `run` 한다 → 입력은 동결이 풀린 첫 루프의 `processInput()` 에서 처리된다. 비교 대상 두 실행(리눅스·DOS)이 같은 루프에서 같은 입력을 받으므로, 같은 `random_seed` 에서 덤프가 같다.

### 3.4 폰트 (4장에 더함)

- DOS 에는 FreeType 이 없으므로 SCUMM 맵은 `[font.N] bitmap=` (charset 마다 SVFN) 만 쓴다. 셀은 2장의 표(패치 폰트 × 2), `mkfont.py --clip-cell` 로 구워 **셀 밖 잉크가 없다**.
- **16행 문제의 SCUMM 판**: SCI 는 모든 줄이 16행이라 17–18 px 얼굴이 넘쳤고 엔진 클립(`023c96813a0`)을 넣었다. SCUMM 은 charset 마다 줄 높이가 다르고(16/18/24 행) 글자를 게임 셀(`metrics=game`) 안에 그린다. 셀 = 줄 높이로 `--clip-cell` 로 구우면 넘칠 길은 두 슬롯(라틴/한글) 기준선 맞춤(`drawChar` 의 ascent 차 이동)뿐이다 → DOS 맵은 **`[latin] mode=off`**(ASCII 는 게임 원래 글꼴, 사용자의 "원래 LucasArts 글꼴과 어울리게" 요구와도 맞다)가 기본이고, 비교 시트에 라틴 얼굴 변형도 함께 보인다. 넘침 여부는 "대사가 지워진 뒤 텍스트 면에 남은 잉크 0 px" 로 잰다. 남으면 SCI 와 같은 범용 수정(글리프 잉크를 그 문자열의 줄 사각형으로 자름)을 `hires_text.cpp` 에 넣는다(계획 Task 9 의 조건부 단계).
- 부분 집합: MI2 는 `korean.trs` 의 모든 문자열, MI1 은 복호한 `DISK0N.LEC`/`000.LFL`(XOR 0x69) 안의 NUL 로 끝나는 EUC-KR 문자열에서 모은 글자. 두 경우 모두 KS X 1001 안(EUC-KR 이라 그 밖은 있을 수 없다). 이름은 8.3: `M1L0.SVF`…`M2U8.SVF`(게임·프리셋·charset), 같은 내용은 맵이 한 파일을 가리킨다. 맵: `M1KOL.MAP`, `M1KOU.MAP`, `M2KOL.MAP`, `M2KOU.MAP` (`dists/engine-data/hires_text/dos/`).
- 후보 얼굴과 라이선스: OFL — Galmuri(RFN 없음), Neo둥근모(RFN 있음 → 구운 파일은 다른 이름), NanumGothic Bold·Nanum Myeongjo Bold(RFN 있음), Gowun Batang Bold, Black Han Sans. MIT+RFN — hurss 의 DOSGothic/DOSMyungjo/DOSSaemmul/DOSIyagiBoldface/DOSPilgi/Sam3KRFont/MiraeroNormal(`~/work/scummvm/fonts/24px/LICENSE-hurss.txt`; 저작권 고지 동봉, 구운 파일은 RFN 이 아닌 이름). hurss 글꼴은 "도스 시절 비트맵을 추출했다" 고 밝히므로 원 비트맵의 권리는 불분명하다 — 시트에 표시하고 사용자가 판단한다.

### 3.5 백엔드 (작은 변경만)

- `argv[0]` 기반 EXE 이름(자기 검사 파일, 로그 첫 줄, `fatalError` 문구).
- 메모리 보고: `DOS: memory <phase> dpmi_free=<KB> largest=<KB> phys_free=<KB> phys_total=<KB>` 를 엔진 시작·첫 내용 프레임·종료에 남기고(`__dpmi_get_free_memory_information`), 범용 디버그 소켓에 DOS 전용 `mem` 명령(같은 값; 다른 플랫폼은 `n/a`)을 더한다. 스왑은 하네스가 `C:\CWSDPMI.SWP` 크기로 잰다.
- 그 밖(모드 선택, 포맷, 커서, 흔들기, 로딩 화면)은 SCUMM 에서 새로 필요한 것이 없다고 본다 — Task 5 에서 부팅으로 확인하고, 드러난 결함만 고친다.

## 4. 수락 기준 (M5, DOSBox-X 와 Staging 각각, memsize 16, cycles 60000)

| # | 항목 | 기준 |
|---|---|---|
| A1 | 빌드 | `build-dos.sh scumm` 성공, strip 한 `SCUMM.EXE` ≤ 10,485,760 B(보고). `build-dos.sh`(인수 없음)는 여전히 `SCUMMVM.EXE` |
| A2 | 모드 | `DOS: mode` 로그: `mi2`·`mi1kop` 320×200 CLUT8, `mi1kol`·`mi2kol` 640×400 CLUT8, `mi1ko`·`mi2ko` 640×400 RGB888@4 (`dos_truecolor=off` 면 U 가 CLUT8 로 가고 경고 한 줄) |
| A3 | 부팅 경로 | 타깃마다 로딩 화면(텍스트 → 그래픽 → 게임 순, 창 캡처로 관찰) → 제목/크레디트 카드 → 첫 장면 → 대사 한 줄, 모두 게임 상태 조건으로 도달 |
| A4 | 덤프 | 캡처 지점마다 동결 상태에서 `_low`·`_vrb`·`_layer`·`_cov`·`_pal` 은 리눅스 기준(FreeType 없는 `builds/linux-dos-scumm`, 같은 맵·폰트)과 **바이트 단위 동일**, `_out` 은 RGB 로 풀어 **0 화소 차**. 기준 자체의 재현성: 같은 리눅스 빌드 두 번 실행이 서로 동일해야 그 지점을 쓴다 |
| A5 | 실제 창 | 각 지점에서 에뮬레이터 창 캡처가 `_out` 과 맞음(`window_matches`: 밝은 칸 ≥ 5%, 평균 채널 차 ≤ 12) — CLUT8 과 XRGB8888 모두 |
| A6 | 남는 잉크 | 한국어 L·U 각 게임에서 대사 3줄 이상: 줄이 지워진 뒤 `_layer` 가 그 줄 전의 `_layer` 와 같음(0 px) |
| A7 | OPL | MI2 영어 부팅부터 20 초(입력 없음), `opl_log`: 리눅스와 레지스터 공통 프리픽스 ≥ 1000 줄, 키온 ≥ 200, M3 방식 타이밍 — 중앙값 ≤ 2 ms, 방 전환 구간 밖 최대 ≤ 10 ms, 안 ≤ 20 ms. MI1 은 부팅 10 초, 프리픽스 ≥ 1000, 키온 ≥ 100, 같은 타이밍 기준 |
| A8 | MT-32 | MI2 `music_driver=mpu401 native_mt32=true` 20 초: 리눅스 `--dump-midi` 와 노트/채널 메시지 공통 ≥ 300, 타이밍 중앙값 ≤ 2 ms |
| A9 | PCM | 캡처 구간에서 리눅스가 SBL 을 재생하면 DOS 의 `triggerSound` 순서가 같다(`debugflags=SOUND`); M3 믹서 자기 검사 ±2% |
| A10 | 메모리 | 모든 타깃이 memsize 16 에서 모든 지점을 통과. 보고: 첫 내용 프레임과 마지막 지점의 `phys_free`, `CWSDPMI.SWP` 크기. 스왑 > 2 MB 는 WARN(실패 아님) |
| A11 | 시작 시간 | `main()` → 첫 게임 프레임(로딩 화면 로그) 보고; 8,000 ms 넘으면 실패 |
| A12 | 회귀 | 같은 머리(HEAD)에서 다시 빌드한 `SCUMMVM.EXE` 로 LOADING/M0/M1/M2/M3 수락이 두 에뮬레이터 모두 PASS; 리눅스 `make test` 새 실패 없음 |
| A13 | 배포 | MI1·MI2 zip 을 새로 풀어 DOSBox-X 실제 창에서 BAT 마다 로딩 화면 → 게임 |

성능(스크롤하는 방에서 초당 루프 수, 640×400 XRGB8888 합성 비용)은 **보고만** 한다 — 판정은 실기(M4).

## 5. 위험

| 위험 | 대응 |
|---|---|
| 변형 감지 실패 대화상자(DUMB 패치의 모르는 md5 등)가 오버레이 없는 DOS 에서 안 보인 채 키를 기다림 | Task 2 에서 리눅스로 확인; 뜨면 `scumm-md5.h` 에 항목 추가(범용, 별도 커밋) |
| iMUSE 타이머 프로시저가 256 KB 이상 블록을 만짐 → ISR 페이지 폴트 | Task 2 에서 MI1/MI2 사운드 리소스 최대 크기 측정; 넘으면 M3 규칙대로 그 블록 잠금 |
| `encoding.dat` 을 못 찾아 CP949 가 안 풀림 | 로그의 `encoding.dat is not found` 검사; `DATA\ENCODING.DAT` + `extrapath=DATA` |
| 동결로도 안 맞는 지점(음악 타이머에 묶인 스크립트) | 리눅스 두 번 실행 재현성 검사로 걸러 다른 지점으로 바꾼다 |
| 640×400 XRGB8888 전 화면 합성이 느림 | 보고만(M4 실기); L(CLUT8)이 대안 |
| hurss 글꼴 원 비트맵의 권리 | 시트에 표시, 사용자 결정 |
