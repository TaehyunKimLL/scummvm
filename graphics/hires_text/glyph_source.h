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

#ifndef GRAPHICS_HIRES_TEXT_GLYPH_SOURCE_H
#define GRAPHICS_HIRES_TEXT_GLYPH_SOURCE_H

#include "common/scummsys.h"
#include "graphics/hires_text/unicode_props.h"

namespace Graphics {

/**
 * Metrics of a single glyph, in the pixels the font or face was drawn at
 * (source px).
 *
 * HiResBitmapFont::glyphMetrics() fills advance, bearingX, bearingY, width
 * and height from an SVFN metrics table; a font whose glyphs all share one
 * advance still fills them in, so a caller never has to ask whether a font
 * is proportional before laying text out.
 *
 * UnicodeGlyphSource::metrics() fills advance, originX, combining and wide
 * (I18N_TEXT_DESIGN.md section 4.2): the pen is placed at column originX of
 * row(cp, y), a combining mark is drawn against the previous base's anchor
 * and does not advance.
 */
struct GlyphMetrics {
	GlyphMetrics() : advance(0), bearingX(0), bearingY(0), width(0), height(0),
		originX(0), combining(false), wide(false) {}

	int16 advance;   ///< how far the pen moves for this glyph; 0 for a combining mark
	int16 bearingX;  ///< pen position to the left edge of the ink (SVFN)
	int16 bearingY;  ///< baseline to the top edge of the cell (SVFN)
	uint16 width;    ///< width of the ink, 0 for a blank glyph (SVFN)
	uint16 height;   ///< height of the cell the glyph is stored in (SVFN)

	int16 originX;   ///< column of the pen origin inside row(cp, y); 0 unless ink lies left of the origin
	bool combining;  ///< Unicode::isCombining(cp)
	bool wide;       ///< Unicode::isWide(cp)
};

/**
 * Where GfxFontUnicode gets glyphs: a packed SCVMUNI file or a live TrueType
 * face rasterised on demand.
 *
 * Rows are stride cellWidth()*2 pixels at bitsPerPixel() (1, 2 or 8), the
 * layout TextCompose::expandGlyphRow() reads. Lookups are non-const because a
 * source may rasterise and cache on first use.
 */
class UnicodeGlyphSource {
public:
	virtual ~UnicodeGlyphSource() {}

	virtual byte cellWidth() const = 0;
	virtual byte cellHeight() const = 0;
	virtual byte advanceNarrow() const = 0;
	virtual byte advanceWide() const = 0;
	virtual int bitsPerPixel() const = 0;

	/** 1 or 2 cells, or 0 when the source has no glyph for cp. */
	virtual int cells(uint32 cp) = 0;

	/** Packed row y of cp's glyph; only valid when cells(cp) > 0. */
	virtual const byte *row(uint32 cp, int y) = 0;

	/**
	 * The face's own advance for cp, in hi-res (source) pixels, or 0 when
	 * unknown - no glyph, or a source that only knows cells (SCVMUNI).
	 * Read by hires_text_latin=proportional with metrics=font; the glyph's
	 * origin is column metrics().originX of its row (0 for every glyph whose
	 * ink starts at or right of its origin), so advancing by this places the
	 * next glyph as the face itself would.
	 */
	virtual int advance(uint32 cp) { return 0; }

	/**
	 * Per-glyph placement of cp: false when the source has no glyph for it.
	 * The default (for a source that knows only cells): combining and wide
	 * from Unicode::; advance 0 for a combining mark, else advance(cp), or
	 * cells(cp) * advanceNarrow() when the source reports no advance;
	 * originX 0. The SVFN-only fields are left at 0.
	 */
	virtual bool metrics(uint32 cp, GlyphMetrics &m) {
		const int c = cells(cp);
		if (c <= 0)
			return false;
		m = GlyphMetrics();
		m.combining = Unicode::isCombining(cp);
		m.wide = Unicode::isWide(cp);
		if (!m.combining) {
			const int own = advance(cp);
			m.advance = (int16)(own ? own : c * advanceNarrow());
		}
		return true;
	}

	/** For logs: glyphs in the file, or glyphs rasterised so far. */
	virtual uint32 glyphCount() const = 0;
};

} // End of namespace Graphics

#endif
