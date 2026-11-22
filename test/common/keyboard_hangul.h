#include <cxxtest/TestSuite.h>

#include "backends/keymapper/hardware-input.h"
#include "common/keyboard.h"

#include "test/system/null_osystem.h"

// The Han/Yeong and Hanja keys: key codes of their own, and names a keymap can bind (DESIGN.md section 6).
class KeyboardHangulTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override {
		Common::install_null_g_system();
	}

	void tearDown() override {
		Common::uninstall_null_g_system();
	}

	// Codes of their own after the upstream ones (a keymap stores the names below, not the numbers).
	void test_keyCodes() {
		TS_ASSERT_EQUALS((int)Common::KEYCODE_HANGUL, 357);
		TS_ASSERT_EQUALS((int)Common::KEYCODE_HANJA, (int)Common::KEYCODE_HANGUL + 1);
		TS_ASSERT_LESS_THAN((int)Common::KEYCODE_AUDIOFASTFORWARD, (int)Common::KEYCODE_HANGUL);
		TS_ASSERT_LESS_THAN((int)Common::KEYCODE_HANJA, (int)Common::KEYCODE_LAST);
	}

	void test_keymapperNames() {
		Common::KeyboardHardwareInputSet keys(Common::defaultKeys, Common::defaultModifiers);
		const Common::HardwareInput hangul = keys.findHardwareInput("HANGUL");
		TS_ASSERT_EQUALS(hangul.type, Common::kHardwareInputTypeKeyboard);
		TS_ASSERT_EQUALS(hangul.key.keycode, Common::KEYCODE_HANGUL);
		TS_ASSERT_EQUALS(hangul.description, Common::U32String("Han/Yeong"));
		TS_ASSERT_EQUALS(keys.findHardwareInput("HANJA").key.keycode, Common::KEYCODE_HANJA);
		// The key with a modifier, as the engines' default bindings name it (S+HANGUL).
		TS_ASSERT_EQUALS(keys.findHardwareInput("S+HANGUL").key.flags, (byte)Common::KBD_SHIFT);
		// And back: the event a backend sends for the key finds it.
		Common::Event e;
		e.type = Common::EVENT_KEYDOWN;
		e.kbd = Common::KeyState(Common::KEYCODE_HANGUL);
		TS_ASSERT_EQUALS(keys.findHardwareInput(e).id, "HANGUL");
	}
};
