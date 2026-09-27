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

#ifndef SCI_GRAPHICS_TEXTLAYOUT16_H
#define SCI_GRAPHICS_TEXTLAYOUT16_H

#include "common/array.h"
#include "common/str.h"
#include "common/str-enc.h"
#include "graphics/hires_text/glyph_source.h"
#include "graphics/hires_text/text_layout.h"
#include "sci/detection.h"

namespace Sci {

/**
 * The language-neutral pieces of SCI16 hi-res text (I18N_TEXT_DESIGN.md
 * sections 4.1-4.3), kept free of engine state (no g_sci, no ConfMan) so
 * they are tested alone. GfxCache, GfxFontUnicode and GfxText16 call them
 * with what they know about the running game.
 */

/**
 * Whether the hi-res text keys (hires_text_font, _latin*, hires_text.map)
 * apply: an SCI16 game whose text is a UTF-8 translation, or one in a legacy
 * CJK code page (949, 932, 936, 950). The language of the game is not asked.
 * On false, @p why says which half failed.
 */
bool hiresTextApplies(SciVersion v, Common::CodePage page, bool utf8Translation, Common::String &why);

/**
 * The advance of a glyph drawn on the hi-res plane, in game px (design
 * section 4.2): a wide glyph keeps the cell rule (@p gameWide, as before, so
 * Hangul and kanji keep their grid); a combining mark does not advance; any
 * other glyph advances by the face's own advance, latinAdvanceGamePx()
 * (metrics=font) at @p scale hi-res px per game px, @p gameNarrow when the
 * face cannot say. GfxFontUnicode::gameAdvance() is this.
 */
int16 gameAdvance(const Graphics::GlyphMetrics &m, int gameNarrow, int gameWide, int scale);

/**
 * The width in game px of @p cp drawn by @p src at @p scale hi-res px per
 * game px, 0 when @p src has no glyph for it.
 *
 * With @p perGlyph (a UTF-8 translation is loaded): gameAdvance() of the
 * glyph's metrics, a glyph the source keeps in two cells counting as wide
 * (so Hangul, kana, kanji and every SCVMUNI wide glyph keep the cell rule).
 * Without: the cell width halved to game px exactly as it always was -
 * (advanceWide or advanceNarrow) / scale, a combining mark included - so a
 * legacy font keeps its widths to the pixel.
 */
int16 glyphGameWidth(Graphics::UnicodeGlyphSource *src, uint32 cp, int scale, bool perGlyph);

/**
 * Where GfxFontUnicode draws a glyph on the hi-res plane (design section
 * 4.2): originX hi-res px left of the pen, and a combining mark against the
 * pen the previous base left, in hi-res px, when the mark is drawn where
 * that base left the pen in game px. reset() forgets the base: a mark at
 * the start of a string never attaches to the previous string.
 */
class CombiningAnchor {
public:
	CombiningAnchor() : _valid(false), _left(0), _top(0), _hiresX(0) {}

	void reset() { _valid = false; }

	/**
	 * The hi-res x of a glyph drawn at game (@p left, @p top) with metrics
	 * @p m (@p placed false: no metrics, drawn at left * 2); a base glyph
	 * becomes the anchor, its advance @p gameAdvance game px.
	 */
	int place(const Graphics::GlyphMetrics &m, bool placed, int16 left, int16 top, int gameAdvance);

private:
	bool _valid;
	int16 _left, _top;
	int _hiresX;
};

/**
 * The key a face chain is shared under: everything that changes the
 * chain built - the pixel size its faces are opened at, whether the .uni
 * bundle stands behind them, and the faces in order.
 */
Common::String faceChainKey(const Common::Array<Common::String> &faces, int size, bool uniBehind);

/**
 * SCI16 text as layout units: UTF-8 (decoded as Sci::decodeUtf8Char(), the
 * decoder GfxText16::readChar() and the string ops share), plus SCI's
 * escapes (design section 3.3):
 * - with @p textCodes (SCI1.1 and later), a '|' code up to and including
 *   its closing '|' (to the end of the text when unclosed, as
 *   GfxText16::CodeProcessing() reads it) is one control unit;
 * - CR LF, CR, LF and U+FF20 (SQ4 Japanese) are newline units.
 */
class SciTextDecoder : public Graphics::TextDecoder {
public:
	explicit SciTextDecoder(bool textCodes) : _textCodes(textCodes) {}
	int decode(const byte *p, const byte *end, uint32 &cp, byte &flags) const override;

private:
	bool _textCodes;
};

/**
 * GetLongest()'s measure: the width of each unit in the current font, with
 * the font changes of '|f' codes applied in order. A line's units are
 * measured once, in order, by addUnit() (getLongestLayout() does it up to
 * the first unit that cannot fit); width() and extend() then read those
 * advances. advance(cp) is charWidth(cp) in the current font.
 */
class SciLayoutMetrics : public Graphics::LayoutMetrics {
public:
	SciLayoutMetrics() : _base(0) {}
	virtual ~SciLayoutMetrics() {}

