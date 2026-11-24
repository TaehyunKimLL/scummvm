#!/usr/bin/env python3
"""stage-data.py [ROOT[=PREFIX]]... DATA_DIR [--maps MAP...]: copy engine data into a DOS DATA folder with the names DOS gives them.

MS-DOS (and DJGPP with long names off, _CRT0_FLAG_NO_LFN) truncates every path component to 8.3 and upper-cases it, so
data:hires_text/maps/korean-default.map opens DATA\\HIRES_TE\\MAPS\\KOREAN-D.MAP. This writes exactly those names and
refuses two sources that would get the same one (or a file that would need the name of a folder). Every file under a
ROOT is staged as PREFIX/<its path> (no PREFIX: directly under DATA_DIR). --maps checks that every data: face of
each map's [fonts] and [preload] resolves to a staged file.
"""
import os
import re
import shutil
import sys

DEVICES = {"CON", "PRN", "AUX", "NUL"} | {"COM%d" % i for i in range(1, 10)} | {"LPT%d" % i for i in range(1, 10)}
VALID = re.compile(r"^[A-Z0-9_\-!#$%&'(){}@^`~]+$")


class StageError(Exception):
    pass


def dos_name(component):
    if component.count(".") > 1:
        raise StageError("%s: more than one dot" % component)
    base, _, ext = component.partition(".")
    base, ext = base[:8].upper(), ext[:3].upper()
    if not base or not VALID.match(base) or (ext and not VALID.match(ext)) or base in DEVICES:
        raise StageError("%s: no DOS name" % component)
    return base + ("." + ext if ext else "")


def dos_path(rel):
    return "/".join(dos_name(c) for c in rel.replace("\\", "/").split("/") if c)


def stage(sources, data_dir):
    """sources: [(root, prefix)] - every file under root is staged as prefix/<its path>. Returns {dos path: source path}."""
    seen = {}
    origin = {}
    dirs = set()
    for root, prefix in sources:
        for dirpath, dirnames, files in os.walk(root):
            dirnames.sort()
            for f in sorted(files):
                src = os.path.join(dirpath, f)
                rel = os.path.relpath(src, root).replace(os.sep, "/")
                rel = (prefix + "/" + rel) if prefix else rel
                d = dos_path(rel)
                if d in seen:
                    raise StageError("%s and %s both become %s" % (seen[d], rel, d))
                parts = d.split("/")
                for i in range(1, len(parts)):
                    folder = "/".join(parts[:i])
                    if folder in seen:
                        raise StageError("%s needs %s as a folder, but %s is a file (both become %s)" % (rel, folder, seen[folder], folder))
                    dirs.add(folder)
                if d in dirs:
                    raise StageError("%s becomes %s, which is also the name of a folder" % (rel, d))
                seen[d] = rel
                origin[d] = src
    for d in sorted(seen):
        out = os.path.join(data_dir, *d.split("/"))
        os.makedirs(os.path.dirname(out), exist_ok=True)
        shutil.copyfile(origin[d], out)
    return seen


def check_maps(data_dir, maps):
    """The data: faces of [fonts] and [preload] in each map that DATA_DIR does not hold (as 'MAP: value')."""
    missing = []
    for m in maps:
        section = None
        with open(m, encoding="utf-8-sig") as fh:
            for line in fh:
                line = line.split(";", 1)[0].strip()
                if line.startswith("["):
                    section = line.strip("[]").split(":", 1)[0].lower()
                elif section in ("fonts", "preload") and "=" in line:
                    value = line.split("=", 1)[1].strip()
                    if value.startswith("data:"):
                        try:
                            found = os.path.isfile(os.path.join(data_dir, *dos_path(value[5:]).split("/")))
                        except StageError:
                            found = False
                        if not found:
                            missing.append("%s: %s" % (m, value))
    return missing


def main(argv):
    maps = []
    if "--maps" in argv:
        i = argv.index("--maps")
        argv, maps = argv[:i], argv[i + 1:]
    if len(argv) < 3:
        print(__doc__.strip().splitlines()[0])
        return 2
    *srcs, data = argv[1:]
    try:
        staged = stage([(s.split("=", 1)[0], s.split("=", 1)[1] if "=" in s else "") for s in srcs], data)
    except StageError as e:
        print("stage-data.py: %s" % e, file=sys.stderr)
        return 1
    missing = check_maps(data, maps)
    for m in missing:
        print("stage-data.py: not staged: %s" % m, file=sys.stderr)
    print("stage-data.py: %d files into %s" % (len(staged), data))
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
