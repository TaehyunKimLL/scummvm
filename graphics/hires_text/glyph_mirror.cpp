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

#include "graphics/hires_text/glyph_mirror.h"

#include "common/util.h"

namespace Graphics {

void flipGlyph(byte *pixels, int pitch, int width, int height, HiResMirror mode) {
	if (!pixels || width <= 0 || height <= 0)
		return;
	if (mode == kHiResMirrorHorizontal || mode == kHiResMirrorBoth) {
		for (int y = 0; y < height; ++y) {
			byte *row = pixels + y * pitch;
			for (int l = 0, r = width - 1; l < r; ++l, --r)
				SWAP(row[l], row[r]);
		}
	}
	if (mode == kHiResMirrorVertical || mode == kHiResMirrorBoth) {
		for (int t = 0, b = height - 1; t < b; ++t, --b) {
			byte *top = pixels + t * pitch;
			byte *bottom = pixels + b * pitch;
			for (int x = 0; x < width; ++x)
				SWAP(top[x], bottom[x]);
		}
	}
}

} // End of namespace Graphics
