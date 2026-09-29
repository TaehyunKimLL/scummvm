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
#include <pc.h>

#include "backends/graphics/dos/dos-graphics.h"
#include "backends/platform/dos/cursor-convert.h"
#include "backends/platform/dos/line-repeat.h"
#include "common/config-manager.h"
#include "common/debug.h"
#include "common/file.h"
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
	_modeIndex(-1), _lineRepeat(false), _vsync(false), _vsyncWarned(false), _lastInitW(0), _lastInitH(0), _shotCount(0),
	_window(nullptr), _screenChangeID(0), _pendingW(0), _pendingH(0),
	_overlayVisible(false), _paletteDirty(false), _shakeX(0), _shakeY(0),
	_fullDirty(false), _cursorW(0), _cursorH(0), _cursorHotX(0), _cursorHotY(0), _cursorKey(0),
	_cursorPaletteEnabled(false), _cursorFormatWarned(false), _cursorVisible(false), _mouseX(0), _mouseY(0) {
	memset(_palette, 0, sizeof(_palette));
	memset(_cursorPalette, 0, sizeof(_cursorPalette));
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
	// SCI asks before its initGraphics(640, 400), while the last initSize()
	// is still the launcher's 320x200: answer for 640x400 (exact or line
	// repeat) unless something larger was asked for. dos_truecolor=off
	// leaves CLUT8 only.
	const DOS::FormatsSize size = DOS::formatsSize(_lastInitW, _lastInitH);
	return DOS::supportedFormats(_modes, size.w, size.h, ConfMan.get("dos_truecolor") != "off");
}

void DosGraphicsManager::initSize(uint width, uint height, const Graphics::PixelFormat *format) {
	_pendingW = _lastInitW = width;
	_pendingH = _lastInitH = height;
	_pendingFormat = format ? *format : Graphics::PixelFormat::createFormatCLUT8();
}

bool DosGraphicsManager::setMode(int index, bool lineRepeat, uint srcW, uint srcH) {
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
	if (lineRepeat)
		debug(1, "DOS: mode %dx%d %s (line repeat from %ux%u)", s->w, s->h, _modes[index].format.toString().c_str(),
			  srcW, srcH);
	else
		debug(1, "DOS: mode %dx%d %s", s->w, s->h, _modes[index].format.toString().c_str());
	return true;
}

OSystem::TransactionError DosGraphicsManager::endGFXTransaction() {
	if (!_pendingW)
		return OSystem::kTransactionSuccess;
	const DOS::ModeChoice choice = DOS::chooseMode(_modes, _pendingW, _pendingH, _pendingFormat,
												   ConfMan.getBool("dos_force_fallback"));
	if (choice.index < 0) {
		warning("DosGraphicsManager: no %ux%u %s mode", _pendingW, _pendingH, _pendingFormat.toString().c_str());
		_pendingW = 0;
		return OSystem::kTransactionSizeChangeFailed;
	}
	if (!setMode(choice.index, choice.lineRepeat, _pendingW, _pendingH)) {
		_pendingW = 0;
		// The display may be switched already: back to the mode _screen
		// describes, or, failing that, nothing is drawn until the next
		// successful transaction.
		if (_modeIndex >= 0 && !setMode(_modeIndex, _lineRepeat, _screen.w, _screen.h)) {
			warning("DosGraphicsManager: cannot restore the previous mode either");
			_modeIndex = -1;
			_cursor.forget();
			_screen.free();
		}
		return OSystem::kTransactionSizeChangeFailed;
	}
	_modeIndex = choice.index;
	_lineRepeat = choice.lineRepeat;
	// Once the wait has timed out it stays off: the next mode has no
	// retrace bit either.
	_vsync = ConfMan.get("dos_vsync") == "wait" && !_vsyncWarned;
	// A new game may bring a cursor in another format: say so again.
	_cursorFormatWarned = false;
	// The old frame, cursor included, is gone; the full repaint below
	// draws the cursor afresh.
	_cursor.forget();
	_screen.free();
	_screen.create(_pendingW, _pendingH, _pendingFormat);
	_pendingW = 0;
	_paletteDirty = true;
	_fullDirty = true;
	++_screenChangeID;
	convertCursor();
	return OSystem::kTransactionSuccess;
}

