#include <cxxtest/TestSuite.h>

#include "test/graphics/hires_support.h"
#include "test/graphics/svf_fixture_aa.h"
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

	void test_screenFormatFollowsTheTarget() {
		HiResTest::MemoryFiles files;
		files.addText("game/ko/HIRESVF.MAP", gameMap());
		const Graphics::HiResRenderTarget targets[] = { Graphics::kHiResTargetRgb565, Graphics::kHiResTargetRgb555, Graphics::kHiResTargetRgb888 };
		for (uint i = 0; i < ARRAYSIZE(targets); ++i) {
			Sci::SciHiResText t;
			Common::Array<Common::String> notes;
			Graphics::HiResOptions o;
			o.target = targets[i];
			TS_ASSERT(t.setup(&files, o, Common::Path("game"), "ko", allTargets() | (1 << Graphics::kHiResTargetRgb555), notes));
			TS_ASSERT_EQUALS(t.getTarget(), targets[i]);
			TS_ASSERT(t.wantsRgb());
			TS_ASSERT_EQUALS(Graphics::hiResTargetOf(t.getScreenFormat()), targets[i]);
			TS_ASSERT_EQUALS(Graphics::HiResFontMap::targetQualifier(t.getTarget()), Common::String("rgb"));
		}
		Sci::SciHiResText c;
		Common::Array<Common::String> notes;
		TS_ASSERT(c.setup(&files, Graphics::HiResOptions(), Common::Path("game"), "ko", allTargets(), notes));
		TS_ASSERT(c.getScreenFormat().isCLUT8());
	}

	// AA faces for RGB, a 1 bpp face for CLUT8 (DESIGN 5.1: AA is for the RGB targets): [font:rgb] over [font:clut8].
	static const char *pairedMap() {
		return "[map]\nversion=3\n[fonts]\nng=data:aa/NG16P.SVF\nja=data:aa/NG16JA.SVF\nneo=data:fixture/JAMO.SVF\n"
		       "[font]\nface=neo\nsize=16\n"
		       "[font:rgb]\nface=ng,ja,neo\n"
		       "[font:clut8]\nface=neo\n";
	}

	static void addAaFaces(HiResTest::MemoryFiles &files) {
		files.addBytes("data/aa/NG16P.SVF", kSvfAaNG16P, sizeof(kSvfAaNG16P));
		files.addBytes("data/aa/NG16JA.SVF", kSvfAaNG16JA, sizeof(kSvfAaNG16JA));
	}

	// The bpp of the faces of the chain that draws Hangul in the set of font @p id, "2 2 1".
	static Common::String chainBpps(const Graphics::HiResFontSet *set) {
		const Graphics::SvfChain *c = set->getChain(set->route(0xAC00));
		Common::String out;
		for (uint i = 0; i < c->getFaceCount(); ++i)
			out += Common::String::format(i ? " %u" : "%u", c->getFace(i)->getBpp());
		return out;
	}

	// The coverage levels the set draws "가똠" with, as a bit set of 0: 85, 1: 170, 2: 255, 3: anything else.
	static uint levelsOf(const Graphics::HiResFontSet *set) {
		Graphics::TextPlane plane;
		plane.create(64, 24, 1);
		set->drawText("\xea\xb0\x80\xeb\x98\xa0", 6, 2, 16, plane, 1);
		uint bits = 0;
		for (int y = 0; y < 24; ++y)
			for (int x = 0; x < 64; ++x) {
				const byte c = plane.getCoverage(x, y);
				if (c == 85)
					bits |= 1;
				else if (c == 170)
					bits |= 2;
				else if (c == 255)
					bits |= 4;
				else if (c)
					bits |= 8;
			}
		return bits;
	}

	void test_aaForRgbAndOneBppForClut8() {
		HiResTest::MemoryFiles files;
		files.addText("game/ko/HIRESVF.MAP", pairedMap());
		addAaFaces(files);
		const uint32 all = allTargets() | (1 << Graphics::kHiResTargetRgb555);
		// auto: a face has coverage, so RGB (32-bit where offered), and the [font:rgb] chain is the AA one
		{
			Sci::SciHiResText t;
			Common::Array<Common::String> notes;
			TS_ASSERT(t.setup(&files, Graphics::HiResOptions(), Common::Path("game"), "ko", all, notes));
			TS_ASSERT_EQUALS(t.getTarget(), Graphics::kHiResTargetRgb888);
			TS_ASSERT_EQUALS(chainBpps(t.getFontSet(0)), "2 2 1");
			TS_ASSERT_EQUALS(levelsOf(t.getFontSet(0)), 7u);	// 85, 170, 255 and nothing else
		}
		const Graphics::HiResRenderTarget rgb[] = { Graphics::kHiResTargetRgb565, Graphics::kHiResTargetRgb555, Graphics::kHiResTargetRgb888 };
		for (uint i = 0; i < ARRAYSIZE(rgb); ++i) {
			Sci::SciHiResText t;
			Common::Array<Common::String> notes;
			Graphics::HiResOptions o;
			o.target = rgb[i];
			TS_ASSERT(t.setup(&files, o, Common::Path("game"), "ko", all, notes));
			TS_ASSERT_EQUALS(t.getTarget(), rgb[i]);
			TS_ASSERT_EQUALS(chainBpps(t.getFontSet(0)), "2 2 1");
		}
		// CLUT8 asked for: the [font:clut8] chain, the 1 bpp face alone, whole coverage only
		{
			Sci::SciHiResText t;
			Common::Array<Common::String> notes;
			Graphics::HiResOptions o;
			o.target = Graphics::kHiResTargetClut8;
			TS_ASSERT(t.setup(&files, o, Common::Path("game"), "ko", all, notes));
			TS_ASSERT_EQUALS(t.getTarget(), Graphics::kHiResTargetClut8);
			TS_ASSERT(!t.wantsRgb());
			TS_ASSERT_EQUALS(chainBpps(t.getFontSet(0)), "1");
			TS_ASSERT_EQUALS(levelsOf(t.getFontSet(0)), 4u);	// only 255
			TS_ASSERT_EQUALS(t.coverageOnClut8Warnings(), 0u);
		}
	}

	// auto with an AA map follows what the backend offers: 888 where offered, else 565, else 555 (never the 1 bpp CLUT8 here).
	void test_autoPicksByWhatIsOffered() {
		HiResTest::MemoryFiles files;
		files.addText("game/ko/HIRESVF.MAP", pairedMap());
		addAaFaces(files);
		const uint32 clut = 1 << Graphics::kHiResTargetClut8, r565 = 1 << Graphics::kHiResTargetRgb565, r555 = 1 << Graphics::kHiResTargetRgb555,
		             r888 = 1 << Graphics::kHiResTargetRgb888;
		const struct { uint32 offered; Graphics::HiResRenderTarget want; } c[] = {
			{ clut | r565 | r555 | r888, Graphics::kHiResTargetRgb888 }, { clut | r565 | r555, Graphics::kHiResTargetRgb565 },
			{ clut | r555, Graphics::kHiResTargetRgb555 }, { clut, Graphics::kHiResTargetClut8 },
		};
		for (uint i = 0; i < ARRAYSIZE(c); ++i) {
			Sci::SciHiResText t;
			Common::Array<Common::String> notes;
			TS_ASSERT(t.setup(&files, Graphics::HiResOptions(), Common::Path("game"), "ko", c[i].offered, notes));
			TS_ASSERT_EQUALS(t.getTarget(), c[i].want);
			TS_ASSERT_EQUALS(Graphics::hiResTargetOf(t.getScreenFormat()), c[i].want);
			TS_ASSERT_EQUALS(chainBpps(t.getFontSet(0)), c[i].want == Graphics::kHiResTargetClut8 ? "1" : "2 2 1");
		}
	}

	// A font built over a set (the SCI font cache keeps them) outlives a screen switch: the old sets are kept, so the font stays
	// usable (an ASan build reports the use after free of a deleted set), and the generation says the fonts are stale.
	void test_fontsOutliveTheScreenSwitch() {
		HiResTest::MemoryFiles files;
		files.addText("game/ko/HIRESVF.MAP", pairedMap());
		addAaFaces(files);
		const uint32 all = allTargets() | (1 << Graphics::kHiResTargetRgb555);
		Sci::SciHiResText t;
		Common::Array<Common::String> notes;
		TS_ASSERT(t.setup(&files, Graphics::HiResOptions(), Common::Path("game"), "ko", all, notes));
		TS_ASSERT(t.createPlane(320, 200));
		Common::String drawn;
		Sci::GfxFontHiRes font(new FakeFont(&drawn), *t.getFontSet(0), t.getPlane());
		const Graphics::HiResFontSet *before = t.getFontSet(0);
		const uint generation = t.getGeneration();
		Common::Array<Common::String> n;
		t.setScreenFormat(Graphics::PixelFormat::createFormatCLUT8(), n);
		TS_ASSERT(t.getGeneration() != generation);
		TS_ASSERT(t.getFontSet(0) != before);	// a new set for the new target
		// the old font draws through its old set
		TS_ASSERT(font.getCharWidth(0xAC00) > 0);
		font.draw(0xAC00, 10, 20, 15, false);
		TS_ASSERT(!t.getPlane().isEmpty());
		// the same text on the same screen again changes nothing
		const uint g2 = t.getGeneration();
		t.setScreenFormat(Graphics::PixelFormat::createFormatCLUT8(), n);
		TS_ASSERT_EQUALS(t.getGeneration(), g2);
	}

	// The backend gave another screen than the text resolved: the text follows it (map sections, font sets, the warning).
	void test_textFollowsTheScreenItGot() {
		HiResTest::MemoryFiles files;
		files.addText("game/ko/HIRESVF.MAP", pairedMap());
		addAaFaces(files);
		const uint32 all = allTargets() | (1 << Graphics::kHiResTargetRgb555);
		Common::Array<Common::String> notes;
		// 888 resolved, 565 given: still an RGB chain, a note
		{
			Sci::SciHiResText t;
			TS_ASSERT(t.setup(&files, Graphics::HiResOptions(), Common::Path("game"), "ko", all, notes));
			TS_ASSERT_EQUALS(t.getTarget(), Graphics::kHiResTargetRgb888);
			TS_ASSERT_EQUALS(chainBpps(t.getFontSet(0)), "2 2 1");
			Common::Array<Common::String> n;
			t.setScreenFormat(Graphics::hiResTargetFormat(Graphics::kHiResTargetRgb565), n);
			TS_ASSERT_EQUALS(t.getTarget(), Graphics::kHiResTargetRgb565);
			TS_ASSERT_EQUALS(n.size(), 1u);
			TS_ASSERT_EQUALS(chainBpps(t.getFontSet(0)), "2 2 1");
			n.clear();
			t.setScreenFormat(Graphics::hiResTargetFormat(Graphics::kHiResTargetRgb565), n);	// the same: nothing
			TS_ASSERT(n.empty());
		}
		// an RGB request that ended as a CLUT8 screen: the [font:clut8] chain, the 1 bpp face alone
		{
			Sci::SciHiResText t;
			TS_ASSERT(t.setup(&files, Graphics::HiResOptions(), Common::Path("game"), "ko", all, notes));
			TS_ASSERT_EQUALS(chainBpps(t.getFontSet(0)), "2 2 1");
			Common::Array<Common::String> n;
			t.setScreenFormat(Graphics::PixelFormat::createFormatCLUT8(), n);
			TS_ASSERT_EQUALS(t.getTarget(), Graphics::kHiResTargetClut8);
			TS_ASSERT(!t.wantsRgb());
			TS_ASSERT_EQUALS(chainBpps(t.getFontSet(0)), "1");
			TS_ASSERT_EQUALS(t.coverageOnClut8Warnings(), 0u);
		}
		// a map without [font:clut8]: the AA chain stays, and now the warning is given (once)
		{
			HiResTest::MemoryFiles f2;
			f2.addText("game/ko/HIRESVF.MAP", "[map]\nversion=3\n[fonts]\nng=data:aa/NG16P.SVF\nja=data:aa/NG16JA.SVF\n[font]\nface=ng,ja\nsize=16\n");
			addAaFaces(f2);
			Sci::SciHiResText t;
			TS_ASSERT(t.setup(&f2, Graphics::HiResOptions(), Common::Path("game"), "ko", all, notes));
			Common::Array<Common::String> n;
			t.setScreenFormat(Graphics::PixelFormat::createFormatCLUT8(), n);
			TS_ASSERT_EQUALS(chainBpps(t.getFontSet(0)), "2 2");
			TS_ASSERT_EQUALS(t.coverageOnClut8Warnings(), 1u);
		}
		// a CLUT8 text on an RGB screen (render_target=clut8 with the user's rgb_rendering) follows the screen too: the [font:rgb] chain
		// is blended instead of an AA face being thresholded, and no "CLUT8 screen" warning is given
		{
			Sci::SciHiResText t;
			Graphics::HiResOptions o;
			o.target = Graphics::kHiResTargetClut8;
			TS_ASSERT(t.setup(&files, o, Common::Path("game"), "ko", all, notes));
			TS_ASSERT_EQUALS(chainBpps(t.getFontSet(0)), "1");
			Common::Array<Common::String> n;
			t.setScreenFormat(Graphics::hiResTargetFormat(Graphics::kHiResTargetRgb565), n);
			TS_ASSERT_EQUALS(t.getTarget(), Graphics::kHiResTargetRgb565);
			TS_ASSERT_EQUALS(chainBpps(t.getFontSet(0)), "2 2 1");
			TS_ASSERT_EQUALS(t.coverageOnClut8Warnings(), 0u);
		}
		// an AA-only map, clut8 asked, an RGB screen got: the face is blended, nothing is said about CLUT8
		{
			HiResTest::MemoryFiles f3;
			f3.addText("game/ko/HIRESVF.MAP", "[map]\nversion=3\n[fonts]\nng=data:aa/NG16P.SVF\nja=data:aa/NG16JA.SVF\n[font]\nface=ng,ja\nsize=16\n");
			addAaFaces(f3);
			Sci::SciHiResText t;
			Graphics::HiResOptions o;
			o.target = Graphics::kHiResTargetClut8;
			TS_ASSERT(t.setup(&f3, o, Common::Path("game"), "ko", all, notes));
			Common::Array<Common::String> n;
			t.setScreenFormat(Graphics::hiResTargetFormat(Graphics::kHiResTargetRgb565), n);
			TS_ASSERT(t.wantsRgb());
			TS_ASSERT_EQUALS(t.getComposeMode(), Graphics::TextCompose::kBlend);
			TS_ASSERT_EQUALS(chainBpps(t.getFontSet(0)), "2 2");
			TS_ASSERT_EQUALS(t.coverageOnClut8Warnings(), 0u);
		}
		// an unknown screen format (RGBA4444) changes nothing
		{
			Sci::SciHiResText t;
			TS_ASSERT(t.setup(&files, Graphics::HiResOptions(), Common::Path("game"), "ko", all, notes));
			Common::Array<Common::String> n;
			t.setScreenFormat(Graphics::PixelFormat(2, 4, 4, 4, 4, 12, 8, 4, 0), n);
			TS_ASSERT(n.empty());
			TS_ASSERT_EQUALS(t.getGeneration(), 0u);
		}
	}

	// A map that names an AA face for CLUT8 anyway (no [font:clut8]): the plane keeps the coverage and the sink stamps it at
	// 128 (the defined fallback); the engine says so once.
	void test_aaFaceOnClut8IsStampedAndWarnedOnce() {
		HiResTest::MemoryFiles files;
		files.addText("game/ko/HIRESVF.MAP", "[map]\nversion=3\n[fonts]\nng=data:aa/NG16P.SVF\nja=data:aa/NG16JA.SVF\n[font]\nface=ng,ja\nsize=16\n");
		addAaFaces(files);
		Sci::SciHiResText t;
		Common::Array<Common::String> notes;
		Graphics::HiResOptions o;
		o.target = Graphics::kHiResTargetClut8;
		TS_ASSERT(t.setup(&files, o, Common::Path("game"), "ko", allTargets(), notes));
		TS_ASSERT_EQUALS(t.getTarget(), Graphics::kHiResTargetClut8);
		TS_ASSERT_EQUALS(t.coverageOnClut8Warnings(), 0u);
		TS_ASSERT_EQUALS(chainBpps(t.getFontSet(0)), "2 2");
		TS_ASSERT_EQUALS(t.coverageOnClut8Warnings(), 1u);
		// once: the same id again, other font ids and a second call give no second warning
		TS_ASSERT(t.getFontSet(0));
		TS_ASSERT(t.getFontSet(300));
		TS_ASSERT(t.getFontSet(4));
		TS_ASSERT_EQUALS(t.coverageOnClut8Warnings(), 1u);
		TS_ASSERT_EQUALS(levelsOf(t.getFontSet(0)), 7u);	// the plane has the levels; TextCompose::kStamp cuts them at 128
		TS_ASSERT_EQUALS(t.getComposeMode(), Graphics::TextCompose::kStamp);
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
