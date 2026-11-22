#!/usr/bin/env python3
# ScummVM - Graphic Adventure Engine
#
# ScummVM is the legal property of its developers, whose names
# are too numerous to list here. Please refer to the COPYRIGHT
# file distributed with this source distribution.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
"""Bake a Korean parser word table (sci-ko.dat / SCIKOR.DAT, agi-ko.dat / AGIKOR.DAT) for common/korean_vocab.

The layouts are in common/korean_vocab.h: version 2 (u16 offsets, no header; the packs made before fingerprint
headers) and version 3 (u32 offsets and the fingerprint header line of common/translation_file.h). Python 3 standard
library only.

usage:
  k6bake.py build SOURCE OUT [--format 2|3] [--game ID --fingerprint MD5 --vocab-md5 MD5]
                             [--targets K2TARGETS] [--magic SCIKOR|AGIKOR] [--verbs K2TARGETS]
      SOURCE is a UTF-8 master (.vocab.tsv, first line "#KOREAN-VOCAB 2 ...", the harness's trs_master.py format) or
      a k3map.json (Korean word -> groups; the classes from --targets, or for AGI --magic AGIKOR --verbs). Format 3
      (the default) needs --game, --fingerprint and --vocab-md5; the engine comes from the magic.
  k6bake.py convert IN OUT [--format 2|3] [--game ID --fingerprint MD5 --vocab-md5 MD5]
      rewrite a version 2 or 3 table in the other version (the rows stay byte for byte).
  k6bake.py dump DAT
      the header line (version 3) and one "word<TAB>group<TAB>0xclass" line per entry, in file order.
  k6bake.py golden SOURCE --game ID --fingerprint MD5 [--vocab-md5 MD5]
      a C++ header with the version 2 and 3 tables of SOURCE (test/korean_vocab_golden.h).
  k6bake.py harness-check HARNESS
      build every committed master of the harness (HARNESS/dos/release/<game>/translation/*-ko.dat.vocab.tsv) as
      version 2 and compare with the sha256 in HARNESS/dos/release/translations.tsv; exit 1 on a difference.
"""

import hashlib
import json
import os
import re
import struct
import sys

MASTER_TAG = "#KOREAN-VOCAB 2 (UTF-8 master of a Korean parser word table, sci-ko.dat / agi-ko.dat; trs_master.py)"
MAX_WORD = 16      # common/korean_stem.h KoreanStems::kMaxLen: the reader looks up no longer word
SEQ = 0x8000       # class flag: this row continues into the next row of the same word
MAGICS = ("SCIKOR", "AGIKOR")
MD5 = re.compile(r"^[0-9a-fA-F]{32}$")
ID = re.compile(r"^[A-Za-z0-9_-]+$")


class BakeError(Exception):
    pass


def slurp(path):
    with open(path, "rb") as f:
        return f.read()


def units(word):
    return [ord(c) for c in word]


def sort_rows(rows):
    """Sorted by code units, unsigned, shorter first on a common prefix (Python's list order); stable."""
    return sorted(rows, key=lambda r: units(r[0]))


def header_line(game, fingerprint, engine, vocab=None):
    if not game or not ID.match(game):
        raise BakeError("--game must be a game id")
    if not fingerprint or not MD5.match(fingerprint):
        raise BakeError("--fingerprint must be 32 hex digits")
    if vocab is not None and not MD5.match(vocab):
        raise BakeError("--vocab-md5 must be 32 hex digits")
    line = "#! format=1 game=%s fingerprint=%s engine=%s" % (game, fingerprint.lower(), engine)
    if vocab:
        line += " vocab=" + vocab.lower()
    return line


