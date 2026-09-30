#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/list.h"
#include "common/str.h"
#include "graphics/hires_text/hires_options.h"
#include "graphics/pixelformat.h"

class HiResOptionsTestSuite : public CxxTest::TestSuite {
	struct FakeIni {
		Common::HashMap<Common::String, Common::String> game, global;
	};

	static bool get(const char *key, bool globalFallback, Common::String &value, void *ctx) {
		FakeIni *ini = (FakeIni *)ctx;
		if (ini->game.tryGetVal(key, value))
			return true;
		return globalFallback && ini->global.tryGetVal(key, value);
	}

	static Graphics::PixelFormat rgb565() { return Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0); }
	static Graphics::PixelFormat xrgb1555() { return Graphics::PixelFormat(2, 5, 5, 5, 0, 10, 5, 0, 0); }
	static Graphics::PixelFormat xrgb8888() { return Graphics::PixelFormat(4, 8, 8, 8, 0, 16, 8, 0, 0); }

public:
	void test_parsers() {
		Graphics::HiResRenderTarget t;
		TS_ASSERT(Graphics::parseRenderTarget("RGB565", t));
		TS_ASSERT_EQUALS(t, Graphics::kHiResTargetRgb565);
		TS_ASSERT(!Graphics::parseRenderTarget("argb8888", t));
		TS_ASSERT(!Graphics::parseRenderTarget("truecolor", t));
		Graphics::HiResBlend b;
		TS_ASSERT(Graphics::parseBlend("off", b));
		TS_ASSERT(!Graphics::parseBlend("true", b));
		Graphics::HiResAdvance a;
		TS_ASSERT(Graphics::parseAdvance("cell", a));
		TS_ASSERT(!Graphics::parseAdvance("engine", a));
		TS_ASSERT(!Graphics::parseAdvance("ttf", a));
	}

	void test_format_matching() {
		TS_ASSERT(Graphics::formatMatchesTarget(rgb565(), Graphics::kHiResTargetRgb565));
		TS_ASSERT(!Graphics::formatMatchesTarget(xrgb1555(), Graphics::kHiResTargetRgb565));
		TS_ASSERT(Graphics::formatMatchesTarget(xrgb8888(), Graphics::kHiResTargetRgb888));
		TS_ASSERT(Graphics::formatMatchesTarget(Graphics::PixelFormat::createFormatCLUT8(), Graphics::kHiResTargetClut8));
	}

	void test_format_request_order_and_fallback() {
		Common::List<Graphics::PixelFormat> dosboxStaging;   // no rgb565 at the size
		dosboxStaging.push_back(xrgb8888());
		dosboxStaging.push_back(Graphics::PixelFormat::createFormatCLUT8());
		Common::String note;
		Common::List<Graphics::PixelFormat> got =
			Graphics::formatRequest(Graphics::kHiResTargetRgb565, dosboxStaging, true, note);
		TS_ASSERT_EQUALS(got.size(), 2u);
		TS_ASSERT(got.front() == xrgb8888());
		TS_ASSERT(got.back().isCLUT8());
		TS_ASSERT_EQUALS(note, "render_target=rgb565 is not available here; using rgb888");

		Common::List<Graphics::PixelFormat> all;
		all.push_back(rgb565());
		all.push_back(xrgb1555());
		all.push_back(xrgb8888());
		all.push_back(Graphics::PixelFormat::createFormatCLUT8());
		note.clear();
		got = Graphics::formatRequest(Graphics::kHiResTargetRgb565, all, false, note);   // engine cannot draw 565
		TS_ASSERT(got.front() == xrgb8888());
		TS_ASSERT(!note.empty());
		note.clear();
		got = Graphics::formatRequest(Graphics::kHiResTargetClut8, all, true, note);
		TS_ASSERT_EQUALS(got.size(), 1u);
		TS_ASSERT(got.front().isCLUT8());
		TS_ASSERT(note.empty());
	}

	void test_auto_target_and_blend() {
		TS_ASSERT_EQUALS(Graphics::resolveAutoTarget(false, Graphics::kHiResBlendAuto), Graphics::kHiResTargetClut8);
		TS_ASSERT_EQUALS(Graphics::resolveAutoTarget(true, Graphics::kHiResBlendAuto), Graphics::kHiResTargetRgb888);
		TS_ASSERT_EQUALS(Graphics::resolveAutoTarget(true, Graphics::kHiResBlendOff), Graphics::kHiResTargetClut8);
		TS_ASSERT_EQUALS(Graphics::resolveAutoTarget(false, Graphics::kHiResBlendOn), Graphics::kHiResTargetRgb888);
		TS_ASSERT(Graphics::blendActive(Graphics::kHiResBlendAuto, true, false));
		TS_ASSERT(!Graphics::blendActive(Graphics::kHiResBlendAuto, false, false));
		TS_ASSERT(!Graphics::blendActive(Graphics::kHiResBlendAuto, true, true));
		TS_ASSERT(!Graphics::blendActive(Graphics::kHiResBlendOff, true, false));
		TS_ASSERT(!Graphics::blendActive(Graphics::kHiResBlendOn, true, true));   // until palette-matched AA (Task 20)
	}

	void test_scale_limits() {
		Graphics::HiResScaleLimits desktop = { 1, 3 };
		Graphics::HiResScaleLimits dos = { 2, 2 };
		Common::String w;
		TS_ASSERT_EQUALS(Graphics::clampScale(3, 1, 3, desktop, "SCUMM", w), 3);
		TS_ASSERT(w.empty());
		TS_ASSERT_EQUALS(Graphics::clampScale(3, 1, 3, dos, "SCUMM", w), 2);
		TS_ASSERT_EQUALS(w, "the DOS backend runs hi-res text at 2x only");
		w.clear();
		TS_ASSERT_EQUALS(Graphics::clampScale(1, 2, 2, desktop, "SCI", w), 2);
		TS_ASSERT_EQUALS(w, "SCI draws hi-res text at 2x only");
	}

	void test_ini_domains_and_validation() {
		FakeIni ini;
		ini.global["render_target"] = "clut8";
		ini.global["hires_text_blend"] = "off";        // not read from [scummvm]
		ini.game["hires_text_scale"] = "two";
		ini.game["hires_text_face"] = "ko, original";
		ini.game["hires_text_size"] = "18";
		Common::Array<Common::String> w;
		Graphics::HiResIniOverrides o = Graphics::readHiResIni(get, &ini, w);
		TS_ASSERT(o.enabled);
		TS_ASSERT(o.targetSet);
		TS_ASSERT_EQUALS(o.target, Graphics::kHiResTargetClut8);
		TS_ASSERT(!o.blendSet);
		TS_ASSERT(!o.scaleSet);
		TS_ASSERT_EQUALS(w.size(), 1u);
		TS_ASSERT_EQUALS(w[0], "hires_text_scale 'two' is not 1, 2 or 3; ignoring it");
		TS_ASSERT(o.faceSet);
		TS_ASSERT_EQUALS(o.face, "ko, original");
		TS_ASSERT_EQUALS(o.size, 18);

		FakeIni old;
		old.game["hires_text_font"] = "x.ttf";      // removed keys are simply not read
		old.game["hires_text_alpha"] = "true";
		old.game["dos_truecolor"] = "off";
		w.clear();
		o = Graphics::readHiResIni(get, &old, w);
		TS_ASSERT(!o.faceSet && !o.blendSet && !o.targetSet);
		TS_ASSERT(w.empty());
	}
};
