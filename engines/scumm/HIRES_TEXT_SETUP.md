# Setting up hi-res text: the ini, the map, and the fonts

Three things decide what you get: keys in `scummvm.ini`, a map file next to
the game data, and the font files themselves. This is the reference for all
three, taken from the code rather than from memory — every key below was read
out of `hires_text.cpp` and `font_map.cpp`.

For what the decorations do and how they behave per platform, see
`HIRES_TEXT_DECORATIONS.md`. For the internals, `HIRES_TEXT.md`.

## The shortest thing that works

Drop baked fonts named `hires00.fnt`, `hires01.fnt`, … into the game folder
and nothing else. No map, no ini keys:

```
~/games/mi2kor/
  monkey2.000
  monkey2.001
  hires00.fnt        <- one per charset the game uses
  hires07.fnt
```

The layer measures the fonts against the game's own font and picks the scale
itself. This is the *simple form* — it exists so a translation can ship files
and no configuration.

**It only picks a scale for a CJK game.** The measurement compares the font's
cell against the game's double-byte font height, and a game not in CJK mode
has none — `scumm.cpp` passes 0 — so the scale stays 1 and the fonts are drawn
at original size. Measured on English MI2:

```
SCUMM: hi-res scale 1 from the fonts (16px cell over 0px game font)
```

For a European game, say so explicitly instead:

```ini
[hires]
scale=2
alpha=true
```

Anything beyond the simple form needs a map.

## scummvm.ini

Per-game keys, in the game's own section:

```ini
[mi2kor]
gameid=monkey2
path=/games/mi2kor
hires_text_map=hires_text.map     ; relative to the game folder, or absolute
hires_text_font=/fonts/NanumGothic.ttf
hires_text_scale=2
hires_text_alpha=true
hires_text_metrics=font
hires_text_log=true
hires_text=false                  ; the off switch
```

| key | type | what it does |
|---|---|---|
| `hires_text` | bool | the master switch — see below |
| `hires_text_map` | path | which map to read; otherwise `hires_text.map` in the game folder |
| `hires_text_font` | path | a TrueType face, baked at start-up |
| `hires_text_scale` | 1–3 | text surface multiplier |
| `hires_text_alpha` | bool | antialiased blending rather than keyed text |
| `hires_text_metrics` | `font`/`game` | whose advance widths to use |
| `hires_text_log` | bool | log every line drawn and which font drew it |
| `hires_text_dump_baked` | bool | write baked fonts to `baked%02d.fnt` for inspection |

**A user key beats the map.** That is deliberate: the map is the translator's
intent, the ini is the person running it.

### The master switch

`hires_text=false` is checked **before anything is read**, so it does not
merely stop the drawing — the map, the fonts named beside it and any TrueType
face are all ignored, and the game runs exactly as it would with none of them
present. A disabled feature that still parsed its map would still complain
about it, which is not what someone who turned it off wants.

Measured, one row per way in:

| available | `hires_text` unset | `hires_text=false` |
|---|---|---|
| `hires_text.map` | map read, layer on | nothing read |
| `hires00.fnt` … | fonts probed, layer on | nothing probed |
| `hires_text_font=` | face baked, layer on | nothing baked |

In the ScummVM game options this is the **"Use hi-res fonts from the game
folder"** checkbox. It appears only for a target that has something to switch
— a map or font in the game folder, or one of the keys set — so every other
game's options dialog is unchanged.

**`hires_text_log` needs `-d1`** on the command line. Every diagnostic in this
layer is `debug(1, ...)`, so without it the log is silent and the feature
looks dead.

Three older keys are still read for compatibility and do the same as their
`hires_text_` equivalents: `korean_ttf_map`, `korean_hires_scale`,
`korean_alpha_text`. Prefer the new names.

## The map file

An ini-format file, by default `hires_text.map` beside the game data. A full
one:

```ini
[hires]
scale=2
alpha=true

[encoding]
codepage=cp949

[bitmap]
multi=korean%02d.fnt
single=korean.fnt
glyphs=2350

[latin]
enabled=true
bitmap=hrlat%02d.fnt
metrics=font

[render]
metrics=font

[shadow]
mode=outline
offset=2
color=0

[glyphs]
0x07=keep
0x5e=0x2026

[translation]
file=strings.txt
```

### `[hires]`

| key | values | default |
|---|---|---|
| `scale` | 1–3 | 1 |
| `alpha` | true/false | false |

3 is a policy limit, not a technical one: at 3x a 320x200 game needs a
960x600 text surface.

### `[encoding] codepage`

What the game's own bytes mean. Single-byte: `latin1`/`iso-8859-1`,
`iso-8859-2`, `iso-8859-5`, `cp1250`–`cp1257`, `cp850`, `cp862`, `cp866`,
`macroman`, `maccentraleurope`, `ascii`. Double-byte: `cp949` (Korean),
`cp932` (Japanese), `cp936` (Simplified Chinese), `cp950` (Traditional).

This also decides which glyph set a TrueType face is baked with — see below.

### `[bitmap]`

The primary path: baked `.fnt` files, which carry the same antialiased shapes
a rasteriser would produce and need no FreeType at run time.

- `multi` — a pattern, one font per charset, e.g. `korean%02d.fnt`
- `single` — one font for every charset
- `glyphs` — how many glyphs the file holds, for double-byte sets

### `[latin]`

The single-byte half of a line, when the main font is double-byte. Without
it, Latin letters keep the game's original glyphs and a mixed line is drawn
at two different qualities.

- `enabled`, `bitmap` (a pattern), `font` (a TTF), `metrics`

### `[render] metrics`

