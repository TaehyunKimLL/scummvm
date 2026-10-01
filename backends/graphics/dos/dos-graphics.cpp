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
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <sys/movedata.h>
#include <sys/nearptr.h>

#include "backends/graphics/dos/dos-graphics.h"
#include "backends/platform/dos/cursor-convert.h"
#include "backends/platform/dos/dos.h"
#include "backends/platform/dos/dos-heap.h"
#include "backends/platform/dos/dos-irq.h"
#include "backends/platform/dos/dos-loading.h"
#include "backends/platform/dos/line-repeat.h"
#include "backends/platform/dos/loading-caption.h"
#include "backends/platform/dos/loading-screen.h"
#include "common/config-manager.h"
#include "common/debug.h"
#include "common/file.h"
#include "common/textconsole.h"
#include "engines/engine.h"
#include "gui/debugger.h"

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

static void unlockRegion(uint32 &addr, uint32 &size);

// Past this many rectangles one full-screen update is cheaper than the list.
static const uint kMaxDirtyRects = 32;

// The graphics loading screen gives way to the game after this long even
// if the game still shows nothing but black.
static const uint32 kLoadingTimeoutMs = 15000;
// Sending the screen costs a whole frame (SDL3 sends all of it when the
// pitches match): the bar moves at most this often.
static const uint32 kLoadingRedrawMs = 120;
// Colour indices the loading screen takes in CLUT8 (the game's palette is
// not shown until the game is): the top eight, rarely lit in a game's
// first frame, so the switch shows no stray colours.
static const int kLoadingFirstIndex = 248;

enum {
	kLoadBg,
	kLoadFrame,
	kLoadFill,
	kLoadText,
	kLoadDim,
	kLoadAlert,
	kLoadColorCount
};

static const byte kLoadRgb[kLoadColorCount][3] = {
	{   0,   0,   0 },
	{ 112, 112, 136 },
	{  64, 140, 232 },
	{ 255, 255, 255 },
	{ 168, 168, 168 },
	{ 232,  72,  72 }
};

DosGraphicsManager::DosGraphicsManager() :
	_modeIndex(-1), _lineRepeat(false), _vsync(false), _vsyncWarned(false), _lastInitW(0), _lastInitH(0), _shotCount(0),
	_window(nullptr), _screenChangeID(0), _pendingW(0), _pendingH(0), _screen(_frame.screen().surface()),
	_overlay(640, 480, DOS::rgb565()), _overlayVisible(false), _paletteDirty(false), _shakeX(0), _shakeY(0),
	_fullDirty(false), _cursorW(0), _cursorH(0), _cursorHotX(0), _cursorHotY(0), _cursorKey(0),
	_cursorPaletteEnabled(false), _cursorFormatWarned(false), _cursorVisible(false), _mouseX(0), _mouseY(0),
	_deferModes(false), _engineStarted(false), _modeOwed(false), _loadingShown(false), _loadingAbort(false),
	_loadingSawUpdate(false), _loadingLitSeen(false), _loadingStart(0), _loadingLastDraw(0), _loadingLastFill(-1), _loadingLastLabel(nullptr),
	_loadingDraws(0), _loadingDrawMs(0), _loadingCheckMs(0), _loadingChecks(0),
	_unlockAfterPresent(false), _loadingSwallowUp(Common::EVENT_INVALID),
	_vramOk(false), _vramGran(0), _vramWinSize(0), _vramBase(0), _vramPitch(0) {
	memset(_palette, 0, sizeof(_palette));
	memset(_usedColors, 0, sizeof(_usedColors));
	memset(_lockAddr, 0, sizeof(_lockAddr));
	memset(_lockSize, 0, sizeof(_lockSize));
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
}

DosGraphicsManager::~DosGraphicsManager() {
	lockSurfaces(false);
	_frame.screen().free();
	_overlay.release();
	if (_window)
		SDL_DestroyWindow(_window);
}

