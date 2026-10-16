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

#ifndef GRAPHICS_HIRES_TEXT_GLYPH_SOURCE_JAMO_H
#define GRAPHICS_HIRES_TEXT_GLYPH_SOURCE_JAMO_H

#include "common/array.h"
#include "common/file-cache-stats.h"
#include "common/hashmap.h"
#include "common/str.h"
#include "common/types.h"
#include "graphics/hires_text/glyph_source.h"

namespace Graphics {

class HiResBitmapFont;
class SvfnGlyphSource;
struct JamoSetTable;

/**
 * A jamo-set SVFN (flags bit 1, HiResBitmapFont::jamoSets()) as a
 * UnicodeGlyphSource: the glyphs of the file are positional pieces, and a
 * Hangul syllable is composed from two or three of them, as the DOS Hangul
 * systems did, by taking the strongest coverage of the pieces at each pixel
 * (the OR of a 1bpp stencil; the maximum at 2bpp and 8bpp, in the font's own
 * packing).
 *
 * U+AC00..U+D7A3 are composed from the table's pieces; U+3131..U+3163 with a
 * compatPiece are that piece alone; everything else, and every code point
 * when the font has no valid table, is the file's own glyph, through an
 * SvfnGlyphSource of the same font (which also reads the pieces, so a
 * streamed font's pieces come from the file by its blocks).
 *
 * A composed syllable is cells() 2 and drawn in the rows SvfnGlyphSource
 * gives a glyph of the font (stride cellWidth()*2 pixels, the piece's cell
 * first, the rest blank). Composed rows are kept like SvfnGlyphSource's
 * (least recently used; with the pieces' rows HIRES_SVF_CACHE_KB in all); a pointer from row() holds
 * until another code point is first asked for.
 */
class JamoGlyphSource : public UnicodeGlyphSource {
public:
	/** Takes ownership of @p font when dispose is YES. @p font must be loaded. */
	JamoGlyphSource(HiResBitmapFont *font, DisposeAfterUse::Flag dispose);
	~JamoGlyphSource() override;

	/**
	 * Whether @p font can be drawn as jamo sets: it has a valid table and
	 * every piece the table names has a glyph. False says why in @p why.
	 */
	static bool usable(const HiResBitmapFont &font, Common::String &why);

	/// The file the counters name (the inner source's).
	void setName(const Common::String &name);
	/// Syllables composed so far, cache hits not counted (for tests).
	uint32 composed() const { return _composed; }
	/// Syllables whose rows are kept.
	uint32 cacheEntries() const { return _maxEntries; }
	/// Pieces whose rows the inner source keeps.
	uint32 pieceCacheEntries() const;
	/**
	 * Keep @p bytes worth of rows in all, half for composed syllables and half for pieces, and at least
	 * @p minEntries of each. The default is HIRES_SVF_CACHE_KB, what one SvfnGlyphSource takes.
	 */
	void setCacheBytes(uint32 bytes, uint32 minEntries = 32);

	void prefetch(Common::Array<uint32> &cps) override;

	/// A preload list: the syllables are expanded to the pieces the file holds,
	/// and SvfnGlyphSource::preload() takes those (and the other characters
	/// as they are). The composed syllables keep their own cache.
	uint32 preload(const Common::Array<uint32> &cps) override;

	byte cellWidth() const override;
	byte cellHeight() const override;
	byte advanceNarrow() const override;
	byte advanceWide() const override;
	int bitsPerPixel() const override;
	int cells(uint32 cp) override;
	/** A composed syllable: its cell rows, null (blank) outside the cell; any other glyph as the font has it. */
	const byte *row(uint32 cp, int y) override;
	/** The font's dy range (its Latin may be moved); a composed syllable is at dy 0. */
	int inkRowsAbove() const override;
	int inkRowsBelow() const override;
	int advance(uint32 cp) override;
	bool metrics(uint32 cp, GlyphMetrics &m) override;
	uint32 glyphCount() const override;
	int baselineRow() const override;

private:
	struct Entry {
		uint32 cp = 0;
		uint32 lastUse = 0;
		Common::Array<byte> rows;	///< cellHeight() rows of _rowBytes
	};

	/// The piece code points of @p cp when this source composes it: 2 or 3 (blanks left out, so maybe fewer), else -1.
	int piecesOf(uint32 cp, uint32 out[3]) const;
	Entry &ensure(uint32 cp);
	void compose(Entry &entry, const uint32 *pieces, int count);

	SvfnGlyphSource *_inner;
	const HiResBitmapFont *_font;
	const JamoSetTable *_table;
	int _bpp;
	uint32 _rowBytes;
	uint32 _cellHeight;
	uint32 _maxEntries;
	uint32 _clock;
	uint32 _composed;
	Common::Array<Entry *> _entries;
	Common::HashMap<uint32, Entry *> _byCp;
};

} // End of namespace Graphics

#endif
