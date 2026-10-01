#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/fs.h"
#include "common/memstream.h"
#include "graphics/hires_text/bitmap_font.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/glyph_renderer.h"
#include "graphics/surface.h"

#include "engines/scumm/hires_overlay.h"
#include "engines/scumm/hires_text.h"

#include "../../system/null_osystem.h"

/**
 * SCUMM's hi-res text drawing from the shared glyph sources.
 *
 * The bitmap (SVFN) fonts a translation ships go through SvfnGlyphSource
 * like every other engine's, generically: this suite pins that the move
 * changed no pixel, comparing against a hand-rolled reference blit straight
 * off HiResBitmapFont. The CJK/Latin split is `range.basic-latin=` (a
 * named face), placed by origin= and stepped by advance=.
 */
class ScummHiResGlyphSourceTestSuite : public CxxTest::TestSuite {
private:
	static void put16(Common::Array<byte> &b, uint pos, uint16 v) {
		b[pos] = v & 0xff;
		b[pos + 1] = (v >> 8) & 0xff;
	}

	static void put32(Common::Array<byte> &b, uint pos, uint32 v) {
		b[pos] = v & 0xff;
		b[pos + 1] = (v >> 8) & 0xff;
		b[pos + 2] = (v >> 16) & 0xff;
		b[pos + 3] = (v >> 24) & 0xff;
	}

	static const int kGaChr = 0xA1B0;
	static const int kCell = 16;

	/**
	 * A version 2 SVFN file of two glyphs, U+AC00 and U+0041, with a code
	 * point table and, when asked, a metrics table.
	 */
	static Common::Array<byte> makeFont(int bpp, bool proportional, int ascent, int seed) {
		const int kGlyphs = 2;
		const uint32 cps[kGlyphs] = { 0xAC00, 0x0041 };
		const int cellW = kCell, cellH = kCell;
		const int rowPitch = (bpp == 1) ? (cellW + 7) / 8 : cellW;
		const uint32 glyphStride = rowPitch * cellH;
		const uint32 metricsOff = 36;
		const uint32 metricsSize = proportional ? kGlyphs * 4 : 0;
		const uint32 dataOff = metricsOff + metricsSize;
		const uint32 dataSize = glyphStride * kGlyphs;
		const uint32 cmapOff = dataOff + dataSize;

		Common::Array<byte> b;
		b.resize(cmapOff + kGlyphs * 8);
		for (uint i = 0; i < b.size(); ++i)
			b[i] = 0;

		b[0] = 'S'; b[1] = 'V'; b[2] = 'F'; b[3] = 'N';
		put16(b, 4, 2);
		put16(b, 6, proportional ? 1 : 0);
		b[8] = bpp;
		put16(b, 10, 0);
		put16(b, 12, kGlyphs);
		b[14] = cellW;
		b[15] = cellH;
		b[16] = ascent;
		put32(b, 20, proportional ? metricsOff : 0);
		put32(b, 24, dataOff);
		put32(b, 28, dataSize);
		put32(b, 32, cmapOff);

		if (proportional) {
			for (int i = 0; i < kGlyphs; ++i) {
				b[metricsOff + i * 4 + 0] = 9 + i * 3;  // advance
				b[metricsOff + i * 4 + 1] = 1;          // left bearing
				b[metricsOff + i * 4 + 2] = 12 - i;     // ink width
			}
		}

		for (int i = 0; i < kGlyphs; ++i) {
			for (int y = 2; y < cellH - 2; ++y) {
				byte *row = &b[dataOff + i * glyphStride + y * rowPitch];
				for (int x = 2; x < cellW - 3; ++x) {
					const byte v = (byte)(seed + i * 37 + y * 11 + x * 3 + 1);
					if (((x + y) % 5) == 0)
						continue;
					if (bpp == 8)
						row[x] = v | 1;
					else if (v & 4)
						row[x >> 3] |= 0x80 >> (x & 7);
				}
			}
		}

		for (int i = 0; i < kGlyphs; ++i) {
			put32(b, cmapOff + i * 8, cps[i]);
			put32(b, cmapOff + i * 8 + 4, i);
		}
		return b;
	}

	static bool oldGlyphHasInk(const Graphics::HiResBitmapFont &font, int index) {
		Graphics::GlyphMetrics metrics;
		if (font.isProportional() && font.glyphMetrics(index, metrics))
			return metrics.width > 0 && metrics.height > 0;
		const byte *pixels = font.glyphData(index);
		if (!pixels)
			return false;
		const int bytes = (font.bpp() == 8) ? font.cellWidth() : (font.cellWidth() + 7) / 8;
		for (int y = 0; y < font.cellHeight(); ++y)
			for (int x = 0; x < bytes; ++x)
				if (pixels[y * font.glyphPitch() + x])
					return true;
		return false;
	}

