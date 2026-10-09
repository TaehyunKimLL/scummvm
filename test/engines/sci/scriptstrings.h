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

#include "common/array.h"
#include "sci/engine/translation.h"

/**
 * Where the strings of an SCI1.1 heap begin. sci-<lang>.str keys a heap
 * string by its position among them, so the first string must be counted
 * even when it is empty: QFG1's script 990 opens with two empty strings,
 * and with them lost every id after is two too small.
 */
class SciHeapStringsStartTestSuite : public CxxTest::TestSuite {
public:
	// header {end, objectStart/2 - 2}, one object of 3 properties (magic,
	// count, then the rest of its 3-cell body), then the strings in @p tail.
	static Common::Array<byte> heap(const byte *tail, uint tailSize) {
		static const byte head[] = {
			0, 0, 0, 0,                   // end of strings (patched below), no pre-object cells
			0x34, 0x12, 0x03, 0x00, 0, 0  // object: magic, 3 properties = 6 bytes in all
		};
		Common::Array<byte> h;
		for (uint i = 0; i < sizeof(head); i++)
			h.push_back(head[i]);
		for (uint i = 0; i < tailSize; i++)
			h.push_back(tail[i]);
		h[0] = h.size() & 0xFF;
		h[1] = h.size() >> 8;
		return h;
	}

	void test_leading_empty_strings_are_counted() {
		const byte tail[] = { 0, 0, 'A', 0, 'B', 'C', 0 };
		Common::Array<byte> h = heap(tail, sizeof(tail));
		TS_ASSERT_EQUALS(Sci::ScriptStrings::sci11StringsStart(&h[0], h.size()), 10u);
	}

	void test_text_right_after_the_objects() {
		const byte tail[] = { 'H', 'i', 0, 'Y', 'o', 0 };
		Common::Array<byte> h = heap(tail, sizeof(tail));
		TS_ASSERT_EQUALS(Sci::ScriptStrings::sci11StringsStart(&h[0], h.size()), 10u);
	}

	void test_no_objects() {
		byte h[] = { 6, 0, 0, 0, 0x00, 0x00, 'x', 0 };
		TS_ASSERT_EQUALS(Sci::ScriptStrings::sci11StringsStart(h, sizeof(h)), 4u);
	}

	void test_two_objects_then_strings() {
		byte h[] = {
			0, 0, 0, 0,
			0x34, 0x12, 0x03, 0x00, 0, 0,
			0x34, 0x12, 0x02, 0x00,
			0, 0, 'A', 0
		};
		h[0] = sizeof(h);
		TS_ASSERT_EQUALS(Sci::ScriptStrings::sci11StringsStart(h, sizeof(h)), 14u);
	}

	void test_truncated_heaps_are_bounded() {
		byte tiny[] = { 4, 0, 0 };
		TS_ASSERT_EQUALS(Sci::ScriptStrings::sci11StringsStart(tiny, sizeof(tiny)), 3u);
		// an object whose property count runs past the end of the heap
		byte cut[] = { 12, 0, 0, 0, 0x34, 0x12, 0xFF, 0x00 };
		TS_ASSERT_EQUALS(Sci::ScriptStrings::sci11StringsStart(cut, sizeof(cut)), (uint)sizeof(cut));
	}
};
