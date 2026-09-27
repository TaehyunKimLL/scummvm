#include <cxxtest/TestSuite.h>

#include <string.h>

#include "engines/scumm/charset.h"

/**
 * The Korean fan translations' line breaking (addLinebreaks() with
 * isScummvmKorTarget()) may break between any two Korean characters, and
 * inserts a 0x0D there instead of overwriting a space. addLinebreaks()
 * measures the string once, so each insert has to lengthen the string it
 * works on, and none may write past the buffer.
 */
class ScummKoreanLinebreakTestSuite : public CxxTest::TestSuite {
	enum { kFill = 0x5A }; // stale bytes left in the buffer by earlier text

	static void fill(byte *buf, int size, const char *text) {
		memset(buf, kFill, size);
		memcpy(buf, text, strlen(text) + 1);
	}

public:
	void test_one_insert() {
		byte buf[32];
		fill(buf, sizeof(buf), "AABB");
		int len = 4;
		TS_ASSERT(Scumm::insertLinebreak(buf, len, 2, sizeof(buf)));
		TS_ASSERT_EQUALS(len, 5);
		TS_ASSERT_SAME_DATA(buf, "AA\rBB", 6);
		TS_ASSERT_EQUALS(buf[6], kFill);
	}

	// Three inserts in one pass, as addLinebreaks() makes them for a line
	// that wraps three times: the NUL moves with every insert and no
	// character is lost.
	void test_three_inserts_keep_the_string() {
		byte buf[32];
		fill(buf, sizeof(buf), "AABBCCDD");
		int len = 8;
		TS_ASSERT(Scumm::insertLinebreak(buf, len, 2, sizeof(buf)));
		TS_ASSERT(Scumm::insertLinebreak(buf, len, 5, sizeof(buf)));
		TS_ASSERT(Scumm::insertLinebreak(buf, len, 8, sizeof(buf)));
		TS_ASSERT_EQUALS(len, 11);
		TS_ASSERT_EQUALS(strlen((const char *)buf), 11u);
		TS_ASSERT_SAME_DATA(buf, "AA\rBB\rCC\rDD", 12);
		TS_ASSERT_EQUALS(buf[12], kFill);
	}

	// The longer string must fit, NUL included: with no byte to spare the
	// insert is refused and the string is left alone.
	void test_insert_is_bounded_by_the_buffer() {
		byte buf[8];
		fill(buf, sizeof(buf), "AABB");
		int len = 4;
		TS_ASSERT(!Scumm::insertLinebreak(buf, len, 2, 5));
		TS_ASSERT_EQUALS(len, 4);
		TS_ASSERT_SAME_DATA(buf, "AABB", 5);
		TS_ASSERT_EQUALS(buf[5], kFill);

		TS_ASSERT(Scumm::insertLinebreak(buf, len, 2, 6));
		TS_ASSERT_EQUALS(len, 5);
		TS_ASSERT_SAME_DATA(buf, "AA\rBB", 6);
		TS_ASSERT_EQUALS(buf[6], kFill);

		// Full again: the next insert is refused, the guard byte kept.
		TS_ASSERT(!Scumm::insertLinebreak(buf, len, 3, 6));
		TS_ASSERT_EQUALS(len, 5);
		TS_ASSERT_SAME_DATA(buf, "AA\rBB", 6);
		TS_ASSERT_EQUALS(buf[6], kFill);
	}
};
