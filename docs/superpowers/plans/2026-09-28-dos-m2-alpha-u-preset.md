# DOS 포트 M2 — 트루컬러·알파와 U 프리셋 구현 계획

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** DOS 빌드에서 KQ1·LB1 한국어가 트루컬러(XRGB8888) 640×400 으로 안티앨리어싱 글자(SVFN 8bpp 또는 새 2bpp)와 함께 나오고,
640×400 트루컬러 모드가 없는 카드에서는 640×480 모드에 5줄마다 1줄을 반복해 같은 그림이 나온다. 커서는 모든 모드에서 보인다.
백엔드가 실제로 내보낸 프레임(창 서피스)을 덤프해 검증한다.

**Architecture:** SCI 는 `rgb_rendering=true` 일 때 `getSupportedFormats()` 의 첫 4바이트 포맷을 고른다(`default.cpp:110-125`, 8비트
커버리지 정밀도를 위한 의도된 선택). 그래서 M2 는 엔진을 고치지 않고 DOS 백엔드가 XRGB8888 을 정확히 광고·제공한다. 백엔드는
게임 크기별로 지원 포맷을 계산하고(정확 일치 또는 640×480 줄 반복 폴백), CLUT8 커서를 화면 포맷으로 변환하며, `saveScreenshot()` 로
창 서피스를 파일에 쓴다. SVFN 2bpp 는 로더(`bitmap_font.cpp`)와 `mkfont.py` 만 고친다(렌더러 `expandCoverage` 는 이미 2bpp 를 안다).

**Tech Stack:** 같은 스택 (M0/M1).

**Spec:** `docs/superpowers/specs/2026-09-28-scummvm-dos-sdl3-design.md` (3장 그래픽, 4장 폰트·알파, 8장 M2)

## Global Constraints

- M0/M1 과 같다(브랜치, 하네스 저장소 규칙, 트레일러, 8.3, 엔진은 일반 기능으로만, 알려진 테스트 실패 1건).
- Ruling (M2 prep): 트루컬러는 XRGB8888. spec §3.1 의 RGB565 우선은 SCI 가 4바이트만 받으므로 적용하지 않는다. 백엔드는 목록 순서를
  XRGB8888 먼저로 바꾸지 않아도 된다(SCI 가 4바이트를 골라 쓴다) — 하지만 `getSupportedFormats()` 는 실제로 설정 가능한 포맷만 담는다.
- 줄 반복: 논리 행 y(0..399) → 물리 행 `y + y/5` (0..479), `y % 5 == 4` 인 행은 다음 물리 행에 한 번 더 쓴다. 마우스 역변환: 물리 행 py →
  논리 행 `py - py/6`. (400 × 6/5 = 480.)
- 설정 키: `dos_truecolor=auto|off` (off 면 트루컬러 포맷을 광고하지 않음), `dos_vsync=off|wait` (wait: 포트 0x3DA bit 3 로 수직 귀선 대기 후 전송),
  시험용 `dos_force_fallback=true` (정확 일치 모드가 있어도 줄 반복 폴백을 쓴다 — 하네스가 폴백 경로를 두 에뮬레이터에서 결정론적으로 시험).
- U 프리셋: `KQ1KOU.MAP`/`LB1KOU.MAP`, `alpha=true`, `missing=u+25a1`, 폰트 `KOCP949.SVF`(18px 2bpp, cp949 전체) → `KO2350.SVF`. ini 에 `rgb_rendering=true`.

## 파일 구조

