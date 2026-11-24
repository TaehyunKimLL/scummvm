#include <cxxtest/TestSuite.h>

#include "common/memstream.h"
#include "common/str.h"

#include "agi/saved_strings.h"
#include "agi/textcells.h"

// The UTF-8 string trailer of a version 11 save (agi/saved_strings.h).
class AgiSavedStringsTestSuite : public CxxTest::TestSuite {
	enum { kCount = 24, kStride = 161 }; // MAX_STRINGS, MAX_STRINGBYTES

	struct Lcg {
		uint32 s;
		explicit Lcg(uint32 seed) : s(seed) {}
		uint32 below(uint32 n) { s = s * 1103515245u + 12345u; return (s >> 8) % n; }
	};

	static void randomTable(Lcg &r, char *table, Common::String *answers) {
		static const char *const pieces[] = { "\xEA\xB0\x80", "\xED\x9E\xA3", "a", "Z", " ", "\xC3\xA9", "\xF0\x9F\x98\x80" };
		memset(table, 0, kCount * kStride);
		for (uint i = 0; i < kCount; i++) {
			Common::String s, a;
			const uint n = r.below(40); // up to 39 cells, as setString()
			for (uint k = 0; k < n; k++)
				s += pieces[r.below(ARRAYSIZE(pieces))];
			s = Agi::TextCells::clipCells(s.c_str(), 39, true);
			memcpy(table + i * kStride, s.c_str(), s.size());
			if (r.below(3) == 0)
				for (uint k = 0; k < r.below(20); k++)
					a += pieces[r.below(4)];
			answers[i] = a;
		}
	}

public:
	void test_roundTripOfRandomTables() {
		Lcg r(11);
		uint bad = 0;
		for (uint round = 0; round < 2000; round++) {
			char table[kCount * kStride], back[kCount * kStride];
			Common::String answers[kCount], backAnswers[kCount];
			randomTable(r, table, answers);
			Common::MemoryWriteStreamDynamic out(DisposeAfterUse::YES);
			Agi::writeSavedStrings(out, table, kStride, kCount, answers);
			Common::MemoryReadStream in(out.getData(), out.size());
			memset(back, 0x55, sizeof(back));
			if (Agi::readSavedStrings(in, back, kStride, kCount, backAnswers) != Agi::kSavedStringsRead)
				bad++;
			for (uint i = 0; i < kCount; i++)
				if (strcmp(table + i * kStride, back + i * kStride) || answers[i] != backAnswers[i])
					bad++;
		}
		TS_ASSERT_EQUALS(bad, 0u);
	}

	// Every cut of a trailer, and random byte damage: never a partial
	// table, never a string that is not UTF-8, never a read past the end.
	void test_cutOrDamagedTrailersChangeNothingOrStayUtf8() {
		Lcg r(12);
		char table[kCount * kStride];
		Common::String answers[kCount];
		randomTable(r, table, answers);
		Common::MemoryWriteStreamDynamic out(DisposeAfterUse::YES);
		Agi::writeSavedStrings(out, table, kStride, kCount, answers);
		uint bad = 0;
		for (uint32 cut = 0; cut < out.size(); cut++) {
			char back[kCount * kStride];
			Common::String backAnswers[kCount];
			memset(back, 0x55, sizeof(back));
			Common::MemoryReadStream in(out.getData(), cut);
			if (Agi::readSavedStrings(in, back, kStride, kCount, backAnswers) == Agi::kSavedStringsRead)
				bad++;
			for (uint i = 0; i < sizeof(back); i++)
				if (back[i] != 0x55)
					bad++;
		}
		Common::Array<byte> bytes;
		for (uint32 i = 0; i < out.size(); i++)
			bytes.push_back(out.getData()[i]);
		for (uint round = 0; round < 20000; round++) {
			Common::Array<byte> d(bytes);
			for (uint k = 0; k < 1 + r.below(4); k++)
				d[r.below(d.size())] = r.below(256);
			char back[kCount * kStride];
			Common::String backAnswers[kCount];
			memset(back, 0, sizeof(back));
			Common::MemoryReadStream in(d.data(), d.size());
			if (Agi::readSavedStrings(in, back, kStride, kCount, backAnswers) == Agi::kSavedStringsRead)
				for (uint i = 0; i < kCount; i++)
					if (!Agi::TextCells::isValidUtf8(back + i * kStride) || strlen(back + i * kStride) >= kStride ||
					    !Agi::TextCells::isValidUtf8(backAnswers[i].c_str()))
						bad++;
		}
		TS_ASSERT_EQUALS(bad, 0u);
	}

