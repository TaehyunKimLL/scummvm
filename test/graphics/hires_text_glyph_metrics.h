#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/hashmap.h"
#include "common/memstream.h"
#include "common/str.h"
#include "common/stream.h"
#include "graphics/hires_text/bitmap_font.h"
#include "graphics/hires_text/glyph_source.h"
#include "graphics/hires_text/glyph_source_routed.h"
#include "graphics/hires_text/glyph_source_scvmuni.h"
#include "graphics/hires_text/glyph_source_svfn.h"
#include "graphics/hires_text/glyph_source_ttf.h"

#include "../system/null_osystem.h"

#ifdef USE_FREETYPE2
#include "common/fs.h"
#endif

/**
 * Per-glyph metrics (UnicodeGlyphSource::metrics(), I18N_TEXT_DESIGN.md
 * section 4.2): the default derived from cells(), the SCVMUNI, SVFN and
 * routed overrides, and TrueType's pen origin for ink left of it.
 */

namespace {

/** A source that knows only cells(): the default metrics() applies. */
class CellsOnlySource : public Graphics::UnicodeGlyphSource {
public:
	Common::HashMap<uint32, int> cellMap;
	int fixedAdvance = 0;	///< what advance() reports for every glyph

	byte cellWidth() const override { return 16; }
	byte cellHeight() const override { return 16; }
	byte advanceNarrow() const override { return 8; }
	byte advanceWide() const override { return 16; }
	int bitsPerPixel() const override { return 8; }
	int cells(uint32 cp) override {
		Common::HashMap<uint32, int>::const_iterator it = cellMap.find(cp);
		return it == cellMap.end() ? 0 : it->_value;
	}
	const byte *row(uint32, int) override {
		static byte blank[32] = { 0 };
		return blank;
	}
	int advance(uint32 cp) override { return cells(cp) ? fixedAdvance : 0; }
	uint32 glyphCount() const override { return cellMap.size(); }
};

void mPut16(Common::Array<byte> &b, uint pos, uint16 v) {
	b[pos] = v & 0xff;
	b[pos + 1] = (v >> 8) & 0xff;
}

void mPut32(Common::Array<byte> &b, uint pos, uint32 v) {
	for (int i = 0; i < 4; i++)
		b[pos + i] = (v >> (8 * i)) & 0xff;
}

/**
 * A proportional 8bpp version 2 SVFN file, cell 8x8, of two glyphs:
 * U+0041 (advance 6, bearing +1) and U+0E48 (advance 0, bearing -3, stored
 * as the signed byte 0xFD the format specifies). Every pixel of column 0
 * carries ink, so the row read-back can be checked.
 */
Common::Array<byte> makeSvfn() {
	const int cellW = 8, cellH = 8, glyphs = 2;
	const uint32 glyphStride = cellW * cellH;
	const uint32 metricsOff = 36, dataOff = metricsOff + glyphs * 4;
	const uint32 cmapOff = dataOff + glyphStride * glyphs;
	Common::Array<byte> b;
	b.resize(cmapOff + glyphs * 8, 0);
	b[0] = 'S'; b[1] = 'V'; b[2] = 'F'; b[3] = 'N';
	mPut16(b, 4, 2);
	mPut16(b, 6, 1);	// proportional
	b[8] = 8;
	mPut16(b, 12, glyphs);
	b[14] = cellW;
	b[15] = cellH;
	b[16] = cellH - 1;
	mPut32(b, 20, metricsOff);
	mPut32(b, 24, dataOff);
	mPut32(b, 28, glyphStride * glyphs);
	mPut32(b, 32, cmapOff);
	b[metricsOff + 0] = 6; b[metricsOff + 1] = 1;    b[metricsOff + 2] = 4;
	b[metricsOff + 4] = 0; b[metricsOff + 5] = 0xFD; b[metricsOff + 6] = 3;
	for (int g = 0; g < glyphs; g++)
		for (int y = 0; y < cellH; y++)
			b[dataOff + g * glyphStride + y * cellW] = 200;
	mPut32(b, cmapOff + 0, 0x0041);
	mPut32(b, cmapOff + 4, 0);
	mPut32(b, cmapOff + 8, 0x0E48);
	mPut32(b, cmapOff + 12, 1);
	return b;
}

/** A 1bpp SCVMUNI buffer, cell 4x2, advanceNarrow 3, advanceWide 7 (not
 *  twice the narrow one), glyphs U+0041 (1 cell) and U+AC00 (2 cells). */
Common::Array<byte> makeScvmuni() {
	Common::Array<byte> d;
	const char magic[8] = { 'S', 'C', 'V', 'M', 'U', 'N', 'I', 0 };
	for (int i = 0; i < 8; i++)
		d.push_back(magic[i]);
	d.push_back(1); d.push_back(0);	// version 1
	d.push_back(0); d.push_back(0);	// flags: 1bpp
	d.push_back(4);					// cellWidth
	d.push_back(2);					// cellHeight
	d.push_back(3);					// advanceNarrow
	d.push_back(7);					// advanceWide
	const uint32 glyphs = 2, cpOff = 32, wOff = cpOff + glyphs * 4, bmOff = wOff + glyphs;
	const uint32 vals[4] = { glyphs, cpOff, wOff, bmOff };
	for (int v = 0; v < 4; v++)
		for (int i = 0; i < 4; i++)
			d.push_back((vals[v] >> (8 * i)) & 0xff);
	const uint32 cps[2] = { 0x0041, 0xAC00 };
	for (int g = 0; g < 2; g++)
		for (int i = 0; i < 4; i++)
			d.push_back((cps[g] >> (8 * i)) & 0xff);
	d.push_back(1);
	d.push_back(2);
	for (uint32 i = 0; i < glyphs * 2; i++)	// rowBytes 1 x cellHeight 2
		d.push_back(0x80);
	return d;
}

uint32 fnvRows(Graphics::UnicodeGlyphSource &src, uint32 cp) {
	uint32 h = 2166136261u;
	for (int y = 0; y < src.cellHeight(); y++) {
		const byte *r = src.row(cp, y);
		for (int x = 0; x < src.cellWidth() * 2; x++)
			h = (h ^ (r ? r[x] : 0)) * 16777619u;
	}
	return h;
}

} // anonymous namespace

