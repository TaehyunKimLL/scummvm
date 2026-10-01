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

	// FrameKeeper: the manager's switching between the window and a buffer.
	static int s_allocs;
	static bool s_failAlloc;
	static void *countingAlloc(size_t n) {
		if (s_failAlloc)
			return nullptr;
		++s_allocs;
		return malloc(n);
	}

	static DOS::FrameWindow window(Window &win, bool lineRepeat = false, int w = 8) {
		DOS::FrameWindow fw;
		fw.pixels = win.px;
		fw.pitch = Window::Pitch;
		fw.w = w;
		fw.h = Window::H;
		fw.format = xrgb();
		fw.lineRepeat = lineRepeat;
		return fw;
	}

	void test_keeper_takes_the_window_when_it_can() {
		Window win;
		DOS::FrameWindow fw = window(win);
		DOS::FrameKeeper k;
		k.create(8, 5, xrgb(), &fw, false, false);
		TS_ASSERT(k.screen().direct());
		// Rows of another length, line repeat, another format, the loading
		// screen, or dos_frame_buffer: a buffer.
		DOS::FrameWindow wide = window(win, false, Window::W);
		TS_ASSERT(!k.canBeDirect(&wide, xrgb(), 8, 5, false, false));
		DOS::FrameWindow rep = window(win, true);
		TS_ASSERT(!k.canBeDirect(&rep, xrgb(), 8, 5, false, false));
		TS_ASSERT(!k.canBeDirect(&fw, Graphics::PixelFormat::createFormatCLUT8(), 8, 5, false, false));
		TS_ASSERT(!k.canBeDirect(&fw, xrgb(), 8, 5, true, false));
		TS_ASSERT(!k.canBeDirect(nullptr, xrgb(), 8, 5, false, false));
		k.setForceBuffer(true);
		TS_ASSERT(!k.canBeDirect(&fw, xrgb(), 8, 5, false, false));
		k.create(8, 5, xrgb(), &fw, false, false);
		TS_ASSERT(!k.screen().direct());
		TS_ASSERT_EQUALS(k.sync(&fw, false, 0, 0, true, nullptr, nullptr), DOS::FrameKeeper::kSame);
		TS_ASSERT(!k.screen().direct());
	}

	void test_keeper_line_repeat_keeps_a_buffer() {
		Window win;
		DOS::FrameWindow rep = window(win, true);
		DOS::FrameKeeper k;
		k.create(8, 5, xrgb(), &rep, false, false);
		TS_ASSERT(!k.screen().direct());
		for (int i = 0; i < 50; ++i)
			TS_ASSERT_EQUALS(k.sync(&rep, false, 0, 0, true, nullptr, nullptr), DOS::FrameKeeper::kSame);
		TS_ASSERT(!k.screen().direct());
	}

	// A shake takes one buffer for all of it, the returns to 0 within it
	// included, and gives it back once the offset has stayed 0 a while.
	void test_keeper_one_buffer_for_a_whole_shake() {
		Window win;
		DOS::FrameWindow fw = window(win);
		DOS::FrameKeeper k;
		k.screen().setAllocator(&countingAlloc);
		s_allocs = 0;
		s_failAlloc = false;
		k.create(8, 5, xrgb(), &fw, false, false);
		paint(k.screen().surface());
		DOS::SoftCursor c;
		const uint32 img[4] = { 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF };
		c.setImage((const byte *)img, 2, 2, 0, 0, 0, 4);
		c.draw(win.px, Window::Pitch, 4, Window::W, Window::H, 2, 1);
		Common::Rect back;
		// SCUMM's offsets: 0 1 2 1 0 2 3 1, twice, one a frame.
		static const int shake[8] = { 0, 1, 2, 1, 0, 2, 3, 1 };
		bool first = true;
		for (int i = 0; i < 16; ++i) {
			const DOS::FrameKeeper::SyncResult r = k.sync(&fw, false, 0, shake[i % 8], true, &c, &back);
			if (shake[i % 8] && first) {
				TS_ASSERT_EQUALS(r, DOS::FrameKeeper::kMoved);
				first = false;
			} else {
				TS_ASSERT_EQUALS(r, DOS::FrameKeeper::kSame);
			}
		}
		TS_ASSERT_EQUALS(s_allocs, 1);
		TS_ASSERT(!k.screen().direct());
		TS_ASSERT(k.shakeShown());
		TS_ASSERT(painted(k.screen().surface()));	// without the cursor
		TS_ASSERT(!back.isEmpty());
		// Settled: back into the window after kShakeSettleFrames frames at 0.
		int frames = 0;
		while (!k.screen().direct() && frames < 100) {
			k.sync(&fw, false, 0, 0, true, nullptr, nullptr);
			++frames;
		}
		TS_ASSERT_EQUALS(frames, (int)DOS::FrameKeeper::kShakeSettleFrames);
		TS_ASSERT(painted(k.screen().surface()));
		TS_ASSERT_EQUALS(s_allocs, 1);
	}

	// No memory for the shake's buffer: the frame stays in the window and is
	// shown unshaken, without trying again every frame of that shake.
	void test_keeper_no_memory_for_a_shake_draws_it_unshaken() {
		Window win;
		DOS::FrameWindow fw = window(win);
		DOS::FrameKeeper k;
		k.screen().setAllocator(&countingAlloc);
		s_allocs = 0;
		s_failAlloc = true;
		k.create(8, 5, xrgb(), &fw, false, false);
		paint(k.screen().surface());
		for (int i = 0; i < 10; ++i)
			TS_ASSERT_EQUALS(k.sync(&fw, false, 0, 2, true, nullptr, nullptr), DOS::FrameKeeper::kSame);
		TS_ASSERT(k.screen().direct());
		TS_ASSERT(!k.shakeShown());
		TS_ASSERT(painted(k.screen().surface()));
		s_failAlloc = false;
	}

	// The loading screen takes the window: the frame goes to a buffer with
	// its picture, and comes back with it when the game shows.
	void test_keeper_loading_screen_then_the_game() {
		Window win;
		DOS::FrameWindow fw = window(win);
		DOS::FrameKeeper k;
		k.create(8, 5, xrgb(), &fw, true, false);	// made while the text stage gives way
		TS_ASSERT(!k.screen().direct());
		paint(k.screen().surface());
		memset(win.px, 0x33, sizeof(win.px));	// the loading screen
		TS_ASSERT_EQUALS(k.sync(&fw, true, 0, 0, true, nullptr, nullptr), DOS::FrameKeeper::kSame);
		TS_ASSERT(painted(k.screen().surface()));
		TS_ASSERT_EQUALS(k.sync(&fw, false, 0, 0, true, nullptr, nullptr), DOS::FrameKeeper::kMoved);
		TS_ASSERT(k.screen().direct());
		TS_ASSERT(painted(k.screen().surface()));
		TS_ASSERT_EQUALS(win.at(4, 4), 405u);
		// And out again, should the loading screen come back (another mode).
		TS_ASSERT_EQUALS(k.sync(&fw, true, 0, 0, true, nullptr, nullptr), DOS::FrameKeeper::kMoved);
		TS_ASSERT(!k.screen().direct());
		TS_ASSERT(painted(k.screen().surface()));
	}

	// A window surface made again under a direct frame: the frame follows
	// the new pixels, cleared, and says so; a failed mode switch is the same
	// to the frame (the manager frees it, then makes it again cleared).
	void test_keeper_a_window_made_again_loses_the_picture() {
		Window win, other;
		DOS::FrameWindow fw = window(win);
		DOS::FrameKeeper k;
		k.create(8, 5, xrgb(), &fw, false, false);
		paint(k.screen().surface());
		TS_ASSERT(!k.lost(&fw));
		DOS::FrameWindow moved = window(other);
		TS_ASSERT(k.lost(&moved));
		DOS::SoftCursor c;
		TS_ASSERT_EQUALS(k.sync(&moved, false, 0, 0, true, &c, nullptr), DOS::FrameKeeper::kLost);
		TS_ASSERT(k.screen().direct());
		TS_ASSERT_EQUALS(k.screen().surface().getPixels(), (void *)other.px);
		TS_ASSERT_EQUALS(other.at(3, 2), 0u);
		TS_ASSERT_EQUALS(win.at(3, 2), 204u);	// the old pixels are not written
		// No window at all: a buffer, cleared.
		TS_ASSERT_EQUALS(k.sync(nullptr, false, 0, 0, true, &c, nullptr), DOS::FrameKeeper::kLost);
		TS_ASSERT(!k.screen().direct());
		TS_ASSERT(k.screen().exists());
		// The failed switch: freed, then made again for the restored mode.
		k.screen().free();
		k.create(8, 5, xrgb(), &fw, false, false);
		TS_ASSERT(k.screen().direct());
		TS_ASSERT_EQUALS(win.at(3, 2), 0u);
	}
};

int DosGameScreenTestSuite::s_allocs = 0;
bool DosGameScreenTestSuite::s_failAlloc = false;
