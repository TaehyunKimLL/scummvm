#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/fs.h"
#include "common/memstream.h"
#include "common/str.h"
#include "graphics/hires_text/font_map.h"

#include "engines/scumm/charset.h"
#include "engines/scumm/hires_overlay.h"
#include "engines/scumm/hires_text.h"

#include "support/hires_fixture.h"

/**
 * origin=face (design section 8, replacing the old [latin] baseline=face):
 * a glyph drawn from a bitmap (SVFN) face sits on the baseline baked into
 * the face, rather than under the game glyph's own offsets.
 *
 * A bitmap face's glyph carries its own baseline. printChar() still added
 * the game glyph's offsY on top of it, which is right for a game font whose
 * glyphs are cut to their ink (Monkey Island 2's card fonts: the offsY of a
 * period or a lowercase letter is large) and wrong for the face that
 * replaces it, whose glyph is on a full cell. A TrueType face already drops
 * the game's offsets (latinStepsByFace); origin=face asks the same of a
 * bitmap face, and only when the map says so.
 *
 * Since Task 7 origin=face applies to any code point through a plan
 * (design section 6.2), not only ASCII: latinBaselineByFace() is a thin
 * wrapper over HiResIdPlan::originFor() of the code point actually drawn -
 * so a remap's own origin rule applies too (a deliberate change from the
 * old exclusion of remapped codes from the Latin rules).
 */
class ScummHiResOriginTestSuite : public CxxTest::TestSuite {
	static const int kCs = ScummHiResFixture::kCs;

	bool open(Scumm::ScummHiResText &hr, Scumm::HiResOverlay &overlay, const char *body) {
		return ScummHiResFixture::open(hr, overlay, body);
	}

	bool addOwn(Scumm::ScummHiResText &hr, const Common::Array<uint32> &cps) {
		return ScummHiResFixture::addFace(hr, "/tmp/t/OWN.SVF", cps);
	}

public:
	void setUp() { ScummHiResFixture::setUp(); }
	void tearDown() { ScummHiResFixture::tearDown(); }

	static Common::Array<uint32> ownGlyphs() {
		Common::Array<uint32> cps;
		cps.push_back('A');
		cps.push_back('.');
		return cps;
	}