class HiResTextGlyphMetricsTestSuite : public CxxTest::TestSuite {
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

	void test_default_metrics_from_cells() {
		CellsOnlySource src;
		src.cellMap[0xAC00] = 2;
		src.cellMap[0x0041] = 1;
		src.cellMap[0x0E48] = 1;

		Graphics::GlyphMetrics m;
		TS_ASSERT(src.metrics(0xAC00, m));
		TS_ASSERT_EQUALS(m.advance, 16);
		TS_ASSERT_EQUALS(m.originX, 0);
		TS_ASSERT(m.wide);
		TS_ASSERT(!m.combining);

		TS_ASSERT(src.metrics(0x0041, m));
		TS_ASSERT_EQUALS(m.advance, 8);
		TS_ASSERT_EQUALS(m.originX, 0);
		TS_ASSERT(!m.wide);
		TS_ASSERT(!m.combining);

		TS_ASSERT(src.metrics(0x0E48, m));
		TS_ASSERT_EQUALS(m.advance, 0);
		TS_ASSERT(m.combining);
		TS_ASSERT_EQUALS(m.originX, 0);

		TS_ASSERT(!src.metrics(0x0E01, m));	// no glyph
	}

	void test_default_metrics_prefer_the_faces_advance() {
		CellsOnlySource src;
		src.cellMap[0x0041] = 1;
		src.cellMap[0x0E48] = 1;
		src.fixedAdvance = 5;

		Graphics::GlyphMetrics m;
		TS_ASSERT(src.metrics(0x0041, m));
		TS_ASSERT_EQUALS(m.advance, 5);
		TS_ASSERT(src.metrics(0x0E48, m));
		TS_ASSERT_EQUALS(m.advance, 0);	// a mark never advances
	}

