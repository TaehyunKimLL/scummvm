#!/usr/bin/env python3
"""Make the DOS pack of The Secret of Monkey Island (--game mi1, the default) or
Monkey Island 2: LeChuck's Revenge (--game mi2), Ultimate Talkie Edition, for
SCUMM.EXE, from your own copy of the game.

  python3 mkute.py UTE_DIR OUT_DIR [--game mi1|mi2] [--korean KOREAN_DIR]
                   [--metaflac PATH] [--flac PATH] [--tremor-check PATH]
                   [--test-clips]

UTE_DIR     mi1: the "Ultimate Talkie Version with Midi Music" folder: monkey.000,
            monkey.001, monkey.sof, track1.flac, track25.flac ... track29.flac
            mi2: the Monkey Island 2 folder: monkey2.000, monkey2.001, monkey2.sog
KOREAN_DIR  optional: the ScummVM Kor. Project's translation of this edition
            (korean.trs, korean00.fnt ... korean04.fnt; mi2: korean00 ... 05,
            07, 08); adds the Korean targets
OUT_DIR     gets MI1UTE\\ or MI2UTE\\: GAMES\\<pack>\\ (the game), <pack>.INI and
            MI1.BAT / MI2.BAT (with --korean also MI?KO.BAT, MI?KOL.BAT). Copy the
            folder to the DOS machine (not to a CD: speech seeks need a hard disk)
            and run a BAT from it.

It copies the files under 8.3 names (track25.flac -> TRACK25.FLA), stores
track1.flac, which is a WAV in this build, as TRACK1.WAV, adds a seek point
every second to TRACK25-29.FLA with metaflac (without them a seek stalls the
game for seconds on DOS; mi1 only), checks the speech file's clip index
(MONKEY.SOF, FLAC clips; MONKEY2.SOG, Ogg Vorbis clips), and writes the INI and
the BATs. --test-clips also decodes every clip: mi1 with flac, mi2 with
tremor-check (the Tremor decoder SCUMM.EXE links; build-deps.sh host builds it).
Your files are not changed. metaflac and flac come with FLAC
(https://xiph.org/flac/).
"""
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys

# One profile per game. Everything the two packs differ in is here.
GAMES = {
    "mi1": dict(
        pack="MI1UTE", gameid="monkey", target="mi1", map="data:M1KO.MAP",
        title="The Secret of Monkey Island", speech="monkey.sof", magic=b"fLaC", kind="FLAC",
        files=["monkey.000", "monkey.001", "monkey.sof", "track1.flac"] + ["track%d.flac" % n for n in range(25, 30)],
        korean=["korean.trs"] + ["korean%02d.fnt" % n for n in range(5)]),
    "mi2": dict(
        pack="MI2UTE", gameid="monkey2", target="mi2", map="data:M2KO.MAP",
        title="Monkey Island 2: LeChuck's Revenge", speech="monkey2.sog", magic=b"OggS", kind="Ogg Vorbis",
        files=["monkey2.000", "monkey2.001", "monkey2.sog"],
        korean=["korean.trs"] + ["korean%02d.fnt" % n for n in (0, 1, 2, 3, 4, 5, 7, 8)]),
}
PACK = GAMES["mi1"]["pack"]
UTE_FILES = GAMES["mi1"]["files"]
KOREAN_FILES = GAMES["mi1"]["korean"]
HOME_DEFAULT = "C:\\SCUMMVM"
TREMOR_CHECK = os.path.join(os.environ.get("DOS_FLAC_HOST", os.path.expanduser("~/opt/flac-host")), "bin", "tremor-check")
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


