#include <cxxtest/TestSuite.h>

#include "graphics/hires_text/font_map.h"

/**
 * The whole-multiple scale rule.
 *
 * A text surface can only be enlarged by an integer, so a replacement font
 * lands on the game's own grid only at a scale where its cell is exactly the
 * game's cell times that scale. Getting this wrong is not a crash: the glyphs
 * are drawn into a line box that is too small and the bottom of every one is
 * cut off, which reads as a broken font rather than as a mismatched scale.
 *
 * Measured on Maniac Mansion (8px charset) with a set baked at 20px: the ink
 * band is 16 rows at scale 2 and 21 rows at scale 3 - the box decides, not
 * the font. These tests pin the rule that reports that mismatch.
 */
class HiResScaleFitTestSuite : public CxxTest::TestSuite {
public:
	/// The ordinary case: one font, twice the game's cell.
	void test_exact_double_fits_at_two_and_nothing_else() {
		const int cells[] = { 16 };

		TS_ASSERT(Graphics::hiResCellsFitScale(cells, 1, 8, 2));
		TS_ASSERT(!Graphics::hiResCellsFitScale(cells, 1, 8, 1));
		TS_ASSERT(!Graphics::hiResCellsFitScale(cells, 1, 8, 3));
	}

	/**
	 * A set spans several charsets at several cells, so the test is whether
	 * SOME font matches - not the smallest one. The Korean MI2 set is
	 * 24,16,18,16,24 over game charsets of 12,8,9,8,12; dividing the smallest
	 * cell by the one known height pairs a font with the wrong charset and
	 * refuses a set whose every charset is exactly 2x.
	 */
	void test_some_font_matching_is_enough() {
		const int cells[] = { 24, 16, 18, 16, 24 };

		// The known game height here is 12, whose double is 24 - present,
		// even though the smallest cell (16) is not 12 times anything.
		TS_ASSERT(Graphics::hiResCellsFitScale(cells, 5, 12, 2));
		TS_ASSERT(!Graphics::hiResCellsFitScale(cells, 5, 12, 1));
	}

	/// The defect this rule exists to catch: no scale draws a 20px cell over
	/// an 8px charset correctly, because 20 is not a whole multiple of 8.
	void test_non_multiple_fits_no_scale() {
		const int cells[] = { 20 };

		for (int s = 1; s <= 3; ++s)
			TS_ASSERT(!Graphics::hiResCellsFitScale(cells, 1, 8, s));
	}

	/**
	 * "Fits" is equality, never "no larger than". A font smaller than the box
	 * would sit in it with a gap underneath - a different fault, and treating
	 * it as a fit would pick a scale that stretches nothing into place.
	 */
	void test_smaller_than_the_box_is_not_a_fit() {
		const int cells[] = { 12 };

		TS_ASSERT(!Graphics::hiResCellsFitScale(cells, 1, 8, 2));  // box 16
		TS_ASSERT(!Graphics::hiResCellsFitScale(cells, 1, 8, 3));  // box 24
	}

	/// A game with no known charset height answers nothing, rather than
	/// accepting everything or dividing by zero.
	void test_unknown_game_height_never_fits() {
		const int cells[] = { 16, 24 };

		TS_ASSERT(!Graphics::hiResCellsFitScale(cells, 2, 0, 2));
		TS_ASSERT(!Graphics::hiResCellsFitScale(cells, 2, -8, 2));
	}

	/// An empty or absent set is not a match at any scale.
	void test_empty_set_never_fits() {
		const int cells[] = { 16 };

		TS_ASSERT(!Graphics::hiResCellsFitScale(cells, 0, 8, 2));
		TS_ASSERT(!Graphics::hiResCellsFitScale(nullptr, 4, 8, 2));
	}

	/// A scale of zero or less is not a scale. Guarded because the value can
	/// come from a config key a user typed.
	void test_non_positive_scale_never_fits() {
		const int cells[] = { 16 };

		TS_ASSERT(!Graphics::hiResCellsFitScale(cells, 1, 8, 0));
		TS_ASSERT(!Graphics::hiResCellsFitScale(cells, 1, 8, -2));
	}

	/// Scale 1 is the identity: a set baked at the game's own cell fits it,
	/// which is what keeps a 1x set from being read as something larger.
	void test_scale_one_is_the_games_own_cell() {
		const int cells[] = { 8 };

		TS_ASSERT(Graphics::hiResCellsFitScale(cells, 1, 8, 1));
		TS_ASSERT(!Graphics::hiResCellsFitScale(cells, 1, 8, 2));
	}

	/**
	 * The smallest matching scale is the right answer, and this is why the
	 * caller sweeps upward. A 24px font over an 8px charset is 8x3, but over
	 * a 12px one it is 12x2 - the same file, two readings. Both are true
	 * here; the caller's loop order picks the smaller.
	 */
	void test_one_cell_can_satisfy_two_scales() {
		const int cells[] = { 24 };

		TS_ASSERT(Graphics::hiResCellsFitScale(cells, 1, 12, 2));
		TS_ASSERT(Graphics::hiResCellsFitScale(cells, 1, 8, 3));
	}
};
