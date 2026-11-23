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

int scummEscapeFull(const byte *p, const byte *end) {
	if (p >= end || (*p != 0xFF && *p != 0xFE))
		return 0;
	if (p + 1 >= end)
		return -1;
	const int n = (p[1] == 1 || p[1] == 2 || p[1] == 3 || p[1] == 8) ? 2 : 4;
	return (end - p >= n) ? n : -1;
}

const byte *scummTextEnd(const byte *s, const byte *end) {
	// Only 0xFF starts an escape here, as in resStrLen(): 0xFE is also the trail byte of 25 CP949 syllables (BC FE),
	// and a real 0xFE escape (code 1 or 8) never has a 0 argument that could hide the terminator.
	while (s < end) {
		if (*s == 0xFF)
			s += scummEscapeLength(s, end);
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

// The escape at @p p as engine text has it: 0xFF with any code, 0xFE only with the codes 1, 2, 3, 8 (a line break, ...);
// any other 0xFE is a lone high byte (it is also the trail byte of 25 CP949 syllables). -1: cut by @p end.
static int textEscape(const byte *p, const byte *end) {
	if (*p == 0xFE && p + 1 < end && p[1] != 1 && p[1] != 2 && p[1] != 3 && p[1] != 8)
		return 0;
	return scummEscapeFull(p, end);
}

static bool utf8Valid(const byte *p, const byte *end, bool allowWide) {
	while (p < end && *p) {
		if (*p == 0xFE && p + 1 < end && p[1] != 1 && p[1] != 2 && p[1] != 3 && p[1] != 8)
			return false;	// 0xFE with any other code is a CP949 trail byte, not an escape
		const int esc = scummEscapeFull(p, end);
		if (esc < 0)
			return true;	// a truncated escape ends the text (the copy drops it)
		if (esc) {
			p += esc;
			continue;
		}
		int len;
		const uint32 cp = decodeUtf8(p, end, len);
		// A 2-byte character above U+00FF is no engine text: cp949ToUtf8() only makes U+0080..U+00FF of a lone high
		// byte, and korean.fnt has no glyph for it. Such bytes are a CP949 syllable whose two bytes also form valid
		// UTF-8 (카 is C4 AB, U+012B) and must be read as CP949.
		if (cp == 0xFFFFFFFF || (!allowWide && len == 2 && cp > 0xFF))
			return false;
		p += len;
	}
	return true;
}

bool textIsUtf8(const byte *p, const byte *end) {
	return utf8Valid(p, end, false);
}

void cp949ToUtf8(const byte *src, const byte *end, Common::Array<byte> &out, uint &unmapped, bool keepCutEscape) {
	// The pairing rule is upstream's (is2ByteCharacter(): a lead 0xB0-0xD0 takes the next byte, whatever it is);
	// every other byte >= 0x80 is a glyph of the game's own charset (MI1's 0xFA, say) and becomes U+0080..U+00FF,
	// which the renderer draws from the charset again. So the 1x picture stays upstream's.
	const byte *p = src;
	while (p < end && *p) {
		// 0xFE is an escape only with the codes 1, 2, 3, 8 (a line break, ...); any other 0xFE is a lone high byte.
		const int esc = textEscape(p, end);
		if (esc < 0) {
			// A truncated escape: its NUL would become an argument for every later reader, so the engine-text boundary
			// drops it. A bundle string ends that way on purpose (its terminator is the last argument): keep it as it is.
			if (keepCutEscape)
				for (; p < end; p++)
					out.push_back(*p);
			break;
		}
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

namespace {

uint32 cutOnBoundary(const byte *text, uint32 len, int limit);

int utf8HangulCount(const byte *p, const byte *end) {
	int n = 0;
	while (p < end && *p) {
		const int esc = textEscape(p, end);
		if (esc < 0)
			break;
		if (esc) {
			p += esc;
			continue;
		}
		int len;
		const uint32 cp = decodeUtf8(p, end, len);
		if (cp >= 0xAC00 && cp <= 0xD7A3)
			n++;
		p += len;
	}
	return n;
}

int cp949PairCount(const byte *p, const byte *end) {
	int n = 0;
	while (p < end && *p) {
		const int esc = textEscape(p, end);
		if (esc < 0)
			break;
		if (esc) {
			p += esc;
			continue;
		}
		if (*p >= 0xB0 && *p <= 0xD0 && p + 1 < end && p[1]) {
			if (ksx1001CodePoint(p[0], p[1]))
				n++;
			p += 2;
		} else {
			p++;
		}
	}
	return n;
}

// Text that is UTF-8 apart from a stray byte (the raw 0xFA of one MI1 UTE line) or a cut tail: every valid character
// is kept, a byte that starts no character is read as the charset glyph U+0080..U+00FF, as cp949ToUtf8() reads one.
void tolerantUtf8(const byte *p, const byte *end, Common::Array<byte> &out, uint &stray, bool allowWide) {
	while (p < end && *p) {
		const int esc = textEscape(p, end);
		if (esc < 0)
			break;
		if (esc) {
			for (int i = 0; i < esc; i++)
				out.push_back(p[i]);
			p += esc;
			continue;
		}
		int len;
		const uint32 cp = decodeUtf8(p, end, len);
		if (cp == 0xFFFFFFFF || (!allowWide && len == 2 && cp > 0xFF)) {
			if (*p >= 0xC2 && *p <= 0xF4) {
				// A sequence cut by the end of the text: drop it.
				const int want = *p >= 0xF0 ? 4 : *p >= 0xE0 ? 3 : 2;
				bool cut = end - p < want;
				for (const byte *q = p + 1; cut && q < end; q++)
					cut = (*q & 0xC0) == 0x80;
				if (cut)
					break;
			}
			appendUtf8(*p, out);
			stray++;
			p++;
		} else {
			for (int i = 0; i < len; i++)
				out.push_back(p[i]);
			p += len;
		}
	}
}

} // End of anonymous namespace

int toEngineText(const byte *src, int srcMax, byte *dst, int dstSize, uint *unmapped, bool knownUtf8) {
	if (dstSize <= 0)
		return 0;
	const byte *end = scummTextEnd(src, src + srcMax);
	Common::Array<byte> buf;
	const byte *text;
	uint32 len;
	if (knownUtf8 && !utf8Valid(src, end, true)) {
		// Declared UTF-8 (a bundle translation, text the engine converted): never read as CP949.
		uint stray = 0;
		tolerantUtf8(src, end, buf, stray, true);
		debug(1, "Korean text: %u byte(s) of UTF-8 text start no character, read as U+0080..U+00FF", stray);
		text = buf.begin();
		len = buf.size();
	} else if (knownUtf8 || textIsUtf8(src, end)) {
		text = src;
		len = end - src;
	} else if (utf8HangulCount(src, end) > cp949PairCount(src, end)) {
		// Mostly UTF-8 (more Hangul as UTF-8 than as CP949): one bad byte does not make the whole text CP949.
		uint stray = 0;
		tolerantUtf8(src, end, buf, stray, false);
		debug(1, "Korean text: %u byte(s) of UTF-8 text start no character, read as U+0080..U+00FF", stray);
		text = buf.begin();
		len = buf.size();
	} else {
		uint n = 0;
		cp949ToUtf8(src, end, buf, n);
		if (unmapped)
			*unmapped += n;
		text = buf.begin();
		len = buf.size();
	}
	// Cut after the last whole character or escape that fits, never inside one; a truncated escape is dropped.
	const uint32 at = cutOnBoundary(text, len, dstSize - 1);
	memmove(dst, text, at);
	dst[at] = 0;
	return at;
}

namespace {

// The end of CP949 text: a lead 0xB0-0xD0 takes the next byte (so a trail 0xFE hides nothing), 0xFF starts an escape.
const byte *cp949TextEnd(const byte *p, const byte *end) {
	while (p < end && *p) {
		if (*p == 0xFF)
			p += scummEscapeLength(p, end);
		else if (*p >= 0xB0 && *p <= 0xD0 && p + 1 < end && p[1])
			p += 2;
		else
			p++;
	}
	return p;
}

// The characters of a clean cut: how many bytes of @p text (@p len long) fit into @p limit.
uint32 cutOnBoundary(const byte *text, uint32 len, int limit) {
	uint32 at = 0;
	const byte *tEnd = text + len;
	while (at < len) {
		int n = scummEscapeFull(text + at, tEnd);
		if (n < 0)
			break;
		if (!n)
			decodeUtf8(text + at, tEnd, n);
		if ((int)(at + n) > limit)
			break;
		at += n;
	}
	return at;
}

} // End of anonymous namespace

bool legacyCp949Resource(const byte *src, uint32 size, Common::Array<byte> &out) {
	if (!size || src[size - 1] != 0 || cp949TextEnd(src, src + size) != src + size - 1)
		return false;
	int pairs = 0;
	for (const byte *p = src; p < src + size - 1;) {
		if (*p == 0xFF) {
			p += scummEscapeLength(p, src + size - 1);
		} else if (*p >= 0xB0 && *p <= 0xD0 && p + 1 < src + size - 1) {
			if (ksx1001CodePoint(p[0], p[1]))
				pairs++;
			p += 2;
		} else if (*p < 0x20 && *p != '\n' && *p != '\r') {
			return false;
		} else {
			p++;
		}
	}
	if (!pairs)
		return false;
	out.clear();
	uint unmapped = 0;
	cp949ToUtf8(src, src + size - 1, out, unmapped);
	out.push_back(0);
	return true;
}

bool legacyCp949Buffer(byte *buf, int size, int &pos) {
	const byte *end = cp949TextEnd(buf, buf + size);
	int pairs = 0;
	for (const byte *p = buf; p < end; p++) {
		if (*p >= 0xB0 && *p <= 0xC8 && p + 1 < end && ksx1001CodePoint(p[0], p[1])) {
			pairs++;
			p++;
		}
	}
	if (!pairs)
		return false;
	Common::Array<byte> text, prefix;
	uint unmapped = 0;
	cp949ToUtf8(buf, end, text, unmapped, true);
	cp949ToUtf8(buf, buf + MIN<ptrdiff_t>(MAX(pos, 0), end - buf), prefix, unmapped, true);
	const uint32 n = cutOnBoundary(text.begin(), text.size(), size - 1);
	const uint32 newPos = MIN<uint32>(prefix.size(), n);
	memset(buf, 0, size);
	memcpy(buf, text.begin(), n);
	pos = newPos;
	return true;
}

bool koreanInsertBreak(byte *str, int &strLength, int at, int capacity) {
	if (at < 0 || at > strLength || (capacity && strLength + 2 > capacity))
		return false;
	memmove(str + at + 1, str + at, strLength - at + 1);
	str[at] = 0x0D;
	strLength++;
	return true;
}

int koreanCopyChars(const byte *src, int srcSize, int maxChars, byte *dst, int dstSize) {
	if (dstSize <= 0)
		return 0;
	const byte *end = scummTextEnd(src, src + srcSize);
	int at = 0, chars = 0;
	const byte *p = src;
	while (p < end && *p) {
		int n = scummEscapeFull(p, end);
		if (n < 0)
			break;
		const bool isChar = !n;
		if (isChar) {
			if (chars >= maxChars)
				break;
			decodeUtf8(p, end, n);
		}
		if (at + n > dstSize - 1)
			break;
		memcpy(dst + at, p, n);
		at += n;
		p += n;
		chars += isChar;
	}
	dst[at] = 0;
	return at;
}

Common::CodePage koreanEngineCodePage(bool utf8) {
	return utf8 ? Common::kUtf8 : Common::kWindows949;
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
