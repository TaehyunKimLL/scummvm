#include <cxxtest/TestSuite.h>

#include "common/memstream.h"
#include "graphics/hires_text/bitmap_font.h"
#include "graphics/hires_text/glyph_renderer.h"
#include "graphics/surface.h"

/**
 * Tests for the hi-res text glyph renderer.
 *
 * The fonts are built here with known pixel patterns, so every assertion is
 * about what actually landed on the surface rather than about the call having
 * returned true.
 */
class HiResGlyphRendererTestSuite : public CxxTest::TestSuite {
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

	/// A font of @p glyphs cells, with the pixels left blank for the caller.
	static Common::Array<byte> makeFont(int bpp, int glyphs, int cellW, int cellH) {
		const int rowPitch = (bpp == 1) ? (cellW + 7) / 8 : cellW;
		const uint32 dataOff = 32;
		const uint32 dataSize = rowPitch * cellH * glyphs;

		Common::Array<byte> b;
		b.resize(dataOff + dataSize);
		for (uint i = 0; i < b.size(); ++i)
			b[i] = 0;

		b[0] = 'S'; b[1] = 'V'; b[2] = 'F'; b[3] = 'N';
		put16(b, 4, 1);
		b[8] = bpp;
		put16(b, 12, glyphs);
		b[14] = cellW;
		b[15] = cellH;
		b[16] = cellH - 2;
		put32(b, 24, dataOff);
		put32(b, 28, dataSize);
		return b;
	}

	static void setPixel8(Common::Array<byte> &b, int glyph, int cellW, int cellH,
						  int x, int y, byte value) {
		b[32 + glyph * cellW * cellH + y * cellW + x] = value;
	}

	static void setPixel1(Common::Array<byte> &b, int glyph, int cellW, int cellH,
						  int x, int y) {
		const int pitch = (cellW + 7) / 8;
		b[32 + glyph * pitch * cellH + y * pitch + (x >> 3)] |= 0x80 >> (x & 7);
	}

	static bool loadFont(Graphics::HiResBitmapFont &font, Common::Array<byte> &bytes) {
		Common::MemoryReadStream stream(bytes.begin(), bytes.size());
		return font.load(stream);
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
	void test_draws_an_8bpp_glyph_into_both_surfaces() {
		Common::Array<byte> bytes = makeFont(8, 2, 4, 4);
		setPixel8(bytes, 0, 4, 4, 1, 1, 0xFF);
		setPixel8(bytes, 0, 4, 4, 2, 1, 0x80);

		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		Graphics::Surface dest, cov;
		dest.create(16, 16, Graphics::PixelFormat::createFormatCLUT8());
		cov.create(16, 16, Graphics::PixelFormat::createFormatCLUT8());

		Graphics::GlyphStyle style;
		style.color = 7;

		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(dest, &cov, font, 0, 5, 5, style));

		// The colour goes to the text surface...
		TS_ASSERT_EQUALS(at(dest, 6, 6), 7);
		TS_ASSERT_EQUALS(at(dest, 7, 6), 7);

		// ...and the coverage, which a paletted surface cannot hold, goes to
		// the parallel one. That is what makes anti-aliased text possible.
		TS_ASSERT_EQUALS(at(cov, 6, 6), 0xFF);
		TS_ASSERT_EQUALS(at(cov, 7, 6), 0x80);

		// Pixels with no coverage are left alone.
		TS_ASSERT_EQUALS(at(dest, 5, 5), 0);
		TS_ASSERT_EQUALS(at(cov, 5, 5), 0);

		dest.free();
		cov.free();
	}

	void test_draws_without_a_coverage_surface() {
		Common::Array<byte> bytes = makeFont(8, 1, 4, 4);
		setPixel8(bytes, 0, 4, 4, 1, 1, 0x40);

		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		Graphics::Surface dest;
		dest.create(16, 16, Graphics::PixelFormat::createFormatCLUT8());

		Graphics::GlyphStyle style;
		style.color = 3;

		// Without somewhere to record coverage the glyph still draws, as a
		// stencil: a build or backend with no alpha path is not left blank.
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(dest, nullptr, font, 0, 0, 0, style));
		TS_ASSERT_EQUALS(at(dest, 1, 1), 3);
		TS_ASSERT_EQUALS(inkCount(dest), 1);

