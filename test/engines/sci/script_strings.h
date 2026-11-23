#include <cxxtest/TestSuite.h>

#include <common/memstream.h>

#include "sci/engine/script_strings.h"

class SciScriptStringsTestSuite : public CxxTest::TestSuite {
	static bool load(Sci::ScriptStrings &t, const char *text, const Common::TranslationFileHeader &expected = Common::TranslationFileHeader()) {
		Common::MemoryReadStream s((const byte *)text, strlen(text));
		return t.loadFromStream(s, "sci-ko.str", expected);
	}

	static Common::TranslationFileHeader kq1() {
		Common::TranslationFileHeader h;
		h.format = 1;
		h.game = "kq1sci";
		h.engine = "sci";
		h.fingerprint = "0123456789abcdef0123456789abcdef";
		return h;
	}

public:
	void test_entriesAndRooms() {
		Sci::ScriptStrings t;
		TS_ASSERT(load(t, "\xEF\xBB\xBF# comment\n0\t5\t\xEB\x81\x84\xEA\xB8\xB0\n0\t5\t14\troom\n12\t3\ta\\nb\tc\n"));
		Common::String out;
		TS_ASSERT(t.lookup(Sci::ScriptStrings::Key(0, 5), out));
		TS_ASSERT_EQUALS(out, "\xEB\x81\x84\xEA\xB8\xB0");
		TS_ASSERT(t.lookup(Sci::ScriptStrings::Key(0, 5, 14), out));
		TS_ASSERT_EQUALS(out, "room");
		TS_ASSERT(t.lookup(Sci::ScriptStrings::Key(0, 5, 15), out));
		TS_ASSERT_EQUALS(out, "\xEB\x81\x84\xEA\xB8\xB0");
		TS_ASSERT(t.lookup(Sci::ScriptStrings::Key(12, 3), out));
		TS_ASSERT_EQUALS(out, "a\nb\tc");
		TS_ASSERT(!t.lookup(Sci::ScriptStrings::Key(12, 4), out));
		TS_ASSERT(!t.getHeader().present());
	}

	void test_malformedLineRefusesTable() {
		Sci::ScriptStrings t;
		TS_ASSERT(!load(t, "0\t5\tok\n0\tx\tbad\n"));
		TS_ASSERT(!t.isLoaded());
	}

	void test_headerMatches() {
		Sci::ScriptStrings t;
		TS_ASSERT(load(t, "#! format=1 game=kq1sci fingerprint=0123456789ABCDEF0123456789abcdef engine=sci\n0\t5\tx\n", kq1()));
		TS_ASSERT(t.getHeader().present());
		TS_ASSERT_EQUALS(t.entryCount(), 1u);
	}

	void test_headerMismatchRefuses() {
		Sci::ScriptStrings t;
		TS_ASSERT(!load(t, "#! format=1 game=kq1sci fingerprint=ffffffffffffffffffffffffffffffff engine=sci\n0\t5\tx\n", kq1()));
		TS_ASSERT(!t.isLoaded());
		TS_ASSERT(!load(t, "#! format=1 game=kq4sci fingerprint=0123456789abcdef0123456789abcdef engine=sci\n0\t5\tx\n", kq1()));
	}

	void test_badHeaderRefuses() {
		Sci::ScriptStrings t;
		TS_ASSERT(!load(t, "#! format=1 game=kq1sci fingerprint=nothex engine=sci\n0\t5\tx\n", kq1()));
	}

	void test_headerlessLoadsUnchecked() {
		Sci::ScriptStrings t;
		TS_ASSERT(load(t, "# script strings\n0\t5\tx\n", kq1()));
		TS_ASSERT(t.isLoaded());
	}

	void test_bufferTags() {
		Sci::ScriptStrings t;
		TS_ASSERT(load(t, "997\t19\tA\n995\t6\t.\n"));
		const uint32 buf = Sci::ScriptStrings::bufferId(3, 0x10);
		t.tagBuffer(buf, Sci::ScriptStrings::Key(997, 19), "You carry");
		t.appendToBuffer(buf, "You carry", Sci::ScriptStrings::Key(), Sci::ScriptStrings::Key::kNoBuffer, " clip");
		t.appendToBuffer(buf, "You carry clip", Sci::ScriptStrings::Key(995, 6), Sci::ScriptStrings::Key::kNoBuffer, "!");
		const Sci::ScriptStrings::Key k = t.keyOf(buf, "You carry clip!");
		TS_ASSERT(k.isBuffer());
		Common::String out;
		TS_ASSERT(t.lookup(k, out));
		TS_ASSERT_EQUALS(out, "A clip.");
		TS_ASSERT(!t.keyOf(buf, "other").isSet());
	}

	void test_sci11StringsStart() {
		// header (4), one instance: magic 0x1234, 3 properties -> strings at 4 + 2*0 + 6.
		const byte heap[] = { 0, 0, 0, 0, 0x34, 0x12, 3, 0, 0, 0, 0, 0, 'h', 'i', 0 };
		TS_ASSERT_EQUALS(Sci::ScriptStrings::sci11StringsStart(heap, sizeof(heap)), 10u);
		// the same heap with big-endian words (Mac)
		const byte be[] = { 0, 0, 0, 0, 0x12, 0x34, 0, 3, 0, 0, 0, 0, 'h', 'i', 0 };
		TS_ASSERT_EQUALS(Sci::ScriptStrings::sci11StringsStart(be, sizeof(be), true), 10u);
		// read as little-endian it finds no magic and starts at the first instance
		TS_ASSERT_EQUALS(Sci::ScriptStrings::sci11StringsStart(be, sizeof(be), false), 4u);
	}
};
