#include <cxxtest/TestSuite.h>

#include "engines/scumm/hires_palette_users.h"

/**
 * Which strips a palette change reaches when the screen is true colour.
 *
 * With blended hi-res text the engine resolves colours itself, so a palette
 * change has to recomposite every pixel drawn with a changed entry. It used
 * to recomposite the whole screen on every change: a room that cycles a few
 * colours (MI2's room 33 once the camera has scrolled) then redrew 640x400
 * true-colour pixels twice a loop. These check that only the strips holding
 * a changed index, in the picture or in the text layer, come back, and only
 * over the rows that hold one.
 */
class HiResPaletteUsersTestSuite : public CxxTest::TestSuite {
	static const byte kNone = Scumm::kHiResTextTransparent;

	struct Planes {
		// A picture 24 wide (three strips) and 4 high, at 2x for the planes.
		byte src[4 * 24];
		byte text[8 * 48];
		byte under[8 * 48];
		byte underCov[8 * 48];
		bool changed[256];
		int top[3], bottom[3];

		Planes() {
			memset(src, 1, sizeof(src));
			memset(text, kNone, sizeof(text));
			memset(under, 0, sizeof(under));
			memset(underCov, 0, sizeof(underCov));
			memset(changed, 0, sizeof(changed));
		}

		void scan(bool withUnder = false) {
			Scumm::findPaletteUsers(src, 24, text, 48, withUnder ? under : nullptr,
									withUnder ? underCov : nullptr, 48,
									24, 4, 2, changed, top, bottom);
		}
	};

	static bool empty(const Planes &p, int strip) { return p.top[strip] >= p.bottom[strip]; }

public:
	void test_nothing_changed_reaches_nothing() {
		Planes p;
		p.scan();
		for (int s = 0; s < 3; ++s)
			TS_ASSERT(empty(p, s));
	}

	void test_picture_pixel_marks_its_strip_and_rows() {
		Planes p;
		p.changed[9] = true;
		p.src[1 * 24 + 10] = 9;	// strip 1, row 1
		p.src[2 * 24 + 15] = 9;	// strip 1, row 2
		p.scan();
		TS_ASSERT(empty(p, 0));
		TS_ASSERT_EQUALS(p.top[1], 1);
		TS_ASSERT_EQUALS(p.bottom[1], 3);
		TS_ASSERT(empty(p, 2));
	}

	void test_every_pixel_of_a_changed_entry_is_found() {
		Planes p;
		p.changed[1] = true;	// the whole picture
		p.scan();
		for (int s = 0; s < 3; ++s) {
			TS_ASSERT_EQUALS(p.top[s], 0);
			TS_ASSERT_EQUALS(p.bottom[s], 4);
		}
	}

	/// Text is at the planes' scale: output row 5, column 40 is game row 2, strip 2.
	void test_text_pixel_marks_its_strip() {
		Planes p;
		p.changed[7] = true;
		p.text[5 * 48 + 40] = 7;
		p.scan();
		TS_ASSERT(empty(p, 0));
		TS_ASSERT(empty(p, 1));
		TS_ASSERT_EQUALS(p.top[2], 2);
		TS_ASSERT_EQUALS(p.bottom[2], 3);
	}

	/// The transparent key is not text, even if that palette entry changes.
	void test_transparent_text_is_not_a_user() {
		Planes p;
		p.changed[kNone] = true;
		p.scan();
		for (int s = 0; s < 3; ++s)
			TS_ASSERT(empty(p, s));
	}

	/// A decoration counts where it has coverage, and not where it has none.
	void test_decoration_counts_where_covered() {
		Planes p;
		p.changed[0] = true;	// the under plane's cleared value
		p.changed[5] = true;
		p.under[0 * 48 + 3] = 5;
		p.underCov[0 * 48 + 3] = 200;	// strip 0, game row 0
		p.scan(true);
		TS_ASSERT_EQUALS(p.top[0], 0);
		TS_ASSERT_EQUALS(p.bottom[0], 1);
		TS_ASSERT(empty(p, 1));
		TS_ASSERT(empty(p, 2));
	}
};
