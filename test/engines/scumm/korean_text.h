#include <cxxtest/TestSuite.h>

#include "engines/scumm/korean_text.h"

// Korean engine text: UTF-8 inside, CP949 converted at the boundary (engines/scumm/korean_text.h).
class ScummKoreanTextTestSuite : public CxxTest::TestSuite {
	static Common::String conv(const char *src, uint *unmapped) {
		Common::Array<byte> out;
		Scumm::cp949ToUtf8((const byte *)src, (const byte *)src + strlen(src), out, *unmapped);
		return Common::String((const char *)out.begin(), out.size());
	}

public:
	void test_ksx1001_table() {
		TS_ASSERT_EQUALS(Scumm::kKsx1001Hangul[0], 0xAC00);	// 가
		TS_ASSERT_EQUALS(Scumm::kKsx1001Hangul[2349], 0xD79D);	// 힝
		for (int i = 1; i < 2350; i++)
			TS_ASSERT_LESS_THAN(Scumm::kKsx1001Hangul[i - 1], Scumm::kKsx1001Hangul[i]);
		TS_ASSERT_EQUALS(Scumm::ksx1001Index(0xAC00), 0);
		TS_ASSERT_EQUALS(Scumm::ksx1001Index(0xD55C), (0xC7 - 0xB0) * 94 + (0xD1 - 0xA1));	// 한 = C7 D1
		TS_ASSERT_EQUALS(Scumm::ksx1001Index(0xB620), -1);	// 똠: not in KS X 1001
		TS_ASSERT_EQUALS(Scumm::ksx1001CodePoint(0xC7, 0xD1), 0xD55Cu);
		TS_ASSERT_EQUALS(Scumm::ksx1001CodePoint(0xA1, 0xA6), 0u);
	}

	void test_cp949_to_utf8_keeps_escapes() {
		uint unmapped = 0;
		// 한 FF 04 xx xx 글: an escape's arguments are not text.
		TS_ASSERT_EQUALS(conv("\xc7\xd1\xff\x04\xb0\xa1\xb1\xdb", &unmapped), "\xed\x95\x9c\xff\x04\xb0\xa1\xea\xb8\x80");
		// A trail byte 0xFE is part of its syllable (BC FE), FE 01 is an escape.
		TS_ASSERT_EQUALS(conv("\xbc\xfe\xfe\x01" "a", &unmapped), "\xec\x88\xad\xfe\x01" "a");
		TS_ASSERT_EQUALS(unmapped, 0u);
	}

	void test_utf8_judgement() {
		const byte utf8[] = "\xea\xb0\x80\xff\x04\xb0\xa1";
		const byte cp949[] = "\xc7\xd1";
		TS_ASSERT(Scumm::textIsUtf8(utf8, utf8 + sizeof(utf8)));
		TS_ASSERT(!Scumm::textIsUtf8(cp949, cp949 + sizeof(cp949)));
	}
};
