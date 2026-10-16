# Hi-res text: font maps, faces and glyph drawing

This directory holds the engine-independent half of hi-res text: the map
reader (`font_map.cpp`), the face formats (SVFN bitmap fonts, jamo-set fonts,
TrueType), glyph drawing and decoration. SCI, SCUMM and AGI (and, in part, AGS)
read a **map** that says which face draws which character. This file is the
format reference for the map and the faces; `engines/scumm/HIRES_TEXT_SETUP.md`
covers the ini keys, `tools/korean/README.md` the tools that build faces and
maps. A map without `[map] version=2` is refused. The older (version 1)
sections `[hires]`, `[latin]`, `[bitmap]`, `[encoding]`, `[sizes]` and
`[translation]` are not read any more; a map that still has one loads with one
warning that names it.

## Map file reference (version 2)

An INI file. `HiResFontMap::loadFromStream` takes the stream, the map's folder
and the qualifiers the engine passes (most specific first); it touches no engine
type, ConfigManager or FreeType, and it does not open any font file.

- Section and key names are case-insensitive. A line starting with `;` or `#` is
  a comment. A `;` after whitespace ends a value and starts a comment
  (`scale=3 ; why`); a `;` without whitespace before it is part of the value.
  `#` never starts an inline comment (it is the TTC face suffix).
- Numbers are decimal or `0x` hex. A code point is `u+XXXX` or `0xXXXX`. `u+` is
  a **value** form; as a key it works only in `range.`, `advance.` and `origin.`
  (`range.U+2026`), because `Common::INIFile` rejects other characters in key names.
  `[glyphs]` keys are plain `0x..`/decimal codes or ranges of them.
- Unknown sections and keys, bad values and removed keys are one warning each;
  the entry is skipped and the next level down applies. Keys an engine does not
  use (for instance `cell=` in SCUMM) are parsed, then warned about once.

| Section | Keys | Read by |
|---------|------|---------|
| `[map]` | `version=2` (required) | all |
| `[render]` | `target=auto\|clut8\|rgb565\|rgb888`, `blend=auto\|on\|off`, `scale=1..3`, `gamma=0.5..4` | SCI, SCUMM (AGS and AGI: blend, scale, gamma) |
| `[text]` | `encoding=` a code page (list below): the code page of the game's own strings | SCUMM |
| `[layout]` | `hangul=word\|any`, `kinsoku=on\|off`, `thai=on\|off` | all |
| `[fonts]` | `<name>=<path>`: the name table (below) | all |
| `[font]`, `[font.N]` | `face`, `size`, `pixel`, `shift`, `cell`, `align`, `missing`, `advance`, `advance.<spec>`, `origin`, `origin.<spec>`, `range.<spec>`, `mirror` | see the key table |
| `[load]` | `<name>=whole\|stream\|auto` | SCUMM, SCI, AGI |
| `[preload]` | `<name>=data:X.HOT` | SCUMM, SCI, AGI |
| `[glyphs]`, `[glyphs.N]` | game code -> `original`, `u+XXXX`, `+0xN`, `<face>:u+XXXX` | SCI, SCUMM, AGI |
| `[shadow]`, `[shadow.N]` | `mode`, `offset`, `fill`, `color`, `width`, `style`, `shadow`, `shadow_color`, `shadow_alpha` | SCUMM |

`N` is the engine's font id: the SCI font number, the SCUMM charset id. Any
section of this table except `[map]`, `[text]` and `[layout]` may carry
qualifiers; see "Qualifiers".

### `[fonts]`, paths and font values

`[fonts]` maps a name (`A-Za-z0-9_-`; not `same`, `original` or `data`) to a
file. A face file is recognised by content: the `SVFN` magic is a bitmap face
(a jamo set too), anything else opens as TrueType. A path is resolved by one
rule everywhere:

1. `data:<relative>` is searched in ScummVM's data roots (command-line,
   game and global `extrapath`, then `dists/engine-data/`, `DATA_PATH` and
   SearchMan's folders; on DOS `C:\SCUMMVM\DATA`). A `..` or an absolute path
   after `data:` is refused. Shipped game packs name their faces this way
   (`ui=data:IYAGI16.SVF`), so the faces live once in the engine data or in a
   font pack.
2. An absolute path is used as written.
3. A relative path is relative to the folder of the file that names it (the map).

