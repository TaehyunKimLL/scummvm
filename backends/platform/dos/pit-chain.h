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

#ifndef BACKENDS_PLATFORM_DOS_PIT_CHAIN_H
#define BACKENDS_PLATFORM_DOS_PIT_CHAIN_H

#include "common/scummsys.h"

namespace DOS {

/** PIT channel 0 divisor for a ~1 kHz IRQ0 (1193182 Hz / 1193 ~= 1000.15 Hz). */
const uint32 kPitDivisor = 1193;

/** The resulting tick rate, truncated: 1193182 / 1193 == 1000 Hz. */
const uint32 kPitHz = 1193182 / kPitDivisor;

/** Free-running accumulator that decides when an IRQ0 tick should chain
 * to the original BIOS INT 8 handler (which itself only fires at
 * 18.2 Hz -- roughly every 55th 1 kHz tick). */
struct PitChain {
	uint32 acc = 0;
};

/**
 * Advances the accumulator by @p divisor (a PIT channel-0 divisor). When
 * it reaches or passes 65536 -- the PIT's own reload point -- it wraps
 * and this returns true, meaning: chain this tick to the BIOS's INT 8
 * handler (so BIOS-timed things like the day count keep working).
 * Otherwise it returns false, meaning: just acknowledge the interrupt
 * (EOI) and return.
 */
inline bool pitTick(PitChain &c, uint32 divisor = kPitDivisor) {
	c.acc += divisor;
	if (c.acc >= 65536) {
		c.acc -= 65536;
		return true;
	}
	return false;
}

} // End of namespace DOS

#endif
