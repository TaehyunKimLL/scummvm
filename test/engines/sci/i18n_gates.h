#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/fs.h"
#include "common/str.h"
#include "common/stream.h"
#include "common/system.h"
#include "graphics/hires_text/glyph_source.h"
#include "graphics/hires_text/glyph_source_fallback.h"
#include "graphics/hires_text/glyph_source_ttf.h"
#include "graphics/hires_text/text_layout.h"
#include "graphics/hires_text/unicode_props.h"
#include "sci/graphics/textlayout16.h"
#include "sci/utf8.h"

/**
 * C11 Task 5: SCI decides hi-res text by the translation, measures glyphs
 * per glyph, and breaks UTF-8 lines with the shared layout stage
 * (I18N_TEXT_DESIGN.md sections 4.1, 4.2, 4.3).
 */
class SciI18nGatesTestSuite : public CxxTest::TestSuite {
public:
	void setUp() {
		if (!g_system)
			Common::install_null_g_system();
	}

	void tearDown() {
		Common::uninstall_null_g_system();
	}

	// --- the gate -------------------------------------------------------

	void test_hires_text_applies() {
		Common::String why;
		TS_ASSERT(!Sci::hiresTextApplies(Sci::SCI_VERSION_1_1, Common::kLatin1, false, why));
		TS_ASSERT_EQUALS(why, Common::String("no translation and no CJK code page"));

		why.clear();
		TS_ASSERT(Sci::hiresTextApplies(Sci::SCI_VERSION_1_1, Common::kLatin1, true, why));
		TS_ASSERT(Sci::hiresTextApplies(Sci::SCI_VERSION_1_1, Common::kWindows949, false, why));
		TS_ASSERT(Sci::hiresTextApplies(Sci::SCI_VERSION_01, Common::kWindows932, false, why));
		TS_ASSERT(Sci::hiresTextApplies(Sci::SCI_VERSION_0_EARLY, Common::kWindows936, false, why));
		TS_ASSERT(Sci::hiresTextApplies(Sci::SCI_VERSION_1_LATE, Common::kWindows950, false, why));

		TS_ASSERT(!Sci::hiresTextApplies(Sci::SCI_VERSION_2, Common::kWindows949, true, why));
		TS_ASSERT_EQUALS(why, Common::String("SCI32 games do not support it yet"));
	}

	// --- the advance rule -----------------------------------------------

	void test_game_advance() {
		Graphics::GlyphMetrics hangul;
		hangul.advance = 16;
		hangul.wide = true;
		// The cell rule of today: a wide glyph advances by the wide cell.
		TS_ASSERT_EQUALS(Sci::gameAdvance(hangul, 4, 8, 2), 8);

		Graphics::GlyphMetrics mark;
		mark.advance = 0;
		mark.combining = true;
		TS_ASSERT_EQUALS(Sci::gameAdvance(mark, 4, 8, 2), 0);
		// Even a mark whose face gives it an advance (Thonburi) does not move.
		mark.advance = 12;
		TS_ASSERT_EQUALS(Sci::gameAdvance(mark, 4, 8, 2), 0);

		Graphics::GlyphMetrics koKai;      // U+0E01
		koKai.advance = 14;
		TS_ASSERT_EQUALS(Sci::gameAdvance(koKai, 4, 8, 2), 7);

		Graphics::GlyphMetrics saraAm;     // U+0E33, rounds half up
		saraAm.advance = 25;
		TS_ASSERT_EQUALS(Sci::gameAdvance(saraAm, 4, 8, 2), 13);

		// A face that cannot say keeps the narrow cell.
		Graphics::GlyphMetrics unknown;
		TS_ASSERT_EQUALS(Sci::gameAdvance(unknown, 4, 8, 2), 4);
	}

	// --- a bitmap bundle behind a TrueType face ---------------------------

	/** 8x8 at 1 bpp; 'A' has its first column-byte set in every row. */
	class OneBitSource : public Graphics::UnicodeGlyphSource {
	public:
		OneBitSource() { _row[0] = 0xF0; _row[1] = 0x01; }
		byte cellWidth() const override { return 8; }
		byte cellHeight() const override { return 8; }
		byte advanceNarrow() const override { return 8; }
		byte advanceWide() const override { return 16; }
		int bitsPerPixel() const override { return 1; }
		int cells(uint32 cp) override { return cp == 'A' ? 1 : 0; }
		const byte *row(uint32 cp, int y) override { return _row; }
		uint32 glyphCount() const override { return 1; }
	private:
		byte _row[2];
	};

	void test_normalized_source_pads_a_smaller_cell_at_8bpp() {
		Common::String error;
		Graphics::NormalizedGlyphSource *n =
			Graphics::NormalizedGlyphSource::create(new OneBitSource(), 12, 10, DisposeAfterUse::YES, error);
		TS_ASSERT(n);
		if (!n)
			return;
		TS_ASSERT_EQUALS(n->cellWidth(), 12);
		TS_ASSERT_EQUALS(n->cellHeight(), 10);
		TS_ASSERT_EQUALS(n->bitsPerPixel(), 8);
		TS_ASSERT_EQUALS(n->cells('A'), 1);
		TS_ASSERT_EQUALS(n->advanceNarrow(), 8);

		const byte *r = n->row('A', 3);
		for (int x = 0; x < 24; x++) {
			// 0xF0 0x01 at 1 bpp: pixels 0-3 and 15 set; 16..23 are padding.
			const byte want = (x < 4 || x == 15) ? 255 : 0;
			TS_ASSERT_EQUALS(r[x], want);
		}
		// Rows below the source's cell are empty.
		r = n->row('A', 9);
		for (int x = 0; x < 24; x++)
			TS_ASSERT_EQUALS(r[x], 0);

		// It joins a chain of a 12x10, 8 bpp face.
		Common::Array<Graphics::UnicodeGlyphSource *> chain;
		chain.push_back(n);
		Graphics::FallbackGlyphSource fb(chain, DisposeAfterUse::YES);
		TS_ASSERT_EQUALS(fb.cells('A'), 1);
	}

	void test_normalized_source_refuses_a_larger_cell() {
		Common::String error;
		TS_ASSERT(!Graphics::NormalizedGlyphSource::create(new OneBitSource(), 6, 10, DisposeAfterUse::YES, error));
		TS_ASSERT(!error.empty());
	}

	// SARA AM (U+0E33) is a spacing vowel (Lo) whose nikhahit ring sits over
	// the base before it, left of its own origin: like a combining mark it
	// is drawn with its origin moved right, so the ring is not cut off.
	void test_ttf_sara_am_keeps_its_ring() {
#ifdef USE_FREETYPE2
		Common::FSNode node("/Users/juami/work/scummvm/runs/c11/data/fonts/sukhumvit-text.ttf");
		if (!node.exists())
			return;
		Common::String error;
		Graphics::TtfGlyphSource *src = Graphics::TtfGlyphSource::create(node.createReadStream(), DisposeAfterUse::YES,
																		  16, error, false);
		TS_ASSERT(src);
		if (!src)
			return;
		Graphics::GlyphMetrics am, aa;
		TS_ASSERT(src->metrics(0x0E33, am));
		TS_ASSERT(src->metrics(0x0E32, aa));
		TS_ASSERT(am.originX > 0);
		TS_ASSERT(!am.combining);
		TS_ASSERT(am.advance > 0);
		TS_ASSERT_EQUALS(aa.originX, 0);   // SARA AA is left alone
		delete src;
#endif
	}
};
