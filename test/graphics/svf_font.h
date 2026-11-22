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

#include "common/file.h"
#include "common/fs.h"
#include "common/ustr.h"

#include "graphics/fonts/svffont.h"
#include "graphics/pixelformat.h"
#include "graphics/surface.h"
#include "image/png.h"

#include "test/graphics/svf_golden_data.h"
#include "test/graphics/svf_test_util.h"
#include "test/system/null_osystem.h"

class SvfFontTestSuite : public CxxTest::TestSuite {
	static uint32 blend(const Graphics::PixelFormat &f, uint32 bg, uint32 fg, byte cov) {
		if (cov == 0)
			return bg;
		if (cov == 255)
			return fg;
		byte ba, br, bgr, bb, fa, fr, fgr, fb;
		f.colorToARGB(bg, ba, br, bgr, bb);
		f.colorToARGB(fg, fa, fr, fgr, fb);
#define SVF_MIX(b, c) (byte)(((uint)(b) * (255 - cov) + (uint)(c) * cov + 127) / 255)
		return f.ARGBToColor(SVF_MIX(ba, fa), SVF_MIX(br, fr), SVF_MIX(bgr, fgr), SVF_MIX(bb, fb));
#undef SVF_MIX
	}

	static uint32 pixelAt(const Graphics::Surface &s, int x, int y) {
		const void *p = s.getBasePtr(x, y);
		return s.format.bytesPerPixel == 1 ? *(const byte *)p : s.format.bytesPerPixel == 2 ? *(const uint16 *)p : *(const uint32 *)p;
	}

	// Draws @p text with SvfFont::drawString at (x, y - ascent) and compares each pixel with the golden coverage.
	void checkSurface(const Graphics::PixelFormat &format, uint num, uint den, const char *text, int x, int y, int w, int h,
	                  const char *const *rows, uint32 bg, uint32 fg) {
		Graphics::SvfFont font(SvfTest::makeChain(num, den), DisposeAfterUse::YES);
		Graphics::Surface s;
		s.create(w, h, format);
		s.fillRect(Common::Rect(w, h), bg);
		font.drawString(&s, Common::U32String(text), x, y - font.getFontAscent(), w, fg);
		const Common::Array<byte> cov = SvfTest::goldenBytes(rows, w, h);
		int bad = 0;
		for (int yy = 0; yy < h; ++yy)
			for (int xx = 0; xx < w; ++xx) {
				const uint32 want = format.isCLUT8() ? (cov[yy * w + xx] >= 128 ? fg : bg) : blend(format, bg, fg, cov[yy * w + xx]);
				if (pixelAt(s, xx, yy) != want)
					++bad;
			}
		TS_ASSERT_EQUALS(bad, 0);
		s.free();
	}

public:
	void test_metrics() {
		Graphics::SvfFont font(SvfTest::makeChain(), DisposeAfterUse::YES);
		TS_ASSERT_EQUALS(font.getFontAscent(), 13);	// marks face: ascent 13 of 16
		TS_ASSERT_EQUALS(font.getFontDescent(), 4);	// jamo face: 16 - 12
		TS_ASSERT_EQUALS(font.getFontHeight(), 16);	// the cell height; ascent + descent would be 17
		TS_ASSERT_EQUALS(font.getMaxCharWidth(), 16);
		TS_ASSERT_EQUALS(font.getCharWidth(0xD55C), 16);
		TS_ASSERT_EQUALS(font.getCharWidth('a'), 8);
		TS_ASSERT_EQUALS(font.getCharWidth(0x0301), 0);	// a mark
		TS_ASSERT_EQUALS(font.getCharWidth(0x2603), 0);	// no face has it, missing advance 0
		TS_ASSERT_EQUALS(font.getStringWidth(Common::U32String(kSvfGoldenFont1xText)), kSvfGoldenFont1xWidth);
		TS_ASSERT_EQUALS(font.getFontName(), "marks");
	}

	void test_scaledMetrics() {
		Graphics::SvfFont font(SvfTest::makeChain(3, 2), DisposeAfterUse::YES);
		TS_ASSERT_EQUALS(font.getFontAscent(), 20);
		TS_ASSERT_EQUALS(font.getFontHeight(), 24);
		TS_ASSERT_EQUALS(font.getCharWidth(0xD55C), 24);
	}

	void test_encodeUtf8() {
		char b[4];
		TS_ASSERT_EQUALS(Graphics::SvfFont::encodeUtf8('a', b), 1u);
		TS_ASSERT_EQUALS(Graphics::SvfFont::encodeUtf8(0xD55C, b), 3u);
		TS_ASSERT_EQUALS(memcmp(b, "\xed\x95\x9c", 3), 0);
		TS_ASSERT_EQUALS(Graphics::SvfFont::encodeUtf8(0x1F600, b), 4u);
		TS_ASSERT_EQUALS(Graphics::SvfFont::encodeUtf8(0xD800, b), 1u);
		TS_ASSERT_EQUALS((byte)b[0], 0xFF);
	}

