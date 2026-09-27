#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/memstream.h"
#include "graphics/hires_text/font_map.h"
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
	static Graphics::HiResTextConfig parse(const char *text) {
		Graphics::HiResTextConfig c;
		Common::Array<Common::String> qualifiers;
		Common::MemoryReadStream stream((const byte *)text, strlen(text));
		TS_ASSERT(Graphics::HiResFontMap::loadFromStream(stream, Common::Path("/tmp/c27", '/'), qualifiers, c));
		return c;
	}

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

	void test_parse_mirror_values() {
		Graphics::HiResMirror m;
		TS_ASSERT(Graphics::parseMirror("true", m));
		TS_ASSERT_EQUALS(m, Graphics::kHiResMirrorGame);
		TS_ASSERT(Graphics::parseMirror("On", m));
		TS_ASSERT_EQUALS(m, Graphics::kHiResMirrorGame);
		TS_ASSERT(Graphics::parseMirror("false", m));
		TS_ASSERT_EQUALS(m, Graphics::kHiResMirrorNone);
		TS_ASSERT(Graphics::parseMirror("off", m));
		TS_ASSERT_EQUALS(m, Graphics::kHiResMirrorNone);
		TS_ASSERT(Graphics::parseMirror("horizontal", m));
		TS_ASSERT_EQUALS(m, Graphics::kHiResMirrorHorizontal);
		TS_ASSERT(Graphics::parseMirror("vertical", m));
		TS_ASSERT_EQUALS(m, Graphics::kHiResMirrorVertical);
		TS_ASSERT(Graphics::parseMirror("both", m));
		TS_ASSERT_EQUALS(m, Graphics::kHiResMirrorBoth);
		TS_ASSERT(Graphics::parseMirror("rotate", m));
		TS_ASSERT_EQUALS(m, Graphics::kHiResMirrorBoth);
		TS_ASSERT(!Graphics::parseMirror("sideways", m));
		TS_ASSERT(!Graphics::parseMirror("", m));
	}

	void test_map_font_section_mirror() {
		const Graphics::HiResTextConfig c = parse(
			"[hires]\nscale=2\n"
			"[font.3]\nmirror=true\n"
			"[font.4]\nmirror=false\n"
			"[font.5]\nmirror=horizontal\n"
			"[font.6]\nmirror=sideways\n"
			"[font.7]\nsize=20\n");
		const Graphics::HiResFontIdSettings *f3 = c.fontIdSettings(3);
		const Graphics::HiResFontIdSettings *f4 = c.fontIdSettings(4);
		const Graphics::HiResFontIdSettings *f5 = c.fontIdSettings(5);
		const Graphics::HiResFontIdSettings *f6 = c.fontIdSettings(6);
		const Graphics::HiResFontIdSettings *f7 = c.fontIdSettings(7);
		TS_ASSERT(f3 && f4 && f5 && f6 && f7);
		if (!f3 || !f4 || !f5 || !f6 || !f7)
			return;
		TS_ASSERT(f3->mirrorSet);
		TS_ASSERT_EQUALS(f3->mirror, Graphics::kHiResMirrorGame);
		TS_ASSERT(f4->mirrorSet);
		TS_ASSERT_EQUALS(f4->mirror, Graphics::kHiResMirrorNone);
		TS_ASSERT(f5->mirrorSet);
		TS_ASSERT_EQUALS(f5->mirror, Graphics::kHiResMirrorHorizontal);
		// A bad value is warned about and leaves the key unset.
		TS_ASSERT(!f6->mirrorSet);
		TS_ASSERT(!f7->mirrorSet);
		// mirror= names no face, so a section holding only it still says
		// "no font named" to the map check.
		TS_ASSERT(!f3->faceSet);
		TS_ASSERT(f3->onlyMirror());
		TS_ASSERT(!f7->onlyMirror());
	}
};
