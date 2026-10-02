#include <cxxtest/TestSuite.h>
#include <string.h>
#include "backends/platform/dos/exit-trace.h"

class DosExitTraceTestSuite : public CxxTest::TestSuite {
	static Common::String rendered(const DOS::ExitTraceLine &line) {
		char out[DOS::ExitTraceLine::kCols];
		line.render(out);
		Common::String s(out, DOS::ExitTraceLine::kCols);
		while (!s.empty() && s.lastChar() == ' ')
			s.deleteLastChar();
		return s;
	}

public:
	void test_empty_line_is_the_prefix_and_spaces() {
		DOS::ExitTraceLine line;
		char out[DOS::ExitTraceLine::kCols];
		line.render(out);
		TS_ASSERT_EQUALS(out[0], 'X');
		TS_ASSERT_EQUALS(out[1], ':');
		for (int i = 2; i < DOS::ExitTraceLine::kCols; ++i)
			TS_ASSERT_EQUALS(out[i], ' ');
	}

	void test_steps_show_in_order_with_a_dot_once_saved() {
		DOS::ExitTraceLine line;
		line.add(10);
		line.markSaved();
		line.add(11);
		line.markSaved();
		line.add(99);
		TS_ASSERT_EQUALS(rendered(line), Common::String("X:10. 11. 99"));
	}

	void test_only_the_last_entries_are_kept() {
		DOS::ExitTraceLine line;
		for (int i = 0; i < 30; ++i)
			line.add(i);
		TS_ASSERT_EQUALS(line.count, DOS::ExitTraceLine::kMax);
		TS_ASSERT_EQUALS(line.steps[0], 30 - DOS::ExitTraceLine::kMax);
		TS_ASSERT_EQUALS(line.steps[DOS::ExitTraceLine::kMax - 1], 29);
	}

	void test_a_full_line_of_two_digit_steps_fits() {
		DOS::ExitTraceLine line;
		for (int i = 0; i < DOS::ExitTraceLine::kMax; ++i) {
			line.add(40 + i);
			line.markSaved();
		}
		const Common::String s = rendered(line);
		TS_ASSERT(s.hasSuffix("58."));
		TS_ASSERT(s.size() <= (uint)DOS::ExitTraceLine::kCols);
	}

	void test_numbers_are_clamped_to_three_digits() {
		char buf[4];
		TS_ASSERT_EQUALS(DOS::ExitTraceLine::formatNumber(buf, 7), 1);
		TS_ASSERT_EQUALS(buf[0], '7');
		TS_ASSERT_EQUALS(DOS::ExitTraceLine::formatNumber(buf, 105), 3);
		TS_ASSERT_EQUALS(Common::String(buf, 3), Common::String("105"));
		TS_ASSERT_EQUALS(DOS::ExitTraceLine::formatNumber(buf, 5000), 3);
		TS_ASSERT_EQUALS(Common::String(buf, 3), Common::String("999"));
		TS_ASSERT_EQUALS(DOS::ExitTraceLine::formatNumber(buf, -3), 1);
		TS_ASSERT_EQUALS(buf[0], '0');
	}

	void test_log_line_is_step_text_crlf() {
		char buf[40];
		const int n = DOS::formatExitLogLine(buf, sizeof(buf), 42, "atexit: timer teardown");
		TS_ASSERT_EQUALS(Common::String(buf, n), Common::String("42 atexit: timer teardown\r\n"));
	}

	void test_log_line_is_cut_to_fit_and_still_ends_in_crlf() {
		char buf[10];
		const int n = DOS::formatExitLogLine(buf, sizeof(buf), 99, "a very long description");
		TS_ASSERT_EQUALS(n, 10);
		TS_ASSERT_EQUALS(Common::String(buf, n), Common::String("99 a ver\r\n"));
	}

	void test_irq_pic_bits() {
		uint8 m, s;
		DOS::irqPicBits(5, m, s);
		TS_ASSERT_EQUALS(m, 0x20);
		TS_ASSERT_EQUALS(s, 0);
		DOS::irqPicBits(7, m, s);
		TS_ASSERT_EQUALS(m, 0x80);
		TS_ASSERT_EQUALS(s, 0);
		DOS::irqPicBits(10, m, s);
		TS_ASSERT_EQUALS(m, 0x04);	// the cascade
		TS_ASSERT_EQUALS(s, 0x04);
		DOS::irqPicBits(15, m, s);
		TS_ASSERT_EQUALS(m, 0x04);
		TS_ASSERT_EQUALS(s, 0x80);
		DOS::irqPicBits(16, m, s);
		TS_ASSERT_EQUALS(m, 0);
		TS_ASSERT_EQUALS(s, 0);
		DOS::irqPicBits(-1, m, s);
		TS_ASSERT_EQUALS(m, 0);
		TS_ASSERT_EQUALS(s, 0);
	}

	void test_mask_restore_unmasks_what_was_unmasked() {
		// IRQ 5 was open when DOS ran us; SDL3 masked it on its way out.
		TS_ASSERT_EQUALS(DOS::picMaskRestore(0xB8 | 0x20, 0xB8, 0x20), 0xB8);
	}

	void test_mask_restore_keeps_a_masked_one_masked() {
		TS_ASSERT_EQUALS(DOS::picMaskRestore(0x00, 0x20, 0x20), 0x20);
	}

	void test_mask_restore_leaves_other_bits_as_they_are_now() {
		// IRQ 3 changed meanwhile (by someone else): stays as it is now.
		TS_ASSERT_EQUALS(DOS::picMaskRestore(0x28, 0xF0, 0x20), 0x28);
		TS_ASSERT_EQUALS(DOS::picMaskRestore(0x08, 0xD0, 0x20), 0x08);
	}
};
