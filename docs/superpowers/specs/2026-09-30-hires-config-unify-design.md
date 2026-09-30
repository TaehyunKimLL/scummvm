# Hi-res text configuration, unified: design

Date: 2026-09-30. Branch `dos-port`, worktree `~/work/scummvm/dos`.
Source: the audit `.superpowers/sdd/2026-09-29-dos-m5-scumm/config-audit.md` (inventory, problems, file list).
Plan: `docs/superpowers/plans/2026-09-30-hires-config-unify.md`.

This document is the single reference for every hi-res text key after the change: map file (`HIRESTXT.MAP`, the DOS `*.MAP`
files) and ini (`scummvm.ini` / `SCUMMVM.INI`), for SCI, SCUMM, AGS and the MS-DOS backend. Anything not listed here is not
read. Where this document and an engine's own doc disagree, this document wins and the engine doc is corrected.

## 0. Binding rulings (user, 2026-09-30)

1. **No backward compatibility.** Old section names, old key names, old maps and old ini keys stop working. Every map, ini,
   generator and test is rewritten to this scheme and the old parsing code is deleted. Upstream-owned options (SCI's
   `rgb_rendering`, `palette_mods`, `render_mode`, `disable_dithering`; `text_encoding`, `extrapath`, `path`, `language`)
   keep their upstream behaviour, but none of them controls hi-res text any more.
2. **Render target** is one key: ini `render_target = auto | clut8 | rgb565 | rgb888`, map default `[render] target`. It
   replaces `dos_truecolor` and SCUMM's use of `alpha=` to choose the screen format. **Blending** is a separate key: map
   `[render] blend = auto | on | off`, ini `hires_text_blend`.
3. **Faces by Unicode range**: `range.<block-name | U+XXXX[-YYYY]> = <font value>` inside `[font]` / `[font.N]`; the
   narrowest range wins. Font value = path | `[fonts]` name | `same` | `original`. Latin is one range like any other; the
   four old Latin modes become recipes (section 6.6).
4. **Scale** is one key: map `[render] scale`, ini `hires_text_scale`. SCI supports 2x only (warn otherwise). The DOS backend
   supports 2x only (warn, run at 2x). SCUMM and AGS keep 1..3 on other platforms.
5. **RGB565 output** and **palette-matched anti-aliasing on CLUT8** are defined here (sections 7.2, 7.3); their
   implementation may land as separate later tasks.
6. **Addendum (user, 2026-09-30): a game code can name a code point in a specific font** (`[glyphs]` target form,
   section 6.7), including Private Use Area code points in a custom SVF.

## 1. Vocabulary

| Term | Meaning |
|---|---|
| **id** | the engine's font number: SCI font resource id, SCUMM charset 0..19, AGS font number. `[font.N]` addresses it. |
| **game code** | the character code the engine hands the hi-res layer for one glyph, after the engine has parsed its own control codes. SCI: the decoded Unicode code point (UTF-8 translation or legacy code page). SCUMM: the charset code of a single-byte character, or the decoded code point of a double-byte one. AGS: the decoded code point. |
| **code point** | the Unicode scalar that is drawn. Starts as the decoded game code; `[glyphs]` can change it. |
| **face** | one opened font file: an SVFN bitmap font (`*.SVF`, sniffed by the `SVFN` magic) or a TrueType/TTC face (with optional `#N`). |
| **chain** | an ordered list of faces; the first face that has a glyph for the code point draws it (coverage fallback). |
| **id chain** | the chain the id's resolved `face` key gives (section 5.3). |
| **qualifier** | the engine's section suffix: SCI the platform code; SCUMM the gameid, then `v<N>`; AGS the gameid. |
| **engine scope** | the defaults an engine supplies below `[font]` (section 8). |

## 2. Principles

1. One name per concept, the same name in every scope. Map-wide defaults live in `[font]`, per-id overrides in `[font.N]`,
   with the same keys in both.
2. One value grammar for every face, in the map and in the ini: the **font value** (section 5).
3. One precedence rule: **ini > `[S.N:q]` > `[S.N]` > `[S:q]` > `[S]` > engine scope**, key by key. No sentinel inverts it.
4. One path rule (section 4).
5. Blending and screen format are separate knobs.
6. Strict parsing: every section warns about unknown keys; every engine declares the keys it honours and the loader warns
   once for a key the engine cannot honour.
7. The map carries its version: `[map] version=2`; anything else is refused.

## 3. The map file

### 3.1 Syntax

- INI text. The loader calls `Common::INIFile::allowNonEnglishCharacters()` so that `+` is legal in key names
  (`range.U+2026`); `[`, `]`, `=`, `#`, CR and LF stay illegal in names. Key and section names are case-insensitive.
- A line starting with `;` or `#` is a comment. A `;` preceded by a space or a tab ends a value and starts a comment; a `;`
  with anything else before it is part of the value. `#` never starts an inline comment (it is the TTC face suffix).
- Sections may carry one qualifier after a colon: `[font.4:pc]`, `[render:monkey2]`, `[glyphs.2:v5]`. The engine passes its
  qualifiers most specific first; each key is looked up in `[S:q1]`, `[S:q2]`, ..., then `[S]`.
- Numbers: decimal, or hex with `0x`. Code points: `u+XXXX` / `U+XXXX` (1-6 hex digits), or `0xXXXX`.

### 3.2 Sections and keys

Legend for "Read by": **SCI**, **SCUMM**, **AGS**; `-` = the loader warns once when the map sets it for that engine
(section 10.3). Precedence is principle 3 unless the row says otherwise.

