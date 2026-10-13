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

	void test_truncated_table_is_refused() {
		static const byte bad[] = { 'A', 'G', 'I', 'K', 'O', 'R', 0, 0, 2, 0, 0, 0, 9, 0, 0, 0 };
		Common::MemoryReadStream s(bad, sizeof(bad));
		Common::KoreanVocabulary v;
		TS_ASSERT(!v.loadFromStream(s, "AGIKOR", "bad"));
	}
};