void DosGraphicsManager::setPalette(const byte *colors, uint start, uint num) {
	memcpy(_palette + start * 3, colors, num * 3);
	_paletteDirty = true;
	if (!_cursorPaletteEnabled && _screen.format.bytesPerPixel > 1)
		convertCursor();
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
	if (!_screen.getPixels())	// no mode (see endGFXTransaction())
		return;
	_screen.copyRectToSurface(buf, pitch, x, y, w, h);
	addDirty(Common::Rect(x, y, x + w, y + h));
}

void DosGraphicsManager::fillScreen(uint32 col) {
	if (!_screen.getPixels())
		return;
	_screen.fillRect(Common::Rect(_screen.w, _screen.h), col);
	_fullDirty = true;
}

void DosGraphicsManager::fillScreen(const Common::Rect &r, uint32 col) {
	if (!_screen.getPixels())
		return;
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
	if (!s || !surfaceFits(s))
		return;

	// SDL's DOS driver programs the VGA DAC from the window surface's
	// palette, but SDL_CreateSurfaceFrom() gives that surface none: without
	// one, SDL_SetPaletteColors() fails and the DAC stays all black. The
	// surface is new after every mode change, so check each frame.
	if (s->format == SDL_PIXELFORMAT_INDEX8 && !SDL_GetSurfacePalette(s)) {
		if (!SDL_CreateSurfacePalette(s)) {
			warning("DosGraphicsManager: no palette for the window surface: %s", SDL_GetError());
			return;
		}
		_paletteDirty = true;
	}

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

	// Rectangles below are in window rows before line repeat: the mode's
	// own rows, or the 400 the 480 hold.
	const int windowH = _lineRepeat ? DOS::logicalRow(s->h - 1) + 1 : s->h;
	const Common::Rect window(s->w, windowH);
	if (_fullDirty) {
		// Shake shifts the whole picture; the uncovered strip is colour 0.
		memset(s->pixels, 0, s->pitch * s->h);
		Common::Rect dst(_screen.w, _screen.h);
		dst.translate(_shakeX, _shakeY);
		dst.clip(window);
		blit(s, dst);
		send.clear();
		send.push_back(Common::Rect(s->w, s->h));
	} else {
		for (uint i = 0; i < _dirty.size(); ++i) {
			Common::Rect r = _dirty[i];
			r.translate(_shakeX, _shakeY);
			r.clip(window);
			if (r.isEmpty())
				continue;
			blit(s, r);
			send.push_back(_lineRepeat ? DOS::physRect(r) : r);
		}
	}

	if (_cursorVisible && _cursor.hasImage()) {
		// At its physical position, not stretched: the hotspot row is the
		// first copy of the mouse's logical row.
		const int y = _lineRepeat ? DOS::physRow(_mouseY) : _mouseY;
		Common::Rect c = _cursor.draw((byte *)s->pixels, s->pitch, SDL_BYTESPERPIXEL(s->format), s->w, s->h, _mouseX, y);
		if (!c.isEmpty())
			send.push_back(c);
	}

	if (!send.empty() || _paletteDirty) {
		Common::Array<SDL_Rect> rects;
		for (uint i = 0; i < send.size(); ++i) {
			SDL_Rect r = { send[i].left, send[i].top, send[i].width(), send[i].height() };
			rects.push_back(r);
		}
		if (_vsync) {
			// Wait for the start of a vertical retrace (VGA input status 1,
			// bit 3). Each loop gives up after ~100000 port reads (tens of
			// ms on real hardware) in case the bit never toggles.
			// Without a VGA status register (bit 3 stuck) every frame
			// would pay the timeout, so after the first one stop waiting.
			uint32 n = 0;
			while ((inportb(0x3DA) & 8) && ++n < 100000) {}
			bool timedOut = n >= 100000;
			n = 0;
			while (!(inportb(0x3DA) & 8) && ++n < 100000) {}
			timedOut = timedOut || n >= 100000;
			if (timedOut) {
				_vsync = false;
				if (!_vsyncWarned) {
					warning("dos_vsync=wait: no retrace seen, waiting disabled");
					_vsyncWarned = true;
				}
			}
		}
		SDL_UpdateWindowSurfaceRects(_window, rects.empty() ? nullptr : &rects[0], (int)rects.size());
	}
	_dirty.clear();
	_fullDirty = false;
	_paletteDirty = false;
}

