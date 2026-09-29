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


#ifndef BACKENDS_PLATFORM_DOS_CURSOR_CONVERT_H
#define BACKENDS_PLATFORM_DOS_CURSOR_CONVERT_H

#include "common/array.h"
#include "common/endian.h"
#include "common/scummsys.h"
#include "graphics/pixelformat.h"

namespace DOS {

/** What cursorImage() decided: what the SoftCursor should show. */
enum CursorResult {
	kCursorClear,		///< no cursor (0x0): clear the image
	kCursorAsIs,		///< same format as the screen: the engine's pixels and key
	kCursorConverted,	///< CLUT8 on true colour: @p out and @p outKey
	kCursorMismatch		///< another non-CLUT8 format: nothing to show (clear it)
};

/**
 * Decide how a w x h cursor in @p src (format @p srcFormat, key colour
 * @p key) is shown on a screen in @p screen. A CLUT8 cursor on a CLUT8
 * screen goes as is, since the hardware has one palette. A CLUT8 cursor on
 * true colour is converted through @p pal (256 RGB triplets) into @p out
 * at the screen's pixel size, with a new key colour @p outKey that is not
 * among the converted pixels: of the first 257 values at least one is not
 * used by the (at most 256) colours.
 */
inline CursorResult cursorImage(const byte *src, uint w, uint h, const Graphics::PixelFormat &srcFormat, uint32 key,
								const Graphics::PixelFormat &screen, const byte *pal,
								Common::Array<byte> &out, uint32 &outKey) {
	if (!w || !h)
		return kCursorClear;
	if (srcFormat == screen)
		return kCursorAsIs;
	if (srcFormat.bytesPerPixel != 1)
		return kCursorMismatch;
	const uint bpp = screen.bytesPerPixel;
	const uint n = w * h;
	Common::Array<uint32> colors(n);
	bool used[257];
	memset(used, 0, sizeof(used));
	for (uint i = 0; i < n; ++i) {
		const byte idx = src[i];
		if (idx == key)
			continue;
		colors[i] = screen.RGBToColor(pal[idx * 3], pal[idx * 3 + 1], pal[idx * 3 + 2]);
		if (colors[i] <= 256)
			used[colors[i]] = true;
	}
	outKey = 0;
	while (used[outKey])
		++outKey;
	out.resize(n * bpp);
	for (uint i = 0; i < n; ++i) {
		const uint32 c = (src[i] == key) ? outKey : colors[i];
		if (bpp == 2)
			WRITE_UINT16(&out[i * 2], c);
		else
			WRITE_UINT32(&out[i * 4], c);
	}
	return kCursorConverted;
}

} // End of namespace DOS

#endif