Common::List<Graphics::PixelFormat> DosGraphicsManager::getSupportedFormats() const {
	// SCI asks before its initGraphics(640, 400), while the last initSize()
	// is still the launcher's 320x200: answer for 640x400 (exact or line
	// repeat) unless something larger was asked for. While a game runs,
	// render_target (game domain, then [scummvm]) puts its own family first,
	// then the other true-colour one, then CLUT8 (DOS::supportedFormats());
	// the launcher and its options dialogs see every format, so the options
	// can offer them all.
	const DOS::FormatsSize size = DOS::formatsSize(_lastInitW, _lastInitH);
	const Common::String value = ConfMan.get("render_target");
	bool invalid = false;
	const Graphics::HiResRenderTarget cap = DOS::renderTargetCap(ConfMan.getActiveDomain() != nullptr, value, invalid);
	if (invalid) {
		// Asked for often (engines, videos, the options): say it once per
		// value while a game runs; engineStopped() forgets it.
		if (_renderTargetWarned != value) {
			_renderTargetWarned = value;
			warning("DOS: render_target '%s' is not auto, clut8, rgb565 or rgb888; using auto", value.c_str());
		}
	}
	return DOS::supportedFormats(_modes, size.w, size.h, cap);
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

OSystem::TransactionError DosGraphicsManager::switchMode(uint w, uint h, const Graphics::PixelFormat &f) {
	lockSurfaces(false);	// the surfaces go with the mode
	const DOS::ModeChoice choice = DOS::chooseMode(_modes, w, h, f, ConfMan.getBool("dos_force_fallback"));
	if (choice.index < 0) {
		warning("DosGraphicsManager: no %ux%u %s mode", w, h, f.toString().c_str());
		return OSystem::kTransactionSizeChangeFailed;
	}
	// A frame in the window surface goes with it: the frame that follows a
	// switch is a new one, cleared. Only a failed switch below wants the old
	// one back, which it then gets cleared.
	const int16 oldW = _screen.w, oldH = _screen.h;
	const Graphics::PixelFormat oldFormat = _screen.format;
	const bool hadDirect = _frame.screen().direct();
	if (hadDirect) {
		_cursor.forget();
		_frame.screen().free();
	}
	// dos_frame_buffer=true: the frame stays a buffer of its own, copied into
	// the window, as before it could be the window.
	_frame.setForceBuffer(ConfMan.getBool("dos_frame_buffer"));
	if (!setMode(choice.index, choice.lineRepeat, w, h)) {
		// The display may be switched already: back to the mode _screen
		// describes, or, failing that, nothing is drawn until the next
		// successful transaction.
		if (_modeIndex >= 0 && !setMode(_modeIndex, _lineRepeat, oldW, oldH)) {
			warning("DosGraphicsManager: cannot restore the previous mode either");
			_modeIndex = -1;
			_cursor.forget();
			_frame.screen().free();
		} else if (_modeIndex >= 0 && hadDirect) {
			// The old picture went with the frame: a cleared one, and the
			// engine told to draw it again.
			createFrame(oldW, oldH, oldFormat);
			_fullDirty = true;
			++_screenChangeID;
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
	// The old frame, cursor included, is gone; the full repaint that
	// follows draws the cursor afresh.
	_cursor.forget();
	_modeOwed = false;
	return OSystem::kTransactionSuccess;
}

OSystem::TransactionError DosGraphicsManager::endGFXTransaction() {
	if (!_pendingW)
		return OSystem::kTransactionSuccess;
	if (deferring()) {
		// The launcher's size (base/main.cpp setupGraphics()), while the
		// text loading screen is up or a stopped loading screen says why:
		// only the buffer for now, as long as the mode would exist.
		if (DOS::chooseMode(_modes, _pendingW, _pendingH, _pendingFormat, ConfMan.getBool("dos_force_fallback")).index < 0) {
			warning("DosGraphicsManager: no %ux%u %s mode", _pendingW, _pendingH, _pendingFormat.toString().c_str());
			_pendingW = 0;
			return OSystem::kTransactionSizeChangeFailed;
		}
		_modeOwed = true;
	} else {
		const OSystem::TransactionError err = gameModeSwitch(_pendingW, _pendingH, _pendingFormat);
		if (err != OSystem::kTransactionSuccess) {
			_pendingW = 0;
			return err;
		}
	}
	createFrame(_pendingW, _pendingH, _pendingFormat);
	_pendingW = 0;
	_paletteDirty = true;
	_fullDirty = true;
	++_screenChangeID;
	convertCursor();
	if (_modeOwed)
		return OSystem::kTransactionSuccess;
	if (DOS::Loading::stage() == DOS::Loading::kStageText)
		startLoadingScreen();
	else if (_loadingShown) {
		queryVramWindow();	// the engine changed modes while it loads
		lockSurfaces(true);
		drawLoadingScreen(true);
	}
	return OSystem::kTransactionSuccess;
}

bool DosGraphicsManager::deferring() const {
	return !_engineStarted && (_deferModes || DOS::Loading::halted());
}

OSystem::TransactionError DosGraphicsManager::gameModeSwitch(uint w, uint h, const Graphics::PixelFormat &f) {
	// The text stage's timer proc writes text memory: out before the mode
	// changes, whether or not the change works.
	const bool text = DOS::Loading::stage() == DOS::Loading::kStageText;
	if (text)
		DOS::Loading::stopTextUpdates();
	const OSystem::TransactionError err = switchMode(w, h, f);
	if (err == OSystem::kTransactionSuccess)
		return err;
	if (text) {
		DOS::Loading::finish("no mode");	// nothing left to show it on
	} else if (_loadingShown) {
		if (_modeIndex >= 0) {
			// The previous mode is back, _screen with it.
			queryVramWindow();
			lockSurfaces(true);
			drawLoadingScreen(true);
		} else {
			finishLoading("no mode");
		}
	}
	return err;
}

bool DosGraphicsManager::applyDeferredMode() {
	// An engine that draws in the launcher's mode without a transaction
	// of its own: that mode now, _screen as it is.
	if (gameModeSwitch(_screen.w, _screen.h, _screen.format) != OSystem::kTransactionSuccess) {
		_modeOwed = false;	// as a failed transaction: nothing drawn
		return false;
	}
	_paletteDirty = true;
	_fullDirty = true;
	if (DOS::Loading::stage() == DOS::Loading::kStageText)
		startLoadingScreen();
	syncFrame(false);
	return true;
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

void DosGraphicsManager::prepareWrite() {
	if (!_frame.screen().direct())
		return;
	syncFrame(false);	// still the window's pixels? (FrameKeeper::lost())
	if (!_frame.screen().direct())
		return;
	SDL_Surface *s = SDL_GetWindowSurface(_window);
	const Common::Rect r = _cursor.restore((byte *)s->pixels, s->pitch);
	if (!r.isEmpty()) {
		if (_cursorBack.isEmpty())
			_cursorBack = r;
		else
			_cursorBack.extend(r);
	}
}

void DosGraphicsManager::copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h) {
	if (!_screen.getPixels())	// no mode (see endGFXTransaction())
		return;
	prepareWrite();
	_screen.copyRectToSurface(buf, pitch, x, y, w, h);
	addDirty(Common::Rect(x, y, x + w, y + h));
}

Graphics::Surface *DosGraphicsManager::lockScreen() {
	// Until unlockScreen() the game may read and write any of it.
	prepareWrite();
	return &_screen;
}

void DosGraphicsManager::fillScreen(uint32 col) {
	if (!_screen.getPixels())
		return;
	prepareWrite();
	_screen.fillRect(Common::Rect(_screen.w, _screen.h), col);
	_fullDirty = true;
}

void DosGraphicsManager::fillScreen(const Common::Rect &r, uint32 col) {
	if (!_screen.getPixels())
		return;
	prepareWrite();
	_screen.fillRect(r, col);
	addDirty(r);
}

void DosGraphicsManager::setShakePos(int shakeXOffset, int shakeYOffset) {
	if (shakeXOffset != _shakeX || shakeYOffset != _shakeY) {
		_shakeX = shakeXOffset;
		_shakeY = shakeYOffset;
		_fullDirty = true;
		// Shaken, the window shows the frame shifted: a buffer of its own
		// meanwhile.
		syncFrame(false);
	}
}

void DosGraphicsManager::updateScreen() {
	if (_modeOwed && _screen.getPixels() && _engineStarted && !applyDeferredMode())
		return;
	if (!_window || !_screen.getPixels())
		return;
	SDL_Surface *s = SDL_GetWindowSurface(_window);
	if (!s || !surfaceFits(s))
		return;
	syncFrame(true);

	// If there's an active debugger, update it (as the SDL and OpenGL
	// graphics managers do): this is what opens the debug socket
	// (Debugger::onFrame(), gui/debugsocket.cpp) on a null test before the
	// engine exists.
	GUI::Debugger *debugger = g_engine ? g_engine->getDebugger() : nullptr;
	if (debugger)
		debugger->onFrame();

	if (_loadingShown) {
		// The loading screen stays until the game has drawn something
		// (its first frames are black: SCI's title fades in), a key or a
		// click comes, or the engine has been at it for too long.
		if (!_loadingSawUpdate) {
			_loadingSawUpdate = true;
			DOS::Loading::enter(DOS::kLoadFirstFrame);
			DOS::logMemInfo("first-frame");
		}
		// Stopped (DOS::Loading::halt()), it stays for its message unless
		// the game draws after all.
		const bool halted = DOS::Loading::halted();
		const char *why = nullptr;
		const uint32 t0 = DOS::Loading::now();
		const bool content = !(_loadingAbort && !halted) && gameHasContent();
		_loadingCheckMs += DOS::Loading::now() - t0;
		_loadingChecks++;
		if (_loadingAbort && !halted)
			why = "input";
		else if (content)
			why = "content";
		else if (!halted && DOS::Loading::now() - _loadingStart > kLoadingTimeoutMs)
			why = "timeout";
		if (!why) {
			loadingTick();
			return;
		}
		finishLoading(why);
	}

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
	if (!_cursorBack.isEmpty())
		send.push_back(_cursorBack);	// taken away before a write (prepareWrite())
	_cursorBack = Common::Rect();

	// Rectangles below are in window rows before line repeat: the mode's
	// own rows, or the 400 the 480 hold.
	const int windowH = _lineRepeat ? DOS::logicalRow(s->h - 1) + 1 : s->h;
	const Common::Rect window(s->w, windowH);
	if (_fullDirty) {
		if (_frame.screen().direct()) {
			// The frame is there already; around it, colour 0.
			DOS::GameScreen::clearOutside((byte *)s->pixels, s->pitch, s->w, s->h, _screen.w, _screen.h,
										  SDL_BYTESPERPIXEL(s->format));
		} else {
			// Shake shifts the whole picture; the uncovered strip is colour 0.
			memset(s->pixels, 0, s->pitch * s->h);
			Common::Rect dst(_screen.w, _screen.h);
			dst.translate(shakeX(), shakeY());
			dst.clip(window);
			blit(s, dst);
		}
		send.clear();
		send.push_back(Common::Rect(s->w, s->h));
	} else {
		for (uint i = 0; i < _dirty.size(); ++i) {
			Common::Rect r = _dirty[i];
			r.translate(shakeX(), shakeY());
			r.clip(window);
			if (r.isEmpty())
				continue;
			if (!_frame.screen().direct())
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
	if (_unlockAfterPresent) {
		_unlockAfterPresent = false;
		lockSurfaces(false);
	}
}

bool DosGraphicsManager::surfaceFits(const SDL_Surface *s) const {
	// After a failed mode change the display can be in another mode than
	// _screen describes; copying then would write past the surface or at
	// the wrong pixel size.
	if (_modeIndex < 0 || !_screen.getPixels())
		return false;
	return fitsWindow(s, _screen.w, _screen.h, _screen.format.bytesPerPixel);
}

bool DosGraphicsManager::fitsWindow(const SDL_Surface *s, int w, int h, int bpp) const {
	const int windowH = _lineRepeat ? DOS::logicalRow(s->h - 1) + 1 : s->h;
	return SDL_BYTESPERPIXEL(s->format) == bpp && s->w >= w && windowH >= h;
}

const DOS::FrameWindow *DosGraphicsManager::frameWindow(DOS::FrameWindow &out) const {
	SDL_Surface *s = _window ? SDL_GetWindowSurface(_window) : nullptr;
	if (!s || !s->pixels || _modeIndex < 0 || _modeOwed)
		return nullptr;
	out.pixels = (byte *)s->pixels;
	out.pitch = s->pitch;
	out.w = s->w;
	out.h = _lineRepeat ? DOS::logicalRow(s->h - 1) + 1 : s->h;
	out.format = _modes[_modeIndex].format;
	out.lineRepeat = _lineRepeat;
	return &out;
}

void DosGraphicsManager::createFrame(uint w, uint h, const Graphics::PixelFormat &f) {
	_cursor.forget();
	_cursorBack = Common::Rect();
	// The old frame may be a buffer the loading screen locked: unlocked
	// before it is freed (CWSDPMI keeps no lock count).
	unlockRegion(_lockAddr[1], _lockSize[1]);
	DOS::FrameWindow win;
	// The text loading screen gives way to the graphics one as the game's
	// mode comes (endGFXTransaction()): that frame starts in a buffer.
	const bool loading = _loadingShown || DOS::Loading::stage() == DOS::Loading::kStageText;
	_frame.create(w, h, f, frameWindow(win), loading, _shakeX || _shakeY);
}

void DosGraphicsManager::syncFrame(bool frameTick) {
	if (!_frame.screen().exists())
		return;
	// A locked buffer is unlocked before it can be freed; only the loading
	// screen locks it, and then it stays.
	if (_lockSize[1] && !_loadingShown)
		unlockRegion(_lockAddr[1], _lockSize[1]);
	DOS::FrameWindow win;
	const DOS::FrameWindow *w = frameWindow(win);
	switch (_frame.sync(w, _loadingShown, _shakeX, _shakeY, frameTick, w ? &_cursor : nullptr, &_cursorBack)) {
	case DOS::FrameKeeper::kSame:
		break;
	case DOS::FrameKeeper::kMoved:
		_fullDirty = true;
		break;
	case DOS::FrameKeeper::kLost:
		warning("DosGraphicsManager: the window surface was made again; the frame is redrawn");
		_cursorBack = Common::Rect();
		_fullDirty = true;
		++_screenChangeID;
		break;
	}
}

void DosGraphicsManager::blit(SDL_Surface *s, const Common::Rect &r) {
	// Only from a buffer of its own (never when _frame is the window).
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

uint32 DosGraphicsManager::loadingColor(int which) const {
	const Graphics::PixelFormat &f = _modes[_modeIndex].format;
	if (f.bytesPerPixel == 1)
		return kLoadingFirstIndex + which;
	return f.RGBToColor(kLoadRgb[which][0], kLoadRgb[which][1], kLoadRgb[which][2]);
}

void DosGraphicsManager::startLoadingScreen() {
	DOS::Loading::enterGraphics();
	DOS::Loading::setTickHook(loadingTickHook, this);
	_loadingShown = true;
	syncFrame(false);	// the window shows the loading screen, not the frame
	_loadingAbort = false;
	_loadingSawUpdate = false;
	_loadingStart = DOS::Loading::now();
	memset(_usedColors, 0, sizeof(_usedColors));
	_loadingLitSeen = false;
	_loadingDraws = _loadingDrawMs = _loadingCheckMs = _loadingChecks = 0;
	queryVramWindow();
	lockSurfaces(true);
	drawLoadingScreen(true);
}

// The phase line is laid out for the longest label.
static const int kLoadLabelChars = 26;

DOS::LoadScreenLayout DosGraphicsManager::loadingLayout(const SDL_Surface *s, Common::String &title) const {
	title = DOS::asciiTitle(DOS::Loading::title(), s->w / DOS::kLoadGlyphW - 2);
	const bool korean = DOS::Loading::korean();
	const int captionW = korean ? DOS::kCaptionKoWidth : (int)strlen("Loading...") * DOS::kLoadGlyphW;
	const int captionH = korean ? DOS::kCaptionKoHeight : DOS::kLoadGlyphH;
	return DOS::loadScreenLayout(s->w, s->h, title.size(), captionW, captionH, kLoadLabelChars);
}

void DosGraphicsManager::drawLoadingScreen(bool full) {
	SDL_Surface *s = _window ? SDL_GetWindowSurface(_window) : nullptr;
	if (!s || _modeIndex < 0)
		return;
	if (s->format == SDL_PIXELFORMAT_INDEX8 && full) {
		// Its own colours, the rest black; the game's palette waits in
		// _palette (_paletteDirty) for the switch.
		if (!SDL_GetSurfacePalette(s) && !SDL_CreateSurfacePalette(s))
			return;
		SDL_Color c[256];
		memset(c, 0, sizeof(c));
		for (int i = 0; i < 256; ++i)
			c[i].a = 255;
		for (int i = 0; i < kLoadColorCount; ++i) {
			c[kLoadingFirstIndex + i].r = kLoadRgb[i][0];
			c[kLoadingFirstIndex + i].g = kLoadRgb[i][1];
			c[kLoadingFirstIndex + i].b = kLoadRgb[i][2];
		}
		SDL_SetPaletteColors(SDL_GetSurfacePalette(s), c, 0, 256);
	}
	Common::String title;
	const bool korean = DOS::Loading::korean();
	const char *english = "Loading...";
	const DOS::LoadScreenLayout l = loadingLayout(s, title);
	DOS::LoadCanvas canvas((byte *)s->pixels, s->pitch, s->w, s->h, SDL_BYTESPERPIXEL(s->format));
	const byte *font = DOS::Loading::romFont();

	if (full) {
		canvas.fill(Common::Rect(s->w, s->h), loadingColor(kLoadBg));
		canvas.text(l.title.left, l.title.top, title.c_str(), font, loadingColor(kLoadText));
		if (korean)
			canvas.bitmap(l.caption.left, l.caption.top, DOS::kCaptionKo, (DOS::kCaptionKoWidth + 7) / 8,
						  DOS::kCaptionKoWidth, DOS::kCaptionKoHeight, loadingColor(kLoadDim));
		else
			canvas.text(l.caption.left, l.caption.top, english, font, loadingColor(kLoadDim));
		canvas.frame(l.bar, loadingColor(kLoadFrame));
	}
	const Common::Rect fill = DOS::loadBarFill(l.bar, DOS::Loading::permille());
	canvas.fill(Common::Rect(l.bar.left + 1, l.bar.top + 1, l.bar.right - 1, l.bar.bottom - 1), loadingColor(kLoadBg));
	canvas.fill(fill, loadingColor(kLoadFill));
	const char *label = DOS::Loading::phaseLabel();
	canvas.fill(l.label, loadingColor(kLoadBg));
	if (DOS::Loading::halted()) {
		// Two lines where the phase was, as wide as the screen allows.
		const Common::Rect lines(0, l.label.top, s->w, MIN<int>(s->h, l.label.top + 2 * DOS::kLoadGlyphH + 4));
		canvas.fill(lines, loadingColor(kLoadBg));
		for (int i = 0; i < 2; ++i) {
			const Common::String line = DOS::asciiTitle(DOS::Loading::haltLine(i), s->w / DOS::kLoadGlyphW - 1);
			canvas.text((s->w - (int)line.size() * DOS::kLoadGlyphW) / 2, l.label.top + i * (DOS::kLoadGlyphH + 4),
						line.c_str(), font, loadingColor(kLoadAlert));
		}
		full = true;	// sent whole, below
	} else {
		const int labelW = strlen(label) * DOS::kLoadGlyphW;
		canvas.text(l.label.left + (l.label.width() - labelW) / 2, l.label.top, label, font, loadingColor(kLoadDim));
	}
	_loadingLastFill = fill.width();
	_loadingLastLabel = label;

	const uint32 t0 = DOS::Loading::now();
	if (full) {
		SDL_UpdateWindowSurface(_window);
	} else {
		const Common::Rect rects[2] = { l.bar, l.label };
		sendRectsToVram(s, rects, 2);
	}
	_loadingLastDraw = DOS::Loading::now();
	_loadingDraws++;
	_loadingDrawMs += _loadingLastDraw - t0;
}

bool DosGraphicsManager::queryVramWindow() {
	// The mode SDL3 set, and its VESA window A: only when SDL3 set it
	// banked (no linear frame buffer bit) with a window we may write.
	_vramOk = false;
	__dpmi_regs r;
	memset(&r, 0, sizeof(r));
	r.x.ax = 0x4F03;
	__dpmi_int(0x10, &r);
	if (r.x.ax != 0x004F || (r.x.bx & 0x4000))
		return false;
	const uint16 mode = r.x.bx & 0x3FFF;
	memset(&r, 0, sizeof(r));
	r.x.ax = 0x4F01;
	r.x.cx = mode;
	r.x.es = __tb >> 4;
	r.x.di = __tb & 0x0F;
	__dpmi_int(0x10, &r);
	if (r.x.ax != 0x004F)
		return false;
	byte info[32];
	dosmemget(__tb, sizeof(info), info);
	const byte winAAttr = info[2];
	const uint32 gran = READ_LE_UINT16(info + 4) * 1024;
	const uint32 size = READ_LE_UINT16(info + 6) * 1024;
	const uint32 seg = READ_LE_UINT16(info + 8);
	const uint32 pitch = READ_LE_UINT16(info + 16);
	if ((winAAttr & 0x05) != 0x05 || !gran || !size || !seg || !pitch)
		return false;
	_vramGran = gran;
	_vramWinSize = size;
	_vramBase = seg << 4;
	_vramPitch = pitch;
	_vramOk = true;
	return true;
}

void DosGraphicsManager::sendRectsToVram(SDL_Surface *s, const Common::Rect *rects, int n) {
	// SDL3's direct path sends the whole screen whatever rectangles it is
	// given (the pitches match): 1 MB in XRGB8888 for a bar a few rows
	// tall, some 5 ms of the game's time at 60000 cycles each redraw and,
	// in DOSBox, a lot more of the host's. Through window A ourselves
	// instead: SDL3 sets its bank afresh on every update, so ours need
	// not be put back. Anything unexpected, and SDL3 sends it.
	const int bpp = SDL_BYTESPERPIXEL(s->format);
	bool ok = _vramOk && (int)_vramPitch >= s->w * bpp;
	int bank = -1;
	for (int i = 0; i < n && ok; ++i) {
		Common::Rect r = rects[i];
		r.clip(Common::Rect(s->w, s->h));
		for (int y = r.top; y < r.bottom && ok; ++y) {
			const byte *src = (const byte *)s->pixels + y * s->pitch + r.left * bpp;
			uint32 offset = (uint32)y * _vramPitch + r.left * bpp;
			uint32 left = r.width() * bpp;
			while (left) {
				const int want = (int)(offset / _vramGran);
				const uint32 inWin = offset % _vramGran;
				const uint32 chunk = MIN(left, _vramWinSize - inWin);
				if (want != bank) {
					__dpmi_regs b;
					memset(&b, 0, sizeof(b));
					b.x.ax = 0x4F05;
					b.x.dx = want;
					__dpmi_int(0x10, &b);
					if (b.x.ax != 0x004F) {
						ok = _vramOk = false;
						break;
					}
					bank = want;
				}
				dosmemput(src, chunk, _vramBase + inWin);
				src += chunk;
				offset += chunk;
				left -= chunk;
			}
		}
	}
	if (bank > 0) {
		// Window A back where the BIOS left it, for anyone who assumes so.
		__dpmi_regs b;
		memset(&b, 0, sizeof(b));
		b.x.ax = 0x4F05;
		__dpmi_int(0x10, &b);
	}
	if (ok)
		return;
	Common::Array<SDL_Rect> sdl;
	for (int i = 0; i < n; ++i) {
		SDL_Rect r = { rects[i].left, rects[i].top, rects[i].width(), rects[i].height() };
		sdl.push_back(r);
	}
	SDL_UpdateWindowSurfaceRects(_window, &sdl[0], n);
}

static void unlockRegion(uint32 &addr, uint32 &size) {
	if (!size)
		return;
	__dpmi_meminfo m;
	m.handle = 0;
	m.address = addr;
	m.size = size;
	__dpmi_unlock_linear_region(&m);
	size = 0;
}

static void lockRegion(const void *p, uint32 bytes, uint32 &addr, uint32 &size) {
	unlockRegion(addr, size);
	// Only a large block of its own (dos-heap.cpp), whose pages it shares
	// with nothing. A block from the sbrk heap shares its first and last
	// pages with its neighbours, which an interrupt handler may have locked:
	// unlocking it later would unlock those pages too. When everything is
	// locked (DOS::lockAll()), large blocks included, there is nothing to
	// do, and the unlock would undo that lock: CWSDPMI keeps no lock count.
	if (!p || !bytes || DOS::lockedAll() || !dosHeapInLargeBlock(p, bytes))
		return;
	__dpmi_meminfo m;
	m.handle = 0;
	m.address = __djgpp_base_address + (uint32)p;
	m.size = bytes;
	if (__dpmi_lock_linear_region(&m) == 0) {
		addr = m.address;
		size = m.size;
	}
}

void DosGraphicsManager::lockSurfaces(bool lock) {
	// The game's frame (_screen) and the window surface, 1 MB each in
	// XRGB8888, are read and written whole when the game's first frame
	// replaces the loading screen. Without the game's frames going through
	// them while the engine loads, in 16 MB they were what CWSDPMI swapped
	// out (KQ1KO, DOSBox-X: 640 KB in CWSDPMI.SWP), and paging them back
	// in held that frame up by over half a second. Locked while the loading
	// screen is up, they stay in memory as the game's frames would keep
	// them. Large blocks are pageable DPMI memory (dos-heap.cpp).
	unlockRegion(_lockAddr[0], _lockSize[0]);
	unlockRegion(_lockAddr[1], _lockSize[1]);
	if (!lock)
		return;
	SDL_Surface *s = _window ? SDL_GetWindowSurface(_window) : nullptr;
	if (s)
		lockRegion(s->pixels, (uint32)(s->pitch * s->h), _lockAddr[0], _lockSize[0]);
	if (_screen.getPixels())
		lockRegion(_screen.getPixels(), (uint32)(_screen.pitch * _screen.h), _lockAddr[1], _lockSize[1]);
	debug(1, "DOS: loading screen: locked %u + %u bytes", _lockSize[0], _lockSize[1]);
}

void DosGraphicsManager::loadingTick() {
	if (!_loadingShown || !_window)
		return;
	const uint32 now = DOS::Loading::now();
	const char *label = DOS::Loading::phaseLabel();
	if (label == _loadingLastLabel && now - _loadingLastDraw < kLoadingRedrawMs)
		return;
	SDL_Surface *s = SDL_GetWindowSurface(_window);
	if (!s)
		return;
	if (DOS::Loading::halted())
		return;	// stopped: its message is up already (loadingHalted())
	// Only when the bar has grown by a pixel or the phase changed.
	Common::String title;
	const int fillW = DOS::loadBarFill(loadingLayout(s, title).bar, DOS::Loading::permille()).width();
	if (label == _loadingLastLabel && fillW == _loadingLastFill)
		return;
	drawLoadingScreen(false);
}

bool DosGraphicsManager::gameHasContent() {
	// What the game drew since the last check: the dirty rectangles (all
	// of it after a full repaint), which the switch replaces with a full
	// repaint anyway. A cheap test on those first -- in CLUT8, where a
	// fade changes only the palette, the indices drawn are collected and
	// checked against the palette as it is now -- and only once something
	// is lit, a count over the whole frame.
	const Common::Rect all(_screen.w, _screen.h);
	Common::Array<Common::Rect> rects;
	if (_fullDirty) {
		rects.push_back(all);
	} else {
		for (uint i = 0; i < _dirty.size(); ++i) {
			Common::Rect r = _dirty[i];
			r.clip(all);
			if (!r.isEmpty())
				rects.push_back(r);
		}
	}
	_dirty.clear();
	_fullDirty = false;
	const byte *pixels = (const byte *)_screen.getPixels();
	if (_screen.format.bytesPerPixel == 1) {
		for (uint i = 0; i < rects.size(); ++i)
			DOS::markUsedColors(pixels, _screen.pitch, rects[i], _usedColors);
		if (!DOS::anyUsedColorLit(_usedColors, _palette))
			return false;
	} else if (!_loadingLitSeen) {
		for (uint i = 0; i < rects.size() && !_loadingLitSeen; ++i)
			_loadingLitSeen = DOS::anyPixelLit(pixels, _screen.pitch, _screen.format, rects[i]);
		if (!_loadingLitSeen)
			return false;
	} else if (rects.empty()) {
		return false;	// counted already, and nothing changed
	}
	const uint need = DOS::firstFrameLitSamples(_screen.w, _screen.h);
	return DOS::countLitSamples(pixels, _screen.pitch, _screen.format, _palette, _screen.w, _screen.h, need) >= need;
}

void DosGraphicsManager::finishLoading(const char *why) {
	if (!_loadingShown)
		return;
	_loadingShown = false;
	_unlockAfterPresent = true;	// that present reads and writes both whole
	DOS::Loading::setTickHook(nullptr, nullptr);
	DOS::Loading::finish(why);
	debug(1, "DOS: loading screen: %u redraws took %u ms, %u looks for the first frame %u ms", _loadingDraws,
		  _loadingDrawMs, _loadingChecks, _loadingCheckMs);
	// The game's own frame, palette and cursor, all of it.
	_dirty.clear();
	_fullDirty = true;
	_paletteDirty = true;
	_cursor.forget();
	syncFrame(false);	// the frame can be the window again
}

void DosGraphicsManager::engineStopped() {
	// A stopped loading screen (the engine failed before its first frame)
	// stays up for its message; the launcher's mode waits (deferring()).
	if (!DOS::Loading::halted())
		finishLoading("engine done");
	_unlockAfterPresent = false;
	lockSurfaces(false);
	// Back in the launcher (or the next game), modes are set as asked:
	// the loading screen is over.
	_engineStarted = false;
	_deferModes = false;
	_renderTargetWarned.clear();
}

bool DosGraphicsManager::loadingPoll(bool got, const Common::Event &event) {
	if (got && _loadingSwallowUp != Common::EVENT_INVALID && event.type == _loadingSwallowUp) {
		_loadingSwallowUp = Common::EVENT_INVALID;
		return true;	// the release of the key or button that skipped it
	}
	if (!_loadingShown)
		return false;
	loadingTick();
	if (!got || DOS::Loading::halted())
		return false;	// stopped: keys are for the dialog it stands for
	Common::EventType up = Common::EVENT_INVALID;
	if (event.type == Common::EVENT_KEYDOWN)
		up = Common::EVENT_KEYUP;
	else if (event.type == Common::EVENT_LBUTTONDOWN)
		up = Common::EVENT_LBUTTONUP;
	else if (event.type == Common::EVENT_RBUTTONDOWN)
		up = Common::EVENT_RBUTTONUP;
	if (up == Common::EVENT_INVALID)
		return false;
	_loadingAbort = true;
	_loadingSwallowUp = up;
	return true;
}

void DosGraphicsManager::loadingHalted() {
	if (_loadingShown)
		drawLoadingScreen(true);
}

void DosGraphicsManager::showOverlay(bool inGUI) {
	// A dialog before the game's mode (an error, say) cannot be shown:
	// the text screen says so.
	DOS::Loading::halt(nullptr);
	loadingHalted();
	static bool warned = false;
	if (!warned) {
		warning("DosGraphicsManager: the GUI overlay is not shown before M4");
		warned = true;
	}
	// The overlay's 600 KB are taken while it is shown (or drawn to) and
	// given back by hideOverlay().
	_overlay.get();
	_overlayVisible = true;
}

void DosGraphicsManager::hideOverlay() {
	_overlayVisible = false;
	_overlay.release();
}

void DosGraphicsManager::clearOverlay() {
	Graphics::Surface &o = _overlay.get();
	o.fillRect(Common::Rect(o.w, o.h), 0);
}

void DosGraphicsManager::grabOverlay(Graphics::Surface &surface) const {
	_overlay.grab(surface);
}

void DosGraphicsManager::copyRectToOverlay(const void *buf, int pitch, int x, int y, int w, int h) {
	_overlay.get().copyRectToSurface(buf, pitch, x, y, w, h);
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
