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

#include <pc.h>

#include "gui/debugsocket-dosuart.h"
#include "common/debug.h"
#include "common/textconsole.h"

namespace GUI {

static const uint16 kComBase[4] = { 0x3F8, 0x2F8, 0x3E8, 0x2E8 };

bool DosUart::open(const Common::String &spec) {
	Common::String s = spec;
	s.toLowercase();
	if (!s.hasPrefix("com") || s.size() < 4 || s[3] < '1' || s[3] > '4') {
		warning("DebugSocket: '%s' is not com1..com4", spec.c_str());
		return false;
	}
	uint32 baud = 115200;
	if (s.size() > 5 && s[4] == ':')
		baud = atol(s.c_str() + 5);
	if (baud == 0 || 115200 % baud) {
		warning("DebugSocket: %u baud does not divide 115200", (uint)baud);
		return false;
	}
	const uint16 base = kComBase[s[3] - '1'];
	outportb(base + 7, 0x5A);		// scratch register: is there a UART at all?
	if (inportb(base + 7) != 0x5A) {
		warning("DebugSocket: no UART at 0x%X", base);
		return false;
	}
	const uint16 div = (uint16)(115200 / baud);
	outportb(base + 1, 0x00);		// no interrupts
	outportb(base + 3, 0x80);		// DLAB
	outportb(base + 0, div & 0xFF);
	outportb(base + 1, div >> 8);
	outportb(base + 3, 0x03);		// 8N1
	outportb(base + 2, 0xC7);		// FIFO on, both cleared, 14-byte trigger
	outportb(base + 4, 0x03);		// DTR, RTS
	_base = base;
	debug(1, "DebugSocket: COM at 0x%X, %u baud", base, (uint)baud);
	return true;
}

int DosUart::read(char *buf, int max) {
	int n = 0;
	while (n < max && (inportb(_base + 5) & 0x01))
		buf[n++] = (char)inportb(_base);
	return n;
}

void DosUart::write(const char *p, uint len) {
	for (uint i = 0; i < len; ++i) {
		while (!(inportb(_base + 5) & 0x20))
			;
		outportb(_base, (byte)p[i]);
	}
}

} // End of namespace GUI

#endif
