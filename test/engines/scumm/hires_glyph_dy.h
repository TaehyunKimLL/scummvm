#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/memstream.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/surface.h"

#include "engines/scumm/hires_overlay.h"
#include "engines/scumm/hires_text.h"

#include "support/hires_fixture.h"

/**
 * SVFN per-glyph dy as SCUMM draws it (graphics/hires_text/README.md,
 * "Per-glyph dy"): a glyph is drawn dy rows below its cell position, and
 * its ink outside the cell is drawn, not clipped.
 */
class ScummHiResGlyphDyTestSuite : public CxxTest::TestSuite {
private:
	static const byte kInk = 15;
	static const int kCell = 16;

	struct G {
		uint32 cp;
		int dy;
		int x0, x1, y0, y1;	///< the glyph's ink, a rectangle in its cell
	};

	static void put16(Common::Array<byte> &b, uint pos, uint16 v) {
		b[pos] = v & 0xff;
		b[pos + 1] = (v >> 8) & 0xff;
	}

	static void put32(Common::Array<byte> &b, uint pos, uint32 v) {
		for (int i = 0; i < 4; i++)
			b[pos + i] = (v >> (8 * i)) & 0xff;
	}

	/// A proportional 8 bpp SVFN with the dy range @p dyMin..@p dyMax (header bytes 17/18).
	static Common::Array<byte> svfn(const G *g, int n, int dyMin, int dyMax) {
		const uint32 stride = kCell * kCell;
		const uint32 metricsOff = 36, dataOff = metricsOff + n * 4;
		const uint32 cmapOff = dataOff + stride * n;
		Common::Array<byte> b;
		b.resize(cmapOff + n * 8, 0);
		b[0] = 'S'; b[1] = 'V'; b[2] = 'F'; b[3] = 'N';
		put16(b, 4, 2);
		put16(b, 6, 1);
		b[8] = 8;
		put16(b, 12, n);
		b[14] = kCell;
		b[15] = kCell;
		b[16] = 12;
		b[17] = (byte)(int8)dyMin;
		b[18] = (byte)(int8)dyMax;
		put32(b, 20, metricsOff);
		put32(b, 24, dataOff);
		put32(b, 28, stride * n);
		put32(b, 32, cmapOff);
		for (int i = 0; i < n; i++) {
			b[metricsOff + i * 4 + 0] = 10;
			b[metricsOff + i * 4 + 1] = 0;
			b[metricsOff + i * 4 + 2] = g[i].x1 - g[i].x0;
			b[metricsOff + i * 4 + 3] = (byte)(int8)g[i].dy;
			for (int y = g[i].y0; y < g[i].y1; y++)
				for (int x = g[i].x0; x < g[i].x1; x++)
					b[dataOff + i * stride + y * kCell + x] = 255;
			put32(b, cmapOff + i * 8, g[i].cp);
			put32(b, cmapOff + i * 8 + 4, i);
		}
		return b;
	}

	static Graphics::HiResMap parse(const char *body) {
		Graphics::HiResMap m;
		const Common::String text = Common::String("[map]\nversion=2\n") + body;
		Common::Array<Common::String> q;
		Common::MemoryReadStream stream((const byte *)text.c_str(), text.size());
		TS_ASSERT(Graphics::HiResFontMap::loadMap(stream, Common::Path("/tmp/dy", '/'), q, Graphics::kHiResKeysScumm, m));
		return m;
	}

	static bool ink(const Graphics::Surface &s, int x, int y) {
		if (x < 0 || y < 0 || x >= s.w || y >= s.h)
			return false;
		return *(const byte *)s.getBasePtr(x, y) != 0;
	}

	/// The rows with ink in column @p x, first and last (-1, -1 when none).
	static void inkSpan(const Graphics::Surface &s, int x, int &first, int &last) {
		first = last = -1;
		for (int y = 0; y < s.h; ++y)
			if (ink(s, x, y)) {
				if (first < 0)
					first = y;
				last = y;
			}
	}

