# 한글 비트맵 폰트 파일 형식 정리 (형식 계열별)

작성: 2026-10-14. 대상: `~/work/johab-fonts` 수집본(연구용, 배포 안 함).
짝 문서: [조합형 코드와 조합형 비트맵 폰트 조사](2026-10-14-johab-font-research.md)(이하 "조사 문서"). 벌 선택표, 조합형 코드, 검증 기록(V1–V19)은 그 문서를 따른다.

- 디코더: `~/work/johab-fonts/tools/kfont.py`(라이브러리 + CLI). 표본 생성기: `tools/kfont_samples.py`.
  Hanme와 Dokkaebi 선택표는 같은 폴더의 `hanme_fnt.py`, `dkb_fnt.py`를 그대로 불러 쓴다.
- 표본 그림: `~/work/johab-fonts/samples/<area>/<name>.png`, 목록은 `samples/README.md`, 기계용 목록은 `samples/manifest.tsv`,
  렌더하지 못한 파일과 이유는 `samples/skipped.tsv`.
- 모든 폰트 파일은 바이트로만 읽었다. 수집본 안의 프로그램은 실행하지 않았다(실행 파일 안의 데이터 블록을 읽은 경우는 따로 적었다).
- **검증**: "렌더 확인"은 해당 디코더로 표본 문장을 그려 눈으로 확인했다는 뜻이다. 렌더로 확인하지 못한 항목은 **UNVERIFIED**로 표시한다.

---

## 0. 요약 표

검출 규칙의 "크기"는 파일 전체 바이트 수다. 크기만으로 정하는 형식은 `kfont.py`가 추가로 내용 검사(벌의 0번 채움 슬롯이 비었는지, KS A1A1이 공백인지 등)를 한다.

| 계열 | 형식 | 글리프 크기 | 개수 / 배치 | 검출 규칙 | 디코드 | 표본 예 |
|---|---|---|---|---|---|---|
| 1 벌 | raw 8×4×4 | 16×16 | 360 = 160+88+112 | 11,520 B, 벌 0번 빈칸 | 예 (`open_raw844`) | `dos/font-kor_SAN.KOR.png` |
| 1 벌 | raw 8×4×4 + 2 B 헤더 (T-Hangul) | 16×16 | 360 | 11,522 B | 예 | `dos/T_HANGUL_TBG1.FNT.png` |
| 1 벌 | raw 8×4×4 큰 크기 | 24/40/48 | 360 | 25,920 / 72,000 / 103,680 B | 예 | `dos/tb20_TB24G.FNT.png`, `dos/FNT_GHANC40.PFT.png`, `dos/FNT_GHANC48.PFT.png` |
| 1 벌 | Dokkaebi DKB2 | 16×16 | 360 + 특수 127 | 15,584 B | 예 (`open_dkb2`) | `dos/user-dkbb_DKBBG.FNT.png` |
| 1 벌 | 10×4×4 (san) | 16×16 | 400 = 200+88+112 | 12,800 B | 예 (`open_1044_san`) | `dos/font-kor_HANPL.KOR.png` |
| 1 벌 | 6×2×1 (Apple II) | 16×16 (바이트 열 순서) | 192 = 120+44+28 | 6,144 B | 예 (`open_621`) | `dos/src_han_hlink.fnt.png` |
| 1 벌 | Kyra MK15.BIT | 15×15 (행 2 B) | 385 = 191+85+109, 자모 우선 | 11,550 B | 예 (`open_kyra_mk15`) | `games/LOK1K_200823_MK15.BIT.png` |
| 1 벌 | Dokkaebi 프린터 7×2×2 | 16, 24 (열 우선) | 240 × 2벌체 | `HANGUL*` 15,360 / 34,560 B | 예 (`open_dkb_printer`) | `dos/Prt09_x_HANGUL16.FNT.png` |
| 1 벌 | Dokkaebi 프린터 12×4×4 | 40, 48 | 440 × 2벌체 | `HANGUL*` 176,000 / 253,440 B | 예 | `dos/Prt48_x_HANGUL48.FNT.png` |
| 1 벌 | Hanme HANME.FNT 5×2×3 | 16×16 | 218 조각 × 5폰트 + 영문 7 | 매직 `Hanme Hangul font file` | 예 (`open_hanme_fnt`) | `dos/hh31_HANME.FNT.png` |
| 1 벌 | hanterm `johab844-1` BDF | 16×16 | 360, ENCODING = 글리프 번호 | BDF, registry `johab844` | 예 (`open_bdf`) | `x11-unix/8-4-4_hiyggd2.bdf.png` 등 83장 |
| 1 벌 | hanterm `johab-1` BDF | 16×16 | 529, 10×(3\|4)×4 | BDF, registry `johab*` | 예 | `x11-unix/10-6-6_johabg16.bdf.png` |
| 2 완성형 | ScummVM `korean.fnt` | 헤더의 w×h | 2,350 (+빈칸 180) | 이름 `korean.fnt` + 크기 | 예 (`open_korean_fnt`, 자체 왕복 검사) | – (수집본에 없음) |
| 2 완성형 | HWP 3.0 FNT | 16/24/40/96 | 2,350 (+16 px는 8×4×4 360) | 매직 `FNT\x01`, 72 B 헤더 | 예 (`open_hwp3_fnt`) | `dos/FONT_HAN16_0.FNT.png` |
| 2 완성형 | HBIOS 24×30 (`$KMYZN24`, 태백 `*24.FNT`) | 24×24 (셀 24×30) | 360 자모 + 2,350 | 243,900 B | 예 (`open_hbios24`) | `os/PCDOS-HBIOS__KMYZN24.FNT.png` |
| 2 완성형 | 태백 3.5 PFT | 24/40/48 | 2,350 | 169,200 / 470,000 / 676,800 B | 예 (`open_ks_array`) | `dos/FNT_GHAN24.PFT.png` |
| 2 완성형 | 96MT.BIT (문화부 바탕) | 96×96 | 2,350 | 2,707,200 B | 예 | `printer/munhwa_96MT.BIT.png` |
| 2 완성형 | Win 3.1K/95K TBM | 12/16/20 | ASCII 128 + 기호 1,128 + 2,350 + 한자 4,888, 반전 | 매직 `TTBITMAP VARIABLE-PITCH HANGEUL` | 예 (`open_tbm`) | `os/win31ko_tbm_BT16.TBM.png` |
| 2 완성형 | KOEI .16P | 16×14 | 코드 레코드 30 B | 확장자 `.16P`/`.16`, 크기 % 30 = 0 | 예 (`open_koei_16p`) | `games/daehanghae2_ALL_FONT.16P.png` |
| 2 완성형 | OS/2 `OS2DBCS.FNT` (부팅) | 16×16 | 2,390 (빈칸 40) | 76,480 B | 예 (`open_os2dbcs`) | `os/DISK_0_OS2DBCS.FNT.png` |
| 2 완성형 | OS/2 `$korZN16/24.FNT` "System DBCS" | 16×16, 24×24 | 기호 1,128 + 2,350 + 한자 4,888, 허프만·문맥 예측 압축 레코드 | 매직 `System DBCS` | 예 (`open_os2_dbcs`) | `os/os2warp4_ko_ftcomp_D28REQ.002.OS2_SYSDATA__korZN24.FNT.png` |
| – | OS/2 `$SYS1Z16/24.FNT` | ? | 비KS 조각 658/653 | 이름 `SYS1Z` | 아니오 | – |
| 2 완성형 | zhcon `ksc-16.bpsf` | 16×16 | 94×93 행 | 이름 `ksc*.bpsf` | 예 (`open_bpsf`) | `x11-unix/font_ksc-16.bpsf.png` |
| 2 완성형 | CCIC HBF `Hangul16/24.bin` | 16, 24 | 151 + 2,350 | 80,032 / 180,072 B | 예 (`open_hbf_bin_hangul`) | `printer/Hangul16.bin.png` |
| 2 기호·한자 | HFONT.SYS (MS-DOS 6.22K, Win95K) | 16×16 | 48 B 헤더 + 기호 1,128 + 한자 4,888 | 192,560 / 206,276 B | 예 | `dos/dos612_expanded_HFONT.SYS.png` |
| 2 기호·한자 | HBIOS `$KSPHJ16/24`, 태백 `SPHJ*`, `SPECIAL.FNT` | 16, 24 | 기호 1,128 또는 1,316 + 한자 | 192,512 / 541,440 / 198,528 / 558,360 B | 예 | `dos/tb20_SPECIAL.FNT.png`, `dos/SPHJ24.FNT.png` |
| 2 기호·한자 | 한자 4,888 raw (Dokkaebi, 태백, Iyagi) | 16/24/40/48 | 4,888 | 156,416 / 351,936 / 977,600 / 1,407,744 B | 예 | `dos/Prt24_x_HANJA24.FNT.png` |
| 2 기호·한자 | 태백 SPC PFT | 24/40/48 | 기호 1,316 | 94,752 / 263,200 / 379,008 B | 예 | `dos/FNT_SPC24.PFT.png` |
| 2 기호·한자 | HWP3 `HAJ16_0.FNT`, Hanme `HANJA.FNT`, HWP 1.x `HANJA.SFT` | 16×16 | 4,888 (+기호) | 매직 | 예 | `dos/FONT_HAJ16_0.FNT.png`, `dos/hh31_HANJA.FNT.png` |
| 3 BDF/PCF | BDF, PCF (`ksc5601.1987-0`, `ksx1001.*`, `iso10646-1`, `johab*`) | 8–24 | 문자별 | `STARTFONT`, `\x01fcp` | 예 (`open_bdf`, `open_pcf`) | `x11-unix/baekmuk-2.0_gulim16.bdf.png` 등 |
| 3 HBF | HBF 헤더 + .bin | 16, 24 | 코드 범위별 | `HBF_START_FONT` | 일부 (고정폭만) | `printer/12x24.hbf.png` |
| 4 영문 | raw 8×16 × 256 / 128 / 96, 8×14 × 128 | 8×16 | 256 / 128 / 96 | 4,096 / 2,048 / 1,536 / 1,792 B + 글자 모양 검사 | 예 (`open_eng`) | `dos/font-eng_SAN.ENG.png` |
| 4 영문 | HBIOS 12×24 (셀 16×30), 태백 `*ASCII*.PFT`, HWP3 `ENG*.FNT`, TBM ASCII, Hanme `E`, DKBB.COM 데이터 | 6–24 | 128–269 | 크기/매직 | 예 | `os/PCDOS-HBIOS__KROHN24.FNT.png`, `dos/FONT_ENG16_0.FNT.png` |
| – | HWP 2.0 HFT, HWP 3.0 "Han Unified Font File" HFT, VFT | ? | ? | 매직 | 아니오 | – |
| 1 벌 | HWP 1.20 `HANGUL.SFT` / `HANGULn.PFT` | 16×16 / 24×24 (PFT 열 우선) | 3서체 × 31/29/31조각 벌: SFT 10/4/4·7/2/2·6/2/2 = 1,193, PFT 7/2/3·7/2/2·5/2/2 = 980 | 매직 `HWP Font Editor v1.0`(또는 `FE\x1a`) + 개수 | 예 (`open_hwp1_sft`, `hwp1_fnt.py`) | `printer/HANGUL.SFT.png`, `printer/HANGUL1.PFT.png` |
| 4 영문 | HWP 1.20 `ASCII.SFT/PFT` | 8×16 / 14×24 | 109 + 6서체 × 147 | 같음, 개수 991 | 예 | `printer/ASCII.SFT.sheet.png` |
| – | HWP 1.20 `SPECIAL1/2`, `JAPAN`, `USER1/2`, `SYSTEM` | 16×16, 8×16, 24×24, 14×24 | 95자 단위 세트 | 같음 | 글리프는 읽지만 Unicode 매핑 없음(HWP 1.x 고유 코드) | `printer/*.sheet.png` |
| 1 벌 | Mac HangulTalk 1.0 `fbit` (Gotic12, Munjo12/16/24) | 12, 16, 24 | 45개 조각 세트, 68k `fdef` 표로 선택 | `.rsrc`에 `fbit`+`fdef` 리소스 | 예 (`open_mac`, `mac_kfont.py`) | `mac/kh10_Munjo16.png` |
| 2 완성형 | Mac 7.5.3–8.6 FFIL `sfnt` + `bloc`/`bdat` 1비트 스트라이크 | 9–24 ppem | 2,350 + 한자 4,888 + 기호 (Mac 한국어 코드) | `.rsrc`에 `bloc`이 있는 `sfnt` | 예 (`open_mac`) | `mac/kh753_AppleMyungjo.png` |
| – | Mac 7.1.x `Seoul12` fbit(세트 표 없음), 'JM' 압축 `splt` | 12, 24 | ? | – | 아니오 | `mac/kh712_Seoul12_fbit_raw_glyphs_index24.png`(원시 글리프) |
| – | GSVIP GSF, Makkoya FNT/CSF, Hanme HFNT, Family Production FX4/5, KTH | ? | ? | 매직 | 아니오 | – |

