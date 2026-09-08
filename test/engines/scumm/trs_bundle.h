#include <cxxtest/TestSuite.h>

#include "engines/scumm/trs_bundle.h"

/**
 * The .trs bundle naming rule, in both directions.
 *
 * The file name is the only thing that says which language a .trs bundle
 * carries - the format itself is language-neutral. Detection scans a folder
 * for a bundle and the engine opens the one its language names, so the two
 * must agree. They share getTrsBundleName()/getTrsBundleLanguage(), and these
 * tests pin the round trip: whatever the detector recognises, the engine must
 * ask for, or a game is detected as translated and then renders untranslated.
 *
 * The Korean case is the one that cannot be derived from the language code:
 * every existing fan translation ships "korean.trs", not "ko.trs", so the
 * special case has to survive any future tidying of this rule.
 */
class TrsBundleTestSuite : public CxxTest::TestSuite {
public:
	void test_korean_keeps_its_established_name() {
		// Existing translations ship this name. "ko.trs" would break them all.
		TS_ASSERT_EQUALS(Scumm::getTrsBundleName(Common::KO_KOR).toString('/'),
		                 Common::String("korean.trs"));
		TS_ASSERT_EQUALS(Scumm::getTrsBundleLanguage("korean.trs"), Common::KO_KOR);
	}

	void test_other_languages_use_their_language_code() {
		TS_ASSERT_EQUALS(Scumm::getTrsBundleName(Common::DE_DEU).toString('/'),
		                 Common::String("de.trs"));
		TS_ASSERT_EQUALS(Scumm::getTrsBundleName(Common::FR_FRA).toString('/'),
		                 Common::String("fr.trs"));
		// A code that is not two letters must still work.
		TS_ASSERT_EQUALS(Scumm::getTrsBundleName(Common::PT_BRA).toString('/'),
		                 Common::String("br.trs"));
		TS_ASSERT_EQUALS(Scumm::getTrsBundleName(Common::FR_CAN).toString('/'),
		                 Common::String("fr-ca.trs"));
	}

	void test_unknown_language_names_no_bundle() {
		// Not "unk.trs" or ".trs" - a game with no language must ask for
		// nothing, or it would open whatever a stray file happened to be
		// called.
		TS_ASSERT(Scumm::getTrsBundleName(Common::UNK_LANG).empty());
	}

	void test_names_that_are_not_bundles_are_declined() {
		TS_ASSERT_EQUALS(Scumm::getTrsBundleLanguage("monkey2.000"), Common::UNK_LANG);
		TS_ASSERT_EQUALS(Scumm::getTrsBundleLanguage("de.trs.bak"), Common::UNK_LANG);
		TS_ASSERT_EQUALS(Scumm::getTrsBundleLanguage(""), Common::UNK_LANG);
		TS_ASSERT_EQUALS(Scumm::getTrsBundleLanguage(".trs"), Common::UNK_LANG);
		// A .trs that is not a language bundle - Rebel Assault 2 ships
		// SYSTM/GAME.TRS and is identified by size, not by name.
		TS_ASSERT_EQUALS(Scumm::getTrsBundleLanguage("GAME.TRS"), Common::UNK_LANG);
	}

	void test_extension_case_does_not_matter() {
		// Game folders come off CDs and case-insensitive filesystems.
		TS_ASSERT_EQUALS(Scumm::getTrsBundleLanguage("KOREAN.TRS"), Common::KO_KOR);
		TS_ASSERT_EQUALS(Scumm::getTrsBundleLanguage("DE.TRS"), Common::DE_DEU);
		TS_ASSERT_EQUALS(Scumm::getTrsBundleLanguage("De.Trs"), Common::DE_DEU);
	}

	void test_round_trip_over_every_language() {
		// The property that matters: a bundle the engine would open is a
		// bundle the detector recognises, for every language ScummVM knows.
		// A language whose code collides with "korean" would break this.
		for (const Common::LanguageDescription *l = Common::g_languages; l->code; ++l) {
			Common::Path name = Scumm::getTrsBundleName(l->id);
			TS_ASSERT(!name.empty());
			TS_ASSERT_EQUALS(Scumm::getTrsBundleLanguage(name.toString('/')), l->id);
		}
	}
};
