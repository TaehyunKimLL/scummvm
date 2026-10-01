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

#include "backends/platform/dos/soft-cursor.h"
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
	/// Where a buffer of its own comes from: malloc() (freed by Surface::free()).
	typedef void *(*Allocator)(size_t bytes);

	GameScreen() : _direct(false), _alloc(&defaultAlloc) {}
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

	/**
	 * A buffer of its own again, holding the picture the window has now.
	 * False, and still in the window, if no memory is left for it.
	 */
	bool detach() {
		if (!_direct)
			return true;
		const int pitch = _s.w * _s.format.bytesPerPixel;
		byte *pixels = (byte *)_alloc((size_t)pitch * _s.h);
		if (!pixels)
			return false;
		copyRows(pixels, pitch, (const byte *)_s.getPixels(), _s.pitch, pitch, _s.h);
		_s.init(_s.w, _s.h, pitch, pixels, _s.format);
		_direct = false;
		return true;
	}

	/// For tests: where detach() gets its buffer.
	void setAllocator(Allocator alloc) { _alloc = alloc; }

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

	static void *defaultAlloc(size_t bytes) { return malloc(bytes); }

	Graphics::Surface _s;
	bool _direct;
	Allocator _alloc;
};

/**
 * Whether the frame can be the window surface: the mode set (@p haveMode)
 * shows it row for row (no @p lineRepeat), in its own @p sameFormat, in a
 * window of rows as long and at least as many (@p fits), with no @p shaking,
 * and the window shows the game (no @p loadingScreen).
 */
inline bool screenCanBeDirect(bool haveMode, bool lineRepeat, bool sameFormat, bool fits, bool shaking,
							  bool loadingScreen) {
	return haveMode && !lineRepeat && sameFormat && fits && !shaking && !loadingScreen;
}

/** The window surface as the frame sees it (DosGraphicsManager, or a test's array). */
struct FrameWindow {
	byte *pixels;
	int pitch;
	int w, h;	///< h: the rows the picture can use (logical rows under line repeat)
	Graphics::PixelFormat format;
	bool lineRepeat;
};

/**
 * The frame and when it is the window (DosGraphicsManager's state machine,
 * apart so that it is tested on its own).
 *
 * - A shake takes the frame into a buffer of its own at its first offset
 *   and keeps it there until the offset has stayed 0 for kShakeSettleFrames
 *   frames: the many returns to 0 within a shake cost nothing. With no
 *   memory for that buffer the frame stays in the window and is shown
 *   unshaken (shakeShown() false) until the shake ends.
 * - The loading screen, line repeat, a mode of another format or row length,
 *   and setForceBuffer() (dos_frame_buffer=true) keep a buffer.
 * - A window whose pixels moved under a direct frame (the surface made
 *   again) loses the picture: the frame is made again, cleared (kLost).
 *
 * Before the frame moves, the cursor's pixels are put back into the window
 * (and the rectangle added to @p cursorBack for sending): a frame taken out
 * must not keep the cursor, and one put in covers it.
 */
class FrameKeeper {
public:
	enum { kShakeSettleFrames = 30 };
	enum SyncResult {
		kSame,		///< nothing moved
		kMoved,		///< into or out of the window: repaint all of it
		kLost		///< made again, cleared: the engine must repaint (a screen change)
	};

	FrameKeeper() : _forceBuffer(false), _settle(0), _detachFailed(false) {}

	GameScreen &screen() { return _frame; }
	const GameScreen &screen() const { return _frame; }
	void setForceBuffer(bool force) { _forceBuffer = force; }
	bool forceBuffer() const { return _forceBuffer; }

	/** Whether the window shows the shake offset (false: a direct frame drawn unshaken). */
	bool shakeShown() const { return !_frame.direct(); }
	/** Frames left before a shaken frame may go back into the window. */
	int settle() const { return _settle; }

	bool canBeDirect(const FrameWindow *win, const Graphics::PixelFormat &f, int w, int h, bool loading,
					 bool shaking) const {
		const bool haveMode = win && win->pixels;
		return !_forceBuffer &&
			   screenCanBeDirect(haveMode, haveMode && win->lineRepeat, haveMode && win->format == f,
								 haveMode && win->w == w && win->h >= h, shaking || _settle > 0, loading);
	}

	/** A new frame, cleared: in the window if it can be. */
	void create(int16 w, int16 h, const Graphics::PixelFormat &f, const FrameWindow *win, bool loading,
				bool shaking) {
		if (canBeDirect(win, f, w, h, loading, shaking))
			_frame.createDirect(w, h, f, win->pixels, win->pitch);
		else
			_frame.createBuffer(w, h, f);
	}

	/** Whether a direct frame's pixels are no longer the window's. */
	bool lost(const FrameWindow *win) const {
		return _frame.direct() &&
			   (!win || win->pixels != _frame.surface().getPixels() || win->pitch != _frame.surface().pitch);
	}

	/**
	 * The frame into the window or out of it, as it can be now. @p frameTick
	 * once a frame (updateScreen()): the settle count after a shake runs on
	 * those.
	 */
	SyncResult sync(const FrameWindow *win, bool loading, int shakeX, int shakeY, bool frameTick, SoftCursor *cursor,
					Common::Rect *cursorBack) {
		if (!_frame.exists())
			return kSame;
		const bool shaking = shakeX || shakeY;
		if (shaking)
			_settle = kShakeSettleFrames;
		else if (frameTick && _settle > 0)
			--_settle;
		if (!shaking)
			_detachFailed = false;

		const Graphics::Surface &s = _frame.surface();
		if (lost(win)) {
			if (cursor)
				cursor->forget();
			const int16 w = s.w, h = s.h;
			const Graphics::PixelFormat f = s.format;
			_frame.free();
			create(w, h, f, win, loading, shaking);
			return kLost;
		}

		bool can = canBeDirect(win, s.format, s.w, s.h, loading, shaking);
		if (!can && _frame.direct() && _detachFailed)
			return kSame;	// no memory to shake with: shown unshaken
		if (_frame.direct() == can)
			return kSame;
		if (cursor && win) {
			const Common::Rect r = cursor->restore(win->pixels, win->pitch);
			if (cursorBack && !r.isEmpty()) {
				if (cursorBack->isEmpty())
					*cursorBack = r;
				else
					cursorBack->extend(r);
			}
		}
		if (can) {
			_frame.attach(win->pixels, win->pitch);
		} else if (!_frame.detach()) {
			_detachFailed = true;
			return kSame;
		}
		return kMoved;
	}

private:
	GameScreen _frame;
	bool _forceBuffer;
	int _settle;
	bool _detachFailed;
};

} // End of namespace DOS

#endif
