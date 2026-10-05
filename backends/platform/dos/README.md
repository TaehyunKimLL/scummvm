# ScummVM for MS-DOS

## Files open at once

ScummVM keeps some files open for the whole game, and DOS has room for only
as many open files as `FILES=` in CONFIG.SYS allows (shared with COMMAND.COM
and any resident programs):

- the game's data files (a SCUMM game's disk files, a SCI game's resource
  volumes and audio volumes);
- each hi-res SVF font larger than 64 KB of glyphs (read as it is drawn);
- a SCUMM translation bundle (`korean.trs` and the like).

A Korean MI2 with its hi-res map keeps about 10 files open. Put

    FILES=40

in CONFIG.SYS (DOSBox's default is enough). With too few, a game stops with
an error when it opens a room or a save file.

## Two engines and PLAY.EXE

The port builds two EXEs: SCI.EXE (the SCI engine) and SCUMM.EXE (SCUMM,
without the 7/8 and HE engines). Both read SCUMMVM.INI, SAVES and DATA from
the current directory and write SCUMMVM.LOG and EXITLOG.TXT there, so
started from their own folder they need nothing else.

PLAY.EXE (`launcher/`, plain DJGPP, no ScummVM code) keeps those files per
game and lets the game folder sit on a read-only drive. Its own folder is
the home, and the current directory is the pack:

    D:\> C:\SCUMMVM\PLAY MI1KO mi1kol

Home:  `C:\SCUMMVM\{SCI.EXE, SCUMM.EXE, CWSDPMI.EXE, PLAY.EXE, DATA\}` and, per
game, `C:\SCUMMVM\GAMES\ID\{SCUMMVM.INI, SAVES\, SCUMMVM.LOG, EXITLOG.TXT}`.
Pack:  `GAMES\ID\` (the game's files) and `ID.INI` (a sample SCUMMVM.INI
without `path=`).

PLAY makes the profile folder and copies `ID.INI` into it when there is no
SCUMMVM.INI yet (never over one), picks SCUMM.EXE or SCI.EXE by the
`engineid=` of the target's own section (the first `engineid=` of the file
when the target has none), runs it from the home with `--config`,
`--savepath` and `--path` naming the profile and the pack, and moves the
logs into the profile, also when the game ends on a signal. Further
arguments go to the engine; path options among them are made absolute
against the pack first. Run from the home folder, PLAY gives no `--path`
(the INI's own is used). When the profile INI lacks the target's section,
PLAY says so and does not merge the sample. DOS is back on the drive and
directory it was started from, and the home drive keeps its own current
directory, when PLAY ends. Messages name paths with backslashes.

- `PLAY --install ID`: only the profile folder and INI.
- `PLAY --sound=adlib|mt32|gm ID`: sets `music_driver`, `native_mt32` and
  `enable_gs` in the profile's `[scummvm]` section (section and key names in
  any case; the file is replaced whole, never half written).

Both refuse to run, and create nothing, when neither the profile INI nor
`ID.INI` in the current folder exists.

### Codecs (SCUMM.EXE only)

`backends/platform/dos/build-deps.sh codecs` builds libFLAC 1.4.3, libogg 1.3.5
and Tremor (xiph git 820fb32, integer Vorbis) for DJGPP into ~/opt/codecs-dos
(`DOS_CODECS` moves it); `build-dos.sh scumm` links them and stages their
licences as FLAC.TXT and VORBIS.TXT. `build-deps.sh host` builds flac and
metaflac for this machine into ~/opt/flac-host, for `mkute.py` and its tests.
SCI.EXE links no codec.

## Settings

In SCUMMVM.INI, `[scummvm]` or a game's section:

- `render_target=auto|clut8|rgb565|rgb888`: the screen hi-res text is drawn
  on. `auto` (the default) gives a true-colour game 16 bits (5-6-5: half the
  memory and bus bytes of 32 bits) when the card has 640x400 in 5-6-5; else
  32 bits when it has 640x400 in 32 bits (drawn straight into the screen,
  where 5-6-5 would need a 640x480 mode with the rows repeated, and a frame
  buffer for that); else 5-6-5 at 640x480. `rgb565` and `rgb888` ask for
  their own format whatever the card has at 640x400.
- `dos_vsync=off|wait`: wait for the vertical retrace before sending a frame.
- `dos_force_fallback=true`: use the 640x480 line-repeat mode even when the
  card has 640x400.
- `dos_frame_buffer=true`: keep the game's frame in a buffer of its own,
  copied into the screen surface, instead of drawing it into the screen
  surface itself (1 MB more at 640x400 true colour). For a card or driver
  on which the default draws wrongly.
