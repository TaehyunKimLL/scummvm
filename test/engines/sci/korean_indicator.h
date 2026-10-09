#include <cxxtest/TestSuite.h>

#include "common/fs.h"
#include "common/rect.h"
#include "common/scummsys.h"
#include "common/str.h"
#include "common/stream.h"
#include "../../system/null_osystem.h"

#include "graphics/surface.h"

#include "engines/sci/graphics/koreaninput.h"

/**
 * The Han/Yeong badge: the player can see which input mode they are in.
 *
 * The complaint this closes was that pressing the toggle produced nothing on
 * screen, so a working toggle and a dead key looked the same. The capture
 * harness (harness/s5cap.sh) answers "is it visible"; what a test can add is
 * the part a screenshot cannot pin down - that the two states are genuinely
 * DIFFERENT pixels, in a fixed place, with a body colour the harness can
 * count. Both sides of that agreement are here: the constants below are the
 * same ones harness/s5find.py is pointed at.
 *
 * The placement decision itself was measured, not reasoned - see the class
 * comment in engines/sci/graphics/koreaninput.h - and what keeps it from
 * being quietly undone is the last two tests, which pin the config gate and
 * the fact that nothing draws into the game's own surface.
 */

class SciKoreanIndicatorTestSuite : public CxxTest::TestSuite {

public:
	void setUp() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::install_null_g_system();
#endif
	}

	void tearDown() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::uninstall_null_g_system();