| 파일 | 책임 |
|---|---|
| `graphics/hires_text/bitmap_font.cpp` | SVFN bpp 2 적재 |
| `tools/korean/mkfont.py` | `--bpp 2`, 이름 묶음 `cp949` |
| `docs` 저장소 `~/work/scummvm/docs/docs/FONT_FORMAT.md` | bpp 2 명세 |
| `tools/korean/bake-dos-fonts.sh`, `dists/engine-data/hires_text/dos/KOCP949.SVF`, `KQ1KOU.MAP`, `LB1KOU.MAP` | U 프리셋 자산 |
| `backends/platform/dos/dos-modes.h` | 크기별 지원 포맷과 폴백 모드 선택(순수, 테스트) |
| `backends/platform/dos/line-repeat.h` (신규) | 행 매핑과 dirty rect 확장(순수, 테스트) |
| `backends/graphics/dos/dos-graphics.{h,cpp}` | 폴백 모드, 줄 반복 전송, 커서 변환, `kFeatureCursorPalette`, `saveScreenshot`, `dos_vsync` |
| `gui/debugsocket.cpp` | 일반 명령 `shot` → `g_system->saveScreenshot()` |
| `configure` | msdosdjgpp 에 `-Wno-format` |
| `test/backends/dos_modes.h`, `dos_line_repeat.h`, `test/graphics/hires_text_bitmap_font.h` | 단위 테스트 |
| `harness/dos/m2_accept.py` | M2 수락 |

---

### Task 1: SVFN 2bpp 와 `mkfont.py --bpp 2`

- 로더: `bitmap_font.cpp:142-146` 의 bpp 검사에 2 를 허용, `:153` rowPitch 에 `bpp == 2 ? (cellW + 3) / 4` 가지. 픽셀 순서는
  `expandCoverage` 와 같다(바이트의 상위 비트 쌍이 왼쪽 픽셀, 값 0–3 → ×85).
- 테스트(`hires_text_bitmap_font.h`, 기존 `makeFont` 헬퍼에 bpp 2 지원 추가): 2bpp 폰트 로드, 행 피치, 한 글리프의 커버리지 값 4단계.
- `mkfont.py`: `--bpp` choices 에 2; `pack_glyph` 에 2bpp 가지(0–255 커버리지를 `(v * 3 + 127) // 255` 로 0–3, 행은 `(cell_w+3)//4` 바이트);
  `NAMED_RANGES['cp949']` = cp949 로 디코드되는 모든 2바이트 코드(0x81–0xFE × 0x41–0xFE)의 코드 포인트 + ASCII.
- 확인: 같은 TTF·크기로 8bpp 와 2bpp 를 구워 SVFN 로더로 읽고 같은 글자의 커버리지 차가 모든 픽셀에서 ≤ 85 인 것을 보이는 작은 python 검사
  (또는 CxxTest). 결과를 보고서에.
- `FONT_FORMAT.md` (docs 저장소, `~/work/scummvm/docs`, 자체 git) 의 bpp 필드와 행 레이아웃 절에 2 추가, 따로 커밋.
- 커밋: `GRAPHICS: SVFN at 2 bpp` / `TEST: ...` / `TOOLS: mkfont --bpp 2 and the cp949 range`.

### Task 2: U 프리셋 자산

- `bake-dos-fonts.sh` 에 추가: `KOCP949.SVF` = NanumGothic-Bold 18px `--bpp 2 --unicode ascii,cp949` (v2). 크기를 재어 spec 의 ≈1.4MB 추정과 비교해 보고.
- 맵: `KQ1KOU.MAP`, `LB1KOU.MAP` — `[hires] scale=2 alpha=true face=ko missing=u+25a1`, `[fonts] ko=KOCP949.SVF,KO2350.SVF` (면 체인 문법은
  `font_map.cpp` `parseFaceChain` 을 따른다), `[latin] mode=proportional metrics=font`.
- `build-dos.sh` 는 M1 에서 `dists/engine-data/hires_text/dos/*` 를 이미 `DATA/` 로 복사한다 — 새 파일도 따라간다. NanumGothic 라이선스는
  `OFL.TXT` 에 이어 붙이거나 `OFLNANUM.TXT` 로 둔다.
