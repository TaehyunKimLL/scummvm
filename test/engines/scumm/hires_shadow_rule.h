#include <cxxtest/TestSuite.h>

#include "engines/scumm/hires_text.h"
#include "graphics/hires_text/glyph_renderer.h"

/**
 * How [shadow] mode=game turns the game's shadow byte into a decoration (C18).
 *
 * A kor-trs korean%02d.fnt carries its shadow in byte 1: 1 none, 2 a drop to
 * the south-east, 3 stroke, and anything else - 0 in practice, the MI2
 * dialogue font korean02.fnt - the 8-direction outline the patch's own
 * renderer (drawBits1Kor) draws. The hi-res layer mapped 0 to "none", so MI2's
 * Korean dialogue lost its black outline.
 *
 * 0 means two different things, though. In a game that never loads a Korean
 * patch font, _2byteShadow is still its initial 0, and outlining there wrapped
 * English replacement text in shadowColor - black on black (3bd719542f). So 0
 * is an outline only when the Korean patch fonts are what set it.
 */
class HiResShadowRuleTestSuite : public CxxTest::TestSuite {
	static Graphics::HiResShadowMode game(int shadow, bool korPatch) {
		return Scumm::ScummHiResText::resolveShadow(Graphics::kHiResShadowGame, shadow, korPatch);
	}

public:
	void test_patch_font_zero_is_the_outline() {
		TS_ASSERT_EQUALS(game(0, true), Graphics::kHiResShadowOutline);
	}

	void test_unset_zero_draws_nothing() {
		// 3bd719542f's guarantee: no Korean patch font, no decoration.
		TS_ASSERT_EQUALS(game(0, false), Graphics::kHiResShadowNone);
	}

	void test_the_other_bytes_are_unchanged() {
		for (int k = 0; k < 2; ++k) {
			const bool kor = (k == 1);
			TS_ASSERT_EQUALS(game(1, kor), Graphics::kHiResShadowNone);
			TS_ASSERT_EQUALS(game(2, kor), Graphics::kHiResShadowDrop);
			TS_ASSERT_EQUALS(game(3, kor), Graphics::kHiResShadowStroke);
			TS_ASSERT_EQUALS(game(4, kor), Graphics::kHiResShadowOutline);
		}
	}