#endif
	}

	// =================================================== the badge, as pixels

	/**
	 * The two states are different images.
	 *
	 * This is the whole point of the card stated as an assertion: an
	 * indicator whose two states are pixel-identical indicates nothing, and
	 * that is exactly the failure being fixed - a toggle with no visible
	 * effect. Compared over every pixel rather than by a summary, because
	 * two images can share a colour histogram and differ.
	 */
	void test_the_two_modes_are_not_the_same_image() {
		Graphics::Surface ko = Sci::KoreanInputIndicator::renderBadge(true);
		Graphics::Surface en = Sci::KoreanInputIndicator::renderBadge(false);

		TS_ASSERT_EQUALS(ko.w, en.w);
		TS_ASSERT_EQUALS(ko.h, en.h);
		TS_ASSERT_EQUALS(ko.format.bytesPerPixel, en.format.bytesPerPixel);

		int differing = 0;
		for (int y = 0; y < ko.h; ++y)
			for (int x = 0; x < ko.w; ++x)
				if (ko.getPixel(x, y) != en.getPixel(x, y))
					differing++;

		TS_ASSERT_LESS_THAN(0, differing);
		ko.free();
		en.free();
	}

	/**
	 * Each state carries its documented body colour, and a lot of it.
	 *
	 * The count is what harness/s5find.py looks for in a capture, so a change
	 * to the badge that leaves this test passing and the harness finding
	 * nothing is not possible: they are asserting about the same pixels.
	 * A lower bound rather than an exact count, because the label is drawn
	 * over the body and the GUI font's width is not this file's business.
	 */
	void test_each_mode_has_its_own_body_colour() {
		checkBody(true, Sci::KoreanInputIndicator::kKoreanR,
				  Sci::KoreanInputIndicator::kKoreanG,
				  Sci::KoreanInputIndicator::kKoreanB);
		checkBody(false, Sci::KoreanInputIndicator::kEnglishR,
				  Sci::KoreanInputIndicator::kEnglishG,
				  Sci::KoreanInputIndicator::kEnglishB);
	}

	/**
	 * The Korean badge is the loud one.
	 *
	 * Not decoration: the mode a player forgets they are in is Korean,
	 * because every key then produces something other than the keycap. The
	 * two bodies also have to be far enough apart to tell apart at a glance,
	 * which a pair of dark greys would not be.
	 */
	void test_korean_is_the_conspicuous_state() {
		const int koSum = Sci::KoreanInputIndicator::kKoreanR +
						  Sci::KoreanInputIndicator::kKoreanG +
						  Sci::KoreanInputIndicator::kKoreanB;
		const int enSum = Sci::KoreanInputIndicator::kEnglishR +
						  Sci::KoreanInputIndicator::kEnglishG +
						  Sci::KoreanInputIndicator::kEnglishB;
		TS_ASSERT_LESS_THAN(enSum, koSum);

		// Red dominates in Korean; English is neutral.
		TS_ASSERT_LESS_THAN(Sci::KoreanInputIndicator::kKoreanG,
							Sci::KoreanInputIndicator::kKoreanR);
		TS_ASSERT_EQUALS(Sci::KoreanInputIndicator::kEnglishR,
						 Sci::KoreanInputIndicator::kEnglishG);
	}

	/**
	 * The badge is opaque where it is drawn.
	 *
	 * It is composited over the game picture, so a body that inherited a
	 * zero alpha would be invisible without changing a single RGB value -
	 * the one way this feature can ship looking exactly like the bug it
	 * fixes.
	 *
	 * Sampled just inside the border rather than at the centre: the label is
	 * drawn centred, so a centre sample reads the opaque white of a letter
	 * and passes on a fully transparent body. Found by injecting exactly
	 * that defect (harness/s5bite.sh, `transparent`) and watching this test
	 * pass.
	 */
	void test_the_badge_is_opaque() {
		Graphics::Surface ko = Sci::KoreanInputIndicator::renderBadge(true);
		byte a, r, g, b;
		ko.format.colorToARGB(ko.getPixel(2, 2), a, r, g, b);
		TS_ASSERT_EQUALS((int)a, 0xFF);
		TS_ASSERT_EQUALS((int)r, (int)Sci::KoreanInputIndicator::kKoreanR);
		ko.free();
	}

	// =============================================== the badge, as a decision

	/**
	 * Nothing is shown unless `sci_hangul_input` asked for it.
	 *
	 * That single config key is what keeps this invisible to every existing
	 * game, and it is checked where the indicator is driven rather than
	 * inside the indicator, so the gate is one place and readable.
	 */
	void test_the_indicator_is_behind_the_config_gate() {
		Common::String ev = readSource("engines/sci/event.cpp");

		const int gate = ev.find("if (_hangulInputAvailable)");
		const int show = ev.find("_hangulIndicator.show(_hangulInputEnabled)");
		TS_ASSERT_LESS_THAN(0, gate);
		TS_ASSERT_LESS_THAN(0, show);
		TS_ASSERT_LESS_THAN(gate, show);
		// Nothing between them: the gate guards this call and only this call.
		TS_ASSERT_LESS_THAN(show - gate, 40);

		// And _hangulInputAvailable is still that one key, read once.
		TS_ASSERT(ev.contains("ConfMan.hasKey(\"sci_hangul_input\")"));

		// show() is the only way the indicator is driven, so there is no
		// second, ungated path.
		TS_ASSERT_EQUALS(count(ev, "_hangulIndicator."), 1);
	}

	/**
	 * The badge never touches the game's own surface.
	 *
	 * This is the property that makes option 3 (an engine-drawn corner
	 * overlay) unnecessary and option 1 (a marker inside the edit control)
	 * unattractive: the backend composites the icon after the game picture,
	 * so no SCI paint path, dirty rectangle or driver-rendered text can be
	 * disturbed by it. Asserted as "the only drawing call is the backend
	 * one", because the alternative is re-measuring S4's erase defect.
	 */
	void test_the_badge_goes_through_the_backend_not_the_engine() {
		Common::String ki = readSource("engines/sci/graphics/koreaninput.cpp");

		TS_ASSERT(ki.contains("g_system->displayActivityIconOnOSD(&badge)"));
		TS_ASSERT(ki.contains("g_system->displayActivityIconOnOSD(nullptr)"));

		// No engine-side painting: these are the calls that would put pixels
		// into the game's buffer or force an update of it.
		TS_ASSERT_EQUALS(count(ki, "_paint16"), 0);
		TS_ASSERT_EQUALS(count(ki, "bitsShow"), 0);
		TS_ASSERT_EQUALS(count(ki, "_screen"), 0);
		TS_ASSERT_EQUALS(count(ki, "putHangulChar"), 0);
	}

	/**
	 * Showing the same state twice does not re-post the icon.
	 *
	 * The caller is the event pump - measured at about 1,550 entries in a
	 * 40-second session - so a show() that rebuilt the surface every time
	 * would convert an indicator into a per-poll texture upload. The guard
	 * is a state compare, and this pins that it compares BOTH the shown flag
	 * and the mode: comparing only the flag would freeze the badge in
	 * whichever state it was first shown in, which reads on screen exactly
	 * like the bug being fixed.
	 */
	void test_show_is_idempotent_per_state() {
		Common::String ki = readSource("engines/sci/graphics/koreaninput.cpp");
		const int fn = ki.find("void KoreanInputIndicator::show(");
		const int end = ki.find("void KoreanInputIndicator::clear(");
		TS_ASSERT_LESS_THAN(0, fn);
		TS_ASSERT_LESS_THAN(fn, end);

		Common::String body(ki.c_str() + fn, end - fn);
		TS_ASSERT(body.contains("if (_shown && _shownEnabled == hangulEnabled)"));
		TS_ASSERT(body.contains("return;"));
		// The early return comes before the surface is built.
		TS_ASSERT_LESS_THAN((int)body.find("return;"),
							(int)body.find("renderBadge("));
	}

	/**
	 * The label is ASCII, and the reason is written down.
	 *
	 * Measured in harness/s5probe3.sh: the OSD renders with the GUI theme
	 * font, and this build has no scalable font, so 한글 posted through the
	 * same path drew two empty boxes while the ASCII beside it rendered. A
	 * future edit that "improves" the label to Hangul would ship a badge
	 * that reads as boxes on exactly the builds this feature targets.
	 */
	void test_the_label_is_ascii() {
		Common::String ki = readSource("engines/sci/graphics/koreaninput.cpp");
		TS_ASSERT(ki.contains("hangulEnabled ? \"KO\" : \"EN\""));
	}

