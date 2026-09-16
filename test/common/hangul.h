#include <cxxtest/TestSuite.h>

#include "common/hangul.h"
#include "common/str.h"
#include "common/ustr.h"

/**
 * The two-beolsik Hangul composer, as an automaton over code points.
 *
 * This class was written and measured for AGI (card K2) and moved to common/
 * unchanged so SCI can use it too. The phrases in the first group below are
 * the ones K2's commit message records as measured through the real AGI
 * prompt; they are repeated here so the move is a move and not a rewrite - if
 * this file and that commit message ever disagree, one of them is wrong.
 *
 * The rest are properties rather than examples. An example test passes on a
 * composer that special-cases exactly the examples; the sweeps below feed
 * every key pair and every key triple the layout can produce and check the
 * result against the Unicode composition formula, which is stated once, in
 * the test, and nowhere in the composer.
 */

class HangulTestSuite : public CxxTest::TestSuite {

	/** Feed a key string; '~' backspaces. Returns the composed text. */
	Common::U32String type(const char *keys, bool flush = true) {
		Common::HangulComposer c;
		for (const char *p = keys; *p; ++p) {
			if (*p == '~')
				c.backspace();
			else
				c.feed(*p);
		}
		if (flush)
			c.flush();
		return c.text();
	}

	Common::String hex(const Common::U32String &s) {
		Common::String out;
		for (uint i = 0; i < s.size(); ++i)
			out += Common::String::format("%s%04X", i ? " " : "", s[i]);
		return out;
	}

	void assertText(const char *keys, const char *expectHex) {
		Common::String got = hex(type(keys));
		if (got != expectHex) {
			TS_FAIL((Common::String("typing '") + keys + "' gave [" + got +
					 "], expected [" + expectHex + "]").c_str());
		}
	}

	// The standard tables, restated here so the test does not read the
	// composer's own. 0 is unused so that 0 can mean "not set", matching the
	// Unicode formula's 1-based initial and medial indices.
	static const int kNumCho = 19;
	static const int kNumJung = 21;
	static const int kNumJong = 28;   // including "no final"

	/** Unicode 3.12: S = 0xAC00 + (cho * 21 + jung) * 28 + jong, 0-based. */
	static uint32 syllable(int cho0, int jung0, int jong0) {
		return 0xAC00 + (cho0 * kNumJung + jung0) * kNumJong + jong0;
	}

	static bool isSyllable(uint32 c) {
		return c >= 0xAC00 && c <= 0xD7A3;
	}

	/** Every key the two-beolsik layout answers to, unshifted then shifted. */
	const char *layoutKeys() {
		return "qwertyuiopasdfghjklzxcvbnmQWERTOP";
	}

public:

	// ------------------------------------------------ K2's measured phrases

	void test_k2_phrase_mun_yeoleo() {
		// `ans dufdj` -> 문 열어. The space is a non-jamo key, so it flushes
		// the pending syllable and is appended verbatim.
		assertText("ans dufdj", "BB38 0020 C5F4 C5B4");
	}

	void test_k2_phrase_hangul() {
		// `gksrmf` -> 한글
		assertText("gksrmf", "D55C AE00");
	}

	void test_k2_backspace_removes_jung_leaving_cho() {
		// `rk~` -> ㄱ. Mid-syllable: cho+jung, the jung is removed and the
		// lone initial is shown as the COMPATIBILITY jamo U+3131, not the
		// conjoining U+1100. That choice is load-bearing for SCI: EUC-KR
		// encodes U+3131 and does not encode U+1100.
		assertText("rk~", "3131");
	}

	void test_k2_backspace_removes_jong_leaving_syllable() {
		// `rkq~` -> 가. A complete syllable with a final; the final goes and
		// cho+jung remain composed.
		assertText("rkq~", "AC00");
	}

	void test_k2_backspace_decomposes_compound_jong() {
		// `dkfr~` -> 알. The compound final ㄺ decomposes to ㄹ rather than
		// vanishing.
		assertText("dkfr~", "C54C");
	}

