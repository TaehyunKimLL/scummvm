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

#ifndef BACKENDS_PLATFORM_DOS_LAZY_OVERLAY_H
#define BACKENDS_PLATFORM_DOS_LAZY_OVERLAY_H

#include "backends/platform/dos/dos-modes.h"
#include "graphics/surface.h"

namespace DOS {

/**
 * The GUI overlay's pixels, there only while something uses them.
 *
 * Size and format are fixed and known without the pixels (the GUI asks
 * for them before it draws). The first get() allocates a cleared surface;
 * release() frees it, and the next get() starts cleared again. Before the
 * first get(), grab() hands out a cleared copy without allocating here.
 */
class LazyOverlay {
public:
	LazyOverlay(int16 w, int16 h, const Graphics::PixelFormat &format) : _w(w), _h(h), _format(format) {}
	~LazyOverlay() { release(); }

	int16 w() const { return _w; }
	int16 h() const { return _h; }
	const Graphics::PixelFormat &format() const { return _format; }
	bool allocated() const { return _surface.getPixels() != nullptr; }

	Graphics::Surface &get() {
		if (!allocated()) {
			// Surface::create() leaves the pixels cleared.
			_surface.create(_w, _h, _format);
		}
		return _surface;
	}

	void grab(Graphics::Surface &dst) const {
		if (allocated()) {
			dst.copyFrom(_surface);
		} else {
			dst.create(_w, _h, _format);
		}
	}

	void release() { _surface.free(); }

private:
	LazyOverlay(const LazyOverlay &);
	LazyOverlay &operator=(const LazyOverlay &);

	int16 _w, _h;
	Graphics::PixelFormat _format;
	Graphics::Surface _surface;
};

} // End of namespace DOS

#endif
