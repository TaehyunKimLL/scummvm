#include <cxxtest/TestSuite.h>
#include "backends/platform/dos/pit-chain.h"

class DosPitChainTestSuite : public CxxTest::TestSuite {
public:
	void test_constants() {
		TS_ASSERT_EQUALS(DOS::kPitDivisor, (uint32)1193);
		TS_ASSERT_EQUALS(DOS::kPitHz, (uint32)(1193182 / 1193));
	}

	void test_default_accumulator_is_zero() {
		DOS::PitChain c;
		TS_ASSERT_EQUALS(c.acc, (uint32)0);
	}

	void test_first_chain_happens_on_the_55th_tick() {
		DOS::PitChain c;
		int chainTick = -1;
		for (int tick = 1; tick <= 100; ++tick) {
			if (DOS::pitTick(c)) {
				chainTick = tick;
				break;
			}
		}
		TS_ASSERT_EQUALS(chainTick, 55);
	}

	void test_accumulator_wraps_below_65536_after_a_chain() {
		DOS::PitChain c;
		for (int tick = 0; tick < 55; ++tick)
			DOS::pitTick(c);
		TS_ASSERT(c.acc < 65536);
		// 55 * 1193 == 65615, minus 65536 == 79.
		TS_ASSERT_EQUALS(c.acc, (uint32)79);
	}

	void test_chain_count_over_55000_ticks_matches_the_formula() {
		DOS::PitChain c;
		uint32 chains = 0;
		for (int tick = 0; tick < 55000; ++tick) {
			if (DOS::pitTick(c))
				++chains;
		}

		// acc is a running sum of 1193 per tick; after N ticks, the number
		// of times it has wrapped past 65536 is floor(N*1193 / 65536).
		uint64 expected = (uint64)55000 * 1193 / 65536;
		TS_ASSERT_EQUALS((uint64)chains, expected);
	}
};
