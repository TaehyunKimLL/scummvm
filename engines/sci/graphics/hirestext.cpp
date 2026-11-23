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

#include "sci/graphics/hirestext.h"

namespace Sci {

const char *const SciHiResText::kDefaultMap = "data:hires_text/maps/korean-default.map";

SciHiResText::SciHiResText() : _coverageWarnings(0), _generation(0), _hangulBreakSet(false), _hangulBreak(Graphics::kHangulBreakWord), _active(false), _files(nullptr), _ownFiles(false), _faces(nullptr),
	_target(Graphics::kHiResTargetClut8), _blend(Graphics::kHiResBlendAuto) {
}

SciHiResText::~SciHiResText() {
	for (uint i = 0; i < _retiredSets.size(); ++i)
		delete _retiredSets[i];
	for (Common::HashMap<int, Graphics::HiResFontSet *>::iterator it = _sets.begin(); it != _sets.end(); ++it)
		delete it->_value;
	delete _faces;
	if (_ownFiles)
		delete _files;
}

bool SciHiResText::setup(const Common::Path &gameDir, const Common::String &langFolder, uint32 offeredTargets,
                         Common::Array<Common::String> &notes) {
	Common::Array<Common::String> warnings;
	const Graphics::HiResOptions options = Graphics::readHiResOptions(warnings);
	notes.push_back(warnings);
	Graphics::HiResFiles *files = new Graphics::HiResDiskFiles();
	const bool ok = setup(files, options, gameDir, langFolder, offeredTargets, notes);
	_ownFiles = true;
	return ok;
}

bool SciHiResText::setup(Graphics::HiResFiles *files, const Graphics::HiResOptions &options, const Common::Path &gameDir,
                         const Common::String &langFolder, uint32 offeredTargets, Common::Array<Common::String> &notes) {
	_files = files;
	_ownFiles = false;
	Graphics::HiResMapQualifiers q;
	q.engine = "sci";
	if (!Graphics::pickHiResMap(options, gameDir, langFolder, q, *_files, _pick, notes)) {
		if (!options.enabled || (options.mapSet && options.map.empty()))
			return false;
		// No map of the game's own: the engine's (DESIGN.md section 5.2, the
		// last face of every chain is NEODG16A).
		Graphics::HiResOptions fallback = options;
		fallback.mapSet = true;
		fallback.map = kDefaultMap;
		if (!Graphics::pickHiResMap(fallback, gameDir, langFolder, q, *_files, _pick, notes))
			return false;
		notes.push_back(Common::String("using ") + kDefaultMap);
	}
	_faces = new Graphics::HiResFaceCache(*_files);

	Common::String note;
	_target = Graphics::resolveRenderTarget(options, &_pick.map, anyCoverageFace(notes), offeredTargets, note);
	if (!note.empty())
		notes.push_back(note);
	_blend = Graphics::resolveBlend(options, &_pick.map);

	loadTargetSections(notes);
	_breakRules = _pick.map.layout.apply(Graphics::BreakRules());
	_hangulBreakSet = options.hangulBreakSet;
	_hangulBreak = options.hangulBreak;
	if (_hangulBreakSet)
		_breakRules.hangul = _hangulBreak;
	_active = true;
	return true;
}

void SciHiResText::loadTargetSections(Common::Array<Common::String> &notes) {
	Graphics::HiResMapQualifiers q;
	q.engine = "sci";
	q.target = _target;
	Common::ScopedPtr<Common::SeekableReadStream> s(_files->open(_pick.path));
	Graphics::HiResMap map;
	if (s && Graphics::HiResFontMap::load(*s, _pick.path.baseName(), _pick.path.getParent(), q, map))
		_pick.map = map;
	else
		notes.push_back("the map did not load again with the target's sections");
	for (uint i = 0; i < _pick.map.warnings.size(); ++i)
		notes.push_back(_pick.map.warnings[i]);
}

void SciHiResText::setScreenFormat(const Graphics::PixelFormat &format, Common::Array<Common::String> &notes) {
	if (!_active)
		return;
	const Graphics::HiResRenderTarget got = Graphics::hiResTargetOf(format);
	// The text follows the screen only when it is LESS capable than the text asked for (an RGB target but a CLUT8 screen, or another
	// RGB format). A CLUT8 text stays CLUT8 on an RGB screen (the user's rgb_rendering or palette_mods): an explicit
	// render_target=clut8 wins, a paired map keeps its 1 bpp [font:clut8] chain and an AA-only map is stamped, with the warning.
	if (got == Graphics::kHiResTargetAuto || got == _target || _target == Graphics::kHiResTargetClut8)
		return;
	notes.push_back(Common::String("the backend gave a ") + Graphics::HiResFontMap::targetName(got) + " screen, not " +
	                Graphics::HiResFontMap::targetName(_target) + ": the text follows the screen");
	_target = got;
	// The old sets are kept: GfxFontHiRes objects of the font cache hold references to them. They are deleted with the object;
	// the generation tells the cache that its fonts are stale.
	for (Common::HashMap<int, Graphics::HiResFontSet *>::iterator it = _sets.begin(); it != _sets.end(); ++it)
		if (it->_value)
			_retiredSets.push_back(it->_value);
	_sets.clear();
	++_generation;
	loadTargetSections(notes);
	_breakRules = _pick.map.layout.apply(Graphics::BreakRules());
	if (_hangulBreakSet)
		_breakRules.hangul = _hangulBreak;
}

bool SciHiResText::anyCoverageFace(Common::Array<Common::String> &notes) {
	bool any = false;
	for (Graphics::HiResFaceNames::const_iterator it = _pick.map.faces.begin(); it != _pick.map.faces.end(); ++it) {
		Graphics::SvfFace *face = _faces->get(it->_value, _pick.map.loadModeFor(it->_value), notes);
		any = any || (face && face->getBpp() > 1);
	}
	return any;
}

const Graphics::HiResFontSet *SciHiResText::getFontSet(int fontId) {
	if (!_active)
		return nullptr;
	if (_sets.contains(fontId))
		return _sets[fontId];
	Common::Array<Common::String> warnings;
	Graphics::HiResFontSet *set = new Graphics::HiResFontSet();
	if (!set->build(Graphics::compileIdPlan(_pick.map, fontId, warnings), _pick.map, *_faces, warnings)) {
		delete set;
		set = nullptr;
	}
	// A face with coverage on a CLUT8 screen is stamped at coverage >= 128 (its edges are cut): say so once.
	for (uint c = 0; set && _target == Graphics::kHiResTargetClut8 && !_coverageWarnings && c < set->getChainCount(); ++c)
		for (uint f = 0; f < set->getChain(c)->getFaceCount() && !_coverageWarnings; ++f)
			if (set->getChain(c)->getFace(f)->getBpp() > 1) {
				++_coverageWarnings;
				warning("Hi-res text, font %d: an anti-aliased face on a CLUT8 screen is stamped at coverage >= 128; "
				        "give the map a [font:clut8] section with a 1 bpp face", fontId);
			}
	for (uint i = 0; i < warnings.size(); ++i)
		warning("Hi-res text, font %d: %s", fontId, warnings[i].c_str());
	_sets[fontId] = set;
	return set;
}

GfxFontHiRes::GfxFontHiRes(GfxFont *original, const Graphics::HiResFontSet &set, Graphics::TextPlane &plane) :
	_original(original), _set(set), _plane(plane) {
	_lineHeight = _set.getAscent() + _set.getDescent();
	const int s = SciHiResText::kScale;
	_height = (byte)MAX<int>(_original->getHeight(), (_lineHeight + s - 1) / s);
}

GfxFontHiRes::~GfxFontHiRes() {
	delete _original;
}

// SCI fonts keep symbols (the list arrows of the file dialogs) below U+0020:
// never text, always the game's glyph.
bool GfxFontHiRes::isOriginal(uint32 chr) const {
	return chr < 0x20 || _set.route(chr) < 0;
}

byte GfxFontHiRes::getCharWidth(uint32 chr) {
	if (isOriginal(chr))
		return _original->getCharWidth(chr);
	const int s = SciHiResText::kScale;
	return (byte)MIN<int>(255, (_set.getAdvance(chr) + s - 1) / s);
}

void GfxFontHiRes::draw(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput) {
	if (isOriginal(chr)) {
		_original->draw(chr, top, left, color, greyedOutput);
		return;
	}
	// The faces' line is centred in the SCI line; shift= moves it down.
	const int s = SciHiResText::kScale;
	const int baseline = top * s + (_height * s - _lineHeight) / 2 + _set.getAscent(); // drawChar() adds the shift
	_set.drawChar(chr, left * s, baseline, _plane, color);
}

} // End of namespace Sci
