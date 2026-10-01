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

#ifndef GRAPHICS_HIRES_TEXT_GLYPH_MIRROR_H
#define GRAPHICS_HIRES_TEXT_GLYPH_MIRROR_H

#include "common/scummsys.h"
#include "common/str.h"

namespace Graphics {

/**
 * How a glyph is flipped before it is drawn (C27, [font.N] mirror=).
 *
 * Some games ship a charset whose glyphs are the normal ones flipped: MI1,
 * MI2 and Loom CD have a charset 3 turned half a turn (flipped both ways),
 * and store the strings drawn with it reversed, so that drawn left to right
 * the whole line reads upside down. A replacement face reproduces that by
 * flipping each glyph inside its own box and keeping the string's order.
 */
enum HiResMirror {
	kHiResMirrorNone = 0,        ///< drawn as it is
	kHiResMirrorHorizontal = 1,  ///< left and right swapped (mirror writing)
	kHiResMirrorVertical = 2,    ///< top and bottom swapped
	kHiResMirrorBoth = 3,        ///< both: half a turn
	/// mirror=true: as the game's own font is flipped, which the engine
	/// knows; horizontal where it knows nothing
	kHiResMirrorGame = 4
};

/**
 * Flip an 8 bpp glyph in place.
 *
 * @param pixels  the glyph's top-left pixel
 * @param pitch   bytes from one row to the next
 * @param width   the glyph's width; columns past it are not touched
 * @param height  its rows; a vertical flip turns them about their middle,
 *                so a glyph given its whole cell turns about the cell's
 * @param mode    kHiResMirrorGame is not resolved here and draws as None
 */
void flipGlyph(byte *pixels, int pitch, int width, int height, HiResMirror mode);

/**
 * Where a glyph flipped across lands: its box [@p left, @p left + @p width)
 * reflected about the middle of [@p axisLeft, @p axisRight), the pen and
 * advance of the glyph (or of its base, for a combining mark). A glyph
 * filling its advance stays where it was; one sitting to the left of it
 * moves right by as much.
 */
inline int mirroredLeft(int left, int width, int axisLeft, int axisRight) {
	return axisLeft + axisRight - (left + width);
}

} // End of namespace Graphics

#endif
