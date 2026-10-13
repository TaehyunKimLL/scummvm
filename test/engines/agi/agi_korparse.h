#include <cxxtest/TestSuite.h>

#include "common/korean_stem.h"
#include "common/korean_vocab.h"
#include "common/memstream.h"
#include "../../korean_vocab_fixture.h"
#include "agi/korparse.h"

// Design 5.5: Korean puts the verb last; AGI's said() is positional, so the
// typed words are re-ordered into at most four candidates, best first:
// (a) verbs, plain objects, role-marked nouns; (b) (a) with the game's own
// preposition before a role-marked noun; (c) as typed; (d) X의 Y swapped.
class AgiKorParseTestSuite : public CxxTest::TestSuite {
	Common::KoreanVocabulary _voc;

	static Agi::AgiWord w(uint16 id, uint8 role = Common::kKoreanRoleNone, bool verb = false) {
		Agi::AgiWord a;
		a.id = id;
		a.role = role;
		a.verb = verb;
		a.korean = true;
		return a;
	}
	static Common::Array<uint16> ids(const Agi::AgiSentence &s) {
		Common::Array<uint16> out;
		for (uint i = 0; i < s.size(); i++)
			out.push_back(s[i].id);
		return out;
	}
	static Common::Array<uint16> v(uint16 a, uint16 b = 0, uint16 c = 0, uint16 d = 0) {
		Common::Array<uint16> out;
		const uint16 all[4] = { a, b, c, d };
		for (int i = 0; i < 4 && all[i]; i++)
			out.push_back(all[i]);
		return out;
	}

public:
	void setUp() {
		static const KoreanVocabFixture::Row rows[] = {
			{ "\xEC\x97\xB4\xEC\x87\xA0", 12, 0 },                       // 열쇠 key
			{ "\xEC\xA7\x91\xEC\x96\xB4", 20, 1 },                       // 집어 get (verb)
			{ "\xEB\x84\xA3\xEC\x96\xB4", 50, 1 },                       // 넣어 put (verb)
			{ "\xEC\x83\x81\xEC\x9E\x90", 40, 0 },                       // 상자 box
			{ "\xEB\x82\x98\xEB\xAC\xB4", 60, 0 },                       // 나무 tree
			{ "\xEA\xB5\xAC\xEB\xA9\x8D", 61, 0 },                       // 구멍 hole
			{ "\xEB\xB4\x90", 30, 1 },                                   // 봐 look (verb)
			{ "\xEC\xA2\x80", 0, 0 },                                    // 좀 (ignored)
			{ "\xEB\x93\xA4\xEC\x97\xAC\xEB\x8B\xA4\xEB\xB4\x90", 30, 0x8001 }, // 들여다봐 look+
			{ "\xEB\x93\xA4\xEC\x97\xAC\xEB\x8B\xA4\xEB\xB4\x90", 169, 0 },    //          in
		};
		const Common::Array<byte> img = KoreanVocabFixture::build("AGIKOR", rows, ARRAYSIZE(rows));
		Common::MemoryReadStream s(img.data(), img.size());
		TS_ASSERT(_voc.loadFromStream(s, "AGIKOR", "fixture"));
	}

	void test_said_core_is_the_interpreters() {
		using Agi::saidMatches;
		Agi::AgiSentence s;
		s.push_back(w(20));
		s.push_back(w(12));
		const uint16 exact[] = { 20, 12 }, any[] = { 1, 12 }, rolTail[] = { 20, 9999 }, shorter[] = { 20 },
		             longer[] = { 20, 12, 9999 }, wrong[] = { 12, 20 };
		TS_ASSERT(saidMatches(s, exact, 2));
		TS_ASSERT(saidMatches(s, any, 2));
		TS_ASSERT(saidMatches(s, rolTail, 2));
		TS_ASSERT(!saidMatches(s, shorter, 1));
		TS_ASSERT(saidMatches(s, longer, 3));
		TS_ASSERT(!saidMatches(s, wrong, 2));
		Agi::AgiSentence one;
		one.push_back(w(20));
		TS_ASSERT(!saidMatches(one, exact, 2));
	}

	void test_token_lookup() {
		Agi::AgiSentence out;
		TS_ASSERT(Agi::lookupKoreanToken(_voc, "\xEC\x97\xB4\xEC\x87\xA0\xEB\xA5\xBC", out)); // 열쇠를
		TS_ASSERT_EQUALS(out.size(), 1u);
		TS_ASSERT_EQUALS(out[0].id, 12);
		TS_ASSERT_EQUALS(out[0].role, (uint8)Common::kKoreanRoleObject);
		TS_ASSERT_EQUALS(out[0].word, "\xEC\x97\xB4\xEC\x87\xA0\xEB\xA5\xBC");
		out.clear();
		TS_ASSERT(Agi::lookupKoreanToken(_voc, "\xEC\xA2\x80", out)); // 좀: known, ignored
		TS_ASSERT_EQUALS(out.size(), 0u);
		out.clear();
		TS_ASSERT(Agi::lookupKoreanToken(_voc, "\xEB\x93\xA4\xEC\x97\xAC\xEB\x8B\xA4\xEB\xB4\x90", out)); // look in
		TS_ASSERT_EQUALS(ids(out), v(30, 169));
		TS_ASSERT(out[0].verb);
		out.clear();
		TS_ASSERT(!Agi::lookupKoreanToken(_voc, "\xEB\xAA\xA8\xEB\xA6\x84", out)); // 모름: unknown
		out.clear();
		TS_ASSERT(!Agi::lookupKoreanToken(_voc, "\xEC\x97\xB4\xEC\x87\xA0\xEB\xA5\xBC", out, true)); // exact only
	}

