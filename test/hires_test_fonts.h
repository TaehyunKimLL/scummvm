/* ScummVM - Graphic Adventure Engine
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

// Fonts for the hi-res text tests, taken from the source tree so that a test
// runs the same on a developer machine and on CI: no system font, no path of
// a personal checkout.
//
//  - the CJK TrueType face committed with the engine data
//    (dists/engine-data/fonts/fonts-cjk/NotoSansKR-{Regular,Bold}.otf), always
//    by an absolute path (module.mk passes the absolute source directory as
//    HIRES_TEST_SRCDIR: an in-tree build has a relative $(srcdir), which a
//    font map would resolve against the game folder);
//  - an SVF image baked from that face at test time with the engine's own
//    baker (HiResFontBaker), written to a temporary directory;
//  - a TrueType collection ("ttcf") built from the Regular and the Bold
//    faces, for the face-index tests.
//
// The generated files live in one temporary directory per run, removed when
// the runner exits. Everything that needs FreeType or a real file system is
// compiled only where those exist; the callers skip (visibly) where it is not.

#ifndef TEST_HIRES_TEST_FONTS_H
#define TEST_HIRES_TEST_FONTS_H

#include <stdio.h>
#include <stdlib.h>

#include <common/array.h>
#include <common/endian.h>
#include <common/file.h>
#include <common/fs.h>
#include <common/path.h>
#include <common/str.h>
#include <common/stream.h>

#include "system/null_osystem.h"

#ifdef USE_FREETYPE2
#include <graphics/font.h>
#include <graphics/fonts/ttf.h>
#include <graphics/hires_text/font_baker.h>
#endif

#ifndef HIRES_TEST_SRCDIR
#define HIRES_TEST_SRCDIR "."
#endif

namespace HiResTestFonts {

/// The absolute source directory of the tree the runner was built from.
inline Common::String srcDir() {
	return HIRES_TEST_SRCDIR;
}

inline Common::String cjkDir() {
	return srcDir() + "/dists/engine-data/fonts/fonts-cjk";
}

inline Common::String cjkRegularPath() {
	return cjkDir() + "/NotoSansKR-Regular.otf";
}

inline Common::String cjkBoldPath() {
	return cjkDir() + "/NotoSansKR-Bold.otf";
}

/// The Thai face committed for the tests (test/fonts/noto-thai, OFL): a CJK
/// face has no Thai, and the below-mark fitting needs a face whose marks have
/// zero advance.
inline Common::String thaiRegularPath() {
	return srcDir() + "/test/fonts/noto-thai/NotoSansThai-Regular.ttf";
}

/// The bytes of a file in the repository, or an empty array.
inline Common::Array<byte> readFile(const Common::String &path) {
	Common::Array<byte> out;
#if NULL_OSYSTEM_IS_AVAILABLE
	Common::SeekableReadStream *s = Common::FSNode(Common::Path(path, '/')).createReadStream();
	if (s) {
		out.resize((uint)s->size());
		if (!out.empty() && s->read(out.data(), out.size()) != out.size())
			out.clear();
		delete s;
	}
#endif
	return out;
}

/// A new stream over the committed face (bold or regular), or nullptr.
inline Common::SeekableReadStream *openFile(const Common::String &path) {
#if NULL_OSYSTEM_IS_AVAILABLE
	Common::FSNode node(Common::Path(path, '/'));
	if (node.exists() && !node.isDirectory())
		return node.createReadStream();
#endif
	return nullptr;
}

inline Common::SeekableReadStream *openCjk(bool bold = false) {
	return openFile(bold ? cjkBoldPath() : cjkRegularPath());
}

/// A new stream over the committed Thai face, or nullptr.
inline Common::SeekableReadStream *openThai() {
	return openFile(thaiRegularPath());
}

#if NULL_OSYSTEM_IS_AVAILABLE && defined(POSIX)

struct Scratch {
	Common::Array<Common::String> files;
	Common::String dir;
	bool made;
	Scratch() : made(false) {}
	~Scratch() {
		for (uint i = 0; i < files.size(); ++i)
			remove(files[i].c_str());
		if (made)
			remove(dir.c_str());
	}
};

inline Scratch &scratch() {
	static Scratch s;
	return s;
}

/// The run's temporary directory (absolute), made on first use; empty on failure.
inline Common::String tempDir() {
	Scratch &s = scratch();
	if (!s.made) {
		char name[] = "/tmp/scummvm-hires-fonts-XXXXXX";
		if (!mkdtemp(name))
			return Common::String();
		s.dir = name;
		s.made = true;
	}
	return s.dir;
}

/// Write a file into the temporary directory; its absolute path, or empty.
inline Common::String writeTemp(const char *name, const byte *data, uint size) {
	const Common::String dir = tempDir();
	if (dir.empty())
		return Common::String();
	const Common::String path = dir + "/" + name;
	Common::DumpFile f;
	if (!f.open(Common::Path(path, '/')))
		return Common::String();
	scratch().files.push_back(path);
	const bool ok = f.write(data, size) == size;
	f.close();
	return ok ? path : Common::String();
}

#else

inline Common::String tempDir() { return Common::String(); }
inline Common::String writeTemp(const char *, const byte *, uint) { return Common::String(); }

#endif

/**
 * A TrueType collection made of the committed Regular (face 0) and Bold
 * (face 1) faces: a "ttcf" header (version 1.0, two offsets) and the two
 * fonts after it, 4-byte aligned, with every table record offset rewritten to
 * be absolute in the new file (a collection's table offsets are measured from
 * the start of the file, a lone font's from the start of its own).
 *
 * @return the absolute path of the generated .ttc, or empty.
 */
