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

#ifndef AGI_KORPARSE_H
#define AGI_KORPARSE_H

#include "common/array.h"
#include "common/str.h"

namespace Common {
class KoreanVocabulary;
}

namespace Agi {

/** One parsed word: what said() compares (id) and what %w shows (word). */
struct AgiWord {
	Common::String word; ///< as typed (UTF-8), or the English preposition a candidate inserted
	uint16 id;           ///< WORDS.TOK group
	uint8 role;          ///< Common::KoreanRole: the case particle a Korean noun lost
	bool verb;           ///< the group starts some said() of the game (AGIKOR class 0x0001)
	bool korean;
	AgiWord() : id(0), role(0), verb(false), korean(false) {}
};

typedef Common::Array<AgiWord> AgiSentence;

enum { kAgiKoreanVerbClass = 0x0001 };

/**
 * The interpreter's said() test (op_test.cpp testSaid()) on one word list:
 * word i must equal spec i, except 1 (any word) and 9999 (the rest of the
 * line, nothing included); every typed word must be used unless the spec
 * ends in 9999.
 */
bool saidMatches(const AgiSentence &words, const uint16 *spec, int nwords);

/** The game's group for the English preposition of each KoreanRole; 0 = none or ignored. */
struct AgiPrepositions {
	uint16 group[8];
	Common::String word[8];
	AgiPrepositions() {
		for (int i = 0; i < 8; i++)
			group[i] = 0;
	}
};

/**
 * One Korean token (UTF-8) through AGIKOR.DAT: exact, then the stemmer's
 * readings. A sequence row set (들여다봐 = look in) gives several words, the
 * role on the last; a homonym gives its first group; group 0 (좀) gives no
 * word. False when the table has no reading of the token. @p exactOnly
 * takes only a stored form (a joined pair of typed words).
 */
bool lookupKoreanToken(const Common::KoreanVocabulary &voc, const Common::String &utf8, AgiSentence &out,
                       bool exactOnly = false);

/** Design 5.5's candidates (a)-(d) of a sentence with Korean in it, duplicates dropped. */
void buildKoreanCandidates(const AgiSentence &typed, const AgiPrepositions &preps, uint maxWords,
                           Common::Array<AgiSentence> &out);

} // End of namespace Agi

#endif
