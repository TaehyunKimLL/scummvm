#include <cxxtest/TestSuite.h>

#include "common/str.h"
#include "common/array.h"
#include "common/fs.h"
#include "common/stream.h"
#include "common/language.h"
#include "../../system/null_osystem.h"

#include "engines/sci/parser/lowercase.h"

/**
 * What the parser's lower-case fold does to double-byte text.
 *
 * Vocabulary::tokenizeString() folds the player's input through a 256-entry
 * table before looking it up in the game's dictionary. The table is a
 * statement about single-byte text, and one of its entries - 0xA5 -> 0xA4 -
 * is also a legal EUC-KR trail byte. Applied to the trail byte of a Korean
 * syllable it produces a different, equally legal syllable, so the lookup
 * searches for a word the player did not type and reports it as unknown.
 *
 * The damaged syllables are DERIVED here from the table in the engine source
 * rather than listed: a list pasted into a test goes stale silently when the
 * table changes, and this one would then pass while measuring nothing. The
 * count is asserted to be non-zero for the same reason - a fold that stopped
 * damaging anything would make every other assertion here vacuous, and that
 * is a change to the table which should be seen, not absorbed.
 */

class SciParserLowerCaseTestSuite : public CxxTest::TestSuite {

	// -------------------------------------------------------------- source

	/**
	 * A file from the tree this runner was built from. Fails rather than
	 * returning empty: a table parsed out of no source is 256 zeroes, and
	 * every test below would pass against it.
	 */
	Common::String readSource(const char *rel) {
		Common::String path = Common::String(SCI_TEST_SRCDIR) + "/" + rel;
		Common::FSNode node(Common::Path(path, '/'));
		Common::SeekableReadStream *in = node.createReadStream();
		if (!in) {
			TS_FAIL(("cannot read " + path).c_str());
			return Common::String();
		}
		uint32 len = (uint32)in->size();
		char *buf = new char[len + 1];
		uint32 got = in->read(buf, len);
		buf[got] = 0;
		Common::String out(buf, got);
		delete[] buf;
		delete in;
		return out;
	}

	/**
	 * Parse `static const byte <name>[256] = { ... };` out of the engine.
	 *
	 * Accepts the two forms the table is written in: 0xNN and 'c'. Anything
	 * else fails the test rather than being skipped, because a skipped entry
	 * would shift every value after it.
	 */
	static bool isHex(char c) {
		return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
		       (c >= 'A' && c <= 'F');
	}

	void parseTable(const Common::String &src, const char *name, byte out[256]) {
		Common::String decl = Common::String("const byte ") + name + "[256] = {";
		const char *start = strstr(src.c_str(), decl.c_str());
		TSM_ASSERT(("table not found: " + decl).c_str(), start != nullptr);
		if (!start)
			return;
		start += decl.size();
		const char *end = strstr(start, "};");
		TSM_ASSERT("unterminated table", end != nullptr);
		if (!end)
			return;

		int n = 0;
		for (const char *p = start; p < end; ) {
			// Skip whitespace, commas and // comments.
			if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',') {
				p++;
				continue;
			}
			if (p[0] == '/' && p[1] == '/') {
				while (p < end && *p != '\n')
					p++;
				continue;
			}
			if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
				int v = 0;
				p += 2;
				while (p < end && isHex(*p)) {
					char c = *p++;
					v = v * 16 + (c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
				}
				TSM_ASSERT("more than 256 entries", n < 256);
				if (n < 256)
					out[n++] = (byte)v;
				continue;
			}
			if (p[0] == '\'' && p[2] == '\'') {
				TSM_ASSERT("more than 256 entries", n < 256);
				if (n < 256)
					out[n++] = (byte)p[1];
				p += 3;
				continue;
			}
			TS_FAIL(Common::String::format(
				"unparsed token in %s at '%.10s'", name, p).c_str());
			return;
		}
		TS_ASSERT_EQUALS(n, 256);
	}

	// ------------------------------------------------------------ EUC-KR

	/**
	 * The EUC-KR Hangul syllable block: lead 0xB0..0xC8, trail 0xA1..0xFE.
	 *
	 * These are the rows Graphics::checkKorCode() accepts, the rows
	 * GfxText16::SwitchToFont1001OnKorean() sniffs for, and the only rows
	 * FontKoreanWansung has glyphs for - 25 x 94 = 2350 syllables, all of
	 * them valid code points.
	 */
	static const byte kKorLeadLo = 0xB0, kKorLeadHi = 0xC8;
	static const byte kKorTrailLo = 0xA1, kKorTrailHi = 0xFE;

	struct Pair {
		byte lead, trail;
	};

	/**
	 * Every Hangul syllable the table would alter if it were applied byte by
	 * byte, i.e. every syllable the defect corrupts.
	 */
	void damagedSyllables(const byte lc[256], Common::Array<Pair> &out) {
		for (int lead = kKorLeadLo; lead <= kKorLeadHi; lead++) {
			for (int trail = kKorTrailLo; trail <= kKorTrailHi; trail++) {
				if (lc[lead] != lead || lc[trail] != trail) {
					Pair p = { (byte)lead, (byte)trail };
					out.push_back(p);
				}
			}
		}
	}

