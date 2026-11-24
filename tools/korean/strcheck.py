#!/usr/bin/env python3
"""strcheck.py: check a script string table (sci-ko.str, agi-ko.str) and set its header.

The table is UTF-8 text: an optional fingerprint header first
(#! format=1 game=<id> fingerprint=<md5> engine=<sci|agi>, DESIGN.md section 4),
then '#' comments, blank lines and entries. SCI: script<TAB>id[<TAB>room]<TAB>text
(numbers 0..65534; "\\n" is the only escape). AGI: key<TAB>text with the key
m<logic>:<message> (logic 0..255, message 1..255), o<object index> or
v<view number> (0..255); the escapes are \\n \\t \\\\, and a text that is not
valid UTF-8 refuses the line. The engine refuses the whole table on one bad
line, so this tool says which line it is.

The engine is the header's engine= field, or --engine when there is no header
(default sci).

usage: strcheck.py check FILE [--engine sci|agi]
       strcheck.py header FILE --line "#! format=1 ..." [-o OUT] [--engine sci|agi]
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


def _agi_num(line, p, limit):
    """-> (value, new p) for decimal digits at line[p:] with value <= limit, else (None, p); as readNum()."""
    q = p
    while q < len(line) and line[q] in "0123456789":
        q += 1
    if q == p or int(line[p:q]) > limit:
        return None, p
    return int(line[p:q]), q


def agi_key(line):
    """-> ((kind, logic, number), None) for an agi-<code>.str entry line, (None, problem) otherwise.

    As engines/agi/translation.cpp parseLine(): digits (leading zeros allowed) with the value
    at most 255; a message number is 1..255; the key is followed by a TAB; the text is UTF-8.
    """
    kind = line[0]
    logic = 0
    if kind == "m":
        logic, p = _agi_num(line, 1, 255)
        if logic is None or p >= len(line) or line[p] != ":":
            return None, "bad logic number"
        number, p = _agi_num(line, p + 1, 255)
        if number is None or number == 0:
            return None, "bad message number (1..255)"
    elif kind == "o":
        number, p = _agi_num(line, 1, 255)
        if number is None:
            return None, "bad object index"
    elif kind == "v":
        number, p = _agi_num(line, 1, 255)
        if number is None:
            return None, "bad view number"
    else:
        return None, "unknown key prefix (m<logic>:<n>, o<index> or v<view>)"
    if p >= len(line) or line[p] != "\t":
        return None, "no tab after the key"
    try:
        line[p + 1:].encode("utf-8")
    except UnicodeEncodeError:
        return None, "the text is not UTF-8"
    return (kind, logic, number), None


def check(lines, engine=None):
    """-> (header or None, entries, problems); problems are (line number, text).

    engine: "sci" or "agi" when the table has no header (the header's engine= wins).
    """
    problems = []
    header = None
    seen = {}
    entries = 0
    if lines:
        first = lines[0].rstrip("\r\n").lstrip("\ufeff")
        if first.startswith("#!"):
            try:
                engine = parse_header(first).get("engine") or engine
            except ValueError:
                pass
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
        if engine == "agi":
            key, problem = agi_key(line)
            if problem:
                problems.append((n, problem))
                continue
            if key in seen:
                problems.append((n, "duplicates line %d (the later line wins)" % seen[key]))
            else:
                seen[key] = n
            entries += 1
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
    c.add_argument("--engine", choices=("sci", "agi"))
    h = sub.add_parser("header")
    h.add_argument("file")
    h.add_argument("--line", required=True)
    h.add_argument("-o", "--out")
    h.add_argument("--engine", choices=("sci", "agi"))
    a = ap.parse_args(argv)
    try:
        raw = open(a.file, "rb").read()
        try:
            lines = raw.decode("utf-8").splitlines(True)
        except UnicodeDecodeError:
            # an AGI table with a text that is not UTF-8 is a refused line, not an unreadable file
            lines = raw.decode("utf-8", "surrogateescape").splitlines(True)
            first = lines[0].rstrip("\r\n").lstrip("\ufeff") if lines else ""
            agi = a.engine == "agi" or (first.startswith("#!") and " engine=agi" in first)
            if not agi:
                raise
            if a.cmd == "header":
                print("strcheck: %s: not valid UTF-8, run check" % a.file, file=sys.stderr)
                return 2
    except (OSError, UnicodeDecodeError) as e:
        print("strcheck: %s: %s" % (a.file, e), file=sys.stderr)
        return 2
    if a.cmd == "check":
        header, entries, problems = check(lines, a.engine)
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
