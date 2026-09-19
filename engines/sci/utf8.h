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

#ifndef SCI_UTF8_H
#define SCI_UTF8_H

#include "common/scummsys.h"

namespace Sci {

/**
 * Decode one UTF-8 sequence at @p p.
 *
 * Returns the code point and sets @p outBytes to the number of bytes
 * consumed, 1..4. A byte that does not start a well-formed sequence - a
 * stray continuation byte, an over-long form, a truncated tail - is returned
 * as itself with @p outBytes = 1, so a walk always advances and a corrupt
 * byte becomes one visible wrong glyph rather than a hang or a skip.
 *
 * This is the single decoder for SCI text once heapStringsAreUtf8(): the
 * kernel string ops count with it and GfxText16 walks with it, so "how
 * many characters" and "where does the next one start" cannot disagree.
 *
 * Kept apart from util.h on purpose: this is a pure function with no
 * dependency on the engine, and it is unit-tested, which util.h's SciSpan
 * friend declarations make awkward.
 */
uint32 decodeUtf8Char(const byte *p, int &outBytes);

/** Number of code points in a NUL-terminated UTF-8 string. */
uint32 utf8Length(const byte *p);

/**
 * Byte offset of the @p index-th code point in a NUL-terminated UTF-8
 * string, or the offset of the terminator when @p index is past the end.
 */
uint32 utf8OffsetOf(const byte *p, uint32 index);

} // End of namespace Sci

#endif // SCI_UTF8_H
