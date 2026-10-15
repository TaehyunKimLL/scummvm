# SCUMM Korean fonts: mkfont.py --chars-from \*.trs, scummtext.py, bake-scumm-fonts.sh

Font tooling specific to the SCUMM engine's Korean targets (MI1/MI2 and
Indiana Jones 3/4 on the DOS port). The general `mkfont.py` usage, cell-size rule and
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
  otherwise. The Kor. Project's `korean.trs` of MI1 (Ultimate Talkie) and MI2 came as CP949
  (`README.md`, "Byte fixes to a translation bundle"; the shipped game folders now hold them in UTF-8,
  `trs_fix.py --utf8`); the BOM check is what decides it, not a bundle's name or provenance. A raw game
  byte (not UTF-8, not an escape) in a UTF-8 bundle is dropped like any undecodable byte.
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

With the CP949 bundle before its byte fixes, `--chars-from mi2kor/korean.trs --limit ascii,ksx1001-nohanja` collected 1286
distinct code points and dropped 6: U+AD3B/AE1F/AE32/B2A2/BB69/BBB9 (괻 긟 긲
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
code). The plans in `tools/korean/scumm-fonts/` use it, so they work
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

Two more positional arguments, both optional, cover a map's own remap
targets - a `[glyphs]` entry that points an in-game byte at a real Unicode
code point (`0x5e = u+2026`, say), rather than at the game's own glyph:

```sh
tools/korean/bake-scumm-fonts.sh plan.tsv ~/games/mi2kor /out/dir chars.txt \
    HIRESTXT.MAP 2026
```

`<extra_chars_from>` (here, the map itself) is a second `--chars-from` input:
`mkfont.py` already reads a map's `missing=` and `[glyphs]` absolute
`u+XXXX` targets, so this pulls a target the map just gained into the bake
without touching `<chars>`, the game's own scanned text. `<extra_limit>`,
if given, is appended to the hardcoded `--limit ascii,ksx1001-nohanja` (as
plain hex, no `u+` prefix, comma-separated for more than one) so that target
survives the limit too - a remap target such as `u+2026` is outside both
named ranges and would otherwise be silently dropped. Both default to
empty, reproducing the four-argument invocation byte for byte. Only the
preset whose SVF needs the remapped glyph takes these two arguments; a
preset that keeps the byte as the game's own glyph (`[glyphs:clut8] 0x5e =
original`, say) bakes with the plain four-argument form.

The shipped MI1/MI2 SVFs are a base face and a supplement face per cell and preset (below). The bases
(`M1U0-4`, `M1L2`, `M2U0-8`, `M2L2`; the jamo sets `M1L0/L1/L4`, `M2L0/L1/L4` are older and already complete)
are baked from the same plan tables, but from the 2,350 syllables of KS X 1001 rather than from what one game's
text uses:

```sh
G=~/work/scummvm/gamedata D=dists/engine-data/hires_text/dos W=/tmp/m1bake
mkdir -p $W
python3 -c "print(''.join(bytes([h,l]).decode('cp949') for h in range(0xb0,0xc9) for l in range(0xa1,0xff)))" > $W/ks2350.txt
tools/korean/bake-scumm-fonts.sh tools/korean/scumm-fonts/m1u.tsv $G/mi1ute-kor $D $W/ks2350.txt
tools/korean/bake-scumm-fonts.sh tools/korean/scumm-fonts/m2u.tsv $G/mi2kor $D $W/ks2350.txt
# L: bake into $W and copy only M1L2 and M2L2; M1L1/M2L1 are the neodgm jamo sets (jamoset --from-svf, below),
# M?L0/M?L4 the Taebaek ones.
tools/korean/bake-scumm-fonts.sh tools/korean/scumm-fonts/m1l.tsv $G/mi1ute-kor $W $W/ks2350.txt
tools/korean/bake-scumm-fonts.sh tools/korean/scumm-fonts/m2l.tsv $G/mi2kor $W $W/ks2350.txt
cp $W/M1L2.SVF $W/M2L2.SVF $D/
```

### Base and supplement faces

A base face has ASCII and all 2,350 KS X 1001 syllables whatever the game's text uses (a jamo set composes all
11,172 and counts as a base). What the game's `korean.trs` and the map draw beyond that - Latin-1 letters such as
the a-circumflex and e-acute of MI2's "Papier-Mâché", the ellipsis and trademark sign `[glyphs]` remaps
to, a syllable outside KS X 1001 (U+D25D in the MI1 Ultimate Talkie text) - is a small supplement face, baked by
`bake-supplements.py` and chained after its base in the map (`[font.2] face=sent,sentx`, with `sentx=M2U2X.SVF`
under `[fonts]` and the 8-bit face under `[fonts:clut8]`). The supplement has its base's cell, bit depth and
baseline (read from the base file) and exactly the characters its base lacks: the bundle's characters plus the
`[glyphs]` and `missing=` targets of every font id whose chain starts with that base, minus the base's own. A base
that lacks nothing gets none (the Taebaek and neodgm jamo sets of MI1; the 24 px MI2 ones need only the two
letters). `scumm-fonts/m1x.tsv`, `m2x.tsv` are the plans (supplement, base, TTF, size, flags):

```sh
tools/korean/bake-supplements.py tools/korean/scumm-fonts/m2x.tsv --trs $G/mi2kor/korean.trs \
    --map $D/M2KO.MAP --faces $D