	/// 'A' 'p' 't' drawn at pens 4, 20, 36 on row 10 with the font given, mirror= as @p mirror says.
	static void draw(const G *g, int n, int dyMin, int dyMax, Graphics::Surface &dest, const char *mirror = "off") {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 48, true);
		Scumm::ScummHiResText hr;
		hr.useOverlay(&overlay);
		const Common::String map = Common::String::format(
			"[render]\nblend=off\n[font.0]\nface=/tmp/dy/f.svf\nmirror=%s\n[font]\nadvance.basic-latin=font\n", mirror);
		hr.adoptMap(parse(map.c_str()));
		hr.useUtf8Text();
		hr.noteGameCharset(0, 8, 8);
		hr.setCharsetGrid(0, 8, 8);
		const Common::Array<byte> bytes = svfn(g, n, dyMin, dyMax);
		Common::MemoryReadStream s(bytes.begin(), bytes.size());
		TS_ASSERT(hr.addFace("/tmp/dy/f.svf", s));
		dest.create(64, 48, Graphics::PixelFormat::createFormatCLUT8());
		memset(dest.getPixels(), 0, 64 * 48);
		hr.beginString();
		const uint32 text[] = { 'A', 'p', 't' };
		for (int i = 0; i < 3; ++i)
			TS_ASSERT(hr.drawChar(dest, text[i], 0, 4 + 16 * i, 10, kInk, 0, 1));
	}

public:
	void setUp() { ScummHiResFixture::setUp(); }
	void tearDown() { ScummHiResFixture::tearDown(); }

	/// (a) dy 0 in a font with a range drawn byte for byte as the same font without one.
	void test_dy_zero_draws_as_before() {
		const G g[] = { { 'A', 0, 2, 8, 2, 12 }, { 'p', 0, 2, 8, 6, 16 }, { 't', 0, 2, 8, 0, 10 } };
		Graphics::Surface a, b;
		draw(g, 3, 0, 0, a);
		draw(g, 3, -2, 3, b);
		TS_ASSERT_SAME_DATA(a.getPixels(), b.getPixels(), 64 * 48);
		a.free();
		b.free();
	}

	/// (b) (c) dy +3: 3 rows lower, the descender drawn past the cell bottom (row 10 + 16); dy -2: 2 rows higher,
	/// above the cell top; dy 0 where it was.
	void test_glyphs_are_drawn_by_their_dy() {
		const G g[] = { { 'A', 0, 2, 8, 2, 12 }, { 'p', 3, 2, 8, 6, 16 }, { 't', -2, 2, 8, 0, 10 } };
		Graphics::Surface s;
		draw(g, 3, -2, 3, s);
		int first, last;
		inkSpan(s, 4 + 4, first, last);
		TS_ASSERT_EQUALS(first, 10 + 2);
		TS_ASSERT_EQUALS(last, 10 + 11);
		inkSpan(s, 20 + 4, first, last);
		TS_ASSERT_EQUALS(first, 10 + 6 + 3);
		TS_ASSERT_EQUALS(last, 10 + 15 + 3);	// rows 26, 27, 28: below the cell, not clipped
		inkSpan(s, 36 + 4, first, last);
		TS_ASSERT_EQUALS(first, 10 - 2);		// rows 8, 9: above the cell
		TS_ASSERT_EQUALS(last, 10 + 9 - 2);
		s.free();
	}

	/// Turned upside down (mirror=vertical) the glyph turns within its cell: a glyph moved down is drawn moved up.
	void test_vertical_mirror_turns_within_the_cell() {
		const G g[] = { { 'A', 0, 2, 8, 2, 12 }, { 'p', 3, 2, 8, 6, 16 }, { 't', -2, 2, 8, 0, 10 } };
		Graphics::Surface s;
		draw(g, 3, -2, 3, s, "vertical");
		int first, last;
		// Cell rows r drawn at 10 + 15 - r.
		inkSpan(s, 4 + 4, first, last);
		TS_ASSERT_EQUALS(first, 10 + 15 - 11);
		TS_ASSERT_EQUALS(last, 10 + 15 - 2);
		inkSpan(s, 20 + 4, first, last);
		TS_ASSERT_EQUALS(first, 10 + 15 - 18);
		TS_ASSERT_EQUALS(last, 10 + 15 - 9);
		inkSpan(s, 36 + 4, first, last);
		TS_ASSERT_EQUALS(first, 10 + 15 - 7);
		TS_ASSERT_EQUALS(last, 10 + 15 + 2);
		s.free();
	}
};
