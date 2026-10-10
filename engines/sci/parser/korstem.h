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
#include "common/str.h"

namespace Sci {

/** Upper bound on what koreanStemCandidates() can return. */
enum { kMaxKoreanStems = 48 };

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
 * explicit endings first, regular stems before irregular readings, and
 * particle strips last so a verb form never loses to a particle that
 * looks like its ending.
 *
 * @param word        EUC-KR bytes as typed
 * @param len         length in bytes
 * @param outCands    filled with at most kMaxKoreanStems candidates
 * @return number of candidates written
 */
uint koreanStemCandidates(const char *word, uint len,
                          Common::String *outCands);

} // End of namespace Sci

#endif // SCI_PARSER_KORSTEM_H
