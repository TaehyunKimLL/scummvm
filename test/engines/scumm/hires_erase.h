#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/rect.h"

#include "engines/scumm/hires_text.h"

/**
 * C32: text on a single-buffered virtual screen (the verb area, where MI1
 * puts its dialogue choices and sentence line) is drawn by the game into the
 * screen's own buffer, and erased when the game paints that buffer over -
 * restoreVerbBG(), a box fill. Hi-res text lives on the overlay instead, so
 * the paint has to reach the overlay too, or every verb and choice ever shown
 * piles up there.
 */
class ScummHiResEraseTestSuite : public CxxTest::TestSuite {
public:
	/// A game rect of a virtual screen, in the overlay's pixels.
	void test_overlay_rect_scales_and_offsets_by_the_screen_top() {
		// MI1's verb screen starts at row 144; a verb at (8,11)-(35,22) in it.
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::overlayRectFor(Common::Rect(8, 11, 35, 22), 144, 2),
						 Common::Rect(16, 310, 70, 332));
		// A scrolled main screen: the offset is topline minus _screenTop.
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::overlayRectFor(Common::Rect(0, 20, 10, 30), -16, 2),
						 Common::Rect(0, 8, 20, 28));
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::overlayRectFor(Common::Rect(1, 2, 3, 4), 0, 1),
						 Common::Rect(1, 2, 3, 4));
		TS_ASSERT(Scumm::ScummHiResText::overlayRectFor(Common::Rect(), 144, 2).isEmpty());
	}

	/// The painted area goes, and so does all of every glyph the game erased.
	void test_painted_glyph_goes_with_its_decoration() {
		Common::Array<Scumm::ScummHiResText::TracedGlyph> glyphs;
		Scumm::ScummHiResText::TracedGlyph g;
		g.cell = Common::Rect(16, 310, 32, 326);
		g.area = Common::Rect(14, 308, 35, 329); // outline beyond the cell
		glyphs.push_back(g);

		Common::Array<Common::Rect> clear;
		Scumm::ScummHiResText::retireTracedGlyphs(Common::Rect(16, 310, 70, 332), glyphs, clear);
		TS_ASSERT(glyphs.empty());
		TS_ASSERT_EQUALS(clear.size(), 2u);
		TS_ASSERT_EQUALS(clear[0], Common::Rect(16, 310, 70, 332));
		TS_ASSERT_EQUALS(clear[1], Common::Rect(14, 308, 35, 329));
	}

	/**
	 * A neighbour whose outline reaches into the painted area was not erased
	 * by the game: its cell is outside, so it stays whole (only the overlap,
	 * which the game painted too, goes with the painted area).
	 */
	void test_neighbour_touching_only_by_decoration_stays() {
		Common::Array<Scumm::ScummHiResText::TracedGlyph> glyphs;
		Scumm::ScummHiResText::TracedGlyph above;
		above.cell = Common::Rect(16, 294, 32, 310);
		above.area = Common::Rect(14, 292, 35, 313);
		glyphs.push_back(above);

		Common::Array<Common::Rect> clear;
		Scumm::ScummHiResText::retireTracedGlyphs(Common::Rect(16, 310, 70, 332), glyphs, clear);
		TS_ASSERT_EQUALS(glyphs.size(), 1u);
		TS_ASSERT_EQUALS(clear.size(), 1u);
	}

	/// Nothing painted, nothing cleared.
	void test_empty_paint_clears_nothing() {
		Common::Array<Scumm::ScummHiResText::TracedGlyph> glyphs;
		Scumm::ScummHiResText::TracedGlyph g;
		g.cell = g.area = Common::Rect(0, 0, 8, 8);
		glyphs.push_back(g);

		Common::Array<Common::Rect> clear;
		Scumm::ScummHiResText::retireTracedGlyphs(Common::Rect(), glyphs, clear);
		TS_ASSERT_EQUALS(glyphs.size(), 1u);
		TS_ASSERT(clear.empty());
	}
};
