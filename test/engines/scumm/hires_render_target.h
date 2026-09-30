#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/memstream.h"
#include "engines/scumm/hires_overlay.h"
#include "engines/scumm/hires_text.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/hires_options.h"
#include "graphics/pixelformat.h"

#include "../../system/null_osystem.h"

class ScummHiResRenderTargetTestSuite : public CxxTest::TestSuite {
	Graphics::HiResMap map(const char *render) {
		const Common::String text = Common::String("[map]\nversion=2\n[render]\n") + render;
		Common::MemoryReadStream s((const byte *)text.c_str(), text.size());
		Common::Array<Common::String> q;
		Graphics::HiResMap m;
		TS_ASSERT(Graphics::HiResFontMap::loadMap(s, Common::Path("/m", '/'), q, Graphics::kHiResKeysScumm, m));
		return m;
	}

	static void writeFile(const Common::Path &path, const Common::String &text) {
		Common::DumpFile f;
		TS_ASSERT(f.open(path));
		f.write(text.c_str(), text.size());
		f.close();
	}

	/// A minimal SVFN header: magic, then the bpp byte at offset 8 (the
	/// coverage probe hires_alpha_default.h's faceHasCoverage() test reads;
	/// mapHasCoverage() here reaches the same probe through the map).
	static void writeSvf(const Common::Path &path, byte bpp) {
		Common::Array<byte> b(16, (byte)0);
		b[0] = 'S'; b[1] = 'V'; b[2] = 'F'; b[3] = 'N';
		b[8] = bpp;
		Common::DumpFile f;
		TS_ASSERT(f.open(path));
		f.write(b.begin(), b.size());
		f.close();
	}

public:
	void setUp() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::install_null_g_system();
#endif
	}

	void tearDown() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::uninstall_null_g_system();
