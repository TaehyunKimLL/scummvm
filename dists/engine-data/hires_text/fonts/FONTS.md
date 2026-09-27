# Fonts for hi-res game text

These fonts are shipped with ScummVM for the hi-res text layer
(`graphics/hires_text`). A game's `hires_text.map` names one with the `data:`
prefix, for example

    [fonts]
    ko=data:hires_text/fonts/nanumgothic/NanumGothic-Bold.ttf

`data:` is looked up in the effective extrapath (command line, game or
global settings), then in the application-level extrapath, then in the
ScummVM data directory. `make install` puts this folder at
`<datadir>/hires_text/fonts/`. In a source tree, run with
`--extrapath=dists/engine-data`. Example maps are in `../maps/`.

Only the POSIX `make install` carries `hires_text/`. The macOS app bundle,
dist-generic and the other port packages do not. On those, copy
`hires_text/` somewhere and point `extrapath` at the folder that holds it.
A `data:` path may not be absolute and may not contain `..`.

Every font here is an unmodified upstream file. Its licence text sits next to
it in the same folder, unmodified. All of them are licensed under the SIL Open
Font License 1.1 (also in `LICENSES/COPYING.OFL`). Under the OFL you may
bundle, embed and redistribute the fonts, also commercially, but not sell a
font by itself. A modified version (a subset, a baked bitmap font, a
conversion) must not use a Reserved Font Name.

Only two files were renamed, and their contents are unchanged: Google Fonts
names the variable fonts `EBGaramond[wght].ttf` and `Caveat[wght].ttf`. Here
they are `EBGaramond-VF.ttf` and `Caveat-VF.ttf`, so that map files and
Makefiles need no brackets.

Each file matches its upstream byte for byte. Checked on 2026-09-28 against
github.com/google/fonts (`ofl/<family>/`), the Galmuri v2.40.4 release zip
and the neodgm v1.601 release asset.

## Text faces

| Folder | Files | Size | Recommended for |
|---|---|---|---|
| `nanumgothic/` | NanumGothic-Regular.ttf, NanumGothic-Bold.ttf, OFL.txt | 2.0 + 2.0 MB | Default Korean face. **Bold** for blended (alpha) text at 2x (SCUMM v5/v6, SCI, AGS). **Regular** for keyed 8-bit text. It covers all 11172 Hangul syllables, but no Hanja. |

**NanumGothic** (나눔고딕)
- Copyright © 2010 NHN Corporation (now Naver), per `OFL.txt`; the font's
  name table says "Copyright © 2011 NHN Corporation. All rights reserved.
  Font designed by Sandoll Communications Inc.".
- Designers (name table): Bruce Kwon, Nicolas Noh, Sung-woo Choi.
- Licence: SIL OFL 1.1 (`OFL.txt`).
- Reserved Font Names: Nanum, Naver Nanum, NanumGothic, Naver NanumGothic,
  NanumMyeongjo, Naver NanumMyeongjo, NanumBrush, Naver NanumBrush,
  NanumPen, Naver NanumPen.
- Source: Google Fonts, https://github.com/google/fonts/tree/main/ofl/nanumgothic.
  The same family is published by Naver at https://hangeul.naver.com/font
  and listed on 공유마당 (gongu.copyright.or.kr) as OFL.

## Pixel faces (keyed 8-bit text)

A pixel font is sharp only at its design size or at a whole multiple of it.

| Folder | Files | Size | Design size | Recommended for |
|---|---|---|---|---|
| `galmuri/` | Galmuri7.ttf, Galmuri9.ttf, Galmuri11-Bold.ttf, LICENSE.txt | 3.7 + 4.6 + 2.6 MB | 8 / 10 / 12 px | Galmuri9 for Full Throttle's 12 px cell, keyed at 1x. Galmuri7 for 10 px cells. Galmuri11 Bold for 16 px cells. |
| `neodgm/` | neodgm.ttf, LICENSE.txt | 0.6 MB | 16 px | Keyed SCUMM v5/v6 text at 2x, held at 16 px (`[font.N] size=16`). Has a DOS-era Korean look. |

