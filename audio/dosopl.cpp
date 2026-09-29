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

#define FORBIDDEN_SYMBOL_EXCEPTION_getenv

#include "audio/dosopl.h"

#include "backends/platform/dos/blaster.h"
#include "common/debug.h"
#include "common/textconsole.h"

#include <pc.h>
#include <stdlib.h>

namespace OPL {
namespace DosOPL {

namespace {

/**
 * Status port reads after an address write and after a data write. An
 * OPL2 needs 3.3 us after the address and 23 us after the data; a status
 * read on the ISA bus takes about 1 us, with margin for faster buses.
 */
const int kAddressDelayReads = 6;
const int kDataDelayReads = 35;

/**
 * Upper bound on status reads while waiting for timer 1 (80 us period) to
 * overflow during detection. The wait ends as soon as the flag shows, so
 * this only bounds the time spent on a port with no chip behind it.
 */
const int kTimerPollReads = 100000;

void delayReads(uint16 status, int count) {
	for (int i = 0; i < count; ++i)
		inportb(status);
}

void rawWrite(uint16 port, uint16 status, int reg, int value) {
	outportb(port, (uint8)reg);
	delayReads(status, kAddressDelayReads);
	outportb(port + 1, (uint8)value);
	delayReads(status, kDataDelayReads);
}

/**
 * The AdLib timer test at @p base: with both timers reset the status
 * reads 0 in bits 7..5; start timer 1 at 0xFF (overflow after 80 us) and
 * bits 7 and 6 must come up. The OPL2 returns 1s in status bits 2..1, the
 * OPL3 0s.
 */
bool probe(uint16 base, bool &opl3) {
	rawWrite(base, base, 0x04, 0x60);
	rawWrite(base, base, 0x04, 0x80);
	uint8 before = inportb(base);
	rawWrite(base, base, 0x02, 0xFF);
	rawWrite(base, base, 0x04, 0x21);
	uint8 after = 0;
	for (int i = 0; i < kTimerPollReads; ++i) {
		after = inportb(base);
		if ((after & 0xE0) == 0xC0)
			break;
	}
	rawWrite(base, base, 0x04, 0x60);
	rawWrite(base, base, 0x04, 0x80);

	if ((before & 0xE0) != 0 || (after & 0xE0) != 0xC0)
		return false;
	opl3 = (after & 0x06) == 0;
	return true;
}

struct Probe {
	bool done;
	bool found;
	uint16 base;
	bool opl3;
};

Probe g_probe = { false, false, 0, false };

/**
 * Looks for the chip at 0x388 (every AdLib-compatible card), then at the
 * BLASTER base A (Sound Blaster Pro 2/16: an OPL3 with its second bank at
 * A+2), then at A+8 (the OPL2-compatible pair of every Sound Blaster).
 */
const Probe &probeHardware() {
	if (g_probe.done)
		return g_probe;
	g_probe.done = true;

	::DOS::BlasterConfig blaster = ::DOS::parseBlaster(getenv("BLASTER"));

	uint16 bases[3];
	int count = 0;
	bases[count++] = 0x388;
	if (blaster.present) {
		bases[count++] = blaster.port;
		bases[count++] = blaster.port + 8;
	}

	for (int i = 0; i < count; ++i) {
		bool opl3 = false;
		if (probe(bases[i], opl3)) {
			g_probe.found = true;
			g_probe.base = bases[i];
			// A+8 decodes only the address/data pair of the first bank.
			g_probe.opl3 = opl3 && i != 2;
			break;
		}
	}
	return g_probe;
}

} // End of anonymous namespace

bool OPL::detect(Config::OplType type) {
	const Probe &p = probeHardware();
	if (!p.found)
		return false;
	return type == Config::kOpl2 || p.opl3;
}

OPL::OPL(Config::OplType type) : _type(type), _activeReg(0), _initialized(false), _base(0x388), _opl3(false) {
}

OPL::~OPL() {
	// Stop the callbacks before silencing the chip: RealChip's destructor
	// runs after this one.
	stop();
	if (_initialized) {
		reset();
		_initialized = false;
	}
}

bool OPL::init() {
	const Probe &p = probeHardware();
	if (!p.found) {
		warning("DOSOPL: No OPL chip found at 0x388 or the BLASTER port");
		return false;
	}
	if (_type != Config::kOpl2 && !p.opl3) {
		warning("DOSOPL: %s needs an OPL3, found an OPL2 at 0x%X", _type == Config::kOpl3 ? "OPL3" : "Dual OPL2", p.base);
		return false;
	}

	_base = p.base;
	_opl3 = p.opl3;
	_initialized = true;
	debug("DOSOPL: %s at 0x%X, type %d", _opl3 ? "OPL3" : "OPL2", _base, (int)_type);

	reset();
	return true;
}

void OPL::reset() {
	if (!_initialized)
		return;

	writeReg(0x01, 0, true);
	writeReg(0x02, 0, true);
	writeReg(0x03, 0, true);
	writeReg(0x04, 0x60, true);
	writeReg(0x04, 0x80, true);
	if (_opl3) {
		writeReg(0x104, 0, true);
		writeReg(0x105, 0, true);
	}
	writeReg(0x08, 0, true);

	for (int offset = 0; offset <= (_opl3 ? 0x100 : 0); offset += 0x100) {
		for (int reg = 0x20; reg <= 0xF5; reg++)
			writeReg(offset + reg, 0, true);
	}

	initDualOpl2OnOpl3(_type);
}

void OPL::write(int portAddress, int value) {
	if (portAddress & 1) {
		writeReg(_activeReg, value);
	} else {
		if (_type == Config::kOpl2)
			_activeReg = value & 0xFF;
		else
			_activeReg = ((portAddress & 2) << 7) | (value & 0xFF);
	}
}

void OPL::writeReg(int reg, int value) {
	if (emulateDualOpl2OnOpl3(reg, value, _type))
		writeReg(reg, value, false);
}

void OPL::writeReg(int reg, int value, bool forcePort) {
	if (!_initialized)
		return;

	uint16 port = _base;
	if ((reg & 0x100) && (forcePort || _type != Config::kOpl2)) {
		if (!_opl3)
			return;
		port += 2;
	}

	_mutex.lock();
	rawWrite(port, _base, reg & 0xFF, value & 0xFF);
	_mutex.unlock();
}

::OPL::OPL *create(Config::OplType type) {
	return new OPL(type);
}

} // End of namespace DosOPL
} // End of namespace OPL
