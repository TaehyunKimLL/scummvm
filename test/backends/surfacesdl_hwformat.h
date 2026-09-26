#include <cxxtest/TestSuite.h>

#include "backends/graphics/surfacesdl/surfacesdl-hwformat.h"

/**
 * Tests for the SurfaceSDL hardware-screen format policy: when the renderer
 * path uses a 32-bit screen, and which formats getSupportedFormats() lists.
 *
 * The expected lists are spelled out in full so that any reordering - which
 * changes the format an engine negotiates - fails here.
 */
class SurfaceSdlHwFormatTestSuite : public CxxTest::TestSuite {
private:
	typedef Common::List<Graphics::PixelFormat> FormatList;

	static Graphics::PixelFormat rgb565() { return Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0); }
	static Graphics::PixelFormat xrgb1555() { return Graphics::PixelFormat(2, 5, 5, 5, 1, 10, 5, 0, 15); }
	static Graphics::PixelFormat rgb555() { return Graphics::PixelFormat(2, 5, 5, 5, 0, 10, 5, 0, 0); }
	static Graphics::PixelFormat rgba4444() { return Graphics::PixelFormat(2, 4, 4, 4, 4, 12, 8, 4, 0); }
	static Graphics::PixelFormat argb4444() { return Graphics::PixelFormat(2, 4, 4, 4, 4, 8, 4, 0, 12); }
	static Graphics::PixelFormat bgr565() { return Graphics::PixelFormat(2, 5, 6, 5, 0, 0, 5, 11, 0); }
	static Graphics::PixelFormat xbgr1555() { return Graphics::PixelFormat(2, 5, 5, 5, 1, 0, 5, 10, 15); }
	static Graphics::PixelFormat bgr555() { return Graphics::PixelFormat(2, 5, 5, 5, 0, 0, 5, 10, 0); }
	static Graphics::PixelFormat abgr4444() { return Graphics::PixelFormat(2, 4, 4, 4, 4, 0, 4, 8, 12); }
	static Graphics::PixelFormat bgra4444() { return Graphics::PixelFormat(2, 4, 4, 4, 4, 4, 8, 12, 0); }
	static Graphics::PixelFormat rgba8888() { return Graphics::PixelFormat(4, 8, 8, 8, 8, 24, 16, 8, 0); }
	static Graphics::PixelFormat argb8888() { return Graphics::PixelFormat(4, 8, 8, 8, 8, 16, 8, 0, 24); }
	static Graphics::PixelFormat rgb888() { return Graphics::PixelFormat(3, 8, 8, 8, 0, 16, 8, 0, 0); }
	static Graphics::PixelFormat abgr8888() { return Graphics::PixelFormat(4, 8, 8, 8, 8, 0, 8, 16, 24); }
	static Graphics::PixelFormat bgra8888() { return Graphics::PixelFormat(4, 8, 8, 8, 8, 8, 16, 24, 0); }
	static Graphics::PixelFormat bgr888() { return Graphics::PixelFormat(3, 8, 8, 8, 0, 0, 8, 16, 0); }
	static Graphics::PixelFormat xrgb8888() { return Graphics::PixelFormat(4, 8, 8, 8, 0, 16, 8, 0, 0); }
	static Graphics::PixelFormat clut8() { return Graphics::PixelFormat::createFormatCLUT8(); }

	static void assertList(const FormatList &got, const Graphics::PixelFormat *want, uint n) {
		TS_ASSERT_EQUALS(got.size(), n);
		uint i = 0;
		for (FormatList::const_iterator it = got.begin(); it != got.end() && i < n; ++it, ++i) {
			if (*it != want[i])
				TS_FAIL(Common::String::format("entry %u: got %s, want %s", i,
				                               it->toString().c_str(), want[i].toString().c_str()).c_str());
		}
	}

