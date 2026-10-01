# SCUMM Korean fonts: mkfont.py --chars-from \*.trs, scummtext.py, bake-scumm-fonts.sh

Font tooling specific to the SCUMM engine's Korean targets (MI1/MI2 on the
DOS port, M5). The general `mkfont.py` usage, cell-size rule and
`--chars-from`/`--limit` design are in `README.md`; this file only covers
what SCUMM adds: reading a `.trs` bundle for `--chars-from`, recovering text
from a patch that never shipped one, and baking a game's charsets from a
plan table. (This file exists separately from `README.md` because another
change was mid-flight against that file when this one was written; expect it
to be folded in later.)

## `mkfont.py --chars-from *.trs`

A `.trs` (SCVMTRS bundle - `engines/scumm/trs_bundle.h`, content rules in
`engines/scumm/script.cpp::resStrLen()`/`text_utf8.h`, and the same format
`harness/tools/trslib.py` reads and writes) is now a recognised
`--chars-from` input, picked by its `.trs` extension:

```sh
tools/korean/mkfont.py NanumGothic-Bold.ttf out.svf --size 16 --bpp 2 \
    --clip-cell --unicode ascii --chars-from korean.trs \
    --limit ascii,ksx1001-nohanja --require
```

Rules (`mkfont.chars_from_trs()`):

* Every string the bundle's line index points at is read - both the
  original (source-language) text and the translation, since either side
  can hold Korean (the source side can too, for a UTF-8 bundle keyed by the
  translated release's own already-Korean text - not MI1/MI2's case, but the
  reader does not need to know which release a bundle is for).
