#!/usr/bin/env python3
"""Tests of stage-data.py: the 8.3 names DOS gives engine data, collisions, and the maps' data: faces."""
import importlib.util
import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("stage_data", os.path.join(HERE, "stage-data.py"))
sd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sd)


def write(path, text="x"):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


class DosNameTest(unittest.TestCase):
    def test_truncates_and_upcases(self):
        self.assertEqual(sd.dos_name("hires_text"), "HIRES_TE")
        self.assertEqual(sd.dos_name("korean-default.map"), "KOREAN-D.MAP")
        self.assertEqual(sd.dos_name("NEODG16A.SVF"), "NEODG16A.SVF")
        self.assertEqual(sd.dos_name("maps"), "MAPS")
        self.assertEqual(sd.dos_name("a.extension"), "A.EXT")

    def test_refuses_what_dos_cannot_name(self):
        with self.assertRaises(sd.StageError):
            sd.dos_name("a.b.c")
        with self.assertRaises(sd.StageError):
            sd.dos_name("con.map")
        with self.assertRaises(sd.StageError):
            sd.dos_name("COM1")
        with self.assertRaises(sd.StageError):
            sd.dos_name("a b.map")
        with self.assertRaises(sd.StageError):
            sd.dos_name("")

    def test_checks_before_the_cut(self):
        for bad in ("korean-\ud55c\uae00.map", "a b.map", "q?.map", "abcdefgh\u00e9.map", "a.m\u00e9p", "x*.map", "CLOCK$.map", "clock$"):
            with self.assertRaises(sd.StageError, msg=bad):
                sd.dos_name(bad)
        self.assertEqual(sd.dos_name("abcdefghij.mapx"), "ABCDEFGH.MAP")

    def test_random_names_against_a_model(self):
        import random
        rnd = random.Random(5)
        alphabet = "abcXYZ019_-.$ ?\u00e9\ud55c!"
        accepted = 0
        for _ in range(20000):
            name = "".join(rnd.choice(alphabet) for _ in range(rnd.randint(0, 14)))
            base, _, ext = name.partition(".")
            ok = (name.count(".") <= 1 and base != "" and all(0x21 <= ord(c) <= 0x7e and c not in "?*" and c in
                  "abcXYZ019_-$!" for c in base + ext) and base.upper()[:8] not in sd.DEVICES)
            try:
                out = sd.dos_name(name)
            except sd.StageError:
                self.assertFalse(ok, name)
                continue
            self.assertTrue(ok, name)
            accepted += 1
            b, _, e = out.partition(".")
            self.assertTrue(len(b) <= 8 and len(e) <= 3 and out == out.upper(), out)
        self.assertTrue(accepted > 1000)

    def test_dos_path(self):
        self.assertEqual(sd.dos_path("hires_text/maps/korean-default.map"), "HIRES_TE/MAPS/KOREAN-D.MAP")
        self.assertEqual(sd.dos_path("hires_text\\maps\\korean-default.map"), "HIRES_TE/MAPS/KOREAN-D.MAP")


class StageTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="stage-data-", dir=os.environ.get("TMPDIR"))
        self.addCleanup(shutil.rmtree, self.tmp, True)

    def test_stage_writes_the_truncated_names(self):
        src = os.path.join(self.tmp, "src")
        write(os.path.join(src, "hires_text", "maps", "korean-default.map"), "map")
        write(os.path.join(src, "hires_text", "NEODG16A.SVF"), "face")
        data = os.path.join(self.tmp, "DATA")
        staged = sd.stage([(os.path.join(src, "hires_text"), "hires_text")], data)
        self.assertEqual(sorted(staged), ["HIRES_TE/MAPS/KOREAN-D.MAP", "HIRES_TE/NEODG16A.SVF"])
        with open(os.path.join(data, "HIRES_TE", "MAPS", "KOREAN-D.MAP")) as f:
            self.assertEqual(f.read(), "map")
        self.assertTrue(os.path.isfile(os.path.join(data, "HIRES_TE", "NEODG16A.SVF")))

    def test_collision_is_refused_and_names_both_sources(self):
        src = os.path.join(self.tmp, "src")
        write(os.path.join(src, "hires_text", "maps", "korean-default.map"))
        write(os.path.join(src, "hires_text", "maps", "korean-defaults.map"))
        with self.assertRaises(sd.StageError) as cm:
            sd.stage([(os.path.join(src, "hires_text"), "hires_text")], os.path.join(self.tmp, "DATA"))
        msg = str(cm.exception)
        self.assertIn("korean-default.map", msg)
        self.assertIn("korean-defaults.map", msg)
        self.assertIn("KOREAN-D.MAP", msg)

    def test_collision_across_case_and_roots(self):
        a = os.path.join(self.tmp, "a")
        b = os.path.join(self.tmp, "b")
        write(os.path.join(a, "face.svf"))
        write(os.path.join(b, "FACE.SVF"))
        with self.assertRaises(sd.StageError):
            sd.stage([(a, ""), (b, "")], os.path.join(self.tmp, "DATA"))

    def test_a_file_cannot_take_the_name_of_a_folder(self):
        src = os.path.join(self.tmp, "src")
        write(os.path.join(src, "faces"))               # FACES, a file
        write(os.path.join(src, "FACES", "a.svf"))      # FACES/A.SVF, needs FACES as a folder
        with self.assertRaises(sd.StageError):
            sd.stage([(src, "")], os.path.join(self.tmp, "DATA"))

    def test_nothing_is_written_when_a_collision_is_found(self):
        src = os.path.join(self.tmp, "src")
        write(os.path.join(src, "a-long-name.svf"))
        write(os.path.join(src, "a-long-nameX.svf"))
        data = os.path.join(self.tmp, "DATA")
        with self.assertRaises(sd.StageError):
            sd.stage([(src, "")], data)
        self.assertFalse(os.path.exists(data))

    def test_check_maps_also_reads_preload(self):
        data = os.path.join(self.tmp, "DATA")
        write(os.path.join(data, "HIRES_TE", "NEODG16A.SVF"))
        m = os.path.join(self.tmp, "x.map")
        write(m, "[fonts]\nneo=data:hires_text/NEODG16A.SVF\n[preload]\nneo=data:hires_text/NEODG16A.HOT\n")
        self.assertEqual(len(sd.check_maps(data, [m])), 1)

    def test_check_maps_reports_missing_faces(self):
        data = os.path.join(self.tmp, "DATA")
        write(os.path.join(data, "HIRES_TE", "NEODG16A.SVF"))
        m = os.path.join(self.tmp, "x.map")
        write(m, "[map]\nversion=3\n[fonts]\nneo=data:hires_text/NEODG16A.SVF\nbig=data:hires_text/bigface-long.svf\n"
                 "own=game/OWN.SVF\n[font:clut8]\nface=neo\n")
        missing = sd.check_maps(data, [m])
        self.assertEqual(len(missing), 1)
        self.assertIn("bigface-long.svf", missing[0])

    def test_missing_and_linked_roots_are_refused(self):
        with self.assertRaises(sd.StageError):
            sd.stage([(os.path.join(self.tmp, "nope"), "")], os.path.join(self.tmp, "DATA"))
        src = os.path.join(self.tmp, "src")
        write(os.path.join(src, "a.svf"))
        os.symlink(os.path.join(src, "a.svf"), os.path.join(src, "b.svf"))
        with self.assertRaises(sd.StageError):
            sd.stage([(src, "")], os.path.join(self.tmp, "DATA"))
        self.assertEqual(sd.main(["stage-data.py", os.path.join(self.tmp, "nope"), os.path.join(self.tmp, "D2")]), 1)

    def test_check_maps_hash_comments(self):
        data = os.path.join(self.tmp, "DATA")
        m = os.path.join(self.tmp, "x.map")
        write(m, "# [fonts]\n[fonts]\n# neo=data:hires_text/GONE.SVF\nneo=data:hires_text/GOT.SVF\n")
        write(os.path.join(data, "HIRES_TE", "GOT.SVF"))
        self.assertEqual(sd.check_maps(data, [m]), [])

    def test_check_maps_ok(self):
        data = os.path.join(self.tmp, "DATA")
        write(os.path.join(data, "HIRES_TE", "NEODG16A.SVF"))
        m = os.path.join(self.tmp, "x.map")
        write(m, "[fonts]\nneo = data:hires_text/NEODG16A.SVF ; comment\n")
        self.assertEqual(sd.check_maps(data, [m]), [])


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
