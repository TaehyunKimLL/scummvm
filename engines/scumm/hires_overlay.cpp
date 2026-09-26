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

void HiResOverlay::fillIndices(const Common::Rect &r, byte index) {
	if (!_index.getPixels())
		return;

	Common::Rect clipped = r;
	clipped.clip(Common::Rect(_index.w, _index.h));
	if (clipped.isEmpty())
		return;

	_index.fillRect(clipped, index);

	// Not a mistake that this is zero rather than `index`: coverage says how
	// much of a pixel a glyph covers, and a flat fill covers all of it by
	// definition. Leaving the old values would blend the departed glyphs'
	// edges against the new colour.
	if (_coverage.getPixels())
		_coverage.fillRect(clipped, 0);
}

void HiResOverlay::createCoverage(int w, int h) {
	if (!_index.getPixels())
		return;

	// Sized from the index plane: two planes of different sizes is exactly
	// the failure this class exists to prevent, so a disagreeing argument
	// loses rather than being honoured.
	(void)w;
	(void)h;
	_coverage.free();
	_coverage.create(_index.w, _index.h, Graphics::PixelFormat::createFormatCLUT8());
	_coverage.fillRect(Common::Rect(0, 0, _index.w, _index.h), 0);
}

void HiResOverlay::freeCoverage() {
	_coverage.free();
	_savedCoverage.free();
}

void HiResOverlay::clearCoverage(int top, int height) {
	if (!_coverage.getPixels())
		return;

	if (top < 0) {
		height += top;
		top = 0;
	}
	if (top >= _coverage.h)
		return;
	height = MIN(height, _coverage.h - top);
	if (height <= 0)
		return;

	_coverage.fillRect(Common::Rect(0, top, _coverage.w, top + height), 0);
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
