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
	       gLook = 0x70, gAsk = 0x80, gAbout = 0x90, gTree = 0xA0, gHole = 0xA1 };

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

	void test_englishLineListsEveryWordOfEachGroup() {
		Sci::KoreanParser p(_words, _vocab);
		add("portal", kNoun, gDoor); // a synonym in the same group
		static const char *const typed[] = { "\xEB\xAC\xB8\xEC\x9D\x84", "\xEC\x97\xB4\xEC\x96\xB4", nullptr }; // 문을 열어
		ResultWordListList words;
		Common::Array<uint8> roles;
		tokens(p, typed, words, roles);
		TS_ASSERT_EQUALS(p.englishLine(words), "door portal open");
	}

	void test_englishLineIsEmptyForATokenWithNoWord() {
		Sci::KoreanParser p(_words, _vocab);
		ResultWordListList words;
		ResultWordList none;
		ResultWord r = { kNoun, 0x7FF };
		none.push_back(r);
		words.push_back(none);
		TS_ASSERT_EQUALS(p.englishLine(words), "");
	}

	void test_answerRoomTable() {
		TS_ASSERT(Sci::koreanAnswerRoom(Sci::GID_QFG2, 199));
		TS_ASSERT(Sci::koreanAnswerRoom(Sci::GID_QFG1, 22));
		TS_ASSERT(Sci::koreanAnswerRoom(Sci::GID_QFG1, 29));
		TS_ASSERT(!Sci::koreanAnswerRoom(Sci::GID_QFG1VGA, 29));
		TS_ASSERT(!Sci::koreanAnswerRoom(Sci::GID_QFG2, 198));
		TS_ASSERT(!Sci::koreanAnswerRoom(Sci::GID_KQ1, 199));
	}
};