#### `[map]`

| Key | Values | Default | Read by | Notes |
|---|---|---|---|---|
| `version` | `2` | none: **required** | all | Any other value, or a map without it, is refused (section 10.1). |

#### `[render]` (and `[render:q]`)

| Key | Values | Default | Read by | Precedence |
|---|---|---|---|---|
| `target` | `auto` `clut8` `rgb565` `rgb888` | `auto` | SCI, SCUMM; AGS `-` | ini `render_target` (game domain) > ini `render_target` (`[scummvm]`) > `[render:q]` > `[render]` > `auto`. Section 7.1. |
| `blend` | `auto` `on` `off` | `auto` | SCI, SCUMM, AGS | ini `hires_text_blend` > map > `auto`. Section 7.2. |
| `scale` | `1` `2` `3` | SCI 2, SCUMM 2, AGS 1 | SCI (2 only), SCUMM, AGS | ini `hires_text_scale` > map > default, then the limits of section 7.4. |
| `gamma` | `0.5`..`4.0` | `1.0` | SCI, SCUMM, AGS | TrueType coverage curve `255*(c/255)^(1/g)`; SVF faces ignore it. |

#### `[text]`

| Key | Values | Default | Read by | Notes |
|---|---|---|---|---|
| `encoding` | `cp932` `cp936` `cp949` `cp950` `utf8` `johab` `ascii` (the names `HiResFontMap::parseCodePage()` knows) | from the game language | SCUMM; SCI `-`; AGS `-` | Code page of the game's own strings. SCUMM: map beats the language default; the ini `text_encoding` keeps its upstream meaning (SCUMM `.trs` only) and is not changed by this design. SCI keeps reading only the ini `text_encoding`. |

#### `[layout]`

| Key | Values | Default | Read by |
|---|---|---|---|
| `hangul` | `word` `any` | engine rule | SCI, SCUMM, AGS |
| `kinsoku` | `on` `off` | engine rule | SCI, SCUMM, AGS |
| `thai` | `on` `off` | engine rule | SCI, SCUMM, AGS |

#### `[fonts]` - the name table

`<name> = <path>`. A name is `[A-Za-z0-9_-]+`, case-insensitive, and may not be `same`, `original` or `data`. The path
follows section 4. A qualified `[fonts:q]` entry refines the bare one of the same name. The table has no roles: no name is
special (`default`, `bold`, `title` are ordinary names).

#### `[font]` and `[font.N]` (and their `:q` forms)

The same keys in both. `[font]` is the map-wide default; `[font.N]` overrides it for id N.

| Key | Values | Default (engine scope) | Read by | Notes |
|---|---|---|---|---|
| `face` | font value (section 5) | none | SCI, SCUMM, AGS | The id chain. ini `hires_text_face` beats every level. `same` in `[font.N]` = inherit (as if absent); `same` in `[font]` = warning, ignored. `original` = section 6.5. |
| `size` | `8`..`64` (px at the hi-res scale) | SCI 16; SCUMM the charset's cell; AGS its plan | SCI, SCUMM, AGS | ini `hires_text_size` beats every level. TrueType faces are rasterised at it; an SVF keeps its baked glyphs. On SCI it is also the layout cell when `cell=glyph`. |
| `pixel` | `1`..`64` ppem | 0 (off) | SCI, SCUMM, AGS | A pixel TrueType face held on its grid: opened at the largest whole multiple of `pixel` that fits the cell. Only the first face of the id chain. |
| `shift` | `-32`..`32` px | 0 | SCI; SCUMM `-`; AGS `-` | Glyphs drawn this many hi-res px lower (negative: higher); layout unchanged. |
| `cell` | `game` `glyph` | `game` | SCI; others `-` | `game`: the engine's 16 px cell; `glyph`: the cell is `size`. |
| `align` | `game` `cell` `font` | `game` | SCI; others `-` | Vertical placement of the face on the line (unchanged meaning). |
| `missing` | code point, or `off` | `off` | SCI, SCUMM; AGS `-` | Section 6.4. `[font.N] missing=off` cancels a map-wide one. |
| `advance` | `game` `font` `cell` | engine scope | SCI, SCUMM; AGS `-` | Id-wide advance rule. ini `hires_text_advance` beats every advance key. Section 6.3. |
| `advance.<spec>` | `game` `font` `cell` | engine scope | SCI, SCUMM; AGS `-` | Advance for a range (section 6.1). |
| `origin` | `game` `face` | `game` | SCUMM; SCI `-`; AGS `-` | Vertical origin: the game glyph's offsets, or the face's baked baseline. |
| `origin.<spec>` | `game` `face` | engine scope | SCUMM; SCI `-`; AGS `-` | Origin for a range. |
| `range.<spec>` | font value | engine scope | SCI, SCUMM; AGS `-` | Section 6. |
| `mirror` | `off` `horizontal` `vertical` `both` | the game table (SCUMM) | SCUMM; others `-` | Draw the id's glyphs flipped. |

#### `[glyphs]` and `[glyphs.N]` (and their `:q` forms)

Per-game-code exceptions. `[glyphs.N]` refines `[glyphs]` for id N (for SCUMM, N is the charset: the old `[glyphs:csN]`
becomes `[glyphs.N]`). Read by SCI and SCUMM; AGS `-`. Grammar and semantics: section 6.7.

#### `[shadow]` (and `[shadow:q]`)

