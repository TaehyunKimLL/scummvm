#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/rect.h"

#include "engines/scumm/hires_text.h"

/**
 * Text on a single-buffered virtual screen (the verb area, where MI1
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

	/// Several glyphs: only those whose cell the paint meets go.
	void test_several_glyphs_only_the_painted_cells_go() {
		typedef Scumm::ScummHiResText::TracedGlyph G;
		Common::Array<G> glyphs;
		G a, b, c;
		a.cell = Common::Rect(0, 0, 16, 16);  a.area = Common::Rect(-2, -2, 19, 19);
		b.cell = Common::Rect(16, 0, 32, 16); b.area = Common::Rect(14, -2, 35, 19);
		c.cell = Common::Rect(48, 0, 64, 16); c.area = Common::Rect(46, -2, 67, 19);
		glyphs.push_back(a);
		glyphs.push_back(b);
		glyphs.push_back(c);

		Common::Array<Common::Rect> clear;
		Scumm::ScummHiResText::retireTracedGlyphs(Common::Rect(0, 0, 32, 16), glyphs, clear);
		TS_ASSERT_EQUALS(glyphs.size(), 1u);
		TS_ASSERT_EQUALS(glyphs[0].cell, c.cell);
		TS_ASSERT_EQUALS(clear.size(), 3u);
		TS_ASSERT_EQUALS(clear[1], a.area);
		TS_ASSERT_EQUALS(clear[2], b.area);
	}

	/// A glyph inside the painted rect adds nothing to clear: the rect has it.
	void test_glyph_inside_the_paint_adds_no_rect() {
		Common::Array<Scumm::ScummHiResText::TracedGlyph> glyphs;
		Scumm::ScummHiResText::TracedGlyph g;
		g.cell = Common::Rect(10, 10, 20, 20);
		g.area = Common::Rect(9, 9, 21, 21);
		glyphs.push_back(g);

		Common::Array<Common::Rect> clear;
		Scumm::ScummHiResText::retireTracedGlyphs(Common::Rect(0, 0, 40, 40), glyphs, clear);
		TS_ASSERT(glyphs.empty());
		TS_ASSERT_EQUALS(clear.size(), 1u);
	}

	/**
	 * The main screen: only text the game drew for keeps goes,
	 * whole, by its cell; the painted area itself is not cleared (removable
	 * text is the charset's); a glyph also drawn into the back buffer stays,
	 * because the game blits that copy back; a neighbour touched only by its
	 * outline stays.
	 */
	void test_main_screen_retires_front_kept_glyphs_by_cell() {
		typedef Scumm::ScummHiResText::TracedGlyph G;
		Common::Array<G> glyphs;
		G front, back, neighbour;
		front.cell = Common::Rect(10, 10, 20, 20);     front.area = Common::Rect(9, 9, 21, 21);
		back.cell = Common::Rect(20, 10, 30, 20);      back.area = Common::Rect(19, 9, 31, 21);
		back.inBackBuffer = true;
		neighbour.cell = Common::Rect(10, 40, 20, 50); neighbour.area = Common::Rect(9, 38, 21, 51);
		glyphs.push_back(front);
		glyphs.push_back(back);
		glyphs.push_back(neighbour);

		Common::Array<Common::Rect> clear;
		Scumm::ScummHiResText::retireGlyphsByCell(Common::Rect(0, 0, 40, 40), glyphs, clear, false, true);
		TS_ASSERT_EQUALS(clear.size(), 1u);
		TS_ASSERT_EQUALS(clear[0], front.area); // whole, though inside the paint
		TS_ASSERT_EQUALS(glyphs.size(), 2u);
		TS_ASSERT_EQUALS(glyphs[0].cell, back.cell);
		TS_ASSERT_EQUALS(glyphs[1].cell, neighbour.cell);
	}
};