A **font value** (`face=`, a `range.` value) is a comma-separated chain of
entries: a `[fonts]` name, a path, `same` or `original`. A TrueType path may end
in `#N` for face N of a collection (below). `original` ends the chain: the game's
own font draws. `same` in a `range.` value stands for the id's chain; in
`[font.N] face=` it means "inherit"; in `[font] face=` it is ignored with a warning.
The first face of a chain that has the glyph draws it. An SVF in a chain must have
the same cell height as the first SVF of that id's chain, or it is dropped with a
warning.

### `[font]` and `[font.N]`

`[font]` is the default for every id; `[font.N]` overrides it for id N. Each key
falls back from `[font.N:q]` to `[font.N]` to `[font:q]` to `[font]`.

| Key | Values | Meaning |
|-----|--------|---------|
| `face` | font value | the id's chain: first face with the glyph draws it |
| `size` | 8..64 | pixel size of a TrueType face at the hi-res scale; an SVF keeps its baked glyphs. With `cell=glyph` (SCI) it is also the layout cell |
| `pixel` | 1..64 | a pixel TrueType face held on its grid at a whole multiple |
| `shift` | -32..32 | SCI/AGI: draw glyphs this many hi-res pixels lower |
| `cell` | `game`, `glyph` | SCI: layout cell is the engine's 16 px (`game`) or `size=` (`glyph`). A face larger than 16 px needs `cell=glyph`, or its glyphs draw off their pens |
| `align` | `game`, `cell`, `font` | SCI/AGI: vertical placement of the face on the line. `game` puts the face's baseline on the game font's; `cell` centres the face's fitted box on the cell (the Korean SCI maps use it); `font` uses the face's own ascent, as FreeType lays a line out |
| `missing` | `u+25a1` or `off` | code point drawn when no face of the chain has a character (from the first chain face that has it) |
| `advance` | `game`, `font`, `cell` | id-wide advance rule: the game font's width, the face's own, or the cell (a wide cell, half for narrow) |
| `range.<spec>` | font value | the chain for a code range |
| `advance.<spec>`, `origin.<spec>` | as `advance`; `origin` is `game`\|`face` | per range; `origin=face` (SCUMM) uses the face's baked baseline instead of the game glyph's offsets |
| `mirror` | `off`, `horizontal`, `vertical`, `both` | SCUMM: draw the id's glyphs flipped |

`<spec>` is a block name (`basic-latin`, `latin-1`, `latin-ext-a`, `latin-ext-b`,
`thai`, `hangul-jamo`, `general-punctuation`, `letterlike`, `arrows`,
`geometric-shapes`, `cjk-symbols`, `hiragana`, `katakana`, `hangul-compat-jamo`,
`cjk-unified`, `hangul-syllables`, `pua`, `fullwidth-forms`, `box-drawing`,
`misc-symbols`), `U+XXXX-YYYY`, one `U+XXXX`, or `wide` (every wide character;
least specific). The narrowest matching range wins, and a rule in `[font.N]` beats
one in `[font]`. Two spellings of one span, or two overlapping spans of equal width
in one section, warn and the later line is ignored.
`range.basic-latin=same` with `advance.basic-latin=font`, as in the Korean maps,
draws ASCII from the same face as the Hangul, stepped by its own widths.
The SCUMM and AGI defaults are in `engines/scumm/HIRES_TEXT.md`.

`[text] encoding` names: cp932/sjis, cp936/gbk, cp949/uhc, cp950/big5, johab,
utf8; cp1250..cp1257, iso-8859-1/latin1, iso-8859-2, iso-8859-5, macroman,
maccentraleurope, cp850, cp862, cp866, ascii. ksc5601/euc-kr select the CP949
decoder (a compatible superset, not strict EUC-KR validation).

### Qualifiers

A section may take one engine qualifier and/or one render-target qualifier:
`[font.4:monkey2]`, `[fonts:clut8]`, `[font.4:monkey2:clut8]` (engine first, target
last). Targets are `clut8`, `rgb565`, `rgb888`. With engine qualifiers `e1, e2`
(most specific first) and the screen's target `t`, a key is looked up in
`[S:e1:t] [S:e2:t] ... [S:e1] [S:e2] ... [S:t] [S]`: the first that sets it wins;
`range./advance./origin.` rules merge span by span, `[glyphs]` code by code,
`[fonts]` name by name. A target never reads another target's sections, so write
the true-colour preset bare and the 8-bit preset as `:clut8`:

```ini
[fonts]
dlg=NGOTB21A.SVF       ; true colour (render_target=auto or rgb565/rgb888)
[fonts:clut8]
dlg=TBGOB24.SVF        ; 8-bit screen
[render:clut8]
blend=off
```

`[render:t] target=` is refused (the target is chosen first). `[text]` and
`[layout]` take no target qualifier. A bad qualifier ignores the section with a
warning.

### `[glyphs]`: exceptions per game code

A game reuses character codes for pictograms (MI2's skull at 0x07, DOTT's
arrows). `[glyphs]` (and `[glyphs.N]` for id N) says what each code draws:

```ini
[glyphs]
0x07=original        ; the game's own glyph
0x5e=u+2026          ; draw this code point (it then goes through the range rules)
0x21-0x7E=+0xFEE0    ; a range, offset form: ASCII to the fullwidth block
0x80-0x9F=icons:+0xE000   ; targeted: that face only, no chain
```

Keys are `0x5e`, decimal, or a range `<code>-<code>` (end at most `0xFFFF`). A
value is `original`, `u+XXXX`, `+<n>` (`0x..` or decimal; an offset, never `u+`),
or `<face>:u+XXXX` / `<face>:+<n>` where `<face>` is one `[fonts]` name or path (or
`same`); the value is split at its last colon. A single code beats a range that
covers it; between two ranges in a section the later wins; a qualified section beats
the bare one. A range takes only `original` or an offset (an absolute target would
draw many codes as one glyph). Ranges add at most 131072 codes per load. A
targeted face that cannot open or lacks the glyph falls back to the game's glyph,
with one warning.

### `[shadow]`, `[shadow.N]`

Outline or shadow for the replacement glyphs (SCUMM). `[shadow.N]` is charset N's
own section: its keys override `[shadow]` and fall back to it when unset
(`[shadow.N:q]` > `[shadow.N]` > `[shadow:q]` > `[shadow]`). Keys are listed under
"Decoration" below.

### Annotated examples

SCI, one map for every screen (`dists/engine-data/hires_text/dos/KQ1KO.MAP`,
comments shortened). Two 1 bpp jamo-set faces by font id; the same faces for every
render target, so no `:clut8` section is needed:

```ini
[map]
version=2
[render]
scale=2            ; 320x200 game drawn on a 640x400 text surface
blend=off          ; 1 bpp faces: hard-edged, nothing to blend
[fonts]
ui=IYAGI16.SVF         ; Iyagi 16 px jamo set (a pack's copy says data:IYAGI16.SVF)
body=MUNJO24.SVF       ; Mac Munjo 24 px jamo set (no ASCII)
[font]             ; every id without its own section
face=ui
missing=u+25a1     ; a character no face has draws a box
align=cell
range.basic-latin=original ; English in the game's own font
[font.300]         ; message boxes, narration
face=body
size=24
cell=glyph         ; layout cell is the face's 24 px, not the engine's 16
```

SCUMM, true colour and 8-bit in one map (`M2KO.MAP`, excerpt): a base face plus a
supplement per charset, streaming for the large title-card faces, a preload list,
and a per-charset shadow (the `[shadow.N]` lines are illustrative, the shipped map
sets no shadow):

```ini
[map]
version=2
[render]
scale=2
blend=auto
[render:clut8]
blend=off
[text]
encoding=cp949             ; the game's own strings
[fonts]
card=GBATB23A.SVF      ; base: ASCII + KS X 1001 (or a jamo set)
cardx=GBA23AX2.SVF     ; supplement: only what the text adds, same cell
[fonts:clut8]
card=TBMY24.SVF
cardx=TBM24X2.SVF
[load]
card=stream                ; read glyphs from the file as they are drawn
[load:clut8]
card=auto                  ; the 8-bit face is small: the size rule
[preload]
card=GBATB23A.HOT      ; characters read into the cache when the map loads (a path, or data:X.HOT)
[preload:clut8]
card=                      ; empty value: no list
[font]
missing=u+25a1
range.basic-latin=same
advance.basic-latin=font
origin.basic-latin=face    ; SCUMM: the face's baseline, not the game glyph's offsets
[font.4]
face=card,cardx            ; base first, supplement behind it
[glyphs]
0x07=original
0x5e=u+2026
[shadow]
mode=outline
[shadow.4]                 ; charset 4 only
mode=drop
offset=2
```