Unchanged keys and values: `mode = game|none|drop|outline|stroke`, `offset`, `color`, `width`, `style = round|square|legacy`,
`shadow = dx,dy`, `shadow_color`, `shadow_alpha`. Read by SCUMM; SCI `-`; AGS `-`. One change: an unknown `mode` is now a
warning and the key is ignored (it used to mean `game` silently).

### 3.3 Removed outright

Sections `[hires]`, `[latin]`, `[bitmap]`, `[encoding]`, `[sizes]`, `[translation]`, and `[map] height_N`. Keys `font=`
(as an alias of `face=`), `bitmap=`, `latin*=`, `metrics=`, `baseline=`, `alpha=`, `enabled=`, `[glyphs] ... = keep` (now
`original`), `[glyphs:csN]` (now `[glyphs.N]`), and `[render] mode/metrics`. No alias is read. A v2 map that contains one of
the removed sections gets one warning per section naming its replacement (section 10.2) and is otherwise loaded.

## 4. Paths (one base rule)

A value that names a file is resolved by exactly one rule, in every engine and for every key:

1. `data:<relative>` - searched in ScummVM's data roots (`HiResFontMap::dataRoots()`: command-line `extrapath`, the game's
   `extrapath`, the global `extrapath`, the in-tree `dists/engine-data/`, `DATA_PATH`, then SearchMan's folders). A
   `..` component or an absolute path after `data:` is refused.
2. An absolute path is used as written.
3. **A relative path is resolved against the folder of the file that names it.** For a map key that is the map's own
   folder. For an ini key the "containing folder" is defined as the game folder (`path=` of the game domain), because the
   location of `scummvm.ini` means nothing to a game.

Consequences: the current directory is never a base; `[bitmap]`-style game-folder paths inside a map are gone (a map that
wants a font in the game folder writes the path relative to itself, or the map sits in the game folder); SCI, SCUMM and
AGS resolve `hires_text_map` and `hires_text_face` identically. `hires_text_map` with no value set means
`<game folder>/HIRESTXT.MAP` when that file exists; an empty value (`hires_text_map=`) means "no map". The default file name is 8.3 (DOS), matched case-insensitively on every platform.

## 5. Font value

### 5.1 Grammar

```
font-value := entry ("," entry)*
entry      := <[fonts] name> | <path> | data:<path> | same | original
```

Whitespace around entries is ignored. An entry is classified in this order: `same`; `original`; a `[fonts]` name
(case-insensitive); a path (it contains `/`, `\`, `.` or starts with `data:`). Anything else is dropped with one warning
("unknown face name 'x'"). A path entry may end in `#N` to pick face N of a TTC.

### 5.2 Entries

- **A file** is sniffed by content in every engine: the `SVFN` magic means an SVFN bitmap font, anything else is opened as
  TrueType. There is no separate key for bitmap fonts.
- **`original`**: the game's own font draws it. It ends the chain: entries after it are dropped with a warning.
- **`same`**: "the chain of the enclosing scope". In a `range.*` value, and in a `[glyphs]` target, it stands for the id
  chain at that position. In `[font.N] face` it means "inherit from `[font]`" (identical to leaving the key out). In
  `[font] face` and in the ini `hires_text_face` it is a warning and the key is ignored.

### 5.3 The id chain

The id chain of id N is the first of: ini `hires_text_face`; `[font.N:q]`/`[font.N] face`; `[font:q]`/`[font] face`; empty.
An empty id chain means "no replacement face": SCUMM then borrows the nearest charset's faces (`nearestFont()`), SCI falls
back to its `.uni` bundle, AGS keeps the game's font - each engine's existing fallback, which is behaviour, not a key.

### 5.4 Opening rules

- A TrueType face is opened at the id's resolved `size` (and `pixel` for the first face of the id chain); the same
  file at the same size is opened once and shared.
- An SVF face on an id must have the same cell height as the first SVF of that id's chain; a face that differs is refused
  at load with one warning and dropped from every chain that names it. (SCUMM keys its sources by cell height; SCI pads rows
  per chain. Bakes that share an id share `--cell`, `--ascent` and `--clip-cell`.)
- A face that fails to open is warned about once per path and dropped from every chain.

## 6. Unicode ranges

### 6.1 Range spec

`<spec>` in `range.<spec>`, `advance.<spec>` and `origin.<spec>` is one of:

- **A block name** (case-insensitive) from the fixed table in `graphics/hires_text/unicode_ranges.cpp`:

  | Name | Range | Name | Range |
  |---|---|---|---|
  | `basic-latin` | U+0020-007E | `cjk-symbols` | U+3000-303F |
  | `latin-1` | U+00A0-00FF | `hiragana` | U+3040-309F |
  | `latin-ext-a` | U+0100-017F | `katakana` | U+30A0-30FF |
  | `latin-ext-b` | U+0180-024F | `hangul-compat-jamo` | U+3130-318F |
  | `thai` | U+0E00-0E7F | `cjk-unified` | U+4E00-9FFF |
  | `hangul-jamo` | U+1100-11FF | `hangul-syllables` | U+AC00-D7A3 |
  | `general-punctuation` | U+2000-206F | `pua` | U+E000-F8FF |
  | `letterlike` | U+2100-214F | `fullwidth-forms` | U+FF00-FFEF |
  | `arrows` | U+2190-21FF | `box-drawing` | U+2500-257F |
  | `geometric-shapes` | U+25A0-25FF | `misc-symbols` | U+2600-26FF |

  `basic-latin` is the printable range only; U+007F and C0 controls are in no block.
- **An explicit range** `U+XXXX-YYYY` (or `U+XXXX-U+YYYY`), `XXXX <= YYYY`, or **one code point** `U+XXXX`.
- **The class `wide`**: every code point `Graphics::isWide()` answers true for. It is the least specific spec: inside one
  scope it matches only when no block or explicit range of that scope does.

An unknown block name or a malformed range is a warning; the key is ignored.

### 6.2 Which rule applies

For id N and code point `cp`, each of the three rule families (`range.`, `advance.`, `origin.`) is resolved on its own:

1. The scopes are searched in order: `[font.N]` (with `[font.N:q]` merged in, section 6.2.1), then `[font]` (with
   `[font:q]`), then the engine scope. The first scope that has any matching spec answers.
2. Inside a scope, the **narrowest** matching span wins (the span of `U+XXXX` is 1; a block is its table span); `wide` is
   used only when no span matches. A per-id rule therefore beats a map-wide rule of any width.
3. For `advance.`/`origin.`, when no spec matches in any scope, the id-wide `advance`/`origin` key (resolved by principle 3)
   applies, then the engine default. The ini `hires_text_advance` overrides all of this.

#### 6.2.1 Merging, duplicates, overlaps

- Rules are identified by their span, not by spelling: `range.basic-latin` and `range.U+0020-007E` are the same rule.
- Qualified and bare sections merge span by span: `[font.N:q]`'s rule for a span replaces `[font.N]`'s rule for the same
  span; other spans from both remain.
- Two spellings of one span in the same section: warning; the later line in the file is ignored.
- Two different spans of equal width that overlap in the same section: warning; the later line is ignored. Different widths
  may overlap freely (the narrower wins where they overlap).

#### 6.2.2 Compiled form (implementation contract)

At load, each id's three families are compiled with its scopes into one sorted vector of non-overlapping runs
`{lo, hi, faceRule, advance, origin}` (`-1` = no rule), split at every boundary, so no precedence remains at run time. A
256-entry page table indexed by `cp >> 8` (the BMP; code points above U+FFFF always search) holds, per page, "no run touches this page", or the single run that covers the
whole page, or "search"; a search is a binary search over the runs (n < 64). `wide` is resolved per code point after the
table says "no span rule". This replaces the fixed `cp >= 0x20 && cp <= 0x7E` tests in both engines.

### 6.3 Advance values

| Value | Meaning |
|---|---|
| `game` | the game font's width for the game code (for SCI and SCUMM exactly the old `metrics=game`: scaled game width, glyph fitted to it). Where the game font has no glyph for the code, `cell`. |
| `font` | the drawing face's own advance for the code point (old `metrics=font`). |
| `cell` | the id's cell advance: the wide cell for a code point `isWide()` answers true for, half of it otherwise (old `latin=half`). |

### 6.4 Missing glyphs

When no face of the chain (section 6.5, step 4) has the code point and the chain did not end in `original`: if the id's
resolved `missing` is set, the missing code point (for example `u+25a1`, a box) is drawn from the first face of that same
chain that has it, with the advance of the missing code point's rules; if no face has the box, or `missing` is `off`, the
game's font draws the game code (which for a character the game font lacks draws nothing - today's behaviour). This applies
to **every** code point in SCI and SCUMM (SCUMM used to apply it only to ASCII under `font=same`). Each code point that falls
to `missing` is logged once (`hires_text_log`).