표본은 모두 491장이다(이 생성기가 그린 450장 + `hwp1_fnt.py`가 그린 21장 + `mac_kfont.py`가 그린 20장)(`samples/README.md`). 계열별 수는 `samples/PROGRESS.md`에 있다.

---

## 1. 공통 사항

### 1.1 비트 배치
- 특별한 말이 없으면 1bpp, **행 우선**, 한 행은 `ceil(w/8)` 바이트, 바이트의 MSB가 왼쪽 픽셀이다. 글리프 크기 = `ceil(w/8) × h`. (`kfont.g_rowmajor`)
- 예외
  - Apple II 6×2×1(iolo): **바이트 열 순서**. 왼쪽 바이트 열 16 B, 이어서 오른쪽 바이트 열 16 B. (`g_bytecols`, 근거: iolo `fnt2bdf.py`의 `glyph_data[offset + x*glyph_height + y]`)
  - Dokkaebi `HANGUL16/24.FNT`: **열 우선**, 한 열이 `ceil(px/8)` 바이트, MSB가 위쪽. (`g_bitcols`, 근거: `notes/hanme-dokkaebi.md` 3절, DP09/DP24.COM)
  - Win TBM: 비트가 **반전**되어 저장된다(`255 - x`).
  - HBIOS 24 계열: 셀 24×30(또는 16×30), 잉크는 3–26행. 24×24로 잘라 쓴다.
- Kyra MK15.BIT는 15×15 글리프를 한 행 2 B(15비트 사용)로 담는다. 행 사이에 비트를 이어 붙이는 방식이 아니다(`renderGlyph`가 행마다 마스크를 0으로 초기화한다, `engines/kyra/graphics/screen_lok.cpp`). 처음에 비트 연속 해석도 시험했으나 글자가 깨졌다.

### 1.2 자모 번호
- `kfont.lvt(cp)`는 Unicode 음절에서 L(0–18), V(0–20), T(0–27)를 구한다. 선택표는 모두 이 번호로 적는다. V 순서: ㅏ ㅐ ㅑ ㅒ ㅓ ㅔ ㅕ ㅖ ㅗ ㅘ ㅙ ㅚ ㅛ ㅜ ㅝ ㅞ ㅟ ㅠ ㅡ ㅢ ㅣ.
- 조합형 5비트 필드 값이 필요한 표(hanterm, Dokkaebi 원본 ASM)는 `johab_fields(L,V,T)`로 바꾼다(조사 문서 1.3절 표).
- 벌 폰트의 음절 = 선택된 조각 2–3개의 OR. 받침이 없으면 종성 조각을 건너뛴다.

### 1.3 완성형 색인
- `kfont.ks_rowcol(cp)`: Python `euc_kr` 코덱(KS X 1001)으로 Unicode → (행, 열), 1부터 센다.
- 블록과 첫 글리프 번호를 짝지어 색인한다(`ks_mapper`).
  - 기호 1–12행 = 1,128자(`SEG_SYM`). 태백 계열은 1–14행 = 1,316자(`SEG_SYM14`, 13–14행은 빈칸).
  - 한글 16–40행 = 2,350자(`SEG_HAN`), 색인 `(행-16)*94 + 열-1` = `(hi-0xB0)*94 + (lo-0xA1)`.
  - 한자 42–93행 = 4,888자(`SEG_HJ`).
- 완성형 폰트에 없는 음절(예: 뷁)은 그릴 수 없다. 표본 문장 "다람쥐 헌 쳇바퀴에 타고파", "ScummVM 한글 …"의 음절은 모두 KS X 1001 2,350자 안에 있다.

### 1.4 표본 그림의 구성
- 맨 위 세 줄(회색): 원본 경로와 크기, 디코더·셀·배치, ASCII에 쓴 영문 폰트.
- 이어서 1배와 2배(최근접) 렌더. 1행 "다람쥐 헌 쳇바퀴에 타고파", 2행 "ScummVM 한글 Johab <배치> 1994"(벌 폰트) 또는 "ScummVM 한글 KS X 1001 1994"(완성형). 한자 폰트는 "漢字 大韓民國 ①②③", 기호 폰트는 "①②③ ★☆○● ←→ ㉠㉡", 영문 폰트는 ASCII 두 줄.
- 한 파일에 완성형과 자모 벌이 함께 있으면(HWP3 HAN16, HBIOS 24) 두 구역으로 나눠 둘 다 그린다. Dokkaebi 프린터 폰트는 두 서체(face 0/1), Hanme HANME.FNT는 한글 폰트 5개를 모두 그린다.
- 없는 글리프는 회색 빈 상자로 그린다.
- 짝 영문 폰트가 없으면 **내장 8×16 대체 폰트**를 쓴다. HBIOS-SYS 1.046(quiple)의 ASCII 0x20–0x7E를 `kfont.py` 안에 16진수로 넣었다(OFL-1.1 / CC BY-SA 4.0, ASCII는 VileR의 IBM EGA 8×14를 바탕으로 한 것).
- 중복 제거: sha256이 같은 파일, 또는 표본 문장의 렌더 픽셀이 같은 파일은 그림을 따로 만들지 않고 README의 "same as" 칸에 적는다. 렌더 비교는 표본 문장(한글 줄과 ASCII 줄)에 한한 비교다.

---

## 2. 계열 1: 조합형 벌(jamo-set) 폰트

### 2.1 raw 8×4×4, 16×16 (11,520 B)
- **판별**: 헤더와 매직이 없다. 크기 11,520 B. `check_844_blanks`가 모든 벌의 0번 슬롯(글리프 0, 20, …, 140, 160, 182, 204, 226, 248, 276, 304, 332)이 비었는지, 초성 첫 벌이 대부분 채워졌는지 본다. 같은 크기의 게임 데이터(Super Beemger `STAGE*.MAP`, VanSlug `PAL.DAT`)는 이 검사로 걸러졌다.
- **배치**: 32 B 글리프 360개, set-major. 초성 8벌×20(0–159), 중성 4벌×22(160–247), 종성 4벌×28(248–359). 벌마다 0번은 채움(빈 글리프).
- **선택표**: 조사 문서 3.2절 표 그대로다(`sel_844`).
  ```
  V                     ㅏ ㅐ ㅑ ㅒ ㅓ ㅔ ㅕ ㅖ ㅗ ㅘ ㅙ ㅚ ㅛ ㅜ ㅝ ㅞ ㅟ ㅠ ㅡ ㅢ ㅣ
  초성 벌, 받침 없음     0  0  0  0  0  0  0  0  1  3  3  3  1  2  4  4  4  2  1  3  0
  초성 벌, 받침 있음     5  5  5  5  5  5  5  5  6  7  7  7  6  6  7  7  7  6  6  7  5
  종성 벌                0  2  0  2  1  2  1  2  3  0  2  1  3  3  1  2  1  3  3  1  1
  중성 벌 = (초성 ㄱ·ㅋ ? 0 : 1) + (받침 ? 2 : 0)
  glyph(초성) = 벌*20 + L+1;  glyph(중성) = 160 + 벌*22 + V+1;  glyph(종성) = 248 + 벌*28 + T
  ```
  IHNM(`sbh1616.fnt`)만 중성 ㄱ·ㅋ를 1/3벌로 쓴다(`sel_844_ihnm`, `open_ihnm844`). 실제 파일이 없어 **UNVERIFIED**.
