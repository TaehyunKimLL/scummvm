#!/usr/bin/env python3
"""Tests for mkute.py. Needs host flac/metaflac (build-deps.sh host):
FLAC_HOST names their bin directory (default ~/opt/flac-host/bin).

    python3 backends/platform/dos/test_mkute.py
"""
import hashlib
import os
import struct
import subprocess
import sys
import tempfile
import unittest
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkute  # noqa: E402

BIN = os.environ.get("FLAC_HOST", os.path.expanduser("~/opt/flac-host/bin"))
FLAC = os.path.join(BIN, "flac")
METAFLAC = os.path.join(BIN, "metaflac")


def write_wav(path, secs, channels=2, rate=44100):
    with wave.open(path, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(rate)
        frames = bytearray()
        for i in range(secs * rate):
            v = (i * 37) % 2000 - 1000
            frames += struct.pack("<h", v) * channels
        w.writeframes(bytes(frames))


def write_sof(path, clips):
    """A MONKEY.SOF with the given clips: [(original offset, tag bytes, data)]."""
    index = b""
    body = b""
    for org, tags, data in clips:
        index += struct.pack(">IIII", org, len(body), len(tags), len(data))
        body += tags + data
    with open(path, "wb") as f:
        f.write(struct.pack(">I", len(index)) + index + body)


def md5(path):
    with open(path, "rb") as f:
        return hashlib.md5(f.read()).hexdigest()


class MkuteTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.ute = os.path.join(self.tmp.name, "ute")
        self.kor = os.path.join(self.tmp.name, "kor")
        self.out = os.path.join(self.tmp.name, "out")
        os.makedirs(self.ute)
        os.makedirs(self.kor)
        for n in ("monkey.000", "monkey.001"):
            with open(os.path.join(self.ute, n), "wb") as f:
                f.write(n.encode() * 10)
        write_sof(os.path.join(self.ute, "monkey.sof"),
                  [(8, b"\0\1", b"fLaC" + b"\0" * 60), (500, b"\0\2", b"fLaC" + b"\0" * 30)])
        write_wav(os.path.join(self.ute, "track1.flac"), 1, channels=1)
        wav = os.path.join(self.tmp.name, "t.wav")
        write_wav(wav, 3)
        for n in range(25, 30):
            subprocess.run([FLAC, "-s", "-f", "-o", os.path.join(self.ute, "track%d.flac" % n), wav], check=True)
        with open(os.path.join(self.kor, "korean.trs"), "wb") as f:
            f.write(b"SCVMTRS \0\0")
        for n in range(5):
            with open(os.path.join(self.kor, "korean%02d.fnt" % n), "wb") as f:
                f.write(bytes([0, 0, 8, 8]))

    def tearDown(self):
        self.tmp.cleanup()

    def make(self, korean=False):
        return mkute.make_pack(self.ute, self.out, korean=self.kor if korean else None,
                               metaflac=METAFLAC, flac=FLAC, log=lambda *a: None)

    def test_dos_names(self):
        self.assertEqual(mkute.dos_name("track25.flac"), "TRACK25.FLA")
        self.assertEqual(mkute.dos_name("monkey.sof"), "MONKEY.SOF")
        self.assertEqual(mkute.dos_name("track1.flac"), "TRACK1.WAV")
        self.assertEqual(mkute.dos_name("korean00.fnt"), "KOREAN00.FNT")

    def test_english_pack(self):
        before = md5(os.path.join(self.ute, "track25.flac"))
        pack = self.make()
        game = os.path.join(pack, "GAMES", "MI1UTE")
        want = {"MONKEY.000", "MONKEY.001", "MONKEY.SOF", "TRACK1.WAV"} | {"TRACK%d.FLA" % n for n in range(25, 30)}
        self.assertEqual(set(os.listdir(game)), want)
        self.assertEqual(sorted(os.listdir(pack)), ["GAMES", "MI1.BAT", "MI1UTE.INI"])
        for n in range(25, 30):
            pts = mkute.seek_points(METAFLAC, os.path.join(game, "TRACK%d.FLA" % n))
            self.assertGreaterEqual(len(pts), 3)
        self.assertEqual(md5(os.path.join(self.ute, "track25.flac")), before)   # input untouched
        ini = open(os.path.join(pack, "MI1UTE.INI"), "rb").read().decode("ascii")
        self.assertIn("[mi1]\r\n", ini)
        self.assertNotIn("[mi1ko]", ini)
        self.assertIn("speech_mute=false\r\n", ini)
        self.assertFalse(any(l.startswith("path=") for l in ini.split("\r\n")))
        bat = open(os.path.join(pack, "MI1.BAT"), "rb").read().decode("ascii")
        self.assertEqual(bat, mkute.game_bat("MI1UTE", "mi1"))
        self.assertIn("%SVH%\\PLAY MI1UTE mi1\r\n", bat)

    def test_korean_pack(self):
        pack = self.make(korean=True)
        game = os.path.join(pack, "GAMES", "MI1UTE")
        self.assertTrue({"KOREAN.TRS"} | {"KOREAN%02d.FNT" % n for n in range(5)} <= set(os.listdir(game)))
        self.assertEqual(sorted(f for f in os.listdir(pack) if f.endswith(".BAT")), ["MI1.BAT", "MI1KO.BAT", "MI1KOL.BAT"])
        ini = open(os.path.join(pack, "MI1UTE.INI"), "rb").read().decode("ascii")
        for s in ("[mi1]", "[mi1ko]", "[mi1kol]", "hires_text_map=data:M1KO.MAP", "render_target=clut8", "language=ko"):
            self.assertIn(s, ini)
        self.assertTrue(all(l.endswith("\r") or l == "" for l in ini.split("\n")[:-1]))

    def test_missing_file(self):
        os.remove(os.path.join(self.ute, "track27.flac"))
        with self.assertRaisesRegex(mkute.PackError, "track27.flac"):
            self.make()

    def test_track1_must_be_wav(self):
        with open(os.path.join(self.ute, "track1.flac"), "wb") as f:
            f.write(b"fLaC" + b"\0" * 40)
        with self.assertRaisesRegex(mkute.PackError, "not a WAV"):
            self.make()

    def test_sof_index_checks(self):
        p = os.path.join(self.tmp.name, "bad.sof")
        write_sof(p, [(500, b"", b"fLaC"), (8, b"", b"fLaC")])
        with self.assertRaisesRegex(mkute.PackError, "out of order"):
            mkute.check_sof(p)
        write_sof(p, [(8, b"\0\1", b"OggS")])
        with self.assertRaisesRegex(mkute.PackError, "not FLAC"):
            mkute.check_sof(p)
        self.assertEqual(len(mkute.check_sof(os.path.join(self.ute, "monkey.sof"))), 2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
