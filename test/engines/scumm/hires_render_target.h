#include <cxxtest/TestSuite.h>

#include "common/memstream.h"
#include "engines/scumm/hires_text.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/hires_options.h"

class ScummHiResRenderTargetTestSuite : public CxxTest::TestSuite {
	Graphics::HiResMap map(const char *render) {
		const Common::String text = Common::String("[map]\nversion=2\n[render]\n") + render;
		Common::MemoryReadStream s((const byte *)text.c_str(), text.size());
		Common::Array<Common::String> q;
		Graphics::HiResMap m;
		TS_ASSERT(Graphics::HiResFontMap::loadMap(s, Common::Path("/m", '/'), q, Graphics::kHiResKeysScumm, m));
		return m;
	}

public:
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
};
