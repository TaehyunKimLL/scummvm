# 한글 폰트 미리보기와 짝 맞추기

작성: 2026-10-14. 짝 문서: [조합형 조사](2026-10-14-johab-font-research.md),
[비트맵 폰트 형식](2026-10-14-johab-font-formats.md),
[`[shadow.N]` 설계](../superpowers/specs/2026-10-14-per-font-shadow-design.md).

게임마다 원래 영문 폰트를 하나씩 보여 주고, 폰트마다 어울리는 한글 비트맵 폰트를 골라
640×400 화면 배율로 견주는 도구와, 고른 결과를 SVF와 맵으로 바꾸는 과정을 정리한다.

## 1. 전체 흐름

```
게임 파일 ──(측정·내보내기)──> english.json ─┐
폰트 수집본·TTF ──(내보내기)──> korean*.json ─┼─> 짝 맞추기 페이지 ──(내보내기 버튼)──> 선택 JSON
                                               │
선택 JSON + korean*.json ──(pairing2svf.py)──> KPxxxxxx.SVF + 게임별 맵(HIRESKP.MAP, M1KP.MAP …)
```

- 페이지: claude.ai 아티팩트 "한글 폰트 짝 맞추기" (`VVXGqtaP9wppYCN2oboyqZ`). 페이지와 데이터 파일은
  `scratchpad/art/font-pairing.html`, `scratchpad/art/pairdata/`에서 게시한다.
- 데이터와 스크립트: `~/work/scummvm/runs/fontmeasure/` (저장소 밖, 개인 연구용).
- 변환 도구: 이 브랜치의 `tools/korean/pairing2svf.py` (+ `test_pairing2svf.py`).

## 2. 페이지 사용법

**게임과 폰트.** 왼쪽에서 게임(24종: SCI 13, AGI 1, SCUMM 10)을 고르면 그 게임의 원래 폰트(폰트 id·
charset)마다 카드가 나온다. 카드에는 높이, 고정폭/가변폭, bpp, 서체 계열(serif / sans / decorative /
pixel), 쓰임이 붙는다.

**미리보기 세 줄.** 줄 1은 원래 영문 폰트 ×2, 줄 2는 한글만 고른 폰트이고 나머지는 원래 폰트(게임에서
보게 될 혼합 줄), 줄 3은 고른 한글 폰트만(그 폰트의 영문 포함). 문장은 왼쪽에서 고칠 수 있다.

**한글 후보 거르기.**
- 크기: 원래 폰트 줄 높이 ×2의 ±25% 안에 있는 것만. 하나도 없으면 전부 보인다.
- 비트 깊이: 전부 / 1bpp / AA(2bpp).
- 정렬: "추천 순"(크기 차 + 계열이 다르면 벌점, decorative는 serif와 짝) 또는 "출처별로 묶어서".

**꾸밈(외곽선·그림자).** 엔진이 실시간으로 그리는 꾸밈을 미리 본다.
- 종류: 게임 기본(SCUMM 한국어 패치 `korean0N.fnt` 1번 바이트: 1 없음, 0/4+ 외곽선, 2 그림자, 3 외곽선+그림자),
  없음, 외곽선, 그림자, 외곽선+그림자.
- 크기: "원본 비례"(원래 폰트의 측정한 꾸밈 × 2를 한글 폰트 크기에 맞춤, 측정이 없으면 16 px을 엔진 기본으로 놓고 비례)
  또는 "엔진 기본"(외곽선 1.5 px, 그림자 2 px = 1 게임 px). 카드 설명에 실제 크기를 적는다.
- 색: 검정 / 진회색 / 회색. 검정 배경에서는 검정 외곽선이 안 보이므로 배경을 파랑·회색으로 바꿔 본다.

**내보내기.** "이 게임 선택 복사" / "전체 게임 복사"가 선택을 JSON(`scummvm-ko-font-pairing/1`)으로
클립보드에 넣는다. 고르지 않은 폰트는 그때의 맨 위 후보로 채우고 `"picked": false`로 표시한다. 고른 것은
브라우저에 저장되어 다시 열어도 남는다.

**큰 데이터는 필요할 때만.** 20·22·28·34·36·40·48·54 px 파일은 고른 게임의 폰트가 그 크기를 필요로 할
때만 읽는다(줄 높이 ×2의 ±25%).

## 3. 데이터

### 3.1 원래 영문 폰트 (`export/english.json`, 158개 폰트, 24 게임)

| 스크립트 | 하는 일 |
|---|---|
| `sci_fontmeasure.py` (`run_sci.sh`) | SCI 폰트 리소스(SCI0 LZW, SCI1 LZW1, SCI1.1 DCL) |
| `scumm_fontmeasure.py`, `scumm_index.py` (`run_scumm.sh`) | SCUMM v3 LFL, v4 LEC, v5/v6 CHAR 블록 |
| `scumm_v2_fontmeasure.py`, `agi_fontmeasure.py` | MM/Zak 내장 8×8, AGI PC-BIOS 8×8 |
| `specimens.py`, `classify.py` | 표본 그림(61개 서체), 서체 계열 판정 |
| `export_json.py` | `english.json` |

