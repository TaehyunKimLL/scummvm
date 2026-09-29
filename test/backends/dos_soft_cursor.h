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
		Common::Rect r = c.draw(screen, 4, 1, 4, 4, 1, 1);
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
		Common::Rect r = c.draw(screen, 4, 1, 4, 4, 0, 0);	// top-left at (-1,-1)
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
		c.draw((byte *)screen, 4, 2, 2, 2, 1, 0);
		TS_ASSERT_EQUALS(screen[1], 0xF800);
		c.restore((byte *)screen, 4);
		TS_ASSERT_EQUALS(screen[1], 5);
	}

	void test_restore_skipped_after_bpp_change() {
		byte screen[4 * 4];
		memset(screen, 1, sizeof(screen));
		DOS::SoftCursor c;
		c.setImage(image(), 2, 2, 0, 0, 0, 1);
		c.draw(screen, 4, 1, 4, 4, 1, 1);
		const uint16 img16[1] = { 0xF800 };
		c.setImage((const byte *)img16, 1, 1, 0, 0, 0, 2);
		byte copy[sizeof(screen)];
		memcpy(copy, screen, sizeof(screen));
		TS_ASSERT(c.restore(screen, 4).isEmpty());
		TS_ASSERT_SAME_DATA(screen, copy, sizeof(screen));
		TS_ASSERT(c.restore(screen, 4).isEmpty());	// and it stays dropped
	}

	void test_draw_refuses_another_pixel_size() {
		// A 4-byte image (true-colour cursor) against a CLUT8 screen: the
		// rows would be 4x too long; nothing is written or saved.
		byte screen[4 * 4];
		memset(screen, 1, sizeof(screen));
		const uint32 img32[4] = { 0, 0x00FFFFFF, 0x00FFFFFF, 0x00FFFFFF };
		DOS::SoftCursor c;
		c.setImage((const byte *)img32, 2, 2, 0, 0, 0, 4);
		TS_ASSERT(c.draw(screen, 4, 1, 4, 4, 1, 1).isEmpty());
		for (int i = 0; i < 16; ++i)
			TS_ASSERT_EQUALS(screen[i], 1);
		TS_ASSERT(c.restore(screen, 4).isEmpty());
	}

	void test_cleared_image_draws_nothing() {
		byte screen[4 * 4];
		memset(screen, 1, sizeof(screen));
		DOS::SoftCursor c;
		c.setImage(image(), 2, 2, 0, 0, 0, 1);
		c.clearImage();
		TS_ASSERT(!c.hasImage());
		TS_ASSERT(c.draw(screen, 4, 1, 4, 4, 1, 1).isEmpty());
		for (int i = 0; i < 16; ++i)
			TS_ASSERT_EQUALS(screen[i], 1);
	}

	void test_restore_after_forget_writes_nothing() {
		byte screen[4 * 4];
		memset(screen, 1, sizeof(screen));
		DOS::SoftCursor c;
		c.setImage(image(), 2, 2, 0, 0, 0, 1);
		c.draw(screen, 4, 1, 4, 4, 1, 1);
		byte copy[sizeof(screen)];
		memcpy(copy, screen, sizeof(screen));
		c.forget();
		TS_ASSERT(c.restore(screen, 4).isEmpty());
		TS_ASSERT_SAME_DATA(screen, copy, sizeof(screen));
	}
};
