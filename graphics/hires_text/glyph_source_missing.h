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

#ifndef GRAPHICS_HIRES_TEXT_GLYPH_SOURCE_MISSING_H
#define GRAPHICS_HIRES_TEXT_GLYPH_SOURCE_MISSING_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/types.h"
#include "graphics/hires_text/glyph_source.h"

namespace Graphics {

/**
 * The map's missing=: a source that answers for every code
 * point. What @p inner has is inner's; anything else is drawn as a box in
 * a slot of its Unicode width (East Asian W/F: 2 cells, else 1) - inner's
 * glyph for the box code point when inner sizes that glyph to the same
 * cells, else a 1 px outline drawn in the slot at inner's depth. With no
 * box glyph in inner, nothing is substituted (cells 0, as inner). Each
 * substituted code point is logged once.
 *
 * Only ask this for a character every other font has declined: it claims
 * them all, so standing in a fallback order before another font would hide
 * that font's glyphs.
 */
class MissingGlyphSource : public UnicodeGlyphSource {
public:
	MissingGlyphSource(UnicodeGlyphSource *inner, uint32 boxCp, DisposeAfterUse::Flag dispose);
	~MissingGlyphSource() override;

	byte cellWidth() const override { return _inner->cellWidth(); }
	byte cellHeight() const override { return _inner->cellHeight(); }
	byte advanceNarrow() const override { return _inner->advanceNarrow(); }
	byte advanceWide() const override { return _inner->advanceWide(); }
	int bitsPerPixel() const override { return _inner->bitsPerPixel(); }
	int cells(uint32 cp) override;
	const byte *row(uint32 cp, int y) override;
	int advance(uint32 cp) override;
	bool metrics(uint32 cp, GlyphMetrics &m) override;
	uint32 glyphCount() const override { return _inner->glyphCount(); }

	/** The wrapped source: what the fonts themselves have. */
	UnicodeGlyphSource *inner() const { return _inner; }
	uint32 boxCodePoint() const { return _boxCp; }
	/** Distinct code points drawn as the box so far. */
	uint32 substitutedCount() const { return _substituted.size(); }

private:
	/** 0 when inner has cp; else the box's cells (0 when there is no box). */
	int boxCells(uint32 cp);

	UnicodeGlyphSource *_inner;
	uint32 _boxCp;
	DisposeAfterUse::Flag _dispose;
	Common::HashMap<uint32, bool> _substituted;	///< code points logged
	Common::Array<byte> _drawnBox[2];			///< the drawn 1- and 2-cell outlines, every row, built on first use
};

} // End of namespace Graphics

#endif
