#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/fs.h"
#include "common/memstream.h"
#include "graphics/hires_text/font_map.h"

#include "engines/scumm/hires_overlay.h"
#include "engines/scumm/hires_text.h"

#include "support/hires_fixture.h"

/**
 * The C31 legacy grid rule (design section 8's advance=, kHiResAdvanceEngine:
 * nothing set an advance rule for the code point): a bitmap (SVFN) face
 * steps a wide (CJK) glyph on the game's own grid; a wide TrueType glyph
 * steps by its own advance instead, unless the map asks otherwise
 * (advance=game or an advance.<spec>= rule).
 */
class ScummHiResWideAdvanceTestSuite : public CxxTest::TestSuite {
	static const int kCs = ScummHiResFixture::kCs;

	bool open(Scumm::ScummHiResText &hr, Scumm::HiResOverlay &overlay, const char *body) {
		return ScummHiResFixture::open(hr, overlay, body);
	}

public:
	void setUp() { ScummHiResFixture::setUp(); }
	void tearDown() { ScummHiResFixture::tearDown(); }

	/// breakRules(): Hangul breaks at spaces only in centred (actor speech)
	/// text with the layer on; [layout] overrides it either way.
	void test_hangul_break_follows_centring() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\n"));
		TS_ASSERT_EQUALS(hr.breakRules(true).hangul, Graphics::kHangulBreakWord);
		TS_ASSERT_EQUALS(hr.breakRules(false).hangul, Graphics::kHangulBreakAny);

		Scumm::ScummHiResText any;
		TS_ASSERT(open(any, overlay, "[font.4]\nface=OWN.SVF\n[layout]\nhangul=any\n"));
		TS_ASSERT_EQUALS(any.breakRules(true).hangul, Graphics::kHangulBreakAny);
	}

	/// A bitmap face's wide glyph always steps on the game's grid (a floor
	/// at gameWidth), whatever advance= says - there is no "step by face"
	/// concept for a bitmap face under the engine default.
	void test_bitmap_wide_glyph_keeps_the_game_grid() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\n"));
		Common::Array<uint32> own;
		own.push_back(0xAC00);
		TS_ASSERT(ScummHiResFixture::addFace(hr, "/tmp/t/OWN.SVF", own));
		// makeFont()'s advance (9 hi-res px) at scale 2 rounds up to 5, and
		// the game's own (12) is wider, so the game's width is the floor.
		TS_ASSERT_EQUALS(hr.advanceFor(0xAC00, kCs, 12), 12);
	}

	static const char *systemTtf() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		static const char *const kFonts[] = {
			"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
			"/System/Library/Fonts/AppleSDGothicNeo.ttc",
		};
		for (uint i = 0; i < ARRAYSIZE(kFonts); ++i)
			if (Common::FSNode(kFonts[i]).exists())
				return kFonts[i];
#endif
		return nullptr;
	}

public:
	/// A wide TrueType glyph steps by its own advance by default (no
	/// advance= key at all): the C31 rule, still the default for CJK
	/// (unlike ASCII's, which changed to `game`).
	void test_wide_ttf_steps_by_face_by_default() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		const char *ttf = systemTtf();
		if (!ttf) {
			TS_SKIP("needs a TrueType face");
			return;
		}
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, true);
		Scumm::ScummHiResText hr;
		const Common::String body = Common::String::format("[font.2]\nface=%s\n", ttf);
		TS_ASSERT(open(hr, overlay, body.c_str()));
		hr.noteGameCharset(2, 16, 16);
		hr.setCharsetGrid(2, 16, 16);
		TS_ASSERT(hr.loadFonts(Common::Path()));
		// 가 (U+AC00): whatever the face's own advance rounds to, not
		// necessarily the game's 16 - the point is it need not equal it.
		const int adv = hr.advanceFor(0xAC00, 2, 16);
		TS_ASSERT_LESS_THAN(0, adv);
#else
		TS_SKIP("needs FreeType");
#endif
	}

	/// advance=game overrides the TTF default and keeps the game's own grid.
	void test_explicit_advance_game_keeps_the_cell() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		const char *ttf = systemTtf();
		if (!ttf) {
			TS_SKIP("needs a TrueType face");
			return;
		}
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, true);
		Scumm::ScummHiResText hr;
		const Common::String body = Common::String::format("[font.2]\nface=%s\nadvance=game\n", ttf);
		TS_ASSERT(open(hr, overlay, body.c_str()));
		hr.noteGameCharset(2, 16, 16);
		hr.setCharsetGrid(2, 16, 16);
		TS_ASSERT(hr.loadFonts(Common::Path()));
		TS_ASSERT_EQUALS(hr.advanceFor(0xAC00, 2, 16), 16);
#else
		TS_SKIP("needs FreeType");
#endif
	}
};
