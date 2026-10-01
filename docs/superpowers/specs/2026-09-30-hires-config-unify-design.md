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
7. **Addendum (user, 2026-09-30): one map per game holds every preset.** The U (true-colour, anti-aliased) and L
   (8-bit, 1 bpp) presets are render-target variants inside one map, selected by the resolved render target through
   section qualifiers (`[font]` = the default preset, `[font:clut8]` = the 8-bit one; section 3.4), not separate map
   files. A package's targets differ only by `render_target=`. `render_target` is also a popup in the Graphics
   options (section 11.1).

## 1. Vocabulary

| Term | Meaning |
|---|---|
| **id** | the engine's font number: SCI font resource id, SCUMM charset 0..19, AGS font number. `[font.N]` addresses it. |
| **game code** | the character code the engine hands the hi-res layer for one glyph, after the engine has parsed its own control codes. SCI: the decoded Unicode code point (UTF-8 translation or legacy code page). SCUMM: the charset code of a single-byte character, or the decoded code point of a double-byte one. AGS: the decoded code point. |
| **code point** | the Unicode scalar that is drawn. Starts as the decoded game code; `[glyphs]` can change it. |
| **face** | one opened font file: an SVFN bitmap font (`*.SVF`, sniffed by the `SVFN` magic) or a TrueType/TTC face (with optional `#N`). |
| **chain** | an ordered list of faces; the first face that has a glyph for the code point draws it (coverage fallback). |
| **id chain** | the chain the id's resolved `face` key gives (section 5.3). |
| **qualifier** | a section suffix after a colon. An **engine qualifier** is the engine's: SCI the platform code; SCUMM the gameid, then `v<N>`; AGS the gameid. A **target qualifier** is a resolved render target: `clut8`, `rgb565` or `rgb888` (section 3.4). |
| **phase 1 / phase 2** | the two loads of a map (section 7.1.1): phase 1 reads the bare and engine-qualified sections to choose the render target; phase 2 reads the whole map for that target. |
| **engine scope** | the defaults an engine supplies below `[font]` (section 8). |

## 2. Principles

1. One name per concept, the same name in every scope. Map-wide defaults live in `[font]`, per-id overrides in `[font.N]`,
   with the same keys in both.
2. One value grammar for every face, in the map and in the ini: the **font value** (section 5).
3. One precedence rule: **ini > `[S.N:q]` > `[S.N]` > `[S:q]` > `[S]` > engine scope**, key by key. No sentinel inverts it.
   `[S:q]` stands for every qualified form in the qualifier order of section 3.4 (`:e:t`, then `:e`, then `:t`).
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
- Sections may carry one qualifier after a colon: `[font.4:pc]`, `[render:monkey2]`, `[glyphs.2:v5]`, `[font:clut8]`,
  or two, an engine qualifier then a target qualifier: `[font.4:monkey2:clut8]` (section 3.4). The engine passes its
  engine qualifiers most specific first; the loader adds the resolved target; each key is looked up in the qualified
  sections in the order of section 3.4, then `[S]`.
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
| `target` | `auto` `clut8` `rgb565` `rgb888` | `auto` | SCI, SCUMM; AGS `-` | ini `render_target` (game domain) > ini `render_target` (`[scummvm]`) > `[render:e]` > `[render]` > `auto`. Read in phase 1 only: a `target` key in a target-qualified `[render:t]` / `[render:e:t]` is refused (section 3.4). Section 7.1. |
| `blend` | `auto` `on` `off` | `auto` | SCI, SCUMM, AGS | ini `hires_text_blend` > map (target-qualified sections included) > `auto`. Section 7.2. |
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

#### `[translation.<lang>]`

`[translation.<lang>]` sections are reserved for docs/superpowers/specs/2026-09-30-map-languages-design.md; the v2 loader
skips them without warnings.

### 3.3 Removed outright

Sections `[hires]`, `[latin]`, `[bitmap]`, `[encoding]`, `[sizes]`, `[translation]`, and `[map] height_N`. Keys `font=`
(as an alias of `face=`), `bitmap=`, `latin*=`, `metrics=`, `baseline=`, `alpha=`, `enabled=`, `[glyphs] ... = keep` (now
`original`), `[glyphs:csN]` (now `[glyphs.N]`), and `[render] mode/metrics`. No alias is read. A v2 map that contains one of
the removed sections gets one warning per section naming its replacement (section 10.2) and is otherwise loaded.

