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

#include "scumm/hires_text.h"
#include "scumm/text_utf8.h"

#include "common/config-manager.h"
#include "common/fs.h"
#include "common/rect.h"
#include "common/stream.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "common/ustr.h"
#include "graphics/hires_text/font_face.h"
#include "graphics/hires_text/glyph_source_fallback.h"
#include "graphics/hires_text/glyph_source_file.h"
#include "graphics/hires_text/glyph_source_ranged.h"
#include "graphics/hires_text/glyph_source_svfn.h"
#include "graphics/hires_text/glyph_source_ttf.h"
#include "graphics/hires_text/latin_advance.h"
#include "graphics/hires_text/text_compose.h"
#include "graphics/hires_text/text_layout.h"
#include "graphics/hires_text/unicode_props.h"

namespace Scumm {

// The default map name (design section 4): 8.3, matched case-insensitively
// on every platform, so a translation that ships it needs no config key at
// all.
static const char *const kDefaultMapName = "HIRESTXT.MAP";

/**
 * A readable name for a code page, for logs.
 *
 * Which code page a font is indexed by decides whether a game's bytes reach
 * its glyphs at all, so it is worth saying out loud rather than leaving the
 * reader to infer it from a number.
 */
static const char *codePageName(Common::CodePage cp) {
	switch (cp) {
	case Common::kWindows949:
		return "CP949/Korean";
	case Common::kWindows932:
		return "CP932/Japanese";
	case Common::kWindows936:
		return "CP936/Simplified Chinese";
	case Common::kWindows950:
		return "CP950/Traditional Chinese";
	case Common::kWindows1252:
		return "CP1252/Latin";
	case Common::kUtf8:
		return "UTF-8";
	case Common::kCodePageInvalid:
		return "none (Unicode indices)";
	default:
		return "other";
	}
}

/**
 * Expand a numbered font name, e.g. "korean%02d.fnt" with 3 -> "korean03.fnt".
 *
 * The template comes from a map file, so it must not be handed to printf: a
 * hand-edited map could otherwise name any conversion it liked, including one
 * that reads a pointer off the stack. Only a single integer field is
 * understood, with an optional zero-padded width.
 */
static Common::String expandFontPattern(const Common::String &pattern, int index) {
	const char *percent = strchr(pattern.c_str(), '%');
	if (!percent)
		return Common::String();

	Common::String out(pattern.c_str(), percent);
	const char *p = percent + 1;

	bool zeroPad = false;
	if (*p == '0') {
		zeroPad = true;
		++p;
	}

	int width = 0;
	while (*p >= '0' && *p <= '9') {
		width = width * 10 + (*p - '0');
		if (width > 8)
			return Common::String();
		++p;
	}

	if (*p != 'd')
		return Common::String();
	++p;

	Common::String number = Common::String::format("%d", index);
	while (zeroPad && (int)number.size() < width)
		number = Common::String("0") + number;

	out += number;
	out += p;
	return out;
}

/// The code page a language's text is in, when the map does not say.
static Common::CodePage defaultEncodingFor(Common::Language language) {
	switch (language) {
	case Common::JA_JPN:
		return Common::kWindows932;
	case Common::ZH_CHN:
		return Common::kWindows936;
	case Common::ZH_TWN:
		return Common::kWindows950;
	case Common::KO_KOR:
		return Common::kWindows949;
	default:
		// Everything else is a single byte page the game itself defines. The
		// adapter has to be told explicitly before it may assume otherwise.
		return Common::kCodePageInvalid;
	}
}

bool ScummHiResText::faceHasCoverage(const Common::Path &face, void *ctx) {
	Common::FSNode node(face);
	if (!node.exists())
		return false;
	Common::SeekableReadStream *stream = node.createReadStream();
	if (!stream)
		return false;
	byte head[9];
	bool coverage = true;
	if (stream->read(head, sizeof(head)) == sizeof(head) && Graphics::isSvfnFile(head, 4))
		coverage = (head[8] == 8 || head[8] == 2);
	delete stream;
	return coverage;
}

/// design section 4's default map file, matched case-insensitively.
static Common::Path findDefaultMapFile(const Common::Path &gameDir) {
	const Common::Path direct = gameDir.appendComponent(kDefaultMapName);
	if (Common::FSNode(direct).exists())
		return direct;

	Common::FSNode dir(gameDir);
	if (!dir.isDirectory())
		return Common::Path();
	Common::FSList children;
	if (dir.getChildren(children, Common::FSNode::kListFilesOnly)) {
		for (uint i = 0; i < children.size(); ++i) {
			if (children[i].getName().equalsIgnoreCase(kDefaultMapName))
				return children[i].getPath();
		}
	}
	return Common::Path();
}

ScummHiResText::ScummHiResText() {
	reset();
}

ScummHiResText::~ScummHiResText() {
	freeFaces();
}

void ScummHiResText::reset() {
	_enabled = false;
	_simpleFonts = false;
	_simpleCellHeight = 0;
	_simpleCellCount = 0;
	_scaleFromUser = false;
	_scale = 1;
	for (int i = 0; i < kMaxFonts; ++i)
		_gameFontW[i] = _gameFontH[i] = 0;
	_cjkCells = false;
	_fontsLoaded = false;
	_alphaActive = false;
	_korPatchShadow = false;
	_perGlyph = false;
	_wantsAlpha = false;
	_haveMap = false;
	_mapDir.clear();
	_gameDir.clear();
	_target = Graphics::kHiResTargetAuto;
	_mapPath.clear();
	_qualifiers.clear();
	_haveMapPath = false;
	_resolvedBlend = Graphics::kHiResBlendAuto;
	_anyCoverage = false;
	_simpleBitmapPattern.clear();
	_simpleBitmapSingle.clear();
	_simpleLatinBitmapName.clear();
	_encoding = Common::kCodePageInvalid;
	_map.clear();
	_ini = Graphics::HiResIniOverrides();
	for (int i = 0; i < kMaxFonts; ++i) {
		_plans[i] = Graphics::HiResIdPlan();
		_idBound[i] = false;
		_chainSources[i].clear();
		_targetSources[i].clear();
		_borrowed[i].clear();
		_excludedForId[i].clear();
		_gameMirror[i] = Graphics::kHiResMirrorNone;
	}
	_translationCps = Graphics::CodePointSet();
	_coverageSample.clear();
	_fitProbes.clear();
	memset(_paletteCache, 0, sizeof(_paletteCache));
	memset(_paletteRGB, 0, sizeof(_paletteRGB));
	freeCoverage();
	freeFaces();
}

void ScummHiResText::freeFaces() {
	for (Common::HashMap<Common::String, Face *>::iterator it = _sources.begin();
		 it != _sources.end(); ++it) {
		if (it->_value)
			delete it->_value->source;
		delete it->_value;
	}
	_sources.clear();
	_failedFaces.clear();
	_attemptedPaths.clear();
	_faceBySource.clear();
	_warnedOnceThisLoad.clear();

	for (int i = 0; i < kMaxFonts; ++i) {
		_simpleCjkFaces[i] = nullptr;
		_simpleLatinFaces[i] = nullptr;
		_ttfFaces[i] = nullptr;
		_ttfFacePx[i] = 0;
		_chainSources[i].clear();
		_targetSources[i].clear();
		_borrowed[i].clear();
		_excludedForId[i].clear();
		_chainSourcesPixelSize[i] = 0;
		_idBound[i] = false;
	}
	_anchorValid = false;
	_coverageChecked.clear();
	_coverageWarnings.clear();
	_logFace = nullptr;
}

ScummHiResText::Face *ScummHiResText::faceForSource(Graphics::UnicodeGlyphSource *src) const {
	if (!src)
		return nullptr;
	Common::HashMap<uint64, Face *>::const_iterator it = _faceBySource.find((uint64)(uintptr)src);
	return (it != _faceBySource.end()) ? it->_value : nullptr;
}

Graphics::HiResFontScope ScummHiResText::engineScope() {
	// design section 8: range.basic-latin=same (ASCII from the charset's own
	// face - the old font=same default), advance.basic-latin=game. Every
	// other key (advance, origin, cell, align, mirror, ...) keeps its
	// documented zero default, which is already what HiResFontScope's own
	// constructor gives.
	Graphics::HiResFontScope scope;

	Graphics::HiResRangeSpec basicLatin;
	Common::String error;
	Graphics::parseRangeSpec("basic-latin", basicLatin, error);

	Graphics::HiResFontValue same;
	Graphics::HiResFaceEntry sameEntry;
	sameEntry.kind = Graphics::kHiResFaceSame;
	sameEntry.written = "same";
	same.entries.push_back(sameEntry);

	scope.rangeSpecs.push_back(basicLatin);
	scope.rangeValues.push_back(same);

	scope.advanceSpecs.push_back(basicLatin);
	scope.advanceValues.push_back(Graphics::kHiResAdvanceGame);

	return scope;
}

Graphics::BreakRules ScummHiResText::breakRules(bool centred) const {
	Graphics::BreakRules rules;
	// The Korean patches' addLinebreaks() breaks Hangul anywhere except in
	// centred text (actor speech), where it breaks at spaces. Only with this
	// layer on (C31): with hi-res text off a UTF-8 translation breaks as it
	// always did, Hangul anywhere.
	rules.hangul = (centred && _enabled) ? Graphics::kHangulBreakWord : Graphics::kHangulBreakAny;
	if (_map.layout.hangulSet)
		rules.hangul = _map.layout.hangul;
	if (_map.layout.kinsokuSet)
		rules.kinsoku = _map.layout.kinsoku;
	if (_map.layout.thaiSet)
		rules.thaiFallback = _map.layout.thai;
	return rules;
}

void ScummHiResText::useUtf8Text() {
	_encoding = Common::kUtf8;
	if (_enabled)
		debug(1, "SCUMM: hi-res text: the translation is UTF-8 (source encoding UTF-8, "
				 "per-glyph placement; the map's [text] encoding is not used)");
}

// ---------------------------------------------------------------------
// version-2 map / compiled per-id plans
// ---------------------------------------------------------------------

void ScummHiResText::adoptMap(const Graphics::HiResMap &map, const Graphics::HiResIniOverrides &ini) {
	freeFaces();
	_map = map;
	_ini = ini;
	_haveMap = true;
	_perGlyph = true;
	_enabled = true;
	_fontsLoaded = false;
	_encoding = _map.encodingSet ? _map.encoding : _encoding;

	compilePlans(_gameDir);
}

void ScummHiResText::compilePlans(const Common::Path &gameDir) {
	Common::Array<Common::String> planWarnings;
	for (int i = 0; i < kMaxFonts; ++i)
		_plans[i] = Graphics::compileIdPlan(_map, _haveMap, i, _ini, engineScope(), _mapDir, gameDir, planWarnings);
	// Task 5 review F2 / L4: de-duplicate before emitting (a map-wide
	// face=same warning would otherwise print once per id), and - unlike
	// the adoptMap() this replaces - never silently drop them.
	for (uint i = 0; i < planWarnings.size(); ++i) {
		bool dup = false;
		for (uint j = 0; j < i && !dup; ++j)
			dup = (planWarnings[j] == planWarnings[i]);
		if (!dup)
			warning("SCUMM: %s", planWarnings[i].c_str());
	}

	Common::String scaleWarning;
	_scale = resolvedScale(_map, _haveMap, _ini, Graphics::hiResScaleLimits(), scaleWarning);
	if (!scaleWarning.empty())
		warning("SCUMM: %s", scaleWarning.c_str());

	// design 7.2: blend is read in phase 2, against the sections the
	// resolved target actually uses - not the phase-1 coverage question
	// wantedTarget() answers before any target-qualified section exists
	// (Task 7 review L1(b)).
	_resolvedBlend = _ini.blendSet ? _ini.blend : _map.blend;
	_anyCoverage = Graphics::mapHasCoverage(_map, _haveMap, _ini, _mapDir, gameDir,
											&ScummHiResText::faceHasCoverage, nullptr);
}

Graphics::HiResRenderTarget ScummHiResText::wantedTarget(const Graphics::HiResMap &map, bool mapLoaded,
														 const Graphics::HiResIniOverrides &ini, int gameVersion,
														 bool anyCoverage, Common::String &warning) {
	warning.clear();

	if (gameVersion >= 7) {
		bool explicitTarget = false;
		const Graphics::HiResRenderTarget want = Graphics::wantedRenderTarget(map, mapLoaded, ini, anyCoverage, explicitTarget);
		if (explicitTarget && want != Graphics::kHiResTargetClut8) {
			warning = Common::String::format("SCUMM v7+ keeps a paletted screen; render_target=%s ignored",
											  Graphics::renderTargetName(want));
		}
		return Graphics::kHiResTargetClut8;
	}

	bool explicitTarget = false;
	return Graphics::wantedRenderTarget(map, mapLoaded, ini, anyCoverage, explicitTarget);
}

int ScummHiResText::resolvedScale(const Graphics::HiResMap &map, bool mapLoaded, const Graphics::HiResIniOverrides &ini,
								  const Graphics::HiResScaleLimits &platform, Common::String &warning) {
	warning.clear();
	const int wanted = ini.scaleSet ? ini.scale : (mapLoaded && map.scaleSet ? map.scale : 2);

	Common::String clampWarning;
	const int resolved = Graphics::clampScale(wanted, 1, 3, platform, 2, "SCUMM", clampWarning);
	if (!clampWarning.empty())
		warning = clampWarning + Common::String::format("; using %d", resolved);
	return resolved;
}

void ScummHiResText::adoptScreen(const Graphics::PixelFormat &actual) {
	const Graphics::HiResRenderTarget actualTarget = Graphics::targetOfFormat(actual);
	if (actualTarget == _target)
		return;

	warning("SCUMM: the screen is %s, not %s; hi-res text uses the %s sections",
			Graphics::renderTargetName(actualTarget), Graphics::renderTargetName(_target),
			Graphics::renderTargetName(actualTarget));
	_target = actualTarget;

	if (_haveMapPath) {
		Graphics::HiResMapLoadOptions p2opts;
		p2opts.target = _target;
		p2opts.quiet = false;
		_haveMap = Graphics::HiResFontMap::loadMapFile(_mapPath, _qualifiers, Graphics::kHiResKeysScumm, _map, p2opts);
	}

	compilePlans(_gameDir);
}

Common::Array<Common::String> ScummHiResText::collectFacePaths(int id) const {
	Common::Array<Common::String> paths;
	if (id < 0 || id >= kMaxFonts)
		return paths;
	const Graphics::HiResIdPlan &plan = _plans[id];

	auto add = [&](const Common::String &p) {
		if (p.empty())
			return;
		for (uint i = 0; i < paths.size(); ++i)
			if (paths[i] == p)
				return;
		paths.push_back(p);
	};

	for (uint i = 0; i < plan.idChain.faces.size(); ++i)
		add(plan.idChain.faces[i].path.toString('/'));
	for (uint c = 0; c < plan.ruleChains.size(); ++c)
		for (uint i = 0; i < plan.ruleChains[c].faces.size(); ++i)
			add(plan.ruleChains[c].faces[i].path.toString('/'));
	for (uint i = 0; i < plan.targets.size(); ++i)
		if (plan.targets[i].face.kind == Graphics::kHiResFaceFile)
			add(plan.targets[i].face.path.toString('/'));

	return paths;
}

int ScummHiResText::nearestPlanCharset(int charsetId) const {
	const int want = (charsetId >= 0 && charsetId < kMaxFonts) ? _charsetWidths[charsetId] : 0;
	if (want <= 0)
		return -1;
	const int m = MAX(1, _scale);

	int best = -1, bestDelta = 0;
	for (int i = 0; i < kMaxFonts; ++i) {
		if (i == charsetId || (int)_chainSources[i].size() == 0 || _chainSources[i][0].empty())
			continue;
		Graphics::UnicodeGlyphSource *src = nullptr;
		for (uint j = 0; j < _chainSources[i][0].size() && !src; ++j)
			src = _chainSources[i][0][j];
		if (!src)
			continue;
		const int delta = ABS((int)src->cellWidth() / m - want);
		if (best < 0 || delta < bestDelta) {
			best = i;
			bestDelta = delta;
		}
	}
	return best;
}

void ScummHiResText::ensureChainSources(int id, bool allowDiskOpen) const {
	if (id < 0 || id >= kMaxFonts)
		return;
	const Graphics::HiResIdPlan &plan = _plans[id];

	if (_chainSources[id].size() != plan.ruleChains.size() + 1)
		_chainSources[id].resize(plan.ruleChains.size() + 1);
	if (_chainSources[id][0].size() != plan.idChain.faces.size())
		_chainSources[id][0].resize(plan.idChain.faces.size(), nullptr);
	for (uint c = 0; c < plan.ruleChains.size(); ++c)
		if (_chainSources[id][c + 1].size() != plan.ruleChains[c].faces.size())
			_chainSources[id][c + 1].resize(plan.ruleChains[c].faces.size(), nullptr);
	if (_targetSources[id].size() != plan.targets.size())
		_targetSources[id].resize(plan.targets.size(), nullptr);

	int pixelSize = 0;
	bool lineFit = true;
	const bool haveSize = ttfSizeForSimple(id, pixelSize, lineFit) || plan.sizeSet;
	if (plan.sizeSet) {
		pixelSize = plan.size;
		lineFit = false;
	}

	// M5: a TrueType face opened at an earlier, borrowed or guessed size
	// (nearestTtfCharset(), before this id's own cell was known) must be
	// re-opened once the id's own size differs from that - an SVFN slot
	// never depends on pixelSize and is left alone.
	if (haveSize && _chainSourcesPixelSize[id] != 0 && _chainSourcesPixelSize[id] != pixelSize) {
		for (uint c = 0; c < _chainSources[id].size(); ++c) {
			for (uint i = 0; i < _chainSources[id][c].size(); ++i) {
				Face *f = faceForSource(_chainSources[id][c][i]);
				if (f && f->ttf)
					_chainSources[id][c][i] = nullptr;
			}
		}
		for (uint i = 0; i < _targetSources[id].size(); ++i) {
			Face *f = faceForSource(_targetSources[id][i]);
			if (f && f->ttf)
				_targetSources[id][i] = nullptr;
		}
	}
	if (haveSize)
		_chainSourcesPixelSize[id] = pixelSize;

	auto resolveEntry = [&](const Graphics::HiResFaceEntry &e, int pixelGrid, Graphics::UnicodeGlyphSource *&slot) {
		if (slot || e.kind != Graphics::kHiResFaceFile)
			return;
		const Common::String p = e.path.toString('/');
		if (_excludedForId[id].contains(p))
			return; // design 5.4: refused for this id (cell height); stays refused
		// A path already known (added via addFace(), or opened by an
		// earlier allowDiskOpen=true call) is always safe to wire up; one
		// that is not is only worth trying from here when disk opens are
		// allowed and a size is known (a TrueType face) - never from
		// addFace()'s own eager sweep (allowDiskOpen=false), which must not
		// pre-empt a sibling chain entry addFace() has not reached yet.
		if (!_sources.contains(p) && !(allowDiskOpen && haveSize))
			return;
		Graphics::UnicodeGlyphSource *src = openPlanFace(e.path, pixelSize, lineFit, pixelGrid);
		if (src) {
			slot = src;
			debug(1, "SCUMM: hi-res font %d <- %s", id, p.c_str());
		}
	};

	for (uint i = 0; i < plan.idChain.faces.size(); ++i)
		resolveEntry(plan.idChain.faces[i], (i == 0) ? plan.pixel : 0, _chainSources[id][0][i]);
	for (uint c = 0; c < plan.ruleChains.size(); ++c)
		for (uint i = 0; i < plan.ruleChains[c].faces.size(); ++i)
			resolveEntry(plan.ruleChains[c].faces[i], 0, _chainSources[id][c + 1][i]);
	for (uint i = 0; i < plan.targets.size(); ++i)
		resolveEntry(plan.targets[i].face, 0, _targetSources[id][i]);

	const int nearest = nearestPlanCharset(id);
	_borrowed[id] = (nearest >= 0 && !_chainSources[nearest].empty()) ? _chainSources[nearest][0]
																	  : Common::Array<Graphics::UnicodeGlyphSource *>();

	checkIdOnceReady(id);
	checkCoverageForId(id);
}

void ScummHiResText::checkCoverageForId(int id) const {
	if (id < 0 || id >= kMaxFonts || _coverageSample.empty())
		return;
	const Graphics::HiResIdPlan &plan = _plans[id];
	if (plan.idChain.faces.empty() || _chainSources[id].empty())
		return;
	Common::String key = Common::String::format("id:%d:", id);
	Common::Array<Common::String> names;
	for (uint i = 0; i < plan.idChain.faces.size(); ++i) {
		names.push_back(plan.idChain.faces[i].written);
		key += plan.idChain.faces[i].path.toString('/');
		key += '|';
	}
	checkCoverage(_chainSources[id][0], names, key);
}

void ScummHiResText::checkIdOnceReady(int id) const {
	if (id < 0 || id >= kMaxFonts || _idBound[id])
		return;
	const Common::Array<Common::String> needed = collectFacePaths(id);
	for (uint i = 0; i < needed.size(); ++i) {
		// M2: a TrueType path that opened fine is never in _sources under
		// its own (raw) key - only openPlanFace()'s size-qualified one - so
		// without _attemptedPaths this id would never bind and its S3
		// checks below would never run.
		if (!_sources.contains(needed[i]) && !_failedFaces.contains(needed[i]) &&
			!_attemptedPaths.contains(needed[i]))
			return; // still waiting on at least one path
	}
	_idBound[id] = true;

	const Graphics::HiResIdPlan &plan = _plans[id];
	if (plan.original)
		return;

	// design section 5.4 / M1's dedup, M3's "every SVF the plan names, not
	// only the id chain's": one warning per (path, cause), regardless of how
	// many ids share the cause (a map-wide missing= or [glyphs] target is
	// checked once per id it touches otherwise, spec 10's "once per cause
	// per load").
	auto warnOnce = [&](const Common::String &w) {
		if (_warnedOnceThisLoad.contains(w))
			return;
		_warnedOnceThisLoad[w] = true;
		warning("%s", w.c_str());
		_map.warnings.push_back(w);
	};

	// M3: every SVF the plan names anywhere (id chain, every range./
	// advance./origin. chain a rule compiled, every [glyphs] target) must
	// share the first SVF's cell height - not only the id chain's own.
	// collectFacePaths() lists the id chain first, so "the first SVF of
	// that id's chain" (5.4) is still what sets the reference when the id
	// chain has one.
	Common::String firstPath;
	int firstHeight = -1;
	for (uint i = 0; i < needed.size(); ++i) {
		const Common::String &p = needed[i];
		Common::HashMap<Common::String, Face *>::iterator it = _sources.find(p);
		if (it == _sources.end() || !it->_value || it->_value->ttf)
			continue; // not an opened SVF
		const int h = it->_value->source->cellHeight();
		if (firstHeight < 0) {
			firstHeight = h;
			firstPath = p;
			continue;
		}
		if (h != firstHeight) {
			warnOnce(Common::String::format(
				"HIRESTXT.MAP: %s: cell height %d differs from %s's %d on %d; not used",
				p.c_str(), h, firstPath.c_str(), firstHeight, id));
			_excludedForId[id][p] = true;
			// The same path may appear as a separate copy in a range./
			// advance./origin. rule chain that expanded a `same` entry
			// (design 6.5 step 4), or as a [glyphs] target - null every one
			// of its slots, not only the id chain's.
			for (uint c = 0; c < _chainSources[id].size(); ++c) {
				const Common::Array<Graphics::HiResFaceEntry> &faces =
					(c == 0) ? plan.idChain.faces : plan.ruleChains[c - 1].faces;
				for (uint j = 0; j < faces.size() && j < _chainSources[id][c].size(); ++j) {
					if (faces[j].path.toString('/') == p)
						_chainSources[id][c][j] = nullptr;
				}
			}
			for (uint j = 0; j < plan.targets.size() && j < _targetSources[id].size(); ++j) {
				if (plan.targets[j].face.kind == Graphics::kHiResFaceFile &&
					plan.targets[j].face.path.toString('/') == p)
					_targetSources[id][j] = nullptr;
			}
		}
	}

	// design section 10.4: a [glyphs] target lacking its code point - a
	// named face (L6: `same` too, answered by the id chain, design 6.7).
	for (uint i = 0; i < plan.targets.size(); ++i) {
		Graphics::UnicodeGlyphSource *src = nullptr;
		Common::String faceText;
		if (plan.targets[i].face.kind == Graphics::kHiResFaceFile) {
			src = (i < _targetSources[id].size()) ? _targetSources[id][i] : nullptr;
			faceText = plan.targets[i].face.written;
		} else if (plan.targets[i].face.kind == Graphics::kHiResFaceSame) {
			faceText = "same";
			if (!_chainSources[id].empty())
				for (uint j = 0; j < _chainSources[id][0].size() && !src; ++j)
					if (_chainSources[id][0][j] && _chainSources[id][0][j]->cells(plan.targets[i].cp) > 0)
						src = _chainSources[id][0][j];
		} else {
			continue;
		}
		if (src && src->cells(plan.targets[i].cp) > 0)
			continue;
		uint32 code = 0;
		for (Common::HashMap<uint32, uint32>::const_iterator it = plan.targetIndexByCode.begin();
			 it != plan.targetIndexByCode.end(); ++it) {
			if (it->_value == i) {
				code = it->_key;
				break;
			}
		}
		warnOnce(Common::String::format(
			"HIRESTXT.MAP: [glyphs] 0x%02x -> %s:U+%04X: the face has no such glyph; the game's font draws it",
			code, faceText.c_str(), plan.targets[i].cp));
	}

	// design section 10.4: missing= naming a code point no face of the id's
	// chain has. M1: an id that names no face of its own at all (a pure
	// borrower, B6/design 5.3) has nothing here to check - its own drawing
	// chain is the *borrowed* one, checked (once) when the donor id itself
	// binds, so checking here as well would be a false positive (spec
	// still fires it for that donor id).
	bool hasOwnChain = !plan.idChain.faces.empty();
	for (uint c = 0; c < plan.ruleChains.size() && !hasOwnChain; ++c)
		hasOwnChain = !plan.ruleChains[c].faces.empty();
	if (plan.missing && hasOwnChain) {
		bool found = false;
		for (uint c = 0; c < _chainSources[id].size() && !found; ++c)
			for (uint i = 0; i < _chainSources[id][c].size() && !found; ++i)
				found = _chainSources[id][c][i] && _chainSources[id][c][i]->cells(plan.missing) > 0;
		if (!found) {
			const Common::String faceName = !plan.idChain.faces.empty() ? plan.idChain.faces[0].written
																		 : Common::String("the game's font");
			warnOnce(Common::String::format(
				"HIRESTXT.MAP: missing=U+%04X has no effect: %s has no glyph for it", plan.missing, faceName.c_str()));
		}
	}
}

bool ScummHiResText::addFace(const Common::String &resolvedPath, Common::SeekableReadStream &stream) const {
	Common::HashMap<Common::String, Face *>::iterator existing = _sources.find(resolvedPath);
	if (existing != _sources.end())
		return existing->_value != nullptr;

	Graphics::HiResBitmapFont *font = new Graphics::HiResBitmapFont();
	if (!font->load(stream)) {
		delete font;
		_sources[resolvedPath] = nullptr;
		_failedFaces[resolvedPath] = true;
		return false;
	}

	Face *face = new Face();
	face->source = new Graphics::SvfnGlyphSource(font, DisposeAfterUse::YES);
	_sources[resolvedPath] = face;
	_faceBySource[(uint64)(uintptr)face->source] = face;
	_fontsLoaded = true;

	debug(1, "SCUMM: hi-res font <- %s: %dx%d cell, %d bpp, %d glyphs, %s, codepage %s%s",
		  resolvedPath.c_str(), font->cellWidth(), font->cellHeight(), font->bpp(),
		  font->glyphCount(), font->isProportional() ? "proportional" : "fixed width",
		  codePageName(font->codePage()), font->bpp() > 1 ? ", anti-aliased" : ", stencil");

	for (int i = 0; i < kMaxFonts; ++i)
		ensureChainSources(i, false);

	return true;
}

Graphics::UnicodeGlyphSource *ScummHiResText::sourceForFace(const Common::String &resolvedPath) const {
	Common::HashMap<Common::String, Face *>::const_iterator it = _sources.find(resolvedPath);
	return (it != _sources.end() && it->_value) ? it->_value->source : nullptr;
}

const Graphics::HiResIdPlan &ScummHiResText::planFor(int charsetId) const {
	if (charsetId < 0 || charsetId >= kMaxFonts)
		charsetId = 0;
	return _plans[charsetId];
}

Graphics::UnicodeGlyphSource *ScummHiResText::openPlanFace(const Common::Path &path, int pixelSize, bool lineFit,
														   int pixelGrid) const {
	const Common::String p = path.toString('/');
	Common::HashMap<Common::String, Face *>::iterator it = _sources.find(p);
	if (it != _sources.end())
		return it->_value ? it->_value->source : nullptr; // an SVF, already added (or failed)
	if (_failedFaces.contains(p))
		return nullptr;
	// M2: a successfully opened TrueType face is stored under the
	// size-qualified key below, never under the raw path, so
	// checkIdOnceReady()'s "every path attempted" gate needs its own record
	// that this path was (successfully or not) tried at all.
	_attemptedPaths[p] = true;

	Common::String key = Common::String::format("ttf:%s@%d%s", p.c_str(), pixelSize, lineFit ? "" : "c");
	if (pixelGrid > 0)
		key += Common::String::format("p%d", pixelGrid);
	Common::HashMap<Common::String, Face *>::iterator kit = _sources.find(key);
	if (kit != _sources.end())
		return kit->_value ? kit->_value->source : nullptr;

	int32 faceIndex = 0;
	Common::String openError;
	Common::SeekableReadStream *stream = Graphics::openFontFace(path, faceIndex, openError);
	if (!stream) {
		warning("SCUMM: cannot open hi-res font '%s'", p.c_str());
		_sources[key] = nullptr;
		_failedFaces[p] = true;
		return nullptr;
	}

	byte head[4];
	const bool svfn = stream->read(head, sizeof(head)) == sizeof(head) && Graphics::isSvfnFile(head, sizeof(head));
	stream->seek(0);
	if (svfn) {
		const bool ok = addFace(p, *stream);
		delete stream;
		return ok ? sourceForFace(p) : nullptr;
	}

#ifdef USE_FREETYPE2
	Common::String error;
	Graphics::TtfGlyphSource *ttf;
	if (pixelGrid > 0)
		ttf = Graphics::TtfGlyphSource::createPixel(stream, DisposeAfterUse::YES, pixelSize, pixelGrid, error, faceIndex);
	else
		ttf = Graphics::TtfGlyphSource::create(stream, DisposeAfterUse::YES, pixelSize, error, false, lineFit,
											   _fitProbes.empty() ? nullptr : _fitProbes.begin(), _fitProbes.size(),
											   faceIndex);
	if (!ttf) {
		warning("SCUMM: cannot use hi-res TrueType font '%s': %s", p.c_str(), error.c_str());
		_sources[key] = nullptr;
		_failedFaces[p] = true;
		return nullptr;
	}
	ttf->setCoverageGamma(_map.coverageGamma);
	debug(1, "SCUMM: hi-res TrueType font %s opened at %dpx: %u probe glyphs rasterised in %u ms",
		  path.baseName().c_str(), pixelSize, ttf->rasterCount(), ttf->totalRenderMs());
	Face *face = new Face();
	face->source = ttf;
	face->ttf = ttf;
	face->pixelSize = pixelSize;
	face->lineFit = lineFit;
	_sources[key] = face;
	_faceBySource[(uint64)(uintptr)ttf] = face;
	return ttf;
#else
	delete stream;
	warning("SCUMM: hi-res TrueType fonts need a build with FreeType; bake the font to .fnt instead: '%s'",
			p.c_str());
	_sources[key] = nullptr;
	_failedFaces[p] = true;
	return nullptr;
#endif
}

Graphics::UnicodeGlyphSource *ScummHiResText::primarySourceFor(int charsetId) const {
	if (charsetId < 0 || charsetId >= kMaxFonts)
		return nullptr;
	if (_perGlyph) {
		ensureChainSources(charsetId);
		if (!_chainSources[charsetId].empty())
			for (uint i = 0; i < _chainSources[charsetId][0].size(); ++i)
				if (_chainSources[charsetId][0][i])
					return _chainSources[charsetId][0][i];
		return nullptr;
	}
	Face *f = _simpleCjkFaces[charsetId];
	return f ? f->source : nullptr;
}

int ScummHiResText::originShiftFor(Graphics::UnicodeGlyphSource *drawn, int charsetId) const {
	Graphics::UnicodeGlyphSource *ref = primarySourceFor(charsetId);
	if (!drawn || !ref || drawn == ref)
		return 0;
	const int db = drawn->baselineRow();
	const int rb = ref->baselineRow();
	if (db < 0 || rb < 0)
		return 0;
	return rb - db;
}

Graphics::UnicodeGlyphSource *ScummHiResText::faceForCodePoint(int charsetId, uint32 code, uint32 &cp,
																bool &declined) const {
	declined = false;
	const int id = (charsetId >= 0 && charsetId < kMaxFonts) ? charsetId : 0;
	ensureChainSources(id);
	const Graphics::HiResIdPlan &plan = _plans[id];

	const uint32 decoded = cp;
	uint32 outCp = decoded;
	const Graphics::HiResGlyphStep step = plan.glyphFor(code, decoded, outCp);
	if (step == Graphics::kHiResGlyphStepGame) {
		declined = true;
		return nullptr;
	}

	Graphics::HiResPick pick = Graphics::pickGlyph(plan, _chainSources[id], _targetSources[id], outCp, &_borrowed[id]);
	if (pick.kind == Graphics::HiResPick::kGame) {
		// B6/design 5.3: an id with no face of its own at all (no rule, empty
		// idChain, not `original`) never reaches pickGlyph()'s own `borrowed`
		// step - HiResIdPlan::chainFor() answers null for "off" and "empty"
		// alike. Retry against the nearest charset explicitly here, so a
		// charset with nothing of its own (MI2's odd ones) still borrows, as
		// it always has.
		if (outCp < Graphics::kHiResTargetBase && !plan.original && !_borrowed[id].empty()) {
			const int idx = plan.faceRules.lookup(outCp);
			const Graphics::HiResFaceChain &chain = (idx >= 0) ? plan.ruleChains[(uint)idx] : plan.idChain;
			if (!chain.endsInOriginal) {
				for (uint i = 0; i < _borrowed[id].size(); ++i) {
					if (_borrowed[id][i] && _borrowed[id][i]->cells(outCp) > 0) {
						cp = outCp;
						return _borrowed[id][i];
					}
				}
				if (plan.missing) {
					for (uint i = 0; i < _borrowed[id].size(); ++i) {
						if (_borrowed[id][i] && _borrowed[id][i]->cells(plan.missing) > 0) {
							cp = plan.missing;
							return _borrowed[id][i];
						}
					}
				}
			}
		}
		declined = true;
		cp = outCp;
		return nullptr;
	}

	cp = pick.cp;
	if (pick.chain == -1)
		return (pick.face >= 0 && (uint)pick.face < _targetSources[id].size()) ? _targetSources[id][(uint)pick.face]
																			   : nullptr;
	if (pick.chain == -2)
		return (pick.face >= 0 && (uint)pick.face < _borrowed[id].size()) ? _borrowed[id][(uint)pick.face] : nullptr;
	if (pick.chain >= 0 && (uint)pick.chain < _chainSources[id].size()) {
		const Common::Array<Graphics::UnicodeGlyphSource *> &s = _chainSources[id][(uint)pick.chain];
		return (pick.face >= 0 && (uint)pick.face < s.size()) ? s[(uint)pick.face] : nullptr;
	}
	return nullptr;
}

Graphics::UnicodeGlyphSource *ScummHiResText::perGlyphSourceFor(int charsetId, uint32 &cp) const {
	if (!_perGlyph)
		return nullptr;
	const int id = (charsetId >= 0 && charsetId < kMaxFonts) ? charsetId : 0;
	bool declined = false;
	return faceForCodePoint(id, cp, cp, declined);
}

// ---------------------------------------------------------------------
// drawing
// ---------------------------------------------------------------------

/**
 * Whether a glyph carries any ink.
 *
 * The layer's contract with every renderer is "false means I drew nothing, so
 * draw it yourself". A glyph that exists in the file but is empty breaks that
 * promise the expensive way: the caller is told the character was handled, so
 * the game's own picture is never drawn and the pixels simply go missing.
 *
 * That is not hypothetical. SCUMM games store small pictures in the control
 * code range - The Dig's option sliders are a run of 0x0B with one 0x0C for
 * the handle - and a Latin face baked from a TrueType font has 68 blank cells
 * in exactly that range. Accepting them erased the slider tracks from the
 * options menu while the labels around them rendered correctly.
 *
 * A space is blank too and is declined here as well, which costs nothing: the
 * original draws nothing for it either, and it then advances by the game's own
 * width like every other character the layer passes on.
 */
int ScummHiResText::scannedInkRight(Face *owner, Graphics::UnicodeGlyphSource *src, uint32 cp) const {
	if (owner) {
		Common::HashMap<uint32, int16>::const_iterator it = owner->inkRight.find(cp);
		if (it != owner->inkRight.end())
			return it->_value;
	}
	// The face draws its glyph from column 0 of a two-cell row, bearing
	// included; what matters to layout and to the dirty area is how far
	// the ink reaches.
	int right = 0;
	const int w = src->cellWidth() * 2;
	const int bpp = src->bitsPerPixel();
	for (int y = 0; y < src->cellHeight(); ++y) {
		const byte *row = src->row(cp, y);
		if (!row)
			break;
		for (int x = w - 1; x >= right; --x) {
			if (Graphics::TextCompose::expandCoverage(row, x, bpp)) {
				right = x + 1;
				break;
			}
		}
	}
	if (owner)
		owner->inkRight[cp] = (int16)right;
	return right;
}

bool ScummHiResText::glyphInk(Graphics::UnicodeGlyphSource *src, uint32 cp, int *inkRight) const {
	if (!src || src->cells(cp) <= 0)
		return false;

	Face *owner = faceForSource(src);

	// M7: an SVFN face's glyph box is the whole cell, not the ink extent -
	// it draws from column 0 of the cell, so a narrower box shrinks the
	// dirty rect below what was actually drawn (the old bitmap-only
	// glyphHasInk()/glyphInk() pair). The *existence* test is still the
	// face's own declared ink box (a proportional font's metrics table);
	// a fixed-width font has none, so glyphMetrics() answers false and
	// width/height stay 0 - fall back to the pixel scan for those (and for
	// a proportional font's genuinely blank glyph, which correctly stays
	// false either way).
	if (owner && !owner->ttf) {
		Graphics::GlyphMetrics m;
		bool hasInk = src->metrics(cp, m) && m.width > 0 && m.height > 0;
		if (!hasInk)
			hasInk = scannedInkRight(owner, src, cp) > 0;
		if (!hasInk)
			return false;
		if (inkRight)
			*inkRight = src->cellWidth();
		return true;
	}

	const int right = scannedInkRight(owner, src, cp);
	if (right <= 0)
		return false;
	if (inkRight)
		*inkRight = right;
	return true;
}

bool ScummHiResText::drawRows(Graphics::Surface &dest, Graphics::UnicodeGlyphSource &src, uint32 cp, int width,
							  int x, int y, byte color, byte shadowColor, int gameShadow,
							  Common::Rect *dirty, bool withCoverage,
							  Graphics::HiResMirror mirror, int axisLeft, int axisRight) {
	const int height = src.cellHeight();
	const int bpp = src.bitsPerPixel();
	Common::Array<byte> glyphBuf;
	glyphBuf.resize(width * height);
	for (int gy = 0; gy < height; ++gy) {
		const byte *row = src.row(cp, gy);
		if (!row)
			return false;
		Graphics::TextCompose::expandGlyphRow(&glyphBuf[gy * width], row, width, bpp, false, 0, 0);
	}

	// A flipped charset (C27): the rows turned within the cell, the glyph
	// reflected about its box across, so the string's order is kept.
	if (mirror != Graphics::kHiResMirrorNone && mirror != Graphics::kHiResMirrorGame) {
		Graphics::flipGlyph(glyphBuf.begin(), width, width, height, mirror);
		if (mirror == Graphics::kHiResMirrorHorizontal || mirror == Graphics::kHiResMirrorBoth)
			x = Graphics::mirroredLeft(x, width, axisLeft, axisRight);
	}

	Graphics::GlyphBitmap glyph;
	glyph.pixels = glyphBuf.begin();
	glyph.pitch = width;
	glyph.width = width;
	glyph.height = height;
	glyph.bpp = 8;

	const Graphics::GlyphStyle style = glyphStyle(legacyShadowConfig(), gameShadow, _korPatchShadow, color, shadowColor);
	const bool decorated = style.shadowMode != Graphics::kHiResShadowNone;

	Graphics::GlyphPlanes planes(&dest, (withCoverage && (bpp > 1 || decorated)) ? coverage() : nullptr);

	if (planes.coverage && decorated && _layeredDecorations && _overlay && &dest == &_overlay->index()) {
		if (!_overlay->underCoverage())
			_overlay->createUnder();
		planes.underIndex = _overlay->underIndex();
		planes.underCoverage = _overlay->underCoverage();
	}

	return Graphics::HiResGlyphRenderer::drawGlyph(planes, glyph, x, y, style, dirty);
}

bool ScummHiResText::drawGlyphPlaced(Graphics::Surface &dest, int chr, int charsetId,
									 int x, int y, byte color, byte shadowColor, int gameShadow,
									 Common::Rect *dirty, bool withCoverage, int gameAdvance) {
	const int id = (charsetId >= 0 && charsetId < kMaxFonts) ? charsetId : 0;
	uint32 cp = codePointFor(chr);
	if (!cp)
		return false;

	// L2/spec section 1: the game code a [glyphs] rule matches is the
	// decoded code point for anything past a single byte, not the raw
	// double-byte value.
	const uint32 code = (uint32)((chr < 0x100) ? chr : cp);
	bool declined = false;
	Graphics::UnicodeGlyphSource *src = faceForCodePoint(id, code, cp, declined);
	if (!src)
		return false;

	if (_logText)
		noteDrawn(charsetId, nullptr, chr);

	int width = 0;
	if (!glyphInk(src, cp, &width))
		return false;
	Graphics::GlyphMetrics m;
	if (!src->metrics(cp, m))
		return false;

	const Graphics::HiResIdPlan &plan = _plans[id];
	Face *srcFace = faceForSource(src);
	const bool ttf = srcFace && srcFace->ttf;
	// M4: only a face opened line-fit (sized to the game's own cell, the
	// start-up bake's rule) is clipped to it; a face given size=/pixel=
	// explicitly (lineFit false) draws at its own size uncapped.
	if (m.wide && ttf && srcFace->lineFit) {
		const int cellW = ttfCellWidth(charsetId);
		if (cellW > 0 && width > cellW)
			width = cellW;
	}

	int drawX = x - m.originX;
	int axisLeft = x, axisRight = x;
	if (m.combining) {
		if (_anchorValid && _anchorY == y) {
			drawX = _anchorX - m.originX;
			axisLeft = _anchorAxisLeft;
			axisRight = _anchorAxisRight;
		}
	} else {
		int own = src->advance(cp);
		if (own <= 0)
			own = m.advance;
		const Graphics::HiResAdvance rule = plan.advanceFor(cp);
		// H1: centre in the game cell exactly when the old per-family
		// metrics defaulted to Game - true for ASCII/"other" only when
		// something explicitly asked for it (advance=game), but true for a
		// *wide* glyph by default too (kHiResAdvanceEngine, nothing set),
		// unless it is a TrueType face stepping by its own advance instead
		// (the C31 default for a wide glyph - never centred).
		const bool faceSteps = m.wide && ttf && rule == Graphics::kHiResAdvanceEngine;
		const bool centre = gameAdvance > 0 && !faceSteps &&
							(rule == Graphics::kHiResAdvanceGame ||
							 (m.wide && rule == Graphics::kHiResAdvanceEngine));
		if (centre) {
			const int slack = gameAdvance * MAX(1, _scale) - own;
			if (slack > 1)
				drawX += slack / 2;
		}
		_anchorX = drawX + m.originX + own;
		_anchorY = y;
		_anchorValid = true;
		axisLeft = _anchorAxisLeft = drawX + m.originX;
		axisRight = _anchorAxisRight = _anchorX;
	}

	// M8 (controller ruling): any face other than the id's own primary one
	// (e.g. a range-named Latin SVF beside a CJK primary) always sits on
	// the primary's baseline, whatever origin= says - origin=face's own
	// meaning (dropping the game glyph's own offsets) is latinBaselineByFace()'s
	// concern in charset.cpp, unrelated to this implicit alignment.
	// originShiftFor() itself answers 0 when @p src already is the primary.
	const int baselineShift = originShiftFor(src, id);

	return drawRows(dest, *src, cp, width, drawX, y + baselineShift, color, shadowColor, gameShadow, dirty,
					withCoverage, mirrorFor(charsetId), axisLeft, axisRight);
}

bool ScummHiResText::isSourceTtf(Graphics::UnicodeGlyphSource *src) const {
	Face *f = faceForSource(src);
	return f && f->ttf != nullptr;
}

bool ScummHiResText::drawChar(Graphics::Surface &dest, int chr, int charsetId,
							  int x, int y, byte color, byte shadowColor,
							  int gameShadow, Common::Rect *dirty,
							  bool withCoverage, int gameAdvance) {
	if (!_enabled || !_fontsLoaded)
		return false;

	if (keepsGameFont(charsetId, chr)) {
		_anchorValid = false;
		return false;
	}

	if (_perGlyph) {
		const bool drawn = drawGlyphPlaced(dest, chr, charsetId, x, y, color, shadowColor, gameShadow, dirty,
										   withCoverage, gameAdvance);
		if (!drawn)
			_anchorValid = false;
		return drawn;
	}

	const bool wantLatin = (chr < 256);
	Face *face = faceForSimple(charsetId, wantLatin);
	if (!face)
		return false;

	if (_logText)
		noteDrawn(charsetId, face, chr);

	const uint32 cp = codePointFor(chr);
	if (!cp)
		return false;

	int width = 0;
	if (!glyphInk(face->source, cp, &width))
		return false;

	if (face->ttf) {
		const int cellW = ttfCellWidth(charsetId);
		if (cellW > 0 && width > cellW)
			width = cellW;
	}

	int baselineShift = 0;
	if (wantLatin)
		baselineShift = originShiftFor(face->source, charsetId);

	const Graphics::HiResMirror mirror = mirrorFor(charsetId);
	int advance = face->source->advance(cp);
	if (advance <= 0)
		advance = width;
	return drawRows(dest, *face->source, cp, width, x, y + baselineShift, color, shadowColor,
					gameShadow, dirty, withCoverage, mirror, x, x + advance);
}

Graphics::HiResTextConfig ScummHiResText::legacyShadowConfig() const {
	Graphics::HiResTextConfig c;
	c.scale = _scale;
	c.shadowMode = _map.shadowMode;
	c.shadowOffset = _map.shadowOffset;
	c.shadowColor = _map.shadowColor;
	c.shadowColorSet = _map.shadowColorSet;
	c.shadowWidthQ = _map.shadowWidthQ;
	c.shadowStyle = _map.shadowStyle;
	c.shadowShiftSet = _map.shadowShiftSet;
	c.shadowDx = _map.shadowDx;
	c.shadowDy = _map.shadowDy;
	c.shadowShiftColor = _map.shadowShiftColor;
	c.shadowShiftColorSet = _map.shadowShiftColorSet;
	c.shadowAlpha = _map.shadowAlpha;
	return c;
}

// ---------------------------------------------------------------------
// advance
// ---------------------------------------------------------------------

int ScummHiResText::advanceFor(int chr, int charsetId, int gameWidth, int *carry) const {
	if (!_enabled || !_fontsLoaded)
		return gameWidth;

	if (keepsGameFont(charsetId, chr))
		return gameWidth;

	if (!_perGlyph) {
		Face *face = faceForSimple(charsetId, chr < 256);
		if (!face)
			return gameWidth;
		const uint32 cp = codePointFor(chr);
		if (!cp)
			return gameWidth;
		// The map-less form has no [render] metrics=/ini hires_text_metrics
		// equivalent any more (Task 7): fontMetrics is always the "game"
		// (floor) rule, as an unset map-wide metrics= always was.
		return cellRuleAdvance(face, cp, charsetId, gameWidth, carry, /*fontMetrics*/ false, /*requireInk*/ true,
							   chr >= 256 && face->ttf != nullptr);
	}

	const int id = (charsetId >= 0 && charsetId < kMaxFonts) ? charsetId : 0;
	uint32 cp = codePointFor(chr);
	if (!cp)
		return gameWidth;

	// L2/spec section 1: [glyphs]' game code is the decoded code point past
	// a single byte, not the raw double-byte value.
	const uint32 code = (uint32)((chr < 0x100) ? chr : cp);
	bool declined = false;
	Graphics::UnicodeGlyphSource *src = faceForCodePoint(id, code, cp, declined);
	if (!src)
		return gameWidth;

	return advancePlaced(cp, src, id, gameWidth, carry);
}

int ScummHiResText::advancePlaced(uint32 cp, Graphics::UnicodeGlyphSource *src, int charsetId, int gameWidth,
								  int *carry) const {
	Graphics::GlyphMetrics m;
	if (!src->metrics(cp, m))
		return gameWidth;
	if (m.combining)
		return 0;

	const Graphics::HiResAdvance rule = _plans[charsetId].advanceFor(cp);
	const int scale = MAX(1, _scale);
	const bool ascii = (cp >= 0x20 && cp <= 0x7E);
	Face *face = faceForSource(src);

	// M6 (controller ruling): spec 6.3's game/font are exactly the old
	// metrics=game/font *for the ASCII/Latin rule*; a wide glyph always -
	// and a narrow non-ASCII one under the engine default (H2) - goes
	// through the legacy grid rule (cellRuleAdvance()'s ink floor, carry
	// and ink-reach widening), never the simple advanceGamePx() scaling.
	if (m.wide) {
		if (rule == Graphics::kHiResAdvanceCell) {
			const int raw = src->advanceWide();
			return raw > 0 ? MAX(1, raw / scale) : gameWidth;
		}
		const bool fontMetrics = (rule == Graphics::kHiResAdvanceFont);
		const bool faceFit = (rule == Graphics::kHiResAdvanceEngine) && face && face->ttf;
		return cellRuleAdvance(face, cp, charsetId, gameWidth, carry, fontMetrics, false, faceFit);
	}

	if (ascii) {
		if (rule == Graphics::kHiResAdvanceGame || rule == Graphics::kHiResAdvanceFont)
			return Graphics::advanceGamePx(rule, gameWidth, src->advance(cp), scale);
		if (rule == Graphics::kHiResAdvanceCell) {
			const int raw = src->advanceNarrow();
			return raw > 0 ? MAX(1, raw / scale) : gameWidth;
		}
		// kHiResAdvanceEngine for ASCII should not normally happen (the
		// engine scope always sets advance.basic-latin=game); fall back to
		// the face's own advance like "other" does, rather than the grid
		// rule cellRuleAdvance() is for wide glyphs.
		return Graphics::advanceGamePx(Graphics::kHiResAdvanceFont, gameWidth, src->advance(cp), scale);
	}

	// "other": non-wide, non-ASCII (Thai base letters, narrow punctuation,
	// a UTF-8 translation's own scripts, ...). H2 / the old otherMetrics
	// default: the face's own advance (kHiResAdvanceFont) unless something
	// explicit says otherwise.
	if (rule == Graphics::kHiResAdvanceCell) {
		const int raw = src->advanceNarrow();
		return raw > 0 ? MAX(1, raw / scale) : gameWidth;
	}
	const Graphics::HiResAdvance effective = (rule == Graphics::kHiResAdvanceEngine) ? Graphics::kHiResAdvanceFont : rule;
	return Graphics::advanceGamePx(effective, gameWidth, src->advance(cp), scale);
}

int ScummHiResText::cellRuleAdvance(Face *face, uint32 cp, int charsetId, int gameWidth, int *carry,
									bool fontMetrics, bool requireInk, bool faceFit) const {
	if (!face || !face->source)
		return gameWidth;
	Graphics::UnicodeGlyphSource *src = face->source;

	// The old ink floor/widening is the pixel-scanned reach, not the whole
	// cell that M7 hands to the renderer for a bitmap face's draw box: use
	// glyphInk() only for the "does it have ink at all" existence test (which
	// still needs its own font-vs-scan logic for a proportional face with an
	// empty metrics table) and scannedInkRight() itself for the value that
	// feeds the advance rule below, for every kind of face alike.
	bool hasInk = glyphInk(src, cp, nullptr);
	int inkRight = hasInk ? scannedInkRight(face, src, cp) : 0;
	if (!hasInk) {
		if (requireInk || src->cells(cp) <= 0)
			return gameWidth;
		inkRight = 0; // a blank-but-present glyph (e.g. an ideographic space) still advances by the face
	}

	int advance = 0;
	if (!face->ttf) {
		// M7: a proportional SVFN steps by its own advance, widened to
		// clear ink that reaches one pixel past it (some baked glyphs do);
		// a fixed-width one (no metrics table: advance() answers 0) steps
		// by the cell, as it always drew the whole cell.
		const int own = src->advance(cp);
		if (own > 0)
			advance = MAX(own, inkRight);
		else
			advance = src->cellWidth();
	} else {
		const int cellW = face->lineFit ? ttfCellWidth(charsetId) : 0;
		if (cellW > 0 && inkRight > cellW)
			inkRight = cellW;
		advance = MAX(src->advance(cp), inkRight);
	}
	if (advance <= 0)
		return gameWidth;

	const int m = MAX(1, _scale);
	if (faceFit && face->ttf)
		return MAX(1, (m > 1) ? (advance + m - 1) / m : advance);

	if (!fontMetrics) {
		const int fit = (m > 1) ? (advance + m - 1) / m : advance;
		return MAX(fit, gameWidth);
	}

	// advance=font: spend the fractional pixel a previous character in the
	// run could not on this one, so the run as a whole keeps the font's own
	// spacing (advanceFor()'s own carry doc).
	if (m > 1) {
		if (carry) {
			const int total = advance + *carry;
			int whole = total / m;
			*carry = total - whole * m;
			if (whole < 1) {
				whole = 1;
				*carry = 0;
			}
			advance = whole;
		} else {
			advance = (advance + m - 1) / m;
		}
	}
	return advance;
}

bool ScummHiResText::latinStepsByFace(int chr, int charsetId) const {
	if (!_enabled || !_fontsLoaded || !_latinFaceStepAllowed)
		return false;
	if (chr < 0x20 || chr > 0x7E)
		return false;
	if (keepsGameFont(charsetId, chr))
		return false;

	if (_perGlyph) {
		const int id = (charsetId >= 0 && charsetId < kMaxFonts) ? charsetId : 0;
		uint32 cp = codePointFor(chr);
		if (!cp)
			return false;
		bool declined = false;
		Graphics::UnicodeGlyphSource *src = faceForCodePoint(id, (uint32)chr, cp, declined);
		if (!src || !isSourceTtf(src))
			return false;
		if (!glyphInk(src, cp, nullptr))
			return false;
		return _plans[id].advanceFor(cp) == Graphics::kHiResAdvanceFont;
	}

	Face *face = faceForSimple(charsetId, true);
	if (!face || !face->ttf)
		return false;
	const uint32 cp = codePointFor(chr);
	return cp != 0 && glyphInk(face->source, cp, nullptr);
}

bool ScummHiResText::latinBaselineByFace(int chr, int charsetId) const {
	if (!_enabled || !_fontsLoaded || !_latinFaceStepAllowed || !_perGlyph)
		return false;
	if (keepsGameFont(charsetId, chr))
		return false;

	const int id = (charsetId >= 0 && charsetId < kMaxFonts) ? charsetId : 0;
	uint32 cp = codePointFor(chr);
	if (!cp)
		return false;
	bool declined = false;
	Graphics::UnicodeGlyphSource *src = faceForCodePoint(id, (uint32)chr, cp, declined);
	if (!src || isSourceTtf(src))
		return false; // a TrueType face is handled by latinStepsByFace(); the space has no ink to place
	if (!glyphInk(src, cp, nullptr))
		return false;
	return _plans[id].originFor(cp) == Graphics::kHiResOriginFace;
}

bool ScummHiResText::drawsCode(int chr, int charsetId) const {
	if (!_enabled || !_fontsLoaded)
		return false;
	if (keepsGameFont(charsetId, chr))
		return false;

	if (_perGlyph) {
		const int id = (charsetId >= 0 && charsetId < kMaxFonts) ? charsetId : 0;
		uint32 cp = codePointFor(chr);
		const uint32 code = (uint32)((chr < 0x100) ? chr : cp);
		bool declined = false;
		Graphics::UnicodeGlyphSource *src = faceForCodePoint(id, code, cp, declined);
		return src && glyphInk(src, cp, nullptr);
	}

	Face *face = faceForSimple(charsetId, chr < 256);
	if (!face)
		return false;
	const uint32 cp = codePointFor(chr);
	return cp != 0 && glyphInk(face->source, cp, nullptr);
}

// ---------------------------------------------------------------------
// misc small helpers (unchanged behaviour)
// ---------------------------------------------------------------------

void ScummHiResText::updatePaletteCache(const Graphics::PixelFormat &format,
										const byte *rgb, uint first, uint num) {
	if (first >= 256)
		return;
	if (first + num > 256)
		num = 256 - first;

	memcpy(_paletteRGB + first * 3, rgb, num * 3);
	for (uint i = 0; i < num; ++i) {
		_paletteCache[first + i] = format.RGBToColor(rgb[i * 3 + 0],
													 rgb[i * 3 + 1],
													 rgb[i * 3 + 2]);
	}
}

uint32 ScummHiResText::codePointFor(int chr) const {
	if (_encoding == Common::kUtf8) {
		if (chr >= 0xF780 && chr <= 0xF7FF)
			return 0;
		return chr > 0 ? (uint32)chr : 0;
	}

	byte bytes[2];
	int len;
	if (chr < 256) {
		bytes[0] = (byte)chr;
		len = 1;
	} else {
		bytes[0] = (byte)(chr & 0xFF);
		bytes[1] = (byte)(chr >> 8);
		len = 2;
	}

	const byte *p = bytes;
	const uint32 codepoint = decodeNext(p, bytes + len);

	if (codepoint == 0xFFFD)
		return 0;
	return codepoint;
}

Graphics::PixelFormat ScummHiResText::cursorFormat(const Graphics::PixelFormat &screenFormat) const {
	if (_alphaActive)
		return Graphics::PixelFormat::createFormatCLUT8();
	return screenFormat;
}

void ScummHiResText::createCoverage(int w, int h) {
	freeCoverage();
	if (!_enabled || !_wantsAlpha || w <= 0 || h <= 0)
		return;
	if (_overlay)
		_overlay->createCoverage(w, h);
}

void ScummHiResText::freeCoverage() {
	if (_overlay)
		_overlay->freeCoverage();
}

void ScummHiResText::noteDrawn(int charsetId, const Face *face, int chr) const {
	if (charsetId != _logCharset || face != _logFace) {
		flushTextLog();
		_logCharset = charsetId;
		_logFace = face;
	}

	const uint32 cp = codePointFor(chr);
	if (!cp) {
		_logRun += '?';
		return;
	}

	if (cp < 0x80) {
		_logRun += (char)cp;
	} else if (cp < 0x800) {
		_logRun += (char)(0xC0 | (cp >> 6));
		_logRun += (char)(0x80 | (cp & 0x3F));
	} else if (cp < 0x10000) {
		_logRun += (char)(0xE0 | (cp >> 12));
		_logRun += (char)(0x80 | ((cp >> 6) & 0x3F));
		_logRun += (char)(0x80 | (cp & 0x3F));
	} else {
		_logRun += (char)(0xF0 | (cp >> 18));
		_logRun += (char)(0x80 | ((cp >> 12) & 0x3F));
		_logRun += (char)(0x80 | ((cp >> 6) & 0x3F));
		_logRun += (char)(0x80 | (cp & 0x3F));
	}
}

void ScummHiResText::flushTextLog() const {
	if (_logRun.empty())
		return;
	debug("HRTEXT charset=%d \"%s\"", _logCharset, _logRun.c_str());
	_logRun.clear();
}

// ---------------------------------------------------------------------
// map-less, name-only form (probeSimpleFonts())
// ---------------------------------------------------------------------

static const char *const kSimpleFontLegacy = "hires%02d.fnt";
static const char *const kSimpleFontSingle = "hires.fnt";
static const char *const kSimpleLatinPattern = "hrlat%02d.fnt";

static const char *simpleFontPatternFor(Common::Language language) {
	switch (language) {
	case Common::KO_KOR:
		return "hrkor%02d.fnt";
	case Common::JA_JPN:
		return "hrjpn%02d.fnt";
	case Common::ZH_CHN:
		return "hrchs%02d.fnt";
	case Common::ZH_TWN:
		return "hrcht%02d.fnt";
	default:
		return nullptr;
	}
}

static bool isSingleByteFont(const Graphics::HiResBitmapFont &font) {
	return font.codePage() == Common::kISO8859_1 ||
		   font.codePage() == Common::kWindows1252;
}

bool ScummHiResText::probeSimpleFonts(const Common::Path &gameDir, Common::Language language) {
	int smallestCell = 0;
	int found = 0;
	int singleByte = 0;
	bool anyCoverage = false;

	const char *cjkPattern = simpleFontPatternFor(language);
	Common::String usedPattern;

	Graphics::HiResBitmapFont probe;
	for (int pass = 0; pass < 2 && found == 0; ++pass) {
		const char *pattern = (pass == 0) ? cjkPattern : kSimpleFontLegacy;
		if (!pattern)
			continue;

		for (int i = 0; i < kMaxFonts; ++i) {
			const Common::String name = expandFontPattern(pattern, i);
			Common::FSNode node(gameDir.appendComponent(name));
			if (!node.exists())
				continue;
			Common::SeekableReadStream *stream = node.createReadStream();
			if (!stream)
				continue;
			const bool ok = probe.load(*stream);
			if (!ok) {
				delete stream;
				warning("SCUMM: %s is not a usable hi-res font", name.c_str());
				continue;
			}
			++found;
			if (smallestCell == 0 || probe.cellHeight() < smallestCell)
				smallestCell = probe.cellHeight();
			anyCoverage = anyCoverage || probe.bpp() > 1;
			if (_simpleCellCount < kMaxFonts)
				_simpleCells[_simpleCellCount++] = probe.cellHeight();
			if (isSingleByteFont(probe))
				++singleByte;
			probe.free();
			delete stream;
		}

		if (found > 0)
			usedPattern = pattern;
	}

	bool haveSingle = false;
	bool singleIsLatin = false;
	{
		Common::FSNode node(gameDir.appendComponent(kSimpleFontSingle));
		if (node.exists()) {
			Common::SeekableReadStream *stream = node.createReadStream();
			if (stream) {
				if (probe.load(*stream)) {
					haveSingle = true;
					if (smallestCell == 0 || probe.cellHeight() < smallestCell)
						smallestCell = probe.cellHeight();
					anyCoverage = anyCoverage || probe.bpp() > 1;
					singleIsLatin = isSingleByteFont(probe);
					probe.free();
				} else {
					warning("SCUMM: %s is not a usable hi-res font", kSimpleFontSingle);
				}
				delete stream;
			}
		}
	}

	bool haveLatin = false;
	{
		Graphics::HiResBitmapFont latinProbe;
		for (int i = 0; i < kMaxFonts; ++i) {
			const Common::String name = expandFontPattern(kSimpleLatinPattern, i);
			Common::FSNode node(gameDir.appendComponent(name));
			if (!node.exists())
				continue;
			Common::SeekableReadStream *stream = node.createReadStream();
			if (!stream)
				continue;
			if (latinProbe.load(*stream)) {
				haveLatin = true;
				if (smallestCell == 0 || latinProbe.cellHeight() < smallestCell)
					smallestCell = latinProbe.cellHeight();
				if (_simpleCellCount < kMaxFonts)
					_simpleCells[_simpleCellCount++] = latinProbe.cellHeight();
				anyCoverage = anyCoverage || latinProbe.bpp() > 1;
				latinProbe.free();
			} else {
				warning("SCUMM: %s is not a usable hi-res font", name.c_str());
			}
			delete stream;
		}
	}

	if (!found && !haveSingle && !haveLatin)
		return false;

	const bool numberedAreLatin = (found > 0 && singleByte == found);

	if (found) {
		if (numberedAreLatin)
			_simpleLatinBitmapName = usedPattern;
		else
			_simpleBitmapPattern = usedPattern;
	}
	if (haveSingle) {
		if (singleIsLatin) {
			if (_simpleLatinBitmapName.empty())
				_simpleLatinBitmapName = kSimpleFontSingle;
		} else {
			_simpleBitmapSingle = kSimpleFontSingle;
		}
	}
	if (haveLatin) {
		_simpleLatinBitmapName = kSimpleLatinPattern;
	}

	_wantsAlpha = _wantsAlpha || anyCoverage;

	_simpleFonts = true;
	_simpleCellHeight = smallestCell;
	debug(1, "SCUMM: hi-res fonts found by name (no map): %d %s%s%s%s, "
			 "smallest cell %d",
		  found, found ? usedPattern.c_str() : "numbered",
		  haveSingle ? " + single" : "",
		  numberedAreLatin ? " (single-byte, used as Latin)" : "",
		  haveLatin ? ", with Latin" : "", smallestCell);
	return true;
}

bool ScummHiResText::cellMatchesScale(const int *cells, int count,
									  int gameFontHeight, int scale) {
	if (!cells || count <= 0 || gameFontHeight <= 0 || scale <= 0)
		return false;
	for (int i = 0; i < count; ++i) {
		if (cells[i] == gameFontHeight * scale)
			return true;
	}
	return false;
}

void ScummHiResText::resolveScale(int gameFontHeight) {
	if (!_enabled || !_simpleFonts)
		return;

	if (_scaleFromUser) {
		if (gameFontHeight > 0 && _simpleCellCount > 0 &&
			!cellMatchesScale(_simpleCells, _simpleCellCount, gameFontHeight, _scale))
			warning("SCUMM: hi-res fonts are %dpx, which is not %d times the "
					"%dpx game font; drawing them anyway because the scale was "
					"asked for. Bake them at %dpx to fit",
					_simpleCellHeight, _scale, gameFontHeight,
					gameFontHeight * _scale);
		return;
	}

	if (gameFontHeight > 0 && _simpleCellCount > 0) {
		int scale = 0;
		for (int s = 1; s <= 3 && scale == 0; ++s) {
			if (cellMatchesScale(_simpleCells, _simpleCellCount, gameFontHeight, s))
				scale = s;
		}

		if (scale == 0) {
			warning("SCUMM: hi-res fonts are %dpx for a %dpx game font, which is "
					"not a whole multiple; ignoring them. Bake them at %dpx or %dpx",
					_simpleCellHeight, gameFontHeight,
					gameFontHeight * 2, gameFontHeight * 3);
			reset();
			return;
		}

		_scale = scale;
		debug(1, "SCUMM: hi-res scale %d from the fonts (a %dpx cell over the "
				 "%dpx game font)", scale, gameFontHeight * scale, gameFontHeight);
		return;
	}

	_scale = 1;
	debug(1, "SCUMM: hi-res scale 1 from the fonts (%dpx cell over %dpx game font)",
		  _simpleCellHeight, gameFontHeight);
}

bool ScummHiResText::loadSimpleBitmapFile(const Common::Path &gameDir, const Common::String &name,
										  int charsetId, bool latin) {
	Common::FSNode node(gameDir.appendComponent(name));
	if (!node.exists())
		return false;
	Common::SeekableReadStream *stream = node.createReadStream();
	if (!stream)
		return false;

	Graphics::HiResBitmapFont *font = new Graphics::HiResBitmapFont();
	if (!font->load(*stream)) {
		delete font;
		delete stream;
		warning("SCUMM: %s is not a usable hi-res font", name.c_str());
		return false;
	}
	delete stream;

	const Common::String key = Common::String::format("svfn:%s@%d", name.c_str(), font->cellHeight());
	Face *face = nullptr;
	Common::HashMap<Common::String, Face *>::iterator it = _sources.find(key);
	if (it != _sources.end()) {
		delete font;
		face = it->_value;
	} else {
		face = new Face();
		face->source = new Graphics::SvfnGlyphSource(font, DisposeAfterUse::YES);
		_sources[key] = face;
		_faceBySource[(uint64)(uintptr)face->source] = face;
	}

	if (charsetId < 0) {
		for (int i = 0; i < kMaxFonts; ++i) {
			if (latin) {
				if (!_simpleLatinFaces[i])
					_simpleLatinFaces[i] = face;
			} else if (!_simpleCjkFaces[i]) {
				_simpleCjkFaces[i] = face;
			}
		}
	} else if (latin) {
		_simpleLatinFaces[charsetId] = face;
	} else {
		_simpleCjkFaces[charsetId] = face;
	}

	debug(1, "SCUMM: hi-res %s%d <- %s: %dx%d cell, %d bpp",
		  latin ? "Latin font " : "font ", MAX(charsetId, 0), name.c_str(),
		  face->source->cellWidth(), face->source->cellHeight(), face->source->bitsPerPixel());

	_fontsLoaded = true;
	return true;
}

ScummHiResText::Face *ScummHiResText::faceForSimple(int charsetId, bool latin) const {
	if (!_fontsLoaded)
		return nullptr;
	const bool inRange = charsetId >= 0 && charsetId < kMaxFonts;

	if (latin) {
		if (inRange && _simpleLatinFaces[charsetId])
			return _simpleLatinFaces[charsetId];
		return ttfFaceForSimple(charsetId);
	}

	if (inRange && _simpleCjkFaces[charsetId])
		return _simpleCjkFaces[charsetId];

	if (Face *ttf = ttfFaceForSimple(charsetId))
		return ttf;

	const int nearest = nearestFont(charsetId);
	if (nearest >= 0)
		return _simpleCjkFaces[nearest];

	return nullptr;
}

bool ScummHiResText::ttfSizeForSimple(int charsetId, int &pixelSize, bool &lineFit) const {
	if (charsetId < 0 || charsetId >= kMaxFonts)
		return false;

	if (_perGlyph && _plans[charsetId].sizeSet) {
		pixelSize = _plans[charsetId].size;
		lineFit = false;
		return true;
	}

	int height = _gameFontH[charsetId];
	if (height <= 0) {
		const int nearest = nearestTtfCharset(charsetId);
		if (nearest < 0)
			return false;
		height = _gameFontH[nearest];
	}
	pixelSize = height * MAX(1, _scale);
	lineFit = true;
	return true;
}

ScummHiResText::Face *ScummHiResText::ttfFaceForSimple(int charsetId) const {
	if (charsetId < 0 || charsetId >= kMaxFonts || _ttfPathSimple.empty())
		return nullptr;

	int pixelSize = 0;
	bool lineFit = true;
	if (!ttfSizeForSimple(charsetId, pixelSize, lineFit))
		return nullptr;

	if (_ttfFacePx[charsetId] == pixelSize)
		return _ttfFaces[charsetId];

	Common::Array<Common::Path> chain;
	chain.push_back(_ttfPathSimple);
	Face *face = openTtfChain(chain, pixelSize, lineFit, 0);
	_ttfFaces[charsetId] = face;
	_ttfFacePx[charsetId] = pixelSize;
	return face;
}

int ScummHiResText::ttfCellWidth(int charsetId) const {
	if (charsetId < 0 || charsetId >= kMaxFonts)
		return 0;
	int width = _gameFontW[charsetId];
	if (_gameFontH[charsetId] <= 0) {
		const int nearest = nearestTtfCharset(charsetId);
		width = (nearest >= 0) ? _gameFontW[nearest] : 0;
	}
	return width * MAX(1, _scale);
}

ScummHiResText::Face *ScummHiResText::openTtfChain(const Common::Array<Common::Path> &chain,
												  int pixelSize, bool lineFit, int pixelGrid) const {
	Common::String chainKey;
	for (uint i = 0; i < chain.size(); ++i) {
		if (i)
			chainKey += '|';
		chainKey += chain[i].toString('/');
	}
	Common::String key = Common::String::format("ttfchain:%s@%d%s", chainKey.c_str(), pixelSize,
												lineFit ? "" : "c");
	if (pixelGrid > 0)
		key += Common::String::format("p%d", pixelGrid);
	Common::HashMap<Common::String, Face *>::iterator it = _sources.find(key);
	if (it != _sources.end())
		return it->_value;

	_sources[key] = nullptr;

#ifdef USE_FREETYPE2
	if (pixelSize < Graphics::TtfGlyphSource::kMinPixelSize ||
		pixelSize > Graphics::TtfGlyphSource::kMaxPixelSize) {
		warning("SCUMM: hi-res TrueType font cannot be drawn at %dpx", pixelSize);
		return nullptr;
	}

	const bool requireHangul = (_encoding == Common::kWindows949 || _encoding == Common::kJohab);

	Common::Array<Graphics::UnicodeGlyphSource *> sources;
	Common::Array<Common::String> names;
	for (uint i = 0; i < chain.size(); ++i) {
		const Common::String path = chain[i].toString('/');
		if (_failedFaces.contains(path))
			continue;

		int32 faceIndex = 0;
		Common::String openError;
		Common::SeekableReadStream *stream = Graphics::openFontFace(chain[i], faceIndex, openError);
		if (!stream) {
			Common::String unusedFile;
			int32 unusedIndex;
			if (Graphics::splitFontFaceIndex(chain[i].baseName(), unusedFile, unusedIndex))
				warning("SCUMM: cannot open hi-res TrueType font '%s': %s", chain[i].toString().c_str(), openError.c_str());
			else
				warning("SCUMM: cannot open hi-res TrueType font '%s'", chain[i].toString().c_str());
			_failedFaces[path] = true;
			continue;
		}

		Common::String error;
		Graphics::TtfGlyphSource *ttf;
		if (pixelGrid > 0 && i == 0)
			ttf = Graphics::TtfGlyphSource::createPixel(stream, DisposeAfterUse::YES, pixelSize, pixelGrid,
														error, faceIndex);
		else
			ttf = Graphics::TtfGlyphSource::create(stream, DisposeAfterUse::YES, pixelSize, error,
												   requireHangul, lineFit,
												   _fitProbes.empty() ? nullptr : _fitProbes.begin(),
												   _fitProbes.size(), faceIndex);
		if (!ttf) {
			warning("SCUMM: cannot use hi-res TrueType font '%s': %s",
					chain[i].toString().c_str(), error.c_str());
			_failedFaces[path] = true;
			continue;
		}
		ttf->setCoverageGamma(_map.coverageGamma);
		debug(1, "SCUMM: hi-res TrueType font %s opened at %dpx: %u probe glyphs rasterised in %u ms",
			  chain[i].baseName().c_str(), pixelSize, ttf->rasterCount(), ttf->totalRenderMs());
		sources.push_back(ttf);
		names.push_back(chain[i].baseName());
	}
	if (sources.empty())
		return nullptr;

	Face *face = new Face();
	face->ttf = static_cast<Graphics::TtfGlyphSource *>(sources[0]);
	face->source = (sources.size() == 1) ? sources[0]
				   : new Graphics::FallbackGlyphSource(sources, DisposeAfterUse::YES);
	face->pixelSize = pixelSize;
	face->lineFit = lineFit;
	face->chain = sources;
	face->chainNames = names;
	_sources[key] = face;
	_faceBySource[(uint64)(uintptr)face->source] = face;

	checkCoverage(face->chain, face->chainNames, chainKey);
	return face;
#else
	warning("SCUMM: hi-res TrueType fonts need a build with FreeType; "
			"bake the font to .fnt instead");
	return nullptr;
#endif
}

void ScummHiResText::checkCoverage(const Common::Array<Graphics::UnicodeGlyphSource *> &sources,
								   const Common::Array<Common::String> &names, const Common::String &key) const {
	if (_coverageSample.empty() || sources.empty() || _coverageChecked.contains(key))
		return;
	_coverageChecked[key] = true;

	Common::Array<uint32> wanted = _coverageSample;
	for (uint i = 0; i < sources.size() && !wanted.empty(); ++i) {
		Graphics::UnicodeGlyphSource *src = sources[i];
		if (!src)
			continue;
		const Graphics::CoverageReport report = Graphics::checkCoverage(src, wanted);
		const Common::String fallback = (i + 1 < names.size()) ? names[i + 1]
																: Common::String("the game's font");
		const Common::String text = Graphics::coverageWarning(i < names.size() ? names[i] : Common::String(), report, fallback);
		if (!text.empty()) {
			Common::String line;
			for (uint c = 0; c <= text.size(); ++c) {
				if (c == text.size() || text[c] == '\n') {
					if (!line.empty()) {
						warning("SCUMM: %s", line.c_str());
						_coverageWarnings.push_back(line);
					}
					line.clear();
				} else {
					line += text[c];
				}
			}
		}

		Common::Array<uint32> missing;
		for (uint k = 0; k < wanted.size(); ++k) {
			if (src->cells(wanted[k]) <= 0)
				missing.push_back(wanted[k]);
		}
		wanted = missing;
	}
}

// ---------------------------------------------------------------------
// loadConfig / loadFonts
// ---------------------------------------------------------------------

bool ScummHiResText::loadFonts(const Common::Path &gameDir) {
	_fontsLoaded = false;
	freeFaces();

	if (!_enabled)
		return false;

	{
		const Common::CodePage page = _encoding;
		if ((page == Common::kWindows932 || page == Common::kWindows936 ||
			 page == Common::kWindows949 || page == Common::kWindows950 ||
			 page == Common::kJohab) && !_warnedTables && !cjkTablesPresent(page)) {
			_warnedTables = true;
			warning("SCUMM: encoding.dat not found (pass --extrapath to dists/engine-data); "
					"CJK glyphs disabled");
		}
	}

	const uint32 startMs = g_system ? g_system->getMillis() : 0;

	_coverageSample.clear();
	_fitProbes.clear();
	if (_translationCps.size() > 0) {
		_translationCps.sample(64, _coverageSample);
		_translationCps.fitProbes(Graphics::TtfGlyphSource::kMaxExtraFitProbes, _fitProbes);
	}

	if (!_perGlyph) {
		if (!_simpleBitmapPattern.empty()) {
			for (int i = 0; i < kMaxFonts; ++i) {
				const Common::String name = expandFontPattern(_simpleBitmapPattern, i);
				if (name.empty())
					break;
				loadSimpleBitmapFile(gameDir, name, i, false);
			}
		}
		if (!_simpleBitmapSingle.empty())
			loadSimpleBitmapFile(gameDir, _simpleBitmapSingle, -1, false);

		if (!_simpleLatinBitmapName.empty()) {
			if (_simpleLatinBitmapName.contains('%')) {
				for (int i = 0; i < kMaxFonts; ++i) {
					const Common::String name = expandFontPattern(_simpleLatinBitmapName, i);
					if (name.empty())
						break;
					loadSimpleBitmapFile(gameDir, name, i, true);
				}
			} else if (!Common::FSNode(gameDir.appendComponent(_simpleLatinBitmapName)).exists()) {
				warning("SCUMM: hi-res Latin font not found: %s", _simpleLatinBitmapName.c_str());
			} else {
				loadSimpleBitmapFile(gameDir, _simpleLatinBitmapName, -1, true);
			}
		}

		if (!_fontsLoaded)
			warning("SCUMM: hi-res text is configured but no replacement font loaded");
	} else {
		Common::HashMap<Common::String, bool> attempted;
		for (int id = 0; id < kMaxFonts; ++id) {
			const Common::Array<Common::String> paths = collectFacePaths(id);
			for (uint i = 0; i < paths.size(); ++i) {
				if (attempted.contains(paths[i]))
					continue;
				attempted[paths[i]] = true;

				Common::FSNode node(Common::Path(paths[i], '/'));
				if (!node.exists()) {
					warning("SCUMM: cannot open hi-res font '%s'", paths[i].c_str());
					_failedFaces[paths[i]] = true;
					continue;
				}
				Common::SeekableReadStream *stream = node.createReadStream();
				if (!stream) {
					warning("SCUMM: cannot open hi-res font '%s'", paths[i].c_str());
					_failedFaces[paths[i]] = true;
					continue;
				}
				byte head[4];
				const bool svfn = stream->read(head, sizeof(head)) == sizeof(head) &&
								  Graphics::isSvfnFile(head, sizeof(head));
				stream->seek(0);
				if (svfn)
					addFace(paths[i], *stream);
				// A TrueType path is left for ensureChainSources() below,
				// which knows the id's resolved pixel size.
				delete stream;
			}
		}

		for (int id = 0; id < kMaxFonts; ++id)
			ensureChainSources(id);

		// C1: a TrueType face named for a game/charset whose cell is not yet
		// known (any SCUMM version but the CJK ones only calls
		// setGameFontCell() before loadFonts() - scumm.cpp) is not open yet
		// (ensureChainSources() has nothing to size it with), but it is not
		// a *failure* either: noteGameCharset()/setGameFontCell() can still
		// give it a size later, and every draw call re-tries
		// ensureChainSources(). _fontsLoaded must stay true for that pending
		// case - false only when every path this map/ini named has
		// definitely failed to open at all.
		bool anyOpen = false;
		for (int id = 0; id < kMaxFonts && !anyOpen; ++id) {
			for (uint c = 0; c < _chainSources[id].size() && !anyOpen; ++c)
				for (uint i = 0; i < _chainSources[id][c].size() && !anyOpen; ++i)
					anyOpen = _chainSources[id][c][i] != nullptr;
			for (uint i = 0; i < _targetSources[id].size() && !anyOpen; ++i)
				anyOpen = _targetSources[id][i] != nullptr;
		}
		if (anyOpen) {
			_fontsLoaded = true;
		} else {
			bool anyPending = false;
			for (Common::HashMap<Common::String, bool>::const_iterator it = attempted.begin();
				 it != attempted.end() && !anyPending; ++it) {
				const Common::String &p = it->_key;
				const bool failed = _failedFaces.contains(p) ||
									 (_sources.contains(p) && !sourceForFace(p));
				anyPending = !failed;
			}
			_fontsLoaded = anyPending;
			if (!_fontsLoaded)
				warning("SCUMM: hi-res text is configured but no replacement font loaded");
		}
	}

	if (g_system)
		debug(1, "SCUMM: hi-res fonts loaded in %u ms (%d glyph sources open)",
			  g_system->getMillis() - startMs, sourceCount());

	return _fontsLoaded;
}

bool ScummHiResText::hasFonts() const {
	return _fontsLoaded;
}

int ScummHiResText::sourceCount() const {
	int n = 0;
	for (Common::HashMap<Common::String, Face *>::const_iterator it = _sources.begin();
		 it != _sources.end(); ++it) {
		if (it->_value)
			++n;
	}
	return n;
}

Graphics::TtfGlyphSource *ScummHiResText::ttfChainFace(int charsetId, uint index) const {
	const Face *face = _fontsLoaded ? ttfFaceForSimple(charsetId) : nullptr;
	if (!face || index >= face->chain.size())
		return nullptr;
	return static_cast<Graphics::TtfGlyphSource *>(face->chain[index]);
}

Graphics::UnicodeGlyphSource *ScummHiResText::sourceFor(int charsetId, bool latin) const {
	Face *face = faceForSimple(charsetId, latin);
	return face ? face->source : nullptr;
}

void ScummHiResText::setCharsetGrid(int charsetId, int width, int height) {
	if (charsetId >= 0 && charsetId < kMaxFonts) {
		_charsetWidths[charsetId] = width;
		_charsetHeights[charsetId] = height;
	}
}

void ScummHiResText::setGameFontCell(int charsetId, int width, int height) {
	if (charsetId >= 0 && charsetId < kMaxFonts) {
		_gameFontW[charsetId] = width;
		_gameFontH[charsetId] = height;
		if (width > 0 && height > 0)
			_cjkCells = true;
	}
}

void ScummHiResText::noteGameCharset(int charsetId, int width, int height) {
	if (charsetId < 0 || charsetId >= kMaxFonts || width <= 0 || height <= 0)
		return;
	if (_gameFontW[charsetId] > 0 && _gameFontH[charsetId] > 0)
		return;
	_gameFontW[charsetId] = width;
	_gameFontH[charsetId] = height;
}

int ScummHiResText::nearestFont(int charsetId) const {
	const int want = (charsetId >= 0 && charsetId < kMaxFonts) ? _charsetWidths[charsetId] : 0;
	if (want <= 0)
		return -1;
	const int m = MAX(1, _scale);

	int best = -1, bestDelta = 0;
	for (int i = 0; i < kMaxFonts; ++i) {
		if (!_simpleCjkFaces[i])
			continue;
		const int delta = ABS((int)_simpleCjkFaces[i]->source->cellWidth() / m - want);
		if (best < 0 || delta < bestDelta) {
			best = i;
			bestDelta = delta;
		}
	}
	return best;
}

int ScummHiResText::nearestTtfCharset(int charsetId) const {
	const int want = (charsetId >= 0 && charsetId < kMaxFonts) ? _charsetWidths[charsetId] : 0;
	if (want <= 0)
		return -1;

	int best = -1, bestDelta = 0;
	for (int i = 0; i < kMaxFonts; ++i) {
		if (_gameFontH[i] <= 0)
			continue;
		const int delta = ABS(_gameFontW[i] - want);
		if (best < 0 || delta < bestDelta) {
			best = i;
			bestDelta = delta;
		}
	}
	return best;
}

// ---------------------------------------------------------------------
// shadow / decoration / dirty-rect geometry (unchanged since before Task 7)
// ---------------------------------------------------------------------

Graphics::HiResShadowMode ScummHiResText::resolveShadow(Graphics::HiResShadowMode fromMap,
														 int gameShadow, bool korPatchShadow) {
	if (fromMap != Graphics::kHiResShadowGame)
		return fromMap;

	switch (gameShadow) {
	case 0:
		return korPatchShadow ? Graphics::kHiResShadowOutline : Graphics::kHiResShadowNone;
	case 1:
		return Graphics::kHiResShadowNone;
	case 2:
		return Graphics::kHiResShadowDrop;
	case 3:
		return Graphics::kHiResShadowStroke;
	default:
		return Graphics::kHiResShadowOutline;
	}
}

Common::Rect ScummHiResText::gameRectFor(const Common::Rect &area, int m) {
	if (area.isEmpty())
		return Common::Rect();
	m = MAX(1, m);
	auto floorDiv = [](int v, int d) { return (v >= 0) ? v / d : -((-v + d - 1) / d); };
	return Common::Rect(floorDiv(area.left, m), floorDiv(area.top, m),
						-floorDiv(-area.right, m), -floorDiv(-area.bottom, m));
}

Common::Rect ScummHiResText::overlayRectFor(const Common::Rect &rect, int topOffset, int m) {
	if (rect.isEmpty())
		return Common::Rect();
	m = MAX(1, m);
	return Common::Rect(rect.left * m, (rect.top + topOffset) * m,
						rect.right * m, (rect.bottom + topOffset) * m);
}

void ScummHiResText::retireTracedGlyphs(const Common::Rect &painted, Common::Array<TracedGlyph> &glyphs,
										Common::Array<Common::Rect> &clear) {
	retireGlyphsByCell(painted, glyphs, clear, true, false);
}

void ScummHiResText::retireGlyphsByCell(const Common::Rect &painted, Common::Array<TracedGlyph> &glyphs,
										Common::Array<Common::Rect> &clear, bool clearPainted, bool keepBackBuffer) {
	if (painted.isEmpty())
		return;
	if (clearPainted)
		clear.push_back(painted);
	for (uint i = 0; i < glyphs.size();) {
		if (!glyphs[i].cell.intersects(painted) || (keepBackBuffer && glyphs[i].inBackBuffer)) {
			++i;
			continue;
		}
		if (!clearPainted || !painted.contains(glyphs[i].area))
			clear.push_back(glyphs[i].area);
		glyphs.remove_at(i);
	}
}

Graphics::GlyphStyle ScummHiResText::glyphStyle(const Graphics::HiResTextConfig &config,
												 int gameShadow, bool korPatchShadow,
												 byte color, byte shadowColor) {
	Graphics::GlyphStyle style;
	style.color = color;
	style.shadowColor = config.shadowColorSet ? config.shadowColor : shadowColor;
	style.shadowMode = resolveShadow(config.shadowMode, gameShadow, korPatchShadow);
	Graphics::HiResGlyphRenderer::applyMap(style, config, MAX(1, config.scale));
	return style;
}

// ---------------------------------------------------------------------
// mirror (C27) / flipped charsets
// ---------------------------------------------------------------------

Graphics::HiResMirror ScummHiResText::gameMirror(const Common::String &gameId, int version,
												 int charsetId) {
	static const struct {
		const char *gameId;
		int minVersion, maxVersion;
		int charsetId;
		Graphics::HiResMirror mode;
	} kTable[] = {
		{ "monkey", 4, 5, 3, Graphics::kHiResMirrorBoth },
		{ "monkey2", 5, 5, 3, Graphics::kHiResMirrorBoth },
		{ "loom", 4, 4, 3, Graphics::kHiResMirrorBoth },
	};
	for (uint i = 0; i < ARRAYSIZE(kTable); ++i) {
		if (gameId.equalsIgnoreCase(kTable[i].gameId) && charsetId == kTable[i].charsetId &&
			version >= kTable[i].minVersion && version <= kTable[i].maxVersion)
			return kTable[i].mode;
	}
	return Graphics::kHiResMirrorNone;
}

void ScummHiResText::setGameMirror(const Common::String &gameId, int version) {
	for (int cs = 0; cs < kMaxFonts; ++cs)
		_gameMirror[cs] = gameMirror(gameId, version, cs);
}

Graphics::HiResMirror ScummHiResText::resolveMirror(bool mirrorSet, Graphics::HiResMirror mirror,
													Graphics::HiResMirror game) {
	if (!mirrorSet)
		return game;
	if (mirror == Graphics::kHiResMirrorGame)
		return game != Graphics::kHiResMirrorNone ? game : Graphics::kHiResMirrorHorizontal;
	return mirror;
}

bool ScummHiResText::keepsGameFont(bool named, Graphics::HiResMirror game, bool utf8, int chr) {
	if (game == Graphics::kHiResMirrorNone)
		return false;
	if (named)
		return false;
	return !utf8 || (chr >= 0 && chr < 0x80);
}

Graphics::HiResMirror ScummHiResText::mirrorFor(int charsetId) const {
	const Graphics::HiResMirror game = (charsetId >= 0 && charsetId < kMaxFonts)
		? _gameMirror[charsetId] : Graphics::kHiResMirrorNone;
	if (charsetId < 0 || charsetId >= kMaxFonts || !_perGlyph)
		return game;
	return resolveMirror(_plans[charsetId].mirrorSet, _plans[charsetId].mirror, game);
}

bool ScummHiResText::idNamesOwnFont(int charsetId) const {
	if (charsetId < 0 || charsetId >= kMaxFonts)
		return false;
	const Graphics::HiResIdPlan &p = _plans[charsetId];
	return p.mirrorSet || p.original || !p.idChain.faces.empty() || p.idChain.endsInOriginal;
}

bool ScummHiResText::keepsGameFont(int charsetId, int chr) const {
	if (charsetId < 0 || charsetId >= kMaxFonts || _gameMirror[charsetId] == Graphics::kHiResMirrorNone)
		return false;
	const bool named = _perGlyph ? idNamesOwnFont(charsetId) : (_simpleCjkFaces[charsetId] != nullptr);
	return keepsGameFont(named, _gameMirror[charsetId], _encoding == Common::kUtf8, chr);
}

// ---------------------------------------------------------------------
// coverage sampling / CJK table probe (unchanged)
// ---------------------------------------------------------------------

bool ScummHiResText::cjkTablesPresent(Common::CodePage page) {
	const char *bytes = "\xb0\xa1";
	uint32 want = 0xAC00;
	switch (page) {
	case Common::kWindows932:
		bytes = "\x82\xa0";
		want = 0x3042;
		break;
	case Common::kWindows936:
		want = 0x554A;
		break;
	case Common::kWindows950:
		bytes = "\xa4\x40";
		want = 0x4E00;
		break;
	case Common::kJohab:
		bytes = "\x88\x61";
		break;
	default:
		break;
	}
	const Common::U32String probe(bytes, page == Common::kCodePageInvalid ? Common::kWindows949 : page);
	return probe.size() == 1 && probe[0] == want;
}

void ScummHiResText::noteTranslatedString(const byte *s, uint32 maxLen) {
	if (!s)
		return;
	const byte *p = s;
	const byte *end = s + maxLen;
	while (p < end && *p) {
		if (*p == 0xFF || *p == 0xFE) {
			if (end - p < 2)
				break;
			const byte code = p[1];
			p += 2;
			p += MIN<ptrdiff_t>(escapeArgBytes(code), end - p);
			continue;
		}
		if (*p == '@') {
			++p;
			continue;
		}
		const uint32 cp = decodeNext(p, end);
		if (cp && cp != 0xFFFD)
			_translationCps.add(cp);
	}
}

// ---------------------------------------------------------------------
// loadConfig
// ---------------------------------------------------------------------

void ScummHiResText::loadConfig(const Common::Path &gameDir, const Common::String &gameId,
								int version, Common::Language language) {
	reset();
	_gameDir = gameDir;

	Common::Array<Common::String> iniWarnings;
	_ini = Graphics::readHiResIniFromConfMan(ConfMan.getActiveDomainName(), iniWarnings);
	for (uint i = 0; i < iniWarnings.size(); ++i)
		warning("SCUMM: %s", iniWarnings[i].c_str());

	Common::Array<Common::String> qualifiers;
	if (!gameId.empty())
		qualifiers.push_back(gameId);
	qualifiers.push_back(Common::String::format("v%d", version));

	// The render target is resolved whatever hires_text says: design
	// section 11 does not gate render_target on it, so a player's
	// render_target=clut8 (or the DOS backend's own cap) still governs the
	// screen a game with no replacement text at all runs in. Phase 1
	// (design 7.1.1) loads only the engine-qualified sections, quietly: its
	// warnings are dropped and, when hi-res text is actually on, repeated
	// for real by phase 2 below.
	Common::Path mapPath;
	bool haveMapPath = false;
	if (_ini.enabled) {
		if (_ini.mapSet) {
			if (!_ini.map.empty()) {
				mapPath = Graphics::HiResFontMap::resolvePath(_ini.map, gameDir);
				haveMapPath = true;
			}
		} else {
			const Common::Path candidate = findDefaultMapFile(gameDir);
			if (!candidate.empty()) {
				mapPath = candidate;
				haveMapPath = true;
			}
		}
	}

	Graphics::HiResMap p1;
	bool p1Loaded = false;
	if (haveMapPath) {
		_mapDir = mapPath.getParent();
		Common::FSNode probe(mapPath);
		if (!probe.exists()) {
			warning("SCUMM: hi-res text map not found: '%s'", mapPath.toString().c_str());
		} else {
			Graphics::HiResMapLoadOptions p1opts;
			p1opts.target = Graphics::kHiResTargetAuto;
			p1opts.quiet = true;
			const bool ok1 = Graphics::HiResFontMap::loadMapFile(mapPath, qualifiers, Graphics::kHiResKeysScumm, p1, p1opts);
			if (!ok1) {
				for (uint i = 0; i < p1.warnings.size(); ++i)
					warning("SCUMM: %s", p1.warnings[i].c_str());
			} else {
				p1Loaded = true;
			}
		}
	}

	const bool anyCoverageP1 = Graphics::mapHasCoverage(p1, p1Loaded, _ini, _mapDir, gameDir,
														 &ScummHiResText::faceHasCoverage, nullptr);

	Common::String targetWarning;
	const Graphics::HiResRenderTarget want = wantedTarget(p1, p1Loaded, _ini, version, anyCoverageP1, targetWarning);
	if (!targetWarning.empty())
		warning("%s", targetWarning.c_str());

	Common::List<Graphics::PixelFormat> supported;
	if (g_system)
		supported = g_system->getSupportedFormats();
	_target = Graphics::predictedTarget(want, supported, true);

	if (!_ini.enabled) {
		debug(1, "SCUMM: hi-res text off (hires_text=false): the map, the fonts "
				 "in the game folder and any TrueType face are all ignored");
		return;
	}

	setGameMirror(gameId, version);

	_haveMap = false;
	_haveMapPath = p1Loaded;
	if (p1Loaded) {
		_mapPath = mapPath;
		_qualifiers = qualifiers;
		Graphics::HiResMapLoadOptions p2opts;
		p2opts.target = _target;
		p2opts.quiet = false;
		_haveMap = Graphics::HiResFontMap::loadMapFile(mapPath, qualifiers, Graphics::kHiResKeysScumm, _map, p2opts);
		debug(1, "SCUMM: hi-res map %s: '%s'%s", _haveMap ? "read" : "REJECTED",
			  mapPath.toString().c_str(), _ini.mapSet ? " (from the config)" : " (found in the game folder)");
	}

	_logText = _ini.log;

	_simpleFonts = false;
	if (!_haveMap)
		_simpleFonts = probeSimpleFonts(gameDir, language);

	_encoding = _map.encodingSet ? _map.encoding : defaultEncodingFor(language);

	_perGlyph = !_simpleFonts;
	// Always compiled (not only when _perGlyph): compilePlans() also
	// resolves _scale (ini > map > 2) and blend()/anyCoverage() (against the
	// resolved-target phase-2 map), which the map-less simple form still
	// needs a starting scale from before resolveScale() (called externally,
	// once the game's own font height is known) can override it. A
	// compiled-but-unused plan for the simple form costs nothing but the
	// call.
	compilePlans(gameDir);
	_scaleFromUser = _ini.scaleSet;

	_wantsAlpha = wantsAlphaFor(_resolvedBlend, _anyCoverage, canBlendText(version));

	const bool iniNamesFace = _ini.faceSet && !_ini.face.equalsIgnoreCase("original");
	_enabled = _haveMap || _simpleFonts || iniNamesFace;

	if (_enabled) {
		debug(1, "SCUMM: hi-res text enabled: scale %d, blend %s (wants alpha %s), source encoding %s%s, render target %s",
			  _scale,
			  _resolvedBlend == Graphics::kHiResBlendOn ? "on" : _resolvedBlend == Graphics::kHiResBlendOff ? "off" : "auto",
			  _wantsAlpha ? "yes" : "no",
			  codePageName(_encoding),
			  _perGlyph ? ", per-glyph placement" : "",
			  Graphics::renderTargetName(_target));
	}
}

uint32 ScummHiResText::decodeNext(const byte *&p, const byte *end) const {
	if (!p || p >= end)
		return 0;

	const Common::CodePage page = _encoding;

	if (page == Common::kCodePageInvalid)
		return *p++;

	uint32 cp = 0;
	byte flags = 0;
	if (page == Common::kUtf8) {
		const Graphics::Utf8TextDecoder utf8;
		p += utf8.decode(p, end, cp, flags);
		return cp;
	}

	const int len = Graphics::CodePageTextDecoder::charLength(page, p, end);
	if (end - p < len) {
		if (*p < 0x80)
			return *p++;
		const Common::U32String lone(Common::String((const char *)p, 1), page);
		p++;
		return lone.empty() ? 0 : (uint32)lone[0];
	}

	const Graphics::CodePageTextDecoder dec(page);
	p += dec.decode(p, end, cp, flags);
	return cp;
}

} // End of namespace Scumm
