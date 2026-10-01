#include <cxxtest/TestSuite.h>
#include "backends/platform/dos/lazy-overlay.h"

// The DOS overlay (640x480 RGB565, 600 KB) exists only while something
// uses it: its size and format are known before, and it reads as cleared.
class DosLazyOverlayTestSuite : public CxxTest::TestSuite {
public:
	void test_size_and_format_without_pixels() {
		DOS::LazyOverlay o(640, 480, DOS::rgb565());
		TS_ASSERT_EQUALS(o.w(), 640);
		TS_ASSERT_EQUALS(o.h(), 480);
		TS_ASSERT(o.format() == DOS::rgb565());
		TS_ASSERT(!o.allocated());
	}

	void test_first_use_allocates_a_cleared_surface() {
		DOS::LazyOverlay o(8, 4, DOS::rgb565());
		Graphics::Surface &s = o.get();
		TS_ASSERT(o.allocated());
		TS_ASSERT_EQUALS(s.w, 8);
		TS_ASSERT_EQUALS(s.h, 4);
		TS_ASSERT(s.format == DOS::rgb565());
		for (int y = 0; y < 4; ++y)
			for (int x = 0; x < 8; ++x)
				TS_ASSERT_EQUALS(*(const uint16 *)s.getBasePtr(x, y), 0);
	}

	void test_get_keeps_the_same_pixels() {
		DOS::LazyOverlay o(8, 4, DOS::rgb565());
		*(uint16 *)o.get().getBasePtr(3, 2) = 0x1234;
		TS_ASSERT_EQUALS(*(const uint16 *)o.get().getBasePtr(3, 2), 0x1234);
	}

	void test_release_frees_and_next_use_is_cleared_again() {
		DOS::LazyOverlay o(8, 4, DOS::rgb565());
		*(uint16 *)o.get().getBasePtr(3, 2) = 0x1234;
		o.release();
		TS_ASSERT(!o.allocated());
		TS_ASSERT_EQUALS(o.w(), 8);
		TS_ASSERT_EQUALS(o.h(), 4);
		TS_ASSERT_EQUALS(*(const uint16 *)o.get().getBasePtr(3, 2), 0);
	}

	void test_grab_without_pixels_gives_a_cleared_copy_and_allocates_nothing() {
		DOS::LazyOverlay o(8, 4, DOS::rgb565());
		Graphics::Surface copy;
		o.grab(copy);
		TS_ASSERT(!o.allocated());
		TS_ASSERT_EQUALS(copy.w, 8);
		TS_ASSERT_EQUALS(copy.h, 4);
		TS_ASSERT(copy.format == DOS::rgb565());
		TS_ASSERT_EQUALS(*(const uint16 *)copy.getBasePtr(7, 3), 0);
		copy.free();
	}

	void test_grab_copies_the_pixels() {
		DOS::LazyOverlay o(8, 4, DOS::rgb565());
		*(uint16 *)o.get().getBasePtr(1, 1) = 0xBEEF;
		Graphics::Surface copy;
		o.grab(copy);
		TS_ASSERT_EQUALS(*(const uint16 *)copy.getBasePtr(1, 1), 0xBEEF);
		copy.free();
	}
};
