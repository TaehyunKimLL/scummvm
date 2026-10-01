#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/config-manager.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/list.h"
#include "common/str.h"
#include "graphics/hires_text/hires_options.h"
#include "graphics/pixelformat.h"

#include "../system/null_osystem.h"

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
		TS_ASSERT_EQUALS(a, Graphics::kHiResAdvanceCell);
		TS_ASSERT(Graphics::parseAdvance("game", a));
		TS_ASSERT_EQUALS(a, Graphics::kHiResAdvanceGame);
		TS_ASSERT(Graphics::parseAdvance("font", a));
		TS_ASSERT_EQUALS(a, Graphics::kHiResAdvanceFont);
		TS_ASSERT(!Graphics::parseAdvance("engine", a));
		TS_ASSERT(!Graphics::parseAdvance("ttf", a));
		Graphics::HiResOrigin o;
		TS_ASSERT(Graphics::parseOrigin("game", o));
		TS_ASSERT_EQUALS(o, Graphics::kHiResOriginGame);
		TS_ASSERT(Graphics::parseOrigin("face", o));
		TS_ASSERT_EQUALS(o, Graphics::kHiResOriginFace);
		TS_ASSERT(!Graphics::parseOrigin("self", o));
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

	/// S7: `blend=on` refused on a CLUT8 screen gets exactly one warning
	/// (design 7.2/10.4) - the pure gate each engine calls to decide whether
	/// to print it.
	void test_blend_on_refused_on_clut8() {
		TS_ASSERT(Graphics::blendRefusedOnClut8(Graphics::kHiResBlendOn, true));
		TS_ASSERT(!Graphics::blendRefusedOnClut8(Graphics::kHiResBlendOn, false));
		TS_ASSERT(!Graphics::blendRefusedOnClut8(Graphics::kHiResBlendAuto, true));
		TS_ASSERT(!Graphics::blendRefusedOnClut8(Graphics::kHiResBlendAuto, false));
		TS_ASSERT(!Graphics::blendRefusedOnClut8(Graphics::kHiResBlendOff, true));
	}

	void test_scale_limits() {
		Graphics::HiResScaleLimits desktop = { 1, 3 };
		Graphics::HiResScaleLimits dos = { 2, 2 };
		Common::String w;
		TS_ASSERT_EQUALS(Graphics::clampScale(3, 1, 3, desktop, 2, "SCUMM", w), 3);
		TS_ASSERT(w.empty());
		TS_ASSERT_EQUALS(Graphics::clampScale(3, 1, 3, dos, 2, "SCUMM", w), 2);
		TS_ASSERT_EQUALS(w, "the DOS backend runs hi-res text at 2x only");
		w.clear();
		TS_ASSERT_EQUALS(Graphics::clampScale(1, 2, 2, desktop, 2, "SCI", w), 2);
		TS_ASSERT_EQUALS(w, "SCI draws hi-res text at 2x only");
		w.clear();
		// engineDefault (ruling M16) need not equal engineMin: a generic
		// out-of-range SCUMM request clamps to 2 (SCUMM's default), not 1
		// (engineMin).
		TS_ASSERT_EQUALS(Graphics::clampScale(99, 1, 3, desktop, 2, "SCUMM", w), 2);
		TS_ASSERT_EQUALS(w, "hires text scale 99 is out of range 1..3");
	}

	void test_platform_scale_limits() {
		// No backend has registered hires_text_platform_scale: {1, 3}.
		Graphics::HiResScaleLimits limits = Graphics::hiResScaleLimits();
		TS_ASSERT_EQUALS(limits.min, 1);
		TS_ASSERT_EQUALS(limits.max, 3);

		// The DOS backend's OSystem_DOS::initBackend() path (design section
		// 7.4/9): ConfMan.registerDefault(), not a real ini/session key.
		ConfMan.registerDefault("hires_text_platform_scale", "2");
		limits = Graphics::hiResScaleLimits();
		TS_ASSERT_EQUALS(limits.min, 2);
		TS_ASSERT_EQUALS(limits.max, 2);

		// A real key (session domain here) still wins over the registered
		// default, as for any other ConfMan key.
		ConfMan.set("hires_text_platform_scale", "3", Common::ConfigManager::kTransientDomain);
		limits = Graphics::hiResScaleLimits();
		TS_ASSERT_EQUALS(limits.min, 3);
		TS_ASSERT_EQUALS(limits.max, 3);

		// Clean up: ConfMan is a process-wide singleton shared with every
		// other test suite in this binary. There is no "unregister a
		// default" call, so blank it out instead - empty is
		// indistinguishable from absent to hiResScaleLimits().
		ConfMan.removeKey("hires_text_platform_scale", Common::ConfigManager::kTransientDomain);
		ConfMan.registerDefault("hires_text_platform_scale", "");
		limits = Graphics::hiResScaleLimits();
		TS_ASSERT_EQUALS(limits.min, 1);
		TS_ASSERT_EQUALS(limits.max, 3);
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

	void test_target_qualifiers() {
		Graphics::HiResRenderTarget t;
		TS_ASSERT(Graphics::parseRenderTargetQualifier("CLUT8", t));
		TS_ASSERT_EQUALS(t, Graphics::kHiResTargetClut8);
		TS_ASSERT(!Graphics::parseRenderTargetQualifier("auto", t));
		TS_ASSERT(!Graphics::parseRenderTargetQualifier("monkey2", t));
		Common::Array<Common::String> e;
		e.push_back("monkey2");
		e.push_back("");
		e.push_back("v5");
		const Common::Array<Common::String> q = Graphics::qualifiersForTarget(e, Graphics::kHiResTargetClut8);
		TS_ASSERT_EQUALS(q.size(), 5u);
		TS_ASSERT_EQUALS(q[0], "monkey2:clut8");
		TS_ASSERT_EQUALS(q[1], "v5:clut8");
		TS_ASSERT_EQUALS(q[2], "monkey2");
		TS_ASSERT_EQUALS(q[3], "v5");
		TS_ASSERT_EQUALS(q[4], "clut8");
		TS_ASSERT_EQUALS(Graphics::qualifiersForTarget(e, Graphics::kHiResTargetAuto).size(), 2u);
		TS_ASSERT_EQUALS(Graphics::qualifiersForTarget(Common::Array<Common::String>(), Graphics::kHiResTargetRgb565).size(), 1u);
	}

	void test_target_of_format_and_prediction() {
		TS_ASSERT_EQUALS(Graphics::targetOfFormat(Graphics::PixelFormat::createFormatCLUT8()), Graphics::kHiResTargetClut8);
		TS_ASSERT_EQUALS(Graphics::targetOfFormat(rgb565()), Graphics::kHiResTargetRgb565);
		TS_ASSERT_EQUALS(Graphics::targetOfFormat(xrgb1555()), Graphics::kHiResTargetRgb565);   // family, not formatMatchesTarget
		TS_ASSERT_EQUALS(Graphics::targetOfFormat(xrgb8888()), Graphics::kHiResTargetRgb888);
		Common::List<Graphics::PixelFormat> staging;
		staging.push_back(xrgb8888());
		staging.push_back(Graphics::PixelFormat::createFormatCLUT8());
		TS_ASSERT_EQUALS(Graphics::predictedTarget(Graphics::kHiResTargetRgb565, staging, true), Graphics::kHiResTargetRgb888);
		TS_ASSERT_EQUALS(Graphics::predictedTarget(Graphics::kHiResTargetClut8, staging, true), Graphics::kHiResTargetClut8);
		Common::List<Graphics::PixelFormat> clutOnly;
		clutOnly.push_back(Graphics::PixelFormat::createFormatCLUT8());
		TS_ASSERT_EQUALS(Graphics::predictedTarget(Graphics::kHiResTargetRgb888, clutOnly, true), Graphics::kHiResTargetClut8);
	}

	void test_targets_offered() {
		Common::List<Graphics::PixelFormat> l;
		l.push_back(xrgb1555());
		TS_ASSERT_EQUALS(Graphics::hiResTargetsOffered(l), 0u);                  // 1-5-5-5 is not rgb565
		l.push_back(rgb565());
		l.push_back(xrgb8888());
		l.push_back(Graphics::PixelFormat::createFormatCLUT8());
		TS_ASSERT_EQUALS(Graphics::hiResTargetsOffered(l),
						 (1u << Graphics::kHiResTargetClut8) | (1u << Graphics::kHiResTargetRgb565) | (1u << Graphics::kHiResTargetRgb888));
	}

	void test_hires_text_configured() {
		ConfMan.addGameDomain("hrconf-test");
		TS_ASSERT(!Graphics::hiResTextConfigured("hrconf-test"));               // no map, no face, no path
		ConfMan.set("hires_text_map", "data:KQ1KO.MAP", "hrconf-test");
		TS_ASSERT(Graphics::hiResTextConfigured("hrconf-test"));
		ConfMan.set("hires_text", "false", "hrconf-test");
		TS_ASSERT(!Graphics::hiResTextConfigured("hrconf-test"));
		TS_ASSERT(Graphics::hiResTextNamed("hrconf-test"));                     // still there to switch back on
		ConfMan.set("hires_text", "true", "hrconf-test");
		ConfMan.set("hires_text_map", "", "hrconf-test");                      // empty: no map
		TS_ASSERT(!Graphics::hiResTextConfigured("hrconf-test"));
		ConfMan.set("hires_text_face", "KO.TTF", "hrconf-test");
		TS_ASSERT(Graphics::hiResTextConfigured("hrconf-test"));
		ConfMan.removeKey("hires_text", "hrconf-test");
		ConfMan.set("hires_text", "false", Common::ConfigManager::kApplicationDomain);   // falls back to [scummvm]
		TS_ASSERT(!Graphics::hiResTextConfigured("hrconf-test"));
		ConfMan.removeKey("hires_text", Common::ConfigManager::kApplicationDomain);
		ConfMan.removeGameDomain("hrconf-test");
	}

	void test_default_map_in_the_game_folder() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::install_null_g_system();
		char name[] = "hires-options-XXXXXX";
		TS_ASSERT(mkdtemp(name) != nullptr);
		char *full = realpath(name, nullptr);
		const Common::String dir = full ? full : name;
		free(full);
		const Common::Path gameDir(dir, '/');

		ConfMan.addGameDomain("hrmap-test");
		ConfMan.setPath("path", gameDir, "hrmap-test");
		TS_ASSERT(Graphics::findDefaultHiResMap(gameDir).empty());
		TS_ASSERT(Graphics::findDefaultHiResMap(Common::Path()).empty());
		TS_ASSERT(!Graphics::hiResTextConfigured("hrmap-test"));

		const Common::Path map = gameDir.appendComponent("hirestxt.map");       // any case
		{
			Common::DumpFile f;
			TS_ASSERT(f.open(map));
			f.writeString("[map]\nversion=2\n");
			f.close();
		}
		TS_ASSERT(!Graphics::findDefaultHiResMap(gameDir).empty());
		TS_ASSERT_EQUALS(Graphics::findDefaultHiResMap(gameDir).baseName(), "hirestxt.map");
		TS_ASSERT(Graphics::hiResTextConfigured("hrmap-test"));
		ConfMan.set("hires_text_map", "", "hrmap-test");                       // an empty map key: no map at all
		TS_ASSERT(!Graphics::hiResTextConfigured("hrmap-test"));

		ConfMan.removeGameDomain("hrmap-test");
		remove(map.toString('/').c_str());
		remove(dir.c_str());
		Common::uninstall_null_g_system();
#endif
	}
};
