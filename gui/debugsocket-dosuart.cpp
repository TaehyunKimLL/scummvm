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

#include <dpmi.h>
#include <go32.h>
#include <pc.h>

#include "gui/debugsocket-dosuart.h"
#include "backends/platform/dos/dos-exit.h"
#include "backends/platform/dos/dos-irq.h"
#include "common/debug.h"
#include "common/textconsole.h"
#include "common/util.h"

DOS_IRQ_CODE_RANGE(uart);

namespace GUI {

static const uint16 kComBase[4] = { 0x3F8, 0x2F8, 0x3E8, 0x2E8 };
static const int kComIrq[4] = { 4, 3, 4, 3 };

// The ring the IRQ handler fills and read() drains. One UART at a time:
// the debug socket opens one COM port. Locked in open(), with the rest of
// what the handler touches.
static const uint kRingSize = 4096;	// a power of two
static volatile byte g_ring[kRingSize];
static volatile uint g_ringHead = 0;	// written by the handler
static volatile uint g_ringTail = 0;	// written by read()
static volatile uint g_overruns = 0;
static volatile uint32 g_irqs = 0;		// uartIsr() calls, for DosUart::irqCount()
static uint16 g_isrBase = 0;
static _go32_dpmi_seginfo g_oldVector, g_newVector;

// What teardown() needs, kept outside the object: OSystem_DOS::quit() and
// fatalError() leave through exit(), which runs atexit() handlers but not
// the debugger's destructor.
static uint16 g_openBase = 0;		// 0: nothing hooked
static int g_openIrq = 0;
static bool g_irqWasMasked = true;
static bool g_atexitDone = false;

// Interrupts off, remembering whether they were on; the "memory" clobber
// keeps the compiler from moving ring accesses across either end.
static inline uint32 irqSave() {
	uint32 flags;
	__asm__ __volatile__("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
	return flags;
}
static inline void irqRestore(uint32 flags) {
	if (flags & 0x200)
		__asm__ __volatile__("sti" : : : "memory");
}

// Moves whatever the receive FIFO holds into the ring. Called by the
// handler, and by read() with interrupts off.
static DOS_IRQ_CODE void drainFifo() {
	byte lsr;
	while ((lsr = DOS::irqIn8(g_isrBase + 5)) & 0x01) {
		const byte c = DOS::irqIn8(g_isrBase);
		if (lsr & 0x02)
			g_overruns++;
		const uint next = (g_ringHead + 1) & (kRingSize - 1);
		if (next != g_ringTail) {
			g_ring[g_ringHead] = c;
			g_ringHead = next;
		} else {
			g_overruns++;
		}
	}
}

// The handler does not chain to the previous one: a device sharing the IRQ
// (a mouse on COM3, IRQ 4) would starve. Fine for a debug transport only.
static DOS_IRQ_CODE void uartIsr() {
	// Serve every cause the UART reports, until it reports none: the IRQ
	// line is edge-triggered at the PIC, so a cause left pending keeps the
	// line up and no further interrupt ever arrives. No switch here: its
	// jump table would land outside the locked code.
	byte iir;
	g_irqs++;
	for (int guard = 0; guard < 16 && !((iir = DOS::irqIn8(g_isrBase + 2)) & 0x01); guard++) {
		const byte cause = iir & 0x0E;
		if (cause == 0x06)
			(void)DOS::irqIn8(g_isrBase + 5);	// line status
		else if (cause == 0x00)
			(void)DOS::irqIn8(g_isrBase + 6);	// modem status
		// 0x02 transmitter empty: not enabled; the rest: data or character timeout
		drainFifo();
	}
	drainFifo();
	DOS::irqOut8(0x20, 0x20);	// EOI to the master PIC (IRQ 3 and 4 live there)
}

static void teardown();
static void teardownAtExit();

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
	// A second open() would save our own handler as the one to restore.
	if (g_openBase)
		teardown();
	const uint16 base = kComBase[s[3] - '1'];
	const int irq = kComIrq[s[3] - '1'];
	outportb(base + 7, 0x5A);		// scratch register: is there a UART at all?
	if (inportb(base + 7) != 0x5A) {
		warning("DebugSocket: no UART at 0x%X", base);
		return false;
	}
	const uint16 div = (uint16)(115200 / baud);
	outportb(base + 1, 0x00);		// no interrupts while it is set up
	outportb(base + 3, 0x80);		// DLAB
	outportb(base + 0, div & 0xFF);
	outportb(base + 1, div >> 8);
	outportb(base + 3, 0x03);		// 8N1
	// FIFO on, both cleared, interrupt at the first byte: a short line
	// under a higher trigger level would rest on the UART's character
	// timeout, which an emulator need not raise.
	outportb(base + 2, 0x07);

	g_isrBase = base;
	g_ringHead = g_ringTail = 0;
	g_overruns = 0;
	{
		const void *const fns[2] = { (const void *)drainFifo, (const void *)uartIsr };
		DOS::lockIrqCode(dosIrqBegin_uart, dosIrqEnd_uart, fns, ARRAYSIZE(fns), "COM");
	}
	DOS::lockIrqData(g_ring, sizeof(g_ring));
	DOS::lockIrqData(&g_ringHead, sizeof(g_ringHead));
	DOS::lockIrqData(&g_ringTail, sizeof(g_ringTail));
	DOS::lockIrqData(&g_overruns, sizeof(g_overruns));
	DOS::lockIrqData(&g_irqs, sizeof(g_irqs));
	DOS::lockIrqData(&g_isrBase, sizeof(g_isrBase));
	const int vec = 8 + irq;
	_go32_dpmi_get_protected_mode_interrupt_vector(vec, &g_oldVector);
	g_newVector.pm_offset = (unsigned long)uartIsr;
	g_newVector.pm_selector = _go32_my_cs();
	if (_go32_dpmi_allocate_iret_wrapper(&g_newVector)) {
		warning("DebugSocket: no IRET wrapper for IRQ %d", irq);
		return false;
	}
	_go32_dpmi_set_protected_mode_interrupt_vector(vec, &g_newVector);

	outportb(base + 4, 0x0B);		// DTR, RTS, OUT2 (gates the IRQ line)
	(void)inportb(base + 5);		// clear pending line, modem and data state
	(void)inportb(base + 6);
	while (inportb(base + 5) & 0x01)
		(void)inportb(base);
	(void)inportb(base + 2);
	outportb(base + 1, 0x01);		// interrupt on received data
	const byte mask = inportb(0x21);
	g_irqWasMasked = (mask & (1 << irq)) != 0;
	g_openBase = base;
	g_openIrq = irq;
	if (!g_atexitDone) {
		atexit(teardownAtExit);
		g_atexitDone = true;
	}
	outportb(0x21, mask & ~(1 << irq));

	_base = base;
	_irq = irq;
	debug(1, "DebugSocket: COM at 0x%X, IRQ %d, %u baud", base, irq, (uint)baud);
	return true;
}

static void teardownAtExit() {
	DOS::exitMark(DOS::kExitAtexitUart, "atexit: debug socket COM port");
	teardown();
}

// Idempotent: from close() (the destructor) and from exit() alike.
static void teardown() {
	if (!g_openBase)
		return;
	const uint32 flags = irqSave();
	if (g_irqWasMasked)
		outportb(0x21, inportb(0x21) | (1 << g_openIrq));
	outportb(g_openBase + 1, 0x00);
	outportb(g_openBase + 4, 0x03);		// OUT2 off, DTR and RTS stay
	_go32_dpmi_set_protected_mode_interrupt_vector(8 + g_openIrq, &g_oldVector);
	irqRestore(flags);
	_go32_dpmi_free_iret_wrapper(&g_newVector);
	g_openBase = 0;
}

void DosUart::close() {
	if (!_base)
		return;
	teardown();
	if (g_overruns)
		warning("DebugSocket: %u bytes lost to receive overruns", (uint)g_overruns);
	_base = 0;
}

uint32 DosUart::irqCount() {
	return g_irqs;
}

int DosUart::read(char *buf, int max) {
	// The handler is what keeps up with the line. Draining here as well is
	// the backstop: should an interrupt ever be missed, with a cause left
	// pending and the IRQ line stuck high, this poll empties the FIFO and
	// the next byte raises the line again.
	const uint32 flags = irqSave();
	drainFifo();
	irqRestore(flags);
	int n = 0;
	while (n < max && g_ringTail != g_ringHead) {
		buf[n++] = (char)g_ring[g_ringTail];
		g_ringTail = (g_ringTail + 1) & (kRingSize - 1);
	}
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