### 6.5 Composition, per character

1. The engine parses its control codes and hands the layer the game code `c` for id N. `[glyphs]` never sees control codes
   (SCI: `GfxText16::readChar()` still returns the raw character; the rules apply in `glyphChar()`).
2. **`[glyphs]`** (section 6.7): `[glyphs.N:q]`, `[glyphs.N]`, `[glyphs:q]`, `[glyphs]`, first match:
   - `original` - the game's font draws `c`. Stop.
   - a code point or offset - `cp` becomes that code point; continue at step 3 **with the new cp** (its range, advance and
     origin rules apply; this is a deliberate change from SCUMM's old exclusion of remapped codes from the Latin rules).
   - a target `<face>:<cp>` - draw exactly that glyph from that face (section 6.7). Stop.
   - no entry: `cp` = the decoded code point; continue.
3. **Id off**: with the ini `hires_text_face=original`, the game's font draws `c` for every id; stop. If id N's resolved
   `face` is `original` (from `[font.N]` or `[font]`), only the range rules written in `[font.N]` itself apply (neither
   `[font]`'s nor the engine scope's); with no such rule the game's font draws `c`; stop.
4. **Chain**: the face rule of `cp` (section 6.2). With no rule, the chain is the id chain. With a rule, the chain is the
   rule's value with each `same` replaced by the id chain; if the value contains no `same` and does not end in `original`,
   the id chain is appended after it (so a Latin face falls back to the id's faces by coverage). `original` anywhere ends the
   chain there.
5. The first face in the chain with a glyph for `cp` draws it. SCUMM then tries its nearest-charset borrowing.
6. If none has it: section 6.4.
7. Advance and origin: sections 6.2 and 6.3, looked up for the drawn code point (for the missing box: the box's code point).

### 6.6 The old Latin modes as recipes

| Old | New |
|---|---|
| `[latin] mode=off` | `range.basic-latin = original` |
| `[latin] mode=half` | `range.basic-latin = same` (or a face) + `advance.basic-latin = cell` |
| `[latin] mode=proportional` + `metrics=game|font` | `range.basic-latin = same` (or a face) + `advance.basic-latin = game|font` |
| `[latin] mode=fullwidth` | `[glyphs] 0x21-0x7E = +0xFEE0` (and `0x20 = u+3000` for the old `space=fullwidth`) |
| `[latin] font=X` / `latin_font=X` | `range.basic-latin = X` |
| `[latin] font=same` | `range.basic-latin = same` |
| `[latin] font=original` | `range.basic-latin = original` |
| `[latin] baseline=face` | `origin.basic-latin = face` |
| `[hires]`/`[font.N] baseline=±px` | `shift = ±px` |
| `[render] metrics` / `[font.N] metrics` | `advance` / `[font.N] advance` |
| `[hires] alpha=` | `[render] blend` (and `[render] target` if it chose the screen) |
| `[font.N] bitmap=X.SVF` | `[font.N] face = X.SVF` |
| `[bitmap] single/multi`, `[latin] bitmap` | a `[fonts]` name or path in `face=` / `range.basic-latin=`, relative to the map |
| `[glyphs] c = keep` | `[glyphs] c = original` |
| `[glyphs:csN]` | `[glyphs.N]` |
| `[encoding] codepage` | `[text] encoding` |

### 6.7 `[glyphs]`: remaps and targeted glyphs

Keys: a game code `0xNN` (or decimal), or a range `0xLL-0xHH`. Values:

| Value | Key form | Meaning |
|---|---|---|
| `original` | code or range | The game's font draws it (was `keep`; one name for the one meaning, the same word as the font value). |
| `u+XXXX` | code | Draw code point XXXX; it goes through the range rules (section 6.5 step 3 on). |
| `+0xNNNN` | range (or code) | Offset: draw `c + N`; goes through the range rules. |
| `<face>:u+XXXX` | code | **Targeted glyph**: draw code point XXXX from exactly `<face>`; the range rules are bypassed. |
| `<face>:+0xNNNN` | range (or code) | Targeted glyphs with an offset (`0x80-0x9F = icons:+0xE000` draws U+E080..U+E09F from `icons`). |

`<face>` is **one** entry of the font-value grammar: a `[fonts]` name, a path (`SYMBOLS.SVF`, `data:hires_text/x.svf`,
`C:\f\x.ttf#1`), or `same` (the id chain, first face with the glyph). A chain (comma) and `original` are refused with a
warning. The value is split at its **last** colon, so `data:` and drive letters survive; the part after the colon must be
`u+XXXX` or `+0xNNNN`. Private Use Area code points (U+E000-F8FF, planes 15-16) are allowed, so game-specific pictograms can
live in a custom SVF (`0x07 = ICONS.SVF:u+e001`).

Targeted glyph rules:

- **Opening and size**: the named face is opened under the same rules as a face of id N (section 5.4): a TrueType face at
  the id's `size`, not as a `pixel` face, with its fit probes limited to the targeted code points; an SVF at its baked cell,
  which must have the id's cell height. A targeted face is opened only for the ids whose `[glyphs.N]` (or the map-wide
  `[glyphs]`) name it.
- **Placement**: vertical placement is the id's (`align`, `shift` on SCI; `origin` rules **of the target code point** on
  SCUMM). Advance: the `advance` rules of the target code point (section 6.2), where `game` means the game font's width for
  the game code `c` - so `advance.U+2620 = game` keeps a pictogram on the game's grid.
- **Fallback**: when the face cannot be opened or has no glyph for the target code point, the **game's font draws `c`**
  (as `original`), with one warning at load naming the face and code point. Why not the `missing=` box: the author named an
  exact picture because the game code is a pictogram; the game's own low-res pictogram still carries that meaning, a box
  does not. The check runs at load (coverage is known then), so nothing is decided per draw.
- **Coverage**: a targeted face is not part of any chain; it answers only its listed codes and never takes part in the id's
  coverage fallback, `nearestFont()` borrowing, or the TrueType fit of the id chain. The load-time check above is its
  coverage check. The tools' coverage gate (`mkfont.py --require`) treats each target as a required glyph of that face.
- **Implementation contract**: the `[glyphs]` step returns, for a target, a virtual code point `kHiResTargetBase +
  index` (`kHiResTargetBase = 0x110000`, outside Unicode) that the ranged glyph source decodes to `{face, real cp}`; the
  range table is never consulted for it. Advance/origin lookups use the real cp.

Game code key per engine: SCI the decoded code point (so `0x21-0x7E = +0xFEE0` is the fullwidth recipe); SCUMM the charset
code of a single-byte character (`0x5e = u+2026`); AGS does not read `[glyphs]`.

## 7. Render target, blend and scale

### 7.1 `render_target` / `[render] target`

| Value | Screen |
|---|---|
| `clut8` | paletted 8-bit screen. |
| `rgb565` | 16-bit 5-6-5. A 1-5-5-5 format does not qualify. |
| `rgb888` | 8 bits per channel in a 4-byte pixel, whatever the fourth byte is (xrgb8888, argb8888, abgr8888...). There is no `argb8888` value: the screen's alpha byte is never used. |
| `auto` | `clut8` when nothing can blend (the resolved `blend` is `off`, or every face of every id chain, range rule and target is 1 bpp and `blend` is not `on`). Otherwise the first of `rgb888`, `rgb565` the backend offers at the needed size, else `clut8` with one warning. |

Resolution: game-domain ini > `[scummvm]` ini > map > `auto`. An explicit value the backend cannot give (or the engine
cannot draw, section 7.3) falls back in the order `rgb888`, `rgb565`, `clut8`, skipping what is unavailable, with one
warning naming what was asked and what was set.

Per engine:

- **SCUMM** (v < 7): asks `initGraphics()` for the formats that match the resolved target, then the fallbacks. v7+
  (palette driven by SMUSH) always gets `clut8`; an explicit non-`clut8` target is warned about. The 16-bit sink that
  FM-Towns already uses (`HiResPalette16Sink`) draws `rgb565`.
- **SCI**: `render_target` decides the format requested in `Sci::GfxDriver` creation (`drivers/init.cpp`) and
  `GfxDefaultDriver::initScreen()`. With `render_target=auto` and upstream `rgb_rendering` or `palette_mods` on, the
  upstream request stands exactly as upstream does it (RGB, the upscaled driver's first 4-byte format) - upstream behaviour
  is kept. With an explicit target, that target is requested; if it is `clut8` while `rgb_rendering`/`palette_mods` is on,
  one warning says `render_target=clut8` wins and the upstream RGB request is dropped.
- **AGS**: does not read it (AGS takes its colour depth from the game); set in an AGS game's map it is warned about once.
- **DOS backend**: section 9.

### 7.2 `blend` / `hires_text_blend`

| Value | Meaning |
|---|---|
| `auto` | blend a face's coverage when the face has coverage (2 bpp or 8 bpp SVF, or TrueType) and the screen is not `clut8`; otherwise draw it as a hard stencil. |
| `on` | blend whenever the face has coverage. On `clut8` this means **palette-matched anti-aliasing**: each partially covered pixel gets the palette entry nearest (in RGB) to the blend of text colour and the pixel under it, computed per palette change through a cache keyed by (text colour, background index, coverage level); coverage is quantised to 4 levels for a 2 bpp face and 8 levels for 8 bpp. Until that is implemented, `on` with `clut8` gives one warning and draws the hard stencil. |
| `off` | never blend: coverage is thresholded at 50% into a stencil, as the engines do today on a paletted screen. |

SCI now honours `blend` (it used to blend whenever the screen was RGB). SCUMM's options dialog checkbox "Smooth the hi-res
text" shows the effective state and writes `hires_text_blend=on|off` only when the player toggles it; an untouched dialog
writes nothing (fixes the old unconditional `hires_text_alpha=true`).

### 7.3 RGB565 output (defined now, SCI implementation may land later)

On `rgb565` the text is composited into the 16-bit screen with 8-bit coverage math per channel and the result rounded to
5-6-5 (no dithering). SCUMM already has this path. SCI: until its 16-bit hi-res compositor exists, an `rgb565` request is
treated as unavailable for SCI (section 7.1 fallback: `rgb888` with one warning).

### 7.4 Scale

`[render] scale` / `hires_text_scale`: the hi-res text surface multiplier.

| Where | Accepted | Otherwise |
|---|---|---|
| SCI | 2 | warning `SCI draws hi-res text at 2x only`, 2 |
| SCUMM, AGS (not DOS) | 1, 2, 3 | warning, the default |
| any engine on the DOS backend | 2 | warning `the DOS backend runs hi-res text at 2x only`, 2 |

The platform limit comes from one shared function, `Graphics::hiResScaleLimits()`, which reads the backend-registered
default `hires_text_platform_scale`: the DOS backend registers `2` in `OSystem_DOS::initBackend()`, no other backend
registers it, and without it the limits are 1..3. So neither engines nor shared code carry a DOS `#ifdef`. It is a backend
capability, not a player key, and is not documented for players. A non-numeric ini value is a warning and is ignored (never `ConfMan.getInt()`'s `error()`).

## 8. Engine scope (defaults below `[font]`)

| | SCI | SCUMM | AGS |
|---|---|---|---|
| `[font.N]` id | font resource id | charset 0..19 | font number |
| qualifiers | platform code | gameid, then `v<N>` | gameid |
| `range.basic-latin` | `original` | `same` | none |
| `advance` | as today: the 16 px cell (`cell`) for a legacy code-page game, `font` for a UTF-8 translation (per-glyph mode) | as today (C31: a bitmap face steps on the game's grid, a wide TrueType glyph by the face) | the face |
| `advance.basic-latin` | `game` | `game` | - |
| `origin` | - | `game` | - |
| `scale` | 2 (only) | 2 | 1 |
| empty id chain | `.uni` bundle | nearest charset's faces | the game's font |

"As today" means: when no advance key applies, the adapter takes exactly the code path it takes now; the regression gates
compare pixels before and after.

## 9. The DOS backend

- `dos_truecolor` is removed (no reader, no `registerDefault`). Its replacement is `render_target=clut8` in `[scummvm]`.
- `DosGraphicsManager::getSupportedFormats()` reads `render_target` through ConfMan's normal lookup (active game domain,
  then `[scummvm]`). `auto` (or unset) reports what it reports today: every format the mode list can set at the size (exact
  or through the 640x480 line-repeat fallback), cheapest on the bus first (rgb565, xrgb1555, xrgb8888), then CLUT8. An
  explicit value reports only that family (for `rgb565` only 5-6-5; for `rgb888` only 4-byte 8-8-8) that can be set, then
  CLUT8. So the cap also governs upstream paths that ask for `nullptr` (SCI EGA with `rgb_rendering`, videos).
- `rgb565` at 640x400: DOSBox-X offers it directly; DOSBox Staging has no 640x400 5-6-5 mode, so the existing 640x480
  line-repeat mode is used (`dos_force_fallback` still forces it for tests).
- Scale: section 7.4 (2 only): `initBackend()` registers `hires_text_platform_scale=2`.
- Unchanged backend keys: `dos_vsync`, `dos_force_fallback`, `dos_loading_screen`, `dos_timer_selftest`,
  `dos_mixer_selftest`, `dos_midi_log`, `opl_log`.

## 10. Errors and warnings

All messages go through `warning()` (SCI, SCUMM, backend) or `Debug::Printf(kDbgMsg_Warn, ...)` (AGS), once per cause per
load, and are also kept in `HiResMap::warnings` for tests.

### 10.1 The map is refused (loaded as "no map")

- Unreadable file, not an INI, or `[map] version` missing or not `2`: `HIRESTXT.MAP <path>: not a version 2 map; regenerate it
  (graphics/hires_text/README.md)`. When the refused file has an old section (`[hires]`, `[latin]`, `[bitmap]`,
  `[encoding]`), the message names the first one found: `... (found [latin])`.

### 10.2 The map loads; the offending key is ignored

- Unknown section, unknown key in a known section, or a removed section/key (section 3.3): `HIRESTXT.MAP: [latin] is not
  read any more; see "Ranges" in graphics/hires_text/README.md`, `HIRESTXT.MAP: unknown key [font.4] bitmap`.
- Invalid value (out of range, wrong word, bad code point): the key is ignored and the next level down applies (never a
  silent substitute value).
- Font value: unknown face name; entries after `original`; `same` where it has no meaning (section 5.2).
- Ranges: unknown block, malformed span, duplicate spelling, equal-width overlap (section 6.2.1).
- `[glyphs]`: a chain or `original` as a target face; a target code point that is not `u+`/`+0x`.

### 10.3 The engine cannot honour a key

The loader takes the engine's honoured-key set (`HiResEngineKeys`) and warns once per section/key the map sets that the
engine does not use: `SCI does not use [font.4] mirror`. The key is otherwise parsed (so its syntax is still checked).

### 10.4 At open and draw time

- A face that fails to open, an SVF whose cell height differs from its id's, a targeted glyph whose face lacks it (section
  6.7), a `missing=` code point no face of a chain has: one warning each, at load. Texts:
  `HIRESTXT.MAP: <path>: cell height <h> differs from <first path>'s <h0> on <id>; not used`,
  `HIRESTXT.MAP: [glyphs] 0x<code> -> <face>:U+<cp>: the face has no such glyph; the game's font draws it`,
  `HIRESTXT.MAP: missing=U+<cp> has no effect: <face> has no glyph for it`.
- An unavailable render target, `blend=on` on `clut8` before section 7.2 is implemented, an unsupported scale: one warning.
- The ini: invalid values are warnings and the key is ignored (the map or default applies); no ini value is ever
  substituted by another value.

## 11. ini keys

All hi-res text keys are read from the **game domain only**, except `render_target` and `hires_text`, which fall back to
`[scummvm]`.

| Key | Values | Default | Read by | Replaces |
|---|---|---|---|---|
| `hires_text` | `true` `false` | `true` | SCI, SCUMM, AGS | SCUMM's `hires_text` (now all three). `false`: no map, no faces, the engine draws exactly as without the layer. |
| `hires_text_map` | path (section 4) or empty | `<game folder>/HIRESTXT.MAP` if present | SCI, SCUMM, AGS | same key; relative now = game folder everywhere |
| `hires_text_face` | font value | - | SCI, SCUMM, AGS | `hires_text_font`, `hires_text_latin_font`. Names resolve through the loaded map's `[fonts]`. `original`: every id on the game's font (section 6.5 step 3). |
| `hires_text_size` | `8`..`64` | - | SCI, SCUMM, AGS | `hires_text_font_size` |
| `hires_text_scale` | `1` `2` `3` | - | SCI, SCUMM, AGS | `hires_text_scale`, `korean_hires_scale` |
| `hires_text_blend` | `auto` `on` `off` | - | SCI, SCUMM, AGS | `hires_text_alpha`, `korean_alpha_text` |
| `hires_text_advance` | `game` `font` `cell` | - | SCI, SCUMM | `hires_text_metrics`, `hires_text_latin`, `hires_text_latin_space` (the space and fullwidth parts need a map `[glyphs]`) |
| `hires_text_log` | `true` `false` | `false` | SCI, SCUMM | same key |
| `render_target` | `auto` `clut8` `rgb565` `rgb888` | `auto` | SCI, SCUMM, DOS backend | `dos_truecolor`; `rgb_rendering`/`alpha=` in their render role |

Removed without replacement: `hires_text_font`, `hires_text_font_size`, `hires_text_latin`, `hires_text_latin_space`,
`hires_text_latin_font`, `hires_text_metrics`, `hires_text_alpha`, `korean_alpha_text`, `korean_hires_scale`,
`korean_ttf_map` (and its warning), `dos_truecolor`. A range rule in an ini key would be unreadable: a player who wants one
writes a map. Unchanged, upstream-owned: `rgb_rendering`, `palette_mods`, `render_mode`, `disable_dithering`, `text_encoding`.

The GUI keeps "Use hi-res fonts" (`hires_text`) and "Smooth the hi-res text" (`hires_text_blend`, section 7.2).

## 12. Examples

### 12.1 SCI: Laura Bow 1, U preset

```ini
; DATA/LB1KOU.MAP - Laura Bow 1 Korean (UTF-8 translation), U preset.
; body = KO2350B.SVF (Gowun Batang Bold 17 px, 2 bpp), ui = KO2350G.SVF
; (NanumGothic Bold 16 px, 2 bpp); both baked on one baseline in a 16x16 cell.
[map]
version=2

[render]
target=rgb888          ; true colour; the ini may still say rgb565 or clut8
blend=auto             ; 2 bpp faces -> blended on an RGB screen

[fonts]
ui=KO2350G.SVF
body=KO2350B.SVF

[font]
face=ui
align=cell
missing=u+25a1
range.basic-latin=same          ; ASCII from the same SVF (was [latin] mode=proportional)
advance.basic-latin=font        ; was [latin] metrics=font

[font.1]
face=body
[font.4]
face=body
[font.8]
face=body
advance.basic-latin=game        ; cast names lined up with spaces (was metrics=game)
[font.40]
face=body
cell=glyph
size=18
[font.41]
face=body
cell=glyph
size=18
```

```ini
; SCUMMVM.INI
[scummvm]
render_target=auto      ; clut8 here forces 8-bit everywhere (was dos_truecolor=off)

[lb1ko]
gameid=laurabow
engineid=sci
language=ko
path=GAMES\LB1KO
extrapath=DATA
hires_text_map=data:LB1KOU.MAP

[lb1kol]
gameid=laurabow
engineid=sci
language=ko
path=GAMES\LB1KO
extrapath=DATA
hires_text_map=data:LB1KOL.MAP
```

`rgb_rendering=true` is no longer needed: the map says `target=rgb888`. `size=18` with `cell=glyph` makes ids 40 and 41
lay out on an 18 px cell (an SVF keeps its baked glyphs; only the cell grows).

### 12.2 SCUMM: Monkey Island 2, U preset

```ini
; DATA/M2KOU.MAP - Monkey Island 2 Korean (mi2kor patch), U preset, 2 bpp.
[map]
version=2

[render]
target=rgb888
blend=auto
scale=2

[text]
encoding=cp949

[fonts]
dlg=M2U0.SVF
small=M2U1.SVF
sent=M2U2.SVF
card=M2U4.SVF
card7=M2U7.SVF
card8=M2U8.SVF

[font]
missing=u+25a1
range.basic-latin=same            ; ASCII from each charset's own SVF (was [latin] font=same)
advance.basic-latin=font          ; was [latin] metrics=font
origin.basic-latin=face           ; the card fonts' '.' and ',' sit on the SVF baseline (was baseline=face)
origin.general-punctuation=face   ; the remapped ellipsis follows the letters around it

[font.0]
face=dlg
[font.1]
face=small
[font.2]
face=sent
[font.3]
face=small
[font.4]
face=card
[font.5]
face=small
[font.6]
face=dlg                          ; MI2 draws its verb line in charset 6
[font.7]
face=card7
[font.8]
face=card8

[glyphs]
0x07 = original                   ; the skull bullet stays the game's own pictogram
; 0x07 = ICONS.SVF:u+e001         ; or: a hi-res skull baked into a custom SVF at a PUA code point
0x5e = u+2026                     ; ellipsis
0x5c = u+0021                     ; exclamation mark
0x60 = u+0022                     ; double quote
```

```ini
; SCUMMVM.INI
[mi2ko]
engineid=scumm
gameid=monkey2
language=ko
path=GAMES\MI2
extrapath=DATA
hires_text_map=data:M2KOU.MAP

[mi2ko565]                          ; the same map on a 16-bit screen
engineid=scumm
gameid=monkey2
language=ko
path=GAMES\MI2
extrapath=DATA
hires_text_map=data:M2KOU.MAP
render_target=rgb565
```

The L presets differ in their SVF names and in `[render] target=clut8` (or `auto`: 1 bpp faces resolve to `clut8`).

## 13. Worktree sync rule for the shared parser

- **The shared set**: `graphics/hires_text/font_map.{h,cpp}`, `graphics/hires_text/font_value.{h,cpp}`,
  `graphics/hires_text/id_plan.{h,cpp}`,
  `graphics/hires_text/unicode_ranges.{h,cpp}`, `graphics/hires_text/hires_options.{h,cpp}`,
  `graphics/hires_text/glyph_source_ranged.{h,cpp}`, and their tests `test/graphics/hires_text_font_map.h`,
  `hires_text_font_value.h`, `hires_text_id_plan.h`, `hires_text_unicode_ranges.h`, `hires_text_hires_options.h`, `hires_text_glyph_source_ranged.h`.
- **Where**: `dos-port` (`~/work/scummvm/dos`) and `i18n` (`~/work/scummvm/i18n`; `~/work/scummvm/i18n-win` is a detached
  checkout of the same commits and follows `i18n`). The `hires-text` branch in `~/work/scummvm/repo/scummvm` is an older
  line and is not part of the set.
- **Rule**: at every sync point the shared set is byte-identical in both branches (`cmp`). Between sync points only
  `dos-port` edits the shared set; nobody edits it on `i18n`. A sync point is a commit on each branch whose message names
  the other branch's commit it matches (`Sync shared hi-res parser with dos-port <sha>`).
- **Sync points**: (1) before this work starts - `i18n` receives `dos-port`'s `missing=` and `[latin] baseline` parser
  commits, so the two agree on the old parser; (2) after this work passes its regression gates - the shared set moves to
  `i18n` together with the adapters that consume it (the files cannot move alone: the new parser breaks the old adapters).
  How sync point 2 is carried out (a merge of `dos-port` into `i18n`, or a port of this work's commits) is a user decision.
- A test run on each branch at a sync point: the shared tests pass there.

## 14. Out of scope

- The value set of the upstream ini `text_encoding` (SCI and SCUMM keep their readers).
- AGS reading `range.*`, `[glyphs]`, `advance`, `missing`: warned as unhonoured; a later change can add them through the same
  shared compiled plan.
- `rgb555` as a target value (the DOS backend offers xrgb1555; add a value if a use appears).
- Per-range `shift`/`align`.
