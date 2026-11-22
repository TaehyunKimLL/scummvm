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

#include "common/ustr.h"

#include "graphics/fonts/svffont.h"

#include "test/graphics/svf_golden_data.h"
#include "test/graphics/svf_test_util.h"

class SvfFontTestSuite : public CxxTest::TestSuite {
public:
	void test_metrics() {
		Graphics::SvfFont font(SvfTest::makeChain(), DisposeAfterUse::YES);
		TS_ASSERT_EQUALS(font.getFontAscent(), 13);	// marks face: ascent 13 of 16
		TS_ASSERT_EQUALS(font.getFontDescent(), 4);	// jamo face: 16 - 12
		TS_ASSERT_EQUALS(font.getFontHeight(), 17);
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
};
