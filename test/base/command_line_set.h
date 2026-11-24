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

	void test_control_keys_are_refused() {
		static const char *const bad[] = { "game", "GAME", "config", "exit", "recursive", "set", "initial-cfg", "list-games", "path", "md5-path" };
		for (uint i = 0; i < ARRAYSIZE(bad); ++i)
			TS_ASSERT(Base::isControlSetKey(bad[i]));
		static const char *const ok[] = { "render_target", "render-target", "hires_text_map", "extrapath", "gfx-mode" };
		for (uint i = 0; i < ARRAYSIZE(ok); ++i)
			TS_ASSERT(!Base::isControlSetKey(ok[i]));
		TS_ASSERT_EQUALS(Base::normalizeSetKey("render-target"), "render_target");
		TS_ASSERT_EQUALS(Base::normalizeSetKey("a-b-c"), "a_b_c");
	}

	// Fuzz: any text either splits at the first '=' into a key of letters, digits, '_' and '-' and a value, or is refused;
	// a key and its dashed twin are one control key or none.
	void test_fuzz_split_and_normalise() {
		uint32 seed = 99;
		const char alphabet[] = "ab_-=.1 \xfe\xed";
		for (int n = 0; n < 20000; ++n) {
			char buf[16];
			int len = 0;
			seed = seed * 1103515245 + 12345;
			const int want = (seed >> 16) % 14;
			for (int k = 0; k < want; ++k) {
				seed = seed * 1103515245 + 12345;
				buf[len++] = alphabet[(seed >> 16) % (sizeof(alphabet) - 1)];
			}
			buf[len] = 0;
			Common::String key, value;
			const bool ok = Base::parseSetOption(buf, key, value);
			const char *eq = strchr(buf, '=');
			bool model = eq && eq != buf;
			for (const char *c = buf; model && c < eq; ++c)
				model = Common::isAlnum(*c) || *c == '_' || *c == '-';
			TS_ASSERT_EQUALS(ok, model);
			if (ok) {
				TS_ASSERT_EQUALS(key, Common::String(buf, eq));
				TS_ASSERT_EQUALS(value, Common::String(eq + 1));
				Common::String dashed(key);
				for (uint i = 0; i < dashed.size(); ++i)
					if (dashed[i] == '_')
						dashed.setChar('-', i);
				TS_ASSERT_EQUALS(Base::isControlSetKey(key), Base::isControlSetKey(dashed));
			}
		}
	}
};