tools/korean/bake-supplements.py tools/korean/scumm-fonts/m1x.tsv --trs $G/mi1ute-kor/korean.trs \
    --map $D/M1KO.MAP --faces $D
tools/korean/facecoverage.py --trs $G/mi2kor/korean.trs --map $D/M2KO.MAP --base-check   # exit 0, or what is missing
```

Source fonts: the supplement is baked from the face the base came from where that face has the glyph. The U
bases' Google NanumGothic-Bold has no a-circumflex or e-acute (nor has any Nanum Myeongjo): `A|B` in the TTF field
takes those from B, the NHN NanumGothic Bold, at the same size (`mkfont.py --fallback-ttf`). The Taebaek jamo sets
(M2L0, M2L4) have a 12x24 ASCII half that draws 0x80-0xFF as DOS Hangul pictographs and no Latin-1 letters: M2L0X
is NHN NanumGothic Bold and M2L4X Gowun Batang Bold (the U preset's serif card face), both FreeType-hinted mono at
23 px, whose capitals are as tall as Taebaek's (17 rows) on the base's baseline.

### Preload lists (`M1U4.HOT`, `M2U4.HOT`, `M2U7.HOT`, `M2U8.HOT`)

The true-colour card faces (MI2 `card`, `card7`, `card8` = M2U4, M2U7, M2U8; MI1 `cs4` = M1U4) are `[load]`
`stream` in the maps. M2U4 and M2U8 also have a `[preload]` list, so the characters the game draws with them are
read once at start and the rest of the face stays in the file (graphics/hires_text/README.md, "Preloading"). M2U7
(about 1,040 characters by the analysis) and M1U4 (about 870) are not preloaded: a cache entry keeps two cells plus
its bookkeeping (about 2.4 times the stored glyph), so a list that long holds as much as the whole face and reads
all of it at start (DOS measurements in runs/preload/report.md); their lists are shipped for a try
(`#card7=M2U7.HOT`, `#cs4=M1U4.HOT` in the maps). The 8-bit preset (`[load:clut8]` auto) keeps its small faces whole.
The lists are made from the scripts and bundles by `charsetchars.py` (union over the font ids whose chain uses the
face, the `[glyphs]` and `missing=` targets, ASCII) and shipped next to the maps; regenerate them whenever a bundle
or a map changes:

