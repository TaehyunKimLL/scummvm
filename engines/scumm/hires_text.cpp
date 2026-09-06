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

#include "common/config-manager.h"
#include "common/fs.h"
#include "common/textconsole.h"

namespace Scumm {

// A map file with this name in the game folder is picked up with no config key
// at all, so a translation can ship one and need no setup.
static const char *const kDefaultMapName = "hires_text.map";

// The name the Korean translations have been shipping. Still honoured so that
// an existing install keeps working.
static const char *const kLegacyMapName = "korean_ttf.map";

ScummHiResText::ScummHiResText() {
	reset();
}

void ScummHiResText::reset() {
	_enabled = false;
	_config.clear();
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

void ScummHiResText::loadConfig(const Common::Path &gameDir, const Common::String &gameId,
								int version, Common::Language language) {
	reset();

	_config.encoding = defaultEncodingFor(language);

	// Sections may be narrowed by game or by SCUMM version, most specific
	// first. These strings are the engine's business; the parser treats them
	// as opaque.
	Common::Array<Common::String> qualifiers;
	if (!gameId.empty())
		qualifiers.push_back(gameId);
	qualifiers.push_back(Common::String::format("v%d", version));

	// An explicit key wins; otherwise a conventionally named file in the game
	// folder is used if one is there.
	Common::Path mapPath;
	bool explicitMap = false;
	if (ConfMan.hasKey("hires_text_map")) {
		mapPath = Graphics::HiResFontMap::resolvePath(ConfMan.get("hires_text_map"), gameDir);
		explicitMap = true;
	} else if (ConfMan.hasKey("korean_ttf_map")) {
		mapPath = Graphics::HiResFontMap::resolvePath(ConfMan.get("korean_ttf_map"), gameDir);
		explicitMap = true;
	}

	if (!explicitMap) {
		const Common::Path candidates[] = {
			gameDir.appendComponent(kDefaultMapName),
			gameDir.appendComponent(kLegacyMapName)
		};
		for (int i = 0; i < ARRAYSIZE(candidates); ++i) {
			Common::FSNode probe(candidates[i]);
			if (probe.exists()) {
				mapPath = candidates[i];
				break;
			}
		}
	}

	bool haveMap = false;
	if (!mapPath.empty()) {
		Common::FSNode probe(mapPath);
		if (probe.exists())
			haveMap = Graphics::HiResFontMap::load(mapPath, qualifiers, _config);
		else if (explicitMap)
			warning("SCUMM: hi-res text map not found: '%s'", mapPath.toString().c_str());
	}

	// A user setting outranks the map, which is why logical font sizes are
	// only resolved once this is settled.
	if (ConfMan.hasKey("hires_text_scale"))
		_config.scale = ConfMan.getInt("hires_text_scale");
	else if (ConfMan.hasKey("korean_hires_scale"))
		_config.scale = ConfMan.getInt("korean_hires_scale");

	if (_config.scale < 1 || _config.scale > 3) {
		warning("SCUMM: hi-res text scale %d is out of range, ignoring", _config.scale);
		_config.scale = 1;
	}

	if (ConfMan.hasKey("hires_text_alpha"))
		_config.alpha = ConfMan.getBool("hires_text_alpha");
	else if (ConfMan.hasKey("korean_alpha_text"))
		_config.alpha = ConfMan.getBool("korean_alpha_text");

	// Being asked for is not the same as being usable: without a map there is
	// nothing naming the fonts, so the engine stays on its original path.
	_enabled = haveMap && (_config.scale > 1 || !_config.bitmapPattern.empty() ||
						   !_config.bitmapSingle.empty());

	if (_enabled)
		debug(1, "SCUMM: hi-res text enabled, scale %d, alpha %d", _config.scale, _config.alpha);
}

int ScummHiResText::resolvedFontSize(int role) const {
	if (role < 0 || role >= Graphics::kHiResRoleCount)
		return 0;

	const int size = _config.ttfSize[role];
	if (size <= 0)
		return 0;

	// A logical size follows whatever scale ended up in force, so one map
	// looks the same at 2x and at 3x.
	return _config.ttfSizeRelative[role] ? size * _config.scale : size;
}

int ScummHiResText::supersample(int role) const {
	if (role < 0 || role >= Graphics::kHiResRoleCount)
		return 1;
	return _config.ttfSupersample[role];
}

} // End of namespace Scumm