def table_bytes(magic, rows, version=3, header=None):
    """The table of [(word, group, class)] (already in table order)."""
    if magic not in MAGICS:
        raise BakeError("magic must be SCIKOR or AGIKOR")
    out = bytearray(magic.encode("ascii").ljust(8, b"\0"))
    out += struct.pack("<II", version, len(rows))
    if version == 3:
        text = header.encode("ascii") + b"\0"
        text += b"\0" * (-len(text) % 4)
        out += struct.pack("<I", len(text)) + text
    elif version != 2:
        raise BakeError("format must be 2 or 3")
    entries, blob = bytearray(), bytearray()
    for word, group, cls in rows:
        u = units(word)
        if not u or any(c > 0xFFFF for c in u):
            raise BakeError("%r is empty or outside the BMP" % word)
        if len(u) > MAX_WORD:
            raise BakeError("%r is %d code units; the reader finds words of at most %d (KoreanStems::kMaxLen)"
                            % (word, len(u), MAX_WORD))
        if not (0 <= group <= 0xFFFF and 0 <= cls <= 0xFFFF):
            raise BakeError("%s: group or class out of range" % word)
        offset = len(blob) // 2
        if version == 2:
            if offset + len(u) > 0xFFFF:
                raise BakeError("the strings need more than 65535 code units: use format 3")
            entries += struct.pack("<HHHH", offset, len(u), group, cls)
        else:
            entries += struct.pack("<IHHHH", offset, len(u), group, cls, 0)
        blob += word.encode("utf-16-le")
    return bytes(out + entries + blob)


def read_table(data, what="table"):
    """(magic, version, header line or None, [(word, group, class)]) of a version 2 or 3 table."""
    if len(data) < 16 or data[6:8] != b"\0\0" or data[:6].decode("ascii", "replace") not in MAGICS:
        raise BakeError("%s: not a Korean parser word table" % what)
    magic = data[:6].decode("ascii")
    version, count = struct.unpack_from("<II", data, 8)
    header = None
    if version == 2:
        at, size, fmt = 16, 8, "<HHHH"
    elif version == 3:
        if len(data) < 20:
            raise BakeError("%s: too small for a version 3 header" % what)
        (hsize,) = struct.unpack_from("<I", data, 16)
        raw = data[20:20 + hsize]
        if hsize == 0 or hsize % 4 or len(raw) != hsize or b"\0" not in raw:
            raise BakeError("%s: bad header size" % what)
        try:
            header = raw[:raw.index(b"\0")].decode("ascii")
        except UnicodeDecodeError:
            raise BakeError("%s: the header text is not ASCII" % what)
        at, size, fmt = 20 + hsize, 12, "<IHHHH"
    else:
        raise BakeError("%s: version %d, only 2 and 3 exist" % (what, version))
    blob = at + count * size
    if blob > len(data):
        raise BakeError("%s: %d entries do not fit" % (what, count))
    rows = []
    for i in range(count):
        f = struct.unpack_from(fmt, data, at + i * size)
        off, ln, group, cls = f[0], f[1], f[2], f[3]
        if blob + (off + ln) * 2 > len(data):
            raise BakeError("%s: entry %d runs past the end" % (what, i))
        if not 0 < ln <= MAX_WORD:
            raise BakeError("%s: entry %d is %d code units; the reader finds words of 1..%d (KoreanStems::kMaxLen)"
                            % (what, i, ln, MAX_WORD))
        rows.append((data[blob + off * 2:blob + (off + ln) * 2].decode("utf-16-le"), group, cls))
    if [units(w) for w, _g, _c in rows] != sorted(units(w) for w, _g, _c in rows):
        raise BakeError("%s: the entries are not sorted by code units" % what)
    return magic, version, header, rows