#endif
	}

	void test_explicit_map_target() {
		Common::String w;
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::wantedTarget(map("target=rgb565\n"), true, Graphics::HiResIniOverrides(), 5, true, w),
						 Graphics::kHiResTargetRgb565);
	}

	void test_ini_beats_map() {
		Graphics::HiResIniOverrides ini;
		ini.targetSet = true;
		ini.target = Graphics::kHiResTargetClut8;
		Common::String w;
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::wantedTarget(map("target=rgb888\n"), true, ini, 5, true, w),
						 Graphics::kHiResTargetClut8);
		TS_ASSERT(w.empty());
	}

	void test_auto_follows_coverage_and_blend() {
		Common::String w;
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::wantedTarget(map("target=auto\n"), true, Graphics::HiResIniOverrides(), 5, false, w),
						 Graphics::kHiResTargetClut8);
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::wantedTarget(map("blend=auto\n"), true, Graphics::HiResIniOverrides(), 5, true, w),
						 Graphics::kHiResTargetRgb888);
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::wantedTarget(map("blend=off\n"), true, Graphics::HiResIniOverrides(), 5, true, w),
						 Graphics::kHiResTargetClut8);
	}

	void test_ini_auto_defers_to_the_map() {
		Graphics::HiResIniOverrides ini;
		ini.targetSet = true;
		ini.target = Graphics::kHiResTargetAuto;
		Common::String w;
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::wantedTarget(map("target=rgb565\n"), true, ini, 5, true, w),
						 Graphics::kHiResTargetRgb565);
	}

	void test_v7_stays_paletted() {
		Common::String w;
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::wantedTarget(map("target=rgb888\n"), true, Graphics::HiResIniOverrides(), 7, true, w),
						 Graphics::kHiResTargetClut8);
		TS_ASSERT_EQUALS(w, "SCUMM v7+ keeps a paletted screen; render_target=rgb888 ignored");
	}

	void test_scale_default_limits_and_dos() {
		Common::String w;
		const Graphics::HiResScaleLimits desktop = { 1, 3 };
		const Graphics::HiResScaleLimits dos = { 2, 2 };
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::resolvedScale(map("target=auto\n"), true, Graphics::HiResIniOverrides(), desktop, w), 2);
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::resolvedScale(map("scale=3\n"), true, Graphics::HiResIniOverrides(), desktop, w), 3);
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::resolvedScale(map("scale=3\n"), true, Graphics::HiResIniOverrides(), dos, w), 2);
		TS_ASSERT_EQUALS(w, "the DOS backend runs hi-res text at 2x only; using 2");
		Graphics::HiResIniOverrides ini;
		ini.scaleSet = true;
		ini.scale = 1;
		w.clear();
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::resolvedScale(map("scale=3\n"), true, ini, desktop, w), 1);
	}

	/**
	 * Task 8 review M2: adoptScreen() must refresh wantsAlpha() alongside
	 * blend()/anyCoverage(), or createCoverage() (gated on wantsAlpha(),
	 * called once after adoptScreen()/setAlphaActive() in
	 * ScummEngine::init()) can be left never allocating a coverage surface
	 * for a corrected target that now wants blending - alphaActive() would
	 * then say blending is on with nothing for gfx.cpp's composite path to
	 * read.
	 *
	 * The map's predicted (clut8) section is `blend=off` with a 1bpp face -
	 * nothing wants blending. Its rgb888 section is `blend=auto` with a 2bpp
	 * (covering) face - once the backend actually hands back an rgb888
	 * screen, blend()/anyCoverage() (and, with the fix, wantsAlpha()) must
	 * all agree that blending is wanted.
	 */
	void test_adopt_screen_refreshes_wants_alpha_and_coverage() {
		Common::FSNode tmp("/tmp/scummvm-hires-adopt-screen-test");
		tmp.createDirectory();
		const Common::Path dir = tmp.getPath();

		const Common::Path onePath = dir.join(Common::Path("one.svf", '/'));
		const Common::Path twoPath = dir.join(Common::Path("two.svf", '/'));
		writeSvf(onePath, 1);
		writeSvf(twoPath, 2);

		const Common::Path mapPath = dir.join(Common::Path("test.map", '/'));
		writeFile(mapPath, Common::String::format(
			"[map]\nversion=2\n"
			"[render]\nblend=off\n"
			"[font.0]\nface=%s\n"
			"[render:rgb888]\nblend=auto\n"
			"[font.0:rgb888]\nface=%s\n",
			onePath.toString().c_str(), twoPath.toString().c_str()));

		Common::Array<Common::String> qualifiers;
		qualifiers.push_back("v5");

		Graphics::HiResMap predicted;
		Graphics::HiResMapLoadOptions opts;
		opts.target = Graphics::kHiResTargetClut8;
		TS_ASSERT(Graphics::HiResFontMap::loadMapFile(mapPath, qualifiers, Graphics::kHiResKeysScumm, predicted, opts));

		Scumm::ScummHiResText hr;
		Scumm::HiResOverlay overlay;
		overlay.create(16, 16, false); // the index plane; createCoverage() adds the coverage plane onto it
		hr.useOverlay(&overlay);
		hr.adoptMapForScreenTest(predicted, Graphics::HiResIniOverrides(), Graphics::kHiResTargetClut8, 5, mapPath, qualifiers);

		TS_ASSERT_EQUALS(hr.blend(), Graphics::kHiResBlendOff);
		TS_ASSERT(!hr.anyCoverage());
		TS_ASSERT(!hr.wantsAlpha());
		hr.createCoverage(16, 16);
		TS_ASSERT(!hr.coverage());

		// The backend actually gave an rgb888 screen.
		hr.adoptScreen(Graphics::PixelFormat(4, 8, 8, 8, 0, 16, 8, 0, 0));
		TS_ASSERT_EQUALS(hr.renderTarget(), Graphics::kHiResTargetRgb888);
		TS_ASSERT_EQUALS(hr.blend(), Graphics::kHiResBlendAuto);
		TS_ASSERT(hr.anyCoverage());
		// The fix under test: wantsAlpha() follows, rather than staying
		// stale at the predicted (clut8) section's answer.
		TS_ASSERT(hr.wantsAlpha());

		// createCoverage()'s gate now matches: the surface is allocated,
		// consistent with alphaActive() once scumm.cpp sets it the same way
		// scumm.cpp itself would (Graphics::blendActive() off the now-
		// corrected blend()/anyCoverage(), screen not CLUT8).
		hr.createCoverage(16, 16);
		TS_ASSERT(hr.coverage());
		hr.setAlphaActive(Graphics::blendActive(hr.blend(), hr.anyCoverage(), false));
		TS_ASSERT(hr.alphaActive());
	}
};
