#!/usr/bin/env python3
"""Generates the anti-aliased (2 bpp) SVF test faces of the Korean-support tests from TrueType, with the repository's own tools.

  gen_aa_faces.py OUTDIR [--only NAME,...]
                              writes the faces below (or those named) into OUTDIR (nothing is cached: a jamo set takes about 75 s)
  gen_aa_faces.py --print     prints the command lines and the TTF sha256 (docs/korean/plans/AA-tests.md quotes them)

All faces come from dists/svf-fonts-open (the pinned submodule) and its OFL TrueType files, Python run with -I:

  NG16P.SVF   precomposed 16x16 2 bpp, NanumGothic Bold 16 px:   mkfont.py --size 16 --cell 16 --width 16 --bpp 2 --clip-cell
  NG18P.SVF   precomposed 18x18 2 bpp, NanumGothic Bold 18 px, ascent 16 (another cell size, for chains of two sizes)
  NG16L.SVF   precomposed 16x16 2 bpp Latin + U+0300/U+0301, NanumGothic Regular 16 px (the Latin face of a three face chain)
  NG16JA.SVF  jamo set 8x4x4 (layout 844) 16x16 2 bpp, NanumGothic Bold 16 px:   jamoset.py --layout 844 --from-ttf --bpp 2 --check
              (jamoaa.py: exact-area outline coverage, pieces by the 1 bpp fit regions; needs fontTools only, route
              "outline composite"; a full set of all 11,172 syllables, 24,960 bytes)
  NG16J1.SVF  the 1 bpp pair for CLUT8: jamo set 844, 16x16, jamoset.py --from-ttf --binarize threshold (not a test fixture)
  NG16P8.SVF  precomposed 16x16 8 bpp, the same as NG16P with --bpp 8 (the one 8 bpp face, a few glyphs only: no shipped face has 8 bpp)

The precomposed faces carry only the code points of FREQUENT (and ASCII), so a syllable outside it is drawn by the jamo set
of a chain; the compat jamo U+3131..U+3163 are left out of them on purpose, for the same reason.
"""
import hashlib
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SVF = os.path.join(ROOT, "dists", "svf-fonts-open")
TOOLS = os.path.join(SVF, "tools")
BOLD = os.path.join(SVF, "ttf", "nanumgothic", "NanumGothic-Bold.ttf")
REGULAR = os.path.join(SVF, "ttf", "nanumgothic", "NanumGothic-Regular.ttf")

# 한글 게임 text of the tests: the frequent syllables the precomposed faces keep. Rare ones (똠 쀍 뷁 ...) are not in it.
FREQUENT = "가나다라마바사아자차카타파하각간갈감강개거게고구그기너네노누느니더데도두드디러레로루르리머메모무므미버베보부브비서세소수스시어에오우으이저제조주즈지한글게임왕국의모험을시작합니다예확인취소저장불러끝내양탈출"

UNICODE8 = "0020,0031-0033,0041-0045,0061-0065,AC00,B098,D55C,AE00"	# few glyphs: an 8 bpp glyph is 256 bytes

SIZES = {"NG16P": (BOLD, 16, 16, 16, None, 2), "NG18P": (BOLD, 18, 18, 18, 16, 2), "NG16P8": (BOLD, 16, 16, 16, None, 8)}


KNOWN = tuple(SIZES) + ("NG16L", "NG16JA", "NG16J1")


def unicode_arg(chars):
	return "0020-007E,00B7," + ",".join("%04X" % ord(c) for c in sorted(set(chars)))


