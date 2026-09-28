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

#ifndef BACKENDS_PLATFORM_DOS_LINE_REPEAT_H
#define BACKENDS_PLATFORM_DOS_LINE_REPEAT_H

#include "common/rect.h"
#include "common/scummsys.h"

namespace DOS {

/**
 * Line-repeat fallback: a 400-row picture shown on a 480-row mode by
 * repeating every 5th logical row once. physRow() maps a logical row
 * (0..399) to the physical row (0..479) it is written to first;
 * repeats() says whether that logical row is also written to
 * physRow() + 1. logicalRow() is the exact inverse: which logical row
 * a physical row (0..479) belongs to, including a repeated one --
 * logicalRow(physRow(y)) == y for every y, and when repeats(y),
 * logicalRow(physRow(y) + 1) == y too (the repeat maps back to the row
 * it repeats, not the next one).
 */
inline int physRow(int y) {
	return y + y / 5;
}

inline bool repeats(int y) {
	return y % 5 == 4;
}

inline int logicalRow(int py) {
	return py - (py + 1) / 6;
}

/** The physical rows a dirty rect of logical rows touches, same left/right. */
inline Common::Rect physRect(const Common::Rect &r) {
	const int lastLogical = r.bottom - 1;
	const int top = physRow(r.top);
	const int bottom = physRow(lastLogical) + (repeats(lastLogical) ? 2 : 1);
	return Common::Rect(r.left, top, r.right, bottom);
}

/**
 * Copy the logical rows of @p r from @p src to @p dst, writing each one to
 * its physRow() in @p dst and, when repeats() says so, once more to
 * physRow() + 1. @p dst / @p src are the base of the whole surface; @p
 * dstPitch / @p srcPitch and @p bpp describe it in bytes.
 */
inline void copyRows(byte *dst, int dstPitch, const byte *src, int srcPitch, int bpp, const Common::Rect &r) {
	const int bytes = r.width() * bpp;
	const int colOffset = r.left * bpp;
	for (int y = r.top; y < r.bottom; ++y) {
		const byte *s = src + y * srcPitch + colOffset;
		byte *d = dst + physRow(y) * dstPitch + colOffset;
		memcpy(d, s, bytes);
		if (repeats(y))
			memcpy(d + dstPitch, s, bytes);
	}
}

} // End of namespace DOS

#endif
