#include <cxxtest/TestSuite.h>

#include "engines/scumm/korean_text.h"

// Korean engine text: UTF-8 inside, CP949 converted at the boundary (engines/scumm/korean_text.h).
class ScummKoreanTextTestSuite : public CxxTest::TestSuite {
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

};
