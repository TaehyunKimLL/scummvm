#include <cxxtest/TestSuite.h>

#include "engines/scumm/korean_sentence.h"

// The v2 sentence line in Korean (engines/scumm/korean_sentence.h). Strings are UTF-8.
#define KO_TICKET "\xed\x8b\xb0\xec\xbc\x93"   // 티켓 (final ㅅ)
#define KO_KING   "\xec\x99\x95"           // 왕 (consonant-final)
#define KO_DOOR   "\xeb\xac\xb8"           // 문 (consonant-final)
#define KO_KEY    "\xec\x97\xb4\xec\x87\xa0"   // 열쇠 (vowel-final)
#define KO_GIVE   "\xec\xa3\xbc\xea\xb8\xb0"   // 주기
#define KO_OPEN   "\xec\x97\xb4\xea\xb8\xb0"   // 열기
#define KO_WHAT   "\xec\x9d\xb4\xea\xb2\x8c \xeb\xad\x90\xec\xa7\x80?" // 이게 뭐지?

class ScummKoreanSentenceTestSuite : public CxxTest::TestSuite {
	static int bytesWide(const Common::String &s, void *) { return (int)s.size() * 2; }

public:
	void test_final_and_josa() {
		TS_ASSERT_EQUALS(Scumm::koreanFinalOf(KO_KING), Scumm::kKoFinalConsonant);
		TS_ASSERT_EQUALS(Scumm::koreanFinalOf(KO_TICKET "@@"), Scumm::kKoFinalConsonant);
		TS_ASSERT_EQUALS(Scumm::koreanFinalOf(KO_KEY " "), Scumm::kKoFinalNone);
		TS_ASSERT_EQUALS(Scumm::koreanFinalOf("\xed\x85\x8c\xec\x9d\xb4\xeb\xb8\x94"), Scumm::kKoFinalRieul); // 테이블
		TS_ASSERT_EQUALS(Scumm::koreanFinalOf("sign"), Scumm::kKoFinalNone);
		TS_ASSERT_EQUALS(Scumm::koreanFinalOf(""), Scumm::kKoFinalUnknown);
		TS_ASSERT_EQUALS(Scumm::koreanJosa(Scumm::kKoFinalRieul, Scumm::kJosaMeans), "\xeb\xa1\x9c");
		TS_ASSERT_EQUALS(Scumm::koreanJosa(Scumm::kKoFinalConsonant, Scumm::kJosaMeans), "\xec\x9c\xbc\xeb\xa1\x9c");
	}

	void test_compose() {
		Scumm::KoreanSentenceParts p;
		p.verb = KO_GIVE;
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(p), KO_GIVE);
		p.objA = KO_TICKET; p.hasA = true; p.prep = 4;
		// B not chosen yet: "...에게 티켓 주기"
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(p), "...\xec\x97\x90\xea\xb2\x8c " KO_TICKET " " KO_GIVE);
		p.objB = KO_KING; p.hasB = true; p.bIsActor = true;
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(p), KO_KING "\xec\x97\x90\xea\xb2\x8c " KO_TICKET " " KO_GIVE);
		// Open door with key (MM "unlock ... with"): the tool takes (으)로; 열쇠 is vowel-final -> 로.
		Scumm::KoreanSentenceParts q;
		q.verb = KO_OPEN; q.objA = KO_DOOR; q.hasA = true; q.objB = KO_KEY; q.hasB = true; q.prep = 2; q.instrumentB = true;
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(q), KO_KEY "\xeb\xa1\x9c " KO_DOOR " " KO_OPEN);
		// "What is" stays first.
		Scumm::KoreanSentenceParts w;
		w.verb = KO_WHAT; w.objA = "sign"; w.hasA = true;
		TS_ASSERT_EQUALS(Scumm::composeKoreanSentence(w), KO_WHAT " sign");
	}

	void test_fit_never_cuts_a_character() {
		Scumm::KoreanSentenceParts p;
		p.verb = KO_GIVE; p.objA = KO_TICKET KO_TICKET KO_TICKET; p.hasA = true;
		// 4 px a byte; "티켓티켓티켓 주기" = 17 bytes = 68 px; 40 px leaves A two characters + " 주기".
		const Common::String fit = Scumm::fitKoreanSentence(p, 40, bytesWide, nullptr);
		TS_ASSERT_EQUALS(fit, KO_TICKET KO_TICKET " " KO_GIVE);
		TS_ASSERT(fit.size() * 2 <= 40);
	}

	void test_applies() {
		TS_ASSERT(Scumm::koreanSentenceApplies(2, Common::kPlatformDOS, Common::KO_KOR, true));
		TS_ASSERT(!Scumm::koreanSentenceApplies(1, Common::kPlatformDOS, Common::KO_KOR, true));
		TS_ASSERT(!Scumm::koreanSentenceApplies(2, Common::kPlatformNES, Common::KO_KOR, true));
		TS_ASSERT(!Scumm::koreanSentenceApplies(2, Common::kPlatformDOS, Common::EN_ANY, true));
		TS_ASSERT(!Scumm::koreanSentenceApplies(2, Common::kPlatformDOS, Common::KO_KOR, false));
		TS_ASSERT(!Scumm::koreanVerbTranslated("Give"));
		TS_ASSERT(Scumm::koreanVerbTranslated(KO_GIVE));
	}
};
