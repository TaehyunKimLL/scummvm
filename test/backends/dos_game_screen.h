#include <cxxtest/TestSuite.h>
#include "backends/platform/dos/game-screen.h"
#include "backends/platform/dos/soft-cursor.h"

// The DOS game frame: the window surface itself, or a buffer of its own
// while the window cannot hold it; moving between the two keeps the picture.
class DosGameScreenTestSuite : public CxxTest::TestSuite {
	static Graphics::PixelFormat xrgb() { return Graphics::PixelFormat(4, 8, 8, 8, 0, 16, 8, 0, 0); }

	struct Window {
		// A window wider and taller than the frame, with a pitch of its own.
		enum { W = 12, H = 7, Pitch = 13 * 4 };
		byte px[Pitch * H];
		Window() { memset(px, 0xAA, sizeof(px)); }
		uint32 at(int x, int y) const { return READ_UINT32(px + y * Pitch + x * 4); }
	};

	static void paint(Graphics::Surface &s) {
		for (int y = 0; y < s.h; ++y)
			for (int x = 0; x < s.w; ++x)
				*(uint32 *)s.getBasePtr(x, y) = (uint32)(y * 100 + x + 1);
	}

	static bool painted(const Graphics::Surface &s) {
		for (int y = 0; y < s.h; ++y)
			for (int x = 0; x < s.w; ++x)
				if (*(const uint32 *)s.getBasePtr(x, y) != (uint32)(y * 100 + x + 1))
					return false;
		return true;
	}

public:
	void test_a_direct_frame_is_the_window_cleared_and_owns_nothing() {
		Window win;
		DOS::GameScreen f;
		f.createDirect(8, 5, xrgb(), win.px, Window::Pitch);
		TS_ASSERT(f.direct());
		TS_ASSERT_EQUALS(f.ownBytes(), 0u);
		TS_ASSERT_EQUALS(f.surface().getPixels(), (void *)win.px);
		TS_ASSERT_EQUALS(f.surface().pitch, (int)Window::Pitch);
		TS_ASSERT_EQUALS(win.at(7, 4), 0u);
		TS_ASSERT_EQUALS(win.at(8, 4), 0xAAAAAAAAu);	// outside the frame: left alone
		paint(f.surface());
		TS_ASSERT_EQUALS(win.at(3, 2), 204u);	// written where the window is
		f.free();
		TS_ASSERT(!f.exists());
		TS_ASSERT_EQUALS(win.at(3, 2), 204u);	// not the frame's to free
	}

	void test_attach_moves_the_picture_into_the_window_and_frees_the_buffer() {
		Window win;
		DOS::GameScreen f;
		f.createBuffer(8, 5, xrgb());
		TS_ASSERT(!f.direct());
		TS_ASSERT_EQUALS(f.ownBytes(), 8u * 5 * 4);
		paint(f.surface());
		f.attach(win.px, Window::Pitch);
		TS_ASSERT(f.direct());
		TS_ASSERT_EQUALS(f.ownBytes(), 0u);
		TS_ASSERT(painted(f.surface()));
		TS_ASSERT_EQUALS(win.at(7, 4), 408u);
		TS_ASSERT_EQUALS(f.surface().w, 8);
		TS_ASSERT_EQUALS(f.surface().h, 5);
		TS_ASSERT(f.surface().format == xrgb());
	}

	void test_detach_keeps_the_picture_in_a_buffer_of_its_own() {
		Window win;
		DOS::GameScreen f;
		f.createDirect(8, 5, xrgb(), win.px, Window::Pitch);
		paint(f.surface());
		f.detach();
		TS_ASSERT(!f.direct());
		TS_ASSERT(f.surface().getPixels() != (void *)win.px);
		TS_ASSERT(painted(f.surface()));
		memset(win.px, 0, sizeof(win.px));	// the window shows something else
		TS_ASSERT(painted(f.surface()));
		f.attach(win.px, Window::Pitch);	// and back
		TS_ASSERT(painted(f.surface()));
	}

	void test_clear_outside_leaves_the_frame() {
		Window win;
		DOS::GameScreen f;
		f.createDirect(8, 5, xrgb(), win.px, Window::Pitch);
		paint(f.surface());
		DOS::GameScreen::clearOutside(win.px, Window::Pitch, Window::W, Window::H, 8, 5, 4);
		TS_ASSERT(painted(f.surface()));
		TS_ASSERT_EQUALS(win.at(8, 0), 0u);
		TS_ASSERT_EQUALS(win.at(11, 4), 0u);
		TS_ASSERT_EQUALS(win.at(0, 5), 0u);
		TS_ASSERT_EQUALS(win.at(11, 6), 0u);
	}

	void test_only_a_plain_mode_takes_the_frame() {
		TS_ASSERT(DOS::screenCanBeDirect(true, false, true, true, false, false));
		TS_ASSERT(!DOS::screenCanBeDirect(false, false, true, true, false, false));	// no mode
		TS_ASSERT(!DOS::screenCanBeDirect(true, true, true, true, false, false));	// line repeat
		TS_ASSERT(!DOS::screenCanBeDirect(true, false, false, true, false, false));	// another format
		TS_ASSERT(!DOS::screenCanBeDirect(true, false, true, false, false, false));	// too small
		TS_ASSERT(!DOS::screenCanBeDirect(true, false, true, true, true, false));	// shaking
		TS_ASSERT(!DOS::screenCanBeDirect(true, false, true, true, false, true));	// loading screen
	}

	// The backend's order on a frame in the window: the cursor's pixels go
	// before the game writes (prepareWrite()), and it is drawn again after.
	void test_the_cursor_never_ends_up_in_the_frame() {
		Window win;
		DOS::GameScreen f;
		f.createDirect(8, 5, xrgb(), win.px, Window::Pitch);
		paint(f.surface());
		DOS::SoftCursor c;
		const uint32 img[4] = { 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF };
		c.setImage((const byte *)img, 2, 2, 0, 0, 0, 4);
		c.draw(win.px, Window::Pitch, 4, Window::W, Window::H, 2, 1);
		TS_ASSERT_EQUALS(win.at(2, 1), 0xFFFFFFu);
		c.restore(win.px, Window::Pitch);	// prepareWrite()
		TS_ASSERT(painted(f.surface()));
		*(uint32 *)f.surface().getBasePtr(2, 1) = 7;	// the game writes under it
		c.draw(win.px, Window::Pitch, 4, Window::W, Window::H, 2, 1);	// updateScreen()
		c.restore(win.px, Window::Pitch);
		TS_ASSERT_EQUALS(win.at(2, 1), 7u);
	}
};
