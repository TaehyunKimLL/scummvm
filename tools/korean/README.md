# Korean hi-res text: font tools

Scripts for preparing replacement fonts for the Korean fan translations of the
SCUMM games, and for checking that the engine is really using them.

Everything here runs on stock Debian with `python3-pil` and `fonts-nanum`; no
part of the engine needs these at runtime.

## The one thing that decides everything: cell size

The game lays text out on **its own** font's grid. Indy3 reports
`_2byteWidth = 8` for a Hangul syllable, so each character gets 8 game pixels
and `8 * scale` on screen. A replacement font whose cell is wider than that is
overlapped by the next character; narrower, and the text has gaps.

    cell = the game font's width x scale

That is not a preference, and getting it wrong looks exactly like a bug in the
rendering code. A 24px font at scale 2 has 16 pixels of room and loses 8.

## Quick start

```sh
# What does this game need?
python3 tools/korean/fontplan.py /path/to/gamedir

# Bake it, at scale 2 (or 3)
tools/korean/bakecells.sh /path/to/gamedir 2

# Write the map the engine reads
python3 tools/korean/makemaps.py /path/to/gamedir
```

Then point the target at the map, in `~/.config/scummvm/scummvm.ini`:

```ini
[my-korean-game]
hires_text_map=/path/to/gamedir/hires_text.map
```

and make sure `encoding.dat` can be found, or every Korean character silently
becomes U+FFFD:

```ini
[scummvm]
extrapath=/path/to/scummvm/dists/engine-data
```

## The scripts

| script | what it does |
|---|---|
| `fontplan.py` | reads the game's own fonts and prints which cells are needed, per scale |
| `bakecells.sh` | bakes a replacement for each charset at the right cell, plus a Latin companion |
| `mkfont.py` | TrueType to SVFN; `bakecells.sh` drives it, but it is usable directly |
| `makemaps.py` | writes `hires_text.map` from whatever fonts are present |
| `fontcheck.sh` | runs a target and reports which font system actually drew the text |
| `textlog.sh` | logs each run of text with the font that drew it, while you play |
| `fontmetrics.py` | dumps a font's cell, advances and ink widths |
| `advancecheck.py` | how many glyphs would collide at a given scale |
| `carrycheck.py` | spacing error from rounding advances up versus carrying the remainder |
| `inifix.py` | patches a throwaway ini for a test run |

## Things that cost hours to work out

**A CJK face reports one advance for every syllable.** They are designed on a
square em, so NanumGothic says 15.05 for 가, 이 and 무 alike - while their ink
is 14, 12 and 15 wide. Baking `--variable` from those advances gives a font
that is proportional in name only. `mkfont.py --ink-advance` measures the ink
instead, which is what `bakecells.sh` uses.

**Fonts are not always square.** MI2's `korean00.fnt` is 11x12 and The Dig's
`korean.fnt` is 10x9. The width sets the advance and the height sets the line
box, so a square replacement either clips the glyph or overruns the line.

**`;` is not a comment in a ScummVM ini.** `scale=3   ; because` parses as the
literal string `"3   ; because"`, fails the integer check, and is silently
dropped - the map then looks like it was ignored.

**Charset 6 is a broken resource in MI1 CD, MI2 and DOTT.** It has 123
characters where the others have 256, and the engine remaps it to font 0
(`charset.cpp`, "HACK: Fix monkey1cd/monkey2/dott font error"). A charset-6
replacement therefore has to be baked at font 0's cell, not at charset 6's own
nominal size. `bakecells.sh` does this.

**Some games use one font, not several.** `loadKorFont()` takes the multi-font
path only for `version < 7` (plus Full Throttle); The Dig loads a single
`korean.fnt`. `makemaps.py` writes `single=` rather than `multi=` for those.
