#include <cxxtest/TestSuite.h>

#include "common/memstream.h"
#include "agi/translation.h"

// The AGI overlay agi-<lang>.str (design 8.1): key TAB text, keys
// m<logic>:<n> (n 1-based, as scripts number messages), o<index> and v<view>
// (a view's description: show.obj's inventory close-up text).
class AgiOverlayTestSuite : public CxxTest::TestSuite {
	static bool load(Agi::TextOverlay &o, const char *text) {
		Common::MemoryReadStream s((const byte *)text, strlen(text));
		return o.loadFromStream(s);
	}

public:
	void test_entries_and_lookup() {
		Agi::TextOverlay o;
		TS_ASSERT(load(o, "\xEF\xBB\xBF# SQ0\nm5:1\t\xEB\xAC\xB8\nm0:12\tGame\\nmenu\no3\t\xEC\x97\xB4\xEC\x87\xA0\n\n"));
		TS_ASSERT(o.isPresent());
		TS_ASSERT(o.isLoaded());
		TS_ASSERT_EQUALS(o.entryCount(), 3u);
		TS_ASSERT(o.message(5, 1));
		TS_ASSERT_EQUALS(*o.message(5, 1), "\xEB\xAC\xB8");
		TS_ASSERT_EQUALS(*o.message(0, 12), "Game\nmenu");
		TS_ASSERT_EQUALS(*o.object(3), "\xEC\x97\xB4\xEC\x87\xA0");
		TS_ASSERT(!o.message(5, 2));
		TS_ASSERT(!o.object(5));
		TS_ASSERT(!o.message(3, 0));
	}

	void test_view_descriptions_are_their_own_keys() {
		Agi::TextOverlay o;
		TS_ASSERT(load(o, "o3\tobject\nv3\tview\nm3:3\tmessage\n"));
		TS_ASSERT_EQUALS(o.entryCount(), 3u);
		TS_ASSERT_EQUALS(*o.object(3), "object");
		TS_ASSERT_EQUALS(*o.view(3), "view");
		TS_ASSERT_EQUALS(*o.message(3, 3), "message");
		TS_ASSERT(!o.view(4));
	}

	void test_comments_only_is_present_but_not_loaded() {
		Agi::TextOverlay o;
		TS_ASSERT(load(o, "# nothing yet\n"));
		TS_ASSERT(o.isPresent());
		TS_ASSERT(!o.isLoaded());
	}

	void test_a_bad_line_rejects_the_table() {
		static const char *const bad[] = {
			"m5\tno colon\n", "m5:x\tbad number\n", "q1\tunknown prefix\n", "m5:1 no tab\n",
			"o\tno index\n", "m5:0\tmessage zero\n", "m70000:1\ttoo big\n",
		};
		for (uint i = 0; i < ARRAYSIZE(bad); ++i) {
			Agi::TextOverlay o;
			const Common::String text = Common::String("m1:1\tok\n") + bad[i];
			TS_ASSERT(!load(o, text.c_str()));
			TS_ASSERT(!o.isPresent());
			TS_ASSERT(!o.message(1, 1));
		}
	}

	void test_duplicate_key_later_wins() {
		Agi::TextOverlay o;
		TS_ASSERT(load(o, "m1:1\tfirst\nm1:1\tsecond\n"));
		TS_ASSERT_EQUALS(*o.message(1, 1), "second");
		TS_ASSERT_EQUALS(o.entryCount(), 1u);
	}

	void test_escapes() {
		TS_ASSERT_EQUALS(Agi::TextOverlay::unescape("a\\nb\\tc\\\\d\\q"), "a\nb\tc\\d\\q");
	}

	void test_parse_line_keys() {
		Agi::TextOverlay::Key k;
		Common::String t;
		const char *err = nullptr;
		TS_ASSERT_EQUALS(Agi::TextOverlay::parseLine("m12:3\tx\ty", k, t, err), Agi::TextOverlay::kLineEntry);
		TS_ASSERT_EQUALS(k.kind, Agi::TextOverlay::kKeyMessage);
		TS_ASSERT_EQUALS(k.logic, 12);
		TS_ASSERT_EQUALS(k.number, 3);
		TS_ASSERT_EQUALS(t, "x\ty");
		TS_ASSERT_EQUALS(Agi::TextOverlay::keyName(k), "m12:3");
		TS_ASSERT_EQUALS(Agi::TextOverlay::parseLine("o7\t", k, t, err), Agi::TextOverlay::kLineEntry);
		TS_ASSERT_EQUALS(k.kind, Agi::TextOverlay::kKeyObject);
		TS_ASSERT_EQUALS(Agi::TextOverlay::keyName(k), "o7");
		TS_ASSERT_EQUALS(Agi::TextOverlay::parseLine("v121\tring", k, t, err), Agi::TextOverlay::kLineEntry);
		TS_ASSERT_EQUALS(k.kind, Agi::TextOverlay::kKeyView);
		TS_ASSERT_EQUALS(k.number, 121);
		TS_ASSERT_EQUALS(Agi::TextOverlay::keyName(k), "v121");
		TS_ASSERT_EQUALS(Agi::TextOverlay::parseLine("v300\tx", k, t, err), Agi::TextOverlay::kLineError);
		TS_ASSERT_EQUALS(Agi::TextOverlay::parseLine("", k, t, err), Agi::TextOverlay::kLineSkip);
		TS_ASSERT_EQUALS(Agi::TextOverlay::parseLine("# c", k, t, err), Agi::TextOverlay::kLineSkip);
	}

	// Review focus 1: an untranslated message with code-page bytes becomes
	// UTF-8 before it reaches the UTF-8 decoder.
	void test_legacy_bytes_become_utf8() {
		TS_ASSERT_EQUALS(Agi::TextOverlay::legacyToUtf8("caf\x82", Common::kDos850), "caf\xC3\xA9");
		TS_ASSERT_EQUALS(Agi::TextOverlay::legacyToUtf8("plain", Common::kDos850), "plain");
	}
};