측정 결과는 `english-fonts.md`. 줄 높이는 8(상태줄·UI), 9–10(SCI 본문, SCUMM 대사), 12(SCI 큰 메시지),
14–15(SCUMM 제목), 18, 22–23(SCI 제목), 27–28(DOTT 제목). AGI·MM·Zak만 고정폭이고 나머지는 가변폭.

손으로 고친 값: Indy4 cs4는 대문자 전용 서체(대문자가 소문자 자리에 있음)라 기준선을 자동으로 못 구해
`ascent=10`(셀 행 11)을 넣었다. 페이지는 대문자 자리가 비고 소문자 자리에 잉크가 있으면 대문자를 소문자
자리에서 그린다(MI2 cs5/cs8도 같음).

### 3.2 한글 후보

| 파일 | 내용 | 스크립트 |
|---|---|---|
| `korean.json` | 수집본 비트맵 52개 + 현재 SVF | 내보내기 에이전트 (`korean-index.md`) |
| `korean-hwp3.json` | HWP 3.0 서체별 13개(16 px 5벌체 + 조합판, 24 고딕·명조, 40 명조) | `hwp3_export.py` |
| `korean-ttf-pixel.json` | 픽셀 TTF 24개 1bpp(neodgm, 갈무리, 둥근모, hurss DOS*, …) | `ttf_export.py pixel` |
| `korean-ttf-outline.json` | 외곽선 TTF 17서체 × 16·24 px, 2bpp | `ttf_export.py outline` |
| `korean-ttf-extra.json` | 사용자가 준 서체: Gmarket Sans, Terrarum, 마비옛체, Youth, Diphylleia, Grandiflora One | `ttf_extra.py` |
| `korean-ttf-s{20,22,28,34,36}-N.json`, `…b1-N.json` | 22–24서체의 크기별 AA·1bpp | `ttf_sizes.py SIZES [1]` |
| `korean-ttf-s40b1-N.json`, `korean-printer-40.json` | 40 px 1bpp(TTF, 태백·도깨비 40도트, 96MT·48도트 축소) | `ttf_sizes.py 40 1`, `printer_40.py` |
| `korean-printer-large-{1bpp,aa}.json`, `korean-ttf-large-N.json` | 48 px 프린터, 54 px(TTF·96MT) | `printer_large.py`, `ttf_large.py` |
| `korean-ttf-gapyeong-{24,28,34,36}.json` | 가평물결·가평한석봉 L/R/B | `ttf_gapyeong.py` |

형식(모두 같음): `key, label, origin, style, h, w, ascent, bpp, chars, data[, adv, ascii_ascent]`. `data`는
셀을 이어 붙인 base64(행 우선, 1bpp는 MSB가 왼쪽, 2bpp는 픽셀당 2비트, 0=투명, 3=가득). TTF는 이 프로젝트의
`mkfont.py` 렌더러로 굽고, 2,350자가 셀에 다 들어갈 때까지 크기를 줄인다(라벨의 px과 실제 렌더 크기가 1–2 px
다를 수 있다). 폰트 파일은 `~/work/scummvm/fonts/`(gmarket, mabinogi, youth, diphylleia, grandiflora,
cts2410, terrarum).

### 3.3 크기 고르기: 눈으로 확인

`vision/size_probe.py GAME ID OUT SIZES FACE...`가 원래 폰트 ×2 아래에 같은 한글 줄을 여러 크기로 한 기준선에
그리고 잉크 높이를 잰다. Camelot 99(줄 14, 대문자 13 px → 26)는 28 px 한글(잉크 27)이 맞았고 34–36은 컸다.
`vision/render_card.py`는 페이지의 카드를 같은 규칙으로 PNG로 그린다(기준선 빨간 줄).

## 4. 선택에서 게임까지: `pairing2svf.py`

```
python3 tools/korean/pairing2svf.py 선택.json --data <korean*.json 폴더> --out <출력>
```

- **SVF.** 고른 폰트마다 SVFN v2(코드 포인트 표) 한 개, 이름은 `KP` + 키의 SHA-1 앞 6자리. 셀·bpp·기준선·
  폭은 그대로. U+25A1이 없으면 상자를 그려 넣는다(맵의 `missing=`).
- **맵.** 그 게임의 지금 맵을 틀로 쓴다(SCI·AGI: 하네스 `HIRES*.MAP`, SCUMM: 엔진 `*KO.MAP`).
  `[fonts]`를 새 폰트로, 각 폰트 id 구역(`[font.N]`, SCUMM `csN`→`[font.N]`, AGI `[font]`, MM/Zak `[font.0]`)에
  고른 폰트를 넣는다. 렌더 대상별 구역(`[fonts:clut8]` 등)은 버리고, 2bpp가 있으면 `blend=auto`.
