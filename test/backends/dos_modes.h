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
		Common::List<Graphics::PixelFormat> got = DOS::supportedFormats(m, s.w, s.h, Graphics::kHiResTargetAuto);
		TS_ASSERT_EQUALS(got.size(), 2u);
		TS_ASSERT(got.front() == DOS::xrgb8888());
		// What asking for the launcher's size itself would have said.
		TS_ASSERT_EQUALS(DOS::supportedFormats(m, 320, 200, Graphics::kHiResTargetAuto).size(), 1u);
	}

	static bool sameList(const Common::List<Graphics::PixelFormat> &got, const Graphics::PixelFormat *want, uint n) {
		if (got.size() != n)
			return false;
		Common::List<Graphics::PixelFormat>::const_iterator it = got.begin();
		for (uint i = 0; i < n; ++i, ++it)
			if (!(*it == want[i]))
				return false;
		return true;
	}

	// An explicit target comes first, then the other true-colour family
	// (never 1-5-5-5), then CLUT8: what the engine falls back to in the
	// order rgb888, rgb565, clut8.
	void test_render_target_caps_the_list() {
		const Graphics::PixelFormat clut8 = Graphics::PixelFormat::createFormatCLUT8();

		const Graphics::PixelFormat x565[] = { DOS::rgb565(), DOS::xrgb8888(), clut8 };
		TS_ASSERT(sameList(DOS::supportedFormats(dosboxX(), 640, 400, Graphics::kHiResTargetRgb565), x565, 3));

		// Staging has no 640x400 5-6-5: the 640x480 line-repeat mode serves it.
		TS_ASSERT(sameList(DOS::supportedFormats(staging(), 640, 400, Graphics::kHiResTargetRgb565), x565, 3));

		const Graphics::PixelFormat x888[] = { DOS::xrgb8888(), DOS::rgb565(), clut8 };
		TS_ASSERT(sameList(DOS::supportedFormats(dosboxX(), 640, 400, Graphics::kHiResTargetRgb888), x888, 3));
		TS_ASSERT(sameList(DOS::supportedFormats(staging(), 640, 400, Graphics::kHiResTargetRgb888), x888, 3));

		const Graphics::PixelFormat onlyClut[] = { clut8 };
		TS_ASSERT(sameList(DOS::supportedFormats(dosboxX(), 640, 400, Graphics::kHiResTargetClut8), onlyClut, 1));

		// auto is unchanged: every format, 1-5-5-5 included.
		const Graphics::PixelFormat all[] = { DOS::rgb565(), DOS::xrgb1555(), DOS::xrgb8888(), clut8 };
		TS_ASSERT(sameList(DOS::supportedFormats(dosboxX(), 640, 400, Graphics::kHiResTargetAuto), all, 4));
	}

	void test_render_target_without_rgb565_hardware() {
		// 640x400 true colour and 1-5-5-5, no 5-6-5 at any size: rgb565
		// gets true colour rather than CLUT8, and 1-5-5-5 never stands in.
		const Graphics::PixelFormat clut8 = Graphics::PixelFormat::createFormatCLUT8();
		Common::Array<DOS::VideoMode> m;
		DOS::VideoMode a = { 640, 400, DOS::xrgb8888() };
		DOS::VideoMode b = { 640, 400, DOS::xrgb1555() };
		DOS::VideoMode c = { 640, 400, clut8 };
		m.push_back(a); m.push_back(b); m.push_back(c);
		const Graphics::PixelFormat tc[] = { DOS::xrgb8888(), clut8 };
		TS_ASSERT(sameList(DOS::supportedFormats(m, 640, 400, Graphics::kHiResTargetRgb565), tc, 2));
		TS_ASSERT(sameList(DOS::supportedFormats(m, 640, 400, Graphics::kHiResTargetRgb888), tc, 2));

		// Only 1-5-5-5: neither target has a family to offer.
		Common::Array<DOS::VideoMode> m555;
		m555.push_back(b); m555.push_back(c);
		const Graphics::PixelFormat onlyClut[] = { clut8 };
		TS_ASSERT(sameList(DOS::supportedFormats(m555, 640, 400, Graphics::kHiResTargetRgb565), onlyClut, 1));
		TS_ASSERT(sameList(DOS::supportedFormats(m555, 640, 400, Graphics::kHiResTargetRgb888), onlyClut, 1));
	}

	void test_render_target_cap_only_while_a_game_runs() {
		bool invalid = false;
		TS_ASSERT_EQUALS(DOS::renderTargetCap(false, "clut8", invalid), Graphics::kHiResTargetAuto);	// launcher
		TS_ASSERT_EQUALS(DOS::renderTargetCap(true, "clut8", invalid), Graphics::kHiResTargetClut8);
		TS_ASSERT_EQUALS(DOS::renderTargetCap(true, "RGB888", invalid), Graphics::kHiResTargetRgb888);
		TS_ASSERT_EQUALS(DOS::renderTargetCap(true, "auto", invalid), Graphics::kHiResTargetAuto);
		TS_ASSERT_EQUALS(DOS::renderTargetCap(true, "", invalid), Graphics::kHiResTargetAuto);
		TS_ASSERT(!invalid);
		TS_ASSERT_EQUALS(DOS::renderTargetCap(true, "truecolor", invalid), Graphics::kHiResTargetAuto);
		TS_ASSERT(invalid);
		// The launcher never looks at the value, so it never calls it invalid.
		invalid = false;
		TS_ASSERT_EQUALS(DOS::renderTargetCap(false, "truecolor", invalid), Graphics::kHiResTargetAuto);
		TS_ASSERT(!invalid);
	}
};
