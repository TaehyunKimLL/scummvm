#include <cxxtest/TestSuite.h>

#include "test/graphics/hires_support.h"
#include "test/system/null_osystem.h"

#include "graphics/hires_text/svfchain.h"
#include "graphics/hires_text/svfface.h"

#include "agi/hirestext.h"

#include "common/ptr.h"

// AgiHiResText: a 16x16 1 bpp stencil per character for AGI's 640x400 display.
class AgiHiResTextTestSuite : public CxxTest::TestSuite {
	static const char *gameMap() {
		return "[map]\nversion=3\n[fonts]\njamo=data:fixture/JAMO.SVF\n[font]\nface=jamo\nmissing=u+25a1\n";
	}

	struct Gray {
		byte px[16 * 16];
	};
	static void plot(void *ctx, int16_t x, int16_t y, uint8_t coverage) {
		Gray *g = (Gray *)ctx;
		if (x >= 0 && y >= 0 && x < 16 && y < 16)
			g->px[y * 16 + x] = MAX<byte>(g->px[y * 16 + x], coverage);
	}

public:
	void setUp() override {
		Common::install_null_g_system();
	}
	void tearDown() override {
		Common::uninstall_null_g_system();
	}

	void test_basicLatinIsAgisOwnFont() {
		HiResTest::MemoryFiles files;
		files.addText("game/ko/HIRESBIT.MAP", gameMap());
		Agi::AgiHiResText t;
		Common::Array<Common::String> notes;
		TS_ASSERT(t.setup(&files, Graphics::HiResOptions(), Common::Path("game"), "ko", notes));
		uint16 rows[16];
		for (uint32 cp = 0; cp < 0x80; cp++)
			TS_ASSERT(!t.glyph(cp, rows));
		TS_ASSERT(t.glyph(0xAC00, rows));
	}

	// koverlay's "missing glyphs": glyph() draws the missing box for a character
	// no face has and still answers true, so the check needs hasGlyph().
	void test_hasGlyphSaysWhatTheFaceHas() {
		HiResTest::MemoryFiles files;
		files.addText("game/ko/HIRESBIT.MAP", gameMap());
		Agi::AgiHiResText t;
		Common::Array<Common::String> notes;
		TS_ASSERT(t.setup(&files, Graphics::HiResOptions(), Common::Path("game"), "ko", notes));
		for (uint32 cp = 0; cp < 0x80; cp++)
			TS_ASSERT(!t.hasGlyph(cp)); // AGI's own font draws those
		for (uint32 cp = 0xAC00; cp <= 0xD7A3; cp++)
			TS_ASSERT(t.hasGlyph(cp));
		TS_ASSERT(!t.hasGlyph(0x4E00)); // a hanja the fixture lacks: drawn as the box
		uint16 rows[16];
		TS_ASSERT(t.glyph(0x4E00, rows)); // and glyph() still answers true for it
	}

	// Every Hangul syllable: the stencil is the face drawn straight through
	// the chain at the cell's pen position and cut at coverage 128 (the glue:
	// centring, baseline, clip, bit order), and has ink.
	void test_everySyllableIsTheFaceInItsCell() {
		HiResTest::MemoryFiles files;
		files.addText("game/ko/HIRESBIT.MAP", gameMap());
		Agi::AgiHiResText t;
		Common::Array<Common::String> notes;
		TS_ASSERT(t.setup(&files, Graphics::HiResOptions(), Common::Path("game"), "ko", notes));
		// the reference chain: the same fixture face, opened on its own
		Graphics::SvfFace face;
		TS_ASSERT(face.loadFromMemory(kSvfFixtureJamo, sizeof(kSvfFixtureJamo), "JAMO.SVF"));
		Graphics::SvfChain chain;
		TS_ASSERT(chain.addFace(&face));
		const int line = chain.getAscent() + chain.getDescent();
		uint bad = 0, empty = 0;
		for (uint32 cp = 0xAC00; cp <= 0xD7A3; cp++) {
			uint16 rows[16];
			if (!t.glyph(cp, rows)) {
				bad++;
				continue;
			}
			char utf8[4];
			utf8[0] = (char)(0xE0 | (cp >> 12));
			utf8[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
			utf8[2] = (char)(0x80 | (cp & 0x3F));
			const int advance = CLIP<int>(chain.getWidth(utf8, 3), 0, 16);
			Gray g;
			memset(g.px, 0, sizeof(g.px));
			chain.drawCoverage(utf8, 3, (16 - advance) / 2, (16 - line) / 2 + chain.getAscent(), 16, 16, plot, &g);
			uint ink = 0;
			for (int y = 0; y < 16; y++) {
				uint16 want = 0;
				for (int x = 0; x < 16; x++)
					if (g.px[y * 16 + x] >= 128)
						want |= (uint16)(0x8000 >> x);
				if (want != rows[y])
					bad++;
				for (int x = 0; x < 16; x++)
					ink += (rows[y] >> x) & 1;
			}
			if (!ink)
				empty++;
		}
		TS_ASSERT_EQUALS(bad, 0u);
		TS_ASSERT_EQUALS(empty, 0u);
	}

	void test_offWithoutAMapWhenHiresTextIsOff() {
		HiResTest::MemoryFiles files;
		Graphics::HiResOptions off;
		off.enabled = false;
		Agi::AgiHiResText t;
		Common::Array<Common::String> notes;
		TS_ASSERT(!t.setup(&files, off, Common::Path("game"), "ko", notes));
		TS_ASSERT(!t.isActive());
		uint16 rows[16];
		TS_ASSERT(!t.glyph(0xAC00, rows));
	}
};
