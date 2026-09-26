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

#ifndef GRAPHICS_HIRES_TEXT_GLYPH_SOURCE_SCVMUNI_H
#define GRAPHICS_HIRES_TEXT_GLYPH_SOURCE_SCVMUNI_H

#include "common/array.h"
#include "common/str.h"
#include "graphics/hires_text/glyph_source.h"

namespace Graphics {

/**
 * A parsed SCVMUNI bundle: glyphs addressed by Unicode code point, held in a
 * sorted table and read out through UnicodeGlyphSource.
 *
 * The existing Korean font (SCVMSJIS, GfxFontKorean) indexes its glyph array
 * as `codepoint - 0xAC00`, so it holds exactly the 11,172 pre-composed hangul
 * syllables and nothing else. SCVMUNI replaces that arithmetic with an
 * explicit sorted code point table, so the representable set is whatever the
 * font was built with, in any script, and an absent glyph is a clean lookup
 * miss. Bundles are produced by harness/i18n/m7mkfont.py, which verifies what
 * it writes.
 */
class ScvmuniGlyphSource : public UnicodeGlyphSource {
public:
	/**
	 * Parses data as an SCVMUNI bundle. Returns null and fills error with a
	 * one-line description on any failure: bad magic, unsupported version,
	 * both the 2bpp and 8bpp flags set, an empty font, a table that runs past
	 * the end of data, or a code point table that is not sorted ascending.
	 *
	 * name is used only to name the file in error text.
	 */
	static ScvmuniGlyphSource *create(Common::Array<byte> &&data, const Common::String &name, Common::String &error);

	byte cellWidth() const override { return _cellWidth; }
	byte cellHeight() const override { return _cellHeight; }
	byte advanceNarrow() const override { return _advanceNarrow; }
	byte advanceWide() const override { return _advanceWide; }
	int bitsPerPixel() const override { return _bitsPerPixel; }
	int cells(uint32 cp) override;
	const byte *row(uint32 cp, int y) override;
	uint32 glyphCount() const override { return _glyphCount; }

private:
	ScvmuniGlyphSource() {}

	/**
	 * Binary search of the sorted code point table; -1 when absent. Caches
	 * the last looked-up cp -> index pair, so cells() followed by row() for
	 * the same code point does one search.
	 */
	int findGlyph(uint32 cp);

	Common::Array<byte> _data;

	const byte *_codepoints = nullptr;	// glyphCount x uint32 LE, ascending
	const byte *_widths = nullptr;		// glyphCount x uint8, 1 or 2 cells
	const byte *_bitmaps = nullptr;	// glyphCount x _bytesPerGlyph

	uint32 _glyphCount = 0;
	byte _cellWidth = 0;
	byte _cellHeight = 0;
	byte _advanceNarrow = 0;
	byte _advanceWide = 0;
	byte _bitsPerPixel = 1;
	uint32 _rowBytes = 0;
	uint32 _bytesPerGlyph = 0;

	bool _cacheValid = false;
	uint32 _cachedCp = 0;
	int _cachedIndex = -1;
};

} // End of namespace Graphics

#endif
