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

#ifndef AGI_SAIDSCAN_H
#define AGI_SAIDSCAN_H

#include "common/array.h"

namespace Agi {

/**
 * Collect the word groups a logic resource tests with said().
 *
 * AGI scripts match player input with said(gid, gid, ...), so the set of
 * said() calls in the current room's logic is exactly the set of commands that
 * room understands. Feeding that set to the semantic parser as a whitelist
 * removes most of the ambiguity that an embedding cannot resolve on its own
 * (Korean 가져와 is both "get" and "carry"; the room only tests one of them).
 *
 * The scanner walks the condition blocks of the bytecode. said is opcode 0x0E
 * inside a condition and is variable length: one count byte followed by that
 * many little-endian group ids.
 */
class SaidScanner {
public:
	/**
	 * @param code      logic bytecode (after the 2-byte text offset header)
	 * @param codeSize  length of the code section
	 * @param opCodes      the engine's action opcode table (256 entries)
	 * @param opCodesCond  the engine's condition opcode table (256 entries)
	 * @param verbs     receives every distinct first-word group id
	 * @param nouns     receives every distinct later-word group id
	 *
	 * The opcode tables are passed in rather than duplicated here: a
	 * hand-written copy silently drifts from the engine and the walker then
	 * desynchronises, reporting group ids far outside the dictionary.
	 */
	static void scan(const byte *code, uint32 codeSize,
	                 const AgiOpCodeEntry *opCodes,
	                 const AgiOpCodeEntry *opCodesCond,
	                 Common::Array<uint16> &verbs,
	                 Common::Array<uint16> &nouns);
};

} // End of namespace Agi

#endif
