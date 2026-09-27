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
 * C34/C36: Latin drawn by a TrueType face steps by the face. The replacement
 * face is sized to the game's cell, not to the game's own Latin font, so the
 * game's Latin widths space the face's smaller letters apart
 * ("T h r i f t w e e d", "W e l l, t h e n"). C34 stepped ASCII by the
 * face's own advance inside CJK text and UTF-8 translations; C36 makes it
 * the default everywhere, the game's own English included. An explicit
 * metrics=game ([render], [font.N], [latin], the ini's hires_text_metrics)
 * keeps the game's widths, as do [latin] mode=off/half/fullwidth, a kept or
 * remapped glyph, a mirrored charset kept on the game's font (C27), bitmap
 * faces, and hi-res off. The space keeps the game's width.
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

	static Graphics::HiResTextConfig parse(const Common::String &text, const char *baseDir = "/tmp/c34") {
		Graphics::HiResTextConfig c;
		Common::Array<Common::String> qualifiers;
		Common::MemoryReadStream stream((const byte *)text.c_str(), text.size());
		TS_ASSERT(Graphics::HiResFontMap::loadFromStream(stream, Common::Path(baseDir, '/'), qualifiers, c));
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
				Scumm::ScummHiResText cp949, utf8, en;
				TS_ASSERT(open(cp949, overlay, c, false, true));
				TS_ASSERT(open(utf8, overlay, c, true, true));
				TS_ASSERT(open(en, overlay, c, false, false));
				TS_ASSERT_EQUALS(cp949.advanceFor('T', kCs, kGameT), kGameT);
				TS_ASSERT_EQUALS(utf8.advanceFor('T', kCs, kGameT), kGameT);
				TS_ASSERT(!cp949.latinStepsByFace('T', kCs));
				// The game's own English too (C36).
				TS_ASSERT_EQUALS(en.advanceFor('T', kCs, kGameT), kGameT);
				TS_ASSERT(!en.latinStepsByFace('T', kCs));
			}
		}
#else
		TS_SKIP("needs FreeType and a real filesystem");
#endif
	}

	/// C36: a game without CJK cells in its own encoding (English MI1)
	/// steps its Latin by the face too, as does a UTF-8 translation without
	/// patch fonts (ja.trs, th.trs); the space as well.
	void test_english_steps_by_face() {
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
			const uint32 letters[] = { 'W', 'e', 'l', 'j', 'v', '/', '\\', ',', '-' };
			for (int i = 0; i < ARRAYSIZE(letters); i++) {
				const int want = faceStep(en, letters[i]);
				TS_ASSERT(want > 0);
				TS_ASSERT_EQUALS(en.advanceFor(letters[i], kCs, kGameT), want);
				TS_ASSERT_EQUALS(en.advanceFor(letters[i], kCs, 3), want);
				TS_ASSERT(en.latinStepsByFace(letters[i], kCs));
				TS_ASSERT_EQUALS(utf8.advanceFor(letters[i], kCs, kGameT), want);
				TS_ASSERT(utf8.latinStepsByFace(letters[i], kCs));
			}
			// The space steps by the face too without CJK cells (C36 M1).
			const int space = faceStep(en, ' ');
			TS_ASSERT(space > 0 && space != 6);
			TS_ASSERT_EQUALS(en.advanceFor(' ', kCs, 6), space);
			TS_ASSERT(en.latinStepsByFace(' ', kCs));
			TS_ASSERT_EQUALS(utf8.advanceFor(' ', kCs, 6), space);
		}
#else
		TS_SKIP("needs FreeType and a real filesystem");
#endif
	}

	/// C36: with CJK cells the space keeps the game's width (the Hangul
	/// word gap), in CP949 and UTF-8 alike; any metrics= key keeps it in
	/// English too.
	void test_space_rule() {
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
			TS_ASSERT_EQUALS(cp949.advanceFor(' ', kCs, 6), 6);
			TS_ASSERT_EQUALS(utf8.advanceFor(' ', kCs, 6), 6);
			TS_ASSERT(!cp949.latinStepsByFace(' ', kCs));
			const Graphics::HiResTextConfig g = parse(mi1Map(apple, pg == 1, "[render]\nmetrics=game\n"));
			Scumm::ScummHiResText en;
			TS_ASSERT(open(en, overlay, g, false, false));
			TS_ASSERT_EQUALS(en.advanceFor(' ', kCs, 6), 6);
		}
