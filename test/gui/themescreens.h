#include <cxxtest/TestSuite.h>

#include "gui/theme-screens.h"

// The theme's two overlay-sized screens. With gui_release_buffers they
// exist only while a dialog is shown; without it they always do, as
// before. Nothing may draw or grab into them while they are not there.
class ThemeScreensTestSuite : public CxxTest::TestSuite {
	static Graphics::PixelFormat fmt() { return Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0); }

public:
	void test_without_release_screens_always_exist() {
		Graphics::ManagedSurface screen, back;
		GUI::ThemeScreens s(screen, back);
		s.reset(64, 48, fmt(), false, false);	// hidden, no release
		TS_ASSERT(s.ready());
		TS_ASSERT_EQUALS(screen.w, 64);
		TS_ASSERT_EQUALS(back.h, 48);
		s.hidden(false);
		TS_ASSERT(s.ready());
	}

	void test_reset_while_hidden_only_sizes_them() {
		// A screen change while no dialog is shown (refresh() from
		// checkScreenChange() of a cached dialog being opened again):
		// nothing is made, and nothing may be drawn.
		Graphics::ManagedSurface screen, back;
		GUI::ThemeScreens s(screen, back);
		s.reset(64, 48, fmt(), false, true);
		TS_ASSERT(!s.ready());
		TS_ASSERT(s.usable());
		TS_ASSERT_EQUALS(s.width(), 64);
		TS_ASSERT_EQUALS(s.height(), 48);
		TS_ASSERT(screen.getPixels() == nullptr);
		TS_ASSERT(back.getPixels() == nullptr);
	}

	void test_shown_makes_them_at_the_last_size() {
		Graphics::ManagedSurface screen, back;
		GUI::ThemeScreens s(screen, back);
		s.reset(64, 48, fmt(), false, true);
		TS_ASSERT(s.ensure());
		TS_ASSERT(s.ready());
		TS_ASSERT_EQUALS(screen.w, 64);
		TS_ASSERT_EQUALS(screen.h, 48);
		TS_ASSERT(back.format == fmt());
	}

	void test_reset_while_shown_makes_them() {
		Graphics::ManagedSurface screen, back;
		GUI::ThemeScreens s(screen, back);
		s.reset(64, 48, fmt(), true, true);
		TS_ASSERT(s.ready());
	}

	void test_hidden_frees_them_and_they_come_back() {
		Graphics::ManagedSurface screen, back;
		GUI::ThemeScreens s(screen, back);
		s.reset(64, 48, fmt(), true, true);
		s.hidden(true);
		TS_ASSERT(!s.ready());
		TS_ASSERT(screen.getPixels() == nullptr);
		// A screen change while hidden moves the size; the next show uses it.
		s.reset(80, 60, fmt(), false, true);
		TS_ASSERT(!s.ready());
		TS_ASSERT(s.ensure());
		TS_ASSERT_EQUALS(back.w, 80);
		TS_ASSERT_EQUALS(back.h, 60);
		// and they are a proper allocation again, not a borrowed one
		*(uint16 *)back.getBasePtr(79, 59) = 0x1234;
		TS_ASSERT_EQUALS(*(const uint16 *)back.getBasePtr(79, 59), 0x1234);
	}

	void test_an_empty_overlay_is_not_usable() {
		Graphics::ManagedSurface screen, back;
		GUI::ThemeScreens s(screen, back);
		s.reset(0, 0, fmt(), false, true);
		TS_ASSERT(!s.usable());
		TS_ASSERT(!s.ensure());
	}
};
