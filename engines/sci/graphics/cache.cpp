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
#include "sci/graphics/fontbanked.h"
#include "sci/graphics/fontkorean.h"
#include "sci/graphics/fontset.h"
#include "sci/graphics/fontunicode.h"
#include "sci/graphics/hirestextstate.h"
#include "graphics/hires_text/chain_layout.h"
#include "graphics/hires_text/coverage.h"
#include "graphics/hires_text/font_face.h"
#include "graphics/hires_text/font_value.h"
#include "graphics/hires_text/glyph_source_file.h"
#include "graphics/hires_text/glyph_source_fallback.h"
#include "graphics/hires_text/glyph_source_ranged.h"
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

// Whether a font id names a FONT resource, as GfxFontFromResource resolves
// it: lsl1sci mixes its own font ids (extra high bits, e.g. 2107) with the
// global ones, and the resource loader strips those bits (& 0x7ff, see
// scifont.cpp). Asked with the raw id, such a font looked like a legacy CJK
// id with no resource, got the code-page adapter instead of a set, and
// UTF-8 text in it was drawn as code-page pairs (Korean) or as the low byte
// of each code point (any other language).
bool fontResourceExists(ResourceManager *resMan, GuiResourceId fontId) {
	return resMan->testResource(ResourceId(kResourceTypeFont, fontId)) ||
		resMan->testResource(ResourceId(kResourceTypeFont, fontId & 0x7ff));
}

bool isExcluded(const Common::Array<Common::String> &excludedPaths, const Common::String &path) {
	for (uint i = 0; i < excludedPaths.size(); i++) {
		if (excludedPaths[i] == path)
			return true;
	}
	return false;
}

} // End of anonymous namespace

GfxCache::GfxCache(ResourceManager *resMan, GfxScreen *screen, GfxPalette *palette)
	: _resMan(resMan), _screen(screen), _palette(palette),
	  _hiresResolved(false), _hiresApplies(false), _hiresMapLoaded(false),
	  _hiresBlend(Graphics::kHiResBlendAuto), _hiresScreenIsClut8(true),
	  _uniBundle(nullptr), _uniBundleTried(false),
	  _textLogResolved(false), _textLog(false),
	  _layoutRulesResolved(false), _sampleResolved(false), _fitProbesResolved(false) {
}

void GfxCache::resolveHiresText() {
	if (_hiresResolved)
		return;
	_hiresResolved = true;

	// SciEngine read the ini keys and the map's phase-1 view before the
	// screen was set (HiresTextState); this is phase 2, for the screen
	// actually set.
	const HiresTextState *state = g_sci->hiresTextState();
	assert(state);
	_hiresGameDir = state->gameDir();
	_hiresIni = state->ini();

	// hires_text_face and the map are honoured for an SCI16 game whose text
	// is a UTF-8 translation, or one in a legacy CJK code page: the text
	// decides, not the game's language (I18N_TEXT_DESIGN.md section 4.6).
	Common::String why;
	_hiresApplies = hiresTextApplies(getSciVersion(), g_sci->getSciLanguageCodePage(),
									 g_sci->heapStringsAreUtf8(), why);

	if (!_hiresApplies) {
		if (_hiresIni.faceSet)
			warning("hires_text_face is ignored: %s", why.c_str());
		const bool defaultMapExists = !_hiresIni.mapSet && !Graphics::findDefaultHiResMap(_hiresGameDir).empty();
		if (_hiresIni.mapSet || defaultMapExists)
			warning("HIRESTXT.MAP is ignored: %s", why.c_str());
		return;
	}

	if (!_hiresIni.enabled) {
		// design section 11: hires_text=false is the layer off entirely - as
		// if no map and no other ini key had been given at all.
		_hiresIni = Graphics::HiResIniOverrides();
		_hiresIni.enabled = false;
		return;
	}

	const Graphics::PixelFormat screen = g_system->getScreenFormat();
	if (state->haveMapPath())
		_hiresMapDir = state->mapDir();
	// A refused map was reported once, by phase 1: it is refused whatever
	// the target, so it is not read again.
	if (state->haveMapPath() && !state->mapRefused()) {
		Common::Array<Common::String> qualifiers;
		const char *platform = Common::getPlatformCode(g_sci->getPlatform());
		if (platform && *platform)
			qualifiers.push_back(platform);

		// GfxCache runs after GfxScreen has set the actual screen (design
		// section 7.1.1's phase 2): the map is read for the sections that
		// screen's own render target actually uses.
		Graphics::HiResMapLoadOptions options;
		options.target = Graphics::targetOfFormat(screen);
		options.quiet = false;
		_hiresMapLoaded = Graphics::HiResFontMap::loadMapFile(state->mapPath(), qualifiers, Graphics::kHiResKeysSci,
															  _hiresMap, options);
		if (_hiresMapLoaded)
			debug(1, "SCI: %s loaded (platform '%s', target %s), %u font id sections",
				  state->mapPath().toString().c_str(), platform ? platform : "", Graphics::renderTargetName(options.target),
				  (uint)_hiresMap.fontIds.size());
	}

	// checkIniFaceWarnings() runs here, after the map has loaded (not before,
	// and not once per font id resolveFontSettings() would otherwise repeat
	// the same text for - design section 10's "once per cause per load") -
	// so a name the map's own [fonts] resolves does not get a spurious
	// "unknown face name" warning first.
	Common::Array<Common::String> faceWarnings;
	checkIniFaceWarnings(_hiresIni, _hiresMapLoaded, _hiresMap.faces, _hiresMapDir, _hiresGameDir, faceWarnings);
	for (uint i = 0; i < faceWarnings.size(); i++) {
		_hiresMap.warnings.push_back(faceWarnings[i]);
		warning("%s", faceWarnings[i].c_str());
	}

	// design 7.4: SCI draws at 2x only.
	Common::String scaleWarning;
	sciHiresScale(_hiresMap, _hiresMapLoaded, _hiresIni, Graphics::hiResScaleLimits(), scaleWarning);
	if (!scaleWarning.empty()) {
		_hiresMap.warnings.push_back(scaleWarning);
		warning("%s", scaleWarning.c_str());
	}

	// design 7.2: blend from the sections of this screen; coverage is
	// blended iff Graphics::blendActive() (sciThresholdCoverage()).
	_hiresBlend = sciBlend(_hiresIni, _hiresMap, _hiresMapLoaded);
	_hiresScreenIsClut8 = screen.isCLUT8();
	const bool anyCoverage = Graphics::mapHasCoverage(_hiresMap, _hiresMapLoaded, _hiresIni, _hiresMapDir, _hiresGameDir,
													  &HiresTextState::faceHasCoverage, nullptr);
	const Common::String blendWarning = sciBlendWarning(_hiresBlend, anyCoverage, _hiresScreenIsClut8);
	if (!blendWarning.empty())
		warning("%s", blendWarning.c_str());
	debug(1, "SCI: hi-res text blend %s on a %s screen", _hiresBlend == Graphics::kHiResBlendOff ? "off" :
		  (_hiresBlend == Graphics::kHiResBlendOn ? "on" : "auto"), Graphics::renderTargetName(Graphics::targetOfFormat(screen)));
}

