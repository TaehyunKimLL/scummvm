#include <cxxtest/TestSuite.h>

#include "common/str.h"
#include "graphics/hires_text/font_descriptor.h"

/**
 * The one-line font descriptors translations ship next to a game font
 * (Grim's "<font>.laf.txt": "D2Coding.ttf 17px"). The files come with a
 * patch, so the parser must reject anything odd instead of reading past it.
 */
class HiResTextFontDescriptorTestSuite : public CxxTest::TestSuite {
public:
	static bool parse(const char *line, Common::String &face, int &px) {
		face = "unchanged";
		px = -99;
		return Graphics::parseFontDescriptor(line, face, px);
	}

	void test_shipped_forms() {
		Common::String face;
		int px;
		TS_ASSERT(parse("D2Coding-Ver1.3.2-20180524.ttf 17px", face, px));
		TS_ASSERT_EQUALS(face, "D2Coding-Ver1.3.2-20180524.ttf");
		TS_ASSERT_EQUALS(px, 17);
		TS_ASSERT(parse("x.ttf 12px", face, px));
		TS_ASSERT_EQUALS(face, "x.ttf");
		TS_ASSERT_EQUALS(px, 12);
		TS_ASSERT(parse("x.svfn 12", face, px));
		TS_ASSERT_EQUALS(face, "x.svfn");
		TS_ASSERT_EQUALS(px, 12);
		// Line ends and surrounding blanks are trimmed.
		TS_ASSERT(parse("  x.ttf\t19px \r", face, px));
		TS_ASSERT_EQUALS(face, "x.ttf");
		TS_ASSERT_EQUALS(px, 19);
		// The face is everything before the last blank, as before.
		TS_ASSERT(parse("My Font.ttf 11px", face, px));
		TS_ASSERT_EQUALS(face, "My Font.ttf");
		TS_ASSERT_EQUALS(px, 11);
		TS_ASSERT(parse("x.ttf 1px", face, px));
		TS_ASSERT(parse("x.ttf 128px", face, px));
		TS_ASSERT_EQUALS(px, 128);
	}

	void test_rejected() {
		Common::String face;
		int px;
		TS_ASSERT(!parse("D2Coding.ttf ", face, px));   // trailing blank, no size
		TS_ASSERT(!parse("D2Coding.ttf", face, px));    // no size at all
		TS_ASSERT(!parse("x.ttf 0px", face, px));
		TS_ASSERT(!parse("x.ttf -3", face, px));
		TS_ASSERT(!parse("x.ttf 9999px", face, px));
		TS_ASSERT(!parse("x.ttf 129px", face, px));
		TS_ASSERT(!parse("x.ttf px", face, px));
		TS_ASSERT(!parse("x.ttf 12pt", face, px));
		TS_ASSERT(!parse(" 12px", face, px));           // no face
		TS_ASSERT(!parse("", face, px));
		TS_ASSERT(!parse("   ", face, px));
		TS_ASSERT(!parse("x.ttf 99999999999999999999px", face, px));
		// Nothing is written on failure.
		TS_ASSERT_EQUALS(face, "unchanged");
		TS_ASSERT_EQUALS(px, -99);
	}
};
