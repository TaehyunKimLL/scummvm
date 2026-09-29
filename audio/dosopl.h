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

#ifndef AUDIO_DOSOPL_H
#define AUDIO_DOSOPL_H

#include "audio/fmopl.h"
#include "common/mutex.h"

namespace OPL {
namespace DosOPL {

/**
 * A real OPL2/OPL3 chip on the ISA bus of a DOS machine (AdLib, Sound
 * Blaster), programmed through port I/O.
 *
 * Dual OPL2 (Sound Blaster Pro 1) is not driven as two chips: kDualOpl2 is
 * accepted only when an OPL3 is present, whose two register banks stand in
 * for the two OPL2s.
 *
 * The RealChip timer callbacks may run inside the IRQ0 handler, so the
 * write path does port I/O only; each address/data pair is written under a
 * mutex so that a timer callback cannot split a pair written by the main
 * thread.
 */
class OPL : public ::OPL::OPL, public Audio::RealChip {
public:
	explicit OPL(Config::OplType type);
	~OPL() override;

	bool init() override;
	void reset() override;

	void write(int portAddress, int value) override;
	void writeReg(int reg, int value) override;

	/**
	 * Probes the hardware (once; the result is cached) and returns whether
	 * a chip able to serve @p type is present.
	 */
	static bool detect(Config::OplType type);

private:
	void writeReg(int reg, int value, bool forcePort);

	Config::OplType _type;
	int _activeReg;
	bool _initialized;
	uint16 _base;
	bool _opl3;

	Common::Mutex _mutex;
};

} // End of namespace DosOPL
} // End of namespace OPL

#endif