	void test_noTrailerIsNone() {
		const byte upstreamTail[] = { 0x00, 0x00 }; // end of a version 11 save: nothing after AGIPal
		Common::MemoryReadStream in(upstreamTail, 0);
		char table[kCount * kStride];
		Common::String answers[kCount];
		TS_ASSERT_EQUALS(Agi::readSavedStrings(in, table, kStride, kCount, answers), Agi::kSavedStringsNone);
		Common::MemoryReadStream other(upstreamTail, sizeof(upstreamTail));
		TS_ASSERT_EQUALS(Agi::readSavedStrings(other, table, kStride, kCount, answers), Agi::kSavedStringsNone);
	}

	// The version 11 slot: the longest whole-character prefix of at most 39 bytes.
	void test_legacySlotCutsOnACharacter() {
		char slot[Agi::kLegacySlotBytes];
		Common::String ko;
		for (int i = 0; i < 20; i++)
			ko += "\xEA\xB0\x80";
		Agi::legacySlot(ko.c_str(), slot);
		TS_ASSERT_EQUALS(strlen(slot), 39u); // 13 syllables
		TS_ASSERT(Agi::TextCells::isValidUtf8(slot));
		Agi::legacySlot("abc", slot);
		TS_ASSERT_EQUALS(Common::String(slot), "abc");
		for (int i = 3; i < Agi::kLegacySlotBytes; i++)
			TS_ASSERT_EQUALS(slot[i], 0);
	}

	void test_legacyStringsBecomeUtf8() {
		char table[2 * kStride];
		memset(table, 0, sizeof(table));
		Common::strlcpy(table, "caf\x82", kStride); // CP850 e acute
		Common::strlcpy(table + kStride, "plain", kStride);
		Agi::convertLegacyStrings(table, kStride, 2, 39);
		TS_ASSERT_EQUALS(Common::String(table), "caf\xC3\xA9");
		TS_ASSERT_EQUALS(Common::String(table + kStride), "plain");
	}

	// loadGame()'s tail in UTF-8 text mode (finishSavedStrings): a save whose
	// version 11 table already holds UTF-8 - a Korean save written again by an
	// English or unmodified build (no trailer) or with a cut trailer - must not be
	// re-decoded as CP850 (켄이 보냈다 became ý╝äýØ┤ Ù│┤ÙâêÙïñ); only a code page
	// table is converted, and only when there is no trailer at all.
	static Common::String randomSlot(uint32 &seed, bool utf8) {
		Common::String s;
		const int n = seed % 14;
		for (int i = 0; i < n; i++) {
			seed = seed * 1664525u + 1013904223u;
			const uint32 pick = (seed >> 8) % 4;
			if (pick == 0)
				s += (char)('a' + (seed >> 12) % 26);
			else if (utf8)
				s += "\xEC\xBC\x84"; // 켄
			else
				s += (char)(0x80 + (seed >> 12) % 0x80);
		}
		return s;
	}

	void test_utf8SlotsSurviveTheLoadPath() {
		uint32 seed = 5;
		uint converted = 0, kept = 0;
		for (int iter = 0; iter < 20000; iter++) {
			char table[4 * kStride];
			char before[4 * kStride];
			memset(table, 0, sizeof(table));
			bool utf8[4];
			for (int i = 0; i < 4; i++) {
				seed = seed * 1664525u + 1013904223u;
				utf8[i] = (seed >> 8) % 2;
				Common::strlcpy(table + i * kStride, randomSlot(seed, utf8[i]).c_str(), kStride - 1);
			}
			memcpy(before, table, sizeof(table));
			for (int r = 0; r < 3; r++) {
				memcpy(table, before, sizeof(table));
				Agi::finishSavedStrings((Agi::SavedStringsResult)r, table, kStride, 4, 39);
				for (int i = 0; i < 4; i++) {
					const char *slot = table + i * kStride;
					TS_ASSERT_LESS_THAN(strlen(slot), (size_t)kStride);
					for (uint k = strlen(slot) + 1; k < kStride; k++)
						TS_ASSERT_EQUALS(slot[k], before[i * kStride + k]); // nothing written past the NUL
					const bool high = Agi::TextCells::hasHighBytes(before + i * kStride);
					if (r != Agi::kSavedStringsNone || !high || Agi::TextCells::isValidUtf8(before + i * kStride)) {
						TS_ASSERT_EQUALS(Common::String(slot), Common::String(before + i * kStride)); // untouched
						kept += high;
					} else {
						TS_ASSERT(Agi::TextCells::isValidUtf8(slot)); // a code page slot became UTF-8
						converted++;
					}
				}
			}
		}
		TS_ASSERT(converted > 1000u && kept > 1000u);
	}
};
