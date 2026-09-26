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
#include "common/endian.h"
#include "common/str.h"
#include "common/memstream.h"
#include "common/stream.h"
#include "sci/graphics/glyphsource_scvmuni.h"
#include "sci/graphics/glyphsource_ttf.h"

#include "../../system/null_osystem.h"

#ifdef USE_FREETYPE2
#include "common/fs.h"
#endif

using Sci::ScvmuniGlyphSource;
using Sci::TtfGlyphSource;

namespace {

void putU16(Common::Array<byte> &d, uint16 v) {
	d.push_back(v & 0xFF);
	d.push_back((v >> 8) & 0xFF);
}

void putU32(Common::Array<byte> &d, uint32 v) {
	d.push_back(v & 0xFF);
	d.push_back((v >> 8) & 0xFF);
	d.push_back((v >> 16) & 0xFF);
	d.push_back((v >> 24) & 0xFF);
}

/**
 * Builds a minimal SCVMUNI buffer: cell 2x2, glyphs {U+0041 narrow (1 cell),
 * U+AC00 wide (2 cells), U+AC01 wide (2 cells)}, in that (sorted) code point
 * order. bpp is 1 or 8; rowBytes = (cellWidth*2*bpp + 7) / 8 as the format
 * requires - 1 for 1bpp, 4 for 8bpp.
 *
 * Bitmaps are all zero except one marked pixel per glyph, at (x=1, y=0) for
 * the narrow glyph and (x=2, y=1) for the wide ones, so a row read-back can
 * be checked against a known value.
 */
Common::Array<byte> makeBundle(int bpp) {
	const byte cellWidth = 2, cellHeight = 2;
	const byte advanceNarrow = 1, advanceWide = 2;
	const uint32 glyphCount = 3;
	const uint32 rowBytes = ((uint32)cellWidth * 2 * bpp + 7) / 8;
	const uint32 bytesPerGlyph = rowBytes * cellHeight;

	const uint32 cpOff = 32;	// right after the 32-byte fixed header
	const uint32 wOff = cpOff + glyphCount * 4;
	const uint32 bmOff = wOff + glyphCount;

	Common::Array<byte> d;
	d.push_back('S'); d.push_back('C'); d.push_back('V'); d.push_back('M');
	d.push_back('U'); d.push_back('N'); d.push_back('I'); d.push_back(0);
	putU16(d, 1);								// version
	putU16(d, bpp == 8 ? 2 : 0);				// flags: bit1 = 8bpp
	d.push_back(cellWidth);
	d.push_back(cellHeight);
	d.push_back(advanceNarrow);
	d.push_back(advanceWide);
	putU32(d, glyphCount);
	putU32(d, cpOff);
	putU32(d, wOff);
	putU32(d, bmOff);
	TS_ASSERT_EQUALS((uint32)d.size(), cpOff);

	putU32(d, 0x0041);
	putU32(d, 0xAC00);
	putU32(d, 0xAC01);
	TS_ASSERT_EQUALS((uint32)d.size(), wOff);

	d.push_back(1);	// U+0041 narrow
	d.push_back(2);	// U+AC00 wide
	d.push_back(2);	// U+AC01 wide
	TS_ASSERT_EQUALS((uint32)d.size(), bmOff);

	for (uint32 g = 0; g < glyphCount; g++) {
		Common::Array<byte> glyph(bytesPerGlyph, 0);
		if (bpp == 1) {
			if (g == 0)
				glyph[0] = 0x40;			// row 0, bit for x=1 set (MSB first)
			else
				glyph[rowBytes] = 0x20;	// row 1, bit for x=2 set
		} else { // 8bpp coverage
			if (g == 0)
				glyph[1] = 200;				// row 0, x=1
			else
				glyph[rowBytes + 2] = 180;	// row 1, x=2
		}
		for (uint32 i = 0; i < bytesPerGlyph; i++)
			d.push_back(glyph[i]);
	}

	return d;
}

} // namespace

class SciGlyphSourceScvmuniTestSuite : public CxxTest::TestSuite {
public:
	void test_parse_valid_1bpp() {
		Common::String error;
		Common::Array<byte> data = makeBundle(1);
		ScvmuniGlyphSource *src = ScvmuniGlyphSource::create(Common::move(data), "test.uni", error);
		TS_ASSERT(src != nullptr);
		TS_ASSERT(error.empty());
		if (!src)
			return;

		TS_ASSERT_EQUALS(src->cells(0x0041), 1);
		TS_ASSERT_EQUALS(src->cells(0xAC00), 2);
		TS_ASSERT_EQUALS(src->cells(0x0042), 0);	// not in the table: a clean miss

		TS_ASSERT_EQUALS(src->cellWidth(), 2);
		TS_ASSERT_EQUALS(src->cellHeight(), 2);
		TS_ASSERT_EQUALS(src->advanceNarrow(), 1);
		TS_ASSERT_EQUALS(src->advanceWide(), 2);
		TS_ASSERT_EQUALS(src->bitsPerPixel(), 1);
		TS_ASSERT_EQUALS(src->glyphCount(), (uint32)3);

		delete src;
	}

