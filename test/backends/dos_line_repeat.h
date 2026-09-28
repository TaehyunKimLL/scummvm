#include <cxxtest/TestSuite.h>
#include "backends/platform/dos/line-repeat.h"

class DosLineRepeatTestSuite : public CxxTest::TestSuite {
public:
	void test_row_4_repeats_to_physical_4_and_5() {
		TS_ASSERT_EQUALS(DOS::physRow(4), 4);
		TS_ASSERT_EQUALS(DOS::repeats(4), true);
	}

	void test_row_5_maps_to_physical_6_only() {
		TS_ASSERT_EQUALS(DOS::physRow(5), 6);
		TS_ASSERT_EQUALS(DOS::repeats(5), false);
	}

	void test_logical_row_is_inverse_of_phys_row() {
		for (int y = 0; y < 400; ++y)
			TS_ASSERT_EQUALS(DOS::logicalRow(DOS::physRow(y)), y);
	}

	void test_logical_row_of_a_repeated_physical_row_rounds_to_the_next_row() {
		// logicalRow() is the cheap py - py/6 formula, not a true inverse:
		// on the extra (repeated) physical row it reports y + 1, not y --
		// at most one row off, which is fine for a mouse position.
		for (int y = 0; y < 400; ++y)
			if (DOS::repeats(y))
				TS_ASSERT_EQUALS(DOS::logicalRow(DOS::physRow(y) + 1), y + 1);
	}

	void test_phys_row_400_reaches_480() {
		// 400 * 6 / 5 == 480: the last logical row (399) plus its repeat
		// lands on physical row 479, the last of a 480-row picture.
		TS_ASSERT_EQUALS(DOS::physRow(399) + 1, 479);
		TS_ASSERT_EQUALS(DOS::repeats(399), true);
	}

	void test_phys_rect_of_full_picture_is_full_480() {
		Common::Rect full(0, 0, 10, 400);
		Common::Rect got = DOS::physRect(full);
		TS_ASSERT_EQUALS(got, Common::Rect(0, 0, 10, 480));
	}

	void test_phys_rect_boundary_of_a_partial_span() {
		// Rows 10..14 (5 logical rows); row 14 repeats, so this spans
		// physical rows 12..17 (6 physical rows).
		Common::Rect span(0, 10, 10, 15);
		Common::Rect got = DOS::physRect(span);
		TS_ASSERT_EQUALS(got, Common::Rect(0, 12, 10, 18));
	}

	void test_copy_rows_fills_all_480_rows_with_no_gaps() {
		byte src[400];
		byte dst[480];
		for (int y = 0; y < 400; ++y)
			src[y] = (byte)((y % 255) + 1); // never 0, so 0 in dst means "untouched" (avoid byte wraparound at y=255)
		memset(dst, 0, sizeof(dst));

		DOS::copyRows(dst, 1, src, 1, 1, Common::Rect(0, 0, 1, 400));

		for (int py = 0; py < 480; ++py)
			TS_ASSERT_DIFFERS(dst[py], 0);
	}

	void test_copy_rows_row_4_goes_to_physical_4_and_5() {
		byte src[400];
		byte dst[480];
		for (int y = 0; y < 400; ++y)
			src[y] = (byte)((y % 255) + 1);
		memset(dst, 0, sizeof(dst));

		DOS::copyRows(dst, 1, src, 1, 1, Common::Rect(0, 0, 1, 400));

		TS_ASSERT_EQUALS(dst[4], src[4]);
		TS_ASSERT_EQUALS(dst[5], src[4]);
	}

	void test_copy_rows_row_5_goes_to_physical_6_only() {
		byte src[400];
		byte dst[480];
		for (int y = 0; y < 400; ++y)
			src[y] = (byte)((y % 255) + 1);
		memset(dst, 0, sizeof(dst));

		DOS::copyRows(dst, 1, src, 1, 1, Common::Rect(0, 0, 1, 400));

		TS_ASSERT_EQUALS(dst[6], src[5]);
		// Row 5 does not repeat, so physical row 7 comes from logical row 6,
		// not row 5.
		TS_ASSERT_DIFFERS(dst[7], src[5]);
	}
};
