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

			Graphics::Surface dest, cov;
			dest.create(20, 20, Graphics::PixelFormat::createFormatCLUT8());
			cov.create(20, 20, Graphics::PixelFormat::createFormatCLUT8());

			Graphics::GlyphStyle style;
			style.color = 7;
			style.shadowColor = 1;
			style.shadowMode = modes[m];
			style.shadowOffset = 1;

			TS_ASSERT(Graphics::HiResGlyphRenderer::drawGlyph(
				dest, &cov, font, 0, 8, 8, style));

			// Find a pixel that is decoration, and check it is fully covered.
			bool sawSolidDecoration = false;
			for (int y = 0; y < cov.h && !sawSolidDecoration; ++y)
				for (int x = 0; x < cov.w; ++x)
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
				Graphics::Surface dest, cov;
				dest.create(12, 12, Graphics::PixelFormat::createFormatCLUT8());
				cov.create(12, 12, Graphics::PixelFormat::createFormatCLUT8());

				Graphics::GlyphStyle style;
				style.color = 7;
				style.shadowColor = 1;
				style.shadowMode = Graphics::kHiResShadowOutline;
				style.shadowOffset = offset;

				TSM_ASSERT(shapes[sh].name,
						   Graphics::HiResGlyphRenderer::drawGlyph(
							   dest, &cov, font, 0, 2, 2, style));

				// Every body pixel is still the text colour: a decoration
				// must frame the letterform, never eat into it.
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
			Graphics::Surface backing, cbacking;
			backing.create(12 + 2 * kGuard, 12 + 2 * kGuard,
						   Graphics::PixelFormat::createFormatCLUT8());
			cbacking.create(12 + 2 * kGuard, 12 + 2 * kGuard,
							Graphics::PixelFormat::createFormatCLUT8());
			backing.fillRect(Common::Rect(backing.w, backing.h), 0xAA);
			cbacking.fillRect(Common::Rect(cbacking.w, cbacking.h), 0xAA);

			// A 12x12 view onto the middle of it, sharing the same rows.
			Graphics::Surface dest = backing.getSubArea(
				Common::Rect(kGuard, kGuard, kGuard + 12, kGuard + 12));
			Graphics::Surface cov = cbacking.getSubArea(
				Common::Rect(kGuard, kGuard, kGuard + 12, kGuard + 12));

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
					TS_ASSERT_EQUALS(at(cbacking, x, y), 0xAA);
				}
			}

			backing.free();
			cbacking.free();
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

		Graphics::Surface dest, cov;
		dest.create(16, 16, Graphics::PixelFormat::createFormatCLUT8());
		cov.create(16, 16, Graphics::PixelFormat::createFormatCLUT8());

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
	 * has nothing to consult. Reported by review; this is the test that says
	 * whether it matters.
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
