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

#include "common/scummsys.h"

#if defined(DOS_DJGPP)

#include <SDL3/SDL.h>

#include "backends/graphics/dos/dos-graphics.h"
#include "common/debug.h"
#include "common/textconsole.h"

static Graphics::PixelFormat fromSdl(SDL_PixelFormat f, bool &ok) {
	ok = true;
	switch (f) {
	case SDL_PIXELFORMAT_INDEX8: return Graphics::PixelFormat::createFormatCLUT8();
	case SDL_PIXELFORMAT_RGB565: return DOS::rgb565();
	case SDL_PIXELFORMAT_XRGB1555: return DOS::xrgb1555();
	case SDL_PIXELFORMAT_XRGB8888: return DOS::xrgb8888();
	default: ok = false; return Graphics::PixelFormat();
	}
}

// Past this many rectangles one full-screen update is cheaper than the list.
static const uint kMaxDirtyRects = 32;

DosGraphicsManager::DosGraphicsManager() :
	_window(nullptr), _screenChangeID(0), _pendingW(0), _pendingH(0),
	_overlayVisible(false), _paletteDirty(false), _shakeX(0), _shakeY(0),
	_fullDirty(false), _cursorVisible(false), _mouseX(0), _mouseY(0) {
	memset(_palette, 0, sizeof(_palette));
	int n = 0;
	SDL_DisplayMode **m = SDL_GetFullscreenDisplayModes(SDL_GetPrimaryDisplay(), &n);
	for (int i = 0; i < n; ++i) {
		bool ok;
		Graphics::PixelFormat f = fromSdl(m[i]->format, ok);
		if (!ok)
			continue;
		DOS::VideoMode vm = { (uint16)m[i]->w, (uint16)m[i]->h, f };
		_modes.push_back(vm);
		// A copy is all SDL_SetWindowFullscreenMode() needs; its internal
		// pointer belongs to the display, which outlives this manager.
		_sdlModes.push_back(*m[i]);
	}
	SDL_free(m);
	_overlay.create(640, 480, DOS::rgb565());
}

DosGraphicsManager::~DosGraphicsManager() {
	_screen.free();
	_overlay.free();
	if (_window)
		SDL_DestroyWindow(_window);
}

Common::List<Graphics::PixelFormat> DosGraphicsManager::getSupportedFormats() const {
	// SCI asks before initGraphics(); M0 answers for its hires size.
	return DOS::supportedFormats(_modes, 640, 400);
}

void DosGraphicsManager::initSize(uint width, uint height, const Graphics::PixelFormat *format) {
	_pendingW = width;
	_pendingH = height;
	_pendingFormat = format ? *format : Graphics::PixelFormat::createFormatCLUT8();
}

bool DosGraphicsManager::setMode(int index) {
	const SDL_DisplayMode &mode = _sdlModes[index];
	if (!_window)
		_window = SDL_CreateWindow("ScummVM", mode.w, mode.h, 0);
	if (!_window) {
		warning("DosGraphicsManager: SDL_CreateWindow: %s", SDL_GetError());
		return false;
	}
	if (!SDL_SetWindowFullscreenMode(_window, &mode) || !SDL_SetWindowFullscreen(_window, true)) {
		warning("DosGraphicsManager: mode %dx%d: %s", mode.w, mode.h, SDL_GetError());
		return false;
	}
	SDL_SyncWindow(_window);
	// updateScreen() copies rows straight into this surface: it must be
	// exactly the mode we asked for, or we write past it or in the wrong format.
	const SDL_Surface *s = SDL_GetWindowSurface(_window);
	if (!s) {
		warning("DosGraphicsManager: mode %dx%d: no window surface: %s", mode.w, mode.h, SDL_GetError());
		return false;
	}
	if (s->w != mode.w || s->h != mode.h || s->format != mode.format) {
		warning("DosGraphicsManager: asked for %dx%d %s, got a %dx%d %s surface", mode.w, mode.h,
				SDL_GetPixelFormatName(mode.format), s->w, s->h, SDL_GetPixelFormatName(s->format));
		return false;
	}
	debug(1, "DOS: mode %dx%d %s", s->w, s->h, _modes[index].format.toString().c_str());
	return true;
}

OSystem::TransactionError DosGraphicsManager::endGFXTransaction() {
	if (!_pendingW)
		return OSystem::kTransactionSuccess;
	const int index = DOS::findExactMode(_modes, _pendingW, _pendingH, _pendingFormat);
	if (index < 0) {
		warning("DosGraphicsManager: no %ux%u %s mode", _pendingW, _pendingH, _pendingFormat.toString().c_str());
		_pendingW = 0;
		return OSystem::kTransactionSizeChangeFailed;
	}
	if (!setMode(index)) {
		_pendingW = 0;
		return OSystem::kTransactionSizeChangeFailed;
	}
	// The old frame, cursor included, is gone; the full repaint below
	// draws the cursor afresh.
	_cursor.forget();
	_screen.free();
	_screen.create(_pendingW, _pendingH, _pendingFormat);
	_pendingW = 0;
	_paletteDirty = true;
	_fullDirty = true;
	++_screenChangeID;
	return OSystem::kTransactionSuccess;
}

void DosGraphicsManager::setPalette(const byte *colors, uint start, uint num) {
	memcpy(_palette + start * 3, colors, num * 3);
	_paletteDirty = true;
}

void DosGraphicsManager::grabPalette(byte *colors, uint start, uint num) const {
	memcpy(colors, _palette + start * 3, num * 3);
}

void DosGraphicsManager::addDirty(const Common::Rect &r) {
	if (_fullDirty || r.isEmpty())
		return;
	if (_dirty.size() >= kMaxDirtyRects) {
		_fullDirty = true;
		_dirty.clear();
		return;
	}
	_dirty.push_back(r);
}