### 3.4 Render-target qualifiers (one map, every preset)

A map carries the presets for every screen as **target-qualified sections**. The default preset (in practice the
true-colour, anti-aliased U preset) is written in the bare sections; a variant for a render target overrides only the
keys that differ, in sections qualified with that target:

```ini
[fonts]                 ; default (rgb888 and rgb565: the U preset)
ui = KO2350G.SVF
[fonts:clut8]           ; render_target=clut8 (the L preset)
ui = KO2350.SVF
[render:clut8]
blend = off
```

**Which sections.** Every section that takes an engine qualifier takes a target qualifier: `[render]`, `[fonts]`,
`[font]`, `[font.N]`, `[glyphs]`, `[glyphs.N]`, `[shadow]`. Not target-qualifiable: `[map]` (never qualified), `[text]`
and `[layout]` (the game's encoding and line breaking must not change with the screen: a preset switch would re-flow
text differently), and `[translation.*]` (the map-languages spec). A target-qualified form of those is warned about and
ignored.

**Names.** The target qualifiers are exactly `clut8`, `rgb565`, `rgb888` (case-insensitive). They are reserved: no SCI
platform code, SCUMM gameid, `v<N>` or AGS gameid has these names, so an engine qualifier cannot be mistaken for one.
`auto` is not a qualifier (the resolved target is never `auto`).

**One syntax, at most two qualifiers.** `[S]`, `[S:e]`, `[S:t]`, `[S:e:t]`: the engine qualifier first, the target
last. `[S:t:e]` (target first), `[S:e1:e2]` (two engine qualifiers), three or more qualifiers, and `[S:auto]` are
warned about and the section is ignored (section 10.2).

**Order.** With engine qualifiers `e1, e2, ...` (most specific first) and the resolved target `t`, the loader looks at

```
[S:e1:t]  [S:e2:t]  ...  [S:e1]  [S:e2]  ...  [S:t]  [S]
```

most specific first. An engine qualifier beats a target qualifier: the target only chooses a screen variant, while the
engine qualifier names the game, and adding a `:clut8` variant to a shared map must never change what an existing
`[S:monkey2]` section does. An author who needs both writes `[S:monkey2:clut8]`. Id scopes stay above all of it
(principle 3): `[font.4]` beats `[font:clut8]` for id 4, so a map whose ids name their own faces either qualifies the
ids (`[font.4:clut8]`) or - simpler - keeps `face=<name>` in the ids and refines the names in `[fonts:clut8]`.

**Merge rule** - the loader's existing rule (section 6.2.1 and the Task 4 loader), unchanged, over the longer list:
scalar keys (`face`, `size`, `missing`, `blend`, `[shadow]` keys, ...) are taken from the first section in the order
above that sets them; `range.`/`advance.`/`origin.` rules merge span by span (a qualified section's rule for a span
replaces the less specific rule for that span; other spans from all levels remain); `[glyphs]` merges code by code;
`[fonts]` merges name by name. A key a qualified section does not set falls through to the next level - this is what
lets a `:clut8` section hold only the differences.

**Values, not deletions.** A qualified section sets values; it cannot remove a key a less specific section set. Every
key can spell its default (`align=game`, `cell=game`, `shift=0`, `missing=off`, `origin=game`, `mirror=off`; for a range
family the engine scope's value, section 8, e.g. SCI `range.basic-latin=original`, `advance.basic-latin=game`), except
the id-wide `advance`, whose default is the engine's own rule: a key only one preset uses without such a spelling goes
into that preset's qualified sections, not the bare ones. (None of the shipped maps needs it.)

**No fallback between targets.** A target reads its own qualified sections, then the bare ones - never another
target's. So `rgb565`, which blends like `rgb888`, uses the bare (default) preset when the map has only `[S]` and
`[S:clut8]`: write the true-colour preset bare and the 8-bit preset as `:clut8`, and every RGB screen shares the
anti-aliased faces. A map that writes `[S:rgb888]` sections instead of bare ones gives `rgb565` nothing from them;
the tools write the default preset bare for that reason.

**`[render:t] target`.** The target is chosen before target-qualified sections are read (section 7.1.1), so a `target`
key in `[render:t]` or `[render:e:t]` is circular: it is warned about and ignored. `blend`, `scale` and `gamma` are
honoured there.

**Warnings once.** The phase-1 load (section 7.1.1) is silent; the phase-2 load reports every warning of the map,
including the target-qualifier ones, once. When the phase-1 load refuses the map (section 10.1), the engine prints
that load's warnings itself and there is no phase 2.

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
| `auto` | no preference: phase 1 (section 7.1.1) decides from the map and the faces, then the backend. |

Resolution: game-domain ini > `[scummvm]` ini > map (`[render:e]`, `[render]`) > `auto`. An ini value `auto` means "no
ini preference": it falls through to the map, but a game-domain `auto` still hides a `[scummvm]` value (the game domain
is read first). An explicit value the backend cannot give (or the engine cannot draw, section 7.3) falls back in the
order `rgb888`, `rgb565`, `clut8`, skipping what is unavailable, with one warning naming what was asked and what was
set.

