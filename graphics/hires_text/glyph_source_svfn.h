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

#ifndef GRAPHICS_HIRES_TEXT_GLYPH_SOURCE_SVFN_H
#define GRAPHICS_HIRES_TEXT_GLYPH_SOURCE_SVFN_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/types.h"
#include "graphics/hires_text/glyph_source.h"

namespace Graphics {

class HiResBitmapFont;

/**
 * An SVFN bitmap font (HiResBitmapFont, 1bpp or 8bpp) exposed as a
 * UnicodeGlyphSource, so an engine can draw from a baked font through the
 * same interface as a live TrueType face (TtfGlyphSource).
 *
 * The geometry follows TtfGlyphSource at the same size: cellWidth() is the
 * font's cell width, a glyph is 1 or 2 cells by East Asian Width
 * (TtfGlyphSource::isWide()), advanceNarrow()/advanceWide() are half and
 * all of the cell, and rows are cellWidth()*2 pixels at bitsPerPixel(), the
 * layout TextCompose::expandGlyphRow() reads. SVFN stores one cell per
 * glyph, so each glyph's rows are copied once, on first use, into that
 * stride with the second cell blank; the copy is cached per code point.
 */
class SvfnGlyphSource : public UnicodeGlyphSource {
public:
	/** Takes ownership of @p font when dispose is YES. Same row layout as TtfGlyphSource
	 *  (stride cellWidth()*2 px at bitsPerPixel()), so TextCompose::expandGlyphRow reads it.
	 *  @p font must be loaded. */
	SvfnGlyphSource(HiResBitmapFont *font, DisposeAfterUse::Flag dispose);
	~SvfnGlyphSource() override;

	byte cellWidth() const override { return _cellWidth; }
	byte cellHeight() const override { return _cellHeight; }
	byte advanceNarrow() const override { return _cellWidth / 2; }
	byte advanceWide() const override { return _cellWidth; }
	int bitsPerPixel() const override { return _bitsPerPixel; }
	int cells(uint32 cp) override;
	const byte *row(uint32 cp, int y) override;
	int advance(uint32 cp) override;       // the SVFN per-glyph advance, 0 without a metrics table
	int bearingX(uint32 cp) const;         // SVFN bearingX, for proportional placement
	uint32 glyphCount() const override;

private:
	struct Entry {
		byte cells = 0;               ///< 0: the font has no glyph for this code point
		Common::Array<byte> rows;     ///< cellHeight() rows of _rowBytes
	};

	Entry &ensure(uint32 cp);

	HiResBitmapFont *_font;
	DisposeAfterUse::Flag _dispose;
	byte _cellWidth;
	byte _cellHeight;
	int _bitsPerPixel;
	uint32 _rowBytes;                 ///< bytes per row at the two-cell stride

	Common::HashMap<uint32, Entry> _cache;
};

} // End of namespace Graphics

#endif
