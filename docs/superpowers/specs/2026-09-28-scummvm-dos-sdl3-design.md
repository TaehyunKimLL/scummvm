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
  `Graphics::composeSpan()` 으로 어떤 `PixelFormat` 에든 합성한다. 다만 지금의 SCI 는
  `rgb_rendering`/`palette_mods` 설정이 있을 때만 트루컬러로 `initGraphics` 하고, 그때도
  `getSupportedFormats()` 의 첫 **4바이트** 포맷만 고른다 (4.5절). 이는 의도된 것이다 — 8비트
  커버리지를 5/6/5 채널에 섞으면 정밀도를 잃는다 (`drivers/default.cpp` 주석). SCI 는 고치지 않고,
  DOS 에서 SCI 의 트루컬러는 XRGB8888 이다 (두 에뮬레이터 모두 640×400 XRGB8888 이 있다).
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
   ├─ timer/             IRQ0 1kHz → DefaultTimerManager 콜백 (M3)
   ├─ mutex/             cli / popf (M3)
   ├─ fs/                posix-fs 재사용
   └─ midi/dos_mpu401    MPU-401 UART
audio/dosopl.cpp         OPL 하드웨어 (RealChip)
gui/debugsocket          COM 포트 전송 추가
configure                *-msdosdjgpp 호스트
```

`timer/` 와 `mutex/` 는 M3 에 왔다 (M0–M2 는 타이머 콜백을 `pollEvent()` 에서 협력형으로 돌렸고
(`DefaultTimerManager::checkTimers()`), 뮤텍스는 null 뮤텍스였다). **M3 결과**: PIT 분주값 1193 의
IRQ0 가 고정소수점 누산기로 BIOS 를 체이닝하며(`pit-chain.h`) `DefaultTimerManager::handler` 를
4틱마다 선점 호출하고, `Common::Mutex` 는 cli/popf 로 진짜 상호 배제가 된다 — 자세한 내용과 실측치는
5.4절.

원칙:

1. 엔진은 고치지 않는다. DOS 사정은 백엔드가 흡수한다. 불가피하면 `#ifdef` 대신 `hasFeature` 로 묻는다.
   예외는 이 설계 자체의 폰트·알파 목표(SVFN 체인, non-CLUT8 포맷 선택 등, 4장)에 필요한 **범용 i18n
   기능** 뿐이다 — DOS 전용 분기(`#ifdef` 등)는 이 예외에 해당하지 않는다. 이런 변경은 항상 DOS 백엔드와
   별도 커밋으로 낸다 (원칙 4).
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
(버스 대역폭), CLUT8 은 항상 넣는다. 이 선호 순서는 첫 포맷을 쓰는 엔진에만 해당한다 — SCI 는
4바이트 포맷만 받으므로 XRGB8888 을 쓴다 (4.5절). 사용자 설정 `dos_truecolor=auto|off` (기본 auto) 가 off 면
트루컬러를 빼서 엔진이 CLUT8 hires 로 간다.

**M2 결과**: 줄 반복 공식은 `physRow(y) = y + y/5`, `repeats(y) = (y%5==4)`, 반대 방향(물리→논리)은
`logicalRow(py) = py - (py+1)/6` — 계획 문서의 `py - py/6` 은 반복된 물리 행을 다음 논리 행으로
잘못 매핑하는 결함이었다(0..479 전수 검증, `backends/platform/dos/line-repeat.h`).
`getSupportedFormats()` 는 마지막 `initSize()` 가 640×400 보다 크지 않은 한 640×400 기준으로
답한다(`DOS::formatsSize()`) — 그렇지 않으면 base 의 런처가 먼저 부르는 320×200
`setupGraphics()` 때문에 SCI 가 640×400 용 포맷을 못 받는다(M2 리뷰에서 발견). 시험용 설정
`dos_force_fallback=true` 는 640×400→640×480 뿐 아니라 320×200→320×240 도 줄 반복으로
바꾼다(`chooseMode()` 가 두 크기 모두에 같은 규칙을 적용하므로). 기본값은 `dos_truecolor=auto`,
`dos_vsync=off`, `dos_force_fallback=false`.

### 3.2 합성과 전송

- 게임 화면은 게임 포맷 그대로 시스템 RAM 에 둔다. 커서를 얹어 SDL 창 서피스로 옮기고
  `SDL_UpdateWindowSurfaceRects` 로 VRAM 에 보낸다. direct-FB 힌트를 켠다.
- `copyRectToScreen` 이 바꾼 곳만 dirty rect 로 모은다. 640×400 8bpp 전체는 256KB 로,
  PCI 에서 약 8ms 로 추정한다.
- CLUT8 에서는 팔레트를 서피스 팔레트로 넘기고 SDL 이 VGA DAC 를 쓴다.
  트루컬러 모드의 팔레트 페이드는 SCI 드라이버가 전체를 다시 합성하므로 백엔드는 하는 일이 없다.
- SDL3 direct-FB 는 VESA 뱅크 창이 있으면 뱅크 `dosmemput` 을 고르고, 640×400 처럼 창 하나를 넘는 모드에서는
  넘겨받은 사각형만 보낸다 (`SDL_dosframebuffer.c` 의 multibank 경로, 코드로 확인). 이 경로는 페이지 플리핑이 없다.
- 설정 `dos_vsync=off|wait|flip`:
  - `off` (기본): 바로 보낸다. 찢어짐을 받아들인다. SCI0 은 화면 변화가 작다.
  - `wait` (M2, 구현 완료): 포트 0x3DA 로 수직 귀선을 기다린 뒤 dirty rect 를 보낸다. 백엔드만으로 된다.
    대기 루프는 `inportb()` 10만 회를 상한으로 끊는다(`uclock()` 은 쓰지 않는다 — DJGPP 의 `uclock()`
    은 첫 호출 때 PIT 채널 0 을 재설정해 5.4절 타이머와 부딪힌다). DOSBox-X 에서 걸림(hang) 없이 확인.
  - `flip` (M4 실기 측정 뒤, 필요할 때): VBE `4F07h` 페이지 플리핑. SDL 에 "뱅크 대신 LFB 를 쓴다" 힌트를
    더하는 작은 패치가 필요하다 (SDL 본가에 보낸다). 뒤 페이지에는 두 프레임 전 화면이 있으므로 백엔드가
    최근 두 프레임의 dirty rect 를 합쳐 넘긴다. VRAM 은 640×400 16bpp 기준 1MB 가 든다.
- 하드웨어 BitBlt·확대 블릿·오버레이는 VESA 표준에 없다 (VBE/AF, 카드별 2D 엔진, Streams Processor 만).
  2배 확대와 텍스트 합성은 소프트웨어로 하고, 버스 부담은 dirty rect 와 `TextLayer::rowHasText` 로 줄인다.
