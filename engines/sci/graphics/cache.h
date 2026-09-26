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
#include "graphics/hires_text/font_map.h"
#include "sci/graphics/hirestextsettings.h"
#include "sci/graphics/textlatin.h"

namespace Sci {

class GfxFont;
class GfxFontUnicode;
class GfxView;
class TtfGlyphSource;

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

	/**
	 * hires_text_log (game domain, bool): whether GfxText16 emits one
	 * debug(1, ...) line per drawn line, naming the font id and a tally of
	 * which face kind (see textlatin.h's FaceKind) drew each glyph.
	 *
	 * Resolved once and cached here, so GfxText16 never
	 * does a ConfMan lookup per character. Unlike hires_text_font, this is
	 * read unconditionally - NOT gated on hiresTextFontApplies() - so an
	 * English game (which the scope predicate refuses hires_text_font on)
	 * can still log which font id drew its text.
	 */
	bool isTextLogEnabled();

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

	/**
	 * The Unicode face for a font id with settings @p s: a TrueType bundle
	 * (s.facePath at s.size, routed with s.latinFacePath when that differs),
	 * or the shared .uni bundle when no face is named or the face fails.
	 * Bundles are shared by every font id resolving to the same faces and
	 * are owned here. Forces @p s.latin to kLatinOff when the id ends up
	 * with no live TrueType face - there is nothing to route Latin text to.
	 * nullptr when neither a face nor a .uni bundle is available.
	 */
	GfxFontUnicode *unicodeFaceFor(GuiResourceId fontId, FontSettings &s);

	/** The .uni bundle (sci.uni, korean.uni, towns.uni), loaded at most
	 *  once; nullptr when the game ships none. */
	GfxFontUnicode *loadUniBundle();

	/**
	 * The TrueType source for @p path at @p size, opened at most once per
	 * (path, size, Hangul check) and shared; nullptr when it fails to open,
	 * with one warning per such key (@p what names the setting in it).
	 */
	TtfGlyphSource *ttfSource(const Common::String &path, int size, bool requireHangul,
							  const char *what, const char *fallback);

	/**
	 * Wrap the game's own font for @p fontId in a GfxFontSet, or nullptr when
	 * the id names no resource. See docs/i18n/M10_FONTSET.md.
	 */
	GfxFont *createFontSet(GuiResourceId fontId);

	/**
	 * Reads the hi-res text ini keys (hires_text_font, _font_size, _latin,
	 * _latin_font, _latin_space, _metrics) and hires_text.map (the game
	 * directory's, or the file ini hires_text_map names) once per engine
	 * run, under the scope predicate (SCI16, a CJK code page, the game's own
	 * domain). Out of scope, every key and the map get one warning each and
	 * nothing applies. Each bad value gets one warning and is ignored.
	 */
	void resolveHiresText();

	/** resolveFontSettings() for @p fontId from what resolveHiresText() read. */
	FontSettings fontSettingsFor(GuiResourceId fontId);

	bool _hiresResolved;
	bool _hiresApplies;              ///< the scope predicate held
	HiresTextOverrides _hiresIni;
	Graphics::HiResTextConfig _hiresMap;
	bool _hiresMapLoaded;
	Common::Path _gameDir;
	/// The ini latin keys were set: their "not in effect" warning is due
	/// (once) when a font ends up with no TrueType face.
	bool _iniLatinKeysSet;
	bool _iniLatinIgnoredWarned;
	/// Font ids already warned about a map Latin mode with no face.
	Common::HashMap<int, bool> _latinNoFaceWarned;

	/// TrueType sources by "path|size|hangul"; nullptr = failed (warned).
	Common::HashMap<Common::String, TtfGlyphSource *> _ttfSources;
	/// Unicode bundles built on those sources, by their faces and routing.
	Common::HashMap<Common::String, GfxFontUnicode *> _ttfBundles;
	/// The .uni bundle.
	GfxFontUnicode *_uniBundle;
	bool _uniBundleTried;

	bool _textLogResolved;
	bool _textLog;
	/**
	 * Fonts an adapter wraps but does not own. They are not in _cachedFonts
	 * (only the adapter is), so the cache has to delete them separately.
	 */
	Common::Array<GfxFont *> _ownedFonts;
	ViewCache _cachedViews;
};

} // End of namespace Sci

#endif // SCI_GRAPHICS_CACHE_H
