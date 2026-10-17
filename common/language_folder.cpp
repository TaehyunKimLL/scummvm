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

#include "common/language_folder.h"
#include "common/str.h"

namespace Common {

namespace {

// <prefix>-<code>.str, or a hi-res text map, in a language folder.
bool holdsTranslation(const FSNode &folder, const String &table) {
	FSList children;
	if (!folder.getChildren(children, FSNode::kListFilesOnly))
		return false;
	for (uint i = 0; i < children.size(); ++i) {
		String name = children[i].getName();
		name.toLowercase();
		if (name == table || (name.hasPrefix("hires") && name.hasSuffix(".map")))
			return true;
	}
	return false;
}

} // End of anonymous namespace

Array<Language> translationFolderLanguages(const FSList &files, const char *prefix) {
	const String head = String(prefix) + "-";
	Array<Language> out;
	for (uint i = 0; i < files.size(); ++i) {
		const FSNode &node = files[i];
		String name = node.getName();
		name.toLowercase();
		Language lang = UNK_LANG;
		if (node.isDirectory()) {
			lang = parseLanguageFolderCode(name);
			if (lang == UNK_LANG || !holdsTranslation(node, head + name + ".str"))
				continue;
		} else {
			if (!name.hasPrefix(head) || !name.hasSuffix(".str") || name.size() <= head.size() + 4)
				continue;
			lang = parseLanguageFolderCode(String(name.c_str() + head.size(), name.size() - head.size() - 4));
		}
		if (lang == UNK_LANG)
			continue;
		bool seen = false;
		for (uint j = 0; j < out.size(); ++j)
			seen |= out[j] == lang;
		if (!seen)
			out.push_back(lang);
	}
	return out;
}

} // End of namespace Common
