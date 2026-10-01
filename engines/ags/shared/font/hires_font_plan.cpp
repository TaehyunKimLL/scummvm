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


// The parts of HiResFontConfig that need neither ConfMan nor the engine's
// globals, so the unit tests link them (test/engines/ags/hires_font_plan.h).

#include "common/fs.h"
#include "ags/shared/font/hires_font_config.h"
#include "graphics/hires_text/font_value.h"

namespace AGS3 {

Common::Path HiResFontConfig::mapPathFor(const Graphics::HiResIniOverrides &ini, const Common::Path &gameDir,
										 Common::String &warning) {
	if (!ini.enabled)
		return Common::Path();
	if (ini.mapSet) {
		if (ini.map.empty()) {
			warning = "hires_text_map: empty path; no map is used";
			return Common::Path();
		}
		// data:, an absolute path, or one relative to the game folder
		return Graphics::HiResFontMap::resolvePath(ini.map, gameDir);
	}
	return Graphics::findDefaultHiResMap(gameDir);
}

HiResFontConfig::HiResFontConfig() {
	clear();
}

void HiResFontConfig::clear() {
	_loaded = false;
	_loadedDepth = 0;
	_active = false;
	_mapLoaded = false;
	_map.clear();
	_mapDir.clear();
	_gameDir.clear();
	_ini = Graphics::HiResIniOverrides();
	_iniFaceUsed = false;
	_target = Graphics::kHiResTargetAuto;
	_blendRefusalNoticed = false;
	_scale = 1;
	_scaleWarning.clear();
	_warnings.clear();
	_sample.clear();
}

// The engine scope below [font] (design section 8): AGS supplies no range,
// advance or origin rule of its own, and an empty id chain keeps the game's
// font.
static const Graphics::HiResFontScope &agsEngineScope() {
	static const Graphics::HiResFontScope scope;
	return scope;
}

static void addWarning(Common::Array<Common::String> &warnings, const Common::String &text) {
	for (uint i = 0; i < warnings.size(); i++) {
		if (warnings[i] == text)
			return;
	}
	warnings.push_back(text);
}

void HiResFontConfig::configure(const Graphics::HiResMap *map, bool mapLoaded, const Graphics::HiResIniOverrides &ini,
								const Common::Path &mapDir, const Common::Path &gameDir) {
	clear();
	_loaded = true;
	_ini = ini;
	_mapDir = mapDir;
	_gameDir = gameDir;
	if (map) {
		for (uint i = 0; i < map->warnings.size(); i++)
			addWarning(_warnings, map->warnings[i]);
	}
	// hires_text=false: no map, no faces, the game draws as without the layer
	if (!ini.enabled)
		return;
	_mapLoaded = map && mapLoaded;
	if (_mapLoaded)
		_map = *map;

	// Whether hires_text_face is the chain: it names at least one face.
	// Its warnings come with the plans' below.
	if (ini.faceSet && !ini.face.equalsIgnoreCase("same")) {
		Graphics::HiResFontValue value;
		Common::Array<Common::String> unused;
		_iniFaceUsed = Graphics::parseFontValue(ini.face, _map.faces, _mapDir, _gameDir, value, unused);
	}

	// Font N's chain comes from the ini, [font.N] or [font]: the map's
	// own ids and any other id (-1, which no [font.N] names) cover every
	// chain a font can get.
	Common::Array<int> ids;
	ids.push_back(-1);
	if (_mapLoaded) {
		for (Common::HashMap<int, Graphics::HiResFontScope>::const_iterator it = _map.fontIds.begin();
			 it != _map.fontIds.end(); ++it)
			ids.push_back(it->_key);
	}
	for (uint i = 0; i < ids.size(); i++) {
		Common::Array<Common::String> planWarnings;
		const Graphics::HiResIdPlan p = compile(ids[i], planWarnings);
		for (uint w = 0; w < planWarnings.size(); w++)
			addWarning(_warnings, planWarnings[w]);
		if (!p.original && !p.idChain.faces.empty())
			_active = true;
	}

	// ini > map > 1, within the shared limits
	const int wanted = ini.scaleSet ? ini.scale : ((_mapLoaded && _map.scaleSet) ? _map.scale : 1);
	Common::String clampWarning;
	_scale = Graphics::clampScale(wanted, 1, 3, Graphics::hiResScaleLimits(), 1, "AGS", clampWarning);
	if (!clampWarning.empty())
		_scaleWarning = clampWarning + Common::String::format("; using %d", _scale);
}

Graphics::HiResIdPlan HiResFontConfig::compile(int fontNumber, Common::Array<Common::String> &warnings) const {
	return Graphics::compileIdPlan(_map, _mapLoaded, fontNumber, _ini, agsEngineScope(), _mapDir, _gameDir, warnings);
}

HiResFontPlan HiResFontConfig::plan(int fontNumber) const {
	HiResFontPlan p;
	if (!_active)
		return p;
	Common::Array<Common::String> unused;   // configure() kept them
	const Graphics::HiResIdPlan id = compile(fontNumber, unused);
	if (id.original)
		return p;
	for (uint i = 0; i < id.idChain.faces.size(); i++)
		p.faces.push_back(id.idChain.faces[i].path);
	if (p.faces.empty())
		return p;

	p.kind = HiResFontPlan::kFaces;
	const Graphics::HiResFontScope *scope = _mapLoaded ? _map.fontIdScope(fontNumber) : nullptr;
	if (_iniFaceUsed)
		p.source = "hires_text_face";
	else if (scope && scope->faceSet)
		p.source = Common::String::format("[font.%d] face", fontNumber);
	else
		p.source = "[font] face";
	p.size = id.sizeSet ? id.size : 0;
	if (_mapLoaded)
		p.gamma = _map.coverageGamma;
	// A pixel font (the chain's first face) is held on its grid in the size above
	p.pixel = id.pixel;
	return p;
}

HiResFontPlan scaledPlan(const HiResFontPlan &plan, int scale, int smallPixelPpem) {
	HiResFontPlan p = plan;
	p.pixel = (plan.pixel > 0 && smallPixelPpem > 0) ? MAX(1, scale) * smallPixelPpem : 0;
	return p;
}

Graphics::HiResBlend HiResFontConfig::blend() const {
	if (_ini.blendSet)
		return _ini.blend;
	return (_mapLoaded && _map.blendSet) ? _map.blend : Graphics::kHiResBlendAuto;
}

Graphics::HiResRenderTarget HiResFontConfig::targetForColorDepth(int bits) {
	if (bits <= 8)
		return Graphics::kHiResTargetClut8;
	if (bits <= 16)
		return Graphics::kHiResTargetRgb565;
	return Graphics::kHiResTargetRgb888;
}

int HiResFontConfig::gateScale(int requested, bool fontsNamed, int gameColorDepth, bool has32BitFormat,
							   Common::String &why) {
	if (requested <= 1)
		return 1;
	if (!fontsNamed)
		why = Common::String::format("hires text scale %d needs a mapped font (HIRESTXT.MAP or hires_text_face); using 1", requested);
	else if (gameColorDepth <= 8)
		why = Common::String::format("hires text scale %d is not supported for 8-bit games; using 1", requested);
	else if (!has32BitFormat)
		why = Common::String::format("hires text scale %d needs a 32-bit screen format, which the backend lacks; using 1", requested);
	else
		return requested;
	return 1;
}

Graphics::BreakRules HiResFontConfig::breakRules() const {
	Graphics::BreakRules rules;   // hangul=word, kinsoku on, Thai on
	if (_mapLoaded) {
		const Graphics::HiResLayoutSettings &l = _map.layout;
		if (l.hangulSet)
			rules.hangul = l.hangul;
		if (l.kinsokuSet)
			rules.kinsoku = l.kinsoku;
		if (l.thaiSet)
			rules.thaiFallback = l.thai;
	}
	return rules;
}

} // namespace AGS3
