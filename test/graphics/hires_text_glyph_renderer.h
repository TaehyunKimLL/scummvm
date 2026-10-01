#include <cxxtest/TestSuite.h>

#include "common/memstream.h"
#include "graphics/hires_text/bitmap_font.h"
#include "graphics/hires_text/banded_plane.h"
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
		const int rowPitch = (cellW * bpp + 7) / 8;
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

	/// Sets pixel (x, y) of a 2bpp glyph to @p level (0..3).
	static void setPixel2(Common::Array<byte> &b, int glyph, int cellW, int cellH,
						  int x, int y, int level) {
		const int pitch = (cellW * 2 + 7) / 8;
		b[32 + glyph * pitch * cellH + y * pitch + (x >> 2)] |= (byte)(level << (6 - (x & 3) * 2));
	}

	static bool loadFont(Graphics::HiResBitmapFont &font, Common::Array<byte> &bytes) {
		Common::MemoryReadStream stream(bytes.begin(), bytes.size());
		return font.load(stream);
	}

	static byte at(const Graphics::Surface &s, int x, int y) {
		return *(const byte *)s.getBasePtr(x, y);
	}

	static byte at(const Graphics::BandedPlane &p, int x, int y) {
		return p.get(x, y);
	}

	static int inkCount(const Graphics::BandedPlane &p) {
		int n = 0;
		for (int y = 0; y < p.height(); ++y)
			for (int x = 0; x < p.width(); ++x)
				if (p.get(x, y))
					++n;
		return n;
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

		Graphics::Surface dest;
		Graphics::BandedPlane cov;
		dest.create(16, 16, Graphics::PixelFormat::createFormatCLUT8());
		cov.create(16, 16, true);

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

	// A 2bpp glyph is coverage too, at four levels: each is drawn and, where
	// there is a coverage surface, recorded as level * 85.
	void test_draws_a_2bpp_glyph_into_both_surfaces() {
		// 6 px at 2 bits is 12 bits a row: the second byte is half padding.
		Common::Array<byte> bytes = makeFont(2, 2, 6, 3);
		setPixel2(bytes, 0, 6, 3, 0, 1, 1);
		setPixel2(bytes, 0, 6, 3, 1, 1, 2);
		setPixel2(bytes, 0, 6, 3, 2, 1, 3);
		setPixel2(bytes, 0, 6, 3, 5, 2, 3);
		// Glyph 1 is all ink, so reading past glyph 0's rows would show.
		for (int y = 0; y < 3; ++y)
			for (int x = 0; x < 6; ++x)
				setPixel2(bytes, 1, 6, 3, x, y, 3);

		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		Graphics::Surface dest;
		Graphics::BandedPlane cov;
		dest.create(16, 16, Graphics::PixelFormat::createFormatCLUT8());
		cov.create(16, 16, true);

		Graphics::GlyphStyle style;
		style.color = 9;
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(dest, &cov, font, 0, 4, 4, style));

		TS_ASSERT_EQUALS(at(cov, 4, 5), 85);
		TS_ASSERT_EQUALS(at(cov, 5, 5), 170);
		TS_ASSERT_EQUALS(at(cov, 6, 5), 255);
		TS_ASSERT_EQUALS(at(cov, 9, 6), 255);
		TS_ASSERT_EQUALS(at(dest, 4, 5), 9);
		TS_ASSERT_EQUALS(at(dest, 5, 5), 9);
		TS_ASSERT_EQUALS(at(dest, 6, 5), 9);
		TS_ASSERT_EQUALS(at(dest, 9, 6), 9);
		TS_ASSERT_EQUALS(inkCount(dest), 4);
		TS_ASSERT_EQUALS(inkCount(cov), 4);

		dest.free();
		cov.free();
	}

	// Keyed (no coverage surface), a 2bpp glyph keeps the ink past
	// kKeyedInkThreshold, as an 8bpp one does.
	void test_draws_a_2bpp_glyph_keyed() {
		Common::Array<byte> bytes = makeFont(2, 1, 4, 1);
		setPixel2(bytes, 0, 4, 1, 1, 0, 1);
		setPixel2(bytes, 0, 4, 1, 2, 0, 2);
		setPixel2(bytes, 0, 4, 1, 3, 0, 3);

		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		Graphics::Surface dest;
		dest.create(8, 4, Graphics::PixelFormat::createFormatCLUT8());
		Graphics::GlyphStyle style;
		style.color = 4;
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(dest, nullptr, font, 0, 0, 0, style));
		for (int x = 0; x < 4; ++x) {
			const byte cv = (byte)(x * 85);
			TS_ASSERT_EQUALS(at(dest, x, 0), (cv >= Graphics::HiResGlyphRenderer::kKeyedInkThreshold) ? 4 : 0);
		}

		dest.free();
	}

	void test_1bpp_coverage_is_ignored() {
		Common::Array<byte> bytes = makeFont(1, 1, 8, 2);
		setPixel1(bytes, 0, 8, 2, 0, 0);

		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		Graphics::Surface dest;
		Graphics::BandedPlane cov;
		dest.create(8, 4, Graphics::PixelFormat::createFormatCLUT8());
		cov.create(8, 4, true);

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

		Graphics::Surface dest;
		Graphics::BandedPlane cov;
		dest.create(16, 16, Graphics::PixelFormat::createFormatCLUT8());
		cov.create(16, 16, true);

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

		Graphics::Surface dest;
		Graphics::BandedPlane cov;
		dest.create(8, 8, Graphics::PixelFormat::createFormatCLUT8());
		cov.create(8, 8, true);

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

	// --- the decoration matrix -------------------------------------------
	//
	// Every mode, at every offset the map allows, at both pixel depths.
	// Rather than asserting an exact pixel layout per combination - which
	// would be a transcription of the implementation - each case checks the
	// properties a decoration must have whatever its shape.

	/// The modes a map can ask for, with whether they put ink outside the cell.
	struct DecorCase {
		Graphics::HiResShadowMode mode;
		const char *name;
		bool spreads;
	};

	/**
	 * A decoration must never reduce what is drawn.
	 *
	 * Whatever the mode and offset, adding a decoration can only add ink:
	 * the body still lands, and the stroke goes around it. A mode that came
	 * out with less ink than the plain glyph would be eating the letterform,
	 * which is what the pre-dilation implementation did when two glyphs
	 * overlapped.
	 */
	void test_every_mode_and_offset_only_adds_ink() {
		static const DecorCase cases[] = {
			{ Graphics::kHiResShadowNone,    "none",    false },
			{ Graphics::kHiResShadowDrop,    "drop",    true  },
			{ Graphics::kHiResShadowOutline, "outline", true  },
			{ Graphics::kHiResShadowStroke,  "stroke",  true  },
		};

		for (int bpp = 1; bpp <= 8; bpp += 7) {
			Common::Array<byte> bytes = makeFont(bpp, 1, 4, 4);
			for (int y = 1; y < 3; ++y)
				for (int x = 1; x < 3; ++x) {
					if (bpp == 1)
						setPixel1(bytes, 0, 4, 4, x, y);
					else
						setPixel8(bytes, 0, 4, 4, x, y, 0xFF);
				}

			Graphics::HiResBitmapFont font;
			TS_ASSERT(loadFont(font, bytes));

			int plain = 0;
			for (uint c = 0; c < ARRAYSIZE(cases); ++c) {
				for (int offset = 1; offset <= 3; ++offset) {
					Graphics::Surface dest;
					dest.create(20, 20, Graphics::PixelFormat::createFormatCLUT8());

					Graphics::GlyphStyle style;
					style.color = 7;
					style.shadowColor = 1;
					style.shadowMode = cases[c].mode;
					style.shadowOffset = offset;

					TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(
						dest, nullptr, font, 0, 8, 8, style));

					const int ink = inkCount(dest);
					if (c == 0 && offset == 1)
						plain = ink;

					// The body is always there.
					TSM_ASSERT(cases[c].name, ink >= plain);

					// And a decoration that spreads must actually spread.
					if (cases[c].spreads)
						TSM_ASSERT(cases[c].name, ink > plain);

					dest.free();
				}
			}
		}
	}

	/**
	 * A larger offset never draws less than a smaller one.
	 *
	 * offset is the thickness a map asks for, so it has to be monotonic -
	 * otherwise a map author tuning it would see the decoration flicker
	 * between weights instead of growing.
	 */
	void test_a_bigger_offset_draws_at_least_as_much() {
		Common::Array<byte> bytes = makeFont(8, 1, 4, 4);
		setPixel8(bytes, 0, 4, 4, 1, 1, 0xFF);
		setPixel8(bytes, 0, 4, 4, 2, 2, 0xFF);

		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		int previous = -1;
		for (int offset = 1; offset <= 3; ++offset) {
			Graphics::Surface dest;
			dest.create(24, 24, Graphics::PixelFormat::createFormatCLUT8());

			Graphics::GlyphStyle style;
			style.color = 7;
			style.shadowColor = 1;
			style.shadowMode = Graphics::kHiResShadowOutline;
			style.shadowOffset = offset;

			TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(
				dest, nullptr, font, 0, 10, 10, style));

			const int ink = inkCount(dest);
			TS_ASSERT(ink >= previous);
			previous = ink;

			dest.free();
		}
	}

	/**
	 * A decoration is opaque in the coverage plane, at every mode and depth.
	 *
	 * This is the fix that made outlines visible. A stroke that inherited the
	 * body's coverage was semi-transparent exactly where it should hide the
	 * background, so it blended away - invisible at 8bpp while looking
	 * correct at 1bpp, where coverage is all-or-nothing anyway.
	 */
	void test_decoration_coverage_is_always_solid() {
		static const Graphics::HiResShadowMode modes[] = {
			Graphics::kHiResShadowDrop,
			Graphics::kHiResShadowOutline,
			Graphics::kHiResShadowStroke,
		};

		for (uint m = 0; m < ARRAYSIZE(modes); ++m) {
			// A glyph whose every pixel is a faint edge: if the stroke took
			// its coverage from the body, nothing here would be solid.
			Common::Array<byte> bytes = makeFont(8, 1, 4, 4);
			setPixel8(bytes, 0, 4, 4, 1, 1, 0x20);
			setPixel8(bytes, 0, 4, 4, 2, 1, 0x20);

			Graphics::HiResBitmapFont font;
			TS_ASSERT(loadFont(font, bytes));

			Graphics::Surface dest;
		Graphics::BandedPlane cov;
			dest.create(20, 20, Graphics::PixelFormat::createFormatCLUT8());
			cov.create(20, 20, true);

			Graphics::GlyphStyle style;
			style.color = 7;
			style.shadowColor = 1;
			style.shadowMode = modes[m];
			style.shadowOffset = 1;

			TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(
				dest, &cov, font, 0, 8, 8, style));

			// Find a pixel that is decoration, and check it is fully covered.
			bool sawSolidDecoration = false;
			for (int y = 0; y < cov.height() && !sawSolidDecoration; ++y)
				for (int x = 0; x < cov.width(); ++x)
					if (at(dest, x, y) == 1) {
						TS_ASSERT_EQUALS(at(cov, x, y), 0xFF);
						sawSolidDecoration = true;
						break;
					}
			TS_ASSERT(sawSolidDecoration);

			// The body keeps its own faint coverage.
			TS_ASSERT_EQUALS(at(cov, 9, 9), 0x20);

			dest.free();
			cov.free();
		}
	}

	/**
	 * A decoration in the text colour is skipped, in every mode.
	 *
	 * Correct - it would be invisible - but it is why asking for colour 4 on
	 * FM-Towns produced nothing at all: that platform draws its text in 4.
	 * Pinned here so the behaviour is a decision rather than a surprise.
	 */
	void test_a_same_colour_decoration_is_skipped_in_every_mode() {
		static const Graphics::HiResShadowMode modes[] = {
			Graphics::kHiResShadowDrop,
			Graphics::kHiResShadowOutline,
			Graphics::kHiResShadowStroke,
		};

		Common::Array<byte> bytes = makeFont(8, 1, 4, 4);
		setPixel8(bytes, 0, 4, 4, 1, 1, 0xFF);

		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		for (uint m = 0; m < ARRAYSIZE(modes); ++m) {
			Graphics::Surface plain, same;
			plain.create(16, 16, Graphics::PixelFormat::createFormatCLUT8());
			same.create(16, 16, Graphics::PixelFormat::createFormatCLUT8());

			Graphics::GlyphStyle none;
			none.color = 7;
			TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(
				plain, nullptr, font, 0, 6, 6, none));

			Graphics::GlyphStyle style;
			style.color = 7;
			style.shadowColor = 7;      // the same
			style.shadowMode = modes[m];
			style.shadowOffset = 1;
			TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(
				same, nullptr, font, 0, 6, 6, style));

			TS_ASSERT_EQUALS(inkCount(plain), inkCount(same));

			plain.free();
			same.free();
		}
	}

	/**
	 * Both transparency conventions survive a decoration.
	 *
	 * The engine keys text out with CHARSET_MASK_TRANSPARENCY on most
	 * platforms and with 0 on FM-Towns. A decoration colour that collides
	 * with either is drawn but then read back as 'nothing here', which is
	 * exactly how a black outline vanished on FM-Towns. The renderer cannot
	 * know the platform, so what it must guarantee is narrower: it writes
	 * the colour it was given, unchanged.
	 */
	void test_the_decoration_colour_is_written_verbatim() {
		Common::Array<byte> bytes = makeFont(8, 1, 4, 4);
		setPixel8(bytes, 0, 4, 4, 1, 1, 0xFF);

		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		static const byte colours[] = { 0, 1, 8, 0xFD, 0xFF };
		for (uint i = 0; i < ARRAYSIZE(colours); ++i) {
			Graphics::Surface dest;
			dest.create(16, 16, Graphics::PixelFormat::createFormatCLUT8());
			// Fill with something neither colour, so a written pixel shows.
			dest.fillRect(Common::Rect(16, 16), 0x55);

			Graphics::GlyphStyle style;
			style.color = 7;
			style.shadowColor = colours[i];
			style.shadowMode = Graphics::kHiResShadowOutline;
			style.shadowOffset = 1;

			TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(
				dest, nullptr, font, 0, 6, 6, style));

			// The pixel diagonally out from the body is stroke, and holds
			// precisely the requested index - including 0 and 0xFD.
			TS_ASSERT_EQUALS(at(dest, 6, 6), colours[i]);

			dest.free();
		}
	}

	/**
	 * A decoration on glyphs of every shape a charset holds.
	 *
	 * A real charset mixes kinds: dense full-width CJK cells, sparse
	 * proportional Latin ones, irregular pictograms, and punctuation that is
	 * a few pixels in one corner. The decoration is dilated from whatever ink
	 * the glyph has, so the shapes that reach the cell edge are the ones that
	 * can write outside it.
	 */
	void test_decorations_on_every_glyph_shape() {
		struct Shape {
			const char *name;
			const char *rows[6];
		};
		static const Shape shapes[] = {
			{ "dense CJK cell",  { "######", "######", "######",
								   "######", "######", "######" } },
			{ "sparse Latin",    { "..##..", ".#..#.", "#....#",
								   "######", "#....#", "......" } },
			{ "corner mark",     { "##....", "##....", "......",
								   "......", "......", "......" } },
			{ "edge to edge",    { "######", "......", "......",
								   "......", "......", "######" } },
			{ "single pixel",    { "......", "......", "..#...",
								   "......", "......", "......" } },
			{ "diagonal",        { "#.....", ".#....", "..#...",
								   "...#..", "....#.", ".....#" } },
		};

		for (uint sh = 0; sh < ARRAYSIZE(shapes); ++sh) {
			Common::Array<byte> bytes = makeFont(8, 1, 6, 6);
			for (int y = 0; y < 6; ++y)
				for (int x = 0; x < 6; ++x)
					if (shapes[sh].rows[y][x] == '#')
						setPixel8(bytes, 0, 6, 6, x, y, 0xFF);

			Graphics::HiResBitmapFont font;
			TSM_ASSERT(shapes[sh].name, loadFont(font, bytes));

			for (int offset = 1; offset <= 2; ++offset) {
				// Deliberately tight: the glyph sits two pixels from the top
				// left, so a dilation of 2 reaches the edge exactly.
				Graphics::Surface dest;
		Graphics::BandedPlane cov;
				dest.create(12, 12, Graphics::PixelFormat::createFormatCLUT8());
				cov.create(12, 12, true);

				Graphics::GlyphStyle style;
				style.color = 7;
				style.shadowColor = 1;
				style.shadowMode = Graphics::kHiResShadowOutline;
				style.shadowOffset = offset;

				TSM_ASSERT(shapes[sh].name,
						   Graphics::HiResGlyphRenderer::drawGlyph(
							   dest, &cov, font, 0, 2, 2, style));

				// Every body pixel is still the text colour. On its own this
				// is nearly tautological - the body is drawn after the
				// decoration, so it lands on top - which is why the second
				// glyph below is what gives this teeth.
				for (int y = 0; y < 6; ++y)
					for (int x = 0; x < 6; ++x)
						if (shapes[sh].rows[y][x] == '#')
							TSM_ASSERT_EQUALS(shapes[sh].name,
											  at(dest, 2 + x, 2 + y), 7);

				// A second glyph alongside, close enough that its decoration
				// reaches back over the first. Nothing redraws the first
				// glyph's body afterwards, so if the decoration does not
				// yield to ink already present, this is where it shows.
				TSM_ASSERT(shapes[sh].name,
						   Graphics::HiResGlyphRenderer::drawGlyph(
							   dest, &cov, font, 0, 2 + 5, 2, style));

				for (int y = 0; y < 6; ++y)
					for (int x = 0; x < 6; ++x)
						if (shapes[sh].rows[y][x] == '#')
							TSM_ASSERT_EQUALS(shapes[sh].name,
											  at(dest, 2 + x, 2 + y), 7);

				dest.free();
				cov.free();
			}
		}
	}

	/**
	 * A decoration on a glyph at the very edge writes nothing outside.
	 *
	 * The dilation reaches further than the cell, so the clipping that was
	 * enough for a plain glyph is not obviously enough for a decorated one.
	 * Put the glyph hard against each edge in turn and check the surface is
	 * unharmed beyond it - a scribble here would be a heap overrun in the
	 * engine, where the surface is the text plane.
	 */
	void test_a_decorated_glyph_at_the_edge_stays_inside() {
		Common::Array<byte> bytes = makeFont(8, 1, 4, 4);
		for (int y = 0; y < 4; ++y)
			for (int x = 0; x < 4; ++x)
				setPixel8(bytes, 0, 4, 4, x, y, 0xFF);

		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		static const int positions[][2] = {
			{ -3, -3 }, { -3, 5 }, { 9, -3 }, { 9, 5 },   // corners, part off
			{ -4, 4 }, { 12, 4 }, { 4, -4 }, { 4, 12 },   // fully off
		};

		for (uint i = 0; i < ARRAYSIZE(positions); ++i) {
			// A guard band around the surface: the renderer is told the
			// surface is 12x12, and anything it writes beyond that lands
			// here, where it can be seen. Without this the check is only
			// "it did not crash", and a Surface is one allocation, so an
			// overrun usually does not.
			const int kGuard = 8;
			Graphics::Surface backing;
			backing.create(12 + 2 * kGuard, 12 + 2 * kGuard,
						   Graphics::PixelFormat::createFormatCLUT8());
			backing.fillRect(Common::Rect(backing.w, backing.h), 0xAA);

			// A 12x12 view onto the middle of it, sharing the same rows. The
			// coverage plane is 12x12 as well, and holds no more than that.
			Graphics::Surface dest = backing.getSubArea(
				Common::Rect(kGuard, kGuard, kGuard + 12, kGuard + 12));
			Graphics::BandedPlane cov;
			cov.create(12, 12, true);

			Graphics::GlyphStyle style;
			style.color = 7;
			style.shadowColor = 1;
			style.shadowMode = Graphics::kHiResShadowStroke;   // reaches furthest
			style.shadowOffset = 3;

			Graphics::HiResGlyphRenderer::drawGlyph(dest, &cov, font, 0,
													positions[i][0],
													positions[i][1], style);

			// Every byte outside the 12x12 view must still be the fill.
			for (int y = 0; y < backing.h; ++y) {
				for (int x = 0; x < backing.w; ++x) {
					if (x >= kGuard && x < kGuard + 12 &&
						y >= kGuard && y < kGuard + 12)
						continue;
					TS_ASSERT_EQUALS(at(backing, x, y), 0xAA);
				}
			}

			backing.free();
		}
	}

	/**
	 * A decoration clipped at the right edge must not wrap onto the next row.
	 *
	 * A Surface is one allocation, so a write past the right edge lands at
	 * the start of the row below rather than outside the buffer: no crash, no
	 * allocator complaint, just ink in the wrong place. Nothing else here
	 * looks for that, so a missing horizontal clip would pass every other
	 * test in this file.
	 */
	void test_a_decoration_does_not_wrap_around_the_right_edge() {
		Common::Array<byte> bytes = makeFont(8, 1, 4, 4);
		for (int y = 0; y < 4; ++y)
			for (int x = 0; x < 4; ++x)
				setPixel8(bytes, 0, 4, 4, x, y, 0xFF);

		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		Graphics::Surface dest;
		dest.create(16, 16, Graphics::PixelFormat::createFormatCLUT8());

		Graphics::GlyphStyle style;
		style.color = 7;
		style.shadowColor = 1;
		style.shadowMode = Graphics::kHiResShadowOutline;
		style.shadowOffset = 2;

		// Hard against the right edge: the body reaches x=15, and the stroke
		// would like to reach x=17.
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(
			dest, nullptr, font, 0, 12, 4, style));

		// The rows the glyph occupies must be clean at the left, where a
		// wrapped write would land.
		for (int y = 2; y < 11; ++y) {
			TS_ASSERT_EQUALS(at(dest, 0, y), 0);
			TS_ASSERT_EQUALS(at(dest, 1, y), 0);
		}

		dest.free();
	}

	/**
	 * A 1bpp glyph decorated with an 8bpp coverage plane present.
	 *
	 * Bitmap charsets are 1bpp while replacement fonts are 8bpp, and a map
	 * can mix them - a CJK bitmap alongside a baked Latin face. A 1bpp glyph
	 * has no partial coverage to record, so the renderer leaves the plane
	 * alone entirely (see test_1bpp_coverage_is_ignored); what matters here
	 * is that the decoration still reaches the index plane, and that the
	 * coverage plane is not half-written on the way.
	 */
	void test_a_1bpp_glyph_decorates_without_touching_coverage() {
		Common::Array<byte> bytes = makeFont(1, 1, 4, 4);
		setPixel1(bytes, 0, 4, 4, 1, 1);
		setPixel1(bytes, 0, 4, 4, 2, 1);

		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		Graphics::Surface dest;
		Graphics::BandedPlane cov;
		dest.create(16, 16, Graphics::PixelFormat::createFormatCLUT8());
		cov.create(16, 16, true);

		Graphics::GlyphStyle style;
		style.color = 7;
		style.shadowColor = 1;
		style.shadowMode = Graphics::kHiResShadowOutline;
		style.shadowOffset = 1;

		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(
			dest, &cov, font, 0, 6, 6, style));

		// Body and stroke both land, in their own colours.
		TS_ASSERT_EQUALS(at(dest, 7, 7), 7);
		TS_ASSERT_EQUALS(at(dest, 6, 6), 1);

		// And coverage stays untouched: a 1bpp glyph is all-or-nothing, so
		// there is nothing to blend and a partially written plane would be
		// worse than an empty one.
		TS_ASSERT_EQUALS(inkCount(cov), 0);

		dest.free();
		cov.free();
	}

	/**
	 * A decoration must not eat the previous glyph's body, at 1bpp either.
	 *
	 * The guard that protects body ink reads the coverage plane, and coverage
	 * is deliberately not written for a 1bpp font - so on that path the guard
	 * has nothing to consult; this is the test that says whether it matters.
	 *
	 * Two glyphs are drawn close enough that the second's stroke reaches into
	 * the first's body.
	 */
	void test_a_1bpp_decoration_does_not_eat_the_previous_glyph() {
		Common::Array<byte> bytes = makeFont(1, 1, 4, 4);
		for (int y = 1; y < 3; ++y)
			for (int x = 1; x < 3; ++x)
				setPixel1(bytes, 0, 4, 4, x, y);

		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		Graphics::Surface dest;
		dest.create(24, 12, Graphics::PixelFormat::createFormatCLUT8());

		Graphics::GlyphStyle style;
		style.color = 7;
		style.shadowColor = 1;
		style.shadowMode = Graphics::kHiResShadowOutline;
		style.shadowOffset = 1;

		// First glyph, then a second one four pixels along - close enough
		// that its outline overlaps where the first one's body sits.
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(
			dest, nullptr, font, 0, 2, 2, style));
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(
			dest, nullptr, font, 0, 6, 2, style));

		// The first glyph's body must still be its own colour.
		for (int y = 3; y < 5; ++y)
			for (int x = 3; x < 5; ++x)
				TS_ASSERT_EQUALS(at(dest, x, y), 7);

		dest.free();
	}

	/**
	 * The stroke's left-hand arm reaches two steps, and must not wrap.
	 *
	 * The stroke table reaches two steps left and two steps down, unlike the
	 * outline which stays within one. A mask sized for one step both overran
	 * its allocation and wrapped the far-left offset onto the end of the
	 * previous row - so the left arm drew on the right, one row up.
	 *
	 * test_a_decoration_does_not_wrap_around_the_right_edge uses the outline
	 * mode, which cannot reach far enough to show this.
	 */
	void test_the_stroke_reaches_two_steps_left_without_wrapping() {
		Common::Array<byte> bytes = makeFont(8, 1, 4, 4);
		// Ink in the left column only, so the arms are easy to tell apart.
		for (int y = 0; y < 4; ++y)
			setPixel8(bytes, 0, 4, 4, 0, y, 0xFF);

		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		Graphics::Surface dest;
		dest.create(20, 20, Graphics::PixelFormat::createFormatCLUT8());

		Graphics::GlyphStyle style;
		style.color = 7;
		style.shadowColor = 1;
		style.shadowMode = Graphics::kHiResShadowStroke;
		style.shadowOffset = 2;

		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(
			dest, nullptr, font, 0, 8, 8, style));

		// The glyph's ink column sits at x=8, and the stroke table reaches
		// two steps left, so x=4 must carry stroke. Under the old sizing that
		// arm wrapped to the end of the previous mask row and appeared at
		// x=12 instead, leaving x=4 empty.
		bool leftArm = false;
		for (int y = 0; y < dest.h; ++y)
			if (at(dest, 4, y) == 1)
				leftArm = true;
		TS_ASSERT(leftArm);

		// And nothing at all beyond the rightmost legitimate offset: the
		// glyph is 4 wide from x=8, plus one step, so x=13 onwards is clear.
		for (int y = 0; y < dest.h; ++y)
			for (int x = 13; x < dest.w; ++x)
				TS_ASSERT_EQUALS(at(dest, x, y), 0);
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

	/**
	 * Two fonts of the same cell but different ascents, drawn at one y.
	 *
	 * A Latin face does not fill its cell - measured on the shipped MI2 set,
	 * 'H' occupies rows 2..19 of a 24 row cell while Hangul fills 0..21 - so
	 * the two are only on one baseline if something aligns them. Nothing
	 * did: bitmap_font never set originY and ascent() had no callers at all,
	 * so every glyph went down cell-top aligned.
	 *
	 * The rule this pins: shifting by the difference of the ascents puts the
	 * two baselines on the same row. Here the cells are equal and the
	 * ascents differ by 4, so the shallower font must move down 4.
	 */
	void test_ascent_difference_is_the_baseline_shift() {
		const int cellW = 8, cellH = 16;

		// One pixel sitting exactly on each font's baseline row.
		Common::Array<byte> tall = makeFont(8, 1, cellW, cellH);
		tall[16] = 14;                                  // ascent
		setPixel8(tall, 0, cellW, cellH, 0, 14, 0xFF);  // ink on the baseline

		Common::Array<byte> shallow = makeFont(8, 1, cellW, cellH);
		shallow[16] = 10;                               // ascent, 4 rows higher
		setPixel8(shallow, 0, cellW, cellH, 0, 10, 0xFF);

		Graphics::HiResBitmapFont tallFont, shallowFont;
		TS_ASSERT(loadFont(tallFont, tall));
		TS_ASSERT(loadFont(shallowFont, shallow));

		TS_ASSERT_EQUALS(tallFont.ascent(), 14);
		TS_ASSERT_EQUALS(shallowFont.ascent(), 10);

		// Drawn at the same y, the two inks land on different rows.
		Graphics::Surface a, b;
		a.create(16, 32, Graphics::PixelFormat::createFormatCLUT8());
		b.create(16, 32, Graphics::PixelFormat::createFormatCLUT8());

		Graphics::GlyphStyle style;
		style.color = 7;
		style.shadowMode = Graphics::kHiResShadowNone;

		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(a, nullptr, tallFont, 0, 0, 0, style));
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(b, nullptr, shallowFont, 0, 0, 0, style));

		TS_ASSERT_EQUALS(*(byte *)a.getBasePtr(0, 14), 7);
		TS_ASSERT_EQUALS(*(byte *)b.getBasePtr(0, 10), 7);
		TS_ASSERT_EQUALS(*(byte *)b.getBasePtr(0, 14), 0);   // NOT aligned

		// Shifted by the ascent difference, they agree.
		Graphics::Surface c;
		c.create(16, 32, Graphics::PixelFormat::createFormatCLUT8());
		const int shift = tallFont.ascent() - shallowFont.ascent();
		TS_ASSERT_EQUALS(shift, 4);
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(c, nullptr, shallowFont, 0, 0, shift, style));
		TS_ASSERT_EQUALS(*(byte *)c.getBasePtr(0, 14), 7);

		a.free();
		b.free();
		c.free();
	}

	/**
	 * Keyed (no coverage surface) drawing keeps only real ink (C17).
	 *
	 * An 8-bit game cannot blend, so an anti-aliased glyph is keyed. Keying
	 * every pixel with any coverage at all turned the faint fringe FreeType
	 * puts around each stroke into solid ink: at Full Throttle's 12 px the
	 * counters closed and syllables like 좋 and 를 drew as filled blobs.
	 * Half coverage is too strict the other way - a thin CJK stroke at 9 to
	 * 16 px is often only a quarter to a third covered and would vanish
	 * (王 turning into 三). A quarter keeps those strokes and drops the fringe.
	 */
	void test_keyed_drawing_drops_the_faint_fringe() {
		TS_ASSERT_EQUALS((int)Graphics::HiResGlyphRenderer::kKeyedInkThreshold, 0x40);

		Common::Array<byte> bytes = makeFont(8, 1, 4, 4);
		setPixel8(bytes, 0, 4, 4, 0, 0, 0x3F);   // fringe
		setPixel8(bytes, 0, 4, 4, 1, 0, 0x40);   // a thin stroke
		setPixel8(bytes, 0, 4, 4, 2, 0, 0xFF);   // solid

		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		Graphics::GlyphStyle style;
		style.color = 5;

		Graphics::Surface keyed;
		keyed.create(8, 8, Graphics::PixelFormat::createFormatCLUT8());
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(keyed, nullptr, font, 0, 0, 0, style));
		TS_ASSERT_EQUALS(at(keyed, 0, 0), 0);
		TS_ASSERT_EQUALS(at(keyed, 1, 0), 5);
		TS_ASSERT_EQUALS(at(keyed, 2, 0), 5);

		// With a coverage surface nothing is dropped: the compositor blends
		// the fringe in at its own strength.
		Graphics::Surface dest;
		Graphics::BandedPlane cov;
		dest.create(8, 8, Graphics::PixelFormat::createFormatCLUT8());
		cov.create(8, 8, true);
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(dest, &cov, font, 0, 0, 0, style));
		TS_ASSERT_EQUALS(at(dest, 0, 0), 5);
		TS_ASSERT_EQUALS(at(cov, 0, 0), 0x3F);

		keyed.free();
		dest.free();
		cov.free();
	}

	/**
	 * A keyed decoration follows the keyed body: the fringe the body drops
	 * does not come back as shadow-coloured ink around it.
	 */
	void test_keyed_decoration_ignores_the_fringe() {
		Common::Array<byte> bytes = makeFont(8, 1, 4, 4);
		setPixel8(bytes, 0, 4, 4, 1, 1, 0xFF);
		setPixel8(bytes, 0, 4, 4, 3, 3, 0x10);   // stray fringe

		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		Graphics::GlyphStyle style;
		style.color = 5;
		style.shadowColor = 9;
		style.shadowMode = Graphics::kHiResShadowDrop;

		Graphics::Surface keyed;
		keyed.create(12, 12, Graphics::PixelFormat::createFormatCLUT8());
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(keyed, nullptr, font, 0, 2, 2, style));
		TS_ASSERT_EQUALS(at(keyed, 3, 3), 5);    // body
		TS_ASSERT_EQUALS(at(keyed, 4, 4), 9);    // its drop shadow
		TS_ASSERT_EQUALS(at(keyed, 5, 5), 0);    // fringe: no body
		TS_ASSERT_EQUALS(at(keyed, 6, 6), 0);    // fringe: no shadow
		TS_ASSERT_EQUALS(inkCount(keyed), 2);

		keyed.free();
	}

	// --- Antialiased outlines ----------------------------------------

	/// A set of planes for a layered draw, all w x h and zeroed.
	struct Planes {
		Graphics::Surface index;
		Graphics::BandedPlane cov, uIndex, uCov;
		Planes(int w, int h) {
			index.create(w, h, Graphics::PixelFormat::createFormatCLUT8());
			cov.create(w, h, true);
			uIndex.create(w, h, false);
			uCov.create(w, h, true);
		}
		~Planes() { index.free(); cov.free(); uIndex.free(); uCov.free(); }
		Graphics::GlyphPlanes layered() { return Graphics::GlyphPlanes(&index, &cov, &uIndex, &uCov); }
		bool same(const Planes &o) const {
			for (int y = 0; y < index.h; ++y)
				if (memcmp(index.getBasePtr(0, y), o.index.getBasePtr(0, y), index.w))
					return false;
			const Graphics::BandedPlane *a[] = { &cov, &uIndex, &uCov };
			const Graphics::BandedPlane *b[] = { &o.cov, &o.uIndex, &o.uCov };
			for (int i = 0; i < 3; ++i)
				for (int y = 0; y < a[i]->height(); ++y)
					for (int x = 0; x < a[i]->width(); ++x)
						if (a[i]->get(x, y) != b[i]->get(x, y))
							return false;
			return true;
		}
	};

	/// An 8bpp glyph from rows of '#' (0xFF), '+' (0x80), '-' (0x40) and '.'.
	struct Pattern {
		Common::Array<byte> px;
		Graphics::GlyphBitmap g;
		Pattern(const char *const *rows, int h) {
			const int w = strlen(rows[0]);
			px.resize(w * h);
			for (int y = 0; y < h; ++y)
				for (int x = 0; x < w; ++x) {
					const char c = rows[y][x];
					px[y * w + x] = c == '#' ? 0xFF : c == '+' ? 0x80 : c == '-' ? 0x40 : 0;
				}
			g.pixels = px.begin();
			g.pitch = w;
			g.width = w;
			g.height = h;
			g.bpp = 8;
		}
	};

	static int weightAt(const Graphics::DilationKernel &k, int dx, int dy) {
		for (int i = 0; i < k.taps; ++i)
			if (k.dx[i] == dx && k.dy[i] == dy)
				return k.w[i];
		return 0;
	}

	/**
	 * The recommended pen, 1.5 px round (C19 memo): 21 taps, full weight to a
	 * distance of sqrt(2), half at 2 and a quarter at sqrt(5).
	 */
	void test_round_kernel_of_1_5_px() {
		Graphics::DilationKernel k;
		Graphics::HiResGlyphRenderer::buildKernel(k, 6, Graphics::kHiResOutlineRound);
		int nonZero = 0;
		for (int i = 0; i < k.taps; ++i)
			if (k.w[i])
				++nonZero;
		TS_ASSERT_EQUALS(nonZero, 21);
		TS_ASSERT_EQUALS(k.reach, 2);
		TS_ASSERT_EQUALS(weightAt(k, 0, 0), 255);
		TS_ASSERT_EQUALS(weightAt(k, 1, 0), 255);
		TS_ASSERT_EQUALS(weightAt(k, 1, 1), 255);
		TS_ASSERT_EQUALS(weightAt(k, 0, -2), 128);
		TS_ASSERT_EQUALS(weightAt(k, 2, 1), 67);    // 2.5 - sqrt(5)
		TS_ASSERT_EQUALS(weightAt(k, 2, 2), 0);
	}

	/// One pixel round is the eight neighbours once cut at half: the old look.
	void test_round_kernel_of_1_px_is_the_eight_neighbours() {
		Graphics::DilationKernel k;
		Graphics::HiResGlyphRenderer::buildKernel(k, 4, Graphics::kHiResOutlineRound);
		int solid = 0;
		for (int i = 0; i < k.taps; ++i)
			if (k.w[i] >= Graphics::HiResGlyphRenderer::kKeyedDecorationThreshold) {
				TS_ASSERT(ABS(k.dx[i]) <= 1 && ABS(k.dy[i]) <= 1);
				++solid;
			}
		TS_ASSERT_EQUALS(solid, 9);
		TS_ASSERT_EQUALS(weightAt(k, 1, 1), 149);   // 2 - sqrt(2)

		// Square: the same radius reaches the corners at full weight.
		Graphics::HiResGlyphRenderer::buildKernel(k, 4, Graphics::kHiResOutlineSquare);
		TS_ASSERT_EQUALS(weightAt(k, 1, 1), 255);
		TS_ASSERT_EQUALS(weightAt(k, 2, 2), 0);
	}

	/**
	 * Legacy at step 1 is the old table plus the centre; at step 2 it is the
	 * table grown twice, so nothing between the steps is missing (defect 2).
	 */
	void test_legacy_kernel_grows_instead_of_multiplying() {
		Graphics::DilationKernel k;
		Graphics::HiResGlyphRenderer::buildKernel(k, 4, Graphics::kHiResOutlineLegacy,
												  Graphics::kHiResShadowOutline, 1);
		TS_ASSERT_EQUALS(k.taps, 9);
		Graphics::HiResGlyphRenderer::buildKernel(k, 8, Graphics::kHiResOutlineLegacy,
												  Graphics::kHiResShadowOutline, 2);
		TS_ASSERT_EQUALS(k.taps, 25);                // 5x5, not a ring at 2
		TS_ASSERT_EQUALS(weightAt(k, 1, 0), 255);

		Graphics::HiResGlyphRenderer::buildKernel(k, 4, Graphics::kHiResOutlineLegacy,
												  Graphics::kHiResShadowStroke, 1);
		TS_ASSERT_EQUALS(k.taps, 11);               // 10 distinct offsets (the table lists (-1,1) twice) and the centre
		TS_ASSERT_EQUALS(weightAt(k, -2, 0), 255);
		Graphics::HiResGlyphRenderer::buildKernel(k, 8, Graphics::kHiResOutlineLegacy,
												  Graphics::kHiResShadowStroke, 2);
		TS_ASSERT_EQUALS(weightAt(k, -1, 0), 255);   // no ghost copy at -2 alone
		TS_ASSERT_EQUALS(weightAt(k, -3, 0), 255);
		TS_ASSERT_EQUALS(weightAt(k, -4, 0), 255);
	}

	/**
	 * Dilation reads coverage as how far into its pixel the ink reaches: a
	 * pixel c covered pushes the pen's edge back by 1 - c. So the outline of
	 * a stroke drawn at partial coverage - a thin face's stems straddle two
	 * pixels at two thirds each - is as solid as a vector stroker makes it,
	 * rather than capped at the stroke's own coverage.
	 */
	void test_dilation_takes_the_strongest_neighbour() {
		static const char *const rows[] = { "#+" };
		Pattern p(rows, 1);
		Graphics::DilationKernel k;
		Graphics::HiResGlyphRenderer::buildKernel(k, 6, Graphics::kHiResOutlineRound);
		const int mw = 2 + 4, mh = 1 + 4;
		Common::Array<byte> out(mw * mh);
		Graphics::HiResGlyphRenderer::dilate(p.g, k, out.begin());
		// The glyph sits at (2,2).
		TS_ASSERT_EQUALS(out[2 * mw + 2], 255);
		TS_ASSERT_EQUALS(out[2 * mw + 3], 255);
		TS_ASSERT_EQUALS(out[2 * mw + 4], 255);          // 1 from '+': 1.5 + 0.5 - 1, clamped
		TS_ASSERT_EQUALS(out[2 * mw + 5], 1);            // 2 from '+': 1.5 + 0.5 - 2, the rim
		TS_ASSERT_EQUALS(out[0 * mw + 2], 128);          // 2 above '#'
		TS_ASSERT_EQUALS(out[0 * mw + 0], 0);            // (2,2) away: outside the disk

		// A faint stem still gets a solid outline next to it.
		static const char *const faint[] = { "-" };      // 0x40
		Pattern f(faint, 1);
		Common::Array<byte> fo(5 * 5);
		Graphics::HiResGlyphRenderer::dilate(f.g, k, fo.begin());
		TS_ASSERT_EQUALS(fo[2 * 5 + 3], 192);            // 1.5 + 0.25 - 1: three quarters
		TS_ASSERT_EQUALS(fo[2 * 5 + 2], 255);            // under the stem itself
		// Cut at a coverage first, the faint pixel counts as solid.
		Graphics::HiResGlyphRenderer::dilate(p.g, k, out.begin(), 0x40);
		TS_ASSERT_EQUALS(out[2 * mw + 5], 128);
	}

	/**
	 * The fix for the seam (C19 defect 1): with under planes, the body's
	 * antialiased edge stays in the body planes at its own coverage, and the
	 * outline under it is solid, so the compositor blends the edge over the
	 * outline instead of over the game's picture.
	 */
	void test_layered_body_edge_sits_on_a_solid_outline() {
		static const char *const rows[] = { "##+", "##+" };
		Pattern p(rows, 2);
		Planes pl(16, 16);

		Graphics::GlyphStyle style;
		style.color = 7;
		style.shadowColor = 1;
		style.shadowMode = Graphics::kHiResShadowOutline;
		style.outlineQ = 6;

		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(pl.layered(), p.g, 5, 5, style));

		// The edge pixel: body at 0x80 over a solid outline.
		TS_ASSERT_EQUALS(at(pl.index, 7, 5), 7);
		TS_ASSERT_EQUALS(at(pl.cov, 7, 5), 0x80);
		TS_ASSERT_EQUALS(at(pl.uIndex, 7, 5), 1);
		TS_ASSERT_EQUALS(at(pl.uCov, 7, 5), 255);

		// Outside the body, the outline alone, and antialiased at its rim.
		TS_ASSERT_EQUALS(at(pl.index, 4, 5), 0);
		TS_ASSERT_EQUALS(at(pl.cov, 4, 5), 0);
		TS_ASSERT_EQUALS(at(pl.uCov, 4, 5), 255);
		TS_ASSERT_EQUALS(at(pl.uCov, 3, 5), 128);
		TS_ASSERT_EQUALS(at(pl.uIndex, 3, 5), 1);
	}

	/// The body planes are the same with and without a layered decoration.
	void test_layered_decoration_leaves_the_body_planes_alone() {
		static const char *const rows[] = { ".+#+.", "+###+", ".+#+." };
		Pattern p(rows, 3);
		Planes plain(16, 16), decorated(16, 16);

		Graphics::GlyphStyle none;
		none.color = 7;
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(plain.layered(), p.g, 5, 5, none));

		Graphics::GlyphStyle style = none;
		style.shadowColor = 1;
		style.shadowMode = Graphics::kHiResShadowStroke;
		style.outlineQ = 6;
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(decorated.layered(), p.g, 5, 5, style));

		for (int y = 0; y < 16; ++y)
			for (int x = 0; x < 16; ++x) {
				TS_ASSERT_EQUALS(at(plain.index, x, y), at(decorated.index, x, y));
				TS_ASSERT_EQUALS(at(plain.cov, x, y), at(decorated.cov, x, y));
			}
		// And nothing at all goes under a plain glyph.
		TS_ASSERT_EQUALS(inkCount(plain.uCov), 0);
		TS_ASSERT(inkCount(decorated.uCov) > 0);
	}

	/**
	 * A base and a stacked mark (Thai, C19): drawn in either order, the planes
	 * are identical, and the mark's outline merges into the base's rather
	 * than cutting it.
	 */
	void test_layered_outlines_of_a_base_and_a_mark_merge_in_any_order() {
		static const char *const base[] = { "..........", "..........", "..........",
											"##....##..", "#+....+#..", "#+....+#..",
											"########.." };
		static const char *const mark[] = { "..+#+.....", "..........", "..........",
											"..........", "..........", "..........",
											".........." };
		Pattern b(base, 7), m(mark, 7);

		Graphics::GlyphStyle style;
		style.color = 7;
		style.shadowColor = 1;
		style.shadowMode = Graphics::kHiResShadowOutline;
		style.outlineQ = 6;

		Planes one(24, 16), two(24, 16);
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(one.layered(), b.g, 6, 4, style));
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(one.layered(), m.g, 6, 4, style));
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(two.layered(), m.g, 6, 4, style));
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(two.layered(), b.g, 6, 4, style));
		TS_ASSERT(one.same(two));

		// The mark's body is intact, though the base's outline reaches it.
		TS_ASSERT_EQUALS(at(one.index, 9, 4), 7);
		TS_ASSERT_EQUALS(at(one.cov, 9, 4), 0xFF);
	}

	/**
	 * A hairline outlined 2 px wide has no gaps (C19 defect 2): every pixel
	 * within two of the line is covered, and the cover only falls with
	 * distance.
	 */
	void test_a_hairline_outline_has_no_gaps() {
		static const char *const rows[] = { "#", "#", "#", "#", "#", "#" };
		Pattern p(rows, 6);

		for (int shape = 0; shape < 3; ++shape) {
			Planes pl(16, 16);
			Graphics::GlyphStyle style;
			style.color = 7;
			style.shadowColor = 1;
			style.shadowMode = Graphics::kHiResShadowOutline;
			style.shadowOffset = 2;
			style.outlineQ = 8;
			style.outlineShape = (Graphics::HiResOutlineShape)shape;
			TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(pl.layered(), p.g, 8, 5, style));

			for (int y = 5; y < 11; ++y) {
				int previous = 256;
				for (int d = 0; d <= 2; ++d) {
					const int l = at(pl.uCov, 8 - d, y), r = at(pl.uCov, 8 + d, y);
					TS_ASSERT_EQUALS(l, r);
					TS_ASSERT(l >= 128);
					TS_ASSERT(l <= previous);
					previous = l;
				}
			}
		}
	}

	/// A stroke is the outline plus a copy of it moved (-offset, +offset).
	void test_a_stroke_is_the_outline_and_its_shadow() {
		static const char *const rows[] = { "#" };
		Pattern p(rows, 1);

		Graphics::GlyphStyle style;
		style.color = 7;
		style.shadowColor = 1;
		style.shadowMode = Graphics::kHiResShadowOutline;
		style.shadowOffset = 1;
		style.outlineQ = 4;

		Planes outline(16, 16), stroke(16, 16);
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(outline.layered(), p.g, 8, 8, style));
		style.shadowMode = Graphics::kHiResShadowStroke;
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(stroke.layered(), p.g, 8, 8, style));

		for (int y = 1; y < 15; ++y)
			for (int x = 1; x < 15; ++x) {
				const int want = MAX(at(outline.uCov, x, y), at(outline.uCov, x + 1, y - 1));
				TS_ASSERT_EQUALS(at(stroke.uCov, x, y), want);
			}
		TS_ASSERT_EQUALS(at(stroke.uCov, 6, 10), 149);  // the corner of the moved copy
	}

	/// A shadow in a colour of its own, at a strength of its own.
	void test_a_shadow_has_its_own_colour_and_alpha() {
		static const char *const rows[] = { "#" };
		Pattern p(rows, 1);

		Graphics::GlyphStyle style;
		style.color = 7;
		style.shadowColor = 1;
		style.shadowMode = Graphics::kHiResShadowDrop;
		style.shadowShiftSet = true;
		style.shadowDx = 3;
		style.shadowDy = 2;
		style.shadowShiftColor = 9;
		style.shadowShiftColorSet = true;
		style.shadowAlpha = 153;

		Planes pl(16, 16);
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(pl.layered(), p.g, 4, 4, style));
		TS_ASSERT_EQUALS(at(pl.uIndex, 7, 6), 9);
		TS_ASSERT_EQUALS(at(pl.uCov, 7, 6), 153);
		TS_ASSERT_EQUALS(inkCount(pl.uCov), 1);

		// Keyed, a shadow under half strength is not drawn, and one above is solid.
		Graphics::Surface keyed;
		keyed.create(16, 16, Graphics::PixelFormat::createFormatCLUT8());
		style.shadowAlpha = 100;
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(keyed, nullptr, p.g, 4, 4, style));
		TS_ASSERT_EQUALS(inkCount(keyed), 1);
		style.shadowAlpha = 200;
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(keyed, nullptr, p.g, 4, 4, style));
		TS_ASSERT_EQUALS(at(keyed, 7, 6), 9);
		keyed.free();
	}

	/**
	 * style=legacy on a 1bpp glyph keyed at step 1 is today's outline, pixel
	 * for pixel: the old table applied to the stencil.
	 */
	void test_legacy_outline_matches_the_old_table_on_1bpp() {
		static const int8 kOutlineX[] = { -1, 0, 1, -1, 1, -1, 0, 1 };
		static const int8 kOutlineY[] = { -1, -1, -1, 0, 0, 1, 1, 1 };
		static const char *const rows[] = { ".#..#.", "######", "#....#", ".#..#." };

		Common::Array<byte> bytes = makeFont(1, 1, 6, 4);
		for (int y = 0; y < 4; ++y)
			for (int x = 0; x < 6; ++x)
				if (rows[y][x] == '#')
					setPixel1(bytes, 0, 6, 4, x, y);
		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		Graphics::Surface want, got;
		want.create(12, 10, Graphics::PixelFormat::createFormatCLUT8());
		got.create(12, 10, Graphics::PixelFormat::createFormatCLUT8());
		for (int c = 0; c < 8; ++c)
			for (int y = 0; y < 4; ++y)
				for (int x = 0; x < 6; ++x)
					if (rows[y][x] == '#')
						*(byte *)want.getBasePtr(3 + x + kOutlineX[c], 3 + y + kOutlineY[c]) = 1;
		for (int y = 0; y < 4; ++y)
			for (int x = 0; x < 6; ++x)
				if (rows[y][x] == '#')
					*(byte *)want.getBasePtr(3 + x, 3 + y) = 7;

		Graphics::GlyphStyle style;
		style.color = 7;
		style.shadowColor = 1;
		style.shadowMode = Graphics::kHiResShadowOutline;
		style.outlineShape = Graphics::kHiResOutlineLegacy;
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(got, nullptr, font, 0, 3, 3, style));

		for (int y = 0; y < 10; ++y)
			for (int x = 0; x < 12; ++x)
				TS_ASSERT_EQUALS(at(got, x, y), at(want, x, y));
		want.free();
		got.free();
	}

	/// Keyed, a later glyph's outline does not overwrite an earlier body.
	void test_a_keyed_outline_yields_to_earlier_ink() {
		static const char *const rows[] = { "###", "###", "###" };
		Pattern p(rows, 3);
		Graphics::Surface keyed;
		keyed.create(20, 10, Graphics::PixelFormat::createFormatCLUT8());

		Graphics::GlyphStyle style;
		style.color = 7;
		style.shadowColor = 1;
		style.shadowMode = Graphics::kHiResShadowOutline;
		style.outlineQ = 8;

		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(keyed, nullptr, p.g, 3, 3, style));
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(keyed, nullptr, p.g, 7, 3, style));
		for (int y = 3; y < 6; ++y)
			for (int x = 3; x < 6; ++x)
				TS_ASSERT_EQUALS(at(keyed, x, y), 7);
		keyed.free();
	}

	/// The dirty area holds the whole decoration, shadow included.
	void test_dirty_area_holds_the_outline_and_its_shadow() {
		static const char *const rows[] = { "#" };
		Pattern p(rows, 1);
		Planes pl(32, 32);

		Graphics::GlyphStyle style;
		style.color = 7;
		style.shadowColor = 1;
		style.shadowMode = Graphics::kHiResShadowStroke;
		style.shadowOffset = 3;
		style.outlineQ = 6;

		Common::Rect dirty;
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(pl.layered(), p.g, 16, 16, style, &dirty));
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 32; ++x)
				if (at(pl.uCov, x, y))
					TS_ASSERT(dirty.contains(x, y));
	}

	/// A shadow moved left and up is inside the dirty area too.
	void test_dirty_area_reaches_a_shadow_moved_left_and_up() {
		static const char *const rows[] = { "##", "##" };
		Pattern p(rows, 2);
		Planes pl(32, 32);

		Graphics::GlyphStyle style;
		style.color = 7;
		style.shadowColor = 1;
		style.shadowMode = Graphics::kHiResShadowDrop;
		style.shadowShiftSet = true;
		style.shadowDx = -5;
		style.shadowDy = -4;

		Common::Rect dirty;
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(pl.layered(), p.g, 16, 16, style, &dirty));
		TS_ASSERT_EQUALS(at(pl.uCov, 11, 12), 255);
		TS_ASSERT_EQUALS(dirty.left, 11);
		TS_ASSERT_EQUALS(dirty.top, 12);
		TS_ASSERT_EQUALS(dirty.right, 18);
		TS_ASSERT_EQUALS(dirty.bottom, 18);
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 32; ++x)
				if (at(pl.uCov, x, y))
					TS_ASSERT(dirty.contains(x, y));

		// Keyed, the same.
		Graphics::Surface keyed;
		keyed.create(32, 32, Graphics::PixelFormat::createFormatCLUT8());
		Common::Rect kd;
		TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(keyed, nullptr, p.g, 16, 16, style, &kd));
		TS_ASSERT_EQUALS(at(keyed, 11, 12), 1);
		TS_ASSERT_EQUALS(kd, dirty);
		keyed.free();
	}

	/**
	 * style=legacy keeps the old step at every scale: the map's offset as
	 * written, else 1; offset 0 draws nothing, as the old tables did.
	 */
	void test_legacy_keeps_the_old_step_at_every_scale() {
		Graphics::HiResMap map;
		map.shadowStyle = Graphics::kHiResOutlineLegacy;
		Graphics::GlyphStyle style;
		style.shadowColor = 1;
		style.color = 7;

		style.shadowMode = Graphics::kHiResShadowDrop;
		Graphics::HiResGlyphRenderer::applyMap(style, map, 3);
		TS_ASSERT_EQUALS(style.shadowOffset, 1);
		Graphics::GlyphDecoration d = Graphics::HiResGlyphRenderer::decorationFor(style);
		TS_ASSERT_EQUALS(d.shadowDx, 1);

		style.shadowMode = Graphics::kHiResShadowStroke;
		d = Graphics::HiResGlyphRenderer::decorationFor(style);
		TS_ASSERT_EQUALS(d.step, 1);

		map.shadowOffset = 0;
		Graphics::HiResGlyphRenderer::applyMap(style, map, 2);
		TS_ASSERT_EQUALS(style.shadowOffset, 0);
		d = Graphics::HiResGlyphRenderer::decorationFor(style);
		TS_ASSERT(!d.outline);
		TS_ASSERT(!d.shadow);

		// The other styles follow the scale.
		map.shadowStyle = Graphics::kHiResOutlineRound;
		map.shadowOffset = -1;
		Graphics::HiResGlyphRenderer::applyMap(style, map, 3);
		TS_ASSERT_EQUALS(style.shadowOffset, 2);
	}

	/// The cached pen follows the decoration asked for, not the first one.
	void test_a_changed_width_is_drawn_with_its_own_pen() {
		static const char *const rows[] = { "#" };
		Pattern p(rows, 1);
		Graphics::GlyphStyle style;
		style.color = 7;
		style.shadowColor = 1;
		style.shadowMode = Graphics::kHiResShadowOutline;

		for (int q = 4; q <= 12; q += 4) {
			Planes pl(24, 24);
			style.outlineQ = q;
			TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(pl.layered(), p.g, 12, 12, style));
			// Solid out to the radius, nothing two pixels beyond it.
			TS_ASSERT_EQUALS(at(pl.uCov, 12 - q / 4, 12), 255);
			TS_ASSERT_EQUALS(at(pl.uCov, 12 - q / 4 - 2, 12), 0);
		}
	}

	/// What each mode resolves to.
	void test_decoration_for_each_mode() {
		Graphics::GlyphStyle style;
		style.color = 7;
		style.shadowColor = 1;
		style.shadowOffset = 2;
		style.outlineQ = 6;

		style.shadowMode = Graphics::kHiResShadowNone;
		Graphics::GlyphDecoration d = Graphics::HiResGlyphRenderer::decorationFor(style);
		TS_ASSERT(!d.outline);
		TS_ASSERT(!d.shadow);

		style.shadowMode = Graphics::kHiResShadowDrop;
		d = Graphics::HiResGlyphRenderer::decorationFor(style);
		TS_ASSERT(!d.outline);
		TS_ASSERT(d.shadow);
		TS_ASSERT_EQUALS(d.shadowDx, 2);
		TS_ASSERT_EQUALS(d.shadowDy, 2);

		style.shadowMode = Graphics::kHiResShadowOutline;
		d = Graphics::HiResGlyphRenderer::decorationFor(style);
		TS_ASSERT(d.outline);
		TS_ASSERT_EQUALS(d.outlineQ, 6);
		TS_ASSERT(!d.shadow);

		style.shadowMode = Graphics::kHiResShadowStroke;
		d = Graphics::HiResGlyphRenderer::decorationFor(style);
		TS_ASSERT(d.outline);
		TS_ASSERT(d.shadow);
		TS_ASSERT_EQUALS(d.shadowDx, -2);
		TS_ASSERT_EQUALS(d.shadowDy, 2);
		TS_ASSERT_EQUALS(d.shadowColor, 1);

		// An explicit shadow replaces the mode's, and (0,0) removes it.
		style.shadowShiftSet = true;
		style.shadowDx = 0;
		style.shadowDy = 0;
		d = Graphics::HiResGlyphRenderer::decorationFor(style);
		TS_ASSERT(d.outline);
		TS_ASSERT(!d.shadow);
		style.shadowMode = Graphics::kHiResShadowOutline;
		style.shadowDx = 1;
		style.shadowDy = 3;
		style.shadowShiftColor = 4;
		style.shadowShiftColorSet = true;
		d = Graphics::HiResGlyphRenderer::decorationFor(style);
		TS_ASSERT(d.shadow);
		TS_ASSERT_EQUALS(d.shadowDx, 1);
		TS_ASSERT_EQUALS(d.shadowDy, 3);
		TS_ASSERT_EQUALS(d.shadowColor, 4);

		// Legacy strokes carry their weighting in the table.
		style.shadowShiftSet = false;
		style.shadowMode = Graphics::kHiResShadowStroke;
		style.outlineShape = Graphics::kHiResOutlineLegacy;
		d = Graphics::HiResGlyphRenderer::decorationFor(style);
		TS_ASSERT(d.outline);
		TS_ASSERT(!d.shadow);
		TS_ASSERT_EQUALS(d.legacyTable, Graphics::kHiResShadowStroke);
		TS_ASSERT_EQUALS(d.step, 2);

		// No outlineQ: the offset is the width, as it always was.
		style.outlineShape = Graphics::kHiResOutlineRound;
		style.shadowMode = Graphics::kHiResShadowOutline;
		style.outlineQ = -1;
		d = Graphics::HiResGlyphRenderer::decorationFor(style);
		TS_ASSERT_EQUALS(d.outlineQ, 8);
	}

	/// A map's [shadow] keys, and their defaults at each scale.
	void test_apply_map_defaults_follow_the_scale() {
		Graphics::HiResMap map;
		Graphics::GlyphStyle style;
		style.shadowMode = Graphics::kHiResShadowOutline;

		Graphics::HiResGlyphRenderer::applyMap(style, map, 2);
		TS_ASSERT_EQUALS(style.outlineQ, 6);          // 1.5 px at 2x
		TS_ASSERT_EQUALS(style.shadowOffset, 1);      // half a game pixel
		TS_ASSERT_EQUALS(style.outlineShape, Graphics::kHiResOutlineRound);
		TS_ASSERT(!style.shadowShiftSet);

		Graphics::HiResGlyphRenderer::applyMap(style, map, 1);
		TS_ASSERT_EQUALS(style.outlineQ, 3);
		TS_ASSERT_EQUALS(style.shadowOffset, 1);
		Graphics::HiResGlyphRenderer::applyMap(style, map, 3);
		TS_ASSERT_EQUALS(style.outlineQ, 9);
		TS_ASSERT_EQUALS(style.shadowOffset, 2);

		// offset= alone: the width it always gave, and the shadow distance.
		map.shadowOffset = 2;
		Graphics::HiResGlyphRenderer::applyMap(style, map, 2);
		TS_ASSERT_EQUALS(style.outlineQ, 8);
		TS_ASSERT_EQUALS(style.shadowOffset, 2);

		// width= wins for the width; the rest is carried across.
		map.shadowWidthQ = 5;
		map.shadowStyle = Graphics::kHiResOutlineSquare;
		map.shadowShiftSet = true;
		map.shadowDx = -1;
		map.shadowDy = 1;
		map.shadowShiftColor = 3;
		map.shadowShiftColorSet = true;
		map.shadowAlpha = 128;
		Graphics::HiResGlyphRenderer::applyMap(style, map, 2);
		TS_ASSERT_EQUALS(style.outlineQ, 5);
		TS_ASSERT_EQUALS(style.outlineShape, Graphics::kHiResOutlineSquare);
		TS_ASSERT(style.shadowShiftSet);
		TS_ASSERT_EQUALS(style.shadowDx, -1);
		TS_ASSERT_EQUALS(style.shadowDy, 1);
		TS_ASSERT_EQUALS(style.shadowShiftColor, 3);
		TS_ASSERT(style.shadowShiftColorSet);
		TS_ASSERT_EQUALS(style.shadowAlpha, 128);
	}

	/// A font with no ascent recorded must not be shifted by a wild amount.
	void test_a_missing_ascent_asks_for_no_shift() {
		Common::Array<byte> bytes = makeFont(8, 1, 8, 16);
		bytes[16] = 0;                                  // nothing recorded

		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));
		TS_ASSERT_EQUALS(font.ascent(), 0);
	}
};