```sh
G=~/work/scummvm/gamedata; D=dists/engine-data/hires_text/dos
tools/korean/charsetchars.py $G/mi2kor --v5 monkey2 --trs $G/mi2kor/korean.trs -o /tmp/mi2.json \
    --map $D/M2KO.MAP --preset U --hot-faces card,card7,card8 --hot-dir $D
# MI1: the Ultimate Talkie text and the floppy text (mi1kor, no bundle: the scripts' own CP949) share M1U4
tools/korean/charsetchars.py <UTE game folder with MONKEY.000> --v5 MONKEY --trs $G/mi1ute-kor/korean.trs -o /tmp/mi1ute.json
tools/korean/charsetchars.py $G/mi1kor -o /tmp/mi1dumb.json
tools/korean/charsetchars.py --from-json /tmp/mi1ute.json /tmp/mi1dumb.json \
    --map $D/M1KO.MAP --preset U --hot-faces cs4 --hot-dir $D
tools/korean/facecoverage.py --trs $G/mi2kor/korean.trs --map $D/M2KO.MAP --hot $D   # list sizes and coverage (informational)
```

The sets are conservative supersets (a script reached in several charsets counts for all), so a list is longer than
what one play draws; a character missing from a list costs one read of the file, never a wrong glyph.

`facecoverage.py` is the check: for every font id of both presets it resolves the face chain and lists the
characters the chain lacks (a bitmap face by its table, a jamo set by composing, a TrueType face by its cmap);
`--base-check` also demands ASCII and the 2,350 syllables of the chain's first face; exit status 1 on any gap.
`test_facecoverage.py` runs it over the shipped maps whenever the bundles are in `gamedata/`.

The MI1 faces cover the Ultimate Talkie korean.trs and, as syllables of KS X 1001, the DUMB floppy text
(mi1ko/mi1kol/mi1kop of the MI1 game zip, same M1KO.MAP and cells: the UTE translation ships the DUMB korean0N.fnt
byte for byte). Not covered since the bases stopped being baked from a scan: the scan's non-text noise (Hanja, kana,
Greek, compatibility jamo) which the earlier bakes carried over.

The Indiana Jones 3 and 4 SVFs (`I3KO.MAP`, `I4KO.MAP`) are baked from each
game's own `korean.trs` the same way:

```sh
G=~/work/scummvm/gamedata D=dists/engine-data/hires_text/dos
printf '\xe2\x80\xa6\n' > ellipsis.txt
# Indy3: the map's only remap target is U+2026 (0x60 becomes U+0022, ASCII).
tools/korean/bake-scumm-fonts.sh tools/korean/scumm-fonts/i3u.tsv $G/indy3kor $D $G/indy3kor/korean.trs $D/I3KO.MAP 2026
tools/korean/bake-scumm-fonts.sh tools/korean/scumm-fonts/i3l.tsv $G/indy3kor $D $G/indy3kor/korean.trs $D/I3KO.MAP 2026
# Indy4: U takes U+2026 and U+2122; L the ellipsis alone (neodgm has no U+2122,
# and clut8 keeps the game's own 0x0f).
tools/korean/bake-scumm-fonts.sh tools/korean/scumm-fonts/i4u.tsv $G/indy4kor $D $G/indy4kor/korean.trs $D/I4KO.MAP 2026,2122
tools/korean/bake-scumm-fonts.sh tools/korean/scumm-fonts/i4l.tsv $G/indy4kor $D $G/indy4kor/korean.trs ellipsis.txt 2026
```

