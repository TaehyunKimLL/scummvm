#include <cxxtest/TestSuite.h>
#include "backends/platform/dos/dos-modes.h"
#include "graphics/hires_text/hires_options.h"

class DosModesTestSuite : public CxxTest::TestSuite {
	static Common::Array<DOS::VideoMode> staging() {
		// DOSBox Staging 0.83 svga_s3, measured 2026-11-23 (P7 plan 0.1): 640x400 only 8888 and 8 bpp.
		Common::Array<DOS::VideoMode> m;
		const DOS::VideoMode l[] = { { 640, 400, DOS::xrgb8888() }, { 640, 400, Graphics::PixelFormat::createFormatCLUT8() },
			{ 640, 480, DOS::xrgb8888() }, { 640, 480, DOS::rgb565() }, { 640, 480, DOS::xrgb1555() },
			{ 640, 480, Graphics::PixelFormat::createFormatCLUT8() }, { 320, 200, Graphics::PixelFormat::createFormatCLUT8() } };
		for (uint i = 0; i < ARRAYSIZE(l); ++i)
			m.push_back(l[i]);
		return m;
	}
	static Common::Array<DOS::VideoMode> dosboxX() {
		Common::Array<DOS::VideoMode> m = staging();
		const DOS::VideoMode e = { 640, 400, DOS::rgb565() }, f = { 640, 400, DOS::xrgb1555() };
		m.push_back(e);
		m.push_back(f);
		return m;
	}
	static bool sameList(const Common::List<Graphics::PixelFormat> &got, const Graphics::PixelFormat *want, uint n) {
		if (got.size() != n)
			return false;
		uint i = 0;
		for (Common::List<Graphics::PixelFormat>::const_iterator it = got.begin(); it != got.end(); ++it, ++i)
			if (!(*it == want[i]))
				return false;
		return true;
	}

public:
	void test_exact_mode_found() {
		TS_ASSERT_EQUALS(DOS::findExactMode(staging(), 320, 200, Graphics::PixelFormat::createFormatCLUT8()), 6);
	}

	void test_exact_mode_missing_format() {
		TS_ASSERT_EQUALS(DOS::findExactMode(staging(), 640, 400, DOS::rgb565()), -1);
	}

	void test_auto_offers_16_then_15_bit_then_clut8() {
		const Graphics::PixelFormat want[] = { DOS::rgb565(), DOS::xrgb1555(), Graphics::PixelFormat::createFormatCLUT8() };
		TS_ASSERT(sameList(DOS::supportedFormats(dosboxX(), 640, 400), want, 3));
		TS_ASSERT(sameList(DOS::supportedFormats(staging(), 640, 400), want, 3));	// both through the 640x480 line repeat
	}

	void test_rgb888_only_when_asked() {
		const Graphics::PixelFormat want[] = { DOS::xrgb8888(), DOS::rgb565(), DOS::xrgb1555(),
			Graphics::PixelFormat::createFormatCLUT8() };
		TS_ASSERT(sameList(DOS::supportedFormats(dosboxX(), 640, 400, Graphics::kHiResTargetRgb888), want, 4));
	}

	void test_clut8_alone() {
		const Graphics::PixelFormat want[] = { Graphics::PixelFormat::createFormatCLUT8() };
		TS_ASSERT(sameList(DOS::supportedFormats(dosboxX(), 640, 400, Graphics::kHiResTargetClut8), want, 1));
	}

	void test_engine_auto_picks_565_on_dos() {
		// The engine's choice over the DOS list (an AA map, nothing configured): 5-6-5, not 8888.
		const uint32 offered = Graphics::hiResTargetsOffered(DOS::supportedFormats(dosboxX(), 640, 400));
		Common::String note;
		TS_ASSERT_EQUALS(Graphics::resolveRenderTarget(Graphics::HiResOptions(), nullptr, true, offered, note),
		                 Graphics::kHiResTargetRgb565);
		Graphics::PixelFormat f;
		TS_ASSERT(Graphics::hiResPickScreenFormat(Graphics::kHiResTargetRgb555, DOS::supportedFormats(dosboxX(), 640, 400), f));
		TS_ASSERT(f == DOS::xrgb1555());
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
		// No 640x400 RGB565 mode, but 640x480 RGB565 exists (index 3).
		DOS::ModeChoice c = DOS::chooseMode(staging(), 640, 400, DOS::rgb565(), false);
		TS_ASSERT_EQUALS(c.index, 3);
		TS_ASSERT_EQUALS(c.lineRepeat, true);
	}

	void test_choose_mode_force_fallback_skips_exact_match() {
		// XRGB8888 has an exact 640x400 match, but forceFallback skips it
		// and picks the 640x480 XRGB8888 mode instead.
		Common::Array<DOS::VideoMode> modes = staging();
		DOS::ModeChoice c = DOS::chooseMode(modes, 640, 400, DOS::xrgb8888(), true);
		TS_ASSERT_EQUALS(c.index, 2);
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
		// RGB565: SCI asks after the launcher's 320x200 initSize() and
		// must still see RGB565 (for 640x400 by line repeat).
		Common::Array<DOS::VideoMode> m;
		DOS::VideoMode a = { 640, 480, DOS::rgb565() };
		DOS::VideoMode b = { 320, 200, Graphics::PixelFormat::createFormatCLUT8() };
		m.push_back(a); m.push_back(b);
		const DOS::FormatsSize s = DOS::formatsSize(320, 200);
		Common::List<Graphics::PixelFormat> got = DOS::supportedFormats(m, s.w, s.h, Graphics::kHiResTargetAuto);
		TS_ASSERT_EQUALS(got.size(), 2u);
		TS_ASSERT(got.front() == DOS::rgb565());
		// What asking for the launcher's size itself would have said.
		TS_ASSERT_EQUALS(DOS::supportedFormats(m, 320, 200, Graphics::kHiResTargetAuto).size(), 1u);
	}

	void test_render_target_cap_only_while_a_game_runs() {
		bool invalid = false;
		TS_ASSERT_EQUALS(DOS::renderTargetCap(false, "clut8", invalid), Graphics::kHiResTargetAuto);	// launcher
		TS_ASSERT_EQUALS(DOS::renderTargetCap(true, "clut8", invalid), Graphics::kHiResTargetClut8);
		TS_ASSERT_EQUALS(DOS::renderTargetCap(true, "RGB888", invalid), Graphics::kHiResTargetRgb888);
		TS_ASSERT_EQUALS(DOS::renderTargetCap(true, "rgb555", invalid), Graphics::kHiResTargetRgb555);
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
