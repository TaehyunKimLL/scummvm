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

#include "graphics/hires_text/hires_files.h"

#include "common/archive.h"
#include "common/config-manager.h"
#include "common/fs.h"

namespace Graphics {

namespace {

const char *const kSearchManPrefix = "searchman:";

void addRoot(Common::Array<Common::Path> &roots, const Common::Path &p) {
	if (p.empty())
		return;
	for (uint i = 0; i < roots.size(); ++i)
		if (roots[i] == p)
			return;
	roots.push_back(p);
}

} // End of anonymous namespace

HiResDiskFiles::HiResDiskFiles() {
	if (ConfMan.hasKey("extrapath", Common::ConfigManager::kTransientDomain))
		addRoot(_roots, ConfMan.getPath("extrapath", Common::ConfigManager::kTransientDomain));
	if (!ConfMan.getActiveDomainName().empty() && ConfMan.hasKey("extrapath", ConfMan.getActiveDomainName()))
		addRoot(_roots, ConfMan.getPath("extrapath", ConfMan.getActiveDomainName()));
	if (ConfMan.hasKey("extrapath", Common::ConfigManager::kApplicationDomain))
		addRoot(_roots, ConfMan.getPath("extrapath", Common::ConfigManager::kApplicationDomain));
#ifdef DATA_PATH
	addRoot(_roots, Common::Path(DATA_PATH));
#endif
}

bool HiResDiskFiles::isSafeDataPath(const Common::Path &relative) {
	const Common::String s = relative.toString('/');
	if (s.empty() || s[0] == '/' || s[0] == '\\' || (s.size() > 1 && s[1] == ':'))
		return false;
	const Common::StringArray parts = relative.splitComponents();
	for (uint i = 0; i < parts.size(); ++i)
		if (parts[i] == "..")
			return false;
	return true;
}

Common::Path HiResDiskFiles::find(const Common::Path &dir, const Common::String &name) {
	// Each component of @p name ("ko/HIRESVF.MAP") is matched ignoring case.
	Common::FSNode node(dir);
	const Common::StringArray parts = Common::Path(name, '/').splitComponents();
	for (uint c = 0; c < parts.size(); ++c) {
		Common::FSList list;
		const bool last = c + 1 == parts.size();
		if (!node.isDirectory() || !node.getChildren(list, last ? Common::FSNode::kListFilesOnly : Common::FSNode::kListDirectoriesOnly))
			return Common::Path();
		uint i = 0;
		while (i < list.size() && !list[i].getName().equalsIgnoreCase(parts[c]))
			++i;
		if (i == list.size())
			return Common::Path();
		node = list[i];
	}
	return parts.empty() ? Common::Path() : node.getPath();
}

Common::Path HiResDiskFiles::locate(const HiResFaceRef &ref) {
	if (!ref.data)
		return Common::FSNode(ref.path).exists() ? ref.path : Common::Path();
	if (!isSafeDataPath(ref.path))
		return Common::Path();
	for (uint i = 0; i < _roots.size(); ++i) {
		const Common::Path p = _roots[i].join(ref.path);
		if (Common::FSNode(p).exists())
			return p;
	}
	if (SearchMan.hasFile(ref.path))
		return Common::Path(kSearchManPrefix).appendInPlace(ref.path);
	return Common::Path();
}

Common::SeekableReadStream *HiResDiskFiles::open(const Common::Path &path) {
	const Common::String s = path.toString('/');
	if (s.hasPrefix(kSearchManPrefix))
		return SearchMan.createReadStreamForMember(Common::Path(s.c_str() + strlen(kSearchManPrefix), '/'));
	Common::FSNode node(path);
	return node.exists() && !node.isDirectory() ? node.createReadStream() : nullptr;
}

Common::Path findGameMapFile(HiResFiles &files, const Common::Path &gameDir, const Common::String &langFolder,
                             const Common::String &name) {
	if (name.empty())
		return Common::Path();
	if (name.hasPrefix(HiResFontMap::kDataPrefix) || name[0] == '/' || name[0] == '\\' || (name.size() > 1 && name[1] == ':'))
		return files.locate(HiResFontMap::resolveFace(name, gameDir));
	if (name.contains('/'))
		return files.locate(HiResFontMap::resolveFace(name, gameDir));
	if (!langFolder.empty()) {
		const Common::Path inLang = files.find(gameDir, langFolder + "/" + name);
		if (!inLang.empty())
			return inLang;
	}
	return files.find(gameDir, name);
}

} // End of namespace Graphics
