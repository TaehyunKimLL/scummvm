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

#include "graphics/svf/svf.h"

class SvfBuildTestSuite : public CxxTest::TestSuite {
public:
	void test_switches() {
		TS_ASSERT_EQUALS(SVF_JMAP, 1);
		TS_ASSERT_EQUALS(SVF_JSET_BIG, 1);
		TS_ASSERT_EQUALS(SVF_JMAP_RECOG, 0);
	}

	void test_linked() {
		const uint16_t *set = nullptr;
		TS_ASSERT_EQUALS(svf_layouts(&set), 0u);	// every layout is read
		TS_ASSERT_EQUALS(svf_bpp(nullptr), 0);
	}
};
