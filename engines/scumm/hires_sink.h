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

#ifndef SCUMM_HIRES_SINK_H
#define SCUMM_HIRES_SINK_H

#include "common/scummsys.h"
#include "graphics/pixelformat.h"

namespace Scumm {

/**
 * Where composited pixels go.
 *
 * Text is composited from two 8-bit planes - the game's palette indices and
 * a coverage value per pixel - over the game's own picture, which is also
 * paletted. What that becomes on the way out differs per platform: a palette
 * index again, a 16-bit entry, or a blended true-colour pixel.
 *
 * Keeping the destination behind this interface means the rule for deciding
 * *what* each pixel is gets written once, and a platform that renders text
 * differently supplies a sink rather than another copy of the loop.
 *
 * @par Indices, not colours
 * Every method takes palette indices. This is not incidental: the overlay
 * planes store indices precisely so that a later palette change - cycling, a
 * room fade, a darkening effect - recolours text that was drawn long before.
 * A sink resolves indices through the palette on each call and must not cache
 * the result across frames.
 *
 * @par Cost
 * Calls carry runs, not single pixels, so the virtual dispatch is amortised
 * over a span. Nothing here is called per pixel.
 */
class HiResSink {
public:
	virtual ~HiResSink() {}

	/**
	 * A run with no text over it: the game's picture shows through.
	 *
	 * @param bg     palette indices from the game's own buffer
	 * @param count  how many pixels
	 */
	virtual void writeBackground(const byte *bg, int count) = 0;

	/**
	 * A run of text at full coverage, hiding what is behind it.
	 *
	 * @param fg     palette indices from the text plane
	 * @param count  how many pixels
	 */
	virtual void writeOpaque(const byte *fg, int count) = 0;

	/**
	 * A run of text that only partly covers the picture behind it.
	 *
	 * A destination that cannot express a mixture is entitled to choose one
	 * side or the other; it must not drop the run.
	 *
	 * @param fg        palette indices from the text plane
	 * @param bg        palette indices from the game's own buffer
	 * @param coverage  0 = show bg, 255 = show fg
	 * @param count     how many pixels
	 */
	virtual void writeBlended(const byte *fg, const byte *bg,
							  const byte *coverage, int count) = 0;
};

} // End of namespace Scumm

#endif
