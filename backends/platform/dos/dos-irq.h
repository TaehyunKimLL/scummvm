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

#ifndef BACKENDS_PLATFORM_DOS_DOS_IRQ_H
#define BACKENDS_PLATFORM_DOS_DOS_IRQ_H

#include "common/scummsys.h"

/*
 * Memory that interrupt handlers touch.
 *
 * Nothing is locked as a whole: CWSDPMI may page out any code or data.
 * A page fault is fatal in a hardware interrupt that came in real mode
 * (DOS may be busy) and in a real-mode callback, so whatever runs there
 * is locked on its own: code with DOS_IRQ_CODE, data with lockIrqData().
 *
 * DOS_IRQ_CODE puts a function in the section .text.dosirq_b. A source
 * file that uses it starts with DOS_IRQ_CODE_RANGE(tag), ahead of every
 * function: one top-level asm statement opens .text.dosirq_a (a begin
 * label), .text.dosirq_b and .text.dosirq_c (an end label) in that order.
 * GCC emits top-level asm before any function, the object keeps its
 * sections in the order they were opened, and the link (*(.text.*))
 * keeps each object's order, so the file's IRQ code ends up between the
 * two labels whatever order GCC gave the functions themselves.
 *
 * IRQ code may call only DOS_IRQ_CODE functions of its own file and
 * always_inline helpers (below), and must not use a switch statement:
 * its jump table would go to plain .text.
 */
#define DOS_IRQ_CODE __attribute__((section(".text.dosirq_b")))

#define DOS_IRQ_CODE_RANGE(tag) \
	__asm__(".section .text.dosirq_a,\"x\"\n" \
		"_dosIrqBegin_" #tag ":\n\t.byte 0xc3\n" \
		".section .text.dosirq_b,\"x\"\n" \
		".section .text.dosirq_c,\"x\"\n" \
		"_dosIrqEnd_" #tag ":\n\t.byte 0xc3\n" \
		".text\n"); \
	extern "C" char dosIrqBegin_##tag[], dosIrqEnd_##tag[]

namespace DOS {

// Port and far memory access for IRQ code, inlined whatever the
// optimisation level (pc.h's and farptr.h's are extern inline: GCC may
// call libc's copies, which are not locked).
static inline __attribute__((always_inline)) uint8 irqIn8(uint16 port) {
	uint8 v;
	__asm__ __volatile__("inb %1, %0" : "=a"(v) : "Nd"(port));
	return v;
}

static inline __attribute__((always_inline)) void irqOut8(uint16 port, uint8 v) {
	__asm__ __volatile__("outb %0, %1" : : "a"(v), "Nd"(port));
}

static inline __attribute__((always_inline)) uint32 irqPeek32(uint16 selector, uint32 offset) {
	uint32 v;
	__asm__ __volatile__("movw %w1, %%fs\n\tmovl %%fs:(%2), %0" : "=r"(v) : "rm"(selector), "r"(offset));
	return v;
}

/**
 * Locks the IRQ code between @p begin and @p end (a DOS_IRQ_CODE_RANGE's
 * labels) once each of the @p count functions in @p fns is seen to lie
 * between them. If one does not, the link did not lay the sections out
 * as expected, and the whole of .text is locked instead. @p what names
 * the handler in the log.
 */
void lockIrqCode(const char *begin, const char *end, const void *const *fns, uint count, const char *what);

/** Locks @p size bytes at @p p; logs a failure. */
bool lockIrqData(const volatile void *p, uint32 size);

/**
 * Picks the memory regime, first thing in main(). Under CWSDPMI nothing
 * is locked beyond the handlers' own memory. Under any other DPMI host,
 * whose behaviour on a page fault in an interrupt is not known here, the
 * image and the heap are locked as a whole, and every later sbrk() too.
 */
void chooseLockRegime();

/** True when everything is locked (any host but CWSDPMI). */
bool lockedAll();

/** The DPMI host's name and version as DPMI 1.0 function 0x401 gives them ("CWSDPMI 7.0"), or "DPMI 0.9". */
const char *dpmiHost();

} // End of namespace DOS

#endif
