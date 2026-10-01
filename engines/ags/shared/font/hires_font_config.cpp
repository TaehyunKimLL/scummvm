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
#include "ags/shared/font/hires_font_config.h"
#include "ags/shared/debugging/out.h"

namespace AGS3 {

using namespace AGS::Shared;

void HiResFontConfig::load(int gameColorDepth) {
	if (_loaded && _loadedDepth == gameColorDepth)
		return;
	// The translation's sample is set once, whenever the config is read
	const Common::Array<uint32> sample = _sample;

	const Common::String domain = ConfMan.getActiveDomainName();
	const Common::Path gameDir = ConfMan.getPath("path", domain);

	Common::Array<Common::String> iniWarnings;
	const Graphics::HiResIniOverrides ini = Graphics::readHiResIniFromConfMan(domain, iniWarnings);
	for (uint i = 0; i < iniWarnings.size(); i++)
		Debug::Printf(kDbgMsg_Warn, "WARNING: %s", iniWarnings[i].c_str());

	// The map: the file hires_text_map names (relative: the game folder),
	// else the game folder's own. An empty hires_text_map= names nothing.
	const Graphics::HiResRenderTarget target = targetForColorDepth(gameColorDepth);
	Common::String mapWarning;
	const Common::Path mapPath = mapPathFor(ini, gameDir, mapWarning);
	if (!mapWarning.empty())
		Debug::Printf(kDbgMsg_Warn, "WARNING: %s", mapWarning.c_str());

	Graphics::HiResMap map;
	bool mapLoaded = false;
	Common::Path mapDir;
	if (!mapPath.empty()) {
		// Qualified by the game id ([font.0:5daysastranger] before [font.0])
		// and by the game's colour depth: AGS does not choose its screen,
		// so the map is read once, for that target.
		Common::Array<Common::String> qualifiers;
		const Common::String gameId = ConfMan.get("gameid", domain);
		if (!gameId.empty())
			qualifiers.push_back(gameId);
		Graphics::HiResMapLoadOptions opts;
		opts.target = target;
		opts.quiet = true;   // printed below, through the engine's log
		mapDir = mapPath.getParent();
		mapLoaded = Graphics::HiResFontMap::loadMapFile(mapPath, qualifiers, Graphics::kHiResKeysAgs, map, opts);
		if (mapLoaded)
			Debug::Printf(kDbgMsg_Info, "HIRESTXT.MAP %s loaded (game '%s', %s), %u font sections",
						  mapPath.toString().c_str(), gameId.c_str(), Graphics::renderTargetName(target),
						  (uint)map.fontIds.size());
	}

	configure(mapPath.empty() ? nullptr : &map, mapLoaded, ini, mapDir, gameDir);
	_loadedDepth = gameColorDepth;
	_target = target;
	_sample = sample;
	for (uint i = 0; i < _warnings.size(); i++)
		Debug::Printf(kDbgMsg_Warn, "WARNING: %s", _warnings[i].c_str());
	if (!ini.enabled)
		Debug::Printf(kDbgMsg_Info, "hires text off (hires_text=false): the game's own fonts draw everything");
}

} // namespace AGS3
