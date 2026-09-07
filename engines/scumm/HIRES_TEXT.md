# Hi-res text in the SCUMM engine

The engine's side of the hi-res text layer. Everything that knows about SCUMM
lives here; the font handling itself is in `graphics/hires_text` and has no
engine dependency, so a second engine can reuse it without inheriting SCUMM's
screen model.

## What it does

A CJK translation ships bitmap fonts drawn on the game's 320x200 grid. This
layer lets it ship larger ones instead: the text surface is enlarged by an
integer factor (1-3), the translation's fonts are replaced by anti-aliased
`.fnt` files baked at that size, and the result is composited over the
unscaled game. Game graphics are untouched; only text is drawn differently.

`ScummHiResText` owns the configuration, the loaded fonts and the drawing
entry points that `CharsetRenderer` calls into. When it is not enabled the
engine never reaches this code, which is the guarantee that every game we do
not touch stays untouched.

## Configuration

Per target, in `scummvm.ini`:

| key | values | default | meaning |
|---|---|---|---|
| `hires_text_map` | path | *(none)* | map file; relative paths resolve against the game folder |
| `hires_text_scale` | 1-3 | from map, else 1 | text surface multiplier; outranks the map |
| `hires_text_alpha` | bool | from map, else false | keep a coverage surface and blend, instead of stencilling |
| `hires_text_metrics` | `game` / `font` | `game` | whose advances lay the line out (see below) |
| `hires_text_log` | bool | false | print one `HRTEXT` line per string drawn, with charset and font |

Codes a game repurposed for pictograms are named in the map's `[glyphs]`
section rather than by an ini key; see below.

The older `korean_hires_scale` and `korean_alpha_text` keys are still read,
so an existing install keeps working. The new names win when both are present.

`korean_ttf_map` and the `korean_ttf.map` file name are **not** read. Those
maps are in the TrueType-era format; honouring them let the legacy loader draw
the text while this layer supplied only the scale, and the mismatch showed up
as click drift in Loom. A configured `korean_ttf_map` produces a warning.

None of these keys can be passed on the command line; ScummVM rejects unknown
options there. They go in the target's section.

### Where the map comes from

1. `hires_text_map` in the target section, if set. A missing file is a warning
   and the layer stays off.
2. Otherwise `hires_text.map` in the game folder, if one is there. This is how
   a translation ships hi-res text and needs no setup.
3. Otherwise the layer is off.

Map sections may be narrowed by game id or by SCUMM version, most specific
first - `[fonts:monkey2]`, then `[fonts:v5]`, then `[fonts]`. Those strings are
the engine's business; the parser treats them as opaque qualifiers.

### There is no "off" switch yet

`enabled()` is true whenever a map loaded *and* it asks for something - a
scale above 1, or a replacement bitmap font. With a `hires_text.map` in the
game folder there is currently no ini key that turns the layer off short of
removing the file: `hires_text_scale=1` only drops the scale, the replacement
fonts still apply. A `hires_text=false` master switch is the planned fix and
the natural thing to bind a GUI checkbox to.

## Ordering

`loadConfig()` runs before `loadCJKFont()`, because a map may name the scale the
rest of the setup works from. A user setting outranks the map, so logical font
sizes (`12pt`) are only resolved afterwards, by `resolvedFontSize()`.

`loadFonts()` runs after the charsets are known. The map names a numbered set
(`multi=hr%02d.fnt`, one file per charset the game uses) or a single file
(`single=`) standing in for all of them; a charset without a file of its own
falls back to the nearest loaded one. Latin companions (`[latin] bitmap=`) are
loaded the same way, because a double-byte set carries no ASCII and a Latin
face at the wrong cell sits on a different baseline from the Hangul beside it.

The default source encoding follows the detected language (CP949 for Korean,
CP932 for Japanese, CP936/CP950 for Chinese) and is otherwise left unset - a
single byte game defines its own, and the adapter must be told explicitly
before assuming anything else.

## Glyphs a game drew itself

A game's own font is not a character set. LucasArts titles store an ellipsis
where Latin-1 has `^`, solid arrows where it has `_` and DEL, and Monkey
Island 2 puts a skull-and-crossbones dialogue bullet at `0x07`, which no Latin
face has anything for at all. Left alone, the layer routes every single byte
code to the replacement font and reports it as drawn - so the picture is not
merely wrong, the fallback that would have drawn the original never runs and
the character disappears.

`[glyphs]` names the exceptions:

```ini
[glyphs]
0x5e = keep        ; an ellipsis, not a caret
0x07 = keep        ; the dialogue bullet
0x7f = u+2192      ; drawn from the replacement at another code point

[glyphs:cs1]       ; charset 1 only
0x5f = keep
```

`keep` declines before a font is chosen, which is what lets the existing
fallback draw the game's own glyph; `advanceFor()` declines in step, so the
line still measures as the game laid it out. A `u+XXXX` value draws that code
point from the replacement instead.

Scopes matter because the repurposed slots differ per charset: `0x5F` is a
left arrow in a dialogue font and a genuine underscore in the others. The
adapter names each charset `cs0`, `cs1`, ... and a scoped entry overrides the
common table for that charset only.

Baking honours the same table. The run-time TrueType path bakes a fixed
Latin-1 block, so without this a remap would name a code point the font was
never given; the set is filtered per charset before baking, dropping `keep`
codes and adding remap targets.

