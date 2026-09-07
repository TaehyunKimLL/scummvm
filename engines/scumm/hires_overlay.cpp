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

#include "scumm/hires_overlay.h"

#include "common/algorithm.h"

namespace Scumm {

void HiResOverlay::create(int w, int h, bool withCoverage) {
	free();

	_index.create(w, h, Graphics::PixelFormat::createFormatCLUT8());
	if (withCoverage)
		_coverage.create(w, h, Graphics::PixelFormat::createFormatCLUT8());
}

void HiResOverlay::free() {
	dropState();
	_index.free();
	_coverage.free();
}

void HiResOverlay::clear(int top, int height, byte transparent) {
	if (!_index.getPixels())
		return;

	if (top < 0) {
		height += top;
		top = 0;
	}
	if (top >= _index.h)
		return;
	height = MIN(height, _index.h - top);
	if (height <= 0)
		return;

	byte *dst = (byte *)_index.getBasePtr(0, top);
	for (int y = 0; y < height; ++y) {
		memset(dst, transparent, _index.w);
		dst += _index.pitch;
	}

	// The coverage has to go with it: left behind, it would blend the shape
	// of the previous frame's glyphs into whatever is drawn next.
	if (_coverage.getPixels()) {
		byte *cov = (byte *)_coverage.getBasePtr(0, top);
		for (int y = 0; y < height; ++y) {
			memset(cov, 0, _coverage.w);
			cov += _coverage.pitch;
		}
	}
}

void HiResOverlay::clear(const Common::Rect &r, byte transparent) {
	if (!_index.getPixels())
		return;

	Common::Rect area(r);
	area.clip(Common::Rect(_index.w, _index.h));
	if (area.isEmpty())
		return;

	_index.fillRect(area, transparent);
	if (_coverage.getPixels())
		_coverage.fillRect(area, 0);
}

void HiResOverlay::saveState() {
	dropState();
	if (!_index.getPixels())
		return;

	_savedIndex.copyFrom(_index);
	if (_coverage.getPixels())
		_savedCoverage.copyFrom(_coverage);
}

void HiResOverlay::restoreState() {
	if (!_savedIndex.getPixels())
		return;

	if (_index.getPixels() && _index.w == _savedIndex.w && _index.h == _savedIndex.h)
		_index.copyFrom(_savedIndex);

	if (_coverage.getPixels() && _savedCoverage.getPixels() &&
		_coverage.w == _savedCoverage.w && _coverage.h == _savedCoverage.h)
		_coverage.copyFrom(_savedCoverage);

	dropState();
}

void HiResOverlay::dropState() {
	_savedIndex.free();
	_savedCoverage.free();
}

} // End of namespace Scumm