## Face formats and loading

### A face of a font collection (`<file>.ttc#<N>`, C21)

A TrueType collection (`.ttc`) holds several faces; `Graphics::loadTTFFont`
opens the first unless given `faceIndex`. Anywhere a map names a TrueType face -
`[fonts]` entries, `[font] face=` and `[font.N] face=` chains, SCUMM's legacy
`hires_text_font`, SCI's and AGS's faces - the path may end in `#<N>` to open
face N instead:

```ini
[fonts]
ko=/System/Library/Fonts/AppleSDGothicNeo.ttc#6      ; Bold (face 0 is Regular)
th=/System/Library/Fonts/Supplemental/SukhumvitSet.ttc#2   ; Text (face 0 is Thin)
```

The parser keeps the path as written (relative paths still resolve against
the map). The suffix is read when the font is opened, by `openFontFace()`
(`font_face.h`):

- A file by the **full** name (including `#<N>`) is opened as it is, at face 0,
  so a file whose name really contains `#` still works.
- Otherwise, when the text after the last `#` of the file name is one or more
  ASCII digits and the text before it is not empty, that file is opened at
  face N. `#`, `#-1`, `# 6`, `#6a`, `#0x6` and a `#` in a directory name are
  not face suffixes. The path then fails like any other missing file.
- Face N is passed to `TtfGlyphSource::create(..., faceIndex)`, so the
  vertical-fit probes, the Hangul check, the coverage check and every glyph
  use that face.
- A bad index - one the file does not have (`could not open face 99 of the
  font`) or one above 65535 (`face index out of range`) - is warned about and
  the face is skipped, exactly as for a missing file: the chain falls through
  to its next face, or the engine to its own fonts.
- A path without the suffix opens exactly as before, at face 0.

Grim is not covered: its font descriptors name files inside the game's
resources, not map paths.

Faces of the macOS collections this fork uses: AppleSDGothicNeo.ttc 0 Regular,
2 Medium, 4 SemiBold, 6 Bold; SukhumvitSet.ttc 0 Thin, 2 Text; the Hiragino
Sans W*.ttc files hold the weight in the file name and face 0 is the one
wanted. `fc-scan` or FreeType's `ftdump` list a collection's faces.

## Bitmap fonts (G2)

`HiResBitmapFont` reads the "SVFN" bitmap font format: 1bpp stencil, or 8bpp or
2bpp coverage, optional per-glyph metrics. The coverage forms carry anti-aliased
shapes baked by a tool, so a build without FreeType renders the same glyphs; that
makes it the primary format rather than a fallback.

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

### Whole or streamed: the `[load]` section

An SVF is either read into memory whole, or only its tables are (4 and 8 bytes a
glyph) and each glyph is read from the file as it is drawn (a cache of
`HIRES_SVF_CACHE_KB`, a per-line prefetch). By default the size decides: glyph
data up to the threshold is read whole (64 KB, `HIRES_SVF_STREAM_MIN`; the DOS
port raises it to 512 KB, `dos_svf_preload_kb`), anything larger streams. A map
can decide per face instead, because it knows which faces carry the dialogue
every second and which are drawn once, on a title card:

```ini
[fonts]
dlg=NGOTB21A.SVF
card=GBATB23A.SVF

[load]
dlg=whole        ; always in memory, however large
card=stream      ; always read from the file, however small
[load:clut8]
card=auto        ; the size rule again for the 8-bit set
```

The keys are `[fonts]` names (a face is not named by a path here), the values:

| value | the face |
|-------|----------|
| `whole` | is read into memory whatever its size; when the allocation fails it still falls back to streaming |
| `stream` | stays in its file whatever its size |
| `auto` | follows the size rule above; the default for every face the section does not name |

