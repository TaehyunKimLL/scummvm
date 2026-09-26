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

#include "graphics/hires_text/glyph_source_scvmuni.h"

#include "common/endian.h"
#include "common/ptr.h"
#include "common/util.h"

namespace Graphics {

static const char kMagic[8] = { 'S', 'C', 'V', 'M', 'U', 'N', 'I', 0 };
static const uint16 kVersion = 1;
static const uint32 kHeaderSize = 36;

ScvmuniGlyphSource *ScvmuniGlyphSource::create(Common::Array<byte> &&data,
												const Common::String &name,
												Common::String &error) {
	const uint32 size = data.size();
	if (size < kHeaderSize) {
		error = Common::String::format("%s is too small", name.c_str());
		return nullptr;
	}

	Common::ScopedPtr<ScvmuniGlyphSource> src(new ScvmuniGlyphSource());
	src->_data = Common::move(data);
	const byte *d = &src->_data[0];

	if (memcmp(d, kMagic, 8) != 0) {
		error = Common::String::format("%s has a bad signature", name.c_str());
		return nullptr;
	}

	const uint16 version = READ_LE_UINT16(d + 8);
	const uint16 flags = READ_LE_UINT16(d + 10);
	if (version != kVersion) {
		error = Common::String::format("unsupported version %u", version);
		return nullptr;
	}

	src->_cellWidth = d[12];
	src->_cellHeight = d[13];
	src->_advanceNarrow = d[14];
	src->_advanceWide = d[15];
	src->_glyphCount = READ_LE_UINT32(d + 16);

	const uint32 cpOff = READ_LE_UINT32(d + 20);
	const uint32 wOff = READ_LE_UINT32(d + 24);
	const uint32 bmOff = READ_LE_UINT32(d + 28);

	if ((flags & 3) == 3) {
		error = Common::String::format("%s sets both 2bpp and 8bpp", name.c_str());
		return nullptr;
	}
	src->_bitsPerPixel = (flags & 2) ? 8 : ((flags & 1) ? 2 : 1);
	// A wide glyph spans two cells and every glyph uses the same stride, so
	// one row length serves both widths and the reader stays branch-free.
	src->_rowBytes = ((uint32)src->_cellWidth * 2 * src->_bitsPerPixel + 7) / 8;
	src->_bytesPerGlyph = src->_rowBytes * src->_cellHeight;

	if (src->_cellWidth == 0 || src->_cellHeight == 0 || src->_glyphCount == 0) {
		error = Common::String::format("%s declares an empty font", name.c_str());
		return nullptr;
	}

	// Every table must lie inside the file. Checked before any table is read
	// so a truncated or hostile bundle cannot walk off the buffer.
	if (cpOff > size || (uint64)src->_glyphCount * 4 > size - cpOff ||
		wOff > size || (uint64)src->_glyphCount > size - wOff ||
		bmOff > size || (uint64)src->_glyphCount * src->_bytesPerGlyph > size - bmOff) {
		error = Common::String::format("%s has a table that runs past the end", name.c_str());
		return nullptr;
	}

	src->_codepoints = d + cpOff;
	src->_widths = d + wOff;
	src->_bitmaps = d + bmOff;

	// The code point table must be sorted, because lookup is a binary search.
	// Verify once at load rather than trusting the producer.
	for (uint32 i = 1; i < src->_glyphCount; i++) {
		if (READ_LE_UINT32(src->_codepoints + i * 4) <=
			READ_LE_UINT32(src->_codepoints + (i - 1) * 4)) {
			error = Common::String::format("%s code point table is not sorted at %u",
											name.c_str(), i);
			return nullptr;
		}
	}

	return src.release();
}

int ScvmuniGlyphSource::findGlyph(uint32 cp) {
	if (_cacheValid && _cachedCp == cp)
		return _cachedIndex;

	int idx = -1;
	uint32 lo = 0, hi = _glyphCount;
	while (lo < hi) {
		const uint32 mid = lo + (hi - lo) / 2;
		const uint32 c = READ_LE_UINT32(_codepoints + mid * 4);
		if (c == cp) {
			idx = (int)mid;
			break;
		}
		if (c < cp)
			lo = mid + 1;
		else
			hi = mid;
	}

	_cachedCp = cp;
	_cachedIndex = idx;
	_cacheValid = true;
	return idx;
}

int ScvmuniGlyphSource::cells(uint32 cp) {
	const int g = findGlyph(cp);
	return g >= 0 ? _widths[g] : 0;
}

bool ScvmuniGlyphSource::metrics(uint32 cp, GlyphMetrics &m) {
	if (!UnicodeGlyphSource::metrics(cp, m))
		return false;
	if (!m.combining)
		m.advance = (cells(cp) >= 2) ? _advanceWide : _advanceNarrow;
	return true;
}

const byte *ScvmuniGlyphSource::row(uint32 cp, int y) {
	const int g = findGlyph(cp);
	if (g < 0)
		return _bitmaps;	// contract: only called when cells(cp) > 0
	return _bitmaps + (uint32)g * _bytesPerGlyph + (uint32)y * _rowBytes;
}

} // End of namespace Graphics
