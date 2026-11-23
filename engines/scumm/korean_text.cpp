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

#include "common/debug.h"
#include "common/str.h"
#include "common/ustr.h"

#include "scumm/korean_text.h"

namespace Scumm {

int ksx1001Index(uint32 cp) {
	int lo = 0, hi = 2349;
	while (lo <= hi) {
		const int mid = (lo + hi) / 2;
		if (kKsx1001Hangul[mid] == cp)
			return mid;
		if (kKsx1001Hangul[mid] < cp)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return -1;
}

uint32 ksx1001CodePoint(byte lead, byte trail) {
	if (lead < 0xB0 || lead > 0xC8 || trail < 0xA1 || trail > 0xFE)
		return 0;
	return kKsx1001Hangul[(lead - 0xB0) * 94 + (trail - 0xA1)];
}

} // End of namespace Scumm