	void test_scvmuni_metrics_use_the_bundles_advances() {
		Common::String error;
		Common::Array<byte> data = makeScvmuni();
		Graphics::ScvmuniGlyphSource *src = Graphics::ScvmuniGlyphSource::create(Common::move(data), "t.uni", error);
		TS_ASSERT(src != nullptr);
		if (!src)
			return;
		Graphics::GlyphMetrics m;
		TS_ASSERT(src->metrics(0x0041, m));
		TS_ASSERT_EQUALS(m.advance, 3);
		TS_ASSERT_EQUALS(m.originX, 0);
		TS_ASSERT(src->metrics(0xAC00, m));
		TS_ASSERT_EQUALS(m.advance, 7);	// advanceWide, not 2 * advanceNarrow
		TS_ASSERT(m.wide);
		TS_ASSERT(!src->metrics(0x0E01, m));
		delete src;
	}

	void test_svfn_metrics_origin_from_negative_bearing() {
		Common::Array<byte> bytes = makeSvfn();
		Graphics::HiResBitmapFont *font = new Graphics::HiResBitmapFont();
		Common::MemoryReadStream stream(bytes.begin(), bytes.size());
		TS_ASSERT(font->load(stream));
		Graphics::SvfnGlyphSource src(font, DisposeAfterUse::YES);

		Graphics::GlyphMetrics m;
		TS_ASSERT(src.metrics(0x0041, m));
		TS_ASSERT_EQUALS(m.advance, 6);
		TS_ASSERT_EQUALS(m.originX, 0);	// bearing +1: ink right of the origin
		TS_ASSERT(!m.combining);

		TS_ASSERT(src.metrics(0x0E48, m));
		TS_ASSERT_EQUALS(m.advance, 0);
		TS_ASSERT_EQUALS(m.originX, 3);	// bearing -3
		TS_ASSERT(m.combining);
		TS_ASSERT(!m.wide);

		TS_ASSERT(!src.metrics(0x0E01, m));
	}

	void test_routed_metrics_follow_the_route() {
		CellsOnlySource *mainSrc = new CellsOnlySource();
		CellsOnlySource *latinSrc = new CellsOnlySource();
		mainSrc->cellMap[0x0041] = 1;
		mainSrc->cellMap[0xAC00] = 2;
		mainSrc->fixedAdvance = 11;
		latinSrc->cellMap[0x0041] = 1;
		latinSrc->fixedAdvance = 4;
		Graphics::RoutedGlyphSource routed(mainSrc, latinSrc, Graphics::kHiResLatinHalf);

		Graphics::GlyphMetrics m;
		TS_ASSERT(routed.metrics(0x0041, m));
		TS_ASSERT_EQUALS(m.advance, 4);	// from the latin source
		TS_ASSERT(routed.metrics(0xAC00, m));
		TS_ASSERT_EQUALS(m.advance, 11);	// from the main source
		TS_ASSERT(!routed.metrics(0x0E01, m));
	}

	// Sukhumvit Set, face 0, 24 px: the face the design measured (U+0E48:
	// advance 0, ink box left -4). Fixtures below were captured from
	// TtfGlyphSource::row() of the base build (i18n bee83518ad), before
	// originX existed: FNV-1a over cellHeight rows of cellWidth*2 bytes.
	Common::SeekableReadStream *openThaiFont(bool &haveFixture) {
		haveFixture = false;
#ifdef USE_FREETYPE2
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::FSNode ttc("/System/Library/Fonts/Supplemental/SukhumvitSet.ttc");
		if (ttc.exists()) {
			haveFixture = true;
			return ttc.createReadStream();
		}
		// Plan 6 Task 4's extracted face: same marks, no fixture for it.
		Common::FSNode ttf("/Users/juami/work/scummvm/runs/c11/data/fonts/sukhumvit-text.ttf");
		if (ttf.exists())
			return ttf.createReadStream();
		TS_SKIP("no Sukhumvit face (SukhumvitSet.ttc or Task 4's sukhumvit-text.ttf) on this machine");
		return nullptr;
#else
		TS_SKIP("no real filesystem access in this test environment");
		return nullptr;
#endif
#else
		TS_SKIP("this build has no FreeType");
		return nullptr;
#endif
	}

