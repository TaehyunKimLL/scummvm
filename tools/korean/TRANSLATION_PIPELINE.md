# 한국어 번역 파이프라인: 포맷, 도구, 패키징

이 문서는 SCI·SCUMM 두 엔진에서 원본 게임 리소스가 한국어 번역 파일이 되고,
그것이 다시 DOS(그리고 있다면 Windows) 패키지로 굳어지기까지의 전체 경로를
정리한 참조 문서다. 폰트 굽기와 결과 검증의 세부 옵션은 이미
`tools/korean/README.md`(맵/셀 크기/코드포인트 선택 규칙)와
`tools/korean/SCUMM_FONTS.md`(`.trs`에서 `--chars-from` 읽기), 엔진 쪽 설정은
`engines/scumm/HIRES_TEXT_SETUP.md`/`HIRES_TEXT.md`/`HIRES_TEXT_DECORATIONS.md`,
그리고 리더 자체는 `graphics/hires_text/README.md`가 자세히 다룬다. 이 문서는
그것들을 다시 쓰지 않고 링크만 걸며, 대신 이 문서에만 있는 것 — 리소스
포맷의 바이트 그레인, 탐지 규칙, 엔진별 파이프라인의 단계 순서, 포맷
지정자/제어 코드 처리 규칙, 그리고 라이선싱 — 을 채운다.

`hires_text_map=` 등 맵/ini 키 자체는 현재
`docs/superpowers/specs/2026-09-30-hires-config-unify-design.md`에서 재설계가
진행 중이다(이 브랜치에는 아직 파일이 없을 수 있음 — 진행 중인 스펙이므로).
그 결과가 나오면 옛 키 이름은 바뀔 수 있으므로, 이 문서는 폰트/맵을 포인터로만
다루고 개별 키를 문서화하지 않는다.

## 목차

1. 포맷
   1. SCUMM `korean.trs` (SCVMTRS)
   2. SCI 메시지 리소스: RESOURCE.MSG + MESSAGE.MAP, EUC-KR vs UTF-8, SCI-KO.STR
   3. SCI0 한국어 패치 (원본 인터프리터용, Camelot)
   4. 더 오래된 팬패치 포맷들 (text.map/Text.res, TEXT.052/SCRIPT.052, 게임별 XML)
   5. 탐지: detection_tables.h의 KO_KOR, md5, language=/text_encoding=
   6. 폰트와 맵 (포인터만)
2. 엔진별 파이프라인
   1. SCUMM
   2. SCI
3. 포맷 지정자와 제어 코드
   1. SCI: kFormat과 메시지 내 제어 시퀀스
   2. SCUMM: 0xFF/0xFE 이스케이프
   3. AGS: 간단히
4. 공백(gap)과 할 일
5. 라이선싱 노트

---

## 1. 포맷

### 1.1 SCUMM `korean.trs` (SCVMTRS 번들)

`korean.trs`는 게임 언어와 무관한 컨테이너 포맷이다 — 어떤 언어인지는 파일
이름이 말한다. 이 이름 규칙과 로더가 같은 규칙을 쓴다는 보장은
`engines/scumm/trs_bundle.h`에 있다:

- `getTrsBundleName()`/`getTrsBundleNames()` (trs_bundle.h:47-70): 한국어는
  `korean.trs`, 그 밖의 언어는 `<ScummVM 언어 코드>.trs` (`de.trs`, `ja.trs`
  등). 탐지와 엔진 둘 다 언어 코드 파일을 먼저 찾고, 한국어에 한해 `korean.trs`
  로 대체한다.
- `getTrsBundleLanguage()` (trs_bundle.h:86-98): 파일 이름만 보고 거꾸로
  언어를 알아낸다. `korean` 이라는 stem은 무조건 `KO_KOR`.
- `trsBodyIsUtf8()` (trs_bundle.h:78-80): 본문(룸 테이블 뒤, 첫 문자열 앞)이
  UTF-8 BOM(`EF BB BF`)으로 시작하는지만 본다 — 이것이 "V1"(레거시 CP949)과
  "V2"(UTF-8) 두 세대를 가르는 유일한 신호다. 매직 자체(`SCVMTRS `)는 두
  세대에서 동일하다.

**바이트 레이아웃** (파일 순서대로; 정본은 `harness/tools/trslib.py:1-29`의
문서 주석이고, `tools/korean/scumm_trs_from_patch.py:29-63`의 `parse_trs()`가
같은 구조를 다시 구현해 읽는다):

```
"SCVMTRS "                              8바이트 매직
uint16   numTranslatedLines             줄 개수 n
n × { uint16 idx, uint32 originalOff,   줄 색인: idx는 순열,
      uint32 translatedOff }            원문/번역문 오프셋(파일 절대)
uint8    numTranslatedRoom
per room { uint8 roomId, uint16 numScript,
           per script { uint32 scriptKey, uint16 left, uint16 right } }
body     (선택적으로 EF BB BF로 시작 = UTF-8) 문자열들
```

`idx`는 순열이며 두 번째 검색 순서(`_languageLineIndex[idx] = i`)를 만드는 데
쓰인다: 엔진은 `resStrLen()` 바이트 단위로 `memcmp` 이분 탐색을 하므로 파일
순서·색인 순서 둘 다 정렬돼 있어야 한다(trslib.py:16-25). `resStrLen()`은
단순 `strlen`이 아니다 — `0xFF`는 SCUMM 이스케이프를 열고, 그 피연산자
바이트 자체가 `0x00`일 수 있어서, 문자열 끝은 "이스케이프의 일부가 아닌
`0x00`"이다(§3.2 참고).

실제 예시로 `gamedata/mi2kor/korean.trs`(829,789바이트)를 열어 보면
`53 43 56 4D 54 52 53 20` 매직 뒤 `numLines=8780`, `nroom=82`, 본문 시작
오프셋은 약 87,810바이트다 — BOM이 없으므로 레거시 CP949(V1) 본문이다.

**엔진이 파일을 찾고 판단하는 방식** — `ScummEngine::probeLanguageBundle()`
(engines/scumm/string.cpp:2413-2483):

1. `getTrsBundleNames()`로 얻은 이름들을 게임 디렉터리에서 순서대로 열어
   본다(The Dig/COMI는 제외 — 자기 `language.bnd/.tab`을 읽는다).
2. `parseTrsHeader()`(text_utf8.h:170-173)로 헤더가 읽히는지 확인.
3. `decideTrsUtf8()`(text_utf8.h:176-182)로 UTF-8 여부 결정: BOM이 있으면
   UTF-8, `text_encoding=utf8`이 명시돼도 UTF-8, 아니면 CP949(레거시)로 본다.
   BOM 없이 유효한 UTF-8처럼 "보이기만" 하면 경고만 하고 UTF-8로 취급하지
   않는다(CP949 쌍이 우연히 유효한 UTF-8이 될 수 있어서).
4. UTF-8이면 이 릴리스의 렌더러가 코드포인트 단위 렌더러(v1-v6, PC 계열)인지
   확인 — FM-TOWNS/PC-Engine/Mac 등은 지원 밖이라 번역을 무시하고 경고.
5. hi-res 텍스트가 켜져 있으면 hi-res 레이어가 UTF-8을 그대로 그리고, 꺼져
   있으면 `legacyTextPage()`로 언어의 레거시 코드페이지로 트랜스코딩해서
   게임 자체 CJK 폰트로 그린다(코드페이지가 없으면 `?`로 표시하고 경고).

**인코딩**: CP949(=본질적으로 EUC-KR 상위집합, §1.2 참고) 또는 UTF-8(BOM
필수). 본문 안에서 완성형 한글 음절·자모·KS X 1001 기호는 문자 단위로
CP949 ↔ UTF-8 변환되고, SCUMM 자체 이스케이프(`0xFF`/`0xFE`)와 게임 고유
글리프 바이트(MI1의 `0xFA` 전각 공백, "Melee"의 `0x88`/`0x82`)는 원본 그대로
남는다 — 렌더러가 `kRawGameByteBase`로 게임 폰트를 이용해 그린다
(`tools/korean/README.md` "A UTF-8 korean.trs for a SCUMM release" 절 참고).

**쓰는 게임**: 기본으로 MI2(`gamedata/mi2kor/korean.trs`, 원조 한국어 팬번역
그대로)와, `scumm_trs_from_patch.py`가 다른 릴리스(예: MI1 VGA 플로피,
`000.LFL` md5 `15e03ffb...`)를 위해 새로 합성한 UTF-8 번들이 있다 — MI1의
DUMB 원조 패치는 `korean.trs`가 아니라 `000.LFL`/`90N.LFL`/`DISK0N.LEC`를
XOR 0x69로 직접 고쳐 쓰는 v3/v4 방식이라(§1.4 인접 구절 참고), 그 자체로는
`.trs` 파일이 없다.

