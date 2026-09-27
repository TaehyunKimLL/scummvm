#include <cxxtest/TestSuite.h>

#include "common/fs.h"
#include "common/memstream.h"
#include "common/str.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/glyph_source.h"
#include "graphics/hires_text/latin_advance.h"
#include "graphics/surface.h"

#include "engines/scumm/hires_overlay.h"
#include "engines/scumm/hires_text.h"

#include "../../system/null_osystem.h"

/**
 * C34: Latin inside CJK text. When the game lays its text out on a CJK font's
 * cells (a Korean patch in CP949, or its cells under a UTF-8 translation),
 * the replacement face is sized to that cell, not to the game's own Latin
 * font, so the game's Latin widths space the face's smaller letters apart
 * ("T h r i f t w e e d"). With no metrics= key ASCII then steps by the
 * face's own advance, as a wide glyph does since C31; so does ASCII in any
 * UTF-8 translation, whose own script already steps by the face. An explicit
 * metrics=game keeps the game's widths; a game's own text without CJK cells
 * (English) is unchanged.
 */
class ScummHiResLatinAdvanceTestSuite : public CxxTest::TestSuite {
private:
	static const byte kInk = 15;
	static const int kCs = 2;			///< MI1's dialogue charset
	static const int kCell = 9;			///< korean02.fnt: 9x9
	static const int kGameT = 8;		///< a game Latin width wider than the face's

	static const char *appleGothic() {
		static const char *const kPath = "/System/Library/Fonts/AppleSDGothicNeo.ttc";
		return Common::FSNode(kPath).exists() ? kPath : nullptr;
	}

	static Graphics::HiResTextConfig parse(const Common::String &text) {
		Graphics::HiResTextConfig c;
		Common::Array<Common::String> qualifiers;
		Common::MemoryReadStream stream((const byte *)text.c_str(), text.size());
		TS_ASSERT(Graphics::HiResFontMap::loadFromStream(stream, Common::Path("/tmp/c34", '/'), qualifiers, c));
		return c;
	}

	/// The MI1 test map, the face named by [fonts] default= (legacy
	/// placement for CP949) or by [hires] face= (per-glyph placement).
	static Common::String mi1Map(const char *face, bool perGlyph, const char *extra) {
		if (perGlyph)
			return Common::String::format("[hires]\nscale=2\nalpha=true\nface=%s\n[encoding]\ncodepage=cp949\n%s",
										  face, extra);
		return Common::String::format("[hires]\nscale=2\nalpha=true\n[encoding]\ncodepage=cp949\n"
									  "[fonts]\ndefault=%s\n%s", face, extra);
	}

	/// @p cjkCells: the game has a CJK font's cells (setGameFontCell), as a
	/// Korean patch does; otherwise the charset is measured from the game's
	/// own font, as English is.
	static bool open(Scumm::ScummHiResText &hr, Scumm::HiResOverlay &overlay,
					 const Graphics::HiResTextConfig &c, bool utf8, bool cjkCells) {
		hr.useOverlay(&overlay);
		hr.adoptConfig(c);
		if (utf8)
			hr.useUtf8Text();
		hr.setTtfFace(c.ttfPath[Graphics::kHiResRoleDefault]);
		if (cjkCells)
			hr.setGameFontCell(kCs, kCell, kCell);
		else
			hr.noteGameCharset(kCs, kCell, kCell);
		return hr.loadFonts(Common::Path());
	}