- 리눅스 no-FreeType 빌드(`builds/linux-dos-noft`)로 KQ1 한국어 + `KQ1KOU.MAP` + `rgb_rendering=true` 를 띄워 대사창 덤프에서 부분 커버리지 픽셀이
  보이는 것을 확인(`_scaled.bin` 이 4바이트 포맷이고 글자 가장자리에 중간 색이 있음).

### Task 3: 크기별 지원 포맷과 폴백 모드 선택 (순수 로직)

- `dos-modes.h`:
  - `struct ModeChoice { int index; bool lineRepeat; }`
  - `ModeChoice chooseMode(const Common::Array<VideoMode> &modes, uint w, uint h, const Graphics::PixelFormat &f, bool forceFallback)`:
    정확 일치(forceFallback 아니면) → 없으면 같은 포맷·같은 가로·세로 `h * 6 / 5` 인 모드(`h % 5 == 0` 일 때만) → 없으면 `{-1,false}`.
  - `supportedFormats(modes, w, h, allowTrueColor)` 를 `chooseMode` 가 성공하는 포맷만 담도록 바꾸고(순서 RGB565, XRGB1555, XRGB8888, 끝에 CLUT8),
    `allowTrueColor=false` 면 CLUT8 만.
- 테스트(`test/backends/dos_modes.h`에 추가): Staging 목록에서 640×400 XRGB8888 은 정확, 640×400 RGB565 는 640×480 폴백, forceFallback 이면
  XRGB8888 도 640×480 (그 목록에 640×480 XRGB8888 이 있으므로), `h % 5 != 0` 은 폴백 없음, allowTrueColor=false → CLUT8 만.

### Task 4: 줄 반복 매핑 (순수 로직)

- `line-repeat.h` (namespace DOS): `int physRow(int y)` = `y + y / 5`; `bool repeats(int y)` = `y % 5 == 4`; `int logicalRow(int py)` = `py - py / 6`;
  `Common::Rect physRect(const Common::Rect &r)` = 위 `physRow(r.top)`, 아래 `physRow(r.bottom - 1) + (repeats(r.bottom - 1) ? 2 : 1)`.
  `void copyRows(byte *dst, int dstPitch, const byte *src, int srcPitch, int bpp, const Common::Rect &r)` — r 의 각 논리 행을 physRow 에,
  repeats 면 physRow+1 에도 복사.
- 테스트(`test/backends/dos_line_repeat.h`): 400 행 전체를 복사하면 480 행이 채워지고 빈 행이 없음; 행 4 가 물리 4·5 에, 행 5 가 물리 6 에; logicalRow 가
  physRow 의 역(모든 y 에 대해 `logicalRow(physRow(y)) == y`, 반복된 물리 행도 같은 y); physRect 경계.

### Task 5: `DosGraphicsManager` — 폴백, 커서 변환, 스크린샷, vsync

- `endGFXTransaction`: `chooseMode(..., ConfMan.getBool("dos_force_fallback"))`, `_lineRepeat` 저장, 표면 검증은 물리 모드 크기로. debug(1) 줄에
  `DOS: mode WxH <fmt>` 와 폴백이면 ` (line repeat from 640x400)`.
- `getSupportedFormats()`: 하드코딩 640×400 을 없애고, 마지막 `initSize` 크기(없으면 640×400)로 `supportedFormats(..., !dos_truecolor=off)`.
  ConfMan 기본값 등록은 `OSystem_DOS::initBackend()` 에서(`dos_truecolor=auto`, `dos_vsync=off`, `dos_force_fallback=false`).
