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

## Three engines and PLAY.EXE

The port builds three EXEs: SCI.EXE (the SCI engine, not SCI32), SCUMM.EXE
(SCUMM without the 7/8 and HE engines; links FLAC and Tremor) and AGI.EXE (the
AGI engine). All read SCUMMVM.INI, SAVES and DATA from the current directory
and write SCUMMVM.LOG and EXITLOG.TXT there, so started from their own folder
they need nothing else. AGI.EXE is the upstream AGI engine, unchanged: it runs
English AGI games only (the Korean AGI work is a later topic).

PLAY.EXE (`launcher/`, plain DJGPP, no ScummVM code) keeps those files per
game and lets the game folder sit on a read-only drive. Its own folder is
the home, and the current directory is the pack:

    D:\> C:\SCUMMVM\PLAY KQ1KO kq1kol

Home:  `C:\SCUMMVM\{SCI.EXE, SCUMM.EXE, AGI.EXE, PLAY.EXE, CWSDPMI.EXE, DATA\}`,
`DEFAULT.INI` and, per game, `C:\SCUMMVM\GAMES\ID\{SCUMMVM.INI, SAVES\,
SCUMMVM.LOG, EXITLOG.TXT, SOUND.INI}`.
Pack:  `GAMES\ID\` (the game's files) and `ID.INI` (a sample SCUMMVM.INI
without `path=`). Pack ids and targets are at most 8 characters (`kq1kol`,
`lb1ko`, `mi1ko`): a longer target name is cut by DOS in its save file names
(the save manager maps them back, see Memory and file names below, but two
targets that share their first 8 characters would share a save file).

PLAY makes the profile folder and copies `ID.INI` into it when there is no
SCUMMVM.INI yet (never over one), picks the EXE by the `engineid=` of the
target's own section (`agi` -> AGI.EXE; `scumm` -> SCUMM.EXE, then SCI.EXE;
else SCI.EXE, then SCUMM.EXE; the first `engineid=` of the file when the
target has none), runs it from the home with `--config`,
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

A Korean pack's `ID.INI` needs `language=ko` in the target (detection then picks the
`-ko` target of the translation), and only where the pack needs them
`hires_text_map=`, `render_target=` and `hires_text_whole_kb=` (see Korean text).

### Codecs (SCUMM.EXE only)

`backends/platform/dos/build-deps.sh codecs` builds libFLAC 1.4.3, libogg 1.3.5
and Tremor (xiph git 820fb32, integer Vorbis, chunk size patched) for DJGPP into
~/opt/codecs-dos (`DOS_CODECS` moves it); `build-dos.sh scumm` links them and
the staging step puts their licences next to SCUMM.EXE as FLAC.TXT and VORBIS.TXT.
SCI.EXE and AGI.EXE link no codec.

## Memory and file names

Measured in DOSBox-X (`machine=svga_s3`, `memsize=16`, so `phys_total` 14,896 KB, CWSDPMI 7, SB16), from the
backend's own `DOS: memory <phase>` log lines; the GUI is compiled in. Free = `dpmi_free`.

| EXE (stripped size) | Game, screen | `engine` | `first-frame` |
|---|---|---:|---:|
| SCI.EXE (5,413,376 B) | KQ1 Korean, 5-6-5, AA pair faces | 8,968 KB | 4,720 KB |
| | same, 1-5-5-5 | 8,968 KB | 4,724 KB |
| | same, 640x480 line repeat (Staging) | 8,968 KB | 4,624 KB |
| | same, 8 bpp, 1 bpp pair face | 8,968 KB | 5,740 KB |
| | same, `render_target=rgb888` (32 bit) | 8,968 KB | 3,224 KB |
| | same, 5-6-5, every face streamed (`hires_text_whole_kb=0`) | 8,968 KB | 4,344 KB |
| SCUMM.EXE (5,688,832 B) | Monkey Island 1 Korean, 320x200 8 bpp | 6,672 KB | 6,272 KB |
| AGI.EXE (3,940,864 B) | AGI Demo (English), 320x200 8 bpp | 10,400 KB | 8,748 KB |

PLAY.EXE is 279,040 B. Every EXE keeps more than 2 MB free at its measured worst
point with the GUI in (the gate for not carrying a GUI-less build: 2,048 KB); the
cheapest 32-bit case measured, 3,224 KB, is the one asked for with `rgb888`.
A 5-6-5 or 1-5-5-5 screen costs 1 MB less than 32 bit: the screen and its copy of the
frame are 2 bytes a pixel.

Faces (the `svf_stream_tables_size` of each file; a streamed face also holds a block cache of
64 slots x 1 KB, and a syllable cache of 64 entries per chain):

| Face | Kind | File | Streamed tables |
|---|---|---:|---:|
| NEODG16A (the engine's fallback) | 1 bpp jamo set | 16,408 B | 8,153 B |
| NG16J1 / NG16JA | 1 / 2 bpp jamo set | 20,064 / 24,960 B | 8,137 B |
| GBATB17 | 2 bpp precomposed, 17 px | 245,564 B | 50,492 B |
| GBATB23 | 2 bpp, 23 px | 338,948 B | 39,148 B |
| MARUS24A / MARUS24 | 2 / 1 bpp, 24 px | 107,508 / 87,956 B | 32,333 B |
| PAPLG18 | 2 bpp jamo, 18 px | 69,408 B | 29,341 B |

`hires_text_whole_kb` (default 64 on DOS, 256 elsewhere): a face file up to that many KB is
read whole, a larger one streamed. A face under 64 KB is cheaper whole (the KQ1 pair map:
4,716 KB free whole against 4,344 KB with every face streamed); a precomposed face of
100-340 KB is 30-50 KB of tables plus the 65 KB cache streamed. Each streamed face keeps its
file open (see `FILES=` above).

CWSDPMI swaps to `CWSDPMI.SWP` only when the BAT passes `-s` with a path (the optional line
of MI1KO.BAT: `SET SCUMMVM_SWAP=`); a 16 MB machine ran every measured game without swap.
Real HIMEM/EMM386 configurations and a swap file on a real disk are not measured.

File names are 8.3 on DOS. The engine's data (`data:hires_text/maps/korean-default.map`) is
staged by `stage-data.py` under the names DOS gives it (`DATA\HIRES_TE\MAPS\KOREAN-D.MAP`): it
refuses two sources that would get the same name. The EXEs start with long file names off
(`_CRT0_FLAG_NO_LFN`). Where DOS truncates names to 8.3 itself (MS-DOS, and DOSBox with
`lfn=false`), that is what finds the staged data; an LFN-capable DOS (DOSBox-X with `lfn=true`
and `ver=7.1`) does not truncate a long name it is given and does not find it. Saves of a target
longer than 8 characters are written under the cut name (`KQ1-RGB5.001`) and listed, loaded and
removed under the engine's own name.

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
