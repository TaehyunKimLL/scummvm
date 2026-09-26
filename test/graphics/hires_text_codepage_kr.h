#include <cxxtest/TestSuite.h>

#include "common/archive.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/str.h"
#include "common/ustr.h"
#include "graphics/hires_text/codepage_kr.h"

#include "../system/null_osystem.h"

using namespace Graphics::KoreanCodePage;

/**
 * Tests for the Korean code-page helpers engines use to turn the EUC-KR bytes
 * of the fan patches into code points (and back to KS X 1001 glyph indices).
 * The table behind them is static, so none of this needs encoding.dat; one
 * test cross-checks it against the shared decoder when encoding.dat is there.
 */
class HiResTextCodePageKrTestSuite : public CxxTest::TestSuite {
public:
	void test_is_euc_kr_pair() {
		TS_ASSERT(isEucKrPair(0xB0, 0xA1));
		TS_ASSERT(isEucKrPair(0xA1, 0xFE));
		TS_ASSERT(isEucKrPair(0xFE, 0xFE));
		TS_ASSERT(!isEucKrPair(0xA0, 0xA1));
		TS_ASSERT(!isEucKrPair(0xB0, 0xA0));
		TS_ASSERT(!isEucKrPair(0xB0, 0xFF));
		TS_ASSERT(!isEucKrPair(0xE9, 0x62));	// Latin-1 e acute, then 'b'
		TS_ASSERT(!isEucKrPair(0x41, 0xA1));
	}

	void test_ksx1001_index() {
		TS_ASSERT_EQUALS(ksx1001HangulIndex(0xB0, 0xA1), 0);
		TS_ASSERT_EQUALS(ksx1001HangulIndex(0xB0, 0xFE), 93);
		TS_ASSERT_EQUALS(ksx1001HangulIndex(0xB1, 0xA1), 94);
		TS_ASSERT_EQUALS(ksx1001HangulIndex(0xC8, 0xFE), 2349);
		TS_ASSERT_EQUALS(ksx1001HangulIndex(0xB0, 0xA0), -1);
		TS_ASSERT_EQUALS(ksx1001HangulIndex(0xB0, 0xFF), -1);
		TS_ASSERT_EQUALS(ksx1001HangulIndex(0xAF, 0xFE), -1);
		TS_ASSERT_EQUALS(ksx1001HangulIndex(0xC9, 0xA1), -1);

		TS_ASSERT_EQUALS(ksx1001HangulIndexOf(0xAC00), 0);		// ga
		TS_ASSERT_EQUALS(ksx1001HangulIndexOf(0xD79D), 2349);	// hing, pair C8 FE
		TS_ASSERT_EQUALS(ksx1001HangulIndexOf(0xB9E4), 788);	// mae, pair B8 C5
		TS_ASSERT_EQUALS(ksx1001HangulIndexOf(0xD7A3), -1);	// hih: not in KS X 1001
		TS_ASSERT_EQUALS(ksx1001HangulIndexOf(0xAC02), -1);	// gakk: not in KS X 1001 either
		TS_ASSERT_EQUALS(ksx1001HangulIndexOf(0x0041), -1);
		TS_ASSERT_EQUALS(ksx1001HangulIndexOf(0x110000), -1);

		TS_ASSERT_EQUALS(decodeEucKrPair(0xB0, 0xA1), 0xAC00u);
		TS_ASSERT_EQUALS(decodeEucKrPair(0xB8, 0xC5), 0xB9E4u);
		TS_ASSERT_EQUALS(decodeEucKrPair(0xC8, 0xFE), 0xD79Du);
		TS_ASSERT_EQUALS(decodeEucKrPair(0xA1, 0xA1), 0u);	// a symbol row: not in the table
		TS_ASSERT_EQUALS(decodeEucKrPair(0xE9, 0x62), 0u);
		TS_ASSERT(!isEucKrPair(0xE9, 0x62));
	}

	// Every index maps to a code point and back, and the table is sorted, so
	// the reverse lookup cannot miss an entry.
	void test_ksx1001_round_trip() {
		uint32 prev = 0;
		for (int i = 0; i < kKsx1001HangulCount; i++) {
			const byte hi = (byte)(0xB0 + i / 94);
			const byte lo = (byte)(0xA1 + i % 94);
			TS_ASSERT_EQUALS(ksx1001HangulIndex(hi, lo), i);
			const uint32 cp = decodeEucKrPair(hi, lo);
			TS_ASSERT(cp >= 0xAC00 && cp <= 0xD7A3);
			TS_ASSERT(cp > prev);
			prev = cp;
			TS_ASSERT_EQUALS(ksx1001HangulIndexOf(cp), i);
		}
	}

	// The static table against the shared CP949 decoder, which reads
	// encoding.dat. The test runner finds it in test/engine-data once the
	// null OSystem is installed; without it the check cannot be made.
	void test_ksx1001_table_matches_encoding_dat() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::install_null_g_system();
		// An in-tree build runs the runner from the source root.
		const char *kFallbackDir = "dists/engine-data";
		bool addedFallback = false;
		if (!Common::File::exists("encoding.dat")) {
			Common::FSNode dir(kFallbackDir);
			if (dir.exists()) {
				SearchMan.addDirectory("hires_text_codepage_kr", dir);
				addedFallback = true;
			}
		}

		// Checked before anything is decoded: decoding without the file
		// makes the shared decoder print a warning, and the output of
		// make test must stay pristine.
		if (!Common::File::exists("encoding.dat")) {
			if (addedFallback)
				SearchMan.remove("hires_text_codepage_kr");
			Common::uninstall_null_g_system();
			TS_SKIP("encoding.dat is not available; the static KS X 1001 table was not cross-checked");
			return;
		}

		int mismatches = 0;
		for (int i = 0; i < kKsx1001HangulCount; i++) {
			const byte hi = (byte)(0xB0 + i / 94);
			const byte lo = (byte)(0xA1 + i % 94);
			const char pair[2] = { (char)hi, (char)lo };
			const Common::U32String decoded(Common::String(pair, 2), Common::kWindows949);
			if (decoded.size() != 1 || decoded[0] != decodeEucKrPair(hi, lo))
				mismatches++;
		}
		TS_ASSERT_EQUALS(mismatches, 0);

		if (addedFallback)
			SearchMan.remove("hires_text_codepage_kr");
		Common::uninstall_null_g_system();
#endif
	}
};