- `updateScreen`: 전송 사각형을 `_lineRepeat` 면 `copyRows`/`physRect` 로; 커서는 물리 좌표에 그린다(논리 마우스 y → physRow). 전체 갱신도 같은 경로.
- `gameMouse(wx, wy)`: `_lineRepeat` 면 y 에 `logicalRow`.
- 커서: `hasFeature(kFeatureCursorPalette)` true, `setCursorPalette` 저장, `setFeatureState(kFeatureCursorPalette)` 로 켜고 끔.
  `setMouseCursor` 가 CLUT8 커서를 받고 화면이 트루컬러면 키 색이 아닌 픽셀을 (커서 팔레트가 켜져 있으면 그것, 아니면 게임 팔레트)로
  화면 포맷에 변환해 `SoftCursor` 에 넣고, 키 색은 화면 포맷에 없는 값(예: 0x00000000 과 겹치지 않게 변환 결과에 없는 색을 골라)으로 둔다.
  팔레트가 바뀌면(게임 또는 커서) 다시 변환한다. 화면·커서 포맷이 같으면 그대로.
- `saveScreenshot()`: 창 서피스를 `SHOTnnnn.RAW`(원시 행, pitch = w × bpp) + `SHOTnnnn.TXT`(`w h bpp format`) + CLUT8 이면 `SHOTnnnn.PAL`
  로 현재 디렉터리에 쓴다(nnnn 은 0000 부터 증가).
- `dos_vsync=wait`: 전송 직전 `while (inportb(0x3DA) & 8); while (!(inportb(0x3DA) & 8));`.
- 커밋은 기능별로 나눈다.

### Task 6: 디버그 소켓 `shot` 명령, 경고 정리

- `gui/debugsocket.cpp` `genericCommand`: `shot` → `g_system->saveScreenshot(); out = "OK";` (도움말/주석에 한 줄). 모든 플랫폼에서 무해
  (SDL 백엔드는 자기 스크린샷 경로에 저장).
- `configure`: msdosdjgpp 호스트에 `append_var CXXFLAGS "-Wno-format"` — DJGPP 에서 uint32 가 `unsigned long` 이라 나오는 ~1400 건을 끈다
  (리눅스는 그대로 검사). DOS 빌드 경고 수를 전후로 보고.

### Task 7: M2 수락 테스트

`harness/dos/m2_accept.py <emu>`:
1. 리눅스 기준: `builds/linux-dos-noft` 로 KQ1 한국어 + `KQ1KOU.MAP` + `rgb_rendering=true`, M1 과 같은 두 지점 덤프(`T`, `L`).
2. DOS 정확 모드: 같은 ini + `debuglevel=1`, 로그에 `DOS: mode 640x400 XRGB8888`(포맷 문자열은 `PixelFormat::toString()` 이 내는 그대로) 확인.
   엔진 덤프 `_scaled.bin` 을 `_out.txt`/포맷 정보로 RGB 로 바꿔 리눅스와 비교(리눅스 포맷이 ARGB 등으로 다를 수 있으므로 RGB 값 비교, 정확히 같아야 함).
   `shot` 으로 창 서피스를 받아 엔진 `_scaled` 와 RGB 로 같음(커서를 숨기거나 화면 밖으로 옮긴 뒤 — `move` 로 커서를 구석에, 또는 커서 영역 제외).
3. DOS 폴백: `dos_force_fallback=true` 로 다시. 로그에 `(line repeat from 640x400)`. `shot` 은 640×480 이고, 물리 행을 `logicalRow` 로 되돌리면 2번의 창 서피스와 같다.
4. LB1 한국어 패치 + `LB1KOU.MAP` 으로 2번만(정확 모드).
5. `dos_truecolor=off` 로 한 번: 로그 모드가 `640x400 CLUT8` (알파 꺼짐 폴백).
6. 전부 통과면 `M2 <emu>: PASS`. DOSBox-X 와 Staging 둘 다.

### Task 8: spec 갱신

- §3.1: SCI 가 4바이트만 받으므로 DOS 트루컬러는 XRGB8888, RGB565 우선은 SCI 에 적용되지 않음(이유: 커버리지 정밀도, `default.cpp` 주석), 줄 반복 공식과
  `dos_force_fallback`. §3.3 커서 변환. §4 SVFN 2bpp 완료, KOCP949 실제 크기. §7.3 `shot`. §8 M2 결과.