def master_rows(text):
    """(magic, rows) of a .vocab.tsv master (the harness's trs_master.py text_to_vocab rules); rows in table order."""
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    if not lines or lines[0] != MASTER_TAG:
        raise BakeError("not a Korean parser word master (first line must be: %s)" % MASTER_TAG)
    magic, classes, rows, seen = None, {}, [], set()
    for no, line in enumerate(lines[1:], 2):
        where = "line %d: " % no
        if line.startswith("@magic "):
            magic = line[7:]
            if magic not in MAGICS:
                raise BakeError(where + "@magic SCIKOR or AGIKOR")
        elif line.startswith("@class "):
            f = line.split(" ", 3)
            try:
                classes[int(f[1])] = int(f[2], 16)
            except (IndexError, ValueError):
                raise BakeError(where + "expected @class <group> <class hex> [English words]")
        elif line.startswith("#") or not line:
            continue
        else:
            f = line.split("\t")
            if len(f) not in (2, 3) or not f[0] or f[0] in seen:
                raise BakeError(where + ("%s is on two lines" % f[0] if f[0] in seen else
                                         "expected word TAB groups [TAB English]"))
            seen.add(f[0])
            if any(ord(c) > 0xFFFF for c in f[0]):
                raise BakeError(where + "a character outside the BMP")
            seq = "+" in f[1]
            try:
                groups = [int(g) for g in f[1].split("+" if seq else ",")]
            except ValueError:
                raise BakeError(where + "groups are numbers joined by , (alternatives) or + (a sequence)")
            for k, g in enumerate(groups):
                if g not in classes:
                    raise BakeError(where + "group %d has no @class line" % g)
                rows.append((f[0], g, classes[g] | (SEQ if seq and k + 1 < len(groups) else 0)))
    if magic is None:
        raise BakeError("the master has no @magic line")
    return magic, sort_rows(rows)


def k3map_rows(kmap, targets=None, verbs=None):
    """Rows of a k3map.json map (the harness's k6bake.py rules: classes ORed from targets, AGI verb flag, words not
    typeable as CP949 skipped)."""
    classof = {}
    if targets:
        for g in targets["groups"]:
            acc = 0
            for c in g.get("classes") or []:
                acc |= c
            if g.get("classes"):
                classof[g["id"]] = acc
    if verbs:
        for g in verbs["groups"]:
            classof[g["id"]] = 0x0001 if g.get("verb") else 0
    rows = []
    for word, groups in kmap.items():
        try:
            word.encode("cp949")
        except UnicodeEncodeError:
            continue
        seq = isinstance(groups, str)
        if seq:
            groups = [int(x) for x in groups.split("+")]
        groups = groups if isinstance(groups, list) else [groups]
        for n, group in enumerate(groups):
            cls = classof.get(group, 0)
            if len(word) > 0xFFFF or group > 0xFFFF or cls > 0x7FFF:
                continue
            if seq and n + 1 < len(groups):
                cls |= SEQ
            rows.append((word, group, cls))
    return sort_rows(rows)


def take(args, name, default=None):
    if name in args:
        i = args.index(name)
        if i + 1 >= len(args):
            raise BakeError(name + " needs a value")
        value = args[i + 1]
        del args[i:i + 2]
        return value
    return default


def source_rows(path, args):
    magic = take(args, "--magic")
    targets = take(args, "--targets")
    verbs = take(args, "--verbs")
    with open(path, encoding="utf-8") as f:
        text = f.read()
    if path.endswith(".json"):
        try:
            kmap = json.loads(text)["map"]
            t = json.loads(slurp(targets).decode("utf-8")) if targets else None
            v = json.loads(slurp(verbs).decode("utf-8")) if verbs else None
            return magic or "SCIKOR", k3map_rows(kmap, t, v)
        except (KeyError, TypeError, AttributeError, ValueError) as e:
            raise BakeError("%s: not a usable k3map.json / targets file (%s: %s)" % (path, type(e).__name__, e))
    if magic or targets or verbs:
        raise BakeError("--magic, --targets and --verbs are for a k3map.json")
    return master_rows(text)


def versioned(magic, rows, args):
    try:
        version = int(take(args, "--format", "3"))
    except ValueError:
        raise BakeError("--format must be 2 or 3")
    game = take(args, "--game")
    fingerprint = take(args, "--fingerprint")
    vocab = take(args, "--vocab-md5")
    header = None
    if version == 3:
        if not vocab:
            raise BakeError("format 3 needs --vocab-md5 (the md5 of the game's vocabulary resource, DESIGN.md section 4)")
        header = header_line(game, fingerprint, magic[:3].lower(), vocab)
    elif game or fingerprint or vocab:
        raise BakeError("--game, --fingerprint and --vocab-md5 are for format 3")
    return table_bytes(magic, rows, version, header)


