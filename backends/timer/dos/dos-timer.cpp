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

#define FORBIDDEN_SYMBOL_EXCEPTION_time_h

#include "common/scummsys.h"

#if defined(DOS_DJGPP)

#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <time.h>
#include <stdlib.h>
#include <SDL3/SDL_timer.h>

#include "backends/timer/dos/dos-timer.h"
#include "backends/platform/dos/pit-chain.h"
#include "common/textconsole.h"
#include "common/util.h"

// DefaultTimerManager::handler() walks every due timer proc; running it on
// every 1 kHz tick would spend the CPU on an empty queue check most of the
// time. Every 4th tick (~4 ms) still places a 60 Hz proc (SCI's music,
// 16.667 ms) within 4 ms of its due time, and the OPL/MIDI drivers'
// callbacks are no finer than that.
static const uint32 kHandlerEvery = 4;

// One PIT tick is 1193 / 1193182 s = 1193000 / 1193182 ms.
static const uint32 kMsNum = DOS::kPitDivisor * 1000;
static const uint32 kMsDen = 1193182;

// A 32-bit protected-mode far pointer as `lcall *mem` reads it.
struct FarPtr32 {
	uint32 offset;
	uint16 selector;
} __attribute__((packed));

// Everything the interrupt handler touches that is not already on the
// heap, in one block so it is locked with one call.
struct IsrState {
	volatile uint32 ticks;		// IRQ0 ticks since install
	volatile uint32 millis;		// what getMillis() returns; 32-bit aligned, read in one load
	uint32 msAcc;				// ms remainder, in 1/kMsDen ms
	DOS::PitChain chain;
	FarPtr32 oldInt8;			// the BIOS INT 8 (as the DPMI host presents it)
	uint32 sinceHandler;
	volatile bool inHandler;
	DefaultTimerManager *timer;	// null until the manager is fully built
	byte fpu[108] __attribute__((aligned(4)));	// fnsave image
};
static IsrState g_isr;

static volatile bool g_installed = false;
static bool g_atexitDone = false;
static _go32_dpmi_seginfo g_oldVector, g_newVector;
static uclock_t g_uclockBase = 0;	// uclock() when the handler went in
static uclock_t g_uclockLast = 0;

