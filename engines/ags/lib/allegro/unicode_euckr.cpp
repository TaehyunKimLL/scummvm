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


#include "ags/lib/allegro/unicode_euckr.h"
#include "graphics/hires_text/codepage_kr.h"

namespace AGS3 {

namespace {

using namespace Graphics::KoreanCodePage;

// The code point of the pair at s, or 0 when s does not start with a
// KS X 1001 Hangul pair. Reads s[1] only when s[0] is not the terminator.
uint32 pairAt(const char *s) {
	const byte hi = (byte)s[0];
	if (hi < 0xA1)
		return 0;
	const byte lo = (byte)s[1];
	if (!isEucKrPair(hi, lo))
		return 0;
	return decodeEucKrPair(hi, lo);
}

} // End of anonymous namespace

int euckr_getc(const char *s) {
	const uint32 cp = pairAt(s);
	return cp ? (int)cp : *(const unsigned char *)s;
}

int euckr_getx(char **s) {
	const uint32 cp = pairAt(*s);
	if (cp) {
		*s += 2;
		return (int)cp;
	}
	return *((unsigned char *)((*s)++));
}

int euckr_setc(char *s, int c) {
	if (c >= 0 && c < 256) {
		*s = c;
		return 1;
	}
	const int idx = ksx1001HangulIndexOf((uint32)c);
	if (idx < 0) {
		*s = '?';
		return 1;
	}
	s[0] = (char)(0xB0 + idx / 94);
	s[1] = (char)(0xA1 + idx % 94);
	return 2;
}

int euckr_width(const char *s) {
	return pairAt(s) ? 2 : 1;
}

int euckr_cwidth(int c) {
	return (c >= 256 && ksx1001HangulIndexOf((uint32)c) >= 0) ? 2 : 1;
}

int euckr_isok(int c) {
	if (c >= 0 && c < 256)
		return 1;
	return c > 0 && ksx1001HangulIndexOf((uint32)c) >= 0;
}

} // namespace AGS3
