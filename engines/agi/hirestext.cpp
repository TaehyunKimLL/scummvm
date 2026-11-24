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


#include "common/ptr.h"
#include "common/stream.h"
#include "common/textconsole.h"

#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/id_plan.h"
#include "graphics/hires_text/svfchain.h"
#include "graphics/hires_text/svfface.h"

#include "agi/hirestext.h"

namespace Agi {

const char *const AgiHiResText::kDefaultMap = "data:hires_text/maps/korean-default.map";

AgiHiResText::AgiHiResText() : _files(nullptr), _ownFiles(false), _faces(nullptr), _set(nullptr), _hangulAny(false),
	_coverageWarnings(0), _log(false) {
}

AgiHiResText::~AgiHiResText() {
	clear();
}

void AgiHiResText::clear() {
	delete _set;
	_set = nullptr;
	delete _faces;
	_faces = nullptr;
	if (_ownFiles)
		delete _files;
	_files = nullptr;
	_ownFiles = false;
	_cell.free();
}

bool AgiHiResText::setup(const Common::Path &gameDir, const Common::String &langFolder, Common::Array<Common::String> &notes) {
	Common::Array<Common::String> warnings;
	const Graphics::HiResOptions options = Graphics::readHiResOptions(warnings);
	notes.push_back(warnings);
	Graphics::HiResFiles *files = new Graphics::HiResDiskFiles();
	const bool ok = setup(files, options, gameDir, langFolder, notes);
	_ownFiles = true; // setup() kept @p files in _files, whatever it returned
	return ok;
}

bool AgiHiResText::setup(Graphics::HiResFiles *files, const Graphics::HiResOptions &options, const Common::Path &gameDir,
                         const Common::String &langFolder, Common::Array<Common::String> &notes) {
	clear();
	_files = files;
	Graphics::HiResMapQualifiers q;
	q.engine = "agi";
	q.target = Graphics::kHiResTargetClut8; // AGI's display is CLUT8 (EGA indices): [font:clut8] sections apply
	if (!Graphics::pickHiResMap(options, gameDir, langFolder, q, *_files, _pick, notes)) {
		if (!options.enabled || (options.mapSet && options.map.empty()))
			return false;
		Graphics::HiResOptions fallback = options;
		fallback.mapSet = true;
		fallback.map = kDefaultMap;
		if (!Graphics::pickHiResMap(fallback, gameDir, langFolder, q, *_files, _pick, notes))
			return false;
		notes.push_back(Common::String("using ") + kDefaultMap);
	}
	for (uint i = 0; i < _pick.map.warnings.size(); ++i)
		notes.push_back(_pick.map.warnings[i]);
	if (!_pick.map.fontIds.empty())
		notes.push_back("AGI has one font: the map's [font.N] sections are not used");

	_faces = new Graphics::HiResFaceCache(*_files);
	Common::Array<Common::String> warnings;
	Graphics::HiResFontSet *set = new Graphics::HiResFontSet();
	if (!set->build(Graphics::compileIdPlan(_pick.map, 0, warnings), _pick.map, *_faces, warnings) || !set->getChainCount()) {
		delete set;
		set = nullptr;
	}
	notes.push_back(warnings);
	if (!set) {
		notes.push_back("no face of the map could be opened");
		return false;
	}
	for (uint c = 0; c < set->getChainCount() && !_coverageWarnings; ++c)
		for (uint f = 0; f < set->getChain(c)->getFaceCount() && !_coverageWarnings; ++f)
			if (set->getChain(c)->getFace(f)->getBpp() > 1) {
				++_coverageWarnings;
				warning("Hi-res text: an anti-aliased face on AGI's CLUT8 screen is cut at coverage 128; "
				        "give the map a [font:clut8] section with a 1 bpp face");
			}
	if (!_cell.create(kCell, kCell, 1)) {
		delete set;
		return false;
	}
	_set = set;
	_log = options.log;
	_hangulAny = _pick.map.layout.hangulSet && _pick.map.layout.hangul == Graphics::kHangulBreakAny;
	if (options.hangulBreakSet)
		_hangulAny = options.hangulBreak == Graphics::kHangulBreakAny;
	return true;
}

bool AgiHiResText::glyph(uint32 cp, uint16 rows[16]) {
	if (!_set || cp < 0x80 || _set->route(cp) < 0)
		return false;
	_cell.clear();
	const int advance = CLIP<int>(_set->getAdvance(cp), 0, kCell);
	const int x = (kCell - advance) / 2;
	const int line = _set->getAscent() + _set->getDescent();
	const int baseline = (kCell - line) / 2 + _set->getAscent(); // drawChar() adds shift=
	const Common::Rect clip(0, 0, kCell, kCell);
	_set->drawChar(cp, x, baseline, _cell, 1, &clip);
	// hires_text_log: each character no face has (drawn as the missing box) once, for the harness's coverage diff
	if (_log && !_set->getChain(_set->route(cp))->has(cp) && !_missLogged.contains(cp)) {
		_missLogged[cp] = true;
		warning("Hi-res text: no face has U+%04X", cp);
	}
	for (int y = 0; y < kCell; ++y) {
		uint16 bits = 0;
		for (int px = 0; px < kCell; ++px)
			if (_cell.getCoverage(px, y) >= 128)
				bits |= (uint16)(0x8000 >> px);
		rows[y] = bits;
	}
	return true;
}

} // End of namespace Agi
