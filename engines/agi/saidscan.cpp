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

#include "agi/agi.h"
#include "agi/saidscan.h"

namespace Agi {

void SaidScanner::scan(const byte *code, uint32 codeSize, const AgiOpCodeEntry *opCodes,
                       const AgiOpCodeEntry *opCodesCond, uint16 logic, Common::Array<SaidSpec> &out) {
	if (!code || !codeSize || !opCodes || !opCodesCond)
		return;
	uint32 ip = 0;
	while (ip < codeSize) {
		const byte op = code[ip++];
		if (op == 0xFF) {
			// an if() condition block
			while (ip < codeSize) {
				const uint32 at = ip;
				const byte c = code[ip++];
				if (c == 0xFF) {
					ip += 2; // closing bracket plus the jump offset
					break;
				}
				if (c == 0xFC || c == 0xFD)
					continue; // or / not
				if (c == 0x0E) {
					if (ip >= codeSize)
						break;
					const byte count = code[ip++];
					SaidSpec spec;
					spec.logic = logic;
					spec.offset = at;
					for (byte k = 0; k < count && ip + 1 < codeSize; ++k) {
						spec.words.push_back(READ_LE_UINT16(code + ip));
						ip += 2;
					}
					out.push_back(spec);
					continue;
				}
				ip += opCodesCond[c].parameterSize;
			}
		} else if (op == 0xFE) {
			ip += 2; // goto
		} else {
			if (op != 0 && !opCodes[op].functionPtr)
				break; // an illegal opcode: stop rather than desync
			ip += opCodes[op].parameterSize;
		}
	}
}

} // End of namespace Agi
