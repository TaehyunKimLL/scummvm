# 조합형(Johab) 코드와 조합형 비트맵 폰트 조사 (통합본)

작성: 2026-10-14. 트리: `~/work/scummvm/johab` (branch johab @ 809dabb9e97).

이 문서는 독립 보고서 세 개를 하나로 합친 것이다.
- **[C]** Claude: `runs/johab-research/claude-out.md`
- **[S]** hermes GPT-SOL: `runs/johab-research/sol-out.md`
- **[G]** agy/Gemini: `runs/johab-research/agy-out.md`

세 보고서가 갈린 항목은 표준 문서, 트리 안의 코드, 실제 폰트 파일(`~/work/johab-fonts`)로 다시 확인했다. 그런 항목에는 **[쟁점]** 표시와 판정을 붙였다.
트리 안의 사실은 `파일:줄`로, 외부 사실은 URL로 출처를 단다. 수집 폰트는 `~/work/johab-fonts/…` 경로로 가리킨다.
확인하지 못한 것은 **UNVERIFIED**로 표시한다. 검사 기록은 부록 A "검증 기록"에 번호(V1…)를 붙여 모았다.

---

## 1. 조합형 코드 체계

### 1.1 표준과 변형
- 조합형은 KS C 5601-1992(현 KS X 1001) **부속서 3**에 정의된 보조 부호계다. 1992년 개정 때 업계의 상용 조합형(KSSM)을 받아들였다. 세 보고서 모두 같은 내용이다.
  출처: https://en.wikipedia.org/wiki/KS_X_1001 (History 절), https://www.unicode.org/Public/MAPPINGS/OBSOLETE/EASTASIA/KSC/JOHAB.TXT 머리말
