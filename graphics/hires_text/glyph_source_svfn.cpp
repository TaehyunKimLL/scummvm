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

#include "common/algorithm.h"
#include "common/textconsole.h"
#include "graphics/hires_text/bitmap_font.h"
#include "graphics/hires_text/unicode_props.h"

namespace Graphics {

SvfnGlyphSource::SvfnGlyphSource(HiResBitmapFont *font, DisposeAfterUse::Flag dispose)
	: _font(font), _dispose(dispose), _cellWidth(0), _cellHeight(0), _bitsPerPixel(1), _rowBytes(0), _maxEntries(32),
	  _clock(0), _glyphReads(0), _lastCp(0xFFFFFFFF), _failedCp(0xFFFFFFFF), _readFailWarned(false), _registered(false) {
	_stats.kind = "svf";
	if (!_font || !_font->isLoaded()) {
		warning("SvfnGlyphSource: font is not loaded; no glyphs");
		return;
	}
	// The SVFN header stores both as a byte, so they always fit.
	_cellWidth = (byte)_font->cellWidth();
	_cellHeight = (byte)_font->cellHeight();
	_bitsPerPixel = _font->bpp();
	_rowBytes = ((uint32)_cellWidth * 2 * _bitsPerPixel + 7) / 8;
	setCacheBytes(HIRES_SVF_CACHE_KB * 1024);
	if (_font->isStreamed()) {
		Common::FileCacheRegistry::add(&_stats);
		_registered = true;
	}
}

// What an entry costs besides its rows: the entry, its hash map node and
// the allocator's headers, about.
static const uint32 kEntryOverhead = 64;

void SvfnGlyphSource::setCacheBytes(uint32 bytes) {
	const uint32 each = _rowBytes * _cellHeight + kEntryOverhead;
	_maxEntries = MAX<uint32>(32, bytes / each);
	_stats.capacity = _maxEntries * each;
	while (_entries.size() > _maxEntries) {
		// The least recently used go.
		uint oldest = 0;
		for (uint i = 1; i < _entries.size(); ++i)
			if (_entries[i]->lastUse < _entries[oldest]->lastUse)
				oldest = i;
		_byCp.erase(_entries[oldest]->cp);
		delete _entries[oldest];
		_entries.remove_at(oldest);
	}
	if (_font && _font->isLoaded())
		updateStats();
}

void SvfnGlyphSource::updateStats() {
	uint32 used = 0;
	for (uint i = 0; i < _entries.size(); ++i)
		used += _entries[i]->rows.size() + kEntryOverhead;
	_stats.used = used + (_font->isStreamed() ? _font->memoryBytes() : 0);
	_stats.reads = _font->readCount();
	_stats.readBytes = _font->readBytes();
}

SvfnGlyphSource::~SvfnGlyphSource() {
	if (_registered)
		Common::FileCacheRegistry::remove(&_stats);
	for (uint i = 0; i < _entries.size(); ++i)
		delete _entries[i];
	if (_dispose == DisposeAfterUse::YES)
		delete _font;
}

SvfnGlyphSource::Entry &SvfnGlyphSource::takeEntry(uint32 cp) {
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
	entry->cells = 0;
	_byCp[cp] = entry;
	return *entry;
}

SvfnGlyphSource::Entry *SvfnGlyphSource::find(uint32 cp) {
	Common::HashMap<uint32, Entry *>::iterator it = _byCp.find(cp);
	return it != _byCp.end() ? it->_value : nullptr;
}

SvfnGlyphSource::Entry &SvfnGlyphSource::ensure(uint32 cp) {
	const bool counted = cp != _lastCp;
	_lastCp = cp;
	if (counted)
		++_stats.lookups;
	Entry *found = find(cp);
	if (found) {
		if (counted)
			++_stats.hits;
		found->lastUse = ++_clock;
		return *found;
	}

	// A glyph whose read failed in this same drawing is not read again for
	// each of its rows (row() lets row 0 try again).
	if (cp == _failedCp)
		return _failed;

	// Taken first, so a miss is cached too.
	Entry &entry = takeEntry(cp);
	entry.lastUse = ++_clock;
	if (!_rowBytes)
		return entry;

	const int index = _font->glyphIndex(cp);
	const byte *glyph = index >= 0 ? _font->glyphData(index) : nullptr;
	if (!glyph && index >= 0) {
		// The font has it but its pixels could not be read (a removed disc,
		// a file changed underneath). Nothing is kept, so the next use tries
		// again; meanwhile it is blank at the width cells() gives, never a
		// missing row.
		if (!_readFailWarned) {
			warning("SVF %s: a glyph could not be read from the file; drawn blank until it can be",
					_stats.name.c_str());
			_readFailWarned = true;
		}
		_byCp.erase(cp);
		entry.cp = 0xFFFFFFFF;
		entry.lastUse = 0;
		_failedCp = cp;
		_failed.cp = cp;
		_failed.cells = cellsFor(cp, index);
		_failed.rows.resize(_rowBytes * _cellHeight);
		memset(_failed.rows.begin(), 0, _failed.rows.size());
		updateStats();
		return _failed;
	}
	if (!glyph)
		return entry;
	if (cp == _failedCp)
		_failedCp = 0xFFFFFFFF;
	++_glyphReads;

	const uint32 pitch = (uint32)_font->glyphPitch();
	entry.cells = cellsFor(cp, index);
	// The rows of the entry's last code point, if any, are all overwritten.
	entry.rows.resize(_rowBytes * _cellHeight);
	for (uint32 y = 0; y < _cellHeight; y++) {
		byte *dst = &entry.rows[y * _rowBytes];
		memcpy(dst, glyph + y * pitch, pitch);
		memset(dst + pitch, 0, _rowBytes - pitch);
		// Packed below 8bpp, the last byte of a row may hold bits past the
		// cell (18 px at 2bpp is 36 bits: 4 of padding); a wide glyph reads
		// those as its second cell, so they are cleared.
		const int usedBits = (_cellWidth * _bitsPerPixel) & 7;
		if (usedBits)
			dst[pitch - 1] &= (byte)(0xFF << (8 - usedBits));
	}
	updateStats();
	return entry;
}

void SvfnGlyphSource::prefetch(Common::Array<uint32> &cps) {
	if (!_rowBytes)
		return;
	struct Want {
		int index;
		uint32 cp;
	};
	Common::Array<Want> want;
	Common::Array<uint32> rest;
	for (uint i = 0; i < cps.size(); ++i) {
		const uint32 cp = cps[i];
		Entry *kept = find(cp);
		if (kept) {
			// About to be drawn: not what the new glyphs below push out.
			kept->lastUse = ++_clock;
			if (!kept->cells)
				rest.push_back(cp);
			continue;
		}
		const int index = _font->glyphIndex(cp);
		if (index < 0) {
			rest.push_back(cp);
			continue;
		}
		bool listed = false;
		for (uint k = 0; k < want.size() && !listed; ++k)
			listed = want[k].cp == cp;
		if (!listed && want.size() < _maxEntries) {
			Want w = { index, cp };
			want.push_back(w);
		}
	}
	cps.swap(rest);
	if (want.empty() || !_font->isStreamed())
		return;
	Common::sort(want.begin(), want.end(), [](const Want &a, const Want &b) { return a.index < b.index; });
	// Not counted as lookups: the draw that follows is.
	const uint32 lookups = _stats.lookups, hits = _stats.hits;
	for (uint i = 0; i < want.size(); ++i)
		ensure(want[i].cp);
	_stats.lookups = lookups;
	_stats.hits = hits;
	_lastCp = 0xFFFFFFFF;
}

byte SvfnGlyphSource::cellsFor(uint32 cp, int index) const {
	// A font with a metrics table says how wide each glyph is: one whose
	// advance runs past the narrow half cell takes two cells, whatever its
	// Unicode width. East Asian Ambiguous characters (U+25CB, U+25A1, U+2015,
	// circled letters) are drawn full width by a Korean font but are not
	// Unicode::isWide(), so a cell-based layout would advance them by half
	// the ink they draw. Without the table only the Unicode width is known.
	GlyphMetrics m;
	if (_font->isProportional() && _font->glyphMetrics(index, m))
		return m.advance > _cellWidth / 2 ? 2 : 1;
	return Unicode::isWide(cp) ? 2 : 1;
}

int SvfnGlyphSource::cells(uint32 cp) {
	// From the tables alone: a layout or a coverage check that only asks
	// how wide a glyph is reads no pixels and keeps nothing.
	if (!_rowBytes)
		return 0;
	const Entry *kept = find(cp);
	if (kept)
		return kept->cells;
	const int index = _font->glyphIndex(cp);
	return index >= 0 ? cellsFor(cp, index) : 0;
}

const byte *SvfnGlyphSource::row(uint32 cp, int y) {
	// Row 0 starts a drawing of the glyph: one whose read failed is tried
	// again then, not for each of its other rows.
	if (y == 0 && cp == _failedCp)
		_failedCp = 0xFFFFFFFF;
	Entry &entry = ensure(cp);
	if (!entry.cells || y < 0 || y >= _cellHeight)
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

bool SvfnGlyphSource::metrics(uint32 cp, GlyphMetrics &m) {
	if (!UnicodeGlyphSource::metrics(cp, m))
		return false;
	// originX is 0 for every glyph of a file written before flags bit 2:
	// its rows start at the pen (HiResFontBaker drew with the pen at column
	// 0, clipping ink left of it; mkfont.py shifts the pen so the ink starts
	// at x >= 0). The bearing is passed on as data, read as the signed byte
	// FONT_FORMAT.md section 3 specifies (HiResBitmapFont hands it back
	// unsigned).
	GlyphMetrics font;
	if (_font->glyphMetrics(_font->glyphIndex(cp), font)) {
		m.bearingX = (int8)(byte)font.bearingX;
		m.bearingY = font.bearingY;
		m.width = font.width;
		m.height = font.height;
		// With flags bit 2 a combining mark was stored with the pen at
		// column max(0, -bearingX) - the rule TtfGlyphSource draws marks by
		// - so the ink left of the pen is in the row and the drawer places
		// the pen, not the row start, on the anchor. Other glyphs keep their
		// rows and originX 0, as in TtfGlyphSource.
		if (m.combining && _font->marksAtOrigin() && m.bearingX < 0)
			m.originX = (int16)MIN<int>(-m.bearingX, _cellWidth);
	}
	return true;
}

uint32 SvfnGlyphSource::glyphCount() const {
	return _rowBytes ? (uint32)_font->glyphCount() : 0;
}

int SvfnGlyphSource::baselineRow() const {
	if (!_rowBytes || _font->ascent() <= 0)
		return -1;
	return MIN<int>(_font->ascent(), _cellHeight);
}

} // End of namespace Graphics
