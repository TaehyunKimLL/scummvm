# Hi-res text in the SCUMM engine

The engine's side of the hi-res text layer. Everything that knows about SCUMM
lives here; the font handling itself is in `graphics/hires_text` and has no
engine dependency, so a second engine can reuse it without inheriting SCUMM's
screen model.

## Current state

`ScummHiResText` holds the configuration and decides whether the hi-res path
applies at all. Nothing else reads it yet: fonts are not loaded, strings are not
decoded and nothing is drawn differently. This step is deliberately inert, so
that any rendering difference it causes is a bug rather than a feature.

## Configuration

Per target, in `scummvm.ini`:

```ini
hires_text_scale=3        ; 1-3, 1 disables
hires_text_alpha=true     ; keep a coverage surface for blending
hires_text_map=/path/to/hires_text.map
```

The older `korean_hires_scale`, `korean_alpha_text` and `korean_ttf_map` keys
are still read, so an existing install keeps working. The new names win when
both are present.

With no map key at all, `hires_text.map` and then `korean_ttf.map` are looked
for in the game folder, which lets a translation ship one and need no setup.

Map sections may be narrowed by game id or by SCUMM version, most specific
first - `[fonts:monkey2]`, then `[fonts:v5]`, then `[fonts]`. Those strings are
the engine's business; the parser treats them as opaque qualifiers.

## Ordering

`loadConfig()` runs before `loadCJKFont()`, because a map may name the scale the
rest of the setup works from. A user setting outranks the map, so logical font
sizes (`12pt`) are only resolved afterwards, by `resolvedFontSize()`.

## Enabling

`enabled()` is false unless a map actually loaded *and* it asks for something -
a scale above 1, or a replacement bitmap font. Being asked for is not the same
as being usable: without a map there is nothing naming the fonts, so the engine
stays on its original path. Every game we do not touch has to stay untouched,
and this is the switch that guarantees it.

The default source encoding follows the detected language (CP949 for Korean,
CP932 for Japanese, CP936/CP950 for Chinese) and is otherwise left unset - a
single byte game defines its own, and the adapter must be told explicitly
before assuming anything else.

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

## Verification

There is no unit test: SCUMM is not registered with the test runner, and this
code reads ConfMan and the filesystem. It is verified by the regression harness
instead (`~/games/regress.sh` + `rgdiff.py`), which must report **zero** changed
targets against the previous commit - the whole point of this step is that
nothing renders differently yet.