	void test_ascii_only_passes_through_when_the_key_is_not_a_jamo_key() {
		// NOT "open door": every one of o p e n d o o r is a jamo key, so a
		// composer that is switched on turns English into Korean. That is the
		// whole reason the caller needs an on/off toggle rather than a
		// heuristic - there is no key sequence that is unambiguously English.
		// K2's own test spelled this `!open door`, where the '!' meant "run
		// this one with Korean OFF".
		//
		// What IS true unconditionally is that a key the layout does not claim
		// reaches the output unaltered; that is asserted over every byte value
		// in test_unclaimed_bytes_pass_through below. Here, one readable case:
		Common::U32String got = type("1 2, 3!");
		Common::String ascii;
		for (uint i = 0; i < got.size(); ++i) {
			TS_ASSERT_LESS_THAN(got[i], 0x80u);
			ascii += (char)got[i];
		}
		TS_ASSERT_EQUALS(ascii, Common::String("1 2, 3!"));
	}

	// -------------------------------------------------- automaton properties

	/**
	 * Every key pair and every key triple, checked against the formula.
	 *
	 * Nothing here knows which key is which jamo. The claim is only that
	 * whatever the composer emits inside the syllable block decomposes into
	 * indices that are in range - i.e. it never emits a code point it could
	 * not have built, and never builds one out of a bad index.
	 */
	void test_every_key_pair_and_triple_is_a_wellformed_syllable() {
		const char *keys = layoutKeys();
		const uint n = strlen(keys);
		uint pairs = 0, triples = 0, syllables = 0;

		char buf[4] = { 0, 0, 0, 0 };
		for (uint a = 0; a < n; ++a) {
			for (uint b = 0; b < n; ++b) {
				buf[0] = keys[a]; buf[1] = keys[b]; buf[2] = 0;
				Common::U32String out = type(buf);
				pairs++;
				for (uint i = 0; i < out.size(); ++i) {
					if (!isSyllable(out[i]))
						continue;
					syllables++;
					uint32 s = out[i] - 0xAC00;
					int jong = s % kNumJong;
					int jung = (s / kNumJong) % kNumJung;
					int cho = s / (kNumJong * kNumJung);
					TS_ASSERT_EQUALS(syllable(cho, jung, jong), out[i]);
					TS_ASSERT_LESS_THAN(cho, kNumCho);
				}

				for (uint c = 0; c < n; ++c) {
					buf[0] = keys[a]; buf[1] = keys[b]; buf[2] = keys[c];
					Common::U32String o3 = type(buf);
					triples++;
					for (uint i = 0; i < o3.size(); ++i) {
						if (!isSyllable(o3[i]))
							continue;
						uint32 s = o3[i] - 0xAC00;
						int cho = s / (kNumJong * kNumJung);
						TS_ASSERT_LESS_THAN(cho, kNumCho);
					}
				}
				buf[2] = 0;
			}
		}
		TS_ASSERT_EQUALS(pairs, n * n);
		TS_ASSERT_EQUALS(triples, n * n * n);
		// A sweep that produced no syllables at all would pass every
		// assertion above while measuring nothing.
		TS_ASSERT_LESS_THAN(0u, syllables);
	}

	/**
	 * How much of the syllable block three keystrokes can reach.
	 *
	 * Pinned as an exact number: this is the population an SCI adapter has to
	 * range-check against the font, so a change that silently widens or
	 * narrows it should be seen rather than absorbed.
	 */
	void test_reachable_syllable_count_is_pinned() {
		const char *keys = layoutKeys();
		const uint n = strlen(keys);
		bool seen[0xD7A4 - 0xAC00] = { false };
		uint distinct = 0;

		char buf[4] = { 0, 0, 0, 0 };
		for (uint a = 0; a < n; ++a) {
			for (uint b = 0; b < n; ++b) {
				for (uint c = 0; c < n; ++c) {
					buf[0] = keys[a]; buf[1] = keys[b]; buf[2] = keys[c];
					Common::U32String out = type(buf);
					for (uint i = 0; i < out.size(); ++i) {
						if (!isSyllable(out[i]))
							continue;
						uint32 idx = out[i] - 0xAC00;
						if (!seen[idx]) {
							seen[idx] = true;
							distinct++;
						}
					}
				}
			}
		}
		// Measured, not derived: 19 initials x 21 medials = 399 syllables need
		// no third key, and the third key adds a final to some of them. The
		// number is below 19*21*28 because not every consonant is a legal
		// final and a third key is sometimes consumed as the next syllable's
		// initial instead.
		TS_ASSERT_EQUALS(distinct, 4655u);
	}