`[load:<qualifier>]` follows the rules of `[fonts]`: the bare section and then
each qualifier, least specific first, so a qualified entry replaces the bare one
for that name (`auto` included, which undoes a bare `whole`/`stream`). The
choice belongs to the file, not the name: two names that resolve to one file
share one loaded face, and if they disagree the first one in the section wins and
a warning says so. A name the map's `[fonts]` does not hold, or a value that is
not `whole`, `stream` or `auto`, is warned about once and means `auto`. A map
without `[load]` loads exactly as before. A jamo-set face (below) honours the
choice as well, as it wraps the same font. SCUMM, SCI and AGI read the section;
AGS loads every face whole and warns that it does not use it. A face reached by
a path in an ini key (`hires_text_face`) rather than a `[fonts]` name is always
`auto`.

### Preloading the glyphs a game draws: the `[preload]` section

A streamed face costs a disk read the first time each glyph is drawn. A map can
say which characters a face is expected to draw, so they are read once, when the
map is loaded, instead of one by one during play:

```ini
[fonts]
dlg=NGOTB16B.SVF
card=GBATB23.SVF

[preload]
dlg=NGOTB16B.HOT       ; characters to keep in memory for this face
card=data:GBATB23.HOT  ; a path next to the map, or data:
```

A `.HOT` file is UTF-8 text: the characters in any order with any line breaks;
a line whose first character is `#` is a comment; whitespace and a byte order
mark are skipped. `tools/korean/charsetchars.py --hot-dir` writes one for every
face of a map from the game's scripts and translation (union of the charsets
whose chain uses the face, plus ASCII); it can be edited by hand. Keys and
qualifiers follow `[load]`: keys are `[fonts]` names, `[preload:clut8]` overlays
the bare section (an empty value drops a list), a face has one list (two names on
one file: the first wins, with a warning), an unknown name is a warning, and a
list file that cannot be read is one warning and no preload. A map without
`[preload]` loads exactly as before.

What the engine does, when it opens a face that streams (`stream`, or `auto`
above the threshold): the face's glyph cache is sized to the list - the stored
bytes of the glyphs of the list that the face has, rounded up to
`HIRES_SVF_READ_BLOCK`, as that many cache entries, never fewer than 32 - and the
glyphs are read into it right away, sorted by their place in the file so that
glyphs in one read block cost one read. After that the cache is the usual LRU one:
a glyph outside the list pushes out the least recently used, which can be a listed
one (read again on its next use). A face held whole ignores the list. A jamo-set
face expands the list's syllables to the pieces the file holds and preloads those.
When the memory for the cache is not there, the face keeps the cache it has, reads
nothing and one warning says so. SCUMM, SCI and AGI read the section; AGS warns
that it does not use it.

The debug socket's `caches all` shows, per face, `list` (glyphs the list named that
the face has), `preloaded` (of those, read at open), `misses` (lookups that were not
hits) and `listevict` (listed glyphs pushed out since); `hits`, `reads` and `read`
count as before from the first draw on, the preload's own reads included in `reads`.

### Jamo-set fonts (flags bit 1)

A face holding every syllable precomposed is large (DOTT's 1bpp faces total
1,954 KB); a DOS Hangul system held positional jamo pieces and drew a syllable
as the OR of two or three. An SVFN v2 whose header flags carry bit 1 (0x2)
is such a font: its code point table is followed immediately by a **JSET**
block, and `createSvfnSource()` returns a `JamoGlyphSource` for it, so a map
names the file like any face (`ui=data:DOTTJ0.SVF`). An older reader ignores
the bit and the block and sees a plain font.

- The pieces are glyphs at code point U+F0000 + piece number (plane 15, private
  use). The numbers are the font's own; the JSET tables name them.
- Any other glyph (ASCII, symbols, compatibility jamo stored whole) is looked
  up as in any SVFN.
- The block ends where the next table the header names begins (metrics table
  or glyph data), or at the end of the file; its length must equal what its
  counts give. A block that fails validation (counts, classes, sets out of
  range, a length that does not add up, a piece number with no glyph) makes
  the font load as a plain SVFN with one warning naming the file.
- U+AC00..U+D7A3 are composed from the pieces by the maximum coverage at each
  pixel (the OR of a 1bpp stencil; 2bpp and 8bpp work too); U+3131..U+3163 with
  a compatPiece are that piece alone. A syllable is two cells wide.

JSET block, little-endian:

