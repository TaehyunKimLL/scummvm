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
#define FORBIDDEN_SYMBOL_EXCEPTION_getenv

#include "common/scummsys.h"

#if defined(DOS_DJGPP)

#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <sys/exceptn.h>
#include <sys/farptr.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL_timer.h>

#include "backends/timer/dos/dos-timer.h"
#include "backends/platform/dos/dos-irq.h"
#include "backends/platform/dos/pit-chain.h"
#include "common/debug.h"
#include "common/str.h"
#include "common/textconsole.h"
#include "common/util.h"

DOS_IRQ_CODE_RANGE(timer);

// DefaultTimerManager::handler() walks every due timer proc; running it on
// every 1 kHz tick would spend the CPU on an empty queue check most of the
// time. Every 4th tick (~4 ms) still places a 60 Hz proc (SCI's music,
// 16.667 ms) within 4 ms of its due time, and the OPL/MIDI drivers'
// callbacks are no finer than that.
static const uint32 kHandlerEvery = 4;

// One PIT tick is 1193 / 1193182 s = 1193000 / 1193182 ms.
static const uint32 kMsNum = DOS::kPitDivisor * 1000;
static const uint32 kMsDen = 1193182;

// handler() run time histogram bounds, in ms (logStats()).
static const uint32 kStallMs[6] = { 1, 2, 5, 20, 50, 200 };

// The longest handler() run the catch-up accounts for: past that the
// ticks are lost as before.
static const uint32 kCatchUpMaxTicks = 2000;

// Ticks the handler samples at install to learn how CWSDPMI hands it an
// interrupt that came in our protected-mode code.
static const uint32 kCalibrationTicks = 8;

// A 32-bit protected-mode far pointer as `lcall *mem` reads it.
struct FarPtr32 {
	uint32 offset;
	uint16 selector;
} __attribute__((packed));

// Which interrupts may run the timer procs. They touch pageable code and
// data, and CWSDPMI does not service a page fault in a hardware interrupt
// that came in real mode (DOS may be busy then): the program dies.
enum ProcsMode {
	kProcsCalibrating,	// none yet
	kProcsFromOurCode,	// those that came in our protected-mode code
	kProcsAlways,		// all: everything is locked (DOS::lockedAll())
	kProcsOnMainThread	// none: pollEvent() runs them
};

// Everything the interrupt handler touches before it may run the timer
// procs, in one block so it is locked with one call.
struct IsrState {
	volatile uint32 ticks;		// IRQ0 ticks since install
	volatile uint32 millis;		// what getMillis() returns; 32-bit aligned, read in one load
	uint32 msAcc;				// ms remainder, in 1/kMsDen ms
	DOS::PitChain chain;
	FarPtr32 oldInt8;			// the BIOS INT 8 (as the DPMI host presents it)
	uint32 sinceHandler;
	volatile bool inHandler;
	volatile uint8 procsCtx;	// where the running procs were called from: DosTimerManager::ProcsContext
	volatile uint8 procsMode;	// ProcsMode
	DefaultTimerManager *timer;	// null until the manager is fully built
	// What an interrupt from our protected-mode code looks like (see
	// readFrame()): our CS and SS, and CWSDPMI's return stub.
	uint16 ourCs, ourSs;
	uint32 stubEip;
	uint16 stubCs;
	volatile uint32 calSeen, calMatched;
	// Counters, logged by logStats().
	volatile uint32 procRuns;		// handler() calls
	volatile uint32 procRunsAfterCall;	// ... of them as a real-mode call returned
	volatile uint32 procDeferred;	// due ticks that came in real mode or the host
	volatile uint32 maxWait;		// the most ticks the procs waited past due
	volatile uint32 waits10;		// runs that waited 10 ticks or more
	// Catch-up for the ticks lost while the procs run (IRQ0 masked, or
	// interrupts off): see runProcs(). 0 without a TSC, or before it is
	// calibrated (calibrateTsc()).
	uint32 tscPerTick;			// TSC counts per PIT period
	uint32 tscCap;				// the longest run it accounts for, in TSC counts
	volatile uint32 biosOwed;	// BIOS INT 8 calls owed for credited ticks
	volatile uint32 ticksCredited;	// ticks credited so far
	volatile uint32 biosTicks;	// BIOS ticks given: INT 8 calls, and spinMillis()'s own
	// handler() run times in IRQ0, by kStallMs bin, and the longest.
	uint32 stallTsc[6];			// kStallMs in TSC counts
	volatile uint32 stalls[7];
	volatile uint32 stallMax;	// TSC counts
	byte fpu[108] __attribute__((aligned(4)));	// fnsave image
};
static IsrState g_isr;

