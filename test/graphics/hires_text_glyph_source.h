#include <cxxtest/TestSuite.h>

#include "common/memstream.h"
#include "graphics/hires_text/bitmap_font.h"
#include "graphics/hires_text/glyph_renderer.h"
#include "graphics/hires_text/glyph_source.h"
#include "graphics/surface.h"

/**
 * Tests for the glyph source interface.
 *
 * The point of this interface is that a build without FreeType behaves like one
 * with it, so the cases below are about a caller being able to draw text
 * without knowing which kind of font it was handed.
 */
class HiResGlyphSourceTestSuite : public CxxTest::TestSuite {
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

	/// A single byte font, indexed by character code, with per-glyph advances.
	static Common::Array<byte> makeLatinFont(int cellW, int cellH) {
		const int glyphs = 256;
		const uint32 metricsOff = 32;
		const uint32 dataOff = metricsOff + glyphs * 4;
		const uint32 dataSize = cellW * cellH * glyphs;

		Common::Array<byte> b;
		b.resize(dataOff + dataSize);
		for (uint i = 0; i < b.size(); ++i)
			b[i] = 0;

		b[0] = 'S'; b[1] = 'V'; b[2] = 'F'; b[3] = 'N';
		put16(b, 4, 1);
		put16(b, 6, 1);           // proportional
		b[8] = 8;
		put16(b, 10, 0);          // single byte
		put16(b, 12, glyphs);
		b[14] = cellW;
		b[15] = cellH;
		b[16] = cellH - 3;        // ascent
		put32(b, 20, metricsOff);
		put32(b, 24, dataOff);
		put32(b, 28, dataSize);

		// 'i' is narrow, 'W' is wide - the difference a proportional font is
		// for.
		b[metricsOff + 'i' * 4 + 0] = 5;
		b[metricsOff + 'i' * 4 + 1] = 1;
		b[metricsOff + 'i' * 4 + 2] = 3;
		b[metricsOff + 'W' * 4 + 0] = cellW;
		b[metricsOff + 'W' * 4 + 1] = 0;
		b[metricsOff + 'W' * 4 + 2] = cellW;

		// One lit pixel per glyph, so a draw can be told apart from a no-op.
		for (int g = 0; g < glyphs; ++g)
			b[dataOff + g * cellW * cellH] = 0xFF;

		return b;
	}

	static byte at(const Graphics::Surface &s, int x, int y) {
		return *(const byte *)s.getBasePtr(x, y);
	}

	static int inkCount(const Graphics::Surface &s) {
		int n = 0;
		for (int y = 0; y < s.h; ++y)
			for (int x = 0; x < s.w; ++x)
				if (at(s, x, y))
					++n;
		return n;
	}

public:
	void test_bitmap_source_reports_the_fonts_own_answers() {
		Common::Array<byte> bytes = makeLatinFont(12, 16);
		Common::MemoryReadStream stream(bytes.begin(), bytes.size());

		Graphics::HiResBitmapFont font;
		TS_ASSERT(font.load(stream));

		Graphics::HiResBitmapGlyphSource source(font);

		TS_ASSERT(source.hasGlyph('W'));
		TS_ASSERT(!source.hasGlyph(0xAC00));   // not in a single byte font

		TS_ASSERT_EQUALS(source.fontHeight(), 16);
		TS_ASSERT_EQUALS(source.ascent(), 13);

		Graphics::GlyphMetrics narrow, wide;
		TS_ASSERT(source.metrics('i', narrow));
		TS_ASSERT(source.metrics('W', wide));
		TS_ASSERT_EQUALS(narrow.advance, 5);
		TS_ASSERT_EQUALS(wide.advance, 12);
	}

	void test_bitmap_source_hands_out_the_stored_pixels() {
		Common::Array<byte> bytes = makeLatinFont(8, 8);
		Common::MemoryReadStream stream(bytes.begin(), bytes.size());

		Graphics::HiResBitmapFont font;
		TS_ASSERT(font.load(stream));

		Graphics::HiResBitmapGlyphSource source(font);

		Graphics::GlyphBitmap glyph;
		TS_ASSERT(source.glyph('A', glyph));

		TS_ASSERT_EQUALS(glyph.width, 8);
		TS_ASSERT_EQUALS(glyph.height, 8);
		TS_ASSERT_EQUALS(glyph.bpp, 8);
		TS_ASSERT_EQUALS(glyph.pitch, 8);

		// A baked font's glyphs sit at the pen with no offset of their own.
		TS_ASSERT_EQUALS(glyph.originX, 0);
		TS_ASSERT_EQUALS(glyph.originY, 0);

		TS_ASSERT(glyph.pixels != nullptr);
		TS_ASSERT_EQUALS(glyph.pixels[0], 0xFF);

		// A code point the font does not have yields nothing at all.
		TS_ASSERT(!source.glyph(0xAC00, glyph));
	}