#### 7.1.1 Two phases: the target first, then the sections for it

The render target selects target-qualified sections (section 3.4), and target-qualified sections may name other faces,
so the target is resolved from a view of the map that has none of them:

1. **Phase 1 (target).** Load the map with the engine qualifiers only (quiet: its warnings are dropped). The **wanted
   target** is the ini `render_target` if it is not `auto`, else the phase-1 `[render] target` if it is not `auto`, else
   **auto**: `clut8` when nothing can blend - the phase-1 blend (ini `hires_text_blend` > phase-1 `[render] blend`) is
   `off`, or every face the phase-1 view names (ini `hires_text_face`, every `face`, every `range.` value, every
   `[glyphs]` target file) is 1 bpp and the blend is not `on` - otherwise `rgb888`. (This is the rule of the first
   version of this section, applied to the phase-1 view; for a map with a bare U preset it gives `rgb888`, for a map
   whose bare faces are all 1 bpp it gives `clut8`, as before.)
2. **The backend.** The engine asks for `formatRequest(wanted, getSupportedFormats(), engineCanRgb565)` (the fallback
   order above). The **resolved target** is the family of the screen format the engine actually runs on:
   `targetOfFormat()` = `clut8` for CLUT8, `rgb565` for any 2-byte format, `rgb888` for any 3- or 4-byte format. It is
   never `auto`. On the DOS backend `getSupportedFormats()` is already capped by `render_target` (section 9), so an
   ini `render_target=clut8` is honoured even by upstream paths that do not ask the hi-res layer.
   - An engine that loads its faces **after** its screen is set (SCI: `GfxCache` is created after `GfxScreen`) uses
     the actual `g_system->getScreenFormat()`.
   - An engine that loads **before** `initGraphics()` (SCUMM) uses the predicted family - the first format of its
     request list - and, because upstream `initGraphics()` picks the first format of the BACKEND's list that the request
     contains, the request names only that one RGB family (then CLUT8), so the backend's order cannot substitute the
     other family - and after
     `initGraphics()` compares it with the actual screen; if they differ it warns once (`SCUMM: the screen is <actual>,
     not <predicted>; hi-res text uses the <actual> sections`) and repeats phase 2 with the actual family.
   - An engine whose screen format is not chosen by the hi-res layer passes the family of the screen it runs on (SCUMM
     v7+: `clut8`; AGS: the game's colour depth, 8 -> `clut8`, 16 -> `rgb565`, 32 -> `rgb888`; SCI with
     `render_target=auto` and upstream `rgb_rendering`/`palette_mods`: whatever upstream set).
