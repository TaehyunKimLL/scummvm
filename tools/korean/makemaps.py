#!/usr/bin/env python3
"""Write one hi-res font map (version 2) per game, from what the font files
actually are.

Maps were being hand-written and drifting: i3-multi still pointed at a map in
the older TrueType format, so the legacy loader drew the text while the new
reader supplied only the scale - two systems laying out one screen, which looks
like a font bug and is not one.

This reads every .fnt in a game folder, groups the ones that form a numbered
set, and writes HIRESTXT.MAP (the name a game folder's map is found by) naming
them, one [font.N] per charset. Choices are recorded in the file itself so the
next reader does not have to guess.

A folder that already has a version 1 map (hires_text.map, hires_text.map.v1
- the old map set aside - or a HIRESTXT.MAP without [map] version=2) keeps
its hand-made choices: scale, alpha, the code
page, which set and Latin companion it names, metrics, [glyphs] and [shadow]
are carried over in their version 2 spelling (graphics/hires_text/README.md,
"The old Latin modes as recipes"); only the numbered file patterns are expanded
from the files that exist. --fresh ignores the old map.

    python3 makemaps.py                 # every known game folder
    python3 makemaps.py ~/games/mi2kor  # just one
    python3 makemaps.py --fresh ~/games/mi2kor
    python3 makemaps.py --convert OLD.map [--out NEW.map]
                                        # one version 1 map, its folder's fonts
"""
import argparse
import glob
import os
import re
import struct
import sys

HEADER = "<4sHHBBHHBBBBHIII"
HEADER_SIZE = struct.calcsize(HEADER)
MAP_NAME = "HIRESTXT.MAP"
OLD_MAP_NAME = "hires_text.map"

# Which set to prefer when a folder carries several. Earlier wins.
# The reasoning, from measuring the files:
#   svfn*  the set baked to match each of the game's own charsets - the safe
#          default, since every charset the game switches to has a font
#   vj/lm  proportional sets; better looking, but only for games whose scripts
#          tolerate different advances (see the advance= key)
#   jm/aa  alternative typefaces at the same sizes
#   uni*   1bpp stencil sets, for comparison against the anti-aliased ones
# "hr" first: those are baked by ~/games/bakecells.sh at exactly
# height * scale for this game, so they sit on the layout grid. The rest were
# baked by hand at whatever size and only fit by luck.
PREFERENCE = ["hr", "svfn", "vj", "lm", "jm", "aa", "jangmi", "uni24_", "uni"]

GAMES = {
    "mi2kor": ("monkey2", "Monkey Island 2"),
    "indy3kor": ("indy3", "Indiana Jones 3"),
    "indy4kor": ("atlantis", "Indiana Jones 4"),
    "loomkor": ("loom", "Loom"),
    "loomtowns": ("loom", "Loom (FM-Towns)"),
    "zakkor": ("zak", "Zak McKracken"),
    "mi1kor": ("monkey", "Monkey Island 1"),
    "mmkor": ("maniac", "Maniac Mansion"),
    "digkor": ("dig", "The Dig"),
}


def describe(path):
    """Read an SVFN header; None when the file is not one."""
    try:
        raw = open(path, "rb").read(HEADER_SIZE)
    except OSError:
        return None
    if len(raw) < HEADER_SIZE:
        return None

    (magic, ver, flags, bpp, shadow, codepage, count, cellW, cellH,
     ascent, _r0, _r1, metricsOff, dataOff, dataSize) = struct.unpack(HEADER, raw)
    if magic != b"SVFN":
        return None

    return {
        "cell": (cellW, cellH),
        "bpp": bpp,
        "glyphs": count,
        "codepage": codepage,
        "proportional": bool(flags & 1),
    }


