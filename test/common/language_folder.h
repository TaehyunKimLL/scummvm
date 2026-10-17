#include <cxxtest/TestSuite.h>

#include "common/language.h"
#include "common/str.h"

/**
 * The language folder codes: the ISO 639-1 code a translation's folder and
 * files are named by (ko/, sci-ko.str), one per language whatever its
 * dialect, and back.
 */
class LanguageFolderTestSuite : public CxxTest::TestSuite {
public:
	void test_every_language_has_a_two_letter_code() {
		for (const Common::LanguageDescription *l = Common::g_languages; l->code; ++l) {
			const Common::String code = Common::getLanguageFolderCode(l->id);
			TS_ASSERT_EQUALS(code.size(), 2u);
			for (uint i = 0; i < code.size(); ++i)
				TS_ASSERT(code[i] >= 'a' && code[i] <= 'z');
			// The code is the ISO 639-1 part of the language's POSIX locale.
			TS_ASSERT(Common::String(l->unixLocale).hasPrefix(code));
			// And names a language of the same code, the generic one.
			const Common::Language back = Common::parseLanguageFolderCode(code);
			TS_ASSERT_DIFFERS(back, Common::UNK_LANG);
			TS_ASSERT_EQUALS(Common::String(Common::getLanguageFolderCode(back)), code);
		}
	}

	void test_the_translation_languages_keep_their_names() {
		// ko / ja / en: as getLanguageCode() gave them before.
		TS_ASSERT_EQUALS(Common::String(Common::getLanguageFolderCode(Common::KO_KOR)), "ko");
		TS_ASSERT_EQUALS(Common::String(Common::getLanguageFolderCode(Common::JA_JPN)), "ja");
		TS_ASSERT_EQUALS(Common::String(Common::getLanguageFolderCode(Common::EN_ANY)), "en");
		TS_ASSERT_EQUALS(Common::String(Common::getLanguageCode(Common::KO_KOR)), "ko");
		TS_ASSERT_EQUALS(Common::String(Common::getLanguageCode(Common::JA_JPN)), "ja");
		TS_ASSERT_EQUALS(Common::String(Common::getLanguageCode(Common::EN_ANY)), "en");
	}

	void test_dialects_share_their_language_code() {
		TS_ASSERT_EQUALS(Common::String(Common::getLanguageFolderCode(Common::EN_GRB)), "en");
		TS_ASSERT_EQUALS(Common::String(Common::getLanguageFolderCode(Common::EN_USA)), "en");
		TS_ASSERT_EQUALS(Common::String(Common::getLanguageFolderCode(Common::ZH_CHN)), "zh");
		TS_ASSERT_EQUALS(Common::String(Common::getLanguageFolderCode(Common::ZH_TWN)), "zh");
		TS_ASSERT_EQUALS(Common::String(Common::getLanguageFolderCode(Common::PT_BRA)), "pt");
		TS_ASSERT_EQUALS(Common::String(Common::getLanguageFolderCode(Common::FR_CAN)), "fr");
		TS_ASSERT_EQUALS(Common::String(Common::getLanguageFolderCode(Common::NL_BEL)), "nl");
		TS_ASSERT_EQUALS(Common::String(Common::getLanguageFolderCode(Common::UA_UKR)), "uk");
	}

	void test_parse() {
		TS_ASSERT_EQUALS(Common::parseLanguageFolderCode("ko"), Common::KO_KOR);
		TS_ASSERT_EQUALS(Common::parseLanguageFolderCode("KO"), Common::KO_KOR);
		TS_ASSERT_EQUALS(Common::parseLanguageFolderCode("ja"), Common::JA_JPN);
		TS_ASSERT_EQUALS(Common::parseLanguageFolderCode("en"), Common::EN_ANY);
		TS_ASSERT_EQUALS(Common::parseLanguageFolderCode("zh"), Common::ZH_ANY);
		TS_ASSERT_EQUALS(Common::parseLanguageFolderCode("pt"), Common::PT_PRT);
		// Configuration codes that are not ISO 639-1 name no folder.
		TS_ASSERT_EQUALS(Common::parseLanguageFolderCode("cn"), Common::UNK_LANG);
		TS_ASSERT_EQUALS(Common::parseLanguageFolderCode("gb"), Common::UNK_LANG);
		TS_ASSERT_EQUALS(Common::parseLanguageFolderCode("kr"), Common::UNK_LANG);
		TS_ASSERT_EQUALS(Common::parseLanguageFolderCode(""), Common::UNK_LANG);
		TS_ASSERT_EQUALS(Common::parseLanguageFolderCode("patches"), Common::UNK_LANG);
	}

	void test_unknown_is_empty() {
		TS_ASSERT_EQUALS(Common::String(Common::getLanguageFolderCode(Common::UNK_LANG)), "");
	}
};
