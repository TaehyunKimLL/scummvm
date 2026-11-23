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

#include "common/str.h"
#include "common/util.h"

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

int encodeUtf8Char(uint32 cp, byte *out) {
	if (cp > 0x10FFFF)
		cp = 0xFFFD;
	if (cp < 0x80) {
		out[0] = (byte)cp;
		return 1;
	}
	if (cp < 0x800) {
		out[0] = (byte)(0xC0 | (cp >> 6));
		out[1] = (byte)(0x80 | (cp & 0x3F));
		return 2;
	}
	if (cp < 0x10000) {
		out[0] = (byte)(0xE0 | (cp >> 12));
		out[1] = (byte)(0x80 | ((cp >> 6) & 0x3F));
		out[2] = (byte)(0x80 | (cp & 0x3F));
		return 3;
	}
	out[0] = (byte)(0xF0 | (cp >> 18));
	out[1] = (byte)(0x80 | ((cp >> 12) & 0x3F));
	out[2] = (byte)(0x80 | ((cp >> 6) & 0x3F));
	out[3] = (byte)(0x80 | (cp & 0x3F));
	return 4;
}

uint32 utf8WriteOffset(const byte *p, uint32 index) {
	const byte *start = p;
	int bytes;
	while (*p && index) {
		decodeUtf8Char(p, bytes);
		p += bytes;
		index--;
	}
	return (uint32)(p - start) + index;
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

bool utf8IndexIsPastEnd(const byte *p, uint32 index) {
	return index > Common::strnlen((const char *)p, 0xFFFFFFFFU);
}

uint32 utf8IndexOfOffset(const byte *p, uint32 offset) {
	uint32 pos = 0, index = 0;
	int bytes;
	while (p[pos] && pos < offset) {
		decodeUtf8Char(p + pos, bytes);
		pos += bytes;
		index++;
	}
	return index;
}

uint32 utf8PrevBoundary(const byte *p, uint32 len, uint32 pos) {
	// A code point is at most 4 bytes: try each start that would end at pos
	// and take the earliest one that decodes to exactly that span.
	for (uint32 back = 4; back >= 2; back--) {
		if (pos < back)
			continue;
		const uint32 start = pos - back;
		int bytes;
		decodeUtf8Char(p + start, bytes);
		if ((uint32)bytes == back && start + back <= len)
			return start;
	}
	return pos - 1;
}

uint32 utf8NextBoundary(const byte *p, uint32 len, uint32 pos) {
	if (pos >= len)
		return pos;
	int bytes;
	decodeUtf8Char(p + pos, bytes);
	return MIN<uint32>(pos + bytes, len);
}

Common::String utf8Truncate(const Common::String &text, uint32 capacity) {
	if (!capacity || text.size() < capacity)
		return text;
	const byte *p = (const byte *)text.c_str();
	uint32 end = 0;
	while (end < capacity - 1 && p[end]) {
		int bytes;
		decodeUtf8Char(p + end, bytes);
		if (end + bytes > capacity - 1)
			break;
		end += bytes;
	}
	return Common::String(text.c_str(), end);
}

// Drop a code point at the end of @p text that its lead byte says is incomplete.
static Common::String dropIncompleteTail(const Common::String &text) {
	const uint32 size = text.size();
	for (uint32 back = 1; back <= 4 && back <= size; ++back) {
		const byte b = (byte)text[size - back];
		if ((b & 0xC0) == 0x80)
			continue; // a continuation byte: look further back
		const uint32 need = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
		return need > back ? Common::String(text.c_str(), size - back) : text;
	}
	return text;
}

Utf8CopyPlan utf8CopyPlan(const Common::String &text, uint32 n, int32 maxSize, bool growable) {
	Utf8CopyPlan plan;
	plan.growTo = 0;
	plan.cut = false;
	const bool unbounded = n == 0xFFFFFFFFU;
	uint32 capacity = maxSize > 0 ? (uint32)maxSize : 0;
	if (unbounded && growable && capacity && text.size() >= capacity) {
		plan.growTo = text.size() + 1;
		capacity = plan.growTo;
	}
	plan.text = text;
	if (capacity && text.size() >= capacity)
		plan.text = utf8Truncate(text, capacity);
	else if (!unbounded && text.size() >= n)
		plan.text = dropIncompleteTail(text);
	plan.cut = plan.text.size() < text.size();
	if (unbounded)
		plan.limit = plan.text.size() + 1;
	else
		plan.limit = capacity ? MIN<uint32>(n, capacity) : n;
	return plan;
}

Utf8WriteResult utf8ReplaceAt(Common::String &text, uint32 index, uint32 cp, uint32 capacity, uint32 &old) {
	const byte *p = (const byte *)text.c_str();
	const uint32 chars = utf8Length(p);
	old = 0;
	if (index > chars)
		return utf8IndexIsPastEnd(p, index) ? kUtf8ByteOffset : kUtf8Refused;
	const uint32 at = utf8OffsetOf(p, index);
	int bytes = 0;
	if (index < chars)
		old = decodeUtf8Char(p + at, bytes) & 0xFFFF;
	Common::String head(text.c_str(), at);
	Common::String tail(text.c_str() + at + bytes);
	Common::String enc;
	if (cp) {
		byte buf[4];
		enc = Common::String((const char *)buf, encodeUtf8Char(cp, buf));
	} else {
		tail.clear(); // a NUL ends the string
	}
	if (capacity) {
		if (head.size() + enc.size() + 1 > capacity)
			return kUtf8Refused;
		tail = utf8Truncate(tail, capacity - head.size() - enc.size());
	}
	text = head + enc + tail;
	return kUtf8Replaced;
}

} // End of namespace Sci
