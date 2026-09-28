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

#ifndef GUI_DEBUGSOCKET_DOSUART_H
#define GUI_DEBUGSOCKET_DOSUART_H

#include "common/str.h"

namespace GUI {

/**
 * A 16550 UART polled from the game loop, the DOS transport of the debug
 * socket. No interrupts: the receive FIFO holds 16 bytes and is drained
 * once a frame, so the host paces what it sends (2 ms a byte).
 */
class DosUart {
public:
	DosUart() : _base(0) {}

	/** "com1".."com4", optionally ":<baud>" (a divisor of 115200). */
	bool open(const Common::String &spec);
	bool isOpen() const { return _base != 0; }
	/** Whatever has arrived, up to max bytes; never waits. */
	int read(char *buf, int max);
	/** Blocks until every byte is in the transmit register. */
	void write(const char *p, uint len);

private:
	uint16 _base;
};

} // End of namespace GUI

#endif
