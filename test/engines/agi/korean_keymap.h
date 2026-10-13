#include <cxxtest/TestSuite.h>

#include "support/read_source.h"

// Han/Yeong as a keymap action, as S9 (a9fe2374373) wired it for the K chain
// and as SCI does (engines/sci/metaengine.cpp): a press on this machine would
// pass against broken wiring (right Alt is Alt_R on X11 only), so the wiring
// is what is pinned. The end-to-end press is harness/agi/lsl1_play.py's.
class AgiKoreanKeymapTestSuite : public CxxTest::TestSuite {
	static Common::String readSource(const char *rel) { return AgiTest::readSource(rel); }

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

	// PRECONDITION CHECK, not a test of this task: the backend and keymapper
	// side (S9) already carries the Han/Yeong key, so this passes before and
	// after the AGI wiring. It is kept so that a regression there is named
	// as such instead of showing up as a dead AGI action.
	void test_precondition_the_hangul_keycode_reaches_the_keymapper() {
		TS_ASSERT(readSource("common/keyboard.h").contains("KEYCODE_HANGUL"));
		TS_ASSERT(readSource("backends/events/sdl/sdl3-events.cpp").contains("SDL_SCANCODE_LANG1"));
		TS_ASSERT(readSource("backends/keymapper/hardware-input.cpp").contains("KEYCODE_HANGUL"));
	}

	void test_agi_registers_the_action_behind_its_config_keys() {
		const Common::String me = readSource("engines/agi/metaengine.cpp");
		TS_ASSERT(me.contains("initKeymaps"));
		TS_ASSERT(me.contains("kAgiActionHangulToggle"));
		static const char *const keys[] = { "HANGUL", "RALT", "C+SPACE", "S+SPACE", "SCROLLOCK", "F12" };
		for (uint i = 0; i < ARRAYSIZE(keys); i++)
			TS_ASSERT(me.contains(Common::String::format("addDefaultInputMapping(\"%s\")", keys[i])));
		TS_ASSERT(me.contains("Graphics::confManGameBool(\"agi_hangul_input\", target, false)"));
		TS_ASSERT(me.contains("Graphics::confManGameBool(\"agi_hangul_builtin\", target, true)"));
	}

	void test_the_action_becomes_the_pseudo_key_behind_the_gate() {
		const Common::String kbd = readSource("engines/agi/keyboard.cpp");
		const int action = kbd.find("event.customType == kAgiActionHangulToggle");
		const int guard = kbd.find("_text->hangulInputAvailable()");
		TS_ASSERT_LESS_THAN(0, action);
		TS_ASSERT_LESS_THAN(action, guard);
		TS_ASSERT(kbd.contains("keyEnqueue(AGI_KEY_HANGUL_TOGGLE);"));
		TS_ASSERT(!kbd.contains("case Common::KEYCODE_RALT:"));
		TS_ASSERT(!kbd.contains("KEYCODE_MODE"));
	}

	void test_the_toggle_is_consumed_before_the_controllers() {
		const Common::String cyc = readSource("engines/agi/cycle.cpp");
		const int toggle = cyc.find("rawKey == AGI_KEY_HANGUL_TOGGLE");
		const int controller = cyc.find("handleController(key)");
		TS_ASSERT_LESS_THAN(0, toggle);
		TS_ASSERT_LESS_THAN(toggle, controller);
		TS_ASSERT(readSource("engines/agi/text.cpp").contains("void TextMgr::hangulToggle()"));
	}
};
