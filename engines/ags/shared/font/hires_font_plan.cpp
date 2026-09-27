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

#include "ags/shared/font/hires_font_config.h"

namespace AGS3 {

HiResFontConfig::HiResFontConfig() {
	clear();
}

void HiResFontConfig::clear() {
	_loaded = false;
	_active = false;
	_mapLoaded = false;
	_map.clear();
	_mapDir.clear();
	_iniChain.clear();
	_iniSize = 0;
	_iniScale = 0;
	_sample.clear();
}

void HiResFontConfig::configure(const Graphics::HiResTextConfig *map, const Common::Path &mapDir,
								const Common::Array<Common::Path> &iniChain, int iniSize, int iniScale) {
	clear();
	_loaded = true;
	_mapLoaded = map != nullptr;
	if (map)
		_map = *map;
	_mapDir = mapDir;
	_iniChain = iniChain;
	_iniSize = iniSize;
	_iniScale = iniScale;
	updateActive();
}

Common::Path HiResFontConfig::defaultFace() const {
	// [fonts] default=: the face when no face is named (HIRES_TEXT_MAP.md)
	if (!_mapLoaded)
		return Common::Path();
	Graphics::HiResTextConfig::FaceTable::const_iterator it = _map.fontFaces.find("default");
	if (it == _map.fontFaces.end() || it->_value.empty())
		return Common::Path();
	return Graphics::HiResFontMap::resolvePath(it->_value, _mapDir);
}

void HiResFontConfig::updateActive() {
	_active = !_iniChain.empty() ||
		(_mapLoaded && (!_map.fontIds.empty() || (_map.hiresFaceSet && !_map.hiresFaceChain.empty()) ||
						!defaultFace().empty()));
}

HiResFontPlan HiResFontConfig::plan(int fontNumber) const {
	HiResFontPlan p;
	if (!_active)
		return p;
	const Graphics::HiResFontIdSettings *f = _mapLoaded ? _map.fontIdSettings(fontNumber) : nullptr;
	if (f && f->bitmapSet && !f->bitmap.empty()) {
		p.kind = HiResFontPlan::kBitmap;
		p.bitmap = f->bitmap;
		p.source = Common::String::format("[font.%d] bitmap", fontNumber);
	} else if (f && f->faceSet && !f->faceChain.empty()) {
		p.kind = HiResFontPlan::kFaces;
		p.faces = f->faceChain;
		p.source = Common::String::format("[font.%d] face", fontNumber);
	} else if (!_iniChain.empty()) {
		p.kind = HiResFontPlan::kFaces;
		p.faces = _iniChain;
		p.source = "hires_text_font";
	} else if (_mapLoaded && _map.hiresFaceSet && !_map.hiresFaceChain.empty()) {
		p.kind = HiResFontPlan::kFaces;
		p.faces = _map.hiresFaceChain;
		p.source = "[hires] face";
	} else if (!defaultFace().empty()) {
		p.kind = HiResFontPlan::kFaces;
		p.faces.push_back(defaultFace());
		p.source = "[fonts] default";
	} else {
		return p;
	}

	if (f && f->sizeSet)
		p.size = f->size;
	else if (_iniSize > 0)
		p.size = _iniSize;
	else if (_mapLoaded && _map.hiresSizeSet)
		p.size = _map.hiresSize;
	if (_mapLoaded)
		p.gamma = _map.coverageGamma;
	// A pixel font (C28) is held on its grid in the size above.
	if (f && f->pixelSet)
		p.pixel = f->pixel;
	else if (_mapLoaded && _map.hiresPixelSet)
		p.pixel = _map.hiresPixel;
	return p;
}

bool HiResFontConfig::alpha() const {
	return (_mapLoaded && _map.alphaFromMap) ? _map.alpha : true;
}

// The shared reader's range for [hires] scale= (graphics/hires_text/font_map.cpp)
static const int kMaxScale = 3;

int HiResFontConfig::requestedScale() const {
	if (_iniScale >= 1 && _iniScale <= kMaxScale)
		return _iniScale;
	if (_mapLoaded && _map.scaleFromMap && _map.scale >= 1 && _map.scale <= kMaxScale)
		return _map.scale;
	return 1;
}

bool HiResFontConfig::parseScale(const Common::String &value, int &scale) {
	if (value.size() != 1 || value[0] < '1' || value[0] > '0' + kMaxScale)
		return false;
	scale = value[0] - '0';
	return true;
}

int HiResFontConfig::gateScale(int requested, bool fontsNamed, int gameColorDepth, bool has32BitFormat,
							   Common::String &why) {
	if (requested <= 1)
		return 1;
	if (!fontsNamed)
		why = Common::String::format("hires text scale %d needs a mapped font (hires_text.map or hires_text_font); using 1", requested);
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