The Indy4 `korean.trs` these were baked from has seven translated lines
corrected (five syllables outside KS X 1001, such as 됬 -> 됐, and two lines
whose bytes the translation's converter had mangled) and four map/puzzle
tables restored to the game's own bytes; the bake reads the bundle as it
stands in the game folder.

Maniac Mansion, Zak McKracken, Loom and Day of the Tentacle (`MMKO.MAP`,
`ZAKKO.MAP`, `LOOMKO.MAP`, `DOTTKO.MAP`) use the same plan-table bake, one U
and one L plan each (`mm`, `zak`, `loom`, `dott` + `u`/`l` + `.tsv`). Every
SVF comes out byte for byte from these commands:

```sh
G=~/work/scummvm/gamedata D=dists/engine-data/hires_text/dos
P=tools/korean/scumm-fonts
printf '\xe2\x80\xa6\n' > ellipsis.txt
# MM and Zak: one charset (0), 0x5e = u+2026, 0x60 = u+0022.
tools/korean/bake-scumm-fonts.sh $P/mmu.tsv $G/mmkor $D $G/mmkor/korean.trs $D/MMKO.MAP 2026
tools/korean/bake-scumm-fonts.sh $P/mml.tsv $G/mmkor $D $G/mmkor/korean.trs $D/MMKO.MAP 2026
tools/korean/bake-scumm-fonts.sh $P/zaku.tsv $G/zakkor $D $G/zakkor/korean.trs $D/ZAKKO.MAP 2026
tools/korean/bake-scumm-fonts.sh $P/zakl.tsv $G/zakkor $D $G/zakkor/korean.trs ellipsis.txt 2026
# Loom: charsets 1 and 2. U also takes (TM) and (C) from the map (0x0f, 0x3d); L keeps the
# game's 0x0f and only needs the ellipsis.
tools/korean/bake-scumm-fonts.sh $P/loomu.tsv $G/loomkor $D $G/loomkor/korean.trs $D/LOOMKO.MAP 2026,2122,a9
tools/korean/bake-scumm-fonts.sh $P/looml.tsv $G/loomkor $D $G/loomkor/korean.trs ellipsis.txt 2026
# DOTT: no .trs (the Korean text is inside TENTACLE.00N), so the bake covers every KS X 1001
# syllable (2350). scummtext.py reads TENTACLE.00N too, but it is only a reference here.
python3 -c "print(''.join(bytes([h,l]).decode('cp949') for h in range(0xb0,0xc9) for l in range(0xa1,0xff)))" > dott-all.txt
tools/korean/bake-scumm-fonts.sh $P/dottu.tsv "$G/dott/Day Of the Tentacle (DOS Floppy)" $D dott-all.txt $D/DOTTKO.MAP
tools/korean/bake-scumm-fonts.sh $P/dottl.tsv "$G/dott/Day Of the Tentacle (DOS Floppy)" $D dott-all.txt $D/DOTTKO.MAP
```

The shipped DOTT, MI1 and MI2 L faces are not these bakes as they come out of
`bake-scumm-fonts.sh`: each is converted to a jamo set (1 bpp, flag bit 1) in
place, so the pack carries 344 pieces of the cell instead of every syllable.
`--from-svf` decomposes the precomposed face's present syllables and keeps its
other glyphs, cell, baseline, metrics and flags, and the step of its syllables
(the JSET advance byte: 16 for these faces, 32 for the 46x54 and 36x36 DOTT
cells, even in a 26 px cell - the engine centres a syllable that steps less than
the game cell, so a 0 here would shift every line); the engine composes the same
pixels back (IoU 1.000, every present syllable exact, for all 15 faces; sizes
2,001,044 -> 366,742 B for DOTT, 621,962 -> 167,024 B for MI1, 498,506 ->
175,911 B for MI2 with their 24 px faces as neodgm; the MI1/MI2 L0, L4 and L7 faces
are since written from a DOS jamo set, below, and the L2 faces are a hinted NanumGothic Bold, neither converted:
a hinted 17 px TrueType face is not an OR of 8x4x4 pieces, jamoset --from-svf measures IoU 0.81 on M2L2). Run it after the L bakes above, with the same `$D`:

```sh
D=dists/engine-data/hires_text/dos
for f in DOTTL0 DOTTL1 DOTTL2 DOTTL3 DOTTL4 DOTTL7 M1L1 M2L1; do
  python3 tools/korean/jamoset.py --from-svf $D/$f.SVF --layout 844 -o /tmp/$f.jamo.SVF \
    --report /tmp/$f.json && mv /tmp/$f.jamo.SVF $D/$f.SVF
done
```

A compat jamo (U+3131..3163) the face draws whole stays whole: the engine reads the jamo set's piece table first, so
`jamoset.py` leaves those entries blank when the base has the glyph. `jamoset.py --layout 844 --blank-compat F.SVF -o OUT`
repairs a face converted before that (M1L1 had 42). `--from-ttf` fits only the syllables the font draws
(`--no-present-only` also fits `.notdef`), and `--advance` overrides the step in `--from-ttf` and `--from-svf`.

Each run prints `fit: N syllables, IoU 1.000 ... exact N`; a face that is not
exact stays precomposed (check the line before the `mv`). The U faces (2 bpp)
stay precomposed. The maps are unchanged: same face names.

### MI1 and MI2: the 24 dot faces (M1L0, M1L4, M2L0, M2L4)

The L preset's 24 px tall cells hold a real 24 dot DOS Hangul face, not neodgm's 16 px in a 24 cell:
M1L0 and M2L0 (cs0, 22x24 in the game: MI2's verb bar, MI1 Ultimate Talkie's through cs6) are Taebaek 3.5's
GOTHIC24 with its GASCII24, 1 px emboldened (the thin 1 px strokes are weak on the purple verb buttons);
M1L4, M2L4 (cs4 26x24, and cs7 24x24 through the same file) are Taebaek's MY24 with MASCII24, the serif of the
title and credit cards. They are jamo sets (28-38 KB, all 11,172 syllables, the original 8x4x4 pieces:
lossless), written by `jamoset.py --layout 844-hbios24` from the HBIOS 24x30 face's jamo half. The syllable
advance is 24 (JSET advance): the game steps a cs0 syllable 24 and a cs4 one 28 (the engine centres the glyph),
ASCII keeps the face's 12 px step on its baseline (the ascent, row 20: capitals end on row 19; the glyphs keep all 16 bits of the .PFT row, so MASCII24's right serifs in columns 12-15 are drawn), `--symbols` adds U+2026 on the
baseline and U+25A1. The faces carry no licence text (LICENCE NOT STATED, M12FONT.TXT). Dialogue (cs2, 18x18) and
the sentence line and verbs of MI1 (cs1/cs3, 16x16) are smaller cells. Run after the L bakes and the jamo
conversion above, from the Taebaek 3.5 `FNT/` folder:

