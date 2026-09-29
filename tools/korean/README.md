# Korean hi-res text: tools

Scripts for preparing replacement fonts for the Korean fan translations of the
SCUMM games, and for checking that what reaches the screen is what was
intended.

Everything runs on Linux with Python 3 and Pillow. Baking needs FreeType
through Pillow; the engine does not.

## Preparing fonts

```sh
tools/korean/fontplan.py   ~/games/mi2kor          # what cells are needed
tools/korean/bakecells.sh  ~/games/mi2kor 2        # bake them
tools/korean/makemaps.py   ~/games/mi2kor          # write hires_text.map
```

### The cell size is decided by the game

The game lays text out on its own font's grid, so a replacement only fits
when

    cell = the original korean<NN>.fnt height * scale

Exceed it and glyphs overlap - Indy3 gives an 8px font a 16px box at 2x, and
a 24px replacement overlapped by 8. The header is
`[version, shadow, width, height]` and width and height are independent: MI2's
`korean00.fnt` is 11x12, The Dig's `korean.fnt` is 10x9. A square cell either
clips the glyph or overruns the line.

### v0-v2 need a full-width Latin face

`CharsetRendererV2::getCharWidth()` returns a hard-coded 8, so every
character - Latin included - occupies one 8px cell. A Korean face's Latin is
half-width with padding, so it fills about half the cell and the line looks
thin and gappy. `bakecells.sh` detects these games (one `korean00.fnt`, 8x8)
and switches to `unifont_jp`, rendered at twice the cell so its 16-unit
advance lands on the grid.

### Every syllable, and other scripts: `--unicode`

A code-page bake (`--codepage 949`, the default) holds only the 2350
syllables of KS X 1001, so a UTF-8 translation that writes 똠 or 뷁 falls back
to another face for them. `mkfont.py --unicode <list>` bakes a version 2 SVFN
(a code point table, FONT_FORMAT.md) in the order given; the list takes hex
ranges and names (`ascii`, `latin1`, `hangul` = all 11172, `jamo`,
`cjk-punct`, `ksx1001` = the whole of KS X 1001 with its 4888 Hanja, `kana`,
`thai`). Code points the face does not draw (no ink, or its .notdef box) are
left out, so the `[font.N] bitmap=` loader falls back for them. A pixel font
baked at its design size is its own bitmap exactly:

```sh
tools/korean/mkfont.py Galmuri7.ttf galmuri7-8px.fnt --size 8 --cell 9 --bpp 1 \
    --unicode ascii,hangul,jamo,cjk-punct
```

The glyph count field is 16 bits: at most 65535 glyphs per file.

Besides the 11172-syllable `hangul`/`cp949-hangul` and the 2350+4888
`ksx1001`, three narrower named sets split KS X 1001 for a per-game subset
bake: `ksx1001-hangul` (just the 2350 syllables), `ksx1001-symbols`
(everything else in `ksx1001-nohanja` - jamo, punctuation, Latin/Greek/Cyrillic
rows, circled and parenthesized forms - with no Hangul and no Hanja), and
`ksx1001-nohanja` (`ksx1001-hangul` + `ksx1001-symbols`, i.e. `ksx1001` minus
its 4888 Hanja).

### Only what a game uses: `--chars-from`, `--limit`