- **짝 영문 폰트**: 같은 이름의 `.ENG`(san `font-eng/*.ENG`), iolo `asc_*.fnt`, 게임 폴더의 `ENGLISH.FNT`, HBIOS `$KROHN16.FNT`, 태백 `ROMAN16.FNT`. 없으면 내장 대체 폰트.
- **수집본의 파일** (16×16 표본 73장, 중복은 README 참고)
  - san-1.0 `font-kor/*.KOR` 55종(drypot/san-1.0, 김윤수 FONTS COLLECTION 계열), iolo `8x4x4-fonts/src/han_*.fnt` 11종(대부분 san 파일과 같은 글리프)
  - HWP 2.1 Pro `HMJ.FNT`(iolo `han_sans.fnt`와 sha256 같음), IBM PC DOS HBIOS `$KGOZN16.FNT`, `$KMYZN16.FNT`
  - 태백 2.0 `TBG1–5.FNT`, 태백 3.5 `GOTHIC16/MY16/PILGI16/SAM16/HUNMINH.FNT`(일부는 태백 2.0과 sha256 같음), 한메 3.1 `BE/HAN.FNT`
  - 게임: CRW `HANGUL.FNT`/`S_HAN.FNT`, CRW2 `HANGUL.FNT`, Darkside Story / 천하무적 `HANGUL.FNT`, Stillhunt `HAN.AYU`, Time Machine I `WSHGODIC/WSHMYONG.FNT`, Great Fighter `HANGUL.FNT`, 얼의 모험 `DEFAULT.HBF`, Zis `HANGUL.KOR`
- **디코더**: `open_raw844` — 렌더 확인(전체 표본).
- **변환**: `kfont.py to-korean-fnt FILE OUT`가 2,350자를 합성해 `korean.fnt`를 쓴다(6절). 11,172자 전부가 필요하면 SVFN/SVM으로 구워야 한다(조사 문서 8.2–8.3절).
- **표본**: `samples/dos/font-kor_*.KOR.png`, `samples/dos/src_han_*.fnt.png`, `samples/os/PCDOS-HBIOS__KGOZN16.FNT.png`, `samples/games/*HANGUL.FNT.png` 등.

### 2.2 raw 8×4×4 + 2 B 헤더 (11,522 B, 초기 태백 T-Hangul)
- `TBG1–4.FNT`(t-han-gul): 앞 2 B를 건너뛰면 2.1절과 같다. 2 B의 의미는 확인하지 않았다.
- `open_raw844`가 크기로 오프셋 2를 고른다. 렌더 확인. 표본 `samples/dos/T_HANGUL_TBG1.FNT.png` 외 3장.

### 2.3 raw 8×4×4 큰 크기 (24×24 25,920 B / 40×40 72,000 B / 48×48 103,680 B)
- 2.1절과 같은 배치이고 글리프만 크다(24: 행 3 B 72 B, 40: 행 5 B 200 B, 48: 행 6 B 288 B).
- 파일: 태백 2.0 `TB24G/M/P.FNT`, 태백 3.5 `GHANC24/MHANC24.PFT`(TB24G·M과 sha256 같음), `GHANC40/MHANC40.PFT`, `GHANC48/MHANC48.PFT`.
- 짝 영문: 태백 `GASCII24/40/48.PFT`, `MASCII*.PFT`(5절).
- 렌더 확인. 표본 `samples/dos/tb20_TB24G.FNT.png`, `samples/dos/FNT_GHANC40.PFT.png`, `samples/dos/FNT_MHANC48.PFT.png` 등 7장.

### 2.4 Dokkaebi DKB2 (15,584 B, `DKBBG.FNT`/`DKBBM.FNT`)
- 8×4×4 360글리프(11,520 B) + 특수 127글리프(4,064 B). 앞 11,520 B는 san `DKBGOTH.KOR`, iolo `han_dkby.fnt`와 같다(`notes/hanme-dokkaebi.md` 2절).
- 특수 127자는 조합형 lead 0xD4, trail 0x80–0xFF에 쓰인다(`glyph = trail & 0x7F`). 이 영역은 Unicode 대응이 없어 `kfont`는 매핑하지 않는다.
- 선택표는 DKBB.ASM의 `hdisp1–6`이고 8×4×4 표준과 같다(중성 ㄱ·ㅋ = 0/2).
- 짝 영문: `DKBB.COM` 파일 오프셋 19,662의 128×16 B 데이터 블록(`open_dkbb_com_english`, 실행하지 않고 바이트로만 읽음).
- 렌더 확인. 표본 `samples/dos/user-dkbb_DKBBG.FNT.png`, `user-dkbb_DKBBM.FNT.png`.

### 2.5 10×4×4 san (12,800 B)
- 초성 10벌×20(0–199), 중성 4벌×22(200–287), 종성 4벌×28(288–399). set-major, 0번은 채움.
- 초성 벌 = 8×4×4의 받침 없음 표 + (받침 ? 5 : 0). 중성·종성은 8×4×4와 같다(조사 문서 3.3절, san `HANDRV.C` `Draw10_4_4`). `sel_1044_san`.
- 파일: san `HANPL.KOR`, `HUNMIN.KOR`. 짝 영문 없음(내장 대체).
- 렌더 확인. 표본 `samples/dos/font-kor_HANPL.KOR.png`, `font-kor_HUNMIN.KOR.png`.

### 2.6 6×2×1 Apple II (6,144 B)
- 192글리프 = 초성 6벌×20 + 중성 2벌×22 + 종성 1벌×28, 각 블록 0번은 채움. 글리프는 **바이트 열 순서**(1.1절).
- 선택표(iolo `6x2x1-fonts/generate_hangul_syllables.py`, `sel_621`):
  ```
  V                     ㅏ ㅐ ㅑ ㅒ ㅓ ㅔ ㅕ ㅖ ㅗ ㅘ ㅙ ㅚ ㅛ ㅜ ㅝ ㅞ ㅟ ㅠ ㅡ ㅢ ㅣ
  초성 벌, 받침 없음     0  0  0  0  0  0  0  0  1  2  2  2  1  1  2  2  2  1  1  2  0
  초성 벌, 받침 있음     3  3  3  3  3  3  3  3  4  5  5  5  4  4  5  5  5  4  4  5  3
  중성 벌 = 받침 ? 1 : 0;   종성은 1벌
  glyph(초성) = 벌*20 + L+1;  glyph(중성) = 120 + 벌*22 + V+1;  glyph(종성) = 164 + T
  ```
- 짝 영문: iolo `build.sh`의 짝(hlink↔asc_hlink, hgkey/joong/mr128↔asc_mr128 …). `asc_*.fnt`는 128×16 B, 같은 바이트 열 순서(8 px 폭이라 행 우선과 같다).
- 파일: iolo `6x2x1-fonts/src/han_*.fnt` 8종. iolo `fed/samples/*_HAN.fnt`(5,888 B = 184글리프)는 채움 슬롯이 빠진 변형으로 보이나 확인하지 않았다(**UNVERIFIED**, 디코더 없음).
- 렌더 확인. 표본 `samples/dos/src_han_*.fnt.png` 8장.

### 2.7 Kyra LoK `MK15.BIT` (11,550 B)
- 15×15, 한 행 2 B(15비트 사용), 30 B/글리프, 385글리프. 블록 0 초성 191 = 1+19×10, 블록 1 중성 85 = 1+21×4, 블록 2 종성 109 = 1+27×4. **자모 우선(jamo-major)** 10×4×4.
- 선택(조사 문서 6.1절, `sel_kyra`)
  ```
  초성 = 1 + 10·L + 2·g(V) + (받침 ? 1 : 0)      g = 8×4×4 받침 없음 표(세로 0, ㅗㅛㅡ 1, ㅜㅠ 2, ㅘㅙㅚㅢ 3, ㅝㅞㅟ 4)
  중성 = 191 + 1 + 4·V + (ㄱ·ㅋ ? 0 : 2) + (받침 ? 1 : 0)
  종성 = 276 + 1 + 4·(T−1) + 8×4×4 종성 벌
  ```
- 판별: 크기 11,550 B. "30 B 헤더 + 11,520 B"로도 읽힐 수 있으므로 크기만으로 정하면 위험하다(조사 문서 8.2절).
- 짝 영문: 게임의 8×8 fat 폰트(수집본에 없음) → 내장 대체.
- 렌더 확인. 표본 `samples/games/LOK1K_200823_MK15.BIT.png`.

### 2.8 hanterm BDF: `johab844-1`과 `johab-1`
- 두 형식 모두 BDF이고 `ENCODING`이 **글리프 번호**다(문자 코드가 아님). 그래서 일반 BDF 리더로 읽은 뒤 벌 선택표로 합성한다(`BitmapCharFont`, registry로 구분).
- `johab844-1`(hanterm-font 3.1 `8-4-4/*.bdf` 133종): ENCODING 0–359, 2.1절과 같은 배치·선택표.
  상당수가 san `.KOR` 폰트를 옮긴 것이라 렌더 비교로 중복이 많이 걸렸다. 표본 83장(`samples/x11-unix/8-4-4_*.bdf.png`).
- `johab-1`, `johabs-1`, `johabsh-1`, `johabnz*-1`(`10-6-6/`, `10-6-6-nz/`, `hanterm304fonts`): 529글리프 10×(3|4)×4. 선택표는 hanterm-xf `hfont.c` `convert_3_to_johabfont()`를 그대로 옮겼다(`sel_hanterm`, 표 `HT_*`, 조사 문서 3.4절).
  ```
  초성 = f*10 + (받침 ? fcon_map2 : fcon_map1)[m] − 19
  중성 = vow_base[m] + (vow_kind[m] ? (ㄱ·ㅋ ? 0 : 1) + (받침 ? 2 : 0) : lcon_kind[l])
  종성 = l*4 + lcon_map[m] + (l < 18 ? 397 : 393)          (f, m, l = 조합형 5비트 필드 값)
  비한글(johabs*, johabsh*) = 0x211 + (행−1)*94 + (열−1)
  ```
  `johab-1` 폰트는 조각마다 DWIDTH가 0인 글리프가 있어, 합성 결과의 advance를 셀 폭으로 고정한다.
