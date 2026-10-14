# Per-font decoration: `[shadow.N]`

Date: 2026-10-14. Branch: johab. Status: design, for review.

## Why

A hi-res map has one `[shadow]` section, so every charset of a game gets the
same outline width and drop distance. The originals do not work that way.
Measured from the colour-2 pixels of the English 2bpp SCUMM faces
(`runs/fontmeasure/vision/shadow-measure.md`):

| face | line | original decoration (game px) | at 2x |
|---|---|---|---|
| MI1/MI2/Loom cs2, cs4, Indy4 cs2, DOTT cs2 | 9-15 | outline, reach 1 | 2 px |
| MI2 cs7, Indy4 cs3, cs4 | 12-15 | outline, reach 2 | 4 px |
| MI cs6, DOTT cs3, cs5 | 12-14 | drop (1,1) | 2,2 px |
| DOTT cs6 | 28 | drop (2,2) | 4,4 px |

The engine draws a 1.5 px outline and a 1 px drop at 2x for all of them, so a
54 px title gets the same hairline decoration as 16 px dialogue. The font-
pairing page now previews decorations grown with the original; the maps need
a way to say it.

## What

A map may carry `[shadow.N]` sections, N a font id - the same id `[font.N]`
and `[glyphs.N]` use (a SCUMM charset number). A `[shadow.N]` key overrides
the same `[shadow]` key for glyphs drawn in charset N; a key it does not set
falls back to `[shadow]`, and `[shadow]` keys not set fall back to the built-in
defaults as today.

```ini
[shadow]          ; every charset
mode=game
[shadow.3]        ; Indy4 cs3: a 2-game-pixel outline
width=3
[shadow.6]        ; DOTT cs6: a (2,2) drop at 2x
offset=4
```

Keys: all eight of `[shadow]` (`mode offset color width style shadow
shadow_color shadow_alpha`), same syntax and limits, same warnings with the
section name in them. Render-target qualifiers work as on `[shadow]`:
`[shadow.6:clut8]` beats `[shadow.6]`, which beats `[shadow:clut8]`, which
beats `[shadow]` (the order `[glyphs.N]` already uses).

Scope: SCUMM only, as `[shadow]` is. SCI and AGI do not draw decorations; a
SCI/AGI map with `[shadow.N]` gets the same "does not use [shadow]" warning
`[shadow]` gets.

Unchanged: maps without `[shadow.N]` behave exactly as before, byte for byte.

## How

`graphics/hires_text/font_map.{h,cpp}`
- The ten shadow fields of `HiResMap` move into a `HiResShadowSpec` with a
  "set" flag per key (most already have one: `shadowColorSet`,
  `shadowShiftSet`; `mode`, `offset`, `width`, `style`, `alpha` get one).
  `HiResMap::shadow` holds `[shadow]`; `HiResMap::shadowIds`
  (`HashMap<int, HiResShadowSpec>`) holds each `[shadow.N]`.
- `HiResMap::shadowFor(int id)` returns `[shadow]` with the keys `[shadow.N]`
  set laid over it (the resolved spec).
- The section classifier learns `shadow.N` as it knows `glyphs.N`;
  `buildShadowSection()` fills a `HiResShadowSpec` and is called once for
  `[shadow]` and once per `[shadow.N]`.
- Callers that read `map.shadowMode` etc. read `map.shadow.mode`; a plain
  rename, no behaviour change.

`graphics/hires_text/glyph_renderer.{h,cpp}`
- `applyMap(style, map, scale)` becomes `applyShadow(style, spec, scale)`,
  taking the resolved spec.

`engines/scumm/hires_text.cpp`
- `glyphStyle()` gets the charset id the glyph is drawn in (the caller,
  `drawGlyph`, already has `_curId` through its font id) and uses
  `map.shadowFor(id)`.

## Tools

`tools/korean/pairing2svf.py` writes a `[shadow.N]` for each chosen SCUMM
font whose original has a measured decoration: `width` = 0.75 x 2 x reach x
(Korean face height / (2 x original line height)), in quarter pixels;
`offset` the drop likewise. Fonts without a measurement keep `[shadow]`.
(Also the page's "원본 비례" preview rule, so the preview and the map agree.)

## Tests

- `test/graphics/hires_text_font_map.h`: `[shadow.N]` parses every key; an
  unset key falls back to `[shadow]`; qualifier precedence
  (`[shadow.6:clut8]` > `[shadow.6]` > `[shadow:clut8]` > `[shadow]`); a bad
  value warns with `[shadow.6]` in the text and is ignored; SCI keys warn.
- `test/engines/scumm/hires_shadow_rule.h`: `glyphStyle` for charset 3 and 6
  with the example map gives width 3 / offset 4, other charsets the `[shadow]`
  values; a map without `[shadow.N]` gives the same style as before for every
  charset.
- `tools/korean/test_pairing2svf.py`: the measured faces get their
  `[shadow.N]`, others none.
- Run: `make test`; Indy4 and DOTT in the game with a `[shadow.N]` map,
  screenshots compared with the page preview.

## Risks

- `HiResMap` field rename touches every reader of the shadow fields; the
  compiler finds them all. Kept as a mechanical first commit.
- A font id used both for SCUMM charsets and (future) other engines' fonts
  means the same thing everywhere ("the id `[font.N]` uses"), so no new id
  space.
