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


#ifndef AGS_SHARED_FONT_AGS_TEXT_LAYOUT_H
#define AGS_SHARED_FONT_AGS_TEXT_LAYOUT_H

#include "common/array.h"
#include "graphics/hires_text/text_layout.h"

namespace AGS3 {

/**
 * Whether split_lines() breaks a text through the shared layout stage
 * (graphics/hires_text/text_layout.h) instead of its own loop
 * (split_lines_bytes()): only for text the new i18n path owns - an EUC-KR
 * translation (U_EUCKR), a UTF-8 text while a translation is loaded, or any
 * non-ASCII format while hires_text.map names fonts (mapActive). U_ASCII
 * and a native UTF-8 game with neither keep AGS's own breaking, byte for
 * byte.
 */
bool split_lines_uses_layout(int uformat, bool translationLoaded, bool mapActive);

/**
 * AGS text (after unescape_script_string(), so '[' is already '\n') as
 * layout units: U_UTF8 as UTF-8, U_EUCKR through the EUC-KR reader (a
 * KS X 1001 Hangul pair is one unit), anything else one byte per unit.
 * '\n' is a newline unit.
 */
class AgsTextDecoder : public Graphics::TextDecoder {
public:
	explicit AgsTextDecoder(int uformat) : _uformat(uformat) {}
	int decode(const byte *p, const byte *end, uint32 &cp, byte &flags) const override;

private:
	int _uformat;
};

/**
 * AGS measures whole strings (outline, kerning: widths are not additive),
 * so width() measures the bytes of [from, to) through measureBytes() and
 * extend() re-measures instead of adding (design section 3.2).
 */
class AgsLayoutMetrics : public Graphics::LayoutMetrics {
public:
	AgsLayoutMetrics() : _text(nullptr) {}
	/** The text the runs are decoded from. */
	void setText(const char *text) { _text = text; }

	/** Not used: width() and extend() measure bytes. */
	int advance(uint32 cp) override { return 0; }
	int width(const Graphics::TextRun &run, uint32 from, uint32 to) override;
	int extend(const Graphics::TextRun &run, uint32 from, uint32 i, int widthSoFar) override;

protected:
	/** Width of len bytes at s, as split_lines() measures a line. */
	virtual int measureBytes(const char *s, uint32 len) = 0;

private:
	const char *_text;
};

/** One line of a laid-out text: bytes [start, end). */
struct AgsLineSpan {
	uint32 start, end;
};

/**
 * The lines of text (len bytes, already unescaped) at maxWidth.
 * A line ended by a newline keeps what precedes the newline and the last
 * line runs to the end of the text, as split_lines() cuts them; a wrapped
 * line ends at LineSpan::byteEnd (its trailing spaces dropped). Returns
 * false, with out empty, when not even one character fits (split_lines()
 * then shows no line, as before).
 */
bool ags_layout_lines(const char *text, uint32 len, int uformat, int maxWidth, AgsLayoutMetrics &m,
					  const Graphics::BreakRules &rules, Common::Array<AgsLineSpan> &out);

/**
 * split_lines()'s own loop, moved here unchanged (only its helpers became
 * Ops calls) so a unit test runs the very code the engine runs: break at the
 * last space, else "display as much as possible" at the previous character.
 * It still serves U_ASCII, and every text split_lines_uses_layout() refuses.
 *
 * theline is the unescaped text (it is written to and restored). Ops:
 *   int nextChar(char **s); int charAt(const char *s); int putChar(char *s, int c)
 *   (Allegro's ugetx/ugetc/usetc) and int width(const char *s) (the
 *   outlined width of a line). Lines: Add(const char *), Count(), Reset(),
 *   operator[](i).Append(const char *).
 */
template<class Lines, class Ops>
size_t split_lines_bytes(char *theline, Lines &lines, int wii, size_t max_lines, Ops &ops) {
	char *scan_ptr = theline;
	char *prev_ptr = theline;
	char *last_whitespace = nullptr;
	while (1) {
		char *split_at = nullptr;

		if (*scan_ptr == 0) {
			// end of the text, add the last line if necessary
			if (scan_ptr > theline) {
				lines.Add(theline);
			}
			break;
		}

		if (*scan_ptr == ' ')
			last_whitespace = scan_ptr;

		// force end of line with the \n character
		if (*scan_ptr == '\n') {
			split_at = scan_ptr;
			// otherwise, see if we are too wide
		} else {
			// temporarily terminate the line in the *next* char and test its width
			char *next_ptr = scan_ptr;
			ops.nextChar(&next_ptr);
			const int next_chwas = ops.charAt(next_ptr);
			*next_ptr = 0;

			if (ops.width(theline) > wii) {
				// line is too wide, order the split
				if (last_whitespace)
					// revert to the last whitespace
					split_at = last_whitespace;
				else
					// single very wide word, display as much as possible
					split_at = prev_ptr;
			}

			// restore the character that was there before
			ops.putChar(next_ptr, next_chwas);
		}

		if (split_at == nullptr) {
			prev_ptr = scan_ptr;
			ops.nextChar(&scan_ptr);
		} else {
			// check if even one char cannot fit...
			if (split_at == theline && !((*theline == ' ') || (*theline == '\n'))) {
				// cannot split with current width restriction
				lines.Reset();
				break;
			}
			// add this line; do the temporary terminator trick again
			const int next_chwas = ops.charAt(split_at);
			*split_at = 0;
			lines.Add(theline);
			ops.putChar(split_at, next_chwas);
			// check if too many lines
			if (lines.Count() >= max_lines) {
				lines[lines.Count() - 1].Append("...");
				break;
			}
			// the next line starts from the split point
			theline = split_at;
			// skip the space or new line that caused the line break
			if ((*theline == ' ') || (*theline == '\n'))
				theline++;
			scan_ptr = theline;
			prev_ptr = theline;
			last_whitespace = nullptr;
		}
	}
	return lines.Count();
}

} // namespace AGS3

#endif
