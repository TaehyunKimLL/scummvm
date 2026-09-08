#include <cxxtest/TestSuite.h>

#include "engines/scumm/hires_text.h"

/**
 * The whole-multiple scale rule for a map-less font set.
 *
 * A map-less set names no scale, so it is read off the fonts: a set is
 * accepted only when some font in it is exactly the game's own cell times an
 * integer. The consequences of getting this wrong are not subtle - a set
 * refused is a set the player never sees, and a set accepted at the wrong
 * multiple draws every glyph at the wrong size on a grid built for another.
 *
 * Reaching the rule through resolveScale() needs a directory of real .fnt
 * files and a launched game, which is why it went untested while the harness
 * grew around it. It is a pure function over the cell array, so it does not
 * need any of that.
 */
class HiResScaleRuleTestSuite : public CxxTest::TestSuite {
public:
	/**
	 * The set is measured font by font, not by its smallest cell.
	 *
	 * The Korean MI2 set is 24, 16, 18, 16, 24 against game charsets of
	 * 12, 8, 9, 8, 12 - every charset exactly 2x. Dividing the smallest cell
	 * (16) by the one game height that is known at this point (12) pairs a
	 * font with the wrong charset and would refuse the whole set.
	 */
	void test_some_font_matching_is_enough() {
		static const int kMi2[] = { 24, 16, 18, 16, 24 };

		TS_ASSERT(Scumm::ScummHiResText::cellMatchesScale(kMi2, 5, 12, 2));
		TS_ASSERT(Scumm::ScummHiResText::cellMatchesScale(kMi2, 5, 8, 2));
		TS_ASSERT(Scumm::ScummHiResText::cellMatchesScale(kMi2, 5, 9, 2));

		// And a height no font in the set is a multiple of is refused.
		TS_ASSERT(!Scumm::ScummHiResText::cellMatchesScale(kMi2, 5, 7, 2));
		TS_ASSERT(!Scumm::ScummHiResText::cellMatchesScale(kMi2, 5, 10, 2));
	}

	/**
	 * A cell that is not a whole multiple is refused at every scale.
	 *
	 * 18px and 30px over an 8px game font are 2.25x and 3.75x. The surface
	 * can only be enlarged by an integer, so there is no scale that draws
	 * them correctly - rounding is what this rule exists to prevent. Both
	 * are real cells from the baked MI2 Latin set.
	 */
	void test_a_fractional_cell_matches_no_scale() {
		static const int kEighteen[] = { 18 };
		static const int kThirty[] = { 30 };

		for (int s = 1; s <= 3; ++s) {
			TS_ASSERT(!Scumm::ScummHiResText::cellMatchesScale(kEighteen, 1, 8, s));
			TS_ASSERT(!Scumm::ScummHiResText::cellMatchesScale(kThirty, 1, 8, s));
		}

		// 16 over 8 is exactly 2x, and only 2x.
		static const int kSixteen[] = { 16 };
		TS_ASSERT(!Scumm::ScummHiResText::cellMatchesScale(kSixteen, 1, 8, 1));
		TS_ASSERT(Scumm::ScummHiResText::cellMatchesScale(kSixteen, 1, 8, 2));
		TS_ASSERT(!Scumm::ScummHiResText::cellMatchesScale(kSixteen, 1, 8, 3));
	}

	/**
	 * One cell can satisfy two scales, which is why the caller asks in
	 * ascending order.
	 *
	 * The 24px font of a 12px charset is also 8x3. Reading it as 3x draws
	 * every glyph half again too large, so the smallest multiple any font
	 * matches is the one the set was baked at - a property of the caller's
	 * loop, but only sound because this function answers both questions
	 * truthfully.
	 */
	void test_one_cell_can_answer_to_more_than_one_scale() {
		static const int kCells[] = { 24 };

		TS_ASSERT(Scumm::ScummHiResText::cellMatchesScale(kCells, 1, 12, 2));
		TS_ASSERT(Scumm::ScummHiResText::cellMatchesScale(kCells, 1, 8, 3));
	}

	/**
	 * Nothing to measure is not a match.
	 *
	 * A European game has no CJK font, so the height arrives as 0, and a set
	 * that failed to probe has no cells. Either one multiplied out would make
	 * 0 == 0 true and accept an unmeasured set at every scale.
	 */
	void test_nothing_measured_is_not_a_match() {
		static const int kCells[] = { 16 };

		TS_ASSERT(!Scumm::ScummHiResText::cellMatchesScale(kCells, 1, 0, 2));
		TS_ASSERT(!Scumm::ScummHiResText::cellMatchesScale(kCells, 0, 8, 2));
		TS_ASSERT(!Scumm::ScummHiResText::cellMatchesScale(nullptr, 1, 8, 2));
		TS_ASSERT(!Scumm::ScummHiResText::cellMatchesScale(kCells, 1, 8, 0));

		// A zero cell in the array must not match a zero-height game font
		// either - both are "unknown", not "equal".
		static const int kZero[] = { 0 };
		TS_ASSERT(!Scumm::ScummHiResText::cellMatchesScale(kZero, 1, 0, 2));
	}
};
