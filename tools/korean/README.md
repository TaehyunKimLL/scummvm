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
