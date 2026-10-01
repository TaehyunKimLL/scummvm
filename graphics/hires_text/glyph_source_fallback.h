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


#ifndef GRAPHICS_HIRES_TEXT_GLYPH_SOURCE_FALLBACK_H
#define GRAPHICS_HIRES_TEXT_GLYPH_SOURCE_FALLBACK_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/str.h"
#include "common/types.h"
#include "graphics/hires_text/glyph_source.h"

namespace Graphics {

/**
 * A face fallback chain (HIRESTXT.MAP "face=ko, ja, th",
 * I18N_TEXT_DESIGN.md section 4.5): each lookup asks the sources in order,
 * and the first whose cells(cp) > 0 answers cells, row, advance and metrics
 * for that code point. Which source answered is remembered per code point.
 *
 * Geometry (cell size, advances, bits per pixel) is sources[0]'s. Rows are
 * read with that stride and depth, so a later source whose cell width,
 * height or bits per pixel differs cannot answer: it is left out of the
 * lookups with one warning (open every face of a chain at one pixel size).
 *
 * Owns the sources (every one, including a left-out one) when constructed
 * with DisposeAfterUse::YES.
 */
class FallbackGlyphSource : public UnicodeGlyphSource {
public:
	/** Asks each source in order; the first with cells(cp) > 0 answers. Cell size = sources[0]'s. */
	FallbackGlyphSource(const Common::Array<UnicodeGlyphSource *> &sources, DisposeAfterUse::Flag dispose);
	~FallbackGlyphSource() override;

	byte cellWidth() const override;
	byte cellHeight() const override;
	byte advanceNarrow() const override;
	byte advanceWide() const override;
	int bitsPerPixel() const override;
	int cells(uint32 cp) override;
	const byte *row(uint32 cp, int y) override;
	int advance(uint32 cp) override;
	bool metrics(uint32 cp, GlyphMetrics &m) override;
	/** The sum over every source. */
	uint32 glyphCount() const override;

private:
	/** The source that answers cp, or nullptr when none has it. */
	UnicodeGlyphSource *answering(uint32 cp);

	Common::Array<UnicodeGlyphSource *> _sources;	///< every source given, for disposal and glyphCount()
	Common::Array<UnicodeGlyphSource *> _lookup;	///< the ones whose geometry matches sources[0]
	DisposeAfterUse::Flag _dispose;
	Common::HashMap<uint32, int> _answer;			///< cp -> index into _lookup, -1 for none
};

/**
 * A source presented in a larger cell and at 8 bits per pixel, so that a
 * bitmap bundle (SCVMUNI at 1 or 2 bpp, 16x16) can stand behind TrueType
 * faces in a FallbackGlyphSource, whose sources must share one cell and one
 * depth. Each row is expanded into a scratch row: the source's pixels at
 * the left, its rows at the top, coverage 0 around them. Placement
 * (cells, advances, metrics) is the source's own; only the row layout
 * changes. A source whose cell is larger than the chain's cannot be
 * presented without cutting its glyphs and is refused.
 */
class NormalizedGlyphSource : public UnicodeGlyphSource {
public:
	/**
	 * @p src in a @p cellWidth x @p cellHeight cell at 8 bpp, or nullptr
	 * (and @p error) when its cell is larger or its depth is not 1, 2 or
	 * 8. Owns @p src with DisposeAfterUse::YES, and deletes it on failure.
	 */
	static NormalizedGlyphSource *create(UnicodeGlyphSource *src, byte cellWidth, byte cellHeight,
										 DisposeAfterUse::Flag dispose, Common::String &error);
	/** As above, with the source's rows starting at row @p topRow of the
	 *  cell instead of row 0 (behind a padded TtfGlyphSource, whose cell
	 *  proper starts at its TtfGlyphSource::rowPad()). */
	static NormalizedGlyphSource *create(UnicodeGlyphSource *src, byte cellWidth, byte cellHeight, int topRow,
										 DisposeAfterUse::Flag dispose, Common::String &error);
	~NormalizedGlyphSource() override;

	byte cellWidth() const override { return _cellWidth; }
	byte cellHeight() const override { return _cellHeight; }
	byte advanceNarrow() const override { return _src->advanceNarrow(); }
	byte advanceWide() const override { return _src->advanceWide(); }
	int bitsPerPixel() const override { return 8; }
	int cells(uint32 cp) override { return _src->cells(cp); }
	/** Valid until the next call. */
	const byte *row(uint32 cp, int y) override;
	int advance(uint32 cp) override { return _src->advance(cp); }
	bool metrics(uint32 cp, GlyphMetrics &m) override { return _src->metrics(cp, m); }
	uint32 glyphCount() const override { return _src->glyphCount(); }

private:
	NormalizedGlyphSource(UnicodeGlyphSource *src, byte cellWidth, byte cellHeight, DisposeAfterUse::Flag dispose);

	UnicodeGlyphSource *_src;
	byte _cellWidth, _cellHeight;
	DisposeAfterUse::Flag _dispose;
	Common::Array<byte> _scratch;	///< one row: cellWidth * 2 pixels at 8 bpp
	int _topRow = 0;				///< the cell row the source's row 0 is presented at
};

} // End of namespace Graphics

#endif
