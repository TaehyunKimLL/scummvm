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

#include "common/debug.h"
#include "common/str.h"
#include "common/ustr.h"

#include "scumm/korean_text.h"

namespace Scumm {

int ksx1001Index(uint32 cp) {
	int lo = 0, hi = 2349;
	while (lo <= hi) {
		const int mid = (lo + hi) / 2;
		if (kKsx1001Hangul[mid] == cp)
			return mid;
		if (kKsx1001Hangul[mid] < cp)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return -1;
}

uint32 ksx1001CodePoint(byte lead, byte trail) {
	if (lead < 0xB0 || lead > 0xC8 || trail < 0xA1 || trail > 0xFE)
		return 0;
	return kKsx1001Hangul[(lead - 0xB0) * 94 + (trail - 0xA1)];
}

int scummEscapeLength(const byte *p, const byte *end) {
	if (p >= end || (*p != 0xFF && *p != 0xFE))
		return 0;
	int n = 2;
	if (p + 1 < end && p[1] != 1 && p[1] != 2 && p[1] != 3 && p[1] != 8)
		n = 4;
	return (int)MIN<ptrdiff_t>(n, end - p);
}

const byte *scummTextEnd(const byte *s, const byte *end) {
	while (s < end) {
		const int esc = scummEscapeLength(s, end);
		if (esc)
			s += esc;
		else if (*s)
			s++;
		else
			break;
	}
	return s;
}

uint32 decodeUtf8(const byte *p, const byte *end, int &len) {
	const byte c = p[0];
	len = 1;
	if (c < 0x80)
		return c;
	int n;
	uint32 cp;
	if (c >= 0xC2 && c <= 0xDF) {
		n = 2; cp = c & 0x1F;
	} else if (c >= 0xE0 && c <= 0xEF) {
		n = 3; cp = c & 0x0F;
	} else if (c >= 0xF0 && c <= 0xF4) {
		n = 4; cp = c & 0x07;
	} else {
		return 0xFFFFFFFF;
	}
	if (end - p < n)
		return 0xFFFFFFFF;
	for (int i = 1; i < n; i++) {
		if ((p[i] & 0xC0) != 0x80)
			return 0xFFFFFFFF;
		cp = (cp << 6) | (p[i] & 0x3F);
	}
	if ((n == 3 && cp < 0x800) || (n == 4 && (cp < 0x10000 || cp > 0x10FFFF)) || (cp >= 0xD800 && cp <= 0xDFFF))
		return 0xFFFFFFFF;
	len = n;
	return cp;
}

void appendUtf8(uint32 cp, Common::Array<byte> &out) {
	if (cp < 0x80) {
		out.push_back(cp);
	} else if (cp < 0x800) {
		out.push_back(0xC0 | (cp >> 6));
		out.push_back(0x80 | (cp & 0x3F));
	} else if (cp < 0x10000) {
		out.push_back(0xE0 | (cp >> 12));
		out.push_back(0x80 | ((cp >> 6) & 0x3F));
		out.push_back(0x80 | (cp & 0x3F));
	} else {
		out.push_back(0xF0 | (cp >> 18));
		out.push_back(0x80 | ((cp >> 12) & 0x3F));
		out.push_back(0x80 | ((cp >> 6) & 0x3F));
		out.push_back(0x80 | (cp & 0x3F));
	}
}

bool textIsUtf8(const byte *p, const byte *end) {
	while (p < end && *p) {
		const int esc = scummEscapeLength(p, end);
		if (esc) {
			p += esc;
			continue;
		}
		int len;
		if (decodeUtf8(p, end, len) == 0xFFFFFFFF)
			return false;
		p += len;
	}
	return true;
}

void cp949ToUtf8(const byte *src, const byte *end, Common::Array<byte> &out, uint &unmapped) {
	// The pairing rule is upstream's (is2ByteCharacter(): a lead 0xB0-0xD0 takes the next byte, whatever it is);
	// every other byte >= 0x80 is a glyph of the game's own charset (MI1's 0xFA, say) and becomes U+0080..U+00FF,
	// which the renderer draws from the charset again. So the 1x picture stays upstream's.
	const byte *p = src;
	while (p < end && *p) {
		const int esc = scummEscapeLength(p, end);
		if (esc) {
			for (int i = 0; i < esc; i++)
				out.push_back(p[i]);
			p += esc;
			continue;
		}
		const byte c = *p;
		if (c < 0x80) {
			out.push_back(c);
			p++;
			continue;
		}
		if (c < 0xB0 || c > 0xD0 || p + 1 >= end || !p[1]) {
			appendUtf8(c, out);
			p++;
			continue;
		}
		uint32 cp = ksx1001CodePoint(c, p[1]);
		if (!cp) {
			// Hanja, or a CP949 extension syllable (trail < 0xA1): only encoding.dat knows it.
			const char pair[2] = { (char)c, (char)p[1] };
			const Common::U32String u(pair, 2, Common::kWindows949);
			if (u.size() == 1 && u[0] != '?' && u[0] >= 0x100 && u[0] != 0xFFFD)
				cp = u[0];
		}
		if (cp) {
			appendUtf8(cp, out);
		} else {
			debug(1, "Korean text: CP949 %02X %02X cannot be read, '?' written", c, p[1]);
			out.push_back('?');
			unmapped++;
		}
		p += 2;
	}
}

int toEngineText(const byte *src, int srcMax, byte *dst, int dstSize, uint *unmapped) {
	if (dstSize <= 0)
		return 0;
	const byte *end = scummTextEnd(src, src + srcMax);
	Common::Array<byte> buf;
	const byte *text;
	uint32 len;
	if (textIsUtf8(src, end)) {
		text = src;
		len = end - src;
	} else {
		uint n = 0;
		cp949ToUtf8(src, end, buf, n);
		if (unmapped)
			*unmapped += n;
		text = buf.begin();
		len = buf.size();
	}
	// Cut after the last whole character or escape that fits, never inside one.
	uint32 at = 0;
	const byte *tEnd = text + len;
	while (at < len) {
		int n = scummEscapeLength(text + at, tEnd);
		if (!n)
			decodeUtf8(text + at, tEnd, n);
		if ((int)(at + n) > dstSize - 1)
			break;
		at += n;
	}
	memmove(dst, text, at);
	dst[at] = 0;
	return at;
}

int koreanFinalBits(const byte *p, const byte *end) {
	int bits = 0;
	while (p < end && *p) {
		const int esc = scummEscapeLength(p, end);
		if (esc) {
			p += esc;
			continue;
		}
		int len;
		const uint32 cp = decodeUtf8(p, end, len);
		if (cp >= 0xAC00 && cp <= 0xD7A3) {
			const int jong = (cp - 0xAC00) % 28;
			bits = (jong ? 1 : 0) | (jong == 8 ? 2 : 0);
		}
		p += len;
	}
	return bits;
}

} // End of namespace Scumm