A shared font (`bake-dos-fonts.sh`'s `KO2350.SVF`/`KOCP949.SVF`) has to cover
every game; a single game's translation uses far fewer code points, and a
per-game bake of just those is smaller. `--chars-from <file>...` collects
every code point a game's translation actually writes and unions it into
`--unicode` (a bare `--chars-from` with no `--unicode` works too, same as
`--unicode ""`). ASCII and U+25A1 (the box `[hires] missing=` draws for a
character no font has, FONT_FORMAT.md) are always in the result, with or
without `--chars-from`.

The file format is told apart by name, not content:

- `TEXT.nnn`/`text.nnn` - a SCI TEXT resource patch (a UTF-8 fan translation
  replaces the game's own TEXT resources this way): a 2-byte header (`type`,
  then how many more header bytes follow - 0 for TEXT, so 2 bytes in
  practice, `resource.h`'s `kResourceHeaderSize` and
  `ResourceManager::processPatch()`), then NUL-separated UTF-8 strings,
  indexed like a real TEXT resource.
- `*.str`/`sci-ko.str` - the script-string manifest
  (`engines/sci/engine/translation.h`/`.cpp`): `script<TAB>id[<TAB>room]<TAB>text`,
  UTF-8, `#` whole-line comments.
- `*.map` - a `hires_text.map`/per-game `.MAP` (`graphics/hires_text/font_map.cpp`
  INI): the `[hires] missing=` code point and any `[glyphs]` entry whose value
  is an absolute code point (not `keep`, not a `+n` range offset, which adds
  no fixed code of its own).
- anything else - read whole as UTF-8 text.

A glob (`TEXT.*`) not already expanded by the shell is expanded here too. A
`file:<path>` item inside `--unicode` does the same single-file collection
inline, so it can be mixed with named sets in one list.

`--limit <list>` (same syntax as `--unicode`) intersects the result with a
character set - typically a code page the L preset's face can actually
represent. Combined with `--chars-from` this bakes only the game's characters
that also fit that set:

```sh
# L preset: this game's characters that KS X 1001 (no Hanja) can draw.
tools/korean/mkfont.py neodgm.ttf kq1kol.fnt --size 16 --cell 16 --bpp 1 \
    --chars-from KQ1KO/TEXT.* KQ1KO/SCI-KO.STR --limit ascii,ksx1001-nohanja

# U preset: this game's characters that cp949 (all 11172 syllables) can draw.
tools/korean/mkfont.py NanumGothic-Bold.ttf kq1kou.fnt --size 18 --bpp 2 \
    --chars-from KQ1KO/TEXT.* KQ1KO/SCI-KO.STR --limit ascii,cp949

# Every KS X 1001 syllable regardless of what any game uses (a shared font).
tools/korean/mkfont.py neodgm.ttf ko2350.fnt --size 16 --cell 16 --bpp 1 \
    --unicode ascii,ksx1001-hangul
```

A character the game uses but `--limit` excludes is left out and would show
as the `missing=` box (□) with no fallback face behind this one; `mkfont.py`
prints how many and which, and `--fail-on-drop` turns that into a non-zero
exit instead of a warning - useful in a script that must notice when a
translation starts using a syllable outside KS X 1001. ASCII and U+25A1
survive `--limit` even when the given sets do not name them.

`--require` is the opposite check: it fails (and writes no file) if any
collected code point - after `--limit`, so an intentional drop does not also
trip this - has no glyph in the source TTF at all (drawn as its `.notdef`
box). Where `--fail-on-drop` catches "outside the character set this face is
supposed to cover", `--require` catches "this TTF is missing something it was
expected to have".

### Hangul is proportional too

CJK faces report one advance per syllable because they are drawn on a square
em - NanumGothic says 15.05 for 가, 이 and 무 alike - while their ink is 14,
12 and 15 wide. `--variable` alone therefore bakes a font that is
proportional in name only, and one pixel too narrow everywhere, so syllables
touch. `--ink-advance` measures the ink instead.

## Checking the result

```sh
tools/korean/fontcheck.sh mi2-svfn-hr      # which font system drew this?
tools/korean/textlog.sh   mi2-svfn-hr 1 30 # what text, in which font?
tools/korean/coverrun.sh  mi2-svfn-hr      # which fonts are never used?
```

`fontcheck.sh` matters because the legacy `loadKorFont()` runs for every
Korean target whatever the hi-res settings say. Two systems can be live at
once and a fault may belong to either; the giveaway is a scale with zero
hi-res fonts loaded.

Loading a font is not using it: MI2 loads nine and draws with four, so the
rest are untested until some scene nobody captured reaches them.

## Comparing against the original

The control is the game's **own** rendering, not an earlier build of this
feature - that only shows which build regressed, never whether either is
correct.

```sh
tools/korean/findscene.sh mi2-svfn-hr      # which save actually shows text?
tools/korean/sceneab.sh   mi2-svfn-hr 2    # capture that scene both ways
tools/korean/abrank.py    /tmp/sab_mi2-svfn-hr
```

`hires_text_scale=1` does **not** turn the feature off: the map in the game
folder is still found, the replacement fonts still load, and the glyphs are
drawn at 1x and magnified by the backend. The capture then shows big
anti-aliased text that is ours, not the game's. `shot.sh NOHIRES=1` and
`sceneab.sh` point the game at a copy of the folder with the map and the
baked fonts removed.

Most saves sit on a cutscene and draw nothing, which reads as a broken
renderer when the game simply had nothing to say - Loom's slots 0-2 were like
that and only slot 5 spoke. `findscene.sh` asks the engine rather than
guessing.

`abrank.py` ranks frame pairs by where they differ. A pair differing only in
the text band is evidence; one differing everywhere caught the two runs at
different moments, which happens whenever a character is walking.

## Regression against another build

```sh
tools/korean/fbdump.sh    /tmp/out target 600 ./scummvm
tools/korean/fbcompare.sh ./old ./new 600 ja-mi2 ja-zak en-dig
tools/korean/noisefloor.sh ./new ./new 600 3 en-dig
```

These dump the engine's own surfaces through GDB at a chosen frame, which is
reproducible in a way that timed screenshots are not.

Measure the noise floor before believing a difference: The Dig plays a SMUSH
cutscene at start-up whose timeline the frame counter does not pin, so the
same binary against itself differs by 0 to 3.3 kB between runs.

## Pitfalls

1. `Common::INIFile` does not treat `;` as a comment - `scale=3  ; why` is
   read as the string `"3  ; why"` and silently ignored.
2. SDL takes the X11 window class from `argv[0]`, so a binary copied to
   `/tmp/svm-work` has class `svm-work` and a search for `scummvm` finds
   nothing. `xvfb.sh` looks up the first mapped child of the root instead.
3. `extrapath` must point at `dists/engine-data` or `encoding.dat` is not
   found and every Korean character decodes to U+FFFD - which looks like a
   font bug rather than a configuration one.
4. Xvfb needs to be at least as large as the window; a 3x window is 960x600
   and a smaller server drops the game to a scaled mode.
5. `autosave_period` defaults to 20 seconds and overwrites slot 0 mid-run.
   `inifix.py` forces it to 0.
6. A fixed X display number or a fixed temp file lets two runs capture each
   other's output. `xvfb.sh` allocates a free display; use `mktemp` for
   anything written per run.