public:

	void setUp() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::install_null_g_system();
#endif
	}

	void tearDown() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::uninstall_null_g_system();
#endif
	}

	/**
	 * The fix, against the whole population it protects.
	 *
	 * Every EUC-KR Hangul syllable survives the fold byte for byte. Running
	 * all 2350 rather than only the damaged ones costs nothing and catches a
	 * fold that starts damaging a syllable the current table leaves alone.
	 */
	void test_every_hangul_syllable_survives_the_fold() {
		int checked = 0;
		for (int lead = kKorLeadLo; lead <= kKorLeadHi; lead++) {
			for (int trail = kKorTrailLo; trail <= kKorTrailHi; trail++) {
				byte word[2] = { (byte)lead, (byte)trail };
				Sci::parserLowerCaseWord(word, 2, Common::KO_KOR);
				if (word[0] != lead || word[1] != trail) {
					TS_FAIL(Common::String::format(
						"EUC-KR %02X%02X folded to %02X%02X",
						lead, trail, word[0], word[1]).c_str());
					return;
				}
				checked++;
			}
		}
		TS_ASSERT_EQUALS(checked, 2350);
	}

	/**
	 * The defect is real, and the list above is not empty.
	 *
	 * Derived from the table in the source: if nothing in lowerCaseMap
	 * collides with an EUC-KR byte any more, the test above proves nothing
	 * and this one says so.
	 */
	void test_the_table_still_collides_with_euckr() {
#if !NULL_OSYSTEM_IS_AVAILABLE
		TS_SKIP("no file access in this test environment");
#else
		byte lc[256];
		parseTable(readSource("engines/sci/parser/lowercase.cpp"),
		           "lowerCaseMap", lc);

		Common::Array<Pair> damaged;
		damagedSyllables(lc, damaged);

		TSM_ASSERT("lowerCaseMap no longer alters any EUC-KR syllable - "
		           "the round-trip test above has nothing left to prove",
		           !damaged.empty());

		// And each one is exactly the case the fix has to handle: a lead
		// byte the table leaves alone followed by a trail byte it folds.
		for (uint i = 0; i < damaged.size(); i++) {
			byte word[2] = { damaged[i].lead, damaged[i].trail };
			Sci::parserLowerCaseWord(word, 2, Common::KO_KOR);
			TS_ASSERT_EQUALS(word[0], damaged[i].lead);
			TS_ASSERT_EQUALS(word[1], damaged[i].trail);
		}
#endif
	}

	/**
	 * A Korean word is not a special case of an English one.
	 *
	 * A syllable inside a longer word, and one that follows ASCII, must be
	 * found by walking the word from its start - a check that only looks at
	 * the byte before would mistake a fold-target trail byte for a lead.
	 */
	void test_a_syllable_inside_a_word_is_kept_intact() {
		// "AB" + B0A5 + "CD" - the ASCII folds, the syllable does not.
		byte word[] = { 'A', 'B', 0xB0, 0xA5, 'C', 'D' };
		Sci::parserLowerCaseWord(word, sizeof(word), Common::KO_KOR);
		TS_ASSERT_EQUALS(word[0], 'a');
		TS_ASSERT_EQUALS(word[1], 'b');
		TS_ASSERT_EQUALS(word[2], 0xB0);
		TS_ASSERT_EQUALS(word[3], 0xA5);
		TS_ASSERT_EQUALS(word[4], 'c');
		TS_ASSERT_EQUALS(word[5], 'd');
	}

	/**
	 * Two syllables in a row: the second lead must be recognised as a lead,
	 * which only holds if the walk consumed the first pair as a unit.
	 */
	void test_consecutive_syllables_stay_aligned() {
		byte word[] = { 0xB0, 0xA5, 0xB8, 0xA5 };
		Sci::parserLowerCaseWord(word, sizeof(word), Common::KO_KOR);
		TS_ASSERT_EQUALS(word[0], 0xB0);
		TS_ASSERT_EQUALS(word[1], 0xA5);
		TS_ASSERT_EQUALS(word[2], 0xB8);
		TS_ASSERT_EQUALS(word[3], 0xA5);
	}

	/**
	 * A truncated syllable at the end of the word does not read past it.
	 *
	 * The text control inserts one byte per keypress, so a half-typed
	 * syllable reaches the tokenizer routinely.
	 */
	void test_a_trailing_lead_byte_is_not_a_pair() {
		byte word[] = { 'a', 0xB0 };
		Sci::parserLowerCaseWord(word, sizeof(word), Common::KO_KOR);
		TS_ASSERT_EQUALS(word[0], 'a');
		// Folded as a single byte, because there is no second byte. 0xB0 is
		// not remapped by the table, so it is unchanged either way; what
		// matters is that the call stayed inside the buffer.
		TS_ASSERT_EQUALS(word[1], 0xB0);
	}

	/**
	 * Korean ASCII still lower-cases.
	 *
	 * The dictionary of a Korean SCI game is 7-bit ASCII - vocab.000 masks
	 * every stored byte with 0x7F - so an English word typed into a Korean
	 * game must fold exactly as it does in an English one, or nothing
	 * matches at all.
	 */
	void test_ascii_still_folds_in_a_korean_game() {
		byte word[] = { 'O', 'P', 'E', 'N', ' ', 'D', 'O', 'O', 'R' };
		byte control[] = { 'O', 'P', 'E', 'N', ' ', 'D', 'O', 'O', 'R' };
		Sci::parserLowerCaseWord(word, sizeof(word), Common::KO_KOR);
		Sci::parserLowerCaseWord(control, sizeof(control), Common::EN_ANY);
		TS_ASSERT_SAME_DATA(word, control, sizeof(word));
		TS_ASSERT_EQUALS(word[0], 'o');
		TS_ASSERT_EQUALS(word[8], 'r');
	}

	// ----------------------------------------------------- other languages

	/**
	 * Every byte value folds exactly as the old code did, for every
	 * single-byte language.
	 *
	 * This is the no-change claim for English, German, French, Spanish,
	 * Italian, Polish, Portuguese and Hebrew, stated as a measurement over
	 * all 256 values rather than as an argument: the fold is the table
	 * lookup the tokenizer used to do inline, for every byte, unchanged.
	 */
	void test_single_byte_languages_fold_exactly_as_the_table_says() {
#if !NULL_OSYSTEM_IS_AVAILABLE
		TS_SKIP("no file access in this test environment");
#else
		byte lc[256];
		parseTable(readSource("engines/sci/parser/lowercase.cpp"),
		           "lowerCaseMap", lc);

		static const Common::Language kSingleByte[] = {
			Common::EN_ANY, Common::DE_DEU, Common::FR_FRA, Common::ES_ESP,
			Common::IT_ITA, Common::PL_POL, Common::PT_BRA, Common::HE_ISR,
			Common::JA_JPN, Common::UNK_LANG
		};

		for (uint l = 0; l < ARRAYSIZE(kSingleByte); l++) {
			for (int b = 0; b < 256; b++) {
				byte word[1] = { (byte)b };
				Sci::parserLowerCaseWord(word, 1, kSingleByte[l]);
				if (word[0] != lc[b]) {
					TS_FAIL(Common::String::format(
						"language %d: %02X folded to %02X, table says %02X",
						(int)kSingleByte[l], b, word[0], lc[b]).c_str());
					return;
				}
			}
		}
#endif
	}

	/**
	 * Russian still gets its own table, over the whole byte range.
	 *
	 * The Russian fan translations put their vocabulary in the high half of
	 * CP866; picking the wrong table there breaks every word.
	 */
	void test_russian_still_uses_its_own_table() {
#if !NULL_OSYSTEM_IS_AVAILABLE
		TS_SKIP("no file access in this test environment");
#else
		Common::String src = readSource("engines/sci/parser/lowercase.cpp");
		byte lc[256], lc866[256];
		parseTable(src, "lowerCaseMap", lc);
		parseTable(src, "lowerCaseMap866", lc866);

		int differing = 0;
		for (int b = 0; b < 256; b++) {
			byte word[1] = { (byte)b };
			Sci::parserLowerCaseWord(word, 1, Common::RU_RUS);
			if (word[0] != lc866[b]) {
				TS_FAIL(Common::String::format(
					"RU %02X folded to %02X, CP866 table says %02X",
					b, word[0], lc866[b]).c_str());
				return;
			}
			if (lc866[b] != lc[b])
				differing++;
		}
		// The two tables really are different, so the assertion above is not
		// satisfied by accident.
		TSM_ASSERT("lowerCaseMap866 is identical to lowerCaseMap", differing > 0);
#endif
	}

	/**
	 * The fix cannot reach a single-byte language, over every language
	 * ScummVM has.
	 *
	 * The card's no-change claim for the Korean SCI1 titles rests on
	 * this: the only behaviour change is the double-byte skip, and the skip
	 * is unreachable unless parserLanguageIsDoubleByte() says so. Walking
	 * the whole Common::Language enum makes that an enumeration rather than
	 * an argument - a language added to ScummVM later is covered without
	 * anyone remembering to extend this.
	 */
	void test_no_language_but_korean_takes_the_double_byte_path() {
		int doubleByteLanguages = 0;
		for (int l = -1; l <= (int)Common::ZH_TWN; l++) {
			Common::Language lang = (Common::Language)l;
			if (Sci::parserLanguageIsDoubleByte(lang)) {
				doubleByteLanguages++;
				TS_ASSERT_EQUALS((int)lang, (int)Common::KO_KOR);
				continue;
			}
			// Not double-byte, so no byte value may be treated as a lead.
			for (int b = 0; b < 256; b++) {
				if (Sci::parserIsLeadByte((byte)b, lang)) {
					TS_FAIL(Common::String::format(
						"language %d treats %02X as a lead byte", l, b).c_str());
					return;
				}
			}
		}
		TS_ASSERT_EQUALS(doubleByteLanguages, 1);
	}

	/**
	 * A word with no lead byte folds the same in Korean as anywhere else.
	 *
	 * The Korean SCI1 titles have 7-bit ASCII dictionaries - vocab.000
	 * masks every stored byte with 0x7F - so the words they look up are the
	 * ones this exercises. Their behaviour is unchanged whether or not the
	 * skip exists, which is why the fix is safe for games nobody here can
	 * run.
	 */
	void test_a_korean_game_folds_ascii_words_identically() {
		for (int b = 0; b < 0xA1; b++) {
			byte ko[1] = { (byte)b };
			byte en[1] = { (byte)b };
			Sci::parserLowerCaseWord(ko, 1, Common::KO_KOR);
			Sci::parserLowerCaseWord(en, 1, Common::EN_ANY);
			if (ko[0] != en[0]) {
				TS_FAIL(Common::String::format(
					"%02X folds to %02X in Korean but %02X in English",
					b, ko[0], en[0]).c_str());
				return;
			}
		}
	}

	/**
	 * Which SCI languages are double-byte, as a decision rather than an
	 * oversight.
	 *
	 * Every language named in detection_tables.h is listed here with a
	 * reason. A language added to the detection table later fails this test
	 * until someone records what its parser input is encoded in - which is
	 * how the Korean case stayed broken for as long as it did.
	 */
	void test_every_detected_language_has_a_recorded_encoding() {
#if !NULL_OSYSTEM_IS_AVAILABLE
		TS_SKIP("no file access in this test environment");
#else
		static const struct {
			Common::Language lang;
			const char *name;
			bool doubleByte;
			const char *why;
		} kEncoding[] = {
			{ Common::EN_ANY, "EN_ANY", false, "ASCII" },
			{ Common::DE_DEU, "DE_DEU", false, "DOS code page" },
			{ Common::FR_FRA, "FR_FRA", false, "DOS code page" },
			{ Common::ES_ESP, "ES_ESP", false, "DOS code page" },
			{ Common::IT_ITA, "IT_ITA", false, "DOS code page" },
			{ Common::PL_POL, "PL_POL", false, "DOS code page" },
			{ Common::PT_BRA, "PT_BRA", false, "DOS code page" },
			{ Common::RU_RUS, "RU_RUS", false, "CP866, single byte" },
			{ Common::HE_ISR, "HE_ISR", false, "CP862, single byte" },
			{ Common::KO_KOR, "KO_KOR", true,  "EUC-KR" },
			// Japanese text IS Shift-JIS and the same table damages it -
			// about a third of the plane, mostly through the A-Z fold
			// hitting trail bytes 0x41..0x5A. It is left alone here because
			// no Japanese SCI game was available to check the change
			// against, and because SCI's Japanese input goes through
			// checkAltInput() (vocabulary.cpp), a romaji substitution step
			// this fold runs after. Fixing it is a separate card with a
			// separate measurement.
			{ Common::JA_JPN, "JA_JPN", false, "Shift-JIS; unfixed, see above" },
		};

		Common::String tables = readSource("engines/sci/detection_tables.h");
		TSM_ASSERT("detection_tables.h read as empty", tables.size() > 1000);

		// How many Korean rows go through this fold. The number is counted
		// here rather than quoted, because a count in prose goes stale the
		// next time a fan translation is added to the table.
		int koreanRows = 0;
		{
			uint pos = 0;
			while (true) {
				const char *p = strstr(tables.c_str() + pos, "Common::KO_KOR");
				if (!p)
					break;
				pos = (uint)(p - tables.c_str()) + 14;
				koreanRows++;
			}
		}
		TSM_ASSERT("no Korean rows found - the fold reaches no game",
		           koreanRows > 0);

		int found = 0, unaccounted = 0;
		for (uint i = 0; i < ARRAYSIZE(kEncoding); i++) {
			Common::String needle =
				Common::String("Common::") + kEncoding[i].name;
			if (tables.contains(needle.c_str()))
				found++;
			TS_ASSERT_EQUALS(
				Sci::parserLanguageIsDoubleByte(kEncoding[i].lang),
				kEncoding[i].doubleByte);
		}
		TS_ASSERT_EQUALS(found, (int)ARRAYSIZE(kEncoding));

		// The other direction: a language in the detection table that this
		// list does not name.
		uint pos = 0;
		while (true) {
			const char *p = strstr(tables.c_str() + pos, "Common::");
			if (!p)
				break;
			pos = (uint)(p - tables.c_str()) + 8;
			// A language constant is XX_YYY: two upper-case letters, an
			// underscore, then upper-case letters.
			const char *q = tables.c_str() + pos;
			if (!(q[0] >= 'A' && q[0] <= 'Z' && q[1] >= 'A' && q[1] <= 'Z' &&
			      q[2] == '_'))
				continue;
			uint e = 3;
			while (q[e] >= 'A' && q[e] <= 'Z')
				e++;
			if (e < 5 || q[e] == '_')
				continue;
			Common::String name(q, e);
			if (name == "UNK_LANG")
				continue;
			bool known = false;
			for (uint i = 0; i < ARRAYSIZE(kEncoding); i++)
				if (name == kEncoding[i].name)
					known = true;
			if (!known) {
				TS_FAIL(("detection_tables.h names Common::" + name +
				         " with no recorded parser encoding").c_str());
				unaccounted++;
				break;
			}
		}
		TS_ASSERT_EQUALS(unaccounted, 0);
#endif
	}
};
