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

// C21: a map's font path may name one face of a TrueType collection,
// "<file>.ttc#<N>". The suffix is taken only when the part after the last
// '#' is all digits and no file carries the full name, so a literal '#'
// in a file name still works.

#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/path.h"
#include "common/str.h"
#include "common/stream.h"
#include "graphics/hires_text/font_face.h"
#include "graphics/hires_text/glyph_source_ttf.h"

#include "../system/null_osystem.h"

#ifdef USE_FREETYPE2
#include "common/fs.h"
#endif

namespace {
// The files the fake file system holds, for resolveFontFace() below.
static const char *const *g_fontFaceTestFiles = nullptr;
static bool fontFaceTestExists(const Common::Path &path) {
	const Common::String s = path.toString('/');
	for (const char *const *f = g_fontFaceTestFiles; f && *f; ++f)
		if (s == *f)
			return true;
	return false;
}
} // namespace

class HiResTextFontFaceTestSuite : public CxxTest::TestSuite {
public:
	void setUp() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::install_null_g_system();
#endif
	}

	void tearDown() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::uninstall_null_g_system();
#endif
		g_fontFaceTestFiles = nullptr;
	}

	static bool split(const char *in, Common::String &file, int32 &index) {
		file = "unset";
		index = -2;
		return Graphics::splitFontFaceIndex(in, file, index);
	}

	void test_split_takes_a_digit_suffix() {
		Common::String file;
		int32 index;
		TS_ASSERT(split("AppleSDGothicNeo.ttc#6", file, index));
		TS_ASSERT_EQUALS(file, "AppleSDGothicNeo.ttc");
		TS_ASSERT_EQUALS(index, 6);
		TS_ASSERT(split("a.ttc#0", file, index));
		TS_ASSERT_EQUALS(index, 0);
		TS_ASSERT(split("a.ttc#06", file, index));
		TS_ASSERT_EQUALS(index, 6);
		TS_ASSERT(split("a.ttc#65535", file, index));
		TS_ASSERT_EQUALS(index, 65535);
		// Only the last '#' splits.
		TS_ASSERT(split("x#1.ttc#2", file, index));
		TS_ASSERT_EQUALS(file, "x#1.ttc");
		TS_ASSERT_EQUALS(index, 2);
	}

	void test_split_rejects_anything_else() {
		Common::String file;
		int32 index;
		const char *const bad[] = {
			"a.ttc", "a.ttc#", "#6", "a#b.ttf", "a.ttc#6 ", "a.ttc# 6", "a.ttc#-1",
			"a.ttc#+1", "a.ttc#6a", "a.ttc#0x6", "", "#"
		};
		for (uint i = 0; i < ARRAYSIZE(bad); ++i) {
			TSM_ASSERT(bad[i], !split(bad[i], file, index));
			TSM_ASSERT_EQUALS(bad[i], file, "unset");
			TSM_ASSERT_EQUALS(bad[i], index, -2);
		}
	}

	void test_split_out_of_range_is_a_bad_index() {
		// Still a face suffix (so the file is looked up without it), but no
		// face FreeType could open: -1, which the opener reports.
		Common::String file;
		int32 index;
		TS_ASSERT(split("a.ttc#65536", file, index));
		TS_ASSERT_EQUALS(file, "a.ttc");
		TS_ASSERT_EQUALS(index, -1);
		TS_ASSERT(split("a.ttc#99999999999999999999", file, index));
		TS_ASSERT_EQUALS(index, -1);
	}

	void test_resolve_prefers_the_full_name() {
		static const char *const files[] = { "/f/odd#2", "/f/odd", "/f/set.ttc", nullptr };
		g_fontFaceTestFiles = files;
		Common::Path file;
		int32 index = -2;
		// A file whose name ends in "#2" is that file, face 0.
		TS_ASSERT(Graphics::resolveFontFace(Common::Path("/f/odd#2"), file, index, fontFaceTestExists));
		TS_ASSERT_EQUALS(file.toString('/'), "/f/odd#2");
		TS_ASSERT_EQUALS(index, 0);
		// No file by the full name: the suffix names a face.
		TS_ASSERT(Graphics::resolveFontFace(Common::Path("/f/set.ttc#6"), file, index, fontFaceTestExists));
		TS_ASSERT_EQUALS(file.toString('/'), "/f/set.ttc");
		TS_ASSERT_EQUALS(index, 6);
		// A plain path is itself, face 0.
		TS_ASSERT(Graphics::resolveFontFace(Common::Path("/f/set.ttc"), file, index, fontFaceTestExists));
		TS_ASSERT_EQUALS(file.toString('/'), "/f/set.ttc");
		TS_ASSERT_EQUALS(index, 0);
		// A '#' in a directory name is not a face suffix.
		TS_ASSERT(!Graphics::resolveFontFace(Common::Path("/f#1/set.ttc"), file, index, fontFaceTestExists));
		// Neither exists: the path as given, face 0, and false.
		TS_ASSERT(!Graphics::resolveFontFace(Common::Path("/f/none.ttc#3"), file, index, fontFaceTestExists));
		TS_ASSERT_EQUALS(file.toString('/'), "/f/none.ttc#3");
		TS_ASSERT_EQUALS(index, 0);
		// Out of range: resolved to the file, with index -1.
		TS_ASSERT(Graphics::resolveFontFace(Common::Path("/f/set.ttc#70000"), file, index, fontFaceTestExists));
		TS_ASSERT_EQUALS(file.toString('/'), "/f/set.ttc");
		TS_ASSERT_EQUALS(index, -1);
	}

