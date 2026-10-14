#!/usr/bin/env python3
"""Turn a font-pairing selection into SVFN fonts and per-game hi-res maps.

The font-pairing page (one card per original game font, a Korean bitmap face
picked for each) exports its choices as JSON ("scummvm-ko-font-pairing/1").
Each choice names a Korean face by key and the data file that holds it; those
files (korean*.json) carry the face's glyphs - ASCII, the 2350 KS X 1001
syllables and a little punctuation - as packed cells. This script:

  1. writes one SVFN (v2, code-point table) per chosen face, its glyphs as
     they are: same cell, bit depth, baseline and advances;
  2. writes a map per game from that game's current map: the [fonts] section
     names the new faces, each original font id's section gets the face
     chosen for it, the preset sections (render_target qualifiers) that named
     the old faces are dropped, and blending follows the chosen faces.

  python3 pairing2svf.py selection.json --data runs/fontmeasure/export --out runs/pairing-out

Out: <out>/<game>/<MAP> with the SVFs it names beside it, and <out>/MANIFEST.md.
A face's file name is KP + 6 hex digits of its key, the same in every game.
The faces hold no KS symbols other than the punctuation above; what a face
lacks the map's missing= glyph draws, so a face without U+25A1 (the maps'
missing=) gets a drawn box for it.
"""

import argparse
import base64
import hashlib
import json
import os
import re
import struct
import sys

MAGIC = b"SVFN"
VERSION_CMAP = 2
HEADER = "<4sHHBBHHBBBBHIII"
HEADER_V2 = 36
FLAG_VARIABLE = 1 << 0
FLAG_MARKS_AT_ORIGIN = 1 << 2
SHADOW_NONE = 0xFF
CODEPAGE = 949

HOME = os.path.expanduser("~/work/scummvm")
H = HOME + "/harness/dos/release/"
E = HOME + "/i18n/dists/engine-data/hires_text/dos/"
# Game (as the page names it) -> [(map template, map file name to write)].
TEMPLATES = {
    "KQ1 (DOS 1991 remake)": [(H + "kq1/HIRESVF.MAP", "HIRESKP.MAP")],
    "KQ4": [(H + "kq4/HIRESVF.MAP", "HIRESKP.MAP")],
    "LB1 Colonel's Bequest": [(H + "lb1/HIRESVF.MAP", "HIRESKP.MAP")],
    "LB2 (DOS CD)": [(E + "LB2KO.MAP", "HIRESKP.MAP")],
    "LB2 (DOS floppy)": [(E + "LB2KO.MAP", "HIRESKP.MAP")],
    "LSL1 EGA remake": [(H + "lsl1e/HIRESVF.MAP", "HIRESKP.MAP")],
    "LSL2": [(H + "lsl2/HIRESVF.MAP", "HIRESKP.MAP")],
    "PQ2": [(H + "pq2/HIRESVF.MAP", "HIRESKP.MAP")],
    "QFG1 EGA": [(H + "qfg1e/HIRESVF.MAP", "HIRESKP.MAP")],
    "QFG2": [(H + "qfg2/HIRESVF.MAP", "HIRESKP.MAP")],
    "SQ4 (DOS CD)": [(H + "sq4/SQ4KO.MAP", "HIRESKP.MAP")],
    "SQ4 (DOS EGA floppy)": [(H + "sq4/SQ4KO.MAP", "HIRESKP.MAP")],
    "Conquests of Camelot": [(H + "camelot/HIRESVF.MAP", "HIRESKP.MAP")],
    "LSL1 AGI": [(H + "lsl1a/HIRESBIT.MAP", "HIRESKP.MAP")],
    "MI1 EGA/VGA floppy": [(E + "M1KO.MAP", "M1KP.MAP")],
    "MI1 CD": [(E + "M1KO.MAP", "M1KP.MAP")],
    "MI2 DOS VGA": [(E + "M2KO.MAP", "M2KP.MAP")],
    "Indy3 VGA": [(E + "I3KO.MAP", "I3KP.MAP")],
    "Indy3 EGA": [(E + "I3KO.MAP", "I3KP.MAP")],
    "Indy4 DOS CD": [(E + "I4KO.MAP", "I4KP.MAP")],
    "Indy4 DOS floppy": [(E + "I4KO.MAP", "I4KP.MAP")],
    "Loom CD (VGA talkie)": [(E + "LOOMKO.MAP", "LOOMKP.MAP")],
    "DOTT DOS floppy": [(E + "DOTTKO.MAP", "DOTTKP.MAP")],
    "Maniac Mansion / Zak McKracken": [(E + "MMKO.MAP", "MMKP.MAP"), (E + "ZAKKO.MAP", "ZAKKP.MAP")],
}