	void test_row_bytes_1bpp() {
		Common::String error;
		ScvmuniGlyphSource *src = ScvmuniGlyphSource::create(makeBundle(1), "test.uni", error);
		TS_ASSERT(src != nullptr);
		if (!src)
			return;

		// Known pixel: U+0041 row 0 has bit x=1 set (0x40 = 0100 0000).
		const byte *row0 = src->row(0x0041, 0);
		TS_ASSERT_EQUALS(row0[0], 0x40);

		// U+AC00 row 1 has bit x=2 set (0x20 = 0010 0000).
		const byte *row1 = src->row(0xAC00, 1);
		TS_ASSERT_EQUALS(row1[0], 0x20);

		delete src;
	}

	void test_parse_8bpp_row_is_coverage() {
		Common::String error;
		ScvmuniGlyphSource *src = ScvmuniGlyphSource::create(makeBundle(8), "test8.uni", error);
		TS_ASSERT(src != nullptr);
		if (!src)
			return;

		TS_ASSERT_EQUALS(src->bitsPerPixel(), 8);
		const byte *row0 = src->row(0x0041, 0);
		TS_ASSERT_EQUALS(row0[1], 200);
		const byte *row1 = src->row(0xAC00, 1);
		TS_ASSERT_EQUALS(row1[2], 180);

		delete src;
	}

	void test_rejects_bad_magic() {
		Common::Array<byte> data = makeBundle(1);
		data[0] = 'X';
		Common::String error;
		ScvmuniGlyphSource *src = ScvmuniGlyphSource::create(Common::move(data), "bad.uni", error);
		TS_ASSERT(src == nullptr);
		TS_ASSERT(!error.empty());
	}

	void test_rejects_both_bpp_flags() {
		Common::Array<byte> data = makeBundle(1);
		data[10] = 3;	// both bit0 (2bpp) and bit1 (8bpp) set
		data[11] = 0;
		Common::String error;
		ScvmuniGlyphSource *src = ScvmuniGlyphSource::create(Common::move(data), "bad.uni", error);
		TS_ASSERT(src == nullptr);
		TS_ASSERT(!error.empty());
	}

	void test_rejects_unsorted() {
		Common::Array<byte> data = makeBundle(1);
		// Code point table starts at offset 32: swap the first two entries so
		// it is no longer ascending.
		for (int i = 0; i < 4; i++) {
			const byte t = data[32 + i];
			data[32 + i] = data[36 + i];
			data[36 + i] = t;
		}
		Common::String error;
		ScvmuniGlyphSource *src = ScvmuniGlyphSource::create(Common::move(data), "bad.uni", error);
		TS_ASSERT(src == nullptr);
		TS_ASSERT(!error.empty());
	}

	void test_rejects_truncated_table() {
		Common::Array<byte> data = makeBundle(1);
		data.resize(data.size() - 4);	// bitmap table no longer fits
		Common::String error;
		ScvmuniGlyphSource *src = ScvmuniGlyphSource::create(Common::move(data), "bad.uni", error);
		TS_ASSERT(src == nullptr);
		TS_ASSERT(!error.empty());
	}
};

// Apple's Korean system font: face 0 of a .ttc, used only for local testing.
// Absent (non-macOS, or an unusual install), every test below skips itself.
static const char *kTestTtcPath = "/System/Library/Fonts/AppleSDGothicNeo.ttc";

class SciGlyphSourceTtfTestSuite : public CxxTest::TestSuite {
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

	// isWide() reads a plain static table: it must work in every build,
	// including one with no FreeType, since layout needs glyph widths
	// whether or not a live face backs them.
	void test_is_wide_table() {
		TS_ASSERT(TtfGlyphSource::isWide(0xAC00));	// 가: hangul syllable
		TS_ASSERT(TtfGlyphSource::isWide(0x3000));	// ideographic space
		TS_ASSERT(TtfGlyphSource::isWide(0xFF21));	// fullwidth A
		TS_ASSERT(TtfGlyphSource::isWide(0x4E00));	// 一: CJK ideograph

		TS_ASSERT(!TtfGlyphSource::isWide(0x0041));	// A
		TS_ASSERT(!TtfGlyphSource::isWide(0x2500));	// box drawing light horizontal
		TS_ASSERT(!TtfGlyphSource::isWide(0x00B0));	// degree sign

		TS_ASSERT(TtfGlyphSource::isWide(0x1F600));	// 😀: emoji
	}