static volatile bool g_installed = false;
static uint32 g_isrDelayBlocked = 0;	// delayInHandler() found IRQ0 in service
static bool g_atexitDone = false;
static _go32_dpmi_seginfo g_oldVector, g_newVector;
static uclock_t g_uclockBase = 0;	// uclock() when the handler went in
static uclock_t g_uclockLast = 0;

static inline __attribute__((always_inline)) uint32 irqSave() {
	uint32 flags;
	__asm__ __volatile__("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
	return flags;
}
static inline __attribute__((always_inline)) void irqRestore(uint32 flags) {
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
static inline __attribute__((always_inline)) uint8 maskIrq0() {
	const uint8 m = DOS::irqIn8(0x21);
	DOS::irqOut8(0x21, m | 0x01);
	return m & 0x01;
}
// Puts back bit 0 alone, so a change to the other IRQs' bits made in the
// meantime (by a real-mode handler in the BIOS chain, say) stays.
static inline __attribute__((always_inline)) void restoreIrq0(uint8 wasMasked) {
	const uint8 m = DOS::irqIn8(0x21);
	DOS::irqOut8(0x21, (m & ~0x01) | wasMasked);
}

// The master PIC's in-service register (OCW3 0x0B; 0x0A puts the request
// register back as the default read).
static inline __attribute__((always_inline)) uint8 picRead(uint8 ocw3) {
	DOS::irqOut8(0x20, ocw3);
	const uint8 r = DOS::irqIn8(0x20);
	DOS::irqOut8(0x20, 0x0A);
	return r;
}

// Advances the clock by one PIT period.
static inline __attribute__((always_inline)) void tickClock() {
	g_isr.ticks++;
	g_isr.msAcc += kMsNum;
	if (g_isr.msAcc >= kMsDen) {	// kMsNum < kMsDen: at most once per tick
		g_isr.msAcc -= kMsDen;
		g_isr.millis++;
	}
}

// The PIT's count, from IRQ code.
static inline __attribute__((always_inline)) uint16 irqPitRead() {
	DOS::irqOut8(0x43, 0x00);	// latch channel 0
	const uint16 lo = DOS::irqIn8(0x40);
	return lo | (DOS::irqIn8(0x40) << 8);
}

// n / d for a quotient that fits 32 bits (the caller makes sure), inline:
// GCC calls libgcc's __udivdi3 for a 64-bit division, which is not locked.
static inline __attribute__((always_inline)) uint32 irqDiv64(uint64 n, uint32 d) {
	uint32 q, r;
	__asm__("divl %4" : "=a"(q), "=d"(r) : "a"((uint32)n), "d"((uint32)(n >> 32)), "rm"(d) : "cc");
	return q;
}

// Credits @p n ticks that never reached the handler to the clock, and
// owes the BIOS the INT 8 calls they would have made (timerIsr() pays
// those back one per tick).
static inline __attribute__((always_inline)) void creditTicks(uint32 n) {
	g_isr.ticks += n;
	// kMsNum < kMsDen: n * kMsNum stays well under 2^32 for n <= kCatchUpMaxTicks.
	g_isr.msAcc += n * kMsNum;
	g_isr.millis += g_isr.msAcc / kMsDen;
	g_isr.msAcc %= kMsDen;
	g_isr.chain.acc += n * DOS::kPitDivisor;
	g_isr.biosOwed += g_isr.chain.acc >> 16;
	g_isr.chain.acc &= 0xFFFF;
	g_isr.ticksCredited += n;
}

// The bytes of the frames readFrame() reads, from the saved ESP.
static const uint32 kFrameBytes = 80;

/*
 * Whether this interrupt came in our own protected-mode code, from the
 * frames below the IRET wrapper's saved SS:ESP: the wrapper's pushes
 * (pusha, then DS ES FS GS: 48 bytes), then CWSDPMI's IRET frame into its
 * return stub (EIP CS EFLAGS), then the frame the CPU pushed when the
 * interrupt took our ring 3 code to ring 0 (EIP CS EFLAGS ESP SS): what
 * CWSDPMI r7's irq_common builds (see DOS::chooseLockRegime() for the
 * source). An interrupt that came in real mode returns through rmcb_task
 * instead, and has its saved registers where the ring 3 frame would be.
 *
 * The stack the frames lie on is the host's, or another handler's: its
 * limit is checked before the read (a read past it would fault in here).
 * Returns false if they do not fit below it.
 */
static inline __attribute__((always_inline)) bool readFrame(uint32 esp, uint16 ss,
		uint32 &stubEip, uint16 &stubCs, uint16 &cs, uint16 &stackSs) {
	uint32 limit;
	uint8 valid;
	__asm__("lsll %2, %0\n\tsetz %1" : "=r"(limit), "=q"(valid) : "r"((uint32)ss) : "cc");
	if (!valid || limit < kFrameBytes - 1 || esp > limit - (kFrameBytes - 1))
		return false;
	stubEip = DOS::irqPeek32(ss, esp + 48);
	stubCs = (uint16)DOS::irqPeek32(ss, esp + 52);
	cs = (uint16)DOS::irqPeek32(ss, esp + 64);
	stackSs = (uint16)DOS::irqPeek32(ss, esp + 76);
	return true;
}

static inline __attribute__((always_inline)) bool mayRunProcs(uint32 esp, uint16 ss) {
	if (g_isr.procsMode == kProcsAlways)
		return true;
	if (g_isr.procsMode != kProcsFromOurCode)
		return false;
	uint32 stubEip;
	uint16 stubCs, cs, stackSs;
	if (!readFrame(esp, ss, stubEip, stubCs, cs, stackSs))
		return false;
	return stubEip == g_isr.stubEip && stubCs == g_isr.stubCs && cs == g_isr.ourCs && stackSs == g_isr.ourSs;
}

// While the main thread spins in protected mode at install: every tick
// came in our code; the stub is what the first one returns through.
static inline __attribute__((always_inline)) void calibrate(uint32 esp, uint16 ss) {
	uint32 stubEip;
	uint16 stubCs, cs, stackSs;
	const bool read = readFrame(esp, ss, stubEip, stubCs, cs, stackSs);
	if (g_isr.calSeen++ == 0 && read) {
		g_isr.stubEip = stubEip;
		g_isr.stubCs = stubCs;
	}
	if (read && stubEip == g_isr.stubEip && stubCs == g_isr.stubCs && cs == g_isr.ourCs && stackSs == g_isr.ourSs)
		g_isr.calMatched++;
}

// The one call into the timer procs, out of line so that it stays one
// call site (irqcheck.py allows one such indirect call) however runProcs()
// is inlined.
static DOS_IRQ_CODE __attribute__((noinline)) void callHandler() {
	g_isr.timer->handler();
}

// Runs the timer procs (DefaultTimerManager::handler()), with interrupts
// off: from timerIsr(), and on the main thread as a real-mode call returns
// (runDeferredProcs()).
//
// Interrupts must stay off through handler(): a tick that came in now would
// never reach timerIsr -- DJGPP's IRET wrapper returns at once, without an
// EOI, from an entry nested in its own, and IRQ0 and every lower-priority
// IRQ would stay in service for good. Hence no DOS calls in timer procs
// (OSystem_DOS::logMessage defers its file writes while interrupts are
// off). The one place that turns them on is delayInHandler(), with IRQ0
// masked at the PIC. CWSDPMI may turn them on as well, in real mode, to
// page in for a proc: IRQ0 is masked for the whole of handler() so that
// this cannot nest it either. The other IRQs are served meanwhile; their
// handlers touch only their own locked memory. inHandler is only a
// backstop.
//
// The ticks that came while handler() ran are lost to the PIC (it latches
// one, which counts itself once delivered): a page fault in a proc holds
// IRQ0 masked for tens of ms. With a TSC they are counted here instead --
// the PIT periods that began during the run, from the TSC and the PIT's
// count on both sides -- and credited to the clock and the BIOS chain, so
// that the clock does not fall behind and DefaultTimerManager runs the
// missed proc intervals on its next pass. spinMillis() credits its own.
static inline __attribute__((always_inline)) void runProcs(uint8 ctx) {
	const uint32 waited = g_isr.sinceHandler - kHandlerEvery;
	if (waited > g_isr.maxWait)
		g_isr.maxWait = waited;
	if (waited >= 10)
		g_isr.waits10++;
	g_isr.sinceHandler = 0;
	g_isr.inHandler = true;
	g_isr.procsCtx = ctx;
	const uint8 wasMasked = maskIrq0();
	// Timer procs may use the FPU; the code we interrupted may be in the
	// middle of an x87 computation. fnsave also leaves the FPU initialised.
	__asm__ __volatile__("fnsave %0" : "=m"(g_isr.fpu) : : "memory");
	const uint32 tscPerTick = g_isr.tscPerTick;
	uint64 tsc0 = 0;
	uint16 pit0 = 0;
	uint32 ticks0 = 0;
	if (tscPerTick) {
		ticks0 = g_isr.ticks;
		pit0 = irqPitRead();
		tsc0 = DOS::irqRdtsc();
	}
	callHandler();
	if (tscPerTick) {
		const uint64 d = DOS::irqRdtsc() - tsc0;
		const uint16 pit1 = irqPitRead();
		const uint32 elapsed = d > g_isr.tscCap ? g_isr.tscCap : (uint32)d;
		// The PIT's input clocks that passed, from the TSC (the quotient
		// fits: elapsed <= kCatchUpMaxTicks periods), and the periods
		// begun meanwhile: the count falls from the divisor to 1 and
		// reloads, so its position on both sides makes the sum a whole
		// number of periods, give or take the TSC's error, which the
		// rounding takes out.
		const int32 clocks = (int32)irqDiv64((uint64)elapsed * DOS::kPitDivisor, tscPerTick) + (int32)pit1 - (int32)pit0;
		const uint32 periods = clocks <= 0 ? 0 : ((uint32)clocks + DOS::kPitDivisor / 2) / DOS::kPitDivisor;
		const uint32 credited = g_isr.ticks - ticks0;	// spinMillis()'s
		if (periods > credited + 1)
			creditTicks(periods - credited - 1);
		if (ctx == DosTimerManager::kProcsInIrq0) {
			uint bin = 0;
			while (bin < ARRAYSIZE(g_isr.stallTsc) && elapsed >= g_isr.stallTsc[bin])
				bin++;
			g_isr.stalls[bin]++;
			if (elapsed > g_isr.stallMax)
				g_isr.stallMax = elapsed;
		}
	}
	__asm__ __volatile__("frstor %0" : : "m"(g_isr.fpu) : "memory");
	g_isr.procRuns++;
	// A tick that came in meanwhile is latched, and delivered after our
	// IRET; any after it are lost to the clock, as before (interrupts were
	// off). Reading the PIC's request register here to count them made
	// DOSBox-X run KQ1's title differently (a palette entry off in M0).
	restoreIrq0(wasMasked);
	g_isr.procsCtx = DosTimerManager::kProcsNone;
	g_isr.inHandler = false;
}

// Entered through the IRET wrapper with interrupts off, and they stay off:
// nothing here or in the timer procs may enable them (the wrapper's stack
// is not reentrant). The wrapper calls this with the interrupted SS:ESP
// as its two stack arguments.
static DOS_IRQ_CODE void timerIsr(uint32 savedEsp, uint32 savedSs) {
	tickClock();
	bool chain = DOS::pitTick(g_isr.chain);
	if (!chain && g_isr.biosOwed) {
		// A tick credited by runProcs()'s catch-up: one owed call a tick.
		g_isr.biosOwed--;
		chain = true;
	}
	if (chain) {
		g_isr.biosTicks++;
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
		DOS::irqOut8(0x20, 0x20);
	}

	if (g_isr.procsMode == kProcsCalibrating) {
		calibrate(savedEsp, (uint16)savedSs);
		return;
	}
	// IRQ0 is out of service at the PIC from here on: the EOI above, or
	// the BIOS's own in the chain. delayInHandler() relies on that.
	if (++g_isr.sinceHandler < kHandlerEvery || g_isr.inHandler || !g_isr.timer)
		return;
	// The procs touch pageable memory. Unless everything is locked, they
	// run only in an interrupt that came in our protected-mode code:
	// CWSDPMI services a page fault there (DOS is not busy: the main
	// thread is in none of its calls). One that came in real mode -- a DOS
	// call, a page-in of the main thread's -- leaves them due (sinceHandler
	// stays where it is) for the next tick that did not, or for the end of
	// that call (runDeferredProcs()).
	if (!mayRunProcs(savedEsp, (uint16)savedSs)) {
		g_isr.procDeferred++;
		return;
	}
	runProcs(DosTimerManager::kProcsInIrq0);
}

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

/*
 * DJGPP's delay() (SDL_Delay()'s sleep) waits in the BIOS (INT 15h 86h),
 * in real mode, where an IRQ0 may not run the timer procs (see
 * timerIsr()): a game that sleeps between frames would hold its music
 * back. This one waits in protected mode.
 */
extern "C" void delay(unsigned msecs) {
	if (!g_installed) {
		const uclock_t end = __uclock() + (uclock_t)msecs * UCLOCKS_PER_SEC / 1000;
		while (__uclock() < end)
			;
		return;
	}
	if (!DosTimerManager::interruptsEnabled()) {
		DosTimerManager::spinMillis(msecs);
		return;
	}
	const uint32 start = g_isr.millis;
	while (g_isr.millis - start < msecs)
		__asm__ __volatile__("" : : : "memory");
}

// The main stack's bounds (crt0).
extern "C" unsigned int __djgpp_stack_limit, __djgpp_stack_top;

// The timer procs' stack on the main thread: one of their own, as in the
// handler (the IRET wrapper's).
static byte g_procStack[64 * 1024] __attribute__((aligned(16)));

static void runProcsCall() {
	runProcs(DosTimerManager::kProcsAfterCall);
}

/*
 * Timer procs that came due while the main thread was in real mode run as
 * the call returns, where a tick a moment later would have run them: a DOS
 * file read is a string of calls of a few ms each with only moments in
 * protected mode between them, which ticks seldom hit, and room loads held
 * the music up by tens of ms. As in the handler: interrupts off, a stack
 * of their own, and the segment registers the caller had (the IRET wrapper
 * restores them all; a caller may keep a farptr.h selector in FS across
 * the call). Only with interrupts on (not under a mutex, not in a timer
 * proc) and on the main thread (SDL3's threads have stacks of their own).
 */
static void runDeferredProcs() {
	if ((g_isr.procsMode != kProcsFromOurCode && g_isr.procsMode != kProcsOnMainThread) ||
		!DosTimerManager::interruptsEnabled())
		return;
	uint32 esp;
	__asm__ __volatile__("movl %%esp, %0" : "=r"(esp));
	if (esp < __djgpp_stack_limit || esp > __djgpp_stack_top)
		return;
	const uint32 flags = irqSave();
	if (g_isr.sinceHandler >= kHandlerEvery && !g_isr.inHandler && g_isr.timer) {
		g_isr.procRunsAfterCall++;
		__asm__ __volatile__(
			"pushl %%es\n\tpushl %%fs\n\tpushl %%gs\n\t"
			"movl %%esp, %%ebx\n\tmovl %0, %%esp\n\t"
			"call *%1\n\t"
			"movl %%ebx, %%esp\n\tpopl %%gs\n\tpopl %%fs\n\tpopl %%es"
			: : "S"(g_procStack + sizeof(g_procStack)), "D"(runProcsCall)
			: "eax", "ebx", "ecx", "edx", "memory", "cc");
	}
	irqRestore(flags);
}

// Every real-mode call libc makes for us (DOS, the BIOS, the mouse driver)
// goes through __dpmi_int() (-Wl,--wrap, see module.mk).
extern "C" int __real___dpmi_int(int vector, __dpmi_regs *regs);
extern "C" int __wrap___dpmi_int(int vector, __dpmi_regs *regs) {
	const int rv = __real___dpmi_int(vector, regs);
	if (g_isr.sinceHandler >= kHandlerEvery)
		runDeferredProcs();
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
	// An exit from inside the timer procs (a crash, Ctrl-C on the main
	// thread's call) leaves IRQ0 masked: the BIOS clock would stop in DOS.
	outportb(0x21, inportb(0x21) & ~0x01);
	g_isr.inHandler = false;
	g_isr.procsCtx = DosTimerManager::kProcsNone;
	irqRestore(flags);
	_go32_dpmi_free_iret_wrapper(&g_newVector);
}

// The handler is in; learns how an interrupt that came in our code looks
// (see readFrame()) while this spins in protected mode, and decides
// where the timer procs may run.
static void calibrate() {
	// Without an FPU its instructions trap to DJGPP's emulator, which keeps
	// its state in static memory: a timer proc (and runProcs()' fnsave)
	// in an interrupt that came in while the main thread was inside the
	// emulator would corrupt it. The procs run on the main thread instead.
	if (DOS::fpuEmulated()) {
		g_isr.procsMode = kProcsOnMainThread;
		return;
	}
	if (DOS::lockedAll()) {
		// Not all of it (lockAll() said so): no interrupt may run them.
		g_isr.procsMode = DOS::lockedAllComplete() ? kProcsAlways : kProcsOnMainThread;
		return;
	}
	uint16 cs, ss;
	__asm__ __volatile__("movw %%cs, %0; movw %%ss, %1" : "=r"(cs), "=r"(ss));
	g_isr.ourCs = cs;
	g_isr.ourSs = ss;
	g_isr.calSeen = 0;
	g_isr.calMatched = 0;
	g_isr.procsMode = kProcsCalibrating;
	// Ticks come every millisecond; this bound only guards a PIT that
	// never interrupts.
	for (uint32 spin = 0; g_isr.calSeen < kCalibrationTicks && spin < 200000000; ++spin)
		__asm__ __volatile__("" : : : "memory");
	// Every one of them must show the same stub and our CS:SS. And the
	// stacks DJGPP's interrupt wrappers switch to (__djgpp_ds_alias) must
	// not look like ours: that is what keeps a tick that nests in another
	// handler from passing for one that came in our code.
	const char *why = nullptr;
	const char *test = getenv("SCUMMVM_DOS_LOCKTEST");
	if (test && strcmp(test, "calibration") == 0)
		why = "SCUMMVM_DOS_LOCKTEST=calibration";
	else if (g_isr.calSeen < kCalibrationTicks)
		why = "IRQ0 never came";
	else if (g_isr.calMatched != g_isr.calSeen)
		why = "IRQ0 frames not as expected";
	else if (__djgpp_ds_alias == ss)
		why = "interrupt stacks share our SS";
	if (!why) {
		g_isr.procsMode = kProcsFromOurCode;
		return;
	}
	// Nothing here can tell where an interrupt came from. Lock everything
	// instead, as under any other host, and run the procs from every
	// IRQ0 (no timer proc can run yet: g_isr.timer is null) -- unless
	// some of it could not be locked: CWSDPMI may then page a proc's
	// memory in an interrupt that came in real mode, which is fatal, so
	// they stay on the main thread.
	g_isr.procsMode = kProcsOnMainThread;
	warning("DOS: %s under %s (%u of %u); locking all memory",
		why, DOS::dpmiHost(), (uint)g_isr.calMatched, (uint)g_isr.calSeen);
	if (DOS::lockAll(why))
		g_isr.procsMode = kProcsAlways;
}

// The TSC's rate in PIT periods, for runProcs()'s catch-up: two runs of
// kTscCalTicks ticks each, which must agree within 1/64. Without a TSC,
// or if they do not agree (a TSC that does not run at a steady rate),
// there is no catch-up. Runs on the main thread with interrupts on, before
// any timer proc can run.
static const uint32 kTscCalTicks = 25;

static void calibrateTsc() {
	if (!DOS::haveTsc() || !DosTimerManager::interruptsEnabled())
		return;
	uint64 at[3];
	uint32 t = g_isr.ticks;
	for (uint32 spin = 0; g_isr.ticks == t && spin < 100000000; ++spin)
		__asm__ __volatile__("" : : : "memory");
	for (int i = 0; i < 3; ++i) {
		t = g_isr.ticks;
		at[i] = DOS::irqRdtsc();
		if (i == 2)
			break;
		for (uint32 spin = 0; g_isr.ticks - t < kTscCalTicks && spin < 400000000; ++spin)
			__asm__ __volatile__("" : : : "memory");
		if (g_isr.ticks - t != kTscCalTicks) {
			warning("DOS: no TSC catch-up: IRQ0 ticks did not come as expected");
			return;
		}
	}
	const uint64 a = (at[1] - at[0]) / kTscCalTicks, b = (at[2] - at[1]) / kTscCalTicks;
	const uint64 diff = a > b ? a - b : b - a;
	if (!a || !b || diff > a / 64 || a > 0xFFFFFFFFull) {
		warning("DOS: no TSC catch-up: TSC per tick %u then %u", (uint)a, (uint)b);
		return;
	}
	const uint32 perTick = (uint32)((a + b) / 2);
	const uint64 cap = (uint64)perTick * kCatchUpMaxTicks;
	g_isr.tscCap = cap > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32)cap;
	for (uint i = 0; i < ARRAYSIZE(kStallMs); ++i) {
		const uint64 v = (uint64)perTick * kStallMs[i];
		g_isr.stallTsc[i] = v > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32)v;
	}
	g_isr.tscPerTick = perTick;
	debug(1, "DOS: TSC %u per IRQ0 tick (%u, %u): lost ticks caught up", (uint)perTick, (uint)a, (uint)b);
}

