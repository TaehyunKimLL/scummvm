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

#include "graphics/hires_text/text_plane.h"

#include "common/util.h"

namespace Graphics {

bool TextPlane::create(int lowW, int lowH, int scale) {
	free();
	if (lowW <= 0 || lowH <= 0 || scale < 1 || scale > 4)
		return false;
	_scale = scale;
	_index.create(lowW * scale, lowH * scale, false);
	_coverage.create(lowW * scale, lowH * scale, true);
	return true;
}

void TextPlane::free() {
	_index.free();
	_coverage.free();
	_scale = 0;
}

void TextPlane::plot(int x, int y, byte coverage, byte index) {
	if (!coverage || x < 0 || y < 0 || x >= getWidth() || y >= getHeight())
		return;
	const byte old = _coverage.get(x, y);
	if (old && _index.get(x, y) == index && old > coverage)
		return;
	_index.set(x, y, index);
	_coverage.set(x, y, coverage);
}

void TextPlane::clear() {
	_index.clear();
	_coverage.clear();
}

void TextPlane::clearRect(const Common::Rect &hires) {
	_index.fill(hires, 0);
	_coverage.fill(hires, 0);
}

Common::Rect TextPlane::toHires(const Common::Rect &lowres) const {
	Common::Rect r(lowres.left * _scale, lowres.top * _scale, lowres.right * _scale, lowres.bottom * _scale);
	r.clip(Common::Rect(getWidth(), getHeight()));
	return r;
}

void TextPlane::xorIndicesLowresRect(const Common::Rect &lowres, byte mask) {
	const Common::Rect r = toHires(lowres);
	if (!mask || r.isEmpty())
		return;
	for (int y = r.top; y < r.bottom; ++y) {
		if (!rowHasText(y))
			continue;
		for (int x = r.left; x < r.right; ++x)
			if (_coverage.get(x, y))
				_index.set(x, y, _index.get(x, y) ^ mask);
	}
}

void TextPlane::swapIndicesLowresRect(const Common::Rect &lowres, byte a, byte b) {
	const Common::Rect r = toHires(lowres);
	if (a == b || r.isEmpty())
		return;
	for (int y = r.top; y < r.bottom; ++y) {
		if (!rowHasText(y))
			continue;
		for (int x = r.left; x < r.right; ++x) {
			if (!_coverage.get(x, y))
				continue;
			const byte i = _index.get(x, y);
			if (i == a)
				_index.set(x, y, b);
			else if (i == b)
				_index.set(x, y, a);
		}
	}
}

bool TextPlane::covers(const Common::Rect &r) const {
	for (int y = r.top; y < r.bottom; ++y) {
		if (!rowHasText(y))
			continue;
		for (int x = r.left; x < r.right; ++x)
			if (_coverage.get(x, y))
				return true;
	}
	return false;
}

uint32 TextPlane::saveSize(const Common::Rect &lowres) const {
	const Common::Rect r = toHires(lowres);
	return covers(r) ? 1 + 2 * (uint32)r.width() * r.height() : 1;
}

void TextPlane::save(const Common::Rect &lowres, byte *&out) const {
	const Common::Rect r = toHires(lowres);
	const bool any = covers(r);
	*out++ = any ? 1 : 0;
	if (!any)
		return;
	for (int y = r.top; y < r.bottom; ++y) {
		_index.readBytes((uint32)y * getWidth() + r.left, out, r.width());
		out += r.width();
		_coverage.readBytes((uint32)y * getWidth() + r.left, out, r.width());
		out += r.width();
	}
}

void TextPlane::restore(const Common::Rect &lowres, const byte *&in) {
	const Common::Rect r = toHires(lowres);
	if (!*in++) {
		clearRect(r);
		return;
	}
	for (int y = r.top; y < r.bottom; ++y) {
		_index.writeBytes((uint32)y * getWidth() + r.left, in, r.width());
		in += r.width();
		_coverage.writeBytes((uint32)y * getWidth() + r.left, in, r.width());
		in += r.width();
	}
}

bool TextPlane::getRow(int y, int x0, int n, byte *indexScratch, byte *coverageScratch,
                       const byte *&index, const byte *&coverage) const {
	if (y < 0 || y >= getHeight() || !rowHasText(y))
		return false;
	coverage = _coverage.row(y, coverageScratch, x0, n);
	index = _index.row(y, indexScratch, x0, n);
	if (!index) {
		memset(indexScratch, 0, n);
		index = indexScratch;
	}
	return coverage != nullptr;
}

} // End of namespace Graphics
