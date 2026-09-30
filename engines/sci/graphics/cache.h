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
#include "graphics/hires_text/hires_options.h"
#include "graphics/hires_text/text_layout.h"
#include "sci/graphics/hirestextsettings.h"

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
	 * which face kind (see textlatin.h's TextFaceKind) drew each glyph.
	 *
	 * Resolved once and cached here, so GfxText16 never
	 * does a ConfMan lookup per character. Unlike hires_text_face, this is
	 * read unconditionally - NOT gated on the hi-res scope predicate - so an
	 * English game can still log which font id drew its text.
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
	 * The Unicode face for a font id with settings @p s (design sections 5,
	 * 6, 8): @p s.plan's id chain and every range rule it names, opened and
	 * routed through one Graphics::RangeRoutedGlyphSource (Graphics::
	 * pickGlyph(), with no `borrowed` array - SCI's empty-chain fallback is
	 * the shared .uni bundle, folded into every retained chain instead, see
	 * faceChainFor()), or the shared .uni bundle alone when the id names no
	 * face at all. nullptr for @p s.original (the resource font only, not
	 * even .uni) or when nothing opens.
	 */
	GfxFontUnicode *unicodeFaceFor(GuiResourceId fontId, FontSettings &s);

	/** The .uni bundle (sci.uni, korean.uni, towns.uni), loaded at most
	 *  once; nullptr when the game ships none. */
	GfxFontUnicode *loadUniBundle();

	/**
	 * design section 6.4: give @p f the box (GfxFontUnicode::setMissing()),
	 * warning once (in @p map's own warnings, design section 10) when
	 * @p name has no glyph to draw it with. Only for the faceless path
	 * (a bare .uni bundle, which does not itself know about missing=): a
	 * font id with its own chain already has missing= baked into its
	 * Graphics::RangeRoutedGlyphSource by Graphics::pickGlyph() and does not
	 * call this.
	 */
	void applyMissing(GfxFontUnicode *f, uint32 missing, const Common::String &name);

	/** Which characters a face is checked and fitted with when it opens. */
	enum FaceProbes {
		kProbesDefault = 0,     ///< the fixed Hangul/Latin fit set, no check
		kProbesHangul = 1,      ///< the same, and the face must draw Hangul (a legacy Korean game)
		kProbesTranslation = 2  ///< the fixed set plus a sample of the translation's characters
	};

	/**
	 * The TrueType source for @p path at @p size, opened at most once per
	 * (path, size, probes, pixel) and shared; nullptr when it fails to open,
	 * with one warning per such key (@p what names the setting in it).
	 *
	 * @param pixel          > 0 opens the face as a pixel font of that design
	 *                       size in a size cell (TtfGlyphSource::createPixel());
	 *                       probes then do not apply
	 * @param explicitProbes when given, the face is fitted to exactly these
	 *                       code points instead of @p probes' fixed set (a
	 *                       `[glyphs]` target face, design 6.7: "its fit
	 *                       probes = its target cps")
	 */
	Graphics::TtfGlyphSource *ttfSource(const Common::String &path, int size, FaceProbes probes,
							  const char *what, const char *fallback, int pixel = 0,
							  const Common::Array<uint32> *explicitProbes = nullptr);

	/**
	 * The SVFN bitmap font at @p path when the file is one (its header,
	 * Graphics::isSvfnFile()): @p isSvfn is set, and the source is opened at
	 * most once per path - a bitmap font has its own size - and shared;
	 * nullptr when it fails to load, with one warning per path, worded as
	 * ttfSource()'s. @p isSvfn false (and nullptr) for any other file, which
	 * is then the caller's to open as a TrueType face.
	 */
	Graphics::UnicodeGlyphSource *svfnSource(const Common::String &path, const char *what, const char *fallback,
											 bool &isSvfn);

	/**
	 * One face of a plan's chain (design section 5.4): the SVFN bitmap font
	 * at @p path (svfnSource()), refused with one warning under
	 * kProbesHangul when it has no Hangul, else the TrueType face
	 * (ttfSource(), then also returned in @p ttf; nullptr for a bitmap face).
	 */
	Graphics::UnicodeGlyphSource *singleFace(const Common::String &path, int size, FaceProbes probes,
											 const char *fallback, int pixel, Graphics::TtfGlyphSource *&ttf);

	/**
	 * @p s's Unicode routing face (design sections 5, 6, 8): every face
	 * @p s.plan names - the id chain, every range rule's chain, every
	 * `[glyphs]` target - opened (singleFace()/svfnSource()/ttfSource(), a
	 * target TrueType face fitted to just its own target code points), laid
	 * out in one shared cell (Graphics::layoutFaceChain(), over the union of
	 * every one of those faces plus the .uni bundle) and routed by one
	 * Graphics::RangeRoutedGlyphSource. The .uni bundle is appended as the
	 * last entry of every chain that does not end in `original`, so it
	 * stands behind the id's own faces exactly where the id chain's own
	 * emptiness would otherwise have sent every character. Runs
	 * checkPlanLoadWarnings() once every named face has been opened or
	 * failed. Shared by every font id whose plan, size and faces are
	 * identical (unicodeBundleKey()) and owned here (_chainParts).
	 *
	 * @param firstRaw   set to the id chain's own first opened face, before
	 *                   any chain-wide normalisation - what GlyphPlacement
	 *                   (design 5.4's C41) measures against, distinct from
	 *                   the shared routing surface this returns; null when
	 *                   nothing opened.
	 * @return nullptr when nothing opens (an empty id chain, or every named
	 *         face failed); @p chainName then still names what was tried.
	 */
	Graphics::UnicodeGlyphSource *faceChainFor(GuiResourceId fontId, const FontSettings &s,
											   Common::String &chainName, Graphics::UnicodeGlyphSource *&firstRaw);

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
	 * Reads the hi-res text ini keys (design section 11,
	 * Graphics::readHiResIniFromConfMan()) and hires_text.map (the game
	 * directory's own default name, or the file the ini names, design
	 * section 4) once per engine run, under the scope predicate (SCI16, a
	 * CJK code page or a UTF-8 translation, the game's own domain). Out of
	 * scope, every key and the map get one warning each and nothing
	 * applies. hires_text=false leaves the layer as if no map and no ini
	 * keys had been given at all. Each bad value gets one warning and is
	 * ignored.
	 */
	void resolveHiresText();

	/** resolveFontSettings() for @p fontId from what resolveHiresText() read. */
	FontSettings fontSettingsFor(GuiResourceId fontId);

	bool _hiresResolved;
	bool _hiresApplies;              ///< the scope predicate held
	Graphics::HiResIniOverrides _hiresIni;
	Graphics::HiResMap _hiresMap;
	bool _hiresMapLoaded;
	/// The directory holding the map file: relative paths from the map
	/// ([fonts] entries, face paths) resolve against it (design section 4).
	Common::Path _hiresMapDir;
	/// The game's own folder: the ini hires_text_face's own path entries
	/// resolve against it (design section 4).
	Common::Path _hiresGameDir;
	/// design 10's "once per cause per load": every load-time warning text
	/// checkPlanLoadWarnings() has already printed this load. Causes are
	/// map-wide (missing=, a target), so this is shared across every font
	/// id resolved from the same load rather than per id.
	Common::HashMap<Common::String, bool> _warnedOnceThisLoad;
	/// The shared .uni bundle has already had its one missing= wrap applied
	/// (applyMissing() on it is otherwise idempotent, but which id's
	/// missing= value wins if they differ is only meaningful once).
	bool _uniBundleMissingApplied;

	/// TrueType sources by "path|size|probes[|pN][|tN,N,...]"; nullptr = failed (warned).
	Common::HashMap<Common::String, Graphics::TtfGlyphSource *> _ttfSources;
	/// SVFN bitmap faces by path; nullptr = an SVFN file that failed (warned).
	Common::HashMap<Common::String, Graphics::UnicodeGlyphSource *> _svfnSources;
	/// Paths already found not to be SVFN files (svfnSource()).
	Common::HashMap<Common::String, bool> _notSvfn;
	/// SVFN faces refused as a Korean game's main face (warned once).
	Common::HashMap<Common::String, bool> _svfnNoHangul;
	/// Unicode bundles built on those sources, by their faces, size and plan hash.
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
	/// Routing sources (Graphics::RangeRoutedGlyphSource) by faceChainFor()'s
	/// own key (faces, size, plan hash); each is also in _chainParts.
	Common::HashMap<Common::String, Graphics::UnicodeGlyphSource *> _chains;
	/// faceChainFor()'s own `firstRaw` out-param, by the same key: a cache
	/// hit still has to answer it without reopening anything.
	Common::HashMap<Common::String, Graphics::UnicodeGlyphSource *> _chainFirstFace;
	/// The NormalizedGlyphSources and RangeRoutedGlyphSources the chains are
	/// made of, owned.
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
