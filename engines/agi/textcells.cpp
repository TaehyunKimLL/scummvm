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

#include "agi/textcells.h"

#include "common/ustr.h"
#include "common/util.h"

namespace Agi {
namespace TextCells {

static const int16 kHeightMax = 20; // text.h HEIGHT_MAX

bool hasHighBytes(const char *text) {
	for (; *text; text++)
		if ((byte)*text >= 0x80)
			return true;
	return false;
}

int decode(const char *text, bool utf8, uint32 &cp) {
	const byte b0 = (byte)text[0];
	if (utf8 && b0 >= 0x80) {
		uint32 v = 0;
		int extra = 0;
		if ((b0 & 0xE0) == 0xC0) {
			v = b0 & 0x1F;
			extra = 1;
		} else if ((b0 & 0xF0) == 0xE0) {
			v = b0 & 0x0F;
			extra = 2;
		} else if ((b0 & 0xF8) == 0xF0) {
			v = b0 & 0x07;
			extra = 3;
		}
		bool valid = extra != 0;
		for (int i = 1; valid && i <= extra; i++)
			valid = ((byte)text[i] & 0xC0) == 0x80;
		if (valid) {
			for (int i = 1; i <= extra; i++)
				v = (v << 6) | ((byte)text[i] & 0x3F);
			// The shortest form only, no surrogates, nothing past U+10FFFF:
			// anything else is malformed, its lead byte one cell.
			static const uint32 kMinForExtra[4] = { 0, 0x80, 0x800, 0x10000 };
			valid = v >= kMinForExtra[extra] && !(v >= 0xD800 && v <= 0xDFFF) && v <= 0x10FFFF;
		}
		if (valid) {
			cp = v;
			return extra + 1;
		}
	}
	cp = b0;
	return 1;
}

int16 cellLen(const char *text, bool utf8) {
	int16 cells = 0;
	while (*text) {
		uint32 cp;
		text += decode(text, utf8, cp);
		cells++;
	}
	return cells;
}

int16 measure(const char *text, int16 byteLen, bool utf8, int16 maxCells, int16 *bytesForMaxCellsPtr) {
	int16 cells = 0, pos = 0, bytesForMaxCells = byteLen;
	while (pos < byteLen) {
		if (cells == maxCells)
			bytesForMaxCells = pos;
		uint32 cp;
		pos += decode(text + pos, utf8, cp);
		cells++;
	}
	if (cells == maxCells)
		bytesForMaxCells = pos;
	if (bytesForMaxCellsPtr)
		*bytesForMaxCellsPtr = bytesForMaxCells;
	return cells;
}

Common::String clipCells(const char *text, int16 maxCells, bool utf8) {
	const char *p = text;
	int16 cells = 0;
	while (*p && cells < maxCells) {
		uint32 cp;
		p += decode(p, utf8, cp);
		cells++;
	}
	return Common::String(text, p - text);
}

Common::String clipBytes(const char *text, int16 maxBytes, bool utf8) {
	const char *p = text;
	while (*p) {
		uint32 cp;
		const int n = decode(p, utf8, cp);
		if ((p - text) + n > maxBytes)
			break;
		p += n;
	}
	return Common::String(text, p - text);
}

Common::String fromCells(const uint32 *cells, bool utf8) {
	if (!utf8) {
		Common::String out;
		for (; *cells; cells++)
			out += (char)(*cells & 0xFF);
		return out;
	}
	Common::U32String u;
	for (; *cells; cells++)
		u += (Common::u32char_type_t)*cells;
	return u.encode(Common::kUtf8);
}

int16 toCells(const char *text, uint32 *cells, int16 capacity, bool utf8) {
	int16 n = 0;
	while (*text && n < capacity - 1) {
		uint32 cp;
		text += decode(text, utf8, cp);
		cells[n++] = cp;
	}
	cells[n] = 0;
	return n;
}

int16 cellCount(const uint32 *cells) {
	int16 n = 0;
	while (cells[n])
		n++;
	return n;
}

bool isHangulSyllable(uint32 cp) {
	return cp >= 0xAC00 && cp <= 0xD7A3;
}

static Common::String fold(const char *s) {
	Common::String out;
	for (; *s; s++) {
		const byte c = (byte)*s;
		switch (c) {
		case 0x20: case 0x09: case '-': case '.': case ',': case ':': case ';': case '!': case '\'':
			break;
		default:
			out += (c < 0x80) ? (char)tolower(c) : (char)c;
			break;
		}
	}
	return out;
}

bool agiStringsEqual(const char *a, const char *b) {
	return fold(a) == fold(b);
}

void wordWrap(const char *originalText, int16 maxWidth, bool utf8, bool hangulAny, Common::String &out,
              int16 &width, int16 &height, bool *truncated) {
	int16 boxWidth = 0, boxHeight = 0, lineWidth = 0, lineWidthLeft = maxWidth;
	int16 wordStartPos = 0, curReadPos = 0;
	bool stopped = false, droppedWord = false;
	out.clear();

	while (originalText[curReadPos]) {
		if (originalText[curReadPos] == ' ')
			curReadPos++;
		while (originalText[curReadPos]) {
			if (originalText[curReadPos] == ' ' || originalText[curReadPos] == 0x0A)
				break;
			uint32 cp;
			curReadPos += (int16)decode(&originalText[curReadPos], utf8, cp);
		}
		const byte wordEndChar = originalText[curReadPos];
		int16 wordLen = curReadPos - wordStartPos;
		int16 wordCells = measure(&originalText[wordStartPos], wordLen, utf8);

		if (wordCells >= lineWidthLeft && hangulAny && lineWidth > 0) {
			// [layout] hangul=any: Hangul breaks between syllables, so a word
			// that would move down fills the rest of this line first (the
			// leading space stays on it, one cell kept free as the >= rule does).
			const int16 lead = (originalText[wordStartPos] == ' ') ? 1 : 0;
			const int16 fit = lineWidthLeft - 1 - lead;
			uint32 first = 0;
			decode(&originalText[wordStartPos + lead], utf8, first);
			if (fit > 0 && isHangulSyllable(first)) {
				int16 bytes = 0;
				measure(&originalText[wordStartPos + lead], wordLen - lead, utf8, fit, &bytes);
				out += Common::String(&originalText[wordStartPos], lead + bytes);
				lineWidth += lead + fit;
				lineWidthLeft -= lead + fit;
				wordStartPos += lead + bytes;
				wordLen -= lead + bytes;
				wordCells -= lead + fit;
			}
		}

		if (wordCells >= lineWidthLeft) {
			if (wordLen && originalText[wordStartPos] == ' ') {
				wordStartPos++;
				wordLen--;
				wordCells--;
			}
			if (wordCells > maxWidth) {
				// split on a character boundary: the bytes of the first maxWidth cells
				int16 bytesForMaxWidth = wordLen;
				measure(&originalText[wordStartPos], wordLen, utf8, maxWidth, &bytesForMaxWidth);
				wordLen = bytesForMaxWidth;
				wordCells = maxWidth;
				curReadPos = wordStartPos + wordLen;
			}
			out += '\n';
			if (lineWidth > boxWidth)
				boxWidth = lineWidth;
			boxHeight++;
			lineWidth = 0;
			lineWidthLeft = maxWidth;
			if (boxHeight >= kHeightMax) {
				stopped = true;
				droppedWord = true; // the word that did not fit is lost whatever follows
				break;
			}
		}

		out += Common::String(&originalText[wordStartPos], wordLen);
		lineWidth += wordCells;
		lineWidthLeft -= wordCells;

		if (wordEndChar == 0x0A) {
			curReadPos++;
			out += '\n';
			if (lineWidth > boxWidth)
				boxWidth = lineWidth;
			boxHeight++;
			lineWidth = 0;
			lineWidthLeft = maxWidth;
			if (boxHeight >= kHeightMax) {
				stopped = true;
				break;
			}
		}
		wordStartPos = curReadPos;
	}

	if (curReadPos > 0) {
		if (lineWidth > boxWidth)
			boxWidth = lineWidth;
		boxHeight++;
	}
	width = boxWidth;
	height = boxHeight;
	if (truncated)
		*truncated = droppedWord || (stopped && originalText[curReadPos] != 0);
}

} // End of namespace TextCells
} // End of namespace Agi
