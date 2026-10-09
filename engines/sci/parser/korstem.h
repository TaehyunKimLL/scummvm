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
enum { kMaxKoreanStems = 20 };

/**
 * Dictionary-form candidates for an inflected Korean word.
 *
 * The mapping is built around citation forms (-다) because that is what a
 * generated dictionary naturally contains, while a player types the plain
 * or imperative form. Measured on the Cascade Quest map: -다 accounts for
 * 249 entries and -라 for 116, against only 30 in -어.
 *
 * Every rule here REMOVES something and optionally re-attaches 다. None
 * of them invents a conjugation: a generated form that is not a real word
 * can still collide with a real entry, and the AGI chain measured that
 * generating endings lowered accuracy (15/18 -> 14/18) while stripping
 * them did not. A stripped candidate only matters when it hits an entry
 * that already exists.
 *
 * Irregular short forms (봐, 가져, 해) are deliberately NOT handled here.
 * No suffix rule reaches them - 봐 comes from 보 + 아 fused - so they are
 * requested at generation time and stored as their own entries instead.
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
