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

/*
 * Tests for layoutFaceChain() (where GfxCache puts every face of a chain
 * and the .uni bundle in one cell) and for NormalizedGlyphSource presenting
 * a source below row 0 of its cell.
 */

#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/str.h"
#include "graphics/hires_text/chain_layout.h"
#include "graphics/hires_text/glyph_source_fallback.h"

namespace {

Graphics::ChainFaceInfo bitmapFace(int w, int h, int bpp, int baseline) {
	Graphics::ChainFaceInfo f;
	f.cellWidth = w;
	f.cellHeight = h;
	f.bpp = bpp;
	f.baselineRow = baseline;
	return f;
}

Graphics::ChainFaceInfo ttfFace(int w, int h, int pad, int baseline) {
	Graphics::ChainFaceInfo f;
	f.cellWidth = w;
	f.cellHeight = h;
	f.rowPad = pad;
	f.bpp = 8;
	f.baselineRow = baseline;
	f.trueType = true;
	return f;
}

/** A 1 bpp source of one glyph (U+0041) in a 4x3 cell: row y has bit x set for x == y. */
class DiagonalSource : public Graphics::UnicodeGlyphSource {
public:
	byte cellWidth() const override { return 4; }
	byte cellHeight() const override { return 3; }
	byte advanceNarrow() const override { return 2; }
	byte advanceWide() const override { return 4; }
	int bitsPerPixel() const override { return 1; }
	int cells(uint32 cp) override { return cp == 0x41 ? 1 : 0; }
	const byte *row(uint32 cp, int y) override {
		_row = (byte)(0x80 >> y);
		return &_row;
	}
	uint32 glyphCount() const override { return 1; }

private:
	byte _row = 0;
};

} // End of anonymous namespace

class HiResTextChainLayoutTestSuite : public CxxTest::TestSuite {
public:
	void test_empty_chain_gives_an_empty_layout() {
		Common::Array<Graphics::ChainFaceInfo> faces;
		const Graphics::ChainLayout l = Graphics::layoutFaceChain(faces, true, 16, 16);
		TS_ASSERT_EQUALS(l.tops.size(), 0u);
		TS_ASSERT_EQUALS(l.normalize.size(), 0u);
	}

	// An 18 px font (ascent 15) and a 16 px one (ascent 12): their baselines
	// fall on one row of the chain's cell.
	void test_bitmap_faces_of_two_heights_share_a_baseline() {
		Common::Array<Graphics::ChainFaceInfo> faces;
		faces.push_back(bitmapFace(18, 18, 2, 15));
		faces.push_back(bitmapFace(16, 16, 1, 12));
		const Graphics::ChainLayout l = Graphics::layoutFaceChain(faces, false, 0, 0);
		TS_ASSERT(l.byBaseline);
		TS_ASSERT_EQUALS(l.tops.size(), 2u);
		TS_ASSERT_EQUALS(l.tops[0], 0);
		TS_ASSERT_EQUALS(l.tops[1], 3);
		TS_ASSERT_EQUALS(l.tops[0] + 15, l.tops[1] + 12);
		TS_ASSERT_EQUALS(l.cellWidth, 18);
		TS_ASSERT_EQUALS(l.cellHeight, 19);	// the 16 px face reaches row 3 + 16
		TS_ASSERT(l.normalize[0]);			// 2 bpp
		TS_ASSERT(l.normalize[1]);
	}

	// The same two the other way round: the taller ascent is still at row 0.
	void test_the_face_with_the_lower_baseline_is_moved_down() {
		Common::Array<Graphics::ChainFaceInfo> faces;
		faces.push_back(bitmapFace(16, 16, 1, 12));
		faces.push_back(bitmapFace(18, 18, 8, 15));
		const Graphics::ChainLayout l = Graphics::layoutFaceChain(faces, true, 16, 16);
		TS_ASSERT_EQUALS(l.tops[0], 3);
		TS_ASSERT_EQUALS(l.tops[1], 0);
		TS_ASSERT_EQUALS(l.cellHeight, 19);
		// The .uni bundle starts where the first face does.
		TS_ASSERT_EQUALS(l.uniTop, 3);
		// 8 bpp but not the chain's cell: normalised too.
		TS_ASSERT(l.normalize[1]);
	}