	void test_exact_only_takes_a_stored_form() {
		Agi::AgiSentence out;
		TS_ASSERT(Agi::lookupKoreanToken(_voc, "\xEC\x97\xB4\xEC\x87\xA0", out, true)); // 열쇠
		TS_ASSERT_EQUALS(ids(out), v(12));
		TS_ASSERT_EQUALS(out[0].role, (uint8)Common::kKoreanRoleNone);
	}

	// 5.5: 들여다봐 = look+in. The tail of a sequence stays behind its verb
	// in every re-ordering, or said(look, in, box) is never reached.
	void test_sequence_tail_stays_with_its_verb() {
		Agi::AgiSentence typed;
		TS_ASSERT(Agi::lookupKoreanToken(_voc, "\xEC\x83\x81\xEC\x9E\x90\xEB\xA5\xBC", typed)); // 상자를
		TS_ASSERT(Agi::lookupKoreanToken(_voc, "\xEB\x93\xA4\xEC\x97\xAC\xEB\x8B\xA4\xEB\xB4\x90", typed)); // 들여다봐
		TS_ASSERT_EQUALS(ids(typed), v(40, 30, 169));
		TS_ASSERT(!typed[1].cont);
		TS_ASSERT(typed[2].cont);
		Common::Array<Agi::AgiSentence> c;
		Agi::buildKoreanCandidates(typed, Agi::AgiPrepositions(), 20, c);
		TS_ASSERT_EQUALS(c.size(), 2u);
		TS_ASSERT_EQUALS(ids(c[0]), v(30, 169, 40));
		TS_ASSERT_EQUALS(ids(c[1]), v(40, 30, 169));
		const uint16 spec[] = { 30, 169, 40 };
		TS_ASSERT(Agi::saidMatches(c[0], spec, 3));
	}

	void test_of_swap_moves_a_sequence_whole() {
		Agi::AgiSentence typed;
		typed.push_back(w(60, Common::kKoreanRoleOf));          // 나무의
		typed.push_back(w(61));                                 // 구멍에 (a sequence:
		Agi::AgiWord tail = w(62, Common::kKoreanRoleAt);       // the role is on its tail)
		tail.cont = true;
		typed.push_back(tail);
		typed.push_back(w(30, Common::kKoreanRoleNone, true));  // 봐
		Common::Array<Agi::AgiSentence> c;
		Agi::buildKoreanCandidates(typed, Agi::AgiPrepositions(), 20, c);
		TS_ASSERT_EQUALS(c.size(), 3u);
		TS_ASSERT_EQUALS(ids(c[0]), v(30, 60, 61, 62));
		TS_ASSERT_EQUALS(ids(c[1]), v(60, 61, 62, 30));
		TS_ASSERT_EQUALS(ids(c[2]), v(30, 61, 62, 60)); // (d): 구멍 moved whole
	}

	void test_verb_last_becomes_verb_first() {
		Agi::AgiSentence typed;
		typed.push_back(w(12));
		typed.push_back(w(20, Common::kKoreanRoleNone, true));
		Common::Array<Agi::AgiSentence> c;
		Agi::buildKoreanCandidates(typed, Agi::AgiPrepositions(), 20, c);
		TS_ASSERT_EQUALS(c.size(), 2u);
		TS_ASSERT_EQUALS(ids(c[0]), v(20, 12));
		TS_ASSERT_EQUALS(ids(c[1]), v(12, 20));
	}

	void test_role_nouns_follow_and_take_the_games_preposition() {
		Agi::AgiPrepositions preps;
		preps.group[Common::kKoreanRoleAt] = 169;
		preps.word[Common::kKoreanRoleAt] = "in";
		Agi::AgiSentence typed;
		typed.push_back(w(40, Common::kKoreanRoleAt));          // 상자에
		typed.push_back(w(12, Common::kKoreanRoleObject));      // 열쇠를
		typed.push_back(w(50, Common::kKoreanRoleNone, true));  // 넣어
		Common::Array<Agi::AgiSentence> c;
		Agi::buildKoreanCandidates(typed, preps, 20, c);
		TS_ASSERT_EQUALS(c.size(), 3u);
		TS_ASSERT_EQUALS(ids(c[0]), v(50, 12, 40));
		TS_ASSERT_EQUALS(ids(c[1]), v(50, 12, 169, 40));
		TS_ASSERT_EQUALS(c[1][2].word, "in");
		TS_ASSERT_EQUALS(ids(c[2]), v(40, 12, 50));
	}

	void test_of_swaps_and_duplicates_drop() {
		Agi::AgiSentence typed;
		typed.push_back(w(60, Common::kKoreanRoleOf));          // 나무의
		typed.push_back(w(61));                                 // 구멍
		typed.push_back(w(30, Common::kKoreanRoleNone, true));  // 봐
		Common::Array<Agi::AgiSentence> c;
		Agi::buildKoreanCandidates(typed, Agi::AgiPrepositions(), 20, c);
		TS_ASSERT_EQUALS(c.size(), 2u);          // (d) equals (a) and is dropped
		TS_ASSERT_EQUALS(ids(c[0]), v(30, 61, 60));
		TS_ASSERT_EQUALS(ids(c[1]), v(60, 61, 30));
	}

	void test_max_words_clips_every_candidate() {
		Agi::AgiSentence typed;
		for (int i = 0; i < 6; i++)
			typed.push_back(w(100 + i));
		typed.push_back(w(20, Common::kKoreanRoleNone, true));
		Common::Array<Agi::AgiSentence> c;
		Agi::buildKoreanCandidates(typed, Agi::AgiPrepositions(), 3, c);
		for (uint i = 0; i < c.size(); i++)
			TS_ASSERT_LESS_THAN_EQUALS(c[i].size(), 3u);
		TS_ASSERT_LESS_THAN_EQUALS(c.size(), 4u);
	}
};