bool GfxCache::thresholdsCoverage(int faceBpp) {
	resolveHiresText();
	return sciThresholdCoverage(_hiresBlend, faceBpp, _hiresScreenIsClut8);
}

FontSettings GfxCache::fontSettingsFor(GuiResourceId fontId) {
	resolveHiresText();
	if (!_hiresApplies || !_hiresIni.enabled)
		return FontSettings();
	return resolveFontSettings(_hiresMap, _hiresMapLoaded, fontId, _hiresIni, _hiresMapDir, _hiresGameDir);
}

Graphics::TtfGlyphSource *GfxCache::ttfSource(const Common::String &path, int size, FaceProbes probes,
									const char *what, const char *fallback, int pixel,
									const Common::Array<uint32> *explicitProbes) {
	const bool requireHangul = probes == kProbesHangul;
	const Common::String pixelKey = pixel > 0 ? Common::String::format("|p%d", pixel) : Common::String();
	Common::String key = Common::String::format("%s|%d|%d", path.c_str(), size, (int)probes) + pixelKey;
	if (explicitProbes) {
		key += "|t";
		for (uint i = 0; i < explicitProbes->size(); i++)
			key += Common::String::format(",%x", (*explicitProbes)[i]);
	}
	if (_ttfSources.contains(key))
		return _ttfSources[key];
	// A face without the Hangul check is satisfied by one that passed it.
	if (!explicitProbes && probes == kProbesDefault) {
		const Common::String checked = Common::String::format("%s|%d|1", path.c_str(), size) + pixelKey;
		if (_ttfSources.contains(checked) && _ttfSources[checked])
			return _ttfSources[checked];
	}

	Common::String error;
	Graphics::TtfGlyphSource *src = nullptr;
	int32 faceIndex = 0;
	Common::SeekableReadStream *stream = nullptr;
	if (path.empty()) {
		error = "empty path";
	} else if ((stream = Graphics::openFontFace(Common::Path(path, Common::Path::kNativeSeparator), faceIndex, error))) {
		const uint32 startMs = g_system->getMillis();
		if (pixel > 0) {
			// Held on its grid: no fit, so no probes and no Hangul check.
			src = Graphics::TtfGlyphSource::createPixel(stream, DisposeAfterUse::YES, size, pixel, error, faceIndex);
		} else if (explicitProbes) {
			// design 6.7: a [glyphs] target face is fitted to exactly its own
			// targeted code points, nothing else.
			src = Graphics::TtfGlyphSource::create(stream, DisposeAfterUse::YES, size, error, false, false,
												   explicitProbes->empty() ? nullptr : &(*explicitProbes)[0],
												   explicitProbes->size(), faceIndex);
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
			// [render] gamma=: off (100) unless the map asks.
			src->setCoverageGamma(_hiresMap.coverageGamma);
			// Rows of headroom above and below the cell, so a glyph
			// the fit could not bring inside it is kept whole; GfxFontUnicode
			// places the raster by its measured baseline (GlyphPlacement).
			src->padRows((size + 3) / 4);
			debug(1, "SCI: %s %s opened at %dpx in %u ms", what, path.c_str(), size, elapsedMs);
		}
	}

	// A failure is remembered too, so each key warns once, even though
	// purgeFontCache() rebuilds the font sets.
	if (!src)
		warning("%s %s: %s; %s", what, path.c_str(), error.c_str(), fallback);
	_ttfSources[key] = src;
	return src;
}

