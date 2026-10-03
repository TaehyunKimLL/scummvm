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

## Where it reads and writes

The folder of the EXE is the program's own: SCUMMVM.INI, SAVES, DATA,
SCUMMVM.LOG and EXITLOG.TXT are found and written there, whatever the
current directory is when it is started. Only the game's folder is looked
for from the current directory, and a path on the command line is made
absolute against it first, so

    D:\> C:\SCUMMVM\SCUMMVM.EXE --path=GAMES\LB2KO lb2ko

runs a game from a read-only drive D: while everything is written to C:.
`path=` in a section of SCUMMVM.INI stays relative to the folder of the EXE.
DOS is back on the drive and directory it was started from when the program
ends. Started from its own folder, nothing changes.

`SCUMMVM --sound=adlib|mt32|gm` (or `--sound adlib`) sets the music output
in the `[scummvm]` section of SCUMMVM.INI (`music_driver`, `native_mt32`,
`enable_gs`) and exits.

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
