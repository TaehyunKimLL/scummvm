#include <cxxtest/TestSuite.h>

#include "common/memstream.h"
#include "agi/translation.h"
#include "common/translation_file.h"

// The AGI overlay agi-<lang>.str (design 8.1): key TAB text, keys
// m<logic>:<n> (n 1-based, as scripts number messages), o<index> and v<view>
// (a view's description: show.obj's inventory close-up text).
class AgiTranslationTestSuite : public CxxTest::TestSuite {
	static bool load(Agi::TextOverlay &o, const char *text,
	                 const Common::TranslationFileHeader &expected = Common::TranslationFileHeader()) {
		Common::MemoryReadStream s((const byte *)text, strlen(text));
		return o.loadFromStream(s, "agi-ko.str", expected);
	}

	static Common::TranslationFileHeader lsl1() {
		Common::TranslationFileHeader h;
		h.format = 1;
		h.game = "lsl1";
		h.engine = "agi";
		h.fingerprint = "1d9311bbee31342e76c7570a29d7616d";
		return h;
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

	// DESIGN.md section 4: the header names the game; another game's table is refused whole.
	void test_header_matches_or_refuses() {
		const char *head = "#! format=1 game=lsl1 fingerprint=1d9311bbee31342e76c7570a29d7616d engine=agi\n";
		Agi::TextOverlay o;
		TS_ASSERT(load(o, (Common::String(head) + "m1:1\tok\n").c_str(), lsl1()));
		TS_ASSERT(o.getHeader().present());
		TS_ASSERT_EQUALS(*o.message(1, 1), "ok");
		Common::TranslationFileHeader other = lsl1();
		other.fingerprint = "0d9311bbee31342e76c7570a29d7616d";
		Agi::TextOverlay p;
		TS_ASSERT(!load(p, (Common::String(head) + "m1:1\tok\n").c_str(), other));
		TS_ASSERT(!p.message(1, 1));
		Common::TranslationFileHeader sci = lsl1();
		sci.engine = "sci";
		Agi::TextOverlay q;
		TS_ASSERT(!load(q, (Common::String(head) + "m1:1\tok\n").c_str(), sci));
		// no header: loaded unchecked (a pack made before headers)
		Agi::TextOverlay r;
		TS_ASSERT(load(r, "m1:1\tok\n", lsl1()));
		TS_ASSERT(!r.getHeader().present());
		// a broken header line is refused, not read as an entry
		Agi::TextOverlay t;
		TS_ASSERT(!load(t, "#! format=2 game=lsl1\nm1:1\tok\n", lsl1()));
	}

	// A text that is not UTF-8 (a CP949 or Latin-1 line pasted in) rejects the table.
	void test_text_must_be_utf8() {
		static const char *const bad[] = { "m1:1\t\xB9\xAE\n", "m1:1\tcaf\xE9\n", "m1:1\t\xEA\xB0\n", "m1:1\t\xED\xA0\x80\n" };
		for (uint i = 0; i < ARRAYSIZE(bad); ++i) {
			Agi::TextOverlay o;
			TS_ASSERT(!load(o, bad[i]));
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
