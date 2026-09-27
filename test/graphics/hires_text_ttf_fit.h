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

#include "common/algorithm.h"
#include "common/array.h"
#include "common/str.h"
#include "common/memstream.h"
#include "common/stream.h"
#include "graphics/hires_text/coverage.h"
#include "graphics/hires_text/glyph_source_ttf.h"

#include "../system/null_osystem.h"

#ifdef USE_FREETYPE2
#include "common/fs.h"
#include "graphics/font.h"
#include "graphics/fonts/ttf.h"
#include "graphics/managed_surface.h"
#endif

using Graphics::TtfGlyphSource;

// The faces measured, each overridable by an environment variable (a path
// to a .ttf/.ttc; face 0 is used). The defaults are macOS system fonts; a
// test whose face is absent is skipped, visibly.
//   SCUMMVM_TEST_THAI_FONT   Sukhumvit Set (a Thai face with zero-width marks)
//   SCUMMVM_TEST_KO_FONT     Apple SD Gothic Neo
//   SCUMMVM_TEST_JA_FONT     Hiragino Sans W3
#pragma push_macro("getenv")
#undef getenv
static const char *ttfFitTestFontPath(const char *var, const char *def) {
	const char *v = getenv(var);
	return (v && *v) ? v : def;
}
#pragma pop_macro("getenv")

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
	// "Text" face the C11 maps name.
	static Common::FSNode fontNode(const char *var, const char *def) {
		Common::FSNode node(ttfFitTestFontPath(var, def));
		if (!node.exists())
			TS_SKIP(Common::String::format("%s: no font at '%s'", var, ttfFitTestFontPath(var, def)).c_str());
		return node;
	}
	static Common::FSNode sukhumvitNode() {
		return fontNode("SCUMMVM_TEST_THAI_FONT", "/System/Library/Fonts/Supplemental/SukhumvitSet.ttc");
	}
	static Common::FSNode sdGothicNode() {
		return fontNode("SCUMMVM_TEST_KO_FONT", "/System/Library/Fonts/AppleSDGothicNeo.ttc");
	}
	static Common::FSNode hiraginoNode() {
		return fontNode("SCUMMVM_TEST_JA_FONT", "/System/Library/Fonts/\xe3\x83\x92\xe3\x83\xa9\xe3\x82\xae\xe3\x83\x8e\xe8\xa7\x92\xe3\x82\xb4\xe3\x82\xb7\xe3\x83\x83\xe3\x82\xaf W3.ttc");
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
			return;
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

	// fix1: the fits the translation does not reach stay the base's own.
	// Each case is (face, cell, sample?) -> the size, line top and raster
	// count the base (6ef770b7d5) picked, measured with it: the legacy fit
	// with no sample, and a Korean / Japanese sample on the default
	// (non-line) fit, all of which shrink the face.
	struct PinnedFit {
		int face;	///< 0 SD Gothic, 1 Hiragino
		int sample;	///< 0 none, 1 Korean, 2 Japanese
		int cell, faceSize, lineTop;
		uint32 rasterCount;
	};

	void checkPinned(const PinnedFit *cases, uint count) {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		const uint32 ko[] = { 0xAC00, 0xB098, 0xB2E4, 0xD7A3, 0xBDC1, 0xB620, 0x3131, 0x300C, 0x2026,
		                      '?', '!', 'g', 'y', '(', 'j', '|' };
		const uint32 ja[] = { 0x3042, 0x3044, 0x3089, 0x30FC, 0x4E00, 0x6F22, 0x9F8D, 0x300C, 0x300D,
		                      0x3002, 0xFF08, 0xFF09, 'g', 'j', '|' };
		for (uint i = 0; i < count; i++) {
			const PinnedFit &c = cases[i];
			Common::FSNode node = c.face == 0 ? sdGothicNode() : hiraginoNode();
			if (!node.exists())
				return;
			Common::String error;
			TtfGlyphSource *src;
			if (c.sample == 0)
				src = TtfGlyphSource::create(node.createReadStream(), DisposeAfterUse::YES, c.cell, error);
			else if (c.sample == 1)
				src = TtfGlyphSource::create(node.createReadStream(), DisposeAfterUse::YES, c.cell, error, ko, ARRAYSIZE(ko));
			else
				src = TtfGlyphSource::create(node.createReadStream(), DisposeAfterUse::YES, c.cell, error, ja, ARRAYSIZE(ja));
			TS_ASSERT(src != nullptr);
			if (!src)
				continue;
			TS_ASSERT_EQUALS(src->faceSize(), c.faceSize);
			TS_ASSERT_EQUALS(src->lineTop(), c.lineTop);
			TS_ASSERT_EQUALS(src->rasterCount(), c.rasterCount);
			delete src;
		}
#endif
	}

	void test_legacy_fit_without_sample_is_the_base_fit() {
		const PinnedFit cases[] = {
			{ 0, 0, 16, 15, 0, 30 },
			{ 1, 0, 24, 22, 0, 34 },
			{ 1, 0, 32, 30, 0, 34 },
			{ 1, 0, 40, 38, 0, 34 },
		};
		checkPinned(cases, ARRAYSIZE(cases));
	}

	void test_cjk_sample_fit_is_the_base_fit() {
		const PinnedFit cases[] = {
			{ 0, 1, 16, 15, 0, 46 },
			{ 0, 1, 24, 23, 0, 46 },
			{ 0, 1, 32, 31, 0, 46 },
			{ 1, 2, 24, 22, 0, 49 },
			{ 1, 2, 32, 30, 0, 49 },
		};
		checkPinned(cases, ARRAYSIZE(cases));
	}

	// A Thai translation with more than 64 distinct characters: sample()
	// keeps the lowest 64 non-ASCII ones and drops the tone marks
	// (U+0E48..U+0E4B); fitProbes() keeps every mark, so a line-fitted face
	// still fits SARA UU and MAI EK.
	void test_fit_probes_keep_every_mark_of_a_large_translation() {
		Graphics::CodePointSet set;
		for (uint32 cp = 0x0E01; cp <= 0x0E3A; cp++)
			set.add(cp);
		for (uint32 cp = 0x0E3F; cp <= 0x0E5B; cp++)
			set.add(cp);
		for (uint32 cp = 'a'; cp <= 'z'; cp++)
			set.add(cp);
		TS_ASSERT(set.size() > 64u);

		Common::Array<uint32> sample, probes;
		set.sample(64, sample);
		set.fitProbes(TtfGlyphSource::kMaxExtraFitProbes, probes);
		TS_ASSERT(Common::find(sample.begin(), sample.end(), 0x0E48u) == sample.end());
		TS_ASSERT_EQUALS(probes.size(), (uint)TtfGlyphSource::kMaxExtraFitProbes);
		const uint32 marks[] = { 0x0E31, 0x0E33, 0x0E34, 0x0E38, 0x0E39, 0x0E3A, 0x0E47, 0x0E48,
		                         0x0E49, 0x0E4A, 0x0E4B, 0x0E4C, 0x0E4D, 0x0E4E };
		for (uint i = 0; i < ARRAYSIZE(marks); i++)
			TS_ASSERT(Common::find(probes.begin(), probes.end(), marks[i]) != probes.end());
		for (uint i = 0; i < probes.size(); i++) {
			TS_ASSERT(set.contains(probes[i]));
			for (uint j = 0; j < i; j++)
				TS_ASSERT_DIFFERS(probes[i], probes[j]);
		}
		// A small set: all of it, marks first.
		Graphics::CodePointSet small;
		small.add('a');
		small.add(0x0E01);
		small.add(0x0E39);
		small.fitProbes(64, probes);
		TS_ASSERT_EQUALS(probes.size(), 3u);
		if (probes.size() == 3)
			TS_ASSERT_EQUALS(probes[0], 0x0E39u);

#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		Common::FSNode node = sukhumvitNode();
		if (!node.exists())
			return;
		set.fitProbes(TtfGlyphSource::kMaxExtraFitProbes, probes);
		Common::String error;
		TtfGlyphSource *src = TtfGlyphSource::create(node.createReadStream(), DisposeAfterUse::YES, 30, error,
		                                             false, true, probes.data(), probes.size());
		TS_ASSERT(src != nullptr);
		if (!src)
			return;
		const uint32 check[] = { 0x0E1C, 0x0E39, 0x0E48, 0x0E4B };
		for (uint i = 0; i < ARRAYSIZE(check); i++) {
			TS_ASSERT(src->cells(check[i]) > 0);
			TS_ASSERT_EQUALS(lostInkRows(src, node, true, check[i]), 0);
		}
		delete src;
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
