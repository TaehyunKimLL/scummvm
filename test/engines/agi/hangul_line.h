#include <cxxtest/TestSuite.h>

#include "agi/hangulline.h"

#include "common/hangul.h"

// The built-in composer driving AGI's code-point edit line: a jamo key
// rewrites the run at the end of the line (committed syllables + the one in
// progress), a non-jamo key ends it, a jamo that would grow the line past
// its budget is refused and changes nothing (review focus 2).
class AgiHangulLineTestSuite : public CxxTest::TestSuite {
	static void feed(Agi::HangulLine &h, const char *keys, uint32 *cells, int16 &len, int16 max) {
		for (; *keys; keys++)
			h.feed(*keys, cells, len, max);
	}

public:
	void test_dufthl_is_yeolsoe() {
		Agi::HangulLine h;
		uint32 cells[42] = { 0 };
		int16 len = 0;
		feed(h, "dufthl", cells, len, 40);
		TS_ASSERT_EQUALS(len, 2);
		TS_ASSERT_EQUALS(cells[0], 0xC5F4u); // 열
		TS_ASSERT_EQUALS(cells[1], 0xC1E0u); // 쇠
		TS_ASSERT_EQUALS(cells[2], 0u);
		TS_ASSERT(h.composing());
		TS_ASSERT_EQUALS(h.preedit(), 0xC1E0u);
	}

	void test_backspace_removes_jamo_then_syllables() {
		Agi::HangulLine h;
		uint32 cells[42] = { 0 };
		int16 len = 0;
		feed(h, "dufthl", cells, len, 40);
		TS_ASSERT(h.backspace(cells, len));
		TS_ASSERT_EQUALS(len, 2);
		TS_ASSERT_DIFFERS(cells[1], 0xC1E0u); // 쇠 lost a jamo
		int guard = 0;
		while (h.backspace(cells, len) && guard++ < 20) {
		}
		TS_ASSERT_EQUALS(len, 0);
		TS_ASSERT_EQUALS(cells[0], 0u);
		TS_ASSERT(!h.composing());
		TS_ASSERT(!h.backspace(cells, len));
	}

	void test_a_non_jamo_key_ends_the_run() {
		Agi::HangulLine h;
		uint32 cells[42] = { 0 };
		int16 len = 0;
		feed(h, "gks", cells, len, 40);
		TS_ASSERT_EQUALS(len, 1);
		TS_ASSERT_EQUALS(cells[0], 0xD55Cu); // 한
		TS_ASSERT(!h.feed('1', cells, len, 40));
		TS_ASSERT(!h.composing());
		TS_ASSERT_EQUALS(len, 1);
		feed(h, "rk", cells, len, 40); // a new run after the committed 한
		TS_ASSERT_EQUALS(len, 2);
		TS_ASSERT_EQUALS(cells[0], 0xD55Cu);
		TS_ASSERT_EQUALS(cells[1], 0xAC00u); // 가
		TS_ASSERT_EQUALS(h.runStart(), 1);
	}

	void test_budget_refuses_a_jamo_without_change() {
		Agi::HangulLine h;
		uint32 cells[42] = { 0 };
		int16 len = 39;
		for (int i = 0; i < 39; i++)
			cells[i] = 'a';
		feed(h, "rkr", cells, len, 40); // 각 at cell 39
		TS_ASSERT_EQUALS(len, 40);
		TS_ASSERT_EQUALS(cells[39], 0xAC01u);
		TS_ASSERT(h.feed('k', cells, len, 40)); // 가가 would need 41
		TS_ASSERT_EQUALS(len, 40);
		TS_ASSERT_EQUALS(cells[39], 0xAC01u);
		TS_ASSERT_EQUALS(cells[40], 0u);
		TS_ASSERT(h.composing());
	}
	// 50000 random runs of jamo keys, other keys, backspaces and commits under a
	// random budget: the line never passes the budget, the cells before the
	// run never change, and the run is the composer's text for the same keys.
	void test_randomKeysKeepTheRunInside() {
		static const char keys[] = "rRseEfaqQtTdwWczxvgkoiOjpuPhynbml1 .";
		uint32 seed = 7;
		uint bad = 0;
		for (uint round = 0; round < 50000; round++) {
			uint32 cells[42];
			int16 len = 0;
			cells[0] = 0;
			seed = seed * 1103515245u + 12345u;
			const int16 budget = 1 + (seed >> 8) % 40;
			Agi::HangulLine line;
			Common::HangulComposer model;
			uint32 before[42];
			for (uint k = 0; k < 30; k++) {
				seed = seed * 1103515245u + 12345u;
				const uint pick = (seed >> 8) % (sizeof(keys) + 2);
				memcpy(before, cells, sizeof(cells));
				const int16 keep = line.composing() ? line.runStart() : len; // what this key may not touch
				if (pick == sizeof(keys)) { // backspace
					if (line.backspace(cells, len))
						model.backspace();
				} else if (pick == sizeof(keys) + 1) { // a commit (Enter, a mode switch)
					line.commit(len);
					model.reset();
				} else {
					const char c = keys[pick % (sizeof(keys) - 1)];
					const Common::HangulComposer was = model;
					if (line.feed(c, cells, len, budget)) {
						model.feed(c);
						if ((int16)(line.runStart() + model.text().size()) > budget)
							model = was; // refused: the line did not change
					} else {
						model.reset(); // not a jamo: the run ended
						if (len < budget) {
							cells[len++] = (byte)c;
							cells[len] = 0;
						}
					}
				}
				if (len > budget)
					bad++;
				for (int16 i = 0; i < MIN<int16>(keep, len); i++)
					if (cells[i] != before[i])
						bad++;
				if (line.composing()) {
					const Common::U32String run = model.text();
					if ((int16)(line.runStart() + run.size()) != len)
						bad++;
					for (uint i = 0; i < run.size() && line.runStart() + (int16)i < len; i++)
						if (cells[line.runStart() + i] != run[i])
							bad++;
				}
			}
		}
		TS_ASSERT_EQUALS(bad, 0u);
	}
};
