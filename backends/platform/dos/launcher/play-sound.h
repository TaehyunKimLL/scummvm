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

#ifndef BACKENDS_PLATFORM_DOS_LAUNCHER_PLAY_SOUND_H
#define BACKENDS_PLATFORM_DOS_LAUNCHER_PLAY_SOUND_H

#include <ctype.h>
#include <string.h>

#include <string>
#include <vector>

namespace Play {

enum SoundChoice {
	kSoundAdlib,
	kSoundMt32,
	kSoundGm
};

/** "adlib", "mt32" or "gm" in any case. */
inline bool parseSoundChoice(const char *name, SoundChoice &out) {
	if (!name)
		return false;
	std::string n(name);
	for (size_t i = 0; i < n.size(); ++i)
		n[i] = (char)tolower((unsigned char)n[i]);
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

/** The menu key of the music output: '1' AdLib, '2' MT-32, '3' General MIDI. */
inline bool soundOfKey(int key, SoundChoice &out) {
	switch (key) {
	case '1':
		out = kSoundAdlib;
		return true;
	case '2':
		out = kSoundMt32;
		return true;
	case '3':
		out = kSoundGm;
		return true;
	default:
		return false;
	}
}

/** True when a BLASTER value has a P word (the MPU-401 port), like "A220 I7 D1 P330". */
inline bool blasterHasMpuPort(const char *blaster) {
	if (!blaster)
		return false;
	for (const char *c = blaster; *c;) {
		while (*c == ' ' || *c == '\t')
			++c;
		const char *word = c;
		while (*c && *c != ' ' && *c != '\t')
			++c;
		if (c - word > 1 && (*word == 'P' || *word == 'p')) {
			bool hex = true;
			for (const char *d = word + 1; d < c; ++d)
				hex = hex && ((*d >= '0' && *d <= '9') || (*d >= 'A' && *d <= 'F') || (*d >= 'a' && *d <= 'f'));
			if (hex)
				return true;
		}
	}
	return false;
}

namespace SoundDetail {

inline bool isBlank(char c) {
	return c == ' ' || c == '\t';
}

inline std::string trim(const std::string &s) {
	size_t b = 0, e = s.size();
	while (b < e && isBlank(s[b]))
		++b;
	while (e > b && isBlank(s[e - 1]))
		--e;
	return s.substr(b, e - b);
}

// Key of a "key = value" line; empty for comments, headers and other text.
inline std::string keyOf(const std::string &line) {
	std::string t = trim(line);
	if (t.empty() || t[0] == '#' || t[0] == ';' || t[0] == '[')
		return std::string();
	const size_t eq = t.find('=');
	if (eq == std::string::npos)
		return std::string();
	return trim(t.substr(0, eq));
}

inline std::string lower(std::string s) {
	for (size_t i = 0; i < s.size(); ++i)
		s[i] = (char)tolower((unsigned char)s[i]);
	return s;
}

inline bool isSection(const std::string &line) {
	const std::string t = trim(line);
	return !t.empty() && t[0] == '[';
}

} // End of namespace SoundDetail

/**
 * `ini` with the music settings of the [scummvm] section set for `choice`
 * and every other line left as it was. Missing keys (or the whole
 * section) are added; the file's line ending style is kept.
 */
inline std::string applySoundChoice(const std::string &ini, SoundChoice choice) {
	using namespace SoundDetail;
	// The INI parser skips a UTF-8 BOM; so must a header on the first line.
	if (ini.compare(0, 3, "\xEF\xBB\xBF") == 0)
		return std::string("\xEF\xBB\xBF") + applySoundChoice(ini.substr(3), choice);
	const bool crlf = ini.find("\r\n") != std::string::npos;

	std::vector<std::string> lines;
	size_t start = 0;
	for (size_t i = 0; i < ini.size(); ++i) {
		if (ini[i] != '\n')
			continue;
		size_t end = i;
		if (end > start && ini[end - 1] == '\r')
			--end;
		lines.push_back(ini.substr(start, end - start));
		start = i + 1;
	}
	if (start < ini.size())
		lines.push_back(ini.substr(start));

	struct Setting {
		const char *key;
		const char *value;
	};
	static const Setting kAdlib[] = { { "music_driver", "adlib" }, { "native_mt32", "false" }, { "enable_gs", "false" } };
	static const Setting kMt32[] = { { "music_driver", "mpu401" }, { "native_mt32", "true" }, { "enable_gs", "false" } };
	static const Setting kGm[] = { { "music_driver", "mpu401" }, { "native_mt32", "false" }, { "enable_gs", "true" } };
	const Setting *set = choice == kSoundAdlib ? kAdlib : (choice == kSoundMt32 ? kMt32 : kGm);

	int header = -1;
	for (size_t i = 0; i < lines.size(); ++i) {
		if (isSection(lines[i]) && lower(trim(lines[i])) == "[scummvm]") {
			header = (int)i;
			break;
		}
	}
	if (header < 0) {
		lines.insert(lines.begin(), "[scummvm]");
		header = 0;
		for (int k = 0; k < 3; ++k)
			lines.insert(lines.begin() + 1 + k, std::string(set[k].key) + "=" + set[k].value);
		if (lines.size() > 4)
			lines.insert(lines.begin() + 4, std::string());
	} else {
		for (int k = 0; k < 3; ++k) {
			size_t end = lines.size();
			for (size_t i = header + 1; i < lines.size(); ++i)
				if (isSection(lines[i])) {
					end = i;
					break;
				}
			bool found = false;
			size_t lastSetting = header;
			for (size_t i = header + 1; i < end; ++i) {
				const std::string key = keyOf(lines[i]);
				if (key.empty())
					continue;
				lastSetting = i;
				if (lower(key) == set[k].key) {
					lines[i] = std::string(set[k].key) + "=" + set[k].value;
					found = true;
				}
			}
			if (!found)
				lines.insert(lines.begin() + lastSetting + 1, std::string(set[k].key) + "=" + set[k].value);
		}
	}

	const char *eol = crlf ? "\r\n" : "\n";
	std::string out;
	for (size_t i = 0; i < lines.size(); ++i) {
		out += lines[i];
		out += eol;
	}
	return out;
}

} // End of namespace Play

#endif
