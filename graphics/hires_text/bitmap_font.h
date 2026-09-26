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

namespace Common {
class SeekableReadStream;
}

namespace Graphics {

/**
 * Metrics of a single glyph, in the pixels the font was drawn at.
 *
 * A font whose glyphs all share one advance still fills these in; a caller
 * therefore never has to ask whether a font is proportional before laying text
 * out, only whether it wants the font's opinion at all.
 */
struct GlyphMetrics {
	GlyphMetrics() : advance(0), bearingX(0), bearingY(0), width(0), height(0) {}

	int16 advance;   ///< how far the pen moves for this glyph
	int16 bearingX;  ///< pen position to the left edge of the ink
	int16 bearingY;  ///< baseline to the top edge of the cell
	uint16 width;    ///< width of the ink, 0 for a blank glyph
	uint16 height;   ///< height of the cell the glyph is stored in
};

/**
 * A bitmap font holding either a 1bpp stencil or 8bpp coverage per pixel.
 *
 * The 8bpp form is what makes anti-aliased text possible without a rasteriser
 * in the build: the shapes are baked once, by a tool that may use FreeType,
 * and at run time only need blending. That makes it the primary format rather
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

	void free();

	bool isLoaded() const { return _data != nullptr; }

	int bpp() const { return _bpp; }
	int cellWidth() const { return _cellW; }
	int cellHeight() const { return _cellH; }
	int ascent() const { return _ascent; }
	int glyphCount() const { return _glyphs; }

	/// True when the file carries a metrics table, i.e. glyphs may differ in
	/// width. A false here means every glyph advances by the cell width.
	bool isProportional() const { return _metrics != nullptr; }

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
	 * have. 1bpp rows are packed MSB first and padded to a byte; 8bpp rows are
	 * one byte per pixel. Rows are cellWidth() wide either way.
	 */
	const byte *glyphData(int index) const;

	/// Bytes between the start of one row of a glyph and the next.
	int glyphPitch() const { return _rowPitch; }

private:
	int legacyGlyphIndex(uint32 codepoint) const;

	byte *_data;
	uint32 _dataSize;

	const byte *_pixels;
	const byte *_metrics;

	int _bpp;
	int _cellW;
	int _cellH;
	int _ascent;
	int _glyphs;
	int _rowPitch;
	int _glyphStride;

	Common::CodePage _codePage;

	/// Code point to glyph index, for fonts that carry the table themselves.
	Common::HashMap<uint32, int> _cmap;

	/// The same, worked out from the code page of a font that does not.
	/// Built on the first lookup, hence mutable.
	mutable Common::HashMap<uint32, int> _legacyMap;
};

} // End of namespace Graphics

#endif