def scan(folder):
    """Group the SVFN fonts in a folder into numbered sets and Latin extras."""
    sets = {}       # prefix -> {index: (name, info)}
    latin = {}      # prefix -> (name, info)
    unindexed = {}  # stem -> (name, info), CJK fonts with no number

    for path in sorted(glob.glob(os.path.join(folder, "*.fnt"))):
        info = describe(path)
        if not info:
            continue
        name = os.path.basename(path)
        stem = name[:-4]

        m = re.match(r"^([a-zA-Z_]+?)(\d+)$", stem)
        if m and info["glyphs"] > 1000:
            prefix, idx = m.group(1), int(m.group(2))
            sets.setdefault(prefix, {})[idx] = (name, info)
        elif info["glyphs"] <= 512:
            # A 256-glyph font is the Latin companion to a CJK set.
            latin[stem] = (name, info)
        elif info["glyphs"] > 1000:
            # A CJK font whose name carries no index: The Dig ships korean.fnt
            # and korean_g.fnt, which bake to hr0.fnt and hrg.fnt. hrg matched
            # neither branch above and vanished from the map silently - the
            # exact "loads zero fonts" failure this script exists to prevent.
            unindexed[stem] = (name, info)

    return sets, latin, unindexed


def original_heights(folder):
    """Heights of the game's own korean*.fnt, which define the layout grid.

    The engine reads these into _2byteWidth/_2byteHeight and the scripts lay
    text out on them, so they - not the replacement font - decide how much room
    each character gets. Byte 2 of the old format is the height.
    """
    heights = set()
    for name in sorted(os.listdir(folder)):
        if not name.startswith("korean") or not name.endswith(".fnt"):
            continue
        try:
            with open(os.path.join(folder, name), "rb") as fh:
                head = fh.read(4)
        except OSError:
            continue
        # The old format starts with a version byte of 2; SVFN files start
        # with the magic and are the replacements, not the originals.
        if len(head) >= 3 and head[:4] != b"SVFN" and head[0] == 2:
            heights.add(head[2])
    return heights


def expand_pattern(folder, pattern):
    """{index: file name} for the files in folder that a printf pattern such
    as hr%02d.fnt names (version 2 maps have no patterns: one entry per
    charset that has a file)."""
    m = re.match(r"^(.*)%0?(\d*)d(.*)$", pattern)
    if not m:
        return {}
    width = m.group(2)
    digits = r"\d{%s}" % width if width else r"\d+"
    rx = re.compile("^" + re.escape(m.group(1)) + "(" + digits + ")" + re.escape(m.group(3)) + "$", re.I)
    found = {}
    for name in sorted(os.listdir(folder)):
        mm = rx.match(name)
        if mm:
            found[int(mm.group(1))] = name
    return found


# ---- the model both paths fill, and its version 2 spelling ---------------------

def new_model(title):
    return {
        "title": title,
        "notes": [],          # comment lines for the head of the file
        "scale": None,
        "scale_notes": [],
        "blend": None,        # auto | off
        "encoding": None,
        "missing": None,
        "face": None,         # map-wide face (one file for every charset)
        "faces": {},          # charset -> file
        "latin": None,        # map-wide Latin face
        "latins": {},         # charset -> Latin face
        "latin_rule": None,   # original | same (a [latin] mode without a face)
        "advance": None,      # id-wide advance
        "latin_advance": None,
        "latin_origin": None,
        "glyphs": [],         # (section name, [(key, value)])
        "shadow": [],         # [(key, value)]
        "skipped": [],        # comment lines: what was left out and why
    }


def check_cells(folder, model):
    """Drop a Latin face whose cell height differs from its charset's face:
    a face on an id must share the id's cell height, or the loader refuses it
    (and warns once per load)."""
    def height(name):
        info = describe(os.path.join(folder, name)) if name else None
        return info["cell"][1] if info else None
    for cs, lat in sorted(model["latins"].items()):
        face = model["faces"].get(cs, model["face"])
        hf, hl = height(face), height(lat)
        if hf and hl and hf != hl:
            model["skipped"].append("%s: %d px cell, not the %d px of %s - left out (a face on an id "
                                    "shares its cell height)" % (lat, hl, hf, face))
            del model["latins"][cs]
    if model["latin"]:
        bad = [f for f in [model["face"]] + list(model["faces"].values())
               if f and height(f) and height(model["latin"]) and height(f) != height(model["latin"])]
        if bad:
            model["skipped"].append("%s: %d px cell, not the %s of %s - left out (a face on an id shares its "
                                    "cell height)" % (model["latin"], height(model["latin"]),
                                                      "/".join("%d px" % height(f) for f in bad), ", ".join(bad)))
            model["latin"] = None


