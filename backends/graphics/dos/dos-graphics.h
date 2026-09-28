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
#include "backends/platform/dos/soft-cursor.h"
#include "common/array.h"
#include "common/rect.h"
#include "graphics/surface.h"

struct SDL_Window;
struct SDL_DisplayMode;

/**
 * The game screen in system RAM, copied to SDL3's window surface one dirty
 * rectangle at a time with the cursor on top; SDL3's direct-framebuffer
 * path then sends those rectangles to VRAM. The physical mode is the game's
 * size and format, or (M2) 640x480 for a 640x400 game with every fifth row
 * sent twice (line-repeat.h) when the card has no exact mode or
 * `dos_force_fallback=true`. The overlay is kept but not shown.
 */
class DosGraphicsManager : public GraphicsManager {
public:
	DosGraphicsManager();
	~DosGraphicsManager() override;

	SDL_Window *window() const { return _window; }
	Common::Point gameMouse(float wx, float wy) const;
	void setMousePos(int x, int y);

	bool hasFeature(OSystem::Feature f) const override { return false; }
	void setFeatureState(OSystem::Feature f, bool enable) override {}
	bool getFeatureState(OSystem::Feature f) const override { return false; }

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
	void setCursorPalette(const byte *colors, uint start, uint num) override {}

private:
	void addDirty(const Common::Rect &r);
	bool setMode(int index, bool lineRepeat);
	void blit(SDL_Surface *s, const Common::Rect &r);

	Common::Array<DOS::VideoMode> _modes;
	Common::Array<SDL_DisplayMode> _sdlModes;	///< SDL3's own copy of _modes[i], as SDL_SetWindowFullscreenMode() takes it
	bool _lineRepeat;	///< 640x400 game in a 640x480 mode (line-repeat.h)
	uint _formatsW, _formatsH;	///< the size getSupportedFormats() answers for

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
	bool _cursorVisible;
	int _mouseX, _mouseY;
};

#endif