### 1.2 SCI 메시지 리소스: RESOURCE.MSG + MESSAGE.MAP, EUC-KR vs UTF-8, SCI-KO.STR

**RESOURCE.MSG(볼륨) + MESSAGE.MAP(맵)** 은 SCI 팬패치가 텍스트 리소스
전체를 하나의 볼륨으로 묶어 배포할 때 쓰는 모양이다. 엔진이 이걸 특별
취급하는 지점은 `ResourceManager::isKoreanMessageMap()`
(engines/sci/resource/resource.cpp:3118-3120):

```cpp
bool ResourceManager::isKoreanMessageMap(ResourceSource *source) {
	return source->getLocationName() == "message.map" && g_sci && g_sci->getLanguage() == Common::KO_KOR;
}
```

파일 이름이 정확히 `message.map`이고 현재 언어가 `KO_KOR`일 때만 특별
경로를 탄다. `readResourceMapSCI1()`(resource.cpp:1933-2000)이 이 맵을
SCI1식 엔트리 크기(`SCI1_RESMAP_ENTRIES_SIZE`)로 강제하고, 오프셋을 "4바이트
전부가 평범한 오프셋"으로 해석한다(SCI11의 볼륨 비트 인코딩을 쓰지 않음).
**바이트 레이아웃**(`tools/korean/packvol.py:117-133`의 `build_msg_map()`이
쓰는 것과 같고, 위 리더가 읽는 것과 정확히 대응):

```
디렉터리: { u8 (0x80|type), u16 offset } 를 타입마다 하나씩, 오름차순 타입,
           마지막은 { u8 0xFF, u16 totalSize }
타입별 본문: { u16 number, u32 offset }  (offset은 RESOURCE.MSG 안의 절대 위치)
```

RESOURCE.MSG 자체는 SCI0 볼륨 포맷을 그대로 쓴다(resource.cpp:2266-2271,
2285-2299 `readResourceInfo()`와 `packvol.py:101-102`의 `vol_entry()`가
서로 대응):

```
u16 resId(= (type<<11)|number), u16 packedSize+4, u16 unpackedSize, u16 compression(=0),
그 뒤 압축 없는 리소스 바이트
```

`RESOURCE.MAP`(영문 원본의 것)은 이 패치와 무관하게 바이트 단위로 동일하게
유지된다 — 그래서 md5 기반 탐지가 안 깨진다(packvol.py:15-19, README.md
"Packing a translation's patch files" 절). `language=ko`가 걸린 타깃에서
RESOURCE.MSG/MESSAGE.MAP의 엔트리가 게임 자체 볼륨의 리소스를 대체하지만,
낱개 패치 파일(loose `TEXT.NNN` 등)이 있으면 그게 항상 이긴다.

**낱개 패치 파일 `TEXT.nnn`**(UTF-8 팬번역이 리소스 하나하나를 이렇게
대체) 은 `kResourceHeaderSize = 2`(resource.h:51)바이트 헤더 — `type`,
그 다음 "추가 헤더 바이트 수"(TEXT면 0) — 뒤에 NUL로 구분된 UTF-8 문자열이
원본 TEXT 리소스와 같은 인덱스로 이어진다(`processPatch()`,
resource.cpp:1574-1627+). `packvol.py`의 도움말이 같은 구조를 요약한다
(README.md "The file format is told apart by name, not content" 절).

**레거시 EUC-KR vs 완전 UTF-8**: `ADGF_UTF8I18N` 플래그
(engines/sci/detection.h:52-64)가 "이 탐지 엔트리의 데이터가 UTF-8
번역"이라고 선언하는 유일한 신호다. 주석을 그대로 옮기면:

> This is a statement about the data, not the language. A Korean entry
> without it is a code-page translation - cp949 resources, as every
> upstream Korean fan patch is - and must keep byte semantics: cp949 hangul
> decoded as UTF-8 comes out as stray bytes ... or as the wrong character.

즉 `KO_KOR`이면서 `ADGF_UTF8I18N`이 없는 엔트리는 전부 CP949(=엔진이
"euc-kr/cp949"를 사실상 동의어로 취급한다, `textencoding.h:43`)이고, 있으면
UTF-8이다. 실례:

- `engines/sci/detection_tables.h:1682-1688` — KQ1 SCI 리메이크, 영문
  볼륨 위에 `text.000`(UTF-8 패치)이 얹힌 엔트리. `ADGF_UTF8I18N` 있음.
- `engines/sci/detection_tables.h:1691-1699` — 같은 KQ1이지만
  `resource.msg`(md5 `6f44b032...`, 2,948,975바이트) 하나로 전체를 실은
  레거시 엔트리. `ADGF_NO_FLAGS` — CP949.
- `harness/dos/release/build_sci_lb2.py:1-38`이 실제로 포장하는 LB2(Dagger
  of Amon-Ra) 팬패치는 2014년 원조 그대로 `SIERRA/LB2CD/message.map` +
  `resource.msg`를 재인코딩 없이 쓴다 — "native CP949 RESOURCE.MSG"의
  살아있는 예.

**SCI-KO.STR 스크립트 문자열**: 패치가 닿지 않는 곳 — 스크립트 자체 문자열
블록 안의, 절대 오프셋(`lofsa`)으로 참조되는 문자열(인벤토리 이름, 파서
응답 등) — 을 위한 런타임 치환 테이블. `engines/sci/engine/translation.h`의
`ScriptStrings` 클래스 문서 주석(1-65)이 정본:

```
script <TAB> id [<TAB> room] <TAB> text
```

UTF-8, `#` 줄 주석, 텍스트 안의 `\n`은 개행. 키는 `(script, id, room)` —
`id`는 로드 시 `Script::identifyOffsets()`가 매기고 표시 시
`SegManager::stringKey()`가 보고하는 바로 그 번호. room이 있으면 그 방에서만
적용되고 room 없는 엔트리보다 우선한다. 파일 이름 `sci-<lang>.str`의
`<lang>`은 `Common::getLanguageCode()`가 주는 ScummVM 코드(`ko`) —
KQ1 Korean 패키지의 `SCI-KO.STR`이 실례(LB1 패키지에도 동일).

### 1.3 SCI0 한국어 패치(원본 인터프리터용) — Camelot

일부 SCI0 팬 빌드는 ScummVM이 아니라 **Sierra 자신의 SCIV.EXE**(패치된
것) 위에서 돈다: Conquests of Camelot KR beta 3/5는 영문
`RESOURCE.001`-`004`를 손대지 않고 `RESOURCE.005`를 추가해 번역된 TEXT·
SCRIPT 리소스(EUC-KR)와 한글 폰트 뱅크를 싣고, `RESOURCE.MAP`을 다시 쓴다
(`gamedata/ConquestsOfCamelot_DOS_KR_BETA5_20260912.zip`이 실물 — `sciv.exe`,
`resource.001-005`, `resource.map`을 담고 있다).

`tools/korean/sci0_kr_extract.py`(1-49행 docstring)가 이런 빌드를 우리
포크의 UTF-8 번역 파일로 바꿔 영문 원본 위에 얹을 수 있게 한다. 세 명령:

```sh
tools/korean/sci0_kr_extract.py rebuild-map KRDIR resource.map   # 영문 맵 복원
tools/korean/sci0_kr_extract.py extract ENDIR KRDIR OUTDIR       # text.NNN + sci-ko.str
tools/korean/sci0_kr_extract.py script-strings ENDIR             # = dump_script_strings
```

- `rebuild-map`: 새 빌드가 옮겨놓은 모든 엔트리를 SCI0 헤더를 훑어 찾은
  옛 볼륨 위치로 되돌리고, 새 볼륨에만 있는 엔트리는 버린다. Camelot의
  경우 소매판 맵을 바이트 단위로 재현한다(md5 `26d6030d...`, 7278바이트;
  **탐지는 첫 5000바이트만 md5** — 아래 §1.5 — 하므로 그 부분의 md5는
  `95eca399...`).
