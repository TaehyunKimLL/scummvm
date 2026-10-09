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
#include "common/str-enc.h"

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
 * Encode @p codePoint (up to U+10FFFF; larger values become U+FFFD) as
 * UTF-8 into @p out, which has room for 4 bytes. Returns the byte count.
 */
int encodeUtf8Char(uint32 codePoint, byte *out);

/**
 * Where a write of the @p index-th code point lands in a UTF-8 string that
 * is being built left to right: the byte offset of that code point, and
 * past the terminator one byte per missing index (so an ASCII string keeps
 * the byte-for-index answer it always had).
 */
uint32 utf8WriteOffset(const byte *p, uint32 index);

/**
 * Byte offset of the @p index-th code point in a NUL-terminated UTF-8
 * string, or the offset of the terminator when @p index is past the end.
 */
uint32 utf8OffsetOf(const byte *p, uint32 index);

/**
 * Whether @p index lies beyond the terminator of the NUL-terminated UTF-8
 * string @p p, counting code points (the terminator itself is index
 * length, so it is not beyond). A script that reads past a string's end is
 * addressing the buffer's own layout, such as a list of fixed-size records,
 * so there the index is a byte offset and not a character count.
 */
bool utf8IndexIsPastEnd(const byte *p, uint32 index);

/**
 * The code point of one double-byte character of a code page (lead byte,
 * then trail byte), or 0 when the pair does not decode.
 *
 * Common::String::decode() needs encoding.dat for every CJK code page, and
 * without it answers U+FFFD for every pair - measured on Conquests of
 * Camelot's Korean beta, 44 "font.0 is missing glyph 65533" and empty menu
 * buttons. For CP949 the KS X 1001 Hangul block (the whole of what the
 * Korean fan patches write) is therefore decoded from the static table in
 * graphics/hires_text/codepage_kr.h first, which needs no data file; the
 * rest of the code page still goes through decode().
 */
uint32 decodeCodePagePair(byte lead, byte trail, Common::CodePage codePage);

/**
 * The inverse: @p codePoint encoded in @p codePage as a packed pair (lead
 * byte low, trail byte high - the layout the legacy CJK fonts index by), or
 * 0 when it is not a double-byte character there. The same static-table
 * shortcut for CP949 Hangul.
 */
uint32 encodeCodePagePair(uint32 codePoint, Common::CodePage codePage);

/**
 * For a game that carries its Hangul as banks of FONT resources, one per
 * EUC-KR lead byte 0xB0..0xC8 starting at @p bankBase (GfxFontBanked): the
 * bank resource and glyph slot (the trail byte) of a packed pair. False
 * outside the Hangul rows, for a bad trail byte, or when bankBase < 0.
 */
bool koreanBankAndSlot(uint32 packed, int bankBase, int &bank, byte &slot);

} // End of namespace Sci

#endif // SCI_UTF8_H
