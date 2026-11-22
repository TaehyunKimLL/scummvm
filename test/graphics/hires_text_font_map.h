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

#include "common/memstream.h"

#include "graphics/hires_text/font_map.h"

class HiResTextFontMapTestSuite : public CxxTest::TestSuite {
public:
	void test_codePoints() {
		uint32 cp;
		TS_ASSERT(Graphics::HiResFontMap::parseCodePoint("U+25A1", cp));
		TS_ASSERT_EQUALS(cp, 0x25A1u);
		TS_ASSERT(Graphics::HiResFontMap::parseCodePoint("0x10ffff", cp));
		TS_ASSERT(!Graphics::HiResFontMap::parseCodePoint("u+110000", cp));
		TS_ASSERT(!Graphics::HiResFontMap::parseCodePoint("25A1", cp));
		TS_ASSERT(!Graphics::HiResFontMap::parseCodePoint("u+", cp));
	}

	void test_resolveFace() {
		Graphics::HiResFaceRef r = Graphics::HiResFontMap::resolveFace("data:hires_text/X.SVF", Common::Path("game/ko"));
		TS_ASSERT(r.data);
		TS_ASSERT_EQUALS(r.path.toString('/'), "hires_text/X.SVF");
		TS_ASSERT_EQUALS(r.key(), "data:hires_text/X.SVF");
		r = Graphics::HiResFontMap::resolveFace("A.SVF", Common::Path("game/ko"));
		TS_ASSERT(!r.data);
		TS_ASSERT_EQUALS(r.key(), "game/ko/A.SVF");
		TS_ASSERT_EQUALS(Graphics::HiResFontMap::resolveFace("/abs/B.SVF", Common::Path("game")).key(), "/abs/B.SVF");
		TS_ASSERT_EQUALS(Graphics::HiResFontMap::resolveFace("C:\\fonts\\C.SVF", Common::Path("game")).path.toString('/'), "C:/fonts/C.SVF");
	}
};
