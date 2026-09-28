# ScummVM DOS 포트 (SDL3 / DJGPP) — 설계

- 날짜: 2026-09-28
- 브랜치: `dos-port` (`fork/i18n` @ `191d9b05cb2` 에서 분기), 워크트리 `~/work/scummvm/dos`
- 상태: 설계 확정, 구현 계획 전

## 1. 목표와 범위

ScummVM `i18n` 브랜치를 SDL3 의 DOS(DJGPP) 지원으로 DOS 에 포팅한다. SCI0 엔진부터 시작하고,
i18n 브랜치의 hires 텍스트, 고해상도 폰트, 알파 블렌딩 텍스트를 DOS 에서 쓸 수 있게 한다.

| 항목 | 결정 |
|---|---|
| 목표 하드웨어 | Pentium 급 실기, RAM 16MB, VESA VBE 1.2+ (16bpp 는 2.0 권장), Sound Blaster 호환. 개발은 DOSBox-X / DOSBox Staging |
| 수락 게임 | King's Quest I (SCI 리메이크), Laura Bow 1 — 각각 영어 원본과 한국어 |
| 구조 | 전용 백엔드 `backends/platform/dos`, SDL3 는 하드웨어 추상 계층으로만 사용 |
| 사운드 | OPL(0x388) / MPU-401(0x330, UART) 직접 구동, PCM 은 SDL3 Sound Blaster |
| 타이머 | 보호 모드 IRQ0 핸들러, PIT 1kHz |
| 폰트 | DOS/V·HBIOS 폰트 API 는 쓰지 않는다. 배포 폰트 + 패치 원본 폰트 (3장) |
| 디버그 | 기존 debug socket 프로토콜을 COM 포트로 확장 (6장) |

범위 밖: SCI1 이상 엔진, MPU-401 인텔리전트 모드, 스케일러·셰이더·창 모드·화면 회전,
동영상/압축 오디오 코덱, mt32emu·fluidsynth 소프트 합성, 네트워크, 동적 플러그인.

### 1.1 확인된 사실 (2026-09-28 측정)

- SDL3 main (`1ce4c5b`, 2026-09-26) 이 DJGPP GCC 12.2 로 정적 빌드된다.
- SDL3 DOS 의 스레드는 협력형이다. 전환은 `SDL_Delay` / 이벤트 펌프에서만 일어난다.
  10ms 타이머가 1초에 92 회(Staging), 95 회(DOSBox-X) 불렸다.
- `SDL_HINT_DOS_ALLOW_DIRECT_FRAMEBUFFER` 로 시스템 RAM 서피스를 VRAM 에 바로 보낼 수 있다.
- 비디오 모드는 창 속성이 아니라 모드 목록에서 골라 `SDL_SetWindowFullscreenMode` 로 지정한다.
- 같은 S3 에뮬레이션이라도 모드 목록이 다르다. 640×400 에서 DOSBox-X 는 INDEX8/RGB565/XRGB1555/XRGB8888,
  Staging 은 INDEX8/XRGB8888 만 준다. 640×480 은 둘 다 RGB565 가 있다.
- i18n 의 SCI 는 텍스트를 `TextLayer` (hires 픽셀마다 색 인덱스 + 커버리지) 에 두고
  `Graphics::composeSpan()` 으로 어떤 `PixelFormat` 에든 합성한다. SCI 드라이버는
  `getSupportedFormats()` 에 트루컬러가 있으면 그것으로 `initGraphics` 한다.
  → 알파는 백엔드가 트루컬러 화면을 주기만 하면 동작한다.
- `graphics/fonts/ttf.cpp` 는 `FT_OPEN_STREAM` 으로 파일을 스트리밍한다 (8MB TTF 도 통째로 올리지 않는다).
- SVFN 은 1bpp / 8bpp, v1(코드 페이지 순서) / v2(자체 코드 포인트 표) 가 있다.
  `tools/korean/mkfont.py` 가 TTF 를 SVFN 으로 굽는다.

## 2. 구조

