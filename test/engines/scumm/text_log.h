#include <cxxtest/TestSuite.h>

#include "engines/scumm/hires_text.h"
#include "engines/scumm/text_log.h"

/**
 * The wording of the debug socket's text log: which face draws a string and
 * what decoration it gets, from a map's [shadow] / [shadow.N] and the game's
 * shadow byte. A harness reads these to see why a string looks as it does,
 * so they must name what the renderer draws (HiResGlyphRenderer::decorationFor()).
 */
class ScummTextLogTestSuite : public CxxTest::TestSuite {
	static Common::String deco(const Graphics::HiResMap &map, int charset, int gameShadow, bool kor) {
		const Graphics::GlyphStyle style =
			Scumm::ScummHiResText::glyphStyle(map, charset, 2, gameShadow, kor, 15, 0);
		return Scumm::describeDecoration(map.shadowFor(charset), map.shadowIds.contains(charset), charset, style,
		                                 gameShadow, kor);
	}

public:
	void test_face() {
		TS_ASSERT_EQUALS(Scumm::describeFace("KP12AB34.SVF", 54, 54, 2), "KP12AB34.SVF (54x54, 2bpp)");
	}

	void test_quarter_pixels() {
		TS_ASSERT_EQUALS(Scumm::quarterPixels(6), "1.5");
		TS_ASSERT_EQUALS(Scumm::quarterPixels(4), "1");
		TS_ASSERT_EQUALS(Scumm::quarterPixels(3), "0.75");
		TS_ASSERT_EQUALS(Scumm::quarterPixels(1), "0.25");
	}

	void test_game_mode_outline_from_patch_font() {
		Graphics::HiResMap map;
		TS_ASSERT_EQUALS(deco(map, 3, 0, true),
		                 "outline width 1.5 color game (from [shadow] mode=game, game shadow byte 0, Korean patch font)");
	}

	void test_game_mode_zero_without_patch_is_none() {
		Graphics::HiResMap map;
		TS_ASSERT_EQUALS(deco(map, 3, 0, false),
		                 "none (from [shadow] mode=game, game shadow byte 0, no Korean patch font)");
	}

	void test_shadow_n_drop_offset() {
		Graphics::HiResMap map;
		Graphics::HiResShadowSpec n;
		n.mode = Graphics::kHiResShadowDrop;
		n.modeSet = true;
		n.offset = 4;
		n.offsetSet = true;
		map.shadowIds[3] = n;
		TS_ASSERT_EQUALS(deco(map, 3, 1, false), "drop offset 4 color game (from [shadow.3] mode=drop)");
		// Another charset does not see [shadow.3].
		TS_ASSERT_EQUALS(deco(map, 2, 1, false),
		                 "none (from [shadow] mode=game, game shadow byte 1)");
	}

	void test_outline_width_and_color() {
		Graphics::HiResMap map;
		map.shadow.mode = Graphics::kHiResShadowOutline;
		map.shadow.modeSet = true;
		map.shadow.widthQ = 6;
		map.shadow.widthSet = true;
		map.shadow.color = 8;
		map.shadow.colorSet = true;
		TS_ASSERT_EQUALS(deco(map, 1, 2, false), "outline width 1.5 color 8 (from [shadow] mode=outline)");
	}

	void test_shadow_shift() {
		Graphics::HiResMap map;
		map.shadow.mode = Graphics::kHiResShadowOutline;
		map.shadow.widthQ = 4;
		map.shadow.shiftSet = true;
		map.shadow.dx = -1;
		map.shadow.dy = 1;
		TS_ASSERT_EQUALS(deco(map, 1, 2, false),
		                 "outline width 1 shadow (-1,1) color game (from [shadow] mode=outline)");
	}

	void test_game_font() {
		TS_ASSERT_EQUALS(Scumm::describeGameDecoration(2), "game's own (game shadow byte 2)");
		TS_ASSERT_EQUALS(Scumm::textLogLine(7, 3, "game font", "x", "hi"),
		                 "loop 7, charset 3, game font, decoration x, text 'hi'");
	}
};
