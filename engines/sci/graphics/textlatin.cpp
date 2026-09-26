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

#include "sci/graphics/textlatin.h"

namespace Sci {
namespace TextCompose {

uint32 latinFullwidth(uint32 cp, LatinMode mode, bool fullwidthSpace) {
	if (mode != kLatinFullwidth)
		return cp; // kLatinOff and kLatinHalf never remap the code point

	if (cp >= 0x0021 && cp <= 0x007E)
		return cp + 0xFEE0; // U+FF01..U+FF5E, the fullwidth-forms block
	if (cp == 0x0020 && fullwidthSpace)
		return 0x3000; // IDEOGRAPHIC SPACE
	return cp;
}

bool asciiGoesToUnicodeFace(uint32 cp, LatinMode mode) {
	return mode == kLatinHalf && cp >= 0x0020 && cp <= 0x007E;
}

} // End of namespace TextCompose
} // End of namespace Sci
