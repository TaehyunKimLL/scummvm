#include <cxxtest/TestSuite.h>

#include "base/config-load.h"

class ConfigLoadTestSuite : public CxxTest::TestSuite {
public:
	void test_loaded_config_continues() {
		TS_ASSERT_EQUALS(Base::configLoadAction(true, true), Base::kConfigLoadContinue);
		TS_ASSERT_EQUALS(Base::configLoadAction(true, false), Base::kConfigLoadContinue);
	}

	void test_bad_config_with_gui_asks() {
		TS_ASSERT_EQUALS(Base::configLoadAction(false, true), Base::kConfigLoadAskOverwrite);
	}

	// No GUI: never overwrite a file that did not parse.
	void test_bad_config_without_gui_refuses() {
		TS_ASSERT_EQUALS(Base::configLoadAction(false, false), Base::kConfigLoadRefuse);
	}
};
