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
};
