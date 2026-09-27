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


// C11 T3b: a TrueType face's vertical fit must keep the ink of every
// character the translation uses inside the cell - including combining
// marks below the base (Thai SARA U / SARA UU / PHINTHU), which a face's
// own line metrics do not cover. Measured against an unclipped rendering of
// the same face at the size and offset the source settled on.

#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/str.h"
#include "common/memstream.h"
#include "common/stream.h"
#include "graphics/hires_text/glyph_source_ttf.h"

#include "../system/null_osystem.h"

#ifdef USE_FREETYPE2
#include "common/fs.h"
#include "graphics/font.h"
#include "graphics/fonts/ttf.h"
#include "graphics/managed_surface.h"
#endif

using Graphics::TtfGlyphSource;

class HiResTextTtfFitTestSuite : public CxxTest::TestSuite {
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

#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
	// Sukhumvit Set: its hhea line (1103 + 474 units) is far taller than
	// its OS/2 win line (834 + 250), which kTTFSizeModeCell sizes by, so a
	// line-fitted face puts the baseline below the cell and SARA UU under
	// it. The system .ttc's face 0 ("Thin") has the same metrics as the
	// extracted "Text" face the C11 maps name.
	static Common::FSNode sukhumvitNode() {
		Common::FSNode node("/System/Library/Fonts/Supplemental/SukhumvitSet.ttc");
		if (!node.exists())
			node = Common::FSNode("/Users/juami/work/scummvm/runs/c11/data/fonts/sukhumvit-text.ttf");
		return node;
	}

	static Common::FSNode sdGothicNode() {
		return Common::FSNode("/System/Library/Fonts/AppleSDGothicNeo.ttc");
	}

	// The fit's own ink threshold (m7mkfont.py's INK_THRESHOLD): a fainter
	// antialiasing fringe (a few /255) may fall outside the cell.
	static const int kInkThreshold = 40;

	/**
	 * Ink rows of cp that the source's cell loses: cp is drawn by a
	 * separately opened face of the same file at the source's faceSize(),
	 * with its line top at the source's lineTop(), on a canvas three cells
	 * tall around the cell; every row with ink (coverage >= kInkThreshold)
	 * outside the cell counts.
	 * Also checks that the rows inside the cell are the source's own.
	 */
	static int lostInkRows(TtfGlyphSource *src, const Common::FSNode &node, bool lineFit, uint32 cp) {
		Common::SeekableReadStream *stream = node.createReadStream();
		if (!stream)
			return -1;
		Graphics::Font *font = Graphics::loadTTFFont(stream, DisposeAfterUse::YES, src->faceSize(),
		                                             lineFit ? Graphics::kTTFSizeModeCell : Graphics::kTTFSizeModeCharacter,
		                                             0, 0, Graphics::kTTFRenderModeLight);
		if (!font)
			return -1;
		const int cellH = src->cellHeight(), w = src->cellWidth() * 2;
		Graphics::GlyphMetrics m;
		int originX = 0;
		if (src->metrics(cp, m))
			originX = m.originX;
		Graphics::ManagedSurface surf(w, cellH * 3, Graphics::PixelFormat::createFormatARGB32());
		font->drawChar(&surf, cp, originX, cellH + src->lineTop(), surf.format.ARGBToColor(255, 255, 255, 255));
		int lost = 0, differ = 0;
		for (int y = 0; y < cellH * 3; y++) {
			bool ink = false;
			for (int x = 0; x < w; x++) {
				uint8 a, r, g, b;
				surf.format.colorToARGB(surf.getPixel(x, y), a, r, g, b);
				if (y >= cellH && y < cellH * 2) {
					const byte *row = src->cells(cp) > 0 ? src->row(cp, y - cellH) : nullptr;
					if ((row ? row[x] : 0) != a)
						differ++;
				}
				ink = ink || a >= kInkThreshold;
			}
			if (ink && (y < cellH || y >= cellH * 2))
				lost++;
		}
		delete font;
		TS_ASSERT_EQUALS(differ, 0);
		return lost;
	}

