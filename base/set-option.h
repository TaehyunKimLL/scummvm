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

#ifndef BASE_SET_OPTION_H
#define BASE_SET_OPTION_H

#include "common/str.h"
#include "common/util.h"

namespace Base {

/**
 * Split the value of --set=KEY=VALUE. KEY is one or more letters, digits,
 * '_' or '-' ('-' becomes '_' when the setting is stored, as for every
 * option); VALUE is everything after the first '=' and may be empty.
 * Returns false (key and value untouched) for any other text.
 */
inline bool parseSetOption(const char *option, Common::String &key, Common::String &value) {
	if (!option)
		return false;
	const char *eq = strchr(option, '=');
	if (!eq || eq == option)
		return false;
	for (const char *c = option; c < eq; ++c) {
		if (!Common::isAlnum(*c) && *c != '_' && *c != '-')
			return false;
	}
	key = Common::String(option, eq);
	value = Common::String(eq + 1);
	return true;
}

} // End of namespace Base

#endif