public:
	void test_hw32_format_is_xrgb8888() {
		TS_ASSERT(SurfaceSdlHwFormat::hwFormat32() == xrgb8888());
	}

	void test_want_hw32_only_for_4_byte_requests_or_the_key() {
		TS_ASSERT(!SurfaceSdlHwFormat::wantHwScreen32(clut8(), false));
		TS_ASSERT(!SurfaceSdlHwFormat::wantHwScreen32(rgb565(), false));
		TS_ASSERT(!SurfaceSdlHwFormat::wantHwScreen32(rgb555(), false));
		TS_ASSERT(!SurfaceSdlHwFormat::wantHwScreen32(rgb888(), false));
		TS_ASSERT(SurfaceSdlHwFormat::wantHwScreen32(argb8888(), false));
		TS_ASSERT(SurfaceSdlHwFormat::wantHwScreen32(rgba8888(), false));
		TS_ASSERT(SurfaceSdlHwFormat::wantHwScreen32(xrgb8888(), false));
		// The key forces it whatever the game asked for.
		TS_ASSERT(SurfaceSdlHwFormat::wantHwScreen32(clut8(), true));
		TS_ASSERT(SurfaceSdlHwFormat::wantHwScreen32(rgb565(), true));
	}

	// Without the renderer path's 32-bit offer the list is exactly what the
	// backend always produced with a 565 screen.
	void test_565_screen_without_offer_is_unchanged() {
		FormatList l;
		const Graphics::PixelFormat hw = rgb565();
		SurfaceSdlHwFormat::buildSupportedFormats(l, &hw, false, false);
		const Graphics::PixelFormat want[] = {
			rgb565(), xrgb1555(), rgb555(), rgba4444(), argb4444(),
			bgr565(), xbgr1555(), bgr555(), abgr4444(), bgra4444(),
			clut8()
		};
		assertList(l, want, ARRAYSIZE(want));
	}

	// With the offer, the 16-bit part is untouched (front() and list-order
	// negotiation give the same answer) and the 4-byte formats follow it,
	// the hw candidate first.
	void test_565_screen_with_offer_appends_32bit_after_16bit() {
		FormatList l;
		const Graphics::PixelFormat hw = rgb565();
		SurfaceSdlHwFormat::buildSupportedFormats(l, &hw, false, true);
		const Graphics::PixelFormat want[] = {
			rgb565(), xrgb1555(), rgb555(), rgba4444(), argb4444(),
			bgr565(), xbgr1555(), bgr555(), abgr4444(), bgra4444(),
			xrgb8888(), rgba8888(), argb8888(), abgr8888(), bgra8888(),
			clut8()
		};
		assertList(l, want, ARRAYSIZE(want));
	}

	void test_32bit_screen_lists_hw_first_then_everything() {
		FormatList l;
		const Graphics::PixelFormat hw = xrgb8888();
		SurfaceSdlHwFormat::buildSupportedFormats(l, &hw, false, true);
		const Graphics::PixelFormat want[] = {
			xrgb8888(),
			rgba8888(), argb8888(), rgb888(),
			rgb565(), xrgb1555(), rgb555(), rgba4444(), argb4444(),
			abgr8888(), bgra8888(), bgr888(),
			bgr565(), xbgr1555(), bgr555(), abgr4444(), bgra4444(),
			clut8()
		};
		assertList(l, want, ARRAYSIZE(want));
		// The first 4-byte entry, which SCI and SCUMM pick, is the hw format.
		for (FormatList::const_iterator it = l.begin(); it != l.end(); ++it) {
			if (it->bytesPerPixel == 4) {
				TS_ASSERT(*it == xrgb8888());
				break;
			}
		}
	}

	void test_no_screen_yet_lists_everything() {
		FormatList l;
		SurfaceSdlHwFormat::buildSupportedFormats(l, nullptr, false, true);
		const Graphics::PixelFormat want[] = {
			rgba8888(), argb8888(), rgb888(),
			rgb565(), xrgb1555(), rgb555(), rgba4444(), argb4444(),
			abgr8888(), bgra8888(), bgr888(),
			bgr565(), xbgr1555(), bgr555(), abgr4444(), bgra4444(),
			clut8()
		};
		assertList(l, want, ARRAYSIZE(want));
	}

	void test_hw_palette_lists_only_hw_and_clut8() {
		FormatList l;
		const Graphics::PixelFormat hw = clut8();
		SurfaceSdlHwFormat::buildSupportedFormats(l, &hw, true, true);
		const Graphics::PixelFormat want[] = { clut8(), clut8() };
		assertList(l, want, ARRAYSIZE(want));
	}
};