	/** Forget the advances of the previous line; the next addUnit() measures unit @p first. */
	void begin(uint32 first) { _adv.resize(0); _base = first; }
	/**
	 * Measure the next unit (its bytes at @p p, @p bytes long; flags as
	 * the TextRun has them) and return its advance: 0 for a code (applied
	 * through textCode()), a newline or a combining mark.
	 */
	int addUnit(const byte *p, int bytes, uint32 cp, byte flags);
	/** The advance addUnit() gave unit @p i (0 when it was not measured). */
	int unitAdvance(uint32 i) const {
		return (i >= _base && i - _base < _adv.size()) ? _adv[i - _base] : 0;
	}

	int advance(uint32 cp) override { return charWidth(cp); }
	int width(const Graphics::TextRun &run, uint32 from, uint32 to) override;
	int extend(const Graphics::TextRun &run, uint32 from, uint32 i, int widthSoFar) override;

protected:
	/** The width of @p cp in the current font (GfxFont::getCharWidth()). */
	virtual int charWidth(uint32 cp) = 0;
	/** A '|' code, @p bytes long from its '|': apply it (a font change). */
	virtual void textCode(const byte *code, int bytes) {}

private:
	Common::Array<int16> _adv;
	uint32 _base;
};

/**
 * The units of the string GetLongest() is walking, decoded once for all
 * its lines. The callers (Size(), Box(), ...) ask for one line after the
 * other with a pointer that moves through the same string; each call
 * checks that the text from that pointer on is the tail of what was
 * decoded, byte for byte, and otherwise decodes it afresh. Only the
 * pointed-to text is read, never what lay before it.
 */
class SciLayoutText {
public:
	SciLayoutText() : _textCodes(false) {}

	/** The unit at which @p text (NUL-terminated) starts, decoding it if needed. */
	uint32 prepare(const byte *text, bool textCodes);
	const Graphics::TextRun &run() const { return _run; }

private:
	Graphics::TextRun _run;
	Common::Array<byte> _bytes;	///< the decoded string, for the tail check
	bool _textCodes;
};

/**
 * GfxText16::GetLongest() for UTF-8 text, on the shared layout stage: the
 * byte count of the line starting at @p text that fits @p maxWidth, and in
 * @p next the byte offset where the following line starts. The line is
 * found by Graphics::TextLayout::fitLine() with @p rules (kinsoku, Thai
 * fallback, Hangul at spaces); what is returned follows the old contract:
 * - a line ended by a newline counts the newline; the end of the text
 *   counts everything, trailing spaces included;
 * - a line broken at spaces counts up to the last of those spaces that
 *   still fitted (the old "last breaking space"), and @p next skips them;
 * - a line with no break opportunity is split at a cluster boundary and
 *   @p next is its end; a first character wider than the line is a line
 *   of nothing (count 0), as it always was.
 * @p early is GameFeatures::useEarlyGetLongestTextCalculations(): as the
 * original interpreters did, a first word that reaches maxWidth exactly
 * ends the line there, and a split word keeps the character that overflowed.
 * Units are measured only up to the first one that cannot fit.
 */
int16 getLongestLayout(const byte *text, int16 maxWidth, bool textCodes, bool early,
					   SciLayoutMetrics &m, const Graphics::BreakRules &rules,
					   SciLayoutText &layoutText, uint32 &next);

} // End of namespace Sci

#endif // SCI_GRAPHICS_TEXTLAYOUT16_H
