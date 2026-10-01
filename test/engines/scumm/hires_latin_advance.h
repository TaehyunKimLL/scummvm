#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/fs.h"
#include "common/memstream.h"
#include "common/rect.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/surface.h"

#include "engines/scumm/hires_overlay.h"
#include "engines/scumm/hires_text.h"

#include "support/hires_fixture.h"

/**
 * latinStepsByFace() (design section 8's advance=): true exactly when
 * advanceFor()'s own resolution would draw the ASCII character from a
 * TrueType face and step by that face's own advance (advance.basic-
 * latin=font). SCUMM's engine-scope default is advance.basic-latin=game
 * (design section 8's table): a TrueType face does not step Latin by
 * itself unless the map (or hires_text_advance) asks for it.
 */
class ScummHiResLatinAdvanceTestSuite : public CxxTest::TestSuite {
	static const int kCs = ScummHiResFixture::kCs;

	bool open(Scumm::ScummHiResText &hr, Scumm::HiResOverlay &overlay, const char *body) {
		return ScummHiResFixture::open(hr, overlay, body);
	}

	static const char *systemTtf() {
		// The repo's own Hangul/Latin TrueType face, tried first: an
		// absolute, SCUMM_HIRES_CENSUS_SRCDIR-anchored path (see
		// hires_wide_advance.h's systemTtf() for why a bare relative
		// "dists/engine-data" path is not reachable from this build's
		// actual `make test` working directory).
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

	/**
	 * drawsMissingGameGlyph(): a game glyph
	 * the charset has none for (prepareDraw() failed) is still drawn, and
	 * measured at the face's own step from a game width of 0, when
	 * advance.basic-latin=font makes the ASCII character step by the face
	 * (latinStepsByFace()) - not only for a UTF-8 code point past 0x80.
	 * Without that key (the engine default), the same character neither
	 * draws nor measures a real width from a 0 game width.
	 */
	void test_missing_game_glyph_measured_as_drawn() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		const char *ttf = systemTtf();
		if (!ttf) {
			TS_SKIP("needs a TrueType face");
			return;
		}
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, true);
		Scumm::ScummHiResText face;
		const Common::String faceBody = Common::String::format(
			"[font.2]\nface=%s\nadvance.basic-latin=font\n", ttf);
		TS_ASSERT(open(face, overlay, faceBody.c_str()));
		face.noteGameCharset(2, 9, 9);
		face.setCharsetGrid(2, 9, 9);
		TS_ASSERT(face.loadFonts(Common::Path()));
		TS_ASSERT(face.drawsMissingGameGlyph('T', 2, false));
		const int step = face.advanceFor('T', 2, 0);
		TS_ASSERT_LESS_THAN(0, step);

		Graphics::Surface dest;
		dest.create(64, 40, Graphics::PixelFormat::createFormatCLUT8());
		memset(dest.getPixels(), 0, dest.pitch * dest.h);
		TS_ASSERT(face.drawChar(dest, 'T', 2, 4, 4, 15, 0, 1, nullptr, true, step));
		dest.free();

		// The engine default (advance.basic-latin=game): neither drawn nor
		// measured from a 0 game width as a "missing glyph" step.
		Scumm::ScummHiResText game;
		const Common::String gameBody = Common::String::format("[font.2]\nface=%s\n", ttf);
		TS_ASSERT(open(game, overlay, gameBody.c_str()));
		game.noteGameCharset(2, 9, 9);
		game.setCharsetGrid(2, 9, 9);
		TS_ASSERT(game.loadFonts(Common::Path()));
		TS_ASSERT(!game.drawsMissingGameGlyph('T', 2, false));
		TS_ASSERT_EQUALS(game.advanceFor('T', 2, 0), 0);
#else
		TS_SKIP("needs FreeType and a real filesystem");
#endif
	}

	/**
	 * A TrueType Latin glyph's ink often overhangs its own advance step
	 * (italic-leaning descenders/ascenders: 'j', slashes, 'W', 'f') - the
	 * dirty rect drawChar() hands back must still contain every pixel it
	 * actually inked, at every scale.
	 */
	void test_latin_overhang_inside_dirty() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		const char *ttf = systemTtf();
		if (!ttf) {
			TS_SKIP("needs a TrueType face");
			return;
		}
		for (int scale = 2; scale <= 3; ++scale) {
			Scumm::HiResOverlay overlay;
			overlay.create(160 * scale, 48 * scale, true);
			Scumm::ScummHiResText hr;
			const Common::String body = Common::String::format(
				"[map]\nversion=2\n[render]\nblend=off\nscale=%d\n"
				"[font.2]\nface=%s\nadvance.basic-latin=font\n", scale, ttf);
			Graphics::HiResMap m;
			Common::Array<Common::String> q;
			Common::MemoryReadStream s((const byte *)body.c_str(), body.size());
			TS_ASSERT(Graphics::HiResFontMap::loadMap(s, Common::Path("/tmp/t", '/'), q, Graphics::kHiResKeysScumm, m));
			hr.useOverlay(&overlay);
			hr.adoptMap(m);
			hr.noteGameCharset(2, 9, 9);
			hr.setCharsetGrid(2, 9, 9);
			TS_ASSERT(hr.loadFonts(Common::Path()));

			const char letters[] = { 'j', '/', '\\', 'v', 'W', 'f' };
			for (uint i = 0; i < ARRAYSIZE(letters); ++i) {
				const int chr = letters[i];
				if (!hr.latinStepsByFace(chr, 2))
					continue; // the face has no glyph for it: nothing to check
				const int step = hr.advanceFor(chr, 2, 9);
				Graphics::Surface dest;
				dest.create(160 * scale, 48 * scale, Graphics::PixelFormat::createFormatCLUT8());
				memset(dest.getPixels(), 0, dest.pitch * dest.h);
				Common::Rect dirty;
				const int pen = 40 * scale;
				TS_ASSERT(hr.drawChar(dest, chr, 2, pen, 4 * scale, 15, 0, 1, &dirty, true, step));
				int inked = 0;
				for (int y = 0; y < dest.h; ++y)
					for (int x = 0; x < dest.w; ++x)
						if (*(const byte *)dest.getBasePtr(x, y)) {
							++inked;
							TS_ASSERT(dirty.contains(x, y));
						}
				TS_ASSERT(inked > 0);
				dest.free();
			}
		}
