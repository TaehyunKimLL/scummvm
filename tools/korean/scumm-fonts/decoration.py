#!/usr/bin/env python3
"""Read each original 2bpp SCUMM charset's decoration off the game's own font resources.

  decoration.py                 print the table for every game found
  decoration.py --check         exit 1 when original-decoration.json disagrees
  decoration.py --write         rewrite original-decoration.json

A 2bpp SCUMM face draws the letter in colour 1 and its decoration in colour 2.
Each font is scored against models of where colour 2 should be:
  drop (dx, dy)        the letter shifted by (dx, dy), less the letter itself;
  outline l, u, r, d   the letter grown by a box reaching l px left, u up,
                       r right, d down, less the letter (Indy4 cs3 is 2,1,1,2).
The model whose pixels best match colour 2 over the face's letters and digits
(Jaccard index) is the face's decoration, in game pixels, the form
pairing2svf.py and the font-pairing page read (see classify): a drop
{"d": [dx, dy]}, an outline {"o": r}, or both {"o": r, "d": [dx, dy]}. A face
with no colour 2 (every 1bpp one) is {"none": true}: the classic renderer draws
its Latin text undecorated, whatever the Korean patch byte says.

Resources (CharsetRendererClassic, see scumm_fontmeasure notes in
docs/johab/2026-10-14-font-pairing.md):
  v4   charset N is 90N.LFL, plain; the font starts at byte 21.
  v5/6 CHAR blocks in the XOR 0x69 data file; the charset id comes from the
       index file's DCHR table (room, offset) and the data file's LOFF.
  font: u8 bpp, u8 height, u16 numChars, u32 offsets[numChars] from the font
  start (0 = absent); glyph: u8 w, u8 h, s8 xoff, s8 yoff, w*h pixels, MSB first.

Game folders resolve against $GAMEDATA (default ~/work/scummvm/gamedata); Indy4
against $INDY4 (the unpacked Multi-Platform zip). A game whose files are absent
is skipped and reported.
"""

import argparse
import glob
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
JSON = os.path.join(HERE, "original-decoration.json")
MIN_SCORE = 0.5     # below this neither model explains colour 2: report, do not guess

G = os.environ.get("GAMEDATA", os.path.expanduser("~/work/scummvm/gamedata"))
I4 = os.environ.get("INDY4", os.path.join(G, "indy4", "Indiana Jones and the Fate of Atlantis (Multi-Platform)"))
M2 = os.path.join(G, "Monkey Island 2 LeChuck's Revenge (Multi-Platform)",
                  "Monkey Island 2 LeChuck's Revenge (Multi-Platform)", "Monkey Island 2 LeChuck's Revenge (DOS VGA)")
DT = os.path.join(G, "dott", "Day Of the Tentacle (DOS Floppy)")
I4CD = os.path.join(I4, "Indiana Jones and the Fate of Atlantis (DOS CD)")
I4FL = os.path.join(I4, "Indiana Jones and the Fate of Atlantis (DOS Floppy)")
# game (as english.json names it), kind, files
GAMES = [
    ("MI1 EGA/VGA floppy", "v4", os.path.join(G, "mi1vga", "90?.LFL")),
    ("MI1 CD", "v5", (os.path.join(G, "mi1en", "MONKEY1.000"), os.path.join(G, "mi1en", "MONKEY1.001"))),
    ("MI2 DOS VGA", "v5", (os.path.join(M2, "MONKEY2.000"), os.path.join(M2, "MONKEY2.001"))),
    ("Indy4 DOS CD", "v5", (os.path.join(I4CD, "ATLANTIS.000"), os.path.join(I4CD, "ATLANTIS.001"))),
    ("Indy4 DOS floppy", "v5", (os.path.join(I4FL, "ATLANTIS.000"), os.path.join(I4FL, "ATLANTIS.001"))),
    ("Loom CD (VGA talkie)", "v4", os.path.join(G, "loomkor", "90?.LFL")),
    ("DOTT DOS floppy", "v6", (os.path.join(DT, "TENTACLE.000"), os.path.join(DT, "TENTACLE.001"))),
]


