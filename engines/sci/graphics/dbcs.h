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

#ifndef SCI_GRAPHICS_DBCS_H
#define SCI_GRAPHICS_DBCS_H

#include "common/scummsys.h"
#include "common/str.h"

namespace Sci {

/**
 * Is this the first byte of a double-byte character in EUC-KR?
 *
 * Both bytes of a double-byte character are 0xA1..0xFE. This is the range
 * GfxFontKorean::isDoubleByte() answers with, and the same range the parser's
 * fold skips (parser/lowercase.cpp), so what the font draws as one character,
 * what the cursor steps over, and what the tokenizer keeps intact all agree.
 *
 * Deliberately wider than the 0xB0..0xC8 that
 * GfxText16::SwitchToFont1001OnKorean() sniffs for: that narrower range is the
 * rows the bundled korean.fnt has Hangul glyphs for, not the extent of the
 * encoding. A hanja or a fullwidth comma still occupies two bytes and one
 * cursor position even when nothing can draw it.
 */
inline bool isEucKrLeadByte(byte c) {
	return c >= 0xA1 && c <= 0xFE;
}

/**
 * Decode one EUC-KR character from @p text into the value the Korean font
 * indexes by, and say how many bytes it took.
 *
 * The returned packing is lead-in-the-low-byte, trail-in-the-high-byte,
 * because that is what FontKoreanWansung::getCharData() indexes with:
 * ((ch % 256) - 0xb0) * 94 + (ch / 256) - 0xa1. Reversing it yields a
 * legal-looking value that selects a different glyph.
 *
 * A string that ends on a lead byte decodes as that single byte. An edit
 * buffer legitimately holds one - a syllable is written one byte at a time -
 * and packing it would read the terminator and past it.
 *
 * This lives apart from the font class so it can be tested without
 * constructing an engine; it depends on nothing at all.
 *
 * @param text      the byte string, never empty
 * @param bytesRead out: 1 or 2
 */
inline uint16 decodeEucKrChar(const char *text, int *bytesRead) {
	const byte lead = *(const byte *)text;

	if (isEucKrLeadByte(lead) && text[1] != '\0') {
		*bytesRead = 2;
		return lead | ((uint16)(*(const byte *)(text + 1)) << 8);
	}

	*bytesRead = 1;
	return lead;
}

/**
 * Step one character LEFT from @p pos, returning the new byte offset.
 *
 * Deleting or stepping by one byte inside a double-byte character leaves a
 * half character in the buffer, and the renderer walks the string by
 * decoding: GfxText16::Box() reads a lead byte, takes the next byte as its
 * trail, and so runs past the terminator when that trail is the terminator.
 * The crash therefore happens on the NEXT redraw, not at the edit, which is
 * why it presents as "deleting Korean kills the game".
 *
 * Scanning forward from the start is what makes this correct rather than
 * merely plausible. EUC-KR trail bytes cover 0xA1..0xFE, the same range as
 * lead bytes, so the byte before the cursor cannot be classified by looking
 * at it: in "AB한" the byte at offset 3 is a trail, and in "한한" the byte
 * at offset 2 is a lead, and both can hold the same value. Only the parse
 * from a known boundary says which.
 */
inline uint stepCharLeft(const Common::String &text, uint pos) {
	uint i = 0, prev = 0;
	while (i < pos) {
		prev = i;
		i += (isEucKrLeadByte((byte)text[i]) && i + 1 < text.size()) ? 2 : 1;
	}
	// When i overshot, pos sat inside a character: prev is that character's
	// start, which is the boundary wanted either way.
	return prev;
}

/**
 * Step one character RIGHT from @p pos, returning the new byte offset.
 * Same reasoning as stepCharLeft(): the parse decides, not the byte.
 */
inline uint stepCharRight(const Common::String &text, uint pos) {
	uint i = 0;
	while (i < text.size()) {
		const uint next =
		    i + ((isEucKrLeadByte((byte)text[i]) && i + 1 < text.size()) ? 2 : 1);
		if (i >= pos)
			return MIN<uint>(next, text.size());
		i = next;
	}
	return text.size();
}

} // End of namespace Sci

#endif // SCI_GRAPHICS_DBCS_H