	/// The face's own step for @p cp in game pixels at scale 2, SCI's
	/// proportional rule (latinAdvanceGamePx under metrics=font).
	static int faceStep(Scumm::ScummHiResText &hr, uint32 cp) {
		Graphics::UnicodeGlyphSource *src = hr.sourceFor(kCs, true);
		TS_ASSERT(src);
		if (!src)
			return -1;
		return Graphics::latinAdvanceGamePx(Graphics::kHiResMetricsFont, 99, src->advance(cp), 2);
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

	/// No metrics key, CJK cells: ASCII steps by the face in CP949 (legacy
	/// and per-glyph placement) and in UTF-8, whatever the game's width.
	void test_latin_in_cjk_steps_by_face() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		const char *apple = appleGothic();
		if (!apple) {
			TS_SKIP("needs Apple SD Gothic Neo");
			return;
		}
		Scumm::HiResOverlay overlay;
		overlay.create(200, 40, true);
		for (int pg = 0; pg < 2; pg++) {
			const Graphics::HiResTextConfig c = parse(mi1Map(apple, pg == 1, ""));
			Scumm::ScummHiResText cp949, utf8;
			TS_ASSERT(open(cp949, overlay, c, false, true));
			TS_ASSERT(open(utf8, overlay, c, true, true));
			const uint32 letters[] = { 'T', 'h', 'i', 'w', '*' };
			for (int i = 0; i < ARRAYSIZE(letters); i++) {
				const int want = faceStep(cp949, letters[i]);
				TS_ASSERT(want > 0 && want < kGameT);
				TS_ASSERT_EQUALS(cp949.advanceFor(letters[i], kCs, kGameT), want);
				TS_ASSERT_EQUALS(utf8.advanceFor(letters[i], kCs, kGameT), want);
				TS_ASSERT_EQUALS(utf8.advanceFor(letters[i], kCs, 3), want);
				TS_ASSERT(cp949.latinStepsByFace(letters[i], kCs));
			}
		}
#else
		TS_SKIP("needs FreeType and a real filesystem");
#endif
	}

	/// An explicit metrics=game (map-wide, the charset's, or [latin]'s)
	/// keeps the game's Latin width.
	void test_explicit_metrics_game_keeps_game_width() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		const char *apple = appleGothic();
		if (!apple) {
			TS_SKIP("needs Apple SD Gothic Neo");
			return;
		}
		Scumm::HiResOverlay overlay;
		overlay.create(200, 40, true);
		const char *const extras[] = { "[render]\nmetrics=game\n", "[font.2]\nmetrics=game\n",
									   "[latin]\nmetrics=game\n" };
		for (int pg = 0; pg < 2; pg++) {
			for (int i = 0; i < ARRAYSIZE(extras); i++) {
				const Graphics::HiResTextConfig c = parse(mi1Map(apple, pg == 1, extras[i]));
				Scumm::ScummHiResText cp949, utf8;
				TS_ASSERT(open(cp949, overlay, c, false, true));
				TS_ASSERT(open(utf8, overlay, c, true, true));
				TS_ASSERT_EQUALS(cp949.advanceFor('T', kCs, kGameT), kGameT);
				TS_ASSERT_EQUALS(utf8.advanceFor('T', kCs, kGameT), kGameT);
				TS_ASSERT(!cp949.latinStepsByFace('T', kCs));
			}
		}
#else
		TS_SKIP("needs FreeType and a real filesystem");
#endif
	}

	/// A game without CJK cells in its own encoding (English MI1) keeps the
	/// game's Latin width: metrics=game stays the default for the game's own
	/// text. A UTF-8 translation without patch fonts (ja.trs, th.trs) steps
	/// ASCII by the face, as its kana, kanji and Thai already do.
	void test_no_cjk_cells() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		const char *apple = appleGothic();
		if (!apple) {
			TS_SKIP("needs Apple SD Gothic Neo");
			return;
		}
		Scumm::HiResOverlay overlay;
		overlay.create(200, 40, true);
		for (int pg = 0; pg < 2; pg++) {
			const Graphics::HiResTextConfig c = parse(mi1Map(apple, pg == 1, ""));
			Scumm::ScummHiResText en, utf8;
			TS_ASSERT(open(en, overlay, c, false, false));
			TS_ASSERT(open(utf8, overlay, c, true, false));
			TS_ASSERT_EQUALS(en.advanceFor('T', kCs, kGameT), kGameT);
			TS_ASSERT(!en.latinStepsByFace('T', kCs));
			TS_ASSERT_EQUALS(utf8.advanceFor('T', kCs, kGameT), faceStep(utf8, 'T'));
			TS_ASSERT(utf8.latinStepsByFace('T', kCs));
		}