inline Common::String cjkCollectionPath() {
	static Common::String path;
	if (!path.empty())
		return path;
	const Common::Array<byte> fonts[2] = { readFile(cjkRegularPath()), readFile(cjkBoldPath()) };
	if (fonts[0].size() < 12 || fonts[1].size() < 12)
		return Common::String();

	Common::Array<byte> out;
	const uint headerSize = 12 + 4 * 2;
	out.resize(headerSize);
	WRITE_BE_UINT32(&out[0], MKTAG('t', 't', 'c', 'f'));
	WRITE_BE_UINT32(&out[4], 0x00010000);
	WRITE_BE_UINT32(&out[8], 2);
	for (int f = 0; f < 2; ++f) {
		while (out.size() & 3)
			out.push_back(0);
		const uint base = out.size();
		WRITE_BE_UINT32(&out[12 + 4 * f], base);
		const Common::Array<byte> &src = fonts[f];
		for (uint i = 0; i < src.size(); ++i)
			out.push_back(src[i]);
		const uint numTables = READ_BE_UINT16(&src[4]);
		if (src.size() < 12 + 16 * numTables)
			return Common::String();
		for (uint t = 0; t < numTables; ++t) {
			byte *rec = &out[base + 12 + 16 * t];
			WRITE_BE_UINT32(rec + 8, READ_BE_UINT32(rec + 8) + base);
		}
	}
	path = writeTemp("NotoSansKR.ttc", out.data(), out.size());
	return path;
}

#ifdef USE_FREETYPE2
/**
 * Bake the committed face into an SVF (SVFN) file at test time with the
 * engine's own baker, under the temporary directory.
 *
 * @param name         file name to write (e.g. "KO2350.SVF")
 * @param pixelSize    the face's pixel size
 * @param codepoints   what to bake
 * @param cellW,cellH  the cell; 0 for the face's own
 * @return the absolute path of the file, or empty on failure.
 */
inline Common::String bakeSvf(const char *name, int pixelSize, const Common::Array<uint32> &codepoints,
                              int cellW = 0, int cellH = 0, bool proportional = true, bool bold = false) {
	Common::SeekableReadStream *stream = openCjk(bold);
	if (!stream)
		return Common::String();
	Graphics::Font *face = Graphics::loadTTFFont(stream, DisposeAfterUse::YES, pixelSize,
	                                             Graphics::kTTFSizeModeCharacter);
	if (!face)
		return Common::String();
	Common::Array<byte> image;
	const bool ok = Graphics::HiResFontBaker::bake(*face, codepoints, cellW, cellH, proportional, image);
	delete face;
	if (!ok)
		return Common::String();
	return writeTemp(name, image.data(), image.size());
}
#endif

} // End of namespace HiResTestFonts

#endif
