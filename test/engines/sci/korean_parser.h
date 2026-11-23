#include <cxxtest/TestSuite.h>

#include <common/memstream.h>

#include "../../korean_vocab_fixture.h"
#include "sci/parser/korean_parser.h"

using Sci::ResultWord;
using Sci::ResultWordList;
using Sci::ResultWordListList;
using Sci::WordMap;

// Groups and classes as a SCI0 vocabulary gives them (class << 4).
class SciKoreanParserTestSuite : public CxxTest::TestSuite {
	enum { kVerb = 0x800, kNoun = 0x100, kPrep = 0x010 };
	enum { gOpen = 0x10, gDoor = 0x20, gGive = 0x30, gBowl = 0x40, gWoodcutter = 0x50, gTo = 0x60, gIn = 0x61,
	       gLook = 0x70, gAsk = 0x80, gAbout = 0x90, gTree = 0xA0, gHole = 0xA1, gThe = 0xB0, gInside = 0xB1, gGriffin = 0xC0, gAir = 0xC1 };
	enum { kArticle = 0x020 };

	WordMap _words;
	Common::Array<byte> _table;
	Common::KoreanVocabulary _vocab;

	void add(const char *w, int cls, int group) {
		ResultWord r = { cls, group };
		_words[w].push_back(r);
	}

	// Tokens of a typed sentence through the parser's lookup; the roles alongside.
	void tokens(Sci::KoreanParser &p, const char *const *typed, ResultWordListList &out, Common::Array<uint8> &roles) {
		for (; *typed; ++typed) {
			ResultWordList w;
			ResultWordListList extra;
			uint8 role = 0;
			TSM_ASSERT(*typed, p.lookup(*typed, w, extra, role) > 0);
			out.push_back(w);
			roles.push_back(role);
			for (ResultWordListList::const_iterator e = extra.begin(); e != extra.end(); ++e) {
				out.push_back(*e);
				roles.push_back(0);
			}
		}
	}

	static Common::String groups(const ResultWordListList &l) {
		Common::String s;
		for (ResultWordListList::const_iterator t = l.begin(); t != l.end(); ++t)
			s += Common::String::format("%s%x", s.empty() ? "" : " ", t->front()._group);
		return s;
	}

public:
	void setUp() override {
		_words.clear();
		add("open", kVerb, gOpen);
		add("door", kNoun, gDoor);
		add("give", kVerb, gGive);
		add("bowl", kNoun, gBowl);
		add("woodcutter", kNoun, gWoodcutter);
		add("to", kPrep, gTo);
		add("in", kPrep, gIn);
		add("look", kVerb, gLook);
		add("ask", kVerb, gAsk);
		add("about", 0x008, gAbout);
		add("tree", kNoun, gTree);
		add("hole", kNoun, gHole);
		add("the", kArticle, gThe);
		static const KoreanVocabFixture::Row rows[] = {
			{ "\xEB\xAC\xB8", gDoor, kNoun },                         // 문
			{ "\xEC\x97\xB4\xEB\x8B\xA4", gOpen, kVerb },             // 열다
			{ "\xEA\xB7\xB8\xEB\xA6\x87", gBowl, kNoun },             // 그릇
			{ "\xEB\x82\x98\xEB\xAC\xB4\xEA\xBE\xBC", gWoodcutter, kNoun }, // 나무꾼
			{ "\xEC\xA3\xBC\xEB\x8B\xA4", gGive, kVerb },             // 주다
			{ "\xEB\x93\xA4\xEC\x97\xAC\xEB\x8B\xA4\xEB\xB3\xB4\xEB\x8B\xA4", gLook, kVerb | Common::KoreanVocabulary::kSequenceFlag }, // 들여다보다
			{ "\xEB\x93\xA4\xEC\x97\xAC\xEB\x8B\xA4\xEB\xB3\xB4\xEB\x8B\xA4", gIn, kPrep },
			{ "\xEB\x82\x98\xEB\xAC\xB4", gTree, kNoun },             // 나무
			{ "\xEA\xB5\xAC\xEB\xA9\x8D", gHole, kNoun },             // 구멍
			{ "\xEB\xAC\xBC\xEC\x96\xB4\xEB\xB3\xB4\xEB\x8B\xA4", gAsk, kVerb }, // 물어보다
			{ "\xEA\xB7\xB8", gThe, kArticle },                    // 그
			{ "\xEC\x95\x88", gIn, kPrep },                        // 안
			{ "\xEB\xB3\xB4\xEB\x8B\xA4", gLook, kVerb },       // 보다
		};
		_table = KoreanVocabFixture::build("SCIKOR", rows, ARRAYSIZE(rows));
		Common::MemoryReadStream s(_table.data(), _table.size());
		TS_ASSERT(_vocab.loadFromStream(s, "SCIKOR", "fixture"));
	}

