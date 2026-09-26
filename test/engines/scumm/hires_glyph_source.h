#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/memstream.h"
#include "graphics/hires_text/bitmap_font.h"
#include "graphics/hires_text/glyph_renderer.h"
#include "graphics/hires_text/glyph_source_ttf.h"
#include "graphics/surface.h"

#include "engines/scumm/hires_overlay.h"
#include "engines/scumm/hires_text.h"

#include "../../system/null_osystem.h"

/**
 * SCUMM's hi-res text drawing from the shared glyph sources.
 *
 * The bitmap (SVFN) fonts a translation ships used to be blitted straight out
 * of HiResBitmapFont; they now go through SvfnGlyphSource like every other
 * engine's. The first test pins that the move changed no pixel: the old blit
 * is kept below as a helper and both are run on the same font. A TrueType
 * face is no longer baked at start-up but opened once per pixel size and
 * rasterised one code point at a time, which the second test counts.
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

	/// "가" as printChar() hands it over: the lead byte 0xB0 in the low half.
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

		// Ink with a gap in it, so the outline mask has inner edges too.
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

	/**
	 * The old path's glyph test, copied from hires_text.cpp before the move:
	 * a proportional font answers from its metrics, a fixed one is scanned.
	 */
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

	/**
	 * The old blit, as ScummHiResText::drawChar() did it with the fonts held
	 * as HiResBitmapFont: the glyph by index, the Latin baseline moved onto
	 * the CJK font's, and the cell handed whole to the glyph renderer.
	 */
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

	static Graphics::HiResTextConfig koreanConfig() {
		Graphics::HiResTextConfig c;
		c.scale = 2;
		c.alpha = true;
		c.encoding = Common::kWindows949;
		// Route the CP949 pair straight to U+AC00, so the test does not
		// depend on encoding.dat being where the runner looks.
		c.glyphOverrides[kGaChr] = Graphics::HiResGlyphOverride(Graphics::kHiResGlyphRemap, 0xAC00);
		return c;
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
		hr.adoptConfig(koreanConfig());
		{
			Common::MemoryReadStream s1(cjkBytes.begin(), cjkBytes.size());
			Common::MemoryReadStream s2(latinBytes.begin(), latinBytes.size());
			TS_ASSERT(hr.addBitmapFont(0, false, s1, "t00.fnt"));
			TS_ASSERT(hr.addBitmapFont(0, true, s2, "l00.fnt"));
		}
		TS_ASSERT(hr.hasFonts());
		TS_ASSERT(hr.sourceFor(0, false) != nullptr);
		TS_ASSERT(hr.sourceFor(0, true) != nullptr);

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
		// gameShadow 4 is the engine's outline, 2 a drop shadow, 1 none.
		style.shadowMode = gameShadow == 4 ? Graphics::kHiResShadowOutline
						 : gameShadow == 2 ? Graphics::kHiResShadowDrop
										   : Graphics::kHiResShadowNone;
		style.shadowOffset = 1;

		Common::Rect dirty, refDirty;
		TS_ASSERT(hr.drawChar(dest, kGaChr, 0, 3, 4, 15, 4, gameShadow, &dirty));
		TS_ASSERT(oldDrawChar(refDest, &refCov, cjk, latin, 0xAC00, false, 3, 4, style, &refDirty));
		TS_ASSERT(hr.drawChar(dest, 'A', 0, 30, 5, 15, 4, gameShadow, &dirty));
		TS_ASSERT(oldDrawChar(refDest, &refCov, cjk, latin, 'A', true, 30, 5, style, &refDirty));

		// A code point neither font has is declined by both.
		TS_ASSERT(!hr.drawChar(dest, 'B', 0, 60, 5, 15, 4, gameShadow, &dirty));

		TS_ASSERT(inkCount(refDest) > 0);
		TS_ASSERT(sameBytes(dest, refDest));
		TS_ASSERT(sameBytes(*overlay.coverage(), refCov));
		TS_ASSERT_EQUALS(dirty, refDirty);

		// The same advance as the old path: metrics (reach included) or the cell.
		const int advA = hr.advanceFor('A', 0, 3);
		const int refA = proportional ? (MAX(9 + 3, 1 + 11) + 1) / 2 : kCell / 2;
		TS_ASSERT_EQUALS(advA, MAX(refA, 3));

		dest.free();
		refDest.free();
		refCov.free();
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

	void test_svfn_path_equals_old_path() {
		// The brief's case: 8bpp, 16x16, U+AC00 and U+0041.
		checkSvfnMatchesOldPath(8, true, 4);
		// And the variants the old path treated differently: a fixed-width
		// set (ink found by scanning), a 1bpp stencil (no coverage written),
		// a drop shadow and none.
		checkSvfnMatchesOldPath(8, false, 2);
		checkSvfnMatchesOldPath(1, true, 4);
		checkSvfnMatchesOldPath(1, false, 1);
	}

	void test_ttf_source_shared_per_size() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		static const char *const kTtc = "/System/Library/Fonts/AppleSDGothicNeo.ttc";
		Common::FSNode node(kTtc);
		if (!node.exists()) {
			TS_SKIP("Apple SD Gothic Neo not present on this machine");
			return;
		}

		Scumm::HiResOverlay overlay;
		overlay.create(96, 40, true);

		Scumm::ScummHiResText hr;
		hr.useOverlay(&overlay);
		hr.adoptConfig(koreanConfig());
		hr.setTtfFace(Common::Path(kTtc, '/'));
		// Two charsets on the same 8px game cell: one face at 16px.
		hr.setGameFontCell(0, 8, 8);
		hr.setGameFontCell(1, 8, 8);
		TS_ASSERT(hr.loadFonts(Common::Path()));
		TS_ASSERT_EQUALS(hr.sourceCount(), 1);

		Graphics::UnicodeGlyphSource *s0 = hr.sourceFor(0, false);
		Graphics::UnicodeGlyphSource *s1 = hr.sourceFor(1, false);
		TS_ASSERT(s0 != nullptr);
		TS_ASSERT_EQUALS(s0, s1);
		// Latin comes from the same face at the same size.
		TS_ASSERT_EQUALS(s0, hr.sourceFor(0, true));
		if (!s0)
			return;
		TS_ASSERT_EQUALS((int)s0->cellHeight(), 16);

		const Graphics::TtfGlyphSource *ttf = static_cast<const Graphics::TtfGlyphSource *>(s0);
		const uint32 probes = ttf->rasterCount();
		TS_ASSERT_EQUALS(ttf->glyphCount(), (uint32)0);

		Graphics::Surface dest;
		dest.create(96, 40, Graphics::PixelFormat::createFormatCLUT8());
		memset(dest.getPixels(), 0, 96 * 40);

		// "가가", once in each charset.
		TS_ASSERT(hr.drawChar(dest, kGaChr, 0, 2, 2, 15, 0, 1));
		TS_ASSERT(hr.drawChar(dest, kGaChr, 1, 30, 2, 15, 0, 1));
		TS_ASSERT_EQUALS(ttf->rasterCount(), probes + 1);
		TS_ASSERT_EQUALS(ttf->glyphCount(), (uint32)1);
		TS_ASSERT(inkCount(dest) > 0);
		TS_ASSERT_EQUALS(hr.sourceCount(), 1);

		// A charset on another cell opens the face again, at its own size.
		hr.setGameFontCell(2, 12, 12);
		Graphics::UnicodeGlyphSource *s2 = hr.sourceFor(2, false);
		TS_ASSERT(s2 != nullptr);
		TS_ASSERT(s2 != s0);
		if (s2)
			TS_ASSERT_EQUALS((int)s2->cellHeight(), 24);
		TS_ASSERT_EQUALS(hr.sourceCount(), 2);

		dest.free();
#else
		TS_SKIP("needs FreeType and a real filesystem");
#endif
	}

	void test_map_without_bitmap_but_ttf_does_not_warn() {
		Graphics::HiResTextConfig c;
		const Common::Path none;
		const Common::Path face("/System/Library/Fonts/AppleSDGothicNeo.ttc", '/');

		// Nothing named at all: the older TrueType-era map, worth a warning.
		TS_ASSERT(Scumm::ScummHiResText::mapNamesNoFonts(c, none));

		// A face and no [bitmap]: the layer does use it, so no warning.
		TS_ASSERT(!Scumm::ScummHiResText::mapNamesNoFonts(c, face));

		Graphics::HiResTextConfig p;
		p.bitmapPattern = "korean%02d.fnt";
		TS_ASSERT(!Scumm::ScummHiResText::mapNamesNoFonts(p, none));

		Graphics::HiResTextConfig s;
		s.bitmapSingle = "hires.fnt";
		TS_ASSERT(!Scumm::ScummHiResText::mapNamesNoFonts(s, none));

		Graphics::HiResTextConfig l;
		l.legacy.latinBitmapName = "hrlat%02d.fnt";
		TS_ASSERT(!Scumm::ScummHiResText::mapNamesNoFonts(l, none));
	}
};
