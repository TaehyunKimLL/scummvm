#include <cxxtest/TestSuite.h>

#include "common/korean_stem.h"
#include "common/korean_vocab.h"
#include "common/memstream.h"
#include "../korean_vocab_fixture.h"

// The word table common/ now owns for SCI (SCIKOR.DAT) and AGI (AGIKOR.DAT):
// one layout, the magic says whose it is.
class KoreanVocabTestSuite : public CxxTest::TestSuite {
	static Common::U32String u(const char *utf8) {
		return Common::String(utf8).decode(Common::kUtf8);
	}

	static bool loadRows(Common::KoreanVocabulary &v, const char *magicInFile, const char *magicWanted) {
		static const KoreanVocabFixture::Row rows[] = {
			{ "\xEC\x97\xB4\xEC\x87\xA0", 12, 0 },          // 열쇠 key
			{ "\xEC\xA7\x91\xEB\x8B\xA4", 20, 1 },          // 집다 get (verb)
			{ "\xEB\x93\xA4\xEC\x97\xAC\xEB\x8B\xA4\xEB\xB4\x90", 30, 0x8001 }, // 들여다봐 look+
			{ "\xEB\x93\xA4\xEC\x97\xAC\xEB\x8B\xA4\xEB\xB4\x90", 169, 0 },     //          in
		};
		const Common::Array<byte> img = KoreanVocabFixture::build(magicInFile, rows, ARRAYSIZE(rows));
		Common::MemoryReadStream s(img.data(), img.size());
		return v.loadFromStream(s, magicWanted, "fixture");
	}

public:
	void test_magic_is_checked() {
		Common::KoreanVocabulary v;
		TS_ASSERT(!loadRows(v, "SCIKOR", "AGIKOR"));
		TS_ASSERT(!v.isLoaded());
		TS_ASSERT(loadRows(v, "AGIKOR", "AGIKOR"));
		TS_ASSERT_EQUALS(v.size(), 4u);
	}

	void test_exact_and_stemmed_lookup() {
		Common::KoreanVocabulary v;
		TS_ASSERT(loadRows(v, "AGIKOR", "AGIKOR"));
		uint16 g[4], c[4];
		uint8 role = 0xFF;
		TS_ASSERT_EQUALS(v.lookupAll(u("\xEC\x97\xB4\xEC\x87\xA0"), g, c, 4, role), 1u);
		TS_ASSERT_EQUALS(g[0], 12);
		TS_ASSERT_EQUALS(role, (uint8)Common::kKoreanRoleNone);
		// 열쇠를: the object particle is stripped and named
		TS_ASSERT_EQUALS(v.lookupAll(u("\xEC\x97\xB4\xEC\x87\xA0\xEB\xA5\xBC"), g, c, 4, role), 1u);
		TS_ASSERT_EQUALS(g[0], 12);
		TS_ASSERT_EQUALS(role, (uint8)Common::kKoreanRoleObject);
		// 열쇠로: with
		TS_ASSERT_EQUALS(v.lookupAll(u("\xEC\x97\xB4\xEC\x87\xA0\xEB\xA1\x9C"), g, c, 4, role), 1u);
		TS_ASSERT_EQUALS(role, (uint8)Common::kKoreanRoleWith);
		// exact only: 열쇠를 is a reading, not a stored form
		TS_ASSERT_EQUALS(v.lookupExact(u("\xEC\x97\xB4\xEC\x87\xA0\xEB\xA5\xBC"), g, c, 4), 0u);
		TS_ASSERT_EQUALS(v.lookupExact(u("\xEC\x97\xB4\xEC\x87\xA0"), g, c, 4), 1u);
	}

	void test_sequence_rows_come_back_in_order() {
		Common::KoreanVocabulary v;
		TS_ASSERT(loadRows(v, "AGIKOR", "AGIKOR"));
		uint16 g[4], c[4];
		uint8 role;
		TS_ASSERT_EQUALS(v.lookupAll(u("\xEB\x93\xA4\xEC\x97\xAC\xEB\x8B\xA4\xEB\xB4\x90"), g, c, 4, role), 2u);
		TS_ASSERT_EQUALS(g[0], 30);
		TS_ASSERT(c[0] & Common::KoreanVocabulary::kSequenceFlag);
		TS_ASSERT_EQUALS(g[1], 169);
	}