```
4  "JSET"
2  version (1)
1  LC  lead classes   (1..12)
1  TC  tail classes   (1..12)
1  NI  initial sets   (1..24)
1  NM  medial sets    (1..16)
1  NF  final sets     (1..16)
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

The three set tables are indexed `(leadClass * 21 + vowel) * TC + tailClass`
(vowel 0..20 in Unicode order, ㅏ..ㅣ); a piece table is indexed
`set * 19 + initial`, `set * 21 + vowel`, `set * 27 + tail - 1`. A final set
is validated and read only for tail classes some tail 1..27 belongs to. The
DOS set systems (8x4x4, 10x4x4, hanterm's 10x(3|4)x4, 6x2x1, the Dokkaebi
printer sets, Kyra's jamo-major order, and the Mac HangulTalk 1.0 'fbit' fonts with their 8 lead classes and
20 initial sets) are all expressible; `tools/korean/jamoset.py`
writes such fonts, also at 2 bpp from an outline font (`--bpp 2`, anti-aliased
pieces composed by the maximum) and with set systems fitted to the font
(`--layout adaptive`, up to the limits above).

The class limits were 8 and 8 until 2026-10 (largest block 6641 bytes); they
are 12 and 12 now (11681 bytes), with the version still 1 since the layout did
not change. An engine built before that rejects a block with more than 8
classes of either kind (the font then loads as a plain SVFN, its syllables
from the next source), so faces with 9..12 classes need a current engine;
every older face loads unchanged.

### Per-glyph dy (header bytes 17 and 18, metrics byte 3)

The SVFN header, little-endian (version 2 adds bytes 32..35):

```
0  "SVFN"      4  u16 version (1, 2)   6  u16 flags (bit 0 metrics table, bit 1 JSET, bit 2 marks at origin)
8  bpp (1, 2, 8)   9  shadow   10 u16 code page   12 u16 glyphs   14 cell width   15 cell height
16 ascent (baseline row)   17 dy_min (int8, <= 0)   18 dy_max (int8, >= 0)   19 unused (0)
20 u32 metrics offset   24 u32 glyph data offset   28 u32 glyph data size   32 u32 code point table offset (v2)
```

A metrics entry is 4 bytes: advance, left bearing (int8), ink width, and **dy**
(int8): the glyph is drawn `dy` rows below its cell position (negative: above),
`dy_min <= dy <= dy_max`. The glyph's rows are stored in the cell as always; dy
only moves where they are drawn, and ink that then lies outside the cell is
drawn, not clipped. A Latin face whose letters must stand on a Hangul baseline
near the bottom of the cell stores them high enough for their descenders and
draws them lower by dy (`tools/korean/svfdy.py` sets it; `jamoset.py` reads and
keeps it).

- Every file written before this had a meaning holds 0 in all of these bytes,
  and draws exactly as before. An engine that predates it ignores them: every
  glyph is drawn at its cell position (a face that uses dy should say so where
  it ships).
- Only a font with a metrics table has a dy per glyph. A range given without
  one, a range with `dy_min > 0` or `dy_max < 0`, or one reaching more than the
  cell height above or below the cell is ignored with a warning (dy 0 for
  every glyph). A glyph whose dy lies outside the range is drawn at dy 0; one
  warning at load counts them.
- `GlyphMetrics::dy` is the glyph's dy, `bearingY` (baseline to the top of the
  stored glyph as drawn) is `ascent - dy`. The cell keeps its meaning:
  `baselineRow()`, the face chain layout, line heights and the cell-height
  checks of a map all use the cell, not the range.
- Buffer rule: `UnicodeGlyphSource::inkRowsAbove()` / `inkRowsBelow()` are
  `-dy_min` / `dy_max` (0 for every other source; a chain takes the largest of
  its faces, a `NormalizedGlyphSource` what leaves its own cell). `row(cp, y)`
  answers `y` from `-inkRowsAbove()` to `cellHeight() + inkRowsBelow() - 1`,
  the glyph already moved by its dy; a drawer sizes its glyph buffer once per
  face as `inkRowsAbove() + cellHeight() + inkRowsBelow()` rows and draws it
  `inkRowsAbove()` rows above the cell top (a null row outside the cell is
  blank). SCUMM (a vertical mirror turns the glyph within its cell), SCI, AGI
  (within its 16-row box), AGS and `HiResGlyphRenderer::drawGlyph(font)` do;
  SCI's placement path still clips to the text line, and Grim draws the cell
  rows only (the glyph moved, clipped to the cell).
- A jamo-set face composes its syllables at dy 0; a piece must not carry a dy
  (`jamoset.py check` refuses one). Its other glyphs (Latin) may.

## Glyph rendering (G3)

`HiResGlyphRenderer::drawGlyph` draws one glyph of a `HiResBitmapFont` onto a
CLUT8 surface. A paletted surface cannot hold coverage, so the colour goes to
the text surface and the coverage to a parallel 8bpp one that the caller blends
against the background. The coverage surface is optional: without it an 8bpp
or 2bpp font still draws as a stencil, so a backend with no alpha path is not left
blank. That stencil keeps the pixels covered at least a quarter
(`kKeyedInkThreshold`, 0x40): any coverage turns the stroke fringe into ink and
closes small glyphs into blobs, while half coverage drops thin CJK strokes. A
1bpp font writes no coverage at all - it has none to record.

### Decoration (C19)

A decoration is built from the glyph's **coverage**, whatever the source:
TrueType, 8bpp or 2bpp SVFN, a baked face or a 1bpp stencil (read as 0/255). Nothing
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
one game pixel (scale). Map keys, all in `[shadow]` (and in each
`[shadow.N]`, a SCUMM charset's own section, whose keys override these for that
charset and fall back to them when unset; `[shadow.N:q]` > `[shadow.N]` >
`[shadow:q]` > `[shadow]`):

```ini
mode=outline        ; none | drop | outline | stroke | game (unchanged)
color=0             ; decoration colour (unchanged)
offset=1            ; shadow distance, and the width when width= is absent
                    ; offset=dx,dy: a drop's / stroke copy's vector, both legs