Graphics::UnicodeGlyphSource *GfxCache::svfnSource(const Common::String &path, const char *what,
												  const char *fallback, bool &isSvfn) {
	isSvfn = false;
	if (path.empty() || _notSvfn.contains(path))
		return nullptr;
	if (_svfnSources.contains(path)) {
		isSvfn = true;
		return _svfnSources[path];
	}

	// Only the header is looked at here; a path that does not open is left
	// to ttfSource(), which gives it its one warning.
	int32 faceIndex = 0;
	Common::String error;
	Common::SeekableReadStream *stream =
		Graphics::openFontFace(Common::Path(path, Common::Path::kNativeSeparator), faceIndex, error);
	// Before any read: an SVF stays open and reads blocks of its own.
	Graphics::unbufferCacheStream(stream);
	byte head[4];
	if (!stream || stream->read(head, sizeof(head)) != sizeof(head) || !Graphics::isSvfnFile(head, sizeof(head))) {
		delete stream;
		_notSvfn[path] = true;
		return nullptr;
	}

	isSvfn = true;
	stream->seek(0);
	// The source keeps the file and reads each glyph as it is first drawn.
	Graphics::UnicodeGlyphSource *src = Graphics::createSvfnSource(stream, DisposeAfterUse::YES, error,
		Common::Path(path, Common::Path::kNativeSeparator).baseName());
	if (src)
		debug(1, "SCI: %s %s opened as a %dx%d bitmap font", what, path.c_str(), src->cellWidth(), src->cellHeight());
	else
		warning("%s %s: %s; %s", what, path.c_str(), error.c_str(), fallback);
	_svfnSources[path] = src;
	return src;
}

Graphics::UnicodeGlyphSource *GfxCache::singleFace(const Common::String &path, int size, FaceProbes probes,
												   const char *fallback, int pixel, Graphics::TtfGlyphSource *&ttf) {
	ttf = nullptr;
	bool isSvfn = false;
	Graphics::UnicodeGlyphSource *src = svfnSource(path, "hires_text_face", fallback, isSvfn);
	if (!isSvfn) {
		ttf = ttfSource(path, size, probes, "hires_text_face", fallback, pixel);
		return ttf;
	}
	// As a TrueType face is refused (requireHangul): a Korean game needs
	// Hangul from its main face.
	if (src && probes == kProbesHangul && src->cells(0xAC00) <= 0) {
		if (!_svfnNoHangul.contains(path)) {
			_svfnNoHangul[path] = true;
			warning("hires_text_face %s: face has no Hangul glyphs; %s", path.c_str(), fallback);
		}
		return nullptr;
	}
	return src;
}

void GfxCache::applyMissing(GfxFontUnicode *f, uint32 missing, const Common::String &name) {
	// GfxFontUnicode::setMissing() is idempotent per object (it bails once
	// its _source is already the box-wrapped one), so this is safe to call
	// once per font id that shares @p f, whether that is the one shared
	// .uni bundle (the faceless path) or a chain's own routed source
	// (faceChainFor() builds that source with missing= off, so its box has to
	// be applied here instead, same as the faceless path).
	if (!missing || !f)
		return;
	f->setMissing(missing);
	if (f->source() && f->source()->cells(missing) <= 0) {
		const Common::String w = Common::String::format(
			"HIRESTXT.MAP: missing=U+%04X has no effect: %s has no glyph for it", missing, name.c_str());
		_hiresMap.warnings.push_back(w);
		if (!_warnedOnceThisLoad.contains(w)) {
			_warnedOnceThisLoad[w] = true;
			warning("%s", w.c_str());
		}
	}
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
			f->setHardStencil(thresholdsCoverage(f->bitsPerPixel()));
			_uniBundle = f;
		} else {
			delete f;
		}
	}
	return _uniBundle;
}

