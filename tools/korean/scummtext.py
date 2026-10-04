#!/usr/bin/env python3
"""Extract a SCUMM v3/v4 EUC-KR patch's strings (XOR 0x69 DISK0N.LEC, 000.LFL, 90N.LFL, TENTACLE.00N) for mkfont.py --chars-from.

Usage: scummtext.py <gamedir> <out.txt>
"""

import os
import re
import sys

XOR_KEY = 0x69

MAX_RUN = 4096

FILE_PATTERNS = (
    re.compile(r"^disk\d+\.lec$", re.IGNORECASE),
    re.compile(r"^000\.lfl$", re.IGNORECASE),
    re.compile(r"^90\d\.lfl$", re.IGNORECASE),
    re.compile(r"^tentacle\.00\d$", re.IGNORECASE),
)

HANGUL_SYLLABLES = (0xAC00, 0xD7A3)
HANGUL_JAMO = (0x3131, 0x318E)   # compatibility jamo (ㄱ..ㅣ)


def is_hangul(ch):
    cp = ord(ch)
    return HANGUL_SYLLABLES[0] <= cp <= HANGUL_SYLLABLES[1] or \
        HANGUL_JAMO[0] <= cp <= HANGUL_JAMO[1]


def _escape_total_len(code):
    return 2 + (0 if code in (1, 2, 3, 8) else 2)


def find_patch_files(gamedir):
    names = sorted(os.listdir(gamedir))
    return [os.path.join(gamedir, n) for n in names
            if any(p.match(n) for p in FILE_PATTERNS)]


def decrypt(data):
    return bytes(b ^ XOR_KEY for b in data)


def scan_strings(data):
    """Hangul-bearing NUL-terminated text runs in decrypted data, escapes dropped."""
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
        sys.exit(f"scummtext: {gamedir} 에서 DISK0N.LEC/000.LFL/90N.LFL/TENTACLE.00N 을 "
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
