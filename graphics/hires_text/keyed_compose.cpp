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


#include "graphics/hires_text/keyed_compose.h"

namespace Graphics {
namespace KeyedCompose {

void rowsScalar(byte *dst, const byte *src, int srcSkip, const byte *text, int textSkip,
				int width, int height, int m, byte key) {
	const int outWidth = width * m;
	for (int h = 0; h < height * m; ++h) {
		const byte *srcRow = src + (h / m) * (width + srcSkip);
		if (m == 2) {
			for (int x = 0; x < width; ++x) {
				const byte bg = srcRow[x];
				const byte t0 = text[2 * x], t1 = text[2 * x + 1];
				dst[2 * x] = (t0 == key) ? bg : t0;
				dst[2 * x + 1] = (t1 == key) ? bg : t1;
			}
		} else {
			int w = 0;
			for (int x = 0; x < width; ++x) {
				const byte bg = srcRow[x];
				for (int k = 0; k < m; ++k, ++w)
					dst[w] = (text[w] == key) ? bg : text[w];
			}
		}
		dst += outWidth;
		text += outWidth + textSkip;
	}
}

void rows(byte *dst, const byte *src, int srcSkip, const byte *text, int textSkip,
		  int width, int height, int m, byte key) {
	rowsScalar(dst, src, srcSkip, text, textSkip, width, height, m, key);
}

} // End of namespace KeyedCompose
} // End of namespace Graphics
