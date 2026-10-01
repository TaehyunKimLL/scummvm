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

#ifndef GRAPHICS_HIRES_TEXT_BITMAP_FONT_H
#define GRAPHICS_HIRES_TEXT_BITMAP_FONT_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/str-enc.h"
#include "common/types.h"
#include "graphics/hires_text/glyph_source.h"

namespace Common {
class SeekableReadStream;
}

/// Bytes a streamed SVF reads at a time: the glyphs around the one asked
/// for come in with it (HiResBitmapFont::loadStreamed()).
#ifndef HIRES_SVF_READ_BLOCK
#define HIRES_SVF_READ_BLOCK 4096
#endif

/// A font with no more glyph data than this is read whole even when it is
/// asked to stream (HiResBitmapFont::loadStreamed()): kept open, it would
/// cost about as much as it saves, and a file handle.
#ifndef HIRES_SVF_STREAM_MIN
#define HIRES_SVF_STREAM_MIN (64 * 1024)
#endif

namespace Graphics {

// GlyphMetrics (advance, bearingX, bearingY, width, height, and the
// glyph-source fields originX, combining, wide) is declared in
// glyph_source.h, shared with UnicodeGlyphSource::metrics().

/**
 * A bitmap font holding a 1bpp stencil, or 2bpp (four levels) or 8bpp
 * coverage per pixel.
 *
 * The coverage forms are what make anti-aliased text possible without a
 * rasteriser in the build: the shapes are baked once, by a tool that may use
 * FreeType, and at run time only need blending. That makes it the primary format rather
 * than a fallback for builds that lack FreeType.
 *
 * Lookup is by Unicode code point. Files that predate that - everything
 * shipped so far - say in their header which code page their glyphs were
 * ordered by, and this class converts code points through it, so old font
 * files keep working unchanged.
 */
class HiResBitmapFont {
public:
	HiResBitmapFont();
	~HiResBitmapFont();

	/**
	 * Read a font.
	 *
	 * The whole file is copied into memory, so the stream is not needed
	 * afterwards. Every offset in the header is checked against the data
	 * actually present: a font file ships with a translation and cannot be
	 * taken on trust.
	 *
	 * @param stream    the font file
	 * @param sizeLimit largest file to accept, in bytes
	 * @return false if the stream does not hold a valid font of this format
	 */
	bool load(Common::SeekableReadStream &stream, uint32 sizeLimit = 64 * 1024 * 1024);

	/**
	 * Read a font's header and tables only (its metrics and code point
	 * table, 4 and 8 bytes a glyph); glyphData() reads each glyph from
	 * @p stream when asked for it. The same checks as load(). The stream is
	 * kept until free(), and deleted then when @p dispose says so (also when
	 * this fails). A font with at most streamThreshold() bytes of glyphs is
	 * read whole instead (isStreamed() false), and the stream let go. The
	 * caller drops a stdio stream's buffer as it opens it
	 * (unbufferCacheStream()), before reading anything.
	 */
	bool loadStreamed(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose,
					  uint32 sizeLimit = 64 * 1024 * 1024);

	void free();

	bool isLoaded() const { return _loaded; }

	/// Whether the glyphs are read from the file as they are asked for (loadStreamed()).
	bool isStreamed() const { return _stream != nullptr; }

	/// HIRES_SVF_STREAM_MIN, or what a test set.
	static uint32 streamThreshold() { return _streamThreshold; }
	static void setStreamThreshold(uint32 bytes) { _streamThreshold = bytes; }

	int bpp() const { return _bpp; }
	int cellWidth() const { return _cellW; }
	int cellHeight() const { return _cellH; }
	int ascent() const { return _ascent; }
	int glyphCount() const { return _glyphs; }

	/// True when the file carries a metrics table, i.e. glyphs may differ in
	/// width. A false here means every glyph advances by the cell width.
	bool isProportional() const { return _metrics != nullptr; }

	/**
	 * Flags bit 2 (FONT_FORMAT.md section 3): a combining mark's rows hold
	 * the pen at column max(0, -bearingX), so ink left of the pen is kept.
	 * Without it (every file written before the bit existed) the rows start
	 * at the pen and a mark's ink left of it was clipped when it was baked.
	 */
	bool marksAtOrigin() const { return _marksAtOrigin; }

	/// The code page the glyphs are ordered by, or kUtf8 for a font that
	/// carries its own code point table.
	Common::CodePage codePage() const { return _codePage; }

	/**
	 * Find the glyph for a Unicode code point.
	 *
	 * @return the glyph index, or -1 when the font has no glyph for it
	 */
	int glyphIndex(uint32 codepoint) const;

	bool hasGlyph(uint32 codepoint) const { return glyphIndex(codepoint) >= 0; }

	/// Metrics of a glyph by index. False for an index this font does not have.
	bool glyphMetrics(int index, GlyphMetrics &out) const;

