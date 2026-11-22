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

#include "common/scummsys.h"

#include "test/graphics/svf_golden_data.h"
#include "test/graphics/svf_test_util.h"

class SvfGoldenTestSuite : public CxxTest::TestSuite {
public:
	void test_measure() {
		Graphics::SvfChain *chain = SvfTest::makeChain(1, 1, kSvfGoldenLine1xMissing);
		svf_extent e;
		TS_ASSERT_EQUALS(chain->measure(kSvfGoldenLine1xText, strlen(kSvfGoldenLine1xText), e), SVF_OK);
		TS_ASSERT_EQUALS(e.width, kSvfGoldenLine1xWidth);
		TS_ASSERT_EQUALS(e.ascent, kSvfGoldenLine1xAscent);
		TS_ASSERT_EQUALS(e.descent, kSvfGoldenLine1xDescent);
		TS_ASSERT_EQUALS(chain->getAscent(), 13);
		TS_ASSERT_EQUALS(chain->getDescent(), 4);
		delete chain;
		chain = SvfTest::makeChain(3, 2);
		TS_ASSERT_EQUALS(chain->getWidth(kSvfGoldenLine3by2Text, strlen(kSvfGoldenLine3by2Text)), kSvfGoldenLine3by2Width);
		TS_ASSERT_EQUALS(chain->getAscent(), 20);
		TS_ASSERT(!chain->setScale(0, 1));
		TS_ASSERT(!chain->setScale(17, 1));
		delete chain;
	}
};
