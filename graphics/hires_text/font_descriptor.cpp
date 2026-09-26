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

#include "graphics/hires_text/font_descriptor.h"

namespace Graphics {

static bool isBlank(char c) {
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool parseFontDescriptor(const Common::String &line, Common::String &face, int &px) {
	uint begin = 0, end = line.size();
	while (begin < end && isBlank(line[begin]))
		begin++;
	while (end > begin && isBlank(line[end - 1]))
		end--;

	// The last blank separates the face from the size.
	uint split = end;
	for (uint i = begin; i < end; i++) {
		if (isBlank(line[i]))
			split = i;
	}
	if (split == end)
		return false;

	uint faceEnd = split;
	while (faceEnd > begin && isBlank(line[faceEnd - 1]))
		faceEnd--;
	if (faceEnd == begin)
		return false;

	uint sizeEnd = end;
	if (sizeEnd - (split + 1) >= 2 && line[sizeEnd - 2] == 'p' && line[sizeEnd - 1] == 'x')
		sizeEnd -= 2;
	if (sizeEnd == split + 1)
		return false;

	int value = 0;
	for (uint i = split + 1; i < sizeEnd; i++) {
		const char c = line[i];
		if (c < '0' || c > '9')
			return false;
		value = value * 10 + (c - '0');
		if (value > kFontDescriptorMaxPx)
			return false;
	}
	if (value < 1)
		return false;

	face = Common::String(line.c_str() + begin, faceEnd - begin);
	px = value;
	return true;
}

} // End of namespace Graphics
