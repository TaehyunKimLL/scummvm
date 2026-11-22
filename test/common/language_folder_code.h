#include <cxxtest/TestSuite.h>

#include "common/language.h"
#include "common/str.h"

class LanguageFolderCodeTestSuite : public CxxTest::TestSuite {
public:
	void test_folderCode() {
		TS_ASSERT_EQUALS(Common::String(Common::getLanguageFolderCode(Common::KO_KOR)), "ko");
		TS_ASSERT_EQUALS(Common::String(Common::getLanguageFolderCode(Common::ZH_TWN)), "zh");
		TS_ASSERT_EQUALS(Common::String(Common::getLanguageFolderCode(Common::PT_BRA)), "pt");
		TS_ASSERT_EQUALS(Common::String(Common::getLanguageFolderCode(Common::UNK_LANG)), "");
	}

	void test_parse() {
		TS_ASSERT_EQUALS(Common::parseLanguageFolderCode("KO"), Common::KO_KOR);
		TS_ASSERT_EQUALS(Common::parseLanguageFolderCode("en"), Common::EN_ANY);
		TS_ASSERT_EQUALS(Common::parseLanguageFolderCode("zh"), Common::ZH_ANY);
		TS_ASSERT_EQUALS(Common::parseLanguageFolderCode("kr"), Common::UNK_LANG);
		TS_ASSERT_EQUALS(Common::parseLanguageFolderCode(""), Common::UNK_LANG);
	}

	// Every language a game can be set to has a folder code.
	void test_everyLanguage() {
		for (int l = 0; Common::getLanguageCode((Common::Language)l); ++l)
			TSM_ASSERT(Common::getLanguageCode((Common::Language)l), *Common::getLanguageFolderCode((Common::Language)l));
	}
};
