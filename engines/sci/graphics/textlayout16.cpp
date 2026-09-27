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

#include "sci/graphics/textlayout16.h"

#include "graphics/hires_text/latin_advance.h"
#include "sci/utf8.h"

namespace Sci {

bool hiresTextApplies(SciVersion v, Common::CodePage page, bool utf8Translation, Common::String &why) {
	if (v >= SCI_VERSION_2) {
		why = "SCI32 games do not support it yet";
		return false;
	}
	if (utf8Translation)
		return true;
	switch (page) {
	case Common::kWindows949:
	case Common::kWindows932:
	case Common::kWindows936:
	case Common::kWindows950:
		return true;
	default:
		why = "no translation and no CJK code page";
		return false;
	}
}

int16 gameAdvance(const Graphics::GlyphMetrics &m, int gameNarrow, int gameWide, int scale) {
	if (m.combining)
		return 0;
	if (m.wide)
		return (int16)gameWide;
	return (int16)Graphics::latinAdvanceGamePx(Graphics::kHiResMetricsFont, gameNarrow, m.advance, scale);
}

// --- SciTextDecoder -------------------------------------------------------

int SciTextDecoder::decode(const byte *p, const byte *end, uint32 &cp, byte &flags) const {
	flags = 0;
	const byte b = p[0];

	if (b == '|' && _textCodes) {
		// CodeProcessing(): the code runs to the next '|', inclusive, or to
		// the end of the text.
		const byte *q = p + 1;
		while (q < end && *q && *q != '|')
			q++;
		if (q < end && *q == '|')
			q++;
		cp = Graphics::kControlUnit;
		flags = Graphics::kUnitControl;
		return (int)(q - p);
	}

	if (b == 0x0D) {
		cp = 0x0D;
		flags = Graphics::kUnitNewline;
		return (p + 1 < end && p[1] == 0x0A) ? 2 : 1;
	}
	if (b == 0x0A) {
		cp = 0x0A;
		flags = Graphics::kUnitNewline;
		return 1;
	}

	int bytes = 1;
	cp = decodeUtf8Char(p, bytes);
	if (p + bytes > end) {
		// A sequence cut off by the end of the range: one byte, as the
		// renderer's own walk would never see past the terminator.
		cp = b;
		bytes = 1;
	}
	if (cp == 0xFF20)
		flags = Graphics::kUnitNewline;
	return bytes;
}

// --- SciLayoutMetrics -----------------------------------------------------

int SciLayoutMetrics::addUnit(const byte *p, int bytes, uint32 cp, byte flags) {
	int a = 0;
	if (flags & Graphics::kUnitControl) {
		if (!(flags & Graphics::kUnitNewline))
			textCode(p, bytes);
	} else if (!(flags & (Graphics::kUnitNewline | Graphics::kUnitCombining))) {
		a = charWidth(cp);
	}
	_adv.push_back((int16)a);
	return a;
}

int SciLayoutMetrics::width(const Graphics::TextRun &run, uint32 from, uint32 to) {
	int w = 0;
	for (uint32 i = from; i < to; i++)
		w += unitAdvance(i);
	return w;
}

int SciLayoutMetrics::extend(const Graphics::TextRun &run, uint32 from, uint32 i, int widthSoFar) {
	return widthSoFar + unitAdvance(i);
}

// --- SciLayoutText --------------------------------------------------------

uint32 SciLayoutText::prepare(const byte *text, bool textCodes) {
	const uint32 len = strlen((const char *)text);
	const uint32 total = _bytes.size();
	if (textCodes == _textCodes && len <= total &&
		(len == 0 || !memcmp(text, &_bytes[total - len], len))) {
		// The tail of the string decoded last - if it starts on a unit.
		const uint32 off = total - len;
		uint32 lo = 0, hi = _run.size();
		while (lo < hi) {
			const uint32 mid = lo + (hi - lo) / 2;
			if (_run.byteOffset(mid) < off)
				lo = mid + 1;
			else
				hi = mid;
		}
		if (_run.byteOffset(lo) == off)
			return lo;
	}

	_textCodes = textCodes;
	_bytes.resize(len);
	if (len)
		memcpy(&_bytes[0], text, len);
	const SciTextDecoder dec(textCodes);
	_run.decode(text, len, dec);
	return 0;
}

// --- getLongestLayout() ---------------------------------------------------

namespace {

bool isGlue(byte flags) {
	return (flags & Graphics::kUnitControl) && !(flags & Graphics::kUnitNewline);
}

/** The first cluster boundary after unit i. */
uint32 nextBoundary(const Graphics::TextRun &run, uint32 i) {
	uint32 e = i + 1;
	while (e < run.size() && !Graphics::TextLayout::isClusterBoundary(run, e))
		e++;
	return e;
}

/**
 * The split of a line with no usable break: the longest prefix from
 * @p from that fits (at least one cluster), plus, for @p early, the cluster
 * that overflowed.
 */
uint32 splitEnd(const Graphics::TextRun &run, uint32 from, SciLayoutMetrics &m, int maxWidth, bool early) {
	const uint32 n = run.size();
	int w = 0;
	uint32 i = from;
	for (; i < n; i++) {
		if (run.flags(i) & Graphics::kUnitNewline)
			break;
		w += m.unitAdvance(i);
		if (w > maxWidth)
			break;
	}
	uint32 e = i;
	while (e > from && !Graphics::TextLayout::isClusterBoundary(run, e))
		e--;
	if (e == from)
		return nextBoundary(run, from);
	if (early && i < n && !(run.flags(i) & Graphics::kUnitNewline))
		e = nextBoundary(run, e);
	return e;
}

} // End of anonymous namespace

int16 getLongestLayout(const byte *text, int16 maxWidth, bool textCodes, bool early,
					   SciLayoutMetrics &m, const Graphics::BreakRules &rules,
					   SciLayoutText &layoutText, uint32 &next) {
	const uint32 from = layoutText.prepare(text, textCodes);
	const Graphics::TextRun &run = layoutText.run();
	const uint32 n = run.size();
	next = 0;
	if (from >= n)
		return 0;
	const uint32 base = run.byteOffset(from);
	// Byte offsets from the line's start, which is `text`.
#define REL(i) (run.byteOffset(i) - base)

	// Measure units in order until the line cannot need more: through the
	// first unit that overflows (spaces hang, marks and codes are zero
	// wide, so neither can), the marks and codes glued to it, and one unit
	// more for fitLine()'s look-ahead. A newline ends it.
	m.begin(from);
	int w = 0;
	bool over = false;
	for (uint32 i = from; i < n; i++) {
		const byte flags = run.flags(i);
		const int a = m.addUnit(text + REL(i), REL(i + 1) - REL(i), run.cp(i), flags);
		if (flags & Graphics::kUnitNewline)
			break;
		const bool glue = isGlue(flags);
		const bool mark = (flags & Graphics::kUnitCombining) != 0;
		if (over) {
			if (!glue && !mark)
				break;
			continue;
		}
		w += a;
		if (!(flags & Graphics::kUnitSpace) && !glue && !mark && w > maxWidth)
			over = true;
	}

	// Without the early rule, a line whose first character is already too
	// wide is empty, as it always was (the caller stops there): the codes
	// before it are all it holds.
	if (!early) {
		uint32 k = from;
		while (k < n && isGlue(run.flags(k)))
			k++;
		if (k < n && !(run.flags(k) & Graphics::kUnitNewline) && m.unitAdvance(k) > maxWidth) {
			next = REL(k);
			return (int16)next;
		}
	}

	const Graphics::LineSpan l = Graphics::TextLayout::fitLine(run, from, maxWidth, m, rules);

	// The original interpreters' first-word rule (early): while no space
	// has been passed, reaching maxWidth exactly ends the line after that
	// character, spaces not skipped. Applied where the layout stage ends
	// the line (with the spaces after it) at that same place or later, so
	// kinsoku still decides where a line of Japanese breaks. (A break after
	// spaces at the start of the line was never one.)
	if (early) {
		int fw = 0;
		for (uint32 k = from; k < n; k++) {
			const byte f = run.flags(k);
			if ((f & Graphics::kUnitNewline) || (k > from && (f & Graphics::kUnitSpace)))
				break;
			if (isGlue(f))
				continue;
			fw += m.unitAdvance(k);
			if (fw > maxWidth)
				break;
			if (fw == maxWidth) {
				const uint32 e = nextBoundary(run, k);
				if (l.next >= e || l.end == from) {
					next = REL(e);
					return (int16)next;
				}
				break;
			}
		}
	}

	uint32 count;
	if (l.forced) {
		// The newline belongs to the line.
		count = next = REL(l.next);
	} else if (l.emergency) {
		uint32 e = l.end;
		// A split word keeps the character that overflowed (early).
		if (early && l.width < maxWidth && e < n && !(run.flags(e) & Graphics::kUnitNewline))
			e = nextBoundary(run, e);
		count = next = REL(e);
	} else {
		// Broken at spaces: count up to the last of them that still fitted
		// (the old "last breaking space"); at the end of the text, all of
		// it when everything fits.
		int ws = m.width(run, from, l.end);
		uint32 c = l.end;
		uint32 s = l.end;
		for (; s < l.next; s++) {
			if (ws > maxWidth)
				break;
			c = s;
			ws += m.unitAdvance(s);
		}
		if (s == l.next && l.next >= n && ws <= maxWidth)
			c = n;
		count = REL(c);
		next = REL(l.next);
		if (!count) {
			// Only spaces fitted before the break (a line that starts with
			// spaces): split the text instead, as a space at the start of a
			// line was never a break.
			count = next = REL(splitEnd(run, from, m, maxWidth, early));
		}
	}
#undef REL
	return (int16)count;
}

} // End of namespace Sci