		dest.free();
	}

	void test_draws_a_1bpp_glyph() {
		Common::Array<byte> bytes = makeFont(1, 1, 16, 4);
		setPixel1(bytes, 0, 16, 4, 0, 0);
		setPixel1(bytes, 0, 16, 4, 7, 0);
		setPixel1(bytes, 0, 16, 4, 8, 1);
		setPixel1(bytes, 0, 16, 4, 15, 1);

		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		Graphics::Surface dest;
		dest.create(20, 8, Graphics::PixelFormat::createFormatCLUT8());

		Graphics::GlyphStyle style;
		style.color = 5;

		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(dest, nullptr, font, 0, 0, 0, style));

		// Rows are packed most significant bit first, so bit 7 of the first
		// byte is pixel 0 and bit 0 of the second byte is pixel 15.
		TS_ASSERT_EQUALS(at(dest, 0, 0), 5);
		TS_ASSERT_EQUALS(at(dest, 7, 0), 5);
		TS_ASSERT_EQUALS(at(dest, 8, 1), 5);
		TS_ASSERT_EQUALS(at(dest, 15, 1), 5);
		TS_ASSERT_EQUALS(inkCount(dest), 4);

		dest.free();
	}

	void test_1bpp_coverage_is_ignored() {
		Common::Array<byte> bytes = makeFont(1, 1, 8, 2);
		setPixel1(bytes, 0, 8, 2, 0, 0);

		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		Graphics::Surface dest, cov;
		dest.create(8, 4, Graphics::PixelFormat::createFormatCLUT8());
		cov.create(8, 4, Graphics::PixelFormat::createFormatCLUT8());

		Graphics::GlyphStyle style;
		style.color = 2;
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(dest, &cov, font, 0, 0, 0, style));

		// A 1bpp font has no coverage to record - every pixel is on or off -
		// so blending it would be meaningless and the surface stays clean.
		TS_ASSERT_EQUALS(at(dest, 0, 0), 2);
		TS_ASSERT_EQUALS(inkCount(cov), 0);

		dest.free();
		cov.free();
	}

	void test_unknown_glyph_draws_nothing() {
		Common::Array<byte> bytes = makeFont(8, 2, 4, 4);
		setPixel8(bytes, 0, 4, 4, 0, 0, 0xFF);

		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		Graphics::Surface dest;
		dest.create(8, 8, Graphics::PixelFormat::createFormatCLUT8());

		Graphics::GlyphStyle style;
		style.color = 9;

		TS_ASSERT(!Graphics::HiResGlyphRenderer::drawGlyph(dest, nullptr, font, 5, 0, 0, style));
		TS_ASSERT(!Graphics::HiResGlyphRenderer::drawGlyph(dest, nullptr, font, -1, 0, 0, style));
		TS_ASSERT_EQUALS(inkCount(dest), 0);

		dest.free();
	}

	void test_clips_against_every_edge() {
		Common::Array<byte> bytes = makeFont(8, 1, 4, 4);
		for (int y = 0; y < 4; ++y)
			for (int x = 0; x < 4; ++x)
				setPixel8(bytes, 0, 4, 4, x, y, 0xFF);

		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		Graphics::Surface dest;
		dest.create(8, 8, Graphics::PixelFormat::createFormatCLUT8());

		Graphics::GlyphStyle style;
		style.color = 4;

		// Off the top left: only the bottom right quarter lands.
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(dest, nullptr, font, 0, -2, -2, style));
		TS_ASSERT_EQUALS(inkCount(dest), 4);
		TS_ASSERT_EQUALS(at(dest, 0, 0), 4);

		// Off the bottom right: the other quarter.
		dest.fillRect(Common::Rect(8, 8), 0);
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(dest, nullptr, font, 0, 6, 6, style));
		TS_ASSERT_EQUALS(inkCount(dest), 4);
		TS_ASSERT_EQUALS(at(dest, 7, 7), 4);

		// Entirely outside: nothing at all, and no crash.
		dest.fillRect(Common::Rect(8, 8), 0);
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(dest, nullptr, font, 0, 100, 100, style));
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(dest, nullptr, font, 0, -100, -100, style));
		TS_ASSERT_EQUALS(inkCount(dest), 0);

		dest.free();
	}

	void test_drop_shadow_sits_below_and_right() {
		Common::Array<byte> bytes = makeFont(8, 1, 2, 2);
		setPixel8(bytes, 0, 2, 2, 0, 0, 0xFF);

		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		Graphics::Surface dest;
		dest.create(8, 8, Graphics::PixelFormat::createFormatCLUT8());

		Graphics::GlyphStyle style;
		style.color = 7;
		style.shadowColor = 1;
		style.shadowMode = Graphics::kHiResShadowDrop;
		style.shadowOffset = 1;

		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(dest, nullptr, font, 0, 2, 2, style));

		TS_ASSERT_EQUALS(at(dest, 2, 2), 7);   // the glyph
		TS_ASSERT_EQUALS(at(dest, 3, 3), 1);   // its shadow
		TS_ASSERT_EQUALS(inkCount(dest), 2);

		dest.free();
	}

	void test_outline_surrounds_the_glyph() {
		Common::Array<byte> bytes = makeFont(8, 1, 1, 1);
		setPixel8(bytes, 0, 1, 1, 0, 0, 0xFF);

		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		Graphics::Surface dest;
		dest.create(8, 8, Graphics::PixelFormat::createFormatCLUT8());

		Graphics::GlyphStyle style;
		style.color = 7;
		style.shadowColor = 1;
		style.shadowMode = Graphics::kHiResShadowOutline;
		style.shadowOffset = 1;

		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(dest, nullptr, font, 0, 4, 4, style));

		// One pixel of body with eight of outline around it.
		TS_ASSERT_EQUALS(at(dest, 4, 4), 7);
		TS_ASSERT_EQUALS(at(dest, 3, 3), 1);
		TS_ASSERT_EQUALS(at(dest, 4, 3), 1);
		TS_ASSERT_EQUALS(at(dest, 5, 5), 1);
		TS_ASSERT_EQUALS(inkCount(dest), 9);

		dest.free();
	}

	void test_shadow_offset_scales_the_decoration() {
		Common::Array<byte> bytes = makeFont(8, 1, 1, 1);
		setPixel8(bytes, 0, 1, 1, 0, 0, 0xFF);

		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		Graphics::Surface dest;
		dest.create(16, 16, Graphics::PixelFormat::createFormatCLUT8());

		Graphics::GlyphStyle style;
		style.color = 7;
		style.shadowColor = 1;
		style.shadowMode = Graphics::kHiResShadowDrop;
		style.shadowOffset = 3;

		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(dest, nullptr, font, 0, 5, 5, style));

		// A font baked for a larger surface needs its shadow moved out to
		// match, or the decoration closes up against the strokes.
		TS_ASSERT_EQUALS(at(dest, 5, 5), 7);
		TS_ASSERT_EQUALS(at(dest, 8, 8), 1);

		dest.free();
	}

	void test_a_shadow_in_the_text_colour_is_skipped() {
		Common::Array<byte> bytes = makeFont(8, 1, 1, 1);
		setPixel8(bytes, 0, 1, 1, 0, 0, 0xFF);

		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		Graphics::Surface dest;
		dest.create(8, 8, Graphics::PixelFormat::createFormatCLUT8());

		Graphics::GlyphStyle style;
		style.color = 7;
		style.shadowColor = 7;
		style.shadowMode = Graphics::kHiResShadowOutline;
		style.shadowOffset = 1;

		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(dest, nullptr, font, 0, 4, 4, style));

		// An outline the same colour as the text cannot be seen; drawing it
		// would only fatten the glyph.
		TS_ASSERT_EQUALS(inkCount(dest), 1);
		TS_ASSERT_EQUALS(at(dest, 4, 4), 7);

		dest.free();
	}

	void test_outline_does_not_erode_a_neighbour() {
		// Two glyphs drawn close together: the second one's outline crosses
		// where the first one's body already is.
		Common::Array<byte> bytes = makeFont(8, 2, 2, 2);
		setPixel8(bytes, 0, 2, 2, 0, 0, 0xFF);
		setPixel8(bytes, 1, 2, 2, 0, 0, 0xFF);

		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		Graphics::Surface dest, cov;
		dest.create(16, 16, Graphics::PixelFormat::createFormatCLUT8());
		cov.create(16, 16, Graphics::PixelFormat::createFormatCLUT8());

		Graphics::GlyphStyle style;
		style.color = 7;
		style.shadowColor = 1;
		style.shadowMode = Graphics::kHiResShadowOutline;
		style.shadowOffset = 1;

		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(dest, &cov, font, 0, 5, 5, style));
		TS_ASSERT_EQUALS(at(dest, 5, 5), 7);

		// The next glyph's outline reaches (5,5). Full coverage is already
		// recorded there, so the body colour has to survive.
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(dest, &cov, font, 1, 6, 5, style));
		TS_ASSERT_EQUALS(at(dest, 5, 5), 7);
		TS_ASSERT_EQUALS(at(cov, 5, 5), 0xFF);

		dest.free();
		cov.free();
	}

	void test_a_stronger_pixel_is_not_replaced_by_a_fainter_one() {
		Common::Array<byte> bytes = makeFont(8, 2, 2, 2);
		setPixel8(bytes, 0, 2, 2, 0, 0, 0x30);   // faint
		setPixel8(bytes, 1, 2, 2, 0, 0, 0xF0);   // strong

		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		Graphics::Surface dest, cov;
		dest.create(8, 8, Graphics::PixelFormat::createFormatCLUT8());
		cov.create(8, 8, Graphics::PixelFormat::createFormatCLUT8());

		Graphics::GlyphStyle body;
		body.color = 7;
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(dest, &cov, font, 1, 2, 2, body));
		TS_ASSERT_EQUALS(at(cov, 2, 2), 0xF0);

		// A faint decoration pixel landing on a strong body pixel must leave
		// it alone, or the glyph is eaten from the edges inwards.
		Graphics::GlyphStyle outlined;
		outlined.color = 7;
		outlined.shadowColor = 1;
		outlined.shadowMode = Graphics::kHiResShadowOutline;
		outlined.shadowOffset = 1;
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(dest, &cov, font, 0, 3, 3, outlined));

		TS_ASSERT_EQUALS(at(cov, 2, 2), 0xF0);
		TS_ASSERT_EQUALS(at(dest, 2, 2), 7);

		dest.free();
		cov.free();
	}

	void test_reports_the_area_it_touched() {
		Common::Array<byte> bytes = makeFont(8, 1, 4, 4);
		setPixel8(bytes, 0, 4, 4, 0, 0, 0xFF);

		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		Graphics::Surface dest;
		dest.create(32, 32, Graphics::PixelFormat::createFormatCLUT8());

		Graphics::GlyphStyle style;
		style.color = 7;

		// A caller has to know what to clear later: the engine's own charset
		// mask does not track text drawn onto this surface.
		Common::Rect dirty;
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(dest, nullptr, font, 0, 10, 10, style, &dirty));
		TS_ASSERT_EQUALS(dirty.left, 10);
		TS_ASSERT_EQUALS(dirty.top, 10);
		TS_ASSERT_EQUALS(dirty.right, 14);
		TS_ASSERT_EQUALS(dirty.bottom, 14);

		// A second glyph extends the same rectangle rather than replacing it.
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(dest, nullptr, font, 0, 20, 10, style, &dirty));
		TS_ASSERT_EQUALS(dirty.left, 10);
		TS_ASSERT_EQUALS(dirty.right, 24);

		dest.free();
	}

	void test_dirty_area_covers_the_decoration() {
		Common::Array<byte> bytes = makeFont(8, 1, 4, 4);
		setPixel8(bytes, 0, 4, 4, 0, 0, 0xFF);

		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		Graphics::Surface dest;
		dest.create(32, 32, Graphics::PixelFormat::createFormatCLUT8());

		Graphics::GlyphStyle style;
		style.color = 7;
		style.shadowColor = 1;
		style.shadowMode = Graphics::kHiResShadowOutline;
		style.shadowOffset = 2;

		Common::Rect dirty;
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(dest, nullptr, font, 0, 10, 10, style, &dirty));

		// An outline reaches outside the cell, and the area reported has to
		// include it or leftovers survive a clear.
		TS_ASSERT(dirty.left < 10);
		TS_ASSERT(dirty.top < 10);
		TS_ASSERT(dirty.right > 14);
		TS_ASSERT(dirty.bottom > 14);

		dest.free();
	}
};
