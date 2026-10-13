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

#include "agi/hirestext.h"

#include "common/debug.h"
#include "common/stream.h"
#include "common/textconsole.h"
#include "graphics/hires_text/font_face.h"
#include "graphics/hires_text/glyph_source_file.h"
#include "graphics/hires_text/glyph_source_ranged.h"
#include "graphics/hires_text/map_pick.h"
#include "graphics/hires_text/text_compose.h"
#include "graphics/hires_text/text_layout.h"
#ifdef USE_FREETYPE2
#include "graphics/hires_text/glyph_source_ttf.h"
#endif

namespace Agi {

// Phase 1: AGI's 640x400 mode, cell 16x16 (design 4.5). Most specific first.
static Common::Array<Common::String> agiQualifiers() {
	Common::Array<Common::String> q;
	q.push_back("agi");
	q.push_back("scale2x2");
	q.push_back("scale2");
	return q;
}

HiresText::HiresText() : _source(nullptr) {
}

HiresText::~HiresText() {
	clear();
}

void HiresText::clear() {
	delete _source;
	_source = nullptr;
}

bool HiresText::hangulAny() const {
	return isActive() && _map.layout.hangulSet && _map.layout.hangul == Graphics::kHangulBreakAny;
}

bool HiresText::load(const Common::String &domain, const Common::Path &gameDir) {
	Common::Array<Common::String> warnings;
	const Graphics::HiResIniOverrides ini = Graphics::readHiResIniFromConfMan(domain, warnings);
	for (uint i = 0; i < warnings.size(); i++)
		warning("%s", warnings[i].c_str());
	if (!ini.enabled)
		return false;
	Common::Path mapPath;
	if (Graphics::hiResFontPicksMap(ini)) {
		Graphics::HiResFontKind kind;
		Common::Array<Common::String> notes;
		if (!Graphics::pickHiResMap(ini, gameDir, agiQualifiers(), Graphics::kHiResKeysAgi, mapPath, kind, notes))
			mapPath = Graphics::findDefaultHiResMap(gameDir);
		for (uint i = 0; i < notes.size(); i++)
			debug(1, "AGI hi-res text: %s", notes[i].c_str());
	} else if (!ini.map.empty()) {
		mapPath = Graphics::findGameMapFile(gameDir, ini.map);
		if (mapPath.empty())
			warning("AGI hi-res text: hires_text_map=%s not found", ini.map.c_str());
	}
	if (mapPath.empty())
		return false;
	return loadMap(mapPath, gameDir, ini);
}

Graphics::UnicodeGlyphSource *HiresText::openFace(const Graphics::HiResFaceEntry &e) {
	if (e.kind != Graphics::kHiResFaceFile)
		return nullptr;
	int32 faceIndex = 0;
	Common::String error;
	Common::SeekableReadStream *s = Graphics::openFontFace(e.path, faceIndex, error);
	if (!s) {
		warning("AGI hi-res text: %s: %s", e.written.c_str(), error.c_str());
		return nullptr;
	}
	byte head[4] = { 0, 0, 0, 0 };
	s->read(head, 4);
	s->seek(0);
	if (Graphics::isSvfnFile(head, 4)) {
		Graphics::UnicodeGlyphSource *src = Graphics::createSvfnSource(s, DisposeAfterUse::YES, error, e.path.toString());
		if (!src)
			warning("AGI hi-res text: %s: %s", e.written.c_str(), error.c_str());
		return src;
	}
#ifdef USE_FREETYPE2
	Graphics::UnicodeGlyphSource *ttf = Graphics::TtfGlyphSource::create(s, DisposeAfterUse::YES,
		_plan.sizeSet ? _plan.size : 16, error, false, false, nullptr, 0, faceIndex);
	if (!ttf)
		warning("AGI hi-res text: %s: %s", e.written.c_str(), error.c_str());
	return ttf;
#else
	delete s;
	warning("AGI hi-res text: %s is a TrueType face; this build has no FreeType", e.written.c_str());
	return nullptr;
#endif
}

bool HiresText::loadMap(const Common::Path &mapPath, const Common::Path &gameDir, const Graphics::HiResIniOverrides &ini) {
	clear();
	_ini = ini;
	if (!Graphics::HiResFontMap::loadMapFile(mapPath, agiQualifiers(), Graphics::kHiResKeysAgi, _map))
		return false;
	if (!_map.fontIds.empty())
		warning("AGI hi-res text: AGI has one font; the [font.N] sections of %s are ignored", mapPath.toString().c_str());
	Common::Array<Common::String> warnings;
	_plan = Graphics::compileIdPlan(_map, true, 0, _ini, Graphics::HiResFontScope(), mapPath.getParent(), gameDir, warnings);
	for (uint i = 0; i < warnings.size(); i++)
		warning("%s", warnings[i].c_str());

	Common::Array<Common::Array<Graphics::UnicodeGlyphSource *> > chains;
	bool any = false;
	chains.resize(1 + _plan.ruleChains.size());
	for (uint c = 0; c < chains.size(); c++) {
		const Graphics::HiResFaceChain &chain = c == 0 ? _plan.idChain : _plan.ruleChains[c - 1];
		for (uint f = 0; f < chain.faces.size(); f++) {
			Graphics::UnicodeGlyphSource *src = openFace(chain.faces[f]);
			any = any || src;
			chains[c].push_back(src);
		}
	}
	Common::Array<Graphics::UnicodeGlyphSource *> targets;
	for (uint t = 0; t < _plan.targets.size(); t++) {
		Graphics::UnicodeGlyphSource *src = openFace(_plan.targets[t].face);
		any = any || src;
		targets.push_back(src);
	}
	if (!any) {
		warning("AGI hi-res text: no face of %s could be opened", mapPath.toString().c_str());
		for (uint c = 0; c < chains.size(); c++)
			for (uint f = 0; f < chains[c].size(); f++)
				delete chains[c][f];
		for (uint t = 0; t < targets.size(); t++)
			delete targets[t];
		return false;
	}
	_source = new Graphics::RangeRoutedGlyphSource(_plan, chains, targets, DisposeAfterUse::YES);
	return true;
}

bool HiresText::glyph(uint32 cp, uint16 rows[16]) {
	if (!_source)
		return false;
	const Graphics::HiResPick pick = _source->pick(cp);
	if (pick.kind != Graphics::HiResPick::kFace)
		return false;
	const int cells = _source->cells(cp);
	if (cells <= 0)
		return false;
	const int w = MIN<int>(cells * _source->cellWidth(), 16);
	const int h = MIN<int>(_source->cellHeight(), 16);
	const int bpp = _source->bitsPerPixel();
	const int x0 = (16 - w) / 2;
	const int y0 = CLIP<int>((16 - h) / 2 + _plan.shift, 0, 16 - h);
	memset(rows, 0, 16 * sizeof(uint16));
	for (int y = 0; y < h; y++) {
		const byte *row = _source->row(cp, y);
		if (!row)
			continue;
		for (int x = 0; x < w; x++) {
			// D1: one bit per pixel; 2 bpp and 8 bpp coverage cut at half.
			if (Graphics::TextCompose::expandCoverage(row, x, bpp) >= 128)
				rows[y0 + y] |= (uint16)(0x8000 >> (x0 + x));
		}
	}
	return true;
}

bool HiresText::canDraw(uint32 cp) {
	if (!_source)
		return cp < 0x80;
	const Graphics::HiResPick pick = _source->pick(cp);
	if (pick.kind == Graphics::HiResPick::kFace)
		return !pick.missingBox;
	return cp < 0x80;
}

} // End of namespace Agi
