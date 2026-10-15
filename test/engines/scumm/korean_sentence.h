#include <cxxtest/TestSuite.h>

#include <string.h>

#include "engines/scumm/korean_sentence.h"

/**
 * The SCUMM v2 sentence line in Korean (MM, Zak): the particle helper, the
 * composer (verb last, preposition as a particle on B), the width fit, and
 * the English line drawSentence() keeps for every other game.
 */
namespace {

// Game pixels as the v2 sentence line measures them with the Korean fonts:
// 8 per single-byte character, 9 per Hangul syllable (CP949 pair or one
// UTF-8 code point).
int fakeWidth(const Common::String &s, void *ctx) {
	const bool utf8 = ctx != nullptr;
	int w = 0;
	for (const char *p = s.c_str(); *p;) {
		const int n = Scumm::koreanCharLength(p, utf8);
		w += (n == 1) ? 8 : 9;
		p += n;
	}
	return w;
}

void *kUtf8 = (void *)1;

Scumm::KoreanSentenceParts parts(const char *verb, const char *a = nullptr, int prep = 0, const char *b = nullptr,
								 bool bIsActor = false, bool instrumentB = false) {
	Scumm::KoreanSentenceParts p;
	p.verb = verb;
	if (a) {
		p.objA = a;
		p.hasA = true;
	}
	p.prep = prep;
	if (b) {
		p.objB = b;
		p.hasB = true;
	}
	p.bIsActor = bIsActor;
	p.instrumentB = instrumentB;
	return p;
}

// Every character whole: walking the string by koreanCharLength() ends on its NUL.
bool wholeChars(const Common::String &s, bool utf8) {
	const char *p = s.c_str();
	const char *end = p + s.size();
	while (p < end)
		p += Scumm::koreanCharLength(p, utf8);
	return p == end;
}

} // End of anonymous namespace

class ScummKoreanSentenceTestSuite : public CxxTest::TestSuite {
public:
	// --- the final consonant -------------------------------------------------

	void test_final_utf8() {
		TS_ASSERT_EQUALS(Scumm::koreanFinalOf("열쇠", true), Scumm::kKoFinalNone);
		TS_ASSERT_EQUALS(Scumm::koreanFinalOf("문", true), Scumm::kKoFinalConsonant);
		TS_ASSERT_EQUALS(Scumm::koreanFinalOf("칼", true), Scumm::kKoFinalRieul);
		TS_ASSERT_EQUALS(Scumm::koreanFinalOf("DAT 테입", true), Scumm::kKoFinalConsonant);
		TS_ASSERT_EQUALS(Scumm::koreanFinalOf("카드@@  ", true), Scumm::kKoFinalNone);
	}

	void test_final_cp949() {
		TS_ASSERT_EQUALS(Scumm::koreanFinalOf("\xc4\xae", false), Scumm::kKoFinalRieul);      // 칼
		TS_ASSERT_EQUALS(Scumm::koreanFinalOf("\xc6\xbc\xc4\xcf", false), Scumm::kKoFinalConsonant); // 티켓
		TS_ASSERT_EQUALS(Scumm::koreanFinalOf("\xc1\xd6\xb1\xe2", false), Scumm::kKoFinalNone); // 주기
	}

	// Latin letters, digits and symbols are read as vowel-final (no
	// pronunciation rule); nothing at all gives both forms.
	void test_final_latin_digits_and_empty() {
		TS_ASSERT_EQUALS(Scumm::koreanFinalOf("TV", true), Scumm::kKoFinalNone);
		TS_ASSERT_EQUALS(Scumm::koreanFinalOf("$1138", false), Scumm::kKoFinalNone);
		TS_ASSERT_EQUALS(Scumm::koreanFinalOf("", true), Scumm::kKoFinalUnknown);
		TS_ASSERT_EQUALS(Scumm::koreanFinalOf("@@", false), Scumm::kKoFinalUnknown);
	}

	// --- particles -----------------------------------------------------------

	void test_josa_pairs() {
		TS_ASSERT_EQUALS(Scumm::koreanWithJosa("문", Scumm::kJosaObject, true), "문을");
		TS_ASSERT_EQUALS(Scumm::koreanWithJosa("열쇠", Scumm::kJosaObject, true), "열쇠를");
		TS_ASSERT_EQUALS(Scumm::koreanWithJosa("문", Scumm::kJosaTopic, true), "문은");
		TS_ASSERT_EQUALS(Scumm::koreanWithJosa("열쇠", Scumm::kJosaTopic, true), "열쇠는");
		TS_ASSERT_EQUALS(Scumm::koreanWithJosa("문", Scumm::kJosaSubject, true), "문이");
		TS_ASSERT_EQUALS(Scumm::koreanWithJosa("열쇠", Scumm::kJosaSubject, true), "열쇠가");
		TS_ASSERT_EQUALS(Scumm::koreanWithJosa("문", Scumm::kJosaAnd, true), "문과");
		TS_ASSERT_EQUALS(Scumm::koreanWithJosa("열쇠", Scumm::kJosaAnd, true), "열쇠와");
		TS_ASSERT_EQUALS(Scumm::koreanWithJosa("왕", Scumm::kJosaTo, true), "왕에게");
		TS_ASSERT_EQUALS(Scumm::koreanWithJosa("문", Scumm::kJosaAt, true), "문에");
	}

