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

#include "graphics/hires_text/text_plane.h"

class HiResTextTextPlaneTestSuite : public CxxTest::TestSuite {
public:
	void test_createAndPlot() {
		Graphics::TextPlane p;
		TS_ASSERT(!p.create(320, 200, 0));
		TS_ASSERT(p.create(320, 200, 2));
		TS_ASSERT_EQUALS(p.getWidth(), 640);
		TS_ASSERT_EQUALS(p.getHeight(), 400);
		TS_ASSERT(p.isEmpty());
		TS_ASSERT_EQUALS(p.getBytesHeld(), 0u);
		p.plot(10, 20, 255, 0);	// index 0 is a colour like any other
		p.plot(-1, 0, 255, 3);
		p.plot(640, 0, 255, 3);
		TS_ASSERT(!p.isEmpty());
		TS_ASSERT(p.rowHasText(20));
		TS_ASSERT(!p.rowHasText(40));
		TS_ASSERT_EQUALS(p.getCoverage(10, 20), 255);
		TS_ASSERT_EQUALS(p.getIndex(10, 20), 0);
		// One 16-row band of 640 4-bit coverage pixels; index 0 holds no index band.
		TS_ASSERT_EQUALS(p.getBytesHeld(), 16u * 320);
	}

	void test_plotRule() {
		Graphics::TextPlane p;
		p.create(8, 8, 1);
		p.plot(1, 1, 170, 5);
		p.plot(1, 1, 85, 5);	// same index: the larger coverage stays
		TS_ASSERT_EQUALS(p.getCoverage(1, 1), 170);
		p.plot(1, 1, 255, 5);
		TS_ASSERT_EQUALS(p.getCoverage(1, 1), 255);
		p.plot(1, 1, 85, 9);	// another index replaces it
		TS_ASSERT_EQUALS(p.getCoverage(1, 1), 85);
		TS_ASSERT_EQUALS(p.getIndex(1, 1), 9);
		p.plot(1, 1, 0, 2);		// coverage 0 is no text
		TS_ASSERT_EQUALS(p.getIndex(1, 1), 9);
	}

	void test_clearAndBands() {
		Graphics::TextPlane p;
		p.create(320, 200, 2);
		p.plot(4, 4, 255, 15);
		p.plot(4, 100, 255, 15);
		TS_ASSERT_EQUALS(p.getBytesHeld(), 2u * (16 * 320 + 16 * 640));
		p.clearLowresRect(Common::Rect(0, 0, 10, 10));
		TS_ASSERT_EQUALS(p.getCoverage(4, 4), 0);
		TS_ASSERT_EQUALS(p.getCoverage(4, 100), 255);
		TS_ASSERT_EQUALS(p.getBytesHeld(), 16u * 320 + 16 * 640);
		p.clear();
		TS_ASSERT(p.isEmpty());
		TS_ASSERT_EQUALS(p.getBytesHeld(), 0u);
	}

	void test_eightBitCoverageUnpacks() {
		Graphics::TextPlane p;
		p.create(16, 16, 1);
		p.plot(0, 0, 170, 1);
		p.plot(1, 0, 100, 1);	// not a multiple of 17
		TS_ASSERT_EQUALS(p.getCoverage(0, 0), 170);
		TS_ASSERT_EQUALS(p.getCoverage(1, 0), 100);
	}

	void test_invert() {
		Graphics::TextPlane p;
		p.create(10, 10, 2);
		p.plot(2, 2, 255, 0x03);
		p.plot(3, 2, 255, 0x0C);
		p.xorIndicesLowresRect(Common::Rect(0, 0, 2, 2), 0x0F);
		TS_ASSERT_EQUALS(p.getIndex(2, 2), 0x0C);
		TS_ASSERT_EQUALS(p.getIndex(3, 2), 0x03);
		TS_ASSERT_EQUALS(p.getIndex(5, 5), 0);	// no text: untouched
		p.xorIndicesLowresRect(Common::Rect(0, 0, 2, 2), 0x0F);
		TS_ASSERT_EQUALS(p.getIndex(2, 2), 0x03);
		p.swapIndicesLowresRect(Common::Rect(0, 0, 2, 2), 0x03, 0x0C);
		TS_ASSERT_EQUALS(p.getIndex(2, 2), 0x0C);
		TS_ASSERT_EQUALS(p.getIndex(3, 2), 0x03);
	}

	void test_saveRestore() {
		Graphics::TextPlane p;
		p.create(20, 20, 2);
		const Common::Rect box(1, 1, 5, 4);
		TS_ASSERT_EQUALS(p.saveSize(box), 1u);
		p.plot(3, 3, 170, 4);
		p.plot(9, 7, 255, 6);
		TS_ASSERT_EQUALS(p.saveSize(box), 1u + 2 * 8 * 6);
		byte buf[1 + 2 * 8 * 6];
		byte *out = buf;
		p.save(box, out);
		TS_ASSERT_EQUALS((uint32)(out - buf), p.saveSize(box));
		p.clearLowresRect(box);
		TS_ASSERT_EQUALS(p.getCoverage(3, 3), 0);
		const byte *in = buf;
		p.restore(box, in);
		TS_ASSERT_EQUALS(p.getCoverage(3, 3), 170);
		TS_ASSERT_EQUALS(p.getIndex(9, 7), 6);
		const byte empty = 0;
		in = &empty;
		p.restore(box, in);
		TS_ASSERT(p.isEmpty());
	}

	void test_getRow() {
		Graphics::TextPlane p;
		p.create(8, 8, 1);
		byte is[8], cs[8];
		const byte *idx, *cov;
		TS_ASSERT(!p.getRow(3, 0, 8, is, cs, idx, cov));
		p.plot(5, 3, 85, 2);
		TS_ASSERT(p.getRow(3, 4, 3, is, cs, idx, cov));
		TS_ASSERT_EQUALS(cov[0], 0);
		TS_ASSERT_EQUALS(cov[1], 85);
		TS_ASSERT_EQUALS(idx[1], 2);
	}
};