#else
		TS_SKIP("needs FreeType and a real filesystem");
#endif
	}

	/// C36 I1: a charset renderer that measures with the game's widths
	/// (FM-Towns, V2) switches the face step off, so measuring and drawing
	/// agree: every Latin code keeps the game's width.
	void test_renderer_gate() {
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
			for (int cells = 0; cells < 2; cells++) {
				Scumm::ScummHiResText hr;
				hr.setLatinFaceStepAllowed(false);
				TS_ASSERT(open(hr, overlay, c, false, cells == 1));
				TS_ASSERT(!hr.latinStepsByFace('T', kCs));
				TS_ASSERT(!hr.latinStepsByFace(' ', kCs));
				TS_ASSERT_EQUALS(hr.advanceFor('T', kCs, kGameT), kGameT);
				TS_ASSERT(!hr.drawsMissingGameGlyph('?', kCs, false));
			}
		}
#else
		TS_SKIP("needs FreeType and a real filesystem");
#endif
	}

	/// C36 in English: the keys that choose another Latin rule keep it.
	/// [latin] mode=off/half/fullwidth (per-glyph placement), a [glyphs]
	/// keep or remap, and [latin] metrics=font (its own, older path) are not
	/// the face step; a mirrored charset kept on the game's font (C27) keeps
	/// the game's width.
	void test_english_other_latin_rules_kept() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		const char *apple = appleGothic();
		if (!apple) {
			TS_SKIP("needs Apple SD Gothic Neo");
			return;
		}
		Scumm::HiResOverlay overlay;
		overlay.create(200, 40, true);
		const char *const extras[] = { "[latin]\nmode=off\n", "[latin]\nmode=half\n",
									   "[latin]\nmode=fullwidth\n", "[glyphs]\n0x54=keep\n",
									   "[glyphs]\n0x54=0x55\n", "[latin]\nmetrics=font\n" };
		for (int i = 0; i < ARRAYSIZE(extras); i++) {
			const Graphics::HiResTextConfig c = parse(mi1Map(apple, true, extras[i]));
			Scumm::ScummHiResText en;
			TS_ASSERT(open(en, overlay, c, false, false));
			TS_ASSERT(!en.latinStepsByFace('T', kCs));
		}
		// [glyphs] keep: the game's width, as before.
		{
			const Graphics::HiResTextConfig c = parse(mi1Map(apple, true, "[glyphs]\n0x54=keep\n"));
			Scumm::ScummHiResText en;
			TS_ASSERT(open(en, overlay, c, false, false));
			TS_ASSERT_EQUALS(en.advanceFor('T', kCs, kGameT), kGameT);
			// Its neighbours still step by the face.
			TS_ASSERT(en.latinStepsByFace('U', kCs));
		}
		// MI1's mirrored charset (C27): kept on the game's font and width.
		for (int pg = 0; pg < 2; pg++) {
			const Graphics::HiResTextConfig c = parse(mi1Map(apple, pg == 1, ""));
			Scumm::ScummHiResText en;
			en.setGameMirror("monkey", 5);
			TS_ASSERT(open(en, overlay, c, false, false));
			en.noteGameCharset(3, kCell, kCell);
			TS_ASSERT(!en.latinStepsByFace('T', 3));
			TS_ASSERT_EQUALS(en.advanceFor('T', 3, kGameT), kGameT);
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

			// The game's own text (English) steps by the face too since C36:
			// drawn, and measured at the same step.
			TS_ASSERT(en.drawsMissingGameGlyph('?', kCs, false));
			TS_ASSERT_EQUALS(en.advanceFor('?', kCs, 0), step);
			// With metrics=game it is not drawn and, under per-glyph
			// placement, measures 0. (A legacy map's metrics=game floor has
			// measured the face's fit for it since before C34: unchanged.)
			{
				const Graphics::HiResTextConfig g = parse(mi1Map(apple, pg == 1, "[render]\nmetrics=game\n"));
				Scumm::ScummHiResText game;
				TS_ASSERT(open(game, overlay, g, false, false));
				TS_ASSERT(!game.drawsMissingGameGlyph('?', kCs, false));
				if (pg == 1)
					TS_ASSERT_EQUALS(game.advanceFor('?', kCs, 0), 0);
			}
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

	/// C36: English letters whose ink leaves their face step ('j' left of
	/// the pen; '/', '\\', 'v' past the step when round-half-up rounds the
	/// advance down at 3x) are not clipped: drawn whole, and the dirty rect
	/// the engine masks and later erases (C32) covers all of their ink.
	void test_latin_overhang_inside_dirty() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		const char *const faces[] = { appleGothic(),
									  "dists/engine-data/hires_text/fonts/nanumgothic/NanumGothic-Bold.ttf" };
		int tried = 0;
		for (int fi = 0; fi < ARRAYSIZE(faces); fi++) {
			if (!faces[fi] || !Common::FSNode(Common::Path(faces[fi], '/')).exists())
				continue;
			const Common::String face = faces[fi];
			for (int scale = 2; scale <= 3; scale++) {
				tried++;
				const Graphics::HiResTextConfig c = parse(Common::String::format(
					"[hires]\nscale=%d\nalpha=true\nface=%s\n[encoding]\ncodepage=cp949\n", scale, face.c_str()), ".");
				Scumm::HiResOverlay overlay;
				overlay.create(120 * scale, 40 * scale, true);
				Scumm::ScummHiResText en;
				TSM_ASSERT(Common::String::format("%s at %d", face.c_str(), scale).c_str(), open(en, overlay, c, false, false));
				const uint32 letters[] = { 'j', '/', '\\', 'v', 'W', 'f' };
				for (int i = 0; i < ARRAYSIZE(letters); i++) {
					TS_ASSERT(en.latinStepsByFace(letters[i], kCs));
					const int step = en.advanceFor(letters[i], kCs, kGameT);
					Graphics::Surface a;
					a.create(120 * scale, 40 * scale, Graphics::PixelFormat::createFormatCLUT8());
					memset(a.getPixels(), 0, a.pitch * a.h);
					Common::Rect dirty;
					const int pen = 40 * scale;
					TS_ASSERT(en.drawChar(a, letters[i], kCs, pen, 4 * scale, kInk, 0, 1, &dirty, true, step));
					int inked = 0;
					for (int y = 0; y < a.h; y++)
						for (int x = 0; x < a.w; x++)
							if (*(const byte *)a.getBasePtr(x, y) != 0) {
								inked++;
								TS_ASSERT(dirty.contains(x, y));
							}
					TS_ASSERT(inked > 0);
					a.free();
				}
			}
		}
		if (!tried)
			TS_SKIP("needs Apple SD Gothic Neo or NanumGothic-Bold");
#else
		TS_SKIP("needs FreeType and a real filesystem");
#endif
	}

	/// C28 pixel faces ([font.N] pixel=) are TrueType faces held on their
	/// grid: with no metrics key their Latin steps by the face too, in
	/// English as in CJK text (the rule C34 set for any TrueType face).
	void test_pixel_face_latin() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		Common::String path;
		{
#pragma push_macro("getenv")
#undef getenv
			const char *data = getenv("SCUMMVM_TEST_I18N_DATA");
#pragma pop_macro("getenv")
			if (data && *data)
				path = Common::String::format("%s/../fonts/pixel/galmuri/Galmuri11.ttf", data);
		}
		if (path.empty() || !Common::FSNode(Common::Path(path, '/')).exists()) {
			TS_SKIP("Galmuri11.ttf not found (SCUMMVM_TEST_I18N_DATA)");
			return;
		}
		Scumm::HiResOverlay overlay;
		overlay.create(200, 40, true);
		for (int cells = 0; cells < 2; cells++) {
			const Graphics::HiResTextConfig c = parse(Common::String::format(
				"[hires]\nscale=2\nalpha=false\nface=%s\n[encoding]\ncodepage=cp949\n[font.2]\npixel=12\n",
				path.c_str()));
			Scumm::ScummHiResText hr;
			TS_ASSERT(open(hr, overlay, c, false, cells == 1));
			TS_ASSERT(hr.latinStepsByFace('T', kCs));
			TS_ASSERT_EQUALS(hr.advanceFor('T', kCs, 99), faceStep(hr, 'T'));
		}
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
