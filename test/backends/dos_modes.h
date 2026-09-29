#include <cxxtest/TestSuite.h>
#include "backends/platform/dos/dos-modes.h"

class DosModesTestSuite : public CxxTest::TestSuite {
	static Common::Array<DOS::VideoMode> staging() {
		// 640x400 as DOSBox Staging's S3 offers it, measured 2026-09-28.
		Common::Array<DOS::VideoMode> m;
		DOS::VideoMode a = { 640, 400, DOS::xrgb8888() };
		DOS::VideoMode b = { 640, 400, Graphics::PixelFormat::createFormatCLUT8() };
		DOS::VideoMode c = { 640, 480, DOS::rgb565() };
		DOS::VideoMode d = { 320, 200, Graphics::PixelFormat::createFormatCLUT8() };
		m.push_back(a); m.push_back(b); m.push_back(c); m.push_back(d);
		return m;
	}
	static Common::Array<DOS::VideoMode> dosboxX() {
		Common::Array<DOS::VideoMode> m = staging();
		DOS::VideoMode e = { 640, 400, DOS::rgb565() };
		DOS::VideoMode f = { 640, 400, DOS::xrgb1555() };
		m.push_back(e); m.push_back(f);
		return m;
	}
	// Staging list plus a 640x480 XRGB8888 mode, to exercise a forced
	// fallback for a format that also has an exact match.
	static Common::Array<DOS::VideoMode> stagingWithTrueColorFallback() {
		Common::Array<DOS::VideoMode> m = staging();
		DOS::VideoMode g = { 640, 480, DOS::xrgb8888() };
		m.push_back(g);
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

	// 640x400 has no exact RGB565 mode in the Staging list, but 640x480
	// RGB565 exists, so RGB565 is reported too (via the line-repeat
	// fallback) alongside the exact XRGB8888 match.
	void test_formats_include_fallback_capable_formats() {
		Common::List<Graphics::PixelFormat> got = DOS::supportedFormats(staging(), 640, 400);
		Common::List<Graphics::PixelFormat>::const_iterator it = got.begin();
		TS_ASSERT_EQUALS(got.size(), 3u);
		TS_ASSERT(*it++ == DOS::rgb565());
		TS_ASSERT(*it++ == DOS::xrgb8888());
		TS_ASSERT(*it++ == Graphics::PixelFormat::createFormatCLUT8());
	}

	void test_clut8_even_without_a_mode() {
		Common::List<Graphics::PixelFormat> got = DOS::supportedFormats(staging(), 800, 600);
		TS_ASSERT_EQUALS(got.size(), 1u);
		TS_ASSERT(got.front() == Graphics::PixelFormat::createFormatCLUT8());
	}

	void test_choose_mode_exact_match() {
		DOS::ModeChoice c = DOS::chooseMode(staging(), 640, 400, DOS::xrgb8888(), false);
		TS_ASSERT_EQUALS(c.index, 0);
		TS_ASSERT_EQUALS(c.lineRepeat, false);
	}

	void test_choose_mode_falls_back_to_640x480() {
		// No 640x400 RGB565 mode, but 640x480 RGB565 exists (index 2).
		DOS::ModeChoice c = DOS::chooseMode(staging(), 640, 400, DOS::rgb565(), false);
		TS_ASSERT_EQUALS(c.index, 2);
		TS_ASSERT_EQUALS(c.lineRepeat, true);
	}

	void test_choose_mode_force_fallback_skips_exact_match() {
		// XRGB8888 has an exact 640x400 match, but forceFallback skips it
		// and picks the 640x480 XRGB8888 mode instead.
		Common::Array<DOS::VideoMode> modes = stagingWithTrueColorFallback();
		DOS::ModeChoice c = DOS::chooseMode(modes, 640, 400, DOS::xrgb8888(), true);
		TS_ASSERT_EQUALS(c.index, 4);
		TS_ASSERT_EQUALS(c.lineRepeat, true);
	}

	void test_choose_mode_no_fallback_when_height_not_multiple_of_5() {
		// 399 % 5 != 0, so no fallback height is even considered, and
		// there is no exact 640x399 mode either.
		DOS::ModeChoice c = DOS::chooseMode(staging(), 640, 399, DOS::rgb565(), false);
		TS_ASSERT_EQUALS(c.index, -1);
		TS_ASSERT_EQUALS(c.lineRepeat, false);
	}

	void test_choose_mode_missing_entirely() {
		DOS::ModeChoice c = DOS::chooseMode(staging(), 800, 600, DOS::xrgb8888(), false);
		TS_ASSERT_EQUALS(c.index, -1);
		TS_ASSERT_EQUALS(c.lineRepeat, false);
	}

	void test_formats_size_is_640x400_until_something_larger() {
		TS_ASSERT_EQUALS(DOS::formatsSize(0, 0).w, 640u);
		TS_ASSERT_EQUALS(DOS::formatsSize(0, 0).h, 400u);
		TS_ASSERT_EQUALS(DOS::formatsSize(320, 200).w, 640u);	// the launcher's setupGraphics()
		TS_ASSERT_EQUALS(DOS::formatsSize(320, 200).h, 400u);
		TS_ASSERT_EQUALS(DOS::formatsSize(640, 400).h, 400u);
		TS_ASSERT_EQUALS(DOS::formatsSize(800, 600).w, 800u);
		TS_ASSERT_EQUALS(DOS::formatsSize(800, 600).h, 600u);
		TS_ASSERT_EQUALS(DOS::formatsSize(640, 480).h, 480u);
	}

	void test_true_color_advertised_after_320x200_with_only_a_640x480_mode() {
		// No 320x200/320x240 or 640x400 true-colour mode, only 640x480
		// XRGB8888: SCI asks after the launcher's 320x200 initSize() and
		// must still see XRGB8888 (for 640x400 by line repeat).
		Common::Array<DOS::VideoMode> m;
		DOS::VideoMode a = { 640, 480, DOS::xrgb8888() };
		DOS::VideoMode b = { 320, 200, Graphics::PixelFormat::createFormatCLUT8() };
		m.push_back(a); m.push_back(b);
		const DOS::FormatsSize s = DOS::formatsSize(320, 200);
		Common::List<Graphics::PixelFormat> got = DOS::supportedFormats(m, s.w, s.h, true);
		TS_ASSERT_EQUALS(got.size(), 2u);
		TS_ASSERT(got.front() == DOS::xrgb8888());
		// What asking for the launcher's size itself would have said.
		TS_ASSERT_EQUALS(DOS::supportedFormats(m, 320, 200, true).size(), 1u);
	}

	void test_disallow_true_color_reports_clut8_only() {
		Common::List<Graphics::PixelFormat> got = DOS::supportedFormats(dosboxX(), 640, 400, false);
		TS_ASSERT_EQUALS(got.size(), 1u);
		TS_ASSERT(got.front() == Graphics::PixelFormat::createFormatCLUT8());
	}
};
