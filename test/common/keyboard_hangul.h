#include <cxxtest/TestSuite.h>

#include "common/keyboard.h"

#include "test/system/null_osystem.h"

// The Han/Yeong and Hanja keys: key codes of their own (DESIGN.md section 6).
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
};