	// What faceChainFor() always did with TrueType faces only: the first
	// face's cell, every face at row 0, none normalised, the .uni bundle at
	// the first face's row pad - even when the bundle is larger.
	void test_truetype_only_chain_is_laid_out_as_before() {
		Common::Array<Graphics::ChainFaceInfo> faces;
		faces.push_back(ttfFace(16, 24, 4, 18));
		faces.push_back(ttfFace(18, 28, 2, 20));
		for (int uni = 0; uni < 3; uni++) {
			const int uniSize = uni == 2 ? 40 : 16;
			const Graphics::ChainLayout l = Graphics::layoutFaceChain(faces, uni > 0, uniSize, uniSize);
			TS_ASSERT(!l.byBaseline);
			TS_ASSERT_EQUALS(l.cellWidth, 16);
			TS_ASSERT_EQUALS(l.cellHeight, 24);
			TS_ASSERT_EQUALS(l.tops.size(), 2u);
			TS_ASSERT_EQUALS(l.tops[0], 0);
			TS_ASSERT_EQUALS(l.tops[1], 0);
			TS_ASSERT(!l.normalize[0]);
			TS_ASSERT(!l.normalize[1]);
			TS_ASSERT_EQUALS(l.uniTop, 4);
		}
	}

	void test_a_larger_uni_bundle_grows_the_cell() {
		Common::Array<Graphics::ChainFaceInfo> faces;
		faces.push_back(bitmapFace(16, 16, 1, 12));
		const Graphics::ChainLayout l = Graphics::layoutFaceChain(faces, true, 20, 18);
		TS_ASSERT_EQUALS(l.cellWidth, 20);
		TS_ASSERT_EQUALS(l.cellHeight, 18);
		TS_ASSERT_EQUALS(l.uniTop, 0);
		TS_ASSERT(l.normalize[0]);
	}

	void test_a_smaller_uni_bundle_leaves_the_cell() {
		Common::Array<Graphics::ChainFaceInfo> faces;
		faces.push_back(bitmapFace(16, 16, 1, 12));
		const Graphics::ChainLayout l = Graphics::layoutFaceChain(faces, true, 12, 12);
		TS_ASSERT_EQUALS(l.cellWidth, 16);
		TS_ASSERT_EQUALS(l.cellHeight, 16);
		TS_ASSERT_EQUALS(l.tops[0], 0);
	}

	// A lone face with nothing behind it is the chain itself, whatever its depth.
	void test_a_lone_face_without_a_bundle_is_not_normalised() {
		Common::Array<Graphics::ChainFaceInfo> faces;
		faces.push_back(bitmapFace(16, 16, 1, 12));
		const Graphics::ChainLayout l = Graphics::layoutFaceChain(faces, false, 0, 0);
		TS_ASSERT(!l.normalize[0]);
		TS_ASSERT_EQUALS(l.cellWidth, 16);
		TS_ASSERT_EQUALS(l.cellHeight, 16);
	}

	// An 8 bpp face that is the chain's cell at row 0 joins as it is.
	void test_a_face_that_fills_the_cell_at_8bpp_is_not_normalised() {
		Common::Array<Graphics::ChainFaceInfo> faces;
		faces.push_back(bitmapFace(18, 18, 8, 15));
		faces.push_back(bitmapFace(16, 16, 1, 12));
		faces.push_back(bitmapFace(16, 14, 1, 11));
		const Graphics::ChainLayout l = Graphics::layoutFaceChain(faces, true, 16, 16);
		TS_ASSERT_EQUALS(l.cellHeight, 19);
		TS_ASSERT_EQUALS(l.tops[2], 4);
		TS_ASSERT_EQUALS(l.cellHeight, 19);
		// 18 rows in a 19-row cell: normalised.
		TS_ASSERT(l.normalize[0]);

		Common::Array<Graphics::ChainFaceInfo> same;
		same.push_back(bitmapFace(18, 18, 8, 15));
		same.push_back(bitmapFace(16, 16, 1, 13));
		const Graphics::ChainLayout s = Graphics::layoutFaceChain(same, true, 16, 16);
		TS_ASSERT_EQUALS(s.cellHeight, 18);
		TS_ASSERT_EQUALS(s.tops[1], 2);
		TS_ASSERT(!s.normalize[0]);
		TS_ASSERT(s.normalize[1]);
	}

