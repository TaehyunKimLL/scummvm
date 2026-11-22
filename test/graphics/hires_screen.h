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

#ifndef TEST_GRAPHICS_HIRES_SCREEN_H
#define TEST_GRAPHICS_HIRES_SCREEN_H

#include "graphics/hires_text/text_compose.h"
#include "graphics/hires_text/text_plane.h"
#include "graphics/surface.h"

namespace HiResTest {

/**
 * A throwaway stand-in for an engine (P1 has none): a low-res CLUT8 game
 * frame with its palette and a TextPlane at its scale, presented the way an
 * upscaling driver will: the frame pixel-doubled, then the plane over it.
 */
class FakeScreen {
public:
	FakeScreen(int w, int h, int scale) : _scale(scale) {
		frame.create(w, h, Graphics::PixelFormat::createFormatCLUT8());
		memset(palette, 0, sizeof(palette));
		plane.create(w, h, scale);
		_big.create(w * scale, h * scale, Graphics::PixelFormat::createFormatCLUT8());
	}
	~FakeScreen() {
		frame.free();
		_big.free();
	}

	void setColor(byte index, byte r, byte g, byte b) {
		palette[index * 3] = r;
		palette[index * 3 + 1] = g;
		palette[index * 3 + 2] = b;
	}

	/** The frame at the plane's scale, the plane over it, in @p out (created in @p format). */
	bool present(Graphics::Surface &out, const Graphics::PixelFormat &format, Graphics::TextCompose::Mode mode) {
		for (int y = 0; y < _big.h; ++y)
			for (int x = 0; x < _big.w; ++x)
				*(byte *)_big.getBasePtr(x, y) = *(const byte *)frame.getBasePtr(x / _scale, y / _scale);
		out.create(_big.w, _big.h, format);
		const Common::Rect all(_big.w, _big.h);
		if (format.isCLUT8()) {
			out.copyFrom(_big);
			return Graphics::TextCompose::compositeRect(plane, all, out, nullptr, mode);
		}
		return Graphics::TextCompose::presentRect(plane, _big, all, out, palette, mode);
	}

	Graphics::Surface frame;
	byte palette[256 * 3];
	Graphics::TextPlane plane;

private:
	int _scale;
	Graphics::Surface _big;
};

} // End of namespace HiResTest

#endif
