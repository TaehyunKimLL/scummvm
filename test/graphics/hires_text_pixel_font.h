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

// C28: a pixel font (hires_text.map pixel=<design px>) is opened at its
// design size, or the largest whole multiple of it the cell holds, with no
// probe shrink, so every glyph is the designer's bitmap: coverage is only
// ever 0 or 255.
//
// TEST: every test_ method here is declared unconditionally (S15): cxxtestgen
// has no C preprocessor of its own, so it finds a method's name whether or
// not the #ifdef around its declaration is true for this build, and always
// generates a call to it in test/runner.cpp - a method actually compiled out
// (USE_FREETYPE2 undefined) then fails to link the generated call, not to
// build the suite itself. Guarding only each method's *body* - the method
// itself always exists, skipping visibly when FreeType is unavailable - is
// what keeps a freetype-less build's test runner buildable.

#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/str.h"
#include "common/stream.h"
#include "graphics/hires_text/glyph_source_ttf.h"

#include "../system/null_osystem.h"

#ifdef USE_FREETYPE2
#include "common/fs.h"
#endif

using Graphics::TtfGlyphSource;

// The Galmuri faces (OFL, github.com/quiple/galmuri) live in a folder named
// by SCUMMVM_TEST_PIXEL_FONT_DIR, else <SCUMMVM_TEST_I18N_DATA>/../fonts/
// pixel/galmuri; a test whose face is absent is skipped, visibly.
#ifdef USE_FREETYPE2
#if NULL_OSYSTEM_IS_AVAILABLE
#pragma push_macro("getenv")
#undef getenv
static Common::String pixelFontTestPath(const char *file) {
	const char *dir = getenv("SCUMMVM_TEST_PIXEL_FONT_DIR");
	if (dir && *dir)
		return Common::String::format("%s/%s", dir, file);
	const char *data = getenv("SCUMMVM_TEST_I18N_DATA");
	if (data && *data)
		return Common::String::format("%s/../fonts/pixel/galmuri/%s", data, file);
	return Common::String();
}
#pragma pop_macro("getenv")
#endif
#endif

class HiResTextPixelFontTestSuite : public CxxTest::TestSuite {
#ifdef USE_FREETYPE2
#if NULL_OSYSTEM_IS_AVAILABLE
private:
	static Common::SeekableReadStream *openFont(const char *file) {
		const Common::String path = pixelFontTestPath(file);
		if (path.empty())
			return nullptr;
		Common::FSNode node{Common::Path(path, '/')};
		if (!node.exists())
			return nullptr;
		return node.createReadStream();
	}

	// Every coverage byte of cp's cell is 0 or 255; returns the ink box.
	static bool binaryInk(TtfGlyphSource &src, uint32 cp, int &top, int &bottom) {
		top = src.cellHeight();
		bottom = -1;
		if (src.cells(cp) == 0)
			return false;
		bool binary = true;
		for (int y = 0; y < src.cellHeight(); y++) {
			const byte *r = src.row(cp, y);
			for (int x = 0; x < src.cellWidth() * 2; x++) {
				if (r[x] != 0 && r[x] != 255)
					binary = false;
				if (r[x]) {
					top = MIN(top, y);
					bottom = MAX(bottom, y);
				}
			}
		}
		return binary;
	}

