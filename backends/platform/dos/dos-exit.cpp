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


// Port I/O, DOS handles and the BLASTER variable on the way out.
#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include "common/scummsys.h"

#if defined(DOS_DJGPP)

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <sys/farptr.h>
#include <sys/movedata.h>

#include "backends/platform/dos/dos-exit.h"
#include "backends/platform/dos/exit-trace.h"
#include "backends/platform/dos/blaster.h"
#include "backends/timer/dos/dos-timer.h"
#include "common/util.h"

#include <SDL3/SDL.h>

namespace DOS {

namespace {

const char kExitLog[] = "EXITLOG.TXT";
const int kTraceRow = 24;
const uint8 kTraceAttr = 0x4F;	// white on red

bool g_traceOn = false;
ExitTraceLine g_line;
bool g_masksSaved = false;
uint8 g_savedMaster = 0xFF;
uint8 g_savedSlave = 0xFF;
bool g_blasterOpen = false;
int g_blasterPort = -1;		// as SDL3 logged them (noteSoundBlasterConfig())
int g_blasterIrq = -1;

inline uint32 irqSave() {
	uint32 flags;
	__asm__ __volatile__("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
	return flags;
}

inline void irqRestore(uint32 flags) {
	if (flags & 0x200)
		__asm__ __volatile__("sti" : : : "memory");
}

void drawLine() {
	char text[ExitTraceLine::kCols];
	g_line.render(text);
	for (int c = 0; c < ExitTraceLine::kCols; ++c)
		_farpokew(_dos_ds, 0xB8000 + (kTraceRow * ExitTraceLine::kCols + c) * 2,
				  (uint16)((uint8)text[c] | (kTraceAttr << 8)));
}

// INT 21h itself, through the transfer buffer: the last mark comes after
// exit() has taken libc's file layer down (a libc call there crashed).
bool dos21(__dpmi_regs &r) {
	r.x.ds = (__tb >> 4);
	__dpmi_int(0x21, &r);
	return !(r.x.flags & 1);
}

// One DOS open, seek, write and close: nothing stays buffered. With
// @p create the file is made afresh (empty) first.
bool appendLine(const char *buf, int len, bool create = false) {
	const int nameLen = sizeof(kExitLog);
	dosmemput(kExitLog, nameLen, __tb);
	dosmemput(buf, len, __tb + nameLen);
	__dpmi_regs r;
	memset(&r, 0, sizeof(r));
	r.x.ax = create ? 0x3C00 : 0x3D01;	// create (CX: attributes) or open for writing
	r.x.dx = __tb & 15;
	if (!dos21(r))
		return false;
	const uint16 handle = r.x.ax;
	memset(&r, 0, sizeof(r));
	r.x.ax = 0x4202;	// to the end
	r.x.bx = handle;
	bool ok = dos21(r);
	if (ok) {
		memset(&r, 0, sizeof(r));
		r.h.ah = 0x40;
		r.x.bx = handle;
		r.x.cx = len;
		r.x.dx = (__tb & 15) + nameLen;
		ok = dos21(r) && r.x.ax == len;
	}
	memset(&r, 0, sizeof(r));
	r.h.ah = 0x3E;
	r.x.bx = handle;
	dos21(r);
	return ok;
}

char *putHex(char *p, uint8 v) {
	static const char kHex[] = "0123456789ABCDEF";
	*p++ = kHex[v >> 4];
	*p++ = kHex[v & 15];
	return p;
}

char *putText(char *p, const char *s) {
	while (*s)
		*p++ = *s++;
	return p;
}

// Both PICs' mask, in-service and request registers (OCW3 0x0B, 0x0A),
// as "mask=21/A1 isr=../.. irr=../..". 31 characters and a terminator.
void picState(char *out) {
	uint8 v[6];
	const uint32 flags = irqSave();
	v[0] = inportb(0x21);
	v[1] = inportb(0xA1);
	outportb(0x20, 0x0B);
	v[2] = inportb(0x20);
	outportb(0xA0, 0x0B);
	v[3] = inportb(0xA0);
	outportb(0x20, 0x0A);
	v[4] = inportb(0x20);
	outportb(0xA0, 0x0A);
	v[5] = inportb(0xA0);
	irqRestore(flags);
	static const char *const kNames[3] = { "mask=", " isr=", " irr=" };
	char *p = out;
	for (int i = 0; i < 3; ++i) {
		p = putText(p, kNames[i]);
		p = putHex(p, v[i * 2]);
		*p++ = '/';
		p = putHex(p, v[i * 2 + 1]);
	}
	*p = 0;
}

// The VCPI server's idea of the PIC vector bases (EMM386 and the like,
// with CWSDPMI as their client), as " vcpi=MM/SS", or " vcpi=none".
void vcpiState(char *out) {
	char *p = putText(out, " vcpi=");
	const uint32 vec = _farpeekl(_dos_ds, 0x67 * 4);
	char sig[8];
	bool emm = false;
	if (vec >> 16) {
		dosmemget((vec >> 16) * 16 + 0x0A, sizeof(sig), sig);
		emm = memcmp(sig, "EMMXXXX0", 8) == 0 || memcmp(sig, "EMMQXXX0", 8) == 0;
	}
	__dpmi_regs r;
	memset(&r, 0, sizeof(r));
	if (emm) {
		r.x.ax = 0xDE00;	// VCPI installed?
		__dpmi_int(0x67, &r);
	}
	if (emm && r.h.ah == 0) {
		memset(&r, 0, sizeof(r));
		r.x.ax = 0xDE0A;	// get the 8259A vector mappings
		__dpmi_int(0x67, &r);
	}
	if (emm && r.h.ah == 0) {
		p = putHex(p, r.h.bl);
		*p++ = '/';
		p = putHex(p, r.h.cl);
	} else {
		p = putText(p, emm ? "no" : "none");
	}
	*p = 0;
}

} // End of anonymous namespace

void saveIrqMasks() {
	g_savedMaster = inportb(0x21);
	g_savedSlave = inportb(0xA1);
	g_masksSaved = true;
}

void exitTraceStart(bool on) {
	g_traceOn = on;
	if (!on)
		return;
	// What the PIC and the VCPI server look like now, to set the last
	// mark's (on _exit) against.
	char what[80];
	char *p = putText(what, "trace on: at start ");
	p = putHex(p, g_savedMaster);
	*p++ = '/';
	p = putHex(p, g_savedSlave);
	*p++ = ' ';
	picState(p);
	vcpiState(p + strlen(p));
	char buf[96];
	const int n = formatExitLogLine(buf, sizeof(buf), 0, what);
	appendLine(buf, n, true);
}

void exitMark(int step, const char *what) {
	if (!g_traceOn)
		return;
	g_line.add(step);
	drawLine();
	// A DOS call turns interrupts on: not from where they are off.
	uint32 flags;
	__asm__ __volatile__("pushfl; popl %0" : "=r"(flags));
	if (!(flags & 0x200))
		return;
	char buf[96];
	const int n = formatExitLogLine(buf, sizeof(buf), step, what);
	if (appendLine(buf, n)) {
		g_line.markSaved();
		drawLine();
	}
}

void noteSoundBlasterOpen() {
	g_blasterOpen = true;
}

void noteSoundBlasterConfig(int port, int irq) {
	if (port >= 0)
		g_blasterPort = port;
	if (irq >= 0)
		g_blasterIrq = irq;
}

void soundBlasterClosed() {
	// Only once SDL3 has closed the device and joined its thread: with the
	// audio subsystem still up (initialised twice, say) the card is live.
	if (!g_blasterOpen || SDL_WasInit(SDL_INIT_AUDIO))
		return;
	g_blasterOpen = false;
	// The port and IRQ SDL3 used (its log), else our reading of BLASTER.
	const BlasterConfig blaster = parseBlaster(getenv("BLASTER"));
	if (g_blasterPort < 0 && !blaster.present)
		return;
	const uint16 base = g_blasterPort >= 0 ? (uint16)g_blasterPort : blaster.port;
	const int irq = g_blasterIrq >= 0 ? g_blasterIrq : blaster.irq;
	// DSP reset: 1, at least 3 us, 0, then the 0xAA it answers with,
	// within 100 us on a real card. The pulse is timed on the PIT
	// (uclock(), 0.84 us a count), with port 80h reads (ISA timing on
	// almost every chipset) as the floor and a bound should the clock
	// not move.
	outportb(base + 0x6, 1);
	const uclock_t t0 = uclock();
	for (int i = 0; i < 1000 && (i < 4 || uclock() - t0 < 6); ++i)
		(void)inportb(0x80);
	outportb(base + 0x6, 0);
	for (int i = 0; i < 100000 && !(inportb(base + 0xE) & 0x80); ++i)
		;
	if (inportb(base + 0xE) & 0x80)
		(void)inportb(base + 0xA);
	// An interrupt the card raised after the handler went: acknowledged
	// (8-bit, and the SB16's 16-bit one).
	(void)inportb(base + 0xE);
	(void)inportb(base + 0xF);
	if (!g_masksSaved)
		return;
	uint8 master, slave;
	irqPicBits(irq, master, slave);
	const uint32 flags = irqSave();
	if (master)
		outportb(0x21, picMaskRestore(inportb(0x21), g_savedMaster, master));
	if (slave)
		outportb(0xA1, picMaskRestore(inportb(0xA1), g_savedSlave, slave));
	irqRestore(flags);
}

namespace {

// The timer out (if it still is in) and the PIT in the mode the BIOS runs
// it in (3, a square wave, 18.2 Hz), not DJGPP uclock()'s 2. No heap.
void stopTimerHardware() {
	DosTimerManager::stopHardware();
	uint32 flags;
	__asm__ __volatile__("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
	outportb(0x43, 0x36);
	outportb(0x40, 0);
	outportb(0x40, 0);
	if (flags & 0x200)
		__asm__ __volatile__("sti" : : : "memory");
}

// DJGPP's default action for these (a traceback, then libc's own _exit,
// which --wrap does not reach: it is called inside libc) would leave
// IRQ0 on our handler at 1 kHz. The hardware first, then that action.
void fatalSignal(int sig) {
	stopTimerHardware();
	exitMark(kExitSignal, "fatal signal: timer out, then DJGPP's traceback");
	signal(sig, SIG_DFL);
	raise(sig);
}

} // End of anonymous namespace

void installExitSignals() {
	// SIGINT is Ctrl-C and Ctrl-Break alike.
	static const int kSignals[] = { SIGABRT, SIGSEGV, SIGFPE, SIGILL, SIGINT, SIGQUIT };
	for (uint i = 0; i < ARRAYSIZE(kSignals); ++i)
		signal(kSignals[i], fatalSignal);
}

} // End of namespace DOS

// exit() (once the atexit handlers, stdio and the destructors are done)
// and libc's out-of-memory exits end in DJGPP's _exit(), linked with
// --wrap=_exit (module.mk). DJGPP's traceback exits (a crash, a signal's
// default action) call it from inside libc, past the wrap: those go
// through DOS::installExitSignals()'s handler instead.
extern "C" void __real__exit(int code) __attribute__((noreturn));
extern "C" void __wrap__exit(int code) {
	DOS::stopTimerHardware();
	if (DOS::g_traceOn) {
		char what[72] = "_exit, then CWSDPMI and DOS: ";
		DOS::picState(what + strlen(what));
		DOS::exitMark(DOS::kExitDjgppExit, what);
	}
	__real__exit(code);
}

#endif
