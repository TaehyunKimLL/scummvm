# Hi-res text: font map reader (G1)

This stage parses configuration only. No engine calls it yet. It does not load
fonts, decode translations, lay out text or change rendering.

`HiResFontMap::loadFromStream` accepts an INI stream, its base directory, and
opaque qualifier strings in most-specific-first order. Keys fall back through
qualified sections to the bare section. Height-role maps merge in the reverse
order. The parser does not access engine types, ConfigManager or FreeType.

Example (a value ends at a `;` with whitespace before it, e.g. `scale=3 ;
comment`; without that whitespace the `;` stays part of the value, e.g.
`single=my;font.fnt`; a line whose first character is `;` or `#` is a
whole-line comment either way):

```ini
[hires]
scale=3
alpha=true

[encoding]
codepage=utf8

[bitmap]
single=subtitle.fnt

[render]
metrics=font

[fonts]
default=fonts/subtitle.ttf

[sizes]
default=12pt

[map]
height_12=default

[glyphs]
0x5e=keep
0x7f=u+2192
```

`codepage` records an adapter's source encoding choice, not a renderer gate.
Supported names: cp932/sjis, cp936/gbk, cp949/uhc, cp950/big5, johab, utf8/utf-8;
also cp1250..cp1257, iso-8859-1/latin1, iso-8859-2, iso-8859-5, macroman,
maccentraleurope, cp850, cp862, cp866 and ascii.
ksc5601/euc-kr select the CP949 decoder (a compatible superset, not strict EUC-KR
validation). UTF-16 input and engine control-token handling are later stages.

New `render.metrics=game|font` applies to all text, independent of script or
encoded byte length. `game` is the safe default. Old `[latin]` data is isolated
in `LegacyFontMapOptions` for adapter compatibility; it must not become the
generic renderer's character classification. Legacy `metrics=ttf` and
`metrics=bitmap` retain separate settings. Naming a legacy TTF font does not
implicitly enable it; a legacy bitmap name does, as in the original parser.

Size syntax: `N` physical pixels, `NxM` size N with supersampling M, `Npt` legacy
logical pixels (not typographic points). Logical sizes remain unresolved until
an adapter has applied explicit user scale overrides. Bounds: scale 1..3,
size 1..4096, supersampling 1..16, shadow offset -1 or 0..4096, shadow width
0..8 px in quarters, shadow shift -16..16, shadow alpha 0..100, palette color
0..255, glyph count 1..0x110000, height key 1..65535. These parser limits are not
promises that every backend can allocate that size; loaders must check products
and memory budgets. Numeric overflow and trailing junk are rejected. Invalid
optional values preserve existing values. Unknown sections/keys are ignored.
Malformed INI or stream errors return false without modifying the output.

TTF paths are resolved relative to the map. Legacy bitmap templates and
translation names remain opaque adapter data. Never pass a bitmap template as
an unchecked printf format. The parser performs no allocations based on glyph
count and does not open referenced font or translation files.

`[glyphs]` records per-character exceptions: `keep` for a code the caller
should not draw from a replacement font at all, or a code point value to draw
in its place. Both sides accept `0x5e`, decimal, or `u+2192`; values above
0x10FFFF and trailing junk are rejected, and a rejected entry is skipped
rather than failing the map. The parser attaches no meaning to either action -
what "keep" does is the adapter's business.

A key may also be a range, `<code>-<code>` (each half parsed the same as a
single key, so `0x21 - 0x7E` works too): `<code>-<code>=keep`, or
`<code>-<code>=+<n>` to remap every code in the range by the same offset
(`<n>` is `0x..` or decimal, never `u+` - an offset is a distance, not a
code point; an absolute target on a range is rejected, since it would draw
every code in the range as one glyph). `+<n>` is also accepted on a single
code. Within one section a single code always beats a range that covers it;
between two ranges in one section the later one wins for the codes they
share; a qualified section beats the bare one as usual. Bounds: a range must
not end before it starts, its end is capped at `0xFFFF`, and `end + offset`
at `U+10FFFF`; a bad range warns and is skipped, like a bad single entry.
Ranges may add at most 131072 codes per load, summed across the common table
and every scope - a range that would cross that limit is skipped whole, with
one warning.