```
engines/sci (i18n 코드, 변경 최소)
   │  initGraphics(640x400, CLUT8 | RGB565 | XRGB1555 | XRGB8888)
   ▼
backends/platform/dos/   OSystem_DOS (ModularBackend)
   ├─ graphics/dos/      DosGraphicsManager  ── SDL3 창 서피스 (direct-FB)
   ├─ mixer/             SDL3 오디오 스트림에 ScummVM 믹서 연결 ── SB PCM
   ├─ events/            SDL3 키보드/마우스 → Common::Event
   ├─ timer/             IRQ0 1kHz → DefaultTimerManager 콜백
   ├─ mutex/             cli / popf
   ├─ fs/                posix-fs 재사용
   └─ midi/dos_mpu401    MPU-401 UART
audio/dosopl.cpp         OPL 하드웨어 (RealChip)
gui/debugsocket          COM 포트 전송 추가
configure                *-msdosdjgpp 호스트
```

원칙:

1. 엔진은 고치지 않는다. DOS 사정은 백엔드가 흡수한다. 불가피하면 `#ifdef` 대신 `hasFeature` 로 묻는다.
2. 스레드를 전제하지 않는다. 선점이 필요한 일(음악 타이밍)은 IRQ0 이 하고, 나머지는 메인 루프가
   `pollEvent()` / `delayMillis()` 에서 SDL3 에 양보한다.
3. 기존 코드를 재사용한다. 새로 쓰는 것은 그래픽 매니저, 타이머/뮤텍스, OPL, MPU-401, COM 전송이다.
4. 공통 코드 변경(SVFN 2bpp, `missing=`, 드라이버 표, COM 전송)은 DOS 백엔드와 다른 커밋으로 둔다.
   `i18n` 으로 되돌려 보낼 수 있어야 한다.

## 3. 그래픽 — `DosGraphicsManager`

### 3.1 요청 → 물리 모드

게임이 `initGraphics(W×H, format)` 을 부르면 SDL3 모드 목록에서 차례로 찾는다.

1. 같은 크기, 같은 포맷.
2. 같은 포맷, 가로가 같고 세로가 더 큰 모드. 400 → 480 이면 **5 줄마다 1 줄을 반복**해 세로를 늘린다.
   원래 화면(320×200 / 640×400)은 CRT 에서 4:3 으로 늘어나 보였으므로 레터박스 대신 늘린다.
3. 없으면 그 포맷은 `getSupportedFormats()` 에 넣지 않는다.

`getSupportedFormats()` 는 모드 목록에서 만든다. 트루컬러는 RGB565 > XRGB1555 > XRGB8888 순
(버스 대역폭), CLUT8 은 항상 넣는다. 사용자 설정 `dos_truecolor=auto|off` (기본 auto) 가 off 면
트루컬러를 빼서 엔진이 CLUT8 hires 로 간다.

### 3.2 합성과 전송

- 게임 화면은 게임 포맷 그대로 시스템 RAM 에 둔다. 커서를 얹어 SDL 창 서피스로 옮기고
  `SDL_UpdateWindowSurfaceRects` 로 VRAM 에 보낸다. direct-FB 힌트를 켠다.
- `copyRectToScreen` 이 바꾼 곳만 dirty rect 로 모은다. 640×400 8bpp 전체는 256KB 로,
  PCI 에서 약 8ms 로 추정한다.
- CLUT8 에서는 팔레트를 서피스 팔레트로 넘기고 SDL 이 VGA DAC 를 쓴다.
  트루컬러 모드의 팔레트 페이드는 SCI 드라이버가 전체를 다시 합성하므로 백엔드는 하는 일이 없다.
- vsync 는 기본 끔, 설정으로 켠다.

### 3.3 커서

direct-FB 모드에는 SDL 커서가 없으므로 소프트웨어 커서를 그린다. 밑그림을 저장해 두고 움직일 때 그 영역만
되돌린다. CLUT8 키 컬러 커서와 트루컬러 커서를 모두 받는다.

### 3.4 GUI 오버레이 (M4)

ScummVM GUI 는 16bpp 이상이 필요하다. 오버레이를 켜면 640×480 RGB565 로 모드를 바꾸고, 끄면 되돌린다.
RGB565 가 없는 카드에서는 고정 256색 팔레트로 양자화해 보인다.

## 4. 폰트와 알파

### 4.1 구성: 입력 × 글리프 소스 × 출력

세 축은 서로 독립이다.

```
[입력]                        [글리프 소스 체인 — 맵이 순서를 정한다]          [출력]
UTF-8 번역 ─┐                 ① SVFN v2 (자체 코드 포인트 표)  1/2/8bpp
            ├→ 유니코드  ───→ ② SVFN v1 (cp949/cp932 순서) ─ 매핑      → CLUT8 (커버리지 임계값)
EUC-KR 패치 ┘  (cp949 디코드) ③ 패치 원본 폰트 (korean.fnt, 폰트 뱅크) ─ 매핑 → 트루컬러 + 알파
                              ④ SCVMUNI                     1/2/8bpp
                              ⑤ TTF (FreeType 을 넣은 빌드에서만)
                              ⑥ □ (missing=)
```

