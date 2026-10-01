#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/fs.h"
#include "common/memstream.h"
#include "common/str.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/surface.h"

#include "engines/scumm/charset.h"
#include "engines/scumm/hires_overlay.h"
#include "engines/scumm/hires_text.h"

#include "support/hires_fixture.h"

/**
 * origin=face (design section 8):
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
 * origin=face applies to any code point through a plan (design section
 * 6.2), not only ASCII: latinBaselineByFace() is a thin wrapper over
 * HiResIdPlan::originFor() of the code point actually drawn - so a remap's
 * own origin rule applies too (design 6.5 step 2).
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

	/// range.basic-latin=original + origin=face: with ASCII declined to the
	/// game's own font, origin=face has nothing to apply to - the game
	/// offsets stand, same as with no origin key at all.
	void test_latin_off_keeps_the_game_font() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay,
					   "[font.4]\nface=OWN.SVF\nrange.basic-latin=original\norigin.basic-latin=face\n"));
		TS_ASSERT(addOwn(hr, ownGlyphs()));
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

	/// The implicit ascent alignment applies to
	/// any face other than the id's own primary - a bitmap Latin companion
	/// sits on a TrueType CJK primary's baseline whatever origin= says (or
	/// does not say at all: this map sets no origin key), unlike
	/// test_key_puts_bitmap_latin_on_the_face_baseline's origin=face, which
	/// is latinBaselineByFace()'s separate, game-offset-dropping meaning.
	void test_latin_svfn_on_ttf_baseline() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
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
			"[font.4]\nface=%s\nrange.basic-latin=OWN.SVF\n", ttf);
		TS_ASSERT(open(hr, overlay, body.c_str()));
		hr.noteGameCharset(kCs, 16, 16);
		hr.setCharsetGrid(kCs, 16, 16);
		TS_ASSERT(hr.loadFonts(Common::Path()));
		TS_ASSERT(addOwn(hr, ownGlyphs()));

		// A code point outside basic-latin so it resolves to the id's own
		// (TrueType) chain, not the range.basic-latin companion - just to
		// read the primary's own baselineRow().
		uint32 cpFace = 0xE9; // e-acute: Latin-1 Supplement, DejaVuSans has it
		Graphics::UnicodeGlyphSource *ttfSrc = hr.perGlyphSourceFor(kCs, cpFace);
		uint32 cpLatin = 'A';
		Graphics::UnicodeGlyphSource *svfSrc = hr.perGlyphSourceFor(kCs, cpLatin);
		TS_ASSERT(ttfSrc);
		TS_ASSERT(svfSrc);
		if (!ttfSrc || !svfSrc)
			return;
		TS_ASSERT(ttfSrc != svfSrc);
		TS_ASSERT(ttfSrc->baselineRow() >= 0);
		TS_ASSERT(svfSrc->baselineRow() >= 0);
		const int expectedShift = ttfSrc->baselineRow() - svfSrc->baselineRow();

		Scumm::ScummHiResText plain;
		TS_ASSERT(open(plain, overlay, "[font.4]\nface=OWN.SVF\n"));
		TS_ASSERT(addOwn(plain, ownGlyphs()));

		Graphics::Surface d1, d2;
		d1.create(64, 40, Graphics::PixelFormat::createFormatCLUT8());
		d2.create(64, 40, Graphics::PixelFormat::createFormatCLUT8());
		memset(d1.getPixels(), 0, d1.pitch * d1.h);
		memset(d2.getPixels(), 0, d2.pitch * d2.h);
		TS_ASSERT(hr.drawChar(d1, 'A', kCs, 10, 10, 15, 0, 1));
		TS_ASSERT(plain.drawChar(d2, 'A', kCs, 10, 10, 15, 0, 1));

		int row1 = -1, row2 = -1;
		for (int y = 0; y < d1.h && row1 < 0; ++y)
			for (int x = 0; x < d1.w; ++x)
				if (*(const byte *)d1.getBasePtr(x, y)) { row1 = y; break; }
		for (int y = 0; y < d2.h && row2 < 0; ++y)
			for (int x = 0; x < d2.w; ++x)
				if (*(const byte *)d2.getBasePtr(x, y)) { row2 = y; break; }
		TS_ASSERT(row1 >= 0);
		TS_ASSERT(row2 >= 0);
		// The TTF-primary draw's ink starts expectedShift rows from the
		// plain (no primary to align to) draw's - the implicit alignment,
		// with no origin= key in sight.
		TS_ASSERT_EQUALS(row1, row2 + expectedShift);
		d1.free();
		d2.free();
#else
		TS_SKIP("needs FreeType and a real filesystem");
#endif
	}

	/// What printChar() does with the game glyph's offsets (a pure function
	/// of two booleans and a line offset).
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