	static const uint32 *samples(uint &n) {
		// 가 뷁 를 똠 힣 A g j y
		static const uint32 cps[] = { 0xAC00, 0xBDC1, 0xB97C, 0xB620, 0xD7A3, 'A', 'g', 'j', 'y' };
		n = ARRAYSIZE(cps);
		return cps;
	}
#endif
#endif

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
	}

	void test_pixel_grid_size_is_the_largest_multiple_the_cell_holds() {
		TS_ASSERT_EQUALS(TtfGlyphSource::pixelGridSize(12, 10), 10);	// FT dialogue, Galmuri9
		TS_ASSERT_EQUALS(TtfGlyphSource::pixelGridSize(9, 8), 8);		// FT cell 9, Galmuri7
		TS_ASSERT_EQUALS(TtfGlyphSource::pixelGridSize(18, 12), 12);	// MI2 2x, Galmuri11
		TS_ASSERT_EQUALS(TtfGlyphSource::pixelGridSize(24, 12), 24);
		TS_ASSERT_EQUALS(TtfGlyphSource::pixelGridSize(24, 10), 20);
		TS_ASSERT_EQUALS(TtfGlyphSource::pixelGridSize(32, 16), 32);
		// A cell smaller than the design keeps the design size (and clips).
		TS_ASSERT_EQUALS(TtfGlyphSource::pixelGridSize(9, 10), 10);
		// No design size: no pixel grid.
		TS_ASSERT_EQUALS(TtfGlyphSource::pixelGridSize(12, 0), 0);
		TS_ASSERT_EQUALS(TtfGlyphSource::pixelGridSize(12, -3), 0);
		// Never past what create() accepts.
		TS_ASSERT_EQUALS(TtfGlyphSource::pixelGridSize(255, 100), 200);
		TS_ASSERT_EQUALS(TtfGlyphSource::pixelGridSize(40, 300), 0);
	}

	// FT's dialogue charset: cell 12, Galmuri9 (design 10). Held at 10, on
	// the grid, placed from the line top - exactly what the line fit gives
	// in this cell today, byte for byte.
	void test_galmuri9_held_at_design_size_in_a_12_cell() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		Common::SeekableReadStream *s1 = openFont("Galmuri9.ttf");
		Common::SeekableReadStream *s2 = openFont("Galmuri9.ttf");
		if (!s1 || !s2) {
			delete s1;
			delete s2;
			TS_SKIP("Galmuri9.ttf not found (SCUMMVM_TEST_PIXEL_FONT_DIR)");
			return;
		}
		Common::String error;
		TtfGlyphSource *px = TtfGlyphSource::createPixel(s1, DisposeAfterUse::YES, 12, 10, error);
		TS_ASSERT(px);
		TtfGlyphSource *lf = TtfGlyphSource::create(s2, DisposeAfterUse::YES, 12, error, false, true);
		TS_ASSERT(lf);
		if (!px || !lf) {
			delete px;
			delete lf;
			return;
		}
		TS_ASSERT_EQUALS(px->faceSize(), 10);
		TS_ASSERT_EQUALS(px->cellHeight(), 12);
		TS_ASSERT_EQUALS(px->lineTop(), 0);
		TS_ASSERT_EQUALS(px->baseline(), lf->baseline());
		uint n;
		const uint32 *cps = samples(n);
		for (uint i = 0; i < n; i++) {
			int t, b;
			TSM_ASSERT(Common::String::format("U+%04X on the grid", cps[i]).c_str(), binaryInk(*px, cps[i], t, b));
			TS_ASSERT_EQUALS(px->advance(cps[i]), lf->advance(cps[i]));
			for (int y = 0; y < 12; y++)
				TS_ASSERT_SAME_DATA(px->row(cps[i], y), lf->row(cps[i], y), 24);
		}
		delete px;
		delete lf;
#else
		TS_SKIP("needs FreeType");
#endif
	}

	// The size= path shrinks the same face off its grid (C25): the probe
	// set's ink (Å, brackets) is taller than 10 rows.
	void test_size_key_shrinks_galmuri9_where_pixel_does_not() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		Common::SeekableReadStream *s = openFont("Galmuri9.ttf");
		if (!s) {
			TS_SKIP("Galmuri9.ttf not found (SCUMMVM_TEST_PIXEL_FONT_DIR)");
			return;
		}
		Common::String error;
		TtfGlyphSource *src = TtfGlyphSource::create(s, DisposeAfterUse::YES, 10, error);
		TS_ASSERT(src);
		if (src)
			TS_ASSERT_LESS_THAN(src->faceSize(), 10);
		delete src;
#else
		TS_SKIP("needs FreeType");
#endif
	}

	// MI2 at 2x: charset 2's cell is 9 * 2 = 18. Galmuri11 (design 12) is
	// held at 12 in the game's own 18 cell; the line fit would open 14.
	void test_galmuri11_held_at_12_in_an_18_cell() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		Common::SeekableReadStream *s = openFont("Galmuri11.ttf");
		if (!s) {
			TS_SKIP("Galmuri11.ttf not found (SCUMMVM_TEST_PIXEL_FONT_DIR)");
			return;
		}
		Common::String error;
		TtfGlyphSource *src = TtfGlyphSource::createPixel(s, DisposeAfterUse::YES, 18, 12, error);
		TS_ASSERT(src);
		if (!src)
			return;
		TS_ASSERT_EQUALS(src->faceSize(), 12);
		TS_ASSERT_EQUALS(src->cellHeight(), 18);
		uint n;
		const uint32 *cps = samples(n);
		for (uint i = 0; i < n; i++) {
			int t, b;
			TSM_ASSERT(Common::String::format("U+%04X on the grid", cps[i]).c_str(), binaryInk(*src, cps[i], t, b));
			TS_ASSERT_LESS_THAN_EQUALS(0, t);
			TS_ASSERT_LESS_THAN(b, 18);
		}
		delete src;
