#include <cxxtest/TestSuite.h>
#include "backends/platform/dos/dos-modes.h"

class DosModesTestSuite : public CxxTest::TestSuite {
	static Common::Array<DOS::VideoMode> staging() {
		// 640x400 as DOSBox Staging's S3 offers it, measured 2026-09-28.
		Common::Array<DOS::VideoMode> m;
		DOS::VideoMode a = { 640, 400, DOS::xrgb8888(), 0 };
		DOS::VideoMode b = { 640, 400, Graphics::PixelFormat::createFormatCLUT8(), 1 };
		DOS::VideoMode c = { 640, 480, DOS::rgb565(), 2 };
		DOS::VideoMode d = { 320, 200, Graphics::PixelFormat::createFormatCLUT8(), 3 };
		m.push_back(a); m.push_back(b); m.push_back(c); m.push_back(d);
		return m;
	}
	static Common::Array<DOS::VideoMode> dosboxX() {
		Common::Array<DOS::VideoMode> m = staging();
		DOS::VideoMode e = { 640, 400, DOS::rgb565(), 4 };
		DOS::VideoMode f = { 640, 400, DOS::xrgb1555(), 5 };
		m.push_back(e); m.push_back(f);
		return m;
	}

public:
	void test_exact_mode_found() {
		TS_ASSERT_EQUALS(DOS::findExactMode(staging(), 320, 200, Graphics::PixelFormat::createFormatCLUT8()), 3);
	}

	void test_exact_mode_missing_format() {
		TS_ASSERT_EQUALS(DOS::findExactMode(staging(), 640, 400, DOS::rgb565()), -1);
	}

	void test_formats_prefer_16bit_then_clut8_last() {
		Common::List<Graphics::PixelFormat> got = DOS::supportedFormats(dosboxX(), 640, 400);
		Common::List<Graphics::PixelFormat>::const_iterator it = got.begin();
		TS_ASSERT_EQUALS(got.size(), 4u);
		TS_ASSERT(*it++ == DOS::rgb565());
		TS_ASSERT(*it++ == DOS::xrgb1555());
		TS_ASSERT(*it++ == DOS::xrgb8888());
		TS_ASSERT(*it++ == Graphics::PixelFormat::createFormatCLUT8());
	}

	void test_formats_only_what_the_size_has() {
		Common::List<Graphics::PixelFormat> got = DOS::supportedFormats(staging(), 640, 400);
		TS_ASSERT_EQUALS(got.size(), 2u);
		TS_ASSERT(got.front() == DOS::xrgb8888());
		TS_ASSERT(got.back() == Graphics::PixelFormat::createFormatCLUT8());
	}

	void test_clut8_even_without_a_mode() {
		Common::List<Graphics::PixelFormat> got = DOS::supportedFormats(staging(), 800, 600);
		TS_ASSERT_EQUALS(got.size(), 1u);
		TS_ASSERT(got.front() == Graphics::PixelFormat::createFormatCLUT8());
	}
};
