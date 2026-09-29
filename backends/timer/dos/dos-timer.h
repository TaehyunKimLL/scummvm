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

#ifndef BACKENDS_TIMER_DOS_H
#define BACKENDS_TIMER_DOS_H

#include "backends/timer/default/default-timer.h"

/**
 * Preemptive timers for DOS. The constructor hooks IRQ0 with a protected-
 * mode handler and runs PIT channel 0 at divisor 1193 (~1 kHz). Each tick
 * advances the millisecond clock getMillis() reads; about every 55th tick
 * is chained to the BIOS's INT 8 handler (so the BIOS clock and anything
 * on INT 1Ch keep their 18.2 Hz), the others just get their EOI. Every
 * kHandlerEvery ticks the handler runs DefaultTimerManager::handler(),
 * with interrupts off and the FPU state saved around it.
 *
 * Teardown (PIT back to its default rate, the old vector back) runs from
 * the destructor and from exit() alike, and only once.
 */
class DosTimerManager : public DefaultTimerManager {
public:
	DosTimerManager();
	~DosTimerManager() override;

	/** True while our IRQ0 handler is installed. */
	static bool installed();

	/** Milliseconds from the IRQ0 tick count (monotonic, ISR-safe). */
	static uint32 millis();

	/**
	 * Waits @p msecs by polling the PIT, for callers that hold interrupts
	 * off (a timer proc, or code under a Common::Mutex), where the tick
	 * count cannot advance. The ticks that pass are credited to millis().
	 */
	static void spinMillis(uint msecs);

	/** True if interrupts are enabled (EFLAGS.IF). */
	static bool interruptsEnabled();
};

#endif
