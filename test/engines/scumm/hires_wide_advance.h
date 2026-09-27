#include <cxxtest/TestSuite.h>

#include "common/fs.h"
#include "common/memstream.h"
#include "common/str.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/glyph_source.h"
#include "graphics/surface.h"

#include "engines/scumm/hires_overlay.h"
#include "engines/scumm/hires_text.h"

#include "../../system/null_osystem.h"

/**
 * C31: a wide glyph (Hangul, kanji) drawn by a TrueType face steps by the
 * face's own advance, rounded up to game pixels, unless a metrics= key asks
 * for the game's cell. The same rule answers a CP949 layout (legacy placement,
 * the double-byte cell as the game width) and a UTF-8 one (per-glyph
 * placement, the '?' stand-in as the game width), so the two lay a line out
 * alike.
 */
class ScummHiResWideAdvanceTestSuite : public CxxTest::TestSuite {
private:
	static const byte kInk = 15;
	static const int kGaCp949 = 0xA1B0;	///< U+AC00 as printChar() gets it: lead B0 low, trail A1 high
	static const int kCs = 2;			///< MI1's dialogue charset
	static const int kCell = 9;			///< korean02.fnt: 9x9

	static const char *appleGothic() {
		static const char *const kPath = "/System/Library/Fonts/AppleSDGothicNeo.ttc";
		return Common::FSNode(kPath).exists() ? kPath : nullptr;
	}

	static Graphics::HiResTextConfig parse(const Common::String &text) {
		Graphics::HiResTextConfig c;
		Common::Array<Common::String> qualifiers;
		Common::MemoryReadStream stream((const byte *)text.c_str(), text.size());
		TS_ASSERT(Graphics::HiResFontMap::loadFromStream(stream, Common::Path("/tmp/c31", '/'), qualifiers, c));
		return c;
	}

	/// The MI1 test map (runs/c6-maps/mi1ute), @p extra appended.
	static Common::String mi1Map(const char *face, const char *extra) {
		return Common::String::format("[hires]\nscale=2\nalpha=true\n[encoding]\ncodepage=cp949\n"
									  "[fonts]\ndefault=%s\n%s", face, extra);
	}

	static bool open(Scumm::ScummHiResText &hr, Scumm::HiResOverlay &overlay,
					 const Graphics::HiResTextConfig &c, bool utf8) {
		hr.useOverlay(&overlay);
		hr.adoptConfig(c);
		if (utf8)
			hr.useUtf8Text();
		hr.setTtfFace(c.ttfPath[Graphics::kHiResRoleDefault]);
		hr.setGameFontCell(kCs, kCell, kCell);
		return hr.loadFonts(Common::Path());
	}

	/// The face's own step for U+AC00 in game pixels: its advance, widened
	/// to its ink, rounded up at scale 2.
	static int faceFit(Scumm::ScummHiResText &hr) {
		Graphics::UnicodeGlyphSource *src = hr.sourceFor(kCs, false);
		TS_ASSERT(src);
		if (!src)
			return -1;
		return (src->advance(0xAC00) + 1) / 2;
	}

	static bool inkLeft(const Graphics::Surface &s, int &left) {
		left = s.w;
		for (int y = 0; y < s.h; y++)
			for (int x = 0; x < s.w; x++)
				if (*(const byte *)s.getBasePtr(x, y) != 0)
					left = MIN(left, x);
		return left < s.w;
	}

public:
	void setUp() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::install_null_g_system();
#endif
	}

	void tearDown() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::uninstall_null_g_system();
