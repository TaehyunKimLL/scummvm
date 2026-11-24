#include <cxxtest/TestSuite.h>

#include "agi/textmode.h"

// AGI's text mode (agi/textmode.h): upstream (kSingle) unless the target's
// language has a translation in the game folder.
class AgiTextModeTestSuite : public CxxTest::TestSuite {
	static Agi::TextModeInputs korean() {
		Agi::TextModeInputs in;
		in.detected = Common::EN_ANY;
		in.requested = Common::KO_KOR;
		in.translationFolder = true;
		in.stringTable = true;
		return in;
	}

public:
	void test_englishStaysUpstream() {
		Agi::TextModeInputs in;
		in.detected = Common::EN_ANY;
		Common::String notes;
		const Agi::TextMode m = Agi::decideTextMode(in, notes);
		TS_ASSERT(!m.utf8());
		TS_ASSERT(!m.hires);
		TS_ASSERT_EQUALS(m.language, Common::EN_ANY);
	}

	void test_koreanTranslationIsUtf8AndHires() {
		Common::String notes;
		const Agi::TextMode m = Agi::decideTextMode(korean(), notes);
		TS_ASSERT(m.utf8());
		TS_ASSERT(m.hires);
		TS_ASSERT_EQUALS(m.language, Common::KO_KOR);
		TS_ASSERT(notes.empty());
	}

	void test_languageWithoutTranslationChangesNothing() {
		Agi::TextModeInputs in = korean();
		in.translationFolder = false;
		in.stringTable = false;
		Common::String notes;
		const Agi::TextMode m = Agi::decideTextMode(in, notes);
		TS_ASSERT(!m.utf8());
		TS_ASSERT_EQUALS(m.language, Common::EN_ANY);
		TS_ASSERT(notes.contains("no translation"));
	}

	void test_folderWithoutTableIsStillUtf8() {
		Agi::TextModeInputs in = korean();
		in.stringTable = false;
		Common::String notes;
		TS_ASSERT(Agi::decideTextMode(in, notes).utf8());
		TS_ASSERT(notes.contains("no agi string table"));
	}

	void test_offSwitchesAndKeys() {
		Agi::TextModeInputs in = korean();
		in.koreanText = false;
		Common::String notes;
		Agi::TextMode m = Agi::decideTextMode(in, notes);
		TS_ASSERT(!m.utf8());
		TS_ASSERT_EQUALS(m.language, Common::EN_ANY);
		in = korean();
		in.hiresText = false;
		m = Agi::decideTextMode(in, notes);
		TS_ASSERT(m.utf8());
		TS_ASSERT(!m.hires);
		in = korean();
		in.textEncoding = "cp949";
		m = Agi::decideTextMode(in, notes);
		TS_ASSERT(m.utf8()); // cp949 is no AGI encoding: auto
		TS_ASSERT(notes.contains("not auto or utf8"));
		Agi::TextModeInputs en;
		en.detected = Common::EN_ANY;
		en.textEncoding = "utf8";
		TS_ASSERT(Agi::decideTextMode(en, notes).utf8());
	}
};