- **대체 사슬.** `face=kpN,<예전 폰트>,cjk` — 고른 폰트에 없는 기호는 예전 폰트(폰트 팩 SVF, KS X 1001 기호
  전부)가, 그것도 없으면 바이너리의 `fonts-cjk.dat` Noto Sans KR(FreeType 빌드만)이 그린다.
- **꾸밈 크기.** SCUMM charset마다 `[shadow.N]`을 쓴다. 배수 m = 한글 높이 / 원래 줄 높이(2×면 2).
  외곽선 `width = 원래 폭 × m`(0.25 단위; 원본 외곽선은 8방향 1 게임 px 그대로라 엔진 기본값의 0.75를 곱하지 않음), 그림자(외곽선+그림자의 사본도) `offset = dx,dy`
  (원래 이동 × m, 두 축 모두), 채움 그림자는 `fill=on`. `original-decoration.json`에 있는 폰트는 `mode=`도 원본대로,
  없는 폰트는 `mode=game`(패치 바이트)에 원래 1 게임 px의 크기(`width = 0.75 × m`, `offset = m`).
  엔진 기본값은 렌더 배율만 따라(2×에서 외곽선 1.5 px, 그림자 2 px) 줄의 2배보다 큰 폰트에서 작다. 페이지의 "원본 비례"와 같은 식.
- 출력 `<out>/<게임>/`에 맵과 SVF, `<out>/MANIFEST.md`에 대응표. 게임에서는 `hires_text_map=HIRESKP.MAP`
  (SCUMM은 `M1KP.MAP` 등).

## 5. 꾸밈: 측정과 엔진

원래 2bpp SCUMM 폰트의 꾸밈은 게임의 charset 리소스(v4 `90N.LFL`, v5/v6 `CHAR` 블록)에서 직접 읽는다:
`tools/korean/scumm-fonts/decoration.py`가 색 1(글자)과 색 2(꾸밈)를 그림자 모델(이동 dx,dy)과 방향별
외곽선 모델(왼·위·오른·아래 두께)에 맞춰 보고, 가장 잘 맞는 것을 `original-decoration.json`에 쓴다
(`--write`; `--check`는 JSON과 리소스가 다르면 1로 끝남). 페이지와 `pairing2svf.py`는 이 JSON만 읽는다.

| 폰트 | 줄 | 원래 꾸밈(게임 px) |
|---|---|---|
| MI1/MI2/Loom cs2·cs4, MI2 cs8, Indy4 cs2, DOTT cs2 | 8–15 | 외곽선 1 |
| MI2 cs7, Indy4 cs3·cs4 | 12–15 | 외곽선 1 + 그림자 (-1,+1) = 엔진의 stroke |
| MI cs6, DOTT cs3 | 12–14 | 그림자 (1,1) |
| DOTT cs5 | 12 | 채움 그림자 (1,1): 오른쪽·아래·모서리 |
| DOTT cs6 | 28 | 그림자 (2,2) |
| 1bpp 폰트 전부 (MI·Loom cs1·cs3, MI2 cs5, Indy4 cs1·cs5, DOTT cs1·cs4·cs7) | – | 없음 (classic 렌더러는 영문을 그림자 없이 그림) |

한글 패치의 꾸밈 바이트(`mode=game`)는 원본과 다른 곳이 있다: DOTT cs3은 외곽선, cs5는 없음, cs6은 항목
없음(없음), 1bpp인 cs4는 외곽선. 그래서 `[shadow.N]`은 `mode=`를 원본대로 적는다.

엔진은 맵의 `[shadow]` 하나로 모든 charset을 같게 그렸다(2× 기준 외곽선 1.5 px, 그림자는 반 게임 px인 1 px — 지금은 1 게임 px인 2 px). 이 브랜치에서
`[shadow.N]`(charset N 전용, 키마다 `[shadow]`로 대체)이 들어갔다 — `engines/scumm/HIRES_TEXT_DECORATIONS.md`
"Per charset: [shadow.N]".

## 6. 한계와 남은 일

- 한글 후보는 ASCII, 완성형 2,350자, `…“”‘’·「」`만 담는다. 조합형으로 11,172자를 다 쓰려면 엔진에 조합형
  글리프 원천이 있어야 한다(조사 문서 8장).
- 크기 필터는 줄 높이 기준이라, 줄보다 잉크가 큰 장식 폰트(Camelot 99)는 맞는 크기가 범위 가장자리에 온다.
- `pairing2svf.py`는 고르지 않은 폰트 id에 첫 선택 폰트를 기본으로 준다. 페이지의 "전체 게임 복사"는 모든 id를
  채우므로 문제가 없지만, 손으로 일부만 적은 선택이면 큰 제목 폰트가 다른 charset에도 쓰일 수 있다.
- LB2·SQ4는 CD/플로피 두 판이 같은 틀 맵을 쓴다. id 배치가 다르면 맵을 손봐야 한다.
- 크기·서체 판단은 모두 이 페이지에서 눈으로 하고, 실제 게임 화면 확인은 고른 뒤에 한다.