3. **Phase 2 (sections).** Load the map again with the engine qualifiers and the resolved target (section 3.4's order);
   this load reports every warning once, and everything below it (faces, plans, blend, scale, glyph tables, shadow)
   uses it.

So `auto` ends as "the screen actually set", and the map's `:clut8` preset is used whenever the screen is paletted -
also where the player did not ask for it (a SCUMM v7 game, an 8-bit AGS game, an SCI EGA game without `rgb_rendering`).

Per engine:

- **SCUMM** (v < 7): asks `initGraphics()` for the resolved RGB family only (or the first family the backend offers), then CLUT8 (7.1.1). v7+
  (palette driven by SMUSH) always gets `clut8`; an explicit non-`clut8` target is warned about. The 16-bit sink that
  FM-Towns already uses (`HiResPalette16Sink`) draws `rgb565`.
- **SCI**: `render_target` decides the format requested in `Sci::GfxDriver` creation (`drivers/init.cpp`) and
  `GfxDefaultDriver::initScreen()`. With `render_target=auto` and upstream `rgb_rendering` or `palette_mods` on, the
  upstream request stands exactly as upstream does it (RGB, the upscaled driver's first 4-byte format) - upstream behaviour
  is kept. With an explicit target, that target is requested; if it is `clut8` while `rgb_rendering`/`palette_mods` is on,
  one warning says `render_target=clut8` wins and the upstream RGB request is dropped. Phase 1 runs before the driver is
  created; phase 2 in `GfxCache` with the actual screen format.
- **AGS**: does not read `[render] target` (AGS takes its colour depth from the game); set in an AGS game's map it is
  warned about once. AGS does read target-qualified sections, with the target of the game's colour depth (no phase 1).
- **DOS backend**: section 9.

### 7.2 `blend` / `hires_text_blend`

| Value | Meaning |
|---|---|
| `auto` | blend a face's coverage when the face has coverage (2 bpp or 8 bpp SVF, or TrueType) and the screen is not `clut8`; otherwise draw it as a hard stencil. |
| `on` | blend whenever the face has coverage. On `clut8` this means **palette-matched anti-aliasing**: each partially covered pixel gets the palette entry nearest (in RGB) to the blend of text colour and the pixel under it, computed per palette change through a cache keyed by (text colour, background index, coverage level); coverage is quantised to 4 levels for a 2 bpp face and 8 levels for 8 bpp. Until that is implemented, `on` with `clut8` gives one warning and draws the hard stencil. |
| `off` | never blend: coverage is thresholded at 50% into a stencil, as the engines do today on a paletted screen. |

`blend` is read in phase 2, so it may differ per target (`[render:clut8] blend=off`), and `auto` means, per resolved
target: `rgb888` and `rgb565` - blend every face with coverage (the bare U preset's 2 bpp faces are anti-aliased on both);
`clut8` - hard stencil (a 2 bpp face that reaches a `clut8` screen because the map has no `:clut8` variant is thresholded
at 50%, today's behaviour). The phase-1 blend used by the `auto` target rule (section 7.1.1) reads only the ini and the
bare/engine-qualified `[render]`.

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

SCI's own scope also keeps `range.U+0000-001F` and `range.U+007F` (with their `advance` set to `game`) on the game's own
font: section 6.1 puts C0 controls and DEL in no named block, so without a built-in rule for them here they would fall to
an id's plain chain (section 6.5 step 4) the moment any face is configured at all, rather than staying on the resource
font as they always have. A map may still override either span explicitly.

## 9. The DOS backend

- `dos_truecolor` is removed (no reader, no `registerDefault`). Its replacement is `render_target=clut8` in `[scummvm]`.
- `DosGraphicsManager::getSupportedFormats()` reads `render_target` through ConfMan's normal lookup (active game domain,
  then `[scummvm]`) **while a game domain is active**; with no active domain (the launcher and its options dialogs) it
  applies no cap, so the Graphics options can list every target the hardware offers (section 11.1) even when
  `[scummvm] render_target=clut8`. `auto` (or unset) reports what it reports today: every format the mode list can set at the size (exact
  or through the 640x480 line-repeat fallback), cheapest on the bus first (rgb565, xrgb1555, xrgb8888), then CLUT8. An
  explicit value reports its own family first, then the other true-colour family, then CLUT8, each only when it can be set
  (`rgb565`: 5-6-5, 4-byte 8-8-8, CLUT8; `rgb888`: 4-byte 8-8-8, 5-6-5, CLUT8; `clut8`: CLUT8 alone; 1-5-5-5 is in neither
  family), so the section 7.1 fallback order still has a true-colour screen to fall back to. So the cap also governs upstream paths that ask for `nullptr` (SCI EGA with `rgb_rendering`, videos).
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
- Qualifiers (section 3.4), the section is ignored:
  `HIRESTXT.MAP: [font:auto]: auto is not a render-target qualifier; ignoring the section`,
  `HIRESTXT.MAP: [font:clut8:monkey2]: the render target goes last ([font:monkey2:clut8]); ignoring the section`,
  `HIRESTXT.MAP: [font:pc:v5]: the second qualifier must be clut8, rgb565 or rgb888; ignoring the section`,
  `HIRESTXT.MAP: [font:a:b:clut8]: a section takes at most two qualifiers; ignoring the section`,
  `HIRESTXT.MAP: [text:clut8]: [text] cannot depend on the render target; ignoring the section` (the same for
  `[layout]`). The key only: `HIRESTXT.MAP: [render:clut8] target cannot depend on the render target; ignoring it`.
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
| `render_target` | `auto` `clut8` `rgb565` `rgb888` | `auto` | SCI, SCUMM, DOS backend; AGS no (section 7.1.1) | `dos_truecolor`; `rgb_rendering`/`alpha=` in their render role; the map preset (`hires_text_map=...L.MAP`). Also the Graphics options popup (section 11.1). |

Removed without replacement: `hires_text_font`, `hires_text_font_size`, `hires_text_latin`, `hires_text_latin_space`,
`hires_text_latin_font`, `hires_text_metrics`, `hires_text_alpha`, `korean_alpha_text`, `korean_hires_scale`,
`korean_ttf_map` (and its warning), `dos_truecolor`. A range rule in an ini key would be unreadable: a player who wants one
writes a map. Unchanged, upstream-owned: `rgb_rendering`, `palette_mods`, `render_mode`, `disable_dithering`, `text_encoding`.

The GUI keeps "Use hi-res fonts" (`hires_text`) and SCUMM's in-game "Smooth the hi-res text" (`hires_text_blend`,
section 7.2; the launcher's bool checkbox is removed, pre-flight ruling S6), and gains the popup of section 11.1.

### 11.1 The Graphics options: "Hi-res text screen"

- **Where.** The Graphics tab of the global options (`GUI::GlobalOptionsDialog`) and of a game's options
  (`GUI::EditGameDialog`), both built by `OptionsDialog::addGraphicControls()` (`gui/options.cpp`), below "Render
  mode": a label **"Hi-res text screen:"** and a popup with **Auto**, **8-bit palette**, **16-bit colour**, **True colour**,
  writing `render_target` = `auto`, `clut8`, `rgb565`, `rgb888`. Tooltip: "The screen the game runs in when it draws
  hi-res text. 8-bit uses the map's paletted fonts; 16-bit and true colour blend the smooth ones. Takes effect the next
  time the game starts."
- **Domains.** The global dialog writes `[scummvm]`; a game's dialog writes the game domain and, like every key of
  that tab, only while "Override global graphic settings" is checked - unchecking it removes the key
  (`gui/options.cpp`'s removal path for `render_mode`), and a game domain that has `render_target` counts as overriding
  (`gui/editgamedialog.cpp`'s list at the `render_mode` check). "Auto" writes `auto`, as "Render mode"'s `<default>`
  writes its code: in a game domain it deliberately hides a global `clut8` (section 7.1: an ini `auto` hides the global
  value and then defers to the map).
- **Which entries.** Auto always; the three targets only when the backend offers them: `Graphics::hiResTargetsOffered()`
  maps `g_system->getSupportedFormats()` to a set (CLUT8 -> 8-bit, an exact 5-6-5 -> 16-bit, a 4-byte 8-8-8 -> true
  colour; `formatMatchesTarget()`). The DOS backend applies its cap only while a game runs (section 9), so the launcher
  lists what the hardware has. A popup with fewer than two targets is hidden (a backend without `USE_RGB_COLOR` offers
  CLUT8 only). A stored value the backend does not offer is shown as its entry anyway, marked "(not available here)",
  so the dialog never rewrites a value silently.
- **Which games.** The global tab always shows it. A game's tab shows it only when the target uses hi-res text:
  `MetaEngine::hasHiResText(target)` (new virtual, default `false`), implemented by SCI, SCUMM and AGS through one
  shared `Graphics::hiResTextConfigured(domain)`: `hires_text` is not `false` and `hires_text_map` is set non-empty, or
  `hires_text_face` is set, or `<path>/HIRESTXT.MAP` exists. Not a GUIO flag: GUIO flags come from the detection
  tables, are stored in the target when it is added (stale for existing targets) and cannot know whether a map has been
  put in the game folder; the per-target question is what `getExtraGuiOptions(target)` already asks (SCUMM's
  `targetHasHiResText()`), so the new hook follows that.
- **Restart.** The screen format is chosen at engine start (sections 7.1, 7.1.1), so a change applies the next time the
  game starts; the tooltip says so. When the dialog is closed with OK while an engine runs (`g_engine` non-null) and
  `render_target` changed, a `MessageDialog` says "The hi-res text screen changes the next time the game starts."
- **Blend** stays out of the Graphics tab: `blend=auto` already follows the target (section 7.2); the one useful manual
  choice, turning smoothing off, is SCUMM's in-game checkbox. With blending off a pre-v7 SCUMM game takes a paletted
  (CLUT8) screen and the map's `:clut8` sections, never an RGB screen it would not draw into, and prints
  `render_target=<wanted> is not available here; using clut8` once; `on` for `clut8` waits for palette-matched
  anti-aliasing (Task 20 of the plan), which is when a "Text smoothing" popup (Auto / On / Off, game domain only,
  since `hires_text_blend` is read only there) would earn its place.

## 12. Examples

### 12.1 SCI: Laura Bow 1, both presets in one map

```ini
; DATA/LB1KO.MAP - Laura Bow 1 Korean (UTF-8 translation).
; Default (true colour, rgb888/rgb565): body = KO2350B.SVF (Gowun Batang Bold 17 px, 2 bpp),
; ui = KO2350G.SVF (NanumGothic Bold 16 px, 2 bpp); both baked on one baseline in a 16x16 cell.
; render_target=clut8: every id on KO2350.SVF (16 px, 1 bpp) on the game baseline.
[map]
version=2

[render]
blend=auto             ; 2 bpp faces -> blended on an RGB screen, hard-edged on clut8

[fonts]
ui=KO2350G.SVF
body=KO2350B.SVF
[fonts:clut8]           ; the 8-bit preset: one 1 bpp face under both names
ui=KO2350.SVF
body=KO2350.SVF

[font]
face=ui
align=cell
missing=u+25a1
range.basic-latin=same          ; ASCII from the same SVF (was [latin] mode=proportional)
advance.basic-latin=font        ; was [latin] metrics=font
[font:clut8]
align=game                      ; the 1 bpp ink sits on the game baseline

[font.0:clut8]
shift=-1                        ; status line: the 1 bpp ink one row up
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
[font.40:clut8]
cell=game                       ; the 16 px glyphs keep the game cell
size=16
[font.41]
face=body
cell=glyph
size=18
[font.41:clut8]
cell=game
size=16
```

```ini
; SCUMMVM.INI
[scummvm]
render_target=auto      ; clut8 here forces 8-bit everywhere (was dos_truecolor=off)

[lb1ko]                 ; true colour: no render_target, so [scummvm] decides (auto -> rgb888 here)
gameid=laurabow
engineid=sci
language=ko
path=GAMES\LB1KO
extrapath=DATA
hires_text_map=data:LB1KO.MAP

[lb1kol]                ; 8-bit: the same map, its :clut8 sections
gameid=laurabow
engineid=sci
language=ko
path=GAMES\LB1KO
extrapath=DATA
hires_text_map=data:LB1KO.MAP
render_target=clut8
```

`rgb_rendering=true` is no longer needed: `auto` resolves to `rgb888` because the bare faces have coverage. `size=18`
with `cell=glyph` makes ids 40 and 41 lay out on an 18 px cell (an SVF keeps its baked glyphs; only the cell grows).
With `render_target=rgb565` the same map gives the bare (anti-aliased) preset on a 16-bit screen.

### 12.2 SCUMM: Monkey Island 2, both presets in one map

```ini
; DATA/M2KO.MAP - Monkey Island 2 Korean (mi2kor patch). Default: 2 bpp M2U*.SVF;
; render_target=clut8: 1 bpp M2L*.SVF (same cells per charset).
[map]
version=2

[render]
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
[fonts:clut8]
dlg=M2L0.SVF
small=M2L1.SVF
sent=M2L2.SVF
card=M2L4.SVF
card7=M2L7.SVF
card8=M2L1.SVF                    ; the L set has no own charset 8 face

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
hires_text_map=data:M2KO.MAP

[mi2kol]                            ; the same map, 8-bit
engineid=scumm
gameid=monkey2
language=ko
path=GAMES\MI2
extrapath=DATA
hires_text_map=data:M2KO.MAP
render_target=clut8

[mi2ko565]                          ; the same map on a 16-bit screen: the bare (2 bpp) preset
engineid=scumm
gameid=monkey2
language=ko
path=GAMES\MI2
extrapath=DATA
hires_text_map=data:M2KO.MAP
render_target=rgb565
```

Package names stay: every BAT keeps its name and starts the same target; only the maps merge
(`<X>KOU.MAP` + `<X>KOL.MAP` -> `<X>KO.MAP`) and the L targets say `render_target=clut8` instead of naming the L map.

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
- A fallback chain between targets (`rgb565` reading `:rgb888` sections), a target-qualified `[render] target`, a
  target-qualified `[text]`/`[layout]`, and a "Text smoothing" popup (section 11.1).