def commands(out, only=None):
	"""The command lines, for the faces in @p only (names without .SVF; default all)."""
	want = lambda n: only is None or n in only
	cmds = []
	for name, (ttf, size, cell, width, ascent, bpp) in SIZES.items():
		if not want(name):
			continue
		c = [sys.executable, "-I", os.path.join(TOOLS, "mkfont.py"), ttf, os.path.join(out, name + ".SVF"), "--size", str(size),
		     "--cell", str(cell), "--width", str(width), "--bpp", str(bpp), "--clip-cell"]
		if ascent:
			c += ["--ascent", str(ascent)]
		cmds.append(c + ["--unicode", UNICODE8 if bpp == 8 else unicode_arg(FREQUENT), "--only-listed"])
	if want("NG16L"):
		cmds.append([sys.executable, "-I", os.path.join(TOOLS, "mkfont.py"), REGULAR, os.path.join(out, "NG16L.SVF"), "--size", "16",
		             "--cell", "16", "--width", "16", "--bpp", "2", "--clip-cell", "--unicode", "0020-007E,0300-0301", "--only-listed"])
	if want("NG16JA"):
		cmds.append([sys.executable, "-I", os.path.join(TOOLS, "jamoset.py"), "--layout", "844", "--from-ttf", BOLD, "--size", "16",
		             "--bpp", "2", "--check", "--pylib", pylib(), "-o", os.path.join(out, "NG16JA.SVF")])
	# the 1 bpp pair of the CLUT8 chain: the same TrueType, the jamo set recipe of docs/recipes.md 3 (exact-area rasteriser,
	# no FreeType), not a fixture (the tests use the 1 bpp NEODG-derived fixture face)
	if want("NG16J1"):
		cmds.append([sys.executable, "-I", os.path.join(TOOLS, "jamoset.py"), "--layout", "844", "--from-ttf", BOLD, "--size", "16",
		             "--binarize", "threshold", "--check", "--pylib", pylib(), "-o", os.path.join(out, "NG16J1.SVF")])
	return cmds


def probe(code, pythonpath=None):
	"""Runs @p code in this interpreter without -I (the user site loads: fontTools lives there); its stdout, or None."""
	env = dict(os.environ)
	if pythonpath:
		env["PYTHONPATH"] = pythonpath
	r = subprocess.run([sys.executable, "-c", code], capture_output=True, text=True, env=env)
	return r.stdout.strip() if r.returncode == 0 else None


def pylib():
	"""A directory holding fontTools (jamoset --bpp 2 needs it; -I hides the user site): $JAMOSET_PYLIB or where this
	interpreter finds it; an error when it is nowhere."""
	if os.environ.get("JAMOSET_PYLIB"):
		return os.environ["JAMOSET_PYLIB"]
	found = probe("import fontTools,os;print(os.path.dirname(os.path.dirname(fontTools.__file__)))")
	if not found:
		sys.exit("gen_aa_faces: fontTools is not importable (pip install fonttools, or set JAMOSET_PYLIB to a directory holding it)")
	return found


def versions():
	"""The versions of what draws the faces: Pillow, its FreeType, fontTools (a cache of the faces depends on them)."""
	pil = probe("import PIL, PIL.features;print(PIL.__version__, PIL.features.version('freetype2'))")
	ft = probe("import fontTools;print(fontTools.version)", pylib())
	return "Pillow %s fontTools %s" % (pil, ft)


def sha(path):
	return hashlib.sha256(open(path, "rb").read()).hexdigest()


def main():
	if len(sys.argv) == 2 and sys.argv[1] == "--print":
		for p in (BOLD, REGULAR):
			print("%s sha256 %s" % (os.path.relpath(p, ROOT), sha(p)))
		for c in commands("OUT"):
			print(" ".join(os.path.relpath(a, ROOT) if a.startswith(ROOT) else a for a in c))
		return 0
	only = None
	if len(sys.argv) == 4 and sys.argv[2] == "--only":
		only = sys.argv[3].split(",")
		unknown = [n for n in only if n not in KNOWN]
		if unknown:
			print("gen_aa_faces: unknown face %s (known: %s)" % (", ".join(unknown), ", ".join(KNOWN)), file=sys.stderr)
			return 2
	elif len(sys.argv) != 2:
		print(__doc__.split("\n\n")[0], file=sys.stderr)
		return 2
	out = sys.argv[1]
	os.makedirs(out, exist_ok=True)
	for c in commands(out, only):
		r = subprocess.run(c, capture_output=True, text=True)
		if r.returncode:
			sys.stderr.write(r.stdout + r.stderr)
			return 1
	return 0


if __name__ == "__main__":
	sys.exit(main())
