#!/usr/bin/env python3
"""check-tremor-cache.sh: build-dos.sh's guard for the Vorbis setup cache."""
import os
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "check-tremor-cache.sh")


def run(prefix):
    p = subprocess.run(["bash", SCRIPT, prefix], capture_output=True, text=True)
    return p.returncode, p.stdout, p.stderr


def make_prefix(root, internal=True, chunksize=None):
    inc = os.path.join(root, "include", "tremor")
    os.makedirs(inc)
    if internal:
        open(os.path.join(inc, "codec_internal.h"), "w").close()
    if chunksize is not None:
        with open(os.path.join(inc, "ivorbisfile.h"), "w") as f:
            f.write("#define CHUNKSIZE %s\n#define READSIZE  1024\n" % chunksize)
    return root


class CheckTremorCache(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = self._tmp.name

    def tearDown(self):
        self._tmp.cleanup()

    def test_missing_codec_internal_warns_loudly_and_continues(self):
        rc, out, err = run(make_prefix(self.root, internal=False, chunksize=4096))
        self.assertEqual(rc, 0)
        self.assertEqual(out, "")
        self.assertIn("Vorbis setup cache OFF", err)
        self.assertIn("every MI2 line will open cold (~150 ms)", err)
        self.assertIn("build-deps.sh codecs", err)
        self.assertGreaterEqual(len(err.strip().splitlines()), 3)

    def test_patched_prefix_is_quiet(self):
        rc, out, err = run(make_prefix(self.root, chunksize=4096))
        self.assertEqual((rc, err), (0, ""))

    def test_smaller_chunksize_is_quiet(self):
        rc, out, err = run(make_prefix(self.root, chunksize=1024))
        self.assertEqual((rc, err), (0, ""))

    def test_unpatched_chunksize_warns(self):
        rc, out, err = run(make_prefix(self.root, chunksize=65535))
        self.assertEqual(rc, 0)
        self.assertIn("CHUNKSIZE 65535", err)
        self.assertIn("tremor-chunksize.patch", err)
        self.assertNotIn("setup cache OFF", err)

    def test_missing_ivorbisfile_warns(self):
        rc, out, err = run(make_prefix(self.root))
        self.assertEqual(rc, 0)
        self.assertIn("ivorbisfile.h", err)


if __name__ == "__main__":
    unittest.main()
