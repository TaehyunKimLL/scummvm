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

static_assert(sizeof(TextPixel) == 4, "TextPixel is saved raw into underbits");

TextLayer::TextLayer(uint16 width, uint16 height, uint16 scale)
	: _width(width), _height(height), _scale(scale), _any(false) {
	_pixels.resize((uint32)width * height);
	_rowFlags.resize(height);
	clear();
}

void TextLayer::clear() {
	TextPixel none = { 0, 0, 0, 0 };
	for (uint i = 0; i < _pixels.size(); i++)
		_pixels[i] = none;
	for (uint i = 0; i < _rowFlags.size(); i++)
		_rowFlags[i] = 0;
	_any = false;
}

Common::Rect TextLayer::toHires(const Common::Rect &lowres) const {
	Common::Rect r(lowres.left * _scale, lowres.top * _scale, lowres.right * _scale, lowres.bottom * _scale);
	r.clip(Common::Rect(0, 0, _width, _height));
	return r;
}

void TextLayer::putGlyph(int16 hx, int16 hy, const byte *coverage, int16 w, int16 h, byte fgIndex) {
	for (int16 gy = 0; gy < h; gy++) {
		const int y = hy + gy;
		if (y < 0 || y >= _height)
			continue;
		TextPixel *dst = &_pixels[(uint32)y * _width];
		bool wrote = false;
		for (int16 gx = 0; gx < w; gx++) {
			const byte c = coverage[gy * w + gx];
			const int x = hx + gx;
			if (!c || x < 0 || x >= _width)
				continue;
			dst[x].fgIndex = fgIndex;
			dst[x].fgCoverage = c;
			wrote = true;
		}
		if (wrote) {
			_rowFlags[y] = 1;
			_any = true;
		}
	}
}

void TextLayer::clearLowresRect(const Common::Rect &lowres) {
	if (!_any)
		return;
	const Common::Rect r = toHires(lowres);
	TextPixel none = { 0, 0, 0, 0 };
	for (int y = r.top; y < r.bottom; y++)
		for (int x = r.left; x < r.right; x++)
			_pixels[(uint32)y * _width + x] = none;
}

void TextLayer::swapIndicesLowresRect(const Common::Rect &lowres, byte a, byte b) {
	if (!_any || a == b)
		return;
	const Common::Rect r = toHires(lowres);
	for (int y = r.top; y < r.bottom; y++) {
		if (!rowHasText(y))
			continue;
		TextPixel *p = &_pixels[(uint32)y * _width];
		for (int x = r.left; x < r.right; x++) {
			if (p[x].fgCoverage) {
				if (p[x].fgIndex == a)
					p[x].fgIndex = b;
				else if (p[x].fgIndex == b)
					p[x].fgIndex = a;
			}
			if (p[x].outlineCoverage) {
				if (p[x].outlineIndex == a)
					p[x].outlineIndex = b;
				else if (p[x].outlineIndex == b)
					p[x].outlineIndex = a;
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
		TextPixel *p = &_pixels[(uint32)y * _width];
		for (int x = r.left; x < r.right; x++) {
			if (p[x].fgCoverage)
				p[x].fgIndex ^= mask;
			if (p[x].outlineCoverage)
				p[x].outlineIndex ^= mask;
		}
	}
}

uint32 TextLayer::saveSize(const Common::Rect &lowres) const {
	const Common::Rect r = toHires(lowres);
	return 1 + (uint32)r.width() * r.height() * sizeof(TextPixel);
}

void TextLayer::save(const Common::Rect &lowres, byte *&out) const {
	const Common::Rect r = toHires(lowres);
	bool any = false;
	for (int y = r.top; y < r.bottom && !any; y++)
		any = rowHasText(y);
	*out++ = any ? 1 : 0;
	if (!any)
		return;
	for (int y = r.top; y < r.bottom; y++) {
		const uint32 n = r.width() * sizeof(TextPixel);
		memcpy(out, &_pixels[(uint32)y * _width + r.left], n);
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
		const uint32 n = r.width() * sizeof(TextPixel);
		memcpy(&_pixels[(uint32)y * _width + r.left], in, n);
		in += n;
		_rowFlags[y] = 1;
	}
	_any = true;
}

} // End of namespace Sci
