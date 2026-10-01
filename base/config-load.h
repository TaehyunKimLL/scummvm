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

#ifndef BASE_CONFIG_LOAD_H
#define BASE_CONFIG_LOAD_H

namespace Base {

/** What to do after loading the config file. */
enum ConfigLoadAction {
	kConfigLoadContinue,		///< it loaded
	kConfigLoadAskOverwrite,	///< it did not: ask whether to overwrite it
	kConfigLoadRefuse			///< it did not, and no one can be asked: stop, change nothing
};

/**
 * A config file that did not load is only overwritten when the user says
 * so. Without a GUI to ask (configure --disable-gui) the program stops
 * before anything writes the file: what did not parse would be lost.
 */
inline ConfigLoadAction configLoadAction(bool loaded, bool canAsk) {
	if (loaded)
		return kConfigLoadContinue;
	return canAsk ? kConfigLoadAskOverwrite : kConfigLoadRefuse;
}

} // End of namespace Base

#endif
