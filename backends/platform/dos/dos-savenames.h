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
 * DOS keeps 8.3 names: a save "kq1-rgb565.001" is written as KQ1-RGB5.001 (the open truncates), and a later listing
 * with the engine's pattern "kq1-rgb565.###" finds nothing. dosPattern() is the pattern as DOS names it (the part
 * before the last dot cut to 8, the part after it to 3; a part with a wildcard is left alone); it is also the name a
 * save file gets on disk. engineName() gives a listed file back under the name the engine would open it by, when the
 * engine's base was cut.
 */
inline Common::String dosPattern(const Common::String &pattern) {
	const size_t dot = pattern.findLastOf('.');
	if (dot == Common::String::npos)
		return pattern.size() > 8 && !pattern.contains('*') ? Common::String(pattern.c_str(), 8) : pattern;
	Common::String base(pattern.c_str(), dot), ext(pattern.c_str() + dot + 1);
	if (base.size() > 8 && !base.contains('*'))
		base = Common::String(base.c_str(), 8);
	if (ext.size() > 3 && !ext.contains('*'))
		ext = Common::String(ext.c_str(), 3);
	return base + "." + ext;
}

inline Common::String engineName(const Common::String &pattern, const Common::String &listed) {
	const size_t pdot = pattern.findLastOf('.'), ldot = listed.findLastOf('.');
	if (pdot == Common::String::npos || ldot == Common::String::npos)
		return listed;
	const Common::String base(pattern.c_str(), pdot);
	if (base.size() <= 8 || base.contains('*') || base.contains('?') || base.contains('#'))
		return listed;
	Common::String ext(listed.c_str() + ldot + 1);
	ext.toLowercase();
	return base + "." + ext;
}

} // End of namespace DOS

#endif