- M0 스파이크 실측(`harness/dos/spikes/RESULTS.md`, "전송"): 부분 전송이 전체 전송보다 뚜렷이 싼 조합은
  640×400 RGB565 (DOSBox-X, ratio 4.1 — 547us vs 2246us) 하나뿐이다. 같은 해상도의 INDEX8, 640×480
  RGB565, XRGB8888 은 모두 ratio ≈1.0 — 부분 전송의 이득이 없다. 에뮬레이터 타이밍은 실기 타이밍이
  아니므로, dirty-rect 정책 자체는 유지하되 "모든 모드에서 이득" 이라고 가정하지 않는다. 실기 검증은 M4.

### 3.3 커서

direct-FB 모드에는 SDL 커서가 없으므로 소프트웨어 커서를 그린다. 밑그림을 저장해 두고 움직일 때 그 영역만
되돌린다. CLUT8 키 컬러 커서와 트루컬러 커서를 모두 받는다.

**M2 결과**: 커서 이미지(포맷·크기·핫스팟·키)는 원본 그대로 갖고 있다가, 화면 포맷이 바뀔 때마다(트랜잭션이
끝날 때마다) 화면 포맷으로 변환해 그린다. CLUT8 커서를 트루컬러 화면에 그릴 때는 화소마다
`RGBToColor()` 로 변환한다 — `kFeatureCursorPalette` 가 켜져 있으면 커서 팔레트를, 꺼져 있으면
게임 팔레트를 쓴다. 키 컬러는 변환된 화소 중 어느 것도 쓰지 않는 가장 작은 값이다(최대 256가지뿐이라
항상 찾아진다). 화면이 CLUT8 이면 하드웨어 팔레트가 하나뿐이라 커서 팔레트는 적용되지 않는다. 그 밖의
포맷 불일치(변환되지 않는 트루컬러 간, 또는 알 수 없는 포맷)는 그림을 비우고(`clearImage()`) 아무것도
그리지 않는다 — 이전에는 화면 bpp 와 다른 크기의 커서 이미지가 남아 있으면 프레임버퍼를 넘어 쓸 수
있었다(M2 리뷰에서 발견, 고침).

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
| SVFN 2bpp | `bpp=2` 를 v1/v2 모두에 허용. 행은 `(cellWidth+3)/4` 바이트, 상위 비트 쌍이 왼쪽 픽셀, 값 0–3 → 커버리지 0/85/170/255. **M2 구현 완료**: 로더(`bitmap_font.cpp`), 렌더러(`glyph_renderer.cpp` — `expandCoverage()` 로 1/2/8bpp 공통 경로를 타게 했다; 당초 예상과 달리 렌더러도 손봤다), SCUMM(`hires_text.cpp`, 잉크 스캔과 "안티에일리어싱 여부" 판정 6곳을 `bpp>1` 기준으로), 패딩 마스크(`glyph_source_svfn.cpp`, `usedBits=(cellWidth*bpp)&7`). 8bpp 대비 화소당 커버리지 차 최대 42(≤43). `FONT_FORMAT.md` 갱신 |
| `mkfont.py` | `--bpp 2` (선형 양자화 `(v*3+127)//255`, 읽는 쪽은 단계 x 85), `--unicode` 묶음 `cp949`, `ksx1001-nohanja` |
| `[hires] missing=` | 예: `missing=u+25a1`. 체인 전체에 없는 코드 포인트는 체인에서 그 글리프(□)를 그린다. 칸 폭은 원래 글자의 East Asian Width 를 따른다 (전각 16px, 반각 8px); □ 글리프는 폰트가 그 글리프에 주는 칸 수와 같은 칸에만 쓰고, 다른 폭의 칸에는 1px 테두리 상자를 그린다. (SVFN 폰트에 메트릭 표가 있으면 글리프의 칸 수는 advance 로 정한다 — advance 가 셀 폭의 절반보다 크면 2칸. East Asian Ambiguous 기호 ○ ● □ ■ △ ― 등이 전각으로 그려지므로.) 대체한 코드 포인트는 처음 한 번 로그에 남긴다. `glyph_source_fallback` 의 마지막 단계에 둔다 |
| LRU 글리프 캐시 | FreeType 빌드 전용. `(face, size, codepoint)` → 커버리지, 기본 512KB. M2 이후 |

### 4.3 배포 폰트와 프리셋 맵

| 파일 | 내용 | 크기 |
|---|---|---|
| `KO2350.SVF` | SVFN v2, 16px 1bpp. ASCII + KS X 1001 기호·낱자 + 완성형 2350자, 한자 없음 (≈3,500자) | ≈110KB |
| `KO2350A.SVF` | 같은 글자, 18px 8bpp | ≈1.1MB |
| `KOCP949.SVF` | SVFN, 18px 2bpp, cp949 전체 (한글 11172자 + 기호 + 한자) | 실측 1,192,926B / 11,695 glyphs (NanumGothic 에 한자가 없어 그 코드포인트는 □) |

프리셋 맵은 설정 모음일 뿐이다.

| 프리셋 | 체인 | 출력 |
|---|---|---|
| L (가벼움) `KQ1KOL.MAP`, `LB1KOL.MAP` | `KO2350.SVF` → 패치 원본 폰트 → □ | CLUT8 (`alpha=false`) |
| U (고품질) `KQ1KOU.MAP`, `LB1KOU.MAP` | `KOCP949.SVF` 또는 `KO2350A.SVF` → `KO2350.SVF` → □ | XRGB8888 + 알파 — ini 의 `rgb_rendering=true` 로 켠다 (맵의 `alpha=` 는 SCI 에서 아무 일도 하지 않는다). 없으면 CLUT8 |

레이아웃 키(`size`, `baseline`, `align`, `cell`)는 기존 `kq1-ko.map` 과 같게 둔다.
FreeType 을 넣은 빌드에서는 맵이 `.ttf` 를 가리켜도 된다.

### 4.4 메모리 예산 (16MB)

폰트는 파일 전체를 RAM 에 올린다. **M2 실측**: U 프리셋 파일 상주분(KOCP949 1,192,926B + KO2350
127,548B) = 1,320,474B ≈1.26MB. 여기에 그려진 글리프마다 쌓이는 캐시(KOCP949 162B/glyph, KO2350
64B/glyph)를 더하면, 이론상 최악(두 폰트 전체 글리프를 다 그림) ≈3.24MB(memsize 16 의 ~20%), KQ1
실측(926자 사용) ≈1.40MB(~8.8%)이다.
FreeType 빌드는 코드 ≈0.5MB + face 당 0.1–0.2MB + 캐시 0.5MB.

### 4.5 M1/M2 를 계획하며 확인한 사실 (2026-09-28)