def emit(model):
    """The version 2 map text for a model."""
    L = list(model["notes"])
    L += ["", "[map]", "version=2", ""]

    render = []
    if model["scale"] is not None:
        render += model["scale_notes"] + ["scale=%d" % model["scale"]]
    if model["blend"] is not None:
        if model["blend"] == "auto":
            render.append("# blend glyph coverage into the background (a true-colour screen)")
        else:
            render.append("# no blending: hard-edged glyphs on an 8-bit screen")
        render.append("blend=%s" % model["blend"])
    if render:
        L += ["[render]"] + render + [""]

    if model["encoding"]:
        L += ["[text]", "encoding=%s" % model["encoding"], ""]

    font = []
    if model["face"]:
        font.append("face=%s" % model["face"])
    if model["missing"]:
        font.append("missing=%s" % model["missing"])
    if model["advance"]:
        if model["advance"] == "game":
            font.append("# the game's own widths: its scripts lay text out on them")
        font.append("advance=%s" % model["advance"])
    if model["latin"]:
        font.append("# ASCII from the Latin companion, ahead of the Korean face")
        font.append("range.basic-latin=%s" % model["latin"])
    elif model["latin_rule"]:
        font.append("range.basic-latin=%s" % model["latin_rule"])
    if model["latin_advance"]:
        font.append("advance.basic-latin=%s" % model["latin_advance"])
    if model["latin_origin"]:
        font.append("origin.basic-latin=%s" % model["latin_origin"])
    if font:
        L += ["[font]"] + font + [""]

    if model["latins"] and not model["latin"]:
        L.append("# [font.N] range.basic-latin: one Latin face per charset, matching each CJK cell")
    for cs in sorted(set(model["faces"]) | set(model["latins"])):
        L.append("[font.%d]" % cs)
        if cs in model["faces"]:
            L.append("face=%s" % model["faces"][cs])
        if cs in model["latins"]:
            L.append("range.basic-latin=%s" % model["latins"][cs])
        L.append("")

    for name, rows in model["glyphs"]:
        L.append("[%s]" % name)
        L += ["%s = %s" % kv for kv in rows]
        L.append("")
    if model["shadow"]:
        L.append("[shadow]")
        L += ["%s=%s" % kv for kv in model["shadow"]]
        L.append("")
    if model["skipped"]:
        L += ["# left out:"] + ["#   " + s for s in model["skipped"]] + [""]
    while L and L[-1] == "":
        L.pop()
    return "\n".join(L) + "\n"


# ---- a fresh map from the fonts -------------------------------------------------

def survey(folder, sets, latin, unindexed, chosen, indexed_names):
    lines = ["# Fonts found in %s:" % os.path.basename(folder)]
    for prefix in sorted(sets):
        idx = sorted(sets[prefix])
        sample = sets[prefix][idx[0]][1]
        cells = sorted({"%dx%d" % sets[prefix][i][1]["cell"] for i in idx})
        lines.append("#   %-12s %2d files, cells %s, %dbpp, %s%s" % (
            prefix + "*", len(idx), ",".join(cells), sample["bpp"],
            "proportional" if sample["proportional"] else "fixed width",
            "   <- selected" if prefix == chosen else ""))
    # List these too. They used to be dropped without a word, so the comment
    # block looked like a full survey of the folder while omitting files
    # sitting right there.
    for stem in sorted(unindexed):
        name, info = unindexed[stem]
        lines.append("#   %-12s    (no index) cell %dx%d, %dbpp%s" % (
            name, info["cell"][0], info["cell"][1], info["bpp"],
            "   <- used for every charset" if not indexed_names and stem.endswith("0") else ""))
    for stem, (name, info) in sorted(latin.items()):
        lines.append("#   %-12s %d glyphs (Latin), %dx%d, %dbpp" % (
            name, info["glyphs"], info["cell"][0], info["cell"][1], info["bpp"]))
    return lines


