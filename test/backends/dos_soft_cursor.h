#include <cxxtest/TestSuite.h>
#include "backends/platform/dos/soft-cursor.h"

class DosSoftCursorTestSuite : public CxxTest::TestSuite {
	// 2x2 cursor: key colour 0 at top-left, ink 7 elsewhere.
	static const byte *image() { static const byte img[4] = { 0, 7, 7, 7 }; return img; }

public:
	void test_draw_skips_key_colour_and_restore_puts_back() {
		byte screen[4 * 4];
		memset(screen, 1, sizeof(screen));
		DOS::SoftCursor c;
		c.setImage(image(), 2, 2, 0, 0, 0, 1);
		Common::Rect r = c.draw(screen, 4, 4, 4, 1, 1);
		TS_ASSERT_EQUALS(r, Common::Rect(1, 1, 3, 3));
		TS_ASSERT_EQUALS(screen[1 * 4 + 1], 1);	// key colour: untouched
		TS_ASSERT_EQUALS(screen[1 * 4 + 2], 7);
		TS_ASSERT_EQUALS(screen[2 * 4 + 1], 7);
		Common::Rect back = c.restore(screen, 4);
		TS_ASSERT_EQUALS(back, r);
		for (int i = 0; i < 16; ++i)
			TS_ASSERT_EQUALS(screen[i], 1);
	}

	void test_hotspot_and_clipping() {
		byte screen[4 * 4];
		memset(screen, 1, sizeof(screen));
		DOS::SoftCursor c;
		c.setImage(image(), 2, 2, 1, 1, 0, 1);
		Common::Rect r = c.draw(screen, 4, 4, 4, 0, 0);	// top-left at (-1,-1)
		TS_ASSERT_EQUALS(r, Common::Rect(0, 0, 1, 1));
		TS_ASSERT_EQUALS(screen[0], 7);
		c.restore(screen, 4);
		TS_ASSERT_EQUALS(screen[0], 1);
	}

	void test_restore_without_draw_is_empty() {
		byte screen[16];
		DOS::SoftCursor c;
		TS_ASSERT(c.restore(screen, 4).isEmpty());
	}

	void test_16bpp_key() {
		uint16 screen[2 * 2] = { 5, 5, 5, 5 };
		const uint16 img[1] = { 0xF800 };
		DOS::SoftCursor c;
		c.setImage((const byte *)img, 1, 1, 0, 0, 0x001F, 2);
		c.draw((byte *)screen, 4, 2, 2, 1, 0);
		TS_ASSERT_EQUALS(screen[1], 0xF800);
		c.restore((byte *)screen, 4);
		TS_ASSERT_EQUALS(screen[1], 5);
	}
};