	// A TrueType face in front of a bitmap font: both by their baselines
	// (the face's includes its row pad); the bundle below the row pad.
	void test_truetype_and_bitmap_faces_share_a_baseline() {
		Common::Array<Graphics::ChainFaceInfo> faces;
		faces.push_back(ttfFace(16, 24, 4, 18));
		faces.push_back(bitmapFace(16, 16, 1, 12));
		const Graphics::ChainLayout l = Graphics::layoutFaceChain(faces, true, 16, 16);
		TS_ASSERT(l.byBaseline);
		TS_ASSERT_EQUALS(l.tops[0], 0);
		TS_ASSERT_EQUALS(l.tops[1], 6);
		TS_ASSERT_EQUALS(l.cellHeight, 24);
		TS_ASSERT_EQUALS(l.uniTop, 4);
		TS_ASSERT(!l.normalize[0]);
		TS_ASSERT(l.normalize[1]);
	}

	// A face with no baseline: the M1 rule, cells proper on one row.
	void test_without_every_baseline_the_row_pads_line_up() {
		Common::Array<Graphics::ChainFaceInfo> faces;
		faces.push_back(bitmapFace(16, 16, 1, -1));
		faces.push_back(ttfFace(16, 24, 4, 18));
		const Graphics::ChainLayout l = Graphics::layoutFaceChain(faces, true, 16, 16);
		TS_ASSERT(!l.byBaseline);
		TS_ASSERT_EQUALS(l.tops[0], 4);
		TS_ASSERT_EQUALS(l.tops[1], 0);
		TS_ASSERT_EQUALS(l.cellWidth, 16);
		TS_ASSERT_EQUALS(l.cellHeight, 24);
		TS_ASSERT_EQUALS(l.uniTop, 4);
	}

	// NormalizedGlyphSource with its source below row 0: the rows above are
	// blank, the source's rows follow shifted down, the rows below are blank.
	void test_normalized_source_at_a_top_row() {
		DiagonalSource src;
		Common::String error;
		Graphics::NormalizedGlyphSource *n =
			Graphics::NormalizedGlyphSource::create(&src, 6, 7, 2, DisposeAfterUse::NO, error);
		TS_ASSERT(n != nullptr);
		if (!n)
			return;
		TS_ASSERT_EQUALS(n->cellWidth(), 6);
		TS_ASSERT_EQUALS(n->cellHeight(), 7);
		TS_ASSERT_EQUALS(n->bitsPerPixel(), 8);
		for (int y = 0; y < 7; y++) {
			const byte *row = n->row(0x41, y);
			TS_ASSERT(row != nullptr);
			for (int x = 0; x < 12 && row; x++) {
				const bool ink = y >= 2 && y < 5 && x == y - 2;
				TS_ASSERT_EQUALS(row[x], ink ? 255 : 0);
			}
		}
		delete n;
	}

	// A source that would run past the cell's bottom at that row is refused.
	void test_normalized_source_refuses_a_top_row_past_the_cell() {
		DiagonalSource src;
		Common::String error;
		TS_ASSERT(Graphics::NormalizedGlyphSource::create(&src, 6, 4, 2, DisposeAfterUse::NO, error) == nullptr);
		TS_ASSERT(!error.empty());
		Graphics::NormalizedGlyphSource *n = Graphics::NormalizedGlyphSource::create(&src, 6, 5, 2, DisposeAfterUse::NO, error);
		TS_ASSERT(n != nullptr);
		delete n;
	}
};
