# Setting up hi-res text: the ini, the map, and the fonts

Three things decide what you get: keys in `scummvm.ini`, a map file next to
the game data, and the font files themselves. This is the reference for all
three, taken from the code rather than from memory — every key below was read
out of `hires_text.cpp` and `font_map.cpp`.

For what the decorations do and how they behave per platform, see
`HIRES_TEXT_DECORATIONS.md`. For the internals, `HIRES_TEXT.md`.

## The shortest thing that works

Drop baked fonts into the game folder and nothing else. No map, no ini keys:

```
~/games/mi2kor/
  monkey2.000
  monkey2.001
  hires00.fnt        <- one per charset the game uses
  hires07.fnt
```

Two names, one per half of a line:

| name | holds | for |
|---|---|---|
| `hires%02d.fnt` | the double-byte set | CJK — Hangul, kana, hanzi |
| `hrlat%02d.fnt` | the single-byte set | Latin letters and punctuation |

`HIRES00.FNT` is the CJK font. A Korean, Japanese or Chinese translation
ships it for the glyphs the game's own charset cannot hold, and adds
`HRLAT00.FNT` for the letters that set has no room for — without the second
one, Latin characters keep the game's original font and a mixed line is
drawn at two qualities.

**A European translation ships `hrlat%02d.fnt` alone.** It has no
double-byte half, so there is no `hires%02d.fnt` to write. That works by
itself: the fonts are found, the scale is measured from them, and the
game-options checkbox appears.

Both names fit 8.3 — these files travel with game data that often sits on a
FAT volume or inside an archive built by a DOS-era tool, and a name the
filesystem truncates is a font that silently does not load.

`hrlat%02d.fnt` is also what the `[latin] bitmap=` line of an existing map
names, so a translation moving to the map-less form keeps the files it has.

If a font is filed under the other name, the layer still places it correctly:
the header records whether the glyphs are indexed by a code page or by a
single byte, and that decides which half it draws. The names above are the
convention, not a requirement.

Also read, for a game that uses one font throughout: `hires.fnt`.

This is the *simple form* — it exists so a translation can ship files and no
configuration. It works for a European game too: the scale comes from the
game's own charset, whatever the language. Measured on English MI2 with
eight 16px fonts and no ini key at all — 4 distinct colours in the subtitle
band without them, 199 with.

**The fonts must be a whole multiple of the game's cell.** 16px over an 8px
charset is 2x; 20px over 8px is refused outright and the game keeps its own
font, because there is no integer scale that draws it correctly. The log
names the sizes that would work — see "A whole multiple, or nothing" below.

**A game that repurposes its charset still needs a map.** MI2 draws an
ellipsis at `0x5e` and a skull at `0x07`; a replacement font has ordinary
letters at those codes and will draw them, because only `[glyphs]` can say
otherwise. The simple form has nowhere to write that down.

Anything beyond that needs a map.

## scummvm.ini

Per-game keys, in the game's own section:

```ini
[mi2kor]
gameid=monkey2
path=/games/mi2kor
# hires_text_map is relative to the game folder, or absolute
hires_text_map=hires_text.map
hires_text_font=/fonts/NanumGothic.ttf
hires_text_scale=2
hires_text_alpha=true
hires_text_metrics=font
hires_text_log=true
# hires_text=false is the off switch
hires_text=false
```

**`scummvm.ini` has no inline comments.** It is read by `ConfigManager`, not
by the map parser below, and a `;` after a value stays part of the value:
`hires_text=false ; the off switch` sets `hires_text` to the literal string
`false ; the off switch`, which is not the boolean `false`. Put a `; ...` or
`# ...` comment on its own line instead, as above. (The inline-comment rule
further down — whitespace before `;` ends a value — belongs to
`hires_text.map`, a different file with a different reader.)

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

### In the game options

Two checkboxes, shown only for a target that has something to switch — a map
or font in the game folder, or one of the keys set. Every other game's dialog
is unchanged.

| checkbox | key | off means |
|---|---|---|
| **Use hi-res fonts from the game folder** | `hires_text` | the map, the fonts and any face are ignored entirely |
| **Smooth the hi-res text** | `hires_text_alpha` | the same fonts, drawn with hard edges |

Measured on English MI2, distinct colours in the subtitle band:

