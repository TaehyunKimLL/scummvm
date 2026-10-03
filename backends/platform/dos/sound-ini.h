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

#ifndef BACKENDS_PLATFORM_DOS_SOUND_INI_H
#define BACKENDS_PLATFORM_DOS_SOUND_INI_H

#include "common/array.h"
#include "common/scummsys.h"
#include "common/str.h"

namespace DOS {

enum SoundChoice {
	kSoundAdlib,
	kSoundMt32,
	kSoundGm
};

/** "adlib", "mt32" or "gm" in any case. */
inline bool parseSoundChoice(const char *name, SoundChoice &out) {
	if (!name)
		return false;
	Common::String n(name);
	n.toLowercase();
	if (n == "adlib")
		out = kSoundAdlib;
	else if (n == "mt32")
		out = kSoundMt32;
	else if (n == "gm")
		out = kSoundGm;
	else
		return false;
	return true;
}

namespace SoundIniDetail {

inline bool isBlank(char c) {
	return c == ' ' || c == '\t';
}

inline Common::String trim(const Common::String &s) {
	uint b = 0, e = s.size();
	while (b < e && isBlank(s[b]))
		++b;
	while (e > b && isBlank(s[e - 1]))
		--e;
	return Common::String(s.c_str() + b, s.c_str() + e);
}

// Key of a "key = value" line; empty for comments, headers and other text.
inline Common::String keyOf(const Common::String &line) {
	Common::String t = trim(line);
	if (t.empty() || t[0] == '#' || t[0] == ';' || t[0] == '[')
		return Common::String();
	uint eq = 0;
	while (eq < t.size() && t[eq] != '=')
		++eq;
	if (eq == t.size())
		return Common::String();
	return trim(Common::String(t.c_str(), t.c_str() + eq));
}

inline bool isSection(const Common::String &line) {
	Common::String t = trim(line);
	return !t.empty() && t[0] == '[';
}

}

/**
 * `ini` with the music settings of the [scummvm] section set for `choice`
 * and every other line left as it was. Missing keys (or the whole
 * section) are added; the file's line ending style is kept.
 */
inline Common::String applySoundChoice(const Common::String &ini, SoundChoice choice) {
	using namespace SoundIniDetail;
	// The INI parser skips a UTF-8 BOM; so must a header on the first line.
	if (ini.hasPrefix("\xEF\xBB\xBF"))
		return Common::String("\xEF\xBB\xBF") + applySoundChoice(Common::String(ini.c_str() + 3), choice);
	const bool crlf = ini.contains("\r\n");

	Common::Array<Common::String> lines;
	uint start = 0;
	for (uint i = 0; i < ini.size(); ++i) {
		if (ini[i] != '\n')
			continue;
		uint end = i;
		if (end > start && ini[end - 1] == '\r')
			--end;
		lines.push_back(Common::String(ini.c_str() + start, ini.c_str() + end));
		start = i + 1;
	}
	if (start < ini.size())
		lines.push_back(Common::String(ini.c_str() + start));

	struct Setting {
		const char *key;
		const char *value;
	};
	static const Setting kAdlib[] = { { "music_driver", "adlib" }, { "native_mt32", "false" }, { "enable_gs", "false" } };
	static const Setting kMt32[] = { { "music_driver", "mpu401" }, { "native_mt32", "true" }, { "enable_gs", "false" } };
	static const Setting kGm[] = { { "music_driver", "mpu401" }, { "native_mt32", "false" }, { "enable_gs", "true" } };
	const Setting *set = choice == kSoundAdlib ? kAdlib : (choice == kSoundMt32 ? kMt32 : kGm);

	int header = -1;
	for (uint i = 0; i < lines.size(); ++i) {
		if (isSection(lines[i]) && trim(lines[i]) == "[scummvm]") {
			header = (int)i;
			break;
		}
	}
	if (header < 0) {
		lines.insert(lines.begin(), "[scummvm]");
		header = 0;
		for (int k = 0; k < 3; ++k)
			lines.insert(lines.begin() + 1 + k, Common::String(set[k].key) + "=" + set[k].value);
		if (lines.size() > 4)
			lines.insert(lines.begin() + 4, Common::String());
	} else {
		for (int k = 0; k < 3; ++k) {
			uint end = lines.size();
			for (uint i = header + 1; i < lines.size(); ++i)
				if (isSection(lines[i])) {
					end = i;
					break;
				}
			bool found = false;
			uint lastSetting = header;
			for (uint i = header + 1; i < end; ++i) {
				Common::String key = keyOf(lines[i]);
				if (key.empty())
					continue;
				lastSetting = i;
				if (key == set[k].key) {
					lines[i] = Common::String(set[k].key) + "=" + set[k].value;
					found = true;
				}
			}
			if (!found)
				lines.insert(lines.begin() + lastSetting + 1, Common::String(set[k].key) + "=" + set[k].value);
		}
	}

	const char *eol = crlf ? "\r\n" : "\n";
	Common::String out;
	for (uint i = 0; i < lines.size(); ++i) {
		out += lines[i];
		out += eol;
	}
	return out;
}

} // End of namespace DOS

#endif