GPL_C = """/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
"""


def c_array(name, data):
    lines = ["static const byte %s[%d] = {" % (name, len(data))]
    for i in range(0, len(data), 16):
        lines.append("\t" + ", ".join("0x%02x" % b for b in data[i:i + 16]) + ",")
    lines.append("};")
    return "\n".join(lines)


def golden(path, args):
    magic, rows = source_rows(path, args)
    game = take(args, "--game")
    fingerprint = take(args, "--fingerprint")
    vocab = take(args, "--vocab-md5")
    v2 = table_bytes(magic, rows, 2)
    v3 = table_bytes(magic, rows, 3, header_line(game, fingerprint, magic[:3].lower(), vocab))
    return GPL_C + "\n".join([
        "",
        "// Generated by tools/korean/k6bake.py golden %s; do not edit." % os.path.basename(path),
        "// The tables of that master as version 2 and as version 3 (common/korean_vocab.h).",
        "",
        "#ifndef TEST_KOREAN_VOCAB_GOLDEN_H",
        "#define TEST_KOREAN_VOCAB_GOLDEN_H",
        "",
        "#include <common/scummsys.h>",
        "",
        c_array("kKoreanVocabGoldenV2", v2),
        "",
        c_array("kKoreanVocabGoldenV3", v3),
        "",
        "#endif",
        "",
    ])


def harness_check(root):
    release = os.path.join(root, "dos", "release")
    want = {}
    with open(os.path.join(release, "translations.tsv"), encoding="utf-8") as f:
        for line in f:
            f5 = line.rstrip("\n").split("\t")
            if len(f5) >= 5 and f5[3].endswith(".vocab.tsv"):
                want[(f5[1], f5[3])] = f5[4]
    bad = 0
    for (game, master), sha in sorted(want.items()):
        _magic, rows = master_rows(slurp(os.path.join(release, game, "translation", master)).decode("utf-8"))
        data = table_bytes(_magic, rows, 2)
        got = hashlib.sha256(data).hexdigest()
        ok = got == sha
        bad += not ok
        print("%-8s %-22s %6d bytes %s %s" % (game, master, len(data), got[:16], "ok" if ok else "DIFFERS, want " + sha))
    return 1 if bad or not want else 0


def main(argv):
    args = list(argv[1:])
    if not args:
        sys.exit(__doc__)
    cmd = args.pop(0)
    try:
        if cmd == "build" and len(args) >= 2:
            src, out = args.pop(0), args.pop(0)
            magic, rows = source_rows(src, args)
            data = versioned(magic, rows, args)
        elif cmd == "convert" and len(args) >= 2:
            src, out = args.pop(0), args.pop(0)
            magic, _version, _header, rows = read_table(slurp(src), src)
            data = versioned(magic, rows, args)
        elif cmd == "dump" and len(args) == 1:
            magic, version, header, rows = read_table(slurp(args[0]), args[0])
            print("@magic %s version %d" % (magic, version))
            if header:
                print(header)
            for w, g, c in rows:
                print("%s\t%d\t0x%04x" % (w, g, c))
            return 0
        elif cmd == "golden" and len(args) >= 1:
            src = args.pop(0)
            sys.stdout.write(golden(src, args))
            return 0
        elif cmd == "harness-check" and len(args) == 1:
            return harness_check(args[0])
        else:
            sys.exit(__doc__)
        if args:
            raise BakeError("unknown arguments: " + " ".join(args))
        # Read it back: a format mistake fails here, not as "Korean does nothing" in the engine.
        back = read_table(data, out)
        if back[3] != rows:
            raise BakeError("read-back differs from the rows written")
        with open(out, "wb") as f:
            f.write(data)
        print("%s: %s version %d, %d entries, %d bytes, sha256 %s" % (out, magic, back[1], len(rows), len(data),
                                                                    hashlib.sha256(data).hexdigest()))
        return 0
    except BakeError as e:
        print("k6bake: %s" % e, file=sys.stderr)
        return 2
    except OSError as e:
        print("k6bake: %s" % e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