Keys are parsed as `0x5e`, `94` or `u+2192`, but note that `Common::INIFile`
only allows alphanumerics, `-`, `_`, `.`, `:` and space in a **key**, and
rejects the whole map on anything else. So `u+2192` is fine as a value while a
key must be written `0x7f`.

## Decorations: outline and shadow

A map can ask for an outline or a drop shadow around every glyph:

```ini
[shadow]
mode=outline     ; none | drop | outline | stroke | game
offset=2         ; thickness in output pixels
color=8          ; palette index of the stroke
```

The decoration is built as a dilation mask and laid down solid before the
body, the way `FontSJISBase::drawChar` does it in `graphics/sjis.cpp` -
drawing the glyph again at offsets would give the stroke the body's own
antialiasing, and it would then blend into the background it exists to hide.

Two traps are worth knowing before writing a map:

- **`color=0` is not portable.** FM-Towns clears its text plane to 0 and
  reads 0 back as transparent, so a stroke in colour 0 vanishes there. It
  also skips any decoration drawn in the text colour, which is 4 on that
  platform. Use 8.
- **`offset` has to suit the replacement font's weight**, not the dark-pixel
  count. At `offset=3` a thin face's outline closes the gaps between letters
  even though the totals match the original.

See `HIRES_TEXT_DECORATIONS.md` for the measurements behind both, why a
baked-in stroke cannot work, and what the TTF path can carry.

## Metrics: whose advances to use
```
hires_text_metrics=game     ; default - the game's own advances
hires_text_metrics=font     ; the replacement font's advances
```

The game decides line breaks and speech-bubble sizes from the widths of its
own font, so a replacement that advances differently can wrap text in the wrong
place or push it out of a bubble. The default therefore keeps the original
spacing and merely draws a better glyph in the same box.

`metrics=font` is for a proportional replacement that should space itself.
Only fonts with a metrics table (SVFN header flags bit 0) are affected;
a fixed-width replacement advances by its cell whatever this says.

Measured on Indy3's proportional `vj00.fnt` at scale 2:

| character | font advance | scaled to game px | game said |
|---|---|---|---|
| U+D0B9 | 18 | 9 | 4 |
| U+E2B1 | 20 | 10 | 4 |
| U+E7B4 | 21 | 11 | 4 |
| U+B8BA | 19 | 10 | 4 |

Note the game advanced every character by 4 regardless; the replacement varies
per glyph, which is the point. With `metrics=game` the code is not consulted at
all - the probe recorded zero calls - so existing layouts cannot shift.

## Scaling, and platforms that already scale

`_textSurfaceMultiplier` decides both the text surface size and the resolution
the backend is asked for. Several places set it, in this order:

1. `loadCJKFont()` — resets it to 1 for the resource-font path, 2 for the
   FM-Towns and PC-Engine font ROMs
2. the FM-Towns `_forceFMTownsHiResMode` case
3. the Macintosh case for Indy3, Loom and Maniac
4. **the hi-res text layer** — last, so nothing later resets it

The factors are **not multiplied**. When a platform already scales the surface,
its value stands and the hi-res scale is ignored with a warning. Measured with
`~/games/multcheck.sh`:

| target | forced hi-res scale | backend |
|---|---|---|
| `ja-mi2` (FM-Towns) | none | 640x400 |
| `ja-mi2` (FM-Towns) | 3 | 640x400 — ignored |
| `en-mi1towns` (FM-Towns) | 3 | 640x400 — ignored |
| `mi2-svfn` (PC, Korean) | none | 960x600 |

The reason is not caution about arithmetic. FM-Towns doubling emulates a second
hardware text layer: `_townsScreen`, cleared per layer and written through
`towns_fillTopLayerRect()`, with its own branch in `drawStripToScreen()`. The
hi-res layer instead draws into one larger surface and composites. Multiplying
the two would ask for a size neither path knows how to composite.

The cost is real: an FM-Towns game cannot use a hi-res scale. Removing that
limit means reworking the Towns layer path, which is deliberately out of scope
here - it would touch every SCUMM platform at once.

v7 games (Full Throttle, The Dig) blit their own screen and are not enlarged
either; they take replacement fonts at scale 1 only, and SMUSH subtitles still
go through `NutRenderer` untouched.

## Diagnostics

Run with `-d1` and read the log:

| line | meaning |
|---|---|
| `hi-res map read: '...' (from the config / found in the game folder)` | which map was used and why |
| `hi-res map REJECTED` | the parser refused it; see `graphics/hires_text/README.md` for the limits |
| `'...' names no [bitmap] fonts; if this is an older TrueType map ...` | a stale map; regenerate it |
| `hi-res text enabled: scale N, alpha ..., fonts ...` | the layer is on |
| `hi-res font N <- file: WxH cell, bpp, glyphs, ...` | one line per font actually loaded |
| `... is not a usable hi-res font` | a named file exists but is not SVFN |
| `HRTEXT charset=N font=M cell=WxH "..."` | with `hires_text_log=true`, every string drawn |

No `hi-res text enabled` line means the engine is on its original path,
whatever the ini says.

## Verification

There is no unit test for this file: SCUMM is not registered with the test
runner, and this code reads ConfMan and the filesystem. It is verified by the
regression harness instead (`~/games/regress.sh` + `rgdiff.py`), which must
report **zero** changed targets for any commit that is not meant to change
rendering. Font map parsing and bitmap font loading have unit tests under
`test/graphics/`.