	void test_objectVerbBecomesVerbObject() {
		Sci::KoreanParser p(_words, _vocab);
		static const char *const typed[] = { "\xEB\xAC\xB8\xEC\x9D\x84", "\xEC\x97\xB4\xEC\x96\xB4", nullptr }; // 문을 열어
		ResultWordListList words, fallback;
		Common::Array<uint8> roles;
		tokens(p, typed, words, roles);
		p.arrange(words, roles, fallback);
		TS_ASSERT_EQUALS(groups(words), "10 20");
		TS_ASSERT(fallback.empty());
	}

	void test_particleBecomesPreposition() {
		Sci::KoreanParser p(_words, _vocab);
		// 나무꾼에게 그릇을 줘 -> give bowl to woodcutter; the fallback drops the "to".
		static const char *const typed[] = { "\xEB\x82\x98\xEB\xAC\xB4\xEA\xBE\xBC\xEC\x97\x90\xEA\xB2\x8C",
		                                     "\xEA\xB7\xB8\xEB\xA6\x87\xEC\x9D\x84", "\xEC\xA4\x98", nullptr };
		ResultWordListList words, fallback;
		Common::Array<uint8> roles;
		tokens(p, typed, words, roles);
		TS_ASSERT_EQUALS(roles[0], (uint8)Common::kKoreanRoleTo);
		p.arrange(words, roles, fallback);
		TS_ASSERT_EQUALS(groups(words), "30 40 60 50");
		TS_ASSERT_EQUALS(groups(fallback), "30 50 40");
	}

	void test_sequenceWordAndNounUnit() {
		Sci::KoreanParser p(_words, _vocab);
		// 나무 구멍을 들여다봐 -> look in tree hole
		static const char *const typed[] = { "\xEB\x82\x98\xEB\xAC\xB4", "\xEA\xB5\xAC\xEB\xA9\x8D\xEC\x9D\x84",
		                                     "\xEB\x93\xA4\xEC\x97\xAC\xEB\x8B\xA4\xEB\xB4\x90", nullptr };
		ResultWordListList words, fallback;
		Common::Array<uint8> roles;
		tokens(p, typed, words, roles);
		p.arrange(words, roles, fallback);
		TS_ASSERT_EQUALS(groups(words), "70 61 a0 a1");
	}

	void test_askGetsAbout() {
		Sci::KoreanParser p(_words, _vocab);
		static const char *const typed[] = { "\xEB\xAC\xB8", "\xEB\xAC\xBC\xEC\x96\xB4\xEB\xB4\x90", nullptr }; // 문 물어봐
		ResultWordListList words, fallback;
		Common::Array<uint8> roles;
		tokens(p, typed, words, roles);
		p.arrange(words, roles, fallback);
		TS_ASSERT_EQUALS(groups(words), "80 90 20");
	}

	void test_joinPairs() {
		Sci::KoreanParser p(_words, _vocab);
		// 들여다 봐: the table holds the joined word.
		TS_ASSERT_EQUALS(p.joinPairs("\xEB\x93\xA4\xEC\x97\xAC\xEB\x8B\xA4 \xEB\xB4\x90"),
		                 "\xEB\x93\xA4\xEC\x97\xAC\xEB\x8B\xA4\xEB\xB4\x90");
		TS_ASSERT_EQUALS(p.joinPairs("open  door"), "open door");
	}

