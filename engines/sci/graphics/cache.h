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

#ifndef SCI_GRAPHICS_CACHE_H
#define SCI_GRAPHICS_CACHE_H

#include "common/hashmap.h"
#include "common/array.h"
#include "common/str.h"

namespace Sci {

class GfxFont;
class GfxFontUnicode;
class GfxView;

typedef Common::HashMap<int, GfxFont *> FontCache;
typedef Common::HashMap<int, GfxView *> ViewCache;

/**
 * Cache class, handles caching of views/fonts
 */
class GfxCache {
public:
	GfxCache(ResourceManager *resMan, GfxScreen *screen, GfxPalette *palette);
	~GfxCache();

	GfxFont *getFont(GuiResourceId fontId);

	/**
	 * Whether @p fontId resolves to a GfxFontSet, i.e. whether that id can
	 * already draw characters outside the game's own face. Callers use it to
	 * skip the legacy switch to font 1001 / 900.
	 */
	bool fontIsSet(GuiResourceId fontId);

	GfxView *getView(GuiResourceId viewId);

	int16 kernelViewGetCelWidth(GuiResourceId viewId, int16 loopNo, int16 celNo);
	int16 kernelViewGetCelHeight(GuiResourceId viewId, int16 loopNo, int16 celNo);
	int16 kernelViewGetLoopCount(GuiResourceId viewId);
	int16 kernelViewGetCelCount(GuiResourceId viewId, int16 loopNo);

private:
	void purgeFontCache();
	void purgeViewCache();

	ResourceManager *_resMan;
	GfxScreen *_screen;
	GfxPalette *_palette;

	FontCache _cachedFonts;

	/**
	 * Build a Unicode-backed font for @p fontId, or nullptr when the game
	 * ships no SCVMUNI bundle. Keyed on the bundle rather than on a font
	 * number, so a game that never requests the legacy CJK font id still
	 * gets Unicode text.
	 */
	GfxFont *createUnicodeFont(GuiResourceId fontId);

	/** The shared Unicode bundle, loaded on first use; nullptr when absent. */
	GfxFontUnicode *loadUnicodeFont();


	/**
	 * Wrap the game's own font for @p fontId in a GfxFontSet, or nullptr when
	 * the id names no resource. See docs/i18n/M10_FONTSET.md.
	 */
	GfxFont *createFontSet(GuiResourceId fontId);

	/**
	 * Reads hires_text_font / hires_text_font_size from the game's own
	 * domain, once per engine run, so each of their warnings is given at
	 * most once even though purgeFontCache() reloads the bundle. Leaves
	 * _hiresTextFontPath empty when the key is absent or ignored.
	 */
	void resolveHiresTextFont();

	/** The shared SCVMUNI bundle, loaded at most once. */
	GfxFontUnicode *_unicodeFont;
	bool _unicodeFontTried;

	bool _hiresTextFontResolved;
	Common::String _hiresTextFontPath;
	int _hiresTextFontSize;
	/**
	 * Fonts an adapter wraps but does not own. They are not in _cachedFonts
	 * (only the adapter is), so the cache has to delete them separately.
	 */
	Common::Array<GfxFont *> _ownedFonts;
	ViewCache _cachedViews;
};

} // End of namespace Sci

#endif // SCI_GRAPHICS_CACHE_H
