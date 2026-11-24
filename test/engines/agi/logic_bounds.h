#include <cxxtest/TestSuite.h>

#include "common/endian.h"
#include "common/scummsys.h"

#include "agi/saidscan.h"

/**
 * The logic resource's message section is read by the console and the
 * translation checks from data a game (or a damaged volume) supplies: every
 * read is checked against the resource length. Random resources in exact-size
 * heap blocks, so a read past the end is a sanitizer report, and the answers
 * equal a model.
 */
class AgiLogicBoundsTestSuite : public CxxTest::TestSuite {
public:
	void test_messageSectionReadsStayInsideTheResource() {
		uint32 seed = 1;
		uint valid = 0, invalid = 0;
		for (int iter = 0; iter < 60000; iter++) {
			seed = seed * 1664525u + 1013904223u;
			const uint32 len = (seed >> 8) % 40;
			byte *d = (byte *)malloc(len ? len : 1);
			for (uint32 i = 0; i < len; i++) {
				seed = seed * 1664525u + 1013904223u;
				// small numbers so a bytecode size inside the block and counts of a few messages are common
				d[i] = (seed >> 10) % 3 == 0 ? (byte)(seed >> 16) : (byte)((seed >> 12) % 12);
			}
			if (len >= 2 && iter % 2) { // a bytecode size inside the block about half of the time
				seed = seed * 1664525u + 1013904223u;
				d[0] = (byte)((seed >> 8) % len);
				d[1] = 0;
			}
			int count = -1;
			const bool ok = Agi::logicMessageCount(d, len, count);
			const bool modelOk = len >= 2 && 2u + READ_LE_UINT16(d) < len;
			TS_ASSERT_EQUALS(ok, modelOk);
			if (ok) {
				valid++;
				TS_ASSERT_EQUALS(count, (int)d[2 + READ_LE_UINT16(d)]);
			} else {
				invalid++;
			}
			for (int n = 0; n <= 6; n++) {
				uint16 off = 0xFFFF;
				const bool got = Agi::logicMessageOffset(d, len, n, off);
				bool want = false;
				if (modelOk && n >= 1 && n <= (int)d[2 + READ_LE_UINT16(d)]) {
					const uint32 at = 2 + READ_LE_UINT16(d) + 3 + (n - 1) * 2;
					want = at + 1 < len;
					if (want)
						TS_ASSERT_EQUALS(off, READ_LE_UINT16(d + at));
				}
				TS_ASSERT_EQUALS(got, want);
			}
			free(d);
		}
		TS_ASSERT(valid > 5000u && invalid > 5000u);
	}
};