def check_sof(path, magic=b"fLaC", kind="FLAC"):
    """The speech file's clip index as SCUMM reads it (sound.cpp setupSfxFile and
    startTalkSound): a big-endian index size, 16 bytes a clip (original
    offset, offset after the index, tag bytes, data bytes), sorted by original
    offset (the engine bsearches it); each clip's data, after its tags, starts
    with `magic` (kind: FLAC for MONKEY.SOF, Ogg Vorbis for MONKEY2.SOG) and
    ends inside the file. Returns [(start, size)] of every clip's data."""
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
            if f.read(len(magic)) != magic:
                raise PackError("%s: clip %d (original offset %d) is not %s" % (path, i // 16, org, kind))
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


def tremor_clips(tool, path, clips, log):
    """Every clip of MONKEY2.SOG through tremor-check (build-deps.sh host), which
    opens and decodes each with Tremor, the decoder SCUMM.EXE links."""
    try:
        r = subprocess.run([tool, path], input="".join("%d %d\n" % c for c in clips).encode("ascii"),
                           capture_output=True, check=False)
    except OSError as e:
        raise PackError("cannot run %s: %s" % (tool, e))
    out = r.stdout.decode("utf-8", "replace").strip().splitlines()
    if r.returncode:
        bad = [l for l in out if l.startswith("FAIL")]
        raise PackError("%s: Tremor cannot decode %d clips, first: %s" % (
            path, len(bad), bad[0] if bad else r.stderr.decode("utf-8", "replace").strip()))
    log("MONKEY2.SOG: all %d clips decode with Tremor (%s)" % (len(clips), out[-1]))


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


def targets(korean, game="mi1"):
    g = GAMES[game]
    t = [(g["target"], g["target"].upper(), "%s (Ultimate Talkie, English)" % g["title"], "en", None, None)]
    if korean:
        t += [(g["target"] + "ko", g["target"].upper() + "KO",
               "%s (Ultimate Talkie, Korean, anti-aliased)" % g["title"], "ko", g["map"], None),
              (g["target"] + "kol", g["target"].upper() + "KOL",
               "%s (Ultimate Talkie, Korean, 8-bit font)" % g["title"], "ko", g["map"], "clut8")]
    return t


def ini_text(korean, game="mi1"):
    """The pack's INI (PLAY copies it to the profile once), CRLF, no path=."""
    g = GAMES[game]
    bats = ", ".join("%s.BAT" % b for _, b, _, _, _, _ in targets(korean, game))
    out = ["# Ready-to-run configuration: %s, Ultimate Talkie" % g["title"],
           "# Edition%s, for SCUMM.EXE. Written by MKUTE.PY." % (" (Midi Music build)" if game == "mi1" else ""),
           "# Started by %s. PLAY copies this file to" % bats,
           "# C:\\SCUMMVM\\GAMES\\%s\\SCUMMVM.INI once and gives the game directory" % g["pack"],
           "# itself, so there is no path= line.",
           "",
           "[scummvm]",
           "lastselectedgame=%s" % g["target"],
           "music_driver=adlib",
           ""]
    for name, _, desc, lang, mp, rt in targets(korean, game):
        out += ["[%s]" % name, "engineid=scumm", "gameid=%s" % g["gameid"], "description=%s" % desc,
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


def make_pack(ute, out, korean=None, metaflac="metaflac", flac="flac", test_clips=False, log=print,
              game="mi1", tremor=None):
    g = GAMES[game]
    missing = [n for n in g["files"] if not find(ute, n)]
    if missing:
        raise PackError('%s lacks %s (the %s folder has them)' % (
            ute, ", ".join(missing), "UTE \"Midi Music\"" if game == "mi1" else "Monkey Island 2 UTE"))
    if korean:
        missing = [n for n in g["korean"] if not find(korean, n)]
        if missing:
            raise PackError("%s lacks %s" % (korean, ", ".join(missing)))
    if game == "mi1":
        if not is_wav(find(ute, "track1.flac")):
            raise PackError('%s is not a WAV file: is this the "Midi Music" build?' % find(ute, "track1.flac"))
        run([metaflac, "--version"])
    speech = find(ute, g["speech"])
    clips = check_sof(speech, g["magic"], g["kind"])
    log("%s: %d clips, index in order, every clip %s" % (g["speech"].upper(), len(clips), g["kind"]))
    if test_clips:
        if game == "mi1":
            run([flac, "--version"])
            decode_clips(flac, speech, clips, log)
        else:
            tremor_clips(tremor or TREMOR_CHECK, speech, clips, log)
    pack = os.path.join(out, g["pack"])
    gamedir = os.path.join(pack, "GAMES", g["pack"])
    if os.path.exists(pack):
        shutil.rmtree(pack)
    os.makedirs(gamedir)
    for n in g["files"] + (g["korean"] if korean else []):
        src = find(korean if n in g["korean"] else ute, n)
        dst = os.path.join(gamedir, dos_name(n))
        shutil.copyfile(src, dst)
        if n.startswith("track") and n != "track1.flac":
            log("%s: %d seek points" % (dos_name(n), add_seekpoints(metaflac, dst)))
    with open(os.path.join(pack, g["pack"] + ".INI"), "wb") as f:
        f.write(ini_text(bool(korean), game).encode("ascii"))
    for target, bat, _, _, _, _ in targets(bool(korean), game):
        with open(os.path.join(pack, bat + ".BAT"), "wb") as f:
            f.write(game_bat(g["pack"], target).encode("ascii"))
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
    ap.add_argument("--game", choices=sorted(GAMES), default="mi1")
    ap.add_argument("--tremor-check", default=TREMOR_CHECK)
    a = ap.parse_args(argv)
    try:
        make_pack(a.ute, a.out, a.korean, a.metaflac, a.flac, a.test_clips, game=a.game, tremor=a.tremor_check)
    except PackError as e:
        print("mkute: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