#else
		TS_SKIP("needs FreeType and a real filesystem");
#endif
	}

	/**
	 * A pixel=-sized TrueType Latin face (design's pixel=, held at its
	 * design size in the game cell rather than scaled to fit it) still
	 * steps Latin by the face under advance.basic-latin=font, the same as
	 * a plain size= one.
	 */
	void test_pixel_face_latin() {
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
			"[font.2]\nface=%s\npixel=12\nadvance.basic-latin=font\n", ttf);
		TS_ASSERT(open(hr, overlay, body.c_str()));
		hr.noteGameCharset(2, 9, 9);
		hr.setCharsetGrid(2, 9, 9);
		TS_ASSERT(hr.loadFonts(Common::Path()));
		TS_ASSERT(hr.latinStepsByFace('T', 2));
		TS_ASSERT_LESS_THAN(0, hr.advanceFor('T', 2, 99));
#else
		TS_SKIP("needs FreeType and a real filesystem");
#endif
	}

	/**
	 * Stepping by the face (advance.basic-latin=font), a Latin glyph is
	 * drawn at the pen, not centred in the game's (wider) width - unlike
	 * advance.basic-latin=game's centring, which hires_glyph_advance.h
	 * covers.
	 */
	void test_face_step_latin_not_centred() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		const char *ttf = systemTtf();
		if (!ttf) {
			TS_SKIP("needs a TrueType face");
			return;
		}
		Scumm::HiResOverlay overlay;
		overlay.create(200, 40, true);
		Scumm::ScummHiResText hr;
		const Common::String body = Common::String::format(
			"[font.2]\nface=%s\nadvance.basic-latin=font\n", ttf);
		TS_ASSERT(open(hr, overlay, body.c_str()));
		hr.noteGameCharset(2, 9, 9);
		hr.setCharsetGrid(2, 9, 9);
		TS_ASSERT(hr.loadFonts(Common::Path()));

		Graphics::Surface a, b;
		a.create(200, 40, Graphics::PixelFormat::createFormatCLUT8());
		b.create(200, 40, Graphics::PixelFormat::createFormatCLUT8());
		memset(a.getPixels(), 0, a.pitch * a.h);
		memset(b.getPixels(), 0, b.pitch * b.h);
		// The same character at the same pen, once with no gameAdvance
		// passed and once with a much wider one (16, well past the face's
		// own step): a centred draw would shift with it, a face-stepped one
		// must not.
		TS_ASSERT(hr.drawChar(a, 'T', 2, 40, 4, 15, 0, 1, nullptr, true, 0));
		TS_ASSERT(hr.drawChar(b, 'T', 2, 40, 4, 15, 0, 1, nullptr, true, 16));
		int la = -1, lb = -1;
		for (int x = 0; x < a.w && la < 0; ++x)
			for (int y = 0; y < a.h; ++y)
				if (*(const byte *)a.getBasePtr(x, y)) { la = x; break; }
		for (int x = 0; x < b.w && lb < 0; ++x)
			for (int y = 0; y < b.h; ++y)
				if (*(const byte *)b.getBasePtr(x, y)) { lb = x; break; }
		TS_ASSERT(la >= 0);
		TS_ASSERT_EQUALS(la, lb);
		a.free();
		b.free();
#else
		TS_SKIP("needs FreeType and a real filesystem");
#endif
	}
};