	void test_unknownWord() {
		Sci::KoreanParser p(_words, _vocab);
		ResultWordList w;
		ResultWordListList extra;
		uint8 role;
		TS_ASSERT_EQUALS(p.lookup("\xEC\x9E\x90\xEC\x84\xB8\xED\x9E\x88", w, extra, role), 0u); // 자세히
		TS_ASSERT(w.empty());
	}

	// The words an answer room searches for, as whole words of a token's groups.
	void test_englishLineEmitsOnlySearchWords() {
		Sci::KoreanParser p(_words, _vocab);
		add("portal", kNoun, gDoor); // a synonym in the same group
		static const char *const search[] = { "door", "open", nullptr };
		static const char *const typed[] = { "\xEB\xAC\xB8\xEC\x9D\x84", "\xEC\x97\xB4\xEC\x96\xB4", nullptr }; // 문을 열어
		ResultWordListList words;
		Common::Array<uint8> roles;
		tokens(p, typed, words, roles);
		TS_ASSERT_EQUALS(p.englishLine(words, search), "door open"); // not "portal", in the list's order
	}

	void test_englishLineIsEmptyWithoutASearchWord() {
		Sci::KoreanParser p(_words, _vocab);
		ResultWordListList words;
		ResultWordList none;
		ResultWord r = { kNoun, 0x7FF };
		none.push_back(r);
		words.push_back(none);
		static const char *const search[] = { "door", nullptr };
		TS_ASSERT_EQUALS(p.englishLine(words, search), "");
	}

	// agy P4 #3 / Opus M2: a word whose English synonym merely contains the search word does not pass.
	void test_englishLineIsNotASubstringSearch() {
		Sci::KoreanParser p(_words, _vocab);
		add("airplane", kNoun, gGriffin);
		add("griffin", kNoun, gGriffin);
		add("air", kNoun, gAir);
		static const KoreanVocabFixture::Row rows[] = {
			{ "\xEA\xB7\xB8\xEB\xA6\xAC\xED\x95\x80", gGriffin, kNoun }, // 그리핀
			{ "\xEA\xB3\xB5\xEA\xB8\xB0", gAir, kNoun },                    // 공기
			{ "\xEB\xAC\xB8", gDoor, kNoun },
		};
		_table = KoreanVocabFixture::build("SCIKOR", rows, ARRAYSIZE(rows));
		Common::MemoryReadStream st(_table.data(), _table.size());
		Common::KoreanVocabulary v;
		TS_ASSERT(v.loadFromStream(st, "SCIKOR", "fixture"));
		Sci::KoreanParser q(_words, v);
		static const char *const search[] = { "air", nullptr };
		{
			static const char *const typed[] = { "\xEA\xB7\xB8\xEB\xA6\xAC\xED\x95\x80", nullptr }; // 그리핀
			ResultWordListList words;
			Common::Array<uint8> roles;
			tokens(q, typed, words, roles);
			TS_ASSERT_EQUALS(q.englishLine(words, search), "");
		}
		{
			static const char *const typed[] = { "\xEA\xB3\xB5\xEA\xB8\xB0", nullptr }; // 공기
			ResultWordListList words;
			Common::Array<uint8> roles;
			tokens(q, typed, words, roles);
			TS_ASSERT_EQUALS(q.englishLine(words, search), "air");
		}
		{
			// a case particle on the answer: the token still reads as its word (공기로), the preposition is not an answer word
			static const char *const typed[] = { "\xEA\xB3\xB5\xEA\xB8\xB0\xEB\xA1\x9C", nullptr };
			ResultWordListList words;
			Common::Array<uint8> roles;
			tokens(q, typed, words, roles);
			TS_ASSERT_EQUALS(q.englishLine(words, search), "air");
		}
		{
			// 문에: a word that is not an answer word reads as nothing, the typed text stays
			static const char *const typed[] = { "\xEB\xAC\xB8\xEC\x97\x90", nullptr };
			ResultWordListList words;
			Common::Array<uint8> roles;
			tokens(q, typed, words, roles);
			TS_ASSERT_EQUALS(q.englishLine(words, search), "");
		}
	}