def fresh_model(folder, title):
    sets, latin, unindexed = scan(folder)
    if not sets and not unindexed:
        return None

    chosen = None
    for want in PREFERENCE:
        if want in sets:
            chosen = want
            break
    if chosen is None and sets:
        chosen = sorted(sets)[0]
    members = sets.get(chosen, {})
    indices = sorted(members)

    # Whether the game selects a font by charset at all. v7 loadCJKFont() opens
    # a single "korean.fnt" - The Dig's own fonts are korean.fnt and
    # korean_g.fnt, with no index - so one face per charset there matches
    # nothing the game asks for and the hi-res layer quietly loads zero fonts.
    indexed_names = any(re.match(r"^korean\d+\.fnt$", n) for n in os.listdir(folder))
    if members:
        first_name, first_info = members[indices[0]]
    else:
        first_name, first_info = unindexed[sorted(unindexed)[0]]

    m = new_model(title)
    m["notes"] = ["# %s - hi-res text map" % title, "#",
                  "# Generated by tools/korean/makemaps.py from the font files themselves.",
                  "# Edit freely; rerunning the script overwrites it.", "#"]
    m["notes"] += survey(folder, sets, latin, unindexed, chosen, indexed_names)

    # The scale is not free: the game lays text out on the ORIGINAL font's
    # grid. Indy3's korean00.fnt is 8px high, so a Hangul syllable advances 8
    # game pixels and occupies 8*scale on screen. A replacement cell wider than
    # that is overlapped by the next character - a 24px cell at scale 2 gets 16
    # pixels of room and loses 8.
    #
    # So pick the scale that makes the replacement cell fit the game's own
    # grid, and say so when nothing fits exactly. Derive the scale from the
    # cell, not the other way round: a set baked at height * 2 must run at 2x
    # or it will not sit on the grid.
    orig = original_heights(folder)
    cell = first_info["cell"][0]
    scale = None
    if orig:
        for want in (2, 3):
            if any(h * want == cell for h in orig):
                scale = want
                break
    if scale is None:
        # Nothing matches exactly, so fall back to the largest scale whose
        # grid can still hold the cell.
        if orig:
            scale = 3 if min(orig) * 3 >= cell else 2
        else:
            scale = 3 if cell >= 30 else 2
    if orig:
        base = min(orig)
        m["scale_notes"].append("# the game's own fonts are %s px high; the smallest (%d) sets the grid"
                                % (sorted(orig), base))
        room = base * scale
        if room < cell:
            m["scale_notes"] += ["# WARNING: %dpx cell needs %d but the grid gives %d - characters will "
                                 "overlap by %dpx" % (cell, cell, room, cell - room),
                                 "# a %dx%d font would fit exactly" % (room, room)]
        else:
            m["scale_notes"].append("# %dx gives each character %dpx for a %dpx cell" % (scale, room, cell))
    else:
        m["scale_notes"].append("# cells are %dpx, so %dx keeps them near their native size" % (cell, scale))
    m["scale"] = scale
    m["blend"] = "auto"

    if members and indexed_names:
        m["faces"] = {i: members[i][0] for i in indices}
    else:
        m["face"] = first_name

    # A Latin companion for the single-byte characters. The double-byte sets
    # are indexed by a code page that has no Latin glyphs, so without this the
    # menu and the Latin half of mixed lines stay at the game's own 8px size
    # while the Hangul around them is replaced - the two halves of one line
    # end up at different qualities.
    #
    # Prefer a per-charset set: the cell can differ per charset (MI2 has five)
    # and one Latin face cannot sit on all of those baselines.
    indexed = {}
    for stem, (name, info) in latin.items():
        digits = "".join(c for c in os.path.splitext(name)[0] if c.isdigit())
        if name.startswith("hrlat") and digits:
            indexed[int(digits)] = name
    if len(indexed) > 1 and set(indexed) == set(indices):
        m["latins"] = dict(indexed)
    else:
        # Match the cell height to the chosen CJK set so both sit on one
        # baseline: an exact cell match first, then the nearest, and among
        # equals the one whose name shares the CJK set's prefix (baked
        # together).
        want_cell = first_info["cell"][1]
        best = None
        for stem, (name, info) in sorted(latin.items()):
            delta = abs(info["cell"][1] - want_cell)
            related = 0 if chosen and name.startswith(chosen.rstrip("0123456789")) else 1
            key = (delta, related, name)
            if best is None or key < best[0]:
                best = (key, name)
        if best is not None:
            m["latin"] = best[1]

    # The game picks line breaks and speech-bubble sizes from its own widths,
    # so the advance stays with the game; a proportional set can be given
    # advance=font by hand.
    m["advance"] = "game"
    if first_info["proportional"]:
        m["notes"].append("# The selected set carries per-glyph metrics: advance=font in [font] would let it")
        m["notes"].append("# space itself, but the game lays its text out on its own widths.")
    check_cells(folder, m)
    return m


