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

#include "sci/graphics/textlayer.h"

namespace Sci {

static_assert(sizeof(TextPixelFg) == 2, "TextPixelFg is saved raw into underbits");

TextLayer::TextLayer(uint16 width, uint16 height, uint16 scale)
	: _width(width), _height(height), _scale(scale), _any(false) {
	_bands.resize((height + kBandRows - 1) / kBandRows);
	for (uint i = 0; i < _bands.size(); i++)
		_bands[i] = nullptr;
	TextPixelFg none = { 0, 0 };
	_clearRow.resize(width);
	for (uint i = 0; i < _clearRow.size(); i++)
		_clearRow[i] = none;
	_rowFlags.resize(height);
	clear();
}

TextLayer::~TextLayer() {
	for (uint i = 0; i < _bands.size(); i++)
		delete[] _bands[i];
}

void TextLayer::clear() {
	for (uint i = 0; i < _bands.size(); i++) {
		delete[] _bands[i];
		_bands[i] = nullptr;
	}
	for (uint i = 0; i < _rowFlags.size(); i++)
		_rowFlags[i] = 0;
	_any = false;
}

uint32 TextLayer::memoryBytes() const {
	uint32 n = 0;
	for (uint i = 0; i < _bands.size(); i++)
		if (_bands[i])
			n += (uint32)kBandRows * _width * sizeof(TextPixelFg);
	return n;
}

TextPixelFg *TextLayer::rowForWrite(uint16 y) {
	TextPixelFg *&band = _bands[y / kBandRows];
	if (!band) {
		const uint32 n = (uint32)kBandRows * _width;
		band = new TextPixelFg[n];
		memset(band, 0, n * sizeof(TextPixelFg));
	}
	return band + (uint32)(y % kBandRows) * _width;
}

Common::Rect TextLayer::toHires(const Common::Rect &lowres) const {
	Common::Rect r(lowres.left * _scale, lowres.top * _scale, lowres.right * _scale, lowres.bottom * _scale);
	r.clip(Common::Rect(0, 0, _width, _height));
	return r;
}

void TextLayer::putGlyph(int16 hx, int16 hy, const byte *coverage, int16 w, int16 h, byte fgIndex,
						 const Common::Rect *clip) {
	Common::Rect bounds(0, 0, _width, _height);
	if (clip)
		bounds.clip(*clip);
	for (int16 gy = 0; gy < h; gy++) {
		const int y = hy + gy;
		if (y < bounds.top || y >= bounds.bottom)
			continue;
		TextPixelFg *dst = nullptr;
		for (int16 gx = 0; gx < w; gx++) {
			const byte c = coverage[gy * w + gx];
			const int x = hx + gx;
			if (!c || x < bounds.left || x >= bounds.right)
				continue;
			if (!dst)
				dst = rowForWrite(y);
			dst[x].fgIndex = fgIndex;
			dst[x].fgCoverage = c;
		}
		if (dst) {
			_rowFlags[y] = 1;
			_any = true;
		}
	}
}

void TextLayer::clearLowresRect(const Common::Rect &lowres) {
	if (!_any)
		return;
	const Common::Rect r = toHires(lowres);
	for (int y = r.top; y < r.bottom; y++) {
		if (!_bands[y / kBandRows])
			continue;
		TextPixelFg *p = rowForWrite(y);
		memset(p + r.left, 0, r.width() * sizeof(TextPixelFg));
	}
}

void TextLayer::swapIndicesLowresRect(const Common::Rect &lowres, byte a, byte b) {
	if (!_any || a == b)
		return;
	const Common::Rect r = toHires(lowres);
	for (int y = r.top; y < r.bottom; y++) {
		if (!rowHasText(y))
			continue;
		TextPixelFg *p = rowForWrite(y);
		for (int x = r.left; x < r.right; x++) {
			if (p[x].fgCoverage) {
				if (p[x].fgIndex == a)
					p[x].fgIndex = b;
				else if (p[x].fgIndex == b)
					p[x].fgIndex = a;
			}
		}
	}
}

void TextLayer::xorIndicesLowresRect(const Common::Rect &lowres, byte mask) {
	if (!_any || !mask)
		return;
	const Common::Rect r = toHires(lowres);
	for (int y = r.top; y < r.bottom; y++) {
		if (!rowHasText(y))
			continue;
		TextPixelFg *p = rowForWrite(y);
		for (int x = r.left; x < r.right; x++) {
			if (p[x].fgCoverage)
				p[x].fgIndex ^= mask;
		}
	}
}

bool TextLayer::covers(const Common::Rect &r) const {
	for (int y = r.top; y < r.bottom; y++) {
		if (!rowHasText(y))
			continue;
		const TextPixelFg *p = row(y);
		for (int x = r.left; x < r.right; x++)
			if (p[x].fgCoverage)
				return true;
	}
	return false;
}

uint32 TextLayer::saveSize(const Common::Rect &lowres) const {
	// Only the text there is: a box saved over a picture with no text on
	// it (most of them) costs one byte here, not two a hi-res pixel.
	const Common::Rect r = toHires(lowres);
	return covers(r) ? 1 + (uint32)r.width() * r.height() * sizeof(TextPixelFg) : 1;
}

void TextLayer::save(const Common::Rect &lowres, byte *&out) const {
	const Common::Rect r = toHires(lowres);
	const bool any = covers(r);
	*out++ = any ? 1 : 0;
	if (!any)
		return;
	for (int y = r.top; y < r.bottom; y++) {
		const uint32 n = r.width() * sizeof(TextPixelFg);
		memcpy(out, row(y) + r.left, n);
		out += n;
	}
}

void TextLayer::restore(const Common::Rect &lowres, const byte *&in) {
	const Common::Rect r = toHires(lowres);
	const byte flag = *in++;
	if (!flag) {
		clearLowresRect(lowres);
		return;
	}
	for (int y = r.top; y < r.bottom; y++) {
		const uint32 n = r.width() * sizeof(TextPixelFg);
		memcpy(rowForWrite(y) + r.left, in, n);
		in += n;
		_rowFlags[y] = 1;
	}
	_any = true;
}

} // End of namespace Sci
