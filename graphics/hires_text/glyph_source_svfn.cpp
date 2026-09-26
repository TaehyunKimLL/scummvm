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

#include "graphics/hires_text/glyph_source_svfn.h"

#include "common/textconsole.h"
#include "graphics/hires_text/bitmap_font.h"
#include "graphics/hires_text/glyph_source_ttf.h"

namespace Graphics {

SvfnGlyphSource::SvfnGlyphSource(HiResBitmapFont *font, DisposeAfterUse::Flag dispose)
	: _font(font), _dispose(dispose), _cellWidth(0), _cellHeight(0), _bitsPerPixel(1), _rowBytes(0) {
	if (!_font || !_font->isLoaded()) {
		warning("SvfnGlyphSource: font is not loaded; no glyphs");
		return;
	}
	// The SVFN header stores both as a byte, so they always fit.
	_cellWidth = (byte)_font->cellWidth();
	_cellHeight = (byte)_font->cellHeight();
	_bitsPerPixel = _font->bpp();
	_rowBytes = ((uint32)_cellWidth * 2 * _bitsPerPixel + 7) / 8;
}

SvfnGlyphSource::~SvfnGlyphSource() {
	if (_dispose == DisposeAfterUse::YES)
		delete _font;
}

SvfnGlyphSource::Entry &SvfnGlyphSource::ensure(uint32 cp) {
	Common::HashMap<uint32, Entry>::iterator it = _cache.find(cp);
	if (it != _cache.end())
		return it->_value;

	// Inserted first, so a miss is cached too.
	Entry &entry = _cache[cp];
	if (!_rowBytes)
		return entry;

	const int index = _font->glyphIndex(cp);
	const byte *glyph = _font->glyphData(index);
	if (index < 0 || !glyph)
		return entry;

	const uint32 pitch = (uint32)_font->glyphPitch();
	entry.cells = TtfGlyphSource::isWide(cp) ? 2 : 1;
	entry.rows.resize(_rowBytes * _cellHeight, 0);
	for (uint32 y = 0; y < _cellHeight; y++) {
		byte *dst = &entry.rows[y * _rowBytes];
		memcpy(dst, glyph + y * pitch, pitch);
		// At 1bpp the last byte of a row may hold bits past the cell; a wide
		// glyph reads those as its second cell, so they are cleared.
		if (_bitsPerPixel == 1 && (_cellWidth & 7))
			dst[pitch - 1] &= (byte)(0xFF << (8 - (_cellWidth & 7)));
	}
	return entry;
}

int SvfnGlyphSource::cells(uint32 cp) {
	return ensure(cp).cells;
}

const byte *SvfnGlyphSource::row(uint32 cp, int y) {
	Entry &entry = ensure(cp);
	if (entry.rows.empty() || y < 0 || y >= _cellHeight)
		return nullptr; // contract: only called when cells(cp) > 0
	return &entry.rows[(uint32)y * _rowBytes];
}

int SvfnGlyphSource::advance(uint32 cp) {
	if (!_rowBytes || !_font->isProportional())
		return 0;
	GlyphMetrics m;
	if (!_font->glyphMetrics(_font->glyphIndex(cp), m))
		return 0;
	return m.advance;
}

int SvfnGlyphSource::bearingX(uint32 cp) const {
	if (!_rowBytes)
		return 0;
	GlyphMetrics m;
	if (!_font->glyphMetrics(_font->glyphIndex(cp), m))
		return 0;
	return m.bearingX;
}

uint32 SvfnGlyphSource::glyphCount() const {
	return _rowBytes ? (uint32)_font->glyphCount() : 0;
}

} // End of namespace Graphics