#else
		TS_SKIP("needs FreeType and a real filesystem");
#endif
	}

	/// An ASCII code the game's charset has no glyph for (MI1's verb charset
	/// lacks '?'): getCharWidth() measures advanceFor(chr, cs, 0) and
	/// printChar() draws it through this layer when it steps by the face, so
	/// both sides agree; otherwise it measures 0 and is not drawn.
	void test_missing_game_glyph_measured_as_drawn() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		const char *apple = appleGothic();
		if (!apple) {
			TS_SKIP("needs Apple SD Gothic Neo");
			return;
		}
		Scumm::HiResOverlay overlay;
		overlay.create(200, 40, true);
		for (int pg = 0; pg < 2; pg++) {
			const Graphics::HiResTextConfig c = parse(mi1Map(apple, pg == 1, ""));
			Scumm::ScummHiResText cp949, en;
			TS_ASSERT(open(cp949, overlay, c, false, true));
			TS_ASSERT(open(en, overlay, c, false, false));

			// Face-stepped: drawn, and measured at the face step from a game
			// width of 0.
			TS_ASSERT(cp949.drawsMissingGameGlyph('?', kCs, false));
			const int step = cp949.advanceFor('?', kCs, 0);
			TS_ASSERT_EQUALS(step, faceStep(cp949, '?'));
			TS_ASSERT(step > 0);
			Graphics::Surface a;
			a.create(200, 40, Graphics::PixelFormat::createFormatCLUT8());
			memset(a.getPixels(), 0, 200 * 40);
			TS_ASSERT(cp949.drawChar(a, '?', kCs, 40, 4, kInk, 0, 1, nullptr, true, step));
			int left;
			TS_ASSERT(inkLeft(a, left));
			a.free();

			// The game's own text (English): not drawn, and under per-glyph
			// placement measured 0. (A legacy map's metrics=game floor has
			// measured the face's fit for it since before C34: unchanged.)
			TS_ASSERT(!en.drawsMissingGameGlyph('?', kCs, false));
			if (pg == 1)
				TS_ASSERT_EQUALS(en.advanceFor('?', kCs, 0), 0);
			// The space is never face-stepped.
			TS_ASSERT(!cp949.drawsMissingGameGlyph(' ', kCs, false));
			// UTF-8 code points above ASCII, as before C34.
			TS_ASSERT(en.drawsMissingGameGlyph(0xAC00, kCs, true));
		}

		// Hi-res off: nothing is drawn by the layer.
		Scumm::ScummHiResText off;
		TS_ASSERT(!off.drawsMissingGameGlyph('?', kCs, false));
		TS_ASSERT_EQUALS(off.advanceFor('?', kCs, 0), 0);
#else
		TS_SKIP("needs FreeType and a real filesystem");
#endif
	}

	/// Stepping by the face, a Latin glyph is drawn at the pen, not centred
	/// in the game's (wider) width.
	void test_latin_face_step_not_centred() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		const char *apple = appleGothic();
		if (!apple) {
			TS_SKIP("needs Apple SD Gothic Neo");
			return;
		}
		Scumm::HiResOverlay overlay;
		overlay.create(200, 40, true);
		const Graphics::HiResTextConfig c = parse(mi1Map(apple, true, ""));
		Scumm::ScummHiResText utf8;
		TS_ASSERT(open(utf8, overlay, c, true, true));

		Graphics::Surface a, b;
		a.create(200, 40, Graphics::PixelFormat::createFormatCLUT8());
		b.create(200, 40, Graphics::PixelFormat::createFormatCLUT8());
		memset(a.getPixels(), 0, 200 * 40);
		memset(b.getPixels(), 0, 200 * 40);
		TS_ASSERT(utf8.drawChar(a, 'T', kCs, 40, 4, kInk, 0, 1, nullptr, true, 0));
		TS_ASSERT(utf8.drawChar(b, 'T', kCs, 40, 4, kInk, 0, 1, nullptr, true, 16));
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