- 짝 영문: 폰트에 ASCII가 없다 → 내장 대체.
- 렌더 확인. 표본 `samples/x11-unix/10-6-6_johabg16.bdf.png` 외(`johabs*`, `johabsh*`는 한글 렌더가 같아 중복으로 묶임).
- 함정: hurss `DOSIyagiBoldface-16.bdf`, `DOSPilgi-16.bdf`는 XLFD와 `CHARSET_REGISTRY`가 `Johab-1`이지만 ENCODING은 **Unicode**다(34,302자, U+AC00–D7A3 포함). 처음에 hanterm 글리프 번호로 읽어 엉뚱한 글자가 나왔다. 지금은 `johab*` registry라도 U+AC00이 들어 있으면 Unicode로 읽는다.

### 2.9 Dokkaebi 5.1 프린터 폰트 `HANGUL16/24/40/48.FNT`
- 한 파일에 두 서체가 이어서 들어 있다(face 1은 240 또는 440번 글리프부터). face 0 = 명조, face 1 = 고딕으로 추정(`notes/hanme-dokkaebi.md` 3절, 증명 안 됨).
- 7×2×2(16, 24 px, **열 우선**): 초성 0, 중성 140, 종성 184.
  ```
  초성 벌, 받침 없음 [0]*8 + [1,3,3,3,1,2,3,3,3,2,1,3,0];  받침 있음 [4]*8 + [5,6,6,6,5,5,6,6,6,5,5,6,4]
  초성 = 벌*20 + L+1;  중성 = 140 + (받침 ? 22 : 0) + V+1;  종성 = 184 + 28*fset + T   (fset = V가 8..19이면 1, 아니면 0)
  ```
- 12×4×4(40, 48 px, 행 우선): 초성 0, 중성 240, 종성 328.
  ```
  class = [0,1,0,1,0,1,0,1, 2,4,5,4,2,3,4,5,4,3,2,4,0][V]
  초성 = (class + 6*받침)*20 + L+1;  중성 = 240 + (2*받침 + (ㄱ·ㅋ ? 0 : 1))*22 + V+1;  종성 = 328 + 종성벌*28 + T (8×4×4 종성 표)
  ```
  근거는 DP09/DP24/DP40/DP48.COM의 표와 산술(정적 디스어셈블). `kfont`는 `dkb_fnt.parts()`를 그대로 부른다(`sel_dkb`).
- 짝 영문 없음 → 내장 대체. 렌더 확인(두 서체 모두). 표본 `samples/dos/Prt09_x_HANGUL16.FNT.png`, `Prt24_x_HANGUL24.FNT.png`, `Prt40_x_HANGUL40.FNT.png`, `Prt48_x_HANGUL48.FNT.png`.

### 2.10 Hanme 3.x `HANME.FNT` (65,570 B, 5×2×3)
- **컨테이너**(little endian): `"Hanme Hangul font file\0" 0x1A`(0x20까지 0 채움), `u16 count`@0x20(=12), 0x22부터 44 B 항목 × count:
  `u8 kind('E'|'K') | char name[33](조합형) | u32 offset | u32 size | u16 glyph_count`. 폰트 데이터는 0x232부터 이어진다.
- `E` 7개: 8×16 영문, 269글리프 × 16 B(0–255 = CP437 비슷, 256–268 추가). `K` 5개: 본문체·고딕체·가는 본문체·가는 고딕체·샘물체, 218조각 × 32 B.
- 조각 배치(채움 슬롯 없음): 초성 5벌×19(0–94), 중성 2벌×21(95–136), 종성 3벌×27(137–217).
- 선택표(H.EXE 조합 루틴, image 0x87A–0x9FF, 표 DS:0x96CA / 0x9B9A / 0x9FFA에서 읽음; `hanme_fnt.py`의 `INIT_SET`, `FIN_SET`):
  ```
  V                     ㅏ ㅐ ㅑ ㅒ ㅓ ㅔ ㅕ ㅖ ㅗ ㅘ ㅙ ㅚ ㅛ ㅜ ㅝ ㅞ ㅟ ㅠ ㅡ ㅢ ㅣ
  초성 벌 (받침과 무관)  0  3  0  3  0  3  0  3  2  1  4  1  2  2  1  4  1  2  2  1  0
  종성 벌 (받침과 무관)  0  2  0  2  0  2  0  2  1  0  2  0  1  1  0  2  0  1  1  0  0
  중성 벌 = 받침 ? 1 : 0
  초성 = 벌*19 + L;  중성 = 95 + 21*받침 + V;  종성 = 137 + 27*벌 + T−1
  ```
  초성 벌이 받침 유무에 따라 바뀌지 않는다는 점이 8×4×4와 다르다. `kfont`는 `hanme_fnt.pieces()`를 부른다(`sel_hanme`).
- 짝 영문: 같은 파일의 `E` 0번(Default).
- 렌더 확인(5개 한글 폰트 모두). 표본 `samples/dos/hh31_HANME.FNT.png`(user-hanme311 사본은 sha256이 같다).

### 2.11 다른 파일 안의 자모 벌
- HWP 3.0 `HAN16_n.FNT` 글리프 2,350–2,709, HBIOS `$KMYZN24.FNT`/태백 `GOTHIC24/MY24/PILGI24.FNT` 글리프 0–359는 8×4×4다. `part='jamo'`로 연다(`open_hwp3_fnt`, `open_hbios24`). 표본 그림의 두 번째 구역이 이것이다(3.2, 3.3절).

### 2.12 벌 폰트이지만 디코드하지 못한 것
| 파일 | 크기 | 확인한 것 | 상태 |
|---|---|---|---|
| Iyagi `I.FNT`(LZEXE로 압축된 MZ) | 16,267 B | 사본을 `unlzexe`로 풀었다(53,772 B, 실행 안 함). 풀린 이미지 파일 오프셋 7,152부터 hanterm `iyagi16.bdf`(johab-1)의 글리프가 32 B 단위로 그대로 나온다(528개 중 430개 일치). 초성은 23칸 묶음의 set-major로 보인다(ㄱ=0, ㄲ=1 …, 다음 벌 +23) | 배치 일부만 확인, **UNVERIFIED**, 디코더 없음. 같은 글꼴은 `iyagi16.bdf`로 렌더됨 |
| Iyagi `IKS.FNT`, `ICON.FNT` | 19,228 / 7,260 B | 풀었으나 iyagi16 글리프가 나오지 않음 | 미해독 |
| Makkoya `KSSM.FNT`/`10X13.FNT`, `*.CSF` | 6,133 B 등 | `"MAKKOYA FNT\x1a"` 30 B 헤더, 10×13 비트 연속 17 B, 358–359글리프 | 미해독(8×4×4로 추정) |
| GSVIP `*.GSF` | 2–7 KB, 190 KB | `"GSVIP Font 0.87 H-H"`, 압축 | 미해독 |
| Hanme Drawing Tool `HFNT` `*.FNT` | 1.7–85 KB | 획(stroke) 데이터 | 비트맵 아님 |
| Dokkaebi `DKB24.FNT` | 698,184 B = 9,697×72 | 크기만 | 미해독 |
| T-Hangul `TB41–45.FNT` | 3,266 B | 크기만 | 미해독 |
| HBANNER `CAPSHAN.16`, Iyagi `HAN1.FON`, Stillhunt `SHUNT.HAN`, Great Fighter `DHJ.FNT` | 488 / 382 / 538 / 429 글리프 | 크기만 | 미해독 |
| iolo fed `*_HAN.fnt` | 5,888 B | 6×2×1에서 8글리프 적음 | 미해독 |

---

### 2.13 HWP 1.20 `HANGUL.SFT` / `HANGULn.PFT` (31/29/31조각 벌, 3서체)
해독: `notes/hwp1.md`, `tools/hwp1_fnt.py`(HWP.EXE를 바이트로 읽고 정적 해석한 결과. 실행하지 않음). `kfont.open_hwp1_sft(path, face=0..2)`가 감싼다.
- **컨테이너**: 32 B 헤더 `"HWP Font Editor v1.0" 1A 00 …`, BE16 글리프 수 @0x1C, (w−1, h−1) @0x1E, 이어서 글리프. `HPFT3.LZH`의 `HANGUL3.PFT`만 매직이 `"FE" 1A`다.
  - `*.SFT`(화면): 행 우선, MSB 왼쪽. `*.PFT`(프린터): **열 우선**(24핀 헤드 순서), 열마다 3 B, MSB 위쪽. 24×24 = 72 B, 14×24 = 42 B. 파일 크기 = 32 + 개수 × 글리프 크기로 정확히 맞는다.
- **배치**: 한 파일에 서체 3개가 이어진다(서체 번호 0/1/2; HWP.EXE 메뉴 순서로 명조/고딕/샘물로 추정, 증명 안 됨). 서체 = [초성 세트들][중성 세트들][종성 세트들], 세트 하나 = 초성 31 / 중성 29 / 종성 31조각(조각 0 = "없음"은 저장하지 않음, 조각 n → 세트 안 글리프 n−1). 옛한글 조각도 들어 있다.
  | 파일 | 서체 0 | 서체 1 | 서체 2 | 글리프 |
  |---|---|---|---|---|
  | `HANGUL.SFT` 16×16 | 10/4/4 = 550 | 7/2/2 = 337 | 6/2/2 = 306 | 1,193 |
  | `HANGUL1/2/3.PFT` 24×24 | 7/2/3 = 368 | 7/2/2 = 337 | 5/2/2 = 275 | 980 |
