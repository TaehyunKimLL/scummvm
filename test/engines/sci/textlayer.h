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

#include <cxxtest/TestSuite.h>

#include "common/rect.h"
#include "sci/graphics/textlayer.h"

class SciTextLayerTestSuite : public CxxTest::TestSuite {
public:
	static Common::Array<byte> fill(int w, int h, byte v) {
		Common::Array<byte> a;
		a.resize(w * h);
		for (uint i = 0; i < a.size(); i++)
			a[i] = v;
		return a;
	}

	void test_new_layer_is_empty() {
		Sci::TextLayer l(8, 4, 2);
		TS_ASSERT(l.isEmpty());
		TS_ASSERT(!l.rowHasText(0));
		TS_ASSERT_EQUALS(l.row(3)[7].fgCoverage, 0);
	}

	void test_put_glyph_writes_nonzero_coverage_only() {
		Sci::TextLayer l(8, 4, 2);
		const byte cov[4] = { 0, 128, 255, 0 };   // 4x1
		l.putGlyph(2, 1, cov, 4, 1, 15);
		TS_ASSERT(!l.isEmpty());
		TS_ASSERT(l.rowHasText(1));
		TS_ASSERT(!l.rowHasText(0));
		TS_ASSERT_EQUALS(l.row(1)[2].fgCoverage, 0);
		TS_ASSERT_EQUALS(l.row(1)[3].fgCoverage, 128);
		TS_ASSERT_EQUALS(l.row(1)[3].fgIndex, 15);
		TS_ASSERT_EQUALS(l.row(1)[4].fgCoverage, 255);
	}

	void test_put_glyph_clips_to_the_layer() {
		Sci::TextLayer l(4, 2, 2);
		Common::Array<byte> cov = fill(4, 4, 255);
		l.putGlyph(2, -1, cov.begin(), 4, 4, 1);   // hangs off right and top
		TS_ASSERT_EQUALS(l.row(0)[3].fgCoverage, 255);
		TS_ASSERT_EQUALS(l.row(0)[1].fgCoverage, 0);
	}

	void test_clear_lowres_rect_clears_scaled_block() {
		Sci::TextLayer l(8, 8, 2);
		Common::Array<byte> cov = fill(8, 8, 255);
		l.putGlyph(0, 0, cov.begin(), 8, 8, 1);
		l.clearLowresRect(Common::Rect(1, 1, 2, 3));   // hires x 2..3, y 2..5
		TS_ASSERT_EQUALS(l.row(2)[2].fgCoverage, 0);
		TS_ASSERT_EQUALS(l.row(5)[3].fgCoverage, 0);
		TS_ASSERT_EQUALS(l.row(1)[2].fgCoverage, 255);
		TS_ASSERT_EQUALS(l.row(6)[2].fgCoverage, 255);
		TS_ASSERT_EQUALS(l.row(2)[4].fgCoverage, 255);
	}

	void test_clear_lowres_pixel() {
		Sci::TextLayer l(4, 4, 2);
		Common::Array<byte> cov = fill(4, 4, 255);
		l.putGlyph(0, 0, cov.begin(), 4, 4, 1);
		l.clearLowresPixel(1, 0);
		TS_ASSERT_EQUALS(l.row(0)[2].fgCoverage, 0);
		TS_ASSERT_EQUALS(l.row(1)[3].fgCoverage, 0);
		TS_ASSERT_EQUALS(l.row(0)[1].fgCoverage, 255);
	}

	void test_save_restore_brings_text_back() {
		// Window over window: save the lower window's area, the upper
		// window fills it (clear), closing the upper window restores it.
		Sci::TextLayer l(8, 8, 2);
		Common::Array<byte> cov = fill(8, 8, 200);
		l.putGlyph(0, 0, cov.begin(), 8, 8, 9);
		const Common::Rect r(0, 0, 4, 4);
		Common::Array<byte> mem;
		mem.resize(l.saveSize(r));
		byte *w = mem.begin();
		l.save(r, w);
		TS_ASSERT(w <= mem.end());
		l.clearLowresRect(r);
		TS_ASSERT_EQUALS(l.row(3)[3].fgCoverage, 0);
		const byte *rd = mem.begin();
		l.restore(r, rd);
		TS_ASSERT_EQUALS(rd, (const byte *)w);
		TS_ASSERT_EQUALS(l.row(3)[3].fgCoverage, 200);
		TS_ASSERT_EQUALS(l.row(3)[3].fgIndex, 9);
	}

	void test_restore_of_an_empty_save_clears() {
		Sci::TextLayer l(8, 8, 2);
		const Common::Rect r(0, 0, 2, 2);
		Common::Array<byte> mem;
		mem.resize(l.saveSize(r));
		byte *w = mem.begin();
		l.save(r, w);                 // nothing there
		Common::Array<byte> cov = fill(4, 4, 255);
		l.putGlyph(0, 0, cov.begin(), 4, 4, 1);
		const byte *rd = mem.begin();
		l.restore(r, rd);             // restores "no text"
		TS_ASSERT_EQUALS(l.row(0)[0].fgCoverage, 0);
	}
};