- Microsoft 코드페이지 번호는 **1361**(Korean Johab)이고, 949는 UHC다. 출처: https://learn.microsoft.com/windows/win32/intl/code-page-identifiers
- **[쟁점] 삼보 조합형의 한글 배치.**
  - [C]와 [S]: "삼보 조합형 = KSSM = 상용 조합형"이고, 한글 비트 배치는 같다. [S]는 당시 기사(https://www.joongang.co.kr/article/2750898, "상용조합형(삼보조합형·KSSM)")를 근거로 들었다.
    [C]는 삼보 TG·큐닉스·대우 조합형도 한글 부분은 같고 한자·기호 배치만 다르다고 보았다(https://charset.fandom.com/ko/wiki/조합형, 『컴퓨터 속의 한글』 인용).
  - [G]: 삼보 조합형은 비트 배치와 채움 정의가 달라 호환되지 않는다고 했다. 출처는 TTA 용어사전 첫 화면뿐이고 구체적인 쪽이 없다.
  - **판정**: [C]·[S]를 따른다. [G]의 주장은 근거가 없어 채택하지 않았다. fandom 페이지는 이번에 HTTP 402로 다시 열리지 않아 개수 수치는 재확인하지 못했다(**UNVERIFIED**).
  - [C]는 별도로 이렇게 적었다: 삼성 SPC, 금성 MIGHTY, 도깨비 II, KS C 5601-1987 16비트 조합형은 5비트 값과 자모의 대응 자체가 다르다. 이것도 같은 fandom 출처라 **UNVERIFIED**로 남긴다.
- [G]는 CP1361이 D8 사용자 정의 영역까지 "완벽하게" 지원한다고 했다. 그러나 JOHAB.TXT에는 D8 행이 하나도 없다(V3).
  Windows가 D8을 어떻게 처리하는지는 **UNVERIFIED**이다.

### 1.2 16비트 배치
```
 bit 15 | 14..10 | 9..5  | 4..0
   1    | 초성(5)| 중성(5)| 종성(5)
```
- 파일과 문자열에서는 **lead(상위) 바이트가 먼저** 온다. 가 = `88 61`.
- x86에서 `uint16`으로 읽으면 두 바이트가 뒤집힌다. Kyra가 `createGlyph()` 첫 줄에서 바이트를 바꾸는 이유가 이것이다(`engines/kyra/graphics/screen_lok.cpp:621`). 파일의 바이트 순서가 다른 것은 아니다.
- 세 보고서가 일치한다. 출처: glibc `iconvdata/johab.c` (https://github.com/bminor/glibc/blob/master/iconvdata/johab.c), CPython `Modules/cjkcodecs/_codecs_kr.c`

### 1.3 필드 값 표 (부속서 3 표 2)
| 값 | 초성 | 중성 | 종성 |
|---|---|---|---|
| 0 | – | – | – |
| 1 | **채움** | – | **채움(받침 없음)** |
| 2 | ㄱ | **채움** | ㄱ |
| 3–7 | ㄲ ㄴ ㄷ ㄸ ㄹ | ㅏ ㅐ ㅑ ㅒ ㅓ | ㄲ ㄳ ㄴ ㄵ ㄶ |
| 8, 9 | ㅁ ㅂ | – | ㄷ ㄹ |
| 10–15 | ㅃ ㅅ ㅆ ㅇ ㅈ ㅉ | ㅔ ㅕ ㅖ ㅗ ㅘ ㅙ | ㄺ ㄻ ㄼ ㄽ ㄾ ㄿ |
| 16, 17 | ㅊ ㅋ | – | ㅀ ㅁ |
| 18 | ㅌ | ㅚ | **–** |
| 19–23 | ㅍ ㅎ – – – | ㅛ ㅜ ㅝ ㅞ ㅟ | ㅂ ㅄ ㅅ ㅆ ㅇ |
| 24, 25 | – | – | ㅈ ㅊ |
| 26–29 | – | ㅠ ㅡ ㅢ ㅣ | ㅋ ㅌ ㅍ ㅎ |
| 30, 31 | – | – | – |

세 보고서의 표가 같고, glibc의 `init[]`, `mid[]`, `final[]`와도 같다.

중성의 빈 값(0,1,8,9,16,17,24,25,30,31)은 하위 3비트가 000이나 001이 되는 자리다. 그래서 trail byte는 늘 0x41 이상이다([C], [G], Wikipedia 각주).

### 1.4 바이트 범위와 개수
- 바이트 범위
  - 한글: lead 0x84–0xD3, trail 0x41–0x7E·0x81–0xFE
  - 사용자 정의: lead 0xD8
  - 기호: lead 0xD9–0xDE
  - 한자: lead 0xE0–0xF9
  - 비한글 영역의 trail: 0x31–0x7E·0x91–0xFE(188개 = 94×2)

  출처: JOHAB.TXT 머리말. 세 보고서가 일치한다.
- JOHAB.TXT의 한글 영역은 **11,223항목**이다. 음절이 11,172자, 호환 자모(U+3131–3163)가 51자다. 음절 공식은 11,172자 모두와 일치한다(V3, [C]의 직접 검증을 재현함).
- **[쟁점] 단독 자모의 개수와 0x8441.**
  - [G]: 단독 자모가 67개(채움 포함 68개)라고 했다. 종성 단독 `0x8442`가 ㄱ이고, `0x8441`이 U+115F 또는 U+3164라고 했다.
  - [C]·[S]: 표준(JOHAB.TXT)에는 51자뿐이다. 겹받침 전용 11자만 `채움+채움+종성`(ㄳ = 0x8444) 꼴로 들어 있다. 0x8441은 없다.
  - **판정**: [C]·[S]가 맞다(V3).
    - JOHAB.TXT에는 0x8441도 0x8442도 없다.
    - CPython의 `johab` 코덱은 JOHAB.TXT보다 넓게 받아들인다. 차이는 정확히 19개다: 0x8441→U+3000, `채움+채움+단자음 종성` 16개(0x8442 ㄱ … 0x845D ㅎ), D9E6 €, D9E7 ®.
    - [C]가 "추가 19개"라고 한 것은 이 19개 전체를 가리킨 것이다. 단자음 종성형만 세면 16개다.
- 0x5C의 매핑은 구현마다 다르다. JOHAB.TXT와 glibc는 U+20A9(₩)로, CPython과 이 트리의 ASCII 경로는 U+005C로 매핑한다([C], [S], V3).

### 1.5 비한글 영역과 KS X 1001의 대응
- lead 하나가 KS X 1001의 두 행을 담는다.
  - **기호**: 0xD9–0xDE ↔ 1–12행
  - **한자**: 0xE0–0xF9 ↔ **42–93행**
- **[쟁점] 한자 행 번호.**
  - [S]의 식은 `row = 49 + 2*(lead-0xE0) + q/94`였고, 이 식으로는 한자가 "49..100행"이 된다.
  - [C]와 [G]는 42–93행이라고 했다.
  - **판정**: 42–93행이 맞다. JOHAB.TXT에서 0xE031 = U+4F3D(伽)이고, 이 글자의 EUC-KR 코드는 CAA1(42행)이다. 0xF9FE ↔ FDFE(93행)이다(V3).
    Wikipedia KS X 1001 표도 한자를 42–93행(0xCA–0xFD)으로 둔다. [S]의 식은 기준 행이 7만큼 어긋나 있다.
- 완성형 → 조합형 산술식([C]의 식이고, [G]의 식과 같은 값을 낸다).
  ```
  기호: hi 0xA1..0xAC  lead = 0xD9 + ((hi-0xA1)>>1)
  한자: hi 0xCA..0xFD  lead = 0xE0 + ((hi-0xCA)>>1)
  쌍의 첫 행: trail = lo<=0xEE ? lo-0x70 : lo-0x5E
  쌍의 둘째 행: trail = lo
  ```
  EUC-KR의 기호와 한자 전체를 CPython 코덱과 대조했다. **5,824자가 일치**했고, 어긋난 51자는 모두 4행 현대 자모였다. 이 51자는 한글 영역(0x8841 등)으로 옮겨져 있기 때문이다(V4).
- 0xDAD4 = U+3164(HANGUL FILLER)이고, 그 뒤 DAD5–DAFE는 옛 호환 자모다(V3).
- **D8 사용자 정의 영역** ([C]는 UNVERIFIED로 남겼던 항목). hanterm-xf의 `hfont.c` `convert_johab_to_3()`는 D8 31–A0을 KS X 1001 **41행**(0xC9)으로, D8 A1–FE를 **94행**(0xFE)으로 옮긴다.
  출처: `~/work/johab-fonts/x11-unix/extracted/hanterm-xf-2.0.0-167/hanterm-xf-2.0.0/hfont.c` (V7). 즉 KS X 1001의 두 사용자 정의 행이다. JOHAB.TXT, glibc, CPython은 D8을 디코드하지 않는다.

---

## 2. 변환

### 2.1 조합형 ↔ Unicode 음절 (표 없이 산술로)
세 보고서의 식이 같다. 11,172자 모두 JOHAB.TXT와 일치한다(V3).
```c
// Unicode -> Johab
i = U - 0xAC00; L = i/588; V = (i/28)%21; T = i%28;
static const uint8 JUNG[21] = {3,4,5,6,7,10,11,12,13,14,15,18,19,20,21,22,23,26,27,28,29};
c = 0x8000 | (L+2)<<10 | JUNG[V]<<5 | (T<=16 ? T+1 : T+2);

// Johab -> Unicode (역표; 채움과 무효 값 검사 먼저)
L = init-2;  V = midIdx[mid];  T = (fin==1) ? 0 : (fin<=17 ? fin-1 : fin-2);
U = 0xAC00 + (L*21 + V)*28 + T;
```
예: 가 = 0x8861, 힣 = 0xD3BD.

### 2.2 자모
- 표준 단독 자모 51자는 **호환 자모** U+3131–3163에 1:1로 대응한다. 대응 방식은 다음과 같다.
  - 초성이 될 수 있는 자음: `초성+채움+채움` (ㄱ = 0x8841)
  - 모음: `채움+모음+채움` (ㅏ = 0x8461)
  - 겹받침 전용 자음: `채움+채움+종성` (ㄳ = 0x8444)
- 첫가끝(conjoining) 자모 U+1100/1161/11A8 계열은 L/V/T 인덱스와 산술로 대응한다. 그러나 미완성 조합에는 precomposed 문자가 없다. 이를 어떻게 처리할지는 정책 문제다([C], [S]; Unicode 핵심 명세 3.12절).

### 2.3 조합형 ↔ 완성형/CP949
- 한글 음절은 **Unicode를 거쳐야 한다**. KS X 1001의 2,350자는 11,172자 가운데 띄엄띄엄 고른 집합이라 산술로 변환할 수 없다.
  - 2,350자 밖의 음절은 2바이트 완성형으로 바꿀 수 없다.
  - CP949(UHC)는 11,172자를 모두 담는다([S]).
  - 이 트리에는 2,350자 정적 표가 이미 있다(`graphics/hires_text/codepage_kr.h`, `KoreanCodePage::ksx1001HangulIndexOf`).
- 기호와 한자는 1.5절의 산술식으로 완성형과 직접 변환된다. KS X 1001 ↔ Unicode 값은 표가 필요하다.

---

## 3. 벌(glyph set) 체계와 선택표 (검증됨)

### 3.1 8×4×4 배치 (DOS 표준)
- 16×16, 1bpp, 한 줄 2바이트, MSB가 왼쪽이다. 글리프당 32바이트이고 **헤더가 없다**.
- 벌 단위가 바깥 순서(set-major)이고, 각 벌의 0번은 **빈 채움 글리프**다.
  ```
  초성: 8벌 × (채움+19) = 160   글리프 0..159
  중성: 4벌 × (채움+21) =  88   글리프 160..247
  종성: 4벌 × (채움+27) = 112   글리프 248..359
  합계 360 글리프 = 11,520 B
  ```
- 세 보고서가 일치한다. 실제 파일로도 확인했다(V5, V6).
  - iolo `src/han_*.fnt` 11종, san-1.0 `font-kor/*.KOR` 55종, HWP 2.1 Pro `HMJ.FNT`, IBM PC DOS `$KGOZN16`/`$KMYZN16`가 모두 11,520 B다.
  - 모든 벌의 0번 슬롯이 비어 있다.
- 벌 안의 자모 순서는 필드 값 순서에서 빈 값을 뺀 것이다([S]의 목록이 정확하다).
  - 초성: 채움, ㄱ … ㅎ
  - 중성: 채움, ㅏ ㅐ ㅑ ㅒ ㅓ ㅔ ㅕ ㅖ ㅗ ㅘ ㅙ ㅚ ㅛ ㅜ ㅝ ㅞ ㅟ ㅠ ㅡ ㅢ ㅣ
  - 종성: 채움, ㄱ ㄲ ㄳ ㄴ ㄵ ㄶ ㄷ ㄹ ㄺ ㄻ ㄼ ㄽ ㄾ ㄿ ㅀ ㅁ ㅂ ㅄ ㅅ ㅆ ㅇ ㅈ ㅊ ㅋ ㅌ ㅍ ㅎ
- 채움 슬롯을 뺀 변형(11,008 B)도 있다. fviewer의 `hft2han.cpp`는 이런 파일에 채움을 끼워 넣는다([S], https://github.com/gcjjyy/fviewer).

### 3.2 8×4×4 선택표

V는 Unicode 중성 인덱스(ㅏ=0 … ㅣ=20)이고, 벌 번호는 0부터 센다.
```
V 순서                  ㅏ ㅐ ㅑ ㅒ ㅓ ㅔ ㅕ ㅖ ㅗ ㅘ ㅙ ㅚ ㅛ ㅜ ㅝ ㅞ ㅟ ㅠ ㅡ ㅢ ㅣ
초성 벌, 받침 없음       0  0  0  0  0  0  0  0  1  3  3  3  1  2  4  4  4  2  1  3  0
초성 벌, 받침 있음       5  5  5  5  5  5  5  5  6  7  7  7  6  6  7  7  7  6  6  7  5
종성 벌                  0  2  0  2  1  2  1  2  3  0  2  1  3  3  1  2  1  3  3  1  1
중성 벌 = (초성이 ㄱ·ㅋ(·채움) ? 0 : 1) + (받침 ? 2 : 0)        <- 표준
glyph(초성) = 벌*20 + L+1;  glyph(중성) = 160 + 벌*22 + V+1;  glyph(종성) = 248 + 벌*28 + T
```
- 초성 벌 표와 종성 벌 표는 세 보고서가 같다. 대조한 구현도 모두 같은 값을 쓴다: iolo `generate_hangul_syllables.py`, fviewer `fviewer.cpp:13-15`, hanterm-xf `hfont.c`의 `type1_no`/`type1_yes`/`_type3`, IHNM `engines/saga/font.cpp:758-783`(V1, V2, V7).
- 말로 정리하면 다음과 같다.
  - 초성 벌 0–4(받침 없음): 세로모음 / ㅗㅛㅡ / ㅜㅠ / ㅘㅙㅚㅢ / ㅝㅞㅟ
  - 초성 벌 5–7(받침 있음): 세로모음 / 가로모음 ㅗㅛㅜㅠㅡ / 겹모음
  - 종성 벌: ㅏㅑㅘ / ㅓㅕㅚㅝㅟㅢㅣ / ㅐㅒㅔㅖㅙㅞ / ㅗㅛㅜㅠㅡ
- **[쟁점] 중성 벌에서 ㄱ·ㅋ의 순서.**
  - [C]: 표준은 ㄱ·ㅋ가 벌 0(받침 없음)과 2(받침 있음)다. IHNM 코드만 반대다.
  - [S]·[G]: ㄱ·ㅋ가 벌 1과 3이라고 했다. [S]는 fviewer와 IHNM이 같은 규칙을 쓴다고 했고, [G]는 IHNM 코드를 표준 표로 옮겨 적었다.
  - **판정**: 표준은 **ㄱ·ㅋ → 0/2**다. 근거는 다음과 같다.
    1. 소스 코드 대조(V1, V2, V7):
       - fviewer `fviewer.cpp:130`: `joong_type = ((choseong == 1 || choseong == 16) ? 0 : 1) + (jongseong ? 2 : 0)`. [S]가 fviewer를 잘못 읽은 것이다.
       - iolo `generate_hangul_syllables.py:64-73`: ㄱ·ㅋ이면 0 또는 2다.
       - hanterm-xf `hfont.c` `convert_3_to_jo844font`: 초성이 채움·ㄱ·ㅋ이면 0이다.
       - san-1.0 `src-c/HANDRV.C` `Draw10_4_4`의 `mid[]`: ㄱ(1)·ㅋ(16)이 0이다.
       - Unifoundry의 johab844 설명: "초성이 채움, ㄱ, ㅋ이고 받침 없음"이 첫 벌이다 (https://www.unifoundry.com/hangul/hangul-generation.html).
    2. 실제 폰트 데이터(V8, V9).
       - HWP 3.0 `HAN16_0/1/4.FNT`는 같은 파일 안에 완성형 2,350자와 8×4×4 자모 360자를 함께 담고 있다. 그래서 각 음절에 대해 네 중성 벌 가운데 어느 것이 완성형 글리프와 가장 가까운지 셀 수 있다.
         - 두 벌이 다른 경우만 보면, ㄱ·ㅋ 음절은 벌 0·2를, 그 밖의 음절은 1·3을 골랐다. 예: `HAN16_1` 받침 있는 ㄱ·ㅋ 음절은 벌 2가 101자, 벌 3이 3자였다.
         - 정확히 일치하는 음절 수도 `HAN16_0`에서 0/2 규칙 2,083자 대 1/3 규칙 1,803자로, 0/2 규칙이 앞섰다.
       - 자모만 있는 폰트에서는 벌 0과 1이 ㅗㅘㅙㅚㅛ(가로모음)에서만 다르다(HMJ.FNT). 벌 0의 ㅗ는 기둥이 위로 2줄 더 길고, 벌 1의 ㅗ는 짧아서 ㄴ의 밑획 아래에 자리를 남긴다(렌더 비교 V9).
         `$KMYZN24`에서도 벌 1이 ㄴ·ㅁ과의 간격을 1px 더 둔다. 즉 벌 0이 "받침 획이 없는" ㄱ·ㅋ용이다.
    3. **IHNM은 의도적으로 반대다.** `engines/saga/font.cpp`의 이 줄은 처음에 `if (!(initial == 2 || initial == 3 || initial == 17)) midvariant++`(표준 순서, ㄲ 포함)였다.
       이후 commit 829525e2bd6 "SAGA: Improve handling of Hangul to match the original"(2022-11-17)에서 `if (initial == 2 || initial == 17) midvariant++`로 바뀌었다. 원본에 맞추려고 일부러 뒤집은 것이다(V10).
       따라서 sbh1616.fnt는 중성 벌 순서가 표준과 반대인 파일로 보인다. 파일이 없어 직접 확인하지는 못했다(**UNVERIFIED**, 실제 sbh1616.fnt로 확인할 것).
    4. 덧붙여, ㄲ는 ㄱ·ㅋ 그룹이 아니다. hanterm, iolo, fviewer, Kyra 표 모두 그렇고, HWP 데이터에서도 ㄲ 음절은 1·3벌을 골랐다(V8).
- 알려진 함정: mytears 예제는 `first==1||first==24`로 비교한다. 24는 **종성** ㅋ의 번호라 초성 ㅋ이 틀린 벌을 쓴다([C], https://kldp.org/node/39679).
- 함께 쓰는 영문 폰트 `.ENG`는 8×16 × 256자 = 4,096 B이고 CP437 배치다. 128자짜리 2,048 B 변형도 있다(iolo `asc_san.fnt`; san `HANDRV.C`의 `SetEng`는 4096 또는 2048 B를 받는다). 세 보고서가 일치한다(V5).

### 3.3 다른 벌 체계
| 체계 | 글리프 | 16px 크기 | 상태 |
|---|---|---|---|
| 8×4×4 | 360 | 11,520 | 검증(3.1–3.2절) |
| 8×4×4 + 기호 127 (DKB2) | 487 | 15,584 | san `HANDRV.C` `SetHan`이 15,584 B를 8×4×4로 받는다(V11). 이번에 새로 찾은 사실 |
| 10×4×4 (san/한라 계열) | 400 | **12,800** | 검증(V11). san `HANPL.KOR`·`HUNMIN.KOR`이 12,800 B. 아래 설명 참고 |
| DKB3 | 912 | 29,184 | san `SetDKB3Han`의 재배치 표만 확인. 각 글리프의 의미는 미해독 |
| 6×2×1 (애플 II 계열) | 192 | 6,144 | [C], iolo/6x2x1-fonts. 크기만 확인 |
| hanterm johab-1 **10×(3|4)×4** | 529 | – (BDF) | 검증. 3.4절 |
| Kyra LoK MK15.BIT 10×4×4 (15×15, 자모-major) | 385 | 11,550 (30 B/글리프) | 검증. 6.1절 |
| 10×6×4 | – | – | KLDP 설명: 중성 6벌 = (ㄱㅋ / ㅎ / 그 밖) × 받침 유무 ([C], [S]: https://doc.kldp.org/wiki.php/%C1%B6%C7%D5%B1%DB%B2%C3) |
| 6×2×2, 2×1×2 | 220 / 118 | 7,040 / 3,776 | [G]의 크기는 산술 값일 뿐이다. 실제 파일과 선택표는 **UNVERIFIED** |
| Unifont 6/3/1 | – | – | Unifont 15.1부터의 자체 체계 ([S], Unifoundry) |

- **10×4×4 (san)**: `src-c/HANDRV.C`의 `Draw10_4_4`, `SetDKB2Han`을 읽어 확인했다(V11). [C]가 "규칙 미확정"으로 남겼던 항목이다.
  - 배치: 초성 10벌×20(0–199) → 중성 4벌×22(200–287) → 종성 4벌×28(288–399). set-major이고 0번은 채움이다.
  - 초성 벌 = `cho[V]` + (받침 ? 5 : 0). `cho[]`는 8×4×4의 "받침 없음" 표와 같다.
    즉 받침이 있을 때도 모음 5그룹(세로 / ㅗㅛㅡ / ㅜㅠ / ㅘㅙㅚㅢ / ㅝㅞㅟ)을 따로 둔다.
  - 중성 벌과 종성 벌은 8×4×4 규칙과 같다(ㄱ·ㅋ → 0/2).
  - `SetDKB2Han`은 8×4×4를 10×4×4로 넓히는 방법을 그대로 보여 준다. 초성 벌 6을 7로, 벌 7을 8과 9로 복제한다(`movmem`).
  - 다른 프로그램의 10×4×4(한라프로, GSVIP, Kyra)가 같은 규칙과 배치를 쓰는지는 보장되지 않는다.

### 3.4 hanterm `johab-1` 배치
- **[쟁점]**: [S]는 "10×(3/4)×4, 529글리프, 자모별 변형이 연속"이라고 했다. 수집 노트(`notes/x11-unix.md`)는 "10-6-6"이라고 적었다.
- **판정**: [S]가 맞다. "10-6-6"은 hanterm-font 3.1 tarball의 **디렉터리 이름**일 뿐이다(V7).
  - `10-6-6/johabg16.bdf`는 `CHARS 529`이고 ENCODING이 0–528이다. 글리프 0은 빈 채움, 1 = ㄱ, 11 = ㄲ, 311 = ㅏ, 405 = 종성 ㄱ이다(렌더로 확인).
  - hanterm-xf `hfont.c`의 `convert_3_to_johabfont()` 규칙은 다음과 같다.
    - 초성 글리프 = `f*10 + (받침 ? fcon_map2 : fcon_map1)[m] - 19`, 즉 1 + 10·L + 변형.
      `fcon_map1` = 8×4×4의 받침 없는 표다. `fcon_map2`는 5,6,8,7,9로, 받침이 있어도 5그룹을 따로 둔다.
    - 중성 글리프 = `vow_base[m]` + 변형.
      - ㅗ 계열(`vow_kind=1`: ㅗㅘㅙㅚㅛㅡㅢ)은 4벌이다. 변형 = (ㄱ·ㅋ ? 0 : 1) + (받침 ? 2 : 0).
      - 그 밖의 모음은 3벌이다. 변형 = `lcon_kind[l]`: 받침 없음 0, **ㄴ 받침 2**, 그 밖의 받침 1.
    - 종성 글리프 = `l*4 + lcon_map[m] + (l<18 ? 397 : 393)`, 즉 405 + 4·(T−1) + 변형. 변형은 `lcon_map[m]`이고, 8×4×4 종성 표(F844)와 **다르다**.
      - `lcon_map`(5비트 중성값 0..29 순) = 0,0,0,0,2,0,2,1,0,0,2,1,2,3,0,0,0,0,0,3,3,1,1,1,0,0,3,3,0,1. ㅙ·ㅚ·ㅞ·ㅢ는 0,0,1,0이다(F844는 2,1,2,1).
      - 출처: hanterm-xf 2.0.0-167·2.0.6-177 `hfont.c`, hanterm 3.1-19990706·3.1.3·3.1.5·3.1.6·3.3.1p17·3.3.1p18 `hangul.c`. 일곱 판 모두 같은 30개 숫자다. 2026-10-15 정정: 앞선 판은 육안으로 `johabm16`·`johabp16`·`iyagi16`을 보고 F844와 같다고 적었으나 틀렸다. kfont.py의 `HT_LMAP`이 원본과 일치한다.
    - 비한글은 `0x211 + (row−1)*94 + col` 위치에 놓인다. `johabs*`·`johabsh*` BDF가 기호(1,644자)와 기호+한자(6,563자)를 함께 싣는다.
  - 191–310번에도 글리프가 있다. 옛한글 초성 12자 × 10변형이다(Unifoundry: "31 initials × 10").
  - `8-4-4/*.bdf`(136종)는 360글리프, ENCODING 0–359이고 3.1절의 표준 배치다. 폰트 이름 끝이 `johab844-1`이다.

---

## 4. 실제 파일 형식 (수집본 기준)

| 파일 | 크기 | 형식 | 확인 |
|---|---|---|---|
| iolo `src/han_*.fnt` 11종 | 11,520 | raw 8×4×4 | V5 |
| iolo `src/asc_*.fnt` | 4,096 / 2,048 | raw 8×16 × 256/128 | V5 |
| san-1.0 `font-kor/*.KOR` | 11,520 ×55, 12,800 ×2 | raw 8×4×4 / 10×4×4 | V11 |
| HWP 2.1 Pro `HMJ.FNT` | 11,520 | raw 8×4×4 | V6 |
| **HWP 3.0 DOS `HDR30/FONT/HAN16_n.FNT`** | 86,792 = 72 + 2,710×32 | 72 B 헤더 `"FNT\x01"`, 01 01, u16 LE w=16 h=16, 벌 수 8/4/4, count 2710, 폰트 이름(조합형 "고딕체 16*16"). 이어서 **완성형 2,350자(KS 순서) + 8×4×4 자모 360자** | V12 |
| HWP 3.0 `HAN24_n.FNT` | 169,272 = 72 + 2,350×72 | 같은 헤더(count는 2710으로 적혀 있음). 실제로는 완성형 2,350자만 있다 | V12 |
| HWP 3.0 `HAJ16_0.FNT` | 156,488 = 72 + 4,888×32 | 헤더 type 바이트 02, 한자 | V12 |
| HWP 3.0 `ENG16_n.FNT` | 2,424 = 72 + 147×16 | type 00, 8×16 | V12 |
| HWP 1.20 `HANGUL.SFT` | 38,208 = 32 + 1,193×32 | `"HWP Font Editor v1.0"` 32 B 헤더. 1,193글리프로 **8×4×4가 아니다**. 합성 규칙은 미해독 | V13 |
| IBM PC DOS HBIOS `$KGOZN16`·`$KMYZN16.FNT` | 11,520 | 헤더 없는 raw 8×4×4(고딕, 명조) | V6 |
| **IBM PC DOS HBIOS `$KMYZN24.FNT`** | 243,900 = 2,710×90 | 헤더 없음. 셀 24×30, 한 줄 3 B, 잉크는 3–26행(24×24). 글리프 0–359 = 8×4×4 자모(모든 벌의 0번이 빈 채움), 360 = 가 … 2709 = 힝(완성형 2,350자) | V6, V9 |
| Kyra LoK `MK15.BIT` | 11,550 = 385×30 | 15×15, 한 줄 2 B, 초성 191 / 중성 85 / 종성 109 | V14 |
| Kyra HoF 팬번역 `KOREAN.FNT` | 42,300 = 2,350×18 | 10×9, **완성형** | `engines/kyra/detection_tables.h:753`, V15 |
| IHNM `sbh1616.fnt` | 미확보 | 8×4×4 + 비자모 126 (코드 기준) | **UNVERIFIED** |

**[쟁점] 정리**

- **SFT 형식**
  - [G]: 한글 1.2/1.5 SFT = 32 B 헤더 + 11,520 B 8×4×4, 총 11,552 B라고 했다.
  - **판정**: 틀렸다. 실제 HWP 1.20 `HANGUL.SFT`는 32 B 헤더 + **1,193글리프**다(V13). [C]·[S]는 "32 B 헤더 뒤 raw 비트맵"이라고만 해서 틀리지 않았다.
- **sbh1616.fnt 크기**
  - [G]: "정확히 15,552 B"라고 했다.
  - **판정**: 근거가 없다. 로더는 11,520 B 이상인지만 검사한다(`engines/saga/font.cpp:249-262`). 126이라는 상수만으로 전체 크기를 단정할 수 없다([S]와 같은 견해).
  - 비슷한 실제 형식으로는 san이 받는 15,584 B(360 + 127) DKB2가 있다.
- **MK15.BIT**
  - 수집 노트 `notes/games.tsv`는 "30 B zero header + 11,520 B 8×4×4"라고 적었다.
  - **판정**: 틀렸다. 두 해석으로 모두 렌더해 보았다(V14).
    - 32 B 글리프로 읽으면 쓰레기가 나온다.
    - 30 B 글리프로 읽으면 0 = 빈 채움, 1 = ㄱ, 11 = ㄲ, 191 = 중성 채움, 192 = ㅏ로 맞아떨어진다.
    - Kyra 표로 가·고·각·꽉·뷁·흙을 합성하면 올바르게 나온다.
  - 첫 30바이트는 헤더가 아니라 빈 글리프 0이다. 11,550 = 30 + 11,520은 우연의 일치다.
- **[G]의 `HANJA.FNT` 156,416 B와 `SPECIAL.FNT`**: 일반론이다. 실제 대응물은 다음과 같다.
  - HWP `HAJ16_0.FNT`(72 + 156,416)
  - 이야기 `IHANJA.FNT`(raw 4,888×32, `notes/dos.md`)
  - HBIOS `$KSPHJ16.FNT`(6,016×32 = 기호 1,128 + 한자 4,888, `notes/os.md`)
- **한메 .HAN과 이야기의 실제 형식**
  - 이야기 `I.FNT`는 LZEXE로 압축된 실행 파일 형태다. 한메 Drawing Tool `.FNT`와 GSVIP `.GSF`는 압축된 가변 길이 형식이다. 모두 미해독이다(`notes/dos.md`).
  - `.HAN` 확장자는 김윤수 "FONTS COLLECTION"의 raw 8×4×4이고, san에서는 `.KOR`로 이름만 바꿨다([C], san `doc/FONT.TXT`).
- **기타 보고**
  - [C]·[S]가 peremen 블로그(https://blog.peremen.name/entry/조합형-글꼴의-기술-분석/)를 인용해 적은 체계들은 이번에 파일로 다시 확인하지 않았다. 대상은 한메 1.x `HANME.FNT` 5×2×3, 한라프로 `HAN10MD1`·`HAN212GD`, GSVIP·HECON(벌마다 높이가 다름), 삼보 TGHP 14×6×4다.
  - 수집 노트(`notes/dos.md`)의 크기 기반 기록도 같은 상태다: 24×24 8×4×4 = 25,920, 40×40 = 72,000, 48×48 = 103,680.
- **게임 폰트 (`notes/games.md`, 수집 진행 중)**: 국산 DOS 게임 다수가 11,520 B 8×4×4를 쓴다. 예: CRW1 `HANGUL.FNT`, Stillhunt `HAN.AYU`, Great Fighter `DEFAULT.HBF`, Zis `HANGUL.KOR`.
  막고야 게임은 `"MAKKOYA FNT\x1a"` 30 B 헤더 + 10×13 17 B 글리프(358–359개)를 쓴다.

---

## 5. 합성 렌더링

- 기본 방식은 세 글리프를 OR하는 것이다. 받침이 없으면 종성 단계를 건너뛰거나 빈 채움 글리프를 OR한다. 결과는 같다. 세 보고서가 일치한다.
- 겹받침(ㄳ ㄺ …)과 겹모음(ㅘ ㅢ …)은 각각 독립된 글리프 하나다. 렌더러가 다시 쪼개 합성하지 않는다. 세 보고서가 일치한다.
- 받침이 없을 때 종성 벌 번호를 채움 0에 더하면 엉뚱한 받침이 생긴다. johab2ucs2 변경 기록에도 이 수정이 있다([S]).
- 단독 자모를 어느 벌로 그릴지는 표준이 없다. 다만 hanterm은 채움 초성을 ㄱ·ㅋ와 같은 중성 벌로 처리한다.
- 같은 폰트 안의 완성형과 OR 합성 결과는 같지 않다. HWP `HAN16_*`에서 정확히 일치한 것은 375–2,221자/2,350자였고, `$KMYZN24`에서는 0자였다(V8).
  즉 상용 제품의 완성형 글리프는 따로 손본 디자인이다. 조합형 합성 품질은 완성형보다 떨어진다고 봐야 한다.
- 품질 문제와 함정([C], [S], [G]를 합친 것)
  1. 8×4×4에는 "세로모음 + ㄴ 받침" 전용 중성 형태가 없다. hanterm johab-1과 10×6×4는 이를 보완한다.
  2. 폰트마다 벌을 재사용하는 정도가 다르다. 예를 들어 `han_hanme`, `han_sam`, `han_thin`은 중성 벌 0과 1이 22글자 모두 같다(V2). 그래서 규칙을 잘못 골라도 일부 폰트에서는 드러나지 않는다.
  3. 다음과 같은 혼동은 모든 오프셋을 어긋나게 한다: 채움 포함/생략, set-major/jamo-major, 15×15/16×16 피치, 행 MSB와 바이트 엔디언([S]).
  4. 벌마다 높이가 다른 폰트(GSVIP, HECON)는 세로 위치 보정이 필요하다([C], peremen).
  5. 16px 원본을 정수가 아닌 배율로 키우면 흐려진다([C]).
  6. 획이 많은 음절(뷁, 흙 등)은 16px에서 뭉친다([G]).

---

## 6. 이 트리의 구현과 외부 구현

### 6.1 Kyra LoK `JohabFontLoK` (`engines/kyra/graphics/screen_lok.cpp:538-680`)
- `MK15.BIT`(`engines/kyra/engine/kyra_lok.cpp:343`)는 크기가 (191+85+109)×30 = 11,550 B일 때만 받는다(`screen_lok.cpp:561`). 15×15 글리프이고, 한 줄 2 B 가운데 15비트를 쓴다.
- **[쟁점] 블록 이름.**
  - [G]: 첫 블록 191개가 "종성"이라고 했다. 코드의 enum 이름 `kNumJongseong=191`을 그대로 따른 것이다.
  - [C]·[S]: 첫 블록은 초성이고 enum 이름이 뒤바뀐 것이라고 했다.
  - **판정**: [C]·[S]가 맞다(V14).
    - `_2byteTables[0]`은 **초성 필드**(`i3`)로 색인한다. ㄱ = 0x20(글리프 1), ㄲ = 0x160(11)로 10씩 늘어난다.
    - 실제 파일의 글리프 1은 ㄱ, 11은 ㄲ이다.
    - 블록 0(191 = 1 + 19×10)은 초성, 블록 1(85 = 1 + 21×4)은 중성, 블록 2(109 = 1 + 27×4)는 종성이다.
- 표 해독([C]·[S]와 같고 V14로 재확인했다)
  - 초성 글리프 = 1 + 10·L + 2·모음그룹 + 받침 유무. 모음그룹은 세로 0, ㅗㅛㅡ 1, ㅜㅠ 2, ㅘㅙㅚㅢ 3, ㅝㅞㅟ 4다. 즉 **10×4×4**다(san의 10×4×4와 같은 분류지만 배치는 jamo-major다).
  - 중성 글리프 = 1 + 4·V + (ㄱ·ㅋ·채움 ? 0 : 2) + 받침 유무. ㄱ·ㅋ이 먼저다(표준 방향).
  - 종성 글리프 = 1 + 4·(T−1) + 표준 종성 벌.
- off-by-one: `if (t[l] <= lim[l])`은 `<`가 맞다. 11,172자 전부를 넣어 보면 실제 최대 인덱스는 190/84/108로 경계 안이다. 그래서 지금은 무해하다(V14, [C]·[S]).
- Kyra LoK 한국어 텍스트는 조합형이다. `devtools/create_kyradat/resources/lok_dos_korean.h`의 2바이트 문자열 217개가 모두 조합형으로 디코드된다(V15).
- 조합형 입력: `engines/kyra/engine/util.cpp:112-260`. 두벌식 키를 조합형 필드로 합친다(마스크 0xFC1F, 0x3E0, 0x1F).

### 6.2 Kyra HoF (팬번역)
- **[쟁점] 실제 인코딩.**
  - 세 보고서 모두 코드만 보고 "`kJohab` 타입이지만 실제로는 EUC-KR"이라고 했다.
  - **판정**: 확정이다(V15).
    - `KoreanTwoByteFontHOF::getFontOffset`은 `(hi-0xB0)*94 + (lo-0xA1)`로 계산한다(`screen_hof.cpp:118-129`).
    - `KOREAN.FNT`는 42,300 B = 2,350×18 B다(`detection_tables.h:753`).
    - `hof_dos_cd_korean.h`의 2바이트 escape 문자열 36개가 모두 EUC-KR로 디코드되고, 조합형으로는 하나도 디코드되지 않는다.
  - 이 코드는 2026-03-24 commit f49d5ca9845(팬번역 지원 추가)에서 들어왔다. `getType()`이 `kJohab`을 돌려주는 것은 ASCII 분기(`screen.cpp:1537,1668`)를 타기 위한 것이다. 이름이 오해를 부른다.

### 6.3 Saga IHNM (`engines/saga/font.cpp:249-262, 720-800`, `font.h:270-280`)
- 16×16 8×4×4 + 비자모 126이다. 초성 벌 표와 종성 벌 표는 표준과 같다.
- **중성 벌만 ㄱ·ㅋ = 1/3**이다. 3.2절 판정대로 원본에 맞춘 의도적인 차이다(commit 829525e2bd6).
- 매 그리기마다 `blitGlyph`를 세 번 호출한다. 캐시는 없다. 비자모(lead ≥ 0x15 필드)는 칸만 띄운다.

### 6.4 외부 구현 (라이선스)
| 프로젝트 | 내용 | 라이선스 | 비고 |
|---|---|---|---|
| glibc `iconvdata/johab.c` | 조합형 ↔ UCS4 | LGPL-2.1+ | 세 보고서 |
| CPython `_codecs_kr.c` | `johab`/`cp1361` 코덱 | PSF | 표준보다 넓게 받아들임(V3) |
| gcjjyy/fviewer | 8×4×4 뷰어, `hft2han` | MIT | 중성 ㄱ·ㅋ = 0 (V1) |
| iolo/8x4x4-fonts | raw → BDF → TTF, 폰트 11종 | 코드 MIT, 폰트 OFL 표기 | 원본 폰트의 권리는 7절 |
| hanterm-xf `hfont.c` | johab-1 / johab844 선택표 | X11 계열로 추정 | 라이선스 문구는 확인하지 못함(**UNVERIFIED**) |
| drypot/san-1.0 | 8×4×4 / 10×4×4 / DKB2·3 로더 | 저장소 MIT | V11 |
| neodgm (Neo둥근모) | 둥근모꼴 자모로 11,172자 합성 | OFL-1.1 | [C] |
| Unifont 도구 | `johab2syllables`, `unigen-hangul` | GPL-2.0+ | [C], [S] |
| ruo91/jfbterm (han) | PCF 로더 + iconv 기반 `johabcode.c` | font.c는 BSD 2-clause | raw 8×4×4 합성기가 아님 ([S]) |
| libhangul | 입력기 오토마타 | LGPL-2.1 | 렌더링 없음 |

- **[쟁점] jfbterm/fbterm, DOSBox-X**
  - [G]: jfbterm이 8×4×4를 직접 렌더한다고 했고, DOSBox-X가 `HAN16.FNT`·`DKB16.FNT` 8×4×4를 로드한다고 했다.
  - **판정**: 근거가 없어 채택하지 않는다.
    - jfbterm han 포크의 `font.c`는 PCF 로더다([S]). 수집본의 jfbterm 폰트 팩도 Unicode PCF뿐이다(`notes/x11-unix.md`).
    - DOSBox-X 문서는 `dosv=ko`(DOS/V, CP949)와 TTF 출력만 말한다. 8×4×4나 조합형을 언급하는 곳은 찾지 못했다(https://dosbox-x.com/wiki/Guide%3AEast-Asian-language-support-in-DOSBox‐X, https://dosbox-x.com/release-0.83.16.html).
- **[쟁점] hanterm의 라이선스**
  - [G]: hanterm과 그 비트맵 폰트가 MIT/BSD이고 자유롭게 재배포할 수 있다고 했다.
  - **판정**: 틀렸다. Unifoundry는 이렇게 적었다: Hanterm 폰트 4종(johabg16 등)은 "free of charge"이지만 "only licensed in Korea for use with Hanterm"이고, "johabg16 could not be licensed under the GPL"이다. 그래서 Unifont에서 빼고 다시 그렸다(https://www.unifoundry.com/hangul/index.html, [S]가 인용).
    수집본의 `8-4-4/*.bdf`에도 "subject to the same copyright … as the original font"(fstobdf 문구)만 있다.

---

## 7. 재배포 가능한 폰트

- **확실히 자유로운 것**
  - Neo둥근모(OFL-1.1). 원본 둥근모꼴(김중태)은 퍼블릭 도메인으로 배포되었다(https://github.com/neodgm/neodgm, https://ith.kr/about/font.html).
    단 raw 8×4×4 파일이 아니라 Elixir 소스 안의 자모 비트맵이다([C]). 원본 archive 안의 공지는 직접 열어 보지 않았다([S], **UNVERIFIED**).
  - GNU Unifont 한글: GPL-2.0+ + 글꼴 예외 / OFL-1.1. 체계는 6/3/1이라 DOS 8×4×4와 다르다([S]).
  - HBIOS-SYS: CC BY-SA 4.0 + OFL. MS-DOS 한글 시스템 폰트를 복각한 것으로, 수집본은 BDF/TTF다([C], `notes/dos.md`).
  - hurss/fonts(MIT): DOS 비트맵에서 만든 11,172자 BDF(16px, 24px)다. 원본 비트맵의 권리는 따로 따져야 한다(`notes/printer.md`).
  - 개미체(AGPL-3.0, 2×1×2) ([S], https://blog.aaidee.com/183)
  - san 자작 폰트(SANIGJ, SANHSB, SAN.ENG): 저장소 MIT ([C])
- **출처 문제가 있는 것**: 한메, 이야기, 도깨비, 삼보, HWP, IBM HBIOS, 게임 폰트(MK15.BIT, sbh1616.fnt, 국산 DOS 게임).
  - 김윤수 컬렉션은 상업적 사용을 금지했다.
  - iolo가 붙인 OFL은 원권리자가 허락한 것인지 확인되지 않았다([C], **UNVERIFIED**).
  - [G]가 "재배포 가능"으로 분류한 hanterm 폰트와 "단비 퍼블릭 도메인"은 근거가 없다. hanterm은 6.4절에서 반박했고, 단비 퍼블릭 도메인은 **UNVERIFIED**이다.
- **결론**: 프로젝트 팩(bitmap-ko1)에는 Neo둥근모 계열이나 자작 자모만 넣는다. 그 밖의 폰트는 사용자가 자기 파일을 넣어 쓰도록 로더만 제공한다. 세 보고서의 결론과 같고, [G]의 hanterm 항목만 제외했다.

---

## 8. 이 포크에 대한 시사점과 권고

### 8.1 핵심 원칙: 폰트 배치와 텍스트 인코딩은 서로 독립이다
hires_text는 바이트를 Unicode로 디코드한 뒤 `UnicodeGlyphSource`(`graphics/hires_text/glyph_source.h:64-100`)에 글리프를 요청한다.
조합형 비트맵 소스는 U+AC00–D7A3에서 L/V/T를 산술로 구하고 벌 표로 합성하면 된다. 이렇게 하면 같은 조합형 폰트로 CP949 게임(SCI, SCUMM, AGI 팬번역)과 조합형 게임(Kyra LoK, IHNM)을 모두 그릴 수 있다.
맵의 `encoding=johab`(`font_map.cpp:88`)은 텍스트 바이트의 해석이지 폰트 배치가 아니다. 세 보고서가 일치한다.

### 8.2 `graphics/hires_text`의 Johab glyph source (권고)
- **`JohabBitmapGlyphSource : UnicodeGlyphSource`** + **레이아웃 프로파일**
  - 프로파일 필드: 셀 w/h, 행 바이트, 글리프 바이트, 헤더 길이, 블록 순서·오프셋, set-major/jamo-major, 벌 수, 채움 슬롯 포함 여부, 초성/중성/종성 선택표, 중성 ㄱ·ㅋ 방향, 잉크 행 범위(crop), 메트릭.
  - 내장 프로파일(모두 실제 파일이나 코드로 검증함)

    | 이름 | 셀 | 글리프 | 크기 | 비고 |
    |---|---|---|---|---|
    | `844` | 16×16 | 32 B | 11,520 | ㄱ·ㅋ = 0/2. iolo, san, HMJ, `$K*ZN16` |
    | `844+dkb127` | 16×16 | 32 B | 15,584 | 앞 360글리프만 사용 |
    | `844-ihnm` | 16×16 | 32 B | ≥11,520 | 중성 ㄱ·ㅋ = 1/3 |
    | `1044-san` | 16×16 | 32 B | 12,800 | 3.3절 |
    | `kyra-lok` | 15×15 | 30 B | 11,550 | jamo-major, 6.1절. 셀 15, advance 16 |
    | `hwp3-fnt` | 16×16 | 32 B | – | 72 B 헤더, 자모가 글리프 2,350부터. 앞의 완성형 2,350자는 별도 경로(8.3절)로 직접 쓸 수 있음 |
    | `hbios24` | 24×30 | 90 B | – | 3–26행을 잘라 24×24로 씀. 자모 0–359 |
    | `hanterm-johab` | 16×16 | 32 B | 529글리프 | BDF 입력. 10×(3|4)×4 |

  - **판별**: 파일에 매직이 없다. 크기(11,520 / 12,800 / 11,550 / 15,584)는 후보를 고르는 데만 쓰고, 맵에 `layout=` 키를 두는 방식을 우선한다. 세 보고서가 일치한다.
    11,550 B는 Kyra만이 아니다. "30 B 헤더 + 8×4×4"로도 해석될 수 있다는 것을 이번에 확인했다(V14). 크기만으로 판별하면 위험하다.
    지금 SVFN/SCVMUNI 판별은 엔진 네 곳에 흩어져 있다([C]). 공용 팩토리(`glyph_source_file.cpp`)로 모으는 것이 좋다.
- **합성 시점**
  - 11,172 × 32 B = 357,504 B(349 KiB)다.
  - [G]는 로드할 때 전부 합성하라고 했다. [C]·[S]는 처음 쓸 때 합성하고 캐시하라고 했다.
  - **권고**: 기본은 **처음 쓸 때 합성 + 캐시**다. `glyph_source.h`는 non-const lookup으로 이미 이런 캐시를 허용하고, `glyph_source_svfn.cpp:114`가 참고 구현이다.
    DOS 포트의 메모리 제약을 생각하면 LRU 캐시를 둔다. PC 빌드에서는 전부 미리 합성하는 옵션을 둔다.
  - `row(cp, y)`가 행마다 다시 합성하면 안 된다. 한 번 합성한 글리프를 모든 행에 재사용한다([S]).
- **배율**: 합성한 원본 마스크에 정수배 최근접 확대를 적용한다. advance와 baseline도 같은 배율로 늘린다. 비정수 배율은 피한다. 세 보고서가 일치한다.
  [G]의 EPX/xBRZ 제안은 선택 사항이다.
- **커버리지**: 한글 11,172자 + 호환 자모 51자(초성 벌 0, 중성 벌 0 또는 1, 겹받침은 종성 벌 0 등 정책을 정해야 함). `.ENG`가 있으면 ASCII도 덮는다.
  기호와 한자는 기존 fallback 체인에 맡긴다(`glyph_source_fallback`, TTF, korean.fnt, HBIOS `$KSPHJ16`/HWP `HAJ16` 같은 완성형 기호·한자 셋).
- **대안(런타임 코드 0줄)**: 오프라인 도구가 8×4×4를 11,172자 SVFN(버전 2, cmap)으로 굽는다. 지금의 `tools/korean/mkfont.py`는 TTF → SVFN만 처리하므로 비트맵 입력 경로를 추가해야 한다. 두 방식은 함께 쓸 수 있다([C], [S]).

### 8.3 완성형 korean.fnt로 바꾸는 경로
- `FontKoreanWansung::loadData`(`graphics/korfont.cpp:425-443`)
  - 형식: 4 B 헤더 [미사용 1 B, shadow, width, height] + 글리프. 색인은 `(hi-0xB0)*94 + (lo-0xA1)`이므로 2,350자를 KS 순서로 쓴다(로더는 `kFontNumChars = 2530`개분을 읽으려 하지만 실제로 쓰는 것은 2,350개).
  - `english.fnt`: 4 B 헤더 [2 B, w, h] + 256 글리프. raw `.ENG`와 직접 호환되지 않는다([S]).
- 조합형 → korean.fnt 변환 절차: KS X 1001 2,350자 각각을 Unicode로 바꾸고, 그 Unicode에서 L/V/T를 구해 8×4×4 합성한다. 결과를 4 B 헤더 + 2,350×32 B로 쓴다.
  - 근거: 이 방식으로 합성한 HWP `HAN16_2` 글리프는 원래 완성형과 2,221/2,350자가 일치했다(V8). 품질은 폰트에 따라 다르다.
- **이미 완성형인 원본은 합성할 필요가 없다.** 아래 원본은 KS 순서 2,350자를 그대로 잘라 쓰면 된다.
  - HWP 3.0 `HAN16_n`의 앞 2,350자(72 B 헤더 다음)
  - HBIOS `$KMYZN24`의 글리프 360–2709(24×24로 crop)
  - 수집 노트가 정리한 태백 `GHAN24.PFT`, Win3.1K `.TBM`(반전 저장) 등(`notes/dos.md`, `notes/os.md`)
- 그러나 korean.fnt는 2,350자로 제한된다. 조합형 폰트의 장점(11,172자)을 살리려면 **SVM 형식**(`SCVM`/`SJIS` 매직, 11,184 슬롯 Unicode 순서, `korfont.cpp:291-345`)이나 SVFN으로 굽는 편이 낫다.
  korean.fnt 경로는 완성형만 읽는 구형 엔진 호환용으로만 권한다.

### 8.4 텍스트 경로 버그와 수정 과제 (트리 안, 검증됨)
1. **`decodeJohab`은 한글 영역만 처리한다.** `devtools/create_encoding/create_encoding.py:67-70`이 JOHAB.TXT에서 `HIGH: range(0x84, 0xD4)`만 싣고, `common/str-enc.cpp:226`은 `80*188`개만 읽는다. `convertJohabToUCSReal`(`str-enc.cpp:497-515`)은 lead가 0xD4 이상이면 0을 돌려준다.
   - 그래서 **기호(D9–DE)와 한자(E0–F9)가 모두 invalidCode가 된다.** `encodeJohab`도 기호와 한자를 만들지 못한다. 세 보고서가 일치하고, V16으로 확인했다.
   - 수정 방법: 1.5절의 산술식과 KS X 1001 표(CP949 표 재사용)를 쓴다.
2. 한글 음절과 51자모는 **산술로 디코드**해야 한다. 그러면 `encoding.dat` 없이도 동작한다.
   - 지금은 `encoding.dat`이 없으면 SCUMM이 경고를 내고 한글을 잃는다(`engines/scumm/hires_text.cpp:2183-2193`, 프로브 `\x88\x61` → U+AC00, `:2560`).
   - 근거는 [C]이고, 코드로 재확인했다.
3. **`text_layout.cpp:142-143`** `charLength`는 lead 0x84–0xF9를 모두 2바이트로 본다. 그래서 표준상 무효인 0xD4–0xD7, 0xDF와 0xD8(사용자 정의)도 2바이트로 소비한다.
   - 한편 `decodeJohab`은 0x81–0x83과 0xFA–0xFE도 2바이트로 소비한다. 두 코드의 기준이 다르다.
   - 수정 방법: {0x84–0xD3, 0xD8–0xDE, 0xE0–0xF9}로 통일한다([C], V16).
4. **`encodeJohab`의 경계 버그**: `str-enc.cpp:911` `if (point > 0x10000 …)`는 `point == 0x10000`을 통과시킨다. 그러면 크기 0x10000인 역표 `rt[0x10000]`을 읽어 1칸 범위를 넘는다. `>=`가 맞다([S]가 지적했고 코드로 확인함).
5. 0x5C(₩와 \) 정책을 정해야 한다([C], [S]).
6. Kyra `JohabFontLoK`의 `<=` off-by-one(6.1절). 지금은 무해하다.

### 8.5 조합형 텍스트를 쓰는 게임
- **확인됨**
  - Kyra LoK 한국어 DOS: 텍스트가 조합형이다(V15). 폰트는 MK15.BIT.
  - IHNM 한국어: 렌더러가 조합형 필드를 해석한다(`font.cpp:731`). 원본 텍스트 바이트는 직접 보지 못했다.
- **조합형이 아님**
  - Kyra HoF 팬번역: EUC-KR(V15)
  - SCI, SCUMM, AGI 팬번역: CP949/EUC-KR(`codepage_kr.h`)
- **후보**: 국산 DOS 게임 다수가 8×4×4 폰트를 싣고 있다(`notes/games.md`). 폰트가 조합형이면 내부 텍스트도 조합형일 가능성이 높지만, 텍스트 인코딩 자체는 **UNVERIFIED**이다.
  [G]가 든 어스토니시아, 망국원기, 삼국지 영걸전은 근거가 없다. KOEI 한글판은 `.16P` 완성형 폰트를 쓴다(`notes/games.md`). 따라서 KOEI 계열을 조합형으로 분류하는 것은 반증된다.

---

## 9. 열린 질문
1. 실제 `sbh1616.fnt`의 중성 벌 순서와 파일 크기. IHNM 코드의 ㄱ·ㅋ = 1/3과 비자모 126글리프를 파일로 확인해야 한다.
2. 한라프로, GSVIP, HECON, DKB16의 10×4×4·5×2×3·14×6×4 실제 배치와 선택표. 지금은 peremen의 보고만 있고 파일로 확인하지 못했다.
3. HWP 1.x SFT/PFT(1,193 / 980글리프)의 합성 규칙. HWP 2.x/3.x `.HFT`, 한메 `.FNT`, GSVIP `.GSF`, 이야기 `I.FNT`(LZEXE)의 압축 형식.
4. CP1361이 D8 영역과 `채움+채움+단자음` 16개를 받아들이는지(Windows 실측 필요). 삼보 TG·큐닉스·대우 조합형의 비한글 배치.
5. 단독 자모와 미완성 조합(첫가끝 자모열)을 어느 벌로 그릴지에 대한 정책.
6. Neo둥근모 원본(둥근모꼴) 공지의 원문. 단비 등 "퍼블릭 도메인"이라고 주장된 DOS 폰트의 근거.
7. 국산 DOS 게임(8×4×4 폰트를 싣는 것들)의 텍스트 인코딩과 ScummVM 지원 여부. 대부분 ScummVM 대상이 아니다.

---

## 부록 A. 검증 기록

스크립트는 모두 scratchpad(`/tmp/claude-1000/…/scratchpad/merge/`)에 두었다. 내려받은 파일을 읽는 Python은 모두 `python3 -I`로 실행했다. 내려받은 프로그램은 실행하지 않았다.

| # | 검사 | 방법 | 결과 |
|---|---|---|---|
| V1 | fviewer 중성 규칙 | `curl` raw `gcjjyy/fviewer/master/fviewer.cpp`(scratchpad/dl/fv)를 받아 `grep` | `:130 joong_type = ((choseong==1 \|\| choseong==16) ? 0 : 1) + (jong?2:0)`. ㄱ·ㅋ = 0. [S]의 "fviewer = IHNM 규칙"은 틀림 |
| V2 | iolo 생성기와 벌 동일성 | `generate_hangul_syllables.py:36-74` 읽기. `gk.py`로 11개 폰트의 중성 벌 0과 1 비교 | ㄱ·ㅋ → 0/2. 벌 0==1인 글리프 수: hanme·sam·thin 22/22, san 14/22, 나머지 17–21/22 |
| V3 | JOHAB.TXT | unicode.org에서 받아(scratchpad/dl/jt) 파싱 | 17,141항목. 한글 lead 영역 11,223 = 음절 11,172 + 자모 51. lead에 D4–D8·DF 없음. 0x5C → U+20A9. 0x8441·0x8442 없음. 음절 공식 불일치 0. E031 → U+4F3D(EUC CAA1, 42행), F9FE → FDFE. DAD4 → U+3164. CPython과의 차이 19개 = 0x8441, 0x8442–845D 단자음 16, D9E6 €, D9E7 ® |
| V4 | 완성형 → 조합형 기호·한자 공식 | EUC-KR A1–AC, CA–FD 전체를 공식과 CPython `johab` 인코딩으로 대조 | 5,824 일치. 불일치 51자는 전부 4행 현대 자모 |
| V5 | 8×4×4 원시 파일 크기 | `ls -l` iolo `src`, san `font-kor` | han_*.fnt 11개 = 11,520. asc 4,096(san 2,048). KOR 55개 = 11,520, 2개 = 12,800 |
| V6 | HBIOS·HMJ 구조 | `os.md` 확인 + 파이썬으로 잉크 행과 채움 슬롯 검사 | `$KMYZN24` 243,900 = 2,710×90, 잉크 3–26행. 글리프 161 = 중성 벌 0의 ㅏ, 360 = 가, 2709 = 힝(렌더). 16px·24px 모두 벌 0번 슬롯이 빔 |
| V7 | hanterm 배치 | `hanterm-xf-2.0.0/hfont.c`(cp949 → UTF-8) 읽기. `10-6-6/johabg16.bdf` 파싱·렌더. `8-4-4/*.bdf` 헤더 | johab-1: CHARS 529, ENC 0–528, 1 = ㄱ, 11 = ㄲ, 311 = ㅏ, 405 = 종성 ㄱ, 191–310도 채워짐. 선택표는 `fcon_map1/2`, `vow_base`, `vow_kind`, `lcon_kind`(ㄴ = 2), `lcon_map`. johab844는 ㄱ·ㅋ·채움 → 0. D8 → KS 41·94행. 8-4-4: 136개, ENC 0–359 |
| V8 | HWP HAN16 완성형 대 합성 | `comp.py` / `best.py`: 2,350자 각각을 두 규칙으로 합성해 같은 파일의 완성형과 비교하고, 해밍 거리로 최적 벌을 셈 | 정확 일치(0/2 규칙 대 1/3 규칙): HAN16_0 2,083 대 1,803, _1 1,788 대 1,180, _2 2,221 대 2,221, _3 375 대 357, _4 1,700 대 1,590. 두 벌이 다를 때 ㄱ·ㅋ은 0·2를, 그 밖과 ㄲ은 1·3을 고름. `$KMYZN24`는 정확 일치 0(완성형이 별도 디자인) |
| V9 | 벌 0과 1의 형태 | `HMJ.FNT`에서 고/노를 벌 0·1로 렌더. `$KMYZN24`에서 ㅗ와 초성 사이 최소 거리 측정 | HMJ: 벌 0과 1은 ㅗㅘㅙㅚㅛ만 다르고, 벌 0의 ㅗ 기둥이 2줄 더 김. 24px: 벌 1이 ㄴ(1 → 2px), ㅁ(2 → 3px)과의 간격을 더 둠. 벌 0 = ㄱ·ㅋ용 |
| V10 | IHNM 규칙의 이력 | `git log -L` 해당 줄, `git show 829525e2bd6` | 처음에는 `!(initial==2\|\|3\|\|17)`(표준). 829525e "to match the original"에서 `initial==2\|\|17` → +1로 바뀜 |
| V11 | san 10×4×4 / DKB2 / DKB3 | `src-c/HANDRV.C` 읽기 | `SetHan`: 12,800 → MB(10×4×4), 15,584 또는 11,520 → DKB2(8×4×4, 360글리프 읽음), 29,184 → DKB3(912). `Draw10_4_4`의 `cho[]`는 표준 받침 없음 표에 받침 있으면 +5, `mid[]`는 ㄱ·ㅋ = 0. 배치 200/288 오프셋. `SetDKB2Han`의 `movmem`으로 벌 6 → 7, 벌 7 → 8·9 복제 |
| V12 | HWP 3.0 FNT 헤더 | `struct.unpack('<6H', hdr[6:18])`, 이름을 조합형으로 디코드 | HAN16_0: w16 h16, 8/4/4, count 2710, "고딕체 16*16", 86,792 = 72 + 2,710×32. HAN24_0: 169,272 = 72 + 2,350×72(count는 2710). HAJ16_0: type 02, 4,888. ENG16_0: 147 × 16 B |
| V13 | HWP 1.20 SFT | `xxd`, 크기 계산 | `"HWP Font Editor v1.0"`, 38,208 = 32 + 1,193×32. [G]의 "SFT = 11,552 B 8×4×4" 반증 |
| V14 | MK15.BIT 해석 | `show.py`로 30 B 글리프와 "30 B 헤더 + 32 B" 두 방식으로 렌더. `lok_dos_korean.h` 표로 `createGlyph` 재구현. 11,172자 최대 인덱스 측정 | 30 B 해석이 맞음(0 = 빈 글리프, 1 = ㄱ, 11 = ㄲ, 191 = 중성 채움, 192 = ㅏ). 가·고·각·꽉·뷁·흙이 바르게 합성됨. 최대 인덱스 190/84/108(경계 191/85/109). 32 B 해석은 쓰레기. 블록 0 = 초성 |
| V15 | Kyra 텍스트 인코딩 | 리소스 헤더의 escape 문자열을 EUC-KR / 조합형으로 디코드 | LoK: 217/217 조합형. HoF: escape 문자열 36개 모두 EUC-KR이고 조합형 0(나머지 36개는 UTF-8 리터럴). `detection_tables.h:753` KOREAN.FNT 42,300 = 2,350×18 |
| V16 | 트리의 Johab 경로 | `create_encoding.py:67-70`, `str-enc.cpp:226,497-546,878-930`, `text_layout.cpp:142-143`, `hires_text.cpp:2183-2193,2545-2565` 읽기 | 8.4절 1–4항 확인 |
| V17 | DOSBox-X 조합형 | 웹 검색 | `dosv=ko`/TTF/CP949만 나옴. 8×4×4 언급 없음 |
| V18 | hanterm 라이선스 | WebFetch unifoundry.com/hangul/index.html, hangul-generation.html | "only licensed in Korea for use with Hanterm", "could not be licensed under the GPL". johab844 중성 규칙은 "채움·ㄱ·ㅋ이 첫 벌" |
| V19 | korean.fnt 로더 | `graphics/korfont.cpp:46-60,285-345,425-490`, `korfont.h:254-255` 읽기 | 8.3절 형식. SVM 우선, Wansung은 대체 경로 |
