# Jamo-set (Johab "벌") bitmap fonts in the hi-res text layer

Date: 2026-10-14. Branch: jamo (from johab). Status: design.

## Why

A 1bpp hi-res face holds every Hangul syllable it draws, precomposed: DOTT's
six 1bpp faces are 1,954 KB, and DOTTL0 (46x54) alone 803 KB. On the 8-16 MB
DOS machines the port targets that is real memory, and a disk cache to stream
it from (SMARTDRV) costs memory too. A jamo-set font holds only positional
jamo pieces - 8x4x4 is 344 of them - and composes any of the 11,172 syllables
by OR-ing two or three, as the DOS Hangul systems did. Measured on the DOS
packs (`dists/engine-data/hires_text/dos`): DOTT 1bpp 1,954 KB -> ~320 KB,
MI1 607 -> ~110, MI2 486 -> ~140, KO2350 124 -> ~13. Anti-aliased (2bpp)
faces stay precomposed: their quality comes from the designed syllables.

The DOS era used many set systems (docs/johab/2026-10-14-johab-font-research.md
section 3, formats doc section 2): 8x4x4, 10x4x4, hanterm 10x(3|4)x4, 6x2x1,
Dokkaebi printer 7x2x2 and 12x4x4, Kyra's jamo-major 10x4x4. The engine must
not hard-code one of them.

## What

### The file: an SVFN with a jamo-set table

A jamo-set font is an ordinary SVFN v2 file (`graphics/hires_text/README.md`)
whose header flags carry bit 1 (0x2, "glyphs are jamo, composed at run time",
reserved for this in bitmap_font.cpp and mkfont.py's `FLAG_JAMO`), and whose code
point table is followed immediately by a **JSET** block. Everything else is
unchanged, so an older reader sees a plain font.

- The pieces are glyphs at code points U+F0000 + piece number (plane 15 private
  use). Piece numbers are the font's own; the JSET tables name them.
- Any other glyph (ASCII, symbols, compatibility jamo the tool chose to store
  whole) is looked up as in any SVFN.

JSET block, little-endian:

```
4  "JSET"
2  version (1)
1  LC  lead classes   (1..4)      2  TC  tail classes (1..4)
1  NI  initial sets   (1..16)     1  NM  medial sets  (1..16)     1  NF final sets (1..16)
1  advance            syllable advance in px; 0 = the cell width
19 leadClass[19]      class of each initial (ㄱ..ㅎ), < LC
28 tailClass[28]      class of each tail, index 0 = no tail, < TC
LC*21*TC initSet      set of the initial for (leadClass, vowel, tailClass), < NI
LC*21*TC medSet       set of the medial
LC*21*TC finSet       set of the final (read only when tail != 0)
NI*19 u16 initPiece   piece number per (set, initial); 0xFFFF = blank
NM*21 u16 medPiece    per (set, vowel)
NF*27 u16 finPiece    per (set, tail 1..27)
51 u16 compatPiece    U+3131..U+3163 drawn from one piece; 0xFFFF = not by piece
```

Three tables indexed by (lead class, vowel, tail class) cover every system in
the research: 8x4x4 (lead classes {ㄱㅋ, rest}, tail classes {none, some}),
10x4x4, hanterm (tail classes {none, ㄴ, other}), 6x2x1, the printer sets,
Kyra (its jamo-major order is just other piece numbers). A system that needs
more than 4 classes or 16 sets is out of scope until one is found.

### The engine: JamoGlyphSource

`graphics/hires_text/glyph_source_jamo.{h,cpp}`: a `UnicodeGlyphSource` over
an `SvfnGlyphSource` of the same font.

- U+AC00..U+D7A3: L, V, T by arithmetic; sets from the tables; the pieces'
  coverage combined per pixel by maximum (OR at 1bpp, so 2bpp and 8bpp sets
  work too). A blank piece adds nothing.
- U+3131..U+3163 with a compatPiece: that piece alone.
- Anything else, and syllables when the block is absent: the inner SVFN source.
- `cells()` 2 for a syllable, `advance()` the block's advance (cell width when
  0), `baselineRow()` the font's. Composed rows are cached like
  SvfnGlyphSource's (HIRES_SVF_CACHE_KB, least recently used); a piece's rows
  are copied before combining (a row() pointer holds only until another code
  point is first asked for).
- `prefetch()` asks the inner source for the pieces the syllables need.
- `createSvfnSource()` (glyph_source_file.cpp) returns a JamoGlyphSource when
  the flag is set, so a map names the file like any face:
  `ui=data:DOTTJ0.SVF`. No new map key.

A block that fails validation (sizes, classes, sets or piece numbers out of
range, a piece number with no glyph) makes the font load as a plain SVFN and
warns once with the file name; syllables then fall through the face chain.

### The tool: tools/korean/jamoset.py

Builds a jamo-set SVF from a source font and a named layout:

```
jamoset.py --layout 844 GAM.KOR --eng SAN.ENG -o KOGAMJ.SVF
jamoset.py --layout dkb-printer-12x4x4 --face 1 HANGUL48.FNT -o DKB48J.SVF
```

- Layouts are Python tables, one per verified system (844, 844-ihnm, 1044-san,
  hanterm-johab, 621, dkb-printer-7x2x2, dkb-printer-12x4x4, kyra-lok), each
  producing the JSET tables from the selection rules in the research doc.
  Readers come from `~/work/johab-fonts/tools/kfont.py` (copied in, as the
  formats doc names them) for raw, BDF and printer files.
- ASCII from `--eng` (raw 8x16) or `--base` (an SVFN whose non-Hangul glyphs
  are kept), as johab844.py does.
- `--check`: composes all 11,172 syllables in Python with the same rule the
  engine uses and, for 844, compares against johab844.py's output (must be
  byte-identical).

Size generation (pieces at sizes no source has) is a separate design, after
the technical review in `runs/jamo-gen-review/`.

## Tests

- `test/graphics/hires_text_jamo.h`: a synthetic 8x4x4 font built in the test
  (pieces as single marked pixels): 가, 각, 뷁 compose from the expected pieces;
  a syllable whose final set differs by vowel picks it; 2bpp pieces combine by
  max; compat ㄱ draws its piece; ASCII passes to the inner font; a bad JSET
  (class out of range, piece without glyph) loads as plain SVFN with one warning;
  a font without the flag behaves byte for byte as before.
- `tools/korean/test_jamoset.py`: each layout's tables against hand-checked
  syllables from the research doc's tables; 844 output equals johab844.py's
  composition for all 11,172 syllables.
- A DOS-size check: KOGAMJ.SVF size, and a hi-res map using it drawing
  "다람쥐 헌 쳇바퀴에 타고파 뷁 흙 똠" in a SCUMM test target (screenshot).

## Risks

- Quality: composed syllables are cruder than designed ones at large sizes
  (research section 5). The format does not decide which faces become jamo
  sets; the packs do, per face.
- Performance: a syllable is 2-3 piece copies plus a max per pixel, once per
  cache miss; negligible next to drawing.
- Old readers: they ignore bit 0x2 and the trailing block, and draw only the
  non-Hangul glyphs.
