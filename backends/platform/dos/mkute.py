#!/usr/bin/env python3
"""Make the DOS pack of The Secret of Monkey Island, Ultimate Talkie Edition
("Midi Music" build), for SCUMM.EXE, from your own copy of the game.

  python3 mkute.py UTE_DIR OUT_DIR [--korean KOREAN_DIR] [--metaflac PATH]
                   [--flac PATH] [--test-clips]

UTE_DIR     the "Ultimate Talkie Version with Midi Music" folder: monkey.000,
            monkey.001, monkey.sof, track1.flac, track25.flac ... track29.flac
KOREAN_DIR  optional: the ScummVM Kor. Project's translation of this edition
            (korean.trs, korean00.fnt ... korean04.fnt); adds the Korean targets
OUT_DIR     gets MI1UTE\\: GAMES\\MI1UTE\\ (the game), MI1UTE.INI and MI1.BAT
            (with --korean also MI1KO.BAT, MI1KOL.BAT). Copy MI1UTE\\ to the
            DOS machine (not to a CD: speech seeks need a hard disk) and run a
            BAT from it.

It copies the files under 8.3 names (track25.flac -> TRACK25.FLA), stores
track1.flac, which is a WAV in this build, as TRACK1.WAV, adds a seek point
every second to TRACK25-29.FLA with metaflac (without them a seek stalls the
game for seconds on DOS), checks MONKEY.SOF's clip index (--test-clips also
decodes every clip with flac), and writes the INI and the BATs. Your files are
not changed. metaflac and flac come with FLAC (https://xiph.org/flac/).
"""
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys

PACK = "MI1UTE"
UTE_FILES = ["monkey.000", "monkey.001", "monkey.sof", "track1.flac"] + ["track%d.flac" % n for n in range(25, 30)]
KOREAN_FILES = ["korean.trs"] + ["korean%02d.fnt" % n for n in range(5)]
HOME_DEFAULT = "C:\\SCUMMVM"
_POINT = re.compile(r"^\s*point \d+: sample_number=(\d+)", re.M)
_PLACEHOLDER = 0xFFFFFFFFFFFFFFFF


class PackError(Exception):
    pass


def find(folder, name):
    """The file in folder called name in any letter case, or None."""
    for f in os.listdir(folder):
        p = os.path.join(folder, f)
        if f.lower() == name.lower() and os.path.isfile(p):
            return p
    return None


def dos_name(name):
    """The 8.3 upper-case name in the pack."""
    if name.lower() == "track1.flac":
        return "TRACK1.WAV"
    base, ext = os.path.splitext(name)
    return (base[:8] + ext[:4]).upper()


def is_wav(path):
    with open(path, "rb") as f:
        head = f.read(12)
    return head[:4] == b"RIFF" and head[8:12] == b"WAVE"


def run(cmd, data=None):
    try:
        r = subprocess.run(cmd, input=data, capture_output=True, check=False)
    except OSError as e:
        raise PackError("cannot run %s: %s" % (cmd[0], e))
    if r.returncode:
        raise PackError("%s failed: %s" % (" ".join(cmd[:2]), r.stderr.decode("utf-8", "replace").strip()))
    return r.stdout.decode("utf-8", "replace")


