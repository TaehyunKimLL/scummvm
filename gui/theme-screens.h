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


#ifndef GUI_THEME_SCREENS_H
#define GUI_THEME_SCREENS_H

#include "graphics/managed_surface.h"
#include "graphics/pixelformat.h"

namespace GUI {

/**
 * The theme's screen and back buffer, both the overlay's size.
 *
 * Without release (the default) they are made whenever the size is set and
 * stay, as they always did. With release (gui_release_buffers) they exist
 * only while the GUI is shown: a size set while it is hidden is kept for
 * ensure(), and hidden() frees them. Callers draw and grab only while
 * ready().
 */
class ThemeScreens {
public:
	ThemeScreens(Graphics::ManagedSurface &screen, Graphics::ManagedSurface &backBuffer)
		: _screen(screen), _backBuffer(backBuffer), _w(0), _h(0) {}

	/** New size and format; made now unless release is wanted while not shown. */
	void reset(int16 w, int16 h, const Graphics::PixelFormat &format, bool shown, bool release) {
		_w = w;
		_h = h;
		_format = format;
		free();
		if (shown || !release)
			make();
	}

	/** Make them if they are not there (the GUI is about to be shown). */
	bool ensure() {
		if (!ready() && usable())
			make();
		return ready();
	}

	/** The GUI is hidden: free them when release is wanted. */
	void hidden(bool release) {
		if (release)
			free();
	}

	bool ready() const { return _screen.getPixels() && _backBuffer.getPixels(); }
	/** Whether a size to make them at is known. */
	bool usable() const { return _w > 0 && _h > 0; }
	int16 width() const { return _w; }
	int16 height() const { return _h; }

private:
	void make() {
		if (!usable())
			return;
		_screen.create(_w, _h, _format);
		_backBuffer.create(_w, _h, _format);
	}

	void free() {
		_screen.free();
		_backBuffer.free();
	}

	Graphics::ManagedSurface &_screen;
	Graphics::ManagedSurface &_backBuffer;
	int16 _w, _h;
	Graphics::PixelFormat _format;
};

} // End of namespace GUI

#endif