static bool install() {
	if (g_installed)
		return true;
	{
		const void *const fns[1] = { (const void *)timerIsr };
		DOS::lockIrqCode(dosIrqBegin_timer, dosIrqEnd_timer, fns, ARRAYSIZE(fns), "IRQ0");
	}
	DOS::lockIrqData(&g_isr, sizeof(g_isr));

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
	g_isr.procsCtx = DosTimerManager::kProcsNone;
	g_isr.procsMode = kProcsCalibrating;
	g_isr.timer = nullptr;
	g_isr.procRuns = g_isr.procRunsAfterCall = g_isr.procDeferred = 0;
	g_isr.maxWait = g_isr.waits10 = 0;
	g_isr.tscPerTick = 0;
	g_isr.biosOwed = g_isr.ticksCredited = g_isr.biosTicks = 0;
	for (uint i = 0; i < ARRAYSIZE(g_isr.stalls); ++i)
		g_isr.stalls[i] = 0;
	g_isr.stallMax = 0;

	// Timer procs run on the wrapper's own stack (malloc'd and locked by
	// libc, as is the wrapper); SCI's music code is deeper than the 32 KB
	// default comfortably allows.
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
	calibrate();
	calibrateTsc();
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
			g_isr.biosTicks++;
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

DosTimerManager::ProcsContext DosTimerManager::procsContext() {
	return (ProcsContext)g_isr.procsCtx;
}

bool DosTimerManager::procsOnMainThread() {
	return !g_installed || g_isr.procsMode == kProcsOnMainThread;
}

void DosTimerManager::logStats() {
	if (!g_installed)
		return;
	static const char *const kModes[] = { "calibrating", "from our code", "always (all locked)", "main thread" };
	debug(1, "DOS: timer procs %s: ran %u (in IRQ0 %u, as a real-mode call returned %u), ticks deferred %u, "
		"longest wait %u ms, waits of 10 ms or more %u",
		kModes[g_isr.procsMode], (uint)g_isr.procRuns, (uint)(g_isr.procRuns - g_isr.procRunsAfterCall),
		(uint)g_isr.procRunsAfterCall, (uint)g_isr.procDeferred, (uint)g_isr.maxWait, (uint)g_isr.waits10);
	if (!g_isr.tscPerTick)
		return;
	// handler()'s run times in IRQ0, by bin: <1, <2, <5, <20, <50, <200 ms, and more.
	Common::String bins;
	for (uint i = 0; i < ARRAYSIZE(g_isr.stalls); ++i)
		bins += Common::String::format("%s%u", i ? "/" : "", (uint)g_isr.stalls[i]);
	// The BIOS's due: one tick per 65536 PIT clocks (the accumulator
	// starts at 0 with the tick count).
	debug(1, "DOS: timer stalls in IRQ0 bins=%s max_us=%u; ticks caught up %u; BIOS ticks %u + owed %u of %u due",
		bins.c_str(), (uint)((uint64)g_isr.stallMax * 1000 / g_isr.tscPerTick), (uint)g_isr.ticksCredited,
		(uint)g_isr.biosTicks, (uint)g_isr.biosOwed, (uint)((uint64)g_isr.ticks * DOS::kPitDivisor / 65536));
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
	if (picRead(0x0B) & 0x01) {
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
