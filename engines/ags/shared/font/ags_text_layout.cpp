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


// split_lines() through the shared layout stage, free of the engine's
// globals so the unit tests link it alone (test/engines/ags/split_lines_layout.h).

#include "ags/shared/font/ags_text_layout.h"
#include "ags/lib/allegro/unicode_euckr.h"

namespace AGS3 {

// The text format ids of ags/lib/allegro/unicode.h (AL_ID), repeated here
// because that header cannot be included next to <locale.h> (its enum
// LC_CTYPE); split_lines_layout.h checks them against the real ones.
#define AGS_LAYOUT_ID(a, b, c, d) ((uint32)((d) | ((c) << 8) | ((b) << 16) | ((a) << 24)))
static const uint32 kFormatAscii = AGS_LAYOUT_ID('A', 'S', 'C', '8');
static const uint32 kFormatUtf8 = AGS_LAYOUT_ID('U', 'T', 'F', '8');
static const uint32 kFormatEucKr = AGS_LAYOUT_ID('E', 'U', 'K', 'R');
#undef AGS_LAYOUT_ID

bool split_lines_uses_layout(int uformat) {
	return (uint32)uformat != kFormatAscii;
}

int AgsTextDecoder::decode(const byte *p, const byte *end, uint32 &cp, byte &flags) const {
	if ((uint32)_uformat == kFormatUtf8) {
		static const Graphics::Utf8TextDecoder utf8;
		return utf8.decode(p, end, cp, flags);
	}
	flags = 0;
	if ((uint32)_uformat == kFormatEucKr && end - p >= 2) {
		// euckr_getx() reads a pair only when both bytes are there.
		char pair[3] = { (char)p[0], (char)p[1], 0 };
		char *s = pair;
		cp = (uint32)euckr_getx(&s);
		return (int)(s - pair);
	}
	cp = *p;
	if (cp == '\n')
		flags = Graphics::kUnitNewline;
	return 1;
}

int AgsLayoutMetrics::width(const Graphics::TextRun &run, uint32 from, uint32 to) {
	// A newline unit is never inside a measured range (fitLine() stops at
	// it), but a range that ends with one is measured without it.
	while (to > from && (run.flags(to - 1) & Graphics::kUnitNewline))
		to--;
	if (to <= from || !_text)
		return 0;
	const uint32 start = run.byteOffset(from);
	return measureBytes(_text + start, run.byteOffset(to) - start);
}

int AgsLayoutMetrics::extend(const Graphics::TextRun &run, uint32 from, uint32 i, int widthSoFar) {
	(void)widthSoFar;
	return width(run, from, i + 1);
}

bool ags_layout_lines(const char *text, uint32 len, int uformat, int maxWidth, AgsLayoutMetrics &m,
					  const Graphics::BreakRules &rules, Common::Array<AgsLineSpan> &out) {
	out.resize(0);
	const AgsTextDecoder dec(uformat);
	Graphics::TextRun run;
	run.decode((const byte *)text, len, dec);
	m.setText(text);
	Common::Array<Graphics::LineSpan> spans;
	Graphics::TextLayout::breakLines(run, maxWidth, m, rules, spans);
	for (uint i = 0; i < spans.size(); i++) {
		const Graphics::LineSpan &l = spans[i];
		// Only a first cluster that does not fit even alone is wider than
		// the box: split_lines() then shows nothing, as it always did.
		if (l.emergency && l.width > maxWidth) {
			out.resize(0);
			return false;
		}
		AgsLineSpan s;
		s.start = l.byteStart;
		if (l.forced)
			s.end = run.byteOffset(l.next - 1);   // up to the newline, spaces kept
		else if (l.next >= run.size())
			s.end = len;                          // the last line, spaces kept
		else
			s.end = l.byteEnd;
		out.push_back(s);
	}
	return true;
}

} // namespace AGS3
