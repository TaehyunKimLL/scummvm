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
#include "common/file-cache-stats.h"
#include "common/hashmap.h"
#include "common/types.h"
#include "graphics/hires_text/glyph_source.h"

/// KB of glyph rows an SVF face keeps (SvfnGlyphSource).
#ifndef HIRES_SVF_CACHE_KB
#define HIRES_SVF_CACHE_KB 32
#endif

namespace Graphics {

class HiResBitmapFont;

/**
 * An SVFN bitmap font (HiResBitmapFont, 1, 2 or 8 bpp) exposed as a
 * UnicodeGlyphSource, so an engine can draw from a baked font through the
 * same interface as a live TrueType face (TtfGlyphSource).
 *
 * The geometry follows TtfGlyphSource at the same size: cellWidth() is the
 * font's cell width, a glyph is 1 or 2 cells - by its metrics advance
 * (more than half the cell: 2) when the font has a metrics table, else by
 * East Asian Width (Unicode::isWide()) - advanceNarrow()/advanceWide() are half and
 * all of the cell, and rows are cellWidth()*2 pixels at bitsPerPixel(), the
 * layout TextCompose::expandGlyphRow() reads. SVFN stores one cell per
 * glyph, so each glyph's rows are copied, on first use, into that stride
 * with the second cell blank. The copies of the code points drawn last
 * (row()) are kept, HIRES_SVF_CACHE_KB of them (a code point the font lacks
 * counts as one); cells() answers from the font's tables and keeps nothing.
 * A pointer from row() holds until another code point is first asked for.
 * With a streamed font (HiResBitmapFont::loadStreamed()) that is what stands
 * between drawing and the file; its counters are registered with
 * Common::FileCacheRegistry as kind "svf". A glyph whose read fails is drawn
 * blank, at the width cells() gives (the font does not read that block
 * again, so a glyph drawn every frame cannot read the disk every frame).
 */
class SvfnGlyphSource : public UnicodeGlyphSource {
public:
	/** Takes ownership of @p font when dispose is YES. Same row layout as TtfGlyphSource
	 *  (stride cellWidth()*2 px at bitsPerPixel()), so TextCompose::expandGlyphRow reads it.
	 *  @p font must be loaded. */
	SvfnGlyphSource(HiResBitmapFont *font, DisposeAfterUse::Flag dispose);
	~SvfnGlyphSource() override;

	/// Glyphs copied out of the font so far (for tests).
	uint32 glyphReads() const { return _glyphReads; }
	/// Code points whose rows are kept: HIRES_SVF_CACHE_KB worth, or @p bytes worth (at least 32).
	uint32 cacheEntries() const { return _maxEntries; }
	void setCacheBytes(uint32 bytes);
	/// The file the counters name.
	void setName(const Common::String &name) { _stats.name = name; }
	const Common::FileCacheStats &stats() const { return _stats; }

	/**
	 * Read the glyphs of @p cps not kept yet, in the font's order, so that
	 * glyphs near each other in the file come in one read (as many as the
	 * cache holds); done before a line is laid out. The code points this
	 * font has leave @p cps.
	 */
	void prefetch(Common::Array<uint32> &cps) override;

	byte cellWidth() const override { return _cellWidth; }
	byte cellHeight() const override { return _cellHeight; }
	byte advanceNarrow() const override { return _cellWidth / 2; }
	byte advanceWide() const override { return _cellWidth; }
	int bitsPerPixel() const override { return _bitsPerPixel; }
	int cells(uint32 cp) override;
	const byte *row(uint32 cp, int y) override;
	int advance(uint32 cp) override;       // the SVFN per-glyph advance, 0 without a metrics table
	int bearingX(uint32 cp) const;         // SVFN bearingX, for proportional placement
	/** The default metrics with originX 0 (the stored row starts at the
	 *  pen), plus the SVFN fields as data: bearingX (signed, as the format
	 *  specifies), bearingY, width, height. */
	bool metrics(uint32 cp, GlyphMetrics &m) override;
	uint32 glyphCount() const override;
	/** The SVFN ascent, held to the cell; -1 for a font that records none (0). */
	int baselineRow() const override;

private:
	struct Entry {
		uint32 cp = 0;
		uint32 lastUse = 0;
		byte cells = 0;               ///< 0: the font has no glyph for this code point
		Common::Array<byte> rows;     ///< cellHeight() rows of _rowBytes, or none
	};

	Entry &ensure(uint32 cp);
	/// How many cells the glyph @p index (of @p cp) takes, from the tables.
	byte cellsFor(uint32 cp, int index) const;
	Entry *find(uint32 cp);
	void updateStats();
	/// The entry for a code point not cached: a new one, or the least recently used.
	Entry &takeEntry(uint32 cp);

	HiResBitmapFont *_font;
	DisposeAfterUse::Flag _dispose;
	byte _cellWidth;
	byte _cellHeight;
	int _bitsPerPixel;
	uint32 _rowBytes;                 ///< bytes per row at the two-cell stride

	/// At most _maxEntries entries, never moved once made (row() hands out pointers into them).
	Common::Array<Entry *> _entries;
	Common::HashMap<uint32, Entry *> _byCp;
	uint32 _maxEntries;
	uint32 _clock;
	uint32 _glyphReads;
	uint32 _lastCp;	///< the last code point looked up, counted once however often in a row
	/// A glyph the font has whose read failed: blank rows at its width, for
	/// the use that asked (_failedCp); the next use reads again.
	Entry _failed;
	uint32 _failedCp;
	bool _readFailWarned;
	Common::FileCacheStats _stats;
	bool _registered;
};

} // End of namespace Graphics

#endif
