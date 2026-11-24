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

#ifndef BACKENDS_PLATFORM_DOS_SAVENAMES_H
#define BACKENDS_PLATFORM_DOS_SAVENAMES_H

#include "common/str.h"

namespace DOS {

/**
 * DOS keeps 8.3 names, so the name an engine makes from a target ("kq1-rgb565.001") must become one that no other
 * target's names share. A plain cut to 8 characters gave "lsl1sci-en" and "lsl1sci-ko" the same file.
 *
 * dosFileName(): the part before the last dot (the base) stays as it is when it is a valid 8.3 base (at most 8
 * characters, no dot, no wildcard). Otherwise it becomes its first 4 characters (dots as '_') followed by 4 base-36
 * digits of the FNV-1a hash of the whole lower-cased base: "kq1-rgb565" -> "kq1-WXYZ", so targets sharing their first
 * 8 characters stay apart (two bases collide only when their 4-character prefixes and 4 hash digits, 1 in 1,679,616,
 * agree; a short name that happens to equal a mapped one would share too). The mapped base is lower case. The extension is cut to 3 characters.
 * Names are compared ignoring case, as DOS does, so the hash is of the lower-cased base.
 *
 * dosPattern() is the same for a listing pattern: a base with a wildcard ('*', '?', '#') cannot be mapped and is left
 * alone (it matches 8.3 names as listed); the extension is cut only when it has no wildcard. engineName() gives a
 * listed file back under the engine's name when the pattern's base was mapped.
 */
inline bool isWildcard(char c) {
	return c == '*' || c == '?' || c == '#';
}

inline bool hasWildcard(const Common::String &s) {
	for (uint i = 0; i < s.size(); ++i)
		if (isWildcard(s[i]))
			return true;
	return false;
}

inline Common::String mappedBase(const Common::String &base) {
	if (base.size() <= 8 && base.findFirstOf('.') == Common::String::npos)
		return base;
	uint32 h = 2166136261u;
	for (uint i = 0; i < base.size(); ++i) {
		char c = base[i];
		if (c >= 'A' && c <= 'Z')
			c = (char)(c - 'A' + 'a');
		h = (h ^ (byte)c) * 16777619u;
	}
	static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
	Common::String out;
	for (uint i = 0; i < 4 && i < base.size(); ++i)
		out += base[i] == '.' ? '_' : (char)(base[i] >= 'A' && base[i] <= 'Z' ? base[i] - 'A' + 'a' : base[i]);
	h %= 1679616u;
	out += digits[h / 46656];
	out += digits[h / 1296 % 36];
	out += digits[h / 36 % 36];
	out += digits[h % 36];
	return out;
}

inline Common::String dosPattern(const Common::String &pattern) {
	const size_t dot = pattern.findLastOf('.');
	Common::String base, ext;
	if (dot == Common::String::npos) {
		base = pattern;
	} else {
		base = Common::String(pattern.c_str(), dot);
		ext = Common::String(pattern.c_str() + dot + 1);
	}
	if (!hasWildcard(base))
		base = mappedBase(base);
	if (ext.size() > 3 && !hasWildcard(ext))
		ext = Common::String(ext.c_str(), 3);
	return dot == Common::String::npos ? base : base + "." + ext;
}

inline Common::String engineName(const Common::String &pattern, const Common::String &listed) {
	const size_t pdot = pattern.findLastOf('.'), ldot = listed.findLastOf('.');
	if (pdot == Common::String::npos || ldot == Common::String::npos)
		return listed;
	const Common::String base(pattern.c_str(), pdot);
	if (hasWildcard(base) || mappedBase(base) == base)
		return listed;
	Common::String ext(listed.c_str() + ldot + 1);
	ext.toLowercase();
	return base + "." + ext;
}

} // End of namespace DOS

#endif