	// (으)로: 으로 after a consonant, 로 after a vowel and after ㄹ.
	void test_josa_means_rieul_rule() {
		TS_ASSERT_EQUALS(Scumm::koreanWithJosa("문", Scumm::kJosaMeans, true), "문으로");
		TS_ASSERT_EQUALS(Scumm::koreanWithJosa("열쇠", Scumm::kJosaMeans, true), "열쇠로");
		TS_ASSERT_EQUALS(Scumm::koreanWithJosa("칼", Scumm::kJosaMeans, true), "칼로");
		TS_ASSERT_EQUALS(Scumm::koreanWithJosa("TV", Scumm::kJosaMeans, true), "TV로");
		// ㄹ only changes (으)로: 칼을, 칼과
		TS_ASSERT_EQUALS(Scumm::koreanWithJosa("칼", Scumm::kJosaObject, true), "칼을");
		TS_ASSERT_EQUALS(Scumm::koreanWithJosa("칼", Scumm::kJosaAnd, true), "칼과");
	}

	void test_josa_unknown_writes_both() {
		TS_ASSERT_EQUALS(Scumm::koreanJosa(Scumm::kKoFinalUnknown, Scumm::kJosaObject, true), "을(를)");
		TS_ASSERT_EQUALS(Scumm::koreanJosa(Scumm::kKoFinalUnknown, Scumm::kJosaTopic, true), "은(는)");
		TS_ASSERT_EQUALS(Scumm::koreanJosa(Scumm::kKoFinalUnknown, Scumm::kJosaSubject, true), "이(가)");
		TS_ASSERT_EQUALS(Scumm::koreanJosa(Scumm::kKoFinalUnknown, Scumm::kJosaAnd, true), "과(와)");
		TS_ASSERT_EQUALS(Scumm::koreanJosa(Scumm::kKoFinalUnknown, Scumm::kJosaMeans, true), "(으)로");
		TS_ASSERT_EQUALS(Scumm::koreanJosa(Scumm::kKoFinalUnknown, Scumm::kJosaNone, true), "");
	}

	void test_josa_cp949() {
		TS_ASSERT_EQUALS(Scumm::koreanWithJosa("\xc4\xae", Scumm::kJosaMeans, false), "\xc4\xae\xb7\xce"); // 칼로
		TS_ASSERT_EQUALS(Scumm::koreanWithJosa("\xbf\xd5", Scumm::kJosaTo, false), "\xbf\xd5\xbf\xa1\xb0\xd4"); // 왕에게
	}

	// --- the composer --------------------------------------------------------