Sections may also be narrowed by a caller-supplied **scope**, passed to
`load()` as an array of names. `[glyphs:cs1]` fills
`scopedGlyphOverrides[1]` when the caller listed `"cs1"` second, and
`glyphOverride(code, out, scope)` prefers a scope's table over the common one.
Scopes exist because which characters are repurposed can differ per font
within one game; the parser treats the names as opaque, exactly as it does
qualifiers.

Note `Common::INIFile` restricts **key** names to alphanumerics, `-`, `_`,
`.`, `:` and space, and rejects the entire file on anything else - so a
`u+XXXX` form is usable as a value but not as a key.

## Bitmap fonts (G2)

`HiResBitmapFont` reads the "SVFN" bitmap font format: 1bpp stencil or 8bpp
coverage, optional per-glyph metrics. The 8bpp form carries anti-aliased shapes
baked by a tool, so a build without FreeType renders the same glyphs; that makes
it the primary format rather than a fallback.

Lookup is by Unicode code point. Files written so far order their glyphs by a
code page named in the header (949 for the Korean sets, 0 for the single byte
sets); the loader builds a code point map for that block by decoding it, so
existing files keep working. Version 2 files carry their own code point table
and need no code page.

The CJK block decoding uses ScummVM's shared conversion tables, so
`encoding.dat` must be reachable at run time. Without it every CJK lookup
returns -1 and the engine already warns "Support for CJK is disabled".
Single byte fonts do not depend on it.

These fonts hold only their double byte block: ASCII resolves to -1 in a Korean
font and belongs to a separate single byte font. Header offsets are all checked
against the file, `load` bounds its allocation with a caller-supplied limit, and
a metrics table that does not fit is dropped without losing the glyphs.

Verified against the shipped fonts inside a running engine (MI2 and Indy3
Korean targets): U+AC00 maps to glyph 0 and U+D79D to glyph 2349 in the 2350
glyph Korean sets, 'A' maps to glyph 65 in the Latin sets, and the engine's own
legacy `.fnt` files are rejected so the caller can fall back.

## Glyph rendering (G3)

`HiResGlyphRenderer::drawGlyph` draws one glyph of a `HiResBitmapFont` onto a
CLUT8 surface. A paletted surface cannot hold coverage, so the colour goes to
the text surface and the coverage to a parallel 8bpp one that the caller blends
against the background. The coverage surface is optional: without it an 8bpp
font still draws as a stencil, so a backend with no alpha path is not left
blank. That stencil keeps the pixels covered at least a quarter
(`kKeyedInkThreshold`, 0x40): any coverage turns the stroke fringe into ink and
closes small glyphs into blobs, while half coverage drops thin CJK strokes. A
1bpp font writes no coverage at all - it has none to record.

### Decoration (C19)

A decoration is built from the glyph's **coverage**, whatever the source:
TrueType, 8bpp SVFN, a baked face or a 1bpp stencil (read as 0/255). Nothing
here knows about scripts, so a Thai or Japanese line is outlined exactly like
a Korean one.

- **Outline**: the coverage dilated by a soft disk (`buildKernel`, `dilate`).
  A round pen of radius r gives each tap k = r + 1 - distance, so the outline
  has an antialiased rim of any width; 1.5 px has 21 taps weighing 255, 128
  and 67. Coverage is read as distance, not opacity: alpha(p) = max over taps
  of clamp(k - (1 - cov(p - tap))). A partly covered stem pushes the outline's
  edge back instead of dimming the whole outline, so a thin face whose stems
  straddle two pixels still gets a solid outline, as a vector stroker would. `square` uses the larger axis distance. `legacy` is the old binary
  offset table of the mode (8 neighbours, or the 11-offset lower-left stroke),
  grown by the step (a Minkowski sum) rather than multiplied, so a step above
  one leaves no gaps. Legacy keeps the old step too: `offset=` as written (0 draws
  nothing, as it did), else 1 at every scale.
- **Shadow**: that alpha moved by (dx, dy), at `shadowAlpha`. A drop is the
  glyph's own coverage moved; a stroke is the outline plus a shadow of it at
  (-offset, +offset). An explicit `shadow=` replaces a mode's own shadow.

Where it goes depends on the planes the caller hands over (`GlyphPlanes`):