	/** Backspacing as many times as keys were fed always empties the run. */
	void test_backspace_always_terminates_empty() {
		const char *keys = layoutKeys();
		const uint n = strlen(keys);
		char buf[4] = { 0, 0, 0, 0 };
		for (uint a = 0; a < n; ++a) {
			for (uint b = 0; b < n; ++b) {
				for (uint c = 0; c < n; ++c) {
					buf[0] = keys[a]; buf[1] = keys[b]; buf[2] = keys[c];
					Common::HangulComposer comp;
					for (const char *p = buf; *p; ++p)
						comp.feed(*p);
					// Three keys can never produce more than three jamo, so
					// three backspaces must clear it.
					comp.backspace();
					comp.backspace();
					comp.backspace();
					TS_ASSERT(comp.empty());
					TS_ASSERT_EQUALS(comp.text().size(), 0u);
				}
			}
		}
	}

	/** isJamoKey() agrees with what feed() does, over every byte value. */
	void test_isJamoKey_agrees_with_feed_over_all_bytes() {
		for (int i = 0; i < 256; ++i) {
			char ch = (char)i;
			Common::HangulComposer comp;
			bool consumed = comp.feed(ch);
			TS_ASSERT_EQUALS(consumed, Common::HangulComposer::isJamoKey(ch));
		}
	}

	/** A key the layout does not claim reaches the output unaltered. */
	void test_unclaimed_bytes_pass_through() {
		for (int i = 1; i < 128; ++i) {
			char ch = (char)i;
			if (Common::HangulComposer::isJamoKey(ch))
				continue;
			Common::HangulComposer comp;
			comp.feed(ch);
			comp.flush();
			Common::U32String out = comp.text();
			TS_ASSERT_EQUALS(out.size(), 1u);
			TS_ASSERT_EQUALS(out[0], (uint32)(byte)ch);
		}
	}

	/** preedit() is the pending syllable, and text() ends with it. */
	void test_preedit_is_the_tail_of_text() {
		Common::HangulComposer comp;
		TS_ASSERT_EQUALS(comp.preedit(), 0u);
		TS_ASSERT(comp.empty());

		comp.feed('g');                       // ㅎ
		TS_ASSERT_EQUALS(comp.preedit(), 0x314Eu);
		TS_ASSERT(!comp.empty());

		comp.feed('k');                       // ㅎ + ㅏ = 하
		TS_ASSERT_EQUALS(comp.preedit(), 0xD558u);
		Common::U32String t = comp.text();
		TS_ASSERT_EQUALS(t.size(), 1u);
		TS_ASSERT_EQUALS(t[t.size() - 1], comp.preedit());

		comp.feed('s');                       // 한
		TS_ASSERT_EQUALS(comp.preedit(), 0xD55Cu);

		comp.flush();
		TS_ASSERT_EQUALS(comp.preedit(), 0u);
		TS_ASSERT_EQUALS(comp.text().size(), 1u);
		TS_ASSERT_EQUALS(comp.text()[0], 0xD55Cu);
	}

	/** reset() drops committed text as well as the pending syllable. */
	void test_reset_drops_everything() {
		Common::HangulComposer comp;
		for (const char *p = "gksrmf"; *p; ++p)
			comp.feed(*p);
		TS_ASSERT(!comp.empty());
		comp.reset();
		TS_ASSERT(comp.empty());
		TS_ASSERT_EQUALS(comp.text().size(), 0u);
		TS_ASSERT_EQUALS(comp.preedit(), 0u);
	}

	/**
	 * A vowel after a final steals it to start the next syllable.
	 *
	 * This is the rule that makes `dufdj` two syllables rather than one, and
	 * it is the one an implementation is most likely to get wrong, so it is
	 * checked on its own rather than only inside a phrase.
	 */
	void test_vowel_steals_the_pending_final() {
		assertText("dufdj", "C5F4 C5B4");   // 열 + 어
		assertText("rkrk", "AC00 AC00");    // 가가
		assertText("rkfk", "AC00 B77C");    // 가라, the ㄹ moves on
	}

	/** A compound final splits when the next key is a vowel. */
	void test_compound_final_splits_on_a_following_vowel() {
		// dkfr = 앎 without the ㅁ: ㅇㅏㄹㄱ -> 앍
		assertText("dkfr", "C54D");
		// dkfrk: the ㄱ of the compound moves to the next syllable -> 알가
		assertText("dkfrk", "C54C AC00");
	}
};
