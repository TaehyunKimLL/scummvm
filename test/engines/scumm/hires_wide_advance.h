#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/fs.h"
#include "common/memstream.h"
#include "common/rect.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/glyph_source.h"
#include "graphics/hires_text/text_compose.h"
#include "graphics/surface.h"

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

	/// Whether src actually paints some pixel for cp - GlyphMetrics::width/
	/// height are an SVFN-only field (its metrics table); the generic
	/// UnicodeGlyphSource::metrics() default (which TtfGlyphSource::metrics()
	/// calls into and never overrides width/height itself) leaves both at 0
	/// unconditionally, so checking them would always call a real TrueType
	/// glyph "blank". Scan the actual rows instead, the way
	/// ScummHiResText::scannedInkRight() does internally.
	static bool hasRealInk(Graphics::UnicodeGlyphSource *src, uint32 cp) {
		const int w = src->cellWidth() * 2;
		const int bpp = src->bitsPerPixel();
		for (int y = 0; y < src->cellHeight(); ++y) {
			const byte *row = src->row(cp, y);
			if (!row)
				break;
			for (int x = 0; x < w; ++x)
				if (Graphics::TextCompose::expandCoverage(row, x, bpp))
					return true;
		}
		return false;
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
			// 0xAC00 (가), the first Hangul syllable: the repo's own
			// NanumGothic-Bold.ttf (systemTtf()'s first candidate) has it,
			// so this is checked first, ahead of the Fullwidth-Forms/CJK
			// punctuation fallbacks a system font might cover instead.
			0xAC00, 0xFF21, 0xFF41, 0xFF10, 0xFF01, 0xFF08, 0xFF09, 0x300C, 0x300D, 0x3001, 0x3002,
		};
		for (uint i = 0; i < ARRAYSIZE(kCandidates); ++i) {
			uint32 cp = kCandidates[i];
			Graphics::UnicodeGlyphSource *src = hr.perGlyphSourceFor(id, cp);
			if (!src || src->cells(cp) <= 0)
				continue;
			Graphics::GlyphMetrics m;
			if (!src->metrics(cp, m) || !m.wide)
				continue;
			if (!hasRealInk(src, cp))
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
		// The repo's own Hangul TrueType face, tried first: SCUMM_HIRES_CENSUS_SRCDIR
		// (test/module.mk's -D for hires_hook_census.h, defined whenever
		// this file is - both are only compiled with ENABLE_SCUMM) gives an
		// absolute path to the source tree regardless of the runner's own
		// working directory ("dists/engine-data" alone, a path relative to
		// cwd, is not reachable from every build's actual `make test`
		// invocation - only from the source root, unlike
		// hires_text_font_map.h's identically-named precedent test, which
		// TS_SKIPs itself when it is not). DejaVuSans/AppleSDGothicNeo below
		// have no Hangul/Han/most fullwidth forms at all - see
		// findRealWideGlyph()'s comment.
#ifdef SCUMM_HIRES_CENSUS_SRCDIR
		static const Common::String kNanumGothic =
			Common::String(SCUMM_HIRES_CENSUS_SRCDIR) + "/dists/engine-data/hires_text/fonts/nanumgothic/NanumGothic-Bold.ttf";
		if (Common::FSNode(Common::Path(kNanumGothic, '/')).exists())
			return kNanumGothic.c_str();
#endif
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
		// codePointFor() only reads a literal Unicode code point past 0xFF
		// (a real Hangul syllable, not a game byte pair) in UTF-8 mode;
		// otherwise advanceFor() returns gameWidth from its own early-out
		// before ever reaching cellRuleAdvance() - the exact trap
		// test_wide_keeps_cell (hires_glyph_advance.h) hit first.
		hr.useUtf8Text();
		hr.noteGameCharset(2, 16, 16);
		hr.setCharsetGrid(2, 16, 16);
		TS_ASSERT(hr.loadFonts(Common::Path()));

		// M10: 0xAC00 (Hangul) is not in DejaVuSans, so this used to measure
		// only the missing-glyph fallback, whose advance happens to be the
		// game's own 16 either way - "adv > 0" could never fail. Use a real,
		// inked glyph, and prove the C31 default genuinely differs from the
		// grid rule (advance=game on the very same glyph), not merely that
		// it returns something positive.
		uint32 wide = findRealWideGlyph(hr, 2);
		if (!wide) {
			TS_SKIP("the available TrueType face has no real wide glyph to measure");
			return;
		}
		const int adv = hr.advanceFor(wide, 2, 16);
		TS_ASSERT_LESS_THAN(0, adv);

		Scumm::ScummHiResText grid;
		const Common::String gridBody = Common::String::format("[font.2]\nface=%s\nadvance=game\n", ttf);
		TS_ASSERT(open(grid, overlay, gridBody.c_str()));
		grid.useUtf8Text();
		grid.noteGameCharset(2, 16, 16);
		grid.setCharsetGrid(2, 16, 16);
		TS_ASSERT(grid.loadFonts(Common::Path()));
		const int gridAdv = grid.advanceFor(wide, 2, 16);
		TS_ASSERT_EQUALS(gridAdv, 16);
		TS_ASSERT(adv != gridAdv);
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
		// Measured via the drawn dirty-rect width (drawGlyphPlaced()'s own
		// `width` variable), not advanceFor(): cellRuleAdvance()'s clip
		// feeds into MAX(src->advance(cp), inkRight), and a real face's own
		// advance() is often already >= the clipped ink reach, which would
		// mask the clip entirely (confirmed by hand: reverting only
		// cellRuleAdvance()'s `face->lineFit ? ttfCellWidth(...) : 0` back
		// to an unconditional ttfCellWidth() did not fail this test the
		// first time it was written this way). The draw-width clip
		// (`hires_text.cpp` ~:1005) has no such competing MAX and clips the
		// measured value directly.
		int widths[2];
		uint32 wide = 0;
		for (int pass = 0; pass < 2; ++pass) {
			Scumm::HiResOverlay overlay;
			overlay.create(96, 96, true);
			Scumm::ScummHiResText hr;
			const Common::String body = pass == 0
				? Common::String::format("[font.2]\nface=%s\n", ttf)
				: Common::String::format("[font.2]\nface=%s\nsize=24\n", ttf);
			TS_ASSERT(open(hr, overlay, body.c_str()));
			hr.useUtf8Text(); // see test_wide_ttf_steps_by_face_by_default's note
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
			Graphics::Surface dest;
			dest.create(96, 96, Graphics::PixelFormat::createFormatCLUT8());
			memset(dest.getPixels(), 0, dest.pitch * dest.h);
			Common::Rect dirty;
			TS_ASSERT(hr.drawChar(dest, wide, 2, 10, 10, 15, 0, 1, &dirty));
			widths[pass] = dirty.width();
			dest.free();
		}
		// Line-fit (pass 0, no size=): the glyph is sized to (and clipped
		// to, if wider) the tiny 8 px game cell. size=24 (pass 1, lineFit
		// false): a much bigger glyph at its own size is not clipped down
		// to that cell - its drawn width is larger, not merely different.
		// A plain widths[0] < widths[1] is too weak here: even with the bug
		// reverted (the clip applied to any TTF wide glyph, lineFit or not)
		// pass 1's much bigger 24px glyph still edges out pass 0's, just
		// capped close to it (measured: 14 vs 16, a 2px gap) rather than
		// reflecting its true, unclamped size (measured: 14 vs 22, an 8px
		// gap, with the fix). Require a real margin, not just "more".
		TS_ASSERT_LESS_THAN_EQUALS(widths[0] + 5, widths[1]);
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
		hr.useUtf8Text(); // see test_wide_ttf_steps_by_face_by_default's note

		// Only kOtherCs's cell (noteGameCharset(), the actual font height/
		// width nearestTtfCharset() matches sizes by) is known at load
		// time; kCs's is not. kCs's own *grid* (setCharsetGrid(), used only
		// for glyph-box geometry) is given up front regardless - without
		// it, nearestTtfCharset()'s own `_charsetWidths[kCs] <= 0` guard
		// refuses to borrow a size for kCs at all, which is not what this
		// test is about (that guard is unrelated to M5's own re-resolution
		// question).
		hr.setCharsetGrid(kCs, 48, 48);
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
