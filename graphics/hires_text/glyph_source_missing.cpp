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

#include "graphics/hires_text/glyph_source_missing.h"
#include "graphics/hires_text/unicode_props.h"

#include "common/textconsole.h"

namespace Graphics {

MissingGlyphSource::MissingGlyphSource(UnicodeGlyphSource *inner, uint32 boxCp, DisposeAfterUse::Flag dispose)
	: _inner(inner), _boxCp(boxCp), _dispose(dispose) {
}

MissingGlyphSource::~MissingGlyphSource() {
	if (_dispose == DisposeAfterUse::YES)
		delete _inner;
}

int MissingGlyphSource::boxCells(uint32 cp) {
	if (_inner->cells(cp) > 0)
		return 0;
	// With no box glyph there is nothing to draw a wide box with; a narrow
	// one alone would make the two widths disagree, so neither is drawn.
	if (!_boxCp || cp == _boxCp || _inner->cells(_boxCp) <= 0)
		return 0;
	if (!_substituted.contains(cp)) {
		_substituted[cp] = true;
		warning("HiResText: U+%04X has no glyph; drawing U+%04X", cp, _boxCp);
	}
	return Unicode::isWide(cp) ? 2 : 1;
}

int MissingGlyphSource::cells(uint32 cp) {
	const int own = _inner->cells(cp);
	return own > 0 ? own : boxCells(cp);
}

const byte *MissingGlyphSource::row(uint32 cp, int y) {
	const int box = boxCells(cp);
	if (!box)
		return _inner->row(cp, y);
	if (box == 2)
		return _inner->row(_boxCp, y);

	const int h = cellHeight();
	if (y < 0 || y >= h)
		return nullptr;
	const int bpp = bitsPerPixel();
	// A row is cellWidth() * 2 pixels at inner's depth.
	const uint rowBytes = ((uint)cellWidth() * 2 * bpp + 7) / 8;
	if (_narrowBox.empty()) {
		// A 1 px outline in the narrow half cell (cellWidth() / 2 columns),
		// one pixel in from every edge: rows 1 and h - 2 across, the rows
		// between only at the two ends.
		_narrowBox.resize(rowBytes * h, 0);
		const int w = cellWidth() / 2;
		const int left = 1, right = w - 2, top = 1, bottom = h - 2;
		const byte full = (byte)((1 << bpp) - 1);
		for (int yy = top; yy <= bottom && left <= right; yy++) {
			byte *r = &_narrowBox[yy * rowBytes];
			for (int x = left; x <= right; x++) {
				if (yy != top && yy != bottom && x != left && x != right)
					continue;
				// Packed MSB first, as TextCompose::expandCoverage() reads.
				const int bit = x * bpp;
				r[bit / 8] |= (byte)(full << (8 - bpp - bit % 8));
			}
		}
	}
	return &_narrowBox[y * rowBytes];
}

int MissingGlyphSource::advance(uint32 cp) {
	const int box = boxCells(cp);
	if (!box)
		return _inner->advance(cp);
	return box == 2 ? advanceWide() : advanceNarrow();
}

bool MissingGlyphSource::metrics(uint32 cp, GlyphMetrics &m) {
	const int box = boxCells(cp);
	if (!box)
		return _inner->metrics(cp, m);
	// The box stands at the pen and advances by its cell, even for a
	// combining mark: a box drawn over its base would hide both.
	m = GlyphMetrics();
	m.wide = Unicode::isWide(cp);
	m.advance = (int16)(box == 2 ? advanceWide() : advanceNarrow());
	return true;
}

} // End of namespace Graphics
