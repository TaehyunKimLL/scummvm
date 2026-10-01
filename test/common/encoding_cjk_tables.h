#include <cxxtest/TestSuite.h>

#include "common/str.h"
#include "common/ustr.h"
#include "common/str-enc.h"
#include "common/enc-internal.h"
#include "common/file.h"
#include "../system/null_osystem.h"

// encoding.dat holds six tables; a code page reads its own only, the first
// time it is needed. The runner finds the file in test/engine-data once
// the null OSystem is installed (uninstalling it releases every table).
class EncodingCJKTablesTestSuite : public CxxTest::TestSuite {
#if NULL_OSYSTEM_IS_AVAILABLE
	bool _haveFile;

	static int loadedCount() {
		int n = 0;
		for (int i = 0; i < Common::kCJKTableCount; i++)
			n += Common::isCJKTableLoaded((Common::CJKTable)i) ? 1 : 0;
		return n;
	}
#endif

public:
	void setUp() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::install_null_g_system();
		_haveFile = Common::File::exists("encoding.dat");
#endif
	}

	void tearDown() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::uninstall_null_g_system();
#endif
	}

	void test_nothing_is_loaded_before_use() {
#if NULL_OSYSTEM_IS_AVAILABLE
		TS_ASSERT_EQUALS(loadedCount(), 0);
#endif
	}

	void test_decoding_949_loads_only_949() {
#if NULL_OSYSTEM_IS_AVAILABLE
		if (!_haveFile)
			return;
		const byte uhc[] = { 0xb0, 0xa1, 0x00 };	// U+AC00
		Common::U32String s((const char *)uhc, Common::kWindows949);
		TS_ASSERT_EQUALS(s.size(), 1u);
		TS_ASSERT_EQUALS((uint32)s[0], 0xAC00u);
		TS_ASSERT(Common::isCJKTableLoaded(Common::kCJKTable949));
		TS_ASSERT_EQUALS(loadedCount(), 1);
#endif
	}

	void test_uhc_to_ucs_loads_only_949() {
#if NULL_OSYSTEM_IS_AVAILABLE
		if (!_haveFile)
			return;
		TS_ASSERT_EQUALS((uint32)Common::convertUHCToUCS(0xb0, 0xa1), 0xAC00u);
		TS_ASSERT(Common::isCJKTableLoaded(Common::kCJKTable949));
		TS_ASSERT_EQUALS(loadedCount(), 1);
#endif
	}

	void test_encoding_949_loads_only_949() {
#if NULL_OSYSTEM_IS_AVAILABLE
		if (!_haveFile)
			return;
		Common::U32String s;
		s += (Common::u32char_type_t)0xAC00;
		const Common::String uhc = s.encode(Common::kWindows949);
		TS_ASSERT_EQUALS(uhc, Common::String("\xb0\xa1"));
		TS_ASSERT(Common::isCJKTableLoaded(Common::kCJKTable949));
		TS_ASSERT_EQUALS(loadedCount(), 1);
#endif
	}

	void test_each_code_page_adds_its_own_table() {
#if NULL_OSYSTEM_IS_AVAILABLE
		if (!_haveFile)
			return;
		const byte sjis[] = { 0x82, 0xa0, 0x00 };	// U+3042
		Common::U32String a((const char *)sjis, Common::kWindows932);
		TS_ASSERT_EQUALS((uint32)a[0], 0x3042u);
		TS_ASSERT(Common::isCJKTableLoaded(Common::kCJKTable932));
		TS_ASSERT_EQUALS(loadedCount(), 1);

		const byte big5[] = { 0xa4, 0x40, 0x00 };	// U+4E00
		Common::U32String b((const char *)big5, Common::kWindows950);
		TS_ASSERT_EQUALS((uint32)b[0], 0x4E00u);
		TS_ASSERT(Common::isCJKTableLoaded(Common::kCJKTable950));
		TS_ASSERT_EQUALS(loadedCount(), 2);

		const byte gbk[] = { 0xd2, 0xbb, 0x00 };	// U+4E00
		Common::U32String c((const char *)gbk, Common::kWindows936);
		TS_ASSERT_EQUALS((uint32)c[0], 0x4E00u);
		TS_ASSERT(Common::isCJKTableLoaded(Common::kCJKTable936));
		TS_ASSERT_EQUALS(loadedCount(), 3);

		const byte johab[] = { 0x88, 0x61, 0x00 };	// U+AC00
		Common::U32String d((const char *)johab, Common::kJohab);
		TS_ASSERT_EQUALS((uint32)d[0], 0xAC00u);
		TS_ASSERT(Common::isCJKTableLoaded(Common::kCJKTableJohab));
		TS_ASSERT_EQUALS(loadedCount(), 4);
		TS_ASSERT(!Common::isCJKTableLoaded(Common::kCJKTable949));
		TS_ASSERT(!Common::isCJKTableLoaded(Common::kCJKTableT2S));
#endif
	}

	void test_t2s_loads_only_its_table() {
#if NULL_OSYSTEM_IS_AVAILABLE
		if (!_haveFile)
			return;
		Common::U32String t;
		t += (Common::u32char_type_t)0x9AD4;	// traditional "body"
		const Common::U32String s = t.transcodeChineseT2S();
		TS_ASSERT_EQUALS((uint32)s[0], 0x4F53u);	// simplified
		TS_ASSERT(Common::isCJKTableLoaded(Common::kCJKTableT2S));
		TS_ASSERT_EQUALS(loadedCount(), 1);
#endif
	}

	void test_release_unloads_everything() {
#if NULL_OSYSTEM_IS_AVAILABLE
		if (!_haveFile)
			return;
		const byte uhc[] = { 0xb0, 0xa1, 0x00 };
		Common::U32String s((const char *)uhc, Common::kWindows949);
		TS_ASSERT_EQUALS(loadedCount(), 1);
		Common::releaseCJKTables();
		TS_ASSERT_EQUALS(loadedCount(), 0);
		// and it loads again on the next use
		Common::U32String again((const char *)uhc, Common::kWindows949);
		TS_ASSERT_EQUALS((uint32)again[0], 0xAC00u);
#endif
	}
};
