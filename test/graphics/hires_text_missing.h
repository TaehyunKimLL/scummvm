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

#include "common/array.h"
#include "graphics/hires_text/glyph_source_missing.h"
#include "graphics/hires_text/text_compose.h"

using Graphics::MissingGlyphSource;
using Graphics::UnicodeGlyphSource;

namespace {

/**
 * A 16x16 cell at 1 bpp holding U+0041 (narrow: its first row all set) and,
 * optionally, U+25A1 (wide unless asked narrow - it is East Asian
 * Ambiguous, so a source may size it either way - with a distinct pattern
 * per row, so a row read-back shows which glyph answered).
 */
class FakeBoxSource : public UnicodeGlyphSource {
public:
	explicit FakeBoxSource(bool haveBox, int boxCells = 2) : _haveBox(haveBox), _boxCells(boxCells), _a(16 * 4, 0), _box(16 * 4, 0) {
		// Rows are cellWidth()*2 = 32 px at 1 bpp: 4 bytes.
		_a[0] = 0xFF;
		for (int y = 0; y < 16; y++)
			_box[y * 4] = (byte)(y + 1);
	}

	byte cellWidth() const override { return 16; }
	byte cellHeight() const override { return 16; }
	byte advanceNarrow() const override { return 8; }
	byte advanceWide() const override { return 16; }
	int bitsPerPixel() const override { return 1; }
	int cells(uint32 cp) override {
		if (cp == 0x41)
			return 1;
		if (cp == 0x25A1 && _haveBox)
			return _boxCells;
		return 0;
	}
	const byte *row(uint32 cp, int y) override {
		if (cp == 0x41)
			return &_a[y * 4];
		if (cp == 0x25A1 && _haveBox)
			return &_box[y * 4];
		return nullptr;
	}
	uint32 glyphCount() const override { return _haveBox ? 2 : 1; }

private:
	bool _haveBox;
	int _boxCells;
	Common::Array<byte> _a, _box;
};

bool pixelOn(const byte *row, int x) {
	return Graphics::TextCompose::expandCoverage(row, x, 1) != 0;
}

} // End of anonymous namespace

class HiResTextMissingTestSuite : public CxxTest::TestSuite {
public:
	void test_a_glyph_the_inner_source_has_is_its_own() {
		FakeBoxSource inner(true);
		MissingGlyphSource src(&inner, 0x25A1, DisposeAfterUse::NO);
		TS_ASSERT_EQUALS(src.cells(0x41), 1);
		for (int y = 0; y < 16; y++)
			TS_ASSERT_EQUALS(src.row(0x41, y), inner.row(0x41, y));
		TS_ASSERT_EQUALS(src.cellWidth(), 16);
		TS_ASSERT_EQUALS(src.cellHeight(), 16);
		TS_ASSERT_EQUALS(src.bitsPerPixel(), 1);
		TS_ASSERT_EQUALS(src.substitutedCount(), 0u);
	}

	void test_a_missing_wide_character_is_the_box_glyph() {
		FakeBoxSource inner(true);
		MissingGlyphSource src(&inner, 0x25A1, DisposeAfterUse::NO);
		TS_ASSERT_EQUALS(src.cells(0xAC00), 2);
		for (int y = 0; y < 16; y++) {
			const byte *got = src.row(0xAC00, y);
			TS_ASSERT(got);
			if (got)
				TS_ASSERT_EQUALS(got[0], inner.row(0x25A1, y)[0]);
		}
		TS_ASSERT_EQUALS(src.advance(0xAC00), 16);
		Graphics::GlyphMetrics m;
		TS_ASSERT(src.metrics(0xAC00, m));
		TS_ASSERT(m.wide);
		TS_ASSERT(!m.combining);
		TS_ASSERT_EQUALS(m.advance, 16);
	}

	void test_a_missing_narrow_character_is_a_drawn_half_width_box() {
		FakeBoxSource inner(true);
		MissingGlyphSource src(&inner, 0x25A1, DisposeAfterUse::NO);
		TS_ASSERT_EQUALS(src.cells(0xE9), 1);
		// cellWidth()/2 = 8 columns, a 1 px margin all round: the box is
		// columns 1..6, rows 1..14.
		for (int y = 0; y < 16; y++) {
			const byte *row = src.row(0xE9, y);
			TS_ASSERT(row);
			if (!row)
				continue;
			for (int x = 0; x < 32; x++) {
				bool want = false;
				if (y == 1 || y == 14)
					want = x >= 1 && x <= 6;
				else if (y >= 2 && y <= 13)
					want = x == 1 || x == 6;
				TS_ASSERT_EQUALS(pixelOn(row, x), want);
			}
		}
		TS_ASSERT_EQUALS(src.advance(0xE9), 8);
		Graphics::GlyphMetrics m;
		TS_ASSERT(src.metrics(0xE9, m));
		TS_ASSERT(!m.wide);
		TS_ASSERT_EQUALS(m.advance, 8);
	}

	// A source that sizes the box glyph narrow (a face going by Unicode
	// width) has it used for narrow slots, and a wide slot gets a drawn
	// full-cell outline instead of a narrow glyph in a wide slot.
	void test_a_narrow_box_glyph_fills_narrow_slots_only() {
		FakeBoxSource inner(true, 1);
		MissingGlyphSource src(&inner, 0x25A1, DisposeAfterUse::NO);
		TS_ASSERT_EQUALS(src.cells(0xE9), 1);
		for (int y = 0; y < 16; y++)
			TS_ASSERT_EQUALS(src.row(0xE9, y), inner.row(0x25A1, y));
		TS_ASSERT_EQUALS(src.cells(0xAC00), 2);
		// Columns 1..14, rows 1..14.
		for (int y = 0; y < 16; y++) {
			const byte *row = src.row(0xAC00, y);
			TS_ASSERT(row);
			if (!row)
				continue;
			for (int x = 0; x < 32; x++) {
				bool want = false;
				if (y == 1 || y == 14)
					want = x >= 1 && x <= 14;
				else if (y >= 2 && y <= 13)
					want = x == 1 || x == 14;
				TS_ASSERT_EQUALS(pixelOn(row, x), want);
			}
		}
		TS_ASSERT_EQUALS(src.advance(0xAC00), 16);
		TS_ASSERT_EQUALS(src.advance(0xE9), 8);
	}

	void test_no_box_glyph_means_no_substitute() {
		FakeBoxSource inner(false);
		MissingGlyphSource src(&inner, 0x25A1, DisposeAfterUse::NO);
		TS_ASSERT_EQUALS(src.cells(0xAC00), 0);
		TS_ASSERT_EQUALS(src.cells(0xE9), 0);
		Graphics::GlyphMetrics m;
		TS_ASSERT(!src.metrics(0xAC00, m));
		TS_ASSERT_EQUALS(src.cells(0x41), 1);
	}

	void test_each_code_point_is_counted_once() {
		FakeBoxSource inner(true);
		MissingGlyphSource src(&inner, 0x25A1, DisposeAfterUse::NO);
		src.cells(0xAC00);
		src.cells(0xAC00);
		src.row(0xAC00, 3);
		TS_ASSERT_EQUALS(src.substitutedCount(), 1u);
		src.cells(0xE9);
		TS_ASSERT_EQUALS(src.substitutedCount(), 2u);
	}

	void test_owns_the_inner_source_when_asked() {
		MissingGlyphSource src(new FakeBoxSource(true), 0x25A1, DisposeAfterUse::YES);
		TS_ASSERT_EQUALS(src.glyphCount(), 2u);
	}
};
