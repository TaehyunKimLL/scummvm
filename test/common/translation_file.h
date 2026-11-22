#include <cxxtest/TestSuite.h>

#include "common/translation_file.h"

#include "test/system/null_osystem.h"

// The fingerprint header line of the translation files (DESIGN.md section 4, decision 4).
class TranslationFileHeaderTestSuite : public CxxTest::TestSuite {
	static const char *kq1() {
		return "#! format=1 game=kq1 fingerprint=0123456789abcdef0123456789abcdef engine=sci";
	}

public:
	void setUp() override {
		Common::install_null_g_system();
	}

	void tearDown() override {
		Common::uninstall_null_g_system();
	}

	void test_parse() {
		Common::TranslationFileHeader h;
		Common::String error;
		TS_ASSERT(Common::TranslationFileHeader::parse(kq1(), h, error));
		TS_ASSERT(error.empty());
		TS_ASSERT(h.present());
		TS_ASSERT_EQUALS(h.format, 1);
		TS_ASSERT_EQUALS(h.game, "kq1");
		TS_ASSERT_EQUALS(h.fingerprint, "0123456789abcdef0123456789abcdef");
		TS_ASSERT_EQUALS(h.engine, "sci");
		TS_ASSERT(h.vocab.empty());
		TS_ASSERT_EQUALS(h.toString(), kq1());
	}

	// Any order, tabs, upper-case md5, a CR line end, and a key of a later format.
	void test_parseIsLenient() {
		Common::TranslationFileHeader h;
		Common::String error;
		TS_ASSERT(Common::TranslationFileHeader::parse(
			"#!\tengine=agi  vocab=FEDCBA9876543210FEDCBA9876543210 future=x game=lsl1 "
			"fingerprint=0123456789ABCDEF0123456789ABCDEF format=1\r", h, error));
		TS_ASSERT_EQUALS(h.toString(), "#! format=1 game=lsl1 fingerprint=0123456789abcdef0123456789abcdef engine=agi "
		                               "vocab=fedcba9876543210fedcba9876543210");
	}

	void test_notAHeader() {
		Common::TranslationFileHeader h;
		Common::String error = "x";
		TS_ASSERT(!Common::TranslationFileHeader::parse("# script strings: script<TAB>id<TAB>text", h, error));
		TS_ASSERT(error.empty());
		TS_ASSERT(!h.present());
		TS_ASSERT(!Common::TranslationFileHeader::parse("0\t5\tx", h, error));
		TS_ASSERT(error.empty());
	}

	void test_badHeaders() {
		const char *bad[] = {
			"#! game=kq1 fingerprint=0123456789abcdef0123456789abcdef engine=sci",          // no format
			"#! format=2 game=kq1 fingerprint=0123456789abcdef0123456789abcdef engine=sci", // newer format
			"#! format=x game=kq1 fingerprint=0123456789abcdef0123456789abcdef engine=sci",
			"#! format=1 fingerprint=0123456789abcdef0123456789abcdef engine=sci",           // no game
			"#! format=1 game=kq1 fingerprint=0123 engine=sci",                               // short md5
			"#! format=1 game=kq1 fingerprint=0123456789abcdef0123456789abcdeg engine=sci",  // not hex
			"#! format=1 game=kq1 fingerprint=0123456789abcdef0123456789abcdef",             // no engine
			"#! format=1 game=kq1 game=kq2 fingerprint=0123456789abcdef0123456789abcdef engine=sci",
			"#! format=1 game=kq1 fingerprint=0123456789abcdef0123456789abcdef engine=sci vocab=1",
			"#! format=1 game=kq1 fingerprint=0123456789abcdef0123456789abcdef engine=sci junk",
			"#! format=1 game=k/q fingerprint=0123456789abcdef0123456789abcdef engine=sci",
		};
		for (uint i = 0; i < ARRAYSIZE(bad); ++i) {
			Common::TranslationFileHeader h;
			Common::String error;
			TSM_ASSERT(bad[i], !Common::TranslationFileHeader::parse(bad[i], h, error));
			TSM_ASSERT(bad[i], !error.empty());
			TSM_ASSERT(bad[i], !h.present());
		}
	}

	void test_check() {
		Common::TranslationFileHeader found, expected, none;
		Common::String error;
		TS_ASSERT(Common::TranslationFileHeader::parse(kq1(), found, error));
		TS_ASSERT_EQUALS(Common::checkTranslationHeader(found, none, "a"), Common::kTranslationMatches);
		expected.game = "kq1";
		expected.engine = "sci";
		expected.fingerprint = "0123456789ABCDEF0123456789ABCDEF";
		TS_ASSERT_EQUALS(Common::checkTranslationHeader(found, expected, "a"), Common::kTranslationMatches);
		expected.fingerprint = "00000000000000000000000000000000";
		TS_ASSERT_EQUALS(Common::checkTranslationHeader(found, expected, "a"), Common::kTranslationMismatch);
		expected.fingerprint.clear();
		expected.game = "kq4";
		TS_ASSERT_EQUALS(Common::checkTranslationHeader(found, expected, "a"), Common::kTranslationMismatch);
		expected.game.clear();
		expected.vocab = "00000000000000000000000000000000"; // the file names no vocab
		TS_ASSERT_EQUALS(Common::checkTranslationHeader(found, expected, "a"), Common::kTranslationMismatch);
		TS_ASSERT_EQUALS(Common::checkTranslationHeader(none, expected, "a"), Common::kTranslationUnchecked);
	}

	void test_isMd5String() {
		TS_ASSERT(Common::isMd5String("0123456789abcdefABCDEF0123456789"));
		TS_ASSERT(!Common::isMd5String(""));
		TS_ASSERT(!Common::isMd5String("0123456789abcdef0123456789abcde"));
		TS_ASSERT(!Common::isMd5String("0123456789abcdef0123456789abcdefg"));
	}
};