#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
	static const char *sdGothicPath() {
		return "/System/Library/Fonts/AppleSDGothicNeo.ttc";
	}

	// TS_SKIP does not leave the test in this runner (built without
	// exceptions): every real-font test returns right after it.
	static bool haveFont(const char *path) {
		Common::FSNode node{Common::Path(path)};
		if (node.exists() && !node.isDirectory())
			return true;
		TS_SKIP(Common::String::format("no font at '%s'", path).c_str());
		return false;
	}

	// Every row of U+D55C (Hangul HAN) and 'A' in the source, concatenated.
	static Common::Array<byte> glyphBytes(Graphics::TtfGlyphSource *src) {
		Common::Array<byte> out;
		const uint32 cps[] = { 0xD55C, 'A' };
		for (uint c = 0; c < ARRAYSIZE(cps); ++c) {
			const int w = src->cellWidth() * src->cells(cps[c]);
			for (int y = 0; y < src->cellHeight(); ++y) {
				const byte *r = src->row(cps[c], y);
				for (int x = 0; r && x < w; ++x)
					out.push_back(r[x]);
			}
		}
		return out;
	}

	static Graphics::TtfGlyphSource *open(const char *path, int32 faceIndex, Common::String &error, bool explicitIndex = true) {
		Common::SeekableReadStream *stream = Common::FSNode(Common::Path(path)).createReadStream();
		if (!stream)
			return nullptr;
		if (!explicitIndex)
			return Graphics::TtfGlyphSource::create(stream, DisposeAfterUse::YES, 24, error, false, false, nullptr, 0);
		return Graphics::TtfGlyphSource::create(stream, DisposeAfterUse::YES, 24, error, false, false, nullptr, 0, faceIndex);
	}

	void test_face_6_draws_differently_from_face_0() {
		if (!haveFont(sdGothicPath()))
			return;
		Common::String error;
		Graphics::TtfGlyphSource *regular = open(sdGothicPath(), 0, error);
		TS_ASSERT(regular);
		Graphics::TtfGlyphSource *bold = open(sdGothicPath(), 6, error);
		TS_ASSERT(bold);
		if (regular && bold) {
			const Common::Array<byte> a = glyphBytes(regular), b = glyphBytes(bold);
			TS_ASSERT(!a.empty());
			TS_ASSERT(a != b);
		}
		delete regular;
		delete bold;
	}

	void test_no_index_is_face_0_byte_for_byte() {
		if (!haveFont(sdGothicPath()))
			return;
		Common::String error;
		Graphics::TtfGlyphSource *plain = open(sdGothicPath(), 0, error, false);
		Graphics::TtfGlyphSource *zero = open(sdGothicPath(), 0, error);
		TS_ASSERT(plain && zero);
		if (plain && zero) {
			TS_ASSERT_EQUALS(plain->faceSize(), zero->faceSize());
			TS_ASSERT_EQUALS(plain->lineTop(), zero->lineTop());
			TS_ASSERT(glyphBytes(plain) == glyphBytes(zero));
		}
		delete plain;
		delete zero;
	}

	void test_bad_face_index_fails_with_an_error() {
		if (!haveFont(sdGothicPath()))
			return;
		Common::String error;
		Graphics::TtfGlyphSource *src = open(sdGothicPath(), 99, error);
		TS_ASSERT(!src);
		TS_ASSERT(error.contains("99"));
		delete src;
		error.clear();
		src = open(sdGothicPath(), -1, error);
		TS_ASSERT(!src);
		TS_ASSERT(!error.empty());
		delete src;
	}

	void test_open_font_face_on_a_real_collection() {
		if (!haveFont(sdGothicPath()))
			return;
		int32 index = -2;
		Common::String error;
		Common::SeekableReadStream *s = Graphics::openFontFace(Common::Path(Common::String(sdGothicPath()) + "#6"), index, error);
		TS_ASSERT(s);
		TS_ASSERT_EQUALS(index, 6);
		delete s;
		s = Graphics::openFontFace(Common::Path(sdGothicPath()), index, error);
		TS_ASSERT(s);
		TS_ASSERT_EQUALS(index, 0);
		delete s;
		s = Graphics::openFontFace(Common::Path(Common::String(sdGothicPath()) + "#70000"), index, error);
		TS_ASSERT(!s);
		TS_ASSERT_EQUALS(error, "face index out of range (0..65535)");
		s = Graphics::openFontFace(Common::Path("/nonexistent-c21/none.ttc#6"), index, error);
		TS_ASSERT(!s);
		TS_ASSERT_EQUALS(error, "does not exist");
		s = Graphics::openFontFace(Common::Path("/System/Library/Fonts"), index, error);
		TS_ASSERT(!s);
		TS_ASSERT_EQUALS(error, "is a directory");
	}
#endif
};
