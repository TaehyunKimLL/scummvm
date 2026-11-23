#include <cxxtest/TestSuite.h>

#include "test/graphics/hires_support.h"
#include "test/system/null_osystem.h"

#include "sci/graphics/hirestext.h"

class SciHiResTextTestSuite : public CxxTest::TestSuite {
	// The game's own font: 4 px wide, 8 high; records what it draws.
	class FakeFont : public Sci::GfxFont {
	public:
		Common::String *drawn;
		explicit FakeFont(Common::String *d) : drawn(d) {}
		byte getHeight() override { return 8; }
		byte getCharWidth(uint32 chr) override { return 4; }
		void draw(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput) override {
			*drawn += Common::String::format("%c@%d,%d ", (char)chr, left, top);
		}
	};

	static const char *gameMap() {
		return "[map]\nversion=3\n[fonts]\njamo=data:fixture/JAMO.SVF\n"
		       "[font]\nface=jamo\nrange.basic-latin=original\n"
		       "[font.300:clut8]\nshift=2\n";
	}

	static uint32 allTargets() {
		return (1 << Graphics::kHiResTargetClut8) | (1 << Graphics::kHiResTargetRgb565) | (1 << Graphics::kHiResTargetRgb888);
	}

public:
	void setUp() override {
		Common::install_null_g_system();
	}
	void tearDown() override {
		Common::uninstall_null_g_system();
	}

	void test_picksTheGameMap() {
		HiResTest::MemoryFiles files;
		files.addText("game/ko/HIRESVF.MAP", gameMap());
		Sci::SciHiResText t;
		Common::Array<Common::String> notes;
		TS_ASSERT(t.setup(&files, Graphics::HiResOptions(), Common::Path("game"), "ko", allTargets(), notes));
		TS_ASSERT(t.isActive());
		TS_ASSERT_EQUALS(t.getPick().path.toString('/'), "game/ko/HIRESVF.MAP");
		// 1 bpp face: CLUT8 (the stamp) even where RGB is offered.
		TS_ASSERT_EQUALS(t.getTarget(), Graphics::kHiResTargetClut8);
		TS_ASSERT(!t.wantsRgb());
		TS_ASSERT(t.getFontSet(0));
		TS_ASSERT_EQUALS(t.getFontSet(300)->getShift(), 2); // the :clut8 section was merged
	}

	void test_fallsBackToTheEngineMap() {
		HiResTest::MemoryFiles files;
		files.addText("data/hires_text/maps/korean-default.map",
		              "[map]\nversion=3\n[fonts]\njamo=data:fixture/JAMO.SVF\n[font]\nface=jamo\n");
		Sci::SciHiResText t;
		Common::Array<Common::String> notes;
		TS_ASSERT(t.setup(&files, Graphics::HiResOptions(), Common::Path("game"), "ko", allTargets(), notes));
		TS_ASSERT_EQUALS(notes.back(), Common::String("using ") + Sci::SciHiResText::kDefaultMap);
	}

	void test_offWhenTheKeySaysSo() {
		HiResTest::MemoryFiles files;
		files.addText("game/ko/HIRESVF.MAP", gameMap());
		Graphics::HiResOptions o;
		o.enabled = false;
		Sci::SciHiResText t;
		Common::Array<Common::String> notes;
		TS_ASSERT(!t.setup(&files, o, Common::Path("game"), "ko", allTargets(), notes));
		TS_ASSERT(!t.getFontSet(0));
	}

	void test_fontDrawsHangulIntoThePlane() {
		HiResTest::MemoryFiles files;
		files.addText("game/ko/HIRESVF.MAP", gameMap());
		Sci::SciHiResText t;
		Common::Array<Common::String> notes;
		TS_ASSERT(t.setup(&files, Graphics::HiResOptions(), Common::Path("game"), "ko", allTargets(), notes));
		TS_ASSERT(t.createPlane(320, 200));
		Common::String drawn;
		Sci::GfxFontHiRes font(new FakeFont(&drawn), *t.getFontSet(0), t.getPlane());
		TS_ASSERT(font.getHeight() >= 8);
		TS_ASSERT_EQUALS(font.getCharWidth('A'), 4);          // the game's font
		const byte w = font.getCharWidth(0xAC00);              // 가: the face, low-res
		TS_ASSERT(w >= 4 && w <= 16);
		font.draw('A', 10, 20, 15, false);
		TS_ASSERT_EQUALS(drawn, "A@20,10 ");
		font.draw(0x18, 10, 30, 15, false);                    // a SCI symbol below U+0020: the game's
		TS_ASSERT_EQUALS(drawn, "A@20,10 \x18@30,10 ");
		TS_ASSERT(t.getPlane().isEmpty());
		font.draw(0xAC00, 10, 20, 15, false);
		TS_ASSERT(!t.getPlane().isEmpty());
		// Ink only inside the character's hi-res box, in colour 15.
		int ink = 0;
		for (int y = 0; y < t.getPlane().getHeight(); ++y)
			for (int x = 0; x < t.getPlane().getWidth(); ++x)
				if (t.getPlane().getCoverage(x, y)) {
					++ink;
					TS_ASSERT(x >= 40 && x < 40 + 2 * w && y >= 20 && y < 20 + 2 * font.getHeight());
					TS_ASSERT_EQUALS(t.getPlane().getIndex(x, y), 15);
				}
		TS_ASSERT(ink > 10);
		// A low-res fill over the character clears it (what GfxScreen does).
		t.getPlane().clearLowresRect(Common::Rect(20, 10, 20 + w, 10 + font.getHeight()));
		TS_ASSERT(t.getPlane().isEmpty());
	}

	// agy 5: shift= is applied once (it was added to the baseline and again inside drawChar()).
	void test_shiftMovesTheGlyphOnce() {
		HiResTest::MemoryFiles files;
		files.addText("game/ko/HIRESVF.MAP", gameMap());
		Sci::SciHiResText t;
		Common::Array<Common::String> notes;
		TS_ASSERT(t.setup(&files, Graphics::HiResOptions(), Common::Path("game"), "ko", allTargets(), notes));
		TS_ASSERT(t.createPlane(320, 200));
		Common::String drawn;
		int topRow[2] = { -1, -1 };
		const int fonts[2] = { 0, 300 }; // shift 0 and shift 2
		for (int f = 0; f < 2; ++f) {
			t.getPlane().clearLowresRect(Common::Rect(0, 0, 320, 200));
			Sci::GfxFontHiRes font(new FakeFont(&drawn), *t.getFontSet(fonts[f]), t.getPlane());
			font.draw(0xAC00, 10, 20, 15, false);
			for (int y = 0; y < t.getPlane().getHeight() && topRow[f] < 0; ++y)
				for (int x = 0; x < t.getPlane().getWidth(); ++x)
					if (t.getPlane().getCoverage(x, y)) {
						topRow[f] = y;
						break;
					}
		}
		TS_ASSERT(topRow[0] >= 0 && topRow[1] >= 0);
		TS_ASSERT_EQUALS(topRow[1] - topRow[0], 2);
	}
};