static inline uint32 irqSave() {
	uint32 flags;
	__asm__ __volatile__("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
	return flags;
}
static inline void irqRestore(uint32 flags) {
	if (flags & 0x200)
		__asm__ __volatile__("sti" : : : "memory");
}

static inline void pitProgram(uint16 divisor) {
	// Mode 2 (rate generator), as DJGPP's uclock() left it: the count
	// falls linearly from the divisor to 1, which uclock() below reads.
	outportb(0x43, 0x34);
	outportb(0x40, divisor & 0xFF);
	outportb(0x40, divisor >> 8);
}

static inline uint16 pitRead() {
	outportb(0x43, 0x00);	// latch channel 0
	const uint16 lo = inportb(0x40);
	return lo | (inportb(0x40) << 8);
}

// Advances the clock by one PIT period.
static void tickClock() {
	g_isr.ticks++;
	g_isr.msAcc += kMsNum;
	if (g_isr.msAcc >= kMsDen) {	// kMsNum < kMsDen: at most once per tick
		g_isr.msAcc -= kMsDen;
		g_isr.millis++;
	}
}

// Entered through the IRET wrapper with interrupts off, and they stay off:
// nothing here or in the timer procs may enable them (the wrapper's stack
// is not reentrant).
static void timerIsr() {
	tickClock();
	if (DOS::pitTick(g_isr.chain)) {
		// The old handler (reflected to the BIOS) bumps 0040:006C, calls
		// INT 1Ch and sends its own EOI. pushfl + lcall build the frame
		// its IRET pops.
		__asm__ __volatile__("pushfl; lcall *%0" : : "m"(g_isr.oldInt8) : "memory", "cc");
	} else {
		outportb(0x20, 0x20);
	}

	if (++g_isr.sinceHandler < kHandlerEvery || g_isr.inHandler || !g_isr.timer)
		return;
	// A tick that arrives while a timer proc runs (only if something turned
	// interrupts on) just counts: handler() catches up from getMillis().
	g_isr.sinceHandler = 0;
	g_isr.inHandler = true;
	// Timer procs may use the FPU; the code we interrupted may be in the
	// middle of an x87 computation. fnsave also leaves the FPU initialised.
	__asm__ __volatile__("fnsave %0" : "=m"(g_isr.fpu) : : "memory");
	g_isr.timer->handler();
	__asm__ __volatile__("frstor %0" : : "m"(g_isr.fpu) : "memory");
	g_isr.inHandler = false;
}
static void timerIsrEnd() {}

// DJGPP's own uclock(), which ours falls back on before the handler is in.
extern "C" uclock_t __uclock(void);

/*
 * DJGPP's uclock() (under SDL_GetTicks(), SDL_Delay() and SDL's audio
 * timeouts) adds the PIT's count to the BIOS tick at 0040:006C, assuming a
 * divisor of 65536. At divisor 1193 that sum only moves once per BIOS tick,
 * so SDL_Delay(1) would wait up to 55 ms. While our handler is installed,
 * this one counts our 1 kHz ticks plus the PIT's count within the tick
 * instead, in the same units (UCLOCKS_PER_SEC == the PIT's input clock).
 */
extern "C" uclock_t uclock(void) {
	if (!g_installed)
		return __uclock();
	uint32 t1, t2;
	uint16 count;
	do {
		t1 = g_isr.ticks;
		const uint32 flags = irqSave();
		count = pitRead();
		irqRestore(flags);
		t2 = g_isr.ticks;
	} while (t1 != t2);
	if (count == 0 || count > DOS::kPitDivisor)
		count = DOS::kPitDivisor;
	uclock_t rv = g_uclockBase + (uclock_t)t1 * DOS::kPitDivisor + (DOS::kPitDivisor - count);
	// The count reloads a moment before its IRQ0 is served (or long before,
	// with interrupts off): never go back.
	if (rv < g_uclockLast)
		rv = g_uclockLast;
	g_uclockLast = rv;
	return rv;
}

// Idempotent: from the destructor and from exit() alike.
static void teardown() {
	if (!g_installed)
		return;
	const uint32 flags = irqSave();
	pitProgram(0);		// 65536: the BIOS's 18.2 Hz
	_go32_dpmi_set_protected_mode_interrupt_vector(8, &g_oldVector);
	g_installed = false;
	g_isr.timer = nullptr;
	irqRestore(flags);
	_go32_dpmi_free_iret_wrapper(&g_newVector);
}

static bool install() {
	if (g_installed)
		return true;
	{
		const uintptr fns[3] = { (uintptr)tickClock, (uintptr)timerIsr, (uintptr)timerIsrEnd };
		const uintptr lo = MIN(fns[0], MIN(fns[1], fns[2]));
		const uintptr hi = MAX(fns[0], MAX(fns[1], fns[2]));
		_go32_dpmi_lock_code((void *)lo, hi - lo + 256);
	}
	// All of the image and every later sbrk() is locked as well
	// (_CRT0_FLAG_LOCK_MEMORY stays set; see main()): handler() and the
	// timer procs run from here, touching the heap.
	_go32_dpmi_lock_data(&g_isr, sizeof(g_isr));
	_go32_dpmi_lock_data(const_cast<bool *>(&g_installed), sizeof(g_installed));

	// Its first call reprograms the PIT (mode 2, 65536); make sure that
	// has happened before ours.
	const uclock_t base = __uclock();

	_go32_dpmi_get_protected_mode_interrupt_vector(8, &g_oldVector);
	g_isr.oldInt8.offset = g_oldVector.pm_offset;
	g_isr.oldInt8.selector = g_oldVector.pm_selector;
	g_isr.ticks = 0;
	g_isr.msAcc = 0;
	g_isr.chain = DOS::PitChain();
	g_isr.sinceHandler = 0;
	g_isr.inHandler = false;
	g_isr.timer = nullptr;

	// Timer procs run on the wrapper's own stack; SCI's music code is
	// deeper than the 32 KB default comfortably allows.
	const unsigned long oldStack = _go32_interrupt_stack_size;
	_go32_interrupt_stack_size = 64 * 1024;
	g_newVector.pm_offset = (unsigned long)timerIsr;
	g_newVector.pm_selector = _go32_my_cs();
	const int err = _go32_dpmi_allocate_iret_wrapper(&g_newVector);
	_go32_interrupt_stack_size = oldStack;
	if (err) {
		warning("DOS: no IRET wrapper for IRQ0; timers run from the event loop");
		return false;
	}

	const uint32 startMillis = (uint32)SDL_GetTicks();	// getMillis() until now
	const uint32 flags = irqSave();
	g_uclockBase = base;
	g_uclockLast = base;
	g_isr.millis = startMillis;
	_go32_dpmi_set_protected_mode_interrupt_vector(8, &g_newVector);
	pitProgram(DOS::kPitDivisor);
	g_installed = true;
	irqRestore(flags);

	if (!g_atexitDone) {
		atexit(teardown);
		g_atexitDone = true;
	}
	return true;
}

DosTimerManager::DosTimerManager() {
	if (install())
		g_isr.timer = this;	// only now may the handler call handler()
}

DosTimerManager::~DosTimerManager() {
	// Before DefaultTimerManager's destructor frees the timer slots.
	teardown();
}

bool DosTimerManager::installed() {
	return g_installed;
}

uint32 DosTimerManager::millis() {
	return g_isr.millis;
}

bool DosTimerManager::interruptsEnabled() {
	uint32 flags;
	__asm__ __volatile__("pushfl; popl %0" : "=r"(flags));
	return (flags & 0x200) != 0;
}

void DosTimerManager::spinMillis(uint msecs) {
	// The count falls from 1193 to 1 once a millisecond; each time it goes
	// up again, one period has passed. A port read takes about a
	// microsecond, so no period slips by unseen.
	uint wraps = 0;
	uint16 prev = pitRead();
	while (wraps < msecs) {
		const uint16 c = pitRead();
		if (c > prev)
			wraps++;
		prev = c;
	}
	// The PIC holds one IRQ0 back for when interrupts come on again, and
	// that one counts itself; the other periods would be lost.
	const uint32 flags = irqSave();
	for (uint i = 1; i < wraps; ++i)
		tickClock();
	irqRestore(flags);
}

#endif
