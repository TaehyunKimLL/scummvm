#!/usr/bin/env python3
"""Writes a detection table header without the entries a DOS edition can
never run (the DOS build includes the result instead of the original).

    detection-filter.py sci   <detection_tables.h> <detection_internal.cpp>  > out.h
    detection-filter.py scumm <scumm-md5.h | detection_tables.h> <detection_tables.h>  > out.h

sci: drops every SciGameDescriptions entry whose game ID is an SCI32 one
(isSci32 true in detection_internal.cpp's gameIdStrToEnum): the SCI
edition is built without sci32.

scumm: keeps only the md5table / gameVariantsTable / gameFilenamesTable
entries of game IDs that have a v0-v6 variant (version < 7, heversion 0
in gameVariantsTable, read from the second file): the SCUMM edition is
built without scumm_7_8 and he. Such an ID is kept whole, every platform.

Everything else is copied as it is, comments included, so the line count
changes only by the dropped entries. Entries are found by brace depth, not
by layout: a top-level "{...}," inside the named arrays, whose first string
is the game ID (md5table: the second string).
"""
import re
import sys

STRING = re.compile(r'"(?:[^"\\]|\\.)*"')


def entries(text, array_re):
    """(start, end, body) of each depth-1 brace entry of the array whose
    declaration matches array_re; end includes the trailing comma and
    newline."""
    m = re.search(array_re, text)
    if not m:
        raise SystemExit("detection-filter: no array matching %r" % array_re)
    i = text.index("{", m.end() - 1) + 1
    depth = 0
    start = None
    out = []
    n = len(text)
    while i < n:
        c = text[i]
        if c == '"':
            j = STRING.match(text, i).end()
            i = j
            continue
        if text.startswith("//", i):
            i = text.index("\n", i)
            continue
        if text.startswith("/*", i):
            i = text.index("*/", i) + 2
            continue
        if c == "{":
            if depth == 0:
                start = i
            depth += 1
        elif c == "}":
            if depth == 0:
                break   # the array's own closing brace
            depth -= 1
            if depth == 0:
                end = i + 1
                k = end
                while k < n and text[k] in " \t":
                    k += 1
                if k < n and text[k] == ",":
                    k += 1
                if k < n and text[k] == "\n":
                    k += 1
                body = text[start:end]
                # take the entry's own indentation too
                ls = text.rfind("\n", 0, start) + 1
                if text[ls:start].strip() == "":
                    start = ls
                out.append((start, k, body))
        i += 1
    return out


def drop(text, array_re, keep):
    parts = []
    last = 0
    dropped = 0
    for s, e, body in entries(text, array_re):
        if keep(body):
            continue
        parts.append(text[last:s])
        last = e
        dropped += 1
    parts.append(text[last:])
    return "".join(parts), dropped


def strings(body):
    return [s[1:-1] for s in STRING.findall(body)]


def ids(text, array_re, idx):
    """Entry count per game ID (the idx-th string) of an array."""
    count = {}
    for s, e, body in entries(text, array_re):
        st = strings(body)
        if len(st) > idx:
            count[st[idx]] = count.get(st[idx], 0) + 1
    return count


def verify(name, original, filtered, array_re, idx, kept):
    """Fails unless every game ID the filter keeps (kept(id)) has as many
    entries in the filtered array as in the original, and no other ID is
    left."""
    before = ids(original, array_re, idx)
    after = ids(filtered, array_re, idx)
    bad = []
    for gid, n in before.items():
        want = n if kept(gid) else 0
        if after.get(gid, 0) != want:
            bad.append("%s: %d entries, expected %d" % (gid, after.get(gid, 0), want))
    bad += ["%s: not in the original" % g for g in after if g not in before]
    if bad:
        raise SystemExit("detection-filter: %s: self-check failed:\n  %s" % (name, "\n  ".join(bad)))
    dropped = sorted(g for g in before if not kept(g))
    sys.stderr.write("detection-filter: %s: dropped IDs: %s\n" % (name, " ".join(dropped) or "(none)"))


def sci(tables, internal):
    src = re.sub(r"//[^\n]*", "", open(internal).read())
    sci32 = set(re.findall(r'\{\s*"([^"]+)",\s*"[^"]*",\s*GID_\w+,\s*true,', src))
    if not sci32:
        raise SystemExit("detection-filter: no SCI32 game IDs found in " + internal)
    original = open(tables).read()
    arr = r"SciGameDescriptions\[\]\s*=\s*\{"

    def keep(body):
        s = strings(body)
        return not s or s[0] not in sci32 or "AD_TABLE_END_MARKER" in body
    text, n = drop(original, arr, keep)
    verify("SciGameDescriptions", original, text, arr, 0, lambda g: g not in sci32)
    return text, n


def scumm(target, variants_file):
    vt = open(variants_file).read()
    # gameVariantsTable rows: {"gameid", "variant", "preferredTag", GID_..., version, heversion, ...}
    old, new = set(), set()
    for s, e, body in entries(vt, r"gameVariantsTable\[\]\s*=\s*\{"):
        m = re.match(r'\{\s*"([^"]+)"\s*,\s*(?:"[^"]*"|0)\s*,\s*(?:"[^"]*"|0)\s*,\s*GID_\w+\s*,\s*(\d+)\s*,\s*(\d+)', body)
        if not m:
            st = strings(body)
            if st and st[0]:
                # Fail closed: an unread row would drop its game everywhere.
                raise SystemExit("detection-filter: cannot read this gameVariantsTable row:\n  " + body)
            continue    # the generic HE rows (empty ID) and the terminator
        gid, ver, he = m.group(1), int(m.group(2)), int(m.group(3))
        (new if ver >= 7 or he else old).add(gid)
    if not old or not new - old:
        raise SystemExit("detection-filter: no v0-v6 or no v7+/HE game IDs in " + variants_file)
    # Kept: the IDs with a v0-v6 variant. (md5table also names IDs that no
    # variant has; those could not run in any build.)
    original = open(target).read()
    text = original
    total = 0
    arrays = [(r"md5table\[\]\s*=\s*\{", 1), (r"gameVariantsTable\[\]\s*=\s*\{", 0),
              (r"gameFilenamesTable\[\]\s*=\s*\{", 0)]
    found = 0
    for arr, idx in arrays:
        if re.search(arr, text):
            found += 1
            text, n = drop(text, arr, lambda b, idx=idx: len(strings(b)) <= idx or strings(b)[idx] in old)
            total += n
            verify(arr.split("\\")[0], original, text, arr, idx, lambda g: g in old)
    if not found:
        raise SystemExit("detection-filter: no table to filter in " + target)
    return text, total


def main():
    kind = sys.argv[1]
    if kind == "sci":
        text, n = sci(sys.argv[2], sys.argv[3])
    elif kind == "scumm":
        text, n = scumm(sys.argv[2], sys.argv[3])
        # The tables include the MD5 table: the filtered one.
        text = text.replace('#include "scumm/scumm-md5.h"', '#include "dosdetect/scumm/scumm-md5.h"')
    else:
        raise SystemExit(__doc__)
    sys.stdout.write("// Generated by backends/platform/dos/detection-filter.py from %s:\n"
                     "// %d entries a DOS edition cannot run are left out.\n" % (sys.argv[2], n))
    sys.stdout.write(text)
    sys.stderr.write("detection-filter: %s: %d entries dropped\n" % (sys.argv[2], n))


if __name__ == "__main__":
    main()
