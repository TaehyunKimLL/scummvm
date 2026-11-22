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

#include "common/ptr.h"
#include "common/stream.h"

#include "graphics/hires_text/hires_files.h"

namespace Graphics {

bool hiResValueAvailable(const HiResFontValue &value, HiResFiles &files) {
	for (uint i = 0; i < value.faces.size(); ++i)
		if (!files.locate(value.faces[i]).empty())
			return true;
	return value.faces.empty();
}

namespace {

bool scopeAvailable(const HiResFontScope &s, HiResFiles &files, Common::String &missing) {
	if (s.faceSet && !hiResValueAvailable(s.face, files)) {
		missing = s.face.faces[0].name;
		return false;
	}
	for (uint i = 0; i < s.rangeValues.size(); ++i) {
		if (!hiResValueAvailable(s.rangeValues[i], files)) {
			missing = s.rangeValues[i].faces[0].name;
			return false;
		}
	}
	return true;
}

bool loadPath(HiResFiles &files, const Common::Path &path, const Common::String &name, const HiResMapQualifiers &q,
              HiResMap &map) {
	Common::ScopedPtr<Common::SeekableReadStream> s(files.open(path));
	return s && HiResFontMap::load(*s, name, path.getParent(), q, map);
}

} // End of anonymous namespace

bool pickHiResMap(const HiResOptions &options, const Common::Path &gameDir, const Common::String &langFolder,
                  const HiResMapQualifiers &qualifiers, HiResFiles &files, HiResPick &out,
                  Common::Array<Common::String> &notes) {
	if (!options.enabled) {
		notes.push_back("hires_text is off");
		return false;
	}
	if (options.mapSet) {
		if (options.map.empty()) {
			notes.push_back("hires_text_map is empty: no map");
			return false;
		}
		out.path = findGameMapFile(files, gameDir, langFolder, options.map);
		out.kind = kHiResFontAuto;
		if (out.path.empty()) {
			notes.push_back("hires_text_map " + options.map + ": not found");
			return false;
		}
		if (!loadPath(files, out.path, options.map, qualifiers, out.map)) {
			notes.push_back("hires_text_map " + options.map + ": does not load");
			notes.push_back(out.map.warnings);
			return false;
		}
		return true;
	}
	if (options.font == kHiResFontTtf)
		notes.push_back("hires_text_font=ttf: this build draws no TrueType hi-res text");
	const Common::Array<HiResFontKind> order = hiResFontOrder(options.font);
	for (uint i = 0; i < order.size(); ++i) {
		const char *name = hiResFontKindMap(order[i]);
		const Common::Path path = findGameMapFile(files, gameDir, langFolder, name);
		if (path.empty()) {
			notes.push_back(Common::String::format("%s map %s: not in the game folder", hiResFontKindName(order[i]), name));
			continue;
		}
		HiResMap map;
		if (!loadPath(files, path, name, qualifiers, map)) {
			notes.push_back(Common::String::format("%s map %s: does not load%s%s", hiResFontKindName(order[i]), name,
			                                       map.warnings.empty() ? "" : ": ",
			                                       map.warnings.empty() ? "" : map.warnings[0].c_str()));
			continue;
		}
		Common::String missing;
		bool ok = scopeAvailable(map.font, files, missing);
		for (Common::HashMap<int, HiResFontScope>::const_iterator it = map.fontIds.begin(); ok && it != map.fontIds.end(); ++it)
			ok = scopeAvailable(it->_value, files, missing);
		if (!ok) {
			notes.push_back(Common::String::format("%s map %s: face %s is missing", hiResFontKindName(order[i]), name,
			                                       missing.c_str()));
			continue;
		}
		out.path = path;
		out.kind = order[i];
		out.map = map;
		return true;
	}
	return false;
}

} // End of namespace Graphics
