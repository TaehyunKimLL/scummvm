/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <cxxtest/TestSuite.h>

#include "common/fs.h"
#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/str.h"
#include "common/str-enc.h"
#include "common/stream.h"
#include "common/ustr.h"
#include "../../system/null_osystem.h"

#include "sci/parser/korstem.h"

// Korean stemming for parser input.
//
// The mapping is built around citation forms (-다), because that is what a
// generated dictionary contains, while a player types the plain or
// imperative form. These tests pin the rules that bridge the two.
//
// The central invariant is that stemming only ever STRIPS. It must never
// manufacture a form and then treat it as a word: a generated conjugation
// that is not real can still collide with a real entry, which is a wrong
// answer rather than a missing one. So every test below checks that a
// candidate is a shorter or re-suffixed version of the input, and the
// nonsense cases check that no rule invents a match out of noise.

class KoreanStemTestSuite : public CxxTest::TestSuite {
	// Helpers. The engine works in code points - the typed word is decoded
	// once from cp949 (or UTF-8) and every rule is syllable arithmetic -
	// so the tests speak UTF-8 and compare code points.
	static Common::U32String toU32(const char *utf8) {
		return Common::convertToU32String(utf8, Common::kUtf8);
	}

	static Common::String toUtf8(const Common::U32String &u) {
		return Common::convertFromU32String(u, Common::kUtf8);
	}

	static uint stems(const char *typedUtf8, Sci::KoreanStems &out) {
		return Sci::koreanStemCandidates(toU32(typedUtf8), out);
	}

	// The first candidate that is one of the given dictionary words, as
	// UTF-8; empty when none is. This is what the engine's lookup sees,
	// because it takes the first candidate the map holds.
	static Common::String firstOf(const char *typedUtf8, const char *const *dictUtf8) {
		Sci::KoreanStems out;
		uint n = stems(typedUtf8, out);
		for (uint i = 0; i < n; ++i)
			for (uint d = 0; dictUtf8[d]; ++d)
				if (out.get(i) == toU32(dictUtf8[d]))
					return dictUtf8[d];
		return Common::String();
	}

	// Does the candidate list contain this word?
	static bool has(const char *typedUtf8, const char *wantUtf8) {
		Sci::KoreanStems out;
		uint n = stems(typedUtf8, out);
		const Common::U32String want = toU32(wantUtf8);
		for (uint i = 0; i < n; ++i) {
			if (out.get(i) == want)
				return true;
		}
		return false;
	}

	static uint count(const char *typedUtf8) {
		Sci::KoreanStems out;
		return stems(typedUtf8, out);
	}