```sh
T=tb351/FNT     # wherever the Taebaek 3.5 FNT folder is
for g in M1L0 M2L0; do python3 tools/korean/jamoset.py --layout 844-hbios24 $T/GOTHIC24.FNT --eng $T/GASCII24.PFT \
    --advance 24 --symbols --embolden --check -o $D/$g.SVF; done
for g in M1L4 M2L4; do python3 tools/korean/jamoset.py --layout 844-hbios24 $T/MY24.FNT --eng $T/MASCII24.PFT \
    --advance 24 --symbols --check -o $D/$g.SVF; done
```

M2KO.MAP's `card7` (clut8) names M2L4.SVF: the cs4 and cs7 cards share one 24x24 face, so M2L7.SVF is gone.
(`tools/korean/bdfface.py` bakes a precomposed face from a KS X 1001 BDF; no shipped face uses it now.)

### MI1 and MI2: the dialogue face (M1L2, M2L2)

Dialogue is charset 2, an 18x18 cell (korean02.fnt 9x9 doubled), so no 24 dot face can go there: a line steps
18 screen px and a syllable steps 20. M1L2/M2L2 are NanumGothic Bold (NHN's hinted original, OFL: OFLNANUM.TXT)
at 17 px drawn by FreeType's monochrome renderer with the font's hinting, `mkfont.py --mono-hint`
(Pillow `fontmode "1"`): the instructions in the font snap the stems to whole pixels, so the strokes are an even
width and nothing blobs (an anti-aliased bake cut at half coverage, and the unhinted `--from-ttf` render, do not
have that). The ink is at most 17x17 in the 18x18 cell, ascent 14, nothing clipped. The plan lines are in
`m1l.tsv` / `m2l.tsv` (the bakes above), so they come out of the L bake commands, precomposed (a jamo fit of the face
is IoU 0.81, so it is not converted); sizes 144 KB (MI1) and 85 KB (MI2). `FONTS` needs `nanum` (a link to the
fonts-nanum folder, e.g. `~/.local/sysroot/usr/share/fonts/truetype/nanum`).
Judged on real dialogue lines at 1x and 2x against neodgm: `runs/mi24/gallery18/` (25 TrueType faces hinted mono,
and the other renderers: light autohint, forced autohint, 16/18 px, for the best four).

Notes per game:

* The cells are 2x the game's `korean0N.fnt` header, as everywhere: MM/Zak
  charset 0 (the patch's one font), Loom cs1 16x16 and cs2 18x18, DOTT cs0 46x54,
  cs1 16x16, cs2 20x24, cs3/5/8 18x24, cs4 22x22, cs7 36x36. DOTT has no
  `korean06.fnt`, so `[font.6]` uses the cs0 face; cs5 and cs8 use cs3's SVF.
* L is neodgm at 16 px (32 px for the 46x54 and 36x36 DOTT cells, ascent
  12 + (cell h - 16) / 2 when the cell is taller than 16); U is NanumGothic Bold,
  and Gowun Batang Bold for DOTT's cs0 and cs7 (the big title and credit cells).
  `mkfont.py --fit-cell --cell H --width W --bpp 2 --chars-from FILE` prints the size and
  ascent that fit a cell.
* A bake whose text is the whole Hangul table has no `--require` (the `.trs`
  bakes have it), so the face's coverage shows as the count of dropped glyphs;
  all 2446 requested glyphs of each DOTT SVF are present.
* The maps carry no `[shadow]` section: the engine follows the game's shadow byte.
* `ENCODING.DAT` must sit in the same `extrapath` as the map. Without it the
  cp949 text cannot be decoded, every Hangul glyph falls back to the game's own bitmap
  font, and the dialogue then comes out half size (the overlay is 2x, the game font is
  drawn at 1x) while Latin still uses the SVF.
* SCUMM V2/V3 verb hit boxes: `CharsetRendererV3::printChar` halves the glyph height by
  `_textSurfaceMultiplier`, which shrank a Korean patch's verb boxes (MM, Zak, Loom use
  2-byte glyphs). The halving is skipped for Korean patch targets.

A plan line may have a seventh field, a `--limit` of its own that replaces
`ascii,ksx1001-nohanja` for that line (`<extra_limit>` is still appended). The
Indy4 bundle holds a few non-text records (map and puzzle tables, records 20
and 83) whose bytes decode to KS X 1001 symbols - U+0421, U+2160, U+30B3 among
them - that GowunBatang and neodgm lack; `--require` would refuse those faces
although no text ever draws the symbols. `i4u.tsv`'s credit line and every
`i4l.tsv` line therefore limit to `ascii,ksx1001-hangul,3131-318e` (Hangul
syllables, the compatibility jamo, ASCII), which is everything the text itself
uses. A line without the field bakes exactly as before.

A line whose `(ttf, size, bpp, cell, ascent)` was already baked earlier in
the same plan is copied from that earlier output rather than baked again -
MI1/MI2 commonly reuse one face at one size for several charsets (every UI
charset at the same cell, say), and re-running FreeType for an identical
result wastes time. Ascent is part of the key (`-` and an empty field count
as the same), so a line with an explicit ascent never reuses another line's
bake; so is a line's own limit.

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

### KQ1 (SCI remake): the Iyagi 16 px and HWP 1.20 24 px jamo sets (KQ1I16, KQ1H24)

KQ1KO.MAP draws the game's fonts with two jamo sets, the user's pairing selection (the same in every render_target, 1 bpp):
font 0 (status line, menu bar, parser line), 4 (title buttons) and any other id: `KQ1I16.SVF`, Iyagi IYG.KOR (san-1.0's
font-kor) 16 px with SAN.ENG, ascent 15; font 300 (message boxes, narration): `KQ1H24.SVF`, HWP 1.20 printer font
HANGUL3.PFT face 0 (24x24) with the same package's ASCII.PFT (14x24, advance 14, kept at its own rows: baseline row 20, so
ASCII sits above the Hangul baseline like HWP's), ascent 24. Font 300 needs `size=24` and `cell=glyph` in its section: the
engine's default layout cell is 16 px, and a 24 px face drawn in it lands 4 px off its pen (overlapped, sheared-looking
syllables). The text has no character outside ASCII + KS X 1001 (the bases are complete), so there is no supplement.
KQ1G16.SVF (GAM.KOR, the previous KQ1 face) stays: KQ4KO.MAP uses it. Licences: KQ1FONT.TXT (san is MIT; HWP's is not stated).

The HWP PFT is a 7/2/3-set font that kfont composes, so it is exported as a precomposed SVFN (ASCII + all 11,172 syllables;
`pairing2svf.svfn_bytes`) and then fitted to 844 pieces (IoU 1.000, exact for all 11,172):

```sh
D=dists/engine-data/hires_text/dos; B=~/work/johab-fonts/dos/raw/gh_drypot__san-1.0
python3 tools/korean/jamoset.py --layout 844 $B/font-kor/IYG.KOR --eng $B/font-eng/SAN.ENG --ascent 15 --symbols --check -o $D/KQ1I16.SVF
python3 - <<'PY'
import sys, os, base64
sys.path[:0] = [os.path.expanduser("~/work/johab-fonts/tools"), "tools/korean"]
import kfont, pairing2svf as p
R = os.path.expanduser("~/work/johab-fonts/printer/extracted/ia-HWP120.zip/")
kor, eng = kfont.open_font(R + "lzh/hangul.002.HANGUL3.PFT"), kfont.open_font(R + "ASCII.PFT")
def cell(g):
    d = bytearray()
    for r in g.rows[:24]:
        b = bytearray(3)
        for x in range(min(g.w, 24)):
            if r >> (g.w - 1 - x) & 1: b[x >> 3] |= 0x80 >> (x & 7)
        d += b
    return d + bytes(3 * (24 - len(g.rows)))
cps = list(range(0x20, 0x7f)) + [0xAC00 + i for i in range(11172)]
data = b"".join(bytes(cell((eng if cp < 0x80 else kor).glyph(cp))) for cp in cps)
adv = [14 if cp < 0x80 else 24 for cp in cps]
face = {"key": "hwp120-h3-f0", "w": 24, "h": 24, "bpp": 1, "ascent": 24, "adv": adv,
        "chars": "".join(map(chr, cps)), "data": base64.b64encode(data).decode()}
open("/tmp/hwp24.svf", "wb").write(p.svfn_bytes(face))
PY
python3 tools/korean/jamoset.py --layout 844 --from-svf /tmp/hwp24.svf --symbols --check -o $D/KQ1H24.SVF
```

### KQ4 (SCI0): san 16 px shared with KQ1, Mac AppleMyungjo 20 px (KQ4A20)

KQ4KO.MAP: fonts 0, 4 and any other id on `KQ1G16.SVF` (the KQ1 face, same 16 px cell); font 1 on `KQ4A20.SVF` with `size=20`,
`cell=glyph`. In the game font 1 is the default message font (parser replies, the "enter" prompt, inventory and other boxes);
font 4 shows only the first-run and copy-protection boxes. HAN24_1 is a precomposed KS X 1001 source (a jamo fit is IoU 0.815),
so KQ4H24 is a precomposed 1 bpp base (ASCII from ENG24_0 at 12 px + 2,350 syllables + U+25A1; 205 KB), written from the
pairing export `korean-hwp3.json` (key hwp3-7, `runs/fontmeasure/export/hwp3_export.py`) by `pairing2svf.svfn_bytes`:

```sh
python3 -c "
import sys, json; sys.path.insert(0, 'tools/korean'); import pairing2svf as p
f = [x for x in json.load(open('runs/fontmeasure/export/korean.json'))['fonts'] if x['key'] == 'mac-applemyungjo-20'][0]
open('dists/engine-data/hires_text/dos/KQ4A20.SVF', 'wb').write(p.svfn_bytes(f))"
```

The text (ko.tsv) has no character outside ASCII and the 2,350 syllables: no supplement; `facecoverage` (base_check) passes.
Licences: KQ4FONT.TXT.
