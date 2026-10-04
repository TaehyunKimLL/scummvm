# MI1 / MI2 Ultimate Talkie Edition on the DOS port: design

Date: 2026-10-04. Status: draft for user review.

## Goal

Make the Ultimate Talkie Edition (UTE) of The Secret of Monkey Island 1 and 2 work properly in `SCUMM.EXE` on the DOS port (Pentium-class, about 16 MB RAM): speech, music variants, subtitles (including the Korean translation), and a correct audio mix.

## Starting point

- The engine already detects both UTEs, and they boot today without speech. No engine change is needed to find or play the speech files.
- `build-dos.sh` passes `--disable-vorbis --disable-tremor --disable-flac --disable-mad`, so the DOS build has no compressed-audio codec. DOTT voice works only because its `MONSTER.SOU` is uncompressed VOC.
- Speech formats: MI1 `monkey.sof` is FLAC (469 MB, 44.1 kHz mono). MI2 `monkey2.sog` is Ogg Vorbis (135 MB, about 48 kHz mono).
- Music: the MI1 "Midi Music" variant still uses FLAC ambient tracks 25-29 (44.1 kHz stereo). The Original CD (651 MB) and SE CD (657 MB) variants are FLAC. MI2 has no CD tracks.
- SDL3 threads on DOS are cooperative: they yield only in `SDL_Delay` and the event pump.
- The `spike-flac` branch measured FLAC speech at 2-3% CPU, +142 KB EXE, and showed that decoding must happen outside the interrupts-off mixer lock.

Figures above come from the research report of 2026-10-04 and are to be re-checked during S0 and S1.

## Decisions (made with the user)

1. Codecs: FLAC (libFLAC) and Ogg Vorbis (Tremor, with libogg) are both built into `SCUMM.EXE`. MI2's `.sog` is used as shipped, with no transcoding. `SCI.EXE` is unchanged.
2. Music: all three MI1 variants are in scope: Midi Music (with ambient tracks), Original CD Tracks, SE CD Tracks.
3. Distribution: no speech or music data is distributed. A host script builds the pack from the user's own UTE files.
4. Order: MI1 first (English and Korean targets), then MI2, then the CD-track variants.
5. MPXPlay is rejected: it cannot run next to ScummVM under single-tasking DOS, and its source license adds restrictions that could not be fully verified.

## Staged work

- **S0 Codecs.** Make libFLAC, libogg and Tremor build reproducibly from `build-dos.sh` or a deps script (the Tremor build exists only in a scratchpad today). Enable `USE_FLAC` and `USE_TREMOR` for the SCUMM build only. Expected EXE growth: about +142 KB (FLAC), about +115 KB (Tremor).
- **S1 Mixer.** Per-stream decode-ahead outside the interrupts-off lock; 4096-frame buffers (92.9 ms IRQs, 32 KB DMA buffer, 64 KB conventional memory including the page-crossing guard). Open, seek and read the first block of a speech line on the main thread in `startTalkSound`; defer stream teardown out of the mutex. Because SDL threads are cooperative, prefetch is driven from the engine loop or a timer callback; the plan decides which after reading the spike. SCI regression runs are required.
- **S2 Lip sync.** Mouth movement follows a timer started at `startTalkSound`, not the audio position, so voice can lag the mouth by the queued audio (about 0.4 s at 4096 frames). Measure first; if the offset is over about 150 ms, add an engine hook that offsets the speech timer by the mixer's queued time.
- **S3 MI1 UTE pack.** Host script: 8.3 renames, 1 s seek points in the tracks, `TRACK1.WAV`, targets for MI1, MI1KO and MI1KOL on UTE data with the UTE `korean.trs` (copy it from `/tmp/ScummVM-Kor-Trs` into gamedata first), and the voice INI.
- **S4 MI2 speech.** Wire `monkey2.sog` through Tremor and measure the unmeasured mono 48 kHz CPU early (estimate: 20-30% at P75 while a line plays).
- **S5 CD-track variants.** Original CD and SE CD FLAC tracks with prefetch and 1 s seek points. This is the CPU worst case (about 37-43% at P75/P60 with speech).
- **S6 Hardware pass** on 86Box or real hardware.

## Acceptance

- Every talk line in MI1 and MI2 plays, with no "SFX file not found" or "did not find sound" warnings.
- Voice plus subtitles, voice only and text only all work, from the INI and with Ctrl+T.
- Korean subtitles play over the English voice.
- After startup at P75 (40000 cycles) with 4096 frames: 0 underruns, clock at least 99.5%, at least 9.5 loops/s, swap 0.
- Mouth-to-voice offset under about 150 ms (to be measured).
- With the speech file absent, the game still runs text-only.

## Testing

- Linux unit tests in `builds/linux-dos-test-scumm` for pure helpers (prefetch ring with a fake stream, seek-point and rate math, host-script index checks).
- DOSBox-X headless runs with underrun, clock and loop-rate counters, driven through the debug socket, with recordings analysed for clip order and spacing (within 0.5 s).
- 86Box or real hardware only: true Pentium CPU cost, IDE/CF and FAT behaviour on 400+ MB files, SB Pro 8-bit conversion cost, OPL write timing, conventional memory with PLAY.EXE resident.

## Risks

- Lip-sync lag from deep buffers (S2).
- A per-line `open()` and far `lseek` into a 469 MB file on real DOS (FAT16 cluster walk). Speech must not run from CD-ROM (100-200 ms seeks).
- Tremor CPU for MI2 speech is an estimate.
- Redistributing game-derived data is not done; users supply their own UTE files.
- Scratchpad-only artefacts (`tremor-dos`, the spike worktree) and `/tmp/ScummVM-Kor-Trs` can disappear.