	// chooseFitSize() is a pure helper (no Font/FreeType dependency), so the
	// load-time raster budget it enforces can be pinned directly with fake
	// measure callbacks, in every build.

	// The pathological case: a measure that never reports a fit. Before this
	// test was added, the vertical-fit retry re-measured the whole probe set
	// (26 code points) at every candidate size, so against a real font whose
	// initial ink box narrowly missed the cell, total rasterisations reached
	// ~46-52 - well past the plan's "at most 32" load-time budget. This pins
	// that the bound is now structural: it stops calling measure before the
	// total would exceed maxRasterCount, and settles on the smallest size it
	// actually tried.
	void test_choose_fit_size_caps_raster_budget() {
		uint32 rasterCount = 26;	// as if the 26-probe initial pass just ran
		int top = 0, bottom = 0;
		int measureCalls = 0;

		auto neverFits = [&](int trySize, int &t, int &b) -> bool {
			measureCalls++;
			t = 0;
			b = 100;	// always "too tall", regardless of trySize
			return false;
		};

		const int chosen = TtfGlyphSource::chooseFitSize(16, 6, 2, rasterCount, 32, neverFits, top, bottom);

		TS_ASSERT(rasterCount <= 32);
		TS_ASSERT_EQUALS(rasterCount, (uint32)32);
		TS_ASSERT_EQUALS(measureCalls, 3);
		TS_ASSERT_EQUALS(chosen, 13);	// the smallest size actually tried: 15, 14, 13
	}

	// The common case: a measure that fits on the very first try must stop
	// immediately (one call), not keep searching smaller sizes.
	void test_choose_fit_size_stops_at_first_fit() {
		uint32 rasterCount = 26;
		int top = 0, bottom = 0;
		int measureCalls = 0;

		auto fitsImmediately = [&](int trySize, int &t, int &b) -> bool {
			measureCalls++;
			t = 0;
			b = 10;
			return true;
		};

		const int chosen = TtfGlyphSource::chooseFitSize(16, 6, 2, rasterCount, 32, fitsImmediately, top, bottom);

		TS_ASSERT_EQUALS(measureCalls, 1);
		TS_ASSERT_EQUALS(rasterCount, (uint32)28);
		TS_ASSERT_EQUALS(chosen, 15);
		TS_ASSERT_EQUALS(top, 0);
		TS_ASSERT_EQUALS(bottom, 10);
	}

	// No headroom left at all: measure must never be called, and the
	// original size (nothing was tried) must come back unchanged.
	void test_choose_fit_size_no_budget_for_any_retry() {
		uint32 rasterCount = 32;	// already at the cap
		int top = 5, bottom = 9;
		int measureCalls = 0;

		auto shouldNotBeCalled = [&](int trySize, int &t, int &b) -> bool {
			measureCalls++;
			t = 0;
			b = 0;
			return true;
		};

		const int chosen = TtfGlyphSource::chooseFitSize(16, 6, 2, rasterCount, 32, shouldNotBeCalled, top, bottom);

		TS_ASSERT_EQUALS(measureCalls, 0);
		TS_ASSERT_EQUALS(rasterCount, (uint32)32);
		TS_ASSERT_EQUALS(chosen, 16);
		TS_ASSERT_EQUALS(top, 5);	// unchanged: no candidate was tried
		TS_ASSERT_EQUALS(bottom, 9);
	}

	// Every FreeType test below needs a live face; skip cleanly (rather than
	// fail) where the build has no FreeType or the test font is not
	// installed. cxxtestgen's generated runner calls every method it finds
	// regardless of which branch of an #ifdef guards it, so every method in
	// this suite must be declared unconditionally: only bodies are guarded.
	Common::SeekableReadStream *openTestFont() {
#ifdef USE_FREETYPE2
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::FSNode node(kTestTtcPath);
		if (!node.exists()) {
			TS_SKIP("Apple SD Gothic Neo not present on this machine");
			return nullptr;
		}
		Common::SeekableReadStream *stream = node.createReadStream();
		if (!stream)
			TS_SKIP("Apple SD Gothic Neo could not be opened");
		return stream;
#else
		TS_SKIP("no real filesystem access in this test environment");
		return nullptr;
#endif
#else
		TS_SKIP("this build has no FreeType");
		return nullptr;
#endif
	}