	static bool loadJoinRows(Common::KoreanVocabulary &v) {
		static const KoreanVocabFixture::Row rows[] = {
			{ "나무", 10, 0 },          // tree
			{ "보다", 11, 1 },          // look (verb)
			{ "물을내리다", 12, 1 },    // flush (verb, stored joined)
			{ "고기", 13, 0 },          // meat
			{ "고다", 14, 1 },          // a verb that 고기 must not become
			{ "사다", 15, 1 },          // buy
			{ "웃다", 16, 1 },          // laugh
		};
		const Common::Array<byte> img = KoreanVocabFixture::build("SCIKOR", rows, ARRAYSIZE(rows));
		Common::MemoryReadStream s(img.data(), img.size());
		return v.loadFromStream(s, "SCIKOR", "fixture");
	}

	// Two typed words are one word only when the table stores them joined:
	// the joined form as typed, or the first word as typed with a reading
	// of the second (물을 내린다 -> 물을내리다). A reading of the joined
	// string must never swallow the second word: 나무 보다 is tree + look,
	// not 나무보다 read as 나무 + the comparative particle.
	void test_a_pair_joins_only_on_a_stored_joined_form() {
		Common::KoreanVocabulary v;
		TS_ASSERT(loadJoinRows(v));
		TS_ASSERT(v.lookupJoined(u("물을"), u("내린다")));
		TS_ASSERT(v.lookupJoined(u("물을"), u("내리다")));
		TS_ASSERT(!v.lookupJoined(u("나무"), u("보다")));
		TS_ASSERT(!v.lookupJoined(u("나무"), u("처럼")));
		TS_ASSERT(!v.lookupJoined(u("나무"), u("이다")));
		TS_ASSERT(!v.lookupJoined(u("나무를"), u("보라")));
		// An auxiliary verb typed apart is still the verb's own ending: 사
		// 줘 is 사줘 (buy), 웃어 봐 is 웃어봐 (laugh). The reading reaches
		// past the first word, so the second word was read, not dropped.
		TS_ASSERT(v.lookupJoined(u("사"), u("줘")));
		TS_ASSERT(v.lookupJoined(u("웃어"), u("봐")));
		// The attached comparative is still a reading of one word.
		uint16 g[4], c[4];
		uint8 role;
		TS_ASSERT_EQUALS(v.lookupAll(u("나무보다"), g, c, 4, role), 1u);
		TS_ASSERT_EQUALS(g[0], 10);
	}

	// A noun the table holds wins over a verb reading of it: 고기 is meat,
	// never 고다 + the -기 nominaliser; 보기 is a reading of 보다.
	void test_a_stored_noun_wins_over_a_gi_reading() {
		Common::KoreanVocabulary v;
		TS_ASSERT(loadJoinRows(v));
		uint16 g[4], c[4];
		uint8 role;
		TS_ASSERT_EQUALS(v.lookupAll(u("고기"), g, c, 4, role), 1u);
		TS_ASSERT_EQUALS(g[0], 13);
		TS_ASSERT_EQUALS(v.lookupAll(u("고기를"), g, c, 4, role), 1u);
		TS_ASSERT_EQUALS(g[0], 13);
		TS_ASSERT_EQUALS(v.lookupAll(u("보기"), g, c, 4, role), 1u);
		TS_ASSERT_EQUALS(g[0], 11);
		TS_ASSERT_EQUALS(v.lookupAll(u("보라"), g, c, 4, role), 1u);
		TS_ASSERT_EQUALS(g[0], 11);
	}

	void test_truncated_table_is_refused() {
		static const byte bad[] = { 'A', 'G', 'I', 'K', 'O', 'R', 0, 0, 2, 0, 0, 0, 9, 0, 0, 0 };
		Common::MemoryReadStream s(bad, sizeof(bad));
		Common::KoreanVocabulary v;
		TS_ASSERT(!v.loadFromStream(s, "AGIKOR", "bad"));
	}
};