- 글리프 소스와 출력은 무관하다. 1bpp 글리프는 트루컬러에서 커버리지 0/255 로, 8bpp 글리프는 CLUT8 에서
  임계값으로 그린다 (`composeSpan` 의 현재 동작).
- 코드 페이지 순서 폰트(SVFN v1, 패치 폰트, SJIS 폰트)는 로드할 때 코드 페이지를 디코드해 코드 포인트 맵을
  만든다 (기존 기능). 그래서 UTF-8 입력도 cp949 폰트로 그린다.
- EUC-KR 한글 패치는 `text_encoding=cp949` 로 디코드한 뒤 같은 체인을 탄다. 패치 원본 폰트(③)도 그대로
  쓸 수 있어서, 맵이 SVFN 을 지정하지 않으면 옛 한글화 게임은 원래 모습으로 나온다.

### 4.2 새로 만드는 것

| 대상 | 내용 |
|---|---|
| SVFN 2bpp | `bpp=2` 를 v1/v2 모두에 허용. 행은 `(cellWidth+3)/4` 바이트, 상위 비트 쌍이 왼쪽 픽셀, 값 0–3 → 커버리지 0/85/170/255. `bitmap_font.cpp`, `glyph_source_svfn.cpp` 에서 8bpp 로 펼친다. 렌더러는 그대로. `FONT_FORMAT.md` 갱신 |
| `mkfont.py` | `--bpp 2` (C20 coverage gamma 에 맞춘 양자화), `--unicode` 묶음 `cp949`, `ksx1001-nohanja` |
| `[hires] missing=` | 예: `missing=u+25a1`. 체인 전체에 없는 코드 포인트는 체인에서 그 글리프(□)를 그린다. 폭은 원래 글자의 East Asian Width 를 따른다 (전각 16px, 반각 8px). 대체한 코드 포인트는 처음 한 번 로그에 남긴다. `glyph_source_fallback` 의 마지막 단계에 둔다 |
| LRU 글리프 캐시 | FreeType 빌드 전용. `(face, size, codepoint)` → 커버리지, 기본 512KB. M2 이후 |

### 4.3 배포 폰트와 프리셋 맵

| 파일 | 내용 | 크기 |
|---|---|---|
| `KO2350.SVF` | SVFN v2, 16px 1bpp. ASCII + KS X 1001 기호·낱자 + 완성형 2350자, 한자 없음 (≈3,500자) | ≈110KB |
| `KO2350A.SVF` | 같은 글자, 18px 8bpp | ≈1.1MB |
| `KOCP949.SVF` | SVFN, 18px 2bpp, cp949 전체 (한글 11172자 + 기호 + 한자, ≈17,000자) | ≈1.4MB |

프리셋 맵은 설정 모음일 뿐이다.

| 프리셋 | 체인 | 출력 |
|---|---|---|
| L (가벼움) `KQ1KOL.MAP`, `LB1KOL.MAP` | `KO2350.SVF` → 패치 원본 폰트 → □ | CLUT8 (`alpha=false`) |
| U (고품질) `KQ1KOU.MAP`, `LB1KOU.MAP` | `KOCP949.SVF` 또는 `KO2350A.SVF` → `KO2350.SVF` → □ | 트루컬러 + 알파 (없으면 CLUT8) |

레이아웃 키(`size`, `baseline`, `align`, `cell`)는 기존 `kq1-ko.map` 과 같게 둔다.
FreeType 을 넣은 빌드에서는 맵이 `.ttf` 를 가리켜도 된다.

### 4.4 메모리 예산 (16MB)

폰트는 파일 전체를 RAM 에 올린다. U 프리셋 최악(KOCP949 1.4MB + KO2350 0.1MB)이 1.5MB 다.
FreeType 빌드는 코드 ≈0.5MB + face 당 0.1–0.2MB + 캐시 0.5MB.

## 5. 사운드와 타이머

### 5.1 OPL — `audio/dosopl.cpp`

