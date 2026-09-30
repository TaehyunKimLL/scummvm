#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/fs.h"
#include "common/memstream.h"
#include "graphics/hires_text/font_map.h"

#include "engines/scumm/hires_overlay.h"
#include "engines/scumm/hires_text.h"

#include "support/hires_fixture.h"

/**
 * latinStepsByFace() (design section 8's advance=): true exactly when
 * advanceFor()'s own resolution would draw the ASCII character from a
 * TrueType face and step by that face's own advance (advance.basic-
 * latin=font). Since Task 7 SCUMM's engine-scope default is advance.basic-
 * latin=game (design section 8's table): a TrueType face no longer steps
 * Latin by itself unless the map (or hires_text_advance) asks for it - the
 * inverse of C34/C36's old default.
 */
class ScummHiResLatinAdvanceTestSuite : public CxxTest::TestSuite {
	static const int kCs = ScummHiResFixture::kCs;

	bool open(Scumm::ScummHiResText &hr, Scumm::HiResOverlay &overlay, const char *body) {
		return ScummHiResFixture::open(hr, overlay, body);
	}

	static const char *systemTtf() {
		static const char *const kFonts[] = {
			"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
			"/System/Library/Fonts/AppleSDGothicNeo.ttc",
		};
		for (uint i = 0; i < ARRAYSIZE(kFonts); ++i)
			if (Common::FSNode(kFonts[i]).exists())
				return kFonts[i];
		return nullptr;
	}

public:
	void setUp() { ScummHiResFixture::setUp(); }
	void tearDown() { ScummHiResFixture::tearDown(); }

	/// The layer off: no map, no change.
	void test_layer_off() {
		Scumm::ScummHiResText hr;
		TS_ASSERT(!hr.latinStepsByFace('A', kCs));
	}

	/// An SVF (bitmap) face never steps Latin by itself: only a TrueType
	/// face's own metrics can (latinStepsByFace() checks the source type).
	void test_bitmap_face_never_steps_by_itself() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\nadvance.basic-latin=font\n"));
		Common::Array<uint32> own;
		own.push_back('A');
		TS_ASSERT(ScummHiResFixture::addFace(hr, "/tmp/t/OWN.SVF", own));
		TS_ASSERT(!hr.latinStepsByFace('A', kCs));
	}

	/// advance.basic-latin=font on a TrueType face steps Latin by it.
	void test_ttf_with_advance_font_steps_by_face() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		const char *ttf = systemTtf();
		if (!ttf) {
			TS_SKIP("needs a TrueType face");
			return;
		}
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, true);
		Scumm::ScummHiResText hr;
		const Common::String body = Common::String::format(
			"[font.2]\nface=%s\nadvance.basic-latin=font\n", ttf);
		TS_ASSERT(open(hr, overlay, body.c_str()));
		hr.noteGameCharset(2, 9, 9);
		hr.setCharsetGrid(2, 9, 9);
		TS_ASSERT(hr.loadFonts(Common::Path()));
		TS_ASSERT(hr.latinStepsByFace('A', 2));
		TS_ASSERT_LESS_THAN(0, hr.advanceFor('A', 2, 5));
#elif defined(USE_FREETYPE2)
		TS_SKIP("needs the null OSystem");
#else
		TS_SKIP("needs FreeType");
#endif
	}

	/// The engine default (advance.basic-latin=game, no key at all) does not
	/// step Latin by the face even though one is loaded - the inverse of the
	/// pre-Task-7 default.
	void test_ttf_without_advance_key_keeps_the_game_width() {
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
		hr.noteGameCharset(2, 9, 9);
		hr.setCharsetGrid(2, 9, 9);
		TS_ASSERT(hr.loadFonts(Common::Path()));
		TS_ASSERT(!hr.latinStepsByFace('A', 2));
		TS_ASSERT_EQUALS(hr.advanceFor('A', 2, 5), 5);
#elif defined(USE_FREETYPE2)
		TS_SKIP("needs the null OSystem");
#else
		TS_SKIP("needs FreeType");
#endif
	}
};