bool DosGraphicsManager::surfaceFits(const SDL_Surface *s) const {
	// After a failed mode change the display can be in another mode than
	// _screen describes; copying then would write past the surface or at
	// the wrong pixel size.
	if (_modeIndex < 0 || !_screen.getPixels())
		return false;
	const int windowH = _lineRepeat ? DOS::logicalRow(s->h - 1) + 1 : s->h;
	return SDL_BYTESPERPIXEL(s->format) == _screen.format.bytesPerPixel && s->w >= _screen.w && windowH >= _screen.h;
}

void DosGraphicsManager::blit(SDL_Surface *s, const Common::Rect &r) {
	// r is in window rows (see updateScreen()); the source is r less the shake.
	const int bpp = _screen.format.bytesPerPixel;
	const int bytes = r.width() * bpp;
	if (!_lineRepeat) {
		for (int y = r.top; y < r.bottom; ++y)
			memcpy((byte *)s->pixels + y * s->pitch + r.left * bpp, _screen.getBasePtr(r.left - _shakeX, y - _shakeY), bytes);
		return;
	}
	if (!_shakeX && !_shakeY) {
		DOS::copyRows((byte *)s->pixels, s->pitch, (const byte *)_screen.getPixels(), _screen.pitch, bpp, r);
		return;
	}
	// copyRows() maps rows of one surface to the same rows doubled; with a
	// shake the source rows are offset, so the same mapping row by row.
	for (int y = r.top; y < r.bottom; ++y) {
		const byte *src = (const byte *)_screen.getBasePtr(r.left - _shakeX, y - _shakeY);
		byte *dst = (byte *)s->pixels + DOS::physRow(y) * s->pitch + r.left * bpp;
		memcpy(dst, src, bytes);
		if (DOS::repeats(y))
			memcpy(dst + s->pitch, src, bytes);
	}
}

void DosGraphicsManager::saveScreenshot() {
	SDL_Surface *s = _window ? SDL_GetWindowSurface(_window) : nullptr;
	if (!s || _modeIndex < 0 || !surfaceFits(s)) {
		warning("DosGraphicsManager: no screen to save");
		return;
	}
	const Graphics::PixelFormat pf = _modes[_modeIndex].format;
	const Common::String base = Common::String::format("SHOT%04u", _shotCount++);
	Common::DumpFile f;
	if (!f.open(Common::Path(base + ".RAW"))) {
		warning("DosGraphicsManager: cannot write %s.RAW", base.c_str());
		return;
	}
	for (int y = 0; y < s->h; ++y)
		f.write((const byte *)s->pixels + y * s->pitch, s->w * pf.bytesPerPixel);
	f.close();
	if (f.open(Common::Path(base + ".TXT"))) {
		f.writeString(Common::String::format("%d %d %d %s\n", s->w, s->h, pf.bytesPerPixel * 8,
											 pf.bytesPerPixel == 1 ? "CLUT8" : pf.toString().c_str()));
		f.close();
	}
	if (pf.bytesPerPixel == 1 && f.open(Common::Path(base + ".PAL"))) {
		f.write(_palette, sizeof(_palette));
		f.close();
	}
	debug(1, "DOS: saved %s (%dx%d %s)", base.c_str(), s->w, s->h, pf.toString().c_str());
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
		SDL_WarpMouseInWindow(_window, (float)x, (float)(_lineRepeat ? DOS::physRow(y) : y));
}

