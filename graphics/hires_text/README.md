# Hi-res text: font map reader (G1)

This stage parses configuration only. No engine calls it yet. It does not load
fonts, decode translations, lay out text or change rendering.

`HiResFontMap::loadFromStream` accepts an INI stream, its base directory, and
opaque qualifier strings in most-specific-first order. Keys fall back through
qualified sections to the bare section. Height-role maps merge in the reverse
order. The parser does not access engine types, ConfigManager or FreeType.

Example (comments must occupy their own lines):

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
size 1..4096, supersampling 1..16, shadow offset -1 or 0..4096, palette color
0..255, glyph count 1..0x110000, height key 1..65535. These parser limits are not
promises that every backend can allocate that size; loaders must check products
and memory budgets. Numeric overflow and trailing junk are rejected. Invalid
optional values preserve existing values. Unknown sections/keys are ignored.
Malformed INI or stream errors return false without modifying the output.

TTF paths are resolved relative to the map. Legacy bitmap templates and
translation names remain opaque adapter data. Never pass a bitmap template as
an unchecked printf format. The parser performs no allocations based on glyph
count and does not open referenced font or translation files.

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

Tests: `test/graphics/hires_text_font_map.h`, included by `make test`. Both
FreeType-enabled and no-engine/no-FreeType configurations must pass. G2 adds
actual glyph metrics and SVFN loading; G3 adds coverage rendering.
