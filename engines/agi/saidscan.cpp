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

static void addUnique(Common::Array<uint16> &arr, uint16 value) {
	for (uint i = 0; i < arr.size(); ++i) {
		if (arr[i] == value)
			return;
	}
	arr.push_back(value);
}

void SaidScanner::scan(const byte *code, uint32 codeSize,
                       const AgiOpCodeEntry *opCodes,
                       const AgiOpCodeEntry *opCodesCond,
                       Common::Array<uint16> &verbs,
                       Common::Array<uint16> &nouns) {
	verbs.clear();
	nouns.clear();
	if (!code || !codeSize || !opCodes || !opCodesCond)
		return;

	uint32 ip = 0;
	while (ip < codeSize) {
		const byte op = code[ip++];

		if (op == 0xFF) {
			// Start of an if() condition block.
			while (ip < codeSize) {
				const byte c = code[ip++];

				if (c == 0xFF) {
					ip += 2;    // closing bracket plus the jump offset
					break;
				}
				if (c == 0xFC || c == 0xFD)
					continue;   // or / not

				if (c == 0x0E) {
					// said() is variable length: a count byte then that many
					// little-endian word group ids.
					if (ip >= codeSize)
						break;
					const byte count = code[ip++];
					for (byte k = 0; k < count; ++k) {
						if (ip + 1 >= codeSize)
							break;
						const uint16 gid = READ_LE_UINT16(code + ip);
						ip += 2;
						// 1 is the "anyword" wildcard, never a real group.
						if (gid != 1) {
							if (k == 0)
								addUnique(verbs, gid);
							else
								addUnique(nouns, gid);
						}
					}
					continue;
				}

				ip += opCodesCond[c].parameterSize;
			}
		} else if (op == 0xFE) {
			ip += 2;            // goto
		} else {
			if (!opCodes[op].name)
				break;          // unknown opcode: stop rather than desync
			ip += opCodes[op].parameterSize;
		}
	}
}

} // End of namespace Agi
