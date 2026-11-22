#include <cxxtest/TestSuite.h>

#include "common/fs.h"
#include "common/language_folder.h"

#include "test/system/null_osystem.h"

// translationFolderLanguages() over the fixture game folder test/common/language_folder_game/:
//   ko/sci-ko.str  JA/HIRESVF.MAP  zh/Sci-Zh.Str  de/readme.txt  patches/sci-ko.str
//   sci-fr.str  SCI-KO.STR  sci-xx.str  agi-it.str  it.trs
class LanguageFolderTestSuite : public CxxTest::TestSuite {
	static Common::FSList game() {
		Common::FSList files;
		const Common::FSNode dir(Common::Path(__FILE__, '/').getParent().join("language_folder_game"));
		TS_ASSERT(dir.getChildren(files, Common::FSNode::kListAll));
		return files;
	}

	static Common::String codes(const Common::Array<Common::Language> &langs) {
		Common::String s;
		for (uint i = 0; i < langs.size(); ++i)
			s += Common::String(i ? " " : "") + Common::getLanguageFolderCode(langs[i]);
		return s;
	}

public:
	void setUp() override {
		Common::install_null_g_system();
	}

	void tearDown() override {
		Common::uninstall_null_g_system();
	}

	void test_tableName() {
		TS_ASSERT_EQUALS(Common::translationTableName("sci-%s.str", Common::KO_KOR), "sci-ko.str");
		TS_ASSERT_EQUALS(Common::translationTableName("%s.trs", Common::ZH_TWN), "zh.trs");
		TS_ASSERT_EQUALS(Common::translationTableName("sci-%s.str", Common::UNK_LANG), "");
		TS_ASSERT_EQUALS(Common::translationTableName("sci.str", Common::KO_KOR), "");
		TS_ASSERT_EQUALS(Common::translationTableName("%s-%s.str", Common::KO_KOR), "");
		TS_ASSERT_EQUALS(Common::translationTableName("%d-%s.str", Common::KO_KOR), "");
		TS_ASSERT_EQUALS(Common::translationTableName(nullptr, Common::KO_KOR), "");
	}

	// SCI: ko/ by its table, JA/ by its map, zh/ by its table in another case, fr by the loose file; de/ holds
	// neither, patches/ is no language, sci-xx.str no code, SCI-KO.STR the same language as ko/.
	void test_sci() {
		const Common::Array<Common::Language> l = Common::translationFolderLanguages(game(), "sci-%s.str");
		TS_ASSERT_EQUALS(codes(l), "fr ja ko zh");
		TS_ASSERT_EQUALS(l.size(), 4u);
		if (l.size() == 4) {
			TS_ASSERT_EQUALS(l[0], Common::FR_FRA);
			TS_ASSERT_EQUALS(l[2], Common::KO_KOR);
			TS_ASSERT_EQUALS(l[3], Common::ZH_ANY);
		}
	}

	void test_otherPatterns() {
		TS_ASSERT_EQUALS(codes(Common::translationFolderLanguages(game(), "agi-%s.str")), "it ja");
		TS_ASSERT_EQUALS(codes(Common::translationFolderLanguages(game(), "%s.trs")), "it ja");
		TS_ASSERT_EQUALS(codes(Common::translationFolderLanguages(game(), "sci.str")), "");
		TS_ASSERT_EQUALS(codes(Common::translationFolderLanguages(Common::FSList(), "sci-%s.str")), "");
	}
};
