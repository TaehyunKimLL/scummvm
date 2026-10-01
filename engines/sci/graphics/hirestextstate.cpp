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

#include "sci/graphics/hirestextstate.h"

#include "common/config-manager.h"
#include "common/debug.h"
#include "common/fs.h"
#include "common/platform.h"
#include "common/stream.h"
#include "common/textconsole.h"
#include "graphics/hires_text/font_face.h"
#include "graphics/hires_text/glyph_source_file.h"
#include "graphics/hires_text/id_plan.h"
#include "graphics/pixelformat.h"
#include "sci/sci.h"

namespace Sci {

namespace {

// design section 4's default map file; the same fixed 8.3 name every engine
// looks for.
const char *const kHiResMapName = "HIRESTXT.MAP";

} // End of anonymous namespace

Common::Path findDefaultHiresMap(const Common::Path &gameDir) {
	const Common::Path direct = gameDir.appendComponent(kHiResMapName);
	if (Common::FSNode(direct).exists())
		return direct;

	Common::FSNode dir(gameDir);
	if (!dir.isDirectory())
		return Common::Path();
	Common::FSList children;
	if (dir.getChildren(children, Common::FSNode::kListFilesOnly)) {
		for (uint i = 0; i < children.size(); ++i) {
			if (children[i].getName().equalsIgnoreCase(kHiResMapName))
				return children[i].getPath();
		}
	}
	return Common::Path();
}

HiresTextState::HiresTextState()
	: _active(false), _haveMapPath(false), _mapRefused(false), _phase1Loaded(false),
	  _driverTarget(Graphics::kHiResTargetAuto), _noted(false) {
	_choice.target = Graphics::kHiResTargetAuto;
	_choice.blend = Graphics::kHiResBlendAuto;
	_choice.anyCoverage = false;
}

bool HiresTextState::faceHasCoverage(const Common::Path &face, void *) {
	int32 faceIndex = 0;
	Common::String error;
	Common::SeekableReadStream *stream = Graphics::openFontFace(face, faceIndex, error);
	if (!stream)
		return false;
	byte head[9];
	bool coverage = true;
	if (stream->read(head, sizeof(head)) == sizeof(head) && Graphics::isSvfnFile(head, 4))
		coverage = (head[8] == 8 || head[8] == 2);
	delete stream;
	return coverage;
}

void HiresTextState::load(bool applies) {
	// The game's own domain only, except for the two keys readHiResIni()
	// itself lets fall back to [scummvm] (render_target, hires_text).
	const Common::String &domain = ConfMan.getActiveDomainName();
	_gameDir = ConfMan.getPath("path", domain);

	Common::Array<Common::String> iniWarnings;
	_ini = Graphics::readHiResIniFromConfMan(domain, iniWarnings);
	for (uint i = 0; i < iniWarnings.size(); i++)
		warning("%s", iniWarnings[i].c_str());

	_active = applies && _ini.enabled;
	if (_active) {
		// The map (design section 4): the file hires_text_map names, else
		// the game folder's own HIRESTXT.MAP when it exists. A relative
		// hires_text_map is the game folder's, not the current directory.
		if (_ini.mapSet) {
			if (_ini.map.empty()) {
				warning("hires_text_map: empty path; no map is used");
			} else {
				_mapPath = Graphics::HiResFontMap::resolvePath(_ini.map, _gameDir);
				_haveMapPath = true;
			}
		} else {
			_mapPath = findDefaultHiresMap(_gameDir);
			_haveMapPath = !_mapPath.empty();
		}

		if (_haveMapPath) {
			_mapDir = _mapPath.getParent();
			Common::Array<Common::String> qualifiers;
			const char *platform = Common::getPlatformCode(g_sci->getPlatform());
			if (platform && *platform)
				qualifiers.push_back(platform);

			// Phase 1: no target sections, quiet. GfxCache's phase-2 load
			// reports every warning of a map that loads; a refused map is
			// refused whatever the target, so it is reported here, once.
			Graphics::HiResMapLoadOptions options;
			options.target = Graphics::kHiResTargetAuto;
			options.quiet = true;
			_phase1Loaded = Graphics::HiResFontMap::loadMapFile(_mapPath, qualifiers, Graphics::kHiResKeysSci,
																 _phase1, options);
			if (!_phase1Loaded) {
				_mapRefused = true;
				for (uint i = 0; i < _phase1.warnings.size(); i++)
					warning("%s", _phase1.warnings[i].c_str());
			}
		}
	}

	const bool anyCoverage = _active &&
		Graphics::mapHasCoverage(_phase1, _phase1Loaded, _ini, _mapDir, _gameDir, &HiresTextState::faceHasCoverage, nullptr);
	_choice = sciPhase1(applies, _ini, _phase1, _phase1Loaded, anyCoverage);
	if (_active)
		debug(1, "SCI: hi-res text phase 1: target %s, blend %s, %s faces with coverage%s",
			  Graphics::renderTargetName(_choice.target),
			  _choice.blend == Graphics::kHiResBlendOff ? "off" : (_choice.blend == Graphics::kHiResBlendOn ? "on" : "auto"),
			  _choice.anyCoverage ? "some" : "no",
			  _haveMapPath ? (_phase1Loaded ? "" : " (map refused)") : " (no map)");
}

void HiresTextState::adoptScreen(const Graphics::PixelFormat &actual) {
	if (_noted)
		return;
	// The explicit target asked for: phase 1's when hi-res text is on,
	// else the bare ini value (it does not apply here, but say so in the log).
	const Graphics::HiResRenderTarget wanted =
		_active ? _choice.target : (_ini.targetSet ? _ini.target : Graphics::kHiResTargetAuto);
	const Common::String note = sciTargetNote(wanted, Graphics::targetOfFormat(actual));
	if (note.empty())
		return;
	_noted = true;
	if (_active)
		warning("SCI: %s", note.c_str());
	else
		debug(1, "SCI: %s (hi-res text is off for this game)", note.c_str());
}

} // End of namespace Sci
