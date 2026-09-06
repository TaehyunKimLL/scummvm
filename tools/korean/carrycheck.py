#!/usr/bin/env python3
"""Does carrying the remainder actually keep a run on the font's metrics?

A proportional font measures in the scaled surface's pixels while the engine
positions text in game pixels, so each character's advance has to be divided
and the fraction is lost. Rounding every character up costs up to (scale - 1)
pixels each; carrying the remainder forward costs at most that once for the
whole run.

This is arithmetic, so it can be checked exactly against the real font files
without launching the game - and unlike a capture it says how big the error
would be on a specific string.

    python3 carrycheck.py tools/korean/indy3kor/hrlat16.fnt 2 "Walk to the door"
    python3 carrycheck.py tools/korean/indy3kor/hrlat16.fnt 2      # built-in strings
"""
import struct
import sys

HEADER = "<4sHHBBHHBBBBHIII"

HANGUL_SAMPLES = [
    "걸어가기",
    "이게 뭐지?",
    "대화하기",
    "인디아나 존스와 최후의 성전",
    "이이이이이이이이",       # narrow syllables: worst case for rounding up
    "뭉뭉뭉뭉뭉뭉뭉뭉",       # wide
    "무엇을 사용하시겠습니까",
]

SAMPLES = [
    "Walk to",
    "Pick up",
    "Look at the mysterious inscription",
    "Indiana Jones and the Last Crusade",
    "iiiiiiiiiiiiiiii",      # narrowest glyphs: worst case for rounding up
    "MMMMMMMMMMMMMMMM",      # widest
    "Illinois militia",
]


def load(path):
    data = open(path, "rb").read()
    h = struct.unpack(HEADER, data[:32])
    if h[0] != b"SVFN":
        raise SystemExit(f"{path}: not an SVFN font")
    flags, count, cell_w, metrics_off = h[2], h[6], h[7], h[12]
    codepage = h[5]
    if not (flags & 1):
        raise SystemExit(f"{path}: fixed width - nothing to carry")
    adv = {}
    for i in range(count):
        base = metrics_off + i * 4
        advance = data[base]
        bearing = struct.unpack("<b", data[base + 1:base + 2])[0]
        ink = data[base + 2]
        # Ink reaching past the advance has to be cleared or glyphs collide.
        adv[i] = max(advance, bearing + ink)
    return adv, cell_w, codepage


def cp949_index(ch):
    """Glyph index for a Hangul syllable, matching legacyGlyphIndex().

    KS X 1001 runs 2350 syllables in ku/ten order, which is what the fonts are
    baked in and what the engine looks up.
    """
    try:
        raw = ch.encode("cp949")
    except UnicodeEncodeError:
        return None
    if len(raw) != 2:
        return None
    hi, lo = raw[0], raw[1]
    if not (0xB0 <= hi <= 0xC8):
        return None
    if not (0xA1 <= lo <= 0xFE):
        return None
    return (hi - 0xB0) * 94 + (lo - 0xA1)


def run(text, adv, scale, index_of=None):
    """Return (rounded-up total, carried total, exact total) in game pixels."""
    up = 0
    carried = 0
    carry = 0
    exact = 0

    for ch in text:
        if index_of:
            i = index_of(ch)
            if i is None:
                continue
            a = adv.get(i, 0)
        else:
            a = adv.get(ord(ch), 0)
        if a <= 0:
            continue
        exact += a

        up += (a + scale - 1) // scale

        total = a + carry
        whole = total // scale
        carry = total - whole * scale
        if whole < 1:
            whole, carry = 1, 0
        carried += whole

    return up, carried, exact


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1

    path, scale = sys.argv[1], int(sys.argv[2])
    texts = sys.argv[3:] or SAMPLES

    adv, cell, codepage = load(path)

    # A CP949 font is indexed by KS X 1001 order, not by code point.
    index_of = cp949_index if codepage == 949 else None
    if index_of and len(sys.argv) < 4:
        texts = HANGUL_SAMPLES

    print(f"{path}  cell {cell}px  scale {scale}"
          f"{'  (CP949)' if index_of else ''}")
    print()
    print(f"{'string':<36} {'exact':>7} {'round-up':>9} {'carried':>8} "
          f"{'err up':>7} {'err carry':>10}")
    print(f"{'-' * 36} {'-' * 7} {'-' * 9} {'-' * 8} {'-' * 7} {'-' * 10}")

    worst_up = worst_carry = 0
    for text in texts:
        up, carried, exact = run(text, adv, scale, index_of)
        # Compare in surface pixels so the numbers mean the same thing.
        want = exact
        err_up = up * scale - want
        err_carry = carried * scale - want
        worst_up = max(worst_up, abs(err_up))
        worst_carry = max(worst_carry, abs(err_carry))
        shown = text if len(text) <= 34 else text[:31] + "..."
        print(f"{shown:<36} {exact:>7} {up * scale:>9} {carried * scale:>8} "
              f"{err_up:>+7} {err_carry:>+10}")

    print()
    print(f"worst error, rounding up: {worst_up} surface pixels")
    print(f"worst error, carrying:    {worst_carry} surface pixels")
    print()
    print(f"Carrying cannot exceed scale-1 = {scale - 1} for a whole run, "
          f"however long;")
    print("rounding up grows with the number of characters.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