- **선택** (HWP.EXE 화면 합성 루틴 image 0xF268, 표는 DS 0x438A–0x4466, 세트 표 화면 DS 0x44A8 / 프린터 DS 0x4578 + 서체×0x44):
  ```
  l = L_IDX[초성 5비트]  v = V_IDX[중성 5비트]  t = T_IDX[종성 5비트]       # 조각 번호, 0 = 없음
  lc = L_CLS[l] (0 = ㄱ·ㅋ)   vc = V_CLS[v] (모음 모양 1..15)   tc = T_CLS[t] (0 = 받침 없음)
  초성 세트 = S_ini[tc][vc]   중성 세트 = S_vow[tc*2 + lc]   종성 세트 = S_fin[vc]
  초성 = l0 + 세트*31 + l−1;  중성 = v0 + 세트*29 + v−1;  종성 = t0 + 세트*31 + t−1      (OR 합성)
  ```
  표 값(`L_IDX`, `V_IDX`, `T_IDX`, `L_CLS`, `V_CLS`, `T_CLS`, `SEL['sft'|'pft']`, `SETS`)은 `hwp1_fnt.py`에 그대로 옮겨져 있다. 중성 세트도 ㄱ·ㅋ에서 갈린다(8×4×4와 같은 방향).
- **나머지 파일**(HWP 1.x 16비트 문자 코드로 주소를 정한다; `notes/hwp1.md`):
  - `ASCII.SFT/PFT` 991 = 앞 109 + 6서체 × 147(0x20부터). `kfont`는 `open_hwp1_sft`로 영문 서체를 연다(개수 991).
  - `HANJA.SFT` 4,888(코드 − 0x4000, KS 순서). 3.8절.
  - `JAPAN`(2서체 × 170), `SPECIAL1`(9세트 × 95, PFT 8세트), `SPECIAL2`(8 × 95, 반각), `USER1/2`, `SYSTEM.PFT`(39): Unicode 대응이 없어 원시 글리프 표만 그렸다(`printer/*.sheet.png`).
- **짝 영문**: 같은 폴더의 `ASCII.SFT`/`ASCII.PFT`.
- **LZH 변형**: `HANGUL.LZH`(HANGUL1/2/3.PFT, 1990년 9–12월, 디자인 다름), `HPFT3.LZH`(HANGUL3.PFT). deark로 풀었고(`lzh/`), 같은 표로 디코드된다.
- **검증**: 표본 문장, 가 각 꽉 의 한 뷁, `SAMPLE.HWP`(HWP 1.10 문서) 본문 40줄이 SFT 서체 0–2, PFT 서체 0–2에서 바르게 나온다(`hwp1_fnt.py`가 렌더). 서체 번호와 서체 이름의 대응은 **UNVERIFIED**.
- **변환**: `kfont.py to-korean-fnt HANGUL.SFT OUT`(서체 0)으로 2,350자를 합성할 수 있다.
- **표본**: `samples/printer/HANGUL.SFT.png`, `HANGUL1.PFT.png`, `HANGUL2.PFT.png`, `LZH_HANGUL.LZH_HANGUL1/2/3.PFT.png`, `LZH_HPFT3.LZH_HANGUL3.PFT.png`, `SAMPLE.HWP.text.png`, `*.sheet.png`(13장).

### 2.14 Mac HangulTalk 1.0 `fbit` 자모 평면 폰트 (System 4.1, 1988)
해독: `notes/mac.md`, `tools/mac_kfont.py`(68k `fdef` 코드의 표를 읽어 옮긴 것, 실행·에뮬레이션 안 함). `kfont.open_mac(path)`가 감싼다. 디스크 이미지는 machfs로 풀었고, 파일은 `<이름>.rsrc`(리소스 포크)로 저장되어 있다.
- **파일**: `Gotic12`, `Munjo12`, `Munjo16`, `Munjo24`(type `fbit`, creator `hfnt`). 리소스 `fbit`(글리프 세트), `fdef` 3(68k 합성 코드 2,398 B, 넷 모두 같음), `hfnt` 0.
- **`fbit` 배치**(big endian): `80 00 'fbit'` 헤더(0x54까지, 0x48 = 평면 수 3), 0x54 = 세트 표 크기 0x21C, **0x56부터 세트 45개 × 12 B**(u16 행 비트/4, u32 글리프 영역 오프셋, u16 높이, 0, 0), 0x272부터 글리프. 글리프는 cell×cell, 행 사이 패딩 없이 비트를 이어 붙인다(12 px 18 B, 16 px 32 B, 24 px 72 B).
- **세트**: 0–7 초성(받침 없음, 모음 모양 8가지, 19자), 8–9 초성 25자(19 + ㄴㄷㄸㄹㅌㅍ 변형 6), 10–17 초성(받침 있음), 18–20 세로 모음 중성(9/13/13), 21–25 가로 모음 중성(11/10×4), 26–40 겹모음 중성(7자 × 15위치), 41–44 종성(27자, 모음에 따라 4가지).
- **선택**: 입력은 조합형 코드. `fdef`의 표 A–R(오프셋 0x524–0x6F6)이 세트와 글리프 번호를 정한다. 요약: 초성 세트 = 받침 없으면 B[V]/2, 있으면 8 + (C[V]&0x3F)/2(+1 if D[T] bit7), 세로 모음 18 / 19 + D[T], 가로 모음 21 / 22 + J[L]/2, 겹모음 26 / 31 / 36 + O[L]/2, 종성 41 + (P[V]&0x3F)/2(+1 조건부), 글리프 R[T]. 세부는 `mac_kfont.py` `compose()`.
- **짝 영문**: System 파일의 Mac `FONT` 리소스(14220/14226/14232 = Munjo 12/18/24, 23052 = Gotic 12).
- **검증**: 24 px로 5개 초성 × 21모음 × 여러 받침, 표본 문장을 렌더해 확인. 표본 `samples/mac/kh10_Gotic12.png`, `kh10_Munjo12/16/24.png`, 세트 표 `kh10_Munjo12_fbit_sets_x3.png`, `kh10_Munjo16_fbit_sets_x2.png`.
- **변환**: `mac_kfont.py korean-fnt` 또는 `kfont.write_korean_fnt`로 2,350자를 합성한다(Munjo16 시험함).
- **미해독**: HangulTalk 7.1.x의 `Seoul12` `fbit`(8,760 B)는 세트 표가 없고(글리프 0x66부터 481 × 18 B), 합성 규칙이 압축된(`dcmp`) `FDEF`/`hglc` 리소스에 있어 풀지 못했다. 원시 글리프만 `samples/mac/kh712_Seoul12_fbit_raw_glyphs_index24.png`(**UNVERIFIED**).

## 3. 계열 2: 완성형(KS X 1001) 폰트

### 3.1 ScummVM `korean.fnt`
- 4 B 헤더 `[미사용, shadow, w, h]` + 글리프. 색인 `(hi−0xB0)*94 + (lo−0xA1)`(`graphics/korfont.cpp:425-461`). 로더는 `kFontNumChars = 2530`개 분량을 읽는다(`graphics/korfont.h:255`).
- 판별: 파일 이름 `korean.fnt`이고 크기가 `4 + ceil(w/8)*h*2350` 또는 `…*2530`.
- 수집본에는 없다. `write_korean_fnt`로 HWP `HAN16_2.FNT`를 korean.fnt로 쓴 뒤 `open_korean_fnt`로 다시 읽어 2,350자 모두 원본과 같음을 확인했다(왕복 검사). 실제 게임용 파일로는 확인하지 않았다(**UNVERIFIED**: 외부에서 만든 korean.fnt).
- 짝 영문: `english.fnt` = 4 B 헤더 `[?, ?, w, h]` + 256글리프(`write_english_fnt`).

### 3.2 HWP 3.0 DOS `HDR30/FONT/*.FNT`
- **헤더 72 B** (little endian)
  | 오프셋 | 크기 | 값 |
  |---|---|---|
  | 0x00 | 4 | `"FNT"` 0x01 |
  | 0x04 | 1 | type: 0 영문, 1 한글, 2 한자 |
  | 0x05 | 1 | 1 |
  | 0x06 | u16 | 글리프 폭 |
  | 0x08 | u16 | 글리프 높이 |
  | 0x0A, 0x0C, 0x0E | u16 ×3 | 벌 수 8, 4, 4 (한글 16/24 px만, 96MT와 한자·영문은 0) |
  | 0x10 | u16 | 글리프 수(HAN16/24 = 2,710, HAJ16 = 4,888, 96MT = 2,350, ENG16 = 147) |
  | 0x14 | 문자열 | 폰트 이름(조합형, NUL 끝). 예: "고딕체 16*16" |
  | 나머지 | | 미해독 |
- **본문**
  - `HAN16_0–4`(고딕, 명조, 샘물, 필기, 모양) 86,792 B = 72 + 2,710×32: 완성형 2,350자(KS 순서, 0 = 가) + 8×4×4 자모 360.
  - `HAN24_0/1` 169,272 B, `HAN40_0` 470,072 B, `96MT.FNT` 2,707,272 B: 완성형 2,350자만. HAN24의 count 필드는 2,710으로 적혀 있지만 실제는 2,350자다. 글리프 수는 `(파일 크기 − 72) / 글리프 크기`로 구한다.
  - `HAJ16_0` 156,488 B: 한자 4,888(KS 순서). `ENG16_0–5` 2,424 B: 8×16 영문 147글리프, 0번 = 0x20(렌더 확인). `ENG24_0` 12×24, `ENG40_0` 20×40.
- 짝 영문: 같은 번호의 `ENG16_n`, 24·40 px는 `ENG24_0`, `ENG40_0`.
- 디코더 `open_hwp3_fnt`(type별). 렌더 확인(완성형과 자모 합성 둘 다). 같은 파일의 완성형과 자모 합성은 디자인이 다르다(조사 문서 5절).
- 변환: 완성형 2,350자를 잘라 korean.fnt로 쓰면 된다(합성 불필요).
- 표본: `samples/dos/FONT_HAN16_0.FNT.png`–`_4`, `FONT_HAN24_0/1.FNT.png`, `FONT_HAN40_0.FNT.png`, `FONT_96MT.FNT.png`, `FONT_HAJ16_0.FNT.png`, `FONT_ENG*.png`. printer 영역의 같은 ISO 사본은 sha256 중복.