private:
	void checkBody(bool korean, int r, int g, int b) {
		Graphics::Surface s = Sci::KoreanInputIndicator::renderBadge(korean);
		const uint32 want = s.format.ARGBToColor(0xFF, r, g, b);

		int hits = 0;
		for (int y = 0; y < s.h; ++y)
			for (int x = 0; x < s.w; ++x)
				if (s.getPixel(x, y) == want)
					hits++;

		// The body is the badge minus a one-pixel border and the label.
		TS_ASSERT_LESS_THAN(200, hits);
		s.free();
	}

	static int count(const Common::String &hay, const char *needle) {
		const uint n = strlen(needle);
		int c = 0;
		for (uint i = 0; i + n <= hay.size(); ++i)
			if (!strncmp(hay.c_str() + i, needle, n))
				c++;
		return c;
	}

	Common::String readSource(const char *rel) {
		Common::String path = Common::String(SCI_TEST_SRCDIR) + "/" + rel;
		Common::FSNode node(Common::Path(path, '/'));
		Common::SeekableReadStream *in = node.createReadStream();
		if (!in) {
			TS_FAIL(("cannot read " + path).c_str());
			return Common::String();
		}
		uint32 len = (uint32)in->size();
		char *buf = new char[len + 1];
		uint32 got = in->read(buf, len);
		buf[got] = 0;
		Common::String out(buf, got);
		delete[] buf;
		delete in;
		TS_ASSERT_LESS_THAN(0u, out.size());
		return out;
	}
};
