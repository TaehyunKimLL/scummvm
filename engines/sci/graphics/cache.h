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
#include "graphics/hires_text/text_layout.h"
#include "sci/graphics/hirestextsettings.h"
#include "sci/graphics/textlatin.h"

namespace Graphics {
class TtfGlyphSource;
class UnicodeGlyphSource;
}

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

	/**
	 * How GfxText16 breaks UTF-8 lines: the [layout] section of
	 * hires_text.map over SCI's defaults (Hangul at spaces, kinsoku on,
	 * the Thai fallback on). Resolved once.
	 */
	const Graphics::BreakRules &layoutRules();

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
	/** Which characters a face is checked and fitted with when it opens. */
	enum FaceProbes {
		kProbesDefault = 0,     ///< the fixed Hangul/Latin fit set, no check
		kProbesHangul = 1,      ///< the same, and the face must draw Hangul (a legacy Korean game)
		kProbesTranslation = 2  ///< the fixed set plus a sample of the translation's characters
	};

	/// pixel > 0 opens the face as a pixel font of that design size in a
	/// size cell (TtfGlyphSource::createPixel()); probes then do not apply.
	Graphics::TtfGlyphSource *ttfSource(const Common::String &path, int size, FaceProbes probes,
							  const char *what, const char *fallback, int pixel = 0);

	/**
	 * With a UTF-8 translation: every face of @p s's chain that opens,
	 * then the .uni bundle (in the faces' cell), as one
	 * FallbackGlyphSource - or the one face alone - checked against the
	 * translation (one coverage warning per face). Shared by every font id
	 * with the same chain and owned here. nullptr when no face opens;
	 * @p chainName receives the faces that did, comma-separated.
	 */
	Graphics::UnicodeGlyphSource *faceChainFor(const FontSettings &s, Common::String &chainName,
											   Graphics::TtfGlyphSource **firstFace = nullptr);

	/**
	 * The baseline of font resource @p fontId in hi-res px below its line
	 * top (bitmapFontBaseline() times the hi-res scale), measured once;
	 * -1 when the game has no such font or it has none of the probes.
	 */
	int gameFontBaseline(GuiResourceId fontId);

	/** 64 code points of g_sci->translationCodePoints() (Graphics::CodePointSet::sample()). */
	const Common::Array<uint32> &translationSample();

	/** g_sci->translationCodePoints().fitProbes(): what the faces are fitted to. */
	const Common::Array<uint32> &translationFitProbes();

	/** The non-ASCII part of translationSample(): what the Unicode faces must draw. */
	Common::Array<uint32> coverageSample();

	/**
	 * checkCoverage() of @p src against @p sample, warned once per @p name
	 * (the design's two lines); @p sample is then left with what @p src
	 * lacks, for the next face of the chain.
	 */
	void checkFaceCoverage(Graphics::UnicodeGlyphSource *src, const Common::String &name,
						   const Common::String &fallback, Common::Array<uint32> &sample);

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
	/// The directory holding the map file: relative paths from the map
	/// ([fonts] entries, face paths) resolve against it.
	Common::Path _hiresMapDir;
	/// The ini latin keys were set: their "not in effect" warning is due
	/// (once) when a font ends up with no TrueType face.
	bool _iniLatinKeysSet;
	bool _iniLatinIgnoredWarned;
	/// Font ids already warned about a map Latin mode with no face.
	Common::HashMap<int, bool> _latinNoFaceWarned;

	/// TrueType sources by "path|size|hangul"; nullptr = failed (warned).
	Common::HashMap<Common::String, Graphics::TtfGlyphSource *> _ttfSources;
	/// Unicode bundles built on those sources, by their faces and routing.
	Common::HashMap<Common::String, GfxFontUnicode *> _ttfBundles;
	Common::HashMap<int, int> _gameBaselines;  ///< gameFontBaseline(), by font id
	/// The .uni bundle.
	GfxFontUnicode *_uniBundle;
	bool _uniBundleTried;

	bool _textLogResolved;
	bool _textLog;

	bool _layoutRulesResolved;
	Graphics::BreakRules _layoutRules;
	bool _sampleResolved;
	Common::Array<uint32> _sample;
	bool _fitProbesResolved;
	Common::Array<uint32> _fitProbes;
	/// Faces already checked for coverage (and warned about), by name.
	Common::HashMap<Common::String, bool> _coverageChecked;
	/// Face chains by faceChainKey() (size, .uni behind, faces); each is a face in _ttfSources or one of _chainParts.
	Common::HashMap<Common::String, Graphics::UnicodeGlyphSource *> _chains;
	/// The FallbackGlyphSources and .uni wrappers the chains are made of, owned.
	Common::Array<Graphics::UnicodeGlyphSource *> _chainParts;
	/**
	 * Fonts an adapter wraps but does not own. They are not in _cachedFonts
	 * (only the adapter is), so the cache has to delete them separately.
	 */
	Common::Array<GfxFont *> _ownedFonts;
	ViewCache _cachedViews;
};

} // End of namespace Sci

#endif // SCI_GRAPHICS_CACHE_H