	void test_the_map_wins_over_the_game() {
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::resolveShadow(Graphics::kHiResShadowNone, 0, true),
						 Graphics::kHiResShadowNone);
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::resolveShadow(Graphics::kHiResShadowDrop, 3, false),
						 Graphics::kHiResShadowDrop);
	}

	// --- What each byte draws, at MI2's 2x --------------------------

	static Graphics::GlyphDecoration drawn(int shadow, bool korPatch, int scale = 2) {
		Graphics::HiResMap map;
		const Graphics::GlyphStyle style =
			Scumm::ScummHiResText::glyphStyle(map, 0, scale, shadow, korPatch, 15, 0);
		TS_ASSERT_EQUALS(style.color, 15);
		TS_ASSERT_EQUALS(style.shadowColor, 0);
		return Graphics::HiResGlyphRenderer::decorationFor(style);
	}

	/// 0 from a patch font (C18) and 4 and up: a round outline, 1.5 px at 2x.
	void test_outline_bytes_draw_a_round_antialiased_outline() {
		const int bytes[] = { 0, 4, 7 };
		for (int i = 0; i < 3; ++i) {
			const Graphics::GlyphDecoration d = drawn(bytes[i], true);
			TS_ASSERT(d.outline);
			TS_ASSERT_EQUALS(d.outlineQ, 6);
			TS_ASSERT_EQUALS(d.shape, Graphics::kHiResOutlineRound);
			TS_ASSERT(!d.shadow);
		}
		// Width follows the scale: 0.75 of a game pixel.
		TS_ASSERT_EQUALS(drawn(0, true, 3).outlineQ, 9);
	}

	/// 0 and 1 with no patch font draw nothing, as before.
	void test_none_bytes_draw_nothing() {
		Graphics::GlyphDecoration d = drawn(0, false);
		TS_ASSERT(!d.outline);
		TS_ASSERT(!d.shadow);
		d = drawn(1, true);
		TS_ASSERT(!d.outline);
		TS_ASSERT(!d.shadow);
	}

	/// 2: a drop of the glyph itself, one game pixel down and right.
	void test_byte_2_is_a_drop_of_a_game_pixel() {
		Graphics::GlyphDecoration d = drawn(2, true);
		TS_ASSERT(!d.outline);
		TS_ASSERT(d.shadow);
		TS_ASSERT_EQUALS(d.shadowDx, 2);
		TS_ASSERT_EQUALS(d.shadowDy, 2);
		d = drawn(2, true, 3);
		TS_ASSERT_EQUALS(d.shadowDx, 3);
	}

	/**
	 * 3: the outline plus a copy of it one game pixel to the lower left,
	 * in place of the eleven-offset stroke table.
	 */
	void test_byte_3_is_an_outline_and_its_lower_left_shadow() {
		const Graphics::GlyphDecoration d = drawn(3, true);
		TS_ASSERT(d.outline);
		TS_ASSERT_EQUALS(d.outlineQ, 6);
		TS_ASSERT(d.shadow);
		TS_ASSERT_EQUALS(d.shadowDx, -2);
		TS_ASSERT_EQUALS(d.shadowDy, 2);
		TS_ASSERT_EQUALS(d.shadowColor, 0);
	}

	/// A map's own colour and geometry still win.
	void test_the_map_geometry_applies_to_the_game_modes() {
		Graphics::HiResMap map;
		map.shadow.color = 8;
		map.shadow.colorSet = true;
		map.shadow.widthQ = 4;
		map.shadow.style = Graphics::kHiResOutlineLegacy;
		const Graphics::GlyphStyle style = Scumm::ScummHiResText::glyphStyle(map, 0, 2, 0, true, 15, 0);
		TS_ASSERT_EQUALS(style.shadowColor, 8);
		const Graphics::GlyphDecoration d = Graphics::HiResGlyphRenderer::decorationFor(style);
		TS_ASSERT(d.outline);
		TS_ASSERT_EQUALS(d.shape, Graphics::kHiResOutlineLegacy);
		TS_ASSERT_EQUALS(d.legacyTable, Graphics::kHiResShadowOutline);
	}

	/**
	 * The game pixels a decorated glyph touches (C19): the overlay
	 * area divided by the scale, rounded outwards, negative coordinates
	 * included - an outline can start left of or above the surface.
	 */
	void test_game_rect_holds_the_whole_decoration() {
		// A 2x glyph at game cell (10,5)-(19,14) with a 1.5 px outline and a
		// stroke shadow reaching three output pixels left.
		const Common::Rect r = Scumm::ScummHiResText::gameRectFor(Common::Rect(17, 8, 41, 31), 2);
		TS_ASSERT_EQUALS(r, Common::Rect(8, 4, 21, 16));
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::gameRectFor(Common::Rect(-3, -1, 4, 5), 2),
						 Common::Rect(-2, -1, 2, 3));
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::gameRectFor(Common::Rect(5, 5, 9, 9), 1),
						 Common::Rect(5, 5, 9, 9));
		TS_ASSERT(Scumm::ScummHiResText::gameRectFor(Common::Rect(), 2).isEmpty());
	}

	void test_each_charset_takes_its_own_shadow_section() {
		Graphics::HiResMap map;
		map.shadow.mode = Graphics::kHiResShadowOutline;
		map.shadow.modeSet = true;
		Graphics::HiResShadowSpec cs3;
		cs3.widthQ = 12; cs3.widthSet = true;            // 3 px
		map.shadowIds[3] = cs3;
		Graphics::HiResShadowSpec cs6;
		cs6.mode = Graphics::kHiResShadowDrop; cs6.modeSet = true;
		cs6.offset = 4; cs6.offsetSet = true;
		map.shadowIds[6] = cs6;

		const Graphics::GlyphStyle s3 = Scumm::ScummHiResText::glyphStyle(map, 3, 2, 0, false, 15, 0);
		TS_ASSERT_EQUALS(s3.shadowMode, Graphics::kHiResShadowOutline);
		TS_ASSERT_EQUALS(s3.outlineQ, 12);
		const Graphics::GlyphStyle s6 = Scumm::ScummHiResText::glyphStyle(map, 6, 2, 0, false, 15, 0);
		TS_ASSERT_EQUALS(s6.shadowMode, Graphics::kHiResShadowDrop);
		TS_ASSERT_EQUALS(s6.shadowOffset, 4);
		const Graphics::GlyphStyle s1 = Scumm::ScummHiResText::glyphStyle(map, 1, 2, 0, false, 15, 0);
		TS_ASSERT_EQUALS(s1.outlineQ, 6);                // the scale's 0.75 game px
		TS_ASSERT_EQUALS(s1.shadowOffset, 2);              // the scale's 1 game px
	}

	void test_a_map_without_shadow_ids_draws_as_before() {
		Graphics::HiResMap map;
		for (int id = 0; id < 9; ++id) {
			const Graphics::GlyphStyle a = Scumm::ScummHiResText::glyphStyle(map, id, 2, 0, true, 15, 0);
			Graphics::GlyphStyle b;
			b.color = 15;
			b.shadowColor = 0;
			b.shadowMode = Scumm::ScummHiResText::resolveShadow(map.shadow.mode, 0, true);
			Graphics::HiResGlyphRenderer::applyShadow(b, map.shadow, 2);
			TS_ASSERT_EQUALS(a.shadowMode, b.shadowMode);
			TS_ASSERT_EQUALS(a.outlineQ, b.outlineQ);
			TS_ASSERT_EQUALS(a.shadowOffset, b.shadowOffset);
		}
	}
};
