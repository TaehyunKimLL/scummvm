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

#include "scumm/text_utf8.h"

#include "common/memstream.h"
#include "common/str.h"
#include "common/ustr.h"
#include "scumm/charset.h"
#include "scumm/trs_bundle.h"

namespace Scumm {

int textCharLength(bool utf8, Common::Language lang, const byte *p, const byte *end) {
	if (!utf8)
		return is2ByteCharacter(lang, *p) ? 2 : 1;

	const byte lead = *p;
	int len = 1;
	if ((lead & 0xE0) == 0xC0)
		len = 2;
	else if ((lead & 0xF0) == 0xE0)
		len = 3;
	else if ((lead & 0xF8) == 0xF0)
		len = 4;
	if (end - p < len)
		return 1;
	for (int k = 1; k < len; k++) {
		if ((p[k] & 0xC0) != 0x80)
			return 1;
	}
	return len;
}

uint32 readUtf8TextChar(const byte *&p, const byte *end) {
	if (p >= end) {
		return 0;
	}
	const Graphics::Utf8TextDecoder dec;
	uint32 cp = 0;
	byte flags = 0;
	int n = dec.decode(p, end, cp, flags);
	if (n < 1)
		n = 1;
	p += n;
	if (cp > 0xFFFF)
		cp = 0xFFFD;
	return cp;
}

/** Codes that carry two argument bytes (charset.cpp, string.cpp). */
static bool escapeHasArgs(byte code) {
	return code == 9 || code == 10 || code == 12 || code == 13 || code == 14 || code == 21;
}

int ScummTextDecoder::escapeLength(const byte *p, const byte *end) const {
	if (_he)
		return 0;
	if (!(*p == 0xFF || (_version <= 6 && *p == 0xFE)))
		return 0;
	if (end - p < 2)
		return (int)(end - p);
	int len = 2;
	if (escapeHasArgs(p[1]))
		len += 2;
	if (end - p < len)
		len = (int)(end - p);
	return len;
}

int ScummTextDecoder::decode(const byte *p, const byte *end, uint32 &cp, byte &flags) const {
	flags = 0;
	const int esc = escapeLength(p, end);
	if (esc > 0) {
		cp = Graphics::kControlUnit;
		flags = Graphics::kUnitControl;
		if (esc >= 2) {
			const byte code = p[1];
			if (code == 1 || (code == 8 && _verbNewline) || (_newLineChar != 0 && code == _newLineChar))
				flags |= Graphics::kUnitNewline;
		}
		return esc;
	}
	if (*p == '@') {
		cp = Graphics::kControlUnit;
		flags = Graphics::kUnitControl;
		return 1;
	}
	if (*p == 0x0D) {
		cp = 0x0D;
		flags = Graphics::kUnitNewline;
		return 1;
	}
	if (*p < 0x80) {
		// 0x0A included: SCUMM draws it as a character, never breaks on it.
		cp = *p;
		return 1;
	}
	return _utf8.decode(p, end, cp, flags);
}

int ScummLayoutHooks::extend(const Graphics::TextRun &run, uint32 from, uint32 i, int widthSoFar) {
	if ((run.flags(i) & Graphics::kUnitControl) && _text) {
		const uint32 at = run.byteOffset(i);
		const uint32 len = run.byteOffset(i + 1) - at;
		if (len >= 2)
			escape(_text[at + 1], len >= 4 ? _text + at + 2 : nullptr);
		return widthSoFar;
	}
	return LayoutMetrics::extend(run, from, i, widthSoFar);
}

namespace {

/// Where wrapping stops: the NUL, or a 'Wait' / 'no newline' escape.
int wrapRegionEnd(const byte *str, int pos, const ScummTextDecoder &dec) {
	const byte *p = str + pos;
	const byte *end = p + strlen((const char *)p);
	while (p < end) {
		const int esc = dec.escapeLength(p, end);
		if (esc >= 2 && (p[1] == 3 || p[1] == 2))
			break;
		p += esc > 0 ? esc : 1;
	}
	return (int)(p - str);
}

/// Replay the escapes of units [0, upto) so the hooks' state (the charset)
/// is the one in effect where a line starts.
void rewindHooks(ScummLayoutHooks &hooks, const Graphics::TextRun &run, uint32 upto) {
	hooks.reset();
	int w = 0;
	for (uint32 i = 0; i < upto; i++) {
		if (run.flags(i) & Graphics::kUnitControl)
			w = hooks.extend(run, 0, i, w);
	}
}

struct Break {
	uint32 at;       ///< byte offset from the start of the wrapped text
	bool replace;    ///< the byte there is a space that becomes 0x0D
};

} // End of anonymous namespace

void layoutLinebreaks(byte *str, int bufSize, int pos, int maxwidth, ScummLayoutHooks &hooks,
					  const Graphics::BreakRules &rules, int version, byte newLineChar, int a) {
	if (!str || pos < 0 || pos >= bufSize)
		return;
	ScummTextDecoder dec(version, false, newLineChar);
	dec.setVerbNewline(a == 1);

	// FF 08 'verb on next line': a newline for a == 1; otherwise the spaces
	// after it are hidden as '@', as addLinebreaks() does.
	const int regionEnd = wrapRegionEnd(str, pos, dec);
	for (int i = pos; i < regionEnd;) {
		const int esc = dec.escapeLength(str + i, str + regionEnd);
		if (esc >= 2 && str[i + 1] == 8 && a != 1) {
			int j = i + esc;
			while (j < regionEnd && str[j] == ' ')
				str[j++] = '@';
		}
		i += esc > 0 ? esc : 1;
	}

	byte *text = str + pos;
	const uint32 len = (uint32)(regionEnd - pos);
	Graphics::TextRun run;
	run.decode(text, len, dec);
	hooks._text = text;

	Common::Array<Break> breaks;
	const uint32 n = run.size();
	uint32 from = 0;
	rewindHooks(hooks, run, 0);
	while (from < n) {
		Graphics::LineSpan l = Graphics::TextLayout::fitLine(run, from, maxwidth - 1, hooks, rules);
		if (l.emergency && !l.forced) {
			// Nothing fitted. SCUMM never splits a word: the line runs on to
			// the first break opportunity, as addLinebreaks() lets it.
			uint32 k = l.end + 1;
			while (k < n && !(run.flags(k) & Graphics::kUnitNewline) &&
				   !Graphics::TextLayout::canBreakBefore(run, k, rules))
				k++;
			if (k >= n)
				break;
			if (run.flags(k) & Graphics::kUnitNewline) {
				from = k + 1;
				rewindHooks(hooks, run, from);
				continue;
			}
			l.end = l.next = k;
			while (l.end > from && (run.flags(l.end - 1) & Graphics::kUnitSpace))
				l.end--;
		}
		if (l.forced) {
			from = l.next;
			rewindHooks(hooks, run, from);
			continue;
		}
		if (l.next >= n || l.end >= n)
			break;

		// Where to write the break, the way addLinebreaks() writes it. It
		// breaks at the last space it has seen when a character overflows,
		// so a space that itself overflows is the break and the spaces after
		// it start the next line; and when a wide character (Hangul under
		// "break anywhere") follows the spaces, it breaks before that
		// character and the spaces stay at the end of the line.
		uint32 overflow = n;
		{
			rewindHooks(hooks, run, from);
			int w = 0;
			for (uint32 k = from; k < n && k < l.next + 1; k++) {
				w = hooks.extend(run, from, k, w);
				if (k >= l.end && w > maxwidth - 1) {
					overflow = k;
					break;
				}
			}
		}
		uint32 space = n;
		for (uint32 k = l.end; k < l.next && k <= overflow; k++) {
			if (run.cp(k) == ' ')
				space = k;
		}
		Break b;
		uint32 nextFrom = l.next;
		const bool overflowInSpaces = overflow < l.next;
		const bool wideNext = l.next < n && (run.flags(l.next) & Graphics::kUnitWide);
		if (space < n && (overflowInSpaces || !wideNext)) {
			b.at = run.byteOffset(space);
			b.replace = true;
			nextFrom = space + 1;
		} else {
			b.at = run.byteOffset(l.next);
			b.replace = false;
		}
		breaks.push_back(b);
		if (nextFrom <= from)
			break;    // no progress: cannot happen, fitLine takes a cluster at least
		from = nextFrom;
		rewindHooks(hooks, run, from);
	}
	hooks._text = nullptr;

	// Apply, front to back; every insert shifts the rest by one byte.
	int strLen = pos + (int)strlen((const char *)text);
	int shift = 0;
	for (uint k = 0; k < breaks.size(); k++) {
		const int at = pos + (int)breaks[k].at + shift;
		if (breaks[k].replace) {
			str[at] = 0x0D;
			continue;
		}
		// Room for one more byte plus the NUL; otherwise drop the text's
		// last character, on a character boundary, while it lies after the
		// break.
		while (strLen + 2 > bufSize) {
			int last = at;
			for (int q = at; q < strLen;) {
				last = q;
				q += textCharLength(true, Common::UNK_LANG, str + q, str + strLen);
			}
			if (last <= at)
				break;
			strLen = last;
			str[strLen] = 0;
		}
		if (strLen + 2 > bufSize || at >= strLen)
			break;
		memmove(str + at + 1, str + at, strLen - at + 1);
		str[at] = 0x0D;
		strLen++;
		shift++;
	}
}

bool parseTrsHeader(const byte *data, uint32 size, TrsHeader &h) {
	h = TrsHeader();
	if (!data || size < 10)
		return false;
	if (memcmp(data, "SCVMTRS ", 8) != 0)
		return false;
	Common::MemoryReadStream s(data, size);
	s.seek(8);
	const uint32 num = s.readUint16LE();
	if (10 + num * 10 > size)
		return false;
	h.numLines = num;
	h.originalOffset.resize(num);
	h.translatedOffset.resize(num);
	for (uint32 i = 0; i < num; i++) {
		s.readUint16LE();
		h.originalOffset[i] = s.readUint32LE();
		h.translatedOffset[i] = s.readUint32LE();
	}
	if (s.pos() + 1 > (int64)size)
		return false;
	const byte rooms = s.readByte();
	for (uint32 r = 0; r < rooms; r++) {
		if (s.pos() + 3 > (int64)size)
			return false;
		s.readByte();
		const uint16 scripts = s.readUint16LE();
		if (s.pos() + scripts * 8 > (int64)size)
			return false;
		s.skip(scripts * 8);
	}
	h.bodyPos = (uint32)s.pos();
	return true;
}

namespace {

/// Valid UTF-8 (escapes skipped) with at least one multi-byte sequence.
bool looksLikeUtf8(const byte *p, const byte *end) {
	const ScummTextDecoder dec(6, false, 0);
	bool multi = false;
	while (p < end) {
		if (*p == 0) {
			p++;
			continue;
		}
		const int esc = dec.escapeLength(p, end);
		if (esc > 0) {
			p += esc;
			continue;
		}
		if (*p < 0x80) {
			p++;
			continue;
		}
		const int n = textCharLength(true, Common::UNK_LANG, p, end);
		if (n < 2)
			return false;
		// Overlong and surrogate forms are not UTF-8 either.
		uint32 cp = 0;
		byte flags = 0;
		Graphics::Utf8TextDecoder u;
		if (u.decode(p, end, cp, flags) != n || cp == 0xFFFD)
			return false;
		multi = true;
		p += n;
	}
	return multi;
}

} // End of anonymous namespace

bool decideTrsUtf8(const byte *data, uint32 size, const TrsHeader &h, bool iniUtf8, bool *looksUtf8) {
	if (looksUtf8)
		*looksUtf8 = false;
	if (h.bodyPos > size)
		return iniUtf8;
	const byte *body = data + h.bodyPos;
	const uint32 bodySize = size - h.bodyPos;
	if (trsBodyIsUtf8(body, bodySize) || iniUtf8)
		return true;
	if (looksUtf8)
		*looksUtf8 = looksLikeUtf8(body, body + bodySize);
	return false;
}

void transcodeScummText(const byte *src, uint32 len, Common::CodePage from, Common::CodePage to,
						Common::Array<byte> &out, Common::HashMap<uint32, bool> *unmapped) {
	out.clear();
	const ScummTextDecoder esc(6, false, 0);
	const Graphics::Utf8TextDecoder utf8;
	const Graphics::CodePageTextDecoder page(from);
	const byte *p = src;
	const byte *end = src + len;
	while (p < end) {
		const int e = esc.escapeLength(p, end);
		if (e > 0) {
			for (int k = 0; k < e; k++)
				out.push_back(p[k]);
			p += e;
			continue;
		}
		if (*p < 0x80) {
			out.push_back(*p++);
			continue;
		}
		uint32 cp = 0;
		byte flags = 0;
		int n = (from == Common::kUtf8) ? utf8.decode(p, end, cp, flags) : page.decode(p, end, cp, flags);
		if (n < 1)
			n = 1;
		if (cp == 0xFFFD && !(from == Common::kUtf8 && n == 3 && p[0] == 0xEF && p[1] == 0xBF && p[2] == 0xBD)) {
			// Not a character of this page: keep the bytes.
			for (int k = 0; k < n; k++)
				out.push_back(p[k]);
			p += n;
			continue;
		}
		p += n;
		const Common::String enc = Common::U32String(Common::u32char_type_t(cp)).encode(to);
		if (enc.empty() || (enc.size() == 1 && enc[0] == '?')) {
			out.push_back('?');
			if (unmapped)
				unmapped->setVal(cp, true);
			continue;
		}
		for (uint k = 0; k < enc.size(); k++)
			out.push_back((byte)enc[k]);
	}
}

Common::CodePage legacyTextPage(Common::Language lang) {
	switch (lang) {
	case Common::KO_KOR:
		return Common::kWindows949;
	case Common::JA_JPN:
		return Common::kWindows932;
	case Common::ZH_CHN:
		return Common::kWindows936;
	case Common::ZH_TWN:
		return Common::kWindows950;
	default:
		return Common::kCodePageInvalid;
	}
}

} // End of namespace Scumm