`OPL::OPL` + `Audio::RealChip` 구현 (`audio/rwopl3.cpp` 와 같은 모양). 포트 0x388, `BLASTER` 의
`A220` 이 있으면 0x220/0x222 에서 OPL3 을 감지한다. 레지스터 쓰기 뒤에 상태 포트를 6 회(주소), 35 회(데이터)
읽어 3.3µs / 23µs 를 지킨다. `fmopl.cpp` 드라이버 표에 `dosopl` 을 넣고 DOS 빌드의 기본으로 한다.

### 5.2 MPU-401 — `backends/midi/dos_mpu401.cpp`

UART 모드 (0x331 에 `0x3F`), 0x330 에 쓰기 전 DRR 비트를 확인한다. 포트는 `BLASTER` 의 `P330`, 없으면 설정.
MT-32 sysex 지연은 기존 `MidiDriver_MT32GM` 이 맡는다. 인텔리전트 모드는 범위 밖이다.

### 5.3 PCM

ScummVM 믹서를 SDL3 오디오 스트림에 붙인다. SDL3 SB 드라이버의 링 버퍼(≈45ms)로 SCI0 에 충분하다.

### 5.4 IRQ0 타이머 (1kHz)

- DJGPP 로 보호 모드 IRQ0 핸들러를 걸고 PIT 을 1kHz 로 설정한다. 원래 18.2Hz BIOS 틱은 누적해서 체인한다.
  SDL3 가 IRQ0 을 쓰면 그 핸들러도 체인한다.
- 핸들러는 `DefaultTimerManager` 의 콜백을 선점으로 부른다 (원래 SCIV.EXE 방식). 음악 시퀀싱이 여기서 돈다.
- `Common::Mutex` 는 `pushf; cli` / `popf` 로 구현한다. 음악 코드는 데스크톱에서 멀티스레드로 돌기 때문에 이미
  뮤텍스로 보호되어 있고, 이 구현은 IRQ 에 대해 진짜 상호 배제가 된다.
- 조건: `_CRT0_FLAG_LOCK_MEMORY` 로 전체 메모리를 잠근다 (핸들러 중 페이지 폴트 금지). 핸들러에서 DOS/BIOS 를
  부르지 않는다. FPU 상태를 저장·복원한다. 재진입을 막는다 (핸들러가 1ms 를 넘기면 다음 틱은 건너뛰고 센다).
- 이 방식이 실패하면 협력형으로 내려간다: `delayMillis()` / `pollEvent()` 에서 밀린 콜백을 실행한다.

### 5.5 감지

`BLASTER` 를 읽어 음악 `dosopl`, PCM SB 로 자동 설정한다. MT-32 는 `music_driver=mt32` 와 MPU-401.
감지에 실패하면 무음으로 계속하고 로그에 남긴다.

## 6. COM 포트 디버그 채널

`gui/debugsocket` 은 한 줄 명령 → 한 줄 응답 프로토콜이고, 전송이 `DEBUGSOCKET_POSIX` /
`DEBUGSOCKET_WIN32` 로 나뉘어 있다. 세 번째 전송 `DEBUGSOCKET_DOSCOM` 을 더한다.

- 설정: `debug_socket=com1` 또는 `debug_socket=com1:115200` (COM1–COM4, 표준 I/O 주소).
- 16550 UART 를 메인 루프에서 폴링한다 (IRQ 없음). 8N1, FIFO 사용.
- 명령 집합(걷기, 저장/로드, 일시정지/틱 진행, 객체·플레인 조회, 입력 기록)은 그대로다. 엔진 확장
  (`DebugSocketExtension`) 도 그대로 동작한다.
- 호스트 쪽: DOSBox-X / Staging 의 `serial1=nullmodem server:127.0.0.1 port:<n>` 로 TCP 에 연결한다.
  실기는 널 모뎀 케이블(또는 USB 시리얼)로 같은 하네스를 쓴다.
- 115200bps(≈11KB/s) 는 명령에 충분하다. 화면은 파일로 남긴다 (7.3).

## 7. 빌드, 배포, 테스트

### 7.1 configure

`*-msdosdjgpp` 호스트: `_backend=dos`, 정적 링크, 플러그인 없음. `--disable-all-engines --enable-engine=sci`,
`sci32` 제외. 네트워크, curl, OpenGL, fluidsynth, mt32emu, 동영상·압축 오디오 코덱을 끈다. zlib 은 DJGPP 로
빌드해 켠다. FreeType 은 선택 (`--enable-freetype2`). 환경은 `~/opt/dos-dev/env.sh`.

