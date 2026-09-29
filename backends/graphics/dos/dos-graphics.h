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

#ifndef BACKENDS_GRAPHICS_DOS_DOS_GRAPHICS_H
#define BACKENDS_GRAPHICS_DOS_DOS_GRAPHICS_H

#include "backends/graphics/graphics.h"
#include "backends/platform/dos/dos-modes.h"
#include "backends/platform/dos/loading-screen.h"
#include "backends/platform/dos/soft-cursor.h"
#include "common/array.h"
#include "common/events.h"
#include "common/rect.h"
#include "graphics/surface.h"

struct SDL_Window;
struct SDL_Surface;
struct SDL_DisplayMode;

/**
 * The game screen in system RAM, copied to SDL3's window surface one dirty
 * rectangle at a time with the cursor on top; SDL3's direct-framebuffer
 * path then sends those rectangles to VRAM. The physical mode is the game's
 * size and format, or (M2) 640x480 for a 640x400 game with every fifth row
 * sent twice (line-repeat.h) when the card has no exact mode or
 * `dos_force_fallback=true`. A CLUT8 cursor on a true-colour screen is
 * converted through the cursor palette (kFeatureCursorPalette) or the game
 * palette. The overlay is kept but not shown.
 */
class DosGraphicsManager : public GraphicsManager {
public:
	DosGraphicsManager();
	~DosGraphicsManager() override;

	SDL_Window *window() const { return _window; }
	Common::Point gameMouse(float wx, float wy) const;
	void setMousePos(int x, int y);

	bool hasFeature(OSystem::Feature f) const override { return f == OSystem::kFeatureCursorPalette; }
	void setFeatureState(OSystem::Feature f, bool enable) override;
	bool getFeatureState(OSystem::Feature f) const override;

	Graphics::PixelFormat getScreenFormat() const override { return _screen.format; }
	Common::List<Graphics::PixelFormat> getSupportedFormats() const override;

	void initSize(uint width, uint height, const Graphics::PixelFormat *format = nullptr) override;
	int getScreenChangeID() const override { return _screenChangeID; }
	void beginGFXTransaction() override {}
	OSystem::TransactionError endGFXTransaction() override;

	int16 getHeight() const override { return _screen.h; }
	int16 getWidth() const override { return _screen.w; }
	void setPalette(const byte *colors, uint start, uint num) override;
	void grabPalette(byte *colors, uint start, uint num) const override;
	void copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h) override;
	Graphics::Surface *lockScreen() override { return &_screen; }
	void unlockScreen() override { addDirty(Common::Rect(_screen.w, _screen.h)); }
	void fillScreen(uint32 col) override;
	void fillScreen(const Common::Rect &r, uint32 col) override;
	void updateScreen() override;
	void setShakePos(int shakeXOffset, int shakeYOffset) override;
	void setFocusRectangle(const Common::Rect &rect) override {}
	void clearFocusRectangle() override {}

	void showOverlay(bool inGUI) override;
	void hideOverlay() override { _overlayVisible = false; }
	bool isOverlayVisible() const override { return _overlayVisible; }
	Graphics::PixelFormat getOverlayFormat() const override { return _overlay.format; }
	void clearOverlay() override;
	void grabOverlay(Graphics::Surface &surface) const override;
	void copyRectToOverlay(const void *buf, int pitch, int x, int y, int w, int h) override;
	int16 getOverlayHeight() const override { return _overlay.h; }
	int16 getOverlayWidth() const override { return _overlay.w; }

	bool showMouse(bool visible) override;
	void warpMouse(int x, int y) override;
	void setMouseCursor(const void *buf, uint w, uint h, int hotspotX, int hotspotY, uint32 keycolor,
						const Graphics::PixelFormat *format, const byte *mask, frac_t scaleX, frac_t scaleY) override;
	void setCursorPalette(const byte *colors, uint start, uint num) override;

	/**
	 * The window surface as it was last sent, cursor included: raw rows
	 * (pitch w * bytes per pixel) at SHOTnnnn.RAW, "w h bits format" at
	 * SHOTnnnn.TXT (as the debug socket's dump writes it), the palette at
	 * SHOTnnnn.PAL when it is CLUT8; nnnn counts from 0000 in this run.
	 */
	void saveScreenshot() override;

	/**
	 * While a loading screen is up (DOS::Loading), no graphics mode is set
	 * before the engine starts: base/main.cpp's launcher-size mode would
	 * take the text screen away early. The mode comes with the engine's
	 * first graphics transaction (or, if it has none, its first
	 * updateScreen()), and the graphics loading screen with it.
	 */
	void setDeferModes(bool defer) { _deferModes = defer; }
	/** OSystem::engineInit(): graphics transactions set modes from now on. */
	void engineStarted() { _engineStarted = true; }
	/** OSystem::engineDone(): a loading screen still up goes. */
	void engineStopped();
	/**
	 * From pollEvent(): @p event if @p got. A key or a click ends the
	 * loading screen; true if @p event was that (or its release) and is
	 * not for the game.
	 */
	bool loadingPoll(bool got, const Common::Event &event);
	/** DOS::Loading::halt() came: the graphics loading screen shows why. */
	void loadingHalted();

