#include <cxxtest/TestSuite.h>

#include "common/config-manager.h"
#include "graphics/hires_text/hires_options.h"
#include "gui/hirestextoptions.h"

#include "../system/null_osystem.h"

class HiResTargetOptionsTestSuite : public CxxTest::TestSuite {
	static uint32 bit(Graphics::HiResRenderTarget t) { return 1u << t; }

public:
	void tearDown() {
		if (ConfMan.hasGameDomain("hrtarget-test"))
			ConfMan.removeGameDomain("hrtarget-test");
	}

	void test_entries_follow_the_backend() {
		const uint32 all = bit(Graphics::kHiResTargetClut8) | bit(Graphics::kHiResTargetRgb565) | bit(Graphics::kHiResTargetRgb888);
		Common::Array<GUI::HiResTargetEntry> e = GUI::renderTargetEntries(all, false, Graphics::kHiResTargetAuto);
		TS_ASSERT_EQUALS(e.size(), 4u);
		TS_ASSERT_EQUALS(e[0].target, Graphics::kHiResTargetAuto);
		TS_ASSERT_EQUALS(e[1].target, Graphics::kHiResTargetClut8);
		TS_ASSERT_EQUALS(e[3].target, Graphics::kHiResTargetRgb888);
		const uint32 staging = bit(Graphics::kHiResTargetClut8) | bit(Graphics::kHiResTargetRgb888);
		e = GUI::renderTargetEntries(staging, false, Graphics::kHiResTargetAuto);
		TS_ASSERT_EQUALS(e.size(), 3u);
		TS_ASSERT(GUI::renderTargetEntries(bit(Graphics::kHiResTargetClut8), false, Graphics::kHiResTargetAuto).empty());
	}

	void test_a_stored_target_the_backend_lacks_is_kept_visible() {
		const uint32 staging = bit(Graphics::kHiResTargetClut8) | bit(Graphics::kHiResTargetRgb888);
		Common::Array<GUI::HiResTargetEntry> e = GUI::renderTargetEntries(staging, true, Graphics::kHiResTargetRgb565);
		TS_ASSERT_EQUALS(e.size(), 4u);
		TS_ASSERT_EQUALS(e.back().target, Graphics::kHiResTargetRgb565);
		TS_ASSERT(!e.back().available);
		// A CLUT8-only backend with a stored true colour: Auto and the stored entry
		e = GUI::renderTargetEntries(bit(Graphics::kHiResTargetClut8), true, Graphics::kHiResTargetRgb888);
		TS_ASSERT_EQUALS(e.size(), 3u);
		TS_ASSERT_EQUALS(e[1].target, Graphics::kHiResTargetClut8);
		TS_ASSERT(e[1].available);
		TS_ASSERT(!e[2].available);
		// A stored Auto, or an offered target, adds nothing
		TS_ASSERT_EQUALS(GUI::renderTargetEntries(staging, true, Graphics::kHiResTargetAuto).size(), 3u);
		TS_ASSERT_EQUALS(GUI::renderTargetEntries(staging, true, Graphics::kHiResTargetClut8).size(), 3u);
	}

	void test_labels() {
#if NULL_OSYSTEM_IS_AVAILABLE
		// The translation manager looks for its file through g_system.
		Common::install_null_g_system();
		GUI::HiResTargetEntry e;
		e.target = Graphics::kHiResTargetRgb565;
		e.available = true;
		TS_ASSERT_EQUALS(GUI::renderTargetLabel(e).encode(), "16-bit colour");
		e.available = false;
		TS_ASSERT_EQUALS(GUI::renderTargetLabel(e).encode(), "16-bit colour (not available here)");
		e.target = Graphics::kHiResTargetAuto;
		e.available = true;
		TS_ASSERT_EQUALS(GUI::renderTargetLabel(e).encode(), "Auto");
		Common::uninstall_null_g_system();
#endif
	}

	void test_write_and_read_one_domain() {
		ConfMan.addGameDomain("hrtarget-test");
		Graphics::HiResRenderTarget t;
		TS_ASSERT(!GUI::readRenderTarget("hrtarget-test", t));
		TS_ASSERT(GUI::writeRenderTarget("hrtarget-test", Graphics::kHiResTargetClut8));
		TS_ASSERT_EQUALS(ConfMan.get("render_target", "hrtarget-test"), "clut8");
		TS_ASSERT(!GUI::writeRenderTarget("hrtarget-test", Graphics::kHiResTargetClut8));   // unchanged
		TS_ASSERT(GUI::writeRenderTarget("hrtarget-test", Graphics::kHiResTargetAuto));
		TS_ASSERT_EQUALS(ConfMan.get("render_target", "hrtarget-test"), "auto");
		TS_ASSERT(GUI::readRenderTarget("hrtarget-test", t));
		TS_ASSERT_EQUALS(t, Graphics::kHiResTargetAuto);
		ConfMan.set("render_target", "truecolor", "hrtarget-test");
		TS_ASSERT(!GUI::readRenderTarget("hrtarget-test", t));
	}

	void test_read_ignores_the_global_domain() {
		ConfMan.addGameDomain("hrtarget-test");
		ConfMan.set("render_target", "clut8", Common::ConfigManager::kApplicationDomain);
		Graphics::HiResRenderTarget t;
		TS_ASSERT(!GUI::readRenderTarget("hrtarget-test", t));
		TS_ASSERT(GUI::readRenderTarget(Common::ConfigManager::kApplicationDomain, t));
		TS_ASSERT_EQUALS(t, Graphics::kHiResTargetClut8);
		ConfMan.removeKey("render_target", Common::ConfigManager::kApplicationDomain);
	}
};
