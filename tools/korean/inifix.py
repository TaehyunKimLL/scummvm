#!/usr/bin/env python3
"""Patch a throwaway scummvm.ini for a test run.

Every harness needs the same two or three edits, and doing them with inline
python inside a shell script produced heredocs long enough to be rejected. This
does them from a file instead.

    python3 inifix.py <ini> [target] [--log] [--gui] [--no-alpha]

Always sets extrapath, because without encoding.dat every CJK character decodes
to U+FFFD and the capture shows tofu that looks like a font fault.
"""
import argparse
import os
import re
import sys

ENGDATA = os.path.expanduser("~/src/scummvm/dists/engine-data")


def set_global(text, key, value):
    """Force key=value in [scummvm], replacing any existing setting."""
    line = key + "=" + value
    out, seen, inside = [], False, False
    for row in text.splitlines(keepends=True):
        if row.startswith("["):
            if inside and not seen:
                out.append(line + "\n")
                seen = True
            inside = row.strip() == "[scummvm]"
        elif inside and row.split("=")[0].strip() == key:
            row = line + "\n"
            seen = True
        out.append(row)
    if inside and not seen:
        out.append(line + "\n")
        seen = True
    if not seen:
        return "[scummvm]\n" + line + "\n\n" + text
    return "".join(out)


def add_global(text, line):
    """Add a key to [scummvm], replacing it there if already present.

    Look only inside that section: game sections carry their own extrapath, and
    a whole-file search finds those and concludes there is nothing to do. The
    global one then never gets written, encoding.dat is not found, and every
    CJK character decodes to U+FFFD - which looks like a font bug.
    """
    key = line.split("=")[0]
    m = re.search(r"^\[scummvm\]\s*$", text, re.M)
    if not m:
        return "[scummvm]\n" + line + "\n\n" + text

    end = text.find("\n[", m.end())
    if end < 0:
        end = len(text)
    body = text[m.end():end]

    if re.search(r"^" + re.escape(key) + r"=", body, re.M):
        body = re.sub(r"^" + re.escape(key) + r"=.*$", line, body,
                      count=1, flags=re.M)
    else:
        body = "\n" + line + body

    return text[:m.end()] + body + text[end:]


def add_to_target(text, target, line):
    key = line.split("=")[0]
    pat = re.compile(r"^\[" + re.escape(target) + r"\]\s*$", re.M)
    m = pat.search(text)
    if not m:
        return text, False

    end = text.find("\n[", m.end())
    if end < 0:
        end = len(text)
    body = text[m.end():end]

    # Replace the key if the section already has it, rather than adding a
    # second copy - ScummVM takes the first and the edit would look ignored.
    if re.search(r"^" + re.escape(key) + r"=", body, re.M):
        body = re.sub(r"^" + re.escape(key) + r"=.*$", line, body, count=1, flags=re.M)
    else:
        body = "\n" + line + body

    return text[:m.end()] + body + text[end:], True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ini")
    ap.add_argument("target", nargs="?")
    ap.add_argument("--log", action="store_true",
                    help="turn on the hi-res text log for this target")
    ap.add_argument("--gui", action="store_true",
                    help="use the ScummVM GUI rather than the game's own")
    ap.add_argument("--no-alpha", action="store_true",
                    help="turn blending off, to compare against it")
    ap.add_argument("--no-hires", action="store_true",
                    help="turn the hi-res layer off entirely - the control")
    args = ap.parse_args()

    text = open(args.ini, errors="replace").read()

    if os.path.exists(os.path.join(ENGDATA, "encoding.dat")):
        text = add_global(text, "extrapath=" + ENGDATA)

    # The autosave fires every 20 seconds by default and writes over slot 0.
    # During a scripted play-through that both interrupts the game and
    # destroys the save the run was started from.
    text = set_global(text, "autosave_period", "0")

    if args.target:
        wanted = []
        if args.log:
            wanted.append("hires_text_log=true")
        if args.gui:
            wanted.append("original_gui=false")
        if args.no_alpha:
            wanted.append("hires_text_alpha=false")
        if args.no_hires:
            wanted.append("hires_text_scale=1")
            wanted.append("korean_hires_scale=1")

        missing = False
        for line in wanted:
            text, ok = add_to_target(text, args.target, line)
            if not ok:
                missing = True
                break

        if missing:
            # Write what did apply before reporting the failure. Returning
            # early discarded the global extrapath edit too, so a typo in the
            # target name produced a run with no encoding.dat - every Korean
            # character became U+FFFD, which looks like a font bug rather
            # than a mistyped argument.
            open(args.ini, "w").write(text)
            print(f"inifix: no [{args.target}] section in {args.ini}",
                  file=sys.stderr)
            print("        (global edits were still applied)", file=sys.stderr)
            return 1

    open(args.ini, "w").write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