Graphics::UnicodeGlyphSource *GfxCache::faceChainFor(GuiResourceId fontId, FontSettings &s,
													 Common::String &chainName, Graphics::UnicodeGlyphSource *&firstRaw,
													 Graphics::UnicodeGlyphSource *&firstNormalized, int &firstTop) {
	chainName.clear();
	firstRaw = nullptr;
	firstNormalized = nullptr;
	firstTop = 0;
	Graphics::HiResIdPlan &plan = s.plan;

	// Shared by every font id whose face, size and plan agree: opening
	// faces, checking coverage and load-time warnings all happen only once.
	const Common::String key = unicodeBundleKey(s.facePath, s.size, plan.hash());
	if (_chains.contains(key)) {
		for (uint i = 0; i < plan.idChain.faces.size(); i++)
			chainName += (i ? "," : "") + plan.idChain.faces[i].path.toString('/');
		if (_chainFirstFace.contains(key)) {
			const ChainFirstFace &cached = _chainFirstFace[key];
			firstRaw = cached.raw;
			firstNormalized = cached.normalized;
			firstTop = cached.top;
			// This id's own plan is a fresh copy of the same starting
			// plan the cached chain was built from - it needs the same
			// load-time decline applied to it, since only the *chain*
			// (opening, warnings, the decline itself) was shared, not the
			// mutation each id's own settings.plan carries forward.
			declineFailedTargets(plan, cached.declinedTargets);
		}
		return _chains[key];
	}

	const bool utf8 = g_sci->heapStringsAreUtf8();
	// A legacy (non-UTF8) main face needs Hangul, as a single main face
	// always has; a UTF-8 translation's chain is fitted to its own sampled
	// characters too (faces beyond the first keep the plain default fit).
	const FaceProbes idProbes = utf8 ? kProbesTranslation :
		(g_sci->getSciLanguageCodePage() == Common::kWindows949 ? kProbesHangul : kProbesDefault);

	// Every distinct path the id chain and every range rule's chain name,
	// id chain first, in plan order.
	Common::Array<Common::String> order;
	Common::HashMap<Common::String, Graphics::UnicodeGlyphSource *> opened;
	Common::HashMap<Common::String, bool> openedIsSvf;

	auto rememberPath = [&](const Common::String &p) {
		for (uint i = 0; i < order.size(); i++)
			if (order[i] == p)
				return;
		order.push_back(p);
	};

	auto openChainFace = [&](const Common::String &p, bool first) {
		rememberPath(p);
		if (opened.contains(p))
			return;
		// design 5.4: pixel/size apply only to the id chain's own first
		// face; every other position is fitted at the id's plain size.
		Graphics::TtfGlyphSource *ttf = nullptr;
		Graphics::UnicodeGlyphSource *src = singleFace(p, s.size, first ? idProbes : (utf8 ? kProbesTranslation : kProbesDefault),
													   "using the .uni fonts", first ? s.pixel : 0, ttf);
		opened[p] = src;
		openedIsSvf[p] = _svfnSources.contains(p);
	};

	for (uint i = 0; i < plan.idChain.faces.size(); i++)
		openChainFace(plan.idChain.faces[i].path.toString('/'), i == 0);
	for (uint c = 0; c < plan.ruleChains.size(); c++)
		for (uint i = 0; i < plan.ruleChains[c].faces.size(); i++)
			openChainFace(plan.ruleChains[c].faces[i].path.toString('/'), false);

	if (!order.empty())
		firstRaw = opened.contains(order[0]) ? opened[order[0]] : nullptr;

	// design 6.7: every [glyphs] target face, fitted to exactly its own
	// targeted code points (several targets sharing one face share one
	// open, fitted to the union of their code points).
	Common::HashMap<Common::String, Common::Array<uint32> > targetCps;
	for (uint i = 0; i < plan.targets.size(); i++) {
		if (plan.targets[i].face.kind != Graphics::kHiResFaceFile)
			continue;
		targetCps[plan.targets[i].face.path.toString('/')].push_back(plan.targets[i].cp);
	}
	for (Common::HashMap<Common::String, Common::Array<uint32> >::iterator it = targetCps.begin();
		 it != targetCps.end(); ++it) {
		if (opened.contains(it->_key))
			continue; // already opened for the chain itself
		bool isSvf = false;
		Graphics::UnicodeGlyphSource *src = svfnSource(it->_key, "[glyphs]", "the game's font draws it", isSvf);
		if (!isSvf)
			src = ttfSource(it->_key, s.size, kProbesDefault, "[glyphs]", "the game's font draws it", 0, &it->_value);
		opened[it->_key] = src;
		openedIsSvf[it->_key] = isSvf;
	}

	// design 5.4, 6.4, 6.7's load-time checks, over every distinct path in
	// plan order (id chain, every rule chain, every target).
	Common::Array<FontIdFace> faces;
	Common::Array<Common::String> seen;
	auto addFace = [&](const Common::String &p) {
		for (uint i = 0; i < seen.size(); i++)
			if (seen[i] == p)
				return;
		seen.push_back(p);
		FontIdFace f;
		f.path = p;
		f.source = opened.contains(p) ? opened[p] : nullptr;
		f.isSvf = openedIsSvf.contains(p) && openedIsSvf[p];
		faces.push_back(f);
	};
	for (uint i = 0; i < plan.idChain.faces.size(); i++)
		addFace(plan.idChain.faces[i].path.toString('/'));
	for (uint c = 0; c < plan.ruleChains.size(); c++)
		for (uint i = 0; i < plan.ruleChains[c].faces.size(); i++)
			addFace(plan.ruleChains[c].faces[i].path.toString('/'));
	for (uint i = 0; i < plan.targets.size(); i++)
		if (plan.targets[i].face.kind == Graphics::kHiResFaceFile)
			addFace(plan.targets[i].face.path.toString('/'));

	Common::Array<Common::String> excludedPaths;
	Common::Array<uint32> failedTargetCodes;
	checkPlanLoadWarnings(fontId, plan, faces, excludedPaths, failedTargetCodes, _hiresMap, _warnedOnceThisLoad);
	// design 6.7: an unresolvable target is decided now, once, by turning
	// it into `original` in the plan itself - not at draw time.
	declineFailedTargets(plan, failedTargetCodes);

	for (uint i = 0; i < order.size(); i++)
		chainName += (i ? "," : "") + order[i];

	// One shared cell for the whole id, over the union of every face the
	// id chain, every rule chain and every target names, plus the .uni
	// bundle - so RangeRoutedGlyphSource's geometry is uniform.
	GfxFontUnicode *uni = loadUniBundle();
	Graphics::UnicodeGlyphSource *uniSource = uni ? uni->source() : nullptr;

	Common::Array<Common::String> unionPaths;
	Common::Array<Graphics::ChainFaceInfo> infos;
	auto addUnion = [&](const Common::String &p) {
		if (isExcluded(excludedPaths, p) || !opened.contains(p) || !opened[p])
			return;
		for (uint i = 0; i < unionPaths.size(); i++)
			if (unionPaths[i] == p)
				return;
		Graphics::UnicodeGlyphSource *src = opened[p];
		Graphics::ChainFaceInfo info;
		info.cellWidth = src->cellWidth();
		info.cellHeight = src->cellHeight();
		info.bpp = src->bitsPerPixel();
		info.baselineRow = src->baselineRow();
		info.trueType = !(openedIsSvf.contains(p) && openedIsSvf[p]);
		Graphics::TtfGlyphSource *ttf = info.trueType ? dynamic_cast<Graphics::TtfGlyphSource *>(src) : nullptr;
		info.rowPad = ttf ? ttf->rowPad() : 0;
		unionPaths.push_back(p);
		infos.push_back(info);
	};
	for (uint i = 0; i < order.size(); i++)
		addUnion(order[i]);
	for (Common::HashMap<Common::String, Common::Array<uint32> >::iterator it = targetCps.begin();
		 it != targetCps.end(); ++it)
		addUnion(it->_key);

	if (unionPaths.empty())
		return nullptr; // every named face failed to open

	const Graphics::ChainLayout layout = Graphics::layoutFaceChain(
		infos, uniSource != nullptr, uniSource ? uniSource->cellWidth() : 0, uniSource ? uniSource->cellHeight() : 0);

	// Each face is checked for what the faces before it lack (design 4.4):
	// a face that only has to cover Thai is not warned about Japanese.
	if (utf8) {
		Common::Array<uint32> sample = coverageSample();
		for (uint i = 0; i < unionPaths.size(); i++) {
			const Common::String next = i + 1 < unionPaths.size() ? unionPaths[i + 1] :
				(uni ? Common::String("the .uni fonts") : Common::String("the game's font"));
			checkFaceCoverage(opened[unionPaths[i]], unionPaths[i], next, sample);
		}
	}

	Common::HashMap<Common::String, Graphics::UnicodeGlyphSource *> normalized;
	for (uint i = 0; i < unionPaths.size(); i++) {
		Graphics::UnicodeGlyphSource *raw = opened[unionPaths[i]];
		if (!layout.normalize[i]) {
			normalized[unionPaths[i]] = raw;
			continue;
		}
		Common::String error;
		Graphics::NormalizedGlyphSource *n = Graphics::NormalizedGlyphSource::create(
			raw, layout.cellWidth, layout.cellHeight, layout.tops[i], DisposeAfterUse::NO, error);
		if (n) {
			normalized[unionPaths[i]] = n;
			_chainParts.push_back(n);
		} else {
			warning("hires text: %s cannot join the face chain (%s)", unionPaths[i].c_str(), error.c_str());
			normalized[unionPaths[i]] = nullptr;
		}
	}
	// The id chain's own first face, in the chain's one shared cell - what
	// GlyphPlacement measures against - and the row it now sits at.
	if (!order.empty()) {
		for (uint i = 0; i < unionPaths.size(); i++) {
			if (unionPaths[i] == order[0]) {
				firstNormalized = normalized.contains(order[0]) ? normalized[order[0]] : nullptr;
				firstTop = layout.tops[i];
				break;
			}
		}
	}

	Graphics::UnicodeGlyphSource *uniNormalized = nullptr;
	if (uniSource) {
		Common::String error;
		Graphics::NormalizedGlyphSource *n = Graphics::NormalizedGlyphSource::create(
			uniSource, layout.cellWidth, layout.cellHeight, layout.uniTop, DisposeAfterUse::NO, error);
		if (n) {
			uniNormalized = n;
			_chainParts.push_back(n);
		} else {
			warning("hires text: the .uni fonts cannot stand behind font %d's faces (%s)", fontId, error.c_str());
		}
	}

	auto sourceFor = [&](const Common::String &p) -> Graphics::UnicodeGlyphSource * {
		if (isExcluded(excludedPaths, p) || !normalized.contains(p))
			return nullptr;
		return normalized[p];
	};

	Common::Array<Common::Array<Graphics::UnicodeGlyphSource *> > chainSources;
	chainSources.resize(plan.ruleChains.size() + 1);
	auto buildChain = [&](const Graphics::HiResFaceChain &chain, Common::Array<Graphics::UnicodeGlyphSource *> &out) {
		for (uint i = 0; i < chain.faces.size(); i++)
			out.push_back(sourceFor(chain.faces[i].path.toString('/')));
		// The .uni bundle stands behind every chain that does not end
		// in `original`, as the id's own empty-chain fallback would.
		if (uniNormalized && !chain.endsInOriginal)
			out.push_back(uniNormalized);
	};
	buildChain(plan.idChain, chainSources[0]);
	for (uint c = 0; c < plan.ruleChains.size(); c++)
		buildChain(plan.ruleChains[c], chainSources[c + 1]);

	Common::Array<Graphics::UnicodeGlyphSource *> targetSources;
	targetSources.resize(plan.targets.size());
	for (uint i = 0; i < plan.targets.size(); i++) {
		if (plan.targets[i].face.kind == Graphics::kHiResFaceFile)
			targetSources[i] = sourceFor(plan.targets[i].face.path.toString('/'));
		// kHiResFaceSame answers through chainSources[0] inside pickGlyph();
		// no entry of its own is needed here.
	}

	// missing= is applied to the GfxFontUnicode wrapper afterward
	// (GfxCache::applyMissing()), not baked into the routed source itself -
	// with it built in, cells()/hasGlyph() would answer "yes" for the box on
	// a code a legacy face named after this one in a GfxFontSet could still
	// have covered, pre-empting it. A plan copy is enough: nothing else
	// reads `missing` off the source's own plan.
	Graphics::HiResIdPlan rangedPlan = plan;
	rangedPlan.missing = 0;
	Graphics::UnicodeGlyphSource *ranged =
		new Graphics::RangeRoutedGlyphSource(rangedPlan, chainSources, targetSources, DisposeAfterUse::NO);
	_chainParts.push_back(ranged);
	_chains[key] = ranged;
	ChainFirstFace &cached = _chainFirstFace[key];
	cached.raw = firstRaw;
	cached.normalized = firstNormalized;
	cached.top = firstTop;
	cached.declinedTargets = failedTargetCodes;

	debug(1, "SCI: font %d hi-res chain %s (%u faces%s)", fontId, chainName.c_str(), (uint)unionPaths.size(),
		  uniNormalized ? ", then the .uni fonts" : "");
	return ranged;
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
	// ASCII is left out: the game's own font draws it by default, so a face
	// or a bundle without it lacks nothing. The fit probes keep it.
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

int GfxCache::gameFontBaseline(GuiResourceId fontId) {
	if (_gameBaselines.contains(fontId))
		return _gameBaselines[fontId];
	int baseline = -1;
	if (fontResourceExists(_resMan, fontId)) {
		GfxFontFromResource font(_resMan, _screen, fontId);
		baseline = bitmapFontBaseline([&font](uint32 ch) { return font.inkBottom(ch); });
		if (baseline >= 0)
			baseline *= getSciVersion() >= SCI_VERSION_2 ? 1 : 2;
	}
	_gameBaselines[fontId] = baseline;
	return baseline;
}

GfxFontUnicode *GfxCache::unicodeFaceFor(GuiResourceId fontId, FontSettings &s) {
	// design 6.5 step 3: this font id draws only from the game's own
	// resource font, not even the shared .uni bundle a faceless id
	// otherwise falls back to.
	if (s.original)
		return nullptr;

	// A plan can name a face through range.*/[glyphs] alone, with no
	// face= anywhere (an empty id chain) - those still have to be opened.
	// Only a plan naming no face at all falls straight to the shared .uni
	// bundle (design 5.3's empty-id-chain fallback).
	if (s.facePath.empty() && !planNamesAnyFace(s.plan)) {
		GfxFontUnicode *uni = loadUniBundle();
		if (uni) {
			applyMissing(uni, s.plan.missing, "the .uni fonts");
			if (g_sci->heapStringsAreUtf8()) {
				Common::Array<uint32> sample = coverageSample();
				checkFaceCoverage(uni->source(), "the .uni fonts", "the game's font", sample);
			}
		}
		return uni;
	}

	Common::String chainName;
	Graphics::UnicodeGlyphSource *firstRaw = nullptr;
	Graphics::UnicodeGlyphSource *firstNormalized = nullptr;
	int firstTop = 0;
	Graphics::UnicodeGlyphSource *ranged = faceChainFor(fontId, s, chainName, firstRaw, firstNormalized, firstTop);
	if (!ranged) {
		// Every face the id named failed to open: fall back exactly as a
		// faceless id would.
		s.facePath.clear();
		s.faceChain.clear();
		GfxFontUnicode *uni = loadUniBundle();
		if (uni) {
			applyMissing(uni, s.plan.missing, "the .uni fonts");
			if (g_sci->heapStringsAreUtf8()) {
				Common::Array<uint32> sample = coverageSample();
				checkFaceCoverage(uni->source(), "the .uni fonts", "the game's font", sample);
			}
		}
		return uni;
	}

	// The face in its layout cell - on the game font's baseline
	// (align=game), on its own line (font) or centred (cell) - moved by
	// shift=. Measured against the id chain's own first face, already folded
	// into the chain's one shared cell: the raster dimensions and
	// the ink-scanned baseline both have to be the cell every glyph is
	// actually drawn from, or a chain whose first face is not the tallest
	// (the id chain shorter than a range-named face, or than the .uni
	// bundle) places every glyph off by the padding that first face got.
	GlyphPlacement placement;
	int gameBaseline = -1;
	if (firstNormalized) {
		const GlyphPlacement::Align align = s.align == Graphics::kHiResAlignFont ? GlyphPlacement::kAlignFont :
			(s.align == Graphics::kHiResAlignCell ? GlyphPlacement::kAlignCell : GlyphPlacement::kAlignGame);
		gameBaseline = align == GlyphPlacement::kAlignGame ? gameFontBaseline(fontId) : -1;
		// buildChainPlacementInput() measures firstNormalized, the id chain's
		// own first face already folded into the chain's one shared cell -
		// never firstRaw's own, pre-fold cell, which can differ from the cell
		// every glyph is actually drawn from when that face is not the
		// chain's tallest (the id chain shorter than a range-named face, or
		// than the .uni bundle); firstRaw is asked only for its TrueType
		// lineTop(), which a NormalizedGlyphSource wrapper would hide.
		const GlyphPlacement::Input in =
			buildChainPlacementInput(firstNormalized, firstRaw, firstTop, s.cell, align, s.baseline, gameBaseline);
		placement = GlyphPlacement::compute(in);
		debug(1, "SCI: font %d glyphs: %dpx face (%d rows) in a %dpx cell, align %d: baseline row %d, game's %d, "
			  "shift %d -> offset (%d, %d)%s",
			  fontId, firstNormalized->cellWidth(), firstNormalized->cellHeight(), s.cell, (int)align,
			  in.rasterBaseline, gameBaseline, s.baseline, placement.dx, placement.dy,
			  placement.active() ? "" : " (unchanged)");
	}

	Common::String key = unicodeBundleKey(s.facePath, s.size, s.plan.hash());
	if (s.pixel > 0)
		key += Common::String::format("|p%d", s.pixel);
	if (placement.active())
		key += Common::String::format("|c%d,%d,%d", placement.cellPx, placement.dx, placement.dy);
	if (_ttfBundles.contains(key))
		return _ttfBundles[key];

	GfxFontUnicode *f = new GfxFontUnicode(_screen, 0);
	f->setPerGlyph(g_sci->heapStringsAreUtf8());
	f->setPlacement(placement);
	// The router owns no source: every face and the .uni bundle stay in
	// their own caches, shared.
	f->setSource(ranged, s.facePath, DisposeAfterUse::NO);
	f->setHardStencil(thresholdsCoverage(f->bitsPerPixel()));
	// `ranged` was built with missing= off (faceChainFor()), so the box
	// is applied to this wrapper instead - reachable only through
	// drawsMissing(), never through hasGlyph(), so a legacy face named after
	// this one in a GfxFontSet still gets first refusal at a code the chain
	// lacks (GfxFontSet::faceFor()'s own final step). The "no glyph for the
	// box either" warning is already checkPlanLoadWarnings()'s, per chain
	// (called above, inside faceChainFor()); setMissing() alone is
	// idempotent and safe to call unconditionally.
	if (s.plan.missing)
		f->setMissing(s.plan.missing);
	_ttfBundles[key] = f;
	return f;
}

const Graphics::BreakRules &GfxCache::layoutRules() {
	if (!_layoutRulesResolved) {
		_layoutRulesResolved = true;
		resolveHiresText();
		// [layout] of HIRESTXT.MAP; the defaults are SCI's (Hangul at
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
		// Deliberately unconditional: no scope-predicate check. Font-id
		// logging is useful on an English game too, which the predicate
		// would otherwise refuse hires_text_face itself on.
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
	for (Common::HashMap<Common::String, Graphics::UnicodeGlyphSource *>::iterator it = _svfnSources.begin();
		 it != _svfnSources.end(); ++it)
		delete it->_value;
	_svfnSources.clear();
	_notSvfn.clear();
	_svfnNoHangul.clear();
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
	const bool haveResource = fontResourceExists(_resMan, fontId);
	if (!haveResource)
		return nullptr;

	// This font id's own hi-res settings (ini keys, then HIRESTXT.MAP),
	// and the Unicode face they name.
	FontSettings settings = fontSettingsFor(fontId);
	GfxFontUnicode *uni = unicodeFaceFor(fontId, settings);
	if (_hiresApplies)
		debug(1, "SCI: font %d hi-res settings: face '%s' %dpx", fontId, settings.facePath.c_str(), settings.size);

	GfxFontSet *set = new GfxFontSet(fontId, g_sci->getSciLanguageCodePage(), settings);
	set->setUtf8Text(g_sci->heapStringsAreUtf8());
	set->addFace(new GfxFontFromResource(_resMan, _screen, fontId), GfxFontSet::kFaceResource);

	// The legacy double-byte faces, when the game ships their font file.
	// GfxFontKorean and GfxFontSjis call error() on a missing file, so
	// existence is checked rather than assumed.
	// In a Korean game, when a hi-res face is in effect (a map's face= or
	// hires_text_face), that face is what the player asked to draw the text
	// with: it goes before korean.fnt, which then only draws what the face
	// lacks. Without one, korean.fnt keeps drawing the Hangul syllables and
	// the .uni fonts stand last as before. Japanese games keep the legacy
	// SJIS face first.
	const bool namedFace = uni && !settings.facePath.empty() && g_sci->usesKoreanText();
	if (namedFace)
		set->addFace(uni, GfxFontSet::kFaceCodePoint, false, true);

	if (g_sci->usesKoreanText()) {
		if (Common::File::exists(Common::Path("korean.fnt"))) {
			// GfxFontKorean and GfxFontSjis already halve their own metrics below
			// SCI2, so they must NOT be marked as hires-plane faces here - doing
			// so halves twice and the glyphs pile up on top of each other.
			set->addFace(new GfxFontKorean(_screen, 1001, true), GfxFontSet::kFaceLegacyDbcs);
		} else if (!uni) {
			// No korean.fnt and no hi-res face configured: a game that carries
			// its Hangul as banks of its own FONT resources (Conquests of
			// Camelot's Korean beta, fontbanked.h) draws from those, at native
			// resolution, as its patched DOS interpreter did. A configured
			// hi-res face (hires_text_face / HIRESTXT.MAP) wins over them.
			const int bankBase = GfxFontBanked::bankBaseFor(_resMan, fontId);
			if (bankBase >= 0) {
				debug(1, "SCI: font %d draws Hangul from font banks %d..%d", fontId, bankBase,
					  bankBase + GfxFontBanked::kLeadLast - GfxFontBanked::kLeadFirst);
				set->addFace(new GfxFontBanked(_resMan, _screen, fontId, bankBase), GfxFontSet::kFaceLegacyDbcs);
			}
		}
	} else if (g_sci->getLanguage() == Common::JA_JPN) {
		if (Common::File::exists(Common::Path("SJIS.FNT")))
			set->addFace(new GfxFontSjis(_screen, 900), GfxFontSet::kFaceLegacyDbcs);
	}

	// The Unicode face last: it is the widest, and being last means it only
	// answers for characters nothing else covers. Shared, so unowned.
	if (uni && !namedFace)
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
	if (fontId == 1001 && g_sci->usesKoreanText() &&
		Common::File::exists(Common::Path("korean.fnt")))
		fallback = new GfxFontKorean(_screen, fontId, getSciVersion() >= SCI_VERSION_2);
	else if (fontId == 900 && g_sci->getLanguage() == Common::JA_JPN &&
			 Common::File::exists(Common::Path("SJIS.FNT")))
		fallback = new GfxFontSjis(_screen, fontId);
	else if (fontResourceExists(_resMan, fontId))
		fallback = new GfxFontFromResource(_resMan, _screen, fontId);
	// No fallback at all when the id names no resource. GfxText16 switches to
	// the legacy CJK font id (1001/900) to get double-byte glyphs, and most
	// games have no such resource - GfxFontFromResource would abort with
	// "font resource N not found". The adapter copes with a null fallback;
	// single-byte characters then have no glyph, which is correct, because
	// the game only ever switches to that id for double-byte text.
	if (fallback)
		_ownedFonts.push_back(fallback);

	return new GfxFontUnicodeAdapter(uni, g_sci->getSciLanguageCodePage(), fallback, fontId, settings.plan,
									 settings.cell);
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
		if (!font && (fontId == 1001) && g_sci->usesKoreanText())
			// GfxText16 hands a font code points, GfxText32 packed byte pairs.
			font = new GfxFontKorean(_screen, fontId, getSciVersion() >= SCI_VERSION_2);
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
