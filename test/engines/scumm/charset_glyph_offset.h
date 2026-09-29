#include <cxxtest/TestSuite.h>

#include "engines/scumm/charset.h"

/**
 * A classic SCUMM font's offset table has one entry per character it has,
 * and a code past the end has no glyph.
 *
 * Loading a Korean Monkey Island 2 save into the English game redraws the
 * saved Korean verbs with the English charset 6, which has 123 characters:
 * measuring the verb's first byte, 193, read glyph bitmap bytes as an offset
 * and crashed. Such a character now measures and draws as missing, like any
 * character a font has no glyph for; the ones it has are read as before.
 */
class ScummCharsetGlyphOffsetTestSuite : public CxxTest::TestSuite {
public:
	void test_codes_past_the_table_have_no_glyph() {
		// _fontPtr layout: bpp, height, numChars (LE16), then numChars
		// offsets (LE32, from _fontPtr), then the glyphs.
		static const byte kFont[] = {
			1, 8, 3, 0,
			0x10, 0, 0, 0,		// 0 -> 16
			0, 0, 0, 0,			// 1: none
			0x14, 0, 0, 0,		// 2 -> 20
			// glyph data right after the table: read as an offset for
			// code 3 it would be 0xfffefdfc
			0xfc, 0xfd, 0xfe, 0xff,
			2, 8, 0, 0
		};

		TS_ASSERT_EQUALS(Scumm::CharsetRendererClassic::glyphOffset(kFont, 3, 0), 0x10u);
		TS_ASSERT_EQUALS(Scumm::CharsetRendererClassic::glyphOffset(kFont, 3, 1), 0u);
		TS_ASSERT_EQUALS(Scumm::CharsetRendererClassic::glyphOffset(kFont, 3, 2), 0x14u);

		TS_ASSERT_EQUALS(Scumm::CharsetRendererClassic::glyphOffset(kFont, 3, 3), 0u);
		TS_ASSERT_EQUALS(Scumm::CharsetRendererClassic::glyphOffset(kFont, 3, 193), 0u);
		TS_ASSERT_EQUALS(Scumm::CharsetRendererClassic::glyphOffset(kFont, 3, -1), 0u);
	}
};
