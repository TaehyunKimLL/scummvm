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

#ifndef SCI_PARSER_KORSTEM_H
#define SCI_PARSER_KORSTEM_H

#include "common/scummsys.h"
#include "common/ustr.h"

namespace Sci {

/**
 * The readings of one typed word, as a fixed buffer: no heap, and small
 * enough to live on the stack of a parser lookup on DOS. Hangul syllables
 * are BMP code points, so 16 bits hold each one.
 */
struct KoreanStems {
	enum { kMaxCount = 48, kMaxLen = 16 };

	uint16 cp[kMaxCount][kMaxLen];
	uint8 len[kMaxCount];
	uint count;

	KoreanStems() : count(0) {}

	/** Candidate @p i as a string; empty past the end. */
	Common::U32String get(uint i) const;
};

/**
 * Dictionary-form candidates for an inflected Korean word.
 *
 * The map holds citation forms (열다, 가방) while a player types what a
 * Korean sentence needs: 연다, 열어봐, 열어주세요, 가방을, 방에서는. The
 * rules undo that in layers - final ending, auxiliary verb (어 보다, 어
 * 주다, 고 있다), prefinal ending (았/었, 시, 겠), then the stem's own
 * shape: a fused vowel (봐, 줘, 해, 써, 잠가), a dropped ㄹ (연다, 엽니다,
 * 여세요) and the ㄷ, ㅂ, ㅅ, 르, ㅎ irregulars (들어, 도와, 지어, 불러,
 * 그래). Nouns lose case markers and auxiliary particles, two deep, with
 * the allomorph rule (을 after a consonant, 를 after a vowel) deciding
 * which strip is even tried.
 *
 * Every candidate is a reading of the typed word, never a form made up
 * and then searched for: a candidate only matters when it hits an entry
 * that already exists. The order is the preference when several do:
 * explicit endings first; after a separate 어/아 the ㄷ and ㅅ irregular
 * readings before the regular stem (걸어 is walk before hang, 지어 build
 * before lose) and elsewhere the regular stem first; particle strips
 * last so a verb form never loses to a particle that looks like its
 * ending.
 *
 * Works on code points throughout - the rules are syllable arithmetic on
 * precomposed Hangul - and allocates nothing: the tables are code points,
 * the temporaries fit a U32String's inline storage, and the result is a
 * fixed buffer.
 *
 * @param word   the typed word, decoded; longer than KoreanStems::kMaxLen
 *               or not ending in a Hangul syllable yields nothing
 * @param out    receives the candidates, best first
 * @return number of candidates written
 */
uint koreanStemCandidates(const Common::U32String &word, KoreanStems &out);

} // End of namespace Sci

#endif // SCI_PARSER_KORSTEM_H