	void test_ttf_metrics_origin() {
		bool haveFixture = false;
		Common::SeekableReadStream *stream = openThaiFont(haveFixture);
		if (!stream)
			return;
		Common::String error;
		Graphics::TtfGlyphSource *src = Graphics::TtfGlyphSource::create(stream, DisposeAfterUse::YES, 24, error);
		TS_ASSERT(src != nullptr);
		if (!src)
			return;

		// The tone mark: drawn with its origin at column originX, not clipped.
		Graphics::GlyphMetrics m;
		TS_ASSERT(src->metrics(0x0E48, m));
		TS_ASSERT(m.originX > 0);
		TS_ASSERT_EQUALS(m.advance, 0);
		TS_ASSERT(m.combining);
		TS_ASSERT_EQUALS(src->cells(0x0E48), 1);
		bool inkLeftOfOrigin = false;
		for (int y = 0; y < src->cellHeight(); y++) {
			const byte *r = src->row(0x0E48, y);
			TS_ASSERT(r != nullptr);
			for (int x = 0; r && x < m.originX; x++)
				if (r[x])
					inkLeftOfOrigin = true;
		}
		TS_ASSERT(inkLeftOfOrigin);

		// A base consonant and a Latin capital: ink right of the origin, so
		// origin at column 0 and the same row bytes as before.
		const uint32 cps[2] = { 0x0E01, 0x0041 };
		const uint32 fixture[2] = { 3444642818u, 1439034942u };
		for (int i = 0; i < 2; i++) {
			TS_ASSERT(src->metrics(cps[i], m));
			TS_ASSERT_EQUALS(m.originX, 0);
			TS_ASSERT(!m.combining);
			TS_ASSERT_EQUALS(m.advance, src->advance(cps[i]));
			if (haveFixture && src->cellWidth() == 24 && src->cellHeight() == 24)
				TSM_ASSERT_EQUALS(Common::String::format("U+%04X", cps[i]).c_str(), fnvRows(*src, cps[i]), fixture[i]);
		}

		// 'j' reaches left of its origin with its descender: it moves right
		// by originX and keeps the ink the base build clipped (49 lit
		// pixels there, face 0 at 24 px).
		TS_ASSERT(src->metrics(0x006A, m));
		TS_ASSERT(m.originX > 0);
		TS_ASSERT(!m.combining);
		int lit = 0;
		for (int y = 0; y < src->cellHeight(); y++) {
			const byte *r = src->row(0x006A, y);
			for (int x = 0; r && x < src->cellWidth() * 2; x++)
				if (r[x])
					lit++;
		}
		if (haveFixture && src->cellWidth() == 24)
			TS_ASSERT(lit > 49);
		delete src;
	}

	// The Korean face of the TTF path: no Hangul syllable has ink left of
	// its origin, so no wide glyph moves; of printable ASCII only 'j' does
	// (its descender reaches left of the origin).
	void test_ttf_hangul_keeps_origin_zero() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		Common::FSNode node("/System/Library/Fonts/AppleSDGothicNeo.ttc");
		if (!node.exists()) {
			TS_SKIP("Apple SD Gothic Neo not present on this machine");
			return;
		}
		Common::String error;
		Graphics::TtfGlyphSource *src = Graphics::TtfGlyphSource::create(node.createReadStream(), DisposeAfterUse::YES, 16, error);
		TS_ASSERT(src != nullptr);
		if (!src)
			return;
		Graphics::GlyphMetrics m;
		uint32 moved = 0, first = 0;
		for (uint32 cp = 0x21; cp <= 0xD7A3; cp = (cp == 0x7E) ? 0xAC00 : cp + 1) {
			if (src->metrics(cp, m) && m.originX != 0 && cp != 0x006A) {
				if (!moved)
					first = cp;
				moved++;
			}
		}
		TSM_ASSERT_EQUALS(Common::String::format("first U+%04X", first).c_str(), moved, 0u);
		TS_ASSERT(src->metrics(0x006A, m));
		TS_ASSERT(m.originX > 0);
		delete src;
#else
		TS_SKIP("needs FreeType and a real filesystem");
#endif
	}
};
