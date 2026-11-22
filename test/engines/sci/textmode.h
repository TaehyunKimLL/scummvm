#include <cxxtest/TestSuite.h>

#include "sci/textmode.h"

class SciTextModeTestSuite : public CxxTest::TestSuite {
	static Sci::TextModeInputs koreanPack() {
		Sci::TextModeInputs in;
		in.detected = Common::EN_ANY;
		in.requested = Common::KO_KOR;
		in.translationFolder = true;
		in.stringTable = true;
		in.version = Sci::SCI_VERSION_01;
		return in;
	}

public:
	void test_koreanPackIsUtf8AndHires() {
		Common::String notes;
		const Sci::TextMode m = Sci::decideTextMode(koreanPack(), notes);
		TS_ASSERT_EQUALS(m.encoding, Sci::TextMode::kUtf8);
		TS_ASSERT(m.hires);
		TS_ASSERT_EQUALS(m.language, Common::KO_KOR);
		TS_ASSERT(notes.empty());
	}

	void test_englishGameUntouched() {
		Sci::TextModeInputs in = koreanPack();
		in.requested = Common::UNK_LANG;
		Common::String notes;
		const Sci::TextMode m = Sci::decideTextMode(in, notes);
		TS_ASSERT_EQUALS(m.encoding, Sci::TextMode::kSingle);
		TS_ASSERT(!m.hires);
		TS_ASSERT_EQUALS(m.language, Common::EN_ANY);
	}

	void test_languageWithoutTranslationFolder() {
		Sci::TextModeInputs in = koreanPack();
		in.translationFolder = false;
		in.stringTable = false;
		Common::String notes;
		const Sci::TextMode m = Sci::decideTextMode(in, notes);
		TS_ASSERT_EQUALS(m.language, Common::EN_ANY);
		TS_ASSERT_EQUALS(m.encoding, Sci::TextMode::kSingle);
		TS_ASSERT(notes.contains("no translation"));
	}

	void test_upstreamCp949PackKeepsItsPath() {
		Sci::TextModeInputs in;
		in.detected = Common::KO_KOR;
		in.version = Sci::SCI_VERSION_01;
		Common::String notes;
		const Sci::TextMode m = Sci::decideTextMode(in, notes);
		TS_ASSERT_EQUALS(m.encoding, Sci::TextMode::kCp949);
		TS_ASSERT(!m.hires);
		TS_ASSERT_EQUALS(m.language, Common::KO_KOR);
	}

	void test_overrides() {
		Sci::TextModeInputs in = koreanPack();
		Common::String notes;
		in.textEncoding = " CP949 ";
		TS_ASSERT_EQUALS(Sci::decideTextMode(in, notes).encoding, Sci::TextMode::kCp949);
		in.textEncoding = "latin1";
		TS_ASSERT_EQUALS(Sci::decideTextMode(in, notes).encoding, Sci::TextMode::kUtf8);
		TS_ASSERT(notes.contains("text_encoding=latin1"));
		in.textEncoding = "";
		in.hiresText = false;
		Sci::TextMode m = Sci::decideTextMode(in, notes);
		TS_ASSERT(m.utf8() && !m.hires);
		in.hiresText = true;
		in.koreanText = false;
		m = Sci::decideTextMode(in, notes);
		TS_ASSERT_EQUALS(m.encoding, Sci::TextMode::kSingle);
		TS_ASSERT_EQUALS(m.language, Common::EN_ANY);
	}

	void test_translationFolderWithoutTableIsUtf8() {
		// A ko/ folder with maps or another table spelling but no usable sci-ko.str is still a
		// translation: never silent CP949 of an English game.
		Sci::TextModeInputs in = koreanPack();
		in.stringTable = false;
		Common::String notes;
		const Sci::TextMode m = Sci::decideTextMode(in, notes);
		TS_ASSERT_EQUALS(m.encoding, Sci::TextMode::kUtf8);
		TS_ASSERT_EQUALS(m.language, Common::KO_KOR);
		TS_ASSERT(notes.contains("no sci-ko.str"));
		// A fan CP949 entry without a translation folder keeps its path.
		in.detected = Common::KO_KOR;
		in.requested = Common::UNK_LANG;
		in.translationFolder = false;
		TS_ASSERT_EQUALS(Sci::decideTextMode(in, notes).encoding, Sci::TextMode::kCp949);
	}

	void test_sci32HasNoHiresText() {
		Sci::TextModeInputs in = koreanPack();
		in.version = Sci::SCI_VERSION_2;
		Common::String notes;
		const Sci::TextMode m = Sci::decideTextMode(in, notes);
		TS_ASSERT(m.utf8() && !m.hires);
	}
};
