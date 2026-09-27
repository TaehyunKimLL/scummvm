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
#include "graphics/hires_text/coverage.h"
#include "graphics/hires_text/font_face.h"
#include "graphics/hires_text/glyph_source_fallback.h"
#include "graphics/hires_text/glyph_source_routed.h"
#include "graphics/hires_text/glyph_source_ttf.h"
#include "sci/graphics/textlayout16.h"
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
	  _textLogResolved(false), _textLog(false),
	  _layoutRulesResolved(false), _sampleResolved(false), _fitProbesResolved(false) {
}

void GfxCache::resolveHiresText() {
	if (_hiresResolved)
		return;
	_hiresResolved = true;

	// The game's own domain only: ConfMan.hasKey(key) also finds a key set
	// in [scummvm], which would turn the face on for every game.
	const Common::String &domain = ConfMan.getActiveDomainName();
	const Common::Path gameDir = ConfMan.getPath("path", domain);

	// hires_text_font and the map are honoured for an SCI16 game whose text
	// is a UTF-8 translation, or one in a legacy CJK code page: the text
	// decides, not the game's language (I18N_TEXT_DESIGN.md section 4.6).
	Common::String why;
	_hiresApplies = hiresTextApplies(getSciVersion(), g_sci->getSciLanguageCodePage(),
									 g_sci->heapStringsAreUtf8(), why);

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
		if (!mapKeyEmpty) {
			const Common::String value = ConfMan.get("hires_text_map", domain);
			// "data:" names a map shipped with ScummVM (HiResFontMap::resolvePath()).
			mapNode = Common::FSNode(Graphics::HiResFontMap::isDataPath(value)
										 ? Graphics::HiResFontMap::resolvePath(value, Common::Path())
										 : Common::Path(value, Common::Path::kNativeSeparator));
		}
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

/** Sci::LatinMode as the shared router in graphics/hires_text names it; the
 *  two enums list the same modes (hirestextsettings.cpp maps the other way). */
static Graphics::HiResLatinMode toHiResLatinMode(LatinMode mode) {
	switch (mode) {
	case kLatinHalf:
		return Graphics::kHiResLatinHalf;
	case kLatinFullwidth:
		return Graphics::kHiResLatinFullwidth;
	case kLatinProportional:
		return Graphics::kHiResLatinProportional;
	case kLatinOff:
	default:
		return Graphics::kHiResLatinOff;
	}
}

FontSettings GfxCache::fontSettingsFor(GuiResourceId fontId) {
	resolveHiresText();
	if (!_hiresApplies)
		return FontSettings();
	return resolveFontSettings(_hiresMap, _hiresMapLoaded, fontId, _hiresIni, _hiresMapDir);
}

Graphics::TtfGlyphSource *GfxCache::ttfSource(const Common::String &path, int size, FaceProbes probes,
									const char *what, const char *fallback, int pixel) {
	const bool requireHangul = probes == kProbesHangul;
	// A pixel font is another source of the same file and size.
	const Common::String pixelKey = pixel > 0 ? Common::String::format("|p%d", pixel) : Common::String();
	const Common::String key = Common::String::format("%s|%d|%d", path.c_str(), size, (int)probes) + pixelKey;
	if (_ttfSources.contains(key))
		return _ttfSources[key];
	// A face without the Hangul check is satisfied by one that passed it.
	if (probes == kProbesDefault) {
		const Common::String checked = Common::String::format("%s|%d|1", path.c_str(), size) + pixelKey;
		if (_ttfSources.contains(checked) && _ttfSources[checked])
			return _ttfSources[checked];
	}

	Common::String error;
	Graphics::TtfGlyphSource *src = nullptr;
	// openFontFace() checks exists() and isDirectory() before
	// createReadStream(): that call emits its own "FSNode::createReadStream:
	// ..." warning for an absent node or a directory, which would give
	// run.log two warnings for one bad path. A node that still fails to open
	// (permissions, ...) gets our single warning below. "<file>.ttc#<N>"
	// names face N of a collection (font_face.h).
	int32 faceIndex = 0;
	Common::SeekableReadStream *stream = nullptr;
	if (path.empty()) {
		error = "empty path";
	} else if ((stream = Graphics::openFontFace(Common::Path(path, Common::Path::kNativeSeparator), faceIndex, error))) {
		const uint32 startMs = g_system->getMillis();
		if (pixel > 0) {
			// Held on its grid: no fit, so no probes and no Hangul check.
			src = Graphics::TtfGlyphSource::createPixel(stream, DisposeAfterUse::YES, size, pixel, error, faceIndex);
		} else if (probes == kProbesTranslation) {
			// Fitted to the translation's own characters too (Thai marks,
			// Japanese brackets), not only to the fixed Hangul/Latin set.
			const Common::Array<uint32> &sample = translationFitProbes();
			src = Graphics::TtfGlyphSource::create(stream, DisposeAfterUse::YES, size, error, false, false,
												   sample.empty() ? nullptr : &sample[0], sample.size(), faceIndex);
		} else {
			src = Graphics::TtfGlyphSource::create(stream, DisposeAfterUse::YES, size, error, requireHangul, false,
												   nullptr, 0, faceIndex);
		}
		const uint32 elapsedMs = g_system->getMillis() - startMs;
		if (src) {
			// [hires] gamma=: off (100) unless the map asks.
			src->setCoverageGamma(_hiresMap.coverageGamma);
			debug(1, "SCI: %s %s opened at %dpx in %u ms", what, path.c_str(), size, elapsedMs);
		}
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
			// Per-glyph advance and placement for a UTF-8 translation only:
			// a legacy game keeps the bundle's cell widths to the pixel.
			f->setPerGlyph(g_sci->heapStringsAreUtf8());
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
	//
	// With a UTF-8 translation the whole face chain is opened and checked
	// against the translation's own characters instead (faceChainFor()).
	const bool utf8 = g_sci->heapStringsAreUtf8();
	const FaceProbes probes = g_sci->getSciLanguageCodePage() == Common::kWindows949 ? kProbesHangul : kProbesDefault;
	Common::String mainPath = s.facePath;
	Graphics::UnicodeGlyphSource *main = nullptr;
	if (utf8) {
		main = faceChainFor(s, mainPath);
	} else if (!mainPath.empty()) {
		// A face only this font id names ([font.N] face=) falls back to the
		// face every other id gets (the ini key, else [hires] font=), then
		// to the .uni fonts. Font id -1 is never a [font.N] section.
		const FontSettings global = fontSettingsFor(-1);
		const bool haveGlobal = !global.facePath.empty() && global.facePath != mainPath;
		const Common::String toGlobal = "using " + global.facePath;
		main = ttfSource(mainPath, s.size, probes, "hires_text_font",
						 haveGlobal ? toGlobal.c_str() : "using the .uni fonts", s.pixel);
		if (!main && haveGlobal) {
			// The global face with its own pixel= ([hires]), never this id's.
			mainPath = global.facePath;
			s.pixel = global.pixel;
			main = ttfSource(mainPath, s.size, probes, "hires_text_font", "using the .uni fonts", s.pixel);
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
		GfxFontUnicode *uni = loadUniBundle();
		if (utf8 && uni) {
			Common::Array<uint32> sample = coverageSample();
			checkFaceCoverage(uni->source(), "the .uni fonts", "the game's font", sample);
		}
		return uni;
	}

	// hires_text_latin_font / [latin] font=: a second face for the Latin
	// range, wrapped together with the main face in a RoutedGlyphSource.
	// Absent (or the same file), the main face draws that range too
	// (glyphChar()/faceFor() send it code points the main face can already
	// answer for, so no second source is needed).
	Graphics::TtfGlyphSource *latin = nullptr;
	if (s.latin != kLatinOff && !s.latinFacePath.empty() && s.latinFacePath != mainPath) {
		latin = ttfSource(s.latinFacePath, s.size, kProbesDefault, "hires_text_latin_font",
						  "the main face draws Latin text");
	}

	// The set carries what is drawn: no Latin face when the mode is off or
	// the face failed (or is the main face itself) - the main face then
	// draws the Latin range.
	if (!latin)
		s.latinFacePath.clear();

	// One router per Latin mode (see unicodeBundleKey()).
	Common::String key = unicodeBundleKey(mainPath, s.size, s.latinFacePath, s.latin);
	if (s.pixel > 0)
		key += Common::String::format("|p%d", s.pixel);
	if (_ttfBundles.contains(key))
		return _ttfBundles[key];

	GfxFontUnicode *f = new GfxFontUnicode(_screen, 0);
	f->setPerGlyph(utf8);
	if (latin) {
		// The router owns neither face: both stay in _ttfSources, shared.
		f->setSource(new Graphics::RoutedGlyphSource(main, latin, toHiResLatinMode(s.latin), DisposeAfterUse::NO), mainPath);
		debug(1, "SCI: font %d routes Latin text to %s", fontId, s.latinFacePath.c_str());
	} else {
		f->setSource(main, mainPath, DisposeAfterUse::NO);
	}
	_ttfBundles[key] = f;
	return f;
}

const Common::Array<uint32> &GfxCache::translationSample() {
	if (!_sampleResolved) {
		_sampleResolved = true;
		if (g_sci->heapStringsAreUtf8())
			g_sci->translationCodePoints().sample(64, _sample);
	}
	return _sample;
}

const Common::Array<uint32> &GfxCache::translationFitProbes() {
	if (!_fitProbesResolved) {
		_fitProbesResolved = true;
		if (g_sci->heapStringsAreUtf8())
			g_sci->translationCodePoints().fitProbes(Graphics::TtfGlyphSource::kMaxExtraFitProbes, _fitProbes);
	}
	return _fitProbes;
}

Common::Array<uint32> GfxCache::coverageSample() {
	// ASCII is left out: the game's own font draws it (the set's first
	// face), so a face or a bundle without it lacks nothing. The fit probes
	// keep it.
	const Common::Array<uint32> &all = translationSample();
	Common::Array<uint32> sample;
	for (uint i = 0; i < all.size(); i++) {
		if (all[i] >= 0x80)
			sample.push_back(all[i]);
	}
	return sample;
}

void GfxCache::checkFaceCoverage(Graphics::UnicodeGlyphSource *src, const Common::String &name,
								 const Common::String &fallback, Common::Array<uint32> &sample) {
	if (!src || sample.empty())
		return;
	const Graphics::CoverageReport r = Graphics::checkCoverage(src, sample);

	// What this face lacks is what the next one is asked for.
	Common::Array<uint32> left;
	for (uint i = 0; i < sample.size(); i++) {
		Graphics::GlyphMetrics m;
		if (!src->metrics(sample[i], m))
			left.push_back(sample[i]);
	}

	if (!_coverageChecked.contains(name)) {
		_coverageChecked[name] = true;
		debug(1, "SCI: coverage of %s: %u of %u sampled characters missing, %u spacing marks",
			  name.c_str(), r.missing, r.sampled, r.spacingMarks);
		const Common::String text = Graphics::coverageWarning(name, r, fallback);
		// One warning per line of the text (the missing line, the spacing-mark line).
		uint start = 0;
		for (uint i = 0; i < text.size() + 1 && !text.empty(); i++) {
			if (i == text.size() || text[i] == '\n') {
				warning("%s", Common::String(text.c_str() + start, i - start).c_str());
				start = i + 1;
			}
		}
	}
	sample = left;
}

Graphics::UnicodeGlyphSource *GfxCache::faceChainFor(const FontSettings &s, Common::String &chainName) {
	chainName.clear();
	Common::Array<Common::String> paths = s.faceChain;
	if (paths.empty() && !s.facePath.empty())
		paths.push_back(s.facePath);

	// Each face of the chain; one that fails to open is left out with one
	// warning. A chain only this font id names falls back to the one every
	// other id gets, as a single face always did.
	Common::Array<Graphics::UnicodeGlyphSource *> faces;
	Common::Array<Common::String> names;
	// The pixel= of the chain actually opened: the global chain has its own.
	int pixel = s.pixel;
	for (int pass = 0; pass < 2 && faces.empty(); pass++) {
		if (pass == 1) {
			const FontSettings global = fontSettingsFor(-1);
			if (global.faceChain.empty() || global.faceChain == paths)
				break;
			paths = global.faceChain;
			pixel = global.pixel;
		}
		for (uint i = 0; i < paths.size(); i++) {
			const bool last = i + 1 == paths.size();
			Graphics::TtfGlyphSource *src = ttfSource(paths[i], s.size, kProbesTranslation, "hires_text_font",
													  last ? "using the .uni fonts" : "using the next face of the chain",
													  i == 0 ? pixel : 0);
			if (src) {
				faces.push_back(src);
				names.push_back(paths[i]);
			}
		}
	}
	if (faces.empty())
		return nullptr;

	for (uint i = 0; i < names.size(); i++)
		chainName += (i ? "," : "") + names[i];

	// Behind the faces, the .uni bundle, then the game's own font (what
	// GfxFontSet falls back to for a character no face has). The chain is
	// shared by the font ids whose faces, size and bundle all match: the
	// faces are opened at that size, so another size is another chain.
	GfxFontUnicode *uni = loadUniBundle();
	Common::String key = faceChainKey(names, s.size, uni && uni->source());
	if (pixel > 0)
		key += Common::String::format("|p%d", pixel);
	if (_chains.contains(key))
		return _chains[key];

	// Each face is checked for what the faces before it lack: a face that
	// only has to cover Thai is not warned about Japanese.
	Common::Array<uint32> sample = coverageSample();
	for (uint i = 0; i < faces.size(); i++) {
		const Common::String next = i + 1 < faces.size() ? names[i + 1] :
			(uni ? Common::String("the .uni fonts") : Common::String("the game's font"));
		checkFaceCoverage(faces[i], names[i], next, sample);
	}

	Graphics::UnicodeGlyphSource *chain = faces[0];
	if (faces.size() > 1 || (uni && uni->source())) {
		Common::Array<Graphics::UnicodeGlyphSource *> sources = faces;
		if (uni && uni->source()) {
			// The bundle's 1 bpp cell, presented in the faces' cell.
			Common::String error;
			Graphics::NormalizedGlyphSource *n = Graphics::NormalizedGlyphSource::create(
				uni->source(), faces[0]->cellWidth(), faces[0]->cellHeight(), DisposeAfterUse::NO, error);
			if (n) {
				sources.push_back(n);
				_chainParts.push_back(n);
			} else {
				warning("hires text: the .uni fonts cannot stand behind %s (%s); the game's font draws what the chain lacks",
						names.back().c_str(), error.c_str());
			}
		}
		if (sources.size() > 1) {
			chain = new Graphics::FallbackGlyphSource(sources, DisposeAfterUse::NO);
			_chainParts.push_back(chain);
		}
	}
	debug(1, "SCI: face chain %s at %dpx (%u faces%s)", chainName.c_str(), s.size, faces.size(),
		  chain != faces[0] && uni ? ", then the .uni fonts" : "");
	_chains[key] = chain;
	return chain;
}

const Graphics::BreakRules &GfxCache::layoutRules() {
	if (!_layoutRulesResolved) {
		_layoutRulesResolved = true;
		resolveHiresText();
		// [layout] of hires_text.map; the defaults are SCI's (Hangul at
		// spaces, as SCI always broke it; kinsoku and the Thai fallback on).
		if (_hiresMapLoaded) {
			const Graphics::HiResLayoutSettings &l = _hiresMap.layout;
			if (l.hangulSet)
				_layoutRules.hangul = l.hangul;
			if (l.kinsokuSet)
				_layoutRules.kinsoku = l.kinsoku;
			if (l.thaiSet)
				_layoutRules.thaiFallback = l.thai;
		}
	}
	return _layoutRules;
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
	// The chains next (newest first: a chain points at the wrappers before
	// it), then the faces they point at.
	for (int i = (int)_chainParts.size() - 1; i >= 0; i--)
		delete _chainParts[i];
	_chainParts.clear();
	_chains.clear();
	for (Common::HashMap<Common::String, Graphics::TtfGlyphSource *>::iterator it = _ttfSources.begin();
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