def check_sof(path):
    """MONKEY.SOF's clip index as SCUMM reads it (sound.cpp setupSfxFile and
    startTalkSound): a big-endian index size, 16 bytes a clip (original
    offset, offset after the index, tag bytes, FLAC bytes), sorted by original
    offset (the engine bsearches it); each clip's data, after its tags, is FLAC
    and ends inside the file. Returns [(start, size)] of every clip's FLAC data."""
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        head = f.read(4)
        if len(head) < 4:
            raise PackError("%s: too short for a clip index" % path)
        n = struct.unpack(">I", head)[0]
        if n == 0 or n % 16 or n + 4 > size:
            raise PackError("%s: index size %d is not a multiple of 16 inside the file" % (path, n))
        idx = f.read(n)
        clips = []
        last = -1
        for i in range(0, n, 16):
            org, new, tags, csize = struct.unpack(">IIII", idx[i:i + 16])
            if org <= last:
                raise PackError("%s: clip %d is out of order (original offset %d after %d)" % (path, i // 16, org, last))
            last = org
            start = new + n + 4 + tags
            if start + csize > size:
                raise PackError("%s: clip %d (original offset %d) ends past the file" % (path, i // 16, org))
            f.seek(start)
            if f.read(4) != b"fLaC":
                raise PackError("%s: clip %d (original offset %d) is not FLAC" % (path, i // 16, org))
            clips.append((start, csize))
    return clips


def decode_clips(flac, path, clips, log):
    with open(path, "rb") as f:
        for i, (start, csize) in enumerate(clips):
            f.seek(start)
            run([flac, "-t", "-s", "-"], f.read(csize))
            if (i + 1) % 500 == 0:
                log("  %d/%d clips decoded" % (i + 1, len(clips)))
    log("MONKEY.SOF: all %d clips decode" % len(clips))


def seek_points(metaflac, path):
    out = run([metaflac, "--list", "--block-type=SEEKTABLE", path])
    return [int(s) for s in _POINT.findall(out) if int(s) != _PLACEHOLDER]


def add_seekpoints(metaflac, path):
    """A seek point every second; at least one per whole second of audio after."""
    run([metaflac, "--add-seekpoint=1s", path])
    total = int(run([metaflac, "--show-total-samples", path]).strip())
    rate = int(run([metaflac, "--show-sample-rate", path]).strip())
    pts = seek_points(metaflac, path)
    if rate <= 0 or len(pts) < total // rate:
        raise PackError("%s: %d seek points for %d s of audio" % (path, len(pts), total // max(rate, 1)))
    return len(pts)


def game_bat(game_id, target):
    """The game BAT of the DOS packs (the harness's pack_common.game_bat):
    PLAY.EXE through SCUMMVM_HOME (default C:\\SCUMMVM), the swap file from
    SCUMMVM_SWAP. Plain MS-DOS 5 commands."""
    lines = [
        "@echo off",
        "set SVH=%SCUMMVM_HOME%",
        'if "%SVH%"=="" set SVH=' + HOME_DEFAULT,
        "for %%h in (%SVH%) do set SVH=%%h",
        "if exist %SVH%\\PLAY.EXE goto run",
        "echo PLAY.EXE is not in %SVH%.",
        "echo Unzip the engine zip there, or SET SCUMMVM_HOME to the folder that has it.",
        "goto end",
        ":run",
        "echo Loading ScummVM...",
        'if not "%SCUMMVM_SWAP%"=="" %SVH%\\CWSDPMI -s%SCUMMVM_SWAP%\\CWSDPMI.SWP',
        "%SVH%\\PLAY " + game_id + " " + target,
        ":end",
        "set SVH=",
    ]
    return "\r\n".join(lines) + "\r\n"


def targets(korean):
    t = [("mi1", "MI1", "The Secret of Monkey Island (Ultimate Talkie, English)", "en", None, None)]
    if korean:
        t += [("mi1ko", "MI1KO", "The Secret of Monkey Island (Ultimate Talkie, Korean, anti-aliased)",
               "ko", "data:M1KO.MAP", None),
              ("mi1kol", "MI1KOL", "The Secret of Monkey Island (Ultimate Talkie, Korean, 8-bit font)",
               "ko", "data:M1KO.MAP", "clut8")]
    return t


def ini_text(korean):
    """The pack's INI (PLAY copies it to the profile once), CRLF, no path=."""
    bats = ", ".join("%s.BAT" % b for _, b, _, _, _, _ in targets(korean))
    out = ["# Ready-to-run configuration: The Secret of Monkey Island, Ultimate Talkie",
           "# Edition (Midi Music build), for SCUMM.EXE. Written by MKUTE.PY.",
           "# Started by %s. PLAY copies this file to" % bats,
           "# C:\\SCUMMVM\\GAMES\\MI1UTE\\SCUMMVM.INI once and gives the game directory",
           "# itself, so there is no path= line.",
           "",
           "[scummvm]",
           "lastselectedgame=mi1",
           "music_driver=adlib",
           ""]
    for name, _, desc, lang, mp, rt in targets(korean):
        out += ["[%s]" % name, "engineid=scumm", "gameid=monkey", "description=%s" % desc,
                "language=%s" % lang, "platform=pc",
                "# Voice and subtitles. Ctrl+T in the game cycles voice and text, text only,",
                "# voice only.",
                "subtitles=true", "speech_mute=false"]
        if mp:
            out += ["extrapath=DATA", "hires_text_map=%s" % mp]
        if rt:
            out.append("render_target=%s" % rt)
        out.append("")
    return "\r\n".join(out)


def make_pack(ute, out, korean=None, metaflac="metaflac", flac="flac", test_clips=False, log=print):
    missing = [n for n in UTE_FILES if not find(ute, n)]
    if missing:
        raise PackError('%s lacks %s (the UTE "Midi Music" folder has them)' % (ute, ", ".join(missing)))
    if korean:
        missing = [n for n in KOREAN_FILES if not find(korean, n)]
        if missing:
            raise PackError("%s lacks %s" % (korean, ", ".join(missing)))
    if not is_wav(find(ute, "track1.flac")):
        raise PackError('%s is not a WAV file: is this the "Midi Music" build?' % find(ute, "track1.flac"))
    run([metaflac, "--version"])
    clips = check_sof(find(ute, "monkey.sof"))
    log("MONKEY.SOF: %d clips, index in order, every clip FLAC" % len(clips))
    if test_clips:
        run([flac, "--version"])
        decode_clips(flac, find(ute, "monkey.sof"), clips, log)
    pack = os.path.join(out, PACK)
    game = os.path.join(pack, "GAMES", PACK)
    if os.path.exists(pack):
        shutil.rmtree(pack)
    os.makedirs(game)
    for n in UTE_FILES + (KOREAN_FILES if korean else []):
        src = find(korean if n in KOREAN_FILES else ute, n)
        dst = os.path.join(game, dos_name(n))
        shutil.copyfile(src, dst)
        if n.startswith("track") and n != "track1.flac":
            log("%s: %d seek points" % (dos_name(n), add_seekpoints(metaflac, dst)))
    with open(os.path.join(pack, PACK + ".INI"), "wb") as f:
        f.write(ini_text(bool(korean)).encode("ascii"))
    for target, bat, _, _, _, _ in targets(bool(korean)):
        with open(os.path.join(pack, bat + ".BAT"), "wb") as f:
            f.write(game_bat(PACK, target).encode("ascii"))
    log("pack: %s" % pack)
    return pack


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("ute")
    ap.add_argument("out")
    ap.add_argument("--korean")
    ap.add_argument("--metaflac", default="metaflac")
    ap.add_argument("--flac", default="flac")
    ap.add_argument("--test-clips", action="store_true")
    a = ap.parse_args(argv)
    try:
        make_pack(a.ute, a.out, a.korean, a.metaflac, a.flac, a.test_clips)
    except PackError as e:
        print("mkute: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