# ---- a version 1 map, in version 2 terms ---------------------------------------

def parse_v1(text):
    """[(section, [(key, value)])] in file order; duplicate keys kept."""
    out, cur = [], None
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line[0] in "#;":
            continue
        if line.startswith("[") and line.endswith("]"):
            cur = (line[1:-1].strip(), [])
            out.append(cur)
            continue
        if cur is None or "=" not in line:
            continue
        k, v = line.split("=", 1)
        cur[1].append((k.strip(), v.strip()))
    return out


def is_v2(text):
    sections = parse_v1(text)
    return any(s.lower() == "map" and any(k.lower() == "version" and v == "2" for k, v in rows)
               for s, rows in sections)


def converted_model(folder, old_path, title):
    """The version 2 model of a version 1 map, its patterns expanded from the
    files in folder."""
    text = open(old_path, encoding="utf-8", errors="replace").read()
    m = new_model(title)
    m["notes"] = ["# %s - hi-res text map" % title, "#",
                  "# Converted by tools/korean/makemaps.py from the version 1 map %s;" % os.path.basename(old_path),
                  "# its choices (scale, blending, fonts, code page, [glyphs], [shadow]) are kept.",
                  "# Rerunning the script overwrites this file."]
    # Keep the old map's own comment block (its survey and reasoning), minus
    # the lines that named version 1 keys.
    head = []
    for raw in text.splitlines():
        s = raw.strip()
        if s.startswith("[") or (s and not s.startswith("#") and not s.startswith(";")):
            break
        if s.startswith(("#", ";")) and not re.search(r"\[(hires|latin|bitmap|encoding)\]|alpha=|metrics=|"
                                                         r"Generated by|rerunning the script", s, re.I):
            head.append("#" + s[1:] if s.startswith(";") else s)
    if head:
        m["notes"] += ["#"] + head
    unknown = []
    for section, rows in parse_v1(text):
        sec = section.lower()
        for k, v in rows:
            key = k.lower()
            if sec == "hires":
                if key == "scale":
                    m["scale"] = int(v, 0)
                elif key == "alpha":
                    m["blend"] = "auto" if v.lower() in ("true", "on", "1", "yes") else "off"
                elif key == "missing":
                    m["missing"] = v
                elif key == "enabled":
                    pass                                  # no such key: the map is the switch
                else:
                    unknown.append("[hires] %s=%s" % (k, v))
            elif sec == "encoding" and key == "codepage":
                m["encoding"] = v
            elif sec == "bitmap":
                if key == "multi":
                    found = expand_pattern(folder, v)
                    if not found:
                        m["skipped"].append("[bitmap] multi=%s: no such files in the folder" % v)
                    m["faces"].update(found)
                elif key == "single":
                    m["face"] = v
                elif key == "glyphs":
                    pass                                  # the loader reads the count from the file
                else:
                    unknown.append("[bitmap] %s=%s" % (k, v))
            elif sec == "latin":
                if key == "bitmap":
                    if "%" in v:
                        found = expand_pattern(folder, v)
                        if not found:
                            m["skipped"].append("[latin] bitmap=%s: no such files in the folder" % v)
                        m["latins"].update(found)
                    else:
                        m["latin"] = v
                elif key in ("font", "face"):
                    m["latin"] = None if v.lower() in ("same", "original") else v
                    if v.lower() in ("same", "original"):
                        m["latin_rule"] = v.lower()
                elif key == "mode":
                    if v.lower() == "off":
                        m["latin_rule"] = "original"
                    elif v.lower() == "half":
                        m["latin_advance"] = "cell"
                    elif v.lower() != "proportional":
                        unknown.append("[latin] mode=%s (fullwidth: a [glyphs] 0x21-0x7E = +0xFEE0 rule)" % v)
                elif key == "metrics":
                    m["latin_advance"] = v.lower()
                elif key == "baseline":
                    if v.lower() == "face":
                        m["latin_origin"] = "face"
                elif key == "enabled":
                    if v.lower() in ("false", "off", "0", "no"):
                        m["latin_rule"] = "original"
                else:
                    unknown.append("[latin] %s=%s" % (k, v))
            elif sec == "render" and key == "metrics":
                m["advance"] = v.lower()
            elif sec == "shadow":
                m["shadow"].append((k, v))
            elif sec == "glyphs" or sec.startswith("glyphs:cs") or sec.startswith("glyphs."):
                name = "glyphs" if sec == "glyphs" else "glyphs." + re.sub(r"^glyphs(:cs|\.)", "", sec)
                value = "original" if v.lower() == "keep" else v
                for n, rows_ in m["glyphs"]:
                    if n == name:
                        rows_.append((k, value))
                        break
                else:
                    m["glyphs"].append((name, [(k, value)]))
            else:
                unknown.append("[%s] %s=%s" % (section, k, v))
    for u in unknown:
        m["skipped"].append("%s: no version 2 equivalent written; check by hand" % u)
        print("makemaps: %s: %s has no version 2 equivalent here; left out" % (old_path, u), file=sys.stderr)
    check_cells(folder, m)
    return m