	void test_renderer_draws_through_a_source() {
		Common::Array<byte> bytes = makeLatinFont(4, 4);
		Common::MemoryReadStream stream(bytes.begin(), bytes.size());

		Graphics::HiResBitmapFont font;
		TS_ASSERT(font.load(stream));

		Graphics::HiResBitmapGlyphSource source(font);

		Graphics::Surface dest;
		dest.create(16, 16, Graphics::PixelFormat::createFormatCLUT8());

		Graphics::GlyphStyle style;
		style.color = 9;

		// The caller names a code point and never learns what kind of font
		// answered it. That is what makes a TrueType face and a baked bitmap
		// interchangeable.
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(dest, nullptr, source, 'A', 2, 2, style));
		TS_ASSERT_EQUALS(at(dest, 2, 2), 9);

		TS_ASSERT(!Graphics::HiResGlyphRenderer::drawGlyph(dest, nullptr, source, 0xAC00, 8, 8, style));
		TS_ASSERT_EQUALS(inkCount(dest), 1);

		dest.free();
	}

	void test_glyph_origin_offsets_the_drawing() {
		// A rasteriser reports where a glyph sits relative to the pen, and
		// that offset has to be honoured or descenders land in the wrong row.
		byte pixels[4] = { 0xFF, 0xFF, 0xFF, 0xFF };

		Graphics::GlyphBitmap glyph;
		glyph.pixels = pixels;
		glyph.pitch = 2;
		glyph.width = 2;
		glyph.height = 2;
		glyph.bpp = 8;
		glyph.originX = -1;
		glyph.originY = 3;

		Graphics::Surface dest;
		dest.create(16, 16, Graphics::PixelFormat::createFormatCLUT8());

		Graphics::GlyphStyle style;
		style.color = 6;

		Common::Rect dirty;
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(dest, nullptr, glyph, 5, 5, style, &dirty));

		// Pen at (5,5) plus an origin of (-1,3) puts the top left at (4,8).
		TS_ASSERT_EQUALS(at(dest, 4, 8), 6);
		TS_ASSERT_EQUALS(at(dest, 5, 9), 6);
		TS_ASSERT_EQUALS(at(dest, 5, 5), 0);
		TS_ASSERT_EQUALS(inkCount(dest), 4);

		// The reported area follows the glyph, not the pen.
		TS_ASSERT_EQUALS(dirty.left, 4);
		TS_ASSERT_EQUALS(dirty.top, 8);

		dest.free();
	}

	void test_rasterised_glyph_keeps_its_coverage() {
		// Whatever produced these bytes, the renderer treats them the same:
		// colour to the text surface, coverage to the parallel one.
		byte pixels[4] = { 0xFF, 0x80, 0x40, 0x00 };

		Graphics::GlyphBitmap glyph;
		glyph.pixels = pixels;
		glyph.pitch = 2;
		glyph.width = 2;
		glyph.height = 2;
		glyph.bpp = 8;

		Graphics::Surface dest, cov;
		dest.create(8, 8, Graphics::PixelFormat::createFormatCLUT8());
		cov.create(8, 8, Graphics::PixelFormat::createFormatCLUT8());

		Graphics::GlyphStyle style;
		style.color = 4;

		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(dest, &cov, glyph, 1, 1, style));

		TS_ASSERT_EQUALS(at(cov, 1, 1), 0xFF);
		TS_ASSERT_EQUALS(at(cov, 2, 1), 0x80);
		TS_ASSERT_EQUALS(at(cov, 1, 2), 0x40);
		TS_ASSERT_EQUALS(at(cov, 2, 2), 0);     // no coverage, not drawn

		TS_ASSERT_EQUALS(at(dest, 1, 1), 4);
		TS_ASSERT_EQUALS(at(dest, 2, 2), 0);

		dest.free();
		cov.free();
	}

	void test_an_empty_glyph_is_rejected() {
		Graphics::Surface dest;
		dest.create(8, 8, Graphics::PixelFormat::createFormatCLUT8());

		Graphics::GlyphStyle style;
		style.color = 1;

		Graphics::GlyphBitmap empty;
		TS_ASSERT(!Graphics::HiResGlyphRenderer::drawGlyph(dest, nullptr, empty, 0, 0, style));

		byte pixel = 0xFF;
		Graphics::GlyphBitmap zeroSized;
		zeroSized.pixels = &pixel;
		zeroSized.pitch = 1;
		zeroSized.width = 0;
		zeroSized.height = 1;
		TS_ASSERT(!Graphics::HiResGlyphRenderer::drawGlyph(dest, nullptr, zeroSized, 0, 0, style));

		TS_ASSERT_EQUALS(inkCount(dest), 0);
		dest.free();
	}
};