### 3.3 HBIOS 24×30 (`$KMYZN24.FNT`, 태백 3.5 `GOTHIC24/MY24/PILGI24.FNT`, 243,900 B)
- 헤더 없음. 2,710 × 90 B(셀 24×30, 한 행 3 B, 잉크 3–26행). 글리프 0–359 = 8×4×4 자모, 360–2,709 = 완성형 2,350자(360 = 가, 2,709 = 힝).
- **새로 확인한 것**: 태백 3.5의 `GOTHIC24.FNT`, `MY24.FNT`, `PILGI24.FNT`도 이 형식이다(`notes/dos.tsv`는 "3387.5×72, 8×4×4 아님"으로 적었으나 2,710×90으로 맞아떨어지고 렌더도 맞다). 태백 3.5와 IBM HBIOS는 같은 폰트 세트를 쓴다: 태백 `SPHJ16.FNT`·HBIOS `$KSPHJ16.FNT`·태백 2.0 `SPECIAL.FNT`, 태백 `SPHJ24.FNT`·`$KSPHJ24.FNT`, `ROMAN16`·`$KROHN16`, `SANS16`·`$KSAHN16`이 표본 렌더에서 같았다(바이트는 다름).
- 짝 영문: HBIOS `$KROHN24.FNT`, 태백 `ROMAN24.FNT`(256 × 60 B, 16×30 셀, 잉크 12×24, 5절).
- 디코더 `open_hbios24`(완성형) / `part='jamo'`. 렌더 확인. 표본 `samples/os/PCDOS-HBIOS__KMYZN24.FNT.png`, `samples/dos/GOTHIC24.FNT.png`, `MY24.FNT.png`, `PILGI24.FNT.png`.

### 3.4 태백 3.5 PFT (헤더 없는 KS 배열)과 96MT.BIT
- `GHAN24/MHAN24.PFT` 169,200 B = 2,350×72, `GHAN40/MHAN40.PFT` 470,000 B = 2,350×200, `GHAN48/MHAN48.PFT` 676,800 B = 2,350×288. 0번 = 가, KS 순서.
- `96MT.BIT`(kaist munhwa-fonts) 2,707,200 B = 2,350×1,152(96×96): HWP `96MT.FNT`의 본문과 바이트가 같다(`notes/printer.md`).
- 판별은 크기와 `check_ks_array`(가·각·한·글·힝이 비어 있지 않고 너무 진하지 않을 것). 짝 영문: `GASCII*`/`MASCII*.PFT`.
- 디코더 `open_ks_array`(`KS_ARRAY_BY_SIZE`). 렌더 확인. 표본 `samples/dos/FNT_GHAN24.PFT.png` 등 6장, `samples/printer/munhwa_96MT.BIT.png`.

### 3.5 Windows 3.1K / 95K `.TBM`
- 매직 `"TTBITMAP VARIABLE-PITCH HANGEUL"`(32 B). `u16 h`@0x20, `u16 w`@0x22, `u16 레코드 수`@0x24(BT16 = 67), 0x26부터 16 B 레코드. 첫 레코드의 `u32`가 데이터 시작 오프셋이다(BT16 0xEC98, BT20 0xF038, GULIM12/16 0xEE08). 둘째 레코드의 오프셋 = 첫 오프셋 + ASCII 블록 크기였다. 레코드의 나머지 필드는 미해독.
- 데이터(모든 바이트 반전): ASCII 128자(폭 w/2) + 기호 1,128 + 한글 2,350 + 한자 4,888. GULIM은 끝에 빈 글리프 1개가 더 있다.
- 파일: Win 3.1K `BT16.TBM`(16), `BT20.TBM`(20, 행 3 B), Win95K `GULIM16.TBM`, `GULIM12.TBM`(12, 행 2 B). `HGPART.TBM`은 코드 목록이라 폰트가 아니다.
- 짝 영문: 같은 파일의 ASCII 블록. 디코더 `open_tbm`. 렌더 확인. 표본 `samples/os/win31ko_tbm_BT16.TBM.png`, `BT20`, `MINI_GULIM12/16.TBM.png`.

### 3.6 KOEI `.16P` (대항해시대 2, 삼국지 IV/V 한글판)
- 레코드 30 B의 배열, 코드 순 정렬. 레코드 = KS 코드 2 B(**trail 먼저**: A1A2는 `a2 a1`) + 16×14 비트맵(14행 × 2 B).
- 처음에 lead 먼저로 찾았더니 "쳇"이 다른 글자로 나왔다(뒤집은 코드도 유효한 KS 코드인 경우). trail 먼저로 고친 뒤 렌더가 맞았다.
- `ALL_FONT.16P` 104,340 B = 3,478 레코드(한글 2,350 + 기호 1,128, 한자 없음). 삼국지 V `ALL_FONT.16`(SZDD를 푼 것)은 같은 형식, 다른 디자인. 삼국지 II `RTK2MAIN.16P`는 603자 부분집합이라 표본에 빈 상자가 많다.
- 짝 영문: `ANKFONT.DA(T)`(8×16, 0x20부터 96자), 대항해시대 2는 `HANKAKU.FNT`.
- 디코더 `open_koei_16p`. 렌더 확인. 표본 `samples/games/daehanghae2_ALL_FONT.16P.png`, `ALL_FONT.16_.d_ALL_FONT.16.png`, `sangoku2_ko_RTK2MAIN.16P.png`.

### 3.7 OS/2 Warp 4 한글판
OS/2 한글판 CD에는 부팅용 `OS2DBCS.FNT`(비압축)와 시스템 폰트 `$korZN16/24.FNT`(압축)가 있다. 형식 해독은 `notes/os2-dbcs.md`와 `tools/os2_dbcs_fnt.py`(다른 작업의 결과)이고, `kfont`는 그 디코더를 감싼다.

**(a) 부팅 폰트 `OS2IMAGE/DISK_0/OS2DBCS.FNT` (76,480 B)**
- 헤더 없음, 2,390 × 32 B. KS 순서 한글이고 1,880–1,919번 40칸이 비어 있다(층 = 1,879, 치 = 1,920, 힝 = 2,389). 즉 색인 1,880 이상에 40을 더한다.
- 디코더 `open_os2dbcs`. 렌더 확인(층·치·힝 개별 확인 포함). 표본 `samples/os/DISK_0_OS2DBCS.FNT.png`.

**(b) 시스템 폰트 `$korZN16.FNT` (220,160 B), `$korZN24.FNT` (346,624 B) — "System DBCS"**
- FTCOMP 묶음(`DISK_28/REQUIRED` 등)을 deark `os2pack2`로 푼 것(`os/extracted/os2warp4_ko_ftcomp/`). `D28REQ.004`와 `D10BUN.007`(ZN16)은 바이트가 같다.
- **헤더** (little endian)
  | 오프셋 | 내용 |
  |---|---|
  | 0x00 | 16 B ASCII `"System DBCS 16  "` / `"System DBCS 24  "` (판별 매직) |
  | 0x10 | u16 0x03B7(코드페이지 태그 951), u16 w, u16 h, u16 1, u16 글리프 바이트 수(0x20 / 0x48), u16 0, u16 1, 8 B 날짜("12/05/96" / "08/19/91") |
  | 0x80 | u16 범위 수(3) + lead 범위 쌍 A1–AC, B0–C8, CA–FD (trail은 늘 A1–FE) |
  | 0xA0 | u16 범위 수(3) + 글리프 번호 범위 256–1383(기호 1,128), 1666–4015(한글 2,350), 4110–8997(한자 4,888) |
  | 0xC0 | 256 B 허프만 표: 다음 8비트로 색인, 값 = (부호 길이<<4) \| 니블. 값 0은 탈출: 이어지는 3비트로 0x1C0의 8 B 표를 본다(길이 8 + 추가) |
  | 0x200 | 2,048 B 문맥 표(4,096 니블): (두 줄 위 니블, 한 줄 위 니블, 잔차 니블) → 픽셀 니블 |
  | 0xA00 | 4,096 B 목차((u16 오프셋, u16 순번) 쌍, 0xFFFF = 없음). 순차로 읽을 때는 필요 없다 |
  | 0x1A00 | 512 B 블록마다 레코드 `[u16 글리프 번호][u8 길이][페이로드]`; 다음 레코드가 블록에 안 들어가면 0xFF로 채우고 다음 블록으로 |
- **글리프 번호** = 256 + (lead−0xA1)·94 + (trail−0xA1), 즉 KS (행−1)·94 + (열−1) + 256. 가 = B0A1 = 1,666, 힝 = 4,015, 伽 = 4,110.
- **글리프 복원**: 페이로드에서 허프만으로 니블 2×(글리프 바이트 수)개를 읽어 잔차 바이트를 만든 뒤, 바이트마다 위 두 줄의 같은 자리 바이트와 함께 상·하위 니블을 문맥 표로 바꾼다(줄 바이트 수 rb = ceil(w/8)). 결과는 행 우선, MSB 왼쪽. 근거는 FNTCALLS.DLL `FNTGETIMAGE`의 정적 해석(`notes/os2-dbcs.md`).
- 레코드 8,224개 = 기호 1,128(미할당 KS 칸 142개는 없음) + 한글 2,350 + 한자 4,888. ZN16의 한글은 부팅 폰트 `OS2DBCS.FNT`와 2,350자 중 2,347자가 바이트까지 같다.
- 짝 영문: 같은 CD의 `_949HN16.FNT`(OS/2 GPI 형식)는 디코드하지 않았다 → 내장 대체.
- 디코더 `open_os2_dbcs`(`os2_dbcs_fnt.decode()`를 부름). 렌더 확인(한글 문장, 한자·기호 줄). 표본 `samples/os/os2warp4_ko_ftcomp_D10BUN.007.OS2_SYSDATA__KORZN16.FNT.png`, `samples/os/os2warp4_ko_ftcomp_D28REQ.002.OS2_SYSDATA__korZN24.FNT.png`.
- 변환: 2,350자를 KS 순서로 꺼내면 korean.fnt 본문이 된다(16×16, 24×24). `notes/os2-dbcs.md`의 작업이 만든 `os2warp4_ko_dbcs_decoded/zn16|zn24/korean*.fnt`가 그 결과다(표본 생성기는 `*_decoded` 폴더를 원본이 아니라서 건너뛴다).

