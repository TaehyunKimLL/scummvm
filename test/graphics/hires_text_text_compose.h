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

#include "graphics/hires_text/text_compose.h"
#include "graphics/hires_text/text_plane.h"
#include "graphics/surface.h"

namespace {

// Index 0 navy, 1 yellow, 2 white.
const byte kComposePalette[256 * 3] = { 0, 0, 128, 255, 255, 0, 255, 255, 255 };

const Graphics::PixelFormat kComposeRgb565(2, 5, 6, 5, 0, 11, 5, 0, 0);
const Graphics::PixelFormat kComposeXrgb8888(4, 8, 8, 8, 0, 16, 8, 0, 0);

} // End of anonymous namespace

class HiResTextTextComposeTestSuite : public CxxTest::TestSuite {
public:
	void test_blend() {
		TS_ASSERT_EQUALS(Graphics::TextCompose::blend(0, 255, 170), 170);
		TS_ASSERT_EQUALS(Graphics::TextCompose::blend(128, 0, 170), 43);
		TS_ASSERT_EQUALS(Graphics::TextCompose::blend(10, 200, 0), 10);
		TS_ASSERT_EQUALS(Graphics::TextCompose::blend(10, 200, 255), 200);
	}

	void test_stamp() {
		byte dst[4] = { 9, 9, 9, 9 };
		const byte idx[4] = { 1, 2, 3, 4 };
		const byte cov[4] = { 0, 127, 128, 255 };
		Graphics::TextCompose::stampSpan(dst, idx, cov, 4);
		TS_ASSERT_EQUALS(dst[0], 9);
		TS_ASSERT_EQUALS(dst[1], 9);
		TS_ASSERT_EQUALS(dst[2], 3);
		TS_ASSERT_EQUALS(dst[3], 4);
	}

	// Yellow at 2 bpp levels over navy, read back from the screen.
	void test_composeRgb565() {
		uint16 dst[4];
		for (int i = 0; i < 4; ++i)
			dst[i] = (uint16)kComposeRgb565.RGBToColor(0, 0, 128);
		const byte idx[4] = { 1, 1, 1, 1 };
		const byte cov[4] = { 0, 85, 170, 255 };
		Graphics::TextCompose::composeSpan((byte *)dst, kComposeRgb565, idx, cov, 4, kComposePalette, Graphics::TextCompose::kBlend);
		TS_ASSERT_EQUALS(dst[0], 0x0010);
		TS_ASSERT_EQUALS(dst[1], 0x52AB);
		TS_ASSERT_EQUALS(dst[2], 0xAD45);
		TS_ASSERT_EQUALS(dst[3], 0xFFE0);
		Graphics::TextCompose::composeSpan((byte *)dst, kComposeRgb565, idx, cov, 4, kComposePalette, Graphics::TextCompose::kThreshold);
		TS_ASSERT_EQUALS(dst[1], 0x52AB);	// 85 < 128: untouched
		TS_ASSERT_EQUALS(dst[2], 0xFFE0);
	}

	// From the frame's own indices: the under colour is exact (navy is 128, not 132 read back from RGB565).
	void test_convertXrgb8888() {
		uint32 dst[4];
		const byte frame[4] = { 0, 0, 0, 2 };
		const byte idx[4] = { 1, 1, 1, 1 };
		const byte cov[4] = { 0, 85, 170, 255 };
		Graphics::TextCompose::convertSpan((byte *)dst, kComposeXrgb8888, frame, idx, cov, 4, kComposePalette, Graphics::TextCompose::kBlend);
		TS_ASSERT_EQUALS(dst[0], 0x000080u);
		TS_ASSERT_EQUALS(dst[1], 0x555555u);
		TS_ASSERT_EQUALS(dst[2], 0xAAAA2Bu);
		TS_ASSERT_EQUALS(dst[3], 0xFFFF00u);
		Graphics::TextCompose::convertSpan((byte *)dst, kComposeXrgb8888, frame, nullptr, nullptr, 4, kComposePalette, Graphics::TextCompose::kBlend);
		TS_ASSERT_EQUALS(dst[3], 0xFFFFFFu);
	}

	void test_compositeRectChecksPairing() {
		Graphics::TextPlane plane;
		plane.create(4, 2, 2);
		plane.plot(1, 1, 255, 1);
		Graphics::Surface s;
		s.create(8, 4, Graphics::PixelFormat::createFormatCLUT8());
		s.fillRect(Common::Rect(8, 4), 0);
		TS_ASSERT(!Graphics::TextCompose::compositeRect(plane, Common::Rect(8, 4), s, kComposePalette, Graphics::TextCompose::kBlend));
		TS_ASSERT(Graphics::TextCompose::compositeRect(plane, Common::Rect(8, 4), s, nullptr, Graphics::TextCompose::kStamp));
		TS_ASSERT_EQUALS(*(const byte *)s.getBasePtr(1, 1), 1);
		TS_ASSERT_EQUALS(*(const byte *)s.getBasePtr(2, 1), 0);
		s.free();
	}
};
