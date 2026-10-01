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
		_coverage.create(w, h, true);
}

void HiResOverlay::free() {
	dropState();
	_index.free();
	_coverage.free();
	freeUnder();
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
	_coverage.fill(Common::Rect(0, top, _coverage.width(), top + height), 0);

	// And the outlines of the text that was there.
	clearUnder(Common::Rect(0, top, _index.w, top + height));
}

void HiResOverlay::clear(const Common::Rect &r, byte transparent) {
	if (!_index.getPixels())
		return;

	Common::Rect area(r);
	area.clip(Common::Rect(_index.w, _index.h));
	if (area.isEmpty())
		return;

	_index.fillRect(area, transparent);
	_coverage.fill(area, 0);
	clearUnder(area);
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
	_coverage.fill(clipped, 0);

	// A flat fill hides whatever outline was under the old glyphs as well.
	clearUnder(clipped);
}

void HiResOverlay::createCoverage(int w, int h) {
	if (!_index.getPixels())
		return;

	// Sized from the index plane: two planes of different sizes is exactly
	// the failure this class exists to prevent, so a disagreeing argument
	// loses rather than being honoured.
	(void)w;
	(void)h;
	_coverage.create(_index.w, _index.h, true);
}

void HiResOverlay::freeCoverage() {
	_coverage.free();
	_savedCoverage.free();
	freeUnder();
}

void HiResOverlay::createUnder() {
	if (!_coverage.exists())
		return;

	freeUnder();
	_underIndex.create(_index.w, _index.h, false);
	_underCoverage.create(_index.w, _index.h, true);
}

void HiResOverlay::freeUnder() {
	_underIndex.free();
	_underCoverage.free();
	_savedUnderIndex.free();
	_savedUnderCoverage.free();
}

void HiResOverlay::clearUnder(const Common::Rect &r) {
	if (!_underCoverage.exists())
		return;

	// Coverage is what the compositor reads; the index goes too so that a
	// saved state or a debugger dump shows nothing stale.
	_underCoverage.fill(r, 0);
	_underIndex.fill(r, 0);
}

void HiResOverlay::clearCoverage(int top, int height) {
	if (!_coverage.exists())
		return;

	if (top < 0) {
		height += top;
		top = 0;
	}
	if (top >= _coverage.height())
		return;
	height = MIN(height, _coverage.height() - top);
	if (height <= 0)
		return;

	_coverage.fill(Common::Rect(0, top, _coverage.width(), top + height), 0);
	clearUnder(Common::Rect(0, top, _coverage.width(), top + height));
}

void HiResOverlay::saveState() {
	dropState();
	if (!_index.getPixels())
		return;

	_savedIndex.copyFrom(_index);
	if (_coverage.exists())
		_savedCoverage.copyFrom(_coverage);
	if (_underCoverage.exists()) {
		_savedUnderIndex.copyFrom(_underIndex);
		_savedUnderCoverage.copyFrom(_underCoverage);
	}
}

void HiResOverlay::restoreState() {
	if (!_savedIndex.getPixels())
		return;

	if (_index.getPixels() && _index.w == _savedIndex.w && _index.h == _savedIndex.h)
		_index.copyFrom(_savedIndex);

	if (_coverage.exists() && _savedCoverage.exists() &&
		_coverage.width() == _savedCoverage.width() && _coverage.height() == _savedCoverage.height())
		_coverage.copyFrom(_savedCoverage);

	if (_underCoverage.exists() && _savedUnderCoverage.exists() &&
		_underCoverage.width() == _savedUnderCoverage.width() &&
		_underCoverage.height() == _savedUnderCoverage.height()) {
		_underIndex.copyFrom(_savedUnderIndex);
		_underCoverage.copyFrom(_savedUnderCoverage);
	} else if (_underCoverage.exists() && !_savedUnderCoverage.exists()) {
		// The planes arrived after the save (they are made on the first
		// decorated glyph): the state saved had no decoration at all.
		clearUnder(Common::Rect(_underCoverage.width(), _underCoverage.height()));
	}

	dropState();
}

void HiResOverlay::dropState() {
	_savedIndex.free();
	_savedCoverage.free();
	_savedUnderIndex.free();
	_savedUnderCoverage.free();
}

} // End of namespace Scumm
