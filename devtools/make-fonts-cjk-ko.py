#!/usr/bin/env python3
"""Build a Korean-only fonts-cjk.dat.

ScummVM's fonts-cjk.dat carries Noto Sans for Japanese, Simplified and
Traditional Chinese and Korean (about 38 MB). This branch ships the Korean
faces only, cut to what Korean text in KS X 1001 needs: ASCII, Latin-1,
general punctuation and the KS X 1001 repertoire (the 2350 Hangul syllables,
the jamo, the symbols and the Hanja). The GUI's Korean translation stays
within it, and so do the Korean game translations the hi-res text maps
draw; the maps can name these faces as data:NotoSansKR-Bold.otf.

Needs fontTools (pyftsubset).

    make-fonts-cjk-ko.py [SRC_DIR] [DEST_FILE]

SRC_DIR defaults to dists/engine-data/fonts/fonts-cjk, DEST_FILE to
dists/engine-data/fonts-cjk.dat (both relative to this script's tree).
"""
import os
import subprocess
import sys
import tempfile
import zipfile

FACES = ("NotoSansKR-Regular.otf", "NotoSansKR-Bold.otf")


def repertoire():
    chars = set(chr(c) for c in range(0x20, 0x7F))
    chars |= set(chr(c) for c in range(0xA0, 0x100))
    chars |= set(chr(c) for c in range(0x2000, 0x2070))
    for hi in range(0xA1, 0xFF):
        for lo in range(0xA1, 0xFF):
            try:
                chars.add(bytes([hi, lo]).decode("euc-kr"))
            except UnicodeDecodeError:
                pass
    return "".join(sorted(chars))


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "dists", "engine-data", "fonts", "fonts-cjk")
    dest = sys.argv[2] if len(sys.argv) > 2 else os.path.join(root, "dists", "engine-data", "fonts-cjk.dat")
    with tempfile.TemporaryDirectory() as tmp:
        text = os.path.join(tmp, "chars.txt")
        with open(text, "w", encoding="utf-8") as f:
            f.write(repertoire())
        out = []
        for face in FACES:
            sub = os.path.join(tmp, face)
            subprocess.run(["pyftsubset", os.path.join(src, face), "--text-file=" + text,
                            "--output-file=" + sub, "--layout-features=*", "--name-IDs=*",
                            "--name-legacy", "--name-languages=*"], check=True)
            out.append(sub)
        with zipfile.ZipFile(dest, "w", zipfile.ZIP_DEFLATED) as zf:
            for path in out:
                zf.write(path, os.path.basename(path))
                print("    Adding file: ./" + os.path.basename(path))
            version = os.path.join(src, "version.txt")
            if os.path.exists(version):
                zf.write(version, "version.txt")
    print("%s: %d bytes" % (dest, os.path.getsize(dest)))


if __name__ == "__main__":
    main()