	/**
	 * The pixels of a glyph by index, or null for an index this font does not
	 * have. 1bpp and 2bpp rows are packed MSB first (at 2bpp the leftmost
	 * pixel is the top bit pair, level 0..3) and padded to a byte; 8bpp rows
	 * are one byte per pixel. Rows are cellWidth() pixels wide in every case.
	 *
	 * A streamed font (loadStreamed()) reads whole glyphs, as many as fit in
	 * HIRES_SVF_READ_BLOCK bytes from a multiple of that many, in one read
	 * into one buffer; the glyph asked for and its neighbours are then
	 * served from there (SvfnGlyphSource::prefetch() asks in file order).
	 * The pointer holds until the next call, and is null if the read fails;
	 * a block whose read failed is not read again until free().
	 */
	const byte *glyphData(int index) const;

	/// Reads of the file a streamed font has made (one per block), and the bytes.
	uint32 readCount() const { return _readCount; }
	uint32 readBytes() const { return _readBytes; }
	/// Bytes a streamed font holds: its tables and read buffers.
	uint32 memoryBytes() const;

	/// Bytes between the start of one row of a glyph and the next.
	int glyphPitch() const { return _rowPitch; }

	/**
	 * Whether code points are looked up in the font's own table, in place
	 * (version 2), rather than in a map built from it.
	 */
	bool searchesCodePointTable() const { return _cmapTable != nullptr; }

	/// Whether that table had to be given a sorted order of its own.
	bool codePointTableNeedsOrder() const { return !_cmapOrder.empty(); }

private:
	/// The header's fields, checked against a file of @p size bytes.
	struct Layout {
		uint16 version, flags, codePage;
		int bpp, glyphs, cellW, cellH, ascent, rowPitch;
		uint32 glyphStride, metricsOff, dataOff, dataSize, cmapOff;
		bool metricsOk;	///< there is a metrics table, and it fits
	};
	/// @p have bytes of the file's start at @p raw, of a file of @p size bytes.
	static bool readLayout(const byte *raw, uint32 have, uint32 size, Layout &out);
	/// Checks a version 2 table's indices; @p order gets a sorted order when the table is not sorted.
	static bool checkCodePointTable(const byte *table, int glyphs, Common::Array<uint16> &order);
	void adopt(const Layout &layout, const byte *metrics, const byte *cmapTable, Common::Array<uint16> &cmapOrder);

	int legacyGlyphIndex(uint32 codepoint) const;
	int searchCodePointTable(uint32 codepoint) const;

	/// Orders entries of a code point table by code point, ties by position.
	struct CmapEntryLess {
		explicit CmapEntryLess(const byte *table);
		bool operator()(uint16 a, uint16 b) const;
		const byte *_table;
	};
	void clearLookupCache();

	/// The whole file (load()), or the tables only (loadStreamed()).
	byte *_data;
	bool _loaded;

	/// The glyphs: in _data, or in the file (null then).
	const byte *_pixels;
	/// Where a streamed font's glyphs are read from.
	Common::SeekableReadStream *_stream;
	DisposeAfterUse::Flag _disposeStream;
	uint32 _dataOff;
	uint32 _tablesSize;
	static uint32 _streamThreshold;
	/// Glyphs [first, first + count) of a streamed font, as read.
	struct ReadBlock {
		ReadBlock() : first(-1), count(0), lastUse(0) {}
		Common::Array<byte> data;
		int first, count;
		uint32 lastUse;
	};
	enum { kReadBlocks = 1 };
	mutable ReadBlock _blocks[kReadBlocks];
	int _glyphsPerBlock;
	mutable uint32 _readClock, _readCount, _readBytes;
	mutable Common::Array<int> _failedBlocks;	///< first glyph of each block whose read failed

	const byte *_metrics;

	int _bpp;
	int _cellW;
	int _cellH;
	int _ascent;
	bool _marksAtOrigin;
	int _glyphs;
	int _rowPitch;
	int _glyphStride;

	Common::CodePage _codePage;

	/// A version 2 font's code point table inside _data, searched in place.
	const byte *_cmapTable;
	int _cmapEntries;
	/// The table's entries in code point order (ties in file order), when
	/// the file does not list them so; empty when it does.
	Common::Array<uint16> _cmapOrder;

	/// The last lookups in _cmapTable, by the low bits of the code point.
	struct LookupHit {
		uint32 codepoint;
		int index;
		bool valid;
	};
	enum { kLookupCacheSize = 32 };
	mutable LookupHit _lookupCache[kLookupCacheSize];


	/// The same, worked out from the code page of a font that does not.
	/// Built on the first lookup, hence mutable.
	mutable Common::HashMap<uint32, int> _legacyMap;
};

} // End of namespace Graphics

#endif