	/// The old blit, straight off HiResBitmapFont: the glyph by index, the
	/// Latin baseline moved onto the CJK font's, the cell handed whole to
	/// the glyph renderer.
	static bool oldDrawChar(Graphics::Surface &dest, Graphics::Surface *cov,
							const Graphics::HiResBitmapFont &cjk,
							const Graphics::HiResBitmapFont &latin,
							uint32 cp, bool wantLatin, int x, int y,
							const Graphics::GlyphStyle &style, Common::Rect *dirty) {
		const Graphics::HiResBitmapFont &font = wantLatin ? latin : cjk;
		const int index = font.glyphIndex(cp);
		if (index < 0 || !oldGlyphHasInk(font, index))
			return false;
		int shift = 0;
		if (wantLatin && font.ascent() > 0 && cjk.ascent() > 0)
			shift = cjk.ascent() - font.ascent();
		return Graphics::HiResGlyphRenderer::drawGlyph(dest, cov, font, index,
													   x, y + shift, style, dirty);
	}

	static Graphics::HiResMap koreanMap(int gameShadow) {
		// The Latin companion sits on the CJK face's baseline
		// unconditionally (the implicit ascent alignment) - no origin=face
		// key is needed to get it.
		Common::String text =
			"[map]\nversion=2\n[render]\nblend=on\n[text]\nencoding=cp949\n"
			"[fonts]\nlat=LAT.SVF\n[font.0]\nface=CJK.SVF\n"
			"[font]\nrange.basic-latin=lat\n"
			"[glyphs]\n0xa1b0 = u+ac00\n";
		Graphics::HiResMap m;
		Common::Array<Common::String> q;
		Common::MemoryReadStream s((const byte *)text.c_str(), text.size());
		TS_ASSERT(Graphics::HiResFontMap::loadMap(s, Common::Path("/tmp/gs", '/'), q, Graphics::kHiResKeysScumm, m));
		return m;
	}

	static bool sameBytes(const Graphics::Surface &a, const Graphics::Surface &b) {
		if (a.w != b.w || a.h != b.h)
			return false;
		for (int y = 0; y < a.h; ++y)
			for (int x = 0; x < a.w; ++x)
				if (*(const byte *)a.getBasePtr(x, y) != *(const byte *)b.getBasePtr(x, y))
					return false;
		return true;
	}

	static int inkCount(const Graphics::Surface &s) {
		int n = 0;
		for (int y = 0; y < s.h; ++y)
			for (int x = 0; x < s.w; ++x)
				n += *(const byte *)s.getBasePtr(x, y) ? 1 : 0;
		return n;
	}