- **Layered** (index, coverage and both under planes): the decoration goes into
  the under planes only, keeping the strongest value any glyph put there, so
  glyphs may be drawn in any order, a stacked mark's outline merges with its
  base's, and one glyph's outline never erases another's body. The body planes
  get exactly what they would with no decoration. The compositor blends the
  under layer over the picture and the body over that, so the body's
  antialiased edge sits on the outline rather than showing the picture as a
  seam inside it.
- **Coverage, no under planes**: the decoration shares the body's planes and is
  solid: the keyed body dilated and cut at half (`kKeyedDecorationThreshold`),
  written only where no ink is recorded yet. The body is drawn over it.
- **Keyed** (no coverage): the same solid mask, from the body keyed at
  `kKeyedInkThreshold`; it does not overwrite pixels already in the text
  colour. A shadow under half strength is not drawn.

A decoration in the text colour is skipped. `shadowOffset` and the width are in
destination pixels. The dirty rectangle includes the pen's reach and the
shadow's offset.

`applyMap(style, map, scale)` turns a map's `[shadow]` keys into a style, with
these defaults for text drawn `scale` output pixels per game pixel: outline
width 0.75 x scale (1.5 px at 2x), round; `offset=` alone still sets the width
(as it always has) and the shadow distance; without it the shadow distance is
half a game pixel rounded up. Map keys, all in `[shadow]`:

```ini
mode=outline        ; none | drop | outline | stroke | game (unchanged)
color=0             ; decoration colour (unchanged)
offset=1            ; shadow distance, and the width when width= is absent
width=1.5           ; outline radius, output pixels, to a quarter (0..8)
style=round         ; round | square | legacy
shadow=-1,1         ; a shadow of the outline, dx,dy output px; none = off
shadow_color=0      ; defaults to color
shadow_alpha=60     ; 0..100, blended targets; keyed: >= 50 solid, else none
```

The pen is built once and kept while the decoration asked for stays the same
(a one-entry cache), and the dilation scratch buffer is reused. The dilation
itself is per draw call: a glyph is a few hundred pixels and only covered ones
do any work, so no per-glyph cache is kept.

Nothing here scales: a font is baked at the size it is drawn. The optional dirty
rectangle is extended, not replaced, and includes the decoration, since the
engine's own charset mask does not track text on this surface.

Verified with the shipped fonts: MI2 `svfn00.fnt` glyphs 0/1/2 render as the
Hangul syllables 가/각/간 (the KS X 1001 order the loader reports), Indy3
`latin24.fnt` renders `H e l o !` with visibly narrower ink for `l` and `!`, and
coverage carries 249 distinct levels - real anti-aliasing, with no FreeType in
the build. Coverage rises monotonically across the decoration modes
(278/361/481/544 pixels) and the letter body stays intact in every one.

## Glyph sources (G4)

`HiResGlyphSource` is what makes a build without FreeType behave like one with
it: the renderer and the engine adapters only ever see this interface, and a
baked bitmap font satisfies it exactly as a rasteriser does. A `GlyphBitmap` is
coverage plus an origin relative to the pen, so descenders and overhangs are
placed correctly whichever produced them.

`HiResBitmapGlyphSource` adds no rasterising and no allocation - glyphs are
handed out as they sit in the file. `HiResTtfGlyphSource` (only compiled with
`USE_FREETYPE2`) wraps a face loaded by `Graphics::loadTTFFont`. ScummVM's TTF
wrapper writes coverage into an alpha channel that a CLUT8 destination cannot
hold, so this rasterises into a 32bpp scratch in opaque white and reads the
coverage back out of the alpha byte. It caches the last glyph, bounds the box a
face may ask for, and can box-filter a supersampled raster back down, which
keeps a pixel font on its native grid when the line box is not a multiple of it.

TrueType is a convenience: everything it produces can be baked ahead of time,
which is what a build without FreeType uses. It exists so a translation can
point at a .ttf during development without baking first.

Verified with a real face (NanumJangMiCe at 36px): U+AC00/AC01/AC04 and A/i/W
all render correctly through the same call, with 233 distinct coverage levels
and per-glyph advances (28/25/25/14/6/27). Supersampling 2 at 18px gives the
same advances as supersampling 1.

Tests: `test/graphics/hires_text_font_map.h`, included by `make test`. Both
FreeType-enabled and no-engine/no-FreeType configurations must pass. G2 adds
actual glyph metrics and SVFN loading; G3 adds coverage rendering.