	void test_clut8() {
		checkSurface(Graphics::PixelFormat::createFormatCLUT8(), 1, 1, kSvfGoldenFont1xText, kSvfGoldenFont1xX, kSvfGoldenFont1xY,
		             kSvfGoldenFont1xW, kSvfGoldenFont1xH, kSvfGoldenFont1xRows, 3, 7);
	}

	void test_rgb565() {
		const Graphics::PixelFormat f(2, 5, 6, 5, 0, 11, 5, 0, 0);
		checkSurface(f, 1, 1, kSvfGoldenFont1xText, kSvfGoldenFont1xX, kSvfGoldenFont1xY, kSvfGoldenFont1xW, kSvfGoldenFont1xH,
		             kSvfGoldenFont1xRows, f.RGBToColor(0, 0, 128), f.RGBToColor(255, 255, 0));
		// 3/2 has partial coverage: the blend is checked pixel by pixel
		checkSurface(f, 3, 2, kSvfGoldenLine3by2Text, kSvfGoldenLine3by2X, kSvfGoldenLine3by2Y, kSvfGoldenLine3by2W,
		             kSvfGoldenLine3by2H, kSvfGoldenLine3by2Rows, f.RGBToColor(0, 0, 128), f.RGBToColor(255, 255, 0));
	}

	void test_argb32() {
		const Graphics::PixelFormat f = Graphics::PixelFormat::createFormatARGB32();
		checkSurface(f, 3, 2, kSvfGoldenLine3by2Text, kSvfGoldenLine3by2X, kSvfGoldenLine3by2Y, kSvfGoldenLine3by2W,
		             kSvfGoldenLine3by2H, kSvfGoldenLine3by2Rows, f.ARGBToColor(255, 10, 20, 30), f.ARGBToColor(255, 250, 240, 230));
	}

	void test_rgb332() {	// the 1 byte overlay format of the SDL surface backend: blended, not thresholded
		const Graphics::PixelFormat f(1, 3, 3, 2, 0, 5, 2, 0, 0);
		checkSurface(f, 3, 2, kSvfGoldenLine3by2Text, kSvfGoldenLine3by2X, kSvfGoldenLine3by2Y, kSvfGoldenLine3by2W,
		             kSvfGoldenLine3by2H, kSvfGoldenLine3by2Rows, f.RGBToColor(0, 0, 0), f.RGBToColor(255, 255, 255));
	}

	void test_edges() {	// cells partly or wholly outside the surface: nothing outside it is written
		Graphics::SvfFont font(SvfTest::makeChain(2, 1), DisposeAfterUse::YES);
		Graphics::Surface s;
		s.create(20, 10, Graphics::PixelFormat::createFormatCLUT8());
		s.fillRect(Common::Rect(20, 10), 0);
		const int xs[] = { -40, -20, -7, 0, 9, 19, 40 };
		const int ys[] = { -40, -9, 0, 5, 9, 40 };
		for (uint i = 0; i < ARRAYSIZE(xs); ++i)
			for (uint j = 0; j < ARRAYSIZE(ys); ++j)
				font.drawChar(&s, 0xD55C, xs[i], ys[j], 1);
		font.drawChar(&s, 0xD55C, 100000, 0, 1);
		font.drawChar(&s, 0xD55C, 0, -100000, 1);
		s.free();
	}

	// Developer aid: with a directory "svf-dump" in the working directory, writes the CLUT8 and RGB565 test lines
	// as PNG files there (make test, then look at svf-dump/*.png).
	void test_dumpWhenAsked() {
		Common::install_null_g_system();
		if (Common::FSNode(Common::Path("svf-dump")).isDirectory()) {
			const Graphics::PixelFormat formats[] = { Graphics::PixelFormat::createFormatCLUT8(), Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0) };
			const char *names[] = { "svf-dump/clut8.png", "svf-dump/rgb565.png" };
			for (int i = 0; i < 2; ++i) {
				Graphics::SvfFont font(SvfTest::makeChain(2, 1), DisposeAfterUse::YES);
				Graphics::Surface s;
				s.create(240, 40, formats[i]);
				s.fillRect(Common::Rect(240, 40), i ? formats[i].RGBToColor(0, 0, 128) : 0);
				font.drawString(&s, Common::U32String(kSvfGoldenFont1xText), 4, 2, 236, i ? formats[i].RGBToColor(255, 255, 0) : 1);
				Common::DumpFile out;
				const byte palette[6] = { 0, 0, 128, 255, 255, 0 };
				TS_ASSERT(out.open(Common::Path(names[i])));
				TS_ASSERT(Image::writePNG(out, s, i ? nullptr : palette, 2));
				s.free();
			}
		}
		Common::uninstall_null_g_system();
	}
};