fill=off            ; on: a drop swept from the glyph to its offset
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

### Coverage gamma (`[render] gamma=`, C20)

A thin face blended over a dark outline reads grey: a stem that covers half a
pixel is blended at half strength in gamma space, where linear light would
show it at about three quarters. `[render] gamma=` is an opt-in curve applied to
every TrueType glyph's coverage as `TtfGlyphSource` rasterises it:

```ini
[render]
gamma=1.8   ; 0.5..4, to the nearest hundredth; 1 (the default) is off
```

`c' = 255 * (c/255)^(1/gamma)`. Above 1 the partial pixels get stronger and
stems read heavier; below 1 they get weaker. Above 1, coverage under 4 is left
alone, so faint fringe (haze between a stacked mark and its base, or inside a
tight counter) is not lifted into view. 0 stays 0 and 255 stays 255, so a
glyph's box, advance, fit and origin are unchanged. At 2.2 light text over a
black outline looks as linear-light blending would draw it.

What changes on screen depends on the path:
- Blended (alpha): no pixel is added, and a Thai mark does not grow into its
  base. The outline does widen with the body, because C19's `dilate()` reads
  coverage as distance: about +0.23 px at 2.2, up to +0.47 px at 4. That is
  intended; the outline follows the heavier edge.
- Keyed (`blend=off`): the body is cut at 0x40 and the decoration at 0x80,
  so a pixel whose coverage now crosses a cut becomes a whole solid pixel.
  Keyed text and its outline grow; below 1, thin keyed strokes can drop out.

The code changes only the coverage bytes: sinks and SVFN bitmap fonts (baked
or shipped) are untouched, and a map without the key leaves every byte as
FreeType drew it. The key applies to the whole face chain. SCUMM, SCI and AGS
pass the map's value to each TrueType source they open
(`TtfGlyphSource::setCoverageGamma`, which drops glyphs already cached);
Grim does not use `TtfGlyphSource` and ignores it.
`TtfGlyphSource::buildGammaCurve` is the pure curve. A heavier face of the
same family is usually the better first choice (it keeps crisp stems where the
curve widens soft ones); gamma is for a script whose only face is light.

### How a TrueType face is sized: line fit, `size=`, `pixel=` (C28)

A face is opened in one of three ways, and each decides both the ppem and
the **layout cell** (`cellHeight()`, the box the engine lays text out on):

