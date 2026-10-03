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

#ifndef BACKENDS_PLATFORM_DOS_DOS_HOME_H
#define BACKENDS_PLATFORM_DOS_DOS_HOME_H

#include <ctype.h>
#include <string.h>

#include "common/array.h"
#include "common/scummsys.h"
#include "common/str.h"

namespace DOS {

/**
 * Where the program keeps what it writes (SCUMMVM.INI, SAVES, logs) and
 * reads its own data (DATA): the folder of the EXE, whatever the current
 * directory is. The game folder may then sit on a read-only drive and be
 * named on the command line.
 */
namespace HomeDetail {

inline bool isSep(char c) {
	return c == '/' || c == '\\';
}

inline bool hasDrive(const char *p) {
	return ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':';
}

}

/** "C:\SCUMMVM\SCUMMVM.EXE" -> "C:\SCUMMVM"; "C:\SCUMMVM.EXE" -> "C:\"; "" when argv0 has no directory. */
inline Common::String exeDir(const char *argv0) {
	if (!argv0)
		return Common::String();
	const char *last = nullptr;
	for (const char *p = argv0; *p; ++p)
		if (HomeDetail::isSep(*p))
			last = p;
	if (!last) {
		// "C:SCUMMVM.EXE": the drive's current directory is not ours to name.
		return Common::String();
	}
	Common::String dir(argv0, last);
	if (dir.empty() || (dir.size() == 2 && HomeDetail::hasDrive(dir.c_str())))
		dir += *last;
	return dir;
}

/**
 * The current directory of drive `letter`, as "X:/DIR": what a drive-relative
 * "X:" or "X:DIR" refers to before the program changes directory. Empty when
 * unknown.
 */
typedef Common::String (*DriveCwdFn)(char letter);

namespace HomeDetail {

inline Common::String slashed(Common::String s) {
	for (uint i = 0; i < s.size(); ++i)
		if (s[i] == '\\')
			s.setChar('/', i);
	return s;
}

} // End of namespace HomeDetail

/**
 * `p` made absolute against `cwd` (an absolute path); an absolute `p` is
 * returned as given. A joined result uses '/' only: DJGPP's path code splits
 * on that alone.
 */
inline Common::String absolutePath(const Common::String &cwd, const char *p, DriveCwdFn driveCwd = nullptr) {
	if (HomeDetail::hasDrive(p)) {
		// "D:" and "D:GAMES" are relative to drive D's own current directory.
		if (HomeDetail::isSep(p[2]))
			return p;
		const char letter = p[0];
		Common::String base;
		if (HomeDetail::hasDrive(cwd.c_str()) && toupper((unsigned char)cwd[0]) == toupper((unsigned char)letter))
			base = cwd;
		else if (driveCwd)
			base = driveCwd(letter);
		if (base.empty())
			return p;
		if (!p[2])
			return HomeDetail::slashed(base);
		if (!HomeDetail::isSep(base.lastChar()))
			base += '/';
		return HomeDetail::slashed(base + (p + 2));
	}
	if (HomeDetail::isSep(p[0])) {
		// "\GAMES": the drive of cwd, from its root.
		return HomeDetail::slashed(HomeDetail::hasDrive(cwd.c_str()) ? Common::String(cwd.c_str(), 2) + p : Common::String(p));
	}
	if (cwd.empty())
		return p;
	Common::String base = cwd;
	if (!HomeDetail::isSep(base.lastChar()))
		base += '/';
	return HomeDetail::slashed(base + p);
}

/** Two directory names for the same place, ignoring case, separator style and a trailing separator. */
inline bool samePath(const Common::String &a, const Common::String &b) {
	Common::String x[2] = { a, b };
	for (int k = 0; k < 2; ++k) {
		x[k].toUppercase();
		for (uint i = 0; i < x[k].size(); ++i)
			if (x[k][i] == '/')
				x[k].setChar('\\', i);
		while (!x[k].empty() && x[k].lastChar() == '\\')
			x[k].deleteLastChar();
	}
	return x[0] == x[1];
}

namespace HomeDetail {

// Options whose value is a file or directory the program opens relative to
// the current directory. Only the long names carry a table entry; the short
// letters are `-c`, `-i`, `-l` and `-p`.
inline bool isPathLongOption(const Common::String &name) {
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

}

/**
 * The command line with every path-valued option made absolute against
 * `cwd`, for a program that is about to change its current directory.
 * Handles --opt=VALUE, -oVALUE and -o VALUE; any other word is copied.
 * A command that looks for games in the current directory (--add,
 * --detect, --auto-detect) given no --path gets one naming `cwd`.
 */
inline Common::Array<Common::String> absolutizeArgs(int argc, const char *const *argv, const Common::String &cwd,
													DriveCwdFn driveCwd = nullptr) {
	Common::Array<Common::String> out;
	bool hasPath = false, scansCwd = false;
	for (int i = 0; i < argc; ++i) {
		const char *a = argv[i];
		if (i == 0 || !a || a[0] != '-' || !a[1]) {
			out.push_back(a ? a : "");
			continue;
		}
		if (a[1] == '-') {
			if (!strcmp(a, "--add") || !strcmp(a, "--detect") || !strcmp(a, "--auto-detect"))
				scansCwd = true;
			const char *eq = strchr(a, '=');
			if (eq && HomeDetail::isPathLongOption(Common::String(a + 2, eq)) && eq[1]) {
				if (!strncmp(a, "--path=", 7))
					hasPath = true;
				out.push_back(Common::String(a, eq + 1) + absolutePath(cwd, eq + 1, driveCwd));
				continue;
			}
			out.push_back(a);
			continue;
		}
		if (a[1] == 'a' || a[1] == 'A')
			scansCwd = scansCwd || !a[2];
		if (!HomeDetail::isPathShortOption(a[1])) {
			out.push_back(a);
			continue;
		}
		if (a[1] == 'p' || a[1] == 'P')
			hasPath = true;
		if (a[2]) {
			out.push_back(Common::String(a, 2) + absolutePath(cwd, a + 2, driveCwd));
		} else if (i + 1 < argc && argv[i + 1] && *argv[i + 1]) {
			out.push_back(a);
			out.push_back(absolutePath(cwd, argv[++i], driveCwd));
		} else {
			out.push_back(a);
		}
	}
	if (scansCwd && !hasPath)
		out.push_back("--path=" + HomeDetail::slashed(cwd));
	return out;
}

} // End of namespace DOS

#endif
