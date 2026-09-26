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

#ifndef SCI_GRAPHICS_TEXTCOMPOSE_H
#define SCI_GRAPHICS_TEXTCOMPOSE_H

#include "common/scummsys.h"
#include "graphics/pixelformat.h"

namespace Sci {

struct TextPixel;

/** The arithmetic of hi-res text, free of engine state so it is tested alone
 *  (HIRES_COMPOSITOR_DESIGN.md §3.2). */
namespace TextCompose {

byte expandCoverage(const byte *row, int x, int bpp);
void expandGlyphRow(byte *dstCoverage, const byte *row, int width, int bpp, bool greyed, int screenY, int screenX0);

inline byte blend(byte dst, byte src, byte a) {
	return (byte)(((uint32)dst * (255 - a) + (uint32)src * a + 127) / 255);
}

void composeSpan(byte *dst, const Graphics::PixelFormat &fmt, const TextPixel *text, int count, const byte *paletteRGB);
void stampSpan(byte *dstIndex, const TextPixel *text, int count);

} // End of namespace TextCompose
} // End of namespace Sci

#endif
