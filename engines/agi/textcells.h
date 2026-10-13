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

#ifndef AGI_TEXTCELLS_H
#define AGI_TEXTCELLS_H

#include "common/str.h"

namespace Agi {

/**
 * One character is one cell of AGI's 40x25 text grid (text.cpp advances
 * one column per character). In UTF-8 text mode a character is a code
 * point; otherwise it is a byte, as it always was - the Russian, Hebrew and
 * French fonts have real glyphs at 0x80-0xFF, so `utf8` must come from
 * AgiBase::textIsUtf8() and nowhere else.
 *
 * A malformed UTF-8 sequence (a bad lead or continuation byte, an overlong
 * form, a surrogate, a value past U+10FFFF) is its first byte, one cell: a translation is
 * not guaranteed to be valid, and a decoder that ran off a truncated
 * sequence would swallow the rest of the string.
 */
namespace TextCells {

/** True if any byte of text is >= 0x80: the one scan for "is this string not plain ASCII". */
bool hasHighBytes(const char *text);
int decode(const char *text, bool utf8, uint32 &cp);
int16 cellLen(const char *text, bool utf8);
int16 measure(const char *text, int16 byteLen, bool utf8, int16 maxCells = -1, int16 *bytesForMaxCells = nullptr);
Common::String clipCells(const char *text, int16 maxCells, bool utf8);
Common::String fromCells(const uint32 *cells, bool utf8);
int16 toCells(const char *text, uint32 *cells, int16 capacity, bool utf8);
int16 cellCount(const uint32 *cells);
bool isHangulSyllable(uint32 cp);

/**
 * compare.strings (op_test.cpp): drop space, tab, - . , : ; ! and the
 * apostrophe, fold ASCII letters to lower case, compare the rest byte for
 * byte. Bytes >= 0x80 are never folded (they are Hangul in UTF-8 mode and
 * the original's tolower() left them alone in the C locale).
 */
bool agiStringsEqual(const char *a, const char *b);

/**
 * TextMgr::stringWordWrap()'s loop, in cells (K5). `hangulAny` is the map's
 * [layout] hangul=any: a Hangul word that does not fit the rest of a line
 * fills it and continues on the next instead of moving down whole.
 */
void wordWrap(const char *text, int16 maxWidth, bool utf8, bool hangulAny, Common::String &out,
              int16 &width, int16 &height, bool *truncated = nullptr);

} // End of namespace TextCells
} // End of namespace Agi

#endif