#else
		TS_SKIP("needs FreeType");
#endif
	}

	// A cell twice the design: the face doubles, still on the grid.
	void test_galmuri9_doubles_in_a_24_cell() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		Common::SeekableReadStream *s = openFont("Galmuri9.ttf");
		if (!s) {
			TS_SKIP("Galmuri9.ttf not found (SCUMMVM_TEST_PIXEL_FONT_DIR)");
			return;
		}
		Common::String error;
		TtfGlyphSource *src = TtfGlyphSource::createPixel(s, DisposeAfterUse::YES, 24, 10, error);
		TS_ASSERT(src);
		if (!src)
			return;
		TS_ASSERT_EQUALS(src->faceSize(), 20);
		uint n;
		const uint32 *cps = samples(n);
		for (uint i = 0; i < n; i++) {
			int t, b;
			TSM_ASSERT(Common::String::format("U+%04X on the grid", cps[i]).c_str(), binaryInk(*src, cps[i], t, b));
			// Every ink run is an even number of rows: the design pixel doubled.
			TS_ASSERT_EQUALS((b - t + 1) % 2, 0);
		}
		delete src;
#else
		TS_SKIP("needs FreeType");
#endif
	}

	// FT's cell-9 charset with Galmuri7 (design 8): its line (8 + 1) is the
	// cell, so it sits at the top, on the grid, nothing clipped.
	void test_galmuri7_held_at_8_in_a_9_cell() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		Common::SeekableReadStream *s = openFont("Galmuri7.ttf");
		if (!s) {
			TS_SKIP("Galmuri7.ttf not found (SCUMMVM_TEST_PIXEL_FONT_DIR)");
			return;
		}
		Common::String error;
		TtfGlyphSource *src = TtfGlyphSource::createPixel(s, DisposeAfterUse::YES, 9, 8, error);
		TS_ASSERT(src);
		if (!src)
			return;
		TS_ASSERT_EQUALS(src->faceSize(), 8);
		TS_ASSERT_EQUALS(src->lineTop(), 0);
		uint n;
		const uint32 *cps = samples(n);
		for (uint i = 0; i < n; i++) {
			int t, b;
			TSM_ASSERT(Common::String::format("U+%04X on the grid", cps[i]).c_str(), binaryInk(*src, cps[i], t, b));
		}
		delete src;
#else
		TS_SKIP("needs FreeType");
#endif
	}

	// A design taller than the cell (Galmuri9, line 11, in a 9 cell) keeps
	// its size; the Hangul and Latin probes' ink is moved inside the cell by
	// whole rows, as far as it fits.
	void test_design_taller_than_the_cell_keeps_its_size_and_hangul() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		Common::SeekableReadStream *s = openFont("Galmuri9.ttf");
		if (!s) {
			TS_SKIP("Galmuri9.ttf not found (SCUMMVM_TEST_PIXEL_FONT_DIR)");
			return;
		}
		Common::String error;
		TtfGlyphSource *src = TtfGlyphSource::createPixel(s, DisposeAfterUse::YES, 9, 10, error);
		TS_ASSERT(src);
		if (!src)
			return;
		TS_ASSERT_EQUALS(src->faceSize(), 10);
		TS_ASSERT_LESS_THAN(src->lineTop(), 0);
		// 가 keeps its whole ink (top row lit), on the grid.
		int t, b;
		TS_ASSERT(binaryInk(*src, 0xAC00, t, b));
		TS_ASSERT_EQUALS(t, 0);
		delete src;
#else
		TS_SKIP("needs FreeType");
#endif
	}

	void test_create_pixel_refuses_what_create_refuses() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		Common::String error;
		TS_ASSERT(!TtfGlyphSource::createPixel(nullptr, DisposeAfterUse::YES, 12, 10, error));
		Common::SeekableReadStream *s = openFont("Galmuri9.ttf");
		if (!s) {
			TS_SKIP("Galmuri9.ttf not found (SCUMMVM_TEST_PIXEL_FONT_DIR)");
			return;
		}
		TS_ASSERT(!TtfGlyphSource::createPixel(s, DisposeAfterUse::YES, 12, 0, error));
		TS_ASSERT(!error.empty());
#else
		TS_SKIP("needs FreeType");
#endif
	}
};