* A string's encoding is decided once for the whole bundle, from whether its
  body (right after the room/script table, before the first string) starts
  with the UTF-8 BOM `EF BB BF` (`trsBodyIsUtf8()`'s rule): UTF-8 if so, CP949
  otherwise. Measured on the two real bundles this task used - `korean.trs`
  from a UTE Monkey Island 1 conversion and from `gamedata/mi2kor` - **both
  are CP949**, not UTF-8, despite having been called "the UTF-8 trs" earlier
  in the M5 plan; the BOM check is what actually decides it, not a bundle's
  name or provenance.
* A string's end is `resStrLen()`'s rule, not the next NUL byte: `0xFF`
  introduces a SCUMM escape (one code byte, then two more argument bytes
  unless the code is 1, 2, 3 or 8), and a `0x00` inside those argument bytes
  does not end the string. `0xFE` is *not* special here - `resStrLen()` only
  ever checks for `0xFF` (confirmed by reading `engines/scumm/script.cpp`
  directly: the `heversion <= 71` branch, which covers every non-HE game
  including MI1/MI2, tests `chr == 0xFF` and nothing tests `0xFE`). Escape
  bytes are skipped, not decoded - they are opcode arguments, not glyphs.
* A string with no terminating NUL, or an escape that runs past the end of
  the file, stops the tool with a message naming the file and the string's
  offset.
* A byte or byte pair that does not decode under the chosen codec becomes
  U+FFFD and is dropped rather than baked - real `.trs` bodies carry a few
  non-text records (short binary-looking entries unrelated to any visible
  string), and decoding them best-effort rather than aborting keeps the tool
  usable without hand-editing the bundle first.

`--chars-from mi2kor/korean.trs --limit ascii,ksx1001-nohanja` collects 1286
distinct code points and drops 6: U+AD3B/AE1F/AE32/B2A2/BB69/BBB9 (괻 긟 긲
늢 뭩 뮹). These six are real CP949 characters - Microsoft's extended
Wansung/UHC region, lead byte 0x82-0x92 - so `mi2kor/korean.trs`, while
CP949 end to end, is not confined to plain EUC-KR/KS X 1001's 0xA1-0xFE lead
bytes everywhere. They fall back to the shared `missing=` box (□) in the L
preset the same as any other syllable outside the 2350, same as they would
without this change; logged in the M5 ledger per the task brief, not treated
as a bug.

## `scummtext.py`: recovering strings from a patch with no `.trs`

```sh
tools/korean/scummtext.py ~/games/mi1kor mi1kor-strings.txt
```

The 2005 DUMB Monkey Island 1 VGA floppy patch (`mi1kop`) predates the
`.trs` format: its Korean text lives only inside the patch's own encrypted
resource files, `DISK0N.LEC`, `000.LFL` and `90N.LFL` (XOR 0x69 - the
standard SCUMM v3/v4 resource obfuscation key), with no index telling a
reader where a string starts or ends. `scummtext.py` decrypts each file and
scans it for text: a NUL-terminated run whose every byte is printable ASCII
(0x20-0x7E), a valid EUC-KR two-byte pair (KS X 1001's 0xA1-0xFE x
0xA1-0xFE, decoded with `cp949`) or a SCUMM control code (`0xFF` + the same
argument-byte count as `resStrLen()`/`chars_from_trs()` above - only `0xFF`,
not `0xFE`, matches the EUC-KR range's own 0xA1-0xFE upper bound, so treating
`0xFE` as a possible control byte here would misread real two-byte text),
and that holds at least one Hangul syllable or compatibility jamo.

It is a scanner, not a parser: nothing here understands SCUMM opcodes, room
layout or which bytes are actually a `printLine` argument versus costume or
sound data - it only knows what a valid run of text bytes looks like. Two
consequences:

* **It tries every offset, not just the bytes after a NUL.** A first cut
  that treated each `\x00`-delimited span as one candidate string (mirroring
  how `.trs`/script strings are actually stored) missed most of the real
  text: a string's *first* byte is usually a script opcode's argument, not
  right after a NUL, so the candidate span starting at the previous NUL
  usually mixes unrelated binary bytes in front of the real text and fails
  the grammar as a whole. Scanning for where a valid run *starts*, resuming
  one byte past any failure, raised the yield from ~1,000 short/garbled
  matches to 7,639 strings (1,919 distinct Hangul syllables) across
  `gamedata/mi1kor`.
* **False positives happen and are left in.** EUC-KR's high bytes are dense
  enough that a short run of unrelated binary data can occasionally satisfy
  the grammar by chance (a couple of accidental syllables amid graphics or
  costume data - `잿灰`, `쨩標` in the sample output below are two). The
  extra glyphs are harmless to the font itself, but they are not harmless to
  `--require`: a noise syllable the TTF lacks makes `--require` fail the
  bake. So `bake-scumm-fonts.sh` passes `--require` only for a `.trs` CHARS
  file and omits it for a `scummtext.py` file (mkfont.py still prints how
  many requested glyphs the face lacks); if you run mkfont.py by hand on
  `scummtext.py` output, do the same or comment the noise out first. Anyone
  reading the output file for its text should expect noise mixed into real
  lines like:

  ```
  먼지가 덮인 책
  잉크
  깃털 펜
  노 젓는 배@@@@@@
  ```

The last line of the output file is a one-line summary:

```
# scummtext: 7639 strings, 1919 distinct Hangul, 636 distinct other
```

**Verified against Task 2's census**, per the task brief: `scummtext.py`'s
output on `gamedata/mi1kor` contains every Hangul syllable that appears in
the census's `mi1ko` 60-second HRTEXT log and the lookout-tower monologue
(`probe/m1talk/run.log`) - 214 distinct syllables, 0 missing. That log is
from the *Ultimate Talkie Edition* `.trs` translation, a different (and much
later) translation of the same game than this 2005 DUMB patch, so this is
not a check that the two texts read the same - it is a check that scanning
this patch's own resources is not silently missing large parts of its own
text. The two translations' wording differs, as does which glyphs each one
happens to need.

## `bake-scumm-fonts.sh`: baking a game's charsets from a plan table

```sh
tools/korean/bake-scumm-fonts.sh plan.tsv ~/games/mi2kor /out/dir chars.txt
```

`plan.tsv` is tab-separated, one output font per line:

```
<output 8.3 name>	<charset>	<ttf path>	<size>	<bpp>	<ascent or ->
```

The `<ttf path>` may start with `$FONTS`, written literally in the plan. The
script replaces it with the `FONTS` environment variable, default
`~/scummvm-i18n/fonts` (nothing else is expanded, so a plan cannot run
code). The four plans in `tools/korean/scumm-fonts/` use it, so they work
wherever the fonts are: `FONTS=/data/fonts tools/korean/bake-scumm-fonts.sh ...`.

`<charset>` is the SCUMM charset number
(`engines/scumm/charset.cpp::loadKorFont()`/`loadCJKCells()`:
`korean%02d.fnt`, numbered 0-19; MI1/MI2 use 0-8, one per in-game font -
dialogue, verbs/UI, etc.). The cell this script bakes at is **that
charset's own `korean<NN>.fnt` header, doubled** - not a fixed 16x16.  The
header is `[[skip 1], shadow, width, height]` (same 4 bytes
`loadKorFont()`/`loadCJKCells()` read); doubling matches the already-shipped
hi-res fonts exactly - measured against `gamedata/mi2kor`, `korean00.fnt`'s
header is 11x12 and the shipped `hr00.fnt`'s cell is 22x24. `<ascent>` is
passed to `mkfont.py --ascent` verbatim, or omitted (mkfont.py picks one from
the face and `--clip-cell`) when the field is `-`.

`<chars>` is one file for `--chars-from`: a `.trs` bundle or a
`scummtext.py` output file. Every line bakes with the fixed template

```sh
mkfont.py <ttf> <out> --size <size> --cell <H> --width <W> --bpp <bpp> \
    --clip-cell [--ascent <A>] --unicode ascii --chars-from <chars> \
    --limit ascii,ksx1001-nohanja [--require]
```

`--require` (added only when `<chars>` ends in `.trs`) means a syllable the
bundle's text actually uses but the chosen face lacks fails the whole bake
instead of silently shipping a gap - pick a face with the coverage the
bundle needs. For a `scummtext.py` file it is omitted, because the
heuristic's noise syllables would make it fail; the bake then reports how
many requested glyphs the face lacks, and the noise can be commented out of
the file to get a clean count.

A line whose `(ttf, size, bpp, cell, ascent)` was already baked earlier in
the same plan is copied from that earlier output rather than baked again -
MI1/MI2 commonly reuse one face at one size for several charsets (every UI
charset at the same cell, say), and re-running FreeType for an identical
result wastes time. Ascent is part of the key (`-` and an empty field count
as the same), so a line with an explicit ascent never reuses another line's
bake.

Comment lines (`#`, after leading whitespace) and blank lines in `plan.tsv`
are skipped.

## Bake options and baselines

Where a baked glyph stands is set here, at bake time; whether the game's own
per-glyph offset is added on top of it is set at run time by the map's
`origin.basic-latin=` key (`origin.<range>=game|face` in `[font]`/`[font.N]`;
`engines/scumm/HIRES_TEXT.md`, "Baselines and glyph offsets", which has the
diagrams and the before/after crops).

How each `mkfont.py` option affects the baked baseline:

* `--ascent N` is the baseline row, counted from the cell top. Left out,
  `choose_ascent_from()` chooses it: the face's own ascent + descent if that
  line fits the cell, else the ink box of a probe string centred in the cell
  (a Latin set larger than the cell puts the capitals at the cell top).
* `--size` is the face's pixel size. A bigger face has more ink above and
  below its baseline, so fewer ascents keep it inside the cell.
* `--fit-cell` bakes with **one** size and **one** ascent for every glyph: it
  starts at `--size` and steps down until all the glyphs' ink fits the cell.
  `--ascent` is then the preferred value when several ascents fit (the
  closest is taken).
* `--clip-cell` keeps one baseline for every glyph and cuts ink beyond the
  cell top or bottom; it never moves a glyph. It would delete a floating `.`,
  not pull it up. Without `--ascent` it also picks the ascent that cuts the
  fewest glyphs, preferring `choose_ascent_from()`'s value; an explicit
  `--ascent` is used as given.
* `--cell` / `--width` set the cell; the cell height drives the ascent
  choice. `bake-scumm-fonts.sh` sets them to 2x the game's `korean0N.fnt`
  header.
* `--unicode ascii` bakes Latin into the same SVF as the Hangul, so they
  share one baseline. (`--latin` is for single-byte fonts, glyph number =
  character code; it is not used for SCUMM.)
* In the plan table (`name charset ttf size bpp ascent`) `size` and `ascent`
  become `--size` and `--ascent`, and the script always adds `--clip-cell`.
  The numbers in `m2u.tsv` are what `--fit-cell` picked for each cell:
  Nanum 22x24 -> size 21, ascent 19; Gowun 26x24 -> size 23, ascent 21.

What follows from that:

1. `origin.basic-latin=face` is a **map** key. No SVF is re-baked for it.
2. Re-baking with a different `--ascent` moves the baked baseline (every
   glyph, Hangul and Latin, by the same rows); the card crops must be
   re-captured and looked at afterwards.
3. A baked baseline plus the game glyph's `offsY` is a double shift, and no
   bake option fixes it. Trimmed card fonts carry `offsY` up to 9 game px
   (charset 4: `.` 9, `a` 4, `A` 0), which is 18 rows at scale 2: a `.`
   baked at row 21 is drawn at row 39 of a 24-row cell. With
   `origin.basic-latin=face` the baked baseline is the only placement.
