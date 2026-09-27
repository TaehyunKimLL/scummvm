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


#include "common/config-manager.h"
#include "common/fs.h"
#include "common/stream.h"
#include "ags/shared/font/hires_font_config.h"
#include "ags/shared/debugging/out.h"

namespace AGS3 {

using namespace AGS::Shared;

// The pixel sizes TtfGlyphSource accepts (kMinPixelSize..kMaxPixelSize)
static const int kMinFontSize = 6;
static const int kMaxFontSize = 255;

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
	_sample.clear();
}

void HiResFontConfig::load() {
	if (_loaded)
		return;
	_loaded = true;

	const Common::String domain = ConfMan.getActiveDomainName();
	const Common::Path gameDir = ConfMan.getPath("path", domain);

	// hires_text.map: the file hires_text_map names, else the game
	// directory's own. An empty hires_text_map= names nothing.
	const bool mapKeySet = ConfMan.hasKey("hires_text_map", domain);
	Common::FSNode mapNode;
	if (mapKeySet) {
		const Common::String value = ConfMan.get("hires_text_map", domain);
		if (value.empty())
			Debug::Printf(kDbgMsg_Warn, "WARNING: hires_text_map: empty path; no map is used");
		else
			mapNode = Common::FSNode(Common::Path(value, Common::Path::kNativeSeparator));
	} else if (!gameDir.empty()) {
		mapNode = Common::FSNode(gameDir).getChild("hires_text.map");
	}

	if ((mapKeySet && !mapNode.getPath().empty()) || (!mapKeySet && mapNode.exists())) {
		Common::String error;
		Common::SeekableReadStream *stream = nullptr;
		if (!mapNode.exists())
			error = "does not exist";
		else if (mapNode.isDirectory())
			error = "is a directory";
		else if (!(stream = mapNode.createReadStream()))
			error = "could not open the file";
		if (stream) {
			// Qualified by the game id: [font.0:5daysastranger] before [font.0].
			Common::Array<Common::String> qualifiers;
			const Common::String gameId = ConfMan.get("gameid", domain);
			if (!gameId.empty())
				qualifiers.push_back(gameId);
			_mapDir = mapNode.getParent().getPath();
			_mapLoaded = Graphics::HiResFontMap::loadFromStream(*stream, _mapDir, qualifiers, _map);
			delete stream;
			if (!_mapLoaded) {
				_map.clear();
				error = "is not a valid map";
			} else {
				Debug::Printf(kDbgMsg_Info, "hires_text.map %s loaded (game '%s'), %u font sections",
							  mapNode.getPath().toString().c_str(), gameId.c_str(), (uint)_map.fontIds.size());
			}
		}
		if (!_mapLoaded)
			Debug::Printf(kDbgMsg_Warn, "WARNING: hires_text.map %s: %s; ignoring it",
						  mapNode.getPath().toString().c_str(), error.c_str());
	}

	// hires_text_font: a face (or a comma-separated chain) for every font,
	// names looked up in the map's [fonts]
	if (ConfMan.hasKey("hires_text_font", domain)) {
		const Common::String value = ConfMan.get("hires_text_font", domain);
		const char *p = value.c_str();
		while (true) {
			const char *comma = strchr(p, ',');
			Common::String entry = comma ? Common::String(p, comma - p) : Common::String(p);
			entry.trim();
			if (!entry.empty())
				_iniChain.push_back(Graphics::HiResFontMap::resolvePath(_map.resolveFace(entry), _mapDir));
			if (!comma)
				break;
			p = comma + 1;
		}
		if (_iniChain.empty())
			Debug::Printf(kDbgMsg_Warn, "WARNING: hires_text_font: empty; the game's fonts are used");
	}
	if (ConfMan.hasKey("hires_text_font_size", domain)) {
		const Common::String value = ConfMan.get("hires_text_font_size", domain);
		char *end = nullptr;
		const long size = strtol(value.c_str(), &end, 10);
		if (value.empty() || *end != '\0' || size < kMinFontSize || size > kMaxFontSize)
			Debug::Printf(kDbgMsg_Warn, "WARNING: hires_text_font_size '%s' is not a number from %d to %d; ignoring it",
						  value.c_str(), kMinFontSize, kMaxFontSize);
		else
			_iniSize = (int)size;
	}

	_active = !_iniChain.empty() ||
		(_mapLoaded && (!_map.fontIds.empty() || (_map.hiresFaceSet && !_map.hiresFaceChain.empty())));
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
	} else {
		return p;
	}

	if (f && f->sizeSet)
		p.size = f->size;
	else if (_iniSize > 0)
		p.size = _iniSize;
	else if (_mapLoaded && _map.hiresSizeSet)
		p.size = _map.hiresSize;
	return p;
}

bool HiResFontConfig::alpha() const {
	return (_mapLoaded && _map.alphaFromMap) ? _map.alpha : true;
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
