#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/memstream.h"
#include "graphics/hires_text/bitmap_font.h"
#include "graphics/hires_text/glyph_source_svfn.h"

#include "../system/null_osystem.h"

#ifdef USE_FREETYPE2
#include "common/fs.h"
#include "graphics/font.h"
#include "graphics/fonts/ttf.h"
#include "graphics/hires_text/font_baker.h"
#endif

/**
 * Tests for SvfnGlyphSource, the adapter that lets an SVFN bitmap font serve
 * as a UnicodeGlyphSource next to TtfGlyphSource and ScvmuniGlyphSource.
 *
 * The fixture is a version 2 SVFN file of three glyphs - U+0041, U+AC00 and
 * U+D7A3 - written out byte by byte, so both the FreeType and the
 * no-FreeType build run it.
 */
class HiResTextSvfnSourceTestSuite : public CxxTest::TestSuite {
private:
	static void put16(Common::Array<byte> &b, uint pos, uint16 v) {
		b[pos] = v & 0xff;
		b[pos + 1] = (v >> 8) & 0xff;
	}

	static void put32(Common::Array<byte> &b, uint pos, uint32 v) {
		b[pos] = v & 0xff;
		b[pos + 1] = (v >> 8) & 0xff;
		b[pos + 2] = (v >> 16) & 0xff;
		b[pos + 3] = (v >> 24) & 0xff;
	}

	static const int kGlyphs = 3;

	/// Code point of glyph index i. The table is deliberately not in glyph
	/// order, so the test sees the adapter go through the font's lookup.
	static uint32 codepointOf(int index) {
		static const uint32 cps[kGlyphs] = { 0xAC00, 0xD7A3, 0x0041 };
		return cps[index];
	}

	/**
	 * A version 2 SVFN file: 36-byte header, metrics table (when
	 * proportional), glyph data, and the code point table last, so cutting
	 * even the final byte leaves a table running past the end.
	 */
	static Common::Array<byte> makeFont(int bpp, int cellW, int cellH, bool proportional) {
		const int rowPitch = (bpp == 1) ? (cellW + 7) / 8 : cellW;
		const uint32 glyphStride = rowPitch * cellH;
		const uint32 metricsOff = 36;
		const uint32 metricsSize = proportional ? kGlyphs * 4 : 0;
		const uint32 dataOff = metricsOff + metricsSize;
		const uint32 dataSize = glyphStride * kGlyphs;
		const uint32 cmapOff = dataOff + dataSize;

		Common::Array<byte> b;
		b.resize(cmapOff + kGlyphs * 8);
		for (uint i = 0; i < b.size(); ++i)
			b[i] = 0;

		b[0] = 'S'; b[1] = 'V'; b[2] = 'F'; b[3] = 'N';
		put16(b, 4, 2);
		put16(b, 6, proportional ? 1 : 0);
		b[8] = bpp;
		put16(b, 10, 0);
		put16(b, 12, kGlyphs);
		b[14] = cellW;
		b[15] = cellH;
		b[16] = cellH - 1;
		put32(b, 20, proportional ? metricsOff : 0);
		put32(b, 24, dataOff);
		put32(b, 28, dataSize);
		put32(b, 32, cmapOff);

		if (proportional) {
			for (int i = 0; i < kGlyphs; ++i) {
				b[metricsOff + i * 4 + 0] = 5 + i * 2;  // advance
				b[metricsOff + i * 4 + 1] = i;          // left bearing
				b[metricsOff + i * 4 + 2] = 3 + i;      // ink width
			}
		}

		// A distinct value for every pixel of every glyph. At 1bpp the bits
		// past the cell width stay clear, as the baker writes them.
		for (int i = 0; i < kGlyphs; ++i) {
			for (int y = 0; y < cellH; ++y) {
				byte *row = &b[dataOff + i * glyphStride + y * rowPitch];
				for (int x = 0; x < cellW; ++x) {
					const byte v = (byte)(i * 37 + y * 11 + x * 3 + 1);
					if (bpp == 8)
						row[x] = v;
					else if (v & 4)
						row[x >> 3] |= 0x80 >> (x & 7);
				}
			}
		}

		for (int i = 0; i < kGlyphs; ++i) {
			put32(b, cmapOff + i * 8, codepointOf(i));
			put32(b, cmapOff + i * 8 + 4, i);
		}
		return b;
	}

	static bool loadFont(Graphics::HiResBitmapFont &font, const Common::Array<byte> &bytes, uint32 size) {
		Common::MemoryReadStream stream(bytes.begin(), size);
		return font.load(stream);
	}

	/// Every row of every glyph read through the adapter against the font.
	static void checkRows(Graphics::HiResBitmapFont &font, Graphics::SvfnGlyphSource &src) {
		const int bpp = font.bpp();
		const int pitch = font.glyphPitch();
		const int stride = (src.cellWidth() * 2 * bpp + 7) / 8;
		for (int i = 0; i < font.glyphCount(); ++i) {
			const uint32 cp = codepointOf(i);
			const int index = font.glyphIndex(cp);
			TS_ASSERT_EQUALS(index, i);
			const byte *glyph = font.glyphData(index);
			TS_ASSERT(glyph != nullptr);
			TS_ASSERT(src.cells(cp) > 0);
			for (int y = 0; y < font.cellHeight(); ++y) {
				const byte *row = src.row(cp, y);
				TS_ASSERT(row != nullptr);
				if (!row || !glyph)
					return;
				for (int x = 0; x < pitch; ++x)
					TS_ASSERT_EQUALS(row[x], glyph[y * pitch + x]);
				// The second cell of the stride is blank.
				for (int x = pitch; x < stride; ++x)
					TS_ASSERT_EQUALS(row[x], 0);
			}
		}
	}