# ---- driver --------------------------------------------------------------------

def old_map_in(folder):
    """The folder's version 1 map, if it has one."""
    for name in (MAP_NAME, OLD_MAP_NAME, OLD_MAP_NAME + ".v1"):
        for cand in os.listdir(folder):
            if cand.lower() == name.lower():
                path = os.path.join(folder, cand)
                text = open(path, encoding="utf-8", errors="replace").read()
                if not is_v2(text):
                    return path
    return None


def write_map(folder, title, fresh=False, old=None, out=None):
    old = None if fresh else (old or old_map_in(folder))
    model = converted_model(folder, old, title) if old else fresh_model(folder, title)
    if model is None:
        return None
    text = emit(model)
    out = out or os.path.join(folder, MAP_NAME)
    with open(out, "w") as fh:
        fh.write(text)
    faces = len(model["faces"]) + (1 if model["face"] else 0)
    return out, old, faces, model["scale"]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("folders", nargs="*")
    ap.add_argument("--fresh", action="store_true", help="ignore an existing version 1 map")
    ap.add_argument("--convert", metavar="OLD_MAP", help="convert this version 1 map (its folder's fonts)")
    ap.add_argument("--out", metavar="NEW_MAP", help="with --convert: where to write (default: "
                    "HIRESTXT.MAP in the old map's folder)")
    a = ap.parse_args()

    if a.convert:
        folder = os.path.dirname(os.path.abspath(a.convert))
        key = os.path.basename(folder)
        title = GAMES.get(key, (key, key))[1]
        result = write_map(folder, title, old=os.path.abspath(a.convert), out=a.out)
        print("%s -> %s (%d faces, scale %s)" % (a.convert, result[0], result[2], result[3]))
        return 0

    folders = a.folders
    if not folders:
        base = os.path.expanduser("~/games")
        folders = [os.path.join(base, name) for name in GAMES
                   if os.path.isdir(os.path.join(base, name))]

    for folder in folders:
        folder = os.path.abspath(os.path.expanduser(folder))
        key = os.path.basename(folder)
        title = GAMES.get(key, (key, key))[1]
        result = write_map(folder, title, fresh=a.fresh)
        if result is None:
            print("%s: no numbered SVFN set found - skipped" % key)
            continue
        out, old, faces, scale = result
        print("%s: %s, %d faces, scale %s -> %s" % (
            key, "converted from %s" % os.path.basename(old) if old else "from the fonts", faces, scale, out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
