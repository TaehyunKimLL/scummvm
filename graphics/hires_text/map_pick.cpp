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


#include "graphics/hires_text/map_pick.h"
#include "graphics/hires_text/font_face.h"
#include "graphics/hires_text/id_plan.h"

namespace Graphics {

namespace {

struct FaceCheck {
	FontFileExistsFn exists;
	Common::String missing; ///< the first face that is not there
};

// Called for every distinct face of the map; always "no coverage", so that
// mapHasCoverage() walks them all.
bool noteMissingFace(const Common::Path &face, void *ctx) {
	FaceCheck *check = static_cast<FaceCheck *>(ctx);
	if (check->missing.empty() && !check->exists(face))
		check->missing = face.toString('/');
	return false;
}

} // End of anonymous namespace

bool pickHiResMap(const HiResIniOverrides &ini, const Common::Path &gameDir,
				  const Common::Array<Common::String> &qualifiers, const HiResEngineKeys &keys,
				  Common::Path &mapPath, HiResFontKind &kind, Common::Array<Common::String> &notes,
				  FontFileExistsFn exists) {
	const HiResFontKind preferred = ini.fontSet ? ini.font : kHiResFontAuto;
	if (preferred != kHiResFontAuto && !hiResFontKindBuilt(preferred))
		notes.push_back(Common::String::format("hires_text_font=%s: this build cannot draw TrueType faces",
											   hiResFontKindName(preferred)));

	const Common::Array<HiResFontKind> order = hiResFontOrder(preferred);
	for (uint i = 0; i < order.size(); ++i) {
		const HiResFontKind k = order[i];
		const Common::String name = ini.kindMapSet[k - 1] ? ini.kindMap[k - 1] : Common::String(hiResFontKindDefaultMap(k));
		const Common::Path path = findGameMapFile(gameDir, name);
		if (path.empty()) {
			notes.push_back(Common::String::format("%s map %s: not in the game folder", hiResFontKindName(k), name.c_str()));
			continue;
		}

		HiResMap map;
		HiResMapLoadOptions options;
		options.target = kHiResTargetAuto;
		options.quiet = true;
		if (!HiResFontMap::loadMapFile(path, qualifiers, keys, map, options)) {
			notes.push_back(Common::String::format("%s map %s: does not load%s%s", hiResFontKindName(k), name.c_str(),
												   map.warnings.empty() ? "" : ": ",
												   map.warnings.empty() ? "" : map.warnings[0].c_str()));
			continue;
		}

		FaceCheck check;
		check.exists = exists;
		mapHasCoverage(map, true, ini, path.getParent(), gameDir, &noteMissingFace, &check);
		if (!check.missing.empty()) {
			notes.push_back(Common::String::format("%s map %s: face %s is missing", hiResFontKindName(k), name.c_str(),
												   check.missing.c_str()));
			continue;
		}

		mapPath = path;
		kind = k;
		return true;
	}
	return false;
}

} // End of namespace Graphics
