# Text decorations: outline and shadow

How the hi-res layer draws an outline or a drop shadow, what controls it, and
the platform traps that make a correct-looking map produce nothing.

Measured against `bfa1cb0e442` with English and Korean MI2.

## How it is drawn

`HiResGlyphRenderer::drawGlyph` builds the decoration as a **dilation mask**
before drawing the body, following `FontSJISBase::drawChar` in
`graphics/sjis.cpp`.

A pixel belongs to the stroke if any offset in the mode's table lands glyph
ink on it. The mask is then laid down **solid, in one pass**, and the body is
drawn over it with its antialiasing intact.

This matters because the obvious implementation - re-blitting the glyph once
per offset - does not work with alpha:

- each copy carries the body's own antialiased edge, so the stroke is
  semi-transparent exactly where it should be solid, and blends with the
  background it exists to hide;
- the copies overwrite each other, so the rule meant to stop a decoration
  eating the body ends up arbitrating between strokes.

One difference from `sjis.cpp` is load-bearing: that code composes a glyph
into a buffer of its own, while this draws into a plane shared by every glyph
on the line. A stroke therefore yields to body ink already present, including
its faint antialiased edge - otherwise each character erases the tail of the
one before it. Two unit tests cover exactly that.

### Why it cannot be baked into the font

A baked stroke does not work, and the reason is structural. An SVFN glyph
stores **coverage** - one channel saying how much of a pixel is ink. Pillow's
`stroke_width` draws the stroke in the same fill as the body, so the baked
result says "this is also ink", not "draw this part in the other colour".

Measured: baking a 1px stroke raised the font's opaque bytes 10760 → 27262
and the on-screen ink 6036 → 10729, with black unchanged at 375. The glyph
got **fatter**; it did not gain an outline.

An outline needs two colours, so it has to come from something that knows
both - the renderer.

## Map keys

```ini
[shadow]
mode=outline     ; none | drop | outline | stroke | game
offset=2         ; thickness in output pixels
color=8          ; palette index of the stroke
```

- `mode=game` (the default) follows whatever the game asked for.
- `drop` is one-sided, `outline` surrounds evenly, `stroke` is an outline
  weighted towards the lower left.
- `offset` is in **output** pixels, so it scales with `[hires] scale`.

## Trap 1: the stroke colour is platform-specific

A black outline is naturally written `color=0`. That is wrong on two
platforms, for two different reasons:

**FM-Towns treats index 0 as transparent.** `gfx.cpp` clears the text plane
to 0 there rather than to `CHARSET_MASK_TRANSPARENCY`, and branches on the
platform to do it. A stroke in colour 0 is drawn correctly and then read back
as "nothing here". `gfx_towns.cpp` never mentions
`CHARSET_MASK_TRANSPARENCY` at all.

**The renderer skips a decoration in the text colour.** If
`shadowColor == color` the stroke is not drawn - a same-colour decoration is
invisible and only costs time. FM-Towns draws its text in colour 4
(`_townsCharsetColorMap`), so `color=4` silently produces nothing there.

Measured on English MI2, FM-Towns, dark pixels in the subtitle box:

| `color` | dark px | why |
|---|---|---|
| 0 | 297 | drawn, then read as transparent |
| 1, 2, 3 | 375 | not dark in `_textPalette` |
| 4 | 297 | skipped: same as the text colour |
| **8** | **5494** | works |

On DOS, `color=0` is correct and gives 4687 black pixels where the plain
font gives none.

## Trap 2: offset has to suit the font's weight

The bitmap font being replaced has 4px strokes with a 2px surround. A
replacement face with thinner strokes needs a *smaller* offset than a
dark-pixel count would suggest, because a heavy outline closes the gaps
between letters even while the totals match.

English MI2 on FM-Towns, against the original's 12168 dark / 5076 ink:

| configuration | dark | ink | note |
|---|---|---|---|
| DejaVu Sans, off=1 | 5494 | 6036 | too light |
| DejaVu Sans, off=2 | 9320 | 6036 | closest structurally |
| DejaVu Sans, off=3 | 12252 | 6036 | totals match, gaps fill in |
| DejaVu Sans **Bold**, off=2 | 8942 | 10175 | chosen |
| DejaVu Sans Bold, off=3 | 11911 | 10175 | heavy |

Matching the dark-pixel total is not the same as matching the look: at
`offset=3` the outline runs between adjacent letters instead of ringing each
one. Judge these on screen, not by the counts.

## What the TTF path can and cannot do

The TTF path bakes a face at startup (`bakeTtfFonts`, `hires_text.cpp:843`)
and hands the result to the same `drawGlyph`, so `[shadow]` applies to it
identically. Two limits, both measured:

**A map naming no `[bitmap]` fonts is not used at all.** `[fonts] default=`
parses, but `loadConfig` warns and the layer declines the map:

```
WARNING: '...hires_text.map' names no [bitmap] fonts; if this is an older
         TrueType map, the hi-res text layer will not use it!
```

So a TTF has to be named through `hires_text_font` in the config, not through
the map's `[fonts]` section.

**Baking only knows CJK glyph sets.** `bakeTtfFonts` has glyph lists for code
pages 949, 932, 936 and 950 and returns early for anything else:

```
WARNING: SCUMM: hi-res TrueType font: no glyph set for this language!
```

English (latin1) therefore cannot use the TTF path; it needs pre-baked `.fnt`
files from `mkfont.py`.

### The gap this leaves

`[shadow]` lives in the map, and a map without `[bitmap]` fonts is ignored.
Anyone using `hires_text_font` on its own has **no way to ask for a
decoration** - the same structural gap `[glyphs]` has on that path.

Worth fixing, in one of two ways:

1. accept a map that names only `[fonts]`, so the TTF path can carry
   `[shadow]` and `[glyphs]` too; or
2. add config keys mirroring the map's, which duplicates the surface.

(1) is the smaller change and keeps one place to describe a game's text.

## Reference implementations

- `graphics/sjis.cpp` - `createOutline` dilates a 1-bit glyph by OR-ing each
  row into three adjacent rows; `drawChar` blits the mask, then the body.
- SDL_ttf - `TTF_SetFontOutline` strokes the **vector contour** with
  `FT_Stroker` and rasterises that as a separate glyph, so the stroke gets
  its own coverage. The common recipe renders text twice into two surfaces
  and blits the outline one first.

Both keep the stroke a separate rendering with its own alpha. Neither derives
it from the body's coverage.
