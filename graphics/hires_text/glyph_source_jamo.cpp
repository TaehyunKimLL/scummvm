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

#include "graphics/hires_text/glyph_source_jamo.h"

#include "common/algorithm.h"
#include "common/textconsole.h"
#include "graphics/hires_text/bitmap_font.h"
#include "graphics/hires_text/glyph_source_svfn.h"
#include "graphics/hires_text/jamo_set.h"

namespace Graphics {

// What an entry costs besides its rows (as in glyph_source_svfn.cpp).
static const uint32 kEntryOverhead = 64;

JamoGlyphSource::JamoGlyphSource(HiResBitmapFont *font, DisposeAfterUse::Flag dispose)
	: _inner(new SvfnGlyphSource(font, dispose)), _font(font), _table(nullptr), _bpp(1), _rowBytes(0),
	  _cellHeight(0), _maxEntries(32), _clock(0), _composed(0) {
	if (!_font || !_font->isLoaded())
		return;
	_table = _font->jamoSets();
	_bpp = _font->bpp();
	_cellHeight = (uint32)_font->cellHeight();
	_rowBytes = ((uint32)_font->cellWidth() * 2 * _bpp + 7) / 8;
	setCacheBytes(HIRES_SVF_CACHE_KB * 1024);
}

void JamoGlyphSource::setCacheBytes(uint32 bytes, uint32 minEntries) {
	// One face, one budget: composed syllables and their pieces share it.
	_inner->setCacheBytes(bytes / 2, minEntries);
	_maxEntries = MAX<uint32>(minEntries, bytes / 2 / MAX<uint32>(1, _rowBytes * _cellHeight + kEntryOverhead));
	while (_entries.size() > _maxEntries) {
		uint oldest = 0;
		for (uint i = 1; i < _entries.size(); ++i)
			if (_entries[i]->lastUse < _entries[oldest]->lastUse)
				oldest = i;
		_byCp.erase(_entries[oldest]->cp);
		delete _entries[oldest];
		_entries.remove_at(oldest);
	}
}

uint32 JamoGlyphSource::pieceCacheEntries() const {
	return _inner->cacheEntries();
}

JamoGlyphSource::~JamoGlyphSource() {
	for (uint i = 0; i < _entries.size(); ++i)
		delete _entries[i];
	delete _inner;
}

bool JamoGlyphSource::usable(const HiResBitmapFont &font, Common::String &why) {
	const JamoSetTable *t = font.jamoSets();
	if (!t) {
		why = font.jamoSetError().empty() ? Common::String("no JSET block") : font.jamoSetError();
		return false;
	}
	const Common::Array<uint16> *lists[3] = { &t->initPiece, &t->medPiece, &t->finPiece };
	for (int w = 0; w < 3; ++w)
		for (uint i = 0; i < lists[w]->size(); ++i) {
			const uint16 n = (*lists[w])[i];
			if (n != 0xFFFF && !font.hasGlyph(kJamoPieceBase + n)) {
				why = Common::String::format("piece %u has no glyph", n);
				return false;
			}
		}
	for (uint c = 0; c < 51; ++c) {
		const uint16 n = t->compatPiece[c];
		if (n != 0xFFFF && !font.hasGlyph(kJamoPieceBase + n)) {
			why = Common::String::format("piece %u has no glyph", n);
			return false;
		}
	}
	return true;
}

void JamoGlyphSource::setName(const Common::String &name) {
	_inner->setName(name);
}

int JamoGlyphSource::piecesOf(uint32 cp, uint32 out[3]) const {
	if (!_table)
		return -1;
	uint16 numbers[3];
	int count = 0;
	if (!_table->pieces(cp, numbers, count)) {
		const uint16 one = _table->compat(cp);
		if (one == 0xFFFF)
			return -1;
		out[0] = kJamoPieceBase + one;
		return 1;
	}
	int n = 0;
	for (int i = 0; i < count; ++i)
		if (numbers[i] != 0xFFFF)
			out[n++] = kJamoPieceBase + numbers[i];
	return n;
}

byte JamoGlyphSource::cellWidth() const { return _inner->cellWidth(); }
byte JamoGlyphSource::cellHeight() const { return _inner->cellHeight(); }
byte JamoGlyphSource::advanceNarrow() const { return _inner->advanceNarrow(); }
byte JamoGlyphSource::advanceWide() const { return _inner->advanceWide(); }
int JamoGlyphSource::bitsPerPixel() const { return _inner->bitsPerPixel(); }
uint32 JamoGlyphSource::glyphCount() const { return _inner->glyphCount(); }
int JamoGlyphSource::baselineRow() const { return _inner->baselineRow(); }
int JamoGlyphSource::inkRowsAbove() const { return _inner->inkRowsAbove(); }
int JamoGlyphSource::inkRowsBelow() const { return _inner->inkRowsBelow(); }

int JamoGlyphSource::cells(uint32 cp) {
	uint32 pieces[3];
	return piecesOf(cp, pieces) >= 0 ? 2 : _inner->cells(cp);
}

int JamoGlyphSource::advance(uint32 cp) {
	uint32 pieces[3];
	if (piecesOf(cp, pieces) < 0)
		return _inner->advance(cp);
	return _table->advance ? _table->advance : _inner->cellWidth();
}

bool JamoGlyphSource::metrics(uint32 cp, GlyphMetrics &m) {
	uint32 pieces[3];
	if (piecesOf(cp, pieces) < 0)
		return _inner->metrics(cp, m);
	// A composed glyph fills its cell: ink from the pen, the font's baseline.
	if (!UnicodeGlyphSource::metrics(cp, m))
		return false;
	m.bearingX = 0;
	m.bearingY = (int16)_font->ascent();
	m.width = _inner->cellWidth();
	m.height = _inner->cellHeight();
	m.wide = true;
	return true;
}

/// Takes the larger coverage of @p src and @p dst at each pixel of a row packed as the font is.
static void maxRow(byte *dst, const byte *src, uint32 bytes, int bpp) {
	for (uint32 i = 0; i < bytes; ++i) {
		if (bpp == 1) {
			dst[i] |= src[i];
		} else if (bpp == 8) {
			dst[i] = MAX(dst[i], src[i]);
		} else {
			// Four 2-bit levels a byte, the leftmost in the top bits.
			byte out = 0;
			for (int shift = 6; shift >= 0; shift -= 2)
				out |= MAX<byte>((dst[i] >> shift) & 3, (src[i] >> shift) & 3) << shift;
			dst[i] = out;
		}
	}
}

void JamoGlyphSource::compose(Entry &entry, const uint32 *pieces, int count) {
	entry.rows.resize(_rowBytes * _cellHeight);
	memset(entry.rows.begin(), 0, entry.rows.size());
	for (int p = 0; p < count; ++p) {
		if (_inner->cells(pieces[p]) <= 0)
			continue;
		for (uint32 y = 0; y < _cellHeight; ++y) {
			// The pointer holds only until the next code point is asked for
			// (or the next row, of a piece whose read failed): used at once.
			const byte *src = _inner->row(pieces[p], (int)y);
			if (src)
				maxRow(&entry.rows[y * _rowBytes], src, _rowBytes, _bpp);
		}
	}
	++_composed;
}

JamoGlyphSource::Entry &JamoGlyphSource::ensure(uint32 cp) {
	Common::HashMap<uint32, Entry *>::iterator it = _byCp.find(cp);
	if (it != _byCp.end()) {
		it->_value->lastUse = ++_clock;
		return *it->_value;
	}
	Entry *entry;
	if (_entries.size() < _maxEntries) {
		entry = new Entry();
		_entries.push_back(entry);
	} else {
		entry = _entries[0];
		for (uint i = 1; i < _entries.size(); ++i)
			if (_entries[i]->lastUse < entry->lastUse)
				entry = _entries[i];
		_byCp.erase(entry->cp);
	}
	entry->cp = cp;
	entry->lastUse = ++_clock;
	_byCp[cp] = entry;
	uint32 pieces[3];
	const int count = piecesOf(cp, pieces);
	compose(*entry, pieces, MAX(count, 0));
	return *entry;
}

const byte *JamoGlyphSource::row(uint32 cp, int y) {
	uint32 pieces[3];
	if (piecesOf(cp, pieces) < 0)
		return _inner->row(cp, y);
	if (y < 0 || (uint32)y >= _cellHeight)
		return nullptr;
	return &ensure(cp).rows[(uint32)y * _rowBytes];
}

void JamoGlyphSource::prefetch(Common::Array<uint32> &cps) {
	// What this source composes is asked of the inner one as its pieces; the
	// rest as it is. Only the latter come back out.
	Common::Array<uint32> ask, others;
	for (uint i = 0; i < cps.size(); ++i) {
		uint32 pieces[3];
		const int n = piecesOf(cps[i], pieces);
		if (n < 0) {
			ask.push_back(cps[i]);
			others.push_back(cps[i]);
			continue;
		}
		for (int k = 0; k < n; ++k)
			if (Common::find(ask.begin(), ask.end(), pieces[k]) == ask.end())
				ask.push_back(pieces[k]);
	}
	_inner->prefetch(ask);
	cps.clear();
	for (uint i = 0; i < ask.size(); ++i)
		if (Common::find(others.begin(), others.end(), ask[i]) != others.end())
			cps.push_back(ask[i]);
}

uint32 JamoGlyphSource::preload(const Common::Array<uint32> &cps) {
	Common::Array<uint32> ask;
	for (uint i = 0; i < cps.size(); ++i) {
		uint32 pieces[3];
		const int n = piecesOf(cps[i], pieces);
		if (n < 0) {
			ask.push_back(cps[i]);
			continue;
		}
		for (int k = 0; k < n; ++k)
			ask.push_back(pieces[k]);
	}
	return _inner->preload(ask);
}

} // End of namespace Graphics
