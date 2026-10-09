#include <cxxtest/TestSuite.h>

#include "common/fs.h"
#include "common/scummsys.h"
#include "common/str.h"
#include "common/stream.h"
#include "../../system/null_osystem.h"

/**
 * The Korean input checkboxes in the SCI game options.
 *
 * The dialog is Qt-free but still GUI code, so these pin the agreement the
 * dialog must keep with the rest of the engine instead of instantiating it:
 * the three config keys are the ones the EventManager and the keymap read,
 * the defaults registered for them leave an unconfigured target behaving as
 * before, and the dependency between the boxes is wired.
 */

class SciKoreanOptionsTestSuite : public CxxTest::TestSuite {
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

	void test_the_dialog_offers_exactly_the_keys_the_engine_reads() {
		Common::String dlg = readSource("engines/sci/dialogs.cpp");
		Common::String ev = readSource("engines/sci/event.cpp");

		const char *keys[] = { "sci_hangul_input", "sci_hangul_builtin", "sci_hangul_start" };
		for (uint i = 0; i < 3; ++i) {
			Common::String quoted = Common::String("\"") + keys[i] + "\"";
			TS_ASSERT_LESS_THAN(0, dlg.find(quoted.c_str()));
			TS_ASSERT(ev.contains(quoted.c_str()));
		}
		// No fourth sci_hangul_ key in the dialog that the engine ignores.
		const char *prefix = "\"sci_hangul_";
		for (uint pos = 0; pos + 12 <= dlg.size(); ++pos) {
			if (strncmp(dlg.c_str() + pos, prefix, 12))
				continue;
			uint end = pos + 1;
			while (end < dlg.size() && dlg[end] != '"')
				++end;
			Common::String key(dlg.c_str() + pos + 1, end - pos - 1);
			TS_ASSERT(key == keys[0] || key == keys[1] || key == keys[2]);
		}
	}

	void test_defaults_are_registered_and_match_the_engine() {
		Common::String me = readSource("engines/sci/metaengine.cpp");
		TS_ASSERT(me.contains("for (const HangulOptionsMap *entry = hangulOptionsList; entry->configOption; ++entry)\n\t\tConfMan.registerDefault"));

		// Built-in composer: on everywhere except Android, as in event.cpp.
		Common::String dlg = readSource("engines/sci/dialogs.cpp");
		const int android = dlg.find("#ifdef ANDROID_BACKEND\n\t\tfalse,\n#else\n\t\ttrue,\n#endif");
		const int key = dlg.find("\"sci_hangul_builtin\",");
		TS_ASSERT_LESS_THAN(0, android);
		TS_ASSERT_LESS_THAN(key, android);

		// Input and start are off by default.
		TS_ASSERT(dlg.contains("\"sci_hangul_input\",\n\t\tfalse,"));
		TS_ASSERT(dlg.contains("\"sci_hangul_start\",\n\t\tfalse,"));
	}

	void test_the_boxes_depend_on_each_other() {
		Common::String dlg = readSource("engines/sci/dialogs.cpp");
		TS_ASSERT(dlg.contains("_checkboxes[\"sci_hangul_builtin\"]->setEnabled(input);"));
		TS_ASSERT(dlg.contains("_checkboxes[\"sci_hangul_start\"]->setEnabled(input && builtin);"));
		// Refreshed after load() and on a click of either parent box.
		TS_ASSERT_EQUALS(count(dlg, "updateHangulDependents();"), 2);
		TS_ASSERT(dlg.contains("case kHangulInputCmd:"));
		TS_ASSERT(dlg.contains("case kHangulBuiltinCmd:"));
	}

	void test_the_boxes_are_shown_for_korean_or_configured_targets_only() {
		Common::String dlg = readSource("engines/sci/dialogs.cpp");
		TS_ASSERT(dlg.contains("ConfMan.hasKey(\"sci_hangul_input\", domain)"));
		TS_ASSERT(dlg.contains("Common::KO_KOR"));
		// Created, loaded and saved only behind the same flag.
		TS_ASSERT_EQUALS(count(dlg, "if (_hangulShown)"), 4);
	}

private:
	int count(const Common::String &hay, const char *needle) {
		const uint n = strlen(needle);
		int c = 0;
		for (uint i = 0; i + n <= hay.size(); ++i)
			if (!strncmp(hay.c_str() + i, needle, n))
				c++;
		return c;
	}

	Common::String readSource(const char *rel) {
		Common::String path = Common::String(SCI_TEST_SRCDIR) + "/" + rel;
		Common::FSNode node(Common::Path(path, '/'));
		Common::SeekableReadStream *in = node.createReadStream();
		if (!in) {
			TS_FAIL(("cannot read " + path).c_str());
			return Common::String();
		}
		uint32 len = (uint32)in->size();
		char *buf = new char[len + 1];
		uint32 got = in->read(buf, len);
		buf[got] = 0;
		Common::String out(buf, got);
		delete[] buf;
		delete in;
		TS_ASSERT_LESS_THAN(0u, out.size());
		return out;
	}
};
