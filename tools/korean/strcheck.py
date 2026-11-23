#!/usr/bin/env python3
"""strcheck.py: check a script string table (sci-ko.str, agi-ko.str) and set its header.

The table is UTF-8 text: an optional fingerprint header first
(#! format=1 game=<id> fingerprint=<md5> engine=<sci|agi>, DESIGN.md section 4),
then '#' comments, blank lines and entries script<TAB>id[<TAB>room]<TAB>text
(numbers 0..65534; "\\n" is the only escape). The engine refuses the whole
table on one bad line, so this tool says which line it is.

usage: strcheck.py check FILE
       strcheck.py header FILE --line "#! format=1 ..." [-o OUT]
       (the engine prints the line for a game: "This game's sci-ko.str header line is: ...")
"""

import argparse
import re
import sys

HEADER = re.compile(r"^#![ \t]")
FIELD = re.compile(r"^([a-z]+)=(\S*)$")
MD5 = re.compile(r"^[0-9a-fA-F]{32}$")
NUM = re.compile(r"^[0-9]{1,5}$")  # as ScriptStrings::readNum: digits, leading zeros allowed


def parse_header(line):
    """-> dict of the header fields; ValueError when it is not a format 1 header."""
    if not HEADER.match(line):
        raise ValueError("not a header line (it must start with '#! ')")
    fields = {}
    for tok in line[2:].split():
        m = FIELD.match(tok)
        if not m:
            raise ValueError("field %r is not key=value" % tok)
        fields[m.group(1)] = m.group(2)
    if fields.get("format") != "1":
        raise ValueError("format=%s is not 1" % fields.get("format"))
    for key in ("game", "engine"):
        if not fields.get(key):
            raise ValueError("%s= is missing" % key)
    for key in ("fingerprint", "vocab"):
        if key in fields and not MD5.match(fields[key]):
            raise ValueError("%s=%s is not an md5" % (key, fields[key]))
    if "fingerprint" not in fields:
        raise ValueError("fingerprint= is missing")
    return fields


def check(lines):
    """-> (header or None, entries, problems); problems are (line number, text)."""
    problems = []
    header = None
    seen = {}
    entries = 0
    for n, raw in enumerate(lines, 1):
        line = raw.rstrip("\r\n")
        if n == 1 and line.startswith("\ufeff"):
            line = line[1:]
        if n == 1 and line.startswith("#!"):
            try:
                header = parse_header(line)
            except ValueError as e:
                problems.append((n, "header: %s" % e))
            continue
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) < 3 or not NUM.match(parts[0]) or not NUM.match(parts[1]) or int(parts[0]) > 65534 or int(parts[1]) > 65534:
            problems.append((n, "not script<TAB>id[<TAB>room]<TAB>text"))
            continue
        room = "any"
        if len(parts) >= 4 and NUM.match(parts[2]) and int(parts[2]) <= 65534:
            room = parts[2]
        key = (int(parts[0]), int(parts[1]), room if room == "any" else int(room))
        if key in seen:
            problems.append((n, "duplicates line %d (the later line wins)" % seen[key]))
        else:
            seen[key] = n
        entries += 1
    if header is None:
        problems.append((1, "no fingerprint header: the engine loads the table unchecked"))
    return header, entries, problems


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("check")
    c.add_argument("file")
    h = sub.add_parser("header")
    h.add_argument("file")
    h.add_argument("--line", required=True)
    h.add_argument("-o", "--out")
    a = ap.parse_args(argv)
    try:
        lines = open(a.file, encoding="utf-8").read().splitlines(True)
    except (OSError, UnicodeDecodeError) as e:
        print("strcheck: %s: %s" % (a.file, e), file=sys.stderr)
        return 2
    if a.cmd == "check":
        header, entries, problems = check(lines)
        for n, text in problems:
            print("%s:%d: %s" % (a.file, n, text))
        print("%s: %d entries, header %s" % (a.file, entries, "yes" if header else "no"))
        return 1 if any(not t.startswith("no fingerprint") and "duplicates" not in t for _, t in problems) else 0
    try:
        parse_header(a.line)
    except ValueError as e:
        print("strcheck: --line: %s" % e, file=sys.stderr)
        return 2
    if lines and lines[0].lstrip("\ufeff").startswith("#!"):
        lines = lines[1:]
    out = open(a.out or a.file, "w", encoding="utf-8", newline="")
    out.write(a.line + "\n")
    out.writelines(lines)
    out.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
