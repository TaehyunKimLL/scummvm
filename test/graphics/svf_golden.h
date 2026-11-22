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

#define SVF_GOLDEN(p) p##Text, p##X, p##Y, p##W, p##H, p##ScaleNum, p##ScaleDen, p##Missing, p##Rows

class SvfGoldenTestSuite : public CxxTest::TestSuite {
	// Draws a golden case on GRAY8 and compares it with the reference bytes.
	void checkGray8(const char *text, int x, int y, int w, int h, int num, int den, int missing, const char *const *rows,
	                bool streamed = false, uint cacheSlots = 0, const Common::Rect *clip = nullptr) {
		Graphics::SvfChain *chain = SvfTest::makeChain(num, den, missing, streamed);
		chain->setSyllableCache(cacheSlots);
		Common::Array<byte> buf;
		buf.resize(w * h);
		memset(buf.data(), 0, buf.size());
		svf_target t;
		TS_ASSERT_EQUALS(svf_target_init(&t, SVF_T_GRAY8, buf.data(), buf.size(), w, h, w), SVF_OK);
		if (clip) {
			t.clip_x0 = clip->left;
			t.clip_y0 = clip->top;
			t.clip_x1 = clip->right;
			t.clip_y1 = clip->bottom;
		}
		TS_ASSERT_EQUALS(chain->draw(text, strlen(text), x, y, t), SVF_OK);
		if (cacheSlots)		// a second, cached draw must not change a pixel
			TS_ASSERT_EQUALS(chain->draw(text, strlen(text), x, y, t), SVF_OK);
		Common::Array<byte> expected = SvfTest::goldenBytes(rows, w, h);
		if (clip) {
			for (int yy = 0; yy < h; ++yy)
				for (int xx = 0; xx < w; ++xx)
					if (!clip->contains(xx, yy))
						expected[yy * w + xx] = 0;
		}
		const bool same = memcmp(buf.data(), expected.data(), buf.size()) == 0;
		TS_ASSERT(same);
		if (!same) {
			TS_TRACE(("got" + SvfTest::asciiArt(buf.data(), w, h)).c_str());
			TS_TRACE(("expected" + SvfTest::asciiArt(expected.data(), w, h)).c_str());
		}
		delete chain;
	}

public:
	void test_line1x() { checkGray8(SVF_GOLDEN(kSvfGoldenLine1x)); }
	void test_line2x() { checkGray8(SVF_GOLDEN(kSvfGoldenLine2x)); }
	void test_line3by2() { checkGray8(SVF_GOLDEN(kSvfGoldenLine3by2)); }
	void test_streamed() { checkGray8(SVF_GOLDEN(kSvfGoldenLine1x), true); }
	void test_syllableCache() { checkGray8(SVF_GOLDEN(kSvfGoldenLine1x), true, 16); }
	void test_clip() {
		const Common::Rect clip(10, 4, 60, 14);
		checkGray8(SVF_GOLDEN(kSvfGoldenLine1x), false, 0, &clip);
	}

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

	void test_cachedStreamedReadsNothing() {
		Graphics::SvfChain *chain = SvfTest::makeChain(1, 1, 0, true, 0);	// no block cache: every glyph is read
		chain->setSyllableCache(16);
		Common::Array<byte> buf;
		buf.resize(64 * 20);
		svf_target t;
		svf_target_init(&t, SVF_T_GRAY8, buf.data(), buf.size(), 64, 20, 64);
		const char *text = "\xed\x95\x9c\xea\xb8\x80";	// two syllables
		chain->draw(text, strlen(text), 0, 14, t);
		const uint32 reads = chain->getFace(1)->getStreamReads();
		chain->draw(text, strlen(text), 0, 14, t);
		TS_ASSERT_EQUALS(chain->getFace(1)->getStreamReads(), reads);
		delete chain;
	}
};
