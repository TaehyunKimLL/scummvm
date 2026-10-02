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
 * with interrupts off, IRQ0 masked and the FPU state saved around it --
 * but only on a tick that came in our protected-mode code, as the timer
 * procs' memory is pageable (see timerIsr()); a tick that came in real
 * mode leaves them for the next one, or for the end of the real-mode call.
 * Where the handler cannot tell (another host, or frames that do not look
 * as calibrated), everything is locked (DOS::lockAll()) and every due tick
 * runs them; if not everything could be locked, they run on the main
 * thread only. With a TSC, the ticks that come while the procs run (IRQ0
 * masked) are counted and credited afterwards, BIOS calls included.
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

	/** True while a timer proc runs, i.e. inside the IRQ0 handler. */
	static bool inHandler();

	/** Where the timer procs that run now were called from. */
	enum ProcsContext {
		kProcsNone,			// none run
		kProcsInIrq0,		// the IRQ0 handler
		kProcsAfterCall		// the main thread, as a real-mode call returned
	};
	static ProcsContext procsContext();

	/**
	 * True when the IRQ0 handler does not run the timer procs (it is not
	 * in, or the DPMI host hands interrupts over in a way it cannot read),
	 * so that the event loop must.
	 */
	static bool procsOnMainThread();

	/**
	 * The event loop is about to run the timer procs (procsOnMainThread()):
	 * counts the run and the time they waited, as runs elsewhere count
	 * theirs, for logStats().
	 */
	static void noteEventLoopRun();

	/** Logs (debug level 1) where the timer procs ran, and how often they waited. */
	static void logStats();

	/**
	 * delayMillis() for a timer proc: waits @p msecs by polling the PIT,
	 * with IRQ0 masked at the PIC and interrupts on, so the other IRQs
	 * are served during the wait. Called with interrupts off, returns
	 * with them off.
	 */
	static void delayInHandler(uint msecs);

	/** Times delayInHandler() found IRQ0 still in service (and waited with interrupts off). */
	static uint32 delaysBlocked();

	/**
	 * Takes the handler out (PIT and vector back) ahead of the destructor,
	 * for quit() and fatalError(). Idempotent.
	 */
	static void shutdown();

	/**
	 * shutdown() without freeing anything, for an exit that skipped it (a
	 * fatal signal, a direct _exit()): the heap may not be usable there.
	 * Idempotent.
	 */
	static void stopHardware();

	/** True if interrupts are enabled (EFLAGS.IF). */
	static bool interruptsEnabled();
};

#endif