private:
	void addDirty(const Common::Rect &r);
	/** Set _sdlModes[index]; srcW x srcH is the picture it shows (for the log). */
	bool setMode(int index, bool lineRepeat, uint srcW, uint srcH);
	/**
	 * Choose and set the mode for a w x h picture in @p f (_modeIndex,
	 * _lineRepeat and the rest along with it), or put the previous one
	 * back. The TransactionError for endGFXTransaction().
	 */
	OSystem::TransactionError switchMode(uint w, uint h, const Graphics::PixelFormat &f);
	/** switchMode() for the game, with the loading screen kept right if it fails. */
	OSystem::TransactionError gameModeSwitch(uint w, uint h, const Graphics::PixelFormat &f);
	/**
	 * Whether a transaction only makes the buffer (see setDeferModes()):
	 * before engineInit() while the text loading screen is up, or while a
	 * stopped loading screen shows its message outside an engine.
	 */
	bool deferring() const;
	/** Where the loading screen's parts go in @p s; @p title gets the name as drawn. */
	DOS::LoadScreenLayout loadingLayout(const SDL_Surface *s, Common::String &title) const;
	/** The mode deferred by setDeferModes(), for _screen as it is. */
	bool applyDeferredMode();

	// The graphics loading screen (DOS::Loading's second stage).
	void startLoadingScreen();
	void drawLoadingScreen(bool full);
	/** Whether the game has drawn something that is not black since the loading screen came up. */
	bool gameHasContent();
	void finishLoading(const char *why);
	/** Moves the bar when it is due (at most every kLoadingRedrawMs). */
	void loadingTick();
	static void loadingTickHook(void *self) { ((DosGraphicsManager *)self)->loadingTick(); }
	uint32 loadingColor(int which) const;
	/** Keeps _screen and the window surface in memory while the loading screen is up (see there). */
	void lockSurfaces(bool lock);
	/** VESA window A of the mode SDL3 set (_vram*), if it set it banked. */
	bool queryVramWindow();
	/** @p rects of the window surface to the screen: through window A, or SDL3. */
	void sendRectsToVram(SDL_Surface *s, const Common::Rect *rects, int n);
	/** Whether @p s has _screen's pixel size and room for it (see updateScreen()). */
	bool surfaceFits(const SDL_Surface *s) const;
	void blit(SDL_Surface *s, const Common::Rect &r);
	void convertCursor();

	Common::Array<DOS::VideoMode> _modes;
	Common::Array<SDL_DisplayMode> _sdlModes;	///< SDL3's own copy of _modes[i], as SDL_SetWindowFullscreenMode() takes it
	int _modeIndex;	///< the entry of _modes that is set, or -1
	bool _lineRepeat;	///< 640x400 game in a 640x480 mode (line-repeat.h)
	bool _vsync;	///< dos_vsync=wait: send in the vertical retrace
	bool _vsyncWarned;	///< the wait timed out once and was turned off
	uint _lastInitW, _lastInitH;	///< the last initSize(), 0x0 before any (see DOS::formatsSize())
	uint _shotCount;

	SDL_Window *_window;
	int _screenChangeID;

	uint _pendingW, _pendingH;
	Graphics::PixelFormat _pendingFormat;

	Graphics::Surface _screen;	///< the game's pixels, in its own format
	Graphics::Surface _overlay;	///< RGB565 640x480; not shown until M4
	bool _overlayVisible;
	byte _palette[256 * 3];
	bool _paletteDirty;
	int _shakeX, _shakeY;

	Common::Array<Common::Rect> _dirty;
	bool _fullDirty;

	DOS::SoftCursor _cursor;
	// The cursor as the engine gave it; convertCursor() makes _cursor from it.
	Common::Array<byte> _cursorSrc;
	uint _cursorW, _cursorH;
	int _cursorHotX, _cursorHotY;
	uint32 _cursorKey;
	Graphics::PixelFormat _cursorFormat;
	byte _cursorPalette[256 * 3];
	bool _cursorPaletteEnabled;
	bool _cursorFormatWarned;
	bool _cursorVisible;
	int _mouseX, _mouseY;

	bool _deferModes;	///< see setDeferModes()
	bool _engineStarted;
	bool _modeOwed;	///< _screen was made while modes were deferred; no mode for it yet
	bool _loadingShown;	///< the window shows the loading screen, not _screen
	bool _loadingAbort;	///< a key or click came in: show the game
	bool _loadingSawUpdate;	///< updateScreen() came since the loading screen went up
	bool _loadingLitSeen;	///< true colour: the game has drawn a lit pixel
	uint32 _loadingStart;	///< DOS::Loading::now() when it went up
	uint32 _loadingLastDraw;
	int _loadingLastFill;	///< bar fill width last drawn, pixels
	const char *_loadingLastLabel;
	uint _loadingDraws, _loadingDrawMs, _loadingCheckMs, _loadingChecks;	///< what it cost (debug level 1)
	bool _usedColors[256];
	uint32 _lockAddr[2], _lockSize[2];	///< lockSurfaces()'s regions (linear), size 0 if none
	bool _unlockAfterPresent;
	Common::EventType _loadingSwallowUp;	///< the release to drop, of the key/click that skipped the loading screen	///< the loading screen is gone: unlock once the game's frame is sent
	bool _vramOk;	///< see queryVramWindow()
	uint32 _vramGran, _vramWinSize, _vramBase, _vramPitch;	///< CLUT8 indices the game drew with during the loading screen
};

#endif
