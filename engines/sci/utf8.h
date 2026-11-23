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
#include "common/str.h"

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
 * This is the single decoder for SCI text once the TextMode is kUtf8: the
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
 * Whether @p index lies beyond both the character count and the byte length
 * of the NUL-terminated UTF-8 string @p p. A script that reads that far past
 * a string's end is addressing the buffer's own layout, such as a list of
 * fixed-size records (a record of R bytes holds at most R - 1 bytes of
 * text, so the next record's index R is past it), and there the index is a
 * byte offset. An index up to the byte length stays a character index.
 */
bool utf8IndexIsPastEnd(const byte *p, uint32 index);

/**
 * Index of the code point that starts at byte @p offset of the
 * NUL-terminated UTF-8 string @p p: the number of code points before it.
 * The inverse of utf8OffsetOf(); an offset inside a character counts that
 * character as before it, and one past the terminator is the length.
 */
uint32 utf8IndexOfOffset(const byte *p, uint32 offset);

/**
 * Byte offset where the code point that ends at byte @p pos of the
 * @p len-byte UTF-8 text @p p begins (what BackSpace and Left step to).
 * A byte that is not part of a well-formed sequence is one step by itself.
 * @p pos must be 1..len. @p p must be NUL-terminated at @p len (a heap
 * string is), so a truncated tail stops at the terminator.
 */
uint32 utf8PrevBoundary(const byte *p, uint32 len, uint32 pos);

/**
 * Byte offset just after the code point that begins at byte @p pos (what
 * Delete and Right step to); @p pos + 1 for a malformed byte; @p pos
 * itself at @p len. @p pos must be a boundary.
 */
uint32 utf8NextBoundary(const byte *p, uint32 len, uint32 pos);

/** What utf8ReplaceAt() did. */
enum Utf8WriteResult {
	kUtf8Replaced,   ///< the text was rebuilt: write it back as a whole string
	kUtf8ByteOffset, ///< @p index is beyond both the code points and the bytes: the caller writes a byte at that offset
	kUtf8Refused     ///< nothing changed: the index is between the counts, or the code point does not fit
};

/**
 * The longest prefix of @p text that, with its NUL, fits @p capacity bytes,
 * cut on a code point boundary. @p capacity 0 means no bound.
 */
Common::String utf8Truncate(const Common::String &text, uint32 capacity);

/** What a UTF-8 string copy into a buffer does (utf8CopyPlan()). */
struct Utf8CopyPlan {
	Common::String text; ///< what is written (then a NUL), whole code points only
	uint32 limit;        ///< the count to hand the byte copy: it never exceeds the buffer
	uint32 growTo;       ///< 0, or the size the buffer must grow to first
	bool cut;            ///< @c text is shorter than the source text asked for
};

/**
 * A strncpy of the UTF-8 @p text (already limited to @p n bytes, as
 * strnlen(src, n) gives) into a buffer of @p maxSize bytes (<= 0: size
 * unknown, no bound). @p n 0xFFFFFFFF is the unbounded copy (strcpy): only
 * that one may ask the buffer to grow (@p growable), to hold the whole text.
 * Any other copy is clamped to the buffer, cut on a code point boundary
 * where it must be, and a finite @p n that ends inside a code point
 * is rounded down to the one before. Nothing is ever written past
 * @p maxSize.
 */
Utf8CopyPlan utf8CopyPlan(const Common::String &text, uint32 n, int32 maxSize, bool growable);

/**
 * kStrAt's write on a UTF-8 string: the @p index-th code point becomes
 * @p cp (0 ends the string there), the bytes after it move, so the text
 * stays valid UTF-8. @p index equal to the code point count appends.
 * @p old gets the replaced code point (0 at the end). The result,
 * including its NUL, is kept within @p capacity bytes (0: no bound): the
 * tail is cut on a code point boundary, and kUtf8Refused answers when the
 * new code point alone does not fit.
 */
Utf8WriteResult utf8ReplaceAt(Common::String &text, uint32 index, uint32 cp, uint32 capacity, uint32 &old);

} // End of namespace Sci

#endif