	void test_create_rasterises_only_probes() {
		Common::SeekableReadStream *stream = openTestFont();
		if (!stream)
			return;

		Common::String error;
		TtfGlyphSource *src = TtfGlyphSource::create(stream, DisposeAfterUse::YES, 16, error);
		TS_ASSERT(src != nullptr);
		if (!src)
			return;

		TS_ASSERT(src->rasterCount() <= 32);
		TS_ASSERT_EQUALS(src->glyphCount(), (uint32)0);

		delete src;
	}

	void test_second_request_is_cached() {
		Common::SeekableReadStream *stream = openTestFont();
		if (!stream)
			return;

		Common::String error;
		TtfGlyphSource *src = TtfGlyphSource::create(stream, DisposeAfterUse::YES, 16, error);
		TS_ASSERT(src != nullptr);
		if (!src)
			return;

		const uint32 beforeFirst = src->rasterCount();
		TS_ASSERT_EQUALS(src->cells(0xAC00), 2);
		const uint32 afterFirst = src->rasterCount();
		TS_ASSERT_EQUALS(afterFirst, beforeFirst + 1);

		TS_ASSERT_EQUALS(src->cells(0xAC00), 2);
		src->row(0xAC00, 0);
		TS_ASSERT_EQUALS(src->rasterCount(), afterFirst);

		delete src;
	}

	void test_missing_is_cached() {
		Common::SeekableReadStream *stream = openTestFont();
		if (!stream)
			return;

		Common::String error;
		TtfGlyphSource *src = TtfGlyphSource::create(stream, DisposeAfterUse::YES, 16, error);
		TS_ASSERT(src != nullptr);
		if (!src)
			return;

		const uint32 before = src->rasterCount();
		TS_ASSERT_EQUALS(src->cells(0xE000), 0);	// private use area: not in the face
		const uint32 afterFirst = src->rasterCount();
		TS_ASSERT_EQUALS(afterFirst, before + 1);

		TS_ASSERT_EQUALS(src->cells(0xE000), 0);
		TS_ASSERT_EQUALS(src->rasterCount(), afterFirst);

		delete src;
	}

	void test_coverage_is_eight_bit() {
		Common::SeekableReadStream *stream = openTestFont();
		if (!stream)
			return;

		Common::String error;
		TtfGlyphSource *src = TtfGlyphSource::create(stream, DisposeAfterUse::YES, 16, error);
		TS_ASSERT(src != nullptr);
		if (!src)
			return;

		TS_ASSERT_EQUALS(src->cells(0xAC00), 2);	// 가

		bool sawFull = false, sawPartial = false;
		for (int y = 0; y < src->cellHeight(); y++) {
			const byte *row = src->row(0xAC00, y);
			TS_ASSERT(row != nullptr);
			if (!row)
				continue;
			for (int x = 0; x < src->cellWidth() * 2; x++) {
				const byte v = row[x];
				if (v == 255)
					sawFull = true;
				else if (v >= 1 && v <= 254)
					sawPartial = true;
			}
		}
		TS_ASSERT(sawFull);
		TS_ASSERT(sawPartial);

		delete src;
	}

	void test_ttc_face0() {
		Common::SeekableReadStream *stream = openTestFont();
		if (!stream)
			return;

		Common::String error;
		TtfGlyphSource *src = TtfGlyphSource::create(stream, DisposeAfterUse::YES, 16, error);
		TS_ASSERT(src != nullptr);
		TS_ASSERT(error.empty());

		delete src;
	}

	void test_width_without_draw() {
		Common::SeekableReadStream *stream = openTestFont();
		if (!stream)
			return;

		Common::String error;
		TtfGlyphSource *src = TtfGlyphSource::create(stream, DisposeAfterUse::YES, 16, error);
		TS_ASSERT(src != nullptr);
		if (!src)
			return;

		TS_ASSERT_EQUALS(src->cells(0x0041), 1);	// A: narrow
		TS_ASSERT_EQUALS(src->cells(0xAC01), 2);	// 각: wide

		delete src;
	}

	void test_no_freetype_stub() {
#ifndef USE_FREETYPE2
		byte dummy[4] = { 0, 0, 0, 0 };
		Common::MemoryReadStream stream(dummy, sizeof(dummy));
		Common::String error;
		TtfGlyphSource *src = TtfGlyphSource::create(&stream, DisposeAfterUse::NO, 16, error);
		TS_ASSERT(src == nullptr);
		TS_ASSERT(!error.empty());
#else
		TS_SKIP("this build has FreeType");
#endif
	}
};