	static void thaiSample(Common::Array<uint32> &sample) {
		// ผู้ ปุ่ม ฤ ฎ ฏ ญ ปี่ ฺ, as a translation's sample would hold them
		// (non-ASCII ascending, then ASCII).
		const uint32 cps[] = {
			0x0E0D, 0x0E0E, 0x0E0F, 0x0E1B, 0x0E1C, 0x0E21, 0x0E24, 0x0E35,
			0x0E38, 0x0E39, 0x0E3A, 0x0E48, 0x0E49, 0x0E4B, 'g', 'j'
		};
		sample = Common::Array<uint32>(cps, ARRAYSIZE(cps));
	}

	void checkThai(bool lineFit, int pixelSize) {
		Common::FSNode node = sukhumvitNode();
		if (!node.exists())
			return; // not on this machine: nothing to measure
		Common::Array<uint32> sample;
		thaiSample(sample);
		Common::String error;
		TtfGlyphSource *src = TtfGlyphSource::create(node.createReadStream(), DisposeAfterUse::YES, pixelSize, error,
		                                             false, lineFit, sample.data(), sample.size());
		TS_ASSERT(src != nullptr);
		if (!src)
			return;
		TS_ASSERT_EQUALS((int)src->cellHeight(), pixelSize);
		for (uint i = 0; i < sample.size(); i++) {
			// Drawn (a mark pushed out of the cell read as "missing")...
			TS_ASSERT(src->cells(sample[i]) > 0);
			// ...and whole.
			TS_ASSERT_EQUALS(lostInkRows(src, node, lineFit, sample[i]), 0);
		}
		delete src;
	}
#endif

	void test_line_fit_keeps_thai_below_marks_in_the_cell() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		checkThai(true, 16);
		checkThai(true, 24);
#endif
	}

	void test_fit_keeps_thai_below_marks_in_the_cell() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		checkThai(false, 16);
		checkThai(false, 24);
#endif
	}

	// The invariant: a line-fitted face whose sample already lies inside
	// the cell (Korean with Apple SD Gothic Neo) draws exactly what it drew
	// without a sample.
	void test_line_fit_sample_that_fits_changes_nothing() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		Common::FSNode node = sdGothicNode();
		if (!node.exists())
			return;
		const uint32 cps[] = { 0xAC00, 0xB620, 0xBDC1, 0xD7A3, 0x3131, 0x300C, 'g', 'j', '|', '(' };
		const uint sizes[] = { 16, 24, 32 };
		for (uint s = 0; s < ARRAYSIZE(sizes); s++) {
			Common::String error;
			TtfGlyphSource *plain = TtfGlyphSource::create(node.createReadStream(), DisposeAfterUse::YES, sizes[s], error,
			                                               true, true);
			TtfGlyphSource *fitted = TtfGlyphSource::create(node.createReadStream(), DisposeAfterUse::YES, sizes[s], error,
			                                                true, true, cps, ARRAYSIZE(cps));
			TS_ASSERT(plain && fitted);
			if (!plain || !fitted) {
				delete plain;
				delete fitted;
				return;
			}
			TS_ASSERT_EQUALS(fitted->faceSize(), plain->faceSize());
			TS_ASSERT_EQUALS(fitted->lineTop(), 0);
			TS_ASSERT_EQUALS(fitted->baseline(), plain->baseline());
			for (uint i = 0; i < ARRAYSIZE(cps); i++) {
				TS_ASSERT_EQUALS(fitted->cells(cps[i]), plain->cells(cps[i]));
				TS_ASSERT_EQUALS(fitted->advance(cps[i]), plain->advance(cps[i]));
				for (int y = 0; y < plain->cellHeight(); y++) {
					const byte *a = plain->row(cps[i], y), *b = fitted->row(cps[i], y);
					TS_ASSERT(a && b && memcmp(a, b, plain->cellWidth() * 2) == 0);
				}
			}
			delete plain;
			delete fitted;
		}
#endif
	}

	void test_no_freetype_stub_with_probes() {
#ifndef USE_FREETYPE2
		byte dummy[4] = { 0, 0, 0, 0 };
		Common::MemoryReadStream stream(dummy, sizeof(dummy));
		Common::String error;
		const uint32 cp = 0x0E39;
		TS_ASSERT(TtfGlyphSource::create(&stream, DisposeAfterUse::NO, 16, error, false, true, &cp, 1) == nullptr);
		TS_ASSERT(!error.empty());
#endif
	}
};