# ---- SVFN -------------------------------------------------------------------

def svf_name(key):
    return "KP" + hashlib.sha1(key.encode()).hexdigest()[:6].upper() + ".SVF"


def ink_box(cell, w, h, bpp):
    """(first inked column, inked width) of one packed cell."""
    stride = (w * bpp + 7) // 8
    mask = (1 << bpp) - 1
    used = [x for x in range(w)
            if any((cell[y * stride + (x * bpp >> 3)] >> (8 - bpp - (x * bpp & 7))) & mask for y in range(h))]
    return (used[0], used[-1] - used[0] + 1) if used else (0, 0)


MISSING = 0x25A1


def box_cell(w, h, bpp, ascent):
    """A white square (U+25A1) on the baseline, about the size of a syllable."""
    stride = (w * bpp + 7) // 8
    cell = bytearray(stride * h)
    full = (1 << bpp) - 1
    side = min(max(3, min(w, ascent) * 3 // 4), w, h)
    x0, y1 = (w - side) // 2, min(h, max(side, ascent)) - 1
    y0 = y1 - side + 1

    def put(x, y):
        bit = x * bpp
        cell[y * stride + (bit >> 3)] |= full << (8 - bpp - (bit & 7))
    for i in range(side):
        for x, y in ((x0 + i, y0), (x0 + i, y1), (x0, y0 + i), (x0 + side - 1, y0 + i)):
            put(x, y)
    return bytes(cell)


def svfn_bytes(face):
    """An SVFN v2 of a page face: its cells as they are, a width table if it has advances."""
    w, h, bpp = face["w"], face["h"], face["bpp"]
    if bpp not in (1, 2, 8):
        raise ValueError(f"{face['key']}: {bpp} bpp")
    data = base64.b64decode(face["data"])
    chars = [ord(c) for c in face["chars"]]
    cell = ((w * bpp + 7) // 8) * h
    if len(data) != len(chars) * cell:
        raise ValueError(f"{face['key']}: {len(data)} bytes for {len(chars)} cells of {cell}")
    adv = face.get("adv")
    if MISSING not in chars:
        data += box_cell(w, h, bpp, face["ascent"])
        chars.append(MISSING)
        if adv:
            adv = list(adv) + [w]
    metrics = bytearray()
    if adv:
        for i in range(len(chars)):
            ink_x, ink_w = ink_box(data[i * cell:(i + 1) * cell], w, h, bpp)
            metrics += struct.pack("<BbBB", max(0, min(255, adv[i])), ink_x, ink_w, 0)
    cmap = b"".join(struct.pack("<II", cp, i) for cp, i in sorted((cp, i) for i, cp in enumerate(chars)))
    flags = FLAG_MARKS_AT_ORIGIN | (FLAG_VARIABLE if adv else 0)
    metrics_off = HEADER_V2 + len(cmap) if adv else 0
    data_off = HEADER_V2 + len(cmap) + len(metrics)
    header = struct.pack(HEADER, MAGIC, VERSION_CMAP, flags, bpp, SHADOW_NONE, CODEPAGE, len(chars),
                         w, h, max(0, min(255, face["ascent"])), 0, 0, metrics_off, data_off, len(data))
    header += struct.pack("<I", HEADER_V2)
    return header + cmap + bytes(metrics) + data


# ---- maps -------------------------------------------------------------------

SECTION = re.compile(r"^\s*\[([^\]]+)\]\s*$")
CJK_ALIAS = "cjk"
CJK_FACE = "data:NotoSansKR-Bold.otf"


def parse_sections(text):
    """[(name or None for the preamble, [lines])], comments and all."""
    out = [(None, [])]
    for line in text.splitlines():
        m = SECTION.match(line)
        if m:
            out.append((m.group(1).strip(), [line]))
        else:
            out[-1][1].append(line)
    return out


def font_section(font_id):
    """The map section an original font id is configured in."""
    if font_id == "builtin":
        return None                       # AGI, MM/Zak: the one game font
    digits = re.sub(r"\D", "", font_id)
    return f"font.{int(digits)}" if digits else None


def set_key(lines, key, value):
    """Set key=value in a section's lines (replacing the first, dropping repeats)."""
    out, done = [], False
    for line in lines:
        k = line.split("=", 1)[0].strip().lower() if "=" in line and not line.lstrip().startswith((";", "#")) else None
        if k == key:
            if not done:
                out.append(f"{key}={value}")
                done = True
            continue
        out.append(line)
    if not done:
        # After the header and any comments right under it.
        out.insert(1 if out and SECTION.match(out[0]) else len(out), f"{key}={value}")
    return out


def key_value(line):
    if "=" not in line or line.lstrip().startswith((";", "#")):
        return None, None
    k, v = line.split("=", 1)
    return k.strip().lower(), v.split(";")[0].split(" #")[0].strip()


def make_map(template_text, game, fonts, aliases, blend, decorations=None):
    """The game's map with the chosen faces in place of the old ones.

    A section's old face stays behind the new one in its chain (face=kp1,ui),
    so the symbols a chosen face lacks (it carries ASCII, the syllables and a
    little punctuation) come from the face the map used before, which has the
    KS X 1001 set."""
    sections = parse_sections(template_text)
    by_name = {}
    kept = []
    old_fonts = []                        # the template's own [fonts] lines, kept as fallbacks
    for name, lines in sections:
        if name and ":" in name:
            continue                      # a preset for another screen; it named the old faces
        if name and name.lower() == "fonts":
            old_fonts = [l for l in lines[1:] if key_value(l)[0]]
            continue                      # rewritten below
        kept.append([name, lines])
        if name:
            by_name[name.lower()] = kept[-1]
    old_aliases = {key_value(l)[0] for l in old_fonts}

    def old_chain(sec):
        """The faces a section named before (or the [font] default's)."""
        for s in (sec, "font"):
            if s in by_name:
                for line in by_name[s][1]:
                    k, v = key_value(line)
                    if k == "face":
                        return [a.strip() for a in v.split(",") if a.strip().lower() in old_aliases]
        return []

    default_alias = aliases[fonts[0]["korean"]["key"]]
    chosen = {}
    for f in fonts:
        sec = font_section(f["id"])
        if sec is None:
            # The game's one font: AGI's maps configure [font], MM/Zak's [font.0].
            sec = "font.0" if "font.0" in by_name else "font"
        chosen[sec] = aliases[f["korean"]["key"]]
    # Last in every chain: the Noto Sans KR the binary carries (fonts-cjk.dat), for
    # builds with FreeType when no font pack is there; a DOS build skips it.
    has_cjk = CJK_ALIAS in old_aliases
    tail = [CJK_ALIAS]
    chains = {sec: [alias] + old_chain(sec) + tail for sec, alias in chosen.items()}
    # A section not in the selection keeps its old faces behind the default.
    for name in list(by_name):
        if name not in chains and any(key_value(l)[0] == "face" for l in by_name[name][1]):
            chains[name] = [default_alias] + old_chain(name) + tail
    chains.setdefault("font", [default_alias] + old_chain("font") + tail)
    for sec, chain in chains.items():
        if sec not in by_name:
            entry = [sec, [f"[{sec}]"]]
            kept.append(entry)
            by_name[sec] = entry
        by_name[sec][1] = set_key(by_name[sec][1], "face", ",".join(dict.fromkeys(chain)))
    if "render" in by_name:
        by_name["render"][1] = set_key(by_name["render"][1], "blend", blend)

    # Every SCUMM charset's decoration grows with its face: the multiple is the
    # Korean face height over the original line height (2 at the plain 2x), and
    # outline width and drop offset are both that many times the original's game
    # px. A charset whose original face has a measured decoration gets it back,
    # mode and all; any other keeps mode=game (the patch byte) with the sizes the
    # patch's 1 game px decoration would have at that multiple - the engine's own
    # default is fixed by the render scale, a 2 px drop at 2x on any face.
    shadow_secs = []
    for f in fonts:
        n = font_section(f["id"])
        if not n or not f["id"].startswith("cs"):
            continue
        mult = f["korean"].get("h", 0) / f["lineh"] if f.get("lineh") else 2.0
        m = (decorations or {}).get(f["id"])
        lines = [f"[shadow.{n.split('.')[1]}]"]
        if not m:
            lines += [f"width={round(0.75 * mult * 4) / 4:g}", f"offset={max(1, round(mult))}"]
            shadow_secs.append(lines)
            continue
        # The mode is the original's, not the patch byte's (mode=game): DOTT's
        # patch asks cs3 for an outline where the original drops a shadow.
        lines.append("mode=" + ("stroke" if "o" in m and "d" in m else "outline" if "o" in m else "drop"))
        if "o" in m:
            lines.append(f"width={round(0.75 * m['o'] * mult * 4) / 4:g}")
            if "d" in m:
                dx, dy = (round(v * mult) for v in m["d"])
                lines.append(f"shadow={dx},{dy}")
        else:
            lines.append(f"offset={max(1, round(max(map(abs, m['d'])) * mult))}")
        shadow_secs.append(lines)

    fonts_sec = ["[fonts]"] + [f"{a}={svf_name(k)}" for k, a in aliases.items()] + \
        ["; the map's own faces, behind the chosen ones for the symbols they lack"] + old_fonts + \
        ([] if has_cjk else [f"{CJK_ALIAS}={CJK_FACE}"])
    head = [f"; Generated by tools/korean/pairing2svf.py from a font-pairing selection ({game}).",
            "; Faces (alias = file: Korean face):"]
    for f in fonts:
        k = f["korean"]
        head.append(f";   font {f['id']}: {aliases[k['key']]} = {svf_name(k['key'])}: {k['label']}")
    head.append(";")
    out = head + kept[0][1]
    inserted = False
    for name, lines in kept[1:]:
        out += lines
        if name and name.lower() == "render" and not inserted:
            out += fonts_sec
            inserted = True
    if not inserted:
        out += fonts_sec
    for lines in shadow_secs:
        out += lines
    return "\n".join(out).rstrip("\n") + "\n"


# ---- main -------------------------------------------------------------------

def load_faces(data_dir, wanted):
    faces = {}
    for name in sorted(os.listdir(data_dir)):
        if not (name.startswith("korean") and name.endswith(".json")):
            continue
        for f in json.load(open(os.path.join(data_dir, name)))["fonts"]:
            if f["key"] in wanted:
                faces[f["key"]] = f
    return faces


def main():
    ap = argparse.ArgumentParser(description="font-pairing selection -> SVFN fonts + per-game maps")
    ap.add_argument("selection", help="JSON copied from the font-pairing page")
    ap.add_argument("--data", required=True, help="folder with the page's korean*.json files")
    ap.add_argument("--out", required=True)
    ap.add_argument("--template", action="append", default=[], metavar="GAME=MAP",
                    help="use this map as the game's template (the page's game name)")
    args = ap.parse_args()

    sel = json.load(open(args.selection))
    if sel.get("format") != "scummvm-ko-font-pairing/1":
        sys.exit(f"{args.selection}: not a font-pairing selection")
    templates = dict(TEMPLATES)
    for t in args.template:
        game, path = t.split("=", 1)
        templates[game] = [(path, os.path.basename(path).rsplit(".", 1)[0] + "KP.MAP")]

    wanted = {f["korean"]["key"] for g in sel["games"] for f in g["fonts"] if f.get("korean")}
    faces = load_faces(args.data, wanted)
    missing = wanted - set(faces)
    if missing:
        sys.exit(f"faces not in {args.data}: {', '.join(sorted(missing))}")

    table_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "scumm-fonts", "original-decoration.json")
    table = json.load(open(table_path, encoding="utf-8"))

    os.makedirs(args.out, exist_ok=True)
    blobs = {k: svfn_bytes(f) for k, f in faces.items()}
    manifest = ["# Font-pairing output", "", f"Selection: {os.path.basename(args.selection)} "
                f"({sel.get('created', '?')})", "", "| game | map | font id | Korean face | file | bpp | cell | picked |",
                "|---|---|---|---|---|---|---|---|"]
    for g in sel["games"]:
        fonts = [f for f in g["fonts"] if f.get("korean")]
        if not fonts:
            continue
        if g["game"] not in templates:
            print(f"{g['game']}: no map template; pass --template \"{g['game']}=PATH\"", file=sys.stderr)
            continue
        aliases = {}
        for f in fonts:
            aliases.setdefault(f["korean"]["key"], f"kp{len(aliases) + 1}")
        blend = "auto" if any(faces[k]["bpp"] > 1 for k in aliases) else "off"
        slug = re.sub(r"[^A-Za-z0-9]+", "-", g["game"]).strip("-").lower()
        for template, map_name in templates[g["game"]]:
            folder = os.path.join(args.out, slug if len(templates[g["game"]]) == 1
                                  else slug + "-" + map_name.split(".")[0].lower())
            os.makedirs(folder, exist_ok=True)
            text = make_map(open(template, encoding="utf-8").read(), g["game"], fonts, aliases, blend,
                            decorations=table.get(g["game"]))
            open(os.path.join(folder, map_name), "w", encoding="utf-8").write(text)
            for k in aliases:
                open(os.path.join(folder, svf_name(k)), "wb").write(blobs[k])
            for f in fonts:
                k = faces[f["korean"]["key"]]
                manifest.append(f"| {g['game']} | {os.path.relpath(os.path.join(folder, map_name), args.out)} | "
                                f"{f['id']} | {k['label']} | {svf_name(k['key'])} | {k['bpp']} | "
                                f"{k['w']}x{k['h']} | {'yes' if f.get('picked') else 'default'} |")
            print(f"{os.path.join(folder, map_name)}: {len(fonts)} font ids, {len(aliases)} faces, blend={blend}")
    open(os.path.join(args.out, "MANIFEST.md"), "w", encoding="utf-8").write("\n".join(manifest) + "\n")


if __name__ == "__main__":
    main()
