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
#include "graphics/hires_text/latin_advance.h"

using Graphics::advanceGamePx;

/**
 * advanceGamePx(): the advance of one drawn code point, in game pixels -
 * the game font's width (advance=game) or the face's own advance, rounded
 * (advance=font).
 */
class SciLatinAdvanceTestSuite : public CxxTest::TestSuite {
public:
	void test_game_advance_returns_the_game_width() {
		TS_ASSERT_EQUALS(advanceGamePx(Graphics::kHiResAdvanceGame, 6, 13, 2), 6);
		TS_ASSERT_EQUALS(advanceGamePx(Graphics::kHiResAdvanceGame, 3, 0, 2), 3);
		TS_ASSERT_EQUALS(advanceGamePx(Graphics::kHiResAdvanceGame, 8, 30, 1), 8);
	}

	void test_font_advance_rounds_half_up() {
		TS_ASSERT_EQUALS(advanceGamePx(Graphics::kHiResAdvanceFont, 6, 13, 2), 7);
		TS_ASSERT_EQUALS(advanceGamePx(Graphics::kHiResAdvanceFont, 6, 12, 2), 6);
		TS_ASSERT_EQUALS(advanceGamePx(Graphics::kHiResAdvanceFont, 6, 11, 2), 6);
		TS_ASSERT_EQUALS(advanceGamePx(Graphics::kHiResAdvanceFont, 6, 9, 2), 5);
		// At scale 1 (glyphs at game resolution) the advance is used as is.
		TS_ASSERT_EQUALS(advanceGamePx(Graphics::kHiResAdvanceFont, 6, 9, 1), 9);
	}

	void test_font_advance_falls_back_to_game_when_unknown() {
		TS_ASSERT_EQUALS(advanceGamePx(Graphics::kHiResAdvanceFont, 5, 0, 2), 5);
		TS_ASSERT_EQUALS(advanceGamePx(Graphics::kHiResAdvanceFont, 5, -3, 2), 5);
	}

	void test_never_zero_for_a_positive_input() {
		for (int a = 1; a <= 64; a++) {
			TS_ASSERT(advanceGamePx(Graphics::kHiResAdvanceFont, 0, a, 2) >= 1);
			TS_ASSERT(advanceGamePx(Graphics::kHiResAdvanceFont, 0, a, 1) >= 1);
		}
		TS_ASSERT_EQUALS(advanceGamePx(Graphics::kHiResAdvanceFont, 0, 1, 2), 1);
		for (int w = 1; w <= 16; w++)
			TS_ASSERT_EQUALS(advanceGamePx(Graphics::kHiResAdvanceGame, w, 0, 2), w);
	}

	// cell and the engine default are not game/font metrics: the caller
	// applies its own rule.
	void test_cell_and_engine_advance_are_left_to_the_caller() {
		TS_ASSERT_EQUALS(advanceGamePx(Graphics::kHiResAdvanceCell, 8, 20, 2), -1);
		TS_ASSERT_EQUALS(advanceGamePx(Graphics::kHiResAdvanceEngine, 8, 20, 2), -1);
	}

	// ---- cellFallbackWidth()/advanceGameOrFontPx() (design 6.3's cell
	// fallback, shared by GfxFontSet and GfxFontUnicodeAdapter's own
	// getCharWidth() instead of each duplicating the arithmetic) ----------

	void test_cell_fallback_width_is_half_the_cell_for_a_narrow_glyph() {
		// Half the cell, then scaled down to game px, same as the cell rule
		// (kHiResAdvanceCell) a narrow glyph with no face advance falls to.
		TS_ASSERT_EQUALS(Graphics::cellFallbackWidth(false, 16, 2), 4);
		TS_ASSERT_EQUALS(Graphics::cellFallbackWidth(false, 16, 1), 8);
		TS_ASSERT_EQUALS(Graphics::cellFallbackWidth(false, 9, 2), 2);
	}

	void test_cell_fallback_width_is_the_whole_cell_for_a_wide_glyph() {
		TS_ASSERT_EQUALS(Graphics::cellFallbackWidth(true, 16, 2), 8);
		TS_ASSERT_EQUALS(Graphics::cellFallbackWidth(true, 16, 1), 16);
	}

