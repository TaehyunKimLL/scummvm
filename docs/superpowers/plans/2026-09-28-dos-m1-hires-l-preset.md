# DOS 포트 M1 — hires 텍스트와 L 프리셋 구현 계획

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** DOS 빌드에서 KQ1 한국어(UTF-8 번역)와 LB1 한국어(EUC-KR 패치, 패치 원본 `korean.fnt`)의 대사가 640×400 CLUT8 hires 로
나오고, 글리프는 새 SVFN v2 16px 1bpp 폰트 `KO2350.SVF` 에서 오며, 폰트에 없는 글자는 □ 로 그려진다. DOS 덤프가 FreeType 없는
리눅스 빌드와 바이트 단위로 같다.

**Architecture:** SCI 의 hires 폰트 적재(`GfxFontUnicode::load`, `GfxCache::faceChainFor`)가 파일 머리말을 보고 SVFN 도 받는다
(지금은 SCVMUNI 와 TTF 만). 맵 키 `[hires] missing=` 는 공통 코드(`graphics/hires_text`)의 래퍼 글리프 소스로 구현하고 SCI 가
체인 끝에 씌운다. DOS 는 8.3 이름만 쓰므로 DOS 프리셋 맵과 폰트는 한 디렉터리(`dists/engine-data/hires_text/dos/`, 배포 시 `DATA\`)에
두고 맵이 폰트를 맵 기준 상대 경로로 가리킨다.

**Tech Stack:** ScummVM i18n (SCI, graphics/hires_text), Python 3 + Pillow (`tools/korean/mkfont.py`), CxxTest, DJGPP/SDL3 DOS 빌드,
DOSBox-X/Staging 하네스(M0 의 `harness/dos/dosgame.py`).

**Spec:** `docs/superpowers/specs/2026-09-28-scummvm-dos-sdl3-design.md` (4장 폰트와 알파, 8장 M1)

## Global Constraints

- 브랜치 `dos-port`, 워크트리 `~/work/scummvm/dos`. 하네스는 `~/work/scummvm` (master) — 이름 붙인 파일만 `git add`, `git stash` 금지.
- 엔진 변경은 i18n 일반 기능으로만(SVFN 적재, `missing=`), DOS `#ifdef` 없이, DOS 백엔드와 다른 커밋으로 (ledger ruling, M1/M2 prep).
- DOS 에 배포되는 파일 이름은 8.3.
- L 프리셋: SVFN v2, 16px, 1bpp, ASCII + KS X 1001 기호·낱자 + 완성형 2350자, 한자 없음. □ = U+25A1. 출력 CLUT8 640×400.
- `missing=` 폭: 원래 글자가 `Graphics::Unicode::isWide()` 면 2셀(전각), 아니면 1셀. 대체한 코드 포인트는 코드 포인트마다 처음 한 번만 로그.
- 커밋 트레일러:
  ```
  Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4
  ```
- 리눅스 `make test` 에는 알려진 실패 1건이 있다(`HiResTextTtfFitTestSuite::test_pad_rows_moves_the_glyph_down_whole`, 시스템 폰트 없음). 그 밖의 실패는 새 것이다.
- 참고 사실: `.superpowers/sdd/m1m2-research.md` (파일:줄 목록).

## 테스트 데이터

| 게임 | 위치 | 성격 |
|---|---|---|
| KQ1 한국어 | `~/work/scummvm/dist/scummvm-sci-i18n-ef8dc87f-win64/gamedata/kq1-ko` | 영어 볼륨 + UTF-8 `text.*` + `sci-ko.str` + `korean.uni` |
| LB1 한국어 | `~/work/scummvm/dist/scummvm-i18n-edec8f56-win64/testgames/lb1-ko-patch` | EUC-KR 팬 패치, `korean.fnt`, `CB1SG.*`, `TEXT.052`/`SCRIPT.052` |

하네스는 이 디렉터리들을 읽기만 한다 (복사해서 쓴다).

## 파일 구조

| 파일 | 책임 |
|---|---|
| `graphics/hires_text/glyph_source_file.h`, `.cpp` (신규) | 파일 머리말로 SVFN 을 알아보고 `SvfnGlyphSource` 를 만드는 한 곳 |
| `graphics/hires_text/glyph_source_missing.h`, `.cpp` (신규) | `MissingGlyphSource`: 안쪽 소스에 없는 코드 포인트를 □ 로 |
| `graphics/hires_text/font_map.h/.cpp` | `[hires] missing=` 파싱 → `HiResFontMap` 필드 |
| `graphics/module.mk` | 새 오브젝트 |
| `engines/sci/graphics/fontunicode.cpp` | `GfxFontUnicode::load` 가 SVFN 도 받는다 |
| `engines/sci/graphics/cache.cpp` | `faceChainFor` 가 SVFN 경로를 받는다, 체인에 `MissingGlyphSource` 를 씌운다 |
| `tools/korean/mkfont.py` | `--unicode` 묶음 `ksx1001-nohanja` |
| `tools/korean/bake-dos-fonts.sh` (신규) | `KO2350.SVF` 굽기 |
| `dists/engine-data/hires_text/dos/` (신규) | `KO2350.SVF`, `KQ1KOL.MAP`, `LB1KOL.MAP`, `OFL.TXT` |
| `backends/platform/dos/build-dos.sh` | `dist/dos/DATA/` 에 위 디렉터리를 복사 |
| `test/graphics/hires_text_glyph_source_file.h`, `hires_text_missing.h` (신규), `hires_text_font_map.h` | 단위 테스트 |
| `harness/dos/m1_accept.py` (신규) | M1 수락 테스트 |

---

### Task 1: SVFN 을 알아보는 파일 적재 (공통 코드)

**Files:**
- Create: `graphics/hires_text/glyph_source_file.h`, `graphics/hires_text/glyph_source_file.cpp`
- Modify: `graphics/module.mk`
- Test: `test/graphics/hires_text_glyph_source_file.h`, `test/module.mk` (TESTS 는 이미 `hires_text*.h` 글롭이면 수정 불필요 — 확인)

**Interfaces:**
- Produces (namespace `Graphics`):
  - `bool isSvfnFile(const byte *head, uint32 size)` — 첫 4바이트가 `"SVFN"` 이면 true
  - `UnicodeGlyphSource *createSvfnSource(Common::SeekableReadStream &stream, Common::String &error)` — `HiResBitmapFont::load(stream)` 후
    `new SvfnGlyphSource(font, DisposeAfterUse::YES)`; 실패하면 nullptr 과 이유

- [ ] **Step 1: 실패하는 테스트** — `test/graphics/hires_text_glyph_source_file.h`: (a) `"SVFN"` 머리말 → `isSvfnFile` true, `"SCVM"`/짧은 버퍼 → false;
  (b) `hires_text_svfn_source.h` 의 v2 3글자 픽스처와 같은 방식으로 만든 SVFN 바이트를 `Common::MemoryReadStream` 에 넣어
  `createSvfnSource` 가 소스를 돌려주고 `cells(0x41) == 1`, `cells(0xAC00) == 2`; (c) 잘린 SVFN → nullptr, error 비어 있지 않음.
  픽스처 생성 코드는 `test/graphics/hires_text_svfn_source.h` 에서 가져온다(복사하지 말고, 그 헬퍼가 헤더에 static 으로 있으면
  include 해서 쓴다; 불가능하면 이 테스트 파일에 최소 픽스처를 새로 쓴다).
- [ ] **Step 2: 실패 확인** — `cd ~/work/scummvm/builds/linux-dos-test && make test` → 새 헤더 없음으로 실패.
- [ ] **Step 3: 구현** — 두 함수. `graphics/module.mk` 의 hires_text 오브젝트 목록에 `hires_text/glyph_source_file.o`.
- [ ] **Step 4: 통과 확인** — `make test`, 새 스위트 통과, 알려진 1건 외 실패 없음.
- [ ] **Step 5: Commit** — `"GRAPHICS: Recognise an SVFN font by its header and open it as a glyph source"` / `"TEST: ..."` 두 커밋.

---

### Task 2: SCI 가 SVFN 폰트를 쓴다 (엔진, i18n 일반 기능)

지금 SCI 는 `.uni` 번들(`GfxFontUnicode::load`, SCVMUNI 만)과 맵의 face 체인(`GfxCache::faceChainFor` → `ttfSource`, TTF 만)으로
hires 글리프를 얻는다. FreeType 없는 빌드(DOS)에서는 TTF 가 열리지 않는다. 두 곳 모두 SVFN 을 받게 한다.

**Files:**
- Modify: `engines/sci/graphics/fontunicode.cpp` (`GfxFontUnicode::load`)
- Modify: `engines/sci/graphics/cache.cpp` (`faceChainFor`, 필요하면 `ttfSource` 옆에 `svfnSource`)

**Interfaces:**
- Consumes: `Graphics::isSvfnFile`, `Graphics::createSvfnSource` (Task 1)
- Produces: 맵의 `face=`/`[fonts]` 경로가 SVFN 파일이면 그 파일이 체인의 한 면이 된다. `.uni` 번들 이름 목록(`sci.uni`, `korean.uni`,
  `towns.uni`)의 파일이 SVFN 이어도 적재된다.

요구사항:
1. `GfxFontUnicode::load(filename)`: 읽은 바이트가 `isSvfnFile` 이면 `createSvfnSource`, 아니면 지금처럼 `ScvmuniGlyphSource::create`.
2. `faceChainFor`: 각 경로를 열어 머리말이 SVFN 이면 SVFN 소스를(크기 인자는 무시 — 비트맵은 제 크기), 아니면 지금의 `ttfSource`.
   `firstFace` (TTF 전용 정보: `lineTop()`, `rowPad()`) 는 첫 면이 TTF 일 때만 채운다. 이것을 쓰는 곳(`cache.cpp:482` 부근의
   `firstFace->lineTop()`, `NormalizedGlyphSource::create(..., rowPad())` 등)은 첫 면이 SVFN 이면 그 정보 없이 동작하도록
   분기한다 — SVFN 은 셀 높이가 곧 줄 높이이고 rowPad 는 0.
3. 열기에 실패한 SVFN 은 TTF 와 같은 방식으로 한 번 경고하고 체인에서 빠진다(캐시 키 규칙 동일).
4. FreeType 이 있는 빌드의 동작은 TTF 경로에 대해 바뀌지 않는다.

- [ ] **Step 1: FreeType 없는 리눅스 빌드 준비** —
  `mkdir -p ~/work/scummvm/builds/linux-dos-noft && cd $_ && ~/work/scummvm/dos/configure --disable-all-engines --enable-engine=sci --enable-debug-socket --disable-freetype2 && make -j$(nproc)`
- [ ] **Step 2: 기준 동작 기록(RED)** — `harness/i18n/scigame.py` 의 `SciGame.launch` 로 KQ1 한국어(테스트 데이터 표)를
  `binary=~/work/scummvm/builds/linux-dos-noft/scummvm`, `extra_ini="hires_text_map=<임시 맵>\n"` 로 띄운다. 임시 맵은
  `[hires]\nscale=2\nalpha=false\nface=ko\n[fonts]\nko=<Task 3 전이므로, 임시로 mkfont.py 로 구운 아무 SVFN v2 1bpp 한글 폰트>`
  로 두고, 로그에 `hires_text_font ... ` 적재 실패 경고가 나오는 것(=SVFN 을 TTF 로 열려다 실패)을 확인한다.
  임시 폰트: `python3 tools/korean/mkfont.py ~/work/scummvm/fonts/neodgm.ttf /tmp/claude-1000/ko16.svf --size 16 --cell 16 --bpp 1 --unicode ascii,hangul`
  (옵션이 거부되면 `--help` 로 맞춘다).
- [ ] **Step 3: 구현** — 요구사항 1–4.
- [ ] **Step 4: 확인(GREEN)** — 같은 실행에서 경고가 사라지고 `SCI: face chain ... (1 faces ...)` debug 줄, 타이틀 화면 덤프(`dump`)의
  `_scaled.bin` 에 한글 글리프가 있다(`_out.txt` 나 `wait_text("게임시작")` 로 확인). FreeType 있는 빌드(`linux-dos-test`)로 같은
  KQ1 한국어를 기존 `kq1-ko.map`(TTF)으로 띄워 타이틀 덤프가 이 변경 전후 같음을 확인한다(변경 전 덤프를 Step 2 에서 같이 떠 둔다).
- [ ] **Step 5: Commit** — `"SCI: Take SVFN bitmap fonts in the hi-res face chain and the .uni bundle"`.

---

### Task 3: `KO2350.SVF` 굽기

**Files:**
- Modify: `tools/korean/mkfont.py` (`NAMED_RANGES` 에 `ksx1001-nohanja`)
- Create: `tools/korean/bake-dos-fonts.sh`
- Create: `dists/engine-data/hires_text/dos/KO2350.SVF`, `dists/engine-data/hires_text/dos/OFL.TXT`

**Interfaces:**
- Produces: `KO2350.SVF` — SVFN v2, 16px 셀, 1bpp, 코드 포인트 = ASCII(0x20–0x7E) ∪ (`_ksx1001()` 에서 CJK 한자 블록 U+4E00–U+9FFF,
  U+F900–U+FAFF 를 뺀 것). U+25A1 포함.

- [ ] **Step 1:** `ksx1001-nohanja` = `[cp for cp in _ksx1001() if not (0x4E00 <= cp <= 0x9FFF or 0xF900 <= cp <= 0xFAFF)]`.
  `python3 -c` 로 개수를 확인한다: 한글 음절(U+AC00–U+D7A3) 이 정확히 2350, 한자 0, U+25A1 있음.
- [ ] **Step 2:** `bake-dos-fonts.sh`:
  ```bash
  #!/bin/bash
  # Bake the DOS L-preset font: neodgm 16 px, 1 bpp, KS X 1001 without Hanja.
  set -e
  here="$(cd "$(dirname "$0")" && pwd)"
  src="${NEODGM:-$HOME/work/scummvm/fonts/neodgm.ttf}"
  out="$here/../../dists/engine-data/hires_text/dos"
  python3 "$here/mkfont.py" "$src" "$out/KO2350.SVF" --size 16 --cell 16 --bpp 1 --unicode ascii,ksx1001-nohanja
  ```
  (neodgm 이 이 경로에 없으면 `dists/engine-data/hires_text/fonts/neodgm/neodgm.ttf` 를 쓴다; 옵션 이름은 `mkfont.py --help` 에 맞춘다.)
- [ ] **Step 3:** 실행, 크기 확인(≈110KB 예상, 실제 값을 커밋 메시지에). `OFL.TXT` 는 neodgm 의 라이선스 파일을 8.3 이름으로 복사.
  `mkfont.py` 가 찍는 요약 줄에 누락 글자(폰트에 없어 빠진 코드 포인트) 수가 있으면 그 수와, U+25A1 이 빠지지 않았음을 보고한다.
- [ ] **Step 4:** Task 1 의 `createSvfnSource` 로 읽히는지 — 작은 CxxTest 대신, Task 2 의 리눅스 no-FreeType 실행에서 이 파일을 face 로 써서
  확인한다(Task 6 이 정식으로 한다). 여기서는 `python3` 로 머리말(버전 2, bpp 1, cell 16x16)과 cmap 개수만 검사한다.
- [ ] **Step 5: Commit** — `"TOOLS: KS X 1001 without Hanja as a named range, and the DOS font bake"` / `"DISTS: KO2350.SVF, neodgm 16 px 1 bpp for the DOS L preset"`.

---

### Task 4: `[hires] missing=` (공통 코드)

**Files:**
- Create: `graphics/hires_text/glyph_source_missing.h`, `.cpp`
- Modify: `graphics/hires_text/font_map.h`, `font_map.cpp`, `graphics/module.mk`
- Test: `test/graphics/hires_text_missing.h` (신규), `test/graphics/hires_text_font_map.h`

**Interfaces:**
- `HiResFontMapData` (또는 `[hires]` 값을 담는 기존 구조체 — `alpha`/`alphaFromMap` 이 있는 곳, `font_map.h:271`) 에
  `uint32 missing; ///< code point drawn for a character no source has; 0 = off` 와 `bool missingFromMap`.
  파싱: `missing=u+25a1` 또는 `missing=U+25A1` 또는 `missing=0x25a1`; 잘못된 값은 경고하고 무시(0 유지). 기본 0.
- `class Graphics::MissingGlyphSource : public UnicodeGlyphSource`:
  - `MissingGlyphSource(UnicodeGlyphSource *inner, uint32 boxCp, DisposeAfterUse::Flag dispose)`
  - `cells(cp)`: `inner->cells(cp) > 0` 이면 그 값; 아니면 `Unicode::isWide(cp) ? 2 : 1` (단, inner 에 boxCp 가 없으면 0 — 대체 불가)
  - `row(cp, y)`: inner 가 cp 를 가지면 inner 의 행. 아니면 전각(2셀)은 `inner->row(boxCp, y)`; 반각(1셀)은 셀 폭 `cellWidth()/2`
    안에 1px 테두리 사각형(위아래 1행 여백, 좌우 1열 여백)을 inner 의 bpp 로 그린 행 (8bpp 는 255, 2bpp 는 3, 1bpp 는 1)
  - `advance(cp)`/`metrics(cp, m)`: inner 가 가지면 inner; 아니면 전각 `advanceWide()`, 반각 `advanceNarrow()`, `m.wide = isWide(cp)`
  - 셀 크기·bpp 는 inner 그대로.
  - 대체한 코드 포인트는 `Common::HashMap<uint32, bool>` 로 기억하고 처음 한 번 `warning("HiResText: U+%04X has no glyph; drawing U+%04X", cp, boxCp)`.

- [ ] **Step 1: 실패하는 테스트** — `hires_text_missing.h`: 작은 가짜 inner 소스(테스트 파일 안에 정의: 셀 16x16, 1bpp, U+0041 과 U+25A1
  만 가짐)로 (a) `cells(0x41)==1` 이고 행은 inner 와 같음; (b) `cells(0xAC00)==2` 이고 `row(0xAC00,y)==inner.row(0x25A1,y)`;
  (c) `cells(0xE9)==1` 이고 반각 상자: 행 1 과 행 14 가 가로선, 행 2–13 은 좌우 끝 열만 켜짐; (d) inner 에 U+25A1 이 없으면 `cells(0xAC00)==0`;
  (e) 같은 코드 포인트로 두 번 물어도 경고는 한 번(경고 횟수는 테스트에서 직접 볼 수 없으면 이 항목은 생략하고 보고한다).
  `hires_text_font_map.h`: `missing=u+25a1` → 0x25A1, `missing=0x25a1` → 0x25A1, `missing=zz` → 0 과 경고, 키 없음 → 0.
- [ ] **Step 2–4:** RED → 구현 → GREEN (`make test`).
- [ ] **Step 5: SCI 연결** — `cache.cpp` 의 face 체인이 완성된 뒤(`faceChainFor` 가 `chain` 을 캐시에 넣기 직전), 맵의 `missing` 이
  0 이 아니면 `chain = new Graphics::MissingGlyphSource(chain, missing, DisposeAfterUse::NO)` 로 감싸 `_chainParts` 에 넣는다.
  SCI 의 폴백 순서(체인 → 레거시 CJK 폰트/패치 원본 폰트 → 리소스 폰트)는 `GfxFontUnicodeAdapter::classify`
  (`fontunicode.cpp:192-217`)가 `hasGlyph` 로 정한다: 체인이 □ 를 "가진다"고 답하면 패치 원본 폰트가 가려진다. 스펙 순서
  (SVF → 패치 원본 폰트 → □)를 지키기 위해, `MissingGlyphSource` 는 체인에 씌우되 `classify` 가 체인을 물을 때는 □ 대체 전의 답
  (inner 의 `cells`)을 쓰고, 레거시 폰트도 글리프가 없을 때만 □ 로 그리도록 한다. 구체적 방법은 구현자가 `classify` 와
  `GfxFontUnicode::hasGlyph` 를 읽고 정하되, 결과 동작을 다음 두 실행으로 보인다:
  - KQ1 한국어 + `missing=u+25a1` + 한 글자를 일부러 뺀 폰트 → 그 글자가 □ 로 그려지고 로그 한 줄.
  - LB1 한국어 패치 + `KO2350.SVF` + `missing=u+25a1` → 한자(KO2350 에 없음)가 패치의 `korean.fnt` 로 그려짐(□ 아님).
- [ ] **Step 6: Commit** — 공통 코드(`GRAPHICS: ...`), 테스트(`TEST: ...`), SCI(`SCI: Draw the map's missing= glyph after every font has declined`) 따로.

---

### Task 5: DOS L 프리셋 맵과 배포

**Files:**
- Create: `dists/engine-data/hires_text/dos/KQ1KOL.MAP`, `dists/engine-data/hires_text/dos/LB1KOL.MAP`
- Modify: `backends/platform/dos/build-dos.sh`

- [ ] **Step 1: 맵** — `KQ1KOL.MAP`:
  ```ini
  ; DOS L preset: King's Quest I (SCI remake), Korean UTF-8 translation.
  ; KO2350.SVF (16 px, 1 bpp) beside this file; characters it lacks draw U+25A1.
  [hires]
  scale=2
  alpha=false
  face=ko
  missing=u+25a1

  [fonts]
  ko=KO2350.SVF

  [latin]
  mode=proportional
  metrics=font
  ```
  `LB1KOL.MAP` 은 첫 줄 주석만 LB1 EUC-KR 패치로 바꾸고 같은 내용. (kq1-ko.map 의 `[font.300]` 등 크기 지정은 비트맵에 의미가 없으므로 뺀다.)
- [ ] **Step 2: 배포** — `build-dos.sh` 끝에: `mkdir -p "$src/dist/dos/DATA" && cp "$src"/dists/engine-data/hires_text/dos/* "$src/dist/dos/DATA/"`.
  DOS 에서 맵은 `hires_text_map=DATA/KQ1KOL.MAP` 로 지정하고 폰트는 맵 기준 상대 경로(`KO2350.SVF`)로 찾는다(`font_map.cpp` 의
  비-`data:` 상대 경로 = 맵 디렉터리 기준).
- [ ] **Step 3:** 빌드 후 `ls dist/dos/DATA` — 네 파일, 전부 8.3.
- [ ] **Step 4: Commit** — `"DISTS: DOS L-preset maps for KQ1 and LB1 Korean"` / `"DOS: Ship the L-preset maps and font in DATA"`.

---

### Task 6: M1 수락 테스트

**Files (하네스 저장소):**
- Create: `harness/dos/m1_accept.py`
- Modify (필요 시): `harness/dos/dosgame.py` — 게임 디렉터리 외에 `extra_ini` 와 추가 파일 복사(`DATA`)를 받도록

- [ ] **Step 1:** `m1_accept.py <emu>` 가 다음을 한다:
  1. 리눅스 기준: `builds/linux-dos-noft/scummvm` (FreeType 없음)로 KQ1 한국어를 `hires_text_map=<dists/.../dos/KQ1KOL.MAP 절대경로>` 로
     띄워 타이틀(`wait_text("게임시작")`, `wait_idle`) 덤프 `T`, 그리고 게임 시작 후 대사창이 뜨는 지점(`kq1_tour.py` 의
     "Begin Game → room 1 → look" 흐름을 따른다) 덤프 `L`.
  2. DOS: `dosgame.launch(emu, <KQ1 한국어 디렉터리>, out, extra_ini="language=ko\nhires_text_map=DATA/KQ1KOL.MAP\n")` 로 같은 두 지점 덤프.
  3. `_scaled.bin` (640×400 hires 평면) 과 `_pal.bin` 을 바이트 비교. `_low.bin` 도 비교.
  4. LB1: 한국어 패치 디렉터리로 같은 방식, 비교 지점은 인트로에서 한국어 텍스트가 처음 보이는 화면(구현자가 결정론적 지점을 고르고
     근거를 적는다; 복사 방지 화면이 먼저 오면 그 화면의 한국어 텍스트로 충분하다).
  5. 모드 전환 확인: DOS 의 SCUMMVM.LOG 에 백엔드의 `DOS: mode 640x400 ...` 줄(M0 최종 수정에서 추가, `debuglevel=1` 필요)이
     있어야 한다 — 320×200 → 640×400 전환이 실제로 일어났다는 증거. 없으면 FAIL.
  6. 기준 디렉터리는 실행마다 지우고(`rmtree`), 리눅스 `dump` 응답이 `OK` 인지 확인한다(m0_accept 와 같은 방식).
  7. 두 게임 × 비교 전부 같으면 `M1 <emu>: PASS`.
- [ ] **Step 2:** DOSBox-X, Staging 둘 다 실행해 PASS.
- [ ] **Step 3:** SCUMMVM.LOG 의 `has no glyph` 줄 수를 보고서에 적는다(KQ1: 번역이 2350자 밖 글자를 쓰는지의 실측).
- [ ] **Step 4: Commit** — `"harness/dos: M1 acceptance, KQ1 and LB1 Korean on the L preset against a FreeType-less Linux build"`.

---

### Task 7: spec 갱신

- [ ] spec 4장에: SCI 가 SVFN 을 face 체인과 `.uni` 번들 양쪽에서 받는다(M1), DOS 프리셋 맵·폰트는 `DATA\` 한 디렉터리·맵 기준 상대 경로(8.3),
  `missing=` 가 레거시 폰트 다음에 온다는 순서 규칙과 구현 위치. 8장 M1 행에 결과(두 에뮬레이터 PASS, KQ1 의 □ 대체 수).
- [ ] Commit — `"docs: DOS design after M1"`.