Common::Point DosGraphicsManager::gameMouse(float wx, float wy) const {
	// The mode is the game's size, or its rows with every fifth repeated.
	const int y = (int)wy;
	return Common::Point((int16)wx, (int16)(_lineRepeat ? DOS::logicalRow(y) : y));
}

void DosGraphicsManager::setMouseCursor(const void *buf, uint w, uint h, int hotspotX, int hotspotY, uint32 keycolor,
										const Graphics::PixelFormat *format, const byte *mask, frac_t scaleX, frac_t scaleY) {
	_cursorFormat = format ? *format : Graphics::PixelFormat::createFormatCLUT8();
	_cursorW = w;
	_cursorH = h;
	_cursorHotX = hotspotX;
	_cursorHotY = hotspotY;
	_cursorKey = keycolor;
	_cursorSrc.resize(w * h * _cursorFormat.bytesPerPixel);
	if (!_cursorSrc.empty())
		memcpy(&_cursorSrc[0], buf, _cursorSrc.size());
	convertCursor();
}

void DosGraphicsManager::setCursorPalette(const byte *colors, uint start, uint num) {
	memcpy(_cursorPalette + start * 3, colors, num * 3);
	_cursorPaletteEnabled = true;
	convertCursor();
}

void DosGraphicsManager::setFeatureState(OSystem::Feature f, bool enable) {
	if (f != OSystem::kFeatureCursorPalette || enable == _cursorPaletteEnabled)
		return;
	_cursorPaletteEnabled = enable;
	convertCursor();
}

bool DosGraphicsManager::getFeatureState(OSystem::Feature f) const {
	return f == OSystem::kFeatureCursorPalette && _cursorPaletteEnabled;
}

void DosGraphicsManager::convertCursor() {
	// setMouseCursor(nullptr, 0, 0) (CursorMan with nothing left on its
	// stack) means no cursor: the last image must not stay drawn.
	if (!_cursorW || !_cursorH) {
		_cursor.clearImage();
		return;
	}
	if (!_screen.getPixels())
		return;
	const Graphics::PixelFormat &screen = _screen.format;
	// Only a CLUT8 cursor on true colour reads the palette: the cursor
	// palette when it is on, the game palette otherwise.
	const byte *pal = _cursorPaletteEnabled ? _cursorPalette : _palette;
	Common::Array<byte> image;
	uint32 key = 0;
	switch (DOS::cursorImage(&_cursorSrc[0], _cursorW, _cursorH, _cursorFormat, _cursorKey, screen, pal, image, key)) {
	case DOS::kCursorClear:
		_cursor.clearImage();
		break;
	case DOS::kCursorAsIs:
		_cursor.setImage(&_cursorSrc[0], _cursorW, _cursorH, _cursorHotX, _cursorHotY, _cursorKey, screen.bytesPerPixel);
		break;
	case DOS::kCursorConverted:
		_cursor.setImage(&image[0], _cursorW, _cursorH, _cursorHotX, _cursorHotY, key, screen.bytesPerPixel);
		break;
	case DOS::kCursorMismatch:
		// Nothing to show: the last image may be another pixel size than
		// this screen's (a true-colour cursor after a switch to CLUT8).
		_cursor.clearImage();
		if (!_cursorFormatWarned) {
			warning("DosGraphicsManager: cursor format %s differs from the screen's %s, not shown",
					_cursorFormat.toString().c_str(), screen.toString().c_str());
			_cursorFormatWarned = true;
		}
		break;
	}
}

#endif