`font` uses the replacement font's advance widths, `game` keeps the original
layout. Default `game` — safer, because the engine's own line breaking was
computed against those widths.

### `[shadow]`

`mode` is `none`, `drop`, `outline`, `stroke`, or `game` (follow whatever the
game asked for). `offset` is the thickness in pixels, `color` a palette
index.

**The colour is platform-specific.** On FM-Towns index 0 is transparent and
index 4 is the text colour — a decoration in either is invisible, and 8 is
the usual answer. On DOS 0 is a normal black. `HIRES_TEXT_DECORATIONS.md` has
the measurements.

### `[glyphs]`

Character codes the replacement font must not touch. A game reuses codes in
its charset for pictograms — MI2's skull at `0x07`, DOTT's arrows — and those
are drawn from the game's own font, not from a face that has a letter there.

- `0x07=keep` — leave this code to the original font
- `0x5e=0x2026` — draw this code using that Unicode code point instead

### `[fonts]` and `[sizes]`

A TrueType face named by the map rather than by the ini:

```ini
[fonts]
default=/fonts/NanumGothic.ttf
bold=/fonts/NanumGothic-Bold.ttf

[sizes]
default=16
```

Roles are `default`, `bold`, `title`.

### Per-game sections

Any section may be qualified, and the qualified one wins:

```ini
[shadow]
color=0            ; DOS

[shadow:fmtowns]
color=8            ; where 0 is transparent
```

## Using a TrueType face directly

A face is baked into bitmap fonts at start-up, so everything downstream —
decorations, metrics, the no-FreeType build — sees only bitmap fonts.

```ini
[mi2en]
hires_text_font=/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf
```

Measured on English MI2: 4 distinct colours in the subtitle band without it
(i.e. the original font), 104 with.

Two things to know:

**The face is baked at the game's own cell**, measured when a charset is first
selected. MI2's charset 7 is 14px, so at scale 2 the bake is 28x28. Only the
charsets a game actually uses are baked.

**Which glyphs get baked follows `[encoding] codepage`.** A CJK codepage bakes
that block plus Latin; anything else bakes Latin alone. A Korean game pointed
at a Latin-only face will render its Latin halves hi-res and leave Hangul
original — two qualities on one line.

A face alone cannot carry decorations, because `[shadow]` lives in the map.
`hires_text_font` plus a map with only `[shadow]` in it is a valid
combination and does work — measured: 1026 outline pixels around the text
versus 0 without the map.

## Baking the font files

`.fnt` files are made offline with `mkfont.py` from
`scummvm-korean-ttf/scripts`. Baking beats `hires_text_font` for anything you
ship: it costs no start-up time, needs no FreeType in the player's build, and
the result can be inspected.

A Latin set at 2x for a game whose charsets are 8px:

```bash
python3 mkfont.py DejaVuSans-Bold.ttf hrlat00.fnt --size 16 --bpp 8 --latin
```

A Korean set:

```bash
python3 mkfont.py NanumGothic.ttf korean00.fnt \
    --size 32 --cell 32 --bpp 8 --codepage 949
```

The options that matter:

| option | when |
|---|---|
| `--size` | render size in pixels — the game's cell times the scale |
| `--cell` | cell height if it differs from `--size`; a face whose ink is smaller than its nominal size needs a larger `--size` and the cell set to fit |
| `--width` | cell width, defaults to the height |
| `--bpp 8` | antialiased. `--bpp 1` is on/off, for a game not using alpha |
| `--codepage` | 932/936/949/950 — which double-byte block to bake |
| `--latin` | bake the single-byte range instead |
| `--center` | centre the ink in a fixed cell, for the v0–v2 engines |
| `--outline N` | a heavier face, **not an outline** — see below |

**Match the size to the charset, not to a guess.** Each charset has its own
cell; MI2's charset 7 is 14px, so its font at 2x is 28px. Run with
`hires_text_log=true -d1` and the layer prints the cell it wants.

## What cannot be baked into a font

An outline needs two colours; a `.fnt` glyph stores one channel — coverage.
Baking a stroke into the font with Pillow's `stroke_fill` produces **a
heavier glyph**, not an outlined one: the file can only say "there is ink
here", and the engine paints all of it in the text colour.

Measured: body ink 6036 → 10729 (+78%), outline pixels 375 → 375.

So decorations are the renderer's job, and `mkfont.py --outline` is a
weight control despite its name.

## Diagnosing a setup that does nothing

Run with `-d1` and read the first hi-res line:

```
SCUMM: hi-res text enabled: scale 2, alpha on, metrics font,
       source encoding CP949/Korean, fonts korean%02d.fnt
```

| symptom | cause |
|---|---|
| no `hi-res text enabled` line at all | the layer never started — check `-d1` is present, and that the config section has `engineid=scumm` |
| `source encoding other` | cosmetic. The log names only the four CJK pages, CP1252 and UTF-8; every other valid codepage — including `latin1` — prints as `other`. The map was parsed correctly |
| `fonts (none named)` | no map was found and no fonts matched `hires%02d.fnt` |
| `names no [bitmap] fonts` | a warning, not an error; the map is still used |
| `no CJK block for this language` | expected for a European game — Latin is baked alone |
| `hi-res scale 1 from the fonts (… over 0px game font)` | the simple form on a non-CJK game; set `[hires] scale` explicitly |
| `hi-res text is configured but no replacement font loaded` | fonts were named and none loaded; check paths and that the files are valid |
| `hi-res TrueType fonts need a build with FreeType` | this build cannot rasterise; bake to `.fnt` offline instead |

`hires_text_dump_baked=true` writes what was baked to `baked%02d.fnt`, which
can then be inspected with the same tools as a shipped font.