- **SCI 는 이제 SVFN 을 hires 얼굴(face) 체인과 `.uni` 묶음 양쪽에서 받는다 (M1, `engines/sci/graphics/cache.cpp`).**
  비-UTF-8 단일 얼굴 경로(레거시 cp949 게임의 `face=`)도 SVFN 을 받는다 — `kProbesHangul` 아래에서 얼굴에
  한글 글리프가 없으면 경고 한 줄과 함께 거부한다. 이건 DOS 전용이 아니라 범용 i18n 기능이다 (2장 원칙 1).
  - SVFN 얼굴이 섞인 체인은 한 8bpp 셀로 맞춘다(각 얼굴을 셀 높이·top 오프셋에 맞게 `NormalizedGlyphSource`
    로 감싼다); TTF 전용 체인은 기존 그대로 손대지 않는다. 서로 다른 높이의 비트맵 얼굴은 이제 베이스라인에
    맞춰 정렬한다(**M2, `layoutFaceChain()`** — 얼굴마다 SVFN ascent 또는 TTF baseline 을 앵커로 top
    오프셋을 계산하고, 하나라도 baseline 을 모르면 M1 의 위쪽-정렬 규칙으로 되돌아간다). TTF 전용 체인은
    바이트 단위로 그대로임을 확인했다. `.uni` 묶음은 아직 첫 얼굴의 칸 위쪽(베이스라인 아님)에 놓인다 —
    미해결.
  - 한국어 게임에서 hires 얼굴이 적용 중이면(맵의 `face=` 또는 `hires_text_font`) 그 얼굴이 `korean.fnt`
    보다 먼저 온다 (일본어/SJIS 순서는 그대로 — `SJIS.FNT` 가 여전히 먼저).
  - `missing=` 대체는 `GfxFontSet::faceFor` 끝에서, 모든 얼굴(`korean.fnt` 와 `.uni` 포함)이 디코드된
    non-ASCII 코드 포인트를 거절한 다음에만 일어난다. 게임 자체 리소스 폰트에 그 글자가 있으면 절대
    대체하지 않는다. □ 칸 폭은 East Asian Width 를 따르고 대체는 코드 포인트마다 처음 한 번만 로그에 남긴다;
    맵의 □ 글리프를 어느 폰트도 갖지 않으면 그것도 한 번 경고한다.
  - `KO2350.SVF` 실측: 글리프 2898개, 127,548바이트 (4.3 절의 ≈110KB 추정보다 큼) — neodgm 에 KS X 1001
    비한글 기호 535개가 없어, 그 글자들은 (missing= 이 있으면) □ 로 그려진다.
  - DOS L 프리셋 파일: `DATA\KO2350.SVF`, `KQ1KOL.MAP`, `LB1KOL.MAP`, `OFL.TXT`.
  - **U 프리셋(M2) 실측**: `KOCP949.SVF` 1,192,926B / 11,695 glyphs. `KQ1KOU.MAP` 은
    `face=ko, ko2`(`ko2=KO2350.SVF`)로 KO2350 을 뒤에 두어 KOCP949 에 없는 KS X 1001 장식 기호
    157자(원, 괄호 숫자/한글 낱자, 옛한글 자모 등 — 일반 한글 음절은 전부 KOCP949 에 있다)를 메운다(둘
    다 없으면 □). `LB1KOU.MAP` 은 `singleFace()` 가 첫 얼굴만 쓰므로 `face=ko` 하나뿐이다(`ko2=` 줄은
    참고용으로 남기지만 로드되지 않는다). 두 얼굴 체인의 칸은 18×19 인데 KQ1 의 폰트 0/4/300 은 모두
    16px 줄 칸이라 수식대로는 3행이 넘친다 — `GlyphPlacement` 규칙대로 이웃 줄 위에 그려질 수 있지만,
    제목 메뉴·2줄 대화창·빈 인벤토리 캡처(리눅스 no-FreeType 빌드) 샘플에서는 눈에 띄는 침범이 없었다
    (전수 증명은 아니다).
- **SCI 는 맵의 `alpha=` 를 무시한다.** 트루컬러는 `rgb_rendering`/`palette_mods` ConfMan 설정이 있을
  때만 켜지고, 그나마 `getSupportedFormats()` 에서 처음 나오는 **4바이트** 포맷만 고른다 (`RGB565` 는
  이 경로로 절대 선택되지 않는다). **SCI 는 고치지 않는다 (M2 결정):** 4바이트만 받는 것은 8비트 커버리지
  정밀도를 위한 의도다 (`drivers/default.cpp` 주석). 그래서 DOS 에서 SCI 의 트루컬러는 XRGB8888 이고, 3.1절의
  RGB565 선호는 SCI 에 해당하지 않는다. 트루컬러는 ini 의 `rgb_rendering=true` 로 켜고, 맵의 `alpha=` 는 SCI 에서
  아무 일도 하지 않는다.