# ---- classification -------------------------------------------------------

def _sets(rows):
    one, two = set(), set()
    for y, r in enumerate(rows):
        for x, v in enumerate(r):
            if v == 1:
                one.add((x, y))
            elif v == 2:
                two.add((x, y))
    return one, two


def _box(one, left, up, right, down):
    """The letter grown by a box reaching left/up/right/down, less the letter."""
    row = {(x + i, y) for x, y in one for i in range(-left, right + 1)}
    return {(x, y + j) for x, y in row for j in range(-up, down + 1)} - one


# Simplest first: on a tie the earlier model wins.
MODELS = [("d", (dx, dy)) for dy in range(-3, 4) for dx in range(-3, 4) if dx or dy] + \
         [("o", (r,) * 4) for r in (1, 2, 3)] + \
         [("o", b) for b in ((l, u, r, d) for l in range(4) for u in range(4) for r in range(4) for d in range(4))
          if len(set(b)) > 1 and max(b) > 0]


def scores(glyphs):
    """{model: Jaccard index of its pixels against colour 2}, over every glyph with both colours."""
    inter = dict.fromkeys(MODELS, 0)
    union = dict.fromkeys(MODELS, 0)
    for rows in glyphs:
        one, two = _sets(rows)
        if not one or not two:
            continue
        for m in MODELS:
            want = {(x + m[1][0], y + m[1][1]) for x, y in one} - one if m[0] == "d" else _box(one, *m[1])
            inter[m] += len(want & two)
            union[m] += len(want | two)
    return {m: inter[m] / union[m] for m in MODELS if union[m]}


def classify(glyphs):
    """The face's decoration in game px, or None when it has no colour 2 or no
    model explains it (score below MIN_SCORE):
      {"d": [dx, dy]}               a drop: the letter shifted;
      {"d": [dx, dy], "fill": true} a filled drop: the letter swept to the shift
                                    (a box grown one way, DOTT cs5);
      {"o": r}                      an outline r px on every side;
      {"o": r, "d": [dx, dy]}       an outline r plus a drop - a box thicker on
                                    two sides, as Indy4 cs3's (-1, +1): the
                                    engine's stroke."""
    s = scores(glyphs)
    if not s:
        return None
    kind, arg = max(s, key=s.get)
    if s[(kind, arg)] < MIN_SCORE:
        return None
    if kind == "d":
        return {"d": list(arg)}
    left, up, right, down = arg
    m = min(arg)
    drop = [right - left, down - up]
    if not m:
        return {"d": drop, "fill": True}
    return {"o": m, "d": drop} if any(drop) else {"o": m}


def describe(d):
    if d.get("none"):
        return "none"
    if "o" in d and "d" in d:
        return "outline %d + drop %d,%d" % (d["o"], *d["d"])
    if d.get("fill"):
        return "filled drop %d,%d" % tuple(d["d"])
    return "outline %d" % d["o"] if "o" in d else "drop %d,%d" % tuple(d["d"])


# ---- reading the resources ------------------------------------------------

