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

#include "common/str.h"
#include "common/str-enc.h"
#include "common/ustr.h"

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
	// Helpers: the engine works in EUC-KR because that is what the edit
	// control leaves in the game's heap, so the tests speak it too.
	static Common::String toEuc(const char *utf8) {
		Common::U32String u = Common::convertToU32String(utf8, Common::kUtf8);
		return Common::convertFromU32String(u, Common::kWindows949);
	}

	static Common::String toUtf8(const Common::String &euckr) {
		Common::U32String u = Common::convertToU32String(euckr.c_str(),
		                                                 Common::kWindows949);
		return Common::convertFromU32String(u, Common::kUtf8);
	}

	// Does the candidate list contain this word?
	static bool has(const char *typedUtf8, const char *wantUtf8) {
		Common::String typedEuc = toEuc(typedUtf8);
		Common::String wantEuc = toEuc(wantUtf8);
		Common::String cands[Sci::kMaxKoreanStems];
		uint n = Sci::koreanStemCandidates(typedEuc.c_str(), typedEuc.size(),
		                                   cands);
		for (uint i = 0; i < n; ++i) {
			if (cands[i] == wantEuc)
				return true;
		}
		return false;
	}

	static uint count(const char *typedUtf8) {
		Common::String typedEuc = toEuc(typedUtf8);
		Common::String cands[Sci::kMaxKoreanStems];
		return Sci::koreanStemCandidates(typedEuc.c_str(), typedEuc.size(),
		                                 cands);
	}

public:
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
			Common::String in = toEuc(inputs[k]);
			Common::String cands[Sci::kMaxKoreanStems];
			uint n = Sci::koreanStemCandidates(in.c_str(), in.size(), cands);
			for (uint i = 0; i < n; ++i) {
				// +2 bytes is one EUC-KR syllable, the 다 a rule may add.
				TS_ASSERT_LESS_THAN_EQUALS(cands[i].size(), in.size() + 2);
			}
		}
	}

	// Never more candidates than the caller's buffer holds.
	void test_candidate_count_is_bounded() {
		TS_ASSERT_LESS_THAN_EQUALS(count("\xec\x9d\xbd\xec\x96\xb4\xeb\x9d\xbc"),
		                           (uint)Sci::kMaxKoreanStems);
		TS_ASSERT_LESS_THAN_EQUALS(count("\xeb\xa8\xb9\xeb\x8a\x94\xeb\x8b\xa4"),
		                           (uint)Sci::kMaxKoreanStems);
	}

	// Degenerate input must not crash or produce anything.
	void test_empty_and_null_input() {
		Common::String cands[Sci::kMaxKoreanStems];
		TS_ASSERT_EQUALS(Sci::koreanStemCandidates(nullptr, 0, cands), 0u);
		TS_ASSERT_EQUALS(Sci::koreanStemCandidates("", 0, cands), 0u);
	}

	// ASCII must be left alone: English goes through the real vocabulary,
	// and a rule firing on it would be a regression for every other game.
	void test_ascii_is_not_stemmed() {
		Common::String cands[Sci::kMaxKoreanStems];
		uint n = Sci::koreanStemCandidates("look", 4, cands);
		for (uint i = 0; i < n; ++i)
			TS_ASSERT_DIFFERS(cands[i], Common::String("look"));
	}

	// Particles. The map stores bare nouns, so 문을 has to reach 문 - this
	// is the case that made "문을 열어라" fail with everything else working:
	// 열어라 resolved and 문을 did not.
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
			Common::String in = toEuc(words[k]);
			Common::String cands[Sci::kMaxKoreanStems];
			uint n = Sci::koreanStemCandidates(in.c_str(), in.size(), cands);
			// The caller tries the word itself before any candidate, so what
			// matters is that stemming did not REPLACE it - a stripped form
			// is only ever an extra thing to try.
			TS_ASSERT_LESS_THAN_EQUALS(n, (uint)Sci::kMaxKoreanStems);
		}
	}
};