**(c) `$SYS1Z16.FNT` (73,732 B), `$SYS1Z24.FNT` (90,628 B)** — 미해독
- "System DBCS" 헤더가 없다. 0x800부터 (u16 오프셋, u16 순번) 목차, 0x1800부터 512 B 블록 안에 비압축 레코드 `[u16 코드][u8 길이][데이터]`(16-dot 84 B, 24-dot 120 B). 코드 0xA793–0xAA1F(16-dot은 0x81CF–0x81D3도), 658 / 653개.
- 내용은 KS 음절이나 한자가 아닌 막대·자모 비슷한 조각(OS/2 "SYS1" 확장 영역으로 보임)이다. 84 B는 16×16의 배수가 아니라 셀 크기를 정하지 못했다. 변환 대상 아님. `kfont`는 이유를 적고 건너뛴다(`skipped.tsv`).

### 3.8 기호·한자 전용 폰트
모두 KS 순서 배열이고, 표본 문장은 "漢字 大韓民國 ①②③"(기호만 있으면 기호 줄)이다.
| 파일 | 크기 | 배치 | 디코더 | 표본 |
|---|---|---|---|---|
| MS-DOS 6.22K `HFONT.SYS` | 192,560 | 48 B 헤더(길이 바이트 + "큐닉스" 완성형, "4.00"@0x15) + 기호 1,128 + 한자 4,888 | `open_ks_array` | `dos/dos612_expanded_HFONT.SYS.png` |
| Win95K `hfont.sys` | 206,276 | 위와 같은 192,560 B + 13,716 B 코드 표 | 같음 | (렌더 중복) |
| HBIOS `$KSPHJ16.FNT`, 태백 2.0 `SPECIAL.FNT` | 192,512 | 기호 1,128 + 한자 4,888 | 같음 | `dos/tb20_SPECIAL.FNT.png` |
| 태백 3.5 `SPHJ16.FNT` | 198,528 | 기호 1,316(1–14행) + 한자 | 같음 | (렌더 중복) |
| HBIOS `$KSPHJ24.FNT` / 태백 `SPHJ24.FNT` | 541,440 / 558,360 | 24×30 셀, 기호 1,128 / 1,316 + 한자 | 같음 | `dos/SPHJ24.FNT.png` |
| Dokkaebi `HANJA16/24/40/48.FNT`, 태백 `HANJA24/40/48.PFT`, Iyagi `IHANJA.FNT` | 156,416 / 351,936 / 977,600 / 1,407,744 | 한자 4,888 | 같음 | `dos/Prt24_x_HANJA24.FNT.png`, `dos/FNT_HANJA48.PFT.png` 등 |
| 태백 `SPC24/40/48.PFT` | 94,752 / 263,200 / 379,008 | 기호 1,316(1–14행; ①과 ㉠의 위치로 확인) | 같음 | `dos/FNT_SPC24.PFT.png` 등 |
| HWP3 `HAJ16_0.FNT` | 156,488 | 72 B 헤더 + 한자 4,888 | `open_hwp3_fnt` | `dos/FONT_HAJ16_0.FNT.png` |
| Hanme `HANJA.FNT` | 196,736 | 0x80 헤더(섹션 오프셋 u32 ×3 @0x50) + 한자 4,888 + 기호 1,128 + 그림 128 | `open_hanme_hanja` | `dos/hh31_HANJA.FNT.png` |
| HWP 1.20 `HANJA.SFT` | 156,448 | 32 B 헤더 + 한자 4,888 (HWP 1.x 코드 − 0x4000) | `open_hwp1_sft` | (HAJ16과 렌더 중복) |
- 표본 렌더에서 Iyagi `IHANJA.FNT`와 HWP 1.20 `HANJA.SFT`는 HWP3 `HAJ16_0.FNT`와 같았다.
- Dokkaebi `SPEC16/24/40/48.FNT`(40,192 B = 1,256 × 32 등)는 순서를 확인하지 못해 디코더가 없다.
- 변환: korean.fnt는 한글만 담으므로 이 폰트들은 쓰이지 않는다. 기호·한자는 기존 대체 체인(조사 문서 8.2절)에 맡긴다.

### 3.9 그 밖의 완성형
- zhcon `ksc-16.bpsf`: 9 B 헤더 + 1행부터 94열 단위 32 B 글리프. 렌더 확인(한·①·漢). 표본 `samples/x11-unix/font_ksc-16.bpsf.png`.
- CCIC HBF `Hangul16.bin`/`Hangul24.bin`: 호환 자모 94 + 괄호 29 + 원 28 + 음절 2,350(Unicode 1.1의 U+3400–3D2D, KS 순서). 음절은 151번부터. `.hbf` 헤더 없이도 연다(`open_hbf_bin_hangul`). 렌더 확인. 표본 `samples/printer/Hangul16.bin.png`, `Hangul24.bin.png`(렌더 비교로 kaist `hanglm16/24`와 같은 글리프였다).

---

### 3.10 Mac 7.5.3 / 7.6 / 8.5 / 8.6 FFIL `sfnt` 비트맵 스트라이크 (`bloc`/`bdat`)
- FFIL 서체 가방의 리소스 포크: `FOND`, Latin `NFNT` 8개, `sfnt`(Elex Computer, 1994–1995). "Bitmap 서체 설치" 폴더의 사본은 **비트맵만 있는 sfnt**(`bdat bhed bloc cmap …`), Font Folder 사본은 `glyf/loca`를 더 갖지만 `bdat`는 같다.
- `bloc`(Apple, 0x00020000): 스트라이크 8개(9, 10, 12, 14, 16, 18, 20, 24 ppem; HeadLineA는 18/20/24). 스트라이크마다 색인 부표 2개: 글리프 0–231 = 1바이트 문자(색인 형식 1, `bdat` 이미지 형식 2, 비례폭 Latin), **232–9888 = 2바이트 문자(색인 형식 2 고정 크기, 이미지 형식 5 = 글리프별 메트릭 없음, ppem×ppem 비트를 패딩 없이 이어 붙임)**.
- 주소: `cmap` (1, 3) 형식 2(Mac 한국어 = EUC-KR 확장, 9,799 코드), 글리프 이름 `uni` + 2바이트 코드. KS X 1001 2,350자 전부 + 한자 4,888(GongJak, TaeGraphic, Script, HeadLineA에는 한자 없음) + 기호. `cmap` (1, 0)은 Mac Roman 1바이트.
- 서체: Seoul, AppleGothic, AppleMyungjo, #PC명조, #신명조, #궁서체, #중고딕, #공작체, #태그래픽, #필기체, #헤드라인A(7.5.3; 7.6.1·7.5.3 rev 2.2는 같음), 8.5에서 Seoul의 `bdat`가 바뀌고 한강체(Hangang)가 더해짐.
- `mac_kfont.SfntBitmapFont`(ppem 선택). fontTools는 `cmap`/글리프 이름 조회에만 쓰고(`tools/.venv/lib`), `bloc`/`bdat`는 직접 파싱한다.
- 검증: 비트맵 전용 서체 11종 × 모든 스트라이크에서 KS 2,350자가 모두 있고 비어 있지 않음. 표본 `samples/mac/kh753_*.png`(11장, 12/16/24 px), `kh851_Hangang.png`, `kh851_Seoul.png`.
- 변환: `mac_kfont.py korean-fnt FORK.rsrc OUT --ppem 16`(셀 모드, 줄 높이 여백 제거). AppleMyungjo 12/16/24에서 2,350자 시험함.
- **미해독**: 7.1.2의 12/24 px 비트맵(`AppleGothic12/24`, `AppleMyungjo12/24`, `KSSymbol12/24`, `Hanja24`, `GHanja24`, `GSymbol24`, `Seoul12`)은 Installer `splt` 파일이고 `'JM'` 압축(20 B 헤더 뒤 엔트로피 7.87 bit/B 스트림, zlib 아님)이라 풀지 못했다. 7.5.1 설치 디스크의 `ircp`/`idcp` 압축 토움, Language Kit 패키지도 풀지 못했다. 7.1.2의 AppleGothic/AppleMyungjo는 외곽선(TrueType)만 있다.

## 4. 계열 3: BDF / PCF / HBF

- **BDF**(`open_bdf`): `FONTBOUNDINGBOX`, `FONT_ASCENT/DESCENT`, `CHARSET_REGISTRY-ENCODING`(없으면 XLFD 끝 두 필드), 문자별 `ENCODING`, `DWIDTH`, `BBX`, `BITMAP`. 글리프는 ascent 기준선에 맞춰 셀로 옮긴다.
- **PCF**(`open_pcf`, gzip도 읽음): 목차, PROPERTIES, METRICS(압축/비압축), BITMAPS(패딩, 비트·바이트 순서, scan unit), BDF_ENCODINGS를 구현했다.
- **registry별 매핑**
  | registry | ENCODING의 뜻 | 처리 |
  |---|---|---|
  | `iso10646-1`, `unicode` | Unicode | 그대로 |
  | `ksc5601.1987-0`, `ksx1001.1997-0`, `ksx1001.1998-0` | KS GL 코드 0x2121–0x7D7E | (행+0x20)<<8 \| (열+0x20); 없으면 +0x8080(GR, `ksc5601.1987-1`) |
  | `johab844-1` | 8×4×4 글리프 번호 | 2.8절 |
  | `johab-1`, `johabs*`, `johabnz*` | hanterm 529 글리프 번호 | 2.8절 |
  | `iso8859-1` | Latin-1 | 영문 |
