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

#ifndef SCI_PARSER_LOWERCASE_H
#define SCI_PARSER_LOWERCASE_H

#include "common/language.h"
#include "common/scummsys.h"

namespace Sci {

/**
 * Lower-case one word of parser input in place, the way the game's own
 * vocabulary expects it.
 *
 * The dictionary stores its words lower-cased, so the tokenizer folds the
 * player's input before looking it up. Which fold applies is decided by the
 * language of the detection entry the game matched, because that is what
 * decides the encoding the game's text is stored in.
 *
 * For a double-byte language the fold must skip both bytes of a double-byte
 * character: a lower-case table is a statement about single-byte text, and
 * applying it to the halves of a character rewrites the character. Which
 * bytes form a double-byte character is a property of the encoding, so the
 * word is walked left to right rather than byte by byte in isolation.
 *
 * This lives apart from vocabulary.cpp so it can be tested without a running
 * engine; it depends on nothing but the language.
 *
 * @param word		the bytes to fold, modified in place
 * @param len		how many bytes
 * @param language	the language of the game's detection entry
 */
void parserLowerCaseWord(byte *word, uint len, Common::Language language);

/**
 * Does this language's text use a double-byte encoding whose characters the
 * parser must keep intact?
 *
 * Exposed for the tests, which enumerate every language SCI detects and
 * require a recorded decision for each.
 */
bool parserLanguageIsDoubleByte(Common::Language language);

/**
 * Does this byte start a double-byte character in this language's encoding?
 *
 * False for every language that is not double-byte. Exposed for the tests.
 */
bool parserIsLeadByte(byte c, Common::Language language);

} // End of namespace Sci

#endif // SCI_PARSER_LOWERCASE_H
