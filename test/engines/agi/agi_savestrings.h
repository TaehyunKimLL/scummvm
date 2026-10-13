#include <cxxtest/TestSuite.h>

#include "common/memstream.h"
#include "agi/savestrings.h"
#include "agi/textcells.h"

// Design 7 / D7: a UTF-8 session saves its script strings as length +
// bytes (version 12); 40 cells of Hangul do not fit the old 40-byte slots.
class AgiSaveStringsTestSuite : public CxxTest::TestSuite {
public:
	void test_round_trip_of_a_39_cell_korean_string() {
		char table[3][161];
		memset(table, 0, sizeof(table));
		Common::String ko;
		for (int i = 0; i < 39; i++)
			ko += "\xED\x95\x9C"; // 한
		Common::strlcpy(table[0], ko.c_str(), 161);
		Common::strlcpy(table[1], "plain", 161);
		Common::String answers[3];
		answers[0] = "password";
		Common::MemoryWriteStreamDynamic out(DisposeAfterUse::YES);
		Agi::writeUtf8Strings(out, &table[0][0], 161, 3, answers);

		char back[3][161];
		memset(back, 'x', sizeof(back));
		Common::String backAnswers[3];
		Common::MemoryReadStream in(out.getData(), out.size());
		TS_ASSERT(Agi::readUtf8Strings(in, &back[0][0], 161, 3, backAnswers));
		TS_ASSERT_EQUALS(Common::String(back[0]), ko);
		TS_ASSERT_EQUALS(Agi::TextCells::cellLen(back[0], true), 39);
		TS_ASSERT_EQUALS(Common::String(back[1]), "plain");
		TS_ASSERT_EQUALS(Common::String(back[2]), "");
		TS_ASSERT_EQUALS(backAnswers[0], "password");
		TS_ASSERT_EQUALS(backAnswers[1], "");
		TS_ASSERT_EQUALS(in.pos(), (int64)out.size());
	}

	// A restore must not leave a stale English alternative behind.
	void test_load_replaces_stale_answers() {
		char table[2][161];
		memset(table, 0, sizeof(table));
		Common::String none[2];
		Common::MemoryWriteStreamDynamic out(DisposeAfterUse::YES);
		Agi::writeUtf8Strings(out, &table[0][0], 161, 2, none);

		char back[2][161];
		memset(back, 0, sizeof(back));
		Common::String stale[2];
		stale[0] = "old answer";
		stale[1] = "another";
		Common::MemoryReadStream in(out.getData(), out.size());
		TS_ASSERT(Agi::readUtf8Strings(in, &back[0][0], 161, 2, stale));
		TS_ASSERT_EQUALS(stale[0], "");
		TS_ASSERT_EQUALS(stale[1], "");
	}

	// A version-11 slot is code page 850; in a UTF-8 session it becomes UTF-8.
	void test_legacy_slots_become_utf8() {
		char table[3][161];
		memset(table, 0, sizeof(table));
		Common::strlcpy(table[0], "caf\x82", 161);      // CP850 0x82 = e-acute
		Common::strlcpy(table[1], "plain text", 161);
		Agi::convertLegacyStrings(&table[0][0], 161, 3, 39);
		TS_ASSERT_EQUALS(Common::String(table[0]), "caf\xC3\xA9");
		TS_ASSERT_EQUALS(Common::String(table[1]), "plain text");
		TS_ASSERT_EQUALS(Common::String(table[2]), "");
	}

	void test_a_slot_longer_than_the_table_is_refused() {
		const byte bad[] = { 200 };
		Common::MemoryReadStream in(bad, sizeof(bad));
		char back[1][161];
		Common::String a[1];
		TS_ASSERT(!Agi::readUtf8Strings(in, &back[0][0], 161, 1, a));
	}

	void test_truncated_stream_is_refused() {
		const byte bad[] = { 5, 'a', 'b' };
		Common::MemoryReadStream in(bad, sizeof(bad));
		char back[1][161];
		Common::String a[1];
		TS_ASSERT(!Agi::readUtf8Strings(in, &back[0][0], 161, 1, a));
	}
};