	void checkSvfnMatchesOldPath(int bpp, bool proportional, int gameShadow) {
		const Common::Array<byte> cjkBytes = makeFont(bpp, proportional, 13, 0);
		const Common::Array<byte> latinBytes = makeFont(bpp, proportional, 11, 5);

		Graphics::HiResBitmapFont cjk, latin;
		{
			Common::MemoryReadStream s1(cjkBytes.begin(), cjkBytes.size());
			Common::MemoryReadStream s2(latinBytes.begin(), latinBytes.size());
			TS_ASSERT(cjk.load(s1));
			TS_ASSERT(latin.load(s2));
		}

		Scumm::HiResOverlay overlay;
		overlay.create(96, 40, true);

		Scumm::ScummHiResText hr;
		hr.useOverlay(&overlay);
		// advance.basic-latin= only makes sense with a per-glyph metric on
		// a proportional face; a fixed-width one steps by its cell.
		Graphics::HiResMap m = koreanMap(gameShadow);
		{
			Common::Array<Common::String> w;
			Graphics::HiResRangeSpec spec;
			Common::String err;
			Graphics::parseRangeSpec("basic-latin", spec, err);
			m.font.advanceSpecs.push_back(spec);
			m.font.advanceValues.push_back(proportional ? Graphics::kHiResAdvanceFont : Graphics::kHiResAdvanceCell);
		}
		hr.adoptMap(m);
		{
			Common::MemoryReadStream s1(cjkBytes.begin(), cjkBytes.size());
			Common::MemoryReadStream s2(latinBytes.begin(), latinBytes.size());
			TS_ASSERT(hr.addFace("/tmp/gs/CJK.SVF", s1));
			TS_ASSERT(hr.addFace("/tmp/gs/LAT.SVF", s2));
		}
		TS_ASSERT(hr.hasFonts());

		Graphics::Surface dest, refDest, refCov;
		dest.create(96, 40, Graphics::PixelFormat::createFormatCLUT8());
		refDest.create(96, 40, Graphics::PixelFormat::createFormatCLUT8());
		refCov.create(96, 40, Graphics::PixelFormat::createFormatCLUT8());
		memset(dest.getPixels(), 0, 96 * 40);
		memset(refDest.getPixels(), 0, 96 * 40);
		memset(refCov.getPixels(), 0, 96 * 40);

		Graphics::GlyphStyle style;
		style.color = 15;
		style.shadowColor = 4;
		style.shadowMode = gameShadow == 4 ? Graphics::kHiResShadowOutline
						 : gameShadow == 2 ? Graphics::kHiResShadowDrop
										   : Graphics::kHiResShadowNone;
		Graphics::HiResMap map;
		Graphics::HiResGlyphRenderer::applyMap(style, map, 2);
		TS_ASSERT_EQUALS(style.outlineQ, 6);

		Common::Rect dirty, refDirty;
		TS_ASSERT(hr.drawChar(dest, kGaChr, 0, 3, 4, 15, 4, gameShadow, &dirty));
		TS_ASSERT(oldDrawChar(refDest, &refCov, cjk, latin, 0xAC00, false, 3, 4, style, &refDirty));
		TS_ASSERT(hr.drawChar(dest, 'A', 0, 30, 5, 15, 4, gameShadow, &dirty));
		TS_ASSERT(oldDrawChar(refDest, &refCov, cjk, latin, 'A', true, 30, 5, style, &refDirty));

		TS_ASSERT(!hr.drawChar(dest, 'B', 0, 60, 5, 15, 4, gameShadow, &dirty));

		TS_ASSERT(inkCount(refDest) > 0);
		TS_ASSERT(sameBytes(dest, refDest));
		if (bpp == 1 && gameShadow != 1) {
			const Graphics::Surface &cov = *overlay.coverage();
			for (int y = 0; y < cov.h; ++y)
				for (int x = 0; x < cov.w; ++x) {
					const byte c = *(const byte *)cov.getBasePtr(x, y);
					const byte d = *(const byte *)dest.getBasePtr(x, y);
					TS_ASSERT(c == 0 || c == 0xFF);
					TS_ASSERT_EQUALS(c != 0, d != 0);
				}
		} else {
			TS_ASSERT(sameBytes(*overlay.coverage(), refCov));
		}
		// Every inked pixel lies inside the dirty rect handed back - the
		// old test's exact TS_ASSERT_EQUALS(dirty, refDirty) is not required
		// verbatim (GlyphBitmap here vs. HiResBitmapFont+index in the
		// reference are not bound to grow the rect by the same rounding),
		// but a rect that misses drawn ink (the bug the loosened
		// dirty.contains(refDirty) || refDirty.contains(dirty) check let
		// through) must fail here.
		for (int y = 0; y < dest.h; ++y)
			for (int x = 0; x < dest.w; ++x)
				if (*(const byte *)dest.getBasePtr(x, y))
					TS_ASSERT(dirty.contains(Common::Point(x, y)));

		// The same advance as the old path: the face's own metrics (reach
		// included) for a proportional face, or the narrow half-cell for a
		// fixed-width one - restoring cellRuleAdvance()'s fixed-width branch
		// and glyphInk()'s cellWidth() box together keep this pinned.
		const int advA = hr.advanceFor('A', 0, 3);
		const int refA = proportional ? (MAX(9 + 3, 1 + 11) + 1) / 2 : kCell / 2 / 2;
		TS_ASSERT_EQUALS(advA, MAX(refA, 3));

		dest.free();
		refDest.free();
		refCov.free();
	}