| Map | ppem | Cell | Probe fit |
|---|---|---|---|
| no `size=`, no `pixel=` (SCUMM only: the line fit) | `round(upm * cell / (usWinAscent + usWinDescent))` (`kTTFSizeModeCell`) | the game's own cell times the scale | none (only a translation sample that leaves the cell moves or shrinks it) |
| `size=N` (SCUMM `[font.N]`/`[font]`, SCI, AGS - AGS uses the game font height when no size is given) | N, stepped down until the fixed probe set (Hangul, `A g j y Å`, brackets, CJK quotes) plus the translation sample fits N rows | **N** | yes |
| `pixel=D` | `D * k`, the largest multiple of D the cell holds (D itself in a smaller cell) | the engine's cell, unchanged: SCUMM the game cell times the scale, or `size=` when set; SCI `size=` (16 by default); AGS `size=` times the game's size multiplier, else the game font's height | none |

**`size=` also changes the layout cell.** In SCUMM a charset with
`[font.N] size=` is laid out on an N-pixel cell instead of its own:
Monkey Island 2 at 2x, charset 2 (game cell 9, so 18) with `size=16` is laid
out on 16 - the log shows `cell=16x16` - and its lines sit two pixels closer
than the game's grid (C25). Harmless where the game only stacks lines, but a
map that must keep the game's line pitch should not use `size=` for it; with
`pixel=` alone the cell stays the game's.

**Pixel fonts** (Galmuri, Neo둥근모, HBIOS-SYS, IyagiGGC, Unifont, LanaPixel)
are drawn on a grid of `upm / D` font units: crisp only at D ppem or a whole
multiple, and broken into grey steps at any other size (C25 measured 0 grey
pixels at the design size, 1178-1713 off it). The line fit lands on the grid
only when the cell happens to match (Galmuri9 in a 12 cell: 10), and `size=`
shrinks a face whose Latin and brackets pass the em (every Galmuri; LanaPixel
goes blank off its 11 ppem strike). `pixel=` names the design size:

```ini
[font.2]              ; MI2 at 2x: the 18-pixel cell holds Galmuri11 at 12
face=galmuri11
pixel=12
[font.1]              ; a 9-pixel cell: Galmuri7 at 8
face=galmuri7
pixel=8
```

`TtfGlyphSource::createPixel()` opens the chain's first face at
`pixelGridSize(cell, D)` in character mode, with no probe fit and no
translation-sample fit, and draws glyphs from the face's line top by whole
pixels (FreeType's glyph origins and advances are whole pixels at a grid
ppem, so no pen position is fractional). When the face's line (ascent +
descent + gap) is taller than the cell, the ink of the Hangul probes and
`A g j y` is moved into the cell by whole rows, or its top put at row 0 when
the ink itself is taller; the ppem never changes, and ink outside the cell is
clipped. The faces behind it in a chain (`face=ko, ja, th`) are fallbacks
for what it lacks and are opened as usual (line fit, or `size=` with its
probe fit, and the translation sample's fit), in the same cell.
`[font] pixel=` sets it for every font; `[font.N] pixel=` wins. It
describes a face the map names: a face from the ini (`hires_text_font`) is
never opened as a pixel face, and when SCI falls back from a `[font.N]`
face to the face every font gets, that face takes `[font] pixel=`, not the
`[font.N]` one.
SCUMM, SCI and AGS read it; Grim does not use `TtfGlyphSource`. AGS text
drawn N times larger (C23's N x path) opens the pixel face at exactly N
times the ppem it has at 1x (`AGS3::scaledPlan()`), not at
`pixelGridSize(N * cell, D)`, so its glyphs line up with the N x pens (cell
15, D 10 at 2x: 20, not 30; a 9 cell with D 10: 20, not 10). A Latin
companion face (`latin_font=`) is not a pixel face. Design sizes: Galmuri7 8,
Galmuri9 10, Galmuri11 12, Galmuri14 15; Neo둥근모, HBIOS-SYS, IyagiGGC,
Unifont 16; LanaPixel 11 (its only strike: at 22 it is blank).

Pixel grids are not detected automatically. A grid shows as the GCD of every
outline coordinate (Galmuri9: 100 units of 1000) or as an embedded bitmap
strike, but `Graphics::Font` exposes neither, reading every glyph's outline at
load time would cost what the probe budget forbids, and a false positive
would move ordinary faces. The key is explicit; `runs/c25/coverage.py` in the
harness reports a face's grid.

A pixel font baked to SVFN with `tools/korean/mkfont.py --unicode` (version 2,
every code point it draws) is the same bitmap for a build without FreeType.

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
