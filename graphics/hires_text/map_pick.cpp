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
#include "graphics/hires_text/font_value.h"

namespace Graphics {

namespace {

// A font value is there when it names no file at all (same, original) or
// when any one of its files is: the entries are a fallback chain, so a map
// may list a font pack's face first and fonts-cjk.dat's after it.
// @p missing gets the chain as written when none of its files is there.
bool valueAvailable(const HiResFontValue &value, FontFileExistsFn exists, Common::String &missing) {
	bool anyFile = false;
	Common::String written;
	for (uint i = 0; i < value.entries.size(); ++i) {
		const HiResFaceEntry &e = value.entries[i];
		if (e.kind != kHiResFaceFile)
			continue;
		if (exists(e.path))
			return true;
		anyFile = true;
		if (!written.empty())
			written += ", ";
		written += e.path.toString('/');
	}
	if (anyFile && missing.empty())
		missing = written;
	return !anyFile;
}

bool scopeAvailable(const HiResFontScope &scope, FontFileExistsFn exists, Common::String &missing) {
	bool ok = valueAvailable(scope.face, exists, missing);
	for (uint i = 0; i < scope.rangeValues.size(); ++i)
		ok = valueAvailable(scope.rangeValues[i], exists, missing) && ok;
	return ok;
}

bool glyphsAvailable(const HiResGlyphTable &table, FontFileExistsFn exists, Common::String &missing) {
	bool ok = true;
	for (HiResGlyphTable::const_iterator it = table.begin(); it != table.end(); ++it) {
		const HiResGlyphRule &rule = it->_value;
		if (rule.kind != kHiResGlyphTarget && rule.kind != kHiResGlyphTargetOffset)
			continue;
		HiResFontValue one;
		one.entries.push_back(rule.face);
		ok = valueAvailable(one, exists, missing) && ok;
	}
	return ok;
}

// Every font value the map (and the ini's hires_text_face) draws with.
bool facesAvailable(const HiResMap &map, const HiResIniOverrides &ini, const Common::Path &mapDir,
					const Common::Path &gameDir, FontFileExistsFn exists, Common::String &missing) {
	bool ok = true;
	if (ini.faceSet) {
		HiResFontValue fv;
		Common::Array<Common::String> localWarnings;
		parseFontValue(ini.face, map.faces, mapDir, gameDir, fv, localWarnings);
		ok = valueAvailable(fv, exists, missing) && ok;
	}
	ok = scopeAvailable(map.font, exists, missing) && ok;
	for (Common::HashMap<int, HiResFontScope>::const_iterator it = map.fontIds.begin(); it != map.fontIds.end(); ++it)
		ok = scopeAvailable(it->_value, exists, missing) && ok;
	ok = glyphsAvailable(map.glyphs, exists, missing) && ok;
	for (Common::HashMap<int, HiResGlyphTable>::const_iterator it = map.glyphIds.begin(); it != map.glyphIds.end(); ++it)
		ok = glyphsAvailable(it->_value, exists, missing) && ok;
	return ok;
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

		Common::String missing;
		if (!facesAvailable(map, ini, path.getParent(), gameDir, exists, missing)) {
			notes.push_back(Common::String::format("%s map %s: face %s is missing", hiResFontKindName(k), name.c_str(),
												   missing.c_str()));
			continue;
		}

		mapPath = path;
		kind = k;
		return true;
	}
	return false;
}

} // End of namespace Graphics