def classic_glyphs(font):
    """(bpp, {code: rows}) of a CharsetRendererClassic font blob."""
    bpp, n = font[0], struct.unpack_from("<H", font, 2)[0]
    out = {}
    for c in range(n):
        o = struct.unpack_from("<I", font, 4 + 4 * c)[0]
        if not o or o + 4 > len(font):
            continue
        w, h = font[o], font[o + 1]
        nbits = w * h * bpp
        data = font[o + 4:o + 4 + (nbits + 7) // 8]
        if not w or not h or len(data) * 8 < nbits:
            continue
        bits = int.from_bytes(data, "big")
        tot = len(data) * 8
        mask = (1 << bpp) - 1
        out[c] = [[(bits >> (tot - (y * w + x + 1) * bpp)) & mask for x in range(w)] for y in range(h)]
    return bpp, out


def read_v4(paths):
    """{"csN": (bpp, glyphs)} from 90N.LFL files."""
    faces = {}
    for p in sorted(paths):
        name = os.path.basename(p).upper()
        if not (name.startswith("90") and name.endswith(".LFL")):
            continue
        faces["cs" + name[2:-4]] = classic_glyphs(open(p, "rb").read()[21:])
    return faces


def _blocks(b):
    p, out = 0, {}
    while p + 8 <= len(b):
        tag, size = b[p:p + 4], struct.unpack_from(">I", b, p + 4)[0]
        if not tag.isalpha() or size < 8 or p + size > len(b):
            break
        out[tag.decode()] = b[p + 8:p + size]
        p += size
    return out


def read_v56(index, data, key=0x69):
    """{"csN": (bpp, glyphs)} from the CHAR blocks the index's DCHR table names."""
    ib = bytes(x ^ key for x in open(index, "rb").read())
    dchr = _blocks(ib)["DCHR"]
    n = struct.unpack_from("<H", dchr, 0)[0]
    rooms = list(dchr[2:2 + n])
    offs = struct.unpack_from("<%dI" % n, dchr, 2 + n)
    db = bytes(x ^ key for x in open(data, "rb").read())
    # LECF > LOFF: u8 count, then (u8 room, u32 offset of the room's block)
    lo = db.find(b"LOFF")
    cnt = db[lo + 8]
    loff = {db[lo + 9 + 5 * i]: struct.unpack_from("<I", db, lo + 10 + 5 * i)[0] for i in range(cnt)}
    faces = {}
    for cid, (room, off) in enumerate(zip(rooms, offs)):
        if room not in loff or not off:
            continue
        p = loff[room] + off
        if db[p:p + 4] != b"CHAR":
            continue
        faces["cs%d" % cid] = classic_glyphs(db[p + 8 + 21:])   # 8 block header + 21 = 6+15 colour map
    return faces


def read_game(kind, files):
    if kind == "v4":
        paths = glob.glob(files)
        return read_v4(paths) if paths else None
    if not all(os.path.exists(f) for f in files):
        return None
    return read_v56(*files)


def face_decoration(bpp, glyphs):
    """A charset's decoration: classify() on a 2bpp face's letters; {"none": True}
    for a face with no colour 2 - a 1bpp one, which CharsetRendererClassic draws
    unshadowed (only the patch's double-byte glyphs get the patch byte's
    decoration); None when colour 2 is there but no model explains it."""
    rows = letters(glyphs)
    if bpp != 2 or not any(v == 2 for g in rows for r in g for v in r):
        return {"none": True}
    return classify(rows)


def letters(glyphs):
    return [rows for c, rows in glyphs.items() if chr(c).isalnum() and c < 0x80]


def measure(games=GAMES):
    """({game: {font id: decoration}}, [games skipped])."""
    table, skipped = {}, []
    for game, kind, files in games:
        faces = read_game(kind, files)
        if faces is None:
            skipped.append(game)
            continue
        row = {}
        for fid, (bpp, glyphs) in faces.items():
            d = face_decoration(bpp, glyphs)
            if d:
                row[fid] = d
        table[game] = dict(sorted(row.items(), key=lambda kv: int(kv[0][2:])))
    return table, skipped


def dump(table):
    return "{\n" + ",\n".join(" %s: %s" % (json.dumps(g), json.dumps(r)) for g, r in table.items()) + "\n}\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", action="store_true", help="compare with original-decoration.json")
    ap.add_argument("--write", action="store_true", help="rewrite original-decoration.json")
    a = ap.parse_args()
    table, skipped = measure()
    for g in skipped:
        print("skipped (files not found): %s" % g, file=sys.stderr)
    for g, row in table.items():
        print("%-22s %s" % (g, "  ".join("%s %s" % (f, describe(d)) for f, d in row.items()) or "-"))
    if a.write:
        old = json.load(open(JSON)) if os.path.exists(JSON) else {}
        old.update(table)
        open(JSON, "w").write(dump(old))
    if a.check:
        old = json.load(open(JSON))
        bad = [g for g in table if old.get(g) != table[g]]
        for g in bad:
            print("DIFFERS %s: json %s, resources %s" % (g, old.get(g), table[g]))
        return 1 if bad else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