	void test_verb_only_and_one_object() {
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(parts("집기"), true), "집기");
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(parts("집기", "티켓"), true), "티켓 집기");
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(parts("향하기", "문"), true), "문 향하기");
	}

	void test_give_to() {
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(parts("주기", "티켓", 4, "왕", true), true), "왕에게 티켓 주기");
		// To a thing, not a person: 에
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(parts("주기", "티켓", 4, "문", false), true), "문에 티켓 주기");
		// B not chosen yet
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(parts("주기", "티켓", 4), true), "...에게 티켓 주기");
	}

	void test_use_in_with_on() {
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(parts("사용하기", "DAT 테입", 1, "녹음 붐박스"), true),
						 "녹음 붐박스에 DAT 테입 사용하기");
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(parts("사용하기", "빵가루", 2, "새 모이통"), true),
						 "새 모이통에 빵가루 사용하기");
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(parts("사용하기", "열쇠", 3, "문"), true), "문에 열쇠 사용하기");
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(parts("사용하기", "열쇠", 1), true), "...에 열쇠 사용하기");
	}

	// Unlock/Fix X with Y: Y is the tool, (으)로.
	void test_with_instrument() {
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(parts("잠금 풀기", "문", 2, "열쇠", false, true), true),
						 "열쇠로 문 잠금 풀기");
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(parts("고치기", "전화", 2, "진공관", false, true), true),
						 "진공관으로 전화 고치기");
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(parts("잠금 풀기", "문", 2, nullptr, false, true), true),
						 "...(으)로 문 잠금 풀기");
	}

	void test_question_verb_stays_first() {
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(parts("이게 뭐지?", "램프"), true), "이게 뭐지? 램프");
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(parts("이게 뭐지?"), true), "이게 뭐지?");
	}

	// '@' padding and the spaces that centre a verb on its button are not part of the line.
	void test_padding_dropped() {
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(parts("   미소 짓기", "문@@"), true), "문 미소 짓기");
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(parts("주기@@", "티켓@", 4, "왕@@@", true), true), "왕에게 티켓 주기");
	}

	// A preposition with no particle (index 0, or B with none): English order of the nouns, verb last.
	void test_two_objects_no_preposition() {
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(parts("사용하기", "열쇠", 0, "문"), true), "열쇠 문 사용하기");
	}

	void test_compose_cp949() {
		Scumm::KoreanSentenceParts p = parts("\xc1\xd6\xb1\xe2", "\xc6\xbc\xc4\xcf", 4, "\xbf\xd5", true);
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(p, false),
						 "\xbf\xd5\xbf\xa1\xb0\xd4 \xc6\xbc\xc4\xcf \xc1\xd6\xb1\xe2");
		p.hasB = false;
		p.objB.clear();
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(p, false), "...\xbf\xa1\xb0\xd4 \xc6\xbc\xc4\xcf \xc1\xd6\xb1\xe2");
	}

	// --- the width fit -------------------------------------------------------

	void test_fit_leaves_a_short_line() {
		Scumm::KoreanSentenceParts p = parts("사용하기", "DAT 테입", 1, "녹음 붐박스");
		TS_ASSERT_EQUALS(Scumm::fitKoreanSentence(p, true, 320, fakeWidth, kUtf8), "녹음 붐박스에 DAT 테입 사용하기");
	}

	// Zak's longest names (16 bytes, 7 syllables) on both sides, the longest
	// verb: 219 px at 9 px a syllable, inside the 320 px line - the fit never
	// has to shorten a real Zak or MM sentence.
	void test_fit_longest_zak_names_untouched() {
		Scumm::KoreanSentenceParts p = parts("사용하기", "테이프 붙인 어항", 1, "레이니어 산 동굴");
		const Common::String line = Scumm::fitKoreanSentence(p, true, 320, fakeWidth, kUtf8);
		TS_ASSERT_EQUALS(line, "레이니어 산 동굴에 테이프 붙인 어항 사용하기");
		TS_ASSERT_LESS_THAN_EQUALS(fakeWidth(line, kUtf8), 320);
		p = parts("사용하기", "DAT 테입", 1, "녹음 붐박스");
		TS_ASSERT_EQUALS(fakeWidth(Scumm::fitKoreanSentence(p, true, 320, fakeWidth, kUtf8), kUtf8), 164);
	}

	// Too long: the longer name loses characters, whole ones, the verb stays,
	// and the particle is the full name's.
	void test_fit_shortens_the_longer_name() {
		Scumm::KoreanSentenceParts p = parts("잠금 풀기", "문", 2, "커다란 놋쇠 열쇠", false, true);
		const Common::String full = Scumm::composeKoreanSentence(p, true);
		TS_ASSERT_EQUALS(full, "커다란 놋쇠 열쇠로 문 잠금 풀기");
		const int maxW = fakeWidth(full, kUtf8) - 20;
		const Common::String fit = Scumm::fitKoreanSentence(p, true, maxW, fakeWidth, kUtf8);
		TS_ASSERT_LESS_THAN_EQUALS(fakeWidth(fit, kUtf8), maxW);
		// 열쇠 goes, then the space before it: 131 px of 137
		TS_ASSERT_EQUALS(fit, "커다란 놋쇠로 문 잠금 풀기");
		TS_ASSERT(wholeChars(fit, true));
	}

	void test_fit_cp949_never_splits_a_syllable() {
		// 왕에게 티켓 주기 = 7 syllables + 2 spaces = 7*9 + 16 = 79 px
		Scumm::KoreanSentenceParts p = parts("\xc1\xd6\xb1\xe2", "\xc6\xbc\xc4\xcf", 4, "\xbf\xd5", true);
		for (int maxW = 0; maxW <= 80; maxW++) {
			const Common::String fit = Scumm::fitKoreanSentence(p, false, maxW, fakeWidth, nullptr);
			TS_ASSERT_LESS_THAN_EQUALS(fakeWidth(fit, nullptr), maxW);
			TS_ASSERT(wholeChars(fit, false));
		}
		// Room for all but one syllable: the name 티켓 gives one up (켓), 왕 stays.
		TS_ASSERT_EQUALS(Scumm::fitKoreanSentence(p, false, 75, fakeWidth, nullptr),
						 "\xbf\xd5\xbf\xa1\xb0\xd4 \xc6\xbc \xc1\xd6\xb1\xe2");
	}

	// Names at one character and still too wide: cut after the last whole character.
	void test_fit_hard_cut() {
		Scumm::KoreanSentenceParts p = parts("사용하기", "열쇠", 1, "문");
		const Common::String fit = Scumm::fitKoreanSentence(p, true, 30, fakeWidth, kUtf8);
		TS_ASSERT_EQUALS(fit, "문에");   // 9+9 = 18 (+8 for the space = 26); the next syllable would make 35, and no space is left hanging
		TS_ASSERT(wholeChars(fit, true));
	}

	// A chosen B whose name is empty (only '@' padding) is not the "..." of a B still to come.
	void test_empty_chosen_b_is_not_the_placeholder() {
		Scumm::KoreanSentenceParts p = parts("주기", "티켓", 4, "@@");
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(p, true), "티켓 주기");
		Scumm::KoreanSentenceParts q = parts("주기", "티켓", 4);	// B not chosen yet
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(q, true), "...에게 티켓 주기");
	}

	// --- where it applies, and the English line ------------------------------

	// An untranslated verb ("Give" kept by a translation that maps it to itself)
	// keeps the English order; a Korean one composes.
	void test_verb_translated() {
		TS_ASSERT(Scumm::koreanVerbTranslated("주기@@"));
		TS_ASSERT(Scumm::koreanVerbTranslated("\xc1\xd6\xb1\xe2"));
		TS_ASSERT(Scumm::koreanVerbTranslated("이게 뭐지?"));
		TS_ASSERT(!Scumm::koreanVerbTranslated("Give@@"));
		TS_ASSERT(!Scumm::koreanVerbTranslated("What is@@"));
		TS_ASSERT(!Scumm::koreanVerbTranslated(""));
	}

	void test_applies_only_to_korean_v2_with_bundle() {
		TS_ASSERT(Scumm::koreanSentenceApplies(2, Common::kPlatformDOS, Common::KO_KOR, true));
		TS_ASSERT(!Scumm::koreanSentenceApplies(2, Common::kPlatformDOS, Common::KO_KOR, false));
		TS_ASSERT(!Scumm::koreanSentenceApplies(2, Common::kPlatformDOS, Common::EN_ANY, true));
		TS_ASSERT(!Scumm::koreanSentenceApplies(2, Common::kPlatformDOS, Common::DE_DEU, false));
		TS_ASSERT(!Scumm::koreanSentenceApplies(1, Common::kPlatformDOS, Common::KO_KOR, true));
		TS_ASSERT(!Scumm::koreanSentenceApplies(1, Common::kPlatformC64, Common::KO_KOR, true));
		TS_ASSERT(!Scumm::koreanSentenceApplies(2, Common::kPlatformNES, Common::KO_KOR, true));
		TS_ASSERT(!Scumm::koreanSentenceApplies(5, Common::kPlatformDOS, Common::KO_KOR, true));
	}

	// The English line exactly as drawSentence() has always cut it.
	void test_english_cut_unchanged() {
		byte out[80];
		Scumm::cutV2Sentence("Give ticket to King", false, out, sizeof(out));
		TS_ASSERT_EQUALS(Common::String((const char *)out), "Give ticket to King");

		// '@' is copied but not counted; the 41st printable character is dropped.
		Scumm::cutV2Sentence("Use@@@ 0123456789012345678901234567890123456789", false, out, sizeof(out));
		TS_ASSERT_EQUALS(Common::String((const char *)out), "Use@@@ 012345678901234567890123456789012345");

		// NES: 60 characters, a 0xFF 0x08 after the 30th.
		Common::String nes;
		for (int i = 0; i < 70; i++)
			nes += (char)('a' + i % 26);
		Scumm::cutV2Sentence(nes.c_str(), true, out, sizeof(out));
		TS_ASSERT_EQUALS(strlen((const char *)out), 62u);
		TS_ASSERT_EQUALS(out[30], 0xFF);
		TS_ASSERT_EQUALS(out[31], 8);
		TS_ASSERT_SAME_DATA(out, nes.c_str(), 30);
		TS_ASSERT_SAME_DATA(out + 32, nes.c_str() + 30, 30);
	}

	// Many '@' could run the old loop past its buffer; the cut stays inside.
	void test_english_cut_bounded() {
		byte out[16];
		memset(out, 0x5A, sizeof(out));
		Scumm::cutV2Sentence("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@x", false, out, 8);
		TS_ASSERT_EQUALS(strlen((const char *)out), 7u);
		TS_ASSERT_EQUALS(out[8], 0x5A);
	}
};
