#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/fs.h"
#include "common/memstream.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/glyph_source.h"

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
	static const int kOtherCs = ScummHiResFixture::kOtherCs;

	bool open(Scumm::ScummHiResText &hr, Scumm::HiResOverlay &overlay, const char *body) {
		return ScummHiResFixture::open(hr, overlay, body);
	}

	/// A Unicode::isWide() code point this id's own TrueType face actually
	/// has a real glyph for - DejaVuSans has broad symbol coverage but no
	/// Hangul or Han, so the fixed 0xAC00 many other tests in this file use
	/// (which only ever exercises DejaVuSans's .notdef/missing-glyph
	/// fallback, not a real one) will not do for a test that needs to
	/// measure a real glyph's own ink at two different sizes. Returns 0 if
	/// none of the candidates are covered.
	static uint32 findRealWideGlyph(Scumm::ScummHiResText &hr, int id) {
		static const uint32 kCandidates[] = {
			0xFF21, 0xFF41, 0xFF10, 0xFF01, 0xFF08, 0xFF09, 0x300C, 0x300D, 0x3001, 0x3002,
		};
		for (uint i = 0; i < ARRAYSIZE(kCandidates); ++i) {
			uint32 cp = kCandidates[i];
			Graphics::UnicodeGlyphSource *src = hr.perGlyphSourceFor(id, cp);
			if (!src || src->cells(cp) <= 0)
				continue;
			Graphics::GlyphMetrics m;
			if (!src->metrics(cp, m) || !m.wide)
				continue;
			if (m.width <= 0 || m.height <= 0)
				continue; // no ink to measure (e.g. a blank/space glyph)
			return kCandidates[i];
		}
		return 0;
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
		// codePointFor() reads a literal Unicode code point past 0xFF as a
		// game code only in UTF-8 mode; without this, 0xAC00 decodes to
		// nothing and advanceFor() returns gameWidth from its own early-out,
		// which happened to equal 12 below regardless of cellRuleAdvance()
		// (M10's flagged weak-test pattern - this one was not caught by the
		// review, but has the identical defect: see test_wide_keeps_cell).
		hr.useUtf8Text();
		Common::Array<uint32> own;
		own.push_back(0xAC00);
		TS_ASSERT(ScummHiResFixture::addFace(hr, "/tmp/t/OWN.SVF", own));
		// makeFont()'s advance (9 hi-res px) at scale 2 rounds up to 5, and
		// the game's own (12) is wider, so the game's width is the floor.
		TS_ASSERT_EQUALS(hr.advanceFor(0xAC00, kCs, 12), 12);
		// A narrower game width does not float the answer down with it - the
		// ink-based fit (5) is still the floor.
		TS_ASSERT_EQUALS(hr.advanceFor(0xAC00, kCs, 2), 5);
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

	/// M4: the wide-glyph clip to ttfCellWidth() only applies to a face
	/// opened line-fit (sized to the game's own cell); a face given an
	/// explicit size= draws and advances at its own size, uncapped, even
	/// on a tiny 8 px game charset (the old test_font_n_is_charset_id
	/// configuration).
	void test_line_fit_gates_the_wide_glyph_clip() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		const char *ttf = systemTtf();
		if (!ttf) {
			TS_SKIP("needs a TrueType face");
			return;
		}
		int adv[2];
		uint32 wide = 0;
		for (int pass = 0; pass < 2; ++pass) {
			Scumm::HiResOverlay overlay;
			overlay.create(64, 40, true);
			Scumm::ScummHiResText hr;
			const Common::String body = pass == 0
				? Common::String::format("[font.2]\nface=%s\nadvance=font\n", ttf)
				: Common::String::format("[font.2]\nface=%s\nsize=24\nadvance=font\n", ttf);
			TS_ASSERT(open(hr, overlay, body.c_str()));
			hr.noteGameCharset(2, 8, 8);
			hr.setCharsetGrid(2, 8, 8);
			TS_ASSERT(hr.loadFonts(Common::Path()));
			if (pass == 0) {
				wide = findRealWideGlyph(hr, 2);
				if (!wide) {
					TS_SKIP("the available TrueType face has no real wide glyph to measure");
					return;
				}
			}
			adv[pass] = hr.advanceFor(wide, 2, 8);
		}
		// Line-fit (pass 0, no size=): the face is sized to the tiny 8 px
		// game cell, so its own ink reach is small too. size=24 (pass 1,
		// lineFit false): a much bigger face at its own size is not capped
		// down to that cell - its advance is larger, not merely different.
		TS_ASSERT_LESS_THAN(adv[0], adv[1]);
#else
		TS_SKIP("needs FreeType");
#endif
	}

	/// M5: a TrueType face is re-opened when its own charset's size becomes
	/// known, even though it was already opened once at a borrowed/guessed
	/// size (nearestTtfCharset(), before noteGameCharset() gave the id its
	/// own cell) - the old ttfFaceFor()'s "a charset on another cell opens
	/// the face again". Built without ScummHiResFixture::open()/openMap(),
	/// which both give kCs and kOtherCs the same 8x8 grid up front - this
	/// test needs kCs's cell to still be *unknown* at loadFonts() time.
	void test_ttf_reopens_when_the_charset_learns_its_own_size() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		const char *ttf = systemTtf();
		if (!ttf) {
			TS_SKIP("needs a TrueType face");
			return;
		}
		Scumm::HiResOverlay overlay;
		overlay.create(96, 48, true);
		Scumm::ScummHiResText hr;
		hr.useOverlay(&overlay);
		const Common::String text = Common::String::format(
			"[map]\nversion=2\n[render]\nblend=off\n[font]\nface=%s\nadvance=font\n", ttf);
		Graphics::HiResMap m;
		Common::Array<Common::String> q;
		Common::MemoryReadStream s((const byte *)text.c_str(), text.size());
		TS_ASSERT(Graphics::HiResFontMap::loadMap(s, Common::Path("/tmp/m5", '/'), q, Graphics::kHiResKeysScumm, m));
		hr.adoptMap(m);

		// Only kOtherCs's cell is known at load time; kCs's is not, so it
		// opens at kOtherCs's (tiny) borrowed size (nearestTtfCharset()).
		hr.noteGameCharset(kOtherCs, 8, 8);
		hr.setCharsetGrid(kOtherCs, 8, 8);
		TS_ASSERT(hr.loadFonts(Common::Path()));

		uint32 wide = findRealWideGlyph(hr, kCs);
		if (!wide) {
			TS_SKIP("the available TrueType face has no real wide glyph to measure");
			return;
		}
		const int advBorrowed = hr.advanceFor(wide, kCs, 8);

		// kCs's own, much larger cell becomes known, as it does when the
		// game selects this charset for the first time.
		hr.noteGameCharset(kCs, 48, 48);
		hr.setCharsetGrid(kCs, 48, 48);
		const int advOwn = hr.advanceFor(wide, kCs, 48);

		TS_ASSERT_LESS_THAN(advBorrowed, advOwn);
#else
		TS_SKIP("needs FreeType");
#endif
	}
};
