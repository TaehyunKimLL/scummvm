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

	// Final review Minor 2: the toggle is handled once, where every loop and
	// every wait takes its keys, and it is never a key press - a message box,
	// a menu, the inventory, have.key and waitKey/waitAnyKey all see 0.
	void test_the_toggle_is_handled_once_for_every_loop_and_wait() {
		const Common::String kbd = readSource("engines/agi/keyboard.cpp");
		const int poll = kbd.find("int AgiEngine::doPollKeyboard()");
		const int take = kbd.find("key = getKeypress();", poll);
		const int toggle = kbd.find("if (key == AGI_KEY_HANGUL_TOGGLE) {", take);
		const int call = kbd.find("_text->hangulToggle();", toggle);
		const int zero = kbd.find("key = 0;", call);
		const int ret = kbd.find("return key;", zero);
		const int end = kbd.find("bool AgiEngine::handleMouseClicks(", poll);
		TS_ASSERT_LESS_THAN(0, poll);
		TS_ASSERT_LESS_THAN(poll, take);
		TS_ASSERT_LESS_THAN(take, toggle);
		TS_ASSERT_LESS_THAN(toggle, call);
		TS_ASSERT_LESS_THAN(call, zero);
		TS_ASSERT_LESS_THAN(zero, ret);
		TS_ASSERT_LESS_THAN(ret, end);
		// doPollKeyboard is the one place a key leaves the queue (clearKeyQueue drops them).
		int takes = 0;
		for (int at = kbd.find("getKeypress();"); at >= 0; at = kbd.find("getKeypress();", at + 1))
			takes++;
		TS_ASSERT_EQUALS(takes, 2);
		TS_ASSERT_LESS_THAN(kbd.find("void AgiEngine::clearKeyQueue()"), kbd.rfind("getKeypress();"));
		static const char *const others[] = { "engines/agi/cycle.cpp", "engines/agi/op_test.cpp", "engines/agi/op_cmd.cpp",
		                                       "engines/agi/text.cpp", "engines/agi/menu.cpp", "engines/agi/inv.cpp",
		                                       "engines/agi/systemui.cpp", "engines/agi/agi.cpp" };
		for (uint i = 0; i < ARRAYSIZE(others); i++) {
			const Common::String src = readSource(others[i]);
			TS_ASSERT(!src.contains("getKeypress()"));
			// text.cpp uses it only as the code-point range's upper bound
			TS_ASSERT(!src.contains("== AGI_KEY_HANGUL_TOGGLE"));
		}
		// waitKey and waitAnyKey take their keys from doPollKeyboard, so the toggle never ends them.
		const int waitKey = kbd.find("int AgiEngine::waitKey()");
		const int waitAny = kbd.find("int AgiEngine::waitAnyKey()");
		TS_ASSERT_LESS_THAN(waitKey, kbd.find("key = doPollKeyboard();", waitKey));
		TS_ASSERT_LESS_THAN(kbd.find("key = doPollKeyboard();", waitKey), waitAny);
		TS_ASSERT_LESS_THAN(waitAny, kbd.find("key = doPollKeyboard();", waitAny));
		// D8 still holds inside hangulToggle(): the save name ignores it.
		const Common::String txt = readSource("engines/agi/text.cpp");
		const int def = txt.find("void TextMgr::hangulToggle()");
		const int d8 = txt.find("if (_inputStringAscii) {", def);
		const int flip = txt.find("_hangulMode = !_hangulMode;", def);
		TS_ASSERT_LESS_THAN(0, def);
		TS_ASSERT_LESS_THAN(def, d8);
		TS_ASSERT_LESS_THAN(d8, flip);
		TS_ASSERT_LESS_THAN(flip, txt.find("hangulUpdateBadge();", flip));
	}
};
