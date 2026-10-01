#include <cxxtest/TestSuite.h>

#include "graphics/hires_text/glyph_mirror.h"

/**
 * Mirrored charsets (C27): a glyph drawn flipped, and where it lands.
 *
 * MI1, MI2 and Loom CD carry a charset 3 whose glyphs are the normal ones
 * turned upside down (flipped both ways), and the strings drawn with it are
 * stored reversed; drawn left to right they show the whole line rotated
 * half a turn. A replacement face reproduces that by flipping each glyph
 * inside its own box and keeping the string's order.
 */
class HiResTextGlyphMirrorTestSuite : public CxxTest::TestSuite {
	/// A 4x3 glyph, one distinct value per pixel, in a buffer of pitch 6.
	static void fill(byte *buf) {
		memset(buf, 0xEE, 6 * 3);
		for (int y = 0; y < 3; ++y)
			for (int x = 0; x < 4; ++x)
				buf[y * 6 + x] = (byte)(10 * y + x + 1);
	}

public:
	void test_flip_horizontal_reverses_each_row() {
		byte buf[6 * 3];
		fill(buf);
		Graphics::flipGlyph(buf, 6, 4, 3, Graphics::kHiResMirrorHorizontal);
		for (int y = 0; y < 3; ++y) {
			for (int x = 0; x < 4; ++x)
				TS_ASSERT_EQUALS(buf[y * 6 + x], (byte)(10 * y + (3 - x) + 1));
			// The pitch padding is not the glyph's and is left alone.
			TS_ASSERT_EQUALS(buf[y * 6 + 4], 0xEE);
			TS_ASSERT_EQUALS(buf[y * 6 + 5], 0xEE);
		}
	}

	void test_flip_vertical_reverses_the_rows() {
		byte buf[6 * 3];
		fill(buf);
		Graphics::flipGlyph(buf, 6, 4, 3, Graphics::kHiResMirrorVertical);
		for (int y = 0; y < 3; ++y)
			for (int x = 0; x < 4; ++x)
				TS_ASSERT_EQUALS(buf[y * 6 + x], (byte)(10 * (2 - y) + x + 1));
	}

	void test_flip_both_is_a_half_turn() {
		byte buf[6 * 3];
		fill(buf);
		Graphics::flipGlyph(buf, 6, 4, 3, Graphics::kHiResMirrorBoth);
		for (int y = 0; y < 3; ++y)
			for (int x = 0; x < 4; ++x)
				TS_ASSERT_EQUALS(buf[y * 6 + x], (byte)(10 * (2 - y) + (3 - x) + 1));
	}

	void test_flip_none_changes_nothing() {
		byte buf[6 * 3], ref[6 * 3];
		fill(buf);
		fill(ref);
		Graphics::flipGlyph(buf, 6, 4, 3, Graphics::kHiResMirrorNone);
		TS_ASSERT_EQUALS(memcmp(buf, ref, sizeof(buf)), 0);
	}

	void test_mirrored_left_reflects_about_the_box() {
		// A glyph whose ink starts 1 px after the pen and is 5 px wide, in a
		// 9 px advance: flipped, it ends 1 px before the box's right edge.
		TS_ASSERT_EQUALS(Graphics::mirroredLeft(101, 5, 100, 109), 103);
		// A glyph that fills its box stays where it is.
		TS_ASSERT_EQUALS(Graphics::mirroredLeft(100, 9, 100, 109), 100);
		// A mark reaching left of its base's box lands right of it.
		TS_ASSERT_EQUALS(Graphics::mirroredLeft(98, 3, 100, 109), 108);
		// An empty box (a mark with no base) reflects about the pen.
		TS_ASSERT_EQUALS(Graphics::mirroredLeft(40, 4, 42, 42), 40);
	}

	void test_line_order_is_kept() {
		// Two glyphs drawn in string order, each flipped in its own box:
		// the first stays left of the second, as the game draws its own.
		const int a = Graphics::mirroredLeft(0, 6, 0, 8);
		const int b = Graphics::mirroredLeft(8, 6, 8, 16);
		TS_ASSERT(a < b);
		TS_ASSERT_EQUALS(a, 2);
		TS_ASSERT_EQUALS(b, 10);
	}
};
