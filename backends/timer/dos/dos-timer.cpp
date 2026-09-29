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
#include <sys/farptr.h>
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
static uint32 g_isrDelayBlocked = 0;	// delayInHandler() found IRQ0 in service
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

// IRQ0's bit in the master PIC's mask register (port 0x21). Masking holds
// an IRQ0 back without losing it: the PIC still latches the request, and
// delivers it once the bit is clear again and interrupts are on.
static inline uint8 maskIrq0() {
	const uint8 m = inportb(0x21);
	outportb(0x21, m | 0x01);
	return m & 0x01;
}
// Puts back bit 0 alone, so a change to the other IRQs' bits made in the
// meantime (by a real-mode handler in the BIOS chain, say) stays.
static inline void restoreIrq0(uint8 wasMasked) {
	const uint8 m = inportb(0x21);
	outportb(0x21, (m & ~0x01) | wasMasked);
}

// The master PIC's in-service register (OCW3 0x0B selects it, 0x0A puts
// the IRR back as the default read).
static inline uint8 picInService() {
	outportb(0x20, 0x0B);
	const uint8 isr = inportb(0x20);
	outportb(0x20, 0x0A);
	return isr;
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
		//
		// IRQ0 is masked at the PIC while it runs. Real-mode code in that
		// chain may turn interrupts on after the BIOS's EOI -- the BIOS
		// itself runs INT 1Ch with interrupts on, and a TSR hooked on INT 8
		// commonly does its work after chaining to the BIOS. A tick that
		// came in then would enter the IRET wrapper while it is busy, which
		// returns at once without an EOI: IRQ0, and every IRQ below it,
		// would stay in service for good. Masked, the PIC latches that
		// tick instead and delivers it after our IRET, where it counts
		// itself as a tick that came in with interrupts off would (a chain
		// that ran over 1 ms still loses the periods after the first, as
		// any long interrupts-off stretch does).
		const uint8 wasMasked = maskIrq0();
		__asm__ __volatile__("pushfl; lcall *%0" : : "m"(g_isr.oldInt8) : "memory", "cc");
		restoreIrq0(wasMasked);
	} else {
		outportb(0x20, 0x20);
	}

	// IRQ0 is out of service at the PIC from here on: the EOI above, or
	// the BIOS's own in the chain. delayInHandler() relies on that.
	if (++g_isr.sinceHandler < kHandlerEvery || g_isr.inHandler || !g_isr.timer)
		return;
	// Interrupts must stay off through handler(): a tick that came in now
	// would never reach timerIsr -- DJGPP's IRET wrapper returns at once,
	// without an EOI, from an entry nested in its own, and IRQ0 and every
	// lower-priority IRQ would stay in service for good. Hence no DOS calls
	// in timer procs (OSystem_DOS::logMessage defers its file writes while
	// interrupts are off). The one place that turns them on is
	// delayInHandler(), with IRQ0 masked at the PIC. inHandler is only a
	// backstop.
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

	// getMillis() until now (OSystem_DOS's fallback): uclock() in ms,
	// scaled the same way.
	const uint32 startMillis = (uint32)((uint64)base * 1000 / UCLOCKS_PER_SEC);
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
	//
	// Interrupts may be on here (delayInHandler(), IRQ0 masked), so each
	// read is made with them off: no handler comes between the latch and
	// the two bytes. A handler that ran for over 1 ms between two reads
	// would hide a period; the ones that can (the COM, keyboard and Sound
	// Blaster handlers) take microseconds.
	uint wraps = 0;
	uint32 flags = irqSave();
	uint16 prev = pitRead();
	irqRestore(flags);
	while (wraps < msecs) {
		flags = irqSave();
		const uint16 c = pitRead();
		irqRestore(flags);
		if (c > prev)
			wraps++;
		prev = c;
	}
	// The PIC holds one IRQ0 back (interrupts off, or IRQ0 masked) for
	// when it can be delivered, and that one counts itself; the other
	// periods would be lost. Their BIOS
	// ticks are counted here too (chaining to INT 8 outside the handler
	// would send a stray EOI): 0040:006C, wrapping at midnight.
	flags = irqSave();
	for (uint i = 1; i < wraps; ++i) {
		tickClock();
		if (DOS::pitTick(g_isr.chain)) {
			uint32 bios = _farpeekl(_dos_ds, 0x46C) + 1;
			if (bios >= 0x1800B0) {
				bios = 0;
				_farpokeb(_dos_ds, 0x470, 1);
			}
			_farpokel(_dos_ds, 0x46C, bios);
		}
	}
	irqRestore(flags);
}

bool DosTimerManager::inHandler() {
	return g_isr.inHandler;
}

void DosTimerManager::delayInHandler(uint msecs) {
	// A timer proc that waits (SCI's MT-32 driver, ~46 ms after a SysEx)
	// would otherwise hold every interrupt off for the whole wait: the
	// Sound Blaster's, the keyboard's, the COM port's. Only IRQ0 must not
	// come in -- it would nest in the busy IRET wrapper (see timerIsr()) --
	// so it alone is masked, and interrupts go on for the wait. Nothing
	// else can run into the timer procs' state meanwhile: the main thread
	// is the code this interrupt stopped, and the other handlers (the
	// debug socket's COM, SDL3's keyboard and Sound Blaster) touch only
	// their own locked buffers.
	//
	// IRQ0 is no longer in service here (timerIsr() sends or chains the
	// EOI before handler() runs), so the IRQs below it get through; had it
	// still been, they would stay blocked and the wait would be the old
	// interrupts-off one, which is what happens then.
	const uint8 wasMasked = maskIrq0();
	if (picInService() & 0x01) {
		g_isrDelayBlocked++;
		spinMillis(msecs);
	} else {
		__asm__ __volatile__("sti" : : : "memory");
		spinMillis(msecs);
		__asm__ __volatile__("cli" : : : "memory");
	}
	restoreIrq0(wasMasked);
	// An IRQ0 that came in during the wait is latched and delivered after
	// the handler's IRET; spinMillis() credited the other periods.
}

uint32 DosTimerManager::delaysBlocked() {
	return g_isrDelayBlocked;
}

void DosTimerManager::shutdown() {
	teardown();
}

#endif
