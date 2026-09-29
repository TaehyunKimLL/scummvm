#!/usr/bin/env python3
"""A SCUMM v3/v4 EUC-KR patch's own strings, extracted from its raw resource
files - for a fan patch that never shipped a .trs bundle (the DUMB Monkey
Island VGA floppy patch, mi1kop), so mkfont.py's --chars-from has something
to read for it.

The patch's resources (DISK0N.LEC room bundles, 000.LFL index, 90N.LFL
object-name tables) are XOR 0x69 obfuscated, like every classic SCUMM v3/v4
resource (ScummEngine_v3old::decrypt... uses the same key for these games).
Nothing in the format says where a string starts or how long it is once
decrypted - this is the same difficulty --chars-from's TEXT.nnn/*.trs readers
don't have, since those formats carry their own lengths. Here we have only
the byte stream, so we scan it for runs that could not be anything else: a
NUL-terminated span whose every byte is printable ASCII, a valid EUC-KR
two-byte pair (KS X 1001's A1-FE x A1-FE), or a SCUMM control code (0xFF +
1 code byte + resStrLen()'s argument count - see mkfont.py's
_res_str_len()/harness/tools/trslib.py, the same rule the .trs reader uses),
with at least one Hangul syllable or jamo pair in it (a run with none is
almost certainly binary data that happens to parse - see below).

This is a heuristic, not a parser: nothing here understands SCUMM opcodes, so
a byte run that is not text can still happen to satisfy the grammar (most do
not - EUC-KR's high bytes are dense enough that random bytes rarely form
enough valid pairs in a row to look like a real word, but a short accidental
match, one or two syllables amid binary data, does happen and shows up in the
output as noise). Comment the false positives out of the collected file by
hand, or just let mkfont.py bake them: an extra, wrong glyph a game never
actually uses does not hurt the font, and --chars-from's job is recall
(finding every glyph the game needs), not precision. Verified against Task
2's HRTEXT log (docs/superpowers/sdd/2026-09-29-dos-m5-scumm/task-6-brief.md
Step 2): the DUMB patch is a different (2005) translation of the same game
than the Ultimate Talkie .trs, so exact strings differ, but scanning all of
mi1kor's DISK0N.LEC/000.LFL/90N.LFL finds every Hangul syllable the census's
mi1ko 60 s run and its lookout-tower monologue drew.

Usage:
  python3 scummtext.py <gamedir> <out.txt>
"""

import os
import re
import sys

XOR_KEY = 0x69

# A run this long without hitting a terminating NUL cannot be real dialogue
# (MI1's longest lines are a few hundred bytes): capping the inner scan keeps
# a long accidental run of printable-looking binary bytes from making the
# whole file quadratic.
MAX_RUN = 4096

# The three resource kinds a DUMB-style SCUMM v3/v4 Korean patch carries text
# in: the room bundles, the main index, and the object/verb name tables.
FILE_PATTERNS = (
    re.compile(r"^disk\d+\.lec$", re.IGNORECASE),
    re.compile(r"^000\.lfl$", re.IGNORECASE),
    re.compile(r"^90\d\.lfl$", re.IGNORECASE),
)

HANGUL_SYLLABLES = (0xAC00, 0xD7A3)
HANGUL_JAMO = (0x3131, 0x318E)   # compatibility jamo (ㄱ..ㅣ)


def is_hangul(ch):
    cp = ord(ch)
    return HANGUL_SYLLABLES[0] <= cp <= HANGUL_SYLLABLES[1] or \
        HANGUL_JAMO[0] <= cp <= HANGUL_JAMO[1]


def _escape_total_len(code):
    """mkfont.py's _res_str_len()/harness/tools/trslib.py's res_str_len():
    codes 1, 2, 3, 8 take no operand bytes, everything else takes two -
    plus the marker (0xFF) and the code byte itself."""
    return 2 + (0 if code in (1, 2, 3, 8) else 2)


def find_patch_files(gamedir):
    names = sorted(os.listdir(gamedir))
    return [os.path.join(gamedir, n) for n in names
            if any(p.match(n) for p in FILE_PATTERNS)]


def decrypt(data):
    return bytes(b ^ XOR_KEY for b in data)


def scan_strings(data):
    """Every NUL-terminated run in `data` (already decrypted) that is made
    up only of printable ASCII (0x20-0x7E), EUC-KR two-byte pairs (A1-FE x
    A1-FE, decoded with cp949) and SCUMM control codes (0xFF + argument
    bytes, skipped rather than decoded), and holds at least one Hangul
    syllable or compatibility jamo. Returned as decoded str, control codes
    dropped (they are not glyphs - see mkfont.py's chars_from_trs()).

    A tokenizer, not a splitter: split(b'\\x00') would glue a real string to
    whatever binary bytes sit between the previous NUL and it, which almost
    always breaks the grammar and drops the string. Scanning for where a
    valid run *starts* - trying every offset, resuming one byte past a
    failure - finds strings regardless of what precedes them.
    """
    n = len(data)
    out = []
    i = 0
    while i < n:
        start = i
        j = i
        has_hangul = False
        broke_on_nul = False
        while j < n and j - start < MAX_RUN:
            b = data[j]
            if b == 0:
                broke_on_nul = True
                break
            if b == 0xFF:
                if j + 1 >= n:
                    break
                length = _escape_total_len(data[j + 1])
                if j + length > n:
                    break
                j += length
                continue
            if 0x20 <= b <= 0x7E:
                j += 1
                continue
            if 0xA1 <= b <= 0xFE:
                if j + 1 >= n or not (0xA1 <= data[j + 1] <= 0xFE):
                    break
                try:
                    ch = bytes((b, data[j + 1])).decode("cp949")
                except UnicodeDecodeError:
                    break
                if len(ch) != 1:
                    break
                has_hangul = has_hangul or is_hangul(ch)
                j += 2
                continue
            break
        if broke_on_nul and j > start and has_hangul:
            raw = bytearray()
            k = start
            while k < j:
                if data[k] == 0xFF:
                    k += _escape_total_len(data[k + 1])
                    continue
                raw.append(data[k])
                k += 1
            out.append(raw.decode("cp949"))
            i = j + 1
            continue
        i = start + 1
    return out


def main():
    if len(sys.argv) != 3:
        sys.exit(f"usage: {sys.argv[0]} <gamedir> <out.txt>")
    gamedir, out_path = sys.argv[1], sys.argv[2]

    files = find_patch_files(gamedir)
    if not files:
        sys.exit(f"scummtext: {gamedir} 에서 DISK0N.LEC/000.LFL/90N.LFL 을 "
                  f"찾을 수 없다")

    strings = []
    for path in files:
        with open(path, "rb") as f:
            data = decrypt(f.read())
        found = scan_strings(data)
        strings.extend(found)
        print(f"  {path}: {len(found)}", file=sys.stderr)

    hangul = set()
    other = set()
    for s in strings:
        for ch in s:
            if ord(ch) < 0x80:
                continue
            if is_hangul(ch):
                hangul.add(ch)
            else:
                other.add(ch)

    with open(out_path, "w", encoding="utf-8") as f:
        for s in strings:
            f.write(s + "\n")
        f.write(f"# scummtext: {len(strings)} strings, {len(hangul)} distinct "
                 f"Hangul, {len(other)} distinct other\n")

    print(f"{out_path}: {len(strings)} strings, {len(hangul)} distinct Hangul, "
          f"{len(other)} distinct other")


if __name__ == "__main__":
    main()
