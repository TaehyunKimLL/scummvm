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

#ifndef BACKENDS_PLATFORM_DOS_LAUNCHER_PLAY_HOME_H
#define BACKENDS_PLATFORM_DOS_LAUNCHER_PLAY_HOME_H

#include <ctype.h>
#include <string.h>

#include <string>
#include <vector>

// The launcher (PLAY.EXE) keeps what a game writes in a folder of its own on
// the writable drive, and reads the game from the folder the pack was
// started in, which may be on a read-only drive. Plain std::string: the
// launcher links no ScummVM code.
namespace Play {

namespace Detail {

inline bool isSep(char c) {
	return c == '/' || c == '\\';
}

inline bool hasDrive(const char *p) {
	return ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':';
}

inline std::string slashed(std::string s) {
	for (size_t i = 0; i < s.size(); ++i)
		if (s[i] == '\\')
			s[i] = '/';
	return s;
}

inline char lastChar(const std::string &s) {
	return s.empty() ? '\0' : s[s.size() - 1];
}

inline std::string upper(std::string s) {
	for (size_t i = 0; i < s.size(); ++i)
		s[i] = (char)toupper((unsigned char)s[i]);
	return s;
}

inline bool startsWith(const std::string &s, const char *prefix) {
	return s.compare(0, strlen(prefix), prefix) == 0;
}

} // End of namespace Detail

/** "C:\SCUMMVM\PLAY.EXE" -> "C:\SCUMMVM"; "C:\PLAY.EXE" -> "C:\"; "" when argv0 has no directory. */
inline std::string exeDir(const char *argv0) {
	if (!argv0)
		return std::string();
	const char *last = nullptr;
	for (const char *p = argv0; *p; ++p)
		if (Detail::isSep(*p))
			last = p;
	if (!last)
		return std::string();
	std::string dir(argv0, last);
	if (dir.empty() || (dir.size() == 2 && Detail::hasDrive(dir.c_str())))
		dir += *last;
	return dir;
}

/**
 * The current directory of drive `letter`, as "X:/DIR": what a drive-relative
 * "X:" or "X:DIR" refers to. Empty when unknown.
 */
typedef std::string (*DriveCwdFn)(char letter);

/**
 * `p` made absolute against `cwd` (an absolute path); an absolute `p` is
 * returned as given. A joined result uses '/' only: DJGPP's path code splits
 * on that alone.
 */
inline std::string absolutePath(const std::string &cwd, const char *p, DriveCwdFn driveCwd = nullptr) {
	if (Detail::hasDrive(p)) {
		// "D:" and "D:GAMES" are relative to drive D's own current directory.
		if (Detail::isSep(p[2]))
			return p;
		const char letter = p[0];
		std::string base;
		if (Detail::hasDrive(cwd.c_str()) && toupper((unsigned char)cwd[0]) == toupper((unsigned char)letter))
			base = cwd;
		else if (driveCwd)
			base = driveCwd(letter);
		if (base.empty())
			return p;
		if (!p[2])
			return Detail::slashed(base);
		if (!Detail::isSep(Detail::lastChar(base)))
			base += '/';
		return Detail::slashed(base + (p + 2));
	}
	if (Detail::isSep(p[0])) {
		// "\GAMES": the drive of cwd, from its root.
		return Detail::slashed(Detail::hasDrive(cwd.c_str()) ? cwd.substr(0, 2) + p : std::string(p));
	}
	if (cwd.empty())
		return p;
	std::string base = cwd;
	if (!Detail::isSep(Detail::lastChar(base)))
		base += '/';
	return Detail::slashed(base + p);
}

/** Two directory names for the same place, ignoring case, separator style and a trailing separator. */
inline bool samePath(const std::string &a, const std::string &b) {
	std::string x[2] = { Detail::upper(a), Detail::upper(b) };
	for (int k = 0; k < 2; ++k) {
		for (size_t i = 0; i < x[k].size(); ++i)
			if (x[k][i] == '/')
				x[k][i] = '\\';
		while (!x[k].empty() && x[k][x[k].size() - 1] == '\\')
			x[k].erase(x[k].size() - 1);
	}
	return x[0] == x[1];
}

namespace Detail {

// Options whose value is a file or directory. Only the long names carry a
// table entry; the short letters are `-c`, `-i`, `-l` and `-p`.
inline bool isPathLongOption(const std::string &name) {
	static const char *const kNames[] = {
		"path", "savepath", "extrapath", "iconspath", "screenshotpath",
		"themepath", "config", "initial-cfg", "logfile", "soundfont",
		"md5-path", nullptr
	};
	for (int i = 0; kNames[i]; ++i)
		if (name == kNames[i])
			return true;
	return false;
}

inline bool isPathShortOption(char c) {
	switch (c) {
	case 'c': case 'C': case 'i': case 'I': case 'l': case 'L': case 'p': case 'P':
		return true;
	default:
		return false;
	}
}

} // End of namespace Detail

/**
 * The command line words in argv[0..argc) with every path-valued option made
 * absolute against `cwd`, for a program that is started in another directory.
 * Handles --opt=VALUE, -oVALUE and -o VALUE; any other word is copied. A
 * command that looks for games in the current directory (--add, --detect,
 * --auto-detect) given no --path gets one naming `cwd`.
 */
inline std::vector<std::string> absolutizeArgs(int argc, const char *const *argv, const std::string &cwd,
												DriveCwdFn driveCwd = nullptr) {
	std::vector<std::string> out;
	bool hasPath = false, scansCwd = false;
	for (int i = 0; i < argc; ++i) {
		const char *a = argv[i];
		if (!a || a[0] != '-' || !a[1]) {
			out.push_back(a ? a : "");
			continue;
		}
		if (a[1] == '-') {
			if (!strcmp(a, "--add") || !strcmp(a, "--detect") || !strcmp(a, "--auto-detect"))
				scansCwd = true;
			const char *eq = strchr(a, '=');
			if (eq && Detail::isPathLongOption(std::string(a + 2, eq)) && eq[1]) {
				if (!strncmp(a, "--path=", 7))
					hasPath = true;
				out.push_back(std::string(a, eq + 1) + absolutePath(cwd, eq + 1, driveCwd));
				continue;
			}
			out.push_back(a);
			continue;
		}
		if (a[1] == 'a' || a[1] == 'A')
			scansCwd = scansCwd || !a[2];
		if (!Detail::isPathShortOption(a[1])) {
			out.push_back(a);
			continue;
		}
		if (a[1] == 'p' || a[1] == 'P')
			hasPath = true;
		if (a[2]) {
			out.push_back(std::string(a, 2) + absolutePath(cwd, a + 2, driveCwd));
		} else if (i + 1 < argc && argv[i + 1] && *argv[i + 1]) {
			out.push_back(a);
			out.push_back(absolutePath(cwd, argv[++i], driveCwd));
		} else {
			out.push_back(a);
		}
	}
	if (scansCwd && !hasPath)
		out.push_back("--path=" + Detail::slashed(cwd));
	return out;
}

/** A game id names a folder: 1 to 8 letters, digits, '_' or '-'. */
inline bool validId(const std::string &id) {
	if (id.empty() || id.size() > 8)
		return false;
	for (size_t i = 0; i < id.size(); ++i) {
		const char c = id[i];
		if (!(c >= '0' && c <= '9') && !(c >= 'A' && c <= 'Z') && !(c >= 'a' && c <= 'z') && c != '_' && c != '-')
			return false;
	}
	return true;
}

/** DEFAULT names the music default of all games, so no game can use it. `id` is upper case. */
inline bool isReservedId(const std::string &id) {
	return id == "DEFAULT";
}

/** `a` and `b` joined with one '/'; every separator in the result is '/'. */
inline std::string join(const std::string &a, const std::string &b) {
	std::string r = Detail::slashed(a);
	if (!r.empty() && r[r.size() - 1] != '/')
		r += '/';
	return r + Detail::slashed(b);
}

/** `path` the way DOS users write it, for messages. */
inline std::string dosPath(std::string path) {
	for (size_t i = 0; i < path.size(); ++i)
		if (path[i] == '/')
			path[i] = '\\';
	return path;
}

namespace Detail {

inline std::string lower(std::string s) {
	for (size_t i = 0; i < s.size(); ++i)
		s[i] = (char)tolower((unsigned char)s[i]);
	return s;
}

inline std::string trimmed(const std::string &s) {
	size_t b = 0, e = s.size();
	while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r'))
		++b;
	while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r'))
		--e;
	return s.substr(b, e - b);
}

// Calls `fn(section, key, value)` for each "key = value" line, section and key
// lower case and trimmed; returns early when fn returns true.
template<class Fn>
inline void eachSetting(const std::string &ini, Fn fn) {
	std::string section;
	size_t pos = 0;
	while (pos < ini.size()) {
		size_t end = ini.find('\n', pos);
		if (end == std::string::npos)
			end = ini.size();
		const std::string line = trimmed(ini.substr(pos, end - pos));
		pos = end + 1;
		if (line.empty() || line[0] == '#' || line[0] == ';')
			continue;
		if (line[0] == '[') {
			const size_t close = line.find(']');
			section = lower(trimmed(line.substr(1, close == std::string::npos ? std::string::npos : close - 1)));
			continue;
		}
		const size_t eq = line.find('=');
		if (eq == std::string::npos)
			continue;
		if (fn(section, lower(trimmed(line.substr(0, eq))), trimmed(line.substr(eq + 1))))
			return;
	}
}

} // End of namespace Detail

/**
 * The engine named by `engineid=` in the section `target` of an INI text, or
 * by the first `engineid=` line when `target` has none; lower case, empty when
 * there is none.
 */
inline std::string engineOf(const std::string &ini, const std::string &target = std::string()) {
	const std::string want = Detail::lower(target);
	std::string first, found;
	Detail::eachSetting(ini, [&](const std::string &section, const std::string &key, const std::string &value) {
		if (key != "engineid")
			return false;
		if (first.empty())
			first = Detail::lower(value);
		if (!want.empty() && section == want)
			found = Detail::lower(value);
		return !found.empty();
	});
	return found.empty() ? first : found;
}

/** True when the INI text has a section named `target` with at least one setting, in any case. */
inline bool hasTarget(const std::string &ini, const std::string &target) {
	const std::string want = Detail::lower(target);
	bool seen = false;
	Detail::eachSetting(ini, [&](const std::string &section, const std::string &, const std::string &) {
		seen = seen || section == want;
		return seen;
	});
	return seen;
}

/** The target of a command line: its last word that is not an option; empty when there is none. */
inline std::string targetOf(const std::vector<std::string> &extra) {
	for (size_t i = extra.size(); i-- > 0;)
		if (!extra[i].empty() && extra[i][0] != '-')
			return extra[i];
	return std::string();
}

/** Where one game's own files are, below the home folder (the writable one). */
struct Profile {
	std::string dir;   // HOME/GAMES/ID
	std::string ini;   // HOME/GAMES/ID/SCUMMVM.INI
	std::string saves; // HOME/GAMES/ID/SAVES
	std::string log;   // HOME/GAMES/ID/SCUMMVM.LOG
	std::string exitLog; // HOME/GAMES/ID/EXITLOG.TXT
	std::string sound; // HOME/GAMES/ID/SOUND.INI: the game's own music choice, when it has one
};

inline Profile profileOf(const std::string &home, const std::string &id) {
	Profile p;
	p.dir = join(join(home, "GAMES"), Detail::upper(id));
	p.ini = join(p.dir, "SCUMMVM.INI");
	p.saves = join(p.dir, "SAVES");
	p.log = join(p.dir, "SCUMMVM.LOG");
	p.exitLog = join(p.dir, "EXITLOG.TXT");
	p.sound = join(p.dir, "SOUND.INI");
	return p;
}

/**
 * The words after the program name for a game run: the profile's INI and
 * save folder, the game folder when `gameDir` is not empty and `extra` names
 * none, then `extra`.
 */
inline std::vector<std::string> childArgs(const Profile &p, const std::string &gameDir,
											const std::vector<std::string> &extra) {
	std::vector<std::string> out;
	out.push_back("--config=" + p.ini);
	out.push_back("--savepath=" + p.saves);
	bool hasPath = false;
	for (size_t i = 0; i < extra.size(); ++i)
		if (Detail::startsWith(extra[i], "--path=") || Detail::startsWith(extra[i], "-p") || Detail::startsWith(extra[i], "-P"))
			hasPath = true;
	if (!gameDir.empty() && !hasPath)
		out.push_back("--path=" + gameDir);
	out.insert(out.end(), extra.begin(), extra.end());
	return out;
}

} // End of namespace Play

#endif