- `extract`: 새 볼륨의 TEXT 리소스를 cp949로 디코드해 `text.NNN` 패치
  (`0x83 0x00` + NUL 구분 UTF-8)로 쓰고, 문자열 개수 차이를 보고한다.
  스크립트는 `Script::identifyOffsets()`와 똑같이 번호를 매긴 뒤(엔진의
  `dump_script_strings` 출력과 대조 확인) 영문 스크립트의 비어있지 않은
  문자열과 순서대로 짝짓는다 — 빌드가 문자열 블록을 NUL로 패딩해서 NUL마다
  자기 id를 가진 빈 문자열이 되어버리므로 한국어 쪽 id는 그대로 못 쓴다
  (Camelot 스크립트 3: 영문 19개 vs 빌드 984개). 바뀐 문자열은 영문 id로
  `sci-ko.str`에 쓰고, CR LF는 한 개의 `\n`(SCI0 텍스트 코드에서 개행 하나)
  으로 합친다. 비어있지 않은 개수가 다르면 diff 정렬로 폴백하고 못 맞춘
  것은 나열한다.
- 글리프 검사로 인코딩을 재확인한다: 빌드가 추가한 폰트는 EUC-KR 리드
  바이트(`0xB0`-`0xC8`)당 하나씩, 25뱅크 단위로 묶여 있고(뱅크=선행바이트
  런, 글리프 인덱스=트레일 바이트), 문자열이 쓰는 모든 음절에 잉크가
  있어야 한다. Camelot: 뱅크 500-524(8px), 525-549(9px, 아웃라인 인트로
  텍스트), 1135음절, 누락·미사용 0개.

### 1.4 더 오래된 팬패치 포맷들 — 역사적 형태, 우리 도구는 소비하지 않음

`gamedata/` 안에는 지금 파이프라인이 직접 읽지 않는 옛 한국어 팬 번역
포맷들이 원본 그대로 남아 있다. 출처 확인용으로만 정리한다 — 아래 파일들을
파싱하는 코드는 `tools/korean/`에도 `engines/`에도 없다:

- **`text.map`/`Text.res`(KQ5)** — `gamedata/KQ5.rar`의
  `SIERRA/KQ5/text.map`(732바이트)·`Text.res`(93,930바이트). `text.map`은
  6바이트 엔트리(`u16 number, u32 offset` 꼴 — SCI 자신의 RESOURCE.MAP과
  같은 문법이지만 독자적인 파일이며 타입 번호 3=text 하나만 담는다)가
  이어지는 색인이고, `Text.res`는 EUC-KR 원문 바이트를 그대로 담은 본문
  볼륨이다. 이 짝은 그 옛 "ScummVM Kor. Project"가 자체 빌드한
  `scummvm.exe`용이며, 우리 포크의 `ResourceManager`는 이 파일명·포맷을
  전혀 인식하지 않는다.
- **`TEXT.052`/`SCRIPT.052`(LB1 레거시)** — 지금 LB1 패키지의
  `GAMES/LB1/`(영문) 쪽에 그대로 들어 있는, packvol.py로 아직 묶이지 않은
  형태의 낱개 SCI0 패치 파일 이름 예시(§1.2의 `TEXT.nnn` 규약과 같은
  포맷, 번호만 052). `GAMES/LB1KO/`(한국어)는 이제 같은 내용을
  RESOURCE.MSG+MESSAGE.MAP으로 묶어서 담는다 — 두 배치 전략을 나란히
  보여주는 예다(§2.2, dist zip 비교 참고).
- **게임별 XML(구 Korean 포크)** — `key`=영문, `value`=한국어인
  `<SentenceList>` XML. 예: 최상위 `gamedata/kq1.xml`(76KB), 그리고
  `gamedata/SQ4_KoreanPatch.rar`(7-Zip으로 읽음) 속
  `SQ4_KoreanPatch/sq4.xml`(4658바이트):

  ```xml
  <?xml version="1.0" encoding="utf-8"?>
  <sq4.xml type="Struct">
    <SentenceList type="associative_array">
      <item type="pair">
        <key type="string" value="Skip it" />
        <value type="string" value="스킵한다" />
      </item>
      ...
  ```

  이 XML은 (SCUMM의 `.trs`처럼 오프셋/컨텍스트가 아니라) **영문 문자열
  전체를 키로 쓰는 평문 사전**이다 — 문자열이 조금만 달라도(공백, 줄바꿈)
  매치가 깨진다. `gamedata/LauraBow2_KoreanPatch.zip` 안에는 같은 구조의
  `fairytale.xml`/`sq1.xml`과, 여러 게임의 `message.map`+`resource.msg`
  (LB2CD, GK1CD, FPFPCD) 및 `Text.MAP`+`Text.Res`(MOTHERGOOSE, SQ1) 쌍이
  함께 들어 있다 — 그 옛 포크 하나가 여러 포맷을 게임별로 섞어 썼다는
  증거. 이 zip에는 `MT32_CONTROL.ROM`/`MT32_PCM.ROM`/`CM32L_*.ROM`도 그대로
  들어 있다 — §5에서 다시 나오는, 우리가 절대 하지 않는 일의 실례다.

  이 XML 사전을 우리 포맷(korean.trs나 TEXT.nnn/SCI-KO.STR)으로 자동
  변환하는 도구는 없다(§4 gap).

### 1.5 탐지: KO_KOR 엔트리, md5, language=/text_encoding=

**SCI**: `engines/sci/detection_tables.h`의 `KO_KOR` 엔트리들이 전부
`AD_LISTEND`로 파일 목록·md5·크기를 나열한 `ADGameDescription` 매크로다
(예시는 §1.2에 인용한 KQ1 두 엔트리). 이 md5는 파일 **전체**가 아니라
`AdvancedMetaEngineDetection`의 기본값인 **첫 5000바이트**로 계산된다
(`engines/advancedDetector.cpp:994`의 `_md5Bytes = 5000;` — SCI가 이 기본을
오버라이드하지 않으므로 전 SCI 게임에 적용). 그래서 `sci0_kr_extract.py
rebuild-map`이 "맵을 바이트 단위로 재현"했다고 말할 때도, 탐지가 실제로
보는 건 그 앞 5000바이트뿐이다(§1.3).

탐지 엔트리가 어떤 언어·어떤 데이터 형태인지는 세 가지가 함께 정한다:

1. `Common::KO_KOR` — 이 엔트리가 매치하면 언어가 한국어로 정해진다.
2. `ADGF_UTF8I18N`(detection.h:52-64) — 있으면 그 데이터가 UTF-8(TEXT.NNN
   패치/SCI-KO.STR), 없으면 CP949(RESOURCE.MSG 전체 교체 등).
3. `sci.h:249`의 주석대로, ADGF_UTF8I18N이나 이용자가 명시한 `language=`가
   있을 때만 UTF-8/한국어 경로를 타고, `text_encoding=`(sci.cpp:150-160,
   1063-1119)이 있으면 `language=`/탐지 결과 어느 쪽보다도 우선한다
   (`euc-kr`/`cp949`/`utf8`/`utf-8`/`ascii`; `textencoding.h:34-70`).

**SCUMM**: 탐지 단계는 게임 폴더에서 `.trs` 파일 이름만으로 언어를
알아낸다 — `detectLanguageBundle()`(engines/scumm/detection_internal.h:220-230)
이 `getTrsBundleLanguage()`(§1.1)를 그대로 호출한다. 런타임에
`ScummEngine::probeLanguageBundle()`이 다시 열어 UTF-8/레거시를 가른다
(§1.1). SCUMM 탐지에도 md5가 쓰이지만(개별 게임의 `.000`/`.001`/`000.LFL`
서명), Korean 관련 판단 자체는 md5가 아니라 `.trs` 파일 존재 여부로 갈린다.

**AGS**: `engines/ags/engine/ac/translation.cpp:63-84`가 `.tra` 번역
파일의 인코딩을 `text_encoding=`(euc-kr/cp949/utf8/utf-8/ascii)으로 강제
받거나, 이름이 정확히 `korean`이면 기본을 EUC-KR로 추정한다 — 업스트림
AGS는 이름 없는 `.tra`를 ASCII로 가정하는데, 한국어 팬 번역이 인코딩을
선언하지 않은 EUC-KR `.tra`를 배포하기 때문에 생긴 장치다.

### 1.6 폰트와 맵 — 포인터만

이 문서는 hi-res 텍스트의 SVF 폰트 바이너리 포맷과 `HIRESTXT.MAP`/게임별
`.MAP`(버전 2)의 INI 키(`[map]`, `[render]`, `[fonts]`, `[font.N]`, `[glyphs]` 등)를
상세히 다루지 않는다. 정본은:

- `graphics/hires_text/README.md` — 맵 리더, 비트맵/TrueType 소스, 글리프
  렌더링, 커버리지 감마 등.
- `engines/scumm/HIRES_TEXT_SETUP.md` / `HIRES_TEXT.md` /
  `HIRES_TEXT_DECORATIONS.md` — SCUMM 쪽 설정·진단.
- `tools/korean/README.md` — SVF를 굽는 `mkfont.py` 옵션 전체와 셀 크기
  규칙.

