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

#include "graphics/pixelformat.h"
#include "graphics/hires_text/text_compose.h"

class SciTextComposeTestSuite : public CxxTest::TestSuite {
public:
	static Graphics::PixelFormat argb() { return Graphics::PixelFormat(4, 8, 8, 8, 8, 16, 8, 0, 24); }
	static Graphics::PixelFormat rgb565() { return Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0); }

	void test_expand_coverage_1_2_8_bpp() {
		const byte one[1] = { 0xA0 };            // 1010 0000
		TS_ASSERT_EQUALS(Graphics::TextCompose::expandCoverage(one, 0, 1), 255);
		TS_ASSERT_EQUALS(Graphics::TextCompose::expandCoverage(one, 1, 1), 0);
		const byte two[1] = { 0x1B };            // 00 01 10 11
		TS_ASSERT_EQUALS(Graphics::TextCompose::expandCoverage(two, 0, 2), 0);
		TS_ASSERT_EQUALS(Graphics::TextCompose::expandCoverage(two, 1, 2), 85);
		TS_ASSERT_EQUALS(Graphics::TextCompose::expandCoverage(two, 2, 2), 170);
		TS_ASSERT_EQUALS(Graphics::TextCompose::expandCoverage(two, 3, 2), 255);
		const byte eight[2] = { 7, 200 };
		TS_ASSERT_EQUALS(Graphics::TextCompose::expandCoverage(eight, 1, 8), 200);
	}

	void test_expand_row_greyed_checkerboard() {
		const byte eight[4] = { 255, 255, 255, 255 };
		byte out[4];
		Graphics::TextCompose::expandGlyphRow(out, eight, 4, 8, true, 10, 3);
		// screenY 10 is even: pixel x is dropped where (3 + x) is even.
		TS_ASSERT_EQUALS(out[0], 255);   // 3 odd
		TS_ASSERT_EQUALS(out[1], 0);     // 4 even
		TS_ASSERT_EQUALS(out[2], 255);
		TS_ASSERT_EQUALS(out[3], 0);
		Graphics::TextCompose::expandGlyphRow(out, eight, 4, 8, false, 10, 3);
		TS_ASSERT_EQUALS(out[1], 255);
	}

	void test_blend_endpoints_and_middle() {
		TS_ASSERT_EQUALS(Graphics::TextCompose::blend(10, 200, 0), 10);
		TS_ASSERT_EQUALS(Graphics::TextCompose::blend(10, 200, 255), 200);
		TS_ASSERT_EQUALS(Graphics::TextCompose::blend(0, 255, 128), 128);
	}

	void test_compose_outline_then_fg_argb() {
		byte pal[768] = { 0 };
		pal[3 * 1 + 0] = 255;                              // index 1 red
		pal[3 * 2 + 0] = pal[3 * 2 + 1] = pal[3 * 2 + 2] = 255;   // index 2 white
		const Graphics::PixelFormat f = argb();
		uint32 px = f.RGBToColor(0, 0, 0);
		Graphics::TextPixel t = { 2, 128, 1, 255 };             // half white over full red outline
		Graphics::TextCompose::composeSpan((byte *)&px, f, &t, 1, pal);
		byte r, g, b;
		f.colorToRGB(px, r, g, b);
		TS_ASSERT_EQUALS(r, 255);
		TS_ASSERT_EQUALS(g, 128);
		TS_ASSERT_EQUALS(b, 128);
	}

	void test_compose_skips_uncovered_pixels_rgb565() {
		byte pal[768] = { 0 };
		const Graphics::PixelFormat f = rgb565();
		uint16 px[2] = { (uint16)f.RGBToColor(80, 160, 240), (uint16)f.RGBToColor(80, 160, 240) };
		const uint16 before = px[1];
		Graphics::TextPixel t[2] = { { 0, 255, 0, 0 }, { 0, 0, 0, 0 } };
		Graphics::TextCompose::composeSpan((byte *)px, f, t, 2, pal);
		TS_ASSERT_EQUALS(px[0], (uint16)f.RGBToColor(0, 0, 0));
		TS_ASSERT_EQUALS(px[1], before);
	}

	void test_compose_follows_palette() {
		byte palA[768] = { 0 }, palB[768] = { 0 };
		palA[3 * 5 + 1] = 255;          // index 5 green in A
		palB[3 * 5 + 2] = 255;          // index 5 blue in B
		const Graphics::PixelFormat f = argb();
		Graphics::TextPixel t = { 5, 255, 0, 0 };
		uint32 a = f.RGBToColor(0, 0, 0), b = a;
		Graphics::TextCompose::composeSpan((byte *)&a, f, &t, 1, palA);
		Graphics::TextCompose::composeSpan((byte *)&b, f, &t, 1, palB);
		TS_ASSERT_EQUALS(a, f.RGBToColor(0, 255, 0));
		TS_ASSERT_EQUALS(b, f.RGBToColor(0, 0, 255));
	}

	void test_stamp_span() {
		byte idx[3] = { 7, 7, 7 };
		Graphics::TextPixel t[3] = { { 3, 200, 0, 0 }, { 3, 100, 4, 255 }, { 3, 100, 4, 100 } };
		Graphics::TextCompose::stampSpan(idx, t, 3);
		TS_ASSERT_EQUALS(idx[0], 3);
		TS_ASSERT_EQUALS(idx[1], 4);
		TS_ASSERT_EQUALS(idx[2], 7);
	}
	void test_coverage_to_argb() {
		// Grim's TTF lines: RGB is the text colour, alpha is the coverage.
		const byte cov[3] = { 0, 128, 255 };
		const Graphics::PixelFormat formats[2] = { argb(), Graphics::PixelFormat::createFormatRGBA32() };
		for (int f = 0; f < 2; f++) {
			const Graphics::PixelFormat &fmt = formats[f];
			uint32 px[4] = { 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF };
			Graphics::TextCompose::coverageToArgb(cov, px, 3, fmt, 10, 20, 30);
			for (int i = 0; i < 3; i++) {
				byte a, r, g, b;
				fmt.colorToARGB(px[i], a, r, g, b);
				TS_ASSERT_EQUALS(a, cov[i]);
				TS_ASSERT_EQUALS(r, 10);
				TS_ASSERT_EQUALS(g, 20);
				TS_ASSERT_EQUALS(b, 30);
			}
			TS_ASSERT_EQUALS(px[0], fmt.ARGBToColor(0, 10, 20, 30));
			TS_ASSERT_EQUALS(px[1], fmt.ARGBToColor(128, 10, 20, 30));
			TS_ASSERT_EQUALS(px[2], fmt.ARGBToColor(255, 10, 20, 30));
			TS_ASSERT_EQUALS(px[3], 0xDEADBEEFU);   // count is respected
		}
		// Literal values, so a channel placed wrongly in both the helper and
		// the format cannot cancel out.
		uint32 px = 0;
		const byte half = 0x80;
		Graphics::TextCompose::coverageToArgb(&half, &px, 1, argb(), 10, 20, 30);
		TS_ASSERT_EQUALS(px, 0x800A141EU);
		Graphics::TextCompose::coverageToArgb(&half, &px, 1, Graphics::PixelFormat::createFormatRGBA32(), 10, 20, 30);
		const byte *mem = (const byte *)&px;
		TS_ASSERT_EQUALS(mem[0], 0x0A);
		TS_ASSERT_EQUALS(mem[1], 0x14);
		TS_ASSERT_EQUALS(mem[2], 0x1E);
		TS_ASSERT_EQUALS(mem[3], 0x80);
	}
};