**Galmuri** (갈무리) v2.40.4
- Copyright (c) 2019-2025 Lee Minseo (quiple).
- Licence: SIL OFL 1.1 (`LICENSE.txt`). No Reserved Font Name.
- Source: GitHub release https://github.com/quiple/galmuri/releases/tag/v2.40.4
  (site: https://galmuri.quiple.dev).
- Not included, to keep the size down:
  - Galmuri11 Regular (5.4 MB);
  - Galmuri14;
  - the Mono cuts;
  - the `*Bitmap-*.ttf` builds, which have strikes only and cannot be scaled.

  Take them from the release if a game needs them.

**Neo둥근모** (NeoDunggeunmo) v1.601
- Copyright (c) 2017-2021 Eunbin Jeong (Dalgona.). The name table says:
  "Original font was released under the public domain by Jungtae Kim in
  1990s. Conversion and additional character design by Dalgona."
- Licence: SIL OFL 1.1 (`LICENSE.txt`).
- Reserved Font Names: "Neo둥근모", "Neo둥근모 Code", "NeoDunggeunmo" and
  "NeoDunggeunmo Code". A subset or baked copy must be renamed.
- Source: GitHub release https://github.com/neodgm/neodgm/releases/tag/v1.601
  (site: https://neodgm.dalgona.dev).

## Styled faces (per-charset display, serif and handwriting)

| Folder | Files | Size | Recommended for |
|---|---|---|---|
| `blackhansans/` | BlackHanSans-Regular.ttf, OFL.txt | 1.0 MB | Korean heavy display face. Use it for heavy serif chapter cards and credits (MI1/MI2/Loom charset 4). It has 2581 syllables (all 2350 of KS X 1001 plus 231 more), not all 11172, so chain a full-coverage face after it. |
| `coustard/` | Coustard-Black.ttf, OFL.txt | 0.1 MB | Latin heavy serif display face (Cooper/Clarendon look). Use it as `latin_font` next to Black Han Sans. |
| `nanummyeongjo/` | NanumMyeongjo-Bold.ttf, OFL.txt | 3.1 MB | Korean serif (myeongjo). Use it for light serif charsets, such as the MI1/MI2 verbs (charset 6). It covers all 11172 syllables. |
| `ebgaramond/` | EBGaramond-VF.ttf, OFL.txt | 0.8 MB | Latin old-style serif. Use it as `latin_font` next to Nanum Myeongjo. |
| `nanumpenscript/` | NanumPenScript-Regular.ttf, OFL.txt | 3.2 MB | Korean handwriting, such as notebooks and notes (e.g. Blackwell). It covers all 11172 syllables. |
| `caveat/` | Caveat-VF.ttf, OFL.txt | 0.4 MB | Latin handwriting, as the Latin face before Nanum Pen Script. |

The variable fonts (EB Garamond: weight axis 400-800; Caveat: 400-700) open at
their default instance, Regular 400. The map has no weight key, so their bold
instances cannot be selected.

**Black Han Sans** (검은고딕)
- Copyright 2015 The Black Han Sans Project Authors. Designed by Zess Type (name table: ZESSTYPE).
- Licence: OFL 1.1. No Reserved Font Name.
- Upstream: https://github.com/zesstype/Black-Han-Sans.
- Source: Google Fonts, `ofl/blackhansans`.

**Coustard**
- Copyright 2011 The Coustard Project Authors. Designed by Vernon Adams.
  (The name table's copyright URL points at googlefonts/bangers; that
  upstream slip is left as is.)
- Licence: OFL 1.1. No Reserved Font Name.
- Upstream: https://github.com/googlefonts/coustardFont.
- Source: Google Fonts, `ofl/coustard`.

**Nanum Myeongjo** (나눔명조)
- Copyright © 2010 NHN Corporation. All rights reserved. Font designed by
  FONTRIX (name table; `OFL.txt`: "Copyright (c) 2010, NHN Corporation").
- Designers (name table): Yong-rak Park, Ji-hee Yoon.
- Licence: OFL 1.1, with the Nanum Reserved Font Names listed under NanumGothic.
- Source: Google Fonts, `ofl/nanummyeongjo`.

**EB Garamond**
- Copyright 2017 The EB Garamond Project Authors. Designed by Georg Duffner and Octavio Pardo.
- Licence: OFL 1.1. No Reserved Font Name.
- Upstream: https://github.com/octaviopardo/EBGaramond12.
- Source: Google Fonts, `ofl/ebgaramond`. Upstream file name: `EBGaramond[wght].ttf`.

**Nanum Pen Script** (나눔손글씨 펜)
- Copyright © 2010 NHN Corporation. All rights reserved. Font designed by
  Sandoll Communications Inc. (name table).
- Designers (name table): Doo-yul Kwak, Hyunghwan Choi, Nicolas Noh.
- Licence: OFL 1.1, with the Nanum Reserved Font Names.
- Source: Google Fonts, `ofl/nanumpenscript`.

**Caveat**
- Copyright 2014 The Caveat Project Authors. Designed by Pablo Impallari (Impallari Type).
- Licence: OFL 1.1. No Reserved Font Name.
- Upstream: https://github.com/googlefonts/caveat.
- Source: Google Fonts, `ofl/caveat`. Upstream file name: `Caveat[wght].ttf`.

## Not shipped

- **Gowun Batang Bold** (OFL): the only static cut is 8.2 MB. Nanum Myeongjo
  Bold (3.1 MB) takes its place as the Korean serif.
- **HBIOS-SYS**: 5.7 MB. Its Latin and box-drawing glyphs are CC BY-SA 4.0
  (VileR, int10h.org), and the rest is OFL. The mixed licence needs its own
  attribution and share-alike terms. Neo둥근모 covers the same use.
