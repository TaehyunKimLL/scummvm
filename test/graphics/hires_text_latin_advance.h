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

#include "graphics/hires_text/font_map.h"
#include "sci/graphics/latinadvance.h"

using Sci::latinAdvanceGamePx;

/**
 * latinAdvanceGamePx(): hires_text_latin=proportional's advance for one ASCII
 * character, in game pixels - the game font's width (metrics=game) or the
 * TrueType face's own advance, rounded (metrics=font).
 */
class SciLatinAdvanceTestSuite : public CxxTest::TestSuite {
public:
	void test_game_metrics_return_the_game_width() {
		TS_ASSERT_EQUALS(latinAdvanceGamePx(Graphics::kHiResMetricsGame, 6, 13, 2), 6);
		TS_ASSERT_EQUALS(latinAdvanceGamePx(Graphics::kHiResMetricsGame, 3, 0, 2), 3);
		TS_ASSERT_EQUALS(latinAdvanceGamePx(Graphics::kHiResMetricsGame, 8, 30, 1), 8);
	}

	void test_font_metrics_round_half_up() {
		TS_ASSERT_EQUALS(latinAdvanceGamePx(Graphics::kHiResMetricsFont, 6, 13, 2), 7);
		TS_ASSERT_EQUALS(latinAdvanceGamePx(Graphics::kHiResMetricsFont, 6, 12, 2), 6);
		TS_ASSERT_EQUALS(latinAdvanceGamePx(Graphics::kHiResMetricsFont, 6, 11, 2), 6);
		TS_ASSERT_EQUALS(latinAdvanceGamePx(Graphics::kHiResMetricsFont, 6, 9, 2), 5);
		// At scale 1 (glyphs at game resolution) the advance is used as is.
		TS_ASSERT_EQUALS(latinAdvanceGamePx(Graphics::kHiResMetricsFont, 6, 9, 1), 9);
	}

	void test_font_metrics_fall_back_to_game_when_unknown() {
		TS_ASSERT_EQUALS(latinAdvanceGamePx(Graphics::kHiResMetricsFont, 5, 0, 2), 5);
		TS_ASSERT_EQUALS(latinAdvanceGamePx(Graphics::kHiResMetricsFont, 5, -3, 2), 5);
	}

	void test_never_zero_for_a_positive_input() {
		for (int a = 1; a <= 64; a++) {
			TS_ASSERT(latinAdvanceGamePx(Graphics::kHiResMetricsFont, 0, a, 2) >= 1);
			TS_ASSERT(latinAdvanceGamePx(Graphics::kHiResMetricsFont, 0, a, 1) >= 1);
		}
		TS_ASSERT_EQUALS(latinAdvanceGamePx(Graphics::kHiResMetricsFont, 0, 1, 2), 1);
		for (int w = 1; w <= 16; w++)
			TS_ASSERT_EQUALS(latinAdvanceGamePx(Graphics::kHiResMetricsGame, w, 0, 2), w);
	}
};