	void test_cell_fallback_width_is_never_zero() {
		TS_ASSERT_EQUALS(Graphics::cellFallbackWidth(false, 1, 2), 1);
		TS_ASSERT_EQUALS(Graphics::cellFallbackWidth(false, 0, 2), 1);
	}

	// design 6.3: a glyph the game font has no width for at all (@p gameWidth
	// <= 0, the resource face's own width for the game id's game code - never
	// the drawn/remapped/target code) falls to the cell - wide glyphs to the
	// whole cell, narrow ones to half - rather than to the overprinting 0 a
	// bare advanceGamePx(rule, 0, ...) would return.
	void test_advance_game_falls_to_the_cell_when_the_game_font_lacks_the_glyph() {
		TS_ASSERT_EQUALS(Graphics::advanceGameOrFontPx(Graphics::kHiResAdvanceGame, /* gameWidth */ 0, /* cell */ 16,
														/* isWide */ true, /* faceAdvanceHires */ 0, /* scale */ 1),
						 16);
		TS_ASSERT_EQUALS(Graphics::advanceGameOrFontPx(Graphics::kHiResAdvanceGame, 0, 16, false, 0, 1), 8);
		// advance=font still falls back to the cell, not to the game width,
		// when the game font has nothing either (design 6.3's chain: font,
		// then game, then the cell).
		TS_ASSERT_EQUALS(Graphics::advanceGameOrFontPx(Graphics::kHiResAdvanceFont, 0, 16, true, 0, 1), 16);
	}

	void test_advance_game_or_font_keeps_the_game_width_when_it_has_one() {
		// A real gameWidth (the game font does have the glyph) is used as is
		// for advance=game, and as advanceGamePx()'s own fallback for
		// advance=font when the face cannot say - the cell is never reached.
		TS_ASSERT_EQUALS(Graphics::advanceGameOrFontPx(Graphics::kHiResAdvanceGame, 6, 16, true, 13, 2), 6);
		TS_ASSERT_EQUALS(Graphics::advanceGameOrFontPx(Graphics::kHiResAdvanceFont, 6, 16, true, 0, 2), 6);
		TS_ASSERT_EQUALS(Graphics::advanceGameOrFontPx(Graphics::kHiResAdvanceFont, 6, 16, true, 13, 2), 7);
	}

	// A game font that knows only the game's own code 'A' (6 px): a
	// fullwidth remap draws U+FF21, which the game font has nothing for.
	struct GameFontKnowsOnlyA {
		int operator()(uint32 code) const { return code == 'A' ? 6 : 0; }
	};

	// The game font is asked for the game code, not the drawn code: a remap
	// keeps the game's own width instead of falling to the cell.
	void test_advance_for_game_code_measures_the_game_code_not_the_drawn_one() {
		TS_ASSERT_EQUALS(Graphics::advanceForGameCode(Graphics::kHiResAdvanceGame, 'A', 0xFF21, GameFontKnowsOnlyA(),
													  16, 0, 2), 6);
		// Measured on the drawn code it would have been the wide cell
		// fallback (16 / 2) instead.
		TS_ASSERT_DIFFERS(Graphics::advanceForGameCode(Graphics::kHiResAdvanceGame, 'A', 0xFF21, GameFontKnowsOnlyA(),
													   16, 0, 2), 8);
	}

	// No width for the game code either: the cell, wide or narrow by the
	// drawn code point, never 0.
	void test_advance_for_game_code_falls_to_the_cell_by_the_drawn_code() {
		TS_ASSERT_EQUALS(Graphics::advanceForGameCode(Graphics::kHiResAdvanceGame, 0xAC00, 0xAC00, GameFontKnowsOnlyA(),
													  16, 0, 2), 8);
		TS_ASSERT_EQUALS(Graphics::advanceForGameCode(Graphics::kHiResAdvanceGame, 0xE9, 0xE9, GameFontKnowsOnlyA(),
													  16, 0, 2), 4);
		TS_ASSERT_EQUALS(Graphics::advanceForGameCode(Graphics::kHiResAdvanceFont, 0xAC00, 0xAC00, GameFontKnowsOnlyA(),
													  16, 0, 1), 16);
		TS_ASSERT_EQUALS(Graphics::advanceForGameCode(Graphics::kHiResAdvanceFont, 0xAC00, 0xAC00, GameFontKnowsOnlyA(),
													  16, 30, 2), 15);
	}
};