#endif
	}

	void test_render_metrics_set_is_recorded() {
		TS_ASSERT(!parse("[hires]\nscale=2\n").metricsSourceSet);
		TS_ASSERT(parse("[render]\nmetrics=game\n").metricsSourceSet);
		TS_ASSERT(parse("[render]\nmetrics=font\n").metricsSourceSet);
		TS_ASSERT(!parse("[render]\nmetrics=bogus\n").metricsSourceSet);
	}

	/// The Korean patches break Hangul anywhere except in centred text; a
	/// map's [layout] hangul= wins in both.
	void test_hangul_break_follows_centring() {
		Scumm::ScummHiResText hr;
		hr.adoptConfig(parse("[hires]\nscale=2\n"));
		TS_ASSERT_EQUALS(hr.breakRules().hangul, Graphics::kHangulBreakAny);
		TS_ASSERT_EQUALS(hr.breakRules(true).hangul, Graphics::kHangulBreakWord);
		Scumm::ScummHiResText any;
		any.adoptConfig(parse("[hires]\nscale=2\n[layout]\nhangul=any\n"));
		TS_ASSERT_EQUALS(any.breakRules(true).hangul, Graphics::kHangulBreakAny);

		// Hi-res text off: UTF-8 text breaks as it did before C31, Hangul
		// anywhere, centred or not.
		Scumm::ScummHiResText off;
		TS_ASSERT(!off.enabled());
		TS_ASSERT_EQUALS(off.breakRules(true).hangul, Graphics::kHangulBreakAny);
		TS_ASSERT_EQUALS(off.breakRules(false).hangul, Graphics::kHangulBreakAny);
	}

	/// No metrics key: CP949 and UTF-8 both step U+AC00 by the face, not by
	/// the patch cell + 1 (10) nor the stand-in's width.
	void test_wide_ttf_steps_by_face_by_default() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		const char *apple = appleGothic();
		if (!apple) {
			TS_SKIP("needs Apple SD Gothic Neo");
			return;
		}
		Scumm::HiResOverlay overlay;
		overlay.create(200, 40, true);
		const Graphics::HiResTextConfig c = parse(mi1Map(apple, ""));
		Scumm::ScummHiResText cp949, utf8;
		TS_ASSERT(open(cp949, overlay, c, false));
		TS_ASSERT(open(utf8, overlay, c, true));
		TS_ASSERT(!cp949.perGlyphMetrics());
		TS_ASSERT(utf8.perGlyphMetrics());

		const int fit = faceFit(cp949);
		TS_ASSERT(fit > 0 && fit < kCell + 1);
		TS_ASSERT_EQUALS(cp949.advanceFor(kGaCp949, kCs, kCell + 1), fit);
		TS_ASSERT_EQUALS(utf8.advanceFor(0xAC00, kCs, kCell + 1), fit);
		// Whatever the game width: the stand-in's 8, or a too narrow 3.
		TS_ASSERT_EQUALS(utf8.advanceFor(0xAC00, kCs, 8), fit);
		TS_ASSERT_EQUALS(utf8.advanceFor(0xAC00, kCs, 3), fit);
		int carry = 1;
		TS_ASSERT_EQUALS(utf8.advanceFor(0xAC00, kCs, 8, &carry), fit);
		TS_ASSERT_EQUALS(carry, 1);	// the carry belongs to metrics=font
		// ASCII steps by the face too since C34 (hires_latin_advance.h); the
		// space keeps the game's width.
		TS_ASSERT(cp949.advanceFor('a', kCs, 6) != 6);
		TS_ASSERT_EQUALS(cp949.advanceFor(' ', kCs, 6), 6);
#else
		TS_SKIP("needs FreeType and a real filesystem");
#endif
	}

	/// An explicit metrics=game, map-wide or for the charset, keeps the old
	/// cell rule (the patch cell + 1 wins over the face).
	void test_explicit_metrics_game_keeps_cell() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		const char *apple = appleGothic();
		if (!apple) {
			TS_SKIP("needs Apple SD Gothic Neo");
			return;
		}
		Scumm::HiResOverlay overlay;
		overlay.create(200, 40, true);
		const char *const extras[] = { "[render]\nmetrics=game\n", "[font.2]\nmetrics=game\n" };
		for (int i = 0; i < 2; i++) {
			const Graphics::HiResTextConfig c = parse(mi1Map(apple, extras[i]));
			Scumm::ScummHiResText cp949, utf8;
			TS_ASSERT(open(cp949, overlay, c, false));
			TS_ASSERT(open(utf8, overlay, c, true));
			TS_ASSERT_EQUALS(cp949.advanceFor(kGaCp949, kCs, kCell + 1), kCell + 1);
			TS_ASSERT_EQUALS(utf8.advanceFor(0xAC00, kCs, kCell + 1), kCell + 1);
		}
#else
		TS_SKIP("needs FreeType and a real filesystem");
#endif
	}

	/// Under the face's own step a wide glyph is not centred in the game
	/// width: CP949 and UTF-8 put its ink at the same column.
	void test_wide_ttf_not_centred() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		const char *apple = appleGothic();
		if (!apple) {
			TS_SKIP("needs Apple SD Gothic Neo");
			return;
		}
		Scumm::HiResOverlay overlay;
		overlay.create(200, 40, true);
		const Graphics::HiResTextConfig c = parse(mi1Map(apple, ""));
		Scumm::ScummHiResText cp949, utf8;
		TS_ASSERT(open(cp949, overlay, c, false));
		TS_ASSERT(open(utf8, overlay, c, true));

		Graphics::Surface a, b;
		a.create(200, 40, Graphics::PixelFormat::createFormatCLUT8());
		b.create(200, 40, Graphics::PixelFormat::createFormatCLUT8());
		memset(a.getPixels(), 0, 200 * 40);
		memset(b.getPixels(), 0, 200 * 40);
		// The game width a caller passes: the cell + 1 (CP949), much wider
		// than the glyph for UTF-8 to make centring visible.
		TS_ASSERT(cp949.drawChar(a, kGaCp949, kCs, 40, 4, kInk, 0, 1, nullptr, true, 0));
		TS_ASSERT(utf8.drawChar(b, 0xAC00, kCs, 40, 4, kInk, 0, 1, nullptr, true, 16));
		int la, lb;
		TS_ASSERT(inkLeft(a, la));
		TS_ASSERT(inkLeft(b, lb));
		TS_ASSERT_EQUALS(la, lb);
		a.free();
		b.free();
#else
		TS_SKIP("needs FreeType and a real filesystem");
#endif
	}
};
