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

## Settings

In SCUMMVM.INI, `[scummvm]` or a game's section:

- `render_target=auto|clut8|rgb565|rgb888`: the screen hi-res text is drawn
  on. `auto` (the default) gives a true-colour game a 5-6-5 screen (half the
  memory and bus bytes of `rgb888`); a card without 640x400 in 5-6-5 uses its
  640x480 mode with the rows repeated. `rgb888` asks for 32 bits.
- `dos_vsync=off|wait`: wait for the vertical retrace before sending a frame.
- `dos_force_fallback=true`: use the 640x480 line-repeat mode even when the
  card has 640x400.
- `dos_frame_buffer=true`: keep the game's frame in a buffer of its own,
  copied into the screen surface, instead of drawing it into the screen
  surface itself (1 MB more at 640x400 true colour). For a card or driver
  on which the default draws wrongly.