	void test_answerRoomTable() {
		TS_ASSERT(Sci::koreanAnswerRoom(Sci::GID_QFG2, 199));
		TS_ASSERT(Sci::koreanAnswerRoom(Sci::GID_QFG1, 22));
		TS_ASSERT(Sci::koreanAnswerRoom(Sci::GID_QFG1, 29));
		TS_ASSERT(!Sci::koreanAnswerRoom(Sci::GID_QFG1VGA, 29));
		TS_ASSERT(!Sci::koreanAnswerRoom(Sci::GID_QFG2, 198));
		TS_ASSERT(!Sci::koreanAnswerRoom(Sci::GID_KQ1, 199));
	}

	// agy P4 #2: only the room's search script gets the view; a save list (SRDialog)
	// or an edit control of the same room reads what was typed.
	void test_answerViewOnlyForTheSearchScript() {
		const Sci::KoreanAnswerRoom *aziza = Sci::koreanAnswerRoom(Sci::GID_QFG2, 199);
		TS_ASSERT(aziza->appliesTo(aziza->script));
		TS_ASSERT(!aziza->appliesTo(255));  // DEdit / Dialog
		TS_ASSERT(!aziza->appliesTo(199));  // the room script itself
		TS_ASSERT(aziza->words[0] != nullptr);
		TS_ASSERT_EQUALS(sizeof(aziza->room), 2u);
	}

	// agy 3: an article that carries a case particle (그에게) is not part of the next noun's unit.
	void test_articleWithParticleBeforeNounUnit() {
		Sci::KoreanParser p(_words, _vocab);
		// 그에게 문 안을 봐
		static const char *const typed[] = { "\xEA\xB7\xB8\xEC\x97\x90\xEA\xB2\x8C", "\xEB\xAC\xB8",
		                                     "\xEC\x95\x88\xEC\x9D\x84", "\xEB\xB4\x90", nullptr };
		ResultWordListList words, fallback;
		Common::Array<uint8> roles;
		tokens(p, typed, words, roles);
		p.arrange(words, roles, fallback);
		TS_ASSERT(words.size() >= 3);
	}

	void test_articleWithParticleThenNounWithParticle() {
		Sci::KoreanParser p(_words, _vocab);
		// 그에게 문에게 줘
		static const char *const typed[] = { "\xEA\xB7\xB8\xEC\x97\x90\xEA\xB2\x8C",
		                                     "\xEB\xAC\xB8\xEC\x97\x90\xEA\xB2\x8C", "\xEC\xA4\x98", nullptr };
		ResultWordListList words, fallback;
		Common::Array<uint8> roles;
		tokens(p, typed, words, roles);
		p.arrange(words, roles, fallback);
		TS_ASSERT_EQUALS(groups(words).substr(0, 2), "30");
		TS_ASSERT(words.size() >= 3);
	}

	// agy 1: a game without a Korean table is never rearranged. "lege schluessel auf tisch" (put key on table)
	// is verb noun preposition noun: arrange() would turn it around, so the tokenizer must not call it.
	void test_nonKoreanSentenceIsNotArranged() {
		TS_ASSERT(!Sci::KoreanParser::shouldArrange(false, true));
		TS_ASSERT(!Sci::KoreanParser::shouldArrange(false, false));
		TS_ASSERT(!Sci::KoreanParser::shouldArrange(true, false));
		TS_ASSERT(Sci::KoreanParser::shouldArrange(true, true));
		// What arrange() would have done to the German line (verb last? no: key/table are nouns, "auf" a preposition).
		Sci::KoreanParser p(_words, _vocab);
		ResultWordListList words, fallback;
		Common::Array<uint8> roles;
		static const char *const typed[] = { "\xEB\xAC\xB8", "\xEC\x95\x88", "\xEB\xAC\xB8", "\xEC\x97\xB4\xEC\x96\xB4", nullptr };
		tokens(p, typed, words, roles);
		const Common::String before = groups(words);
		p.arrange(words, roles, fallback);
		TS_ASSERT(groups(words) != before); // arrange() does reorder: hence the gate
	}
};
