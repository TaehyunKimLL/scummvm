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

#include "common/scummsys.h"

#if defined(DOS_DJGPP)

#include <SDL3/SDL.h>

#include "backends/events/dos/dos-events.h"
#include "backends/graphics/dos/dos-graphics.h"

Common::KeyCode DosEventSource::toKeyCode(SDL_Keycode key) {
	if (key < 0x80)
		return (Common::KeyCode)key;	// ASCII, the same numbers on both sides
	switch (key) {
	case SDLK_UP: return Common::KEYCODE_UP;
	case SDLK_DOWN: return Common::KEYCODE_DOWN;
	case SDLK_LEFT: return Common::KEYCODE_LEFT;
	case SDLK_RIGHT: return Common::KEYCODE_RIGHT;
	case SDLK_HOME: return Common::KEYCODE_HOME;
	case SDLK_END: return Common::KEYCODE_END;
	case SDLK_PAGEUP: return Common::KEYCODE_PAGEUP;
	case SDLK_PAGEDOWN: return Common::KEYCODE_PAGEDOWN;
	case SDLK_INSERT: return Common::KEYCODE_INSERT;
	case SDLK_F1: return Common::KEYCODE_F1;
	case SDLK_F2: return Common::KEYCODE_F2;
	case SDLK_F3: return Common::KEYCODE_F3;
	case SDLK_F4: return Common::KEYCODE_F4;
	case SDLK_F5: return Common::KEYCODE_F5;
	case SDLK_F6: return Common::KEYCODE_F6;
	case SDLK_F7: return Common::KEYCODE_F7;
	case SDLK_F8: return Common::KEYCODE_F8;
	case SDLK_F9: return Common::KEYCODE_F9;
	case SDLK_F10: return Common::KEYCODE_F10;
	case SDLK_F11: return Common::KEYCODE_F11;
	case SDLK_F12: return Common::KEYCODE_F12;
	case SDLK_KP_0: return Common::KEYCODE_KP0;
	case SDLK_KP_1: return Common::KEYCODE_KP1;
	case SDLK_KP_2: return Common::KEYCODE_KP2;
	case SDLK_KP_3: return Common::KEYCODE_KP3;
	case SDLK_KP_4: return Common::KEYCODE_KP4;
	case SDLK_KP_5: return Common::KEYCODE_KP5;
	case SDLK_KP_6: return Common::KEYCODE_KP6;
	case SDLK_KP_7: return Common::KEYCODE_KP7;
	case SDLK_KP_8: return Common::KEYCODE_KP8;
	case SDLK_KP_9: return Common::KEYCODE_KP9;
	case SDLK_KP_ENTER: return Common::KEYCODE_KP_ENTER;
	case SDLK_KP_PLUS: return Common::KEYCODE_KP_PLUS;
	case SDLK_KP_MINUS: return Common::KEYCODE_KP_MINUS;
	case SDLK_KP_MULTIPLY: return Common::KEYCODE_KP_MULTIPLY;
	case SDLK_KP_DIVIDE: return Common::KEYCODE_KP_DIVIDE;
	case SDLK_KP_PERIOD: return Common::KEYCODE_KP_PERIOD;
	case SDLK_LSHIFT: return Common::KEYCODE_LSHIFT;
	case SDLK_RSHIFT: return Common::KEYCODE_RSHIFT;
	case SDLK_LCTRL: return Common::KEYCODE_LCTRL;
	case SDLK_RCTRL: return Common::KEYCODE_RCTRL;
	case SDLK_LALT: return Common::KEYCODE_LALT;
	case SDLK_RALT: return Common::KEYCODE_RALT;
	case SDLK_CAPSLOCK: return Common::KEYCODE_CAPSLOCK;
	case SDLK_NUMLOCKCLEAR: return Common::KEYCODE_NUMLOCK;
	case SDLK_SCROLLLOCK: return Common::KEYCODE_SCROLLOCK;
	case SDLK_PAUSE: return Common::KEYCODE_PAUSE;
	default: return Common::KEYCODE_INVALID;
	}
}

static byte toFlags(SDL_Keymod mod) {
	byte f = 0;
	if (mod & SDL_KMOD_SHIFT) f |= Common::KBD_SHIFT;
	if (mod & SDL_KMOD_CTRL) f |= Common::KBD_CTRL;
	if (mod & SDL_KMOD_ALT) f |= Common::KBD_ALT;
	if (mod & SDL_KMOD_CAPS) f |= Common::KBD_CAPS;
	if (mod & SDL_KMOD_NUM) f |= Common::KBD_NUM;
	return f;
}

bool DosEventSource::pollEvent(Common::Event &event) {
	SDL_Event ev;
	while (SDL_PollEvent(&ev)) {
		switch (ev.type) {
		case SDL_EVENT_QUIT:
			event.type = Common::EVENT_QUIT;
			return true;
		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP: {
			event.type = (ev.type == SDL_EVENT_KEY_DOWN) ? Common::EVENT_KEYDOWN : Common::EVENT_KEYUP;
			event.kbdRepeat = ev.key.repeat;
			// The unshifted key, so Shift changing between this key's down and up
			// (SDL3's default keymap maps a shifted letter scancode to 'A'..'Z',
			// outside Common::KeyCode's range) cannot split a press/release pair.
			event.kbd.keycode = toKeyCode(SDL_GetKeyFromScancode(ev.key.scancode, SDL_KMOD_NONE, true));
			event.kbd.flags = toFlags(ev.key.mod);
			// The shifted character for this layout, e.g. 'A' or '!'.
			const SDL_Keycode shifted = SDL_GetKeyFromScancode(ev.key.scancode, ev.key.mod, true);
			event.kbd.ascii = (shifted >= 0x20 && shifted < 0x7F) ? (uint16)shifted
							: (event.kbd.keycode == Common::KEYCODE_RETURN || event.kbd.keycode == Common::KEYCODE_KP_ENTER) ? Common::ASCII_RETURN
							: (event.kbd.keycode == Common::KEYCODE_ESCAPE) ? Common::ASCII_ESCAPE
							: (event.kbd.keycode == Common::KEYCODE_BACKSPACE) ? Common::ASCII_BACKSPACE
							: (event.kbd.keycode == Common::KEYCODE_TAB) ? Common::ASCII_TAB
							: (event.kbd.keycode >= Common::KEYCODE_F1 && event.kbd.keycode <= Common::KEYCODE_F12)
								? (uint16)(Common::ASCII_F1 + (event.kbd.keycode - Common::KEYCODE_F1)) : 0;
			// Unmapped keys (menu/media/extra layout keys) are dropped on purpose.
			if (event.kbd.keycode == Common::KEYCODE_INVALID && !event.kbd.ascii)
				continue;
			return true;
		}
		case SDL_EVENT_MOUSE_MOTION:
			event.type = Common::EVENT_MOUSEMOVE;
			event.mouse = _gfx->gameMouse(ev.motion.x, ev.motion.y);
			_gfx->setMousePos(event.mouse.x, event.mouse.y);
			return true;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP: {
			const bool down = (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
			if (ev.button.button == SDL_BUTTON_LEFT)
				event.type = down ? Common::EVENT_LBUTTONDOWN : Common::EVENT_LBUTTONUP;
			else if (ev.button.button == SDL_BUTTON_RIGHT)
				event.type = down ? Common::EVENT_RBUTTONDOWN : Common::EVENT_RBUTTONUP;
			else
				continue;
			event.mouse = _gfx->gameMouse(ev.button.x, ev.button.y);
			return true;
		}
		default:
			break;
		}
	}
	return false;
}

#endif