| | colours | what it looks like |
|---|---|---|
| first box off | 4 | the game's original font |
| both on | 189 | blended edges |
| smoothing off | 3 | hi-res shapes, no intermediate tones |

3 is fewer than the original's 4, which looks alarming and is not: the count
drops because antialiasing is what produced the intermediate tones. The
glyphs are the replacement ones either way — checked on screen, not inferred
from the number.

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

**Comments in a map value.** A `;` ends the value only when whitespace comes
before it — `color=0 ; DOS` reads as `0`. Without that whitespace the `;`
stays part of the value (`single=my;font.fnt` keeps the whole filename). A
line whose first character is `;` or `#` is still a whole-line comment,
exactly as before. This applies to every key the map parser reads, on both
the SCUMM and SCI lines — it is the *map's* rule, not the ini's (see above).

This changes behaviour for a shipped map that already had an inline `; ...`
on a value line: it used to warn and leave that key at its default (see
"Per-game sections" below); it now applies the value the comment was always
sitting next to.

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
- `glyphs` — the shared parser reads this into `bitmapGlyphs`, but SCUMM
  does not apply it: `engines/scumm/hires_text.cpp` never reads that field.
  A `.fnt` file already carries its own glyph count in its header.

### `[latin]`

The single-byte half of a line, when the main font is double-byte. Without
it, Latin letters keep the game's original glyphs and a mixed line is drawn
at two different qualities.

- `enabled`, `bitmap` (a pattern) — read and applied by SCUMM.
- `font` (a TTF), `metrics` — the shared parser reads these too, but SCUMM
  does not apply them; they belong to SCI's Latin typeface path. On the
  SCUMM line, `bitmap=` is the only way to give the Latin half its own face.

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

A range covers many codes in one line:

```ini
[glyphs]
0x21-0x7E=+0xFEE0    ; ASCII to the fullwidth forms block, all at once
0x5e=keep            ; the caret in that range stays the game's own ellipsis
0x80-0x9F=keep       ; a whole block left to the game's font
0x41=+0x20           ; a single code, offset form - same as 0x41=0x61
```

- `<code>-<code>=+<n>` remaps every code in the range by the same offset
  (`n` is `0x..` or decimal, never `u+` — an offset is a distance, not a
  code point). `<code>-<code>=keep` leaves the whole range untouched.
- `+<n>` also works on a single code: `0x41=+0x20` is the same as `0x41=0x61`.
- **Precedence**, most specific wins: a qualified section (`[glyphs:cs1]`,
  see below) beats the bare `[glyphs]` section for the codes it names; within
  one section a single code always beats a range that covers it, whichever
  line comes first — that is how `0x5e=keep` above punches a hole in the
  `0x21-0x7E` range next to it; between two ranges in the same section, the
  later line wins for the codes they share.
- **Bounds**, each one warns and ignores just that one entry: a range must
  not end before it starts, its end must not exceed `0xFFFF` (game codes are
  at most double-byte), and `end + offset` must not exceed `U+10FFFF`. A
  range cannot take an absolute target — `0x21-0x7E=u+FF01` is refused,
  because it would draw 94 different codes as one glyph.
- **Table limit:** ranges may add at most 131072 codes per map load, counted
  over the common table and every scope together. A range that would cross
  the limit is ignored whole, with one warning; ranges earlier in the file
  still apply.
- The key is always a plain code range, never `u+21-u+7E`: `Common::INIFile`
  only allows alphanumerics, `-`, `_`, `.`, `:` and space in a key, so a `u+`
  key rejects the *whole map* — the same failure a single `u+`-keyed entry
  already causes.

### `[fonts]` and `[sizes]`

A TrueType face named by the map rather than by the ini:

```ini
[fonts]
default=/fonts/NanumGothic.ttf
bold=/fonts/NanumGothic-Bold.ttf

[sizes]
default=16
```

The shared table has three roles, `default`, `bold`, `title`, but **SCUMM
applies only `default=`.** `bold=` and `title=` parse without error — they
fill the same table — but nothing on the SCUMM line reads them; only SCI's
typeface path uses more than the default role. `[sizes]` parses in full too,
and SCUMM does not apply any of it: the size a TTF is baked at follows the
game's own cell and `hires_text_scale`, never a value named in the map.

### `[translation]`

```ini
[translation]
file=strings.txt
```

The shared parser reads `file=` into its config, but SCUMM does not apply
it. Translated text comes from the game's own resources, not from the map.

