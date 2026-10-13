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

struct AgiOpCodeEntry;

/** One said() of a logic: where it is and the groups it names (1 any, 9999 rest). */
struct SaidSpec {
	uint16 logic;
	uint32 offset;
	Common::Array<uint16> words;
};

/**
 * Every said() of a logic's bytecode (AGI v2/v3: condition opcode 0x0E,
 * a count byte, then that many little-endian group ids), walked with the
 * engine's own opcode tables - a hand copy drifts and the walk desyncs
 * (the K chain's note, a9fe2374373). harness/agi/agires.py walks the same
 * way; k16said_agi.py checks the two agree.
 */
class SaidScanner {
public:
	static void scan(const byte *code, uint32 codeSize, const AgiOpCodeEntry *opCodes,
	                 const AgiOpCodeEntry *opCodesCond, uint16 logic, Common::Array<SaidSpec> &out);
};

} // End of namespace Agi

#endif
