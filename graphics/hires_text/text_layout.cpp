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

#include "graphics/hires_text/text_layout.h"

#include "common/str.h"
#include "graphics/hires_text/unicode_props.h"

namespace Graphics {

namespace {

const uint32 kReplacement = 0xFFFD;

bool isContinuation(byte b, byte lo = 0x80, byte hi = 0xBF) {
	return b >= lo && b <= hi;
}

/** Flags the run adds by code point: space, combining, wide. */
byte classify(uint32 cp) {
	if (cp == 0x20 || cp == 0x3000)
		return kUnitSpace;
	if (Unicode::isCombining(cp))
		return kUnitCombining;
	if (Unicode::isWide(cp))
		return kUnitWide;
	return 0;
}

} // End of anonymous namespace

// --- Decoders -----------------------------------------------------------

int Utf8TextDecoder::decode(const byte *p, const byte *end, uint32 &cp, byte &flags) const {
	flags = 0;
	const byte b0 = p[0];
	if (b0 < 0x80) {
		cp = b0;
		if (b0 == '\n')
			flags = kUnitNewline;
		return 1;
	}

	// RFC 3629 table 3.7: the second byte's range depends on the lead, which
	// rules out overlong forms, surrogates and anything past U+10FFFF.
	int len;
	byte lo = 0x80, hi = 0xBF;
	uint32 value;
	if (b0 >= 0xC2 && b0 <= 0xDF) {
		len = 2;
		value = b0 & 0x1F;
	} else if (b0 >= 0xE0 && b0 <= 0xEF) {
		len = 3;
		value = b0 & 0x0F;
		if (b0 == 0xE0)
			lo = 0xA0;
		else if (b0 == 0xED)
			hi = 0x9F;
	} else if (b0 >= 0xF0 && b0 <= 0xF4) {
		len = 4;
		value = b0 & 0x07;
		if (b0 == 0xF0)
			lo = 0x90;
		else if (b0 == 0xF4)
			hi = 0x8F;
	} else {
		cp = kReplacement;
		return 1;
	}

	if (end - p < len || !isContinuation(p[1], lo, hi)) {
		cp = kReplacement;
		return 1;
	}
	value = (value << 6) | (p[1] & 0x3F);
	for (int k = 2; k < len; k++) {
		if (!isContinuation(p[k])) {
			cp = kReplacement;
			return 1;
		}
		value = (value << 6) | (p[k] & 0x3F);
	}
	cp = value;
	return len;
}

/**
 * How many bytes the character starting at @p p takes, in @p page.
 *
 * Returns 1 for anything that is not a lead byte, so a caller always makes
 * progress and never splits a string mid-character.
 *
 * Taken verbatim from SCUMM's charLength() (engines/scumm/hires_text.cpp),
 * which keeps its own copy until it switches to this decoder.
 */
int CodePageTextDecoder::charLength(Common::CodePage page, const byte *p, const byte *end) {
	const byte lead = *p;

	switch (page) {
	case Common::kUtf8:
		if (lead < 0x80)
			return 1;
		if ((lead & 0xE0) == 0xC0)
			return 2;
		if ((lead & 0xF0) == 0xE0)
			return 3;
		if ((lead & 0xF8) == 0xF0)
			return 4;
		return 1;   // a stray continuation byte

	case Common::kWindows932:
		// Shift-JIS: two lead byte ranges. Everything between them, including
		// half-width katakana at 0xA1..0xDF, is a single byte character - a
		// reminder that byte width says nothing about which script it is.
		return ((lead >= 0x81 && lead <= 0x9F) || (lead >= 0xE0 && lead <= 0xFC)) ? 2 : 1;

	case Common::kWindows936:
	case Common::kWindows950:
		return (lead >= 0x81 && lead <= 0xFE) ? 2 : 1;

	case Common::kWindows949:
		return (lead >= 0x81 && lead <= 0xFE) ? 2 : 1;

	case Common::kJohab:
		return (lead >= 0x84 && lead <= 0xF9) ? 2 : 1;

	default:
		// A single byte page, or none named at all.
		return 1;
	}

	(void)end;
}

int CodePageTextDecoder::decode(const byte *p, const byte *end, uint32 &cp, byte &flags) const {
	flags = 0;
	int len = charLength(_page, p, end);
	if (end - p < len) {
		// A lead byte whose trail is missing: one byte, nothing to convert.
		cp = kReplacement;
		return 1;
	}

	// ASCII is ASCII in every page here, and converting it would need the
	// CJK tables loaded. With no page named, the byte is the code point.
	if ((len == 1 && *p < 0x80) || _page == Common::kCodePageInvalid) {
		cp = *p;
		if (cp == '\n')
			flags = kUnitNewline;
		return 1;
	}

	const Common::U32String decoded(Common::String((const char *)p, len), _page);
	cp = decoded.empty() ? kReplacement : (uint32)decoded[0];
	return len;
}

// --- TextRun ------------------------------------------------------------

void TextRun::clear() {
	// Common::Array::clear() frees the storage; resize(0) keeps it.
	_cp.resize(0);
	_offset.resize(0);
	_flags.resize(0);
}

void TextRun::push(uint32 cp, byte flags, uint32 offset) {
	if (!(flags & kUnitControl))
		flags |= classify(cp);
	_cp.push_back(cp);
	_flags.push_back(flags);
	_offset.push_back(offset);
}

void TextRun::decode(const byte *text, uint32 len, const TextDecoder &dec) {
	clear();
	// At most one unit per byte; reserve() does nothing once the run has
	// grown that far, so a reused run allocates only for longer strings.
	_cp.reserve(len);
	_flags.reserve(len);
	_offset.reserve(len + 1);

	const byte *const end = text + len;
	uint32 pos = 0;
	while (pos < len) {
		uint32 cp = 0;
		byte flags = 0;
		int n = dec.decode(text + pos, end, cp, flags);
		if (n < 1)
			n = 1;
		if ((uint32)n > len - pos)
			n = len - pos;
		push(cp, flags, pos);
		pos += n;
	}
	_offset.push_back(len);
}

void TextRun::assign(const Common::U32String &text) {
	clear();
	const uint32 len = text.size();
	_cp.reserve(len);
	_flags.reserve(len);
	_offset.reserve(len + 1);
	for (uint32 i = 0; i < len; i++) {
		const uint32 cp = text[i];
		push(cp, cp == '\n' ? (byte)kUnitNewline : (byte)0, i);
	}
	_offset.push_back(len);
}

// --- LayoutMetrics ------------------------------------------------------

int LayoutMetrics::width(const TextRun &run, uint32 from, uint32 to) {
	int w = 0;
	for (uint32 i = from; i < to; i++) {
		if (run.flags(i) & (kUnitControl | kUnitCombining))
			continue;
		w += advance(run.cp(i));
	}
	return w;
}

// --- TextLayout ---------------------------------------------------------

namespace TextLayout {

namespace {

/** A control unit that is not a newline: glued to the text after it. */
bool isGlue(const TextRun &run, uint32 i) {
	const byte f = run.flags(i);
	return (f & kUnitControl) && !(f & kUnitNewline);
}

bool isHangul(uint32 cp) {
	return (cp >= 0x1100 && cp <= 0x11FF)     // Hangul Jamo
		|| (cp >= 0x3130 && cp <= 0x318F)     // Hangul Compatibility Jamo
		|| (cp >= 0xA960 && cp <= 0xA97F)     // Hangul Jamo Extended-A
		|| (cp >= 0xAC00 && cp <= 0xD7A3)     // Hangul Syllables
		|| (cp >= 0xD7B0 && cp <= 0xD7FF);    // Hangul Jamo Extended-B
}

/** ASCII closers that may not begin a line after a wide unit (JIS X 4051). */
bool isAsciiCloser(uint32 cp) {
	return cp == ')' || cp == ']' || cp == '}' || cp == ',' || cp == '.'
		|| cp == '!' || cp == '?' || cp == ':' || cp == ';';
}

/** ASCII openers that may not end a line before a wide unit. */
bool isAsciiOpener(uint32 cp) {
	return cp == '(' || cp == '[' || cp == '{';
}

/** Rule 4: a wide unit is a break opportunity on either side. */
bool breaksAsWide(uint32 cp, byte flags, const BreakRules &rules) {
	if (!(flags & kUnitWide))
		return false;
	return rules.hangul == kHangulBreakAny || !isHangul(cp);
}

/** Unit i is Thai, or a combining mark on a Thai base. */
bool isThaiContext(const TextRun &run, uint32 i) {
	while (i > 0 && (run.flags(i) & kUnitCombining))
		i--;
	const uint32 cp = run.cp(i);
	return cp >= 0x0E00 && cp <= 0x0E7F;
}

void finish(const TextRun &run, LineSpan &l, LayoutMetrics &m) {
	l.byteStart = run.byteOffset(l.first);
	l.byteEnd = run.byteOffset(l.end);
	l.byteNext = run.byteOffset(l.next);
	l.width = l.end > l.first ? m.width(run, l.first, l.end) : 0;
}

void trimTrailingSpaces(const TextRun &run, LineSpan &l) {
	while (l.end > l.first && (run.flags(l.end - 1) & kUnitSpace))
		l.end--;
}

} // End of anonymous namespace

bool canBreakBefore(const TextRun &run, uint32 i, const BreakRules &rules) {
	const uint32 n = run.size();
	if (i == 0 || i >= n)
		return false;

	// 1. Never right after an escape, and never before a combining mark.
	const byte fa = run.flags(i - 1);
	if (isGlue(run, i - 1))
		return false;
	// A run of escapes is judged by the unit after it.
	uint32 j = i;
	while (j < n && isGlue(run, j))
		j++;
	if (j >= n)
		return false;
	const byte fb = run.flags(j);
	if (fb & kUnitCombining)
		return false;
	if ((fa & kUnitNewline) || (fb & kUnitNewline))
		return true;

	const uint32 a = run.cp(i - 1);
	const uint32 b = run.cp(j);

	// 2. After a space run.
	if ((fa & kUnitSpace) && !(fb & kUnitSpace))
		return true;

	// 3. Kinsoku.
	if (rules.kinsoku) {
		if (Unicode::kinsokuNoStart(b) || (isAsciiCloser(b) && (fa & kUnitWide)))
			return false;
		if (Unicode::kinsokuNoEnd(a) || (isAsciiOpener(a) && (fb & kUnitWide)))
			return false;
	}

	// 4. Ideographic: either side wide.
	if (breaksAsWide(a, fa, rules) || breaksAsWide(b, fb, rules))
		return true;

	// 5. Thai syllable-ish fallback.
	if (rules.thaiFallback && Unicode::isThaiBase(b) && !Unicode::isThaiLeadingVowel(a)
			&& !Unicode::isThaiFollowingVowel(b) && isThaiContext(run, i - 1))
		return true;

	// 6. Inside a word.
	return false;
}

bool isClusterBoundary(const TextRun &run, uint32 i) {
	if (i == 0 || i >= run.size())
		return true;
	if (run.flags(i) & kUnitCombining)
		return false;
	return !isGlue(run, i - 1);
}

LineSpan fitLine(const TextRun &run, uint32 from, int maxWidth, LayoutMetrics &m, const BreakRules &rules) {
	const uint32 n = run.size();
	LineSpan l;
	l.first = l.end = l.next = from;
	if (from >= n) {
		l.first = l.end = l.next = n;
		finish(run, l, m);
		return l;
	}

	uint32 lastBreak = from;   // from itself means "none"
	uint32 i = from;
	for (; i < n; i++) {
		if (run.flags(i) & kUnitNewline) {
			l.end = i;
			l.next = i + 1;
			l.forced = true;
			trimTrailingSpaces(run, l);
			finish(run, l, m);
			return l;
		}
		if (i > from && canBreakBefore(run, i, rules))
			lastBreak = i;
		// Spaces hang past the edge: they are dropped at a line end anyway.
		if (run.flags(i) & kUnitSpace)
			continue;
		if (m.width(run, from, i + 1) > maxWidth)
			break;
	}

	if (i >= n) {
		l.end = l.next = n;
		trimTrailingSpaces(run, l);
		finish(run, l, m);
		return l;
	}

	// Unit i does not fit.
	uint32 end = lastBreak;
	if (end == from) {
		l.emergency = true;
		end = i;
		while (end > from && !isClusterBoundary(run, end))
			end--;
		if (end == from) {
			// Not even the first cluster fits: it goes on the line alone.
			end = from + 1;
			while (end < n && !isClusterBoundary(run, end))
				end++;
		}
	}

	l.end = l.next = end;
	while (l.next < n && (run.flags(l.next) & kUnitSpace) && !(run.flags(l.next) & kUnitNewline))
		l.next++;
	trimTrailingSpaces(run, l);
	finish(run, l, m);
	return l;
}

void breakLines(const TextRun &run, int maxWidth, LayoutMetrics &m, const BreakRules &rules,
				Common::Array<LineSpan> &out) {
	out.resize(0);
	uint32 from = 0;
	while (from < run.size()) {
		const LineSpan l = fitLine(run, from, maxWidth, m, rules);
		out.push_back(l);
		from = l.next;
	}
}

} // End of namespace TextLayout

} // End of namespace Graphics