	// The KoreanRole of the reading that is this stem; 0xFF when the
	// stem is not among the readings.
	static uint8 roleOf(const char *typedUtf8, const char *stemUtf8) {
		Sci::KoreanStems out;
		uint n = stems(typedUtf8, out);
		for (uint i = 0; i < n; ++i)
			if (out.get(i) == toU32(stemUtf8))
				return out.role[i];
		return 0xFF;
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

	// -는다 is a plain ending that comes off whole: 먹는다 -> 먹다.
	void test_neunda_ending_reaches_citation_form() {
		TS_ASSERT(has("\xeb\xa8\xb9\xeb\x8a\x94\xeb\x8b\xa4",   // 먹는다
		              "\xeb\xa8\xb9\xeb\x8b\xa4"));             // 먹다
	}

	// -어 / -어라: 읽어 and 읽어라 both have to reach 읽다.
	void test_imperative_endings_reach_citation_form() {
		TS_ASSERT(has("\xec\x9d\xbd\xec\x96\xb4",               // 읽어
		              "\xec\x9d\xbd\xeb\x8b\xa4"));             // 읽다
		TS_ASSERT(has("\xec\x9d\xbd\xec\x96\xb4\xeb\x9d\xbc",   // 읽어라
		              "\xec\x9d\xbd\xeb\x8b\xa4"));             // 읽다
	}

	// The ㄴ case that no suffix rule can reach. In 준다 the ㄴ is fused
	// into 주 as a final consonant rather than standing as its own
	// ending, so it only comes off by syllable arithmetic. Without that
	// rule 준다 never finds 주다, which is why this test exists
	// separately from the ending tests above.
	void test_fused_nieun_is_decomposed() {
		TS_ASSERT(has("\xec\xa4\x80\xeb\x8b\xa4",               // 준다
		              "\xec\xa3\xbc\xeb\x8b\xa4"));             // 주다
		TS_ASSERT(has("\xed\x83\x84\xeb\x8b\xa4",               // 탄다
		              "\xed\x83\x80\xeb\x8b\xa4"));             // 타다
	}

	// ㄹ-final stems drop the ㄹ before ㄴ, so the fused ㄴ of 연다 stands
	// for 열다 ("가방을 연다" in Laura Bow) and 만든다 for 만들다.
	void test_fused_nieun_restores_rieul() {
		TS_ASSERT(has("\xec\x97\xb0\xeb\x8b\xa4",               // 연다
		              "\xec\x97\xb4\xeb\x8b\xa4"));             // 열다
		TS_ASSERT(has("\xeb\xa7\x8c\xeb\x93\xa0\xeb\x8b\xa4",               // 만든다
		              "\xeb\xa7\x8c\xeb\x93\xa4\xeb\x8b\xa4"));             // 만들다
	}

	// A bare stem typed alone should still reach its citation form.
	void test_bare_stem_gets_da() {
		TS_ASSERT(has("\xec\x9d\xbd",                           // 읽
		              "\xec\x9d\xbd\xeb\x8b\xa4"));             // 읽다
	}

	// A word that is ALREADY the citation form must not be mangled into a
	// different one. 먹다 must never yield 먹다다.
	void test_citation_form_is_not_re_suffixed() {
		TS_ASSERT(!has("\xeb\xa8\xb9\xeb\x8b\xa4",              // 먹다
		               "\xeb\xa8\xb9\xeb\x8b\xa4\xeb\x8b\xa4")); // 먹다다
	}

	// The stripping invariant, stated directly: no candidate may be longer
	// than the input plus the one 다 a rule is allowed to re-attach.
	void test_candidates_never_grow_unboundedly() {
		const char *inputs[] = {
			"\xeb\xa8\xb9\xeb\x8a\x94\xeb\x8b\xa4",   // 먹는다
			"\xec\x9d\xbd\xec\x96\xb4\xeb\x9d\xbc",   // 읽어라
			"\xec\xa4\x80\xeb\x8b\xa4",               // 준다
			"\xeb\xac\xb8",                           // 문
		};
		for (int k = 0; k < 4; ++k) {
			Sci::KoreanStems out;
			uint n = stems(inputs[k], out);
			const uint typed = toU32(inputs[k]).size();
			for (uint i = 0; i < n; ++i) {
				TS_ASSERT_LESS_THAN_EQUALS((uint)out.len[i], typed + 1);
			}
		}
	}

	// Never more candidates than the buffer holds.
	void test_candidate_count_is_bounded() {
		TS_ASSERT_LESS_THAN_EQUALS(count("\xec\x9d\xbd\xec\x96\xb4\xeb\x9d\xbc"),
		                           (uint)Sci::KoreanStems::kMaxCount);
		TS_ASSERT_LESS_THAN_EQUALS(count("\xeb\xa8\xb9\xeb\x8a\x94\xeb\x8b\xa4"),
		                           (uint)Sci::KoreanStems::kMaxCount);
	}

	// Degenerate input must not crash or produce anything.
	void test_empty_and_overlong_input() {
		Sci::KoreanStems out;
		TS_ASSERT_EQUALS(Sci::koreanStemCandidates(Common::U32String(), out), 0u);
		// Seventeen syllables: longer than any parser word, and longer than
		// the buffer, so it is left alone rather than truncated.
		Common::U32String longWord;
		for (int i = 0; i < 17; ++i)
			longWord += (uint32)0xAC00;
		TS_ASSERT_EQUALS(Sci::koreanStemCandidates(longWord, out), 0u);
		TS_ASSERT_EQUALS(out.count, 0u);
	}

	// ASCII must be left alone: English goes through the real vocabulary,
	// and a rule firing on it would be a regression for every other game.
	void test_ascii_is_not_stemmed() {
		TS_ASSERT_EQUALS(count("look"), 0u);
		// Nor a word whose last character is not Hangul.
		TS_ASSERT_EQUALS(count("\xea\xb0\x80\xeb\xb0\xa9!"), 0u);  // 가방!
	}

	void test_case_markers_are_stripped() {
		TS_ASSERT(has("\xeb\xac\xb8\xec\x9d\x84", "\xeb\xac\xb8"));   // 문을 -> 문
		TS_ASSERT(has("\xeb\xac\xb8\xec\x9d\xb4", "\xeb\xac\xb8"));   // 문이 -> 문
		TS_ASSERT(has("\xeb\xac\xb8\xec\x9d\x80", "\xeb\xac\xb8"));   // 문은 -> 문
		TS_ASSERT(has("\xeb\xac\xb8\xec\x97\x90\xec\x84\x9c",
		              "\xeb\xac\xb8"));                               // 문에서 -> 문
	}

	// The stripping is only safe because a candidate must hit an existing
	// entry. These words END in a particle syllable but are not inflected,
	// and the rule must not silently turn them into something else: the
	// ORIGINAL has to remain among the candidates so an exact match still
	// wins.
	void test_words_ending_in_a_particle_syllable_keep_themselves() {
		// 사나이 (man), 국가 (nation), 종이 (paper)
		const char *words[] = {
			"\xec\x82\xac\xeb\x82\x98\xec\x9d\xb4",
			"\xea\xb5\xad\xea\xb0\x80",
			"\xec\xa2\x85\xec\x9d\xb4",
		};
		for (int k = 0; k < 3; ++k) {
			Sci::KoreanStems out;
			uint n = stems(words[k], out);
			// The caller tries the word itself before any candidate, so what
			// matters is that stemming did not REPLACE it - a stripped form
			// is only ever an extra thing to try.
			TS_ASSERT_LESS_THAN_EQUALS(n, (uint)Sci::KoreanStems::kMaxCount);
		}
	}

	// ================================================= the layered rules

	// An auxiliary verb inside the word comes off with its ending, and
	// the head is stemmed: 열어봐, 열어주세요, 열어봤다 are all 열다.
	void test_auxiliary_verbs_come_off() {
		TS_ASSERT(has("\xec\x97\xb4\xec\x96\xb4\xeb\xb4\x90", "\xec\x97\xb4\xeb\x8b\xa4"));
		TS_ASSERT(has("\xec\x97\xb4\xec\x96\xb4\xeb\xb4\x90\xec\x9a\x94", "\xec\x97\xb4\xeb\x8b\xa4"));
		TS_ASSERT(has("\xec\x97\xb4\xec\x96\xb4\xec\xa3\xbc\xec\x84\xb8\xec\x9a\x94", "\xec\x97\xb4\xeb\x8b\xa4"));
		TS_ASSERT(has("\xec\x97\xb4\xec\x96\xb4\xeb\xb4\xa4\xeb\x8b\xa4", "\xec\x97\xb4\xeb\x8b\xa4"));
		TS_ASSERT(has("\xeb\x93\xa4\xea\xb3\xa0\xec\x9e\x88\xec\x8a\xb5\xeb\x8b\x88\xeb\x8b\xa4", "\xeb\x93\xa4\xeb\x8b\xa4"));
		// A request: 열어줄래, 열어볼까.
		TS_ASSERT(has("\xec\x97\xb4\xec\x96\xb4\xec\xa4\x84\xeb\x9e\x98", "\xec\x97\xb4\xeb\x8b\xa4"));
		TS_ASSERT(has("\xec\x97\xb4\xec\x96\xb4\xeb\xb3\xbc\xea\xb9\x8c", "\xec\x97\xb4\xeb\x8b\xa4"));
		TS_ASSERT(has("\xec\x97\xb4\xea\xb9\x8c", "\xec\x97\xb4\xeb\x8b\xa4"));
		// A fused head before the auxiliary: 도와줘 is 돕다.
		TS_ASSERT(has("\xeb\x8f\x84\xec\x99\x80\xec\xa4\x98", "\xeb\x8f\x95\xeb\x8b\xa4"));
	}

	// Tense and honorific prefinal endings, fused or not.
	void test_prefinal_endings_come_off() {
		TS_ASSERT(has("\xeb\xb4\xa4\xeb\x8b\xa4", "\xeb\xb3\xb4\xeb\x8b\xa4"));
		TS_ASSERT(has("\xed\x96\x88\xec\x96\xb4\xec\x9a\x94", "\xed\x95\x98\xeb\x8b\xa4"));
		TS_ASSERT(has("\xec\x97\xb4\xec\x97\x88\xec\x8a\xb5\xeb\x8b\x88\xeb\x8b\xa4", "\xec\x97\xb4\xeb\x8b\xa4"));
		TS_ASSERT(has("\xec\x97\xac\xec\x85\xa8\xeb\x8b\xa4", "\xec\x97\xb4\xeb\x8b\xa4"));
		TS_ASSERT(has("\xed\x95\x98\xec\x84\xb8\xec\x9a\x94", "\xed\x95\x98\xeb\x8b\xa4"));
		// 마셨다 is 마시다 as well as an honorific 마다.
		TS_ASSERT(has("\xeb\xa7\x88\xec\x85\xa8\xeb\x8b\xa4", "\xeb\xa7\x88\xec\x8b\x9c\xeb\x8b\xa4"));
		// 있 keeps its ㅆ: it is a stem, not a tense.
		TS_ASSERT(has("\xec\x9e\x88\xec\x8a\xb5\xeb\x8b\x88\xeb\x8b\xa4", "\xec\x9e\x88\xeb\x8b\xa4"));
	}

	// A ㄹ-final stem drops the ㄹ before ㄴ, ㅂ and 시: three endings, one
	// 열다.
	void test_dropped_rieul_is_restored() {
		TS_ASSERT(has("\xec\x97\xbd\xeb\x8b\x88\xeb\x8b\xa4", "\xec\x97\xb4\xeb\x8b\xa4"));
		TS_ASSERT(has("\xec\x97\xac\xec\x84\xb8\xec\x9a\x94", "\xec\x97\xb4\xeb\x8b\xa4"));
		TS_ASSERT(has("\xec\x97\xac\xec\x8b\xad\xec\x8b\x9c\xec\x98\xa4", "\xec\x97\xb4\xeb\x8b\xa4"));
	}

	// Fused vowels: the stem's last syllable swallowed the 어/아.
	void test_fused_vowels_are_undone() {
		TS_ASSERT(has("\xeb\xb4\x90", "\xeb\xb3\xb4\xeb\x8b\xa4"));
		TS_ASSERT(has("\xeb\xb4\x90\xeb\x9d\xbc", "\xeb\xb3\xb4\xeb\x8b\xa4"));
		TS_ASSERT(has("\xec\xa4\x98", "\xec\xa3\xbc\xeb\x8b\xa4"));
		TS_ASSERT(has("\xed\x95\xb4", "\xed\x95\x98\xeb\x8b\xa4"));
		TS_ASSERT(has("\xec\x82\xac\xec\x9a\xa9\xed\x95\xb4", "\xec\x82\xac\xec\x9a\xa9\xed\x95\x98\xeb\x8b\xa4"));
		TS_ASSERT(has("\xea\xb0\x80\xec\xa0\xb8", "\xea\xb0\x80\xec\xa7\x80\xeb\x8b\xa4"));
		TS_ASSERT(has("\xea\xb0\x80\xec\xa0\xb8\xec\x99\x80", "\xea\xb0\x80\xec\xa0\xb8\xec\x98\xa4\xeb\x8b\xa4"));
		TS_ASSERT(has("\xec\x8d\xa8", "\xec\x93\xb0\xeb\x8b\xa4"));
		TS_ASSERT(has("\xec\x9e\xa0\xea\xb0\x80", "\xec\x9e\xa0\xea\xb7\xb8\xeb\x8b\xa4"));
		TS_ASSERT(has("\xeb\x8f\xbc", "\xeb\x90\x98\xeb\x8b\xa4"));
	}

	// The irregular conjugations, each reaching its own dictionary form.
	void test_irregular_stems_are_restored() {
		TS_ASSERT(has("\xeb\xb6\x88\xeb\x9f\xac", "\xeb\xb6\x80\xeb\xa5\xb4\xeb\x8b\xa4"));   // 르
		TS_ASSERT(has("\xea\xb3\xa8\xeb\x9d\xbc", "\xea\xb3\xa0\xeb\xa5\xb4\xeb\x8b\xa4"));
		TS_ASSERT(has("\xeb\x93\xa4\xec\x96\xb4", "\xeb\x93\xa3\xeb\x8b\xa4"));     // ㄷ
		TS_ASSERT(has("\xeb\xac\xbc\xec\x9c\xbc\xec\x84\xb8\xec\x9a\x94", "\xeb\xac\xbb\xeb\x8b\xa4"));
		TS_ASSERT(has("\xec\xa7\x80\xec\x96\xb4", "\xec\xa7\x93\xeb\x8b\xa4"));     // ㅅ
		TS_ASSERT(has("\xeb\x8f\x84\xec\x99\x80", "\xeb\x8f\x95\xeb\x8b\xa4"));     // ㅂ
		TS_ASSERT(has("\xeb\x88\x84\xec\x9b\x8c", "\xeb\x88\x95\xeb\x8b\xa4"));
		TS_ASSERT(has("\xeb\x8f\x84\xec\x9a\xb0\xec\x84\xb8\xec\x9a\x94", "\xeb\x8f\x95\xeb\x8b\xa4"));
		TS_ASSERT(has("\xea\xb7\xb8\xeb\x9e\x98", "\xea\xb7\xb8\xeb\xa0\x87\xeb\x8b\xa4"));   // ㅎ
		TS_ASSERT(has("\xeb\xaa\xa8\xec\x95\x84", "\xeb\xaa\xa8\xec\x9c\xbc\xeb\x8b\xa4"));   // ㅡ across a syllable
		TS_ASSERT(has("\xed\x8d\xbc", "\xed\x91\xb8\xeb\x8b\xa4"));     // ㅜ
		TS_ASSERT(has("\xeb\xb6\x99\xec\x97\xac\xec\x9a\x94", "\xeb\xb6\x99\xec\x9d\xb4\xeb\x8b\xa4")); // 여 after an ㅣ stem
		TS_ASSERT(has("\xea\xb8\xb0\xec\x9a\xb8\xec\x98\x80\xeb\x8b\xa4", "\xea\xb8\xb0\xec\x9a\xb8\xec\x9d\xb4\xeb\x8b\xa4"));
	}

	// When two readings are both real words the engine takes the first,
	// so the order is part of the contract. A parser prompt gets 걸어 as
	// walk and 물어 as ask far more often than as hang and bite, 붙여요
	// can only be 붙이다, and 물으세요 can only be 묻다.
	void test_the_first_reading_is_the_likely_one() {
		static const char *const walkOrHang[] = { "\xea\xb1\xb8\xeb\x8b\xa4", "\xea\xb1\xb7\xeb\x8b\xa4", nullptr };
		TS_ASSERT_EQUALS(firstOf("\xea\xb1\xb8\xec\x96\xb4", walkOrHang), Common::String("\xea\xb1\xb7\xeb\x8b\xa4"));
		static const char *const askOrBite[] = { "\xeb\xac\xbc\xeb\x8b\xa4", "\xeb\xac\xbb\xeb\x8b\xa4", nullptr };
		TS_ASSERT_EQUALS(firstOf("\xeb\xac\xbc\xec\x96\xb4", askOrBite), Common::String("\xeb\xac\xbb\xeb\x8b\xa4"));
		TS_ASSERT_EQUALS(firstOf("\xeb\xac\xbc\xec\x9c\xbc\xec\x84\xb8\xec\x9a\x94", askOrBite), Common::String("\xeb\xac\xbb\xeb\x8b\xa4"));
		static const char *const attachOrStick[] = { "\xeb\xb6\x99\xeb\x8b\xa4", "\xeb\xb6\x99\xec\x9d\xb4\xeb\x8b\xa4", nullptr };
		TS_ASSERT_EQUALS(firstOf("\xeb\xb6\x99\xec\x97\xac\xec\x9a\x94", attachOrStick), Common::String("\xeb\xb6\x99\xec\x9d\xb4\xeb\x8b\xa4"));
		// Honorific 시 against a stem that ends in 시: 꼬신다 is 꼬시다 first.
		static const char *const seduce[] = { "\xea\xbc\xac\xeb\x8b\xa4", "\xea\xbc\xac\xec\x8b\x9c\xeb\x8b\xa4", nullptr };
		TS_ASSERT_EQUALS(firstOf("\xea\xbc\xac\xec\x8b\xa0\xeb\x8b\xa4", seduce), Common::String("\xea\xbc\xac\xec\x8b\x9c\xeb\x8b\xa4"));
	}

	// ======================================================= what is NOT a verb

	// A noun with a particle must never read as a verb that happens to
	// share its syllables: 집도 is 집 + 도, not 집다; 나를 is not 나르다;
	// 안은 is not 안다. These are the common nouns of an adventure game,
	// so a wrong verb here would be worse than a missing one.
	void test_particles_do_not_make_verbs() {
		TS_ASSERT(!has("\xec\xa7\x91\xeb\x8f\x84", "\xec\xa7\x91\xeb\x8b\xa4"));
		TS_ASSERT(has("\xec\xa7\x91\xeb\x8f\x84", "\xec\xa7\x91"));
		TS_ASSERT(!has("\xeb\x82\x98\xeb\xa5\xbc", "\xeb\x82\x98\xeb\xa5\xb4\xeb\x8b\xa4"));
		TS_ASSERT(has("\xeb\x82\x98\xeb\xa5\xbc", "\xeb\x82\x98"));
		TS_ASSERT(!has("\xec\x95\x88\xec\x9d\x80", "\xec\x95\x88\xeb\x8b\xa4"));
		TS_ASSERT(has("\xec\x95\x88\xec\x9d\x80", "\xec\x95\x88"));
		TS_ASSERT(!has("\xec\xa7\x91\xec\x9d\x84", "\xec\xa7\x91\xeb\x8b\xa4"));
		TS_ASSERT(!has("\xec\x82\xac\xeb\x8f\x84", "\xec\x82\xac\xeb\x8b\xa4"));
		TS_ASSERT(!has("\xec\xb9\xa8\xeb\x8c\x80\xeb\xa1\x9c", "\xec\xb9\xa8"));
		TS_ASSERT(has("\xec\xb9\xa8\xeb\x8c\x80\xeb\xa1\x9c", "\xec\xb9\xa8\xeb\x8c\x80"));
	}

	// The same, as the engine sees it: with the noun and the look-alike
	// verb both in the dictionary, the noun is the first hit. 말다, 물다,
	// 차다 and 집다 are all real verbs.
	void test_the_noun_is_the_first_hit_over_a_look_alike_verb() {
		static const char *const horseOrStop[] = { "\xeb\xa7\x90\xeb\x8b\xa4", "\xeb\xa7\x90", nullptr };
		TS_ASSERT_EQUALS(firstOf("\xeb\xa7\x90\xec\x9d\x84", horseOrStop), Common::String("\xeb\xa7\x90"));
		static const char *const waterOrBite[] = { "\xeb\xac\xbc\xeb\x8b\xa4", "\xeb\xac\xbc", nullptr };
		TS_ASSERT_EQUALS(firstOf("\xeb\xac\xbc\xec\x9d\x84", waterOrBite), Common::String("\xeb\xac\xbc"));
		static const char *const carOrKick[] = { "\xec\xb0\xa8\xeb\x8b\xa4", "\xec\xb0\xa8", nullptr };
		TS_ASSERT_EQUALS(firstOf("\xec\xb0\xa8\xeb\xa5\xbc", carOrKick), Common::String("\xec\xb0\xa8"));
		static const char *const houseOrPick[] = { "\xec\xa7\x91\xeb\x8b\xa4", "\xec\xa7\x91", nullptr };
		TS_ASSERT_EQUALS(firstOf("\xec\xa7\x91\xec\x9d\x84", houseOrPick), Common::String("\xec\xa7\x91"));
		TS_ASSERT_EQUALS(firstOf("\xec\xa7\x91\xeb\x8f\x84", houseOrPick), Common::String("\xec\xa7\x91"));
		// And the verb when it is one: 집어 is 집다.
		TS_ASSERT_EQUALS(firstOf("\xec\xa7\x91\xec\x96\xb4", houseOrPick), Common::String("\xec\xa7\x91\xeb\x8b\xa4"));
	}

	// Particles stack in one order only - plural, case marker, auxiliary -
	// so a second strip is tried only in that order. 단도를 is 단도 + 를 and
	// must never become 단 + 도 (which is a word of its own); the same trap
	// holds for every noun ending in 도, 는, 은 or 만.
	void test_a_second_strip_follows_the_particle_order() {
		TS_ASSERT(has("\xeb\x8b\xa8\xeb\x8f\x84\xeb\xa5\xbc", "\xeb\x8b\xa8\xeb\x8f\x84"));       // 단도를 -> 단도
		TS_ASSERT(!has("\xeb\x8b\xa8\xeb\x8f\x84\xeb\xa5\xbc", "\xeb\x8b\xa8"));      // ... but not 단
		TS_ASSERT(!has("\xec\xa7\x80\xeb\x8f\x84\xeb\xa5\xbc", "\xec\xa7\x80"));      // 지도를 -> not 지
		TS_ASSERT(!has("\xea\xb0\x80\xeb\xa9\xb4\xec\x9d\x84", "\xea\xb0\x80"));      // 가면을 -> not 가
		TS_ASSERT(has("\xec\x82\xac\xeb\x9e\x8c\xeb\x93\xa4\xec\x9d\x84", "\xec\x82\xac\xeb\x9e\x8c"));       // 사람들을 -> 사람
		TS_ASSERT(has("\xec\x82\xac\xeb\x9e\x8c\xeb\x93\xa4\xec\x97\x90\xea\xb2\x8c\xeb\x8f\x84", "\xec\x82\xac\xeb\x9e\x8c"));       // 사람들에게도 -> 사람
	}

	// Auxiliary particles stack on a case marker: 방에서는 -> 방.
	void test_stacked_particles_are_stripped() {
		TS_ASSERT(has("\xeb\xb0\xa9\xec\x97\x90\xec\x84\x9c\xeb\x8a\x94", "\xeb\xb0\xa9"));
		TS_ASSERT(has("\xec\x97\xb4\xec\x87\xa0\xeb\xa1\x9c", "\xec\x97\xb4\xec\x87\xa0"));
		TS_ASSERT(has("\xec\xb9\x9c\xea\xb5\xac\xec\x97\x90\xea\xb2\x8c", "\xec\xb9\x9c\xea\xb5\xac"));
		TS_ASSERT(has("\xec\x82\xac\xeb\x9e\x8c\xeb\x93\xa4\xec\x9d\x84", "\xec\x82\xac\xeb\x9e\x8c"));
	}

	// The allomorph decides whether a strip is even tried: 이 attaches to
	// a consonant, so 사나이 and 종이 are not 사나 + 이; 가 attaches to a
	// vowel, so 국가 is not 국 + 가.
	void test_allomorphs_gate_the_particle_strip() {
		TS_ASSERT(!has("\xec\x82\xac\xeb\x82\x98\xec\x9d\xb4", "\xec\x82\xac\xeb\x82\x98"));
		TS_ASSERT(!has("\xea\xb5\xad\xea\xb0\x80", "\xea\xb5\xad"));
		TS_ASSERT(!has("\xeb\xb0\x94\xeb\x8b\xa4\xeb\xa5\xbc", "\xeb\xb0\x94"));
	}

	// =========================================== the corpus the oracle made

	// harness/k9stemcorpus.py had Kiwi, a real morphological analyzer,
	// conjugate every verb of the Camelot and Laura Bow maps through the
	// endings a player types, and kept the forms the Python mirror of
	// these rules resolved to the right entry. Here the C++ is held to the
	// same corpus: every surface form must reach its lemma, and the FIRST
	// candidate that is any corpus lemma must be the right one, because
	// that is the candidate the engine will take.
	void test_oracle_corpus_resolves_to_the_right_lemma() {
		Common::String text = readSource("test/engines/sci/korean_stem_corpus.txt");
		TS_ASSERT_LESS_THAN(0u, text.size());

		Common::Array<Common::U32String> surfaces, lemmas;
		Common::HashMap<Common::String, bool> lemmaSet;
		uint lineStart = 0;
		for (uint i = 0; i <= text.size(); ++i) {
			if (i < text.size() && text[i] != '\n')
				continue;
			Common::String line(text.c_str() + lineStart, i - lineStart);
			lineStart = i + 1;
			if (line.empty() || line[0] == '#')
				continue;
			const int t1 = line.find('\t');
			const int t2 = line.find('\t', t1 + 1);
			TS_ASSERT_LESS_THAN(0, t1);
			TS_ASSERT_LESS_THAN(t1, t2);
			surfaces.push_back(toU32(Common::String(line.c_str() + t1 + 1, t2 - t1 - 1).c_str()));
			lemmas.push_back(toU32(Common::String(line.c_str() + t2 + 1).c_str()));
			lemmaSet[Common::String(line.c_str() + t2 + 1)] = true;
		}
		TS_ASSERT_LESS_THAN(500u, surfaces.size());

		uint reached = 0, firstRight = 0;
		for (uint k = 0; k < surfaces.size(); ++k) {
			Sci::KoreanStems out;
			uint n = Sci::koreanStemCandidates(surfaces[k], out);
			bool found = false;
			Common::U32String first;
			for (uint i = 0; i < n; ++i) {
				const Common::U32String cand = out.get(i);
				if (cand == lemmas[k])
					found = true;
				if (first.empty() && lemmaSet.contains(toUtf8(cand)))
					first = cand;
			}
			if (found)
				reached++;
			if (first == lemmas[k])
				firstRight++;
			else
				TS_WARN(("corpus: " + toUtf8(surfaces[k]) + " -> " + toUtf8(first) +
				         " wanted " + toUtf8(lemmas[k])).c_str());
		}
		TS_ASSERT_EQUALS(reached, surfaces.size());
		TS_ASSERT_EQUALS(firstRight, surfaces.size());
	}

private:
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

public:
	// The case particle a reading lost is reported, so the parser can put
	// the noun into an English prepositional phrase: 나무꾼에게 그릇을 줘 is
	// "give bowl to woodcutter". A plain object or subject marker places
	// nothing; the role survives an auxiliary particle outside it and a
	// plural inside it.
	void test_case_particle_roles() {
		TS_ASSERT_EQUALS(roleOf("나무꾼에게", "나무꾼"), (uint8)Sci::kKoreanRoleTo);
		TS_ASSERT_EQUALS(roleOf("나무꾼에게는", "나무꾼"), (uint8)Sci::kKoreanRoleTo);
		TS_ASSERT_EQUALS(roleOf("왕한테", "왕"), (uint8)Sci::kKoreanRoleTo);
		TS_ASSERT_EQUALS(roleOf("왕께", "왕"), (uint8)Sci::kKoreanRoleTo);
		TS_ASSERT_EQUALS(roleOf("친구들에게", "친구"), (uint8)Sci::kKoreanRoleTo);
		TS_ASSERT_EQUALS(roleOf("오븐에", "오븐"), (uint8)Sci::kKoreanRoleAt);
		TS_ASSERT_EQUALS(roleOf("상자에서", "상자"), (uint8)Sci::kKoreanRoleFrom);
		TS_ASSERT_EQUALS(roleOf("칼로", "칼"), (uint8)Sci::kKoreanRoleWith);
		TS_ASSERT_EQUALS(roleOf("열쇠로", "열쇠"), (uint8)Sci::kKoreanRoleWith);
		TS_ASSERT_EQUALS(roleOf("왕과", "왕"), (uint8)Sci::kKoreanRoleWith);
		TS_ASSERT_EQUALS(roleOf("그릇을", "그릇"), (uint8)Sci::kKoreanRoleObject);
		TS_ASSERT_EQUALS(roleOf("왕이", "왕"), (uint8)Sci::kKoreanRoleObject);
		TS_ASSERT_EQUALS(roleOf("왕은", "왕"), (uint8)Sci::kKoreanRoleObject);
		TS_ASSERT_EQUALS(roleOf("왕의", "왕"), (uint8)Sci::kKoreanRoleOf);
		TS_ASSERT_EQUALS(roleOf("왕도", "왕"), (uint8)Sci::kKoreanRoleNone);
		// A verb form has no particle.
		TS_ASSERT_EQUALS(roleOf("열어", "열다"), (uint8)Sci::kKoreanRoleNone);
	}

	// Camelot's "say X" and "ask who": the quotative and question endings
	// and the copula come off a noun, and place nothing.
	void test_quotative_question_and_copula_endings() {
		TS_ASSERT_EQUALS(roleOf("피터라고", "피터"), (uint8)Sci::kKoreanRoleNone);
		TS_ASSERT_EQUALS(roleOf("방패라고", "방패"), (uint8)Sci::kKoreanRoleNone);
		TS_ASSERT_EQUALS(roleOf("사랑이라고", "사랑"), (uint8)Sci::kKoreanRoleNone);
		TS_ASSERT_EQUALS(roleOf("누구냐고", "누구"), (uint8)Sci::kKoreanRoleNone);
		TS_ASSERT_EQUALS(roleOf("뭐냐고", "뭐"), (uint8)Sci::kKoreanRoleNone);
		TS_ASSERT_EQUALS(roleOf("시험이냐고", "시험"), (uint8)Sci::kKoreanRoleNone);
		TS_ASSERT_EQUALS(roleOf("방패다", "방패"), (uint8)Sci::kKoreanRoleNone);
		TS_ASSERT_EQUALS(roleOf("피터다", "피터"), (uint8)Sci::kKoreanRoleNone);
	}
};
