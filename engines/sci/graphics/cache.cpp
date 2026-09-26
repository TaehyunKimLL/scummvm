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

#include "common/util.h"
#include "common/stack.h"
#include "graphics/primitives.h"

#include "sci/sci.h"
#include "sci/engine/state.h"
#include "sci/engine/selector.h"
#include "sci/graphics/cache.h"
#include "sci/graphics/scifont.h"
#include "sci/graphics/fontsjis.h"
#include "sci/graphics/fontkorean.h"
#include "sci/graphics/fontset.h"
#include "sci/graphics/fontunicode.h"
#include "sci/graphics/glyphsource_routed.h"
#include "sci/graphics/glyphsource_ttf.h"
#include "sci/graphics/textlatin.h"
#include "common/config-manager.h"
#include "common/debug.h"
#include "common/file.h"
#include "common/platform.h"
#include "common/fs.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "sci/graphics/view.h"

namespace Sci {

namespace {

// hires_text_font is honoured only for SCI16 games in a CJK code page. The
// key is the face the Korean/Japanese/Chinese hi-res text is drawn in; a
// Western or SCI32 game has no such text, and a TTF face there would replace
// fonts it was never meant to. Interim: once the hires_text master switch
// (HIRES_COMPOSITOR_DESIGN.md, step 3) exists, that switch decides instead.
bool hiresTextFontApplies(Common::String &why) {
	if (getSciVersion() >= SCI_VERSION_2) {
		why = "SCI32 games do not support it yet";
		return false;
	}
	switch (g_sci->getSciLanguageCodePage()) {
	case Common::kWindows949:
	case Common::kWindows932:
	case Common::kWindows936:
	case Common::kWindows950:
		return true;
	default:
		why = "the game's language has no hi-res CJK text";
		return false;
	}
}

// The range hires_text_font_size may ask for (the default, 16, is
// FontSettings').
const int kHiresTextFontMinSize = 8;
const int kHiresTextFontMaxSize = 64;

} // End of anonymous namespace

GfxCache::GfxCache(ResourceManager *resMan, GfxScreen *screen, GfxPalette *palette)
	: _resMan(resMan), _screen(screen), _palette(palette),
	  _hiresResolved(false), _hiresApplies(false), _hiresMapLoaded(false),
	  _iniLatinKeysSet(false), _iniLatinIgnoredWarned(false),
	  _uniBundle(nullptr), _uniBundleTried(false),
	  _textLogResolved(false), _textLog(false) {
}

void GfxCache::resolveHiresText() {
	if (_hiresResolved)
		return;
	_hiresResolved = true;

	// The game's own domain only: ConfMan.hasKey(key) also finds a key set
	// in [scummvm], which would turn the face on for every game.
	const Common::String &domain = ConfMan.getActiveDomainName();
	const Common::Path gameDir = ConfMan.getPath("path", domain);

	Common::String why;
	_hiresApplies = hiresTextFontApplies(why);

	_iniLatinKeysSet = ConfMan.hasKey("hires_text_latin", domain) ||
		ConfMan.hasKey("hires_text_latin_space", domain) ||
		ConfMan.hasKey("hires_text_latin_font", domain) ||
		ConfMan.hasKey("hires_text_metrics", domain);

	// hires_text.map: the file hires_text_map names, else the game
	// directory's own. Only a map that is asked for or present is mentioned.
	const bool mapKeySet = ConfMan.hasKey("hires_text_map", domain);
	// An empty hires_text_map= names nothing, as an empty hires_text_font
	// does; FSNode would take it for the current directory.
	const bool mapKeyEmpty = mapKeySet && ConfMan.get("hires_text_map", domain).empty();
	Common::FSNode mapNode;
	if (mapKeySet) {
		if (!mapKeyEmpty)
			mapNode = Common::FSNode(Common::Path(ConfMan.get("hires_text_map", domain), Common::Path::kNativeSeparator));
	} else {
		mapNode = Common::FSNode(gameDir).getChild("hires_text.map");
	}

	if (!_hiresApplies) {
		if (ConfMan.hasKey("hires_text_font", domain))
			warning("hires_text_font is ignored: %s", why.c_str());
		if (_iniLatinKeysSet)
			warning("hires_text_latin is ignored: hires_text_font is not in effect");
		if (mapKeySet || mapNode.exists())
			warning("hires_text.map is ignored: %s", why.c_str());
		// Nothing applies: every font id gets the defaults, i.e. today's
		// behaviour, and _iniLatinKeysSet has been answered already.
		_iniLatinKeysSet = false;
		return;
	}

	// The map, parsed with the platform code as its one qualifier, so
	// [font.N:<platform>] wins over [font.N] on that platform only.
	if (mapKeySet || mapNode.exists()) {
		Common::String error;
		Common::SeekableReadStream *stream = nullptr;
		if (mapKeyEmpty)
			error = "empty path";
		else if (!mapNode.exists())
			error = "does not exist";
		else if (mapNode.isDirectory())
			error = "is a directory";
		else if (!(stream = mapNode.createReadStream()))
			error = "could not open the file";

		if (stream) {
			Common::Array<Common::String> qualifiers;
			const char *platform = Common::getPlatformCode(g_sci->getPlatform());
			if (platform && *platform)
				qualifiers.push_back(platform);
			// Relative paths in the map are the map's own: they resolve
			// against its directory, which for the game directory's
			// hires_text.map is the game directory.
			_hiresMapDir = mapNode.getParent().getPath();
			_hiresMapLoaded = Graphics::HiResFontMap::loadFromStream(*stream, _hiresMapDir,
																	 qualifiers, _hiresMap);
			delete stream;
			if (_hiresMapLoaded) {
				warnScummOnlyMapKeys(_hiresMap);
				debug(1, "SCI: hires_text.map %s loaded (platform '%s'), %u font id sections",
					  mapNode.getPath().toString().c_str(), platform ? platform : "",
					  (uint)_hiresMap.fontIds.size());
			} else {
				_hiresMap.clear();
				error = "is not a valid map";
			}
		}
		if (mapKeyEmpty)
			warning("hires_text_map: empty path; no map is used");
		else if (!_hiresMapLoaded)
			warning("hires_text.map %s: %s; ignoring it", mapNode.getPath().toString().c_str(), error.c_str());
	}

	// The ini keys: global overrides of the map, each validated here once.
	if (ConfMan.hasKey("hires_text_font", domain)) {
		_hiresIni.hasFont = true;
		_hiresIni.font = ConfMan.get("hires_text_font", domain);
		if (_hiresIni.font.empty()) {
			// An empty value names nothing; FSNode would warn about it itself.
			warning("hires_text_font: empty path; using the .uni fonts");
		}
	}

	// Parsed by hand: ConfMan.getInt() calls error() on non-numeric text.
	if (ConfMan.hasKey("hires_text_font_size", domain)) {
		const Common::String &value = ConfMan.get("hires_text_font_size", domain);
		char *end = nullptr;
		const long size = strtol(value.c_str(), &end, 10);
		if (value.empty() || *end != '\0' ||
			size < kHiresTextFontMinSize || size > kHiresTextFontMaxSize) {
			// Ignored, so the map's size (else the default) still applies.
			warning("hires_text_font_size '%s' is not a number from %d to %d; ignoring it",
					value.c_str(), kHiresTextFontMinSize, kHiresTextFontMaxSize);
		} else {
			_hiresIni.hasFontSize = true;
			_hiresIni.fontSize = (int)size;
		}
	}

	if (ConfMan.hasKey("hires_text_latin", domain)) {
		const Common::String &value = ConfMan.get("hires_text_latin", domain);
		_hiresIni.hasLatin = true;
		if (value == "off") {
			_hiresIni.latin = kLatinOff;
		} else if (value == "half") {
			_hiresIni.latin = kLatinHalf;
		} else if (value == "fullwidth") {
			_hiresIni.latin = kLatinFullwidth;
		} else if (value == "proportional") {
			_hiresIni.latin = kLatinProportional;
		} else {
			warning("hires_text_latin '%s' is not off, half, fullwidth or proportional; using off", value.c_str());
			_hiresIni.latin = kLatinOff;
		}
	}

	if (ConfMan.hasKey("hires_text_latin_space", domain)) {
		const Common::String &value = ConfMan.get("hires_text_latin_space", domain);
		_hiresIni.hasLatinSpace = true;
		if (value == "keep") {
			_hiresIni.latinFullwidthSpace = false;
		} else if (value == "fullwidth") {
			_hiresIni.latinFullwidthSpace = true;
		} else {
			warning("hires_text_latin_space '%s' is not keep or fullwidth; using keep", value.c_str());
			_hiresIni.latinFullwidthSpace = false;
		}
	}

	if (ConfMan.hasKey("hires_text_latin_font", domain)) {
		_hiresIni.hasLatinFont = true;
		_hiresIni.latinFont = ConfMan.get("hires_text_latin_font", domain);
		if (_hiresIni.latinFont.empty()) {
			// An empty value names nothing; the main face draws Latin text.
			warning("hires_text_latin_font: empty path; the main face draws Latin text");
		}
	}

	if (ConfMan.hasKey("hires_text_metrics", domain)) {
		const Common::String &value = ConfMan.get("hires_text_metrics", domain);
		if (value == "game") {
			_hiresIni.hasMetrics = true;
			_hiresIni.metrics = Graphics::kHiResMetricsGame;
		} else if (value == "font") {
			_hiresIni.hasMetrics = true;
			_hiresIni.metrics = Graphics::kHiResMetricsFont;
		} else {
			warning("hires_text_metrics '%s' is not game or font; ignoring it", value.c_str());
		}
	}
}

FontSettings GfxCache::fontSettingsFor(GuiResourceId fontId) {
	resolveHiresText();
	if (!_hiresApplies)
		return FontSettings();
	return resolveFontSettings(_hiresMap, _hiresMapLoaded, fontId, _hiresIni, _hiresMapDir);
}

TtfGlyphSource *GfxCache::ttfSource(const Common::String &path, int size, bool requireHangul,
									const char *what, const char *fallback) {
	const Common::String key = Common::String::format("%s|%d|%d", path.c_str(), size, requireHangul ? 1 : 0);
	if (_ttfSources.contains(key))
		return _ttfSources[key];
	// A face without the Hangul check is satisfied by one that passed it.
	if (!requireHangul) {
		const Common::String checked = Common::String::format("%s|%d|1", path.c_str(), size);
		if (_ttfSources.contains(checked) && _ttfSources[checked])
			return _ttfSources[checked];
	}

	Common::String error;
	TtfGlyphSource *src = nullptr;
	Common::FSNode node(Common::Path(path, Common::Path::kNativeSeparator));
	// Checked with exists() and isDirectory() before createReadStream():
	// that call emits its own "FSNode::createReadStream: ..." warning for an
	// absent node or a directory, which would give run.log two warnings for
	// one bad path. A node that still fails to open (permissions, ...) goes
	// through createReadStream() and gets our single warning below.
	if (path.empty()) {
		error = "empty path";
	} else if (!node.exists()) {
		error = "does not exist";
	} else if (node.isDirectory()) {
		error = "is a directory";
	} else if (Common::SeekableReadStream *stream = node.createReadStream()) {
		const uint32 startMs = g_system->getMillis();
		src = TtfGlyphSource::create(stream, DisposeAfterUse::YES, size, error, requireHangul);
		const uint32 elapsedMs = g_system->getMillis() - startMs;
		if (src)
			debug(1, "SCI: %s %s opened at %dpx in %u ms", what, path.c_str(), size, elapsedMs);
	} else {
		error = "could not open the file";
	}

	// A failure is remembered too, so each (path, size) warns once, even
	// though purgeFontCache() rebuilds the font sets.
	if (!src)
		warning("%s %s: %s; %s", what, path.c_str(), error.c_str(), fallback);
	_ttfSources[key] = src;
	return src;
}

GfxFontUnicode *GfxCache::loadUniBundle() {
	if (!_uniBundleTried) {
		_uniBundleTried = true;
		GfxFontUnicode *f = new GfxFontUnicode(_screen, 0);
		static const char *const names[] = { "sci.uni", "korean.uni", "towns.uni" };
		bool ok = false;
		for (uint i = 0; i < ARRAYSIZE(names) && !ok; i++)
			ok = f->load(names[i]);
		if (ok) {
			_uniBundle = f;
		} else {
			delete f;
		}
	}
	return _uniBundle;
}

GfxFontUnicode *GfxCache::unicodeFaceFor(GuiResourceId fontId, FontSettings &s) {
	// A face on disk, tried before the bundled .uni fonts: a live face is
	// preferred when the player (or the map) asked for one, and the .uni
	// names remain the fallback, both when no face is named (identical to
	// before) and when it fails to load (one warning, never a hard error -
	// the game must still start). A Korean game needs Hangul from its main
	// face: one that has none is refused, and the fallback serves instead.
	const bool requireHangul = g_sci->getSciLanguageCodePage() == Common::kWindows949;
	Common::String mainPath = s.facePath;
	TtfGlyphSource *main = nullptr;
	if (!mainPath.empty()) {
		// A face only this font id names ([font.N] face=) falls back to the
		// face every other id gets (the ini key, else [hires] font=), then
		// to the .uni fonts. Font id -1 is never a [font.N] section.
		const FontSettings global = fontSettingsFor(-1);
		const bool haveGlobal = !global.facePath.empty() && global.facePath != mainPath;
		const Common::String toGlobal = "using " + global.facePath;
		main = ttfSource(mainPath, s.size, requireHangul, "hires_text_font",
						 haveGlobal ? toGlobal.c_str() : "using the .uni fonts");
		if (!main && haveGlobal) {
			mainPath = global.facePath;
			main = ttfSource(mainPath, s.size, requireHangul, "hires_text_font", "using the .uni fonts");
		}
	}
	// The set carries what is actually drawn.
	s.facePath = main ? mainPath : Common::String();

	if (!main) {
		// Latin modes only mean anything once the id has a live TrueType
		// face: with none, there is no face for ASCII to be routed to.
		if (s.latin != kLatinOff) {
			if (_iniLatinKeysSet && _hiresIni.hasLatin) {
				if (!_iniLatinIgnoredWarned)
					warning("hires_text_latin is ignored: hires_text_font is not in effect");
				_iniLatinIgnoredWarned = true;
			} else if (!_latinNoFaceWarned.contains(fontId)) {
				warning("hires_text.map: font %d has a Latin mode but no TrueType face; "
						"the game's font draws its Latin text", fontId);
				_latinNoFaceWarned[fontId] = true;
			}
		} else if (_iniLatinKeysSet && !_iniLatinIgnoredWarned) {
			// As before: any hires_text_latin* key, even =off, is reported
			// once when hires_text_font is not a live TrueType face.
			warning("hires_text_latin is ignored: hires_text_font is not in effect");
			_iniLatinIgnoredWarned = true;
		}
		s.latin = kLatinOff;
		s.latinFacePath.clear();
		return loadUniBundle();
	}

	// hires_text_latin_font / [latin] font=: a second face for the Latin
	// range, wrapped together with the main face in a RoutedGlyphSource.
	// Absent (or the same file), the main face draws that range too
	// (glyphChar()/faceFor() send it code points the main face can already
	// answer for, so no second source is needed).
	TtfGlyphSource *latin = nullptr;
	if (s.latin != kLatinOff && !s.latinFacePath.empty() && s.latinFacePath != mainPath) {
		latin = ttfSource(s.latinFacePath, s.size, false, "hires_text_latin_font",
						  "the main face draws Latin text");
	}

	// The set carries what is drawn: no Latin face when the mode is off or
	// the face failed (or is the main face itself) - the main face then
	// draws the Latin range.
	if (!latin)
		s.latinFacePath.clear();

	// One router per Latin mode (see unicodeBundleKey()).
	const Common::String key = unicodeBundleKey(mainPath, s.size, s.latinFacePath, s.latin);
	if (_ttfBundles.contains(key))
		return _ttfBundles[key];

	GfxFontUnicode *f = new GfxFontUnicode(_screen, 0);
	if (latin) {
		// The router owns neither face: both stay in _ttfSources, shared.
		f->setSource(new RoutedGlyphSource(main, latin, s.latin, DisposeAfterUse::NO), mainPath);
		debug(1, "SCI: font %d routes Latin text to %s", fontId, s.latinFacePath.c_str());
	} else {
		f->setSource(main, mainPath, DisposeAfterUse::NO);
	}
	_ttfBundles[key] = f;
	return f;
}

bool GfxCache::isTextLogEnabled() {
	if (!_textLogResolved) {
		_textLogResolved = true;
		// Deliberately unconditional: no hiresTextFontApplies() check. Task
		// 1 of HIRES_COMPOSITOR_PLAN_3_MAP_PROPORTIONAL needs the font id
		// logged on English games too, which the scope predicate would
		// otherwise refuse hires_text_font itself on.
		const Common::String &domain = ConfMan.getActiveDomainName();
		_textLog = ConfMan.hasKey("hires_text_log", domain) &&
				   ConfMan.getBool("hires_text_log", domain);
	}
	return _textLog;
}

GfxCache::~GfxCache() {
	purgeFontCache();
	purgeViewCache();

	// The Unicode bundles and their TrueType sources outlive purges (every
	// font set holds them unowned), but not the cache. Bundles first: a
	// bundle's router points at the sources.
	for (Common::HashMap<Common::String, GfxFontUnicode *>::iterator it = _ttfBundles.begin();
		 it != _ttfBundles.end(); ++it)
		delete it->_value;
	_ttfBundles.clear();
	for (Common::HashMap<Common::String, TtfGlyphSource *>::iterator it = _ttfSources.begin();
		 it != _ttfSources.end(); ++it)
		delete it->_value;
	_ttfSources.clear();
	delete _uniBundle;
	_uniBundle = nullptr;
}

void GfxCache::purgeFontCache() {
	for (FontCache::iterator iter = _cachedFonts.begin(); iter != _cachedFonts.end(); ++iter) {
		delete iter->_value;
		iter->_value = 0;
	}

	_cachedFonts.clear();

	// Fonts wrapped by an adapter are not in _cachedFonts, so they would
	// otherwise leak - and must be deleted AFTER the adapters that point at
	// them.
	for (uint i = 0; i < _ownedFonts.size(); i++)
		delete _ownedFonts[i];
	_ownedFonts.clear();

	// The shared Unicode bundles are kept: they are keyed by their faces,
	// identical for every set rebuilt after the purge, and reopening a
	// TrueType face costs far more than keeping it.
}

void GfxCache::purgeViewCache() {
	for (ViewCache::iterator iter = _cachedViews.begin(); iter != _cachedViews.end(); ++iter) {
		delete iter->_value;
		iter->_value = 0;
	}

	_cachedViews.clear();
}

bool GfxCache::fontIsSet(GuiResourceId fontId) {
	return dynamic_cast<GfxFontSet *>(getFont(fontId)) != nullptr;
}

GfxFont *GfxCache::createFontSet(GuiResourceId fontId) {
	// Stage 2 of docs/i18n/M10_FONTSET.md: the face the script asked for,
	// followed by whatever else can cover characters it cannot. Order is the
	// contract - the game's own face is first, so single-byte text is drawn
	// by the glyphs the game shipped and keeps its metrics.
	const bool haveResource = _resMan->testResource(ResourceId(kResourceTypeFont, fontId));
	if (!haveResource)
		return nullptr;

	// This font id's own hi-res settings (ini keys, then hires_text.map),
	// and the Unicode face they name. unicodeFaceFor() turns the Latin mode
	// off when the id ends up with no TrueType face, so the set is built with
	// what will actually be drawn.
	FontSettings settings = fontSettingsFor(fontId);
	GfxFontUnicode *uni = unicodeFaceFor(fontId, settings);
	if (_hiresApplies)
		debug(1, "SCI: font %d hi-res settings: face '%s' %dpx, latin %d (face '%s', space %s, metrics %s)",
			  fontId, settings.facePath.c_str(), settings.size, (int)settings.latin,
			  settings.latinFacePath.c_str(), settings.fullwidthSpace ? "fullwidth" : "keep",
			  settings.metrics == Graphics::kHiResMetricsFont ? "font" : "game");

	GfxFontSet *set = new GfxFontSet(fontId, g_sci->getSciLanguageCodePage(), settings);
	set->addFace(new GfxFontFromResource(_resMan, _screen, fontId), GfxFontSet::kFaceResource);

	// The legacy double-byte faces, when the game ships their font file.
	// GfxFontKorean and GfxFontSjis call error() on a missing file, so
	// existence is checked rather than assumed.
	switch (g_sci->getLanguage()) {
	case Common::KO_KOR:
		if (Common::File::exists(Common::Path("korean.fnt")))
			// GfxFontKorean and GfxFontSjis already halve their own metrics below
		// SCI2, so they must NOT be marked as hires-plane faces here - doing
		// so halves twice and the glyphs pile up on top of each other.
		set->addFace(new GfxFontKorean(_screen, 1001), GfxFontSet::kFaceLegacyDbcs);
		break;
	case Common::JA_JPN:
		if (Common::File::exists(Common::Path("SJIS.FNT")))
			set->addFace(new GfxFontSjis(_screen, 900), GfxFontSet::kFaceLegacyDbcs);
		break;
	default:
		break;
	}

	// The Unicode face last: it is the widest, and being last means it only
	// answers for characters nothing else covers. Shared, so unowned.
	if (uni)
		set->addFace(uni, GfxFontSet::kFaceCodePoint, false, true);

	return set;
}

GfxFont *GfxCache::createUnicodeFont(GuiResourceId fontId) {
	FontSettings settings = fontSettingsFor(fontId);
	GfxFontUnicode *uni = unicodeFaceFor(fontId, settings);
	if (!uni)
		return nullptr;

	// The legacy CJK font, where the language has one, remains the fallback so
	// a bundle with partial coverage degrades to the old rendering instead of
	// to blank space. The resource font is the fallback otherwise, which also
	// keeps single-byte text pixel-identical to before.
	// The legacy CJK fonts abort the engine when their font file is absent
	// (GfxFontSjis calls error() on a missing SJIS.FNT), so they are only used
	// as the fallback when that file is actually present. Otherwise the
	// resource font serves, which is also what keeps single-byte text
	// identical to the unmodified engine.
	GfxFont *fallback = nullptr;
	if (fontId == 1001 && g_sci->getLanguage() == Common::KO_KOR &&
		Common::File::exists(Common::Path("korean.fnt")))
		fallback = new GfxFontKorean(_screen, fontId);
	else if (fontId == 900 && g_sci->getLanguage() == Common::JA_JPN &&
			 Common::File::exists(Common::Path("SJIS.FNT")))
		fallback = new GfxFontSjis(_screen, fontId);
	else if (_resMan->testResource(ResourceId(kResourceTypeFont, fontId)))
		fallback = new GfxFontFromResource(_resMan, _screen, fontId);
	// No fallback at all when the id names no resource. GfxText16 switches to
	// the legacy CJK font id (1001/900) to get double-byte glyphs, and most
	// games have no such resource - GfxFontFromResource would abort with
	// "font resource N not found". The adapter copes with a null fallback;
	// single-byte characters then have no glyph, which is correct, because
	// the game only ever switches to that id for double-byte text.
	if (fallback)
		_ownedFonts.push_back(fallback);

	return new GfxFontUnicodeAdapter(uni, g_sci->getSciLanguageCodePage(),
									 fallback, fontId, settings.latin, settings.fullwidthSpace,
									 settings.metrics);
}

GfxFont *GfxCache::getFont(GuiResourceId fontId) {
	if (_cachedFonts.size() >= MAX_CACHED_FONTS)
		purgeFontCache();

	if (!_cachedFonts.contains(fontId)) {
		// A SCVMUNI bundle, when present, serves ANY font the game asks for.
		//
		// The legacy CJK fonts are reachable only through one hard-coded id
		// each - 1001 for Korean, 900 for Shift-JIS - and a game that does not
		// request that id never gets them. Measured: KQ5's Japanese FM-TOWNS
		// release contains fonts 0, 1, 4, 8, 9, 69, 600 and 999 and NO font
		// 900, all of them 128-glyph single-byte fonts, so the SJIS path is
		// unreachable there by construction. Keying the Unicode font on the
		// bundle rather than on a font number avoids that trap.
		// A set is preferred whenever the id names a real font resource: it
		// keeps the face the script chose and adds the others behind it.
		GfxFont *font = createFontSet(fontId);

		// Ids that name no resource are the legacy CJK ones the renderer
		// switches to. They get the adapter, which copes with having no
		// resource face at all.
		if (!font)
			font = createUnicodeFont(fontId);

		// Create special Korean font in korean games, when font 1001 is selected
		if (!font && (fontId == 1001) && (g_sci->getLanguage() == Common::KO_KOR))
			font = new GfxFontKorean(_screen, fontId);
		// Create special SJIS font in japanese games, when font 900 is selected
		if (!font && (fontId == 900) && (g_sci->getLanguage() == Common::JA_JPN))
			font = new GfxFontSjis(_screen, fontId);

		if (!font)
			font = new GfxFontFromResource(_resMan, _screen, fontId);

		_cachedFonts[fontId] = font;
	}

	return _cachedFonts[fontId];
}

GfxView *GfxCache::getView(GuiResourceId viewId) {
	if (_cachedViews.size() >= MAX_CACHED_VIEWS)
		purgeViewCache();

	if (!_cachedViews.contains(viewId))
		_cachedViews[viewId] = new GfxView(_resMan, _screen, _palette, viewId);

	return _cachedViews[viewId];
}

int16 GfxCache::kernelViewGetCelWidth(GuiResourceId viewId, int16 loopNo, int16 celNo) {
	return getView(viewId)->getCelInfo(loopNo, celNo)->scriptWidth;
}

int16 GfxCache::kernelViewGetCelHeight(GuiResourceId viewId, int16 loopNo, int16 celNo) {
	return getView(viewId)->getCelInfo(loopNo, celNo)->scriptHeight;
}

int16 GfxCache::kernelViewGetLoopCount(GuiResourceId viewId) {
#ifdef ENABLE_SCI32
	if (getSciVersion() >= SCI_VERSION_2) {
		return CelObjView::getNumLoops(viewId);
	}
#endif
	return getView(viewId)->getLoopCount();
}

int16 GfxCache::kernelViewGetCelCount(GuiResourceId viewId, int16 loopNo) {
#ifdef ENABLE_SCI32
	if (getSciVersion() >= SCI_VERSION_2) {
		return CelObjView::getNumCels(viewId, loopNo);
	}
#endif
	return getView(viewId)->getCelCount(loopNo);
}

} // End of namespace Sci