툴체인 (사용자 권한, `~/opt`): DJGPP GCC 12.2 (`ar`/`ranlib` 은 `hostlib/libfl.so.2` 를 찾는 래퍼),
SDL3 정적 `~/opt/sdl3-dos`, CWSDPMI, DOSBox Staging 0.83. DOSBox-X 2026.08.31 은 `/usr/bin/dosbox-x`.

### 7.2 8.3 파일 이름과 배포

순수 DOS 에는 긴 파일 이름이 없다. 배포 파일과 맵 안의 경로는 모두 8.3 이다. LFN 드라이버가 있으면 DJGPP 가
긴 이름을 쓰지만 그에 기대지 않는다.

```
SCUMMVM\  SCUMMVM.EXE  CWSDPMI.EXE  SCUMMVM.INI(예시)  README.TXT
          DATA\MAPS\*.MAP   DATA\FONTS\*.SVF   라이선스(OFL 등)
```

EXE 는 10MB 이하가 목표 (추정). 필요하면 UPX.

### 7.3 테스트

1. **리눅스 단위 테스트** — 공통 코드 변경은 `make test`, FreeType 있는/없는 두 설정 모두.
   글리프 소스 × 출력 조합 표 ({v2 1/2/8bpp, v1 cp949, 패치 폰트} × {CLUT8, RGB565}) 를 기준 이미지와 비교한다.
   같은 글자의 8bpp 와 2bpp 는 커버리지 차가 85 이하여야 한다.
2. **DOS 헤드리스 하네스** `harness/dos/run.sh` — xvfb 에서 DOSBox-X 와 Staging 을 둘 다 돌린다.
   `SCUMMVM.EXE --logfile=...`, COM 디버그 채널로 조작, DOS 빌드 전용 `--dump-frames=<목록>` 이 게임 화면을
   BMP 로 남긴다. 리눅스 하네스의 기준 이미지와 비교한다.
3. **성능** — 로그에 프레임 시간, 화면 전송 시간, 타이머 지터(1kHz 대비)를 남긴다. DOSBox-X `cputype=pentium`
   은 참고용이고 판정은 실기(M4).

## 8. 마일스톤

| | 내용 | 통과 기준 |
|---|---|---|
| M0 | 스파이크 + 최소 포트 | 아래 스파이크 3건이 결론 남. KQ1 이 320×200 CLUT8 로 타이틀까지, 덤프가 기준과 일치. COM 채널로 명령 1개 왕복 |
| M1 | hires 텍스트, L 프리셋 | KQ1·LB1 한국어 대사가 640×400 CLUT8 에 나옴. EUC-KR 패치와 패치 원본 폰트도 확인. □ 대체 로그 |
| M2 | 알파, U 프리셋 | RGB565/XRGB8888 에서 기준 이미지와 일치. 16bpp 없는 모드 목록(Staging)에서 640×480 줄 반복 폴백. SVFN 2bpp |
| M3 | 사운드 | KQ1 타이틀 곡의 OPL 노트 온셋을 리눅스(MAME OPL) 와 비교해 편차 중앙값 ≤ 2ms, 최대 ≤ 10ms, 방 로딩 구간 최대 ≤ 20ms. MPU-401 UART 로 같은 곡이 나옴. SB PCM 효과음 1개 재생 |
| M4 | 마무리 | GUI 오버레이, 세이브/로드, 실기 측정, 배포 패키지 |

### M0 스파이크 (구현 전에 확인)

1. IRQ0 1kHz 보호 모드 핸들러가 SDL3 DOS 와 공존하는가, `cli` 뮤텍스가 동작하는가.
2. direct-FB 모드의 `SDL_UpdateWindowSurfaceRects` 가 부분 영역만 보내는가 (아니면 VRAM 쓰기를 직접 한다).
3. SCI 전용 ScummVM 코어가 DJGPP GCC 12 로 컴파일·링크되는가, 크기는.

## 9. 위험

| 위험 | 대응 |
|---|---|
| IRQ0 핸들러가 불안정 (SDL3 IRQ0 사용, 메모리 잠금 실패) | 협력형 타이머로 폴백 (5.4) |
| 실기 카드마다 VESA 모드 목록이 다름 | 3.1 의 선택 규칙, 두 에뮬레이터에서 테스트 |
| Pentium 에서 FreeType 래스터화가 느림 | 기본 배포는 SVFN, FreeType 은 선택 빌드 |
| 8.3 이름 제약 | 배포 경로 전부 8.3 |
| 16MB 초과 | 폰트는 2bpp, 엔진은 SCI 만 |
