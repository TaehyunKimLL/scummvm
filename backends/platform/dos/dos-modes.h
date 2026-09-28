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

#ifndef BACKENDS_PLATFORM_DOS_DOS_MODES_H
#define BACKENDS_PLATFORM_DOS_DOS_MODES_H

#include "common/array.h"
#include "common/list.h"
#include "common/util.h"
#include "graphics/pixelformat.h"

namespace DOS {

/** One VESA mode as SDL3 lists it; sdlIndex is its place in that list. */
struct VideoMode {
	uint16 w, h;
	Graphics::PixelFormat format;
	int sdlIndex;
};

inline Graphics::PixelFormat rgb565() { return Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0); }
inline Graphics::PixelFormat xrgb1555() { return Graphics::PixelFormat(2, 5, 5, 5, 0, 10, 5, 0, 0); }
inline Graphics::PixelFormat xrgb8888() { return Graphics::PixelFormat(4, 8, 8, 8, 0, 16, 8, 0, 0); }

/** Index into @p modes of the mode that is exactly w x h in @p f, or -1. */
inline int findExactMode(const Common::Array<VideoMode> &modes, uint w, uint h, const Graphics::PixelFormat &f) {
	for (uint i = 0; i < modes.size(); ++i)
		if (modes[i].w == w && modes[i].h == h && modes[i].format == f)
			return (int)i;
	return -1;
}

/**
 * What getSupportedFormats() reports for a w x h game: the true-colour
 * formats a mode of exactly that size has, cheapest on the bus first,
 * then CLUT8, which is always there (the engine falls back to it).
 */
inline Common::List<Graphics::PixelFormat> supportedFormats(const Common::Array<VideoMode> &modes, uint w, uint h) {
	static const Graphics::PixelFormat order[] = { rgb565(), xrgb1555(), xrgb8888() };
	Common::List<Graphics::PixelFormat> out;
	for (uint i = 0; i < ARRAYSIZE(order); ++i)
		if (findExactMode(modes, w, h, order[i]) >= 0)
			out.push_back(order[i]);
	out.push_back(Graphics::PixelFormat::createFormatCLUT8());
	return out;
}

} // End of namespace DOS

#endif
