#include <cxxtest/TestSuite.h>

#include "engines/scumm/hires_text.h"

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
};