void DosGraphicsManager::copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h) {
	_screen.copyRectToSurface(buf, pitch, x, y, w, h);
	addDirty(Common::Rect(x, y, x + w, y + h));
}

void DosGraphicsManager::fillScreen(uint32 col) {
	_screen.fillRect(Common::Rect(_screen.w, _screen.h), col);
	_fullDirty = true;
}

void DosGraphicsManager::fillScreen(const Common::Rect &r, uint32 col) {
	_screen.fillRect(r, col);
	addDirty(r);
}

void DosGraphicsManager::setShakePos(int shakeXOffset, int shakeYOffset) {
	if (shakeXOffset != _shakeX || shakeYOffset != _shakeY) {
		_shakeX = shakeXOffset;
		_shakeY = shakeYOffset;
		_fullDirty = true;
	}
}

void DosGraphicsManager::updateScreen() {
	if (!_window || !_screen.getPixels())
		return;
	SDL_Surface *s = SDL_GetWindowSurface(_window);
	if (!s)
		return;

	if (_paletteDirty && s->format == SDL_PIXELFORMAT_INDEX8) {
		SDL_Color c[256];
		for (int i = 0; i < 256; ++i) {
			c[i].r = _palette[i * 3];
			c[i].g = _palette[i * 3 + 1];
			c[i].b = _palette[i * 3 + 2];
			c[i].a = 255;
		}
		SDL_SetPaletteColors(SDL_GetSurfacePalette(s), c, 0, 256);
	}

	Common::Array<Common::Rect> send;
	Common::Rect back = _cursor.restore((byte *)s->pixels, s->pitch);
	if (!back.isEmpty())
		send.push_back(back);

	const int bpp = _screen.format.bytesPerPixel;
	if (_fullDirty) {
		// Shake shifts the whole picture; the uncovered strip is colour 0.
		memset(s->pixels, 0, s->pitch * s->h);
		Common::Rect src(_screen.w, _screen.h);
		Common::Rect dst = src;
		dst.translate(_shakeX, _shakeY);
		dst.clip(Common::Rect(s->w, s->h));
		for (int y = dst.top; y < dst.bottom; ++y)
			memcpy((byte *)s->pixels + y * s->pitch + dst.left * bpp,
				   _screen.getBasePtr(dst.left - _shakeX, y - _shakeY), dst.width() * bpp);
		send.clear();
		send.push_back(Common::Rect(s->w, s->h));
	} else {
		for (uint i = 0; i < _dirty.size(); ++i) {
			Common::Rect r = _dirty[i];
			r.translate(_shakeX, _shakeY);
			r.clip(Common::Rect(s->w, s->h));
			for (int y = r.top; y < r.bottom; ++y)
				memcpy((byte *)s->pixels + y * s->pitch + r.left * bpp,
					   _screen.getBasePtr(r.left - _shakeX, y - _shakeY), r.width() * bpp);
			send.push_back(r);
		}
	}

	if (_cursorVisible && _cursor.hasImage()) {
		Common::Rect c = _cursor.draw((byte *)s->pixels, s->pitch, s->w, s->h, _mouseX, _mouseY);
		if (!c.isEmpty())
			send.push_back(c);
	}

	if (!send.empty() || _paletteDirty) {
		Common::Array<SDL_Rect> rects;
		for (uint i = 0; i < send.size(); ++i) {
			SDL_Rect r = { send[i].left, send[i].top, send[i].width(), send[i].height() };
			rects.push_back(r);
		}
		SDL_UpdateWindowSurfaceRects(_window, rects.empty() ? nullptr : &rects[0], (int)rects.size());
	}
	_dirty.clear();
	_fullDirty = false;
	_paletteDirty = false;
}

void DosGraphicsManager::showOverlay(bool inGUI) {
	static bool warned = false;
	if (!warned) {
		warning("DosGraphicsManager: the GUI overlay is not shown before M4");
		warned = true;
	}
	_overlayVisible = true;
}

void DosGraphicsManager::clearOverlay() {
	_overlay.fillRect(Common::Rect(_overlay.w, _overlay.h), 0);
}

void DosGraphicsManager::grabOverlay(Graphics::Surface &surface) const {
	surface.copyFrom(_overlay);
}

void DosGraphicsManager::copyRectToOverlay(const void *buf, int pitch, int x, int y, int w, int h) {
	_overlay.copyRectToSurface(buf, pitch, x, y, w, h);
}

bool DosGraphicsManager::showMouse(bool visible) {
	const bool last = _cursorVisible;
	_cursorVisible = visible;
	return last;
}

void DosGraphicsManager::setMousePos(int x, int y) {
	_mouseX = x;
	_mouseY = y;
}

void DosGraphicsManager::warpMouse(int x, int y) {
	setMousePos(x, y);
	if (_window)
		SDL_WarpMouseInWindow(_window, (float)x, (float)y);
}

Common::Point DosGraphicsManager::gameMouse(float wx, float wy) const {
	// M0: the mode is the game's size, so window and game coordinates agree.
	return Common::Point((int16)wx, (int16)wy);
}

void DosGraphicsManager::setMouseCursor(const void *buf, uint w, uint h, int hotspotX, int hotspotY, uint32 keycolor,
										const Graphics::PixelFormat *format, const byte *mask, frac_t scaleX, frac_t scaleY) {
	const Graphics::PixelFormat f = format ? *format : Graphics::PixelFormat::createFormatCLUT8();
	if (f != _screen.format) {
		warning("DosGraphicsManager: cursor format %s differs from the screen's, ignored", f.toString().c_str());
		return;
	}
	_cursor.setImage((const byte *)buf, w, h, hotspotX, hotspotY, keycolor, f.bytesPerPixel);
}

#endif
