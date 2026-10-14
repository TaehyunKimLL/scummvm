#include <cxxtest/TestSuite.h>

#include "engines/scumm/charset.h"

/**
 * korean.fnt has 2350 glyphs. A double-byte code the text accepts but the
 * font does not hold (lead 0xC9-0xD0, a UHC trail below 0xA1) used to index
 * past or before the font buffer and draw whatever lay there.
 */
class ScummKoreanFontIndexTestSuite : public CxxTest::TestSuite {
	static int code(int lead, int trail) { return lead | (trail << 8); }

public:
	void test_ks_x_1001_syllables_index_the_font() {
		TS_ASSERT_EQUALS(Scumm::koreanFontIndex(code(0xB0, 0xA1)), 0);		// 가
		TS_ASSERT_EQUALS(Scumm::koreanFontIndex(code(0xB0, 0xFE)), 93);
		TS_ASSERT_EQUALS(Scumm::koreanFontIndex(code(0xB1, 0xA1)), 94);
		TS_ASSERT_EQUALS(Scumm::koreanFontIndex(code(0xC8, 0xFE)), 2349);	// 힝
	}

	void test_codes_outside_the_font_have_no_glyph() {
		TS_ASSERT_EQUALS(Scumm::koreanFontIndex(code(0xC9, 0xA1)), -1);
		TS_ASSERT_EQUALS(Scumm::koreanFontIndex(code(0xD0, 0xFE)), -1);
		TS_ASSERT_EQUALS(Scumm::koreanFontIndex(code(0xB0, 0x41)), -1);	// UHC trail
		TS_ASSERT_EQUALS(Scumm::koreanFontIndex(code(0xB0, 0xA0)), -1);
		TS_ASSERT_EQUALS(Scumm::koreanFontIndex(code(0xB0, 0xFF)), -1);
		TS_ASSERT_EQUALS(Scumm::koreanFontIndex(code(0xAF, 0xA1)), -1);
	}
};
