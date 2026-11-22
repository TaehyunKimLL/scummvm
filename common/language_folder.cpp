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

namespace Common {

// The text before and after the one "%s" of @p pattern; false unless there is exactly one.
static bool splitPattern(const char *pattern, String &head, String &tail) {
	const char *p = pattern ? strstr(pattern, "%s") : nullptr;
	if (!p || strstr(p + 2, "%s") || strchr(p + 2, '%') || memchr(pattern, '%', p - pattern))
		return false;
	head = String(pattern, p - pattern);
	tail = String(p + 2);
	return true;
}

String translationTableName(const char *pattern, Language lang) {
	String head, tail;
	const char *code = getLanguageFolderCode(lang);
	if (!*code || !splitPattern(pattern, head, tail))
		return String();
	return head + code + tail;
}

// A language folder holds the table @p table or a hi-res text map.
static bool holdsTranslation(const FSNode &folder, const String &table) {
	FSList children;
	if (!folder.getChildren(children, FSNode::kListFilesOnly))
		return false;
	for (uint i = 0; i < children.size(); ++i) {
		const String name = children[i].getName();
		if (name.equalsIgnoreCase(table) || (name.hasPrefixIgnoreCase("hires") && name.hasSuffixIgnoreCase(".map")))
			return true;
	}
	return false;
}

Array<Language> translationFolderLanguages(const FSList &files, const char *pattern) {
	Array<Language> out;
	String head, tail;
	if (!splitPattern(pattern, head, tail))
		return out;
	for (uint i = 0; i < files.size(); ++i) {
		const FSNode &node = files[i];
		const String name = node.getName();
		Language lang = UNK_LANG;
		if (node.isDirectory()) {
			lang = parseLanguageFolderCode(name);
			if (lang == UNK_LANG || !holdsTranslation(node, translationTableName(pattern, lang)))
				continue;
		} else {
			if (name.size() <= head.size() + tail.size() || !name.hasPrefixIgnoreCase(head.c_str()) ||
			    !name.hasSuffixIgnoreCase(tail.c_str()))
				continue;
			lang = parseLanguageFolderCode(String(name.c_str() + head.size(), name.size() - head.size() - tail.size()));
			if (lang == UNK_LANG)
				continue;
		}
		// Keep the list sorted by folder code, each language once.
		const String code = getLanguageFolderCode(lang);
		uint at = 0;
		while (at < out.size() && code.compareTo(getLanguageFolderCode(out[at])) > 0)
			++at;
		if (at < out.size() && code == getLanguageFolderCode(out[at]))
			continue;
		out.insert_at(at, lang);
	}
	return out;
}

} // End of namespace Common
