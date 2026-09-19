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

#include "sci/utf8.h"

namespace Sci {

uint32 decodeUtf8Char(const byte *p, int &outBytes) {
	const uint32 lead = p[0];
	outBytes = 1;
	if (lead < 0x80)
		return lead;

	// Lead byte decides the length; every following byte must be 10xxxxxx.
	int len;
	uint32 cp;
	if (lead >= 0xC2 && lead <= 0xDF) { len = 2; cp = lead & 0x1F; }
	else if (lead >= 0xE0 && lead <= 0xEF) { len = 3; cp = lead & 0x0F; }
	else if (lead >= 0xF0 && lead <= 0xF4) { len = 4; cp = lead & 0x07; }
	else
		return lead;	// continuation byte or invalid lead: pass through

	for (int i = 1; i < len; i++) {
		const byte b = p[i];
		if ((b & 0xC0) != 0x80)
			return lead;	// truncated: pass the lead through, advance 1
		cp = (cp << 6) | (b & 0x3F);
	}

	// Reject over-long encodings and surrogates, which a well-formed
	// encoder never produces and which would let two byte strings mean
	// the same text.
	if ((len == 3 && cp < 0x800) || (len == 4 && (cp < 0x10000 || cp > 0x10FFFF)) ||
	    (cp >= 0xD800 && cp <= 0xDFFF))
		return lead;

	outBytes = len;
	return cp;
}

uint32 utf8Length(const byte *p) {
	uint32 n = 0;
	int bytes;
	while (*p) {
		decodeUtf8Char(p, bytes);
		p += bytes;
		n++;
	}
	return n;
}

uint32 utf8OffsetOf(const byte *p, uint32 index) {
	const byte *start = p;
	int bytes;
	while (*p && index--) {
		decodeUtf8Char(p, bytes);
		p += bytes;
	}
	return (uint32)(p - start);
}

} // End of namespace Sci