이 키들은 지금 `docs/superpowers/specs/2026-09-30-hires-config-unify-design.md`
에서 재설계가 진행 중이므로, 옛 키(`hires_text_map=` 등)를 새로 배우거나
새 자료에 박아 넣기 전에 그 스펙을 먼저 확인할 것. SCI 쪽에는 이 외에도
`.uni` 번들(`sci.uni`/`korean.uni`/`towns.uni`, `engines/sci/graphics/cache.cpp:420`,
`fontunicode.cpp:71`)이라는 별도의 폰트 자산 파일이 있는데, 이 역시 폰트/맵
쪽 세부이므로 포인터만 남긴다.

---

## 2. 엔진별 파이프라인

### 2.1 SCUMM

**(a) 원본 리소스·텍스트 추출**

| 도구 | 무엇을 하는가 | 상태 |
|---|---|---|
| `tools/korean/scummtext.py` | v3/v4 EUC-KR 패치(`DISK0N.LEC`/`000.LFL`/`90N.LFL`, XOR `0x69`)에서 한글이 섞인 NUL 종료 문자열만 뽑아 `mkfont.py --chars-from`용 텍스트로 저장. `scummtext.py <gamedir> <out.txt>`(1-4행) | 유지보수 중 |
| `tools/korean/scummscript.py` | v4(`000.LFL`+`DISK0N.LEC`, XOR `0x69`, 6바이트 리틀엔디언 블록 헤더)나 v5(`<name>.000/.001`, LECF) 게임의 스크립트를 옵코드 단위로 걸어 문자열을 `(room, where, script)` 컨텍스트와 함께 나열. 점프를 따라가지 않고 블록을 선형으로 읽는다(README.md "scummscript.py" 절). `scummscript.py <gamedir> [--v5 NAME] [--limit N]` | 유지보수 중, `scumm_trs_from_patch.py`가 라이브러리로 import |
| `tools/korean/findtext.py` | **리소스 추출 도구가 아니다** — 캡처 이미지(스크린샷) 한 장에서 "텍스트처럼 보이는 색"의 픽셀 행·열 범위를 찾아주는 화면 QA 도구. 두 캡처가 안 맞을 때(자막이 다른 y좌표에 있을 때) 쓴다. `findtext.py <capture.png>` | 유지보수 중(QA용) |
| `tools/korean/sci0_kr_extract.py` | SCI 전용(§1.3). SCUMM에는 쓰지 않는다. | — |

**(b) 번역 파일 만들기/변환하기**

`tools/korean/scumm_trs_from_patch.py`가 SCUMM v4/v5 영문 릴리스용 UTF-8
`korean.trs`를 **다른 빌드용으로 이미 있는 번역**에서 만들어낸다(README.md
"A UTF-8 korean.trs for a SCUMM release from existing translations" 절이
정본; 여기서는 입출력·상태만 요약):

```sh
tools/korean/scumm_trs_from_patch.py --english mi1vga \
    --trs  'ScummVM-Kor-Trs/.../korean.trs' \
    --trs-data mi1ute \
    --patch mi1kor \
    -o mi1vga/korean.trs --table provenance.tsv --report stats.txt
```

- 입력: `--english`(대상 릴리스의 게임 폴더, 키를 만드는 기준), `--trs`(다른
  릴리스의 완성된 SCVMTRS 번들 — 1순위 출처), `--trs-data`(그 번들이
  실제로 맞는지 검증할 그 릴리스의 스크립트), `--patch`(대체 출처 — 같은
  게임의 옛 EUC-KR 팬패치 폴더).
- 출력: `-o`로 지정한 새 `korean.trs`(UTF-8, BOM), `--table`(TSV: room,
  where, script, kind, provenance, 영문, 한국어, 패치 원문), `--report`
  (통계·미매치·퍼지 매칭 내역).
- 핵심 옵션: `--no-fuzzy`(퍼지 매칭 끔), 매칭 실패 시 영문 유지.
- 상태: 유지보수 중, `test_scumm_trs.py`(합성 번들 기반 단위 테스트,
  `python3 tools/korean/test_scumm_trs.py`)로 커버.
- **알려진 한계**: DUMB 패치의 "목적어+조사" 문장줄
  (`FF 05 6B 80`, "을/를 향해")은 UTF-8 번들에서 조사가 사라진다(§3.2).

번역 파일을 **처음부터**(스프레드시트나 손번역에서) 만드는 전용 생성기는
`tools/korean/`에 없다 — §4 gap. `harness/tools/trslib.py`가 같은 포맷을
읽고 쓰는 라이브러리라서(1-29행 문서 주석), 새 도구를 짤 때 재사용할 수
있는 기반은 있다.

**문자 커버리지 검사**: `--chars-from`으로 번역이 실제 쓰는 코드포인트를
모으고 `--limit`으로 프리셋 문자집합과 교집합을 취하는 방식은 §1.6이
가리키는 `tools/korean/README.md`("Only what a game uses" 절)와
`SCUMM_FONTS.md`("mkfont.py --chars-from *.trs" 절, `.trs`가
`--chars-from` 입력으로 인식되는 규칙)에 있다. SCUMM에서 쓰는 게이트는
아래 (c)의 2350 게이트와 같다.

**(c) 폰트 굽기**

`tools/korean/bake-scumm-fonts.sh PLAN.TSV GAMEDIR OUTDIR CHARS
[EXTRA_CHARS_FROM] [EXTRA_LIMIT]`(1-13행)가 `mkfont.py`를 감싸 게임별 폰트
세트를 굽는다. `PLAN.TSV`의 `ttf` 필드가 `$FONTS`로 시작하면 `FONTS`
환경변수(기본 `~/scummvm-i18n/fonts`)로 치환된다. 기본 `--limit`은
`ascii,ksx1001-nohanja`(2350 게이트, `bake-scumm-fonts.sh:9-11` 주석)이고,
`EXTRA_LIMIT`으로 확장할 수 있다. 자세한 셀 크기 규칙·`--fit-cell`/
`--clip-cell` 트레이드오프는 `SCUMM_FONTS.md`/`README.md`에 있다.

**(d) Linux/Windows에서 테스트**

`tools/korean/README.md`의 "Checking the result"/"Comparing against the
original"/"Regression against another build" 절이 정본이며 링크만 건다:
`fontcheck.sh`, `textlog.sh`, `coverrun.sh`, `findscene.sh`, `sceneab.sh`,
`abrank.py`, `fbdump.sh`, `fbcompare.sh`, `noisefloor.sh`, `xvfb.sh`,
`shot.sh`. 테스트 하네스가 자주 걸려 넘어지는 함정 6가지(INI 주석 문법,
SDL 윈도 클래스, `extrapath`, Xvfb 크기, `autosave_period`, 디스플레이/임시
파일 충돌)도 README.md "Pitfalls" 절에 정리돼 있다. `tools/korean/inifix.py`
(1-12행)는 시험용 `scummvm.ini`에 `extrapath`/타깃/로그/GUI 설정을 강제로
넣어주는 유틸(직접 만든 heredoc보다 안전). Windows 전용 테스트 절차는
이 트리에 따로 없다 — DOSBox-X/DOSBox Staging 기반 검증(README.TXT
"Tested under" 절)이 사실상의 Windows 대역이다.

**(e) DOS 패키징**

`harness/dos/release/build_scumm.py`(1-260행+)이 MI1/MI2 SCUMM.EXE
개인용 릴리스 zip을 만든다:

```sh
build_scumm.py [mi1|mi2|all] --bin DIR [--meta DIR] [--dist DIR] [--work DIR]
```

- 입력: `--bin`(`backends/platform/dos/build-dos.sh scumm`이 만든 스테이징
  — `SCUMM.EXE`, `CWSDPMI.EXE`, `DATA/`, DJGPP strip까지 끝낸 상태),
  `--static`(이전 패키지의 `COPYING`/`SDL3.TXT`/`CWSDPMI.DOC`),
  `--meta`(DOSBox-X에서 뽑은 `VERSION.TXT`/`HELP.TXT`), 게임 데이터는
  `~/work/scummvm/gamedata`(`mi1kor`, `mi2kor`)에서 읽는다(2-16행).
- 출력: `--dist`(기본 `~/work/scummvm/dist`)에 zip. `MI1.INI`/`README.TXT`/
  `KOREAN.TXT` 등은 `MI1`/`MI2` 딕셔너리(52-119행)와 `README.scumm.tmpl`/
  `KOREAN.scumm.tmpl` 템플릿으로부터 생성.
- 핵심 데이터: 게임마다 `targets`(타깃 이름, gameid, 설명, `language=`,
  `hires_text_map=`, MI1 여부) 목록 하나로 `SCUMMVM.INI`·README 본문·배치
  파일을 전부 도출한다(`ini_text()`, 128-140행).
