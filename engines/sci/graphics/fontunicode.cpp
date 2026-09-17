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

#include "sci/graphics/fontunicode.h"
#include "sci/graphics/screen.h"

#include "common/file.h"
#include "common/textconsole.h"

namespace Sci {

static const char kMagic[8] = { 'S', 'C', 'V', 'M', 'U', 'N', 'I', 0 };
static const uint16 kVersion = 1;
static const uint32 kHeaderSize = 36;

GfxFontUnicode::GfxFontUnicode(GfxScreen *screen, GuiResourceId resourceId)
	: _screen(screen), _resourceId(resourceId), _loaded(false),
	  _codepoints(nullptr), _widths(nullptr), _bitmaps(nullptr),
	  _glyphCount(0), _cellWidth(0), _cellHeight(0),
	  _advanceNarrow(0), _advanceWide(0), _bitsPerPixel(1),
	  _rowBytes(0), _bytesPerGlyph(0) {
}

GfxFontUnicode::~GfxFontUnicode() {
}

bool GfxFontUnicode::load(const Common::String &filename) {
	Common::File f;
	if (!f.open(Common::Path(filename)))
		return false;

	const uint32 size = f.size();
	if (size < kHeaderSize) {
		warning("GfxFontUnicode: %s is too small", filename.c_str());
		return false;
	}

	_data.resize(size);
	if (f.read(&_data[0], size) != size) {
		warning("GfxFontUnicode: could not read %s", filename.c_str());
		_data.clear();
		return false;
	}
	const byte *d = &_data[0];

	if (memcmp(d, kMagic, 8) != 0) {
		warning("GfxFontUnicode: %s has a bad signature", filename.c_str());
		_data.clear();
		return false;
	}

	const uint16 version = READ_LE_UINT16(d + 8);
	const uint16 flags = READ_LE_UINT16(d + 10);
	if (version != kVersion) {
		warning("GfxFontUnicode: unsupported version %u", version);
		_data.clear();
		return false;
	}

	_cellWidth = d[12];
	_cellHeight = d[13];
	_advanceNarrow = d[14];
	_advanceWide = d[15];
	_glyphCount = READ_LE_UINT32(d + 16);

	const uint32 cpOff = READ_LE_UINT32(d + 20);
	const uint32 wOff = READ_LE_UINT32(d + 24);
	const uint32 bmOff = READ_LE_UINT32(d + 28);

	_bitsPerPixel = (flags & 1) ? 2 : 1;
	// A wide glyph spans two cells and every glyph uses the same stride, so
	// one row length serves both widths and the reader stays branch-free.
	_rowBytes = ((uint32)_cellWidth * 2 * _bitsPerPixel + 7) / 8;
	_bytesPerGlyph = _rowBytes * _cellHeight;

	if (_cellWidth == 0 || _cellHeight == 0 || _glyphCount == 0) {
		warning("GfxFontUnicode: %s declares an empty font", filename.c_str());
		_data.clear();
		return false;
	}

	// Every table must lie inside the file. Checked before any table is read
	// so a truncated or hostile bundle cannot walk off the buffer.
	if (cpOff > size || (uint64)_glyphCount * 4 > size - cpOff ||
		wOff > size || (uint64)_glyphCount > size - wOff ||
		bmOff > size || (uint64)_glyphCount * _bytesPerGlyph > size - bmOff) {
		warning("GfxFontUnicode: %s has a table that runs past the end",
				filename.c_str());
		_data.clear();
		return false;
	}

	_codepoints = d + cpOff;
	_widths = d + wOff;
	_bitmaps = d + bmOff;

	// The code point table must be sorted, because lookup is a binary search.
	// Verify once at load rather than trusting the producer.
	for (uint32 i = 1; i < _glyphCount; i++) {
		if (READ_LE_UINT32(_codepoints + i * 4) <=
			READ_LE_UINT32(_codepoints + (i - 1) * 4)) {
			warning("GfxFontUnicode: %s code point table is not sorted at %u",
					filename.c_str(), i);
			_data.clear();
			_codepoints = _widths = _bitmaps = nullptr;
			return false;
		}
	}

	_loaded = true;
	debug(1, "GfxFontUnicode: %s loaded, %u glyphs, %dx%d, %dbpp",
		  filename.c_str(), _glyphCount, _cellWidth, _cellHeight, _bitsPerPixel);
	return true;
}

int GfxFontUnicode::findGlyph(uint32 codepoint) const {
	if (!_loaded)
		return -1;

	uint32 lo = 0, hi = _glyphCount;
	while (lo < hi) {
		const uint32 mid = lo + (hi - lo) / 2;
		const uint32 cp = READ_LE_UINT32(_codepoints + mid * 4);
		if (cp == codepoint)
			return (int)mid;
		if (cp < codepoint)
			lo = mid + 1;
		else
			hi = mid;
	}
	return -1;
}

bool GfxFontUnicode::pixelSet(int glyph, int x, int y) const {
	const byte *row = _bitmaps + (uint32)glyph * _bytesPerGlyph + (uint32)y * _rowBytes;
	if (_bitsPerPixel == 1)
		return (row[x >> 3] & (0x80 >> (x & 7))) != 0;
	return ((row[x >> 2] >> (6 - ((x & 3) * 2))) & 3) != 0;
}

bool GfxFontUnicode::isDoubleByte(uint16 chr) {
	const int g = findGlyph(chr);
	return g >= 0 && _widths[g] == 2;
}

byte GfxFontUnicode::getCharWidth(uint16 chr) {
	const int g = findGlyph(chr);
	if (g < 0)
		return 0;
	return _widths[g] == 2 ? _advanceWide : _advanceNarrow;
}

byte GfxFontUnicode::getCharHeight(uint16 chr) {
	return findGlyph(chr) >= 0 ? _cellHeight : 0;
}

void GfxFontUnicode::draw(uint16 chr, int16 top, int16 left, byte color,
						  bool greyedOutput) {
	const int g = findGlyph(chr);
	if (g < 0)
		return;

	const int cells = _widths[g];
	const int w = _cellWidth * cells;

	for (int y = 0; y < _cellHeight; y++) {
		for (int x = 0; x < w; x++) {
			if (!pixelSet(g, x, y))
				continue;
			// Greying is the engine's existing checkerboard convention: skip
			// every other pixel so the glyph reads as disabled.
			if (greyedOutput && ((top + y) % 2) == ((left + x) % 2))
				continue;
			_screen->putFontPixel(top, left + x, y, color);
		}
	}
}

void GfxFontUnicode::drawToBuffer(uint16 chr, int16 top, int16 left, byte color,
								  bool greyedOutput, byte *buffer,
								  int16 width, int16 height) {
	const int g = findGlyph(chr);
	if (g < 0)
		return;

	const int cells = _widths[g];
	const int w = _cellWidth * cells;

	for (int y = 0; y < _cellHeight; y++) {
		const int destY = top + y;
		if (destY < 0 || destY >= height)
			continue;
		for (int x = 0; x < w; x++) {
			const int destX = left + x;
			if (destX < 0 || destX >= width)
				continue;
			if (!pixelSet(g, x, y))
				continue;
			if (greyedOutput && (destY % 2) == (destX % 2))
				continue;
			buffer[destY * width + destX] = color;
		}
	}
}

} // End of namespace Sci
