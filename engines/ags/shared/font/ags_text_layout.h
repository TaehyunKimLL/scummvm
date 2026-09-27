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
 * (graphics/hires_text/text_layout.h): every text format but U_ASCII. The
 * U_ASCII path is AGS's own, byte for byte (no per-frame cost for ASCII
 * games).
 */
bool split_lines_uses_layout(int uformat);

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

} // namespace AGS3

#endif