- 상태: 유지보수 중 — MI1/MI2 두 게임에 하드코딩(`GAMES = {"mi1": MI1,
  "mi2": MI2}`, 119행). 다른 SCUMM 게임을 추가하려면 이 딕셔너리 구조를
  본떠 새 항목을 만들어야 한다(§4 gap: 일반화된 "새 게임" 경로 없음).

**Windows 패키징**: `harness/dos/release/`에도, 조사한 `harness/` 루트에도
Windows용 SCUMM 배포 생성기는 없다 — §4 gap.

### 2.2 SCI

**(a) 원본 리소스·텍스트 추출**

| 도구 | 무엇을 하는가 | 상태 |
|---|---|---|
| `tools/korean/sci0_kr_extract.py` | §1.3. `rebuild-map`/`extract`/`script-strings` 세 명령. | 유지보수 중 |
| 엔진 내장 `dump_script_strings` 디버그 명령 | `sci0_kr_extract.py script-strings`가 재현하는 번호 매기기의 기준점(README.md, sci0_kr_extract.py 12-15행 docstring). 별도 파이썬 도구가 아니라 엔진 디버거 명령. | — |

SCUMM의 `scummtext.py`/`scummscript.py`에 대응하는, "SCI 게임의 원본
TEXT/SCRIPT를 훑어 번역 대상 문자열 목록을 뽑는" 범용 도구는 `sci0_kr_extract.py
script-strings`/`extract` 두 서브커맨드가 사실상 전부다 — SCI1 이상
게임(예: LB1, KQ1 SCI 리메이크)에서 새로 텍스트를 뽑아내는 별도 스크립트는
없다(이 게임들의 번역은 지금까지 전부 기존 팬패치의 RESOURCE.MSG를
재포장하거나 손으로 만든 TEXT.NNN/SCI-KO.STR을 가져다 썼다 — §4 gap).

**(b) 번역 파일 만들기/변환하기**

세 가지 경로가 실제로 쓰였다:

1. **기존 UTF-8 낱개 패치를 그대로/재포장** — KQ1: 팬패치가 이미
   `TEXT.000` 등 UTF-8 `TEXT.NNN`으로 와 있으면 그대로 쓰거나
   `packvol.py`로 묶는다.
2. **레거시 CP949 RESOURCE.MSG를 그대로 가져오기** — LB2:
   `build_sci_lb2.py`가 `LauraBow2_KoreanPatch.zip`의
   `SIERRA/LB2CD/message.map`+`resource.msg`만 뽑아 쓴다(재인코딩 없음,
   build_sci_lb2.py 14-20행). 이런 게임은 §1.2의 "CP949 그대로" 경로다.
3. **SCI0 원본 인터프리터용 빌드에서 변환** — Camelot: `sci0_kr_extract.py
   extract`(§1.3)로 UTF-8 `text.NNN`+`sci-ko.str`을 새로 뽑아낸다.

**문자 커버리지 검사**: SCI 쪽은 "2350 게이트"를 명시적으로 공유 폰트
빌드 단계에서 돌린다(아래 (c)). `bake-game-fonts.sh`(1-27행 주석)는
게임별 서브셋을 구울 때 `mkfont.py --require`로 "L 프리셋(2350)에 들어가는
코드포인트인데 폰트에 글리프가 없는" 진짜 결함만 실패시키고, KS X 1001
2350 밖의 음절은 "빠지는 게 예상된 일"(□로 표시)로 취급한다.

**(c) 폰트 굽기**

- `tools/korean/bake-game-fonts.sh NAME GAMEDIR OUTDIR [EXTRA-CHARS-FROM...]`
  (1-27행) — 게임 하나의 `TEXT.*`/`SCI-KO.STR`만 읽어 L 프리셋(`<NAME>L.SVF`,
  neodgm 16px 1bpp, `ascii,ksx1001-nohanja`)과 U 프리셋 두 얼굴
  (`<SHORT>GOT.SVF` UI용 NanumGothic-Bold, `<SHORT>BAT.SVF` 본문용 Gowun
  Batang Bold)을 굽는다. **상태: 2026-09-29 이후 SCI DOS 패키지(KQ1/LB1/
  Camelot/LB2)는 이 게임별 서브셋 대신 아래 공유 폰트를 쓴다** —
  `bake-game-fonts.sh`는 여전히 "게임별 `.MAP`의 자체 `missing=` 점검"과
  SCI 외 다른 패키지에 쓰인다(`bake-dos-fonts.sh:5-7` 주석).
- `tools/korean/bake-dos-fonts.sh`(1-60행+) — **DOS 공용 L/U 프리셋**
  (`KO2350.SVF`, `KOCP949.SVF`/`KOBATANG.SVF`, 그리고 2350로 좁힌
  `KO2350G.SVF`/`KO2350B.SVF`)을 굽는다. "common2350" 게이트: 출시된 모든
  SCI 번역(KQ1/LB1/Camelot의 UTF-8 TEXT.*/SCI-KO.STR/RESOURCE.MSG, LB2의
  네이티브 CP949 RESOURCE.MSG)이 실제로 쓰는 코드포인트를 `ascii+
  ksx1001-nohanja`(2350 음절, 한자 제외)와 대조한 결과, LB2만 그 밖의
  코드포인트 7개(`EXTRA_REQUIRE=AC37,B2CF,B584,C125,C2BA,D14A,D59D`)를
  더 필요로 해서 이 7개를 세 폰트 전부의 `--unicode` 목록에 얹었다
  (bake-dos-fonts.sh:4-21). 결과: DOS SCI 패키지는 게임별 서브셋이 아니라
  이 공유 파일들을 `DATA/`에 담아 배포한다.

**(d) Linux/Windows에서 테스트**: SCUMM과 같은 도구 세트(README.md "Checking
the result" 이하)를 그대로 쓴다 — SCI 전용 테스트 스크립트는 따로 없다.

**(e) DOS 패키징**

세 스크립트가 있다:

- `harness/dos/release/build_sci_lb2.py`(1-40행+) — LB2 개인용 릴리스 zip.
  ```sh
  build_sci_lb2.py --exe PATH [--dist DIR] [--work DIR] [--gamedata DIR] [--lb1-zip PATH]
  ```
  `--gamedata`(기본 `~/work/scummvm/gamedata`)에서 원본 CD 파일 114개와
  `LauraBow2_KoreanPatch.zip`의 `message.map`/`resource.msg`**만**
  (`CM32L_*.ROM`/`MT32_*.ROM`/`scummvm.exe`/`SDL.dll`은 명시적으로 제외,
  14-20행) 가져오고, `--lb1-zip`(기본 최신 `dist/scummvm-dos-sci-lb1-*.zip`)
  에서 여러 패키지 공통 파일(`CWSDPMI.DOC`, `COPYING`, `SDL3.TXT`,
  `HELP.TXT`, `VERSION.TXT`)을 재사용한다. 맵(`LB2KO.MAP`)과
  공유 폰트는 `dos/dists/engine-data/hires_text/dos`에서 읽기 전용으로
  가져온다(22-30행). 레이아웃은 "한국어 전용, `GAMES/LB2KO`" 한 가지로
  고정 — 영문판을 따로 안 두는 이유가 스크립트 docstring에 설명돼 있다
  (35-40행: RESOURCE.MAP/RESOURCE.000을 영문·한국어가 공유하므로 한
  디렉터리로 영문 타깃을 깔끔히 분리할 수 없고, AUD를 두 벌 두면 zip이
  ~720MB로 불어난다).
- `harness/dos/release/repack_sci.py`(1-53행+) — **완성된** SCI zip에
  새 `SCUMMVM.EXE`만 갈아 끼우는 도구(전체를 다시 빌드하지 않음).
  ```sh
  repack_sci.py --old OLD_ZIP --exe NEW_EXE [--dist DIR] [--work DIR]
  repack_sci.py --old OLD_ZIP --exe NEW_EXE --f2350 {kq1,lb1,camelot}
  repack_sci.py --old OLD_ZIP --exe NEW_EXE --unify GAME[,GAME...]
  ```
  Info-Zip의 "copy mode"(`zip old --out new -x pattern...`)로 안 바뀐
  엔트리는 재압축 없이 그대로 복사하고, `SCUMMVM.EXE`/`COMMIT.TXT`/
  `VERSION.TXT`/`DATA/*`만 갱신한다. `--f2350`은 게임별 서브셋 폰트에서
  공유 KO2350 폰트로 마이그레이션할 때 `DATA/`를 통째로 교체하고
  `SCUMMVM.INI`의 `hires_text_map=`을 새 표준 이름으로 고쳐 쓴다(LB1의
  `LB1U8U.MAP`→`LB1KOU.MAP` 등). `--unify`는 게임당 맵 하나(`<X>KO.MAP`)로
  `DATA/`를 다시 꾸미고 INI를 섹션별로 고쳐 쓴다: `extrapath=DATA` +
  `hires_text_map=data:<X>KO.MAP`, 8비트 타깃은 `render_target=clut8`.
  **"MT-32 ROM이나 .sf2 사운드폰트를 절대
  포함하지 않는다"를 산출물 스캔으로 직접 강제한다**(33-34행: "the output
  is scanned for them ... before being reported as done") — §5의 근거.
- `harness/dos/release/build_scumm.py`는 SCUMM 전용(§2.1 (e)).

**패키지 예시** (`dist/scummvm-dos-sci-lb1-*.zip`의 `SCUMMVM.INI`,
`repack_sci.py --unify lb1`이 고쳐 쓴 형태):

```ini
[lb1ko]
gameid=laurabow
engineid=sci
description=Laura Bow 1 (Korean, anti-aliased)
language=ko
extrapath=DATA
hires_text_map=data:LB1KO.MAP
path=GAMES\LB1KO

[lb1kol]
...
hires_text_map=data:LB1KO.MAP
render_target=clut8
```

맵은 게임 폴더 기준이 아니라 `extrapath`(`DATA\`)에서 찾는다(`data:`).
8비트 타깃은 같은 맵의 `:clut8` 섹션을 쓴다.

`GAMES/LB1KO`는 영문 원본 + `packvol.py`가 만든 `RESOURCE.MSG`+
`MESSAGE.MAP`(SCRIPT.052 포함) + `SCI-KO.STR`. `language=ko`가 없으면
`isKoreanMessageMap()`이 이 볼륨을 인식하지 않는다(§1.2).

**Windows 패키징**: SCI 쪽에도 Windows 생성기는 없다(§4 gap) — DOSBox 계열
에뮬레이터 검증이 사실상의 대역이라는 점은 SCUMM과 같다.

---

## 3. 포맷 지정자와 텍스트 내 제어 코드

번역자가 원문의 `%s`/`%d`나 `0xFF` 이스케이프를 잘못 건드리면 게임이
죽거나(버퍼 오버런), 엉뚱한 값이 끼거나(타입 불일치), 조사가 빠진 채로
출력된다. 두 엔진은 이 문제를 서로 다른 방식으로 푼다.

### 3.1 SCI: kFormat과 메시지 내 제어 시퀀스

**포맷 지정자**는 `kFormat` 커널 콜(`engines/sci/engine/kstring.cpp:288-509`)
하나가 전부 처리한다. 지원하는 지정자:

- `%s` — 문자열 삽입(kstring.cpp:378-444). 인자가 힙 주소(`reg.getSegment()`)
  면 `paramindex`를 1 증가, 텍스트 리소스 주소(세그먼트 0)면 2 증가시킨다
  — **즉 `%s` 하나가 `argv` 슬롯을 1개 또는 2개 먹을 수 있다.**
- `%d`/`%u`/`%x` — 정수(부호/무부호/16진수), `paramindex` 1 증가
  (kstring.cpp:459-474).
- `%c` — 문자 하나 삽입, `paramindex` 1 증가(kstring.cpp:447-455).
- 너비/정렬: 자리수(우측 정렬 기본), `-`(좌측 정렬), `=`(가운데 정렬),
  선행 `0`(0 채움) — SCI 자신의 확장 문법(kstring.cpp:352-373). `%%`는
  리터럴 `%`.

**인자 소비는 위치식이 아니라 "등장 순서식"**: `paramindex`는 스크립트가
넘긴 `argv[startarg..]`를 **문자열에 지정자가 나타나는 순서대로** 그대로
가져다 쓴다(`argv[startarg + paramindex]`, kstring.cpp:378). `%1$s`
같은 인자 번호 지정 문법은 없다. 결과:

- 지정자를 **재배열해도 되는 경우**: 문자열 안의 모든 지정자가 같은
  타입·같은 슬롯 폭(전부 `%s`뿐이거나 전부 `%d`뿐)일 때만 안전하다. 그때는
  등장 순서가 바뀌어도 매칭되는 인자의 *타입*이 항상 맞기 때문이다.
- 지정자를 **재배열하면 안 되는 경우**: 서로 다른 타입이 섞여 있을 때
  (`"%s ... %d"`처럼). `%d`가 원래 `%s` 자리로 옮겨가면, `%d`가 소비하는
  슬롯은 실제로는 문자열의 `reg_t`/세그먼트 주소이고 그걸 정수로 잘못
  해석한다 — 반대쪽 `%s`도 정수 슬롯을 `reg_t`로 잘못 읽는다. 이건 조용히
  깨진다(크래시가 아니라 garbage 값·garbage 문자열).
- **번역자 규칙**: 문자열 안에서 `%s`/`%d`/`%c`/`%x`/`%u`의 **개수·타입·
  등장 순서**를 원문과 동일하게 유지해야 한다. 한국어 어순 때문에 지정자
  순서를 바꾸고 싶다면(예: "You gave %s to %s"류) 실제로 위치를 바꾸는
  대신, 같은 타입끼리만 서로 자리를 바꾸거나(둘 다 `%s`라면 안전), 아니면
  문장을 지정자 순서를 유지한 채로 재구성해야 한다. 엔진에 `%n$s`류
  포지셔널 지정자는 없으므로 **진짜 재배치가 필요하면 SCI 쪽에는 우회로가
  없다** — 이것이 §4의 공백이다.

**메시지 내 인라인 제어 코드**: SCI16 텍스트 렌더러가 `|` 로 여는 코드를
해석한다(`GfxText16::CodeProcessing()`, engines/sci/graphics/text16.cpp:149-206):

- `|c|`/`|cN|` — 텍스트 색을 포트 펜 색 또는 `_codeColors[N-1]`로.
- `|f|`/`|fN|` — 폰트를 원래 폰트 또는 `_codeFonts[N]`로. 그림 중이면
  새 폰트의 앵커를 다시 시작한다(다른 문자열 취급).
- `|r|` — 참조 사각형 시작/끝 표시(pepper 등에서 하이퍼링크류 영역).

이 코드들은 파이프 두 개(`|c|`, `|f2|` 등)로 감싸인 **문자 그대로의
텍스트 안 마커**이므로, 번역이 그 문자열을 옮기면서 `|...|` 자체를
지우거나 순서를 바꾸면 안 된다 — 내용만 그 사이사이에서 바뀌면 된다.
줄바꿈은 리터럴 `0x0A`/`0x0D` 바이트로 텍스트 안에 그대로 있다
(`text16.cpp:1242,1358-1367`) — 이스케이프가 아니라 그냥 개행 문자이므로
번역에서 자유롭게 움직여도 되지만, 유지해야 하는 자리(대화 상자 줄바꿈
의도)는 원문의 개행 개수·위치를 참고해서 옮기는 게 안전하다.

**조사(을/를, 이/가) 문제**: `engines/sci/` 전체를 뒤져도 SCI 엔진에는
한국어 조사 자동 선택 로직이 **없다**(`grep -rn "josa\|jongsung\|particle"
engines/sci/`가 아무것도 찾지 못함 — SCUMM의 `checkJongsung`/`_krStrPost`에
대응하는 게 전혀 없다). SCI 메시지 텍스트는 스크립트가 `kFormat`으로
런타임에 이름을 끼워 넣을 때, 그 이름의 받침 유무에 따라 조사가 달라지는
문제를 엔진 차원에서 풀어주지 않는다. 실무적으로 번역자가 취할 수 있는
우회는:

- 조사가 붙는 자리를 아예 피하는 문장으로 재작성("~을(를)" 병기,
  또는 "~라는 대상" 같은 조사 불필요 표현으로 바꾸기) — 지금까지의 SCI
  한국어 패치들이 택한 방식으로 보인다.
- 스크립트를 고쳐 조사를 직접 계산하는 새 커널 콜/서브루틴을 넣는 것은
  스크립트 컴파일이 필요한 큰 작업이라 이 포크 범위 밖.

**검증 도구**: 원문과 번역문의 `%` 지정자 시퀀스(타입·개수·순서)가
일치하는지 자동으로 비교하는 스크립트는 `tools/korean/`에도 테스트
스위트(`test/engines/sci/`)에도 없다 — **공백(gap)**. 간단한 체크 아이디어:
TEXT 리소스/SCI-KO.STR을 순회하며 각 문자열에서 `re.findall(r'%-?=?0?\d*[sducx]',
s)` 같은 정규식으로 지정자 나열을 뽑아, 원문·번역문의 나열이 동일한지(순서
포함) 비교하고, 다르면 그 문자열과 두 나열을 나란히 출력하는 것으로 충분히
시작할 수 있다.

### 3.2 SCUMM: 0xFF/0xFE 이스케이프

문자열 안의 `0xFF`(v7+에서는 `0xFE`도, `string.cpp:263`) 다음 바이트가
코드다. `ScummEngine::convertMessageToString()`(string.cpp:1607-1800)이
변수를 치환해 넣고, `ScummEngine::handleNextCharsetCode()`
(string.cpp:255-538)가 화면에 그릴 때 나머지를 처리한다. 인자 있는 코드는
버전에 따라 2바이트(v ≤ 7) 또는 4바이트(v8) 정수를 뒤에 매단다.

| 코드 | 뜻 | 인자 | 비고 |
|---|---|---|---|
| `01` | 줄바꿈 | 없음 | `handleNextCharsetCode` case 1: `c=13` |
| `02` | 메시지 유지(줄바꿈 없이 대기) | 없음 | `_haveMsg=0; _keepText=true` |
| `03` | 메시지 종료(대기) | 없음 | `_haveMsg=...; _keepText=false` — 여러 웨어크라운드가 이 코드에 걸려 있다(SAMNMAX 타이밍 등) |
| `04` | 정수 변수 삽입 | 변수 번호 | `convertIntMessage()` → `%d`에 해당 |
| `05` | 동사(verb) 이름 삽입 | 변수 번호(+ bit15 조사 플래그) | `convertVerbMessage()`, 아래 참고 |
| `06` | 오브젝트/액터 이름 삽입 | 변수 번호 | `convertNameMessage()`, 받침 여부를 `_krStrPost`에 기록 |
| `07` | 문자열 변수 삽입 | 변수 번호(+ bit15 조사 플래그) | `convertStringMessage()` |
| `08` | (말풍선에서는 무시, 동사 문장에서는 줄바꿈처럼) | 없음 | MI2 목수 대사 등 긴 동사 문장용 |
| `09` | 액터 애니메이션 시작 | 프레임 번호 2바이트 | |
| `0A` | 디지털 대사(talkie) 오프셋/길이 | 14바이트 | |
| `0C` | 텍스트 색 변경 | 색 2바이트(`0xFFFF`=원래 펜 색) | |
| `0D` | (미사용/알 수 없는 옵코드) | 2바이트 | 디버그 로그만 남기고 소비 |
| `0E` | 폰트(문자셋 id) 변경 | id 1바이트 + 패딩 2바이트 | |

`01`/`02`/`03`/`08`은 인자가 없고 `convertMessageToString()`에서
그대로 복사되며(1687-1692행), `04`-`07`과 `09`/`0A`/`0C`/`0D`/`0E`는
인자를 읽어 처리한 뒤 다시 쓴다(1693-1751행).

**`04`-`07`은 "자기 완결형"이라 순서를 바꿀 수 있다**: 이스케이프가
가리키는 변수 번호가 문자열 자기 자신에 박혀 있어서(스택에서 순서대로
꺼내오는 게 아니다), `FF 04 <var>` 통짜를 문자열 안에서 옮겨도 그 변수의
값을 그대로 가리킨다. 이건 SCI의 kFormat과 근본적으로 다른 지점이다 — SCI는
"문자열에 등장하는 순서대로 argv를 소비"하지만 SCUMM은 "이스케이프 자체가
변수 번호를 들고 있다." 그래서 번역이 "그가 %VAR1%에게 %VAR2%를 준다" 같은
문장에서 두 삽입어의 한국어 어순을 바꾸고 싶으면, `FF 06 <var1>`과
`FF 07 <var2>` 뭉치를 통째로 옮기면 된다 — 각 뭉치 안의 바이트 순서(코드
바이트 + 인자 바이트)만 깨지 않으면 된다.

**`을/를`, `이/가` 조사 문제 — 이미 부분적으로 풀려 있다(레거시 CJK
모드에서만)**: `convertVerbMessage()`/`convertStringMessage()`
(string.cpp:1859-1935, 1993-2043)에 동사·문자열 변수 번호의 **bit 15**를
"조사 접착"으로 해석하는 코드가 있다 — "Used by Korean fan translated
games (monkey1, monkey2)"라는 주석이 달려 있다:

```cpp
// The Korean patches' postposition glue (bit 15) is CP949 grammar; a
// UTF-8 translation that still carries the code gets nothing for it.
if (_textUtf8 && (var & (1 << 15)))
	return 0;
```

레거시 CJK 경로(`_useCJKMode && !_textUtf8`)에서는 `checkKSCode()`/
`checkJongsung()`(engines/scumm/charset.h:43-52, `ks_check.cpp`의 2350
항목짜리 테이블)로 **직전에 삽입된 한글 음절의 받침 유무**를 읽어
`_krStrPost`에 저장해 두고, 다음 조사 자리에서 "을/를", "와/과"의 알맞은
쪽을 CP949 바이트로 직접 골라 넣는다(MI1/MI2 전용 동사 하드코딩까지
포함, string.cpp:1875-1930).

**그러나 UTF-8 번역에서는 이 코드가 아무 것도 하지 않는다** — 위 인용의
`return 0;`이 바로 그것이고, `engines/scumm/HIRES_TEXT_SETUP.md:363-365`에
"The Korean postposition glue codes do nothing under UTF-8."이라고 명시돼
있다(이 문서가 참조하라고 한 "이미 대장에 올라 있는" 항목). 이유는
`_krStrPost`를 채우는 받침 판정 자체가 CP949 바이트 패턴(`checkKSCode`)
기반이라 UTF-8 문자열에는 적용되지 않기 때문이다.

**번역자 규칙**:

- 이스케이프 코드 바이트와 그 인자(2/4바이트)는 통째로 하나의 단위로
  다뤄야 한다 — 쪼개거나 바이트 순서를 바꾸면 크래시하거나 엉뚱한 변수를
  가리킨다.
- `01`/`02`/`03`/`08`처럼 인자 없는 코드는 그대로 두거나, 줄바꿈 의도를
  살려 다른 위치로만 옮긴다.
- 조사가 필요한 자리(동사/이름/문자열 삽입 직후)가 있는 UTF-8 번역에서는
  엔진이 대신 골라주지 않으므로, **조사 없이도 자연스러운 문장으로
  재작성**하거나 대상이 될 수 있는 모든 받침 패턴에 맞는 중립적 표현을
  쓰는 수밖에 없다 — DUMB의 CP949 패치가 하던 일을 UTF-8에서 엔진이
  대신 안 해준다.

**검증 도구**: `test_scumm_trs.py`가 번들 자체의 무결성(매직, 오프셋,
이스케이프 길이)은 검사하지만, 원문·번역문 사이에서 `04`-`0E` 이스케이프의
**집합·순서**가 맞는지 비교하는 검사기는 없다 — **공백(gap)**. 아이디어:
`scumm_trs_from_patch.py`가 이미 갖고 있는 `esc_len()`/이스케이프 파서를
재사용해, 원문 문자열에서 뽑은 `(코드, 인자)` 시퀀스와 번역문에서 뽑은
시퀀스가 (재배열 허용 대상인 04-07을 집합으로, 그 외는 순서까지 포함해)
같은 멀티셋인지 비교하는 스크립트.

### 3.3 AGS — 간단히

포크가 AGS 엔진에 손댄 부분은 `.tra` 번역 파일의 인코딩 판정
(`engines/ags/engine/ac/translation.cpp:63-84`, §1.5)뿐이다. `.tra`
자체는 "영문 줄 → 한국어 줄" 평문 사전이고, 그 안의 `%s`/`%d` 같은 서식은
AGS 스크립트가 `String::FromFormat()`(`engines/ags/shared/util/string.cpp:289-299`,
내부적으로 표준 `vsnprintf`)으로 만든 것이라 SCI의 kFormat과 비슷하게
플랫폼 `vsnprintf` 구현이 지원하는 문법을 그대로 물려받는다(POSIX
`%n$s` 위치 지정자를 지원하는 libc라면 재배열이 될 수도 있지만, 이 포크가
그걸 검증하거나 보장하지는 않는다). 한국어 조사 처리나 AGS 전용 제어
코드에 대한 이 포크만의 로직은 확인되지 않았다 — 필요해지면 이 절을
SCI/SCUMM 수준으로 확장할 것.

---

## 4. 공백(gap)과 할 일

지금 손으로 하고 있는 일, 그리고 없는 도구들:

1. **SCI 새 게임 텍스트 추출기 없음** — `sci0_kr_extract.py`는 Camelot류
   "SCI0 + 원본 인터프리터 빌드"에 맞춰져 있다. KQ1(SCI 리메이크), LB1,
   LB2처럼 이미 UTF-8 TEXT.NNN이나 CP949 RESOURCE.MSG로 와 있는 게임
   말고, 아직 아무도 번역하지 않은 SCI1+ 게임의 원본 텍스트를 처음부터
   뽑아내는 범용 스크립트가 없다.
2. **번역 파일을 처음부터/스프레드시트에서 만드는 생성기 없음** — SCUMM
   쪽 `scumm_trs_from_patch.py`는 "이미 있는 다른 빌드의 번역"을 변환할
   뿐이고, SCI 쪽도 마찬가지로 기존 팬패치를 재포장하는 도구뿐이다.
   옛 포크의 게임별 XML(`kq5.xml`/`sq4.xml` 류, §1.4)을 우리 포맷으로
   자동 변환하는 도구도 없다.
3. **KQ1/LB1/Camelot의 `README.TXT`/게임별 `.ini` 조각을 만드는 일반화된
   생성기 없음** — `build_scumm.py`는 MI1/MI2 두 게임에 딕셔너리로
   하드코딩돼 있고, SCI 쪽은 `build_sci_lb2.py`(LB2 전용) +
   `repack_sci.py`(기존 zip 패치 전용) 두 스크립트뿐이다. KQ1/LB1/Camelot의
   최초 zip은 이 문서에서 확인한 한 이 두 스크립트가 만든 게 아니라 — 그
   생성 스크립트 자체가 이 트리에 없다(더 예전에 손으로 짜 맞췄을 가능성이
   있다). "새 게임 하나를 넣으면 INI/README/zip이 나온다"는 일반화된
   경로가 SCI에도 SCUMM에도 없다.
4. **Windows용 패키징 생성기 없음** — DOS(`harness/dos/release/*.py`)에만
   있고, Windows 배포 zip을 만드는 스크립트는 SCUMM·SCI 어느 쪽에도 없다.
5. **포맷 지정자/제어 코드 일치 검사기 없음**(§3.1, §3.2) — 원문·번역문
   사이에서 `%s`/`%d`류 지정자나 SCUMM `0xFF 04-0E` 이스케이프의 개수·
   타입·(허용되는 한도 내의) 순서가 같은지 자동으로 비교하는 도구가 없다.
   지금은 전부 리뷰(사람 눈)와 실제 플레이 테스트로만 잡는다.
6. **SCI 조사(을/를, 이/가) 자동 처리 없음**(§3.1) — SCUMM에는 레거시
   CJK 경로에 한해 `_krStrPost`/`checkJongsung` 메커니즘이 있지만(그리고
   UTF-8에서는 꺼져 있다, §3.2), SCI에는 애초에 그런 메커니즘 자체가 없다.

**추천 할 일**(우선순위 순):

1. SCI/SCUMM 공용으로 쓸 수 있는 작은 "지정자 시퀀스 추출 + 비교" 스크립트
   하나를 `tools/korean/`에 추가한다(§3.1/§3.2의 검증 아이디어를 그대로
   구현) — 새 번역이 들어올 때마다 손으로 하던 리뷰의 상당 부분을 대체할
   수 있는, 비용 대비 가치가 가장 큰 항목.
2. `build_sci_lb2.py`/`repack_sci.py`가 게임 하나에 대해 하는 일(파일
   목록·맵·INI 조각)을 선언적 테이블(예: `build_scumm.py`의 `MI1`/`MI2`
   딕셔너리 같은 구조)로 뽑아, KQ1/LB1/Camelot도 같은 러너가 돌리게
   일반화한다.
3. 옛 포크의 게임별 XML(`key=영문, value=한국어`)을 `TEXT.NNN`+
   `SCI-KO.STR`이나 `korean.trs`로 변환하는 1회성 스크립트를 만들어, 아직
   손대지 않은 게임(SQ4, Mother Gooseㄷ 등)의 팬번역을 재활용할 길을
   연다.
4. SCI 조사 문제에 대해, 최소한 "이 스크립트 문자열은 삽입되는 이름의
   받침에 따라 조사가 달라진다"를 표시해 두는 주석/체크리스트라도
   `sci-ko.str`류 파일에 규약으로 두어, 번역자가 문제를 인지하고 회피
   표현을 고르게 한다.
5. Windows 패키징이 필요해지면, DOS 스크립트들의 "파일 나열 → zip"
   골격은 거의 그대로 재사용 가능하니 완전히 새로 짜기보다는
   `build_scumm.py`/`build_sci_lb2.py`의 파일 목록/템플릿 부분만 갈아
   끼우는 방향으로 접근한다.

---

## 5. 라이선싱 노트

- **상용 게임 데이터**: `gamedata/`의 게임 리소스(MI1/MI2/KQ1/LB1/LB2/
  Camelot 등)는 전부 상용 게임 원본이다. 실제 배포 zip의 문구를 그대로
  인용하면(dist 패키지 `README.TXT`, `/tmp/.../lb1/SCUMMVM/README.TXT:396`):
  > The game packages contain the commercial games and are for personal
  > testing only.
  즉 이 저장소가 만드는 "게임 데이터 포함" 패키지는 전부 **개인적
  테스트용**이며 재배포용이 아니다. `build_scumm.py`의 docstring도 첫
  줄부터 "Build the **personal-use** DOS release zips"라고 못박는다
  (harness/dos/release/build_scumm.py:2).
- **팬 번역과 출처(provenance)**: 한국어 팬 번역(DUMB의 MI1/MI2 패치, LB1/
  LB2/Camelot/KQ1의 각 팬패치)은 원저작자의 개인 작업물이다.
  `scumm_trs_from_patch.py --table`이 만드는 provenance TSV(room, where,
  script, kind, **provenance**(`ute`/`ute-split`/`ute-fuzzy`/`dumb`/`none`/
  `n/a`), 영문, 한국어, 패치 원문)가 각 줄이 어디서 왔는지 추적 가능하게
  남긴다 — README.md도 "The translations are the fans' work, for personal
  use with a copy of the game; the bundle is not to be published."라고
  명시한다. 새 번역을 들여올 때도 이 출처 기록 관행을 유지할 것.
- **폰트 라이선스(OFL)**: 굽는 데 쓰는 서체 — neodgm(L 프리셋), NanumGothic
  Bold, Gowun Batang Bold(U 프리셋 두 얼굴) — 는 모두 SIL Open Font
  License 1.1이다. 구운 `.SVF`는 폰트 이름을 담지 않으므로(비트맵/코드포인트
  표만 남는다) Reserved Font Name을 쓰지 않는다는 점까지 README 템플릿이
  명시한다(`harness/dos/release/README.scumm.tmpl:335-338`: "The .SVF files
  are ... no font name, so no Reserved Font Name is used"). 패키지에는
  반드시 해당 라이선스 전문(`DATA\OFL.TXT`, `DATA\OFLNANUM.TXT`,
  `DATA\OFLGOWUN.TXT`)을 동봉한다 — LB1/KQ1 dist zip 둘 다 이 세 파일을
  담고 있다.
- **MT-32 ROM / 사운드폰트는 절대 동봉하지 않는다**: `repack_sci.py`가
  이를 산출물 스캔으로 강제한다("Never ships MT-32 ROMs or .sf2
  soundfonts: the output is scanned for them ... before being reported as
  done", repack_sci.py:33-34). `README.scumm.tmpl:282`도 "No ROMs and no
  soundfonts are included. To listen without a module, use an emulator..."
  라고 이용자에게 안내한다. 대비되는 반면교사가 `gamedata/
  LauraBow2_KoreanPatch.zip`과 `gamedata/SQ4_KoreanPatch.rar`인데, 둘 다
  옛 한국어 포크가 만든 배포판이라 `MT32_CONTROL.ROM`/`MT32_PCM.ROM`/
  `CM32L_CONTROL.ROM`/`CM32L_PCM.ROM`을 통째로 담고 있다 — 이 저장소의
  빌드 스크립트들은 이 zip들에서 게임 리소스와 `message.map`/`resource.msg`
  **만** 골라 쓰고 ROM 파일은 절대 경로에 넣지 않는다(build_sci_lb2.py
  17-20행이 이를 명시).
- **CWSDPMI/SDL3**: 둘 다 재배포 가능한 라이선스(CWSDPMI: 자체 라이선스,
  `CWSDPMI.DOC`에 조건과 소스 위치; SDL3: zlib, `SDL3.TXT`)이고 각 zip에
  라이선스 텍스트가 동봉된다.
