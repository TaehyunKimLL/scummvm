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

#ifndef SCUMM_HIRES_SCALE_H
#define SCUMM_HIRES_SCALE_H

#include "common/array.h"
#include "common/scummsys.h"

namespace Scumm {

/**
 * Magnify a rectangle of the game's picture into a sink, @p m times in both
 * axes.
 *
 * This is the picture-only half of what compositeText() does: no text surface
 * is involved, because the callers are the transition effects, which move
 * pieces of the game's own picture around the screen before the text layer is
 * composited over them again.
 *
 * The reason it exists at all is that a backend @p m times the game's size
 * cannot be fed the game's buffer directly. Handing copyRectToScreen() a
 * scaled destination rectangle and the game's own pointer asks the backend to
 * find m times as many pixels as the buffer holds - it reads on into whatever
 * follows, and (with a source pitch multiplied to hide the shortfall) samples
 * every m-th row while doing so. Neither the magnification nor an eventual
 * CLUT8-to-true-colour conversion can happen there. Both happen here.
 *
 * @param src       the game's buffer, palette indices
 * @param srcPitch  its real row stride in bytes - the buffer's own, never a
 *                  multiplied one
 * @param width     source width, in game pixels
 * @param height    source height, in game pixels
 * @param m         the scale; @p m == 1 is a plain copy
 */
template<class Sink>
void expandStrip(Sink &sink, const byte *src, int srcPitch,
				 int width, int height, int m) {
	if (width <= 0 || height <= 0 || m < 1)
		return;

	const int outWidth = width * m;

	// Each source row is written m times, so it is expanded once and reused:
	// the sink is handed whole rows rather than pixels.
	Common::Array<byte> row(outWidth);

	for (int h = 0; h < height * m; ++h) {
		if (h % m == 0) {
			const byte *s = src + (h / m) * srcPitch;
			for (int w = 0; w < outWidth; ++w)
				row[w] = s[w / m];
		}
		sink.writeBackground(row.begin(), outWidth);
	}
}

} // End of namespace Scumm

#endif