	void checkDecorated1bpp(int gameShadow) {
		const Common::Array<byte> cjkBytes = makeFont(1, false, 13, 0);
		Scumm::HiResOverlay overlay;
		overlay.create(96, 40, true);
		overlay.index().fillRect(Common::Rect(96, 40), 0xFD);

		Scumm::ScummHiResText hr;
		hr.useOverlay(&overlay);
		hr.adoptMap(koreanMap(gameShadow));
		hr.setLayeredDecorations(true);
		{
			Common::MemoryReadStream s1(cjkBytes.begin(), cjkBytes.size());
			TS_ASSERT(hr.addFace("/tmp/gs/CJK.SVF", s1));
		}
		TS_ASSERT(overlay.underCoverage() == nullptr);

		Common::Rect dirty;
		TS_ASSERT(hr.drawChar(overlay.index(), kGaChr, 0, 10, 10, 15, 4, gameShadow, &dirty));
		const Graphics::Surface &idx = overlay.index();
		const Graphics::Surface &cov = *overlay.coverage();
		int ink = 0, covered = 0;
		for (int y = 0; y < 40; ++y)
			for (int x = 0; x < 96; ++x) {
				const byte i = *(const byte *)idx.getBasePtr(x, y);
				const byte c = *(const byte *)cov.getBasePtr(x, y);
				TS_ASSERT(c == 0 || c == 0xFF);
				ink += (i == 15);
				covered += (c != 0);
				if (gameShadow != 1)
					TS_ASSERT_EQUALS(c != 0, i == 15);
			}
		TS_ASSERT(ink > 0);
		if (gameShadow == 1) {
			TS_ASSERT_EQUALS(covered, 0);
			TS_ASSERT(overlay.underCoverage() == nullptr);
			return;
		}
		TS_ASSERT(overlay.underCoverage() != nullptr);
		int under = 0;
		for (int y = 0; y < 40; ++y)
			for (int x = 0; x < 96; ++x) {
				if (*(const byte *)overlay.underCoverage()->getBasePtr(x, y))
					++under;
				TS_ASSERT(*(const byte *)idx.getBasePtr(x, y) != 4);
			}
		TS_ASSERT(under > ink);
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

	void test_drawrows_gives_a_decorated_1bpp_glyph_coverage() {
		checkDecorated1bpp(4);
	}

	void test_drawrows_leaves_a_plain_1bpp_glyph_without_coverage() {
		checkDecorated1bpp(1);
	}

	void test_no_under_planes_unless_layering_is_allowed() {
		const Common::Array<byte> cjkBytes = makeFont(8, false, 13, 0);
		Scumm::HiResOverlay overlay;
		overlay.create(96, 40, true);
		Scumm::ScummHiResText hr;
		hr.useOverlay(&overlay);
		hr.adoptMap(koreanMap(4));
		{
			Common::MemoryReadStream s1(cjkBytes.begin(), cjkBytes.size());
			TS_ASSERT(hr.addFace("/tmp/gs/CJK.SVF", s1));
		}
		TS_ASSERT(hr.drawChar(overlay.index(), kGaChr, 0, 10, 10, 15, 4, 4));
		TS_ASSERT(overlay.underCoverage() == nullptr);
	}

	void test_svfn_path_equals_old_path() {
		checkSvfnMatchesOldPath(8, true, 4);
		checkSvfnMatchesOldPath(8, false, 2);
		checkSvfnMatchesOldPath(1, true, 4);
		checkSvfnMatchesOldPath(1, false, 1);
	}

	/**
	 * C1 regression: a TrueType face named for a game/charset whose cell is
	 * not yet known at loadFonts() time (every non-CJK SCUMM game - and a
	 * UTF-8 translation of one - scumm.cpp only calls setGameFontCell()/
	 * noteGameCharset() for the CJK ones) must not be treated as a load
	 * failure. hasFonts() must stay true so later draws are not gated off
	 * forever, and once noteGameCharset() does learn the cell, the face
	 * opens and actually draws.
	 */
	void test_ttf_face_with_no_cell_known_at_load_still_loads_and_later_draws() {
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
		hr.useOverlay(&overlay);
		const Common::String text = Common::String::format(
			"[map]\nversion=2\n[render]\nblend=off\n[font.4]\nface=%s\n", ttf);
		Graphics::HiResMap m;
		Common::Array<Common::String> q;
		Common::MemoryReadStream s((const byte *)text.c_str(), text.size());
		TS_ASSERT(Graphics::HiResFontMap::loadMap(s, Common::Path("/tmp/c1", '/'), q, Graphics::kHiResKeysScumm, m));
		hr.adoptMap(m);

		// No noteGameCharset()/setCharsetGrid() yet: the id's cell is
		// unknown, exactly the C1 scenario - loadFonts() cannot size or open
		// the face, but that is "pending", not "failed".
		TS_ASSERT(hr.loadFonts(Common::Path()));
		TS_ASSERT(hr.hasFonts());

		// Still nothing to draw with yet.
		Graphics::Surface dest;
		dest.create(64, 40, Graphics::PixelFormat::createFormatCLUT8());
		memset(dest.getPixels(), 0, dest.pitch * dest.h);
		TS_ASSERT(!hr.drawChar(dest, 0xE9, 4, 10, 10, 15, 0, 1));

		// The cell becomes known, as it does when the game selects this
		// charset for the first time.
		hr.noteGameCharset(4, 16, 16);
		hr.setCharsetGrid(4, 16, 16);
		TS_ASSERT(hr.hasFonts());
		TS_ASSERT(hr.drawChar(dest, 0xE9, 4, 10, 10, 15, 0, 1));
		dest.free();
#else
		TS_SKIP("needs FreeType and a real filesystem");
#endif
	}
};