- **수집본**: Baekmuk 2.0/2.2(batang/dotum/gulim/hline 10–24 px, 굵게), 한양 `hgm/hmm 10–20.pcf`(x11과 printer에 사본), KAIST `newmj16/18`, `ks_iyagi16`, `ks_philgi16`, Mizi `mz*16h.pcf`, han-1.0fb `h16m`, Pinetree precomposed(비례폭 8–24), kaist Daewoo `hanglm16/24`, hurss DOS 폰트 BDF(11,172자, Unicode), HBIOS-SYS BDF, jfbterm PCF(Unicode), hanterm johab BDF.
  비례폭(Pinetree)은 DWIDTH대로 그렸다.
- **HBF**(`open_hbf`): `HBF_CODE_RANGE`(코드 범위, 파일, 오프셋)와 `HBF_BYTE_2_RANGE`를 읽는다. `.bin`이 같은 폴더에 없으면 `<area>/extracted/*/`에서 찾는다. 코드 체계가 "Unicode 1.1"이면 한글 음절은 KS 색인 + 0x3400으로 바꾼다.
  - 렌더 확인: `12x24.hbf`(ISO8859-1 영문), `unihan16.hbf`(Hangul16.bin 경유).
  - 못 함: `uni24.hbf`는 비트맵 상자 12×24와 폰트 상자 24가 섞인 혼합 폭이라 범위별 글리프 크기를 처리하지 못한다. `uni16m.hbf`는 `uni16m.bin`이 없다. `UNI24_UP.HBF`는 `FONTBOUNDINGBOX`가 없는 델타 BDF다. `hanja65.hbf`(65×65 한자, KSC5601-1987)는 코드 범위 두 개(0x2121–0x497E, 0x4A21–0x7D7E)가 모두 오프셋 0을 가리킨다. 앞 범위는 자리표시로 보고 버렸다(그러지 않으면 한글 코드가 한자 글리프를 돌려준다). 렌더 확인, 표본 `samples/printer/hanja65.hbf.png`.
- **변환**: wansung BDF/PCF는 `write_korean_fnt`로 korean.fnt를 만들 수 있다(16 px 고정폭만 의미가 있다). Unicode BDF는 11,172자를 담으므로 SVFN으로 굽는 편이 낫다.
- 표본: x11-unix 영역 195장 대부분, printer 영역의 `hanyang-font-pcf_*.pcf.png`, `DOS*-16.bdf.png`, `MiraeroNormal-24.bdf.png`.

---

## 5. 계열 4: 영문(반각) 폰트

| 형식 | 크기 | 배치 | 판별 | 파일 예 |
|---|---|---|---|---|
| raw 8×16 × 256 | 4,096 | 문자 코드 = 글리프 번호(CP437 비슷) | 크기 + 글자 모양 검사 | san `font-eng/*.ENG`, iolo `asc_*.fnt`, HBIOS `$KROHN16`/`$KSAHN16`, 태백 `ROMAN16/SANS16/ANTIQUE/…`, 게임 `ENGLISH.FNT`/`ENG.AYU`/`WSE*.FNT` |
| raw 8×16 × 128 | 2,048 | 0–127 | 같음 | san `SAN.ENG`, Iyagi `ENG.FNT`, iolo 6×2×1 `asc_*.fnt`(바이트 열 순서이지만 8 px라 같음) |
| raw 8×16 × 96 | 1,536 | 0x20–0x7F | 같음 | KOEI `ANKFONT.DA`, `HANKAKU.FNT`, 게임 `ENGLISH.FNT` 일부 |
| raw 8×14 × 128 | 1,792 | 0–127 | 같음 | (수집본에서 통과한 것 없음) |
| HBIOS 12×24 | 15,360 = 256×60 | 16×30 셀, 잉크 3–26행·0–11열 | 크기 | `$KROHN24.FNT`, 태백 `ROMAN24/SANS24/ITALIC24.FNT` |
| 태백 ASCII PFT | 12,288 / 30,720 / 49,152 | 256글리프, 24/40/48 px, 행 2/3/4 B, 폭 = px/2 | 크기 + `.PFT` | `GASCII24/40/48.PFT`, `MASCII*.PFT` |
| HWP3 `ENG*.FNT` | 2,424 등 | 72 B 헤더, 0x20부터 | 매직 | `ENG16_0–5`, `ENG24_0`, `ENG40_0` |
| TBM ASCII | – | TBM 데이터 앞 128자 | TBM | 3.5절 |
| Hanme `E` | 4,304 | 269 × 16 B | HANME.FNT | 2.10절 |
| DKBB.COM 데이터 | – | 오프셋 19,662, 128 × 16 B | 파일 지정 | 2.4절 |

- 글자 모양 검사(`check_ascii`): A·H·B·0이 있고 서로 다르며, 공백이 비어 있고, 잉크가 너무 적거나 많지 않을 것. 같은 크기의 TeX `.tfm`, OS/2 `.BIO`, iolo fed의 Apple II 폰트(공백 칸이 채워져 있음) 등이 이 검사로 걸러졌다.
- 디코더 `open_eng`. 렌더 확인. 표본 97장(대부분 `samples/dos/font-eng_*.ENG.png`).
- 변환: `write_english_fnt`가 ScummVM `english.fnt`(4 B 헤더 + 256글리프)를 쓴다. raw `.ENG`와 직접 호환되지 않는다(헤더가 다름).
- 짝이 없는 폰트는 내장 8×16(HBIOS-SYS ASCII)을 쓴다(1.4절).

---

## 6. 변환 메모

- **korean.fnt**(ScummVM `FontKoreanWansung`): `kfont.py to-korean-fnt FILE OUT [--jamo]`.
  - 완성형 원본(HWP3, HBIOS 24, TBM, KOEI, 태백 PFT, BDF/PCF wansung)은 KS 순서 2,350자를 그대로 옮긴다.
  - 벌 원본은 Unicode를 거쳐 2,350자를 합성한다. 합성 결과는 같은 회사의 완성형 디자인과 다르다(조사 문서 5절, V8).
  - 셀 크기는 원본 그대로 헤더에 적는다. 16×16이 아닌 크기(15×15 Kyra, 16×14 KOEI, 24×24)를 엔진이 어떻게 다루는지는 확인하지 않았다(**UNVERIFIED**).
  - 왕복 검사: HWP `HAN16_2` → korean.fnt → `open_korean_fnt`, 2,350자 불일치 0.
- **SVFN / SVM**: 11,172자 전부를 쓰려면 이쪽이다. `kfont`의 `JamoFont.glyph(cp)`가 U+AC00–D7A3 전부를 돌려주므로 굽는 도구는 이 API를 그대로 쓰면 된다. `tools/korean/mkfont.py`에 비트맵 입력 경로를 붙이는 일은 이 작업 범위 밖이다.
- **런타임 Johab glyph source**(조사 문서 8.2절)의 프로파일은 `kfont`의 `sel_*` 함수와 1:1로 대응한다: `844` = `sel_844`, `844-ihnm` = `sel_844_ihnm`, `1044-san` = `sel_1044_san`, `kyra-lok` = `sel_kyra`, `hanterm-johab` = `sel_hanterm`, `hbios24`/`hwp3-fnt` = 같은 `sel_844`에 오프셋·crop만 다르다. 이번에 6×2×1, Dokkaebi 7×2×2·12×4×4, Hanme 5×2×3, HWP 1.20 31/29/31조각 벌, Mac fbit 45세트가 더해졌다.

---

## 7. 렌더하지 못한 것과 이유

`samples/skipped.tsv`(814행, 같은 형식이 여러 사본으로 되풀이됨)를 형식별로 묶으면 다음과 같다.

| 형식 | 파일 수 | 이유 |
|---|---|---|
| Makkoya FNT/CSF (막고야 게임) | 381 | 10×13 비트 연속 자모 조각, 배치 미해독 |
| HWP 3.0 "Han Unified Font File 1.0" HFT | 326 | 형식 미해독(벡터일 가능성) |
| HWP 2.0 HFT | 43 | 형식 미해독 |
| GSVIP GSF | 21 | 압축, 미해독 |
| HWP 1.x 형식의 `HDR30.ICO` | 1 | 같은 컨테이너의 아이콘(120 × 26×26), 폰트 아님 |
| 영문 후보 탈락 | 33 | 같은 크기의 비폰트 파일, Apple II 8×8 폰트 등 |
| 8×4×4 후보 탈락 | 4 | 같은 크기의 게임 데이터 |
| HBF | 3 | 혼합 폭, bin 없음, 델타 파일 |
| OS/2 `$SYS1Z16/24` | 2 | 비KS 조각 표, 셀 크기 미해독 |
- Mac: 'JM' 압축 `splt`, `Seoul12` fbit 규칙, `ircp`/`idcp` 설치 토움(3.10절, 2.14절). mac 영역은 이 생성기가 훑지 않고 `mac_kfont.py`의 그림을 등록한다.
- 크기 규칙에 맞는 디코더가 없어 아예 후보가 되지 않은 것: 2.12절 표, Dokkaebi `SPEC*.FNT`, Family Production FX4/FX5, Super Beemger KTH, TeX PK(`han`, `han1`, HLaTeX, htex; PK 디코더 없음), 창세기전 ISZ.
- `kfont.py scan ROOT`는 수집본 전체(1,000 B–8 MB 파일)에 검출 규칙을 돌려 결과를 tsv로 낸다.

---

## 부록 A. kfont.py 요약

- 객체: `Glyph(w, h, rows, adv)`, `Font.glyph(cp)`, `TableFont`(번호 배열 + cp→번호), `JamoFont`(조각 배열 + 선택 함수), `BitmapCharFont`(BDF/PCF), `StackFont`(한글 폰트 → 짝 영문 → 내장 대체 순서).
- 열기: `open_font(path, part=None)`가 `sniff()`로 형식을 고른다. 형식을 알면 `open_*`를 직접 부른다.
- CLI: `info`, `text`(ASCII 아트), `png`, `sample`, `scan`, `to-korean-fnt`. 모두 `python3 -I`로 실행한다.
- 표본 재생성: `python3 -I tools/kfont_samples.py` (491장 목록, 약 20초. HWP 1.20과 Mac 그림은 각 디코더가 그린 것을 등록만 한다. `samples/`를 지우지 않고 덮어쓴다).