	static void checkMatches(int bpp) {
		const Common::Array<byte> bytes = makeFont(bpp, 6, 5, true);
		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes, bytes.size()));
		if (!font.isLoaded())
			return;

		Graphics::SvfnGlyphSource src(&font, DisposeAfterUse::NO);
		TS_ASSERT_EQUALS(src.cellWidth(), 6);
		TS_ASSERT_EQUALS(src.cellHeight(), 5);
		TS_ASSERT_EQUALS(src.advanceNarrow(), 3);
		TS_ASSERT_EQUALS(src.advanceWide(), 6);
		TS_ASSERT_EQUALS(src.bitsPerPixel(), bpp);
		TS_ASSERT_EQUALS(src.glyphCount(), 3u);

		TS_ASSERT_EQUALS(src.cells(0x0041), 1);
		TS_ASSERT_EQUALS(src.cells(0xAC00), 2);
		TS_ASSERT_EQUALS(src.cells(0xD7A3), 2);
		TS_ASSERT_EQUALS(src.cells(0x0042), 0);
		TS_ASSERT_EQUALS(src.advance(0x0042), 0);

		checkRows(font, src);

		// advance() and bearingX() are the metrics table's.
		for (int i = 0; i < kGlyphs; ++i) {
			Graphics::GlyphMetrics m;
			TS_ASSERT(font.glyphMetrics(i, m));
			TS_ASSERT_EQUALS(src.advance(codepointOf(i)), (int)m.advance);
			TS_ASSERT_EQUALS(src.advance(codepointOf(i)), 5 + i * 2);
			TS_ASSERT_EQUALS(src.bearingX(codepointOf(i)), i);
		}
	}

public:
	void test_svfn_source_matches_bitmap_font() {
		checkMatches(8);
		checkMatches(1);
	}

	// Without a metrics table the font draws on a fixed grid and has no
	// per-glyph advance to give.
	void test_svfn_source_without_metrics_has_no_advance() {
		const Common::Array<byte> bytes = makeFont(8, 6, 5, false);
		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes, bytes.size()));
		Graphics::SvfnGlyphSource src(&font, DisposeAfterUse::NO);
		TS_ASSERT_EQUALS(src.cells(0x0041), 1);
		TS_ASSERT_EQUALS(src.advance(0x0041), 0);
		TS_ASSERT_EQUALS(src.advance(0xAC00), 0);
		TS_ASSERT_EQUALS(src.bearingX(0xAC00), 0);
		checkRows(font, src);
	}

	// The adapter takes the font when asked to.
	void test_svfn_source_owns_the_font() {
		const Common::Array<byte> bytes = makeFont(8, 6, 5, true);
		Graphics::HiResBitmapFont *font = new Graphics::HiResBitmapFont();
		TS_ASSERT(loadFont(*font, bytes, bytes.size()));
		Graphics::SvfnGlyphSource *src = new Graphics::SvfnGlyphSource(font, DisposeAfterUse::YES);
		TS_ASSERT_EQUALS(src->cells(0xAC00), 2);
		delete src;	// frees the font; leak checkers see it
	}

	// A cut file never loads, so no adapter is ever built on it.
	void test_svfn_source_truncated_file() {
		for (int bpp = 1; bpp <= 8; bpp += 7) {
			const Common::Array<byte> bytes = makeFont(bpp, 6, 5, true);
			const uint32 cuts[3] = { 20, 40, bytes.size() - 1 };
			for (int c = 0; c < 3; ++c) {
				Graphics::HiResBitmapFont font;
				TS_ASSERT(!loadFont(font, bytes, cuts[c]));
				TS_ASSERT(!font.isLoaded());
			}
		}
	}

	// The run-time baker's output reads back the same way.
	void test_svfn_source_matches_baked_font() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		Common::install_null_g_system();
		bakeAndCheck();
		Common::uninstall_null_g_system();
#endif
	}

private:
	static void bakeAndCheck() {
#ifdef USE_FREETYPE2
		Common::FSNode node("/System/Library/Fonts/AppleSDGothicNeo.ttc");
		if (!node.exists())
			return;	// no such face on this machine: nothing to bake
		Common::SeekableReadStream *stream = node.createReadStream();
		if (!stream)
			return;
		Graphics::Font *face = Graphics::loadTTFFont(stream, DisposeAfterUse::YES, 16,
		                                             Graphics::kTTFSizeModeCharacter);
		TS_ASSERT(face != nullptr);
		if (!face)
			return;

		Common::Array<uint32> cps;
		cps.push_back(0xAC00);
		cps.push_back(0xD7A3);
		cps.push_back(0x0041);
		Common::Array<byte> baked;
		TS_ASSERT(Graphics::HiResFontBaker::bake(*face, cps, 0, 0, true, baked));
		delete face;

		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, baked, baked.size()));
		if (!font.isLoaded())
			return;
		TS_ASSERT_EQUALS(font.bpp(), 8);

		Graphics::SvfnGlyphSource src(&font, DisposeAfterUse::NO);
		TS_ASSERT_EQUALS(src.cells(0x0041), 1);
		TS_ASSERT_EQUALS(src.cells(0xAC00), 2);
		TS_ASSERT_EQUALS(src.cells(0x0042), 0);
		checkRows(font, src);
		for (int i = 0; i < kGlyphs; ++i) {
			Graphics::GlyphMetrics m;
			TS_ASSERT(font.glyphMetrics(font.glyphIndex(codepointOf(i)), m));
			TS_ASSERT_EQUALS(src.advance(codepointOf(i)), (int)m.advance);
		}
#endif
	}
};
