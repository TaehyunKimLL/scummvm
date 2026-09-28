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

#ifndef BACKENDS_EVENTS_DOS_DOS_EVENTS_H
#define BACKENDS_EVENTS_DOS_DOS_EVENTS_H

#include <SDL3/SDL_keycode.h>
#include "common/events.h"

class DosGraphicsManager;

/** SDL3's DOS keyboard and INT 33h mouse, as ScummVM events. */
class DosEventSource : public Common::EventSource {
public:
	explicit DosEventSource(DosGraphicsManager *gfx) : _gfx(gfx) {}

	bool pollEvent(Common::Event &event) override;

	static Common::KeyCode toKeyCode(SDL_Keycode key);

private:
	DosGraphicsManager *_gfx;
};

#endif