- DOS 프리셋 맵과 폰트는 8.3 디렉터리 하나에 모은다 (`dists/engine-data/hires_text/dos` → 배포 시
  `DATA\`) — `hires_text` 자체가 8.3 이름이 아니라서, 맵이 상대 경로로 폰트를 가리키는 별도 디렉터리가
  필요하다.
- 테스트 데이터: KQ1 한국어는 UTF-8 `text.*` + `sci-ko.str` (`ADGF_UTF8I18N`). LB1 한국어는 자체
  `korean.fnt` 를 쓰는 EUC-KR 팬패치다.

## 5. 사운드와 타이머

### 5.1 OPL — `audio/dosopl.cpp`

`OPL::OPL` + `Audio::RealChip` 구현 (`audio/rwopl3.cpp` 와 같은 모양). 포트 0x388, `BLASTER` 의
`A220` 이 있으면 0x220/0x222 에서 OPL3 을 감지한다. 레지스터 쓰기 뒤에 상태 포트를 6 회(주소), 35 회(데이터)
읽어 3.3µs / 23µs 를 지킨다. `fmopl.cpp` 드라이버 표에 `dosopl` 을 넣고 DOS 빌드의 기본으로 한다.

**M3 결과**: 구현·측정 완료 (`audio/dosopl.{h,cpp}`, `audio/fmopl.cpp` 의 `DOS_DJGPP` 부분). 감지
순서는 0x388 → (BLASTER 가 있으면) `A` 값(뱅크 2 는 A+2, OPL3 로 취급) → `A+8`(OPL2 전용, OPL3 로는
절대 취급하지 않는다). 상태 포트 읽기 횟수(주소 6회/데이터 35회)는 설계대로다. `dosopl` 은 `dos.cpp`
의 `ConfMan` 기본값이 아니라 `fmopl.cpp` 드라이버 표 순서(`null` 바로 뒤, `kDosOPL=19`)와 `detect()`
로 DOS 기본이 된다 (T2/T3 와 `dos.cpp` 충돌을 피하려고). **규칙(확정)**: DOS 에서 자동 감지는 OPL
칩이 응답하는 한 소프트웨어 에뮬레이터를 절대 고르지 않는다 — OPL2 전용 카드에 `DualOpl2`/`OPL3`
요청이 오면 칩에서 모노로 폴백하고(엔진의 자체 폴백, 예: SCI 의 adlib.cpp:242-248), 칩이 전혀 없을
때만 에뮬레이터(`db` 등)를 쓴다. 레지스터 쓰기 로그는 DOS 전용이 아니라 범용 `opl_log=<path>`(모든
플랫폼)로 `LoggingOPL` 이 감싸 남긴다(`<path>.REG` 는 전체 `%03X %02X`, `<path>.ON` 은 키온마다
`<ms> %03X %02X`) — 링 버퍼(65536 항목, 8바이트, 락 없이 생성자에서 할당)에 쓰고 메인 스레드가
`notifyPoll()` 에서 최대 1초마다 비운다; 완전한 로그는 정상 종료(quit) 때만 보장된다(DOSBox 가 4KB
단위로 호스트 파일을 버퍼링). DOSBox-X/Staging 모두 `DOSOPL: OPL3 at 0x388, type 1` 로 잡았고, 클릭
이전 구간의 레지스터 스트림이 리눅스 `db` 에뮬레이터 로그와 바이트 단위로 일치했다(§8). **실제 타이머
주기는 4ms 가 아니라 10ms 다** — SCI 의 AdLib 드라이버는 250Hz(4ms) 콜백을 요청하지만
`RealChip::startCallbacks` 가 `kMaxFreq`=100Hz 로 누르고, `onTimer` 가 한 틱(10ms)에 콜백을 2~3 회
몰아 돌린다; 5.4절의 "1~4ms 늦음"이 여기에도 그대로 나타나 키온이 10ms 격자에서 ±2ms 정도 흔들린다.

### 5.2 MPU-401 — `backends/midi/dos_mpu401.cpp`

UART 모드 (0x331 에 `0x3F`), 0x330 에 쓰기 전 DRR 비트를 확인한다. 포트는 `BLASTER` 의 `P330`, 없으면 설정.
MT-32 sysex 지연은 기존 `MidiDriver_MT32GM` 이 맡는다. 인텔리전트 모드는 범위 밖이다.

**M3 결과**: 구현 완료, `MidiDriver_DosMPU : MidiDriver_MPU401`. **설계에서 벗어난 점**: 리셋/UART
전환 명령에 ACK 이 없어도 받아들인다 — DOSBox-X 의 `mpu401=uart` 는 두 명령 모두 ACK 하지 않는다
(UART 전용 클론 카드에 흔하다); 포트가 0xFF 를 읽지 않고 DRR 이 풀리면 "살아있다"로 보고 UART 로
취급한다(어느 ACK 이 왔는지 로그에 남긴다). `checkDevice`(자동 감지에서는 제외)가 `open()` 전에 포트
존재를 먼저 확인한다 — 그래야 SCI 의 `open()` 실패가 `error()`(엔진, 수정 불가)로 죽는 대신
ScummVM 의 표준 폴백 다이얼로그로 넘어간다; M4 오버레이 전에는 이 다이얼로그가 보이지 않아 키 입력이
있을 때까지 멈춘다(자동화는 `AUTOTYPE`). 전송은 `Common::Mutex`(= cli) 로 메시지를 통째로 보내
IRQ0 타이머와 메인 스레드가 한 메시지를 반씩 나눠 쓰지 못하게 한다. 로그는 DOS 전용
`dos_midi_log=<path>` 링(128KB, `[ms:4][len:2][bytes]`)으로 남기고, 리눅스 쪽은 기존
`--dump-midi`/`dump_midi=true` 를 기준으로 삼았다 — DOS 의 첫 620 메시지가 리눅스와 바이트 단위로
일치했다(46 SysEx, 191 Note On/Off 쌍). 이 경로의 실제 타이머 주기도 100Hz(10ms)다
(`MidiDriver_MPU401` 자체의 설계 주기이며, OPL 과 같은 늦음 패턴이 그대로 나타난다).

### 5.3 PCM

ScummVM 믹서를 SDL3 오디오 스트림에 붙인다. SDL3 SB 드라이버의 링 버퍼(≈45ms)로 SCI0 에 충분하다.

**M3 결과**: `DosMixerManager`(`backends/mixer/dos`) 구현 완료. 믹서는 **SDL3 가 카드를 연 실제
레이트**로 돈다 — SB16 은 44100Hz, 그보다 오래된 카드(SB/SB2/SB Pro)는 SDL 의 DOS SB 드라이버가
22050Hz 로 낮춘다. 설계상 고정 22050Hz 가 아니다: SDL3 공개 API 로는 SB 를 22050Hz 로 직접 열 수
없고, 레이트를 강제로 22050 에 고정하면 SDL 이 부동소수점 리샘플링을 태워 CPU 점유가 3%→20%
(DOSBox-X, 6만 사이클 기준의 `yield=` 측정)로 뛴다. `output_rate` 로 덮어쓸 수 있다. 디바이스
버퍼는 2048 프레임(`SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES`, 44.1kHz 에서 ≈186ms 여유 + 46ms 지연).
믹스 콜백은 SDL 의 협력 오디오 스레드에서 256프레임(1KB) 조각으로 `Common::Mutex`(cli) 를 잡고
돈다 — 조각당 인터럽트 차단 시간은 139µs(44100Hz) / 188µs(22050Hz)로 1ms IRQ0 주기보다 훨씬 짧아
틱을 잃지 않는다. `SDL_PutAudioStreamData` 는 뮤텍스 밖(IF=1)에서만 부른다 — 뮤텍스 아래 또는 타이머
프로시저 안에서 SDL 을 부르지 않는다는 규칙(5.4절)을 여기서도 지킨다. BLASTER/DSP 가 없으면
`NullMixerManager` 로 폴백한다(리뷰 결과 `ready=false` 인 믹서는 `playStream` 이 assert 로 죽으므로
채택). KQ1 타이틀 25초, `music_driver=adlib` 로 OPL 하드웨어 경로와 SB16 44100Hz PCM 이 동시에
열림을 확인했다.

**알려진 한계**: 이 SDL3 DOS 트리의 SB 드라이버는 SB Pro 스테레오를 절반 속도로만 재생하고
(DOSBox-X 에서 22050Hz 요청에 11346Hz 로 재생 — 시간 상수는 44100 바이트/초로 맞추지만 일반속도
DMA(0x1C) 를 쓴다; 실기 SB Pro 스테레오 22050Hz 는 고속 DMA(0x90) 가 필요하다), SB 1.x(DSP 1.x) 에는
데이터를 전혀 넣지 않는다(자동 초기화 DMA 미지원, SDL 코드의 주석대로). 둘 다 ScummVM 코드가 아니라
`SDL_dosaudio_sb.c` 의 문제이며 SDL 본가에 보고할 대상이다. 믹스는 인터럽트를 끈 채로 돈다 — 재생
중 채널의 `mix()` 가 디스크 I/O(DOS 파일 읽기)를 하면 그 순간 인터럽트가 켜진다, 즉 디스크
스트리밍 사운드는 이 구현으로 안전하지 않다; SCI0 은 모든 사운드를 메모리에 들고 있어 해당 없지만,
SCI1+ 디지털 오디오를 붙일 때는 다시 볼 문제다.

### 5.4 타이머 (자체 IRQ0 핸들러, PIT 분주값 1193 ≈ 1kHz, BIOS INT 8 체이닝)

M0 스파이크 결론 (`harness/dos/spikes/RESULTS.md`, "타이머"): RTC(IRQ8, 1024Hz)가 아니라 **PIT** 을
쓰고, PIT 재설정만으로 BIOS 가 알아서 보정해줄 거라고 가정하지 않고 **우리 자신의 체이닝 IRQ0 핸들러**를
쓴다.

- DJGPP 로 보호 모드 IRQ0 핸들러를 걸고 PIT 분주값을 1193(≈1kHz)으로 설정한다. 고정소수점 누산기(매 틱
  1193 을 더하다 65536 을 넘기면)로 원래 BIOS INT 8 핸들러를 약 55틱마다 체인한다 — 구식 TSR 체이닝
  기법(`pushfl; lcall *old_int08_far_ptr`)을 쓴다. 두 에뮬레이터(DOSBox-X, Staging) 모두에서 자체 틱
  카운터·BIOS 틱(0x46C)·체이닝 횟수가 서로 정확히 들어맞는 것을 확인했다.
- RTC(IRQ8, 1024Hz)는 기각한다: 도는 동안은 정확하지만, DOSBox-X 에서 주기 인터럽트를 켜는 순간 항상
  행(hang)한다 (Staging 에서는 동작한다). 하네스가 요구하는 두 에뮬레이터 중 하나에서 아예 못 쓰는
  접근은 채택할 수 없다. RTC 카운트 자체도 그 전에 PIT 을 어떻게 건드렸는지에 취약하다는 것도 확인했다.
- `SDL_Init()` 은 이미 PIT 채널 0 을 mode 2(rate generator)로 재프로그램해 둔다 — DJGPP `uclock()`
  (`SDL_GetTicks()`/`SDL_GetPerformanceCounter()` 의 기반)이 첫 호출 때 그렇게 한다. 우리 IRQ0 핸들러는
  이미 바뀐 이 상태 위에 분주값 1193 을 다시 설정한다.
- 핸들러는 `DefaultTimerManager` 의 콜백을 선점으로 부른다 (원래 SCIV.EXE 방식). 음악 시퀀싱이 여기서 돈다.
- `Common::Mutex` 는 `pushf; cli` / `popf` 로 구현한다. 음악 코드는 데스크톱에서 멀티스레드로 돌기 때문에 이미
  뮤텍스로 보호되어 있고, 이 구현은 IRQ 에 대해 진짜 상호 배제가 된다.
- 조건: `_CRT0_FLAG_LOCK_MEMORY` 로 전체 메모리를 잠근다 (핸들러 중 페이지 폴트 금지). 핸들러에서 DOS/BIOS 를
  부르지 않는다. FPU 상태를 저장·복원한다. 재진입을 막는다 (핸들러가 1ms 를 넘기면 다음 틱은 건너뛰고 센다).
- 주의: 우리 `main()` 은 `SDL_RunApp()` 을 거치지 않으므로 SDL3 의 `_crt0_startup_flags` 정의는 링크되지
  않는다. 그래서 `backends/platform/dos/dos.cpp` 가 직접
  `_crt0_startup_flags = _CRT0_FLAG_NONMOVE_SBRK | _CRT0_FLAG_LOCK_MEMORY` 를 정의하고 (M0 최종 수정,
  DOSBox-X·Staging 모두 memsize 16 에서 부팅 확인), `SDL_RunApp()` 처럼 `main()` 첫 줄에서
  `_CRT0_FLAG_LOCK_MEMORY` 를 다시 내린다 — 시작할 때 `.data`/`.bss`/`.text`/스택만 잠긴다.
  (`NONMOVE_SBRK` 는 이 DJGPP 에서 이미 기본값(0)이지만, 힙이 자라도 DS 기준이 움직이지 않아야 SDL3 가
  들고 있는 프레임버퍼 near 포인터가 유효하므로 명시한다.) 그 뒤 `malloc()` 한 메모리는 자동으로 잠기지
  않으므로, 핸들러가 만지는 힙 버퍼(예: 링 버퍼)는
  `_go32_dpmi_lock_data()` 로 따로 잠그거나, 애초에 정적/사전할당 버퍼로 설계한다.
- 이 방식이 실패하면 협력형으로 내려간다: `delayMillis()` / `pollEvent()` 에서 밀린 콜백을 실행한다.

**M3 결과**: 구현 완료 (`backends/timer/dos/dos-timer.{h,cpp}`, `backends/mutex/dos/dos-mutex.{h,cpp}`,
`backends/platform/dos/{pit-chain.h,dos-heap.{h,cpp}}`). 위 설계안에서 다음이 실측으로 바뀌거나
채워졌다.

- **IRQ0 핸들러**: `pit-chain.h` 의 고정소수점 누산기(`DOS::pitTick`, 분주값 1193)로 BIOS 를
  체이닝하는 설계는 그대로 확정. `DefaultTimerManager::handler()` 는 매 틱이 아니라 **4틱마다**
  (`kHandlerEvery`, ≈4ms) 불린다 — 그래서 콜백은 설계상 1~4ms 늦게 온다(5.1/5.2절 OPL/MPU 위상이
  이 늦음을 그대로 보인다). 재진입은 `inHandler` 플래그로 막는다(중첩 틱은 EOI 없이 그냥 반환해
  IRQ0 를 in-service 상태로 남긴다 — 그래야 다음 하드웨어 인터럽트까지 안전하게 기다린다, 아래
  "인터럽트 꺼진 상태의 로깅" 참고). FPU 는 108바이트 버퍼에 `fnsave`/`frstor` 로 핸들러 진입·복귀마다
  저장·복원한다(x87 전용, SSE 없음).
- **getMillis**: ISR 의 틱 카운터에서 뽑는다(설치 시 `__uclock()` 값에서 시드해 연속성을 유지하고,
  설치 실패 시에만 SDL 로 폴백). `uclock()` 자체를 DJGPP 것과 다르게 재정의했다 — 분주값 1193 에서는
  DJGPP 의 `uclock()` 이 BIOS 틱(0x46C, ≈55ms 마다 1) 단위로만 올라가므로, `SDL_Delay`/
  `SDL_GetTicks`/SDL SB 오디오의 타임아웃이 최대 55ms 까지 밀릴 수 있었다. 재정의된 `uclock()` 은
  설치 전엔 원래 `__uclock`, 설치 후엔 (틱수×1193 + 틱 안에서의 PIT 카운트)를 단조 증가로 돌려준다.
  M3 하네스는 이 값을 새 디버그 소켓 명령 `millis`(getMillis() 값)로 읽고, `rtc`(그 값과 DOS CMOS
  초시계를 짝지어 반환 — DOS_DJGPP 의 메인 스레드에서만 응답하고, 그 밖에는 실패를 답한다)로 긴 음악
  실행 구간에서 IRQ0 클럭이 실제 시계에서 표류하지 않는지 맞대어 본다(§8).
- **delayMillis**: 인터럽트가 꺼진 상태(타이머 프로시저 안, 또는 뮤텍스 아래)에서는 틱이 못 올라가므로
  PIT 을 직접 폴링해 경과를 센다(`spinMillis`) — 안 그러면 무한 대기한다. SCI 의 MT-32 경로가 타이머
  스레드에서 `delayMillis` 를 부르므로(`midi.cpp:671`) 실사용된다.
- **테어다운**: 모든 종료 경로에서 일어난다 — `~OSystem_DOS`(믹서보다 먼저 타이머 매니저를 지운다),
  `quit()`/`fatalError()` 가 `SDL_Quit()` 전에 부르는 `DosTimerManager::shutdown()`, 그리고 `atexit`
  핸들러(모두 idempotent). PIT → 벡터 → 래퍼 순으로 복원한다.
- **메모리 잠금 (설계 변경, "`_CRT0_FLAG_LOCK_MEMORY` 로 전체 잠금" 을 대체)**: 전체 잠금은 16MB 에서
  깨졌다 — KQ1 M1 덤프 중 ISR 안에서 페이지 폴트가 났다(`mv2freelist`/
  `MidiParser_SCI::parseNextEvent`, cr2≈18.7MB); hires 폰트 캐시만 수 MB(M2 실측 최대 ≈12MB, 두 번
  로드)라 전체를 잠긴 채로 못 둔다. 대신 **분할 힙**(`dos-heap.cpp`): 256KB 이상인 블록은 페이지
  가능한 DPMI(0x501) 블록으로 따로 받고, 그 미만은 기존 sbrk 힙(잠김)에 둔다. `malloc`/`free`/
  `realloc`/`calloc`/`memalign` 은 `-Wl,--wrap` 으로 감싸 그 자체(장부 정리)만 인터럽트를 끈 채로
  (IF=0) 돈다 — DJGPP `malloc` 이 재진입 불가이고 SCI 음악 타이머 프로시저가 `Array::clear` 로
  해제를 하기 때문. 256KB 를 넘겨 커지는 `realloc` 은 잠긴 힙에 남는다. 전제: 타이머 프로시저(SCI
  음악, OPL, MPU)는 작은 할당만 건드린다 — 256KB 이상 블록을 만지는 타이머 프로시저가 생기면(예:
  미래의 OPL 에뮬레이터나 ISR 안에서 도는 믹서) 이 전제가 깨진다.
- **인터럽트 꺼진 상태의 로깅은 유예한다**: 핸들러(타이머 프로시저) 안에서 `debug()`/`warning()` 이
  그대로 파일 I/O(INT 21h) 를 하면 `sti` 가 실행돼, EOI 없이 중첩된 IRQ0 가 통째로 사라져 걸린다
  (hang). 그래서 IF=0 일 때 `logMessage` 는 텍스트를 정적 16KB 링(이미지에 포함, malloc 없음)에
  복사만 하고, 실제 파일 쓰기는 `pollEvent`, IF=1 인 다음 `logMessage` 호출, 또는 `atexit` 핸들러가
  인터럽트를 켠 채로 나중에 한다. 다 못 담은 메시지는 버려지고 "N characters ... lost" 로만 남는다.
- **규칙(확정)**: `Common::Mutex` 아래 또는 타이머 프로시저 안에서 SDL 함수를 부르지 않는다 — DOS 용
  SDL 뮤텍스가 무조건 `sti` 를 하므로, cli 뮤텍스/ISR 안에서 SDL 을 부르면 인터럽트가 다시 켜진다
  (5.3절 믹서가 지키는 규칙).
- **시각(time of day)**: DJGPP 의 `localtime()`/`mktime()`/`time()` 은 이 빌드에서 tz 상태를
  malloc 된 메모리에 보관하는데, zoneinfo 가 없으면 그 상태의 윤초 카운트가 초기화되지 않아 힙이
  조금만 더러워져도 `time()` 이 -1 을, `localtime(0)` 이 엉뚱한 값을 돌려준다 — KQ1 타이틀 메뉴가
  12초 뒤에도 나타나지 않는 실제 결함으로 드러났다(M2 시절 실행 파일에서도 재현되는 잠복 결함이었다).
  고침: `OSystem_DOS::getTimeAndDate` 는 INT 21h `2Ah`/`2Ch` 를 직접 읽는다(자정 넘어감을 잡으려고
  날짜를 시간 앞뒤로 두 번 읽는다). `time()` 자체는 여전히 -1 을 돌려준다 — 이 빌드의 ScummVM 경로는
  전부 `getTimeAndDate` 를 쓰므로 실제로는 안 걸리지만, 나중에 `time()` 을 직접 부르는 코드가 들어오면
  다시 걸릴 잠재적 함정이다. **fix round**: `getTimeAndDate` 는 이제 메인 스레드에서만 INT 21h 를
  부른다 — 인터럽트가 꺼진 채(IF=0, 예: 타이머 프로시저 안) 불리면 DOS 를 부르지 않고 메인 스레드가
  마지막으로 읽은 값을 캐시로 돌려준다(커밋 `957c9d5`).

### 5.5 감지

`BLASTER` 를 읽어 음악 `dosopl`, PCM SB 로 자동 설정한다. MT-32 는 `music_driver=mt32` 와 MPU-401.
감지에 실패하면 무음으로 계속하고 로그에 남긴다.

**M3 결과**: `backends/platform/dos/blaster.h` 의 `DOS::parseBlaster()` 가 이 파싱을 맡는다(핸드롤
16진/10진 파서, DJGPP 비의존). `BLASTER` 가 없으면(`getenv` 가 `nullptr`) 기본값과 `present=false`,
빈 문자열이면 기본값과 `present=true` 로 구분한다. 태그는 인식해도 숫자가 없거나 잘못되면 그 필드
전체가 기본값으로 남는다(예: `A22G` → 기본 0x220).

## 6. COM 포트 디버그 채널

`gui/debugsocket` 은 한 줄 명령 → 한 줄 응답 프로토콜이고, 전송이 `DEBUGSOCKET_POSIX` /
`DEBUGSOCKET_WIN32` 로 나뉘어 있다. 세 번째 전송 `DEBUGSOCKET_DOSCOM` 을 더한다.

- 설정: `debug_socket=com1` 또는 `debug_socket=com1:115200` (COM1–COM4, 표준 I/O 주소).
- 16550 UART 수신은 인터럽트로 받는다 (IRQ4/IRQ3; 원래 M3 계획이었으나 M1 로 앞당겼다 — 메인 루프에서
  16바이트 FIFO 를 폴링하던 방식은 프레임이 길면 FIFO 가 오버런해 명령 바이트를 잃었다). 8N1, FIFO 사용.
  잠금(locked)된 정적 링 버퍼에 IRQ 핸들러가 바이트를 채우고, `read()` 는 인터럽트를 놓쳤을 때를 대비해
  FIFO 도 함께 비운다(백스톱). 해제는 소멸자 경로뿐 아니라 `exit()` 경로에서도 일어난다 — `atexit()` 로
  등록한 정적 teardown 이 PIC 마스크·IER·MCR 을 원래 값으로 복원한다.
- 명령 집합(걷기, 저장/로드, 일시정지/틱 진행, 객체·플레인 조회, 입력 기록)은 그대로다. 엔진 확장
  (`DebugSocketExtension`) 도 그대로 동작한다.
- 호스트 쪽: DOSBox-X / Staging 의 `serial1=nullmodem port:<n>` (`server:` 없이) 로 설정하면 DOSBox 가
  TCP 를 듣고 하네스가 접속한다. 실기는 널 모뎀 케이블(또는 USB 시리얼)로 같은 하네스를 쓴다.
- DOSBox 는 ScummVM 이 실제로 COM1 을 여는 것보다 훨씬 먼저 그 TCP 접속을 받아들인다. 그 사이에 보낸
  바이트는 유실된다. 하네스는 빈 줄을 3초마다 보내면서 ScummVM 이 (파싱 실패에 대해) 빈 응답 `.` 을
  돌려줄 때까지 기다려 COM1 이 열렸음을 확인한다 — 그 확인 전에 보낸 바이트도 유실된다.
- 호스트는 여전히 바이트 사이 2ms 로 보내지만(하네스 코드), 인터럽트 수신이 된 뒤로는 더 이상 필요하지
  않다.
- 115200bps(≈11KB/s) 는 명령에 충분하다. 화면은 파일로 남긴다 (7.3).

## 7. 빌드, 배포, 테스트

### 7.1 configure

`*-msdosdjgpp` 호스트: `_backend=dos`, 정적 링크, 플러그인 없음. `--disable-all-engines --enable-engine=sci`,
`sci32` 제외. 네트워크, curl, OpenGL, fluidsynth, mt32emu, 동영상·압축 오디오 코덱을 끈다. zlib 은 DJGPP 로
빌드해 켠다. FreeType 은 선택 (`--enable-freetype2`). 환경은 `~/opt/dos-dev/env.sh`.

DJGPP 빌드는 인수 없는 `%` 포맷 경고가 대량으로 뜬다(전체 재빌드 1409개, 거의 전부 `-Wformat`) —
**M2**: MorphOS 전례처럼 `msdosdjgpp` 에 `-Wno-format` 을 켠다(`configure`). 남는 경고 1개
(`engines/sci/graphics/text16.cpp` 의 미사용 변수)는 DOS 이식과 무관하다.

툴체인 (사용자 권한, `~/opt`): DJGPP GCC 12.2 (`ar`/`ranlib` 은 `hostlib/libfl.so.2` 를 찾는 래퍼),
SDL3 정적 `~/opt/sdl3-dos`, CWSDPMI, DOSBox Staging 0.83. DOSBox-X 2026.08.31 은 `/usr/bin/dosbox-x`.

### 7.2 8.3 파일 이름과 배포

순수 DOS 에는 긴 파일 이름이 없다. 배포 파일과 맵 안의 경로는 모두 8.3 이다. LFN 드라이버가 있으면 DJGPP 가
긴 이름을 쓰지만 그에 기대지 않는다.

```
SCUMMVM\  SCUMMVM.EXE  CWSDPMI.EXE  SCUMMVM.INI(예시)  README.TXT
          DATA\*.MAP  DATA\*.SVF   라이선스(OFL 등)
```

`DATA\` 는 평평한 디렉터리 하나다 (4.5절, M1 계획). 맵 안의 폰트 경로는 맵 기준 상대 경로(같은 디렉터리의
8.3 이름)다.

EXE 는 10MB 이하가 목표 (추정). 필요하면 UPX.

### 7.3 테스트

1. **리눅스 단위 테스트** — 공통 코드 변경은 `make test`, FreeType 있는/없는 두 설정 모두.
   글리프 소스 × 출력 조합 표 ({v2 1/2/8bpp, v1 cp949, 패치 폰트} × {CLUT8, RGB565, XRGB8888 (SCI 가 쓰는 것)}) 를 기준 이미지와 비교한다.
   같은 글자의 8bpp 와 2bpp 는 커버리지 차가 85 이하여야 한다.
2. **DOS 헤드리스 하네스** `harness/dos/run.sh` — xvfb 에서 DOSBox-X 와 Staging 을 둘 다 돌린다.
   `OSystem_DOS` 는 로그를 현재 디렉터리의 `SCUMMVM.LOG` 에 남긴다 (`--logfile` 은 쓰지 않는다;
   `debuglevel=1` 이면 백엔드가 설정한 모드가 `DOS: mode WxH <format>` 줄로 남는다). COM 디버그 채널로 조작. 화면 덤프는 DOS 전용 옵션을 새로 만들지 않고
   debug socket 의 기존 `dump <prefix>` 명령을 그대로 쓴다 (SCI 는 `_low`/`_scaled`/`_pal`/`_ctl`/`_pri`/
   `_out` 버퍼를 `<prefix>_low.bin` 등으로 남긴다). DOS 에서는 8.3 제약 때문에 접두어를 글자 하나 +
   디렉터리로 둔다 (예: `T:\T`). DOS 는 파일 이름을 대문자로 쓰므로(`T_LOW.BIN`), 리눅스 기준 이미지와
   비교할 때 이름을 맞춰야 한다. 리눅스 하네스의 기준 이미지와 비교한다.
   **`shot`(M2, 새 명령)**: `dump` 와 별개로 백엔드가 실제로 그린 창 서피스를 저장한다 —
   `SHOTnnnn.RAW`(원본 행, w×bpp 피치), `SHOTnnnn.TXT`(`w h bits format`, `dump` 의 .txt 와 같은
   줄 형식이라 파서를 공유), CLUT8 이면 `SHOTnnnn.PAL` 도 같이 남긴다. 카운터는 실행마다 0000 부터
   (9999 를 넘기면 8.3 이름이 깨진다, 알려진 한계). `dump` 의 `_scaled.bin` 은 SCI 가 보고하는 소스
   픽셀 크기만큼 쓰도록 고쳤다(M2) — 트루컬러 비디오 소스일 때 픽셀당 1바이트만 쓰던 버그였다
   (범용 debug-socket/`engines/sci/debugsocket.cpp` 수정, DOS 전용 아님).
3. **성능** — 로그에 프레임 시간, 화면 전송 시간, 타이머 지터(1kHz 대비)를 남긴다. DOSBox-X `cputype=pentium`
   은 참고용이고 판정은 실기(M4).

### 7.4 DJGPP 파일 I/O

M0 의 DOS 부팅을 막은 원인은 세 가지 모두 엔진이 아니라 DJGPP 파일 I/O 였다 (task-9-report.md 참고).
크래시는 없었고 메모리도 문제가 아니었다.

- **상대 seek**: DJGPP 의 `fseeko64(SEEK_CUR)` 는 stdio 버퍼를 무시하고 DOS 핸들의 실제 위치에서 더한다.
  `configure` 가 `msdosdjgpp` 에 `HAS_FSEEKO64` 를 고른 탓에 리소스 맵을 잘못 걸어 "Volume version not
  detected"·"Unknown compression method" 가 났다. 고정: `msdosdjgpp` 는 64비트 `fseeko` 프로브를
  건너뛰고 보통 `fseek`/`ftell` 을 쓴다 (`long`, 2GB 한계 — FAT16/SCI 데이터에는 충분하다).
- **`fstat()` 비용**: DOSBox-X 에서 `fstat()` 한 번에 ≈200ms 걸린다. `PosixIoStream::size()` 가 리소스
  맵 항목마다 이를 불러 965 항목 스캔에 3분 넘게 걸렸다. 고정: `DOS_DJGPP` 에서는 파일 끝으로 seek 해서
  크기를 잰다(`StdioStream::size()`) — 실측 0.35ms(seek) vs 12.9ms(fstat).
- **원자적 쓰기**: `DumpFile`/세이브가 쓰는 `NAME.EXT.tmp` 는 8.3 이름이 아니다. 고정: `DOS_DJGPP` 에서
  `STDIOSTREAM_NO_ATOMIC_SUPPORT` (Atari/MiNT 전례와 같다). 대가: 저장 중 크래시가 나면 세이브 파일이
  잘릴 수 있다.

## 8. 마일스톤

| | 내용 | 통과 기준 |
|---|---|---|
| M0 | 스파이크 + 최소 포트 | 아래 스파이크 3건이 결론 남. KQ1 이 320×200 CLUT8 로 타이틀까지, 덤프가 기준과 일치. COM 채널로 명령 1개 왕복. **결과: 통과 — DOSBox-X / Staging (memsize 16)** (`harness/dos/spikes/RESULTS.md`, `harness/dos/m0_accept.py`) |
| M1 | hires 텍스트, L 프리셋 | KQ1·LB1 한국어 대사가 640×400 CLUT8 에 나옴. EUC-KR 패치와 패치 원본 폰트도 확인. □ 대체 로그. **결과: 통과 — DOSBox-X / Staging.** KQ1 타이틀 + 방 1 "look", LB1 한국어 복사방지 화면(`random_seed=1`) 이 FreeType 없는 리눅스 빌드와 `_low`/`_scaled`/`_pal`/`_layer`/`_out` 바이트 단위로 일치; 모드 로그 320×200 → 640×400 CLUT8 (`harness/dos/m1_accept.py`). □ 대체와 `korean.fnt` 가 □ 보다 먼저 쓰이는 것은 수동 실행으로 확인했다 (Task 4); 자동 인수 캡처에는 없는 글리프가 없다 |
| M2 | 알파, U 프리셋 | XRGB8888 (`rgb_rendering=true`) 에서 기준 이미지와 일치 — SCI 는 4바이트 포맷만 받으므로 RGB565 는 해당 없음. 640×480 줄 반복 폴백은 `dos_force_fallback=true` 로 시험 (두 에뮬레이터 모두 640×400 XRGB8888 이 있으므로). SVFN 2bpp. **결과: 통과 — DOSBox-X / Staging (각 56/56 세부 항목).** 정확 640×400 RGB888@4 는 리눅스 기준과 0 화소 차(커서 제외); 640×480 줄 반복 폴백을 `logicalRow()` 로 되접으면 반복 행이 원본과 같고 `_out` 과 일치하며, 정확 모드 샷과도 커서 상자 밖에서 전부 일치; `dos_truecolor=off` 는 CLUT8 로 감; LB1 U 맵도 확인. M1/M0 회귀도 통과 (`harness/dos/m2_accept.py`) |
| M3 | 사운드 | KQ1 타이틀 곡의 OPL 노트 온셋을 리눅스(MAME OPL) 와 비교해 편차 중앙값 ≤ 2ms, 최대 ≤ 10ms, 방 로딩 구간 최대 ≤ 20ms. MPU-401 UART 로 같은 곡이 나옴. SB PCM 효과음 1개 재생. **결과: 통과 — DOSBox-X / Staging** (`harness/dos/m3_accept.py`). 실제 타이머 격자는 4ms 가 아니라 10ms(5.1/5.2절). **1차 리뷰에서 "최대"·"로딩구간 최대" 항목은 편차를 ≤5ms 로 접어 계산해 원리적으로 실패할 수 없는 검사였음이 드러났다** (이제는 정보용으로만 남긴다) — **실제 판정 기준**은 (1) 키온 위상 편차 중앙값(그리드 위상은 중앙값이 가장 작은 오프셋에 고정) ≤2ms — DOSBox-X/Staging 모두 1.0ms, (2) 리눅스 기준(SDL dummy 오디오의 버퍼 경계로 환산한 "음악 시간")과 약 1초 창 단위 비교 — OPL 최악 −21.7ms(두 에뮬레이터 동일, 한도 30ms), MPU 최악 +36.4ms(X)/+24.8ms(Staging, 한도 50ms), 전체 구간 길이 1% 이내(OPL +0.04%, MPU +0.24%/+0.08%), (3) getMillis 를 DOS CMOS RTC 와 ~45초 음악 실행 동안 맞대어 |차이| ≤1100ms — 실측 −14~−64ms, (4) 마스터 볼륨 SysEx 버스트를 값으로 비교 — 양쪽 `5D 5D 5D` 로 일치. 시계 확인은 새 디버그 소켓 명령 `millis`(getMillis 값)와 `rtc`(그 값과 DOS CMOS 초시계를 짝지어 반환)로 한다. 그 밖의 지표(REG 공통 프리픽스, MIDI 클릭 전 공통 메시지 수, 믹서 레이트, 타이머 셀프테스트)는 최초 통과 실행 기준으로 변경 없음 — DOSBox-X: REG 2102줄, MIDI 583/583, 믹서 44072/44100, 타이머 calls=180 getMillis=3003 bios=54; Staging: REG 2086줄, MIDI 587/587, 믹서 44102, 타이머 179/3000/54. M0–M2 회귀도 두 에뮬레이터에서 모두 통과 |
| M4 | 마무리 | GUI 오버레이, 세이브/로드, 실기 측정, 배포 패키지 |

### M0 스파이크 (구현 전에 확인)

1. IRQ0 1kHz 보호 모드 핸들러가 SDL3 DOS 와 공존하는가, `cli` 뮤텍스가 동작하는가.
2. direct-FB 모드의 `SDL_UpdateWindowSurfaceRects` 가 부분 영역만 보내는가 — 코드로는 예 (3.2). 640×400
   CLUT8/RGB565 에서 전체·부분 전송 시간을 재서 확인한다.
3. SCI 전용 ScummVM 코어가 DJGPP GCC 12 로 컴파일·링크되는가, 크기는.

## 9. 위험

| 위험 | 대응 |
|---|---|
| IRQ0 핸들러가 불안정 (SDL3 IRQ0 사용, 메모리 잠금 실패) | 협력형 타이머로 폴백 (5.4) |
| 실기 카드마다 VESA 모드 목록이 다름 | 3.1 의 선택 규칙, 두 에뮬레이터에서 테스트 |
| Pentium 에서 FreeType 래스터화가 느림 | 기본 배포는 SVFN, FreeType 은 선택 빌드 |
| 8.3 이름 제약 | 배포 경로 전부 8.3 |
| 16MB 초과 | 폰트는 2bpp, 엔진은 SCI 만 |
| VRAM 전송이 병목 (실기) | ① SDL 전송 루프에 FPU 64비트 쓰기 (SDL 본가 패치) ② 선택: 카드별 확대 블릿 (ViRGE 등) |
| 찢어짐이 눈에 띔 | `dos_vsync=wait`, 그래도 안 되면 `flip` (SDL LFB 힌트 패치). 일부 카드는 LFB 가 뱅크보다 느리므로 실측으로 판단 |
