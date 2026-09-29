#include <cxxtest/TestSuite.h>
#include "backends/platform/dos/dos-memory.h"

class DosMemoryTestSuite : public CxxTest::TestSuite {
public:
	void test_sentinel_becomes_zero_in_every_field() {
		const DOS::MemInfo m = DOS::memFromPages(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF);
		TS_ASSERT_EQUALS(m.freeKB, 0u);
		TS_ASSERT_EQUALS(m.largestKB, 0u);
		TS_ASSERT_EQUALS(m.physFreeKB, 0u);
		TS_ASSERT_EQUALS(m.physTotalKB, 0u);
	}

	void test_sentinel_in_one_field_leaves_the_others() {
		const DOS::MemInfo m = DOS::memFromPages(0xFFFFFFFF, 1024, 4096, 4096);
		TS_ASSERT_EQUALS(m.freeKB, 0u);
		TS_ASSERT_EQUALS(m.largestKB, 1u);
		TS_ASSERT_EQUALS(m.physFreeKB, 16384u);
		TS_ASSERT_EQUALS(m.physTotalKB, 16384u);
	}

	void test_pages_are_4kb() {
		const DOS::MemInfo m = DOS::memFromPages(0, 0, 4096, 8192);
		TS_ASSERT_EQUALS(m.physFreeKB, 16384u);
		TS_ASSERT_EQUALS(m.physTotalKB, 32768u);
	}

	void test_bytes_convert_to_kb() {
		const DOS::MemInfo m = DOS::memFromPages(2 * 1024 * 1024, 512 * 1024, 0, 0);
		TS_ASSERT_EQUALS(m.freeKB, 2048u);
		TS_ASSERT_EQUALS(m.largestKB, 512u);
	}

	void test_format_matches_exactly() {
		DOS::MemInfo m;
		m.freeKB = 12345;
		m.largestKB = 6000;
		m.physFreeKB = 8192;
		m.physTotalKB = 16384;
		TS_ASSERT_EQUALS(DOS::formatMemInfo("engine", m),
			Common::String("DOS: memory engine dpmi_free=12345 largest=6000 phys_free=8192 phys_total=16384"));
	}

	void test_format_uses_the_given_phase() {
		DOS::MemInfo m = { 0, 0, 0, 0 };
		TS_ASSERT_EQUALS(DOS::formatMemInfo("quit", m),
			Common::String("DOS: memory quit dpmi_free=0 largest=0 phys_free=0 phys_total=0"));
	}
};
