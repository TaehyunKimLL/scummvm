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

#ifndef BACKENDS_PLATFORM_DOS_GAME_SCREEN_H
#define BACKENDS_PLATFORM_DOS_GAME_SCREEN_H

#include "common/rect.h"
#include "graphics/surface.h"

namespace DOS {

/**
 * The game's frame: a buffer of its own, or the window surface itself.
 *
 * In the window surface (direct) there is no second copy of the frame: the
 * game's writes land where SDL3 sends rows to the screen from. A buffer of
 * its own is kept only while the window cannot hold the frame as it is: 640
 * rows repeated into 480 (line-repeat.h), a shake (the window shows the
 * frame shifted), the loading screen (the window shows that instead), no
 * mode yet, or a mode of another pixel layout.
 *
 * surface() is the frame either way. Its pixels belong to this object only
 * as a buffer; free() frees them only then. Moving between the two forms
 * keeps the picture.
 */
class GameScreen {
public:
	GameScreen() : _direct(false) {}
	~GameScreen() { free(); }

	Graphics::Surface &surface() { return _s; }
	const Graphics::Surface &surface() const { return _s; }
	bool exists() const { return _s.getPixels() != nullptr; }
	/// Whether the frame is the window surface's pixels.
	bool direct() const { return _direct; }

	/** A cleared buffer of its own. */
	void createBuffer(int16 w, int16 h, const Graphics::PixelFormat &f) {
		free();
		_s.create(w, h, f);
	}

	/**
	 * The frame at the top left of @p pixels (rows @p pitch bytes apart),
	 * cleared, without a buffer.
	 */
	void createDirect(int16 w, int16 h, const Graphics::PixelFormat &f, byte *pixels, int pitch) {
		free();
		_s.init(w, h, pitch, pixels, f);
		_direct = true;
		for (int y = 0; y < h; ++y)
			memset(pixels + y * pitch, 0, w * f.bytesPerPixel);
	}

	/** No frame. A buffer is freed; the window's pixels are left alone. */
	void free() {
		if (!_direct)
			_s.free();
		_s = Graphics::Surface();
		_direct = false;
	}

	/** The frame's picture into @p pixels, and the frame there from now on; the buffer goes. */
	void attach(byte *pixels, int pitch) {
		if (_direct || !exists())
			return;
		copyRows(pixels, pitch, (const byte *)_s.getPixels(), _s.pitch, _s.w * _s.format.bytesPerPixel, _s.h);
		const int16 w = _s.w, h = _s.h;
		const Graphics::PixelFormat f = _s.format;
		_s.free();
		_s.init(w, h, pitch, pixels, f);
		_direct = true;
	}

	/** A buffer of its own again, holding the picture the window has now. */
	void detach() {
		if (!_direct)
			return;
		Graphics::Surface buffer;
		buffer.create(_s.w, _s.h, _s.format);
		copyRows((byte *)buffer.getPixels(), buffer.pitch, (const byte *)_s.getPixels(), _s.pitch,
				 _s.w * _s.format.bytesPerPixel, _s.h);
		_s = buffer;
		_direct = false;
	}

	/** Bytes of its own: the buffer's, 0 when direct. */
	uint32 ownBytes() const { return (_direct || !exists()) ? 0 : (uint32)_s.pitch * _s.h; }

	static void copyRows(byte *dst, int dstPitch, const byte *src, int srcPitch, int bytes, int rows) {
		for (int y = 0; y < rows; ++y)
			memcpy(dst + y * dstPitch, src + y * srcPitch, bytes);
	}

	/**
	 * Clear what a @p sw x @p sh window of @p bpp bytes a pixel has outside
	 * the @p w x @p h frame at its top left.
	 */
	static void clearOutside(byte *pixels, int pitch, int sw, int sh, int w, int h, int bpp) {
		for (int y = 0; y < sh; ++y) {
			if (y >= h)
				memset(pixels + y * pitch, 0, sw * bpp);
			else if (sw > w)
				memset(pixels + y * pitch + w * bpp, 0, (sw - w) * bpp);
		}
	}

private:
	GameScreen(const GameScreen &);
	GameScreen &operator=(const GameScreen &);

	Graphics::Surface _s;
	bool _direct;
};

/**
 * Whether the frame can be the window surface: the mode set (@p haveMode)
 * shows it row for row (no @p lineRepeat), in its own @p sameFormat, in a
 * window at least as large (@p fits), with no @p shaking, and the window
 * shows the game (no @p loadingScreen).
 */
inline bool screenCanBeDirect(bool haveMode, bool lineRepeat, bool sameFormat, bool fits, bool shaking,
							  bool loadingScreen) {
	return haveMode && !lineRepeat && sameFormat && fits && !shaking && !loadingScreen;
}

} // End of namespace DOS

#endif
