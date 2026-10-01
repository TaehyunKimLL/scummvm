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


#include "graphics/hires_text/glyph_source_fallback.h"

#include "common/textconsole.h"
#include "graphics/hires_text/text_compose.h"

namespace Graphics {

FallbackGlyphSource::FallbackGlyphSource(const Common::Array<UnicodeGlyphSource *> &sources,
										 DisposeAfterUse::Flag dispose)
	: _dispose(dispose) {
	for (uint i = 0; i < sources.size(); i++) {
		UnicodeGlyphSource *src = sources[i];
		if (!src)
			continue;
		_sources.push_back(src);
		if (!_lookup.empty()) {
			const UnicodeGlyphSource *first = _lookup[0];
			if (src->cellWidth() != first->cellWidth() || src->cellHeight() != first->cellHeight() ||
				src->bitsPerPixel() != first->bitsPerPixel()) {
				warning("HiResText: fallback face %u has a %dx%d cell at %d bpp, the chain %dx%d at %d bpp; "
						"leaving it out", i, src->cellWidth(), src->cellHeight(), src->bitsPerPixel(),
						first->cellWidth(), first->cellHeight(), first->bitsPerPixel());
				continue;
			}
		}
		_lookup.push_back(src);
	}
}

FallbackGlyphSource::~FallbackGlyphSource() {
	if (_dispose == DisposeAfterUse::YES) {
		for (uint i = 0; i < _sources.size(); i++)
			delete _sources[i];
	}
}

byte FallbackGlyphSource::cellWidth() const {
	return _lookup.empty() ? 0 : _lookup[0]->cellWidth();
}

byte FallbackGlyphSource::cellHeight() const {
	return _lookup.empty() ? 0 : _lookup[0]->cellHeight();
}

byte FallbackGlyphSource::advanceNarrow() const {
	return _lookup.empty() ? 0 : _lookup[0]->advanceNarrow();
}

byte FallbackGlyphSource::advanceWide() const {
	return _lookup.empty() ? 0 : _lookup[0]->advanceWide();
}

int FallbackGlyphSource::bitsPerPixel() const {
	return _lookup.empty() ? 8 : _lookup[0]->bitsPerPixel();
}

UnicodeGlyphSource *FallbackGlyphSource::answering(uint32 cp) {
	Common::HashMap<uint32, int>::const_iterator it = _answer.find(cp);
	if (it != _answer.end())
		return it->_value < 0 ? nullptr : _lookup[it->_value];

	int found = -1;
	for (uint i = 0; i < _lookup.size() && found < 0; i++) {
		if (_lookup[i]->cells(cp) > 0)
			found = (int)i;
	}
	_answer[cp] = found;
	return found < 0 ? nullptr : _lookup[found];
}

void FallbackGlyphSource::prefetch(Common::Array<uint32> &cps) {
	for (uint i = 0; i < _lookup.size() && !cps.empty(); i++)
		_lookup[i]->prefetch(cps);
}

int FallbackGlyphSource::cells(uint32 cp) {
	UnicodeGlyphSource *src = answering(cp);
	return src ? src->cells(cp) : 0;
}

const byte *FallbackGlyphSource::row(uint32 cp, int y) {
	UnicodeGlyphSource *src = answering(cp);
	return src ? src->row(cp, y) : nullptr;
}

int FallbackGlyphSource::advance(uint32 cp) {
	UnicodeGlyphSource *src = answering(cp);
	return src ? src->advance(cp) : 0;
}

bool FallbackGlyphSource::metrics(uint32 cp, GlyphMetrics &m) {
	UnicodeGlyphSource *src = answering(cp);
	return src ? src->metrics(cp, m) : false;
}

uint32 FallbackGlyphSource::glyphCount() const {
	uint32 n = 0;
	for (uint i = 0; i < _sources.size(); i++)
		n += _sources[i]->glyphCount();
	return n;
}

// --- NormalizedGlyphSource ------------------------------------------------

NormalizedGlyphSource *NormalizedGlyphSource::create(UnicodeGlyphSource *src, byte cellWidth, byte cellHeight,
													 DisposeAfterUse::Flag dispose, Common::String &error) {
	return create(src, cellWidth, cellHeight, 0, dispose, error);
}

NormalizedGlyphSource *NormalizedGlyphSource::create(UnicodeGlyphSource *src, byte cellWidth, byte cellHeight,
													 int topRow, DisposeAfterUse::Flag dispose, Common::String &error) {
	if (topRow < 0)
		topRow = 0;
	if (!src) {
		error = "no source";
		return nullptr;
	}
	const int bpp = src->bitsPerPixel();
	if (src->cellWidth() > cellWidth || src->cellHeight() + topRow > cellHeight || (bpp != 1 && bpp != 2 && bpp != 8)) {
		error = Common::String::format("a %dx%d cell at %d bpp does not fit a %dx%d cell at 8 bpp",
									   src->cellWidth(), src->cellHeight(), bpp, cellWidth, cellHeight);
		if (dispose == DisposeAfterUse::YES)
			delete src;
		return nullptr;
	}
	NormalizedGlyphSource *n = new NormalizedGlyphSource(src, cellWidth, cellHeight, dispose);
	n->_topRow = topRow;
	return n;
}

NormalizedGlyphSource::NormalizedGlyphSource(UnicodeGlyphSource *src, byte cellWidth, byte cellHeight,
											 DisposeAfterUse::Flag dispose)
	: _src(src), _cellWidth(cellWidth), _cellHeight(cellHeight), _dispose(dispose) {
	_scratch.resize((uint)cellWidth * 2);
}

NormalizedGlyphSource::~NormalizedGlyphSource() {
	if (_dispose == DisposeAfterUse::YES)
		delete _src;
}

const byte *NormalizedGlyphSource::row(uint32 cp, int y) {
	const int bpp = _src->bitsPerPixel();
	if (bpp == 8 && _src->cellWidth() == _cellWidth && _src->cellHeight() == _cellHeight && _topRow == 0)
		return _src->row(cp, y);
	y -= _topRow;

	byte *out = _scratch.begin();
	memset(out, 0, _scratch.size());
	if (y < 0 || y >= _src->cellHeight())
		return out;
	const byte *in = _src->row(cp, y);
	if (!in)
		return out;
	const int w = _src->cellWidth() * 2;
	for (int x = 0; x < w; x++)
		out[x] = TextCompose::expandCoverage(in, x, bpp);
	return out;
}

} // End of namespace Graphics
