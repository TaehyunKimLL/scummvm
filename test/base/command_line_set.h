#include <cxxtest/TestSuite.h>

#include "base/set-option.h"
#include "common/str.h"

class CommandLineSetTestSuite : public CxxTest::TestSuite {
public:
	void test_key_and_value() {
		Common::String key, value;
		TS_ASSERT(Base::parseSetOption("sci_hangul_input=false", key, value));
		TS_ASSERT_EQUALS(key, "sci_hangul_input");
		TS_ASSERT_EQUALS(value, "false");
	}

	// Only the first '=' splits: a value may hold more (a map path, an option list).
	void test_value_keeps_later_equals() {
		Common::String key, value;
		TS_ASSERT(Base::parseSetOption("hires_text_map=a=b.map", key, value));
		TS_ASSERT_EQUALS(key, "hires_text_map");
		TS_ASSERT_EQUALS(value, "a=b.map");
	}

	void test_empty_value_is_allowed() {
		Common::String key, value;
		TS_ASSERT(Base::parseSetOption("hires_text_map=", key, value));
		TS_ASSERT_EQUALS(key, "hires_text_map");
		TS_ASSERT(value.empty());
	}

	// '-' is accepted: processSettings() turns it into '_' as for every option.
	void test_dash_in_key() {
		Common::String key, value;
		TS_ASSERT(Base::parseSetOption("render-target=clut8", key, value));
		TS_ASSERT_EQUALS(key, "render-target");
	}

	void test_refused() {
		Common::String key("k"), value("v");
		TS_ASSERT(!Base::parseSetOption(nullptr, key, value));
		TS_ASSERT(!Base::parseSetOption("", key, value));
		TS_ASSERT(!Base::parseSetOption("novalue", key, value));
		TS_ASSERT(!Base::parseSetOption("=x", key, value));
		TS_ASSERT(!Base::parseSetOption("bad key=x", key, value));
		TS_ASSERT(!Base::parseSetOption("a.b=x", key, value));
		TS_ASSERT_EQUALS(key, "k");   // untouched on refusal
		TS_ASSERT_EQUALS(value, "v");
	}
};