### Per-game sections

Any section may be qualified, and the qualified one wins:

```ini
[shadow]
color=0            ; DOS

[shadow:fmtowns]
color=8            ; where 0 is transparent
```

This now parses exactly as written — `color=0` on DOS, `color=8` on
FM-Towns — by the comment rule above. Before it, both lines warned
(`HiResText: invalid shadow color '0            ; DOS', ignoring`, and the
same for FM-Towns) and `shadowColorSet` stayed false, so a map written this
way silently kept the game's own outline colour. Check any map you already
ship for this pattern: after upgrading, it will start drawing the colour it
always looked like it named.

### Shared with SCI

SCUMM and the SCI engine read `hires_text.map` with the same parser. Some
sections belong to SCI alone — `[font.N]`, `[font.N:<platform>]`,
`[hires] font=`/`face=`/`size=` (`face=` is an alias for `font=`),
`[latin] mode=`/`space=` — and parse without error in a SCUMM map, but SCUMM
ignores every one of them. A badly-formed value in
one of them still produces a warning: the parser has no way to know which
engine will read the map before it reads it. See `HIRES_TEXT_MAP.md` in the
docs repo for the full section-by-section list of what each engine applies.

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

**A whole multiple, or nothing.** The layer enlarges the picture by an
integer, so a font baked at 1.5x or 2.5x has no scale that draws it
correctly. Rather than round — which would stretch every glyph — a map-less
set whose cell is not a whole multiple of the game's own is refused
outright, and the game keeps its original font:

```
WARNING: hi-res fonts are 20px for a 8px game font, which is not a whole
         multiple; ignoring them. Bake them at 16px or 24px
```

The whole set goes, not just the scale. Leaving the fonts loaded at scale 1
would draw 20px glyphs on a layout computed for 8px — overlapping, clipped
text that looks worse than the original.

This applies to the map-less form, where the scale is worked out from the
files. A map that says `scale=` is taken at its word.

### Baselines

A Latin face and a CJK face are aligned by their **ascent**, the row the
baseline sits on, which `mkfont.py` writes into the header.

They need it because the two fill their cells differently. Measured on the
shipped MI2 set at charset 7, a 24px cell:

| | ink rows | height |
|---|---|---|
| Hangul `가` | 0..21 | 22 |
| Latin `H` | 2..19 | 18 |
| Latin `x` | 7..19 | 13 |
| Latin `g` | 7..23 | 17 |

Latin ink is smaller **by design** — capitals leave headroom, `g` drops a
descender below the baseline. A Latin font that filled its cell like Hangul
would look wrong. So a Latin face is not "too small" when its ink is
shorter; it is right when its *baseline* matches.

The engine shifts the Latin glyph by the difference of the two ascents, so
a set baked with matching ascents needs no shift at all. Bake both halves
of a set with the same `--size` and `--cell` and this takes care of itself.
A font with no ascent recorded is drawn unshifted.

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
| fonts load, nothing is drawn | on a European game before this was fixed, a single-byte set filed under the numbered name went to the double-byte slot and was never consulted. The log now says `(single-byte, used as Latin)` when the set is routed by its header |
| `not a whole multiple; ignoring them` | the fonts' cell is not an integer multiple of the game's charset. Rebake at one of the two sizes the warning names |
| Latin letters sit higher or lower than the Hangul beside them | the two fonts record different ascents. Bake both halves of a set at the same `--size` and `--cell` |
| `source encoding other` | cosmetic. The log names only the four CJK pages, CP1252 and UTF-8; every other valid codepage — including `latin1` — prints as `other`. The map was parsed correctly |
| `fonts (none named)` | no map was found and no fonts matched `hires%02d.fnt` |
| `names no [bitmap] fonts` | a warning, not an error; the map is still used |
| `no CJK block for this language` | expected for a European game — Latin is baked alone |
| `hi-res scale 1 from the fonts (… over 0px game font)` | the simple form on a non-CJK game; set `[hires] scale` explicitly |
| `hi-res text is configured but no replacement font loaded` | fonts were named and none loaded; check paths and that the files are valid |
| `hi-res TrueType fonts need a build with FreeType` | this build cannot rasterise; bake to `.fnt` offline instead |

`hires_text_dump_baked=true` writes what was baked to `baked%02d.fnt`, which
can then be inspected with the same tools as a shipped font.