	/// With origin.basic-latin=face, a letter and a period the face has a
	/// glyph for are placed by it.
	void test_key_puts_bitmap_latin_on_the_face_baseline() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\norigin.basic-latin=face\n"));
		TS_ASSERT(addOwn(hr, ownGlyphs()));
		TS_ASSERT(hr.latinBaselineByFace('A', kCs));
		TS_ASSERT(hr.latinBaselineByFace('.', kCs));
	}

	/// Without the key (the engine default, origin=game) nothing changes.
	void test_without_the_key_the_game_offsets_stand() {
		const char *const bodies[] = {
			"[font.4]\nface=OWN.SVF\n",
			"[font.4]\nface=OWN.SVF\norigin.basic-latin=game\n",
		};
		for (uint i = 0; i < ARRAYSIZE(bodies); ++i) {
			Scumm::HiResOverlay overlay;
			overlay.create(64, 40, false);
			Scumm::ScummHiResText hr;
			TS_ASSERT(open(hr, overlay, bodies[i]));
			TS_ASSERT(addOwn(hr, ownGlyphs()));
			TS_ASSERT(!hr.latinBaselineByFace('A', kCs));
			TS_ASSERT(!hr.latinBaselineByFace('.', kCs));
		}
	}

	/// Only what the face draws: a character it has no glyph for, a code
	/// past ASCII, a control code, and a glyph the map keeps for the game
	/// are not placed by it. A charset with no face of its own has nothing
	/// to ask either.
	void test_only_glyphs_the_face_draws() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay,
					   "[font.4]\nface=OWN.SVF\norigin.basic-latin=face\n[glyphs]\n0x2e = original\n"));
		TS_ASSERT(addOwn(hr, ownGlyphs()));
		TS_ASSERT(hr.latinBaselineByFace('A', kCs));
		TS_ASSERT(!hr.latinBaselineByFace('.', kCs));   // kept (original)
		TS_ASSERT(!hr.latinBaselineByFace('B', kCs));   // no glyph
		TS_ASSERT(!hr.latinBaselineByFace(' ', kCs));
		TS_ASSERT(!hr.latinBaselineByFace(0x80, kCs));
		TS_ASSERT(!hr.latinBaselineByFace(0x1f, kCs));
		TS_ASSERT(!hr.latinBaselineByFace('A', 2)); // a charset with no face of its own
	}

	/// A remap's own origin rule now applies to the code point it draws
	/// (design 6.5 step 2): with origin.general-punctuation=face, the
	/// remapped ellipsis sits on the face baseline - the inverse of the
	/// old exclusion of remapped codes from the Latin rules.
	void test_remap_now_follows_its_own_origin_rule() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay,
					   "[font.4]\nface=OWN.SVF\norigin.general-punctuation=face\n"
					   "[glyphs]\n0x5e = u+2026\n"));
		Common::Array<uint32> cps = ownGlyphs();
		cps.push_back(0x2026);
		TS_ASSERT(addOwn(hr, cps));
		TS_ASSERT(hr.latinBaselineByFace(0x5e, kCs));
		// A code the rule does not cover (basic-latin, no origin key here) stands.
		TS_ASSERT(!hr.latinBaselineByFace('A', kCs));
	}

	/// A renderer that measures Latin with the game's widths and never asks
	/// the layer (FM-Towns, V2) switches the face step off; it ignores this
	/// key too.
	void test_renderer_that_does_not_ask_the_layer_ignores_the_key() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\norigin.basic-latin=face\n"));
		TS_ASSERT(addOwn(hr, ownGlyphs()));
		TS_ASSERT(hr.latinBaselineByFace('A', kCs));
		hr.setLatinFaceStepAllowed(false);
		TS_ASSERT(!hr.latinBaselineByFace('A', kCs));
		TS_ASSERT(!hr.latinBaselineByFace('.', kCs));
		hr.setLatinFaceStepAllowed(true);
		TS_ASSERT(hr.latinBaselineByFace('A', kCs));
	}

	/// A TrueType face has its own rule (latinStepsByFace): origin=face is
	/// about bitmap faces and does not apply to it.
	void test_truetype_face_is_unaffected() {
#ifdef USE_FREETYPE2
#if NULL_OSYSTEM_IS_AVAILABLE
		static const char *const kFonts[] = {
			"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
			"/System/Library/Fonts/AppleSDGothicNeo.ttc",
		};
		const char *ttf = nullptr;
		for (uint i = 0; i < ARRAYSIZE(kFonts) && !ttf; ++i)
			if (Common::FSNode(kFonts[i]).exists())
				ttf = kFonts[i];
		if (!ttf) {
			TS_SKIP("needs a TrueType face");
			return;
		}
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, true);
		Scumm::ScummHiResText hr;
		const Common::String body = Common::String::format(
			"[font.2]\nface=%s\nadvance.basic-latin=font\norigin.basic-latin=face\n", ttf);
		TS_ASSERT(open(hr, overlay, body.c_str()));
		hr.noteGameCharset(2, 9, 9);
		hr.setCharsetGrid(2, 9, 9);
		TS_ASSERT(hr.loadFonts(Common::Path()));
		TS_ASSERT(hr.latinStepsByFace('A', 2));
		TS_ASSERT(!hr.latinBaselineByFace('A', 2));
#else
		TS_SKIP("needs the null OSystem");
#endif
#else
		TS_SKIP("needs FreeType");
#endif
	}

	/// What printChar() does with the game glyph's offsets: unchanged since
	/// before Task 7 (a pure function of two booleans and a line offset).
	void test_glyph_offsets() {
		int x = 3, y = 9;		// a trimmed card font's '.'
		Scumm::CharsetRendererClassic::latinGlyphOffsets(false, true, 4, x, y);
		TS_ASSERT_EQUALS(x, 0);
		TS_ASSERT_EQUALS(y, 0);

		x = -1; y = 1;
		Scumm::CharsetRendererClassic::latinGlyphOffsets(true, false, 4, x, y);
		TS_ASSERT_EQUALS(x, 0);
		TS_ASSERT_EQUALS(y, 4);

		x = -1; y = 1;
		Scumm::CharsetRendererClassic::latinGlyphOffsets(false, false, 4, x, y);
		TS_ASSERT_EQUALS(x, -1);
		TS_ASSERT_EQUALS(y, 1);

		x = -1; y = 1;
		Scumm::CharsetRendererClassic::latinGlyphOffsets(true, true, 4, x, y);
		TS_ASSERT_EQUALS(x, 0);
		TS_ASSERT_EQUALS(y, 4);
	}
};
