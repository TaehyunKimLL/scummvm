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

namespace Graphics {

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
	 * origin is column 0 of its row, so advancing by this places the next
	 * glyph as the face itself would.
	 */
	virtual int advance(uint32 cp) { return 0; }

	/** For logs: glyphs in the file, or glyphs rasterised so far. */
	virtual uint32 glyphCount() const = 0;
};

} // End of namespace Graphics

#endif
